;; @file err_code.inc
;; @isa all
;; @desc Canonical unified ERR_* enumeration (0..511) shared across all 5 CPU ISAs and wincmd-diag.

@format inc

;; @const ERR_OK
;; @desc Operation completed without error.
@const ERR_OK = 0x0000

;; @const ERR_INVALID_FUNCTION
;; @desc Invalid MS-DOS or OS function number (0x01).
@const ERR_INVALID_FUNCTION = 0x0001

;; @const ERR_FILE_NOT_FOUND
;; @desc File not found (0x02).
@const ERR_FILE_NOT_FOUND = 0x0002

;; @const ERR_PATH_NOT_FOUND
;; @desc Path not found or subdirectory unsupported on DOS 1.25 (0x03).
@const ERR_PATH_NOT_FOUND = 0x0003

;; @const ERR_TOO_MANY_OPEN_FILES
;; @desc Too many open files / FILES= table exhausted (0x04).
@const ERR_TOO_MANY_OPEN_FILES = 0x0004

;; @const ERR_ACCESS_DENIED
;; @desc Access denied or read-only target (0x05).
@const ERR_ACCESS_DENIED = 0x0005

;; @const ERR_INVALID_HANDLE
;; @desc Invalid file handle (0x06).
@const ERR_INVALID_HANDLE = 0x0006

;; @const ERR_MCB_DESTROYED
;; @desc Memory control blocks destroyed (0x07).
@const ERR_MCB_DESTROYED = 0x0007

;; @const ERR_OUT_OF_MEMORY
;; @desc Insufficient memory or statement arena exhausted (0x08).
@const ERR_OUT_OF_MEMORY = 0x0008

;; @const ERR_INVALID_MCB
;; @desc Invalid memory block address (0x09).
@const ERR_INVALID_MCB = 0x0009

;; @const ERR_INVALID_ENVIRONMENT
;; @desc Invalid environment block (0x0A).
@const ERR_INVALID_ENVIRONMENT = 0x000A

;; @const ERR_BAD_EXE_FORMAT
;; @desc Invalid executable header or unsupported machine type (0x0B).
@const ERR_BAD_EXE_FORMAT = 0x000B

;; @const ERR_INVALID_ACCESS
;; @desc Invalid open access mode (0x0C).
@const ERR_INVALID_ACCESS = 0x000C

;; @const ERR_INVALID_DATA
;; @desc Invalid data (0x0D).
@const ERR_INVALID_DATA = 0x000D

;; @const ERR_INVALID_DRIVE
;; @desc Invalid drive specification (0x0F).
@const ERR_INVALID_DRIVE = 0x000F

;; @const ERR_REMOVE_CURRENT_DIR
;; @desc Attempt to remove active current directory (0x10).
@const ERR_REMOVE_CURRENT_DIR = 0x0010

;; @const ERR_NOT_SAME_DEVICE
;; @desc Cross-device rename or move not directly supported (0x11).
@const ERR_NOT_SAME_DEVICE = 0x0011

;; @const ERR_NO_MORE_FILES
;; @desc No more matching directory entries (0x12).
@const ERR_NO_MORE_FILES = 0x0012

;; @const ERR_WRITE_PROTECT
;; @desc Diskette or volume is write-protected (0x13).
@const ERR_WRITE_PROTECT = 0x0013

;; @const ERR_NOT_READY
;; @desc Drive or device not ready (0x15).
@const ERR_NOT_READY = 0x0015

;; @const ERR_CRC
;; @desc Data error / cyclic redundancy check failure (0x17).
@const ERR_CRC = 0x0017

;; @const ERR_SECTOR_NOT_FOUND
;; @desc Disk sector not found (0x1B).
@const ERR_SECTOR_NOT_FOUND = 0x001B

;; @const ERR_WRITE_FAULT
;; @desc Disk write fault (0x1D).
@const ERR_WRITE_FAULT = 0x001D

;; @const ERR_READ_FAULT
;; @desc Disk read fault (0x1E).
@const ERR_READ_FAULT = 0x001E

;; @const ERR_GEN_FAILURE
;; @desc General device failure (0x1F).
@const ERR_GEN_FAILURE = 0x001F

;; @const ERR_SHARING_VIOLATION
;; @desc File sharing violation (0x20).
@const ERR_SHARING_VIOLATION = 0x0020

;; @const ERR_LOCK_VIOLATION
;; @desc File lock violation (0x21).
@const ERR_LOCK_VIOLATION = 0x0021

;; @const ERR_DISK_FULL
;; @desc Target volume is full (0x27).
@const ERR_DISK_FULL = 0x0027

;; @const ERR_FILE_EXISTS
;; @desc File already exists (0x50).
@const ERR_FILE_EXISTS = 0x0050

;; @const ERR_CANNOT_MAKE
;; @desc Directory or file entry cannot be created (0x52).
@const ERR_CANNOT_MAKE = 0x0052

;; @const ERR_FAIL_INT24
;; @desc Critical error handler returned Fail on INT 24h (0x53).
@const ERR_FAIL_INT24 = 0x0053

;; @const ERR_INVALID_PARAMETER
;; @desc Invalid parameter or switch (0x57).
@const ERR_INVALID_PARAMETER = 0x0057

;; @const ERR_SYNTAX
;; @desc The syntax of the command is incorrect (0x100).
@const ERR_SYNTAX = 0x0100

;; @const ERR_BAD_COMMAND
;; @desc Bad command or file name / not recognized command (0x101).
@const ERR_BAD_COMMAND = 0x0101

;; @const ERR_LABEL_NOT_FOUND
;; @desc Batch target label was not found (0x102).
@const ERR_LABEL_NOT_FOUND = 0x0102

;; @const ERR_DIV_BY_ZERO
;; @desc SET /A divide or modulo by zero (0x103).
@const ERR_DIV_BY_ZERO = 0x0103

;; @const ERR_UNBALANCED_PARENS
;; @desc Unbalanced parentheses in compound block or SET /A (0x104).
@const ERR_UNBALANCED_PARENS = 0x0104

;; @const ERR_MISSING_OPERAND
;; @desc Missing operand in SET /A expression (0x105).
@const ERR_MISSING_OPERAND = 0x0105

;; @const ERR_MISSING_OPERATOR
;; @desc Missing operator in SET /A expression (0x106).
@const ERR_MISSING_OPERATOR = 0x0106

;; @const ERR_INVALID_NUMBER
;; @desc Invalid numeric constant such as 08/09 octal (0x107).
@const ERR_INVALID_NUMBER = 0x0107

;; @const ERR_ENV_FULL
;; @desc Out of environment space (0x108).
@const ERR_ENV_FULL = 0x0108

;; @const ERR_BATCH_NEST_LIMIT
;; @desc Maximum batch CALL or SETLOCAL nesting depth exceeded (0x109).
@const ERR_BATCH_NEST_LIMIT = 0x0109

;; @const ERR_PATH_TOO_LONG
;; @desc Path exceeds era maximum length (0x10A).
@const ERR_PATH_TOO_LONG = 0x010A

;; @const ERR_TRANSIENT_CORRUPT
;; @desc Transient shell reload failed CRC-16 verification (0x10B).
@const ERR_TRANSIENT_CORRUPT = 0x010B

;; @const ERR_CTRL_BREAK_ABORT
;; @desc Execution aborted by Ctrl+C or Ctrl+Break (0x10C).
@const ERR_CTRL_BREAK_ABORT = 0x010C

;; @const ERR_UNSUPPORTED_ON_ERA
;; @desc Feature unsupported on active OS or dialect (0x10D).
@const ERR_UNSUPPORTED_ON_ERA = 0x010D

;; @const ERR_MAX_CODE
;; @desc Highest valid unified ERR_* value (511).
@const ERR_MAX_CODE = 0x01FF
