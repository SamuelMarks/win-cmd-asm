//! Workspace-wide integration tests for `asm-conv` exercising full `asm/` lowering across
//! `NASM`, `FASM`, `FASM-PE32`, `JWasm`, `FASMARM`, `LLVM-MC`, and `ARMASM` plus CLI binary execution.

use std::fs;
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
fn lowers_full_repository_tree_and_exercises_cli_binary() -> Result<(), ToolError> {
    let root = repo_root()?;
    let tmp = std::env::temp_dir().join(format!("wincmd_asmconv_it_{}", std::process::id()));
    let _ = fs::remove_dir_all(&tmp);

    let mut out_buf = Vec::new();
    asm_conv::run_cli(
        [
            "asm-conv",
            "--in-dir",
            &root.join("asm").to_string_lossy(),
            "--out-dir",
            &tmp.to_string_lossy(),
        ],
        &mut out_buf,
    )?;
    assert!(String::from_utf8_lossy(&out_buf).contains("generated"));
    assert!(tmp.join("fasm/dos16/wincmdmz.asm").is_file());
    assert!(tmp.join("fasm-pe32/x86_32/wincmd32.asm").is_file());
    assert!(tmp.join("llvm-mc/arm64/wincmda64.asm").is_file());

    let bin = env!("CARGO_BIN_EXE_asm-conv");
    let out_sub = tmp.join("cli_out");
    let ok = Command::new(bin)
        .args([
            "--in-dir",
            &root.join("asm/win16").to_string_lossy(),
            "--out-dir",
            &out_sub.to_string_lossy(),
            "--target",
            "nasm",
        ])
        .output()?;
    assert!(ok.status.success());

    let fail = Command::new(bin).arg("--invalid-opt").output()?;
    assert!(!fail.status.success());

    let _ = fs::remove_dir_all(&tmp);
    Ok(())
}
