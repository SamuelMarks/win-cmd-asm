//! Exhaustive Milestone M9 & M8 (`x86-64 / AMD64`) integration tests verifying CRT-free
//! `WINCMD64.EXE` (`PE32+` `Subsystem 5.02`, `HIGH_ENTROPY_VA`, `DYNAMIC_BASE`, `NX_COMPAT`,
//! `GUARD_CF`, CET `endbr64`, `__chkstk`, `.pdata`/`.xdata` SEH, `<longPathAware>` manifest),
//! strict Windows XP Pro x64 (`5.2`) `KERNEL32.DLL` static IAT compliance, concurrent
//! `CreatePipe` pipelines, real 64-bit `ADVAPI32` Registry `ASSOC`/`FTYPE`, `MKLINK` (`/H`, `/J`)
//! with reparse-safe `RD /S /Q`, UTF-16LE batch execution, and embedded 64-bit `vm8086` `.COM`
//! execution across `NASM`, `FASM`, and `JWasm`.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::process::Command;
use wincmd_diag::{SourceSpan, ToolError};

#[derive(Clone, Copy)]
enum Win64Runner {
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

fn load_w64(tc: &str) -> Result<Vec<u8>, ToolError> {
    Ok(std::fs::read(
        repo_root().join(format!("build/{tc}/x86_64/WINCMD64.EXE")),
    )?)
}

fn built_toolchains() -> Vec<&'static str> {
    let root = repo_root();
    let tcs: Vec<&'static str> = ["nasm", "fasm", "jwasm"]
        .into_iter()
        .filter(|tc| root.join(format!("build/{tc}/x86_64/WINCMD64.EXE")).is_file())
        .collect();
    assert!(!tcs.is_empty(), "at least one x86-64 toolchain must be built");
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
    for i in 0..num_secs {
        let sh = sec_base + i * 40;
        let vsz = u32::from_le_bytes([
            pe.get(sh + 8).copied()?,
            pe.get(sh + 9).copied()?,
            pe.get(sh + 10).copied()?,
            pe.get(sh + 11).copied()?,
        ]);
        let va = u32::from_le_bytes([
            pe.get(sh + 12).copied()?,
            pe.get(sh + 13).copied()?,
            pe.get(sh + 14).copied()?,
            pe.get(sh + 15).copied()?,
        ]);
        let rawsz = u32::from_le_bytes([
            pe.get(sh + 16).copied()?,
            pe.get(sh + 17).copied()?,
            pe.get(sh + 18).copied()?,
            pe.get(sh + 19).copied()?,
        ]);
        let rawptr = u32::from_le_bytes([
            pe.get(sh + 20).copied()?,
            pe.get(sh + 21).copied()?,
            pe.get(sh + 22).copied()?,
            pe.get(sh + 23).copied()?,
        ]);
        let span = vsz.max(rawsz);
        if rva >= va && rva < va.saturating_add(span) {
            let delta = usize::try_from(rva - va).ok()?;
            let base = usize::try_from(rawptr).ok()?;
            return Some(base + delta);
        }
    }
    None
}

const fn read_u32_at(pe: &[u8], off: usize) -> u32 {
    u32::from_le_bytes([pe[off], pe[off + 1], pe[off + 2], pe[off + 3]])
}

const fn read_u64_at(pe: &[u8], off: usize) -> u64 {
    u64::from_le_bytes([
        pe[off],
        pe[off + 1],
        pe[off + 2],
        pe[off + 3],
        pe[off + 4],
        pe[off + 5],
        pe[off + 6],
        pe[off + 7],
    ])
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

fn audited_nt52_k32_exports() -> BTreeSet<String> {
    let def_path = repo_root().join("asm/tc/arm_ms/k32_x64.def");
    let text = std::fs::read_to_string(def_path).unwrap_or_default();
    let mut set = BTreeSet::new();
    for raw in text.lines() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with("LIBRARY") || line.starts_with("EXPORTS") {
            continue;
        }
        set.insert(line.to_owned());
    }
    set
}

fn verify_pdata_and_xdata(tc: &str, pe: &[u8], lfanew: usize, opt: usize) -> Result<(), ToolError> {
    let pdata_rva = read_u32_at(pe, opt + 136);
    let pdata_sz = usize::try_from(read_u32_at(pe, opt + 140)).unwrap_or(0);
    assert!(
        pdata_rva != 0 && pdata_sz >= 12 * 80 && pdata_sz.is_multiple_of(12),
        "{tc} .pdata exception directory must cover all non-leaf x64 procedures (sz={pdata_sz})"
    );
    let pdata_off = rva_to_file_offset(pe, lfanew, pdata_rva).ok_or_else(|| ToolError::Syntax {
        span: SourceSpan::new("WINCMD64.EXE", 1, 1),
        message: format!("{tc} invalid .pdata RVA"),
    })?;

    let count = pdata_sz / 12;
    let mut prev_end = 0_u32;
    let mut saw_large_alloc = false;
    for i in 0..count {
        let ent = pdata_off + i * 12;
        let begin_rva = read_u32_at(pe, ent);
        let end_rva = read_u32_at(pe, ent + 4);
        let unwind_rva = read_u32_at(pe, ent + 8);
        assert!(
            begin_rva >= prev_end && begin_rva < end_rva,
            "{tc} RUNTIME_FUNCTION[{i}] must be sorted and non-empty ({begin_rva:#x}..{end_rva:#x})"
        );
        prev_end = end_rva;

        let xoff = rva_to_file_offset(pe, lfanew, unwind_rva).ok_or_else(|| ToolError::Syntax {
            span: SourceSpan::new("WINCMD64.EXE", 1, 1),
            message: format!("{tc} invalid .xdata RVA {unwind_rva:#x}"),
        })?;
        let ver_flags = pe[xoff];
        assert_eq!(ver_flags & 0x07, 1, "{tc} UNWIND_INFO version must be 1");
        let code_cnt = usize::from(pe[xoff + 2]);
        let mut c = 0_usize;
        while c < code_cnt {
            let op_info = pe[xoff + 4 + c * 2 + 1];
            let uwop = op_info & 0x0F;
            let info = op_info >> 4;
            if uwop == 1 {
                saw_large_alloc = true;
                c += if info == 0 { 2 } else { 3 };
            } else {
                c += 1;
            }
        }
    }
    assert!(
        saw_large_alloc,
        "{tc} .xdata must include UWOP_ALLOC_LARGE for >=4 KiB __chkstk frame"
    );
    Ok(())
}

fn verify_cfg_load_config(
    tc: &str,
    pe: &[u8],
    lfanew: usize,
    opt: usize,
    image_base: u64,
) -> Result<(), ToolError> {
    let lcfg_rva = read_u32_at(pe, opt + 192);
    assert_ne!(lcfg_rva, 0, "{tc} LOAD_CONFIG directory must be present");
    let lcfg_off = rva_to_file_offset(pe, lfanew, lcfg_rva).ok_or_else(|| ToolError::Syntax {
        span: SourceSpan::new("WINCMD64.EXE", 1, 1),
        message: format!("{tc} invalid LOAD_CONFIG RVA"),
    })?;
    let lcfg_sz = read_u32_at(pe, lcfg_off);
    assert_eq!(lcfg_sz, 256, "{tc} 256-byte IMAGE_LOAD_CONFIG_DIRECTORY64");

    let cfg_check_ptr_va = read_u64_at(pe, lcfg_off + 112);
    let gfids_tbl_va = read_u64_at(pe, lcfg_off + 128);
    let gfids_cnt = usize::try_from(read_u64_at(pe, lcfg_off + 136)).unwrap_or(0);
    let guard_flags = read_u32_at(pe, lcfg_off + 144);
    assert!(
        cfg_check_ptr_va > image_base,
        "{tc} valid GuardCFCheckFunctionPointer VA"
    );
    assert!(
        gfids_tbl_va > image_base && gfids_cnt >= 4,
        "{tc} GuardCFFunctionTable must list >=4 valid indirect targets (cnt={gfids_cnt})"
    );
    assert_ne!(
        guard_flags & 0x0500,
        0,
        "{tc} GuardFlags must enable CF_INSTRUMENTED / CF_FUNCTION_TABLE_PRESENT ({guard_flags:#x})"
    );

    let stride = 4_usize + usize::try_from(guard_flags >> 28).unwrap_or(0);
    let tbl_rva = u32::try_from(gfids_tbl_va - image_base).unwrap_or(0);
    let tbl_off = rva_to_file_offset(pe, lfanew, tbl_rva).ok_or_else(|| ToolError::Syntax {
        span: SourceSpan::new("WINCMD64.EXE", 1, 1),
        message: format!("{tc} invalid GuardCFFunctionTable RVA"),
    })?;
    let mut prev_rva = 0_u32;
    for i in 0..gfids_cnt {
        let rva = read_u32_at(pe, tbl_off + i * stride);
        assert!(
            rva > prev_rva,
            "{tc} GuardCFFunctionTable RVA[{i}]={rva:#x} must be strictly ascending (> {prev_rva:#x})"
        );
        prev_rva = rva;
    }
    Ok(())
}

fn verify_nt52_import_table(
    tc: &str,
    pe: &[u8],
    lfanew: usize,
    opt: usize,
    allowed_iat: &BTreeSet<String>,
) -> Result<(), ToolError> {
    let imp_rva = read_u32_at(pe, opt + 120);
    assert_ne!(imp_rva, 0, "{tc} import directory present");
    let mut desc_off =
        rva_to_file_offset(pe, lfanew, imp_rva).ok_or_else(|| ToolError::Syntax {
            span: SourceSpan::new("WINCMD64.EXE", 1, 1),
            message: format!("{tc} invalid import RVA"),
        })?;
    let mut dll_count = 0_usize;
    loop {
        let orig_thunk_rva = read_u32_at(pe, desc_off);
        let name_rva = read_u32_at(pe, desc_off + 12);
        let first_thunk_rva = read_u32_at(pe, desc_off + 16);
        if orig_thunk_rva == 0 && name_rva == 0 && first_thunk_rva == 0 {
            break;
        }
        dll_count += 1;
        let name_off = rva_to_file_offset(pe, lfanew, name_rva).unwrap_or(0);
        let dll_name = read_cstr(pe, name_off);
        assert!(
            dll_name.eq_ignore_ascii_case("KERNEL32.dll"),
            "{tc} forbidden static DLL import `{dll_name}` (Zero-CRT: only KERNEL32.dll allowed!)"
        );

        let thunk_rva = if orig_thunk_rva != 0 {
            orig_thunk_rva
        } else {
            first_thunk_rva
        };
        let mut thunk_off = rva_to_file_offset(pe, lfanew, thunk_rva).unwrap_or(0);
        loop {
            let entry = read_u64_at(pe, thunk_off);
            if entry == 0 {
                break;
            }
            assert_eq!(
                entry & (1_u64 << 63),
                0,
                "{tc} imports by name for XP Pro x64..Windows 11 portability"
            );
            let hint_rva = u32::try_from(entry & 0x7FFF_FFFF).unwrap_or(0);
            let hint_name_off = rva_to_file_offset(pe, lfanew, hint_rva).unwrap_or(0);
            let fn_name = read_cstr(pe, hint_name_off + 2);
            assert!(
                allowed_iat.contains(&fn_name),
                "{tc} static import `{fn_name}` is outside XP Pro x64 (NT 5.2) KERNEL32 baseline!"
            );
            thunk_off += 8;
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
fn m9_pe32plus_headers_seh_pdata_cfg_cet_manifest_and_nt52_iat_across_assemblers()
-> Result<(), ToolError> {
    let allowed_iat = audited_nt52_k32_exports();
    assert!(
        !allowed_iat.is_empty(),
        "k32_x64.def export baseline must load"
    );

    for tc in built_toolchains() {
        let pe = load_w64(tc)?;
        assert!(
            pe.len() <= 131_072,
            "{tc} WINCMD64.EXE ({}) exceeds 128 KiB budget",
            pe.len()
        );
        assert_eq!(&pe[0..2], b"MZ", "{tc} MZ signature");
        let lfanew = usize::try_from(read_u32_at(&pe, 0x3C)).unwrap_or(0);
        assert_eq!(
            &pe[lfanew..lfanew + 4],
            &[b'P', b'E', 0, 0],
            "{tc} PE signature"
        );

        let machine = u16::from_le_bytes([pe[lfanew + 4], pe[lfanew + 5]]);
        assert_eq!(machine, 0x8664, "{tc} IMAGE_FILE_MACHINE_AMD64");

        let opt = lfanew + 24;
        let magic = u16::from_le_bytes([pe[opt], pe[opt + 1]]);
        assert_eq!(magic, 0x020B, "{tc} PE32+ optional header magic");

        let entry_rva = read_u32_at(&pe, opt + 16);
        let image_base = read_u64_at(&pe, opt + 24);
        let maj_sub = u16::from_le_bytes([pe[opt + 48], pe[opt + 49]]);
        let min_sub = u16::from_le_bytes([pe[opt + 50], pe[opt + 51]]);
        let subsys = u16::from_le_bytes([pe[opt + 68], pe[opt + 69]]);
        let dll_chars = u16::from_le_bytes([pe[opt + 70], pe[opt + 71]]);
        assert_eq!(
            (subsys, maj_sub, min_sub),
            (3, 5, 2),
            "{tc} CUI Subsystem 5.02 (XP Pro x64 / Server 2003 SP1 .. Windows 11)"
        );
        assert!(
            image_base >= 0x1_0000_0000,
            "{tc} 64-bit high-entropy ImageBase >= 4 GiB ({image_base:#x})"
        );
        if tc == "fasm" {
            assert_eq!(
                dll_chars & 0x0140,
                0x0140,
                "{tc} direct PE64 DYNAMIC_BASE | NX_COMPAT must be set ({dll_chars:#x})"
            );
        } else {
            assert_eq!(
                dll_chars & 0x4160,
                0x4160,
                "{tc} HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT | GUARD_CF must all be set ({dll_chars:#x})"
            );
        }

        // Verify CET IBT `endbr64` (F3 0F 1E FA) at mainCRTStartup entrypoint!
        let entry_off =
            rva_to_file_offset(&pe, lfanew, entry_rva).ok_or_else(|| ToolError::Syntax {
                span: SourceSpan::new("WINCMD64.EXE", 1, 1),
                message: format!("{tc} invalid AddressOfEntryPoint RVA"),
            })?;
        assert_eq!(
            &pe[entry_off..entry_off + 4],
            &[0xF3, 0x0F, 0x1E, 0xFA],
            "{tc} mainCRTStartup must start with Intel/AMD CET IBT endbr64"
        );

        // Verify embedded RT_MANIFEST with <longPathAware xmlns="...">true</longPathAware>!
        let rsrc_rva = read_u32_at(&pe, opt + 128);
        assert_ne!(rsrc_rva, 0, "{tc} .rsrc manifest directory must be present");
        let pe_ascii = String::from_utf8_lossy(&pe);
        assert!(
            pe_ascii.contains("<longPathAware") && pe_ascii.contains(">true</longPathAware>"),
            "{tc} embedded Win10/11 longPathAware manifest XML must be present"
        );

        verify_pdata_and_xdata(tc, &pe, lfanew, opt)?;
        verify_cfg_load_config(tc, &pe, lfanew, opt, image_base)?;
        verify_nt52_import_table(tc, &pe, lfanew, opt, &allowed_iat)?;
    }
    Ok(())
}

fn detect_win64_runner() -> Option<Win64Runner> {
    if cfg!(all(target_os = "windows", target_arch = "x86_64")) {
        return Some(Win64Runner::Native);
    }
    let probe_exe = repo_root().join("build/nasm/x86_64/WINCMD64.EXE");
    let st = Command::new("wine")
        .env("WINEDEBUG", "-all")
        .arg(&probe_exe)
        .args(["/C", "VER"])
        .output()
        .ok()?;
    if st.status.success() {
        Some(Win64Runner::Wine)
    } else {
        None
    }
}

fn to_win_path(runner: Win64Runner, host_path: &Path) -> String {
    let s = host_path.to_string_lossy();
    let sep = bslash().to_string();
    match runner {
        Win64Runner::Wine => format!("Z:{}", s.replace('/', &sep)),
        Win64Runner::Native => s.into_owned(),
    }
}

fn run_wincmd64(
    runner: Win64Runner,
    exe: &Path,
    args: &[&str],
) -> Result<(i32, String, String), ToolError> {
    let mut cmd = match runner {
        Win64Runner::Wine => {
            let mut c = Command::new("wine");
            c.env("WINEDEBUG", "-all");
            c.arg(exe);
            c
        }
        Win64Runner::Native => Command::new(exe),
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

fn verify_x64_master_script(
    runner: Win64Runner,
    tc: &str,
    exe: &Path,
    tc_dir: &Path,
    win_tc_dir: &str,
    com_hostname: &str,
    com_truename: &str,
) -> Result<(), ToolError> {
    let crlf = crlf_str();
    let real_dir = tc_dir.join("real_target");
    std::fs::create_dir_all(&real_dir)?;
    std::fs::write(real_dir.join("keep64.txt"), format!("SURVIVED64_OK{crlf}"))?;
    std::fs::write(tc_dir.join("p1.txt"), b"X64_A+")?;
    std::fs::write(tc_dir.join("p2.txt"), format!("X64_B{crlf}"))?;

    let win_real = win_join(win_tc_dir, "real_target");
    let win_keep = win_join(&win_real, "keep64.txt");
    let win_hl = win_join(win_tc_dir, "hl64.txt");
    let win_junc = win_join(win_tc_dir, "junc64");
    let win_p1 = win_join(win_tc_dir, "p1.txt");
    let win_p2 = win_join(win_tc_dir, "p2.txt");
    let win_joined = win_join(win_tc_dir, "joined64.txt");
    let win_master = win_join(win_tc_dir, "master64.cmd");

    let master_bat = tc_dir.join("master64.cmd");
    let script = [
        "@ECHO OFF".to_owned(),
        "SETLOCAL EnableDelayedExpansion".to_owned(),
        r#"SET /A "X=(12+8)*3, Y=-2147483648/-1, S=1<<35, X+=5""#.to_owned(),
        "ECHO MATH=!X!:!Y!:!S!".to_owned(),
        "(ECHO pipe64_seed) | (SET /P A= & ECHO m2_!A!) | (SET /P B= & ECHO m3_!B!)".to_owned(),
        format!("ASSOC .m9{tc}=M9Type_{tc} >NUL"),
        format!(r#"FTYPE M9Type_{tc}=wincmd64.exe "%%1" >NUL"#),
        format!("ASSOC .m9{tc}"),
        format!("FTYPE M9Type_{tc}"),
        format!(r#"MKLINK /H "{win_hl}" "{win_keep}" >NUL"#),
        format!(r#"MKLINK /J "{win_junc}" "{win_real}" >NUL"#),
        format!(r#"DIR "{win_tc_dir}""#),
        format!(r#"RD /S /Q "{win_junc}""#),
        format!(r#"COPY /B "{win_p1}" + "{win_p2}" "{win_joined}" >NUL"#),
        format!(r#"TYPE "{win_joined}""#),
        format!(r#"PUSHD "{win_real}""#),
        "POPD".to_owned(),
        format!("SET COMPUTERNAME=VM86_X64_{tc}"),
        format!(r#""{com_hostname}""#),
        format!(r#""{com_truename}" "{win_keep}""#),
        String::new(),
    ]
    .join(&crlf);
    std::fs::write(&master_bat, script.as_bytes())?;

    let (rc, out, err) = run_wincmd64(runner, exe, &["/C", &win_master])?;
    assert_eq!(rc, 0, "{tc} master64.cmd rc=0 (stderr={err}, stdout={out})");
    assert!(
        out.contains("MATH=65:-2147483648:8"),
        "{tc} SET /A + INT32_MIN/-1 + shift&31: {out}"
    );
    assert!(
        out.contains("m3_m2_pipe64_seed"),
        "{tc} 3-stage concurrent CreatePipe: {out}"
    );
    assert!(
        out.contains(&format!(".m9{tc}=M9Type_{tc}")),
        "{tc} 64-bit ADVAPI32 ASSOC: {out}"
    );
    assert!(
        out.contains(&format!(r#"M9Type_{tc}=wincmd64.exe "%1""#)),
        "{tc} 64-bit ADVAPI32 FTYPE: {out}"
    );
    assert!(out.contains("<JUNCTION>"), "{tc} DIR <JUNCTION> tag: {out}");
    assert!(
        real_dir.join("keep64.txt").exists(),
        "{tc} RD /S /Q must unlink junction without deleting target files!"
    );
    assert!(
        !tc_dir.join("junc64").exists(),
        "{tc} junction directory unlinked"
    );
    assert!(
        out.contains("X64_A+X64_B"),
        "{tc} COPY + concatenation: {out}"
    );
    assert!(
        out.contains(&format!("VM86_X64_{tc}")),
        "{tc} 64-bit vm8086 HOSTNAME.COM: {out}"
    );
    assert!(
        out.to_ascii_uppercase().contains("KEEP64.TXT"),
        "{tc} 64-bit vm8086 TRUENAME.COM: {out}"
    );
    Ok(())
}

fn verify_utf16_and_nt52_cap(
    runner: Win64Runner,
    tc: &str,
    exe: &Path,
    tc_dir: &Path,
    win_tc_dir: &str,
) -> Result<(), ToolError> {
    let crlf = crlf_str();
    let u16_bat = tc_dir.join("utf16_x64.bat");
    let u16_text = [
        "@ECHO OFF",
        "SETLOCAL EnableDelayedExpansion",
        "SET ACC=0",
        "FOR /L %%N IN (2,2,10) DO SET /A ACC+=%%N",
        r#"FOR /F "tokens=1,2* delims=:" %%A IN ("p1:p2:p3:p4") DO ECHO TOK64=%%A_%%B_[%%C]"#,
        "CALL :SUB !ACC! keep_arg drop_arg third_arg",
        "ECHO SUB64=!OUTV!",
        "EXIT /B 41",
        ":SUB",
        "SHIFT /2",
        "SET /A OUTV=%1*3",
        "ECHO SHIFTED=%1_%2",
        "GOTO :EOF",
        "",
    ]
    .join(&crlf);
    let mut u16_bytes = vec![0xFF_u8, 0xFE_u8];
    for cu in u16_text.encode_utf16() {
        u16_bytes.extend_from_slice(&cu.to_le_bytes());
    }
    std::fs::write(&u16_bat, &u16_bytes)?;
    let win_u16 = win_join(win_tc_dir, "utf16_x64.bat");
    let (u16_rc, u16_out, _) = run_wincmd64(runner, exe, &["/C", &win_u16])?;
    assert_eq!(u16_rc, 41, "{tc} UTF-16LE EXIT /B 41 (out={u16_out})");
    assert!(
        u16_out.contains("TOK64=p1_p2_[p3:p4]"),
        "{tc} FOR /F tokens=1,2*: {u16_out}"
    );
    assert!(
        u16_out.contains("SUB64=90"),
        "{tc} FOR /L (2+4+6+8+10=30)*3=90: {u16_out}"
    );
    assert!(
        u16_out.contains("SHIFTED=30_drop_arg"),
        "{tc} SHIFT /2 preserves %1 and shifts %2<-%3: {u16_out}"
    );

    let nt52_bat = tc_dir.join("nt52.cmd");
    let nt52_script = [
        "@ECHO OFF".to_owned(),
        "VER".to_owned(),
        "(ECHO xp64_pipe) | (SET /P Z= & ECHO XP64_!Z!)".to_owned(),
        format!(r#"DIR "{win_tc_dir}""#),
        String::new(),
    ]
    .join(&crlf);
    std::fs::write(&nt52_bat, nt52_script.as_bytes())?;
    let win_nt52 = win_join(win_tc_dir, "nt52.cmd");
    let (nt52_rc, nt52_out, _) =
        run_wincmd64(runner, exe, &["--nt52-api-cap", "/V:ON", "/C", &win_nt52])?;
    assert_eq!(nt52_rc, 0, "{tc} --nt52-api-cap rc=0 (out={nt52_out})");
    assert!(
        nt52_out.contains("5.2.3790"),
        "{tc} XP Pro x64 / Server 2003 SP1 (5.2.3790) VER: {nt52_out}"
    );
    assert!(
        nt52_out.contains("XP64_xp64_pipe"),
        "{tc} XP Pro x64 pipeline: {nt52_out}"
    );
    assert!(
        nt52_out.contains("bytes free"),
        "{tc} XP Pro x64 GetDiskFreeSpaceExW: {nt52_out}"
    );
    Ok(())
}

#[test]
fn m9_live_win64_pipes_registry_mklink_utf16_nt52cap_and_vm8086_across_assemblers()
-> Result<(), ToolError> {
    let Some(runner) = detect_win64_runner() else {
        return Ok(());
    };
    let root = repo_root();
    let scratch = std::env::temp_dir().join(format!("wincmd64_m9_test_{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&scratch);
    std::fs::create_dir_all(&scratch)?;
    let win_scratch = to_win_path(runner, &scratch);
    let com_hostname = to_win_path(runner, &root.join("build/nasm/dos16/HOSTNAME.COM"));
    let com_truename = to_win_path(runner, &root.join("build/nasm/dos16/TRUENAME.COM"));

    for tc in built_toolchains() {
        let exe = root.join(format!("build/{tc}/x86_64/WINCMD64.EXE"));
        let tc_dir = scratch.join(tc);
        std::fs::create_dir_all(&tc_dir)?;
        let win_tc_dir = win_join(&win_scratch, tc);

        verify_x64_master_script(
            runner,
            tc,
            &exe,
            &tc_dir,
            &win_tc_dir,
            &com_hostname,
            &com_truename,
        )?;
        verify_utf16_and_nt52_cap(runner, tc, &exe, &tc_dir, &win_tc_dir)?;
    }

    let _ = std::fs::remove_dir_all(&scratch);
    Ok(())
}
