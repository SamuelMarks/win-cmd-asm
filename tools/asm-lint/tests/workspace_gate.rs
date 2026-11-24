//! Workspace-wide integration tests for `asm-lint` covering `lint_directory("asm")`,
//! 3-assembler instruction equivalence (`x86-16`, `IA-32`, `x86-64` `x64_equiv`),
//! binary ISA purity (`8086`, `286`, `386`, `x64`, `armv4`, `armv7`, `arm64`),
//! FAT 8.3 path enforcement, and the `asm-lint` CLI binary.

use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use wincmd_diag::{CpuIsa, ToolError};

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
fn lint_entire_asm_tree_and_fat83_rules() -> Result<(), ToolError> {
    let root = repo_root()?;
    let count = asm_lint::lint_directory(&root.join("asm"))?;
    assert!(
        count >= 239,
        "expected >= 239 canonical assembly modules, got {count}"
    );

    asm_lint::verify_fat83_rel_path("dos16/util/robocopy.asm")?;
    asm_lint::verify_fat83_rel_path("win16/w16_pif.asm")?;
    asm_lint::verify_fat83_rel_path("x86_64/very_long_modern_filename.asm")?;
    assert!(asm_lint::verify_fat83_rel_path("win16/wincmd_pif.asm").is_err());
    assert!(asm_lint::verify_fat83_rel_path("dos16/a/b/c.asm").is_err());
    Ok(())
}

#[test]
fn verify_equivalence_purity_budgets_and_cli_binary() -> Result<(), ToolError> {
    let root = repo_root()?;
    let b = root.join("build");

    // 3-assembler equivalence across 16-bit COM, MZ, NE, 32-bit PE32, and 64-bit PE32+ (`x64_equiv`)
    let groups: &[(&str, &[&str])] = &[
        (
            "WINCMD.COM",
            &[
                "nasm/dos16/WINCMD.COM",
                "fasm/dos16/WINCMD.COM",
                "jwasm/dos16/WINCMD.COM",
            ],
        ),
        (
            "WINCMDMZ.EXE",
            &["fasm/dos16/WINCMDMZ.EXE", "jwasm/dos16/WINCMDMZ.EXE"],
        ),
        (
            "WINCMD16.EXE",
            &[
                "nasm/win16/WINCMD16.EXE",
                "fasm/win16/WINCMD16.EXE",
                "jwasm/win16/WINCMD16.EXE",
            ],
        ),
        (
            "WINCMD32.EXE",
            &[
                "nasm/x86_32/WINCMD32.EXE",
                "fasm/x86_32/WINCMD32.EXE",
                "jwasm/x86_32/WINCMD32.EXE",
            ],
        ),
        (
            "WINCMD64.EXE",
            &[
                "nasm/x86_64/WINCMD64.EXE",
                "fasm/x86_64/WINCMD64.EXE",
                "jwasm/x86_64/WINCMD64.EXE",
            ],
        ),
    ];
    let mut equiv_checked = 0usize;
    for &(label, rels) in groups {
        let mut loaded = Vec::with_capacity(rels.len());
        for &r in rels {
            let p = b.join(r);
            if p.is_file() {
                loaded.push((r, fs::read(p)?));
            }
        }
        assert!(
            !loaded.is_empty(),
            "at least one binary must be built for `{label}`"
        );
        if loaded.len() >= 2 {
            equiv_checked += 1;
            let refs: Vec<(&str, &[u8])> = loaded.iter().map(|(n, v)| (*n, v.as_slice())).collect();
            asm_lint::verify_binary_equivalence(label, &refs)?;
        }
    }
    assert!(
        equiv_checked >= 4,
        "expected >= 4 multi-assembler equivalence groups, got {equiv_checked}"
    );
    verify_purity_baselines(&b)?;

    let bin = env!("CARGO_BIN_EXE_asm-lint");
    let ok = Command::new(bin)
        .args([
            "--budget",
            &b.join("nasm/dos16/WINCMD.COM").to_string_lossy(),
            "49152",
        ])
        .output()?;
    assert!(ok.status.success());

    let fail = Command::new(bin).arg("--unknown-flag").output()?;
    assert!(!fail.status.success());
    Ok(())
}

fn verify_purity_baselines(b: &Path) -> Result<(), ToolError> {
    let mut purity_checked = 0usize;
    for (isa, rel) in [
        (CpuIsa::X86_8086, "nasm/win16/WINCMD16.EXE"),
        (CpuIsa::X86_8086, "fasm/dos16/WINCMDMZ.EXE"),
        (CpuIsa::X86_8086, "jwasm/dos16/WINCMDMZ.EXE"),
        (CpuIsa::X86_286, "nasm/win16/WINCMD286.EXE"),
        (CpuIsa::Ia32, "nasm/x86_32/WINCMD32.EXE"),
        (CpuIsa::Ia32, "fasm-pe32/x86_32/WINCMD32.EXE"),
        (CpuIsa::X86_64, "nasm/x86_64/WINCMD64.EXE"),
        (CpuIsa::X86_64, "fasm/x86_64/WINCMD64.EXE"),
        (CpuIsa::ArmV4, "fasmarm/arm32/WINCMD_ARMV4.EXE"),
        (CpuIsa::ArmV7, "fasmarm/arm32/WINCMD_ARM32.EXE"),
        (CpuIsa::ArmV7, "llvm/arm32/WINCMD_ARM32.EXE"),
        (CpuIsa::Arm64, "fasmarm/arm64/WINCMD_ARM64.EXE"),
        (CpuIsa::Arm64, "llvm/arm64/WINCMD_ARM64.EXE"),
    ] {
        let p = b.join(rel);
        if !p.is_file() {
            continue;
        }
        purity_checked += 1;
        let bytes = fs::read(p)?;
        asm_lint::verify_binary_purity(rel, isa, &bytes)?;
    }
    assert!(
        purity_checked >= 7,
        "expected >= 7 compiled binary purity checks, got {purity_checked}"
    );
    Ok(())
}
