//! Deterministic `SplitMix64` batch/`.com` workload generator and cross-assembler /
//! cross-era differential verification harness (`diff-fuzz`).

use emu_cov::{DosEra, DosVm, VmRunResult};
use std::fs;
use std::io::Write;
use std::path::PathBuf;
use wincmd_diag::ToolError;

/// Deterministic 64-bit `SplitMix64` PRNG requiring zero external crate dependencies.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    /// Creates a new [`SplitMix64`] generator from `seed`.
    #[must_use]
    pub const fn new(seed: u64) -> Self {
        Self { state: seed }
    }

    /// Advances the generator and returns the next 64-bit pseudo-random integer.
    pub const fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Picks a bounded `usize` in `0..upper_exclusive`.
    pub fn gen_range(&mut self, upper_exclusive: usize) -> usize {
        if upper_exclusive <= 1 {
            return 0;
        }
        let bound = u64::try_from(upper_exclusive).unwrap_or(u64::MAX);
        usize::try_from(self.next_u64() % bound).unwrap_or(0)
    }
}

/// Generates a deterministic command-line invocation tail from `rng` for differential fuzzing.
#[must_use]
pub fn generate_cmd_tail(rng: &mut SplitMix64) -> String {
    const TEMPLATES: &[&str] = &[
        " /C VER",
        " /C ECHO HELLO WORLD",
        " /C ECHO.",
        " /C SET FOO=BAR",
        " /C SET /A 12+34*2",
        " --dialect=nt /C FUZZ.BAT",
        " --dialect=nt /C IF EXIST NUL ECHO NUL_OK",
        " --dialect=nt /C COLOR 1E",
        " /C BREAK",
        " /C VERIFY",
        " /C DATE /T",
        " /C TIME /T",
        " /C REM COMMENT LINE",
        " /C IF 1==1 ECHO MATCH",
        " /C IF NOT 1==2 ECHO DIFF",
        " /?",
        "",
    ];
    let base = TEMPLATES[rng.gen_range(TEMPLATES.len())];
    if base == " /C SET /A 12+34*2" {
        let a = i32::try_from(rng.next_u64() % 400).unwrap_or(0) - 200;
        let b = i32::try_from(rng.next_u64() % 50).unwrap_or(0) + 1;
        let ops = ["+", "-", "*", "/", "%", "&", "^", "|"];
        let op = ops[rng.gen_range(ops.len())];
        format!(" /C SET /A ({a}){op}({b})")
    } else {
        base.to_owned()
    }
}

fn generate_fuzz_bat(rng: &mut SplitMix64, era: DosEra) -> Vec<u8> {
    let v1 = i32::try_from(rng.next_u64() % 100).unwrap_or(0);
    let v2 = i32::try_from(rng.next_u64() % 20).unwrap_or(0) + 1;
    let sh = rng.gen_range(8);
    let mut lines = vec![
        String::from("@ECHO OFF"),
        format!("SET /A X=({v1}+{v2})*3, Y=X>>{sh}"),
        String::from("ECHO V=%X%_%Y%"),
        String::from("ECHO DROP > NUL"),
    ];
    if era != DosEra::Dos125 {
        lines.push(String::from("SETLOCAL EnableDelayedExpansion"));
        lines.push(String::from("SET /A Y+=5"));
        lines.push(String::from("ECHO IN=!Y!"));
        lines.push(String::from("ENDLOCAL"));
        lines.push(String::from("ECHO OUT=%Y%"));
    }
    let mut raw = Vec::new();
    for ln in lines {
        raw.extend_from_slice(ln.as_bytes());
        raw.extend_from_slice(&[0x0D, 0x0A]);
    }
    raw
}

/// Runs `iterations` generated command-line inputs identically across multiple `.COM` builds
/// (e.g. `NASM` vs `FASM` vs `JWasm`) on [`DosVm`] and asserts identical [`VmRunResult`]s.
///
/// # Errors
///
/// Returns [`ToolError::DiffFuzz`] on any divergence in `exit_code`, `stdout`, or `stderr`.
pub fn run_differential_com_fuzz(
    seed: u64,
    iterations: usize,
    era: DosEra,
    binaries: &[(&str, &[u8])],
) -> Result<(), ToolError> {
    if binaries.len() < 2 {
        return Err(ToolError::CliUsage {
            message: "differential fuzzing requires at least 2 binary images".into(),
        });
    }

    let mut rng = SplitMix64::new(seed);
    for case_index in 0..iterations {
        let tail = generate_cmd_tail(&mut rng);
        let bat_body = generate_fuzz_bat(&mut rng, era);
        let mut baseline: Option<(&str, VmRunResult)> = None;

        for &(label, img) in binaries {
            let mut vm = DosVm::new(era);
            vm.vfs.insert(String::from("FUZZ.BAT"), bat_body.clone());
            vm.load_com(img, &tail, &[("COMSPEC", r"A:\WINCMD.COM"), ("PATH", "A:")])?;
            let res = vm.run(3_000_000)?;
            if let Some((base_lbl, ref base_res)) = baseline {
                if res.exit_code != base_res.exit_code
                    || res.stdout != base_res.stdout
                    || res.stderr != base_res.stderr
                {
                    return Err(ToolError::DiffFuzz {
                        seed,
                        case_index,
                        details: format!(
                            "tail `{tail}`: `{base_lbl}` (exit={}, out={:?}) != `{label}` (exit={}, out={:?})",
                            base_res.exit_code,
                            String::from_utf8_lossy(&base_res.stdout),
                            res.exit_code,
                            String::from_utf8_lossy(&res.stdout),
                        ),
                    });
                }
            } else {
                baseline = Some((label, res));
            }
        }
    }

    Ok(())
}

/// Executes the `diff-fuzz` CLI:
/// `diff-fuzz --seed <u64> --cases <N> [--era 4.00] <bin1.com> <bin2.com> [<bin3.com>...]`
///
/// # Errors
///
/// Returns [`ToolError`] on CLI errors, file I/O errors, or differential mismatches.
pub fn run_cli<I, S, W>(args: I, out: &mut W) -> Result<(), ToolError>
where
    I: IntoIterator<Item = S>,
    S: AsRef<str>,
    W: Write,
{
    let collected: Vec<String> = args.into_iter().map(|s| s.as_ref().to_owned()).collect();
    let mut seed = 0x2026_1120_u64;
    let mut cases = 100_usize;
    let mut era = DosEra::Dos400;
    let mut paths: Vec<PathBuf> = Vec::new();

    let mut idx = 1_usize;
    while idx < collected.len() {
        match collected[idx].as_str() {
            "--seed" => {
                let val = collected.get(idx + 1).ok_or_else(|| ToolError::CliUsage {
                    message: "missing value after `--seed`".into(),
                })?;
                seed = if let Some(h) = val.strip_prefix("0x") {
                    u64::from_str_radix(h, 16)?
                } else {
                    val.parse::<u64>()?
                };
                idx += 2;
            }
            "--cases" => {
                let val = collected.get(idx + 1).ok_or_else(|| ToolError::CliUsage {
                    message: "missing value after `--cases`".into(),
                })?;
                cases = val.parse::<usize>()?;
                idx += 2;
            }
            "--era" => {
                let val = collected.get(idx + 1).ok_or_else(|| ToolError::CliUsage {
                    message: "missing value after `--era`".into(),
                })?;
                era = DosEra::parse(val).ok_or_else(|| ToolError::CliUsage {
                    message: format!("unknown DOS era `{val}`"),
                })?;
                idx += 2;
            }
            other if other.starts_with("--") => {
                return Err(ToolError::CliUsage {
                    message: format!("unknown flag `{other}`"),
                });
            }
            pos => {
                paths.push(PathBuf::from(pos));
                idx += 1;
            }
        }
    }

    if paths.len() < 2 {
        return Err(ToolError::CliUsage {
            message: "expected at least 2 `.COM` binaries to compare".into(),
        });
    }

    let mut loaded: Vec<(String, Vec<u8>)> = Vec::with_capacity(paths.len());
    for p in &paths {
        loaded.push((p.to_string_lossy().into_owned(), fs::read(p)?));
    }
    let refs: Vec<(&str, &[u8])> = loaded
        .iter()
        .map(|(n, b)| (n.as_str(), b.as_slice()))
        .collect();

    run_differential_com_fuzz(seed, cases, era, &refs)?;
    writeln!(
        out,
        "diff-fuzz: {cases} differential cases passed across {} binaries (seed 0x{seed:X})",
        refs.len()
    )?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn splitmix_and_differential_runner_work_and_detect_divergence() -> Result<(), ToolError> {
        let mut rng = SplitMix64::new(42);
        assert_eq!(rng.gen_range(0), 0);
        assert_eq!(rng.gen_range(1), 0);
        for _ in 0..32 {
            let t = generate_cmd_tail(&mut rng);
            assert!(t.is_empty() || t.starts_with(' '));
        }

        let ok_com = [0xB8_u8, 0x00, 0x4C, 0xCD, 0x21];
        let bad_com = [0xB8_u8, 0x01, 0x4C, 0xCD, 0x21];

        assert!(run_differential_com_fuzz(1, 5, DosEra::Dos400, &[("only", &ok_com)]).is_err());
        run_differential_com_fuzz(
            1,
            25,
            DosEra::Dos400,
            &[("nasm", &ok_com), ("fasm", &ok_com), ("jwasm", &ok_com)],
        )?;
        assert!(
            run_differential_com_fuzz(1, 5, DosEra::Dos400, &[("nasm", &ok_com), ("bad", &bad_com)])
                .is_err()
        );

        let tmp = std::env::temp_dir().join("wincmd_diff_fuzz_cli_test");
        let _ = fs::remove_dir_all(&tmp);
        fs::create_dir_all(&tmp)?;
        let f1 = tmp.join("a.com");
        let f2 = tmp.join("b.com");
        fs::write(&f1, ok_com)?;
        fs::write(&f2, ok_com)?;

        let mut out = Vec::new();
        run_cli(
            [
                "diff-fuzz",
                "--seed",
                "0x1234",
                "--cases",
                "10",
                "--era",
                "2.00",
                &f1.to_string_lossy(),
                &f2.to_string_lossy(),
            ],
            &mut out,
        )?;
        run_cli(
            [
                "diff-fuzz",
                "--seed",
                "99",
                &f1.to_string_lossy(),
                &f2.to_string_lossy(),
            ],
            &mut out,
        )?;

        assert!(run_cli(["diff-fuzz"], &mut out).is_err());
        assert!(run_cli(["diff-fuzz", "--seed"], &mut out).is_err());
        assert!(run_cli(["diff-fuzz", "--cases"], &mut out).is_err());
        assert!(run_cli(["diff-fuzz", "--era"], &mut out).is_err());
        assert!(run_cli(["diff-fuzz", "--era", "bad"], &mut out).is_err());
        assert!(run_cli(["diff-fuzz", "--unknown"], &mut out).is_err());

        let _ = fs::remove_dir_all(&tmp);
        Ok(())
    }
}
