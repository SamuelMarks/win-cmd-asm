;; @file wincmdv7.asm
;; @isa armv7
;; @desc ARMv7-A Thumb-2 UAL PE32 (0x01C4 ARMNT, Subsystem 6.02, DYNAMIC_BASE, NX_COMPAT, GUARD_CF, .pdata, zero CRT) shell for Windows RT 8.0/8.1 & Windows 10 ARM32.

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global a32_ctrl_handler
@global a32_seh_filter
@global mainCRTStartup

@include "../../inc/err_code.inc"
@include "../../inc/ast_kind.inc"
@include "../../inc/cmd_ids.inc"
@include "../../inc/win_equ.inc"
@include "../../inc/pe_strc.inc"
@include "../a32_equ.inc"

;; @const szRtDefaultEnv_len
;; @desc Exact byte length of default Windows RT / Win10 ARMNT double-NUL WTF-8 environment block.
@const szRtDefaultEnv_len = 136

; Windows RT 8.0/8.1 & Windows 10 ARM32 KERNEL32.DLL static IAT baseline (Zero CRT!)
@extern __imp_CloseHandle, dword
@extern __imp_CopyFileW, dword
@extern __imp_CreateDirectoryW, dword
@extern __imp_CreateFileW, dword
@extern __imp_CreatePipe, dword
@extern __imp_CreateProcessW, dword
@extern __imp_DeleteFileW, dword
@extern __imp_DeviceIoControl, dword
@extern __imp_ExitProcess, dword
@extern __imp_FillConsoleOutputCharacterW, dword
@extern __imp_FindClose, dword
@extern __imp_FindFirstFileW, dword
@extern __imp_FindNextFileW, dword
@extern __imp_FreeEnvironmentStringsW, dword
@extern __imp_GetCommandLineW, dword
@extern __imp_GetConsoleMode, dword
@extern __imp_GetConsoleOutputCP, dword
@extern __imp_GetConsoleScreenBufferInfo, dword
@extern __imp_GetCurrentDirectoryW, dword
@extern __imp_GetDiskFreeSpaceExW, dword
@extern __imp_GetEnvironmentStringsW, dword
@extern __imp_GetExitCodeProcess, dword
@extern __imp_GetFileAttributesW, dword
@extern __imp_GetLastError, dword
@extern __imp_GetLocalTime, dword
@extern __imp_GetModuleFileNameW, dword
@extern __imp_GetModuleHandleW, dword
@extern __imp_GetProcAddress, dword
@extern __imp_GetStdHandle, dword
@extern __imp_GetTickCount, dword
@extern __imp_LoadLibraryW, dword
@extern __imp_MoveFileExW, dword
@extern __imp_ReadFile, dword
@extern __imp_RemoveDirectoryW, dword
@extern __imp_SetConsoleCP, dword
@extern __imp_SetConsoleCtrlHandler, dword
@extern __imp_SetConsoleCursorPosition, dword
@extern __imp_SetConsoleMode, dword
@extern __imp_SetConsoleOutputCP, dword
@extern __imp_SetConsoleTextAttribute, dword
@extern __imp_SetConsoleTitleW, dword
@extern __imp_SetFileAttributesW, dword
@extern __imp_SetFilePointerEx, dword
@extern __imp_SetHandleInformation, dword
@extern __imp_SetLocalTime, dword
@extern __imp_SetUnhandledExceptionFilter, dword
@extern __imp_VirtualAlloc, dword
@extern __imp_VirtualFree, dword
@extern __imp_WaitForSingleObject, dword
@extern __imp_WriteConsoleW, dword
@extern __imp_WriteFile, dword

@section text

@include "hal_rt7.inc"
@include "../core/str_utf.inc"
@include "../vfs/path_glob.inc"
@include "../read/stream_ed.inc"
@include "../parse/exp_prs.inc"
@include "../rt/env_sa.inc"
@include "../rt/exec_ast.inc"
@include "../rt/exec_for.inc"
@include "../cmd/cmds_a_a32.inc"
@include "../cmd/cmds_b_a32.inc"
@include "../vm8086/vm86_a32.inc"
@include "../vm8086/vm86_exec_a32.inc"

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint invoked by Windows RT 8.0/8.1 or Windows 10 ARMNT loader
;; @out Terminates via ExitProcess(r0)
;; @err Exits with shell status code
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc CRT-free ARMv7-A Thumb-2 PE32 entrypoint exercising __chkstk (R4 in words) and running shell_main_a32.
@proc mainCRTStartup
    push {r4, lr}
    ; Probe >= 4 KiB stack guard pages via Windows ARMNT __chkstk (R4 = 1040 words = 4,160 bytes)
    movw r4, #1040
    bl __chkstk
    bl shell_main_a32
    call_iat __imp_ExitProcess
    movs r0, #0
    pop {r4, lr}
    bx lr
@endproc

@section rdata

@include "../a32_data.inc"

@align 4
; UTF-16LE DLL module names for progressive GetModuleHandleW / LoadLibraryW resolution
@data dw u16Kernel32, 0x4B, 0x45, 0x52, 0x4E, 0x45, 0x4C, 0x33, 0x32, 0x2E, 0x44, 0x4C, 0x4C, 0
@data dw u16Ntdll, 0x4E, 0x54, 0x44, 0x4C, 0x4C, 0x2E, 0x44, 0x4C, 0x4C, 0
@data dw u16Advapi32, 0x41, 0x44, 0x56, 0x41, 0x50, 0x49, 0x33, 0x32, 0x2E, 0x44, 0x4C, 0x4C, 0

@align 4
; Dynamic ASCII export names for GetProcAddress
@data db szCreateSymbolicLinkW, "CreateSymbolicLinkW", 0
@data db szCreateHardLinkW, "CreateHardLinkW", 0
@data db szCreatePseudoConsole, "CreatePseudoConsole", 0
@data db szGetMachineTypeAttributes, "GetMachineTypeAttributes", 0
@data db szRtlGetVersion, "RtlGetVersion", 0
@data db szNtQueryInformationFile, "NtQueryInformationFile", 0
@data db szRegOpenKeyExW, "RegOpenKeyExW", 0
@data db szRegCreateKeyExW, "RegCreateKeyExW", 0
@data db szRegQueryValueExW, "RegQueryValueExW", 0
@data db szRegSetValueExW, "RegSetValueExW", 0
@data db szRegCloseKey, "RegCloseKey", 0

@align 4
@data db szRtDefCwd, "C:", 0x5C, 0
@data db szRtDefSelf, "C:", 0x5C, "Windows", 0x5C, "System32", 0x5C, "WINCMD_ARM32.EXE", 0
@data db szRtVtClear, 0x1B, "[2J", 0x1B, "[H", 0
@data db szPipeSubFlags, 0x22, " /S /D /C ", 0x22, 0

@data db szRtDefaultEnv, "COMSPEC=C:", 0x5C, "Windows", 0x5C, "System32", 0x5C, "WINCMD_ARM32.EXE", 0
@data db -, "PATH=C:", 0x5C, "Windows", 0x5C, "System32;C:", 0x5C, "Windows", 0
@data db -, "PATHEXT=.COM;.EXE;.BAT;.CMD", 0
@data db -, "PROMPT=$P$G", 0
@data db -, "OS=Windows_NT", 0, 0

@section bss

@align 8
@res byte g_a32_ctx, 2048
