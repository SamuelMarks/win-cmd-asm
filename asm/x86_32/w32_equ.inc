;; @file w32_equ.inc
;; @isa 386
;; @desc Win32 / NT 3.1..11 / Win95..Me equates and assemble-time asserted IA-32 structures.

@cpu 386
@format inc

;; @const W32_ARENA_RESERVE_STD
;; @desc Default 4 MiB VirtualAlloc MEM_RESERVE arena size on Win32.
@const W32_ARENA_RESERVE_STD = 0x00400000

;; @const W32_ARENA_RESERVE_LOW
;; @desc Low-RAM 256 KiB VirtualAlloc fallback arena size for NT 3.1 / constrained VMs.
@const W32_ARENA_RESERVE_LOW = 0x00040000

;; @const W32_ARENA_COMMIT_GRAN
;; @desc 64 KiB on-demand MEM_COMMIT page chunk granularity.
@const W32_ARENA_COMMIT_GRAN = 0x00010000

;; @const W32_ENV_CAP_BYTES
;; @desc 64 KiB WTF-8 active environment block capacity (KEY=VALUE\0...\0\0).
@const W32_ENV_CAP_BYTES = 0x00010000

;; @const W32_SETLOCAL_MAX
;; @desc Maximum nested SETLOCAL stack depth (32 frames, matching cmd.exe).
@const W32_SETLOCAL_MAX = 32

;; @const W32_BATCH_MAX
;; @desc Maximum nested CALL batch/subroutine frame depth (32 frames).
@const W32_BATCH_MAX = 32

;; @const W32_PUSHD_MAX
;; @desc Maximum nested PUSHD directory/UNC mapping stack depth (32 entries).
@const W32_PUSHD_MAX = 32

;; @const W32_HIST_MAX
;; @desc Maximum DOSKEY interactive history ring lines (64 entries).
@const W32_HIST_MAX = 64

;; @const W32_MACRO_MAX
;; @desc Maximum DOSKEY macro definitions (32 entries).
@const W32_MACRO_MAX = 32

;; @const W32_FOR_SCOPE_MAX
;; @desc Maximum nested FOR %%V loop variable bindings (16 active variables).
@const W32_FOR_SCOPE_MAX = 16

;; @const W32_LINE_MAX
;; @desc Maximum logical batch/command line byte capacity (16 KiB WTF-8, >= 8191 cmd.exe limit).
@const W32_LINE_MAX = 16384

;; @const W32_PATH_MAX_WTF8
;; @desc Maximum WTF-8 path buffer capacity supporting \?\ 32,767-WCHAR long paths (98,320 bytes).
@const W32_PATH_MAX_WTF8 = 98320

;; @const INVALID_HANDLE_VALUE
;; @desc Win32 INVALID_HANDLE_VALUE (-1 = 0xFFFFFFFF).
@const INVALID_HANDLE_VALUE = -1

;; @const INVALID_FILE_ATTRIBUTES
;; @desc Win32 INVALID_FILE_ATTRIBUTES (0xFFFFFFFF).
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
;; @desc CreateFile flag required to open directory/reparse handles on NT (0x02000000).
@const FILE_FLAG_BACKUP_SEMANTICS = 0x02000000

;; @const FILE_FLAG_OPEN_REPARSE_POINT
;; @desc CreateFile flag opening the reparse link itself without traversal (0x00200000).
@const FILE_FLAG_OPEN_REPARSE_POINT = 0x00200000

;; @const TRUNCATE_EXISTING
;; @desc Win32 CreateFile TRUNCATE_EXISTING disposition (5).
@const TRUNCATE_EXISTING = 5

;; @const FILE_BEGIN
;; @desc Win32 SetFilePointer FILE_BEGIN origin (0).
@const FILE_BEGIN = 0

;; @const FILE_CURRENT
;; @desc Win32 SetFilePointer FILE_CURRENT origin (1).
@const FILE_CURRENT = 1

;; @const FILE_END
;; @desc Win32 SetFilePointer FILE_END origin (2).
@const FILE_END = 2

;; @const DUPLICATE_CLOSE_SOURCE
;; @desc DuplicateHandle DUPLICATE_CLOSE_SOURCE flag (0x01).
@const DUPLICATE_CLOSE_SOURCE = 0x00000001

;; @const DUPLICATE_SAME_ACCESS
;; @desc DuplicateHandle DUPLICATE_SAME_ACCESS flag (0x02).
@const DUPLICATE_SAME_ACCESS = 0x00000002

;; @const HANDLE_FLAG_INHERIT
;; @desc SetHandleInformation HANDLE_FLAG_INHERIT bit (0x01).
@const HANDLE_FLAG_INHERIT = 0x00000001

;; @const STARTF_USESHOWWINDOW
;; @desc STARTUPINFO STARTF_USESHOWWINDOW flag (0x00000001).
@const STARTF_USESHOWWINDOW = 0x00000001

;; @const STARTF_USESTDHANDLES
;; @desc STARTUPINFO STARTF_USESTDHANDLES flag (0x00000100).
@const STARTF_USESTDHANDLES = 0x00000100

;; @const CREATE_NEW_CONSOLE
;; @desc CreateProcess CREATE_NEW_CONSOLE flag (0x00000010).
@const CREATE_NEW_CONSOLE = 0x00000010

;; @const CREATE_UNICODE_ENVIRONMENT
;; @desc CreateProcessW UTF-16LE environment block flag (0x00000400).
@const CREATE_UNICODE_ENVIRONMENT = 0x00000400

;; @const CREATE_SEPARATE_WOW_VDM
;; @desc START /SEPARATE 16-bit WOW VDM isolation flag (NT 3.1+, 0x00000800).
@const CREATE_SEPARATE_WOW_VDM = 0x00000800

;; @const CREATE_SHARED_WOW_VDM
;; @desc START /SHARED 16-bit WOW VDM flag (NT 3.5+, 0x00001000).
@const CREATE_SHARED_WOW_VDM = 0x00001000

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
;; @desc Predefined Win32 Registry root handle HKEY_CLASSES_ROOT (0x80000000).
@const HKEY_CLASSES_ROOT = -2147483648

;; @const HKEY_CURRENT_USER
;; @desc Predefined Win32 Registry root handle HKEY_CURRENT_USER (0x80000001).
@const HKEY_CURRENT_USER = -2147483647

;; @const HKEY_LOCAL_MACHINE
;; @desc Predefined Win32 Registry root handle HKEY_LOCAL_MACHINE (0x80000002).
@const HKEY_LOCAL_MACHINE = -2147483646

;; @const KEY_READ_ACCESS
;; @desc Win32 Registry KEY_READ access mask (0x00020019).
@const KEY_READ_ACCESS = 0x00020019

;; @const KEY_WRITE_ACCESS
;; @desc Win32 Registry KEY_WRITE access mask (0x00020006).
@const KEY_WRITE_ACCESS = 0x00020006

;; @const REG_SZ_TYPE
;; @desc Win32 Registry REG_SZ string value type (1).
@const REG_SZ_TYPE = 1

;; @const REG_EXPAND_SZ_TYPE
;; @desc Win32 Registry REG_EXPAND_SZ expandable string value type (2).
@const REG_EXPAND_SZ_TYPE = 2

;; @const SEM_FAILCRITICALERRORS
;; @desc SetErrorMode flag suppressing modal hardware critical-error dialogs (0x0001).
@const SEM_FAILCRITICALERRORS = 0x0001

;; @const FILE_TYPE_CHAR
;; @desc GetFileType character/console device result (0x0002).
@const FILE_TYPE_CHAR = 0x0002

;; @const AST_FLAG_QUIET
;; @desc AST_NODE32.bFlags bit suppressing batch command echo (@ prefix = 0x10).
@const AST_FLAG_QUIET = 0x10

;; @struc SECURITY_ATTRIBUTES32
;; @desc 12-byte 32-bit Win32 SECURITY_ATTRIBUTES structure for inheritable CreatePipe/CreateFile handles.
@struc SECURITY_ATTRIBUTES32, 12
    @field nLength, dword, 1
    @field lpSecurityDescriptor, dword, 1
    @field bInheritHandle, dword, 1
@endstruc

;; @struc SYSTEMTIME16
;; @desc 16-byte Win32 SYSTEMTIME calendar structure.
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

;; @struc CPINFO20
;; @desc 20-byte Win32 CPINFO structure returned by GetCPInfo (MaxCharSize + DefaultChar[2] + LeadByte[12]).
@struc CPINFO20, 20
    @field MaxCharSize, dword, 1
    @field DefaultChar, byte, 2
    @field LeadByte, byte, 12
    @field wPad, word, 1
@endstruc

;; @struc AST_REDIR32
;; @desc 16-byte 32-bit Arena redirection descriptor (<, >, >>, n>&m, n<&m).
@struc AST_REDIR32, 16
    @field bKind, byte, 1
    @field bSrcFd, byte, 1
    @field bTargetFd, byte, 1
    @field bPad, byte, 1
    @field pTargetStr, dword, 1
    @field pNext, dword, 1
    @field dwReserved, dword, 1
@endstruc

;; @struc AST_NODE32
;; @desc 32-byte pointer-sized IA-32 Arena AST node (simple cmd, block, binary, IF, FOR).
@struc AST_NODE32, 32
    @field bKind, byte, 1
    @field bSub, byte, 1
    @field bFlags, byte, 1
    @field bVarChar, byte, 1
    @field pStr1, dword, 1
    @field pStr2, dword, 1
    @field pChild1, dword, 1
    @field pChild2, dword, 1
    @field pRedir, dword, 1
    @field pNext, dword, 1
    @field pExtra, dword, 1
@endstruc

;; @struc BATCH_FRAME32
;; @desc 64-byte active batch/subroutine execution frame tracking exact physical file byte offset.
@struc BATCH_FRAME32, 64
    @field pBatchPath, dword, 1
    @field pArgsRaw, dword, 1
    @field uFileOffLo, dword, 1
    @field uFileOffHi, dword, 1
    @field bEncoding, byte, 1
    @field bShiftOff, byte, 1
    @field bIsCallSub, byte, 1
    @field bSavedSetlocalDepth, byte, 1
    @field argvPtrs, dword, 10
    @field dwReserved, dword, 1
@endstruc

;; @struc SETLOCAL_SNAP32
;; @desc 24-byte SETLOCAL snapshot descriptor storing cloned environment, CWD, drive, and shell mode flags.
@struc SETLOCAL_SNAP32, 24
    @field pSavedEnv, dword, 1
    @field cbSavedEnv, dword, 1
    @field pSavedCwd, dword, 1
    @field bSavedDrive, byte, 1
    @field bSavedExt, byte, 1
    @field bSavedDelayed, byte, 1
    @field bPad, byte, 1
    @field dwReserved0, dword, 1
    @field dwReserved1, dword, 1
@endstruc

;; @struc FIND_ENTRY32
;; @desc 560-byte normalized cross-HAL (*W and *A) directory enumeration result in WTF-8.
@struc FIND_ENTRY32, 560
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
