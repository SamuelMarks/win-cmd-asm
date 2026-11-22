;; @file a64_equ.inc
;; @isa arm64
;; @desc Shared 64-bit AArch64 (PE32+ 0xAA64, Windows 10 & Windows 11 on ARM) constants and 64-bit pointer-aligned STRUCs.

@cpu arm64
@format inc

;; @const A64_ARENA_RESERVE
;; @desc Default 4 MiB statement bump-arena reservation via VirtualAlloc(MEM_RESERVE).
@const A64_ARENA_RESERVE = 4194304

;; @const A64_ARENA_CHUNK
;; @desc 64 KiB on-demand MEM_COMMIT increment for the statement bump-arena.
@const A64_ARENA_CHUNK = 65536

;; @const A64_PERM_INIT_CAP
;; @desc Initial 64 KiB persistent pool for environment, SETLOCAL, PUSHD, and DOSKEY state.
@const A64_PERM_INIT_CAP = 65536

;; @const A64_LINE_MAX
;; @desc Maximum 16 KiB logical batch/command line byte capacity.
@const A64_LINE_MAX = 16384

;; @const A64_PATH_MAX
;; @desc Maximum 64 KiB WTF-8 path buffer supporting \?\ 32,767-WCHAR paths.
@const A64_PATH_MAX = 65536

;; @const A64_VM86_MEM_SIZE
;; @desc Isolated 1 MiB + 64 KiB (1,114,112-byte) 8086 + HMA virtual address space.
@const A64_VM86_MEM_SIZE = 1114112

;; @const A64_BATCH_MAX
;; @desc Maximum nested `.BAT`/`.CMD` and `CALL :label` frames (32).
@const A64_BATCH_MAX = 32

;; @const A64_SETLOCAL_MAX
;; @desc Maximum nested `SETLOCAL` snapshots (32).
@const A64_SETLOCAL_MAX = 32

;; @const A64_PUSHD_MAX
;; @desc Maximum nested `PUSHD` directory stack entries (32).
@const A64_PUSHD_MAX = 32

;; @const A64_FOR_MAX
;; @desc Maximum active nested `FOR` variable slots (32).
@const A64_FOR_MAX = 32

;; @const A64_HIST_MAX
;; @desc Maximum interactive `DOSKEY` command history entries (64).
@const A64_HIST_MAX = 64

;; @const A64_MACRO_MAX
;; @desc Maximum registered `DOSKEY` macro definitions (32).
@const A64_MACRO_MAX = 32

;; @const A64_ENV_MAX
;; @desc Maximum 64 KiB mutable WTF-8 environment block byte capacity.
@const A64_ENV_MAX = 65536

;; @const A64_REDIR_IN
;; @desc Input redirection `<` kind.
@const A64_REDIR_IN = 1

;; @const A64_REDIR_OUT
;; @desc Truncating output redirection `>` kind.
@const A64_REDIR_OUT = 2

;; @const A64_REDIR_APP
;; @desc Appending output redirection `>>` kind.
@const A64_REDIR_APP = 3

;; @const A64_REDIR_DUP_OUT
;; @desc Handle duplication `n>&m` kind.
@const A64_REDIR_DUP_OUT = 4

;; @const A64_REDIR_DUP_IN
;; @desc Handle duplication `n<&m` kind.
@const A64_REDIR_DUP_IN = 5

;; @const A64_IF_CMP_EQ
;; @desc IF equality `==` / `EQU` comparison subtype.
@const A64_IF_CMP_EQ = 0

;; @const A64_IF_CMP_NEQ
;; @desc IF `NEQ` comparison subtype.
@const A64_IF_CMP_NEQ = 1

;; @const A64_IF_CMP_LSS
;; @desc IF `LSS` comparison subtype.
@const A64_IF_CMP_LSS = 2

;; @const A64_IF_CMP_LEQ
;; @desc IF `LEQ` comparison subtype.
@const A64_IF_CMP_LEQ = 3

;; @const A64_IF_CMP_GTR
;; @desc IF `GTR` comparison subtype.
@const A64_IF_CMP_GTR = 4

;; @const A64_IF_CMP_GEQ
;; @desc IF `GEQ` comparison subtype.
@const A64_IF_CMP_GEQ = 5

;; @const A64_IF_ERRORLEVEL
;; @desc IF `ERRORLEVEL n` unary subtype.
@const A64_IF_ERRORLEVEL = 6

;; @const A64_IF_EXIST
;; @desc IF `EXIST path` unary subtype.
@const A64_IF_EXIST = 7

;; @const A64_IF_DEFINED
;; @desc IF `DEFINED var` unary subtype.
@const A64_IF_DEFINED = 8

;; @const A64_IF_CMDEXTVER
;; @desc IF `CMDEXTVERSION n` unary subtype.
@const A64_IF_CMDEXTVER = 9

;; @const A64_FOR_PLAIN
;; @desc Plain `FOR %%I IN (set)` subtype.
@const A64_FOR_PLAIN = 0

;; @const A64_FOR_DIRS
;; @desc Directory `FOR /D %%I IN (set)` subtype.
@const A64_FOR_DIRS = 1

;; @const A64_FOR_REC
;; @desc Recursive `FOR /R [root] %%I IN (set)` subtype.
@const A64_FOR_REC = 2

;; @const A64_FOR_LOOP
;; @desc Numeric `FOR /L %%I IN (start,step,end)` subtype.
@const A64_FOR_LOOP = 3

;; @const A64_FOR_PARSE
;; @desc Tokenizing `FOR /F ["opts"] %%I IN (...)` subtype.
@const A64_FOR_PARSE = 4

;; @const AST_REM
;; @desc `REM` comment AST node kind.
@const AST_REM = 0x0A

;; @const AST_FLAG_NO_ECHO
;; @desc Leading `@` echo suppression flag on an AST node.
@const AST_FLAG_NO_ECHO = 0x01

;; @const AST_FLAG_IF_NOT
;; @desc `IF NOT` condition inversion flag.
@const AST_FLAG_IF_NOT = 0x02

;; @const AST_FLAG_IF_ICASE
;; @desc `IF /I` case-insensitive comparison flag.
@const AST_FLAG_IF_ICASE = 0x04

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
;; @desc Reserved CON / CONIN$ / CONOUT$ character device (2).
@const DEV_CON = 2

;; @const DEV_OTHER
;; @desc Reserved AUX / PRN / CLOCK$ / COM1..9 / LPT1..9 character device (3).
@const DEV_OTHER = 3

;; @struc AST_NODE_A64
;; @desc 64-byte 16-byte-aligned 64-bit pointer Arena AST node for AArch64.
@struc AST_NODE_A64, 64
    @field bKind, byte, 1
    @field bFlags, byte, 1
    @field bSubKind, byte, 1
    @field bForVar, byte, 1
    @field dwArgc, dword, 1
    @field pArgv, qword, 1
    @field pLeft, qword, 1
    @field pRight, qword, 1
    @field pExtra, qword, 1
    @field pRedir, qword, 1
    @field pStr1, qword, 1
    @field qwReserved, qword, 1
@endstruc

;; @struc REDIR_NODE_A64
;; @desc 32-byte 16-byte-aligned I/O redirection descriptor node for FD 0..9.
@struc REDIR_NODE_A64, 32
    @field bKind, byte, 1
    @field bSrcFd, byte, 1
    @field bDstFd, byte, 1
    @field bPad, byte, 5
    @field pTarget, qword, 1
    @field pNext, qword, 1
    @field qwReserved, qword, 1
@endstruc

;; @struc BATCH_FRAME_A64
;; @desc 64-byte stateless byte-offset batch/subroutine frame descriptor on AArch64.
@struc BATCH_FRAME_A64, 64
    @field pFilePath, qword, 1
    @field uFileOff, qword, 1
    @field pArgVec, qword, 1
    @field pRawArgs, qword, 1
    @field dwArgCount, dword, 1
    @field dwShiftOff, dword, 1
    @field dwSetlocalBase, dword, 1
    @field dwSavedCodePage, dword, 1
    @field bIsCallSub, byte, 1
    @field bBomKind, byte, 1
    @field bPad, byte, 14
@endstruc

;; @struc SETLOCAL_SNAP_A64
;; @desc 32-byte SETLOCAL snapshot saving environment block copy, CWD copy, and shell flags.
@struc SETLOCAL_SNAP_A64, 32
    @field pEnvCopy, qword, 1
    @field pCwdCopy, qword, 1
    @field dwEnvLen, dword, 1
    @field bExtFlag, byte, 1
    @field bDelayedFlag, byte, 1
    @field wPad, word, 1
    @field qwPad, qword, 1
@endstruc

;; @struc FOR_VAR_A64
;; @desc 16-byte active FOR loop variable binding (`%%I` -> WTF-8 value pointer).
@struc FOR_VAR_A64, 16
    @field bVarChar, byte, 1
    @field bActive, byte, 1
    @field wPad, word, 3
    @field pValue, qword, 1
@endstruc

;; @struc FIND_INFO_A64
;; @desc 320-byte normalized directory enumeration result on AArch64.
@struc FIND_INFO_A64, 320
    @field dwAttr, dword, 1
    @field dwPad0, dword, 1
    @field uSize, qword, 1
    @field uWriteTime, qword, 1
    @field dwReparseTag, dword, 1
    @field szShort, byte, 20
    @field szName, byte, 272
@endstruc

;; @struc VM86_CTX_A64
;; @desc 256-byte AArch64 virtual 8086 CPU + DOS PSP/DTA + 64-bit host handle table.
@struc VM86_CTX_A64, 256
    @field pMem, qword, 1
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
    @field wPad0, word, 1
    @field dwStepsLeft, dword, 1
    @field dwPad1, dword, 1
    @field hostHandles, qword, 20
    @field qwReserved, qword, 5
@endstruc

;; @struc ARM64_CTX
;; @desc 2,560-byte master global runtime state for WINCMD_ARM64.EXE (X18 TEB strictly untouched!).
@struc ARM64_CTX, 2560
    @field pArenaBase, qword, 1
    @field pArenaPtr, qword, 1
    @field pArenaCommitEnd, qword, 1
    @field pArenaLimit, qword, 1
    @field pEnvBlock, qword, 1
    @field pPermPoolPtr, qword, 1
    @field pPermPoolEnd, qword, 1
    @field hStdIn, qword, 1
    @field hStdOut, qword, 1
    @field hStdErr, qword, 1
    @field pCmdLineRaw, qword, 1
    @field pSelfExePath, qword, 1
    @field pBatchStack, qword, 1
    @field pSetlocalStack, qword, 1
    @field pPushdStack, qword, 1
    @field pForStack, qword, 1
    @field pHistRing, qword, 1
    @field pMacroNames, qword, 1
    @field pMacroBodies, qword, 1
    @field fdTable, qword, 10
    @field pfnRtlGetVersion, qword, 1
    @field pfnCreateSymbolicLinkW, qword, 1
    @field pfnCreateHardLinkW, qword, 1
    @field pfnCreatePseudoConsole, qword, 1
    @field pfnGetMachineTypeAttributes, qword, 1
    @field pfnNtQueryInformationFile, qword, 1
    @field pfnRegOpenKeyExW, qword, 1
    @field pfnRegCreateKeyExW, qword, 1
    @field pfnRegQueryValueExW, qword, 1
    @field pfnRegSetValueExW, qword, 1
    @field pfnRegCloseKey, qword, 1
    @field drvCwdsPtrs, qword, 26
    @field dwEnvLen, dword, 1
    @field dwEnvCap, dword, 1
    @field dwLastErrorLevel, dword, 1
    @field dwExitCode, dword, 1
    @field dwCodePage, dword, 1
    @field dwOsMajor, dword, 1
    @field dwOsMinor, dword, 1
    @field dwOsBuild, dword, 1
    @field dwRandomSeed, dword, 1
    @field dwBatchDepth, dword, 1
    @field dwSetlocalDepth, dword, 1
    @field dwPushdDepth, dword, 1
    @field dwForDepth, dword, 1
    @field dwHistCount, dword, 1
    @field dwMacroCount, dword, 1
    @field dwPad0, dword, 1
    @field wDefaultAttr, word, 1
    @field wCurrentAttr, word, 1
    @field bShouldExit, byte, 1
    @field bEchoOn, byte, 1
    @field bExtensions, byte, 1
    @field bDelayedExp, byte, 1
    @field bVerifyFlag, byte, 1
    @field bCtrlBreak, byte, 1
    @field bAutoFail, byte, 1
    @field bUnicodeOut, byte, 1
    @field bVtEnabled, byte, 1
    @field bStripQuotesS, byte, 1
    @field bSkipAutoRun, byte, 1
    @field bFileCompl, byte, 1
    @field bBreakOn, byte, 1
    @field bPadFlags, byte, 7
    @field szCwd, byte, 512
    @field szTitle, byte, 256
    @field scratchBuf, byte, 1176
@endstruc
