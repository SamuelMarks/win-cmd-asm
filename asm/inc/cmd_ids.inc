;; @file cmd_ids.inc
;; @isa all
;; @desc Sorted internal built-in command identifiers shared across all 5 CPU ISAs.

@format inc

;; @const CMD_NONE
;; @desc External command or batch script (not an internal built-in).
@const CMD_NONE = 0x00

;; @const CMD_ASSOC
;; @desc File extension association built-in (ASSOC).
@const CMD_ASSOC = 0x01

;; @const CMD_BREAK
;; @desc Ctrl+Break checking control built-in (BREAK).
@const CMD_BREAK = 0x02

;; @const CMD_CALL
;; @desc Subroutine/batch invocation built-in (CALL).
@const CMD_CALL = 0x03

;; @const CMD_CD
;; @desc Change or display working directory built-in (CD / CHDIR).
@const CMD_CD = 0x04

;; @const CMD_CHCP
;; @desc Active console/NLS codepage built-in (CHCP).
@const CMD_CHCP = 0x05

;; @const CMD_CLS
;; @desc Clear terminal viewport built-in (CLS).
@const CMD_CLS = 0x06

;; @const CMD_CMD
;; @desc Spawn nested command processor built-in (CMD / COMMAND).
@const CMD_CMD = 0x07

;; @const CMD_COLOR
;; @desc Set console foreground/background attribute built-in (COLOR).
@const CMD_COLOR = 0x08

;; @const CMD_COPY
;; @desc File copy and concatenation built-in (COPY).
@const CMD_COPY = 0x09

;; @const CMD_CTTY
;; @desc Console character device redirection built-in (CTTY).
@const CMD_CTTY = 0x0A

;; @const CMD_DATE
;; @desc Query or set system date built-in (DATE).
@const CMD_DATE = 0x0B

;; @const CMD_DEL
;; @desc Delete files built-in (DEL / ERASE).
@const CMD_DEL = 0x0C

;; @const CMD_DIR
;; @desc List directory entries and free space built-in (DIR).
@const CMD_DIR = 0x0D

;; @const CMD_DOSKEY
;; @desc Line-editor history and macro built-in (DOSKEY).
@const CMD_DOSKEY = 0x0E

;; @const CMD_DPATH
;; @desc Data file search path built-in (DPATH).
@const CMD_DPATH = 0x0F

;; @const CMD_ECHO
;; @desc Print message or toggle command echo built-in (ECHO).
@const CMD_ECHO = 0x10

;; @const CMD_ENDLOCAL
;; @desc Restore saved environment and directory frame built-in (ENDLOCAL).
@const CMD_ENDLOCAL = 0x11

;; @const CMD_EXIT
;; @desc Exit shell or batch subroutine built-in (EXIT).
@const CMD_EXIT = 0x12

;; @const CMD_FOR
;; @desc Loop iteration built-in (FOR).
@const CMD_FOR = 0x13

;; @const CMD_FTYPE
;; @desc File type open command built-in (FTYPE).
@const CMD_FTYPE = 0x14

;; @const CMD_GOTO
;; @desc Jump to batch label built-in (GOTO).
@const CMD_GOTO = 0x15

;; @const CMD_IF
;; @desc Conditional execution built-in (IF).
@const CMD_IF = 0x16

;; @const CMD_KEYS
;; @desc Line-editing compatibility status built-in (KEYS).
@const CMD_KEYS = 0x17

;; @const CMD_LH
;; @desc Load program into upper memory blocks built-in (LH / LOADHIGH).
@const CMD_LH = 0x18

;; @const CMD_MD
;; @desc Create directory built-in (MD / MKDIR).
@const CMD_MD = 0x19

;; @const CMD_MKLINK
;; @desc Create symbolic link, hard link, or junction built-in (MKLINK).
@const CMD_MKLINK = 0x1A

;; @const CMD_MODE
;; @desc Device and console configuration built-in (MODE).
@const CMD_MODE = 0x1B

;; @const CMD_MOVE
;; @desc Move or rename files and directories built-in (MOVE).
@const CMD_MOVE = 0x1C

;; @const CMD_PATH
;; @desc Executable search path built-in (PATH).
@const CMD_PATH = 0x1D

;; @const CMD_PAUSE
;; @desc Prompt and wait for keystroke built-in (PAUSE).
@const CMD_PAUSE = 0x1E

;; @const CMD_POPD
;; @desc Pop directory stack and unmap temporary UNC drive built-in (POPD).
@const CMD_POPD = 0x1F

;; @const CMD_PROMPT
;; @desc Customize interactive prompt template built-in (PROMPT).
@const CMD_PROMPT = 0x20

;; @const CMD_PUSHD
;; @desc Push working directory and change/map path built-in (PUSHD).
@const CMD_PUSHD = 0x21

;; @const CMD_RD
;; @desc Remove directory built-in (RD / RMDIR).
@const CMD_RD = 0x22

;; @const CMD_REM
;; @desc No-op remark built-in (REM).
@const CMD_REM = 0x23

;; @const CMD_REN
;; @desc Rename file or directory built-in (REN / RENAME).
@const CMD_REN = 0x24

;; @const CMD_SET
;; @desc Display, assign, prompt, or evaluate environment variables built-in (SET).
@const CMD_SET = 0x25

;; @const CMD_SETLOCAL
;; @desc Push environment, CWD, and extension state built-in (SETLOCAL).
@const CMD_SETLOCAL = 0x26

;; @const CMD_SHIFT
;; @desc Shift positional batch parameters built-in (SHIFT).
@const CMD_SHIFT = 0x27

;; @const CMD_START
;; @desc Launch program, document, or subshell window built-in (START).
@const CMD_START = 0x28

;; @const CMD_TIME
;; @desc Query or set system time built-in (TIME).
@const CMD_TIME = 0x29

;; @const CMD_TITLE
;; @desc Set terminal window title built-in (TITLE).
@const CMD_TITLE = 0x2A

;; @const CMD_TRUENAME
;; @desc Canonicalize physical path built-in (TRUENAME).
@const CMD_TRUENAME = 0x2B

;; @const CMD_TYPE
;; @desc Write file contents to standard output built-in (TYPE).
@const CMD_TYPE = 0x2C

;; @const CMD_VER
;; @desc Print OS and shell version banner built-in (VER).
@const CMD_VER = 0x2D

;; @const CMD_VERIFY
;; @desc Toggle or query disk write verification built-in (VERIFY).
@const CMD_VERIFY = 0x2E

;; @const CMD_VOL
;; @desc Display volume label and serial number built-in (VOL).
@const CMD_VOL = 0x2F
