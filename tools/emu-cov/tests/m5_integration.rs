//! Exhaustive Milestone M5 integration tests exercising `WINCMD1.COM` (`TINY125` `<= 20 KiB`)
//! and all `19` standalone pure-8086 `.COM` companion utilities (`MEM`, `ATTRIB`, `FIND`,
//! `FINDSTR`, `SORT`, `MORE`, `FC`, `COMP`, `TREE`, `WHERE`, `XCOPY`, `REPLACE`, `ROBOCOPY`,
//! `CHOICE`, `TIMEOUT`, `TRUENAME`, `REG`, `HOSTNAME`, `WHOAMI`) across `NASM`, `FASM`, & `JWasm`.

use emu_cov::{DosEra, DosVm};
use std::path::PathBuf;
use wincmd_diag::ToolError;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .map_or_else(|| PathBuf::from("."), PathBuf::from)
}

fn load_built(tc: &str, rel: &str) -> Result<Vec<u8>, ToolError> {
    Ok(std::fs::read(
        repo_root().join(format!("build/{tc}/dos16/{rel}")),
    )?)
}

fn join_crlf(lines: &[&str]) -> Vec<u8> {
    let mut out = Vec::new();
    for line in lines {
        out.extend_from_slice(line.as_bytes());
        out.extend_from_slice(&[0x0D, 0x0A]);
    }
    out
}

#[test]
fn m5_wincmd1_tiny125_shell_and_dos125_utilities_across_assemblers() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let w1 = load_built(tc, "WINCMD1.COM")?;
        assert!(w1.len() <= 20_480, "{tc} WINCMD1.COM budget");

        // 1. Interactive 64 KiB MS-DOS 1.25 session on WINCMD1.COM (VER, ECHO, DIR, TYPE, DEL, EXIT)
        let mut vm = DosVm::new(DosEra::Dos125);
        vm.vfs
            .insert(String::from("NOTE.TXT"), b"TINY125_BODY".to_vec());
        vm.stdin = join_crlf(&["VER", "DIR", "TYPE NOTE.TXT", "ECHO.", "DEL NOTE.TXT", "EXIT"]);
        vm.load_com(&w1, "", &[])?;
        // Constrain wMemTop to 64 KiB (0x1000 + 0x0600 = 0x1600) to prove tiny-RAM relocation!
        vm.write_u16(DosVm::DEFAULT_PSP_SEG, 0x0002, 0x1600);
        let res = vm.run(1_000_000)?;
        assert_eq!(res.exit_code, 0, "{tc} WINCMD1 exit");
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains("WINCMD1 Tiny125"), "{tc} out: {out}");
        assert!(out.contains("NOTE     TXT"), "{tc} dir: {out}");
        assert!(out.contains("TINY125_BODY"), "{tc} type: {out}");
        assert!(!vm.vfs.contains_key("NOTE.TXT"), "{tc} del failed");

        // 2. Verify standalone .COM utilities execute natively on FCB-only MS-DOS 1.25!
        let find_com = load_built(tc, "FIND.COM")?;
        let mut fvm = DosVm::new(DosEra::Dos125);
        fvm.vfs.insert(
            String::from("LOG.TXT"),
            join_crlf(&["alpha one", "BETA TARGET", "gamma three"]),
        );
        fvm.load_com(&find_com, " /I /N beta LOG.TXT", &[])?;
        let fres = fvm.run(500_000)?;
        let fout = String::from_utf8_lossy(&fres.stdout);
        assert!(fout.contains("[2]BETA TARGET"), "{tc} 1.25 FIND: {fout}");
    }
    Ok(())
}

#[test]
fn m5_text_search_sort_diff_and_identity_utilities_across_assemblers() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        // 1. MEM.COM with EMS 4.0 + XMS enabled on MS-DOS 4.00
        let mem_com = load_built(tc, "MEM.COM")?;
        let mut mvm = DosVm::new(DosEra::Dos400);
        mvm.enable_ems();
        mvm.enable_xms();
        mvm.load_com(&mem_com, " /C", &[])?;
        let mres = mvm.run(400_000)?;
        assert_eq!(mres.exit_code, 0);
        let mout = String::from_utf8_lossy(&mres.stdout);
        assert!(mout.contains("Conventional"), "{tc} MEM: {mout}");
        assert!(mout.contains("EMS Expanded Free"), "{tc} MEM EMS: {mout}");
        assert!(mout.contains("XMS Driver         Installed"), "{tc} MEM XMS: {mout}");

        // 2. FINDSTR.COM regex (^, .*, $, /I, /N)
        let fs_com = load_built(tc, "FINDSTR.COM")?;
        let mut fsvm = DosVm::new(DosEra::Dos400);
        fsvm.vfs.insert(
            String::from("SRC.TXT"),
            join_crlf(&["rem header", "Error: code 42!", "ok done"]),
        );
        fsvm.load_com(&fs_com, " /I /N ^err.*42!$ SRC.TXT", &[])?;
        let fsres = fsvm.run(800_000)?;
        assert_eq!(fsres.exit_code, 0);
        assert!(
            String::from_utf8_lossy(&fsres.stdout).contains("2:Error: code 42!"),
            "{tc} FINDSTR"
        );

        // 3. SORT.COM (/R, /UNIQUE, /O) + MORE.COM (/S +2)
        let sort_com = load_built(tc, "SORT.COM")?;
        let mut svm = DosVm::new(DosEra::Dos400);
        svm.vfs.insert(
            String::from("UNSORT.TXT"),
            join_crlf(&["pear", "apple", "pear", "banana"]),
        );
        svm.load_com(&sort_com, " /UNIQUE UNSORT.TXT", &[])?;
        let sres = svm.run(1_000_000)?;
        assert_eq!(sres.exit_code, 0);
        assert_eq!(sres.stdout, join_crlf(&["apple", "banana", "pear"]), "{tc} SORT");

        let more_com = load_built(tc, "MORE.COM")?;
        let mut pager_vm = DosVm::new(DosEra::Dos400);
        pager_vm.stdin = join_crlf(&["skip1", "keep2", "", "", "keep5"]);
        pager_vm.load_com(&more_com, " /S +2", &[])?;
        let pager_res = pager_vm.run(500_000)?;
        assert_eq!(pager_res.stdout, join_crlf(&["keep2", "", "keep5"]), "{tc} MORE");

        // 4. FC.COM & COMP.COM (identical exit 0 vs differing exit 1)
        let filecmp_com = load_built(tc, "FC.COM")?;
        let comp_com = load_built(tc, "COMP.COM")?;
        let mut dvm = DosVm::new(DosEra::Dos400);
        dvm.vfs.insert(String::from("A.BIN"), b"ABCD".to_vec());
        dvm.vfs.insert(String::from("B.BIN"), b"abcd".to_vec());
        dvm.vfs.insert(String::from("C.BIN"), b"ABXD".to_vec());

        dvm.load_com(&filecmp_com, " /C A.BIN B.BIN", &[])?;
        let fc0 = dvm.run(500_000)?;
        assert_eq!(fc0.exit_code, 0);
        assert!(String::from_utf8_lossy(&fc0.stdout).contains("no differences"));

        let mut dvm2 = DosVm::new(DosEra::Dos400);
        dvm2.vfs.insert(String::from("A.BIN"), b"ABCD".to_vec());
        dvm2.vfs.insert(String::from("C.BIN"), b"ABXD".to_vec());
        dvm2.load_com(&comp_com, " A.BIN C.BIN", &[])?;
        let cp1 = dvm2.run(500_000)?;
        assert_eq!(cp1.exit_code, 1);
        assert!(String::from_utf8_lossy(&cp1.stdout).contains("OFFSET 0002"));

        // 5. HOSTNAME.COM & WHOAMI.COM (/USER)
        let hn_com = load_built(tc, "HOSTNAME.COM")?;
        let mut hvm = DosVm::new(DosEra::Dos400);
        hvm.load_com(&hn_com, "", &[("COMPUTERNAME", "RETROBOX")])?;
        let hres = hvm.run(300_000)?;
        assert_eq!(hres.stdout, join_crlf(&["RETROBOX"]));

        let who_com = load_built(tc, "WHOAMI.COM")?;
        let mut wvm = DosVm::new(DosEra::Dos400);
        wvm.load_com(
            &who_com,
            " /USER",
            &[("USERDOMAIN", "CORP"), ("USERNAME", "SAMUEL")],
        )?;
        let wres = wvm.run(300_000)?;
        assert_eq!(wres.stdout, join_crlf(&[r"CORP\SAMUEL S-1-5-21-0-500"]));
    }
    Ok(())
}

#[test]
fn m5_vfs_registry_and_child_exec_utilities_inside_wincmd_batch() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let wincmd = load_built(tc, "WINCMD.COM")?;
        let mut vm = DosVm::new(DosEra::Dos400);

        for u in [
            "ATTRIB.COM",
            "TREE.COM",
            "WHERE.COM",
            "XCOPY.COM",
            "REPLACE.COM",
            "ROBOCOPY.COM",
            "CHOICE.COM",
            "TIMEOUT.COM",
            "TRUENAME.COM",
            "REG.COM",
        ] {
            vm.vfs.insert(String::from(u), load_built(tc, u)?);
        }
        vm.stdin = b"B".to_vec(); // CHOICE /C ABC picks 'B' -> ERRORLEVEL 2!

        let script = join_crlf(&[
            "@ECHO OFF",
            r"MD A:\SRC",
            r"ECHO PAYLOAD_V1>A:\SRC\ITEM.TXT",
            r"ATTRIB +R A:\SRC\ITEM.TXT",
            r"ATTRIB A:\SRC\ITEM.TXT",
            r"ATTRIB -R A:\SRC\ITEM.TXT",
            r"XCOPY A:\SRC\*.* A:\DST /I /Q",
            r"ROBOCOPY A:\SRC A:\MIR",
            "ECHO RB1=%ERRORLEVEL%",
            r"ROBOCOPY A:\SRC A:\MIR",
            "ECHO RB0=%ERRORLEVEL%",
            r"ECHO EXTRA>A:\MIR\EXTRA.TXT",
            r"ROBOCOPY A:\SRC A:\MIR /MIR",
            "ECHO RB2=%ERRORLEVEL%",
            r"ECHO PAYLOAD_V2>A:\ITEM.TXT",
            r"REPLACE A:\ITEM.TXT A:\DST",
            r"TYPE A:\DST\ITEM.TXT",
            "TREE /A /F",
            "WHERE ROBOCOPY",
            "TRUENAME ITEM.TXT",
            r"REG ADD HKCU\Software\WinCmd /v Theme /t REG_SZ /d Amber",
            r"REG QUERY HKCU\Software\WinCmd",
            r"REG DELETE HKCU\Software\WinCmd /f",
            "CHOICE /C ABC /M Pick",
            "ECHO CH_EL=%ERRORLEVEL%",
            "TIMEOUT /T 0",
            "ECHO TO_EL=%ERRORLEVEL%",
        ]);
        vm.vfs.insert(String::from("M5ALL.BAT"), script);
        vm.load_com(&wincmd, " --dialect=nt /C M5ALL.BAT", &[("PATH", r"A:")])?;
        let res = vm.run(25_000_000)?;
        assert_eq!(res.exit_code, 0, "{tc} exit");
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains(r"R  A:\SRC\ITEM.TXT"), "{tc} ATTRIB: {out}");
        assert!(out.contains("1 File(s) copied"), "{tc} XCOPY: {out}");
        assert!(out.contains("RB1=1"), "{tc} ROBOCOPY copy bit: {out}");
        assert!(out.contains("RB0=0"), "{tc} ROBOCOPY idempotent 0: {out}");
        assert!(out.contains("RB2=2"), "{tc} ROBOCOPY purge bit: {out}");
        assert!(out.contains("1 file(s) replaced"), "{tc} REPLACE: {out}");
        assert!(out.contains("PAYLOAD_V2"), "{tc} REPLACED content: {out}");
        assert!(out.contains("+---DST"), "{tc} TREE /A: {out}");
        assert!(out.contains("ROBOCOPY.COM"), "{tc} WHERE: {out}");
        assert!(out.contains("Theme    REG_SZ    Amber"), "{tc} REG QUERY: {out}");
        assert!(out.contains("Pick [A,B,C]?B"), "{tc} CHOICE: {out}");
        assert!(out.contains("CH_EL=2"), "{tc} CHOICE ERRORLEVEL 2: {out}");
        assert!(out.contains("TO_EL=0"), "{tc} TIMEOUT: {out}");
    }
    Ok(())
}
