;; @file dos_equ.inc
;; @isa all
;; @desc MS-DOS 1.25, 2.00, 4.00, and 7.x/8.0 INT 21h/2Fh/67h syscall, FCB, MCB, and attribute equates.

@format inc

;; @const DOS_CON_IN_ECHO
;; @desc INT 21h AH=01h read character from stdin with echo.
@const DOS_CON_IN_ECHO = 0x01

;; @const DOS_CON_OUT
;; @desc INT 21h AH=02h write DL character to stdout.
@const DOS_CON_OUT = 0x02

;; @const DOS_RAW_CON_IO
;; @desc INT 21h AH=06h direct console I/O (DL=FFh input, else output).
@const DOS_RAW_CON_IO = 0x06

;; @const DOS_RAW_IN_NOECHO
;; @desc INT 21h AH=07h unfiltered char input without echo.
@const DOS_RAW_IN_NOECHO = 0x07

;; @const DOS_CON_IN_NOECHO
;; @desc INT 21h AH=08h char input without echo (honors Ctrl+C).
@const DOS_CON_IN_NOECHO = 0x08

;; @const DOS_PRINT_STR
;; @desc INT 21h AH=09h write $-terminated string at DS:DX.
@const DOS_PRINT_STR = 0x09

;; @const DOS_BUF_INPUT
;; @desc INT 21h AH=0Ah buffered line input at DS:DX.
@const DOS_BUF_INPUT = 0x0A

;; @const DOS_CHECK_STDIN
;; @desc INT 21h AH=0Bh check stdin input status.
@const DOS_CHECK_STDIN = 0x0B

;; @const DOS_SELECT_DISK
;; @desc INT 21h AH=0Eh select default drive DL (0=A:).
@const DOS_SELECT_DISK = 0x0E

;; @const DOS_FCB_OPEN
;; @desc INT 21h AH=0Fh open FCB file (DOS 1.25+).
@const DOS_FCB_OPEN = 0x0F

;; @const DOS_FCB_CLOSE
;; @desc INT 21h AH=10h close FCB file (DOS 1.25+).
@const DOS_FCB_CLOSE = 0x10

;; @const DOS_FCB_FIND_FIRST
;; @desc INT 21h AH=11h find first matching FCB/XFCB entry (DOS 1.25+).
@const DOS_FCB_FIND_FIRST = 0x11

;; @const DOS_FCB_FIND_NEXT
;; @desc INT 21h AH=12h find next matching FCB/XFCB entry (DOS 1.25+).
@const DOS_FCB_FIND_NEXT = 0x12

;; @const DOS_FCB_DELETE
;; @desc INT 21h AH=13h delete FCB wildcard match (DOS 1.25+).
@const DOS_FCB_DELETE = 0x13

;; @const DOS_FCB_CREATE
;; @desc INT 21h AH=16h create/truncate FCB file (DOS 1.25+).
@const DOS_FCB_CREATE = 0x16

;; @const DOS_FCB_RENAME
;; @desc INT 21h AH=17h rename FCB wildcard match (DOS 1.25+).
@const DOS_FCB_RENAME = 0x17

;; @const DOS_GET_DEF_DRIVE
;; @desc INT 21h AH=19h get current default drive in AL.
@const DOS_GET_DEF_DRIVE = 0x19

;; @const DOS_SET_DTA
;; @desc INT 21h AH=1Ah set Disk Transfer Area to DS:DX.
@const DOS_SET_DTA = 0x1A

;; @const DOS_GET_DRIVE_DATA
;; @desc INT 21h AH=1Ch get drive allocation info (clobbers DS on DOS 1.25!).
@const DOS_GET_DRIVE_DATA = 0x1C

;; @const DOS_SET_VEC
;; @desc INT 21h AH=25h set interrupt vector AL to DS:DX.
@const DOS_SET_VEC = 0x25

;; @const DOS_CREATE_PSP
;; @desc INT 21h AH=26h create new PSP at segment DX (DOS 1.25 loader).
@const DOS_CREATE_PSP = 0x26

;; @const DOS_FCB_RAND_READ
;; @desc INT 21h AH=27h random block read via FCB (DOS 1.25+).
@const DOS_FCB_RAND_READ = 0x27

;; @const DOS_FCB_RAND_WRITE
;; @desc INT 21h AH=28h random block write/resize via FCB (DOS 1.25+).
@const DOS_FCB_RAND_WRITE = 0x28

;; @const DOS_PARSE_FCB
;; @desc INT 21h AH=29h parse filename DS:SI into FCB ES:DI (DOS 1.25+).
@const DOS_PARSE_FCB = 0x29

;; @const DOS_GET_DATE
;; @desc INT 21h AH=2Ah query system date (CX=year, DH=month, DL=day, AL=dow).
@const DOS_GET_DATE = 0x2A

;; @const DOS_SET_DATE
;; @desc INT 21h AH=2Bh set system date.
@const DOS_SET_DATE = 0x2B

;; @const DOS_GET_TIME
;; @desc INT 21h AH=2Ch query system time (CH=hr, CL=min, DH=sec, DL=1/100s).
@const DOS_GET_TIME = 0x2C

;; @const DOS_SET_TIME
;; @desc INT 21h AH=2Dh set system time.
@const DOS_SET_TIME = 0x2D

;; @const DOS_SET_VERIFY
;; @desc INT 21h AH=2Eh set disk write verify flag AL.
@const DOS_SET_VERIFY = 0x2E

;; @const DOS_GET_DTA
;; @desc INT 21h AH=2Fh get active DTA into ES:BX (DOS 2.00+).
@const DOS_GET_DTA = 0x2F

;; @const DOS_GET_VERSION
;; @desc INT 21h AH=30h get MS-DOS version (AL=0 on DOS 1.25!).
@const DOS_GET_VERSION = 0x30

;; @const DOS_GET_SET_BREAK
;; @desc INT 21h AH=33h get/set Ctrl+Break check state.
@const DOS_GET_SET_BREAK = 0x33

;; @const DOS_GET_VEC
;; @desc INT 21h AH=35h get interrupt vector AL into ES:BX (DOS 2.00+).
@const DOS_GET_VEC = 0x35

;; @const DOS_GET_FREE_SPACE
;; @desc INT 21h AH=36h get free disk space on drive DL (DOS 2.00+).
@const DOS_GET_FREE_SPACE = 0x36

;; @const DOS_MKDIR
;; @desc INT 21h AH=39h create directory DS:DX (DOS 2.00+).
@const DOS_MKDIR = 0x39

;; @const DOS_RMDIR
;; @desc INT 21h AH=3Ah remove directory DS:DX (DOS 2.00+).
@const DOS_RMDIR = 0x3A

;; @const DOS_CHDIR
;; @desc INT 21h AH=3Bh change current directory DS:DX (DOS 2.00+).
@const DOS_CHDIR = 0x3B

;; @const DOS_CREAT
;; @desc INT 21h AH=3Ch create/truncate handle file DS:DX, CX=attr (DOS 2.00+).
@const DOS_CREAT = 0x3C

;; @const DOS_OPEN
;; @desc INT 21h AH=3Dh open handle file DS:DX, AL=mode (DOS 2.00+).
@const DOS_OPEN = 0x3D

;; @const DOS_CLOSE
;; @desc INT 21h AH=3Eh close file handle BX (DOS 2.00+).
@const DOS_CLOSE = 0x3E

;; @const DOS_READ
;; @desc INT 21h AH=3Fh read CX bytes from handle BX into DS:DX (DOS 2.00+).
@const DOS_READ = 0x3F

;; @const DOS_WRITE
;; @desc INT 21h AH=40h write CX bytes from DS:DX to handle BX (DOS 2.00+).
@const DOS_WRITE = 0x40

;; @const DOS_UNLINK
;; @desc INT 21h AH=41h delete ASCIIZ file DS:DX (DOS 2.00+).
@const DOS_UNLINK = 0x41

;; @const DOS_LSEEK
;; @desc INT 21h AH=42h move file pointer CX:DX on handle BX (DOS 2.00+).
@const DOS_LSEEK = 0x42

;; @const DOS_CHMOD
;; @desc INT 21h AH=43h get/set file attributes DS:DX (DOS 2.00+).
@const DOS_CHMOD = 0x43

;; @const DOS_IOCTL
;; @desc INT 21h AH=44h device I/O control (DOS 2.00+).
@const DOS_IOCTL = 0x44

;; @const DOS_DUP
;; @desc INT 21h AH=45h duplicate file handle BX (DOS 2.00+).
@const DOS_DUP = 0x45

;; @const DOS_DUP2
;; @desc INT 21h AH=46h force duplicate handle BX onto CX (DOS 2.00+).
@const DOS_DUP2 = 0x46

;; @const DOS_GETCWD
;; @desc INT 21h AH=47h get current directory on drive DL into DS:SI (DOS 2.00+).
@const DOS_GETCWD = 0x47

;; @const DOS_ALLOC_MEM
;; @desc INT 21h AH=48h allocate BX paragraphs of conventional memory (DOS 2.00+).
@const DOS_ALLOC_MEM = 0x48

;; @const DOS_FREE_MEM
;; @desc INT 21h AH=49h free memory block at ES (DOS 2.00+).
@const DOS_FREE_MEM = 0x49

;; @const DOS_RESIZE_MEM
;; @desc INT 21h AH=4Ah resize memory block ES to BX paragraphs (DOS 2.00+).
@const DOS_RESIZE_MEM = 0x4A

;; @const DOS_EXEC
;; @desc INT 21h AH=4Bh load/execute program DS:DX with parameter block ES:BX (clobbers SS:SP on DOS 2.00!).
@const DOS_EXEC = 0x4B

;; @const DOS_EXIT
;; @desc INT 21h AH=4Ch terminate process with exit code AL (DOS 2.00+).
@const DOS_EXIT = 0x4C

;; @const DOS_GET_EXIT_CODE
;; @desc INT 21h AH=4Dh retrieve child exit code into AX (DOS 2.00+).
@const DOS_GET_EXIT_CODE = 0x4D

;; @const DOS_FIND_FIRST
;; @desc INT 21h AH=4Eh find first matching ASCIIZ file DS:DX, CX=attr (DOS 2.00+).
@const DOS_FIND_FIRST = 0x4E

;; @const DOS_FIND_NEXT
;; @desc INT 21h AH=4Fh find next matching file in active DTA (DOS 2.00+).
@const DOS_FIND_NEXT = 0x4F

;; @const DOS_GET_VERIFY
;; @desc INT 21h AH=54h get disk write verify flag in AL (DOS 2.00+).
@const DOS_GET_VERIFY = 0x54

;; @const DOS_RENAME
;; @desc INT 21h AH=56h rename/move DS:DX to ES:DI (DOS 2.00+).
@const DOS_RENAME = 0x56

;; @const DOS_FILE_TIME
;; @desc INT 21h AH=57h get/set file handle date/time (DOS 2.00+).
@const DOS_FILE_TIME = 0x57

;; @const DOS_GET_EXT_ERR
;; @desc INT 21h AH=59h get extended error information (DOS 3.00+/4.00).
@const DOS_GET_EXT_ERR = 0x59

;; @const DOS_CREATE_TEMP
;; @desc INT 21h AH=5Ah create unique temporary file in DS:DX (DOS 3.00+/4.00).
@const DOS_CREATE_TEMP = 0x5A

;; @const DOS_TRUENAME
;; @desc INT 21h AH=60h canonicalize path DS:SI into ES:DI (DOS 3.00+/4.00).
@const DOS_TRUENAME = 0x60

;; @const DOS_NLS_INFO
;; @desc INT 21h AH=65h get extended country/uppercase/DBCS tables (DOS 4.00+).
@const DOS_NLS_INFO = 0x65

;; @const DOS_CODEPAGE
;; @desc INT 21h AH=66h get/set global codepage (DOS 4.00+).
@const DOS_CODEPAGE = 0x66

;; @const DOS_DISK_SERIAL
;; @desc INT 21h AX=6900h get/set 32-bit disk serial number (DOS 4.00+).
@const DOS_DISK_SERIAL = 0x6900

;; @const DOS_EXT_OPEN
;; @desc INT 21h AX=6C00h extended atomic open/create (DOS 4.00+).
@const DOS_EXT_OPEN = 0x6C00

;; @const DOS_LFN_VOL_INFO
;; @desc INT 21h AX=71A0h query Windows 95 LFN volume capabilities.
@const DOS_LFN_VOL_INFO = 0x71A0

;; @const DOS_LFN_MKDIR
;; @desc INT 21h AX=7139h Windows 95 LFN CreateDirectory.
@const DOS_LFN_MKDIR = 0x7139

;; @const DOS_LFN_RMDIR
;; @desc INT 21h AX=713Ah Windows 95 LFN RemoveDirectory.
@const DOS_LFN_RMDIR = 0x713A

;; @const DOS_LFN_CHDIR
;; @desc INT 21h AX=713Bh Windows 95 LFN SetCurrentDirectory.
@const DOS_LFN_CHDIR = 0x713B

;; @const DOS_LFN_DELETE
;; @desc INT 21h AX=7141h Windows 95 LFN DeleteFile (SI=wildcard flag, CX=attr).
@const DOS_LFN_DELETE = 0x7141

;; @const DOS_LFN_ATTR
;; @desc INT 21h AX=7143h Windows 95 LFN Get/SetFileAttributes (BL=0 get, BL=1 set).
@const DOS_LFN_ATTR = 0x7143

;; @const DOS_LFN_GETCWD
;; @desc INT 21h AX=7147h Windows 95 LFN GetCurrentDirectory (up to 260 bytes).
@const DOS_LFN_GETCWD = 0x7147

;; @const DOS_LFN_RENAME
;; @desc INT 21h AX=7156h Windows 95 LFN MoveFile/Rename.
@const DOS_LFN_RENAME = 0x7156

;; @const DOS_LFN_TRUENAME
;; @desc INT 21h AX=7160h Windows 95 LFN GetFullPathName / Short(CL=1) / Long(CL=2).
@const DOS_LFN_TRUENAME = 0x7160

;; @const DOS_LFN_EXT_OPEN
;; @desc INT 21h AX=716Ch Windows 95 LFN Extended Open/Create.
@const DOS_LFN_EXT_OPEN = 0x716C

;; @const DOS_LFN_FIND_FIRST
;; @desc INT 21h AX=714Eh Windows 95 LFN FindFirstFile.
@const DOS_LFN_FIND_FIRST = 0x714E

;; @const DOS_LFN_FIND_NEXT
;; @desc INT 21h AX=714Fh Windows 95 LFN FindNextFile.
@const DOS_LFN_FIND_NEXT = 0x714F

;; @const DOS_LFN_FIND_CLOSE
;; @desc INT 21h AX=71A1h Windows 95 LFN FindClose (prevents search handle leak).
@const DOS_LFN_FIND_CLOSE = 0x71A1

;; @const DOS_FAT32_FREE_SPACE
;; @desc INT 21h AX=7303h Windows 95 OSR2 / 98 / Me FAT32 >2 GiB Get_ExtFreeSpace.
@const DOS_FAT32_FREE_SPACE = 0x7303

;; @const MULTIPLEX_NLSFUNC_CHECK
;; @desc INT 2Fh AX=1400h NLSFUNC resident installation check (AL=FFh if installed).
@const MULTIPLEX_NLSFUNC_CHECK = 0x1400

;; @const MULTIPLEX_WIN_STD_CHECK
;; @desc INT 2Fh AX=4680h Windows 3.0+ Real/Standard Mode installation check (AX=0 if active).
@const MULTIPLEX_WIN_STD_CHECK = 0x4680

;; @const HW_BIOS_CON
;; @desc RES_STATE.bHwFlags bit 0: direct IBM PC BIOS INT 10h/16h console active.
@const HW_BIOS_CON = 0x01

;; @const HW_WIN_ENH
;; @desc RES_STATE.bHwFlags bit 1: Windows 3.x/9x Enhanced Mode WINOLDAP detected.
@const HW_WIN_ENH = 0x02

;; @const HW_WIN_STD
;; @desc RES_STATE.bHwFlags bit 2: Windows 3.x Real/Standard Mode detected.
@const HW_WIN_STD = 0x04

;; @const HW_LFN_VOL
;; @desc RES_STATE.bHwFlags bit 3: MS-DOS 7.x Long Filename API (AX=71xxh) supported.
@const HW_LFN_VOL = 0x08

;; @const HW_FAT32_VOL
;; @desc RES_STATE.bHwFlags bit 4: MS-DOS 7.10+ FAT32 API (AX=7303h) supported.
@const HW_FAT32_VOL = 0x10

;; @const HW_EMS_OK
;; @desc RES_STATE.bHwFlags bit 5: LIM EMS 4.0 swap handle allocated for TRANSIENT.
@const HW_EMS_OK = 0x20

;; @const HW_XMS_OK
;; @desc RES_STATE.bHwFlags bit 6: XMS extended-memory swap block allocated for TRANSIENT.
@const HW_XMS_OK = 0x40

;; @const HW_FILE_COMP
;; @desc RES_STATE.bHwFlags bit 7: /F:ON interactive Tab filename completion active.
@const HW_FILE_COMP = 0x80

;; @const DIALECT_DOS125
;; @desc Strict 1982 MS-DOS 1.25 FCB-only dialect.
@const DIALECT_DOS125 = 0x01

;; @const DIALECT_DOS200
;; @desc 1983 MS-DOS 2.00 hierarchical handle dialect.
@const DIALECT_DOS200 = 0x02

;; @const DIALECT_DOS400
;; @desc 1988 MS-DOS 4.00..6.22 NLS/DBCS/EMS dialect.
@const DIALECT_DOS400 = 0x04

;; @const DIALECT_DOS700
;; @desc Windows 95/98/Me MS-DOS 7.x/8.0 LFN & FAT32 dialect.
@const DIALECT_DOS700 = 0x07

;; @const DIALECT_NT_CMD
;; @desc Windows NT/2000/XP/Vista/7/8/10/11 CMD.EXE extensions dialect.
@const DIALECT_NT_CMD = 0x0A

;; @const ATTR_NORMAL
;; @desc Standard read-write file attribute (0x00).
@const ATTR_NORMAL = 0x00

;; @const ATTR_READONLY
;; @desc Read-only file attribute (0x01).
@const ATTR_READONLY = 0x01

;; @const ATTR_HIDDEN
;; @desc Hidden file or directory attribute (0x02).
@const ATTR_HIDDEN = 0x02

;; @const ATTR_SYSTEM
;; @desc System file or directory attribute (0x04).
@const ATTR_SYSTEM = 0x04

;; @const ATTR_VOLID
;; @desc Root directory volume label attribute (0x08).
@const ATTR_VOLID = 0x08

;; @const ATTR_DIRECTORY
;; @desc Subdirectory entry attribute (0x10).
@const ATTR_DIRECTORY = 0x10

;; @const ATTR_ARCHIVE
;; @desc Modified archive file attribute (0x20).
@const ATTR_ARCHIVE = 0x20

;; @const MULTIPLEX_WIN_ENH_CHECK
;; @desc INT 2Fh AX=1600h Windows 3.x/9x Enhanced Mode installation check.
@const MULTIPLEX_WIN_ENH_CHECK = 0x1600

;; @const MULTIPLEX_IDLE_SLICE
;; @desc INT 2Fh AX=1680h release current VM time-slice to Windows/DPMI scheduler.
@const MULTIPLEX_IDLE_SLICE = 0x1680

;; @const MULTIPLEX_WINOLDAP_TITLE
;; @desc INT 2Fh AX=168Eh set Windows 3.x/9x DOS box title bar string.
@const MULTIPLEX_WINOLDAP_TITLE = 0x168E

;; @const MULTIPLEX_XMS_CHECK
;; @desc INT 2Fh AX=4300h XMS driver installation check (AL=80h if installed).
@const MULTIPLEX_XMS_CHECK = 0x4300

;; @const MULTIPLEX_XMS_ENTRY
;; @desc INT 2Fh AX=4310h get XMS driver control function far pointer into ES:BX.
@const MULTIPLEX_XMS_ENTRY = 0x4310

;; @const EMS_INT
;; @desc LIM EMS 4.0 software interrupt vector (0x67).
@const EMS_INT = 0x67
