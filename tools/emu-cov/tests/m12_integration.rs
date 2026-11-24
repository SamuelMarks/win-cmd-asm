//! End-to-end integration tests for all 19 CRT-free Windows PE companion utilities
//! (`MEM`, `ATTRIB`, `FIND`, `FINDSTR`, `SORT`, `MORE`, `FC`, `COMP`, `TREE`, `WHERE`,
//! `XCOPY`, `REPLACE`, `ROBOCOPY`, `CHOICE`, `TIMEOUT`, `TRUENAME`, `REG`, `HOSTNAME`,
//! `WHOAMI`) across all 4 PE architectures (`IA-32`, `x86-64`, `ARM32`, `ARM64`) and
//! all assembler toolchains (`NASM`, `FASM`, `JWasm`, `FASMARM`, `LLVM-MC`).

use emu_cov::arm32_pe::{Arm32RunResult, Arm32Vm};
use emu_cov::arm64_pe::{Arm64RunResult, Arm64Vm};
use emu_cov::win_mock::{BS, DQ, SharedWinEnv, emu_err};
use emu_cov::x86_pe::{X86PeVm, X86RunResult};
use std::cell::RefCell;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::rc::Rc;
use wincmd_diag::{ErrCode, ToolError};

const CRLF_BYTES: [u8; 2] = [13, 10];

const fn crlf() -> &'static str {
    match core::str::from_utf8(&CRLF_BYTES) {
        Ok(s) => s,
        Err(_) => "",
    }
}

const ALL_19_UTILS: [&str; 19] = [
    "MEM.EXE",
    "ATTRIB.EXE",
    "FIND.EXE",
    "FINDSTR.EXE",
    "SORT.EXE",
    "MORE.EXE",
    "FC.EXE",
    "COMP.EXE",
    "TREE.EXE",
    "WHERE.EXE",
    "XCOPY.EXE",
    "REPLACE.EXE",
    "ROBOCOPY.EXE",
    "CHOICE.EXE",
    "TIMEOUT.EXE",
    "TRUENAME.EXE",
    "REG.EXE",
    "HOSTNAME.EXE",
    "WHOAMI.EXE",
];

fn repo_root() -> Result<PathBuf, ToolError> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    manifest
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| emu_err(0, ErrCode::GeneralFailure, "cannot resolve workspace root"))
}

fn rva_to_file_offset(pe: &[u8], lfanew: usize, rva: u32) -> Option<usize> {
    let num_secs = usize::from(u16::from_le_bytes([
        *pe.get(lfanew + 6)?,
        *pe.get(lfanew + 7)?,
    ]));
    let opt_sz = usize::from(u16::from_le_bytes([
        *pe.get(lfanew + 20)?,
        *pe.get(lfanew + 21)?,
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

#[test]
fn m12_all_228_pe_utility_binaries_headers_zero_crt_and_32k_budget() -> Result<(), ToolError> {
    let root = repo_root()?;
    let matrix: &[(&str, u16, bool)] = &[
        ("nasm/x86_32", 0x014C, false),
        ("fasm/x86_32", 0x014C, false),
        ("fasm-pe32/x86_32", 0x014C, false),
        ("jwasm/x86_32", 0x014C, false),
        ("nasm/x86_64", 0x8664, true),
        ("fasm/x86_64", 0x8664, true),
        ("fasm-coff/x86_64", 0x8664, true),
        ("jwasm/x86_64", 0x8664, true),
        ("fasmarm/arm32", 0x01C4, false),
        ("llvm/arm32", 0x01C4, false),
        ("fasmarm/arm64", 0xAA64, true),
        ("llvm/arm64", 0xAA64, true),
    ];

    let mut checked_dirs = 0usize;
    for &(dir, expected_machine, is_pe64) in matrix {
        if !root.join("build").join(dir).join("MEM.EXE").is_file() {
            continue;
        }
        checked_dirs += 1;
        for &exe in &ALL_19_UTILS {
            let rel = format!("build/{dir}/{exe}");
            let pe = fs::read(root.join(&rel))?;
            assert!(
                pe.len() <= 32_768,
                "{rel} size {} exceeds 32 KiB companion utility budget",
                pe.len()
            );
            assert_eq!(&pe[0..2], b"MZ", "{rel} MZ header");
            let lfa = usize::try_from(u32::from_le_bytes([pe[0x3C], pe[0x3D], pe[0x3E], pe[0x3F]]))
                .unwrap_or(0);
            assert_eq!(&pe[lfa..lfa + 4], &[b'P', b'E', 0, 0], "{rel} PE sig");
            let machine = u16::from_le_bytes([pe[lfa + 4], pe[lfa + 5]]);
            assert_eq!(machine, expected_machine, "{rel} machine type");

            let opt = lfa + 24;
            let magic = u16::from_le_bytes([pe[opt], pe[opt + 1]]);
            assert_eq!(
                magic,
                if is_pe64 { 0x020B } else { 0x010B },
                "{rel} optional header magic"
            );

            let imp_dd_off = opt + if is_pe64 { 120 } else { 104 };
            let imp_rva = u32::from_le_bytes([
                pe[imp_dd_off],
                pe[imp_dd_off + 1],
                pe[imp_dd_off + 2],
                pe[imp_dd_off + 3],
            ]);
            assert_ne!(imp_rva, 0, "{rel} import directory present");
            let mut desc_off = rva_to_file_offset(&pe, lfa, imp_rva).unwrap_or(0);
            let mut dll_cnt = 0usize;
            loop {
                let oft = u32::from_le_bytes([
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
                let ft = u32::from_le_bytes([
                    pe[desc_off + 16],
                    pe[desc_off + 17],
                    pe[desc_off + 18],
                    pe[desc_off + 19],
                ]);
                if oft == 0 && name_rva == 0 && ft == 0 {
                    break;
                }
                dll_cnt += 1;
                let dll = read_cstr(&pe, rva_to_file_offset(&pe, lfa, name_rva).unwrap_or(0));
                assert!(
                    dll.eq_ignore_ascii_case("KERNEL32.DLL"),
                    "{rel} must only statically import KERNEL32.DLL, found `{dll}`"
                );
                desc_off += 20;
            }
            assert_eq!(dll_cnt, 1, "{rel} single static DLL descriptor");
        }
    }
    assert!(checked_dirs >= 4, "expected at least 4 built PE utility directories, got {checked_dirs}");
    Ok(())
}

fn seed_utility_vfs(shared: &Rc<RefCell<SharedWinEnv>>) {
    let mut env = shared.borrow_mut();
    let nl = crlf();
    env.mkdir(&format!("C:{BS}work"));
    env.write_file(
        &format!("C:{BS}work{BS}a.txt"),
        format!("alpha1{nl}beta2{nl}").as_bytes(),
    );
    env.write_file(
        &format!("C:{BS}work{BS}a_copy.txt"),
        format!("alpha1{nl}beta2{nl}").as_bytes(),
    );
    env.write_file(
        &format!("C:{BS}work{BS}b.txt"),
        format!("alpha1{nl}DIFF9{nl}").as_bytes(),
    );
    env.write_file(
        &format!("C:{BS}work{BS}lines.txt"),
        format!("alpha1{nl}beta2{nl}gamma3{nl}").as_bytes(),
    );
    env.write_file(
        &format!("C:{BS}work{BS}unsorted.txt"),
        format!("cherry{nl}apple{nl}banana{nl}").as_bytes(),
    );
    env.mkdir(&format!("C:{BS}work{BS}tree_root"));
    env.mkdir(&format!("C:{BS}work{BS}tree_root{BS}sub1"));
    env.write_file(
        &format!("C:{BS}work{BS}tree_root{BS}sub1{BS}leaf.txt"),
        format!("leaf_payload{nl}").as_bytes(),
    );
    env.mkdir(&format!("C:{BS}work{BS}rb_src"));
    for idx in 1..=6 {
        env.write_file(
            &format!("C:{BS}work{BS}rb_src{BS}f{idx}.dat"),
            format!("payload_{idx}{nl}").as_bytes(),
        );
    }
}

fn run_arm32_util(
    tc: &str,
    exe: &str,
    args: &str,
    stdin: &[u8],
    shared: &Rc<RefCell<SharedWinEnv>>,
) -> Result<Arm32RunResult, ToolError> {
    let pe = fs::read(repo_root()?.join(format!("build/{tc}/arm32/{exe}")))?;
    let cmd = if args.is_empty() {
        format!("{DQ}{exe}{DQ}")
    } else {
        format!("{DQ}{exe}{DQ} {args}")
    };
    let mut vm = Arm32Vm::from_pe_bytes(&pe, &cmd, &[("USERNAME", "builder")], Rc::clone(shared))?;
    if !stdin.is_empty() {
        vm.set_stdin(stdin);
    }
    vm.run(10_000_000)
}

fn run_arm64_util(
    tc: &str,
    exe: &str,
    args: &str,
    stdin: &[u8],
    shared: &Rc<RefCell<SharedWinEnv>>,
) -> Result<Arm64RunResult, ToolError> {
    let pe = fs::read(repo_root()?.join(format!("build/{tc}/arm64/{exe}")))?;
    let cmd = if args.is_empty() {
        format!("{DQ}{exe}{DQ}")
    } else {
        format!("{DQ}{exe}{DQ} {args}")
    };
    let mut vm = Arm64Vm::from_pe_bytes(&pe, &cmd, &[("USERNAME", "builder")], Rc::clone(shared))?;
    if !stdin.is_empty() {
        vm.set_stdin(stdin);
    }
    vm.run(12_000_000)
}

fn verify_arm_utils_part_a(
    tc: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
) -> Result<(), ToolError> {
    let nl = crlf();
    // 1. MEM /C
    let r32 = run_arm32_util(tc, "MEM.EXE", "/C", b"", s32)?;
    let r64 = run_arm64_util(tc, "MEM.EXE", "/C", b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0), "{tc} MEM /C");
    assert!(r32.stdout.contains("Physical") && r32.stdout.contains("Working Set"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} MEM /C cross-ISA parity");

    // 2. ATTRIB +R +H & display & -R -H
    let alpha_file = format!("C:{BS}work{BS}a.txt");
    let r32 = run_arm32_util(tc, "ATTRIB.EXE", &format!("+R +H {alpha_file}"), b"", s32)?;
    let r64 = run_arm64_util(tc, "ATTRIB.EXE", &format!("+R +H {alpha_file}"), b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    let r32 = run_arm32_util(tc, "ATTRIB.EXE", &alpha_file, b"", s32)?;
    let r64 = run_arm64_util(tc, "ATTRIB.EXE", &alpha_file, b"", s64)?;
    assert!(r32.stdout.contains('H') && r32.stdout.contains('R'));
    assert_eq!(r32.stdout, r64.stdout, "{tc} ATTRIB parity");
    let _ = run_arm32_util(tc, "ATTRIB.EXE", &format!("-R -H {alpha_file}"), b"", s32)?;
    let _ = run_arm64_util(tc, "ATTRIB.EXE", &format!("-R -H {alpha_file}"), b"", s64)?;

    // 3. FIND /N "beta" & 4. FINDSTR /R /N "^be.*2$"
    let lines_f = format!("C:{BS}work{BS}lines.txt");
    let r32 = run_arm32_util(
        tc,
        "FIND.EXE",
        &format!("/N {DQ}beta{DQ} {lines_f}"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "FIND.EXE",
        &format!("/N {DQ}beta{DQ} {lines_f}"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("[2]beta2"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} FIND parity");

    let r32 = run_arm32_util(
        tc,
        "FINDSTR.EXE",
        &format!("/R /N {DQ}^be.*2${DQ} {lines_f}"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "FINDSTR.EXE",
        &format!("/R /N {DQ}^be.*2${DQ} {lines_f}"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("2:beta2"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} FINDSTR parity");

    // 5. SORT /R & 6. MORE
    let uns_f = format!("C:{BS}work{BS}unsorted.txt");
    let r32 = run_arm32_util(tc, "SORT.EXE", &format!("/R {uns_f}"), b"", s32)?;
    let r64 = run_arm64_util(tc, "SORT.EXE", &format!("/R {uns_f}"), b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert_eq!(r32.stdout, format!("cherry{nl}banana{nl}apple{nl}"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} SORT parity");

    let r32 = run_arm32_util(tc, "MORE.EXE", &lines_f, b"", s32)?;
    let r64 = run_arm64_util(tc, "MORE.EXE", &lines_f, b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("gamma3"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} MORE parity");
    verify_arm_utils_part_a_cmp_tree(tc, s32, s64, &alpha_file)
}

fn verify_arm_utils_part_a_cmp_tree(
    tc: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
    alpha_file: &str,
) -> Result<(), ToolError> {
    // 7. FC (equal & diff) & 8. COMP /M & 9. TREE /F /A & 10. WHERE /R
    let copy_file = format!("C:{BS}work{BS}a_copy.txt");
    let diff_file = format!("C:{BS}work{BS}b.txt");
    let r32 = run_arm32_util(tc, "FC.EXE", &format!("{alpha_file} {copy_file}"), b"", s32)?;
    let r64 = run_arm64_util(tc, "FC.EXE", &format!("{alpha_file} {copy_file}"), b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert_eq!(r32.stdout, r64.stdout, "{tc} FC equal parity");
    let r32 = run_arm32_util(
        tc,
        "FC.EXE",
        &format!("/B {alpha_file} {diff_file}"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "FC.EXE",
        &format!("/B {alpha_file} {diff_file}"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (1, 1));
    assert_eq!(r32.stdout, r64.stdout, "{tc} FC /B diff parity");

    let r32 = run_arm32_util(
        tc,
        "COMP.EXE",
        &format!("/M {alpha_file} {copy_file}"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "COMP.EXE",
        &format!("/M {alpha_file} {copy_file}"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("Files compare OK"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} COMP equal parity");

    let tr_dir = format!("C:{BS}work{BS}tree_root");
    let r32 = run_arm32_util(tc, "TREE.EXE", &format!("/F /A {tr_dir}"), b"", s32)?;
    let r64 = run_arm64_util(tc, "TREE.EXE", &format!("/F /A {tr_dir}"), b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("sub1") && r32.stdout.contains("leaf.txt"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} TREE parity");

    let r32 = run_arm32_util(tc, "WHERE.EXE", &format!("/R {tr_dir} leaf.txt"), b"", s32)?;
    let r64 = run_arm64_util(tc, "WHERE.EXE", &format!("/R {tr_dir} leaf.txt"), b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("leaf.txt"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} WHERE parity");
    Ok(())
}

fn verify_arm_utils_part_b(
    tc: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
) -> Result<(), ToolError> {
    let alpha_file = format!("C:{BS}work{BS}a.txt");
    let diff_file = format!("C:{BS}work{BS}b.txt");
    let tr_dir = format!("C:{BS}work{BS}tree_root");
    let xc_dst = format!("C:{BS}work{BS}xc_dst");

    // 11. XCOPY /S /E /Y & 12. REPLACE /A
    let r32 = run_arm32_util(
        tc,
        "XCOPY.EXE",
        &format!("{tr_dir} {xc_dst} /S /E /Y"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "XCOPY.EXE",
        &format!("{tr_dir} {xc_dst} /S /E /Y"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("1 File(s) copied"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} XCOPY parity");

    let r32 = run_arm32_util(
        tc,
        "REPLACE.EXE",
        &format!("{diff_file} {xc_dst} /A"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "REPLACE.EXE",
        &format!("{diff_file} {xc_dst} /A"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("1 file(s) added"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} REPLACE parity");

    // 13. ROBOCOPY /E /MT:4 (Multi-threaded CreateThread worker pool!)
    let rb_src = format!("C:{BS}work{BS}rb_src");
    let rb_dst = format!("C:{BS}work{BS}rb_dst");
    let r32 = run_arm32_util(
        tc,
        "ROBOCOPY.EXE",
        &format!("{rb_src} {rb_dst} /E /MT:4"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "ROBOCOPY.EXE",
        &format!("{rb_src} {rb_dst} /E /MT:4"),
        b"",
        s64,
    )?;
    assert_eq!(
        (r32.exit_code, r64.exit_code),
        (1, 1),
        "{tc} ROBOCOPY bitmask 1"
    );
    assert!(r32.stdout.contains("Files Copied : 6"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} ROBOCOPY /MT:4 parity");
    for idx in 1..=6 {
        let copied_key = format!("C:{BS}work{BS}rb_dst{BS}f{idx}.dat");
        assert!(s32.borrow().read_file(&copied_key).is_some());
        assert!(s64.borrow().read_file(&copied_key).is_some());
    }

    // 14. CHOICE, 15. TIMEOUT, 16. TRUENAME
    let r32 = run_arm32_util(tc, "CHOICE.EXE", "/C YNC /M Pick", b"c", s32)?;
    let r64 = run_arm64_util(tc, "CHOICE.EXE", "/C YNC /M Pick", b"c", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (3, 3));
    assert!(r32.stdout.contains("Pick [Y,N,C]?"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} CHOICE parity");

    let r32 = run_arm32_util(tc, "TIMEOUT.EXE", "/T 0 /NOBREAK", b"", s32)?;
    let r64 = run_arm64_util(tc, "TIMEOUT.EXE", "/T 0 /NOBREAK", b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert_eq!(r32.stdout, r64.stdout, "{tc} TIMEOUT parity");

    let r32 = run_arm32_util(tc, "TRUENAME.EXE", &alpha_file, b"", s32)?;
    let r64 = run_arm64_util(tc, "TRUENAME.EXE", &alpha_file, b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.to_ascii_lowercase().contains("a.txt"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} TRUENAME parity");
    verify_arm_utils_reg_and_id(tc, s32, s64)
}

fn verify_arm_utils_reg_and_id(
    tc: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
) -> Result<(), ToolError> {
    // 17. REG ADD / QUERY / EXPORT / DELETE, 18. HOSTNAME, 19. WHOAMI
    let reg_key = format!("HKCU{BS}Software{BS}WinCmdM12");
    let add_args = format!("ADD {reg_key} /v Magic /t REG_DWORD /d 0x2A");
    assert_eq!(
        run_arm32_util(tc, "REG.EXE", &add_args, b"", s32)?.exit_code,
        0
    );
    assert_eq!(
        run_arm64_util(tc, "REG.EXE", &add_args, b"", s64)?.exit_code,
        0
    );
    let r32 = run_arm32_util(
        tc,
        "REG.EXE",
        &format!("QUERY {reg_key} /v Magic"),
        b"",
        s32,
    )?;
    let r64 = run_arm64_util(
        tc,
        "REG.EXE",
        &format!("QUERY {reg_key} /v Magic"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("REG_DWORD") && r32.stdout.contains("42"));
    assert_eq!(r32.stdout, r64.stdout, "{tc} REG QUERY parity");
    let reg_out = format!("C:{BS}work{BS}dump.reg");
    assert_eq!(
        run_arm32_util(
            tc,
            "REG.EXE",
            &format!("EXPORT {reg_key} {reg_out}"),
            b"",
            s32
        )?
        .exit_code,
        0
    );
    assert_eq!(
        run_arm64_util(
            tc,
            "REG.EXE",
            &format!("EXPORT {reg_key} {reg_out}"),
            b"",
            s64
        )?
        .exit_code,
        0
    );
    assert_eq!(
        run_arm32_util(
            tc,
            "REG.EXE",
            &format!("DELETE {reg_key} /v Magic"),
            b"",
            s32
        )?
        .exit_code,
        0
    );
    assert_eq!(
        run_arm64_util(
            tc,
            "REG.EXE",
            &format!("DELETE {reg_key} /v Magic"),
            b"",
            s64
        )?
        .exit_code,
        0
    );

    let r32 = run_arm32_util(tc, "HOSTNAME.EXE", "", b"", s32)?;
    let r64 = run_arm64_util(tc, "HOSTNAME.EXE", "", b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert_ne!(r32.stdout.trim(), "");
    assert_eq!(r32.stdout, r64.stdout, "{tc} HOSTNAME parity");

    let r32 = run_arm32_util(tc, "WHOAMI.EXE", "/USER /FO CSV", b"", s32)?;
    let r64 = run_arm64_util(tc, "WHOAMI.EXE", "/USER /FO CSV", b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert!(r32.stdout.contains("S-1-5-21-"), "got: {}", r32.stdout);
    assert_eq!(r32.stdout, r64.stdout, "{tc} WHOAMI /USER /FO CSV parity");
    Ok(())
}

#[test]
fn m12_arm32_and_arm64_all_19_utilities_functional_and_parity() -> Result<(), ToolError> {
    let root = repo_root()?;
    let mut tested = 0usize;
    for tc in ["fasmarm", "llvm"] {
        if !root.join(format!("build/{tc}/arm64/MEM.EXE")).is_file() {
            continue;
        }
        tested += 1;
        let s32 = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let s64 = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        seed_utility_vfs(&s32);
        seed_utility_vfs(&s64);
        verify_arm_utils_part_a(tc, &s32, &s64)?;
        verify_arm_utils_part_b(tc, &s32, &s64)?;
    }
    assert!(tested >= 1, "at least one ARM toolchain utility suite must be built");
    Ok(())
}

fn run_x86_pe_util(
    arch_dir: &str,
    exe: &str,
    args: &str,
    stdin: &[u8],
    shared: &Rc<RefCell<SharedWinEnv>>,
) -> Result<X86RunResult, ToolError> {
    let pe = fs::read(repo_root()?.join(format!("build/{arch_dir}/{exe}")))?;
    let cmd = if args.is_empty() {
        format!("{DQ}{exe}{DQ}")
    } else {
        format!("{DQ}{exe}{DQ} {args}")
    };
    let mut vm = X86PeVm::from_pe_bytes(&pe, &cmd, &[("USERNAME", "builder")], Rc::clone(shared))?;
    if !stdin.is_empty() {
        vm.set_stdin(stdin);
    }
    vm.run(12_000_000)
}

fn verify_x86_utils_part_a(
    dir32: &str,
    dir64: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
) -> Result<(), ToolError> {
    let nl = crlf();
    let r32 = run_x86_pe_util(dir32, "MEM.EXE", "/C", b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "MEM.EXE", "/C", b"", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (0, 0));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} MEM /C");

    let alpha_file = format!("C:{BS}work{BS}a.txt");
    assert_eq!(
        run_x86_pe_util(
            dir32,
            "ATTRIB.EXE",
            &format!("+R +H {alpha_file}"),
            b"",
            s32
        )?
        .exit_code,
        0
    );
    assert_eq!(
        run_x86_pe_util(
            dir64,
            "ATTRIB.EXE",
            &format!("+R +H {alpha_file}"),
            b"",
            s64
        )?
        .exit_code,
        0
    );
    let r32 = run_x86_pe_util(dir32, "ATTRIB.EXE", &alpha_file, b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "ATTRIB.EXE", &alpha_file, b"", s64)?;
    assert!(r32.stdout.contains('H') && r32.stdout.contains('R'));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} ATTRIB");
    let _ = run_x86_pe_util(
        dir32,
        "ATTRIB.EXE",
        &format!("-R -H {alpha_file}"),
        b"",
        s32,
    )?;
    let _ = run_x86_pe_util(
        dir64,
        "ATTRIB.EXE",
        &format!("-R -H {alpha_file}"),
        b"",
        s64,
    )?;

    let lines_f = format!("C:{BS}work{BS}lines.txt");
    let r32 = run_x86_pe_util(
        dir32,
        "FIND.EXE",
        &format!("/N {DQ}beta{DQ} {lines_f}"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "FIND.EXE",
        &format!("/N {DQ}beta{DQ} {lines_f}"),
        b"",
        s64,
    )?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} FIND");

    let r32 = run_x86_pe_util(
        dir32,
        "FINDSTR.EXE",
        &format!("/R /N {DQ}^be.*2${DQ} {lines_f}"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "FINDSTR.EXE",
        &format!("/R /N {DQ}^be.*2${DQ} {lines_f}"),
        b"",
        s64,
    )?;
    assert!(r32.stdout.contains("2:beta2"));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} FINDSTR");

    let uns_f = format!("C:{BS}work{BS}unsorted.txt");
    let r32 = run_x86_pe_util(dir32, "SORT.EXE", &format!("/R {uns_f}"), b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "SORT.EXE", &format!("/R {uns_f}"), b"", s64)?;
    assert_eq!(r32.stdout, format!("cherry{nl}banana{nl}apple{nl}"));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} SORT");

    let r32 = run_x86_pe_util(dir32, "MORE.EXE", &lines_f, b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "MORE.EXE", &lines_f, b"", s64)?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} MORE");
    verify_x86_utils_part_a_cmp_tree(dir32, dir64, s32, s64, &alpha_file)
}

fn verify_x86_utils_part_a_cmp_tree(
    dir32: &str,
    dir64: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
    alpha_file: &str,
) -> Result<(), ToolError> {
    let copy_file = format!("C:{BS}work{BS}a_copy.txt");
    let diff_file = format!("C:{BS}work{BS}b.txt");
    let r32 = run_x86_pe_util(
        dir32,
        "FC.EXE",
        &format!("/B {alpha_file} {diff_file}"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "FC.EXE",
        &format!("/B {alpha_file} {diff_file}"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (1, 1));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} FC /B");

    let r32 = run_x86_pe_util(
        dir32,
        "COMP.EXE",
        &format!("/M {alpha_file} {copy_file}"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "COMP.EXE",
        &format!("/M {alpha_file} {copy_file}"),
        b"",
        s64,
    )?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} COMP");

    let tr_dir = format!("C:{BS}work{BS}tree_root");
    let r32 = run_x86_pe_util(dir32, "TREE.EXE", &format!("/F /A {tr_dir}"), b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "TREE.EXE", &format!("/F /A {tr_dir}"), b"", s64)?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} TREE");

    let r32 = run_x86_pe_util(
        dir32,
        "WHERE.EXE",
        &format!("/R {tr_dir} leaf.txt"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "WHERE.EXE",
        &format!("/R {tr_dir} leaf.txt"),
        b"",
        s64,
    )?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} WHERE");
    Ok(())
}

fn verify_x86_utils_part_b(
    dir32: &str,
    dir64: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
) -> Result<(), ToolError> {
    let alpha_file = format!("C:{BS}work{BS}a.txt");
    let diff_file = format!("C:{BS}work{BS}b.txt");
    let tr_dir = format!("C:{BS}work{BS}tree_root");
    let xc_dst = format!("C:{BS}work{BS}xc_dst");

    let r32 = run_x86_pe_util(
        dir32,
        "XCOPY.EXE",
        &format!("{tr_dir} {xc_dst} /S /E /Y"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "XCOPY.EXE",
        &format!("{tr_dir} {xc_dst} /S /E /Y"),
        b"",
        s64,
    )?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} XCOPY");

    let r32 = run_x86_pe_util(
        dir32,
        "REPLACE.EXE",
        &format!("{diff_file} {xc_dst} /A"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "REPLACE.EXE",
        &format!("{diff_file} {xc_dst} /A"),
        b"",
        s64,
    )?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} REPLACE");

    let rb_src = format!("C:{BS}work{BS}rb_src");
    let rb_dst = format!("C:{BS}work{BS}rb_dst");
    let r32 = run_x86_pe_util(
        dir32,
        "ROBOCOPY.EXE",
        &format!("{rb_src} {rb_dst} /E /MT:4"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "ROBOCOPY.EXE",
        &format!("{rb_src} {rb_dst} /E /MT:4"),
        b"",
        s64,
    )?;
    assert_eq!((r32.exit_code, r64.exit_code), (1, 1));
    assert!(r32.stdout.contains("Files Copied : 6"));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} ROBOCOPY /MT:4");

    let r32 = run_x86_pe_util(dir32, "CHOICE.EXE", "/C YNC /M Pick", b"c", s32)?;
    let r64 = run_x86_pe_util(dir64, "CHOICE.EXE", "/C YNC /M Pick", b"c", s64)?;
    assert_eq!((r32.exit_code, r64.exit_code), (3, 3));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} CHOICE");

    let r32 = run_x86_pe_util(dir32, "TIMEOUT.EXE", "/T 0 /NOBREAK", b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "TIMEOUT.EXE", "/T 0 /NOBREAK", b"", s64)?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} TIMEOUT");

    let r32 = run_x86_pe_util(dir32, "TRUENAME.EXE", &alpha_file, b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "TRUENAME.EXE", &alpha_file, b"", s64)?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} TRUENAME");
    verify_x86_utils_reg_and_id(dir32, dir64, s32, s64)
}

fn verify_x86_utils_reg_and_id(
    dir32: &str,
    dir64: &str,
    s32: &Rc<RefCell<SharedWinEnv>>,
    s64: &Rc<RefCell<SharedWinEnv>>,
) -> Result<(), ToolError> {
    let reg_key = format!("HKCU{BS}Software{BS}WinCmdX86");
    let add_args = format!("ADD {reg_key} /v Magic /t REG_DWORD /d 0x2A");
    assert_eq!(
        run_x86_pe_util(dir32, "REG.EXE", &add_args, b"", s32)?.exit_code,
        0
    );
    assert_eq!(
        run_x86_pe_util(dir64, "REG.EXE", &add_args, b"", s64)?.exit_code,
        0
    );
    let r32 = run_x86_pe_util(
        dir32,
        "REG.EXE",
        &format!("QUERY {reg_key} /v Magic"),
        b"",
        s32,
    )?;
    let r64 = run_x86_pe_util(
        dir64,
        "REG.EXE",
        &format!("QUERY {reg_key} /v Magic"),
        b"",
        s64,
    )?;
    assert!(r32.stdout.contains("REG_DWORD") && r32.stdout.contains("42"));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} REG QUERY");

    let r32 = run_x86_pe_util(dir32, "HOSTNAME.EXE", "", b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "HOSTNAME.EXE", "", b"", s64)?;
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} HOSTNAME");

    let r32 = run_x86_pe_util(dir32, "WHOAMI.EXE", "/USER /FO CSV", b"", s32)?;
    let r64 = run_x86_pe_util(dir64, "WHOAMI.EXE", "/USER /FO CSV", b"", s64)?;
    assert!(r32.stdout.contains("S-1-5-21-"));
    assert_eq!(r32.stdout, r64.stdout, "{dir32} vs {dir64} WHOAMI");
    Ok(())
}

#[test]
fn m12_ia32_and_x64_all_19_utilities_x86_pe_vm_parity() -> Result<(), ToolError> {
    let root = repo_root()?;
    let mut tested = 0usize;
    for (dir32, dir64) in [
        ("nasm/x86_32", "nasm/x86_64"),
        ("fasm/x86_32", "fasm/x86_64"),
        ("fasm-pe32/x86_32", "fasm-coff/x86_64"),
        ("jwasm/x86_32", "jwasm/x86_64"),
    ] {
        if !root.join("build").join(dir32).join("MEM.EXE").is_file()
            || !root.join("build").join(dir64).join("MEM.EXE").is_file()
        {
            continue;
        }
        tested += 1;
        let s32 = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        let s64 = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        seed_utility_vfs(&s32);
        seed_utility_vfs(&s64);
        verify_x86_utils_part_a(dir32, dir64, &s32, &s64)?;
        verify_x86_utils_part_b(dir32, dir64, &s32, &s64)?;
    }
    assert!(tested >= 2, "at least NASM and JWasm x86 PE utilities must be built");
    Ok(())
}

fn can_run_host_pe(exe_path: &Path) -> bool {
    if !exe_path.is_file() {
        return false;
    }
    if cfg!(target_os = "windows") {
        return true;
    }
    Command::new("wine")
        .env("WINEDEBUG", "-all")
        .arg(exe_path)
        .args(["/C", "VER"])
        .output()
        .is_ok_and(|o| o.status.success())
}

fn run_host_pe(
    exe_path: &Path,
    args: &[&str],
    stdin_bytes: &[u8],
) -> Result<(i32, String), ToolError> {
    let mut cmd = if cfg!(target_os = "windows") {
        Command::new(exe_path)
    } else {
        let mut c = Command::new("wine");
        c.env("WINEDEBUG", "-all");
        c.arg(exe_path);
        c
    };
    cmd.args(args)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    let mut child = cmd.spawn()?;
    if !stdin_bytes.is_empty()
        && let Some(mut sin) = child.stdin.take()
    {
        let _ = sin.write_all(stdin_bytes);
    }
    let out = child.wait_with_output()?;
    Ok((
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).into_owned(),
    ))
}

#[test]
fn m12_ia32_and_x64_utilities_wine_or_native_execution() -> Result<(), ToolError> {
    let nl = crlf();
    let root = repo_root()?;
    let tmp = std::env::temp_dir().join(format!("wincmd_m12_{}", std::process::id()));
    let _ = fs::remove_dir_all(&tmp);
    fs::create_dir_all(tmp.join("rb_src"))?;
    fs::write(tmp.join("a.txt"), format!("alpha1{nl}beta2{nl}"))?;
    fs::write(tmp.join("uns.txt"), format!("z{nl}a{nl}m{nl}"))?;
    fs::write(tmp.join("rb_src").join("one.txt"), format!("111{nl}"))?;
    fs::write(tmp.join("rb_src").join("two.txt"), format!("222{nl}"))?;

    let x64_dir = if root.join("build/fasm/x86_64/WINCMD64.EXE").is_file() {
        "fasm/x86_64"
    } else {
        "nasm/x86_64"
    };
    for (arch_dir, shell_exe) in [
        ("nasm/x86_32", "WINCMD32.EXE"),
        (x64_dir, "WINCMD64.EXE"),
    ] {
        let base = root.join("build").join(arch_dir);
        if !can_run_host_pe(&base.join(shell_exe)) {
            continue;
        }
        let tag = arch_dir.replace('/', "_");
        let reg_k = format!("HKCU{BS}Software{BS}WinCmdHost_{tag}");
        let mut lines = vec![
            "@ECHO OFF".to_owned(),
            format!("{DQ}{}{DQ} /C", base.join("MEM.EXE").display()),
            format!(
                "{DQ}{}{DQ} /R {DQ}^be.*2${DQ} {DQ}{}{DQ}",
                base.join("FINDSTR.EXE").display(),
                tmp.join("a.txt").display()
            ),
            format!(
                "{DQ}{}{DQ} {DQ}{}{DQ}",
                base.join("SORT.EXE").display(),
                tmp.join("uns.txt").display()
            ),
            format!(
                "{DQ}{}{DQ} {DQ}{}{DQ} {DQ}{}{DQ} /E /MT:2",
                base.join("ROBOCOPY.EXE").display(),
                tmp.join("rb_src").display(),
                tmp.join(format!("rb_dst_{tag}")).display()
            ),
            format!(
                "{DQ}{}{DQ} ADD {reg_k} /v Val /t REG_SZ /d HelloReg",
                base.join("REG.EXE").display()
            ),
            format!(
                "{DQ}{}{DQ} QUERY {reg_k} /v Val",
                base.join("REG.EXE").display()
            ),
            format!(
                "{DQ}{}{DQ} DELETE {reg_k} /v Val",
                base.join("REG.EXE").display()
            ),
            format!("{DQ}{}{DQ}", base.join("HOSTNAME.EXE").display()),
            format!("{DQ}{}{DQ} /USER", base.join("WHOAMI.EXE").display()),
        ];
        for &exe in &ALL_19_UTILS {
            lines.push(format!("{DQ}{}{DQ} /? >NUL", base.join(exe).display()));
        }
        lines.push("ECHO M12_SUITE_DONE".to_owned());

        let bat_path = tmp.join(format!("run_{tag}.cmd"));
        let mut bat_body = lines.join(nl);
        bat_body.push_str(nl);
        fs::write(&bat_path, bat_body)?;

        let bat_arg = bat_path.to_string_lossy().into_owned();
        let (_, out) = run_host_pe(&base.join(shell_exe), &["/C", &bat_arg], b"")?;
        assert!(out.contains("Physical"), "{arch_dir} MEM: {out}");
        assert!(out.contains("beta2"), "{arch_dir} FINDSTR: {out}");
        assert!(
            out.contains("Files Copied : 2"),
            "{arch_dir} ROBOCOPY: {out}"
        );
        assert!(out.contains("HelloReg"), "{arch_dir} REG QUERY: {out}");
        assert!(out.contains("S-1-"), "{arch_dir} WHOAMI: {out}");
        assert!(
            out.contains("M12_SUITE_DONE"),
            "{arch_dir} suite completion: {out}"
        );
    }

    let _ = fs::remove_dir_all(&tmp);
    Ok(())
}
