//! End-to-end integration tests for `WINCMD_ARM64.EXE` (`0xAA64` `PE32+` `Subsystem 10.0`
//! across `fasmarm` direct `PE64` and `llvm-mc` + `lld-link` builds), including `X18` (`TEB`)
//! immutability, hardware 16-byte `SP` alignment, `PAC`/`BTI`, concurrent `CreatePipe`,
//! `MKLINK`, `\?` `32K` long paths, per-directory case sensitivity, and `ARM64` `vm8086`.

use emu_cov::arm64_pe::{Arm64RunResult, Arm64Vm};
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

fn load_arm64_bin(rel: &str) -> Result<Vec<u8>, ToolError> {
    let p = repo_root()?.join(rel);
    Ok(fs::read(p)?)
}

fn run_arm64(
    pe: &[u8],
    cmdline: &str,
    shared: Rc<RefCell<SharedWinEnv>>,
) -> Result<Arm64RunResult, ToolError> {
    let mut vm = Arm64Vm::from_pe_bytes(pe, cmdline, &[], shared)?;
    vm.run(48_000_000)
}

#[test]
fn m11_pe64_headers_security_pdata_pac_bti_and_zero_crt_imports() -> Result<(), ToolError> {
    for (tc, rel) in [
        ("fasmarm", "build/fasmarm/arm64/WINCMD_ARM64.EXE"),
        ("llvm", "build/llvm/arm64/WINCMD_ARM64.EXE"),
    ] {
        let pe = load_arm64_bin(rel)?;
        assert!(
            pe.len() <= 131_072,
            "{rel} size {} exceeds 128 KiB budget",
            pe.len()
        );
        let lfa = usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]]))
            .unwrap_or(0);
        assert_eq!(&pe[lfa..lfa + 4], &[b'P', b'E', 0, 0]);
        let mach = u16::from_le_bytes([pe[lfa + 4], pe[lfa + 5]]);
        assert_eq!(mach, 0xAA64, "{rel} must target ARM64 AArch64 (0xAA64)");

        let opt = lfa + 24;
        let magic = u16::from_le_bytes([pe[opt], pe[opt + 1]]);
        assert_eq!(magic, 0x020B, "{rel} must use PE32+ magic 0x020B");

        let mut ib_bytes = [0u8; 8];
        ib_bytes.copy_from_slice(&pe[opt + 24..opt + 32]);
        let image_base = u64::from_le_bytes(ib_bytes);
        assert!(
            image_base >= 0x1_0000_0000,
            "{rel} must use 64-bit high-entropy ImageBase >= 4 GiB (0x{image_base:X})"
        );

        let maj_sub = u16::from_le_bytes([pe[opt + 48], pe[opt + 49]]);
        let min_sub = u16::from_le_bytes([pe[opt + 50], pe[opt + 51]]);
        assert_eq!(
            (maj_sub, min_sub),
            (10, 0),
            "{rel} must target Subsystem 10.0 (Windows 10 & Windows 11 on ARM)"
        );
        let subsys = u16::from_le_bytes([pe[opt + 68], pe[opt + 69]]);
        assert_eq!(subsys, 3, "{rel} must target WINDOWS_CUI (3)");

        let dll_chars = u16::from_le_bytes([pe[opt + 70], pe[opt + 71]]);
        if tc == "fasmarm" {
            assert_eq!(dll_chars & 0x0140, 0x0140, "{rel} missing DYNAMIC_BASE | NX_COMPAT");
        } else {
            assert_eq!(
                dll_chars & 0x4160,
                0x4160,
                "{rel} missing HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT | GUARD_CF"
            );
        }

        let pdata_rva =
            u32::from_le_bytes([pe[opt + 136], pe[opt + 137], pe[opt + 138], pe[opt + 139]]);
        let pdata_sz =
            u32::from_le_bytes([pe[opt + 140], pe[opt + 141], pe[opt + 142], pe[opt + 143]]);
        assert!(
            pdata_rva != 0 && pdata_sz >= 256,
            "{rel} missing .pdata ARM64 unwind directory"
        );

        let lcfg_rva =
            u32::from_le_bytes([pe[opt + 192], pe[opt + 193], pe[opt + 194], pe[opt + 195]]);
        assert_ne!(lcfg_rva, 0, "{rel} missing LOAD_CONFIG directory");

        // Verify PAC/BTI `BTI c` (`0xD503245F`) + `PACIASP` (`0xD503233F`) in `.text`!
        assert!(
            pe.windows(4).any(|w| w == 0xD503_245Fu32.to_le_bytes()),
            "{rel} missing BTI c (0xD503245F) instruction"
        );
        assert!(
            pe.windows(4).any(|w| w == 0xD503_233Fu32.to_le_bytes()),
            "{rel} missing PACIASP (0xD503233F) instruction"
        );
        assert!(
            pe.windows(4).any(|w| w == 0xD503_23BFu32.to_le_bytes()),
            "{rel} missing AUTIASP (0xD50323BF) instruction"
        );

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
fn m11_x18_teb_immutability_and_16byte_sp_alignment_hardware_guards() -> Result<(), ToolError> {
    let pe = load_arm64_bin("build/llvm/arm64/WINCMD_ARM64.EXE")?;
    let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));

    // 1. Verify `Arm64Vm` faults immediately if `SP` is misaligned (`SP % 16 != 0`).
    let mut bad_sp_vm = Arm64Vm::from_pe_bytes(&pe, "/C VER", &[], Rc::clone(&env))?;
    bad_sp_vm.sp -= 8;
    assert!(
        bad_sp_vm.run(32).is_err(),
        "Arm64Vm must fault on misaligned SP memory access"
    );

    // 2. Verify `Arm64Vm` faults immediately if `X18` (`TEB`) is corrupted.
    let mut bad_x18_vm = Arm64Vm::from_pe_bytes(&pe, "/C VER", &[], env)?;
    bad_x18_vm.xregs[18] = 0xDEAD_BEEF;
    assert!(
        bad_x18_vm.run(8).is_err(),
        "Arm64Vm must fault when X18 (TEB) is modified"
    );
    Ok(())
}

#[test]
fn m11_arith_expansion_delayed_utf16_bom_and_setlocal_all_arm64_builds() -> Result<(), ToolError> {
    let script = join_lines(&[
        "@ECHO OFF".to_owned(),
        format!("SET /A {DQ}A=(10+5)*3 - (8<<1), B=100/0{DQ} 2>NUL"),
        format!("SET /A {DQ}M=-2147483648/-1, S=1<<35{DQ}"),
        "SET STR=HelloArmWorld".to_owned(),
        "SET SUB=%STR:~5,3%".to_owned(),
        "SET REP=%STR:Arm=AArch64%".to_owned(),
        "SETLOCAL EnableDelayedExpansion".to_owned(),
        "SET V=init".to_owned(),
        format!("(SET {DQ}V=delayed_ok{DQ} & ECHO !V!_{DQ}%A%{DQ}_%S%_%SUB%_%REP%)"),
        "ENDLOCAL".to_owned(),
        "ECHO AFTER=%V%".to_owned(),
    ]);

    // UTF-16LE BOM (`FF FE`) batch script test
    let mut u16_bytes = vec![0xFFu8, 0xFEu8];
    let u16_src = join_lines(&["@ECHO OFF".to_owned(), "ECHO UTF16_BOM_OK".to_owned()]);
    for u in u16_src.encode_utf16() {
        u16_bytes.extend_from_slice(&u.to_le_bytes());
    }

    let mut outputs = Vec::new();
    for rel in [
        "build/fasmarm/arm64/WINCMD_ARM64.EXE",
        "build/llvm/arm64/WINCMD_ARM64.EXE",
    ] {
        let pe = load_arm64_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let bat_path = format!("C:{BS}Temp{BS}arith64.cmd");
        let u16_path = format!("C:{BS}Temp{BS}u16.cmd");
        env.borrow_mut().write_file(&bat_path, script.as_bytes());
        env.borrow_mut().write_file(&u16_path, &u16_bytes);

        let res = run_arm64(
            &pe,
            &format!("/C {DQ}CALL {bat_path} & CALL {u16_path}{DQ}"),
            env,
        )?;
        assert_eq!(res.exit_code, 0, "{rel} stderr: {}", res.stderr);
        assert!(
            res.stdout
                .contains(&format!("delayed_ok_{DQ}29{DQ}_8_Arm_HelloAArch64World")),
            "{rel} unexpected stdout: {}",
            res.stdout
        );
        assert!(res.stdout.contains("AFTER="), "{rel} SETLOCAL unwind failed");
        assert!(
            res.stdout.contains("UTF16_BOM_OK"),
            "{rel} UTF-16LE BOM batch failed: {}",
            res.stdout
        );
        assert!(res.coverage.executed_pcs.len() > 500);
        outputs.push(res.stdout);
    }
    assert_eq!(outputs[0], outputs[1], "fasmarm vs llvm-mc stdout mismatch");
    Ok(())
}

#[test]
fn m11_batch_control_flow_for_if_call_shift_and_autorun_all_arm64_builds() -> Result<(), ToolError>
{
    let script = join_lines(&[
        "@ECHO OFF".to_owned(),
        "SET SUM=0".to_owned(),
        "FOR /L %%I IN (1,2,7) DO CALL :ADD %%I".to_owned(),
        format!("FOR /F {DQ}tokens=2 delims=:{DQ} %%A IN ({DQ}k1:alpha{DQ}) DO SET TOK=%%A"),
        format!("IF /I {DQ}%TOK%{DQ}=={DQ}ALPHA{DQ} IF %SUM% EQU 16 ECHO FLOW_OK_%SUM%_%TOK%"),
        "CALL :SHIFT_TEST first second third".to_owned(),
        "EXIT /B 41".to_owned(),
        ":ADD".to_owned(),
        "SET /A SUM+=%1".to_owned(),
        "GOTO :EOF".to_owned(),
        ":SHIFT_TEST".to_owned(),
        "SHIFT".to_owned(),
        "ECHO SHIFTED=%1_%2".to_owned(),
        "EXIT /B 0".to_owned(),
    ]);

    for rel in [
        "build/fasmarm/arm64/WINCMD_ARM64.EXE",
        "build/llvm/arm64/WINCMD_ARM64.EXE",
    ] {
        let pe = load_arm64_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let bat = format!("C:{BS}Temp{BS}flow64.cmd");
        env.borrow_mut().write_file(&bat, script.as_bytes());
        env.borrow_mut().set_reg_sz(
            &format!("HKCU{BS}Software{BS}Microsoft{BS}Command Processor"),
            "AutoRun",
            "ECHO AUTORUN_FIRED",
        );

        let res_auto = run_arm64(&pe, &format!("/C {bat}"), Rc::clone(&env))?;
        assert_eq!(res_auto.exit_code, 41, "{rel} exit code mismatch");
        assert!(res_auto.stdout.contains("AUTORUN_FIRED"), "{rel} AutoRun missing");
        assert!(res_auto.stdout.contains("FLOW_OK_16_alpha"), "{rel} out: {}", res_auto.stdout);
        assert!(
            res_auto.stdout.contains("SHIFTED=second_third"),
            "{rel} out: {}",
            res_auto.stdout
        );

        // `/D` must suppress Registry AutoRun!
        let res_noauto = run_arm64(&pe, &format!("/D /C {bat}"), env)?;
        assert!(!res_noauto.stdout.contains("AUTORUN_FIRED"), "{rel} /D failed");
    }
    Ok(())
}

#[test]
fn m11_win11_arm64_concurrent_pipes_mklink_registry_longpath_and_case_sens() -> Result<(), ToolError>
{
    for rel in [
        "build/fasmarm/arm64/WINCMD_ARM64.EXE",
        "build/llvm/arm64/WINCMD_ARM64.EXE",
    ] {
        let pe = load_arm64_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        env.borrow_mut().write_file(
            &format!("C:{BS}Windows{BS}System32{BS}WINCMD_ARM64.EXE"),
            &pe,
        );

        let cs_dir = format!("C:{BS}Temp{BS}DevDriveCase");
        env.borrow_mut().set_dir_case_sensitive(&cs_dir);
        env.borrow_mut()
            .write_file(&format!("{cs_dir}{BS}ExactArm64.txt"), b"UPPER");
        env.borrow_mut()
            .write_file(&format!("{cs_dir}{BS}exactarm64.txt"), b"lower");

        let seg = "Arm64LongSubDirSegmentName1234567890";
        let deep_dir = format!(
            "C:{BS}Temp{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}{BS}{seg}"
        );
        assert!(deep_dir.len() > 280);

        let script = join_lines(&[
            "@ECHO OFF".to_owned(),
            "CHCP 65001 >NUL".to_owned(),
            "ASSOC .a64=Arm64AsmFile >NUL".to_owned(),
            "ASSOC .a64".to_owned(),
            format!("FTYPE Arm64AsmFile=C:{BS}bin{BS}a64.exe {DQ}%%1{DQ} >NUL"),
            "FTYPE Arm64AsmFile".to_owned(),
            format!("MD C:{BS}Temp{BS}RealTarget"),
            format!("ECHO keep_safe>C:{BS}Temp{BS}RealTarget{BS}precious.txt"),
            format!("MKLINK /J C:{BS}Temp{BS}JuncLink C:{BS}Temp{BS}RealTarget >NUL"),
            format!("DIR C:{BS}Temp"),
            format!("RD /S /Q C:{BS}Temp{BS}JuncLink"),
            format!("IF EXIST C:{BS}Temp{BS}RealTarget{BS}precious.txt ECHO JUNC_SAFE"),
            format!("MD {DQ}{deep_dir}{DQ}"),
            format!("ECHO deep64_payload>{DQ}{deep_dir}{BS}leaf.txt{DQ}"),
            format!("TYPE {DQ}{deep_dir}{BS}leaf.txt{DQ}"),
            format!("DIR /B {cs_dir}{BS}Exact*"),
            "VER".to_owned(),
        ]);

        let bat = format!("C:{BS}Temp{BS}arm64_full.cmd");
        env.borrow_mut().write_file(&bat, script.as_bytes());

        let pipe_cmd = format!(
            "/S /D /C {DQ}CALL {bat} & (ECHO pipe64_stage_data | {DQ}C:{BS}Windows{BS}System32{BS}WINCMD_ARM64.EXE{DQ} /D /V:ON /C {DQ}SET /P LINE= & ECHO PIPED64=!LINE!{DQ}){DQ}"
        );
        let res = run_arm64(&pe, &pipe_cmd, Rc::clone(&env))?;
        assert_eq!(res.exit_code, 0, "{rel} stderr: {}", res.stderr);
        assert!(res.stdout.contains(".a64=Arm64AsmFile"), "{rel} out: {}", res.stdout);
        assert!(
            res.stdout.contains(&format!("Arm64AsmFile=C:{BS}bin{BS}a64.exe")),
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
            res.stdout.contains("deep64_payload"),
            "{rel} >260 long path failed: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("ExactArm64.txt"),
            "{rel} case-sens match missing: {}",
            res.stdout
        );
        assert!(
            !res.stdout.contains("exactarm64.txt"),
            "{rel} case-sensitive directory matched wrong case: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("PIPED64=pipe64_stage_data"),
            "{rel} concurrent CreatePipe failed: {}",
            res.stdout
        );
        assert!(
            res.stdout.contains("10.0.22631"),
            "{rel} VER missing X18 TEB -> PEB Win11 build: {}",
            res.stdout
        );
    }
    Ok(())
}

#[test]
fn m11_vm8086_executes_real_16bit_com_and_mz_exe_on_win11_arm64() -> Result<(), ToolError> {
    // 1. Real 16-bit 8086 `.COM` binary (`ORG 0100h`): prints `A64_VM86_COM_OK` via `INT 21h AH=09h` and exits `37`.
    let com_prog: Vec<u8> = vec![
        0xBA, 0x0E, 0x01, // mov dx, 0x010E
        0xB4, 0x09, // mov ah, 0x09
        0xCD, 0x21, // int 0x21
        0xB8, 0x25, 0x4C, // mov ax, 0x4C25
        0xCD, 0x21, // int 0x21
        0x90, 0x90, // pad to 0x0E
        b'A', b'6', b'4', b'_', b'V', b'M', b'8', b'6', b'_', b'O', b'K', 0x0D, 0x0A, b'$',
    ];

    // 2. Relocated 16-bit DOS `MZ` `.EXE` image with segment relocation exiting `19` (`0x13`).
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
        b'M', b'Z', b'_', b'A', b'6', b'4', b'_', b'O', b'K', 0x0D, 0x0A, b'$', 0,
    ];
    mz_exe[32..62].copy_from_slice(&mz_body);

    // 3. Also load our actual assembled 16-bit `HOSTNAME.COM` utility from `build/nasm/dos16/HOSTNAME.COM`!
    let real_hostname_com = load_arm64_bin("build/nasm/dos16/HOSTNAME.COM")?;

    for rel in [
        "build/fasmarm/arm64/WINCMD_ARM64.EXE",
        "build/llvm/arm64/WINCMD_ARM64.EXE",
    ] {
        let pe = load_arm64_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let com_p = format!("C:{BS}Temp{BS}hello16.com");
        let mz_p = format!("C:{BS}Temp{BS}reloc16.exe");
        let hn_p = format!("C:{BS}Temp{BS}hostname.com");
        env.borrow_mut().write_file(&com_p, &com_prog);
        env.borrow_mut().write_file(&mz_p, &mz_exe);
        env.borrow_mut().write_file(&hn_p, &real_hostname_com);

        let bat = format!("C:{BS}Temp{BS}run86.cmd");
        let script = join_lines(&[
            "@ECHO OFF".to_owned(),
            "SET COMPUTERNAME=WIN11-ARM64-BOX".to_owned(),
            com_p,
            "ECHO COM_EL=%ERRORLEVEL%".to_owned(),
            mz_p,
            "ECHO MZ_EL=%ERRORLEVEL%".to_owned(),
            hn_p,
        ]);
        env.borrow_mut().write_file(&bat, script.as_bytes());

        let res = run_arm64(&pe, &format!("/D /C {bat}"), env)?;
        assert!(res.stdout.contains("A64_VM86_OK"), "{rel} .COM out: {}", res.stdout);
        assert!(res.stdout.contains("COM_EL=37"), "{rel} .COM exit: {}", res.stdout);
        assert!(res.stdout.contains("MZ_A64_OK"), "{rel} MZ out: {}", res.stdout);
        assert!(res.stdout.contains("MZ_EL=19"), "{rel} MZ exit: {}", res.stdout);
        assert!(
            res.stdout.contains("WIN11-ARM64-BOX"),
            "{rel} real 16-bit HOSTNAME.COM failed on ARM64 vm8086: {}",
            res.stdout
        );
    }
    Ok(())
}

#[test]
fn m11_self_host_executes_real_build_cmd_on_arm64() -> Result<(), ToolError> {
    let build_cmd = load_arm64_bin("BUILD.CMD")?;
    for rel in [
        "build/fasmarm/arm64/WINCMD_ARM64.EXE",
        "build/llvm/arm64/WINCMD_ARM64.EXE",
    ] {
        let pe = load_arm64_bin(rel)?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let bcmd_path = format!("C:{BS}Temp{BS}BUILD.CMD");
        env.borrow_mut().write_file(&bcmd_path, &build_cmd);

        let res = run_arm64(
            &pe,
            &format!("/D /C {DQ}CD /D C:{BS}Temp & CALL BUILD.CMD & IF EXIST build{BS}llvm{BS}arm64 ECHO BUILD_DIRS_OK{DQ}"),
            env,
        )?;
        assert_eq!(res.exit_code, 0, "{rel} BUILD.CMD failed: {}", res.stderr);
        assert!(
            res.stdout.contains("BUILD_DIRS_OK"),
            "{rel} BUILD.CMD did not create build/llvm/arm64: {}",
            res.stdout
        );
    }
    Ok(())
}
