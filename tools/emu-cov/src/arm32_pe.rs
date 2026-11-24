//! Deterministic 32-bit ARM (`ARMv4` `A32` & `ARMv7-A` `Thumb-2`) `PE32` coverage VM.
//!
//! Enforces `ARMv4` alignment, `ARMv7` 8-byte `SP` & `IT`-block rules, and collects branch edges.

use crate::CoverageData;
use crate::win_mock::{BS, DQ, GuestMem, SharedWinEnv, WinProcState, emu_err};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::rc::Rc;
use wincmd_diag::{ErrCode, ToolError};

const PAGE_SHIFT: u64 = 12;
const PAGE_SIZE: usize = 4096;
const PAGE_MASK: usize = 4095;
const THUNK_BASE: u32 = 0x7F00_0000;
const THUNK_END: u32 = 0x7F00_4000;
const EXIT_LR: u32 = 0x7FFF_FFE0;
const TEB_ADDR: u32 = 0x0700_0000;
const PEB_ADDR: u32 = 0x0700_1000;
const CMDLINE_ADDR: u32 = 0x0700_2000;
const ENV_ADDR: u32 = 0x0701_0000;
const STACK_BASE: u32 = 0x07FC_0000;
const STACK_TOP: u32 = 0x0800_0000;
const HEAP_START: u64 = 0x1000_0000;

struct Pe32Hdr {
    is_thumb2: bool,
    num_secs: usize,
    sec_table: usize,
    entry_rva: u32,
    image_base: u32,
    size_of_image: u32,
    size_of_headers: usize,
    imp_rva: u32,
}

const TLB_EMPTY: u32 = u32::MAX;
const TLB_ZERO_MAPPED: u32 = u32::MAX - 1;

/// Sparse 4 KiB-paged 32/64-bit guest virtual address space with mapped-region bounds checking
/// and an 8-entry direct-mapped software TLB.
#[derive(Clone, Debug)]
pub struct SparseGuestMem {
    pages: Vec<Box<[u8; PAGE_SIZE]>>,
    page_map: std::collections::HashMap<u64, u32>,
    tlb: std::cell::Cell<[(u64, u32); 8]>,
    mapped_ranges: Vec<(u64, u64)>,
    next_alloc: u64,
}

impl SparseGuestMem {
    /// Creates an empty guest virtual address space with dynamic allocations starting at `alloc_base`.
    #[must_use]
    pub fn new(alloc_base: u64) -> Self {
        Self {
            pages: Vec::with_capacity(64),
            page_map: std::collections::HashMap::with_capacity(64),
            tlb: std::cell::Cell::new([(u64::MAX, TLB_EMPTY); 8]),
            mapped_ranges: Vec::with_capacity(16),
            next_alloc: alloc_base,
        }
    }

    /// Marks `[start, start + size)` as valid mapped virtual memory (lazy zero-filled on read).
    pub fn map_range(&mut self, start: u64, size: u64) {
        let aligned_start = start & !0xFFF;
        let end = (start.saturating_add(size).saturating_add(0xFFF)) & !0xFFF;
        if end > aligned_start {
            self.mapped_ranges.push((aligned_start, end));
        }
    }

    fn is_mapped(&self, addr: u64) -> bool {
        self.mapped_ranges
            .iter()
            .any(|&(lo, hi)| addr >= lo && addr < hi)
    }

    #[inline]
    fn resolve_read_page(
        &self,
        vpn: u64,
        addr: u64,
    ) -> Result<Option<&[u8; PAGE_SIZE]>, ToolError> {
        let slot = usize::try_from(vpn & 7).unwrap_or(0);
        let mut cur_tlb = self.tlb.get();
        let (tag, idx) = cur_tlb[slot];
        if tag == vpn && idx != TLB_EMPTY {
            if idx == TLB_ZERO_MAPPED {
                return Ok(None);
            }
            return Ok(Some(&self.pages[idx as usize]));
        }
        if let Some(&p_idx) = self.page_map.get(&vpn) {
            cur_tlb[slot] = (vpn, p_idx);
            self.tlb.set(cur_tlb);
            return Ok(Some(&self.pages[p_idx as usize]));
        }
        if self.is_mapped(addr) {
            cur_tlb[slot] = (vpn, TLB_ZERO_MAPPED);
            self.tlb.set(cur_tlb);
            Ok(None)
        } else {
            Err(emu_err(
                addr,
                ErrCode::AccessDenied,
                format!("unmapped guest read at 0x{addr:08X}"),
            ))
        }
    }

    #[inline]
    fn resolve_write_page(
        &mut self,
        vpn: u64,
        addr: u64,
    ) -> Result<&mut [u8; PAGE_SIZE], ToolError> {
        let slot = usize::try_from(vpn & 7).unwrap_or(0);
        let mut cur_tlb = self.tlb.get();
        let (tag, idx) = cur_tlb[slot];
        if tag == vpn && idx < TLB_ZERO_MAPPED {
            return Ok(&mut self.pages[idx as usize]);
        }
        if let Some(&p_idx) = self.page_map.get(&vpn) {
            cur_tlb[slot] = (vpn, p_idx);
            self.tlb.set(cur_tlb);
            return Ok(&mut self.pages[p_idx as usize]);
        }
        if !self.is_mapped(addr) {
            return Err(emu_err(
                addr,
                ErrCode::AccessDenied,
                format!("unmapped guest write at 0x{addr:08X}"),
            ));
        }
        let new_idx = u32::try_from(self.pages.len()).unwrap_or(0);
        self.pages.push(Box::new([0u8; PAGE_SIZE]));
        self.page_map.insert(vpn, new_idx);
        cur_tlb[slot] = (vpn, new_idx);
        self.tlb.set(cur_tlb);
        Ok(&mut self.pages[new_idx as usize])
    }
}

impl GuestMem for SparseGuestMem {
    #[inline]
    fn read_u8(&self, addr: u64) -> Result<u8, ToolError> {
        let vpn = addr >> PAGE_SHIFT;
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0) & PAGE_MASK;
        Ok(self.resolve_read_page(vpn, addr)?.map_or(0, |pg| pg[off]))
    }

    #[inline]
    fn write_u8(&mut self, addr: u64, val: u8) -> Result<(), ToolError> {
        let vpn = addr >> PAGE_SHIFT;
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0) & PAGE_MASK;
        let pg = self.resolve_write_page(vpn, addr)?;
        pg[off] = val;
        Ok(())
    }

    #[inline]
    fn read_u16_le(&self, addr: u64) -> Result<u16, ToolError> {
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0);
        if off <= PAGE_SIZE - 2 {
            return Ok(self
                .resolve_read_page(addr >> PAGE_SHIFT, addr)?
                .map_or(0, |pg| u16::from_le_bytes([pg[off], pg[off + 1]])));
        }
        let b0 = self.read_u8(addr)?;
        let b1 = self.read_u8(addr.wrapping_add(1))?;
        Ok(u16::from_le_bytes([b0, b1]))
    }

    #[inline]
    fn write_u16_le(&mut self, addr: u64, val: u16) -> Result<(), ToolError> {
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0);
        let [b0, b1] = val.to_le_bytes();
        if off <= PAGE_SIZE - 2 {
            let pg = self.resolve_write_page(addr >> PAGE_SHIFT, addr)?;
            pg[off] = b0;
            pg[off + 1] = b1;
            return Ok(());
        }
        self.write_u8(addr, b0)?;
        self.write_u8(addr.wrapping_add(1), b1)
    }

    #[inline]
    fn read_u32_le(&self, addr: u64) -> Result<u32, ToolError> {
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0);
        if off <= PAGE_SIZE - 4 {
            return Ok(self
                .resolve_read_page(addr >> PAGE_SHIFT, addr)?
                .map_or(0, |pg| {
                    u32::from_le_bytes([pg[off], pg[off + 1], pg[off + 2], pg[off + 3]])
                }));
        }
        let lo = u32::from(self.read_u16_le(addr)?);
        let hi = u32::from(self.read_u16_le(addr.wrapping_add(2))?);
        Ok(lo | (hi << 16))
    }

    #[inline]
    fn write_u32_le(&mut self, addr: u64, val: u32) -> Result<(), ToolError> {
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0);
        if off <= PAGE_SIZE - 4 {
            let pg = self.resolve_write_page(addr >> PAGE_SHIFT, addr)?;
            pg[off..off + 4].copy_from_slice(&val.to_le_bytes());
            return Ok(());
        }
        let [b0, b1, b2, b3] = val.to_le_bytes();
        self.write_u16_le(addr, u16::from_le_bytes([b0, b1]))?;
        self.write_u16_le(addr.wrapping_add(2), u16::from_le_bytes([b2, b3]))
    }

    #[inline]
    fn read_u64_le(&self, addr: u64) -> Result<u64, ToolError> {
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0);
        if off <= PAGE_SIZE - 8 {
            return Ok(self
                .resolve_read_page(addr >> PAGE_SHIFT, addr)?
                .map_or(0, |pg| {
                    u64::from_le_bytes([
                        pg[off],
                        pg[off + 1],
                        pg[off + 2],
                        pg[off + 3],
                        pg[off + 4],
                        pg[off + 5],
                        pg[off + 6],
                        pg[off + 7],
                    ])
                }));
        }
        let lo = u64::from(self.read_u32_le(addr)?);
        let hi = u64::from(self.read_u32_le(addr.wrapping_add(4))?);
        Ok(lo | (hi << 32))
    }

    #[inline]
    fn write_u64_le(&mut self, addr: u64, val: u64) -> Result<(), ToolError> {
        let off = usize::try_from(addr & 0xFFF).unwrap_or(0);
        if off <= PAGE_SIZE - 8 {
            let pg = self.resolve_write_page(addr >> PAGE_SHIFT, addr)?;
            pg[off..off + 8].copy_from_slice(&val.to_le_bytes());
            return Ok(());
        }
        let lo = u32::try_from(val & 0xFFFF_FFFF).unwrap_or(0);
        let hi = u32::try_from(val >> 32).unwrap_or(0);
        self.write_u32_le(addr, lo)?;
        self.write_u32_le(addr.wrapping_add(4), hi)
    }

    fn write_slice(&mut self, mut addr: u64, mut data: &[u8]) -> Result<(), ToolError> {
        while !data.is_empty() {
            let off = usize::try_from(addr & 0xFFF).unwrap_or(0);
            let chunk = data.len().min(PAGE_SIZE - off);
            let pg = self.resolve_write_page(addr >> PAGE_SHIFT, addr)?;
            pg[off..off + chunk].copy_from_slice(&data[..chunk]);
            addr = addr.wrapping_add(u64::try_from(chunk).unwrap_or(0));
            data = &data[chunk..];
        }
        Ok(())
    }

    fn alloc_pages(&mut self, hint: u64, size: u64) -> Result<u64, ToolError> {
        let aligned_sz = (size.max(4096) + 0xFFF) & !0xFFF;
        if hint != 0 && self.is_mapped(hint) {
            self.map_range(hint, aligned_sz);
            return Ok(hint);
        }
        let base = self.next_alloc;
        self.next_alloc = self
            .next_alloc
            .saturating_add(aligned_sz)
            .saturating_add(0x10000)
            & !0xFFFF;
        self.map_range(base, aligned_sz);
        Ok(base)
    }
}

/// Execution result of running `WINCMD_ARMV4.EXE` or `WINCMD_ARM32.EXE` inside [`Arm32Vm`].
#[derive(Clone, Debug)]
pub struct Arm32RunResult {
    /// Final process exit code (`ExitProcess` / `TerminateProcess` / `r0`).
    pub exit_code: u32,
    /// Captured UTF-8 `stdout` stream.
    pub stdout: String,
    /// Captured UTF-8 `stderr` stream.
    pub stderr: String,
    /// Total retired instructions.
    pub steps: u64,
    /// Recorded PC & conditional-branch coverage telemetry.
    pub coverage: CoverageData,
}

/// Deterministic 32-bit ARM (`ARMv4` `A32` & `ARMv7-A` `Thumb-2`) PE32 Virtual Machine.
pub struct Arm32Vm {
    /// Raw PE32 image bytes (retained to spawn nested `CreatePipe` / `CMD /C` subshells!).
    pe_bytes: Vec<u8>,
    /// True if image is `0x01C4` (`ARMNT` Thumb-2), false if `0x01C0` (`ARMv4` A32 `WinCE`).
    pub is_thumb2: bool,
    /// Guest general-purpose registers `R0..R15` (`R13=SP`, `R14=LR`, `R15=PC`).
    pub regs: [u32; 16],
    /// CPSR condition flags `(N, Z, C, V)`.
    pub nzcv: (bool, bool, bool, bool),
    /// Active Thumb execution state (`true` for `ARMNT`, `false` for `ARMv4`).
    pub thumb_mode: bool,
    /// Active Thumb-2 `ITSTATE` byte (`0` outside `IT` blocks).
    pub it_state: u8,
    /// Guest virtual address space.
    pub mem: SparseGuestMem,
    /// Simulated `WinCE` / `WinNT` OS & VFS context.
    pub os: WinProcState,
    /// Thunk address (`addr & !1`) -> imported API name.
    thunks: BTreeMap<u32, String>,
    /// Next free thunk slot address.
    next_thunk: u32,
    /// Instruction & conditional-branch coverage telemetry.
    pub coverage: CoverageData,
    /// Total retired instructions.
    pub steps: u64,
}

impl Arm32Vm {
    /// Loads a `PE32` `ARMv4` (`0x01C0`) or `ARMv7-A` `ARMNT` (`0x01C4`) image into a fresh VM.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if the PE32 header or machine type is invalid.
    pub fn from_pe_bytes(
        pe: &[u8],
        cmdline_tail: &str,
        env_kv: &[(&str, &str)],
        shared: Rc<RefCell<SharedWinEnv>>,
    ) -> Result<Self, ToolError> {
        let hdr = Self::parse_pe32_hdr(pe)?;

        let mut mem = SparseGuestMem::new(HEAP_START);
        Self::map_pe32_sections(
            &mut mem,
            pe,
            hdr.image_base,
            hdr.size_of_image,
            hdr.size_of_headers,
            hdr.sec_table,
            hdr.num_secs,
        )?;

        let (thunks, next_thunk) =
            Self::bind_pe32_imports(&mut mem, hdr.image_base, hdr.imp_rva, hdr.is_thumb2)?;

        // Map TEB/PEB + command line + environment + 256 KiB stack
        mem.map_range(u64::from(TEB_ADDR), 0x3_0000);
        mem.map_range(u64::from(STACK_BASE), u64::from(STACK_TOP - STACK_BASE));

        let os_ver = if hdr.is_thumb2 {
            (10, 0, 19045)
        } else {
            (4, 20, 1088)
        };
        let mut os = WinProcState::new(!hdr.is_thumb2, false, os_ver, shared);

        // Populate TEB -> PEB (`[TEB+0x30] = PEB`, `[PEB+0xA4]=major`, `[PEB+0xA8]=minor`, `[PEB+0xAC]=build`)
        mem.write_u32_le(u64::from(TEB_ADDR + 0x30), PEB_ADDR)?;
        mem.write_u32_le(u64::from(PEB_ADDR + 0xA4), os_ver.0)?;
        mem.write_u32_le(u64::from(PEB_ADDR + 0xA8), os_ver.1)?;
        mem.write_u16_le(
            u64::from(PEB_ADDR + 0xAC),
            u16::try_from(os_ver.2).unwrap_or(0),
        )?;

        Self::populate_startup_env(&mut mem, &mut os, hdr.is_thumb2, cmdline_tail, env_kv)?;

        let raw_entry = hdr.image_base.wrapping_add(hdr.entry_rva);
        let mut regs = [0u32; 16];
        regs[13] = STACK_TOP - 16; // 8-byte aligned SP!
        regs[14] = if hdr.is_thumb2 { EXIT_LR | 1 } else { EXIT_LR };
        regs[15] = raw_entry & !1;

        let pe_clone = pe.to_vec();
        let is_t2 = hdr.is_thumb2;
        os.child_runner = Some(Box::new(move |sub_cmd, sh, h_in, h_out, h_err| {
            let mut child = Self::from_pe_bytes(&pe_clone, sub_cmd, &[], sh)?;
            let _ = is_t2;
            let _ = child.os.bind_inherited_stdio(0, h_in);
            let _ = child.os.bind_inherited_stdio(1, h_out);
            let _ = child.os.bind_inherited_stdio(2, h_err);
            let child_out = child.run(2_500_000)?;
            Ok((child_out.exit_code, child.os.stdout, child.os.stderr))
        }));

        Ok(Self {
            pe_bytes: pe.to_vec(),
            is_thumb2: hdr.is_thumb2,
            regs,
            nzcv: (false, false, false, false),
            thumb_mode: (raw_entry & 1) != 0,
            it_state: 0,
            mem,
            os,
            thunks,
            next_thunk,
            coverage: CoverageData::default(),
            steps: 0,
        })
    }

    fn parse_pe32_hdr(pe: &[u8]) -> Result<Pe32Hdr, ToolError> {
        if pe.len() < 0x100 || &pe[0..2] != b"MZ" {
            return Err(emu_err(0, ErrCode::BadExeFormat, "missing MZ signature"));
        }
        let lfanew = usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]]))
            .unwrap_or(0);
        if pe.get(lfanew..lfanew + 4) != Some(&[b'P', b'E', 0, 0]) {
            return Err(emu_err(0, ErrCode::BadExeFormat, "missing PE signature"));
        }
        let machine = u16::from_le_bytes([pe[lfanew + 4], pe[lfanew + 5]]);
        let is_thumb2 = match machine {
            0x01C0 => false,
            0x01C4 => true,
            other => {
                return Err(emu_err(
                    0,
                    ErrCode::BadExeFormat,
                    format!("unsupported ARM32 PE machine 0x{other:04X}"),
                ));
            }
        };
        let num_secs = usize::from(u16::from_le_bytes([pe[lfanew + 6], pe[lfanew + 7]]));
        let opt_sz = usize::from(u16::from_le_bytes([pe[lfanew + 20], pe[lfanew + 21]]));
        let opt_off = lfanew + 24;
        let rd32 = |o: usize| u32::from_le_bytes([pe[o], pe[o + 1], pe[o + 2], pe[o + 3]]);
        Ok(Pe32Hdr {
            is_thumb2,
            num_secs,
            sec_table: opt_off + opt_sz,
            entry_rva: rd32(opt_off + 16),
            image_base: rd32(opt_off + 28),
            size_of_image: rd32(opt_off + 56),
            size_of_headers: usize::try_from(rd32(opt_off + 60)).unwrap_or(0),
            imp_rva: rd32(opt_off + 104),
        })
    }

    fn map_pe32_sections(
        mem: &mut SparseGuestMem,
        pe: &[u8],
        image_base: u32,
        size_of_image: u32,
        size_of_headers: usize,
        sec_table: usize,
        num_secs: usize,
    ) -> Result<(), ToolError> {
        mem.map_range(u64::from(image_base), u64::from(size_of_image.max(0x10000)));
        mem.write_slice(u64::from(image_base), &pe[..size_of_headers.min(pe.len())])?;
        for i in 0..num_secs {
            let sh = sec_table + i * 40;
            if sh + 40 > pe.len() {
                break;
            }
            let rva = u32::from_le_bytes([pe[sh + 12], pe[sh + 13], pe[sh + 14], pe[sh + 15]]);
            let raw_sz = usize::try_from(u32::from_le_bytes([
                pe[sh + 16],
                pe[sh + 17],
                pe[sh + 18],
                pe[sh + 19],
            ]))
            .unwrap_or(0);
            let raw_ptr = usize::try_from(u32::from_le_bytes([
                pe[sh + 20],
                pe[sh + 21],
                pe[sh + 22],
                pe[sh + 23],
            ]))
            .unwrap_or(0);
            let vsz = u32::from_le_bytes([pe[sh + 8], pe[sh + 9], pe[sh + 10], pe[sh + 11]]);
            mem.map_range(
                u64::from(image_base.wrapping_add(rva)),
                u64::from(vsz.max(4096)),
            );
            if raw_sz > 0 && raw_ptr < pe.len() {
                let end = (raw_ptr + raw_sz).min(pe.len());
                mem.write_slice(u64::from(image_base.wrapping_add(rva)), &pe[raw_ptr..end])?;
            }
        }
        Ok(())
    }

    fn bind_pe32_imports(
        mem: &mut SparseGuestMem,
        image_base: u32,
        imp_rva: u32,
        is_thumb2: bool,
    ) -> Result<(BTreeMap<u32, String>, u32), ToolError> {
        let mut thunks = BTreeMap::new();
        let mut next_thunk = THUNK_BASE;
        if imp_rva != 0 {
            let mut desc = u64::from(image_base.wrapping_add(imp_rva));
            for _ in 0..32 {
                let oft = mem.read_u32_le(desc)?;
                let ft = mem.read_u32_le(desc.wrapping_add(16))?;
                if oft == 0 && ft == 0 {
                    break;
                }
                let lookup_rva = if oft != 0 { oft } else { ft };
                for slot in 0u32..512 {
                    let ent = mem.read_u32_le(u64::from(
                        image_base.wrapping_add(lookup_rva).wrapping_add(slot * 4),
                    ))?;
                    if ent == 0 {
                        break;
                    }
                    let name_addr =
                        u64::from(image_base.wrapping_add(ent & 0x7FFF_FFFF).wrapping_add(2));
                    let fn_name = mem.read_cstr(name_addr)?;
                    let thunk_addr = next_thunk;
                    next_thunk += 4;
                    thunks.insert(thunk_addr, fn_name);
                    let ptr_val = if is_thumb2 {
                        thunk_addr | 1
                    } else {
                        thunk_addr
                    };
                    mem.write_u32_le(
                        u64::from(image_base.wrapping_add(ft).wrapping_add(slot * 4)),
                        ptr_val,
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
        is_thumb2: bool,
        cmdline_tail: &str,
        env_kv: &[(&str, &str)],
    ) -> Result<(), ToolError> {
        let trimmed = cmdline_tail.trim_start();
        let has_argv0 = !trimmed.is_empty()
            && !trimmed.starts_with('/')
            && (trimmed.starts_with(DQ)
                || trimmed.to_ascii_uppercase().starts_with("WINCMD")
                || trimmed.contains(":\\"));
        let full_cmd = if is_thumb2 {
            if has_argv0 {
                cmdline_tail.to_owned()
            } else if trimmed.is_empty() {
                format!("{DQ}C:{BS}Windows{BS}System32{BS}WINCMD_ARM32.EXE{DQ}")
            } else {
                format!("{DQ}C:{BS}Windows{BS}System32{BS}WINCMD_ARM32.EXE{DQ} {cmdline_tail}")
            }
        } else {
            cmdline_tail.to_owned()
        };
        let _ = mem.write_wstr(u64::from(CMDLINE_ADDR), &full_cmd, 8192)?;
        os.cmdline_w_addr = u64::from(CMDLINE_ADDR);

        let mut env_pos = u64::from(ENV_ADDR);
        let default_pairs: Vec<(&str, String)> = if is_thumb2 {
            vec![
                (
                    "COMSPEC",
                    format!("C:{BS}Windows{BS}System32{BS}WINCMD_ARM32.EXE"),
                ),
                (
                    "PATH",
                    format!("C:{BS}Windows{BS}System32;C:{BS}Windows;C:{BS}Temp"),
                ),
                ("PATHEXT", ".COM;.EXE;.BAT;.CMD".to_owned()),
                ("PROMPT", "$P$G".to_owned()),
                ("TEMP", format!("C:{BS}Temp")),
                ("OS", "Windows_NT".to_owned()),
            ]
        } else {
            Vec::new()
        };
        for (k, v) in &default_pairs {
            if !env_kv.iter().any(|(ek, _)| ek.eq_ignore_ascii_case(k)) {
                let entry = format!("{k}={v}");
                let n = mem.write_wstr(env_pos, &entry, 1024)?;
                env_pos = env_pos.wrapping_add(u64::from(n + 1) * 2);
            }
        }
        for (k, v) in env_kv {
            let entry = format!("{k}={v}");
            let n = mem.write_wstr(env_pos, &entry, 1024)?;
            env_pos = env_pos.wrapping_add(u64::from(n + 1) * 2);
        }
        mem.write_u16_le(env_pos, 0)?;
        os.env_w_addr = u64::from(ENV_ADDR);
        Ok(())
    }

    /// Supplies standard input bytes (`ReadFile(hStdIn)` / `CONIN$`) before calling [`Self::run`].
    pub fn set_stdin(&mut self, data: &[u8]) {
        self.os.stdin_buf = data.to_vec();
        self.os.stdin_pos = 0;
    }

    /// Executes the loaded `ARMv4` or ARMv7-A PE32 binary until process exit or `max_steps`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on unaligned `ARMv4` load/store, `ARMv7` 8-byte `SP`
    /// misalignment, illegal instruction, unmapped memory fault, or step budget exhaustion.
    pub fn run(&mut self, max_steps: u64) -> Result<Arm32RunResult, ToolError> {
        self.run_with_coverage(max_steps, true)
    }

    /// Executes the loaded `ARMv4` or `ARMv7-A` `PE32` image with optional PC/branch coverage collection.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on unaligned `ARMv4` load/store, `ARMv7` 8-byte `SP`
    /// misalignment, illegal instruction, unmapped memory fault, or step budget exhaustion.
    pub fn run_with_coverage(
        &mut self,
        max_steps: u64,
        record_cov: bool,
    ) -> Result<Arm32RunResult, ToolError> {
        self.coverage.disabled = !record_cov;
        let _ = &self.pe_bytes;
        while self.steps < max_steps {
            if let Some(code) = self.os.exited {
                return Ok(self.finish(code));
            }
            let pc = self.regs[15] & !1;
            if pc == (EXIT_LR & !1) {
                return Ok(self.finish(self.regs[0]));
            }
            if (THUNK_BASE..THUNK_END).contains(&pc) {
                self.invoke_thunk(pc)?;
                continue;
            }
            if !self.is_thumb2 && self.thumb_mode {
                return Err(emu_err(
                    u64::from(pc),
                    ErrCode::InvalidFunction,
                    "strict ARMv4 mode entered Thumb state illegally",
                ));
            }

            if !self.coverage.disabled {
                self.coverage.executed_pcs.insert(pc);
            }
            self.steps += 1;

            let step_res = if self.thumb_mode {
                self.step_thumb2(pc)
            } else {
                self.step_a32(pc)
            };
            if let Err(err) = step_res {
                return Err(emu_err(
                    u64::from(pc),
                    ErrCode::AccessDenied,
                    format!("{err} at PC=0x{pc:08X} (LR=0x{:08X})", self.regs[14]),
                ));
            }
        }
        Err(emu_err(
            u64::from(self.regs[15]),
            ErrCode::GeneralFailure,
            format!("ARM32 VM exceeded {max_steps} step limit"),
        ))
    }

    fn finish(&self, exit_code: u32) -> Arm32RunResult {
        Arm32RunResult {
            exit_code,
            stdout: String::from_utf8_lossy(&self.os.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&self.os.stderr).into_owned(),
            steps: self.steps,
            coverage: self.coverage.clone(),
        }
    }

    fn invoke_thunk(&mut self, pc: u32) -> Result<(), ToolError> {
        if self.is_thumb2 && (self.regs[13] & 7) != 0 {
            return Err(emu_err(
                u64::from(pc),
                ErrCode::InvalidParameter,
                format!(
                    "ARMv7 AAPCS stack misalignment at API call: SP=0x{:08X}",
                    self.regs[13]
                ),
            ));
        }
        let Some(api) = self.thunks.get(&pc).cloned() else {
            return Err(emu_err(
                u64::from(pc),
                ErrCode::InvalidFunction,
                format!("unknown ARM32 IAT thunk 0x{pc:08X}"),
            ));
        };
        if api == "CreateThread" {
            let start_addr = self.regs[2];
            let param = self.regs[3];
            let tid_ptr = self.mem.read_u32_le(u64::from(self.regs[13]) + 4)?;
            let saved_regs = self.regs;
            let saved_thumb = self.thumb_mode;
            let saved_nzcv = self.nzcv;
            let saved_it = self.it_state;
            let stk = u32::try_from(self.mem.alloc_pages(0, 65_536)?).unwrap_or(0);
            self.regs[0] = param;
            self.regs[13] = stk.wrapping_add(65_536);
            self.regs[14] = if self.is_thumb2 { EXIT_LR | 1 } else { EXIT_LR };
            self.thumb_mode = self.is_thumb2 && (start_addr & 1) != 0;
            self.regs[15] = start_addr & !1;
            self.it_state = 0;
            let _ = self.run(2_000_000)?;
            self.regs = saved_regs;
            self.thumb_mode = saved_thumb;
            self.nzcv = saved_nzcv;
            self.it_state = saved_it;
            let h = u32::try_from(self.os.alloc_thread_handle()).unwrap_or(1);
            if tid_ptr != 0 {
                self.mem.write_u32_le(u64::from(tid_ptr), h)?;
            }
            self.regs[0] = h;
            let lr = self.regs[14];
            if self.is_thumb2 {
                self.thumb_mode = (lr & 1) != 0;
            }
            self.regs[15] = lr & !1;
            return Ok(());
        }
        let reg_args = [
            u64::from(self.regs[0]),
            u64::from(self.regs[1]),
            u64::from(self.regs[2]),
            u64::from(self.regs[3]),
        ];
        let sp = u64::from(self.regs[13]);
        let is_t2 = self.is_thumb2;
        let next_t = &mut self.next_thunk;
        let thunk_map = &mut self.thunks;
        let ret = self
            .os
            .dispatch_api(&mut self.mem, &api, &reg_args, sp, &mut |dyn_name| {
                for (&addr, existing) in thunk_map.iter() {
                    if existing == dyn_name {
                        return u64::from(if is_t2 { addr | 1 } else { addr });
                    }
                }
                let addr = *next_t;
                *next_t += 4;
                thunk_map.insert(addr, dyn_name.to_owned());
                u64::from(if is_t2 { addr | 1 } else { addr })
            })?;
        self.regs[0] = u32::try_from(ret & 0xFFFF_FFFF).unwrap_or(0);
        let lr = self.regs[14];
        if is_t2 {
            self.thumb_mode = (lr & 1) != 0;
        }
        self.regs[15] = lr & !1;
        Ok(())
    }

    const fn eval_cond(&self, cond: u8) -> bool {
        let (n, z, c, v) = self.nzcv;
        match cond & 0xF {
            0x0 => z,
            0x1 => !z,
            0x2 => c,
            0x3 => !c,
            0x4 => n,
            0x5 => !n,
            0x6 => v,
            0x7 => !v,
            0x8 => c && !z,
            0x9 => !c || z,
            0xA => n == v,
            0xB => n != v,
            0xC => !z && (n == v),
            0xD => z || (n != v),
            _ => true,
        }
    }

    fn record_cond_edge(&mut self, pc: u32, taken: bool) {
        if self.coverage.disabled {
            return;
        }
        let entry = self
            .coverage
            .branch_edges
            .entry(pc)
            .or_insert((false, false));
        if taken {
            entry.0 = true;
        } else {
            entry.1 = true;
        }
    }

    const fn read_reg(&self, r: usize, instr_pc: u32) -> u32 {
        if r == 15 {
            if self.thumb_mode {
                instr_pc.wrapping_add(4)
            } else {
                instr_pc.wrapping_add(8)
            }
        } else {
            self.regs[r]
        }
    }

    const fn write_reg(&mut self, r: usize, val: u32) {
        if r == 15 {
            if self.is_thumb2 && (val & 1) != 0 {
                self.thumb_mode = true;
            }
            self.regs[15] = val & !1;
        } else {
            self.regs[r] = val;
        }
    }

    fn check_align(&self, pc: u32, addr: u32, align: u32) -> Result<(), ToolError> {
        if !self.is_thumb2 && (addr & (align - 1)) != 0 {
            return Err(emu_err(
                u64::from(pc),
                ErrCode::InvalidAccess,
                format!("strict ARMv4 unaligned {align}-byte access at 0x{addr:08X}"),
            ));
        }
        Ok(())
    }

    const fn barrel_shift(
        val: u32,
        stype: u8,
        amount: u32,
        is_reg_shift: bool,
        carry_in: bool,
    ) -> (u32, bool) {
        if is_reg_shift {
            let sh = amount & 0xFF;
            if sh == 0 {
                return (val, carry_in);
            }
            match stype & 3 {
                0 => {
                    if sh < 32 {
                        (val << sh, ((val >> (32 - sh)) & 1) != 0)
                    } else if sh == 32 {
                        (0, (val & 1) != 0)
                    } else {
                        (0, false)
                    }
                }
                1 => {
                    if sh < 32 {
                        (val >> sh, ((val >> (sh - 1)) & 1) != 0)
                    } else if sh == 32 {
                        (0, (val >> 31) != 0)
                    } else {
                        (0, false)
                    }
                }
                2 => {
                    if sh < 32 {
                        #[allow(clippy::cast_possible_wrap, clippy::cast_sign_loss)]
                        let res = ((val as i32) >> sh) as u32;
                        (res, ((val >> (sh - 1)) & 1) != 0)
                    } else {
                        let bit = (val >> 31) != 0;
                        (if bit { 0xFFFF_FFFF } else { 0 }, bit)
                    }
                }
                _ => {
                    let rot = sh & 31;
                    if rot == 0 {
                        (val, (val >> 31) != 0)
                    } else {
                        let res = val.rotate_right(rot);
                        (res, (res >> 31) != 0)
                    }
                }
            }
        } else {
            match stype & 3 {
                0 => {
                    if amount == 0 {
                        (val, carry_in)
                    } else {
                        (val << amount, ((val >> (32 - amount)) & 1) != 0)
                    }
                }
                1 => {
                    if amount == 0 {
                        (0, (val >> 31) != 0)
                    } else {
                        (val >> amount, ((val >> (amount - 1)) & 1) != 0)
                    }
                }
                2 => {
                    if amount == 0 {
                        let bit = (val >> 31) != 0;
                        (if bit { 0xFFFF_FFFF } else { 0 }, bit)
                    } else {
                        #[allow(clippy::cast_possible_wrap, clippy::cast_sign_loss)]
                        let res = ((val as i32) >> amount) as u32;
                        (res, ((val >> (amount - 1)) & 1) != 0)
                    }
                }
                _ => {
                    if amount == 0 {
                        let c_bit = if carry_in { 0x8000_0000 } else { 0 };
                        (c_bit | (val >> 1), (val & 1) != 0)
                    } else {
                        let res = val.rotate_right(amount);
                        (res, (res >> 31) != 0)
                    }
                }
            }
        }
    }

    const fn adc_flags(lhs: u32, rhs: u32, c_in: bool) -> (u32, (bool, bool, bool, bool)) {
        let sum = (lhs as u64) + (rhs as u64) + (c_in as u64);
        #[allow(clippy::cast_possible_truncation)]
        let res = sum as u32;
        let carry = (sum >> 32) != 0;
        let overflow = (((!(lhs ^ rhs)) & (lhs ^ res)) >> 31) != 0;
        let neg = (res >> 31) != 0;
        let zero = res == 0;
        (res, (neg, zero, carry, overflow))
    }

    const fn alu_dp(
        &mut self,
        op: u8,
        rn: u32,
        op2: u32,
        shifter_c: bool,
        set_flags: bool,
    ) -> Option<u32> {
        let (_, _, c_old, v_old) = self.nzcv;
        match op & 0xF {
            0x0 | 0x1 | 0xC | 0xD | 0xE | 0xF => {
                let r = match op & 0xF {
                    0x0 => rn & op2,
                    0x1 => rn ^ op2,
                    0xC => rn | op2,
                    0xD => op2,
                    0xE => rn & !op2,
                    _ => !op2,
                };
                if set_flags {
                    self.nzcv = ((r >> 31) != 0, r == 0, shifter_c, v_old);
                }
                Some(r)
            }
            0x2..=0x7 => {
                let (r, fl) = match op & 0xF {
                    0x2 => Self::adc_flags(rn, !op2, true),
                    0x3 => Self::adc_flags(op2, !rn, true),
                    0x4 => Self::adc_flags(rn, op2, false),
                    0x5 => Self::adc_flags(rn, op2, c_old),
                    0x6 => Self::adc_flags(rn, !op2, c_old),
                    _ => Self::adc_flags(op2, !rn, c_old),
                };
                if set_flags {
                    self.nzcv = fl;
                }
                Some(r)
            }
            0x8 => {
                let r = rn & op2;
                self.nzcv = ((r >> 31) != 0, r == 0, shifter_c, v_old);
                None
            }
            0x9 => {
                let r = rn ^ op2;
                self.nzcv = ((r >> 31) != 0, r == 0, shifter_c, v_old);
                None
            }
            0xA => {
                self.nzcv = Self::adc_flags(rn, !op2, true).1;
                None
            }
            _ => {
                self.nzcv = Self::adc_flags(rn, op2, false).1;
                None
            }
        }
    }

    // =========================================================================
    // Classic 32-bit ARMv4 A32 Decoder & Executor
    // =========================================================================

    #[allow(
        clippy::too_many_lines,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn step_a32(&mut self, pc: u32) -> Result<(), ToolError> {
        let insn = self.mem.read_u32_le(u64::from(pc))?;
        self.regs[15] = pc.wrapping_add(4);

        let cond = u8::try_from(insn >> 28).unwrap_or(0xE);
        if cond < 0xE {
            let pass = self.eval_cond(cond);
            self.record_cond_edge(pc, pass);
            if !pass {
                return Ok(());
            }
        }

        // BX Rn (`0x012FFF10`)
        if (insn & 0x0FFF_FFF0) == 0x012F_FF10 {
            let rm = (insn & 0xF) as usize;
            let tgt = self.read_reg(rm, pc);
            if self.is_thumb2 {
                self.thumb_mode = (tgt & 1) != 0;
            }
            self.regs[15] = tgt & !1;
            return Ok(());
        }

        // Branch B / BL (`101L imm24`)
        if ((insn >> 25) & 7) == 5 {
            let link = ((insn >> 24) & 1) != 0;
            let imm24 = insn & 0x00FF_FFFF;
            let simm = ((imm24 << 8) as i32) >> 6;
            if link {
                self.regs[14] = pc.wrapping_add(4);
            }
            self.regs[15] = pc.wrapping_add(8).wrapping_add(simm as u32) & !3;
            return Ok(());
        }

        // Block Data Transfer LDM / STM (`100 P U S W L Rn reglist`)
        if ((insn >> 25) & 7) == 4 {
            let p_pre = ((insn >> 24) & 1) != 0;
            let u_up = ((insn >> 23) & 1) != 0;
            let w_wb = ((insn >> 21) & 1) != 0;
            let l_load = ((insn >> 20) & 1) != 0;
            let rn = ((insn >> 16) & 0xF) as usize;
            let list = (insn & 0xFFFF) as u16;
            let count = list.count_ones() * 4;
            let base = self.regs[rn];
            let mut addr = match (u_up, p_pre) {
                (true, false) => base,
                (true, true) => base.wrapping_add(4),
                (false, false) => base.wrapping_sub(count).wrapping_add(4),
                (false, true) => base.wrapping_sub(count),
            };
            let wb_val = if u_up {
                base.wrapping_add(count)
            } else {
                base.wrapping_sub(count)
            };
            for r in 0..16usize {
                if (list & (1 << r)) != 0 {
                    self.check_align(pc, addr, 4)?;
                    if l_load {
                        let v = self.mem.read_u32_le(u64::from(addr))?;
                        self.write_reg(r, v);
                    } else {
                        let v = self.read_reg(r, pc);
                        self.mem.write_u32_le(u64::from(addr), v)?;
                    }
                    addr = addr.wrapping_add(4);
                }
            }
            if w_wb && (!l_load || (list & (1 << rn)) == 0) {
                self.regs[rn] = wb_val;
            }
            return Ok(());
        }

        // Single Data Transfer LDR / STR / LDRB / STRB (`01 I P U B W L Rn Rd offset`)
        if ((insn >> 26) & 3) == 1 {
            let is_reg = ((insn >> 25) & 1) != 0;
            let p_pre = ((insn >> 24) & 1) != 0;
            let u_up = ((insn >> 23) & 1) != 0;
            let b_byte = ((insn >> 22) & 1) != 0;
            let w_wb = ((insn >> 21) & 1) != 0;
            let l_load = ((insn >> 20) & 1) != 0;
            let rn = ((insn >> 16) & 0xF) as usize;
            let rd = ((insn >> 12) & 0xF) as usize;
            let off = if is_reg {
                let rm = (insn & 0xF) as usize;
                let stype = u8::try_from((insn >> 5) & 3).unwrap_or(0);
                let amt = (insn >> 7) & 0x1F;
                Self::barrel_shift(self.read_reg(rm, pc), stype, amt, false, self.nzcv.2).0
            } else {
                insn & 0xFFF
            };
            let base = self.read_reg(rn, pc);
            let mod_addr = if u_up {
                base.wrapping_add(off)
            } else {
                base.wrapping_sub(off)
            };
            let eff = if p_pre { mod_addr } else { base };
            if b_byte {
                if l_load {
                    let v = u32::from(self.mem.read_u8(u64::from(eff))?);
                    self.write_reg(rd, v);
                } else {
                    let v = u8::try_from(self.read_reg(rd, pc) & 0xFF).unwrap_or(0);
                    self.mem.write_u8(u64::from(eff), v)?;
                }
            } else {
                self.check_align(pc, eff, 4)?;
                if l_load {
                    let v = self.mem.read_u32_le(u64::from(eff))?;
                    self.write_reg(rd, v);
                } else {
                    let v = self.read_reg(rd, pc);
                    self.mem.write_u32_le(u64::from(eff), v)?;
                }
            }
            if (!p_pre || w_wb) && (!l_load || rn != rd) {
                self.regs[rn] = mod_addr;
            }
            return Ok(());
        }

        // Multiply (`0000 00AS Rd Rn Rs 1001 Rm`) & Long Multiply (`0000 1UAS RdHi RdLo Rs 1001 Rm`)
        if (insn & 0x0FC0_00F0) == 0x0000_0090 {
            let acc = ((insn >> 21) & 1) != 0;
            let s_set = ((insn >> 20) & 1) != 0;
            let rd = ((insn >> 16) & 0xF) as usize;
            let rn = ((insn >> 12) & 0xF) as usize;
            let rs = ((insn >> 8) & 0xF) as usize;
            let rm = (insn & 0xF) as usize;
            let mut r = self.read_reg(rm, pc).wrapping_mul(self.read_reg(rs, pc));
            if acc {
                r = r.wrapping_add(self.read_reg(rn, pc));
            }
            self.regs[rd] = r;
            if s_set {
                self.nzcv.0 = (r >> 31) != 0;
                self.nzcv.1 = r == 0;
            }
            return Ok(());
        }
        if (insn & 0x0F80_00F0) == 0x0080_0090 {
            let signed = ((insn >> 22) & 1) != 0;
            let acc = ((insn >> 21) & 1) != 0;
            let s_set = ((insn >> 20) & 1) != 0;
            let rd_hi = ((insn >> 16) & 0xF) as usize;
            let rd_lo = ((insn >> 12) & 0xF) as usize;
            let rs = ((insn >> 8) & 0xF) as usize;
            let rm = (insn & 0xF) as usize;
            let mut prod: u64 = if signed {
                (i64::from(self.read_reg(rm, pc) as i32) * i64::from(self.read_reg(rs, pc) as i32))
                    as u64
            } else {
                u64::from(self.read_reg(rm, pc)) * u64::from(self.read_reg(rs, pc))
            };
            if acc {
                let prev = (u64::from(self.regs[rd_hi]) << 32) | u64::from(self.regs[rd_lo]);
                prod = prod.wrapping_add(prev);
            }
            self.regs[rd_lo] = u32::try_from(prod & 0xFFFF_FFFF).unwrap_or(0);
            self.regs[rd_hi] = u32::try_from(prod >> 32).unwrap_or(0);
            if s_set {
                self.nzcv.0 = (prod >> 63) != 0;
                self.nzcv.1 = prod == 0;
            }
            return Ok(());
        }

        // Halfword / Signed Byte Transfer (`000 P U I W L Rn Rd offH 1 S H 1 offL/Rm`)
        if (insn >> 25).trailing_zeros() >= 3 && ((insn >> 7) & 1) == 1 && ((insn >> 4) & 1) == 1 {
            let p_pre = ((insn >> 24) & 1) != 0;
            let u_up = ((insn >> 23) & 1) != 0;
            let is_imm = ((insn >> 22) & 1) != 0;
            let w_wb = ((insn >> 21) & 1) != 0;
            let l_load = ((insn >> 20) & 1) != 0;
            let rn = ((insn >> 16) & 0xF) as usize;
            let rd = ((insn >> 12) & 0xF) as usize;
            let sh_op = (insn >> 5) & 3;
            let off = if is_imm {
                (((insn >> 8) & 0xF) << 4) | (insn & 0xF)
            } else {
                self.read_reg((insn & 0xF) as usize, pc)
            };
            let base = self.read_reg(rn, pc);
            let mod_addr = if u_up {
                base.wrapping_add(off)
            } else {
                base.wrapping_sub(off)
            };
            let eff = if p_pre { mod_addr } else { base };
            match (l_load, sh_op) {
                (false, 1) => {
                    self.check_align(pc, eff, 2)?;
                    let v = u16::try_from(self.read_reg(rd, pc) & 0xFFFF).unwrap_or(0);
                    self.mem.write_u16_le(u64::from(eff), v)?;
                }
                (true, 1) => {
                    self.check_align(pc, eff, 2)?;
                    let v = u32::from(self.mem.read_u16_le(u64::from(eff))?);
                    self.write_reg(rd, v);
                }
                (true, 2) => {
                    let b = self.mem.read_u8(u64::from(eff))? as i8;
                    self.write_reg(rd, i32::from(b) as u32);
                }
                (true, 3) => {
                    self.check_align(pc, eff, 2)?;
                    let h = self.mem.read_u16_le(u64::from(eff))? as i16;
                    self.write_reg(rd, i32::from(h) as u32);
                }
                _ => {
                    return Err(emu_err(
                        u64::from(pc),
                        ErrCode::InvalidFunction,
                        format!("unsupported ARMv4 halfword insn 0x{insn:08X}"),
                    ));
                }
            }
            if (!p_pre || w_wb) && (!l_load || rn != rd) {
                self.regs[rn] = mod_addr;
            }
            return Ok(());
        }

        // Data Processing (`00 I opcode S Rn Rd operand2`)
        if (insn >> 26).trailing_zeros() >= 2 {
            let is_imm = ((insn >> 25) & 1) != 0;
            let op = u8::try_from((insn >> 21) & 0xF).unwrap_or(0);
            let s_set = ((insn >> 20) & 1) != 0;
            let rn = ((insn >> 16) & 0xF) as usize;
            let rd = ((insn >> 12) & 0xF) as usize;
            let (op2, sh_c) = if is_imm {
                let imm8 = insn & 0xFF;
                let rot2 = ((insn >> 8) & 0xF) * 2;
                let v = imm8.rotate_right(rot2);
                let c = if rot2 == 0 {
                    self.nzcv.2
                } else {
                    (v >> 31) != 0
                };
                (v, c)
            } else {
                let rm = (insn & 0xF) as usize;
                let stype = u8::try_from((insn >> 5) & 3).unwrap_or(0);
                let reg_sh = ((insn >> 4) & 1) != 0;
                if reg_sh {
                    let rs = ((insn >> 8) & 0xF) as usize;
                    let amt = self.regs[rs];
                    Self::barrel_shift(self.read_reg(rm, pc), stype, amt, true, self.nzcv.2)
                } else {
                    let amt = (insn >> 7) & 0x1F;
                    Self::barrel_shift(self.read_reg(rm, pc), stype, amt, false, self.nzcv.2)
                }
            };
            let rn_val = self.read_reg(rn, pc);
            if let Some(res) = self.alu_dp(op, rn_val, op2, sh_c, s_set) {
                self.write_reg(rd, res);
            }
            return Ok(());
        }

        Err(emu_err(
            u64::from(pc),
            ErrCode::InvalidFunction,
            format!("unhandled ARMv4 instruction 0x{insn:08X} at 0x{pc:08X}"),
        ))
    }

    // =========================================================================
    // ARMv7-A Unified Thumb-2 (16-bit & 32-bit) Decoder & Executor
    // =========================================================================

    const fn advance_it(&mut self) {
        if (self.it_state & 0x0F) != 0 {
            if self.it_state.trailing_zeros() >= 3 {
                self.it_state = 0;
            } else {
                self.it_state = (self.it_state & 0xE0) | ((self.it_state << 1) & 0x1F);
            }
        }
    }

    fn step_thumb2(&mut self, pc: u32) -> Result<(), ToolError> {
        let hw1 = self.mem.read_u16_le(u64::from(pc))?;
        let is_wide = (hw1 & 0xF800) >= 0xE800;
        let len = if is_wide { 4u32 } else { 2u32 };
        let hw2 = if is_wide {
            self.mem.read_u16_le(u64::from(pc + 2))?
        } else {
            0
        };
        self.regs[15] = pc.wrapping_add(len);

        // Check active IT block predication (`IT` instruction itself is never inside an IT block)
        let in_it = (self.it_state & 0x0F) != 0;
        if in_it {
            let cond = self.it_state >> 4;
            let pass = self.eval_cond(cond);
            self.record_cond_edge(pc, pass);
            self.advance_it();
            if !pass {
                return Ok(());
            }
        }

        if is_wide {
            self.exec_thumb32(pc, hw1, hw2)
        } else {
            self.exec_thumb16(pc, hw1, in_it)
        }
    }

    #[allow(
        clippy::too_many_lines,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn exec_thumb16(&mut self, pc: u32, hw: u16, in_it: bool) -> Result<(), ToolError> {
        let set_f = !in_it;
        match hw >> 13 {
            // 000: Shift by imm5 (`LSL`, `LSR`, `ASR`) or `ADD`/`SUB` 3-operand (`00011`)
            0 => {
                if ((hw >> 11) & 3) == 3 {
                    let is_sub = ((hw >> 9) & 1) != 0;
                    let is_imm = ((hw >> 10) & 1) != 0;
                    let rn = usize::from((hw >> 3) & 7);
                    let rd = usize::from(hw & 7);
                    let op2 = if is_imm {
                        u32::from((hw >> 6) & 7)
                    } else {
                        self.regs[usize::from((hw >> 6) & 7)]
                    };
                    let (r, fl) = if is_sub {
                        Self::adc_flags(self.regs[rn], !op2, true)
                    } else {
                        Self::adc_flags(self.regs[rn], op2, false)
                    };
                    self.regs[rd] = r;
                    if set_f {
                        self.nzcv = fl;
                    }
                } else {
                    let stype = u8::try_from((hw >> 11) & 3).unwrap_or(0);
                    let imm5 = u32::from((hw >> 6) & 0x1F);
                    let rm = usize::from((hw >> 3) & 7);
                    let rd = usize::from(hw & 7);
                    let (r, c) = Self::barrel_shift(self.regs[rm], stype, imm5, false, self.nzcv.2);
                    self.regs[rd] = r;
                    if set_f {
                        self.nzcv = ((r >> 31) != 0, r == 0, c, self.nzcv.3);
                    }
                }
                Ok(())
            }
            // 001: MOV / CMP / ADD / SUB 8-bit immediate
            1 => {
                let op = (hw >> 11) & 3;
                let rd = usize::from((hw >> 8) & 7);
                let imm8 = u32::from(hw & 0xFF);
                match op {
                    0 => {
                        self.regs[rd] = imm8;
                        if set_f {
                            self.nzcv.0 = false;
                            self.nzcv.1 = imm8 == 0;
                        }
                    }
                    1 => {
                        let (_, fl) = Self::adc_flags(self.regs[rd], !imm8, true);
                        self.nzcv = fl;
                    }
                    2 => {
                        let (r, fl) = Self::adc_flags(self.regs[rd], imm8, false);
                        self.regs[rd] = r;
                        if set_f {
                            self.nzcv = fl;
                        }
                    }
                    _ => {
                        let (r, fl) = Self::adc_flags(self.regs[rd], !imm8, true);
                        self.regs[rd] = r;
                        if set_f {
                            self.nzcv = fl;
                        }
                    }
                }
                Ok(())
            }
            // 010: Data-processing reg / Special high-reg + BX/BLX / LDR literal / Load-store reg
            2 => {
                if (hw >> 10).trailing_zeros() >= 3 {
                    let op = (hw >> 6) & 0xF;
                    let rm = usize::from((hw >> 3) & 7);
                    let rdn = usize::from(hw & 7);
                    let a = self.regs[rdn];
                    let b = self.regs[rm];
                    match op {
                        0x0 => {
                            let r = a & b;
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv.0 = (r >> 31) != 0;
                                self.nzcv.1 = r == 0;
                            }
                        }
                        0x1 => {
                            let r = a ^ b;
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv.0 = (r >> 31) != 0;
                                self.nzcv.1 = r == 0;
                            }
                        }
                        0x2..=0x4 | 0x7 => {
                            let st = match op {
                                0x2 => 0,
                                0x3 => 1,
                                0x4 => 2,
                                _ => 3,
                            };
                            let (r, c) = Self::barrel_shift(a, st, b, true, self.nzcv.2);
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv = ((r >> 31) != 0, r == 0, c, self.nzcv.3);
                            }
                        }
                        0x5 => {
                            let (r, fl) = Self::adc_flags(a, b, self.nzcv.2);
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv = fl;
                            }
                        }
                        0x6 => {
                            let (r, fl) = Self::adc_flags(a, !b, self.nzcv.2);
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv = fl;
                            }
                        }
                        0x8 => {
                            let r = a & b;
                            self.nzcv.0 = (r >> 31) != 0;
                            self.nzcv.1 = r == 0;
                        }
                        0x9 => {
                            let (r, fl) = Self::adc_flags(0, !b, true);
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv = fl;
                            }
                        }
                        0xA => {
                            let (_, fl) = Self::adc_flags(a, !b, true);
                            self.nzcv = fl;
                        }
                        0xB => {
                            let (_, fl) = Self::adc_flags(a, b, false);
                            self.nzcv = fl;
                        }
                        0xC => {
                            let r = a | b;
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv.0 = (r >> 31) != 0;
                                self.nzcv.1 = r == 0;
                            }
                        }
                        0xD => {
                            let r = a.wrapping_mul(b);
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv.0 = (r >> 31) != 0;
                                self.nzcv.1 = r == 0;
                            }
                        }
                        0xE => {
                            let r = a & !b;
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv.0 = (r >> 31) != 0;
                                self.nzcv.1 = r == 0;
                            }
                        }
                        _ => {
                            let r = !b;
                            self.regs[rdn] = r;
                            if set_f {
                                self.nzcv.0 = (r >> 31) != 0;
                                self.nzcv.1 = r == 0;
                            }
                        }
                    }
                    return Ok(());
                }
                if ((hw >> 10) & 7) == 1 {
                    let op = (hw >> 8) & 3;
                    let d = usize::from(((hw >> 4) & 8) | (hw & 7));
                    let m = usize::from((hw >> 3) & 0xF);
                    match op {
                        0 => {
                            let v = self.read_reg(d, pc).wrapping_add(self.read_reg(m, pc));
                            self.write_reg(d, v);
                        }
                        1 => {
                            let (_, fl) =
                                Self::adc_flags(self.read_reg(d, pc), !self.read_reg(m, pc), true);
                            self.nzcv = fl;
                        }
                        2 => {
                            let v = self.read_reg(m, pc);
                            self.write_reg(d, v);
                        }
                        _ => {
                            let is_blx = ((hw >> 7) & 1) != 0;
                            let tgt = self.read_reg(m, pc);
                            if is_blx {
                                if (self.regs[13] & 7) != 0 {
                                    return Err(emu_err(
                                        u64::from(pc),
                                        ErrCode::InvalidParameter,
                                        format!(
                                            "ARMv7 SP misaligned at BLX: 0x{:08X}",
                                            self.regs[13]
                                        ),
                                    ));
                                }
                                self.regs[14] = pc.wrapping_add(2) | 1;
                            }
                            self.it_state = 0;
                            self.thumb_mode = (tgt & 1) != 0;
                            self.regs[15] = tgt & !1;
                        }
                    }
                    return Ok(());
                }
                if ((hw >> 11) & 3) == 1 {
                    let rt = usize::from((hw >> 8) & 7);
                    let imm = u32::from(hw & 0xFF) * 4;
                    let base = (pc.wrapping_add(4)) & !3;
                    self.regs[rt] = self.mem.read_u32_le(u64::from(base.wrapping_add(imm)))?;
                    return Ok(());
                }
                // Load/store register offset (`0101 op3 Rm Rn Rt`)
                let op3 = (hw >> 9) & 7;
                let rm = usize::from((hw >> 6) & 7);
                let rn = usize::from((hw >> 3) & 7);
                let rt = usize::from(hw & 7);
                let addr = self.regs[rn].wrapping_add(self.regs[rm]);
                match op3 {
                    0 => self.mem.write_u32_le(u64::from(addr), self.regs[rt])?,
                    1 => self.mem.write_u16_le(
                        u64::from(addr),
                        u16::try_from(self.regs[rt] & 0xFFFF).unwrap_or(0),
                    )?,
                    2 => self.mem.write_u8(
                        u64::from(addr),
                        u8::try_from(self.regs[rt] & 0xFF).unwrap_or(0),
                    )?,
                    3 => {
                        let b = self.mem.read_u8(u64::from(addr))? as i8;
                        self.regs[rt] = i32::from(b) as u32;
                    }
                    4 => self.regs[rt] = self.mem.read_u32_le(u64::from(addr))?,
                    5 => self.regs[rt] = u32::from(self.mem.read_u16_le(u64::from(addr))?),
                    6 => self.regs[rt] = u32::from(self.mem.read_u8(u64::from(addr))?),
                    _ => {
                        let h = self.mem.read_u16_le(u64::from(addr))? as i16;
                        self.regs[rt] = i32::from(h) as u32;
                    }
                }
                Ok(())
            }
            // 011: STR / LDR / STRB / LDRB imm5
            3 => {
                let op = (hw >> 11) & 3;
                let imm5 = u32::from((hw >> 6) & 0x1F);
                let rn = usize::from((hw >> 3) & 7);
                let rt = usize::from(hw & 7);
                match op {
                    0 => {
                        let addr = self.regs[rn].wrapping_add(imm5 * 4);
                        self.mem.write_u32_le(u64::from(addr), self.regs[rt])?;
                    }
                    1 => {
                        let addr = self.regs[rn].wrapping_add(imm5 * 4);
                        self.regs[rt] = self.mem.read_u32_le(u64::from(addr))?;
                    }
                    2 => {
                        let addr = self.regs[rn].wrapping_add(imm5);
                        self.mem.write_u8(
                            u64::from(addr),
                            u8::try_from(self.regs[rt] & 0xFF).unwrap_or(0),
                        )?;
                    }
                    _ => {
                        let addr = self.regs[rn].wrapping_add(imm5);
                        self.regs[rt] = u32::from(self.mem.read_u8(u64::from(addr))?);
                    }
                }
                Ok(())
            }
            // 100: STRH / LDRH imm5 (`1000`) & STR / LDR SP-relative (`1001`)
            4 => {
                let is_ldr = ((hw >> 11) & 1) != 0;
                if ((hw >> 12) & 1) == 0 {
                    let imm = u32::from((hw >> 6) & 0x1F) * 2;
                    let rn = usize::from((hw >> 3) & 7);
                    let rt = usize::from(hw & 7);
                    let addr = self.regs[rn].wrapping_add(imm);
                    if is_ldr {
                        self.regs[rt] = u32::from(self.mem.read_u16_le(u64::from(addr))?);
                    } else {
                        self.mem.write_u16_le(
                            u64::from(addr),
                            u16::try_from(self.regs[rt] & 0xFFFF).unwrap_or(0),
                        )?;
                    }
                } else {
                    let rt = usize::from((hw >> 8) & 7);
                    let imm = u32::from(hw & 0xFF) * 4;
                    let addr = self.regs[13].wrapping_add(imm);
                    if is_ldr {
                        self.regs[rt] = self.mem.read_u32_le(u64::from(addr))?;
                    } else {
                        self.mem.write_u32_le(u64::from(addr), self.regs[rt])?;
                    }
                }
                Ok(())
            }
            // 101: ADR / ADD Rd, SP, #imm (`1010`) & Misc 16-bit (`1011`: ADD/SUB SP, SXTH/UXTB, CBZ/CBNZ, PUSH/POP, IT)
            5 => {
                if ((hw >> 12) & 1) == 0 {
                    let is_sp = ((hw >> 11) & 1) != 0;
                    let rd = usize::from((hw >> 8) & 7);
                    let imm = u32::from(hw & 0xFF) * 4;
                    let base = if is_sp {
                        self.regs[13]
                    } else {
                        pc.wrapping_add(4) & !3
                    };
                    self.regs[rd] = base.wrapping_add(imm);
                    return Ok(());
                }
                // Misc `1011 xxxx xxxx xxxx`
                let sub_op = (hw >> 8) & 0xF;
                match sub_op {
                    0 => {
                        let is_sub = ((hw >> 7) & 1) != 0;
                        let imm = u32::from(hw & 0x7F) * 4;
                        self.regs[13] = if is_sub {
                            self.regs[13].wrapping_sub(imm)
                        } else {
                            self.regs[13].wrapping_add(imm)
                        };
                        Ok(())
                    }
                    2 => {
                        let ext_op = (hw >> 6) & 3;
                        let rm = usize::from((hw >> 3) & 7);
                        let rd = usize::from(hw & 7);
                        let v = self.regs[rm];
                        self.regs[rd] = match ext_op {
                            0 => i32::from((v & 0xFFFF) as u16 as i16) as u32,
                            1 => i32::from((v & 0xFF) as u8 as i8) as u32,
                            2 => v & 0xFFFF,
                            _ => v & 0xFF,
                        };
                        Ok(())
                    }
                    1 | 3 | 9 | 11 => {
                        // CBZ / CBNZ
                        let non_zero = ((hw >> 11) & 1) != 0;
                        let rn = usize::from(hw & 7);
                        let i_bit = u32::from((hw >> 9) & 1);
                        let imm5 = u32::from((hw >> 3) & 0x1F);
                        let off = (i_bit << 6) | (imm5 << 1);
                        let taken = if non_zero {
                            self.regs[rn] != 0
                        } else {
                            self.regs[rn] == 0
                        };
                        self.record_cond_edge(pc, taken);
                        if taken {
                            self.regs[15] = pc.wrapping_add(4).wrapping_add(off);
                        }
                        Ok(())
                    }
                    4 | 5 => {
                        // PUSH {reglist, M(LR)}
                        let push_lr = ((hw >> 8) & 1) != 0;
                        let list = u8::try_from(hw & 0xFF).unwrap_or(0);
                        let total = (list.count_ones() + u32::from(push_lr)) * 4;
                        let mut addr = self.regs[13].wrapping_sub(total);
                        self.regs[13] = addr;
                        for r in 0..8usize {
                            if (list & (1 << r)) != 0 {
                                self.mem.write_u32_le(u64::from(addr), self.regs[r])?;
                                addr = addr.wrapping_add(4);
                            }
                        }
                        if push_lr {
                            self.mem.write_u32_le(u64::from(addr), self.regs[14])?;
                        }
                        Ok(())
                    }
                    12 | 13 => {
                        // POP {reglist, P(PC)}
                        let pop_pc = ((hw >> 8) & 1) != 0;
                        let list = u8::try_from(hw & 0xFF).unwrap_or(0);
                        let mut addr = self.regs[13];
                        for r in 0..8usize {
                            if (list & (1 << r)) != 0 {
                                self.regs[r] = self.mem.read_u32_le(u64::from(addr))?;
                                addr = addr.wrapping_add(4);
                            }
                        }
                        if pop_pc {
                            let tgt = self.mem.read_u32_le(u64::from(addr))?;
                            addr = addr.wrapping_add(4);
                            self.it_state = 0;
                            self.thumb_mode = (tgt & 1) != 0;
                            self.regs[15] = tgt & !1;
                        }
                        self.regs[13] = addr;
                        Ok(())
                    }
                    15 => {
                        // IT / NOP hints
                        let mask = u8::try_from(hw & 0xF).unwrap_or(0);
                        if mask != 0 {
                            self.it_state = u8::try_from(hw & 0xFF).unwrap_or(0);
                        }
                        Ok(())
                    }
                    _ => Err(emu_err(
                        u64::from(pc),
                        ErrCode::InvalidFunction,
                        format!("unhandled Thumb16 misc 0x{hw:04X} at 0x{pc:08X}"),
                    )),
                }
            }
            // 110: LDMIA / STMIA (`1100`) & Conditional branch B<cond> (`1101`)
            6 => {
                if ((hw >> 12) & 1) == 0 {
                    let is_ldm = ((hw >> 11) & 1) != 0;
                    let rn = usize::from((hw >> 8) & 7);
                    let list = u8::try_from(hw & 0xFF).unwrap_or(0);
                    let mut addr = self.regs[rn];
                    for r in 0..8usize {
                        if (list & (1 << r)) != 0 {
                            if is_ldm {
                                self.regs[r] = self.mem.read_u32_le(u64::from(addr))?;
                            } else {
                                self.mem.write_u32_le(u64::from(addr), self.regs[r])?;
                            }
                            addr = addr.wrapping_add(4);
                        }
                    }
                    if !is_ldm || (list & (1 << rn)) == 0 {
                        self.regs[rn] = addr;
                    }
                } else {
                    let cond = u8::try_from((hw >> 8) & 0xF).unwrap_or(0);
                    let imm8 = i32::from((hw & 0xFF) as u8 as i8) * 2;
                    let taken = self.eval_cond(cond);
                    self.record_cond_edge(pc, taken);
                    if taken {
                        self.it_state = 0;
                        self.regs[15] = pc.wrapping_add(4).wrapping_add(imm8 as u32);
                    }
                }
                Ok(())
            }
            // 111: Unconditional 11-bit B (`11100`)
            _ => {
                if (hw >> 11).trailing_zeros() >= 2 {
                    let imm11 = u32::from(hw & 0x7FF);
                    let simm = (((imm11 << 21) as i32) >> 20) as u32;
                    self.it_state = 0;
                    self.regs[15] = pc.wrapping_add(4).wrapping_add(simm);
                    Ok(())
                } else {
                    Err(emu_err(
                        u64::from(pc),
                        ErrCode::InvalidFunction,
                        format!("unhandled Thumb16 0x{hw:04X} at 0x{pc:08X}"),
                    ))
                }
            }
        }
    }

    const fn thumb_expand_imm_c(imm12: u32, carry_in: bool) -> (u32, bool) {
        if (imm12 >> 10) == 0 {
            let b = imm12 & 0xFF;
            let val = match (imm12 >> 8) & 3 {
                0 => b,
                1 => (b << 16) | b,
                2 => (b << 24) | (b << 8),
                _ => (b << 24) | (b << 16) | (b << 8) | b,
            };
            (val, carry_in)
        } else {
            let unrot = 0x80 | (imm12 & 0x7F);
            let rot = (imm12 >> 7) & 0x1F;
            let val = unrot.rotate_right(rot);
            (val, (val >> 31) != 0)
        }
    }

    #[allow(
        clippy::too_many_lines,
        clippy::cast_possible_wrap,
        clippy::cast_sign_loss
    )]
    fn exec_thumb32(&mut self, pc: u32, hw1: u16, hw2: u16) -> Result<(), ToolError> {
        let op1 = (hw1 >> 11) & 3;
        let op2 = (hw1 >> 4) & 0x7F;

        // `op1 == 1` (`0xE800..0xEFFF`): Load/Store Multiple (`STMDB`/`PUSH.W`, `LDMIA`/`POP.W`) or Shifted-Reg DP
        if op1 == 1 {
            if (op2 & 0x64) == 0x00 {
                // STM / LDM (`11101 00 P U 0 W L Rn` + `reglist`)
                let p_pre = ((hw1 >> 8) & 1) != 0;
                let u_up = ((hw1 >> 7) & 1) != 0;
                let w_wb = ((hw1 >> 5) & 1) != 0;
                let l_load = ((hw1 >> 4) & 1) != 0;
                let rn = usize::from(hw1 & 0xF);
                let list = hw2;
                let count = list.count_ones() * 4;
                let base = self.regs[rn];
                let mut addr = match (u_up, p_pre) {
                    (true, false) => base,
                    (true, true) => base.wrapping_add(4),
                    (false, false) => base.wrapping_sub(count).wrapping_add(4),
                    (false, true) => base.wrapping_sub(count),
                };
                let wb_val = if u_up {
                    base.wrapping_add(count)
                } else {
                    base.wrapping_sub(count)
                };
                for r in 0..16usize {
                    if (list & (1 << r)) != 0 {
                        if l_load {
                            let v = self.mem.read_u32_le(u64::from(addr))?;
                            if r == 15 {
                                self.it_state = 0;
                                self.thumb_mode = (v & 1) != 0;
                                self.regs[15] = v & !1;
                            } else {
                                self.regs[r] = v;
                            }
                        } else {
                            self.mem.write_u32_le(u64::from(addr), self.regs[r])?;
                        }
                        addr = addr.wrapping_add(4);
                    }
                }
                if w_wb && (!l_load || (list & (1 << rn)) == 0) {
                    self.regs[rn] = wb_val;
                }
                return Ok(());
            }

            if (op2 & 0x60) == 0x20 {
                // Data-processing (shifted register): `11101 01 op(4) S Rn | 0 imm3 Rd imm2 type Rm`
                let dp_op = (hw1 >> 5) & 0xF;
                let s_set = ((hw1 >> 4) & 1) != 0;
                let rn = usize::from(hw1 & 0xF);
                let rd = usize::from((hw2 >> 8) & 0xF);
                let rm = usize::from(hw2 & 0xF);
                let imm5 = u32::from(((hw2 >> 12) & 7) << 2 | ((hw2 >> 6) & 3));
                let stype = u8::try_from((hw2 >> 4) & 3).unwrap_or(0);
                let (val2, sh_c) =
                    Self::barrel_shift(self.regs[rm], stype, imm5, false, self.nzcv.2);
                self.exec_t2_dp_op(dp_op, s_set, rn, rd, val2, sh_c)?;
                return Ok(());
            }

            // Coprocessor MRC (`MRC p15, 0, Rd, c13, c0, 2` = `0xEE1D Rt 0x0F50` -> TEB!)
            if (hw1 & 0xFFF0) == 0xEE10 && (hw2 & 0x0FFF) == 0x0F50 {
                let rt = usize::from((hw2 >> 12) & 0xF);
                self.regs[rt] = TEB_ADDR;
                return Ok(());
            }
        }

        // `op1 == 2` (`0xF000..0xF7FF`): Modified immediate DP, Plain 12/16-bit immediate DP, Branches & Barriers
        if op1 == 2 {
            if (hw2 & 0x8000) == 0 {
                let i_bit = u32::from((hw1 >> 10) & 1);
                let imm3 = u32::from((hw2 >> 12) & 7);
                let imm8 = u32::from(hw2 & 0xFF);
                let rd = usize::from((hw2 >> 8) & 0xF);
                let rn = usize::from(hw1 & 0xF);
                if ((hw1 >> 9) & 1) == 0 {
                    // Modified immediate DP (`11110 i 0 op(4) S Rn | 0 imm3 Rd imm8`)
                    let dp_op = (hw1 >> 5) & 0xF;
                    let s_set = ((hw1 >> 4) & 1) != 0;
                    let imm12 = (i_bit << 11) | (imm3 << 8) | imm8;
                    let (val2, sh_c) = Self::thumb_expand_imm_c(imm12, self.nzcv.2);
                    self.exec_t2_dp_op(dp_op, s_set, rn, rd, val2, sh_c)?;
                    return Ok(());
                }
                // Plain binary immediate (`ADDW`, `SUBW`, `MOVW`, `MOVT`)
                let sub_op = (hw1 >> 4) & 0x1F;
                let imm12 = (i_bit << 11) | (imm3 << 8) | imm8;
                let imm16 = (u32::from(hw1 & 0xF) << 12) | imm12;
                match sub_op {
                    0x00 => {
                        let base = if rn == 15 {
                            pc.wrapping_add(4) & !3
                        } else {
                            self.regs[rn]
                        };
                        self.regs[rd] = base.wrapping_add(imm12);
                        return Ok(());
                    }
                    0x0A => {
                        let base = if rn == 15 {
                            pc.wrapping_add(4) & !3
                        } else {
                            self.regs[rn]
                        };
                        self.regs[rd] = base.wrapping_sub(imm12);
                        return Ok(());
                    }
                    0x04 => {
                        self.regs[rd] = imm16;
                        return Ok(());
                    }
                    0x0C => {
                        self.regs[rd] = (self.regs[rd] & 0x0000_FFFF) | (imm16 << 16);
                        return Ok(());
                    }
                    _ => {}
                }
            } else {
                // Branches & Misc Control (`hw2 & 0x8000 != 0`)
                let b_op = (hw2 >> 12) & 7;
                if (b_op & 5) == 0 {
                    // Conditional B<cond>.W (`11110 S cond(4) imm6 | 10 J1 0 J2 imm11`) or DMB/DSB/ISB (`cond == 111x`)
                    let cond = u8::try_from((hw1 >> 6) & 0xF).unwrap_or(0);
                    if cond >= 0xE {
                        // DMB / DSB / ISB / NOP.W barrier
                        return Ok(());
                    }
                    let s = u32::from((hw1 >> 10) & 1);
                    let j1 = u32::from((hw2 >> 13) & 1);
                    let j2 = u32::from((hw2 >> 11) & 1);
                    let imm6 = u32::from(hw1 & 0x3F);
                    let imm11 = u32::from(hw2 & 0x7FF);
                    let raw21 = (s << 20) | (j2 << 19) | (j1 << 18) | (imm6 << 12) | (imm11 << 1);
                    let simm = (((raw21 << 11) as i32) >> 11) as u32;
                    let taken = self.eval_cond(cond);
                    self.record_cond_edge(pc, taken);
                    if taken {
                        self.it_state = 0;
                        self.regs[15] = pc.wrapping_add(4).wrapping_add(simm);
                    }
                    return Ok(());
                }
                if (b_op & 5) == 1 || (b_op & 5) == 5 {
                    // Unconditional B.W (`b_op & 4 == 0`) or BL (`b_op & 4 != 0`)
                    let is_bl = (b_op & 4) != 0;
                    let s = u32::from((hw1 >> 10) & 1);
                    let j1 = u32::from((hw2 >> 13) & 1);
                    let j2 = u32::from((hw2 >> 11) & 1);
                    let i1 = (!(j1 ^ s)) & 1;
                    let i2 = (!(j2 ^ s)) & 1;
                    let imm10 = u32::from(hw1 & 0x3FF);
                    let imm11 = u32::from(hw2 & 0x7FF);
                    let raw25 = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
                    let simm = (((raw25 << 7) as i32) >> 7) as u32;
                    if is_bl {
                        if (self.regs[13] & 7) != 0 {
                            return Err(emu_err(
                                u64::from(pc),
                                ErrCode::InvalidParameter,
                                format!("ARMv7 SP misaligned at BL: 0x{:08X}", self.regs[13]),
                            ));
                        }
                        self.regs[14] = pc.wrapping_add(4) | 1;
                    }
                    self.it_state = 0;
                    self.regs[15] = pc.wrapping_add(4).wrapping_add(simm);
                    return Ok(());
                }
            }
        }

        // `op1 == 3` (`0xF800..0xFFFF`): Single Data Item Load/Store, Reg Shifts, Sign-Ext, CLZ, Mul/Div
        if op1 == 3 {
            // Load/Store single (`11111 00 ...`)
            if (hw1 >> 9).trailing_zeros() >= 2 {
                let u_imm12 = ((hw1 >> 7) & 1) != 0;
                let size = (hw1 >> 5) & 3;
                let l_load = ((hw1 >> 4) & 1) != 0;
                let sign_ext = ((hw1 >> 8) & 1) != 0;
                let rn = usize::from(hw1 & 0xF);
                let rt = usize::from((hw2 >> 12) & 0xF);
                if rt == 15 && l_load && !sign_ext && size == 0 {
                    // PLD memory preload hint -> no-op
                    return Ok(());
                }
                let (eff, wb) = if rn == 15 {
                    let imm12 = u32::from(hw2 & 0xFFF);
                    let base = pc.wrapping_add(4) & !3;
                    let a = if u_imm12 {
                        base.wrapping_add(imm12)
                    } else {
                        base.wrapping_sub(imm12)
                    };
                    (a, None)
                } else if u_imm12 {
                    let imm12 = u32::from(hw2 & 0xFFF);
                    (self.regs[rn].wrapping_add(imm12), None)
                } else if ((hw2 >> 11) & 1) == 0 {
                    // Shifted register offset (`0000 00 imm2 Rm`)
                    let rm = usize::from(hw2 & 0xF);
                    let sh = u32::from((hw2 >> 4) & 3);
                    (self.regs[rn].wrapping_add(self.regs[rm] << sh), None)
                } else {
                    // `1 P U W imm8`
                    let p_pre = ((hw2 >> 10) & 1) != 0;
                    let u_add = ((hw2 >> 9) & 1) != 0;
                    let w_wb = ((hw2 >> 8) & 1) != 0;
                    let imm8 = u32::from(hw2 & 0xFF);
                    let base = self.regs[rn];
                    let mod_a = if u_add {
                        base.wrapping_add(imm8)
                    } else {
                        base.wrapping_sub(imm8)
                    };
                    let eff_a = if p_pre { mod_a } else { base };
                    (eff_a, if !p_pre || w_wb { Some(mod_a) } else { None })
                };

                match (l_load, sign_ext, size) {
                    (false, _, 0) => self.mem.write_u8(
                        u64::from(eff),
                        u8::try_from(self.regs[rt] & 0xFF).unwrap_or(0),
                    )?,
                    (false, _, 1) => self.mem.write_u16_le(
                        u64::from(eff),
                        u16::try_from(self.regs[rt] & 0xFFFF).unwrap_or(0),
                    )?,
                    (false, _, 2) => self.mem.write_u32_le(u64::from(eff), self.regs[rt])?,
                    (true, false, 0) => {
                        self.regs[rt] = u32::from(self.mem.read_u8(u64::from(eff))?);
                    }
                    (true, true, 0) => {
                        let b = self.mem.read_u8(u64::from(eff))? as i8;
                        self.regs[rt] = i32::from(b) as u32;
                    }
                    (true, false, 1) => {
                        self.regs[rt] = u32::from(self.mem.read_u16_le(u64::from(eff))?);
                    }
                    (true, true, 1) => {
                        let h = self.mem.read_u16_le(u64::from(eff))? as i16;
                        self.regs[rt] = i32::from(h) as u32;
                    }
                    (true, false, 2) => {
                        let v = self.mem.read_u32_le(u64::from(eff))?;
                        if rt == 15 {
                            self.it_state = 0;
                            self.thumb_mode = (v & 1) != 0;
                            self.regs[15] = v & !1;
                        } else {
                            self.regs[rt] = v;
                        }
                    }
                    _ => {
                        return Err(emu_err(
                            u64::from(pc),
                            ErrCode::InvalidFunction,
                            format!("unhandled Thumb32 load/store 0x{hw1:04X}_{hw2:04X}"),
                        ));
                    }
                }
                if let Some(new_rn) = wb
                    && (!l_load || rn != rt)
                {
                    self.regs[rn] = new_rn;
                }
                return Ok(());
            }

            // Data-processing (register): Shifts (`LSL.W`/`LSR.W`/`ASR.W`/`ROR.W`), Sign/Zero Extend, `CLZ` (`0xFA00..0xFAFF`)
            if ((hw1 >> 8) & 7) == 2 {
                let rn = usize::from(hw1 & 0xF);
                let rd = usize::from((hw2 >> 8) & 0xF);
                let rm = usize::from(hw2 & 0xF);
                if (hw1 & 0xFF80) == 0xFA00
                    && ((hw2 >> 12) & 0xF) == 0xF
                    && (hw2 >> 6).trailing_zeros() >= 2
                {
                    let st = u8::try_from((hw1 >> 5) & 3).unwrap_or(0);
                    let s_set = ((hw1 >> 4) & 1) != 0;
                    let (r, c) =
                        Self::barrel_shift(self.regs[rn], st, self.regs[rm], true, self.nzcv.2);
                    self.regs[rd] = r;
                    if s_set {
                        self.nzcv = ((r >> 31) != 0, r == 0, c, self.nzcv.3);
                    }
                    return Ok(());
                }
                if (hw1 & 0xFF8F) == 0xFA0F && ((hw2 >> 12) & 0xF) == 0xF && ((hw2 >> 7) & 1) == 1 {
                    // SXTH.W / UXTH.W / SXTB.W / UXTB.W
                    let rot = u32::from((hw2 >> 4) & 3) * 8;
                    let v = self.regs[rm].rotate_right(rot);
                    let ext_op = (hw1 >> 4) & 7;
                    self.regs[rd] = match ext_op {
                        0 => i32::from((v & 0xFFFF) as u16 as i16) as u32,
                        1 => v & 0xFFFF,
                        4 => i32::from((v & 0xFF) as u8 as i8) as u32,
                        _ => v & 0xFF,
                    };
                    return Ok(());
                }
                if (hw1 & 0xFFF0) == 0xFAB0 && (hw2 & 0xF0F0) == 0xF080 {
                    // CLZ Rd, Rm
                    self.regs[rd] = self.regs[rm].leading_zeros();
                    return Ok(());
                }
            }

            // Multiply (`MUL`, `MLA`, `MLS`) & Long Multiply / Hardware Divide (`UMULL`, `SMULL`, `SDIV`, `UDIV`) (`0xFB00..0xFBFF`)
            if ((hw1 >> 8) & 7) == 3 {
                let rn = usize::from(hw1 & 0xF);
                let rm = usize::from(hw2 & 0xF);
                if ((hw1 >> 7) & 1) == 0 {
                    let rd = usize::from((hw2 >> 8) & 0xF);
                    let ra = usize::from((hw2 >> 12) & 0xF);
                    let sub_kind = (hw2 >> 4) & 0xF;
                    if (hw1 & 0xFFF0) == 0xFB00 && sub_kind == 0 {
                        let prod = self.regs[rn].wrapping_mul(self.regs[rm]);
                        self.regs[rd] = if ra == 15 {
                            prod
                        } else {
                            prod.wrapping_add(self.regs[ra])
                        };
                        return Ok(());
                    }
                    if (hw1 & 0xFFF0) == 0xFB00 && sub_kind == 1 {
                        // MLS Rd, Rn, Rm, Ra = Ra - Rn * Rm
                        let prod = self.regs[rn].wrapping_mul(self.regs[rm]);
                        self.regs[rd] = self.regs[ra].wrapping_sub(prod);
                        return Ok(());
                    }
                } else {
                    let rd_lo = usize::from((hw2 >> 12) & 0xF);
                    let rd_hi = usize::from((hw2 >> 8) & 0xF);
                    let op_hi = (hw1 >> 4) & 7;
                    let op_lo = (hw2 >> 4) & 0xF;
                    match (op_hi, op_lo) {
                        // SMULL
                        (0, 0) => {
                            let p = (i64::from(self.regs[rn] as i32)
                                * i64::from(self.regs[rm] as i32))
                                as u64;
                            self.regs[rd_lo] = u32::try_from(p & 0xFFFF_FFFF).unwrap_or(0);
                            self.regs[rd_hi] = u32::try_from(p >> 32).unwrap_or(0);
                            return Ok(());
                        }
                        // UMULL
                        (2, 0) => {
                            let p = u64::from(self.regs[rn]) * u64::from(self.regs[rm]);
                            self.regs[rd_lo] = u32::try_from(p & 0xFFFF_FFFF).unwrap_or(0);
                            self.regs[rd_hi] = u32::try_from(p >> 32).unwrap_or(0);
                            return Ok(());
                        }
                        // SDIV (Rd = rd_hi, Rn, Rm)
                        (1, 0xF) => {
                            let a = self.regs[rn] as i32;
                            let b = self.regs[rm] as i32;
                            let q = if b == 0 { 0 } else { a.wrapping_div(b) as u32 };
                            self.regs[rd_hi] = q;
                            return Ok(());
                        }
                        // UDIV (Rd = rd_hi, Rn, Rm)
                        (3, 0xF) => {
                            let a = self.regs[rn];
                            let b = self.regs[rm];
                            self.regs[rd_hi] = a.checked_div(b).unwrap_or(0);
                            return Ok(());
                        }
                        _ => {}
                    }
                }
            }
        }

        Err(emu_err(
            u64::from(pc),
            ErrCode::InvalidFunction,
            format!("unhandled Thumb32 instruction 0x{hw1:04X}_{hw2:04X} at 0x{pc:08X}"),
        ))
    }

    fn exec_t2_dp_op(
        &mut self,
        dp_op: u16,
        s_set: bool,
        rn: usize,
        rd: usize,
        val2: u32,
        sh_c: bool,
    ) -> Result<(), ToolError> {
        let rn_v = self.regs[rn];
        let v_old = self.nzcv.3;
        match dp_op {
            0x0 => {
                let r = rn_v & val2;
                if rd != 15 {
                    self.regs[rd] = r;
                }
                if s_set || rd == 15 {
                    self.nzcv = ((r >> 31) != 0, r == 0, sh_c, v_old);
                }
            }
            0x1 => {
                let r = rn_v & !val2;
                self.regs[rd] = r;
                if s_set {
                    self.nzcv = ((r >> 31) != 0, r == 0, sh_c, v_old);
                }
            }
            0x2 => {
                let r = if rn == 15 { val2 } else { rn_v | val2 };
                self.regs[rd] = r;
                if s_set {
                    self.nzcv = ((r >> 31) != 0, r == 0, sh_c, v_old);
                }
            }
            0x3 => {
                let r = if rn == 15 { !val2 } else { rn_v | !val2 };
                self.regs[rd] = r;
                if s_set {
                    self.nzcv = ((r >> 31) != 0, r == 0, sh_c, v_old);
                }
            }
            0x4 => {
                let r = rn_v ^ val2;
                if rd != 15 {
                    self.regs[rd] = r;
                }
                if s_set || rd == 15 {
                    self.nzcv = ((r >> 31) != 0, r == 0, sh_c, v_old);
                }
            }
            0x8 => {
                let (r, fl) = Self::adc_flags(rn_v, val2, false);
                if rd != 15 {
                    self.regs[rd] = r;
                }
                if s_set || rd == 15 {
                    self.nzcv = fl;
                }
            }
            0xA => {
                let (r, fl) = Self::adc_flags(rn_v, val2, self.nzcv.2);
                self.regs[rd] = r;
                if s_set {
                    self.nzcv = fl;
                }
            }
            0xB => {
                let (r, fl) = Self::adc_flags(rn_v, !val2, self.nzcv.2);
                self.regs[rd] = r;
                if s_set {
                    self.nzcv = fl;
                }
            }
            0xD => {
                let (r, fl) = Self::adc_flags(rn_v, !val2, true);
                if rd != 15 {
                    self.regs[rd] = r;
                }
                if s_set || rd == 15 {
                    self.nzcv = fl;
                }
            }
            0xE => {
                let (r, fl) = Self::adc_flags(val2, !rn_v, true);
                self.regs[rd] = r;
                if s_set {
                    self.nzcv = fl;
                }
            }
            _ => {
                return Err(emu_err(
                    u64::from(self.regs[15]),
                    ErrCode::InvalidFunction,
                    format!("unhandled Thumb32 DP op 0x{dp_op:X}"),
                ));
            }
        }
        Ok(())
    }
}
