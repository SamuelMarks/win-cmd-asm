//! CLI binary entrypoint for `asm-lint`.

use std::io::{self, Write};
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut stdout = io::stdout();
    if let Err(err) = asm_lint::run_cli(std::env::args(), &mut stdout) {
        let _ = writeln!(io::stderr(), "asm-lint error: {err}");
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}
