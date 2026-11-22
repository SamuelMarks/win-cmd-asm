;; @file wincmdv4.asm
;; @isa armv4
;; @desc Strict ARMv4 32-bit RISC PE32 (0x01C0, Subsystem 9, COREDLL.DLL, zero CRT) shell for Windows CE / Pocket PC / Embedded Compact.

@cpu armv4
@format armv4_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "../../inc/err_code.inc"
@include "../../inc/ast_kind.inc"
@include "../../inc/cmd_ids.inc"
@include "../../inc/win_equ.inc"
@include "../../inc/pe_strc.inc"
@include "../a32_equ.inc"

;; @const szCeDefaultEnv_len
;; @desc Exact byte length of default Windows CE double-NUL WTF-8 environment block.
@const szCeDefaultEnv_len = 116

; Windows CE / Pocket PC COREDLL.DLL static IAT baseline (Zero CRT!)
@extern __imp_CloseHandle, dword
@extern __imp_CopyFileW, dword
@extern __imp_CreateDirectoryW, dword
@extern __imp_CreateFileW, dword
@extern __imp_CreateProcessW, dword
@extern __imp_DeleteFileW, dword
@extern __imp_DeviceIoControl, dword
@extern __imp_FindClose, dword
@extern __imp_FindFirstFileW, dword
@extern __imp_FindNextFileW, dword
@extern __imp_GetCommandLineW, dword
@extern __imp_GetDiskFreeSpaceExW, dword
@extern __imp_GetExitCodeProcess, dword
@extern __imp_GetFileAttributesW, dword
@extern __imp_GetLastError, dword
@extern __imp_GetLocalTime, dword
@extern __imp_GetModuleFileNameW, dword
@extern __imp_GetTickCount, dword
@extern __imp_GetVersionExW, dword
@extern __imp_MoveFileW, dword
@extern __imp_ReadFile, dword
@extern __imp_RegCloseKey, dword
@extern __imp_RegCreateKeyExW, dword
@extern __imp_RegOpenKeyExW, dword
@extern __imp_RegQueryValueExW, dword
@extern __imp_RegSetValueExW, dword
@extern __imp_RemoveDirectoryW, dword
@extern __imp_SetFileAttributesW, dword
@extern __imp_SetFilePointer, dword
@extern __imp_SetLocalTime, dword
@extern __imp_TerminateProcess, dword
@extern __imp_VirtualAlloc, dword
@extern __imp_VirtualFree, dword
@extern __imp_WaitForSingleObject, dword
@extern __imp_WriteFile, dword

@section text

@include "hal_ce4.inc"
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
;; @abi arm_ce
;; @in Process entrypoint invoked by Windows CE / Pocket PC / Embedded Compact loader
;; @out r0 = final shell exit status
;; @err Returns shell exit status in r0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc CRT-free strict ARMv4 PE32 entrypoint maintaining 8-byte SP alignment and zero ARMv5+/unaligned ops.
@proc mainCRTStartup
    push {r4, lr}
    bl shell_main_a32
    pop {r4, lr}
    bx lr
@endproc

@section rdata

@include "../a32_data.inc"

@align 4
@data db szCeDefComspec, 0x5C, "Windows", 0x5C, "WINCMD_ARMV4.EXE", 0
@data db szCeVtClear, 0x1B, "[2J", 0x1B, "[H", 0
@data db szCeDefaultEnv, "COMSPEC=", 0x5C, "Windows", 0x5C, "WINCMD_ARMV4.EXE", 0
@data db -, "PATH=", 0x5C, "Windows;", 0x5C, 0
@data db -, "PATHEXT=.COM;.EXE;.BAT;.CMD", 0
@data db -, "PROMPT=$P$G", 0
@data db -, "TEMP=", 0x5C, "Temp", 0
@data db -, "OS=Windows_CE", 0, 0

@section bss

@align 8
@res byte g_a32_ctx, 2048
