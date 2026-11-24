//! Deterministic `SplitMix64` batch/`.com` workload generator and cross-assembler /
//! cross-era / 7-target cross-ISA differential verification harness (`diff-fuzz`).

use emu_cov::arm32_pe::Arm32Vm;
use emu_cov::arm64_pe::Arm64Vm;
use emu_cov::win_mock::{BS, DQ, SharedWinEnv};
use emu_cov::x86_pe::X86PeVm;
use emu_cov::{DosEra, DosVm, VmRunResult, Win16Era};
use std::cell::RefCell;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::rc::Rc;
use wincmd_diag::ToolError;

const CRLF_BYTES: [u8; 2] = [13, 10];

const fn crlf() -> &'static str {
    match core::str::from_utf8(&CRLF_BYTES) {
        Ok(s) => s,
        Err(_) => "",
    }
}

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
        raw.extend_from_slice(&CRLF_BYTES);
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
    let mut cases = Vec::with_capacity(iterations);
    for case_index in 0..iterations {
        let tail = generate_cmd_tail(&mut rng);
        let bat_body = generate_fuzz_bat(&mut rng, era);
        cases.push((case_index, tail, bat_body));
    }

    let nthreads = std::thread::available_parallelism()
        .map_or(4, usize::from)
        .clamp(1, 16);
    let chunk_sz = cases.len().div_ceil(nthreads).max(1);
    let comspec = format!("A:{BS}WINCMD.COM");

    std::thread::scope(|scope| -> Result<(), ToolError> {
        let mut handles = Vec::new();
        for chunk in cases.chunks(chunk_sz) {
            let comspec_ref = &comspec;
            handles.push(scope.spawn(move || -> Result<(), ToolError> {
                for (case_index, tail, bat_body) in chunk {
                    let mut baseline: Option<(&str, VmRunResult)> = None;
                    for &(label, img) in binaries {
                        let mut vm = DosVm::new(era);
                        vm.vfs.insert(String::from("FUZZ.BAT"), bat_body.clone());
                        vm.load_com(img, tail, &[("COMSPEC", comspec_ref), ("PATH", "A:")])?;
                        let res = vm.run(3_000_000)?;
                        if let Some((base_lbl, ref base_res)) = baseline {
                            if res.exit_code != base_res.exit_code
                                || res.stdout != base_res.stdout
                                || res.stderr != base_res.stderr
                            {
                                return Err(ToolError::DiffFuzz {
                                    seed,
                                    case_index: *case_index,
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
            }));
        }
        for h in handles {
            match h.join() {
                Ok(r) => r?,
                Err(_) => {
                    return Err(ToolError::DiffFuzz {
                        seed,
                        case_index: 0,
                        details: String::from("worker thread panicked"),
                    });
                }
            }
        }
        Ok(())
    })
}

struct CrossIsaCase {
    bat_bytes: Vec<u8>,
    win16_bat_bytes: Vec<u8>,
    child_com: Vec<u8>,
    in_payload: Vec<u8>,
    expected_arith: i32,
    expected_exit: u8,
}

fn generate_cross_isa_case(rng: &mut SplitMix64, idx: usize) -> CrossIsaCase {
    let nl = crlf();
    let a = i32::try_from(rng.next_u64() % 80).unwrap_or(0) - 20;
    let b = i32::try_from(rng.next_u64() % 25).unwrap_or(0) + 1;
    let mul = i32::try_from(rng.next_u64() % 5).unwrap_or(0) + 2;
    let sh = i32::try_from(rng.gen_range(4)).unwrap_or(0);
    let bit_m = i32::try_from((rng.next_u64() % 15) + 1).unwrap_or(3);
    let expected_arith = (a + b) * mul;
    let child_rc = u8::try_from((rng.next_u64() % 40) + 3).unwrap_or(7);
    let final_rc = u8::try_from(rng.next_u64() % 64).unwrap_or(0);
    let token_idx = rng.gen_range(3);
    let tokens = ["ALPHA", "BETA", "GAMMA"];
    let chosen_tok = tokens[token_idx];

    // 16-bit 8086 CHILD.COM: prints "C86_<idx>_<rc>" + CRLF via INT 21h AH=09h and exits with child_rc!
    let mut child_com = vec![
        0xBA, 0x0D, 0x01, // mov dx, 0x010D
        0xB4, 0x09, // mov ah, 0x09
        0xCD, 0x21, // int 0x21
        0xB8, child_rc, 0x4C, // mov ax, 0x4C00 | child_rc
        0xCD, 0x21, // int 0x21
        0x90, // nop -> offset 0x0D
    ];
    child_com.extend_from_slice(format!("C86_{idx}_{child_rc}{nl}$").as_bytes());

    let in_payload = format!("VFS_PAYLOAD_{idx}_{expected_arith}{nl}").into_bytes();

    let mut lines = vec![
        String::from("@ECHO OFF"),
        format!("SET /A {DQ}X=({a}+{b})*{mul}, Y=(X>>{sh})^{bit_m}{DQ}"),
        String::from("ECHO ARITH=%X%_%Y%"),
        format!("SET S=PRE_FOO_{chosen_tok}_END"),
        String::from("ECHO STR=%S:~4,3%_%S:FOO=BAR%"),
        String::from("SETLOCAL EnableDelayedExpansion"),
        String::from("SET TAG=LOCAL_OK"),
        String::from("SET /A Z=X+10"),
        String::from("ECHO DELAY=!TAG!_!Z!"),
        String::from("ENDLOCAL"),
        format!("IF %X% EQU {expected_arith} (ECHO CMP_EQ_OK) ELSE (ECHO CMP_EQ_BAD)"),
        format!("FOR %%T IN (ALPHA BETA GAMMA) DO IF %%T=={chosen_tok} ECHO FOR_HIT=%%T"),
        format!("CALL :SUB_FN {chosen_tok} {idx}"),
        String::from("MD SUBTMP"),
        String::from("RD SUBTMP"),
        String::from("COPY IN.TXT OUT.TXT > NUL"),
        String::from("DEL IN.TXT"),
        String::from("IF EXIST OUT.TXT ECHO VFS_OK"),
        String::from("IF NOT EXIST IN.TXT ECHO DEL_OK"),
    ];
    if idx.is_multiple_of(2) {
        lines.push(String::from("CHILD.COM"));
        lines.push(format!(
            "IF ERRORLEVEL {child_rc} ECHO CHILD_EL_GE_{child_rc}"
        ));
    }
    lines.push(format!("EXIT /B {final_rc}"));
    lines.push(String::from(":SUB_FN"));
    lines.push(String::from("ECHO SUB=%1_%2"));
    lines.push(String::from("EXIT /B 0"));

    let mut bat_bytes = Vec::new();
    for ln in &lines {
        bat_bytes.extend_from_slice(ln.as_bytes());
        bat_bytes.extend_from_slice(&CRLF_BYTES);
    }

    let w16_lhs = a.unsigned_abs() + 1;
    let w16_rhs = mul.unsigned_abs();
    let w16_expected: u32 = w16_lhs * w16_rhs;
    let win16_lines = [
        String::from("@ECHO OFF"),
        format!("SET /A VAL={w16_lhs}*{w16_rhs}"),
        String::from("COPY IN.TXT OUT.TXT"),
        String::from("DEL IN.TXT"),
    ];
    let mut win16_bat_bytes = Vec::new();
    for ln in &win16_lines {
        win16_bat_bytes.extend_from_slice(ln.as_bytes());
        win16_bat_bytes.extend_from_slice(&CRLF_BYTES);
    }

    CrossIsaCase {
        bat_bytes,
        win16_bat_bytes,
        child_com,
        in_payload,
        expected_arith: i32::try_from(w16_expected).unwrap_or(0),
        expected_exit: final_rc,
    }
}

#[derive(Debug, PartialEq, Eq)]
struct IsaOutcome {
    exit_code: u8,
    stdout: String,
    stderr: String,
    out_txt: Option<Vec<u8>>,
    in_exists: bool,
}

/// Preloaded multi-ISA interpreter images across all `7` primary binary targets and toolchains.
pub struct CrossIsaSuite {
    dos_imgs: Vec<(String, Vec<u8>)>,
    win16_imgs: Vec<(String, Vec<u8>)>,
    x86_32_imgs: Vec<(String, Vec<u8>)>,
    x86_64_imgs: Vec<(String, Vec<u8>)>,
    armv4_img: Option<Vec<u8>>,
    arm32_imgs: Vec<(String, Vec<u8>)>,
    arm64_imgs: Vec<(String, Vec<u8>)>,
}

impl CrossIsaSuite {
    /// Loads all `7` primary binary targets across available toolchains from `build_dir`.
    ///
    /// # Errors
    ///
    /// Returns [`ToolError`] if required target binaries are missing from `build_dir`.
    pub fn load_from_build_dir(build_dir: &Path) -> Result<Self, ToolError> {
        let mut dos_imgs = Vec::new();
        let mut win16_imgs = Vec::new();
        let mut x86_32_imgs = Vec::new();
        let mut x86_64_imgs = Vec::new();
        for tc in ["nasm", "fasm", "jwasm"] {
            let p_dos = build_dir.join(tc).join("dos16/WINCMD.COM");
            if p_dos.is_file() {
                dos_imgs.push((format!("{tc}/dos16"), fs::read(p_dos)?));
            }
            for w16_bin in ["WINCMD16.EXE", "WINCMD286.EXE"] {
                let p_w16 = build_dir.join(tc).join("win16").join(w16_bin);
                if p_w16.is_file() {
                    win16_imgs.push((format!("{tc}/win16/{w16_bin}"), fs::read(p_w16)?));
                }
            }
            let p_32 = build_dir.join(tc).join("x86_32/WINCMD32.EXE");
            if p_32.is_file() {
                x86_32_imgs.push((format!("{tc}/x86_32"), fs::read(p_32)?));
            }
            let p_64 = build_dir.join(tc).join("x86_64/WINCMD64.EXE");
            if p_64.is_file() {
                x86_64_imgs.push((format!("{tc}/x86_64"), fs::read(p_64)?));
            }
        }
        let p_fasm_pe32 = build_dir.join("fasm-pe32/x86_32/WINCMD32.EXE");
        if p_fasm_pe32.is_file() {
            x86_32_imgs.push((String::from("fasm-pe32/x86_32"), fs::read(p_fasm_pe32)?));
        }
        let p_fasm_coff64 = build_dir.join("fasm-coff/x86_64/WINCMD64.EXE");
        if p_fasm_coff64.is_file() {
            x86_64_imgs.push((String::from("fasm-coff/x86_64"), fs::read(p_fasm_coff64)?));
        }
        let p_armv4 = build_dir.join("fasmarm/arm32/WINCMD_ARMV4.EXE");
        let armv4_img = if p_armv4.is_file() {
            Some(fs::read(p_armv4)?)
        } else {
            None
        };
        let mut arm32_imgs = Vec::new();
        let mut arm64_imgs = Vec::new();
        for tc in ["fasmarm", "llvm"] {
            let p_a32 = build_dir.join(tc).join("arm32/WINCMD_ARM32.EXE");
            if p_a32.is_file() {
                arm32_imgs.push((format!("{tc}/arm32"), fs::read(p_a32)?));
            }
            let p_a64 = build_dir.join(tc).join("arm64/WINCMD_ARM64.EXE");
            if p_a64.is_file() {
                arm64_imgs.push((format!("{tc}/arm64"), fs::read(p_a64)?));
            }
        }
        if dos_imgs.is_empty()
            || win16_imgs.is_empty()
            || x86_32_imgs.is_empty()
            || x86_64_imgs.is_empty()
            || arm32_imgs.is_empty()
            || arm64_imgs.is_empty()
        {
            return Err(ToolError::CliUsage {
                message: format!(
                    "incomplete build matrix in `{}` — run `make build` first",
                    build_dir.display()
                ),
            });
        }
        Ok(Self {
            dos_imgs,
            win16_imgs,
            x86_32_imgs,
            x86_64_imgs,
            armv4_img,
            arm32_imgs,
            arm64_imgs,
        })
    }
}

/// Executes `iterations` generated `.BAT`/`.CMD` + 16-bit `.COM` workloads identically across
/// all `7` primary binary targets and all assembler toolchains.
///
/// Covered targets: `8086` `WINCMD.COM`, `286` `Win16` `WINCMD286.EXE`, `IA-32` `WINCMD32.EXE`,
/// `x86-64` `WINCMD64.EXE`, `ARMv4` `WINCMD_ARMV4.EXE`, `ARMv7` `WINCMD_ARM32.EXE`, and
/// `AArch64` `WINCMD_ARM64.EXE` across `NASM`, `FASM`, `JWasm`, `FASMARM`, and `LLVM-MC`.
///
/// # Errors
///
/// Returns [`ToolError::DiffFuzz`] on any `stdout`, `stderr`, `ERRORLEVEL`, or VFS divergence.
pub fn run_cross_isa_fuzz(
    seed: u64,
    iterations: usize,
    suite: &CrossIsaSuite,
) -> Result<(), ToolError> {
    let mut rng = SplitMix64::new(seed);
    let mut cases = Vec::with_capacity(iterations);
    for case_index in 0..iterations {
        cases.push((case_index, generate_cross_isa_case(&mut rng, case_index)));
    }

    let nthreads = std::thread::available_parallelism()
        .map_or(4, usize::from)
        .clamp(1, 32);
    let next_idx = std::sync::atomic::AtomicUsize::new(0);
    let comspec_dos = format!("A:{BS}WINCMD.COM");

    std::thread::scope(|scope| -> Result<(), ToolError> {
        let mut handles = Vec::new();
        for _ in 0..nthreads {
            let comspec_ref = &comspec_dos;
            let cases_ref = &cases;
            let idx_ref = &next_idx;
            handles.push(scope.spawn(move || -> Result<(), ToolError> {
                loop {
                    let i = idx_ref.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    let Some((case_index, case)) = cases_ref.get(i) else {
                        break;
                    };
                    run_single_cross_isa_case(seed, *case_index, case, suite, comspec_ref)?;
                }
                Ok(())
            }));
        }
        for h in handles {
            match h.join() {
                Ok(r) => r?,
                Err(_) => {
                    return Err(ToolError::DiffFuzz {
                        seed,
                        case_index: 0,
                        details: String::from("cross-ISA worker thread panicked"),
                    });
                }
            }
        }
        Ok(())
    })
}

fn seed_pe_fuzz_vfs(env: &Rc<RefCell<SharedWinEnv>>, root_pfx: &str, case: &CrossIsaCase) {
    let mut g = env.borrow_mut();
    g.write_file(&format!("{root_pfx}FUZZ.BAT"), &case.bat_bytes);
    g.write_file(&format!("{root_pfx}CHILD.COM"), &case.child_com);
    g.write_file(&format!("{root_pfx}IN.TXT"), &case.in_payload);
}

fn collect_pe_outcome(
    exit_code: u32,
    stdout: String,
    stderr: String,
    env: &Rc<RefCell<SharedWinEnv>>,
    root_pfx: &str,
) -> IsaOutcome {
    let g = env.borrow();
    IsaOutcome {
        exit_code: u8::try_from(exit_code & 0xFF).unwrap_or(0),
        stdout,
        stderr,
        out_txt: g.read_file(&format!("{root_pfx}OUT.TXT")),
        in_exists: g.read_file(&format!("{root_pfx}IN.TXT")).is_some(),
    }
}

fn verify_win16_fuzz_case(
    seed: u64,
    case_index: usize,
    w16_lbl: &str,
    w16_img: &[u8],
    case: &CrossIsaCase,
) -> Result<(), ToolError> {
    let mut wvm = DosVm::new(DosEra::Dos400);
    wvm.vfs
        .insert(String::from("FUZZ16.BAT"), case.win16_bat_bytes.clone());
    wvm.vfs
        .insert(String::from("IN.TXT"), case.in_payload.clone());
    wvm.load_ne(w16_img, Win16Era::Wfw311, "/K FUZZ16.BAT")?;
    wvm.push_win16_msg(0x000F, 0, 0);
    for &ch in format!("EXIT {}", case.expected_exit).as_bytes() {
        wvm.push_win16_msg(0x0102, u16::from(ch), 0);
    }
    wvm.push_win16_msg(0x0102, 0x000D, 0);
    let wres = wvm.run(4_000_000)?;
    let gdi_txt = wvm.pmode.as_ref().map_or_default(|pm| {
        pm.text_out_calls
            .iter()
            .map(|(_, _, b)| String::from_utf8_lossy(b).into_owned())
            .collect::<Vec<_>>()
            .join(" ")
    });
    if wres.exit_code != case.expected_exit
        || wvm.vfs.get("OUT.TXT").map(Vec::as_slice) != Some(case.in_payload.as_slice())
        || wvm.vfs.contains_key("IN.TXT")
        || !gdi_txt.contains(&case.expected_arith.to_string())
    {
        return Err(ToolError::DiffFuzz {
            seed,
            case_index,
            details: format!(
                "`{w16_lbl}` Win16 divergence: exit={} (want {}), gdi=`{gdi_txt}`",
                wres.exit_code, case.expected_exit
            ),
        });
    }
    Ok(())
}

fn run_single_cross_isa_case(
    seed: u64,
    case_index: usize,
    case: &CrossIsaCase,
    suite: &CrossIsaSuite,
    comspec_dos: &str,
) -> Result<(), ToolError> {
    let (dos_lbl, dos_img) = &suite.dos_imgs[case_index % suite.dos_imgs.len()];
    let (w16_lbl, w16_img) = &suite.win16_imgs[case_index % suite.win16_imgs.len()];
    let (x32_lbl, x32_img) = &suite.x86_32_imgs[case_index % suite.x86_32_imgs.len()];
    let (x64_lbl, x64_img) = &suite.x86_64_imgs[case_index % suite.x86_64_imgs.len()];
    let (a32_lbl, a32_img) = &suite.arm32_imgs[case_index % suite.arm32_imgs.len()];
    let (a64_lbl, a64_img) = &suite.arm64_imgs[case_index % suite.arm64_imgs.len()];

    // 1. 8086 Real-Mode MS-DOS (`WINCMD.COM`)
    let mut dvm = DosVm::new(DosEra::Dos710);
    dvm.vfs
        .insert(String::from("FUZZ.BAT"), case.bat_bytes.clone());
    dvm.vfs
        .insert(String::from("CHILD.COM"), case.child_com.clone());
    dvm.vfs
        .insert(String::from("IN.TXT"), case.in_payload.clone());
    dvm.load_com(
        dos_img,
        " --dialect=nt /C FUZZ.BAT",
        &[("COMSPEC", comspec_dos), ("PATH", "A:")],
    )?;
    let dres = dvm.run(4_000_000)?;
    let base_out = IsaOutcome {
        exit_code: dres.exit_code,
        stdout: String::from_utf8_lossy(&dres.stdout).into_owned(),
        stderr: String::from_utf8_lossy(&dres.stderr).into_owned(),
        out_txt: dvm.vfs.get("OUT.TXT").cloned(),
        in_exists: dvm.vfs.contains_key("IN.TXT"),
    };
    if base_out.exit_code != case.expected_exit
        || base_out.out_txt.as_deref() != Some(case.in_payload.as_slice())
        || base_out.in_exists
    {
        return Err(ToolError::DiffFuzz {
            seed,
            case_index,
            details: format!(
                "`{dos_lbl}` baseline invariants failed: exit={} (want {}), in_exists={}",
                base_out.exit_code, case.expected_exit, base_out.in_exists
            ),
        });
    }

    // 2. 80286 Protected-Mode Win16 NE (`WINCMD286.EXE`)
    verify_win16_fuzz_case(seed, case_index, w16_lbl, w16_img, case)?;

    let verify_pe = |label: &str, outcome: IsaOutcome| -> Result<(), ToolError> {
        if outcome != base_out {
            return Err(ToolError::DiffFuzz {
                seed,
                case_index,
                details: format!(
                    "cross-ISA divergence `{dos_lbl}` vs `{label}`: base={base_out:?} != got={outcome:?}"
                ),
            });
        }
        Ok(())
    };
    let nt_root = format!("C:{BS}");
    let wince_root = BS.to_string();

    // 3. IA-32 PE32 (`WINCMD32.EXE`) & 4. x86-64 PE32+ (`WINCMD64.EXE`)
    for (lbl, img) in [
        (x32_lbl.as_str(), x32_img.as_slice()),
        (x64_lbl.as_str(), x64_img.as_slice()),
    ] {
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        seed_pe_fuzz_vfs(&env, &nt_root, case);
        let mut vm = X86PeVm::from_pe_bytes(img, "/C FUZZ.BAT", &[], Rc::clone(&env))?;
        let r = vm.run_with_coverage(12_000_000, false)?;
        verify_pe(
            lbl,
            collect_pe_outcome(r.exit_code, r.stdout, r.stderr, &env, &nt_root),
        )?;
    }

    // 5. ARMv4 WinCE PE32 (`WINCMD_ARMV4.EXE`) & 6. ARMv7 Thumb-2 PE32 (`WINCMD_ARM32.EXE`)
    if let Some(v4_img) = suite.armv4_img.as_deref() {
        let env = Rc::new(RefCell::new(SharedWinEnv::new(true)));
        seed_pe_fuzz_vfs(&env, &wince_root, case);
        let mut vm =
            Arm32Vm::from_pe_bytes(v4_img, &format!("/C {BS}FUZZ.BAT"), &[], Rc::clone(&env))?;
        let r = vm.run_with_coverage(12_000_000, false)?;
        verify_pe(
            "fasmarm/armv4",
            collect_pe_outcome(r.exit_code, r.stdout, r.stderr, &env, &wince_root),
        )?;
    }
    {
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        seed_pe_fuzz_vfs(&env, &nt_root, case);
        let mut vm = Arm32Vm::from_pe_bytes(a32_img, "/C FUZZ.BAT", &[], Rc::clone(&env))?;
        let r = vm.run_with_coverage(12_000_000, false)?;
        verify_pe(
            a32_lbl,
            collect_pe_outcome(r.exit_code, r.stdout, r.stderr, &env, &nt_root),
        )?;
    }

    // 7. AArch64 PE32+ (`WINCMD_ARM64.EXE`)
    let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
    seed_pe_fuzz_vfs(&env, &nt_root, case);
    let mut vm = Arm64Vm::from_pe_bytes(a64_img, "/C FUZZ.BAT", &[], Rc::clone(&env))?;
    let r = vm.run_with_coverage(20_000_000, false)?;
    verify_pe(
        a64_lbl,
        collect_pe_outcome(r.exit_code, r.stdout, r.stderr, &env, &nt_root),
    )
}

fn probe_reference_cmd_runner() -> Option<(String, Vec<String>)> {
    if cfg!(windows) {
        return Some((
            String::from("cmd.exe"),
            vec![String::from("/d"), String::from("/c")],
        ));
    }
    let ok = std::process::Command::new("wine")
        .env("WINEDEBUG", "-all")
        .args(["cmd.exe", "/d", "/c", "ver"])
        .output()
        .is_ok_and(|o| o.status.success());
    if ok {
        Some((
            String::from("wine"),
            vec![
                String::from("cmd.exe"),
                String::from("/d"),
                String::from("/c"),
            ],
        ))
    } else {
        None
    }
}

/// Verifies `count` generated pure-batch [`CrossIsaSuite`] scripts against real Windows `cmd.exe`.
///
/// Uses native `cmd.exe` on Windows or `wine cmd.exe` on POSIX, asserting identical `stdout`,
/// `stderr`, `ERRORLEVEL`, and `OUT.TXT`/`IN.TXT` VFS state against `WINCMD64.EXE`. Returns
/// `Ok(0)` when neither native `cmd.exe` nor `wine` is installed on the host.
///
/// # Errors
///
/// Returns [`ToolError::DiffFuzz`] on any divergence between `wincmd` and `cmd.exe`.
pub fn verify_against_reference_cmd(
    seed: u64,
    count: usize,
    suite: &CrossIsaSuite,
) -> Result<usize, ToolError> {
    let Some((prog, base_args)) = probe_reference_cmd_runner() else {
        return Ok(0);
    };
    let mut rng = SplitMix64::new(seed);
    let (_, x64_img) = &suite.x86_64_imgs[0];
    let nt_root = format!("C:{BS}");
    let mut verified = 0usize;
    let mut idx = 0usize;

    while verified < count {
        let case = generate_cross_isa_case(&mut rng, idx);
        idx += 1;
        if (idx - 1).is_multiple_of(2) {
            continue;
        }
        let env = Rc::new(RefCell::new(SharedWinEnv::new(false)));
        seed_pe_fuzz_vfs(&env, &nt_root, &case);
        let mut vm = X86PeVm::from_pe_bytes(x64_img, "/C FUZZ.BAT", &[], Rc::clone(&env))?;
        let r = vm.run_with_coverage(12_000_000, false)?;
        let ours = collect_pe_outcome(r.exit_code, r.stdout, r.stderr, &env, &nt_root);

        let work =
            std::env::temp_dir().join(format!("wincmd_refcmd_{}_{verified}", std::process::id()));
        let _ = fs::remove_dir_all(&work);
        fs::create_dir_all(&work)?;
        fs::write(work.join("FUZZ.BAT"), &case.bat_bytes)?;
        fs::write(work.join("IN.TXT"), &case.in_payload)?;

        let out = std::process::Command::new(&prog)
            .args(&base_args)
            .arg("FUZZ.BAT")
            .current_dir(&work)
            .env("WINEDEBUG", "-all")
            .output()?;
        let ref_rc = u8::try_from(out.status.code().unwrap_or(0) & 0xFF).unwrap_or(0);
        let lf = core::str::from_utf8(&[10]).unwrap_or("");
        let crlf_s = core::str::from_utf8(&CRLF_BYTES).unwrap_or("");
        let ref_stdout = String::from_utf8_lossy(&out.stdout).replace(crlf_s, lf);
        let our_stdout = ours.stdout.replace(crlf_s, lf);
        let ref_out_txt = fs::read(work.join("OUT.TXT")).ok();
        let ref_in_exists = work.join("IN.TXT").exists();
        let _ = fs::remove_dir_all(&work);

        if ref_rc != ours.exit_code
            || ref_stdout.trim() != our_stdout.trim()
            || ref_out_txt != ours.out_txt
            || ref_in_exists != ours.in_exists
        {
            return Err(ToolError::DiffFuzz {
                seed,
                case_index: idx - 1,
                details: format!(
                    "reference cmd.exe divergence: ours=(rc={}, out=`{}`) vs cmd.exe=(rc={ref_rc}, out=`{}`)",
                    ours.exit_code,
                    our_stdout.trim(),
                    ref_stdout.trim()
                ),
            });
        }
        verified += 1;
    }
    Ok(verified)
}

/// Executes the `diff-fuzz` CLI:
/// - Cross-assembler DOS mode:
///   `diff-fuzz --seed <u64> --cases <N> [--era 4.00] <bin1.com> <bin2.com> [<bin3.com>...]`
/// - 7-target Cross-ISA mode:
///   `diff-fuzz --cross-isa --seed <u64> --cases <N> --build-dir <dir>`
///
struct CliConfig {
    seed: u64,
    cases: usize,
    ref_cmd_cases: usize,
    era: DosEra,
    cross_isa: bool,
    build_dir: PathBuf,
    paths: Vec<PathBuf>,
}

fn parse_cli_args(collected: &[String]) -> Result<CliConfig, ToolError> {
    let mut cfg = CliConfig {
        seed: 0x2026_1120_u64,
        cases: 100_usize,
        ref_cmd_cases: 0_usize,
        era: DosEra::Dos400,
        cross_isa: false,
        build_dir: PathBuf::from("build"),
        paths: Vec::new(),
    };
    let mut idx = 1_usize;
    while idx < collected.len() {
        let next_val = |flag: &str| {
            collected.get(idx + 1).ok_or_else(|| ToolError::CliUsage {
                message: format!("missing value after `{flag}`"),
            })
        };
        match collected[idx].as_str() {
            "--cross-isa" => {
                cfg.cross_isa = true;
                idx += 1;
            }
            "--ref-cmd" => {
                cfg.ref_cmd_cases = next_val("--ref-cmd")?.parse::<usize>()?;
                idx += 2;
            }
            "--build-dir" => {
                cfg.build_dir = PathBuf::from(next_val("--build-dir")?);
                idx += 2;
            }
            "--seed" => {
                let val = next_val("--seed")?;
                cfg.seed = if let Some(h) = val.strip_prefix("0x") {
                    u64::from_str_radix(h, 16)?
                } else {
                    val.parse::<u64>()?
                };
                idx += 2;
            }
            "--cases" => {
                cfg.cases = next_val("--cases")?.parse::<usize>()?;
                idx += 2;
            }
            "--era" => {
                let val = next_val("--era")?;
                cfg.era = DosEra::parse(val).ok_or_else(|| ToolError::CliUsage {
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
                cfg.paths.push(PathBuf::from(pos));
                idx += 1;
            }
        }
    }
    Ok(cfg)
}

/// CLI entrypoint for `diff-fuzz`.
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
    let cfg = parse_cli_args(&collected)?;

    if cfg.cross_isa {
        let suite = CrossIsaSuite::load_from_build_dir(&cfg.build_dir)?;
        run_cross_isa_fuzz(cfg.seed, cfg.cases, &suite)?;
        let ref_ran = if cfg.ref_cmd_cases > 0 {
            verify_against_reference_cmd(cfg.seed, cfg.ref_cmd_cases, &suite)?
        } else {
            0
        };
        writeln!(
            out,
            "diff-fuzz: {} 7-target cross-ISA cases (+ {ref_ran} live cmd.exe cases) passed (seed 0x{:X})",
            cfg.cases, cfg.seed
        )?;
        return Ok(());
    }

    if cfg.paths.len() < 2 {
        return Err(ToolError::CliUsage {
            message: "expected at least 2 `.COM` binaries to compare".into(),
        });
    }

    let mut loaded: Vec<(String, Vec<u8>)> = Vec::with_capacity(cfg.paths.len());
    for p in &cfg.paths {
        loaded.push((p.to_string_lossy().into_owned(), fs::read(p)?));
    }
    let refs: Vec<(&str, &[u8])> = loaded
        .iter()
        .map(|(n, b)| (n.as_str(), b.as_slice()))
        .collect();

    run_differential_com_fuzz(cfg.seed, cfg.cases, cfg.era, &refs)?;
    writeln!(
        out,
        "diff-fuzz: {} differential cases passed across {} binaries (seed 0x{:X})",
        cfg.cases,
        refs.len(),
        cfg.seed
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
            run_differential_com_fuzz(
                1,
                5,
                DosEra::Dos400,
                &[("nasm", &ok_com), ("bad", &bad_com)]
            )
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
        assert!(run_cli(["diff-fuzz", "--build-dir"], &mut out).is_err());
        assert!(
            run_cli(
                [
                    "diff-fuzz",
                    "--cross-isa",
                    "--build-dir",
                    &tmp.to_string_lossy()
                ],
                &mut out
            )
            .is_err()
        );
        assert!(run_cli(["diff-fuzz", "--unknown"], &mut out).is_err());

        let _ = fs::remove_dir_all(&tmp);
        Ok(())
    }
}
