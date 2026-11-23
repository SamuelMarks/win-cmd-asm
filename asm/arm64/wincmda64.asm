;; @file wincmda64.asm
;; @isa arm64
;; @desc ARM64 AArch64 PE32+ (0xAA64, Subsystem 10.0, HIGH_ENTROPY_VA, DYNAMIC_BASE, NX_COMPAT, GUARD_CF, PAC/BTI, .pdata, zero CRT) shell for Windows 10 & Windows 11 on ARM.

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global a64_ctrl_handler
@global a64_seh_filter
@global mainCRTStartup

@include "../inc/err_code.inc"
@include "../inc/ast_kind.inc"
@include "../inc/cmd_ids.inc"
@include "../inc/win_equ.inc"
@include "../inc/pe_strc.inc"
@include "a64_equ.inc"

; Windows 10 & Windows 11 ARM64 KERNEL32.DLL static IAT baseline (Zero CRT!)
@extern __imp_CloseHandle, qword
@extern __imp_CopyFileW, qword
@extern __imp_CreateDirectoryW, qword
@extern __imp_CreateFileW, qword
@extern __imp_CreatePipe, qword
@extern __imp_CreateProcessW, qword
@extern __imp_DeleteFileW, qword
@extern __imp_DeviceIoControl, qword
@extern __imp_ExitProcess, qword
@extern __imp_FillConsoleOutputCharacterW, qword
@extern __imp_FindClose, qword
@extern __imp_FindFirstFileW, qword
@extern __imp_FindNextFileW, qword
@extern __imp_FreeEnvironmentStringsW, qword
@extern __imp_GetCommandLineW, qword
@extern __imp_GetConsoleMode, qword
@extern __imp_GetConsoleOutputCP, qword
@extern __imp_GetConsoleScreenBufferInfo, qword
@extern __imp_GetCurrentDirectoryW, qword
@extern __imp_GetDiskFreeSpaceExW, qword
@extern __imp_GetEnvironmentStringsW, qword
@extern __imp_GetExitCodeProcess, qword
@extern __imp_GetFileAttributesW, qword
@extern __imp_GetLastError, qword
@extern __imp_GetLocalTime, qword
@extern __imp_GetModuleFileNameW, qword
@extern __imp_GetModuleHandleW, qword
@extern __imp_GetProcAddress, qword
@extern __imp_GetStdHandle, qword
@extern __imp_GetTickCount, qword
@extern __imp_LoadLibraryW, qword
@extern __imp_MoveFileExW, qword
@extern __imp_ReadFile, qword
@extern __imp_RemoveDirectoryW, qword
@extern __imp_SetConsoleCP, qword
@extern __imp_SetConsoleCtrlHandler, qword
@extern __imp_SetConsoleCursorPosition, qword
@extern __imp_SetConsoleMode, qword
@extern __imp_SetConsoleOutputCP, qword
@extern __imp_SetConsoleTextAttribute, qword
@extern __imp_SetConsoleTitleW, qword
@extern __imp_SetFileAttributesW, qword
@extern __imp_SetFilePointerEx, qword
@extern __imp_SetHandleInformation, qword
@extern __imp_SetLocalTime, qword
@extern __imp_SetUnhandledExceptionFilter, qword
@extern __imp_VirtualAlloc, qword
@extern __imp_VirtualFree, qword
@extern __imp_WaitForSingleObject, qword
@extern __imp_WriteConsoleW, qword
@extern __imp_WriteFile, qword

@section text

@include "core/str_utf_a64.inc"
@include "hal_a64.inc"
@include "read/stream_ed_a64.inc"
@include "parse/exp_prs_a64.inc"
@include "vfs/path_glob_a64.inc"
@include "vm8086/vm86_a64.inc"
@include "vm8086/vm86_exec_a64.inc"
@include "rt/env_sa_a64.inc"
@include "rt/exec_for_a64.inc"
@include "rt/exec_ast_a64.inc"
@include "cmd/cmds_a_a64.inc"
@include "cmd/cmds_b_a64.inc"

;; @proc mainCRTStartup
;; @abi win_arm64
;; @in Process entrypoint on Windows 10 & Windows 11 ARM64 (X18 = TEB strictly preserved!)
;; @out Terminates via KERNEL32!ExitProcess with shell exit status w0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc CRT-free Windows ARM64 PE32+ entrypoint with PAC/BTI and 16-byte SP alignment.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    ; Read-only sanity check that X18 (TEB) is non-zero without ever modifying X18
    cmp x18, #0

    bl shell_main_a64
    bl hal_exit_process_a64

    mov w0, #0
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@include "a64_data.inc"

@section bss

@align 16
@res byte g_a64_ctx, 2560
