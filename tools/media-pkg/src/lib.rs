//! Reproducible release packaging and media mastering orchestrator (`media-pkg`):
//! - `160K`, `360K`, `720K`, and `1.44M` FAT12 DOS/Win16 floppy `.IMG` media;
//! - Universal Win9x / Windows NT–11 ISO-9660 `.ISO` and per-ISA `.ZIP` distributions;
//! - Standalone pre-lowered source `.tar.gz` tarballs for `NASM`, `FASM`, and `JWasm`
//!   (requiring zero Rust dependency to assemble).

pub mod archive;
pub mod fat12;
pub mod iso9660;

use crate::archive::{build_ustar, build_zip, gzip_stored, sha256_hex};
use crate::fat12::{FloppyGeom, build_fat12_floppy};
use crate::iso9660::build_iso9660;
use std::collections::BTreeMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use wincmd_diag::ToolError;

const DOS16_UTIL_COMS: [&str; 19] = [
    "MEM.COM",
    "ATTRIB.COM",
    "FIND.COM",
    "FINDSTR.COM",
    "SORT.COM",
    "MORE.COM",
    "FC.COM",
    "COMP.COM",
    "TREE.COM",
    "WHERE.COM",
    "XCOPY.COM",
    "REPLACE.COM",
    "ROBOCOPY.COM",
    "CHOICE.COM",
    "TIMEOUT.COM",
    "TRUENAME.COM",
    "REG.COM",
    "HOSTNAME.COM",
    "WHOAMI.COM",
];

const PE_UTIL_EXES: [&str; 19] = [
    "MEM.EXE",
    "ATTRIB.EXE",
    "FIND.EXE",
    "FINDSTR.EXE",
    "SORT.EXE",
    "MORE.EXE",
    "FC.EXE",
    "COMP.EXE",
    "TREE.EXE",
    "WHERE.EXE",
    "XCOPY.EXE",
    "REPLACE.EXE",
    "ROBOCOPY.EXE",
    "CHOICE.EXE",
    "TIMEOUT.EXE",
    "TRUENAME.EXE",
    "REG.EXE",
    "HOSTNAME.EXE",
    "WHOAMI.EXE",
];

/// Summary of mastered release artifacts and their deterministic `SHA-256` digests.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ReleaseManifest {
    /// Ordered `(filename, byte_size, sha256_hex)` entries written into `out_dir`.
    pub artifacts: Vec<(String, usize, String)>,
}

fn collect_tree_files(
    root: &Path,
    cur: &Path,
    out: &mut BTreeMap<String, Vec<u8>>,
) -> Result<(), ToolError> {
    if !cur.exists() {
        return Ok(());
    }
    let mut entries = Vec::new();
    for entry in fs::read_dir(cur)? {
        entries.push(entry?.path());
    }
    entries.sort();
    for p in entries {
        if p.is_dir() {
            collect_tree_files(root, &p, out)?;
        } else {
            let rel = p
                .strip_prefix(root)
                .map_or_else(|_| p.to_string_lossy().into_owned(), |s| s.to_string_lossy().into_owned())
                .replace(92 as char, "/");
            out.insert(rel, fs::read(&p)?);
        }
    }
    Ok(())
}

fn standalone_build_sh(tc: &str) -> String {
    match tc {
        "nasm" => String::from(
            "#!/bin/sh
set -eu
mkdir -p build/dos16 build/win16
nasm -Ox -w+all -w-reloc -w+error -I src/nasm/dos16/ -f bin src/nasm/dos16/wincmd.asm -o build/dos16/WINCMD.COM
nasm -Ox -w+all -w-reloc -w+error -I src/nasm/dos16/ -f bin src/nasm/dos16/wincmd1.asm -o build/dos16/WINCMD1.COM
nasm -Ox -w+all -w-reloc -w+error -f bin src/nasm/dos16/wincmd_m.asm -o build/dos16/WINCMD.MSG
nasm -Ox -w+all -w-reloc -w+error -f bin src/nasm/dos16/util/asmconv.asm -o build/dos16/ASMCONV.COM
nasm -Ox -w+all -w-reloc -w+error -f obj src/nasm/dos16/wincmdmz.asm -o build/dos16/wincmdmz.obj
nasm -Ox -w+all -w-reloc -w+error -I src/nasm/win16/ -f bin src/nasm/win16/w16_pif.asm -o build/win16/WINCMD.PIF
nasm -Ox -w+all -w-reloc -w+error -I src/nasm/win16/ -f bin src/nasm/win16/w16_main.asm -o build/win16/WINCMD16.EXE
nasm -Ox -w+all -w-reloc -w+error -I src/nasm/win16/ -f bin src/nasm/win16/w16_286.asm -o build/win16/WINCMD286.EXE
",
        ),
        "fasm" => String::from(
            "#!/bin/sh
set -eu
mkdir -p build/dos16 build/win16 build/x86_32 build/x86_64
fasm src/fasm/dos16/wincmd.asm build/dos16/WINCMD.COM >/dev/null
fasm src/fasm/dos16/wincmd1.asm build/dos16/WINCMD1.COM >/dev/null
fasm src/fasm/dos16/wincmd_m.asm build/dos16/WINCMD.MSG >/dev/null
fasm src/fasm/dos16/util/asmconv.asm build/dos16/ASMCONV.COM >/dev/null
fasm src/fasm/dos16/wincmdmz.asm build/dos16/WINCMDMZ.EXE >/dev/null
fasm src/fasm/win16/w16_pif.asm build/win16/WINCMD.PIF >/dev/null
fasm src/fasm/win16/w16_main.asm build/win16/WINCMD16.EXE >/dev/null
fasm src/fasm/win16/w16_286.asm build/win16/WINCMD286.EXE >/dev/null
fasm src/fasm-pe32/x86_32/wincmd32.asm build/x86_32/WINCMD32.EXE >/dev/null
fasm src/fasm/x86_64/wincmd64.asm build/x86_64/WINCMD64.EXE >/dev/null
",
        ),
        _ => String::from(
            "#!/bin/sh
set -eu
mkdir -p build/dos16 build/win16
jwasm -q -WX -W4 -Isrc/jwasm/dos16 -bin -Fobuild/dos16/WINCMD.COM src/jwasm/dos16/wincmd.asm
jwasm -q -WX -W4 -Isrc/jwasm/dos16 -bin -Fobuild/dos16/WINCMD1.COM src/jwasm/dos16/wincmd1.asm
jwasm -q -WX -W4 -bin -Fobuild/dos16/WINCMD.MSG src/jwasm/dos16/wincmd_m.asm
jwasm -q -WX -W4 -bin -Fobuild/dos16/ASMCONV.COM src/jwasm/dos16/util/asmconv.asm
jwasm -q -WX -W4 -mz -Fobuild/dos16/WINCMDMZ.EXE src/jwasm/dos16/wincmdmz.asm
jwasm -q -WX -W4 -Isrc/jwasm/win16 -bin -Fobuild/win16/WINCMD.PIF src/jwasm/win16/w16_pif.asm
jwasm -q -WX -W4 -Isrc/jwasm/win16 -bin -Fobuild/win16/WINCMD16.EXE src/jwasm/win16/w16_main.asm
jwasm -q -WX -W4 -Isrc/jwasm/win16 -bin -Fobuild/win16/WINCMD286.EXE src/jwasm/win16/w16_286.asm
",
        ),
    }
}

/// Builds a standalone zero-Rust source `.tar.gz` archive for `tc` (`"nasm"`, `"fasm"`, or `"jwasm"`).
///
/// # Errors
///
/// Returns [`ToolError`] if lowered source directories are missing or TAR packing fails.
pub fn build_standalone_source_tarball(
    dist_src_dir: &Path,
    repo_root: &Path,
    tc: &str,
) -> Result<Vec<u8>, ToolError> {
    let artifact = format!("wincmd-src-{tc}.tar.gz");
    let mut tree = BTreeMap::new();
    let subdirs: &[&str] = match tc {
        "nasm" => &["nasm"],
        "fasm" => &["fasm", "fasm-pe32", "fasmarm"],
        "jwasm" => &["jwasm"],
        other => {
            return Err(ToolError::MediaPackage {
                artifact,
                details: format!("unsupported standalone tarball assembler `{other}`"),
            });
        }
    };
    for &sub in subdirs {
        let dir = dist_src_dir.join(sub);
        let mut sub_map = BTreeMap::new();
        collect_tree_files(&dir, &dir, &mut sub_map)?;
        if sub_map.is_empty() && sub == tc {
            return Err(ToolError::MediaPackage {
                artifact,
                details: format!("missing lowered sources under `{}`", dir.display()),
            });
        }
        for (rel, bytes) in sub_map {
            tree.insert(format!("wincmd-src-{tc}/src/{sub}/{rel}"), bytes);
        }
    }

    let tc_inc_dir = repo_root.join("asm/tc");
    let mut tc_map = BTreeMap::new();
    collect_tree_files(&tc_inc_dir, &tc_inc_dir, &mut tc_map)?;
    for (rel, bytes) in tc_map {
        tree.insert(format!("wincmd-src-{tc}/tc/{rel}"), bytes);
    }

    let build_sh = standalone_build_sh(tc).into_bytes();
    let makefile = String::from("all:
	@sh ./build-standalone.sh
").into_bytes();
    let readme = format!(
        "# win-cmd-asm Standalone {tc} Source Distribution

Run `make` or `sh ./build-standalone.sh` with `{tc}` in PATH (zero Rust required).
"
    )
    .into_bytes();
    tree.insert(format!("wincmd-src-{tc}/build-standalone.sh"), build_sh);
    tree.insert(format!("wincmd-src-{tc}/Makefile"), makefile);
    tree.insert(format!("wincmd-src-{tc}/README.md"), readme);

    let borrowed: Vec<(&str, &[u8], bool)> = tree
        .iter()
        .map(|(k, v)| {
            let is_exec = std::path::Path::new(k.as_str())
                .extension()
                .is_some_and(|ext| ext.eq_ignore_ascii_case("sh"));
            (k.as_str(), v.as_slice(), is_exec)
        })
        .collect();
    let tar = build_ustar(&artifact, &borrowed)?;
    Ok(gzip_stored(&tar))
}

fn write_and_record(
    out_dir: &Path,
    name: &str,
    bytes: &[u8],
    records: &mut Vec<(String, usize, String)>,
) -> Result<(), ToolError> {
    fs::write(out_dir.join(name), bytes)?;
    records.push((name.to_owned(), bytes.len(), sha256_hex(bytes)));
    Ok(())
}

fn master_floppies(
    build_dir: &Path,
    out_dir: &Path,
    records: &mut Vec<(String, usize, String)>,
) -> Result<(), ToolError> {
    let dos_dir = build_dir.join("nasm/dos16");
    let fasm_dos = build_dir.join("fasm/dos16");
    let win16_dir = build_dir.join("nasm/win16");
    let x32_dir = build_dir.join("nasm/x86_32");

    let mut autoexec = Vec::from(&b"@ECHO OFF"[..]);
    autoexec.extend_from_slice(&[13, 10]);
    autoexec.extend_from_slice(b"VER");
    autoexec.extend_from_slice(&[13, 10]);
    let mut base_owned: Vec<(String, Vec<u8>)> = vec![
        (String::from("AUTOEXEC.BAT"), autoexec),
        (String::from("WINCMD.COM"), fs::read(dos_dir.join("WINCMD.COM"))?),
        (String::from("WINCMD1.COM"), fs::read(dos_dir.join("WINCMD1.COM"))?),
        (String::from("WINCMD.MSG"), fs::read(dos_dir.join("WINCMD.MSG"))?),
        (String::from("ASMCONV.COM"), fs::read(dos_dir.join("ASMCONV.COM"))?),
    ];
    for &u in &DOS16_UTIL_COMS {
        base_owned.push((u.to_owned(), fs::read(dos_dir.join(u))?));
    }

    // 1. 160K 1981 5.25" floppy (WINCMD + 19 .COM utilities)
    let f160_refs: Vec<(&str, &[u8])> = base_owned
        .iter()
        .map(|(n, b)| (n.as_str(), b.as_slice()))
        .collect();
    let img160 = build_fat12_floppy(
        "wincmd-dos-160k.img",
        FloppyGeom::F160K,
        b"WINCMD160K ",
        &f160_refs,
    )?;
    write_and_record(out_dir, "wincmd-dos-160k.img", &img160, records)?;

    // 2. 360K 5.25" floppy (+ WINCMDMZ.EXE)
    let mut f360_owned = base_owned;
    f360_owned.push((
        String::from("WINCMDMZ.EXE"),
        fs::read(fasm_dos.join("WINCMDMZ.EXE"))?,
    ));
    let f360_refs: Vec<(&str, &[u8])> = f360_owned
        .iter()
        .map(|(n, b)| (n.as_str(), b.as_slice()))
        .collect();
    let img360 = build_fat12_floppy(
        "wincmd-dos-360k.img",
        FloppyGeom::F360K,
        b"WINCMD360K ",
        &f360_refs,
    )?;
    write_and_record(out_dir, "wincmd-dos-360k.img", &img360, records)?;

    // 3. 720K 3.5" DD floppy (+ Win16 NE WINCMD16.EXE, WCMD286.EXE, WINCMD.PIF)
    let mut f720_owned = f360_owned;
    for &(fat_name, src_name) in &[
        ("WINCMD.PIF", "WINCMD.PIF"),
        ("WINCMD16.EXE", "WINCMD16.EXE"),
        ("WCMD286.EXE", "WINCMD286.EXE"),
    ] {
        f720_owned.push((fat_name.to_owned(), fs::read(win16_dir.join(src_name))?));
    }
    let f720_refs: Vec<(&str, &[u8])> = f720_owned
        .iter()
        .map(|(n, b)| (n.as_str(), b.as_slice()))
        .collect();
    let img720 = build_fat12_floppy(
        "wincmd-dos-win16-720k.img",
        FloppyGeom::F720K,
        b"WINCMD720K ",
        &f720_refs,
    )?;
    write_and_record(out_dir, "wincmd-dos-win16-720k.img", &img720, records)?;

    // 4. 1.44M 3.5" HD floppy (+ WINCMD32.EXE, WINCMD64.EXE, ARM shells)
    let mut f1440_owned = f720_owned;
    f1440_owned.push((
        String::from("WINCMD32.EXE"),
        fs::read(x32_dir.join("WINCMD32.EXE"))?,
    ));
    f1440_owned.push((
        String::from("WINCMD64.EXE"),
        fs::read(build_dir.join("nasm/x86_64/WINCMD64.EXE"))?,
    ));
    f1440_owned.push((
        String::from("WCMDARM4.EXE"),
        fs::read(build_dir.join("fasmarm/arm32/WINCMD_ARMV4.EXE"))?,
    ));
    f1440_owned.push((
        String::from("WCMDARM7.EXE"),
        fs::read(build_dir.join("fasmarm/arm32/WINCMD_ARM32.EXE"))?,
    ));
    f1440_owned.push((
        String::from("WCMDA64.EXE"),
        fs::read(build_dir.join("fasmarm/arm64/WINCMD_ARM64.EXE"))?,
    ));
    let f1440_refs: Vec<(&str, &[u8])> = f1440_owned
        .iter()
        .map(|(n, b)| (n.as_str(), b.as_slice()))
        .collect();
    let img1440 = build_fat12_floppy(
        "wincmd-full-1440k.img",
        FloppyGeom::F1440K,
        b"WINCMD1440K",
        &f1440_refs,
    )?;
    write_and_record(out_dir, "wincmd-full-1440k.img", &img1440, records)
}

fn master_zips_and_iso(
    repo_root: &Path,
    build_dir: &Path,
    out_dir: &Path,
    records: &mut Vec<(String, usize, String)>,
) -> Result<(), ToolError> {
    let mut iso_owned: Vec<(String, Vec<u8>)> = vec![
        (String::from("README.MD"), fs::read(repo_root.join("README.md"))?),
        (String::from("BUILD.BAT"), fs::read(repo_root.join("BUILD.BAT"))?),
        (String::from("BUILD.CMD"), fs::read(repo_root.join("BUILD.CMD"))?),
        (String::from("MAKE.BAT"), fs::read(repo_root.join("make.bat"))?),
    ];

    // DOS16 + Win16 ZIP
    let mut dos_zip: Vec<(String, Vec<u8>)> = Vec::new();
    for &core in &["WINCMD.COM", "WINCMD1.COM", "WINCMD.MSG", "ASMCONV.COM"] {
        let b = fs::read(build_dir.join("nasm/dos16").join(core))?;
        dos_zip.push((format!("dos16/{core}"), b.clone()));
        iso_owned.push((format!("DOS16/{core}"), b));
    }
    let mz_b = fs::read(build_dir.join("fasm/dos16/WINCMDMZ.EXE"))?;
    dos_zip.push((String::from("dos16/WINCMDMZ.EXE"), mz_b.clone()));
    iso_owned.push((String::from("DOS16/WINCMDMZ.EXE"), mz_b));
    for &u in &DOS16_UTIL_COMS {
        let b = fs::read(build_dir.join("nasm/dos16").join(u))?;
        dos_zip.push((format!("dos16/{u}"), b.clone()));
        iso_owned.push((format!("DOS16/{u}"), b));
    }
    for &w in &["WINCMD.PIF", "WINCMD16.EXE", "WINCMD286.EXE"] {
        let b = fs::read(build_dir.join("nasm/win16").join(w))?;
        dos_zip.push((format!("win16/{w}"), b.clone()));
        iso_owned.push((format!("WIN16/{w}"), b));
    }
    let dz_refs: Vec<(&str, &[u8])> = dos_zip.iter().map(|(k, v)| (k.as_str(), v.as_slice())).collect();
    write_and_record(out_dir, "wincmd-dos-win16.zip", &build_zip(&dz_refs), records)?;

    // Per-ISA PE32 / PE32+ ZIPs + ISO staging
    let pe_targets: &[(&str, &str, &str, &[&str])] = &[
        ("wincmd-win32-x86.zip", "nasm/x86_32", "WIN32", &["WINCMD32.EXE"]),
        ("wincmd-win64-x64.zip", "nasm/x86_64", "WIN64", &["WINCMD64.EXE"]),
        (
            "wincmd-win-arm32.zip",
            "fasmarm/arm32",
            "ARM32",
            &["WINCMD_ARMV4.EXE", "WINCMD_ARM32.EXE"],
        ),
        ("wincmd-win-arm64.zip", "fasmarm/arm64", "ARM64", &["WINCMD_ARM64.EXE"]),
    ];
    for &(zip_name, bsub, iso_dir, shells) in pe_targets {
        let mut z_owned: Vec<(String, Vec<u8>)> = Vec::new();
        for &sh in shells {
            let b = fs::read(build_dir.join(bsub).join(sh))?;
            z_owned.push((sh.to_owned(), b.clone()));
            iso_owned.push((format!("{iso_dir}/{sh}"), b));
        }
        for &u in &PE_UTIL_EXES {
            let b = fs::read(build_dir.join(bsub).join(u))?;
            z_owned.push((u.to_owned(), b.clone()));
            iso_owned.push((format!("{iso_dir}/{u}"), b));
        }
        let z_refs: Vec<(&str, &[u8])> = z_owned.iter().map(|(k, v)| (k.as_str(), v.as_slice())).collect();
        write_and_record(out_dir, zip_name, &build_zip(&z_refs), records)?;
    }

    let iso_refs: Vec<(&str, &[u8])> = iso_owned.iter().map(|(k, v)| (k.as_str(), v.as_slice())).collect();
    let iso_bytes = build_iso9660("wincmd-win-universal.iso", "WINCMD_ASM", &iso_refs)?;
    write_and_record(out_dir, "wincmd-win-universal.iso", &iso_bytes, records)
}

/// Masters all release media (`160K`/`360K`/`720K`/`1.44M` `.IMG`, `.ISO`, `.ZIP`s,
/// standalone `NASM`/`FASM`/`JWasm` `.tar.gz` source tarballs, and `SHA256SUMS.txt`).
///
/// # Errors
///
/// Returns [`ToolError`] if required build or lowered-source inputs are missing or malformed.
pub fn package_all(
    repo_root: &Path,
    build_dir: &Path,
    dist_src_dir: &Path,
    out_dir: &Path,
) -> Result<ReleaseManifest, ToolError> {
    fs::create_dir_all(out_dir)?;
    let mut records = Vec::new();

    master_floppies(build_dir, out_dir, &mut records)?;
    master_zips_and_iso(repo_root, build_dir, out_dir, &mut records)?;

    for tc in ["nasm", "fasm", "jwasm"] {
        let tgz = build_standalone_source_tarball(dist_src_dir, repo_root, tc)?;
        let fname = format!("wincmd-src-{tc}.tar.gz");
        write_and_record(out_dir, &fname, &tgz, &mut records)?;
    }

    let mut sums = String::new();
    for (name, _, hash) in &records {
        use core::fmt::Write as _;
        let _ = writeln!(sums, "{hash}  {name}");
    }
    fs::write(out_dir.join("SHA256SUMS.txt"), sums.as_bytes())?;
    Ok(ReleaseManifest { artifacts: records })
}

/// Executes the `media-pkg` command-line entrypoint:
/// `media-pkg [--root <DIR>] [--build-dir <DIR>] [--dist-src <DIR>] [--out-dir <DIR>]`
///
/// # Errors
///
/// Returns [`ToolError`] on invalid flags or media mastering errors.
pub fn run_cli<I, S, W>(args: I, out: &mut W) -> Result<(), ToolError>
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
    W: Write,
{
    let mut iter = args.into_iter().skip(1);
    let mut root = PathBuf::from(".");
    let mut build_dir = PathBuf::from("build");
    let mut dist_src = PathBuf::from("dist/src");
    let mut out_dir = PathBuf::from("dist/release");

    while let Some(arg) = iter.next() {
        match arg.as_ref() {
            "--root" => {
                let v = iter.next().ok_or_else(|| ToolError::CliUsage {
                    message: String::from("--root requires a path"),
                })?;
                root = PathBuf::from(v.as_ref());
            }
            "--build-dir" => {
                let v = iter.next().ok_or_else(|| ToolError::CliUsage {
                    message: String::from("--build-dir requires a path"),
                })?;
                build_dir = PathBuf::from(v.as_ref());
            }
            "--dist-src" => {
                let v = iter.next().ok_or_else(|| ToolError::CliUsage {
                    message: String::from("--dist-src requires a path"),
                })?;
                dist_src = PathBuf::from(v.as_ref());
            }
            "--out-dir" => {
                let v = iter.next().ok_or_else(|| ToolError::CliUsage {
                    message: String::from("--out-dir requires a path"),
                })?;
                out_dir = PathBuf::from(v.as_ref());
            }
            other => {
                return Err(ToolError::CliUsage {
                    message: format!("unknown flag `{other}`"),
                });
            }
        }
    }

    let manifest = package_all(&root, &build_dir, &dist_src, &out_dir)?;
    writeln!(
        out,
        "media-pkg: mastered {} reproducible artifacts in {}",
        manifest.artifacts.len(),
        out_dir.display()
    )?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::archive::{gunzip_stored, read_ustar_entries, read_zip_entries};
    use crate::fat12::{format_fcb_83, read_fat12_root_files};
    use crate::iso9660::read_iso9660_files;

    #[test]
    fn fat12_all_geometries_roundtrip_and_error_guards() -> Result<(), ToolError> {
        let big = vec![0x5Au8; 1700];
        let files: &[(&str, &[u8])] = &[
            ("AUTOEXEC.BAT", b"@ECHO OFF
"),
            ("EMPTY.TXT", b""),
            ("README", b"NO_EXT"),
            ("BIGDATA.BIN", &big),
        ];
        for &geom in FloppyGeom::ALL {
            let img = build_fat12_floppy("test.img", geom, b"TESTLABEL  ", files)?;
            assert_eq!(img.len(), geom.byte_capacity());
            let parsed = read_fat12_root_files("test.img", &img)?;
            assert_eq!(parsed.get("AUTOEXEC.BAT").map(Vec::as_slice), Some(&b"@ECHO OFF
"[..]));
            assert_eq!(parsed.get("EMPTY.TXT").map(Vec::as_slice), Some(&b""[..]));
            assert_eq!(parsed.get("README").map(Vec::as_slice), Some(&b"NO_EXT"[..]));
            assert_eq!(parsed.get("BIGDATA.BIN").map(Vec::as_slice), Some(big.as_slice()));
        }

        assert!(format_fcb_83("bad.img", "TOO_LONG_STEM.TXT").is_err());
        assert!(format_fcb_83("bad.img", "A.B.C").is_err());
        let many: Vec<(&str, &[u8])> = (0..70).map(|_| ("A.TXT", &b"x"[..])).collect();
        assert!(build_fat12_floppy("f160.img", FloppyGeom::F160K, b"VOL        ", &many).is_err());
        let huge = vec![0u8; 200_000];
        assert!(
            build_fat12_floppy(
                "f160.img",
                FloppyGeom::F160K,
                b"VOL        ",
                &[("HUGE.BIN", &huge)]
            )
            .is_err()
        );
        assert!(read_fat12_root_files("short.img", &[0u8; 16]).is_err());
        let mut trunc_meta = build_fat12_floppy("t.img", FloppyGeom::F160K, b"VOL        ", files)?;
        trunc_meta.truncate(1024);
        assert!(read_fat12_root_files("trunc.img", &trunc_meta).is_err());

        let mut trunc_data = build_fat12_floppy("t.img", FloppyGeom::F160K, b"VOL        ", files)?;
        trunc_data.truncate(9 * 512 + 64);
        assert!(read_fat12_root_files("trunc_d.img", &trunc_data).is_err());
        Ok(())
    }

    #[test]
    fn iso9660_roundtrip_and_validation() -> Result<(), ToolError> {
        let payload_a = vec![0x41u8; 3000];
        let mut owned_names = Vec::new();
        for idx in 0..50 {
            owned_names.push(format!("DOS/FILE_{idx:02}.BIN"));
        }
        let mut entries: Vec<(&str, &[u8])> = vec![
            ("README.MD", b"# Hello ISO"),
            ("WIN64/WINCMD64.EXE", &payload_a),
        ];
        for name in &owned_names {
            entries.push((name.as_str(), b"x"));
        }
        let iso1 = build_iso9660("test.iso", "WINCMD_ISO", &entries)?;
        let iso2 = build_iso9660("test.iso", "WINCMD_ISO", &entries)?;
        assert_eq!(sha256_hex(&iso1), sha256_hex(&iso2), "deterministic ISO");
        let parsed = read_iso9660_files("test.iso", &iso1)?;
        assert_eq!(parsed.get("README.MD").map(Vec::as_slice), Some(&b"# Hello ISO"[..]));
        assert_eq!(parsed.get("DOS/FILE_49.BIN").map(Vec::as_slice), Some(&b"x"[..]));
        assert_eq!(
            parsed.get("WIN64/WINCMD64.EXE").map(Vec::as_slice),
            Some(payload_a.as_slice())
        );
        assert!(build_iso9660("bad.iso", "V", &[("A/B/C.TXT", b"x")]).is_err());
        assert!(read_iso9660_files("bad.iso", &[0u8; 100]).is_err());
        Ok(())
    }

    #[test]
    fn zip_ustar_gzip_sha256_and_error_paths() -> Result<(), ToolError> {
        assert_eq!(
            sha256_hex(b"abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
        let z = build_zip(&[("a/hello.txt", b"hello"), ("b/empty.bin", b"")]);
        let zm = read_zip_entries("t.zip", &z)?;
        assert_eq!(zm.get("a/hello.txt").map(Vec::as_slice), Some(&b"hello"[..]));
        assert!(read_zip_entries("bad.zip", &[1, 2, 3, 4]).is_err());
        let mut bad_crc_zip = z.clone();
        bad_crc_zip[41] ^= 0xFF;
        assert!(read_zip_entries("crc.zip", &bad_crc_zip).is_err());
        assert!(read_zip_entries("trunc.zip", &z[..35]).is_err());

        let long_path = format!("{}/{}", "d".repeat(110), "file.asm");
        let multi_blk = vec![0xABu8; 70_000];
        let tar = build_ustar(
            "t.tar",
            &[
                ("short.sh", b"#!/bin/sh
", true),
                (&long_path, &multi_blk, false),
            ],
        )?;
        let gz = gzip_stored(&tar);
        let un_gz = gunzip_stored("t.tar.gz", &gz)?;
        assert_eq!(un_gz, tar);
        let tm = read_ustar_entries("t.tar", &un_gz)?;
        assert_eq!(tm.get(&long_path).map(Vec::as_slice), Some(multi_blk.as_slice()));

        assert_eq!(gunzip_stored("empty.gz", &gzip_stored(b""))?, b"");
        assert!(gunzip_stored("short.gz", &[0x1F, 0x8B]).is_err());
        assert!(gunzip_stored("trunc_hdr.gz", &[0x1F, 0x8B, 0x08, 0, 0, 0, 0, 0, 0, 0xFF, 1, 0, 0, 0, 0, 0, 0, 0]).is_err());
        assert!(gunzip_stored("trunc_body.gz", &gz[..50]).is_err());
        let mut bad_blk = gz.clone();
        bad_blk[13] ^= 0xFF;
        assert!(gunzip_stored("bad_nlen.gz", &bad_blk).is_err());
        let mut bad_gz_crc = gz;
        let gz_len = bad_gz_crc.len();
        bad_gz_crc[gz_len - 8] ^= 0xFF;
        assert!(gunzip_stored("bad_crc.gz", &bad_gz_crc).is_err());

        let over_ustar = "x".repeat(260);
        assert!(build_ustar("bad.tar", &[(&over_ustar, b"1", false)]).is_err());
        assert!(read_ustar_entries("trunc.tar", &tar[..520]).is_err());
        Ok(())
    }

    #[test]
    fn cli_flag_validation() {
        let mut sink = Vec::new();
        assert!(run_cli(["media-pkg", "--unknown"], &mut sink).is_err());
        assert!(run_cli(["media-pkg", "--root"], &mut sink).is_err());
        assert!(run_cli(["media-pkg", "--build-dir"], &mut sink).is_err());
        assert!(run_cli(["media-pkg", "--dist-src"], &mut sink).is_err());
        assert!(run_cli(["media-pkg", "--out-dir"], &mut sink).is_err());
        assert!(
            build_standalone_source_tarball(Path::new("/nonexistent"), Path::new("/nonexistent"), "badtc")
                .is_err()
        );
        assert!(
            build_standalone_source_tarball(Path::new("/nonexistent"), Path::new("/nonexistent"), "nasm")
                .is_err()
        );
        let rm = ReleaseManifest { artifacts: Vec::new() };
        assert_eq!(rm.clone(), rm);
        assert_ne!(format!("{rm:?}"), "");
    }
}
