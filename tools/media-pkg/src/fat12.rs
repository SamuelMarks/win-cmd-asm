//! Deterministic FAT12 floppy disk image (`.IMG`) builder and reader across standard
//! IBM PC / MS-DOS / Win16 geometries (`160K`, `360K`, `720K`, and `1.44M`).

use std::collections::BTreeMap;
use wincmd_diag::ToolError;

/// Standard `512`-byte physical sector size for IBM PC FAT12 floppies.
pub const SECTOR_BYTES: usize = 512;

/// Fixed deterministic FAT packed date (`2026-01-01`: `((2026 - 1980) << 9) | (1 << 5) | 1`).
pub const DET_FAT_DATE: u16 = (46 << 9) | (1 << 5) | 1;
/// Fixed deterministic FAT packed time (`00:00:00`).
pub const DET_FAT_TIME: u16 = 0;

/// Standard PC/MS-DOS floppy media geometries (`160K`, `360K`, `720K`, `1.44M`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FloppyGeom {
    /// 1981 IBM PC 5.25" single-sided 40-track 8-sector (`163,840` bytes).
    F160K,
    /// IBM PC XT 5.25" double-sided 40-track 9-sector (`368,640` bytes).
    F360K,
    /// 3.5" DD double-sided 80-track 9-sector (`737,280` bytes).
    F720K,
    /// 3.5" HD double-sided 80-track 18-sector (`1,474,560` bytes).
    F1440K,
}

/// Exact BIOS Parameter Block (`BPB`) layout parameters for a [`FloppyGeom`].
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BpbSpec {
    /// Total 512-byte logical sectors on the medium.
    pub total_sectors: u16,
    /// Sectors per allocation cluster (`1` or `2`).
    pub sec_per_clus: u8,
    /// Maximum 32-byte entries in the root directory.
    pub root_entries: u16,
    /// Media descriptor byte (`0xFE`, `0xFD`, `0xF9`, `0xF0`).
    pub media_desc: u8,
    /// Sectors per FAT12 copy.
    pub sec_per_fat: u16,
    /// Physical sectors per track.
    pub sec_per_track: u16,
    /// Number of read/write heads (`1` or `2`).
    pub heads: u16,
}

impl FloppyGeom {
    /// All four supported floppy geometries in ascending capacity order.
    pub const ALL: &'static [Self] = &[Self::F160K, Self::F360K, Self::F720K, Self::F1440K];

    /// Returns the exact MS-DOS [`BpbSpec`] for this floppy geometry.
    #[must_use]
    pub const fn bpb(self) -> BpbSpec {
        match self {
            Self::F160K => BpbSpec {
                total_sectors: 320,
                sec_per_clus: 1,
                root_entries: 64,
                media_desc: 0xFE,
                sec_per_fat: 1,
                sec_per_track: 8,
                heads: 1,
            },
            Self::F360K => BpbSpec {
                total_sectors: 720,
                sec_per_clus: 2,
                root_entries: 112,
                media_desc: 0xFD,
                sec_per_fat: 2,
                sec_per_track: 9,
                heads: 2,
            },
            Self::F720K => BpbSpec {
                total_sectors: 1440,
                sec_per_clus: 2,
                root_entries: 112,
                media_desc: 0xF9,
                sec_per_fat: 3,
                sec_per_track: 9,
                heads: 2,
            },
            Self::F1440K => BpbSpec {
                total_sectors: 2880,
                sec_per_clus: 1,
                root_entries: 224,
                media_desc: 0xF0,
                sec_per_fat: 9,
                sec_per_track: 18,
                heads: 2,
            },
        }
    }

    /// Returns the exact total image byte count (`163,840`..`1,474,560`).
    #[must_use]
    pub const fn byte_capacity(self) -> usize {
        (self.bpb().total_sectors as usize) * SECTOR_BYTES
    }
}

/// Formats an uppercased FAT `8.3` filename into an `11`-byte space-padded FCB slice.
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if `name` is empty, exceeds `8.3`, or uses invalid characters.
pub fn format_fcb_83(artifact: &str, name: &str) -> Result<[u8; 11], ToolError> {
    let (stem, ext) = name.split_once('.').unwrap_or((name, ""));
    let ok_char = |b: u8| b.is_ascii_alphanumeric() || b == b'_' || b == b'-';
    if stem.is_empty()
        || stem.len() > 8
        || ext.len() > 3
        || ext.contains('.')
        || !stem.bytes().all(ok_char)
        || !ext.bytes().all(ok_char)
    {
        return Err(ToolError::MediaPackage {
            artifact: artifact.to_owned(),
            details: format!("invalid FAT12 8.3 entry name `{name}`"),
        });
    }
    let mut out = [b' '; 11];
    for (idx, b) in stem.bytes().enumerate() {
        out[idx] = b.to_ascii_uppercase();
    }
    for (idx, b) in ext.bytes().enumerate() {
        out[8 + idx] = b.to_ascii_uppercase();
    }
    Ok(out)
}

const fn set_fat12_entry(fat: &mut [u8], cluster: usize, value: u16) {
    let off = cluster + (cluster / 2);
    let [lo, hi] = value.to_le_bytes();
    if (cluster & 1) == 0 {
        fat[off] = lo;
        fat[off + 1] = (fat[off + 1] & 0xF0) | (hi & 0x0F);
    } else {
        let shifted = (value << 4).to_le_bytes();
        fat[off] = (fat[off] & 0x0F) | (shifted[0] & 0xF0);
        fat[off + 1] = shifted[1];
    }
}

const fn get_fat12_entry(fat: &[u8], cluster: usize) -> u16 {
    let off = cluster + (cluster / 2);
    let pair = u16::from_le_bytes([fat[off], fat[off + 1]]);
    if (cluster & 1) == 0 {
        pair & 0x0FFF
    } else {
        pair >> 4
    }
}

fn write_boot_sector(img: &mut [u8], spec: BpbSpec, vol_label: &[u8; 11]) {
    img[0..3].copy_from_slice(&[0xEB, 0x3C, 0x90]);
    img[3..11].copy_from_slice(b"WINCMD  ");
    img[11..13].copy_from_slice(&512u16.to_le_bytes());
    img[13] = spec.sec_per_clus;
    img[14..16].copy_from_slice(&1u16.to_le_bytes());
    img[16] = 2;
    img[17..19].copy_from_slice(&spec.root_entries.to_le_bytes());
    img[19..21].copy_from_slice(&spec.total_sectors.to_le_bytes());
    img[21] = spec.media_desc;
    img[22..24].copy_from_slice(&spec.sec_per_fat.to_le_bytes());
    img[24..26].copy_from_slice(&spec.sec_per_track.to_le_bytes());
    img[26..28].copy_from_slice(&spec.heads.to_le_bytes());
    img[38] = 0x29;
    img[39..43].copy_from_slice(&0x1982_2026u32.to_le_bytes());
    img[43..54].copy_from_slice(vol_label);
    img[54..62].copy_from_slice(b"FAT12   ");
    // Minimal 8086 stub: XOR AX, AX; INT 16h; INT 19h (reboot on keypress if booted without IO.SYS)
    img[0x3E..0x44].copy_from_slice(&[0x31, 0xC0, 0xCD, 0x16, 0xCD, 0x19]);
    img[510] = 0x55;
    img[511] = 0xAA;
}

fn write_dir_entry(
    slot: &mut [u8],
    fcb_name: &[u8; 11],
    attr: u8,
    first_cluster: u16,
    size_bytes: u32,
) {
    slot[0..11].copy_from_slice(fcb_name);
    slot[11] = attr;
    slot[22..24].copy_from_slice(&DET_FAT_TIME.to_le_bytes());
    slot[24..26].copy_from_slice(&DET_FAT_DATE.to_le_bytes());
    slot[26..28].copy_from_slice(&first_cluster.to_le_bytes());
    slot[28..32].copy_from_slice(&size_bytes.to_le_bytes());
}

/// Masters a complete, reproducible FAT12 floppy `.IMG` from `(name_83, contents)` pairs.
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if root directory entries or data clusters overflow `geom`,
/// or if any filename violates FAT `8.3`.
pub fn build_fat12_floppy(
    artifact: &str,
    geom: FloppyGeom,
    volume_label: &[u8; 11],
    files: &[(&str, &[u8])],
) -> Result<Vec<u8>, ToolError> {
    let spec = geom.bpb();
    let max_files = usize::from(spec.root_entries).saturating_sub(1);
    if files.len() > max_files {
        return Err(ToolError::MediaPackage {
            artifact: artifact.to_owned(),
            details: format!(
                "file count {} exceeds FAT12 root directory capacity {max_files}",
                files.len()
            ),
        });
    }

    let root_sectors = (usize::from(spec.root_entries) * 32).div_ceil(SECTOR_BYTES);
    let fat1_sec = 1usize;
    let fat2_sec = fat1_sec + usize::from(spec.sec_per_fat);
    let root_sec = fat2_sec + usize::from(spec.sec_per_fat);
    let data_sec = root_sec + root_sectors;
    let data_sectors = usize::from(spec.total_sectors).saturating_sub(data_sec);
    let total_clusters = data_sectors / usize::from(spec.sec_per_clus);
    let cluster_bytes = usize::from(spec.sec_per_clus) * SECTOR_BYTES;

    let mut img = vec![0u8; geom.byte_capacity()];
    write_boot_sector(&mut img[..SECTOR_BYTES], spec, volume_label);

    let mut fat = vec![0u8; usize::from(spec.sec_per_fat) * SECTOR_BYTES];
    fat[0] = spec.media_desc;
    fat[1] = 0xFF;
    fat[2] = 0xFF;

    let root_off = root_sec * SECTOR_BYTES;
    write_dir_entry(&mut img[root_off..root_off + 32], volume_label, 0x08, 0, 0);

    let mut next_cluster = 2usize;
    for (idx, &(name, payload)) in files.iter().enumerate() {
        let fcb = format_fcb_83(artifact, name)?;
        let needed = payload.len().div_ceil(cluster_bytes);
        if next_cluster.saturating_sub(2).saturating_add(needed) > total_clusters {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: format!("floppy cluster capacity exhausted while adding `{name}`"),
            });
        }
        let first_clus = if needed == 0 {
            0u16
        } else {
            u16::try_from(next_cluster).unwrap_or(0)
        };
        for c_idx in 0..needed {
            let cur_c = next_cluster + c_idx;
            let nxt_val = if c_idx + 1 == needed {
                0x0FFFu16
            } else {
                u16::try_from(cur_c + 1).unwrap_or(0x0FFF)
            };
            set_fat12_entry(&mut fat, cur_c, nxt_val);
            let clus_sec = data_sec + (cur_c - 2) * usize::from(spec.sec_per_clus);
            let dst_off = clus_sec * SECTOR_BYTES;
            let src_off = c_idx * cluster_bytes;
            let chunk = &payload[src_off..(src_off + cluster_bytes).min(payload.len())];
            img[dst_off..dst_off + chunk.len()].copy_from_slice(chunk);
        }
        next_cluster += needed;

        let dir_off = root_off + (idx + 1) * 32;
        let sz32 = u32::try_from(payload.len()).unwrap_or(u32::MAX);
        write_dir_entry(&mut img[dir_off..dir_off + 32], &fcb, 0x20, first_clus, sz32);
    }

    let fat_len = fat.len();
    let f1_off = fat1_sec * SECTOR_BYTES;
    let f2_off = fat2_sec * SECTOR_BYTES;
    img[f1_off..f1_off + fat_len].copy_from_slice(&fat);
    img[f2_off..f2_off + fat_len].copy_from_slice(&fat);
    Ok(img)
}

/// Parses a FAT12 `.IMG` buffer and extracts all regular root-directory files (`NAME.EXT -> bytes`).
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if the BPB or cluster chain is malformed.
pub fn read_fat12_root_files(
    artifact: &str,
    img: &[u8],
) -> Result<BTreeMap<String, Vec<u8>>, ToolError> {
    if img.len() < SECTOR_BYTES || img[510] != 0x55 || img[511] != 0xAA {
        return Err(ToolError::MediaPackage {
            artifact: artifact.to_owned(),
            details: String::from("missing 0x55AA boot sector signature"),
        });
    }
    let sec_per_clus = usize::from(img[13].max(1));
    let res_secs = usize::from(u16::from_le_bytes([img[14], img[15]]));
    let num_fats = usize::from(img[16]);
    let root_entries = usize::from(u16::from_le_bytes([img[17], img[18]]));
    let sec_per_fat = usize::from(u16::from_le_bytes([img[22], img[23]]));

    let fat_off = res_secs * SECTOR_BYTES;
    let fat_end = fat_off + sec_per_fat * SECTOR_BYTES;
    let root_sec = res_secs + num_fats * sec_per_fat;
    let root_sectors = (root_entries * 32).div_ceil(SECTOR_BYTES);
    let data_sec = root_sec + root_sectors;
    let clus_bytes = sec_per_clus * SECTOR_BYTES;
    if fat_end > img.len() || data_sec * SECTOR_BYTES > img.len() {
        return Err(ToolError::MediaPackage {
            artifact: artifact.to_owned(),
            details: String::from("truncated FAT12 metadata area"),
        });
    }
    let fat = &img[fat_off..fat_end];
    let mut files = BTreeMap::new();
    for i in 0..root_entries {
        let e = root_sec * SECTOR_BYTES + i * 32;
        let first_b = img[e];
        if first_b == 0x00 {
            break;
        }
        if first_b == 0xE5 || (img[e + 11] & 0x18) != 0 {
            continue;
        }
        let stem = String::from_utf8_lossy(&img[e..e + 8]).trim_end().to_owned();
        let ext = String::from_utf8_lossy(&img[e + 8..e + 11]).trim_end().to_owned();
        let full = if ext.is_empty() {
            stem
        } else {
            format!("{stem}.{ext}")
        };
        let mut cur = usize::from(u16::from_le_bytes([img[e + 26], img[e + 27]]));
        let sz = usize::try_from(u32::from_le_bytes([
            img[e + 28],
            img[e + 29],
            img[e + 30],
            img[e + 31],
        ]))
        .unwrap_or(0);
        let mut out = Vec::with_capacity(sz);
        while sz > out.len() && (2..0xFF8).contains(&cur) {
            let sec = data_sec + (cur - 2) * sec_per_clus;
            let off = sec * SECTOR_BYTES;
            let rem = (sz - out.len()).min(clus_bytes);
            if off + rem > img.len() {
                return Err(ToolError::MediaPackage {
                    artifact: artifact.to_owned(),
                    details: format!("cluster {cur} out of image bounds for `{full}`"),
                });
            }
            out.extend_from_slice(&img[off..off + rem]);
            cur = usize::from(get_fat12_entry(fat, cur));
        }
        files.insert(full, out);
    }
    Ok(files)
}
