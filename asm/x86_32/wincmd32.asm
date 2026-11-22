;; @file wincmd32.asm
;; @isa 386
;; @desc CRT-free IA-32 PE32 (Subsystem 3.10, /SAFESEH, DYNAMIC_BASE, NX_COMPAT) shell for NT 3.1..11 & Win95/98/Me.

@cpu 386
@format coff32
@safeseh _w32_seh_handler
@global _w32_seh_handler
@global _mainCRTStartup

@include "../inc/err_code.inc"
@include "../inc/ast_kind.inc"
@include "../inc/cmd_ids.inc"
@include "../inc/win_equ.inc"
@include "../inc/pe_strc.inc"
@include "w32_equ.inc"

; Strict Windows NT 3.1 RTM (Build 528) + Windows 95 RTM (Build 950) KERNEL32.DLL static IAT baseline (Zero CRT!)
@extern __imp__CloseHandle@4, dword
@extern __imp__CopyFileA@12, dword
@extern __imp__CopyFileW@12, dword
@extern __imp__CreateDirectoryA@8, dword
@extern __imp__CreateDirectoryW@8, dword
@extern __imp__CreateFileA@28, dword
@extern __imp__CreateFileW@28, dword
@extern __imp__CreatePipe@16, dword
@extern __imp__CreateProcessA@40, dword
@extern __imp__CreateProcessW@40, dword
@extern __imp__DeleteFileA@4, dword
@extern __imp__DeleteFileW@4, dword
@extern __imp__DeviceIoControl@32, dword
@extern __imp__DuplicateHandle@28, dword
@extern __imp__ExitProcess@4, dword
@extern __imp__FileTimeToLocalFileTime@8, dword
@extern __imp__FileTimeToSystemTime@8, dword
@extern __imp__FillConsoleOutputAttribute@20, dword
@extern __imp__FillConsoleOutputCharacterA@20, dword
@extern __imp__FindClose@4, dword
@extern __imp__FindFirstFileA@8, dword
@extern __imp__FindFirstFileW@8, dword
@extern __imp__FindNextFileA@8, dword
@extern __imp__FindNextFileW@8, dword
@extern __imp__GetACP@0, dword
@extern __imp__GetCPInfo@8, dword
@extern __imp__GetCommandLineA@0, dword
@extern __imp__GetCommandLineW@0, dword
@extern __imp__GetConsoleMode@8, dword
@extern __imp__GetConsoleOutputCP@0, dword
@extern __imp__GetConsoleScreenBufferInfo@8, dword
@extern __imp__GetCurrentDirectoryA@8, dword
@extern __imp__GetCurrentDirectoryW@8, dword
@extern __imp__GetCurrentProcess@0, dword
@extern __imp__GetCurrentProcessId@0, dword
@extern __imp__GetDiskFreeSpaceA@20, dword
@extern __imp__GetDiskFreeSpaceW@20, dword
@extern __imp__GetDriveTypeA@4, dword
@extern __imp__GetExitCodeProcess@8, dword
@extern __imp__GetFileAttributesA@4, dword
@extern __imp__GetFileAttributesW@4, dword
@extern __imp__GetFileType@4, dword
@extern __imp__GetFullPathNameA@16, dword
@extern __imp__GetFullPathNameW@16, dword
@extern __imp__GetLastError@0, dword
@extern __imp__GetLocalTime@4, dword
@extern __imp__GetModuleFileNameA@12, dword
@extern __imp__GetModuleFileNameW@12, dword
@extern __imp__GetModuleHandleA@4, dword
@extern __imp__GetProcAddress@8, dword
@extern __imp__GetStdHandle@4, dword
@extern __imp__GetTickCount@0, dword
@extern __imp__GetVersion@0, dword
@extern __imp__GetVolumeInformationA@32, dword
@extern __imp__GetVolumeInformationW@32, dword
@extern __imp__LoadLibraryA@4, dword
@extern __imp__MoveFileA@8, dword
@extern __imp__MoveFileW@8, dword
@extern __imp__ReadConsoleA@20, dword
@extern __imp__ReadConsoleW@20, dword
@extern __imp__ReadFile@20, dword
@extern __imp__RemoveDirectoryA@4, dword
@extern __imp__RemoveDirectoryW@4, dword
@extern __imp__SetConsoleCP@4, dword
@extern __imp__SetConsoleCtrlHandler@8, dword
@extern __imp__SetConsoleCursorPosition@8, dword
@extern __imp__SetConsoleMode@8, dword
@extern __imp__SetConsoleOutputCP@4, dword
@extern __imp__SetConsoleTextAttribute@8, dword
@extern __imp__SetConsoleTitleA@4, dword
@extern __imp__SetConsoleTitleW@4, dword
@extern __imp__SetCurrentDirectoryA@4, dword
@extern __imp__SetCurrentDirectoryW@4, dword
@extern __imp__SetEnvironmentVariableA@8, dword
@extern __imp__SetEnvironmentVariableW@8, dword
@extern __imp__SetErrorMode@4, dword
@extern __imp__SetFileAttributesA@8, dword
@extern __imp__SetFileAttributesW@8, dword
@extern __imp__SetFilePointer@16, dword
@extern __imp__SetStdHandle@8, dword
@extern __imp__VirtualAlloc@16, dword
@extern __imp__VirtualFree@12, dword
@extern __imp__WaitForMultipleObjects@16, dword
@extern __imp__WaitForSingleObject@8, dword
@extern __imp__WriteConsoleA@20, dword
@extern __imp__WriteConsoleW@20, dword
@extern __imp__WriteFile@20, dword

@section text

;; @proc _w32_seh_handler
;; @abi win32
;; @in [ESP+4] = EXCEPTION_RECORD*, [ESP+8] = EstablisherFrame, [ESP+12] = CONTEXT*
;; @out EAX = ExceptionContinueSearch (1)
;; @err none
;; @clobbers eax
;; @desc /SAFESEH-registered IA-32 per-thread FS:[0] exception handler.
@proc _w32_seh_handler
    mov eax, 1
    ret
@endproc

@include "core/arena32.inc"
@include "core/math32_64.inc"
@include "core/utf_cp32.inc"
@include "hal_dyn.inc"
@include "hal_ntw.inc"
@include "hal_9xa.inc"
@include "con32.inc"
@include "vfs/path32.inc"
@include "read/stream32.inc"
@include "rt/env_sa32.inc"
@include "parse/expand32.inc"
@include "parse/prs32.inc"
@include "pipe32.inc"
@include "vm8086/vm86_32.inc"
@include "vm8086/vm86_exec.inc"
@include "rt/exec_ast32.inc"
@include "cmd/cmds32_a.inc"
@include "cmd/cmds32_b.inc"

;; @proc w32_skip_exe_tok
;; @abi win32
;; @in ESI = full raw process command line
;; @out EAX = pointer to argument tail immediately after argv[0]
;; @err none
;; @clobbers eax, edx
;; @desc Skips quoted or unquoted argv[0] executable path at the front of GetCommandLineW/A.
@proc w32_skip_exe_tok
    mov eax, esi
.ws0:
    mov dl, byte [eax]
    cmp dl, 0x20
    je .adv0
    cmp dl, 0x09
    jne .tok
.adv0:
    inc eax
    jmp .ws0
.tok:
    cmp dl, '"'
    je .in_q
.unq:
    mov dl, byte [eax]
    test dl, dl
    jz .done
    cmp dl, 0x20
    je .done
    cmp dl, 0x09
    je .done
    inc eax
    jmp .unq
.in_q:
    inc eax
.qlp:
    mov dl, byte [eax]
    test dl, dl
    jz .done
    inc eax
    cmp dl, '"'
    jne .qlp
.done:
    ret
@endproc

;; @proc w32_prescan_sim_flags
;; @abi win32
;; @in ESI = raw ASCII command line from GetCommandLineA
;; @out Sets g_sim_win9x, g_sim_nt31, and g_force_vm86 if leading diagnostic flags are present
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Pre-scans --win9x-ansi-sim, --nt31-api-cap, and --vm8086 before HAL & environment initialization.
@proc w32_prescan_sim_flags
    push ebx
    push esi
    push edi

    call w32_skip_exe_tok
    mov esi, eax
.lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '-'
    jne .done
    cmp byte [esi + 1], '-'
    jne .done

    ; Extract --flag word onto stack [esp..esp+32]
    sub esp, 36
    mov edi, esp
    xor ecx, ecx
.fl_cp:
    mov al, byte [esi + ecx]
    cmp al, 0
    je .fl_got
    cmp al, 0x20
    je .fl_got
    cmp al, 0x09
    je .fl_got
    cmp ecx, 32
    jae .fl_got
    mov byte [edi + ecx], al
    inc ecx
    jmp .fl_cp
.fl_got:
    mov byte [edi + ecx], 0
    lea ebx, [esi + ecx]

    mov esi, edi
    mov edi, addr(szFlagSim9x)
    call str_eq_i32
    test eax, eax
    jnz .set_9x

    mov esi, esp
    mov edi, addr(szFlagCapNt31)
    call str_eq_i32
    test eax, eax
    jnz .set_nt31

    mov esi, esp
    mov edi, addr(szFlagVm8086)
    call str_eq_i32
    test eax, eax
    jnz .set_vm86

    add esp, 36
    jmp .done

.set_9x:
    mov byte [g_sim_win9x], 1
    add esp, 36
    mov esi, ebx
    jmp .lp
.set_nt31:
    mov byte [g_sim_nt31], 1
    add esp, 36
    mov esi, ebx
    jmp .lp
.set_vm86:
    mov byte [g_force_vm86], 1
    add esp, 36
    mov esi, ebx
    jmp .lp

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc w32_strip_cmd_quotes
;; @abi win32
;; @in ESI = /C or /K command tail, CL = bStripFlag (/S = 1)
;; @out EAX = command string ready for Phase 1 expansion
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Applies cmd.exe /S and outer-quote heuristic on /C and /K command strings.
@proc w32_strip_cmd_quotes
    push ebx
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '"'
    jne .keep_as_is

    mov bl, cl ; /S flag
    call str_len32
    cmp ecx, 2
    jb .keep_as_is
    cmp byte [esi + ecx - 1], '"'
    jne .keep_as_is

    ; If /S is explicitly set, ALWAYS strip first and last quote!
    test bl, bl
    jnz .strip_outer

    ; Authentic 5-Rule cmd.exe heuristic:
    ; Preserve outer quotes ONLY if: (1) no /S, (2) quote_count == 2, (3) zero &<>()@^| chars,
    ; (4) >= 1 whitespace char, AND (5) inner string resolves as an executable!
    ; Otherwise, strip first '"' and last '"'!
    xor edx, edx ; quote_count
    xor ebx, ebx ; ws_count
    xor edi, edi
.cnt_q:
    cmp edi, ecx
    jae .chk_qcnt
    mov al, byte [esi + edi]
    cmp al, '"'
    je .inc_q
    cmp al, '&'
    je .strip_outer
    cmp al, '<'
    je .strip_outer
    cmp al, '>'
    je .strip_outer
    cmp al, '('
    je .strip_outer
    cmp al, ')'
    je .strip_outer
    cmp al, '@'
    je .strip_outer
    cmp al, '^'
    je .strip_outer
    cmp al, '|'
    je .strip_outer
    cmp al, 0x20
    je .inc_ws
    cmp al, 0x09
    je .inc_ws
    jmp .nx_q
.inc_q:
    inc edx
    jmp .nx_q
.inc_ws:
    inc ebx
.nx_q:
    inc edi
    jmp .cnt_q

.chk_qcnt:
    cmp edx, 2
    jne .strip_outer
    test ebx, ebx
    jz .strip_outer

    ; Rule 5: check if string resolves as an executable file; if yes, keep outer quotes!
    push ecx
    push esi
    call resolve_executable32
    pop esi
    pop ecx
    jnc .keep_as_is

.strip_outer:
    ; Find last '"' in [esi .. esi+ecx) and remove first '"' + that last '"', preserving any trailing text!
    mov ebx, ecx
.find_last_q:
    dec ebx
    jz .keep_as_is
    cmp byte [esi + ebx], '"'
    jne .find_last_q

    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .keep_as_is
    mov edi, eax
    push eax
    ; Copy [esi+1 .. esi+ebx)
    push ecx
    push esi
    inc esi
    lea ecx, [ebx - 1]
    rep movsb
    pop esi
    pop ecx
    ; Copy any tail [esi+ebx+1 .. esi+ecx] including NUL!
    lea esi, [esi + ebx + 1]
    sub ecx, ebx
    rep movsb
    pop eax
    pop edi
    pop esi
    pop ebx
    ret

.keep_as_is:
    mov eax, esi
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc w32_run_autorun32
;; @abi win32
;; @in none (skipped when g_disable_autorun != 0)
;; @out Queries HKLM and HKCU `Software\Microsoft\Command Processor\AutoRun` and executes if present
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Executes Registry AutoRun commands at interactive/non-/D startup.
@proc w32_run_autorun32
    cmp byte [g_disable_autorun], 0
    jne .done
    push ebx
    push esi
    push edi

    mov ecx, 2048
    call arena_alloc32
    jc .pop_done
    mov ebx, eax ; valBuf

    ; 1. HKLM\Software\Microsoft\Command Processor -> AutoRun
    push ebx
    mov ecx, ebx
    mov ebx, HKEY_LOCAL_MACHINE
    mov esi, addr(szRegCmdProc)
    mov edi, addr(szRegValAutoRun)
    mov edx, 2040
    call hal_reg_query_str32
    pop ebx
    jc .try_hkcu
    cmp byte [ebx], 0
    je .try_hkcu
    mov esi, ebx
    call w32_exec_top_line

.try_hkcu:
    push ebx
    mov ecx, ebx
    mov ebx, HKEY_CURRENT_USER
    mov esi, addr(szRegCmdProc)
    mov edi, addr(szRegValAutoRun)
    mov edx, 2040
    call hal_reg_query_str32
    pop ebx
    jc .pop_done
    cmp byte [ebx], 0
    je .pop_done
    mov esi, ebx
    call w32_exec_top_line

.pop_done:
    pop edi
    pop esi
    pop ebx
.done:
    ret
@endproc

;; @proc w32_exec_top_line
;; @abi win32
;; @in ESI = NUL-terminated command string
;; @out Expands Phase 1, parses Arena AST, and executes via exec_ast_node32
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Executes a single top-level command string (/C, /K, AutoRun, or REPL line).
@proc w32_exec_top_line
    push esi
    xor ecx, ecx ; bInBatch = 0
    call expand_phase1_32
    jc .done_top
    mov esi, eax
    call parse_line_to_ast32
    jc .syn_err
    mov esi, eax
    call exec_ast_node32
.done_top:
    pop esi
    ret
.syn_err:
    call con_report_err32
    mov dword [g_last_exit_code], 1
    pop esi
    ret
@endproc

;; @proc w32_repl_loop
;; @abi win32
;; @in none
;; @out Runs interactive / piped REPL until EXIT or stdin EOF
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Interactive and piped command loop supporting multi-line (...) blocks and PROMPT rendering.
@proc w32_repl_loop
    push ebx
    push esi
    push edi

    mov ecx, W32_LINE_MAX
    call perm_alloc32
    jc .done
    mov ebx, eax ; persistent REPL line accumulator

    ; Check if stdin is an interactive TTY (only print banner & prompt when TTY and ECHO ON!)
    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    push ebx
    mov ebx, eax
    call con_is_handle_tty32
    pop ebx
    mov edi, eax ; is_tty (0/1)

.repl_lp:
    cmp byte [g_exit_flag], 0
    jne .free_out
    mov dword [g_ctrl_break], 0
    call arena_reset32

    test edi, edi
    jz .rd_line
    cmp byte [g_echo_on], 0
    je .rd_line
    call cmd_print_prompt32

.rd_line:
    push edi
    mov edi, ebx
    mov ecx, W32_LINE_MAX - 8
    call con_read_line32
    pop edi
    jc .free_out
    mov esi, eax ; cur_len

    ; Accumulate continuation lines while unquoted '(' > ')'!
.more_parens:
    push esi
    mov esi, ebx
    call prs_paren_balance32
    pop esi
    test eax, eax
    jz .run_it

    test edi, edi
    jz .rd_more
    push esi
    mov esi, addr(szMorePrompt32)
    call con_print_z32
    pop esi
.rd_more:
    mov byte [ebx + esi], 0x0A
    inc esi
    mov byte [ebx + esi], 0
    push edi
    lea edi, [ebx + esi]
    mov ecx, W32_LINE_MAX - 8
    sub ecx, esi
    call con_read_line32
    pop edi
    jc .run_it
    add esi, eax
    jmp .more_parens

.run_it:
    cmp byte [ebx], 0
    je .repl_lp
    mov esi, ebx
    call w32_exec_top_line
    jmp .repl_lp

.free_out:
    mov eax, ebx
    call perm_free32
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc w32_main_body
;; @abi win32
;; @in none
;; @out EAX = final process exit code
;; @err Returns non-zero exit code on failure
;; @clobbers eax, ecx, edx
;; @desc Orchestrates Win32 platform detection, arena/env init, switch parsing, AutoRun, /C, /K, and REPL.
@proc w32_main_body
    push ebx
    push esi
    push edi
    push ebp

    ; Defaults: ECHO ON, Extensions ON, DelayedExpansion OFF
    mov byte [g_echo_on], 1
    mov byte [g_extensions_on], 1
    mov byte [g_delayed_exp], 0

    ; 1. Pre-scan diagnostic flags (--win9x-ansi-sim, --nt31-api-cap, --vm8086) from GetCommandLineA
    call dword [__imp__GetCommandLineA@0]
    mov esi, eax
    call w32_prescan_sim_flags

    ; 2. Initialize OS capability table & *W/*A platform mode, then statement arena!
    call hal_init_platform32
    call arena_init32
    jc .fatal_oom

    ; 3. Capture full raw command line (*W on NT, *A on Win9x) into persistent buffer for %CMDCMDLINE%
    cmp byte [g_is_win9x], 0
    jne .cmd_a
    call dword [__imp__GetCommandLineW@0]
    mov esi, eax
    call wcs_len32
    mov ebx, ecx
    lea ecx, [ecx + ecx * 2 + 16]
    call perm_alloc32
    jc .fatal_oom
    mov dword [g_raw_cmdline], eax
    mov edi, eax
    lea edx, [ebx + ebx * 2 + 12]
    mov ecx, ebx
    call utf16le_to_wtf8_buf
    jmp .got_cmdline

.cmd_a:
    call dword [__imp__GetCommandLineA@0]
    mov esi, eax
    call str_len32
    inc ecx
    push ecx
    call perm_alloc32
    pop ecx
    jc .fatal_oom
    mov dword [g_raw_cmdline], eax
    mov edi, eax
    rep movsb

.got_cmdline:
    ; 4. Capture our own executable path into g_self_exe and import process environment
    cmp byte [g_is_win9x], 0
    jne .self_a
    mov ecx, 2048
    call arena_alloc32
    jc .fatal_oom
    mov esi, eax
    push 1020
    push esi
    push 0
    call dword [__imp__GetModuleFileNameW@12]
    mov ecx, eax
    mov edi, addr(g_self_exe)
    mov edx, 1020
    call utf16le_to_wtf8_buf
    jmp .init_env

.self_a:
    push 1020
    push addr(g_self_exe)
    push 0
    call dword [__imp__GetModuleFileNameA@12]

.init_env:
    call env_init32
    jc .fatal_oom

    ; Point %COMSPEC% at our running WINCMD32.EXE binary!
    mov esi, addr(szVarComspec)
    mov edi, addr(g_self_exe)
    call env_set32

    call con_init32

    ; 5. Parse startup switches after argv[0]
    mov esi, dword [g_raw_cmdline]
    call w32_skip_exe_tok
    mov esi, eax
    xor ebx, ebx ; mode: 0=interactive, 1=/C, 2=/K
    xor ebp, ebp ; bStripQuotes (/S = 1)

.sw_lp:
    call prs_skip_ws32
    mov esi, eax
    mov al, byte [esi]
    test al, al
    jz .enter_repl

    ; Skip leading --win9x-ansi-sim / --nt31-api-cap / --vm8086 flags already pre-scanned!
    cmp al, '-'
    jne .chk_slash
    cmp byte [esi + 1], '-'
    jne .chk_slash
    call prs_read_word32
    mov esi, edx
    jmp .sw_lp

.chk_slash:
    cmp al, '/'
    jne .enter_repl
    mov al, byte [esi + 1]
    cmp al, '?'
    je .show_help
    call ascii_upper_al
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
    cmp al, 'E'
    je .sw_e
    cmp al, 'V'
    je .sw_v
    cmp al, 'F'
    je .sw_f
    cmp al, 'T'
    je .sw_t
    ; Unknown startup switch -> advance past it
    add esi, 2
    jmp .sw_lp

.sw_s:
    mov ebp, 1
    add esi, 2
    jmp .sw_lp
.sw_q:
    mov byte [g_echo_on], 0
    add esi, 2
    jmp .sw_lp
.sw_d:
    mov byte [g_disable_autorun], 1
    add esi, 2
    jmp .sw_lp
.sw_a:
    mov byte [g_unicode_out], 0
    add esi, 2
    jmp .sw_lp
.sw_u:
    mov byte [g_unicode_out], 1
    add esi, 2
    jmp .sw_lp

.sw_e:
    add esi, 2
    cmp byte [esi], ':'
    jne .sw_lp
    inc esi
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'F' ; OFF
    je .e_off
    mov byte [g_extensions_on], 1
    add esi, 2
    jmp .sw_lp
.e_off:
    mov byte [g_extensions_on], 0
    add esi, 3
    jmp .sw_lp

.sw_v:
    add esi, 2
    cmp byte [esi], ':'
    jne .sw_lp
    inc esi
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'F' ; OFF
    je .v_off
    mov byte [g_delayed_exp], 1
    add esi, 2
    jmp .sw_lp
.v_off:
    mov byte [g_delayed_exp], 0
    add esi, 3
    jmp .sw_lp

.sw_f:
    ; `/F:ON`, `/F:OFF`, or standalone `/F` (fail critical errors!)
    add esi, 2
    cmp byte [esi], ':'
    je .f_comp
    push SEM_FAILCRITICALERRORS
    call dword [__imp__SetErrorMode@4]
    jmp .sw_lp
.f_comp:
    inc esi
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'F'
    je .f_off
    mov byte [g_file_comp_on], 1
    add esi, 2
    jmp .sw_lp
.f_off:
    mov byte [g_file_comp_on], 0
    add esi, 3
    jmp .sw_lp

.sw_t:
    add esi, 2
    cmp byte [esi], ':'
    jne .sw_lp
    inc esi
    push esi
    call prs_read_word32
    pop ecx
    mov edi, edx
    mov esi, eax
    call bi_color32
    mov esi, edi
    jmp .sw_lp

.sw_c:
    mov ebx, 1
    add esi, 2
    jmp .run_ck
.sw_k:
    mov ebx, 2
    add esi, 2

.run_ck:
    cmp ebx, 2
    jne .skip_ck_ar
    call w32_run_autorun32
.skip_ck_ar:
    mov ecx, ebp
    call w32_strip_cmd_quotes
    mov esi, eax
    call w32_exec_top_line
    cmp ebx, 2
    je .repl_after_k
    mov eax, dword [g_last_exit_code]
    jmp .out

.enter_repl:
    call w32_run_autorun32
    ; Print interactive banner only when stdin is a real console TTY and /Q was not set!
    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov ebx, eax
    call con_is_handle_tty32
    test eax, eax
    jz .repl_after_k
    cmp byte [g_echo_on], 0
    je .repl_after_k
    call bi_ver32
    call con_print_crlf32

.repl_after_k:
    cmp byte [g_exit_flag], 0
    jne .ret_code
    call w32_repl_loop

.ret_code:
    mov eax, dword [g_last_exit_code]
    jmp .out

.show_help:
    mov esi, addr(szW32Help)
    call con_print_z32
    xor eax, eax
    jmp .out

.fatal_oom:
    mov eax, ERR_OUT_OF_MEMORY
.out:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint invoked directly by NT 3.1..11 or Win95..Me loader
;; @out Terminates via ExitProcess(EAX)
;; @err Exits with final command status
;; @clobbers eax, ecx, edx
;; @desc CRT-free PE32 console entrypoint installing /SAFESEH FS:[0] frame and running w32_main_body.
@proc _mainCRTStartup
    push ebp
    mov ebp, esp
    cld
    push addr(_w32_seh_handler)
    push dword [fs:0]
    mov dword [fs:0], esp

    call w32_main_body

    pop dword [fs:0]
    add esp, 4
    push eax
    call dword [__imp__ExitProcess@4]
    pop ebp
    ret
@endproc

@section rdata

; Version, diagnostic flag, & help strings
@data db szVerBannerNt1, "WINCMD32 Pure-ASM Shell [Windows NT ", 0
@data db szVerBannerNt2, " IA-32 PE32]", 0x0D, 0x0A, 0
@data db szVerBanner9x, "WINCMD32 Pure-ASM Shell [Windows 95/98/Me ANSI/DBCS IA-32 PE32]", 0x0D, 0x0A, 0
@data db szW32Help, "WINCMD32 [/A | /U] [/Q] [/D] [/E:ON|OFF] [/F:ON|OFF] [/V:ON|OFF] [/T:fg] [[/S] {/C | /K} string]", 0x0D, 0x0A, 0
@data db szFlagSim9x, "--win9x-ansi-sim", 0
@data db szFlagCapNt31, "--nt31-api-cap", 0
@data db szFlagVm8086, "--vm8086", 0
@data db szPipeSim9xArg, " --win9x-ansi-sim", 0
@data db szPipeSubArgs, " /S /D /C ", 0x22, 0

; Dynamic DLL & export names
@data db szDllKernel32, "KERNEL32.DLL", 0
@data db szDllNtdll, "NTDLL.DLL", 0
@data db szDllAdvapi32, "ADVAPI32.DLL", 0
@data db szDllShell32, "SHELL32.DLL", 0
@data db szDllMpr, "MPR.DLL", 0
@data db szFnGetEnvStrW, "GetEnvironmentStringsW", 0
@data db szFnFreeEnvStrW, "FreeEnvironmentStringsW", 0
@data db szFnGetEnvStrA, "GetEnvironmentStringsA", 0
@data db szFnGetEnvStrLegacy, "GetEnvironmentStrings", 0
@data db szFnFreeEnvStrA, "FreeEnvironmentStringsA", 0
@data db szFnGetShortPathW, "GetShortPathNameW", 0
@data db szFnGetShortPathA, "GetShortPathNameA", 0
@data db szFnSetHandleInfo, "SetHandleInformation", 0
@data db szFnMoveFileExW, "MoveFileExW", 0
@data db szFnGetDiskFreeExW, "GetDiskFreeSpaceExW", 0
@data db szFnGetDiskFreeExA, "GetDiskFreeSpaceExA", 0
@data db szFnFindFirstExW, "FindFirstFileExW", 0
@data db szFnCopyFileExW, "CopyFileExW", 0
@data db szFnGetLongPathW, "GetLongPathNameW", 0
@data db szFnCreateHardLinkW, "CreateHardLinkW", 0
@data db szFnWow64Disable, "Wow64DisableWow64FsRedirection", 0
@data db szFnWow64Revert, "Wow64RevertWow64FsRedirection", 0
@data db szFnCreateSymLinkW, "CreateSymbolicLinkW", 0
@data db szFnFindFirstStreamW, "FindFirstStreamW", 0
@data db szFnFindNextStreamW, "FindNextStreamW", 0
@data db szFnGetProcGroupCnt, "GetActiveProcessorGroupCount", 0
@data db szFnCreatePseudoCon, "CreatePseudoConsole", 0
@data db szFnGetMachineAttr, "GetMachineTypeAttributes", 0
@data db szFnRtlGetVersion, "RtlGetVersion", 0
@data db szFnNtQueryInfoFile, "NtQueryInformationFile", 0
@data db szFnRegOpenKeyExW, "RegOpenKeyExW", 0
@data db szFnRegOpenKeyExA, "RegOpenKeyExA", 0
@data db szFnRegQueryValExW, "RegQueryValueExW", 0
@data db szFnRegQueryValExA, "RegQueryValueExA", 0
@data db szFnRegCreateKeyExW, "RegCreateKeyExW", 0
@data db szFnRegCreateKeyExA, "RegCreateKeyExA", 0
@data db szFnRegSetValExW, "RegSetValueExW", 0
@data db szFnRegSetValExA, "RegSetValueExA", 0
@data db szFnRegDeleteValW, "RegDeleteValueW", 0
@data db szFnRegDeleteValA, "RegDeleteValueA", 0
@data db szFnRegCloseKey, "RegCloseKey", 0
@data db szFnWNetAdd2A, "WNetAddConnection2A", 0
@data db szFnWNetCancel2A, "WNetCancelConnection2A", 0
@data db szFnShellExecExW, "ShellExecuteExW", 0
@data db szFnShellExecExA, "ShellExecuteExA", 0

; Registry paths & defaults
@data db szRegSoftClasses, "Software", 0x5C, "Classes", 0x5C, 0
@data db szRegShellOpenCmd, 0x5C, "shell", 0x5C, "open", 0x5C, "command", 0
@data db szRegCmdProc, "Software", 0x5C, "Microsoft", 0x5C, "Command Processor", 0
@data db szRegValAutoRun, "AutoRun", 0
@data db szDefBatFileCls, "batfile", 0

; Reserved devices & extensions
@data db szDevNul, "NUL", 0
@data db szDevCon, "CON", 0
@data db szDevConIn, "CONIN$", 0
@data db szDevConOut, "CONOUT$", 0
@data db szDevPrn, "PRN", 0
@data db szDevAux, "AUX", 0
@data db szDevClock, "CLOCK$", 0
@data db szExtBat32, ".bat", 0
@data db szExtCmd32, ".cmd", 0
@data db szExtCom32, ".com", 0

; Environment & dynamic variable names
@data db szVarPathext, "PATHEXT", 0
@data db szDefPathext32, ".COM;.EXE;.BAT;.CMD", 0
@data db szVarPrompt, "PROMPT", 0
@data db szDefPrompt32, "$P$G", 0
@data db szVarPath, "PATH", 0
@data db szVarDpath, "DPATH", 0
@data db szVarComspec, "COMSPEC", 0
@data db szVarCompName, "COMPUTERNAME", 0
@data db szVarNoDefCwd, "NoDefaultCurrentDirectoryInExePath", 0
@data db szDynErrorlevel, "ERRORLEVEL", 0
@data db szDynCd, "CD", 0
@data db szDynRandom, "RANDOM", 0
@data db szDynCmdExtVer, "CMDEXTVERSION", 0
@data db szDynCmdCmdLine, "CMDCMDLINE", 0
@data db szDynExitCode, "=ExitCode", 0
@data db szDynExitAscii, "=ExitCodeAscii", 0
@data db szDynDate, "DATE", 0
@data db szDynTime, "TIME", 0
@data db szDynNuma, "HIGHESTNUMANODENUMBER", 0
@data db szValExtVer2, "2", 0
@data db szValZeroStr, "0", 0

; Parser & built-in command keywords
@data db szKwNot, "NOT", 0
@data db szKwErrorlevel, "ERRORLEVEL", 0
@data db szKwExist, "EXIST", 0
@data db szKwDefined, "DEFINED", 0
@data db szKwCmdExtVer, "CMDEXTVERSION", 0
@data db szKwElse, "ELSE", 0
@data db szKwEqu, "EQU", 0
@data db szKwNeq, "NEQ", 0
@data db szKwLss, "LSS", 0
@data db szKwLeq, "LEQ", 0
@data db szKwGtr, "GTR", 0
@data db szKwGeq, "GEQ", 0
@data db szKwIn, "IN", 0
@data db szKwDo, "DO", 0
@data db szKwEof, "EOF", 0
@data db szKwOn, "ON", 0
@data db szKwOff, "OFF", 0
@data db szDefDelims32, " ", 0x09, 0
@data db szOptEnDelay, "EnableDelayedExpansion", 0
@data db szOptDisDelay, "DisableDelayedExpansion", 0
@data db szOptEnExt, "EnableExtensions", 0
@data db szOptDisExt, "DisableExtensions", 0

; All 47 internal command names (+ aliases)
@data db szCmdAssoc, "ASSOC", 0
@data db szCmdBreak, "BREAK", 0
@data db szCmdCall, "CALL", 0
@data db szCmdCd, "CD", 0
@data db szCmdChdir, "CHDIR", 0
@data db szCmdChcp, "CHCP", 0
@data db szCmdCls, "CLS", 0
@data db szCmdCmd, "CMD", 0
@data db szCmdCommand, "COMMAND", 0
@data db szCmdColor, "COLOR", 0
@data db szCmdCopy, "COPY", 0
@data db szCmdCtty, "CTTY", 0
@data db szCmdDate, "DATE", 0
@data db szCmdDel, "DEL", 0
@data db szCmdErase, "ERASE", 0
@data db szCmdDir, "DIR", 0
@data db szCmdDoskey, "DOSKEY", 0
@data db szCmdDpath, "DPATH", 0
@data db szCmdEcho, "ECHO", 0
@data db szCmdEndlocal, "ENDLOCAL", 0
@data db szCmdExit, "EXIT", 0
@data db szCmdFtype, "FTYPE", 0
@data db szCmdGoto, "GOTO", 0
@data db szCmdKeys, "KEYS", 0
@data db szCmdLh, "LH", 0
@data db szCmdLoadhigh, "LOADHIGH", 0
@data db szCmdMd, "MD", 0
@data db szCmdMkdir, "MKDIR", 0
@data db szCmdMklink, "MKLINK", 0
@data db szCmdMode, "MODE", 0
@data db szCmdMove, "MOVE", 0
@data db szCmdPath, "PATH", 0
@data db szCmdPause, "PAUSE", 0
@data db szCmdPopd, "POPD", 0
@data db szCmdPrompt, "PROMPT", 0
@data db szCmdPushd, "PUSHD", 0
@data db szCmdRd, "RD", 0
@data db szCmdRmdir, "RMDIR", 0
@data db szCmdRem, "REM", 0
@data db szCmdRen, "REN", 0
@data db szCmdRename, "RENAME", 0
@data db szCmdSet, "SET", 0
@data db szCmdSetlocal, "SETLOCAL", 0
@data db szCmdShift, "SHIFT", 0
@data db szCmdStart, "START", 0
@data db szCmdTime, "TIME", 0
@data db szCmdTitle, "TITLE", 0
@data db szCmdTruename, "TRUENAME", 0
@data db szCmdType, "TYPE", 0
@data db szCmdVer, "VER", 0
@data db szCmdVerify, "VERIFY", 0
@data db szCmdVol, "VOL", 0

; UI, DIR, MKLINK, & Error messages
@data db szEmpty32, 0
@data db szCrLf32, 0x0D, 0x0A, 0
@data db szDot32, ".", 0
@data db szStar32, "*", 0
@data db szDash32, "-", 0
@data db szEqSign32, "=", 0
@data db szOneSpace32, " ", 0
@data db szBackslashOnly, 0x5C, 0
@data db szBackslashCrLf, 0x5C, 0x0D, 0x0A, 0
@data db szMorePrompt32, "More? ", 0
@data db szEchoIsOn, "ECHO is on.", 0x0D, 0x0A, 0
@data db szEchoIsOff, "ECHO is off.", 0x0D, 0x0A, 0
@data db szEnvNotDef1, "Environment variable ", 0
@data db szEnvNotDef2, " not defined", 0x0D, 0x0A, 0
@data db szActiveCp32, "Active code page: ", 0
@data db szNullPath32, "(null)", 0
@data db szPausePrompt32, "Press any key to continue . . . ", 0
@data db szMsgBreak, "BREAK is ", 0
@data db szMsgVerify, "VERIFY is ", 0
@data db szMsgKeys, "KEYS is ", 0
@data db szStateOnCrLf, "on.", 0x0D, 0x0A, 0
@data db szStateOffCrLf, "off.", 0x0D, 0x0A, 0
@data db szAssocMissing, "File association not found for extension ", 0
@data db szFtypeMissing, "File type not found or no open command associated with it.", 0x0D, 0x0A, 0
@data db szMklinkSym, "symbolic link created for ", 0
@data db szMklinkSymD, "symbolic link created for ", 0
@data db szMklinkHard, "hardlink created for ", 0
@data db szMklinkJunc, "junction created for ", 0
@data db szMklinkArrow, " <<===>> ", 0
@data db szOneFileCopied, "        1 file(s) copied.", 0x0D, 0x0A, 0
@data db szVolInDrive, " Volume in drive ", 0
@data db szVolIs, " is ", 0
@data db szVolHasNoLbl, " has no label.", 0x0D, 0x0A, 0
@data db szVolSerialIs, " Volume Serial Number is ", 0
@data db szCurDateIs, "The current date is: ", 0
@data db szCurTimeIs, "The current time is: ", 0
@data db szDirOfHdr, " Directory of ", 0
@data db szDirTagDir, "    <DIR>          ", 0
@data db szDirTagJunction, "    <JUNCTION>     ", 0
@data db szDirTagSymlink, "    <SYMLINK>      ", 0
@data db szDirTagSymlinkD, "    <SYMLINKD>     ", 0
@data db szDirStreamIndent, "                  ", 0
@data db szDirSumPad, "               ", 0
@data db szDirFilesWord, " File(s)   ", 0
@data db szDirBytesWord, " bytes", 0x0D, 0x0A, 0
@data db szDirDirsWord, " Dir(s)   ", 0
@data db szDirBytesFree, " bytes free", 0x0D, 0x0A, 0

@data db szErrFnf32, "The system cannot find the file specified.", 0
@data db szErrPnf32, "The system cannot find the path specified.", 0
@data db szErrAcc32, "Access is denied.", 0
@data db szErrExists32, "A subdirectory or file already exists.", 0
@data db szErrOom32, "Not enough storage is available to process this command.", 0
@data db szErrSyntax32, "The syntax of the command is incorrect.", 0
@data db szErrDivZero32, "Divide by zero error.", 0
@data db szErrInvNum32, "Invalid number. Numeric constants are either decimal (17), hexadecimal (0x11), or octal (021).", 0
@data db szErrBadCmd32, "is not recognized as an internal or external command, operable program or batch file.", 0
@data db szErrBadLbl32, "The system cannot find the batch label specified.", 0
@data db szErrGen32, "The command failed to complete.", 0

@section bss

; Arena & persistent memory pointers
@res dword g_arena_base, 1
@res dword g_arena_ptr, 1
@res dword g_arena_committed, 1
@res dword g_arena_limit, 1

; OS version, simulation, & codepage state
@res dword g_os_major, 1
@res dword g_os_minor, 1
@res dword g_os_build, 1
@res dword g_active_cp, 1
@res dword g_ctrl_break, 1
@res dword g_last_exit_code, 1
@res dword g_rng_state, 1
@res dword g_raw_cmdline, 1
@res dword g_sa_ptr, 1
@res byte g_dbcs_bitmap, 32
@res byte g_self_exe, 1024

@res byte g_is_win9x, 1
@res byte g_sim_win9x, 1
@res byte g_sim_nt31, 1
@res byte g_force_vm86, 1
@res byte g_echo_on, 1
@res byte g_extensions_on, 1
@res byte g_delayed_exp, 1
@res byte g_file_comp_on, 1
@res byte g_unicode_out, 1
@res byte g_vt_enabled, 1
@res byte g_disable_autorun, 1
@res byte g_exit_flag, 1
@res byte g_batch_ret_flag, 1
@res byte g_goto_pending, 1
@res byte g_dir_need_short, 1
@res byte g_advapi_loaded, 1
@res byte g_shell32_loaded, 1
@res byte g_mpr_loaded, 1
@res byte g_break_on, 1
@res byte g_verify_on, 1
@res byte g_keys_on, 1
@res word g_default_attr, 1
@res word g_pad_bss0, 1
@res byte g_goto_label, 260

; Environment, SETLOCAL (32 * 24B = 768B), Batch (32 * 64B = 2048B), FOR, PUSHD, & DOSKEY stacks
@res dword g_env_buf, 1
@res dword g_env_used, 1
@res dword g_setlocal_depth, 1
@res byte g_setlocal_stack, 768
@res dword g_batch_depth, 1
@res byte g_batch_stack, 2048
@res dword g_for_depth, 1
@res byte g_for_vars, 16
@res dword g_for_vals, 16
@res dword g_pushd_depth, 1
@res dword g_pushd_dirs, 32
@res byte g_pushd_mapped, 32
@res dword g_hist_count, 1
@res dword g_hist_ptrs, 64

; Dynamic HMODULEs & NT_DYN_API / ADVAPI32 / MPR function pointers (_p<ArgBytes> suffix verified by asm-lint!)
@res dword g_hmod_kernel32, 1
@res dword g_hmod_advapi32, 1
@res dword pfnGetEnvStringsW_p0, 1
@res dword pfnFreeEnvStringsW_p4, 1
@res dword pfnGetEnvStringsA_p0, 1
@res dword pfnFreeEnvStringsA_p4, 1
@res dword pfnGetShortPathNameW_p12, 1
@res dword pfnGetShortPathNameA_p12, 1
@res dword pfnSetHandleInfo_p12, 1
@res dword pfnMoveFileExW_p12, 1
@res dword pfnGetDiskFreeSpaceExW_p16, 1
@res dword pfnGetDiskFreeSpaceExA_p16, 1
@res dword pfnFindFirstFileExW_p24, 1
@res dword pfnCopyFileExW_p24, 1
@res dword pfnGetLongPathNameW_p12, 1
@res dword pfnCreateHardLinkW_p12, 1
@res dword pfnWow64DisableRedir_p4, 1
@res dword pfnWow64RevertRedir_p4, 1
@res dword pfnCreateSymbolicLinkW_p12, 1
@res dword pfnFindFirstStreamW_p16, 1
@res dword pfnFindNextStreamW_p8, 1
@res dword pfnGetActiveProcGroupCnt_p0, 1
@res dword pfnCreatePseudoConsole_p20, 1
@res dword pfnGetMachineTypeAttr_p8, 1
@res dword pfnRtlGetVersion_p4, 1
@res dword pfnNtQueryInfoFile_p20, 1
@res dword pfnRegOpenKeyExW_p20, 1
@res dword pfnRegOpenKeyExA_p20, 1
@res dword pfnRegQueryValueExW_p24, 1
@res dword pfnRegQueryValueExA_p24, 1
@res dword pfnRegCreateKeyExW_p36, 1
@res dword pfnRegCreateKeyExA_p36, 1
@res dword pfnRegSetValueExW_p24, 1
@res dword pfnRegSetValueExA_p24, 1
@res dword pfnRegDeleteValueW_p8, 1
@res dword pfnRegDeleteValueA_p8, 1
@res dword pfnRegCloseKey_p4, 1
@res dword pfnWNetAddConn2A_p16, 1
@res dword pfnWNetCancelConn2A_p12, 1
@res dword pfnShellExecuteExW_p4, 1
@res dword pfnShellExecuteExA_p4, 1
