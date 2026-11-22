//! Exhaustive Milestone M2 integration tests exercising `WINCMD.COM` (`NASM`, `FASM`, `JWasm`)
//! across MS-DOS `1.25`, `2.00`, `4.00`, and `7.10` (`Win9x` `LFN`/`FAT32`), including:
//! - MS-DOS 1.25 FCB/XFCB file ops, `INT 25h` FAT12 free space, manual `.COM` & relocated `MZ`
//!   `.EXE` loader (`ldr_125`), and resident `INT 21h` character-I/O redirector (`red_125`).
//! - MS-DOS 2.00 hierarchical directories, `SS:SP`-destruction-proof `EXEC` (`exec_200`),
//!   and `FILES=8` (`0` open `.BAT` handles) nested batch + `DUP`/`DUP2` redirection.
//! - MS-DOS 4.00 `>32 MiB` 64-bit FAT16 free space, `AX=6900h` serial, `AX=6C00h`, `CHCP` +
//!   `AX=6507h` DBCS bitmap, `WINCMD.MSG` `/MSG`, `INT 23h`/`24h`, and LIM EMS 4.0 & XMS reload!
//! - MS-DOS 7.10 `AX=71xxh` Long Filenames (`AX=71A1h` zero-leak `FindClose`), `>4 GiB` `FAT32`
//!   (`AX=7303h`), `WINOLDAP` `TITLE` + `AX=1680h` idle yield, and BIOS + TTY `line_ed` (`/F:ON`).

use emu_cov::{DosEra, DosVm};
use std::path::{Path, PathBuf};
use wincmd_diag::ToolError;

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .map_or_else(|| PathBuf::from("."), Path::to_path_buf)
}

fn load_built(tc: &str, name: &str) -> Result<Vec<u8>, ToolError> {
    let p = workspace_root().join("build").join(tc).join("dos16").join(name);
    Ok(std::fs::read(p)?)
}

fn bs() -> char {
    char::from(0x5C)
}

fn join_crlf(lines: &[&str]) -> Vec<u8> {
    let mut out = Vec::new();
    for line in lines {
        out.extend_from_slice(line.as_bytes());
        out.extend_from_slice(&[0x0D, 0x0A]);
    }
    out
}

/// Builds a minimal multi-segment DOS `'MZ'` `.EXE` with a segment relocation fixup:
/// loads `DS` via relocated segment immediate (`mov ax, seg_data; mov ds, ax`), prints `"MZOK$"`
/// via `AH=09h`, and exits with `exit_code` (`AH=4Ch` on DOS 2+, or `INT 20h` when `use_int20`).
fn build_relocated_mz_exe(exit_code: u8, use_int20: bool) -> Vec<u8> {
    let mut exe = vec![0_u8; 32];
    exe[0] = b'M';
    exe[1] = b'Z';
    exe[4..6].copy_from_slice(&1_u16.to_le_bytes());
    exe[6..8].copy_from_slice(&1_u16.to_le_bytes());
    exe[8..10].copy_from_slice(&2_u16.to_le_bytes());
    exe[14..16].copy_from_slice(&0x0003_u16.to_le_bytes());
    exe[16..18].copy_from_slice(&0x0080_u16.to_le_bytes());
    exe[20..22].copy_from_slice(&0x0000_u16.to_le_bytes());
    exe[22..24].copy_from_slice(&0x0000_u16.to_le_bytes());
    exe[24..26].copy_from_slice(&28_u16.to_le_bytes());
    exe[28..30].copy_from_slice(&1_u16.to_le_bytes());
    exe[30..32].copy_from_slice(&0_u16.to_le_bytes());

    let mut body = vec![0_u8; 48];
    body[0] = 0xB8;
    body[1] = 0x02;
    body[2] = 0x00;
    body[3] = 0x8E;
    body[4] = 0xD8;
    body[5] = 0xBA;
    body[6] = 0x00;
    body[7] = 0x00;
    body[8] = 0xB4;
    body[9] = 0x09;
    body[10] = 0xCD;
    body[11] = 0x21;
    if use_int20 {
        body[12] = 0xCD;
        body[13] = 0x20;
    } else {
        body[12] = 0xB8;
        body[13] = exit_code;
        body[14] = 0x4C;
        body[15] = 0xCD;
        body[16] = 0x21;
    }
    body[32..37].copy_from_slice(b"MZOK$");
    exe.extend_from_slice(&body);
    let total_len = u16::try_from(exe.len()).unwrap_or(0);
    exe[2..4].copy_from_slice(&total_len.to_le_bytes());
    exe
}

#[test]
fn m2_dos125_fcb_vfs_fat12_manual_loader_and_int21_redirector() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;
        let mut vm = DosVm::new(DosEra::Dos125);
        vm.corrupt_high_ram_on_exec = true;
        vm.free_clusters_125 = 200;

        let comspec = format!("A:{}WINCMD.COM", bs());
        vm.vfs.insert(comspec, com.clone());
        vm.insert_vfs_with_meta("WINCMD12", Vec::new(), 0x08, "");
        vm.vfs
            .insert(String::from("SRC.TXT"), b"Exact19BytePayload!".to_vec());
        vm.insert_vfs_with_meta("LOCKED.TXT", b"RO".to_vec(), 0x01, "");

        let filter_com: Vec<u8> = vec![
            0xB2, b'[', 0xB4, 0x02, 0xCD, 0x21, // 0..6: mov dl, '['; mov ah, 2; int 21h
            0xB4, 0x08, 0xCD, 0x21, // 6..10: .lp: mov ah, 8; int 21h
            0x3C, 0x1A, 0x74, 0x10, // 10..14: cmp al, 0x1A; je .done (+16 -> 30)
            0x3C, 0x0D, 0x74, 0xF4, // 14..18: cmp al, 0x0D; je .lp (-12 -> 6)
            0x3C, 0x0A, 0x74, 0xF0, // 18..22: cmp al, 0x0A; je .lp (-16 -> 6)
            0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21, // 22..28: mov dl, al; mov ah, 2; int 21h
            0xEB, 0xE8, // 28..30: jmp .lp (-24 -> 6)
            0xB2, b']', 0xB4, 0x02, 0xCD, 0x21, // 30..36: .done: mov dl, ']'; mov ah, 2; int 21h
            0xCD, 0x20, // 36..38: int 20h
        ];
        vm.vfs.insert(String::from("FILT.COM"), filter_com);
        vm.vfs
            .insert(String::from("MZ125.EXE"), build_relocated_mz_exe(0, true));

        let batch = join_crlf(&[
            "VOL",
            "DIR",
            "COPY SRC.TXT DST.TXT",
            "COPY SRC.TXT LOCKED.TXT",
            "REN DST.TXT RENAMED.BAK",
            "TYPE RENAMED.BAK",
            "ECHO.",
            "DEL SRC.TXT",
            "ECHO REDIR125> IN125.TXT",
            "FILT < IN125.TXT > OUT125.TXT",
            "TYPE OUT125.TXT",
            "ECHO.",
            "MZ125",
            "ECHO.",
        ]);
        vm.vfs.insert(String::from("RUN125.BAT"), batch);

        vm.load_com(&com, " /C RUN125.BAT", &[])?;
        let res = vm.run(15_000_000)?;
        assert_eq!(res.exit_code, 0);
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains("Volume in drive A is WINCMD12"), "stdout: {out}");
        assert!(out.contains("102,400 bytes free"), "stdout: {out}");
        assert!(out.contains("Access denied"), "stdout: {out}");
        assert!(out.contains("Exact19BytePayload!"), "stdout: {out}");
        assert!(out.contains("[REDIR125]"), "stdout: {out}");
        assert!(out.contains("MZOK"), "stdout: {out}");
        assert!(!vm.vfs.contains_key("SRC.TXT"));
        assert!(vm.vfs.contains_key("RENAMED.BAK"));
    }
    Ok(())
}

#[test]
fn m2_dos200_subdirs_ss_sp_exec_guard_and_files8_discipline() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;
        let mut vm = DosVm::new(DosEra::Dos200);
        vm.corrupt_high_ram_on_exec = true;

        let comspec = format!("A:{}WINCMD.COM", bs());
        vm.vfs.insert(comspec.clone(), com.clone());
        vm.vfs
            .insert(String::from("MZTST.EXE"), build_relocated_mz_exe(37, false));

        vm.vfs.insert(
            String::from("B1.BAT"),
            join_crlf(&[
                "MD SUB",
                "CD SUB",
                "CD",
                "CD ..",
                "ECHO L1 > LOG.TXT",
                "CALL B2.BAT",
                "RD SUB",
                "TYPE LOG.TXT",
                "EXIT 0",
            ]),
        );
        vm.vfs.insert(
            String::from("B2.BAT"),
            join_crlf(&["ECHO L2 >> LOG.TXT", "CALL B3.BAT"]),
        );
        vm.vfs.insert(
            String::from("B3.BAT"),
            join_crlf(&[
                "MZTST",
                "IF ERRORLEVEL 37 ECHO EXIT37 >> LOG.TXT",
                "B4.BAT",
            ]),
        );
        vm.vfs.insert(
            String::from("B4.BAT"),
            join_crlf(&["ECHO L4 >> LOG.TXT"]),
        );

        vm.load_com(&com, " /C B1.BAT", &[("COMSPEC", &comspec), ("PATH", "A:")])?;
        let res = vm.run(15_000_000)?;
        assert_eq!(res.exit_code, 0);
        assert!(
            vm.max_concurrent_handles <= 3,
            "FILES=8 violated: peak user handles = {}",
            vm.max_concurrent_handles
        );
        let out = String::from_utf8_lossy(&res.stdout);
        let expected_sub = format!("A:{}SUB", bs());
        assert!(out.contains(&expected_sub), "stdout: {out}");
        assert!(out.contains("MZOK"), "stdout: {out}");
        assert!(out.contains("L1"), "stdout: {out}");
        assert!(out.contains("L2"), "stdout: {out}");
        assert!(out.contains("EXIT37"), "stdout: {out}");
        assert!(!vm.dirs.contains("SUB"));
    }
    Ok(())
}

#[test]
fn m2_dos400_fat16_64bit_chcp_dbcs_msg_ems40_and_xms_swap() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;
        let msg = load_built(tc, "WINCMD.MSG")?;

        // 1. LIM EMS 4.0 swap test (WINCMD.COM omitted from disk so ONLY EMS 4.0 can reload TRANSIENT!)
        let mut vm = DosVm::new(DosEra::Dos400);
        vm.enable_ems();
        vm.corrupt_high_ram_on_exec = true;
        vm.fat16_free_clusters = 20_000;
        vm.vol_serial = 0x1988_0400;
        vm.vfs.insert(String::from("WINCMD.MSG"), msg);
        vm.vfs
            .insert(String::from("PING.COM"), vec![0xB8, 0x00, 0x4C, 0xCD, 0x21]);
        vm.vfs.insert(
            String::from("T400.BAT"),
            join_crlf(&[
                "PING",
                "VOL",
                "DIR",
                "CHCP",
                "CHCP 936",
                "CHCP",
                "TRUENAME T400.BAT",
                "TYPE MISSING.FIL",
            ]),
        );

        vm.load_com(&com, " /F /MSG /C T400.BAT", &[("PATH", "A:")])?;
        let res = vm.run(12_000_000)?;
        assert_eq!(res.exit_code, 1);
        assert_eq!(
            vm.ems_names.get(&1),
            Some(&[b'W', b'I', b'N', b'C', b'M', b'D', 0, 0])
        );
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains("1988-0400"), "stdout: {out}");
        assert!(out.contains("81,920,000 bytes free"), "stdout: {out}");
        assert!(out.contains("Active code page: 437"), "stdout: {out}");
        assert!(out.contains("Active code page: 936"), "stdout: {out}");
        let expected_tn = format!("A:{}T400.BAT", bs());
        assert!(out.contains(&expected_tn), "stdout: {out}");
        assert!(out.contains("File not found"), "stdout: {out}");

        let trn_seg = 0xA000 - 0x0A20;
        let lead_81_byte = vm.read_u8(trn_seg, 0x8010 + (0x81 / 8));
        assert_ne!(lead_81_byte & (1 << (0x81 & 7)), 0);

        // 2. XMS 2.0 swap test (no EMS, no WINCMD.COM on disk — ONLY XMS can reload TRANSIENT!)
        let mut xvm = DosVm::new(DosEra::Dos400);
        xvm.enable_xms();
        xvm.corrupt_high_ram_on_exec = true;
        xvm.vfs
            .insert(String::from("PING.COM"), vec![0xB8, 0x00, 0x4C, 0xCD, 0x21]);
        xvm.vfs.insert(
            String::from("TXMS.BAT"),
            join_crlf(&["PING", "ECHO XMS_SURVIVED"]),
        );
        xvm.load_com(&com, " /C TXMS.BAT", &[])?;
        let xres = xvm.run(8_000_000)?;
        assert_eq!(xres.exit_code, 0);
        assert!(String::from_utf8_lossy(&xres.stdout).contains("XMS_SURVIVED"));
    }
    Ok(())
}

#[test]
fn m2_dos710_lfn_zero_handle_leak_fat32_64bit_winoldap_and_line_ed() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;
        let mut vm = DosVm::new(DosEra::Dos710);
        vm.win_enh_mode = true;
        vm.fat32_free_clusters = 1_250_000;
        vm.insert_vfs_with_meta(
            "Long Report 1995.txt",
            b"Win95 LFN Content!".to_vec(),
            0x20,
            "LONGRE~1.TXT",
        );

        let mut keys = Vec::new();
        keys.extend_from_slice(b"TITLE Win95 Box");
        keys.push(0x0D);
        keys.extend_from_slice(b"DIR");
        keys.push(0x0D);
        keys.extend_from_slice(b"TYPE LONG");
        keys.push(0x09); // Tab completion (/F:ON)!
        keys.push(0x0D);
        for b in keys {
            vm.bios_keys.push((0, b));
        }

        vm.load_com(&com, " /F:ON", &[("PATH", "A:")])?;
        let res = vm.run(12_000_000)?;
        assert_eq!(res.exit_code, 0);
        assert!(
            vm.open_lfn_handles.is_empty(),
            "LFN search handle leaked (AX=71A1h not called!): {:?}",
            vm.open_lfn_handles
        );
        assert_eq!(vm.winoldap_title, "Win95 Box");
        assert!(vm.idle_yields > 0, "expected INT 2Fh AX=1680h idle yields");
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains("LONGRE~1.TXT"), "stdout: {out}");
        assert!(out.contains("Long Report 1995.txt"), "stdout: {out}");
        assert!(out.contains("5,120,000,000 bytes free"), "stdout: {out}");
        assert!(out.contains("Win95 LFN Content!"), "stdout: {out}");
    }
    Ok(())
}

#[test]
fn m2_int23_ctrl_break_and_int24_critical_error_isrs() -> Result<(), ToolError> {
    let com = load_built("nasm", "WINCMD.COM")?;

    for (era, key, expected_al, expected_prompt) in [
        (DosEra::Dos200, b'i', 0_u8, "Ignore?"),
        (DosEra::Dos400, b'f', 3_u8, "Fail?"),
    ] {
        let mut vm = DosVm::new(era);
        vm.load_com(&com, " /C VER", &[])?;
        for _ in 0..400_000 {
            vm.step()?;
            if vm.read_u16(0, 0x24 * 4 + 2) != DosVm::ROM_SEG {
                break;
            }
        }
        vm.bios_keys.push((0, key));
        let caller_cs = vm.sregs[DosVm::CS];
        let caller_ip = vm.ip;
        vm.raise_hardware_int(0x24);
        for _ in 0..5_000 {
            vm.step()?;
            if vm.sregs[DosVm::CS] == caller_cs && vm.ip == caller_ip {
                break;
            }
        }
        assert_eq!(vm.regs[0] & 0xFF, u16::from(expected_al));
        let out = String::from_utf8_lossy(&vm.stdout);
        assert!(out.contains(expected_prompt), "stdout: {out}");
    }

    let mut bvm = DosVm::new(DosEra::Dos400);
    bvm.vfs.insert(
        String::from("LOOP.BAT"),
        join_crlf(&["ECHO STEP1", "ECHO STEP2"]),
    );
    bvm.stdin.extend_from_slice(&join_crlf(&["Y"]));
    bvm.load_com(&com, " /C LOOP.BAT", &[])?;
    let mut injected = false;
    let mut prompted = false;
    for _ in 0..2_000_000 {
        if bvm.step().is_err() {
            break;
        }
        if !injected && String::from_utf8_lossy(&bvm.stdout).contains("STEP1") {
            bvm.raise_hardware_int(0x23);
            injected = true;
        }
        if String::from_utf8_lossy(&bvm.stdout).contains("Terminate batch job") {
            let rem = bvm.run(500_000)?;
            let full_out = String::from_utf8_lossy(&rem.stdout);
            assert!(
                !full_out.contains("STEP2"),
                "STEP2 ran after Y abort: {full_out}"
            );
            prompted = true;
            break;
        }
    }
    assert!(prompted, "INT 23h batch abort prompt did not trigger");
    Ok(())
}

#[test]
fn m2_init_switches_5rule_quotes_autoexec_and_tty_ansi_line_ed() -> Result<(), ToolError> {
    let com = load_built("nasm", "WINCMD.COM")?;

    // 1. --dialect=nt + 5-rule /C outer quote stripping ("ECHO 5RULE_OK" -> strips quotes!)
    let mut vm = DosVm::new(DosEra::Dos400);
    vm.load_com(&com, " --dialect=nt /C \"ECHO 5RULE_OK\"", &[])?;
    let res = vm.run(2_000_000)?;
    assert_eq!(res.exit_code, 0);
    let out = String::from_utf8_lossy(&res.stdout);
    assert!(out.contains("5RULE_OK"), "stdout: {out}");
    assert!(!out.contains("\"ECHO"), "quotes were not stripped: {out}");

    // 2. /P with AUTOEXEC.BAT runs AUTOEXEC.BAT (ignoring EXIT!) until physical 0x1A EOF
    let mut pvm = DosVm::new(DosEra::Dos200);
    pvm.vfs.insert(
        String::from("AUTOEXEC.BAT"),
        join_crlf(&["ECHO AUTO_RAN", "EXIT"]),
    );
    pvm.load_com(&com, " /P", &[])?;
    let pres = pvm.run(4_000_000)?;
    let pout = String::from_utf8_lossy(&pres.stdout);
    assert!(pout.contains("AUTO_RAN"), "stdout: {pout}");

    // 3. /P without AUTOEXEC.BAT prints startup VER banner, then reads TTY ANSI ESC[A recall!
    let mut tvm = DosVm::new(DosEra::Dos200);
    tvm.force_tty_console = true;
    let mut tty_in = Vec::new();
    tty_in.extend_from_slice(b"ECHO TTY1");
    tty_in.push(0x0D);
    tty_in.extend_from_slice(&[0x1B, b'[', b'A', 0x0D, 0x1A]);
    tvm.stdin = tty_in;
    tvm.load_com(&com, " /P", &[])?;
    let tres = tvm.run(6_000_000)?;
    let tout = String::from_utf8_lossy(&tres.stdout);
    assert!(tout.contains("WINCMD Pure-ASM Shell [MS-DOS 2.00]"), "stdout: {tout}");
    assert_eq!(tout.matches("TTY1").count(), 4, "expected 2 prompt echoes + 2 outputs: {tout}");
    Ok(())
}
