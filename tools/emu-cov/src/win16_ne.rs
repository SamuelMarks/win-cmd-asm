//! Deterministic 16-bit Windows New Executable (`NE`) Protected-Mode execution harness.
//!
//! Simulates Windows `1.04`, `2.03`, `2.10/286`, `3.10`, `WfW 3.11`, and `3.2` (`CP936`),
//! enforcing 80286 LDT selector discipline and dispatching `KERNEL`/`USER`/`GDI`/`SHELL`/`TOOLHELP` APIs.

use crate::{DosVm, flags};
use std::collections::{BTreeMap, VecDeque};
use wincmd_diag::{ErrCode, ToolError};

/// Target 16-bit Windows release simulated by the `NE` execution harness.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Win16Era {
    /// Windows `1.04` (1987): tiled top-level windows (`CW_USEDEFAULT`), `LoadModule` child exec.
    Win104,
    /// Windows `2.03` (1987): overlapping windows, `LoadModule` child exec, LIM EMS 4.0 support.
    Win203,
    /// Windows/286 `2.10` (1988): 80286 Standard-Mode Protected Mode (`PROTMODE`).
    Win210_286,
    /// Windows `3.10` (1992): `WinExec`, `SHELL.DLL` drag-drop, `TOOLHELP.DLL` memory inspection.
    Win310,
    /// Windows for Workgroups `3.11` (1993): adds `WNetAddConnection` / `WNetCancelConnection` UNC.
    Wfw311,
    /// Windows `3.2` Simplified Chinese (1994): `CP936` `GB2312` DBCS + `WM_IME_REPORT` IME input.
    Win320Cjk,
}

impl Win16Era {
    /// Returns the raw `AX` word returned by `KERNEL.3` (`GetVersion`): `AL = major, AH = minor`.
    #[must_use]
    pub const fn version_word(self) -> u16 {
        match self {
            Self::Win104 => 0x0401,
            Self::Win203 => 0x0302,
            Self::Win210_286 => 0x0A02,
            Self::Win310 => 0x0A03,
            Self::Wfw311 => 0x0B03,
            Self::Win320Cjk => 0x1403,
        }
    }
}

/// Protected-Mode Local Descriptor Table (`LDT`) entry enforced on every segment load and access.
#[derive(Clone, Copy, Debug)]
pub struct LdtDescriptor {
    /// Linear base byte address inside [`DosVm::mem`].
    pub lin_base: usize,
    /// Inclusive maximum byte offset (`0..=limit`).
    pub limit: usize,
    /// True if `CS` may execute instructions from this selector.
    pub executable: bool,
    /// True if memory writes (`DS:`, `ES:`, `SS:`) are permitted on this selector.
    pub writable: bool,
    /// True if marked `DISCARDABLE` (`0x1000`) in the `NE` segment table.
    pub discardable: bool,
}

/// Queued 16-bit Windows message (`MSG16`) delivered through `GetMessage` / `DispatchMessage`.
#[derive(Clone, Copy, Debug)]
pub struct Win16Msg {
    /// Target window handle (`HWND`).
    pub hwnd: u16,
    /// Message identifier (`WM_*`).
    pub message: u16,
    /// 16-bit word parameter (`wParam`).
    pub wparam: u16,
    /// 32-bit long parameter (`lParam`).
    pub lparam: u32,
}

/// Recorded `CreateWindow` invocation parameters for OS-era window geometry verification.
#[derive(Clone, Debug, Default)]
pub struct CreatedWindowInfo {
    /// Registered window class name.
    pub class_name: String,
    /// Initial window caption string.
    pub title: String,
    /// 32-bit `WS_*` window style flags.
    pub style: u32,
    /// Initial `x` coordinate (`0x8000` = `CW_USEDEFAULT`).
    pub x: u16,
    /// Initial `y` coordinate (`0x8000` = `CW_USEDEFAULT`).
    pub y: u16,
    /// Initial `cx` width (`0x8000` = `CW_USEDEFAULT`).
    pub width: u16,
    /// Initial `cy` height (`0x8000` = `CW_USEDEFAULT`).
    pub height: u16,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Win16Api {
    GetVersion,
    GlobalAlloc,
    GlobalFree,
    GlobalLock,
    GlobalUnlock,
    WaitEvent,
    LoadModule,
    GetModuleHandle,
    GetModuleUsage,
    GetProcAddress,
    MakeProcInstance,
    FreeProcInstance,
    OpenFile,
    LClose,
    LRead,
    LCreat,
    LLSeek,
    LWrite,
    InitTask,
    LoadLibrary,
    SetErrorMode,
    WinExec,
    GetFreeSpace,
    IsDbcsLeadByte,
    InitApp,
    PostQuitMessage,
    GetClientRect,
    SetWindowText,
    BeginPaint,
    EndPaint,
    CreateWindow,
    ShowWindow,
    RegisterClass,
    ScrollWindow,
    SetScrollPos,
    SetScrollRange,
    DefWindowProc,
    GetMessage,
    PeekMessage,
    TranslateMessage,
    DispatchMessage,
    UpdateWindow,
    InvalidateRect,
    OpenClipboard,
    CloseClipboard,
    EmptyClipboard,
    SetClipboardData,
    GetClipboardData,
    LoadMenu,
    CreateCaret,
    DestroyCaret,
    SetCaretPos,
    ShowCaret,
    LoadCursor,
    LoadIcon,
    GetFreeSystemResources,
    WNetGetConnection,
    WNetAddConnection,
    WNetCancelConnection,
    TextOut,
    SelectObject,
    GetStockObject,
    GetTextMetrics,
    DragAcceptFiles,
    DragQueryFile,
    DragFinish,
    MemManInfo,
}

impl Win16Api {
    const ALL: &[Self] = &[
        Self::GetVersion,
        Self::GlobalAlloc,
        Self::GlobalFree,
        Self::GlobalLock,
        Self::GlobalUnlock,
        Self::WaitEvent,
        Self::LoadModule,
        Self::GetModuleHandle,
        Self::GetModuleUsage,
        Self::GetProcAddress,
        Self::MakeProcInstance,
        Self::FreeProcInstance,
        Self::OpenFile,
        Self::LClose,
        Self::LRead,
        Self::LCreat,
        Self::LLSeek,
        Self::LWrite,
        Self::InitTask,
        Self::LoadLibrary,
        Self::SetErrorMode,
        Self::WinExec,
        Self::GetFreeSpace,
        Self::IsDbcsLeadByte,
        Self::InitApp,
        Self::PostQuitMessage,
        Self::GetClientRect,
        Self::SetWindowText,
        Self::BeginPaint,
        Self::EndPaint,
        Self::CreateWindow,
        Self::ShowWindow,
        Self::RegisterClass,
        Self::ScrollWindow,
        Self::SetScrollPos,
        Self::SetScrollRange,
        Self::DefWindowProc,
        Self::GetMessage,
        Self::PeekMessage,
        Self::TranslateMessage,
        Self::DispatchMessage,
        Self::UpdateWindow,
        Self::InvalidateRect,
        Self::OpenClipboard,
        Self::CloseClipboard,
        Self::EmptyClipboard,
        Self::SetClipboardData,
        Self::GetClipboardData,
        Self::LoadMenu,
        Self::CreateCaret,
        Self::DestroyCaret,
        Self::SetCaretPos,
        Self::ShowCaret,
        Self::LoadCursor,
        Self::LoadIcon,
        Self::GetFreeSystemResources,
        Self::WNetGetConnection,
        Self::WNetAddConnection,
        Self::WNetCancelConnection,
        Self::TextOut,
        Self::SelectObject,
        Self::GetStockObject,
        Self::GetTextMetrics,
        Self::DragAcceptFiles,
        Self::DragQueryFile,
        Self::DragFinish,
        Self::MemManInfo,
    ];

    const fn trap_ip(self) -> u16 {
        0x0010 + (self as u16) * 4
    }

    fn from_trap_ip(ip: u16) -> Option<Self> {
        if ip < 0x0010 || !(ip - 0x0010).is_multiple_of(4) {
            return None;
        }
        let idx = usize::from((ip - 0x0010) / 4);
        Self::ALL.get(idx).copied()
    }

    const fn pascal_arg_bytes(self) -> u16 {
        match self {
            Self::GetVersion
            | Self::InitTask
            | Self::CloseClipboard
            | Self::EmptyClipboard
            | Self::DestroyCaret => 0,
            Self::GlobalFree
            | Self::GlobalLock
            | Self::GlobalUnlock
            | Self::WaitEvent
            | Self::GetModuleUsage
            | Self::LClose
            | Self::SetErrorMode
            | Self::GetFreeSpace
            | Self::IsDbcsLeadByte
            | Self::InitApp
            | Self::PostQuitMessage
            | Self::UpdateWindow
            | Self::OpenClipboard
            | Self::GetClipboardData
            | Self::ShowCaret
            | Self::GetFreeSystemResources
            | Self::GetStockObject
            | Self::DragFinish => 2,
            Self::GetModuleHandle
            | Self::FreeProcInstance
            | Self::LoadLibrary
            | Self::ShowWindow
            | Self::RegisterClass
            | Self::TranslateMessage
            | Self::DispatchMessage
            | Self::SetClipboardData
            | Self::SetCaretPos
            | Self::SelectObject
            | Self::DragAcceptFiles
            | Self::MemManInfo => 4,
            Self::GlobalAlloc
            | Self::GetProcAddress
            | Self::MakeProcInstance
            | Self::LCreat
            | Self::WinExec
            | Self::GetClientRect
            | Self::SetWindowText
            | Self::BeginPaint
            | Self::EndPaint
            | Self::LoadMenu
            | Self::LoadCursor
            | Self::LoadIcon
            | Self::WNetCancelConnection
            | Self::GetTextMetrics => 6,
            Self::LoadModule
            | Self::LRead
            | Self::LLSeek
            | Self::LWrite
            | Self::SetScrollPos
            | Self::InvalidateRect
            | Self::CreateCaret
            | Self::WNetGetConnection => 8,
            Self::OpenFile
            | Self::SetScrollRange
            | Self::DefWindowProc
            | Self::GetMessage
            | Self::DragQueryFile => 10,
            Self::PeekMessage | Self::TextOut | Self::WNetAddConnection => 12,
            Self::ScrollWindow => 14,
            Self::CreateWindow => 30,
        }
    }
}

struct SavedCallbackFrame {
    ret_cs: u16,
    ret_ip: u16,
    expected_sp: u16,
}

struct LoadedSegments {
    ldt: BTreeMap<u16, LdtDescriptor>,
    selectors: Vec<u16>,
    bases: Vec<usize>,
    reloc_offs: Vec<Option<usize>>,
    next_lin: usize,
}

/// Complete runtime state for a loaded 16-bit Windows `NE` Protected-Mode session.
pub struct Win16State {
    /// Simulated Windows 1.x / 2.x / 3.x OS release.
    pub era: Win16Era,
    /// Protected-Mode Local Descriptor Table (`selector -> descriptor`).
    pub ldt: BTreeMap<u16, LdtDescriptor>,
    /// Selector of `_DATA` (`DGROUP`).
    pub dgroup_sel: u16,
    /// Selector of the command-line tail block returned by `InitTask`.
    pub cmdline_sel: u16,
    /// Offset of the raw command-line string inside `cmdline_sel`.
    pub cmdline_off: u16,
    /// Initial `ne_heap` byte count from the `NE` header.
    pub heap_size: u16,
    /// Initial `nCmdShow` passed via `InitTask` (`SW_SHOWNORMAL = 1`).
    pub cmd_show: u16,
    /// Resource type IDs (`0x8003` `RT_ICON`, `0x8004` `RT_MENU`, ...) parsed from the `NE` header.
    pub resource_types: Vec<u16>,
    next_global_lin: usize,
    next_global_sel: u16,
    /// Registered `WndProc` `CS:IP` from `RegisterClass`.
    pub wndproc: Option<(u16, u16)>,
    /// Metadata recorded on `CreateWindow`.
    pub created_window: Option<CreatedWindowInfo>,
    /// Current window caption updated by `CreateWindow` / `SetWindowText`.
    pub window_title: String,
    /// Current simulated client-area pixel dimensions `(width, height)`.
    pub client_size: (u16, u16),
    /// Simulated `SetErrorMode` bitmask (`SEM_FAILCRITICALERRORS = 0x0001`).
    pub error_mode: u16,
    /// GDI `TextOut` rows rendered during `WM_PAINT` (`(x, y, bytes)`).
    pub text_out_calls: Vec<(u16, u16, Vec<u8>)>,
    /// Count of `ScrollWindow` calls executed.
    pub scroll_calls: u32,
    /// Latest caret geometry `(width, height, x, y, visible)`.
    pub caret: (u16, u16, u16, u16, bool),
    /// Latest vertical/horizontal scroll positions `(vscroll, hscroll)`.
    pub scroll_pos: (u16, u16),
    /// Global Clipboard memory block bytes (`CF_OEMTEXT` / `CF_TEXT`).
    pub clipboard_bytes: Vec<u8>,
    /// Queued window messages (`WM_*`) awaiting `GetMessage`.
    pub msg_queue: VecDeque<Win16Msg>,
    /// Dropped filenames returned by `SHELL.DLL!DragQueryFile` (`HDROP = 0x0D01`).
    pub dropped_files: Vec<String>,
    /// Active `WfW` 3.11 network UNC connections (`"Z:" -> "\SERVER\SHARE"`).
    pub unc_connections: BTreeMap<String, String>,
    /// External module launch log (`(api_name, cmdline)` for `WinExec` / `LoadModule`).
    pub launched_modules: Vec<(&'static str, String)>,
    /// Quit code posted via `PostQuitMessage`.
    pub quit_posted: Option<u8>,
    callback_stack: Vec<SavedCallbackFrame>,
    next_hfile: u16,
}

impl Win16State {
    /// Protected-Mode code selector hosting Win16 API trap entrypoints.
    pub const THUNK_CS_SEL: u16 = 0xF007;
    /// Special `IP` inside [`Self::THUNK_CS_SEL`] jumped to when `WndProc` executes `RETF 10`.
    pub const CALLBACK_RET_IP: u16 = 0x0FF0;
    /// Simulated `HINSTANCE` handle of the loaded `NE` task.
    pub const HINSTANCE: u16 = 0x1001;
    /// Simulated `HWND` returned by `CreateWindow`.
    pub const HWND_MAIN: u16 = 0x2001;
    /// Increment between consecutive Protected-Mode huge-block selectors (`KERNEL.114` `__AHINCR`).
    pub const AHINCR: u16 = 0x0008;
}

const fn read_le16(buf: &[u8], off: usize) -> u16 {
    u16::from_le_bytes([buf[off], buf[off + 1]])
}

fn parse_ne_mod_names(image: &[u8], lfanew: usize) -> Vec<String> {
    let ne_cmod = usize::from(read_le16(image, lfanew + 0x1E));
    let ne_modtab = usize::from(read_le16(image, lfanew + 0x28));
    let ne_imptab = usize::from(read_le16(image, lfanew + 0x2A));
    let mut mod_names = Vec::with_capacity(ne_cmod);
    for m_idx in 0..ne_cmod {
        let imp_rel = usize::from(read_le16(image, lfanew + ne_modtab + m_idx * 2));
        let name_pos = lfanew + ne_imptab + imp_rel;
        let nlen = usize::from(image[name_pos]);
        let name =
            String::from_utf8_lossy(&image[name_pos + 1..name_pos + 1 + nlen]).to_ascii_uppercase();
        mod_names.push(name);
    }
    mod_names
}

fn parse_ne_rsrc_types(image: &[u8], lfanew: usize) -> Vec<u16> {
    let ne_rsrctab = usize::from(read_le16(image, lfanew + 0x24));
    let ne_restab = usize::from(read_le16(image, lfanew + 0x26));
    let mut types = Vec::new();
    if ne_rsrctab < ne_restab && lfanew + ne_rsrctab + 4 <= image.len() {
        let mut rpos = lfanew + ne_rsrctab + 2;
        while rpos + 8 <= lfanew + ne_restab && rpos + 2 <= image.len() {
            let rt_id = read_le16(image, rpos);
            if rt_id == 0 {
                break;
            }
            let rt_count = usize::from(read_le16(image, rpos + 2));
            types.push(rt_id);
            rpos += 8 + rt_count * 12;
        }
    }
    types
}

impl DosVm {
    /// Loads a 16-bit Windows `NE` executable (`WINCMD16.EXE` or `WINCMD286.EXE`) into strict
    /// 80286 Protected Mode with non-contiguous LDT selectors, parses `NE` resources and
    /// `NE_RELOC_ITEM` fixups, and prepares `CS:IP` / `SS:SP` / `DS` for `InitTask` startup.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] if the `MZ`/`NE` headers, segment table, or relocations
    /// are malformed.
    pub fn load_ne(
        &mut self,
        image: &[u8],
        era: Win16Era,
        cmdline: &str,
    ) -> Result<(), ToolError> {
        let lfanew = validate_mz_ne(image)?;
        let ne_autodata = usize::from(read_le16(image, lfanew + 0x0E));
        let ne_heap = read_le16(image, lfanew + 0x10);
        let entry_ip = read_le16(image, lfanew + 0x14);
        let entry_seg = usize::from(read_le16(image, lfanew + 0x16));
        let mut init_sp = read_le16(image, lfanew + 0x18);
        let init_ss_seg = usize::from(read_le16(image, lfanew + 0x1A));

        self.cpu_286_enabled = (image[lfanew + 0x37] & 0x02) != 0;
        if era == Win16Era::Win320Cjk {
            self.active_cp = 936;
            self.refresh_rom_dbcs_table();
        }

        let mod_names = parse_ne_mod_names(image, lfanew);
        let resource_types = parse_ne_rsrc_types(image, lfanew);
        let mut segs = self.map_ne_segments(image, lfanew);
        self.apply_ne_relocs(image, &segs, &mod_names)?;

        let cmdline_sel = self.install_ne_cmdline_and_thunks(cmdline, &mut segs);
        let dgroup_sel = segs
            .selectors
            .get(ne_autodata.saturating_sub(1))
            .copied()
            .unwrap_or(segs.selectors[0]);
        let ss_sel = if init_ss_seg > 0 {
            segs.selectors
                .get(init_ss_seg - 1)
                .copied()
                .unwrap_or(dgroup_sel)
        } else {
            dgroup_sel
        };
        if init_sp == 0
            && let Some(d_desc) = segs.ldt.get(&ss_sel)
        {
            init_sp = u16::try_from((d_desc.limit + 1).min(0xFFFE) & !1).unwrap_or(0x6000);
        }

        self.sregs[Self::CS] = segs
            .selectors
            .get(entry_seg.saturating_sub(1))
            .copied()
            .unwrap_or(segs.selectors[0]);
        self.ip = entry_ip;
        self.sregs[Self::DS] = dgroup_sel;
        self.sregs[Self::SS] = ss_sel;
        self.regs[4] = init_sp;
        self.sregs[Self::ES] = cmdline_sel;
        self.regs[1] = ne_heap;
        self.regs[3] = 0x0081;
        self.regs[6] = 0;
        self.regs[7] = Win16State::HINSTANCE;

        self.pmode = Some(Win16State {
            era,
            ldt: segs.ldt,
            dgroup_sel,
            cmdline_sel,
            cmdline_off: 0x0081,
            heap_size: ne_heap,
            cmd_show: 1,
            resource_types,
            next_global_lin: segs.next_lin.max(0x42000),
            next_global_sel: 0x2007,
            wndproc: None,
            created_window: None,
            window_title: String::new(),
            client_size: (640, 300),
            error_mode: 0,
            text_out_calls: Vec::new(),
            scroll_calls: 0,
            caret: (0, 0, 0, 0, false),
            scroll_pos: (0, 0),
            clipboard_bytes: Vec::new(),
            msg_queue: VecDeque::new(),
            dropped_files: Vec::new(),
            unc_connections: BTreeMap::new(),
            launched_modules: Vec::new(),
            quit_posted: None,
            callback_stack: Vec::new(),
            next_hfile: 10,
        });
        self.gp_fault = None;
        self.halted = false;
        self.exit_code = 0;
        Ok(())
    }

    fn map_ne_segments(&mut self, image: &[u8], lfanew: usize) -> LoadedSegments {
        let ne_autodata = usize::from(read_le16(image, lfanew + 0x0E));
        let ne_heap = read_le16(image, lfanew + 0x10);
        let ne_stack = read_le16(image, lfanew + 0x12);
        let ne_cseg = usize::from(read_le16(image, lfanew + 0x1C));
        let ne_segtab = usize::from(read_le16(image, lfanew + 0x22));
        let align_shift = {
            let raw = read_le16(image, lfanew + 0x32);
            u32::from(if raw == 0 { 9 } else { raw })
        };

        let mut ldt = BTreeMap::new();
        let mut selectors = Vec::with_capacity(ne_cseg);
        let mut bases = Vec::with_capacity(ne_cseg);
        let mut reloc_offs = Vec::with_capacity(ne_cseg);
        let mut next_lin = 0x10000_usize;

        for s_idx in 0..ne_cseg {
            let s_off = lfanew + ne_segtab + s_idx * 8;
            let sec_off = usize::from(read_le16(image, s_off));
            let raw_u16 = read_le16(image, s_off + 2);
            let s_flags = read_le16(image, s_off + 4);
            let min_u16 = read_le16(image, s_off + 6);
            let raw_len = if sec_off == 0 {
                0
            } else if raw_u16 == 0 {
                65_536
            } else {
                usize::from(raw_u16)
            };
            let mut alloc_len = if min_u16 == 0 {
                65_536
            } else {
                usize::from(min_u16)
            };
            if s_idx + 1 == ne_autodata {
                alloc_len = alloc_len
                    .saturating_add(usize::from(ne_heap))
                    .saturating_add(usize::from(ne_stack))
                    .min(65_536);
            }
            alloc_len = alloc_len.max(raw_len).max(16);

            let sel = 0x1007_u16 + u16::try_from(s_idx).unwrap_or(0) * 0x0010;
            let file_off = sec_off << align_shift;
            if raw_len > 0 && file_off + raw_len <= image.len() {
                self.mem[next_lin..next_lin + raw_len]
                    .copy_from_slice(&image[file_off..file_off + raw_len]);
            }
            let is_data = (s_flags & 0x0001) != 0;
            ldt.insert(
                sel,
                LdtDescriptor {
                    lin_base: next_lin,
                    limit: alloc_len - 1,
                    executable: !is_data,
                    writable: is_data,
                    discardable: (s_flags & 0x1000) != 0,
                },
            );
            selectors.push(sel);
            bases.push(next_lin);
            reloc_offs.push(if (s_flags & 0x0100) != 0 && raw_len > 0 {
                Some(file_off + raw_len)
            } else {
                None
            });
            next_lin = (next_lin + alloc_len + 0x0FFF) & !0x0FFF;
        }
        LoadedSegments {
            ldt,
            selectors,
            bases,
            reloc_offs,
            next_lin,
        }
    }

    fn apply_ne_relocs(
        &mut self,
        image: &[u8],
        segs: &LoadedSegments,
        mod_names: &[String],
    ) -> Result<(), ToolError> {
        for (s_idx, maybe_reloc) in segs.reloc_offs.iter().enumerate() {
            let Some(reloc_pos) = *maybe_reloc else {
                continue;
            };
            if reloc_pos + 2 > image.len() {
                continue;
            }
            let n_relocs = usize::from(read_le16(image, reloc_pos));
            let seg_lin = segs.bases[s_idx];
            for r_idx in 0..n_relocs {
                let rp = reloc_pos + 2 + r_idx * 8;
                if rp + 8 > image.len() {
                    break;
                }
                let addr_type = image[rp];
                let reloc_type = image[rp + 1] & 0x03;
                let mut patch_off = usize::from(read_le16(image, rp + 2));
                let target1 = usize::from(read_le16(image, rp + 4));
                let target2 = read_le16(image, rp + 6);

                let (val_off, val_sel) = match reloc_type {
                    0 => {
                        let t_sel = segs
                            .selectors
                            .get(target1.saturating_sub(1))
                            .copied()
                            .unwrap_or(0);
                        (target2, t_sel)
                    }
                    1 => {
                        let mname = mod_names
                            .get(target1.saturating_sub(1))
                            .map_or("", String::as_str);
                        if mname == "KERNEL" && target2 == 114 {
                            (Win16State::AHINCR, 0)
                        } else {
                            let api = resolve_ordinal_import(mname, target2).ok_or_else(|| {
                                ToolError::Emulator {
                                    cs: segs.selectors[s_idx],
                                    ip: u16::try_from(patch_off).unwrap_or(0),
                                    code: ErrCode::InvalidFunction,
                                    message: format!("unresolved Win16 NE import {mname}.{target2}"),
                                }
                            })?;
                            (api.trap_ip(), Win16State::THUNK_CS_SEL)
                        }
                    }
                    _ => continue,
                };

                for _ in 0..64 {
                    let prev_chain = self.read_u16_lin(seg_lin + patch_off);
                    match addr_type {
                        2 => {
                            let v = if val_sel != 0 { val_sel } else { val_off };
                            self.write_u16_lin(seg_lin + patch_off, v);
                        }
                        3 => {
                            self.write_u16_lin(seg_lin + patch_off, val_off);
                            self.write_u16_lin(seg_lin + patch_off + 2, val_sel);
                        }
                        5 => self.write_u16_lin(seg_lin + patch_off, val_off),
                        _ => {}
                    }
                    if prev_chain == 0xFFFF || prev_chain == 0x0000 {
                        break;
                    }
                    patch_off = usize::from(prev_chain);
                }
            }
        }
        Ok(())
    }

    fn install_ne_cmdline_and_thunks(&mut self, cmdline: &str, segs: &mut LoadedSegments) -> u16 {
        let cmdline_sel = 0x10F7_u16;
        let cmdline_lin = segs.next_lin;
        let cmd_bytes = cmdline.as_bytes();
        let copy_len = cmd_bytes.len().min(250);
        self.mem[cmdline_lin + 0x80] = u8::try_from(copy_len).unwrap_or(0);
        self.mem[cmdline_lin + 0x81..cmdline_lin + 0x81 + copy_len]
            .copy_from_slice(&cmd_bytes[..copy_len]);
        self.mem[cmdline_lin + 0x81 + copy_len] = 0x00;
        segs.ldt.insert(
            cmdline_sel,
            LdtDescriptor {
                lin_base: cmdline_lin,
                limit: 0x01FF,
                executable: false,
                writable: false,
                discardable: false,
            },
        );
        segs.next_lin = (cmdline_lin + 0x1000) & !0x0FFF;

        let thunk_lin = 0xF1000_usize;
        for api in Win16Api::ALL {
            let ip = usize::from(api.trap_ip());
            let arg_b = api.pascal_arg_bytes();
            if arg_b == 0 {
                self.mem[thunk_lin + ip] = 0xCB;
            } else {
                self.mem[thunk_lin + ip] = 0xCA;
                self.mem[thunk_lin + ip + 1] = (arg_b & 0xFF) as u8;
                self.mem[thunk_lin + ip + 2] = (arg_b >> 8) as u8;
            }
        }
        self.mem[thunk_lin + usize::from(Win16State::CALLBACK_RET_IP)] = 0x90;
        segs.ldt.insert(
            Win16State::THUNK_CS_SEL,
            LdtDescriptor {
                lin_base: thunk_lin,
                limit: 0x0FFF,
                executable: true,
                writable: false,
                discardable: false,
            },
        );
        segs.ldt.insert(
            Self::ROM_SEG,
            LdtDescriptor {
                lin_base: usize::from(Self::ROM_SEG) << 4,
                limit: 0xFFFF,
                executable: true,
                writable: false,
                discardable: false,
            },
        );
        cmdline_sel
    }

    /// Enqueues a 16-bit Windows message (`WM_*`) to be delivered to the loaded `NE` task.
    pub fn push_win16_msg(&mut self, message: u16, wparam: u16, lparam: u32) {
        if let Some(pm) = &mut self.pmode {
            pm.msg_queue.push_back(Win16Msg {
                hwnd: Win16State::HWND_MAIN,
                message,
                wparam,
                lparam,
            });
        }
    }

    /// Validates a Protected-Mode selector load into `sreg_idx` (`ES`, `CS`, `SS`, `DS`).
    pub(crate) fn validate_pmode_sreg(
        &self,
        sreg_idx: usize,
        sel: u16,
    ) -> Result<(), ToolError> {
        let Some(pm) = &self.pmode else {
            return Ok(());
        };
        if sel == 0 && (sreg_idx == Self::ES || sreg_idx == Self::DS) {
            return Ok(());
        }
        let Some(desc) = pm.ldt.get(&sel) else {
            return Err(ToolError::Emulator {
                cs: self.sregs[Self::CS],
                ip: self.ip,
                code: ErrCode::AccessDenied,
                message: format!(
                    "#GP(0x{sel:04X}): illegal Protected-Mode selector loaded into segment register {sreg_idx}"
                ),
            });
        };
        if sreg_idx == Self::CS && !desc.executable {
            return Err(ToolError::Emulator {
                cs: sel,
                ip: self.ip,
                code: ErrCode::AccessDenied,
                message: format!(
                    "#GP(0x{sel:04X}): attempted code execution from non-executable data selector"
                ),
            });
        }
        if sreg_idx == Self::SS && !desc.writable {
            return Err(ToolError::Emulator {
                cs: self.sregs[Self::CS],
                ip: self.ip,
                code: ErrCode::AccessDenied,
                message: format!("#GP(0x{sel:04X}): non-writable SS selector"),
            });
        }
        Ok(())
    }

    /// Resolves `seg:off` through the Protected-Mode LDT when `pmode` is active.
    pub(crate) fn resolve_pmode_or_real(&mut self, seg: u16, off: u16, is_write: bool) -> usize {
        let Some(pm) = &self.pmode else {
            return Self::lin(seg, off);
        };
        match pm.ldt.get(&seg) {
            None => {
                if self.gp_fault.is_none() {
                    self.gp_fault = Some(format!(
                        "#GP(0x{seg:04X}): memory access through unallocated Protected-Mode selector"
                    ));
                }
                0
            }
            Some(desc) => {
                if usize::from(off) > desc.limit {
                    if self.gp_fault.is_none() {
                        self.gp_fault = Some(format!(
                            "#GP(0x{seg:04X}:0x{off:04X}): offset exceeds segment limit 0x{:04X}",
                            desc.limit
                        ));
                    }
                    0
                } else if is_write && !desc.writable {
                    if self.gp_fault.is_none() {
                        self.gp_fault = Some(format!(
                            "#GP(0x{seg:04X}:0x{off:04X}): illegal write into read-only/code selector"
                        ));
                    }
                    0
                } else {
                    (desc.lin_base + usize::from(off)) % crate::MEM_SIZE
                }
            }
        }
    }

    fn read_far_arg_u16(&self, byte_off_from_ret: u16) -> u16 {
        let ss = self.sregs[Self::SS];
        let sp = self.regs[4].wrapping_add(4).wrapping_add(byte_off_from_ret);
        self.read_u16(ss, sp)
    }

    fn read_far_arg_u32(&self, byte_off_from_ret: u16) -> u32 {
        let lo = u32::from(self.read_far_arg_u16(byte_off_from_ret));
        let hi = u32::from(self.read_far_arg_u16(byte_off_from_ret + 2));
        lo | (hi << 16)
    }

    fn invoke_guest_wndproc(&mut self, msg: Win16Msg) -> Result<(), ToolError> {
        let Some((wnd_cs, wnd_ip)) = self.pmode.as_ref().and_then(|p| p.wndproc) else {
            return Ok(());
        };
        let ret_ip = self.pop16();
        let ret_cs = self.pop16();
        let caller_arg_bytes =
            Win16Api::from_trap_ip(self.ip).map_or(0, Win16Api::pascal_arg_bytes);
        self.regs[4] = self.regs[4].wrapping_add(caller_arg_bytes);
        let expected_sp = self.regs[4];
        if let Some(pm) = &mut self.pmode {
            pm.callback_stack.push(SavedCallbackFrame {
                ret_cs,
                ret_ip,
                expected_sp,
            });
        }
        self.push16(msg.hwnd);
        self.push16(msg.message);
        self.push16(msg.wparam);
        self.push16((msg.lparam >> 16) as u16);
        self.push16((msg.lparam & 0xFFFF) as u16);
        self.push16(Win16State::THUNK_CS_SEL);
        self.push16(Win16State::CALLBACK_RET_IP);
        self.validate_pmode_sreg(Self::CS, wnd_cs)?;
        self.sregs[Self::CS] = wnd_cs;
        self.ip = wnd_ip;
        Ok(())
    }

    /// Handles execution at [`Win16State::THUNK_CS_SEL`].
    pub(crate) fn handle_win16_thunk(&mut self, trap_ip: u16) -> Result<bool, ToolError> {
        if trap_ip == Win16State::CALLBACK_RET_IP {
            let frame = self
                .pmode
                .as_mut()
                .and_then(|pm| pm.callback_stack.pop())
                .ok_or_else(|| ToolError::Emulator {
                    cs: Win16State::THUNK_CS_SEL,
                    ip: trap_ip,
                    code: ErrCode::GeneralFailure,
                    message: "corrupted Win16 WndProc callback return stack".into(),
                })?;
            if self.regs[4] != frame.expected_sp {
                return Err(ToolError::Emulator {
                    cs: frame.ret_cs,
                    ip: frame.ret_ip,
                    code: ErrCode::GeneralFailure,
                    message: format!(
                        "WndProc RETF 10 stack mismatch: SP=0x{:04X}, expected 0x{:04X}",
                        self.regs[4], frame.expected_sp
                    ),
                });
            }
            self.sregs[Self::CS] = frame.ret_cs;
            self.ip = frame.ret_ip;
            return Ok(true);
        }

        let Some(api) = Win16Api::from_trap_ip(trap_ip) else {
            return Ok(false);
        };
        if self.handle_win16_kernel(api) || self.handle_win16_vfs(api) {
            return Ok(false);
        }
        self.handle_win16_user_gdi(api)
    }

    fn handle_win16_kernel(&mut self, api: Win16Api) -> bool {
        match api {
            Win16Api::InitTask => {
                if let Some(pm) = &self.pmode {
                    self.regs[0] = 1;
                    self.regs[1] = pm.heap_size;
                    self.regs[2] = pm.cmd_show;
                    self.regs[3] = pm.cmdline_off;
                    self.sregs[Self::ES] = pm.cmdline_sel;
                    self.regs[6] = 0;
                    self.regs[7] = Win16State::HINSTANCE;
                }
                self.flags &= !flags::CF;
            }
            Win16Api::WaitEvent | Win16Api::InitApp | Win16Api::FreeProcInstance => {
                self.regs[0] = 1;
            }
            Win16Api::GetVersion => {
                if let Some(pm) = &self.pmode {
                    self.regs[0] = pm.era.version_word();
                }
            }
            Win16Api::SetErrorMode => {
                let new_mode = self.read_far_arg_u16(0);
                if let Some(pm) = &mut self.pmode {
                    let prev = pm.error_mode;
                    pm.error_mode = new_mode;
                    self.regs[0] = prev;
                }
            }
            Win16Api::GlobalAlloc => self.win16_global_alloc(),
            Win16Api::GlobalLock => {
                let hmem = self.read_far_arg_u16(0);
                let valid = self.pmode.as_ref().is_some_and(|pm| pm.ldt.contains_key(&hmem));
                self.regs[0] = 0;
                self.regs[2] = if valid { hmem } else { 0 };
            }
            Win16Api::GlobalUnlock | Win16Api::GlobalFree | Win16Api::GetModuleUsage => {
                self.regs[0] = 0;
            }
            Win16Api::MakeProcInstance => {
                self.regs[0] = self.read_far_arg_u16(2);
                self.regs[2] = self.read_far_arg_u16(4);
            }
            Win16Api::GetModuleHandle | Win16Api::LoadLibrary => self.win16_get_mod_handle(),
            Win16Api::GetProcAddress => self.win16_get_proc_address(),
            Win16Api::LoadModule => {
                let name = self.read_asciiz(self.read_far_arg_u16(6), self.read_far_arg_u16(4));
                if let Some(pm) = &mut self.pmode {
                    pm.launched_modules.push(("LoadModule", name));
                }
                self.regs[0] = 33;
            }
            Win16Api::WinExec => {
                let cmd = self.read_asciiz(self.read_far_arg_u16(4), self.read_far_arg_u16(2));
                if let Some(pm) = &mut self.pmode {
                    pm.launched_modules.push(("WinExec", cmd));
                }
                self.regs[0] = 33;
            }
            Win16Api::GetFreeSpace => {
                self.regs[0] = 0x0000;
                self.regs[2] = 0x0008;
            }
            Win16Api::GetFreeSystemResources => self.regs[0] = 86,
            Win16Api::MemManInfo => {
                let off = self.read_far_arg_u16(0);
                let sel = self.read_far_arg_u16(2);
                self.write_u16(sel, off.wrapping_add(4), 0x0000);
                self.write_u16(sel, off.wrapping_add(6), 0x0020);
                self.regs[0] = 1;
            }
            Win16Api::IsDbcsLeadByte => {
                let ch = (self.read_far_arg_u16(0) & 0xFF) as u8;
                let is_cjk = self
                    .pmode
                    .as_ref()
                    .is_some_and(|pm| pm.era == Win16Era::Win320Cjk);
                self.regs[0] = u16::from(is_cjk && (0x81..=0xFE).contains(&ch));
            }
            _ => return false,
        }
        true
    }

    fn win16_global_alloc(&mut self) {
        let dw_bytes = usize::try_from(self.read_far_arg_u32(0))
            .unwrap_or(0)
            .max(16);
        let w_flags = self.read_far_arg_u16(4);
        let tiles = dw_bytes.div_ceil(65_536).max(1);
        if let Some(pm) = &mut self.pmode {
            let base_sel = pm.next_global_sel;
            let base_lin = pm.next_global_lin;
            if base_lin + tiles * 65_536 <= crate::MEM_SIZE {
                if (w_flags & 0x0040) != 0 {
                    self.mem[base_lin..base_lin + dw_bytes].fill(0);
                }
                for t in 0..tiles {
                    let sel = base_sel + u16::try_from(t).unwrap_or(0) * Win16State::AHINCR;
                    let lim = dw_bytes.saturating_sub(t * 65_536).min(65_536) - 1;
                    pm.ldt.insert(
                        sel,
                        LdtDescriptor {
                            lin_base: base_lin + t * 65_536,
                            limit: lim,
                            executable: false,
                            writable: true,
                            discardable: false,
                        },
                    );
                }
                pm.next_global_sel =
                    base_sel + u16::try_from(tiles).unwrap_or(1) * Win16State::AHINCR;
                pm.next_global_lin = (base_lin + dw_bytes + 0x0FFF) & !0x0FFF;
                self.regs[0] = base_sel;
            } else {
                self.regs[0] = 0;
            }
        }
    }

    fn win16_get_mod_handle(&mut self) {
        let name = self
            .read_asciiz(self.read_far_arg_u16(2), self.read_far_arg_u16(0))
            .to_ascii_uppercase();
        let era = self.pmode.as_ref().map_or(Win16Era::Win104, |p| p.era);
        let is_3x = matches!(era, Win16Era::Win310 | Win16Era::Wfw311 | Win16Era::Win320Cjk);
        self.regs[0] = match name.trim_end_matches(".DLL").trim_end_matches(".DRV") {
            "KERNEL" => 1,
            "USER" => 2,
            "GDI" => 3,
            "SHELL" if is_3x => 4,
            "TOOLHELP" if is_3x => 5,
            "WFWNET" if era == Win16Era::Wfw311 => 6,
            "WINNLS" if era == Win16Era::Win320Cjk => 7,
            _ => 0,
        };
    }

    fn win16_get_proc_address(&mut self) {
        let name_off = self.read_far_arg_u16(0);
        let name_sel = self.read_far_arg_u16(2);
        let hmod = self.read_far_arg_u16(4);
        let era = self.pmode.as_ref().map_or(Win16Era::Win104, |p| p.era);
        let resolved = if name_sel == 0 {
            resolve_dynamic_ordinal(hmod, name_off, era)
        } else {
            let sym = self.read_asciiz(name_sel, name_off).to_ascii_uppercase();
            resolve_dynamic_name(hmod, &sym, era)
        };
        if let Some(target_api) = resolved {
            self.regs[0] = target_api.trap_ip();
            self.regs[2] = Win16State::THUNK_CS_SEL;
        } else {
            self.regs[0] = 0;
            self.regs[2] = 0;
        }
    }

    fn handle_win16_vfs(&mut self, api: Win16Api) -> bool {
        match api {
            Win16Api::OpenFile => {
                let style = self.read_far_arg_u16(0);
                let raw_name = self.read_asciiz(self.read_far_arg_u16(8), self.read_far_arg_u16(6));
                let key = self.norm_vfs_key(&raw_name);
                if (style & 0x1000) != 0 {
                    self.vfs.insert(key.clone(), Vec::new());
                    self.regs[0] = self.alloc_win16_hfile(&key, 0);
                } else if (style & 0x0200) != 0 {
                    self.regs[0] = if self.vfs.remove(&key).is_some() {
                        1
                    } else {
                        0xFFFF
                    };
                } else if self.vfs.contains_key(&key) {
                    self.regs[0] = self.alloc_win16_hfile(&key, 0);
                } else {
                    self.regs[0] = 0xFFFF;
                }
            }
            Win16Api::LCreat => {
                let raw_name = self.read_asciiz(self.read_far_arg_u16(4), self.read_far_arg_u16(2));
                let key = self.norm_vfs_key(&raw_name);
                self.vfs.insert(key.clone(), Vec::new());
                self.regs[0] = self.alloc_win16_hfile(&key, 0);
            }
            Win16Api::LRead => {
                let count = usize::from(self.read_far_arg_u16(0));
                let buf_off = self.read_far_arg_u16(2);
                let buf_sel = self.read_far_arg_u16(4);
                let hf = self.read_far_arg_u16(6);
                let chunk = if let Some(of) = self.handles.get_mut(&hf)
                    && let Some(data) = self.vfs.get(&of.path)
                {
                    let avail = data.len().saturating_sub(of.pos).min(count);
                    let slice = data[of.pos..of.pos + avail].to_vec();
                    of.pos += avail;
                    Some(slice)
                } else {
                    None
                };
                if let Some(bytes) = chunk {
                    for (i, &b) in bytes.iter().enumerate() {
                        self.write_u8(
                            buf_sel,
                            buf_off.wrapping_add(u16::try_from(i).unwrap_or(0)),
                            b,
                        );
                    }
                    self.regs[0] = u16::try_from(bytes.len()).unwrap_or(0);
                } else {
                    self.regs[0] = 0xFFFF;
                }
            }
            Win16Api::LWrite => self.win16_lwrite(),
            Win16Api::LLSeek => self.win16_llseek(),
            Win16Api::LClose => {
                let hf = self.read_far_arg_u16(0);
                self.handles.remove(&hf);
                self.regs[0] = 0;
            }
            _ => return false,
        }
        true
    }

    fn win16_lwrite(&mut self) {
        let count = usize::from(self.read_far_arg_u16(0));
        let buf_off = self.read_far_arg_u16(2);
        let buf_sel = self.read_far_arg_u16(4);
        let hf = self.read_far_arg_u16(6);
        let mut tmp = Vec::with_capacity(count);
        for i in 0..count {
            tmp.push(self.read_u8(
                buf_sel,
                buf_off.wrapping_add(u16::try_from(i).unwrap_or(0)),
            ));
        }
        if let Some(of) = self.handles.get_mut(&hf) {
            let path = of.path.clone();
            let pos = of.pos;
            let entry = self.vfs.entry(path).or_default();
            if count == 0 {
                entry.truncate(pos);
            } else {
                if entry.len() < pos + count {
                    entry.resize(pos + count, 0);
                }
                entry[pos..pos + count].copy_from_slice(&tmp);
            }
            if let Some(of2) = self.handles.get_mut(&hf) {
                of2.pos = pos + count;
            }
            self.regs[0] = u16::try_from(count).unwrap_or(0);
        } else {
            self.regs[0] = 0xFFFF;
        }
    }

    fn win16_llseek(&mut self) {
        let origin = self.read_far_arg_u16(0);
        let off_i32 = self.read_far_arg_u32(2).cast_signed();
        let hf = self.read_far_arg_u16(6);
        if let Some(of) = self.handles.get_mut(&hf) {
            let flen = self.vfs.get(&of.path).map_or(0, Vec::len);
            let base = match origin {
                1 => i64::try_from(of.pos).unwrap_or(0),
                2 => i64::try_from(flen).unwrap_or(0),
                _ => 0,
            };
            let new_pos = usize::try_from((base + i64::from(off_i32)).max(0)).unwrap_or(0);
            of.pos = new_pos;
            let pos_u32 = u32::try_from(new_pos).unwrap_or(0);
            self.regs[0] = (pos_u32 & 0xFFFF) as u16;
            self.regs[2] = (pos_u32 >> 16) as u16;
        } else {
            self.regs[0] = 0xFFFF;
            self.regs[2] = 0xFFFF;
        }
    }

    fn handle_win16_user_gdi(&mut self, api: Win16Api) -> Result<bool, ToolError> {
        if let Some(jumped) = self.handle_win16_window_msg(api)? {
            return Ok(jumped);
        }
        match api {
            Win16Api::LoadIcon | Win16Api::LoadCursor | Win16Api::LoadMenu => self.regs[0] = 0x3001,
            Win16Api::ShowWindow
            | Win16Api::InvalidateRect
            | Win16Api::TranslateMessage
            | Win16Api::EndPaint
            | Win16Api::SetScrollRange
            | Win16Api::DragAcceptFiles
            | Win16Api::DragFinish => self.regs[0] = 1,
            Win16Api::DefWindowProc => {
                self.regs[0] = 0;
                self.regs[2] = 0;
            }
            Win16Api::RegisterClass => {
                let wc_off = self.read_far_arg_u16(0);
                let wc_sel = self.read_far_arg_u16(2);
                let wnd_ip = self.read_u16(wc_sel, wc_off.wrapping_add(2));
                let wnd_cs = self.read_u16(wc_sel, wc_off.wrapping_add(4));
                if let Some(pm) = &mut self.pmode {
                    pm.wndproc = Some((wnd_cs, wnd_ip));
                }
                self.regs[0] = 0xC001;
            }
            Win16Api::CreateWindow => self.win16_create_window(),
            Win16Api::GetClientRect => {
                let rc_off = self.read_far_arg_u16(0);
                let rc_sel = self.read_far_arg_u16(2);
                let (cw, ch) = self.pmode.as_ref().map_or((640, 300), |p| p.client_size);
                self.write_u16(rc_sel, rc_off, 0);
                self.write_u16(rc_sel, rc_off.wrapping_add(2), 0);
                self.write_u16(rc_sel, rc_off.wrapping_add(4), cw);
                self.write_u16(rc_sel, rc_off.wrapping_add(6), ch);
            }
            Win16Api::SetWindowText => {
                let txt = self.read_asciiz(self.read_far_arg_u16(2), self.read_far_arg_u16(0));
                if let Some(pm) = &mut self.pmode {
                    pm.window_title = txt;
                }
                self.regs[0] = 1;
            }
            Win16Api::BeginPaint => {
                self.write_u16(self.read_far_arg_u16(2), self.read_far_arg_u16(0), 0x4001);
                self.regs[0] = 0x4001;
            }
            Win16Api::ScrollWindow => {
                if let Some(pm) = &mut self.pmode {
                    pm.scroll_calls = pm.scroll_calls.saturating_add(1);
                }
                self.regs[0] = 1;
            }
            Win16Api::SetScrollPos => {
                let pos = self.read_far_arg_u16(2);
                let bar = self.read_far_arg_u16(4);
                if let Some(pm) = &mut self.pmode {
                    if bar == 1 {
                        pm.scroll_pos.0 = pos;
                    } else {
                        pm.scroll_pos.1 = pos;
                    }
                }
                self.regs[0] = pos;
            }
            _ => self.handle_win16_gdi_clip_net(api),
        }
        Ok(false)
    }

    fn handle_win16_window_msg(&mut self, api: Win16Api) -> Result<Option<bool>, ToolError> {
        match api {
            Win16Api::UpdateWindow => {
                self.regs[0] = 1;
                self.invoke_guest_wndproc(Win16Msg {
                    hwnd: Win16State::HWND_MAIN,
                    message: 0x000F,
                    wparam: 0,
                    lparam: 0,
                })?;
                Ok(Some(true))
            }
            Win16Api::DispatchMessage => {
                let msg_off = self.read_far_arg_u16(0);
                let msg_sel = self.read_far_arg_u16(2);
                let hwnd = self.read_u16(msg_sel, msg_off);
                let message = self.read_u16(msg_sel, msg_off.wrapping_add(2));
                let wparam = self.read_u16(msg_sel, msg_off.wrapping_add(4));
                let llo = u32::from(self.read_u16(msg_sel, msg_off.wrapping_add(6)));
                let lhi = u32::from(self.read_u16(msg_sel, msg_off.wrapping_add(8)));
                self.regs[0] = 0;
                self.regs[2] = 0;
                self.invoke_guest_wndproc(Win16Msg {
                    hwnd,
                    message,
                    wparam,
                    lparam: llo | (lhi << 16),
                })?;
                Ok(Some(true))
            }
            Win16Api::GetMessage => {
                let msg_off = self.read_far_arg_u16(6);
                let msg_sel = self.read_far_arg_u16(8);
                let next_msg = if let Some(pm) = &mut self.pmode {
                    if let Some(code) = pm.quit_posted {
                        self.exit_code = code;
                        None
                    } else {
                        pm.msg_queue.pop_front()
                    }
                } else {
                    None
                };
                if let Some(m) = next_msg {
                    self.write_msg_struct(msg_sel, msg_off, m);
                    self.regs[0] = u16::from(m.message != 0x0012);
                } else {
                    self.write_msg_struct(
                        msg_sel,
                        msg_off,
                        Win16Msg {
                            hwnd: Win16State::HWND_MAIN,
                            message: 0x0012,
                            wparam: u16::from(self.exit_code),
                            lparam: 0,
                        },
                    );
                    self.regs[0] = 0;
                }
                Ok(Some(false))
            }
            Win16Api::PeekMessage => {
                let remove = self.read_far_arg_u16(0);
                let msg_off = self.read_far_arg_u16(8);
                let msg_sel = self.read_far_arg_u16(10);
                let maybe_m = self.pmode.as_mut().and_then(|pm| {
                    if (remove & 1) != 0 {
                        pm.msg_queue.pop_front()
                    } else {
                        pm.msg_queue.front().copied()
                    }
                });
                if let Some(m) = maybe_m {
                    self.write_msg_struct(msg_sel, msg_off, m);
                    self.regs[0] = 1;
                } else {
                    self.regs[0] = 0;
                }
                Ok(Some(false))
            }
            Win16Api::PostQuitMessage => {
                let q = (self.read_far_arg_u16(0) & 0xFF) as u8;
                if let Some(pm) = &mut self.pmode {
                    pm.quit_posted = Some(q);
                }
                self.exit_code = q;
                Ok(Some(false))
            }
            _ => Ok(None),
        }
    }

    fn write_msg_struct(&mut self, sel: u16, off: u16, m: Win16Msg) {
        self.write_u16(sel, off, m.hwnd);
        self.write_u16(sel, off.wrapping_add(2), m.message);
        self.write_u16(sel, off.wrapping_add(4), m.wparam);
        self.write_u16(sel, off.wrapping_add(6), (m.lparam & 0xFFFF) as u16);
        self.write_u16(sel, off.wrapping_add(8), (m.lparam >> 16) as u16);
    }

    fn win16_create_window(&mut self) {
        let class_name = self.read_asciiz(self.read_far_arg_u16(28), self.read_far_arg_u16(26));
        let title = self.read_asciiz(self.read_far_arg_u16(24), self.read_far_arg_u16(22));
        let style = self.read_far_arg_u32(18);
        let x = self.read_far_arg_u16(16);
        let y = self.read_far_arg_u16(14);
        let width = self.read_far_arg_u16(12);
        let height = self.read_far_arg_u16(10);
        if let Some(pm) = &mut self.pmode {
            pm.window_title.clone_from(&title);
            pm.created_window = Some(CreatedWindowInfo {
                class_name,
                title,
                style,
                x,
                y,
                width,
                height,
            });
            pm.msg_queue.push_front(Win16Msg {
                hwnd: Win16State::HWND_MAIN,
                message: 0x0001,
                wparam: 0,
                lparam: 0,
            });
        }
        self.regs[0] = Win16State::HWND_MAIN;
    }

    fn handle_win16_gdi_clip_net(&mut self, api: Win16Api) {
        match api {
            Win16Api::OpenClipboard | Win16Api::CloseClipboard | Win16Api::EmptyClipboard => {
                if api == Win16Api::EmptyClipboard
                    && let Some(pm) = &mut self.pmode
                {
                    pm.clipboard_bytes.clear();
                }
                self.regs[0] = 1;
            }
            Win16Api::SetClipboardData => {
                let hmem = self.read_far_arg_u16(0);
                let mut copied = Vec::new();
                for off in 0..4096_u16 {
                    let b = self.read_u8(hmem, off);
                    if b == 0 {
                        break;
                    }
                    copied.push(b);
                }
                if let Some(pm) = &mut self.pmode {
                    pm.clipboard_bytes = copied;
                }
                self.regs[0] = hmem;
            }
            Win16Api::GetClipboardData => self.win16_get_clipboard_data(),
            Win16Api::CreateCaret => {
                let h = self.read_far_arg_u16(0);
                let w = self.read_far_arg_u16(2);
                if let Some(pm) = &mut self.pmode {
                    pm.caret.0 = w;
                    pm.caret.1 = h;
                }
            }
            Win16Api::DestroyCaret => {
                if let Some(pm) = &mut self.pmode {
                    pm.caret.4 = false;
                }
            }
            Win16Api::SetCaretPos => {
                let y = self.read_far_arg_u16(0);
                let x = self.read_far_arg_u16(2);
                if let Some(pm) = &mut self.pmode {
                    pm.caret.2 = x;
                    pm.caret.3 = y;
                }
            }
            Win16Api::ShowCaret => {
                if let Some(pm) = &mut self.pmode {
                    pm.caret.4 = true;
                }
            }
            Win16Api::GetStockObject => self.regs[0] = 0x5000 + self.read_far_arg_u16(0),
            Win16Api::SelectObject => self.regs[0] = 0x5000,
            Win16Api::GetTextMetrics => {
                let tm_off = self.read_far_arg_u16(0);
                let tm_sel = self.read_far_arg_u16(2);
                self.write_u16(tm_sel, tm_off, 12);
                self.write_u16(tm_sel, tm_off.wrapping_add(20), 8);
                self.regs[0] = 1;
            }
            Win16Api::TextOut => {
                let cb = usize::from(self.read_far_arg_u16(0));
                let s_off = self.read_far_arg_u16(2);
                let s_sel = self.read_far_arg_u16(4);
                let y = self.read_far_arg_u16(6);
                let x = self.read_far_arg_u16(8);
                let mut row = Vec::with_capacity(cb);
                for i in 0..cb {
                    row.push(self.read_u8(
                        s_sel,
                        s_off.wrapping_add(u16::try_from(i).unwrap_or(0)),
                    ));
                }
                if let Some(pm) = &mut self.pmode {
                    pm.text_out_calls.push((x, y, row));
                }
                self.regs[0] = 1;
            }
            Win16Api::DragQueryFile => self.win16_drag_query_file(),
            Win16Api::WNetAddConnection => {
                let loc = self
                    .read_asciiz(self.read_far_arg_u16(2), self.read_far_arg_u16(0))
                    .to_ascii_uppercase();
                let net = self.read_asciiz(self.read_far_arg_u16(10), self.read_far_arg_u16(8));
                if let Some(pm) = &mut self.pmode {
                    pm.unc_connections.insert(loc, net);
                }
                self.regs[0] = 0;
            }
            Win16Api::WNetCancelConnection => {
                let loc = self
                    .read_asciiz(self.read_far_arg_u16(4), self.read_far_arg_u16(2))
                    .to_ascii_uppercase();
                if let Some(pm) = &mut self.pmode {
                    pm.unc_connections.remove(&loc);
                }
                self.regs[0] = 0;
            }
            _ => self.regs[0] = 0,
        }
    }

    fn win16_get_clipboard_data(&mut self) {
        let clip = self
            .pmode
            .as_ref()
            .map_or_else(Vec::new, |p| p.clipboard_bytes.clone());
        if clip.is_empty() {
            self.regs[0] = 0;
        } else if let Some(pm) = &mut self.pmode {
            let sel = pm.next_global_sel;
            let lin = pm.next_global_lin;
            let len = clip.len() + 1;
            self.mem[lin..lin + clip.len()].copy_from_slice(&clip);
            self.mem[lin + clip.len()] = 0;
            pm.ldt.insert(
                sel,
                LdtDescriptor {
                    lin_base: lin,
                    limit: len.max(16) - 1,
                    executable: false,
                    writable: true,
                    discardable: false,
                },
            );
            pm.next_global_sel += Win16State::AHINCR;
            pm.next_global_lin = (lin + len + 0x0FFF) & !0x0FFF;
            self.regs[0] = sel;
        }
    }

    fn win16_drag_query_file(&mut self) {
        let cb = usize::from(self.read_far_arg_u16(0));
        let buf_off = self.read_far_arg_u16(2);
        let buf_sel = self.read_far_arg_u16(4);
        let idx = self.read_far_arg_u16(6);
        if idx == 0xFFFF {
            self.regs[0] = self
                .pmode
                .as_ref()
                .map_or(0, |p| u16::try_from(p.dropped_files.len()).unwrap_or(0));
        } else {
            let fname = self
                .pmode
                .as_ref()
                .and_then(|p| p.dropped_files.get(usize::from(idx)).cloned())
                .unwrap_or_default();
            let bytes = fname.as_bytes();
            let n = bytes.len().min(cb.saturating_sub(1));
            for (i, &b) in bytes[..n].iter().enumerate() {
                self.write_u8(
                    buf_sel,
                    buf_off.wrapping_add(u16::try_from(i).unwrap_or(0)),
                    b,
                );
            }
            self.write_u8(
                buf_sel,
                buf_off.wrapping_add(u16::try_from(n).unwrap_or(0)),
                0,
            );
            self.regs[0] = u16::try_from(n).unwrap_or(0);
        }
    }

    fn alloc_win16_hfile(&mut self, path: &str, pos: usize) -> u16 {
        let hf = self.pmode.as_mut().map_or(10, |pm| {
            let h = pm.next_hfile;
            pm.next_hfile = pm.next_hfile.saturating_add(1);
            h
        });
        self.handles.insert(
            hf,
            crate::OpenFile {
                path: path.to_owned(),
                pos,
            },
        );
        hf
    }
}

fn validate_mz_ne(image: &[u8]) -> Result<usize, ToolError> {
    if image.len() < 0x80 || &image[0..2] != b"MZ" {
        return Err(ToolError::Emulator {
            cs: 0,
            ip: 0,
            code: ErrCode::BadExeFormat,
            message: "missing MZ header on Win16 NE image".into(),
        });
    }
    let lfanew = usize::try_from(u32::from_le_bytes([
        image[0x3C],
        image[0x3D],
        image[0x3E],
        image[0x3F],
    ]))
    .unwrap_or(usize::MAX);
    if lfanew.saturating_add(0x40) > image.len() || &image[lfanew..lfanew + 2] != b"NE" {
        return Err(ToolError::Emulator {
            cs: 0,
            ip: 0,
            code: ErrCode::BadExeFormat,
            message: "missing NE header signature (0x454E)".into(),
        });
    }
    Ok(lfanew)
}

fn resolve_ordinal_import(module: &str, ordinal: u16) -> Option<Win16Api> {
    match module {
        "KERNEL" => match ordinal {
            3 => Some(Win16Api::GetVersion),
            15 => Some(Win16Api::GlobalAlloc),
            17 => Some(Win16Api::GlobalFree),
            18 => Some(Win16Api::GlobalLock),
            19 => Some(Win16Api::GlobalUnlock),
            30 => Some(Win16Api::WaitEvent),
            45 => Some(Win16Api::LoadModule),
            47 => Some(Win16Api::GetModuleHandle),
            48 => Some(Win16Api::GetModuleUsage),
            50 => Some(Win16Api::GetProcAddress),
            51 => Some(Win16Api::MakeProcInstance),
            52 => Some(Win16Api::FreeProcInstance),
            74 => Some(Win16Api::OpenFile),
            81 => Some(Win16Api::LClose),
            82 => Some(Win16Api::LRead),
            83 => Some(Win16Api::LCreat),
            84 => Some(Win16Api::LLSeek),
            85 => Some(Win16Api::LWrite),
            91 => Some(Win16Api::InitTask),
            95 => Some(Win16Api::LoadLibrary),
            107 => Some(Win16Api::SetErrorMode),
            _ => None,
        },
        "USER" => match ordinal {
            5 => Some(Win16Api::InitApp),
            6 => Some(Win16Api::PostQuitMessage),
            32 => Some(Win16Api::GetClientRect),
            36 => Some(Win16Api::SetWindowText),
            39 => Some(Win16Api::BeginPaint),
            40 => Some(Win16Api::EndPaint),
            41 => Some(Win16Api::CreateWindow),
            42 => Some(Win16Api::ShowWindow),
            57 => Some(Win16Api::RegisterClass),
            61 => Some(Win16Api::ScrollWindow),
            62 => Some(Win16Api::SetScrollPos),
            64 => Some(Win16Api::SetScrollRange),
            107 => Some(Win16Api::DefWindowProc),
            108 => Some(Win16Api::GetMessage),
            109 => Some(Win16Api::PeekMessage),
            113 => Some(Win16Api::TranslateMessage),
            114 => Some(Win16Api::DispatchMessage),
            124 => Some(Win16Api::UpdateWindow),
            125 => Some(Win16Api::InvalidateRect),
            137 => Some(Win16Api::OpenClipboard),
            138 => Some(Win16Api::CloseClipboard),
            139 => Some(Win16Api::EmptyClipboard),
            141 => Some(Win16Api::SetClipboardData),
            142 => Some(Win16Api::GetClipboardData),
            150 => Some(Win16Api::LoadMenu),
            163 => Some(Win16Api::CreateCaret),
            164 => Some(Win16Api::DestroyCaret),
            165 => Some(Win16Api::SetCaretPos),
            166 => Some(Win16Api::ShowCaret),
            173 => Some(Win16Api::LoadCursor),
            176 => Some(Win16Api::LoadIcon),
            _ => None,
        },
        "GDI" => match ordinal {
            33 => Some(Win16Api::TextOut),
            45 => Some(Win16Api::SelectObject),
            87 => Some(Win16Api::GetStockObject),
            91 => Some(Win16Api::GetTextMetrics),
            _ => None,
        },
        _ => None,
    }
}

const fn resolve_dynamic_ordinal(hmod: u16, ordinal: u16, era: Win16Era) -> Option<Win16Api> {
    let is_3x = matches!(era, Win16Era::Win310 | Win16Era::Wfw311 | Win16Era::Win320Cjk);
    match (hmod, ordinal) {
        (1, 166) if is_3x => Some(Win16Api::WinExec),
        (1, 169) if is_3x => Some(Win16Api::GetFreeSpace),
        (1, 207) if matches!(era, Win16Era::Win320Cjk) => Some(Win16Api::IsDbcsLeadByte),
        (2, 284) if is_3x => Some(Win16Api::GetFreeSystemResources),
        (2 | 6, 512) if matches!(era, Win16Era::Wfw311) => Some(Win16Api::WNetGetConnection),
        (2 | 6, 517) if matches!(era, Win16Era::Wfw311) => Some(Win16Api::WNetAddConnection),
        (2 | 6, 518) if matches!(era, Win16Era::Wfw311) => Some(Win16Api::WNetCancelConnection),
        _ => None,
    }
}

fn resolve_dynamic_name(hmod: u16, sym: &str, era: Win16Era) -> Option<Win16Api> {
    let is_3x = matches!(era, Win16Era::Win310 | Win16Era::Wfw311 | Win16Era::Win320Cjk);
    match (hmod, sym) {
        (1, "WINEXEC") if is_3x => Some(Win16Api::WinExec),
        (1, "GETFREESPACE") if is_3x => Some(Win16Api::GetFreeSpace),
        (1, "ISDBCSLEADBYTE") if is_3x => Some(Win16Api::IsDbcsLeadByte),
        (2, "GETFREESYSTEMRESOURCES") if is_3x => Some(Win16Api::GetFreeSystemResources),
        (2 | 6, "WNETADDCONNECTION") if era == Win16Era::Wfw311 => Some(Win16Api::WNetAddConnection),
        (2 | 6, "WNETCANCELCONNECTION") if era == Win16Era::Wfw311 => {
            Some(Win16Api::WNetCancelConnection)
        }
        (2 | 6, "WNETGETCONNECTION") if era == Win16Era::Wfw311 => {
            Some(Win16Api::WNetGetConnection)
        }
        (4, "DRAGACCEPTFILES") if is_3x => Some(Win16Api::DragAcceptFiles),
        (4, "DRAGQUERYFILE") if is_3x => Some(Win16Api::DragQueryFile),
        (4, "DRAGFINISH") if is_3x => Some(Win16Api::DragFinish),
        (5, "MEMMANINFO") if is_3x => Some(Win16Api::MemManInfo),
        _ => None,
    }
}
