//! End-to-end Milestone M0 & M1 integration tests exercising assembled `ASMCONV.COM` and
//! 3-part `WINCMD.COM` (`NASM`, `FASM`, `JWasm`) across MS-DOS 1.25, 2.00, 4.00, and 7.10.

use emu_cov::{DosEra, DosVm};
use std::path::PathBuf;
use wincmd_diag::ToolError;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .map_or_else(|| PathBuf::from("."), PathBuf::from)
}

fn with_crlf(s: &str) -> String {
    let mut out = String::from(s);
    out.push(char::from(0x0D));
    out.push(char::from(0x0A));
    out
}

#[test]
fn asmconv_com_lowers_canonical_source_inside_dos200_across_all_three_targets()
-> Result<(), ToolError> {
    let com_path = repo_root().join("build/jwasm/dos16/ASMCONV.COM");
    let com_bytes = std::fs::read(&com_path)?;

    let lines = [
        ";; @file mini.asm",
        "@cpu 8086",
        "@format com",
        "@const EXIT_OK = 0x4C00",
        "@proc start",
        "    mov dx, addr(msg)",
        "    mov ax, 0x4C00",
        "    int 0x21",
        "    ret",
        "@endproc",
    ];
    let mut canonical_in = Vec::new();
    for ln in lines {
        canonical_in.extend_from_slice(ln.as_bytes());
        canonical_in.extend_from_slice(&[0x0D, 0x0A]);
    }

    for (flag, out_name, expected_snippets) in [
        (
            "/N",
            "OUTN.ASM",
            &["CPU 8086", "ORG 0x0100", "EXIT_OK equ 0x4C00", "start:"][..],
        ),
        (
            "/F",
            "OUTF.ASM",
            &["format binary as 'com'", "EXIT_OK = 0x4C00", "start:"][..],
        ),
        (
            "/J",
            "OUTJ.ASM",
            &[
                ".8086",
                "_TEXT SEGMENT",
                "EXIT_OK EQU 04C00h",
                "start PROC NEAR",
                "OFFSET msg",
                "04C00h",
                "start ENDP",
                "_TEXT ENDS",
            ][..],
        ),
    ] {
        let mut vm = DosVm::new(DosEra::Dos200);
        vm.vfs.insert("IN.ASM".into(), canonical_in.clone());
        let tail = format!(" {flag} IN.ASM {out_name}");
        vm.load_com(&com_bytes, &tail, &[])?;
        let res = vm.run(500_000)?;
        assert_eq!(res.exit_code, 0, "ASMCONV {flag} failed");
        let generated =
            String::from_utf8_lossy(vm.vfs.get(out_name).ok_or_else(|| ToolError::CliUsage {
                message: format!("missing {out_name} in VFS"),
            })?);
        for needle in expected_snippets {
            assert!(
                generated.contains(needle),
                "ASMCONV {flag} output missing `{needle}`: {generated}"
            );
        }
    }

    Ok(())
}

#[test]
fn wincmd_com_64k_dos125_overlap_relocation_and_era_versions() -> Result<(), ToolError> {
    let mut tested = 0usize;
    for tc in ["nasm", "fasm", "jwasm"] {
        let p = repo_root().join(format!("build/{tc}/dos16/WINCMD.COM"));
        if !p.is_file() {
            continue;
        }
        tested += 1;
        let com_bytes = std::fs::read(p)?;

        // Simulate tight 64 KiB 8088 MS-DOS 1.25 RAM where wMemTop forces backward STD REP MOVSW!
        let mut vm = DosVm::new(DosEra::Dos125);
        vm.load_com(&com_bytes, " /C VER", &[])?;
        vm.write_u16(DosVm::DEFAULT_PSP_SEG, 0x0002, 0x1900);
        let res = vm.run(1_000_000)?;
        assert_eq!(res.exit_code, 0);
        assert_eq!(
            String::from_utf8_lossy(&res.stdout),
            with_crlf("WINCMD Pure-ASM Shell [MS-DOS 1.25]")
        );

        for (era, expected_ver) in [
            (DosEra::Dos200, "2.00"),
            (DosEra::Dos400, "4.00"),
            (DosEra::Dos710, "7.10"),
        ] {
            let mut evm = DosVm::new(era);
            evm.load_com(&com_bytes, " /C VER", &[])?;
            let eres = evm.run(1_000_000)?;
            assert_eq!(eres.exit_code, 0);
            assert_eq!(
                String::from_utf8_lossy(&eres.stdout),
                with_crlf(&format!("WINCMD Pure-ASM Shell [MS-DOS {expected_ver}]"))
            );
        }
    }
    assert!(tested >= 1, "at least one DOS16 toolchain must be built");
    Ok(())
}

#[test]
fn wincmd_com_detects_corrupted_transient_and_reloads_from_comspec() -> Result<(), ToolError> {
    let com_bytes = std::fs::read(repo_root().join("build/nasm/dos16/WINCMD.COM"))?;
    let mut vm = DosVm::new(DosEra::Dos400);
    let mut comspec_key = String::from("A:");
    comspec_key.push(char::from(0x5C));
    comspec_key.push_str("WINCMD.COM");
    vm.vfs.insert(comspec_key, com_bytes.clone());
    vm.load_com(&com_bytes, " /C ECHO Reloaded-OK", &[])?;

    let mut corrupted = false;
    for _ in 0..500_000 {
        vm.step()?;
        if !corrupted
            && vm.coverage.executed_pcs.len() > 80
            && vm.sregs[DosVm::CS] == DosVm::DEFAULT_PSP_SEG
        {
            // Corrupt relocated high-memory TRANSIENT at (0xA000 - 0x0A20) = 0x95E0:0x0004!
            vm.write_u8(0x95E0, 0x0004, 0xCC);
            corrupted = true;
            break;
        }
    }
    assert!(
        corrupted,
        "expected INIT to return to RESIDENT before transient call"
    );
    let res = vm.run(1_000_000)?;
    assert_eq!(res.exit_code, 0);
    assert_eq!(
        String::from_utf8_lossy(&res.stdout),
        with_crlf("Reloaded-OK")
    );
    Ok(())
}

#[test]
fn wincmd_com_set_a_edge_cases_if_and_exit_codes() -> Result<(), ToolError> {
    let fasm_com = repo_root().join("build/fasm/dos16/WINCMD.COM");
    let com_path = if fasm_com.is_file() {
        fasm_com
    } else {
        repo_root().join("build/nasm/dos16/WINCMD.COM")
    };
    let com_bytes = std::fs::read(com_path)?;

    let cases: &[(&str, u8, Option<&str>)] = &[
        (" /C SET /A 12+34*2", 0, Some("80")),
        (" /C SET /A (100-20)/4", 0, Some("20")),
        (" /C SET /A -2147483648/-1", 0, Some("-2147483648")),
        (" /C SET /A 0x10+010", 0, Some("24")),
        (" /C SET /A 10/0", 1, Some("Divide by zero error.")),
        (" /C SET /A 08", 1, Some("Invalid number.")),
        (
            " /C IF NOT foo==bar ECHO Branch-Taken",
            0,
            Some("Branch-Taken"),
        ),
        (" /C IF foo==bar ECHO Should-Not-Print", 0, None),
        (" /C EXIT 42", 42, None),
    ];

    for &(tail, exp_code, exp_line) in cases {
        let mut vm = DosVm::new(DosEra::Dos400);
        vm.load_com(&com_bytes, tail, &[])?;
        let res = vm.run(1_000_000)?;
        assert_eq!(res.exit_code, exp_code, "unexpected exit code on `{tail}`");
        let exp_out = exp_line.map_or_else(String::new, with_crlf);
        assert_eq!(
            String::from_utf8_lossy(&res.stdout),
            exp_out,
            "unexpected stdout on `{tail}`"
        );
    }

    Ok(())
}
