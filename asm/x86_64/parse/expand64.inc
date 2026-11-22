;; @file expand64.inc
;; @isa x64
;; @desc 5-Phase Expander Phases 1 (%, %*, %~0..9, %VAR:a=b%, %VAR:~s,l%), 3 (FOR %%I), and 4 (!VAR! + caret strip) for x86-64.

@cpu x64
@format inc

;; @proc exp_parse_signed_int64
;; @abi win64_ms
;; @in RCX = ASCII signed integer cursor
;; @out EAX = signed 32-bit value, RDX = updated cursor
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses an optional +/- sign and decimal/hex/octal integer for %VAR:~s,l% slicing.
@proc_frame exp_parse_signed_int64, 40, rbx, rsi
    mov rsi, rcx
    xor ebx, ebx
    cmp byte [rcx], '-'
    jne .chk_pl
    mov ebx, 1
    inc rcx
    jmp .go
.chk_pl:
    cmp byte [rcx], '+'
    jne .go
    inc rcx
.go:
    call sa64_parse_i32_str
    jc .zero
    test ebx, ebx
    jz .done
    neg eax
    @epilog_ret
.zero:
    xor eax, eax
    mov rdx, rsi
.done:
    @epilog_ret
@endproc

;; @proc exp_apply_var_expr64
;; @abi win64_ms
;; @in RCX = "NAME" or "NAME:old=new" or "NAME:*old=new" or "NAME:~s[,l]"
;; @out RAX = expanded string in arena (or "" if undefined), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Evaluates %VAR%, %VAR:~start[,len]%, %VAR:old=new%, and %VAR:*old=new% in the 64-bit statement arena.
@proc_frame exp_apply_var_expr64, 40, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    mov rsi, rcx
    mov rbx, rcx
    cmp byte [rbx], '='
    jne .scan_colon
    inc rbx
.scan_colon:
    mov al, byte [rbx]
    test al, al
    jz .plain_var
    cmp al, ':'
    je .has_colon
    inc rbx
    jmp .scan_colon

.plain_var:
    mov rcx, rsi
    call env_get64
    test rax, rax
    jnz .ok
    lea rax, [rel szEmpty64]
    jmp .ok

.has_colon:
    mov r12, rbx
    sub r12, rsi
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r13, rax
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rcx, r13
    call env_get64
    test rax, rax
    jnz .got_base_val
    lea rax, [rel szEmpty64]
    jmp .ok

.got_base_val:
    mov rbp, rax ; base value
    inc rbx      ; past ':'
    cmp byte [rbx], '~'
    je .do_substr

    xor r14d, r14d ; is_star
    cmp byte [rbx], '*'
    jne .find_eq
    mov r14d, 1
    inc rbx
.find_eq:
    mov rsi, rbx
.eq_lp:
    mov al, byte [rsi]
    test al, al
    jz .ret_base
    cmp al, '='
    je .got_eq
    inc rsi
    jmp .eq_lp

.ret_base:
    mov rax, rbp
    jmp .ok

.got_eq:
    mov r12, rsi
    sub r12, rbx
    test r12, r12
    jz .ret_base
    mov r15, rsi ; '=' ptr
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r13, rax ; old_pat
    mov rsi, rbx
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    lea rbx, [r15 + 1] ; new_rep

    test r14d, r14d
    jnz .star_rep

    mov ecx, W64_LINE_MAX
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r15, rax ; result start

.rep_scan:
    cmp byte [rbp], 0
    je .rep_done
    xor r8d, r8d
.cmp_sub:
    cmp r8, r12
    jae .sub_matched
    mov al, byte [r8 + rbp]
    test al, al
    jz .sub_nomatch
    call ascii_upper_al64
    mov r9b, al
    mov al, byte [r13 + r8]
    call ascii_upper_al64
    cmp r9b, al
    jne .sub_nomatch
    inc r8
    jmp .cmp_sub

.sub_matched:
    add rbp, r12
    mov rcx, rdi
    mov rdx, rbx
    call str_copy64
    mov rdi, rax
    jmp .rep_scan

.sub_nomatch:
    mov al, byte [rbp]
    mov byte [rdi], al
    inc rbp
    inc rdi
    jmp .rep_scan

.rep_done:
    mov byte [rdi], 0
    mov rax, r15
    jmp .ok

.star_rep:
    mov rsi, rbp
.st_scan:
    cmp byte [rsi], 0
    je .ret_base
    xor r8d, r8d
.st_cmp:
    cmp r8, r12
    jae .st_hit
    mov al, byte [rsi + r8]
    test al, al
    jz .ret_base
    call ascii_upper_al64
    mov r9b, al
    mov al, byte [r13 + r8]
    call ascii_upper_al64
    cmp r9b, al
    jne .st_adv
    inc r8
    jmp .st_cmp
.st_adv:
    inc rsi
    jmp .st_scan
.st_hit:
    add rsi, r12
    mov ecx, W64_LINE_MAX
    call arena_alloc64
    jc .fail
    mov r15, rax
    mov rcx, rax
    mov rdx, rbx
    call str_copy64
    mov rcx, rax
    mov rdx, rsi
    call str_copy64
    mov rax, r15
    jmp .ok

.do_substr:
    inc rbx
    mov rcx, rbp
    call str_len64
    mov r12, rax ; total_len (64-bit)

    mov rcx, rbx
    call exp_parse_signed_int64
    mov rbx, rdx
    movsxd r13, eax ; start_idx
    test r13, r13
    jns .st_nonneg
    add r13, r12
    jns .st_nonneg
    xor r13d, r13d
.st_nonneg:
    cmp r13, r12
    jae .empty_res
    mov r14, r12
    sub r14, r13 ; remaining len

    cmp byte [rbx], ','
    jne .slice_copy
    lea rcx, [rbx + 1]
    call exp_parse_signed_int64
    movsxd rax, eax
    test rax, rax
    jns .len_pos
    add rax, r14
    js .empty_res
.len_pos:
    cmp rax, r14
    jae .slice_copy
    mov r14, rax

.slice_copy:
    lea rcx, [r14 + 4]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r15, rax
    lea rsi, [r13 + rbp]
    mov rcx, r14
    rep movsb
    mov byte [rdi], 0
    mov rax, r15
    jmp .ok

.empty_res:
    lea rax, [rel szEmpty64]
.ok:
    clc
    @epilog_ret
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc exp_format_tilde64
;; @abi win64_ms
;; @in RCX = raw parameter/variable path value, EDX = modifier bitmask (bit0=f,1=d,2=p,3=n,4=x,5=s,6=a,7=t,8=z)
;; @out RAX = formatted result in statement arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements %~0..9 and %%~I path/attribute/timestamp/64-bit-size modifiers.
@proc_frame exp_format_tilde64, 72, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 72 = 40 + 2*16
    mov ebp, edx
    call arena_strdup64
    jc .fail
    mov rbx, rax
    cmp byte [rbx], '"'
    jne .unq_done
    inc rbx
    mov rcx, rbx
    call str_len64
    test rax, rax
    jz .unq_done
    cmp byte [rbx + rax - 1], '"'
    jne .unq_done
    mov byte [rbx + rax - 1], 0
.unq_done:
    test ebp, ebp
    jnz .has_mods
    mov rax, rbx
    jmp .ok

.has_mods:
    mov rcx, rbx
    test ebp, 0x20
    jz .resolve_full
    call hal_shortpath64
    jnc .got_full
    mov rcx, rbx
.resolve_full:
    call path_normalize64
    jnc .got_full
    mov rax, rbx
.got_full:
    mov rbx, rax ; canonical path

    mov ecx, 4096
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r15, rax ; output start
    mov byte [rdi], 0

    test ebp, 0x40 ; 'a'
    jz .chk_tz
    mov rcx, rbx
    call hal_get_attr64
    jc .chk_tz
    mov edx, eax
    mov byte [rdi], '-'
    test edx, FILE_ATTRIBUTE_DIRECTORY
    jz .a_r
    mov byte [rdi], 'd'
.a_r:
    mov byte [rdi + 1], '-'
    test edx, FILE_ATTRIBUTE_READONLY
    jz .a_a
    mov byte [rdi + 1], 'r'
.a_a:
    mov byte [rdi + 2], '-'
    test edx, FILE_ATTRIBUTE_ARCHIVE
    jz .a_h
    mov byte [rdi + 2], 'a'
.a_h:
    mov byte [rdi + 3], '-'
    test edx, FILE_ATTRIBUTE_HIDDEN
    jz .a_s
    mov byte [rdi + 3], 'h'
.a_s:
    mov byte [rdi + 4], '-'
    test edx, FILE_ATTRIBUTE_SYSTEM
    jz .a_pad
    mov byte [rdi + 4], 's'
.a_pad:
    mov dword [rdi + 5], 0x202D2D2D ; "--- "
    add rdi, 9
    mov byte [rdi], 0

.chk_tz:
    test ebp, 0x180
    jz .chk_path_parts
    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .fail
    mov r12, rax
    mov rcx, rbx
    mov rdx, r12
    call hal_find_first64
    jc .chk_path_parts
    mov rcx, rax
    call qword [rel __imp_FindClose]

    test ebp, 0x80 ; 't'
    jz .chk_z
    lea rcx, [r12 + FIND_ENTRY64.ftWriteLo]
    lea rdx, [rsp + 56] ; local FILETIME (8B)
    call qword [rel __imp_FileTimeToLocalFileTime]
    lea rcx, [rsp + 56]
    lea rdx, [rsp + 32] ; SYSTEMTIME16 (16B)
    call qword [rel __imp_FileTimeToSystemTime]
    movzx ecx, word [rsp + 32 + SYSTEMTIME16.wYear]
    mov rdx, rdi
    call fmt_u64_plain64
    add rdi, rax
    mov byte [rdi], '-'
    inc rdi
    movzx ecx, word [rsp + 32 + SYSTEMTIME16.wMonth]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], '-'
    inc rdi
    movzx ecx, word [rsp + 32 + SYSTEMTIME16.wDay]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], ' '
    inc rdi
    movzx ecx, word [rsp + 32 + SYSTEMTIME16.wHour]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], ':'
    inc rdi
    movzx ecx, word [rsp + 32 + SYSTEMTIME16.wMinute]
    mov dl, '0'
    mov r8, rdi
    call fmt_u32_pad2_64
    mov rdi, rax
    mov byte [rdi], ' '
    inc rdi
    mov byte [rdi], 0

.chk_z:
    test ebp, 0x100 ; 'z'
    jz .chk_path_parts
    mov eax, dword [r12 + FIND_ENTRY64.nSizeHi]
    shl rax, 32
    mov ecx, dword [r12 + FIND_ENTRY64.nSizeLo]
    or rcx, rax
    mov rdx, rdi
    call fmt_u64_plain64
    add rdi, rax
    mov byte [rdi], ' '
    inc rdi
    mov byte [rdi], 0

.chk_path_parts:
    test ebp, 0x01
    jnz .emit_full
    cmp ebp, 0x20
    je .emit_full

    test ebp, 0x1E ; d|p|n|x
    jz .finish_trim

    mov rsi, rbx
    xor r13d, r13d ; last_slash ptr
    xor r14d, r14d ; last_dot ptr
.sp_scan:
    mov al, byte [rsi]
    test al, al
    jz .sp_done
    cmp al, 0x5C
    jne .sp_dot
    mov r13, rsi
    xor r14d, r14d
    jmp .sp_nx
.sp_dot:
    cmp al, '.'
    jne .sp_nx
    mov r14, rsi
.sp_nx:
    inc rsi
    jmp .sp_scan
.sp_done:
    test ebp, 0x02 ; 'd'
    jz .part_p
    cmp byte [rbx + 1], ':'
    jne .part_p
    mov al, byte [rbx]
    mov byte [rdi], al
    mov byte [rdi + 1], ':'
    add rdi, 2
    mov byte [rdi], 0

.part_p:
    test ebp, 0x04 ; 'p'
    jz .part_n
    test r13, r13
    jz .part_n
    mov rsi, rbx
    cmp byte [rsi + 1], ':'
    jne .cp_p_lp
    add rsi, 2
.cp_p_lp:
    cmp rsi, r13
    ja .part_n
    mov al, byte [rsi]
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .cp_p_lp

.part_n:
    test ebp, 0x08 ; 'n'
    jz .part_x
    mov rsi, rbx
    test r13, r13
    jz .cp_n_lp
    lea rsi, [r13 + 1]
.cp_n_lp:
    mov al, byte [rsi]
    test al, al
    jz .part_x
    cmp rsi, r14
    je .part_x
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .cp_n_lp

.part_x:
    test ebp, 0x10 ; 'x'
    jz .finish_nul
    test r14, r14
    jz .finish_nul
    mov rcx, rdi
    mov rdx, r14
    call str_copy64
    mov rdi, rax
    jmp .finish_trim

.emit_full:
    mov rcx, rdi
    mov rdx, rbx
    call str_copy64
    mov rdi, rax

.finish_nul:
    mov byte [rdi], 0
.finish_trim:
    mov rcx, r15
    call str_len64
    test rax, rax
    jz .ret_out
    test ebp, 0x3F
    jnz .ret_out
    cmp byte [r15 + rax - 1], ' '
    jne .ret_out
    mov byte [r15 + rax - 1], 0
.ret_out:
    mov rax, r15
.ok:
    clc
    @epilog_ret
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc exp_parse_tilde_mask64
;; @abi win64_ms
;; @in RCX = cursor right after '~'
;; @out RAX = cursor at parameter digit / FOR var char, EDX = modifier bitmask, CF=0; or CF=1 if invalid
;; @err CF=1 when no valid target char follows modifiers
;; @clobbers rax, rcx, rdx
;; @desc Parses f,d,p,n,x,s,a,t,z modifier letters after %~ or %%~.
@proc exp_parse_tilde_mask64
    xor edx, edx
.lp:
    mov al, byte [rcx]
    test al, al
    jz .bad
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 32
.up:
    cmp al, 'F'
    je .m_f
    cmp al, 'D'
    je .m_d
    cmp al, 'P'
    je .m_p
    cmp al, 'N'
    je .m_n
    cmp al, 'X'
    je .m_x
    cmp al, 'S'
    je .m_s
    cmp al, 'A'
    je .m_a
    cmp al, 'T'
    je .m_t
    cmp al, 'Z'
    je .m_z
    mov rax, rcx
    clc
    ret
.m_f:
    or edx, 0x001
    inc rcx
    jmp .lp
.m_d:
    or edx, 0x002
    inc rcx
    jmp .lp
.m_p:
    or edx, 0x004
    inc rcx
    jmp .lp
.m_n:
    or edx, 0x008
    inc rcx
    jmp .lp
.m_x:
    or edx, 0x010
    inc rcx
    jmp .lp
.m_s:
    or edx, 0x020
    inc rcx
    jmp .lp
.m_a:
    or edx, 0x040
    inc rcx
    jmp .lp
.m_t:
    or edx, 0x080
    inc rcx
    jmp .lp
.m_z:
    or edx, 0x100
    inc rcx
    jmp .lp
.bad:
    stc
    ret
@endproc

;; @proc batch_get_star64
;; @abi win64_ms
;; @in none (reads top of g_batch_stack)
;; @out RAX = raw argument tail (%*) or ""
;; @err none
;; @clobbers rax, rcx, rdx
;; @desc Returns raw %* argument tail of the active 64-bit batch frame.
@proc batch_get_star64
    mov ecx, dword [rel g_batch_depth]
    test ecx, ecx
    jz .empty
    dec ecx
    imul rcx, rcx, BATCH_FRAME64_SIZE
    lea rdx, [rel g_batch_stack]
    mov rax, qword [rdx + rcx + BATCH_FRAME64.pArgsRaw]
    test rax, rax
    jz .empty
    ret
.empty:
    lea rax, [rel szEmpty64]
    ret
@endproc

;; @proc batch_get_arg64
;; @abi win64_ms
;; @in ECX = requested parameter index (0..9)
;; @out RAX = parameter value string (or "" if out of range)
;; @err none
;; @clobbers rax, rcx, rdx, r8
;; @desc Returns %0..%9 from active BATCH_FRAME64 accounting for SHIFT [/n] offset.
@proc batch_get_arg64
    mov eax, ecx
    mov r8d, dword [rel g_batch_depth]
    test r8d, r8d
    jz .empty
    dec r8d
    imul r8, r8, BATCH_FRAME64_SIZE
    lea rdx, [rel g_batch_stack]
    add rdx, r8
    test eax, eax
    jz .get_slot
    movzx ecx, byte [rdx + BATCH_FRAME64.bShiftOff]
    add eax, ecx
    cmp eax, 9
    ja .empty
.get_slot:
    mov rax, qword [rdx + BATCH_FRAME64.argvPtrs + rax * 8]
    test rax, rax
    jz .empty
    ret
.empty:
    lea rax, [rel szEmpty64]
    ret
@endproc

;; @proc for_lookup_var64
;; @abi win64_ms
;; @in CL = case-sensitive FOR variable character (e.g. 'I', 'a')
;; @out RAX = current bound 64-bit value pointer, or 0 if unbound
;; @err none
;; @clobbers rax, rdx, r8
;; @desc Searches active FOR scope stack from innermost to outermost for variable CL.
@proc for_lookup_var64
    mov edx, dword [rel g_for_depth]
    lea r8, [rel g_for_vars]
.lp:
    test edx, edx
    jz .none
    dec edx
    cmp byte [r8 + rdx], cl
    je .hit
    jmp .lp
.hit:
    lea r8, [rel g_for_vals]
    mov rax, qword [r8 + rdx * 8]
    ret
.none:
    xor eax, eax
    ret
@endproc

;; @proc expand_phase1_64
;; @abi win64_ms
;; @in RCX = raw logical line, EDX = bInBatch (1 inside .bat/.cmd, 0 interactive / /C)
;; @out RAX = Phase-1-expanded string in statement arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Expands %%, %*, %0..%9, %~[mods]0..9, and %VAR...% while preserving %%I for Phase 3.
@proc_frame expand_phase1_64, 40, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    mov rsi, rcx
    mov ebp, edx ; bInBatch

    mov ecx, W64_LINE_MAX
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r15, rax ; output base

.scan:
    mov al, byte [rsi]
    test al, al
    jz .done
    cmp al, '%'
    je .on_pct
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .scan

.on_pct:
    mov al, byte [rsi + 1]
    test al, al
    jz .lit_pct

    cmp al, '%'
    jne .chk_batch_params
    test ebp, ebp
    jz .interactive_dbl_pct
    mov byte [rdi], '%'
    add rsi, 2
    inc rdi
    jmp .scan
.interactive_dbl_pct:
    mov byte [rdi], '%'
    mov byte [rdi + 1], '%'
    add rsi, 2
    add rdi, 2
    jmp .scan

.chk_batch_params:
    test ebp, ebp
    jz .chk_env_var

    cmp al, '*'
    jne .chk_tilde_param
    add rsi, 2
    call batch_get_star64
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    jmp .scan

.chk_tilde_param:
    cmp al, '~'
    jne .chk_digit_param
    cmp byte [rel g_extensions_on], 0
    je .chk_digit_param
    lea rcx, [rsi + 2]
    call exp_parse_tilde_mask64
    jc .chk_digit_param
    movzx ecx, byte [rax]
    cmp cl, '0'
    jb .chk_digit_param
    cmp cl, '9'
    ja .chk_digit_param
    mov rbx, rax ; ptr to digit
    mov r12d, edx ; mask
    sub ecx, '0'
    call batch_get_arg64
    mov rcx, rax
    mov edx, r12d
    call exp_format_tilde64
    jc .fail
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    lea rsi, [rbx + 1]
    jmp .scan

.chk_digit_param:
    movzx ecx, byte [rsi + 1]
    cmp cl, '0'
    jb .chk_env_var
    cmp cl, '9'
    ja .chk_env_var
    sub ecx, '0'
    add rsi, 2
    call batch_get_arg64
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    jmp .scan

.chk_env_var:
    lea rbx, [rsi + 1]
.find_close_pct:
    mov al, byte [rbx]
    test al, al
    jz .no_close_pct
    cmp al, '%'
    je .got_close_pct
    inc rbx
    jmp .find_close_pct

.no_close_pct:
    test ebp, ebp
    jz .lit_pct
    inc rsi
    jmp .scan

.got_close_pct:
    mov r12, rbx
    sub r12, rsi
    dec r12
    jz .lit_pct
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .fail
    mov r13, rax
    mov r14, rdi
    mov rdi, rax
    inc rsi
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rdi, r14
    mov rcx, r13
    call exp_apply_var_expr64
    jc .fail
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    lea rsi, [rbx + 1]
    jmp .scan

.lit_pct:
    mov byte [rdi], '%'
    inc rsi
    inc rdi
    jmp .scan

.done:
    mov byte [rdi], 0
    mov rax, r15
    clc
    @epilog_ret
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc expand_phase3_64
;; @abi win64_ms
;; @in RCX = token/command string
;; @out RAX = FOR-variable-expanded string in arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Substitutes active %V and %~[mods]V FOR loop variables (case-sensitively, matching cmd.exe!).
@proc_frame expand_phase3_64, 48, rbx, rsi, rdi, r12, r13
    cmp dword [rel g_for_depth], 0
    jne .active
    mov rax, rcx
    clc
    @epilog_ret

.active:
    mov rsi, rcx
    mov ecx, W64_LINE_MAX
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r13, rax

.lp:
    mov al, byte [rsi]
    test al, al
    jz .done
    cmp al, '%'
    je .chk_for
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.chk_for:
    lea rbx, [rsi + 1]
    cmp byte [rbx], '%'
    jne .at_body
    inc rbx
.at_body:
    mov cl, byte [rbx]
    test cl, cl
    jz .lit
    cmp cl, '~'
    je .try_tilde

    call for_lookup_var64
    test rax, rax
    jz .lit
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    lea rsi, [rbx + 1]
    jmp .lp

.try_tilde:
    inc rbx
    xor r12d, r12d ; mask
.t_lp:
    mov cl, byte [rbx]
    test cl, cl
    jz .lit
    mov cl, byte [rbx + 1]
    test cl, cl
    jz .try_cur_for
    call for_lookup_var64
    test rax, rax
    jnz .consume_mod_first

.try_cur_for:
    mov cl, byte [rbx]
    call for_lookup_var64
    test rax, rax
    jnz .got_tilde_for

.consume_mod_first:
    mov al, byte [rbx]
    call ascii_upper_al64
    cmp al, 'F'
    je .tf
    cmp al, 'D'
    je .td
    cmp al, 'P'
    je .tp
    cmp al, 'N'
    je .tn
    cmp al, 'X'
    je .tx
    cmp al, 'S'
    je .ts
    cmp al, 'A'
    je .ta
    cmp al, 'T'
    je .tt
    cmp al, 'Z'
    je .tz
    jmp .lit
.tf:
    or r12d, 0x001
    inc rbx
    jmp .t_lp
.td:
    or r12d, 0x002
    inc rbx
    jmp .t_lp
.tp:
    or r12d, 0x004
    inc rbx
    jmp .t_lp
.tn:
    or r12d, 0x008
    inc rbx
    jmp .t_lp
.tx:
    or r12d, 0x010
    inc rbx
    jmp .t_lp
.ts:
    or r12d, 0x020
    inc rbx
    jmp .t_lp
.ta:
    or r12d, 0x040
    inc rbx
    jmp .t_lp
.tt:
    or r12d, 0x080
    inc rbx
    jmp .t_lp
.tz:
    or r12d, 0x100
    inc rbx
    jmp .t_lp

.got_tilde_for:
    mov rcx, rax
    mov edx, r12d
    call exp_format_tilde64
    jc .fail
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    lea rsi, [rbx + 1]
    jmp .lp

.lit:
    mov al, byte [rsi]
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.done:
    mov byte [rdi], 0
    mov rax, r13
    clc
    @epilog_ret
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc expand_phase4_64
;; @abi win64_ms
;; @in RCX = Phase-3 string
;; @out RAX = Phase-4 delayed-expanded and caret-unquoted string in arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Performs Phase 4 !VAR! delayed expansion (when /V:ON) and unquoted ^ escape stripping.
@proc_frame expand_phase4_64, 40, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    mov rsi, rcx
    mov ecx, W64_LINE_MAX
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r15, rax
    xor ebp, ebp ; in_quotes (0/1)

.lp:
    mov al, byte [rsi]
    test al, al
    jz .done
    cmp al, '"'
    jne .chk_caret
    xor ebp, 1
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.chk_caret:
    cmp al, '^'
    jne .chk_bang
    test ebp, ebp
    jnz .chk_quoted_caret_bang
    mov al, byte [rsi + 1]
    test al, al
    jz .inc_done
    mov byte [rdi], al
    add rsi, 2
    inc rdi
    jmp .lp

.chk_quoted_caret_bang:
    cmp byte [rel g_delayed_exp], 0
    je .emit_raw
    cmp byte [rsi + 1], '!'
    jne .emit_raw
    mov byte [rdi], '!'
    add rsi, 2
    inc rdi
    jmp .lp

.chk_bang:
    cmp al, '!'
    jne .emit_raw
    cmp byte [rel g_delayed_exp], 0
    je .emit_raw

    lea rbx, [rsi + 1]
.find_bang:
    mov al, byte [rbx]
    test al, al
    jz .emit_raw
    cmp al, '!'
    je .got_bang
    inc rbx
    jmp .find_bang

.got_bang:
    mov r12, rbx
    sub r12, rsi
    dec r12
    jz .skip_empty_bang
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .fail
    mov r13, rax
    mov r14, rdi
    mov rdi, rax
    inc rsi
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rdi, r14
    mov rcx, r13
    call exp_apply_var_expr64
    jc .fail
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    lea rsi, [rbx + 1]
    jmp .lp

.skip_empty_bang:
    add rsi, 2
    jmp .lp

.emit_raw:
    mov al, byte [rsi]
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.inc_done:
    inc rsi
.done:
    mov byte [rdi], 0
    mov rax, r15
    clc
    @epilog_ret
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc
