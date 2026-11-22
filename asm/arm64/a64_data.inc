;; @file a64_data.inc
;; @isa arm64
;; @desc Shared 64-bit AArch64 (.rdata) constant string pool (g_a64_str_pool), built-in command table, and UTF-16LE/WTF-8 literals.

@cpu arm64
@format inc

@align 4
; Fixed-offset string pool indexed by STR_OFF_* constants:
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
@data db g_a64_str_pool, "WINCMD_ARM Pure-ASM Shell [Version ", 0
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
@data db g_a64_cmd_tbl, CMD_ASSOC, "ASSOC", 0
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
@data dw u16Kernel32_a64, 0x4B, 0x45, 0x52, 0x4E, 0x45, 0x4C, 0x33, 0x32, 0x2E, 0x44, 0x4C, 0x4C, 0
@data dw u16Ntdll_a64, 0x4E, 0x54, 0x44, 0x4C, 0x4C, 0x2E, 0x44, 0x4C, 0x4C, 0
@data dw u16Advapi32_a64, 0x41, 0x44, 0x56, 0x41, 0x50, 0x49, 0x33, 0x32, 0x2E, 0x44, 0x4C, 0x4C, 0

@align 4
@data db szCreateSymbolicLinkW_a64, "CreateSymbolicLinkW", 0
@data db szCreateHardLinkW_a64, "CreateHardLinkW", 0
@data db szCreatePseudoConsole_a64, "CreatePseudoConsole", 0
@data db szGetMachineTypeAttributes_a64, "GetMachineTypeAttributes", 0
@data db szRtlGetVersion_a64, "RtlGetVersion", 0
@data db szNtQueryInformationFile_a64, "NtQueryInformationFile", 0
@data db szRegOpenKeyExW_a64, "RegOpenKeyExW", 0
@data db szRegCreateKeyExW_a64, "RegCreateKeyExW", 0
@data db szRegQueryValueExW_a64, "RegQueryValueExW", 0
@data db szRegSetValueExW_a64, "RegSetValueExW", 0
@data db szRegCloseKey_a64, "RegCloseKey", 0

@align 4
@data db szEmptyStr_a64, 0
@data db szOneSpace_a64, " ", 0
@data db szDot_a64, ".", 0
@data db szEqStr_a64, "=", 0
@data db szBackslash_a64, 0x5C, 0
@data db szStarPat_a64, "*", 0
@data db szVerClose_a64, "]", 0x0D, 0x0A, 0

@data db szKwOn_a64, "ON", 0
@data db szKwOff_a64, "OFF", 0
@data db szKwEof_a64, "EOF", 0

@data db szVarPath_a64, "PATH", 0
@data db szVarPrompt_a64, "PROMPT", 0
@data db szVarDpath_a64, "DPATH", 0
@data db szVarPathext_a64, "PATHEXT", 0
@data db szVarTemp_a64, "TEMP", 0
@data db szDefPathext_a64, ".COM;.EXE;.BAT;.CMD", 0
@data db szNoDefCwdEnv_a64, "NoDefaultCurrentDirectoryInExePath", 0

@data db szExtBat_a64, ".BAT", 0
@data db szExtCmd_a64, ".CMD", 0
@data db szExtCom_a64, ".COM", 0

@data db szDynCd_a64, "CD", 0
@data db szDynErrLvl_a64, "ERRORLEVEL", 0
@data db szDynExtVer_a64, "CMDEXTVERSION", 0
@data db szDynCmdLine_a64, "CMDCMDLINE", 0
@data db szDynRandom_a64, "RANDOM", 0
@data db szDynDate_a64, "DATE", 0
@data db szDynTime_a64, "TIME", 0
@data db szDynNuma_a64, "HIGHESTNUMANODENUMBER", 0
@data db szDynExitCode_a64, "=ExitCode", 0
@data db szDynExitAscii_a64, "=ExitCodeAscii", 0

@data db szEnableDelExp_a64, "ENABLEDELAYEDEXPANSION", 0
@data db szDisableDelExp_a64, "DISABLEDELAYEDEXPANSION", 0
@data db szEnableExt_a64, "ENABLEEXTENSIONS", 0
@data db szDisableExt_a64, "DISABLEEXTENSIONS", 0

@data db szWinNtNulDev_a64, 0x5C, 0x5C, ".", 0x5C, "NUL", 0
@data db szSpoolPrefix_a64, "_WCP", 0
@data db szSpoolExt_a64, ".TMP", 0

@data db szRegClassesPrefix_a64, "Software", 0x5C, "Classes", 0x5C, 0
@data db szRegShellOpenCmd_a64, 0x5C, "shell", 0x5C, "open", 0x5C, "command", 0
@data db szRegCmdProcKey_a64, "Software", 0x5C, "Microsoft", 0x5C, "Command Processor", 0
@data db szRegAutoRunVal_a64, "AutoRun", 0

@data db szActiveCpMsg_a64, "Active code page: ", 0
@data db szBreakMsg_a64, "BREAK is off", 0x0D, 0x0A, 0
@data db szVerifyOn_a64, "VERIFY is on.", 0x0D, 0x0A, 0
@data db szVerifyOff_a64, "VERIFY is off.", 0x0D, 0x0A, 0
@data db szKeysMsg_a64, "KEYS is off.", 0x0D, 0x0A, 0
@data db szModeMsg_a64, "Status for device CON:", 0x0D, 0x0A, "    Columns:        80", 0x0D, 0x0A, "    Lines:          25", 0x0D, 0x0A, 0

@data db szDirOfHdr_a64, 0x0D, 0x0A, " Directory of ", 0
@data db szDirPad8_a64, "               ", 0
@data db szBytesSummary_a64, " bytes", 0x0D, 0x0A, 0
@data db szBytesFree_a64, " bytes free", 0x0D, 0x0A, 0

@data db szA64DefCwd, "C:", 0x5C, 0
@data db szA64DefSelf, "C:", 0x5C, "Windows", 0x5C, "System32", 0x5C, "WINCMD_ARM64.EXE", 0
@data db szA64VtClear, 0x1B, "[2J", 0x1B, "[H", 0
@data db szPipeSubFlags_a64, 0x22, " /S /D /C ", 0x22, 0

@data db szA64DefaultEnv, "COMSPEC=C:", 0x5C, "Windows", 0x5C, "System32", 0x5C, "WINCMD_ARM64.EXE", 0
@data db -, "PATH=C:", 0x5C, "Windows", 0x5C, "System32;C:", 0x5C, "Windows", 0
@data db -, "PATHEXT=.COM;.EXE;.BAT;.CMD", 0
@data db -, "PROMPT=$P$G", 0
@data db -, "OS=Windows_NT", 0, 0
