//! Unified diagnostic types, source spans, target ISA/assembler descriptors,
//! canonical `ERR_*` error enumeration (`1..=511`), and the single workspace-wide
//! [`ToolError`] enum powered by `derive_more`.

use core::fmt;
use core::num::ParseIntError;
use core::str::Utf8Error;
use derive_more::{Display, Error, From};

/// Canonical unified error codes (`1..=511`) shared across all assembly targets
/// (`asm/inc/err_code.inc`) and Rust verification tools.
#[repr(u16)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum ErrCode {
    /// Invalid MS-DOS / OS function number (`0x01`).
    InvalidFunction = 1,
    /// File not found (`0x02`).
    FileNotFound = 2,
    /// Path not found or subdirectory unsupported on DOS 1.25 (`0x03`).
    PathNotFound = 3,
    /// Too many open files (`FILES=` exhausted, `0x04`).
    TooManyOpenFiles = 4,
    /// Access denied / read-only target (`0x05`).
    AccessDenied = 5,
    /// Invalid file handle (`0x06`).
    InvalidHandle = 6,
    /// Memory control blocks destroyed (`0x07`).
    McbDestroyed = 7,
    /// Insufficient memory / arena exhausted (`0x08`).
    OutOfMemory = 8,
    /// Invalid memory block address (`0x09`).
    InvalidMcb = 9,
    /// Invalid environment block (`0x0A`).
    InvalidEnvironment = 10,
    /// Invalid executable format (`0x0B`).
    BadExeFormat = 11,
    /// Invalid open access mode (`0x0C`).
    InvalidAccess = 12,
    /// Invalid data (`0x0D`).
    InvalidData = 13,
    /// Invalid drive specification (`0x0F`).
    InvalidDrive = 15,
    /// Attempt to remove current directory (`0x10`).
    RemoveCurrentDir = 16,
    /// Not same device across rename/move (`0x11`).
    NotSameDevice = 17,
    /// No more matching files (`0x12`).
    NoMoreFiles = 18,
    /// Media is write-protected (`0x13`).
    WriteProtect = 19,
    /// Drive not ready (`0x15`).
    DriveNotReady = 21,
    /// Data CRC error (`0x17`).
    CrcError = 23,
    /// Write fault (`0x1D`).
    WriteFault = 29,
    /// Read fault (`0x1E`).
    ReadFault = 30,
    /// General hardware failure (`0x1F`).
    GeneralFailure = 31,
    /// Sharing violation (`0x20`).
    SharingViolation = 32,
    /// Lock violation (`0x21`).
    LockViolation = 33,
    /// File already exists (`0x50`).
    FileExists = 80,
    /// Cannot make directory entry (`0x52`).
    CannotMake = 82,
    /// Fail on `INT 24h` critical error (`0x53`).
    FailInt24 = 83,
    /// Invalid parameter or switch (`0x57`).
    InvalidParameter = 87,
    /// Disk full / insufficient disk space (`0x70`).
    DiskFull = 112,
    /// Directory is not empty (`0x91`).
    DirNotEmpty = 145,
    /// Filename or path exceeds era limit (`0xCE`).
    FilenameExcedRange = 206,
    /// Command syntax error (`256`).
    SyntaxError = 256,
    /// Unknown internal/external command (`257`).
    BadCommand = 257,
    /// Batch label not found (`258`).
    LabelNotFound = 258,
    /// Division by zero in `SET /A` (`259`).
    DivByZero = 259,
    /// Invalid numeric literal (e.g. `08` octal) in `SET /A` (`260`).
    InvalidNumber = 260,
    /// Unbalanced parentheses in expression or command block (`261`).
    UnbalancedParens = 261,
    /// Missing operand in `SET /A` (`262`).
    MissingOperand = 262,
    /// Missing operator in `SET /A` (`263`).
    MissingOperator = 263,
    /// Environment buffer full (`264`).
    EnvFull = 264,
    /// Batch call nesting limit exceeded (`265`).
    BatchNestOverflow = 265,
    /// Transient shell image reload failed (`266`).
    TransientReloadFailed = 266,
    /// Unsupported OS capability on active host (`511`).
    UnsupportedCapability = 511,
}

impl ErrCode {
    /// All defined canonical error variants in ascending numeric order.
    pub const ALL: &'static [Self] = &[
        Self::InvalidFunction,
        Self::FileNotFound,
        Self::PathNotFound,
        Self::TooManyOpenFiles,
        Self::AccessDenied,
        Self::InvalidHandle,
        Self::McbDestroyed,
        Self::OutOfMemory,
        Self::InvalidMcb,
        Self::InvalidEnvironment,
        Self::BadExeFormat,
        Self::InvalidAccess,
        Self::InvalidData,
        Self::InvalidDrive,
        Self::RemoveCurrentDir,
        Self::NotSameDevice,
        Self::NoMoreFiles,
        Self::WriteProtect,
        Self::DriveNotReady,
        Self::CrcError,
        Self::WriteFault,
        Self::ReadFault,
        Self::GeneralFailure,
        Self::SharingViolation,
        Self::LockViolation,
        Self::FileExists,
        Self::CannotMake,
        Self::FailInt24,
        Self::InvalidParameter,
        Self::DiskFull,
        Self::DirNotEmpty,
        Self::FilenameExcedRange,
        Self::SyntaxError,
        Self::BadCommand,
        Self::LabelNotFound,
        Self::DivByZero,
        Self::InvalidNumber,
        Self::UnbalancedParens,
        Self::MissingOperand,
        Self::MissingOperator,
        Self::EnvFull,
        Self::BatchNestOverflow,
        Self::TransientReloadFailed,
        Self::UnsupportedCapability,
    ];

    /// Returns the raw 16-bit `ERR_*` numeric value (`1..=511`).
    #[must_use]
    pub const fn code(self) -> u16 {
        self as u16
    }

    /// Resolves a raw 16-bit integer into an [`ErrCode`] if defined.
    #[must_use]
    pub const fn from_code(raw: u16) -> Option<Self> {
        match raw {
            1 => Some(Self::InvalidFunction),
            2 => Some(Self::FileNotFound),
            3 => Some(Self::PathNotFound),
            4 => Some(Self::TooManyOpenFiles),
            5 => Some(Self::AccessDenied),
            6 => Some(Self::InvalidHandle),
            7 => Some(Self::McbDestroyed),
            8 => Some(Self::OutOfMemory),
            9 => Some(Self::InvalidMcb),
            10 => Some(Self::InvalidEnvironment),
            11 => Some(Self::BadExeFormat),
            12 => Some(Self::InvalidAccess),
            13 => Some(Self::InvalidData),
            15 => Some(Self::InvalidDrive),
            16 => Some(Self::RemoveCurrentDir),
            17 => Some(Self::NotSameDevice),
            18 => Some(Self::NoMoreFiles),
            19 => Some(Self::WriteProtect),
            21 => Some(Self::DriveNotReady),
            23 => Some(Self::CrcError),
            29 => Some(Self::WriteFault),
            30 => Some(Self::ReadFault),
            31 => Some(Self::GeneralFailure),
            32 => Some(Self::SharingViolation),
            33 => Some(Self::LockViolation),
            80 => Some(Self::FileExists),
            82 => Some(Self::CannotMake),
            83 => Some(Self::FailInt24),
            87 => Some(Self::InvalidParameter),
            112 => Some(Self::DiskFull),
            145 => Some(Self::DirNotEmpty),
            206 => Some(Self::FilenameExcedRange),
            256 => Some(Self::SyntaxError),
            257 => Some(Self::BadCommand),
            258 => Some(Self::LabelNotFound),
            259 => Some(Self::DivByZero),
            260 => Some(Self::InvalidNumber),
            261 => Some(Self::UnbalancedParens),
            262 => Some(Self::MissingOperand),
            263 => Some(Self::MissingOperator),
            264 => Some(Self::EnvFull),
            265 => Some(Self::BatchNestOverflow),
            266 => Some(Self::TransientReloadFailed),
            511 => Some(Self::UnsupportedCapability),
            _ => None,
        }
    }

    /// Maps an MS-DOS 1.25 FCB completion code (`AL` from `AH=0Fh/16h/27h/28h`) to [`ErrCode`].
    #[must_use]
    pub const fn from_dos125_fcb(al: u8, is_write: bool) -> Option<Self> {
        match (al, is_write) {
            (0x00, _) => None,
            (0x01, true) => Some(Self::DiskFull),
            (0x01 | 0x03, false) => Some(Self::NoMoreFiles),
            (0x02, _) => Some(Self::InvalidData),
            (0xFF, true) => Some(Self::CannotMake),
            (_, _) => Some(Self::FileNotFound),
        }
    }

    /// Maps a Win32 `GetLastError()` or DOS 2.00+/4.00 extended `AX` code to [`ErrCode`].
    #[must_use]
    pub const fn from_os_error(raw: u32) -> Self {
        let b = raw.to_le_bytes();
        let low16 = u16::from_le_bytes([b[0], b[1]]);
        if raw <= 511
            && let Some(known) = Self::from_code(low16)
        {
            return known;
        }
        Self::GeneralFailure
    }

    /// Returns the canonical assembly constant identifier (`ERR_*`).
    #[must_use]
    pub const fn asm_ident(self) -> &'static str {
        match self {
            Self::InvalidFunction => "ERR_INVALID_FUNCTION",
            Self::FileNotFound => "ERR_FILE_NOT_FOUND",
            Self::PathNotFound => "ERR_PATH_NOT_FOUND",
            Self::TooManyOpenFiles => "ERR_TOO_MANY_OPEN_FILES",
            Self::AccessDenied => "ERR_ACCESS_DENIED",
            Self::InvalidHandle => "ERR_INVALID_HANDLE",
            Self::McbDestroyed => "ERR_MCB_DESTROYED",
            Self::OutOfMemory => "ERR_OUT_OF_MEMORY",
            Self::InvalidMcb => "ERR_INVALID_MCB",
            Self::InvalidEnvironment => "ERR_INVALID_ENVIRONMENT",
            Self::BadExeFormat => "ERR_BAD_EXE_FORMAT",
            Self::InvalidAccess => "ERR_INVALID_ACCESS",
            Self::InvalidData => "ERR_INVALID_DATA",
            Self::InvalidDrive => "ERR_INVALID_DRIVE",
            Self::RemoveCurrentDir => "ERR_REMOVE_CURRENT_DIR",
            Self::NotSameDevice => "ERR_NOT_SAME_DEVICE",
            Self::NoMoreFiles => "ERR_NO_MORE_FILES",
            Self::WriteProtect => "ERR_WRITE_PROTECT",
            Self::DriveNotReady => "ERR_DRIVE_NOT_READY",
            Self::CrcError => "ERR_CRC_ERROR",
            Self::WriteFault => "ERR_WRITE_FAULT",
            Self::ReadFault => "ERR_READ_FAULT",
            Self::GeneralFailure => "ERR_GENERAL_FAILURE",
            Self::SharingViolation => "ERR_SHARING_VIOLATION",
            Self::LockViolation => "ERR_LOCK_VIOLATION",
            Self::FileExists => "ERR_FILE_EXISTS",
            Self::CannotMake => "ERR_CANNOT_MAKE",
            Self::FailInt24 => "ERR_FAIL_INT24",
            Self::InvalidParameter => "ERR_INVALID_PARAMETER",
            Self::DiskFull => "ERR_DISK_FULL",
            Self::DirNotEmpty => "ERR_DIR_NOT_EMPTY",
            Self::FilenameExcedRange => "ERR_FILENAME_EXCED_RANGE",
            Self::SyntaxError => "ERR_SYNTAX_ERROR",
            Self::BadCommand => "ERR_BAD_COMMAND",
            Self::LabelNotFound => "ERR_LABEL_NOT_FOUND",
            Self::DivByZero => "ERR_DIV_BY_ZERO",
            Self::InvalidNumber => "ERR_INVALID_NUMBER",
            Self::UnbalancedParens => "ERR_UNBALANCED_PARENS",
            Self::MissingOperand => "ERR_MISSING_OPERAND",
            Self::MissingOperator => "ERR_MISSING_OPERATOR",
            Self::EnvFull => "ERR_ENV_FULL",
            Self::BatchNestOverflow => "ERR_BATCH_NEST_OVERFLOW",
            Self::TransientReloadFailed => "ERR_TRANSIENT_RELOAD_FAILED",
            Self::UnsupportedCapability => "ERR_UNSUPPORTED_CAPABILITY",
        }
    }

    /// Returns the human-readable standard English diagnostic string matching `cmd.exe` / `COMMAND.COM`.
    #[must_use]
    pub const fn message(self) -> &'static str {
        match self {
            Self::InvalidFunction => "Incorrect function.",
            Self::FileNotFound => "The system cannot find the file specified.",
            Self::PathNotFound => "The system cannot find the path specified.",
            Self::TooManyOpenFiles => "The system cannot open the file.",
            Self::AccessDenied => "Access is denied.",
            Self::InvalidHandle => "The handle is invalid.",
            Self::McbDestroyed => "Memory control blocks destroyed.",
            Self::OutOfMemory => "Not enough memory resources are available to process this command.",
            Self::InvalidMcb => "The storage control block address is invalid.",
            Self::InvalidEnvironment => "The environment is incorrect.",
            Self::BadExeFormat => "The specified program is not a valid executable.",
            Self::InvalidAccess => "The access code is invalid.",
            Self::InvalidData => "The data is invalid.",
            Self::InvalidDrive => "The system cannot find the drive specified.",
            Self::RemoveCurrentDir => "The directory cannot be removed.",
            Self::NotSameDevice => "The system cannot move the file to a different disk drive.",
            Self::NoMoreFiles => "There are no more files.",
            Self::WriteProtect => "The media is write protected.",
            Self::DriveNotReady => "The device is not ready.",
            Self::CrcError => "Data error (cyclic redundancy check).",
            Self::WriteFault => "The system cannot write to the specified device.",
            Self::ReadFault => "The system cannot read from the specified device.",
            Self::GeneralFailure => "A device attached to the system is not functioning.",
            Self::SharingViolation => "The process cannot access the file because it is being used by another process.",
            Self::LockViolation => "The process cannot access the file because another process has locked a portion of the file.",
            Self::FileExists => "The file exists.",
            Self::CannotMake => "The directory or file cannot be created.",
            Self::FailInt24 => "Fail on INT 24.",
            Self::InvalidParameter => "The parameter is incorrect.",
            Self::DiskFull => "There is not enough space on the disk.",
            Self::DirNotEmpty => "The directory is not empty.",
            Self::FilenameExcedRange => "The filename or extension is too long.",
            Self::SyntaxError => "The syntax of the command is incorrect.",
            Self::BadCommand => "Bad command or file name.",
            Self::LabelNotFound => "The system cannot find the batch label specified.",
            Self::DivByZero => "Divide by zero error.",
            Self::InvalidNumber => "Invalid number. Numeric constants are either decimal (17), hexadecimal (0x11), or octal (021).",
            Self::UnbalancedParens => "Unbalanced parenthesis.",
            Self::MissingOperand => "Missing operand.",
            Self::MissingOperator => "Missing operator.",
            Self::EnvFull => "Out of environment space.",
            Self::BatchNestOverflow => "Maximum setlocal or batch recursion level reached.",
            Self::TransientReloadFailed => "Cannot load COMMAND, system halted.",
            Self::UnsupportedCapability => "The request is not supported.",
        }
    }
}

impl fmt::Display for ErrCode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} ({}): {}", self.asm_ident(), self.code(), self.message())
    }
}

/// CPU instruction set architecture tiers supported by `win-cmd-asm`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum CpuIsa {
    /// Strict 1978 Intel 8086 / 8088 16-bit Real Mode (and selector-clean Win16).
    X86_8086,
    /// Intel 80286 16-bit Protected Mode (`.286p` Win16 `NE` / `DPMI`).
    X86_286,
    /// Pure Intel `i386` 32-bit flat Protected Mode (`PE32`).
    Ia32,
    /// 64-bit `x86-64` / `AMD64` Long Mode (`PE32+`).
    X86_64,
    /// Strict 32-bit `ARMv4` `A32` RISC (`PE32` `0x01C0`).
    ArmV4,
    /// 32-bit `ARMv7-A` Unified Thumb-2 (`PE32` `0x01C4` `ARMNT`).
    ArmV7,
    /// 64-bit `ARM64` / `AArch64` (`PE32+` `0xAA64`).
    Arm64,
}

impl CpuIsa {
    /// Parses a canonical `@cpu` directive token into a [`CpuIsa`].
    #[must_use]
    pub fn parse(token: &str) -> Option<Self> {
        match token.trim().to_ascii_lowercase().as_str() {
            "8086" | "8088" | "x86-16" => Some(Self::X86_8086),
            "286" | "80286" | "286p" => Some(Self::X86_286),
            "386" | "i386" | "ia32" | "x86_32" => Some(Self::Ia32),
            "x64" | "amd64" | "x86_64" | "x86-64" => Some(Self::X86_64),
            "armv4" | "armv4t" => Some(Self::ArmV4),
            "armv7" | "armv7a" | "armnt" | "thumb2" => Some(Self::ArmV7),
            "arm64" | "aarch64" => Some(Self::Arm64),
            _ => None,
        }
    }

    /// Returns `true` if this ISA belongs to the x86 family (`8086`, `286`, `IA-32`, `x86-64`).
    #[must_use]
    pub const fn is_x86(self) -> bool {
        matches!(self, Self::X86_8086 | Self::X86_286 | Self::Ia32 | Self::X86_64)
    }

    /// Returns the canonical name string for diagnostics and `@isa` tags.
    #[must_use]
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::X86_8086 => "8086",
            Self::X86_286 => "286",
            Self::Ia32 => "386",
            Self::X86_64 => "x64",
            Self::ArmV4 => "armv4",
            Self::ArmV7 => "armv7",
            Self::Arm64 => "arm64",
        }
    }
}

impl fmt::Display for CpuIsa {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

/// Target assembler backends supported by the lowering engine.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum TargetAssembler {
    /// Netwide Assembler (`nasm`) for `8086`, `286`, `IA-32`, and `x86-64`.
    Nasm,
    /// Flat Assembler (`fasm`) for `8086`, `286`, `IA-32`, and `x86-64`.
    Fasm,
    /// `JWasm` (`jwasm`) MASM-compatible assembler for `8086`, `286`, `IA-32`, and `x86-64`.
    Jwasm,
    /// Flat Assembler for ARM (`fasmarm` / `fasmg`) for `ARMv4`, `ARMv7`, and `ARM64`.
    FasmArm,
    /// LLVM Machine Code / Clang integrated assembler (`llvm-mc`) for `ARMv4`, `ARMv7`, and `ARM64`.
    LlvmMc,
    /// Microsoft ARM Assembler (`armasm` / `armasm64`) for `ARMv4`, `ARMv7`, and `ARM64`.
    ArmAsm,
}

impl TargetAssembler {
    /// Parses a CLI assembler identifier into [`TargetAssembler`].
    #[must_use]
    pub fn parse(name: &str) -> Option<Self> {
        match name.trim().to_ascii_lowercase().as_str() {
            "nasm" => Some(Self::Nasm),
            "fasm" => Some(Self::Fasm),
            "jwasm" => Some(Self::Jwasm),
            "fasmarm" | "fasmg" => Some(Self::FasmArm),
            "llvm-mc" | "llvm" | "clang" => Some(Self::LlvmMc),
            "armasm" | "armasm64" | "msvc-arm" => Some(Self::ArmAsm),
            _ => None,
        }
    }

    /// Returns the canonical lowercase directory/CLI slug for this assembler.
    #[must_use]
    pub const fn slug(self) -> &'static str {
        match self {
            Self::Nasm => "nasm",
            Self::Fasm => "fasm",
            Self::Jwasm => "jwasm",
            Self::FasmArm => "fasmarm",
            Self::LlvmMc => "llvm-mc",
            Self::ArmAsm => "armasm",
        }
    }

    /// Checks whether this assembler supports the given [`CpuIsa`].
    #[must_use]
    pub const fn supports_isa(self, isa: CpuIsa) -> bool {
        match self {
            Self::Nasm | Self::Fasm | Self::Jwasm => isa.is_x86(),
            Self::FasmArm | Self::LlvmMc | Self::ArmAsm => !isa.is_x86(),
        }
    }
}

impl fmt::Display for TargetAssembler {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.slug())
    }
}

/// 1-based file, line, and column location for assembly/verification diagnostics.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct SourceSpan {
    /// Source file path or logical buffer name.
    pub file: String,
    /// 1-based line number.
    pub line: usize,
    /// 1-based column number.
    pub column: usize,
}

impl SourceSpan {
    /// Creates a new [`SourceSpan`].
    #[must_use]
    pub fn new(file: impl Into<String>, line: usize, column: usize) -> Self {
        Self {
            file: file.into(),
            line,
            column,
        }
    }
}

impl fmt::Display for SourceSpan {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}:{}", self.file, self.line, self.column)
    }
}

/// Single unified error enum for the entire `win-cmd-asm` Rust tool workspace,
/// built with `derive_more` (`Display`, `Error`, `From`) and zero `anyhow` / `unwrap`.
#[derive(Debug, Display, Error, From)]
pub enum ToolError {
    /// Standard library I/O failure.
    #[display("I/O error: {_0}")]
    Io(std::io::Error),

    /// UTF-8 decoding failure.
    #[display("UTF-8 decode error: {_0}")]
    Utf8(Utf8Error),

    /// Owned string UTF-8 decoding failure.
    #[display("String UTF-8 error: {_0}")]
    FromUtf8(std::string::FromUtf8Error),

    /// Integer literal parse failure.
    #[display("Integer parse error: {_0}")]
    ParseInt(ParseIntError),

    /// Invalid command-line invocation or flag.
    #[display("CLI usage error: {message}")]
    #[from(ignore)]
    CliUsage {
        /// Detailed explanation of the invalid CLI flag or missing argument.
        message: String,
    },

    /// Canonical assembly syntax error at a specific [`SourceSpan`].
    #[display("{span}: syntax error: {message}")]
    #[from(ignore)]
    Syntax {
        /// Source file location.
        span: SourceSpan,
        /// Explanation of the syntax failure.
        message: String,
    },

    /// Multi-assembler lowering error (`asm-conv`).
    #[display("{span}: lowering error for {assembler}: {message}")]
    #[from(ignore)]
    Lowering {
        /// Source file location.
        span: SourceSpan,
        /// Target assembler that rejected or could not represent the construct.
        assembler: TargetAssembler,
        /// Detailed lowering error message.
        message: String,
    },

    /// Structured assembly documentation coverage violation (`asm-lint`).
    #[display("{span}: missing or incomplete doc comment: {message}")]
    #[from(ignore)]
    DocLint {
        /// Source file location.
        span: SourceSpan,
        /// Missing `@tag` or documentation violation details.
        message: String,
    },

    /// Static control-flow graph / stack balance violation (`asm-lint`).
    #[display("{span}: CFG / stack balance error in `{proc_name}`: {message}")]
    #[from(ignore)]
    CfgLint {
        /// Source file location.
        span: SourceSpan,
        /// Procedure name where stack or control flow failed verification.
        proc_name: String,
        /// Detailed stack delta or unreachable/unterminated block description.
        message: String,
    },

    /// Register ABI, `@clobbers`, Win16 selector-math, or ARM64 `X18` violation (`asm-lint`).
    #[display("{span}: ABI / register discipline violation in `{proc_name}`: {message}")]
    #[from(ignore)]
    AbiLint {
        /// Source file location.
        span: SourceSpan,
        /// Procedure name.
        proc_name: String,
        /// Explanation of the violated ABI invariant.
        message: String,
    },

    /// Forbidden CPU instruction / opcode detected in source or disassembled binary (`asm-lint`).
    #[display("{span}: ISA `{isa}` opcode purity violation: {message}")]
    #[from(ignore)]
    OpcodePurity {
        /// Source or binary offset location.
        span: SourceSpan,
        /// Target CPU ISA whose baseline was violated.
        isa: CpuIsa,
        /// Offending mnemonic or encoding description.
        message: String,
    },

    /// Binary or resident memory footprint exceeded hard Phase 19 byte budget (`asm-lint`).
    #[display("size budget exceeded for `{artifact}`: {actual_bytes} bytes > {limit_bytes} bytes")]
    #[from(ignore)]
    SizeBudget {
        /// Artifact or section name.
        artifact: String,
        /// Observed size in bytes.
        actual_bytes: usize,
        /// Maximum permitted size in bytes.
        limit_bytes: usize,
    },

    /// Multi-assembler output equivalence mismatch (`NASM` vs `FASM` vs `JWasm`).
    #[display("assembler equivalence mismatch on `{artifact}` at byte offset 0x{offset:X}: {details}")]
    #[from(ignore)]
    Equivalence {
        /// Artifact under comparison.
        artifact: String,
        /// First differing byte or instruction offset.
        offset: usize,
        /// Detailed hex/disassembly comparison.
        details: String,
    },

    /// Deterministic CPU / OS syscall emulator fault (`emu-cov`).
    #[display("emulator trap at {cs:04X}:{ip:04X} ({code}): {message}")]
    #[from(ignore)]
    Emulator {
        /// Code segment or high PC word.
        cs: u16,
        /// Instruction pointer or low PC word.
        ip: u16,
        /// Canonical `ERR_*` code associated with the trap.
        code: ErrCode,
        /// Trap explanation.
        message: String,
    },

    /// Instruction or conditional-branch coverage fell below the required threshold (`emu-cov`).
    #[display("coverage gate failed for `{scope}`: {covered}/{total} ({percent_bps} bps < {required_bps} bps)")]
    #[from(ignore)]
    Coverage {
        /// Module or suite name.
        scope: String,
        /// Covered items (instructions or branch edges).
        covered: usize,
        /// Total items.
        total: usize,
        /// Achieved coverage in basis points (`10_000` = `100.00%`).
        percent_bps: u32,
        /// Required coverage in basis points (`10_000` = `100.00%`).
        required_bps: u32,
    },

    /// Differential fuzzing mismatch across dialects, assemblers, or ISAs (`diff-fuzz`).
    #[display("differential fuzz mismatch on seed 0x{seed:016X} (case #{case_index}): {details}")]
    #[from(ignore)]
    DiffFuzz {
        /// PRNG seed reproducing the failure.
        seed: u64,
        /// 0-based test case index.
        case_index: usize,
        /// Diff details (`stdout`, `stderr`, `ERRORLEVEL`, or VFS state).
        details: String,
    },

    /// Release media or archive mastering failure (`media-pkg`).
    #[display("media packaging error for `{artifact}`: {details}")]
    #[from(ignore)]
    MediaPackage {
        /// Target image or archive name.
        artifact: String,
        /// Explanation of the capacity, naming, or format failure.
        details: String,
    },
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn err_code_roundtrip_and_display() {
        for &variant in ErrCode::ALL {
            let raw = variant.code();
            assert!((1..=511).contains(&raw));
            assert_eq!(ErrCode::from_code(raw), Some(variant));
            assert_ne!(variant.asm_ident(), "");
            assert_ne!(variant.message(), "");
            let formatted = variant.to_string();
            assert!(formatted.contains(variant.asm_ident()));
        }
        assert_eq!(ErrCode::from_code(0), None);
        assert_eq!(ErrCode::from_code(999), None);
    }

    #[test]
    fn dos125_and_os_error_mappings() {
        assert_eq!(ErrCode::from_dos125_fcb(0x00, false), None);
        assert_eq!(ErrCode::from_dos125_fcb(0x01, true), Some(ErrCode::DiskFull));
        assert_eq!(ErrCode::from_dos125_fcb(0x01, false), Some(ErrCode::NoMoreFiles));
        assert_eq!(ErrCode::from_dos125_fcb(0x03, false), Some(ErrCode::NoMoreFiles));
        assert_eq!(ErrCode::from_dos125_fcb(0x02, false), Some(ErrCode::InvalidData));
        assert_eq!(ErrCode::from_dos125_fcb(0xFF, true), Some(ErrCode::CannotMake));
        assert_eq!(ErrCode::from_dos125_fcb(0xFF, false), Some(ErrCode::FileNotFound));

        assert_eq!(ErrCode::from_os_error(2), ErrCode::FileNotFound);
        assert_eq!(ErrCode::from_os_error(499), ErrCode::GeneralFailure);
        assert_eq!(ErrCode::from_os_error(99_999), ErrCode::GeneralFailure);
    }

    #[test]
    fn cpu_isa_and_assembler_matrix() {
        let pairs = [
            ("8086", CpuIsa::X86_8086, true),
            ("286p", CpuIsa::X86_286, true),
            ("386", CpuIsa::Ia32, true),
            ("x64", CpuIsa::X86_64, true),
            ("armv4", CpuIsa::ArmV4, false),
            ("armv7", CpuIsa::ArmV7, false),
            ("arm64", CpuIsa::Arm64, false),
        ];
        for (tok, expected, is_x86) in pairs {
            assert_eq!(CpuIsa::parse(tok), Some(expected));
            assert_eq!(expected.is_x86(), is_x86);
            assert_eq!(CpuIsa::parse(expected.as_str()), Some(expected));
            assert_eq!(expected.to_string(), expected.as_str());
        }
        assert_eq!(CpuIsa::parse("mips"), None);

        let assemblers = [
            ("nasm", TargetAssembler::Nasm, CpuIsa::X86_8086, CpuIsa::Arm64),
            ("fasm", TargetAssembler::Fasm, CpuIsa::Ia32, CpuIsa::ArmV7),
            ("jwasm", TargetAssembler::Jwasm, CpuIsa::X86_64, CpuIsa::ArmV4),
            ("fasmarm", TargetAssembler::FasmArm, CpuIsa::ArmV4, CpuIsa::X86_8086),
            ("llvm-mc", TargetAssembler::LlvmMc, CpuIsa::ArmV7, CpuIsa::X86_286),
            ("armasm64", TargetAssembler::ArmAsm, CpuIsa::Arm64, CpuIsa::Ia32),
        ];
        for (name, asm, ok_isa, bad_isa) in assemblers {
            assert_eq!(TargetAssembler::parse(name), Some(asm));
            assert_eq!(TargetAssembler::parse(asm.slug()), Some(asm));
            assert_eq!(asm.to_string(), asm.slug());
            assert!(asm.supports_isa(ok_isa));
            assert!(!asm.supports_isa(bad_isa));
        }
        assert_eq!(TargetAssembler::parse("unknown"), None);
    }

    #[test]
    fn tool_error_display_and_conversions() {
        let span = SourceSpan::new("asm/dos16/core/arena.asm", 12, 5);
        assert_eq!(span.to_string(), "asm/dos16/core/arena.asm:12:5");

        let io_err: ToolError = std::io::Error::new(std::io::ErrorKind::NotFound, "missing").into();
        assert!(io_err.to_string().contains("missing"));

        let bad_bytes = vec![0xFF_u8];
        let utf8_te = core::str::from_utf8(&bad_bytes).err().map(ToolError::from);
        assert!(utf8_te.is_some_and(|te| te.to_string().contains("UTF-8")));

        let str_te = String::from_utf8(vec![0xFF_u8]).err().map(ToolError::from);
        assert!(str_te.is_some_and(|te| te.to_string().contains("UTF-8")));

        let int_te = "xyz".parse::<u32>().err().map(ToolError::from);
        assert!(int_te.is_some_and(|te| te.to_string().contains("Integer")));

        let variants = [
            ToolError::CliUsage {
                message: "bad flag".into(),
            },
            ToolError::Syntax {
                span: span.clone(),
                message: "unexpected token".into(),
            },
            ToolError::Lowering {
                span: span.clone(),
                assembler: TargetAssembler::Jwasm,
                message: "unsupported directive".into(),
            },
            ToolError::DocLint {
                span: span.clone(),
                message: "missing @clobbers".into(),
            },
            ToolError::CfgLint {
                span: span.clone(),
                proc_name: "arena_alloc".into(),
                message: "SP delta +2".into(),
            },
            ToolError::AbiLint {
                span: span.clone(),
                proc_name: "w64_entry".into(),
                message: "clobbered RBX".into(),
            },
            ToolError::OpcodePurity {
                span,
                isa: CpuIsa::X86_8086,
                message: "PUSHA forbidden".into(),
            },
            ToolError::SizeBudget {
                artifact: "RESIDENT".into(),
                actual_bytes: 3000,
                limit_bytes: 2816,
            },
            ToolError::Equivalence {
                artifact: "WINCMD.COM".into(),
                offset: 0x14,
                details: "0x90 != 0x00".into(),
            },
            ToolError::Emulator {
                cs: 0x1000,
                ip: 0x0100,
                code: ErrCode::DivByZero,
                message: "INT 00h".into(),
            },
            ToolError::Coverage {
                scope: "math32".into(),
                covered: 99,
                total: 100,
                percent_bps: 9900,
                required_bps: 10_000,
            },
            ToolError::DiffFuzz {
                seed: 0xDEAD_BEEF,
                case_index: 7,
                details: "stdout differed".into(),
            },
            ToolError::MediaPackage {
                artifact: "wincmd-dos-160k.img".into(),
                details: "root directory full".into(),
            },
        ];
        for err in variants {
            assert_ne!(err.to_string(), "");
        }
    }
}
