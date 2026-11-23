//! Canonical multi-ISA assembly Abstract Syntax Tree (`AST`) definitions.

use wincmd_diag::{CpuIsa, SourceSpan};

/// Structured documentation metadata extracted from contiguous `;; @tag value` comments.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct DocBlock {
    /// Ordered `(tag, value)` pairs (e.g. `("proc", "arena_alloc")`).
    pub tags: Vec<(String, String)>,
}

impl DocBlock {
    /// Returns the first value associated with `tag` (without leading `@`), if present.
    #[must_use]
    pub fn get(&self, tag: &str) -> Option<&str> {
        self.tags
            .iter()
            .find(|(k, _)| k.eq_ignore_ascii_case(tag))
            .map(|(_, v)| v.as_str())
    }

    /// Returns `true` if no `@tag` entries were recorded.
    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.tags.is_empty()
    }
}

/// Target executable or object file layout declared via `@format`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum BinaryFormat {
    /// Shared include header (`@format inc`).
    Include,
    /// Single-segment flat MS-DOS `.COM` (`ORG 0x0100`).
    Com,
    /// Authentic 3-part `COMMAND.COM` `.COM` (`RESIDENT` `0x0100`, `TRANSIENT` `0x0000`, `INIT` `0x0000`).
    Com3Part,
    /// Flat binary starting at `ORG 0x0000` (used for direct `NE` / `.PIF` / `.MSG` images).
    Bin16,
    /// Relocatable 16-bit MS-DOS `'MZ'` `.EXE` / `OMF` 16-bit object (`@format mz16`).
    Mz16,
    /// 32-bit `IA-32` MS COFF / `PE32` (`Subsystem 3.10`).
    Coff32,
    /// 64-bit `x86-64` MS COFF / `PE32+` (`Subsystem 5.02`).
    Coff64,
    /// 32-bit `ARMv4` `PE32` (`0x01C0` / COFF).
    ArmV4Pe,
    /// 32-bit `ARMv7-A` Thumb-2 `PE32` (`0x01C4` `ARMNT` / COFF).
    ArmV7Pe,
    /// 64-bit `ARM64` `PE32+` (`0xAA64`, `Subsystem 10.0` / COFF).
    Arm64Pe,
}

impl BinaryFormat {
    /// Parses a `@format` token into [`BinaryFormat`].
    #[must_use]
    pub fn parse(token: &str) -> Option<Self> {
        match token.trim().to_ascii_lowercase().as_str() {
            "inc" | "include" => Some(Self::Include),
            "com" => Some(Self::Com),
            "com3part" => Some(Self::Com3Part),
            "bin16" | "ne16" => Some(Self::Bin16),
            "mz16" | "mz" => Some(Self::Mz16),
            "coff32" | "pe32" => Some(Self::Coff32),
            "coff64" | "pe64" => Some(Self::Coff64),
            "armv4_pe" | "armv4" => Some(Self::ArmV4Pe),
            "armv7_pe" | "armv7" => Some(Self::ArmV7Pe),
            "arm64_pe" | "arm64" => Some(Self::Arm64Pe),
            _ => None,
        }
    }
}

/// Logical binary section declared via `@section`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum SectionKind {
    /// Low-memory resident DOS shell core (`ORG 0x0100`).
    Resident,
    /// High-memory relocatable DOS transient shell (`vstart = 0x0000`).
    Transient,
    /// Discardable DOS startup initializer (`vstart = 0x0000`).
    Init,
    /// Standard executable code section (`.text`).
    Text,
    /// Read-only constant data section (`.rdata`).
    Rdata,
    /// Initialized read-write data section (`.data`).
    Data,
    /// Zero-initialized BSS section (`.bss`).
    Bss,
}

impl SectionKind {
    /// Parses a `@section` name into [`SectionKind`].
    #[must_use]
    pub fn parse(token: &str) -> Option<Self> {
        match token.trim().to_ascii_lowercase().as_str() {
            "resident" => Some(Self::Resident),
            "transient" => Some(Self::Transient),
            "init" => Some(Self::Init),
            "text" | ".text" | "code" => Some(Self::Text),
            "rdata" | ".rdata" | "rodata" => Some(Self::Rdata),
            "data" | ".data" => Some(Self::Data),
            "bss" | ".bss" => Some(Self::Bss),
            _ => None,
        }
    }
}

/// Scalar element width for `@field`, `@data`, and `@res`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum DataWidth {
    /// 8-bit byte (`1` byte).
    Byte,
    /// 16-bit word (`2` bytes).
    Word,
    /// 32-bit doubleword (`4` bytes).
    Dword,
    /// 64-bit quadword (`8` bytes).
    Qword,
}

impl DataWidth {
    /// Parses `byte`/`db`, `word`/`dw`, `dword`/`dd`, or `qword`/`dq` into [`DataWidth`].
    #[must_use]
    pub fn parse(token: &str) -> Option<Self> {
        match token.trim().to_ascii_lowercase().as_str() {
            "byte" | "db" => Some(Self::Byte),
            "word" | "dw" => Some(Self::Word),
            "dword" | "dd" => Some(Self::Dword),
            "qword" | "dq" => Some(Self::Qword),
            _ => None,
        }
    }

    /// Size of one element in bytes.
    #[must_use]
    pub const fn byte_size(self) -> usize {
        match self {
            Self::Byte => 1,
            Self::Word => 2,
            Self::Dword => 4,
            Self::Qword => 8,
        }
    }
}

/// Field entry inside a `@struc` declaration.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct StrucField {
    /// Field identifier (e.g. `wEnvSeg`).
    pub name: String,
    /// Element scalar width.
    pub width: DataWidth,
    /// Element count (`1` for scalar, `>1` for fixed array).
    pub count: usize,
    /// Source location of the field.
    pub span: SourceSpan,
}

/// Calling/framing flavor of a `@proc` block.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ProcKind {
    /// Standard near procedure (`@proc`).
    Near,
    /// 16-bit far procedure (`@proc_far`).
    Far,
    /// Windows x86-64 SEH framed procedure (`@proc_frame name, stack_alloc[, nonvol...]`).
    Win64Frame {
        /// Fixed stack allocation in bytes (`sub rsp, N`; must be a multiple of 8).
        stack_alloc: u32,
        /// Non-volatile general-purpose registers pushed in the prologue.
        saved_regs: Vec<String>,
    },
}

/// Statement inside a `@proc` body or top-level section.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Stmt {
    /// Preserved ordinary comment line (`; ...`).
    Comment {
        /// Comment text without leading `;`.
        text: String,
        /// Source span.
        span: SourceSpan,
    },
    /// Local label `.name:` inside a procedure.
    LocalLabel {
        /// Label identifier without leading dot.
        name: String,
        /// Source span.
        span: SourceSpan,
    },
    /// Global/file-scope code or data label `name:`.
    GlobalLabel {
        /// Label identifier.
        name: String,
        /// Source span.
        span: SourceSpan,
    },
    /// Initialized data emission (`@data db|dw|dd|dq [name|-,] items...`).
    Data {
        /// Optional symbol label (`None` when `-` anonymous).
        label: Option<String>,
        /// Scalar width.
        width: DataWidth,
        /// Raw comma-separated item expressions/strings.
        items: String,
        /// Source span.
        span: SourceSpan,
    },
    /// Zero/uninitialized reservation (`@res byte|word|dword|qword name, count` or zero-filled `dup`).
    Reserve {
        /// Symbol label.
        label: String,
        /// Scalar width.
        width: DataWidth,
        /// Element count expression or integer.
        count_expr: String,
        /// Source span.
        span: SourceSpan,
    },
    /// Power-of-two zero-byte alignment (`@align N`).
    AlignZero {
        /// Alignment in bytes (e.g. `2`, `4`, `8`, `16`).
        align_bytes: usize,
        /// Source span.
        span: SourceSpan,
    },
    /// Structured Win64 epilogue + `ret` matching `@proc_frame` (`@epilog_ret`).
    Win64EpilogRet {
        /// Source span.
        span: SourceSpan,
    },
    /// CPU instruction (`mnemonic [op1, op2, ...]`).
    Instruction {
        /// Lowercase mnemonic (may include `rep`/`lock` prefix if separated, e.g. `rep movsw` or `mov`).
        mnemonic: String,
        /// Parsed comma-separated operand strings in canonical syntax.
        operands: Vec<String>,
        /// Source span.
        span: SourceSpan,
    },
}

/// Top-level declaration in a [`CanonicalModule`].
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum TopItem {
    /// `@include "rel/path.inc"`.
    Include {
        /// Relative path to included `.inc` file.
        path: String,
        /// Source span.
        span: SourceSpan,
    },
    /// `@section <kind>`.
    Section {
        /// Target section kind.
        kind: SectionKind,
        /// Source span.
        span: SourceSpan,
    },
    /// `@global <symbol>` export.
    Global {
        /// Exported symbol name.
        symbol: String,
        /// Source span.
        span: SourceSpan,
    },
    /// `@extern <symbol>, <kind>` external symbol or DLL IAT import (`kind` = `proc` | `dword` | `qword`).
    Extern {
        /// Symbol identifier.
        symbol: String,
        /// External symbol class (`proc`, `dword`, `qword`).
        kind: String,
        /// Source span.
        span: SourceSpan,
    },
    /// `@const NAME = EXPR` with documentation.
    Const {
        /// Attached `;; @const` documentation block.
        doc: DocBlock,
        /// Constant name.
        name: String,
        /// Constant expression.
        expr: String,
        /// Source span.
        span: SourceSpan,
    },
    /// `@struc NAME, EXPECTED_BYTES` definition with assemble-time size assertion.
    Struc {
        /// Attached `;; @struc` documentation block.
        doc: DocBlock,
        /// Structure name.
        name: String,
        /// Expected byte size verified both statically and at assemble time.
        expected_size: usize,
        /// Ordered structure fields.
        fields: Vec<StrucField>,
        /// Source span.
        span: SourceSpan,
    },
    /// `@proc` / `@proc_far` / `@proc_frame` procedure definition.
    Proc {
        /// Attached `;; @proc` documentation block.
        doc: DocBlock,
        /// Procedure identifier.
        name: String,
        /// Procedure framing kind.
        kind: ProcKind,
        /// Body statements.
        body: Vec<Stmt>,
        /// Source span.
        span: SourceSpan,
    },
    /// Top-level data, label, alignment, or entry trampoline statement outside `@proc`.
    Stmt(Stmt),
}

/// Complete parsed canonical assembly file (`.asm` or `.inc`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct CanonicalModule {
    /// Logical or physical file path.
    pub file_path: String,
    /// Top-of-file `;; @file`, `;; @isa`, `;; @desc` documentation block.
    pub file_doc: DocBlock,
    /// Declared `@cpu` ISA (`None` only for ISA-agnostic constant `.inc` headers).
    pub cpu: Option<CpuIsa>,
    /// Declared `@format` layout.
    pub format: BinaryFormat,
    /// Optional `@entry <symbol>` entrypoint for executable formats.
    pub entry: Option<String>,
    /// Optional `@safeseh <symbol>` IA-32 `/SAFESEH` exception handler symbol.
    pub safeseh: Option<String>,
    /// Ordered top-level items.
    pub items: Vec<TopItem>,
}
