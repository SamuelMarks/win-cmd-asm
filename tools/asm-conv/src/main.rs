//! CLI binary entrypoint for `asm-conv`.

use std::io::{self, Write};
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut stdout = io::stdout();
    if let Err(err) = asm_conv::run_cli(std::env::args(), &mut stdout) {
        let _ = writeln!(io::stderr(), "asm-conv error: {err}");
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}
