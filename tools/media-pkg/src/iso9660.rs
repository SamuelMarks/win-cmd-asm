//! Deterministic ISO-9660 CD-ROM (`.ISO`) image masterer and directory reader for
//! Windows 95, 98, Me, and Windows NT 3.1 through Windows 11 distribution media.

use std::collections::BTreeMap;
use wincmd_diag::ToolError;

/// Standard `2,048`-byte ISO-9660 logical sector size.
pub const ISO_SECTOR: usize = 2048;

const PVD_DEC_TIME: &[u8; 17] = b"2026010100000000\0";
const DIR_REC_TIME: [u8; 7] = [126, 1, 1, 0, 0, 0, 0];

fn write_both_u16(dst: &mut [u8], val: u16) {
    dst[0..2].copy_from_slice(&val.to_le_bytes());
    dst[2..4].copy_from_slice(&val.to_be_bytes());
}

fn write_both_u32(dst: &mut [u8], val: u32) {
    dst[0..4].copy_from_slice(&val.to_le_bytes());
    dst[4..8].copy_from_slice(&val.to_be_bytes());
}

fn pad_str_a(dst: &mut [u8], text: &str) {
    dst.fill(b' ');
    let bytes = text.as_bytes();
    let n = bytes.len().min(dst.len());
    for i in 0..n {
        dst[i] = bytes[i].to_ascii_uppercase();
    }
}

fn append_dir_record(
    sec_buf: &mut Vec<u8>,
    ident: &[u8],
    extent_lba: u32,
    data_len: u32,
    is_dir: bool,
) {
    let rec_len = (33 + ident.len()) + usize::from(ident.len().is_multiple_of(2));
    let in_sec = sec_buf.len() % ISO_SECTOR;
    if in_sec != 0 && in_sec + rec_len > ISO_SECTOR {
        let pad = ISO_SECTOR - in_sec;
        sec_buf.resize(sec_buf.len() + pad, 0);
    }
    let base = sec_buf.len();
    sec_buf.resize(base + rec_len, 0);
    sec_buf[base] = u8::try_from(rec_len).unwrap_or(34);
    write_both_u32(&mut sec_buf[base + 2..base + 10], extent_lba);
    write_both_u32(&mut sec_buf[base + 10..base + 18], data_len);
    sec_buf[base + 18..base + 25].copy_from_slice(&DIR_REC_TIME);
    sec_buf[base + 25] = if is_dir { 0x02 } else { 0x00 };
    write_both_u16(&mut sec_buf[base + 28..base + 32], 1);
    sec_buf[base + 32] = u8::try_from(ident.len()).unwrap_or(1);
    sec_buf[base + 33..base + 33 + ident.len()].copy_from_slice(ident);
}

fn pad_to_sector(buf: &mut Vec<u8>) {
    let rem = buf.len() % ISO_SECTOR;
    if rem != 0 {
        buf.resize(buf.len() + (ISO_SECTOR - rem), 0);
    }
}

struct DirNode<'a> {
    name: String,
    files: Vec<(String, &'a [u8])>,
}

fn build_path_tables(dirs: &[DirNode<'_>], dir_lbas: &[u32]) -> (Vec<u8>, Vec<u8>) {
    let mut l_tbl = Vec::new();
    let mut m_tbl = Vec::new();
    for (is_be, tbl) in [(false, &mut l_tbl), (true, &mut m_tbl)] {
        // Root entry (ID = `\0`, parent = 1)
        tbl.push(1);
        tbl.push(0);
        if is_be {
            tbl.extend_from_slice(&dir_lbas[0].to_be_bytes());
            tbl.extend_from_slice(&1u16.to_be_bytes());
        } else {
            tbl.extend_from_slice(&dir_lbas[0].to_le_bytes());
            tbl.extend_from_slice(&1u16.to_le_bytes());
        }
        tbl.push(0);
        tbl.push(0);
        for (idx, d) in dirs.iter().enumerate().skip(1) {
            let id = d.name.as_bytes();
            tbl.push(u8::try_from(id.len()).unwrap_or(1));
            tbl.push(0);
            if is_be {
                tbl.extend_from_slice(&dir_lbas[idx].to_be_bytes());
                tbl.extend_from_slice(&1u16.to_be_bytes());
            } else {
                tbl.extend_from_slice(&dir_lbas[idx].to_le_bytes());
                tbl.extend_from_slice(&1u16.to_le_bytes());
            }
            tbl.extend_from_slice(id);
            if !id.len().is_multiple_of(2) {
                tbl.push(0);
            }
        }
    }
    (l_tbl, m_tbl)
}

fn write_iso_pvd_and_vdst(
    iso: &mut [u8],
    volume_id: &str,
    total_lbas: u32,
    path_tbl_len: u32,
    root_lba: u32,
    root_len: u32,
) {
    let pvd = &mut iso[16 * ISO_SECTOR..17 * ISO_SECTOR];
    pvd[0] = 0x01;
    pvd[1..6].copy_from_slice(b"CD001");
    pvd[6] = 0x01;
    pad_str_a(&mut pvd[8..40], "WIN9X_WINNT");
    pad_str_a(&mut pvd[40..72], volume_id);
    write_both_u32(&mut pvd[80..88], total_lbas);
    write_both_u16(&mut pvd[120..124], 1);
    write_both_u16(&mut pvd[124..128], 1);
    write_both_u16(&mut pvd[128..132], 2048);
    write_both_u32(&mut pvd[132..140], path_tbl_len);
    pvd[140..144].copy_from_slice(&18u32.to_le_bytes());
    pvd[148..152].copy_from_slice(&19u32.to_be_bytes());
    let mut root_rec = Vec::new();
    append_dir_record(&mut root_rec, &[0x00], root_lba, root_len, true);
    pvd[156..156 + 34].copy_from_slice(&root_rec[..34]);
    pad_str_a(&mut pvd[318..446], "WIN_CMD_ASM_PROJECT");
    pvd[813..830].copy_from_slice(PVD_DEC_TIME);
    pvd[830..847].copy_from_slice(PVD_DEC_TIME);
    pvd[881] = 0x01;

    let vdst = &mut iso[17 * ISO_SECTOR..18 * ISO_SECTOR];
    vdst[0] = 0xFF;
    vdst[1..6].copy_from_slice(b"CD001");
    vdst[6] = 0x01;
}
/// Masters a deterministic ISO-9660 Level 2 image from `(rel_path, contents)` pairs.
///
/// Supports root files (`"README.MD"`) and one-level subdirectories (`"WIN64/WINCMD64.EXE"`).
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if a path has more than 2 components or an empty name.
pub fn build_iso9660(
    artifact: &str,
    volume_id: &str,
    entries: &[(&str, &[u8])],
) -> Result<Vec<u8>, ToolError> {
    let mut grouped: BTreeMap<String, Vec<(String, &[u8])>> = BTreeMap::new();
    grouped.insert(String::new(), Vec::new());
    for &(rel, data) in entries {
        let parts: Vec<&str> = rel.split('/').filter(|s| !s.is_empty()).collect();
        match parts.as_slice() {
            [fname] => grouped
                .entry(String::new())
                .or_default()
                .push((fname.to_ascii_uppercase(), data)),
            [dname, fname] => grouped
                .entry(dname.to_ascii_uppercase())
                .or_default()
                .push((fname.to_ascii_uppercase(), data)),
            _ => {
                return Err(ToolError::MediaPackage {
                    artifact: artifact.to_owned(),
                    details: format!("unsupported ISO path depth `{rel}`"),
                });
            }
        }
    }

    let mut dirs = Vec::with_capacity(grouped.len());
    for (name, mut files) in grouped {
        files.sort_by(|a, b| a.0.cmp(&b.0));
        dirs.push(DirNode { name, files });
    }

    // Layout LBAs: 0..16 system, 16 PVD, 17 VDST, 18 L-Path, 19 M-Path, 20.. dirs, then file extents
    let dir_start_lba = 20u32;
    let mut cur_lba = dir_start_lba;
    // Estimate directory sector counts (2 sectors max per directory is plenty for <= 30 files)
    let mut dir_lbas = Vec::with_capacity(dirs.len());
    let mut dir_sec_counts = Vec::with_capacity(dirs.len());
    for d in &dirs {
        dir_lbas.push(cur_lba);
        let est_secs = u32::try_from(((d.files.len() + dirs.len() + 4) * 64).div_ceil(ISO_SECTOR).max(1))
            .unwrap_or(1);
        dir_sec_counts.push(est_secs);
        cur_lba += est_secs;
    }

    let mut file_lbas: Vec<Vec<u32>> = Vec::with_capacity(dirs.len());
    for d in &dirs {
        let mut fl = Vec::with_capacity(d.files.len());
        for (_, bytes) in &d.files {
            fl.push(cur_lba);
            let secs = u32::try_from(bytes.len().div_ceil(ISO_SECTOR).max(1)).unwrap_or(1);
            cur_lba += secs;
        }
        file_lbas.push(fl);
    }
    let total_lbas = cur_lba;

    let mut dir_blobs = Vec::with_capacity(dirs.len());
    for (d_idx, d) in dirs.iter().enumerate() {
        let own_lba = dir_lbas[d_idx];
        let own_len = dir_sec_counts[d_idx] * u32::try_from(ISO_SECTOR).unwrap_or(2048);
        let parent_lba = dir_lbas[0];
        let parent_len = dir_sec_counts[0] * u32::try_from(ISO_SECTOR).unwrap_or(2048);
        let mut buf = Vec::new();
        append_dir_record(&mut buf, &[0x00], own_lba, own_len, true);
        append_dir_record(&mut buf, &[0x01], parent_lba, parent_len, true);
        if d_idx == 0 {
            for (sub_i, sub_d) in dirs.iter().enumerate().skip(1) {
                let s_len = dir_sec_counts[sub_i] * u32::try_from(ISO_SECTOR).unwrap_or(2048);
                append_dir_record(&mut buf, sub_d.name.as_bytes(), dir_lbas[sub_i], s_len, true);
            }
        }
        for (f_i, (fname, fbytes)) in d.files.iter().enumerate() {
            let iso_id = format!("{fname};1");
            let flen = u32::try_from(fbytes.len()).unwrap_or(u32::MAX);
            append_dir_record(&mut buf, iso_id.as_bytes(), file_lbas[d_idx][f_i], flen, false);
        }
        pad_to_sector(&mut buf);
        let target_bytes = usize::try_from(own_len).unwrap_or(ISO_SECTOR);
        buf.resize(target_bytes, 0);
        dir_blobs.push(buf);
    }

    let (l_tbl, m_tbl) = build_path_tables(&dirs, &dir_lbas);
    let path_tbl_len = u32::try_from(l_tbl.len()).unwrap_or(0);

    let mut iso = vec![0u8; usize::try_from(total_lbas).unwrap_or(0) * ISO_SECTOR];
    write_iso_pvd_and_vdst(
        &mut iso,
        volume_id,
        total_lbas,
        path_tbl_len,
        dir_lbas[0],
        dir_sec_counts[0] * 2048,
    );

    iso[18 * ISO_SECTOR..18 * ISO_SECTOR + l_tbl.len()].copy_from_slice(&l_tbl);
    iso[19 * ISO_SECTOR..19 * ISO_SECTOR + m_tbl.len()].copy_from_slice(&m_tbl);

    for (d_idx, blob) in dir_blobs.iter().enumerate() {
        let off = usize::try_from(dir_lbas[d_idx]).unwrap_or(0) * ISO_SECTOR;
        iso[off..off + blob.len()].copy_from_slice(blob);
    }
    for (d_idx, d) in dirs.iter().enumerate() {
        for (f_i, (_, fbytes)) in d.files.iter().enumerate() {
            let off = usize::try_from(file_lbas[d_idx][f_i]).unwrap_or(0) * ISO_SECTOR;
            iso[off..off + fbytes.len()].copy_from_slice(fbytes);
        }
    }
    Ok(iso)
}

fn parse_iso_dir(iso: &[u8], lba: usize, byte_len: usize) -> Vec<(String, usize, usize, bool)> {
    let mut out = Vec::new();
    let start = lba * ISO_SECTOR;
    let end = (start + byte_len).min(iso.len());
    let mut pos = start;
    while pos < end {
        let rlen = usize::from(iso[pos]);
        if rlen == 0 {
            let next_sec = ((pos / ISO_SECTOR) + 1) * ISO_SECTOR;
            pos = next_sec;
            continue;
        }
        let ext_lba = usize::try_from(u32::from_le_bytes([
            iso[pos + 2],
            iso[pos + 3],
            iso[pos + 4],
            iso[pos + 5],
        ]))
        .unwrap_or(0);
        let dlen = usize::try_from(u32::from_le_bytes([
            iso[pos + 10],
            iso[pos + 11],
            iso[pos + 12],
            iso[pos + 13],
        ]))
        .unwrap_or(0);
        let is_dir = (iso[pos + 25] & 0x02) != 0;
        let id_len = usize::from(iso[pos + 32]);
        let id_bytes = &iso[pos + 33..pos + 33 + id_len];
        if id_bytes != [0x00] && id_bytes != [0x01] {
            let raw = String::from_utf8_lossy(id_bytes);
            let clean = raw.strip_suffix(";1").unwrap_or(&raw).to_owned();
            out.push((clean, ext_lba, dlen, is_dir));
        }
        pos += rlen;
    }
    out
}

/// Reads an ISO-9660 `.ISO` image and returns all files mapped by uppercase relative path.
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if the Primary Volume Descriptor is missing or invalid.
pub fn read_iso9660_files(
    artifact: &str,
    iso: &[u8],
) -> Result<BTreeMap<String, Vec<u8>>, ToolError> {
    if iso.len() < 18 * ISO_SECTOR || &iso[16 * ISO_SECTOR + 1..16 * ISO_SECTOR + 6] != b"CD001" {
        return Err(ToolError::MediaPackage {
            artifact: artifact.to_owned(),
            details: String::from("invalid ISO-9660 Primary Volume Descriptor"),
        });
    }
    let root_rec = &iso[16 * ISO_SECTOR + 156..16 * ISO_SECTOR + 190];
    let root_lba = usize::try_from(u32::from_le_bytes([
        root_rec[2],
        root_rec[3],
        root_rec[4],
        root_rec[5],
    ]))
    .unwrap_or(0);
    let root_len = usize::try_from(u32::from_le_bytes([
        root_rec[10],
        root_rec[11],
        root_rec[12],
        root_rec[13],
    ]))
    .unwrap_or(0);

    let mut files = BTreeMap::new();
    for (name, lba, len, is_dir) in parse_iso_dir(iso, root_lba, root_len) {
        if is_dir {
            for (fname, flba, flen, _) in parse_iso_dir(iso, lba, len) {
                let off = flba * ISO_SECTOR;
                files.insert(format!("{name}/{fname}"), iso[off..off + flen].to_vec());
            }
        } else {
            let off = lba * ISO_SECTOR;
            files.insert(name, iso[off..off + len].to_vec());
        }
    }
    Ok(files)
}
