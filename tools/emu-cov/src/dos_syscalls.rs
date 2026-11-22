//! Complete MS-DOS `1.25`, `2.00`, `4.00`, and `7.10` (`Win9x` `LFN`/`FAT32`) syscall,
//! FCB/XFCB, `INT 25h` FAT12, `LIM EMS 4.0`, `XMS`, `NLS`/`DBCS`, child `EXEC`, and
//! `IBM PC BIOS` emulation for [`DosVm`].

use crate::{DosEra, DosVm, OpenFile, SavedExecFrame, flags};
use std::collections::BTreeSet;
use wincmd_diag::ErrCode;

/// ASCII backslash character (`0x5C`).
pub(crate) const BS: char = 0x5C as char;

/// ASCII backslash byte (`0x5C`).
pub(crate) const BS_BYTE: u8 = 0x5C;

/// ASCII horizontal tab byte (`0x09`).
pub(crate) const TAB_BYTE: u8 = 0x09;

/// Sentinel path representing the unredirected console `stdin` device in `DUP`/`DUP2`.
pub(crate) const CON_IN_SENTINEL: &str = "*CON_IN*";

/// Sentinel path representing the unredirected console `stdout` device in `DUP`/`DUP2`.
pub(crate) const CON_OUT_SENTINEL: &str = "*CON_OUT*";

/// Sentinel path representing the DOS `NUL` character sink/EOF device.
pub(crate) const NUL_SENTINEL: &str = "*NUL*";

/// Metadata stored per virtual filesystem path inside [`DosVm`].
#[derive(Clone, Debug, Default)]
pub struct VfsMeta {
    /// DOS/Win32 file attribute bitmask (`ATTR_READONLY`, `ATTR_VOLUME`, `ATTR_DIRECTORY`, etc.).
    pub attr: u8,
    /// Original mixed-case / Long Filename (`LFN`) display string.
    pub lfn_name: String,
    /// Explicit `8.3` short alias (e.g. `PROGRA~1`) on MS-DOS 7.x volumes.
    pub short_alias: String,
}

/// Active MS-DOS 7.x `AX=714Eh` Long Filename search cursor.
#[derive(Clone, Debug)]
pub struct LfnSearchState {
    /// Matched `(short_alias, lfn_name, attr, size)` entries.
    pub matches: Vec<(String, String, u8, u32)>,
    /// Next entry index to return on `AX=714Fh`.
    pub index: usize,
}

impl DosVm {
    /// Enables LIM EMS 4.0 (`INT 67h`, `"EMMXXXX0"` signature at `0xF000:0x000A`, page frame `0xE000`).
    pub fn enable_ems(&mut self) {
        self.ems_enabled = true;
        let sig_lin = Self::lin(0xF000, 0x000A);
        self.mem[sig_lin..(sig_lin + 8)].copy_from_slice(b"EMMXXXX0");
    }

    /// Enables XMS 2.0 (`INT 2Fh AX=4300h/4310h`) extended memory blocks.
    pub const fn enable_xms(&mut self) {
        self.xms_enabled = true;
    }

    /// Queues a hardware/software interrupt frame onto the guest stack so the installed ISR executes next.
    pub fn raise_hardware_int(&mut self, vec_no: u8) {
        let vec_lin = usize::from(vec_no) * 4;
        let isr_off = self.read_u16_lin(vec_lin);
        let isr_seg = self.read_u16_lin(vec_lin + 2);
        self.push16(self.flags);
        self.push16(self.sregs[Self::CS]);
        self.push16(self.ip);
        self.flags &= !flags::IF;
        self.sregs[Self::CS] = isr_seg;
        self.ip = isr_off;
    }

    /// Returns `"A:"` prefix string.
    #[must_use]
    pub fn drive_a_root() -> String {
        let mut s = String::from("A:");
        s.push(BS);
        s
    }

    /// Normalizes a guest path relative to `self.cwd` into an uppercase VFS key (`DIR\FILE.EXT` or `FILE.EXT`).
    #[must_use]
    pub fn norm_vfs_key(&self, raw: &str) -> String {
        let bs_str = String::from(BS);
        let trimmed = raw.trim().replace('/', &bs_str);
        let a_root_up = format!("A:{BS}");
        let a_root_lo = format!("a:{BS}");
        let c_root_up = format!("C:{BS}");
        let c_root_lo = format!("c:{BS}");

        let (has_drive_or_root, stripped) = trimmed
            .strip_prefix(&a_root_up)
            .or_else(|| trimmed.strip_prefix(&a_root_lo))
            .or_else(|| trimmed.strip_prefix(&c_root_up))
            .or_else(|| trimmed.strip_prefix(&c_root_lo))
            .map_or_else(
                || {
                    trimmed
                        .strip_prefix("A:")
                        .or_else(|| trimmed.strip_prefix("a:"))
                        .or_else(|| trimmed.strip_prefix("C:"))
                        .or_else(|| trimmed.strip_prefix("c:"))
                        .map_or_else(
                            || {
                                trimmed
                                    .strip_prefix(BS)
                                    .map_or((false, trimmed.as_str()), |rest| (true, rest))
                            },
                            |rest| (false, rest),
                        )
                },
                |rest| (true, rest),
            );

        if stripped.is_empty() || stripped == "." {
            return if has_drive_or_root {
                String::new()
            } else {
                self.cwd.clone()
            };
        }
        if stripped == ".." {
            if let Some((parent, _)) = self.cwd.rsplit_once(BS) {
                return parent.to_owned();
            }
            return String::new();
        }

        let base = if has_drive_or_root || self.cwd.is_empty() {
            stripped.to_ascii_uppercase()
        } else {
            format!("{}{BS}{}", self.cwd, stripped.to_ascii_uppercase())
        };

        let a_prefixed = format!("A:{BS}{base}");
        if self.vfs.contains_key(&a_prefixed) && !self.vfs.contains_key(&base) {
            a_prefixed
        } else {
            base
        }
    }

    /// Inserts a read-only, volume-label, or LFN-aliased file into the VM's VFS.
    pub fn insert_vfs_with_meta(&mut self, path: &str, data: Vec<u8>, attr: u8, short_alias: &str) {
        let key = self.norm_vfs_key(path);
        let base_lfn = path.rsplit([BS, '/']).next().unwrap_or(path).to_owned();
        if (attr & 0x08) == 0 {
            self.vfs.insert(key.clone(), data);
        } else {
            self.vfs.insert(key.clone(), Vec::new());
        }
        self.vfs_meta.insert(
            key,
            VfsMeta {
                attr,
                lfn_name: base_lfn,
                short_alias: short_alias.to_ascii_uppercase(),
            },
        );
    }

    /// Parses a filename string at `DS:SI` into the 37-byte FCB at `ES:DI` (`INT 21h AH=29h`).
    pub(crate) fn dos_parse_fcb(&mut self) {
        let ds = self.sregs[Self::DS];
        let es = self.sregs[Self::ES];
        let mut si = self.regs[6];
        let di = self.regs[7];
        let al_flags = self.get_reg8(0);

        if (al_flags & 0x01) != 0 {
            while matches!(self.read_u8(ds, si), b' ' | TAB_BYTE) {
                si = si.wrapping_add(1);
            }
        }

        let mut drive = 0_u8;
        if self.read_u8(ds, si.wrapping_add(1)) == b':' {
            let dch = self.read_u8(ds, si).to_ascii_uppercase();
            if dch.is_ascii_uppercase() {
                drive = dch - b'A' + 1;
                si = si.wrapping_add(2);
            } else {
                self.set_reg8(0, 0xFF);
                return;
            }
        }
        self.write_u8(es, di, drive);
        for i in 1..=11_u16 {
            self.write_u8(es, di.wrapping_add(i), b' ');
        }

        let mut has_wild = false;
        let mut idx = 0_u16;
        loop {
            let ch = self.read_u8(ds, si);
            if matches!(ch, 0 | 0x0D | b' ' | TAB_BYTE | b'.' | b'/' | BS_BYTE) {
                break;
            }
            si = si.wrapping_add(1);
            if ch == b'*' {
                has_wild = true;
                while idx < 8 {
                    self.write_u8(es, di.wrapping_add(1 + idx), b'?');
                    idx += 1;
                }
            } else if idx < 8 {
                if ch == b'?' {
                    has_wild = true;
                }
                self.write_u8(es, di.wrapping_add(1 + idx), ch.to_ascii_uppercase());
                idx += 1;
            }
        }

        if self.read_u8(ds, si) == b'.' {
            si = si.wrapping_add(1);
            let mut eidx = 0_u16;
            loop {
                let ch = self.read_u8(ds, si);
                if matches!(ch, 0 | 0x0D | b' ' | TAB_BYTE | b'/' | BS_BYTE) {
                    break;
                }
                si = si.wrapping_add(1);
                if ch == b'*' {
                    has_wild = true;
                    while eidx < 3 {
                        self.write_u8(es, di.wrapping_add(9 + eidx), b'?');
                        eidx += 1;
                    }
                } else if eidx < 3 {
                    if ch == b'?' {
                        has_wild = true;
                    }
                    self.write_u8(es, di.wrapping_add(9 + eidx), ch.to_ascii_uppercase());
                    eidx += 1;
                }
            }
        }

        self.regs[6] = si;
        self.set_reg8(0, u8::from(has_wild));
    }

    pub(crate) fn fcb_name_at(&self, seg: u16, fcb_off: u16) -> ([u8; 8], [u8; 3]) {
        let mut name = [b' '; 8];
        let mut ext = [b' '; 3];
        for (i, slot) in name.iter_mut().enumerate() {
            *slot = self.read_u8(seg, fcb_off.wrapping_add(1 + u16::try_from(i).unwrap_or(0)));
        }
        for (i, slot) in ext.iter_mut().enumerate() {
            *slot = self.read_u8(seg, fcb_off.wrapping_add(9 + u16::try_from(i).unwrap_or(0)));
        }
        (name, ext)
    }

    pub(crate) fn fcb_to_filename(name: [u8; 8], ext: [u8; 3]) -> String {
        let n = core::str::from_utf8(&name).unwrap_or("").trim_end();
        let e = core::str::from_utf8(&ext).unwrap_or("").trim_end();
        if e.is_empty() {
            n.to_ascii_uppercase()
        } else {
            format!("{}.{}", n.to_ascii_uppercase(), e.to_ascii_uppercase())
        }
    }

    fn key_to_fcb83(key: &str) -> Option<([u8; 8], [u8; 3])> {
        if key.contains(BS) {
            return None;
        }
        let (stem, ext_s) = key.split_once('.').unwrap_or((key, ""));
        let mut n = [b' '; 8];
        let mut e = [b' '; 3];
        for (i, b) in stem.bytes().take(8).enumerate() {
            n[i] = b.to_ascii_uppercase();
        }
        for (i, b) in ext_s.bytes().take(3).enumerate() {
            e[i] = b.to_ascii_uppercase();
        }
        Some((n, e))
    }

    fn fcb_pat_matches(pat: ([u8; 8], [u8; 3]), cand: ([u8; 8], [u8; 3])) -> bool {
        pat.0
            .iter()
            .zip(cand.0.iter())
            .all(|(&p, &c)| p == b'?' || p == c)
            && pat
                .1
                .iter()
                .zip(cand.1.iter())
                .all(|(&p, &c)| p == b'?' || p == c)
    }

    fn resolve_fcb_key(&self, raw_name: &str) -> String {
        let a_prefixed = format!("A:{BS}{raw_name}");
        if self.vfs.contains_key(&a_prefixed) {
            a_prefixed
        } else {
            raw_name.to_owned()
        }
    }

    fn strip_a_root(key: &str) -> &str {
        let a_root = format!("A:{BS}");
        key.strip_prefix(&a_root).unwrap_or(key)
    }

    #[allow(clippy::too_many_lines)]
    pub(crate) fn handle_fcb_call(&mut self, ah: u8) {
        let ds = self.sregs[Self::DS];
        let dx = self.regs[2];
        let is_xfcb = self.read_u8(ds, dx) == 0xFF;
        let req_attr = if is_xfcb {
            self.read_u8(ds, dx.wrapping_add(6))
        } else {
            0
        };
        let fcb_off = if is_xfcb { dx.wrapping_add(7) } else { dx };
        let (name, ext) = self.fcb_name_at(ds, fcb_off);
        let raw_fn = Self::fcb_to_filename(name, ext);
        let key = self.resolve_fcb_key(&raw_fn);

        match ah {
            0x0F => {
                let is_vol = self.vfs_meta.get(&key).is_some_and(|m| (m.attr & 0x08) != 0);
                if !is_vol && let Some(data) = self.vfs.get(&key) {
                    let sz = u32::try_from(data.len()).unwrap_or(0).to_le_bytes();
                    self.write_u16(ds, fcb_off.wrapping_add(14), 128);
                    self.write_u16(
                        ds,
                        fcb_off.wrapping_add(16),
                        u16::from_le_bytes([sz[0], sz[1]]),
                    );
                    self.write_u16(
                        ds,
                        fcb_off.wrapping_add(18),
                        u16::from_le_bytes([sz[2], sz[3]]),
                    );
                    self.set_reg8(0, 0x00);
                } else {
                    self.set_reg8(0, 0xFF);
                }
            }
            0x10 => {
                self.set_reg8(0, 0x00);
            }
            0x16 => {
                self.vfs.insert(key.clone(), Vec::new());
                if is_xfcb && req_attr != 0 {
                    self.vfs_meta.entry(key).or_default().attr = req_attr;
                }
                self.write_u16(ds, fcb_off.wrapping_add(14), 128);
                self.write_u16(ds, fcb_off.wrapping_add(16), 0);
                self.write_u16(ds, fcb_off.wrapping_add(18), 0);
                self.set_reg8(0, 0x00);
            }
            0x11 | 0x12 => {
                if ah == 0x11 {
                    let want_vol = is_xfcb && req_attr == 0x08;
                    let mut matches = Vec::new();
                    for (k, bytes) in &self.vfs {
                        let clean = Self::strip_a_root(k);
                        let attr = self.vfs_meta.get(k).map_or(0, |m| m.attr);
                        let is_vol = (attr & 0x08) != 0;
                        if want_vol != is_vol {
                            continue;
                        }
                        if let Some(cand83) = Self::key_to_fcb83(clean)
                            && Self::fcb_pat_matches((name, ext), (cand83.0, cand83.1))
                        {
                            matches.push((
                                cand83.0,
                                cand83.1,
                                attr,
                                u32::try_from(bytes.len()).unwrap_or(0),
                            ));
                        }
                    }
                    self.fcb_search = matches;
                    self.fcb_search_idx = 0;
                }
                if let Some((m_name, m_ext, m_attr, m_sz)) =
                    self.fcb_search.get(self.fcb_search_idx).copied()
                {
                    self.fcb_search_idx += 1;
                    let (dta_seg, dta_off) = self.dta;
                    let entry_off = if is_xfcb {
                        self.write_u8(dta_seg, dta_off, 0xFF);
                        self.write_u8(dta_seg, dta_off.wrapping_add(6), m_attr);
                        dta_off.wrapping_add(7)
                    } else {
                        dta_off
                    };
                    self.write_u8(dta_seg, entry_off, 1);
                    for (i, &b) in m_name.iter().enumerate() {
                        self.write_u8(
                            dta_seg,
                            entry_off.wrapping_add(1 + u16::try_from(i).unwrap_or(0)),
                            b,
                        );
                    }
                    for (i, &b) in m_ext.iter().enumerate() {
                        self.write_u8(
                            dta_seg,
                            entry_off.wrapping_add(9 + u16::try_from(i).unwrap_or(0)),
                            b,
                        );
                    }
                    self.write_u8(dta_seg, entry_off.wrapping_add(12), m_attr);
                    let sb = m_sz.to_le_bytes();
                    self.write_u16(
                        dta_seg,
                        entry_off.wrapping_add(29),
                        u16::from_le_bytes([sb[0], sb[1]]),
                    );
                    self.write_u16(
                        dta_seg,
                        entry_off.wrapping_add(31),
                        u16::from_le_bytes([sb[2], sb[3]]),
                    );
                    self.set_reg8(0, 0x00);
                } else {
                    self.set_reg8(0, 0xFF);
                }
            }
            0x13 => {
                let keys: Vec<String> = self
                    .vfs
                    .keys()
                    .filter(|k| {
                        let clean = Self::strip_a_root(k);
                        let ro = self.vfs_meta.get(*k).is_some_and(|m| (m.attr & 0x01) != 0);
                        !ro && Self::key_to_fcb83(clean)
                            .is_some_and(|c| Self::fcb_pat_matches((name, ext), (c.0, c.1)))
                    })
                    .cloned()
                    .collect();
                if keys.is_empty() {
                    self.set_reg8(0, 0xFF);
                } else {
                    for k in keys {
                        self.vfs.remove(&k);
                        self.vfs_meta.remove(&k);
                    }
                    self.set_reg8(0, 0x00);
                }
            }
            0x17 => {
                let (dst_pat_n, dst_pat_e) = self.fcb_name_at(ds, fcb_off.wrapping_add(16));
                let keys: Vec<String> = self
                    .vfs
                    .keys()
                    .filter(|k| {
                        let clean = Self::strip_a_root(k);
                        Self::key_to_fcb83(clean)
                            .is_some_and(|c| Self::fcb_pat_matches((name, ext), (c.0, c.1)))
                    })
                    .cloned()
                    .collect();
                if keys.is_empty() {
                    self.set_reg8(0, 0xFF);
                } else {
                    for old_k in keys {
                        let clean = Self::strip_a_root(&old_k);
                        if let Some((src_n, src_e)) = Self::key_to_fcb83(clean) {
                            let mut out_n = [b' '; 8];
                            let mut out_e = [b' '; 3];
                            for i in 0..8 {
                                out_n[i] = if dst_pat_n[i] == b'?' {
                                    src_n[i]
                                } else {
                                    dst_pat_n[i]
                                };
                            }
                            for i in 0..3 {
                                out_e[i] = if dst_pat_e[i] == b'?' {
                                    src_e[i]
                                } else {
                                    dst_pat_e[i]
                                };
                            }
                            let new_k = Self::fcb_to_filename(out_n, out_e);
                            if let Some(bytes) = self.vfs.remove(&old_k) {
                                self.vfs.insert(new_k.clone(), bytes);
                                if let Some(meta) = self.vfs_meta.remove(&old_k) {
                                    self.vfs_meta.insert(new_k, meta);
                                }
                            }
                        }
                    }
                    self.set_reg8(0, 0x00);
                }
            }
            0x27 => {
                let rec_sz = usize::from(self.read_u16(ds, fcb_off.wrapping_add(14)).max(1));
                let rec_lo = u32::from(self.read_u16(ds, fcb_off.wrapping_add(33)));
                let rec_hi = u32::from(self.read_u16(ds, fcb_off.wrapping_add(35)));
                let start_rec = usize::try_from((rec_hi << 16) | rec_lo).unwrap_or(0);
                let req_recs = usize::from(self.regs[1]);
                let start_byte = start_rec.saturating_mul(rec_sz);
                let (avail_bytes, slice) = {
                    let data = self.vfs.get(&key).map_or(&[][..], Vec::as_slice);
                    let avail = data
                        .len()
                        .saturating_sub(start_byte)
                        .min(req_recs.saturating_mul(rec_sz));
                    (avail, data[start_byte..(start_byte + avail)].to_vec())
                };
                let (dta_seg, dta_off) = self.dta;
                for (i, &b) in slice.iter().enumerate() {
                    self.write_u8(
                        dta_seg,
                        dta_off.wrapping_add(u16::try_from(i).unwrap_or(0)),
                        b,
                    );
                }
                let full_recs = avail_bytes.div_ceil(rec_sz);
                let next_rec = u32::try_from(start_rec + full_recs)
                    .unwrap_or(0)
                    .to_le_bytes();
                self.write_u16(
                    ds,
                    fcb_off.wrapping_add(33),
                    u16::from_le_bytes([next_rec[0], next_rec[1]]),
                );
                self.write_u16(
                    ds,
                    fcb_off.wrapping_add(35),
                    u16::from_le_bytes([next_rec[2], next_rec[3]]),
                );
                self.regs[1] = u16::try_from(full_recs).unwrap_or(0);
                self.set_reg8(0, u8::from(avail_bytes == 0));
            }
            _ => {
                let rec_sz = usize::from(self.read_u16(ds, fcb_off.wrapping_add(14)).max(1));
                let rec_lo = u32::from(self.read_u16(ds, fcb_off.wrapping_add(33)));
                let rec_hi = u32::from(self.read_u16(ds, fcb_off.wrapping_add(35)));
                let start_rec = usize::try_from((rec_hi << 16) | rec_lo).unwrap_or(0);
                let req_recs = usize::from(self.regs[1]);
                let start_byte = start_rec.saturating_mul(rec_sz);
                let (dta_seg, dta_off) = self.dta;
                if req_recs == 0 {
                    let entry = self.vfs.entry(key.clone()).or_default();
                    entry.resize(start_byte, 0);
                } else {
                    let total_write = req_recs.saturating_mul(rec_sz);
                    let mut chunk = Vec::with_capacity(total_write);
                    for i in 0..total_write {
                        chunk.push(
                            self.read_u8(
                                dta_seg,
                                dta_off.wrapping_add(u16::try_from(i).unwrap_or(0)),
                            ),
                        );
                    }
                    let entry = self.vfs.entry(key.clone()).or_default();
                    if start_byte + total_write > entry.len() {
                        entry.resize(start_byte + total_write, 0);
                    }
                    entry[start_byte..(start_byte + total_write)].copy_from_slice(&chunk);
                    let next_rec = u32::try_from(start_rec + req_recs)
                        .unwrap_or(0)
                        .to_le_bytes();
                    self.write_u16(
                        ds,
                        fcb_off.wrapping_add(33),
                        u16::from_le_bytes([next_rec[0], next_rec[1]]),
                    );
                    self.write_u16(
                        ds,
                        fcb_off.wrapping_add(35),
                        u16::from_le_bytes([next_rec[2], next_rec[3]]),
                    );
                }
                let new_sz = u32::try_from(self.vfs.get(&key).map_or(0, Vec::len))
                    .unwrap_or(0)
                    .to_le_bytes();
                self.write_u16(
                    ds,
                    fcb_off.wrapping_add(16),
                    u16::from_le_bytes([new_sz[0], new_sz[1]]),
                );
                self.write_u16(
                    ds,
                    fcb_off.wrapping_add(18),
                    u16::from_le_bytes([new_sz[2], new_sz[3]]),
                );
                self.set_reg8(0, 0x00);
            }
        }
    }

    pub(crate) fn handle_int25_fat12(&mut self) {
        let drive = self.get_reg8(0);
        if drive > 2 {
            self.set_cf(true);
            self.regs[0] = 0x0201;
            self.push16(self.flags);
            return;
        }
        let ds = self.sregs[Self::DS];
        let bx = self.regs[3];
        let secs = usize::from(self.regs[1]).min(2);
        let mut fat = vec![0xFF_u8; 1024];
        fat[0] = 0xFE;
        fat[1] = 0xFF;
        fat[2] = 0xFF;
        let free_cnt = usize::from(self.free_clusters_125).min(313);
        for clus in 2..(2 + free_cnt) {
            let off = clus + (clus >> 1);
            if (clus & 1) == 0 {
                fat[off] = 0x00;
                fat[off + 1] &= 0xF0;
            } else {
                fat[off] &= 0x0F;
                fat[off + 1] = 0x00;
            }
        }
        for (i, &b) in fat.iter().take(secs * 512).enumerate() {
            self.write_u8(ds, bx.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
        }
        self.set_cf(false);
        self.push16(self.flags);
    }

    pub(crate) fn handle_dos200_find(&mut self, is_first: bool) {
        if is_first {
            let req_attr = self.regs[1];
            let raw_pat = self.read_asciiz(self.sregs[Self::DS], self.regs[2]);
            let norm_pat = self.norm_vfs_key(&raw_pat);
            let clean_pat = Self::strip_a_root(&norm_pat);
            let (dir_pfx, pat_leaf) = clean_pat
                .rsplit_once(BS)
                .map_or(("", clean_pat), |(d, f)| (d, f));
            let mut results = Vec::new();
            if (req_attr & 0x0010) != 0 {
                for d in &self.dirs {
                    let dc = Self::strip_a_root(d);
                    if dc.is_empty() {
                        continue;
                    }
                    let (dp, dl) = dc.rsplit_once(BS).map_or(("", dc), |(p, l)| (p, l));
                    if dp == dir_pfx && Self::simple_glob(pat_leaf, dl) {
                        results.push((dl.to_owned(), 0x10_u8, 0_u32));
                    }
                }
            }
            for (k, bytes) in &self.vfs {
                let clean = Self::strip_a_root(k);
                let attr = self.vfs_meta.get(k).map_or(0, |m| m.attr);
                if (attr & 0x08) != 0 {
                    continue;
                }
                let (fp, fl) = clean
                    .rsplit_once(BS)
                    .map_or(("", clean), |(p, l)| (p, l));
                if fp == dir_pfx && Self::simple_glob(pat_leaf, fl) {
                    results.push((fl.to_owned(), attr, u32::try_from(bytes.len()).unwrap_or(0)));
                }
            }
            self.dta_searches.insert(self.dta, (results, 0));
        }

        let next_entry = self.dta_searches.get_mut(&self.dta).and_then(|(list, idx)| {
            let item = list.get(*idx).cloned();
            if item.is_some() {
                *idx += 1;
            }
            item
        });

        if let Some((name, attr, sz)) = next_entry {
            let (seg, off) = self.dta;
            self.write_u8(seg, off.wrapping_add(21), attr);
            let sb = sz.to_le_bytes();
            self.write_u16(seg, off.wrapping_add(26), u16::from_le_bytes([sb[0], sb[1]]));
            self.write_u16(seg, off.wrapping_add(28), u16::from_le_bytes([sb[2], sb[3]]));
            for i in 0..13_u16 {
                self.write_u8(seg, off.wrapping_add(30 + i), 0);
            }
            for (i, b) in name.bytes().take(12).enumerate() {
                self.write_u8(
                    seg,
                    off.wrapping_add(30 + u16::try_from(i).unwrap_or(0)),
                    b.to_ascii_uppercase(),
                );
            }
            self.set_cf(false);
        } else {
            self.regs[0] = ErrCode::NoMoreFiles.code();
            self.set_cf(true);
        }
    }

    pub(crate) fn simple_glob(pat: &str, text: &str) -> bool {
        if pat == "*.*" || pat == "*" {
            return true;
        }
        let pb = pat.as_bytes();
        let tb = text.as_bytes();
        let mut dp = vec![false; tb.len() + 1];
        dp[0] = true;
        for &pc in pb {
            if pc == b'*' {
                for j in 1..=tb.len() {
                    dp[j] = dp[j] || dp[j - 1];
                }
            } else {
                for j in (1..=tb.len()).rev() {
                    dp[j] = dp[j - 1] && (pc == b'?' || pc.eq_ignore_ascii_case(&tb[j - 1]));
                }
                dp[0] = false;
            }
        }
        dp[tb.len()]
    }

    #[allow(clippy::too_many_lines)]
    pub(crate) fn handle_dos700_lfn(&mut self) {
        let ax = self.regs[0];
        let ds = self.sregs[Self::DS];
        let es = self.sregs[Self::ES];
        let dx = self.regs[2];
        let di = self.regs[7];

        if self.era != DosEra::Dos710 {
            self.regs[0] = 0x7100;
            self.set_cf(true);
            return;
        }

        match ax {
            0x71A0 => {
                for (i, &b) in b"FAT32".iter().enumerate() {
                    self.write_u8(es, di.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
                }
                self.write_u8(es, di.wrapping_add(5), 0);
                self.regs[3] = 0x4006;
                self.regs[1] = 255;
                self.regs[2] = 260;
                self.set_cf(false);
            }
            0x7139 => {
                let raw = self.read_asciiz(ds, dx);
                let key = self.norm_vfs_key(&raw);
                self.dirs.insert(key.clone());
                self.vfs_meta.insert(
                    key,
                    VfsMeta {
                        attr: 0x10,
                        lfn_name: raw.rsplit([BS, '/']).next().unwrap_or(&raw).to_owned(),
                        short_alias: String::from("LFNDIR~1"),
                    },
                );
                self.set_cf(false);
            }
            0x713A => {
                let key = self.norm_vfs_key(&self.read_asciiz(ds, dx));
                if self.dirs.remove(&key) {
                    self.vfs_meta.remove(&key);
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::PathNotFound.code();
                    self.set_cf(true);
                }
            }
            0x713B => {
                let raw = self.read_asciiz(ds, dx);
                let key = self.norm_vfs_key(&raw);
                if key.is_empty() || self.dirs.contains(&key) {
                    self.cwd = key;
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::PathNotFound.code();
                    self.set_cf(true);
                }
            }
            0x7147 => {
                let si = self.regs[6];
                let cwd_bytes = self.cwd.as_bytes().to_vec();
                for (i, &b) in cwd_bytes.iter().enumerate() {
                    self.write_u8(ds, si.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
                }
                self.write_u8(
                    ds,
                    si.wrapping_add(u16::try_from(cwd_bytes.len()).unwrap_or(0)),
                    0,
                );
                self.set_cf(false);
            }
            0x7141 => {
                let key = self.norm_vfs_key(&self.read_asciiz(ds, dx));
                if self.vfs_meta.get(&key).is_some_and(|m| (m.attr & 0x01) != 0) {
                    self.regs[0] = ErrCode::AccessDenied.code();
                    self.set_cf(true);
                } else if self.vfs.remove(&key).is_some() {
                    self.vfs_meta.remove(&key);
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::FileNotFound.code();
                    self.set_cf(true);
                }
            }
            0x7143 => {
                let key = self.norm_vfs_key(&self.read_asciiz(ds, dx));
                let bl = self.get_reg8(3);
                if bl == 1 {
                    if self.vfs.contains_key(&key) || self.dirs.contains(&key) {
                        self.vfs_meta.entry(key).or_default().attr = self.get_reg8(1);
                        self.set_cf(false);
                    } else {
                        self.regs[0] = ErrCode::FileNotFound.code();
                        self.set_cf(true);
                    }
                } else if self.dirs.contains(&key) {
                    self.regs[1] = 0x10;
                    self.set_cf(false);
                } else if self.vfs.contains_key(&key) {
                    self.regs[1] = u16::from(self.vfs_meta.get(&key).map_or(0x20, |m| m.attr));
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::FileNotFound.code();
                    self.set_cf(true);
                }
            }
            0x7156 => {
                let old_k = self.norm_vfs_key(&self.read_asciiz(ds, dx));
                let new_raw = self.read_asciiz(es, di);
                let new_k = self.norm_vfs_key(&new_raw);
                if let Some(bytes) = self.vfs.remove(&old_k) {
                    self.vfs.insert(new_k.clone(), bytes);
                    let mut meta = self.vfs_meta.remove(&old_k).unwrap_or_default();
                    new_raw
                        .rsplit([BS, '/'])
                        .next()
                        .unwrap_or(&new_raw)
                        .clone_into(&mut meta.lfn_name);
                    self.vfs_meta.insert(new_k, meta);
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::FileNotFound.code();
                    self.set_cf(true);
                }
            }
            0x7160 => {
                let src = self.read_asciiz(ds, self.regs[6]);
                let key = self.norm_vfs_key(&src);
                let clean = Self::strip_a_root(&key);
                let cl = self.get_reg8(1);
                let out = if cl == 1 {
                    let alias = self
                        .vfs_meta
                        .get(&key)
                        .filter(|m| !m.short_alias.is_empty())
                        .map_or_else(|| clean.to_owned(), |m| m.short_alias.clone());
                    format!("A:{BS}{alias}")
                } else if cl == 2 {
                    let lfn = self
                        .vfs_meta
                        .get(&key)
                        .filter(|m| !m.lfn_name.is_empty())
                        .map_or_else(|| clean.to_owned(), |m| m.lfn_name.clone());
                    format!("A:{BS}{lfn}")
                } else {
                    format!("A:{BS}{clean}")
                };
                for (i, b) in out.bytes().enumerate() {
                    self.write_u8(es, di.wrapping_add(u16::try_from(i).unwrap_or(0)), b);
                }
                self.write_u8(
                    es,
                    di.wrapping_add(u16::try_from(out.len()).unwrap_or(0)),
                    0,
                );
                self.set_cf(false);
            }
            0x716C => {
                let raw = self.read_asciiz(ds, self.regs[6]);
                if Self::is_nul_device(&raw) {
                    match self.alloc_file_handle(OpenFile {
                        path: String::from(NUL_SENTINEL),
                        pos: 0,
                    }) {
                        Ok(h) => {
                            self.regs[0] = h;
                            self.regs[1] = 1;
                            self.set_cf(false);
                        }
                        Err(e) => {
                            self.regs[0] = e.code();
                            self.set_cf(true);
                        }
                    }
                    return;
                }
                let key = self.norm_vfs_key(&raw);
                let action = self.regs[2];
                let exists = self.vfs.contains_key(&key);
                if !exists && (action & 0x10) == 0 {
                    self.regs[0] = ErrCode::FileNotFound.code();
                    self.set_cf(true);
                    return;
                }
                if exists
                    && self.vfs_meta.get(&key).is_some_and(|m| (m.attr & 0x01) != 0)
                    && (action & 0x02) != 0
                {
                    self.regs[0] = ErrCode::AccessDenied.code();
                    self.set_cf(true);
                    return;
                }
                if !exists || (action & 0x02) != 0 {
                    self.vfs.insert(key.clone(), Vec::new());
                    let leaf = raw.rsplit([BS, '/']).next().unwrap_or(&raw).to_owned();
                    let meta = self.vfs_meta.entry(key.clone()).or_default();
                    if meta.lfn_name.is_empty() {
                        meta.lfn_name = leaf;
                    }
                    if meta.short_alias.is_empty() {
                        meta.short_alias = String::from("LFNFIL~1.TXT");
                    }
                }
                match self.alloc_file_handle(OpenFile { path: key, pos: 0 }) {
                    Ok(h) => {
                        self.regs[0] = h;
                        self.regs[1] = if exists { 1 } else { 2 };
                        self.set_cf(false);
                    }
                    Err(e) => {
                        self.regs[0] = e.code();
                        self.set_cf(true);
                    }
                }
            }
            0x714E => {
                let raw_pat = self.read_asciiz(ds, dx);
                let norm_pat = self.norm_vfs_key(&raw_pat);
                let clean_pat = Self::strip_a_root(&norm_pat);
                let (dir_pfx, pat_leaf) = clean_pat
                    .rsplit_once(BS)
                    .map_or(("", clean_pat), |(d, f)| (d, f));
                let mut matches = Vec::new();
                for d in &self.dirs {
                    let (dp, dl) = d.rsplit_once(BS).map_or(("", d.as_str()), |(p, l)| (p, l));
                    if dp == dir_pfx && Self::simple_glob(pat_leaf, dl) {
                        let lfn = self
                            .vfs_meta
                            .get(d)
                            .filter(|m| !m.lfn_name.is_empty())
                            .map_or_else(|| dl.to_owned(), |m| m.lfn_name.clone());
                        let alias = self
                            .vfs_meta
                            .get(d)
                            .filter(|m| !m.short_alias.is_empty())
                            .map_or_else(|| String::from("LFNDIR~1"), |m| m.short_alias.clone());
                        matches.push((alias, lfn, 0x10, 0));
                    }
                }
                for (k, bytes) in &self.vfs {
                    let clean = Self::strip_a_root(k);
                    let attr = self.vfs_meta.get(k).map_or(0, |m| m.attr);
                    if (attr & 0x08) != 0 {
                        continue;
                    }
                    let (fp, fl) = clean
                        .rsplit_once(BS)
                        .map_or(("", clean), |(p, l)| (p, l));
                    if fp == dir_pfx && Self::simple_glob(pat_leaf, fl) {
                        let meta = self.vfs_meta.get(k);
                        let lfn = meta
                            .filter(|m| !m.lfn_name.is_empty())
                            .map_or_else(|| fl.to_owned(), |m| m.lfn_name.clone());
                        let alias = meta
                            .filter(|m| !m.short_alias.is_empty())
                            .map_or_else(|| fl.to_owned(), |m| m.short_alias.clone());
                        matches.push((alias, lfn, attr, u32::try_from(bytes.len()).unwrap_or(0)));
                    }
                }
                if matches.is_empty() {
                    self.regs[0] = ErrCode::FileNotFound.code();
                    self.set_cf(true);
                } else {
                    let hdl = self.next_lfn_handle;
                    self.next_lfn_handle += 1;
                    let first = matches[0].clone();
                    self.lfn_searches
                        .insert(hdl, LfnSearchState { matches, index: 1 });
                    self.open_lfn_handles.insert(hdl);
                    self.write_win32_find_dataa(es, di, &first.0, &first.1, first.2, first.3);
                    self.regs[0] = hdl;
                    self.set_cf(false);
                }
            }
            0x714F => {
                let hdl = self.regs[3];
                let next_item = self.lfn_searches.get_mut(&hdl).and_then(|st| {
                    let item = st.matches.get(st.index).cloned();
                    if item.is_some() {
                        st.index += 1;
                    }
                    item
                });
                if let Some((alias, lfn, attr, sz)) = next_item {
                    self.write_win32_find_dataa(es, di, &alias, &lfn, attr, sz);
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::NoMoreFiles.code();
                    self.set_cf(true);
                }
            }
            0x71A1 => {
                let hdl = self.regs[3];
                if self.open_lfn_handles.remove(&hdl) {
                    self.lfn_searches.remove(&hdl);
                    self.set_cf(false);
                } else {
                    self.regs[0] = ErrCode::InvalidHandle.code();
                    self.set_cf(true);
                }
            }
            _ => {
                self.regs[0] = 0x7100;
                self.set_cf(true);
            }
        }
    }

    fn write_win32_find_dataa(
        &mut self,
        seg: u16,
        off: u16,
        short_alias: &str,
        lfn_name: &str,
        attr: u8,
        size: u32,
    ) {
        for i in 0..318_u16 {
            self.write_u8(seg, off.wrapping_add(i), 0);
        }
        self.write_u8(seg, off, attr);
        let sb = size.to_le_bytes();
        self.write_u16(seg, off.wrapping_add(32), u16::from_le_bytes([sb[0], sb[1]]));
        self.write_u16(seg, off.wrapping_add(34), u16::from_le_bytes([sb[2], sb[3]]));
        for (i, b) in lfn_name.bytes().take(259).enumerate() {
            self.write_u8(seg, off.wrapping_add(44 + u16::try_from(i).unwrap_or(0)), b);
        }
        for (i, b) in short_alias.bytes().take(13).enumerate() {
            self.write_u8(seg, off.wrapping_add(304 + u16::try_from(i).unwrap_or(0)), b);
        }
    }

    pub(crate) fn handle_dos_exec_4b00(&mut self) {
        let raw_path = self.read_asciiz(self.sregs[Self::DS], self.regs[2]);
        let key = self.norm_vfs_key(&raw_path);
        let Some(image) = self.vfs.get(&key).cloned() else {
            self.regs[0] = ErrCode::FileNotFound.code();
            self.set_cf(true);
            return;
        };

        let es = self.sregs[Self::ES];
        let bx = self.regs[3];
        let env_seg = self.read_u16(es, bx);
        let tail_off = self.read_u16(es, bx.wrapping_add(2));
        let tail_seg = self.read_u16(es, bx.wrapping_add(4));

        let child_psp: u16 = 0x2000 + u16::try_from(self.exec_frames.len() * 0x1000).unwrap_or(0);
        for i in 0..256_u16 {
            self.write_u8(child_psp, i, 0);
        }
        self.write_u8(child_psp, 0x0000, 0xCD);
        self.write_u8(child_psp, 0x0001, 0x20);
        self.write_u16(child_psp, 0x0002, 0x9FFF);
        self.write_u16(child_psp, 0x002C, env_seg);
        for i in 0..128_u16 {
            let b = self.read_u8(tail_seg, tail_off.wrapping_add(i));
            self.write_u8(child_psp, 0x0080 + i, b);
        }

        self.exec_frames.push(SavedExecFrame {
            regs: self.regs,
            sregs: self.sregs,
            ip: self.ip,
            flags: self.flags,
            dta: self.dta,
        });

        let is_mz = image.len() >= 28
            && ((image[0] == b'M' && image[1] == b'Z') || (image[0] == b'Z' && image[1] == b'M'));
        if is_mz {
            let hdr_paras = u16::from_le_bytes([image[8], image[9]]);
            let hdr_bytes = usize::from(hdr_paras) * 16;
            let load_seg = child_psp.wrapping_add(0x0010);
            let body = image.get(hdr_bytes..).unwrap_or(&[]);
            let load_lin = Self::lin(load_seg, 0);
            self.mem[load_lin..(load_lin + body.len())].copy_from_slice(body);

            let reloc_cnt = usize::from(u16::from_le_bytes([image[6], image[7]]));
            let reloc_off = usize::from(u16::from_le_bytes([image[24], image[25]]));
            for r in 0..reloc_cnt {
                let ro = reloc_off + r * 4;
                if ro + 4 <= image.len() {
                    let item_off = u16::from_le_bytes([image[ro], image[ro + 1]]);
                    let item_seg = u16::from_le_bytes([image[ro + 2], image[ro + 3]])
                        .wrapping_add(load_seg);
                    let cur = self.read_u16(item_seg, item_off);
                    self.write_u16(item_seg, item_off, cur.wrapping_add(load_seg));
                }
            }

            let stack_seg = u16::from_le_bytes([image[14], image[15]]).wrapping_add(load_seg);
            let stack_ptr = u16::from_le_bytes([image[16], image[17]]);
            let entry_ip = u16::from_le_bytes([image[20], image[21]]);
            let entry_cs = u16::from_le_bytes([image[22], image[23]]).wrapping_add(load_seg);
            self.sregs = [child_psp, entry_cs, stack_seg, child_psp];
            self.ip = entry_ip;
            self.regs = [0, 0, 0, 0, stack_ptr, 0, 0, 0];
        } else {
            let load_lin = Self::lin(child_psp, 0x0100);
            self.mem[load_lin..(load_lin + image.len())].copy_from_slice(&image);
            self.sregs = [child_psp, child_psp, child_psp, child_psp];
            self.ip = 0x0100;
            self.regs = [0, 0, 0, 0, 0xFFFE, 0, 0, 0];
            self.write_u16(child_psp, 0xFFFE, 0x0000);
        }
        self.dta = (child_psp, 0x0080);
    }

    pub(crate) fn finish_child_exec(&mut self, child_code: u8) -> bool {
        let Some(frame) = self.exec_frames.pop() else {
            return false;
        };
        self.flush_ems_frame();
        if self.corrupt_high_ram_on_exec {
            let trn_seg = self
                .read_u16(Self::DEFAULT_PSP_SEG, 0x0002)
                .wrapping_sub(0x0A20);
            self.write_u8(trn_seg, 0x0004, 0xCC);
        }
        self.last_child_exit_code = child_code;
        self.regs = frame.regs;
        self.sregs = frame.sregs;
        self.ip = frame.ip;
        self.flags = frame.flags;
        self.dta = frame.dta;
        if self.era == DosEra::Dos200 {
            self.sregs[Self::SS] = 0xDEAD;
            self.regs[4] = 0xBEEF;
        }
        self.regs[0] = 0;
        self.set_cf(false);
        true
    }

    pub(crate) fn handle_ems_int67(&mut self) {
        if !self.ems_enabled {
            self.set_reg8(4, 0x84);
            return;
        }
        let ah = self.get_reg8(4);
        match ah {
            0x40 => self.set_reg8(4, 0x00),
            0x41 => {
                self.regs[3] = 0xE000;
                self.set_reg8(4, 0x00);
            }
            0x42 => {
                self.regs[3] = 64;
                self.regs[2] = 64;
                self.set_reg8(4, 0x00);
            }
            0x43 => {
                let pages = usize::from(self.regs[3].max(1));
                let hdl = self.next_ems_handle;
                self.next_ems_handle += 1;
                self.ems_store.insert(hdl, vec![0_u8; pages * 16_384]);
                self.regs[2] = hdl;
                self.set_reg8(4, 0x00);
            }
            0x44 => {
                if let Some((prev_hdl, prev_page)) = self.ems_mapped
                    && let Some(buf) = self.ems_store.get_mut(&prev_hdl)
                {
                    let frame_lin = Self::lin(0xE000, 0);
                    let start = prev_page * 16_384;
                    if start + 16_384 <= buf.len() {
                        buf[start..(start + 16_384)]
                            .copy_from_slice(&self.mem[frame_lin..(frame_lin + 16_384)]);
                    }
                }
                let hdl = self.regs[2];
                let log_page = usize::from(self.regs[3]);
                if let Some(buf) = self.ems_store.get(&hdl) {
                    let start = log_page * 16_384;
                    if start + 16_384 <= buf.len() {
                        let frame_lin = Self::lin(0xE000, 0);
                        self.mem[frame_lin..(frame_lin + 16_384)]
                            .copy_from_slice(&buf[start..(start + 16_384)]);
                        self.ems_mapped = Some((hdl, log_page));
                        self.set_reg8(4, 0x00);
                        return;
                    }
                }
                self.set_reg8(4, 0x83);
            }
            0x53 => {
                let ds = self.sregs[Self::DS];
                let si = self.regs[6];
                let mut name = [0_u8; 8];
                for (i, slot) in name.iter_mut().enumerate() {
                    *slot = self.read_u8(ds, si.wrapping_add(u16::try_from(i).unwrap_or(0)));
                }
                self.ems_names.insert(self.regs[2], name);
                self.set_reg8(4, 0x00);
            }
            _ => self.set_reg8(4, 0x84),
        }
    }

    pub(crate) fn flush_ems_frame(&mut self) {
        if let Some((hdl, page)) = self.ems_mapped
            && let Some(buf) = self.ems_store.get_mut(&hdl)
        {
            let frame_lin = Self::lin(0xE000, 0);
            let start = page * 16_384;
            if start + 16_384 <= buf.len() {
                buf[start..(start + 16_384)]
                    .copy_from_slice(&self.mem[frame_lin..(frame_lin + 16_384)]);
            }
        }
    }

    pub(crate) fn handle_xms_call(&mut self) {
        let ah = self.get_reg8(4);
        match ah {
            0x09 => {
                let kb = usize::from(self.regs[2].max(1));
                let hdl = self.next_xms_handle;
                self.next_xms_handle += 1;
                self.xms_store.insert(hdl, vec![0_u8; kb * 1024]);
                self.regs[2] = hdl;
                self.regs[0] = 1;
            }
            0x0B => {
                let ds = self.sregs[Self::DS];
                let si = self.regs[6];
                let len = usize::try_from(
                    (u32::from(self.read_u16(ds, si.wrapping_add(2))) << 16)
                        | u32::from(self.read_u16(ds, si)),
                )
                .unwrap_or(0);
                let src_h = self.read_u16(ds, si.wrapping_add(4));
                let src_lo = self.read_u16(ds, si.wrapping_add(6));
                let src_hi = self.read_u16(ds, si.wrapping_add(8));
                let dst_h = self.read_u16(ds, si.wrapping_add(10));
                let dst_lo = self.read_u16(ds, si.wrapping_add(12));
                let dst_hi = self.read_u16(ds, si.wrapping_add(14));

                let mut temp = vec![0_u8; len];
                if src_h == 0 {
                    let lin = Self::lin(src_hi, src_lo);
                    temp.copy_from_slice(&self.mem[lin..(lin + len)]);
                } else if let Some(b) = self.xms_store.get(&src_h) {
                    let off =
                        usize::try_from((u32::from(src_hi) << 16) | u32::from(src_lo)).unwrap_or(0);
                    temp.copy_from_slice(&b[off..(off + len)]);
                }

                if dst_h == 0 {
                    let lin = Self::lin(dst_hi, dst_lo);
                    self.mem[lin..(lin + len)].copy_from_slice(&temp);
                } else if let Some(b) = self.xms_store.get_mut(&dst_h) {
                    let off =
                        usize::try_from((u32::from(dst_hi) << 16) | u32::from(dst_lo)).unwrap_or(0);
                    b[off..(off + len)].copy_from_slice(&temp);
                }
                self.regs[0] = 1;
            }
            _ => self.regs[0] = 0,
        }
    }

    pub(crate) fn handle_bios_int16(&mut self) {
        let ah = self.get_reg8(4);
        let next_key = if let Some(&k) = self.bios_keys.get(self.bios_key_pos) {
            Some((k, true))
        } else if self.stdin_pos < self.stdin.len() {
            let b = self.stdin[self.stdin_pos];
            Some(((0, b), false))
        } else {
            None
        };

        if ah == 0x01 {
            if let Some(((scan, ascii), _)) = next_key {
                self.regs[0] = (u16::from(scan) << 8) | u16::from(ascii);
                self.flags &= !flags::ZF;
            } else {
                self.regs[0] = 0x001A;
                self.flags |= flags::ZF;
            }
        } else if let Some(((scan, ascii), from_bios)) = next_key {
            if from_bios {
                self.bios_key_pos += 1;
            } else {
                self.stdin_pos += 1;
            }
            self.regs[0] = (u16::from(scan) << 8) | u16::from(ascii);
        } else {
            self.regs[0] = 0x001A;
        }
    }

    pub(crate) fn refresh_rom_dbcs_table(&mut self) {
        let tbl_lin = Self::lin(0xF000, 0x0040);
        for i in 0..16 {
            self.mem[tbl_lin + i] = 0;
        }
        match self.active_cp {
            932 => {
                let bytes = [6, 0, 0x81, 0x9F, 0xE0, 0xFC, 0, 0];
                self.mem[tbl_lin..(tbl_lin + bytes.len())].copy_from_slice(&bytes);
            }
            936 | 949 | 950 => {
                let bytes = [4, 0, 0x81, 0xFE, 0, 0];
                self.mem[tbl_lin..(tbl_lin + bytes.len())].copy_from_slice(&bytes);
            }
            _ => {
                self.mem[tbl_lin] = 2;
                self.mem[tbl_lin + 1] = 0;
            }
        }
    }

    /// Returns `true` if `raw` names the DOS `NUL` character device (`NUL`, `NUL:`, `NUL.TXT`, `A:\...\NUL`).
    #[must_use]
    pub(crate) fn is_nul_device(raw: &str) -> bool {
        let leaf = raw.rsplit([BS, '/']).next().unwrap_or(raw).trim();
        let no_colon = leaf.strip_suffix(':').unwrap_or(leaf);
        let stem = no_colon
            .split_once('.')
            .map_or(no_colon, |(s, _)| s)
            .trim();
        stem.eq_ignore_ascii_case("NUL")
    }

    pub(crate) fn read_stdin_char_for_dos(&mut self) -> u8 {
        if let Some((path, pos)) = self.handles.get(&0).map(|of| (of.path.clone(), of.pos))
            && path != CON_IN_SENTINEL
        {
            if path == NUL_SENTINEL {
                return 0x1A;
            }
            let data = self.vfs.get(&path).map_or(&[][..], Vec::as_slice);
            if pos < data.len() {
                let b = data[pos];
                if let Some(of) = self.handles.get_mut(&0) {
                    of.pos += 1;
                }
                return b;
            }
            return 0x1A;
        }
        if self.stdin_pos < self.stdin.len() {
            let b = self.stdin[self.stdin_pos];
            self.stdin_pos += 1;
            b
        } else {
            0x1A
        }
    }

    pub(crate) fn write_stdout_bytes_for_dos(&mut self, bytes: &[u8]) {
        if let Some((path, pos)) = self.handles.get(&1).map(|of| (of.path.clone(), of.pos))
            && path != CON_OUT_SENTINEL
        {
            if path == NUL_SENTINEL {
                return;
            }
            let entry = self.vfs.entry(path).or_default();
            if pos + bytes.len() > entry.len() {
                entry.resize(pos + bytes.len(), 0);
            }
            entry[pos..(pos + bytes.len())].copy_from_slice(bytes);
            if let Some(of2) = self.handles.get_mut(&1) {
                of2.pos += bytes.len();
            }
        } else {
            self.stdout.extend_from_slice(bytes);
        }
    }

    /// Allocates the lowest free file handle (`>= 5`), enforcing the strict `FILES=8`
    /// (`<= 3` user handles above `0..4`) ceiling on MS-DOS 2.00!
    pub(crate) fn alloc_file_handle(&mut self, file: OpenFile) -> Result<u16, ErrCode> {
        let user_cap = if self.era == DosEra::Dos200 { 3 } else { 20 };
        let open_non_std = self.handles.keys().filter(|&&h| h >= 5).count();
        if open_non_std >= user_cap {
            return Err(ErrCode::TooManyOpenFiles);
        }
        let mut h = 5_u16;
        while self.handles.contains_key(&h) {
            h += 1;
        }
        self.handles.insert(h, file);
        self.max_concurrent_handles = self.max_concurrent_handles.max(open_non_std + 1);
        Ok(h)
    }

    /// Initializes an empty `BTreeSet`.
    #[must_use]
    pub(crate) const fn empty_set<T: Ord>() -> BTreeSet<T> {
        BTreeSet::new()
    }
}
