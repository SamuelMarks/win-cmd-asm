//! CLI binary entrypoint for `emu-cov`.

use std::io::{self, Write};
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut stdout = io::stdout();
    if let Err(err) = emu_cov::run_cli(std::env::args(), &mut stdout) {
        let _ = writeln!(io::stderr(), "emu-cov error: {err}");
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}
