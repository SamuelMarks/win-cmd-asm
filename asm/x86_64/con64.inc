;; @file con64.inc
;; @isa x64
;; @desc Win64 Console Ctrl+C XCHG handler, WriteConsoleW / /U UTF-16LE pipe I/O, Win10/11 VT + ConPTY, and DOSKEY history.

@cpu x64
@format inc

;; @proc w64_ctrl_handler
;; @abi win64_ms
;; @in ECX = dwCtrlType (CTRL_C_EVENT=0, CTRL_BREAK_EVENT=1, CTRL_CLOSE_EVENT=2)
;; @out EAX = TRUE (1) for Ctrl+C / Ctrl+Break, FALSE (0) otherwise
;; @err none
;; @clobbers rax, rdx
;; @desc Thread-safe Microsoft x64 PHANDLER_ROUTINE setting g_ctrl_break atomically via implicit-LOCK XCHG.
@proc w64_ctrl_handler
    endbr64
    cmp ecx, 1
    ja .not_handled
    mov edx, 1
    xchg dword [rel g_ctrl_break], edx
    mov eax, 1
    ret
.not_handled:
    xor eax, eax
    ret
@endproc

;; @proc con_init64
;; @abi win64_ms
;; @in none
;; @out Registers SetConsoleCtrlHandler, caches default attribute, and enables Win10/11 VT + ConPTY flag
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Initializes Win64 Console (XP Pro x64..11) and enables ENABLE_VIRTUAL_TERMINAL_PROCESSING on Win10/11.
@proc_frame con_init64, 64, rbx
    lea rcx, [rel w64_ctrl_handler]
    mov edx, 1
    call qword [rel __imp_SetConsoleCtrlHandler]

    mov word [rel g_default_attr], 0x0007
    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    cmp rax, INVALID_HANDLE_VALUE
    je .done
    test rax, rax
    jz .done
    mov rbx, rax

    mov rcx, rbx
    lea rdx, [rsp + 32]
    call qword [rel __imp_GetConsoleScreenBufferInfo]
    test eax, eax
    jz .done
    mov ax, word [rsp + 32 + 8]
    mov word [rel g_default_attr], ax

    cmp dword [rel g_os_major], 10
    jb .done
    mov rcx, rbx
    lea rdx, [rsp + 32]
    call qword [rel __imp_GetConsoleMode]
    test eax, eax
    jz .done
    mov edx, dword [rsp + 32]
    or edx, ENABLE_VIRTUAL_TERMINAL_PROCESSING
    mov rcx, rbx
    call qword [rel __imp_SetConsoleMode]
    test eax, eax
    jz .done
    mov byte [rel g_vt_enabled], 1
    cmp qword [rel pfnCreatePseudoConsole64], 0
    je .done
    mov byte [rel g_conpty_avail], 1

.done:
    @epilog_ret
@endproc

;; @proc con_is_handle_tty64
;; @abi win64_ms
;; @in RCX = Win64 file/console HANDLE
;; @out EAX = 1 if RCX is a real Win64 console buffer, 0 if redirected pipe/file/NUL
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Distinguishes interactive Win64 consoles (GetFileType==CHAR && GetConsoleMode!=0) from pipes/files/NUL.
@proc_frame con_is_handle_tty64, 48, rbx
    cmp rcx, INVALID_HANDLE_VALUE
    je .no
    test rcx, rcx
    jz .no
    mov rbx, rcx
    call qword [rel __imp_GetFileType]
    cmp eax, FILE_TYPE_CHAR
    jne .no
    mov rcx, rbx
    lea rdx, [rsp + 32]
    call qword [rel __imp_GetConsoleMode]
    test eax, eax
    jz .no
    mov eax, 1
    @epilog_ret
.no:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc con_write_handle64
;; @abi win64_ms
;; @in RCX = target Win64 HANDLE, RDX = WTF-8/ASCII buffer, R8 = byte count
;; @out Writes text via WriteConsoleW (TTY) or WriteFile (pipes/files, /U UTF-16LE aware)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Universal CRT-free Win64 console/pipe/file writer supporting Unicode TTY, UTF-8, and /U UTF-16LE pipes.
@proc_frame con_write_handle64, 56, rbp, rbx, rsi, rdi, r12, r13
    test r8, r8
    jz .done
    mov rbx, rcx
    mov rsi, rdx
    mov rbp, r8

    call con_is_handle_tty64
    test eax, eax
    jz .redir_file

    call arena_mark64
    mov r12, rax
    lea rcx, [rbp + 4]
    call arena_alloc64
    jc .redir_raw
    mov rdi, rax
    mov r13, rax
    mov rcx, rbp
    rep movsb
    mov byte [rdi], 0
    mov rcx, r13
    call wtf8_to_utf16le64
    jc .rel_tty
    mov dword [rsp + 40], 0
    mov rcx, rbx
    mov r8d, edx
    mov rdx, rax
    lea r9, [rsp + 40]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteConsoleW]
.rel_tty:
    mov rcx, r12
    call arena_release64
    @epilog_ret

.redir_file:
    cmp byte [rel g_unicode_out], 0
    je .redir_raw
    call arena_mark64
    mov r12, rax
    lea rcx, [rbp + 4]
    call arena_alloc64
    jc .redir_raw
    mov rdi, rax
    mov r13, rax
    mov rcx, rbp
    rep movsb
    mov byte [rdi], 0
    mov rcx, r13
    call wtf8_to_utf16le64
    jc .rel_u
    shl edx, 1
    mov dword [rsp + 40], 0
    mov rcx, rbx
    mov r8d, edx
    mov rdx, rax
    lea r9, [rsp + 40]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]
.rel_u:
    mov rcx, r12
    call arena_release64
    @epilog_ret

.redir_raw:
    mov dword [rsp + 40], 0
    mov rcx, rbx
    mov rdx, rsi
    mov r8d, ebp
    lea r9, [rsp + 40]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]

.done:
    @epilog_ret
@endproc

;; @proc con_write_out64
;; @abi win64_ms
;; @in RCX = WTF-8/ASCII buffer, RDX = byte count
;; @out Writes to current STD_OUTPUT_HANDLE
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Writes RDX bytes from RCX to STD_OUTPUT_HANDLE.
@proc_frame con_write_out64, 40, rsi, rbx
    mov rsi, rcx
    mov rbx, rdx
    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rcx, rax
    mov rdx, rsi
    mov r8, rbx
    call con_write_handle64
    @epilog_ret
@endproc

;; @proc con_print_z64
;; @abi win64_ms
;; @in RCX = NUL-terminated WTF-8/ASCII string
;; @out Writes string to STD_OUTPUT_HANDLE
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints a NUL-terminated string to STD_OUTPUT_HANDLE.
@proc_frame con_print_z64, 32, rsi
    mov rsi, rcx
    call str_len64
    mov rcx, rsi
    mov rdx, rax
    call con_write_out64
    @epilog_ret
@endproc

;; @proc con_print_crlf64
;; @abi win64_ms
;; @in none
;; @out Writes CRLF to STD_OUTPUT_HANDLE
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Emits a CRLF newline to STD_OUTPUT_HANDLE.
@proc_frame con_print_crlf64, 40
    lea rcx, [rel szCrLf64]
    mov edx, 2
    call con_write_out64
    @epilog_ret
@endproc

;; @proc con_print_err_z64
;; @abi win64_ms
;; @in RCX = NUL-terminated WTF-8/ASCII string
;; @out Writes string to STD_ERROR_HANDLE
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints a NUL-terminated diagnostic string to STD_ERROR_HANDLE.
@proc_frame con_print_err_z64, 40, rsi, rbx
    mov rsi, rcx
    call str_len64
    mov rbx, rax
    mov ecx, STD_ERROR_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rcx, rax
    mov rdx, rsi
    mov r8, rbx
    call con_write_handle64
    @epilog_ret
@endproc

;; @proc con_report_err64
;; @abi win64_ms
;; @in ECX = canonical ERR_* code
;; @out Prints error message + CRLF to STD_ERROR_HANDLE
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints canonical error description + CRLF to stderr.
@proc_frame con_report_err64, 40
    call err_fallback_str64
    mov rcx, rax
    call con_print_err_z64
    mov ecx, STD_ERROR_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rcx, rax
    lea rdx, [rel szCrLf64]
    mov r8d, 2
    call con_write_handle64
    @epilog_ret
@endproc

;; @proc doskey_push_hist64
;; @abi win64_ms
;; @in RCX = NUL-terminated line
;; @out Appends copy into g_hist_ptrs[0..W64_HIST_MAX-1]
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Stores a command line in the persistent 64-bit DOSKEY history ring.
@proc_frame doskey_push_hist64, 40, rbx, rsi, rdi, r12
    cmp byte [rcx], 0
    je .done
    mov rsi, rcx
    call str_len64
    lea rbx, [rax + 1]
    mov rcx, rbx
    call perm_alloc64
    jc .done
    mov rdi, rax
    mov r12, rax
    mov rcx, rbx
    rep movsb

    lea rbx, [rel g_hist_ptrs]
    mov ecx, dword [rel g_hist_count]
    cmp ecx, W64_HIST_MAX
    jb .append

    mov rcx, qword [rbx]
    call perm_free64
    xor ecx, ecx
.sh_lp:
    mov rdx, qword [rbx + rcx * 8 + 8]
    mov qword [rbx + rcx * 8], rdx
    inc ecx
    cmp ecx, W64_HIST_MAX - 1
    jb .sh_lp
    mov qword [rbx + (W64_HIST_MAX - 1) * 8], r12
    @epilog_ret

.append:
    mov qword [rbx + rcx * 8], r12
    inc ecx
    mov dword [rel g_hist_count], ecx
.done:
    @epilog_ret
@endproc

;; @proc con_read_line64
;; @abi win64_ms
;; @in RCX = destination WTF-8 buffer, RDX = max byte capacity
;; @out RAX = byte length, CF=0; or CF=1 on EOF
;; @err CF=1 when stdin reaches EOF with 0 bytes read
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads an interactive console line (ReadConsoleW + history) or piped stdin line (ReadFile).
@proc_frame con_read_line64, 56, rbp, rbx, rsi, rdi, r12, r13
    mov rdi, rcx
    mov rbp, rdx

    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rbx, rax
    mov rcx, rbx
    call con_is_handle_tty64
    test eax, eax
    jz .read_piped

    call arena_mark64
    mov r12, rax
    mov ecx, 8196
    call arena_alloc64
    jc .eof_rel
    mov rsi, rax
    mov dword [rsp + 40], 0
    mov rcx, rbx
    mov rdx, rsi
    mov r8d, 4094
    lea r9, [rsp + 40]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadConsoleW]
    test eax, eax
    jz .eof_rel
    mov edx, dword [rsp + 40]
    test edx, edx
    jz .eof_rel

    mov rcx, rsi
    mov r8, rdi
    mov r9, rbp
    call utf16le_to_wtf8_buf64
    mov r13, rax
    mov rcx, r12
    call arena_release64
    jmp .strip_eol

.read_piped:
    xor r13d, r13d
    dec rbp
.pipe_lp:
    cmp r13, rbp
    jae .pipe_done
    mov dword [rsp + 44], 0
    mov byte [rsp + 40], 0
    mov rcx, rbx
    lea rdx, [rsp + 40]
    mov r8d, 1
    lea r9, [rsp + 44]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    test eax, eax
    jz .pipe_eof_chk
    cmp dword [rsp + 44], 0
    je .pipe_eof_chk
    movzx ecx, byte [rsp + 40]
    cmp cl, 0x1A
    je .pipe_eof_chk
    cmp cl, 0x0A
    je .pipe_done
    cmp cl, 0x0D
    je .pipe_lp
    mov byte [rdi + r13], cl
    inc r13
    jmp .pipe_lp

.pipe_eof_chk:
    test r13, r13
    jz .eof
.pipe_done:
    mov byte [rdi + r13], 0

.strip_eol:
    test r13, r13
    jz .check_hist
.trim_crlf:
    test r13, r13
    jz .check_hist
    mov al, byte [rdi + r13 - 1]
    cmp al, 0x0A
    je .chop
    cmp al, 0x0D
    jne .check_hist
.chop:
    dec r13
    mov byte [rdi + r13], 0
    jmp .trim_crlf

.check_hist:
    test r13, r13
    jz .ret_ok
    mov rcx, rdi
    call doskey_push_hist64

.ret_ok:
    mov rax, r13
    clc
    @epilog_ret

.eof_rel:
    mov rcx, r12
    call arena_release64
.eof:
    mov byte [rdi], 0
    xor eax, eax
    stc
    @epilog_ret
@endproc
