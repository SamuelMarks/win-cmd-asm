;; @file w64_equ.inc
;; @isa x64
;; @desc Win64 (XP Pro x64 / Server 2003 SP1 .. Windows 11) equates and assemble-time asserted 64-bit structures.

@cpu x64
@format inc

;; @const W64_ARENA_RESERVE_STD
;; @desc Default 8 MiB VirtualAlloc MEM_RESERVE arena size on 64-bit Windows.
@const W64_ARENA_RESERVE_STD = 0x00800000

;; @const W64_ARENA_RESERVE_LOW
;; @desc Fallback 1 MiB VirtualAlloc MEM_RESERVE arena size.
@const W64_ARENA_RESERVE_LOW = 0x00100000

;; @const W64_ARENA_COMMIT_GRAN
;; @desc 64 KiB on-demand MEM_COMMIT page chunk granularity.
@const W64_ARENA_COMMIT_GRAN = 0x00010000

;; @const W64_ENV_CAP_BYTES
;; @desc 64 KiB WTF-8 active environment block capacity (KEY=VALUE\0...\0\0).
@const W64_ENV_CAP_BYTES = 0x00010000

;; @const W64_SETLOCAL_MAX
;; @desc Maximum nested SETLOCAL stack depth (32 frames, matching cmd.exe).
@const W64_SETLOCAL_MAX = 32

;; @const W64_BATCH_MAX
;; @desc Maximum nested CALL batch/subroutine frame depth (32 frames).
@const W64_BATCH_MAX = 32

;; @const W64_PUSHD_MAX
;; @desc Maximum nested PUSHD directory/UNC mapping stack depth (32 entries).
@const W64_PUSHD_MAX = 32

;; @const W64_HIST_MAX
;; @desc Maximum interactive history ring lines (64 entries).
@const W64_HIST_MAX = 64

;; @const W64_MACRO_MAX
;; @desc Maximum DOSKEY macro definitions (32 entries).
@const W64_MACRO_MAX = 32

;; @const W64_FOR_SCOPE_MAX
;; @desc Maximum nested FOR %%V loop variable bindings (16 active variables).
@const W64_FOR_SCOPE_MAX = 16

;; @const W64_LINE_MAX
;; @desc Maximum logical batch/command line byte capacity (16 KiB WTF-8, >= 8191 cmd.exe limit).
@const W64_LINE_MAX = 16384

;; @const W64_PATH_MAX_WTF8
;; @desc Maximum WTF-8 path buffer capacity supporting 32,767-WCHAR NT long paths (98,320 bytes).
@const W64_PATH_MAX_WTF8 = 98320

;; @const INVALID_HANDLE_VALUE
;; @desc Win64 INVALID_HANDLE_VALUE (-1 = 0xFFFFFFFFFFFFFFFF).
@const INVALID_HANDLE_VALUE = -1

;; @const INVALID_FILE_ATTRIBUTES
;; @desc Win32/Win64 INVALID_FILE_ATTRIBUTES (0xFFFFFFFF).
@const INVALID_FILE_ATTRIBUTES = -1

;; @const FILE_ATTRIBUTE_READONLY
;; @desc Read-only file attribute bit (0x01).
@const FILE_ATTRIBUTE_READONLY = 0x00000001

;; @const FILE_ATTRIBUTE_HIDDEN
;; @desc Hidden file attribute bit (0x02).
@const FILE_ATTRIBUTE_HIDDEN = 0x00000002

;; @const FILE_ATTRIBUTE_SYSTEM
;; @desc System file attribute bit (0x04).
@const FILE_ATTRIBUTE_SYSTEM = 0x00000004

;; @const FILE_ATTRIBUTE_DIRECTORY
;; @desc Directory attribute bit (0x10).
@const FILE_ATTRIBUTE_DIRECTORY = 0x00000010

;; @const FILE_ATTRIBUTE_ARCHIVE
;; @desc Archive attribute bit (0x20).
@const FILE_ATTRIBUTE_ARCHIVE = 0x00000020

;; @const FILE_ATTRIBUTE_NORMAL
;; @desc Normal attribute bit (0x80).
@const FILE_ATTRIBUTE_NORMAL = 0x00000080

;; @const FILE_FLAG_BACKUP_SEMANTICS
;; @desc CreateFileW flag required to open directory/reparse handles on NT (0x02000000).
@const FILE_FLAG_BACKUP_SEMANTICS = 0x02000000

;; @const FILE_FLAG_OPEN_REPARSE_POINT
;; @desc CreateFileW flag opening the reparse link itself without traversal (0x00200000).
@const FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000

;; @const TRUNCATE_EXISTING
;; @desc CreateFileW TRUNCATE_EXISTING disposition (5).
@const TRUNCATE_EXISTING = 5

;; @const FILE_BEGIN
;; @desc SetFilePointerEx FILE_BEGIN origin (0).
@const FILE_BEGIN = 0

;; @const FILE_CURRENT
;; @desc SetFilePointerEx FILE_CURRENT origin (1).
@const FILE_CURRENT = 1

;; @const FILE_END
;; @desc SetFilePointerEx FILE_END origin (2).
@const FILE_END = 2

;; @const DUPLICATE_CLOSE_SOURCE
;; @desc DuplicateHandle DUPLICATE_CLOSE_SOURCE flag (0x01).
@const DUPLICATE_CLOSE_SOURCE = 0x00000001

;; @const DUPLICATE_SAME_ACCESS
;; @desc DuplicateHandle DUPLICATE_SAME_ACCESS flag (0x02).
@const DUPLICATE_SAME_ACCESS = 0x00000002

;; @const STARTF_USESHOWWINDOW
;; @desc STARTUPINFOW STARTF_USESHOWWINDOW flag (0x00000001).
@const STARTF_USESHOWWINDOW = 0x00000001

;; @const STARTF_USESTDHANDLES
;; @desc STARTUPINFOW STARTF_USESTDHANDLES flag (0x00000100).
@const STARTF_USESTDHANDLES = 0x00000100

;; @const CREATE_NEW_CONSOLE
;; @desc CreateProcessW CREATE_NEW_CONSOLE flag (0x00000010).
@const CREATE_NEW_CONSOLE = 0x00000010

;; @const CREATE_UNICODE_ENVIRONMENT
;; @desc CreateProcessW UTF-16LE environment block flag (0x00000400).
@const CREATE_UNICODE_ENVIRONMENT = 0x00000400

;; @const CREATE_NO_WINDOW
;; @desc START /B background console-sharing flag (0x08000000).
@const CREATE_NO_WINDOW = 0x08000000

;; @const INFINITE_WAIT
;; @desc WaitForSingleObject INFINITE timeout (0xFFFFFFFF).
@const INFINITE_WAIT = -1

;; @const FSCTL_SET_REPARSE_POINT
;; @desc NTFS DeviceIoControl code to write a junction/reparse point (0x000900A4).
@const FSCTL_SET_REPARSE_POINT = 0x000900A4

;; @const FSCTL_GET_REPARSE_POINT
;; @desc NTFS DeviceIoControl code to query a junction/symlink/AppExecLink (0x000900A8).
@const FSCTL_GET_REPARSE_POINT = 0x000900A8

;; @const SYMBOLIC_LINK_FLAG_UNPRIVILEGED
;; @desc Windows 10 1703+ Developer Mode unprivileged CreateSymbolicLinkW flag (0x02).
@const SYMBOLIC_LINK_FLAG_UNPRIVILEGED = 0x00000002

;; @const MOVEFILE_REPLACE_EXISTING
;; @desc MoveFileExW replace existing destination flag (0x01).
@const MOVEFILE_REPLACE_EXISTING = 0x00000001

;; @const MOVEFILE_COPY_ALLOWED
;; @desc MoveFileExW cross-volume copy+delete flag (0x02).
@const MOVEFILE_COPY_ALLOWED = 0x00000002

;; @const HKEY_CLASSES_ROOT
;; @desc Predefined Win64 Registry root handle HKEY_CLASSES_ROOT (0xFFFFFFFF80000000).
@const HKEY_CLASSES_ROOT = -2147483648

;; @const HKEY_CURRENT_USER
;; @desc Predefined Win64 Registry root handle HKEY_CURRENT_USER (0xFFFFFFFF80000001).
@const HKEY_CURRENT_USER = -2147483647

;; @const HKEY_LOCAL_MACHINE
;; @desc Predefined Win64 Registry root handle HKEY_LOCAL_MACHINE (0xFFFFFFFF80000002).
@const HKEY_LOCAL_MACHINE = -2147483646

;; @const KEY_READ_ACCESS
;; @desc Win64 Registry KEY_READ access mask (0x00020019).
@const KEY_READ_ACCESS = 0x00020019

;; @const KEY_WRITE_ACCESS
;; @desc Win64 Registry KEY_WRITE access mask (0x00020006).
@const KEY_WRITE_ACCESS = 0x00020006

;; @const REG_SZ_TYPE
;; @desc Win64 Registry REG_SZ string value type (1).
@const REG_SZ_TYPE = 1

;; @const REG_EXPAND_SZ_TYPE
;; @desc Win64 Registry REG_EXPAND_SZ expandable string value type (2).
@const REG_EXPAND_SZ_TYPE = 2

;; @const SEM_FAILCRITICALERRORS
;; @desc SetErrorMode flag suppressing modal hardware critical-error dialogs (0x0001).
@const SEM_FAILCRITICALERRORS = 0x0001

;; @const FILE_TYPE_CHAR
;; @desc GetFileType character/console device result (0x0002).
@const FILE_TYPE_CHAR = 0x0002

;; @const AST_FLAG_QUIET
;; @desc AST_NODE64.bFlags bit suppressing batch command echo (@ prefix = 0x10).
@const AST_FLAG_QUIET = 0x10

;; @struc SECURITY_ATTRIBUTES64
;; @desc 24-byte 64-bit Win64 SECURITY_ATTRIBUTES structure for inheritable CreatePipe/CreateFileW handles.
@struc SECURITY_ATTRIBUTES64, 24
    @field nLength, dword, 1
    @field dwPad0, dword, 1
    @field lpSecurityDescriptor, qword, 1
    @field bInheritHandle, dword, 1
    @field dwPad1, dword, 1
@endstruc

;; @struc SHELLEXECUTEINFOW64
;; @desc 112-byte 64-bit Win64 SHELLEXECUTEINFOW structure passed to ShellExecuteExW.
@struc SHELLEXECUTEINFOW64, 112
    @field cbSize, dword, 1
    @field fMask, dword, 1
    @field hwnd, qword, 1
    @field lpVerb, qword, 1
    @field lpFile, qword, 1
    @field lpParameters, qword, 1
    @field lpDirectory, qword, 1
    @field nShow, dword, 1
    @field dwPad0, dword, 1
    @field hInstApp, qword, 1
    @field lpIDList, qword, 1
    @field lpClass, qword, 1
    @field hkeyClass, qword, 1
    @field dwHotKey, dword, 1
    @field dwPad1, dword, 1
    @field hIconOrMonitor, qword, 1
    @field hProcess, qword, 1
@endstruc

;; @struc SYSTEMTIME16
;; @desc 16-byte Win64 SYSTEMTIME calendar structure.
@struc SYSTEMTIME16, 16
    @field wYear, word, 1
    @field wMonth, word, 1
    @field wDayOfWeek, word, 1
    @field wDay, word, 1
    @field wHour, word, 1
    @field wMinute, word, 1
    @field wSecond, word, 1
    @field wMilliseconds, word, 1
@endstruc

;; @struc AST_REDIR64
;; @desc 24-byte 64-bit Arena redirection descriptor (<, >, >>, n>&m, n<&m).
@struc AST_REDIR64, 24
    @field bKind, byte, 1
    @field bSrcFd, byte, 1
    @field bTargetFd, byte, 1
    @field bPad, byte, 1
    @field dwPad, dword, 1
    @field pTargetStr, qword, 1
    @field pNext, qword, 1
@endstruc

;; @struc AST_NODE64
;; @desc 64-byte pointer-sized x86-64 Arena AST node (simple cmd, block, binary, IF, FOR).
@struc AST_NODE64, 64
    @field bKind, byte, 1
    @field bSub, byte, 1
    @field bFlags, byte, 1
    @field bVarChar, byte, 1
    @field dwPad, dword, 1
    @field pStr1, qword, 1
    @field pStr2, qword, 1
    @field pChild1, qword, 1
    @field pChild2, qword, 1
    @field pRedir, qword, 1
    @field pNext, qword, 1
    @field pExtra, qword, 1
@endstruc

;; @struc BATCH_FRAME64
;; @desc 112-byte active batch/subroutine execution frame tracking exact 64-bit file byte offset.
@struc BATCH_FRAME64, 112
    @field pBatchPath, qword, 1
    @field pArgsRaw, qword, 1
    @field uFileOff, qword, 1
    @field bEncoding, byte, 1
    @field bShiftOff, byte, 1
    @field bIsCallSub, byte, 1
    @field bSavedSetlocalDepth, byte, 1
    @field dwPad, dword, 1
    @field argvPtrs, qword, 10
@endstruc

;; @struc SETLOCAL_SNAP64
;; @desc 32-byte SETLOCAL snapshot descriptor storing cloned 64-bit environment, CWD, drive, and mode flags.
@struc SETLOCAL_SNAP64, 32
    @field pSavedEnv, qword, 1
    @field cbSavedEnv, qword, 1
    @field pSavedCwd, qword, 1
    @field bSavedDrive, byte, 1
    @field bSavedExt, byte, 1
    @field bSavedDelayed, byte, 1
    @field bPad, byte, 1
    @field dwPad, dword, 1
@endstruc

;; @struc FIND_ENTRY64
;; @desc 560-byte normalized UTF-16LE -> WTF-8 directory enumeration result.
@struc FIND_ENTRY64, 560
    @field dwAttr, dword, 1
    @field ftWriteLo, dword, 1
    @field ftWriteHi, dword, 1
    @field ftCreateLo, dword, 1
    @field ftCreateHi, dword, 1
    @field ftAccessLo, dword, 1
    @field ftAccessHi, dword, 1
    @field nSizeLo, dword, 1
    @field nSizeHi, dword, 1
    @field dwReparseTag, dword, 1
    @field szShort83, byte, 32
    @field szNameWtf8, byte, 488
@endstruc
