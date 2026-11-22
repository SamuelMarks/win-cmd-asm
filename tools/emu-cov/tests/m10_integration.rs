//! Milestone M10 integration tests for `WINCMD_ARMV4.EXE` (`0x01C0` `WinCE` strict `ARMv4`)
//! and `WINCMD_ARM32.EXE` (`0x01C4` `ARMNT` Thumb-2 `fasmarm` & `llvm-mc` builds).

use emu_cov::arm32_pe::{Arm32RunResult, Arm32Vm};
use emu_cov::win_mock::{BS, DQ, SharedWinEnv, emu_err};
use std::cell::RefCell;
use std::fs;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use wincmd_diag::{ErrCode, ToolError};

const CRLF_BYTES: [u8; 2] = [13, 10];

const fn crlf() -> &'static str {
    match core::str::from_utf8(&CRLF_BYTES) {
        Ok(s) => s,
        Err(_) => "",
    }
}

fn join_lines(lines: &[String]) -> String {
    let mut out = lines.join(crlf());
    out.push_str(crlf());
    out
}

fn repo_root() -> Result<PathBuf, ToolError> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| emu_err(0, ErrCode::GeneralFailure, "cannot resolve workspace root"))
}

fn load_arm_bin(rel: &str) -> Result<Vec<u8>, ToolError> {
    let p = repo_root()?.join(rel);
    Ok(fs::read(p)?)
}

fn run_arm32(
    pe: &[u8],
    cmdline: &str,
    shared: Rc<RefCell<SharedWinEnv>>,
) -> Result<Arm32RunResult, ToolError> {
    let mut vm = Arm32Vm::from_pe_bytes(pe, cmdline, &[], shared)?;
    vm.run(8_000_000)
}

#[test]
fn m10_pe32_headers_security_pdata_and_zero_crt_imports() -> Result<(), ToolError> {
    let v4 = load_arm_bin("build/fasmarm/arm32/WINCMD_ARMV4.EXE")?;
    assert!(
        v4.len() <= 131_072,
        "WINCMD_ARMV4.EXE size {} exceeds 128 KiB budget",
        v4.len()
    );
    let lfa4 =
        usize::try_from(u32::from_le_bytes([v4[0x3C], v4[0x3D], v4[0x3E], v4[0x3F]])).unwrap_or(0);
    assert_eq!(&v4[lfa4..lfa4 + 4], &[b'P', b'E', 0, 0]);
    let mach4 = u16::from_le_bytes([v4[lfa4 + 4], v4[lfa4 + 5]]);
    assert_eq!(mach4, 0x01C0, "WINCMD_ARMV4.EXE must target classic ARM (0x01C0)");
    let opt4 = lfa4 + 24;
    let maj_sub4 = u16::from_le_bytes([v4[opt4 + 48], v4[opt4 + 49]]);
    let subsys4 = u16::from_le_bytes([v4[opt4 + 68], v4[opt4 + 69]]);
    assert_eq!(maj_sub4, 4);
    assert_eq!(subsys4, 9, "WINCMD_ARMV4.EXE must use Windows CE subsystem (9)");
    let v4_ascii = String::from_utf8_lossy(&v4).to_ascii_uppercase();
    assert!(v4_ascii.contains("COREDLL.DLL"));
    assert!(!v4_ascii.contains("MSVCRT"));
    assert!(!v4_ascii.contains("UCRTBASE"));

    for rel in [
        "build/fasmarm/arm32/WINCMD_ARM32.EXE",
        "build/llvm/arm32/WINCMD_ARM32.EXE",
    ] {
        let pe = load_arm_bin(rel)?;
        assert!(
            pe.len() <= 131_072,
            "{rel} size {} exceeds 128 KiB budget",
            pe.len()
        );
        let lfa =
            usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]])).unwrap_or(0);
        assert_eq!(&pe[lfa..lfa + 4], &[b'P', b'E', 0, 0]);
        let mach = u16::from_le_bytes([pe[lfa + 4], pe[lfa + 5]]);
        assert_eq!(mach, 0x01C4, "{rel} must target ARMNT Thumb-2 (0x01C4)");
        let opt = lfa + 24;
        let entry_rva =
            u32::from_le_bytes([pe[opt + 16], pe[opt + 17], pe[opt + 18], pe[opt + 19]]);
        assert_eq!(
            entry_rva & 1,
            1,
            "{rel} AddressOfEntryPoint 0x{entry_rva:X} must set Thumb bit 0"
        );
        let maj_sub = u16::from_le_bytes([pe[opt + 48], pe[opt + 49]]);
        let min_sub = u16::from_le_bytes([pe[opt + 50], pe[opt + 51]]);
        assert_eq!(
            (maj_sub, min_sub),
            (6, 2),
            "{rel} must target Subsystem 6.02 (Windows RT 8.0/8.1 & Win10 ARM32)"
        );
        let subsys = u16::from_le_bytes([pe[opt + 68], pe[opt + 69]]);
        assert_eq!(subsys, 3, "{rel} must target WINDOWS_CUI (3)");
        let dll_chars = u16::from_le_bytes([pe[opt + 70], pe[opt + 71]]);
        assert_ne!(dll_chars & 0x0040, 0, "{rel} missing DYNAMIC_BASE");
        assert_ne!(dll_chars & 0x0100, 0, "{rel} missing NX_COMPAT");
        assert_ne!(dll_chars & 0x4000, 0, "{rel} missing GUARD_CF");

        let pdata_rva =
            u32::from_le_bytes([pe[opt + 120], pe[opt + 121], pe[opt + 122], pe[opt + 123]]);
        let pdata_sz =
            u32::from_le_bytes([pe[opt + 124], pe[opt + 125], pe[opt + 126], pe[opt + 127]]);
        assert!(pdata_rva != 0 && pdata_sz >= 64, "{rel} missing .pdata unwind directory");

        let pe_ascii = String::from_utf8_lossy(&pe);
        assert!(
            pe_ascii.contains("longPathAware"),
            "{rel} missing longPathAware manifest"
        );
        let pe_up = pe_ascii.to_ascii_uppercase();
        assert!(pe_up.contains("KERNEL32.DLL"));
        assert!(!pe_up.contains("MSVCRT"));
        assert!(!pe_up.contains("UCRTBASE"));
    }
    Ok(())
}

#[test]
fn m10_arith_expansion_delayed_and_setlocal_all_arm32_builds() -> Result<(), ToolError> {
    let script = join_lines(&[
        "@ECHO OFF".to_owned(),
        format!("SET /A {DQ}A=(10+5)*3 - (8<<1), B=100/0{DQ} 2>NUL"),
        format!("SET /A {DQ}M=-2147483648/-1, S=1<<35{DQ}"),
        "SET STR=HelloArmWorld".to_owned(),
        "SET SUB=%STR:~5,3%".to_owned(),
        "SET REP=%STR:Arm=RISC%".to_owned(),
        "SETLOCAL EnableDelayedExpansion".to_owned(),
        "SET V=init".to_owned(),
        format!("(SET {DQ}V=delayed_ok{DQ} & ECHO !V!_{DQ}%A%{DQ}_%S%_%SUB%_%REP%)"),
        "ENDLOCAL".to_owned(),
        "ECHO AFTER=%V%".to_owned(),
    ]);

    {
        let pe = load_arm_bin("build/fasmarm/arm32/WINCMD_ARMV4.EXE")?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(true)));
        let bat_path = format!("{BS}Temp{BS}arith.cmd");
        env.borrow_mut().write_file(&bat_path, script.as_bytes());
        let res = run_arm32(&pe, &format!("/C {bat_path}"), env)?;
        assert_eq!(res.exit_code, 0, "ARMv4 stderr: {}", res.stderr);
        assert!(
            res.stdout
                .contains(&format!("delayed_ok_{DQ}29{DQ}_8_Arm_HelloRISCWorld")),
            "ARMv4 unexpected stdout: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("AFTER="),
            "SETLOCAL did not unwind V on ARMv4: {}",
            res.stdout
        );
        assert!(res.coverage.executed_pcs.len() > 400);
    }

    for rel in [
        "build/fasmarm/arm32/WINCMD_ARM32.EXE",
        "build/llvm/arm32/WINCMD_ARM32.EXE",
    ] {
        let pe = load_arm_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let bat_path = format!("C:{BS}Temp{BS}arith.cmd");
        env.borrow_mut().write_file(&bat_path, script.as_bytes());
        let res = run_arm32(&pe, &format!("/C {bat_path}"), env)?;
        assert_eq!(res.exit_code, 0, "{rel} stderr: {}", res.stderr);
        assert!(
            res.stdout
                .contains(&format!("delayed_ok_{DQ}29{DQ}_8_Arm_HelloRISCWorld")),
            "{rel} unexpected stdout: {}",
            res.stdout
        );
        assert!(res.stdout.contains("AFTER="));
        assert!(res.coverage.executed_pcs.len() > 400);
    }
    Ok(())
}

#[test]
fn m10_batch_control_flow_for_if_call_shift_all_arm32_builds() -> Result<(), ToolError> {
    let script = join_lines(&[
        "@ECHO OFF".to_owned(),
        "SET SUM=0".to_owned(),
        "FOR /L %%I IN (1,2,7) DO CALL :ADD %%I".to_owned(),
        format!("FOR /F {DQ}tokens=2 delims=:{DQ} %%A IN ({DQ}k1:alpha{DQ}) DO SET TOK=%%A"),
        format!("IF /I {DQ}%TOK%{DQ}=={DQ}ALPHA{DQ} IF %SUM% EQU 16 ECHO FLOW_OK_%SUM%_%TOK%"),
        "CALL :SHIFT_TEST first second third".to_owned(),
        "EXIT /B 33".to_owned(),
        ":ADD".to_owned(),
        "SET /A SUM+=%1".to_owned(),
        "GOTO :EOF".to_owned(),
        ":SHIFT_TEST".to_owned(),
        "SHIFT".to_owned(),
        "ECHO SHIFTED=%1_%2".to_owned(),
        "EXIT /B 0".to_owned(),
    ]);

    {
        let pe = load_arm_bin("build/fasmarm/arm32/WINCMD_ARMV4.EXE")?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(true)));
        let bat = format!("{BS}Storage Card{BS}flow.bat");
        env.borrow_mut().write_file(&bat, script.as_bytes());
        let res = run_arm32(&pe, &format!("/C {DQ}{bat}{DQ}"), env)?;
        assert_eq!(res.exit_code, 33);
        assert!(res.stdout.contains("FLOW_OK_16_alpha"), "stdout: {}", res.stdout);
        assert!(res.stdout.contains("SHIFTED=second_third"), "stdout: {}", res.stdout);
    }

    for rel in [
        "build/fasmarm/arm32/WINCMD_ARM32.EXE",
        "build/llvm/arm32/WINCMD_ARM32.EXE",
    ] {
        let pe = load_arm_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let bat = format!("C:{BS}Temp{BS}flow.cmd");
        env.borrow_mut().write_file(&bat, script.as_bytes());
        let res = run_arm32(&pe, &format!("/C {bat}"), env)?;
        assert_eq!(res.exit_code, 33, "{rel} exit code mismatch");
        assert!(res.stdout.contains("FLOW_OK_16_alpha"), "{rel} stdout: {}", res.stdout);
        assert!(res.stdout.contains("SHIFTED=second_third"), "{rel} stdout: {}", res.stdout);
    }
    Ok(())
}

#[test]
fn m10_wince_rootless_paths_and_spooled_pipeline_on_strict_armv4() -> Result<(), ToolError> {
    let pe = load_arm_bin("build/fasmarm/arm32/WINCMD_ARMV4.EXE")?;
    let env = Rc::new(RefCell::new(SharedWinEnv::new(true)));
    let card_dir = format!("{BS}Storage Card{BS}Proj");
    let script = join_lines(&[
        "@ECHO OFF".to_owned(),
        format!("MD {DQ}{card_dir}{DQ}"),
        format!("CD {DQ}{card_dir}{DQ}"),
        "ECHO pocket_pc_ok>note.txt".to_owned(),
        "ECHO piped_ce_val | SET /P PVAL=".to_owned(),
        "TYPE note.txt".to_owned(),
        "VER".to_owned(),
    ]);
    let bat = format!("{BS}Temp{BS}ce_test.cmd");
    env.borrow_mut().write_file(&bat, script.as_bytes());
    let res = run_arm32(&pe, &format!("/C {bat}"), Rc::clone(&env))?;
    assert_eq!(res.exit_code, 0);
    assert!(res.stdout.contains("pocket_pc_ok"), "stdout: {}", res.stdout);
    assert!(res.stdout.contains("4.20.1088"), "VER stdout: {}", res.stdout);
    let written = env
        .borrow()
        .read_file(&format!("{card_dir}{BS}note.txt"))
        .unwrap_or_default();
    assert!(String::from_utf8_lossy(&written).contains("pocket_pc_ok"));
    Ok(())
}

#[test]
fn m10_armnt_concurrent_pipes_mklink_registry_longpath_and_case_sens() -> Result<(), ToolError> {
    for rel in [
        "build/fasmarm/arm32/WINCMD_ARM32.EXE",
        "build/llvm/arm32/WINCMD_ARM32.EXE",
    ] {
        let pe = load_arm_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        env.borrow_mut().write_file(
            &format!("C:{BS}Windows{BS}System32{BS}WINCMD_ARM32.EXE"),
            &pe,
        );

        let cs_dir = format!("C:{BS}Temp{BS}CsDir");
        env.borrow_mut().set_dir_case_sensitive(&cs_dir);
        env.borrow_mut()
            .write_file(&format!("{cs_dir}{BS}ExactCase.txt"), b"UPPER");
        env.borrow_mut()
            .write_file(&format!("{cs_dir}{BS}exactcase.txt"), b"lower");

        let seg = "LongSubDirectorySegmentName1234567890";
        let deep_dir = format!(
            "C:{BS}Temp{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}"
        );
        assert!(deep_dir.len() > 265);

        let script = join_lines(&[
            "@ECHO OFF".to_owned(),
            "CHCP 65001 >NUL".to_owned(),
            "ASSOC .xyz=XyzFile >NUL".to_owned(),
            "ASSOC .xyz".to_owned(),
            format!("FTYPE XyzFile=C:{BS}bin{BS}xyz.exe {DQ}%%1{DQ} >NUL"),
            "FTYPE XyzFile".to_owned(),
            format!("MD C:{BS}Temp{BS}RealTarget"),
            format!("ECHO keep_safe>C:{BS}Temp{BS}RealTarget{BS}precious.txt"),
            format!("MKLINK /J C:{BS}Temp{BS}JuncLink C:{BS}Temp{BS}RealTarget >NUL"),
            format!("DIR C:{BS}Temp"),
            format!("RD /S /Q C:{BS}Temp{BS}JuncLink"),
            format!("IF EXIST C:{BS}Temp{BS}RealTarget{BS}precious.txt ECHO JUNC_SAFE"),
            format!("MD {DQ}{deep_dir}{DQ}"),
            format!("ECHO deep_payload>{DQ}{deep_dir}{BS}leaf.txt{DQ}"),
            format!("TYPE {DQ}{deep_dir}{BS}leaf.txt{DQ}"),
            format!("DIR /B {cs_dir}{BS}Exact*"),
            "VER".to_owned(),
        ]);

        let bat = format!("C:{BS}Temp{BS}armnt_full.cmd");
        env.borrow_mut().write_file(&bat, script.as_bytes());

        let pipe_cmd = format!(
            "/S /C {DQ}CALL {bat} & (ECHO pipe_stage_data | {DQ}C:{BS}Windows{BS}System32{BS}WINCMD_ARM32.EXE{DQ} /V:ON /C {DQ}SET /P LINE= & ECHO PIPED=!LINE!{DQ}){DQ}"
        );
        let res = run_arm32(&pe, &pipe_cmd, Rc::clone(&env))?;
        assert_eq!(res.exit_code, 0, "{rel} stderr: {}", res.stderr);
        assert!(res.stdout.contains(".xyz=XyzFile"), "{rel} out: {}", res.stdout);
        assert!(
            res.stdout.contains(&format!("XyzFile=C:{BS}bin{BS}xyz.exe")),
            "{rel} out: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("<JUNCTION>"),
            "{rel} DIR missing <JUNCTION>: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("JUNC_SAFE"),
            "{rel} RD /S deleted junction target!"
        );
        assert!(
            res.stdout.contains("deep_payload"),
            "{rel} >260 long path failed: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("ExactCase.txt"),
            "{rel} case-sens match missing: {}",
            res.stdout
        );
        assert!(
            !res.stdout.contains("exactcase.txt"),
            "{rel} case-sensitive directory matched wrong case: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("PIPED=pipe_stage_data"),
            "{rel} concurrent CreatePipe failed: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("10.0.19045"),
            "{rel} VER missing PEB build: {}",
            res.stdout
        );
    }
    Ok(())
}

#[test]
fn m10_vm8086_executes_real_16bit_com_and_mz_exe_on_armv4_and_armv7() -> Result<(), ToolError> {
    // 1. Real 8086 `.COM` binary (`ORG 0100h`): prints `VM86_COM_OK` via `INT 21h AH=09h` and exits with `37` (`0x25`).
    let com_prog: Vec<u8> = vec![
        0xBA, 0x0E, 0x01, // mov dx, 0x010E
        0xB4, 0x09, // mov ah, 0x09
        0xCD, 0x21, // int 0x21
        0xB8, 0x25, 0x4C, // mov ax, 0x4C25
        0xCD, 0x21, // int 0x21
        0x90, 0x90, // pad to 0x0E
        b'V', b'M', b'8', b'6', b'_', b'C', b'O', b'M', b'_', b'O', b'K', 0x0D, 0x0A, b'$',
    ];

    // 2. Relocated 16-bit DOS `MZ` `.EXE` image with 1 segment relocation entry exiting with `19` (`0x13`).
    let mut mz_exe = vec![0u8; 64];
    mz_exe[0] = b'M';
    mz_exe[1] = b'Z';
    mz_exe[2..4].copy_from_slice(&64u16.to_le_bytes());
    mz_exe[4..6].copy_from_slice(&1u16.to_le_bytes());
    mz_exe[6..8].copy_from_slice(&1u16.to_le_bytes());
    mz_exe[8..10].copy_from_slice(&2u16.to_le_bytes());
    mz_exe[14..16].copy_from_slice(&4u16.to_le_bytes());
    mz_exe[16..18].copy_from_slice(&0x0100u16.to_le_bytes());
    mz_exe[24..26].copy_from_slice(&28u16.to_le_bytes());
    mz_exe[28..30].copy_from_slice(&1u16.to_le_bytes());
    mz_exe[30..32].copy_from_slice(&0u16.to_le_bytes());
    let mz_body: [u8; 30] = [
        0xB8, 0x01, 0x00, // mov ax, 0x0001 (relocated!)
        0x8E, 0xD8, // mov ds, ax
        0xBA, 0x00, 0x00, // mov dx, 0
        0xB4, 0x09, // mov ah, 9
        0xCD, 0x21, // int 21h
        0xB8, 0x13, 0x4C, // mov ax, 4C13h
        0xCD, 0x21, // int 21h
        b'M', b'Z', b'_', b'R', b'E', b'L', b'_', b'O', b'K', 0x0D, 0x0A, b'$', 0,
    ];
    mz_exe[32..62].copy_from_slice(&mz_body);

    {
        let pe = load_arm_bin("build/fasmarm/arm32/WINCMD_ARMV4.EXE")?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(true)));
        let com_p = format!("{BS}Temp{BS}HELLO16.COM");
        let exe_p = format!("{BS}Temp{BS}RELOC16.EXE");
        let bat_p = format!("{BS}Temp{BS}run16.bat");
        env.borrow_mut().write_file(&com_p, &com_prog);
        env.borrow_mut().write_file(&exe_p, &mz_exe);
        let bat = join_lines(&[
            "@ECHO OFF".to_owned(),
            com_p,
            "CALL ECHO COM_EL=%%ERRORLEVEL%%".to_owned(),
            exe_p,
            "CALL ECHO MZ_EL=%%ERRORLEVEL%%".to_owned(),
        ]);
        env.borrow_mut().write_file(&bat_p, bat.as_bytes());
        let res = run_arm32(&pe, &format!("/C {bat_p}"), env)?;
        assert_eq!(res.exit_code, 0);
        assert!(res.stdout.contains("VM86_COM_OK"), "ARMv4 vm8086 .COM stdout: {}", res.stdout);
        assert!(res.stdout.contains("COM_EL=37"), "ARMv4 vm8086 .COM exit: {}", res.stdout);
        assert!(res.stdout.contains("MZ_REL_OK"), "ARMv4 vm8086 MZ stdout: {}", res.stdout);
        assert!(res.stdout.contains("MZ_EL=19"), "ARMv4 vm8086 MZ exit: {}", res.stdout);
    }

    for rel in [
        "build/fasmarm/arm32/WINCMD_ARM32.EXE",
        "build/llvm/arm32/WINCMD_ARM32.EXE",
    ] {
        let pe = load_arm_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let com_p = format!("C:{BS}Temp{BS}HELLO16.COM");
        let exe_p = format!("C:{BS}Temp{BS}RELOC16.EXE");
        let bat_p = format!("C:{BS}Temp{BS}run16.cmd");
        env.borrow_mut().write_file(&com_p, &com_prog);
        env.borrow_mut().write_file(&exe_p, &mz_exe);
        let bat = join_lines(&[
            "@ECHO OFF".to_owned(),
            com_p,
            "CALL ECHO COM_EL=%%ERRORLEVEL%%".to_owned(),
            exe_p,
            "CALL ECHO MZ_EL=%%ERRORLEVEL%%".to_owned(),
        ]);
        env.borrow_mut().write_file(&bat_p, bat.as_bytes());
        let res = run_arm32(&pe, &format!("/C {bat_p}"), env)?;
        assert_eq!(res.exit_code, 0, "{rel} exit mismatch");
        assert!(res.stdout.contains("VM86_COM_OK"), "{rel} .COM stdout: {}", res.stdout);
        assert!(res.stdout.contains("COM_EL=37"), "{rel} .COM exit: {}", res.stdout);
        assert!(res.stdout.contains("MZ_REL_OK"), "{rel} MZ stdout: {}", res.stdout);
        assert!(res.stdout.contains("MZ_EL=19"), "{rel} MZ exit: {}", res.stdout);
    }
    Ok(())
}
