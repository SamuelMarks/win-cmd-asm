//! End-to-end integration tests for reproducible floppy `.IMG` (`160K`, `360K`,
//! `720K`, `1.44M`), ISO-9660 `.ISO`, `.ZIP`, and zero-Rust standalone source `.tar.gz`
//! distributions (`NASM`, `FASM`, `JWasm`).

use media_pkg::archive::{gunzip_stored, read_ustar_entries, read_zip_entries, sha256_hex};
use media_pkg::fat12::{FloppyGeom, read_fat12_root_files};
use media_pkg::iso9660::read_iso9660_files;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use wincmd_diag::ToolError;

fn repo_root() -> Result<PathBuf, ToolError> {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| ToolError::MediaPackage {
            artifact: String::from("workspace"),
            details: String::from("cannot resolve repo root"),
        })
}

fn verify_media_contents(root: &Path, out_a: &Path) -> Result<(), ToolError> {
    let expected_wincmd = fs::read(root.join("build/nasm/dos16/WINCMD.COM"))?;
    for (fname, geom, must_have) in [
        ("wincmd-dos-160k.img", FloppyGeom::F160K, "WHOAMI.COM"),
        ("wincmd-dos-360k.img", FloppyGeom::F360K, "WINCMDMZ.EXE"),
        (
            "wincmd-dos-win16-720k.img",
            FloppyGeom::F720K,
            "WCMD286.EXE",
        ),
        ("wincmd-full-1440k.img", FloppyGeom::F1440K, "WCMDA64.EXE"),
    ] {
        let raw = fs::read(out_a.join(fname))?;
        assert_eq!(
            raw.len(),
            geom.byte_capacity(),
            "{fname} exact floppy geometry"
        );
        let files = read_fat12_root_files(fname, &raw)?;
        assert_eq!(files.get("WINCMD.COM"), Some(&expected_wincmd));
        assert!(files.contains_key(must_have), "{fname} missing {must_have}");
    }

    let iso = fs::read(out_a.join("wincmd-win-universal.iso"))?;
    let iso_files = read_iso9660_files("wincmd-win-universal.iso", &iso)?;
    for req in [
        "README.MD",
        "DOS16/WINCMD.COM",
        "WIN16/WINCMD286.EXE",
        "WIN32/WINCMD32.EXE",
        "WIN64/WINCMD64.EXE",
        "ARM32/WINCMD_ARM32.EXE",
        "ARM64/WINCMD_ARM64.EXE",
    ] {
        assert!(iso_files.contains_key(req), "ISO missing {req}");
    }

    for zname in [
        "wincmd-dos-win16.zip",
        "wincmd-win32-x86.zip",
        "wincmd-win64-x64.zip",
        "wincmd-win-arm32.zip",
        "wincmd-win-arm64.zip",
    ] {
        let zb = fs::read(out_a.join(zname))?;
        let zm = read_zip_entries(zname, &zb)?;
        assert!(!zm.is_empty(), "{zname} non-empty");
    }
    Ok(())
}

fn verify_standalone_tarballs(root: &Path, out_a: &Path, scratch: &Path) -> Result<(), ToolError> {
    let expected_wincmd_nasm = fs::read(root.join("build/nasm/dos16/WINCMD.COM"))?;
    let tc_bin = std::env::temp_dir()
        .parent()
        .map_or_else(|| PathBuf::from("/tmp"), Path::to_path_buf);
    let _ = tc_bin;
    let extra_tc = PathBuf::from("/Users/samuel/.gemini/tmp/win-cmd-asm/toolchains/bin");
    let cur_path = std::env::var("PATH").unwrap_or_default();
    let full_path = if extra_tc.is_dir() {
        format!("{}:{cur_path}", extra_tc.display())
    } else {
        cur_path
    };

    for tc in ["nasm", "fasm", "jwasm"] {
        let tgz_name = format!("wincmd-src-{tc}.tar.gz");
        let tgz_bytes = fs::read(out_a.join(&tgz_name))?;
        let tar_bytes = gunzip_stored(&tgz_name, &tgz_bytes)?;
        let entries = read_ustar_entries(&tgz_name, &tar_bytes)?;
        let prefix = format!("wincmd-src-{tc}/");
        assert!(entries.contains_key(&format!("{prefix}build-standalone.sh")));

        let work = scratch.join(format!("extract_{tc}"));
        fs::create_dir_all(&work)?;
        for (rel, data) in &entries {
            let dst = work.join(rel);
            if let Some(parent) = dst.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(&dst, data)?;
        }

        let has_tc_sh = Command::new("sh")
            .arg("-c")
            .arg(format!("command -v {tc} >/dev/null 2>&1"))
            .env("PATH", &full_path)
            .status()
            .is_ok_and(|s| s.success());
        if has_tc_sh {
            let sub = work.join(format!("wincmd-src-{tc}"));
            let status = Command::new("sh")
                .arg("./build-standalone.sh")
                .current_dir(&sub)
                .env("PATH", &full_path)
                .status()?;
            assert!(
                status.success(),
                "standalone zero-Rust `{tc}` tarball build failed"
            );
            let built_com = fs::read(sub.join("build/dos16/WINCMD.COM"))?;
            assert_eq!(
                built_com.len(),
                expected_wincmd_nasm.len(),
                "{tc} standalone WINCMD.COM size parity"
            );
            assert!(sub.join("build/win16/WINCMD286.EXE").is_file());
        }
    }
    Ok(())
}

#[test]
fn m14_reproducible_release_media_and_standalone_tarballs() -> Result<(), ToolError> {
    let root = repo_root()?;
    let scratch = std::env::temp_dir().join(format!("wincmd_m14_{}", std::process::id()));
    let _ = fs::remove_dir_all(&scratch);
    let out_a = scratch.join("rel_a");
    let out_b = scratch.join("rel_b");

    // Run 1 via library API
    let mut cli_out = Vec::new();
    media_pkg::run_cli(
        [
            "media-pkg",
            "--root",
            &root.to_string_lossy(),
            "--build-dir",
            &root.join("build").to_string_lossy(),
            "--dist-src",
            &root.join("dist/src").to_string_lossy(),
            "--out-dir",
            &out_a.to_string_lossy(),
        ],
        &mut cli_out,
    )?;
    assert!(String::from_utf8_lossy(&cli_out).contains("mastered 13 reproducible artifacts"));

    // Run 2 via compiled CLI binary (`main.rs`) to verify bit-for-bit reproducibility!
    let bin = env!("CARGO_BIN_EXE_media-pkg");
    let bad_status = Command::new(bin).arg("--invalid-flag").output()?;
    assert!(
        !bad_status.status.success(),
        "media-pkg CLI invalid flag exits non-zero"
    );

    let status = Command::new(bin)
        .args([
            "--root",
            &root.to_string_lossy(),
            "--build-dir",
            &root.join("build").to_string_lossy(),
            "--dist-src",
            &root.join("dist/src").to_string_lossy(),
            "--out-dir",
            &out_b.to_string_lossy(),
        ])
        .status()?;
    assert!(status.success(), "media-pkg CLI binary exit 0");

    let sums_a = fs::read_to_string(out_a.join("SHA256SUMS.txt"))?;
    let sums_b = fs::read_to_string(out_b.join("SHA256SUMS.txt"))?;
    assert_eq!(
        sums_a, sums_b,
        "100% deterministic SHA256SUMS.txt across runs"
    );
    assert_eq!(sums_a.lines().count(), 13);

    for line in sums_a.lines() {
        let (digest, fname) = line.split_once("  ").unwrap_or(("", ""));
        let actual = sha256_hex(&fs::read(out_a.join(fname))?);
        assert_eq!(digest, actual, "SHA-256 verified for {fname}");
    }

    verify_media_contents(&root, &out_a)?;
    verify_standalone_tarballs(&root, &out_a, &scratch)?;

    let _ = fs::remove_dir_all(&scratch);
    Ok(())
}
