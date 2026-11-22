//! Exhaustive Milestone M3 integration tests exercising `WINCMD.COM` (`NASM`, `FASM`, `JWasm`)
//! across Phase 12 (stateless byte-offset reader, UTF-8/UTF-16LE/UTF-16BE BOMs, `0x1A` EOF,
//! `CRLF`/`LF`/`CR`, self-modifying batch byte offsets, `CHCP 65001` & DBCS `CP936`, `^` folding)
//! and Phase 13 (Phase 1 `%` / `%~` modifiers / `%VAR:...%`, Phase 2 context-sensitive lexer &
//! Arena AST parser, Phase 3 `FOR` loop variables, and Phase 4 `!VAR!` delayed expansion).

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

const fn bs() -> char {
    '\x5C'
}

const fn crlf() -> &'static str {
    "\x0D\x0A"
}

fn join_crlf(lines: &[&str]) -> Vec<u8> {
    let mut out = Vec::new();
    for line in lines {
        out.extend_from_slice(line.as_bytes());
        out.extend_from_slice(&[0x0D, 0x0A]);
    }
    out
}

fn encode_utf16_bom_lines(lines: &[&str], big_endian: bool) -> Vec<u8> {
    let mut out = Vec::new();
    if big_endian {
        out.extend_from_slice(&[0xFE, 0xFF]);
    } else {
        out.extend_from_slice(&[0xFF, 0xFE]);
    }
    for line in lines {
        for u in line.encode_utf16().chain([0x000D_u16, 0x000A_u16]) {
            let pair = if big_endian {
                u.to_be_bytes()
            } else {
                u.to_le_bytes()
            };
            out.extend_from_slice(&pair);
        }
    }
    out
}

#[test]
fn m3_phase12_bom_utf8_utf16_ctrlz_line_endings_and_caret_folding() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;

        // 1. UTF-8 BOM + mixed LF (0x0A) / CR (0x0D) / CRLF + 0x1A immediate EOF + UTF-8 bytes
        let mut utf8_bat = vec![0xEF, 0xBB, 0xBF];
        utf8_bat.extend_from_slice(b"@ECHO OFF");
        utf8_bat.push(0x0A);
        utf8_bat.extend_from_slice(b"ECHO UTF8_BOM_OK");
        utf8_bat.push(0x0D);
        utf8_bat.extend_from_slice(&[b'E', b'C', b'H', b'O', b' ', b'C', b'A', b'F', b'E', b'_', 0xC3, 0xA9, 0x0D, 0x0A]);
        utf8_bat.push(0x1A);
        utf8_bat.extend_from_slice(b"ECHO AFTER_CTRLZ_MUST_NOT_RUN");
        utf8_bat.extend_from_slice(&[0x0D, 0x0A]);

        let mut vm = DosVm::new(DosEra::Dos710);
        vm.vfs.insert(String::from("U8.BAT"), utf8_bat);
        vm.load_com(&com, " --dialect=nt /C U8.BAT", &[])?;
        let res = vm.run(2_500_000)?;
        assert_eq!(res.exit_code, 0);
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains("UTF8_BOM_OK"), "tc={tc}: {out}");
        assert!(
            res.stdout.windows(7).any(|w| w == [b'C', b'A', b'F', b'E', b'_', 0xC3, 0xA9]),
            "tc={tc}: {out}"
        );
        assert!(!out.contains("AFTER_CTRLZ"), "tc={tc}: {out}");

        // 2. UTF-16LE BOM and UTF-16BE BOM batch scripts (2 bytes per WCHAR offset tracking!)
        let utf16_little =
            encode_utf16_bom_lines(&["@ECHO OFF", "ECHO U16LE_FIRST", "ECHO U16LE_SECOND"], false);
        let utf16_big =
            encode_utf16_bom_lines(&["@ECHO OFF", "ECHO U16BE_FIRST", "ECHO U16BE_SECOND"], true);

        let mut vm_le = DosVm::new(DosEra::Dos710);
        vm_le.vfs.insert(String::from("ULE.BAT"), utf16_little);
        vm_le.vfs.insert(String::from("UBE.BAT"), utf16_big);
        vm_le.load_com(&com, " --dialect=nt /C CALL ULE.BAT & CALL UBE.BAT", &[])?;
        let res_u16 = vm_le.run(4_000_000)?;
        let out_u16 = String::from_utf8_lossy(&res_u16.stdout);
        assert!(out_u16.contains("U16LE_FIRST"), "tc={tc}: {out_u16}");
        assert!(out_u16.contains("U16LE_SECOND"), "tc={tc}: {out_u16}");
        assert!(out_u16.contains("U16BE_FIRST"), "tc={tc}: {out_u16}");
        assert!(out_u16.contains("U16BE_SECOND"), "tc={tc}: {out_u16}");

        // 3. Caret '^' + newline folding in NT_CMD vs literal '^' in DOS400, plus self-modifying .BAT!
        let mut vm_nt = DosVm::new(DosEra::Dos400);
        vm_nt.vfs.insert(
            String::from("FOLD.BAT"),
            join_crlf(&["@ECHO OFF", "ECHO FOLDED_^", "LINE_OK"]),
        );
        vm_nt.load_com(&com, " --dialect=nt /C FOLD.BAT", &[])?;
        let res_fold = vm_nt.run(2_500_000)?;
        assert_eq!(
            String::from_utf8_lossy(&res_fold.stdout),
            format!("FOLDED_LINE_OK{}", crlf()),
            "tc={tc}"
        );

        let mut vm_dos = DosVm::new(DosEra::Dos400);
        vm_dos.vfs.insert(
            String::from("NOFOLD.BAT"),
            join_crlf(&["@ECHO OFF", "ECHO LITERAL_^"]),
        );
        vm_dos.load_com(&com, " --dialect=dos400 /C NOFOLD.BAT", &[])?;
        let res_nf = vm_dos.run(2_500_000)?;
        assert_eq!(
            String::from_utf8_lossy(&res_nf.stdout),
            format!("LITERAL_^{}", crlf()),
            "tc={tc}"
        );

        // Self-modifying batch script rewriting its own second line while executing line 1!
        let mut vm_self = DosVm::new(DosEra::Dos400);
        vm_self.vfs.insert(
            String::from("NEXT.TXT"),
            join_crlf(&["@ECHO OFF               ", "ECHO MODIFIED_ON_THE_FLY"]),
        );
        vm_self.vfs.insert(
            String::from("SELF.BAT"),
            join_crlf(&["@COPY NEXT.TXT SELF.BAT >NUL", "ECHO ORIGINAL_NEVER_RUNS"]),
        );
        vm_self.load_com(&com, " /C SELF.BAT", &[])?;
        let res_self = vm_self.run(3_000_000)?;
        let out_self = String::from_utf8_lossy(&res_self.stdout);
        assert!(out_self.contains("MODIFIED_ON_THE_FLY"), "tc={tc}: {out_self}");
        assert!(!out_self.contains("ORIGINAL_NEVER_RUNS"), "tc={tc}: {out_self}");
    }
    Ok(())
}

#[test]
fn m3_phase13_modifiers_var_ops_and_shift() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;

        let mut vm = DosVm::new(DosEra::Dos710);
        let sub_dir = format!("A:{}TOOLS", bs());
        vm.dirs.insert(sub_dir.clone());
        let tool_path = format!("{sub_dir}{}BUILD.CMD", bs());
        vm.vfs.insert(tool_path, join_crlf(&["@ECHO OFF"]));
        vm.vfs.insert(String::from("SAMPLE.TXT"), b"Hello7B".to_vec());

        vm.vfs.insert(
            String::from("MODS.BAT"),
            join_crlf(&[
                "@ECHO OFF",
                "ECHO STRIP=%~1",
                "ECHO DPNX=%~dpnx1",
                "ECHO ATTR_SZ=%~a1/%~z1",
                "ECHO TIME=[%~t1]",
                "ECHO FOUND=%~$MYPATH:2",
                "ECHO STAR=[%*]",
                "SHIFT /1",
                "ECHO AFTER_SHIFT=%1/%2/%3",
                "SHIFT /2",
                "ECHO AFTER_SHIFT2=%1/%2",
                "SET STR=abcdef_foo_FOO_end",
                "ECHO SUB1=%STR:~2,4%",
                "ECHO SUB2=%STR:~-3%",
                "ECHO SUB3=%STR:~0,-12%",
                "ECHO REP1=%STR:foo=BAR%",
                "ECHO REP2=%STR:*foo=HEAD%",
                "ECHO REP3=%STR:*nomatch=HEAD%",
                "ECHO PCT=100%%",
            ]),
        );

        let env_mypath = format!("A:{}TOOLS", bs());
        vm.load_com(
            &com,
            r#" --dialect=nt /V:ON /C MODS.BAT "SAMPLE.TXT" BUILD.CMD THIRD FOURTH"#,
            &[("MYPATH", &env_mypath)],
        )?;
        let res = vm.run(5_000_000)?;
        assert_eq!(res.exit_code, 0);
        let out = String::from_utf8_lossy(&res.stdout);
        assert!(out.contains(&format!("STRIP=SAMPLE.TXT{}", crlf())), "tc={tc}: {out}");
        let exp_dpnx = format!("DPNX=A:{}SAMPLE.TXT{}", bs(), crlf());
        assert!(out.contains(&exp_dpnx), "tc={tc}: {out}");
        assert!(out.contains(&format!("/7{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains("TIME=[") && out.contains('/'), "tc={tc}: {out}");
        let exp_found = format!("FOUND=A:{}TOOLS{}BUILD.CMD{}", bs(), bs(), crlf());
        assert!(out.contains(&exp_found), "tc={tc}: {out}");
        assert!(
            out.contains(&format!(r#"STAR=["SAMPLE.TXT" BUILD.CMD THIRD FOURTH]{}"#, crlf())),
            "tc={tc}: {out}"
        );
        assert!(
            out.contains(&format!("AFTER_SHIFT=BUILD.CMD/THIRD/FOURTH{}", crlf())),
            "tc={tc}: {out}"
        );
        assert!(
            out.contains(&format!("AFTER_SHIFT2=BUILD.CMD/FOURTH{}", crlf())),
            "tc={tc}: {out}"
        );
        assert!(out.contains(&format!("SUB1=cdef{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("SUB2=end{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("SUB3=abcdef{}", crlf())), "tc={tc}: {out}");
        assert!(
            out.contains(&format!("REP1=abcdef_BAR_BAR_end{}", crlf())),
            "tc={tc}: {out}"
        );
        assert!(out.contains(&format!("REP2=HEAD_FOO_end{}", crlf())), "tc={tc}: {out}");
        assert!(
            out.contains(&format!("REP3=abcdef_foo_FOO_end{}", crlf())),
            "tc={tc}: {out}"
        );
        assert!(out.contains(&format!("PCT=100%{}", crlf())), "tc={tc}: {out}");
    }
    Ok(())
}

#[test]
fn m3_phase13_dbcs_and_delayed_expansion() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;

        // Delayed !VAR! expansion, nested !ARR_%%I!, ^! escaping, and CP936 DBCS trail-byte safety!
        let mut dvm = DosVm::new(DosEra::Dos400);
        let mut dbcs_script = join_crlf(&["@ECHO OFF", "CHCP 936 >NUL"]);
        dbcs_script.extend_from_slice(b"ECHO DBCS=");
        dbcs_script.extend_from_slice(&[0x81, b'%', 0x81, b'&', 0x0D, 0x0A]);
        dbcs_script.extend_from_slice(&join_crlf(&[
            "SET ARR_1=ALPHA",
            "SET ARR_2=BETA",
            "SET COUNT=0",
            "FOR /L %%I IN (1,1,2) DO (SET /A COUNT=COUNT+10 >NUL & ECHO ITEM=%%I:!ARR_%%I!:!COUNT!)",
            "ECHO ESC=^!COUNT^! ^& LITERAL_AMP",
        ]));
        dvm.vfs.insert(String::from("DELAY.BAT"), dbcs_script);
        dvm.load_com(&com, " --dialect=nt /V:ON /C DELAY.BAT", &[])?;
        let dres = dvm.run(6_000_000)?;
        assert_eq!(dres.exit_code, 0);
        assert!(
            dres.stdout
                .windows(9)
                .any(|w| w == [b'D', b'B', b'C', b'S', b'=', 0x81, b'%', 0x81, b'&']),
            "DBCS trail bytes '%' and '&' must remain intact: {:?}",
            dres.stdout
        );
        let dout = String::from_utf8_lossy(&dres.stdout);
        assert!(dout.contains(&format!("ITEM=1:ALPHA:10{}", crlf())), "tc={tc}: {dout}");
        assert!(dout.contains(&format!("ITEM=2:BETA:20{}", crlf())), "tc={tc}: {dout}");
        assert!(
            dout.contains(&format!("ESC=!COUNT! & LITERAL_AMP{}", crlf())),
            "tc={tc}: {dout}"
        );
    }
    Ok(())
}

#[test]
fn m3_phase13_ast_blocks_if_all_ops_for_d_r_f_pipes_and_redirs() -> Result<(), ToolError> {
    for tc in ["nasm", "fasm", "jwasm"] {
        let com = load_built(tc, "WINCMD.COM")?;

        let mut vm = DosVm::new(DosEra::Dos710);
        let sub_a = format!("A:{}SUBA", bs());
        let sub_b = format!("A:{}SUBB", bs());
        vm.dirs.insert(sub_a.clone());
        vm.dirs.insert(sub_b);
        vm.vfs
            .insert(format!("{sub_a}{}DEEP.LOG", bs()), b"deep".to_vec());
        vm.vfs.insert(
            String::from("DATA.CSV"),
            join_crlf(&[
                "skipped_header_line",
                ";comment_line_ignored",
                "first,second,rest,of,row",
                "uno,dos,tres",
            ]),
        );

        let for_r_line = format!("FOR /R A:{} %%R IN (*.LOG) DO ECHO REC=%%~nxR", bs());
        vm.vfs.insert(
            String::from("AST.BAT"),
            join_crlf(&[
                "@ECHO OFF",
                ":: Double-colon label comment",
                "REM > MUST_NOT_EXIST.TXT | ECHO NO_REM_PIPE",
                "ECHO(PAREN_DELIM",
                "ECHO.",
                "SET V=hello",
                "(",
                r#"  IF /I "%V%"=="HELLO" ("#,
                "    ECHO CASE_INSENS_OK",
                "  ) ELSE (",
                "    ECHO WRONG_BRANCH",
                "  )",
                ")",
                "IF -5 LSS 2 IF 10 GEQ 10 IF DEFINED V IF CMDEXTVERSION 2 ECHO EXT_IF_OK",
                "FOR /D %%D IN (SUB*) DO ECHO DIR=%%D",
                &for_r_line,
                r#"FOR /F "eol=; skip=1 tokens=1,2* delims=," %%A IN (DATA.CSV) DO ECHO CSV=[%%A][%%B][%%C]"#,
                r#"FOR /F "delims=: tokens=1,2 usebackq" %%X IN ('left:right') DO ECHO STR=[%%X][%%Y]"#,
                r#"FOR /F "usebackq" %%Z IN (`ECHO CMD_CAP_OK`) DO ECHO CAP=%%Z"#,
                ">LEAD.TXT ECHO LEADING_REDIR_OK",
                "TYPE LEAD.TXT | MISSING_CMD_XYZ 2>NUL || ECHO PIPE_OR_FALLBACK",
            ]),
        );

        vm.load_com(&com, " --dialect=nt /C AST.BAT", &[])?;
        let res = vm.run(12_000_000)?;
        let out = String::from_utf8_lossy(&res.stdout);
        let root_no_exist = format!("A:{}MUST_NOT_EXIST.TXT", bs());
        assert!(
            !vm.vfs.contains_key("MUST_NOT_EXIST.TXT") && !vm.vfs.contains_key(&root_no_exist),
            "REM must never create redirection target"
        );
        assert!(out.contains(&format!("PAREN_DELIM{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("CASE_INSENS_OK{}", crlf())), "tc={tc}: {out}");
        assert!(!out.contains("WRONG_BRANCH"), "tc={tc}: {out}");
        assert!(out.contains(&format!("EXT_IF_OK{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("DIR=SUBA{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("DIR=SUBB{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("REC=DEEP.LOG{}", crlf())), "tc={tc}: {out}");
        assert!(
            out.contains(&format!("CSV=[first][second][rest,of,row]{}", crlf())),
            "tc={tc}: {out}"
        );
        assert!(
            out.contains(&format!("CSV=[uno][dos][tres]{}", crlf())),
            "tc={tc}: {out}"
        );
        assert!(out.contains(&format!("STR=[left][right]{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("CAP=CMD_CAP_OK{}", crlf())), "tc={tc}: {out}");
        assert!(out.contains(&format!("PIPE_OR_FALLBACK{}", crlf())), "tc={tc}: {out}");
    }
    Ok(())
}
