//! Parser for the canonical multi-ISA assembly source format.

use crate::ast::{
    BinaryFormat, CanonicalModule, DataWidth, DocBlock, ProcKind, SectionKind, Stmt, StrucField,
    TopItem,
};
use wincmd_diag::{CpuIsa, SourceSpan, ToolError};

struct ModuleBuildState {
    cpu: Option<CpuIsa>,
    format: BinaryFormat,
    entry: Option<String>,
    safeseh: Option<String>,
    pending_doc: DocBlock,
    items: Vec<TopItem>,
}

/// Parses canonical assembly source text into a [`CanonicalModule`].
///
/// # Errors
///
/// Returns [`ToolError::Syntax`] if directives, `@struc` blocks, `@proc` blocks,
/// or structure byte-size assertions are malformed.
pub fn parse_canonical(file_path: &str, source: &str) -> Result<CanonicalModule, ToolError> {
    let mut file_doc = DocBlock::default();
    let mut seen_non_doc = false;
    let mut st = ModuleBuildState {
        cpu: None,
        format: BinaryFormat::Include,
        entry: None,
        safeseh: None,
        pending_doc: DocBlock::default(),
        items: Vec::new(),
    };

    let lines: Vec<&str> = source.lines().collect();
    let mut idx = 0_usize;

    while idx < lines.len() {
        let line_no = idx + 1;
        let trimmed = lines[idx].trim();

        if trimmed.is_empty() {
            if !seen_non_doc && !st.pending_doc.is_empty() && file_doc.is_empty() {
                file_doc = core::mem::take(&mut st.pending_doc);
                seen_non_doc = true;
            }
            idx += 1;
            continue;
        }

        if let Some(doc_rest) = trimmed.strip_prefix(";;") {
            let doc_trimmed = doc_rest.trim();
            if !seen_non_doc
                && file_doc.is_empty()
                && st.pending_doc.get("file").is_some()
                && (doc_trimmed.starts_with("@const")
                    || doc_trimmed.starts_with("@struc")
                    || doc_trimmed.starts_with("@proc"))
            {
                file_doc = core::mem::take(&mut st.pending_doc);
                seen_non_doc = true;
            }
            parse_doc_line(doc_trimmed, &mut st.pending_doc);
            idx += 1;
            continue;
        }

        if !seen_non_doc {
            file_doc = core::mem::take(&mut st.pending_doc);
            seen_non_doc = true;
        }

        let span = SourceSpan::new(file_path, line_no, 1);

        if let Some(comment_text) = trimmed.strip_prefix(';') {
            st.items.push(TopItem::Stmt(Stmt::Comment {
                text: comment_text.trim().to_owned(),
                span,
            }));
            idx += 1;
            continue;
        }

        let clean = strip_inline_comment(trimmed);
        if let Some(rest) = clean.strip_prefix('@') {
            idx = parse_top_directive(file_path, &lines, idx, rest, span, &mut st)?;
        } else {
            st.items
                .push(TopItem::Stmt(parse_body_line(&clean, span, false)?));
            idx += 1;
        }
    }

    if !seen_non_doc && !st.pending_doc.is_empty() {
        file_doc = st.pending_doc;
    }

    Ok(CanonicalModule {
        file_path: file_path.to_owned(),
        file_doc,
        cpu: st.cpu,
        format: st.format,
        entry: st.entry,
        safeseh: st.safeseh,
        items: st.items,
    })
}

fn parse_top_directive(
    file_path: &str,
    lines: &[&str],
    idx: usize,
    rest: &str,
    span: SourceSpan,
    st: &mut ModuleBuildState,
) -> Result<usize, ToolError> {
    let (dir, arg_str) = split_first_word(rest);
    match dir {
        "cpu" => {
            st.cpu = Some(CpuIsa::parse(arg_str).ok_or_else(|| ToolError::Syntax {
                span,
                message: format!("unknown @cpu architecture `{arg_str}`"),
            })?);
            Ok(idx + 1)
        }
        "format" => {
            st.format = BinaryFormat::parse(arg_str).ok_or_else(|| ToolError::Syntax {
                span,
                message: format!("unknown @format `{arg_str}`"),
            })?;
            Ok(idx + 1)
        }
        "entry" => {
            st.entry = Some(require_symbol_arg(arg_str, "@entry requires a symbol name", span)?);
            Ok(idx + 1)
        }
        "safeseh" => {
            st.safeseh = Some(require_symbol_arg(
                arg_str,
                "@safeseh requires a handler symbol name",
                span,
            )?);
            Ok(idx + 1)
        }
        "include" => {
            let path = unquote_path(arg_str).ok_or_else(|| ToolError::Syntax {
                span: span.clone(),
                message: "@include requires a quoted path".into(),
            })?;
            st.items.push(TopItem::Include { path, span });
            Ok(idx + 1)
        }
        "section" => {
            let kind = SectionKind::parse(arg_str).ok_or_else(|| ToolError::Syntax {
                span: span.clone(),
                message: format!("unknown @section `{arg_str}`"),
            })?;
            st.items.push(TopItem::Section { kind, span });
            Ok(idx + 1)
        }
        "global" => {
            let symbol = require_symbol_arg(arg_str, "@global requires a symbol name", span.clone())?;
            st.items.push(TopItem::Global { symbol, span });
            Ok(idx + 1)
        }
        "extern" => {
            let parts = split_operands(arg_str);
            if parts.len() != 2 {
                return Err(ToolError::Syntax {
                    span,
                    message: "@extern requires `<symbol>, <proc|dword|qword>`".into(),
                });
            }
            st.items.push(TopItem::Extern {
                symbol: parts[0].clone(),
                kind: parts[1].to_ascii_lowercase(),
                span,
            });
            Ok(idx + 1)
        }
        "const" => parse_const_directive(arg_str, span, st).map(|()| idx + 1),
        "struc" => {
            let doc = core::mem::take(&mut st.pending_doc);
            let (struc_item, next_idx) =
                parse_struc_block(file_path, lines, idx, arg_str, doc, span)?;
            st.items.push(struc_item);
            Ok(next_idx)
        }
        "proc" | "proc_far" | "proc_frame" => {
            let doc = core::mem::take(&mut st.pending_doc);
            let (proc_item, next_idx) =
                parse_proc_block(file_path, lines, idx, dir, arg_str, doc, span)?;
            st.items.push(proc_item);
            Ok(next_idx)
        }
        _ => {
            st.items
                .push(TopItem::Stmt(parse_directive_stmt(dir, arg_str, span)?));
            Ok(idx + 1)
        }
    }
}

fn require_symbol_arg(arg_str: &str, msg: &str, span: SourceSpan) -> Result<String, ToolError> {
    if arg_str.is_empty() {
        return Err(ToolError::Syntax {
            span,
            message: msg.into(),
        });
    }
    Ok(arg_str.to_owned())
}

fn parse_const_directive(
    arg_str: &str,
    span: SourceSpan,
    st: &mut ModuleBuildState,
) -> Result<(), ToolError> {
    let Some((lhs, rhs)) = arg_str.split_once('=') else {
        return Err(ToolError::Syntax {
            span,
            message: "@const requires `NAME = EXPR`".into(),
        });
    };
    let name = lhs.trim();
    let expr = rhs.trim();
    if name.is_empty() || expr.is_empty() {
        return Err(ToolError::Syntax {
            span,
            message: "@const requires non-empty name and expression".into(),
        });
    }
    st.items.push(TopItem::Const {
        doc: core::mem::take(&mut st.pending_doc),
        name: name.to_owned(),
        expr: expr.to_owned(),
        span,
    });
    Ok(())
}

fn parse_doc_line(doc_rest: &str, block: &mut DocBlock) {
    if let Some(tagged) = doc_rest.strip_prefix('@') {
        let (tag, val) = split_first_word(tagged);
        block.tags.push((tag.to_ascii_lowercase(), val.to_owned()));
    } else if let Some((_, last_val)) = block.tags.last_mut()
        && !doc_rest.is_empty()
    {
        last_val.push(' ');
        last_val.push_str(doc_rest);
    }
}

fn parse_struc_block(
    file_path: &str,
    lines: &[&str],
    start_idx: usize,
    header_args: &str,
    doc: DocBlock,
    header_span: SourceSpan,
) -> Result<(TopItem, usize), ToolError> {
    let args = split_operands(header_args);
    if args.len() != 2 {
        return Err(ToolError::Syntax {
            span: header_span,
            message: "@struc requires `<NAME>, <EXPECTED_BYTE_SIZE>`".into(),
        });
    }
    let name = args[0].clone();
    let expected_size = parse_usize_literal(&args[1], &header_span)?;

    let mut fields = Vec::new();
    let mut computed_size = 0_usize;
    let mut idx = start_idx + 1;

    while idx < lines.len() {
        let span = SourceSpan::new(file_path, idx + 1, 1);
        let trimmed = lines[idx].trim();
        if trimmed.is_empty() || trimmed.starts_with(';') {
            idx += 1;
            continue;
        }
        let clean = strip_inline_comment(trimmed);
        if clean == "@endstruc" {
            if computed_size != expected_size {
                return Err(ToolError::Syntax {
                    span: header_span,
                    message: format!(
                        "STRUC `{name}` fields sum to {computed_size} bytes, expected {expected_size} bytes"
                    ),
                });
            }
            return Ok((
                TopItem::Struc {
                    doc,
                    name,
                    expected_size,
                    fields,
                    span: header_span,
                },
                idx + 1,
            ));
        }

        let Some(field_rest) = clean.strip_prefix("@field") else {
            return Err(ToolError::Syntax {
                span,
                message:
                    "expected `@field <name>, <byte|word|dword|qword>, <count>` or `@endstruc`"
                        .into(),
            });
        };
        let fargs = split_operands(field_rest.trim());
        if fargs.len() != 3 {
            return Err(ToolError::Syntax {
                span,
                message: "@field requires `<name>, <width>, <count>`".into(),
            });
        }
        let width = DataWidth::parse(&fargs[1]).ok_or_else(|| ToolError::Syntax {
            span: span.clone(),
            message: format!("invalid @field width `{}`", fargs[1]),
        })?;
        let count = parse_usize_literal(&fargs[2], &span)?;
        if count == 0 {
            return Err(ToolError::Syntax {
                span,
                message: "@field count must be >= 1".into(),
            });
        }
        computed_size += width.byte_size() * count;
        fields.push(StrucField {
            name: fargs[0].clone(),
            width,
            count,
            span,
        });
        idx += 1;
    }

    Err(ToolError::Syntax {
        span: header_span,
        message: format!("unclosed @struc `{name}` (missing @endstruc)"),
    })
}

fn parse_proc_block(
    file_path: &str,
    lines: &[&str],
    start_idx: usize,
    dir: &str,
    header_args: &str,
    doc: DocBlock,
    header_span: SourceSpan,
) -> Result<(TopItem, usize), ToolError> {
    let args = split_operands(header_args);
    if args.is_empty() || args[0].is_empty() {
        return Err(ToolError::Syntax {
            span: header_span,
            message: format!("@{dir} requires a procedure name"),
        });
    }
    let name = args[0].clone();
    let kind = match dir {
        "proc" => ProcKind::Near,
        "proc_far" => ProcKind::Far,
        _ => {
            if args.len() < 2 {
                return Err(ToolError::Syntax {
                    span: header_span,
                    message: "@proc_frame requires `<name>, <stack_alloc>[, <reg>...]`".into(),
                });
            }
            let alloc_usize = parse_usize_literal(&args[1], &header_span)?;
            let stack_alloc = u32::try_from(alloc_usize).map_err(|_| ToolError::Syntax {
                span: header_span.clone(),
                message: "stack_alloc exceeds u32".into(),
            })?;
            if stack_alloc % 8 != 0 {
                return Err(ToolError::Syntax {
                    span: header_span,
                    message: format!(
                        "@proc_frame `{name}` stack_alloc ({stack_alloc}) must be a multiple of 8"
                    ),
                });
            }
            let saved_regs = args[2..].iter().map(|s| s.to_ascii_lowercase()).collect();
            ProcKind::Win64Frame {
                stack_alloc,
                saved_regs,
            }
        }
    };

    let mut body = Vec::new();
    let mut idx = start_idx + 1;
    while idx < lines.len() {
        let span = SourceSpan::new(file_path, idx + 1, 1);
        let trimmed = lines[idx].trim();
        if trimmed.is_empty() {
            idx += 1;
            continue;
        }
        if let Some(comment_text) = trimmed.strip_prefix(';') {
            body.push(Stmt::Comment {
                text: comment_text.trim().to_owned(),
                span,
            });
            idx += 1;
            continue;
        }
        let clean = strip_inline_comment(trimmed);
        if clean == "@endproc" {
            return Ok((
                TopItem::Proc {
                    doc,
                    name,
                    kind,
                    body,
                    span: header_span,
                },
                idx + 1,
            ));
        }
        if let Some(rest) = clean.strip_prefix('@') {
            let (sdir, sargs) = split_first_word(rest);
            body.push(parse_directive_stmt(sdir, sargs, span)?);
        } else {
            body.push(parse_body_line(&clean, span, true)?);
        }
        idx += 1;
    }

    Err(ToolError::Syntax {
        span: header_span,
        message: format!("unclosed @{dir} `{name}` (missing @endproc)"),
    })
}

fn parse_directive_stmt(dir: &str, arg_str: &str, span: SourceSpan) -> Result<Stmt, ToolError> {
    match dir {
        "align16_zero" => Ok(Stmt::AlignZero {
            align_bytes: 16,
            span,
        }),
        "align" => {
            let n = parse_usize_literal(arg_str, &span)?;
            if !n.is_power_of_two() {
                return Err(ToolError::Syntax {
                    span,
                    message: format!("@align value {n} must be a power of two"),
                });
            }
            Ok(Stmt::AlignZero {
                align_bytes: n,
                span,
            })
        }
        "epilog_ret" => Ok(Stmt::Win64EpilogRet { span }),
        "data" => {
            let (w_tok, rest) = split_first_word(arg_str);
            let width = DataWidth::parse(w_tok).ok_or_else(|| ToolError::Syntax {
                span: span.clone(),
                message: format!("invalid @data width `{w_tok}`"),
            })?;
            let Some((lbl_raw, items_raw)) = split_first_comma(rest) else {
                return Err(ToolError::Syntax {
                    span,
                    message: "@data requires `<width> <label|->, <items...>`".into(),
                });
            };
            let lbl_trim = lbl_raw.trim();
            let items_trim = items_raw.trim();
            if lbl_trim.is_empty() || items_trim.is_empty() {
                return Err(ToolError::Syntax {
                    span,
                    message: "@data label and items must be non-empty".into(),
                });
            }
            let label = if lbl_trim == "-" {
                None
            } else {
                Some(lbl_trim.to_owned())
            };
            Ok(Stmt::Data {
                label,
                width,
                items: items_trim.to_owned(),
                span,
            })
        }
        "res" => {
            let (w_tok, rest) = split_first_word(arg_str);
            let width = DataWidth::parse(w_tok).ok_or_else(|| ToolError::Syntax {
                span: span.clone(),
                message: format!("invalid @res width `{w_tok}`"),
            })?;
            let parts = split_operands(rest);
            if parts.len() != 2 {
                return Err(ToolError::Syntax {
                    span,
                    message: "@res requires `<width> <label>, <count_expr>`".into(),
                });
            }
            Ok(Stmt::Reserve {
                label: parts[0].clone(),
                width,
                count_expr: parts[1].clone(),
                span,
            })
        }
        _ => Err(ToolError::Syntax {
            span,
            message: format!("unknown canonical directive `@{dir}`"),
        }),
    }
}

fn parse_body_line(clean: &str, span: SourceSpan, in_proc: bool) -> Result<Stmt, ToolError> {
    if let Some(lbl) = clean.strip_suffix(':') {
        let lbl = lbl.trim();
        if let Some(local) = lbl.strip_prefix('.') {
            if !in_proc || local.is_empty() {
                return Err(ToolError::Syntax {
                    span,
                    message: format!("invalid local label `{clean}` outside @proc"),
                });
            }
            return Ok(Stmt::LocalLabel {
                name: local.to_owned(),
                span,
            });
        }
        if lbl.is_empty() || lbl.contains(char::is_whitespace) {
            return Err(ToolError::Syntax {
                span,
                message: format!("invalid label `{clean}`"),
            });
        }
        return Ok(Stmt::GlobalLabel {
            name: lbl.to_owned(),
            span,
        });
    }

    let (first, rest) = split_first_word(clean);
    let lower_first = first.to_ascii_lowercase();
    let (mnemonic, op_str) = if matches!(
        lower_first.as_str(),
        "rep" | "repe" | "repz" | "repne" | "repnz" | "lock"
    ) && !rest.is_empty()
    {
        let (second, rest2) = split_first_word(rest);
        (
            format!("{lower_first} {}", second.to_ascii_lowercase()),
            rest2,
        )
    } else {
        (lower_first, rest)
    };

    let operands = if op_str.is_empty() {
        Vec::new()
    } else {
        split_operands(op_str)
    };

    Ok(Stmt::Instruction {
        mnemonic,
        operands,
        span,
    })
}

/// Single-quote ASCII codepoint (`'`).
const SQUOTE: char = 39 as char;

/// Strips a trailing `;` comment while respecting single- and double-quoted string literals.
#[must_use]
pub fn strip_inline_comment(line: &str) -> String {
    let mut in_single = false;
    let mut in_double = false;
    let mut out = String::with_capacity(line.len());
    for ch in line.chars() {
        if ch == SQUOTE && !in_double {
            in_single = !in_single;
            out.push(ch);
        } else if ch == '"' && !in_single {
            in_double = !in_double;
            out.push(ch);
        } else if ch == ';' && !in_single && !in_double {
            break;
        } else {
            out.push(ch);
        }
    }
    out.trim().to_owned()
}

/// Splits `s` into `(first_whitespace_delimited_token, trimmed_remainder)`.
#[must_use]
pub fn split_first_word(s: &str) -> (&str, &str) {
    let s = s.trim();
    for (idx, ch) in s.char_indices() {
        if ch.is_whitespace() {
            return (&s[..idx], s[idx..].trim());
        }
    }
    (s, "")
}

/// Splits `s` at the first top-level comma outside quotes and parentheses.
#[must_use]
pub fn split_first_comma(s: &str) -> Option<(&str, &str)> {
    let mut in_single = false;
    let mut in_double = false;
    let mut depth = 0_i32;
    for (idx, ch) in s.char_indices() {
        if ch == SQUOTE && !in_double {
            in_single = !in_single;
        } else if ch == '"' && !in_single {
            in_double = !in_double;
        } else if matches!(ch, '(' | '[' | '{') && !in_single && !in_double {
            depth += 1;
        } else if matches!(ch, ')' | ']' | '}') && !in_single && !in_double {
            depth -= 1;
        } else if ch == ',' && !in_single && !in_double && depth == 0 {
            return Some((&s[..idx], &s[idx + 1..]));
        }
    }
    None
}

/// Splits a comma-separated operand list respecting quotes, brackets, braces, and parentheses.
#[must_use]
pub fn split_operands(s: &str) -> Vec<String> {
    let s = s.trim();
    if s.is_empty() {
        return Vec::new();
    }
    let mut items = Vec::new();
    let mut cur = String::new();
    let mut in_single = false;
    let mut in_double = false;
    let mut depth = 0_i32;

    for ch in s.chars() {
        if ch == SQUOTE && !in_double {
            in_single = !in_single;
            cur.push(ch);
        } else if ch == '"' && !in_single {
            in_double = !in_double;
            cur.push(ch);
        } else if matches!(ch, '(' | '[' | '{') && !in_single && !in_double {
            depth += 1;
            cur.push(ch);
        } else if matches!(ch, ')' | ']' | '}') && !in_single && !in_double {
            depth -= 1;
            cur.push(ch);
        } else if ch == ',' && !in_single && !in_double && depth == 0 {
            items.push(cur.trim().to_owned());
            cur.clear();
        } else {
            cur.push(ch);
        }
    }
    items.push(cur.trim().to_owned());
    items
}

fn unquote_path(raw: &str) -> Option<String> {
    let t = raw.trim();
    if t.len() >= 2
        && ((t.starts_with('"') && t.ends_with('"'))
            || (t.starts_with(SQUOTE) && t.ends_with(SQUOTE)))
    {
        let inner = &t[1..t.len() - 1];
        if !inner.is_empty() {
            return Some(inner.to_owned());
        }
    }
    None
}

fn parse_usize_literal(raw: &str, span: &SourceSpan) -> Result<usize, ToolError> {
    let t = raw.trim();
    t.strip_prefix("0x")
        .or_else(|| t.strip_prefix("0X"))
        .map_or_else(|| t.parse::<usize>(), |hex| usize::from_str_radix(hex, 16))
        .map_err(|_| ToolError::Syntax {
            span: span.clone(),
            message: format!("invalid integer literal `{t}`"),
        })
}
