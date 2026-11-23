//! Deterministic user-mode `IA-32` (`0x014C` `PE32`) and `x86-64` (`0x8664` `PE32+`)
//! emulator and instruction/branch-edge coverage engine (`X86PeVm`), backed by
//! [`SparseGuestMem`] and [`WinProcState`].

use crate::arm32_pe::SparseGuestMem;
use crate::win_mock::{BS, DQ, GuestMem, SharedWinEnv, WinProcState, emu_err};
use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet};
use std::rc::Rc;
use wincmd_diag::{ErrCode, ToolError};

const STACK_BASE: u64 = 0x7400_0000;
const STACK_SIZE: u64 = 0x0010_0000;
const TEB_ADDR: u64 = 0x7500_0000;
const CMDLINE_W_ADDR: u64 = 0x7501_0000;
const CMDLINE_A_ADDR: u64 = 0x7501_8000;
const ENV_W_ADDR: u64 = 0x7502_0000;
const THUNK_BASE: u64 = 0x7600_0000;
const EXIT_PC: u64 = 0x76FF_FFF0;

const CF: u64 = 1 << 0;
const PF: u64 = 1 << 2;
const AF: u64 = 1 << 4;
const ZF: u64 = 1 << 6;
const SF: u64 = 1 << 7;
const DF: u64 = 1 << 10;
const OF: u64 = 1 << 11;

type ThunkMap = BTreeMap<u64, (String, u32)>;

const PFX_REP_F2: u8 = 1 << 0;
const PFX_REP_F3: u8 = 1 << 1;
const PFX_HAS_REX: u8 = 1 << 2;

#[derive(Clone, Copy)]
struct DecodeCtx {
    insn_pc: u64,
    pfx: u8,
    rex: u8,
    op_bits: u8,
    seg_base: u64,
}

impl DecodeCtx {
    const fn has_rex(self) -> bool {
        (self.pfx & PFX_HAS_REX) != 0
    }

    const fn is_rep(self) -> bool {
        (self.pfx & (PFX_REP_F2 | PFX_REP_F3)) != 0
    }
}

const fn lo_u8(v: u64) -> u8 {
    v.to_le_bytes()[0]
}

const fn lo_u16(v: u64) -> u16 {
    let b = v.to_le_bytes();
    u16::from_le_bytes([b[0], b[1]])
}

const fn lo_u32(v: u64) -> u32 {
    let b = v.to_le_bytes();
    u32::from_le_bytes([b[0], b[1], b[2], b[3]])
}

const fn u128_lo64(v: u128) -> u64 {
    let b = v.to_le_bytes();
    u64::from_le_bytes([b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]])
}

const fn u128_hi64(v: u128) -> u64 {
    let b = v.to_le_bytes();
    u64::from_le_bytes([b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]])
}

#[derive(Clone, Copy)]
enum OpOperand {
    Reg(u8),
    AbsAddr(u64),
    RipRel(i32),
}

impl OpOperand {
    const fn resolve(self, next_ip: u64, is_64: bool) -> Option<u64> {
        match self {
            Self::Reg(_) => None,
            Self::AbsAddr(a) => Some(if is_64 { a } else { a & 0xFFFF_FFFF }),
            Self::RipRel(disp) => Some(next_ip.wrapping_add_signed(disp as i64)),
        }
    }
}

/// Execution result of an `IA-32` `PE32` or `x86-64` `PE32+` run in [`X86PeVm`].
#[derive(Clone, Debug)]
pub struct X86RunResult {
    /// Process exit code passed to `ExitProcess` or returned from `mainCRTStartup`.
    pub exit_code: u32,
    /// Captured UTF-8 `stdout` stream.
    pub stdout: String,
    /// Captured UTF-8 `stderr` stream.
    pub stderr: String,
    /// Total instructions retired.
    pub steps: u64,
    /// Distinct instruction addresses executed (`PC` coverage).
    pub executed_pcs: BTreeSet<u64>,
    /// Conditional branch edges `(branch_pc, taken)` observed.
    pub branch_edges: BTreeSet<(u64, bool)>,
}

/// Deterministic `IA-32` (`PE32`) & `x86-64` (`PE32+`) user-mode virtual machine.
pub struct X86PeVm {
    /// True when executing 64-bit `0x8664` `PE32+`; false for 32-bit `0x014C` `PE32`.
    pub is_64bit: bool,
    /// General-purpose registers (`RAX=0, RCX=1, RDX=2, RBX=3, RSP=4, RBP=5, RSI=6, RDI=7, R8..R15`).
    pub regs: [u64; 16],
    /// Instruction pointer (`EIP` / `RIP`).
    pub ip: u64,
    /// CPU `EFLAGS` / `RFLAGS` register.
    pub flags: u64,
    /// Sparse guest virtual address space.
    pub mem: SparseGuestMem,
    /// Simulated Win32/Win64 OS state.
    pub os: WinProcState,
    /// Imported API thunks keyed by synthetic thunk VA -> `(clean_api_name, stdcall_pop_bytes)`.
    thunks: ThunkMap,
    next_thunk: u64,
}

struct MappedPe {
    is_64bit: bool,
    entry_rva: u32,
    image_base: u64,
    imp_rva: u32,
    mem: SparseGuestMem,
}

impl X86PeVm {
    fn map_pe_image(pe: &[u8]) -> Result<MappedPe, ToolError> {
        if pe.len() < 0x100 || &pe[0..2] != b"MZ" {
            return Err(emu_err(0, ErrCode::BadExeFormat, "invalid MZ header"));
        }
        let lfa = usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]]))
            .unwrap_or(0);
        if lfa + 24 > pe.len() || &pe[lfa..lfa + 4] != b"PE\0\0" {
            return Err(emu_err(0, ErrCode::BadExeFormat, "invalid PE signature"));
        }
        let machine = u16::from_le_bytes([pe[lfa + 4], pe[lfa + 5]]);
        let is_64bit = match machine {
            0x014C => false,
            0x8664 => true,
            other => {
                return Err(emu_err(
                    0,
                    ErrCode::BadExeFormat,
                    format!("unsupported x86 PE machine 0x{other:04X}"),
                ));
            }
        };
        let num_secs = usize::from(u16::from_le_bytes([pe[lfa + 6], pe[lfa + 7]]));
        let opt_sz = usize::from(u16::from_le_bytes([pe[lfa + 20], pe[lfa + 21]]));
        let opt = lfa + 24;
        if opt + opt_sz > pe.len() {
            return Err(emu_err(0, ErrCode::BadExeFormat, "truncated optional header"));
        }
        let magic = u16::from_le_bytes([pe[opt], pe[opt + 1]]);
        if (is_64bit && magic != 0x020B) || (!is_64bit && magic != 0x010B) {
            return Err(emu_err(0, ErrCode::BadExeFormat, "mismatched PE32/PE32+ magic"));
        }

        let entry_rva = u32::from_le_bytes([pe[opt + 16], pe[opt + 17], pe[opt + 18], pe[opt + 19]]);
        let image_base = if is_64bit {
            u64::from_le_bytes([
                pe[opt + 24],
                pe[opt + 25],
                pe[opt + 26],
                pe[opt + 27],
                pe[opt + 28],
                pe[opt + 29],
                pe[opt + 30],
                pe[opt + 31],
            ])
        } else {
            u64::from(u32::from_le_bytes([
                pe[opt + 28],
                pe[opt + 29],
                pe[opt + 30],
                pe[opt + 31],
            ]))
        };
        let size_headers = usize::try_from(u32::from_le_bytes([
            pe[opt + 60],
            pe[opt + 61],
            pe[opt + 62],
            pe[opt + 63],
        ]))
        .unwrap_or(0x400)
        .min(pe.len());

        let mut mem = SparseGuestMem::new(0x0100_0000);
        mem.map_range(
            image_base,
            u64::try_from(size_headers.max(4096)).unwrap_or(4096),
        );
        mem.write_slice(image_base, &pe[..size_headers])?;
        Self::map_pe_sections(pe, opt + opt_sz, num_secs, image_base, &mut mem)?;

        let imp_dd = opt + if is_64bit { 120 } else { 104 };
        let imp_rva = u32::from_le_bytes([
            pe[imp_dd],
            pe[imp_dd + 1],
            pe[imp_dd + 2],
            pe[imp_dd + 3],
        ]);
        Ok(MappedPe {
            is_64bit,
            entry_rva,
            image_base,
            imp_rva,
            mem,
        })
    }

    fn map_pe_sections(
        pe: &[u8],
        sec_table: usize,
        num_secs: usize,
        image_base: u64,
        mem: &mut SparseGuestMem,
    ) -> Result<(), ToolError> {
        for i in 0..num_secs {
            let sh = sec_table + i * 40;
            if sh + 40 > pe.len() {
                break;
            }
            let virt_sz = usize::try_from(u32::from_le_bytes([
                pe[sh + 8],
                pe[sh + 9],
                pe[sh + 10],
                pe[sh + 11],
            ]))
            .unwrap_or(0);
            let virt_rva = u64::from(u32::from_le_bytes([
                pe[sh + 12],
                pe[sh + 13],
                pe[sh + 14],
                pe[sh + 15],
            ]));
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
            let map_len = u64::try_from(virt_sz.max(raw_sz).max(4096)).unwrap_or(4096);
            mem.map_range(image_base.wrapping_add(virt_rva), map_len);
            if raw_sz > 0 && raw_ptr < pe.len() {
                let end = raw_ptr.saturating_add(raw_sz).min(pe.len());
                mem.write_slice(image_base.wrapping_add(virt_rva), &pe[raw_ptr..end])?;
            }
        }
        Ok(())
    }

    fn init_teb_and_peb(mem: &mut SparseGuestMem, is_64bit: bool) -> Result<(), ToolError> {
        mem.map_range(STACK_BASE, STACK_SIZE);
        mem.map_range(TEB_ADDR, 0x30000);
        mem.map_range(THUNK_BASE, 0x10000);
        mem.map_range(EXIT_PC & !0xFFF, 4096);

        let peb_addr = TEB_ADDR + 0x1000;
        if is_64bit {
            mem.write_u64_le(TEB_ADDR, 0xFFFF_FFFF_FFFF_FFFF)?;
            mem.write_u64_le(TEB_ADDR + 0x30, TEB_ADDR)?;
            mem.write_u64_le(TEB_ADDR + 0x60, peb_addr)?;
            mem.write_u32_le(peb_addr + 0x118, 10)?;
            mem.write_u32_le(peb_addr + 0x11C, 0)?;
            mem.write_u16_le(peb_addr + 0x120, 22631)?;
        } else {
            let teb32 = lo_u32(TEB_ADDR);
            let peb32 = lo_u32(peb_addr);
            mem.write_u32_le(TEB_ADDR, 0xFFFF_FFFF)?;
            mem.write_u32_le(TEB_ADDR + 0x18, teb32)?;
            mem.write_u32_le(TEB_ADDR + 0x30, peb32)?;
            mem.write_u32_le(peb_addr + 0xA4, 10)?;
            mem.write_u32_le(peb_addr + 0xA8, 0)?;
            mem.write_u16_le(peb_addr + 0xAC, 22631)?;
        }
        Ok(())
    }

    /// Loads a `0x014C` (`IA-32` `PE32`) or `0x8664` (`x86-64` `PE32+`) image into a fresh VM.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if the PE header or import directory is malformed.
    pub fn from_pe_bytes(
        pe: &[u8],
        cmdline_tail: &str,
        env_kv: &[(&str, &str)],
        shared: Rc<RefCell<SharedWinEnv>>,
    ) -> Result<Self, ToolError> {
        let MappedPe {
            is_64bit,
            entry_rva,
            image_base,
            imp_rva,
            mut mem,
        } = Self::map_pe_image(pe)?;
        Self::init_teb_and_peb(&mut mem, is_64bit)?;

        let (thunks, next_thunk) =
            Self::bind_imports(&mut mem, image_base, imp_rva, is_64bit)?;

        let mut os = WinProcState::new(false, is_64bit, (10, 0, 19045), shared);
        os.self_exe = if is_64bit {
            format!("C:{BS}Windows{BS}System32{BS}WINCMD64.EXE")
        } else {
            format!("C:{BS}Windows{BS}System32{BS}WINCMD32.EXE")
        };
        Self::populate_startup_env(&mut mem, &mut os, cmdline_tail, env_kv)?;

        let pe_copy = pe.to_vec();
        let env_copy: Vec<(String, String)> = env_kv
            .iter()
            .map(|(k, v)| ((*k).to_owned(), (*v).to_owned()))
            .collect();
        os.child_runner = Some(Box::new(move |child_cmd, child_shared, in_h, out_h, err_h| {
            let kv_refs: Vec<(&str, &str)> = env_copy
                .iter()
                .map(|(k, v)| (k.as_str(), v.as_str()))
                .collect();
            let mut child = Self::from_pe_bytes(&pe_copy, child_cmd, &kv_refs, child_shared)?;
            let _ = child.os.bind_inherited_stdio(0, in_h);
            let _ = child.os.bind_inherited_stdio(1, out_h);
            let _ = child.os.bind_inherited_stdio(2, err_h);
            let res = child.run(25_000_000)?;
            Ok((res.exit_code, res.stdout.into_bytes(), res.stderr.into_bytes()))
        }));

        let mut regs = [0u64; 16];
        let mut init_sp = STACK_BASE + STACK_SIZE - 0x100;
        if is_64bit {
            init_sp -= 8;
            mem.write_u64_le(init_sp, EXIT_PC)?;
        } else {
            init_sp -= 4;
            mem.write_u32_le(init_sp, lo_u32(EXIT_PC))?;
        }
        regs[4] = init_sp;

        Ok(Self {
            is_64bit,
            regs,
            ip: image_base.wrapping_add(u64::from(entry_rva)),
            flags: 0x0202,
            mem,
            os,
            thunks,
            next_thunk,
        })
    }

    fn clean_import_and_stdcall(raw: &str, is_64bit: bool) -> (String, u32) {
        if is_64bit {
            return (raw.to_owned(), 0);
        }
        let trimmed = raw.strip_prefix('_').unwrap_or(raw);
        if let Some((stem, bytes_s)) = trimmed.rsplit_once('@')
            && let Ok(pop_b) = bytes_s.parse::<u32>()
        {
            return (stem.to_owned(), pop_b);
        }
        let pop = Self::dynamic_stdcall_bytes(trimmed);
        (trimmed.to_owned(), pop)
    }

    fn dynamic_stdcall_bytes(api: &str) -> u32 {
        match api {
            "CloseHandle" | "DeleteFileW" | "DeleteFileA" | "FreeEnvironmentStringsW"
            | "FreeEnvironmentStringsA" | "FreeLibrary" | "GetDriveTypeW" | "GetDriveTypeA"
            | "GetFileAttributesW" | "GetFileAttributesA" | "GetFileType" | "GetLocalTime"
            | "GetModuleHandleA" | "GetModuleHandleW" | "GetStdHandle" | "GlobalMemoryStatus"
            | "GlobalMemoryStatusEx" | "IsDBCSLeadByte" | "LoadLibraryA" | "LoadLibraryW"
            | "LocalFree" | "RegCloseKey" | "RemoveDirectoryW" | "RemoveDirectoryA"
            | "RtlGetVersion" | "SetConsoleCP" | "SetConsoleOutputCP" | "SetConsoleTitleW"
            | "SetConsoleTitleA" | "SetCurrentDirectoryW" | "SetCurrentDirectoryA"
            | "SetEndOfFile" | "SetErrorMode" | "SetLastError" | "SetLocalTime"
            | "SetUnhandledExceptionFilter" | "ShellExecuteExW" | "ShellExecuteExA" | "Sleep"
            | "Wow64DisableWow64FsRedirection" | "Wow64RevertWow64FsRedirection"
            | "FlushFileBuffers" | "FindClose" | "ExitProcess" => 4,
            "CompareFileTime" | "FindFirstFileW" | "FindFirstFileA" | "FindNextFileW"
            | "FindNextFileA" | "FindNextStreamW" | "GetCPInfo" | "GetComputerNameW"
            | "GetComputerNameA" | "GetConsoleMode" | "GetConsoleScreenBufferInfo"
            | "GetCurrentDirectoryW" | "GetCurrentDirectoryA" | "GetExitCodeProcess"
            | "GetFileSize" | "GetFileSizeEx" | "GetMachineTypeAttributes" | "GetProcAddress"
            | "GetTempPathW" | "GetTempPathA" | "IsDBCSLeadByteEx"
            | "LocalFileTimeToFileTime" | "FileTimeToLocalFileTime" | "FileTimeToSystemTime"
            | "SystemTimeToFileTime" | "MoveFileW" | "MoveFileA" | "RegDeleteKeyW"
            | "RegDeleteValueW" | "SetConsoleCtrlHandler" | "SetConsoleCursorPosition"
            | "SetConsoleMode" | "SetConsoleScreenBufferSize" | "SetConsoleTextAttribute"
            | "SetEnvironmentVariableW" | "SetEnvironmentVariableA" | "SetFileAttributesW"
            | "SetFileAttributesA" | "SetStdHandle" | "TerminateProcess"
            | "WaitForSingleObject" | "CreateDirectoryW" | "CreateDirectoryA" => 8,
            "CopyFileW" | "CopyFileA" | "CreateHardLinkW" | "CreateSymbolicLinkW"
            | "GetComputerNameExW" | "GetEnvironmentVariableW" | "GetEnvironmentVariableA"
            | "GetModuleFileNameW" | "GetModuleFileNameA" | "GetProcessMemoryInfo"
            | "K32GetProcessMemoryInfo" | "GetShortPathNameW" | "GetShortPathNameA"
            | "GetLongPathNameW" | "MoveFileExW" | "OpenProcessToken" | "RegDeleteKeyExW"
            | "SetHandleInformation" | "VirtualFree" => 12,
            "CreatePipe" | "FindFirstStreamW" | "GetDiskFreeSpaceExW" | "GetDiskFreeSpaceExA"
            | "GetFinalPathNameByHandleW" | "GetFileTime" | "GetFullPathNameW"
            | "GetFullPathNameA" | "LookupPrivilegeNameW" | "SetFilePointer" | "SetFileTime"
            | "VirtualAlloc" | "WaitForMultipleObjects" => 16,
            "CreatePseudoConsole" | "FillConsoleOutputAttribute"
            | "FillConsoleOutputCharacterW" | "FillConsoleOutputCharacterA"
            | "GetDiskFreeSpaceW" | "GetDiskFreeSpaceA" | "GetTokenInformation"
            | "NtQueryInformationFile" | "ReadConsoleInputW" | "ReadConsoleInputA"
            | "ReadConsoleW" | "ReadConsoleA" | "ReadFile" | "RegOpenKeyExW" | "RegOpenKeyExA"
            | "SetFilePointerEx" | "WriteConsoleW" | "WriteConsoleA" | "WriteFile" => 20,
            "CopyFileExW" | "CreateThread" | "FindFirstFileExW" | "MultiByteToWideChar"
            | "RegQueryValueExW" | "RegQueryValueExA" | "RegSetValueExW" | "RegSetValueExA" => 24,
            "CreateFileW" | "CreateFileA" | "DuplicateHandle" | "FormatMessageW"
            | "FormatMessageA" | "LookupAccountSidW" => 28,
            "DeviceIoControl" | "GetVolumeInformationW" | "GetVolumeInformationA"
            | "RegEnumKeyExW" | "RegEnumValueW" | "WideCharToMultiByte" => 32,
            "RegCreateKeyExW" | "RegCreateKeyExA" => 36,
            "CreateProcessW" | "CreateProcessA" => 40,
            _ => 0,
        }
    }

    fn bind_imports(
        mem: &mut SparseGuestMem,
        image_base: u64,
        imp_rva: u32,
        is_64bit: bool,
    ) -> Result<(ThunkMap, u64), ToolError> {
        let mut thunks = ThunkMap::new();
        let mut next_thunk = THUNK_BASE;
        if imp_rva == 0 {
            return Ok((thunks, next_thunk));
        }
        let ptr_sz: u64 = if is_64bit { 8 } else { 4 };
        let ord_flag: u64 = if is_64bit { 1u64 << 63 } else { 1u64 << 31 };
        let mut desc = image_base.wrapping_add(u64::from(imp_rva));
        for _ in 0..64 {
            let ilt = mem.read_u32_le(desc)?;
            let name_rva = mem.read_u32_le(desc.wrapping_add(12))?;
            let iat = mem.read_u32_le(desc.wrapping_add(16))?;
            if ilt == 0 && name_rva == 0 && iat == 0 {
                break;
            }
            let src_rva = if ilt != 0 { ilt } else { iat };
            for slot in 0u64..512 {
                let entry_addr = image_base
                    .wrapping_add(u64::from(src_rva))
                    .wrapping_add(slot * ptr_sz);
                let iat_addr = image_base
                    .wrapping_add(u64::from(iat))
                    .wrapping_add(slot * ptr_sz);
                let raw = mem.read_ptr(entry_addr, is_64bit)?;
                if raw == 0 {
                    break;
                }
                let raw_name = if (raw & ord_flag) != 0 {
                    format!("Ordinal_{}", raw & 0xFFFF)
                } else {
                    let hint_name = image_base.wrapping_add(raw & 0x7FFF_FFFF);
                    mem.read_cstr(hint_name.wrapping_add(2))?
                };
                let (clean, pop_b) = Self::clean_import_and_stdcall(&raw_name, is_64bit);
                let tva = next_thunk;
                next_thunk = next_thunk.wrapping_add(16);
                thunks.insert(tva, (clean, pop_b));
                mem.write_ptr(iat_addr, tva, is_64bit)?;
            }
            desc = desc.wrapping_add(20);
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
        let upper_prefix = trimmed.to_ascii_uppercase();
        let starts_with_exe = trimmed.starts_with(DQ) || upper_prefix.starts_with("WINCMD");
        let has_argv0 = !trimmed.is_empty() && !trimmed.starts_with('/') && starts_with_exe;
        let full_cmd = if has_argv0 {
            cmdline_tail.to_owned()
        } else if trimmed.is_empty() {
            format!("{DQ}{}{DQ}", os.self_exe)
        } else {
            format!("{DQ}{}{DQ} {cmdline_tail}", os.self_exe)
        };
        let _ = mem.write_wstr(CMDLINE_W_ADDR, &full_cmd, 8192)?;
        os.cmdline_w_addr = CMDLINE_W_ADDR;

        let mut ansi_bytes = full_cmd.as_bytes().to_vec();
        ansi_bytes.push(0);
        mem.write_slice(CMDLINE_A_ADDR, &ansi_bytes)?;
        os.cmdline_a_addr = CMDLINE_A_ADDR;

        let mut env_pos = ENV_W_ADDR;
        let defaults = [
            ("COMSPEC", os.self_exe.as_str()),
            ("PATHEXT", ".COM;.EXE;.BAT;.CMD"),
            ("PATH", &format!("C:{BS}Windows{BS}System32;C:{BS}Windows")),
            ("SYSTEMROOT", &format!("C:{BS}Windows")),
            ("TEMP", &format!("C:{BS}Temp")),
            ("TMP", &format!("C:{BS}Temp")),
            ("OS", "Windows_NT"),
        ];
        for (k, v) in defaults {
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
        os.env_w_addr = ENV_W_ADDR;
        Ok(())
    }

    /// Supplies standard input bytes (`ReadFile(hStdIn)` / `CONIN$`) before calling [`Self::run`].
    pub fn set_stdin(&mut self, data: &[u8]) {
        self.os.stdin_buf = data.to_vec();
        self.os.stdin_pos = 0;
    }

    /// Executes the loaded `PE32` or `PE32+` image until exit or `max_steps`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on unmapped memory access, Win64 stack misalignment,
    /// unknown opcode, or exceeding `max_steps`.
    pub fn run(&mut self, max_steps: u64) -> Result<X86RunResult, ToolError> {
        self.run_with_coverage(max_steps, true)
    }

    /// Executes the loaded `PE32` or `PE32+` image with optional PC/branch coverage collection.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on unmapped memory access, Win64 stack misalignment,
    /// unknown opcode, or exceeding `max_steps`.
    pub fn run_with_coverage(
        &mut self,
        max_steps: u64,
        record_cov: bool,
    ) -> Result<X86RunResult, ToolError> {
        let mut executed_pcs = BTreeSet::new();
        let mut branch_edges = BTreeSet::new();
        let mut steps = 0u64;

        while steps < max_steps {
            if let Some(code) = self.os.exited {
                return Ok(self.finish(code, steps, executed_pcs, branch_edges));
            }
            let pc = if self.is_64bit {
                self.ip
            } else {
                self.ip & 0xFFFF_FFFF
            };
            if pc == EXIT_PC {
                let code = u32::try_from(self.regs[0] & 0xFFFF_FFFF).unwrap_or(0);
                return Ok(self.finish(code, steps, executed_pcs, branch_edges));
            }
            if (THUNK_BASE..THUNK_BASE + 0x10000).contains(&pc)
                && let Some((api_name, pop_b)) = self.thunks.get(&pc).cloned()
            {
                self.invoke_thunk(&api_name, pop_b)?;
                continue;
            }

            if record_cov {
                let _ = executed_pcs.insert(pc);
                self.step_one(Some(&mut branch_edges))?;
            } else {
                self.step_one(None)?;
            }
            steps += 1;
        }

        Err(emu_err(
            self.ip,
            ErrCode::InvalidFunction,
            format!("X86PeVm exceeded max_steps ({max_steps})"),
        ))
    }

    fn finish(
        &self,
        exit_code: u32,
        steps: u64,
        executed_pcs: BTreeSet<u64>,
        branch_edges: BTreeSet<(u64, bool)>,
    ) -> X86RunResult {
        X86RunResult {
            exit_code,
            stdout: String::from_utf8_lossy(&self.os.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&self.os.stderr).into_owned(),
            steps,
            executed_pcs,
            branch_edges,
        }
    }

    fn invoke_thunk(&mut self, api_name: &str, pop_bytes: u32) -> Result<(), ToolError> {
        if self.is_64bit {
            self.invoke_thunk_64(api_name)
        } else {
            self.invoke_thunk_32(api_name, pop_bytes)
        }
    }

    fn invoke_thunk_64(&mut self, api_name: &str) -> Result<(), ToolError> {
        if (self.regs[4] & 0xF) != 8 {
            return Err(emu_err(
                self.ip,
                ErrCode::GeneralFailure,
                format!(
                    "Win64 ABI 16-byte stack alignment violation at `{api_name}`: RSP=0x{:X}",
                    self.regs[4]
                ),
            ));
        }
        let ret_rip = self.pop_u64()?;
        if api_name == "CreateThread" {
            let start_addr = self.regs[8];
            let param = self.regs[9];
            let tid_ptr = self.mem.read_u64_le(self.regs[4].wrapping_add(40))?;
            if tid_ptr != 0 {
                self.mem.write_u32_le(tid_ptr, 0x1A64)?;
            }
            let stk = self.mem.alloc_pages(0, 65536)?;
            let saved_regs = self.regs;
            let saved_flags = self.flags;
            let wsp = stk + 65536 - 0x48;
            self.mem.write_u64_le(wsp, EXIT_PC)?;
            self.regs[1] = param;
            self.regs[4] = wsp;
            self.ip = start_addr;
            let mut tsteps = 0u64;
            while self.ip != EXIT_PC && self.os.exited.is_none() && tsteps < 5_000_000 {
                let cur = self.ip;
                if (THUNK_BASE..THUNK_BASE + 0x10000).contains(&cur)
                    && let Some((t_api, t_pop)) = self.thunks.get(&cur).cloned()
                {
                    self.invoke_thunk(&t_api, t_pop)?;
                } else {
                    self.step_one(None)?;
                }
                tsteps += 1;
            }
            self.regs = saved_regs;
            self.ip = ret_rip;
            self.flags = saved_flags;
            self.regs[0] = self.os.alloc_thread_handle();
            return Ok(());
        }
        let reg_args = [self.regs[1], self.regs[2], self.regs[8], self.regs[9]];
        let stack_base = self.regs[4].wrapping_add(32);
        let mut next_t = self.next_thunk;
        let thunks_ref = &mut self.thunks;
        let ret = self.os.dispatch_api(
            &mut self.mem,
            api_name,
            &reg_args,
            stack_base,
            &mut |dyn_fn| {
                for (&addr, (existing, _)) in thunks_ref.iter() {
                    if existing == dyn_fn {
                        return addr;
                    }
                }
                let addr = next_t;
                next_t = next_t.wrapping_add(16);
                thunks_ref.insert(addr, (dyn_fn.to_owned(), 0));
                addr
            },
        )?;
        self.next_thunk = next_t;
        self.regs[0] = ret;
        self.ip = ret_rip;
        Ok(())
    }

    fn invoke_thunk_32(&mut self, api_name: &str, pop_bytes: u32) -> Result<(), ToolError> {
        let ret_eip = u64::from(self.pop_u32()?);
        let stack_base = self.regs[4] & 0xFFFF_FFFF;
        if api_name == "CreateThread" {
            let start_addr = u64::from(self.mem.read_u32_le(stack_base.wrapping_add(8))?);
            let param = u64::from(self.mem.read_u32_le(stack_base.wrapping_add(12))?);
            let tid_ptr = u64::from(self.mem.read_u32_le(stack_base.wrapping_add(20))?);
            if tid_ptr != 0 {
                self.mem.write_u32_le(tid_ptr, 0x1A32)?;
            }
            let stk = self.mem.alloc_pages(0, 65536)?;
            let saved_regs = self.regs;
            let saved_flags = self.flags;
            let mut wsp = (stk + 65536 - 32) & 0xFFFF_FFF0;
            wsp -= 4;
            self.mem.write_u32_le(wsp, lo_u32(param))?;
            wsp -= 4;
            self.mem.write_u32_le(wsp, lo_u32(EXIT_PC))?;
            self.regs[4] = wsp;
            self.ip = start_addr;
            let mut tsteps = 0u64;
            while (self.ip & 0xFFFF_FFFF) != EXIT_PC
                && self.os.exited.is_none()
                && tsteps < 5_000_000
            {
                let cur = self.ip & 0xFFFF_FFFF;
                if (THUNK_BASE..THUNK_BASE + 0x10000).contains(&cur)
                    && let Some((t_api, t_pop)) = self.thunks.get(&cur).cloned()
                {
                    self.invoke_thunk(&t_api, t_pop)?;
                } else {
                    self.step_one(None)?;
                }
                tsteps += 1;
            }
            self.regs = saved_regs;
            self.flags = saved_flags;
            self.regs[4] = stack_base.wrapping_add(24) & 0xFFFF_FFFF;
            self.regs[0] = self.os.alloc_thread_handle() & 0xFFFF_FFFF;
            self.ip = ret_eip;
            return Ok(());
        }
        let mut next_t = self.next_thunk;
        let thunks_ref = &mut self.thunks;
        let ret = self.os.dispatch_api(
            &mut self.mem,
            api_name,
            &[],
            stack_base,
            &mut |dyn_fn| {
                for (&addr, (existing, _)) in thunks_ref.iter() {
                    if existing == dyn_fn {
                        return addr;
                    }
                }
                let addr = next_t;
                next_t = next_t.wrapping_add(16);
                let pop_b = Self::dynamic_stdcall_bytes(dyn_fn);
                thunks_ref.insert(addr, (dyn_fn.to_owned(), pop_b));
                addr
            },
        )?;
        self.next_thunk = next_t;
        self.regs[4] = stack_base.wrapping_add(u64::from(pop_bytes)) & 0xFFFF_FFFF;
        self.regs[0] = ret & 0xFFFF_FFFF;
        self.ip = ret_eip;
        Ok(())
    }

    fn read_reg(&self, idx: u8, bits: u8, has_rex: bool) -> u64 {
        match bits {
            8 => {
                if !has_rex && (4..8).contains(&idx) {
                    (self.regs[usize::from(idx - 4)] >> 8) & 0xFF
                } else {
                    self.regs[usize::from(idx & 15)] & 0xFF
                }
            }
            16 => self.regs[usize::from(idx & 15)] & 0xFFFF,
            32 => self.regs[usize::from(idx & 15)] & 0xFFFF_FFFF,
            _ => self.regs[usize::from(idx & 15)],
        }
    }

    fn write_reg(&mut self, idx: u8, bits: u8, has_rex: bool, val: u64) {
        let r = usize::from(idx & 15);
        match bits {
            8 => {
                if !has_rex && (4..8).contains(&idx) {
                    let base = usize::from(idx - 4);
                    self.regs[base] = (self.regs[base] & !0xFF00) | ((val & 0xFF) << 8);
                } else {
                    self.regs[r] = (self.regs[r] & !0xFF) | (val & 0xFF);
                }
            }
            16 => {
                self.regs[r] = (self.regs[r] & !0xFFFF) | (val & 0xFFFF);
            }
            32 => {
                // Both IA-32 and x86-64 zero-extend 32-bit writes to 64 bits.
                self.regs[r] = val & 0xFFFF_FFFF;
            }
            _ => {
                self.regs[r] = val;
            }
        }
    }

    fn read_mem_sized(&self, addr: u64, bits: u8) -> Result<u64, ToolError> {
        match bits {
            8 => self.mem.read_u8(addr).map(u64::from),
            16 => self.mem.read_u16_le(addr).map(u64::from),
            32 => self.mem.read_u32_le(addr).map(u64::from),
            _ => self.mem.read_u64_le(addr),
        }
    }

    fn write_mem_sized(&mut self, addr: u64, bits: u8, val: u64) -> Result<(), ToolError> {
        match bits {
            8 => self.mem.write_u8(addr, u8::try_from(val & 0xFF).unwrap_or(0)),
            16 => self
                .mem
                .write_u16_le(addr, u16::try_from(val & 0xFFFF).unwrap_or(0)),
            32 => self
                .mem
                .write_u32_le(addr, u32::try_from(val & 0xFFFF_FFFF).unwrap_or(0)),
            _ => self.mem.write_u64_le(addr, val),
        }
    }

    fn read_op(
        &self,
        op: OpOperand,
        bits: u8,
        has_rex: bool,
        next_ip: u64,
    ) -> Result<u64, ToolError> {
        match op {
            OpOperand::Reg(r) => Ok(self.read_reg(r, bits, has_rex)),
            other => {
                let addr = other.resolve(next_ip, self.is_64bit).unwrap_or(0);
                self.read_mem_sized(addr, bits)
            }
        }
    }

    fn write_op(
        &mut self,
        op: OpOperand,
        bits: u8,
        has_rex: bool,
        next_ip: u64,
        val: u64,
    ) -> Result<(), ToolError> {
        match op {
            OpOperand::Reg(r) => {
                self.write_reg(r, bits, has_rex, val);
                Ok(())
            }
            other => {
                let addr = other.resolve(next_ip, self.is_64bit).unwrap_or(0);
                self.write_mem_sized(addr, bits, val)
            }
        }
    }

    fn push_u32(&mut self, val: u32) -> Result<(), ToolError> {
        let sp = (self.regs[4].wrapping_sub(4)) & 0xFFFF_FFFF;
        self.regs[4] = sp;
        self.mem.write_u32_le(sp, val)
    }

    fn pop_u32(&mut self) -> Result<u32, ToolError> {
        let sp = self.regs[4] & 0xFFFF_FFFF;
        let v = self.mem.read_u32_le(sp)?;
        self.regs[4] = sp.wrapping_add(4) & 0xFFFF_FFFF;
        Ok(v)
    }

    fn push_u64(&mut self, val: u64) -> Result<(), ToolError> {
        let sp = self.regs[4].wrapping_sub(8);
        self.regs[4] = sp;
        self.mem.write_u64_le(sp, val)
    }

    fn pop_u64(&mut self) -> Result<u64, ToolError> {
        let sp = self.regs[4];
        let v = self.mem.read_u64_le(sp)?;
        self.regs[4] = sp.wrapping_add(8);
        Ok(v)
    }

    fn fetch_u8(&self, cur: &mut u64) -> Result<u8, ToolError> {
        let b = self.mem.read_u8(*cur)?;
        *cur = cur.wrapping_add(1);
        Ok(b)
    }

    fn fetch_u16(&self, cur: &mut u64) -> Result<u16, ToolError> {
        let v = self.mem.read_u16_le(*cur)?;
        *cur = cur.wrapping_add(2);
        Ok(v)
    }

    fn fetch_u32(&self, cur: &mut u64) -> Result<u32, ToolError> {
        let v = self.mem.read_u32_le(*cur)?;
        *cur = cur.wrapping_add(4);
        Ok(v)
    }

    fn fetch_u64(&self, cur: &mut u64) -> Result<u64, ToolError> {
        let v = self.mem.read_u64_le(*cur)?;
        *cur = cur.wrapping_add(8);
        Ok(v)
    }

    fn fetch_imm_sext(&self, cur: &mut u64, imm_bits: u8) -> Result<u64, ToolError> {
        match imm_bits {
            8 => Ok(i64::from(self.fetch_u8(cur)?.cast_signed()).cast_unsigned()),
            16 => Ok(i64::from(self.fetch_u16(cur)?.cast_signed()).cast_unsigned()),
            32 => Ok(i64::from(self.fetch_u32(cur)?.cast_signed()).cast_unsigned()),
            _ => self.fetch_u64(cur),
        }
    }

    fn decode_modrm(
        &self,
        cur: &mut u64,
        rex: u8,
        seg_base: u64,
    ) -> Result<(u8, OpOperand), ToolError> {
        let mrm = self.fetch_u8(cur)?;
        let md = mrm >> 6;
        let reg = ((mrm >> 3) & 7) | ((rex & 4) << 1);
        let rm_low = mrm & 7;
        let rm = rm_low | ((rex & 1) << 3);

        if md == 3 {
            return Ok((reg, OpOperand::Reg(rm)));
        }

        if md == 0 && rm_low == 5 {
            let disp = self.fetch_u32(cur)?.cast_signed();
            if self.is_64bit {
                if seg_base != 0 {
                    return Ok((
                        reg,
                        OpOperand::AbsAddr(seg_base.wrapping_add_signed(i64::from(disp))),
                    ));
                }
                return Ok((reg, OpOperand::RipRel(disp)));
            }
            let addr = seg_base.wrapping_add(u64::from(disp.cast_unsigned())) & 0xFFFF_FFFF;
            return Ok((reg, OpOperand::AbsAddr(addr)));
        }

        let base_addr = if rm_low == 4 {
            let sib = self.fetch_u8(cur)?;
            let scale = sib >> 6;
            let idx_low = (sib >> 3) & 7;
            let base_low = sib & 7;
            let idx_reg = idx_low | ((rex & 2) << 2);
            let base_reg = base_low | ((rex & 1) << 3);

            let base_val = if md == 0 && base_low == 5 {
                let d32 = self.fetch_u32(cur)?.cast_signed();
                0u64.wrapping_add_signed(i64::from(d32))
            } else {
                self.regs[usize::from(base_reg)]
            };
            let idx_val = if idx_low == 4 && (rex & 2) == 0 {
                0
            } else {
                self.regs[usize::from(idx_reg)] << scale
            };
            base_val.wrapping_add(idx_val)
        } else {
            self.regs[usize::from(rm)]
        };

        let disp: i64 = match md {
            1 => i64::from(self.fetch_u8(cur)?.cast_signed()),
            2 => i64::from(self.fetch_u32(cur)?.cast_signed()),
            _ => 0,
        };
        let eff = seg_base.wrapping_add(base_addr.wrapping_add_signed(disp));
        Ok((
            reg,
            OpOperand::AbsAddr(if self.is_64bit { eff } else { eff & 0xFFFF_FFFF }),
        ))
    }

    const fn mask_for(bits: u8) -> u64 {
        match bits {
            8 => 0xFF,
            16 => 0xFFFF,
            32 => 0xFFFF_FFFF,
            _ => u64::MAX,
        }
    }

    const fn sign_bit(bits: u8) -> u64 {
        1u64 << (bits - 1)
    }

    const fn set_szp(&mut self, res: u64, bits: u8) {
        let m = res & Self::mask_for(bits);
        let sb = Self::sign_bit(bits);
        self.flags &= !(ZF | SF | PF);
        if m == 0 {
            self.flags |= ZF;
        }
        if (m & sb) != 0 {
            self.flags |= SF;
        }
        if lo_u8(m).count_ones().is_multiple_of(2) {
            self.flags |= PF;
        }
    }

    fn alu_op(&mut self, op: u8, a: u64, b: u64, bits: u8) -> u64 {
        let mask = Self::mask_for(bits);
        let sb = Self::sign_bit(bits);
        let ma = a & mask;
        let mb = b & mask;
        let cf_in = u64::from((self.flags & CF) != 0);

        match op {
            0 | 2 => {
                // ADD (0) / ADC (2)
                let cin = if op == 2 { cf_in } else { 0 };
                let sum128 = u128::from(ma) + u128::from(mb) + u128::from(cin);
                let res = u128_lo64(sum128) & mask;
                let cf = sum128 > u128::from(mask);
                let of = ((!(ma ^ mb) & (ma ^ res)) & sb) != 0;
                let af = ((ma ^ mb ^ res) & 0x10) != 0;
                self.flags &= !(CF | OF | AF);
                if cf {
                    self.flags |= CF;
                }
                if of {
                    self.flags |= OF;
                }
                if af {
                    self.flags |= AF;
                }
                self.set_szp(res, bits);
                res
            }
            3 | 5 | 7 => {
                // SBB (3) / SUB (5) / CMP (7)
                let cin = if op == 3 { cf_in } else { 0 };
                let subtrahend = u128::from(mb) + u128::from(cin);
                let cf = u128::from(ma) < subtrahend;
                let res = ma.wrapping_sub(mb).wrapping_sub(cin) & mask;
                let of = (((ma ^ mb) & (ma ^ res)) & sb) != 0;
                let af = ((ma ^ mb ^ res) & 0x10) != 0;
                self.flags &= !(CF | OF | AF);
                if cf {
                    self.flags |= CF;
                }
                if of {
                    self.flags |= OF;
                }
                if af {
                    self.flags |= AF;
                }
                self.set_szp(res, bits);
                res
            }
            1 => {
                // OR
                let res = (ma | mb) & mask;
                self.flags &= !(CF | OF | AF);
                self.set_szp(res, bits);
                res
            }
            4 => {
                // AND / TEST
                let res = (ma & mb) & mask;
                self.flags &= !(CF | OF | AF);
                self.set_szp(res, bits);
                res
            }
            _ => {
                // XOR (6)
                let res = (ma ^ mb) & mask;
                self.flags &= !(CF | OF | AF);
                self.set_szp(res, bits);
                res
            }
        }
    }

    fn shift_op(&mut self, subop: u8, val: u64, raw_cnt: u64, bits: u8) -> u64 {
        let cnt_mask = if bits == 64 { 63 } else { 31 };
        let cnt = u32::try_from(raw_cnt & cnt_mask).unwrap_or(0);
        let mask = Self::mask_for(bits);
        let v = val & mask;
        if cnt == 0 {
            return v;
        }
        let sb = Self::sign_bit(bits);
        match subop {
            0 => {
                // ROL
                let eff = cnt % u32::from(bits);
                let res = if eff == 0 {
                    v
                } else {
                    ((v << eff) | (v >> (u32::from(bits) - eff))) & mask
                };
                let cf = (res & 1) != 0;
                self.flags = (self.flags & !CF) | if cf { CF } else { 0 };
                res
            }
            1 => {
                // ROR
                let eff = cnt % u32::from(bits);
                let res = if eff == 0 {
                    v
                } else {
                    ((v >> eff) | (v << (u32::from(bits) - eff))) & mask
                };
                let cf = (res & sb) != 0;
                self.flags = (self.flags & !CF) | if cf { CF } else { 0 };
                res
            }
            4 | 6 => {
                // SHL / SAL
                let cf = ((v >> (u32::from(bits) - cnt)) & 1) != 0;
                let res = (v << cnt) & mask;
                self.flags &= !(CF | OF);
                if cf {
                    self.flags |= CF;
                }
                if cnt == 1 && (((res & sb) != 0) ^ cf) {
                    self.flags |= OF;
                }
                self.set_szp(res, bits);
                res
            }
            5 => {
                // SHR
                let cf = ((v >> (cnt - 1)) & 1) != 0;
                let res = (v >> cnt) & mask;
                self.flags &= !(CF | OF);
                if cf {
                    self.flags |= CF;
                }
                if cnt == 1 && (v & sb) != 0 {
                    self.flags |= OF;
                }
                self.set_szp(res, bits);
                res
            }
            7 => {
                // SAR
                let cf = ((v >> (cnt - 1)) & 1) != 0;
                let sext = if (v & sb) != 0 {
                    v | !mask
                } else {
                    v
                };
                let res = ((sext.cast_signed() >> cnt).cast_unsigned()) & mask;
                self.flags &= !(CF | OF);
                if cf {
                    self.flags |= CF;
                }
                self.set_szp(res, bits);
                res
            }
            _ => v,
        }
    }

    const fn eval_cc(&self, cc: u8) -> bool {
        let cf = (self.flags & CF) != 0;
        let pf = (self.flags & PF) != 0;
        let zf = (self.flags & ZF) != 0;
        let sf = (self.flags & SF) != 0;
        let of = (self.flags & OF) != 0;
        match cc & 0xF {
            0x0 => of,
            0x1 => !of,
            0x2 => cf,
            0x3 => !cf,
            0x4 => zf,
            0x5 => !zf,
            0x6 => cf || zf,
            0x7 => !cf && !zf,
            0x8 => sf,
            0x9 => !sf,
            0xA => pf,
            0xB => !pf,
            0xC => sf != of,
            0xD => sf == of,
            0xE => zf || (sf != of),
            _ => !zf && (sf == of),
        }
    }

    fn step_one(
        &mut self,
        mut branch_edges: Option<&mut BTreeSet<(u64, bool)>>,
    ) -> Result<(), ToolError> {
        let insn_pc = if self.is_64bit {
            self.ip
        } else {
            self.ip & 0xFFFF_FFFF
        };
        let mut cur = insn_pc;
        let mut op16 = false;
        let mut pfx: u8 = 0;
        let mut rex: u8 = 0;
        let mut seg_base: u64 = 0;

        let opcode = loop {
            let b = self.fetch_u8(&mut cur)?;
            match b {
                0x66 => op16 = true,
                0x67 | 0x26 | 0x2E | 0x36 | 0x3E | 0xF0 => {}
                0x64 | 0x65 => seg_base = TEB_ADDR,
                0xF2 => pfx |= PFX_REP_F2,
                0xF3 => pfx |= PFX_REP_F3,
                0x40..=0x4F if self.is_64bit => {
                    pfx |= PFX_HAS_REX;
                    rex = b & 0x0F;
                }
                other => break other,
            }
        };

        let op_bits: u8 = if (rex & 8) != 0 {
            64
        } else if op16 {
            16
        } else {
            32
        };
        let ctx = DecodeCtx {
            insn_pc,
            pfx,
            rex,
            op_bits,
            seg_base,
        };

        match opcode {
            0x00..=0x05
            | 0x08..=0x0D
            | 0x10..=0x15
            | 0x18..=0x1D
            | 0x20..=0x25
            | 0x28..=0x2D
            | 0x30..=0x35
            | 0x38..=0x3D => self.step_primary_alu(opcode, &mut cur, ctx)?,
            0x40..=0x5F | 0x63 | 0x68..=0x6B => {
                self.step_stack_and_low(opcode, &mut cur, op16, ctx)?;
            }
            0x70..=0x7F => {
                let rel = i64::from(self.fetch_u8(&mut cur)?.cast_signed());
                let taken = self.eval_cc(opcode & 0x0F);
                if let Some(edges) = branch_edges.as_deref_mut() {
                    let _ = edges.insert((insn_pc, taken));
                }
                self.ip = if taken {
                    cur.wrapping_add_signed(rel)
                } else {
                    cur
                };
            }
            0x0F => self.step_0f(&mut cur, ctx, branch_edges)?,
            other => self.step_mid(other, &mut cur, ctx)?,
        }
        Ok(())
    }

    fn step_primary_alu(
        &mut self,
        opcode: u8,
        cur: &mut u64,
        ctx: DecodeCtx,
    ) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let alu = opcode >> 3;
        let kind = opcode & 7;
        let bits = if (kind & 1) == 0 { 8 } else { ctx.op_bits };
        match kind {
            0 | 1 => {
                let (r, rm) = self.decode_modrm(cur, ctx.rex, ctx.seg_base)?;
                let a = self.read_op(rm, bits, has_rex, *cur)?;
                let b = self.read_reg(r, bits, has_rex);
                let res = self.alu_op(alu, a, b, bits);
                if alu != 7 {
                    self.write_op(rm, bits, has_rex, *cur, res)?;
                }
            }
            2 | 3 => {
                let (r, rm) = self.decode_modrm(cur, ctx.rex, ctx.seg_base)?;
                let a = self.read_reg(r, bits, has_rex);
                let b = self.read_op(rm, bits, has_rex, *cur)?;
                let res = self.alu_op(alu, a, b, bits);
                if alu != 7 {
                    self.write_reg(r, bits, has_rex, res);
                }
            }
            4 => {
                let imm = u64::from(self.fetch_u8(cur)?);
                let a = self.read_reg(0, 8, has_rex);
                let res = self.alu_op(alu, a, imm, 8);
                if alu != 7 {
                    self.write_reg(0, 8, has_rex, res);
                }
            }
            _ => {
                let imm_b = if bits == 16 { 16 } else { 32 };
                let imm = self.fetch_imm_sext(cur, imm_b)?;
                let a = self.read_reg(0, bits, has_rex);
                let res = self.alu_op(alu, a, imm, bits);
                if alu != 7 {
                    self.write_reg(0, bits, has_rex, res);
                }
            }
        }
        self.ip = *cur;
        Ok(())
    }

    fn step_stack_and_low(
        &mut self,
        opcode: u8,
        cur: &mut u64,
        op16: bool,
        ctx: DecodeCtx,
    ) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let rex = ctx.rex;
        let op_bits = ctx.op_bits;
        match opcode {
            0x40..=0x4F => {
                let r = opcode & 7;
                let alu = if opcode < 0x48 { 0 } else { 5 };
                let saved_cf = self.flags & CF;
                let v = self.read_reg(r, op_bits, false);
                let res = self.alu_op(alu, v, 1, op_bits);
                self.flags = (self.flags & !CF) | saved_cf;
                self.write_reg(r, op_bits, false, res);
            }
            0x50..=0x57 => {
                let r = (opcode & 7) | ((rex & 1) << 3);
                if self.is_64bit {
                    self.push_u64(self.regs[usize::from(r)])?;
                } else if op16 {
                    let sp = (self.regs[4].wrapping_sub(2)) & 0xFFFF_FFFF;
                    self.regs[4] = sp;
                    self.mem.write_u16_le(sp, lo_u16(self.regs[usize::from(r)]))?;
                } else {
                    self.push_u32(lo_u32(self.regs[usize::from(r)]))?;
                }
            }
            0x58..=0x5F => {
                let r = (opcode & 7) | ((rex & 1) << 3);
                if self.is_64bit {
                    self.regs[usize::from(r)] = self.pop_u64()?;
                } else if op16 {
                    let sp = self.regs[4] & 0xFFFF_FFFF;
                    let v = u64::from(self.mem.read_u16_le(sp)?);
                    self.regs[4] = sp.wrapping_add(2) & 0xFFFF_FFFF;
                    self.write_reg(r, 16, false, v);
                } else {
                    self.regs[usize::from(r)] = u64::from(self.pop_u32()?);
                }
            }
            0x63 if self.is_64bit => {
                let (r, rm) = self.decode_modrm(cur, rex, ctx.seg_base)?;
                let v32 = lo_u32(self.read_op(rm, 32, has_rex, *cur)?);
                let sext = i64::from(v32.cast_signed()).cast_unsigned();
                self.write_reg(r, op_bits, has_rex, sext);
            }
            0x68 | 0x6A => {
                let imm_b = if opcode == 0x6A {
                    8
                } else if op16 && !self.is_64bit {
                    16
                } else {
                    32
                };
                let imm = self.fetch_imm_sext(cur, imm_b)?;
                if self.is_64bit {
                    self.push_u64(imm)?;
                } else {
                    self.push_u32(lo_u32(imm))?;
                }
            }
            0x69 | 0x6B => {
                let (r, rm) = self.decode_modrm(cur, rex, ctx.seg_base)?;
                let imm_b = if opcode == 0x6B {
                    8
                } else if op_bits == 16 {
                    16
                } else {
                    32
                };
                let imm = self.fetch_imm_sext(cur, imm_b)?;
                let src = self.read_op(rm, op_bits, has_rex, *cur)?;
                let prod = Self::signed_mul(src, imm, op_bits, &mut self.flags);
                self.write_reg(r, op_bits, has_rex, prod);
            }
            other => return self.step_mid(other, cur, ctx),
        }
        self.ip = *cur;
        Ok(())
    }

    fn step_mid(&mut self, opcode: u8, cur: &mut u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let DecodeCtx {
            rex,
            op_bits,
            seg_base,
            ..
        } = ctx;
        match opcode {
            0x80..=0x83 => {
                let bits = if opcode == 0x80 || opcode == 0x82 {
                    8
                } else {
                    op_bits
                };
                let (subop_raw, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let alu = subop_raw & 7;
                let imm_b = if opcode == 0x81 {
                    if bits == 16 { 16 } else { 32 }
                } else {
                    8
                };
                let imm = self.fetch_imm_sext(cur, imm_b)?;
                let a = self.read_op(rm, bits, has_rex, *cur)?;
                let res = self.alu_op(alu, a, imm, bits);
                if alu != 7 {
                    self.write_op(rm, bits, has_rex, *cur, res)?;
                }
                self.ip = *cur;
            }
            0x84 | 0x85 => {
                let bits = if opcode == 0x84 { 8 } else { op_bits };
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let a = self.read_op(rm, bits, has_rex, *cur)?;
                let b = self.read_reg(r, bits, has_rex);
                let _ = self.alu_op(4, a, b, bits);
                self.ip = *cur;
            }
            0x86 | 0x87 => {
                let bits = if opcode == 0x86 { 8 } else { op_bits };
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let a = self.read_op(rm, bits, has_rex, *cur)?;
                let b = self.read_reg(r, bits, has_rex);
                self.write_reg(r, bits, has_rex, a);
                self.write_op(rm, bits, has_rex, *cur, b)?;
                self.ip = *cur;
            }
            0x88..=0x8B => {
                let bits = if (opcode & 1) == 0 { 8 } else { op_bits };
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                if (opcode & 2) == 0 {
                    let v = self.read_reg(r, bits, has_rex);
                    self.write_op(rm, bits, has_rex, *cur, v)?;
                } else {
                    let v = self.read_op(rm, bits, has_rex, *cur)?;
                    self.write_reg(r, bits, has_rex, v);
                }
                self.ip = *cur;
            }
            0x8D => {
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let addr = rm.resolve(*cur, self.is_64bit).unwrap_or(0);
                self.write_reg(r, op_bits, has_rex, addr);
                self.ip = *cur;
            }
            0x8F => {
                let (_, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let v = if self.is_64bit {
                    self.pop_u64()?
                } else {
                    u64::from(self.pop_u32()?)
                };
                self.write_op(rm, if self.is_64bit { 64 } else { 32 }, has_rex, *cur, v)?;
                self.ip = *cur;
            }
            0x90..=0x99 => self.step_accum_conv(opcode, *cur, ctx)?,
            other => return self.step_upper(other, cur, ctx),
        }
        Ok(())
    }

    fn step_accum_conv(&mut self, opcode: u8, cur: u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let op_bits = ctx.op_bits;
        match opcode {
            0x90..=0x97 => {
                let r = (opcode & 7) | ((ctx.rex & 1) << 3);
                if r != 0 {
                    let a = self.read_reg(0, op_bits, has_rex);
                    let b = self.read_reg(r, op_bits, has_rex);
                    self.write_reg(0, op_bits, has_rex, b);
                    self.write_reg(r, op_bits, has_rex, a);
                }
            }
            0x98 => match op_bits {
                16 => {
                    let v = i16::from(lo_u8(self.regs[0]).cast_signed()).cast_unsigned();
                    self.write_reg(0, 16, has_rex, u64::from(v));
                }
                32 => {
                    let v = i32::from(lo_u16(self.regs[0]).cast_signed()).cast_unsigned();
                    self.write_reg(0, 32, has_rex, u64::from(v));
                }
                _ => {
                    self.regs[0] = i64::from(lo_u32(self.regs[0]).cast_signed()).cast_unsigned();
                }
            },
            0x99 => match op_bits {
                16 => {
                    let s = if (self.regs[0] & 0x8000) != 0 { 0xFFFF } else { 0 };
                    self.write_reg(2, 16, has_rex, s);
                }
                32 => {
                    let s = if (self.regs[0] & 0x8000_0000) != 0 {
                        0xFFFF_FFFF
                    } else {
                        0
                    };
                    self.write_reg(2, 32, has_rex, s);
                }
                _ => {
                    self.regs[2] = if (self.regs[0] & (1 << 63)) != 0 {
                        u64::MAX
                    } else {
                        0
                    };
                }
            },
            _ => {
                return Err(emu_err(
                    ctx.insn_pc,
                    ErrCode::InvalidFunction,
                    format!("unhandled opcode 0x{opcode:02X} at 0x{:X}", ctx.insn_pc),
                ));
            }
        }
        self.ip = cur;
        Ok(())
    }

    fn step_string_op(&mut self, opcode: u8, cur: u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let elem_bits: u8 = if (opcode & 1) == 0 { 8 } else { ctx.op_bits };
        let step = u64::from(elem_bits / 8);
        let is_rep = ctx.is_rep();
        let df = (self.flags & DF) != 0;
        let iters = if is_rep {
            if self.is_64bit {
                self.regs[1]
            } else {
                self.regs[1] & 0xFFFF_FFFF
            }
        } else {
            1
        };
        for _ in 0..iters {
            let si = if self.is_64bit { self.regs[6] } else { self.regs[6] & 0xFFFF_FFFF };
            let di = if self.is_64bit { self.regs[7] } else { self.regs[7] & 0xFFFF_FFFF };
            if opcode <= 0xA5 {
                let v = self.read_mem_sized(si, elem_bits)?;
                self.write_mem_sized(di, elem_bits, v)?;
                self.regs[6] = if df { si.wrapping_sub(step) } else { si.wrapping_add(step) };
            } else {
                let v = self.read_reg(0, elem_bits, ctx.has_rex());
                self.write_mem_sized(di, elem_bits, v)?;
            }
            self.regs[7] = if df { di.wrapping_sub(step) } else { di.wrapping_add(step) };
        }
        if is_rep {
            self.regs[1] = 0;
        }
        self.ip = cur;
        Ok(())
    }

    fn step_upper(&mut self, opcode: u8, cur: &mut u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let DecodeCtx {
            rex,
            op_bits,
            seg_base,
            ..
        } = ctx;
        match opcode {
            0x9C => {
                if self.is_64bit {
                    self.push_u64(self.flags)?;
                } else {
                    self.push_u32(lo_u32(self.flags))?;
                }
                self.ip = *cur;
            }
            0x9D => {
                self.flags = if self.is_64bit {
                    self.pop_u64()?
                } else {
                    u64::from(self.pop_u32()?)
                };
                self.ip = *cur;
            }
            0xA0..=0xA3 => {
                let bits = if (opcode & 1) == 0 { 8 } else { op_bits };
                let off = if self.is_64bit {
                    self.fetch_u64(cur)?
                } else {
                    u64::from(self.fetch_u32(cur)?)
                };
                let addr = seg_base.wrapping_add(off);
                if (opcode & 2) == 0 {
                    let v = self.read_mem_sized(addr, bits)?;
                    self.write_reg(0, bits, has_rex, v);
                } else {
                    let v = self.read_reg(0, bits, has_rex);
                    self.write_mem_sized(addr, bits, v)?;
                }
                self.ip = *cur;
            }
            0xA4 | 0xA5 | 0xAA | 0xAB => self.step_string_op(opcode, *cur, ctx)?,
            0xA8 | 0xA9 => {
                let bits = if opcode == 0xA8 { 8 } else { op_bits };
                let imm = match bits {
                    8 => u64::from(self.fetch_u8(cur)?),
                    16 => u64::from(self.fetch_u16(cur)?),
                    _ => self.fetch_imm_sext(cur, 32)?,
                };
                let a = self.read_reg(0, bits, has_rex);
                let _ = self.alu_op(4, a, imm, bits);
                self.ip = *cur;
            }
            0xB0..=0xB7 => {
                let r = (opcode & 7) | ((rex & 1) << 3);
                let imm = u64::from(self.fetch_u8(cur)?);
                self.write_reg(r, 8, has_rex, imm);
                self.ip = *cur;
            }
            0xB8..=0xBF => {
                let r = (opcode & 7) | ((rex & 1) << 3);
                let imm = match op_bits {
                    16 => u64::from(self.fetch_u16(cur)?),
                    32 => u64::from(self.fetch_u32(cur)?),
                    _ => self.fetch_u64(cur)?,
                };
                self.write_reg(r, op_bits, has_rex, imm);
                self.ip = *cur;
            }
            0xC0 | 0xC1 => {
                let bits = if opcode == 0xC0 { 8 } else { op_bits };
                let (subop, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let cnt = u64::from(self.fetch_u8(cur)?);
                let v = self.read_op(rm, bits, has_rex, *cur)?;
                let res = self.shift_op(subop & 7, v, cnt, bits);
                self.write_op(rm, bits, has_rex, *cur, res)?;
                self.ip = *cur;
            }
            other => return self.step_tail(other, cur, ctx),
        }
        Ok(())
    }

    fn step_tail(&mut self, opcode: u8, cur: &mut u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let DecodeCtx {
            insn_pc,
            rex,
            op_bits,
            seg_base,
            ..
        } = ctx;
        match opcode {
            0xC2 | 0xC3 => {
                let pop_extra = if opcode == 0xC2 { u64::from(self.fetch_u16(cur)?) } else { 0 };
                let ret_addr = if self.is_64bit { self.pop_u64()? } else { u64::from(self.pop_u32()?) };
                self.regs[4] = self.regs[4].wrapping_add(pop_extra);
                if !self.is_64bit {
                    self.regs[4] &= 0xFFFF_FFFF;
                }
                self.ip = ret_addr;
            }
            0xC6 | 0xC7 => {
                let bits = if opcode == 0xC6 { 8 } else { op_bits };
                let (_, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let imm = match bits {
                    8 => u64::from(self.fetch_u8(cur)?),
                    16 => u64::from(self.fetch_u16(cur)?),
                    _ => self.fetch_imm_sext(cur, 32)?,
                };
                self.write_op(rm, bits, has_rex, *cur, imm)?;
                self.ip = *cur;
            }
            0xC9 => {
                self.regs[4] = self.regs[5];
                self.regs[5] = if self.is_64bit { self.pop_u64()? } else { u64::from(self.pop_u32()?) };
                self.ip = *cur;
            }
            0xD0..=0xD3 => {
                let bits = if (opcode & 1) == 0 { 8 } else { op_bits };
                let (subop, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let cnt = if (opcode & 2) == 0 { 1 } else { self.regs[1] & 0xFF };
                let v = self.read_op(rm, bits, has_rex, *cur)?;
                let res = self.shift_op(subop & 7, v, cnt, bits);
                self.write_op(rm, bits, has_rex, *cur, res)?;
                self.ip = *cur;
            }
            0xE8 => {
                let rel = i64::from(self.fetch_u32(cur)?.cast_signed());
                if self.is_64bit {
                    self.push_u64(*cur)?;
                } else {
                    self.push_u32(lo_u32(*cur))?;
                }
                self.ip = cur.wrapping_add_signed(rel);
            }
            0xE9 | 0xEB => {
                let rel = if opcode == 0xEB {
                    i64::from(self.fetch_u8(cur)?.cast_signed())
                } else {
                    i64::from(self.fetch_u32(cur)?.cast_signed())
                };
                self.ip = cur.wrapping_add_signed(rel);
            }
            0xF8 => { self.flags &= !CF; self.ip = *cur; }
            0xF9 => { self.flags |= CF; self.ip = *cur; }
            0xFC => { self.flags &= !DF; self.ip = *cur; }
            0xFD => { self.flags |= DF; self.ip = *cur; }
            0xF6 | 0xF7 => self.step_grp3(opcode, cur, ctx)?,
            0xFE | 0xFF => self.step_grp45(opcode, cur, ctx)?,
            other => {
                return Err(emu_err(
                    insn_pc,
                    ErrCode::InvalidFunction,
                    format!("unhandled x86 opcode 0x{other:02X} at 0x{insn_pc:X}"),
                ));
            }
        }
        Ok(())
    }

    fn step_grp3(&mut self, opcode: u8, cur: &mut u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let bits = if opcode == 0xF6 { 8 } else { ctx.op_bits };
        let (subop_r, rm) = self.decode_modrm(cur, ctx.rex, ctx.seg_base)?;
        match subop_r & 7 {
            0 | 1 => {
                let imm = match bits {
                    8 => u64::from(self.fetch_u8(cur)?),
                    16 => u64::from(self.fetch_u16(cur)?),
                    _ => self.fetch_imm_sext(cur, 32)?,
                };
                let a = self.read_op(rm, bits, has_rex, *cur)?;
                let _ = self.alu_op(4, a, imm, bits);
            }
            2 => {
                let a = self.read_op(rm, bits, has_rex, *cur)?;
                self.write_op(rm, bits, has_rex, *cur, (!a) & Self::mask_for(bits))?;
            }
            3 => {
                let a = self.read_op(rm, bits, has_rex, *cur)?;
                let res = self.alu_op(5, 0, a, bits);
                self.write_op(rm, bits, has_rex, *cur, res)?;
            }
            4 => {
                let b = self.read_op(rm, bits, has_rex, *cur)?;
                self.exec_mul(b, bits, false, has_rex);
            }
            5 => {
                let b = self.read_op(rm, bits, has_rex, *cur)?;
                self.exec_mul(b, bits, true, has_rex);
            }
            6 => {
                let b = self.read_op(rm, bits, has_rex, *cur)?;
                self.exec_div(b, bits, false, has_rex)?;
            }
            _ => {
                let b = self.read_op(rm, bits, has_rex, *cur)?;
                self.exec_div(b, bits, true, has_rex)?;
            }
        }
        self.ip = *cur;
        Ok(())
    }

    fn step_grp45(&mut self, opcode: u8, cur: &mut u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let bits = if opcode == 0xFE { 8 } else { ctx.op_bits };
        let (subop_r, rm) = self.decode_modrm(cur, ctx.rex, ctx.seg_base)?;
        match subop_r & 7 {
            0 => {
                let saved_cf = self.flags & CF;
                let v = self.read_op(rm, bits, has_rex, *cur)?;
                let res = self.alu_op(0, v, 1, bits);
                self.flags = (self.flags & !CF) | saved_cf;
                self.write_op(rm, bits, has_rex, *cur, res)?;
                self.ip = *cur;
            }
            1 => {
                let saved_cf = self.flags & CF;
                let v = self.read_op(rm, bits, has_rex, *cur)?;
                let res = self.alu_op(5, v, 1, bits);
                self.flags = (self.flags & !CF) | saved_cf;
                self.write_op(rm, bits, has_rex, *cur, res)?;
                self.ip = *cur;
            }
            2 if opcode == 0xFF => {
                let ptr_bits = if self.is_64bit { 64 } else { 32 };
                let target = self.read_op(rm, ptr_bits, has_rex, *cur)?;
                if self.is_64bit {
                    self.push_u64(*cur)?;
                } else {
                    self.push_u32(lo_u32(*cur))?;
                }
                self.ip = target;
            }
            4 if opcode == 0xFF => {
                let ptr_bits = if self.is_64bit { 64 } else { 32 };
                self.ip = self.read_op(rm, ptr_bits, has_rex, *cur)?;
            }
            6 if opcode == 0xFF => {
                let ptr_bits = if self.is_64bit { 64 } else { ctx.op_bits };
                let v = self.read_op(rm, ptr_bits, has_rex, *cur)?;
                if self.is_64bit {
                    self.push_u64(v)?;
                } else {
                    self.push_u32(lo_u32(v))?;
                }
                self.ip = *cur;
            }
            other => {
                return Err(emu_err(
                    ctx.insn_pc,
                    ErrCode::InvalidFunction,
                    format!("unhandled FF/{other} at 0x{:X}", ctx.insn_pc),
                ));
            }
        }
        Ok(())
    }

    fn step_0f(
        &mut self,
        cur: &mut u64,
        ctx: DecodeCtx,
        branch_edges: Option<&mut BTreeSet<(u64, bool)>>,
    ) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let DecodeCtx {
            insn_pc,
            rex,
            op_bits,
            seg_base,
            ..
        } = ctx;
        let op2 = self.fetch_u8(cur)?;
        match op2 {
            0x1E | 0x1F => {
                let next_b = self.mem.read_u8(*cur)?;
                if op2 == 0x1E && (next_b == 0xFA || next_b == 0xFB) {
                    *cur = cur.wrapping_add(1);
                } else {
                    let _ = self.decode_modrm(cur, rex, seg_base)?;
                }
                self.ip = *cur;
            }
            0x40..=0x4F => {
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let v = self.read_op(rm, op_bits, has_rex, *cur)?;
                if self.eval_cc(op2 & 0x0F) {
                    self.write_reg(r, op_bits, has_rex, v);
                } else if op_bits == 32 && self.is_64bit {
                    let cur_v = self.read_reg(r, 32, has_rex);
                    self.write_reg(r, 32, has_rex, cur_v);
                }
                self.ip = *cur;
            }
            0x80..=0x8F => {
                let rel = i64::from(self.fetch_u32(cur)?.cast_signed());
                let taken = self.eval_cc(op2 & 0x0F);
                if let Some(edges) = branch_edges {
                    let _ = edges.insert((insn_pc, taken));
                }
                self.ip = if taken { cur.wrapping_add_signed(rel) } else { *cur };
            }
            0x90..=0x9F => {
                let (_, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let b = u64::from(self.eval_cc(op2 & 0x0F));
                self.write_op(rm, 8, has_rex, *cur, b)?;
                self.ip = *cur;
            }
            other => return self.step_0f_ext(other, cur, ctx),
        }
        Ok(())
    }

    fn step_0f_ext(&mut self, op2: u8, cur: &mut u64, ctx: DecodeCtx) -> Result<(), ToolError> {
        let has_rex = ctx.has_rex();
        let DecodeCtx {
            insn_pc,
            rex,
            op_bits,
            seg_base,
            ..
        } = ctx;
        match op2 {
            0xA3 | 0xAB => {
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let bit_idx = self.read_reg(r, op_bits, has_rex) & u64::from(op_bits - 1);
                let v = self.read_op(rm, op_bits, has_rex, *cur)?;
                let bit_on = ((v >> bit_idx) & 1) != 0;
                self.flags = (self.flags & !CF) | if bit_on { CF } else { 0 };
                if op2 == 0xAB {
                    self.write_op(rm, op_bits, has_rex, *cur, v | (1u64 << bit_idx))?;
                }
                self.ip = *cur;
            }
            0xAC | 0xAD => {
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let cnt_raw = if op2 == 0xAC {
                    u64::from(self.fetch_u8(cur)?)
                } else {
                    self.regs[1] & 0xFF
                };
                let cnt = u32::try_from(cnt_raw & if op_bits == 64 { 63 } else { 31 }).unwrap_or(0);
                if cnt > 0 {
                    let dst = self.read_op(rm, op_bits, has_rex, *cur)?;
                    let src = self.read_reg(r, op_bits, has_rex);
                    let mask = Self::mask_for(op_bits);
                    let cf = ((dst >> (cnt - 1)) & 1) != 0;
                    let res = ((dst >> cnt) | (src << (u32::from(op_bits) - cnt))) & mask;
                    self.flags = (self.flags & !CF) | if cf { CF } else { 0 };
                    self.set_szp(res, op_bits);
                    self.write_op(rm, op_bits, has_rex, *cur, res)?;
                }
                self.ip = *cur;
            }
            0xAF => {
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let a = self.read_reg(r, op_bits, has_rex);
                let b = self.read_op(rm, op_bits, has_rex, *cur)?;
                let prod = Self::signed_mul(a, b, op_bits, &mut self.flags);
                self.write_reg(r, op_bits, has_rex, prod);
                self.ip = *cur;
            }
            0xB6 | 0xB7 => {
                let src_b = if op2 == 0xB6 { 8 } else { 16 };
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let v = self.read_op(rm, src_b, has_rex && src_b == 8, *cur)?;
                self.write_reg(r, op_bits, has_rex, v);
                self.ip = *cur;
            }
            0xBA => {
                let (subop, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let imm = self.fetch_u8(cur)?;
                let bit_idx = u64::from(imm) & u64::from(op_bits - 1);
                let v = self.read_op(rm, op_bits, has_rex, *cur)?;
                let bit_on = ((v >> bit_idx) & 1) != 0;
                self.flags = (self.flags & !CF) | if bit_on { CF } else { 0 };
                match subop & 7 {
                    5 => self.write_op(rm, op_bits, has_rex, *cur, v | (1u64 << bit_idx))?,
                    6 => self.write_op(rm, op_bits, has_rex, *cur, v & !(1u64 << bit_idx))?,
                    7 => self.write_op(rm, op_bits, has_rex, *cur, v ^ (1u64 << bit_idx))?,
                    _ => {}
                }
                self.ip = *cur;
            }
            0xBE | 0xBF => {
                let src_b = if op2 == 0xBE { 8 } else { 16 };
                let (r, rm) = self.decode_modrm(cur, rex, seg_base)?;
                let v = self.read_op(rm, src_b, has_rex && src_b == 8, *cur)?;
                let sext = if src_b == 8 {
                    i64::from(lo_u8(v).cast_signed()).cast_unsigned()
                } else {
                    i64::from(lo_u16(v).cast_signed()).cast_unsigned()
                };
                self.write_reg(r, op_bits, has_rex, sext);
                self.ip = *cur;
            }
            other => {
                return Err(emu_err(
                    insn_pc,
                    ErrCode::InvalidFunction,
                    format!("unhandled 0x0F 0x{other:02X} at 0x{insn_pc:X}"),
                ));
            }
        }
        Ok(())
    }

    const fn signed_mul(a: u64, b: u64, bits: u8, flags: &mut u64) -> u64 {
        let sa = Self::sext_to_i128(a, bits);
        let sb = Self::sext_to_i128(b, bits);
        let full = sa * sb;
        let res = u128_lo64(full.cast_unsigned()) & Self::mask_for(bits);
        let fits = Self::sext_to_i128(res, bits) == full;
        *flags &= !(CF | OF);
        if !fits {
            *flags |= CF | OF;
        }
        res
    }

    const fn sext_to_i128(v: u64, bits: u8) -> i128 {
        match bits {
            8 => lo_u8(v).cast_signed() as i128,
            16 => lo_u16(v).cast_signed() as i128,
            32 => lo_u32(v).cast_signed() as i128,
            _ => v.cast_signed() as i128,
        }
    }

    fn write_mul_product(&mut self, ufull: u128, bits: u8, has_rex: bool) {
        let lo = u128_lo64(ufull);
        let hi = u128_hi64(ufull);
        match bits {
            8 => self.write_reg(0, 16, has_rex, lo & 0xFFFF),
            16 => {
                self.write_reg(0, 16, has_rex, lo & 0xFFFF);
                self.write_reg(2, 16, has_rex, (lo >> 16) & 0xFFFF);
            }
            32 => {
                self.write_reg(0, 32, has_rex, lo & 0xFFFF_FFFF);
                self.write_reg(2, 32, has_rex, (lo >> 32) & 0xFFFF_FFFF);
            }
            _ => {
                self.regs[0] = lo;
                self.regs[2] = hi;
            }
        }
    }

    fn exec_mul(&mut self, b: u64, bits: u8, signed: bool, has_rex: bool) {
        let a = self.read_reg(0, bits, has_rex);
        if signed {
            let full = Self::sext_to_i128(a, bits) * Self::sext_to_i128(b, bits);
            let ufull = full.cast_unsigned();
            self.write_mul_product(ufull, bits, has_rex);
            let lo = u128_lo64(ufull) & Self::mask_for(bits);
            let fits = Self::sext_to_i128(lo, bits) == full;
            self.flags = (self.flags & !(CF | OF)) | if fits { 0 } else { CF | OF };
        } else {
            let ufull = u128::from(a) * u128::from(b);
            self.write_mul_product(ufull, bits, has_rex);
            let hi = ufull >> bits;
            self.flags = (self.flags & !(CF | OF)) | if hi == 0 { 0 } else { CF | OF };
        }
    }

    fn exec_div(&mut self, b: u64, bits: u8, signed: bool, has_rex: bool) -> Result<(), ToolError> {
        if b == 0 {
            return Err(emu_err(self.ip, ErrCode::GeneralFailure, "x86 divide by zero"));
        }
        if signed {
            let num: i128 = match bits {
                8 => i128::from(lo_u16(self.read_reg(0, 16, has_rex)).cast_signed()),
                16 => {
                    let dx = self.read_reg(2, 16, has_rex);
                    let ax = self.read_reg(0, 16, has_rex);
                    i128::from(lo_u32((dx << 16) | ax).cast_signed())
                }
                32 => {
                    let edx = self.read_reg(2, 32, has_rex);
                    let eax = self.read_reg(0, 32, has_rex);
                    i128::from(((edx << 32) | eax).cast_signed())
                }
                _ => ((u128::from(self.regs[2]) << 64) | u128::from(self.regs[0])).cast_signed(),
            };
            let den = Self::sext_to_i128(b, bits);
            let q = u128_lo64((num / den).cast_unsigned());
            let r = u128_lo64((num % den).cast_unsigned());
            if bits == 8 {
                self.write_reg(0, 16, has_rex, (q & 0xFF) | ((r & 0xFF) << 8));
            } else {
                self.write_reg(0, bits, has_rex, q);
                self.write_reg(2, bits, has_rex, r);
            }
        } else {
            let num: u128 = match bits {
                8 => u128::from(self.read_reg(0, 16, has_rex)),
                16 => {
                    let dx = u128::from(self.read_reg(2, 16, has_rex));
                    let ax = u128::from(self.read_reg(0, 16, has_rex));
                    (dx << 16) | ax
                }
                32 => {
                    let edx = u128::from(self.read_reg(2, 32, has_rex));
                    let eax = u128::from(self.read_reg(0, 32, has_rex));
                    (edx << 32) | eax
                }
                _ => (u128::from(self.regs[2]) << 64) | u128::from(self.regs[0]),
            };
            let den = u128::from(b);
            let q = u128_lo64(num / den);
            let r = u128_lo64(num % den);
            if bits == 8 {
                self.write_reg(0, 16, has_rex, (q & 0xFF) | ((r & 0xFF) << 8));
            } else {
                self.write_reg(0, bits, has_rex, q);
                self.write_reg(2, bits, has_rex, r);
            }
        }
        Ok(())
    }
}
