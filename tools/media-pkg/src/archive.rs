//! Pure-Rust deterministic `.ZIP`, POSIX `ustar` `.TAR`, RFC 1952 `.TAR.GZ`,
//! IEEE 802.3 `CRC-32`, and FIPS 180-4 `SHA-256` builders and verifiers.

use std::collections::BTreeMap;
use wincmd_diag::ToolError;

const DET_ZIP_DATE: u16 = (46 << 9) | (1 << 5) | 1;

/// Computes the standard IEEE 802.3 CRC-32 checksum used by `.ZIP` and `.GZ`.
#[must_use]
pub const fn crc32(bytes: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    let mut idx = 0usize;
    while idx < bytes.len() {
        crc ^= bytes[idx] as u32;
        let mut bit = 0;
        while bit < 8 {
            let mask = (crc & 1).wrapping_neg();
            crc = (crc >> 1) ^ (0xEDB8_8320 & mask);
            bit += 1;
        }
        idx += 1;
    }
    !crc
}

const SHA256_K: [u32; 64] = [
    0x428a_2f98, 0x7137_4491, 0xb5c0_fbcf, 0xe9b5_dba5, 0x3956_c25b, 0x59f1_11f1, 0x923f_82a4, 0xab1c_5ed5,
    0xd807_aa98, 0x1283_5b01, 0x2431_85be, 0x550c_7dc3, 0x72be_5d74, 0x80de_b1fe, 0x9bdc_06a7, 0xc19b_f174,
    0xe49b_69c1, 0xefbe_4786, 0x0fc1_9dc6, 0x240c_a1cc, 0x2de9_2c6f, 0x4a74_84aa, 0x5cb0_a9dc, 0x76f9_88da,
    0x983e_5152, 0xa831_c66d, 0xb003_27c8, 0xbf59_7fc7, 0xc6e0_0bf3, 0xd5a7_9147, 0x06ca_6351, 0x1429_2967,
    0x27b7_0a85, 0x2e1b_2138, 0x4d2c_6dfc, 0x5338_0d13, 0x650a_7354, 0x766a_0abb, 0x81c2_c92e, 0x9272_2c85,
    0xa2bf_e8a1, 0xa81a_664b, 0xc24b_8b70, 0xc76c_51a3, 0xd192_e819, 0xd699_0624, 0xf40e_3585, 0x106a_a070,
    0x19a4_c116, 0x1e37_6c08, 0x2748_774c, 0x34b0_bcb5, 0x391c_0cb3, 0x4ed8_aa4a, 0x5b9c_ca4f, 0x682e_6ff3,
    0x748f_82ee, 0x78a5_636f, 0x84c8_7814, 0x8cc7_0208, 0x90be_fffa, 0xa450_6ceb, 0xbef9_a3f7, 0xc671_78f2,
];

fn sha256_compress(state: &mut [u32; 8], block: &[u8; 64]) {
    let mut sched = [0u32; 64];
    for (idx, slot) in sched.iter_mut().enumerate().take(16) {
        let off = idx * 4;
        *slot = u32::from_be_bytes([block[off], block[off + 1], block[off + 2], block[off + 3]]);
    }
    for idx in 16..64 {
        let sig0 = sched[idx - 15].rotate_right(7)
            ^ sched[idx - 15].rotate_right(18)
            ^ (sched[idx - 15] >> 3);
        let sig1 = sched[idx - 2].rotate_right(17)
            ^ sched[idx - 2].rotate_right(19)
            ^ (sched[idx - 2] >> 10);
        sched[idx] = sched[idx - 16]
            .wrapping_add(sig0)
            .wrapping_add(sched[idx - 7])
            .wrapping_add(sig1);
    }
    let [mut st0, mut st1, mut st2, mut st3, mut st4, mut st5, mut st6, mut st7] = *state;
    for idx in 0..64 {
        let sum1 = st4.rotate_right(6) ^ st4.rotate_right(11) ^ st4.rotate_right(25);
        let choose = (st4 & st5) ^ ((!st4) & st6);
        let tmp1 = st7
            .wrapping_add(sum1)
            .wrapping_add(choose)
            .wrapping_add(SHA256_K[idx])
            .wrapping_add(sched[idx]);
        let sum0 = st0.rotate_right(2) ^ st0.rotate_right(13) ^ st0.rotate_right(22);
        let maj = (st0 & st1) ^ (st0 & st2) ^ (st1 & st2);
        let tmp2 = sum0.wrapping_add(maj);
        st7 = st6;
        st6 = st5;
        st5 = st4;
        st4 = st3.wrapping_add(tmp1);
        st3 = st2;
        st2 = st1;
        st1 = st0;
        st0 = tmp1.wrapping_add(tmp2);
    }
    for (dst, add) in state.iter_mut().zip([st0, st1, st2, st3, st4, st5, st6, st7]) {
        *dst = dst.wrapping_add(add);
    }
}

/// Computes the lowercased 64-hex-character FIPS 180-4 `SHA-256` digest of `data`.
#[must_use]
pub fn sha256_hex(data: &[u8]) -> String {
    let mut state: [u32; 8] = [
        0x6a09_e667, 0xbb67_ae85, 0x3c6e_f372, 0xa54f_f53a,
        0x510e_527f, 0x9b05_688c, 0x1f83_d9ab, 0x5be0_cd19,
    ];
    let bit_len = u64::try_from(data.len()).unwrap_or(0).wrapping_mul(8);
    let mut padded = Vec::with_capacity(data.len() + 72);
    padded.extend_from_slice(data);
    padded.push(0x80);
    while (padded.len() % 64) != 56 {
        padded.push(0);
    }
    padded.extend_from_slice(&bit_len.to_be_bytes());
    for blk in padded.as_chunks::<64>().0 {
        sha256_compress(&mut state, blk);
    }
    let mut hex = String::with_capacity(64);
    for word in state {
        use core::fmt::Write as _;
        let _ = write!(hex, "{word:08x}");
    }
    hex
}

/// Builds a deterministic `.ZIP` archive (`METHOD = 0` Stored, fixed DOS timestamp) from sorted entries.
#[must_use]
pub fn build_zip(entries: &[(&str, &[u8])]) -> Vec<u8> {
    let mut out = Vec::new();
    let mut central = Vec::new();
    for &(path, data) in entries {
        let local_off = u32::try_from(out.len()).unwrap_or(0);
        let crc = crc32(data);
        let sz = u32::try_from(data.len()).unwrap_or(0);
        let name_b = path.as_bytes();
        let nlen = u16::try_from(name_b.len()).unwrap_or(0);

        out.extend_from_slice(&0x0403_4B50u32.to_le_bytes());
        out.extend_from_slice(&20u16.to_le_bytes());
        out.extend_from_slice(&0u16.to_le_bytes());
        out.extend_from_slice(&0u16.to_le_bytes());
        out.extend_from_slice(&0u16.to_le_bytes());
        out.extend_from_slice(&DET_ZIP_DATE.to_le_bytes());
        out.extend_from_slice(&crc.to_le_bytes());
        out.extend_from_slice(&sz.to_le_bytes());
        out.extend_from_slice(&sz.to_le_bytes());
        out.extend_from_slice(&nlen.to_le_bytes());
        out.extend_from_slice(&0u16.to_le_bytes());
        out.extend_from_slice(name_b);
        out.extend_from_slice(data);

        central.extend_from_slice(&0x0201_4B50u32.to_le_bytes());
        central.extend_from_slice(&20u16.to_le_bytes());
        central.extend_from_slice(&20u16.to_le_bytes());
        central.extend_from_slice(&0u16.to_le_bytes());
        central.extend_from_slice(&0u16.to_le_bytes());
        central.extend_from_slice(&0u16.to_le_bytes());
        central.extend_from_slice(&DET_ZIP_DATE.to_le_bytes());
        central.extend_from_slice(&crc.to_le_bytes());
        central.extend_from_slice(&sz.to_le_bytes());
        central.extend_from_slice(&sz.to_le_bytes());
        central.extend_from_slice(&nlen.to_le_bytes());
        central.extend_from_slice(&[0u8; 14]);
        central.extend_from_slice(&local_off.to_le_bytes());
        central.extend_from_slice(name_b);
    }
    let cd_off = u32::try_from(out.len()).unwrap_or(0);
    let cd_sz = u32::try_from(central.len()).unwrap_or(0);
    let cnt = u16::try_from(entries.len()).unwrap_or(0);
    out.extend_from_slice(&central);
    out.extend_from_slice(&0x0605_4B50u32.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&cnt.to_le_bytes());
    out.extend_from_slice(&cnt.to_le_bytes());
    out.extend_from_slice(&cd_sz.to_le_bytes());
    out.extend_from_slice(&cd_off.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out
}

/// Parses a `.ZIP` archive built by [`build_zip`] and verifies CRC-32 integrity on every entry.
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if a local header signature or CRC-32 mismatches.
pub fn read_zip_entries(
    artifact: &str,
    zip: &[u8],
) -> Result<BTreeMap<String, Vec<u8>>, ToolError> {
    let mut pos = 0usize;
    let mut map = BTreeMap::new();
    while pos + 4 <= zip.len() {
        let sig = u32::from_le_bytes([zip[pos], zip[pos + 1], zip[pos + 2], zip[pos + 3]]);
        if sig == 0x0201_4B50 || sig == 0x0605_4B50 {
            break;
        }
        if sig != 0x0403_4B50 || pos + 30 > zip.len() {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: format!("invalid ZIP local file signature 0x{sig:08X}"),
            });
        }
        let expected_crc =
            u32::from_le_bytes([zip[pos + 14], zip[pos + 15], zip[pos + 16], zip[pos + 17]]);
        let sz = usize::try_from(u32::from_le_bytes([
            zip[pos + 18],
            zip[pos + 19],
            zip[pos + 20],
            zip[pos + 21],
        ]))
        .unwrap_or(0);
        let nlen = usize::from(u16::from_le_bytes([zip[pos + 26], zip[pos + 27]]));
        let xlen = usize::from(u16::from_le_bytes([zip[pos + 28], zip[pos + 29]]));
        let data_off = pos + 30 + nlen + xlen;
        if data_off + sz > zip.len() {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: String::from("truncated ZIP entry payload"),
            });
        }
        let name = String::from_utf8_lossy(&zip[pos + 30..pos + 30 + nlen]).into_owned();
        let payload = &zip[data_off..data_off + sz];
        if crc32(payload) != expected_crc {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: format!("CRC-32 mismatch on ZIP entry `{name}`"),
            });
        }
        map.insert(name, payload.to_vec());
        pos = data_off + sz;
    }
    Ok(map)
}

fn write_octal(dst: &mut [u8], val: u64) {
    let digits = dst.len().saturating_sub(1);
    let mut v = val;
    for i in (0..digits).rev() {
        dst[i] = b'0' + u8::try_from(v & 7).unwrap_or(0);
        v >>= 3;
    }
    dst[digits] = 0;
}

/// Builds a deterministic POSIX `ustar` `.TAR` archive with fixed `mtime = 0`, `uid/gid = 0`.
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if a relative path exceeds `255` bytes (`ustar` prefix+name).
pub fn build_ustar(artifact: &str, entries: &[(&str, &[u8], bool)]) -> Result<Vec<u8>, ToolError> {
    let mut tar = Vec::new();
    for &(path, data, exec) in entries {
        let (prefix, name) = if path.len() <= 100 {
            ("", path)
        } else if let Some((p, n)) = path.rsplit_once('/')
            && p.len() <= 155
            && n.len() <= 100
        {
            (p, n)
        } else {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: format!("TAR entry path `{path}` exceeds POSIX ustar limits"),
            });
        };

        let mut hdr = [0u8; 512];
        hdr[0..name.len()].copy_from_slice(name.as_bytes());
        write_octal(&mut hdr[100..108], if exec { 0o755 } else { 0o644 });
        write_octal(&mut hdr[108..116], 0);
        write_octal(&mut hdr[116..124], 0);
        write_octal(
            &mut hdr[124..136],
            u64::try_from(data.len()).unwrap_or(0),
        );
        write_octal(&mut hdr[136..148], 0);
        hdr[148..156].fill(b' ');
        hdr[156] = b'0';
        hdr[257..263].copy_from_slice(b"ustar\0");
        hdr[263..265].copy_from_slice(b"00");
        if !prefix.is_empty() {
            hdr[345..345 + prefix.len()].copy_from_slice(prefix.as_bytes());
        }
        let csum: u64 = hdr.iter().map(|&b| u64::from(b)).sum();
        write_octal(&mut hdr[148..155], csum);
        hdr[155] = b' ';

        tar.extend_from_slice(&hdr);
        tar.extend_from_slice(data);
        let rem = data.len() % 512;
        if rem != 0 {
            tar.resize(tar.len() + (512 - rem), 0);
        }
    }
    tar.resize(tar.len() + 1024, 0);
    Ok(tar)
}

/// Wraps raw `payload` in a deterministic RFC 1952 `.gz` stream (stored Deflate `BTYPE=00` blocks).
///
/// Decompresses cleanly with standard POSIX `tar -xzf` / `gzip -dc` with zero external crates.
#[must_use]
pub fn gzip_stored(payload: &[u8]) -> Vec<u8> {
    let mut gz = Vec::with_capacity(payload.len() + (payload.len() / 65535 + 1) * 5 + 18);
    // 10-byte RFC 1952 header: ID1=1F, ID2=8B, CM=08, FLG=00, MTIME=0, XFL=00, OS=FF
    gz.extend_from_slice(&[0x1F, 0x8B, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xFF]);
    if payload.is_empty() {
        gz.extend_from_slice(&[0x01, 0x00, 0x00, 0xFF, 0xFF]);
    } else {
        let chunks: Vec<&[u8]> = payload.chunks(65_535).collect();
        for (idx, chunk) in chunks.iter().enumerate() {
            let bfinal = u8::from(idx + 1 == chunks.len());
            let len16 = u16::try_from(chunk.len()).unwrap_or(0);
            let nlen16 = !len16;
            gz.push(bfinal);
            gz.extend_from_slice(&len16.to_le_bytes());
            gz.extend_from_slice(&nlen16.to_le_bytes());
            gz.extend_from_slice(chunk);
        }
    }
    gz.extend_from_slice(&crc32(payload).to_le_bytes());
    let isize32 = u32::try_from(payload.len() & 0xFFFF_FFFF).unwrap_or(0);
    gz.extend_from_slice(&isize32.to_le_bytes());
    gz
}

/// Decompresses a stored RFC 1952 `.gz` stream produced by [`gzip_stored`] and verifies `CRC-32`.
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if header, stored block `NLEN`, or trailer `CRC-32` fails.
pub fn gunzip_stored(artifact: &str, gz: &[u8]) -> Result<Vec<u8>, ToolError> {
    if gz.len() < 18 || gz[0..3] != [0x1F, 0x8B, 0x08] {
        return Err(ToolError::MediaPackage {
            artifact: artifact.to_owned(),
            details: String::from("invalid RFC 1952 gzip header"),
        });
    }
    let mut pos = 10usize;
    let mut out = Vec::new();
    loop {
        if pos + 5 > gz.len().saturating_sub(8) {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: String::from("truncated deflate stored block header"),
            });
        }
        let hdr = gz[pos];
        let len16 = u16::from_le_bytes([gz[pos + 1], gz[pos + 2]]);
        let nlen16 = u16::from_le_bytes([gz[pos + 3], gz[pos + 4]]);
        if (hdr & 0x06) != 0 || len16 != !nlen16 {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: String::from("non-stored or corrupted deflate block"),
            });
        }
        pos += 5;
        let blen = usize::from(len16);
        if pos + blen > gz.len().saturating_sub(8) {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: String::from("truncated deflate block payload"),
            });
        }
        out.extend_from_slice(&gz[pos..pos + blen]);
        pos += blen;
        if (hdr & 1) != 0 {
            break;
        }
    }
    let expected_crc = u32::from_le_bytes([gz[pos], gz[pos + 1], gz[pos + 2], gz[pos + 3]]);
    if crc32(&out) != expected_crc {
        return Err(ToolError::MediaPackage {
            artifact: artifact.to_owned(),
            details: String::from("gzip trailer CRC-32 mismatch"),
        });
    }
    Ok(out)
}

/// Parses a POSIX `ustar` `.TAR` buffer and extracts `(path -> bytes)` entries.
///
/// # Errors
///
/// Returns [`ToolError::MediaPackage`] if a header block is truncated.
pub fn read_ustar_entries(
    artifact: &str,
    tar: &[u8],
) -> Result<BTreeMap<String, Vec<u8>>, ToolError> {
    let mut pos = 0usize;
    let mut map = BTreeMap::new();
    while pos + 512 <= tar.len() {
        let hdr = &tar[pos..pos + 512];
        if hdr.iter().all(|&b| b == 0) {
            break;
        }
        let cstr = |slice: &[u8]| -> String {
            let end = slice.iter().position(|&b| b == 0).unwrap_or(slice.len());
            String::from_utf8_lossy(&slice[..end]).into_owned()
        };
        let name = cstr(&hdr[0..100]);
        let prefix = cstr(&hdr[345..500]);
        let full = if prefix.is_empty() {
            name
        } else {
            format!("{prefix}/{name}")
        };
        let sz_str = cstr(&hdr[124..136]);
        let sz = usize::from_str_radix(sz_str.trim(), 8).unwrap_or(0);
        let data_off = pos + 512;
        if data_off + sz > tar.len() {
            return Err(ToolError::MediaPackage {
                artifact: artifact.to_owned(),
                details: format!("truncated TAR entry `{full}`"),
            });
        }
        map.insert(full, tar[data_off..data_off + sz].to_vec());
        pos = data_off + sz.div_ceil(512) * 512;
    }
    Ok(map)
}
