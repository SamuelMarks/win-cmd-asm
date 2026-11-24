//! Integration tests for `diff-fuzz` covering 7-target cross-ISA differential fuzzing
//! and the `diff-fuzz` CLI binary (`main.rs`).

use std::path::{Path, PathBuf};
use std::process::Command;
use wincmd_diag::ToolError;

fn repo_root() -> Result<PathBuf, ToolError> {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .map(Path::to_path_buf)
        .ok_or_else(|| ToolError::CliUsage {
            message: String::from("cannot resolve repo root"),
        })
}

#[test]
fn cross_isa_suite_and_cli_binary_smoke() -> Result<(), ToolError> {
    let root = repo_root()?;
    let build_dir = root.join("build");
    let suite = diff_fuzz::CrossIsaSuite::load_from_build_dir(&build_dir)?;
    diff_fuzz::run_cross_isa_fuzz(0x2026_1123, 6, &suite)?;

    assert!(diff_fuzz::CrossIsaSuite::load_from_build_dir(Path::new("/nonexistent")).is_err());

    let mut out = Vec::new();
    diff_fuzz::run_cli(
        [
            "diff-fuzz",
            "--cross-isa",
            "--seed",
            "0x1234",
            "--cases",
            "2",
            "--ref-cmd",
            "3",
            "--build-dir",
            &build_dir.to_string_lossy(),
        ],
        &mut out,
    )?;
    assert!(String::from_utf8_lossy(&out).contains("7-target cross-ISA"));

    let peer_com = if build_dir.join("fasm/dos16/WINCMD.COM").is_file() {
        build_dir.join("fasm/dos16/WINCMD.COM")
    } else {
        build_dir.join("jwasm/dos16/WINCMD.COM")
    };
    let bin = env!("CARGO_BIN_EXE_diff-fuzz");
    let ok = Command::new(bin)
        .args([
            "--era",
            "4.00",
            "--cases",
            "2",
            &build_dir.join("nasm/dos16/WINCMD.COM").to_string_lossy(),
            &peer_com.to_string_lossy(),
        ])
        .output()?;
    assert!(ok.status.success());

    let fail = Command::new(bin).arg("--unknown-flag").output()?;
    assert!(!fail.status.success());
    Ok(())
}
