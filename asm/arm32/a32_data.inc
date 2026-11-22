;; @file a32_data.inc
;; @isa armv4
;; @desc Shared 32-bit ARM (.rdata) constant string pool (g_a32_str_pool), built-in command table (g_a32_cmd_tbl), and constant WTF-8 literals.

@cpu armv4
@format inc

@align 4
; Fixed-offset string pool indexed by STR_OFF_* constants in core/str_utf.inc:
;   0   = STR_OFF_BANNER (36B)
;   36  = STR_OFF_CRLF (3B)
;   39  = STR_OFF_DEF_PROMPT (5B)
;   44  = STR_OFF_ECHO_ON (14B)
;   58  = STR_OFF_ECHO_OFF (15B)
;   73  = STR_OFF_SYNTAX_ERR (42B)
;   115 = STR_OFF_FILE_NOT_FOUND (37B)
;   152 = STR_OFF_PATH_NOT_FOUND (45B)
;   197 = STR_OFF_ACCESS_DENIED (17B)
;   214 = STR_OFF_DIV_ZERO (18B)
;   232 = STR_OFF_INV_NUM (18B)
;   250 = STR_OFF_BAD_CMD (56B)
;   306 = STR_OFF_PAUSE_MSG (33B)
;   339 = STR_OFF_DIR_TAG (20B)
;   359 = STR_OFF_JUNC_TAG (20B)
;   379 = STR_OFF_SYMLINKD_TAG (20B)
;   399 = STR_OFF_SYMLINK_TAG (20B)
;   419 = STR_OFF_LINK_CREATED (16B)
;   435 = STR_OFF_VERIFY_ON (16B)
;   451 = STR_OFF_VERIFY_OFF (17B)
;   468 = STR_OFF_BREAK_ON (14B)
;   482 = STR_OFF_BREAK_OFF (15B)
;   497 = STR_OFF_KEYS_ON (14B)
;   511 = STR_OFF_KEYS_OFF (15B)
;   526 = STR_OFF_VOL_HDR (32B)
;   558 = STR_OFF_COPIED_1 (28B)
;   586 = STR_OFF_ENV_UNDEF (35B)
@data db g_a32_str_pool, "WINCMD_ARM Pure-ASM Shell [Version ", 0
@data db -, 0x0D, 0x0A, 0
@data db -, "$P$G", 0
@data db -, "ECHO is on.", 0x0D, 0x0A, 0
@data db -, "ECHO is off.", 0x0D, 0x0A, 0
@data db -, "The syntax of the command is incorrect.", 0x0D, 0x0A, 0
@data db -, "The system cannot find the file.", 0x0D, 0x0A, 0, 0, 0
@data db -, "The system cannot find the path specified.", 0x0D, 0x0A, 0
@data db -, "Access denied.", 0x0D, 0x0A, 0
@data db -, "Divide by zero.", 0x0D, 0x0A, 0
@data db -, "Invalid number.", 0x0D, 0x0A, 0
@data db -, "Command is not recognized as internal or external.", 0x0D, 0x0A, 0, 0, 0, 0
@data db -, "Press any key to continue . . . ", 0
@data db -, "    <DIR>          ", 0
@data db -, "    <JUNCTION>     ", 0
@data db -, "    <SYMLINKD>     ", 0
@data db -, "    <SYMLINK>      ", 0
@data db -, "Link created.", 0x0D, 0x0A, 0
@data db -, "VERIFY is on.", 0x0D, 0x0A, 0
@data db -, "VERIFY is off.", 0x0D, 0x0A, 0
@data db -, "BREAK is on", 0x0D, 0x0A, 0
@data db -, "BREAK is off", 0x0D, 0x0A, 0
@data db -, "KEYS is on.", 0x0D, 0x0A, 0
@data db -, "KEYS is off.", 0x0D, 0x0A, 0
@data db -, " Volume in drive C is Windows", 0x0D, 0x0A, 0
@data db -, "        1 file(s) copied.", 0x0D, 0x0A, 0
@data db -, "Environment variable not defined", 0x0D, 0x0A, 0

@align 4
; Complete internal built-in command table (CMD_* ID byte + NUL-terminated uppercase name + aliases)
@data db g_a32_cmd_tbl, CMD_ASSOC, "ASSOC", 0
@data db -, CMD_BREAK, "BREAK", 0
@data db -, CMD_CALL, "CALL", 0
@data db -, CMD_CD, "CD", 0
@data db -, CMD_CD, "CHDIR", 0
@data db -, CMD_CHCP, "CHCP", 0
@data db -, CMD_CLS, "CLS", 0
@data db -, CMD_CMD, "CMD", 0
@data db -, CMD_CMD, "COMMAND", 0
@data db -, CMD_COLOR, "COLOR", 0
@data db -, CMD_COPY, "COPY", 0
@data db -, CMD_CTTY, "CTTY", 0
@data db -, CMD_DATE, "DATE", 0
@data db -, CMD_DEL, "DEL", 0
@data db -, CMD_DEL, "ERASE", 0
@data db -, CMD_DIR, "DIR", 0
@data db -, CMD_DOSKEY, "DOSKEY", 0
@data db -, CMD_DPATH, "DPATH", 0
@data db -, CMD_ECHO, "ECHO", 0
@data db -, CMD_ENDLOCAL, "ENDLOCAL", 0
@data db -, CMD_EXIT, "EXIT", 0
@data db -, CMD_FOR, "FOR", 0
@data db -, CMD_FTYPE, "FTYPE", 0
@data db -, CMD_GOTO, "GOTO", 0
@data db -, CMD_IF, "IF", 0
@data db -, CMD_KEYS, "KEYS", 0
@data db -, CMD_LH, "LH", 0
@data db -, CMD_LH, "LOADHIGH", 0
@data db -, CMD_MD, "MD", 0
@data db -, CMD_MD, "MKDIR", 0
@data db -, CMD_MKLINK, "MKLINK", 0
@data db -, CMD_MODE, "MODE", 0
@data db -, CMD_MOVE, "MOVE", 0
@data db -, CMD_PATH, "PATH", 0
@data db -, CMD_PAUSE, "PAUSE", 0
@data db -, CMD_POPD, "POPD", 0
@data db -, CMD_PROMPT, "PROMPT", 0
@data db -, CMD_PUSHD, "PUSHD", 0
@data db -, CMD_RD, "RD", 0
@data db -, CMD_RD, "RMDIR", 0
@data db -, CMD_REM, "REM", 0
@data db -, CMD_REN, "REN", 0
@data db -, CMD_REN, "RENAME", 0
@data db -, CMD_SET, "SET", 0
@data db -, CMD_SETLOCAL, "SETLOCAL", 0
@data db -, CMD_SHIFT, "SHIFT", 0
@data db -, CMD_START, "START", 0
@data db -, CMD_TIME, "TIME", 0
@data db -, CMD_TITLE, "TITLE", 0
@data db -, CMD_TRUENAME, "TRUENAME", 0
@data db -, CMD_TYPE, "TYPE", 0
@data db -, CMD_VER, "VER", 0
@data db -, CMD_VERIFY, "VERIFY", 0
@data db -, CMD_VOL, "VOL", 0
@data db -, 0

@align 4
; Shared environment, dynamic variable, Registry, path, and built-in formatting strings
@data db szEmptyStr, 0
@data db szOneSpace, " ", 0
@data db szDot, ".", 0
@data db szEqStr, "=", 0
@data db szBackslash, 0x5C, 0
@data db szStarPat, "*", 0
@data db szVerClose, "]", 0x0D, 0x0A, 0

@data db szKwOn, "ON", 0
@data db szKwOff, "OFF", 0
@data db szKwEof, "EOF", 0

@data db szVarPath, "PATH", 0
@data db szVarPrompt, "PROMPT", 0
@data db szVarDpath, "DPATH", 0
@data db szVarPathext, "PATHEXT", 0
@data db szVarTemp, "TEMP", 0
@data db szDefPathext, ".COM;.EXE;.BAT;.CMD", 0
@data db szNoDefCwdEnv, "NoDefaultCurrentDirectoryInExePath", 0

@data db szExtBat, ".BAT", 0
@data db szExtCmd, ".CMD", 0
@data db szExtCom, ".COM", 0

@data db szDynCd, "CD", 0
@data db szDynErrLvl, "ERRORLEVEL", 0
@data db szDynExtVer, "CMDEXTVERSION", 0
@data db szDynCmdLine, "CMDCMDLINE", 0
@data db szDynRandom, "RANDOM", 0
@data db szDynDate, "DATE", 0
@data db szDynTime, "TIME", 0
@data db szDynNuma, "HIGHESTNUMANODENUMBER", 0
@data db szDynExitCode, "=ExitCode", 0
@data db szDynExitAscii, "=ExitCodeAscii", 0

@data db szEnableDelExp, "ENABLEDELAYEDEXPANSION", 0
@data db szDisableDelExp, "DISABLEDELAYEDEXPANSION", 0
@data db szEnableExt, "ENABLEEXTENSIONS", 0
@data db szDisableExt, "DISABLEEXTENSIONS", 0

@data db szWinCeTempDir, 0x5C, "Temp", 0
@data db szWinCeNulFile, 0x5C, "Temp", 0x5C, "_wcnul.tmp", 0
@data db szWinNtNulDev, 0x5C, 0x5C, ".", 0x5C, "NUL", 0
@data db szSpoolPrefix, "_WCP", 0
@data db szSpoolExt, ".TMP", 0

@data db szRegClassesPrefix, "Software", 0x5C, "Classes", 0x5C, 0
@data db szRegShellOpenCmd, 0x5C, "shell", 0x5C, "open", 0x5C, "command", 0
@data db szRegCmdProcKey, "Software", 0x5C, "Microsoft", 0x5C, "Command Processor", 0
@data db szRegAutoRunVal, "AutoRun", 0

@data db szActiveCpMsg, "Active code page: ", 0
@data db szBreakMsg, "BREAK is off", 0x0D, 0x0A, 0
@data db szVerifyOn, "VERIFY is on.", 0x0D, 0x0A, 0
@data db szVerifyOff, "VERIFY is off.", 0x0D, 0x0A, 0
@data db szKeysMsg, "KEYS is off.", 0x0D, 0x0A, 0
@data db szModeMsg, "Status for device CON:", 0x0D, 0x0A, "    Columns:        80", 0x0D, 0x0A, "    Lines:          25", 0x0D, 0x0A, 0

@data db szDirOfHdr, 0x0D, 0x0A, " Directory of ", 0
@data db szDirPad8, "               ", 0
@data db szBytesSummary, " bytes", 0x0D, 0x0A, 0
@data db szBytesFree, " bytes free", 0x0D, 0x0A, 0
