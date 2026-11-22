;; @file a32_equ.inc
;; @isa armv4
;; @desc Shared 32-bit ARM (ARMv4 WinCE & ARMv7-A Thumb-2 ARMNT) equates, string IDs, and asserted structures.

@cpu armv4
@format inc

;; @const A32_ARENA_STD
;; @desc Default 4 MiB VirtualAlloc MEM_RESERVE arena capacity on ARM32 Windows RT/10.
@const A32_ARENA_STD = 0x00400000

;; @const A32_ARENA_LOW
;; @desc Low-RAM 256 KiB VirtualAlloc fallback arena capacity for Windows CE / Pocket PC ARMv4.
@const A32_ARENA_LOW = 0x00040000

;; @const A32_ARENA_GRAN
;; @desc 64 KiB on-demand VirtualAlloc MEM_COMMIT page chunk granularity.
@const A32_ARENA_GRAN = 0x00010000

;; @const A32_ENV_CAP
;; @desc 64 KiB WTF-8 active environment block byte capacity (KEY=VALUE\0...\0\0).
@const A32_ENV_CAP = 0x00010000

;; @const A32_SETLOCAL_MAX
;; @desc Maximum nested SETLOCAL snapshot depth (32 levels).
@const A32_SETLOCAL_MAX = 32

;; @const A32_BATCH_MAX
;; @desc Maximum nested CALL batch/subroutine frame depth (32 levels).
@const A32_BATCH_MAX = 32

;; @const A32_PUSHD_MAX
;; @desc Maximum nested PUSHD directory stack depth (32 entries).
@const A32_PUSHD_MAX = 32

;; @const A32_FOR_MAX
;; @desc Maximum nested FOR %%V variable scope bindings (16 active variables).
@const A32_FOR_MAX = 16

;; @const A32_HIST_MAX
;; @desc Maximum interactive DOSKEY history ring entries (32 lines).
@const A32_HIST_MAX = 32

;; @const A32_MACRO_MAX
;; @desc Maximum DOSKEY macro definitions (32 entries).
@const A32_MACRO_MAX = 32

;; @const A32_LINE_MAX
;; @desc Maximum logical batch/command line byte length (16 KiB WTF-8).
@const A32_LINE_MAX = 16384

;; @const A32_PATH_MAX
;; @desc Maximum WTF-8 path buffer size supporting \?\ 32K WCHAR paths.
@const A32_PATH_MAX = 65536

;; @const A32_VM86_MEM_SIZE
;; @desc 1 MiB + 64 KiB (1,114,112 bytes) isolated virtual 8086 address space.
@const A32_VM86_MEM_SIZE = 0x00110000

;; @const A32_REDIR_IN
;; @desc Input redirection operator `<`.
@const A32_REDIR_IN = 1

;; @const A32_REDIR_OUT
;; @desc Truncating output redirection operator `>`.
@const A32_REDIR_OUT = 2

;; @const A32_REDIR_APP
;; @desc Appending output redirection operator `>>`.
@const A32_REDIR_APP = 3

;; @const A32_REDIR_DUP_OUT
;; @desc Output handle duplication operator `n>&m`.
@const A32_REDIR_DUP_OUT = 4

;; @const A32_REDIR_DUP_IN
;; @desc Input handle duplication operator `n<&m`.
@const A32_REDIR_DUP_IN = 5

;; @const A32_IF_CMP_EQ
;; @desc IF string equality `==` / `EQU` subtype.
@const A32_IF_CMP_EQ = 0

;; @const ERR_SYNTAX_ERROR
;; @desc Canonical syntax error alias (ERR_SYNTAX = 0x0100).
@const ERR_SYNTAX_ERROR = 0x0100

;; @const FILE_ATTRIBUTE_READONLY
;; @desc Win32 read-only file attribute bit (0x01).
@const FILE_ATTRIBUTE_READONLY = 0x00000001

;; @const FILE_ATTRIBUTE_HIDDEN
;; @desc Win32 hidden file attribute bit (0x02).
@const FILE_ATTRIBUTE_HIDDEN = 0x00000002

;; @const FILE_ATTRIBUTE_SYSTEM
;; @desc Win32 system file attribute bit (0x04).
@const FILE_ATTRIBUTE_SYSTEM = 0x00000004

;; @const FILE_ATTRIBUTE_DIRECTORY
;; @desc Win32 directory file attribute bit (0x10).
@const FILE_ATTRIBUTE_DIRECTORY = 0x00000010

;; @const FILE_ATTRIBUTE_ARCHIVE
;; @desc Win32 archive file attribute bit (0x20).
@const FILE_ATTRIBUTE_ARCHIVE = 0x00000020

;; @const FILE_ATTRIBUTE_NORMAL
;; @desc Win32 normal file attribute bit (0x80).
@const FILE_ATTRIBUTE_NORMAL = 0x00000080

;; @const DEV_NONE
;; @desc Path is not a reserved character device (0).
@const DEV_NONE = 0

;; @const DEV_NUL
;; @desc Reserved NUL character device (1).
@const DEV_NUL = 1

;; @const DEV_CON
;; @desc Reserved CON/CONIN$/CONOUT$ character device (2).
@const DEV_CON = 2

;; @const DEV_OTHER
;; @desc Reserved PRN/AUX/CLOCK$/COM1..9/LPT1..9 character device (3).
@const DEV_OTHER = 3

;; @const A32_IF_CMP_NEQ
;; @desc IF comparison `NEQ` subtype.
@const A32_IF_CMP_NEQ = 1

;; @const A32_IF_CMP_LSS
;; @desc IF comparison `LSS` subtype.
@const A32_IF_CMP_LSS = 2

;; @const A32_IF_CMP_LEQ
;; @desc IF comparison `LEQ` subtype.
@const A32_IF_CMP_LEQ = 3

;; @const A32_IF_CMP_GTR
;; @desc IF comparison `GTR` subtype.
@const A32_IF_CMP_GTR = 4

;; @const A32_IF_CMP_GEQ
;; @desc IF comparison `GEQ` subtype.
@const A32_IF_CMP_GEQ = 5

;; @const A32_IF_ERRORLEVEL
;; @desc IF ERRORLEVEL n condition subtype.
@const A32_IF_ERRORLEVEL = 6

;; @const A32_IF_EXIST
;; @desc IF EXIST path condition subtype.
@const A32_IF_EXIST = 7

;; @const A32_IF_DEFINED
;; @desc IF DEFINED var condition subtype.
@const A32_IF_DEFINED = 8

;; @const A32_IF_CMDEXTVER
;; @desc IF CMDEXTVERSION n condition subtype.
@const A32_IF_CMDEXTVER = 9

;; @const A32_FOR_PLAIN
;; @desc Standard FOR %%I IN (set) DO cmd mode.
@const A32_FOR_PLAIN = 0

;; @const A32_FOR_DIRS
;; @desc Directory FOR /D %%I IN (set) DO cmd mode.
@const A32_FOR_DIRS = 1

;; @const A32_FOR_REC
;; @desc Recursive FOR /R [root] %%I IN (set) DO cmd mode.
@const A32_FOR_REC = 2

;; @const A32_FOR_LOOP
;; @desc Numeric sequence FOR /L %%I IN (start,step,end) DO cmd mode.
@const A32_FOR_LOOP = 3

;; @const A32_FOR_PARSE
;; @desc Tokenizing FOR /F ["opts"] %%I IN (...) DO cmd mode.
@const A32_FOR_PARSE = 4

;; @const AST_SEQ
;; @desc Unconditional command separator `&` AST node kind.
@const AST_SEQ = 0x06

;; @const AST_AND
;; @desc Short-circuit success `&&` AST node kind.
@const AST_AND = 0x07

;; @const AST_OR
;; @desc Short-circuit failure `||` AST node kind.
@const AST_OR = 0x08

;; @const AST_PIPE
;; @desc Pipeline `|` AST node kind.
@const AST_PIPE = 0x09

;; @const AST_REM
;; @desc Comment `REM` AST node kind.
@const AST_REM = 0x0A

;; @const AST_FLAG_NO_ECHO
;; @desc Leading `@` per-statement echo suppression flag.
@const AST_FLAG_NO_ECHO = 0x01

;; @const AST_FLAG_IF_NOT
;; @desc `IF NOT` condition inversion flag.
@const AST_FLAG_IF_NOT = 0x02

;; @const AST_FLAG_IF_ICASE
;; @desc `IF /I` case-insensitive comparison flag.
@const AST_FLAG_IF_ICASE = 0x04

;; @const AST_FLAG_ECHO_DELIM
;; @desc Explicit `ECHO.` / `ECHO(` delimiter flag.
@const AST_FLAG_ECHO_DELIM = 0x10

;; @const AST_NODE_A32_SIZE
;; @desc Byte size of AST_NODE_A32 (32 bytes).
@const AST_NODE_A32_SIZE = 32

;; @const REDIR_NODE_A32_SIZE
;; @desc Byte size of REDIR_NODE_A32 (16 bytes).
@const REDIR_NODE_A32_SIZE = 16

;; @const BATCH_FRAME_A32_SIZE
;; @desc Byte size of BATCH_FRAME_A32 (64 bytes).
@const BATCH_FRAME_A32_SIZE = 64

;; @const FOR_VAR_A32_SIZE
;; @desc Byte size of FOR_VAR_A32 (8 bytes).
@const FOR_VAR_A32_SIZE = 8

;; @struc AST_NODE_A32
;; @desc 32-byte 8-byte-aligned 32-bit Arena AST node shared across ARMv4 and ARMv7.
@struc AST_NODE_A32, 32
    @field bKind, byte, 1
    @field bFlags, byte, 1
    @field bSubKind, byte, 1
    @field bForVar, byte, 1
    @field dwArgc, dword, 1
    @field pArgs, dword, 1
    @field pRedir, dword, 1
    @field pLeft, dword, 1
    @field pRight, dword, 1
    @field pExtra, dword, 1
    @field pStr1, dword, 1
@endstruc

;; @struc REDIR_NODE_A32
;; @desc 16-byte linked redirection descriptor (`FD 0..9`, `<`, `>`, `>>`, `2>&1`).
@struc REDIR_NODE_A32, 16
    @field bKind, byte, 1
    @field bFd, byte, 1
    @field bTargetFd, byte, 1
    @field bPad, byte, 1
    @field pTarget, dword, 1
    @field pNext, dword, 1
    @field dwReserved, dword, 1
@endstruc

;; @struc BATCH_FRAME_A32
;; @desc 64-byte stateless byte-offset batch/subroutine execution frame (`0` open handles during exec).
@struc BATCH_FRAME_A32, 64
    @field pFilePath, dword, 1
    @field uFileOffLo, dword, 1
    @field uFileOffHi, dword, 1
    @field dwArgCount, dword, 1
    @field dwShiftBase, dword, 1
    @field pArgVec, dword, 1
    @field pRawArgs, dword, 1
    @field dwSetlocalBase, dword, 1
    @field bIsCallSub, byte, 1
    @field bBomKind, byte, 1
    @field wPad, word, 1
    @field dwSavedCodePage, dword, 1
    @field dwReserved, dword, 6
@endstruc

;; @struc SETLOCAL_SNAP_A32
;; @desc 16-byte SETLOCAL snapshot saving environment block copy, CWD copy, and shell mode flags.
@struc SETLOCAL_SNAP_A32, 16
    @field pEnvCopy, dword, 1
    @field dwEnvLen, dword, 1
    @field pCwdCopy, dword, 1
    @field bExtFlag, byte, 1
    @field bDelayedFlag, byte, 1
    @field wPad, word, 1
@endstruc

;; @struc FOR_VAR_A32
;; @desc 8-byte active FOR loop variable binding (`%%A`..`%%z`).
@struc FOR_VAR_A32, 8
    @field bVarChar, byte, 1
    @field bActive, byte, 1
    @field wPad, word, 1
    @field pValue, dword, 1
@endstruc

;; @struc FIND_INFO_A32
;; @desc 320-byte normalized WTF-8 directory entry record returned by HAL FindFirst/FindNext.
@struc FIND_INFO_A32, 320
    @field dwAttr, dword, 1
    @field uSizeLo, dword, 1
    @field uSizeHi, dword, 1
    @field ftWriteLo, dword, 1
    @field ftWriteHi, dword, 1
    @field dwReparseTag, dword, 1
    @field szShort, byte, 24
    @field szName, byte, 272
@endstruc

;; @struc VM86_CTX_A32
;; @desc 192-byte embedded 8086 virtual CPU + DOS PSP/DTA/handle state block for ARM32 vm8086.
@struc VM86_CTX_A32, 192
    @field pMem, dword, 1
    @field regs16, word, 8
    @field sregs16, word, 4
    @field regIp, word, 1
    @field regFlags, word, 1
    @field wPspSeg, word, 1
    @field wDtaSeg, word, 1
    @field wDtaOff, word, 1
    @field bHalted, byte, 1
    @field bExitCode, byte, 1
    @field bSegOvr, byte, 1
    @field bRepMode, byte, 1
    @field wPad, word, 1
    @field dwStepsLeft, dword, 1
    @field hFindGuest, dword, 1
    @field hostHandles, dword, 20
    @field dwReserved, dword, 15
@endstruc

;; @struc ARM32_CTX
;; @desc 2048-byte central 8-byte-aligned ARM32 shell runtime context (`offset < 2048` for single-insn LDR/STR!).
@struc ARM32_CTX, 2048
    @field pArenaBase, dword, 1
    @field pArenaPtr, dword, 1
    @field pArenaCommit, dword, 1
    @field pArenaLimit, dword, 1
    @field pEnvBlock, dword, 1
    @field dwEnvLen, dword, 1
    @field dwEnvCap, dword, 1
    @field pPermPoolPtr, dword, 1
    @field pPermPoolEnd, dword, 1
    @field hStdIn, dword, 1
    @field hStdOut, dword, 1
    @field hStdErr, dword, 1
    @field dwLastErrorLevel, dword, 1
    @field dwExitCode, dword, 1
    @field bShouldExit, byte, 1
    @field bEchoOn, byte, 1
    @field bExtensions, byte, 1
    @field bDelayedExp, byte, 1
    @field bUnicodeOut, byte, 1
    @field bCtrlBreak, byte, 1
    @field bIsWinCe, byte, 1
    @field bVerifyOn, byte, 1
    @field bFileCompl, byte, 1
    @field bKeysOn, byte, 1
    @field bBreakOn, byte, 1
    @field bVtEnabled, byte, 1
    @field dwCodePage, dword, 1
    @field dwOsMajor, dword, 1
    @field dwOsMinor, dword, 1
    @field dwOsBuild, dword, 1
    @field dwRandomSeed, dword, 1
    @field wDefaultAttr, word, 1
    @field wCurrentAttr, word, 1
    @field dwBatchDepth, dword, 1
    @field dwSetlocalDepth, dword, 1
    @field dwPushdDepth, dword, 1
    @field dwForDepth, dword, 1
    @field dwPipeSeq, dword, 1
    @field dwMacroCount, dword, 1
    @field dwHistCount, dword, 1
    @field pCmdLineRaw, dword, 1
    @field pSelfExePath, dword, 1
    @field pBatchStack, dword, 1
    @field pSetlocalStack, dword, 1
    @field pPushdStack, dword, 1
    @field pForStack, dword, 1
    @field pMacroNames, dword, 1
    @field pMacroBodies, dword, 1
    @field pHistRing, dword, 1
    @field fdTable, dword, 10
    @field pfnRtlGetVersion, dword, 1
    @field pfnRegOpenKeyExW, dword, 1
    @field pfnRegCreateKeyExW, dword, 1
    @field pfnRegQueryValueExW, dword, 1
    @field pfnRegSetValueExW, dword, 1
    @field pfnRegDeleteValueW, dword, 1
    @field pfnRegEnumValueW, dword, 1
    @field pfnRegCloseKey, dword, 1
    @field pfnShellExecuteExW, dword, 1
    @field pfnCreateSymbolicLinkW, dword, 1
    @field pfnCreateHardLinkW, dword, 1
    @field pfnCreatePseudoConsole, dword, 1
    @field pfnGetMachineTypeAttributes, dword, 1
    @field pfnNtQueryInformationFile, dword, 1
    @field bmDbcsLead, byte, 32
    @field szCwd, byte, 512
    @field szTitle, byte, 256
    @field drvCwdsPtrs, dword, 26
    @field scratchBuf, byte, 892
@endstruc
