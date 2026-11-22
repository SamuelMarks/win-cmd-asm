;; @file con32.inc
;; @isa 386
;; @desc Win32 Console Ctrl+C LOCK XCHG handler, Classic *W/*A + Win10/11 VT I/O, COLOR/CLS/TITLE/CHCP, and DOSKEY.

@cpu 386
@format inc

;; @proc w32_ctrl_handler
;; @abi win32
;; @in [ESP+4] = dwCtrlType (CTRL_C_EVENT=0, CTRL_BREAK_EVENT=1, CTRL_CLOSE_EVENT=2)
;; @out EAX = TRUE (1) for Ctrl+C / Ctrl+Break, FALSE (0) otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Thread-safe __stdcall PHANDLER_ROUTINE setting g_ctrl_break atomically via LOCK XCHG.
@proc w32_ctrl_handler
    mov eax, dword [esp + 4]
    cmp eax, 1
    ja .not_handled
    mov edx, 1
    xchg dword [g_ctrl_break], edx
    mov eax, 1
    ret 4
.not_handled:
    xor eax, eax
    ret 4
@endproc

;; @proc con_init32
;; @abi win32
;; @in none
;; @out Registers SetConsoleCtrlHandler, caches initial attribute, and probes Win10/11 VT console mode
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Initializes Classic Win32 Console (NT 3.1..11 & Win95..Me) and dynamically enables VT mode on Win10/11.
@proc con_init32
    push ebx
    sub esp, 28
    mov ebx, esp

    push 1
    push addr(w32_ctrl_handler)
    call dword [__imp__SetConsoleCtrlHandler@8]

    ; Capture initial console attribute for COLOR reset
    mov word [g_default_attr], 0x0007
    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov ecx, eax
    cmp ecx, INVALID_HANDLE_VALUE
    je .done
    test ecx, ecx
    jz .done

    push ecx
    push ebx
    push ecx
    call dword [__imp__GetConsoleScreenBufferInfo@8]
    pop ecx
    test eax, eax
    jz .done
    mov ax, word [ebx + 8]
    mov word [g_default_attr], ax

    ; Probe Windows 10 (1511+) / Windows 11 ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x0004)
    cmp dword [g_os_major], 10
    jb .done
    push ecx
    push ebx
    push ecx
    call dword [__imp__GetConsoleMode@8]
    pop ecx
    test eax, eax
    jz .done
    mov eax, dword [ebx]
    or eax, ENABLE_VIRTUAL_TERMINAL_PROCESSING
    push eax
    push ecx
    call dword [__imp__SetConsoleMode@8]
    test eax, eax
    jz .done
    mov byte [g_vt_enabled], 1

.done:
    add esp, 28
    pop ebx
    ret
@endproc

;; @proc con_is_handle_tty32
;; @abi win32
;; @in EBX = Win32 file/console HANDLE
;; @out EAX = 1 if EBX is a real Win32 console buffer, 0 if redirected pipe/file/NUL
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Distinguishes real interactive consoles (GetFileType==CHAR && GetConsoleMode!=0) from pipes/files/NUL.
@proc con_is_handle_tty32
    cmp ebx, INVALID_HANDLE_VALUE
    je .no
    test ebx, ebx
    jz .no
    push ebx
    call dword [__imp__GetFileType@4]
    cmp eax, FILE_TYPE_CHAR
    jne .no
    sub esp, 4
    mov edx, esp
    push edx
    push ebx
    call dword [__imp__GetConsoleMode@8]
    add esp, 4
    test eax, eax
    jz .no
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret
@endproc

;; @proc con_write_handle32
;; @abi win32
;; @in EBX = target Win32 HANDLE, ESI = WTF-8/ASCII buffer, ECX = byte count
;; @out Writes text via WriteConsoleW (NT TTY), WriteConsoleA (Win9x TTY), or WriteFile (pipes/files, /U UTF-16LE aware!)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Universal CRT-free console/pipe/file writer supporting NT Unicode TTY, Win9x ANSI TTY, CP65001, and /U pipes.
@proc con_write_handle32
    push ebx
    push esi
    push edi
    push ebp
    test ecx, ecx
    jz .done
    mov ebp, ecx

    call con_is_handle_tty32
    test eax, eax
    jz .redir_file

    cmp byte [g_is_win9x], 0
    jne .tty_9x

    ; Copy slice into NUL-terminated arena buffer and call WriteConsoleW
    call arena_mark32
    mov edi, eax
    lea ecx, [ebp + 4]
    call arena_alloc32
    jc .redir_raw
    push edi
    mov edi, eax
    mov edx, eax
    mov ecx, ebp
    rep movsb
    mov byte [edi], 0
    mov esi, edx
    call wtf8_to_utf16le
    pop edi
    jc .rel_only
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push ecx
    push eax
    push ebx
    call dword [__imp__WriteConsoleW@20]
    add esp, 4
.rel_only:
    mov eax, edi
    call arena_release32
    jmp .done

.tty_9x:
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push ebp
    push esi
    push ebx
    call dword [__imp__WriteConsoleA@20]
    add esp, 4
    jmp .done

.redir_file:
    cmp byte [g_unicode_out], 0
    je .redir_raw
    cmp byte [g_is_win9x], 0
    jne .redir_raw
    ; /U switch active on redirected pipe/file: emit UTF-16LE!
    call arena_mark32
    mov edi, eax
    lea ecx, [ebp + 4]
    call arena_alloc32
    jc .redir_raw
    push edi
    mov edi, eax
    mov edx, eax
    mov ecx, ebp
    rep movsb
    mov byte [edi], 0
    mov esi, edx
    call wtf8_to_utf16le
    pop edi
    jc .rel_u
    shl ecx, 1
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push ecx
    push eax
    push ebx
    call dword [__imp__WriteFile@20]
    add esp, 4
.rel_u:
    mov eax, edi
    call arena_release32
    jmp .done

.redir_raw:
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push ebp
    push esi
    push ebx
    call dword [__imp__WriteFile@20]
    add esp, 4

.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc con_write_out32
;; @abi win32
;; @in ESI = WTF-8/ASCII buffer, ECX = byte count
;; @out Writes to current STD_OUTPUT_HANDLE
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes ECX bytes from ESI to STD_OUTPUT_HANDLE.
@proc con_write_out32
    push ebx
    push ecx
    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov ebx, eax
    pop ecx
    call con_write_handle32
    pop ebx
    ret
@endproc

;; @proc con_print_z32
;; @abi win32
;; @in ESI = NUL-terminated WTF-8/ASCII string
;; @out Writes string to STD_OUTPUT_HANDLE
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints a NUL-terminated string to STD_OUTPUT_HANDLE.
@proc con_print_z32
    call str_len32
    call con_write_out32
    ret
@endproc

;; @proc con_print_crlf32
;; @abi win32
;; @in none
;; @out Writes CRLF to STD_OUTPUT_HANDLE
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Emits a CRLF newline to STD_OUTPUT_HANDLE.
@proc con_print_crlf32
    push esi
    mov esi, addr(szCrLf32)
    mov ecx, 2
    call con_write_out32
    pop esi
    ret
@endproc

;; @proc con_print_err_z32
;; @abi win32
;; @in ESI = NUL-terminated WTF-8/ASCII string
;; @out Writes string to STD_ERROR_HANDLE
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints a NUL-terminated diagnostic string to STD_ERROR_HANDLE.
@proc con_print_err_z32
    push ebx
    call str_len32
    push ecx
    push STD_ERROR_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov ebx, eax
    pop ecx
    call con_write_handle32
    pop ebx
    ret
@endproc

;; @proc con_report_err32
;; @abi win32
;; @in EAX = canonical ERR_* code
;; @out Prints localized/fallback error message + CRLF to STD_ERROR_HANDLE
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints era-accurate error description to stderr.
@proc con_report_err32
    push esi
    call err_fallback_str32
    mov esi, eax
    call con_print_err_z32
    mov esi, addr(szCrLf32)
    push ebx
    push STD_ERROR_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov ebx, eax
    mov ecx, 2
    call con_write_handle32
    pop ebx
    pop esi
    ret
@endproc

;; @proc con_read_line32
;; @abi win32
;; @in EDI = destination WTF-8 buffer, ECX = max byte capacity
;; @out EAX = byte length, CF=0; or CF=1 on EOF
;; @err CF=1 when stdin reaches EOF with 0 bytes read
;; @clobbers eax, ecx, edx
;; @desc Reads an interactive console line (ReadConsoleW/A + DOSKEY history) or piped stdin line (ReadFile).
@proc con_read_line32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx

    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov ebx, eax
    call con_is_handle_tty32
    test eax, eax
    jz .read_piped

    cmp byte [g_is_win9x], 0
    jne .tty_9x

    ; Interactive NT Console: ReadConsoleW
    call arena_mark32
    push eax
    mov ecx, 8196
    call arena_alloc32
    jc .eof_pop
    mov esi, eax
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push 4094
    push esi
    push ebx
    call dword [__imp__ReadConsoleW@20]
    mov ecx, dword [esp]
    add esp, 4
    test eax, eax
    jz .eof_pop
    test ecx, ecx
    jz .eof_pop

    mov edx, ebp
    call utf16le_to_wtf8_buf
    mov ebx, eax
    pop eax
    call arena_release32
    jmp .strip_eol

.tty_9x:
    sub esp, 4
    mov edx, esp
    dec ebp
    push 0
    push edx
    push ebp
    push edi
    push ebx
    call dword [__imp__ReadConsoleA@20]
    mov ecx, dword [esp]
    add esp, 4
    test eax, eax
    jz .eof
    test ecx, ecx
    jz .eof
    mov byte [edi + ecx], 0
    mov ebx, ecx
    jmp .strip_eol

.read_piped:
    xor esi, esi
    dec ebp
.pipe_lp:
    cmp esi, ebp
    jae .pipe_done
    sub esp, 8
    mov ecx, esp
    lea edx, [esp + 4]
    mov dword [edx], 0
    push 0
    push edx
    push 1
    push ecx
    push ebx
    call dword [__imp__ReadFile@20]
    mov edx, dword [esp + 4]
    movzx ecx, byte [esp]
    add esp, 8
    test eax, eax
    jz .pipe_eof_chk
    test edx, edx
    jz .pipe_eof_chk
    cmp cl, 0x1A
    je .pipe_eof_chk
    cmp cl, 0x0A
    je .pipe_done
    cmp cl, 0x0D
    je .pipe_lp
    mov byte [edi + esi], cl
    inc esi
    jmp .pipe_lp

.pipe_eof_chk:
    test esi, esi
    jz .eof
.pipe_done:
    mov byte [edi + esi], 0
    mov ebx, esi

.strip_eol:
    test ebx, ebx
    jz .check_hist
.trim_crlf:
    test ebx, ebx
    jz .check_hist
    mov al, byte [edi + ebx - 1]
    cmp al, 0x0A
    je .chop
    cmp al, 0x0D
    jne .check_hist
.chop:
    dec ebx
    mov byte [edi + ebx], 0
    jmp .trim_crlf

.check_hist:
    ; Record non-empty interactive lines in DOSKEY history ring
    test ebx, ebx
    jz .ret_ok
    mov esi, edi
    call doskey_push_hist32

.ret_ok:
    mov eax, ebx
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.eof_pop:
    pop eax
    call arena_release32
.eof:
    mov byte [edi], 0
    xor eax, eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc doskey_push_hist32
;; @abi win32
;; @in ESI = NUL-terminated line
;; @out Appends copy into g_hist_ptrs[0..W32_HIST_MAX-1]
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Stores a command line in the persistent DOSKEY history ring.
@proc doskey_push_hist32
    push ebx
    push esi
    push edi

    cmp byte [esi], 0
    je .done

    call str_len32
    inc ecx
    mov ebx, ecx
    call perm_alloc32
    jc .done
    mov edi, eax
    mov ecx, ebx
    rep movsb

    mov ecx, dword [g_hist_count]
    cmp ecx, W32_HIST_MAX
    jb .append

    ; Free oldest slot 0 and shift down
    push eax
    mov eax, dword [g_hist_ptrs]
    call perm_free32
    xor ecx, ecx
.sh_lp:
    mov edx, dword [g_hist_ptrs + ecx * 4 + 4]
    mov dword [g_hist_ptrs + ecx * 4], edx
    inc ecx
    cmp ecx, W32_HIST_MAX - 1
    jb .sh_lp
    pop eax
    mov dword [g_hist_ptrs + (W32_HIST_MAX - 1) * 4], eax
    jmp .done

.append:
    mov dword [g_hist_ptrs + ecx * 4], eax
    inc ecx
    mov dword [g_hist_count], ecx

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc
