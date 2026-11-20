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
    emit_module_preamble(&mut out, module, target)?;

    let mut active_section: Option<SectionKind> = None;
    let mut active_seg_base = String::from("wincmd_seg_base");
    let mut win64_frames: Vec<(String, u32, Vec<String>)> = Vec::new();

    for item in &module.items {
        match item {
            TopItem::Include { path, .. } => emit_include(&mut out, path, target),
            TopItem::Section { kind, span } => {
                close_open_section(&mut out, module.format, active_section, target);
                active_seg_base = open_section(&mut out, module.format, *kind, target, span)?;
                active_section = Some(*kind);
            }
            TopItem::Global { symbol, .. } => emit_global(&mut out, symbol, module, target),
        TopItem::Extern { symbol, kind, .. } => {
                emit_extern(&mut out, module.format, symbol, kind, target);
            }
            TopItem::Const {
                doc, name, expr, ..
            } => {
                emit_doc_block(&mut out, doc, target);
                emit_const(&mut out, name, expr, target);
            }
            TopItem::Struc {
                doc,
                name,
                expected_size,
                fields,
                ..
            } => {
                emit_doc_block(&mut out, doc, target);
                emit_struc(&mut out, name, *expected_size, fields, target);
            }
            TopItem::Proc {
                doc,
                name,
                kind,
                body,
                span,
            } => {
                emit_doc_block(&mut out, doc, target);
                if let ProcKind::Win64Frame {
                    stack_alloc,
                    saved_regs,
                } = kind
                {
                    win64_frames.push((name.clone(), *stack_alloc, saved_regs.clone()));
                }
                emit_proc(
                    &mut out,
                    &ProcEmitReq {
                        module,
                        name,
                        kind,
                        body,
                        active_seg_base: &active_seg_base,
                        target,
                        span,
                    },
                )?;
            }
            TopItem::Stmt(stmt) => {
                emit_stmt(&mut out, module, None, None, stmt, &active_seg_base, target)?;
            }
        }
    }

    close_open_section(&mut out, module.format, active_section, target);
    if module.format == BinaryFormat::Coff64 && !win64_frames.is_empty() {
        emit_win64_unwind_tables(&mut out, &win64_frames, target);
    }
    if module.format == BinaryFormat::Coff64 && target == TargetAssembler::Fasm {
        emit_fasm_pe64_idata(&mut out, module);
    }
    emit_module_trailer(&mut out, module, target);

    Ok(out)
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
) -> Result<(), ToolError> {
    if module.format == BinaryFormat::Include {
        return Ok(());
    }
    if target.supports_isa(CpuIsa::X86_8086) {
        emit_x86_preamble(out, module, target)
    } else {
        emit_arm_preamble(out, module, target)
    }
}

fn emit_x86_preamble(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
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
        (BinaryFormat::Coff32, TargetAssembler::Nasm) => {
            let _ = writeln!(out, "CPU 386{LF}BITS 32");
        }
        (BinaryFormat::Coff32, TargetAssembler::Fasm) => {
            let _ = writeln!(out, "format MS COFF");
        }
        (BinaryFormat::Coff32, TargetAssembler::Jwasm) => {
            let _ = writeln!(out, ".386{LF}.MODEL FLAT, SYSCALL{LF}OPTION CASEMAP:NONE");
        }
        (BinaryFormat::Coff64, TargetAssembler::Nasm) => {
            let _ = writeln!(out, "CPU X64{LF}BITS 64{LF}DEFAULT REL");
        }
        (BinaryFormat::Coff64, TargetAssembler::Fasm) => {
            let _ = writeln!(out, "format PE64 console 5.02 at 140000000h");
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
            let _ = writeln!(out, ".x64{LF}OPTION CASEMAP:NONE");
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

fn emit_arm_preamble(
    out: &mut String,
    module: &CanonicalModule,
    target: TargetAssembler,
) -> Result<(), ToolError> {
    let span = SourceSpan::new(&module.file_path, 1, 1);
    match (module.format, target) {
        (BinaryFormat::ArmV4Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(out, "format PE GUI 4.0");
            if let Some(ent) = &module.entry {
                let _ = writeln!(out, "entry {ent}");
            }
            let _ = writeln!(
                out,
                "code32{LF}processor CPU32_V1 + CPU32_V2 + CPU32_V3 + CPU32_M + CPU32_V4 + CPU32_V4T"
            );
        }
        (BinaryFormat::ArmV4Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(out, ".syntax unified{LF}.arm");
        }
        (BinaryFormat::ArmV4Pe, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "    ARM");
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(out, "format PE console 6.02 at 00400000h");
            if let Some(ent) = &module.entry {
                let _ = writeln!(out, "entry {ent}");
            }
            let _ = writeln!(
                out,
                "code16{LF}processor CPU32_V1 + CPU32_V2 + CPU32_V3 + CPU32_M + CPU32_V4 + CPU32_V4T + CPU32_V5 + CPU32_V6 + CPU32_6M + CPU32_7M + CPU32_T2 + CPU32_V7 + CPU32_SYNC + CPU32_DIV"
            );
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::LlvmMc) => {
            let _ = writeln!(out, ".syntax unified{LF}.thumb");
        }
        (BinaryFormat::ArmV7Pe, TargetAssembler::ArmAsm) => {
            let _ = writeln!(out, "    THUMB");
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::FasmArm) => {
            let _ = writeln!(out, "format PE64 console 10.0 at 140000000h");
            if let Some(ent) = &module.entry {
                let _ = writeln!(out, "entry {ent}");
            }
            let _ = writeln!(out, "processor CPU64_V8");
        }
        (BinaryFormat::Arm64Pe, TargetAssembler::LlvmMc | TargetAssembler::ArmAsm) => {}
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
            out.push_str("ASSUME CS:RESIDENT_SEG, DS:RESIDENT_SEG, ES:RESIDENT_SEG, SS:RESIDENT_SEG");
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
    if matches!(fmt, BinaryFormat::Com | BinaryFormat::Bin16) {
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
                BinaryFormat::ArmV4Pe | BinaryFormat::ArmV7Pe | BinaryFormat::Arm64Pe
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
            let _ = writeln!(
                out,
                "DB ((16 - (($ - {base}) AND 15)) AND 15) DUP(0)"
            );
            let _ = writeln!(out, "{prefix}_BYTE_LEN EQU ($ - {base})");
            let _ = writeln!(out, "{prefix}_PARAS EQU ({prefix}_BYTE_LEN / 16)");
            let _ = writeln!(out, "{seg_name} ENDS");
        }
        _ => {}
    }
}

fn emit_module_trailer(out: &mut String, module: &CanonicalModule, target: TargetAssembler) {
    match (module.format, target) {
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
) {
    if matches!(
        module.format,
        BinaryFormat::Com | BinaryFormat::Com3Part | BinaryFormat::Bin16
    ) {
        return;
    }
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "GLOBAL {symbol}");
        }
        TargetAssembler::Fasm => {
            if module.format != BinaryFormat::Coff64 {
                let _ = writeln!(out, "public {symbol}");
            }
        }
        TargetAssembler::FasmArm => {}
        TargetAssembler::Jwasm => {
            let _ = writeln!(out, "PUBLIC {symbol}");
        }
        TargetAssembler::LlvmMc => {
            let _ = writeln!(out, ".global {symbol}");
            if module.cpu == Some(CpuIsa::ArmV7) {
                let _ = writeln!(out, ".thumb_func");
            }
        }
        TargetAssembler::ArmAsm => {
            let _ = writeln!(out, "    EXPORT {symbol}");
        }
    }
}

fn emit_extern(
    out: &mut String,
    fmt: BinaryFormat,
    symbol: &str,
    kind: &str,
    target: TargetAssembler,
) {
    if fmt == BinaryFormat::Coff64 && target == TargetAssembler::Fasm {
        return;
    }
    match target {
        TargetAssembler::Nasm => {
            let _ = writeln!(out, "EXTERN {symbol}");
        }
        TargetAssembler::Fasm | TargetAssembler::FasmArm => {
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
        TargetAssembler::LlvmMc => {}
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
                let _ = writeln!(out, "    {name}.{} {r_kw} {}", f.name, f.count);
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
                emit_const(out, &format!("{name}_{}", f.name), &offset.to_string(), target);
                offset += f.width.byte_size() * f.count;
            }
            emit_const(out, &format!("{name}_SIZE"), &expected_size.to_string(), target);
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
        (ProcKind::Win64Frame { .. }, TargetAssembler::Nasm | TargetAssembler::Fasm) => {
            let _ = writeln!(out, "{}__end:", req.name);
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
            TargetAssembler::Nasm | TargetAssembler::Fasm,
        ) => {
            let _ = writeln!(out, "{name}:");
            for (idx, reg) in saved_regs.iter().enumerate() {
                let _ = writeln!(out, "    push {reg}");
                let _ = writeln!(out, "{name}__push_{idx}:");
            }
            if *stack_alloc > 0 {
                let _ = writeln!(out, "    sub rsp, {stack_alloc}");
            }
            let _ = writeln!(out, "{name}__prolog_end:");
        }
        (
            ProcKind::Win64Frame {
                stack_alloc,
                saved_regs,
            },
            TargetAssembler::Jwasm,
        ) => {
            let _ = writeln!(out, "{name} PROC FRAME");
            for reg in saved_regs {
                let _ = writeln!(out, "    push {reg}");
                let _ = writeln!(out, "    .PUSHREG {reg}");
            }
            if *stack_alloc > 0 {
                let _ = writeln!(out, "    sub rsp, {stack_alloc}");
                let _ = writeln!(out, "    .ALLOCSTACK {stack_alloc}");
            }
            out.push_str("    .ENDPROLOG");
            out.push(LF);
        }
        (ProcKind::Win64Frame { .. }, _) => {
            return Err(ToolError::Lowering {
                span: req.span.clone(),
                assembler: target,
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
                TargetAssembler::Fasm
                | TargetAssembler::Jwasm
                | TargetAssembler::FasmArm => {
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
                let _ = writeln!(out, "    add rsp, {stack_alloc}");
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
        BinaryFormat::Com | BinaryFormat::Com3Part | BinaryFormat::Bin16
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
            if in_flat_bin || matches!(fmt, BinaryFormat::ArmV7Pe | BinaryFormat::Arm64Pe) {
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
        TargetAssembler::Fasm | TargetAssembler::FasmArm => {
            let _ = writeln!(
                out,
                "repeat (({align_bytes} - (($ - {active_seg_base}) and {mask})) and {mask})"
            );
            out.push_str("    db 0");
            out.push(LF);
            out.push_str("end repeat");
            out.push(LF);
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

    if operands.is_empty() {
        let _ = writeln!(out, "    {mnem}");
        return;
    }

    let lowered_ops: Vec<String> = operands
        .iter()
        .map(|op| lower_operand(op, module.cpu, proc_name, target))
        .collect();
    let _ = writeln!(out, "    {mnem} {}", lowered_ops.join(", "));
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
pub fn rewrite_expr_tokens(
    expr: &str,
    proc_name: Option<&str>,
    target: TargetAssembler,
) -> String {
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
            let prev_is_ident = i > 0 && (chars[i - 1].is_ascii_alphanumeric() || chars[i - 1] == '_');
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
        if ch == '.' && i + 1 < chars.len() && (chars[i + 1].is_ascii_alphabetic() || chars[i + 1] == '_') {
            let prev_is_ident =
                i > 0 && (chars[i - 1].is_ascii_alphanumeric() || chars[i - 1] == '_' || chars[i - 1] == '$');
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
                    TargetAssembler::Fasm
                    | TargetAssembler::Jwasm
                    | TargetAssembler::FasmArm => {
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
        TargetAssembler::Fasm => {
            out.push_str("section '.xdata' data readable");
            out.push(LF);
            for (name, stack_alloc, saved_regs) in frames {
                emit_unwind_info_bytes(out, name, *stack_alloc, saved_regs, false);
            }
            out.push_str("section '.pdata' data readable");
            out.push(LF);
            out.push_str("data 3");
            out.push(LF);
            for (name, _, _) in frames {
                let _ = writeln!(out, "    dd rva {name}");
                let _ = writeln!(out, "    dd rva {name}__end");
                let _ = writeln!(out, "    dd rva {name}__unwind");
            }
            out.push_str("end data");
            out.push(LF);
        }
        _ => {}
    }
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
            codes.push((format!("{name}__prolog_end - {name}"), (info << 4) | 2, None));
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
