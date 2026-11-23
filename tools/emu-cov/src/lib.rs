//! Deterministic 8086/286 CPU + MS-DOS (`1.25`, `2.00`, `4.00`, `7.10`) syscall & memory
//! coverage emulator (`emu-cov`), recording instruction PCs and conditional-branch edges
//! and emitting standard `lcov.info` reports.

use core::fmt::Write as _;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use wincmd_diag::{ErrCode, ToolError};

pub mod arm32_pe;
pub mod arm64_pe;
pub mod dos_syscalls;
pub mod win16_ne;
pub mod win_mock;
pub mod x86_pe;
use dos_syscalls::{CON_IN_SENTINEL, CON_OUT_SENTINEL, LfnSearchState, NUL_SENTINEL, VfsMeta};
pub use win16_ne::{Win16Era, Win16State};

/// Total simulated 8086 Real-Mode + HMA address space (`1 MiB + 64 KiB`).
pub const MEM_SIZE: usize = 1_114_112;

/// Simulated MS-DOS kernel era for hardware/API quirk fidelity.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DosEra {
    /// MS-DOS `1.25` (1982): `AH=30h` returns `AL=0`, FCB-only file I/O, no subdirectories.
    Dos125,
    /// MS-DOS `2.00` (1983): `FILES=8`, `AH=4Bh` clobbers `SS:SP` across child `EXEC`.
    Dos200,
    /// MS-DOS `4.00` (1988): `AX=6C00h` extended open, `AH=59h` extended error, DBCS.
    Dos400,
    /// MS-DOS `7.10` (Windows 95 OSR2 / 98): `AX=71xxh` LFN & `AX=7303h` FAT32 free space.
    Dos710,
}

impl DosEra {
    /// Parses a CLI era string (`1.25`, `2.00`, `4.00`, `7.10`).
    #[must_use]
    pub fn parse(raw: &str) -> Option<Self> {
        match raw.trim() {
            "1.25" | "dos125" => Some(Self::Dos125),
            "2.0" | "2.00" | "dos200" => Some(Self::Dos200),
            "4.0" | "4.00" | "dos400" => Some(Self::Dos400),
            "7.10" | "dos710" | "win9x" => Some(Self::Dos710),
            _ => None,
        }
    }

    /// Returns `(major, minor)` reported by `INT 21h AH=30h` (`(0, 0)` on DOS 1.25!).
    #[must_use]
    pub const fn dos_version_words(self) -> (u8, u8) {
        match self {
            Self::Dos125 => (0, 0),
            Self::Dos200 => (2, 0),
            Self::Dos400 => (4, 0),
            Self::Dos710 => (7, 10),
        }
    }
}

/// CPU flag bit constants in the 16-bit 8086 `FLAGS` register.
pub mod flags {
    /// Carry Flag (`bit 0`).
    pub const CF: u16 = 1 << 0;
    /// Parity Flag (`bit 2`).
    pub const PF: u16 = 1 << 2;
    /// Auxiliary Carry Flag (`bit 4`).
    pub const AF: u16 = 1 << 4;
    /// Zero Flag (`bit 6`).
    pub const ZF: u16 = 1 << 6;
    /// Sign Flag (`bit 7`).
    pub const SF: u16 = 1 << 7;
    /// Interrupt Enable Flag (`bit 9`).
    pub const IF: u16 = 1 << 9;
    /// Direction Flag (`bit 10`).
    pub const DF: u16 = 1 << 10;
    /// Overflow Flag (`bit 11`).
    pub const OF: u16 = 1 << 11;
}

/// Recorded execution and branch-edge coverage telemetry.
#[derive(Clone, Debug, Default)]
pub struct CoverageData {
    /// When true, per-instruction PC and branch-edge insertions are skipped (fast fuzzing mode).
    pub disabled: bool,
    /// Set of linear 20-bit instruction addresses executed at least once.
    pub executed_pcs: BTreeSet<u32>,
    /// Conditional branch outcomes keyed by linear instruction address: `(seen_taken, seen_not_taken)`.
    pub branch_edges: BTreeMap<u32, (bool, bool)>,
}

impl CoverageData {
    /// Converts a 32-bit or 64-bit PE execution trace (`executed_pcs`, `branch_edges`) into [`CoverageData`].
    #[must_use]
    pub fn from_pe_trace(
        pcs: impl IntoIterator<Item = u64>,
        edges: impl IntoIterator<Item = (u64, bool)>,
    ) -> Self {
        let mut cov = Self::default();
        for pc in pcs {
            let _ = cov
                .executed_pcs
                .insert(u32::try_from(pc & 0xFFFF_FFFF).unwrap_or(0));
        }
        for (pc, taken) in edges {
            let key = u32::try_from(pc & 0xFFFF_FFFF).unwrap_or(0);
            let entry = cov.branch_edges.entry(key).or_insert((false, false));
            if taken {
                entry.0 = true;
            } else {
                entry.1 = true;
            }
        }
        cov
    }

    /// Merges another [`CoverageData`] run into `self`, accumulating both `executed_pcs`
    /// and `(taken, not_taken)` conditional-branch edges across test workloads.
    pub fn merge(&mut self, other: &Self) {
        self.executed_pcs.extend(other.executed_pcs.iter().copied());
        for (&pc, &(t, nt)) in &other.branch_edges {
            let entry = self.branch_edges.entry(pc).or_insert((false, false));
            entry.0 |= t;
            entry.1 |= nt;
        }
    }

    /// Total observed conditional branch Half-Edges (`2` per encountered conditional branch).
    #[must_use]
    pub fn total_branch_edges(&self) -> usize {
        self.branch_edges.len() * 2
    }

    /// Number of conditional branch Half-Edges actually exercised.
    #[must_use]
    pub fn covered_branch_edges(&self) -> usize {
        self.branch_edges
            .values()
            .map(|&(t, nt)| usize::from(t) + usize::from(nt))
            .sum()
    }

    /// Verifies that observed conditional branches achieve at least `required_bps` (`10_000` = `100.00%`)
    /// taken + not-taken edge coverage.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Coverage`] if branch-edge coverage falls below `required_bps`.
    pub fn verify_branch_coverage(
        &self,
        scope: &str,
        required_bps: u32,
    ) -> Result<(), ToolError> {
        let total = self.total_branch_edges();
        if total == 0 {
            return Ok(());
        }
        let covered = self.covered_branch_edges();
        let bps = u32::try_from((covered * 10_000) / total).unwrap_or(0);
        if bps < required_bps {
            return Err(ToolError::Coverage {
                scope: scope.to_owned(),
                covered,
                total,
                percent_bps: bps,
                required_bps,
            });
        }
        Ok(())
    }

    /// Renders an LCOV tracefile (`TN:` / `SF:` / `DA:` / `BRDA:` / `BRF:` / `BRH:` / `end_of_record`).
    #[must_use]
    pub fn to_lcov(&self, source_name: &str) -> String {
        const LF: char = 10 as char;
        let mut out = String::new();
        out.push_str("TN:emu_cov");
        out.push(LF);
        let _ = writeln!(out, "SF:{source_name}");
        for &pc in &self.executed_pcs {
            let _ = writeln!(out, "DA:{pc},1");
        }
        for (&pc, &(taken, not_taken)) in &self.branch_edges {
            let t_cnt = u8::from(taken);
            let nt_cnt = u8::from(not_taken);
            let _ = writeln!(out, "BRDA:{pc},0,0,{t_cnt}");
            let _ = writeln!(out, "BRDA:{pc},0,1,{nt_cnt}");
        }
        let _ = writeln!(out, "BRF:{}", self.total_branch_edges());
        let _ = writeln!(out, "BRH:{}", self.covered_branch_edges());
        let _ = writeln!(out, "LF:{}", self.executed_pcs.len());
        let _ = writeln!(out, "LH:{}", self.executed_pcs.len());
        out.push_str("end_of_record");
        out.push(LF);
        out
    }

    /// Verifies that executed instructions (`expected_pcs`) meet `required_bps` (`10_000` = `100.00%`).
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Coverage`] if coverage is below `required_bps`.
    pub fn verify_pc_coverage(
        &self,
        scope: &str,
        expected_pcs: &[u32],
        required_bps: u32,
    ) -> Result<(), ToolError> {
        if expected_pcs.is_empty() {
            return Ok(());
        }
        let covered = expected_pcs
            .iter()
            .filter(|pc| self.executed_pcs.contains(pc))
            .count();
        let total = expected_pcs.len();
        let bps = u32::try_from((covered * 10_000) / total).unwrap_or(0);
        if bps < required_bps {
            return Err(ToolError::Coverage {
                scope: scope.to_owned(),
                covered,
                total,
                percent_bps: bps,
                required_bps,
            });
        }
        Ok(())
    }
}

/// Outcome of running a `.COM` binary or unit procedure inside [`DosVm`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct VmRunResult {
    /// Process exit code (`AL` from `AH=4Ch`, or `0` from `INT 20h` / `RET`).
    pub exit_code: u8,
    /// Captured `stdout` byte stream (handle `1` + `AH=02h`/`06h`/`09h`).
    pub stdout: Vec<u8>,
    /// Captured `stderr` byte stream (handle `2`).
    pub stderr: Vec<u8>,
    /// Total retired instructions.
    pub steps: u64,
}

#[derive(Clone, Debug)]
pub(crate) struct OpenFile {
    pub(crate) path: String,
    pub(crate) pos: usize,
}

#[derive(Clone, Debug)]
pub(crate) struct SavedExecFrame {
    pub(crate) regs: [u16; 8],
    pub(crate) sregs: [u16; 4],
    pub(crate) ip: u16,
    pub(crate) flags: u16,
    pub(crate) dta: (u16, u16),
}

/// Matched 8.3 FCB directory entry `(name8, ext3, attr, size)`.
pub(crate) type FcbSearchHit = ([u8; 8], [u8; 3], u8, u32);

/// Per-DTA `AH=4Eh/4Fh` search result list `(matches, next_index)` keyed by `(seg, off)`.
pub(crate) type DtaSearchState = (Vec<(String, u8, u32)>, usize);

/// Deterministic 8086 CPU + MS-DOS virtual machine with instruction/branch coverage tracking.
#[allow(clippy::struct_excessive_bools)]
pub struct DosVm {
    /// Flat `1 MiB + 64 KiB` guest RAM.
    pub mem: Vec<u8>,
    /// General-purpose 16-bit registers: `AX, CX, DX, BX, SP, BP, SI, DI`.
    pub regs: [u16; 8],
    /// Segment registers: `ES, CS, SS, DS`.
    pub sregs: [u16; 4],
    /// Instruction pointer.
    pub ip: u16,
    /// 16-bit `FLAGS` register.
    pub flags: u16,
    /// Active DOS kernel era.
    pub era: DosEra,
    /// In-memory virtual filesystem (`UPPERCASE_PATH -> bytes`).
    pub vfs: BTreeMap<String, Vec<u8>>,
    /// Per-file DOS/LFN metadata (`attr`, `lfn_name`, `short_alias`).
    pub vfs_meta: BTreeMap<String, VfsMeta>,
    /// Existing directories (`UPPERCASE_DIR_PATH`).
    pub dirs: BTreeSet<String>,
    /// Current working directory (`""` for root `A:\`).
    pub cwd: String,
    /// Queued console `stdin` bytes.
    pub stdin: Vec<u8>,
    stdin_pos: usize,
    /// Queued IBM PC BIOS `INT 16h` `(scancode, ascii)` keystrokes.
    pub bios_keys: Vec<(u8, u8)>,
    pub(crate) bios_key_pos: usize,
    /// Captured `stdout`.
    pub stdout: Vec<u8>,
    /// Captured `stderr`.
    pub stderr: Vec<u8>,
    /// Active Disk Transfer Address (`DTA`) segment:offset.
    pub dta: (u16, u16),
    handles: BTreeMap<u16, OpenFile>,
    /// Peak concurrent non-standard open handles (`>= 5`) observed during execution.
    pub max_concurrent_handles: usize,
    pub(crate) fcb_search: Vec<FcbSearchHit>,
    pub(crate) fcb_search_idx: usize,
    pub(crate) dta_searches: BTreeMap<(u16, u16), DtaSearchState>,
    pub(crate) lfn_searches: BTreeMap<u16, LfnSearchState>,
    /// Currently open `AX=714Eh` LFN search handles (must be empty on clean completion!).
    pub open_lfn_handles: BTreeSet<u16>,
    pub(crate) next_lfn_handle: u16,
    /// Whether LIM EMS 4.0 (`INT 67h`) is active.
    pub ems_enabled: bool,
    pub(crate) ems_store: BTreeMap<u16, Vec<u8>>,
    /// Handle names assigned via `INT 67h AX=5301h`.
    pub ems_names: BTreeMap<u16, [u8; 8]>,
    pub(crate) ems_mapped: Option<(u16, usize)>,
    pub(crate) next_ems_handle: u16,
    /// Whether XMS 2.0 (`INT 2Fh AX=4300h/4310h`) is active.
    pub xms_enabled: bool,
    pub(crate) xms_store: BTreeMap<u16, Vec<u8>>,
    pub(crate) next_xms_handle: u16,
    /// Active DOS NLS codepage (`437`, `850`, `932`, `936`, `949`, `950`, ...).
    pub active_cp: u16,
    /// Simulated free 12-bit FAT clusters returned by `INT 25h` on MS-DOS 1.25.
    pub free_clusters_125: u16,
    /// Simulated free 16-bit FAT clusters returned by `AH=36h` on MS-DOS 2.00/4.00.
    pub fat16_free_clusters: u16,
    /// Simulated free 32-bit FAT32 clusters returned by `AX=7303h` on MS-DOS 7.10.
    pub fat32_free_clusters: u32,
    /// Simulated 32-bit volume serial number returned by `AX=6900h` on MS-DOS 4.00+.
    pub vol_serial: u32,
    /// Simulated Windows Enhanced Mode (`INT 2Fh AX=1600h`) presence.
    pub win_enh_mode: bool,
    /// Simulates a pure serial/TTY console (`DX = 0x0080` on `AX=4400h`) instead of IBM BIOS `CON`.
    pub force_tty_console: bool,
    /// Count of `INT 2Fh AX=1680h` idle time-slice yields executed.
    pub idle_yields: u32,
    /// Last `WINOLDAP` window title set via `INT 2Fh AX=168Eh`.
    pub winoldap_title: String,
    /// Simulated `WINOLDAP` Clipboard buffer (`INT 2Fh AX=1700h..1708h`).
    pub winoldap_clipboard: Vec<u8>,
    /// Enables 80286 instructions (`PUSHA`/`POPA`, `PUSH imm`, `SHL imm`, `ENTER`/`LEAVE`).
    pub cpu_286_enabled: bool,
    /// Optional 16-bit Windows Protected-Mode `NE` LDT & subsystem state.
    pub pmode: Option<Win16State>,
    pub(crate) gp_fault: Option<String>,
    /// Last DOS extended error code returned by `AH=59h`.
    pub last_dos_err: u16,
    /// Whether child process termination corrupts relocated high-RAM `TRANSIENT` (`wMemTop - 0x0400`).
    pub corrupt_high_ram_on_exec: bool,
    pub(crate) exec_frames: Vec<SavedExecFrame>,
    pub(crate) last_child_exit_code: u8,
    pub(crate) tmp_counter: u16,
    dos_ctrl_flags: u8,
    pub(crate) halted: bool,
    pub(crate) exit_code: u8,
    pc_bits: Vec<u64>,
    br_taken_bits: Vec<u64>,
    br_not_taken_bits: Vec<u64>,
    /// Recorded coverage data.
    pub coverage: CoverageData,
}

impl DosVm {
    /// Segment index for `ES`.
    pub const ES: usize = 0;
    /// Segment index for `CS`.
    pub const CS: usize = 1;
    /// Segment index for `SS`.
    pub const SS: usize = 2;
    /// Segment index for `DS`.
    pub const DS: usize = 3;

    /// Default PSP segment where `.COM` images are loaded (`0x1000`).
    pub const DEFAULT_PSP_SEG: u16 = 0x1000;
    /// Default environment block segment (`0x0800`).
    pub const DEFAULT_ENV_SEG: u16 = 0x0800;
    /// ROM BIOS/DOS stub segment (`0xF000`).
    pub const ROM_SEG: u16 = 0xF000;

    /// Creates a fresh [`DosVm`] configured for `era`.
    #[must_use]
    pub fn new(era: DosEra) -> Self {
        let mut vm = Self {
            mem: vec![0_u8; MEM_SIZE],
            regs: [0; 8],
            sregs: [0; 4],
            ip: 0x0100,
            flags: 0x0002 | flags::IF,
            era,
            vfs: BTreeMap::new(),
            vfs_meta: BTreeMap::new(),
            dirs: Self::empty_set(),
            cwd: String::new(),
            stdin: Vec::new(),
            stdin_pos: 0,
            bios_keys: Vec::new(),
            bios_key_pos: 0,
            stdout: Vec::new(),
            stderr: Vec::new(),
            dta: (Self::DEFAULT_PSP_SEG, 0x0080),
            handles: BTreeMap::new(),
            max_concurrent_handles: 0,
            fcb_search: Vec::new(),
            fcb_search_idx: 0,
            dta_searches: BTreeMap::new(),
            lfn_searches: BTreeMap::new(),
            open_lfn_handles: Self::empty_set(),
            next_lfn_handle: 0x0100,
            ems_enabled: false,
            ems_store: BTreeMap::new(),
            ems_names: BTreeMap::new(),
            ems_mapped: None,
            next_ems_handle: 1,
            xms_enabled: false,
            xms_store: BTreeMap::new(),
            next_xms_handle: 1,
            active_cp: 437,
            free_clusters_125: 200,
            fat16_free_clusters: 20_000,
            fat32_free_clusters: 1_250_000,
            vol_serial: 0x1988_0400,
            win_enh_mode: false,
            force_tty_console: false,
            idle_yields: 0,
            winoldap_title: String::new(),
            winoldap_clipboard: Vec::new(),
            cpu_286_enabled: false,
            pmode: None,
            gp_fault: None,
            last_dos_err: 0,
            corrupt_high_ram_on_exec: false,
            exec_frames: Vec::new(),
            last_child_exit_code: 0,
            tmp_counter: 1,
            dos_ctrl_flags: 0,
            halted: false,
            exit_code: 0,
            pc_bits: vec![0_u64; MEM_SIZE / 64],
            br_taken_bits: vec![0_u64; MEM_SIZE / 64],
            br_not_taken_bits: vec![0_u64; MEM_SIZE / 64],
            coverage: CoverageData::default(),
        };
        for vec_no in 0_u16..256_u16 {
            let off = 0x0200_u16 + vec_no * 4;
            let ivt_lin = usize::from(vec_no) * 4;
            vm.write_u16_lin(ivt_lin, off);
            vm.write_u16_lin(ivt_lin + 2, Self::ROM_SEG);
            vm.mem[Self::lin(Self::ROM_SEG, off)] = 0xCF;
        }
        vm.mem[Self::lin(Self::ROM_SEG, 0x0080)] = 0xCB;
        vm.refresh_rom_dbcs_table();
        vm
    }

    /// Loads a `.COM` binary at `PSP:0100h`, builds an authentic `MCB` chain (`0x07FF` env MCB,
    /// `0x0FFF` PSP MCB up to `0xA000`), initializes `PSP:0080h` with `cmdline_tail`, and sets
    /// `CS=DS=ES=SS=PSP`, `IP=0100h`, `SP=FFFEh` (pushing `0x0000` return word to `INT 20h`).
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::SizeBudget`] if `image.len() > 65_280`.
    pub fn load_com(&mut self, image: &[u8], cmdline_tail: &str, env_vars: &[(&str, &str)]) -> Result<(), ToolError> {
        if image.len() > 65_280 {
            return Err(ToolError::SizeBudget {
                artifact: "COM image".into(),
                actual_bytes: image.len(),
                limit_bytes: 65_280,
            });
        }

        let env_seg = Self::DEFAULT_ENV_SEG;
        let psp_seg = Self::DEFAULT_PSP_SEG;

        // Write environment MCB at `env_seg - 1` (`0x07FF`)
        self.write_mcb(env_seg - 1, b'M', psp_seg, psp_seg - env_seg - 1);
        // Populate environment `KEY=VAL\0...\0\0` + word(1) + `A:\WINCMD.COM\0`
        let mut env_off = Self::lin(env_seg, 0);
        for &(k, v) in env_vars {
            for &b in k.as_bytes() {
                self.mem[env_off] = b;
                env_off += 1;
            }
            self.mem[env_off] = b'=';
            env_off += 1;
            for &b in v.as_bytes() {
                self.mem[env_off] = b;
                env_off += 1;
            }
            self.mem[env_off] = 0;
            env_off += 1;
        }
        self.mem[env_off] = 0;
        env_off += 1;
        self.mem[env_off] = 1;
        self.mem[env_off + 1] = 0;
        env_off += 2;
        for &b in br"A:\WINCMD.COM" {
            self.mem[env_off] = b;
            env_off += 1;
        }
        self.mem[env_off] = 0;

        // Write program MCB at `psp_seg - 1` (`0x0FFF`), spanning to `0xA000` (`0x9000` paras)
        self.write_mcb(psp_seg - 1, b'Z', psp_seg, 0xA000 - psp_seg);

        // Initialize 256-byte PSP
        let psp_lin = Self::lin(psp_seg, 0);
        self.mem[psp_lin] = 0xCD; // INT 20h
        self.mem[psp_lin + 1] = 0x20;
        self.write_u16_lin(psp_lin + 0x02, 0xA000); // wMemTop
        self.write_u16_lin(psp_lin + 0x2C, env_seg); // wEnvSeg

        let tail_bytes = cmdline_tail.as_bytes();
        let tail_len = tail_bytes.len().min(126);
        self.mem[psp_lin + 0x80] = u8::try_from(tail_len).unwrap_or(0);
        self.mem[(psp_lin + 0x81)..(psp_lin + 0x81 + tail_len)]
            .copy_from_slice(&tail_bytes[..tail_len]);
        self.mem[psp_lin + 0x81 + tail_len] = 0x0D;

        let load_lin = Self::lin(psp_seg, 0x0100);
        self.mem[load_lin..(load_lin + image.len())].copy_from_slice(image);

        self.sregs = [psp_seg, psp_seg, psp_seg, psp_seg];
        self.regs = [0; 8];
        self.regs[4] = 0xFFFE; // SP = 0xFFFE, [SS:FFFE] = 0x0000 -> RET jumps to PSP:0000 (INT 20h)
        self.write_u16(psp_seg, 0xFFFE, 0x0000);
        self.ip = 0x0100;
        self.dta = (psp_seg, 0x0080);
        self.halted = false;
        self.exit_code = 0;

        Ok(())
    }

    fn write_mcb(&mut self, mcb_seg: u16, kind: u8, owner: u16, size_paras: u16) {
        let base = Self::lin(mcb_seg, 0);
        self.mem[base] = kind;
        self.write_u16_lin(base + 1, owner);
        self.write_u16_lin(base + 3, size_paras);
    }

    /// Converts `seg:off` to a linear address wrapped inside `MEM_SIZE`.
    #[must_use]
    pub const fn lin(seg: u16, off: u16) -> usize {
        (((seg as usize) << 4) + (off as usize)) % MEM_SIZE
    }

    /// Reads an 8-bit byte at `seg:off` (using LDT translation when `pmode` is active).
    #[must_use]
    pub fn read_u8(&self, seg: u16, off: u16) -> u8 {
        if let Some(pm) = &self.pmode {
            return pm
                .ldt
                .get(&seg)
                .filter(|d| usize::from(off) <= d.limit)
                .map_or(0, |d| self.mem[(d.lin_base + usize::from(off)) % MEM_SIZE]);
        }
        self.mem[Self::lin(seg, off)]
    }

    /// Writes an 8-bit byte at `seg:off` (enforcing Protected-Mode LDT writability & limits).
    pub fn write_u8(&mut self, seg: u16, off: u16, val: u8) {
        let addr = self.resolve_pmode_or_real(seg, off, true);
        if self.gp_fault.is_none() {
            self.mem[addr] = val;
        }
    }

    /// Reads a little-endian 16-bit word at `seg:off`.
    #[must_use]
    pub fn read_u16(&self, seg: u16, off: u16) -> u16 {
        let lo = u16::from(self.read_u8(seg, off));
        let hi = u16::from(self.read_u8(seg, off.wrapping_add(1)));
        lo | (hi << 8)
    }

    /// Writes a little-endian 16-bit word at `seg:off`.
    pub fn write_u16(&mut self, seg: u16, off: u16, val: u16) {
        self.write_u8(seg, off, (val & 0xFF) as u8);
        self.write_u8(seg, off.wrapping_add(1), (val >> 8) as u8);
    }

    pub(crate) fn write_u16_lin(&mut self, lin_addr: usize, val: u16) {
        self.mem[lin_addr % MEM_SIZE] = (val & 0xFF) as u8;
        self.mem[(lin_addr + 1) % MEM_SIZE] = (val >> 8) as u8;
    }

    pub(crate) fn read_u16_lin(&self, lin_addr: usize) -> u16 {
        let lo = u16::from(self.mem[lin_addr % MEM_SIZE]);
        let hi = u16::from(self.mem[(lin_addr + 1) % MEM_SIZE]);
        lo | (hi << 8)
    }

    /// Runs the VM until program termination (`INT 20h`, `INT 21h AH=4Ch/31h`) or `max_steps`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on illegal opcode, division trap (`INT 00h`), or step timeout.
    pub fn run(&mut self, max_steps: u64) -> Result<VmRunResult, ToolError> {
        let mut steps = 0_u64;
        while !self.halted {
            if steps >= max_steps {
                return Err(ToolError::Emulator {
                    cs: self.sregs[Self::CS],
                    ip: self.ip,
                    code: ErrCode::GeneralFailure,
                    message: format!("exceeded {max_steps} instruction step limit"),
                });
            }
            self.step()?;
            steps += 1;
        }
        Ok(VmRunResult {
            exit_code: self.exit_code,
            stdout: self.stdout.clone(),
            stderr: self.stderr.clone(),
            steps,
        })
    }

    fn fetch_u8(&mut self) -> u8 {
        let cs = self.sregs[Self::CS];
        let ip = self.ip;
        let addr = self.resolve_pmode_or_real(cs, ip, false);
        self.ip = ip.wrapping_add(1);
        self.mem[addr]
    }

    fn fetch_u16(&mut self) -> u16 {
        let lo = u16::from(self.fetch_u8());
        let hi = u16::from(self.fetch_u8());
        lo | (hi << 8)
    }

    fn push16(&mut self, val: u16) {
        let sp = self.regs[4].wrapping_sub(2);
        self.regs[4] = sp;
        self.write_u16(self.sregs[Self::SS], sp, val);
    }

    fn pop16(&mut self) -> u16 {
        let sp = self.regs[4];
        let ss = self.sregs[Self::SS];
        let _ = self.resolve_pmode_or_real(ss, sp, false);
        let val = self.read_u16(ss, sp);
        self.regs[4] = sp.wrapping_add(2);
        val
    }

    fn get_reg8(&self, r: u8) -> u8 {
        let idx = usize::from(r & 3);
        if r < 4 {
            (self.regs[idx] & 0xFF) as u8
        } else {
            (self.regs[idx] >> 8) as u8
        }
    }

    fn set_reg8(&mut self, r: u8, val: u8) {
        let idx = usize::from(r & 3);
        if r < 4 {
            self.regs[idx] = (self.regs[idx] & 0xFF00) | u16::from(val);
        } else {
            self.regs[idx] = (self.regs[idx] & 0x00FF) | (u16::from(val) << 8);
        }
    }

    /// Executes one 8086/80286 instruction and updates coverage counters.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError::Emulator`] on unimplemented opcode, Protected-Mode `#GP` fault,
    /// or hardware trap.
    #[allow(clippy::too_many_lines)]
    pub fn step(&mut self) -> Result<(), ToolError> {
        let start_cs = self.sregs[Self::CS];
        let start_ip = self.ip;
        if self.pmode.is_some() && start_cs == Win16State::THUNK_CS_SEL {
            if self.handle_win16_thunk(start_ip)? {
                return Ok(());
            }
            if self.halted {
                return Ok(());
            }
        }
        if start_cs == Self::ROM_SEG {
            if start_ip == 0x0080 {
                self.handle_xms_call();
            } else if (0x0200..0x0600).contains(&start_ip)
                && (start_ip - 0x0200).trailing_zeros() >= 2
            {
                let vec_no = u8::try_from((start_ip - 0x0200) >> 2).unwrap_or(0);
                self.handle_rom_interrupt(vec_no)?;
                if self.halted || self.sregs[Self::CS] != Self::ROM_SEG || self.ip != start_ip {
                    return Ok(());
                }
                let ss = self.sregs[Self::SS];
                let sp = self.regs[4];
                let old_f = self.read_u16(ss, sp.wrapping_add(4));
                let merged_f =
                    (old_f & !(flags::CF | flags::ZF)) | (self.flags & (flags::CF | flags::ZF));
                self.write_u16(ss, sp.wrapping_add(4), merged_f);
            }
        }
        let lin_usize = self.resolve_pmode_or_real(start_cs, start_ip, false);
        if let Some(fault) = self.gp_fault.take() {
            return Err(ToolError::Emulator {
                cs: start_cs,
                ip: start_ip,
                code: ErrCode::AccessDenied,
                message: fault,
            });
        }
        let lin_pc = u32::try_from(lin_usize).unwrap_or(0);
        let pc_word = lin_usize >> 6;
        let pc_mask = 1_u64 << (lin_usize & 63);
        if (self.pc_bits[pc_word] & pc_mask) == 0 {
            self.pc_bits[pc_word] |= pc_mask;
            self.coverage.executed_pcs.insert(lin_pc);
        }

        let mut seg_override: Option<usize> = None;
        let mut rep_mode: u8 = 0; // 0 = none, 1 = REP/REPE, 2 = REPNE

        let opcode = loop {
            let b = self.fetch_u8();
            match b {
                0x26 => seg_override = Some(Self::ES),
                0x2E => seg_override = Some(Self::CS),
                0x36 => seg_override = Some(Self::SS),
                0x3E => seg_override = Some(Self::DS),
                0xF0 => {} // LOCK
                0xF2 => rep_mode = 2,
                0xF3 => rep_mode = 1,
                other => break other,
            }
        };

        match opcode {
            // ADD / OR / ADC / SBB / AND / SUB / XOR / CMP r/m, r & r, r/m & AL/AX, imm
            0x00..=0x05
            | 0x08..=0x0D
            | 0x10..=0x15
            | 0x18..=0x1D
            | 0x20..=0x25
            | 0x28..=0x2D
            | 0x30..=0x35
            | 0x38..=0x3D => {
                let alu_op = (opcode >> 3) & 7;
                let sub = opcode & 7;
                match sub {
                    0 => {
                        let (ea, reg) = self.decode_modrm(seg_override);
                        let a = self.read_rm8(ea);
                        let b = self.get_reg8(reg);
                        let res = self.alu8(alu_op, a, b);
                        if alu_op != 7 {
                            self.write_rm8(ea, res);
                        }
                    }
                    1 => {
                        let (ea, reg) = self.decode_modrm(seg_override);
                        let a = self.read_rm16(ea);
                        let b = self.regs[usize::from(reg)];
                        let res = self.alu16(alu_op, a, b);
                        if alu_op != 7 {
                            self.write_rm16(ea, res);
                        }
                    }
                    2 => {
                        let (ea, reg) = self.decode_modrm(seg_override);
                        let a = self.get_reg8(reg);
                        let b = self.read_rm8(ea);
                        let res = self.alu8(alu_op, a, b);
                        if alu_op != 7 {
                            self.set_reg8(reg, res);
                        }
                    }
                    3 => {
                        let (ea, reg) = self.decode_modrm(seg_override);
                        let a = self.regs[usize::from(reg)];
                        let b = self.read_rm16(ea);
                        let res = self.alu16(alu_op, a, b);
                        if alu_op != 7 {
                            self.regs[usize::from(reg)] = res;
                        }
                    }
                    4 => {
                        let imm = self.fetch_u8();
                        let a = self.get_reg8(0);
                        let res = self.alu8(alu_op, a, imm);
                        if alu_op != 7 {
                            self.set_reg8(0, res);
                        }
                    }
                    _ => {
                        let imm = self.fetch_u16();
                        let a = self.regs[0];
                        let res = self.alu16(alu_op, a, imm);
                        if alu_op != 7 {
                            self.regs[0] = res;
                        }
                    }
                }
            }
            // PUSH / POP segment registers
            0x06 => self.push16(self.sregs[Self::ES]),
            0x07 => {
                let sel = self.pop16();
                self.validate_pmode_sreg(Self::ES, sel)?;
                self.sregs[Self::ES] = sel;
            }
            0x0E => self.push16(self.sregs[Self::CS]),
            0x16 => self.push16(self.sregs[Self::SS]),
            0x17 => {
                let sel = self.pop16();
                self.validate_pmode_sreg(Self::SS, sel)?;
                self.sregs[Self::SS] = sel;
            }
            0x1E => self.push16(self.sregs[Self::DS]),
            0x1F => {
                let sel = self.pop16();
                self.validate_pmode_sreg(Self::DS, sel)?;
                self.sregs[Self::DS] = sel;
            }
            // INC / DEC r16
            0x40..=0x47 => {
                let r = usize::from(opcode - 0x40);
                let cf = self.flags & flags::CF;
                self.regs[r] = self.alu16(0, self.regs[r], 1);
                self.flags = (self.flags & !flags::CF) | cf;
            }
            0x48..=0x4F => {
                let r = usize::from(opcode - 0x48);
                let cf = self.flags & flags::CF;
                self.regs[r] = self.alu16(5, self.regs[r], 1);
                self.flags = (self.flags & !flags::CF) | cf;
            }
            // PUSH / POP r16
            0x50..=0x57 => {
                let r = usize::from(opcode - 0x50);
                self.push16(self.regs[r]);
            }
            0x58..=0x5F => {
                let r = usize::from(opcode - 0x58);
                self.regs[r] = self.pop16();
            }
            // 80286 PUSHA / POPA / PUSH imm / IMUL imm (strictly gated on cpu_286_enabled!)
            0x60 if self.cpu_286_enabled => {
                let saved_sp = self.regs[4];
                let ax = self.regs[0];
                let cx = self.regs[1];
                let dx = self.regs[2];
                let bx = self.regs[3];
                let bp = self.regs[5];
                let si = self.regs[6];
                let di = self.regs[7];
                for v in [ax, cx, dx, bx, saved_sp, bp, si, di] {
                    self.push16(v);
                }
            }
            0x61 if self.cpu_286_enabled => {
                self.regs[7] = self.pop16();
                self.regs[6] = self.pop16();
                self.regs[5] = self.pop16();
                let _discard_sp = self.pop16();
                self.regs[3] = self.pop16();
                self.regs[2] = self.pop16();
                self.regs[1] = self.pop16();
                self.regs[0] = self.pop16();
            }
            0x68 if self.cpu_286_enabled => {
                let imm = self.fetch_u16();
                self.push16(imm);
            }
            0x6A if self.cpu_286_enabled => {
                let imm = Self::sign_ext_u8_to_u16(self.fetch_u8());
                self.push16(imm);
            }
            0x69 | 0x6B if self.cpu_286_enabled => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let lhs = i32::from(self.read_rm16(ea).cast_signed());
                let rhs = if opcode == 0x69 {
                    i32::from(self.fetch_u16().cast_signed())
                } else {
                    i32::from(Self::sign_ext_u8_to_u16(self.fetch_u8()).cast_signed())
                };
                let prod = lhs * rhs;
                let pb = prod.to_le_bytes();
                let res = u16::from_le_bytes([pb[0], pb[1]]);
                self.regs[usize::from(reg)] = res;
                if i32::from(res.cast_signed()) == prod {
                    self.flags &= !(flags::CF | flags::OF);
                } else {
                    self.flags |= flags::CF | flags::OF;
                }
            }
            // Jcc short rel8
            0x70..=0x7F => {
                let rel = Self::sign_ext_u8_to_u16(self.fetch_u8());
                let cond = self.eval_cc(opcode & 0x0F);
                self.record_branch(lin_pc, cond);
                if cond {
                    self.ip = self.ip.wrapping_add(rel);
                }
            }
            // Group 1: ALU r/m, imm
            0x80..=0x83 => {
                let (ea, subop) = self.decode_modrm(seg_override);
                if opcode == 0x80 || opcode == 0x82 {
                    let imm = self.fetch_u8();
                    let a = self.read_rm8(ea);
                    let res = self.alu8(subop, a, imm);
                    if subop != 7 {
                        self.write_rm8(ea, res);
                    }
                } else {
                    let imm = if opcode == 0x81 {
                        self.fetch_u16()
                    } else {
                        Self::sign_ext_u8_to_u16(self.fetch_u8())
                    };
                    let a = self.read_rm16(ea);
                    let res = self.alu16(subop, a, imm);
                    if subop != 7 {
                        self.write_rm16(ea, res);
                    }
                }
            }
            // TEST r/m, r
            0x84 => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let v = self.read_rm8(ea) & self.get_reg8(reg);
                self.set_logic_flags8(v);
            }
            0x85 => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let v = self.read_rm16(ea) & self.regs[usize::from(reg)];
                self.set_logic_flags16(v);
            }
            // XCHG r/m, r
            0x86 => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let a = self.read_rm8(ea);
                let b = self.get_reg8(reg);
                self.write_rm8(ea, b);
                self.set_reg8(reg, a);
            }
            0x87 => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let a = self.read_rm16(ea);
                let b = self.regs[usize::from(reg)];
                self.write_rm16(ea, b);
                self.regs[usize::from(reg)] = a;
            }
            // MOV r/m, r & r, r/m & sreg
            0x88 => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let v = self.get_reg8(reg);
                self.write_rm8(ea, v);
            }
            0x89 => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let v = self.regs[usize::from(reg)];
                self.write_rm16(ea, v);
            }
            0x8A => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let v = self.read_rm8(ea);
                self.set_reg8(reg, v);
            }
            0x8B => {
                let (ea, reg) = self.decode_modrm(seg_override);
                let v = self.read_rm16(ea);
                self.regs[usize::from(reg)] = v;
            }
            0x8C => {
                let (ea, sreg) = self.decode_modrm(seg_override);
                let v = self.sregs[usize::from(sreg & 3)];
                self.write_rm16(ea, v);
            }
            0x8D => {
                let (ea, reg) = self.decode_modrm(seg_override);
                if let RmAddr::Mem(_, off) = ea {
                    self.regs[usize::from(reg)] = off;
                }
            }
            0x8E => {
                let (ea, sreg) = self.decode_modrm(seg_override);
                let v = self.read_rm16(ea);
                let s_idx = usize::from(sreg & 3);
                self.validate_pmode_sreg(s_idx, v)?;
                self.sregs[s_idx] = v;
            }
            0x8F => {
                let (ea, _) = self.decode_modrm(seg_override);
                let v = self.pop16();
                self.write_rm16(ea, v);
            }
            // NOP / XCHG AX, r16
            0x90 => {}
            0x91..=0x97 => {
                let r = usize::from(opcode - 0x90);
                self.regs.swap(0, r);
            }
            // CBW / CWD / PUSHF / POPF
            0x98 => {
                let al = self.regs[0].to_le_bytes()[0];
                self.regs[0] = Self::sign_ext_u8_to_u16(al);
            }
            0x99 => {
                self.regs[2] = if (self.regs[0] & 0x8000) != 0 {
                    0xFFFF
                } else {
                    0x0000
                };
            }
            0x9A => {
                let new_ip = self.fetch_u16();
                let new_cs = self.fetch_u16();
                self.validate_pmode_sreg(Self::CS, new_cs)?;
                self.push16(self.sregs[Self::CS]);
                self.push16(self.ip);
                self.ip = new_ip;
                self.sregs[Self::CS] = new_cs;
            }
            0x9C => self.push16(self.flags),
            0x9D => self.flags = self.pop16() | 0x0002,
            // MOV AL/AX, moffs & moffs, AL/AX
            0xA0 => {
                let off = self.fetch_u16();
                let seg = self.sregs[seg_override.unwrap_or(Self::DS)];
                let _ = self.resolve_pmode_or_real(seg, off, false);
                let v = self.read_u8(seg, off);
                self.set_reg8(0, v);
            }
            0xA1 => {
                let off = self.fetch_u16();
                let seg = self.sregs[seg_override.unwrap_or(Self::DS)];
                let _ = self.resolve_pmode_or_real(seg, off, false);
                self.regs[0] = self.read_u16(seg, off);
            }
            0xA2 => {
                let off = self.fetch_u16();
                let seg = self.sregs[seg_override.unwrap_or(Self::DS)];
                self.write_u8(seg, off, self.get_reg8(0));
            }
            0xA3 => {
                let off = self.fetch_u16();
                let seg = self.sregs[seg_override.unwrap_or(Self::DS)];
                self.write_u16(seg, off, self.regs[0]);
            }
            // String instructions: MOVSB/W, CMPSB/W, STOSB/W, LODSB/W, SCASB/W
            0xA4 | 0xA5 | 0xA6 | 0xA7 | 0xAA | 0xAB | 0xAC | 0xAD | 0xAE | 0xAF => {
                self.exec_string_op(opcode, rep_mode, seg_override);
            }
            // TEST AL/AX, imm
            0xA8 => {
                let imm = self.fetch_u8();
                self.set_logic_flags8(self.get_reg8(0) & imm);
            }
            0xA9 => {
                let imm = self.fetch_u16();
                self.set_logic_flags16(self.regs[0] & imm);
            }
            // MOV r8, imm8 / MOV r16, imm16
            0xB0..=0xB7 => {
                let imm = self.fetch_u8();
                self.set_reg8(opcode - 0xB0, imm);
            }
            0xB8..=0xBF => {
                let imm = self.fetch_u16();
                self.regs[usize::from(opcode - 0xB8)] = imm;
            }
            // 80286 Shifts by imm8 (0xC0 / 0xC1)
            0xC0 | 0xC1 if self.cpu_286_enabled => {
                let (ea, subop) = self.decode_modrm(seg_override);
                let count = self.fetch_u8() & 0x1F;
                if opcode == 0xC0 {
                    let v = self.read_rm8(ea);
                    let r = self.shift8(subop, v, count);
                    self.write_rm8(ea, r);
                } else {
                    let v = self.read_rm16(ea);
                    let r = self.shift16(subop, v, count);
                    self.write_rm16(ea, r);
                }
            }
            // RET imm16 / RET / RETF imm16 / RETF / IRET
            0xC2 => {
                let pop_bytes = self.fetch_u16();
                self.ip = self.pop16();
                self.regs[4] = self.regs[4].wrapping_add(pop_bytes);
            }
            0xC3 => {
                self.ip = self.pop16();
            }
            0xC4 | 0xC5 => {
                let (ea, reg) = self.decode_modrm(seg_override);
                if let RmAddr::Mem(seg, off) = ea {
                    let _ = self.resolve_pmode_or_real(seg, off, false);
                    let new_off = self.read_u16(seg, off);
                    let new_seg = self.read_u16(seg, off.wrapping_add(2));
                    let dst_sreg = if opcode == 0xC4 { Self::ES } else { Self::DS };
                    self.validate_pmode_sreg(dst_sreg, new_seg)?;
                    self.regs[usize::from(reg)] = new_off;
                    self.sregs[dst_sreg] = new_seg;
                }
            }
            0xC6 => {
                let (ea, _) = self.decode_modrm(seg_override);
                let imm = self.fetch_u8();
                self.write_rm8(ea, imm);
            }
            0xC7 => {
                let (ea, _) = self.decode_modrm(seg_override);
                let imm = self.fetch_u16();
                self.write_rm16(ea, imm);
            }
            // 80286 ENTER / LEAVE (0xC8 / 0xC9)
            0xC8 if self.cpu_286_enabled => {
                let alloc_bytes = self.fetch_u16();
                let _nesting = self.fetch_u8();
                self.push16(self.regs[5]);
                let frame_ptr = self.regs[4];
                self.regs[4] = self.regs[4].wrapping_sub(alloc_bytes);
                self.regs[5] = frame_ptr;
            }
            0xC9 if self.cpu_286_enabled => {
                self.regs[4] = self.regs[5];
                self.regs[5] = self.pop16();
            }
            0xCA => {
                let pop_bytes = self.fetch_u16();
                let new_ip = self.pop16();
                let new_cs = self.pop16();
                self.validate_pmode_sreg(Self::CS, new_cs)?;
                self.ip = new_ip;
                self.sregs[Self::CS] = new_cs;
                self.regs[4] = self.regs[4].wrapping_add(pop_bytes);
            }
            0xCB => {
                let new_ip = self.pop16();
                let new_cs = self.pop16();
                self.validate_pmode_sreg(Self::CS, new_cs)?;
                self.ip = new_ip;
                self.sregs[Self::CS] = new_cs;
            }
            0xCF => {
                let new_ip = self.pop16();
                let new_cs = self.pop16();
                self.validate_pmode_sreg(Self::CS, new_cs)?;
                self.ip = new_ip;
                self.sregs[Self::CS] = new_cs;
                self.flags = self.pop16() | 0x0002;
            }
            // INT imm8
            0xCD => {
                let vec_no = self.fetch_u8();
                self.handle_interrupt(vec_no)?;
            }
            // Shifts & Rotates: D0/D1 (by 1), D2/D3 (by CL)
            0xD0..=0xD3 => {
                let (ea, subop) = self.decode_modrm(seg_override);
                let count = if opcode >= 0xD2 {
                    self.get_reg8(1)
                } else {
                    1
                };
                if (opcode & 1) == 0 {
                    let v = self.read_rm8(ea);
                    let r = self.shift8(subop, v, count);
                    self.write_rm8(ea, r);
                } else {
                    let v = self.read_rm16(ea);
                    let r = self.shift16(subop, v, count);
                    self.write_rm16(ea, r);
                }
            }
            0xD7 => {
                let seg = self.sregs[seg_override.unwrap_or(Self::DS)];
                let off = self.regs[3].wrapping_add(u16::from(self.get_reg8(0)));
                let _ = self.resolve_pmode_or_real(seg, off, false);
                let v = self.read_u8(seg, off);
                self.set_reg8(0, v);
            }
            // LOOPNE / LOOPE / LOOP / JCXZ
            0xE0..=0xE3 => {
                let rel = Self::sign_ext_u8_to_u16(self.fetch_u8());
                let take = if opcode == 0xE3 {
                    self.regs[1] == 0
                } else {
                    self.regs[1] = self.regs[1].wrapping_sub(1);
                    match opcode {
                        0xE0 => self.regs[1] != 0 && (self.flags & flags::ZF) == 0,
                        0xE1 => self.regs[1] != 0 && (self.flags & flags::ZF) != 0,
                        _ => self.regs[1] != 0,
                    }
                };
                self.record_branch(lin_pc, take);
                if take {
                    self.ip = self.ip.wrapping_add(rel);
                }
            }
            // CALL rel16 / JMP rel16 / JMP ptr16:16 / JMP rel8
            0xE8 => {
                let rel = self.fetch_u16();
                self.push16(self.ip);
                self.ip = self.ip.wrapping_add(rel);
            }
            0xE9 => {
                let rel = self.fetch_u16();
                self.ip = self.ip.wrapping_add(rel);
            }
            0xEA => {
                let new_ip = self.fetch_u16();
                let new_cs = self.fetch_u16();
                self.validate_pmode_sreg(Self::CS, new_cs)?;
                self.ip = new_ip;
                self.sregs[Self::CS] = new_cs;
            }
            0xEB => {
                let rel = Self::sign_ext_u8_to_u16(self.fetch_u8());
                self.ip = self.ip.wrapping_add(rel);
            }
            // CMC / CLC / STC / CLI / STI / CLD / STD
            0xF5 => self.flags ^= flags::CF,
            0xF8 => self.flags &= !flags::CF,
            0xF9 => self.flags |= flags::CF,
            0xFA => self.flags &= !flags::IF,
            0xFB => self.flags |= flags::IF,
            0xFC => self.flags &= !flags::DF,
            0xFD => self.flags |= flags::DF,
            // Group 3: TEST / NOT / NEG / MUL / IMUL / DIV / IDIV
            0xF6 => {
                let (ea, subop) = self.decode_modrm(seg_override);
                self.exec_grp3_byte(ea, subop, start_cs, start_ip)?;
            }
            0xF7 => {
                let (ea, subop) = self.decode_modrm(seg_override);
                self.exec_grp3_word(ea, subop, start_cs, start_ip)?;
            }
            // Group 4 / 5: INC / DEC / CALL / JMP / PUSH r/m
            0xFE | 0xFF => {
                let is_word = opcode == 0xFF;
                let (ea, subop) = self.decode_modrm(seg_override);
                self.exec_grp45(is_word, ea, subop, start_cs, start_ip)?;
            }
            other => {
                return Err(ToolError::Emulator {
                    cs: start_cs,
                    ip: start_ip,
                    code: ErrCode::BadExeFormat,
                    message: format!("unsupported 8086 opcode 0x{other:02X}"),
                });
            }
        }

        if let Some(fault) = self.gp_fault.take() {
            return Err(ToolError::Emulator {
                cs: start_cs,
                ip: start_ip,
                code: ErrCode::AccessDenied,
                message: fault,
            });
        }

        Ok(())
    }

    fn record_branch(&mut self, lin_pc: u32, taken: bool) {
        let idx = usize::try_from(lin_pc).unwrap_or(0);
        let w = idx >> 6;
        let mask = 1_u64 << (idx & 63);
        if taken {
            if (self.br_taken_bits[w] & mask) == 0 {
                self.br_taken_bits[w] |= mask;
                self.coverage.branch_edges.entry(lin_pc).or_insert((false, false)).0 = true;
            }
        } else if (self.br_not_taken_bits[w] & mask) == 0 {
            self.br_not_taken_bits[w] |= mask;
            self.coverage.branch_edges.entry(lin_pc).or_insert((false, false)).1 = true;
        }
    }

    fn decode_modrm(&mut self, seg_override: Option<usize>) -> (RmAddr, u8) {
        let modrm = self.fetch_u8();
        let mode = (modrm >> 6) & 3;
        let reg = (modrm >> 3) & 7;
        let rm = modrm & 7;

        if mode == 3 {
            return (RmAddr::Reg(rm), reg);
        }

        let (base_off, def_seg) = if mode == 0 && rm == 6 {
            (self.fetch_u16(), Self::DS)
        } else {
            let b = match rm {
                0 => self.regs[3].wrapping_add(self.regs[6]), // BX + SI
                1 => self.regs[3].wrapping_add(self.regs[7]), // BX + DI
                2 => self.regs[5].wrapping_add(self.regs[6]), // BP + SI
                3 => self.regs[5].wrapping_add(self.regs[7]), // BP + DI
                4 => self.regs[6],                            // SI
                5 => self.regs[7],                            // DI
                6 => self.regs[5],                            // BP
                _ => self.regs[3],                            // BX
            };
            let dseg = if matches!(rm, 2 | 3 | 6) {
                Self::SS
            } else {
                Self::DS
            };
            (b, dseg)
        };

        let disp = match mode {
            1 => Self::sign_ext_u8_to_u16(self.fetch_u8()),
            2 => self.fetch_u16(),
            _ => 0,
        };
        let seg_val = self.sregs[seg_override.unwrap_or(def_seg)];
        (RmAddr::Mem(seg_val, base_off.wrapping_add(disp)), reg)
    }

    const fn sign_ext_u8_to_u16(b: u8) -> u16 {
        let hi = if (b & 0x80) != 0 { 0xFF } else { 0x00 };
        u16::from_le_bytes([b, hi])
    }

    fn read_rm8(&mut self, ea: RmAddr) -> u8 {
        match ea {
            RmAddr::Reg(r) => self.get_reg8(r),
            RmAddr::Mem(seg, off) => {
                let _ = self.resolve_pmode_or_real(seg, off, false);
                self.read_u8(seg, off)
            }
        }
    }

    fn write_rm8(&mut self, ea: RmAddr, val: u8) {
        match ea {
            RmAddr::Reg(r) => self.set_reg8(r, val),
            RmAddr::Mem(seg, off) => self.write_u8(seg, off, val),
        }
    }

    fn read_rm16(&mut self, ea: RmAddr) -> u16 {
        match ea {
            RmAddr::Reg(r) => self.regs[usize::from(r)],
            RmAddr::Mem(seg, off) => {
                let _ = self.resolve_pmode_or_real(seg, off, false);
                let _ = self.resolve_pmode_or_real(seg, off.wrapping_add(1), false);
                self.read_u16(seg, off)
            }
        }
    }

    fn write_rm16(&mut self, ea: RmAddr, val: u16) {
        match ea {
            RmAddr::Reg(r) => self.regs[usize::from(r)] = val,
            RmAddr::Mem(seg, off) => self.write_u16(seg, off, val),
        }
    }

    const fn set_szp8(&mut self, v: u8) {
        self.flags &= !(flags::ZF | flags::SF | flags::PF);
        if v == 0 {
            self.flags |= flags::ZF;
        }
        if (v & 0x80) != 0 {
            self.flags |= flags::SF;
        }
        if v.count_ones().is_multiple_of(2) {
            self.flags |= flags::PF;
        }
    }

    const fn set_szp16(&mut self, v: u16) {
        self.flags &= !(flags::ZF | flags::SF | flags::PF);
        if v == 0 {
            self.flags |= flags::ZF;
        }
        if (v & 0x8000) != 0 {
            self.flags |= flags::SF;
        }
        if v.to_le_bytes()[0].count_ones().is_multiple_of(2) {
            self.flags |= flags::PF;
        }
    }

    const fn set_logic_flags8(&mut self, v: u8) {
        self.flags &= !(flags::CF | flags::OF | flags::AF);
        self.set_szp8(v);
    }

    const fn set_logic_flags16(&mut self, v: u16) {
        self.flags &= !(flags::CF | flags::OF | flags::AF);
        self.set_szp16(v);
    }

    fn alu8(&mut self, op: u8, a: u8, b: u8) -> u8 {
        let cf_in = u16::from((self.flags & flags::CF) != 0);
        match op {
            0 | 2 => {
                let c = if op == 2 { cf_in } else { 0 };
                let sum = u16::from(a) + u16::from(b) + c;
                let res = sum.to_le_bytes()[0];
                self.set_szp8(res);
                self.flags &= !(flags::CF | flags::OF);
                if sum > 0xFF {
                    self.flags |= flags::CF;
                }
                if ((!(a ^ b) & (a ^ res)) & 0x80) != 0 {
                    self.flags |= flags::OF;
                }
                res
            }
            1 => {
                let r = a | b;
                self.set_logic_flags8(r);
                r
            }
            4 => {
                let r = a & b;
                self.set_logic_flags8(r);
                r
            }
            6 => {
                let r = a ^ b;
                self.set_logic_flags8(r);
                r
            }
            _ => {
                // 3 = SBB, 5 = SUB, 7 = CMP
                let c = if op == 3 { cf_in } else { 0 };
                let diff = i16::from(a) - i16::from(b) - c.cast_signed();
                let res = diff.to_le_bytes()[0];
                self.set_szp8(res);
                self.flags &= !(flags::CF | flags::OF);
                if diff < 0 {
                    self.flags |= flags::CF;
                }
                if (((a ^ b) & (a ^ res)) & 0x80) != 0 {
                    self.flags |= flags::OF;
                }
                res
            }
        }
    }

    fn alu16(&mut self, op: u8, a: u16, b: u16) -> u16 {
        let cf_in = u32::from((self.flags & flags::CF) != 0);
        match op {
            0 | 2 => {
                let c = if op == 2 { cf_in } else { 0 };
                let sum = u32::from(a) + u32::from(b) + c;
                let bytes = sum.to_le_bytes();
                let res = u16::from_le_bytes([bytes[0], bytes[1]]);
                self.set_szp16(res);
                self.flags &= !(flags::CF | flags::OF);
                if sum > 0xFFFF {
                    self.flags |= flags::CF;
                }
                if ((!(a ^ b) & (a ^ res)) & 0x8000) != 0 {
                    self.flags |= flags::OF;
                }
                res
            }
            1 => {
                let r = a | b;
                self.set_logic_flags16(r);
                r
            }
            4 => {
                let r = a & b;
                self.set_logic_flags16(r);
                r
            }
            6 => {
                let r = a ^ b;
                self.set_logic_flags16(r);
                r
            }
            _ => {
                let c = if op == 3 { cf_in } else { 0 };
                let diff = i32::from(a) - i32::from(b) - c.cast_signed();
                let bytes = diff.to_le_bytes();
                let res = u16::from_le_bytes([bytes[0], bytes[1]]);
                self.set_szp16(res);
                self.flags &= !(flags::CF | flags::OF);
                if diff < 0 {
                    self.flags |= flags::CF;
                }
                if (((a ^ b) & (a ^ res)) & 0x8000) != 0 {
                    self.flags |= flags::OF;
                }
                res
            }
        }
    }

    fn shift8(&mut self, subop: u8, mut v: u8, count: u8) -> u8 {
        for _ in 0..count {
            match subop {
                0 => {
                    let msb = (v >> 7) & 1;
                    v = (v << 1) | msb;
                    self.set_cf(msb != 0);
                }
                1 => {
                    let lsb = v & 1;
                    v = (v >> 1) | (lsb << 7);
                    self.set_cf(lsb != 0);
                }
                2 => {
                    let old_cf = u8::from((self.flags & flags::CF) != 0);
                    let msb = (v >> 7) & 1;
                    v = (v << 1) | old_cf;
                    self.set_cf(msb != 0);
                }
                3 => {
                    let old_cf = u8::from((self.flags & flags::CF) != 0);
                    let lsb = v & 1;
                    v = (v >> 1) | (old_cf << 7);
                    self.set_cf(lsb != 0);
                }
                4 | 6 => {
                    let msb = (v >> 7) & 1;
                    v <<= 1;
                    self.set_cf(msb != 0);
                    self.set_szp8(v);
                }
                5 => {
                    let lsb = v & 1;
                    v >>= 1;
                    self.set_cf(lsb != 0);
                    self.set_szp8(v);
                }
                _ => {
                    let lsb = v & 1;
                    v = (v.cast_signed() >> 1).cast_unsigned();
                    self.set_cf(lsb != 0);
                    self.set_szp8(v);
                }
            }
        }
        v
    }

    fn shift16(&mut self, subop: u8, mut v: u16, count: u8) -> u16 {
        for _ in 0..count {
            match subop {
                0 => {
                    let msb = (v >> 15) & 1;
                    v = (v << 1) | msb;
                    self.set_cf(msb != 0);
                }
                1 => {
                    let lsb = v & 1;
                    v = (v >> 1) | (lsb << 15);
                    self.set_cf(lsb != 0);
                }
                2 => {
                    let old_cf = u16::from((self.flags & flags::CF) != 0);
                    let msb = (v >> 15) & 1;
                    v = (v << 1) | old_cf;
                    self.set_cf(msb != 0);
                }
                3 => {
                    let old_cf = u16::from((self.flags & flags::CF) != 0);
                    let lsb = v & 1;
                    v = (v >> 1) | (old_cf << 15);
                    self.set_cf(lsb != 0);
                }
                4 | 6 => {
                    let msb = (v >> 15) & 1;
                    v <<= 1;
                    self.set_cf(msb != 0);
                    self.set_szp16(v);
                }
                5 => {
                    let lsb = v & 1;
                    v >>= 1;
                    self.set_cf(lsb != 0);
                    self.set_szp16(v);
                }
                _ => {
                    let lsb = v & 1;
                    v = (v.cast_signed() >> 1).cast_unsigned();
                    self.set_cf(lsb != 0);
                    self.set_szp16(v);
                }
            }
        }
        v
    }

    const fn set_cf(&mut self, cond: bool) {
        if cond {
            self.flags |= flags::CF;
        } else {
            self.flags &= !flags::CF;
        }
    }

    const fn eval_cc(&self, cc: u8) -> bool {
        let cf = (self.flags & flags::CF) != 0;
        let zf = (self.flags & flags::ZF) != 0;
        let sf = (self.flags & flags::SF) != 0;
        let of = (self.flags & flags::OF) != 0;
        let pf = (self.flags & flags::PF) != 0;
        match cc {
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

    fn exec_grp3_byte(&mut self, ea: RmAddr, subop: u8, cs: u16, ip: u16) -> Result<(), ToolError> {
        let v = self.read_rm8(ea);
        match subop {
            0 | 1 => {
                let imm = self.fetch_u8();
                self.set_logic_flags8(v & imm);
            }
            2 => self.write_rm8(ea, !v),
            3 => {
                let r = self.alu8(5, 0, v);
                self.write_rm8(ea, r);
            }
            4 => {
                let prod = u16::from(self.get_reg8(0)) * u16::from(v);
                self.regs[0] = prod;
                self.set_cf((prod & 0xFF00) != 0);
            }
            5 => {
                let prod = i16::from(self.get_reg8(0).cast_signed()) * i16::from(v.cast_signed());
                self.regs[0] = prod.cast_unsigned();
            }
            6 => {
                if v == 0 || (self.regs[0] / u16::from(v)) > 0xFF {
                    return Err(ToolError::Emulator {
                        cs,
                        ip,
                        code: ErrCode::DivByZero,
                        message: "8-bit DIV overflow or division by zero (INT 00h)".into(),
                    });
                }
                let q = (self.regs[0] / u16::from(v)).to_le_bytes()[0];
                let r = (self.regs[0] % u16::from(v)).to_le_bytes()[0];
                self.regs[0] = u16::from_le_bytes([q, r]);
            }
            _ => {
                let num = self.regs[0].cast_signed();
                let den = i16::from(v.cast_signed());
                if den == 0 || !(-128..=127).contains(&(num / den)) {
                    return Err(ToolError::Emulator {
                        cs,
                        ip,
                        code: ErrCode::DivByZero,
                        message: "8-bit IDIV overflow or division by zero (INT 00h)".into(),
                    });
                }
                let q = (num / den).to_le_bytes()[0];
                let r = (num % den).to_le_bytes()[0];
                self.regs[0] = u16::from_le_bytes([q, r]);
            }
        }
        Ok(())
    }

    fn exec_grp3_word(&mut self, ea: RmAddr, subop: u8, cs: u16, ip: u16) -> Result<(), ToolError> {
        let v = self.read_rm16(ea);
        match subop {
            0 | 1 => {
                let imm = self.fetch_u16();
                self.set_logic_flags16(v & imm);
            }
            2 => self.write_rm16(ea, !v),
            3 => {
                let r = self.alu16(5, 0, v);
                self.write_rm16(ea, r);
            }
            4 => {
                let prod = u32::from(self.regs[0]) * u32::from(v);
                let b = prod.to_le_bytes();
                self.regs[0] = u16::from_le_bytes([b[0], b[1]]);
                self.regs[2] = u16::from_le_bytes([b[2], b[3]]);
                self.set_cf(self.regs[2] != 0);
            }
            5 => {
                let prod = i32::from(self.regs[0].cast_signed()) * i32::from(v.cast_signed());
                let b = prod.to_le_bytes();
                self.regs[0] = u16::from_le_bytes([b[0], b[1]]);
                self.regs[2] = u16::from_le_bytes([b[2], b[3]]);
            }
            6 => {
                let num = (u32::from(self.regs[2]) << 16) | u32::from(self.regs[0]);
                if v == 0 || (num / u32::from(v)) > 0xFFFF {
                    return Err(ToolError::Emulator {
                        cs,
                        ip,
                        code: ErrCode::DivByZero,
                        message: "16-bit DIV quotient overflow or division by zero (INT 00h)".into(),
                    });
                }
                let qb = (num / u32::from(v)).to_le_bytes();
                let rb = (num % u32::from(v)).to_le_bytes();
                self.regs[0] = u16::from_le_bytes([qb[0], qb[1]]);
                self.regs[2] = u16::from_le_bytes([rb[0], rb[1]]);
            }
            _ => {
                let num = ((u32::from(self.regs[2]) << 16) | u32::from(self.regs[0])).cast_signed();
                let den = i32::from(v.cast_signed());
                if den == 0 || !(-32768..=32767).contains(&(num / den)) {
                    return Err(ToolError::Emulator {
                        cs,
                        ip,
                        code: ErrCode::DivByZero,
                        message: "16-bit IDIV quotient overflow or division by zero (INT 00h)".into(),
                    });
                }
                let qb = (num / den).to_le_bytes();
                let rb = (num % den).to_le_bytes();
                self.regs[0] = u16::from_le_bytes([qb[0], qb[1]]);
                self.regs[2] = u16::from_le_bytes([rb[0], rb[1]]);
            }
        }
        Ok(())
    }

    fn exec_grp45(
        &mut self,
        is_word: bool,
        ea: RmAddr,
        subop: u8,
        cs: u16,
        ip: u16,
    ) -> Result<(), ToolError> {
        if !is_word {
            let cf = self.flags & flags::CF;
            let v = self.read_rm8(ea);
            match subop {
                0 => {
                    let r = self.alu8(0, v, 1);
                    self.write_rm8(ea, r);
                    self.flags = (self.flags & !flags::CF) | cf;
                }
                1 => {
                    let r = self.alu8(5, v, 1);
                    self.write_rm8(ea, r);
                    self.flags = (self.flags & !flags::CF) | cf;
                }
                _ => {
                    return Err(ToolError::Emulator {
                        cs,
                        ip,
                        code: ErrCode::BadExeFormat,
                        message: format!("invalid 0xFE subop {subop}"),
                    });
                }
            }
            return Ok(());
        }

        let cf = self.flags & flags::CF;
        match subop {
            0 => {
                let v = self.read_rm16(ea);
                let r = self.alu16(0, v, 1);
                self.write_rm16(ea, r);
                self.flags = (self.flags & !flags::CF) | cf;
            }
            1 => {
                let v = self.read_rm16(ea);
                let r = self.alu16(5, v, 1);
                self.write_rm16(ea, r);
                self.flags = (self.flags & !flags::CF) | cf;
            }
            2 => {
                let tgt = self.read_rm16(ea);
                self.push16(self.ip);
                self.ip = tgt;
            }
            3 => {
                if let RmAddr::Mem(seg, off) = ea {
                    let _ = self.resolve_pmode_or_real(seg, off, false);
                    let tgt_ip = self.read_u16(seg, off);
                    let tgt_cs = self.read_u16(seg, off.wrapping_add(2));
                    self.validate_pmode_sreg(Self::CS, tgt_cs)?;
                    self.push16(self.sregs[Self::CS]);
                    self.push16(self.ip);
                    self.ip = tgt_ip;
                    self.sregs[Self::CS] = tgt_cs;
                }
            }
            4 => {
                let tgt = self.read_rm16(ea);
                self.ip = tgt;
            }
            5 => {
                if let RmAddr::Mem(seg, off) = ea {
                    let _ = self.resolve_pmode_or_real(seg, off, false);
                    let tgt_ip = self.read_u16(seg, off);
                    let tgt_cs = self.read_u16(seg, off.wrapping_add(2));
                    self.validate_pmode_sreg(Self::CS, tgt_cs)?;
                    self.ip = tgt_ip;
                    self.sregs[Self::CS] = tgt_cs;
                }
            }
            6 => {
                let v = self.read_rm16(ea);
                self.push16(v);
            }
            _ => {
                return Err(ToolError::Emulator {
                    cs,
                    ip,
                    code: ErrCode::BadExeFormat,
                    message: "invalid 0xFF /7 opcode".into(),
                });
            }
        }
        Ok(())
    }

    fn exec_string_op(&mut self, opcode: u8, rep_mode: u8, seg_override: Option<usize>) {
        let is_word = (opcode & 1) != 0;
        let step: u16 = if (self.flags & flags::DF) != 0 {
            if is_word {
                0xFFFE
            } else {
                0xFFFF
            }
        } else if is_word {
            2
        } else {
            1
        };
        let src_seg = self.sregs[seg_override.unwrap_or(Self::DS)];
        let dst_seg = self.sregs[Self::ES];
        let checks_zf = matches!(opcode, 0xA6 | 0xA7 | 0xAE | 0xAF);

        loop {
            if rep_mode != 0 && self.regs[1] == 0 {
                break;
            }
            match opcode {
                0xA4 => {
                    let b = self.read_u8(src_seg, self.regs[6]);
                    self.write_u8(dst_seg, self.regs[7], b);
                    self.regs[6] = self.regs[6].wrapping_add(step);
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
                0xA5 => {
                    let w = self.read_u16(src_seg, self.regs[6]);
                    self.write_u16(dst_seg, self.regs[7], w);
                    self.regs[6] = self.regs[6].wrapping_add(step);
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
                0xA6 => {
                    let a = self.read_u8(src_seg, self.regs[6]);
                    let b = self.read_u8(dst_seg, self.regs[7]);
                    let _ = self.alu8(7, a, b);
                    self.regs[6] = self.regs[6].wrapping_add(step);
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
                0xA7 => {
                    let a = self.read_u16(src_seg, self.regs[6]);
                    let b = self.read_u16(dst_seg, self.regs[7]);
                    let _ = self.alu16(7, a, b);
                    self.regs[6] = self.regs[6].wrapping_add(step);
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
                0xAA => {
                    self.write_u8(dst_seg, self.regs[7], self.get_reg8(0));
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
                0xAB => {
                    self.write_u16(dst_seg, self.regs[7], self.regs[0]);
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
                0xAC => {
                    let b = self.read_u8(src_seg, self.regs[6]);
                    self.set_reg8(0, b);
                    self.regs[6] = self.regs[6].wrapping_add(step);
                }
                0xAD => {
                    let w = self.read_u16(src_seg, self.regs[6]);
                    self.regs[0] = w;
                    self.regs[6] = self.regs[6].wrapping_add(step);
                }
                0xAE => {
                    let b = self.read_u8(dst_seg, self.regs[7]);
                    let _ = self.alu8(7, self.get_reg8(0), b);
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
                _ => {
                    let w = self.read_u16(dst_seg, self.regs[7]);
                    let _ = self.alu16(7, self.regs[0], w);
                    self.regs[7] = self.regs[7].wrapping_add(step);
                }
            }

            if rep_mode == 0 {
                break;
            }
            self.regs[1] = self.regs[1].wrapping_sub(1);
            if checks_zf {
                let zf = (self.flags & flags::ZF) != 0;
                if (rep_mode == 1 && !zf) || (rep_mode == 2 && zf) {
                    break;
                }
            }
        }
    }

    fn handle_interrupt(&mut self, vec_no: u8) -> Result<(), ToolError> {
        let ivt_lin = usize::from(vec_no) * 4;
        let isr_off = self.read_u16_lin(ivt_lin);
        let isr_seg = self.read_u16_lin(ivt_lin + 2);
        let rom_off = 0x0200_u16 + u16::from(vec_no) * 4;
        if isr_seg == Self::ROM_SEG && isr_off == rom_off {
            self.handle_rom_interrupt(vec_no)
        } else {
            self.push16(self.flags);
            self.push16(self.sregs[Self::CS]);
            self.push16(self.ip);
            self.flags &= !flags::IF;
            self.sregs[Self::CS] = isr_seg;
            self.ip = isr_off;
            Ok(())
        }
    }

    fn terminate_dos_process(&mut self, code: u8) {
        if self.finish_child_exec(code) {
            return;
        }
        let int22_off = self.read_u16_lin(0x22 * 4);
        let int22_seg = self.read_u16_lin(0x22 * 4 + 2);
        let rom22_off = 0x0200_u16 + 0x22 * 4;
        if !(int22_seg == Self::ROM_SEG && int22_off == rom22_off) {
            self.write_u16_lin(0x22 * 4, rom22_off);
            self.write_u16_lin(0x22 * 4 + 2, Self::ROM_SEG);
            if self.corrupt_high_ram_on_exec {
                let trn_seg = self
                    .read_u16(Self::DEFAULT_PSP_SEG, 0x0002)
                    .wrapping_sub(0x0A20);
                self.write_u8(trn_seg, 0x0004, 0xCC);
            }
            self.last_child_exit_code = code;
            self.sregs[Self::CS] = int22_seg;
            self.ip = int22_off;
            return;
        }
        self.exit_code = code;
        self.halted = true;
    }

    #[allow(clippy::too_many_lines)]
    fn handle_rom_interrupt(&mut self, vec_no: u8) -> Result<(), ToolError> {
        match vec_no {
            0x10 => {
                let ah = self.get_reg8(4);
                match ah {
                    0x0E => self.stdout.push(self.get_reg8(0)),
                    0x0F => {
                        self.set_reg8(0, 0x03);
                        self.set_reg8(4, 80);
                        self.set_reg8(7, 0);
                    }
                    0x03 => {
                        self.regs[2] = 0x0000;
                        self.regs[1] = 0x0607;
                    }
                    _ => {}
                }
            }
            0x16 => self.handle_bios_int16(),
            0x20 => self.terminate_dos_process(0),
            0x21 => {
                let ah = self.get_reg8(4);
                match ah {
                    0x00 => self.terminate_dos_process(0),
                    0x01 => {
                        let ch = self.read_stdin_char_for_dos();
                        self.set_reg8(0, ch);
                        if ch != 0x1A {
                            self.write_stdout_bytes_for_dos(&[ch]);
                        }
                    }
                    0x02 => {
                        let dl = self.get_reg8(2);
                        self.write_stdout_bytes_for_dos(&[dl]);
                        self.set_reg8(0, dl);
                    }
                    0x06 => {
                        let dl = self.get_reg8(2);
                        if dl == 0xFF {
                            let ch = self.read_stdin_char_for_dos();
                            if ch == 0x1A {
                                self.set_reg8(0, 0);
                                self.flags |= flags::ZF;
                            } else {
                                self.set_reg8(0, ch);
                                self.flags &= !flags::ZF;
                            }
                        } else {
                            self.write_stdout_bytes_for_dos(&[dl]);
                        }
                    }
                    0x07 | 0x08 => {
                        let ch = self.read_stdin_char_for_dos();
                        self.set_reg8(0, ch);
                    }
                    0x09 => {
                        let ds = self.sregs[Self::DS];
                        let mut dx = self.regs[2];
                        let mut out = Vec::new();
                        loop {
                            let b = self.read_u8(ds, dx);
                            if b == b'$' {
                                break;
                            }
                            out.push(b);
                            dx = dx.wrapping_add(1);
                        }
                        self.write_stdout_bytes_for_dos(&out);
                    }
                    0x0A => {
                        let ds = self.sregs[Self::DS];
                        let dx = self.regs[2];
                        let cap = self.read_u8(ds, dx);
                        let mut count = 0_u8;
                        while count < cap {
                            let ch = self.read_stdin_char_for_dos();
                            if matches!(ch, 0x1A | 0x0A) {
                                break;
                            }
                            if ch == 0x0D {
                                let _ = self.read_stdin_char_for_dos();
                                break;
                            }
                            self.write_u8(ds, dx.wrapping_add(2 + u16::from(count)), ch);
                            count += 1;
                        }
                        self.write_u8(ds, dx.wrapping_add(1), count);
                        self.write_u8(ds, dx.wrapping_add(2 + u16::from(count)), 0x0D);
                    }
                    0x0B => {
                        let ready = if let Some(of) = self.handles.get(&0)
                            && of.path != CON_IN_SENTINEL
                        {
                            self.vfs.get(&of.path).is_some_and(|b| of.pos < b.len())
                        } else {
                            self.stdin_pos < self.stdin.len()
                        };
                        self.set_reg8(0, if ready { 0xFF } else { 0x00 });
                    }
                    0x0E => self.set_reg8(0, 26),
                    0x0F..=0x13 | 0x16 | 0x17 | 0x27 | 0x28 => self.handle_fcb_call(ah),
                    0x19 => {
                        self.set_reg8(0, 0);
                    }
                    0x1A => {
                        self.dta = (self.sregs[Self::DS], self.regs[2]);
                    }
                    0x1C => {
                        let dl = self.get_reg8(2);
                        if dl > 2 {
                            self.set_reg8(0, 0xFF);
                        } else {
                            self.set_reg8(0, 1);
                            self.regs[1] = 512;
                            self.regs[2] = 313;
                            self.sregs[Self::DS] = 0xDEAD;
                        }
                    }
                    0x25 | 0x35 => {
                        let v = usize::from(self.get_reg8(0)) * 4;
                        if ah == 0x35 {
                            self.regs[3] = self.read_u16_lin(v);
                            self.sregs[Self::ES] = self.read_u16_lin(v + 2);
                        } else {
                            self.write_u16_lin(v, self.regs[2]);
                            self.write_u16_lin(v + 2, self.sregs[Self::DS]);
                        }
                    }
                    0x26 => {
                        let dst_psp = self.regs[2];
                        for i in 0..256_u16 {
                            let b = self.read_u8(Self::DEFAULT_PSP_SEG, i);
                            self.write_u8(dst_psp, i, b);
                        }
                        self.write_u8(dst_psp, 0, 0xCD);
                        self.write_u8(dst_psp, 1, 0x20);
                    }
                    0x29 => self.dos_parse_fcb(),
                    0x2A => {
                        self.regs[1] = 2026;
                        self.regs[2] = 0x0B15;
                        self.set_reg8(0, 6);
                    }
                    0x2B | 0x2D => self.set_reg8(0, 0),
                    0x2C => {
                        self.regs[1] = 0x0E1E;
                        self.regs[2] = 0;
                    }
                    0x2E => {
                        if (self.get_reg8(0) & 1) != 0 {
                            self.dos_ctrl_flags |= 0x02;
                        } else {
                            self.dos_ctrl_flags &= !0x02;
                        }
                    }
                    0x2F => {
                        self.sregs[Self::ES] = self.dta.0;
                        self.regs[3] = self.dta.1;
                    }
                    0x30 => {
                        let (maj, min) = self.era.dos_version_words();
                        self.set_reg8(0, maj);
                        self.set_reg8(4, min);
                        self.regs[3] = 0;
                        self.regs[1] = 0;
                    }
                    0x33 => match self.get_reg8(0) {
                        0x00 => self.set_reg8(2, self.dos_ctrl_flags & 0x01),
                        0x01 => {
                            if (self.get_reg8(2) & 0x01) != 0 {
                                self.dos_ctrl_flags |= 0x01;
                            } else {
                                self.dos_ctrl_flags &= !0x01;
                            }
                        }
                        _ => {}
                    },
                    0x36 => {
                        let dl = self.get_reg8(2);
                        if dl > 3 {
                            self.regs[0] = 0xFFFF;
                        } else {
                            self.regs[0] = 8;
                            self.regs[1] = 512;
                            self.regs[3] = self.fat16_free_clusters;
                            self.regs[2] = 65_000;
                        }
                    }
                    0x39..=0x3B => {
                        if self.era == DosEra::Dos125 {
                            self.regs[0] = ErrCode::InvalidFunction.code();
                            self.set_cf(true);
                        } else {
                            let raw = self.read_asciiz(self.sregs[Self::DS], self.regs[2]);
                            let key = self.norm_vfs_key(&raw);
                            match ah {
                                0x39 => {
                                    self.dirs.insert(key);
                                    self.set_cf(false);
                                }
                                0x3A => {
                                    if self.dirs.remove(&key) {
                                        self.set_cf(false);
                                    } else {
                                        self.last_dos_err = ErrCode::PathNotFound.code();
                                        self.regs[0] = self.last_dos_err;
                                        self.set_cf(true);
                                    }
                                }
                                _ => {
                                    if key.is_empty() || self.dirs.contains(&key) {
                                        self.cwd = key;
                                        self.set_cf(false);
                                    } else {
                                        self.last_dos_err = ErrCode::PathNotFound.code();
                                        self.regs[0] = self.last_dos_err;
                                        self.set_cf(true);
                                    }
                                }
                            }
                        }
                    }
                    0x3C | 0x3D => {
                        if self.era == DosEra::Dos125 {
                            self.set_reg8(0, 0xFF);
                            self.set_cf(true);
                        } else if ah == 0x3D
                            && self.era == DosEra::Dos200
                            && (self.get_reg8(0) & 0xF0) != 0
                        {
                            self.last_dos_err = ErrCode::InvalidFunction.code();
                            self.regs[0] = self.last_dos_err;
                            self.set_cf(true);
                        } else {
                            let path = self.read_asciiz(self.sregs[Self::DS], self.regs[2]);
                            if Self::is_nul_device(&path) {
                                match self.alloc_file_handle(OpenFile {
                                    path: String::from(NUL_SENTINEL),
                                    pos: 0,
                                }) {
                                    Ok(h) => {
                                        self.regs[0] = h;
                                        self.set_cf(false);
                                    }
                                    Err(e) => {
                                        self.last_dos_err = e.code();
                                        self.regs[0] = self.last_dos_err;
                                        self.set_cf(true);
                                    }
                                }
                                return Ok(());
                            }
                            let key = self.norm_vfs_key(&path);
                            if ah == 0x3C {
                                if self.vfs_meta.get(&key).is_some_and(|m| (m.attr & 0x01) != 0) {
                                    self.last_dos_err = ErrCode::AccessDenied.code();
                                    self.regs[0] = self.last_dos_err;
                                    self.set_cf(true);
                                    return Ok(());
                                }
                                self.vfs.insert(key.clone(), Vec::new());
                            } else if !self.vfs.contains_key(&key) {
                                self.last_dos_err = ErrCode::FileNotFound.code();
                                self.regs[0] = self.last_dos_err;
                                self.set_cf(true);
                                return Ok(());
                            }
                            match self.alloc_file_handle(OpenFile { path: key, pos: 0 }) {
                                Ok(h) => {
                                    self.regs[0] = h;
                                    self.set_cf(false);
                                }
                                Err(e) => {
                                    self.last_dos_err = e.code();
                                    self.regs[0] = self.last_dos_err;
                                    self.set_cf(true);
                                }
                            }
                        }
                    }
                    0x3E => {
                        let bx = self.regs[3];
                        if self.handles.remove(&bx).is_some() || bx < 5 {
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::InvalidHandle.code();
                            self.set_cf(true);
                        }
                    }
                    0x3F => {
                        let bx = self.regs[3];
                        let cx = usize::from(self.regs[1]);
                        let ds = self.sregs[Self::DS];
                        let dx = self.regs[2];
                        let use_stdin = bx == 0
                            && self
                                .handles
                                .get(&0)
                                .is_none_or(|of| of.path == CON_IN_SENTINEL);
                        if use_stdin {
                            let avail = (self.stdin.len() - self.stdin_pos).min(cx);
                            for i in 0..avail {
                                let b = self.stdin[self.stdin_pos + i];
                                self.write_u8(
                                    ds,
                                    dx.wrapping_add(u16::try_from(i).unwrap_or(0)),
                                    b,
                                );
                            }
                            self.stdin_pos += avail;
                            self.regs[0] = u16::try_from(avail).unwrap_or(0);
                            self.set_cf(false);
                        } else if let Some(of) = self.handles.get(&bx) {
                            if of.path == NUL_SENTINEL {
                                self.regs[0] = 0;
                                self.set_cf(false);
                                return Ok(());
                            }
                            let path_clone = of.path.clone();
                            let start_pos = of.pos;
                            let data = self.vfs.get(&path_clone).map_or(&[][..], Vec::as_slice);
                            let avail = data.len().saturating_sub(start_pos).min(cx);
                            let slice = data[start_pos..(start_pos + avail)].to_vec();
                            if let Some(of2) = self.handles.get_mut(&bx) {
                                of2.pos += avail;
                            }
                            for (i, &b) in slice.iter().enumerate() {
                                self.write_u8(ds, dx.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
                            }
                            self.regs[0] = u16::try_from(avail).unwrap_or(0);
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::InvalidHandle.code();
                            self.set_cf(true);
                        }
                    }
                    0x40 => {
                        let bx = self.regs[3];
                        let cx = usize::from(self.regs[1]);
                        let ds = self.sregs[Self::DS];
                        let dx = self.regs[2];
                        let mut buf = Vec::with_capacity(cx);
                        for i in 0..cx {
                            buf.push(
                                self.read_u8(ds, dx.wrapping_add(u16::try_from(i).unwrap_or(0))),
                            );
                        }
                        let use_stdout = bx == 1
                            && self
                                .handles
                                .get(&1)
                                .is_none_or(|of| of.path == CON_OUT_SENTINEL);
                        let use_stderr = bx == 2 && !self.handles.contains_key(&2);
                        if use_stdout {
                            self.stdout.extend_from_slice(&buf);
                            self.regs[0] = self.regs[1];
                            self.set_cf(false);
                        } else if use_stderr {
                            self.stderr.extend_from_slice(&buf);
                            self.regs[0] = self.regs[1];
                            self.set_cf(false);
                        } else if let Some(of) = self.handles.get(&bx) {
                            if of.path == NUL_SENTINEL {
                                self.regs[0] = self.regs[1];
                                self.set_cf(false);
                                return Ok(());
                            }
                            let path_clone = of.path.clone();
                            let pos = of.pos;
                            let entry = self.vfs.entry(path_clone).or_default();
                            if cx == 0 {
                                entry.resize(pos, 0);
                            } else {
                                if pos + buf.len() > entry.len() {
                                    entry.resize(pos + buf.len(), 0);
                                }
                                entry[pos..(pos + buf.len())].copy_from_slice(&buf);
                                if let Some(of2) = self.handles.get_mut(&bx) {
                                    of2.pos += buf.len();
                                }
                            }
                            self.regs[0] = self.regs[1];
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::InvalidHandle.code();
                            self.set_cf(true);
                        }
                    }
                    0x41 => {
                        let raw = self.read_asciiz(self.sregs[Self::DS], self.regs[2]);
                        let key = self.norm_vfs_key(&raw);
                        if self.vfs_meta.get(&key).is_some_and(|m| (m.attr & 0x01) != 0) {
                            self.last_dos_err = ErrCode::AccessDenied.code();
                            self.regs[0] = self.last_dos_err;
                            self.set_cf(true);
                        } else if self.vfs.remove(&key).is_some() {
                            self.vfs_meta.remove(&key);
                            self.set_cf(false);
                        } else {
                            self.last_dos_err = ErrCode::FileNotFound.code();
                            self.regs[0] = self.last_dos_err;
                            self.set_cf(true);
                        }
                    }
                    0x42 => {
                        let bx = self.regs[3];
                        let origin = self.get_reg8(0);
                        let off_i32 =
                            ((u32::from(self.regs[1]) << 16) | u32::from(self.regs[2])).cast_signed();
                        if let Some(of) = self.handles.get(&bx) {
                            let file_len = self.vfs.get(&of.path).map_or(0, Vec::len);
                            let base = match origin {
                                0 => 0_i64,
                                1 => i64::try_from(of.pos).unwrap_or(0),
                                _ => i64::try_from(file_len).unwrap_or(0),
                            };
                            let new_pos = (base + i64::from(off_i32)).max(0);
                            let new_usize = usize::try_from(new_pos).unwrap_or(0);
                            if let Some(of_mut) = self.handles.get_mut(&bx) {
                                of_mut.pos = new_usize;
                            }
                            let pos_u32 = u32::try_from(new_usize).unwrap_or(0);
                            let b = pos_u32.to_le_bytes();
                            self.regs[0] = u16::from_le_bytes([b[0], b[1]]);
                            self.regs[2] = u16::from_le_bytes([b[2], b[3]]);
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::InvalidHandle.code();
                            self.set_cf(true);
                        }
                    }
                    0x43 => {
                        let raw = self.read_asciiz(self.sregs[Self::DS], self.regs[2]);
                        let key = self.norm_vfs_key(&raw);
                        if self.get_reg8(0) == 1 {
                            if self.vfs.contains_key(&key) {
                                self.vfs_meta.entry(key).or_default().attr = self.get_reg8(1);
                                self.set_cf(false);
                            } else {
                                self.regs[0] = ErrCode::FileNotFound.code();
                                self.set_cf(true);
                            }
                        } else if self.dirs.contains(&key) {
                            self.regs[1] = 0x10;
                            self.set_cf(false);
                        } else if self.vfs.contains_key(&key) {
                            self.regs[1] =
                                u16::from(self.vfs_meta.get(&key).map_or(0x20, |m| m.attr));
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::FileNotFound.code();
                            self.set_cf(true);
                        }
                    }
                    0x44 => {
                        let bx = self.regs[3];
                        let is_file = self.handles.get(&bx).is_some_and(|of| {
                            of.path != CON_IN_SENTINEL && of.path != CON_OUT_SENTINEL
                        });
                        self.regs[2] = if is_file {
                            0x0000
                        } else if self.force_tty_console {
                            0x0080
                        } else {
                            0x0081
                        };
                        self.set_cf(false);
                    }
                    0x45 => {
                        let bx = self.regs[3];
                        let cloned = self.handles.get(&bx).cloned().unwrap_or_else(|| OpenFile {
                            path: String::from(if bx == 0 {
                                CON_IN_SENTINEL
                            } else {
                                CON_OUT_SENTINEL
                            }),
                            pos: 0,
                        });
                        match self.alloc_file_handle(cloned) {
                            Ok(h) => {
                                self.regs[0] = h;
                                self.set_cf(false);
                            }
                            Err(e) => {
                                self.regs[0] = e.code();
                                self.set_cf(true);
                            }
                        }
                    }
                    0x46 => {
                        let src = self.regs[3];
                        let dst = self.regs[1];
                        if let Some(of) = self.handles.get(&src).cloned() {
                            if (dst == 0 && of.path == CON_IN_SENTINEL)
                                || (dst == 1 && of.path == CON_OUT_SENTINEL)
                            {
                                self.handles.remove(&dst);
                            } else {
                                self.handles.insert(dst, of);
                            }
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::InvalidHandle.code();
                            self.set_cf(true);
                        }
                    }
                    0x47 => {
                        let ds = self.sregs[Self::DS];
                        let si = self.regs[6];
                        let cwd_bytes = self.cwd.as_bytes().to_vec();
                        for (i, &b) in cwd_bytes.iter().enumerate() {
                            self.write_u8(
                                ds,
                                si.wrapping_add(u16::try_from(i).unwrap_or(0)),
                                b,
                            );
                        }
                        self.write_u8(
                            ds,
                            si.wrapping_add(u16::try_from(cwd_bytes.len()).unwrap_or(0)),
                            0,
                        );
                        self.set_cf(false);
                    }
                    0x48..=0x4A => {
                        self.handle_dos_mem(ah);
                    }
                    0x4B => self.handle_dos_exec_4b00(),
                    0x31 | 0x4C => {
                        let code = self.get_reg8(0);
                        self.terminate_dos_process(code);
                    }
                    0x4D => {
                        self.regs[0] = u16::from(self.last_child_exit_code);
                    }
                    0x4E | 0x4F => self.handle_dos200_find(ah == 0x4E),
                    0x54 => {
                        self.set_reg8(0, u8::from((self.dos_ctrl_flags & 0x02) != 0));
                    }
                    0x56 => {
                        let old_k =
                            self.norm_vfs_key(&self.read_asciiz(self.sregs[Self::DS], self.regs[2]));
                        let new_k =
                            self.norm_vfs_key(&self.read_asciiz(self.sregs[Self::ES], self.regs[7]));
                        if let Some(bytes) = self.vfs.remove(&old_k) {
                            self.vfs.insert(new_k.clone(), bytes);
                            if let Some(meta) = self.vfs_meta.remove(&old_k) {
                                self.vfs_meta.insert(new_k, meta);
                            }
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::FileNotFound.code();
                            self.set_cf(true);
                        }
                    }
                    0x59 => {
                        self.regs[0] = self.last_dos_err.max(1);
                        self.regs[3] = 0x0801;
                        self.regs[1] = 0x0200;
                    }
                    0x5A => {
                        let ds = self.sregs[Self::DS];
                        let dx = self.regs[2];
                        let pfx = self.read_asciiz(ds, dx);
                        let id = self.tmp_counter;
                        self.tmp_counter = self.tmp_counter.wrapping_add(1);
                        let leaf = format!("WCTMP{id:03}.TMP");
                        let full = format!("{pfx}{leaf}");
                        for (i, b) in full.bytes().enumerate() {
                            self.write_u8(ds, dx.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
                        }
                        self.write_u8(
                            ds,
                            dx.wrapping_add(u16::try_from(full.len()).unwrap_or(0)),
                            0,
                        );
                        let key = self.norm_vfs_key(&full);
                        self.vfs.insert(key.clone(), Vec::new());
                        match self.alloc_file_handle(OpenFile { path: key, pos: 0 }) {
                            Ok(h) => {
                                self.regs[0] = h;
                                self.set_cf(false);
                            }
                            Err(e) => {
                                self.regs[0] = e.code();
                                self.set_cf(true);
                            }
                        }
                    }
                    0x60 => {
                        let raw = self.read_asciiz(self.sregs[Self::DS], self.regs[6]);
                        let key = self.norm_vfs_key(&raw);
                        let a_root = Self::drive_a_root();
                        let clean = key.strip_prefix(&a_root).unwrap_or(&key);
                        let canon = format!("{a_root}{clean}");
                        let es = self.sregs[Self::ES];
                        let di = self.regs[7];
                        for (i, b) in canon.bytes().enumerate() {
                            self.write_u8(es, di.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
                        }
                        self.write_u8(
                            es,
                            di.wrapping_add(u16::try_from(canon.len()).unwrap_or(0)),
                            0,
                        );
                        self.set_cf(false);
                    }
                    0x65 => {
                        let al = self.get_reg8(0);
                        let es = self.sregs[Self::ES];
                        let di = self.regs[7];
                        self.write_u8(es, di, al);
                        self.write_u16(es, di.wrapping_add(1), 0x0040);
                        self.write_u16(es, di.wrapping_add(3), Self::ROM_SEG);
                        self.set_cf(false);
                    }
                    0x66 => {
                        if self.get_reg8(0) == 0x01 {
                            self.regs[3] = self.active_cp;
                            self.regs[2] = self.active_cp;
                        } else {
                            self.active_cp = self.regs[3];
                            self.refresh_rom_dbcs_table();
                        }
                        self.set_cf(false);
                    }
                    0x69 => {
                        if matches!(self.era, DosEra::Dos400 | DosEra::Dos710) {
                            let ds = self.sregs[Self::DS];
                            let dx = self.regs[2];
                            let sb = self.vol_serial.to_le_bytes();
                            self.write_u16(ds, dx, 0);
                            self.write_u16(
                                ds,
                                dx.wrapping_add(2),
                                u16::from_le_bytes([sb[0], sb[1]]),
                            );
                            self.write_u16(
                                ds,
                                dx.wrapping_add(4),
                                u16::from_le_bytes([sb[2], sb[3]]),
                            );
                            for (i, &b) in b"WINCMD_VOL FAT16   ".iter().enumerate() {
                                self.write_u8(
                                    ds,
                                    dx.wrapping_add(6 + u16::try_from(i).unwrap_or(0)),
                                    b,
                                );
                            }
                            self.set_cf(false);
                        } else {
                            self.regs[0] = ErrCode::InvalidFunction.code();
                            self.set_cf(true);
                        }
                    }
                    0x6C => {
                        if matches!(self.era, DosEra::Dos400 | DosEra::Dos710) {
                            let raw = self.read_asciiz(self.sregs[Self::DS], self.regs[6]);
                            if Self::is_nul_device(&raw) {
                                match self.alloc_file_handle(OpenFile {
                                    path: String::from(NUL_SENTINEL),
                                    pos: 0,
                                }) {
                                    Ok(h) => {
                                        self.regs[0] = h;
                                        self.regs[1] = 1;
                                        self.set_cf(false);
                                    }
                                    Err(e) => {
                                        self.last_dos_err = e.code();
                                        self.regs[0] = self.last_dos_err;
                                        self.set_cf(true);
                                    }
                                }
                                return Ok(());
                            }
                            let key = self.norm_vfs_key(&raw);
                            let action = self.regs[2];
                            let exists = self.vfs.contains_key(&key);
                            if !exists && (action & 0x10) == 0 {
                                self.last_dos_err = ErrCode::FileNotFound.code();
                                self.regs[0] = self.last_dos_err;
                                self.set_cf(true);
                            } else if exists
                                && self.vfs_meta.get(&key).is_some_and(|m| (m.attr & 0x01) != 0)
                                && (action & 0x02) != 0
                            {
                                self.last_dos_err = ErrCode::AccessDenied.code();
                                self.regs[0] = self.last_dos_err;
                                self.set_cf(true);
                            } else {
                                if !exists || (action & 0x02) != 0 {
                                    self.vfs.insert(key.clone(), Vec::new());
                                }
                                match self.alloc_file_handle(OpenFile { path: key, pos: 0 }) {
                                    Ok(h) => {
                                        self.regs[0] = h;
                                        self.regs[1] = if exists { 1 } else { 2 };
                                        self.set_cf(false);
                                    }
                                    Err(e) => {
                                        self.last_dos_err = e.code();
                                        self.regs[0] = self.last_dos_err;
                                        self.set_cf(true);
                                    }
                                }
                            }
                        } else {
                            self.regs[0] = ErrCode::InvalidFunction.code();
                            self.set_cf(true);
                        }
                    }
                    0x71 => self.handle_dos700_lfn(),
                    0x73 => {
                        if self.era == DosEra::Dos710 && self.get_reg8(0) == 0x03 {
                            let es = self.sregs[Self::ES];
                            let di = self.regs[7];
                            for i in 0..44_u16 {
                                self.write_u8(es, di.wrapping_add(i), 0);
                            }
                            self.write_u16(es, di, 44);
                            self.write_u16(es, di.wrapping_add(2), 0);
                            self.write_u16(es, di.wrapping_add(4), 8);
                            self.write_u16(es, di.wrapping_add(8), 512);
                            let fc = self.fat32_free_clusters.to_le_bytes();
                            self.write_u16(
                                es,
                                di.wrapping_add(12),
                                u16::from_le_bytes([fc[0], fc[1]]),
                            );
                            self.write_u16(
                                es,
                                di.wrapping_add(14),
                                u16::from_le_bytes([fc[2], fc[3]]),
                            );
                            self.set_cf(false);
                        } else {
                            self.regs[0] = 0x7300;
                            self.set_cf(true);
                        }
                    }
                    _ => {
                        self.set_cf(true);
                        self.regs[0] = ErrCode::InvalidFunction.code();
                    }
                }
            }
            0x25 => self.handle_int25_fat12(),
            0x2F => match self.regs[0] {
                0x1400 => {
                    self.set_reg8(
                        0,
                        if matches!(self.era, DosEra::Dos400 | DosEra::Dos710) {
                            0xFF
                        } else {
                            0x00
                        },
                    );
                }
                0x1600 => {
                    self.set_reg8(0, if self.win_enh_mode { 0x04 } else { 0x00 });
                }
                0x1680 => {
                    self.idle_yields = self.idle_yields.saturating_add(1);
                    self.set_reg8(0, 0);
                }
                0x168E => {
                    self.winoldap_title = self.read_asciiz(self.sregs[Self::ES], self.regs[7]);
                    self.regs[0] = 1;
                }
                0x1700 => {
                    if self.win_enh_mode {
                        self.regs[0] = 0x030A;
                    }
                }
                0x1701 | 0x1708 => {
                    self.regs[0] = u16::from(self.win_enh_mode);
                }
                0x1702 => {
                    if self.win_enh_mode {
                        self.winoldap_clipboard.clear();
                        self.regs[0] = 1;
                    } else {
                        self.regs[0] = 0;
                    }
                }
                0x1703 => {
                    if self.win_enh_mode {
                        let es = self.sregs[Self::ES];
                        let bx = self.regs[3];
                        let len = usize::from(self.regs[1]);
                        let mut data = Vec::with_capacity(len);
                        for i in 0..len {
                            let b = self.read_u8(es, bx.wrapping_add(u16::try_from(i).unwrap_or(0)));
                            if b == 0 {
                                break;
                            }
                            data.push(b);
                        }
                        self.winoldap_clipboard = data;
                        self.regs[0] = 1;
                    } else {
                        self.regs[0] = 0;
                    }
                }
                0x1704 => {
                    if self.win_enh_mode && !self.winoldap_clipboard.is_empty() {
                        self.regs[0] =
                            u16::try_from(self.winoldap_clipboard.len() + 1).unwrap_or(0);
                    } else {
                        self.regs[0] = 0;
                    }
                    self.regs[2] = 0;
                }
                0x1705 => {
                    if self.win_enh_mode && !self.winoldap_clipboard.is_empty() {
                        let es = self.sregs[Self::ES];
                        let bx = self.regs[3];
                        let clip = self.winoldap_clipboard.clone();
                        for (i, &b) in clip.iter().enumerate() {
                            self.write_u8(es, bx.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
                        }
                        self.write_u8(
                            es,
                            bx.wrapping_add(u16::try_from(clip.len()).unwrap_or(0)),
                            0,
                        );
                        self.regs[0] = 1;
                    } else {
                        self.regs[0] = 0;
                    }
                }
                0x4300 => {
                    self.set_reg8(0, if self.xms_enabled { 0x80 } else { 0x00 });
                }
                0x4310 => {
                    self.sregs[Self::ES] = Self::ROM_SEG;
                    self.regs[3] = 0x0080;
                }
                0x4680 => {
                    self.regs[0] = 0x0080;
                }
                _ => {}
            },
            0x67 => self.handle_ems_int67(),
            other => {
                return Err(ToolError::Emulator {
                    cs: self.sregs[Self::CS],
                    ip: self.ip,
                    code: ErrCode::InvalidFunction,
                    message: format!("unhandled software interrupt INT 0x{other:02X}"),
                });
            }
        }
        Ok(())
    }

    fn handle_dos_mem(&mut self, ah: u8) {
        match ah {
            0x4A => {
                // Resize block ES to BX paragraphs
                let block_seg = self.sregs[Self::ES];
                let new_paras = self.regs[3];
                let mcb_lin = Self::lin(block_seg.wrapping_sub(1), 0);
                let cur_paras = self.read_u16_lin(mcb_lin + 3);
                if new_paras <= cur_paras {
                    self.write_u16_lin(mcb_lin + 3, new_paras);
                    if new_paras + 1 < cur_paras {
                        self.mem[mcb_lin] = b'M';
                        let free_mcb_seg = block_seg.wrapping_add(new_paras);
                        self.write_mcb(
                            free_mcb_seg,
                            b'Z',
                            0,
                            cur_paras - new_paras - 1,
                        );
                    }
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::OutOfMemory.code();
                    self.regs[3] = cur_paras;
                    self.set_cf(true);
                }
            }
            0x48 => {
                // Allocate BX paragraphs using authentic MS-DOS First-Fit Low MCB strategy!
                let want = self.regs[3];
                let mut mcb_seg = Self::DEFAULT_ENV_SEG - 1;
                let mut first_seg: Option<(u16, u16, u8)> = None;
                for _ in 0..128 {
                    let lin = Self::lin(mcb_seg, 0);
                    let kind = self.mem[lin];
                    let owner = self.read_u16_lin(lin + 1);
                    let sz = self.read_u16_lin(lin + 3);
                    if owner == 0 && sz >= want {
                        first_seg = Some((mcb_seg, sz, kind));
                        break;
                    }
                    if kind == b'Z' {
                        break;
                    }
                    mcb_seg = mcb_seg.wrapping_add(sz).wrapping_add(1);
                }
                if let Some((fseg, fsz, orig_kind)) = first_seg {
                    let psp = Self::DEFAULT_PSP_SEG;
                    if fsz > want + 1 {
                        let rem = fsz - want - 1;
                        self.write_mcb(fseg, b'M', psp, want);
                        let rem_mcb = fseg.wrapping_add(want).wrapping_add(1);
                        self.write_mcb(rem_mcb, orig_kind, 0, rem);
                    } else {
                        let lin = Self::lin(fseg, 0);
                        self.write_u16_lin(lin + 1, psp);
                    }
                    self.regs[0] = fseg.wrapping_add(1);
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::OutOfMemory.code();
                    self.regs[3] = 0;
                    self.set_cf(true);
                }
            }
            _ => {
                // 0x49 Free block ES
                let mcb_lin = Self::lin(self.sregs[Self::ES].wrapping_sub(1), 0);
                self.write_u16_lin(mcb_lin + 1, 0);
                self.set_cf(false);
            }
        }
    }

    fn read_asciiz(&self, seg: u16, mut off: u16) -> String {
        let mut bytes = Vec::new();
        for _ in 0..260 {
            let b = self.read_u8(seg, off);
            if b == 0 {
                break;
            }
            bytes.push(b);
            off = off.wrapping_add(1);
        }
        String::from_utf8_lossy(&bytes).into_owned()
    }
}

#[derive(Clone, Copy, Debug)]
enum RmAddr {
    Reg(u8),
    Mem(u16, u16),
}

fn run_pe_cli<W: Write>(
    path: &Path,
    cmd_tail: &str,
    lcov_out: Option<&Path>,
    out: &mut W,
) -> Result<VmRunResult, ToolError> {
    let pe = fs::read(path)?;
    if pe.len() < 0x80 || &pe[0..2] != b"MZ" {
        return Err(win_mock::emu_err(0, ErrCode::BadExeFormat, "invalid PE file"));
    }
    let lfa = usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]]))
        .unwrap_or(0);
    let machine = u16::from_le_bytes([
        *pe.get(lfa + 4).unwrap_or(&0),
        *pe.get(lfa + 5).unwrap_or(&0),
    ]);
    let shared = std::rc::Rc::new(std::cell::RefCell::new(win_mock::SharedWinEnv::new(
        machine == 0x01C0,
    )));
    let (exit_code, stdout, stderr, steps, cov) = match machine {
        0x014C | 0x8664 => {
            let mut vm = x86_pe::X86PeVm::from_pe_bytes(&pe, cmd_tail, &[], shared)?;
            let r = vm.run(25_000_000)?;
            let c = CoverageData::from_pe_trace(r.executed_pcs, r.branch_edges);
            (r.exit_code, r.stdout, r.stderr, r.steps, c)
        }
        0x01C0 | 0x01C2 | 0x01C4 => {
            let mut vm = arm32_pe::Arm32Vm::from_pe_bytes(&pe, cmd_tail, &[], shared)?;
            let r = vm.run(25_000_000)?;
            (r.exit_code, r.stdout, r.stderr, r.steps, r.coverage)
        }
        0xAA64 => {
            let mut vm = arm64_pe::Arm64Vm::from_pe_bytes(&pe, cmd_tail, &[], shared)?;
            let r = vm.run(25_000_000)?;
            (r.exit_code, r.stdout, r.stderr, r.steps, r.coverage)
        }
        other => {
            return Err(win_mock::emu_err(
                0,
                ErrCode::BadExeFormat,
                format!("unsupported PE machine 0x{other:04X}"),
            ));
        }
    };
    if let Some(lcov_file) = lcov_out {
        fs::write(lcov_file, cov.to_lcov(&path.to_string_lossy()))?;
    }
    writeln!(
        out,
        "emu-cov: `{}` exited with {} in {} steps ({} PCs, {}/{} branch edges)",
        path.display(),
        exit_code,
        steps,
        cov.executed_pcs.len(),
        cov.covered_branch_edges(),
        cov.total_branch_edges()
    )?;
    Ok(VmRunResult {
        exit_code: u8::try_from(exit_code & 0xFF).unwrap_or(0),
        stdout: stdout.into_bytes(),
        stderr: stderr.into_bytes(),
        steps,
    })
}

/// Runs the `emu-cov` CLI:
/// `emu-cov (--com <path.com> | --pe <path.exe>) [--era 1.25|2.00|4.00|7.10] [--args "..."] [--lcov <out.info>]`
///
/// # Errors
///
/// Returns [`ToolError`] on CLI argument errors, I/O errors, or guest runtime faults.
pub fn run_cli<I, S, W>(args: I, out: &mut W) -> Result<VmRunResult, ToolError>
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
    W: Write,
{
    let cli_items: Vec<String> = args.into_iter().map(|s| s.as_ref().to_owned()).collect();
    let mut com_path: Option<PathBuf> = None;
    let mut pe_path: Option<PathBuf> = None;
    let mut era = DosEra::Dos400;
    let mut cmd_tail = String::new();
    let mut lcov_out: Option<PathBuf> = None;

    let mut idx = 1_usize;
    while idx < cli_items.len() {
        let flag = cli_items[idx].as_str();
        let val = cli_items.get(idx + 1).ok_or_else(|| ToolError::CliUsage {
            message: format!("missing argument after `{flag}`"),
        })?;
        match flag {
            "--com" => com_path = Some(PathBuf::from(val)),
            "--pe" => pe_path = Some(PathBuf::from(val)),
            "--era" => {
                era = DosEra::parse(val).ok_or_else(|| ToolError::CliUsage {
                    message: format!("unknown DOS era `{val}`"),
                })?;
            }
            "--args" => cmd_tail.clone_from(val),
            "--lcov" => lcov_out = Some(PathBuf::from(val)),
            _ => {
                return Err(ToolError::CliUsage {
                    message: format!("unknown flag `{flag}`"),
                });
            }
        }
        idx += 2;
    }

    if let Some(path) = pe_path {
        return run_pe_cli(&path, &cmd_tail, lcov_out.as_deref(), out);
    }

    let Some(path) = com_path else {
        return Err(ToolError::CliUsage {
            message: "--com <binary.com> or --pe <binary.exe> is required".into(),
        });
    };

    let bytes = fs::read(&path)?;
    let mut vm = DosVm::new(era);
    vm.load_com(&bytes, &cmd_tail, &[("COMSPEC", r"A:\WINCMD.COM"), ("PATH", "A:")])?;
    let res = vm.run(2_000_000)?;

    if let Some(lcov_file) = lcov_out {
        let lcov_str = vm.coverage.to_lcov(&path.to_string_lossy());
        fs::write(lcov_file, lcov_str)?;
    }

    writeln!(
        out,
        "emu-cov: `{}` exited with {} in {} steps ({} PCs, {}/{} branch edges)",
        path.display(),
        res.exit_code,
        res.steps,
        vm.coverage.executed_pcs.len(),
        vm.coverage.covered_branch_edges(),
        vm.coverage.total_branch_edges()
    )?;
    Ok(res)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn executes_8086_alu_branches_strings_dos_syscalls_and_mcb_alloc() -> Result<(), ToolError> {
        // Hand-assembled 8086 test program exercising instructions + DOS syscalls:
        // MOV AH, 30h; INT 21h; MOV BX, 0100h; MOV AH, 4Ah; INT 21h;
        // MOV BX, 0010h; MOV AH, 48h; INT 21h; MOV ES, AX; MOV AH, 49h; INT 21h;
        // MOV DX, 0120h; MOV AH, 09h; INT 21h; MOV AX, 4C00h; INT 21h
        let mut prog = vec![
            0xB4, 0x30, 0xCD, 0x21, // Get DOS version
            0xBB, 0x00, 0x01, 0xB4, 0x4A, 0xCD, 0x21, // Resize PSP block to 0x100 paras
            0xBB, 0x10, 0x00, 0xB4, 0x48, 0xCD, 0x21, // Alloc 0x10 paras
            0x8E, 0xC0, 0xB4, 0x49, 0xCD, 0x21, // Free ES
            0xBA, 0x30, 0x01, 0xB4, 0x09, 0xCD, 0x21, // Print $-string at 0x0130
            0xB8, 0x07, 0x4C, 0xCD, 0x21, // Exit(7)
        ];
        while prog.len() < 0x30 {
            prog.push(0x90);
        }
        prog.extend_from_slice(b"HI$");

        for (era_str, expected_era) in [
            ("1.25", DosEra::Dos125),
            ("2.00", DosEra::Dos200),
            ("4.00", DosEra::Dos400),
            ("7.10", DosEra::Dos710),
        ] {
            assert_eq!(DosEra::parse(era_str), Some(expected_era));
            let mut vm = DosVm::new(expected_era);
            vm.load_com(&prog, " /C VER", &[("PATH", "A:")])?;
            let res = vm.run(1000)?;
            assert_eq!(res.exit_code, 7);
            assert_eq!(res.stdout, b"HI");
        }
        assert_eq!(DosEra::parse("invalid"), None);
        Ok(())
    }

    #[test]
    fn exercises_full_8086_instruction_matrix_and_traps() -> Result<(), ToolError> {
        let code: Vec<u8> = vec![
            0x06, 0x07, 0x0E, 0x1F, 0x16, 0x17, 0x1E, 0x1F, // seg push/pop
            0xB8, 0x04, 0x00, // mov ax, 4
            0xB9, 0x02, 0x00, // mov cx, 2
            0x40, 0x48, // inc ax; dec ax
            0x50, 0x58, // push ax; pop ax
            0x91, 0x91, // xchg ax, cx twice
            0x98, 0x99, 0x9C, 0x9D, // cbw, cwd, pushf, popf
            0x04, 0x01, 0x2C, 0x01, // add al, 1; sub al, 1
            0x05, 0x01, 0x00, 0x2D, 0x01, 0x00, // add ax, 1; sub ax, 1
            0x0C, 0x02, 0x24, 0xFF, 0x34, 0x02, // or/and/xor al
            0x0D, 0x02, 0x00, 0x25, 0xFF, 0xFF, 0x35, 0x02, 0x00, // or/and/xor ax
            0x3C, 0x04, 0x3D, 0x04, 0x00, // cmp al, 4; cmp ax, 4
            0x74, 0x02, 0xB0, 0xFF, // jz +2
            0x75, 0x00, // jnz +0 (not taken)
            0xD1, 0xE0, 0xD1, 0xE8, 0xD1, 0xF8, // shl ax, 1; shr ax, 1; sar ax, 1
            0xD1, 0xC0, 0xD1, 0xC8, 0xD1, 0xD0, 0xD1, 0xD8, // rol/ror/rcl/rcr ax, 1
            0xD0, 0xE0, 0xD0, 0xE8, 0xD0, 0xF8, 0xD0, 0xC0, 0xD0, 0xC8, 0xD0, 0xD0, 0xD0, 0xD8,
            0xF7, 0xE1, // mul cx
            0xF7, 0xF1, // div cx
            0xF5, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, // flag ops
            0xBE, 0x00, 0x02, 0xBF, 0x10, 0x02, 0xB9, 0x02, 0x00, 0xF3, 0xA4, // rep movsb
            0xCD, 0x20, // int 20h
        ];
        let mut vm = DosVm::new(DosEra::Dos400);
        vm.load_com(&code, "", &[])?;
        let res = vm.run(5000)?;
        assert_eq!(res.exit_code, 0);

        let lcov = vm.coverage.to_lcov("matrix.asm");
        assert!(lcov.contains("BRDA:"));
        vm.coverage.verify_pc_coverage("empty", &[], 10_000)?;
        let first_pc = u32::try_from(DosVm::lin(DosVm::DEFAULT_PSP_SEG, 0x0100)).unwrap_or(0);
        vm.coverage.verify_pc_coverage("matrix", &[first_pc], 10_000)?;
        assert!(
            vm.coverage
                .verify_pc_coverage("matrix", &[first_pc, 0x99999], 10_000)
                .is_err()
        );

        // Division by zero trap (`INT 00h`)
        let div0 = [0x31, 0xC9, 0xF7, 0xF1]; // xor cx, cx; div cx
        let mut vm_div = DosVm::new(DosEra::Dos400);
        vm_div.load_com(&div0, "", &[])?;
        assert!(vm_div.run(100).is_err());

        // Step timeout
        let infloop = [0xEB, 0xFE];
        let mut vm_inf = DosVm::new(DosEra::Dos400);
        vm_inf.load_com(&infloop, "", &[])?;
        assert!(vm_inf.run(10).is_err());

        // Oversized COM budget check
        assert!(vm_inf.load_com(&vec![0_u8; 65_300], "", &[]).is_err());
        Ok(())
    }

    #[test]
    fn vfs_file_handles_and_cli_execution() -> Result<(), ToolError> {
        // Test create file (3Ch), write (40h), close (3Eh), open (3Dh), read (3Fh), stdout (40h BX=1), exit (4Ch)
        let mut code = vec![
            0xBA, 0x40, 0x01, 0x31, 0xC9, 0xB4, 0x3C, 0xCD, 0x21, // create "T.TXT"
            0x89, 0xC3, // mov bx, ax
            0xBA, 0x48, 0x01, 0xB9, 0x02, 0x00, 0xB4, 0x40, 0xCD, 0x21, // write "OK"
            0xB4, 0x3E, 0xCD, 0x21, // close
            0xBA, 0x40, 0x01, 0xB8, 0x00, 0x3D, 0xCD, 0x21, // open "T.TXT"
            0x89, 0xC3,
            0xBA, 0x50, 0x01, 0xB9, 0x02, 0x00, 0xB4, 0x3F, 0xCD, 0x21, // read 2 bytes
            0xBB, 0x01, 0x00, 0xB4, 0x40, 0xCD, 0x21, // write to stdout
            0xB8, 0x00, 0x4C, 0xCD, 0x21,
        ];
        while code.len() < 0x40 {
            code.push(0x90);
        }
        code.extend_from_slice(&[b'T', b'.', b'T', b'X', b'T', 0, 0, 0, b'O', b'K']);

        let tmp = std::env::temp_dir().join("wincmd_emu_cov_cli_test");
        let _ = fs::remove_dir_all(&tmp);
        fs::create_dir_all(&tmp)?;
        let com_file = tmp.join("test.com");
        let lcov_file = tmp.join("lcov.info");
        fs::write(&com_file, &code)?;

        let mut out = Vec::new();
        let res = run_cli(
            [
                "emu-cov",
                "--com",
                &com_file.to_string_lossy(),
                "--era",
                "4.00",
                "--args",
                " /C DIR",
                "--lcov",
                &lcov_file.to_string_lossy(),
            ],
            &mut out,
        )?;
        assert_eq!(res.stdout, b"OK");
        assert!(lcov_file.is_file());

        // CLI error cases
        assert!(run_cli(["emu-cov"], &mut out).is_err());
        assert!(run_cli(["emu-cov", "--com"], &mut out).is_err());
        assert!(run_cli(["emu-cov", "--era", "99.9"], &mut out).is_err());
        assert!(run_cli(["emu-cov", "--bogus", "1"], &mut out).is_err());

        let _ = fs::remove_dir_all(&tmp);
        Ok(())
    }
}
