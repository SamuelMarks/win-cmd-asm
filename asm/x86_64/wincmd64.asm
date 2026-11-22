;; @file wincmd64.asm
;; @isa x64
;; @desc CRT-free x86-64 PE32+ (Subsystem 5.02, HIGH_ENTROPY_VA, DYNAMIC_BASE, NX_COMPAT, GUARD_CF, CET) shell for XP Pro x64..Windows 11.

@cpu x64
@format coff64
@entry mainCRTStartup
@global w64_ctrl_handler
@global w64_top_seh_filter
@global mainCRTStartup

@include "../inc/err_code.inc"
@include "../inc/ast_kind.inc"
@include "../inc/cmd_ids.inc"
@include "../inc/win_equ.inc"
@include "../inc/pe_strc.inc"
@include "w64_equ.inc"

; Strict Windows XP Pro x64 / Server 2003 SP1 (5.2) KERNEL32.DLL static IAT baseline (Zero CRT!)
@extern __imp_CloseHandle, qword
@extern __imp_CreateDirectoryW, qword
@extern __imp_CreateFileW, qword
@extern __imp_CreatePipe, qword
@extern __imp_CreateProcessW, qword
@extern __imp_DeleteFileW, qword
@extern __imp_DeviceIoControl, qword
@extern __imp_DuplicateHandle, qword
@extern __imp_ExitProcess, qword
@extern __imp_FileTimeToLocalFileTime, qword
@extern __imp_FileTimeToSystemTime, qword
@extern __imp_FillConsoleOutputAttribute, qword
@extern __imp_FillConsoleOutputCharacterW, qword
@extern __imp_FindClose, qword
@extern __imp_FindFirstFileW, qword
@extern __imp_FindNextFileW, qword
@extern __imp_FreeEnvironmentStringsW, qword
@extern __imp_GetACP, qword
@extern __imp_GetCommandLineA, qword
@extern __imp_GetCommandLineW, qword
@extern __imp_GetConsoleMode, qword
@extern __imp_GetConsoleOutputCP, qword
@extern __imp_GetConsoleScreenBufferInfo, qword
@extern __imp_GetCurrentDirectoryW, qword
@extern __imp_GetCurrentProcess, qword
@extern __imp_GetCurrentProcessId, qword
@extern __imp_GetDiskFreeSpaceExW, qword
@extern __imp_GetDiskFreeSpaceW, qword
@extern __imp_GetEnvironmentStringsW, qword
@extern __imp_GetExitCodeProcess, qword
@extern __imp_GetFileAttributesW, qword
@extern __imp_GetFileType, qword
@extern __imp_GetFullPathNameW, qword
@extern __imp_GetLastError, qword
@extern __imp_GetLocalTime, qword
@extern __imp_GetModuleFileNameW, qword
@extern __imp_GetModuleHandleA, qword
@extern __imp_GetProcAddress, qword
@extern __imp_GetShortPathNameW, qword
@extern __imp_GetStdHandle, qword
@extern __imp_GetTickCount, qword
@extern __imp_GetVersion, qword
@extern __imp_GetVolumeInformationW, qword
@extern __imp_LoadLibraryA, qword
@extern __imp_MoveFileExW, qword
@extern __imp_ReadConsoleW, qword
@extern __imp_ReadFile, qword
@extern __imp_RemoveDirectoryW, qword
@extern __imp_SetConsoleCP, qword
@extern __imp_SetConsoleCtrlHandler, qword
@extern __imp_SetConsoleCursorPosition, qword
@extern __imp_SetConsoleMode, qword
@extern __imp_SetConsoleOutputCP, qword
@extern __imp_SetConsoleTextAttribute, qword
@extern __imp_SetConsoleTitleW, qword
@extern __imp_SetCurrentDirectoryW, qword
@extern __imp_SetEnvironmentVariableW, qword
@extern __imp_SetErrorMode, qword
@extern __imp_SetFileAttributesW, qword
@extern __imp_SetFilePointerEx, qword
@extern __imp_SetStdHandle, qword
@extern __imp_SetUnhandledExceptionFilter, qword
@extern __imp_VirtualAlloc, qword
@extern __imp_VirtualFree, qword
@extern __imp_WaitForMultipleObjects, qword
@extern __imp_WaitForSingleObject, qword
@extern __imp_WriteConsoleW, qword
@extern __imp_WriteFile, qword

@section text

@include "core/arena64.inc"
@include "core/math64.inc"
@include "core/utf_cp64.inc"
@include "hal_dyn64.inc"
@include "hal_nt64.inc"
@include "con64.inc"
@include "vfs/path64.inc"
@include "read/stream64.inc"
@include "rt/env_sa64.inc"
@include "parse/expand64.inc"
@include "parse/prs64.inc"
@include "pipe64.inc"
@include "vm8086/vm86_64.inc"
@include "vm8086/vm86_exec64.inc"
@include "rt/exec_ast64.inc"
@include "cmd/cmds64_a.inc"
@include "cmd/cmds64_b.inc"

;; @proc w64_skip_exe_tok
;; @abi win64_ms
;; @in RCX = full process command line ("C:\...\WINCMD64.EXE" args...)
;; @out RAX = cursor immediately past argv[0] executable token
;; @err none
;; @clobbers rax, rcx, rdx
;; @desc Skips argv[0] (handling quoted paths with spaces) in GetCommandLineW->WTF8 string.
@proc w64_skip_exe_tok
    mov rax, rcx
.ws0:
    mov dl, byte [rax]
    cmp dl, 0x20
    je .nx0
    cmp dl, 0x09
    jne .tok
.nx0:
    inc rax
    jmp .ws0
.tok:
    xor ecx, ecx ; quote state
.lp:
    mov dl, byte [rax]
    test dl, dl
    jz .done
    cmp dl, '"'
    jne .chk_sp
    xor ecx, 1
    inc rax
    jmp .lp
.chk_sp:
    test ecx, ecx
    jnz .adv
    cmp dl, 0x20
    je .done
    cmp dl, 0x09
    je .done
.adv:
    inc rax
    jmp .lp
.done:
    ret
@endproc

;; @proc w64_prescan_sim_flags
;; @abi win64_ms
;; @in RCX = raw ASCII command line from GetCommandLineA
;; @out Sets g_sim_nt52 and g_force_vm86 if leading diagnostic flags are present
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Pre-scans --nt52-api-cap and --vm8086 before 64-bit HAL & environment initialization.
@proc_frame w64_prescan_sim_flags, 80, rbx, rsi, rdi
    call w64_skip_exe_tok
    mov rsi, rax
.lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '-'
    jne .done
    cmp byte [rsi + 1], '-'
    jne .done

    lea rdi, [rsp + 32]
    xor ecx, ecx
.fl_cp:
    mov al, byte [rsi + rcx]
    test al, al
    jz .fl_got
    cmp al, 0x20
    je .fl_got
    cmp al, 0x09
    je .fl_got
    cmp ecx, 36
    jae .fl_got
    mov byte [rdi + rcx], al
    inc ecx
    jmp .fl_cp
.fl_got:
    mov byte [rdi + rcx], 0
    lea rbx, [rsi + rcx]

    lea rcx, [rsp + 32]
    lea rdx, [rel szFlagCapNt52_64]
    call str_eq_i64
    test eax, eax
    jnz .set_nt52

    lea rcx, [rsp + 32]
    lea rdx, [rel szFlagVm8086_64]
    call str_eq_i64
    test eax, eax
    jnz .set_vm86
    jmp .done

.set_nt52:
    mov byte [rel g_sim_nt52], 1
    mov rsi, rbx
    jmp .lp
.set_vm86:
    mov byte [rel g_force_vm86], 1
    mov rsi, rbx
    jmp .lp

.done:
    @epilog_ret
@endproc

;; @proc w64_strip_cmd_quotes
;; @abi win64_ms
;; @in RCX = /C or /K command string tail, DL = bForceStrip (/S flag = 1)
;; @out RAX = unquoted or preserved command string in statement arena
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements authentic cmd.exe 5-rule `/C` and `/K` outer-quote stripping heuristic (+ `/S`).
@proc_frame w64_strip_cmd_quotes, 32, rbx, rsi, rdi
    movzx ebx, dl
    call prs_skip_ws64
    mov rsi, rax

    cmp byte [rsi], '"'
    jne .keep_as_is
    mov rcx, rsi
    call str_len64
    cmp rax, 2
    jb .keep_as_is
    mov rdi, rax
    cmp byte [rsi + rdi - 1], '"'
    jne .keep_as_is

    test ebx, ebx
    jnz .strip_outer

    ; Count total quotes; only strip without /S when exactly 2 quotes AND no `& < > ( ) @ ^ |` inside
    xor edx, edx ; quote count
    xor r8d, r8d ; special char flag
    xor r9d, r9d ; whitespace flag
    xor ecx, ecx
.sc_lp:
    cmp rcx, rdi
    jae .eval_5rules
    mov al, byte [rsi + rcx]
    cmp al, '"'
    jne .chk_sp_ch
    inc edx
    jmp .sc_nx
.chk_sp_ch:
    cmp al, '&'
    je .has_spec
    cmp al, '<'
    je .has_spec
    cmp al, '>'
    je .has_spec
    cmp al, '('
    je .has_spec
    cmp al, ')'
    je .has_spec
    cmp al, '@'
    je .has_spec
    cmp al, '^'
    je .has_spec
    cmp al, '|'
    je .has_spec
    cmp al, 0x20
    je .has_ws
    cmp al, 0x09
    jne .sc_nx
.has_ws:
    mov r9d, 1
    jmp .sc_nx
.has_spec:
    mov r8d, 1
.sc_nx:
    inc rcx
    jmp .sc_lp

.eval_5rules:
    cmp edx, 2
    jne .strip_outer
    test r8d, r8d
    jnz .strip_outer
    test r9d, r9d
    jz .strip_outer

    ; Preserve outer quotes ONLY if inner token resolves as an executable; otherwise return stripped rbx!
    mov rcx, rdi
    call arena_alloc64
    jc .keep_as_is
    mov rbx, rax
    lea rcx, [rdi - 2]
    mov rdx, rdi
    mov rdi, rbx
    inc rsi
    rep movsb
    mov byte [rdi], 0
    dec rsi
    mov rdi, rdx

    mov rcx, rbx
    call resolve_executable64
    jnc .keep_as_is
    jmp .ret_rbx

.strip_outer:
    mov rcx, rdi
    call arena_alloc64
    jc .keep_as_is
    mov rbx, rax
    lea rcx, [rdi - 2]
    mov rdi, rbx
    inc rsi
    rep movsb
    mov byte [rdi], 0
.ret_rbx:
    mov rax, rbx
    @epilog_ret

.keep_as_is:
    mov rax, rsi
    @epilog_ret
@endproc

;; @proc w64_run_autorun64
;; @abi win64_ms
;; @in none
;; @out Queries HKLM and HKCU `Software\Microsoft\Command Processor\AutoRun` unless `/D` was specified
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Executes Registry AutoRun commands at shell startup when `/D` is not active.
@proc_frame w64_run_autorun64, 32, rbx
    cmp byte [rel g_skip_autorun], 0
    jne .done

    mov ecx, 2048
    call perm_alloc64
    jc .done
    mov rbx, rax

    mov byte [rbx], 0
    mov rcx, HKEY_LOCAL_MACHINE
    lea rdx, [rel szRegCmdProc64]
    lea r8, [rel szRegValAutoRun64]
    mov r9, rbx
    call hal_reg_query_str64
    jc .try_hkcu
    cmp byte [rbx], 0
    je .try_hkcu
    mov rcx, rbx
    call w64_exec_top_line

.try_hkcu:
    mov byte [rbx], 0
    mov rcx, HKEY_CURRENT_USER
    lea rdx, [rel szRegCmdProc64]
    lea r8, [rel szRegValAutoRun64]
    mov r9, rbx
    call hal_reg_query_str64
    jc .free_out
    cmp byte [rbx], 0
    je .free_out
    mov rcx, rbx
    call w64_exec_top_line

.free_out:
    mov rcx, rbx
    call perm_free64
.done:
    @epilog_ret
@endproc

;; @proc w64_exec_top_line
;; @abi win64_ms
;; @in RCX = NUL-terminated command string
;; @out Expands Phase 1, parses 64-bit Arena AST, and executes via exec_ast_node64
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Executes a single top-level command string (/C, /K, AutoRun, or REPL line).
@proc_frame w64_exec_top_line, 32, rsi
    mov rsi, rcx
    xor edx, edx ; bInBatch = 0
    call expand_phase1_64
    jc .done
    mov rcx, rax
    call parse_line_to_ast64
    jc .syn_err
    mov rcx, rax
    call exec_ast_node64
.done:
    @epilog_ret
.syn_err:
    mov ecx, eax
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc w64_repl_loop
;; @abi win64_ms
;; @in none
;; @out Runs interactive / piped 64-bit REPL until EXIT or stdin EOF
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Interactive and piped command loop supporting multi-line (...) blocks and PROMPT rendering.
@proc_frame w64_repl_loop, 40, rbx, rsi, rdi, r12
    mov ecx, W64_LINE_MAX
    call perm_alloc64
    jc .done
    mov rbx, rax ; persistent REPL line accumulator

    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rcx, rax
    call con_is_handle_tty64
    mov r12d, eax ; is_tty (0/1)

.repl_lp:
    cmp byte [rel g_exit_flag], 0
    jne .free_done
    mov dword [rel g_ctrl_break], 0
    call arena_reset64

    test r12d, r12d
    jz .read_first
    cmp byte [rel g_echo_on], 0
    je .read_first
    call cmd_print_prompt64

.read_first:
    mov rcx, rbx
    mov edx, W64_LINE_MAX - 8
    mov r8d, r12d
    call con_read_line64
    jc .free_done
    mov rsi, rax ; cur_len

.more_blk:
    mov rcx, rbx
    call prs_paren_balance64
    test eax, eax
    jz .exec_it
    test r12d, r12d
    jz .read_more
    lea rcx, [rel szMorePrompt64]
    call con_print_z64
.read_more:
    mov byte [rbx + rsi], 0x0A
    inc rsi
    mov byte [rbx + rsi], 0
    mov edx, W64_LINE_MAX - 8
    sub edx, esi
    cmp edx, 8
    jbe .exec_it
    lea rcx, [rbx + rsi]
    xor r8d, r8d
    call con_read_line64
    jc .exec_it
    add rsi, rax
    jmp .more_blk

.exec_it:
    cmp byte [rbx], 0
    je .repl_lp
    mov rcx, rbx
    call w64_exec_top_line
    jmp .repl_lp

.free_done:
    mov rcx, rbx
    call perm_free64
.done:
    @epilog_ret
@endproc

;; @proc w64_main_body
;; @abi win64_ms
;; @in none
;; @out EAX = final shell exit code
;; @err Returns non-zero exit code on command error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Initializes 64-bit dynamic APIs, VirtualAlloc arena, WTF-8 environment, console, and startup switches (/C, /K, /S, /Q, /D, /A, /U, /E, /V, /F, /T).
@proc_frame w64_main_body, 4136, rbp, rbx, rsi, rdi
    ; Note: 4136 >= 4096 exercises __chkstk guard-page probing + UWOP_ALLOC_LARGE in .xdata!
    mov byte [rel g_echo_on], 1
    mov byte [rel g_extensions_on], 1

    ; Pre-scan --nt52-api-cap and --vm8086 from GetCommandLineA
    call qword [rel __imp_GetCommandLineA]
    mov rcx, rax
    call w64_prescan_sim_flags

    call hal_init_platform64
    call arena_init64
    jc .fatal_oom

    ; Query GetModuleFileNameW into [rsp + 48..2095] and convert to g_self_exe
    xor ecx, ecx
    lea rdx, [rsp + 48]
    mov r8d, 1020
    call qword [rel __imp_GetModuleFileNameW]
    lea rcx, [rsp + 48]
    mov rdx, -1
    lea r8, [rel g_self_exe]
    mov r9d, 2040
    call utf16le_to_wtf8_buf64

    ; Convert GetCommandLineW to persistent g_raw_cmdline
    call qword [rel __imp_GetCommandLineW]
    mov rsi, rax
    mov rcx, rsi
    call wcs_len64
    lea rdi, [rax * 4 + 16]
    mov rcx, rdi
    call perm_alloc64
    jc .fatal_oom
    mov qword [rel g_raw_cmdline], rax
    mov rcx, rsi
    mov rdx, -1
    mov r8, rax
    mov r9d, edi
    call utf16le_to_wtf8_buf64

    call env_init64
    jc .fatal_oom

    ; Synchronize initial =X: drive entry from startup CWD
    mov ecx, 2048
    call arena_alloc64
    jc .init_con
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .init_con
    mov rcx, rdi
    call path_sync_drive_env64

.init_con:
    call con_init64

    ; Parse startup switches after argv[0]
    mov rcx, qword [rel g_raw_cmdline]
    call w64_skip_exe_tok
    mov rsi, rax
    xor ebx, ebx ; mode: 0=REPL, 1=/C, 2=/K
    xor ebp, ebp ; /S quote-strip flag

.sw_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .enter_repl

    ; Skip leading `--*` diagnostic flags already handled by w64_prescan_sim_flags
    cmp byte [rsi], '-'
    jne .chk_slash
    cmp byte [rsi + 1], '-'
    jne .chk_slash
    mov rcx, rsi
    call prs_read_word64
    mov rsi, rdx
    jmp .sw_lp

.chk_slash:
    cmp byte [rsi], '/'
    jne .enter_repl
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, '?'
    je .show_help
    cmp al, 'C'
    je .sw_c
    cmp al, 'K'
    je .sw_k
    cmp al, 'S'
    je .sw_s
    cmp al, 'Q'
    je .sw_q
    cmp al, 'D'
    je .sw_d
    cmp al, 'A'
    je .sw_a
    cmp al, 'U'
    je .sw_u
    cmp al, 'V'
    je .sw_v
    cmp al, 'E'
    je .sw_e
    cmp al, 'F'
    je .sw_f
    cmp al, 'T'
    je .sw_t
    add rsi, 2
    jmp .sw_lp

.sw_s:
    mov ebp, 1
    add rsi, 2
    jmp .sw_lp
.sw_q:
    mov byte [rel g_echo_on], 0
    add rsi, 2
    jmp .sw_lp
.sw_d:
    mov byte [rel g_skip_autorun], 1
    add rsi, 2
    jmp .sw_lp
.sw_a:
    mov byte [rel g_unicode_out], 0
    add rsi, 2
    jmp .sw_lp
.sw_u:
    mov byte [rel g_unicode_out], 1
    add rsi, 2
    jmp .sw_lp

.sw_v:
    add rsi, 2
    cmp byte [rsi], ':'
    jne .v_on_only
    inc rsi
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'F' ; OFF
    je .v_off
    mov byte [rel g_delayed_exp], 1
    add rsi, 2
    jmp .sw_lp
.v_off:
    mov byte [rel g_delayed_exp], 0
    add rsi, 3
    jmp .sw_lp
.v_on_only:
    mov byte [rel g_delayed_exp], 1
    jmp .sw_lp

.sw_e:
    add rsi, 2
    cmp byte [rsi], ':'
    jne .e_on_only
    inc rsi
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'F' ; OFF
    je .e_off
    mov byte [rel g_extensions_on], 1
    add rsi, 2
    jmp .sw_lp
.e_off:
    mov byte [rel g_extensions_on], 0
    add rsi, 3
    jmp .sw_lp
.e_on_only:
    mov byte [rel g_extensions_on], 1
    jmp .sw_lp

.sw_f:
    add rsi, 2
    cmp byte [rsi], ':'
    je .f_comp
    mov ecx, SEM_FAILCRITICALERRORS
    call qword [rel __imp_SetErrorMode]
    jmp .sw_lp
.f_comp:
    inc rsi
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'F'
    je .f_off
    mov byte [rel g_file_comp_on], 1
    add rsi, 2
    jmp .sw_lp
.f_off:
    mov byte [rel g_file_comp_on], 0
    add rsi, 3
    jmp .sw_lp

.sw_t:
    add rsi, 2
    cmp byte [rsi], ':'
    jne .sw_lp
    inc rsi
    mov rcx, rsi
    call prs_read_word64
    mov rdi, rdx
    mov rcx, rax
    call bi_color64
    mov rsi, rdi
    jmp .sw_lp

.sw_c:
    mov ebx, 1
    add rsi, 2
    jmp .run_ck
.sw_k:
    mov ebx, 2
    add rsi, 2

.run_ck:
    cmp ebx, 2
    jne .skip_ck_ar
    call w64_run_autorun64
.skip_ck_ar:
    mov rcx, rsi
    mov dl, bpl
    call w64_strip_cmd_quotes
    mov rcx, rax
    call w64_exec_top_line
    cmp ebx, 2
    je .repl_after_k
    mov eax, dword [rel g_last_exit_code]
    jmp .out

.enter_repl:
    call w64_run_autorun64
    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rcx, rax
    call con_is_handle_tty64
    test eax, eax
    jz .repl_after_k
    cmp byte [rel g_echo_on], 0
    je .repl_after_k
    call bi_ver64
    call con_print_crlf64

.repl_after_k:
    cmp byte [rel g_exit_flag], 0
    jne .ret_code
    call w64_repl_loop

.ret_code:
    mov eax, dword [rel g_last_exit_code]
    jmp .out

.show_help:
    lea rcx, [rel szW64Help]
    call con_print_z64
    xor eax, eax
    jmp .out

.fatal_oom:
    mov eax, ERR_OUT_OF_MEMORY
.out:
    @epilog_ret
@endproc

;; @proc w64_top_seh_filter
;; @abi win64_ms
;; @in RCX = EXCEPTION_POINTERS*
;; @out EAX = EXCEPTION_EXECUTE_HANDLER (1)
;; @err none
;; @clobbers rax
;; @desc Top-level pure-ASM x64 UnhandledExceptionFilter with CET IBT endbr64 landing pad.
@proc w64_top_seh_filter
    endbr64
    mov eax, 1
    ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint invoked directly by 64-bit Windows NT 5.2..11 loader
;; @out Terminates via ExitProcess(ECX)
;; @err Exits with final command status
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc CRT-free x86-64 PE32+ console entrypoint installing top-level SEH filter and running w64_main_body.
@proc_frame mainCRTStartup, 40
    cld
    lea rcx, [rel w64_top_seh_filter]
    call qword [rel __imp_SetUnhandledExceptionFilter]

    call w64_main_body

    mov ecx, eax
    call qword [rel __imp_ExitProcess]
    xor eax, eax
    @epilog_ret
@endproc

@section rdata

; Version, diagnostic flag, & help strings
@data db szVerBannerNt1_64, "WINCMD64 Pure-ASM Shell [Windows NT ", 0
@data db szVerBannerNt2_64, " x86-64 PE32+]", 0x0D, 0x0A, 0
@data db szW64Help, "WINCMD64 [/A | /U] [/Q] [/D] [/E:ON|OFF] [/F:ON|OFF] [/V:ON|OFF] [/T:fg] [[/S] {/C | /K} string]", 0x0D, 0x0A, 0
@data db szFlagCapNt52_64, "--nt52-api-cap", 0
@data db szFlagVm8086_64, "--vm8086", 0
@data db szPipeSim52Arg64, " --nt52-api-cap", 0
@data db szPipeDelayArg64, " /V:ON", 0
@data db szPipeSubArgs64, " /S /D /C ", 0x22, 0

; Dynamic DLL & export names
@data db szDllKernel32_64, "KERNEL32.DLL", 0
@data db szDllNtdll64, "NTDLL.DLL", 0
@data db szDllAdvapi32_64, "ADVAPI32.DLL", 0
@data db szDllMpr64, "MPR.DLL", 0
@data db szDllShell32_64, "SHELL32.DLL", 0

@data db szFnFindFirstExW64, "FindFirstFileExW", 0
@data db szFnCreateHardLinkW64, "CreateHardLinkW", 0
@data db szFnCreateSymLinkW64, "CreateSymbolicLinkW", 0
@data db szFnGetFinalPathW64, "GetFinalPathNameByHandleW", 0
@data db szFnFindFirstStreamW64, "FindFirstStreamW", 0
@data db szFnFindNextStreamW64, "FindNextStreamW", 0
@data db szFnGetProcGroupCnt64, "GetActiveProcessorGroupCount", 0
@data db szFnCreatePseudoCon64, "CreatePseudoConsole", 0
@data db szFnResizePseudoCon64, "ResizePseudoConsole", 0
@data db szFnClosePseudoCon64, "ClosePseudoConsole", 0
@data db szFnGetMachineAttr64, "GetMachineTypeAttributes", 0
@data db szFnRtlGetVersion64, "RtlGetVersion", 0
@data db szFnNtQueryInfoFile64, "NtQueryInformationFile", 0

@data db szFnRegOpenKeyExW64, "RegOpenKeyExW", 0
@data db szFnRegCreateKeyExW64, "RegCreateKeyExW", 0
@data db szFnRegQueryValExW64, "RegQueryValueExW", 0
@data db szFnRegSetValExW64, "RegSetValueExW", 0
@data db szFnRegDeleteValW64, "RegDeleteValueW", 0
@data db szFnRegCloseKey64, "RegCloseKey", 0
@data db szFnWNetAddConn2W64, "WNetAddConnection2W", 0
@data db szFnWNetCancel2W64, "WNetCancelConnection2W", 0
@data db szFnShellExecExW64, "ShellExecuteExW", 0

; Registry subkeys & values
@data db szRegSoftClasses64, "Software\Classes", 0x5C, 0
@data db szRegShellOpenCmd64, "\Shell\Open\Command", 0
@data db szRegCmdProc64, "Software\Microsoft\Command Processor", 0
@data db szRegValAutoRun64, "AutoRun", 0

; Reserved device names
@data db szDevNul64, "NUL", 0
@data db szDevCon64, "CON", 0
@data db szDevPrn64, "PRN", 0
@data db szDevAux64, "AUX", 0
@data db szDevClock64, "CLOCK$", 0
@data db szDevConIn64, "CONIN$", 0
@data db szDevConOut64, "CONOUT$", 0

; Dynamic & standard environment variable names
@data db szDynCd64, "CD", 0
@data db szDynDate64, "DATE", 0
@data db szDynTime64, "TIME", 0
@data db szDynRandom64, "RANDOM", 0
@data db szDynErrorlevel64, "ERRORLEVEL", 0
@data db szDynCmdExtVer64, "CMDEXTVERSION", 0
@data db szDynCmdCmdLine64, "CMDCMDLINE", 0
@data db szDynNuma64, "HIGHESTNUMANODENUMBER", 0
@data db szDynExitCode64, "=ExitCode", 0
@data db szDynExitAscii64, "=ExitCodeAscii", 0
@data db szVarDate64, "DATE", 0
@data db szVarTime64, "TIME", 0
@data db szVarPath64, "PATH", 0
@data db szVarDpath64, "DPATH", 0
@data db szVarPathext64, "PATHEXT", 0
@data db szVarPrompt64, "PROMPT", 0
@data db szVarComspec64, "COMSPEC", 0
@data db szVarCompName64, "COMPUTERNAME", 0
@data db szVarNoDefCwd64, "NoDefaultCurrentDirectoryInExePath", 0
@data db szValExtVer2_64, "2", 0
@data db szValZeroStr64, "0", 0
@data db szDefPathext64, ".COM;.EXE;.BAT;.CMD", 0
@data db szDefPrompt64, "$P$G", 0
@data db szDefDelims64, " ", 0x09, 0

; Parser & AST keywords
@data db szKwRem64, "REM", 0
@data db szKwIf64, "IF", 0
@data db szKwNot64, "NOT", 0
@data db szKwErrorlevel64, "ERRORLEVEL", 0
@data db szKwExist64, "EXIST", 0
@data db szKwDefined64, "DEFINED", 0
@data db szKwCmdExtVer64, "CMDEXTVERSION", 0
@data db szKwElse64, "ELSE", 0
@data db szKwFor64, "FOR", 0
@data db szKwIn64, "IN", 0
@data db szKwDo64, "DO", 0
@data db szKwEqu64, "EQU", 0
@data db szKwNeq64, "NEQ", 0
@data db szKwLss64, "LSS", 0
@data db szKwLeq64, "LEQ", 0
@data db szKwGtr64, "GTR", 0
@data db szKwGeq64, "GEQ", 0
@data db szKwEof64, "EOF", 0
@data db szEofLbl64, "EOF", 0
@data db szFoEol64, "eol=", 0
@data db szFoSkip64, "skip=", 0
@data db szFoDelims64, "delims=", 0
@data db szFoTokens64, "tokens=", 0
@data db szFoUsebackq64, "usebackq", 0

; Built-in command names & aliases (all 47)
@data db szCmdAssoc64, "ASSOC", 0
@data db szCmdBreak64, "BREAK", 0
@data db szCmdCall64, "CALL", 0
@data db szCmdCd64, "CD", 0
@data db szCmdChdir64, "CHDIR", 0
@data db szCmdChcp64, "CHCP", 0
@data db szCmdCls64, "CLS", 0
@data db szCmdCmd64, "CMD", 0
@data db szCmdCommand64, "COMMAND", 0
@data db szCmdColor64, "COLOR", 0
@data db szCmdCopy64, "COPY", 0
@data db szCmdCtty64, "CTTY", 0
@data db szCmdDate64, "DATE", 0
@data db szCmdDel64, "DEL", 0
@data db szCmdErase64, "ERASE", 0
@data db szCmdDir64, "DIR", 0
@data db szCmdDoskey64, "DOSKEY", 0
@data db szCmdDpath64, "DPATH", 0
@data db szCmdEcho64, "ECHO", 0
@data db szCmdEndlocal64, "ENDLOCAL", 0
@data db szCmdExit64, "EXIT", 0
@data db szCmdFtype64, "FTYPE", 0
@data db szCmdGoto64, "GOTO", 0
@data db szCmdKeys64, "KEYS", 0
@data db szCmdLh64, "LH", 0
@data db szCmdLoadhigh64, "LOADHIGH", 0
@data db szCmdMd64, "MD", 0
@data db szCmdMkdir64, "MKDIR", 0
@data db szCmdMklink64, "MKLINK", 0
@data db szCmdMode64, "MODE", 0
@data db szCmdMove64, "MOVE", 0
@data db szCmdPath64, "PATH", 0
@data db szCmdPause64, "PAUSE", 0
@data db szCmdPopd64, "POPD", 0
@data db szCmdPrompt64, "PROMPT", 0
@data db szCmdPushd64, "PUSHD", 0
@data db szCmdRd64, "RD", 0
@data db szCmdRmdir64, "RMDIR", 0
@data db szCmdRem64, "REM", 0
@data db szCmdRen64, "REN", 0
@data db szCmdRename64, "RENAME", 0
@data db szCmdSet64, "SET", 0
@data db szCmdSetlocal64, "SETLOCAL", 0
@data db szCmdShift64, "SHIFT", 0
@data db szCmdStart64, "START", 0
@data db szCmdTime64, "TIME", 0
@data db szCmdTitle64, "TITLE", 0
@data db szCmdTruename64, "TRUENAME", 0
@data db szCmdType64, "TYPE", 0
@data db szCmdVer64, "VER", 0
@data db szCmdVerify64, "VERIFY", 0
@data db szCmdVol64, "VOL", 0

; Built-in options & UI strings
@data db szExtCom64, ".COM", 0
@data db szExtExe64, ".EXE", 0
@data db szExtBat64, ".BAT", 0
@data db szExtCmd64, ".CMD", 0
@data db szOn64, "ON", 0
@data db szOff64, "OFF", 0
@data db szOnWord64, "on", 0
@data db szOffWord64, "off", 0
@data db szOptEnDelay64, "EnableDelayedExpansion", 0
@data db szOptDisDelay64, "DisableDelayedExpansion", 0
@data db szOptEnExt64, "EnableExtensions", 0
@data db szOptDisExt64, "DisableExtensions", 0
@data db szEchoIsOn64, "ECHO is on.", 0
@data db szEchoIsOff64, "ECHO is off.", 0
@data db szEnvNotDef64, "Environment variable not defined", 0x0D, 0x0A, 0
@data db szActiveCp64, "Active code page: ", 0
@data db szNullParen64, "(null)", 0
@data db szPauseMsg64, "Press any key to continue . . . ", 0
@data db szMorePrompt64, "More? ", 0
@data db szMsgBreak64, "BREAK is ", 0
@data db szMsgVerify64, "VERIFY is ", 0
@data db szMsgKeys64, "KEYS is ", 0
@data db szAssocNotFound64, "File association not found for extension", 0x0D, 0x0A, 0
@data db szFtypeNotFound64, "File type not found or no open command associated", 0x0D, 0x0A, 0
@data db szMklinkSym64, "symbolic link created for ", 0
@data db szMklinkSymD64, "symbolic link created for ", 0
@data db szMklinkHard64, "Hardlink created for ", 0
@data db szMklinkJunc64, "Junction created for ", 0
@data db szMklinkArrow64, " <<===>> ", 0
@data db szOneFileCopied64, "        1 file(s) copied.", 0x0D, 0x0A, 0
@data db szVolInDrive64, " Volume in drive ", 0
@data db szVolIs64, " is ", 0
@data db szVolHasNoLbl64, " has no label.", 0x0D, 0x0A, 0
@data db szVolSerialIs64, " Volume Serial Number is ", 0
@data db szCurDateIs64, "The current date is: ", 0
@data db szCurTimeIs64, "The current time is: ", 0
@data db szDirOfHdr64, " Directory of ", 0
@data db szDirTagDir64, "    <DIR>          ", 0
@data db szDirTagJunction64, "    <JUNCTION>     ", 0
@data db szDirTagSymlink64, "    <SYMLINK>      ", 0
@data db szDirTagSymlinkD64, "    <SYMLINKD>     ", 0
@data db szDirStreamIndent64, "                  ", 0
@data db szDirSumPad64, "               ", 0
@data db szDirFilesWord64, " File(s)   ", 0
@data db szDirBytesWord64, " bytes", 0x0D, 0x0A, 0
@data db szDirDirsWord64, " Dir(s)    ", 0
@data db szDirBytesFree64, " bytes free", 0x0D, 0x0A, 0
@data db szEmpty64, 0
@data db szDot64, ".", 0
@data db szStar64, "*", 0
@data db szDash64, "-", 0
@data db szEqSign64, "=", 0
@data db szOneSpace64, " ", 0
@data db szBackslashOnly64, 0x5C, 0
@data db szCrLf64, 0x0D, 0x0A, 0

; Canonical ERR_* error messages
@data db szErrFnf64, "The system cannot find the file specified.", 0
@data db szErrPnf64, "The system cannot find the path specified.", 0
@data db szErrAcc64, "Access is denied.", 0
@data db szErrOom64, "Not enough storage is available to complete this operation.", 0
@data db szErrExists64, "A subdirectory or file already exists.", 0
@data db szErrSyntax64, "The syntax of the command is incorrect.", 0
@data db szErrDivZero64, "Divide by zero error.", 0
@data db szErrInvNum64, "Invalid number. Numeric constants are either decimal (17), hexadecimal (0x11), or octal (021).", 0
@data db szErrBadCmd64, "is not recognized as an internal or external command, operable program or batch file.", 0
@data db szErrBadLbl64, "The system cannot find the batch label specified.", 0
@data db szErrGen64, "The command failed to complete.", 0

@section bss

; Core arena & environment state (8-byte aligned first!)
@res qword g_arena_base, 1
@res qword g_arena_ptr, 1
@res qword g_arena_committed, 1
@res qword g_arena_limit, 1
@res qword g_env_buf, 1
@res qword g_env_used, 1
@res qword g_raw_cmdline, 1
@res qword g_sa_ptr, 1

; Dynamic DLL handles & 64-bit capability function pointers
@res qword g_hmod_kernel32, 1
@res qword g_hmod_advapi32, 1
@res qword pfnFindFirstFileExW64, 1
@res qword pfnCreateHardLinkW64, 1
@res qword pfnCreateSymbolicLinkW64, 1
@res qword pfnGetFinalPathByHandleW64, 1
@res qword pfnFindFirstStreamW64, 1
@res qword pfnFindNextStreamW64, 1
@res qword pfnGetActiveProcGroupCnt64, 1
@res qword pfnCreatePseudoConsole64, 1
@res qword pfnResizePseudoConsole64, 1
@res qword pfnClosePseudoConsole64, 1
@res qword pfnGetMachineTypeAttr64, 1
@res qword pfnRtlGetVersion64, 1
@res qword pfnNtQueryInfoFile64, 1
@res qword pfnRegOpenKeyExW64, 1
@res qword pfnRegCreateKeyExW64, 1
@res qword pfnRegQueryValueExW64, 1
@res qword pfnRegSetValueExW64, 1
@res qword pfnRegDeleteValueW64, 1
@res qword pfnRegCloseKey64, 1
@res qword pfnWNetAddConn2W64, 1
@res qword pfnWNetCancelConn2W64, 1
@res qword pfnShellExecuteExW64, 1

; Runtime qword arrays (8-byte aligned!)
@res qword g_for_vals, 32
@res qword g_hist_ptrs, 64
@res qword g_pushd_dirs, 32
@res byte g_batch_stack, 3584
@res byte g_setlocal_stack, 1024

; 32-bit OS / runtime counters
@res dword g_os_major, 1
@res dword g_os_minor, 1
@res dword g_os_build, 1
@res dword g_active_cp, 1
@res dword g_ctrl_break, 1
@res dword g_last_exit_code, 1
@res dword g_rng_state, 1
@res dword g_setlocal_depth, 1
@res dword g_batch_depth, 1
@res dword g_for_depth, 1
@res dword g_hist_count, 1
@res dword g_pushd_depth, 1

; 16-bit & 8-bit runtime flags and fixed buffers
@res word g_default_attr, 1
@res byte g_sim_nt52, 1
@res byte g_force_vm86, 1
@res byte g_vt_enabled, 1
@res byte g_conpty_avail, 1
@res byte g_unicode_out, 1
@res byte g_echo_on, 1
@res byte g_extensions_on, 1
@res byte g_delayed_exp, 1
@res byte g_file_comp_on, 1
@res byte g_skip_autorun, 1
@res byte g_exit_flag, 1
@res byte g_batch_ret_flag, 1
@res byte g_goto_pending, 1
@res byte g_dir_need_short, 1
@res byte g_advapi_loaded, 1
@res byte g_mpr_loaded, 1
@res byte g_shell32_loaded, 1
@res byte g_break_on, 1
@res byte g_verify_on, 1
@res byte g_keys_on, 1
@res byte g_for_vars, 32
@res byte g_pushd_mapped, 32
@res byte g_goto_label, 256
@res byte g_self_exe, 2048
