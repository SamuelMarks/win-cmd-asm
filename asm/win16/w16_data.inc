;; @file w16_data.inc
;; @isa 8086
;; @desc Segment 2 (_DATA DGROUP @ 0x2800): 16-byte LocalInit header + initialized Win16 strings.

@cpu 8086
@format inc

w16_data_base:
; Mandatory 16-byte Win16 DGROUP instance header (0x0000..0x000F)
@res word w16InstHdr, 8

@data db szW16Class, "WINCMD16_TTY", 0
@data db szW16Title, "WINCMD Command Prompt", 0
@data db szW16Banner, "WINCMD Pure-ASM Win16 Shell [Windows 1.0-3.2]", 0
@data db szDefCwd, "C:", 0x5C, 0
@data db szPromptGt, ">", 0
@data db szCtrlC, "^C", 0

@data db szModKernel, "KERNEL", 0
@data db szModUser, "USER", 0
@data db szModShell, "SHELL", 0
@data db szModToolhelp, "TOOLHELP", 0

@data db szFnWinExec, "WINEXEC", 0
@data db szFnGetFreeSpace, "GETFREESPACE", 0
@data db szFnIsDbcs, "ISDBCSLEADBYTE", 0
@data db szFnGetSysRes, "GETFREESYSTEMRESOURCES", 0
@data db szFnDragAccept, "DRAGACCEPTFILES", 0
@data db szFnDragQuery, "DRAGQUERYFILE", 0
@data db szFnDragFinish, "DRAGFINISH", 0
@data db szFnMemManInfo, "MEMMANINFO", 0
@data db szFnWNetAdd, "WNETADDCONNECTION", 0
@data db szFnWNetCancel, "WNETCANCELCONNECTION", 0

@data db szVerPrefix, "WINCMD16 [Windows ", 0
@data db szVerDbcsTag, " [CP936 DBCS]", 0
@data db szUncDrvZ, "Z:", 0
@data db szUncRootZ, "Z:", 0x5C, 0
@data db szMemToolhelp, "WINCMD16 Protected-Mode Memory [TOOLHELP OK]", 0
@data db szMemBasic, "WINCMD16 Standard Heap Ready", 0
@data db szErrFile, "File not found", 0

@data db szKwRem, "REM", 0
@data db szKwVer, "VER", 0
@data db szKwCls, "CLS", 0
@data db szKwEcho, "ECHO", 0
@data db szKwSet, "SET", 0
@data db szKwCd, "CD", 0
@data db szKwPushd, "PUSHD", 0
@data db szKwPopd, "POPD", 0
@data db szKwMem, "MEM", 0
@data db szKwType, "TYPE", 0
@data db szKwDel, "DEL", 0
@data db szKwCopy, "COPY", 0
@data db szKwTitle, "TITLE", 0
@data db szKwCall, "CALL", 0
@data db szKwExit, "EXIT", 0

@res byte padDataTo2C00, (0x2C00 - ($ - wincmd_seg_base))
