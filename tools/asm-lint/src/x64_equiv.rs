//! Normalized `x86-64` (`PE32+`) instruction-stream decoder and 3-assembler equivalence verifier
//! across `NASM` (`-f win64` + `lld-link`), `FASM` (direct `PE64`), and `JWasm` (`-win64` + `lld-link`).

use std::collections::HashMap;
use wincmd_diag::ToolError;

#[derive(Clone, Debug, PartialEq, Eq)]
enum X64NormInsn {
    Branch { cond: u8, target_off: usize },
    CallRel { target_off: usize },
    RipRel { head: Vec<u8>, imm_tail: Vec<u8> },
    Bytes(Vec<u8>),
}

/// Returns `true` if `raw` begins with an `MZ` + `PE\0\0` `AMD64` (`0x8664`) header.
pub const fn is_x64_pe_image(raw: &[u8]) -> bool {
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
        && u16::from_le_bytes([raw[lfanew + 4], raw[lfanew + 5]]) == 0x8664
}

fn decode_x64_modrm_len(bytes: &[u8], modrm_pos: usize) -> Option<usize> {
    let modrm = *bytes.get(modrm_pos)?;
    let md = modrm >> 6;
    let rm = modrm & 7;
    if md == 3 {
        return Some(1);
    }
    let mut len = 1_usize;
    if rm == 4 {
        let sib = *bytes.get(modrm_pos + 1)?;
        len += 1;
        if md == 0 && (sib & 7) == 5 {
            len += 4;
        }
    }
    if md == 1 {
        len += 1;
    } else if md == 2 || (md == 0 && rm == 5) {
        len += 4;
    }
    if modrm_pos.saturating_add(len) <= bytes.len() {
        Some(len)
    } else {
        None
    }
}

fn decode_x64_rel_ctrl(bytes: &[u8], pc: usize, p: usize, op: u8) -> Option<(usize, X64NormInsn)> {
    if (0x70..=0x7F).contains(&op) || op == 0xEB {
        let d = (*bytes.get(p)?).cast_signed();
        let end = p + 1;
        let tgt = end.wrapping_add_signed(isize::from(d));
        let cond = if op == 0xEB { 0xFF } else { op & 0x0F };
        return Some((
            end - pc,
            X64NormInsn::Branch {
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
            X64NormInsn::Branch {
                cond: 0xFF,
                target_off: tgt,
            }
        } else {
            X64NormInsn::CallRel { target_off: tgt }
        };
        return Some((end - pc, insn));
    }
    let d = (*bytes.get(p)?).cast_signed();
    let end = p + 1;
    let tgt = end.wrapping_add_signed(isize::from(d));
    Some((
        end - pc,
        X64NormInsn::Branch {
            cond: op,
            target_off: tgt,
        },
    ))
}

fn build_modrm_or_riprel(
    bytes: &[u8],
    pc: usize,
    p: usize,
    mut head_prefix: Vec<u8>,
    imm_extra: usize,
) -> Option<(usize, X64NormInsn)> {
    let modrm = *bytes.get(p)?;
    let mlen = decode_x64_modrm_len(bytes, p)?;
    let end = p + mlen + imm_extra;
    if end > bytes.len() {
        return None;
    }
    if (modrm & 0xC7) == 0x05 {
        head_prefix.push(modrm);
        let imm_tail = bytes[p + 5..end].to_vec();
        return Some((
            end - pc,
            X64NormInsn::RipRel {
                head: head_prefix,
                imm_tail,
            },
        ));
    }
    head_prefix.extend_from_slice(&bytes[p..end]);
    Some((end - pc, X64NormInsn::Bytes(head_prefix)))
}

fn decode_x64_0f(
    bytes: &[u8],
    pc: usize,
    mut p: usize,
    mut head: Vec<u8>,
) -> Option<(usize, X64NormInsn)> {
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
            X64NormInsn::Branch {
                cond: op2 & 0x0F,
                target_off: tgt,
            },
        ));
    }
    head.extend_from_slice(&[0x0F, op2]);
    if matches!(op2, 0x05 | 0x0B | 0x31 | 0xA2 | 0xC8..=0xCF) {
        return Some((p - pc, X64NormInsn::Bytes(head)));
    }
    let imm_extra = usize::from(matches!(op2, 0xBA | 0xA4 | 0xAC));
    build_modrm_or_riprel(bytes, pc, p, head, imm_extra)
}

fn decode_x64_insn(bytes: &[u8], pc: usize) -> Option<(usize, X64NormInsn)> {
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
    let mut head = bytes[pc..p].to_vec();
    head.sort_unstable();

    let mut rex: Option<u8> = None;
    if let Some(&b) = bytes.get(p)
        && (0x40..=0x4F).contains(&b)
    {
        rex = Some(b);
        head.push(b);
        p += 1;
    }
    let rex_w = rex.is_some_and(|r| (r & 0x08) != 0);
    let imm_word = if has_66 && !rex_w { 2_usize } else { 4_usize };

    let op = *bytes.get(p)?;
    p += 1;
    if op == 0x0F {
        return decode_x64_0f(bytes, pc, p, head);
    }
    if matches!(op, 0x70..=0x7F | 0xE0..=0xE3 | 0xE8 | 0xE9 | 0xEB) {
        return decode_x64_rel_ctrl(bytes, pc, p, op);
    }

    let fixed_extra: Option<usize> = match op {
        0x50..=0x5F
        | 0x90..=0x99
        | 0x9B..=0x9F
        | 0xA4..=0xA7
        | 0xAA..=0xAF
        | 0xC3
        | 0xCB
        | 0xCC
        | 0xCF
        | 0xD7
        | 0xF4
        | 0xF5
        | 0xF8..=0xFD => Some(0),
        0x04
        | 0x0C
        | 0x14
        | 0x1C
        | 0x24
        | 0x2C
        | 0x34
        | 0x3C
        | 0x6A
        | 0xA8
        | 0xB0..=0xB7
        | 0xCD
        | 0xE4..=0xE7 => Some(1),
        0xC2 | 0xCA => Some(2),
        0xC8 => Some(3),
        0xB8..=0xBF => Some(if rex_w { 8 } else { imm_word }),
        0x05 | 0x0D | 0x15 | 0x1D | 0x25 | 0x2D | 0x35 | 0x3D | 0x68 | 0xA9 => Some(imm_word),
        _ => None,
    };
    head.push(op);
    if let Some(ex) = fixed_extra {
        let end = p + ex;
        if end > bytes.len() {
            return None;
        }
        head.extend_from_slice(&bytes[p..end]);
        return Some((end - pc, X64NormInsn::Bytes(head)));
    }

    let modrm_byte = *bytes.get(p)?;
    let reg_fld = (modrm_byte >> 3) & 7;
    let imm_extra = match op {
        0x69 | 0x81 | 0xC7 => imm_word,
        0xF7 if reg_fld <= 1 => imm_word,
        0x6B | 0x80 | 0x83 | 0xC0 | 0xC1 | 0xC6 => 1,
        0xF6 if reg_fld <= 1 => 1,
        _ => 0,
    };
    build_modrm_or_riprel(bytes, pc, p, head, imm_extra)
}

fn decode_x64_stream(bytes: &[u8]) -> Option<(Vec<X64NormInsn>, HashMap<usize, usize>)> {
    let mut trimmed_end = bytes
        .iter()
        .rposition(|&b| b != 0 && b != 0xCC)
        .map_or(0, |idx| idx + 1);
    while trimmed_end > 1 && bytes[trimmed_end - 1] == 0x90 && bytes[trimmed_end - 2] == 0xC3 {
        trimmed_end -= 1;
    }
    let mut insns = Vec::new();
    let mut off_to_idx = HashMap::new();
    let mut pc = 0_usize;
    while pc < trimmed_end {
        let idx = insns.len();
        off_to_idx.insert(pc, idx);
        let (len, insn) = decode_x64_insn(&bytes[..trimmed_end], pc)?;
        insns.push(insn);
        pc += len;
    }
    off_to_idx.insert(trimmed_end, insns.len());
    Some((insns, off_to_idx))
}

fn is_equiv_x64_bytes(v1: &[u8], v2: &[u8]) -> bool {
    if v1 == v2 {
        return true;
    }
    let strip1 = if v1.first() == Some(&0x66) && v2.first() == Some(&0x66) {
        &v1[1..]
    } else {
        v1
    };
    let strip2 = if v1.first() == Some(&0x66) && v2.first() == Some(&0x66) {
        &v2[1..]
    } else {
        v2
    };
    if strip1.len() == 2 && strip2.len() == 2 {
        return crate::is_equiv_x86_mod11_pair(strip1[0], strip1[1], strip2[0], strip2[1]);
    }
    if (strip1 == [0x48, 0x98] && strip2 == [0x48, 0x63, 0xC0])
        || (strip1 == [0x48, 0x63, 0xC0] && strip2 == [0x48, 0x98])
    {
        return true;
    }
    if strip1.len() == 3
        && strip2.len() == 3
        && (0x40..=0x4F).contains(&strip1[0])
        && (0x40..=0x4F).contains(&strip2[0])
        && (strip1[0] & 0xFA) == (strip2[0] & 0xFA)
        && ((strip1[0] >> 2) & 1) == (strip2[0] & 1)
        && (strip1[0] & 1) == ((strip2[0] >> 2) & 1)
    {
        return crate::is_equiv_x86_mod11_pair(strip1[1], strip1[2], strip2[1], strip2[2]);
    }
    is_equiv_x64_sib_scale1(strip1, strip2)
}

const fn x64_sib_scale1_regs(rex: Option<u8>, sib: u8) -> Option<(u8, u8)> {
    if (sib >> 6) != 0 {
        return None;
    }
    let rx = match rex {
        Some(r) => (r >> 1) & 1,
        None => 0,
    };
    let rb = match rex {
        Some(r) => r & 1,
        None => 0,
    };
    let idx = (rx << 3) | ((sib >> 3) & 7);
    let base = (rb << 3) | (sib & 7);
    if idx == 4 {
        return None;
    }
    Some((idx, base))
}

fn is_equiv_x64_sib_scale1(s1: &[u8], s2: &[u8]) -> bool {
    if s1.len() < 3 || s2.len() < 3 {
        return false;
    }
    let (rex1, body1) = if (0x40..=0x4F).contains(&s1[0]) {
        (Some(s1[0]), &s1[1..])
    } else {
        (None, s1)
    };
    let (rex2, body2) = if (0x40..=0x4F).contains(&s2[0]) {
        (Some(s2[0]), &s2[1..])
    } else {
        (None, s2)
    };
    let rw_rr1 = rex1.map_or(0, |r| r & 0x0C);
    let rw_rr2 = rex2.map_or(0, |r| r & 0x0C);
    if rw_rr1 != rw_rr2 || body1.len() < 2 || body2.len() < 2 {
        return false;
    }
    let op_len = if body1[0] == 0x0F { 2_usize } else { 1_usize };
    if body1.len() < op_len + 2 || body2.len() < op_len + 2 || body1[..op_len] != body2[..op_len] {
        return false;
    }
    let Some((idx1, base1)) = x64_sib_scale1_regs(rex1, body1[op_len + 1]) else {
        return false;
    };
    let Some((idx2, base2)) = x64_sib_scale1_regs(rex2, body2[op_len + 1]) else {
        return false;
    };
    if !(idx1 == base2 && base1 == idx2) {
        return false;
    }
    if body1.len() == body2.len() {
        return body1[op_len] == body2[op_len]
            && (body1[op_len] & 0x07) == 0x04
            && (body1[op_len] & 0xC0) != 0xC0
            && body1[op_len + 2..] == body2[op_len + 2..];
    }
    let (sh, lg, lg_base) = if body1.len() + 1 == body2.len() {
        (body1, body2, base2)
    } else if body2.len() + 1 == body1.len() {
        (body2, body1, base1)
    } else {
        return false;
    };
    lg.len() >= op_len + 3
        && (sh[op_len] & 0xC7) == 0x04
        && (lg[op_len] & 0xC7) == 0x44
        && (sh[op_len] & 0x38) == (lg[op_len] & 0x38)
        && (lg_base & 0x07) == 0x05
        && lg[op_len + 2] == 0x00
        && sh[op_len + 2..] == lg[op_len + 3..]
}

/// Verifies instruction-by-instruction equivalence between two x86-64 `.text` streams.
pub fn verify_x64_stream_equiv(
    artifact: &str,
    base_name: &str,
    base_bytes: &[u8],
    other_name: &str,
    other_bytes: &[u8],
) -> Result<(), ToolError> {
    let Some((b_insns, b_map)) = decode_x64_stream(base_bytes) else {
        return Err(ToolError::Equivalence {
            artifact: artifact.to_owned(),
            offset: 0,
            details: format!("failed to decode x86-64 instruction stream for {base_name}"),
        });
    };
    let Some((o_insns, o_map)) = decode_x64_stream(other_bytes) else {
        return Err(ToolError::Equivalence {
            artifact: artifact.to_owned(),
            offset: 0,
            details: format!("failed to decode x86-64 instruction stream for {other_name}"),
        });
    };
    if b_insns.len() != o_insns.len() {
        return Err(ToolError::Equivalence {
            artifact: artifact.to_owned(),
            offset: b_insns.len().min(o_insns.len()),
            details: format!(
                "x86-64 instruction count mismatch: {base_name}={} insns vs {other_name}={} insns",
                b_insns.len(),
                o_insns.len()
            ),
        });
    }
    for (idx, (bi, oi)) in b_insns.iter().zip(o_insns.iter()).enumerate() {
        let ok = match (bi, oi) {
            (
                X64NormInsn::Branch {
                    cond: c1,
                    target_off: t1,
                },
                X64NormInsn::Branch {
                    cond: c2,
                    target_off: t2,
                },
            ) => c1 == c2 && crate::same_target_idx(&b_map, *t1, &o_map, *t2),
            (X64NormInsn::CallRel { target_off: t1 }, X64NormInsn::CallRel { target_off: t2 }) => {
                crate::same_target_idx(&b_map, *t1, &o_map, *t2)
            }
            (
                X64NormInsn::RipRel {
                    head: h1,
                    imm_tail: i1,
                },
                X64NormInsn::RipRel {
                    head: h2,
                    imm_tail: i2,
                },
            ) => h1 == h2 && i1 == i2,
            (X64NormInsn::Bytes(v1), X64NormInsn::Bytes(v2)) => is_equiv_x64_bytes(v1, v2),
            _ => false,
        };
        if !ok {
            return Err(ToolError::Equivalence {
                artifact: artifact.to_owned(),
                offset: idx,
                details: format!(
                    "x64 instruction #{idx} mismatch: {base_name}={bi:?} vs {other_name}={oi:?}"
                ),
            });
        }
    }
    Ok(())
}
