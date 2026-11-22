;; @file env_sa32.inc
;; @isa 386
;; @desc Win32 WTF-8 Environment Manager, 32-level SETLOCAL stack, Dynamic Variables, and 11-tier SET /A evaluator.

@cpu 386
@format inc

;; @proc env_init32
;; @abi win32
;; @in none
;; @out Allocates g_env_buf (64 KiB) and imports initial process environment (*W on NT, *A on Win9x)
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Imports the parent Win32 environment block into our WTF-8 KEY=VALUE\0...\0\0 store.
@proc env_init32
    push ebx
    push esi
    push edi

    mov ecx, W32_ENV_CAP_BYTES
    call perm_alloc32
    jc .fail
    mov dword [g_env_buf], eax
    mov word [eax], 0
    mov dword [g_env_used], 2

    cmp byte [g_is_win9x], 0
    jne .imp_ansi

    mov eax, dword [pfnGetEnvStringsW_p0]
    test eax, eax
    jz .imp_ansi
    call eax
    test eax, eax
    jz .seed_defaults
    mov ebx, eax
    mov esi, eax
    mov edi, dword [g_env_buf]
    mov edx, W32_ENV_CAP_BYTES - 4

.w_entry_lp:
    cmp word [esi], 0
    je .w_done
    call wcs_len32
    push ecx
    push edx
    call utf16le_to_wtf8_buf
    pop edx
    pop ecx
    lea esi, [esi + ecx * 2 + 2]
    inc eax
    add edi, eax
    sub edx, eax
    cmp edx, 16
    ja .w_entry_lp

.w_done:
    mov byte [edi], 0
    inc edi
    sub edi, dword [g_env_buf]
    mov dword [g_env_used], edi

    cmp dword [pfnFreeEnvStringsW_p4], 0
    je .seed_defaults
    push ebx
    call dword [pfnFreeEnvStringsW_p4]
    jmp .seed_defaults

.imp_ansi:
    mov eax, dword [pfnGetEnvStringsA_p0]
    test eax, eax
    jz .seed_defaults
    call eax
    test eax, eax
    jz .seed_defaults
    mov ebx, eax
    mov esi, eax
    mov edi, dword [g_env_buf]
    xor ecx, ecx
.a_lp:
    cmp ecx, W32_ENV_CAP_BYTES - 4
    jae .a_done
    mov al, byte [esi + ecx]
    mov byte [edi + ecx], al
    inc ecx
    test al, al
    jnz .a_lp
    mov al, byte [esi + ecx]
    mov byte [edi + ecx], al
    inc ecx
    test al, al
    jnz .a_lp
.a_done:
    mov byte [edi + ecx], 0
    mov dword [g_env_used], ecx

.seed_defaults:
    ; Ensure PATHEXT and PROMPT have authentic defaults if absent, and sync current drive "=X:"
    mov esi, addr(szVarPathext)
    call env_get_raw32
    test eax, eax
    jnz .chk_prompt
    mov esi, addr(szVarPathext)
    mov edi, addr(szDefPathext32)
    call env_set32
.chk_prompt:
    mov esi, addr(szVarPrompt)
    call env_get_raw32
    test eax, eax
    jnz .sync_cwd
    mov esi, addr(szVarPrompt)
    mov edi, addr(szDefPrompt32)
    call env_set32
.sync_cwd:
    mov ecx, 1024
    call arena_alloc32
    jc .ok
    mov edi, eax
    mov ecx, 1020
    call hal_get_cwd32
    jc .ok
    mov esi, edi
    call path_sync_drive_env32

.ok:
    pop edi
    pop esi
    pop ebx
    clc
    ret
.fail:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc env_match_key32
;; @abi win32
;; @in ESI = entry in g_env_buf ("KEY=VAL"), EDI = lookup name ("KEY")
;; @out EAX = pointer to VALUE (after '=') if matched case-insensitively, else 0
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Matches an environment entry against a variable name (supporting leading '=' hidden drive vars!).
@proc env_match_key32
    push esi
    push edi

    ; Support leading '=' in hidden vars like "=C:"
    cmp byte [esi], '='
    jne .lp
    cmp byte [edi], '='
    jne .no
    inc esi
    inc edi

.lp:
    mov al, byte [edi]
    test al, al
    jz .chk_eq
    mov dl, byte [esi]
    test dl, dl
    jz .no
    cmp dl, '='
    je .no
    call ascii_upper_al
    mov cl, al
    mov al, dl
    call ascii_upper_al
    cmp cl, al
    jne .no
    inc esi
    inc edi
    jmp .lp

.chk_eq:
    cmp byte [esi], '='
    jne .no
    lea eax, [esi + 1]
    pop edi
    pop esi
    ret

.no:
    xor eax, eax
    pop edi
    pop esi
    ret
@endproc

;; @proc env_get_raw32
;; @abi win32
;; @in ESI = NUL-terminated variable name
;; @out EAX = pointer to VALUE inside g_env_buf, or 0 if not explicitly defined
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Searches explicit environment entries in g_env_buf without dynamic variable synthesis.
@proc env_get_raw32
    push ebx
    push esi
    push edi

    mov edi, esi
    mov esi, dword [g_env_buf]
    test esi, esi
    jz .not_found

.scan_lp:
    cmp byte [esi], 0
    je .not_found
    call env_match_key32
    test eax, eax
    jnz .found
    call str_len32
    lea esi, [esi + ecx + 1]
    jmp .scan_lp

.not_found:
    xor eax, eax
.found:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc env_get32
;; @abi win32
;; @in ESI = NUL-terminated variable name
;; @out EAX = NUL-terminated value string (explicit or synthesized dynamic variable in arena), or 0
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Looks up explicit environment variables first, then synthesizes dynamic cmd.exe variables when Extensions are ON.
@proc env_get32
    push ebx
    push esi
    push edi

    call env_get_raw32
    test eax, eax
    jnz .ret_val

    cmp byte [g_extensions_on], 0
    je .not_found

    ; Dynamic variable synthesis: CD, ERRORLEVEL, RANDOM, CMDEXTVERSION, CMDCMDLINE, DATE, TIME, HIGHESTNUMANODENUMBER, =ExitCode, =ExitCodeAscii
    mov edi, addr(szDynErrorlevel)
    call str_eq_i32
    test eax, eax
    jnz .dyn_errlvl

    mov edi, addr(szDynCd)
    call str_eq_i32
    test eax, eax
    jnz .dyn_cd

    mov edi, addr(szDynRandom)
    call str_eq_i32
    test eax, eax
    jnz .dyn_random

    mov edi, addr(szDynCmdExtVer)
    call str_eq_i32
    test eax, eax
    jnz .dyn_extver

    mov edi, addr(szDynCmdCmdLine)
    call str_eq_i32
    test eax, eax
    jnz .dyn_cmdline

    mov edi, addr(szDynExitCode)
    call str_eq_i32
    test eax, eax
    jnz .dyn_exithex

    mov edi, addr(szDynExitAscii)
    call str_eq_i32
    test eax, eax
    jnz .dyn_exitasc

    mov edi, addr(szDynDate)
    call str_eq_i32
    test eax, eax
    jnz .dyn_date

    mov edi, addr(szDynTime)
    call str_eq_i32
    test eax, eax
    jnz .dyn_time

    mov edi, addr(szDynNuma)
    call str_eq_i32
    test eax, eax
    jnz .dyn_numa

    jmp .not_found

.dyn_errlvl:
    mov ecx, 24
    call arena_alloc32
    jc .not_found
    mov edi, eax
    mov eax, dword [g_last_exit_code]
    call fmt_i32_dec
    mov eax, edi
    jmp .ret_val

.dyn_cd:
    mov ecx, 2048
    call arena_alloc32
    jc .not_found
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32
    jc .not_found
    mov eax, edi
    jmp .ret_val

.dyn_random:
    ; LCG seeded from GetTickCount XOR PID -> 0..32767
    mov eax, dword [g_rng_state]
    test eax, eax
    jnz .step_rng
    call dword [__imp__GetTickCount@0]
    mov ebx, eax
    call dword [__imp__GetCurrentProcessId@0]
    xor eax, ebx
    or eax, 1
.step_rng:
    imul eax, eax, 214013
    add eax, 2531011
    mov dword [g_rng_state], eax
    shr eax, 16
    and eax, 0x7FFF
    mov ebx, eax
    mov ecx, 16
    call arena_alloc32
    jc .not_found
    mov edi, eax
    mov eax, ebx
    call fmt_i32_dec
    mov eax, edi
    jmp .ret_val

.dyn_extver:
    mov eax, addr(szValExtVer2)
    jmp .ret_val

.dyn_cmdline:
    mov eax, dword [g_raw_cmdline]
    jmp .ret_val

.dyn_exithex:
    mov ecx, 16
    call arena_alloc32
    jc .not_found
    mov edi, eax
    mov eax, dword [g_last_exit_code]
    call fmt_hex32_8
    mov eax, edi
    jmp .ret_val

.dyn_exitasc:
    mov eax, dword [g_last_exit_code]
    and eax, 0xFF
    cmp eax, 0x20
    jb .not_found
    mov ebx, eax
    mov ecx, 8
    call arena_alloc32
    jc .not_found
    mov byte [eax], bl
    mov byte [eax + 1], 0
    jmp .ret_val

.dyn_date:
    sub esp, 16
    mov ebx, esp
    push ebx
    call dword [__imp__GetLocalTime@4]
    mov ecx, 24
    call arena_alloc32
    jc .date_fail
    mov edi, eax
    mov esi, eax
    movzx eax, word [ebx + SYSTEMTIME16.wYear]
    xor edx, edx
    call fmt_u64_plain32
    add edi, ecx
    mov byte [edi], '-'
    inc edi
    movzx eax, word [ebx + SYSTEMTIME16.wMonth]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], '-'
    inc edi
    movzx eax, word [ebx + SYSTEMTIME16.wDay]
    mov dl, '0'
    call fmt_u32_pad2
    mov byte [eax], 0
    mov eax, esi
    add esp, 16
    jmp .ret_val
.date_fail:
    add esp, 16
    jmp .not_found

.dyn_time:
    sub esp, 16
    mov ebx, esp
    push ebx
    call dword [__imp__GetLocalTime@4]
    mov ecx, 24
    call arena_alloc32
    jc .date_fail
    mov edi, eax
    mov esi, eax
    ; Authentic cmd.exe %TIME%: leading SPACE when hour < 10!
    movzx eax, word [ebx + SYSTEMTIME16.wHour]
    mov dl, ' '
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], ':'
    inc edi
    movzx eax, word [ebx + SYSTEMTIME16.wMinute]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], ':'
    inc edi
    movzx eax, word [ebx + SYSTEMTIME16.wSecond]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], '.'
    inc edi
    movzx eax, word [ebx + SYSTEMTIME16.wMilliseconds]
    xor edx, edx
    mov ecx, 10
    div ecx
    mov dl, '0'
    call fmt_u32_pad2
    mov byte [eax], 0
    mov eax, esi
    add esp, 16
    jmp .ret_val

.dyn_numa:
    mov eax, addr(szValZeroStr)
    jmp .ret_val

.not_found:
    xor eax, eax
.ret_val:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc env_set32
;; @abi win32
;; @in ESI = variable name, EDI = value string (NULL or "" deletes the variable!)
;; @out Updates g_env_buf AND process Win32 environment (*W on NT, *A on Win9x), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY if 64 KiB block is full
;; @clobbers eax, ecx, edx
;; @desc Inserts, replaces, or deletes a variable in g_env_buf and mirrors to OS SetEnvironmentVariableW/A.
@proc env_set32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, esi ; name

    ; First remove any existing matching entry in g_env_buf
    mov edi, esi
    mov esi, dword [g_env_buf]
.rm_lp:
    cmp byte [esi], 0
    je .rm_done
    call env_match_key32
    test eax, eax
    jnz .do_compact
    call str_len32
    lea esi, [esi + ecx + 1]
    jmp .rm_lp

.do_compact:
    call str_len32
    lea ebx, [esi + ecx + 1] ; next entry
    mov edx, dword [g_env_buf]
    add edx, dword [g_env_used]
    sub edx, ebx ; trailing bytes including final NUL
    mov edi, esi
    mov esi, ebx
    mov ecx, edx
    rep movsb
    mov eax, edi
    sub eax, dword [g_env_buf]
    mov dword [g_env_used], eax

.rm_done:
    mov esi, ebp
    mov edi, dword [esp + 4] ; restore caller EDI (value)
    test edi, edi
    jz .sync_os_del
    cmp byte [edi], 0
    je .sync_os_del

    ; Append "NAME=VALUE\0\0" at end of g_env_buf (at g_env_used - 1)
    call str_len32
    mov ebx, ecx
    push esi
    mov esi, edi
    call str_len32
    pop esi
    lea edx, [ebx + ecx + 3]
    mov eax, dword [g_env_used]
    add eax, edx
    cmp eax, W32_ENV_CAP_BYTES
    jae .oom

    mov edx, dword [g_env_buf]
    add edx, dword [g_env_used]
    dec edx
    push edi
    mov edi, edx
    call str_copy32
    mov byte [eax], '='
    lea edi, [eax + 1]
    pop esi ; value
    push esi
    call str_copy32
    mov byte [eax + 1], 0
    add eax, 2
    sub eax, dword [g_env_buf]
    mov dword [g_env_used], eax
    pop edi
    mov esi, ebp

    ; Mirror to Win32 process environment so child CreateProcess inherits seamlessly!
    cmp byte [g_is_win9x], 0
    jne .os_set_a
    call wtf8_to_utf16le
    jc .ok
    mov ebx, eax
    mov esi, edi
    call wtf8_to_utf16le
    jc .ok
    push eax
    push ebx
    call dword [__imp__SetEnvironmentVariableW@8]
    jmp .ok
.os_set_a:
    push edi
    push esi
    call dword [__imp__SetEnvironmentVariableA@8]
    jmp .ok

.sync_os_del:
    cmp byte [g_is_win9x], 0
    jne .os_del_a
    call wtf8_to_utf16le
    jc .ok
    push 0
    push eax
    call dword [__imp__SetEnvironmentVariableW@8]
    jmp .ok
.os_del_a:
    push 0
    push esi
    call dword [__imp__SetEnvironmentVariableA@8]

.ok:
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret
.oom:
    mov eax, ERR_OUT_OF_MEMORY
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc env_setlocal_push32
;; @abi win32
;; @in none
;; @out Pushes environment, CWD, extensions, and delayed-expansion state onto 32-deep SETLOCAL stack
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY if stack full or allocation fails
;; @clobbers eax, ecx, edx
;; @desc Implements SETLOCAL snapshot capture up to 32 nested levels.
@proc env_setlocal_push32
    push ebx
    push esi
    push edi

    mov ebx, dword [g_setlocal_depth]
    cmp ebx, W32_SETLOCAL_MAX
    jae .full

    imul ebx, ebx, SETLOCAL_SNAP32_SIZE
    add ebx, addr(g_setlocal_stack)

    mov ecx, dword [g_env_used]
    add ecx, 16
    call perm_alloc32
    jc .fail
    mov dword [ebx + SETLOCAL_SNAP32.pSavedEnv], eax
    mov ecx, dword [g_env_used]
    mov dword [ebx + SETLOCAL_SNAP32.cbSavedEnv], ecx
    mov edi, eax
    mov esi, dword [g_env_buf]
    rep movsb

    mov ecx, 2048
    call perm_alloc32
    jc .fail_env
    mov dword [ebx + SETLOCAL_SNAP32.pSavedCwd], eax
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32

    mov al, byte [g_extensions_on]
    mov byte [ebx + SETLOCAL_SNAP32.bSavedExt], al
    mov al, byte [g_delayed_exp]
    mov byte [ebx + SETLOCAL_SNAP32.bSavedDelayed], al

    inc dword [g_setlocal_depth]
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fail_env:
    mov eax, dword [ebx + SETLOCAL_SNAP32.pSavedEnv]
    call perm_free32
.full:
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc env_endlocal_pop32
;; @abi win32
;; @in none
;; @out Restores most recent SETLOCAL snapshot (no-op if depth == 0)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Restores saved environment block, Win32 OS environment, CWD, and shell flags on ENDLOCAL or batch exit.
@proc env_endlocal_pop32
    push ebx
    push esi
    push edi

    mov eax, dword [g_setlocal_depth]
    test eax, eax
    jz .done
    dec eax
    mov dword [g_setlocal_depth], eax

    imul ebx, eax, SETLOCAL_SNAP32_SIZE
    add ebx, addr(g_setlocal_stack)

    mov esi, dword [ebx + SETLOCAL_SNAP32.pSavedEnv]
    mov edi, dword [g_env_buf]
    mov ecx, dword [ebx + SETLOCAL_SNAP32.cbSavedEnv]
    mov dword [g_env_used], ecx
    rep movsb

    mov eax, dword [ebx + SETLOCAL_SNAP32.pSavedEnv]
    call perm_free32
    mov dword [ebx + SETLOCAL_SNAP32.pSavedEnv], 0

    mov esi, dword [ebx + SETLOCAL_SNAP32.pSavedCwd]
    test esi, esi
    jz .rest_flags
    cmp byte [esi], 0
    je .free_cwd
    call hal_set_cwd32
.free_cwd:
    mov eax, esi
    call perm_free32
    mov dword [ebx + SETLOCAL_SNAP32.pSavedCwd], 0

.rest_flags:
    mov al, byte [ebx + SETLOCAL_SNAP32.bSavedExt]
    mov byte [g_extensions_on], al
    mov al, byte [ebx + SETLOCAL_SNAP32.bSavedDelayed]
    mov byte [g_delayed_exp], al

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc env_build_unicode_block32
;; @abi win32
;; @in none
;; @out EAX = UTF-16LE double-NUL environment block in statement arena for CreateProcessW, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Builds a CREATE_UNICODE_ENVIRONMENT block from g_env_buf so child processes see exact SET/ENDLOCAL state.
@proc env_build_unicode_block32
    push ebx
    push esi
    push edi

    mov ecx, dword [g_env_used]
    lea ecx, [ecx + ecx + 32]
    call arena_alloc32
    jc .fail
    mov ebx, eax
    mov edi, eax
    mov esi, dword [g_env_buf]

.ent_lp:
    cmp byte [esi], 0
    je .finish
    push ebx
    call wtf8_to_utf16le
    pop ebx
    jc .fail
    push esi
    mov esi, eax
    lea ecx, [ecx + 1]
    rep movsw
    pop esi
    call str_len32
    lea esi, [esi + ecx + 1]
    jmp .ent_lp

.finish:
    mov word [edi], 0
    mov word [edi + 2], 0
    mov eax, ebx
    pop edi
    pop esi
    pop ebx
    clc
    ret
.fail:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

; ============================================================================
; 11-Tier Precedence 32-Bit Signed SET /A Evaluator (arith_eval32)
; ============================================================================

;; @proc sa32_skip_ws
;; @abi win32
;; @in ESI = expression cursor
;; @out EAX = updated cursor past whitespace/quotes
;; @err none
;; @clobbers eax, edx
;; @desc Advances expression cursor past spaces, tabs, and enclosing double quotes.
@proc sa32_skip_ws
    mov eax, esi
.lp:
    mov dl, byte [eax]
    cmp dl, 0x20
    je .adv
    cmp dl, 0x09
    je .adv
    cmp dl, '"'
    je .adv
    ret
.adv:
    inc eax
    jmp .lp
@endproc

;; @proc sa32_is_ident_start
;; @abi win32
;; @in AL = byte
;; @out CF=0 if identifier start ('A'..'Z', 'a'..'z', '_', 0x80+), CF=1 otherwise
;; @err CF=1 if not identifier start
;; @clobbers none
;; @desc Tests whether AL begins a bare SET /A variable identifier.
@proc sa32_is_ident_start
    cmp al, '_'
    je .yes
    cmp al, 0x80
    jae .yes
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    jbe .yes
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
.yes:
    clc
    ret
.no:
    stc
    ret
@endproc

;; @proc sa32_parse_i32_str
;; @abi win32
;; @in ESI = string cursor
;; @out EAX = 32-bit value, EDX = updated cursor, CF=0; or CF=1, EAX=ERR_INVALID_NUMBER
;; @err CF=1, EAX=ERR_INVALID_NUMBER (e.g. invalid octal 08 / 09)
;; @clobbers eax, ecx, edx
;; @desc Parses decimal, 0x hex, 0 octal (rejecting 08/09!), or 'c' character literal.
@proc sa32_parse_i32_str
    push ebx
    push esi

    cmp byte [esi], 0x27 ; single quote 'c'
    jne .chk_hex
    movzx eax, byte [esi + 1]
    test eax, eax
    jz .bad_num
    add esi, 2
    cmp byte [esi], 0x27
    jne .ret_ok
    inc esi
    jmp .ret_ok

.chk_hex:
    cmp byte [esi], '0'
    jne .dec
    mov al, byte [esi + 1]
    cmp al, 'x'
    je .hex
    cmp al, 'X'
    je .hex
    cmp al, '0'
    jb .single_zero
    cmp al, '9'
    ja .single_zero

    ; Octal literal 0... (08 and 09 MUST fail with ERR_INVALID_NUMBER!)
    inc esi
    xor eax, eax
.oct_lp:
    mov cl, byte [esi]
    cmp cl, '0'
    jb .chk_oct_trail
    cmp cl, '7'
    ja .chk_oct_bad
    sub cl, '0'
    movzx ecx, cl
    shl eax, 3
    add eax, ecx
    inc esi
    jmp .oct_lp
.chk_oct_bad:
    cmp cl, '9'
    jbe .bad_num
.chk_oct_trail:
    jmp .ret_ok

.single_zero:
    xor eax, eax
    inc esi
    jmp .ret_ok

.hex:
    add esi, 2
    xor eax, eax
    xor ebx, ebx
.hex_lp:
    mov cl, byte [esi]
    cmp cl, '0'
    jb .hex_end
    cmp cl, '9'
    jbe .hex_dig
    cmp cl, 'A'
    jb .hex_lo
    cmp cl, 'F'
    jbe .hex_up
.hex_lo:
    cmp cl, 'a'
    jb .hex_end
    cmp cl, 'f'
    ja .hex_end
    sub cl, 'a' - 10
    jmp .hex_acc
.hex_up:
    sub cl, 'A' - 10
    jmp .hex_acc
.hex_dig:
    sub cl, '0'
.hex_acc:
    movzx ecx, cl
    shl eax, 4
    or eax, ecx
    inc esi
    inc ebx
    jmp .hex_lp
.hex_end:
    test ebx, ebx
    jz .bad_num
    jmp .ret_ok

.dec:
    xor eax, eax
    xor ebx, ebx
.dec_lp:
    mov cl, byte [esi]
    cmp cl, '0'
    jb .dec_end
    cmp cl, '9'
    ja .dec_end
    sub cl, '0'
    movzx ecx, cl
    imul eax, eax, 10
    add eax, ecx
    inc esi
    inc ebx
    jmp .dec_lp
.dec_end:
    test ebx, ebx
    jz .bad_num

.ret_ok:
    mov edx, esi
    pop esi
    pop ebx
    clc
    ret

.bad_num:
    mov eax, ERR_INVALID_NUMBER
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc sa32_eval_primary
;; @abi win32
;; @in [g_sa_ptr] = cursor
;; @out EAX = 32-bit signed value, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Parses SET /A parenthesized subexpressions, unary + - ! ~, numeric/char literals, and bare variable assignments.
@proc sa32_eval_primary
    push ebx
    push esi
    push edi

    mov esi, dword [g_sa_ptr]
    call sa32_skip_ws
    mov esi, eax
    mov dword [g_sa_ptr], esi

    mov al, byte [esi]
    test al, al
    jz .missing_op

    cmp al, '('
    je .parens
    cmp al, '+'
    je .un_plus
    cmp al, '-'
    je .un_minus
    cmp al, '!'
    je .un_not
    cmp al, '~'
    je .un_bitnot

    cmp al, 0x27
    je .number
    cmp al, '0'
    jb .chk_var
    cmp al, '9'
    jbe .number

.chk_var:
    call sa32_is_ident_start
    jc .missing_op

    ; Scan identifier [esi..ebx]
    mov ebx, esi
.id_lp:
    mov al, byte [ebx]
    call sa32_is_ident_start
    jnc .id_adv
    cmp al, '0'
    jb .id_end
    cmp al, '9'
    ja .id_end
.id_adv:
    inc ebx
    jmp .id_lp
.id_end:
    ; Copy variable name into arena
    mov ecx, ebx
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .err_out
    mov edi, eax
    push edi
    rep movsb
    mov byte [edi], 0
    pop edi ; varName

    mov esi, ebx
    call sa32_skip_ws
    mov esi, eax
    mov dword [g_sa_ptr], esi

    ; Check assignment operators: =, +=, -=, *=, /=, %=, &=, ^=, |=, <<=, >>=
    xor edx, edx ; op code: 1='=', 2='+=', 3='-=', 4='*=', 5='/=', 6='%=', 7='&=', 8='^=', 9='|=', 10='<<=', 11='>>='
    mov al, byte [esi]
    mov ah, byte [esi + 1]
    cmp al, '='
    jne .chk_comp_asgn
    cmp ah, '='
    je .just_read_var
    mov edx, 1
    inc esi
    jmp .do_assign

.chk_comp_asgn:
    cmp ah, '='
    jne .chk_shift_asgn
    cmp al, '+'
    je .asgn_add
    cmp al, '-'
    je .asgn_sub
    cmp al, '*'
    je .asgn_mul
    cmp al, '/'
    je .asgn_div
    cmp al, '%'
    je .asgn_mod
    cmp al, '&'
    je .asgn_and
    cmp al, '^'
    je .asgn_xor
    cmp al, '|'
    je .asgn_or
    jmp .just_read_var

.asgn_add:
    mov edx, 2
    add esi, 2
    jmp .do_assign
.asgn_sub:
    mov edx, 3
    add esi, 2
    jmp .do_assign
.asgn_mul:
    mov edx, 4
    add esi, 2
    jmp .do_assign
.asgn_div:
    mov edx, 5
    add esi, 2
    jmp .do_assign
.asgn_mod:
    mov edx, 6
    add esi, 2
    jmp .do_assign
.asgn_and:
    mov edx, 7
    add esi, 2
    jmp .do_assign
.asgn_xor:
    mov edx, 8
    add esi, 2
    jmp .do_assign
.asgn_or:
    mov edx, 9
    add esi, 2
    jmp .do_assign

.chk_shift_asgn:
    cmp byte [esi + 2], '='
    jne .just_read_var
    cmp al, '<'
    jne .chk_shr_asgn
    cmp ah, '<'
    jne .just_read_var
    mov edx, 10
    add esi, 3
    jmp .do_assign
.chk_shr_asgn:
    cmp al, '>'
    jne .just_read_var
    cmp ah, '>'
    jne .just_read_var
    mov edx, 11
    add esi, 3

.do_assign:
    mov dword [g_sa_ptr], esi
    push edx
    push edi
    mov ecx, 1 ; right-associative assignment precedence = 1 (above comma = 0)
    call sa32_eval_prec
    pop edi
    pop edx
    jc .err_out
    mov ebx, eax ; rhs

    cmp edx, 1
    je .store_var

    ; Read existing LHS variable value (0 if undefined)
    push edx
    mov esi, edi
    call sa32_read_var_i32
    pop edx
    ; EAX = lhs, EBX = rhs
    cmp edx, 2
    je .op_add
    cmp edx, 3
    je .op_sub
    cmp edx, 4
    je .op_mul
    cmp edx, 5
    je .op_div
    cmp edx, 6
    je .op_mod
    cmp edx, 7
    je .op_and
    cmp edx, 8
    je .op_xor
    cmp edx, 9
    je .op_or
    cmp edx, 10
    je .op_shl
    mov ecx, ebx
    call sar32_safe
    mov ebx, eax
    jmp .store_var
.op_add:
    add ebx, eax
    jmp .store_var
.op_sub:
    sub eax, ebx
    mov ebx, eax
    jmp .store_var
.op_mul:
    imul ebx, eax
    jmp .store_var
.op_div:
    mov ecx, ebx
    call sdiv32_safe
    jc .err_out
    mov ebx, eax
    jmp .store_var
.op_mod:
    mov ecx, ebx
    call sdiv32_safe
    jc .err_out
    mov ebx, edx
    jmp .store_var
.op_and:
    and ebx, eax
    jmp .store_var
.op_xor:
    xor ebx, eax
    jmp .store_var
.op_or:
    or ebx, eax
    jmp .store_var
.op_shl:
    mov ecx, ebx
    call shl32_safe
    mov ebx, eax

.store_var:
    sub esp, 24
    mov esi, edi
    mov edi, esp
    mov eax, ebx
    call fmt_i32_dec
    call env_set32
    add esp, 24
    mov eax, ebx
    jmp .ok

.just_read_var:
    mov esi, edi
    call sa32_read_var_i32
    jmp .ok

.parens:
    inc esi
    mov dword [g_sa_ptr], esi
    xor ecx, ecx
    call sa32_eval_prec
    jc .err_out
    mov ebx, eax
    mov esi, dword [g_sa_ptr]
    call sa32_skip_ws
    cmp byte [eax], ')'
    jne .unbal_paren
    inc eax
    mov dword [g_sa_ptr], eax
    mov eax, ebx
    jmp .ok

.un_plus:
    inc esi
    mov dword [g_sa_ptr], esi
    call sa32_eval_primary
    jmp .out_cf

.un_minus:
    inc esi
    mov dword [g_sa_ptr], esi
    call sa32_eval_primary
    jc .err_out
    neg eax
    jmp .ok

.un_not:
    inc esi
    mov dword [g_sa_ptr], esi
    call sa32_eval_primary
    jc .err_out
    xor ecx, ecx
    test eax, eax
    setz cl
    mov eax, ecx
    jmp .ok

.un_bitnot:
    inc esi
    mov dword [g_sa_ptr], esi
    call sa32_eval_primary
    jc .err_out
    not eax
    jmp .ok

.number:
    call sa32_parse_i32_str
    jc .err_out
    mov dword [g_sa_ptr], edx
    jmp .ok

.unbal_paren:
    mov eax, ERR_SYNTAX
    stc
    jmp .out_cf

.missing_op:
    mov eax, ERR_MISSING_OPERAND
    stc
    jmp .out_cf

.ok:
    clc
.err_out:
.out_cf:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc sa32_read_var_i32
;; @abi win32
;; @in ESI = variable name
;; @out EAX = signed 32-bit value (0 if undefined or non-numeric)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Reads an environment variable for SET /A, treating undefined variables as 0.
@proc sa32_read_var_i32
    push esi
    call env_get32
    test eax, eax
    jz .zero
    mov esi, eax
    call sa32_skip_ws
    mov esi, eax
    xor ecx, ecx
    cmp byte [esi], '-'
    jne .chk_p
    mov ecx, 1
    inc esi
    jmp .parse
.chk_p:
    cmp byte [esi], '+'
    jne .parse
    inc esi
.parse:
    push ecx
    call sa32_parse_i32_str
    pop ecx
    jc .zero
    test ecx, ecx
    jz .done
    neg eax
    jmp .done
.zero:
    xor eax, eax
.done:
    pop esi
    ret
@endproc

;; @proc sa32_peek_binop
;; @abi win32
;; @in ESI = cursor
;; @out EAX = precedence (0=comma, 2=|, 3=^, 4=&, 5=shift, 6=add/sub, 7=mul/div/mod, -1=none), DL = opid, ECX = token len
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Classifies binary SET /A operators and returns Pratt precedence tier and token width.
@proc sa32_peek_binop
    mov al, byte [esi]
    mov ah, byte [esi + 1]
    cmp al, ','
    je .comma
    cmp al, '*'
    je .mul
    cmp al, '/'
    je .div
    cmp al, '%'
    je .mod
    cmp al, '+'
    je .add
    cmp al, '-'
    je .sub
    cmp al, '<'
    je .chk_shl
    cmp al, '>'
    je .chk_shr
    cmp al, '&'
    je .bit_and
    cmp al, '^'
    je .bit_xor
    cmp al, '|'
    je .bit_or
    jmp .none

.comma:
    xor eax, eax
    mov dl, ','
    mov ecx, 1
    ret
.mul:
    cmp ah, '='
    je .none
    mov eax, 7
    mov dl, '*'
    mov ecx, 1
    ret
.div:
    cmp ah, '='
    je .none
    mov eax, 7
    mov dl, '/'
    mov ecx, 1
    ret
.mod:
    cmp ah, '='
    je .none
    mov eax, 7
    mov dl, '%'
    mov ecx, 1
    ret
.add:
    cmp ah, '='
    je .none
    mov eax, 6
    mov dl, '+'
    mov ecx, 1
    ret
.sub:
    cmp ah, '='
    je .none
    mov eax, 6
    mov dl, '-'
    mov ecx, 1
    ret
.chk_shl:
    cmp ah, '<'
    jne .none
    cmp byte [esi + 2], '='
    je .none
    mov eax, 5
    mov dl, '<'
    mov ecx, 2
    ret
.chk_shr:
    cmp ah, '>'
    jne .none
    cmp byte [esi + 2], '='
    je .none
    mov eax, 5
    mov dl, '>'
    mov ecx, 2
    ret
.bit_and:
    cmp ah, '='
    je .none
    mov eax, 4
    mov dl, '&'
    mov ecx, 1
    ret
.bit_xor:
    cmp ah, '='
    je .none
    mov eax, 3
    mov dl, '^'
    mov ecx, 1
    ret
.bit_or:
    cmp ah, '='
    je .none
    mov eax, 2
    mov dl, '|'
    mov ecx, 1
    ret
.none:
    mov eax, -1
    xor edx, edx
    xor ecx, ecx
    ret
@endproc

;; @proc sa32_eval_prec
;; @abi win32
;; @in ECX = minimum precedence tier, [g_sa_ptr] = cursor
;; @out EAX = evaluated 32-bit signed result, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Pratt precedence-climbing evaluator across all 11 SET /A operator tiers.
@proc sa32_eval_prec
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx ; min_prec

    call sa32_eval_primary
    jc .fail
    mov ebx, eax ; lhs

.bin_lp:
    mov esi, dword [g_sa_ptr]
    call sa32_skip_ws
    mov esi, eax
    mov dword [g_sa_ptr], esi

    call sa32_peek_binop
    cmp eax, -1
    je .ok
    cmp eax, ebp
    jl .ok

    ; Consume operator and evaluate RHS with prec + 1
    add esi, ecx
    mov dword [g_sa_ptr], esi
    movzx edi, dl ; op char
    lea ecx, [eax + 1]
    call sa32_eval_prec
    jc .fail
    mov ecx, eax ; rhs

    cmp edi, ','
    je .do_comma
    cmp edi, '*'
    je .do_mul
    cmp edi, '/'
    je .do_div
    cmp edi, '%'
    je .do_mod
    cmp edi, '+'
    je .do_add
    cmp edi, '-'
    je .do_sub
    cmp edi, '<'
    je .do_shl
    cmp edi, '>'
    je .do_shr
    cmp edi, '&'
    je .do_and
    cmp edi, '^'
    je .do_xor
    or ebx, ecx
    jmp .bin_lp

.do_comma:
    mov ebx, ecx
    jmp .bin_lp
.do_mul:
    imul ebx, ecx
    jmp .bin_lp
.do_div:
    mov eax, ebx
    call sdiv32_safe
    jc .fail
    mov ebx, eax
    jmp .bin_lp
.do_mod:
    mov eax, ebx
    call sdiv32_safe
    jc .fail
    mov ebx, edx
    jmp .bin_lp
.do_add:
    add ebx, ecx
    jmp .bin_lp
.do_sub:
    sub ebx, ecx
    jmp .bin_lp
.do_shl:
    mov eax, ebx
    call shl32_safe
    mov ebx, eax
    jmp .bin_lp
.do_shr:
    mov eax, ebx
    call sar32_safe
    mov ebx, eax
    jmp .bin_lp
.do_and:
    and ebx, ecx
    jmp .bin_lp
.do_xor:
    xor ebx, ecx
    jmp .bin_lp

.ok:
    mov eax, ebx
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret
.fail:
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc arith_eval32
;; @abi win32
;; @in ESI = NUL-terminated SET /A expression
;; @out EAX = signed 32-bit result, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_DIV_BY_ZERO / ERR_INVALID_NUMBER / ERR_MISSING_OPERAND / ERR_SYNTAX
;; @clobbers eax, ecx, edx
;; @desc Top-level entry point for SET /A expression evaluation.
@proc arith_eval32
    push esi
    mov dword [g_sa_ptr], esi
    xor ecx, ecx
    call sa32_eval_prec
    jc .fail
    push eax
    mov esi, dword [g_sa_ptr]
    call sa32_skip_ws
    cmp byte [eax], 0
    pop eax
    jne .bad_tail
    pop esi
    clc
    ret
.bad_tail:
    mov eax, ERR_SYNTAX
.fail:
    pop esi
    stc
    ret
@endproc
