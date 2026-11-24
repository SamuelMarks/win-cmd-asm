//! End-to-end integration tests: deterministic `IA-32` & `x86-64` user-mode PE
//! emulation (`X86PeVm`), 5-ISA `lcov.info` trace generation (`emu-cov --pe` / `--com`),
//! `Win64` 16-byte stack alignment enforcement, multi-threaded `CreateThread` `ROBOCOPY`
//! on `X86PeVm`, and 7-target Cross-ISA + reference `cmd.exe` differential verification.

use emu_cov::win_mock::{BS, DQ, SharedWinEnv, emu_err};
use emu_cov::x86_pe::X86PeVm;
use std::cell::RefCell;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::rc::Rc;
use wincmd_diag::{ErrCode, ToolError};

const CRLF_BYTES: [u8; 2] = [13, 10];

const fn crlf() -> &'static str {
    match core::str::from_utf8(&CRLF_BYTES) {
        Ok(s) => s,
        Err(_) => "",
    }
}

fn repo_root() -> Result<PathBuf, ToolError> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| emu_err(0, ErrCode::GeneralFailure, "cannot resolve workspace root"))
}

fn verify_single_x86_shell(rel: &str, pe: &[u8], hn_com: &[u8]) -> Result<(), ToolError> {
    let nl = crlf();
    let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
    env.borrow_mut()
        .write_file(&format!("C:{BS}HOSTNAME.COM"), hn_com);
    let child_bat = [
        "@ECHO OFF".to_owned(),
        "IF 1==1 (".to_owned(),
        "    MissingToolXyz 2>NUL || EXIT /B 7".to_owned(),
        "    GOTO :DONE".to_owned(),
        ")".to_owned(),
        ":DONE".to_owned(),
        "EXIT /B 0".to_owned(),
    ]
    .join(nl)
        + nl;
    env.borrow_mut()
        .write_file(&format!("C:{BS}CHILD.CMD"), child_bat.as_bytes());
    let script = [
        "@ECHO OFF".to_owned(),
        "SET COMPUTERNAME=X86-VM86-NODE".to_owned(),
        format!("SET /A {DQ}R=(21*2)+(8<<1){DQ}"),
        "ECHO PIPE_ALPHA | ECHO R=%R%".to_owned(),
        format!("C:{BS}HOSTNAME.COM"),
        format!("CALL C:{BS}CHILD.CMD"),
        "ECHO CHILD_RC=%ERRORLEVEL%".to_owned(),
        "EXIT /B 9".to_owned(),
    ]
    .join(nl)
        + nl;
    env.borrow_mut()
        .write_file(&format!("C:{BS}M13.CMD"), script.as_bytes());

    let mut vm = X86PeVm::from_pe_bytes(pe, "/C M13.CMD", &[], Rc::clone(&env))?;
    let res = vm.run(15_000_000)?;
    assert_eq!(res.exit_code, 9, "{rel} exit code");
    assert!(res.stdout.contains("R=58"), "{rel} stdout: {}", res.stdout);
    assert!(
        res.stdout.contains("CHILD_RC=7"),
        "{rel} compound EXIT /B leaked GOTO: {} / stderr: {}",
        res.stdout,
        res.stderr
    );
    assert!(
        !res.stderr.contains("cannot find the batch label"),
        "{rel} unexpected label error: {}",
        res.stderr
    );
    assert!(
        res.stdout.contains("X86-VM86-NODE"),
        "{rel} vm8086 HOSTNAME.COM: {}",
        res.stdout
    );
    assert!(!res.executed_pcs.is_empty());
    assert!(!res.branch_edges.is_empty());
    Ok(())
}

#[test]
fn m13_x86_pe_vm_executes_all_ia32_and_x64_assemblers_pipes_vm86_and_mt_robocopy()
-> Result<(), ToolError> {
    let root = repo_root()?;
    let nl = crlf();
    let hn_com = fs::read(root.join("build/nasm/dos16/HOSTNAME.COM"))?;

    let mut tested_pe = 0usize;
    for rel in [
        "build/nasm/x86_32/WINCMD32.EXE",
        "build/fasm/x86_32/WINCMD32.EXE",
        "build/fasm-pe32/x86_32/WINCMD32.EXE",
        "build/jwasm/x86_32/WINCMD32.EXE",
        "build/nasm/x86_64/WINCMD64.EXE",
        "build/fasm/x86_64/WINCMD64.EXE",
        "build/fasm-coff/x86_64/WINCMD64.EXE",
        "build/jwasm/x86_64/WINCMD64.EXE",
    ] {
        let p = root.join(rel);
        if !p.is_file() {
            continue;
        }
        tested_pe += 1;
        let pe = fs::read(p)?;
        verify_single_x86_shell(rel, &pe, &hn_com)?;
    }
    assert!(tested_pe >= 2, "at least IA-32 and x86-64 builds must exist");

    // 2. Verify Win64 16-byte RSP hardware alignment check catches misaligned stack at API boundary.
    {
        let pe64 = fs::read(root.join("build/nasm/x86_64/WINCMD64.EXE"))?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let mut bad_vm = X86PeVm::from_pe_bytes(&pe64, "/C VER", &[], env)?;
        bad_vm.regs[4] -= 8; // Corrupt 16-byte RSP alignment before entry!
        assert!(
            bad_vm.run(500).is_err(),
            "X86PeVm must fault on misaligned Win64 RSP at API call boundary"
        );
    }

    // 3. Verify ROBOCOPY.EXE /MT:4 CreateThread worker pool on both IA-32 and x86-64 in X86PeVm!
    for rel in [
        "build/nasm/x86_32/ROBOCOPY.EXE",
        "build/nasm/x86_64/ROBOCOPY.EXE",
    ] {
        let pe = fs::read(root.join(rel))?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        env.borrow_mut().mkdir(&format!("C:{BS}src"));
        for idx in 1..=5 {
            env.borrow_mut().write_file(
                &format!("C:{BS}src{BS}item{idx}.txt"),
                format!("data_{idx}{nl}").as_bytes(),
            );
        }
        let cmd = format!("{DQ}ROBOCOPY.EXE{DQ} C:{BS}src C:{BS}dst /E /MT:4");
        let mut vm = X86PeVm::from_pe_bytes(&pe, &cmd, &[], Rc::clone(&env))?;
        let res = vm.run(15_000_000)?;
        assert_eq!(res.exit_code, 1, "{rel} ROBOCOPY bitmask 1");
        assert!(
            res.stdout.contains("Files Copied : 5"),
            "{rel}: {}",
            res.stdout
        );
        for idx in 1..=5 {
            assert!(
                env.borrow()
                    .read_file(&format!("C:{BS}dst{BS}item{idx}.txt"))
                    .is_some()
            );
        }
    }

    Ok(())
}

#[test]
fn m13_emu_cov_cli_emits_lcov_across_all_5_isa_families() -> Result<(), ToolError> {
    let root = repo_root()?;
    let tmp = std::env::temp_dir().join(format!("wincmd_m13_lcov_{}", std::process::id()));
    let _ = fs::remove_dir_all(&tmp);
    fs::create_dir_all(&tmp)?;

    let mut out = Vec::new();
    let dos_lcov = tmp.join("dos16.info");
    let _ = emu_cov::run_cli(
        [
            "emu-cov",
            "--com",
            &root.join("build/nasm/dos16/WINCMD.COM").to_string_lossy(),
            "--era",
            "7.10",
            "--args",
            " /C VER",
            "--lcov",
            &dos_lcov.to_string_lossy(),
        ],
        &mut out,
    )?;
    assert!(fs::read_to_string(&dos_lcov)?.contains("BRDA:"));

    let arm32_rel = if root.join("build/fasmarm/arm32/WINCMD_ARM32.EXE").is_file() {
        "build/fasmarm/arm32/WINCMD_ARM32.EXE"
    } else {
        "build/llvm/arm32/WINCMD_ARM32.EXE"
    };
    let arm64_rel = if root.join("build/fasmarm/arm64/WINCMD_ARM64.EXE").is_file() {
        "build/fasmarm/arm64/WINCMD_ARM64.EXE"
    } else {
        "build/llvm/arm64/WINCMD_ARM64.EXE"
    };
    let mut isa_targets = vec![
        ("x86_32", "build/nasm/x86_32/WINCMD32.EXE"),
        ("x86_64", "build/nasm/x86_64/WINCMD64.EXE"),
        ("arm32", arm32_rel),
        ("arm64", arm64_rel),
    ];
    if root.join("build/fasmarm/arm32/WINCMD_ARMV4.EXE").is_file() {
        isa_targets.push(("armv4", "build/fasmarm/arm32/WINCMD_ARMV4.EXE"));
    }
    for (tag, rel) in isa_targets {
        let lcov_file = tmp.join(format!("{tag}.info"));
        let res = emu_cov::run_cli(
            [
                "emu-cov",
                "--pe",
                &root.join(rel).to_string_lossy(),
                "--args",
                "/C VER",
                "--lcov",
                &lcov_file.to_string_lossy(),
            ],
            &mut out,
        )?;
        assert_eq!(res.exit_code, 0, "{tag} emu-cov --pe exit");
        let text = fs::read_to_string(&lcov_file)?;
        assert!(
            text.contains("DA:") && text.contains("BRDA:") && text.contains("end_of_record"),
            "{tag} lcov format"
        );
    }

    let _ = fs::remove_dir_all(&tmp);
    Ok(())
}

fn run_ref_cmd_if_available(bat_path: &Path) -> Option<(i32, String)> {
    let bat_s = bat_path.to_string_lossy().into_owned();
    if cfg!(target_os = "windows") {
        let out = Command::new("cmd.exe").args(["/C", &bat_s]).output().ok()?;
        return Some((
            out.status.code().unwrap_or(-1),
            String::from_utf8_lossy(&out.stdout).into_owned(),
        ));
    }
    let probe = Command::new("wine")
        .env("WINEDEBUG", "-all")
        .args(["cmd.exe", "/d", "/c", "ver"])
        .output()
        .ok()?;
    if probe.status.success() {
        let out = Command::new("wine")
            .env("WINEDEBUG", "-all")
            .args(["cmd.exe", "/C", &bat_s])
            .output()
            .ok()?;
        return Some((
            out.status.code().unwrap_or(-1),
            String::from_utf8_lossy(&out.stdout).into_owned(),
        ));
    }
    None
}

#[test]
fn m13_reference_cmd_exe_differential_parity() -> Result<(), ToolError> {
    let root = repo_root()?;
    let nl = crlf();
    let tmp = std::env::temp_dir().join(format!("wincmd_m13_ref_{}", std::process::id()));
    let _ = fs::remove_dir_all(&tmp);
    fs::create_dir_all(&tmp)?;

    let script = [
        "@ECHO OFF".to_owned(),
        format!("SET /A {DQ}A=(15+5)*3, B=(A>>2)^7{DQ}"),
        "ECHO ARITH=%A%_%B%".to_owned(),
        "SET S=HELLO_OLD_WORLD".to_owned(),
        "ECHO SLICE=%S:~6,3%_%S:OLD=NEW%".to_owned(),
        "SETLOCAL EnableDelayedExpansion".to_owned(),
        "SET V=INIT".to_owned(),
        format!("SET {DQ}V=DELAYED{DQ} & ECHO D=!V!"),
        "ENDLOCAL".to_owned(),
        "FOR %%X IN (ONE TWO THREE) DO ECHO F=%%X".to_owned(),
        "CALL :SUB 99".to_owned(),
        "EXIT /B 23".to_owned(),
        ":SUB".to_owned(),
        "ECHO SUB_ARG=%1".to_owned(),
        "EXIT /B 0".to_owned(),
    ]
    .join(nl)
        + nl;

    let bat_path = tmp.join("ref_parity.cmd");
    fs::write(&bat_path, script.as_bytes())?;

    // Execute on our X86PeVm (WINCMD64.EXE)
    let pe64 = fs::read(root.join("build/nasm/x86_64/WINCMD64.EXE"))?;
    let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
    env.borrow_mut()
        .write_file(&format!("C:{BS}ref_parity.cmd"), script.as_bytes());
    let mut vm = X86PeVm::from_pe_bytes(&pe64, &format!("/C C:{BS}ref_parity.cmd"), &[], env)?;
    let ours = vm.run(12_000_000)?;
    assert_eq!(ours.exit_code, 23);

    // Compare against real reference cmd.exe (Windows or Wine cmd.exe) if available on host!
    if let Some((ref_code, ref_out)) = run_ref_cmd_if_available(&bat_path) {
        let lf = core::str::from_utf8(&[10]).unwrap_or("");
        let norm_ours = ours.stdout.replace(nl, lf).trim().to_owned();
        let norm_ref = ref_out.replace(nl, lf).trim().to_owned();
        assert_eq!(ref_code, 23, "reference cmd.exe exit code parity");
        assert_eq!(norm_ours, norm_ref, "reference cmd.exe stdout parity");
    }

    let _ = fs::remove_dir_all(&tmp);
    Ok(())
}

#[test]
fn m13_mz16_executable_and_omf_object_verification() -> Result<(), ToolError> {
    let root = repo_root()?;
    for omf_rel in [
        "build/nasm/dos16/wincmdmz.obj",
        "build/jwasm/dos16/wincmdmz.obj",
    ] {
        let omf = fs::read(root.join(omf_rel))?;
        assert!(omf.len() >= 32, "{omf_rel} non-empty OMF object");
        assert_eq!(omf[0], 0x80, "{omf_rel} OMF THEADR (0x80) record header");
    }

    let wincmd_com = fs::read(root.join("build/nasm/dos16/WINCMD.COM"))?;
    let mut tested_mz = 0usize;
    for mz_rel in [
        "build/fasm/dos16/WINCMDMZ.EXE",
        "build/jwasm/dos16/WINCMDMZ.EXE",
    ] {
        let mz_path = root.join(mz_rel);
        if !mz_path.is_file() {
            continue;
        }
        tested_mz += 1;
        let mz_bytes = fs::read(mz_path)?;
        assert_eq!(&mz_bytes[0..2], b"MZ", "{mz_rel} MZ signature");

        for (era, ver_tag) in [
            (emu_cov::DosEra::Dos125, "1.25]"),
            (emu_cov::DosEra::Dos400, "4.00]"),
        ] {
            let mut dvm = emu_cov::DosVm::new(era);
            dvm.vfs
                .insert(String::from("WINCMDMZ.EXE"), mz_bytes.clone());
            dvm.load_com(
                &wincmd_com,
                " /C WINCMDMZ.EXE",
                &[("COMSPEC", "A:\\WINCMD.COM")],
            )?;
            let dres = dvm.run(2_000_000)?;
            let out = String::from_utf8_lossy(&dres.stdout);
            assert_eq!(dres.exit_code, 0, "{mz_rel} on {era:?}");
            assert!(
                out.contains("WINCMDMZ") && out.contains(ver_tag),
                "got: {out}"
            );
        }

        let pe64 = fs::read(root.join("build/nasm/x86_64/WINCMD64.EXE"))?;
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        env.borrow_mut()
            .write_file(&format!("C:{BS}WINCMDMZ.EXE"), &mz_bytes);
        let mut vm = X86PeVm::from_pe_bytes(&pe64, &format!("/C C:{BS}WINCMDMZ.EXE"), &[], env)?;
        let res = vm.run(10_000_000)?;
        assert_eq!(res.exit_code, 0, "{mz_rel} via x64 vm8086");
        assert!(res.stdout.contains("WINCMDMZ"), "got: {}", res.stdout);

        let cov_a = emu_cov::CoverageData::from_pe_trace(
            res.executed_pcs.iter().copied(),
            res.branch_edges.iter().copied(),
        );
        let mut merged = emu_cov::CoverageData::default();
        merged.merge(&cov_a);
        merged.verify_branch_coverage("x64-mz", 5_000)?;
        let lcov = merged.to_lcov("WINCMD64.EXE");
        assert!(lcov.contains("BRF:") && lcov.contains("BRH:"));
    }
    assert!(tested_mz >= 1, "at least one MZ16 executable must be built");

    let bin = env!("CARGO_BIN_EXE_emu-cov");
    let lcov_tmp = std::env::temp_dir().join(format!("wincmd_m13_cli_{}.info", std::process::id()));
    let ok = Command::new(bin)
        .args([
            "--pe",
            &root
                .join("build/nasm/x86_64/WINCMD64.EXE")
                .to_string_lossy(),
            "--args",
            "/C VER",
            "--lcov",
            &lcov_tmp.to_string_lossy(),
        ])
        .output()?;
    assert!(ok.status.success());
    let _ = fs::remove_file(&lcov_tmp);

    let fail = Command::new(bin).arg("--unknown-opt").output()?;
    assert!(!fail.status.success());
    Ok(())
}
