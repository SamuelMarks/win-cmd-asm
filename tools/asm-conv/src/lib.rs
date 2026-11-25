//! Canonical multi-ISA assembly parser and multi-assembler lowering engine
//! (`NASM`, `FASM`, `JWasm`, `FASMARM`, `LLVM-MC`, and `ARMASM`).

pub mod ast;
pub mod lower;
pub mod parser;

use crate::ast::{BinaryFormat, CanonicalModule, ProcKind, TopItem};
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use wincmd_diag::{TargetAssembler, ToolError};

/// Parses canonical source text and lowers it to the requested [`TargetAssembler`].
///
/// # Errors
///
/// Returns [`ToolError`] if parsing or target lowering fails.
pub fn convert_source(
    file_path: &str,
    source: &str,
    target: TargetAssembler,
) -> Result<String, ToolError> {
    let module = parser::parse_canonical(file_path, source)?;
    lower::lower_module(&module, target)
}

/// Returns the list of [`TargetAssembler`] backends applicable to `module`.
#[must_use]
pub const fn applicable_targets(module: &CanonicalModule) -> &'static [TargetAssembler] {
    const ALL_TARGETS: &[TargetAssembler] = &[
        TargetAssembler::Nasm,
        TargetAssembler::Fasm,
        TargetAssembler::Jwasm,
        TargetAssembler::FasmArm,
        TargetAssembler::LlvmMc,
        TargetAssembler::ArmAsm,
    ];
    const X86_TARGETS: &[TargetAssembler] = &[
        TargetAssembler::Nasm,
        TargetAssembler::Fasm,
        TargetAssembler::Jwasm,
    ];
    const ARM_TARGETS: &[TargetAssembler] = &[
        TargetAssembler::FasmArm,
        TargetAssembler::LlvmMc,
        TargetAssembler::ArmAsm,
    ];

    match module.cpu {
        None => ALL_TARGETS,
        Some(isa) if isa.is_x86() => X86_TARGETS,
        Some(_) => ARM_TARGETS,
    }
}

/// Recursively walks `in_dir` (skipping any `tc` native toolchain subdirectory), parses every
/// canonical `.asm` and `.inc` file, and writes lowered outputs into `<out_dir>/<assembler>/...`.
///
/// Returns the total number of generated output files.
///
/// # Errors
///
/// Returns [`ToolError`] if directory traversal, file I/O, parsing, or lowering fails.
pub fn convert_tree(in_dir: &Path, out_dir: &Path) -> Result<usize, ToolError> {
    let mut files = Vec::new();
    collect_canonical_files(in_dir, in_dir, &mut files)?;
    files.sort();

    let mut parsed_map: BTreeMap<String, CanonicalModule> = BTreeMap::new();
    let mut ordered_rels: Vec<(PathBuf, String)> = Vec::new();

    for full_path in files {
        let rel = full_path
            .strip_prefix(in_dir)
            .map_err(|_| ToolError::CliUsage {
                message: format!("failed to strip prefix `{}`", in_dir.display()),
            })?
            .to_path_buf();
        let display_rel = normalize_slash_path(&rel);
        let content = fs::read_to_string(&full_path)?;
        let module = parser::parse_canonical(&display_rel, &content)?;
        parsed_map.insert(display_rel.clone(), module);
        ordered_rels.push((rel, display_rel));
    }

    let mut generated = 0_usize;
    for (rel, key) in &ordered_rels {
        let Some(module) = parsed_map.get(key) else {
            continue;
        };
        let ordered_frames = if module.format == BinaryFormat::Coff64 {
            let mut frames = Vec::new();
            let mut visited = BTreeSet::new();
            collect_win64_frames_ordered(key, module, &parsed_map, &mut visited, &mut frames);
            Some(frames)
        } else if matches!(
            module.format,
            BinaryFormat::ArmV4Pe | BinaryFormat::ArmV7Pe | BinaryFormat::Arm64Pe
        ) {
            let mut procs = Vec::new();
            let mut visited = BTreeSet::new();
            collect_arm_procs_ordered(key, module, &parsed_map, &mut visited, &mut procs);
            Some(procs)
        } else {
            None
        };

        let direct_pe_module = if matches!(
            module.format,
            BinaryFormat::Coff32
                | BinaryFormat::Coff64
                | BinaryFormat::ArmV4Pe
                | BinaryFormat::ArmV7Pe
                | BinaryFormat::Arm64Pe
        ) {
            Some(with_transitive_externs(key, module, &parsed_map))
        } else {
            None
        };

        for &target in applicable_targets(module) {
            let eff_mod = if target == TargetAssembler::FasmArm
                || (target == TargetAssembler::Fasm && module.format == BinaryFormat::Coff64)
            {
                direct_pe_module.as_ref().unwrap_or(module)
            } else {
                module
            };
            let lowered =
                lower::lower_module_with_frames(eff_mod, ordered_frames.as_deref(), target)?;
            let dst = out_dir.join(target.slug()).join(rel);
            if let Some(parent) = dst.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(&dst, lowered)?;
            generated += 1;
        }

        if key.starts_with("x86_32/") || key.starts_with("inc/") {
            let eff_mod = direct_pe_module.as_ref().unwrap_or(module);
            let lowered = lower::lower_module_fasm_pe32(eff_mod)?;
            let dst = out_dir.join("fasm-pe32").join(rel);
            if let Some(parent) = dst.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(&dst, lowered)?;
            generated += 1;
        }

        if key.starts_with("x86_64/") || key.starts_with("inc/") {
            let lowered = lower::lower_module_fasm_coff64(module, ordered_frames.as_deref())?;
            let dst = out_dir.join("fasm-coff").join(rel);
            if let Some(parent) = dst.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(&dst, lowered)?;
            generated += 1;
        }
    }

    Ok(generated)
}

fn with_transitive_externs(
    root_key: &str,
    module: &CanonicalModule,
    parsed_map: &BTreeMap<String, CanonicalModule>,
) -> CanonicalModule {
    let mut seen_syms = BTreeSet::new();
    for it in &module.items {
        if let TopItem::Extern { symbol, .. } = it {
            let _ = seen_syms.insert(symbol.clone());
        }
    }
    let mut inherited = Vec::new();
    let mut visited = BTreeSet::new();
    collect_externs_ordered(
        root_key,
        module,
        parsed_map,
        &mut visited,
        &mut seen_syms,
        &mut inherited,
    );
    if inherited.is_empty() {
        return module.clone();
    }
    let mut cloned = module.clone();
    cloned.items.extend(inherited);
    cloned
}

fn collect_externs_ordered(
    cur_key: &str,
    module: &CanonicalModule,
    parsed_map: &BTreeMap<String, CanonicalModule>,
    visited: &mut BTreeSet<String>,
    seen_syms: &mut BTreeSet<String>,
    out: &mut Vec<TopItem>,
) {
    if !visited.insert(cur_key.to_owned()) {
        return;
    }
    let parent_dir = Path::new(cur_key).parent().unwrap_or_else(|| Path::new(""));
    for item in &module.items {
        match item {
            TopItem::Include { path, .. } => {
                let joined = parent_dir.join(path);
                let inc_key = normalize_slash_path(&joined);
                if let Some(inc_mod) = parsed_map.get(&inc_key) {
                    collect_externs_ordered(&inc_key, inc_mod, parsed_map, visited, seen_syms, out);
                }
            }
            TopItem::Extern { symbol, .. } if seen_syms.insert(symbol.clone()) => {
                out.push(item.clone());
            }
            _ => {}
        }
    }
}

fn normalize_slash_path(p: &Path) -> String {
    let raw = p.to_string_lossy().replace(char::from(92_u8), "/");
    let mut parts: Vec<&str> = Vec::new();
    for seg in raw.split('/') {
        if seg.is_empty() || seg == "." {
            continue;
        }
        if seg == ".." {
            let _ = parts.pop();
        } else {
            parts.push(seg);
        }
    }
    parts.join("/")
}

fn collect_win64_frames_ordered(
    cur_key: &str,
    module: &CanonicalModule,
    parsed_map: &BTreeMap<String, CanonicalModule>,
    visited: &mut BTreeSet<String>,
    out: &mut Vec<(String, u32, Vec<String>)>,
) {
    if !visited.insert(cur_key.to_owned()) {
        return;
    }
    let parent_dir = Path::new(cur_key).parent().unwrap_or_else(|| Path::new(""));
    for item in &module.items {
        match item {
            TopItem::Include { path, .. } => {
                let joined = parent_dir.join(path);
                let inc_key = normalize_slash_path(&joined);
                if let Some(inc_mod) = parsed_map.get(&inc_key) {
                    collect_win64_frames_ordered(&inc_key, inc_mod, parsed_map, visited, out);
                }
            }
            TopItem::Proc {
                name,
                kind:
                    ProcKind::Win64Frame {
                        stack_alloc,
                        saved_regs,
                    },
                ..
            } => {
                out.push((name.clone(), *stack_alloc, saved_regs.clone()));
            }
            _ => {}
        }
    }
}

fn collect_arm_procs_ordered(
    cur_key: &str,
    module: &CanonicalModule,
    parsed_map: &BTreeMap<String, CanonicalModule>,
    visited: &mut BTreeSet<String>,
    out: &mut Vec<(String, u32, Vec<String>)>,
) {
    if !visited.insert(cur_key.to_owned()) {
        return;
    }
    let parent_dir = Path::new(cur_key).parent().unwrap_or_else(|| Path::new(""));
    for item in &module.items {
        match item {
            TopItem::Include { path, .. } => {
                let joined = parent_dir.join(path);
                let inc_key = normalize_slash_path(&joined);
                if let Some(inc_mod) = parsed_map.get(&inc_key) {
                    collect_arm_procs_ordered(&inc_key, inc_mod, parsed_map, visited, out);
                }
            }
            TopItem::Proc { name, .. } => {
                out.push((name.clone(), 0, Vec::new()));
            }
            _ => {}
        }
    }
}

fn collect_canonical_files(
    root: &Path,
    cur: &Path,
    out: &mut Vec<PathBuf>,
) -> Result<(), ToolError> {
    for entry_res in fs::read_dir(cur)? {
        let entry = entry_res?;
        let path = entry.path();
        if path.is_dir() {
            if path.strip_prefix(root).ok() == Some(Path::new("tc")) {
                continue;
            }
            collect_canonical_files(root, &path, out)?;
        } else if let Some(ext) = path.extension().and_then(|e| e.to_str())
            && (ext.eq_ignore_ascii_case("asm") || ext.eq_ignore_ascii_case("inc"))
        {
            out.push(path);
        }
    }
    Ok(())
}

/// Executes the `asm-conv` command-line interface.
///
/// Supported modes:
/// - `--in-dir <dir> --out-dir <dir>`
/// - `--in-file <file> --out-file <file> --target <assembler>`
///
/// # Errors
///
/// Returns [`ToolError`] on invalid CLI arguments, I/O errors, or lowering errors.
pub fn run_cli<I, S, W>(args: I, out: &mut W) -> Result<(), ToolError>
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
    W: Write,
{
    let collected: Vec<String> = args.into_iter().map(|s| s.as_ref().to_owned()).collect();
    let mut in_dir: Option<PathBuf> = None;
    let mut out_dir: Option<PathBuf> = None;
    let mut in_file: Option<PathBuf> = None;
    let mut out_file: Option<PathBuf> = None;
    let mut target: Option<TargetAssembler> = None;

    let mut idx = 1_usize;
    while idx < collected.len() {
        let flag = collected[idx].as_str();
        let val = collected.get(idx + 1).ok_or_else(|| ToolError::CliUsage {
            message: format!("missing value after `{flag}`"),
        })?;
        match flag {
            "--in-dir" => in_dir = Some(PathBuf::from(val)),
            "--out-dir" => out_dir = Some(PathBuf::from(val)),
            "--in-file" => in_file = Some(PathBuf::from(val)),
            "--out-file" => out_file = Some(PathBuf::from(val)),
            "--target" => {
                target = Some(
                    TargetAssembler::parse(val).ok_or_else(|| ToolError::CliUsage {
                        message: format!("unknown target assembler `{val}`"),
                    })?,
                );
            }
            _ => {
                return Err(ToolError::CliUsage {
                    message: format!("unrecognized flag `{flag}`"),
                });
            }
        }
        idx += 2;
    }

    if let (Some(id), Some(od)) = (in_dir, out_dir) {
        let count = convert_tree(&id, &od)?;
        writeln!(out, "asm-conv: generated {count} files in {}", od.display())?;
        return Ok(());
    }

    if let (Some(inf), Some(outf), Some(tgt)) = (in_file, out_file, target) {
        let src = fs::read_to_string(&inf)?;
        let lowered = convert_source(&inf.to_string_lossy(), &src, tgt)?;
        if let Some(parent) = outf.parent()
            && !parent.as_os_str().is_empty()
        {
            fs::create_dir_all(parent)?;
        }
        fs::write(&outf, lowered)?;
        writeln!(
            out,
            "asm-conv: lowered {} -> {} ({tgt})",
            inf.display(),
            outf.display()
        )?;
        return Ok(());
    }

    Err(ToolError::CliUsage {
        message:
            "expected `--in-dir <d> --out-dir <d>` or `--in-file <f> --out-file <f> --target <t>`"
                .into(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ast::{BinaryFormat, DataWidth, SectionKind};

    const SAMPLE_INC: &str = r"
;; @file err_code.inc
;; @isa all
;; @desc Canonical error constants
;;   with multi-line continuation.

;; @const ERR_OK
@const ERR_OK = 0x0000

;; @struc MCB
;; @desc DOS Memory Control Block
@struc MCB, 16
    @field bType, byte, 1
    @field wOwner, word, 1
    @field wSize, word, 1
    @field bReserved, byte, 3
    @field szName, byte, 8
@endstruc
";

    const SAMPLE_COM3: &str = r#"
;; @file wincmd.asm
;; @isa 8086
;; @desc 3-part DOS shell skeleton.

@cpu 8086
@format com3part
@entry res_start
@include "inc/err_code.inc"

@section resident
; Resident entry stub
res_start:
    mov ax, word [cs:saved_sp]
    mov si, addr(banner_msg)
    retf
@data dw saved_sp, 0x1234
@data db -, 'A', 0
@res byte res_scratch, 4
@align 4

@section transient
;; @proc trn_main
;; @abi dos16
;; @in none
;; @out AX=0
;; @err none
;; @clobbers ax
;; @desc Transient entry.
@proc trn_main
    ; clear AX
    xor ax, ax
.done:
    jz .done
    rep movsb
    ret
@endproc

;; @proc far_helper
;; @desc Far helper
@proc_far far_helper
    retf
@endproc

@section init
banner_msg:
    ret
"#;

    const SAMPLE_X64: &str = r"
;; @file w64_entry.asm
;; @isa x64
;; @desc Win64 framed entry.

@cpu x64
@format coff64
@global w64_entry
@extern ExitProcess, proc
@extern __imp_GetStdHandle, qword

@section rdata
@data dq k_magic, 0x0102030405060708
@section data
@data dd g_flag, 1
@section bss
@res qword g_handle, 2
@res dword g_cnt, 1
@res word g_w, 1
@res byte g_b, 1

@section text
@proc_frame w64_entry, 40, rbx, rsi
    mov rcx, [rel g_flag]
    call qword [rel __imp_GetStdHandle]
    @epilog_ret
@endproc

@proc_frame big_frame, 256, rbp, rdi, r12, r13, r14, r15
    @epilog_ret
@endproc

@proc leaf_x64
    xor eax, eax
    ret
@endproc
";

    #[test]
    fn lowers_inc_across_all_six_assemblers() -> Result<(), ToolError> {
        let m = parser::parse_canonical("err_code.inc", SAMPLE_INC)?;
        assert_eq!(m.file_doc.get("file"), Some("err_code.inc"));
        assert!(!m.file_doc.is_empty());
        assert_eq!(applicable_targets(&m).len(), 6);
        for &t in applicable_targets(&m) {
            let out = lower::lower_module(&m, t)?;
            assert!(out.contains("ERR_OK"));
            assert!(out.contains("MCB"));
        }
        Ok(())
    }

    #[test]
    fn lowers_com3part_com_bin16_coff32_and_x64() -> Result<(), ToolError> {
        for tgt in [
            TargetAssembler::Nasm,
            TargetAssembler::Fasm,
            TargetAssembler::Jwasm,
        ] {
            let c3 = convert_source("wincmd.asm", SAMPLE_COM3, tgt)?;
            assert!(c3.contains("RES_PARAS"));
            assert!(c3.contains("TRN_PARAS"));
            assert!(c3.contains("INIT_PARAS"));

            let x64 = convert_source("w64.asm", SAMPLE_X64, tgt)?;
            assert!(x64.contains("w64_entry"));
            if tgt == TargetAssembler::Nasm {
                assert!(x64.contains("%if __NASM_MAJOR__ >= 3"));
                assert!(x64.contains("GLOBAL w64_entry:function"));
                assert!(x64.contains("GLOBAL w64_entry\n%endif"));
            }
        }

        let com286 = ";; @file a.asm
@cpu 286
@format com
@entry s
@section text
s:
 @align16_zero
 ret
";
        let com_no_ent = ";; @file a.asm
@cpu 8086
@format com
s:
 ret
";
        let c3_no_ent = ";; @file a.asm
@cpu 8086
@format com3part
@section resident
s:
 ret
";
        let bin16 = ";; @file b.asm
@cpu 286
@format bin16
s:
 @res word w, 2
 @res dword d, 1
 @res qword q, 1
 ret
";
        let coff32 = ";; @file c.asm
@cpu 386
@format coff32
@safeseh _seh
@global _seh
@global f
@extern imp, dword
@section text
@proc _seh
 mov eax, 1
 ret
@endproc
@proc f
 mov eax, dword [esp + 4]
 mov al, byte [ds:ebx]
 mov eax, dword [fs:0]
 ret
@endproc
";
        for src in [com286, com_no_ent, c3_no_ent, bin16, coff32] {
            for tgt in [
                TargetAssembler::Nasm,
                TargetAssembler::Fasm,
                TargetAssembler::Jwasm,
            ] {
                let lowered = convert_source("t.asm", src, tgt)?;
                assert_ne!(lowered, "");
            }
        }
        Ok(())
    }

    #[test]
    fn lowers_armv4_armv7_and_arm64_across_all_arm_assemblers() -> Result<(), ToolError> {
        let v4 = ";; @file v4.asm
@cpu armv4
@format armv4_pe
@global start
@extern CoreFn, proc
@section rdata
@data db msg, 'X', 0
@data dw hw, 1
@section data
@data dd v, 2
@section bss
@res dword b, 2
@section text
@proc start
@align 4
 mov r0, #0
.lp:
 b .lp
 bx lr
@endproc
";
        let v7 = ";; @file v7.asm
@cpu armv7
@format armv7_pe
@entry start
@global start
@section bss
@res byte b, 4
@section text
@proc start
 movs r0, #0
 bx lr
@endproc
";
        let a64 = ";; @file a64.asm
@cpu arm64
@format arm64_pe
@entry start
@global start
@section rdata
@data dq qv, 9
@data dd -, 0
@section text
glbl:
@proc start
 mov w0, #0
 ret
@endproc
";

        for src in [v4, v7, a64] {
            let m = parser::parse_canonical("arm.asm", src)?;
            assert_eq!(applicable_targets(&m).len(), 3);
            for &tgt in applicable_targets(&m) {
                let out = lower::lower_module(&m, tgt)?;
                assert_ne!(out, "");
            }
        }
        Ok(())
    }

    #[test]
    fn parser_rejects_malformed_directives() {
        let bad_cases = [
            "@cpu unknown_isa",
            "@format bogus_fmt",
            "@entry",
            "@safeseh",
            "@include unquoted.inc",
            r#"@include """#,
            "@section bogus_sec",
            "@global",
            "@extern only_one",
            "@const NO_EQUALS",
            "@const = 1",
            "@struc ONLY_NAME",
            "@struc BAD_SUM, 4
@field a, byte, 2
@endstruc",
            "@struc UNCLOSED, 1
@field a, byte, 1",
            "@struc BAD_DIRECTIVE, 1
mov ax, bx
@endstruc",
            "@struc BAD_FIELD_ARGS, 1
@field a, byte
@endstruc",
            "@struc BAD_FIELD_WIDTH, 1
@field a, octoword, 1
@endstruc",
            "@struc ZERO_COUNT, 0
@field a, byte, 0
@endstruc",
            "@struc BAD_INT, xyz
@endstruc",
            "@proc",
            "@proc unclosed
ret",
            "@proc_frame only_name",
            "@proc_frame odd_stack, 12
@endproc",
            "@proc_frame huge_stack, 999999999999
@endproc",
            "@align 3",
            "@data badw x, 1",
            "@data db x",
            "@data db , 1",
            "@res badw x, 1",
            "@res byte x",
            "@bogus_dir 123",
            ".local_outside_proc:",
            "bad label:",
        ];
        for src in bad_cases {
            assert!(
                parser::parse_canonical("bad.asm", src).is_err(),
                "expected err for: {src}"
            );
        }
    }

    #[test]
    fn lowerer_rejects_incompatible_targets_and_helpers_preserve_quotes() -> Result<(), ToolError> {
        let doc_only = parser::parse_canonical(
            "doc.inc",
            ";; @file doc.inc
",
        )?;
        assert_eq!(doc_only.file_doc.get("file"), Some("doc.inc"));

        let no_cpu_com = parser::parse_canonical(
            "x.asm",
            "@format com
s:
ret",
        )?;
        assert!(lower::lower_module(&no_cpu_com, TargetAssembler::Nasm).is_err());

        let x86_mod = parser::parse_canonical(
            "x.asm",
            "@cpu 8086
@format com
s:
ret",
        )?;
        assert!(lower::lower_module(&x86_mod, TargetAssembler::LlvmMc).is_err());

        let arm_com = parser::parse_canonical(
            "x.asm",
            "@cpu arm64
@format com
s:
ret",
        )?;
        assert!(lower::lower_module(&arm_com, TargetAssembler::LlvmMc).is_err());

        let bad_sec = parser::parse_canonical(
            "x.asm",
            "@cpu 8086
@format com
@section resident
",
        )?;
        assert!(lower::lower_module(&bad_sec, TargetAssembler::Nasm).is_err());

        let frame_on_8086 = parser::parse_canonical(
            "x.asm",
            "@cpu 8086
@format com
@proc_frame f, 8
@epilog_ret
@endproc",
        )?;
        assert!(lower::lower_module(&frame_on_8086, TargetAssembler::Nasm).is_err());

        let epilog_outside = parser::parse_canonical(
            "x.asm",
            "@cpu x64
@format coff64
@proc f
@epilog_ret
@endproc",
        )?;
        assert!(lower::lower_module(&epilog_outside, TargetAssembler::Nasm).is_err());

        assert_eq!(BinaryFormat::parse("nope"), None);
        assert_eq!(SectionKind::parse("nope"), None);
        assert_eq!(DataWidth::parse("nope"), None);
        let all_regs = ";; @file r.asm
@cpu x64
@format coff64
@section text
@proc_frame r, 8, rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13, r14, r15, xmm0
@epilog_ret
@endproc
";
        assert!(convert_source("r.asm", all_regs, TargetAssembler::Nasm).is_ok());
        assert_eq!(
            parser::strip_inline_comment("mov al, ';' ; real comment"),
            "mov al, ';'"
        );
        assert_eq!(
            parser::strip_inline_comment(r#"@data db s, "a;b", 0 ; c"#),
            r#"@data db s, "a;b", 0"#
        );
        assert_eq!(
            parser::split_operands(r#"a, (b, c), [d, e], 'x,y', "z,w""#).len(),
            5
        );
        assert_eq!(
            lower::rewrite_expr_tokens(r#"'0x10' + "0x20" + 0x30"#, None, TargetAssembler::Jwasm),
            r#"'0x10' + "0x20" + 030h"#
        );
        Ok(())
    }

    #[test]
    fn cli_and_tree_conversion_roundtrip() -> Result<(), ToolError> {
        let tmp = std::env::temp_dir().join("wincmd_asm_conv_unit_test");
        let _ = fs::remove_dir_all(&tmp);
        let in_dir = tmp.join("in");
        let out_dir = tmp.join("out");
        fs::create_dir_all(in_dir.join("inc"))?;
        fs::create_dir_all(in_dir.join("tc"))?;
        fs::create_dir_all(in_dir.join("dos16"))?;

        fs::write(in_dir.join("inc/err.inc"), SAMPLE_INC)?;
        fs::write(in_dir.join("tc/ignored.inc"), "@invalid_not_parsed")?;
        fs::write(in_dir.join("dos16/readme.txt"), "ignored")?;
        fs::write(in_dir.join("dos16/wincmd.asm"), SAMPLE_COM3)?;

        let mut buf = Vec::new();
        run_cli(
            [
                "asm-conv",
                "--in-dir",
                &in_dir.to_string_lossy(),
                "--out-dir",
                &out_dir.to_string_lossy(),
            ],
            &mut buf,
        )?;
        assert!(out_dir.join("nasm/dos16/wincmd.asm").is_file());
        assert!(out_dir.join("fasm/dos16/wincmd.asm").is_file());
        assert!(out_dir.join("jwasm/dos16/wincmd.asm").is_file());
        assert!(!out_dir.join("nasm/tc/ignored.inc").exists());

        let single_out = tmp.join("single/wincmd_nasm.asm");
        run_cli(
            [
                "asm-conv",
                "--in-file",
                &in_dir.join("dos16/wincmd.asm").to_string_lossy(),
                "--out-file",
                &single_out.to_string_lossy(),
                "--target",
                "nasm",
            ],
            &mut buf,
        )?;
        assert!(single_out.is_file());

        // Error CLI invocations
        assert!(run_cli(["asm-conv"], &mut buf).is_err());
        assert!(run_cli(["asm-conv", "--in-dir"], &mut buf).is_err());
        assert!(run_cli(["asm-conv", "--bogus", "1"], &mut buf).is_err());
        assert!(run_cli(["asm-conv", "--target", "bogus"], &mut buf).is_err());

        let _ = fs::remove_dir_all(&tmp);
        Ok(())
    }
}
