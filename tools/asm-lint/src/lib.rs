//! Multi-ISA static analyzer, 100% doc-comment coverage gate, control-flow graph (`CFG`)
//! stack-balance & `@clobbers`/ABI verifier, ISA opcode purity checker, 3-assembler
//! equivalence comparator, and binary footprint budget gate.

mod x64_equiv;

use asm_conv::ast::{BinaryFormat, CanonicalModule, DocBlock, ProcKind, Stmt, TopItem};
use asm_conv::parser::parse_canonical;
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use wincmd_diag::{CpuIsa, SourceSpan, ToolError};

/// Verifies 100% structured doc-comment coverage and full static CFG/ABI/ISA safety
/// on a single [`CanonicalModule`], given an optional map of known procedure clobber sets.
///
/// # Errors
///
/// Returns [`ToolError::DocLint`], [`ToolError::CfgLint`], [`ToolError::AbiLint`],
/// or [`ToolError::OpcodePurity`] on any violation.
pub fn lint_module(
    module: &CanonicalModule,
    known_clobbers: &BTreeMap<String, BTreeSet<String>>,
) -> Result<(), ToolError> {
    verify_docs(module)?;
    verify_semantics(module, known_clobbers)
}

/// Collects declared `@clobbers` register sets for every `@proc` across a slice of modules.
#[must_use]
pub fn collect_module_clobbers(
    modules: &[CanonicalModule],
) -> BTreeMap<String, BTreeSet<String>> {
    let mut map = BTreeMap::new();
    for m in modules {
        for item in &m.items {
            if let TopItem::Proc { doc, name, .. } = item {
                let set = parse_reg_list(doc.get("clobbers").unwrap_or(""));
                map.insert(name.clone(), set);
            }
        }
    }
    map
}

/// Enforces 100.0% structured `;; @tag` documentation across file, `@const`, `@struc`, and `@proc` items.
///
/// # Errors
///
/// Returns [`ToolError::DocLint`] if any required tag is missing or mismatched.
pub fn verify_docs(module: &CanonicalModule) -> Result<(), ToolError> {
    let file_span = SourceSpan::new(&module.file_path, 1, 1);
    for req in ["file", "isa", "desc"] {
        require_tag(&module.file_doc, req, &file_span, "file header")?;
    }

    for item in &module.items {
        match item {
            TopItem::Const {
                doc, name, span, ..
            } => {
                require_tag(doc, "const", span, &format!("@const `{name}`"))?;
                require_tag(doc, "desc", span, &format!("@const `{name}`"))?;
            }
            TopItem::Struc {
                doc, name, span, ..
            } => {
                require_tag(doc, "struc", span, &format!("@struc `{name}`"))?;
                require_tag(doc, "desc", span, &format!("@struc `{name}`"))?;
            }
            TopItem::Proc {
                doc, name, span, ..
            } => {
                for req in ["proc", "abi", "in", "out", "err", "clobbers", "desc"] {
                    require_tag(doc, req, span, &format!("@proc `{name}`"))?;
                }
                let doc_proc = doc.get("proc").unwrap_or("");
                if doc_proc != name {
                    return Err(ToolError::DocLint {
                        span: span.clone(),
                        message: format!(
                            "`;; @proc {doc_proc}` does not match procedure name `{name}`"
                        ),
                    });
                }
            }
            TopItem::Include { .. }
            | TopItem::Section { .. }
            | TopItem::Global { .. }
            | TopItem::Extern { .. }
            | TopItem::Stmt(_) => {}
        }
    }
    Ok(())
}

fn require_tag(
    doc: &DocBlock,
    tag: &str,
    span: &SourceSpan,
    context: &str,
) -> Result<(), ToolError> {
    match doc.get(tag) {
        Some(v) if !v.trim().is_empty() => Ok(()),
        _ => Err(ToolError::DocLint {
            span: span.clone(),
            message: format!("missing `;; @{tag}` on {context}"),
        }),
    }
}

/// Performs ISA opcode purity, CFG stack-balance, callee-save ABI, transitive `@clobbers`,
/// Win16 selector-math, and ARM64 `X18` verification across `module`.
///
/// # Errors
///
/// Returns [`ToolError`] if any static safety rule fails.
pub fn verify_semantics(
    module: &CanonicalModule,
    known_clobbers: &BTreeMap<String, BTreeSet<String>>,
) -> Result<(), ToolError> {
    let Some(isa) = module.cpu else {
        return Ok(());
    };

    for item in &module.items {
        match item {
            TopItem::Stmt(stmt) => {
                check_stmt_isa_purity(isa, None, stmt)?;
            }
            TopItem::Proc {
                doc,
                name,
                kind,
                body,
                span,
            } => {
                let is_win16 = doc
                    .get("abi")
                    .is_some_and(|a| a.to_ascii_lowercase().contains("win16"));
                let ctx = ProcVerifyCtx {
                    isa,
                    is_win16,
                    proc_name: name,
                    doc,
                    kind,
                    body,
                    proc_span: span,
                    known_clobbers,
                };
                verify_proc_cfg_and_abi(&ctx)?;
            }
            _ => {}
        }
    }
    Ok(())
}

struct ProcVerifyCtx<'a> {
    isa: CpuIsa,
    is_win16: bool,
    proc_name: &'a str,
    doc: &'a DocBlock,
    kind: &'a ProcKind,
    body: &'a [Stmt],
    proc_span: &'a SourceSpan,
    known_clobbers: &'a BTreeMap<String, BTreeSet<String>>,
}

fn verify_proc_cfg_and_abi(ctx: &ProcVerifyCtx<'_>) -> Result<(), ToolError> {
    let mut label_to_pc: BTreeMap<String, usize> = BTreeMap::new();
    for (pc, stmt) in ctx.body.iter().enumerate() {
        if let Stmt::LocalLabel { name, .. } = stmt {
            label_to_pc.insert(format!(".{name}"), pc);
        }
    }

    if ctx.isa == CpuIsa::X86_64 {
        verify_win64_proc_abi(ctx)?;
    }
    verify_proc_sequential_hazards(ctx)?;
    verify_proc_cfg_stack(ctx, &label_to_pc)?;
    verify_proc_clobbers_and_abi(ctx)
}

fn verify_win64_proc_abi(ctx: &ProcVerifyCtx<'_>) -> Result<(), ToolError> {
    let has_call = ctx.body.iter().any(|stmt| {
        matches!(
            stmt,
            Stmt::Instruction { mnemonic, .. } if mnemonic == "call"
        )
    });
    match ctx.kind {
        ProcKind::Win64Frame {
            stack_alloc,
            saved_regs,
        } => {
            let total_frame = 8_usize + saved_regs.len() * 8 + (*stack_alloc as usize);
            if !total_frame.is_multiple_of(16) {
                return Err(ToolError::AbiLint {
                    span: ctx.proc_span.clone(),
                    proc_name: ctx.proc_name.to_owned(),
                    message: format!(
                        "Microsoft x64 ABI violation: `(8 + {}*8 + {stack_alloc}) = {total_frame}` is not 16-byte RSP-aligned",
                        saved_regs.len()
                    ),
                });
            }
            if has_call && *stack_alloc < 32 {
                return Err(ToolError::AbiLint {
                    span: ctx.proc_span.clone(),
                    proc_name: ctx.proc_name.to_owned(),
                    message: format!(
                        "Microsoft x64 ABI violation: non-leaf `@proc_frame` allocates {stack_alloc}B (< mandatory 32B shadow space)"
                    ),
                });
            }
        }
        ProcKind::Near | ProcKind::Far => {
            if has_call {
                return Err(ToolError::AbiLint {
                    span: ctx.proc_span.clone(),
                    proc_name: ctx.proc_name.to_owned(),
                    message: "Microsoft x64 ABI violation: non-leaf procedure containing `call` must use `@proc_frame` for `.pdata`/`.xdata` SEH unwind".into(),
                });
            }
        }
    }
    Ok(())
}

fn verify_proc_sequential_hazards(ctx: &ProcVerifyCtx<'_>) -> Result<(), ToolError> {
    let mut prev_int25_26: Option<SourceSpan> = None;
    let mut tainted_arith_regs: BTreeSet<String> = BTreeSet::new();

    for stmt in ctx.body {
        check_stmt_isa_purity(ctx.isa, Some(ctx.proc_name), stmt)?;
        if let Stmt::Instruction {
            mnemonic,
            operands,
            span,
        } = stmt
        {
            if let Some(int_span) = prev_int25_26.take()
                && !(mnemonic.starts_with("pop")
                    || mnemonic == "add"
                        && operands
                            .first()
                            .is_some_and(|o| o.eq_ignore_ascii_case("sp")))
            {
                return Err(ToolError::AbiLint {
                    span: int_span,
                    proc_name: ctx.proc_name.to_owned(),
                    message: "DOS `int 0x25`/`0x26` leaves flags on stack; must be followed immediately by `pop` or `add sp, 2`".into(),
                });
            }
            if mnemonic == "int"
                && operands
                    .first()
                    .is_some_and(|o| matches!(o.trim(), "0x25" | "0x26" | "25h" | "26h"))
            {
                prev_int25_26 = Some(span.clone());
            }

            if ctx.is_win16 {
                check_win16_selector_safety(
                    ctx.proc_name,
                    mnemonic,
                    operands,
                    span,
                    &mut tainted_arith_regs,
                )?;
            }
        }
    }
    if let Some(int_span) = prev_int25_26 {
        return Err(ToolError::AbiLint {
            span: int_span,
            proc_name: ctx.proc_name.to_owned(),
            message: "unbalanced `int 0x25`/`0x26` at end of procedure".into(),
        });
    }
    Ok(())
}

fn verify_proc_cfg_stack(
    ctx: &ProcVerifyCtx<'_>,
    label_to_pc: &BTreeMap<String, usize>,
) -> Result<(), ToolError> {
    if ctx.body.is_empty() {
        return Ok(());
    }

    let mut enter_alloc: i32 = 0;
    for stmt in ctx.body {
        if let Stmt::Instruction {
            mnemonic, operands, ..
        } = stmt
            && mnemonic == "enter"
        {
            enter_alloc = operands
                .first()
                .and_then(|v| parse_signed_imm(v))
                .unwrap_or(0);
            break;
        }
    }

    let mut stack_at: Vec<Option<i32>> = vec![None; ctx.body.len()];
    let mut worklist: Vec<(usize, i32)> = vec![(0, 0)];

    while let Some((pc, depth)) = worklist.pop() {
        if pc >= ctx.body.len() {
            return Err(ToolError::CfgLint {
                span: ctx.proc_span.clone(),
                proc_name: ctx.proc_name.to_owned(),
                message:
                    "control flow falls off the end of `@proc` without `ret` or unconditional jump"
                        .into(),
            });
        }
        if let Some(existing) = stack_at[pc] {
            if existing != depth {
                return Err(ToolError::CfgLint {
                    span: stmt_span(&ctx.body[pc]).clone(),
                    proc_name: ctx.proc_name.to_owned(),
                    message: format!(
                        "conflicting stack pointer delta at merge point ({existing} vs {depth})"
                    ),
                });
            }
            continue;
        }
        stack_at[pc] = Some(depth);

        match &ctx.body[pc] {
            Stmt::Comment { .. }
            | Stmt::LocalLabel { .. }
            | Stmt::GlobalLabel { .. }
            | Stmt::AlignZero { .. } => {
                worklist.push((pc + 1, depth));
            }
            Stmt::Data { .. } | Stmt::Reserve { .. } => {}
            Stmt::Win64EpilogRet { span } => {
                if depth != 0 {
                    return Err(ToolError::CfgLint {
                        span: span.clone(),
                        proc_name: ctx.proc_name.to_owned(),
                        message: format!("non-zero stack delta ({depth}) before `@epilog_ret`"),
                    });
                }
            }
            Stmt::Instruction { .. } => {
                step_cfg_instruction(ctx, pc, depth, enter_alloc, label_to_pc, &mut worklist)?;
            }
        }
    }
    Ok(())
}

fn step_cfg_instruction(
    ctx: &ProcVerifyCtx<'_>,
    pc: usize,
    depth: i32,
    enter_alloc: i32,
    label_to_pc: &BTreeMap<String, usize>,
    worklist: &mut Vec<(usize, i32)>,
) -> Result<(), ToolError> {
    let Stmt::Instruction {
        mnemonic,
        operands,
        span,
    } = &ctx.body[pc]
    else {
        return Ok(());
    };
    let delta = if mnemonic == "leave" {
        let slot: i32 = match ctx.isa {
            CpuIsa::X86_8086 | CpuIsa::X86_286 => 2,
            CpuIsa::Ia32 | CpuIsa::ArmV4 | CpuIsa::ArmV7 => 4,
            CpuIsa::X86_64 | CpuIsa::Arm64 => 8,
        };
        slot + enter_alloc
    } else {
        instruction_stack_delta(ctx.isa, mnemonic, operands)
    };
    let next_depth = depth + delta;

    if matches!(ctx.isa, CpuIsa::ArmV4 | CpuIsa::ArmV7)
        && matches!(mnemonic.as_str(), "bl" | "call_iat")
        && depth % 8 != 0
    {
        return Err(ToolError::AbiLint {
            span: span.clone(),
            proc_name: ctx.proc_name.to_owned(),
            message: format!(
                "ARM32 ABI violation: SP offset ({depth}) is not 8-byte aligned before `{mnemonic}`"
            ),
        });
    }
    if ctx.isa == CpuIsa::Arm64 {
        let uses_sp_mem = operands
            .iter()
            .any(|op| op.to_ascii_lowercase().contains("[sp"));
        if (matches!(mnemonic.as_str(), "bl" | "blr" | "call_iat") && depth % 16 != 0)
            || (uses_sp_mem && depth % 16 != 0 && next_depth % 16 != 0)
        {
            return Err(ToolError::AbiLint {
                span: span.clone(),
                proc_name: ctx.proc_name.to_owned(),
                message: format!(
                    "Windows ARM64 ABI violation: SP offset ({depth}) is not 16-byte aligned at `{mnemonic}`"
                ),
            });
        }
    }

    if is_return_mnemonic(mnemonic, operands) {
        if next_depth != 0 {
            return Err(ToolError::CfgLint {
                span: span.clone(),
                proc_name: ctx.proc_name.to_owned(),
                message: format!("unbalanced stack pointer ({next_depth} bytes) at return"),
            });
        }
    } else if is_unconditional_jump(mnemonic) {
        if let Some(target_lbl) = operands.last()
            && let Some(&target_pc) = label_to_pc.get(target_lbl.trim())
        {
            worklist.push((target_pc, next_depth));
        }
    } else if let Some(target_lbl) = conditional_branch_target(mnemonic, operands) {
        if let Some(&target_pc) = label_to_pc.get(target_lbl) {
            worklist.push((target_pc, next_depth));
        }
        worklist.push((pc + 1, next_depth));
    } else {
        worklist.push((pc + 1, next_depth));
    }
    Ok(())
}

fn verify_proc_clobbers_and_abi(ctx: &ProcVerifyCtx<'_>) -> Result<(), ToolError> {
    let declared_clobbers = parse_reg_list(ctx.doc.get("clobbers").unwrap_or(""));
    let mut pushed_regs: BTreeSet<String> = BTreeSet::new();
    if let ProcKind::Win64Frame { saved_regs, .. } = ctx.kind {
        for r in saved_regs {
            pushed_regs.insert(canonical_reg_family(ctx.isa, r));
        }
    }
    let mut modified_regs: BTreeSet<String> = BTreeSet::new();

    for stmt in ctx.body {
        if let Stmt::Instruction {
            mnemonic, operands, ..
        } = stmt
        {
            if mnemonic == "push" {
                if let Some(op0) = operands.first() {
                    for part in op0.trim().trim_matches(&['{', '}'][..]).split(',') {
                        let fam = canonical_reg_family(ctx.isa, part);
                        if !fam.is_empty() {
                            pushed_regs.insert(fam);
                        }
                    }
                }
            } else if mnemonic == "enter" {
                pushed_regs.insert(canonical_reg_family(ctx.isa, "bp"));
            } else if mnemonic == "pusha" {
                for r in ["ax", "bx", "cx", "dx", "bp", "si", "di"] {
                    pushed_regs.insert(canonical_reg_family(ctx.isa, r));
                }
            } else if mnemonic == "stp" && operands.get(2).is_some_and(|m| m.contains("sp")) {
                if let Some(r0) = operands.first() {
                    pushed_regs.insert(canonical_reg_family(ctx.isa, r0));
                }
                if let Some(r1) = operands.get(1) {
                    pushed_regs.insert(canonical_reg_family(ctx.isa, r1));
                }
            } else {
                collect_modified_regs(
                    ctx.isa,
                    mnemonic,
                    operands,
                    ctx.known_clobbers,
                    &mut modified_regs,
                );
            }
        }
    }

    let callee_saved = callee_saved_regs(ctx.isa);
    let stack_ptr = stack_pointer_family(ctx.isa);

    for reg in &modified_regs {
        if reg == stack_ptr || pushed_regs.contains(reg) {
            continue;
        }
        if callee_saved.contains(&reg.as_str()) {
            return Err(ToolError::AbiLint {
                span: ctx.proc_span.clone(),
                proc_name: ctx.proc_name.to_owned(),
                message: format!(
                    "callee-saved register `{reg}` modified without matching push/pop save"
                ),
            });
        }
        if !declared_clobbers.contains("all") && !declared_clobbers.contains(reg) {
            return Err(ToolError::AbiLint {
                span: ctx.proc_span.clone(),
                proc_name: ctx.proc_name.to_owned(),
                message: format!(
                    "modified scratch register `{reg}` is missing from `;; @clobbers`"
                ),
            });
        }
    }

    Ok(())
}

fn check_8086_purity(m: &str, operands: &[String], span: &SourceSpan) -> Result<(), ToolError> {
    const BANNED_8086: &[&str] = &[
        "pusha", "popa", "pushad", "popad", "enter", "leave", "movzx", "movsx", "bswap", "shld",
        "shrd", "bt", "bts", "btr", "btc", "bsf", "bsr",
    ];
    if BANNED_8086.contains(&m) {
        return Err(ToolError::OpcodePurity {
            span: span.clone(),
            isa: CpuIsa::X86_8086,
            message: format!("instruction `{m}` requires 80186+/80386+ (forbidden in 8086)"),
        });
    }
    if m == "pop" && operands.first().is_some_and(|o| o.eq_ignore_ascii_case("ss")) {
        return Err(ToolError::OpcodePurity {
            span: span.clone(),
            isa: CpuIsa::X86_8086,
            message: "`pop ss` triggers early-8088 interrupt shadow bug; use `cli; mov ss, ...; sti`"
                .into(),
        });
    }
    if m == "push"
        && operands
            .first()
            .is_some_and(|o| is_immediate_token(o.trim()))
    {
        return Err(ToolError::OpcodePurity {
            span: span.clone(),
            isa: CpuIsa::X86_8086,
            message: "`push imm` requires 80186+; forbidden in strict 8086".into(),
        });
    }
    if matches!(
        m,
        "shl" | "shr" | "sar" | "sal" | "rol" | "ror" | "rcl" | "rcr"
    ) && operands.get(1).is_some_and(|op1| {
        let t = op1.trim();
        !t.eq_ignore_ascii_case("1") && !t.eq_ignore_ascii_case("cl")
    }) {
        return Err(ToolError::OpcodePurity {
            span: span.clone(),
            isa: CpuIsa::X86_8086,
            message: format!("`{m}` count must be `1` or `cl` on 8086"),
        });
    }
    for op in operands {
        let lo = op.to_ascii_lowercase();
        if lo.contains("fs:") || lo.contains("gs:") {
            return Err(ToolError::OpcodePurity {
                span: span.clone(),
                isa: CpuIsa::X86_8086,
                message: "386 segment overrides `fs:`/`gs:` forbidden on 8086".into(),
            });
        }
    }
    Ok(())
}

fn check_stmt_isa_purity(
    isa: CpuIsa,
    proc_name: Option<&str>,
    stmt: &Stmt,
) -> Result<(), ToolError> {
    let Stmt::Instruction {
        mnemonic,
        operands,
        span,
    } = stmt
    else {
        return Ok(());
    };
    let m = mnemonic.as_str();

    match isa {
        CpuIsa::X86_8086 => check_8086_purity(m, operands, span)?,
        CpuIsa::Ia32 => {
            if m.starts_with("cmov")
                || matches!(m, "bswap" | "xadd" | "cmpxchg" | "cmpxchg8b" | "cpuid")
            {
                return Err(ToolError::OpcodePurity {
                    span: span.clone(),
                    isa,
                    message: format!("486+/Pentium instruction `{m}` forbidden in baseline i386"),
                });
            }
        }
        CpuIsa::ArmV4 => {
            if matches!(
                m,
                "blx" | "clz" | "movw" | "movt" | "udiv" | "sdiv" | "ldrd" | "strd" | "rbit" | "rev"
            ) {
                return Err(ToolError::OpcodePurity {
                    span: span.clone(),
                    isa,
                    message: format!("ARMv5+/ARMv7 instruction `{m}` forbidden in strict ARMv4"),
                });
            }
        }
        CpuIsa::Arm64 => {
            if let Some(dst) = operands.first() {
                let d = dst.trim().to_ascii_lowercase();
                if (d == "x18" || d == "w18")
                    && !matches!(m, "cmp" | "tst" | "cbz" | "cbnz" | "tbz" | "tbnz" | "str")
                {
                    return Err(ToolError::AbiLint {
                        span: span.clone(),
                        proc_name: proc_name.unwrap_or("<top>").to_owned(),
                        message: "writing to Windows ARM64 platform register `x18`/`w18` (`TEB`) is strictly forbidden".into(),
                    });
                }
            }
        }
        CpuIsa::ArmV7 => {
            if m.starts_with("it")
                && m.bytes().skip(2).all(|b| b == b't' || b == b'e')
                && m.len() > 6
            {
                return Err(ToolError::OpcodePurity {
                    span: span.clone(),
                    isa,
                    message: format!("Thumb-2 IT block `{m}` exceeds 4-instruction Windows ARMNT limit"),
                });
            }
        }
        CpuIsa::X86_286 | CpuIsa::X86_64 => {}
    }

    Ok(())
}

fn check_win16_selector_safety(
    proc_name: &str,
    mnemonic: &str,
    operands: &[String],
    span: &SourceSpan,
    tainted: &mut BTreeSet<String>,
) -> Result<(), ToolError> {
    if !is_readonly_or_branch_mnemonic(mnemonic, operands)
        && mnemonic != "call"
        && let Some(dst) = operands.first()
        && dst.to_ascii_lowercase().contains("cs:")
    {
        return Err(ToolError::AbiLint {
            span: span.clone(),
            proc_name: proc_name.to_owned(),
            message: "Win16 Protected-Mode violation: writing to `CS:` code selector causes #GP"
                .into(),
        });
    }
    if matches!(mnemonic, "add" | "sub" | "inc" | "dec" | "shl" | "shr") {
        let is_ahincr_step = mnemonic == "add"
            && operands
                .get(1)
                .is_some_and(|rhs| rhs.to_ascii_lowercase().contains("ahincr"));
        if !is_ahincr_step && let Some(dst) = operands.first() {
            let r = canonical_reg_family(CpuIsa::X86_286, dst);
            if !r.is_empty() {
                tainted.insert(r);
            }
        }
    } else if mnemonic == "mov" && operands.len() == 2 {
        let dst = operands[0].trim().to_ascii_lowercase();
        let src_fam = canonical_reg_family(CpuIsa::X86_286, &operands[1]);
        if matches!(dst.as_str(), "ds" | "es" | "ss") && tainted.contains(&src_fam) {
            return Err(ToolError::AbiLint {
                span: span.clone(),
                proc_name: proc_name.to_owned(),
                message: format!(
                    "Win16 Protected-Mode selector violation: loading arithmetic-modified `{src_fam}` into `{dst}` causes #GP"
                ),
            });
        }
        if !dst.is_empty() && !tainted.contains(&src_fam) {
            tainted.remove(&canonical_reg_family(CpuIsa::X86_286, &dst));
        }
    }
    Ok(())
}

const fn stmt_span(stmt: &Stmt) -> &SourceSpan {
    match stmt {
        Stmt::Comment { span, .. }
        | Stmt::LocalLabel { span, .. }
        | Stmt::GlobalLabel { span, .. }
        | Stmt::Data { span, .. }
        | Stmt::Reserve { span, .. }
        | Stmt::AlignZero { span, .. }
        | Stmt::Win64EpilogRet { span }
        | Stmt::Instruction { span, .. } => span,
    }
}

fn instruction_stack_delta(isa: CpuIsa, mnemonic: &str, operands: &[String]) -> i32 {
    let slot: i32 = match isa {
        CpuIsa::X86_8086 | CpuIsa::X86_286 => 2,
        CpuIsa::Ia32 | CpuIsa::ArmV4 | CpuIsa::ArmV7 => 4,
        CpuIsa::X86_64 | CpuIsa::Arm64 => 8,
    };
    match mnemonic {
        "push" | "pushf" | "pushfd" | "pushfq" => {
            if matches!(isa, CpuIsa::ArmV4 | CpuIsa::ArmV7)
                && let Some(list) = operands.first()
            {
                let count = i32::try_from(list.split(',').count()).unwrap_or(1);
                return -4 * count;
            }
            -slot
        }
        "pop" | "popf" | "popfd" | "popfq" => {
            if matches!(isa, CpuIsa::ArmV4 | CpuIsa::ArmV7)
                && let Some(list) = operands.first()
            {
                let count = i32::try_from(list.split(',').count()).unwrap_or(1);
                return 4 * count;
            }
            slot
        }
        "pusha" => -16,
        "popa" => 16,
        "enter" => {
            let locals = operands
                .first()
                .and_then(|v| parse_signed_imm(v))
                .unwrap_or(0);
            -slot - locals
        }
        "int"
            if operands
                .first()
                .is_some_and(|o| matches!(o.trim(), "0x25" | "0x26" | "25h" | "26h")) =>
        {
            -2
        }
        "sub" | "subs"
            if operands
                .first()
                .is_some_and(|r| matches!(r.trim().to_ascii_lowercase().as_str(), "sp" | "esp" | "rsp")) =>
        {
            operands
                .last()
                .and_then(|v| parse_signed_imm(v))
                .map_or(0, |n| -n)
        }
        "add" | "adds"
            if operands
                .first()
                .is_some_and(|r| matches!(r.trim().to_ascii_lowercase().as_str(), "sp" | "esp" | "rsp")) =>
        {
            operands.last().and_then(|v| parse_signed_imm(v)).unwrap_or(0)
        }
        "stp" if operands.last().is_some_and(|o| o.ends_with('!')) => {
            extract_arm64_sp_offset(operands.last().map_or("", String::as_str))
        }
        "ldp" if operands.len() == 4 && operands[2].trim() == "[sp]" => {
            parse_signed_imm(&operands[3]).unwrap_or(0)
        }
        "call" if isa == CpuIsa::Ia32 => operands
            .first()
            .and_then(|op| {
                op.rsplit_once('@')
                    .or_else(|| op.rsplit_once("_p"))
                    .and_then(|(_, rest)| {
                        let digits: String =
                            rest.chars().take_while(char::is_ascii_digit).collect();
                        digits.parse::<i32>().ok()
                    })
            })
            .unwrap_or(0),
        "call" if matches!(isa, CpuIsa::X86_8086 | CpuIsa::X86_286) => operands
            .first()
            .and_then(|op| {
                let lo = op.trim().to_ascii_lowercase();
                if !lo.starts_with("far ") {
                    return None;
                }
                lo.rsplit_once("_p").and_then(|(_, rest)| {
                    let digits: String = rest.chars().take_while(char::is_ascii_digit).collect();
                    digits.parse::<i32>().ok()
                })
            })
            .unwrap_or(0),
        _ => 0,
    }
}

fn extract_arm64_sp_offset(mem: &str) -> i32 {
    let trimmed = mem.trim().trim_end_matches('!').trim_matches(&['[', ']'][..]);
    trimmed
        .split(',')
        .nth(1)
        .and_then(parse_signed_imm)
        .unwrap_or(0)
}

fn parse_signed_imm(raw: &str) -> Option<i32> {
    let t = raw.trim().trim_start_matches('#');
    if let Some(neg) = t.strip_prefix('-') {
        return parse_signed_imm(neg).map(|v| -v);
    }
    t.strip_prefix("0x")
        .or_else(|| t.strip_prefix("0X"))
        .map_or_else(
            || t.parse::<i32>().ok(),
            |hex| i32::from_str_radix(hex, 16).ok(),
        )
}

fn is_return_mnemonic(mnemonic: &str, operands: &[String]) -> bool {
    matches!(mnemonic, "ret" | "retn" | "retf" | "iret" | "iretd" | "iretq")
        || (mnemonic == "bx" && operands.first().is_some_and(|o| o.eq_ignore_ascii_case("lr")))
        || (mnemonic == "mov"
            && operands.len() == 2
            && operands[0].eq_ignore_ascii_case("pc")
            && operands[1].eq_ignore_ascii_case("lr"))
}

fn is_unconditional_jump(mnemonic: &str) -> bool {
    matches!(mnemonic, "jmp" | "b")
}

fn conditional_branch_target<'a>(mnemonic: &str, operands: &'a [String]) -> Option<&'a str> {
    let is_cond = matches!(
        mnemonic,
        "jz"
            | "je"
            | "jnz"
            | "jne"
            | "jc"
            | "jb"
            | "jnae"
            | "jnc"
            | "jae"
            | "jnb"
            | "ja"
            | "jnbe"
            | "jbe"
            | "jna"
            | "jg"
            | "jnle"
            | "jge"
            | "jnl"
            | "jl"
            | "jnge"
            | "jle"
            | "jng"
            | "js"
            | "jns"
            | "jo"
            | "jno"
            | "jcxz"
            | "jecxz"
            | "jrcxz"
            | "loop"
            | "loope"
            | "loopne"
            | "cbz"
            | "cbnz"
            | "tbz"
            | "tbnz"
            | "beq"
            | "bne"
            | "bcs"
            | "bcc"
            | "bhi"
            | "bls"
            | "bge"
            | "blt"
            | "bgt"
            | "ble"
    ) || mnemonic.starts_with("b.");
    if is_cond {
        operands.last().map(|s| s.trim())
    } else {
        None
    }
}

fn is_immediate_token(tok: &str) -> bool {
    const SQUOTE: char = 39 as char;
    tok.chars().next().is_some_and(|c| {
        c.is_ascii_digit() || c == '-' || c == '+' || c == SQUOTE || c == '"'
    }) || tok.starts_with("addr(")
}

fn parse_reg_list(raw: &str) -> BTreeSet<String> {
    let mut set = BTreeSet::new();
    for part in raw.split(|c: char| c == ',' || c.is_whitespace()) {
        let p = part.trim().to_ascii_lowercase();
        if !p.is_empty() && p != "none" && p != "flags" && p != "cc" {
            set.insert(p);
        }
    }
    set
}

const fn callee_saved_regs(isa: CpuIsa) -> &'static [&'static str] {
    match isa {
        CpuIsa::X86_8086 | CpuIsa::X86_286 => &["bp", "ds", "es"],
        CpuIsa::Ia32 => &["ebx", "esi", "edi", "ebp"],
        CpuIsa::X86_64 => &["rbx", "rbp", "rdi", "rsi", "r12", "r13", "r14", "r15"],
        CpuIsa::ArmV4 | CpuIsa::ArmV7 => &["r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11"],
        CpuIsa::Arm64 => &[
            "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30",
        ],
    }
}

const fn stack_pointer_family(isa: CpuIsa) -> &'static str {
    match isa {
        CpuIsa::Ia32 => "esp",
        CpuIsa::X86_64 => "rsp",
        CpuIsa::X86_8086 | CpuIsa::X86_286 | CpuIsa::ArmV4 | CpuIsa::ArmV7 | CpuIsa::Arm64 => "sp",
    }
}

fn canonical_reg_family(isa: CpuIsa, tok: &str) -> String {
    let t = tok.trim().to_ascii_lowercase();
    if t.starts_with('[') || is_immediate_token(&t) {
        return String::new();
    }
    match isa {
        CpuIsa::X86_8086 | CpuIsa::X86_286 => match t.as_str() {
            "al" | "ah" | "ax" => "ax".into(),
            "bl" | "bh" | "bx" => "bx".into(),
            "cl" | "ch" | "cx" => "cx".into(),
            "dl" | "dh" | "dx" => "dx".into(),
            "si" | "di" | "bp" | "sp" | "ds" | "es" | "ss" => t,
            _ => String::new(),
        },
        CpuIsa::Ia32 => match t.as_str() {
            "al" | "ah" | "ax" | "eax" => "eax".into(),
            "bl" | "bh" | "bx" | "ebx" => "ebx".into(),
            "cl" | "ch" | "cx" | "ecx" => "ecx".into(),
            "dl" | "dh" | "dx" | "edx" => "edx".into(),
            "si" | "esi" => "esi".into(),
            "di" | "edi" => "edi".into(),
            "bp" | "ebp" => "ebp".into(),
            "sp" | "esp" => "esp".into(),
            _ => String::new(),
        },
        CpuIsa::X86_64 => match t.as_str() {
            "al" | "ah" | "ax" | "eax" | "rax" => "rax".into(),
            "bl" | "bh" | "bx" | "ebx" | "rbx" => "rbx".into(),
            "cl" | "ch" | "cx" | "ecx" | "rcx" => "rcx".into(),
            "dl" | "dh" | "dx" | "edx" | "rdx" => "rdx".into(),
            "sil" | "si" | "esi" | "rsi" => "rsi".into(),
            "dil" | "di" | "edi" | "rdi" => "rdi".into(),
            "bpl" | "bp" | "ebp" | "rbp" => "rbp".into(),
            "spl" | "sp" | "esp" | "rsp" => "rsp".into(),
            _ if t.starts_with('r') => {
                let base = t.trim_end_matches(&['b', 'w', 'd'][..]);
                if matches!(
                    base,
                    "r8" | "r9" | "r10" | "r11" | "r12" | "r13" | "r14" | "r15"
                ) {
                    base.into()
                } else {
                    String::new()
                }
            }
            _ => String::new(),
        },
        CpuIsa::ArmV4 | CpuIsa::ArmV7 => match t.as_str() {
            "fp" => "r11".into(),
            "ip" => "r12".into(),
            "sp" | "r13" => "sp".into(),
            "lr" | "r14" => "lr".into(),
            _ if t.starts_with('r') && t[1..].chars().all(|c| c.is_ascii_digit()) => t,
            _ => String::new(),
        },
        CpuIsa::Arm64 => match t.as_str() {
            "fp" => "x29".into(),
            "lr" => "x30".into(),
            "sp" => "sp".into(),
            _ if (t.starts_with('x') || t.starts_with('w'))
                && t[1..].chars().all(|c| c.is_ascii_digit()) =>
            {
                format!("x{}", &t[1..])
            }
            _ => String::new(),
        },
    }
}

fn collect_call_clobbers(
    isa: CpuIsa,
    mnemonic: &str,
    operands: &[String],
    known_clobbers: &BTreeMap<String, BTreeSet<String>>,
    out: &mut BTreeSet<String>,
) {
    if matches!(mnemonic, "bl" | "blr" | "call_iat") {
        let lr = canonical_reg_family(isa, "lr");
        if !lr.is_empty() {
            out.insert(lr);
        }
    }
    if mnemonic == "call_iat" {
        if matches!(isa, CpuIsa::ArmV4 | CpuIsa::ArmV7) {
            for scratch in ["r0", "r1", "r2", "r3", "r12", "lr"] {
                out.insert(scratch.into());
            }
        } else if isa == CpuIsa::Arm64 {
            for scratch in [
                "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",
                "x13", "x14", "x15", "x16", "x17", "x30",
            ] {
                out.insert(scratch.into());
            }
        }
        return;
    }
    if mnemonic == "blr" && isa == CpuIsa::Arm64 {
        for scratch in [
            "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12",
            "x13", "x14", "x15", "x16", "x17", "x30",
        ] {
            out.insert(scratch.into());
        }
        return;
    }
    if let Some(callee) = operands.first() {
        let trimmed = callee.trim();
        let lo = trimmed.to_ascii_lowercase();
        if let Some(cset) = known_clobbers.get(trimmed) {
            for r in cset {
                out.insert(r.clone());
            }
        } else if lo.starts_with("far ")
            && lo.contains("_p")
            && matches!(isa, CpuIsa::X86_8086 | CpuIsa::X86_286)
        {
            for scratch in ["ax", "bx", "cx", "dx", "es"] {
                out.insert(scratch.into());
            }
        } else if callee.contains("__imp_") && isa == CpuIsa::X86_64 {
            for scratch in ["rax", "rcx", "rdx", "r8", "r9", "r10", "r11"] {
                out.insert(scratch.into());
            }
        } else if callee.contains("__imp_") && isa == CpuIsa::Ia32 {
            for scratch in ["eax", "ecx", "edx"] {
                out.insert(scratch.into());
            }
        }
    }
}

fn is_readonly_or_branch_mnemonic(mnemonic: &str, operands: &[String]) -> bool {
    matches!(
        mnemonic,
        "cmp"
            | "test"
            | "tst"
            | "jmp"
            | "b"
            | "bx"
            | "ret"
            | "retn"
            | "retf"
            | "iret"
            | "str"
            | "strb"
            | "strh"
            | "stp"
            | "nop"
            | "clc"
            | "stc"
            | "cld"
            | "std"
            | "cli"
            | "sti"
            | "int"
            | "svc"
            | "cbz"
            | "cbnz"
            | "tbz"
            | "tbnz"
            | "dmb"
            | "dsb"
            | "isb"
            | "hint"
    ) || (mnemonic.starts_with("it")
        && mnemonic.bytes().skip(2).all(|b| b == b't' || b == b'e'))
        || conditional_branch_target(mnemonic, operands).is_some()
}

fn collect_modified_regs(
    isa: CpuIsa,
    mnemonic: &str,
    operands: &[String],
    known_clobbers: &BTreeMap<String, BTreeSet<String>>,
    out: &mut BTreeSet<String>,
) {
    if is_readonly_or_branch_mnemonic(mnemonic, operands) {
        if mnemonic.starts_with("loop") {
            let cx = canonical_reg_family(isa, "cx");
            if !cx.is_empty() {
                out.insert(cx);
            }
        }
        return;
    }

    if matches!(mnemonic, "enter" | "leave" | "popa") {
        let bp = canonical_reg_family(isa, "bp");
        if !bp.is_empty() {
            out.insert(bp);
        }
        if mnemonic == "popa" {
            for r in ["ax", "bx", "cx", "dx", "si", "di"] {
                let fam = canonical_reg_family(isa, r);
                if !fam.is_empty() {
                    out.insert(fam);
                }
            }
        }
        return;
    }

    if matches!(mnemonic, "call" | "bl" | "blr" | "call_iat" | "blx") {
        if mnemonic == "blx" && matches!(isa, CpuIsa::ArmV4 | CpuIsa::ArmV7) {
            for scratch in ["r0", "r1", "r2", "r3", "r12", "lr"] {
                out.insert(scratch.into());
            }
            return;
        }
        collect_call_clobbers(isa, mnemonic, operands, known_clobbers, out);
        return;
    }

    if collect_arm_special_modified_regs(isa, mnemonic, operands, out) {
        return;
    }

    if matches!(mnemonic, "mul" | "imul" | "div" | "idiv") && operands.len() == 1 {
        let a = canonical_reg_family(isa, "ax");
        let d = canonical_reg_family(isa, "dx");
        if !a.is_empty() {
            out.insert(a);
        }
        if !d.is_empty() {
            out.insert(d);
        }
        return;
    }

    if matches!(mnemonic, "cwd" | "cdq" | "cqo") {
        let d = canonical_reg_family(isa, "dx");
        if !d.is_empty() {
            out.insert(d);
        }
        return;
    }

    if collect_string_modified_regs(isa, mnemonic, out) {
        return;
    }

    if mnemonic == "xchg" && operands.len() == 2 {
        for op in operands {
            let fam = canonical_reg_family(isa, op);
            if !fam.is_empty() {
                out.insert(fam);
            }
        }
        return;
    }

    if mnemonic == "ldp" && operands.len() >= 2 {
        for op in &operands[..2] {
            let fam = canonical_reg_family(isa, op);
            if !fam.is_empty() {
                out.insert(fam);
            }
        }
        return;
    }

    if let Some(op0) = operands.first() {
        let fam = canonical_reg_family(isa, op0);
        if !fam.is_empty() {
            out.insert(fam);
        }
    }
}

fn collect_arm_special_modified_regs(
    isa: CpuIsa,
    mnemonic: &str,
    operands: &[String],
    out: &mut BTreeSet<String>,
) -> bool {
    if mnemonic == "pop"
        && matches!(isa, CpuIsa::ArmV4 | CpuIsa::ArmV7)
        && let Some(op0) = operands.first()
    {
        for part in op0.trim().trim_matches(&['{', '}'][..]).split(',') {
            let fam = canonical_reg_family(isa, part);
            if !fam.is_empty() {
                out.insert(fam);
            }
        }
        return true;
    }
    if matches!(mnemonic, "umull" | "smull" | "umlal" | "smlal") && operands.len() >= 2 {
        for op in &operands[..2] {
            let fam = canonical_reg_family(isa, op);
            if !fam.is_empty() {
                out.insert(fam);
            }
        }
        return true;
    }
    if mnemonic == "mrc" && operands.len() >= 3 {
        let fam = canonical_reg_family(isa, &operands[2]);
        if !fam.is_empty() {
            out.insert(fam);
        }
        return true;
    }
    false
}

fn collect_string_modified_regs(
    isa: CpuIsa,
    mnemonic: &str,
    out: &mut BTreeSet<String>,
) -> bool {
    if (mnemonic.contains("movs") && !mnemonic.contains("movsx")) || mnemonic.contains("cmps") {
        for r in ["si", "di"] {
            let fam = canonical_reg_family(isa, r);
            if !fam.is_empty() {
                out.insert(fam);
            }
        }
        if mnemonic.starts_with("rep") {
            let c = canonical_reg_family(isa, "cx");
            if !c.is_empty() {
                out.insert(c);
            }
        }
        return true;
    }
    if mnemonic.contains("stos") || mnemonic.contains("scas") {
        let di = canonical_reg_family(isa, "di");
        if !di.is_empty() {
            out.insert(di);
        }
        if mnemonic.starts_with("rep") {
            let c = canonical_reg_family(isa, "cx");
            if !c.is_empty() {
                out.insert(c);
            }
        }
        return true;
    }
    if mnemonic.contains("lods") {
        for r in ["ax", "si"] {
            let fam = canonical_reg_family(isa, r);
            if !fam.is_empty() {
                out.insert(fam);
            }
        }
        return true;
    }
    false
}

fn extract_code_payload(bytes: &[u8]) -> &[u8] {
    if bytes.len() < 0x40 || &bytes[0..2] != b"MZ" {
        return bytes;
    }
    let lfanew = usize::try_from(u32::from_le_bytes([
        bytes[0x3C],
        bytes[0x3D],
        bytes[0x3E],
        bytes[0x3F],
    ]))
    .unwrap_or(usize::MAX);
    if lfanew.saturating_add(0x40) <= bytes.len() && &bytes[lfanew..lfanew + 2] == b"NE" {
        let cseg = usize::from(u16::from_le_bytes([
            bytes[lfanew + 0x1C],
            bytes[lfanew + 0x1D],
        ]));
        let segtab = usize::from(u16::from_le_bytes([
            bytes[lfanew + 0x22],
            bytes[lfanew + 0x23],
        ]));
        let align_shift = {
            let raw = u16::from_le_bytes([bytes[lfanew + 0x32], bytes[lfanew + 0x33]]);
            u32::from(if raw == 0 { 9 } else { raw })
        };
        let s0 = lfanew.saturating_add(segtab);
        if cseg >= 1 && s0.saturating_add(8) <= bytes.len() {
            let sec_off = usize::from(u16::from_le_bytes([bytes[s0], bytes[s0 + 1]]));
            let raw_len = usize::from(u16::from_le_bytes([bytes[s0 + 2], bytes[s0 + 3]]));
            let byte_len = if raw_len == 0 { 65_536 } else { raw_len };
            if let Some(file_off) = sec_off.checked_shl(align_shift)
                && file_off > 0
                && file_off.saturating_add(byte_len) <= bytes.len()
            {
                return &bytes[file_off..file_off + byte_len];
            }
        }
        return bytes;
    }
    if lfanew.saturating_add(24) > bytes.len() || &bytes[lfanew..lfanew + 4] != b"PE\0\0" {
        return bytes;
    }
    let num_secs = usize::from(u16::from_le_bytes([bytes[lfanew + 6], bytes[lfanew + 7]]));
    let opt_sz = usize::from(u16::from_le_bytes([bytes[lfanew + 20], bytes[lfanew + 21]]));
    let sec_base = lfanew + 24 + opt_sz;
    for idx in 0..num_secs {
        let off = sec_base + idx * 40;
        if off + 40 > bytes.len() {
            break;
        }
        let chars = u32::from_le_bytes([
            bytes[off + 36],
            bytes[off + 37],
            bytes[off + 38],
            bytes[off + 39],
        ]);
        if &bytes[off..off + 5] == b".text" || (chars & 0x20) != 0 {
            let virt_sz = usize::try_from(u32::from_le_bytes([
                bytes[off + 8],
                bytes[off + 9],
                bytes[off + 10],
                bytes[off + 11],
            ]))
            .unwrap_or(0);
            let raw_sz = usize::try_from(u32::from_le_bytes([
                bytes[off + 16],
                bytes[off + 17],
                bytes[off + 18],
                bytes[off + 19],
            ]))
            .unwrap_or(0);
            let raw_ptr = usize::try_from(u32::from_le_bytes([
                bytes[off + 20],
                bytes[off + 21],
                bytes[off + 22],
                bytes[off + 23],
            ]))
            .unwrap_or(0);
            let len = if virt_sz > 0 { virt_sz.min(raw_sz) } else { raw_sz };
            if raw_ptr.saturating_add(len) <= bytes.len() {
                return &bytes[raw_ptr..raw_ptr + len];
            }
        }
    }
    bytes
}

const fn is_equiv_x86_sib_scale1(sib1: u8, sib2: u8) -> bool {
    if (sib1 & 0xC0) != 0 || (sib2 & 0xC0) != 0 {
        return false;
    }
    let idx1 = (sib1 >> 3) & 7;
    let base1 = sib1 & 7;
    let idx2 = (sib2 >> 3) & 7;
    let base2 = sib2 & 7;
    idx1 != 4 && idx2 != 4 && idx1 == base2 && base1 == idx2
}

const fn is_equiv_x86_mod11_pair(op1: u8, m1: u8, op2: u8, m2: u8) -> bool {
    if (m1 & 0xC0) != 0xC0 || (m2 & 0xC0) != 0xC0 {
        return false;
    }
    if ((m1 >> 3) & 7) != (m2 & 7) || (m1 & 7) != ((m2 >> 3) & 7) {
        return false;
    }
    let dir_pair = (op1 ^ op2) == 0x02
        && matches!(
            op1 & 0xFC,
            0x00 | 0x08 | 0x10 | 0x18 | 0x20 | 0x28 | 0x30 | 0x38 | 0x88
        );
    let comm_pair = op1 == op2 && matches!(op1, 0x84..=0x87);
    dir_pair || comm_pair
}

const fn is_ia32_pe_image(raw: &[u8]) -> bool {
    if raw.len() < 0x40 || raw[0] != b'M' || raw[1] != b'Z' {
        return false;
    }
    let lfanew = u32::from_le_bytes([raw[0x3C], raw[0x3D], raw[0x3E], raw[0x3F]]) as usize;
    if lfanew.saturating_add(6) > raw.len() {
        return false;
    }
    raw[lfanew] == b'P'
        && raw[lfanew + 1] == b'E'
        && raw[lfanew + 2] == 0
        && raw[lfanew + 3] == 0
        && u16::from_le_bytes([raw[lfanew + 4], raw[lfanew + 5]]) == 0x014C
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum Ia32NormInsn {
    Branch { cond: u8, target_off: usize },
    CallRel { target_off: usize },
    PushOrMovTextVa { prefix_op: Vec<u8>, target_off: usize },
    Bytes(Vec<u8>),
}

fn decode_ia32_modrm_len(bytes: &[u8], modrm_pos: usize) -> Option<(usize, Option<[u8; 2]>)> {
    let modrm = *bytes.get(modrm_pos)?;
    let md = modrm >> 6;
    let reg = (modrm >> 3) & 7;
    let rm = modrm & 7;
    if md == 3 {
        return Some((1, None));
    }
    let mut len = 1_usize;
    let mut norm_ebp_sib: Option<[u8; 2]> = None;
    if rm == 4 {
        let sib = *bytes.get(modrm_pos + 1)?;
        len += 1;
        let scale = sib >> 6;
        let idx = (sib >> 3) & 7;
        let base = sib & 7;
        if md == 0 && base == 5 {
            len += 4;
        } else if md == 0 && scale == 0 && idx == 5 && base != 4 && base != 5 {
            // [reg + ebp] -> canonicalize to (reg, base_reg) pair
            norm_ebp_sib = Some([reg, base]);
        } else if md == 1
            && scale == 0
            && base == 5
            && idx != 4
            && idx != 5
            && bytes.get(modrm_pos + 2) == Some(&0)
        {
            // [ebp + reg + 0] -> canonicalize to identical (reg, idx_reg) pair!
            norm_ebp_sib = Some([reg, idx]);
        }
    }
    if md == 1 {
        len += 1;
    } else if md == 2 || (md == 0 && rm == 5) {
        len += 4;
    }
    if modrm_pos.saturating_add(len) <= bytes.len() {
        Some((len, norm_ebp_sib))
    } else {
        None
    }
}

fn decode_ia32_rel_ctrl(
    bytes: &[u8],
    pc: usize,
    p: usize,
    op: u8,
) -> Option<(usize, Ia32NormInsn)> {
    if (0x70..=0x7F).contains(&op) || op == 0xEB {
        let d = (*bytes.get(p)?).cast_signed();
        let end = p + 1;
        let tgt = end.wrapping_add_signed(isize::from(d));
        let cond = if op == 0xEB { 0xFF } else { op & 0x0F };
        return Some((
            end - pc,
            Ia32NormInsn::Branch {
                cond,
                target_off: tgt,
            },
        ));
    }
    if op == 0xE9 || op == 0xE8 {
        let d = i32::from_le_bytes([
            *bytes.get(p)?,
            *bytes.get(p + 1)?,
            *bytes.get(p + 2)?,
            *bytes.get(p + 3)?,
        ]);
        let end = p + 4;
        let tgt = end.wrapping_add_signed(d as isize);
        let insn = if op == 0xE9 {
            Ia32NormInsn::Branch {
                cond: 0xFF,
                target_off: tgt,
            }
        } else {
            Ia32NormInsn::CallRel { target_off: tgt }
        };
        return Some((end - pc, insn));
    }
    let d = (*bytes.get(p)?).cast_signed();
    let end = p + 1;
    let tgt = end.wrapping_add_signed(isize::from(d));
    Some((
        end - pc,
        Ia32NormInsn::Branch {
            cond: op,
            target_off: tgt,
        },
    ))
}

fn decode_ia32_insn(bytes: &[u8], pc: usize) -> Option<(usize, Ia32NormInsn)> {
    let mut p = pc;
    let mut has_66 = false;
    while p < bytes.len() {
        let b = bytes[p];
        if b == 0x66 {
            has_66 = true;
            p += 1;
        } else if matches!(
            b,
            0x26 | 0x2E | 0x36 | 0x3E | 0x64 | 0x65 | 0x67 | 0xF0 | 0xF2 | 0xF3
        ) {
            p += 1;
        } else {
            break;
        }
    }
    let op = *bytes.get(p)?;
    let mut sorted_prefixes = bytes[pc..p].to_vec();
    sorted_prefixes.sort_unstable();
    let imm_word = if has_66 { 2_usize } else { 4_usize };
    p += 1;

    if op == 0x0F {
        let op2 = *bytes.get(p)?;
        p += 1;
        if (0x80..=0x8F).contains(&op2) {
            let d = i32::from_le_bytes([
                *bytes.get(p)?,
                *bytes.get(p + 1)?,
                *bytes.get(p + 2)?,
                *bytes.get(p + 3)?,
            ]);
            let end = p + 4;
            let tgt = end.wrapping_add_signed(d as isize);
            return Some((
                end - pc,
                Ia32NormInsn::Branch {
                    cond: op2 & 0x0F,
                    target_off: tgt,
                },
            ));
        }
        let (mlen, ebp_pair) = decode_ia32_modrm_len(bytes, p)?;
        let extra = usize::from(op2 == 0xBA);
        let end = p + mlen + extra;
        if end > bytes.len() {
            return None;
        }
        let mut raw = sorted_prefixes;
        raw.extend_from_slice(&[op, op2]);
        if let Some([r, idx]) = ebp_pair {
            raw.extend_from_slice(&[0x04 | (r << 3), (5 << 3) | idx]);
        } else {
            raw.extend_from_slice(&bytes[p..p + mlen]);
        }
        raw.extend_from_slice(&bytes[p + mlen..end]);
        return Some((end - pc, Ia32NormInsn::Bytes(raw)));
    }

    if matches!(op, 0x70..=0x7F | 0xE0..=0xE3 | 0xE8 | 0xE9 | 0xEB) {
        return decode_ia32_rel_ctrl(bytes, pc, p, op);
    }

    decode_ia32_fixed_or_modrm(bytes, pc, p, op, imm_word, sorted_prefixes)
}

fn decode_ia32_fixed_or_modrm(
    bytes: &[u8],
    pc: usize,
    p: usize,
    op: u8,
    imm_word: usize,
    sorted_prefixes: Vec<u8>,
) -> Option<(usize, Ia32NormInsn)> {
    let fixed_extra: Option<usize> = match op {
        0x06 | 0x07 | 0x0E | 0x16 | 0x17 | 0x1E | 0x1F | 0x27 | 0x2F | 0x37 | 0x3F
        | 0x40..=0x5F | 0x60 | 0x61 | 0x90..=0x99 | 0x9B..=0x9F | 0xA4..=0xA7 | 0xAA..=0xAF
        | 0xC3 | 0xCB | 0xCC | 0xCE | 0xCF | 0xD7 | 0xF4 | 0xF5 | 0xF8..=0xFD => Some(0),
        0x04 | 0x0C | 0x14 | 0x1C | 0x24 | 0x2C | 0x34 | 0x3C | 0x6A | 0xA8 | 0xB0..=0xB7
        | 0xCD | 0xD4 | 0xD5 | 0xE4..=0xE7 => Some(1),
        0xC2 | 0xCA => Some(2),
        0xC8 => Some(3),
        0x05 | 0x0D | 0x15 | 0x1D | 0x25 | 0x2D | 0x35 | 0x3D | 0x68 | 0xA9 | 0xB8..=0xBF => {
            Some(imm_word)
        }
        0xA0..=0xA3 => Some(4),
        _ => None,
    };
    if let Some(ex) = fixed_extra {
        let end = p + ex;
        if end > bytes.len() {
            return None;
        }
        if ex == 4 && (op == 0x68 || (0xB8..=0xBF).contains(&op)) {
            let imm = u32::from_le_bytes([
                bytes[p],
                bytes[p + 1],
                bytes[p + 2],
                bytes[p + 3],
            ]) as usize;
            if imm >= 0x0040_1000 && imm < 0x0040_1000_usize.saturating_add(bytes.len()) {
                let mut pop = sorted_prefixes;
                pop.push(op);
                return Some((
                    end - pc,
                    Ia32NormInsn::PushOrMovTextVa {
                        prefix_op: pop,
                        target_off: imm - 0x0040_1000,
                    },
                ));
            }
        }
        let mut raw = sorted_prefixes;
        raw.push(op);
        raw.extend_from_slice(&bytes[p..end]);
        return Some((end - pc, Ia32NormInsn::Bytes(raw)));
    }

    let modrm_byte = *bytes.get(p)?;
    let reg_fld = (modrm_byte >> 3) & 7;
    let (mlen, ebp_pair) = decode_ia32_modrm_len(bytes, p)?;
    let imm_extra = match op {
        0x69 | 0x81 | 0xC7 => imm_word,
        0xF7 if reg_fld <= 1 => imm_word,
        0x6B | 0x80 | 0x82 | 0x83 | 0xC0 | 0xC1 | 0xC6 => 1,
        0xF6 if reg_fld <= 1 => 1,
        _ => 0,
    };
    let end = p + mlen + imm_extra;
    if end > bytes.len() {
        return None;
    }
    let mut raw = sorted_prefixes;
    raw.push(op);
    if let Some([r, idx]) = ebp_pair {
        raw.extend_from_slice(&[0x04 | (r << 3), (5 << 3) | idx]);
    } else {
        raw.extend_from_slice(&bytes[p..p + mlen]);
    }
    raw.extend_from_slice(&bytes[p + mlen..end]);
    Some((end - pc, Ia32NormInsn::Bytes(raw)))
}

fn same_target_idx(
    base_map: &std::collections::HashMap<usize, usize>,
    base_off: usize,
    other_map: &std::collections::HashMap<usize, usize>,
    other_off: usize,
) -> bool {
    matches!(
        (base_map.get(&base_off), other_map.get(&other_off)),
        (Some(idx_a), Some(idx_b)) if idx_a == idx_b
    )
}

fn decode_ia32_stream(bytes: &[u8]) -> Option<(Vec<Ia32NormInsn>, std::collections::HashMap<usize, usize>)> {
    let trimmed_end = bytes
        .iter()
        .rposition(|&b| b != 0)
        .map_or(0, |idx| idx + 1);
    let mut insns = Vec::new();
    let mut off_to_idx = std::collections::HashMap::new();
    let mut pc = 0_usize;
    while pc < trimmed_end {
        let idx = insns.len();
        off_to_idx.insert(pc, idx);
        let (len, insn) = decode_ia32_insn(&bytes[..trimmed_end], pc)?;
        insns.push(insn);
        pc += len;
    }
    off_to_idx.insert(trimmed_end, insns.len());
    Some((insns, off_to_idx))
}

fn verify_ia32_stream_equiv(
    artifact: &str,
    base_name: &str,
    base_bytes: &[u8],
    other_name: &str,
    other_bytes: &[u8],
) -> Result<(), ToolError> {
    let Some((b_insns, b_map)) = decode_ia32_stream(base_bytes) else {
        return Err(ToolError::Equivalence {
            artifact: artifact.to_owned(),
            offset: 0,
            details: format!("failed to decode IA-32 instruction stream for {base_name}"),
        });
    };
    let Some((o_insns, o_map)) = decode_ia32_stream(other_bytes) else {
        return Err(ToolError::Equivalence {
            artifact: artifact.to_owned(),
            offset: 0,
            details: format!("failed to decode IA-32 instruction stream for {other_name}"),
        });
    };
    if b_insns.len() != o_insns.len() {
        return Err(ToolError::Equivalence {
            artifact: artifact.to_owned(),
            offset: b_insns.len().min(o_insns.len()),
            details: format!(
                "IA-32 instruction count mismatch: {base_name}={} insns vs {other_name}={} insns",
                b_insns.len(),
                o_insns.len()
            ),
        });
    }
    for (idx, (bi, oi)) in b_insns.iter().zip(o_insns.iter()).enumerate() {
        let ok = match (bi, oi) {
            (
                Ia32NormInsn::Branch {
                    cond: c1,
                    target_off: t1,
                },
                Ia32NormInsn::Branch {
                    cond: c2,
                    target_off: t2,
                },
            ) => c1 == c2 && same_target_idx(&b_map, *t1, &o_map, *t2),
            (Ia32NormInsn::CallRel { target_off: t1 }, Ia32NormInsn::CallRel { target_off: t2 }) => {
                same_target_idx(&b_map, *t1, &o_map, *t2)
            }
            (
                Ia32NormInsn::PushOrMovTextVa {
                    prefix_op: p1,
                    target_off: t1,
                },
                Ia32NormInsn::PushOrMovTextVa {
                    prefix_op: p2,
                    target_off: t2,
                },
            ) => p1 == p2 && same_target_idx(&b_map, *t1, &o_map, *t2),
            (Ia32NormInsn::Bytes(v1), Ia32NormInsn::Bytes(v2)) => {
                let (s1, s2) = if v1.first() == Some(&0x66) && v2.first() == Some(&0x66) {
                    (&v1[1..], &v2[1..])
                } else {
                    (&v1[..], &v2[..])
                };
                v1 == v2
                    || (s1.len() == 2
                        && s2.len() == 2
                        && is_equiv_x86_mod11_pair(s1[0], s1[1], s2[0], s2[1]))
                    || (s1.len() >= 3
                        && s1.len() == s2.len()
                        && s1[0] == s2[0]
                        && s1[1] == s2[1]
                        && (s1[1] & 0x07) == 0x04
                        && (s1[1] & 0xC0) != 0xC0
                        && is_equiv_x86_sib_scale1(s1[2], s2[2])
                        && s1[3..] == s2[3..])
            }
            _ => false,
        };
        if !ok {
            return Err(ToolError::Equivalence {
                artifact: artifact.to_owned(),
                offset: idx,
                details: format!(
                    "instruction #{idx} mismatch: {base_name}={bi:?} vs {other_name}={oi:?}"
                ),
            });
        }
    }
    Ok(())
}

/// Verifies that multiple assembled binary artifacts (e.g. `NASM` vs `FASM` vs `JWasm`)
/// have normalized instruction-stream equivalence across `.COM`, `NE`, `PE32`, and `PE32+`.
///
/// # Errors
///
/// Returns [`ToolError::Equivalence`] on the first non-equivalent instruction or length mismatch.
pub fn verify_binary_equivalence(
    artifact: &str,
    outputs: &[(&str, &[u8])],
) -> Result<(), ToolError> {
    let Some((base_name, raw_base)) = outputs.first() else {
        return Ok(());
    };
    let base_bytes = extract_code_payload(raw_base);
    for &(other_name, raw_other) in &outputs[1..] {
        let other_bytes = extract_code_payload(raw_other);
        if is_ia32_pe_image(raw_base) && is_ia32_pe_image(raw_other) {
            verify_ia32_stream_equiv(artifact, base_name, base_bytes, other_name, other_bytes)?;
            continue;
        }
        if x64_equiv::is_x64_pe_image(raw_base) && x64_equiv::is_x64_pe_image(raw_other) {
            x64_equiv::verify_x64_stream_equiv(
                artifact,
                base_name,
                base_bytes,
                other_name,
                other_bytes,
            )?;
            continue;
        }
        if base_bytes.len() != other_bytes.len() {
            return Err(ToolError::Equivalence {
                artifact: artifact.to_owned(),
                offset: base_bytes.len().min(other_bytes.len()),
                details: format!(
                    "code size mismatch: {base_name}={} bytes vs {other_name}={} bytes",
                    base_bytes.len(),
                    other_bytes.len()
                ),
            });
        }
        let mut i = 0_usize;
        while i < base_bytes.len() {
            if i + 6 <= base_bytes.len()
                && base_bytes[i] == other_bytes[i]
                && base_bytes[i + 1] == other_bytes[i + 1]
                && ((base_bytes[i] == 0xFF && base_bytes[i + 1] == 0x15)
                    || (base_bytes[i] == 0x8D && (base_bytes[i + 1] & 0xC7) == 0x05))
            {
                i += 6;
                continue;
            }
            if i + 1 < base_bytes.len()
                && (base_bytes[i] != other_bytes[i] || base_bytes[i + 1] != other_bytes[i + 1])
                && is_equiv_x86_mod11_pair(
                    base_bytes[i],
                    base_bytes[i + 1],
                    other_bytes[i],
                    other_bytes[i + 1],
                )
            {
                i += 2;
                continue;
            }
            if base_bytes[i] == other_bytes[i] {
                i += 1;
                continue;
            }
            if i >= 2
                && base_bytes[i - 2] == other_bytes[i - 2]
                && base_bytes[i - 1] == other_bytes[i - 1]
                && (base_bytes[i - 1] & 0x07) == 0x04
                && (base_bytes[i - 1] & 0xC0) != 0xC0
                && is_equiv_x86_sib_scale1(base_bytes[i], other_bytes[i])
            {
                i += 1;
                continue;
            }
            if i + 2 < base_bytes.len()
                && (0x40..=0x4F).contains(&base_bytes[i])
                && (0x40..=0x4F).contains(&other_bytes[i])
                && (base_bytes[i] & 0xFA) == (other_bytes[i] & 0xFA)
                && ((base_bytes[i] >> 2) & 1) == (other_bytes[i] & 1)
                && (base_bytes[i] & 1) == ((other_bytes[i] >> 2) & 1)
                && is_equiv_x86_mod11_pair(
                    base_bytes[i + 1],
                    base_bytes[i + 2],
                    other_bytes[i + 1],
                    other_bytes[i + 2],
                )
            {
                i += 3;
                continue;
            }
            return Err(ToolError::Equivalence {
                artifact: artifact.to_owned(),
                offset: i,
                details: format!(
                    "{base_name}=0x{:02X} vs {other_name}=0x{:02X}",
                    base_bytes[i], other_bytes[i]
                ),
            });
        }
    }
    Ok(())
}

fn check_pe_purity(file_label: &str, isa: CpuIsa, bytes: &[u8]) -> Result<(), ToolError> {
    let lower_ascii: Vec<u8> = bytes.iter().map(u8::to_ascii_lowercase).collect();
    for banned in [
        &b"msvcrt.dll"[..],
        &b"ucrtbase.dll"[..],
        &b"vcruntime"[..],
        &b"crtdll.dll"[..],
    ] {
        if lower_ascii.windows(banned.len()).any(|w| w == banned) {
            return Err(ToolError::OpcodePurity {
                span: SourceSpan::new(file_label, 1, 1),
                isa,
                message: "forbidden C runtime DLL dependency detected in binary".into(),
            });
        }
    }
    if bytes.len() < 0x40 || &bytes[0..2] != b"MZ" {
        return Ok(());
    }
    let lfanew = usize::try_from(u32::from_le_bytes([
        bytes[0x3C],
        bytes[0x3D],
        bytes[0x3E],
        bytes[0x3F],
    ]))
    .unwrap_or(usize::MAX);
    if lfanew.saturating_add(24) > bytes.len() || &bytes[lfanew..lfanew + 4] != b"PE\0\0" {
        return Ok(());
    }
    let machine = u16::from_le_bytes([bytes[lfanew + 4], bytes[lfanew + 5]]);
    let ok_mach = match isa {
        CpuIsa::Ia32 => machine == 0x014C,
        CpuIsa::X86_64 => machine == 0x8664,
        CpuIsa::ArmV4 => matches!(machine, 0x01C0 | 0x01C2),
        CpuIsa::ArmV7 => machine == 0x01C4,
        CpuIsa::Arm64 => machine == 0xAA64,
        CpuIsa::X86_8086 | CpuIsa::X86_286 => false,
    };
    if !ok_mach {
        return Err(ToolError::OpcodePurity {
            span: SourceSpan::new(file_label, 1, 1),
            isa,
            message: format!("PE Machine 0x{machine:04X} does not match target ISA `{isa}`"),
        });
    }
    if isa == CpuIsa::Ia32 && lfanew + 24 + 184 <= bytes.len() {
        let opt = lfanew + 24;
        let subsys_maj = u16::from_le_bytes([bytes[opt + 48], bytes[opt + 49]]);
        let subsys_min = u16::from_le_bytes([bytes[opt + 50], bytes[opt + 51]]);
        let subsys = u16::from_le_bytes([bytes[opt + 68], bytes[opt + 69]]);
        let dll_chars = u16::from_le_bytes([bytes[opt + 70], bytes[opt + 71]]);
        let load_cfg_rva = u32::from_le_bytes([
            bytes[opt + 176],
            bytes[opt + 177],
            bytes[opt + 178],
            bytes[opt + 179],
        ]);
        if subsys != 3 || subsys_maj != 3 || subsys_min != 10 {
            return Err(ToolError::AbiLint {
                span: SourceSpan::new(file_label, 1, 1),
                proc_name: "<pe32>".into(),
                message: format!(
                    "IA-32 PE32 must target CUI (3) Subsystem 3.10 for NT 3.1..11 & Win95..Me (got subsys={subsys}, ver={subsys_maj}.{subsys_min:02})"
                ),
            });
        }
        if (dll_chars & 0x0140) != 0x0140 {
            return Err(ToolError::AbiLint {
                span: SourceSpan::new(file_label, 1, 1),
                proc_name: "<pe32>".into(),
                message: format!(
                    "IA-32 PE32 must enable DYNAMIC_BASE (0x0040) and NX_COMPAT (0x0100) DllCharacteristics (got 0x{dll_chars:04X})"
                ),
            });
        }
        if load_cfg_rva == 0 {
            return Err(ToolError::AbiLint {
                span: SourceSpan::new(file_label, 1, 1),
                proc_name: "<pe32>".into(),
                message: "IA-32 PE32 is missing mandatory /SAFESEH LOAD_CONFIG directory".into(),
            });
        }
    }
    if isa == CpuIsa::X86_64 && lfanew + 24 + 200 <= bytes.len() {
        check_pe64_purity(file_label, bytes, lfanew + 24)?;
    }
    if matches!(isa, CpuIsa::ArmV4 | CpuIsa::ArmV7) && lfanew + 24 + 184 <= bytes.len() {
        check_arm32_pe_purity(file_label, isa, bytes, lfanew + 24)?;
    }
    if isa == CpuIsa::Arm64 && lfanew + 24 + 200 <= bytes.len() {
        check_arm64_pe_purity(file_label, bytes, lfanew + 24)?;
    }
    Ok(())
}

fn check_arm32_pe_purity(
    file_label: &str,
    isa: CpuIsa,
    bytes: &[u8],
    opt: usize,
) -> Result<(), ToolError> {
    let subsys_maj = u16::from_le_bytes([bytes[opt + 48], bytes[opt + 49]]);
    let subsys_min = u16::from_le_bytes([bytes[opt + 50], bytes[opt + 51]]);
    let subsys = u16::from_le_bytes([bytes[opt + 68], bytes[opt + 69]]);
    let dll_chars = u16::from_le_bytes([bytes[opt + 70], bytes[opt + 71]]);
    let rsrc_rva = u32::from_le_bytes([bytes[opt + 112], bytes[opt + 113], bytes[opt + 114], bytes[opt + 115]]);
    let pdata_rva = u32::from_le_bytes([bytes[opt + 120], bytes[opt + 121], bytes[opt + 122], bytes[opt + 123]]);
    let lcfg_rva = u32::from_le_bytes([bytes[opt + 176], bytes[opt + 177], bytes[opt + 178], bytes[opt + 179]]);

    if isa == CpuIsa::ArmV4 {
        if !matches!(subsys, 9 | 3) || subsys_maj != 4 || pdata_rva == 0 {
            return Err(ToolError::AbiLint {
                span: SourceSpan::new(file_label, 1, 1),
                proc_name: "<armv4_pe>".into(),
                message: format!(
                    "ARMv4 PE32 must target WinCE Subsystem 9 (or CUI 3) 4.0 with .pdata (got subsys={subsys}, ver={subsys_maj}.{subsys_min:02}, pdata=0x{pdata_rva:X})"
                ),
            });
        }
    } else {
        if subsys != 3 || subsys_maj != 6 || subsys_min != 2 {
            return Err(ToolError::AbiLint {
                span: SourceSpan::new(file_label, 1, 1),
                proc_name: "<armnt_pe>".into(),
                message: format!(
                    "ARMv7 ARMNT PE32 must target CUI (3) Subsystem 6.02 for Windows RT 8.0..Win10 ARM32 (got subsys={subsys}, ver={subsys_maj}.{subsys_min:02})"
                ),
            });
        }
        if (dll_chars & 0x4140) != 0x4140 || pdata_rva == 0 || rsrc_rva == 0 || lcfg_rva == 0 {
            return Err(ToolError::AbiLint {
                span: SourceSpan::new(file_label, 1, 1),
                proc_name: "<armnt_pe>".into(),
                message: format!(
                    "ARMv7 ARMNT PE32 requires DYNAMIC_BASE|NX_COMPAT|GUARD_CF (0x4140) + .pdata + .rsrc + LOAD_CONFIG (dll=0x{dll_chars:04X}, pdata=0x{pdata_rva:X}, rsrc=0x{rsrc_rva:X}, lcfg=0x{lcfg_rva:X})"
                ),
            });
        }
    }
    Ok(())
}

fn check_arm64_pe_purity(file_label: &str, bytes: &[u8], opt: usize) -> Result<(), ToolError> {
    let subsys_maj = u16::from_le_bytes([bytes[opt + 48], bytes[opt + 49]]);
    let subsys_min = u16::from_le_bytes([bytes[opt + 50], bytes[opt + 51]]);
    let subsys = u16::from_le_bytes([bytes[opt + 68], bytes[opt + 69]]);
    let dll_chars = u16::from_le_bytes([bytes[opt + 70], bytes[opt + 71]]);
    let rsrc_rva = u32::from_le_bytes([bytes[opt + 128], bytes[opt + 129], bytes[opt + 130], bytes[opt + 131]]);
    let pdata_rva = u32::from_le_bytes([bytes[opt + 136], bytes[opt + 137], bytes[opt + 138], bytes[opt + 139]]);
    let lcfg_rva = u32::from_le_bytes([bytes[opt + 192], bytes[opt + 193], bytes[opt + 194], bytes[opt + 195]]);
    if subsys != 3 || subsys_maj != 10 || subsys_min != 0 {
        return Err(ToolError::AbiLint {
            span: SourceSpan::new(file_label, 1, 1),
            proc_name: "<arm64_pe>".into(),
            message: format!(
                "ARM64 PE32+ must target CUI (3) Subsystem 10.00 for Windows 10/11 on ARM (got subsys={subsys}, ver={subsys_maj}.{subsys_min:02})"
            ),
        });
    }
    if (dll_chars & 0x0140) != 0x0140 || pdata_rva == 0 || rsrc_rva == 0 || lcfg_rva == 0 {
        return Err(ToolError::AbiLint {
            span: SourceSpan::new(file_label, 1, 1),
            proc_name: "<arm64_pe>".into(),
            message: format!(
                "ARM64 PE32+ requires DYNAMIC_BASE|NX_COMPAT (0x0140) + .pdata + .rsrc + LOAD_CONFIG (dll=0x{dll_chars:04X}, pdata=0x{pdata_rva:X}, rsrc=0x{rsrc_rva:X}, lcfg=0x{lcfg_rva:X})"
            ),
        });
    }
    Ok(())
}

fn check_pe64_purity(file_label: &str, bytes: &[u8], opt: usize) -> Result<(), ToolError> {
    let subsys_maj = u16::from_le_bytes([bytes[opt + 48], bytes[opt + 49]]);
    let subsys_min = u16::from_le_bytes([bytes[opt + 50], bytes[opt + 51]]);
    let subsys = u16::from_le_bytes([bytes[opt + 68], bytes[opt + 69]]);
    let dll_chars = u16::from_le_bytes([bytes[opt + 70], bytes[opt + 71]]);
    let rsrc_rva = u32::from_le_bytes([bytes[opt + 128], bytes[opt + 129], bytes[opt + 130], bytes[opt + 131]]);
    let pdata_rva = u32::from_le_bytes([bytes[opt + 136], bytes[opt + 137], bytes[opt + 138], bytes[opt + 139]]);
    let lcfg_rva = u32::from_le_bytes([bytes[opt + 192], bytes[opt + 193], bytes[opt + 194], bytes[opt + 195]]);
    if subsys != 3 || subsys_maj != 5 || subsys_min != 2 {
        return Err(ToolError::AbiLint {
            span: SourceSpan::new(file_label, 1, 1),
            proc_name: "<pe64>".into(),
            message: format!(
                "x86-64 PE32+ must target CUI (3) Subsystem 5.02 for XP Pro x64..11 (got subsys={subsys}, ver={subsys_maj}.{subsys_min:02})"
            ),
        });
    }
    if (dll_chars & 0x0140) != 0x0140 {
        return Err(ToolError::AbiLint {
            span: SourceSpan::new(file_label, 1, 1),
            proc_name: "<pe64>".into(),
            message: format!(
                "x86-64 PE32+ must enable DYNAMIC_BASE (0x0040) and NX_COMPAT (0x0100) DllCharacteristics (got 0x{dll_chars:04X})"
            ),
        });
    }
    if pdata_rva == 0 || rsrc_rva == 0 || lcfg_rva == 0 {
        return Err(ToolError::AbiLint {
            span: SourceSpan::new(file_label, 1, 1),
            proc_name: "<pe64>".into(),
            message: format!(
                "x86-64 PE32+ missing mandatory directories (pdata=0x{pdata_rva:X}, rsrc=0x{rsrc_rva:X}, load_config=0x{lcfg_rva:X})"
            ),
        });
    }
    Ok(())
}

fn check_ne_purity(file_label: &str, isa: CpuIsa, bytes: &[u8]) -> Result<(), ToolError> {
    if bytes.len() < 0x40 || &bytes[0..2] != b"MZ" {
        return Ok(());
    }
    let lfanew = usize::try_from(u32::from_le_bytes([
        bytes[0x3C],
        bytes[0x3D],
        bytes[0x3E],
        bytes[0x3F],
    ]))
    .unwrap_or(usize::MAX);
    if lfanew.saturating_add(0x40) > bytes.len() || &bytes[lfanew..lfanew + 2] != b"NE" {
        return Ok(());
    }
    let ne_flags = u16::from_le_bytes([bytes[lfanew + 0x0C], bytes[lfanew + 0x0D]]);
    let ne_heap = u16::from_le_bytes([bytes[lfanew + 0x10], bytes[lfanew + 0x11]]);
    let ne_stack = u16::from_le_bytes([bytes[lfanew + 0x12], bytes[lfanew + 0x13]]);
    let ne_cseg = usize::from(u16::from_le_bytes([
        bytes[lfanew + 0x1C],
        bytes[lfanew + 0x1D],
    ]));
    let ne_segtab = usize::from(u16::from_le_bytes([
        bytes[lfanew + 0x22],
        bytes[lfanew + 0x23],
    ]));
    let ne_flagsothers = bytes[lfanew + 0x37];
    let ne_expver = u16::from_le_bytes([bytes[lfanew + 0x3E], bytes[lfanew + 0x3F]]);

    if ne_cseg < 2 || ne_heap < 0x1000 || ne_stack < 0x1000 {
        return Err(ToolError::AbiLint {
            span: SourceSpan::new(file_label, 1, 1),
            proc_name: "<ne16>".into(),
            message: "Win16 NE binary must define code + DGROUP segments with valid heap/stack"
                .into(),
        });
    }
    match isa {
        CpuIsa::X86_8086 => {
            if (ne_flags & 0x0008) != 0 || (ne_flagsothers & 0x02) != 0 {
                return Err(ToolError::OpcodePurity {
                    span: SourceSpan::new(file_label, 1, 1),
                    isa,
                    message: "8086 universal NE binary must not set 286/PROTMODE-only header flags"
                        .into(),
                });
            }
        }
        CpuIsa::X86_286 => {
            if (ne_flags & 0x0008) == 0 || (ne_flagsothers & 0x02) == 0 || ne_expver < 0x0300 {
                return Err(ToolError::OpcodePurity {
                    span: SourceSpan::new(file_label, 1, 1),
                    isa,
                    message:
                        "80286 Protected-Mode NE binary must set PROTMODE (0x0008), 286 CPU flag (0x02), and expver >= 3.00"
                            .into(),
                });
            }
            let seg_base = lfanew.saturating_add(ne_segtab);
            let mut has_discardable = false;
            for idx in 0..ne_cseg {
                let off = seg_base + idx * 8;
                if off + 8 <= bytes.len() {
                    let sflags = u16::from_le_bytes([bytes[off + 4], bytes[off + 5]]);
                    if (sflags & 0x1001) == 0x1000 {
                        has_discardable = true;
                    }
                }
            }
            if !has_discardable && ne_cseg >= 3 {
                return Err(ToolError::AbiLint {
                    span: SourceSpan::new(file_label, 1, 1),
                    proc_name: "<ne286>".into(),
                    message: "multi-code-segment WINCMD286.EXE must mark transient segment DISCARDABLE"
                        .into(),
                });
            }
        }
        _ => {
            return Err(ToolError::OpcodePurity {
                span: SourceSpan::new(file_label, 1, 1),
                isa,
                message: format!("Win16 NE binary incompatible with ISA `{isa}`"),
            });
        }
    }
    Ok(())
}

/// Verifies compiled binary header, CRT-free purity, `.pdata` presence, `ARMv4` instruction
/// restrictions, and `ARM64` `X18` (`TEB`) register immutability.
///
/// # Errors
///
/// Returns [`ToolError::OpcodePurity`] or [`ToolError::AbiLint`] on any violation.
pub fn verify_binary_purity(
    file_label: &str,
    isa: CpuIsa,
    bytes: &[u8],
) -> Result<(), ToolError> {
    check_pe_purity(file_label, isa, bytes)?;
    check_ne_purity(file_label, isa, bytes)?;
    let code = extract_code_payload(bytes);
    if isa == CpuIsa::ArmV4 {
        for (idx, chunk) in code.as_chunks::<4>().0.iter().enumerate() {
            let insn = u32::from_le_bytes(*chunk);
            let is_blx = (insn & 0xFE00_0000) == 0xFA00_0000 || (insn & 0x0FF0_00F0) == 0x0120_0030;
            let is_clz = (insn & 0x0FFF_0FF0) == 0x016F_0F10;
            let is_movwt = (insn & 0x0FB0_0000) == 0x0300_0000;
            let is_div = (insn & 0x0FD0_F0F0) == 0x0710_F010;
            if is_blx || is_clz || is_movwt || is_div {
                return Err(ToolError::OpcodePurity {
                    span: SourceSpan::new(file_label, idx * 4, 1),
                    isa,
                    message: format!("ARMv5+/ARMv7 opcode 0x{insn:08X} detected in ARMv4 binary"),
                });
            }
        }
    } else if isa == CpuIsa::Arm64 {
        for (idx, chunk) in code.as_chunks::<4>().0.iter().enumerate() {
            let insn = u32::from_le_bytes(*chunk);
            let rd = insn & 0x1F;
            let op0 = (insn >> 25) & 0x0F;
            let is_dp_imm_or_reg = matches!(op0, 0b1000 | 0b1001 | 0b0101 | 0b1101);
            if rd == 18 && is_dp_imm_or_reg {
                return Err(ToolError::AbiLint {
                    span: SourceSpan::new(file_label, idx * 4, 1),
                    proc_name: "<arm64_text>".into(),
                    message: format!(
                        "compiled ARM64 instruction 0x{insn:08X} writes reserved X18/W18 (`TEB`)"
                    ),
                });
            }
        }
    }
    Ok(())
}

/// Enforces hard Phase 19 byte budgets on assembled artifacts or resident sections.
///
/// # Errors
///
/// Returns [`ToolError::SizeBudget`] if `actual_bytes > limit_bytes`.
pub fn verify_size_budget(
    artifact: &str,
    actual_bytes: usize,
    limit_bytes: usize,
) -> Result<(), ToolError> {
    if actual_bytes > limit_bytes {
        return Err(ToolError::SizeBudget {
            artifact: artifact.to_owned(),
            actual_bytes,
            limit_bytes,
        });
    }
    Ok(())
}

/// Recursively lints all canonical `.asm` and `.inc` files under `dir` (skipping `tc/`).
///
/// Returns the number of verified canonical modules.
///
/// # Errors
///
/// Returns [`ToolError`] if any file fails parsing, 100% doc coverage, or static CFG/ABI/ISA gates.
pub fn lint_directory(dir: &Path) -> Result<usize, ToolError> {
    let mut files = Vec::new();
    collect_files(dir, dir, &mut files)?;
    files.sort();

    let mut modules = Vec::with_capacity(files.len());
    for f in &files {
        let rel = f
            .strip_prefix(dir)
            .map_or_else(|_| f.to_string_lossy().into_owned(), |p| p.to_string_lossy().into_owned())
            .replace(92 as char, "/");
        let text = fs::read_to_string(f)?;
        let parsed = parse_canonical(&rel, &text)?;
        if parsed.format != BinaryFormat::Include && parsed.cpu.is_none() {
            return Err(ToolError::Syntax {
                span: SourceSpan::new(rel, 1, 1),
                message: "non-include module must specify `@cpu`".into(),
            });
        }
        modules.push(parsed);
    }

    for m in &modules {
        let scoped: Vec<CanonicalModule> = modules
            .iter()
            .filter(|other| {
                other.cpu.is_none()
                    || m.cpu.is_none()
                    || other.cpu == m.cpu
                    || (m.cpu == Some(CpuIsa::X86_286)
                        && other.format == BinaryFormat::Include
                        && other.cpu == Some(CpuIsa::X86_8086))
                    || (m.cpu == Some(CpuIsa::ArmV7)
                        && other.format == BinaryFormat::Include
                        && other.cpu == Some(CpuIsa::ArmV4))
            })
            .cloned()
            .collect();
        let clobbers = collect_module_clobbers(&scoped);
        lint_module(m, &clobbers)?;
    }

    Ok(modules.len())
}

fn collect_files(root: &Path, cur: &Path, out: &mut Vec<PathBuf>) -> Result<(), ToolError> {
    for entry_res in fs::read_dir(cur)? {
        let entry = entry_res?;
        let p = entry.path();
        if p.is_dir() {
            if p.strip_prefix(root).ok() == Some(Path::new("tc")) {
                continue;
            }
            collect_files(root, &p, out)?;
        } else if let Some(ext) = p.extension().and_then(|e| e.to_str())
            && (ext.eq_ignore_ascii_case("asm") || ext.eq_ignore_ascii_case("inc"))
        {
            out.push(p);
        }
    }
    Ok(())
}

/// Executes the `asm-lint` CLI:
/// - `asm-lint --dir <asm/>`
/// - `asm-lint --equiv <artifact_name> <file1> <file2> [<file3>...]`
/// - `asm-lint --budget <file> <max_bytes>`
/// - `asm-lint --purity <isa> <file>`
///
/// # Errors
///
/// Returns [`ToolError`] on CLI usage errors or failed verification checks.
pub fn run_cli<I, S, W>(args: I, out: &mut W) -> Result<(), ToolError>
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
    W: Write,
{
    let cli_items: Vec<String> = args.into_iter().map(|s| s.as_ref().to_owned()).collect();
    if cli_items.len() < 3 {
        return Err(ToolError::CliUsage {
            message: "usage: asm-lint --dir <dir> | --equiv <name> <f1> <f2>... | --budget <file> <max> | --purity <isa> <file>".into(),
        });
    }

    match cli_items[1].as_str() {
        "--dir" => {
            let dir = PathBuf::from(&cli_items[2]);
            let count = lint_directory(&dir)?;
            writeln!(
                out,
                "asm-lint: {count} modules passed 100% doc, CFG, stack, ABI, and ISA purity gates"
            )?;
            Ok(())
        }
        "--equiv" => {
            if cli_items.len() < 5 {
                return Err(ToolError::CliUsage {
                    message: "--equiv requires `<artifact> <file1> <file2> [...]`".into(),
                });
            }
            let artifact = &cli_items[2];
            let mut loaded: Vec<(String, Vec<u8>)> = Vec::new();
            for path_str in &cli_items[3..] {
                let data = fs::read(path_str)?;
                loaded.push((path_str.clone(), data));
            }
            let refs: Vec<(&str, &[u8])> = loaded
                .iter()
                .map(|(n, b)| (n.as_str(), b.as_slice()))
                .collect();
            verify_binary_equivalence(artifact, &refs)?;
            writeln!(
                out,
                "asm-lint: `{artifact}` instruction-equivalent across {} assembler builds ({} code bytes)",
                refs.len(),
                extract_code_payload(refs[0].1).len()
            )?;
            Ok(())
        }
        "--budget" => {
            if cli_items.len() != 4 {
                return Err(ToolError::CliUsage {
                    message: "--budget requires `<file> <max_bytes>`".into(),
                });
            }
            let data = fs::read(&cli_items[2])?;
            let limit: usize = cli_items[3].parse()?;
            verify_size_budget(&cli_items[2], data.len(), limit)?;
            writeln!(
                out,
                "asm-lint: `{}` size {} bytes <= {} byte budget",
                cli_items[2],
                data.len(),
                limit
            )?;
            Ok(())
        }
        "--purity" => {
            if cli_items.len() != 4 {
                return Err(ToolError::CliUsage {
                    message: "--purity requires `<isa> <file>`".into(),
                });
            }
            let isa = CpuIsa::parse(&cli_items[2]).ok_or_else(|| ToolError::CliUsage {
                message: format!("unknown ISA `{}`", cli_items[2]),
            })?;
            let data = fs::read(&cli_items[3])?;
            verify_binary_purity(&cli_items[3], isa, &data)?;
            writeln!(
                out,
                "asm-lint: `{}` passed compiled `{isa}` binary purity & ABI gate",
                cli_items[3]
            )?;
            Ok(())
        }
        other => Err(ToolError::CliUsage {
            message: format!("unrecognized mode `{other}`"),
        }),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn check_src(src: &str) -> Result<(), ToolError> {
        let m = parse_canonical("test.asm", src)?;
        let clobbers = collect_module_clobbers(core::slice::from_ref(&m));
        lint_module(&m, &clobbers)
    }

    #[test]
    fn valid_dos16_module_passes_all_gates() -> Result<(), ToolError> {
        let dos_ok = r"
;; @file math.asm
;; @isa 8086
;; @desc 8086 test module.
@cpu 8086
@format com

;; @const MAGIC
;; @desc Magic value.
@const MAGIC = 0x55AA

;; @struc HDR
;; @desc 2-byte header.
@struc HDR, 2
    @field wVal, word, 1
@endstruc

;; @proc helper
;; @abi dos16
;; @in AX
;; @out AX, DX
;; @err none
;; @clobbers ax, cx, dx, si, di
;; @desc Helper exercising 8086 instructions.
@proc helper
    push bp
    mov bp, sp
    sub sp, 4
    mul cx
    cwd
    rep movsb
    rep stosb
    lodsb
    xchg ax, cx
    int 0x25
    pop cx
.loop_lbl:
    loop .loop_lbl
    jz .done
    jmp .done
.done:
    add sp, 4
    pop bp
    ret
@endproc

;; @proc caller_proc
;; @abi dos16
;; @in none
;; @out AX
;; @err none
;; @clobbers ax, cx, dx, si, di
;; @desc Calls helper transitively.
@proc caller_proc
    call helper
    ret
@endproc
";
        check_src(dos_ok)
    }

    #[test]
    fn valid_ia32_and_x64_modules_pass_all_gates() -> Result<(), ToolError> {
        let x64_ok = r"
;; @file w64.asm
;; @isa x64
;; @desc Win64 framed module.
@cpu x64
@format coff64
@extern __imp_GetVersion, qword
;; @proc w64_fn
;; @abi win64_ms
;; @in none
;; @out EAX
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11, r12
;; @desc Calls Win64 IAT and saves rbx/r12.
@proc_frame w64_fn, 40, rbx, r12
    mov rbx, 1
    mov r12d, 2
    call qword [rel __imp_GetVersion]
    @epilog_ret
@endproc
";
        check_src(x64_ok)?;

        let ia32_ok = r"
;; @file w32.asm
;; @isa 386
;; @desc IA-32 module.
@cpu 386
@format coff32
@extern __imp_GetVersion, dword
;; @proc w32_fn
;; @abi win32
;; @in none
;; @out EAX
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Calls Win32 IAT and saves ebx/esi/edi/ebp.
@proc w32_fn
    push ebp
    push ebx
    push esi
    push edi
    mov ebp, esp
    mov ebx, 1
    mov esi, 2
    mov edi, 3
    call dword [__imp_GetVersion]
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
@endproc
";
        check_src(ia32_ok)
    }

    #[test]
    fn valid_arm_win16_and_include_modules_pass_all_gates() -> Result<(), ToolError> {
        let armv7_ok = r"
;; @file v7.asm
;; @isa armv7
;; @desc ARMv7 Thumb-2 module.
@cpu armv7
@format armv7_pe
;; @proc leaf_v7
;; @abi aapcs_vfp
;; @in r0
;; @out r0
;; @err none
;; @clobbers r0, r12, lr
;; @desc ARMv7 push/pop and bl test.
@proc leaf_v7
    push {r4, lr}
    mov r4, r0
    mov ip, #1
    pop {r4, lr}
    bx lr
@endproc
;; @proc call_v7
;; @abi aapcs_vfp
;; @in r0
;; @out r0
;; @err none
;; @clobbers r0, r12, lr
;; @desc Calls leaf_v7.
@proc call_v7
    push {r11, lr}
    mov fp, sp
    bl leaf_v7
    pop {r11, lr}
    mov pc, lr
@endproc
";
        check_src(armv7_ok)?;

        let aarch64_src = r"
;; @file a64.asm
;; @isa arm64
;; @desc ARM64 module.
@cpu arm64
@format arm64_pe
;; @proc a64_fn
;; @abi win_arm64
;; @in x0
;; @out x0
;; @err none
;; @clobbers x0, x1
;; @desc ARM64 stp/ldp stack frame.
@proc a64_fn
    stp fp, lr, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    cmp x18, #0
    cbz x19, .skip
    mov w1, #2
.skip:
    ldp x19, x20, [sp], #16
    ldp fp, lr, [sp], #16
    ret
@endproc
";
        check_src(aarch64_src)?;

        let win16_ok = r"
;; @file w16.asm
;; @isa 286
;; @desc Win16 selector-clean module.
@cpu 286
@format bin16
;; @proc w16_fn
;; @abi win16
;; @in AX=selector
;; @out AX
;; @err none
;; @clobbers ax
;; @desc Preserves selector discipline.
@proc w16_fn
    push ds
    pusha
    mov ds, ax
    add ax, 4
    mov ax, cs
    mov ds, ax
    popa
    pop ds
    ret
@endproc
";
        check_src(win16_ok)?;

        let inc_ok = ";; @file c.inc\n;; @isa all\n;; @desc Inc.\n;; @const X\n;; @desc X\n@const X = 1\n";
        check_src(inc_ok)
    }

    #[test]
    fn catches_doc_and_cfg_violations() {
        let failures = [
            ";; @file f.asm\n;; @isa 8086\n@cpu 8086\n@format com\n",
            ";; @file f.asm\n;; @isa all\n;; @desc d\n;; @const A\n@const A = 1\n",
            ";; @file f.asm\n;; @isa all\n;; @desc d\n;; @struc S\n@struc S, 1\n@field b, byte, 1\n@endstruc\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc wrong\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers none\n;; @desc d\n@proc right\nret\n@endproc\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc p\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers ax\n;; @desc d\n@proc p\nmov ax, 1\n@endproc\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc p\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers ax\n;; @desc d\n@proc p\npush ax\nret\n@endproc\n",
            ";; @file f.asm\n;; @isa x64\n;; @desc d\n@cpu x64\n@format coff64\n;; @proc p\n;; @abi w\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers rax\n;; @desc d\n@proc_frame p, 32\npush rax\n@epilog_ret\n@endproc\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc p\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers ax\n;; @desc d\n@proc p\njz .m\npush ax\n.m:\nret\n@endproc\n",
        ];
        for bad in failures {
            assert!(check_src(bad).is_err(), "expected failure for:\n{bad}");
        }
    }

    #[test]
    fn catches_abi_selector_x18_and_opcode_violations() {
        let failures = [
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc p\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers bp\n;; @desc d\n@proc p\nmov bp, 0\nret\n@endproc\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc p\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers none\n;; @desc d\n@proc p\nmov ax, 1\nret\n@endproc\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\npusha\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\npop ss\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\npush 4\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\nshl ax, 4\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\nmov ax, [fs:0]\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc p\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers ax\n;; @desc d\n@proc p\nint 0x25\nmov ax, 0\nret\n@endproc\n",
            ";; @file f.asm\n;; @isa 8086\n;; @desc d\n@cpu 8086\n@format com\n;; @proc p\n;; @abi d\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers none\n;; @desc d\n@proc p\nint 0x25\n@endproc\n",
            ";; @file f.asm\n;; @isa 286\n;; @desc d\n@cpu 286\n@format bin16\n;; @proc p\n;; @abi win16\n;; @in n\n;; @out n\n;; @err n\n;; @clobbers ax\n;; @desc d\n@proc p\npush es\nadd ax, 0x1000\nmov es, ax\npop es\nret\n@endproc\n",
            ";; @file f.asm\n;; @isa 386\n;; @desc d\n@cpu 386\n@format coff32\nbswap eax\n",
            ";; @file f.asm\n;; @isa armv4\n;; @desc d\n@cpu armv4\n@format armv4_pe\nclz r0, r1\n",
            ";; @file f.asm\n;; @isa arm64\n;; @desc d\n@cpu arm64\n@format arm64_pe\nmov x18, #0\n",
        ];
        for bad in failures {
            assert!(check_src(bad).is_err(), "expected failure for:\n{bad}");
        }
    }

    #[test]
    fn equivalence_budget_and_cli_paths() -> Result<(), ToolError> {
        verify_binary_equivalence("empty", &[])?;
        verify_binary_equivalence("ok", &[("nasm", &[1, 2, 3]), ("fasm", &[1, 2, 3])])?;
        assert!(
            verify_binary_equivalence("diff_byte", &[("nasm", &[1, 2]), ("fasm", &[1, 9])]).is_err()
        );
        assert!(
            verify_binary_equivalence("diff_len", &[("nasm", &[1, 2]), ("fasm", &[1, 2, 3])])
                .is_err()
        );
        verify_size_budget("WINCMD.COM", 2048, 2816)?;
        assert!(verify_size_budget("WINCMD.COM", 3000, 2816).is_err());

        let tmp = std::env::temp_dir().join("wincmd_asm_lint_unit_test");
        let _ = fs::remove_dir_all(&tmp);
        fs::create_dir_all(tmp.join("tc"))?;
        fs::create_dir_all(tmp.join("dos16"))?;
        fs::write(tmp.join("tc/raw.inc"), "RAW_NATIVE")?;
        fs::write(tmp.join("dos16/note.txt"), "skip")?;
        fs::write(
            tmp.join("dos16/ok.asm"),
            ";; @file ok.asm
;; @isa 8086
;; @desc ok
@cpu 8086
@format com
;; @proc p
;; @abi d
;; @in n
;; @out n
;; @err n
;; @clobbers none
;; @desc p
@proc p
@data db tbl, 1
ret
@endproc
",
        )?;

        let mut out = Vec::new();
        run_cli(["asm-lint", "--dir", &tmp.to_string_lossy()], &mut out)?;

        let b1 = tmp.join("b1.bin");
        let b2 = tmp.join("b2.bin");
        fs::write(&b1, [0xEB_u8, 0xFE])?;
        fs::write(&b2, [0xEB_u8, 0xFE])?;
        run_cli(
            [
                "asm-lint",
                "--equiv",
                "TEST.COM",
                &b1.to_string_lossy(),
                &b2.to_string_lossy(),
            ],
            &mut out,
        )?;
        run_cli(
            ["asm-lint", "--budget", &b1.to_string_lossy(), "16"],
            &mut out,
        )?;

        // Error CLI modes
        assert!(run_cli(["asm-lint"], &mut out).is_err());
        assert!(run_cli(["asm-lint", "--equiv", "A", "only_one"], &mut out).is_err());
        assert!(run_cli(["asm-lint", "--budget", "only_one"], &mut out).is_err());
        assert!(run_cli(["asm-lint", "--unknown", "x"], &mut out).is_err());

        // Non-include without @cpu error in lint_directory
        fs::write(
            tmp.join("dos16/nocpu.asm"),
            ";; @file n.asm
;; @isa 8086
;; @desc n
@format com
",
        )?;
        assert!(lint_directory(&tmp).is_err());

        let _ = fs::remove_dir_all(&tmp);
        Ok(())
    }

    #[test]
    fn normalized_equiv_and_binary_purity_gates() -> Result<(), ToolError> {
        // 8086 `mov ax, bx` in NASM/FASM (89 D8) vs JWasm (8B C3) + `test ax, bx` (85 D8 vs 85 C3)
        verify_binary_equivalence(
            "MOV_MOD11",
            &[("nasm", &[0x89, 0xD8, 0x85, 0xD8]), ("jwasm", &[0x8B, 0xC3, 0x85, 0xC3])],
        )?;
        // x64 `mov rbx, r8` (4C 89 C3) vs (49 8B D8)
        verify_binary_equivalence(
            "X64_REX_MOD11",
            &[("nasm", &[0x4C, 0x89, 0xC3]), ("jwasm", &[0x49, 0x8B, 0xD8])],
        )?;

        // Synthetic minimal PE32+ image with .text and .pdata check
        let mut pe = vec![0_u8; 512];
        pe[0..2].copy_from_slice(b"MZ");
        pe[0x3C..0x40].copy_from_slice(&0x80_u32.to_le_bytes());
        pe[0x80..0x84].copy_from_slice(b"PE\0\0");
        pe[0x84..0x86].copy_from_slice(&0x8664_u16.to_le_bytes()); // AMD64
        pe[0x86..0x88].copy_from_slice(&1_u16.to_le_bytes()); // 1 section
        pe[0x94..0x96].copy_from_slice(&240_u16.to_le_bytes()); // OptHdr64 size
        let sec_off = 0x80 + 24 + 240;
        pe[sec_off..sec_off + 5].copy_from_slice(b".text");
        pe[sec_off + 8..sec_off + 12].copy_from_slice(&4_u32.to_le_bytes());
        pe[sec_off + 16..sec_off + 20].copy_from_slice(&4_u32.to_le_bytes());
        pe[sec_off + 20..sec_off + 24].copy_from_slice(&400_u32.to_le_bytes());
        pe[400..404].copy_from_slice(&[0x48, 0x31, 0xC0, 0xC3]);

        // Missing Subsystem 5.02 / DYNAMIC_BASE|NX_COMPAT / .pdata / .rsrc / LOAD_CONFIG on x64 fails
        assert!(verify_binary_purity("bad64.exe", CpuIsa::X86_64, &pe).is_err());
        pe[0x80 + 24 + 48..0x80 + 24 + 50].copy_from_slice(&5_u16.to_le_bytes());
        pe[0x80 + 24 + 50..0x80 + 24 + 52].copy_from_slice(&2_u16.to_le_bytes());
        pe[0x80 + 24 + 68..0x80 + 24 + 70].copy_from_slice(&3_u16.to_le_bytes());
        assert!(verify_binary_purity("bad64_nx.exe", CpuIsa::X86_64, &pe).is_err());
        pe[0x80 + 24 + 70..0x80 + 24 + 72].copy_from_slice(&0x0140_u16.to_le_bytes());
        assert!(verify_binary_purity("bad64_dirs.exe", CpuIsa::X86_64, &pe).is_err());
        pe[0x80 + 24 + 128..0x80 + 24 + 132].copy_from_slice(&0x3000_u32.to_le_bytes());
        pe[0x80 + 24 + 136..0x80 + 24 + 140].copy_from_slice(&0x2000_u32.to_le_bytes());
        pe[0x80 + 24 + 192..0x80 + 24 + 196].copy_from_slice(&0x4000_u32.to_le_bytes());
        verify_binary_purity("ok64.exe", CpuIsa::X86_64, &pe)?;
        // Wrong ISA Machine fails
        assert!(verify_binary_purity("wrong.exe", CpuIsa::Ia32, &pe).is_err());

        // Synthetic minimal IA-32 PE32 image verifying Subsystem 3.10, DYNAMIC_BASE|NX_COMPAT, and /SAFESEH LoadConfig
        let mut pe32 = vec![0_u8; 512];
        pe32[0..2].copy_from_slice(b"MZ");
        pe32[0x3C..0x40].copy_from_slice(&0x80_u32.to_le_bytes());
        pe32[0x80..0x84].copy_from_slice(b"PE\0\0");
        pe32[0x84..0x86].copy_from_slice(&0x014C_u16.to_le_bytes()); // I386
        pe32[0x86..0x88].copy_from_slice(&1_u16.to_le_bytes());
        pe32[0x94..0x96].copy_from_slice(&224_u16.to_le_bytes()); // OptHdr32 size
        let sec32 = 0x80 + 24 + 224;
        pe32[sec32..sec32 + 5].copy_from_slice(b".text");
        pe32[sec32 + 8..sec32 + 12].copy_from_slice(&4_u32.to_le_bytes());
        pe32[sec32 + 16..sec32 + 20].copy_from_slice(&4_u32.to_le_bytes());
        pe32[sec32 + 20..sec32 + 24].copy_from_slice(&400_u32.to_le_bytes());
        pe32[400..404].copy_from_slice(&[0x31, 0xC0, 0xC3, 0x90]);
        assert!(verify_binary_purity("bad_subsys32.exe", CpuIsa::Ia32, &pe32).is_err());
        pe32[0x80 + 24 + 48..0x80 + 24 + 50].copy_from_slice(&3_u16.to_le_bytes());
        pe32[0x80 + 24 + 50..0x80 + 24 + 52].copy_from_slice(&10_u16.to_le_bytes());
        pe32[0x80 + 24 + 68..0x80 + 24 + 70].copy_from_slice(&3_u16.to_le_bytes());
        assert!(verify_binary_purity("bad_dllchars32.exe", CpuIsa::Ia32, &pe32).is_err());
        pe32[0x80 + 24 + 70..0x80 + 24 + 72].copy_from_slice(&0x0140_u16.to_le_bytes());
        assert!(verify_binary_purity("bad_safeseh32.exe", CpuIsa::Ia32, &pe32).is_err());
        pe32[0x80 + 24 + 176..0x80 + 24 + 180].copy_from_slice(&0x2000_u32.to_le_bytes());
        verify_binary_purity("ok32.exe", CpuIsa::Ia32, &pe32)?;

        // Forbidden CRT import fails
        let mut crt_bin = pe.clone();
        crt_bin[420..430].copy_from_slice(b"MSVCRT.DLL");
        assert!(verify_binary_purity("crt.exe", CpuIsa::X86_64, &crt_bin).is_err());

        // ARMv4 CLZ (0xE16F0F11) rejected; MOV R0, #0 (0xE3A00000) accepted
        verify_binary_purity("v4_ok.bin", CpuIsa::ArmV4, &0xE3A0_0000_u32.to_le_bytes())?;
        assert!(
            verify_binary_purity("v4_clz.bin", CpuIsa::ArmV4, &0xE16F_0F11_u32.to_le_bytes())
                .is_err()
        );

        // ARM64 MOVZ X18, #0 (0xD2800012) rejected; MOVZ X0, #0 (0xD2800000) accepted
        verify_binary_purity("a64_ok.bin", CpuIsa::Arm64, &0xD280_0000_u32.to_le_bytes())?;
        assert!(
            verify_binary_purity("a64_x18.bin", CpuIsa::Arm64, &0xD280_0012_u32.to_le_bytes())
                .is_err()
        );

        let tmp = std::env::temp_dir().join("wincmd_purity_cli_test.bin");
        fs::write(&tmp, 0xD280_0000_u32.to_le_bytes())?;
        let mut out = Vec::new();
        run_cli(
            ["asm-lint", "--purity", "arm64", &tmp.to_string_lossy()],
            &mut out,
        )?;
        assert!(run_cli(["asm-lint", "--purity", "only_one"], &mut out).is_err());
        assert!(run_cli(["asm-lint", "--purity", "bad_isa", &tmp.to_string_lossy()], &mut out).is_err());
        let _ = fs::remove_file(&tmp);
        Ok(())
    }
}
