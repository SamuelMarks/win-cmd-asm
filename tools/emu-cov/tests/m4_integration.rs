//! Exhaustive Milestone M4 integration tests exercising `WINCMD.COM` (`NASM`, `FASM`, `JWasm`)
//! across Phase 14 (`devices`, `pathnorm`, `glob_dp`), Phase 15 (`SETLOCAL`/`ENDLOCAL` + batch
//! auto-unwind, `PUSHD`/`POPD`, 11-tier `SET /A`, dynamic variables, `%PATH%` + `.CMD` +
//! `%NoDefaultCurrentDirectoryInExePath%`), and Phase 16 complete internal built-in commands.

use emu_cov::{DosEra, DosVm};
use std::path::PathBuf;
use wincmd_diag::ToolError;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .map_or_else(|| PathBuf::from("."), PathBuf::from)
}

const fn bs() -> char {
    92 as char
}

fn join_crlf(lines: &[&str]) -> Vec<u8> {
    let mut out = Vec::new();
    for line in lines {
        out.extend_from_slice(line.as_bytes());
        out.extend_from_slice(&[0x0D, 0x0A]);
    }
    out
}

fn load_built(tc: &str, rel: &str) -> Result<Vec<u8>, ToolError> {
    Ok(std::fs::read(
        repo_root().join(format!("build/{tc}/dos16/{rel}")),
    )?)
}

fn built_toolchains() -> Vec<&'static str> {
    let root = repo_root();
    let tcs: Vec<&'static str> = ["nasm", "fasm", "jwasm"]
        .into_iter()
        .filter(|tc| root.join(format!("build/{tc}/dos16/WINCMD.COM")).is_file())
        .collect();
    assert!(!tcs.is_empty(), "at least one DOS16 toolchain must be built");
    tcs
}

#[test]
fn m4_devices_pathnorm_truename_and_dp_glob_across_eras_and_assemblers() -> Result<(), ToolError> {
    let s = bs();
    for tc in built_toolchains() {
        let com = load_built(tc, "WINCMD.COM")?;

        // 1. MS-DOS 1.25 FCB NUL redirection, IF EXIST NUL/CON, TYPE NUL, COPY CON, TRUENAME
        let mut vm125 = DosVm::new(DosEra::Dos125);
        vm125.stdin = b"CON125_BODY\x1A".to_vec();
        let bat125 = join_crlf(&[
            "@ECHO OFF",
            "ECHO HIDDEN > A:NUL.TXT",
            "IF EXIST NUL ECHO DEV125_NUL",
            "IF EXIST CON: ECHO DEV125_CON",
            "TYPE NUL",
            "COPY CON C125.TXT > NUL",
            "TYPE C125.TXT",
            "ECHO.",
            "TRUENAME c125.txt",
        ]);
        vm125.vfs.insert(String::from("M4_125.BAT"), bat125);
        vm125.load_com(&com, " --dialect=nt /C M4_125.BAT", &[])?;
        let r125 = vm125.run(4_000_000)?;
        assert_eq!(r125.exit_code, 0, "{tc} 1.25 exit");
        let o125 = String::from_utf8_lossy(&r125.stdout);
        assert!(!o125.contains("HIDDEN"), "{tc} 1.25 leaked NUL: {o125}");
        assert!(!vm125.vfs.contains_key("NUL.TXT"), "{tc} created NUL.TXT");
        assert!(o125.contains("DEV125_NUL"), "{tc} 1.25 out: {o125}");
        assert!(o125.contains("DEV125_CON"), "{tc} 1.25 out: {o125}");
        assert!(o125.contains("CON125_BODY"), "{tc} 1.25 out: {o125}");
        assert!(o125.contains("A:C125.TXT"), "{tc} 1.25 truename: {o125}");

        // 2. MS-DOS 2.00 & 7.10 pathnorm (UNC, \?\, .., trailing dots) + O(N*M) DP globbing!
        for era in [DosEra::Dos200, DosEra::Dos710] {
            let mut vm = DosVm::new(era);
            for name in ["AAAB.TXT", "AXAYAZB.TXT", "AAAAAAC.TXT", "OTHER.DOC"] {
                vm.vfs.insert(String::from(name), b"x".to_vec());
            }
            let c_nul = format!("C:{s}MISSING{s}NUL.TXT");
            let unc_p = format!("{s}{s}SRV{s}SHARE{s}sub{s}..{s}leaf.");
            let verb_p = format!("{s}{s}?{s}C:{s}KeepDot.");
            let script = [
                String::from("@ECHO OFF"),
                format!("ECHO DROPME > {c_nul}"),
                format!("IF EXIST {c_nul} ECHO DEV_DIR_NUL"),
                String::from("IF EXIST COM1 ECHO DEV_COM1"),
                String::from("IF EXIST LPT2: ECHO DEV_LPT2"),
                String::from("IF EXIST CLOCK$ ECHO DEV_CLK"),
                String::from("IF EXIST CONIN$ ECHO DEV_CIN"),
                String::from("IF EXIST CONOUT$ ECHO DEV_COUT"),
                format!("TYPE {c_nul}"),
                String::from("DIR /B *A*A*B*.TXT"),
                format!("TRUENAME {unc_p}"),
                format!("TRUENAME {verb_p}"),
            ];
            let script_refs: Vec<&str> = script.iter().map(String::as_str).collect();
            vm.vfs
                .insert(String::from("M4VFS.BAT"), join_crlf(&script_refs));
            vm.load_com(&com, " --dialect=nt /C M4VFS.BAT", &[])?;
            let res = vm.run(6_000_000)?;
            assert_eq!(res.exit_code, 0, "{tc} {era:?} exit");
            let out = String::from_utf8_lossy(&res.stdout);
            assert!(!out.contains("DROPME"), "{tc} {era:?} NUL leaked: {out}");
            assert!(out.contains("DEV_DIR_NUL"), "{tc} {era:?} out: {out}");
            assert!(out.contains("DEV_COM1"), "{tc} {era:?} out: {out}");
            assert!(out.contains("DEV_LPT2"), "{tc} {era:?} out: {out}");
            assert!(out.contains("DEV_CLK"), "{tc} {era:?} out: {out}");
            assert!(out.contains("DEV_CIN"), "{tc} {era:?} out: {out}");
            assert!(out.contains("DEV_COUT"), "{tc} {era:?} out: {out}");
            assert!(out.contains("AAAB.TXT"), "{tc} {era:?} glob: {out}");
            assert!(out.contains("AXAYAZB.TXT"), "{tc} {era:?} glob: {out}");
            assert!(
                !out.contains("AAAAAAC.TXT"),
                "{tc} {era:?} glob false pos: {out}"
            );
            if era == DosEra::Dos200 {
                let exp_unc = format!("{s}{s}SRV{s}SHARE{s}LEAF");
                let exp_verb = format!("{s}{s}?{s}C:{s}KeepDot.");
                assert!(out.contains(&exp_unc), "{tc} unc norm: {out}");
                assert!(out.contains(&exp_verb), "{tc} verbatim norm: {out}");
            }
        }
    }
    Ok(())
}

#[test]
fn m4_setlocal_auto_unwind_pushd_dynamic_vars_and_path_search() -> Result<(), ToolError> {
    let s = bs();
    for tc in built_toolchains() {
        let com = load_built(tc, "WINCMD.COM")?;
        let mut vm = DosVm::new(DosEra::Dos400);

        // Place an external .CMD script and .COM binary inside A:\BIN (not in CWD once we CD!)
        let bin_tool_com = format!("A:{s}BIN{s}BINTEST.COM");
        // Tiny .COM that exits with ERRORLEVEL 65 ('A' = 0x41)!
        vm.vfs
            .insert(bin_tool_com, vec![0xB8, 0x41, 0x4C, 0xCD, 0x21]);
        let bin_cmd_script = format!("A:{s}BIN{s}EXTCMD.CMD");
        vm.vfs
            .insert(bin_cmd_script, join_crlf(&["@ECHO EXT_CMD_OK_%1"]));

        // Child .BAT that opens SETLOCAL, mutates OUTER + CWD + DelayedExpansion, and exits WITHOUT ENDLOCAL!
        vm.vfs.insert(
            String::from("LEAKSL.BAT"),
            join_crlf(&[
                "SETLOCAL EnableDelayedExpansion",
                "SET OUTER=INNER_MUTATED",
                "SET GHOST=INSIDE_ONLY",
                "ECHO IN_SL=!OUTER!",
                "EXIT /B 0",
            ]),
        );

        let main_lines = [
            String::from("@ECHO OFF"),
            format!("MD A:{s}BIN"),
            format!("MD A:{s}WORK{s}DEEP"),
            String::from("SET OUTER=ORIGINAL"),
            String::from("CALL LEAKSL.BAT"),
            String::from("ECHO AFTER_SL=%OUTER%_%GHOST%"),
            format!("PUSHD A:{s}WORK{s}DEEP"),
            String::from("ECHO IN_PD=%CD%"),
            String::from("POPD"),
            String::from("ECHO AFTER_PD=%CD%"),
            format!("PATH A:{s}BIN"),
            String::from("SET NoDefaultCurrentDirectoryInExePath=1"),
            String::from("BINTEST"),
            String::from("ECHO EC_HEX=%=ExitCode% EC_ASC=%=ExitCodeAscii%"),
            String::from("CALL EXTCMD HELLO"),
            String::from("ECHO DYN=%DATE%_%HIGHESTNUMANODENUMBER%_%CMDEXTVERSION%"),
        ];
        let line_refs: Vec<&str> = main_lines.iter().map(String::as_str).collect();
        vm.vfs
            .insert(String::from("M4RT.BAT"), join_crlf(&line_refs));
        vm.load_com(&com, " --dialect=nt /C M4RT.BAT", &[])?;
        let res = vm.run(10_000_000)?;
        assert_eq!(res.exit_code, 0, "{tc} exit");
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains("IN_SL=INNER_MUTATED"), "{tc} out: {out}");
        assert!(
            out.contains("AFTER_SL=ORIGINAL_"),
            "{tc} unwind failed: {out}"
        );
        assert!(
            out.contains(&format!("IN_PD=A:{s}WORK{s}DEEP")),
            "{tc} pushd failed: {out}"
        );
        assert!(
            out.contains(&format!("AFTER_PD=A:{s}")),
            "{tc} popd failed: {out}"
        );
        assert!(
            out.contains("EC_HEX=00000041 EC_ASC=A"),
            "{tc} ExitCode dynamic vars: {out}"
        );
        assert!(out.contains("EXT_CMD_OK_HELLO"), "{tc} %PATH% .CMD: {out}");
        assert!(out.contains("_0_2"), "{tc} dynamic vars: {out}");
    }
    Ok(())
}

#[test]
fn m4_eleven_tier_set_a_and_complete_phase16_builtins() -> Result<(), ToolError> {
    for tc in built_toolchains() {
        let com = load_built(tc, "WINCMD.COM")?;
        let mut vm = DosVm::new(DosEra::Dos710);
        let mut sin = b"COPYCON_710".to_vec();
        sin.extend_from_slice(&[0x1A, b'K']);
        vm.stdin = sin; // COPY CON body + 0x1A + 1 key for PAUSE

        let script = join_crlf(&[
            "@ECHO OFF",
            // 1. 11-tier SET /A (hex, octal, char literal, shifts & 31, bitwise, compound, comma, INT32_MIN/-1)
            r#"SET /A "R1=(0x10 + 010 + 'A') * 2, R2=1<<33, R3=(0xFF & 0x0F) ^ 0x05 | 0x20""#,
            r#"SET /A "R1+=10, R3>>=1""#,
            "ECHO SA1=%R1%_%R2%_%R3%",
            "SET /A MIN=-2147483648 / -1",
            "ECHO MIN=%MIN%",
            "SET /A BAD8=08",
            "ECHO BAD8_EL=%ERRORLEVEL%",
            "SET /A DIV0=10/0",
            "ECHO DIV0_EL=%ERRORLEVEL%",
            // 2. COPY CON + COPY concatenation + MOVE + SET prefix filter
            "COPY CON P1.TXT > NUL",
            "ECHO _PART2>P2.TXT",
            "COPY /B P1.TXT + P2.TXT JOIN.TXT > NUL",
            "MOVE /Y JOIN.TXT FINAL.TXT > NUL",
            "TYPE FINAL.TXT",
            "SET PRE_ONE=111",
            "SET PRE_TWO=222",
            "SET PRE_",
            // 3. ASSOC, FTYPE, DPATH, PROMPT, PAUSE, BREAK, VERIFY, DATE /T, TIME /T, COLOR, KEYS, MODE, CMD, LH, MKLINK
            "ASSOC .foo=FooType > NUL",
            "ASSOC .foo",
            "FTYPE FooType=foo.exe %%1 > NUL",
            "FTYPE FooType",
            r"DPATH A:\DATA",
            "DPATH",
            "PATH ;",
            "PATH",
            "BREAK ON",
            "BREAK",
            "VERIFY OFF",
            "VERIFY",
            "DATE /T",
            "TIME /T",
            "COLOR 1F",
            "ECHO COL_OK=%ERRORLEVEL%",
            "COLOR 22",
            "ECHO COL_SAME=%ERRORLEVEL%",
            "KEYS",
            "CTTY CON",
            "DOSKEY /HISTORY",
            "MODE",
            "PAUSE > NUL",
            "LH ECHO LH_OK",
            "START /B ECHO START_OK",
            r#"CMD /V:ON /Q /C "SET SUBV=SUB_OK& ECHO !SUBV!""#,
            "ECHO LEAK_CHK=[%SUBV%]",
            "MKLINK LNK TARGET > NUL",
            "ECHO MKL_EL=%ERRORLEVEL%",
        ]);
        vm.vfs.insert(String::from("M4ALL.BAT"), script);
        vm.load_com(&com, " --dialect=nt /C M4ALL.BAT", &[])?;
        let res = vm.run(14_000_000)?;
        let out = String::from_utf8_lossy(&res.stdout);
        // (16 + 8 + 65)*2 + 10 = 188; 1<<(33&31)=2; ((15^5)|32)>>1 = (10|32)>>1 = 42>>1 = 21
        assert!(out.contains("SA1=188_2_21"), "{tc} SET /A: {out}");
        assert!(out.contains("MIN=-2147483648"), "{tc} INT32_MIN/-1: {out}");
        assert!(out.contains("BAD8_EL=1"), "{tc} octal 08: {out}");
        assert!(out.contains("DIV0_EL=1"), "{tc} div0: {out}");
        assert!(out.contains("COPYCON_710_PART2"), "{tc} copy+/move: {out}");
        assert!(out.contains("PRE_ONE=111"), "{tc} set prefix: {out}");
        assert!(out.contains("PRE_TWO=222"), "{tc} set prefix: {out}");
        assert!(out.contains(".foo=FooType"), "{tc} assoc: {out}");
        assert!(out.contains("FooType=foo.exe"), "{tc} ftype: {out}");
        assert!(out.contains(r"DPATH=A:\DATA"), "{tc} dpath: {out}");
        assert!(out.contains("No Path"), "{tc} empty path: {out}");
        assert!(out.contains("BREAK is ON"), "{tc} break: {out}");
        assert!(out.contains("VERIFY is OFF"), "{tc} verify: {out}");
        assert!(out.contains("COL_OK=0"), "{tc} color ok: {out}");
        assert!(out.contains("COL_SAME=1"), "{tc} color fg==bg: {out}");
        assert!(out.contains("KEYS is ON"), "{tc} keys: {out}");
        assert!(out.contains("Columns=80"), "{tc} mode: {out}");
        assert!(out.contains("LH_OK"), "{tc} lh: {out}");
        assert!(out.contains("START_OK"), "{tc} start: {out}");
        assert!(out.contains("SUB_OK"), "{tc} cmd /c: {out}");
        assert!(
            out.contains("LEAK_CHK=[]"),
            "{tc} cmd subshell isolation: {out}"
        );
        assert!(out.contains("MKL_EL=1"), "{tc} mklink: {out}");
    }
    Ok(())
}
