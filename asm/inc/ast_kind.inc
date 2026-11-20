;; @file ast_kind.inc
;; @isa all
;; @desc Compact Arena AST node discriminants, binary operators, IF comparisons, FOR modes, and redirections.

@format inc

;; @const AST_NOP
;; @desc Empty statement, REM comment, or :label pseudo-node.
@const AST_NOP = 0x00

;; @const AST_SIMPLE_CMD
;; @desc Simple internal or external command invocation with optional redirection list.
@const AST_SIMPLE_CMD = 0x01

;; @const AST_BLOCK
;; @desc Parenthesized (...) statement list with optional outer redirections.
@const AST_BLOCK = 0x02

;; @const AST_BINARY
;; @desc Binary pipeline or conditional connector (|, &, &&, ||).
@const AST_BINARY = 0x03

;; @const AST_IF
;; @desc Conditional IF [NOT] [/I] ... (...) [ELSE (...)] node.
@const AST_IF = 0x04

;; @const AST_FOR
;; @desc Iteration FOR [plain | /D | /R | /L | /F] %%V IN (...) DO (...) node.
@const AST_FOR = 0x05

;; @const BINOP_SEQ
;; @desc Unconditional command separator (&).
@const BINOP_SEQ = 0x01

;; @const BINOP_AND
;; @desc Short-circuit success connector (&&).
@const BINOP_AND = 0x02

;; @const BINOP_OR
;; @desc Short-circuit failure connector (||).
@const BINOP_OR = 0x03

;; @const BINOP_PIPE
;; @desc Standard output to standard input pipeline (|).
@const BINOP_PIPE = 0x04

;; @const IFCMP_STR_EQ
;; @desc Classic string equality test (s1 == s2 or EQU fallback).
@const IFCMP_STR_EQ = 0x01

;; @const IFCMP_ERRORLEVEL
;; @desc Classic IF ERRORLEVEL n (>= n) test.
@const IFCMP_ERRORLEVEL = 0x02

;; @const IFCMP_EXIST
;; @desc Filesystem wildcard existence test (IF EXIST pat).
@const IFCMP_EXIST = 0x03

;; @const IFCMP_DEFINED
;; @desc Environment variable presence test (IF DEFINED name).
@const IFCMP_DEFINED = 0x04

;; @const IFCMP_CMDEXTVER
;; @desc Command extensions version test (IF CMDEXTVERSION n).
@const IFCMP_CMDEXTVER = 0x05

;; @const IFCMP_EQU
;; @desc Extended numeric/lexical equality (EQU).
@const IFCMP_EQU = 0x06

;; @const IFCMP_NEQ
;; @desc Extended numeric/lexical inequality (NEQ).
@const IFCMP_NEQ = 0x07

;; @const IFCMP_LSS
;; @desc Extended numeric/lexical strictly less-than (LSS).
@const IFCMP_LSS = 0x08

;; @const IFCMP_LEQ
;; @desc Extended numeric/lexical less-or-equal (LEQ).
@const IFCMP_LEQ = 0x09

;; @const IFCMP_GTR
;; @desc Extended numeric/lexical strictly greater-than (GTR).
@const IFCMP_GTR = 0x0A

;; @const IFCMP_GEQ
;; @desc Extended numeric/lexical greater-or-equal (GEQ).
@const IFCMP_GEQ = 0x0B

;; @const IFFLAG_NOT
;; @desc Negates IF condition result.
@const IFFLAG_NOT = 0x01

;; @const IFFLAG_NOCASE
;; @desc Case-insensitive comparison (/I).
@const IFFLAG_NOCASE = 0x02

;; @const FORMODE_PLAIN
;; @desc Classic wildcard/item FOR %%I IN (set) DO cmd.
@const FORMODE_PLAIN = 0x00

;; @const FORMODE_DIRS
;; @desc Directory wildcard FOR /D %%I IN (set) DO cmd.
@const FORMODE_DIRS = 0x01

;; @const FORMODE_RECURSE
;; @desc Recursive directory walk FOR /R [root] %%I IN (set) DO cmd.
@const FORMODE_RECURSE = 0x02

;; @const FORMODE_NUMERIC
;; @desc Arithmetic sequence FOR /L %%I IN (start,step,end) DO cmd.
@const FORMODE_NUMERIC = 0x03

;; @const FORMODE_PARSE
;; @desc Tokenizing file/string/command reader FOR /F ["opts"] %%I IN (...) DO cmd.
@const FORMODE_PARSE = 0x04

;; @const REDIR_IN
;; @desc Input redirection (n<file).
@const REDIR_IN = 0x01

;; @const REDIR_OUT_TRUNC
;; @desc Truncating output redirection (n>file).
@const REDIR_OUT_TRUNC = 0x02

;; @const REDIR_OUT_APPEND
;; @desc Appending output redirection (n>>file).
@const REDIR_OUT_APPEND = 0x03

;; @const REDIR_DUP_OUT
;; @desc Output descriptor duplication (n>&m).
@const REDIR_DUP_OUT = 0x04

;; @const REDIR_DUP_IN
;; @desc Input descriptor duplication (n<&m).
@const REDIR_DUP_IN = 0x05

;; @struc AST_REDIR_NODE
;; @desc 8-byte arena node representing a single FD 0..9 I/O redirection or descriptor dup.
@struc AST_REDIR_NODE, 8
    @field bKind, byte, 1
    @field bFd, byte, 1
    @field wTarget, word, 1
    @field wNext, word, 1
    @field wPad, word, 1
@endstruc

;; @struc AST_NODE
;; @desc 16-byte compact tagged-union Arena AST node for statements, blocks, IF, FOR, and connectors.
@struc AST_NODE, 16
    @field bKind, byte, 1
    @field bSub, byte, 1
    @field wFlags, word, 1
    @field wA, word, 1
    @field wB, word, 1
    @field wC, word, 1
    @field wD, word, 1
    @field wNext, word, 1
    @field wReserved, word, 1
@endstruc
