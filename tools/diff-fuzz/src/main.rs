//! CLI binary entrypoint for `diff-fuzz`.

use std::io::{self, Write};
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut stdout = io::stdout();
    if let Err(err) = diff_fuzz::run_cli(std::env::args(), &mut stdout) {
        let _ = writeln!(io::stderr(), "diff-fuzz error: {err}");
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}
