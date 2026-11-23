//! CLI binary entrypoint for `media-pkg`.

use std::io::{self, Write};
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut stdout = io::stdout();
    if let Err(err) = media_pkg::run_cli(std::env::args(), &mut stdout) {
        let _ = writeln!(io::stderr(), "media-pkg error: {err}");
        return ExitCode::FAILURE;
    }
    ExitCode::SUCCESS
}
