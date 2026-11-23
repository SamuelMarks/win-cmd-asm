//! Deterministic 64-bit `AArch64` (`ARMv8.0-A` + `PAC`/`BTI`) `PE32+` (`0xAA64`) coverage VM.
//!
//! Enforces strict Windows ARM64 `X18` (`TEB`) immutability, hardware `16`-byte `SP` alignment
//! at every `SP`-relative memory access, and collects instruction & conditional-branch coverage.

use crate::CoverageData;
use crate::arm32_pe::SparseGuestMem;
use crate::win_mock::{BS, DQ, GuestMem, SharedWinEnv, WinProcState, emu_err};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::rc::Rc;
use wincmd_diag::{ErrCode, ToolError};

const THUNK_BASE: u64 = 0x0000_7FF7_F000_0000;
const THUNK_END: u64 = 0x0000_7FF7_F001_0000;
const EXIT_LR: u64 = 0x0000_7FF7_FFFF_FFE0;
const TEB_ADDR: u64 = 0x0000_0070_0000_0000;
const PEB_ADDR: u64 = 0x0000_0070_0000_1000;
const CMDLINE_ADDR: u64 = 0x0000_0070_0000_2000;
const ENV_ADDR: u64 = 0x0000_0070_0001_0000;
const STACK_BASE: u64 = 0x0000_0070_00FC_0000;
const STACK_TOP: u64 = 0x0000_0070_0100_0000;
const HEAP_START: u64 = 0x0000_0200_0000_0000;

struct Pe64Hdr {
    num_secs: usize,
    sec_table: usize,
    entry_rva: u32,
    image_base: u64,
    size_of_image: u32,
    size_of_headers: usize,
    imp_rva: u32,
}

/// Execution result of running `WINCMD_ARM64.EXE` inside [`Arm64Vm`].
#[derive(Clone, Debug)]
pub struct Arm64RunResult {
    /// Final process exit code (`ExitProcess` / `TerminateProcess` / `w0`).
    pub exit_code: u32,
    /// Captured UTF-8 `stdout` stream.
    pub stdout: String,
    /// Captured UTF-8 `stderr` stream.
    pub stderr: String,
    /// Total retired 32-bit `AArch64` instructions.
    pub steps: u64,
    /// Recorded RVA & conditional-branch coverage telemetry.
    pub coverage: CoverageData,
}

/// Deterministic 64-bit Windows `ARM64` (`0xAA64` `PE32+`) User-Mode Virtual Machine.
pub struct Arm64Vm {
    /// Raw PE32+ image bytes (retained to spawn nested `CreatePipe` / `CMD /C` subshells!).
    pe_bytes: Vec<u8>,
    /// Loaded `ImageBase` (`0x140000000`).
    pub image_base: u64,
    /// General-purpose 64-bit registers `X0..X30` (`X18` = `TEB` read-only, `X29` = `FP`, `X30` = `LR`).
    pub xregs: [u64; 31],
    /// Hardware 64-bit stack pointer `SP` (must be `16`-byte aligned on every `SP` memory access!).
    pub sp: u64,
    /// Program counter `PC` (4-byte aligned).
    pub pc: u64,
    /// `PSTATE` condition flags `(N, Z, C, V)`.
    pub nzcv: (bool, bool, bool, bool),
    /// Sparse 64-bit guest virtual address space.
    pub mem: SparseGuestMem,
    /// Simulated Windows 10 / 11 ARM64 OS & VFS context.
    pub os: WinProcState,
    /// Synthetic import thunk address -> API name.
    thunks: BTreeMap<u64, String>,
    /// Next free synthetic thunk slot.
    next_thunk: u64,
    /// Instruction & conditional-branch coverage telemetry.
    pub coverage: CoverageData,
    /// Total retired instructions.
    pub steps: u64,
}

impl Arm64Vm {
    /// Loads a `PE32+` `ARM64` (`0xAA64`) executable image into a fresh Windows 11 ARM64 VM.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if the `PE32+` header or machine type (`0xAA64`) is invalid.
    pub fn from_pe_bytes(
        pe: &[u8],
        cmdline_tail: &str,
        env_kv: &[(&str, &str)],
        shared: Rc<RefCell<SharedWinEnv>>,
    ) -> Result<Self, ToolError> {
        let hdr = Self::parse_pe64_hdr(pe)?;

        let mut mem = SparseGuestMem::new(HEAP_START);
        Self::map_pe64_sections(
            &mut mem,
            pe,
            hdr.image_base,
            hdr.size_of_image,
            hdr.size_of_headers,
            hdr.sec_table,
            hdr.num_secs,
        )?;

        let (thunks, next_thunk) = Self::bind_pe64_imports(&mut mem, hdr.image_base, hdr.imp_rva)?;

        // Map 64-bit TEB/PEB + command line + environment + 256 KiB thread stack
        mem.map_range(TEB_ADDR, 0x3_0000);
        mem.map_range(STACK_BASE, STACK_TOP - STACK_BASE);

        let os_ver = (10, 0, 22631);
        let mut os = WinProcState::new(false, true, os_ver, shared);

        // Populate 64-bit Windows ARM64 TEB (`[X18 + 0x60] = PEB`) and PEB OS version fields
        mem.write_u64_le(TEB_ADDR + 0x30, TEB_ADDR)?;
        mem.write_u64_le(TEB_ADDR + 0x60, PEB_ADDR)?;
        mem.write_u32_le(PEB_ADDR + 0x118, os_ver.0)?;
        mem.write_u32_le(PEB_ADDR + 0x11C, os_ver.1)?;
        mem.write_u16_le(PEB_ADDR + 0x120, u16::try_from(os_ver.2).unwrap_or(0))?;

        Self::populate_startup_env(&mut mem, &mut os, cmdline_tail, env_kv)?;

        let mut xregs = [0u64; 31];
        xregs[18] = TEB_ADDR; // Windows ARM64 ABI: X18 permanently points to TEB!
        xregs[30] = EXIT_LR;
        let sp = STACK_TOP - 32; // 16-byte aligned SP!
        let pc = hdr.image_base.wrapping_add(u64::from(hdr.entry_rva));

        let pe_clone = pe.to_vec();
        os.child_runner = Some(Box::new(move |sub_cmd, sh, h_in, h_out, h_err| {
            let mut child = Self::from_pe_bytes(&pe_clone, sub_cmd, &[], sh)?;
            let _ = child.os.bind_inherited_stdio(0, h_in);
            let _ = child.os.bind_inherited_stdio(1, h_out);
            let _ = child.os.bind_inherited_stdio(2, h_err);
            let child_out = child.run(3_000_000)?;
            Ok((child_out.exit_code, child.os.stdout, child.os.stderr))
        }));

        Ok(Self {
            pe_bytes: pe.to_vec(),
            image_base: hdr.image_base,
            xregs,
            sp,
            pc,
            nzcv: (false, false, false, false),
            mem,
            os,
            thunks,
            next_thunk,
            coverage: CoverageData::default(),
            steps: 0,
        })
    }

    fn parse_pe64_hdr(pe: &[u8]) -> Result<Pe64Hdr, ToolError> {
        if pe.len() < 0x120 || &pe[0..2] != b"MZ" {
            return Err(emu_err(0, ErrCode::BadExeFormat, "invalid MZ header"));
        }
        let lfanew = usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]]))
            .unwrap_or(0);
        if lfanew + 0x108 > pe.len() || &pe[lfanew..lfanew + 4] != b"PE\0\0" {
            return Err(emu_err(0, ErrCode::BadExeFormat, "invalid PE signature"));
        }
        let fh = lfanew + 4;
        let machine = u16::from_le_bytes([pe[fh], pe[fh + 1]]);
        if machine != 0xAA64 {
            return Err(emu_err(
                0,
                ErrCode::BadExeFormat,
                format!("unsupported ARM64 PE machine 0x{machine:04X}"),
            ));
        }
        let num_secs = usize::from(u16::from_le_bytes([pe[fh + 2], pe[fh + 3]]));
        let opt_size = usize::from(u16::from_le_bytes([pe[fh + 16], pe[fh + 17]]));
        let opt = fh + 20;
        let magic = u16::from_le_bytes([pe[opt], pe[opt + 1]]);
        if magic != 0x020B {
            return Err(emu_err(
                0,
                ErrCode::BadExeFormat,
                format!("expected PE32+ magic 0x020B, got 0x{magic:04X}"),
            ));
        }
        let sec_table = opt + opt_size;
        let entry_rva = u32::from_le_bytes([pe[opt + 16], pe[opt + 17], pe[opt + 18], pe[opt + 19]]);
        let mut ib_bytes = [0u8; 8];
        ib_bytes.copy_from_slice(&pe[opt + 24..opt + 32]);
        let image_base = u64::from_le_bytes(ib_bytes);
        let size_of_image =
            u32::from_le_bytes([pe[opt + 56], pe[opt + 57], pe[opt + 58], pe[opt + 59]]);
        let size_of_headers = usize::try_from(u32::from_le_bytes([
            pe[opt + 60],
            pe[opt + 61],
            pe[opt + 62],
            pe[opt + 63],
        ]))
        .unwrap_or(0);
        let imp_rva =
            u32::from_le_bytes([pe[opt + 120], pe[opt + 121], pe[opt + 122], pe[opt + 123]]);
        Ok(Pe64Hdr {
            num_secs,
            sec_table,
            entry_rva,
            image_base,
            size_of_image,
            size_of_headers,
            imp_rva,
        })
    }

    fn map_pe64_sections(
        mem: &mut SparseGuestMem,
        pe: &[u8],
        image_base: u64,
        size_of_image: u32,
        size_of_headers: usize,
        sec_table: usize,
        num_secs: usize,
    ) -> Result<(), ToolError> {
        mem.map_range(image_base, u64::from(size_of_image.max(0x10000)));
        let hdr_copy = size_of_headers.min(pe.len());
        mem.write_slice(image_base, &pe[..hdr_copy])?;

        for i in 0..num_secs {
            let sh = sec_table + i * 40;
            if sh + 40 > pe.len() {
                break;
            }
            let vsize = u32::from_le_bytes([pe[sh + 8], pe[sh + 9], pe[sh + 10], pe[sh + 11]]);
            let rva = u32::from_le_bytes([pe[sh + 12], pe[sh + 13], pe[sh + 14], pe[sh + 15]]);
            let rsize = usize::try_from(u32::from_le_bytes([
                pe[sh + 16],
                pe[sh + 17],
                pe[sh + 18],
                pe[sh + 19],
            ]))
            .unwrap_or(0);
            let rptr = usize::try_from(u32::from_le_bytes([
                pe[sh + 20],
                pe[sh + 21],
                pe[sh + 22],
                pe[sh + 23],
            ]))
            .unwrap_or(0);
            let sec_va = image_base.wrapping_add(u64::from(rva));
            let map_len = u64::from(vsize).max(u64::try_from(rsize).unwrap_or(0));
            mem.map_range(sec_va, map_len.max(4096));
            if rsize > 0 && rptr < pe.len() {
                let avail = rsize.min(pe.len() - rptr);
                mem.write_slice(sec_va, &pe[rptr..rptr + avail])?;
            }
        }
        Ok(())
    }

    fn bind_pe64_imports(
        mem: &mut SparseGuestMem,
        image_base: u64,
        imp_rva: u32,
    ) -> Result<(BTreeMap<u64, String>, u64), ToolError> {
        let mut thunks = BTreeMap::new();
        let mut next_thunk = THUNK_BASE;
        if imp_rva != 0 {
            let mut desc = image_base.wrapping_add(u64::from(imp_rva));
            for _ in 0..32 {
                let oft = mem.read_u32_le(desc)?;
                let ft = mem.read_u32_le(desc.wrapping_add(16))?;
                if oft == 0 && ft == 0 {
                    break;
                }
                let lookup_rva = if oft != 0 { oft } else { ft };
                for slot in 0u64..512 {
                    let ent = mem.read_u64_le(
                        image_base
                            .wrapping_add(u64::from(lookup_rva))
                            .wrapping_add(slot * 8),
                    )?;
                    if ent == 0 {
                        break;
                    }
                    let name_rva = ent & 0x7FFF_FFFF_FFFF_FFFF;
                    let fn_name = mem.read_cstr(image_base.wrapping_add(name_rva).wrapping_add(2))?;
                    let thunk_addr = next_thunk;
                    next_thunk += 8;
                    thunks.insert(thunk_addr, fn_name);
                    mem.write_u64_le(
                        image_base.wrapping_add(u64::from(ft)).wrapping_add(slot * 8),
                        thunk_addr,
                    )?;
                }
                desc = desc.wrapping_add(20);
            }
        }
        Ok((thunks, next_thunk))
    }

    fn populate_startup_env(
        mem: &mut SparseGuestMem,
        os: &mut WinProcState,
        cmdline_tail: &str,
        env_kv: &[(&str, &str)],
    ) -> Result<(), ToolError> {
        let trimmed = cmdline_tail.trim_start();
        let has_argv0 = !trimmed.is_empty()
            && !trimmed.starts_with('/')
            && (trimmed.starts_with(DQ)
                || trimmed.to_ascii_uppercase().starts_with("WINCMD")
                || trimmed.contains(':'));
        let full_cmd = if has_argv0 {
            cmdline_tail.to_owned()
        } else if cmdline_tail.is_empty() {
            format!("{DQ}{}{DQ}", os.self_exe)
        } else {
            format!("{DQ}{}{DQ} {cmdline_tail}", os.self_exe)
        };
        let _ = mem.write_wstr(CMDLINE_ADDR, &full_cmd, 8192)?;
        os.cmdline_w_addr = CMDLINE_ADDR;

        let mut env_pairs: Vec<(String, String)> = vec![
            ("COMSPEC".into(), os.self_exe.clone()),
            (
                "PATH".into(),
                format!("C:{BS}Windows{BS}System32;C:{BS}Windows"),
            ),
            ("PATHEXT".into(), ".COM;.EXE;.BAT;.CMD".into()),
            ("PROMPT".into(), "$P$G".into()),
            ("OS".into(), "Windows_NT".into()),
            ("TEMP".into(), format!("C:{BS}Temp")),
        ];
        for &(k, v) in env_kv {
            if let Some(existing) = env_pairs
                .iter_mut()
                .find(|(ek, _)| ek.eq_ignore_ascii_case(k))
            {
                v.clone_into(&mut existing.1);
            } else {
                env_pairs.push((k.to_owned(), v.to_owned()));
            }
        }
        let mut cur = ENV_ADDR;
        for (k, v) in &env_pairs {
            let entry = format!("{k}={v}");
            for u in entry.encode_utf16() {
                mem.write_u16_le(cur, u)?;
                cur += 2;
            }
            mem.write_u16_le(cur, 0)?;
            cur += 2;
        }
        mem.write_u16_le(cur, 0)?;
        os.env_w_addr = ENV_ADDR;
        let _ = &Self::from_pe_bytes;
        Ok(())
    }

    /// Supplies standard input bytes (`ReadFile(hStdIn)` / `CONIN$`) before calling [`Self::run`].
    pub fn set_stdin(&mut self, data: &[u8]) {
        self.os.stdin_buf = data.to_vec();
        self.os.stdin_pos = 0;
    }

    /// Executes the loaded `ARM64` `PE32+` image up to `max_steps` instructions.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on unmapped memory access, illegal `X18` write,
    /// `SP` 16-byte alignment violation, unknown opcode, or step-budget exhaustion.
    pub fn run(&mut self, max_steps: u64) -> Result<Arm64RunResult, ToolError> {
        self.run_with_coverage(max_steps, true)
    }

    /// Executes the loaded `ARM64` `PE32+` image with optional PC/branch coverage collection.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on `SP` misalignment, `X18` (`TEB`) corruption,
    /// unmapped memory access, illegal instruction, or step budget exhaustion.
    pub fn run_with_coverage(
        &mut self,
        max_steps: u64,
        record_cov: bool,
    ) -> Result<Arm64RunResult, ToolError> {
        self.coverage.disabled = !record_cov;
        let _ = &self.pe_bytes;
        let mut prev_pc = 0u64;
        while self.steps < max_steps {
            if let Some(code) = self.os.exited {
                return Ok(self.finish_result(code));
            }
            let pc = self.pc;
            if pc == EXIT_LR {
                let code = u32::try_from(self.xregs[0] & 0xFFFF_FFFF).unwrap_or(0);
                return Ok(self.finish_result(code));
            }
            if (THUNK_BASE..THUNK_END).contains(&pc) {
                self.invoke_thunk(pc)?;
                continue;
            }
            if (pc & 3) != 0 {
                return Err(emu_err(
                    pc,
                    ErrCode::InvalidFunction,
                    format!("unaligned AArch64 PC 0x{pc:016X}"),
                ));
            }
            if self.xregs[18] != TEB_ADDR {
                return Err(emu_err(
                    pc,
                    ErrCode::AccessDenied,
                    "Windows ARM64 ABI violation: X18 (TEB) corrupted",
                ));
            }

            if !self.coverage.disabled {
                let rva = u32::try_from(pc.wrapping_sub(self.image_base)).unwrap_or(0);
                self.coverage.executed_pcs.insert(rva);
            }
            self.steps += 1;

            let insn = self.mem.read_u32_le(pc).map_err(|e| {
                emu_err(
                    pc,
                    ErrCode::AccessDenied,
                    format!(
                        "unmapped fetch at 0x{pc:016X} (prev RVA=0x{:X}, steps={}): {e}",
                        prev_pc.wrapping_sub(self.image_base),
                        self.steps
                    ),
                )
            })?;
            prev_pc = pc;
            self.pc = pc.wrapping_add(4);
            self.exec_a64(pc, insn).map_err(|e| {
                emu_err(
                    pc,
                    ErrCode::AccessDenied,
                    format!(
                        "at RVA 0x{:X} (insn=0x{insn:08X}, steps={}): {e}",
                        pc.wrapping_sub(self.image_base),
                        self.steps
                    ),
                )
            })?;
        }

        Err(emu_err(
            self.pc,
            ErrCode::InvalidFunction,
            format!("Arm64Vm exceeded max_steps ({max_steps})"),
        ))
    }

    fn finish_result(&self, exit_code: u32) -> Arm64RunResult {
        Arm64RunResult {
            exit_code,
            stdout: String::from_utf8_lossy(&self.os.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&self.os.stderr).into_owned(),
            steps: self.steps,
            coverage: self.coverage.clone(),
        }
    }

    fn invoke_thunk(&mut self, thunk_addr: u64) -> Result<(), ToolError> {
        let Some(api_name) = self.thunks.get(&thunk_addr).cloned() else {
            return Err(emu_err(
                thunk_addr,
                ErrCode::InvalidFunction,
                format!("unknown ARM64 import thunk at 0x{thunk_addr:016X}"),
            ));
        };
        if (self.sp & 0xF) != 0 {
            return Err(emu_err(
                thunk_addr,
                ErrCode::AccessDenied,
                format!(
                    "Windows ARM64 ABI violation: SP 0x{:016X} not 16-byte aligned at `{api_name}` call",
                    self.sp
                ),
            ));
        }
        if api_name == "CreateThread" {
            let start_addr = self.xregs[2];
            let param = self.xregs[3];
            let tid_ptr = self.xregs[5];
            let saved_xregs = self.xregs;
            let saved_sp = self.sp;
            let saved_pc = self.pc;
            let saved_nzcv = self.nzcv;
            let stk = self.mem.alloc_pages(0, 65_536)?;
            self.xregs[0] = param;
            self.sp = stk + 65_536;
            self.xregs[30] = EXIT_LR;
            self.pc = start_addr;
            let _ = self.run(2_000_000)?;
            self.xregs = saved_xregs;
            self.sp = saved_sp;
            self.pc = saved_pc;
            self.nzcv = saved_nzcv;
            let h = self.os.alloc_thread_handle();
            if tid_ptr != 0 {
                self.mem
                    .write_u32_le(tid_ptr, u32::try_from(h & 0xFFFF_FFFF).unwrap_or(1))?;
            }
            self.xregs[0] = h;
            self.pc = self.xregs[30];
            return Ok(());
        }
        let reg_args = [
            self.xregs[0],
            self.xregs[1],
            self.xregs[2],
            self.xregs[3],
            self.xregs[4],
            self.xregs[5],
            self.xregs[6],
            self.xregs[7],
        ];
        let mut next_t = self.next_thunk;
        let thunks_ref = &mut self.thunks;
        let ret = self.os.dispatch_api(
            &mut self.mem,
            &api_name,
            &reg_args,
            self.sp,
            &mut |dyn_name| {
                if let Some((&addr, _)) = thunks_ref.iter().find(|(_, v)| v.as_str() == dyn_name) {
                    return addr;
                }
                let addr = next_t;
                next_t += 8;
                thunks_ref.insert(addr, dyn_name.to_owned());
                addr
            },
        )?;
        self.next_thunk = next_t;
        self.xregs[0] = ret;
        self.pc = self.xregs[30];
        Ok(())
    }

    const fn read_x_zr(&self, r: usize) -> u64 {
        if r >= 31 { 0 } else { self.xregs[r] }
    }

    const fn read_w_zr(&self, r: usize) -> u32 {
        if r >= 31 {
            0
        } else {
            (self.xregs[r] & 0xFFFF_FFFF) as u32
        }
    }

    const fn read_x_sp(&self, r: usize) -> u64 {
        if r >= 31 { self.sp } else { self.xregs[r] }
    }

    fn write_x_zr(&mut self, pc: u64, rd: usize, val: u64) -> Result<(), ToolError> {
        if rd == 18 {
            return Err(emu_err(
                pc,
                ErrCode::AccessDenied,
                "Windows ARM64 ABI violation: illegal write to reserved X18 (TEB)!",
            ));
        }
        if rd < 31 {
            self.xregs[rd] = val;
        }
        Ok(())
    }

    fn write_w_zr(&mut self, pc: u64, rd: usize, val: u32) -> Result<(), ToolError> {
        self.write_x_zr(pc, rd, u64::from(val))
    }

    fn write_x_sp(&mut self, pc: u64, rd: usize, val: u64) -> Result<(), ToolError> {
        if rd == 31 {
            self.sp = val;
            Ok(())
        } else {
            self.write_x_zr(pc, rd, val)
        }
    }

    fn base_addr_checked(&self, pc: u64, rn: usize) -> Result<u64, ToolError> {
        if rn == 31 {
            if (self.sp & 0xF) != 0 {
                return Err(emu_err(
                    pc,
                    ErrCode::AccessDenied,
                    format!(
                        "AArch64 hardware SP Alignment Fault at PC 0x{pc:016X}: SP=0x{:016X}",
                        self.sp
                    ),
                ));
            }
            Ok(self.sp)
        } else {
            Ok(self.xregs[rn])
        }
    }

    fn record_cond_edge(&mut self, pc: u64, taken: bool) {
        if self.coverage.disabled {
            return;
        }
        let rva = u32::try_from(pc.wrapping_sub(self.image_base)).unwrap_or(0);
        let entry = self
            .coverage
            .branch_edges
            .entry(rva)
            .or_insert((false, false));
        if taken {
            entry.0 = true;
        } else {
            entry.1 = true;
        }
    }

    const fn cond_holds(&self, cond: u8) -> bool {
        let (n, z, c, v) = self.nzcv;
        let base = match (cond >> 1) & 7 {
            0 => z,
            1 => c,
            2 => n,
            3 => v,
            4 => c && !z,
            5 => n == v,
            6 => !z && (n == v),
            _ => true,
        };
        if (cond & 1) != 0 && (cond & 0xE) != 0xE {
            !base
        } else {
            base
        }
    }

    const fn add_flags32(a: u32, b: u32) -> (u32, (bool, bool, bool, bool)) {
        let (res, c) = a.overflowing_add(b);
        let v = (((a ^ !b) & (a ^ res)) >> 31) != 0;
        (res, ((res >> 31) != 0, res == 0, c, v))
    }

    const fn sub_flags32(a: u32, b: u32) -> (u32, (bool, bool, bool, bool)) {
        let (res, borrow) = a.overflowing_sub(b);
        let v = (((a ^ b) & (a ^ res)) >> 31) != 0;
        (res, ((res >> 31) != 0, res == 0, !borrow, v))
    }

    const fn add_flags64(a: u64, b: u64) -> (u64, (bool, bool, bool, bool)) {
        let (res, c) = a.overflowing_add(b);
        let v = (((a ^ !b) & (a ^ res)) >> 63) != 0;
        (res, ((res >> 63) != 0, res == 0, c, v))
    }

    const fn sub_flags64(a: u64, b: u64) -> (u64, (bool, bool, bool, bool)) {
        let (res, borrow) = a.overflowing_sub(b);
        let v = (((a ^ b) & (a ^ res)) >> 63) != 0;
        (res, ((res >> 63) != 0, res == 0, !borrow, v))
    }

    fn decode_bitmask_imm(sf: u32, n: u32, imm_s: u32, rot_r: u32) -> Option<u64> {
        let combined = (n << 6) | ((!imm_s) & 0x3F);
        if combined == 0 {
            return None;
        }
        let highest = combined.ilog2();
        if highest == 0 {
            return None;
        }
        let esize = 1usize << highest;
        let levels = u32::try_from(esize - 1).unwrap_or(0);
        let s = imm_s & levels;
        let r = rot_r & levels;
        if s == levels {
            return None;
        }
        let mut welem = if s + 1 >= 64 {
            u64::MAX
        } else {
            (1u64 << (s + 1)) - 1
        };
        if r != 0 {
            if esize == 64 {
                welem = welem.rotate_right(r);
            } else {
                let emask = (1u64 << esize) - 1;
                let esz32 = u32::try_from(esize).unwrap_or(32);
                welem = ((welem >> r) | (welem << (esz32 - r))) & emask;
            }
        }
        let mut wmask = 0u64;
        let mut pos = 0;
        while pos < 64 {
            wmask |= welem << pos;
            pos += esize;
        }
        if sf == 0 {
            wmask &= 0xFFFF_FFFF;
        }
        Some(wmask)
    }

    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn shift_reg_val(val: u64, sf: u32, shift_type: u32, amount: u32) -> u64 {
        if sf == 0 {
            let v = (val & 0xFFFF_FFFF) as u32;
            let amt = amount & 31;
            let out = match shift_type & 3 {
                0 => v << amt,
                1 => v >> amt,
                2 => ((v as i32) >> amt) as u32,
                _ => v.rotate_right(amt),
            };
            u64::from(out)
        } else {
            let amt = amount & 63;
            match shift_type & 3 {
                0 => val << amt,
                1 => val >> amt,
                2 => ((val as i64) >> amt) as u64,
                _ => val.rotate_right(amt),
            }
        }
    }

    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn extend_reg_val(val: u64, option: u32, shift: u32) -> u64 {
        let ext = match option & 7 {
            0 => val & 0xFF,
            1 => val & 0xFFFF,
            2 => val & 0xFFFF_FFFF,
            4 => i64::from((val & 0xFF) as u8 as i8) as u64,
            5 => i64::from((val & 0xFFFF) as u16 as i16) as u64,
            6 => i64::from((val & 0xFFFF_FFFF) as u32 as i32) as u64,
            _ => val,
        };
        ext << (shift & 4)
    }

    fn exec_a64(&mut self, pc: u64, insn: u32) -> Result<(), ToolError> {
        let op0 = (insn >> 25) & 0xF;
        match op0 {
            0b1000 | 0b1001 => self.exec_dp_imm(pc, insn),
            0b1010 | 0b1011 => self.exec_branch_sys(pc, insn),
            0b0100 | 0b0110 | 0b1100 | 0b1110 => self.exec_load_store(pc, insn),
            0b0101 | 0b1101 => self.exec_dp_reg(pc, insn),
            _ => Err(emu_err(
                pc,
                ErrCode::InvalidFunction,
                format!("unhandled AArch64 opcode 0x{insn:08X} at 0x{pc:016X}"),
            )),
        }
    }

    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn exec_dp_imm(&mut self, pc: u64, insn: u32) -> Result<(), ToolError> {
        let sf = (insn >> 31) & 1;
        let rd = usize::from((insn & 0x1F) as u8);
        let rn = usize::from(((insn >> 5) & 0x1F) as u8);

        // PC-rel addressing (`ADR` / `ADRP`): `(insn & 0x1F00_0000) == 0x1000_0000`
        if (insn & 0x1F00_0000) == 0x1000_0000 {
            let is_adrp = ((insn >> 31) & 1) != 0;
            let immlo = (insn >> 29) & 3;
            let immhi = (insn >> 5) & 0x7_FFFF;
            let raw21 = (immhi << 2) | immlo;
            let sext = i64::from(((raw21 << 11) as i32) >> 11);
            let res = if is_adrp {
                (pc & !0xFFF).wrapping_add((sext << 12) as u64)
            } else {
                pc.wrapping_add(sext as u64)
            };
            return self.write_x_zr(pc, rd, res);
        }

        // Add/subtract (immediate): `(insn & 0x1F80_0000) == 0x1100_0000`
        if (insn & 0x1F80_0000) == 0x1100_0000 {
            let is_sub = ((insn >> 30) & 1) != 0;
            let set_flags = ((insn >> 29) & 1) != 0;
            let sh = ((insn >> 22) & 1) != 0;
            let imm12 = u64::from((insn >> 10) & 0xFFF);
            let imm = if sh { imm12 << 12 } else { imm12 };
            let lhs = self.read_x_sp(rn);

            if sf == 0 {
                let l32 = (lhs & 0xFFFF_FFFF) as u32;
                let i32v = imm as u32;
                let (res, flags) = if is_sub {
                    Self::sub_flags32(l32, i32v)
                } else {
                    Self::add_flags32(l32, i32v)
                };
                if set_flags {
                    self.nzcv = flags;
                    return self.write_w_zr(pc, rd, res);
                }
                return self.write_x_sp(pc, rd, u64::from(res));
            }
            let (res, flags) = if is_sub {
                Self::sub_flags64(lhs, imm)
            } else {
                Self::add_flags64(lhs, imm)
            };
            if set_flags {
                self.nzcv = flags;
                return self.write_x_zr(pc, rd, res);
            }
            return self.write_x_sp(pc, rd, res);
        }

        // Logical (immediate): `(insn & 0x1F80_0000) == 0x1200_0000`
        if (insn & 0x1F80_0000) == 0x1200_0000 {
            let opc = (insn >> 29) & 3;
            let n = (insn >> 22) & 1;
            let rot_r = (insn >> 16) & 0x3F;
            let imm_s = (insn >> 10) & 0x3F;
            let Some(mask) = Self::decode_bitmask_imm(sf, n, imm_s, rot_r) else {
                return Err(emu_err(
                    pc,
                    ErrCode::InvalidFunction,
                    format!("invalid AArch64 bitmask immediate 0x{insn:08X}"),
                ));
            };
            let lhs = if sf == 0 {
                u64::from(self.read_w_zr(rn))
            } else {
                self.read_x_zr(rn)
            };
            let raw = match opc {
                0 | 3 => lhs & mask,
                1 => lhs | mask,
                _ => lhs ^ mask,
            };
            let res = if sf == 0 { raw & 0xFFFF_FFFF } else { raw };
            if opc == 3 {
                let sign = if sf == 0 {
                    ((res >> 31) & 1) != 0
                } else {
                    ((res >> 63) & 1) != 0
                };
                self.nzcv = (sign, res == 0, false, false);
                return self.write_x_zr(pc, rd, res);
            }
            return self.write_x_sp(pc, rd, res);
        }

        // Move wide (immediate) (`MOVN`, `MOVZ`, `MOVK`): `(insn & 0x1F80_0000) == 0x1280_0000`
        if (insn & 0x1F80_0000) == 0x1280_0000 {
            return self.exec_mov_wide(pc, insn, sf, rd);
        }

        // Bitfield (`SBFM`, `BFM`, `UBFM`): `(insn & 0x1F80_0000) == 0x1300_0000`
        if (insn & 0x1F80_0000) == 0x1300_0000 {
            return self.exec_bitfield(pc, insn, sf, rd, rn);
        }

        Err(emu_err(
            pc,
            ErrCode::InvalidFunction,
            format!("unhandled AArch64 DP-imm 0x{insn:08X} at 0x{pc:016X}"),
        ))
    }

    #[allow(clippy::cast_possible_truncation)]
    fn exec_mov_wide(&mut self, pc: u64, insn: u32, sf: u32, rd: usize) -> Result<(), ToolError> {
        let opc = (insn >> 29) & 3;
        let hw = ((insn >> 21) & 3) * 16;
        let imm16 = u64::from((insn >> 5) & 0xFFFF);
        let res = match opc {
            0 => !(imm16 << hw),
            2 => imm16 << hw,
            3 => {
                let prev = self.read_x_zr(rd);
                (prev & !(0xFFFFu64 << hw)) | (imm16 << hw)
            }
            _ => {
                return Err(emu_err(
                    pc,
                    ErrCode::InvalidFunction,
                    format!("unknown MOV wide 0x{insn:08X}"),
                ));
            }
        };
        if sf == 0 {
            return self.write_w_zr(pc, rd, (res & 0xFFFF_FFFF) as u32);
        }
        self.write_x_zr(pc, rd, res)
    }

    fn exec_bitfield(
        &mut self,
        pc: u64,
        insn: u32,
        sf: u32,
        rd: usize,
        rn: usize,
    ) -> Result<(), ToolError> {
        let opc = (insn >> 29) & 3;
        let width: u32 = if sf == 1 { 64 } else { 32 };
        let mask_w = if sf == 1 { u64::MAX } else { 0xFFFF_FFFF };
        let rot_r = ((insn >> 16) & 0x3F) & (width - 1);
        let imm_s = ((insn >> 10) & 0x3F) & (width - 1);
        let src = self.read_x_zr(rn) & mask_w;

        let res = if imm_s >= rot_r {
            let len = imm_s - rot_r + 1;
            let lmask = if len >= 64 {
                u64::MAX
            } else {
                (1u64 << len) - 1
            };
            let mut val = (src >> rot_r) & lmask;
            if opc == 0 && ((val >> (len - 1)) & 1) != 0 && len < width {
                val |= (!lmask) & mask_w;
            } else if opc == 1 {
                let dst = self.read_x_zr(rd) & mask_w;
                val |= dst & !lmask;
            }
            val & mask_w
        } else {
            let shift = width - rot_r;
            let len = imm_s + 1;
            let lmask = if len >= 64 {
                u64::MAX
            } else {
                (1u64 << len) - 1
            };
            let mut val = ((src & lmask) << shift) & mask_w;
            if opc == 0 && ((src >> imm_s) & 1) != 0 && shift + len < width {
                let top_shift = shift + len;
                val |= (u64::MAX << top_shift) & mask_w;
            } else if opc == 1 {
                let dst = self.read_x_zr(rd) & mask_w;
                let wmask = lmask << shift;
                val |= dst & !wmask;
            }
            val & mask_w
        };
        self.write_x_zr(pc, rd, res)
    }

    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn exec_branch_sys(&mut self, pc: u64, insn: u32) -> Result<(), ToolError> {
        // Unconditional B / BL: `(insn & 0x7C00_0000) == 0x1400_0000`
        if (insn & 0x7C00_0000) == 0x1400_0000 {
            let is_bl = ((insn >> 31) & 1) != 0;
            let imm26 = insn & 0x03FF_FFFF;
            let off = i64::from(((imm26 << 6) as i32) >> 6) * 4;
            if is_bl {
                self.xregs[30] = pc.wrapping_add(4);
            }
            self.pc = pc.wrapping_add(off as u64);
            return Ok(());
        }

        // Compare & Branch (`CBZ` / `CBNZ`): `(insn & 0x7E00_0000) == 0x3400_0000`
        if (insn & 0x7E00_0000) == 0x3400_0000 {
            let sf = (insn >> 31) & 1;
            let is_nz = ((insn >> 24) & 1) != 0;
            let imm19 = (insn >> 5) & 0x7_FFFF;
            let rt = usize::from((insn & 0x1F) as u8);
            let val = if sf == 0 {
                u64::from(self.read_w_zr(rt))
            } else {
                self.read_x_zr(rt)
            };
            let taken = if is_nz { val != 0 } else { val == 0 };
            self.record_cond_edge(pc, taken);
            if taken {
                let off = i64::from(((imm19 << 13) as i32) >> 13) * 4;
                self.pc = pc.wrapping_add(off as u64);
            }
            return Ok(());
        }

        // Test & Branch (`TBZ` / `TBNZ`): `(insn & 0x7E00_0000) == 0x3600_0000`
        if (insn & 0x7E00_0000) == 0x3600_0000 {
            let b5 = (insn >> 31) & 1;
            let b40 = (insn >> 19) & 0x1F;
            let bit = (b5 << 5) | b40;
            let is_nz = ((insn >> 24) & 1) != 0;
            let imm14 = (insn >> 5) & 0x3FFF;
            let rt = usize::from((insn & 0x1F) as u8);
            let bit_set = ((self.read_x_zr(rt) >> bit) & 1) != 0;
            let taken = if is_nz { bit_set } else { !bit_set };
            self.record_cond_edge(pc, taken);
            if taken {
                let off = i64::from(((imm14 << 18) as i32) >> 18) * 4;
                self.pc = pc.wrapping_add(off as u64);
            }
            return Ok(());
        }

        // Conditional Branch (`B.cond`): `(insn & 0xFF00_0010) == 0x5400_0000`
        if (insn & 0xFF00_0010) == 0x5400_0000 {
            let cond = (insn & 0xF) as u8;
            let imm19 = (insn >> 5) & 0x7_FFFF;
            let taken = self.cond_holds(cond);
            self.record_cond_edge(pc, taken);
            if taken {
                let off = i64::from(((imm19 << 13) as i32) >> 13) * 4;
                self.pc = pc.wrapping_add(off as u64);
            }
            return Ok(());
        }

        // Hints (`NOP`, `PACIASP`, `AUTIASP`, `BTI c`) & Barriers (`DMB`, `DSB`, `ISB`)
        if (insn & 0xFFFF_E000) == 0xD503_2000 {
            return Ok(());
        }

        // Unconditional Branch Register (`BR`, `BLR`, `RET`): `(insn & 0xFE1F_FC1F) == 0xD61F_0000`
        if (insn & 0xFE1F_FC1F) == 0xD61F_0000 {
            let opc = (insn >> 21) & 3;
            let rn = usize::from(((insn >> 5) & 0x1F) as u8);
            let target = self.read_x_zr(rn);
            if opc == 1 {
                self.xregs[30] = pc.wrapping_add(4);
            }
            self.pc = target;
            return Ok(());
        }

        Err(emu_err(
            pc,
            ErrCode::InvalidFunction,
            format!("unhandled AArch64 branch/sys 0x{insn:08X} at 0x{pc:016X}"),
        ))
    }

    #[allow(
        clippy::too_many_lines,
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn exec_dp_reg(&mut self, pc: u64, insn: u32) -> Result<(), ToolError> {
        let sf = (insn >> 31) & 1;
        let rd = usize::from((insn & 0x1F) as u8);
        let rn = usize::from(((insn >> 5) & 0x1F) as u8);
        let rm = usize::from(((insn >> 16) & 0x1F) as u8);

        // Logical (shifted register): `(insn & 0x1F00_0000) == 0x0A00_0000`
        if (insn & 0x1F00_0000) == 0x0A00_0000 {
            let opc = (insn >> 29) & 3;
            let invert = ((insn >> 21) & 1) != 0;
            let shift_type = (insn >> 22) & 3;
            let imm6 = (insn >> 10) & 0x3F;
            let lhs = if sf == 0 {
                u64::from(self.read_w_zr(rn))
            } else {
                self.read_x_zr(rn)
            };
            let mut rhs = Self::shift_reg_val(self.read_x_zr(rm), sf, shift_type, imm6);
            if invert {
                rhs = !rhs;
            }
            let raw = match opc {
                0 | 3 => lhs & rhs,
                1 => lhs | rhs,
                _ => lhs ^ rhs,
            };
            let res = if sf == 0 { raw & 0xFFFF_FFFF } else { raw };
            if opc == 3 {
                let sign = if sf == 0 {
                    ((res >> 31) & 1) != 0
                } else {
                    ((res >> 63) & 1) != 0
                };
                self.nzcv = (sign, res == 0, false, false);
            }
            return self.write_x_zr(pc, rd, res);
        }

        // Add/subtract (shifted register): `(insn & 0x1F20_0000) == 0x0B00_0000`
        if (insn & 0x1F20_0000) == 0x0B00_0000 {
            let is_sub = ((insn >> 30) & 1) != 0;
            let set_flags = ((insn >> 29) & 1) != 0;
            let shift_type = (insn >> 22) & 3;
            let imm6 = (insn >> 10) & 0x3F;
            let rhs = Self::shift_reg_val(self.read_x_zr(rm), sf, shift_type, imm6);
            if sf == 0 {
                let lhs = self.read_w_zr(rn);
                let r32 = (rhs & 0xFFFF_FFFF) as u32;
                let (res, flags) = if is_sub {
                    Self::sub_flags32(lhs, r32)
                } else {
                    Self::add_flags32(lhs, r32)
                };
                if set_flags {
                    self.nzcv = flags;
                }
                return self.write_w_zr(pc, rd, res);
            }
            let lhs = self.read_x_zr(rn);
            let (res, flags) = if is_sub {
                Self::sub_flags64(lhs, rhs)
            } else {
                Self::add_flags64(lhs, rhs)
            };
            if set_flags {
                self.nzcv = flags;
            }
            return self.write_x_zr(pc, rd, res);
        }

        // Add/subtract (extended register): `(insn & 0x1F20_0000) == 0x0B20_0000`
        if (insn & 0x1F20_0000) == 0x0B20_0000 {
            let is_sub = ((insn >> 30) & 1) != 0;
            let set_flags = ((insn >> 29) & 1) != 0;
            let option = (insn >> 13) & 7;
            let imm3 = (insn >> 10) & 7;
            let lhs = self.read_x_sp(rn);
            let rhs = Self::extend_reg_val(self.read_x_zr(rm), option, imm3);
            if sf == 0 {
                let (res, flags) = if is_sub {
                    Self::sub_flags32((lhs & 0xFFFF_FFFF) as u32, (rhs & 0xFFFF_FFFF) as u32)
                } else {
                    Self::add_flags32((lhs & 0xFFFF_FFFF) as u32, (rhs & 0xFFFF_FFFF) as u32)
                };
                if set_flags {
                    self.nzcv = flags;
                    return self.write_w_zr(pc, rd, res);
                }
                return self.write_x_sp(pc, rd, u64::from(res));
            }
            let (res, flags) = if is_sub {
                Self::sub_flags64(lhs, rhs)
            } else {
                Self::add_flags64(lhs, rhs)
            };
            if set_flags {
                self.nzcv = flags;
                return self.write_x_zr(pc, rd, res);
            }
            return self.write_x_sp(pc, rd, res);
        }

        // Conditional select (`CSEL`, `CSINC`/`CSET`, `CSINV`, `CSNEG`): `(insn & 0x1FE0_0000) == 0x1A80_0000`
        if (insn & 0x1FE0_0000) == 0x1A80_0000 {
            let op = (insn >> 30) & 1;
            let o2 = (insn >> 10) & 1;
            let cond = ((insn >> 12) & 0xF) as u8;
            let mut res = if self.cond_holds(cond) {
                self.read_x_zr(rn)
            } else {
                let mut v = self.read_x_zr(rm);
                if op != 0 {
                    v = !v;
                }
                if o2 != 0 {
                    v = v.wrapping_add(1);
                }
                v
            };
            if sf == 0 {
                res &= 0xFFFF_FFFF;
            }
            return self.write_x_zr(pc, rd, res);
        }

        // Data-processing 2-source (`UDIV`, `SDIV`, `LSLV`, `LSRV`, `ASRV`, `RORV`): `(insn & 0x1FE0_0000) == 0x1AC0_0000`
        if (insn & 0x1FE0_0000) == 0x1AC0_0000 {
            let opcode = (insn >> 10) & 0x3F;
            let res = if sf == 0 {
                let a = self.read_w_zr(rn);
                let b = self.read_w_zr(rm);
                let r32 = match opcode {
                    2 => a.checked_div(b).unwrap_or(0),
                    3 => {
                        if b == 0 {
                            0
                        } else {
                            (a as i32).wrapping_div(b as i32) as u32
                        }
                    }
                    8 => a << (b & 31),
                    9 => a >> (b & 31),
                    10 => ((a as i32) >> (b & 31)) as u32,
                    11 => a.rotate_right(b & 31),
                    _ => {
                        return Err(emu_err(
                            pc,
                            ErrCode::InvalidFunction,
                            format!("unhandled DP-2src 0x{insn:08X}"),
                        ));
                    }
                };
                u64::from(r32)
            } else {
                let a = self.read_x_zr(rn);
                let b = self.read_x_zr(rm);
                match opcode {
                    2 => a.checked_div(b).unwrap_or(0),
                    3 => {
                        if b == 0 {
                            0
                        } else {
                            (a as i64).wrapping_div(b as i64) as u64
                        }
                    }
                    8 => a << (b & 63),
                    9 => a >> (b & 63),
                    10 => ((a as i64) >> (b & 63)) as u64,
                    11 => a.rotate_right(u32::try_from(b & 63).unwrap_or(0)),
                    _ => {
                        return Err(emu_err(
                            pc,
                            ErrCode::InvalidFunction,
                            format!("unhandled DP-2src64 0x{insn:08X}"),
                        ));
                    }
                }
            };
            return self.write_x_zr(pc, rd, res);
        }

        // Data-processing 3-source (`MADD`/`MUL`, `MSUB`, `SMADDL`, `UMADDL`): `(insn & 0x1F00_0000) == 0x1B00_0000`
        if (insn & 0x1F00_0000) == 0x1B00_0000 {
            let op31 = (insn >> 21) & 7;
            let o0 = (insn >> 15) & 1;
            let ra = usize::from(((insn >> 10) & 0x1F) as u8);
            if op31 == 0 {
                if sf == 0 {
                    let prod = self.read_w_zr(rn).wrapping_mul(self.read_w_zr(rm));
                    let acc = self.read_w_zr(ra);
                    let res = if o0 == 0 {
                        acc.wrapping_add(prod)
                    } else {
                        acc.wrapping_sub(prod)
                    };
                    return self.write_w_zr(pc, rd, res);
                }
                let prod = self.read_x_zr(rn).wrapping_mul(self.read_x_zr(rm));
                let acc = self.read_x_zr(ra);
                let res = if o0 == 0 {
                    acc.wrapping_add(prod)
                } else {
                    acc.wrapping_sub(prod)
                };
                return self.write_x_zr(pc, rd, res);
            }
        }

        Err(emu_err(
            pc,
            ErrCode::InvalidFunction,
            format!("unhandled AArch64 DP-reg 0x{insn:08X} at 0x{pc:016X}"),
        ))
    }

    #[allow(
        clippy::too_many_lines,
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn exec_load_store(&mut self, pc: u64, insn: u32) -> Result<(), ToolError> {
        // Load-Acquire / Store-Release (`STLRB`, `STLRH`, `STLR`, `LDARB`, `LDAR`): `(insn & 0x3F00_0000) == 0x0800_0000`
        if (insn & 0x3F00_0000) == 0x0800_0000 {
            let size = (insn >> 30) & 3;
            let is_load = ((insn >> 22) & 1) != 0;
            let rn = usize::from(((insn >> 5) & 0x1F) as u8);
            let rt = usize::from((insn & 0x1F) as u8);
            let addr = self.base_addr_checked(pc, rn)?;
            if is_load {
                let val = match size {
                    0 => u64::from(self.mem.read_u8(addr)?),
                    1 => u64::from(self.mem.read_u16_le(addr)?),
                    2 => u64::from(self.mem.read_u32_le(addr)?),
                    _ => self.mem.read_u64_le(addr)?,
                };
                return self.write_x_zr(pc, rt, val);
            }
            let val = self.read_x_zr(rt);
            match size {
                0 => self.mem.write_u8(addr, (val & 0xFF) as u8)?,
                1 => self.mem.write_u16_le(addr, (val & 0xFFFF) as u16)?,
                2 => self.mem.write_u32_le(addr, (val & 0xFFFF_FFFF) as u32)?,
                _ => self.mem.write_u64_le(addr, val)?,
            }
            return Ok(());
        }

        // Load literal (`LDR Wt/Xt, label`): `(insn & 0x3B00_0000) == 0x1800_0000`
        if (insn & 0x3B00_0000) == 0x1800_0000 {
            let opc = (insn >> 30) & 3;
            let imm19 = (insn >> 5) & 0x7_FFFF;
            let rt = usize::from((insn & 0x1F) as u8);
            let off = i64::from(((imm19 << 13) as i32) >> 13) * 4;
            let addr = pc.wrapping_add(off as u64);
            let val = match opc {
                0 => u64::from(self.mem.read_u32_le(addr)?),
                1 => self.mem.read_u64_le(addr)?,
                _ => i64::from(self.mem.read_u32_le(addr)? as i32) as u64,
            };
            return self.write_x_zr(pc, rt, val);
        }

        // Load/Store Register Pair (`STP`, `LDP`): `(insn & 0x3A00_0000) == 0x2800_0000`
        if (insn & 0x3A00_0000) == 0x2800_0000 {
            let opc = (insn >> 30) & 3;
            let v_fp = ((insn >> 26) & 1) != 0;
            if v_fp {
                return Err(emu_err(pc, ErrCode::InvalidFunction, "SIMD STP/LDP unused"));
            }
            let mode = (insn >> 23) & 3;
            let is_load = ((insn >> 22) & 1) != 0;
            let imm7 = (insn >> 15) & 0x7F;
            let sext7 = i64::from(((imm7 << 25) as i32) >> 25);
            let rt2 = usize::from(((insn >> 10) & 0x1F) as u8);
            let rn = usize::from(((insn >> 5) & 0x1F) as u8);
            let rt1 = usize::from((insn & 0x1F) as u8);
            let scale = if (opc & 2) != 0 { 8i64 } else { 4i64 };
            let off = sext7 * scale;

            let base = self.base_addr_checked(pc, rn)?;
            let addr = if mode == 1 {
                base
            } else {
                base.wrapping_add(off as u64)
            };

            if is_load {
                if (opc & 2) != 0 {
                    let v1 = self.mem.read_u64_le(addr)?;
                    let v2 = self.mem.read_u64_le(addr.wrapping_add(8))?;
                    self.write_x_zr(pc, rt1, v1)?;
                    self.write_x_zr(pc, rt2, v2)?;
                } else {
                    let v1 = self.mem.read_u32_le(addr)?;
                    let v2 = self.mem.read_u32_le(addr.wrapping_add(4))?;
                    self.write_w_zr(pc, rt1, v1)?;
                    self.write_w_zr(pc, rt2, v2)?;
                }
            } else if (opc & 2) != 0 {
                self.mem.write_u64_le(addr, self.read_x_zr(rt1))?;
                self.mem
                    .write_u64_le(addr.wrapping_add(8), self.read_x_zr(rt2))?;
            } else {
                self.mem.write_u32_le(addr, self.read_w_zr(rt1))?;
                self.mem
                    .write_u32_le(addr.wrapping_add(4), self.read_w_zr(rt2))?;
            }

            if mode == 1 || mode == 3 {
                let wb = base.wrapping_add(off as u64);
                self.write_x_sp(pc, rn, wb)?;
            }
            return Ok(());
        }

        // Load/Store Register (unsigned immediate): `(insn & 0x3B00_0000) == 0x3900_0000`
        if (insn & 0x3B00_0000) == 0x3900_0000 && ((insn >> 26) & 1) == 0 {
            let size = (insn >> 30) & 3;
            let opc = (insn >> 22) & 3;
            let imm12 = u64::from((insn >> 10) & 0xFFF);
            let rn = usize::from(((insn >> 5) & 0x1F) as u8);
            let rt = usize::from((insn & 0x1F) as u8);
            let base = self.base_addr_checked(pc, rn)?;
            let addr = base.wrapping_add(imm12 << size);
            return self.exec_single_mem_op(pc, size, opc, rt, addr);
        }

        // Load/Store Register (unscaled / post-idx / pre-idx / reg-offset): `(insn & 0x3B00_0000) == 0x3800_0000`
        if (insn & 0x3B00_0000) == 0x3800_0000 && ((insn >> 26) & 1) == 0 {
            let size = (insn >> 30) & 3;
            let opc = (insn >> 22) & 3;
            let rn = usize::from(((insn >> 5) & 0x1F) as u8);
            let rt = usize::from((insn & 0x1F) as u8);
            let base = self.base_addr_checked(pc, rn)?;

            if ((insn >> 21) & 1) != 0 && ((insn >> 10) & 3) == 2 {
                let rm = usize::from(((insn >> 16) & 0x1F) as u8);
                let option = (insn >> 13) & 7;
                let s_bit = (insn >> 12) & 1;
                let shift = if s_bit != 0 { size } else { 0 };
                let idx = Self::extend_reg_val(self.read_x_zr(rm), option, 0) << shift;
                let addr = base.wrapping_add(idx);
                return self.exec_single_mem_op(pc, size, opc, rt, addr);
            }

            let mode = (insn >> 10) & 3;
            let imm9 = (insn >> 12) & 0x1FF;
            let simm9 = i64::from(((imm9 << 23) as i32) >> 23);
            let addr = if mode == 1 {
                base
            } else {
                base.wrapping_add(simm9 as u64)
            };
            self.exec_single_mem_op(pc, size, opc, rt, addr)?;
            if mode == 1 || mode == 3 {
                self.write_x_sp(pc, rn, base.wrapping_add(simm9 as u64))?;
            }
            return Ok(());
        }

        Err(emu_err(
            pc,
            ErrCode::InvalidFunction,
            format!("unhandled AArch64 load/store 0x{insn:08X} at 0x{pc:016X}"),
        ))
    }

    #[allow(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn exec_single_mem_op(
        &mut self,
        pc: u64,
        size: u32,
        opc: u32,
        rt: usize,
        addr: u64,
    ) -> Result<(), ToolError> {
        if opc == 0 {
            let val = self.read_x_zr(rt);
            match size {
                0 => self.mem.write_u8(addr, (val & 0xFF) as u8)?,
                1 => self.mem.write_u16_le(addr, (val & 0xFFFF) as u16)?,
                2 => self.mem.write_u32_le(addr, (val & 0xFFFF_FFFF) as u32)?,
                _ => self.mem.write_u64_le(addr, val)?,
            }
            return Ok(());
        }
        if opc == 1 {
            let val = match size {
                0 => u64::from(self.mem.read_u8(addr)?),
                1 => u64::from(self.mem.read_u16_le(addr)?),
                2 => u64::from(self.mem.read_u32_le(addr)?),
                _ => self.mem.read_u64_le(addr)?,
            };
            return self.write_x_zr(pc, rt, val);
        }
        let sext64 = match size {
            0 => i64::from(self.mem.read_u8(addr)? as i8) as u64,
            1 => i64::from(self.mem.read_u16_le(addr)? as i16) as u64,
            _ => i64::from(self.mem.read_u32_le(addr)? as i32) as u64,
        };
        if opc == 3 {
            self.write_w_zr(pc, rt, (sext64 & 0xFFFF_FFFF) as u32)
        } else {
            self.write_x_zr(pc, rt, sext64)
        }
    }
}
