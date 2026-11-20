;; @file win_equ.inc
;; @isa all
;; @desc Win16 NE, Win9x, WinNT 3.1..11, and WinCE PE/Console/File/Registry/Reparse equates.

@format inc

;; @const IMAGE_DOS_SIGNATURE
;; @desc MZ executable header signature ('MZ' = 0x5A4D).
@const IMAGE_DOS_SIGNATURE = 0x5A4D

;; @const IMAGE_OS2_SIGNATURE
;; @desc 16-bit Windows New Executable signature ('NE' = 0x454E).
@const IMAGE_OS2_SIGNATURE = 0x454E

;; @const IMAGE_NT_SIGNATURE
;; @desc 32/64-bit Portable Executable signature ('PE\0\0' = 0x00004550).
@const IMAGE_NT_SIGNATURE = 0x00004550

;; @const IMAGE_FILE_MACHINE_I386
;; @desc Intel 80386+ 32-bit PE32 machine ID (0x014C).
@const IMAGE_FILE_MACHINE_I386 = 0x014C

;; @const IMAGE_FILE_MACHINE_ARM
;; @desc ARMv4 little-endian 32-bit PE32 machine ID (0x01C0).
@const IMAGE_FILE_MACHINE_ARM = 0x01C0

;; @const IMAGE_FILE_MACHINE_THUMB
;; @desc ARM Thumb-1 32-bit PE32 machine ID (0x01C2).
@const IMAGE_FILE_MACHINE_THUMB = 0x01C2

;; @const IMAGE_FILE_MACHINE_ARMNT
;; @desc ARMv7-A Thumb-2 Windows RT / Windows 10 ARM32 PE32 machine ID (0x01C4).
@const IMAGE_FILE_MACHINE_ARMNT = 0x01C4

;; @const IMAGE_FILE_MACHINE_AMD64
;; @desc x86-64 / AMD64 64-bit PE32+ machine ID (0x8664).
@const IMAGE_FILE_MACHINE_AMD64 = 0x8664

;; @const IMAGE_FILE_MACHINE_ARM64
;; @desc ARM64 / AArch64 64-bit PE32+ machine ID (0xAA64).
@const IMAGE_FILE_MACHINE_ARM64 = 0xAA64

;; @const IMAGE_SUBSYSTEM_WINDOWS_GUI
;; @desc Windows GUI subsystem ID (2).
@const IMAGE_SUBSYSTEM_WINDOWS_GUI = 2

;; @const IMAGE_SUBSYSTEM_WINDOWS_CUI
;; @desc Windows character-mode console subsystem ID (3).
@const IMAGE_SUBSYSTEM_WINDOWS_CUI = 3

;; @const IMAGE_SUBSYSTEM_WINDOWS_CE_GUI
;; @desc Windows CE / Pocket PC subsystem ID (9).
@const IMAGE_SUBSYSTEM_WINDOWS_CE_GUI = 9

;; @const STD_INPUT_HANDLE
;; @desc Win32 GetStdHandle standard input constant (-10 = 0xFFFFFFF6).
@const STD_INPUT_HANDLE = 0xFFFFFFF6

;; @const STD_OUTPUT_HANDLE
;; @desc Win32 GetStdHandle standard output constant (-11 = 0xFFFFFFF5).
@const STD_OUTPUT_HANDLE = 0xFFFFFFF5

;; @const STD_ERROR_HANDLE
;; @desc Win32 GetStdHandle standard error constant (-12 = 0xFFFFFFF4).
@const STD_ERROR_HANDLE = 0xFFFFFFF4

;; @const INVALID_HANDLE_VALUE32
;; @desc 32-bit Win32 INVALID_HANDLE_VALUE (-1 = 0xFFFFFFFF).
@const INVALID_HANDLE_VALUE32 = 0xFFFFFFFF

;; @const GENERIC_READ
;; @desc Win32 CreateFile GENERIC_READ access mask (0x80000000).
@const GENERIC_READ = 0x80000000

;; @const GENERIC_WRITE
;; @desc Win32 CreateFile GENERIC_WRITE access mask (0x40000000).
@const GENERIC_WRITE = 0x40000000

;; @const FILE_SHARE_READ
;; @desc Win32 CreateFile FILE_SHARE_READ flag (0x00000001).
@const FILE_SHARE_READ = 0x00000001

;; @const FILE_SHARE_WRITE
;; @desc Win32 CreateFile FILE_SHARE_WRITE flag (0x00000002).
@const FILE_SHARE_WRITE = 0x00000002

;; @const CREATE_NEW
;; @desc Win32 CreateFile CREATE_NEW disposition (1).
@const CREATE_NEW = 1

;; @const CREATE_ALWAYS
;; @desc Win32 CreateFile CREATE_ALWAYS disposition (2).
@const CREATE_ALWAYS = 2

;; @const OPEN_EXISTING
;; @desc Win32 CreateFile OPEN_EXISTING disposition (3).
@const OPEN_EXISTING = 3

;; @const OPEN_ALWAYS
;; @desc Win32 CreateFile OPEN_ALWAYS disposition (4).
@const OPEN_ALWAYS = 4

;; @const MEM_COMMIT
;; @desc Win32 VirtualAlloc MEM_COMMIT page state (0x00001000).
@const MEM_COMMIT = 0x00001000

;; @const MEM_RESERVE
;; @desc Win32 VirtualAlloc MEM_RESERVE address-space reservation (0x00002000).
@const MEM_RESERVE = 0x00002000

;; @const MEM_RELEASE
;; @desc Win32 VirtualFree MEM_RELEASE flag (0x00008000).
@const MEM_RELEASE = 0x00008000

;; @const PAGE_READWRITE
;; @desc Win32 VirtualAlloc PAGE_READWRITE protection (0x04).
@const PAGE_READWRITE = 0x00000004

;; @const ENABLE_VIRTUAL_TERMINAL_PROCESSING
;; @desc Windows 10+ console output VT sequence processing mode bit (0x0004).
@const ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004

;; @const ENABLE_VIRTUAL_TERMINAL_INPUT
;; @desc Windows 10+ console input VT sequence mode bit (0x0200).
@const ENABLE_VIRTUAL_TERMINAL_INPUT = 0x0200

;; @const CP_UTF8
;; @desc Windows UTF-8 codepage identifier (65001).
@const CP_UTF8 = 65001

;; @const FILE_ATTRIBUTE_REPARSE_POINT
;; @desc NTFS/ReFS reparse point attribute bit (0x00000400).
@const FILE_ATTRIBUTE_REPARSE_POINT = 0x00000400

;; @const FILE_ATTRIBUTE_COMPRESSED
;; @desc NTFS compressed file attribute bit (0x00000800, NT 3.51+).
@const FILE_ATTRIBUTE_COMPRESSED = 0x00000800

;; @const IO_REPARSE_TAG_MOUNT_POINT
;; @desc NTFS directory junction / volume mount-point tag (0xA0000003).
@const IO_REPARSE_TAG_MOUNT_POINT = 0xA0000003

;; @const IO_REPARSE_TAG_SYMLINK
;; @desc NTFS symbolic link reparse tag (0xA000000C, Vista+).
@const IO_REPARSE_TAG_SYMLINK = 0xA000000C

;; @const IO_REPARSE_TAG_APPEXECLINK
;; @desc Windows 10/11 WindowsApps execution alias reparse tag (0x8000001B).
@const IO_REPARSE_TAG_APPEXECLINK = 0x8000001B

;; @const SYMBOLIC_LINK_FLAG_DIRECTORY
;; @desc CreateSymbolicLinkW directory target flag (0x01).
@const SYMBOLIC_LINK_FLAG_DIRECTORY = 0x01

;; @const SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
;; @desc Windows 10 1703+ Developer Mode unprivileged symlink creation flag (0x02).
@const SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE = 0x02

;; @const CW_USEDEFAULT16
;; @desc Windows 1.0..3.2 CreateWindow default coordinate sentinel (0x8000).
@const CW_USEDEFAULT16 = 0x8000

;; @const WM_DESTROY
;; @desc Win16/Win32 window destruction message (0x0002).
@const WM_DESTROY = 0x0002

;; @const WM_SIZE
;; @desc Win16/Win32 viewport resize message (0x0005).
@const WM_SIZE = 0x0005

;; @const WM_PAINT
;; @desc Win16/Win32 client area repaint message (0x000F).
@const WM_PAINT = 0x000F

;; @const WM_KEYDOWN
;; @desc Win16/Win32 virtual key press message (0x0100).
@const WM_KEYDOWN = 0x0100

;; @const WM_CHAR
;; @desc Win16/Win32 translated character input message (0x0102).
@const WM_CHAR = 0x0102

;; @const WM_IME_REPORT
;; @desc Windows 3.2 Chinese / CJK WINNLS IME composition report message (0x0280).
@const WM_IME_REPORT = 0x0280
