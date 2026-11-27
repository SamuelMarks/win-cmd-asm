//! Lossless multi-assembler lowering engine (`CanonicalModule` -> `NASM`, `FASM`, `JWasm`,
//! `FASMARM`, `LLVM-MC`, and `ARMASM`).

use crate::ast::{
    BinaryFormat, CanonicalModule, DataWidth, DocBlock, ProcKind, SectionKind, Stmt, StrucField,
    TopItem,
};
use core::fmt::Write as _;
use wincmd_diag::{CpuIsa, SourceSpan, TargetAssembler, ToolError};

/// Single-quote ASCII codepoint (`'`).
const SQUOTE: char = 39 as char;
/// Line-feed ASCII codepoint.
const LF: char = 10 as char;

struct ProcEmitReq<'a> {
    module: &'a CanonicalModule,
    name: &'a str,
    kind: &'a ProcKind,
    body: &'a [Stmt],
    active_seg_base: &'a str,
    target: TargetAssembler,
    span: &'a SourceSpan,
}

/// Lowers a parsed [`CanonicalModule`] into native source text for `target`.
///
/// # Errors
///
/// Returns [`ToolError::Lowering`] if the module's `@cpu` or `@format` is incompatible
/// with `target`, or if a construct cannot be represented cleanly.
pub fn lower_module(
    module: &CanonicalModule,
    target: TargetAssembler,
) -> Result<String, ToolError> {
    lower_module_with_frames(module, None, target)
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum FasmPeMode {
    Default,
    DirectPe32,
    Coff64,
}

/// Lowers `module` into native assembler syntax, optionally using a pre-collected list of
/// `Win64Frame` procedures (including transitively `@include`d modules in exact `.text` order).
///
/// # Errors
///
/// Returns [`ToolError::Lowering`] if the module's `@cpu` or `@format` is incompatible
/// with `target`, or if a construct cannot be represented cleanly.
pub fn lower_module_with_frames(
    module: &CanonicalModule,
    ordered_win64_frames: Option<&[(String, u32, Vec<String>)]>,
    target: TargetAssembler,
) -> Result<String, ToolError> {
    lower_module_inner(module, ordered_win64_frames, target, FasmPeMode::Default)
}

/// Lowers an `IA-32` (`@format coff32` or `@format inc`) module into linker-free FASM direct `PE32`
/// (`format PE console 3.10 NX`) syntax.
///
/// # Errors
///
/// Returns [`ToolError::Lowering`] if `module` cannot be lowered to FASM `PE32`.
pub fn lower_module_fasm_pe32(module: &CanonicalModule) -> Result<String, ToolError> {
    lower_module_inner(module, None, TargetAssembler::Fasm, FasmPeMode::DirectPe32)
}

/// Lowers an `x86-64` (`@format coff64` or `@format inc`) module into FASM `MS64 COFF` object syntax
/// suitable for linking via `lld-link /BREPRO /NODEFAULTLIB`.
///
/// # Errors
///
/// Returns [`ToolError::Lowering`] if `module` cannot be lowered to FASM `MS64 COFF`.
pub fn lower_module_fasm_coff64(
    module: &CanonicalModule,
    ordered_win64_frames: Option<&[(String, u32, Vec<String>)]>,
) -> Result<String, ToolError> {
    lower_module_inner(
        module,
        ordered_win64_frames,
        TargetAssembler::Fasm,
        FasmPeMode::Coff64,
    )
}

fn lower_module_inner(
    module: &CanonicalModule,
    ordered_win64_frames: Option<&[(String, u32, Vec<String>)]>,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) -> Result<String, ToolError> {
    let file_span = SourceSpan::new(&module.file_path, 1, 1);
    if let Some(isa) = module.cpu {
        if !target.supports_isa(isa) {
            return Err(ToolError::Lowering {
                span: file_span,
                assembler: target,
                message: format!("assembler `{target}` does not target CPU ISA `{isa}`"),
            });
        }
    } else if module.format != BinaryFormat::Include {
        return Err(ToolError::Lowering {
            span: file_span,
            assembler: target,
            message: "executable/object module must declare `@cpu`".into(),
        });
    }

    let mut out = String::with_capacity(4096);
    emit_doc_block(&mut out, &module.file_doc, target);
    emit_module_preamble(&mut out, module, target, fasm_mode)?;

    let mut sec_state = SectionState {
        active: None,
        seg_base: String::from("wincmd_seg_base"),
        armnt_idx: 0,
        armnt_prev_kind: None,
        emitted_load_config: false,
        emitted_cfg_nop: false,
        emitted_gs_cookie: false,
        fasm_mode,
    };
    let mut local_frames: Vec<(String, u32, Vec<String>)> = Vec::new();
    lower_module_items(&mut out, module, &mut sec_state, &mut local_frames, target)?;

    let frames_slice = ordered_win64_frames.unwrap_or(&local_frames);
    finalize_module_sections(&mut out, module, &mut sec_state, frames_slice, target);
    emit_module_trailer(&mut out, module, target);

    Ok(out)
}

fn lower_module_items(
    out: &mut String,
    module: &CanonicalModule,
    sec_state: &mut SectionState,
    win64_frames: &mut Vec<(String, u32, Vec<String>)>,
    target: TargetAssembler,
) -> Result<(), ToolError> {
    for item in &module.items {
        match item {
            TopItem::Include { path, .. } => emit_include(out, path, target),
            TopItem::Section { kind, span } => {
                switch_module_section(out, sec_state, module, *kind, target, span)?;
            }
            TopItem::Global { symbol, .. } => {
                emit_global(out, symbol, module, target, sec_state.fasm_mode);
            }
            TopItem::Extern { symbol, kind, .. } => {
                emit_extern(out, module, symbol, kind, target, sec_state.fasm_mode);
            }
            TopItem::Const {
                doc, name, expr, ..
            } => {
                emit_doc_block(out, doc, target);
                emit_const(out, name, expr, target);
            }
            TopItem::Struc {
                doc,
                name,
                expected_size,
                fields,
                ..
            } => {
                emit_doc_block(out, doc, target);
                emit_struc(out, name, *expected_size, fields, target);
            }
            TopItem::Proc {
                doc,
                name,
                kind,
                body,
                span,
            } => {
                emit_doc_block(out, doc, target);
                if let ProcKind::Win64Frame {
                    stack_alloc,
                    saved_regs,
                } = kind
                {
                    win64_frames.push((name.clone(), *stack_alloc, saved_regs.clone()));
                } else if matches!(
                    module.format,
                    BinaryFormat::ArmV4Pe | BinaryFormat::ArmV7Pe | BinaryFormat::Arm64Pe
                ) {
                    win64_frames.push((name.clone(), 0, Vec::new()));
                }
                emit_proc(
                    out,
                    &ProcEmitReq {
                        module,
                        name,
                        kind,
                        body,
                        active_seg_base: &sec_state.seg_base,
                        target,
                        span,
                    },
                )?;
            }
            TopItem::Stmt(stmt) => {
                emit_stmt(out, module, None, None, stmt, &sec_state.seg_base, target)?;
            }
        }
    }
    Ok(())
}

struct SectionState {
    active: Option<SectionKind>,
    seg_base: String,
    armnt_idx: usize,
    armnt_prev_kind: Option<SectionKind>,
    emitted_load_config: bool,
    emitted_cfg_nop: bool,
    emitted_gs_cookie: bool,
    fasm_mode: FasmPeMode,
}

fn emit_coff32_load_config(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) {
    match target {
        TargetAssembler::Fasm if fasm_mode == FasmPeMode::DirectPe32 => {
            if let Some(seh) = module.safeseh.as_deref() {
                let _ = writeln!(
                    out,
                    "data 10{LF}__load_config_used:{LF}    dd 72, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}    dd ___safe_se_handler_table, 1{LF}end data{LF}align 4{LF}___safe_se_handler_table:{LF}    dd rva {seh}"
                );
            }
        }
        TargetAssembler::Nasm | TargetAssembler::Fasm => {
            let _ = writeln!(
                out,
                "__load_config_used:{LF}    dd 72, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}    dd ___safe_se_handler_table, ___safe_se_handler_count"
            );
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(
                out,
                "__load_config_used DD 72, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, OFFSET ___safe_se_handler_table, ___safe_se_handler_count"
            );
        }
        _ => {}
    }
}

fn emit_coff64_cfg_nop(out: &mut String, target: TargetAssembler) {
    match target {
        TargetAssembler::Nasm | TargetAssembler::Fasm => {
            let _ = writeln!(
                out,
                "__chkstk:{LF}    push rcx{LF}    push rax{LF}    lea rcx, [rsp + 24]{LF}__chkstk_lp:{LF}    cmp rax, 0x1000{LF}    jb __chkstk_tail{LF}    sub rcx, 0x1000{LF}    test byte [rcx], cl{LF}    sub rax, 0x1000{LF}    jmp __chkstk_lp{LF}__chkstk_tail:{LF}    sub rcx, rax{LF}    test byte [rcx], cl{LF}    pop rax{LF}    pop rcx{LF}    ret{LF}__wincmd_cfg_nop:{LF}    db 0xF3, 0x0F, 0x1E, 0xFA{LF}    ret"
            );
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(
                out,
                "__chkstk PROC{LF}    push rcx{LF}    push rax{LF}    lea rcx, [rsp + 24]{LF}__chkstk_lp:{LF}    cmp rax, 01000h{LF}    jb __chkstk_tail{LF}    sub rcx, 01000h{LF}    test BYTE PTR [rcx], cl{LF}    sub rax, 01000h{LF}    jmp __chkstk_lp{LF}__chkstk_tail:{LF}    sub rcx, rax{LF}    test BYTE PTR [rcx], cl{LF}    pop rax{LF}    pop rcx{LF}    ret{LF}__chkstk ENDP{LF}__wincmd_cfg_nop PROC{LF}    DB 0F3h, 0Fh, 1Eh, 0FAh{LF}    ret{LF}__wincmd_cfg_nop ENDP"
            );
        }
        TargetAssembler::FasmArm | TargetAssembler::LlvmMc | TargetAssembler::ArmAsm => {}
    }
}

fn collect_coff64_gfids(module: &CanonicalModule) -> Vec<&str> {
    module
        .items
        .iter()
        .filter_map(|it| {
            if let TopItem::Global { symbol, .. } = it {
                Some(symbol.as_str())
            } else {
                None
            }
        })
        .collect()
}

fn emit_coff64_load_config(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) {
    let globals = collect_coff64_gfids(module);
    let gfids_cnt = globals.len() + 1;
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(
                out,
                "_load_config_used:{LF}    dd 256, 0, 0, 0, 0, 0{LF}    dq 0, 0, 0, 0, 0, 0{LF}    dd 0, 0{LF}    dq 0, 0, 0, 0{LF}    dq __wincmd_cfg_nop_ptr{LF}    dq 0{LF}    dq __wincmd_gfids_table{LF}    dq {gfids_cnt}{LF}    dd 0x00000500, 0, 0, 0{LF}    dq 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}__wincmd_cfg_nop_ptr:{LF}    dq __wincmd_cfg_nop{LF}    align 4, db 0{LF}__wincmd_gfids_table:"
            );
            for sym in globals
                .iter()
                .copied()
                .chain(std::iter::once("__wincmd_cfg_nop"))
            {
                let _ = writeln!(out, "    dd {sym} wrt ..imagebase");
            }
        }
        TargetAssembler::Fasm if fasm_mode == FasmPeMode::Coff64 => {
            let _ = writeln!(
                out,
                "section {SQUOTE}.rdata{SQUOTE} data readable align 8{LF}_load_config_used:{LF}    dd 256, 0, 0, 0, 0, 0{LF}    dq 0, 0, 0, 0, 0, 0{LF}    dd 0, 0{LF}    dq 0, 0, 0, 0{LF}    dq __wincmd_cfg_nop_ptr{LF}    dq 0{LF}    dq __guard_fids_table{LF}    dq __guard_fids_count{LF}    dd __guard_flags{LF}    dd 0, 0, 0{LF}    dq 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}__wincmd_cfg_nop_ptr:{LF}    dq __wincmd_cfg_nop"
            );
        }
        TargetAssembler::Fasm => {
            let _ = writeln!(
                out,
                "data 10{LF}_load_config_used:{LF}    dd 256, 0, 0, 0, 0, 0{LF}    dq 0, 0, 0, 0, 0, 0{LF}    dd 0, 0{LF}    dq 0, 0, 0, 0{LF}    dq __wincmd_cfg_nop_ptr{LF}    dq 0{LF}    dq __wincmd_gfids_table{LF}    dq {gfids_cnt}{LF}    dd 0x00000500, 0, 0, 0{LF}    dq 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}end data{LF}__wincmd_cfg_nop_ptr dq __wincmd_cfg_nop{LF}align 4{LF}__wincmd_gfids_table:"
            );
            let mut prev: Option<&str> = None;
            for sym in globals
                .iter()
                .copied()
                .chain(std::iter::once("__wincmd_cfg_nop"))
            {
                let _ = writeln!(out, "    dd rva {sym}");
                if let Some(p) = prev {
                    let _ = writeln!(out, "    assert rva {p} < rva {sym}");
                }
                prev = Some(sym);
            }
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(
                out,
                "_load_config_used DD 256, 0, 0, 0, 0, 0{LF}    DQ 0, 0, 0, 0, 0, 0{LF}    DD 0, 0{LF}    DQ 0, 0, 0, 0{LF}    DQ __wincmd_cfg_nop_ptr{LF}    DQ 0{LF}    DQ OFFSET __guard_fids_table{LF}    DQ __guard_fids_count{LF}    DD __guard_flags{LF}    DD 0, 0, 0{LF}    DQ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}__wincmd_cfg_nop_ptr DQ __wincmd_cfg_nop"
            );
        }
        TargetAssembler::FasmArm | TargetAssembler::LlvmMc | TargetAssembler::ArmAsm => {}
    }
}

fn emit_fasm_pe64_rsrc_and_reloc(out: &mut String) {
    const MANIFEST_XML: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application><supportedOS Id="{e2011457-1546-43c5-a5fe-008deee3d3f0}"/><supportedOS Id="{35138b9a-5d96-4fbd-8e2d-a2440225f93a}"/><supportedOS Id="{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}"/><supportedOS Id="{1f676c76-80e1-4239-95bb-83d0f6d0da78}"/><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application></compatibility><application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings><longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware></windowsSettings></application></assembly>"#;
    let _ = writeln!(
        out,
        "section {SQUOTE}.rsrc{SQUOTE} resource data readable{LF}    dd 0, 0, 0{LF}    dw 0, 1{LF}    dd 24, 0x80000018{LF}    dd 0, 0, 0{LF}    dw 0, 1{LF}    dd 1, 0x80000030{LF}    dd 0, 0, 0{LF}    dw 0, 1{LF}    dd 0x0409, 0x00000048{LF}    dd rva __wincmd_manifest_xml, __wincmd_manifest_end - __wincmd_manifest_xml, 65001, 0{LF}__wincmd_manifest_xml:{LF}    db {SQUOTE}{MANIFEST_XML}{SQUOTE}{LF}__wincmd_manifest_end:{LF}section {SQUOTE}.reloc{SQUOTE} fixups data readable discardable"
    );
}

fn emit_arm_cfg_and_chkstk(out: &mut String, fmt: BinaryFormat, target: TargetAssembler) {
    match (fmt, target) {
        (BinaryFormat::ArmV7Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "__chkstk:{LF}    push {{r5, r6}}{LF}    lsl r5, r4, #2{LF}    mov r6, sp{LF}    add r6, r6, #8{LF}__chkstk_lp:{LF}    cmp r5, #0x1000{LF}    bcc __chkstk_tail{LF}    sub r6, r6, #0x1000{LF}    ldr r12, [r6]{LF}    sub r5, r5, #0x1000{LF}    b __chkstk_lp{LF}__chkstk_tail:{LF}    sub r6, r6, r5{LF}    ldr r12, [r6]{LF}    pop {{r5, r6}}{LF}    bx lr{LF}__chkstk__end:{LF}__wincmd_cfg_nop:{LF}    bx lr{LF}__wincmd_cfg_nop__end:"
            );
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                ".global __chkstk{LF}.thumb_func{LF}__chkstk:{LF}    push {{r5, r6}}{LF}    lsl r5, r4, #2{LF}    mov r6, sp{LF}    add r6, r6, #8{LF}.L__chkstk_lp:{LF}    cmp r5, #0x1000{LF}    bcc .L__chkstk_tail{LF}    sub r6, r6, #0x1000{LF}    ldr r12, [r6]{LF}    sub r5, r5, #0x1000{LF}    b .L__chkstk_lp{LF}.L__chkstk_tail:{LF}    sub r6, r6, r5{LF}    ldr r12, [r6]{LF}    pop {{r5, r6}}{LF}    bx lr{LF}.L__chkstk__end:{LF}.thumb_func{LF}__wincmd_cfg_nop:{LF}    bx lr{LF}.L__wincmd_cfg_nop__end:"
            );
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "__chkstk:{LF}    lsl x16, x15, #4{LF}    mov x17, sp{LF}__chkstk_lp:{LF}    cmp x16, #0x1000{LF}    b.lo __chkstk_tail{LF}    sub x17, x17, #0x1000{LF}    ldr wzr, [x17]{LF}    sub x16, x16, #0x1000{LF}    b __chkstk_lp{LF}__chkstk_tail:{LF}    sub x17, x17, x16{LF}    ldr wzr, [x17]{LF}    ret{LF}__chkstk__end:{LF}__wincmd_cfg_nop:{LF}    hint #34{LF}    ret{LF}__wincmd_cfg_nop__end:"
            );
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                ".global __chkstk{LF}__chkstk:{LF}    lsl x16, x15, #4{LF}    mov x17, sp{LF}.L__chkstk_lp:{LF}    cmp x16, #0x1000{LF}    b.lo .L__chkstk_tail{LF}    sub x17, x17, #0x1000{LF}    ldr wzr, [x17]{LF}    sub x16, x16, #0x1000{LF}    b .L__chkstk_lp{LF}.L__chkstk_tail:{LF}    sub x17, x17, x16{LF}    ldr wzr, [x17]{LF}    ret{LF}.L__chkstk__end:{LF}__wincmd_cfg_nop:{LF}    hint #34{LF}    ret{LF}.L__wincmd_cfg_nop__end:"
            );
        }
        _ => {}
    }
}

fn emit_arm_load_config(out: &mut String, module: &CanonicalModule, target: TargetAssembler) {
    let globals = collect_coff64_gfids(module);
    let gfids_cnt = globals.len() + 1;
    match (module.format, target) {
        (BinaryFormat::ArmV7Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "align 4{LF}__armnt_loadcfg_rva = $ - 0x00400000{LF}_load_config_used:{LF}    dw 160{LF}    times 17 dw 0{LF}    dw __wincmd_cfg_nop_ptr{LF}    dw 0{LF}    dw __wincmd_gfids_table{LF}    dw {gfids_cnt}{LF}    dw 0x00000500{LF}    times 17 dw 0{LF}__wincmd_cfg_nop_ptr:{LF}    dw (__wincmd_cfg_nop or 1){LF}__wincmd_gfids_table:"
            );
            for sym in globals
                .iter()
                .copied()
                .chain(std::iter::once("__wincmd_cfg_nop"))
            {
                let _ = writeln!(out, "    dw (({sym} - 0x00400000) and 0xFFFFFFFE)");
            }
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                ".p2align 2{LF}_load_config_used:{LF}    .long 160{LF}    .long 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}    .long __wincmd_cfg_nop_ptr{LF}    .long 0{LF}    .long __guard_fids_table{LF}    .long __guard_fids_count{LF}    .long __guard_flags{LF}    .long 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}__wincmd_cfg_nop_ptr:{LF}    .long __wincmd_cfg_nop"
            );
            emit_llvm_gfids_section(out, &globals, BinaryFormat::ArmV7Pe);
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "align 8{LF}data 10{LF}_load_config_used:{LF}    dw 256, 0, 0, 0, 0, 0{LF}    dd 0, 0, 0, 0, 0, 0{LF}    dw 0, 0{LF}    dd 0, __wincmd_gs_cookie, 0, 0{LF}    dd __wincmd_cfg_nop_ptr{LF}    dd 0{LF}    dd __wincmd_gfids_table{LF}    dd {gfids_cnt}{LF}    dw 0x00000500, 0, 0, 0{LF}    dd 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}end data{LF}__wincmd_cfg_nop_ptr dd __wincmd_cfg_nop{LF}align 4{LF}__wincmd_gfids_table:"
            );
            for sym in globals
                .iter()
                .copied()
                .chain(std::iter::once("__wincmd_cfg_nop"))
            {
                let _ = writeln!(out, "    dw rva {sym}");
            }
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                ".p2align 3{LF}_load_config_used:{LF}    .long 256, 0, 0, 0, 0, 0{LF}    .quad 0, 0, 0, 0, 0, 0{LF}    .long 0, 0{LF}    .quad 0, __wincmd_gs_cookie, 0, 0{LF}    .quad __wincmd_cfg_nop_ptr{LF}    .quad 0{LF}    .quad __guard_fids_table{LF}    .quad __guard_fids_count{LF}    .long __guard_flags, 0, 0, 0{LF}    .quad 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0{LF}__wincmd_cfg_nop_ptr:{LF}    .quad __wincmd_cfg_nop"
            );
            emit_llvm_gfids_section(out, &globals, BinaryFormat::Arm64Pe);
        }
        _ => {}
    }
}

fn emit_llvm_gfids_section(out: &mut String, globals: &[&str], fmt: BinaryFormat) {
    for sym in globals
        .iter()
        .copied()
        .chain(std::iter::once("__wincmd_cfg_nop"))
    {
        let _ = writeln!(out, ".def {sym}; .scl 2; .type 32; .endef");
    }
    let _ = writeln!(out, r#".section .gfids$y,"dr""#);
    for sym in globals
        .iter()
        .copied()
        .chain(std::iter::once("__wincmd_cfg_nop"))
    {
        let _ = writeln!(out, "    .symidx {sym}");
    }
    emit_standard_section(out, fmt, SectionKind::Rdata, TargetAssembler::LlvmMc);
}

fn emit_arm64_gs_cookie(out: &mut String, target: TargetAssembler) {
    match target {
        TargetAssembler::FasmArm => {
            let _ = writeln!(out, "align 8{LF}__wincmd_gs_cookie dd 0x00002B992DDFA232");
        }
        TargetAssembler::LlvmMc => {
            let _ = writeln!(
                out,
                ".p2align 3{LF}__wincmd_gs_cookie:{LF}    .quad 0x00002B992DDFA232"
            );
        }
        _ => {}
    }
}

fn switch_module_section(
    out: &mut String,
    state: &mut SectionState,
    module: &CanonicalModule,
    kind: SectionKind,
    target: TargetAssembler,
    span: &SourceSpan,
) -> Result<(), ToolError> {
    let fmt = module.format;
    if state.active == Some(SectionKind::Text)
        && kind != SectionKind::Text
        && !state.emitted_cfg_nop
    {
        if fmt == BinaryFormat::Coff64 {
            emit_coff64_cfg_nop(out, target);
            state.emitted_cfg_nop = true;
        } else if matches!(fmt, BinaryFormat::ArmV7Pe | BinaryFormat::Arm64Pe) {
            emit_arm_cfg_and_chkstk(out, fmt, target);
            state.emitted_cfg_nop = true;
        }
    }
    close_open_section(out, fmt, state.active, target);
    if matches!(fmt, BinaryFormat::ArmV4Pe | BinaryFormat::ArmV7Pe)
        && target == TargetAssembler::FasmArm
    {
        let base = if fmt == BinaryFormat::ArmV4Pe {
            "0x00010000"
        } else {
            "0x00400000"
        };
        if let Some(prev_k) = state.armnt_prev_kind {
            close_fasmarm_pe32_section(out, state.armnt_idx - 1, prev_k);
        }
        open_fasmarm_pe32_section(out, fmt, kind, state.armnt_idx, base);
        state.armnt_prev_kind = Some(kind);
        state.armnt_idx += 1;
        state.seg_base = String::from("wincmd_seg_base");
    } else {
        state.seg_base = open_section(out, fmt, kind, target, span)?;
    }
    if fmt == BinaryFormat::Coff32
        && kind == SectionKind::Rdata
        && module.safeseh.is_some()
        && !state.emitted_load_config
    {
        emit_coff32_load_config(out, module, target, state.fasm_mode);
        state.emitted_load_config = true;
    }
    if fmt == BinaryFormat::Coff64 && kind == SectionKind::Rdata && !state.emitted_load_config {
        emit_coff64_load_config(out, module, target, state.fasm_mode);
        state.emitted_load_config = true;
    }
    if matches!(fmt, BinaryFormat::ArmV7Pe | BinaryFormat::Arm64Pe)
        && kind == SectionKind::Rdata
        && !state.emitted_load_config
    {
        emit_arm_load_config(out, module, target);
        state.emitted_load_config = true;
    }
    if fmt == BinaryFormat::Arm64Pe && kind == SectionKind::Data && !state.emitted_gs_cookie {
        emit_arm64_gs_cookie(out, target);
        state.emitted_gs_cookie = true;
    }
    state.active = Some(kind);
    Ok(())
}

fn finalize_module_sections(
    out: &mut String,
    module: &CanonicalModule,
    state: &mut SectionState,
    win64_frames: &[(String, u32, Vec<String>)],
    target: TargetAssembler,
) {
    if state.active == Some(SectionKind::Text) && !state.emitted_cfg_nop {
        if module.format == BinaryFormat::Coff64 {
            emit_coff64_cfg_nop(out, target);
            state.emitted_cfg_nop = true;
        } else if matches!(module.format, BinaryFormat::ArmV7Pe | BinaryFormat::Arm64Pe) {
            emit_arm_cfg_and_chkstk(out, module.format, target);
            state.emitted_cfg_nop = true;
        }
    }
    close_open_section(out, module.format, state.active, target);
    if module.format == BinaryFormat::Coff32 && module.safeseh.is_some() {
        if !state.emitted_load_config {
            emit_standard_section(out, BinaryFormat::Coff32, SectionKind::Rdata, target);
            emit_coff32_load_config(out, module, target, state.fasm_mode);
        }
        if target == TargetAssembler::Fasm && state.fasm_mode != FasmPeMode::DirectPe32 {
            let _ = writeln!(out, "section {SQUOTE}.sxdata{SQUOTE} linkinfo{LF}    dd 1");
        }
    }
    if module.format == BinaryFormat::Coff32
        && target == TargetAssembler::Fasm
        && state.fasm_mode == FasmPeMode::DirectPe32
    {
        emit_fasm_pe32_idata_and_reloc(out, module);
    }
    if matches!(module.format, BinaryFormat::ArmV4Pe | BinaryFormat::ArmV7Pe) {
        finalize_arm32_pe_sections(out, module, state, win64_frames, target);
    }
    if module.format == BinaryFormat::Arm64Pe {
        finalize_arm64_pe_sections(out, module, state, win64_frames, target);
    }
    if module.format == BinaryFormat::Coff64 {
        if !state.emitted_cfg_nop {
            emit_standard_section(out, BinaryFormat::Coff64, SectionKind::Text, target);
            emit_coff64_cfg_nop(out, target);
            state.emitted_cfg_nop = true;
        }
        if !state.emitted_load_config {
            emit_standard_section(out, BinaryFormat::Coff64, SectionKind::Rdata, target);
            emit_coff64_load_config(out, module, target, state.fasm_mode);
            state.emitted_load_config = true;
        }
        if !win64_frames.is_empty() {
            emit_win64_unwind_tables(out, win64_frames, target, state.fasm_mode);
        }
        if target == TargetAssembler::Fasm && state.fasm_mode != FasmPeMode::Coff64 {
            emit_fasm_pe64_idata(out, module);
            emit_fasm_pe64_rsrc_and_reloc(out);
        }
    }
}

const fn comment_prefix(target: TargetAssembler) -> &'static str {
    match target {
        TargetAssembler::LlvmMc => "//",
        TargetAssembler::Nasm
        | TargetAssembler::Fasm
        | TargetAssembler::Jwasm
        | TargetAssembler::FasmArm
        | TargetAssembler::ArmAsm => ";",
    }
}

fn emit_doc_block(out: &mut String, doc: &DocBlock, target: TargetAssembler) {
    let pfx = comment_prefix(target);
    for (tag, val) in &doc.tags {
        let _ = writeln!(out, "{pfx}{pfx} @{tag} {val}");
    }
}

fn emit_module_preamble(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) -> Result<(), ToolError> {
    if module.format == BinaryFormat::Include {
        return Ok(());
    }
    if target.supports_isa(CpuIsa::X86_8086) {
        emit_x86_preamble(out, module, target, fasm_mode)
    } else {
        emit_arm_preamble(out, module, target)
    }
}

fn emit_mz16_preamble(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
    span: SourceSpan,
) -> Result<(), ToolError> {
    match target {
        TargetAssembler::Nasm => {
            emit_nasm_cpu(out, module.cpu);
            let _ = writeln!(
                out,
                "BITS 16{LF}SEGMENT _TEXT PUBLIC CLASS=CODE ALIGN=16 USE16{LF}..start:{LF}wincmd_seg_base:"
            );
        }
        TargetAssembler::Fasm => {
            let ent = module.entry.as_deref().unwrap_or("mz_start");
            let _ = writeln!(
                out,
                "format binary as 'exe'{LF}use16{LF}dw 0x5A4D, (__mz_img_end mod 512), ((__mz_img_end + 511) / 512), 0, 2, 64, 0xFFFF{LF}dw ((__mz_text_end + 15) / 16), 0x0400, 0, {ent}, 0, 0x001C, 0, 0, 0{LF}org 0x0000{LF}wincmd_seg_base:"
            );
        }
        TargetAssembler::Jwasm => {
            emit_jwasm_cpu(out, module.cpu);
            let _ = writeln!(
                out,
                "OPTION CASEMAP:NONE{LF}_TEXT SEGMENT PARA PUBLIC 'CODE'{LF}ASSUME CS:_TEXT, DS:_TEXT, ES:_TEXT{LF}wincmd_seg_base:"
            );
        }
        _ => {
            return Err(ToolError::Lowering {
                span,
                assembler: target,
                message: format!("MZ16 format unsupported for assembler `{target}`"),
            });
        }
    }
    Ok(())
}

fn emit_x86_preamble(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) -> Result<(), ToolError> {
    let span = SourceSpan::new(&module.file_path, 1, 1);
    match (module.format, target) {
        (BinaryFormat::Com | BinaryFormat::Bin16, TargetAssembler::Nasm) => {
            emit_nasm_cpu(out, module.cpu);
            let org = if module.format == BinaryFormat::Com {
                "0x0100"
            } else {
                "0x0000"
            };
            let _ = writeln!(out, "BITS 16{LF}ORG {org}{LF}wincmd_seg_base:");
        }
        (BinaryFormat::Com | BinaryFormat::Bin16, TargetAssembler::Fasm) => {
            let hdr = if module.format == BinaryFormat::Com {
                "format binary as 'com'"
            } else {
                "format binary"
            };
            let org = if module.format == BinaryFormat::Com {
                "0x0100"
            } else {
                "0x0000"
            };
            let _ = writeln!(out, "{hdr}{LF}use16{LF}org {org}{LF}wincmd_seg_base:");
        }
        (BinaryFormat::Com | BinaryFormat::Bin16, TargetAssembler::Jwasm) => {
            emit_jwasm_cpu(out, module.cpu);
            let org = if module.format == BinaryFormat::Com {
                "0100h"
            } else {
                "0000h"
            };
            let _ = writeln!(
                out,
                "OPTION CASEMAP:NONE{LF}_TEXT SEGMENT PARA PUBLIC 'CODE'{LF}ASSUME CS:_TEXT, DS:_TEXT, ES:_TEXT, SS:_TEXT{LF}ORG {org}{LF}wincmd_seg_base:"
            );
        }
        (BinaryFormat::Com3Part, TargetAssembler::Nasm) => {
            emit_nasm_cpu(out, module.cpu);
            let _ = writeln!(out, "BITS 16");
        }
        (BinaryFormat::Com3Part, TargetAssembler::Fasm) => {
            let _ = writeln!(out, "format binary as 'com'{LF}use16");
        }
        (BinaryFormat::Com3Part, TargetAssembler::Jwasm) => {
            emit_jwasm_cpu(out, module.cpu);
            let _ = writeln!(out, "OPTION CASEMAP:NONE");
        }
        (BinaryFormat::Mz16, _) => return emit_mz16_preamble(out, module, target, span),
        (BinaryFormat::Coff32, _) => {
            return emit_coff32_preamble(out, module, target, fasm_mode, span);
        }
        (BinaryFormat::Coff64, TargetAssembler::Nasm) => {
            let _ = writeln!(
                out,
                "CPU X64{LF}BITS 64{LF}DEFAULT REL{LF}GLOBAL @feat.00{LF}@feat.00 equ 0x2000{LF}GLOBAL _load_config_used{LF}GLOBAL __wincmd_cfg_nop"
            );
        }
        (BinaryFormat::Coff64, TargetAssembler::Fasm) if fasm_mode == FasmPeMode::Coff64 => {
            let _ = writeln!(
                out,
                "format MS64 COFF{LF}@feat_val = 0x2000{LF}public @feat_val as {SQUOTE}@feat.00{SQUOTE}{LF}public _load_config_used{LF}public __wincmd_cfg_nop{LF}extrn {SQUOTE}__guard_fids_table{SQUOTE} as __guard_fids_table:byte{LF}extrn {SQUOTE}__guard_fids_count{SQUOTE} as __guard_fids_count:byte{LF}extrn {SQUOTE}__guard_flags{SQUOTE} as __guard_flags:dword"
            );
        }
        (BinaryFormat::Coff64, TargetAssembler::Fasm) => {
            let _ = writeln!(
                out,
                "format PE64 console 5.02 NX at 140000000h{LF}stack 1048576, 65536"
            );
            let entry_sym = module.entry.as_deref().or_else(|| {
                module.items.iter().find_map(|it| {
                    if let TopItem::Global { symbol, .. } = it {
                        Some(symbol.as_str())
                    } else {
                        None
                    }
                })
            });
            if let Some(ent) = entry_sym {
                let _ = writeln!(out, "entry {ent}");
            }
        }
        (BinaryFormat::Coff64, TargetAssembler::Jwasm) => {
            let _ = writeln!(
                out,
                ".x64{LF}OPTION CASEMAP:NONE{LF}PUBLIC _load_config_used{LF}PUBLIC __wincmd_cfg_nop{LF}EXTRN __guard_fids_table:BYTE{LF}EXTRN __guard_fids_count:ABS{LF}EXTRN __guard_flags:ABS"
            );
        }
        _ => {
            return Err(ToolError::Lowering {
                span,
                assembler: target,
                message: format!(
                    "unsupported format `{:?}` for assembler `{target}`",
                    module.format
                ),
            });
        }
    }
    Ok(())
}

fn emit_coff32_preamble(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
    span: SourceSpan,
) -> Result<(), ToolError> {
    let safeseh = module.safeseh.as_deref();
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "CPU 386{LF}BITS 32");
            if let Some(seh) = safeseh {
                let _ = writeln!(
                    out,
                    "GLOBAL __load_config_used{LF}EXTERN ___safe_se_handler_table{LF}EXTERN ___safe_se_handler_count{LF}safeseh {seh}"
                );
            }
            Ok(())
        }
        TargetAssembler::Fasm if fasm_mode == FasmPeMode::DirectPe32 => {
            let _ = writeln!(
                out,
                "format PE console 3.10 NX at 400000h{LF}stack 1048576, 65536"
            );
            let entry_sym = module.entry.as_deref().or_else(|| {
                module.items.iter().rev().find_map(|it| {
                    if let TopItem::Global { symbol, .. } = it {
                        Some(symbol.as_str())
                    } else {
                        None
                    }
                })
            });
            if let Some(ent) = entry_sym {
                let _ = writeln!(out, "entry {ent}");
            }
            Ok(())
        }
        TargetAssembler::Fasm => {
            let _ = writeln!(out, "format MS COFF");
            if let Some(seh) = safeseh {
                let _ = writeln!(
                    out,
                    "@feat_val = 1{LF}public @feat_val as {SQUOTE}@feat.00{SQUOTE}{LF}public {seh}{LF}public __load_config_used{LF}extrn {SQUOTE}___safe_se_handler_table{SQUOTE} as ___safe_se_handler_table:dword{LF}extrn {SQUOTE}___safe_se_handler_count{SQUOTE} as ___safe_se_handler_count:dword"
                );
            }
            Ok(())
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(
                out,
                ".386{LF}.MODEL FLAT, SYSCALL{LF}OPTION CASEMAP:NONE{LF}ASSUME FS:NOTHING"
            );
            if let Some(seh) = safeseh {
                let _ = writeln!(
                    out,
                    "PUBLIC __load_config_used{LF}EXTRN ___safe_se_handler_table:DWORD{LF}EXTRN ___safe_se_handler_count:ABS{LF}{seh} PROTO{LF}.SAFESEH {seh}"
                );
            }
            Ok(())
        }
        TargetAssembler::FasmArm | TargetAssembler::LlvmMc | TargetAssembler::ArmAsm => {
            Err(ToolError::Lowering {
                span,
                assembler: target,
                message: format!(
                    "unsupported format `{:?}` for assembler `{target}`",
                    module.format
                ),
            })
        }
    }
}

fn emit_arm_preamble(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
) -> Result<(), ToolError> {
    let span = SourceSpan::new(&module.file_path, 1, 1);
    match (module.format, target) {
        (BinaryFormat::ArmV4Pe, TargetAssembler::FasmArm) => {
            emit_fasmarm_pe32_header(out, module, false);
        }
        (BinaryFormat::ArmV4Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(out, ".syntax unified{LF}.arm");
        }
        (BinaryFormat::ArmV4Pe, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "    ARM");
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::FasmArm) => {
            emit_fasmarm_pe32_header(out, module, true);
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                ".syntax unified{LF}.thumb{LF}.set __WINCMD_THUMB2, 1{LF}.global \"@feat.00\"{LF}.set \"@feat.00\", 0x0800{LF}.global _load_config_used{LF}.global __wincmd_cfg_nop{LF}.extern __guard_fids_table{LF}.extern __guard_fids_count{LF}.extern __guard_flags"
            );
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "    THUMB");
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "format PE64 console 10.0 NX at 140000000h{LF}stack 1048576, 65536"
            );
            if let Some(ent) = &module.entry {
                let _ = writeln!(out, "entry {ent}");
            }
            let _ = writeln!(out, "processor CPU64_V8");
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                ".global \"@feat.00\"{LF}.set \"@feat.00\", 0x0800{LF}.global _load_config_used{LF}.global __wincmd_cfg_nop{LF}.extern __guard_fids_table{LF}.extern __guard_fids_count{LF}.extern __guard_flags"
            );
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::ArmAsm) => {}
        _ => {
            return Err(ToolError::Lowering {
                span,
                assembler: target,
                message: format!(
                    "unsupported format `{:?}` for assembler `{target}`",
                    module.format
                ),
            });
        }
    }
    Ok(())
}

fn collect_pe_imp_names(module: &CanonicalModule) -> Vec<&str> {
    module
        .items
        .iter()
        .filter_map(|it| {
            if let TopItem::Extern { symbol, .. } = it {
                symbol.strip_prefix("__imp_")
            } else {
                None
            }
        })
        .collect()
}

fn emit_fasmarm_pe32_header(out: &mut String, module: &CanonicalModule, is_v7: bool) {
    let mut user_secs: Vec<SectionKind> = module
        .items
        .iter()
        .filter_map(|it| match it {
            TopItem::Section { kind, .. } => Some(*kind),
            _ => None,
        })
        .collect();
    if user_secs.is_empty() {
        user_secs.push(SectionKind::Text);
    }
    let need_synth_rdata = is_v7 && !user_secs.contains(&SectionKind::Rdata);
    let has_imps = !collect_pe_imp_names(module).is_empty();
    let synth_cnt =
        usize::from(need_synth_rdata) + usize::from(has_imps) + 1 + if is_v7 { 2 } else { 0 };
    let nsec = user_secs.len() + synth_cnt;

    let (base, mach, os_maj, os_min, subsys, dll_chars) = if is_v7 {
        ("0x00400000", "0x01C4", 6, 2, 3, "0x4140")
    } else {
        ("0x00010000", "0x01C0", 4, 0, 9, "0x0000")
    };
    let entry_expr = module.entry.as_ref().map_or_else(
        || String::from(if is_v7 { "0x1001" } else { "0x1000" }),
        |ent| {
            if is_v7 {
                format!("((__armnt_text_rva + ({ent} - __armnt_text_base)) or 1)")
            } else {
                format!("(__armnt_text_rva + ({ent} - __armnt_text_base))")
            }
        },
    );
    let code_rsize_expr = user_secs
        .iter()
        .position(|k| *k == SectionKind::Text)
        .map_or_else(|| String::from("0"), |i| format!("__armnt_sec_{i}_rsize"));

    let _ = writeln!(out, "format binary as 'exe'{LF}org {base}");
    if is_v7 {
        let _ = writeln!(
            out,
            "__WINCMD_THUMB2 = 1{LF}code16{LF}processor CPU32_V1 + CPU32_V2 + CPU32_A + CPU32_V3 + CPU32_M + CPU32_V4 + CPU32_V4T + CPU32_V5 + CPU32_V5T + CPU32_E + CPU32_V6 + CPU32_V6T + CPU32_K + CPU32_6M + CPU32_7M + CPU32_T2 + CPU32_V7 + CPU32_MP + CPU32_SYNC + CPU32_DIV"
        );
    } else {
        let _ = writeln!(
            out,
            "code32{LF}processor CPU32_V1 + CPU32_V2 + CPU32_V3 + CPU32_M + CPU32_V4 + CPU32_V4T"
        );
    }
    let _ = writeln!(
        out,
        "dh 0x5A4D{LF}times 29 dh 0{LF}dw 0x00000040{LF}db 'PE', 0, 0{LF}dh {mach}, {nsec}{LF}dw 0, 0, 0{LF}dh 0x00E0, 0x0102"
    );
    let _ = writeln!(
        out,
        "dh 0x010B{LF}db 14, 0{LF}dw {code_rsize_expr}, 0, 0, {entry_expr}, 0x1000, 0x2000, {base}, 0x1000, 0x0200"
    );
    let _ = writeln!(
        out,
        "dh {os_maj}, {os_min}, 0, 0, {os_maj}, {os_min}{LF}dw 0, __armnt_image_size, 0x0400, 0{LF}dh {subsys}, {dll_chars}{LF}dw 0x100000, 0x10000, 0x100000, 0x4000, 0, 16"
    );
    emit_fasmarm_pe32_datadirs_and_sechdrs(out, &user_secs, need_synth_rdata, has_imps, is_v7);
}

fn emit_fasmarm_pe32_datadirs_and_sechdrs(
    out: &mut String,
    user_secs: &[SectionKind],
    need_synth_rdata: bool,
    has_imps: bool,
    is_v7: bool,
) {
    let imp_dd = if has_imps {
        "__armnt_idata_rva, __armnt_idata_vsize"
    } else {
        "0, 0"
    };
    let rsrc_dd = if is_v7 {
        "__armnt_rsrc_rva, __armnt_rsrc_vsize"
    } else {
        "0, 0"
    };
    let reloc_dd = if is_v7 {
        "__armnt_reloc_rva, __armnt_reloc_vsize"
    } else {
        "0, 0"
    };
    let lcfg_dd = if is_v7 {
        "__armnt_loadcfg_rva, 160"
    } else {
        "0, 0"
    };
    let _ = writeln!(
        out,
        "dw 0, 0, {imp_dd}, {rsrc_dd}, __armnt_pdata_rva, __armnt_pdata_vsize, 0, 0, {reloc_dd}"
    );
    let _ = writeln!(
        out,
        "dw 0, 0, 0, 0, 0, 0, 0, 0, {lcfg_dd}, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0"
    );

    let mut idx = 0_usize;
    for kind in user_secs {
        emit_fasmarm_pe32_sechdr_row(out, idx, *kind);
        idx += 1;
    }
    if need_synth_rdata {
        emit_fasmarm_pe32_sechdr_row(out, idx, SectionKind::Rdata);
        idx += 1;
    }
    if has_imps {
        emit_fasmarm_pe32_named_sechdr(out, idx, "'.idata', 0, 0", "0xC0000040");
        idx += 1;
    }
    emit_fasmarm_pe32_named_sechdr(out, idx, "'.pdata', 0, 0", "0x40000040");
    idx += 1;
    if is_v7 {
        emit_fasmarm_pe32_named_sechdr(out, idx, "'.rsrc', 0, 0, 0", "0x40000040");
        idx += 1;
        emit_fasmarm_pe32_named_sechdr(out, idx, "'.reloc', 0, 0", "0x42000040");
    }
}

fn emit_fasmarm_pe32_sechdr_row(out: &mut String, idx: usize, kind: SectionKind) {
    if kind == SectionKind::Bss {
        let _ = writeln!(
            out,
            "db '.bss', 0, 0, 0, 0{LF}dw __armnt_sec_{idx}_vsize, __armnt_sec_{idx}_rva, 0, 0, 0, 0{LF}dh 0, 0{LF}dw 0xC0000080"
        );
        return;
    }
    let (name8, chars) = match kind {
        SectionKind::Text => ("'.text', 0, 0, 0", "0x60000020"),
        SectionKind::Rdata => ("'.rdata', 0, 0", "0x40000040"),
        SectionKind::Data => ("'.data', 0, 0, 0", "0xC0000040"),
        _ => ("'.sec', 0, 0, 0, 0", "0x40000040"),
    };
    emit_fasmarm_pe32_named_sechdr(out, idx, name8, chars);
}

fn emit_fasmarm_pe32_named_sechdr(out: &mut String, idx: usize, name8: &str, chars: &str) {
    let _ = writeln!(
        out,
        "db {name8}{LF}dw __armnt_sec_{idx}_vsize, __armnt_sec_{idx}_rva, __armnt_sec_{idx}_rsize, __armnt_sec_{idx}_fptr, 0, 0{LF}dh 0, 0{LF}dw {chars}"
    );
}

fn open_fasmarm_pe32_section(
    out: &mut String,
    fmt: BinaryFormat,
    kind: SectionKind,
    idx: usize,
    base: &str,
) {
    if idx == 0 {
        let _ = writeln!(
            out,
            "times (0x0400 - ($ - $$)) db 0{LF}__armnt_sec_0_rva = 0x1000{LF}__armnt_sec_0_fptr = 0x0400"
        );
    } else {
        let prev = idx - 1;
        let _ = writeln!(
            out,
            "__armnt_sec_{idx}_rva = __armnt_sec_{prev}_rva + __armnt_sec_{prev}_vspan{LF}__armnt_sec_{idx}_fptr = __armnt_sec_{prev}_fptr + __armnt_sec_{prev}_rsize"
        );
    }
    if kind == SectionKind::Bss {
        let _ = writeln!(
            out,
            "virtual at {base} + __armnt_sec_{idx}_rva{LF}__armnt_sec_{idx}_start:"
        );
        return;
    }
    let _ = writeln!(
        out,
        "org {base} + __armnt_sec_{idx}_rva{LF}__armnt_sec_{idx}_start:"
    );
    if kind == SectionKind::Text {
        let mode = if fmt == BinaryFormat::ArmV7Pe {
            "thumb"
        } else {
            "code32"
        };
        let _ = writeln!(
            out,
            "__armnt_text_rva = __armnt_sec_{idx}_rva{LF}__armnt_text_base:{LF}{mode}"
        );
    }
}

fn close_fasmarm_pe32_section(out: &mut String, idx: usize, kind: SectionKind) {
    let _ = writeln!(out, "__armnt_sec_{idx}_vsize = $ - __armnt_sec_{idx}_start");
    let _ = writeln!(
        out,
        "if __armnt_sec_{idx}_vsize = 0{LF}    __armnt_sec_{idx}_vspan = 0x1000{LF}else{LF}    __armnt_sec_{idx}_vspan = (__armnt_sec_{idx}_vsize + 0x0FFF) and 0xFFFFF000{LF}end if"
    );
    if kind == SectionKind::Bss {
        let _ = writeln!(out, "end virtual{LF}__armnt_sec_{idx}_rsize = 0");
    } else {
        let _ = writeln!(
            out,
            "times ((512 - (__armnt_sec_{idx}_vsize mod 512)) mod 512) db 0{LF}__armnt_sec_{idx}_rsize = $ - __armnt_sec_{idx}_start"
        );
    }
}

fn finalize_arm32_pe_sections(
    out: &mut String,
    module: &CanonicalModule,
    state: &mut SectionState,
    arm_procs: &[(String, u32, Vec<String>)],
    target: TargetAssembler,
) {
    let fmt = module.format;
    let is_v7 = fmt == BinaryFormat::ArmV7Pe;
    if target == TargetAssembler::LlvmMc {
        if is_v7 {
            if !state.emitted_cfg_nop {
                emit_standard_section(out, fmt, SectionKind::Text, target);
                emit_arm_cfg_and_chkstk(out, fmt, target);
                state.emitted_cfg_nop = true;
            }
            if !state.emitted_load_config {
                emit_standard_section(out, fmt, SectionKind::Rdata, target);
                emit_arm_load_config(out, module, target);
                state.emitted_load_config = true;
            }
            emit_llvm_arm_pdata(out, module, arm_procs, true);
        }
        return;
    }
    if target != TargetAssembler::FasmArm {
        return;
    }
    let base = if is_v7 { "0x00400000" } else { "0x00010000" };
    if state.armnt_idx == 0 {
        open_fasmarm_pe32_section(out, fmt, SectionKind::Text, 0, base);
        if is_v7 {
            emit_arm_cfg_and_chkstk(out, fmt, target);
            state.emitted_cfg_nop = true;
        }
        state.armnt_prev_kind = Some(SectionKind::Text);
        state.armnt_idx = 1;
    }
    if is_v7 && !state.emitted_load_config {
        if let Some(pk) = state.armnt_prev_kind {
            close_fasmarm_pe32_section(out, state.armnt_idx - 1, pk);
        }
        open_fasmarm_pe32_section(out, fmt, SectionKind::Rdata, state.armnt_idx, base);
        emit_arm_load_config(out, module, target);
        state.emitted_load_config = true;
        state.armnt_prev_kind = Some(SectionKind::Rdata);
        state.armnt_idx += 1;
    }
    emit_fasmarm_pe32_synth_tails(out, module, state, arm_procs, base, is_v7);
}

fn emit_fasmarm_pe32_synth_tails(
    out: &mut String,
    module: &CanonicalModule,
    state: &mut SectionState,
    arm_procs: &[(String, u32, Vec<String>)],
    base: &str,
    is_v7: bool,
) {
    let imps = collect_pe_imp_names(module);
    if !imps.is_empty() {
        if let Some(pk) = state.armnt_prev_kind {
            close_fasmarm_pe32_section(out, state.armnt_idx - 1, pk);
        }
        let idx = state.armnt_idx;
        open_fasmarm_pe32_section(out, module.format, SectionKind::Data, idx, base);
        let dll = if is_v7 { "KERNEL32.dll" } else { "COREDLL.dll" };
        emit_fasmarm_pe32_idata_body(out, &imps, base, dll, idx);
        state.armnt_prev_kind = Some(SectionKind::Data);
        state.armnt_idx += 1;
    }

    if let Some(pk) = state.armnt_prev_kind {
        close_fasmarm_pe32_section(out, state.armnt_idx - 1, pk);
    }
    let pidx = state.armnt_idx;
    open_fasmarm_pe32_section(out, module.format, SectionKind::Rdata, pidx, base);
    emit_fasmarm_pe32_pdata_body(out, arm_procs, pidx, is_v7);
    state.armnt_prev_kind = Some(SectionKind::Rdata);
    state.armnt_idx += 1;

    if is_v7 {
        if let Some(pk) = state.armnt_prev_kind {
            close_fasmarm_pe32_section(out, state.armnt_idx - 1, pk);
        }
        let ridx = state.armnt_idx;
        open_fasmarm_pe32_section(out, module.format, SectionKind::Rdata, ridx, base);
        emit_fasmarm_pe32_rsrc_body(out, base, ridx);
        close_fasmarm_pe32_section(out, ridx, SectionKind::Rdata);
        state.armnt_idx += 1;

        let xidx = state.armnt_idx;
        open_fasmarm_pe32_section(out, module.format, SectionKind::Rdata, xidx, base);
        let _ = writeln!(
            out,
            "__armnt_reloc_rva = __armnt_sec_{xidx}_rva{LF}    dw 0x1000, 8{LF}__armnt_reloc_vsize = 8"
        );
        close_fasmarm_pe32_section(out, xidx, SectionKind::Rdata);
        state.armnt_prev_kind = None;
        state.armnt_idx += 1;
    } else if let Some(pk) = state.armnt_prev_kind {
        close_fasmarm_pe32_section(out, state.armnt_idx - 1, pk);
        state.armnt_prev_kind = None;
    }
    let last = state.armnt_idx - 1;
    let _ = writeln!(
        out,
        "__armnt_image_size = __armnt_sec_{last}_rva + __armnt_sec_{last}_vspan"
    );
}

fn emit_fasmarm_pe32_idata_body(
    out: &mut String,
    imps: &[&str],
    base: &str,
    dll: &str,
    idx: usize,
) {
    let _ = writeln!(
        out,
        "__armnt_idata_rva = __armnt_sec_{idx}_rva{LF}    dw (__armnt_ilt - {base}), 0, 0, (__armnt_dll_name - {base}), (__armnt_iat - {base}){LF}    dw 0, 0, 0, 0, 0{LF}__armnt_ilt:"
    );
    for api in imps {
        let _ = writeln!(out, "    dw (__armnt_hn_{api} - {base})");
    }
    let _ = writeln!(out, "    dw 0{LF}__armnt_iat:");
    for api in imps {
        let _ = writeln!(out, "__imp_{api}:{LF}    dw (__armnt_hn_{api} - {base})");
    }
    let _ = writeln!(out, "    dw 0{LF}__armnt_dll_name:{LF}    db '{dll}', 0");
    for api in imps {
        let _ = writeln!(
            out,
            "    align 2{LF}__armnt_hn_{api}:{LF}    dh 0{LF}    db '{api}', 0"
        );
    }
    let _ = writeln!(out, "__armnt_idata_vsize = $ - __armnt_sec_{idx}_start");
}

fn emit_fasmarm_pe32_pdata_body(
    out: &mut String,
    arm_procs: &[(String, u32, Vec<String>)],
    idx: usize,
    is_v7: bool,
) {
    let _ = writeln!(out, "__armnt_pdata_rva = __armnt_sec_{idx}_rva");
    for (name, _, _) in arm_procs {
        if is_v7 {
            let _ = writeln!(
                out,
                "    dw ((__armnt_text_rva + ({name} - __armnt_text_base)) or 1){LF}    dw (0x00004001 or (((({name}__end - {name}) / 2) and 0x7FF) shl 2))"
            );
        } else {
            let _ = writeln!(
                out,
                "    dw (0x00010000 + __armnt_text_rva + ({name} - __armnt_text_base)){LF}    dw ((1 shl 30) or (((({name}__end - {name}) / 4) and 0x3FFFFF) shl 8) or 2)"
            );
        }
    }
    if is_v7 {
        for sym in ["__chkstk", "__wincmd_cfg_nop"] {
            let _ = writeln!(
                out,
                "    dw ((__armnt_text_rva + ({sym} - __armnt_text_base)) or 1){LF}    dw (0x00004001 or (((({sym}__end - {sym}) / 2) and 0x7FF) shl 2))"
            );
        }
    } else if arm_procs.is_empty() {
        let _ = writeln!(out, "    dw 0x00011000, 0x40000102");
    }
    let _ = writeln!(out, "__armnt_pdata_vsize = $ - __armnt_sec_{idx}_start");
}

fn emit_fasmarm_pe32_rsrc_body(out: &mut String, base: &str, idx: usize) {
    const MANIFEST_XML: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application><supportedOS Id="{e2011457-1546-43c5-a5fe-008deee3d3f0}"/><supportedOS Id="{35138b9a-5d96-4fbd-8e2d-a2440225f93a}"/><supportedOS Id="{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}"/><supportedOS Id="{1f676c76-80e1-4239-95bb-83d0f6d0da78}"/><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application></compatibility><application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings><longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware></windowsSettings></application></assembly>"#;
    let _ = writeln!(
        out,
        "__armnt_rsrc_rva = __armnt_sec_{idx}_rva{LF}    dw 0, 0, 0{LF}    dh 0, 1{LF}    dw 24, 0x80000018{LF}    dw 0, 0, 0{LF}    dh 0, 1{LF}    dw 1, 0x80000030{LF}    dw 0, 0, 0{LF}    dh 0, 1{LF}    dw 0x0409, 0x00000048{LF}    dw (__wincmd_manifest_xml - {base}), (__wincmd_manifest_end - __wincmd_manifest_xml), 65001, 0{LF}__wincmd_manifest_xml:{LF}    db {SQUOTE}{MANIFEST_XML}{SQUOTE}{LF}__wincmd_manifest_end:{LF}__armnt_rsrc_vsize = $ - __armnt_sec_{idx}_start"
    );
}

fn emit_llvm_arm_pdata(
    out: &mut String,
    module: &CanonicalModule,
    arm_procs: &[(String, u32, Vec<String>)],
    is_v7: bool,
) {
    let _ = writeln!(out, r#".section .pdata,"dr""#);
    let _ = writeln!(out, ".p2align 2");
    if is_v7 {
        if arm_procs.is_empty() {
            for name in module.items.iter().filter_map(|it| match it {
                TopItem::Proc { name, .. } => Some(name.as_str()),
                _ => None,
            }) {
                let _ = writeln!(
                    out,
                    "    .rva {name}{LF}    .long (0x00004001 | ((((.L{name}__end - {name}) / 2) & 0x7FF) << 2))"
                );
            }
        } else {
            for (name, _, _) in arm_procs {
                let _ = writeln!(
                    out,
                    "    .rva {name}{LF}    .long (0x00004001 | ((((.L{name}__end - {name}) / 2) & 0x7FF) << 2))"
                );
            }
        }
        for sym in ["__chkstk", "__wincmd_cfg_nop"] {
            let _ = writeln!(
                out,
                "    .rva {sym}{LF}    .long (0x00004001 | ((((.L{sym}__end - {sym}) / 2) & 0x7FF) << 2))"
            );
        }
    } else {
        for (name, _, _) in arm_procs {
            let _ = writeln!(
                out,
                "    .rva {name}{LF}    .long (0x00000001 | ((((.L{name}__end - {name}) / 4) & 0x3FFFF) << 2))"
            );
        }
        for sym in ["__chkstk", "__wincmd_cfg_nop"] {
            let _ = writeln!(
                out,
                "    .rva {sym}{LF}    .long (0x00000001 | ((((.L{sym}__end - {sym}) / 4) & 0x3FFFF) << 2))"
            );
        }
    }
}

fn finalize_arm64_pe_sections(
    out: &mut String,
    module: &CanonicalModule,
    state: &mut SectionState,
    arm_procs: &[(String, u32, Vec<String>)],
    target: TargetAssembler,
) {
    if !matches!(target, TargetAssembler::FasmArm | TargetAssembler::LlvmMc) {
        return;
    }
    if !state.emitted_cfg_nop {
        emit_standard_section(out, BinaryFormat::Arm64Pe, SectionKind::Text, target);
        emit_arm_cfg_and_chkstk(out, BinaryFormat::Arm64Pe, target);
        state.emitted_cfg_nop = true;
    }
    if !state.emitted_load_config {
        emit_standard_section(out, BinaryFormat::Arm64Pe, SectionKind::Rdata, target);
        emit_arm_load_config(out, module, target);
        state.emitted_load_config = true;
    }
    if !state.emitted_gs_cookie {
        emit_standard_section(out, BinaryFormat::Arm64Pe, SectionKind::Data, target);
        emit_arm64_gs_cookie(out, target);
        state.emitted_gs_cookie = true;
    }
    if target == TargetAssembler::LlvmMc {
        emit_llvm_arm_pdata(out, module, arm_procs, false);
        return;
    }
    out.push_str("section '.pdata' data readable\ndata 3\n");
    for (name, _, _) in arm_procs {
        let _ = writeln!(
            out,
            "    dw rva {name}{LF}    dw (0x00000001 or (((({name}__end - {name}) / 4) and 0x3FFFF) shl 2))"
        );
    }
    for sym in ["__chkstk", "__wincmd_cfg_nop"] {
        let _ = writeln!(
            out,
            "    dw rva {sym}{LF}    dw (0x00000001 or (((({sym}__end - {sym}) / 4) and 0x3FFFF) shl 2))"
        );
    }
    out.push_str("end data\n");
    emit_fasmarm_pe64_idata_and_rsrc(out, module);
}

fn emit_fasmarm_pe64_idata_and_rsrc(out: &mut String, module: &CanonicalModule) {
    const MANIFEST_XML: &str = r#"<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1"><application><supportedOS Id="{e2011457-1546-43c5-a5fe-008deee3d3f0}"/><supportedOS Id="{35138b9a-5d96-4fbd-8e2d-a2440225f93a}"/><supportedOS Id="{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}"/><supportedOS Id="{1f676c76-80e1-4239-95bb-83d0f6d0da78}"/><supportedOS Id="{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}"/></application></compatibility><application xmlns="urn:schemas-microsoft-com:asm.v3"><windowsSettings><longPathAware xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">true</longPathAware></windowsSettings></application></assembly>"#;
    let imps = collect_pe_imp_names(module);
    if !imps.is_empty() {
        out.push_str("section '.idata' import data readable writeable\n");
        out.push_str("    dw rva wincmd_k32_ilt, 0, 0, rva wincmd_k32_name, rva wincmd_k32_iat\n");
        out.push_str("    dw 0, 0, 0, 0, 0\nwincmd_k32_ilt:\n");
        for api in &imps {
            let _ = writeln!(out, "    dd rva wincmd_hn_{api}");
        }
        out.push_str("    dd 0\nwincmd_k32_iat:\n");
        for api in &imps {
            let _ = writeln!(out, "__imp_{api} dd rva wincmd_hn_{api}");
        }
        out.push_str("    dd 0\nwincmd_k32_name db 'KERNEL32.dll', 0\n");
        for api in &imps {
            let _ = writeln!(
                out,
                "    align 2{LF}wincmd_hn_{api}:{LF}    dh 0{LF}    db '{api}', 0"
            );
        }
    }
    let _ = writeln!(
        out,
        "section {SQUOTE}.rsrc{SQUOTE} resource data readable{LF}    dw 0, 0, 0{LF}    dh 0, 1{LF}    dw 24, 0x80000018{LF}    dw 0, 0, 0{LF}    dh 0, 1{LF}    dw 1, 0x80000030{LF}    dw 0, 0, 0{LF}    dh 0, 1{LF}    dw 0x0409, 0x00000048{LF}    dw rva __wincmd_manifest_xml, __wincmd_manifest_end - __wincmd_manifest_xml, 65001, 0{LF}__wincmd_manifest_xml:{LF}    db {SQUOTE}{MANIFEST_XML}{SQUOTE}{LF}__wincmd_manifest_end:{LF}section {SQUOTE}.reloc{SQUOTE} fixups data readable discardable"
    );
}

fn emit_nasm_cpu(out: &mut String, cpu: Option<CpuIsa>) {
    if cpu == Some(CpuIsa::X86_286) {
        let _ = writeln!(out, "CPU 286");
    } else {
        let _ = writeln!(out, "CPU 8086");
    }
}

fn emit_jwasm_cpu(out: &mut String, cpu: Option<CpuIsa>) {
    if cpu == Some(CpuIsa::X86_286) {
        let _ = writeln!(out, ".286p");
    } else {
        let _ = writeln!(out, ".8086");
    }
}

fn open_section(
    out: &mut String,
    fmt: BinaryFormat,
    kind: SectionKind,
    target: TargetAssembler,
    span: &SourceSpan,
) -> Result<String, ToolError> {
    match (fmt, kind, target) {
        (BinaryFormat::Com3Part, SectionKind::Resident, TargetAssembler::Nasm) => {
            out.push_str("SECTION .resident align=16 vstart=0x0100");
            out.push(LF);
            out.push_str("wincmd_res_base:");
            out.push(LF);
            Ok("wincmd_res_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Resident, TargetAssembler::Fasm) => {
            out.push_str("org 0x0100");
            out.push(LF);
            out.push_str("wincmd_res_base:");
            out.push(LF);
            Ok("wincmd_res_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Resident, TargetAssembler::Jwasm) => {
            out.push_str("RESIDENT_SEG SEGMENT PARA PUBLIC 'RES'");
            out.push(LF);
            out.push_str(
                "ASSUME CS:RESIDENT_SEG, DS:RESIDENT_SEG, ES:RESIDENT_SEG, SS:RESIDENT_SEG",
            );
            out.push(LF);
            out.push_str("ORG 0100h");
            out.push(LF);
            out.push_str("wincmd_res_base:");
            out.push(LF);
            Ok("wincmd_res_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Transient, TargetAssembler::Nasm) => {
            out.push_str("SECTION .transient align=16 follows=.resident vstart=0x0000");
            out.push(LF);
            out.push_str("wincmd_trn_base:");
            out.push(LF);
            Ok("wincmd_trn_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Transient, TargetAssembler::Fasm) => {
            out.push_str("org 0x0000");
            out.push(LF);
            out.push_str("wincmd_trn_base:");
            out.push(LF);
            Ok("wincmd_trn_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Transient, TargetAssembler::Jwasm) => {
            out.push_str("TRANSIENT_SEG SEGMENT PARA PUBLIC 'TRN'");
            out.push(LF);
            out.push_str(
                "ASSUME CS:TRANSIENT_SEG, DS:TRANSIENT_SEG, ES:TRANSIENT_SEG, SS:TRANSIENT_SEG",
            );
            out.push(LF);
            out.push_str("ORG 0000h");
            out.push(LF);
            out.push_str("wincmd_trn_base:");
            out.push(LF);
            Ok("wincmd_trn_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Init, TargetAssembler::Nasm) => {
            out.push_str("SECTION .init align=16 follows=.transient vstart=0x0000");
            out.push(LF);
            out.push_str("wincmd_init_base:");
            out.push(LF);
            Ok("wincmd_init_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Init, TargetAssembler::Fasm) => {
            out.push_str("org 0x0000");
            out.push(LF);
            out.push_str("wincmd_init_base:");
            out.push(LF);
            Ok("wincmd_init_base".into())
        }
        (BinaryFormat::Com3Part, SectionKind::Init, TargetAssembler::Jwasm) => {
            out.push_str("INIT_SEG SEGMENT PARA PUBLIC 'INIT'");
            out.push(LF);
            out.push_str("ASSUME CS:INIT_SEG, DS:INIT_SEG, ES:INIT_SEG, SS:INIT_SEG");
            out.push(LF);
            out.push_str("ORG 0000h");
            out.push(LF);
            out.push_str("wincmd_init_base:");
            out.push(LF);
            Ok("wincmd_init_base".into())
        }
        (_, SectionKind::Text | SectionKind::Rdata | SectionKind::Data | SectionKind::Bss, _)
            if fmt != BinaryFormat::Com3Part =>
        {
            emit_standard_section(out, fmt, kind, target);
            Ok("wincmd_seg_base".into())
        }
        _ => Err(ToolError::Lowering {
            span: span.clone(),
            assembler: target,
            message: format!("section `{kind:?}` invalid in format `{fmt:?}`"),
        }),
    }
}

fn emit_standard_section(
    out: &mut String,
    fmt: BinaryFormat,
    kind: SectionKind,
    target: TargetAssembler,
) {
    if matches!(
        fmt,
        BinaryFormat::Com | BinaryFormat::Bin16 | BinaryFormat::Mz16
    ) {
        return;
    }
    match (kind, target) {
        (SectionKind::Text, TargetAssembler::Nasm) => {
            out.push_str("SECTION .text align=16");
            out.push(LF);
        }
        (SectionKind::Rdata, TargetAssembler::Nasm) => {
            out.push_str("SECTION .rdata align=16");
            out.push(LF);
        }
        (SectionKind::Data, TargetAssembler::Nasm) => {
            out.push_str("SECTION .data align=16");
            out.push(LF);
        }
        (SectionKind::Bss, TargetAssembler::Nasm) => {
            out.push_str("SECTION .bss align=16");
            out.push(LF);
        }
        (SectionKind::Text, TargetAssembler::Fasm | TargetAssembler::FasmArm) => {
            out.push_str("section '.text' code readable executable");
            out.push(LF);
            if fmt == BinaryFormat::ArmV7Pe && target == TargetAssembler::FasmArm {
                out.push_str("thumb");
                out.push(LF);
            }
        }
        (SectionKind::Rdata, TargetAssembler::Fasm | TargetAssembler::FasmArm) => {
            out.push_str("section '.rdata' data readable");
            out.push(LF);
        }
        (SectionKind::Data, TargetAssembler::Fasm | TargetAssembler::FasmArm) => {
            out.push_str("section '.data' data readable writeable");
            out.push(LF);
        }
        (SectionKind::Bss, TargetAssembler::Fasm | TargetAssembler::FasmArm) => {
            if matches!(
                fmt,
                BinaryFormat::Coff32 | BinaryFormat::Arm64Pe | BinaryFormat::Include
            ) {
                out.push_str("section '.bss' readable writeable");
            } else if matches!(
                fmt,
                BinaryFormat::Coff64 | BinaryFormat::ArmV4Pe | BinaryFormat::ArmV7Pe
            ) {
                out.push_str("section '.bss' data readable writeable");
            } else {
                out.push_str("section '.bss' udata readable writeable");
            }
            out.push(LF);
        }
        (SectionKind::Text, TargetAssembler::Jwasm) => {
            let _ = writeln!(out, ".CODE");
        }
        (SectionKind::Rdata, TargetAssembler::Jwasm) => {
            let _ = writeln!(out, ".CONST");
        }
        (SectionKind::Data, TargetAssembler::Jwasm) => {
            let _ = writeln!(out, ".DATA");
        }
        (SectionKind::Bss, TargetAssembler::Jwasm) => {
            let _ = writeln!(out, ".DATA?");
        }
        (SectionKind::Text, TargetAssembler::LlvmMc) => {
            let _ = writeln!(out, ".text");
        }
        (SectionKind::Rdata, TargetAssembler::LlvmMc) => {
            let dir = if fmt == BinaryFormat::ArmV4Pe {
                ".section .rodata"
            } else {
                r#".section .rdata,"dr""#
            };
            let _ = writeln!(out, "{dir}");
        }
        (SectionKind::Data, TargetAssembler::LlvmMc) => {
            let _ = writeln!(out, ".data");
        }
        (SectionKind::Bss, TargetAssembler::LlvmMc) => {
            let _ = writeln!(out, ".bss");
        }
        (SectionKind::Text, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "    AREA |.text|, CODE, READONLY");
        }
        (SectionKind::Rdata, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "    AREA |.rdata|, DATA, READONLY");
        }
        (SectionKind::Data, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "    AREA |.data|, DATA, READWRITE");
        }
        (SectionKind::Bss, TargetAssembler::ArmAsm) => {
            out.push_str("    AREA |.bss|, NOINIT, READWRITE");
            out.push(LF);
        }
        _ => {}
    }
}

fn close_open_section(
    out: &mut String,
    fmt: BinaryFormat,
    active: Option<SectionKind>,
    target: TargetAssembler,
) {
    if fmt != BinaryFormat::Com3Part {
        return;
    }
    let Some(sec) = active else {
        return;
    };
    let (base, prefix, seg_name) = match sec {
        SectionKind::Resident => ("wincmd_res_base", "RES", "RESIDENT_SEG"),
        SectionKind::Transient => ("wincmd_trn_base", "TRN", "TRANSIENT_SEG"),
        SectionKind::Init => ("wincmd_init_base", "INIT", "INIT_SEG"),
        _ => return,
    };
    match target {
        TargetAssembler::Nasm => {
            out.push_str("align 16, db 0");
            out.push(LF);
            let _ = writeln!(out, "{prefix}_BYTE_LEN equ ($ - {base})");
            let _ = writeln!(out, "{prefix}_PARAS equ ({prefix}_BYTE_LEN / 16)");
        }
        TargetAssembler::Fasm => {
            let _ = writeln!(out, "repeat ((16 - (($ - {base}) and 15)) and 15)");
            out.push_str("    db 0");
            out.push(LF);
            out.push_str("end repeat");
            out.push(LF);
            let _ = writeln!(out, "{prefix}_BYTE_LEN = ($ - {base})");
            let _ = writeln!(out, "{prefix}_PARAS = ({prefix}_BYTE_LEN / 16)");
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(out, "DB ((16 - (($ - {base}) AND 15)) AND 15) DUP(0)");
            let _ = writeln!(out, "{prefix}_BYTE_LEN EQU ($ - {base})");
            let _ = writeln!(out, "{prefix}_PARAS EQU ({prefix}_BYTE_LEN / 16)");
            let _ = writeln!(out, "{seg_name} ENDS");
        }
        _ => {}
    }
}

fn emit_module_trailer(out: &mut String, module: &CanonicalModule, target: TargetAssembler) {
    match (module.format, target) {
        (BinaryFormat::Mz16, TargetAssembler::Nasm) => {
            let _ = writeln!(
                out,
                "SEGMENT _STACK STACK CLASS=STACK ALIGN=16{LF}    resb 1024"
            );
        }
        (BinaryFormat::Mz16, TargetAssembler::Fasm) => {
            let _ = writeln!(
                out,
                "__mz_text_end = $ - wincmd_seg_base{LF}__mz_img_end = 32 + __mz_text_end"
            );
        }
        (BinaryFormat::Mz16, TargetAssembler::Jwasm) => {
            let _ = writeln!(
                out,
                "_TEXT ENDS{LF}_STACK SEGMENT PARA STACK 'STACK'{LF}    DB 1024 DUP (?){LF}_STACK ENDS"
            );
            if let Some(ent) = &module.entry {
                let _ = writeln!(out, "END {ent}");
            } else {
                out.push_str("END");
                out.push(LF);
            }
        }
        (BinaryFormat::Com, TargetAssembler::Jwasm) => {
            out.push_str("_TEXT ENDS");
            out.push(LF);
            if let Some(ent) = &module.entry {
                let _ = writeln!(out, "END {ent}");
            } else {
                out.push_str("END");
                out.push(LF);
            }
        }
        (BinaryFormat::Bin16, TargetAssembler::Jwasm) => {
            out.push_str("_TEXT ENDS");
            out.push(LF);
            out.push_str("END");
            out.push(LF);
        }
        (BinaryFormat::Com3Part, TargetAssembler::Jwasm) => {
            if let Some(ent) = &module.entry {
                let _ = writeln!(out, "END {ent}");
            } else {
                out.push_str("END");
                out.push(LF);
            }
        }
        (BinaryFormat::Coff32 | BinaryFormat::Coff64, TargetAssembler::Jwasm) => {
            out.push_str("END");
            out.push(LF);
        }
        (_, TargetAssembler::ArmAsm) if module.format != BinaryFormat::Include => {
            out.push_str("    END");
            out.push(LF);
        }
        _ => {}
    }
}

fn emit_include(out: &mut String, path: &str, target: TargetAssembler) {
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, r#"%include "{path}""#);
        }
        TargetAssembler::Fasm | TargetAssembler::FasmArm => {
            let _ = writeln!(out, "include '{path}'");
        }
        TargetAssembler::Jwasm | TargetAssembler::ArmAsm => {
            let _ = writeln!(out, "INCLUDE {path}");
        }
        TargetAssembler::LlvmMc => {
            let _ = writeln!(out, r#".include "{path}""#);
        }
    }
}

fn emit_global(
    out: &mut String,
    symbol: &str,
    module: &CanonicalModule,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) {
    if matches!(
        module.format,
        BinaryFormat::Com | BinaryFormat::Com3Part | BinaryFormat::Bin16
    ) || (module.format == BinaryFormat::Mz16 && target == TargetAssembler::Fasm)
    {
        return;
    }
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "GLOBAL {symbol}");
        }
        TargetAssembler::Fasm => {
            let is_direct_pe = (module.format == BinaryFormat::Coff64
                && fasm_mode != FasmPeMode::Coff64)
                || (module.format == BinaryFormat::Coff32 && fasm_mode == FasmPeMode::DirectPe32);
            let skip_public = is_direct_pe
                || (module.format == BinaryFormat::Coff32
                    && module.safeseh.as_deref() == Some(symbol));
            if !skip_public {
                let _ = writeln!(out, "public {symbol}");
            }
        }
        TargetAssembler::FasmArm => {}
        TargetAssembler::Jwasm => {
            let _ = writeln!(out, "PUBLIC {symbol}");
        }
        TargetAssembler::LlvmMc => {
            let _ = writeln!(out, ".global {symbol}");
        }
        TargetAssembler::ArmAsm => {
            let _ = writeln!(out, "    EXPORT {symbol}");
        }
    }
}

fn emit_extern(
    out: &mut String,
    module: &CanonicalModule,
    symbol: &str,
    kind: &str,
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) {
    let fasm_direct_pe64 = target == TargetAssembler::Fasm
        && fasm_mode != FasmPeMode::Coff64
        && (module.format == BinaryFormat::Coff64 || module.cpu == Some(CpuIsa::X86_64));
    let fasm_direct_pe32 = target == TargetAssembler::Fasm
        && fasm_mode == FasmPeMode::DirectPe32
        && (module.format == BinaryFormat::Coff32 || module.cpu == Some(CpuIsa::Ia32));
    if fasm_direct_pe64 || fasm_direct_pe32 || target == TargetAssembler::FasmArm {
        return;
    }
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "EXTERN {symbol}");
        }
        TargetAssembler::Fasm => {
            let fkind = match kind {
                "dword" => "dword",
                "qword" => "qword",
                _ => "near",
            };
            let _ = writeln!(out, "extrn '{symbol}' as {symbol}:{fkind}");
        }
        TargetAssembler::Jwasm => {
            let jkind = match kind {
                "dword" => "DWORD",
                "qword" => "QWORD",
                _ => "PROC",
            };
            let _ = writeln!(out, "EXTRN {symbol}:{jkind}");
        }
        TargetAssembler::LlvmMc | TargetAssembler::FasmArm => {}
        TargetAssembler::ArmAsm => {
            let _ = writeln!(out, "    IMPORT {symbol}");
        }
    }
}

fn emit_const(out: &mut String, name: &str, expr: &str, target: TargetAssembler) {
    let lowered_expr = rewrite_expr_tokens(expr, None, target);
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "{name} equ {lowered_expr}");
        }
        TargetAssembler::Fasm | TargetAssembler::FasmArm => {
            let _ = writeln!(out, "{name} = {lowered_expr}");
        }
        TargetAssembler::Jwasm | TargetAssembler::ArmAsm => {
            let _ = writeln!(out, "{name} EQU {lowered_expr}");
        }
        TargetAssembler::LlvmMc => {
            let _ = writeln!(out, ".equ {name}, {lowered_expr}");
        }
    }
}

fn emit_struc(
    out: &mut String,
    name: &str,
    expected_size: usize,
    fields: &[StrucField],
    target: TargetAssembler,
) {
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "STRUC {name}");
            for f in fields {
                let res_kw = match f.width {
                    DataWidth::Byte => "resb",
                    DataWidth::Word => "resw",
                    DataWidth::Dword => "resd",
                    DataWidth::Qword => "resq",
                };
                let _ = writeln!(out, "    .{}: {res_kw} {}", f.name, f.count);
            }
            out.push_str("ENDSTRUC");
            out.push(LF);
            let _ = writeln!(out, "%if {name}_size != {expected_size}");
            let _ = writeln!(out, r#"    %error "STRUC {name} size mismatch""#);
            out.push_str("%endif");
            out.push(LF);
            let _ = writeln!(out, "{name}_SIZE equ {expected_size}");
        }
        TargetAssembler::Fasm | TargetAssembler::FasmArm => {
            out.push_str("virtual at 0");
            out.push(LF);
            for f in fields {
                let r_kw = if target == TargetAssembler::FasmArm {
                    match f.width {
                        DataWidth::Byte => "rb",
                        DataWidth::Word => "rh",
                        DataWidth::Dword => "rw",
                        DataWidth::Qword => "rd",
                    }
                } else {
                    match f.width {
                        DataWidth::Byte => "rb",
                        DataWidth::Word => "rw",
                        DataWidth::Dword => "rd",
                        DataWidth::Qword => "rq",
                    }
                };
                if target == TargetAssembler::FasmArm {
                    let _ = writeln!(out, "    {name}.{} = ${LF}    {r_kw} {}", f.name, f.count);
                } else {
                    let _ = writeln!(out, "    {name}.{} {r_kw} {}", f.name, f.count);
                }
            }
            let _ = writeln!(out, "    {name}_SIZE = $");
            let _ = writeln!(out, "    assert {name}_SIZE = {expected_size}");
            out.push_str("end virtual");
            out.push(LF);
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(out, "{name} STRUCT 1");
            for f in fields {
                let d_kw = match f.width {
                    DataWidth::Byte => "DB",
                    DataWidth::Word => "DW",
                    DataWidth::Dword => "DD",
                    DataWidth::Qword => "DQ",
                };
                if f.count == 1 {
                    let _ = writeln!(out, "    {} {d_kw} ?", f.name);
                } else {
                    let _ = writeln!(out, "    {} {d_kw} {} DUP(?)", f.name, f.count);
                }
            }
            let _ = writeln!(out, "{name} ENDS");
            let _ = writeln!(out, "{name}_SIZE EQU SIZEOF {name}");
            let _ = writeln!(out, ".ERRNZ (SIZEOF {name} - {expected_size})");
        }
        TargetAssembler::LlvmMc | TargetAssembler::ArmAsm => {
            let mut offset = 0_usize;
            for f in fields {
                emit_const(
                    out,
                    &format!("{name}_{}", f.name),
                    &offset.to_string(),
                    target,
                );
                offset += f.width.byte_size() * f.count;
            }
            emit_const(
                out,
                &format!("{name}_SIZE"),
                &expected_size.to_string(),
                target,
            );
        }
    }
}

fn emit_proc(out: &mut String, req: &ProcEmitReq<'_>) -> Result<(), ToolError> {
    emit_proc_prologue(out, req)?;
    for stmt in req.body {
        emit_stmt(
            out,
            req.module,
            Some(req.name),
            Some(req.kind),
            stmt,
            req.active_seg_base,
            req.target,
        )?;
    }
    match (req.kind, req.target) {
        (ProcKind::Win64Frame { .. }, TargetAssembler::Nasm | TargetAssembler::Fasm)
        | (_, TargetAssembler::FasmArm) => {
            let _ = writeln!(out, "{}__end:", req.name);
        }
        (_, TargetAssembler::LlvmMc) => {
            let _ = writeln!(out, ".L{}__end:", req.name);
            if req.module.cpu == Some(CpuIsa::ArmV4) {
                let _ = writeln!(out, ".ifndef __WINCMD_THUMB2{LF}    .ltorg{LF}.endif");
            }
        }
        (_, TargetAssembler::Jwasm | TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "{} ENDP", req.name);
        }
        _ => {}
    }
    Ok(())
}

fn emit_proc_prologue(out: &mut String, req: &ProcEmitReq<'_>) -> Result<(), ToolError> {
    let module = req.module;
    let name = req.name;
    let kind = req.kind;
    let target = req.target;
    if matches!(kind, ProcKind::Win64Frame { .. }) && module.cpu != Some(CpuIsa::X86_64) {
        return Err(ToolError::Lowering {
            span: req.span.clone(),
            assembler: target,
            message: format!("@proc_frame `{name}` requires `@cpu x64`"),
        });
    }

    match (kind, target) {
        (
            ProcKind::Near | ProcKind::Far,
            TargetAssembler::Nasm | TargetAssembler::Fasm | TargetAssembler::FasmArm,
        ) => {
            let _ = writeln!(out, "{name}:");
        }
        (ProcKind::Near, TargetAssembler::Jwasm) => {
            if module.cpu == Some(CpuIsa::X86_64) || module.format == BinaryFormat::Coff32 {
                let _ = writeln!(out, "{name} PROC");
            } else {
                let _ = writeln!(out, "{name} PROC NEAR");
            }
        }
        (ProcKind::Far, TargetAssembler::Jwasm) => {
            let _ = writeln!(out, "{name} PROC FAR");
        }
        (ProcKind::Near | ProcKind::Far, TargetAssembler::LlvmMc) => {
            if module.cpu == Some(CpuIsa::ArmV7) {
                out.push_str(".thumb_func");
                out.push(LF);
            } else if module.cpu == Some(CpuIsa::ArmV4) {
                let _ = writeln!(out, ".ifdef __WINCMD_THUMB2{LF}.thumb_func{LF}.endif");
            }
            let _ = writeln!(out, "{name}:");
        }
        (ProcKind::Near | ProcKind::Far, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "{name} PROC");
        }
        (
            ProcKind::Win64Frame {
                stack_alloc,
                saved_regs,
            },
            _,
        ) => emit_win64_frame_prologue(out, req, *stack_alloc, saved_regs)?,
    }
    Ok(())
}

fn emit_win64_frame_prologue(
    out: &mut String,
    req: &ProcEmitReq<'_>,
    stack_alloc: u32,
    saved_regs: &[String],
) -> Result<(), ToolError> {
    let name = req.name;
    let is_glob = collect_coff64_gfids(req.module).contains(&name);
    match req.target {
        TargetAssembler::Nasm | TargetAssembler::Fasm => {
            let _ = writeln!(out, "{name}:");
            if is_glob {
                let _ = writeln!(out, "    db 0xF3, 0x0F, 0x1E, 0xFA");
            }
            for (idx, reg) in saved_regs.iter().enumerate() {
                let _ = writeln!(out, "    push {reg}");
                let _ = writeln!(out, "{name}__push_{idx}:");
            }
            if stack_alloc >= 4096 {
                let _ = writeln!(
                    out,
                    "    mov eax, {stack_alloc}{LF}    call __chkstk{LF}    sub rsp, rax"
                );
            } else if stack_alloc > 0 {
                let _ = writeln!(out, "    sub rsp, {stack_alloc}");
            }
            let _ = writeln!(out, "{name}__prolog_end:");
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(out, "{name} PROC FRAME");
            if is_glob {
                let _ = writeln!(out, "    DB 0F3h, 0Fh, 1Eh, 0FAh");
            }
            for reg in saved_regs {
                let _ = writeln!(out, "    push {reg}");
                let _ = writeln!(out, "    .PUSHREG {reg}");
            }
            if stack_alloc >= 4096 {
                let _ = writeln!(
                    out,
                    "    mov eax, {stack_alloc}{LF}    call __chkstk{LF}    sub rsp, rax"
                );
                let _ = writeln!(out, "    .ALLOCSTACK {stack_alloc}");
            } else if stack_alloc > 0 {
                let _ = writeln!(out, "    sub rsp, {stack_alloc}");
                let _ = writeln!(out, "    .ALLOCSTACK {stack_alloc}");
            }
            out.push_str("    .ENDPROLOG");
            out.push(LF);
        }
        TargetAssembler::FasmArm | TargetAssembler::LlvmMc | TargetAssembler::ArmAsm => {
            return Err(ToolError::Lowering {
                span: req.span.clone(),
                assembler: req.target,
                message: "@proc_frame is only valid for x86-64 assemblers".into(),
            });
        }
    }
    Ok(())
}

fn emit_stmt(
    out: &mut String,
    module: &CanonicalModule,
    proc_name: Option<&str>,
    proc_kind: Option<&ProcKind>,
    stmt: &Stmt,
    active_seg_base: &str,
    target: TargetAssembler,
) -> Result<(), ToolError> {
    let c_pfx = comment_prefix(target);
    match stmt {
        Stmt::Comment { text, .. } => {
            let _ = writeln!(out, "    {c_pfx} {text}");
        }
        Stmt::LocalLabel { name, .. } => {
            let pname = proc_name.unwrap_or("anon");
            match target {
                TargetAssembler::Nasm => {
                    let _ = writeln!(out, ".{name}:");
                }
                TargetAssembler::Fasm | TargetAssembler::Jwasm | TargetAssembler::FasmArm => {
                    let _ = writeln!(out, "{pname}__{name}:");
                }
                TargetAssembler::LlvmMc => {
                    let _ = writeln!(out, ".L{pname}__{name}:");
                }
                TargetAssembler::ArmAsm => {
                    let _ = writeln!(out, "|{pname}__{name}|");
                }
            }
        }
        Stmt::GlobalLabel { name, .. } => {
            if target == TargetAssembler::ArmAsm {
                let _ = writeln!(out, "{name}");
            } else {
                let _ = writeln!(out, "{name}:");
            }
        }
        Stmt::Data {
            label,
            width,
            items,
            ..
        } => {
            emit_data(out, label.as_deref(), *width, items, proc_name, target);
        }
        Stmt::Reserve {
            label,
            width,
            count_expr,
            ..
        } => {
            emit_reserve(out, module, label, *width, count_expr, target);
        }
        Stmt::AlignZero { align_bytes, .. } => {
            emit_align_zero(out, *align_bytes, active_seg_base, target);
        }
        Stmt::Win64EpilogRet { span } => {
            let Some(ProcKind::Win64Frame {
                stack_alloc,
                saved_regs,
            }) = proc_kind
            else {
                return Err(ToolError::Lowering {
                    span: span.clone(),
                    assembler: target,
                    message: "@epilog_ret is only valid inside `@proc_frame`".into(),
                });
            };
            if *stack_alloc > 0 {
                let _ = writeln!(out, "    lea rsp, [rsp + {stack_alloc}]");
            }
            for reg in saved_regs.iter().rev() {
                let _ = writeln!(out, "    pop {reg}");
            }
            out.push_str("    ret");
            out.push(LF);
        }
        Stmt::Instruction {
            mnemonic, operands, ..
        } => {
            emit_instruction(out, module, proc_name, mnemonic, operands, target);
        }
    }
    Ok(())
}

fn emit_data(
    out: &mut String,
    label: Option<&str>,
    width: DataWidth,
    items: &str,
    proc_name: Option<&str>,
    target: TargetAssembler,
) {
    let lowered_items = rewrite_expr_tokens(items, proc_name, target);
    match target {
        TargetAssembler::Nasm | TargetAssembler::Fasm | TargetAssembler::FasmArm => {
            let kw = if target == TargetAssembler::FasmArm {
                match width {
                    DataWidth::Byte => "db",
                    DataWidth::Word => "dh",
                    DataWidth::Dword => "dw",
                    DataWidth::Qword => "dd",
                }
            } else {
                match width {
                    DataWidth::Byte => "db",
                    DataWidth::Word => "dw",
                    DataWidth::Dword => "dd",
                    DataWidth::Qword => "dq",
                }
            };
            if let Some(lbl) = label {
                let _ = writeln!(out, "{lbl}: {kw} {lowered_items}");
            } else {
                let _ = writeln!(out, "    {kw} {lowered_items}");
            }
        }
        TargetAssembler::Jwasm => {
            let kw = match width {
                DataWidth::Byte => "DB",
                DataWidth::Word => "DW",
                DataWidth::Dword => "DD",
                DataWidth::Qword => "DQ",
            };
            if let Some(lbl) = label {
                let _ = writeln!(out, "{lbl} {kw} {lowered_items}");
            } else {
                let _ = writeln!(out, "    {kw} {lowered_items}");
            }
        }
        TargetAssembler::LlvmMc => {
            if let Some(lbl) = label {
                let _ = writeln!(out, "{lbl}:");
            }
            let kw = match width {
                DataWidth::Byte => ".byte",
                DataWidth::Word => ".hword",
                DataWidth::Dword => ".word",
                DataWidth::Qword => ".xword",
            };
            let gas_items = if width == DataWidth::Byte {
                expand_string_bytes_for_gas(&lowered_items)
            } else {
                lowered_items
            };
            let _ = writeln!(out, "    {kw} {gas_items}");
        }
        TargetAssembler::ArmAsm => {
            let kw = match width {
                DataWidth::Byte => "DCB",
                DataWidth::Word => "DCW",
                DataWidth::Dword => "DCD",
                DataWidth::Qword => "DCQ",
            };
            if let Some(lbl) = label {
                let _ = writeln!(out, "{lbl} {kw} {lowered_items}");
            } else {
                let _ = writeln!(out, "    {kw} {lowered_items}");
            }
        }
    }
}

fn expand_string_bytes_for_gas(items: &str) -> String {
    let mut out = String::with_capacity(items.len() * 2);
    let mut in_dq = false;
    let mut first_in_str = true;
    for b in items.bytes() {
        if b == b'"' {
            in_dq = !in_dq;
            first_in_str = true;
        } else if in_dq {
            if !first_in_str {
                out.push_str(", ");
            }
            let _ = write!(out, "0x{b:02X}");
            first_in_str = false;
        } else {
            out.push(char::from(b));
        }
    }
    out
}

fn emit_reserve(
    out: &mut String,
    module: &CanonicalModule,
    label: &str,
    width: DataWidth,
    count_expr: &str,
    target: TargetAssembler,
) {
    let fmt = module.format;
    let cnt = rewrite_expr_tokens(count_expr, None, target);
    let in_flat_bin = matches!(
        fmt,
        BinaryFormat::Com | BinaryFormat::Com3Part | BinaryFormat::Bin16 | BinaryFormat::Mz16
    ) || (fmt == BinaryFormat::Include
        && matches!(module.cpu, Some(CpuIsa::X86_8086 | CpuIsa::X86_286)));
    match target {
        TargetAssembler::Nasm => {
            if in_flat_bin {
                let kw = match width {
                    DataWidth::Byte => "db",
                    DataWidth::Word => "dw",
                    DataWidth::Dword => "dd",
                    DataWidth::Qword => "dq",
                };
                let _ = writeln!(out, "{label}: times ({cnt}) {kw} 0");
            } else {
                let kw = match width {
                    DataWidth::Byte => "resb",
                    DataWidth::Word => "resw",
                    DataWidth::Dword => "resd",
                    DataWidth::Qword => "resq",
                };
                let _ = writeln!(out, "{label}: {kw} {cnt}");
            }
        }
        TargetAssembler::Fasm | TargetAssembler::FasmArm => {
            if in_flat_bin && target == TargetAssembler::Fasm {
                let elem_bytes = width.byte_size();
                let _ = writeln!(out, "{label}:");
                let _ = writeln!(out, "repeat (({cnt}) * {elem_bytes})");
                out.push_str("    db 0");
                out.push(LF);
                out.push_str("end repeat");
                out.push(LF);
            } else {
                let kw = if target == TargetAssembler::FasmArm {
                    match width {
                        DataWidth::Byte => "rb",
                        DataWidth::Word => "rh",
                        DataWidth::Dword => "rw",
                        DataWidth::Qword => "rd",
                    }
                } else {
                    match width {
                        DataWidth::Byte => "rb",
                        DataWidth::Word => "rw",
                        DataWidth::Dword => "rd",
                        DataWidth::Qword => "rq",
                    }
                };
                let _ = writeln!(out, "{label} {kw} {cnt}");
            }
        }
        TargetAssembler::Jwasm => {
            let kw = match width {
                DataWidth::Byte => "DB",
                DataWidth::Word => "DW",
                DataWidth::Dword => "DD",
                DataWidth::Qword => "DQ",
            };
            let fill = if in_flat_bin { "0" } else { "?" };
            let _ = writeln!(out, "{label} {kw} ({cnt}) DUP({fill})");
        }
        TargetAssembler::LlvmMc => {
            let elem_bytes = width.byte_size();
            let _ = writeln!(out, "{label}: .space (({cnt}) * {elem_bytes}), 0");
        }
        TargetAssembler::ArmAsm => {
            let elem_bytes = width.byte_size();
            let _ = writeln!(out, "{label} SPACE (({cnt}) * {elem_bytes})");
        }
    }
}

fn emit_align_zero(
    out: &mut String,
    align_bytes: usize,
    active_seg_base: &str,
    target: TargetAssembler,
) {
    let mask = align_bytes - 1;
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "align {align_bytes}, db 0");
        }
        TargetAssembler::Fasm => {
            let _ = writeln!(
                out,
                "repeat (({align_bytes} - (($ - {active_seg_base}) and {mask})) and {mask})"
            );
            out.push_str("    db 0");
            out.push(LF);
            out.push_str("end repeat");
            out.push(LF);
        }
        TargetAssembler::FasmArm => {
            let _ = writeln!(out, "    align {align_bytes}");
        }
        TargetAssembler::Jwasm => {
            let _ = writeln!(
                out,
                "DB (({align_bytes} - (($ - {active_seg_base}) AND {mask})) AND {mask}) DUP(0)"
            );
        }
        TargetAssembler::LlvmMc => {
            let p2 = align_bytes.trailing_zeros();
            let _ = writeln!(out, "    .p2align {p2}, 0");
        }
        TargetAssembler::ArmAsm => {
            let _ = writeln!(out, "    ALIGN {align_bytes}");
        }
    }
}

fn emit_instruction(
    out: &mut String,
    module: &CanonicalModule,
    proc_name: Option<&str>,
    mnemonic: &str,
    operands: &[String],
    target: TargetAssembler,
) {
    let mut mnem = mnemonic.to_owned();
    if target == TargetAssembler::Jwasm
        && matches!(module.cpu, Some(CpuIsa::X86_8086 | CpuIsa::X86_286))
        && mnem == "retf"
        && operands.is_empty()
    {
        mnem = "db 0CBh".into();
        let _ = writeln!(out, "    {mnem}");
        return;
    }

    if mnem.eq_ignore_ascii_case("endbr64") && operands.is_empty() {
        if target == TargetAssembler::Jwasm {
            let _ = writeln!(out, "    DB 0F3h, 0Fh, 1Eh, 0FAh");
        } else {
            let _ = writeln!(out, "    db 0xF3, 0x0F, 0x1E, 0xFA");
        }
        return;
    }

    if matches!(
        module.cpu,
        Some(CpuIsa::ArmV4 | CpuIsa::ArmV7 | CpuIsa::Arm64)
    ) && try_emit_arm_pseudo(out, module.cpu, proc_name, &mnem, operands, target)
    {
        return;
    }

    if operands.is_empty() {
        let _ = writeln!(out, "    {mnem}");
        return;
    }

    let lowered_ops: Vec<String> = operands
        .iter()
        .map(|op| lower_operand(op, module.cpu, proc_name, target))
        .collect();
    if matches!(module.cpu, Some(CpuIsa::X86_8086 | CpuIsa::X86_286))
        && matches!(target, TargetAssembler::Nasm | TargetAssembler::Fasm)
        && lowered_ops.len() == 1
        && is_x86_cond_jump(&mnem)
        && !lowered_ops[0].starts_with("short ")
    {
        let _ = writeln!(out, "    {mnem} short {}", lowered_ops[0]);
        return;
    }
    let _ = writeln!(out, "    {mnem} {}", lowered_ops.join(", "));
}

fn try_emit_arm_pseudo(
    out: &mut String,
    cpu: Option<CpuIsa>,
    proc_name: Option<&str>,
    mnem: &str,
    operands: &[String],
    target: TargetAssembler,
) -> bool {
    let m = mnem.to_ascii_lowercase();
    if cpu == Some(CpuIsa::Arm64) && m == "uxtw" && operands.len() == 2 {
        let rd = operands[0].trim();
        let rn = operands[1].trim();
        let wd = rd
            .strip_prefix('x')
            .or_else(|| rd.strip_prefix('X'))
            .map_or_else(|| rd.to_owned(), |rest| format!("w{rest}"));
        let _ = writeln!(out, "    mov {wd}, {rn}");
        return true;
    }
    if m == "adr_sym" && operands.len() == 2 {
        let rd = operands[0].trim();
        let sym = rewrite_expr_tokens(operands[1].trim(), proc_name, target);
        emit_arm_adr_sym(out, cpu, rd, &sym, target);
        return true;
    }
    if m == "mov_imm32" && operands.len() == 2 {
        let rd = operands[0].trim();
        let raw_imm = operands[1].trim().trim_start_matches('#').trim();
        emit_arm_mov_imm32(out, cpu, rd, raw_imm, proc_name, target);
        return true;
    }
    if m == "call_iat" && operands.len() == 1 {
        let sym = rewrite_expr_tokens(operands[0].trim(), proc_name, target);
        emit_arm_call_iat(out, cpu, &sym, target);
        return true;
    }
    false
}

fn emit_arm_adr_sym(
    out: &mut String,
    cpu: Option<CpuIsa>,
    rd: &str,
    sym: &str,
    target: TargetAssembler,
) {
    match (cpu, target) {
        (Some(CpuIsa::ArmV4), TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "if defined __WINCMD_THUMB2{LF}    movw {rd}, ({sym}) and 0xFFFF{LF}    movt {rd}, (({sym}) shr 16) and 0xFFFF{LF}else{LF}    mov {rd}, ({sym}) and 0xFF{LF}    orr {rd}, {rd}, ({sym}) and 0xFF00{LF}    orr {rd}, {rd}, ({sym}) and 0xFF0000{LF}    orr {rd}, {rd}, ({sym}) and 0xFF000000{LF}end if"
            );
        }
        (Some(CpuIsa::ArmV4), TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                ".ifdef __WINCMD_THUMB2{LF}    movw {rd}, :lower16:{sym}{LF}    movt {rd}, :upper16:{sym}{LF}.else{LF}    ldr {rd}, ={sym}{LF}.endif"
            );
        }
        (Some(CpuIsa::ArmV4), _) => {
            let _ = writeln!(out, "    ldr {rd}, ={sym}");
        }
        (Some(CpuIsa::ArmV7), TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "    movw {rd}, ({sym}) and 0xFFFF{LF}    movt {rd}, (({sym}) shr 16) and 0xFFFF"
            );
        }
        (Some(CpuIsa::ArmV7), TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                "    movw {rd}, :lower16:{sym}{LF}    movt {rd}, :upper16:{sym}"
            );
        }
        (Some(CpuIsa::ArmV7), _) => {
            let _ = writeln!(out, "    movw {rd}, {sym}{LF}    movt {rd}, {sym}");
        }
        (Some(CpuIsa::Arm64), TargetAssembler::FasmArm) => {
            let _ = writeln!(
                out,
                "    adrp {rd}, ({sym} - ((rva {sym}) and 0xFFF) + ((rva $) and 0xFFF)){LF}    add {rd}, {rd}, (rva {sym}) and 0xFFF"
            );
        }
        (Some(CpuIsa::Arm64), TargetAssembler::LlvmMc) => {
            let _ = writeln!(
                out,
                "    adrp {rd}, {sym}{LF}    add {rd}, {rd}, :lo12:{sym}"
            );
        }
        _ => {
            let _ = writeln!(out, "    adrp {rd}, {sym}{LF}    add {rd}, {rd}, {sym}");
        }
    }
}

fn parse_u32_imm(s: &str) -> Option<u32> {
    s.strip_prefix("0x")
        .or_else(|| s.strip_prefix("0X"))
        .map_or_else(
            || s.parse::<u32>().ok(),
            |hex| u32::from_str_radix(hex, 16).ok(),
        )
}

fn emit_arm_mov_imm32(
    out: &mut String,
    cpu: Option<CpuIsa>,
    rd: &str,
    raw_imm: &str,
    proc_name: Option<&str>,
    target: TargetAssembler,
) {
    let pfx = if target == TargetAssembler::FasmArm {
        ""
    } else {
        "#"
    };
    if let Some(val) = parse_u32_imm(raw_imm) {
        match cpu {
            Some(CpuIsa::ArmV4) => {
                let b0 = val & 0xFF;
                let _ = writeln!(out, "    mov {rd}, {pfx}0x{b0:02X}");
                for shift in [8_u32, 16, 24] {
                    let part = val & (0xFF << shift);
                    if part != 0 {
                        let _ = writeln!(out, "    orr {rd}, {rd}, {pfx}0x{part:08X}");
                    }
                }
            }
            Some(CpuIsa::ArmV7) => {
                let lo = val & 0xFFFF;
                let hi = (val >> 16) & 0xFFFF;
                let _ = writeln!(out, "    movw {rd}, {pfx}0x{lo:04X}");
                if hi != 0 {
                    let _ = writeln!(out, "    movt {rd}, {pfx}0x{hi:04X}");
                }
            }
            _ => {
                let lo = val & 0xFFFF;
                let hi = (val >> 16) & 0xFFFF;
                let _ = writeln!(out, "    movz {rd}, {pfx}0x{lo:04X}");
                if hi != 0 {
                    let _ = writeln!(out, "    movk {rd}, {pfx}0x{hi:04X}, lsl {pfx}16");
                }
            }
        }
        return;
    }
    let expr = rewrite_expr_tokens(raw_imm, proc_name, target);
    match cpu {
        Some(CpuIsa::ArmV4) => {
            let (and_op, _) = if target == TargetAssembler::FasmArm {
                ("and", "")
            } else {
                ("&", "#")
            };
            let _ = writeln!(
                out,
                "    mov {rd}, {pfx}(({expr}) {and_op} 0xFF){LF}    orr {rd}, {rd}, {pfx}(({expr}) {and_op} 0xFF00){LF}    orr {rd}, {rd}, {pfx}(({expr}) {and_op} 0xFF0000){LF}    orr {rd}, {rd}, {pfx}(({expr}) {and_op} 0xFF000000)"
            );
        }
        Some(CpuIsa::ArmV7) => {
            let (and_op, shr_op) = if target == TargetAssembler::FasmArm {
                ("and", "shr")
            } else {
                ("&", ">>")
            };
            let _ = writeln!(
                out,
                "    movw {rd}, {pfx}(({expr}) {and_op} 0xFFFF){LF}    movt {rd}, {pfx}((({expr}) {shr_op} 16) {and_op} 0xFFFF)"
            );
        }
        _ => {
            let (and_op, shr_op) = if target == TargetAssembler::FasmArm {
                ("and", "shr")
            } else {
                ("&", ">>")
            };
            let _ = writeln!(
                out,
                "    movz {rd}, {pfx}(({expr}) {and_op} 0xFFFF){LF}    movk {rd}, {pfx}((({expr}) {shr_op} 16) {and_op} 0xFFFF), lsl {pfx}16"
            );
        }
    }
}

fn emit_arm_call_iat(out: &mut String, cpu: Option<CpuIsa>, sym: &str, target: TargetAssembler) {
    match cpu {
        Some(CpuIsa::ArmV4) => {
            emit_arm_adr_sym(out, cpu, "r12", sym, target);
            let _ = writeln!(out, "    ldr r12, [r12]{LF}    mov lr, pc{LF}    bx r12");
        }
        Some(CpuIsa::ArmV7) => {
            emit_arm_adr_sym(out, cpu, "r12", sym, target);
            let _ = writeln!(out, "    ldr r12, [r12]{LF}    blx r12");
        }
        _ => match target {
            TargetAssembler::FasmArm => {
                let _ = writeln!(
                    out,
                    "    adrp x16, ({sym} - ((rva {sym}) and 0xFFF) + ((rva $) and 0xFFF)){LF}    ldr x16, [x16, (rva {sym}) and 0xFFF]{LF}    blr x16"
                );
            }
            TargetAssembler::LlvmMc => {
                let _ = writeln!(
                    out,
                    "    adrp x16, {sym}{LF}    ldr x16, [x16, :lo12:{sym}]{LF}    blr x16"
                );
            }
            _ => {
                let _ = writeln!(
                    out,
                    "    adrp x16, {sym}{LF}    ldr x16, [x16, {sym}]{LF}    blr x16"
                );
            }
        },
    }
}

fn is_x86_cond_jump(mnem: &str) -> bool {
    matches!(
        mnem.to_ascii_lowercase().as_str(),
        "jo" | "jno"
            | "jb"
            | "jnb"
            | "jae"
            | "jc"
            | "jnc"
            | "je"
            | "jz"
            | "jne"
            | "jnz"
            | "jbe"
            | "jna"
            | "ja"
            | "jnbe"
            | "js"
            | "jns"
            | "jp"
            | "jpe"
            | "jnp"
            | "jpo"
            | "jl"
            | "jnge"
            | "jge"
            | "jnl"
            | "jle"
            | "jng"
            | "jg"
            | "jnle"
    )
}

fn has_16bit_addr_reg(expr: &str) -> bool {
    expr.split(|c: char| !c.is_ascii_alphanumeric() && c != '_')
        .any(|tok| matches!(tok.to_ascii_lowercase().as_str(), "bx" | "bp" | "si" | "di"))
}

fn lower_operand(
    op: &str,
    cpu: Option<CpuIsa>,
    proc_name: Option<&str>,
    target: TargetAssembler,
) -> String {
    let t = op.trim();

    if let Some(inner) = t.strip_prefix("addr(").and_then(|s| s.strip_suffix(')')) {
        let inner_rw = rewrite_expr_tokens(inner.trim(), proc_name, target);
        return match target {
            TargetAssembler::Jwasm => format!("OFFSET {inner_rw}"),
            _ => inner_rw,
        };
    }

    if t.len() > 4 && t[..4].eq_ignore_ascii_case("far ") {
        let rest = t[4..].trim();
        let mem = lower_bracket_operand(rest, cpu, proc_name, target);
        return match target {
            TargetAssembler::Fasm => format!("dword {mem}"),
            TargetAssembler::Jwasm => format!("DWORD PTR {mem}"),
            _ => format!("far {mem}"),
        };
    }

    for (canon_kw, jw_kw) in [
        ("byte ", "BYTE PTR "),
        ("word ", "WORD PTR "),
        ("dword ", "DWORD PTR "),
        ("qword ", "QWORD PTR "),
    ] {
        if t.len() > canon_kw.len() && t[..canon_kw.len()].eq_ignore_ascii_case(canon_kw) {
            let rest = t[canon_kw.len()..].trim();
            let mem = lower_bracket_operand(rest, cpu, proc_name, target);
            return match target {
                TargetAssembler::Jwasm => format!("{jw_kw}{mem}"),
                _ => format!("{canon_kw}{mem}"),
            };
        }
    }

    if t.starts_with('[') && t.ends_with(']') {
        return lower_bracket_operand(t, cpu, proc_name, target);
    }

    if target == TargetAssembler::FasmArm
        && let Some(imm) = t.strip_prefix('#')
    {
        return rewrite_expr_tokens(imm, proc_name, target);
    }

    rewrite_expr_tokens(t, proc_name, target)
}

fn lower_bracket_operand(
    mem: &str,
    cpu: Option<CpuIsa>,
    proc_name: Option<&str>,
    target: TargetAssembler,
) -> String {
    let Some(inner) = mem.strip_prefix('[').and_then(|s| s.strip_suffix(']')) else {
        return rewrite_expr_tokens(mem, proc_name, target);
    };
    let inner_trim = inner.trim();

    if let Some(rel_sym) = inner_trim.strip_prefix("rel ") {
        let sym = rewrite_expr_tokens(rel_sym.trim(), proc_name, target);
        return if target == TargetAssembler::Nasm {
            format!("[rel {sym}]")
        } else {
            format!("[{sym}]")
        };
    }

    for seg in ["cs:", "ds:", "es:", "ss:", "fs:", "gs:"] {
        if inner_trim.len() > seg.len() && inner_trim[..seg.len()].eq_ignore_ascii_case(seg) {
            let expr = rewrite_expr_tokens(inner_trim[seg.len()..].trim(), proc_name, target);
            return match target {
                TargetAssembler::Jwasm => format!("{seg}[{expr}]"),
                _ => format!("[{seg}{expr}]"),
            };
        }
    }

    let rw = rewrite_expr_tokens(inner_trim, proc_name, target);
    if target == TargetAssembler::Jwasm
        && matches!(cpu, Some(CpuIsa::X86_8086 | CpuIsa::X86_286))
        && !has_16bit_addr_reg(inner_trim)
    {
        return format!("ds:[{rw}]");
    }
    format!("[{rw}]")
}

/// Rewrites hex literals (`0x1A` -> `01Ah` on `JWasm`) and local label tokens (`.lbl`)
/// inside an operand or constant expression while preserving string literals untouched.
#[must_use]
pub fn rewrite_expr_tokens(expr: &str, proc_name: Option<&str>, target: TargetAssembler) -> String {
    let chars: Vec<char> = expr.chars().collect();
    let mut out = String::with_capacity(expr.len() + 8);
    let mut i = 0_usize;
    let mut in_single = false;
    let mut in_double = false;

    while i < chars.len() {
        let ch = chars[i];
        if ch == SQUOTE && !in_double {
            in_single = !in_single;
            out.push(ch);
            i += 1;
            continue;
        }
        if ch == '"' && !in_single {
            in_double = !in_double;
            out.push(ch);
            i += 1;
            continue;
        }
        if in_single || in_double {
            out.push(ch);
            i += 1;
            continue;
        }

        // Hex literal `0x...` for JWasm -> `0...h`
        if target == TargetAssembler::Jwasm
            && ch == '0'
            && i + 2 < chars.len()
            && (chars[i + 1] == 'x' || chars[i + 1] == 'X')
            && chars[i + 2].is_ascii_hexdigit()
        {
            let prev_is_ident =
                i > 0 && (chars[i - 1].is_ascii_alphanumeric() || chars[i - 1] == '_');
            if !prev_is_ident {
                let mut j = i + 2;
                while j < chars.len() && chars[j].is_ascii_hexdigit() {
                    j += 1;
                }
                out.push('0');
                for &hc in &chars[(i + 2)..j] {
                    out.push(hc);
                }
                out.push('h');
                i = j;
                continue;
            }
        }

        // Local label `.foo` (when NOT preceded by identifier char like `PSP.wEnvSeg`)
        if ch == '.'
            && i + 1 < chars.len()
            && (chars[i + 1].is_ascii_alphabetic() || chars[i + 1] == '_')
        {
            let prev_is_ident = i > 0
                && (chars[i - 1].is_ascii_alphanumeric()
                    || chars[i - 1] == '_'
                    || chars[i - 1] == '$');
            if !prev_is_ident {
                let mut j = i + 1;
                while j < chars.len() && (chars[j].is_ascii_alphanumeric() || chars[j] == '_') {
                    j += 1;
                }
                let local: String = chars[(i + 1)..j].iter().collect();
                let pname = proc_name.unwrap_or("anon");
                match target {
                    TargetAssembler::Nasm => {
                        out.push('.');
                        out.push_str(&local);
                    }
                    TargetAssembler::Fasm | TargetAssembler::Jwasm | TargetAssembler::FasmArm => {
                        let _ = write!(out, "{pname}__{local}");
                    }
                    TargetAssembler::LlvmMc => {
                        let _ = write!(out, ".L{pname}__{local}");
                    }
                    TargetAssembler::ArmAsm => {
                        let _ = write!(out, "|{pname}__{local}|");
                    }
                }
                i = j;
                continue;
            } else if matches!(target, TargetAssembler::LlvmMc | TargetAssembler::ArmAsm) {
                out.push('_');
                i += 1;
                continue;
            }
        }

        out.push(ch);
        i += 1;
    }

    out
}

fn win64_reg_code(reg: &str) -> u8 {
    match reg {
        "rcx" => 1,
        "rdx" => 2,
        "rbx" => 3,
        "rsp" => 4,
        "rbp" => 5,
        "rsi" => 6,
        "rdi" => 7,
        "r8" => 8,
        "r9" => 9,
        "r10" => 10,
        "r11" => 11,
        "r12" => 12,
        "r13" => 13,
        "r14" => 14,
        "r15" => 15,
        _ => 0,
    }
}

fn emit_win64_unwind_tables(
    out: &mut String,
    frames: &[(String, u32, Vec<String>)],
    target: TargetAssembler,
    fasm_mode: FasmPeMode,
) {
    match target {
        TargetAssembler::Nasm => {
            out.push_str("SECTION .xdata align=4 rdata");
            out.push(LF);
            for (name, stack_alloc, saved_regs) in frames {
                emit_unwind_info_bytes(out, name, *stack_alloc, saved_regs, true);
            }
            out.push_str("SECTION .pdata align=4 rdata");
            out.push(LF);
            for (name, _, _) in frames {
                let _ = writeln!(out, "    dd {name} wrt ..imagebase");
                let _ = writeln!(out, "    dd {name}__end wrt ..imagebase");
                let _ = writeln!(out, "    dd {name}__unwind wrt ..imagebase");
            }
        }
        TargetAssembler::Fasm if fasm_mode == FasmPeMode::Coff64 => {}
        TargetAssembler::Fasm => {
            out.push_str("section '.xdata' data readable");
            out.push(LF);
            for (name, stack_alloc, saved_regs) in frames {
                emit_unwind_info_bytes(out, name, *stack_alloc, saved_regs, false);
            }
            out.push_str("section '.pdata' data readable");
            out.push(LF);
            if fasm_mode != FasmPeMode::Coff64 {
                out.push_str("data 3");
                out.push(LF);
            }
            for (name, _, _) in frames {
                let _ = writeln!(out, "    dd rva {name}");
                let _ = writeln!(out, "    dd rva {name}__end");
                let _ = writeln!(out, "    dd rva {name}__unwind");
            }
            if fasm_mode != FasmPeMode::Coff64 {
                out.push_str("end data");
                out.push(LF);
            }
        }
        _ => {}
    }
}

fn emit_fasm_pe32_idata_and_reloc(out: &mut String, module: &CanonicalModule) {
    let imps: Vec<&str> = module
        .items
        .iter()
        .filter_map(|it| {
            if let TopItem::Extern { symbol, .. } = it {
                symbol.strip_prefix("__imp__")
            } else {
                None
            }
        })
        .collect();
    if !imps.is_empty() {
        let _ = writeln!(
            out,
            "section {SQUOTE}.idata{SQUOTE} import data readable writeable{LF}    dd rva wincmd_k32_ilt, 0, 0, rva wincmd_k32_name, rva wincmd_k32_iat{LF}    dd 0, 0, 0, 0, 0{LF}wincmd_k32_ilt:"
        );
        for idx in 0..imps.len() {
            let _ = writeln!(out, "    dd rva wincmd_hn_{idx}");
        }
        let _ = writeln!(out, "    dd 0{LF}wincmd_k32_iat:");
        for (idx, raw) in imps.iter().enumerate() {
            let _ = writeln!(out, "    __imp__{raw} dd rva wincmd_hn_{idx}");
        }
        let _ = writeln!(
            out,
            "    dd 0{LF}wincmd_k32_name db {SQUOTE}KERNEL32.dll{SQUOTE}, 0"
        );
        for (idx, raw) in imps.iter().enumerate() {
            let clean = raw.split('@').next().unwrap_or(raw);
            let _ = writeln!(
                out,
                "    align 2{LF}wincmd_hn_{idx}:{LF}    dw 0{LF}    db {SQUOTE}{clean}{SQUOTE}, 0"
            );
        }
    }
    let _ = writeln!(
        out,
        "section {SQUOTE}.reloc{SQUOTE} fixups data readable discardable"
    );
}

fn emit_fasm_pe64_idata(out: &mut String, module: &CanonicalModule) {
    let imps: Vec<&str> = module
        .items
        .iter()
        .filter_map(|it| {
            if let TopItem::Extern { symbol, .. } = it {
                symbol.strip_prefix("__imp_")
            } else {
                None
            }
        })
        .collect();
    if imps.is_empty() {
        return;
    }
    out.push_str("section '.idata' import data readable writeable");
    out.push(LF);
    out.push_str("    dd rva wincmd_k32_ilt, 0, 0, rva wincmd_k32_name, rva wincmd_k32_iat");
    out.push(LF);
    out.push_str("    dd 0, 0, 0, 0, 0");
    out.push(LF);
    out.push_str("wincmd_k32_ilt:");
    out.push(LF);
    for api in &imps {
        let _ = writeln!(out, "    dq rva wincmd_hn_{api}");
    }
    out.push_str("    dq 0");
    out.push(LF);
    out.push_str("wincmd_k32_iat:");
    out.push(LF);
    for api in &imps {
        let _ = writeln!(out, "    __imp_{api} dq rva wincmd_hn_{api}");
    }
    out.push_str("    dq 0");
    out.push(LF);
    out.push_str("wincmd_k32_name db 'KERNEL32.dll', 0");
    out.push(LF);
    for api in &imps {
        out.push_str("    align 2");
        out.push(LF);
        let _ = writeln!(out, "wincmd_hn_{api}:");
        out.push_str("    dw 0");
        out.push(LF);
        let _ = writeln!(out, "    db '{api}', 0");
    }
}

fn emit_unwind_info_bytes(
    out: &mut String,
    name: &str,
    stack_alloc: u32,
    saved_regs: &[String],
    is_nasm: bool,
) {
    // UNWIND_CODE entries in reverse prologue order:
    // 1. stack_alloc (UWOP_ALLOC_SMALL = 2 if <= 128, UWOP_ALLOC_LARGE = 1 otherwise)
    // 2. saved_regs in reverse order (UWOP_PUSH_NONVOL = 0)
    let mut codes: Vec<(String, u8, Option<u16>)> = Vec::new();
    if stack_alloc > 0 {
        if stack_alloc <= 128 {
            let info = u8::try_from((stack_alloc / 8) - 1).unwrap_or(0);
            codes.push((
                format!("{name}__prolog_end - {name}"),
                (info << 4) | 2,
                None,
            ));
        } else {
            let slots = u16::try_from(stack_alloc / 8).unwrap_or(0);
            codes.push((format!("{name}__prolog_end - {name}"), 1, Some(slots)));
        }
    }
    for (idx, reg) in saved_regs.iter().enumerate().rev() {
        let rcode = win64_reg_code(reg);
        codes.push((format!("{name}__push_{idx} - {name}"), rcode << 4, None));
    }

    let code_slot_count: usize = codes
        .iter()
        .map(|(_, _, extra)| if extra.is_some() { 2 } else { 1 })
        .sum();

    let _ = writeln!(out, "{name}__unwind:");
    out.push_str("    db 1");
    out.push(LF);
    let _ = writeln!(out, "    db ({name}__prolog_end - {name})");
    let _ = writeln!(out, "    db {code_slot_count}");
    out.push_str("    db 0");
    out.push(LF);
    for (off_expr, op_byte, extra_word) in &codes {
        let _ = writeln!(out, "    db ({off_expr}), 0x{op_byte:02X}");
        if let Some(w) = extra_word {
            let _ = writeln!(out, "    dw 0x{w:04X}");
        }
    }
    if code_slot_count % 2 == 1 {
        out.push_str("    dw 0");
        out.push(LF);
    }
    if is_nasm {
        out.push_str("    align 4, db 0");
        out.push(LF);
    }
}
