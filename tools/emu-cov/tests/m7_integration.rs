//! Exhaustive Milestone M7 & M8 (`IA-32`) integration tests verifying CRT-free `WINCMD32.EXE`
//! (`PE32` `Subsystem 3.10`, `/SAFESEH`, `DYNAMIC_BASE`, `NX_COMPAT`), strict Windows NT 3.1 +
//! Windows 95 `KERNEL32.DLL` static IAT compliance, dual `*W` (NT 3.1..11) / `*A` (Win95/98/Me)
//! HAL execution, concurrent `CreatePipe` pipelines, real `ADVAPI32` Registry `ASSOC`/`FTYPE`,
//! `MKLINK` (`/H`, `/J`) + reparse-safe `RD /S /Q`, UTF-16LE batch execution, and embedded
//! `vm8086` `.COM` execution across `NASM`, `FASM`, and `JWasm`.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::process::Command;
use wincmd_diag::{SourceSpan, ToolError};

#[derive(Clone, Copy)]
enum Win32Runner {
    Native,
    Wine,
}

fn bslash() -> char {
    char::from(92_u8)
}

fn crlf_str() -> String {
    format!("{}{}", char::from(13_u8), char::from(10_u8))
}

fn lf_str() -> String {
    String::from(char::from(10_u8))
}

fn win_join(base: &str, leaf: &str) -> String {
    format!("{base}{}{leaf}", bslash())
}

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .map_or_else(|| PathBuf::from("."), PathBuf::from)
}

fn load_w32(tc: &str) -> Result<Vec<u8>, ToolError> {
    Ok(std::fs::read(
        repo_root().join(format!("build/{tc}/x86_32/WINCMD32.EXE")),
    )?)
}

fn built_toolchains() -> Vec<&'static str> {
    let root = repo_root();
    let tcs: Vec<&'static str> = ["nasm", "fasm", "jwasm"]
        .into_iter()
        .filter(|tc| root.join(format!("build/{tc}/x86_32/WINCMD32.EXE")).is_file())
        .collect();
    assert!(!tcs.is_empty(), "at least one IA-32 toolchain must be built");
    tcs
}

fn rva_to_file_offset(pe: &[u8], lfanew: usize, rva: u32) -> Option<usize> {
    let num_secs = usize::from(u16::from_le_bytes([
        pe.get(lfanew + 6).copied()?,
        pe.get(lfanew + 7).copied()?,
    ]));
    let opt_sz = usize::from(u16::from_le_bytes([
        pe.get(lfanew + 20).copied()?,
        pe.get(lfanew + 21).copied()?,
    ]));
    let sec_base = lfanew + 24 + opt_sz;
    for s in 0..num_secs {
        let off = sec_base + s * 40;
        let virt_sz = u32::from_le_bytes([
            *pe.get(off + 8)?,
            *pe.get(off + 9)?,
            *pe.get(off + 10)?,
            *pe.get(off + 11)?,
        ]);
        let virt_addr = u32::from_le_bytes([
            *pe.get(off + 12)?,
            *pe.get(off + 13)?,
            *pe.get(off + 14)?,
            *pe.get(off + 15)?,
        ]);
        let raw_sz = u32::from_le_bytes([
            *pe.get(off + 16)?,
            *pe.get(off + 17)?,
            *pe.get(off + 18)?,
            *pe.get(off + 19)?,
        ]);
        let raw_ptr = u32::from_le_bytes([
            *pe.get(off + 20)?,
            *pe.get(off + 21)?,
            *pe.get(off + 22)?,
            *pe.get(off + 23)?,
        ]);
        let span = virt_sz.max(raw_sz);
        if rva >= virt_addr && rva < virt_addr.saturating_add(span) {
            let delta = rva - virt_addr;
            if delta < raw_sz {
                return usize::try_from(raw_ptr.saturating_add(delta)).ok();
            }
        }
    }
    None
}

fn read_cstr(pe: &[u8], mut off: usize) -> String {
    let mut out = Vec::new();
    while let Some(&b) = pe.get(off) {
        if b == 0 {
            break;
        }
        out.push(b);
        off += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn audited_nt31_win95_k32_exports() -> BTreeSet<String> {
    let def_path = repo_root().join("asm/tc/arm_ms/k32_x86.def");
    let text = std::fs::read_to_string(def_path).unwrap_or_default();
    let mut set = BTreeSet::new();
    for line in text.lines() {
        let t = line.trim();
        if t.is_empty() || t.starts_with("LIBRARY") || t == "EXPORTS" {
            continue;
        }
        let undecorated = t.split('@').next().unwrap_or(t);
        set.insert(undecorated.to_owned());
    }
    set
}

fn verify_safeseh_load_config(
    tc: &str,
    pe: &[u8],
    lfanew: usize,
    opt: usize,
    image_base: u32,
) -> Result<(), ToolError> {
    let lcfg_rva = u32::from_le_bytes([pe[opt + 176], pe[opt + 177], pe[opt + 178], pe[opt + 179]]);
    assert_ne!(
        lcfg_rva, 0,
        "{tc} /SAFESEH LOAD_CONFIG RVA must be non-zero"
    );
    let lcfg_off = rva_to_file_offset(pe, lfanew, lcfg_rva).ok_or_else(|| ToolError::Syntax {
        span: SourceSpan::new("WINCMD32.EXE", 1, 1),
        message: format!("{tc} invalid LOAD_CONFIG RVA"),
    })?;
    let lcfg_sz = u32::from_le_bytes([
        pe[lcfg_off],
        pe[lcfg_off + 1],
        pe[lcfg_off + 2],
        pe[lcfg_off + 3],
    ]);
    let seh_tbl_va = u32::from_le_bytes([
        pe[lcfg_off + 64],
        pe[lcfg_off + 65],
        pe[lcfg_off + 66],
        pe[lcfg_off + 67],
    ]);
    let seh_cnt = u32::from_le_bytes([
        pe[lcfg_off + 68],
        pe[lcfg_off + 69],
        pe[lcfg_off + 70],
        pe[lcfg_off + 71],
    ]);
    assert_eq!(lcfg_sz, 72, "{tc} 72-byte IMAGE_LOAD_CONFIG_DIRECTORY32");
    assert_eq!(seh_cnt, 1, "{tc} exactly 1 registered /SAFESEH handler");
    assert!(seh_tbl_va > image_base, "{tc} valid SEHandlerTable VA");
    let seh_tbl_off = rva_to_file_offset(pe, lfanew, seh_tbl_va - image_base).ok_or_else(|| {
        ToolError::Syntax {
            span: SourceSpan::new("WINCMD32.EXE", 1, 1),
            message: format!("{tc} invalid SEHandlerTable RVA"),
        }
    })?;
    let handler_rva = u32::from_le_bytes([
        pe[seh_tbl_off],
        pe[seh_tbl_off + 1],
        pe[seh_tbl_off + 2],
        pe[seh_tbl_off + 3],
    ]);
    assert!(
        handler_rva >= 0x1000,
        "{tc} _w32_seh_handler RVA inside .text"
    );
    Ok(())
}

fn verify_nt31_import_table(
    tc: &str,
    pe: &[u8],
    lfanew: usize,
    opt: usize,
    allowed_iat: &BTreeSet<String>,
) -> Result<(), ToolError> {
    let imp_rva = u32::from_le_bytes([pe[opt + 104], pe[opt + 105], pe[opt + 106], pe[opt + 107]]);
    assert_ne!(imp_rva, 0, "{tc} import directory present");
    let mut desc_off =
        rva_to_file_offset(pe, lfanew, imp_rva).ok_or_else(|| ToolError::Syntax {
            span: SourceSpan::new("WINCMD32.EXE", 1, 1),
            message: format!("{tc} invalid import RVA"),
        })?;
    let mut dll_count = 0_usize;
    loop {
        let orig_thunk_rva = u32::from_le_bytes([
            pe[desc_off],
            pe[desc_off + 1],
            pe[desc_off + 2],
            pe[desc_off + 3],
        ]);
        let name_rva = u32::from_le_bytes([
            pe[desc_off + 12],
            pe[desc_off + 13],
            pe[desc_off + 14],
            pe[desc_off + 15],
        ]);
        let first_thunk_rva = u32::from_le_bytes([
            pe[desc_off + 16],
            pe[desc_off + 17],
            pe[desc_off + 18],
            pe[desc_off + 19],
        ]);
        if orig_thunk_rva == 0 && name_rva == 0 && first_thunk_rva == 0 {
            break;
        }
        dll_count += 1;
        let name_off = rva_to_file_offset(pe, lfanew, name_rva).unwrap_or(0);
        let dll_name = read_cstr(pe, name_off);
        assert!(
            dll_name.eq_ignore_ascii_case("KERNEL32.dll"),
            "{tc} forbidden static DLL import `{dll_name}` (must only import KERNEL32.dll)"
        );

        let thunk_rva = if orig_thunk_rva != 0 {
            orig_thunk_rva
        } else {
            first_thunk_rva
        };
        let mut thunk_off = rva_to_file_offset(pe, lfanew, thunk_rva).unwrap_or(0);
        loop {
            let entry = u32::from_le_bytes([
                pe[thunk_off],
                pe[thunk_off + 1],
                pe[thunk_off + 2],
                pe[thunk_off + 3],
            ]);
            if entry == 0 {
                break;
            }
            assert_eq!(
                entry & 0x8000_0000,
                0,
                "{tc} imports by name for NT 3.1/Win95 portability"
            );
            let hint_name_off = rva_to_file_offset(pe, lfanew, entry).unwrap_or(0);
            let fn_name = read_cstr(pe, hint_name_off + 2);
            assert!(
                allowed_iat.contains(&fn_name),
                "{tc} static import `{fn_name}` is outside NT 3.1 RTM + Win95 RTM baseline!"
            );
            thunk_off += 4;
        }
        desc_off += 20;
    }
    assert_eq!(
        dll_count, 1,
        "{tc} exactly 1 static import descriptor (KERNEL32.dll)"
    );
    Ok(())
}

#[test]
fn m7_pe32_headers_safeseh_loadconfig_and_nt31_iat_across_assemblers() -> Result<(), ToolError> {
    let allowed_iat = audited_nt31_win95_k32_exports();
    assert!(
        !allowed_iat.is_empty(),
        "k32_x86.def export baseline must load"
    );

    for tc in built_toolchains() {
        let pe = load_w32(tc)?;
        assert!(
            pe.len() <= 131_072,
            "{tc} WINCMD32.EXE ({}) exceeds 128 KiB budget",
            pe.len()
        );
        assert_eq!(&pe[0..2], b"MZ", "{tc} MZ signature");
        let lfanew = usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]]))
            .unwrap_or(0);
        assert_eq!(
            &pe[lfanew..lfanew + 4],
            &[b'P', b'E', 0, 0],
            "{tc} PE signature"
        );

        let machine = u16::from_le_bytes([pe[lfanew + 4], pe[lfanew + 5]]);
        assert_eq!(machine, 0x014C, "{tc} IMAGE_FILE_MACHINE_I386");

        let opt = lfanew + 24;
        let magic = u16::from_le_bytes([pe[opt], pe[opt + 1]]);
        assert_eq!(magic, 0x010B, "{tc} PE32 optional header magic");

        let image_base =
            u32::from_le_bytes([pe[opt + 28], pe[opt + 29], pe[opt + 30], pe[opt + 31]]);
        let maj_sub = u16::from_le_bytes([pe[opt + 48], pe[opt + 49]]);
        let min_sub = u16::from_le_bytes([pe[opt + 50], pe[opt + 51]]);
        let subsys = u16::from_le_bytes([pe[opt + 68], pe[opt + 69]]);
        let dll_chars = u16::from_le_bytes([pe[opt + 70], pe[opt + 71]]);
        assert_eq!(
            (subsys, maj_sub, min_sub),
            (3, 3, 10),
            "{tc} CUI Subsystem 3.10"
        );
        assert_eq!(
            dll_chars & 0x0140,
            0x0140,
            "{tc} DYNAMIC_BASE (0x0040) | NX_COMPAT (0x0100)"
        );

        verify_safeseh_load_config(tc, &pe, lfanew, opt, image_base)?;
        verify_nt31_import_table(tc, &pe, lfanew, opt, &allowed_iat)?;
    }
    Ok(())
}

fn detect_win32_runner() -> Option<Win32Runner> {
    if cfg!(target_os = "windows") {
        return Some(Win32Runner::Native);
    }
    let probe_exe = repo_root().join("build/nasm/x86_32/WINCMD32.EXE");
    let st = Command::new("wine")
        .env("WINEDEBUG", "-all")
        .arg(&probe_exe)
        .args(["/C", "VER"])
        .output()
        .ok()?;
    if st.status.success() {
        Some(Win32Runner::Wine)
    } else {
        None
    }
}

fn to_win_path(runner: Win32Runner, host_path: &Path) -> String {
    let s = host_path.to_string_lossy();
    let sep = bslash().to_string();
    match runner {
        Win32Runner::Wine => format!("Z:{}", s.replace('/', &sep)),
        Win32Runner::Native => s.into_owned(),
    }
}

fn run_wincmd32(
    runner: Win32Runner,
    exe: &Path,
    args: &[&str],
) -> Result<(i32, String, String), ToolError> {
    let mut cmd = match runner {
        Win32Runner::Wine => {
            let mut c = Command::new("wine");
            c.env("WINEDEBUG", "-all");
            c.arg(exe);
            c
        }
        Win32Runner::Native => Command::new(exe),
    };
    cmd.args(args);
    let out = cmd.output()?;
    let rc = out.status.code().unwrap_or(-1);
    let crlf = crlf_str();
    let lf = lf_str();
    let stdout = String::from_utf8_lossy(&out.stdout).replace(&crlf, &lf);
    let stderr = String::from_utf8_lossy(&out.stderr).replace(&crlf, &lf);
    Ok((rc, stdout, stderr))
}

fn verify_nt_master_script(
    runner: Win32Runner,
    tc: &str,
    exe: &Path,
    tc_dir: &Path,
    win_tc_dir: &str,
    com_hostname: &str,
) -> Result<(), ToolError> {
    let crlf = crlf_str();
    let real_dir = tc_dir.join("real_target");
    std::fs::create_dir_all(&real_dir)?;
    std::fs::write(real_dir.join("keep.txt"), format!("SURVIVED_OK{crlf}"))?;
    std::fs::write(tc_dir.join("p1.txt"), b"PART1+")?;
    std::fs::write(tc_dir.join("p2.txt"), format!("PART2{crlf}"))?;

    let win_real = win_join(win_tc_dir, "real_target");
    let win_keep = win_join(&win_real, "keep.txt");
    let win_hl = win_join(win_tc_dir, "hl.txt");
    let win_junc = win_join(win_tc_dir, "junc");
    let win_p1 = win_join(win_tc_dir, "p1.txt");
    let win_p2 = win_join(win_tc_dir, "p2.txt");
    let win_joined = win_join(win_tc_dir, "joined.txt");
    let win_master = win_join(win_tc_dir, "master.cmd");

    let master_bat = tc_dir.join("master.cmd");
    let script = [
        "@ECHO OFF".to_owned(),
        "SETLOCAL EnableDelayedExpansion".to_owned(),
        "SET /A X=(12+8)*3, Y=-2147483648/-1, X+=5".to_owned(),
        "ECHO MATH=!X!:!Y!".to_owned(),
        r#"ECHO pipe_seed | "%COMSPEC%" /V:ON /C "SET /P A= & ECHO s2_!A!" | "%COMSPEC%" /V:ON /C "SET /P B= & ECHO s3_!B!""#.to_owned(),
        format!("ASSOC .m7{tc}=M7Type_{tc} >NUL"),
        format!(r#"FTYPE M7Type_{tc}=wincmd32.exe "%%1" >NUL"#),
        format!("ASSOC .m7{tc}"),
        format!("FTYPE M7Type_{tc}"),
        format!(r#"MKLINK /H "{win_hl}" "{win_keep}" >NUL"#),
        format!(r#"MKLINK /J "{win_junc}" "{win_real}" >NUL"#),
        format!(r#"DIR "{win_tc_dir}""#),
        format!(r#"RD /S /Q "{win_junc}""#),
        format!(r#"COPY /B "{win_p1}" + "{win_p2}" "{win_joined}" >NUL"#),
        format!(r#"TYPE "{win_joined}""#),
        format!(r#"PUSHD "{win_real}""#),
        "POPD".to_owned(),
        format!("SET COMPUTERNAME=VM86_{tc}"),
        format!(r#""{com_hostname}""#),
        String::new(),
    ]
    .join(&crlf);
    std::fs::write(&master_bat, script.as_bytes())?;
    let (rc, out, err) = run_wincmd32(runner, exe, &["/C", &win_master])?;
    assert_eq!(rc, 0, "{tc} master.cmd rc=0 (stderr={err}, stdout={out})");
    assert!(
        out.contains("MATH=65:-2147483648"),
        "{tc} SET /A + INT32_MIN/-1: {out}"
    );
    assert!(
        out.contains("s3_s2_pipe_seed"),
        "{tc} 3-stage CreatePipe: {out}"
    );
    assert!(
        out.contains(&format!(".m7{tc}=M7Type_{tc}")),
        "{tc} ADVAPI32 ASSOC: {out}"
    );
    assert!(
        out.contains(&format!(r#"M7Type_{tc}=wincmd32.exe "%1""#)),
        "{tc} ADVAPI32 FTYPE: {out}"
    );
    assert!(out.contains("<JUNCTION>"), "{tc} DIR <JUNCTION> tag: {out}");
    assert!(
        real_dir.join("keep.txt").exists(),
        "{tc} RD /S /Q must unlink junction without deleting target files!"
    );
    assert!(
        !tc_dir.join("junc").exists(),
        "{tc} junction directory unlinked"
    );
    assert!(
        out.contains("PART1+PART2"),
        "{tc} COPY + concatenation: {out}"
    );
    assert!(
        out.contains(&format!("VM86_{tc}")),
        "{tc} vm8086 HOSTNAME.COM: {out}"
    );
    Ok(())
}

fn verify_utf16_and_legacy_modes(
    runner: Win32Runner,
    tc: &str,
    exe: &Path,
    tc_dir: &Path,
    win_tc_dir: &str,
) -> Result<(), ToolError> {
    let crlf = crlf_str();
    let u16_bat = tc_dir.join("utf16_test.bat");
    let u16_text = [
        "@ECHO OFF",
        "SETLOCAL EnableDelayedExpansion",
        "SET ACC=0",
        "FOR /L %%N IN (1,2,9) DO SET /A ACC+=%%N",
        r#"FOR /F "tokens=1,2* delims=:" %%A IN ("k1:k2:k3:k4") DO ECHO TOK=%%A_%%B_[%%C]"#,
        "CALL :SUB !ACC!",
        "ECHO SUB_RES=!OUTV!",
        "EXIT /B 37",
        ":SUB",
        "SET /A OUTV=%1*2",
        "GOTO :EOF",
        "",
    ]
    .join(&crlf);
    let mut u16_bytes = vec![0xFF_u8, 0xFE_u8];
    for cu in u16_text.encode_utf16() {
        u16_bytes.extend_from_slice(&cu.to_le_bytes());
    }
    std::fs::write(&u16_bat, &u16_bytes)?;
    let win_u16 = win_join(win_tc_dir, "utf16_test.bat");
    let (utf16_code, utf16_stdout, _) = run_wincmd32(runner, exe, &["/C", &win_u16])?;
    assert_eq!(
        utf16_code, 37,
        "{tc} UTF-16LE EXIT /B 37 (out={utf16_stdout})"
    );
    assert!(
        utf16_stdout.contains("TOK=k1_k2_[k3:k4]"),
        "{tc} FOR /F tokens=1,2*: {utf16_stdout}"
    );
    assert!(
        utf16_stdout.contains("SUB_RES=50"),
        "{tc} FOR /L (1+3+5+7+9=25)*2=50: {utf16_stdout}"
    );

    let w9x_bat = tc_dir.join("w9x.bat");
    let w9x_script = [
        "@ECHO OFF".to_owned(),
        "VER".to_owned(),
        r#"ECHO w9x_pipe | "%COMSPEC%" --win9x-ansi-sim /V:ON /C "SET /P Z= & ECHO A_!Z!""#
            .to_owned(),
        format!(r#"DIR /B "{win_tc_dir}""#),
        String::new(),
    ]
    .join(&crlf);
    std::fs::write(&w9x_bat, w9x_script.as_bytes())?;
    let win_w9x = win_join(win_tc_dir, "w9x.bat");
    let (win9x_code, win9x_stdout, _) =
        run_wincmd32(runner, exe, &["--win9x-ansi-sim", "/C", &win_w9x])?;
    assert_eq!(
        win9x_code, 0,
        "{tc} --win9x-ansi-sim rc=0 (out={win9x_stdout})"
    );
    assert!(
        win9x_stdout.contains("Windows 95/98/Me"),
        "{tc} Win9x VER banner: {win9x_stdout}"
    );
    assert!(
        win9x_stdout.contains("A_w9x_pipe"),
        "{tc} Win9x CreateProcessA pipe: {win9x_stdout}"
    );

    let nt31_bat = tc_dir.join("nt31.bat");
    let nt31_script = [
        "@ECHO OFF".to_owned(),
        "VER".to_owned(),
        format!(r#"DIR "{win_tc_dir}""#),
        String::new(),
    ]
    .join(&crlf);
    std::fs::write(&nt31_bat, nt31_script.as_bytes())?;
    let win_nt31 = win_join(win_tc_dir, "nt31.bat");
    let (nt31_code, nt31_stdout, _) =
        run_wincmd32(runner, exe, &["--nt31-api-cap", "/C", &win_nt31])?;
    assert_eq!(nt31_code, 0, "{tc} --nt31-api-cap rc=0 (out={nt31_stdout})");
    assert!(
        nt31_stdout.contains("3.10.528"),
        "{tc} NT 3.1 Build 528 VER: {nt31_stdout}"
    );
    assert!(
        nt31_stdout.contains("bytes free"),
        "{tc} NT 3.1 GetDiskFreeSpaceW fallback: {nt31_stdout}"
    );
    Ok(())
}

#[test]
fn m7_live_win32_nt_win9x_pipes_registry_mklink_and_vm8086_across_assemblers()
-> Result<(), ToolError> {
    let Some(runner) = detect_win32_runner() else {
        return Ok(());
    };
    let root = repo_root();
    let scratch = std::env::temp_dir().join(format!("wincmd32_m7_test_{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&scratch);
    std::fs::create_dir_all(&scratch)?;
    let win_scratch = to_win_path(runner, &scratch);
    let com_hostname = to_win_path(runner, &root.join("build/nasm/dos16/HOSTNAME.COM"));

    for tc in built_toolchains() {
        let exe = root.join(format!("build/{tc}/x86_32/WINCMD32.EXE"));
        let tc_dir = scratch.join(tc);
        std::fs::create_dir_all(&tc_dir)?;
        let win_tc_dir = win_join(&win_scratch, tc);

        verify_nt_master_script(runner, tc, &exe, &tc_dir, &win_tc_dir, &com_hostname)?;
        verify_utf16_and_legacy_modes(runner, tc, &exe, &tc_dir, &win_tc_dir)?;
    }

    let _ = std::fs::remove_dir_all(&scratch);
    Ok(())
}
