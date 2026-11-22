;; @file env_sa64.inc
;; @isa x64
;; @desc Win64 WTF-8 Environment Manager, 32-level SETLOCAL stack, Dynamic Variables, and 11-tier SET /A evaluator.

@cpu x64
@format inc

;; @proc env_init64
;; @abi win64_ms
;; @in none
;; @out Allocates g_env_buf (64 KiB) and imports initial process environment via GetEnvironmentStringsW
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Imports the parent Win64 UTF-16LE environment block into our WTF-8 KEY=VALUE\0...\0\0 store.
@proc_frame env_init64, 40, rbx, rsi, rdi, r12
    mov ecx, W64_ENV_CAP_BYTES
    call perm_alloc64
    jc .fail
    mov qword [rel g_env_buf], rax
    mov word [rax], 0
    mov qword [rel g_env_used], 2

    call qword [rel __imp_GetEnvironmentStringsW]
    test rax, rax
    jz .seed_defaults
    mov rbx, rax
    mov rsi, rax
    mov rdi, qword [rel g_env_buf]
    mov r12d, W64_ENV_CAP_BYTES - 4

.w_entry_lp:
    cmp word [rsi], 0
    je .w_done
    mov rcx, rsi
    call wcs_len64
    mov r10, rax
    mov rcx, rsi
    mov rdx, rax
    mov r8, rdi
    mov r9, r12
    mov qword [rsp + 32], r10
    call utf16le_to_wtf8_buf64
    mov r10, qword [rsp + 32]
    lea rsi, [rsi + r10 * 2 + 2]
    inc rax
    add rdi, rax
    sub r12, rax
    cmp r12, 16
    ja .w_entry_lp

.w_done:
    mov byte [rdi], 0
    inc rdi
    sub rdi, qword [rel g_env_buf]
    mov qword [rel g_env_used], rdi

    mov rcx, rbx
    call qword [rel __imp_FreeEnvironmentStringsW]

.seed_defaults:
    lea rcx, [rel szVarPathext64]
    call env_get_raw64
    test rax, rax
    jnz .chk_prompt
    lea rcx, [rel szVarPathext64]
    lea rdx, [rel szDefPathext64]
    call env_set64
.chk_prompt:
    lea rcx, [rel szVarPrompt64]
    call env_get_raw64
    test rax, rax
    jnz .sync_cwd
    lea rcx, [rel szVarPrompt64]
    lea rdx, [rel szDefPrompt64]
    call env_set64
.sync_cwd:
    mov ecx, 2048
    call arena_alloc64
    jc .ok
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .ok
    mov rcx, rdi
    call path_sync_drive_env64

.ok:
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc env_match_key64
;; @abi win64_ms
;; @in RCX = entry in g_env_buf ("KEY=VAL"), RDX = lookup name ("KEY")
;; @out RAX = pointer to VALUE (after '=') if matched case-insensitively, else 0
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9
;; @desc Matches an environment entry against a variable name (supporting leading '=' hidden drive vars).
@proc env_match_key64
    cmp byte [rcx], '='
    jne .lp
    cmp byte [rdx], '='
    jne .no
    inc rcx
    inc rdx

.lp:
    mov al, byte [rdx]
    test al, al
    jz .chk_eq
    mov r8b, byte [rcx]
    test r8b, r8b
    jz .no
    cmp r8b, '='
    je .no
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    cmp r8b, 'a'
    jb .u2
    cmp r8b, 'z'
    ja .u2
    sub r8b, 32
.u2:
    cmp al, r8b
    jne .no
    inc rcx
    inc rdx
    jmp .lp

.chk_eq:
    cmp byte [rcx], '='
    jne .no
    lea rax, [rcx + 1]
    ret

.no:
    xor eax, eax
    ret
@endproc

;; @proc env_get_raw64
;; @abi win64_ms
;; @in RCX = NUL-terminated variable name
;; @out RAX = pointer to VALUE inside g_env_buf, or 0 if not explicitly defined
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Searches explicit environment entries in g_env_buf without dynamic variable synthesis.
@proc_frame env_get_raw64, 40, rsi, rdi
    mov rdi, rcx
    mov rsi, qword [rel g_env_buf]
    test rsi, rsi
    jz .not_found

.scan_lp:
    cmp byte [rsi], 0
    je .not_found
    mov rcx, rsi
    mov rdx, rdi
    call env_match_key64
    test rax, rax
    jnz .found
    mov rcx, rsi
    call str_len64
    lea rsi, [rsi + rax + 1]
    jmp .scan_lp

.not_found:
    xor eax, eax
.found:
    @epilog_ret
@endproc

;; @proc env_get64
;; @abi win64_ms
;; @in RCX = NUL-terminated variable name
;; @out RAX = NUL-terminated value string (explicit or synthesized dynamic variable in arena), or 0
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Looks up explicit environment variables first, then synthesizes dynamic cmd.exe variables when Extensions are ON.
@proc_frame env_get64, 64, rbx, rsi, rdi
    mov rsi, rcx
    call env_get_raw64
    test rax, rax
    jnz .ret_val

    cmp byte [rel g_extensions_on], 0
    je .not_found

    mov rcx, rsi
    lea rdx, [rel szDynErrorlevel64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_errlvl

    mov rcx, rsi
    lea rdx, [rel szDynCd64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_cd

    mov rcx, rsi
    lea rdx, [rel szDynRandom64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_random

    mov rcx, rsi
    lea rdx, [rel szDynCmdExtVer64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_extver

    mov rcx, rsi
    lea rdx, [rel szDynCmdCmdLine64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_cmdline

    mov rcx, rsi
    lea rdx, [rel szDynExitCode64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_exithex

    mov rcx, rsi
    lea rdx, [rel szDynExitAscii64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_exitasc

    mov rcx, rsi
    lea rdx, [rel szDynDate64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_date

    mov rcx, rsi
    lea rdx, [rel szDynTime64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_time

    mov rcx, rsi
    lea rdx, [rel szDynNuma64]
    call str_eq_i64
    test eax, eax
    jnz .dyn_numa

    jmp .not_found

.dyn_errlvl:
    mov ecx, 24
    call arena_alloc64
    jc .not_found
    mov rdi, rax
    mov ecx, dword [rel g_last_exit_code]
    mov rdx, rdi
    call fmt_i32_dec64
    mov rax, rdi
    @epilog_ret

.dyn_cd:
    mov ecx, 4096
    call arena_alloc64
    jc .not_found
    mov rdi, rax
    mov rcx, rdi
    mov edx, 4090
    call hal_get_cwd64
    jc .not_found
    mov rax, rdi
    @epilog_ret

.dyn_random:
    mov eax, dword [rel g_rng_state]
    test eax, eax
    jnz .step_rng
    call qword [rel __imp_GetTickCount]
    mov ebx, eax
    call qword [rel __imp_GetCurrentProcessId]
    xor eax, ebx
    or eax, 1
.step_rng:
    imul eax, eax, 214013
    add eax, 2531011
    mov dword [rel g_rng_state], eax
    shr eax, 16
    and eax, 0x7FFF
    mov ebx, eax
    mov ecx, 16
    call arena_alloc64
    jc .not_found
    mov rdi, rax
    mov ecx, ebx
    mov rdx, rdi
    call fmt_i32_dec64
    mov rax, rdi
    @epilog_ret

.dyn_extver:
    lea rax, [rel szValExtVer2_64]
    @epilog_ret

.dyn_cmdline:
    mov rax, qword [rel g_raw_cmdline]
    @epilog_ret

.dyn_exithex:
    mov ecx, 16
    call arena_alloc64
    jc .not_found
    mov rdi, rax
    mov ecx, dword [rel g_last_exit_code]
    mov rdx, rdi
    call fmt_hex32_8_64
    mov rax, rdi
    @epilog_ret

.dyn_exitasc:
    mov eax, dword [rel g_last_exit_code]
    and eax, 0xFF
    cmp eax, 0x20
    jb .not_found
    mov ebx, eax
    mov ecx, 8
    call arena_alloc64
    jc .not_found
    mov byte [rax], bl
    mov byte [rax + 1], 0
    @epilog_ret

.dyn_date:
    lea rbx, [rsp + 32]
    mov rcx, rbx
    call qword [rel __imp_GetLocalTime]
    mov ecx, 24
    call arena_alloc64
    jc .not_found
    mov rsi, rax
    movzx ecx, word [rbx + SYSTEMTIME16.wYear]
    mov rdx, rsi
    call fmt_u64_plain64
    lea rdi, [rsi + rax]
    mov byte [rdi], '-'
    inc rdi
    movzx ecx, word [rbx + SYSTEMTIME16.wMonth]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], '-'
    inc rdi
    movzx ecx, word [rbx + SYSTEMTIME16.wDay]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov byte [rax], 0
    mov rax, rsi
    @epilog_ret

.dyn_time:
    lea rbx, [rsp + 32]
    mov rcx, rbx
    call qword [rel __imp_GetLocalTime]
    mov ecx, 24
    call arena_alloc64
    jc .not_found
    mov rsi, rax
    movzx ecx, word [rbx + SYSTEMTIME16.wHour]
    mov dl, ' '
    mov r8, rsi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], ':'
    inc rdi
    movzx ecx, word [rbx + SYSTEMTIME16.wMinute]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], ':'
    inc rdi
    movzx ecx, word [rbx + SYSTEMTIME16.wSecond]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], '.'
    inc rdi
    movzx eax, word [rbx + SYSTEMTIME16.wMilliseconds]
    xor edx, edx
    mov ecx, 10
    div ecx
    mov ecx, eax
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov byte [rax], 0
    mov rax, rsi
    @epilog_ret

.dyn_numa:
    lea rax, [rel szValZeroStr64]
    @epilog_ret

.not_found:
    xor eax, eax
.ret_val:
    @epilog_ret
@endproc

;; @proc env_set64
;; @abi win64_ms
;; @in RCX = variable name, RDX = value string (0 or "" deletes the variable!)
;; @out Updates g_env_buf AND process Win64 environment (SetEnvironmentVariableW), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY if 64 KiB block is full
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Inserts, replaces, or deletes a variable in g_env_buf and mirrors to OS SetEnvironmentVariableW.
@proc_frame env_set64, 48, rbp, rbx, rsi, rdi, r12
    mov rbp, rcx ; name
    mov r12, rdx ; value

    mov rsi, qword [rel g_env_buf]
.rm_lp:
    cmp byte [rsi], 0
    je .rm_done
    mov rcx, rsi
    mov rdx, rbp
    call env_match_key64
    test rax, rax
    jnz .do_compact
    mov rcx, rsi
    call str_len64
    lea rsi, [rsi + rax + 1]
    jmp .rm_lp

.do_compact:
    mov rcx, rsi
    call str_len64
    lea rbx, [rsi + rax + 1]
    mov rdx, qword [rel g_env_buf]
    add rdx, qword [rel g_env_used]
    sub rdx, rbx
    mov rdi, rsi
    mov rsi, rbx
    mov rcx, rdx
    rep movsb
    mov rax, rdi
    sub rax, qword [rel g_env_buf]
    mov qword [rel g_env_used], rax

.rm_done:
    test r12, r12
    jz .sync_os_del
    cmp byte [r12], 0
    je .sync_os_del

    mov rcx, rbp
    call str_len64
    mov rbx, rax
    mov rcx, r12
    call str_len64
    lea rdx, [rbx + rax + 3]
    mov rax, qword [rel g_env_used]
    add rax, rdx
    cmp rax, W64_ENV_CAP_BYTES
    jae .oom

    mov rcx, qword [rel g_env_buf]
    add rcx, qword [rel g_env_used]
    dec rcx
    mov rdx, rbp
    call str_copy64
    mov byte [rax], '='
    lea rcx, [rax + 1]
    mov rdx, r12
    call str_copy64
    mov byte [rax + 1], 0
    add rax, 2
    sub rax, qword [rel g_env_buf]
    mov qword [rel g_env_used], rax

    mov rcx, rbp
    call wtf8_to_utf16le64
    jc .ok
    mov rbx, rax
    mov rcx, r12
    call wtf8_to_utf16le64
    jc .ok
    mov rcx, rbx
    mov rdx, rax
    call qword [rel __imp_SetEnvironmentVariableW]
    jmp .ok

.sync_os_del:
    mov rcx, rbp
    call wtf8_to_utf16le64
    jc .ok
    mov rcx, rax
    xor edx, edx
    call qword [rel __imp_SetEnvironmentVariableW]

.ok:
    clc
    @epilog_ret
.oom:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc env_setlocal_push64
;; @abi win64_ms
;; @in none
;; @out Pushes environment, CWD, extensions, and delayed-expansion state onto 32-deep SETLOCAL stack
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY if stack full or allocation fails
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements SETLOCAL snapshot capture up to 32 nested levels.
@proc_frame env_setlocal_push64, 32, rbx, rsi, rdi
    mov eax, dword [rel g_setlocal_depth]
    cmp eax, W64_SETLOCAL_MAX
    jae .fail

    imul rbx, rax, SETLOCAL_SNAP64_SIZE
    lea rdx, [rel g_setlocal_stack]
    add rbx, rdx

    mov rcx, qword [rel g_env_used]
    add rcx, 16
    call perm_alloc64
    jc .fail
    mov qword [rbx + SETLOCAL_SNAP64.pSavedEnv], rax
    mov rcx, qword [rel g_env_used]
    mov qword [rbx + SETLOCAL_SNAP64.cbSavedEnv], rcx
    mov rdi, rax
    mov rsi, qword [rel g_env_buf]
    rep movsb

    mov ecx, 4096
    call perm_alloc64
    jc .fail_env
    mov qword [rbx + SETLOCAL_SNAP64.pSavedCwd], rax
    mov rcx, rax
    mov edx, 4090
    call hal_get_cwd64

    mov al, byte [rel g_extensions_on]
    mov byte [rbx + SETLOCAL_SNAP64.bSavedExt], al
    mov al, byte [rel g_delayed_exp]
    mov byte [rbx + SETLOCAL_SNAP64.bSavedDelayed], al

    inc dword [rel g_setlocal_depth]
    clc
    @epilog_ret

.fail_env:
    mov rcx, qword [rbx + SETLOCAL_SNAP64.pSavedEnv]
    call perm_free64
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc env_endlocal_pop64
;; @abi win64_ms
;; @in none
;; @out Restores most recent SETLOCAL snapshot (no-op if depth == 0)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Restores saved environment block, CWD, and shell flags on ENDLOCAL or batch exit.
@proc_frame env_endlocal_pop64, 32, rbx, rsi, rdi
    mov eax, dword [rel g_setlocal_depth]
    test eax, eax
    jz .done
    dec eax
    mov dword [rel g_setlocal_depth], eax

    imul rbx, rax, SETLOCAL_SNAP64_SIZE
    lea rdx, [rel g_setlocal_stack]
    add rbx, rdx

    mov rsi, qword [rbx + SETLOCAL_SNAP64.pSavedEnv]
    mov rdi, qword [rel g_env_buf]
    mov rcx, qword [rbx + SETLOCAL_SNAP64.cbSavedEnv]
    mov qword [rel g_env_used], rcx
    rep movsb

    mov rcx, qword [rbx + SETLOCAL_SNAP64.pSavedEnv]
    call perm_free64
    mov qword [rbx + SETLOCAL_SNAP64.pSavedEnv], 0

    mov rsi, qword [rbx + SETLOCAL_SNAP64.pSavedCwd]
    test rsi, rsi
    jz .rest_flags
    cmp byte [rsi], 0
    je .free_cwd
    mov rcx, rsi
    call hal_set_cwd64
.free_cwd:
    mov rcx, rsi
    call perm_free64
    mov qword [rbx + SETLOCAL_SNAP64.pSavedCwd], 0

.rest_flags:
    mov al, byte [rbx + SETLOCAL_SNAP64.bSavedExt]
    mov byte [rel g_extensions_on], al
    mov al, byte [rbx + SETLOCAL_SNAP64.bSavedDelayed]
    mov byte [rel g_delayed_exp], al

.done:
    @epilog_ret
@endproc

;; @proc env_build_unicode_block64
;; @abi win64_ms
;; @in none
;; @out RAX = UTF-16LE double-NUL environment block in statement arena for CreateProcessW, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Builds a CREATE_UNICODE_ENVIRONMENT block from g_env_buf so child processes see exact SET/ENDLOCAL state.
@proc_frame env_build_unicode_block64, 40, rbx, rsi, rdi, r12
    mov rcx, qword [rel g_env_used]
    lea rcx, [rcx + rcx + 32]
    call arena_alloc64
    jc .fail
    mov rbx, rax
    mov rdi, rax
    mov rsi, qword [rel g_env_buf]

.ent_lp:
    cmp byte [rsi], 0
    je .finish
    mov rcx, rsi
    call wtf8_to_utf16le64
    jc .fail
    mov r12, rsi
    mov rsi, rax
    lea rcx, [rdx + 1]
    rep movsw
    mov rsi, r12
    mov rcx, rsi
    call str_len64
    lea rsi, [rsi + rax + 1]
    jmp .ent_lp

.finish:
    mov word [rdi], 0
    mov word [rdi + 2], 0
    mov rax, rbx
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

; ============================================================================
; 11-Tier Precedence 32-Bit Signed SET /A Evaluator (arith_eval64)
; ============================================================================

;; @proc sa64_skip_ws
;; @abi win64_ms
;; @in RCX = expression cursor
;; @out RAX = updated cursor past whitespace/quotes
;; @err none
;; @clobbers rax, rdx
;; @desc Advances expression cursor past spaces, tabs, and enclosing double quotes.
@proc sa64_skip_ws
    mov rax, rcx
.lp:
    mov dl, byte [rax]
    cmp dl, 0x20
    je .adv
    cmp dl, 0x09
    je .adv
    cmp dl, '"'
    je .adv
    ret
.adv:
    inc rax
    jmp .lp
@endproc

;; @proc sa64_is_ident_start
;; @abi win64_ms
;; @in AL = byte
;; @out CF=0 if identifier start ('A'..'Z', 'a'..'z', '_', 0x80+), CF=1 otherwise
;; @err CF=1 if not identifier start
;; @clobbers none
;; @desc Tests whether AL begins a bare SET /A variable identifier.
@proc sa64_is_ident_start
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

;; @proc sa64_parse_i32_str
;; @abi win64_ms
;; @in RCX = string cursor
;; @out EAX = 32-bit value, RDX = updated cursor, CF=0; or CF=1, EAX=ERR_INVALID_NUMBER
;; @err CF=1, EAX=ERR_INVALID_NUMBER (e.g. invalid octal 08 / 09)
;; @clobbers rax, rcx, rdx, r8, r9
;; @desc Parses decimal, 0x hex, 0 octal (rejecting 08/09!), or 'c' character literal.
@proc sa64_parse_i32_str
    mov r8, rcx
    cmp byte [r8], 0x27
    jne .chk_hex
    movzx eax, byte [r8 + 1]
    test eax, eax
    jz .bad_num
    add r8, 2
    cmp byte [r8], 0x27
    jne .ret_ok
    inc r8
    jmp .ret_ok

.chk_hex:
    cmp byte [r8], '0'
    jne .dec
    mov al, byte [r8 + 1]
    cmp al, 'x'
    je .hex
    cmp al, 'X'
    je .hex
    cmp al, '0'
    jb .single_zero
    cmp al, '9'
    ja .single_zero

    inc r8
    xor eax, eax
.oct_lp:
    mov cl, byte [r8]
    cmp cl, '0'
    jb .ret_ok
    cmp cl, '7'
    ja .chk_oct_bad
    sub cl, '0'
    movzx ecx, cl
    shl eax, 3
    add eax, ecx
    inc r8
    jmp .oct_lp
.chk_oct_bad:
    cmp cl, '9'
    jbe .bad_num
    jmp .ret_ok

.single_zero:
    xor eax, eax
    inc r8
    jmp .ret_ok

.hex:
    add r8, 2
    xor eax, eax
    xor r9d, r9d
.hex_lp:
    mov cl, byte [r8]
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
    inc r8
    inc r9d
    jmp .hex_lp
.hex_end:
    test r9d, r9d
    jz .bad_num
    jmp .ret_ok

.dec:
    xor eax, eax
    xor r9d, r9d
.dec_lp:
    mov cl, byte [r8]
    cmp cl, '0'
    jb .dec_end
    cmp cl, '9'
    ja .dec_end
    sub cl, '0'
    movzx ecx, cl
    imul eax, eax, 10
    add eax, ecx
    inc r8
    inc r9d
    jmp .dec_lp
.dec_end:
    test r9d, r9d
    jz .bad_num

.ret_ok:
    mov rdx, r8
    clc
    ret

.bad_num:
    mov eax, ERR_INVALID_NUMBER
    stc
    ret
@endproc

;; @proc sa64_read_var_i32
;; @abi win64_ms
;; @in RCX = variable name
;; @out EAX = signed 32-bit value (0 if undefined or non-numeric)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads an environment variable for SET /A, treating undefined variables as 0.
@proc_frame sa64_read_var_i32, 32, rbx
    call env_get64
    test rax, rax
    jz .zero
    mov rcx, rax
    call sa64_skip_ws
    xor ebx, ebx
    cmp byte [rax], '-'
    jne .chk_p
    mov ebx, 1
    inc rax
    jmp .parse
.chk_p:
    cmp byte [rax], '+'
    jne .parse
    inc rax
.parse:
    mov rcx, rax
    call sa64_parse_i32_str
    jc .zero
    test ebx, ebx
    jz .done
    neg eax
    @epilog_ret
.zero:
    xor eax, eax
.done:
    @epilog_ret
@endproc

;; @proc sa64_eval_primary
;; @abi win64_ms
;; @in [g_sa_ptr] = 64-bit cursor
;; @out EAX = 32-bit signed value, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses SET /A parenthesized subexpressions, unary + - ! ~, numeric/char literals, and bare variable assignments.
@proc_frame sa64_eval_primary, 64, rbx, rsi, rdi, r12, r13
    mov rcx, qword [rel g_sa_ptr]
    call sa64_skip_ws
    mov rsi, rax
    mov qword [rel g_sa_ptr], rsi

    mov al, byte [rsi]
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
    call sa64_is_ident_start
    jc .missing_op

    mov rbx, rsi
.id_lp:
    mov al, byte [rbx]
    call sa64_is_ident_start
    jnc .id_adv
    cmp al, '0'
    jb .id_end
    cmp al, '9'
    ja .id_end
.id_adv:
    inc rbx
    jmp .id_lp
.id_end:
    mov r12, rbx
    sub r12, rsi
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .err_out
    mov rdi, rax
    mov r13, rax
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rdi, r13 ; varName

    mov rcx, rbx
    call sa64_skip_ws
    mov rsi, rax
    mov qword [rel g_sa_ptr], rsi

    xor r12d, r12d
    mov al, byte [rsi]
    mov dl, byte [rsi + 1]
    cmp al, '='
    jne .chk_comp_asgn
    cmp dl, '='
    je .just_read_var
    mov r12d, 1
    inc rsi
    jmp .do_assign

.chk_comp_asgn:
    cmp dl, '='
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
    mov r12d, 2
    add rsi, 2
    jmp .do_assign
.asgn_sub:
    mov r12d, 3
    add rsi, 2
    jmp .do_assign
.asgn_mul:
    mov r12d, 4
    add rsi, 2
    jmp .do_assign
.asgn_div:
    mov r12d, 5
    add rsi, 2
    jmp .do_assign
.asgn_mod:
    mov r12d, 6
    add rsi, 2
    jmp .do_assign
.asgn_and:
    mov r12d, 7
    add rsi, 2
    jmp .do_assign
.asgn_xor:
    mov r12d, 8
    add rsi, 2
    jmp .do_assign
.asgn_or:
    mov r12d, 9
    add rsi, 2
    jmp .do_assign

.chk_shift_asgn:
    cmp byte [rsi + 2], '='
    jne .just_read_var
    cmp al, '<'
    jne .chk_shr_asgn
    cmp dl, '<'
    jne .just_read_var
    mov r12d, 10
    add rsi, 3
    jmp .do_assign
.chk_shr_asgn:
    cmp al, '>'
    jne .just_read_var
    cmp dl, '>'
    jne .just_read_var
    mov r12d, 11
    add rsi, 3

.do_assign:
    mov qword [rel g_sa_ptr], rsi
    mov ecx, 1
    call sa64_eval_prec
    jc .err_out
    mov ebx, eax ; rhs

    cmp r12d, 1
    je .store_var

    mov rcx, rdi
    call sa64_read_var_i32
    ; EAX = lhs, EBX = rhs
    cmp r12d, 2
    je .op_add
    cmp r12d, 3
    je .op_sub
    cmp r12d, 4
    je .op_mul
    cmp r12d, 5
    je .op_div
    cmp r12d, 6
    je .op_mod
    cmp r12d, 7
    je .op_and
    cmp r12d, 8
    je .op_xor
    cmp r12d, 9
    je .op_or
    cmp r12d, 10
    je .op_shl
    mov ecx, ebx
    and ecx, 31
    sar eax, cl
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
    mov ecx, eax
    mov edx, ebx
    call sdiv32_safe64
    jc .err_out
    mov ebx, eax
    jmp .store_var
.op_mod:
    mov ecx, eax
    mov edx, ebx
    call sdiv32_safe64
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
    and ecx, 31
    shl eax, cl
    mov ebx, eax

.store_var:
    mov ecx, ebx
    lea rdx, [rsp + 32]
    call fmt_i32_dec64
    mov rcx, rdi
    lea rdx, [rsp + 32]
    call env_set64
    mov eax, ebx
    clc
    @epilog_ret

.just_read_var:
    mov rcx, rdi
    call sa64_read_var_i32
    clc
    @epilog_ret

.parens:
    inc rsi
    mov qword [rel g_sa_ptr], rsi
    xor ecx, ecx
    call sa64_eval_prec
    jc .err_out
    mov ebx, eax
    mov rcx, qword [rel g_sa_ptr]
    call sa64_skip_ws
    cmp byte [rax], ')'
    jne .unbal_paren
    inc rax
    mov qword [rel g_sa_ptr], rax
    mov eax, ebx
    clc
    @epilog_ret

.un_plus:
    inc rsi
    mov qword [rel g_sa_ptr], rsi
    call sa64_eval_primary
    @epilog_ret

.un_minus:
    inc rsi
    mov qword [rel g_sa_ptr], rsi
    call sa64_eval_primary
    jc .err_out
    neg eax
    clc
    @epilog_ret

.un_not:
    inc rsi
    mov qword [rel g_sa_ptr], rsi
    call sa64_eval_primary
    jc .err_out
    xor ecx, ecx
    test eax, eax
    setz cl
    mov eax, ecx
    clc
    @epilog_ret

.un_bitnot:
    inc rsi
    mov qword [rel g_sa_ptr], rsi
    call sa64_eval_primary
    jc .err_out
    not eax
    clc
    @epilog_ret

.number:
    mov rcx, rsi
    call sa64_parse_i32_str
    jc .err_out
    mov qword [rel g_sa_ptr], rdx
    clc
    @epilog_ret

.unbal_paren:
    mov eax, ERR_SYNTAX
    stc
    @epilog_ret

.missing_op:
    mov eax, ERR_MISSING_OPERAND
    stc
.err_out:
    @epilog_ret
@endproc

;; @proc sa64_peek_binop
;; @abi win64_ms
;; @in RCX = cursor
;; @out EAX = precedence (0=comma, 2=|, 3=^, 4=&, 5=shift, 6=add/sub, 7=mul/div/mod, -1=none), DL = opid, R8D = token len
;; @err none
;; @clobbers rax, rdx, r8, r9
;; @desc Classifies binary SET /A operators and returns Pratt precedence tier and token width.
@proc sa64_peek_binop
    mov al, byte [rcx]
    mov r9b, byte [rcx + 1]
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
    mov r8d, 1
    ret
.mul:
    cmp r9b, '='
    je .none
    mov eax, 7
    mov dl, '*'
    mov r8d, 1
    ret
.div:
    cmp r9b, '='
    je .none
    mov eax, 7
    mov dl, '/'
    mov r8d, 1
    ret
.mod:
    cmp r9b, '='
    je .none
    mov eax, 7
    mov dl, '%'
    mov r8d, 1
    ret
.add:
    cmp r9b, '='
    je .none
    mov eax, 6
    mov dl, '+'
    mov r8d, 1
    ret
.sub:
    cmp r9b, '='
    je .none
    mov eax, 6
    mov dl, '-'
    mov r8d, 1
    ret
.chk_shl:
    cmp r9b, '<'
    jne .none
    cmp byte [rcx + 2], '='
    je .none
    mov eax, 5
    mov dl, '<'
    mov r8d, 2
    ret
.chk_shr:
    cmp r9b, '>'
    jne .none
    cmp byte [rcx + 2], '='
    je .none
    mov eax, 5
    mov dl, '>'
    mov r8d, 2
    ret
.bit_and:
    cmp r9b, '='
    je .none
    mov eax, 4
    mov dl, '&'
    mov r8d, 1
    ret
.bit_xor:
    cmp r9b, '='
    je .none
    mov eax, 3
    mov dl, '^'
    mov r8d, 1
    ret
.bit_or:
    cmp r9b, '='
    je .none
    mov eax, 2
    mov dl, '|'
    mov r8d, 1
    ret
.none:
    mov eax, -1
    xor edx, edx
    xor r8d, r8d
    ret
@endproc

;; @proc sa64_eval_prec
;; @abi win64_ms
;; @in ECX = minimum precedence tier, [g_sa_ptr] = cursor
;; @out EAX = evaluated 32-bit signed result, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Pratt precedence-climbing evaluator across all 11 SET /A operator tiers.
@proc_frame sa64_eval_prec, 40, rbp, rbx, rsi, rdi
    mov ebp, ecx
    call sa64_eval_primary
    jc .fail
    mov ebx, eax

.bin_lp:
    mov rcx, qword [rel g_sa_ptr]
    call sa64_skip_ws
    mov rsi, rax
    mov qword [rel g_sa_ptr], rsi

    mov rcx, rsi
    call sa64_peek_binop
    cmp eax, -1
    je .ok
    cmp eax, ebp
    jl .ok

    add rsi, r8
    mov qword [rel g_sa_ptr], rsi
    movzx edi, dl
    lea ecx, [eax + 1]
    call sa64_eval_prec
    jc .fail
    mov ecx, eax

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
    mov edx, ecx
    mov ecx, ebx
    call sdiv32_safe64
    jc .fail
    mov ebx, eax
    jmp .bin_lp
.do_mod:
    mov edx, ecx
    mov ecx, ebx
    call sdiv32_safe64
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
    and ecx, 31
    shl ebx, cl
    jmp .bin_lp
.do_shr:
    and ecx, 31
    sar ebx, cl
    jmp .bin_lp
.do_and:
    and ebx, ecx
    jmp .bin_lp
.do_xor:
    xor ebx, ecx
    jmp .bin_lp

.ok:
    mov eax, ebx
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc arith_eval64
;; @abi win64_ms
;; @in RCX = NUL-terminated SET /A expression
;; @out EAX = signed 32-bit result, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_DIV_BY_ZERO / ERR_INVALID_NUMBER / ERR_MISSING_OPERAND / ERR_SYNTAX
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Top-level entry point for x86-64 SET /A expression evaluation.
@proc_frame arith_eval64, 32, rbx
    mov qword [rel g_sa_ptr], rcx
    xor ecx, ecx
    call sa64_eval_prec
    jc .fail
    mov ebx, eax
    mov rcx, qword [rel g_sa_ptr]
    call sa64_skip_ws
    cmp byte [rax], 0
    jne .bad_tail
    mov eax, ebx
    clc
    @epilog_ret
.bad_tail:
    mov eax, ERR_SYNTAX
.fail:
    stc
    @epilog_ret
@endproc
