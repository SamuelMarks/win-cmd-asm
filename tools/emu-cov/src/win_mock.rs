//! Shared CRT-free `WinCE` and `WinNT` virtual filesystem, Registry, console, and pipe layer.
//!
//! Provides [`SharedWinEnv`] and [`WinProcState`] for `ARM32` and `ARM64` user-mode VMs.

use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet};
use std::rc::Rc;
use wincmd_diag::{ErrCode, ToolError};

/// ASCII backslash character (`0x5C`).
pub const BS: char = 92 as char;
/// ASCII double-quote character (`0x22`).
pub const DQ: char = 34 as char;

const BS_BYTES: [u8; 1] = [92];
const UNC_BYTES: [u8; 2] = [92, 92];

/// Returns a single-character backslash `&'static str`.
#[must_use]
pub const fn bs_str() -> &'static str {
    match core::str::from_utf8(&BS_BYTES) {
        Ok(s) => s,
        Err(_) => "",
    }
}

/// Returns a two-character UNC prefix `&'static str`.
#[must_use]
pub const fn unc_str() -> &'static str {
    match core::str::from_utf8(&UNC_BYTES) {
        Ok(s) => s,
        Err(_) => "",
    }
}

/// Abstract guest virtual memory interface shared by 32-bit and 64-bit ARM VMs.
pub trait GuestMem {
    /// Reads one byte at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr` is unmapped.
    fn read_u8(&self, addr: u64) -> Result<u8, ToolError>;

    /// Writes one byte at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr` is unmapped.
    fn write_u8(&mut self, addr: u64, val: u8) -> Result<(), ToolError>;

    /// Allocates `size` zeroed guest bytes aligned to 4 KiB pages (`VirtualAlloc`).
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if guest virtual address space is exhausted.
    fn alloc_pages(&mut self, hint: u64, size: u64) -> Result<u64, ToolError>;

    /// Reads a little-endian 16-bit integer at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr..addr+2` is unmapped.
    fn read_u16_le(&self, addr: u64) -> Result<u16, ToolError> {
        let b0 = self.read_u8(addr)?;
        let b1 = self.read_u8(addr.wrapping_add(1))?;
        Ok(u16::from_le_bytes([b0, b1]))
    }

    /// Writes a little-endian 16-bit integer at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr..addr+2` is unmapped.
    fn write_u16_le(&mut self, addr: u64, val: u16) -> Result<(), ToolError> {
        let [b0, b1] = val.to_le_bytes();
        self.write_u8(addr, b0)?;
        self.write_u8(addr.wrapping_add(1), b1)
    }

    /// Reads a little-endian 32-bit integer at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr..addr+4` is unmapped.
    fn read_u32_le(&self, addr: u64) -> Result<u32, ToolError> {
        let b0 = self.read_u8(addr)?;
        let b1 = self.read_u8(addr.wrapping_add(1))?;
        let b2 = self.read_u8(addr.wrapping_add(2))?;
        let b3 = self.read_u8(addr.wrapping_add(3))?;
        Ok(u32::from_le_bytes([b0, b1, b2, b3]))
    }

    /// Writes a little-endian 32-bit integer at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr..addr+4` is unmapped.
    fn write_u32_le(&mut self, addr: u64, val: u32) -> Result<(), ToolError> {
        let [b0, b1, b2, b3] = val.to_le_bytes();
        self.write_u8(addr, b0)?;
        self.write_u8(addr.wrapping_add(1), b1)?;
        self.write_u8(addr.wrapping_add(2), b2)?;
        self.write_u8(addr.wrapping_add(3), b3)
    }

    /// Reads a little-endian 64-bit integer at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr..addr+8` is unmapped.
    fn read_u64_le(&self, addr: u64) -> Result<u64, ToolError> {
        let lo = u64::from(self.read_u32_le(addr)?);
        let hi = u64::from(self.read_u32_le(addr.wrapping_add(4))?);
        Ok(lo | (hi << 32))
    }

    /// Writes a little-endian 64-bit integer at `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr..addr+8` is unmapped.
    fn write_u64_le(&mut self, addr: u64, val: u64) -> Result<(), ToolError> {
        let lo = u32::try_from(val & 0xFFFF_FFFF).unwrap_or(0);
        let hi = u32::try_from(val >> 32).unwrap_or(0);
        self.write_u32_le(addr, lo)?;
        self.write_u32_le(addr.wrapping_add(4), hi)
    }

    /// Reads a pointer-sized word (`4` bytes on 32-bit, `8` bytes on 64-bit).
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr` is unmapped.
    fn read_ptr(&self, addr: u64, is_64bit: bool) -> Result<u64, ToolError> {
        if is_64bit {
            self.read_u64_le(addr)
        } else {
            self.read_u32_le(addr).map(u64::from)
        }
    }

    /// Writes a pointer-sized word (`4` bytes on 32-bit, `8` bytes on 64-bit).
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if `addr` is unmapped.
    fn write_ptr(&mut self, addr: u64, val: u64, is_64bit: bool) -> Result<(), ToolError> {
        if is_64bit {
            self.write_u64_le(addr, val)
        } else {
            self.write_u32_le(addr, u32::try_from(val & 0xFFFF_FFFF).unwrap_or(0))
        }
    }

    /// Reads a NUL-terminated UTF-16LE string from guest memory into a `String`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if memory access fails.
    fn read_wstr(&self, mut addr: u64) -> Result<String, ToolError> {
        if addr == 0 {
            return Ok(String::new());
        }
        let mut units = Vec::new();
        for _ in 0..32_768 {
            let u = self.read_u16_le(addr)?;
            if u == 0 {
                break;
            }
            units.push(u);
            addr = addr.wrapping_add(2);
        }
        Ok(String::from_utf16_lossy(&units))
    }

    /// Reads a NUL-terminated ASCII/UTF-8 string from guest memory.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if memory access fails.
    fn read_cstr(&self, mut addr: u64) -> Result<String, ToolError> {
        if addr == 0 {
            return Ok(String::new());
        }
        let mut bytes = Vec::new();
        for _ in 0..4096 {
            let b = self.read_u8(addr)?;
            if b == 0 {
                break;
            }
            bytes.push(b);
            addr = addr.wrapping_add(1);
        }
        Ok(String::from_utf8_lossy(&bytes).into_owned())
    }

    /// Writes a UTF-16LE string + NUL terminator into `addr` (up to `max_chars` `WCHAR`s),
    /// returning the number of non-NUL `WCHAR`s written.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if memory access fails.
    fn write_wstr(&mut self, addr: u64, s: &str, max_chars: usize) -> Result<u32, ToolError> {
        let units: Vec<u16> = s.encode_utf16().collect();
        if addr == 0 || max_chars == 0 {
            return Ok(u32::try_from(units.len()).unwrap_or(0));
        }
        let copy_len = units.len().min(max_chars.saturating_sub(1));
        for (i, &u) in units.iter().take(copy_len).enumerate() {
            let off = u64::try_from(i * 2).unwrap_or(0);
            self.write_u16_le(addr.wrapping_add(off), u)?;
        }
        let end_off = u64::try_from(copy_len * 2).unwrap_or(0);
        self.write_u16_le(addr.wrapping_add(end_off), 0)?;
        Ok(u32::try_from(copy_len).unwrap_or(0))
    }

    /// Reads `len` raw bytes from `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if memory access fails.
    fn read_slice(&self, addr: u64, len: usize) -> Result<Vec<u8>, ToolError> {
        let mut out = Vec::with_capacity(len);
        for i in 0..len {
            let off = u64::try_from(i).unwrap_or(0);
            out.push(self.read_u8(addr.wrapping_add(off))?);
        }
        Ok(out)
    }

    /// Writes `data` raw bytes to `addr`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if memory access fails.
    fn write_slice(&mut self, addr: u64, data: &[u8]) -> Result<(), ToolError> {
        for (i, &b) in data.iter().enumerate() {
            let off = u64::try_from(i).unwrap_or(0);
            self.write_u8(addr.wrapping_add(off), b)?;
        }
        Ok(())
    }
}

/// Entry metadata and payload inside [`SharedWinEnv`].
#[derive(Clone, Debug)]
pub struct WinVfsNode {
    /// Original display/case-preserved canonical path.
    pub display_path: String,
    /// Win32 `FILE_ATTRIBUTE_*` flags (`0x10` dir, `0x20` archive, `0x400` reparse point).
    pub attrs: u32,
    /// Reparse tag (`0xA0000003` junction, `0xA000000C` symlink, `0x8000001B` `AppExecLink`).
    pub reparse_tag: u32,
    /// Reparse target path (for symlinks, junctions, and `AppExecLink`).
    pub reparse_target: String,
    /// Raw file content bytes (shared via `Rc<RefCell<Vec<u8>>>` so hard links share storage!).
    pub data: Rc<RefCell<Vec<u8>>>,
}

/// Shared filesystem and Registry backing store shared across parent and child processes.
#[derive(Clone, Debug, Default)]
pub struct SharedWinEnv {
    /// Filesystem entries keyed by normalized lookup key (respecting per-directory case sensitivity).
    pub nodes: BTreeMap<String, WinVfsNode>,
    /// Normalized directory keys that enforce exact case sensitivity (`Win10 1803+` / `Win11`).
    pub case_sensitive_dirs: BTreeSet<String>,
    /// Registry keys -> `(value_name_upper -> (reg_type, raw_bytes))`.
    pub registry: BTreeMap<String, BTreeMap<String, (u32, Vec<u8>)>>,
    /// Shared anonymous pipe buffers keyed by pipe ID.
    pub pipes: BTreeMap<u32, Vec<u8>>,
    /// Next unique pipe ID.
    pub next_pipe_id: u32,
}

impl SharedWinEnv {
    /// Creates an initialized Windows NT or Windows CE environment with standard directories.
    #[must_use]
    pub fn new(is_wince: bool) -> Self {
        let mut env = Self {
            nodes: BTreeMap::new(),
            case_sensitive_dirs: BTreeSet::new(),
            registry: BTreeMap::new(),
            pipes: BTreeMap::new(),
            next_pipe_id: 1,
        };
        if is_wince {
            env.mkdir(bs_str());
            env.mkdir(&format!("{BS}Windows"));
            env.mkdir(&format!("{BS}Temp"));
            env.mkdir(&format!("{BS}Storage Card"));
        } else {
            env.mkdir(&format!("C:{BS}"));
            env.mkdir(&format!("C:{BS}Windows"));
            env.mkdir(&format!("C:{BS}Windows{BS}System32"));
            env.mkdir(&format!("C:{BS}Temp"));
        }
        env
    }

    /// Inserts or updates a `REG_SZ` (`1`) UTF-16LE Registry value under `key_path`.
    pub fn set_reg_sz(&mut self, key_path: &str, val_name: &str, val: &str) {
        let mut raw = Vec::with_capacity((val.len() + 1) * 2);
        for u in val.encode_utf16() {
            raw.extend_from_slice(&u.to_le_bytes());
        }
        raw.extend_from_slice(&0u16.to_le_bytes());
        let k = key_path.replace('/', bs_str()).to_ascii_uppercase();
        self.registry
            .entry(k)
            .or_default()
            .insert(val_name.to_ascii_uppercase(), (1, raw));
    }

    /// Normalizes a Win32 / `WinCE` path, returning `(display_path, lookup_key)`.
    #[must_use]
    pub fn normalize_key(&self, raw: &str, is_wince: bool, cwd: &str) -> (String, String) {
        let long_unc = format!("{BS}{BS}?{BS}UNC{BS}");
        let long_pfx = format!("{BS}{BS}?{BS}");
        let mut s = raw.trim().replace('/', bs_str());
        if let Some(rest) = s.strip_prefix(&long_unc) {
            s = format!("{}{rest}", unc_str());
        } else if s.starts_with(&long_pfx) {
            let _ = s.drain(..long_pfx.len());
        }
        let abs = if is_wince {
            if s.starts_with(BS) {
                s
            } else if cwd.ends_with(BS) {
                format!("{cwd}{s}")
            } else {
                format!("{cwd}{BS}{s}")
            }
        } else if s.len() >= 2 && s.as_bytes()[1] == b':' {
            if s.len() == 2 {
                format!("{s}{BS}")
            } else if s.as_bytes().get(2) == Some(&92) {
                s
            } else {
                format!("{}{BS}{}", &s[..2], &s[2..])
            }
        } else if s.starts_with(unc_str()) {
            s
        } else if s.starts_with(BS) {
            let drv = if cwd.len() >= 2 && cwd.as_bytes()[1] == b':' {
                &cwd[..2]
            } else {
                "C:"
            };
            format!("{drv}{s}")
        } else if cwd.ends_with(BS) {
            format!("{cwd}{s}")
        } else {
            format!("{cwd}{BS}{s}")
        };

        let is_unc = abs.starts_with(unc_str());
        let (prefix, rel) = if is_unc {
            (unc_str().to_owned(), abs.trim_start_matches(BS))
        } else if !is_wince && abs.len() >= 2 && abs.as_bytes()[1] == b':' {
            (
                format!("{}{BS}", abs[..2].to_ascii_uppercase()),
                abs[2..].trim_start_matches(BS),
            )
        } else {
            (bs_str().to_owned(), abs.trim_start_matches(BS))
        };
        let mut parts: Vec<&str> = Vec::new();
        for seg in rel.split(BS) {
            if seg.is_empty() || seg == "." {
                continue;
            }
            if seg == ".." {
                let _ = parts.pop();
            } else {
                parts.push(seg);
            }
        }
        let disp = if parts.is_empty() {
            prefix
        } else if prefix.ends_with(BS) {
            format!("{prefix}{}", parts.join(bs_str()))
        } else {
            format!("{prefix}{BS}{}", parts.join(bs_str()))
        };

        let key = if let Some((parent, leaf)) = disp.rsplit_once(BS) {
            let p_norm = if parent.is_empty() {
                bs_str().to_owned()
            } else if parent.len() == 2 && parent.as_bytes()[1] == b':' {
                format!("{}{BS}", parent.to_ascii_uppercase())
            } else {
                parent.to_ascii_uppercase()
            };
            if self.case_sensitive_dirs.contains(&p_norm) {
                if p_norm.ends_with(BS) {
                    format!("{p_norm}{leaf}")
                } else {
                    format!("{p_norm}{BS}{leaf}")
                }
            } else {
                disp.to_ascii_uppercase()
            }
        } else {
            disp.to_ascii_uppercase()
        };
        (disp, key)
    }

    /// Resolves symlinks/junctions when opening file contents.
    #[must_use]
    pub fn resolve_target_key(&self, key: &str, is_wince: bool, cwd: &str) -> String {
        let nt_obj = format!("{BS}??{BS}");
        let mut cur = key.to_owned();
        for _ in 0..8 {
            if let Some(node) = self.nodes.get(&cur)
                && (node.attrs & 0x0400) != 0
                && !node.reparse_target.is_empty()
            {
                let mut tgt = node.reparse_target.clone();
                if let Some(rest) = tgt.strip_prefix(&nt_obj) {
                    tgt = rest.to_owned();
                }
                let (_, next_k) = self.normalize_key(&tgt, is_wince, cwd);
                if next_k == cur {
                    break;
                }
                cur = next_k;
                continue;
            }
            break;
        }
        cur
    }

    /// Inserts or updates a directory in the virtual filesystem.
    pub fn mkdir(&mut self, path: &str) {
        let is_ce = path.starts_with(BS) && !path.starts_with(unc_str());
        let c_root = format!("C:{BS}");
        let root = if is_ce { bs_str() } else { &c_root };
        let (disp, key) = self.normalize_key(path, is_ce, root);
        self.nodes.entry(key).or_insert_with(|| WinVfsNode {
            display_path: disp,
            attrs: 0x10,
            reparse_tag: 0,
            reparse_target: String::new(),
            data: Rc::new(RefCell::new(Vec::new())),
        });
    }

    /// Writes a file into the virtual filesystem, creating parent directories automatically.
    pub fn write_file(&mut self, path: &str, bytes: &[u8]) {
        let is_ce = path.starts_with(BS) && !path.starts_with(unc_str());
        let c_root = format!("C:{BS}");
        let root = if is_ce { bs_str() } else { &c_root };
        let (disp, key) = self.normalize_key(path, is_ce, root);
        if let Some((parent, _)) = disp.rsplit_once(BS)
            && !parent.is_empty()
        {
            self.mkdir(parent);
        }
        self.nodes.insert(
            key,
            WinVfsNode {
                display_path: disp,
                attrs: 0x20,
                reparse_tag: 0,
                reparse_target: String::new(),
                data: Rc::new(RefCell::new(bytes.to_vec())),
            },
        );
    }

    /// Reads raw file bytes from `path` (following symlinks/junctions if any).
    #[must_use]
    pub fn read_file(&self, path: &str) -> Option<Vec<u8>> {
        let is_ce = path.starts_with(BS) && !path.starts_with(unc_str());
        let c_root = format!("C:{BS}");
        let root = if is_ce { bs_str() } else { &c_root };
        let (_, key) = self.normalize_key(path, is_ce, root);
        let resolved = self.resolve_target_key(&key, is_ce, root);
        let node = self.nodes.get(&resolved)?;
        if (node.attrs & 0x10) != 0 {
            return None;
        }
        Some(node.data.borrow().clone())
    }

    /// Marks `dir_path` as per-directory case-sensitive (`Windows 10 1803+` / `Windows 11`).
    pub fn set_dir_case_sensitive(&mut self, dir_path: &str) {
        let c_root = format!("C:{BS}");
        let (_, key) = self.normalize_key(dir_path, false, &c_root);
        self.mkdir(dir_path);
        self.case_sensitive_dirs.insert(key);
    }
}

/// Active open handle kind inside a simulated `WinCE` or Windows NT process.
#[derive(Clone, Debug)]
pub enum WinHandle {
    /// Standard input stream (`STD_INPUT_HANDLE`).
    StdIn,
    /// Standard output stream (`STD_OUTPUT_HANDLE`).
    StdOut,
    /// Standard error stream (`STD_ERROR_HANDLE`).
    StdErr,
    /// NUL character sink.
    Nul,
    /// Open file or directory/reparse handle.
    File {
        /// Canonical key in [`SharedWinEnv::nodes`].
        key: String,
        /// Byte read/write cursor.
        pos: u64,
        /// Writable flag.
        writable: bool,
    },
    /// Anonymous pipe read end.
    PipeRead {
        /// Pipe buffer ID in [`SharedWinEnv::pipes`].
        pipe_id: u32,
        /// Current read offset.
        pos: usize,
    },
    /// Anonymous pipe write end.
    PipeWrite {
        /// Pipe buffer ID in [`SharedWinEnv::pipes`].
        pipe_id: u32,
    },
    /// Directory enumeration handle (`FindFirstFileW`).
    Search {
        /// Matched snapshot entries `(leaf_name, short_83, attrs, size, reparse_tag)`.
        entries: Vec<(String, String, u32, u64, u32)>,
        /// Next index for `FindNextFileW`.
        idx: usize,
    },
    /// Open Registry key handle (`HKEY`).
    RegKey {
        /// Normalized Registry path (`HKCU` or `HKLM`).
        path: String,
    },
    /// Completed child process handle (`hProcess`).
    Process {
        /// Child process exit status (`GetExitCodeProcess`).
        exit_code: u32,
    },
    /// Access token handle (`OpenProcessToken`).
    Token,
    /// Worker thread handle (`CreateThread`).
    Thread,
}

/// Callback signature invoked when `CreateProcessW` launches a nested `wincmd` subshell.
pub type ChildRunnerFn = Box<
    dyn FnMut(&str, Rc<RefCell<SharedWinEnv>>, u64, u64, u64) -> Result<(u32, Vec<u8>, Vec<u8>), ToolError>,
>;

/// Per-process simulated OS state (`COREDLL.DLL` or `KERNEL32`/`NTDLL`/`ADVAPI32`).
pub struct WinProcState {
    /// Whether this process runs under Windows CE (`COREDLL.DLL` rootless paths, no `argv[0]`).
    pub is_wince: bool,
    /// Whether this process uses 64-bit Win64 structures (`PE32+` `ARM64`).
    pub is_64bit: bool,
    /// Simulated OS major/minor/build version (`4.20.1088` CE, `6.3.9600` `WinRT`, `10.0.22631` Win11).
    pub os_version: (u32, u32, u32),
    /// Shared filesystem, Registry, and pipe backing store.
    pub shared: Rc<RefCell<SharedWinEnv>>,
    /// Open Win32/WinCE handles keyed by integer handle value.
    pub handles: BTreeMap<u64, WinHandle>,
    /// Next allocatable handle value.
    pub next_handle: u64,
    /// Active standard input handle (`GetStdHandle(-10)`).
    pub std_in_handle: u64,
    /// Active standard output handle (`GetStdHandle(-11)`).
    pub std_out_handle: u64,
    /// Active standard error handle (`GetStdHandle(-12)`).
    pub std_err_handle: u64,
    /// Pre-queued standard input bytes.
    pub stdin_buf: Vec<u8>,
    /// Current read cursor into `stdin_buf`.
    pub stdin_pos: usize,
    /// Captured standard output UTF-8 bytes.
    pub stdout: Vec<u8>,
    /// Captured standard error UTF-8 bytes.
    pub stderr: Vec<u8>,
    /// Active Win32 `GetLastError()` value.
    pub last_error: u32,
    /// Process startup current working directory.
    pub cwd: String,
    /// Process executable module path (`GetModuleFileNameW`).
    pub self_exe: String,
    /// Guest address of UTF-16LE command line (`GetCommandLineW`).
    pub cmdline_w_addr: u64,
    /// Guest address of ANSI/UTF-8 command line (`GetCommandLineA`).
    pub cmdline_a_addr: u64,
    /// Guest address of UTF-16LE environment block (`GetEnvironmentStringsW`).
    pub env_w_addr: u64,
    /// Process-local environment variable overrides from `SetEnvironmentVariableW`.
    pub env_overrides: BTreeMap<String, Option<String>>,
    /// Active console output codepage (`GetConsoleOutputCP`).
    pub console_cp: u32,
    /// Active console mode flags (`GetConsoleMode`).
    pub console_mode: u32,
    /// Active console text attribute (`wAttributes`).
    pub console_attr: u16,
    /// Active console window title (`SetConsoleTitleW`).
    pub console_title: String,
    /// Process termination flag (`Some(exit_code)` after `ExitProcess` / `TerminateProcess`).
    pub exited: Option<u32>,
    /// Optional callback invoked when `CreateProcessW` spawns a nested `wincmd` subshell.
    pub child_runner: Option<ChildRunnerFn>,
}

impl WinProcState {
    /// Constructs a new per-process `WinCE` or Windows NT OS context.
    #[must_use]
    pub fn new(
        is_wince: bool,
        is_64bit: bool,
        os_version: (u32, u32, u32),
        shared: Rc<RefCell<SharedWinEnv>>,
    ) -> Self {
        let mut handles = BTreeMap::new();
        handles.insert(0, WinHandle::StdIn);
        handles.insert(1, WinHandle::StdOut);
        handles.insert(2, WinHandle::StdErr);
        handles.insert(0x10, WinHandle::StdIn);
        handles.insert(0x11, WinHandle::StdOut);
        handles.insert(0x12, WinHandle::StdErr);
        let (cwd, self_exe) = if is_wince {
            (bs_str().to_owned(), format!("{BS}Windows{BS}WINCMD_ARMV4.EXE"))
        } else if is_64bit {
            (
                format!("C:{BS}"),
                format!("C:{BS}Windows{BS}System32{BS}WINCMD_ARM64.EXE"),
            )
        } else {
            (
                format!("C:{BS}"),
                format!("C:{BS}Windows{BS}System32{BS}WINCMD_ARM32.EXE"),
            )
        };
        Self {
            is_wince,
            is_64bit,
            os_version,
            shared,
            handles,
            next_handle: 0x100,
            std_in_handle: 0x10,
            std_out_handle: 0x11,
            std_err_handle: 0x12,
            stdin_buf: Vec::new(),
            stdin_pos: 0,
            stdout: Vec::new(),
            stderr: Vec::new(),
            last_error: 0,
            cwd,
            self_exe,
            cmdline_w_addr: 0,
            cmdline_a_addr: 0,
            env_w_addr: 0,
            env_overrides: BTreeMap::new(),
            console_cp: 65001,
            console_mode: 0x0003,
            console_attr: 0x0007,
            console_title: String::new(),
            exited: None,
            child_runner: None,
        }
    }

    /// Allocates a new open handle in this process's handle table.
    pub fn alloc_handle(&mut self, h: WinHandle) -> u64 {
        let id = self.next_handle;
        self.next_handle += 4;
        self.handles.insert(id, h);
        id
    }

    const fn invalid_handle_val(&self) -> u64 {
        if self.is_64bit {
            u64::MAX
        } else {
            0xFFFF_FFFF
        }
    }

    /// Executes a named `WinCE` / `WinNT` API import (`reg_args` = `r0..r3` or `x0..x7`,
    /// plus stack arguments read starting at `stack_args_base`).
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if guest memory access faults or an unknown import is called.
    #[allow(
        clippy::too_many_lines,
        clippy::cognitive_complexity,
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap
    )]
    pub fn dispatch_api<M: GuestMem>(
        &mut self,
        mem: &mut M,
        api: &str,
        reg_args: &[u64],
        stack_args_base: u64,
        thunk_resolver: &mut dyn FnMut(&str) -> u64,
    ) -> Result<u64, ToolError> {
        let is_64 = self.is_64bit;
        let arg = |idx: usize, m: &M| -> Result<u64, ToolError> {
            if idx < reg_args.len() {
                Ok(reg_args[idx])
            } else {
                let slot = idx - reg_args.len();
                let stride = if is_64 { 8 } else { 4 };
                let off = u64::try_from(slot * stride).unwrap_or(0);
                m.read_ptr(stack_args_base.wrapping_add(off), is_64)
            }
        };

        match api {
            "VirtualAlloc" => {
                let hint = arg(0, mem)?;
                let sz = arg(1, mem)?;
                mem.alloc_pages(hint, sz.max(4096))
            }
            "VirtualFree"
            | "FreeLibrary"
            | "LocalFree"
            | "SwitchToThread"
            | "FreeEnvironmentStringsW"
            | "SetUnhandledExceptionFilter"
            | "SetConsoleCtrlHandler"
            | "SetConsoleCursorPosition"
            | "SetConsoleScreenBufferSize"
            | "SetFileTime"
            | "SetEndOfFile"
            | "SetLocalTime"
            | "SetHandleInformation"
            | "SetErrorMode"
            | "FlushFileBuffers"
            | "Wow64DisableWow64FsRedirection"
            | "Wow64RevertWow64FsRedirection"
            | "Sleep" => Ok(1),
            "ExitProcess" => {
                let code = u32::try_from(arg(0, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                self.exited = Some(code);
                Ok(0)
            }
            "TerminateProcess" => {
                let code = u32::try_from(arg(1, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                self.exited = Some(code);
                Ok(1)
            }
            "GetLastError" => Ok(u64::from(self.last_error)),
            "SetLastError" => {
                self.last_error = u32::try_from(arg(0, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                Ok(0)
            }
            "GetTickCount" => Ok(0x1234_5678),
            "GetVersion" => {
                let v = (self.os_version.0 & 0xFF)
                    | ((self.os_version.1 & 0xFF) << 8)
                    | ((self.os_version.2 & 0x7FFF) << 16);
                Ok(u64::from(v))
            }
            "GetCommandLineW" => Ok(self.cmdline_w_addr),
            "GetCommandLineA" => Ok(self.cmdline_a_addr),
            "GetEnvironmentStringsW" => Ok(self.env_w_addr),
            "GetACP" | "GetOEMCP" | "GetConsoleCP" | "GetConsoleOutputCP" => {
                Ok(u64::from(self.console_cp))
            }
            "GetCPInfo" => {
                let cp_ptr = arg(1, mem)?;
                if cp_ptr != 0 {
                    mem.write_slice(cp_ptr, &[0u8; 20])?;
                    mem.write_u32_le(cp_ptr, 1)?;
                    mem.write_u8(cp_ptr.wrapping_add(4), b'?')?;
                }
                Ok(1)
            }
            "GetFileType" => {
                let h = arg(0, mem)?;
                let ft = match self.handles.get(&h) {
                    Some(WinHandle::StdIn | WinHandle::StdOut | WinHandle::StdErr | WinHandle::Nul) => 2,
                    Some(WinHandle::PipeRead { .. } | WinHandle::PipeWrite { .. }) => 3,
                    Some(WinHandle::File { .. }) => 1,
                    _ => 0,
                };
                Ok(ft)
            }
            "SetStdHandle" => {
                let kind = u32::try_from(arg(0, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let h = arg(1, mem)?;
                match kind {
                    0xFFFF_FFF6 => self.std_in_handle = h,
                    0xFFFF_FFF5 => self.std_out_handle = h,
                    0xFFFF_FFF4 => self.std_err_handle = h,
                    _ => return Ok(0),
                }
                Ok(1)
            }
            "DuplicateHandle" => {
                let src_h = arg(1, mem)?;
                let tgt_ptr = arg(3, mem)?;
                let Some(entry) = self.handles.get(&src_h).cloned() else {
                    self.last_error = 6;
                    return Ok(0);
                };
                let nh = self.alloc_handle(entry);
                if tgt_ptr != 0 {
                    mem.write_ptr(tgt_ptr, nh, is_64)?;
                }
                Ok(1)
            }
            "SetEnvironmentVariableW" => {
                let name = mem.read_wstr(arg(0, mem)?)?.to_ascii_uppercase();
                let val_ptr = arg(1, mem)?;
                let val_opt = if val_ptr == 0 {
                    None
                } else {
                    Some(mem.read_wstr(val_ptr)?)
                };
                self.env_overrides.insert(name, val_opt);
                Ok(1)
            }
            "GetShortPathNameW" | "GetLongPathNameW" => {
                let raw = mem.read_wstr(arg(0, mem)?)?;
                let dst = arg(1, mem)?;
                let cap = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let (disp, _) = self.shared.borrow().normalize_key(&raw, self.is_wince, &self.cwd);
                let cch = disp.encode_utf16().count();
                if dst != 0 && cap > cch {
                    let n = mem.write_wstr(dst, &disp, cap)?;
                    Ok(u64::from(n))
                } else {
                    Ok(u64::try_from(cch + 1).unwrap_or(0))
                }
            }
            "GetDiskFreeSpaceW" => {
                let spc = arg(1, mem)?;
                let bps = arg(2, mem)?;
                let free_c = arg(3, mem)?;
                let tot_c = arg(4, mem)?;
                if spc != 0 {
                    mem.write_u32_le(spc, 8)?;
                }
                if bps != 0 {
                    mem.write_u32_le(bps, 512)?;
                }
                if free_c != 0 {
                    mem.write_u32_le(free_c, 2_621_440)?;
                }
                if tot_c != 0 {
                    mem.write_u32_le(tot_c, 16_777_216)?;
                }
                Ok(1)
            }
            "GetDriveTypeW" | "GetDriveTypeA" => Ok(3),
            "GetVolumeInformationW" => {
                let vol_ptr = arg(1, mem)?;
                let vol_cap = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let ser_ptr = arg(3, mem)?;
                let max_comp = arg(4, mem)?;
                let flags_ptr = arg(5, mem)?;
                let fs_ptr = arg(6, mem)?;
                let fs_cap = usize::try_from(arg(7, mem)?).unwrap_or(0);
                if vol_ptr != 0 && vol_cap > 6 {
                    let _ = mem.write_wstr(vol_ptr, "SYSTEM", vol_cap)?;
                }
                if ser_ptr != 0 {
                    mem.write_u32_le(ser_ptr, 0x1234_5678)?;
                }
                if max_comp != 0 {
                    mem.write_u32_le(max_comp, 255)?;
                }
                if flags_ptr != 0 {
                    mem.write_u32_le(flags_ptr, 0x0007_00FF)?;
                }
                if fs_ptr != 0 && fs_cap > 4 {
                    let _ = mem.write_wstr(fs_ptr, "NTFS", fs_cap)?;
                }
                Ok(1)
            }
            "FileTimeToLocalFileTime" | "LocalFileTimeToFileTime" => {
                let src = arg(0, mem)?;
                let dst = arg(1, mem)?;
                if src != 0 && dst != 0 {
                    let ft = mem.read_u64_le(src)?;
                    mem.write_u64_le(dst, ft)?;
                }
                Ok(1)
            }
            "FileTimeToSystemTime" => {
                let st = arg(1, mem)?;
                if st != 0 {
                    mem.write_u16_le(st, 2026)?;
                    mem.write_u16_le(st.wrapping_add(2), 11)?;
                    mem.write_u16_le(st.wrapping_add(4), 1)?;
                    mem.write_u16_le(st.wrapping_add(6), 23)?;
                    mem.write_u16_le(st.wrapping_add(8), 14)?;
                    mem.write_u16_le(st.wrapping_add(10), 30)?;
                    mem.write_u16_le(st.wrapping_add(12), 45)?;
                    mem.write_u16_le(st.wrapping_add(14), 0)?;
                }
                Ok(1)
            }
            "SystemTimeToFileTime" => {
                let ft = arg(1, mem)?;
                if ft != 0 {
                    mem.write_u32_le(ft, 0x8000_0000)?;
                    mem.write_u32_le(ft.wrapping_add(4), 0x01DC_5BE0)?;
                }
                Ok(1)
            }
            "FillConsoleOutputAttribute" => {
                let cnt = u32::try_from(arg(2, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let written_ptr = arg(4, mem)?;
                if written_ptr != 0 {
                    mem.write_u32_le(written_ptr, cnt)?;
                }
                Ok(1)
            }
            "ReadConsoleW" => {
                let h = arg(0, mem)?;
                let buf = arg(1, mem)?;
                let max_ch = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let read_ptr = arg(3, mem)?;
                let chunk = self.read_handle_bytes(h, max_ch);
                let text = String::from_utf8_lossy(&chunk);
                let mut n = 0u32;
                for u in text.encode_utf16().take(max_ch) {
                    mem.write_u16_le(buf.wrapping_add(u64::from(n) * 2), u)?;
                    n += 1;
                }
                if read_ptr != 0 {
                    mem.write_u32_le(read_ptr, n)?;
                }
                Ok(1)
            }
            "GetModuleHandleA" | "LoadLibraryA" => {
                let name = mem.read_cstr(arg(0, mem)?).unwrap_or_default().to_ascii_uppercase();
                if name.is_empty() || name.contains("KERNEL32") {
                    Ok(0x7700_0000)
                } else if name.contains("NTDLL") {
                    Ok(0x7710_0000)
                } else if name.contains("ADVAPI32") {
                    Ok(0x7720_0000)
                } else if name.contains("PSAPI") {
                    Ok(0x7730_0000)
                } else {
                    Ok(0)
                }
            }
            "GetTempPathW" => {
                let cap = usize::try_from(arg(0, mem)?).unwrap_or(0);
                let dst = arg(1, mem)?;
                let tmp = format!("C:{BS}Temp{BS}");
                if dst != 0 && cap > tmp.len() {
                    let n = mem.write_wstr(dst, &tmp, cap)?;
                    Ok(u64::from(n))
                } else {
                    Ok(u64::try_from(tmp.len() + 1).unwrap_or(0))
                }
            }
            "GetModuleFileNameW" => {
                let dst = arg(1, mem)?;
                let max_ch = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let n = mem.write_wstr(dst, &self.self_exe, max_ch)?;
                Ok(u64::from(n))
            }
            "GetCurrentDirectoryW" => {
                let max_ch = usize::try_from(arg(0, mem)?).unwrap_or(0);
                let dst = arg(1, mem)?;
                let n = mem.write_wstr(dst, &self.cwd, max_ch)?;
                Ok(u64::from(n))
            }
            "SetCurrentDirectoryW" => {
                let p = mem.read_wstr(arg(0, mem)?)?;
                let (disp, key) = self.shared.borrow().normalize_key(&p, self.is_wince, &self.cwd);
                let ok = self
                    .shared
                    .borrow()
                    .nodes
                    .get(&key)
                    .is_some_and(|n| (n.attrs & 0x10) != 0);
                if ok {
                    self.cwd = disp;
                    Ok(1)
                } else {
                    self.last_error = 3;
                    Ok(0)
                }
            }
            "GetVersionExW" | "RtlGetVersion" => {
                let ptr = arg(0, mem)?;
                if ptr != 0 {
                    mem.write_u32_le(ptr.wrapping_add(4), self.os_version.0)?;
                    mem.write_u32_le(ptr.wrapping_add(8), self.os_version.1)?;
                    mem.write_u32_le(ptr.wrapping_add(12), self.os_version.2)?;
                    mem.write_u32_le(ptr.wrapping_add(16), if self.is_wince { 3 } else { 2 })?;
                }
                Ok(u64::from(api != "RtlGetVersion"))
            }
            "GetStdHandle" => {
                let kind = u32::try_from(arg(0, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                match kind {
                    0xFFFF_FFF6 => Ok(self.std_in_handle),
                    0xFFFF_FFF5 => Ok(self.std_out_handle),
                    0xFFFF_FFF4 => Ok(self.std_err_handle),
                    _ => Ok(self.invalid_handle_val()),
                }
            }
            "GetConsoleMode" => {
                let h = arg(0, mem)?;
                let out_ptr = arg(1, mem)?;
                let is_con = matches!(
                    self.handles.get(&h),
                    Some(WinHandle::StdIn | WinHandle::StdOut | WinHandle::StdErr)
                );
                if is_con {
                    if out_ptr != 0 {
                        mem.write_u32_le(out_ptr, self.console_mode)?;
                    }
                    Ok(1)
                } else {
                    self.last_error = 6;
                    Ok(0)
                }
            }
            "SetConsoleMode" => {
                let h = arg(0, mem)?;
                let mode = u32::try_from(arg(1, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let is_con = matches!(
                    self.handles.get(&h),
                    Some(WinHandle::StdIn | WinHandle::StdOut | WinHandle::StdErr)
                );
                if is_con {
                    self.console_mode = mode;
                    Ok(1)
                } else {
                    Ok(0)
                }
            }
            "SetConsoleCP" | "SetConsoleOutputCP" => {
                self.console_cp = u32::try_from(arg(0, mem)? & 0xFFFF_FFFF).unwrap_or(65001);
                Ok(1)
            }
            "GetConsoleScreenBufferInfo" => {
                let info_ptr = arg(1, mem)?;
                if info_ptr != 0 {
                    mem.write_u16_le(info_ptr, 80)?;
                    mem.write_u16_le(info_ptr.wrapping_add(2), 25)?;
                    mem.write_u16_le(info_ptr.wrapping_add(4), 0)?;
                    mem.write_u16_le(info_ptr.wrapping_add(6), 0)?;
                    mem.write_u16_le(info_ptr.wrapping_add(8), self.console_attr)?;
                }
                Ok(1)
            }
            "SetConsoleTextAttribute" => {
                self.console_attr = u16::try_from(arg(1, mem)? & 0xFFFF).unwrap_or(7);
                Ok(1)
            }
            "FillConsoleOutputCharacterW" => {
                let written_ptr = arg(4, mem)?;
                let cnt = u32::try_from(arg(2, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                if written_ptr != 0 {
                    mem.write_u32_le(written_ptr, cnt)?;
                }
                Ok(1)
            }
            "SetConsoleTitleW" => {
                self.console_title = mem.read_wstr(arg(0, mem)?)?;
                Ok(1)
            }
            "WriteConsoleW" => {
                let h = arg(0, mem)?;
                let buf = arg(1, mem)?;
                let n_chars = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let written_ptr = arg(3, mem)?;
                let mut units = Vec::with_capacity(n_chars);
                for i in 0..n_chars {
                    let off = u64::try_from(i * 2).unwrap_or(0);
                    units.push(mem.read_u16_le(buf.wrapping_add(off))?);
                }
                let text = String::from_utf16_lossy(&units);
                self.write_handle_bytes(h, text.as_bytes());
                if written_ptr != 0 {
                    mem.write_u32_le(written_ptr, u32::try_from(n_chars).unwrap_or(0))?;
                }
                Ok(1)
            }
            "WriteFile" => {
                let h = arg(0, mem)?;
                let buf = arg(1, mem)?;
                let len = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let written_ptr = arg(3, mem)?;
                let data = mem.read_slice(buf, len)?;
                self.write_handle_bytes(h, &data);
                if written_ptr != 0 {
                    mem.write_u32_le(written_ptr, u32::try_from(len).unwrap_or(0))?;
                }
                Ok(1)
            }
            "ReadFile" => {
                let h = arg(0, mem)?;
                let buf = arg(1, mem)?;
                let cap = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let read_ptr = arg(3, mem)?;
                let chunk = self.read_handle_bytes(h, cap);
                mem.write_slice(buf, &chunk)?;
                if read_ptr != 0 {
                    mem.write_u32_le(read_ptr, u32::try_from(chunk.len()).unwrap_or(0))?;
                }
                Ok(1)
            }
            "CloseHandle" => {
                let h = arg(0, mem)?;
                if h >= 0x100 {
                    let _ = self.handles.remove(&h);
                }
                Ok(1)
            }
            "CreateFileW" => {
                let raw_path = mem.read_wstr(arg(0, mem)?)?;
                let access = u32::try_from(arg(1, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let disp_mode = u32::try_from(arg(4, mem)? & 0xFFFF_FFFF).unwrap_or(3);
                let flags = u32::try_from(arg(5, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                Ok(self.open_or_create_file(&raw_path, access, disp_mode, flags))
            }
            "SetFilePointer" => {
                let h = arg(0, mem)?;
                let dist = i64::from(arg(1, mem)? as u32 as i32);
                let hi_ptr = arg(2, mem)?;
                let method = u32::try_from(arg(3, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let full_dist = if hi_ptr != 0 {
                    let hi = i64::from(mem.read_u32_le(hi_ptr)? as i32);
                    (hi << 32) | (dist & 0xFFFF_FFFF)
                } else {
                    dist
                };
                let new_pos = self.seek_handle(h, full_dist, method);
                if hi_ptr != 0 {
                    mem.write_u32_le(hi_ptr, u32::try_from(new_pos >> 32).unwrap_or(0))?;
                }
                Ok(new_pos & 0xFFFF_FFFF)
            }
            "SetFilePointerEx" => {
                let h = arg(0, mem)?;
                let (dist, new_ptr, method) = if is_64 {
                    (
                        arg(1, mem)? as i64,
                        arg(2, mem)?,
                        u32::try_from(arg(3, mem)? & 0xFFFF_FFFF).unwrap_or(0),
                    )
                } else {
                    let lo = arg(2, mem)? & 0xFFFF_FFFF;
                    let hi = arg(3, mem)? & 0xFFFF_FFFF;
                    let d = ((hi << 32) | lo) as i64;
                    (
                        d,
                        arg(4, mem)?,
                        u32::try_from(arg(5, mem)? & 0xFFFF_FFFF).unwrap_or(0),
                    )
                };
                let new_pos = self.seek_handle(h, dist, method);
                if new_ptr != 0 {
                    mem.write_u64_le(new_ptr, new_pos)?;
                }
                Ok(1)
            }
            "GetFileAttributesW" => {
                let p = mem.read_wstr(arg(0, mem)?)?;
                let (_, key) = self.shared.borrow().normalize_key(&p, self.is_wince, &self.cwd);
                if let Some(node) = self.shared.borrow().nodes.get(&key) {
                    Ok(u64::from(node.attrs))
                } else {
                    self.last_error = 2;
                    Ok(0xFFFF_FFFF)
                }
            }
            "SetFileAttributesW" => {
                let p = mem.read_wstr(arg(0, mem)?)?;
                let attrs = u32::try_from(arg(1, mem)? & 0xFFFF_FFFF).unwrap_or(0x20);
                let (_, key) = self.shared.borrow().normalize_key(&p, self.is_wince, &self.cwd);
                if let Some(node) = self.shared.borrow_mut().nodes.get_mut(&key) {
                    node.attrs = (node.attrs & 0x0410) | (attrs & !0x0410);
                    Ok(1)
                } else {
                    self.last_error = 2;
                    Ok(0)
                }
            }
            "DeleteFileW" => {
                let p = mem.read_wstr(arg(0, mem)?)?;
                let (_, key) = self.shared.borrow().normalize_key(&p, self.is_wince, &self.cwd);
                let can_del = self
                    .shared
                    .borrow()
                    .nodes
                    .get(&key)
                    .is_some_and(|n| (n.attrs & 0x10) == 0 && (n.attrs & 0x01) == 0);
                if can_del {
                    let _ = self.shared.borrow_mut().nodes.remove(&key);
                    Ok(1)
                } else {
                    self.last_error = 2;
                    Ok(0)
                }
            }
            "CreateDirectoryW" => {
                let p = mem.read_wstr(arg(0, mem)?)?;
                let (disp, key) = self.shared.borrow().normalize_key(&p, self.is_wince, &self.cwd);
                let mut env = self.shared.borrow_mut();
                if env.nodes.contains_key(&key) {
                    self.last_error = 183;
                    return Ok(0);
                }
                env.nodes.insert(
                    key,
                    WinVfsNode {
                        display_path: disp,
                        attrs: 0x10,
                        reparse_tag: 0,
                        reparse_target: String::new(),
                        data: Rc::new(RefCell::new(Vec::new())),
                    },
                );
                Ok(1)
            }
            "RemoveDirectoryW" => {
                let p = mem.read_wstr(arg(0, mem)?)?;
                let (disp, key) = self.shared.borrow().normalize_key(&p, self.is_wince, &self.cwd);
                let mut env = self.shared.borrow_mut();
                let Some(node) = env.nodes.get(&key).cloned() else {
                    self.last_error = 2;
                    return Ok(0);
                };
                if (node.attrs & 0x10) == 0 {
                    self.last_error = 267;
                    return Ok(0);
                }
                if (node.attrs & 0x0400) != 0 {
                    let _ = env.nodes.remove(&key);
                    return Ok(1);
                }
                let prefix = format!("{}{BS}", disp.to_ascii_uppercase());
                let non_empty = env
                    .nodes
                    .keys()
                    .any(|k| k != &key && k.to_ascii_uppercase().starts_with(&prefix));
                if non_empty {
                    self.last_error = 145;
                    Ok(0)
                } else {
                    let _ = env.nodes.remove(&key);
                    Ok(1)
                }
            }
            "CopyFileW" | "CopyFileExW" => {
                let src = mem.read_wstr(arg(0, mem)?)?;
                let dst = mem.read_wstr(arg(1, mem)?)?;
                let data_opt = {
                    let env = self.shared.borrow();
                    let (_, sk) = env.normalize_key(&src, self.is_wince, &self.cwd);
                    let rk = env.resolve_target_key(&sk, self.is_wince, &self.cwd);
                    env.nodes
                        .get(&rk)
                        .filter(|n| (n.attrs & 0x10) == 0)
                        .map(|n| n.data.borrow().clone())
                };
                if let Some(bytes) = data_opt {
                    let (disp, dk) =
                        self.shared.borrow().normalize_key(&dst, self.is_wince, &self.cwd);
                    self.shared.borrow_mut().nodes.insert(
                        dk,
                        WinVfsNode {
                            display_path: disp,
                            attrs: 0x20,
                            reparse_tag: 0,
                            reparse_target: String::new(),
                            data: Rc::new(RefCell::new(bytes)),
                        },
                    );
                    Ok(1)
                } else {
                    self.last_error = 2;
                    Ok(0)
                }
            }
            "MoveFileW" | "MoveFileExW" => {
                let src = mem.read_wstr(arg(0, mem)?)?;
                let dst = mem.read_wstr(arg(1, mem)?)?;
                let (disp, dk) = self.shared.borrow().normalize_key(&dst, self.is_wince, &self.cwd);
                let (_, sk) = self.shared.borrow().normalize_key(&src, self.is_wince, &self.cwd);
                let mut env = self.shared.borrow_mut();
                if let Some(mut node) = env.nodes.remove(&sk) {
                    node.display_path = disp;
                    env.nodes.insert(dk, node);
                    Ok(1)
                } else {
                    self.last_error = 2;
                    Ok(0)
                }
            }
            "FindFirstFileW" => {
                let pat = mem.read_wstr(arg(0, mem)?)?;
                let wfd = arg(1, mem)?;
                self.find_first(mem, &pat, wfd)
            }
            "FindFirstFileExW" => {
                let pat = mem.read_wstr(arg(0, mem)?)?;
                let wfd = arg(2, mem)?;
                self.find_first(mem, &pat, wfd)
            }
            "FindNextFileW" => {
                let h = arg(0, mem)?;
                let wfd_ptr = arg(1, mem)?;
                self.find_next(mem, h, wfd_ptr)
            }
            "FindClose" => {
                let h = arg(0, mem)?;
                let _ = self.handles.remove(&h);
                Ok(1)
            }
            "GetDiskFreeSpaceExW" => {
                let free_avail = arg(1, mem)?;
                let total_bytes = arg(2, mem)?;
                let total_free = arg(3, mem)?;
                let free_val: u64 = 10_737_418_240;
                let tot_val: u64 = 68_719_476_736;
                if free_avail != 0 {
                    mem.write_u64_le(free_avail, free_val)?;
                }
                if total_bytes != 0 {
                    mem.write_u64_le(total_bytes, tot_val)?;
                }
                if total_free != 0 {
                    mem.write_u64_le(total_free, free_val)?;
                }
                Ok(1)
            }
            "GetLocalTime" => {
                let st = arg(0, mem)?;
                if st != 0 {
                    mem.write_u16_le(st, 2026)?;
                    mem.write_u16_le(st.wrapping_add(2), 11)?;
                    mem.write_u16_le(st.wrapping_add(4), 1)?;
                    mem.write_u16_le(st.wrapping_add(6), 23)?;
                    mem.write_u16_le(st.wrapping_add(8), 14)?;
                    mem.write_u16_le(st.wrapping_add(10), 30)?;
                    mem.write_u16_le(st.wrapping_add(12), 45)?;
                    mem.write_u16_le(st.wrapping_add(14), 120)?;
                }
                Ok(1)
            }
            "CreateSymbolicLinkW" => {
                let link = mem.read_wstr(arg(0, mem)?)?;
                let target = mem.read_wstr(arg(1, mem)?)?;
                let flags = u32::try_from(arg(2, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let is_dir = (flags & 0x1) != 0;
                let (disp, lk) = self.shared.borrow().normalize_key(&link, self.is_wince, &self.cwd);
                self.shared.borrow_mut().nodes.insert(
                    lk,
                    WinVfsNode {
                        display_path: disp,
                        attrs: (if is_dir { 0x10 } else { 0x20 }) | 0x0400,
                        reparse_tag: 0xA000_000C,
                        reparse_target: target,
                        data: Rc::new(RefCell::new(Vec::new())),
                    },
                );
                Ok(1)
            }
            "CreateHardLinkW" => {
                let link = mem.read_wstr(arg(0, mem)?)?;
                let target = mem.read_wstr(arg(1, mem)?)?;
                let shared_data = {
                    let env = self.shared.borrow();
                    let (_, tk) = env.normalize_key(&target, self.is_wince, &self.cwd);
                    let rk = env.resolve_target_key(&tk, self.is_wince, &self.cwd);
                    env.nodes.get(&rk).map(|n| Rc::clone(&n.data))
                };
                if let Some(data_rc) = shared_data {
                    let (disp, lk) =
                        self.shared.borrow().normalize_key(&link, self.is_wince, &self.cwd);
                    self.shared.borrow_mut().nodes.insert(
                        lk,
                        WinVfsNode {
                            display_path: disp,
                            attrs: 0x20,
                            reparse_tag: 0,
                            reparse_target: String::new(),
                            data: data_rc,
                        },
                    );
                    Ok(1)
                } else {
                    self.last_error = 2;
                    Ok(0)
                }
            }
            "DeviceIoControl" => {
                let h = arg(0, mem)?;
                let code = u32::try_from(arg(1, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let in_buf = arg(2, mem)?;
                let out_buf = arg(4, mem)?;
                let bytes_ret_ptr = arg(6, mem)?;
                self.device_io_control(mem, h, code, in_buf, out_buf, bytes_ret_ptr)
            }
            "GetModuleHandleW" | "LoadLibraryW" => {
                let name = mem.read_wstr(arg(0, mem)?).unwrap_or_default().to_ascii_uppercase();
                if name.contains("KERNEL32") {
                    Ok(0x7700_0000)
                } else if name.contains("NTDLL") {
                    Ok(0x7710_0000)
                } else if name.contains("ADVAPI32") {
                    Ok(0x7720_0000)
                } else if name.contains("PSAPI") {
                    Ok(0x7730_0000)
                } else {
                    Ok(0)
                }
            }
            "GetProcAddress" => {
                let fn_name = mem.read_cstr(arg(1, mem)?)?;
                Ok(thunk_resolver(&fn_name))
            }
            "GetCurrentProcess" => Ok(if is_64 { u64::MAX } else { 0xFFFF_FFFF }),
            "GetCurrentProcessId" => Ok(4242),
            "GlobalMemoryStatusEx" => {
                let ptr = arg(0, mem)?;
                if ptr != 0 {
                    mem.write_u32_le(ptr, 64)?;
                    mem.write_u32_le(ptr + 4, 25)?;
                    mem.write_u64_le(ptr + 8, 8_589_934_592)?;
                    mem.write_u64_le(ptr + 16, 6_442_450_944)?;
                    mem.write_u64_le(ptr + 24, 17_179_869_184)?;
                    mem.write_u64_le(ptr + 32, 12_884_901_888)?;
                    mem.write_u64_le(ptr + 40, 2_147_352_576)?;
                    mem.write_u64_le(ptr + 48, 2_080_374_784)?;
                    mem.write_u64_le(ptr + 56, 0)?;
                }
                Ok(1)
            }
            "GlobalMemoryStatus" => {
                let ptr = arg(0, mem)?;
                if ptr != 0 {
                    if is_64 {
                        mem.write_u32_le(ptr, 56)?;
                        mem.write_u32_le(ptr + 4, 25)?;
                        mem.write_u64_le(ptr + 8, 2_147_483_648)?;
                        mem.write_u64_le(ptr + 16, 1_610_612_736)?;
                        mem.write_u64_le(ptr + 24, 4_294_967_296)?;
                        mem.write_u64_le(ptr + 32, 3_221_225_472)?;
                        mem.write_u64_le(ptr + 40, 2_147_352_576)?;
                        mem.write_u64_le(ptr + 48, 2_080_374_784)?;
                    } else {
                        mem.write_u32_le(ptr, 32)?;
                        mem.write_u32_le(ptr + 4, 25)?;
                        mem.write_u32_le(ptr + 8, 536_870_912)?;
                        mem.write_u32_le(ptr + 12, 402_653_184)?;
                        mem.write_u32_le(ptr + 16, 1_073_741_824)?;
                        mem.write_u32_le(ptr + 20, 805_306_368)?;
                        mem.write_u32_le(ptr + 24, 2_147_352_576)?;
                        mem.write_u32_le(ptr + 28, 2_080_374_784)?;
                    }
                }
                Ok(1)
            }
            "GetProcessMemoryInfo" | "K32GetProcessMemoryInfo" => {
                let ptr = arg(1, mem)?;
                let cb = u32::try_from(arg(2, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                if ptr != 0 && cb >= 16 {
                    mem.write_u32_le(ptr, cb)?;
                    let ws_off = if is_64 { 16 } else { 12 };
                    if u64::from(cb) >= ws_off + if is_64 { 8 } else { 4 } {
                        mem.write_ptr(ptr + ws_off, 4_194_304, is_64)?;
                    }
                }
                Ok(1)
            }
            "GetComputerNameExW" => {
                let dst = arg(1, mem)?;
                let sz_ptr = arg(2, mem)?;
                let host = "WINCMDHOST";
                if sz_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(sz_ptr)?).unwrap_or(0);
                    if dst != 0 && cap > host.len() {
                        let n = mem.write_wstr(dst, host, cap)?;
                        mem.write_u32_le(sz_ptr, n)?;
                        return Ok(1);
                    }
                    mem.write_u32_le(sz_ptr, u32::try_from(host.len() + 1).unwrap_or(0))?;
                }
                Ok(0)
            }
            "GetComputerNameW" => {
                let dst = arg(0, mem)?;
                let sz_ptr = arg(1, mem)?;
                let host = "WINCMDHOST";
                if sz_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(sz_ptr)?).unwrap_or(0);
                    if dst != 0 && cap > host.len() {
                        let n = mem.write_wstr(dst, host, cap)?;
                        mem.write_u32_le(sz_ptr, n)?;
                        return Ok(1);
                    }
                    mem.write_u32_le(sz_ptr, u32::try_from(host.len() + 1).unwrap_or(0))?;
                }
                Ok(0)
            }
            "GetEnvironmentVariableW" => {
                let name = mem.read_wstr(arg(0, mem)?)?;
                let dst = arg(1, mem)?;
                let cap = usize::try_from(arg(2, mem)?).unwrap_or(0);
                if let Some(val) = self.lookup_guest_env(mem, &name)? {
                    if dst != 0 && cap > val.encode_utf16().count() {
                        let n = mem.write_wstr(dst, &val, cap)?;
                        Ok(u64::from(n))
                    } else {
                        Ok(u64::try_from(val.encode_utf16().count() + 1).unwrap_or(0))
                    }
                } else {
                    self.last_error = 203;
                    Ok(0)
                }
            }
            "GetFullPathNameW" => {
                let raw = mem.read_wstr(arg(0, mem)?)?;
                let cap = usize::try_from(arg(1, mem)?).unwrap_or(0);
                let dst = arg(2, mem)?;
                let part_ptr = arg(3, mem)?;
                let (disp, _) = self.shared.borrow().normalize_key(&raw, self.is_wince, &self.cwd);
                if dst != 0 && cap > disp.encode_utf16().count() {
                    let n = mem.write_wstr(dst, &disp, cap)?;
                    if part_ptr != 0 {
                        let leaf_u16_off = disp
                            .rfind(BS)
                            .map_or(0, |idx| disp[..=idx].encode_utf16().count() * 2);
                        mem.write_ptr(part_ptr, dst + u64::try_from(leaf_u16_off).unwrap_or(0), is_64)?;
                    }
                    Ok(u64::from(n))
                } else {
                    Ok(u64::try_from(disp.encode_utf16().count() + 1).unwrap_or(0))
                }
            }
            "GetFinalPathNameByHandleW" => {
                let h = arg(0, mem)?;
                let dst = arg(1, mem)?;
                let cap = usize::try_from(arg(2, mem)?).unwrap_or(0);
                let Some(WinHandle::File { key, .. }) = self.handles.get(&h).cloned() else {
                    self.last_error = 6;
                    return Ok(0);
                };
                let disp = {
                    let env = self.shared.borrow();
                    let resolved = env.resolve_target_key(&key, self.is_wince, &self.cwd);
                    env.nodes
                        .get(&resolved)
                        .map_or(resolved, |n| n.display_path.clone())
                };
                let full = format!("{}{}{disp}", unc_str(), format_args!("?{BS}"));
                if dst != 0 && cap > full.encode_utf16().count() {
                    let n = mem.write_wstr(dst, &full, cap)?;
                    Ok(u64::from(n))
                } else {
                    Ok(u64::try_from(full.encode_utf16().count() + 1).unwrap_or(0))
                }
            }
            "GetFileSizeEx" => {
                let h = arg(0, mem)?;
                let sz_ptr = arg(1, mem)?;
                let Some(WinHandle::File { key, .. }) = self.handles.get(&h).cloned() else {
                    self.last_error = 6;
                    return Ok(0);
                };
                let sz = self
                    .shared
                    .borrow()
                    .nodes
                    .get(&key)
                    .map_or(0, |n| u64::try_from(n.data.borrow().len()).unwrap_or(0));
                if sz_ptr != 0 {
                    mem.write_u64_le(sz_ptr, sz)?;
                }
                Ok(1)
            }
            "GetFileSize" => {
                let h = arg(0, mem)?;
                let hi_ptr = arg(1, mem)?;
                let Some(WinHandle::File { key, .. }) = self.handles.get(&h).cloned() else {
                    self.last_error = 6;
                    return Ok(0xFFFF_FFFF);
                };
                let sz = self
                    .shared
                    .borrow()
                    .nodes
                    .get(&key)
                    .map_or(0, |n| u64::try_from(n.data.borrow().len()).unwrap_or(0));
                if hi_ptr != 0 {
                    mem.write_u32_le(hi_ptr, u32::try_from(sz >> 32).unwrap_or(0))?;
                }
                Ok(sz & 0xFFFF_FFFF)
            }
            "GetFileTime" => {
                let cr = arg(1, mem)?;
                let ac = arg(2, mem)?;
                let wr = arg(3, mem)?;
                for p in [cr, ac, wr] {
                    if p != 0 {
                        mem.write_u32_le(p, 0x8000_0000)?;
                        mem.write_u32_le(p + 4, 0x01DC_5BE0)?;
                    }
                }
                Ok(1)
            }
            "CompareFileTime" => {
                let ft1 = mem.read_u64_le(arg(0, mem)?)?;
                let ft2 = mem.read_u64_le(arg(1, mem)?)?;
                let res: u64 = match ft1.cmp(&ft2) {
                    core::cmp::Ordering::Less => 0xFFFF_FFFF,
                    core::cmp::Ordering::Equal => 0,
                    core::cmp::Ordering::Greater => 1,
                };
                Ok(res)
            }
            "RegOpenKeyExW" => {
                let root = arg(0, mem)?;
                let sub = mem.read_wstr(arg(1, mem)?)?;
                let out_hk = arg(4, mem)?;
                let full = format!("{}{BS}{}", Self::reg_root_name(root), sub.trim_matches(BS));
                let exists = self.shared.borrow().registry.contains_key(&full.to_ascii_uppercase());
                if exists {
                    let hk = self.alloc_handle(WinHandle::RegKey {
                        path: full.to_ascii_uppercase(),
                    });
                    mem.write_ptr(out_hk, hk, is_64)?;
                    Ok(0)
                } else {
                    Ok(2)
                }
            }
            "RegCreateKeyExW" => {
                let root = arg(0, mem)?;
                let sub = mem.read_wstr(arg(1, mem)?)?;
                let out_hk = arg(7, mem)?;
                let full = format!("{}{BS}{}", Self::reg_root_name(root), sub.trim_matches(BS))
                    .to_ascii_uppercase();
                self.shared.borrow_mut().registry.entry(full.clone()).or_default();
                let hk = self.alloc_handle(WinHandle::RegKey { path: full });
                mem.write_ptr(out_hk, hk, is_64)?;
                Ok(0)
            }
            "RegSetValueExW" => {
                let hk = arg(0, mem)?;
                let val_name = mem.read_wstr(arg(1, mem)?)?.to_ascii_uppercase();
                let reg_type = u32::try_from(arg(3, mem)? & 0xFFFF_FFFF).unwrap_or(1);
                let data_ptr = arg(4, mem)?;
                let cb = usize::try_from(arg(5, mem)?).unwrap_or(0);
                let Some(WinHandle::RegKey { path }) = self.handles.get(&hk).cloned() else {
                    return Ok(6);
                };
                let raw = mem.read_slice(data_ptr, cb)?;
                self.shared
                    .borrow_mut()
                    .registry
                    .entry(path)
                    .or_default()
                    .insert(val_name, (reg_type, raw));
                Ok(0)
            }
            "RegQueryValueExW" => {
                let hk = arg(0, mem)?;
                let val_name = mem.read_wstr(arg(1, mem)?)?.to_ascii_uppercase();
                let type_ptr = arg(3, mem)?;
                let data_ptr = arg(4, mem)?;
                let cb_ptr = arg(5, mem)?;
                let Some(WinHandle::RegKey { path }) = self.handles.get(&hk).cloned() else {
                    return Ok(6);
                };
                let found = self
                    .shared
                    .borrow()
                    .registry
                    .get(&path)
                    .and_then(|m| m.get(&val_name).cloned());
                let Some((reg_type, raw)) = found else {
                    return Ok(2);
                };
                if type_ptr != 0 {
                    mem.write_u32_le(type_ptr, reg_type)?;
                }
                if cb_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(cb_ptr)?).unwrap_or(0);
                    mem.write_u32_le(cb_ptr, u32::try_from(raw.len()).unwrap_or(0))?;
                    if data_ptr != 0 && cap >= raw.len() {
                        mem.write_slice(data_ptr, &raw)?;
                    }
                }
                Ok(0)
            }
            "RegCloseKey" => {
                let hk = arg(0, mem)?;
                let _ = self.handles.remove(&hk);
                Ok(0)
            }
            "RegDeleteValueW" => {
                let hk = arg(0, mem)?;
                let val_name = mem.read_wstr(arg(1, mem)?)?.to_ascii_uppercase();
                let Some(WinHandle::RegKey { path }) = self.handles.get(&hk).cloned() else {
                    return Ok(6);
                };
                let removed = self
                    .shared
                    .borrow_mut()
                    .registry
                    .get_mut(&path)
                    .and_then(|m| m.remove(&val_name))
                    .is_some();
                Ok(if removed { 0 } else { 2 })
            }
            "RegDeleteKeyW" | "RegDeleteKeyExW" => {
                let root = arg(0, mem)?;
                let sub = mem.read_wstr(arg(1, mem)?)?;
                let base = if let Some(WinHandle::RegKey { path }) = self.handles.get(&root).cloned()
                {
                    if sub.is_empty() {
                        path
                    } else {
                        format!("{path}{BS}{}", sub.trim_matches(BS)).to_ascii_uppercase()
                    }
                } else {
                    format!("{}{BS}{}", Self::reg_root_name(root), sub.trim_matches(BS))
                        .to_ascii_uppercase()
                };
                let removed = self.shared.borrow_mut().registry.remove(&base).is_some();
                Ok(if removed { 0 } else { 2 })
            }
            "RegEnumValueW" => {
                let hk = arg(0, mem)?;
                let idx = usize::try_from(arg(1, mem)?).unwrap_or(0);
                let name_ptr = arg(2, mem)?;
                let cch_name_ptr = arg(3, mem)?;
                let type_ptr = arg(5, mem)?;
                let data_ptr = arg(6, mem)?;
                let cb_data_ptr = arg(7, mem)?;
                let Some(WinHandle::RegKey { path }) = self.handles.get(&hk).cloned() else {
                    return Ok(6);
                };
                let nth = self
                    .shared
                    .borrow()
                    .registry
                    .get(&path)
                    .and_then(|m| m.iter().nth(idx).map(|(k, v)| (k.clone(), v.clone())));
                let Some((val_name, (reg_type, raw))) = nth else {
                    return Ok(259);
                };
                if cch_name_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(cch_name_ptr)?).unwrap_or(0);
                    let n = if name_ptr != 0 && cap > 0 {
                        mem.write_wstr(name_ptr, &val_name, cap)?
                    } else {
                        0
                    };
                    mem.write_u32_le(cch_name_ptr, n)?;
                }
                if type_ptr != 0 {
                    mem.write_u32_le(type_ptr, reg_type)?;
                }
                if cb_data_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(cb_data_ptr)?).unwrap_or(0);
                    mem.write_u32_le(cb_data_ptr, u32::try_from(raw.len()).unwrap_or(0))?;
                    if data_ptr != 0 && cap >= raw.len() {
                        mem.write_slice(data_ptr, &raw)?;
                    }
                }
                Ok(0)
            }
            "RegEnumKeyExW" => {
                let hk = arg(0, mem)?;
                let idx = usize::try_from(arg(1, mem)?).unwrap_or(0);
                let name_ptr = arg(2, mem)?;
                let cch_ptr = arg(3, mem)?;
                let Some(WinHandle::RegKey { path }) = self.handles.get(&hk).cloned() else {
                    return Ok(6);
                };
                let prefix = format!("{path}{BS}");
                let mut children = BTreeSet::new();
                for k in self.shared.borrow().registry.keys() {
                    if let Some(rest) = k.strip_prefix(&prefix) {
                        let child = rest.split(BS).next().unwrap_or("");
                        if !child.is_empty() {
                            let _ = children.insert(child.to_owned());
                        }
                    }
                }
                let Some(sub_name) = children.into_iter().nth(idx) else {
                    return Ok(259);
                };
                if cch_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(cch_ptr)?).unwrap_or(0);
                    let n = mem.write_wstr(name_ptr, &sub_name, cap)?;
                    mem.write_u32_le(cch_ptr, n)?;
                }
                Ok(0)
            }
            "OpenProcessToken" => {
                let out_tok = arg(2, mem)?;
                let ht = self.alloc_handle(WinHandle::Token);
                mem.write_ptr(out_tok, ht, is_64)?;
                Ok(1)
            }
            "GetTokenInformation" => {
                let class = u32::try_from(arg(1, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                let info_ptr = arg(2, mem)?;
                let info_len = usize::try_from(arg(3, mem)?).unwrap_or(0);
                let ret_len_ptr = arg(4, mem)?;
                // Binary SID S-1-5-21-500 (16 bytes): rev=1, count=2, auth=[0,0,0,0,0,5], sub0=21, sub1=500
                let sid_bytes: [u8; 16] = [
                    1, 2, 0, 0, 0, 0, 0, 5, 21, 0, 0, 0, 244, 1, 0, 0,
                ];
                let hdr_sz: usize = if is_64 { 16 } else { 8 };
                if class == 1 {
                    let need = hdr_sz + sid_bytes.len();
                    if ret_len_ptr != 0 {
                        mem.write_u32_le(ret_len_ptr, u32::try_from(need).unwrap_or(0))?;
                    }
                    if info_ptr == 0 || info_len < need {
                        self.last_error = 122;
                        return Ok(0);
                    }
                    let sid_addr = info_ptr + u64::try_from(hdr_sz).unwrap_or(0);
                    mem.write_ptr(info_ptr, sid_addr, is_64)?;
                    mem.write_ptr(info_ptr + if is_64 { 8 } else { 4 }, 0, is_64)?;
                    mem.write_slice(sid_addr, &sid_bytes)?;
                    Ok(1)
                } else {
                    if ret_len_ptr != 0 {
                        mem.write_u32_le(ret_len_ptr, 16)?;
                    }
                    if info_ptr != 0 && info_len >= 16 {
                        mem.write_slice(info_ptr, &[0u8; 16])?;
                        Ok(1)
                    } else {
                        self.last_error = 122;
                        Ok(0)
                    }
                }
            }
            "LookupAccountSidW" => {
                let name_ptr = arg(2, mem)?;
                let cch_name_ptr = arg(3, mem)?;
                let dom_ptr = arg(4, mem)?;
                let cch_dom_ptr = arg(5, mem)?;
                let use_ptr = arg(6, mem)?;
                let usr = "Administrator";
                let dom = "WINCMDHOST";
                if cch_name_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(cch_name_ptr)?).unwrap_or(0);
                    if name_ptr != 0 && cap > usr.len() {
                        let n = mem.write_wstr(name_ptr, usr, cap)?;
                        mem.write_u32_le(cch_name_ptr, n)?;
                    }
                }
                if cch_dom_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(cch_dom_ptr)?).unwrap_or(0);
                    if dom_ptr != 0 && cap > dom.len() {
                        let n = mem.write_wstr(dom_ptr, dom, cap)?;
                        mem.write_u32_le(cch_dom_ptr, n)?;
                    }
                }
                if use_ptr != 0 {
                    mem.write_u32_le(use_ptr, 1)?;
                }
                Ok(1)
            }
            "LookupPrivilegeNameW" => {
                let name_ptr = arg(2, mem)?;
                let cch_ptr = arg(3, mem)?;
                let priv_name = "SeChangeNotifyPrivilege";
                if cch_ptr != 0 {
                    let cap = usize::try_from(mem.read_u32_le(cch_ptr)?).unwrap_or(0);
                    if name_ptr != 0 && cap > priv_name.len() {
                        let n = mem.write_wstr(name_ptr, priv_name, cap)?;
                        mem.write_u32_le(cch_ptr, n)?;
                    }
                }
                Ok(1)
            }
            "CreatePipe" => {
                let rd_ptr = arg(0, mem)?;
                let wr_ptr = arg(1, mem)?;
                let pid = {
                    let mut env = self.shared.borrow_mut();
                    let id = env.next_pipe_id;
                    env.next_pipe_id += 1;
                    env.pipes.insert(id, Vec::new());
                    id
                };
                let hrd = self.alloc_handle(WinHandle::PipeRead {
                    pipe_id: pid,
                    pos: 0,
                });
                let hwr = self.alloc_handle(WinHandle::PipeWrite { pipe_id: pid });
                mem.write_ptr(rd_ptr, hrd, is_64)?;
                mem.write_ptr(wr_ptr, hwr, is_64)?;
                Ok(1)
            }
            "CreateProcessW" => {
                let app_ptr = arg(0, mem)?;
                let cmd_ptr = arg(1, mem)?;
                let si_ptr = arg(8, mem)?;
                let pi_ptr = arg(9, mem)?;
                self.spawn_process(mem, app_ptr, cmd_ptr, si_ptr, pi_ptr)
            }
            "WaitForSingleObject" | "WaitForMultipleObjects" | "CreatePseudoConsole" => Ok(0),
            "GetExitCodeProcess" => {
                let hp = arg(0, mem)?;
                let code_ptr = arg(1, mem)?;
                let code = match self.handles.get(&hp) {
                    Some(WinHandle::Process { exit_code }) => *exit_code,
                    _ => 0,
                };
                if code_ptr != 0 {
                    mem.write_u32_le(code_ptr, code)?;
                }
                Ok(1)
            }
            "NtQueryInformationFile" => {
                let h = arg(0, mem)?;
                let info_buf = arg(2, mem)?;
                let class = u32::try_from(arg(4, mem)? & 0xFFFF_FFFF).unwrap_or(0);
                if class == 71 {
                    let is_cs = if let Some(WinHandle::File { key, .. }) = self.handles.get(&h) {
                        self.shared
                            .borrow()
                            .case_sensitive_dirs
                            .contains(&key.to_ascii_uppercase())
                            || self.shared.borrow().case_sensitive_dirs.contains(key)
                    } else {
                        false
                    };
                    if info_buf != 0 {
                        mem.write_u32_le(info_buf, u32::from(is_cs))?;
                    }
                    Ok(0)
                } else {
                    Ok(0xC000_0002)
                }
            }
            "GetMachineTypeAttributes" => {
                let out_ptr = arg(1, mem)?;
                if out_ptr != 0 {
                    mem.write_u32_le(out_ptr, 0x03)?;
                }
                Ok(0)
            }
            other => Err(emu_err(
                0,
                ErrCode::InvalidFunction,
                format!("unhandled Win32/WinCE import `{other}`"),
            )),
        }
    }

    /// Allocates and registers a simulated worker thread handle (`CreateThread`).
    pub fn alloc_thread_handle(&mut self) -> u64 {
        self.alloc_handle(WinHandle::Thread)
    }

    fn lookup_guest_env(
        &self,
        mem: &dyn GuestMem,
        target_name: &str,
    ) -> Result<Option<String>, ToolError> {
        if let Some(ov) = self.env_overrides.get(&target_name.to_ascii_uppercase()) {
            return Ok(ov.clone());
        }
        let mut cur = self.env_w_addr;
        if cur == 0 {
            return Ok(None);
        }
        loop {
            let entry = mem.read_wstr(cur)?;
            if entry.is_empty() {
                break;
            }
            let u16_units = u64::try_from(entry.encode_utf16().count() + 1).unwrap_or(1);
            cur = cur.saturating_add(u16_units * 2);
            if let Some((k, v)) = entry.split_once('=')
                && k.eq_ignore_ascii_case(target_name)
            {
                return Ok(Some(v.to_owned()));
            }
        }
        Ok(None)
    }

    const fn reg_root_name(root: u64) -> &'static str {
        match root & 0xFFFF_FFFF {
            0x8000_0000 => "HKCR",
            0x8000_0001 => "HKCU",
            0x8000_0002 => "HKLM",
            _ => "HKU",
        }
    }

    fn write_handle_bytes(&mut self, h: u64, data: &[u8]) {
        match self.handles.get_mut(&h).cloned() {
            Some(WinHandle::StdOut) => self.stdout.extend_from_slice(data),
            Some(WinHandle::StdErr) => self.stderr.extend_from_slice(data),
            Some(WinHandle::PipeWrite { pipe_id }) => {
                if let Some(buf) = self.shared.borrow_mut().pipes.get_mut(&pipe_id) {
                    buf.extend_from_slice(data);
                }
            }
            Some(WinHandle::File {
                key,
                pos,
                writable: true,
            }) => {
                if let Some(node) = self.shared.borrow().nodes.get(&key) {
                    let mut fbuf = node.data.borrow_mut();
                    let p = usize::try_from(pos).unwrap_or(fbuf.len());
                    if p > fbuf.len() {
                        fbuf.resize(p, 0);
                    }
                    let end = p + data.len();
                    if end > fbuf.len() {
                        fbuf.resize(end, 0);
                    }
                    fbuf[p..end].copy_from_slice(data);
                }
                if let Some(WinHandle::File { pos: cur, .. }) = self.handles.get_mut(&h) {
                    *cur = pos.saturating_add(u64::try_from(data.len()).unwrap_or(0));
                }
            }
            _ => {}
        }
    }

    fn read_handle_bytes(&mut self, h: u64, cap: usize) -> Vec<u8> {
        if cap == 0 {
            return Vec::new();
        }
        match self.handles.get(&h).cloned() {
            Some(WinHandle::StdIn) => {
                let avail = self.stdin_buf.len().saturating_sub(self.stdin_pos).min(cap);
                let out = self.stdin_buf[self.stdin_pos..self.stdin_pos + avail].to_vec();
                self.stdin_pos += avail;
                out
            }
            Some(WinHandle::PipeRead { pipe_id, pos }) => {
                let chunk = self
                    .shared
                    .borrow()
                    .pipes
                    .get(&pipe_id)
                    .map_or_else(Vec::new, |b| {
                        let n = b.len().saturating_sub(pos).min(cap);
                        b[pos..pos + n].to_vec()
                    });
                if let Some(WinHandle::PipeRead { pos: cur, .. }) = self.handles.get_mut(&h) {
                    *cur += chunk.len();
                }
                chunk
            }
            Some(WinHandle::File { key, pos, .. }) => {
                let chunk = self
                    .shared
                    .borrow()
                    .nodes
                    .get(&key)
                    .map_or_else(Vec::new, |node| {
                        let b = node.data.borrow();
                        let p = usize::try_from(pos).unwrap_or(b.len()).min(b.len());
                        let n = b.len().saturating_sub(p).min(cap);
                        b[p..p + n].to_vec()
                    });
                if let Some(WinHandle::File { pos: cur, .. }) = self.handles.get_mut(&h) {
                    *cur = pos.saturating_add(u64::try_from(chunk.len()).unwrap_or(0));
                }
                chunk
            }
            _ => Vec::new(),
        }
    }

    fn open_or_create_file(
        &mut self,
        raw_path: &str,
        access: u32,
        disp_mode: u32,
        flags: u32,
    ) -> u64 {
        let nul_dev = format!("{BS}{BS}.{BS}NUL");
        let wcnul_sfx = format!("{BS}_WCNUL.TMP");
        let up = raw_path.trim().to_ascii_uppercase();
        if up == nul_dev || up == "NUL" || up.ends_with(&wcnul_sfx) {
            return self.alloc_handle(WinHandle::Nul);
        }
        let open_reparse = (flags & 0x0020_0000) != 0;
        let backup_sem = (flags & 0x0200_0000) != 0;
        let writable = (access & 0x4000_0000) != 0 || (access & 0x0000_0100) != 0;

        let (disp, raw_key) = self
            .shared
            .borrow()
            .normalize_key(raw_path, self.is_wince, &self.cwd);
        let key = if open_reparse {
            raw_key
        } else {
            self.shared
                .borrow()
                .resolve_target_key(&raw_key, self.is_wince, &self.cwd)
        };

        let exists = self.shared.borrow().nodes.contains_key(&key);
        let is_dir = self
            .shared
            .borrow()
            .nodes
            .get(&key)
            .is_some_and(|n| (n.attrs & 0x10) != 0);
        if is_dir && !backup_sem {
            self.last_error = 5;
            return self.invalid_handle_val();
        }

        match disp_mode {
            2 => {
                if let Some((parent, _)) = disp.rsplit_once(BS)
                    && !parent.is_empty()
                {
                    let (_, pk) =
                        self.shared
                            .borrow()
                            .normalize_key(parent, self.is_wince, &self.cwd);
                    if !self.shared.borrow().nodes.contains_key(&pk) {
                        self.last_error = 3;
                        return self.invalid_handle_val();
                    }
                }
                self.shared.borrow_mut().nodes.insert(
                    key.clone(),
                    WinVfsNode {
                        display_path: disp,
                        attrs: 0x20,
                        reparse_tag: 0,
                        reparse_target: String::new(),
                        data: Rc::new(RefCell::new(Vec::new())),
                    },
                );
            }
            4 => {
                if !exists {
                    self.shared.borrow_mut().nodes.insert(
                        key.clone(),
                        WinVfsNode {
                            display_path: disp,
                            attrs: 0x20,
                            reparse_tag: 0,
                            reparse_target: String::new(),
                            data: Rc::new(RefCell::new(Vec::new())),
                        },
                    );
                }
            }
            _ => {
                if !exists {
                    self.last_error = 2;
                    return self.invalid_handle_val();
                }
            }
        }

        self.alloc_handle(WinHandle::File {
            key,
            pos: 0,
            writable,
        })
    }

    fn seek_handle(&mut self, h: u64, dist: i64, method: u32) -> u64 {
        let Some(WinHandle::File { key, pos, .. }) = self.handles.get(&h).cloned() else {
            return 0;
        };
        let file_len = self
            .shared
            .borrow()
            .nodes
            .get(&key)
            .map_or(0, |n| i64::try_from(n.data.borrow().len()).unwrap_or(0));
        let base = match method {
            1 => i64::try_from(pos).unwrap_or(0),
            2 => file_len,
            _ => 0,
        };
        let next = u64::try_from((base + dist).max(0)).unwrap_or(0);
        if let Some(WinHandle::File { pos: cur, .. }) = self.handles.get_mut(&h) {
            *cur = next;
        }
        next
    }

    fn find_first<M: GuestMem>(
        &mut self,
        mem: &mut M,
        raw_pat: &str,
        wfd_ptr: u64,
    ) -> Result<u64, ToolError> {
        let long_pfx = format!("{BS}{BS}?{BS}");
        let mut s = raw_pat.trim().replace('/', bs_str());
        if s.starts_with(&long_pfx) {
            let _ = s.drain(..long_pfx.len());
        }
        let (dir_part, leaf_pat) = if let Some((d, l)) = s.rsplit_once(BS) {
            let dir_s = if d.is_empty() {
                bs_str().to_owned()
            } else if d.len() == 2 && d.as_bytes()[1] == b':' {
                format!("{d}{BS}")
            } else {
                d.to_owned()
            };
            (dir_s, l)
        } else {
            (self.cwd.clone(), s.as_str())
        };

        let (dir_disp, dir_key) =
            self.shared
                .borrow()
                .normalize_key(&dir_part, self.is_wince, &self.cwd);
        let resolved_dir_key =
            self.shared
                .borrow()
                .resolve_target_key(&dir_key, self.is_wince, &self.cwd);
        let resolved_disp = self
            .shared
            .borrow()
            .nodes
            .get(&resolved_dir_key)
            .map(|n| n.display_path.clone());
        let Some(parent_disp) = resolved_disp else {
            self.last_error = 3;
            return Ok(self.invalid_handle_val());
        };
        let case_sens = self
            .shared
            .borrow()
            .case_sensitive_dirs
            .contains(&dir_disp.to_ascii_uppercase())
            || self.shared.borrow().case_sensitive_dirs.contains(&resolved_dir_key);
        let parent_up = if parent_disp.ends_with(BS) {
            parent_disp.to_ascii_uppercase()
        } else {
            format!("{}{BS}", parent_disp.to_ascii_uppercase())
        };

        let mut entries = Vec::new();
        for (idx, node) in self.shared.borrow().nodes.values().enumerate() {
            let nd_up = node.display_path.to_ascii_uppercase();
            if nd_up == parent_disp.to_ascii_uppercase() {
                continue;
            }
            if let Some(rel) = nd_up.strip_prefix(&parent_up)
                && !rel.is_empty()
                && !rel.contains(BS)
            {
                let orig_leaf = &node.display_path[node.display_path.len() - rel.len()..];
                if win32_pat_match(leaf_pat, orig_leaf, case_sens) {
                    let sz = u64::try_from(node.data.borrow().len()).unwrap_or(0);
                    let short = make_short_83_name(orig_leaf, idx);
                    entries.push((
                        orig_leaf.to_owned(),
                        short,
                        node.attrs,
                        sz,
                        node.reparse_tag,
                    ));
                }
            }
        }
        if entries.is_empty() {
            self.last_error = 2;
            return Ok(self.invalid_handle_val());
        }
        self.write_wfd(mem, wfd_ptr, &entries[0])?;
        Ok(self.alloc_handle(WinHandle::Search { entries, idx: 1 }))
    }

    fn find_next<M: GuestMem>(
        &mut self,
        mem: &mut M,
        h: u64,
        wfd_ptr: u64,
    ) -> Result<u64, ToolError> {
        let next_entry = if let Some(WinHandle::Search { entries, idx }) = self.handles.get_mut(&h)
        {
            if *idx < entries.len() {
                let item = entries[*idx].clone();
                *idx += 1;
                Some(item)
            } else {
                None
            }
        } else {
            None
        };
        if let Some(ent) = next_entry {
            self.write_wfd(mem, wfd_ptr, &ent)?;
            Ok(1)
        } else {
            self.last_error = 18;
            Ok(0)
        }
    }

    fn write_wfd<M: GuestMem>(
        &self,
        mem: &mut M,
        wfd_ptr: u64,
        ent: &(String, String, u32, u64, u32),
    ) -> Result<(), ToolError> {
        let (name, short, attrs, size, rep_tag) = ent;
        let wfd_sz = if self.is_wince { 560 } else { 592 };
        mem.write_slice(wfd_ptr, &vec![0u8; wfd_sz])?;
        mem.write_u32_le(wfd_ptr, *attrs)?;
        mem.write_u32_le(wfd_ptr.wrapping_add(20), 0x8000_0000)?;
        mem.write_u32_le(wfd_ptr.wrapping_add(24), 0x01DC_5BE0)?;
        mem.write_u32_le(
            wfd_ptr.wrapping_add(28),
            u32::try_from(*size >> 32).unwrap_or(0),
        )?;
        mem.write_u32_le(
            wfd_ptr.wrapping_add(32),
            u32::try_from(*size & 0xFFFF_FFFF).unwrap_or(0),
        )?;
        mem.write_u32_le(wfd_ptr.wrapping_add(36), *rep_tag)?;
        if self.is_wince {
            let _ = mem.write_wstr(wfd_ptr.wrapping_add(40), name, 260)?;
        } else {
            let _ = mem.write_wstr(wfd_ptr.wrapping_add(44), name, 260)?;
            let _ = mem.write_wstr(wfd_ptr.wrapping_add(564), short, 14)?;
        }
        Ok(())
    }

    fn device_io_control<M: GuestMem>(
        &self,
        mem: &mut M,
        h: u64,
        code: u32,
        in_buf: u64,
        out_buf: u64,
        bytes_ret_ptr: u64,
    ) -> Result<u64, ToolError> {
        let Some(WinHandle::File { key, .. }) = self.handles.get(&h).cloned() else {
            return Ok(0);
        };
        match code {
            0x0009_00A4 => {
                let tag = mem.read_u32_le(in_buf)?;
                let sub_off = u64::from(mem.read_u16_le(in_buf.wrapping_add(8))?);
                let sub_len = usize::from(mem.read_u16_le(in_buf.wrapping_add(10))?);
                let mut units = Vec::with_capacity(sub_len / 2);
                for i in 0..(sub_len / 2) {
                    let off = u64::try_from(i * 2).unwrap_or(0);
                    units.push(mem.read_u16_le(in_buf.wrapping_add(16 + sub_off + off))?);
                }
                let tgt = String::from_utf16_lossy(&units);
                if let Some(node) = self.shared.borrow_mut().nodes.get_mut(&key) {
                    node.attrs |= 0x0400;
                    node.reparse_tag = tag;
                    node.reparse_target = tgt;
                }
                if bytes_ret_ptr != 0 {
                    mem.write_u32_le(bytes_ret_ptr, 0)?;
                }
                Ok(1)
            }
            0x0009_00A8 => {
                let rep = self
                    .shared
                    .borrow()
                    .nodes
                    .get(&key)
                    .map(|n| (n.reparse_tag, n.reparse_target.clone()));
                let Some((tag, tgt)) = rep else {
                    return Ok(0);
                };
                if tag == 0 {
                    return Ok(0);
                }
                mem.write_u32_le(out_buf, tag)?;
                if tag == 0x8000_001B {
                    mem.write_u32_le(out_buf.wrapping_add(8), 3)?;
                    let mut p = out_buf.wrapping_add(12);
                    let n1 = mem.write_wstr(p, "PkgId", 32)?;
                    p = p.wrapping_add(u64::from(n1 + 1) * 2);
                    let n2 = mem.write_wstr(p, "AppId", 32)?;
                    p = p.wrapping_add(u64::from(n2 + 1) * 2);
                    let n3 = mem.write_wstr(p, &tgt, 260)?;
                    let total = u32::try_from((p - out_buf) + u64::from(n3 + 1) * 2).unwrap_or(0);
                    if bytes_ret_ptr != 0 {
                        mem.write_u32_le(bytes_ret_ptr, total)?;
                    }
                } else {
                    let byte_len = u16::try_from(tgt.encode_utf16().count() * 2).unwrap_or(0);
                    mem.write_u16_le(out_buf.wrapping_add(4), byte_len + 12)?;
                    mem.write_u16_le(out_buf.wrapping_add(8), 0)?;
                    mem.write_u16_le(out_buf.wrapping_add(10), byte_len)?;
                    let _ = mem.write_wstr(out_buf.wrapping_add(16), &tgt, 260)?;
                    if bytes_ret_ptr != 0 {
                        mem.write_u32_le(bytes_ret_ptr, u32::from(byte_len) + 20)?;
                    }
                }
                Ok(1)
            }
            _ => Ok(0),
        }
    }

    fn spawn_process<M: GuestMem>(
        &mut self,
        mem: &mut M,
        app_ptr: u64,
        cmd_ptr: u64,
        si_ptr: u64,
        pi_ptr: u64,
    ) -> Result<u64, ToolError> {
        let app = mem.read_wstr(app_ptr)?;
        let cmd = mem.read_wstr(cmd_ptr)?;

        if !app.is_empty() {
            if std::path::Path::new(&app)
                .extension()
                .is_some_and(|ext| ext.eq_ignore_ascii_case("com"))
            {
                self.last_error = 193;
                return Ok(0);
            }
            if let Some(bytes) = self.shared.borrow().read_file(&app)
                && bytes.len() >= 2
                && &bytes[0..2] == b"MZ"
            {
                let is_pe = if bytes.len() >= 0x40 {
                    let lfanew = usize::try_from(u32::from_le_bytes([
                        bytes[0x3C],
                        bytes[0x3D],
                        bytes[0x3E],
                        bytes[0x3F],
                    ]))
                    .unwrap_or(0);
                    bytes.get(lfanew..lfanew + 4) == Some(&[b'P', b'E', 0, 0])
                } else {
                    false
                };
                if !is_pe {
                    self.last_error = 193;
                    return Ok(0);
                }
            }
        }

        let (mut child_in, mut child_out, mut child_err) = (
            self.std_in_handle,
            self.std_out_handle,
            self.std_err_handle,
        );
        if si_ptr != 0 && !self.is_wince {
            let flags_off = if self.is_64bit { 60 } else { 44 };
            let dw_flags = mem.read_u32_le(si_ptr.wrapping_add(flags_off))?;
            if (dw_flags & 0x0100) != 0 {
                if self.is_64bit {
                    child_in = mem.read_u64_le(si_ptr.wrapping_add(80))?;
                    child_out = mem.read_u64_le(si_ptr.wrapping_add(88))?;
                    child_err = mem.read_u64_le(si_ptr.wrapping_add(96))?;
                } else {
                    child_in = u64::from(mem.read_u32_le(si_ptr.wrapping_add(56))?);
                    child_out = u64::from(mem.read_u32_le(si_ptr.wrapping_add(60))?);
                    child_err = u64::from(mem.read_u32_le(si_ptr.wrapping_add(64))?);
                }
            }
        }

        // Convert parent handles to Pipe/File/Std descriptors in child
        let in_desc = self.handles.get(&child_in).cloned().unwrap_or(WinHandle::StdIn);
        let out_desc = self.handles.get(&child_out).cloned().unwrap_or(WinHandle::StdOut);
        let err_desc = self.handles.get(&child_err).cloned().unwrap_or(WinHandle::StdErr);

        let exit_code = if let Some(mut runner) = self.child_runner.take() {
            let full_cmd = if self.is_wince {
                cmd
            } else if cmd.is_empty() {
                format!("{DQ}{app}{DQ}")
            } else {
                cmd
            };
            let _ = (&in_desc, &out_desc, &err_desc);
            let res = runner(
                &full_cmd,
                Rc::clone(&self.shared),
                Self::encode_inherited_handle(&in_desc),
                Self::encode_inherited_handle(&out_desc),
                Self::encode_inherited_handle(&err_desc),
            );
            self.child_runner = Some(runner);
            let (code, c_out, c_err) = res?;
            if matches!(out_desc, WinHandle::StdOut) {
                self.stdout.extend_from_slice(&c_out);
            }
            if matches!(err_desc, WinHandle::StdErr) {
                self.stderr.extend_from_slice(&c_err);
            }
            code
        } else {
            0
        };

        let hp = self.alloc_handle(WinHandle::Process { exit_code });
        let ht = self.alloc_handle(WinHandle::Nul);
        if pi_ptr != 0 {
            if self.is_64bit {
                mem.write_u64_le(pi_ptr, hp)?;
                mem.write_u64_le(pi_ptr.wrapping_add(8), ht)?;
                mem.write_u32_le(pi_ptr.wrapping_add(16), 4242)?;
                mem.write_u32_le(pi_ptr.wrapping_add(20), 4243)?;
            } else {
                mem.write_u32_le(pi_ptr, u32::try_from(hp).unwrap_or(0))?;
                mem.write_u32_le(pi_ptr.wrapping_add(4), u32::try_from(ht).unwrap_or(0))?;
                mem.write_u32_le(pi_ptr.wrapping_add(8), 4242)?;
                mem.write_u32_le(pi_ptr.wrapping_add(12), 4243)?;
            }
        }
        Ok(1)
    }

    const fn encode_inherited_handle(desc: &WinHandle) -> u64 {
        match desc {
            WinHandle::PipeRead { pipe_id, .. } => 0x8000_0000 | (*pipe_id as u64),
            WinHandle::PipeWrite { pipe_id } => 0x9000_0000 | (*pipe_id as u64),
            WinHandle::Nul => 0xA000_0000,
            WinHandle::StdIn => 0x10,
            WinHandle::StdErr => 0x12,
            _ => 0x11,
        }
    }

    /// Installs an encoded inherited stdio handle (`PipeRead`, `PipeWrite`, `Nul`, or `Std*`)
    /// into a child process's `std_in_handle` / `std_out_handle` / `std_err_handle` slot.
    pub fn bind_inherited_stdio(&mut self, slot: u8, encoded: u64) -> Option<u32> {
        let mut parent_fwd_pipe = None;
        let h = if (encoded & 0xF000_0000) == 0x8000_0000 {
            let pid = u32::try_from(encoded & 0x0FFF_FFFF).unwrap_or(0);
            self.alloc_handle(WinHandle::PipeRead {
                pipe_id: pid,
                pos: 0,
            })
        } else if (encoded & 0xF000_0000) == 0x9000_0000 {
            let pid = u32::try_from(encoded & 0x0FFF_FFFF).unwrap_or(0);
            parent_fwd_pipe = Some(pid);
            self.alloc_handle(WinHandle::PipeWrite { pipe_id: pid })
        } else if encoded == 0xA000_0000 {
            self.alloc_handle(WinHandle::Nul)
        } else {
            u64::from(0x10 + slot)
        };
        match slot {
            0 => self.std_in_handle = h,
            1 => self.std_out_handle = h,
            _ => self.std_err_handle = h,
        }
        let _ = parent_fwd_pipe;
        None
    }
}

/// Synthesizes an 8.3 short alias (`FOOBAR~1.TXT`) for long filenames when `DIR /X` or `%~s1` queries `cAlternateFileName`.
#[must_use]
pub fn make_short_83_name(leaf: &str, ordinal: usize) -> String {
    if leaf == "." || leaf == ".." {
        return String::new();
    }
    let (stem, ext) = leaf.rsplit_once('.').unwrap_or((leaf, ""));
    let clean_stem: String = stem
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .map(|c| c.to_ascii_uppercase())
        .collect();
    let clean_ext: String = ext
        .chars()
        .filter(|c| c.is_ascii_alphanumeric() || *c == '_' || *c == '-')
        .take(3)
        .map(|c| c.to_ascii_uppercase())
        .collect();
    if clean_stem.len() <= 8
        && stem.len() == clean_stem.len()
        && ext.len() <= 3
        && !leaf.contains(' ')
    {
        return String::new();
    }
    let suffix = format!("~{}", (ordinal % 9) + 1);
    let prefix_len = 8usize.saturating_sub(suffix.len()).min(clean_stem.len());
    let base = if prefix_len == 0 {
        format!("FILE{suffix}")
    } else {
        format!("{}{suffix}", &clean_stem[..prefix_len])
    };
    if clean_ext.is_empty() {
        base
    } else {
        format!("{base}.{clean_ext}")
    }
}

/// Simple Win32 wildcard matcher (`*` and `?`) used by `FindFirstFileW` inside [`SharedWinEnv`].
#[must_use]
pub fn win32_pat_match(pat: &str, name: &str, case_sensitive: bool) -> bool {
    if pat == "*" || pat == "*.*" {
        return true;
    }
    let p: Vec<char> = if case_sensitive {
        pat.chars().collect()
    } else {
        pat.chars().map(|c| c.to_ascii_uppercase()).collect()
    };
    let s: Vec<char> = if case_sensitive {
        name.chars().collect()
    } else {
        name.chars().map(|c| c.to_ascii_uppercase()).collect()
    };
    let mut dp = vec![false; p.len() + 1];
    dp[0] = true;
    for j in 0..p.len() {
        if p[j] == '*' {
            dp[j + 1] = dp[j];
        }
    }
    for &sc in &s {
        let mut next = vec![false; p.len() + 1];
        for j in 0..p.len() {
            if p[j] == '*' {
                next[j + 1] = next[j] || dp[j + 1];
            } else if p[j] == '?' || p[j] == sc {
                next[j + 1] = dp[j];
            }
        }
        dp = next;
    }
    dp[p.len()]
}

/// Constructs a `ToolError::Emulator` from a 32/64-bit guest PC.
#[must_use]
pub fn emu_err(pc: u64, code: ErrCode, msg: impl Into<String>) -> ToolError {
    let lo32 = u32::try_from(pc & 0xFFFF_FFFF).unwrap_or(0);
    let cs = u16::try_from(lo32 >> 16).unwrap_or(0);
    let ip = u16::try_from(lo32 & 0xFFFF).unwrap_or(0);
    ToolError::Emulator {
        cs,
        ip,
        code,
        message: msg.into(),
    }
}
