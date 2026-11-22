;; @file exec_ast64.inc
;; @isa x64
;; @desc x86-64 AST Executor, FOR (/D, /R, /L, /F) Engine, Batch Frame/Label Runner, and PATHEXT/vm8086 Spawner.

@cpu x64
@format inc

;; @proc tok_strip_quotes64
;; @abi win64_ms
;; @in RCX = NUL-terminated string
;; @out RAX = copy in arena with outer/embedded double quotes stripped, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Strips surrounding/inline '"' characters from an argument token in the 64-bit statement arena.
@proc_frame tok_strip_quotes64, 40, rsi, rdi
    mov rsi, rcx
    call str_len64
    lea rcx, [rax + 1]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov rdx, rax
.lp:
    mov al, byte [rsi]
    test al, al
    jz .done
    cmp al, '"'
    je .skip
    mov byte [rdi], al
    inc rdi
.skip:
    inc rsi
    jmp .lp
.done:
    mov byte [rdi], 0
    mov rax, rdx
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc str_cmp_exact64
;; @abi win64_ms
;; @in RCX = string A, RDX = string B
;; @out EAX < 0 if A < B, 0 if A == B, > 0 if A > B (exact byte comparison)
;; @err none
;; @clobbers rax, rcx, rdx, r8
;; @desc Performs exact case-sensitive lexicographical comparison of two NUL-terminated strings.
@proc str_cmp_exact64
.lp:
    movzx eax, byte [rcx]
    movzx r8d, byte [rdx]
    sub eax, r8d
    jnz .done
    test r8d, r8d
    jz .done
    inc rcx
    inc rdx
    jmp .lp
.done:
    ret
@endproc

;; @proc exec_eval_if_cond64
;; @abi win64_ms
;; @in RCX = AST_NODE64* (AST_IF)
;; @out EAX = 1 if condition holds (after NOT), 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Evaluates IF [NOT] [/I] (ERRORLEVEL | EXIST | DEFINED | CMDEXTVERSION | == | EQU..GEQ).
@proc_frame exec_eval_if_cond64, 48, rbp, rbx, rsi, rdi, r12
    mov rbp, rcx

    mov rcx, qword [rbp + AST_NODE64.pStr1]
    test rcx, rcx
    jnz .exp1
    lea rcx, [rel szEmpty64]
.exp1:
    call expand_phase3_64
    jc .false_raw
    mov rcx, rax
    call expand_phase4_64
    jc .false_raw
    mov rbx, rax ; lhs

    xor edi, edi
    mov rcx, qword [rbp + AST_NODE64.pStr2]
    test rcx, rcx
    jz .dispatch_sub
    call expand_phase3_64
    jc .false_raw
    mov rcx, rax
    call expand_phase4_64
    jc .false_raw
    mov rdi, rax ; rhs

.dispatch_sub:
    movzx eax, byte [rbp + AST_NODE64.bSub]
    cmp al, IFCMP_ERRORLEVEL
    je .c_errlvl
    cmp al, IFCMP_EXIST
    je .c_exist
    cmp al, IFCMP_DEFINED
    je .c_defined
    cmp al, IFCMP_CMDEXTVER
    je .c_extver
    cmp al, IFCMP_STR_EQ
    je .c_eq_str

    mov rcx, rbx
    call tok_strip_quotes64
    jc .c_lex_cmp
    mov rsi, rax
    mov rcx, rax
    call exp_parse_signed_int64
    cmp byte [rdx], 0
    jne .c_lex_cmp
    cmp rdx, rsi
    je .c_lex_cmp
    mov r12d, eax ; lhs_num

    mov rcx, rdi
    call tok_strip_quotes64
    jc .c_lex_cmp
    mov rsi, rax
    mov rcx, rax
    call exp_parse_signed_int64
    cmp byte [rdx], 0
    jne .c_lex_cmp
    cmp rdx, rsi
    je .c_lex_cmp
    mov ecx, eax  ; rhs_num
    mov eax, r12d ; lhs_num
    jmp .cmp_i32_vals

.c_lex_cmp:
    mov rcx, rbx
    mov rdx, rdi
    test byte [rbp + AST_NODE64.bFlags], IFFLAG_NOCASE
    jnz .lex_i
    call str_cmp_exact64
    xor ecx, ecx
    jmp .cmp_i32_vals
.lex_i:
    call str_cmp_i64
    xor ecx, ecx

.cmp_i32_vals:
    mov dl, byte [rbp + AST_NODE64.bSub]
    cmp eax, ecx
    je .ord_eq
    jl .ord_lt
    cmp dl, IFCMP_NEQ
    je .true_raw
    cmp dl, IFCMP_GTR
    je .true_raw
    cmp dl, IFCMP_GEQ
    je .true_raw
    jmp .false_raw
.ord_eq:
    cmp dl, IFCMP_EQU
    je .true_raw
    cmp dl, IFCMP_LEQ
    je .true_raw
    cmp dl, IFCMP_GEQ
    je .true_raw
    jmp .false_raw
.ord_lt:
    cmp dl, IFCMP_NEQ
    je .true_raw
    cmp dl, IFCMP_LSS
    je .true_raw
    cmp dl, IFCMP_LEQ
    je .true_raw
    jmp .false_raw

.c_eq_str:
    mov rcx, rbx
    mov rdx, rdi
    test byte [rbp + AST_NODE64.bFlags], IFFLAG_NOCASE
    jnz .eq_icase
    call str_cmp_exact64
    test eax, eax
    jz .true_raw
    jmp .false_raw
.eq_icase:
    call str_eq_i64
    test eax, eax
    jnz .true_raw
    jmp .false_raw

.c_errlvl:
    mov rcx, rbx
    call tok_strip_quotes64
    jc .false_raw
    mov rcx, rax
    call exp_parse_signed_int64
    cmp dword [rel g_last_exit_code], eax
    jge .true_raw
    jmp .false_raw

.c_exist:
    mov rcx, rbx
    call tok_strip_quotes64
    jc .false_raw
    mov rsi, rax
    mov rcx, rsi
    call dev_match_name64
    test rax, rax
    jnz .true_raw
    mov rcx, rsi
    call hal_get_attr64
    jnc .true_raw
    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .false_raw
    mov rcx, rsi
    mov rdx, rax
    call hal_find_first64
    jc .false_raw
    mov rcx, rax
    call qword [rel __imp_FindClose]
    jmp .true_raw

.c_defined:
    mov rcx, rbx
    call tok_strip_quotes64
    jc .false_raw
    mov rcx, rax
    call env_get_raw64
    test rax, rax
    jnz .true_raw
    jmp .false_raw

.c_extver:
    mov rcx, rbx
    call tok_strip_quotes64
    jc .false_raw
    mov rcx, rax
    call exp_parse_signed_int64
    cmp eax, 2
    jle .true_raw
    jmp .false_raw

.true_raw:
    mov eax, 1
    jmp .apply_not
.false_raw:
    xor eax, eax
.apply_not:
    test byte [rbp + AST_NODE64.bFlags], IFFLAG_NOT
    jz .done
    xor eax, 1
.done:
    @epilog_ret
@endproc

;; @proc for_bind_run_one64
;; @abi win64_ms
;; @in CL = variable char, RDX = bound value string, R8 = body AST_NODE64*
;; @out Executes body AST with %CL bound to RDX
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Pushes one FOR variable binding onto g_for_vars/g_for_vals, runs body AST, and pops scope.
@proc_frame for_bind_run_one64, 40
    mov eax, dword [rel g_for_depth]
    cmp eax, W64_FOR_SCOPE_MAX
    jae .done
    lea r9, [rel g_for_vars]
    mov byte [r9 + rax], cl
    lea r9, [rel g_for_vals]
    mov qword [r9 + rax * 8], rdx
    inc eax
    mov dword [rel g_for_depth], eax

    mov rcx, r8
    call exec_ast_node64

    dec dword [rel g_for_depth]
.done:
    @epilog_ret
@endproc

;; @proc for_next_set_item64
;; @abi win64_ms
;; @in RCX = set cursor
;; @out RAX = item string in arena (or 0 when set ends), RDX = updated cursor
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads next space/tab/comma/semicolon-separated item from a FOR (...) set.
@proc_frame for_next_set_item64, 40, rbx, rsi, rdi, r12
    mov rsi, rcx

.skip_sep:
    mov al, byte [rsi]
    test al, al
    jz .end_set
    cmp al, 0x20
    je .adv_sep
    cmp al, 0x09
    je .adv_sep
    cmp al, ','
    je .adv_sep
    cmp al, ';'
    je .adv_sep
    cmp al, 0x0D
    je .adv_sep
    cmp al, 0x0A
    jne .tok_start
.adv_sep:
    inc rsi
    jmp .skip_sep

.tok_start:
    mov rbx, rsi
    xor ecx, ecx
.scan:
    mov al, byte [rbx]
    test al, al
    jz .got
    cmp al, '"'
    jne .chk_d
    xor ecx, 1
    inc rbx
    jmp .scan
.chk_d:
    test ecx, ecx
    jnz .adv_t
    cmp al, 0x20
    je .got
    cmp al, 0x09
    je .got
    cmp al, ','
    je .got
    cmp al, ';'
    je .got
    cmp al, 0x0D
    je .got
    cmp al, 0x0A
    je .got
.adv_t:
    inc rbx
    jmp .scan

.got:
    mov r12, rbx
    sub r12, rsi
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .end_set
    mov rdi, rax
    mov r8, rax
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rax, r8
    mov rdx, rbx
    @epilog_ret

.end_set:
    xor eax, eax
    mov rdx, rsi
    @epilog_ret
@endproc

;; @proc exec_for_plain_or_dirs64
;; @abi win64_ms
;; @in RCX = set string, RDX = body AST_NODE64*, R8B = var char, R9B = bDirsOnly (0=files, 1=/D dirs)
;; @out Iterates literal items and wildcard expansions
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Executes plain FOR and FOR /D over literal items and filesystem wildcard matches.
@proc_frame exec_for_plain_or_dirs64, 56, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 56 = 40 + 16
    mov rbp, rcx ; set cursor
    mov rdi, rdx ; body AST
    movzx r12d, r8b ; var char
    movzx r13d, r9b ; bDirsOnly

.item_lp:
    cmp byte [rel g_exit_flag], 0
    jne .done
    cmp byte [rel g_goto_pending], 0
    jne .done
    cmp dword [rel g_ctrl_break], 0
    jne .done

    mov rcx, rbp
    call for_next_set_item64
    mov rbp, rdx
    test rax, rax
    jz .done

    mov rcx, rax
    call tok_strip_quotes64
    jc .done
    mov rsi, rax

    xor ecx, ecx
.chk_wild:
    mov al, byte [rsi + rcx]
    test al, al
    jz .is_literal
    cmp al, '*'
    je .is_wild
    cmp al, '?'
    je .is_wild
    inc rcx
    jmp .chk_wild

.is_literal:
    mov cl, r12b
    mov rdx, rsi
    mov r8, rdi
    call for_bind_run_one64
    jmp .item_lp

.is_wild:
    mov rcx, rsi
    call str_len64
    mov rbx, rax
.find_psep:
    test rbx, rbx
    jz .no_prefix
    mov al, byte [rsi + rbx - 1]
    cmp al, 0x5C
    je .has_prefix
    cmp al, '/'
    je .has_prefix
    cmp al, ':'
    je .has_prefix
    dec rbx
    jmp .find_psep
.no_prefix:
    lea r14, [rel szEmpty64]
    jmp .got_prefix
.has_prefix:
    lea rcx, [rbx + 4]
    call arena_alloc64
    jc .done
    mov r14, rax
    mov r8, rsi
    mov r9, rdi
    mov rdi, rax
    mov rcx, rbx
    rep movsb
    mov byte [rdi], 0
    mov rsi, r8
    mov rdi, r9

.got_prefix:
    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .item_lp
    mov r15, rax ; FIND_ENTRY64
    mov rcx, rsi
    mov rdx, r15
    call hal_find_first64
    jc .item_lp
    mov rbx, rax ; hFind

.wild_lp:
    cmp byte [r15 + FIND_ENTRY64.szNameWtf8], '.'
    jne .chk_kind_w
    cmp byte [r15 + FIND_ENTRY64.szNameWtf8 + 1], 0
    je .wild_next
    cmp byte [r15 + FIND_ENTRY64.szNameWtf8 + 1], '.'
    jne .chk_kind_w
    cmp byte [r15 + FIND_ENTRY64.szNameWtf8 + 2], 0
    je .wild_next

.chk_kind_w:
    mov eax, dword [r15 + FIND_ENTRY64.dwAttr]
    test r13d, r13d
    jnz .want_dir
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jnz .wild_next
    jmp .emit_match
.want_dir:
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .wild_next

.emit_match:
    mov ecx, 2048
    call arena_alloc64
    jc .wild_close
    mov rsi, rax
    mov rcx, rax
    mov rdx, r14
    call str_copy64
    mov rcx, rax
    lea rdx, [r15 + FIND_ENTRY64.szNameWtf8]
    call str_copy64
    mov cl, r12b
    mov rdx, rsi
    mov r8, rdi
    call for_bind_run_one64

.wild_next:
    cmp byte [rel g_exit_flag], 0
    jne .wild_close
    cmp byte [rel g_goto_pending], 0
    jne .wild_close
    mov rcx, rbx
    mov rdx, r15
    call hal_find_next64
    jnc .wild_lp

.wild_close:
    mov rcx, rbx
    call qword [rel __imp_FindClose]
    jmp .item_lp

.done:
    @epilog_ret
@endproc

;; @proc exec_for_walk_r64
;; @abi win64_ms
;; @in RCX = current root dir, RDX = raw set string, R8 = body AST_NODE64*, R9B = var char
;; @out Recursively walks directory tree and expands FOR set in each directory
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements FOR /R [[drive:]path] %V IN (set) DO body (skipping reparse points to prevent cycles).
@proc_frame exec_for_walk_r64, 40, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    mov r12, rcx ; curDir
    mov r13, rdx ; setStr
    mov rdi, r8  ; body AST
    movzx r14d, r9b ; var char

    mov rsi, r13
.r_set_lp:
    mov rcx, rsi
    call for_next_set_item64
    mov rbp, rdx
    test rax, rax
    jz .recurse_subdirs

    mov rcx, rax
    call tok_strip_quotes64
    jc .done
    mov rbx, rax

    mov ecx, 4096
    call arena_alloc64
    jc .done
    mov r15, rax
    mov rcx, rax
    mov rdx, r12
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .r_app_it
    mov byte [rax], 0x5C
    inc rax
.r_app_it:
    mov rcx, rax
    mov rdx, rbx
    call str_copy64

    mov rcx, r15
    mov rdx, rdi
    mov r8b, r14b
    xor r9d, r9d
    call exec_for_plain_or_dirs64
    mov rsi, rbp
    jmp .r_set_lp

.recurse_subdirs:
    mov ecx, 4096
    call arena_alloc64
    jc .done
    mov r15, rax
    mov rcx, rax
    mov rdx, r12
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .r_star
    mov byte [rax], 0x5C
    inc rax
.r_star:
    mov word [rax], 0x002A ; "*"

    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .done
    mov rbx, rax ; FIND_ENTRY64
    mov rcx, r15
    mov rdx, rbx
    call hal_find_first64
    jc .done
    mov rbp, rax ; hFind

.sub_lp:
    mov eax, dword [rbx + FIND_ENTRY64.dwAttr]
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .sub_nx
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .sub_nx
    cmp byte [rbx + FIND_ENTRY64.szNameWtf8], '.'
    jne .do_sub
    cmp byte [rbx + FIND_ENTRY64.szNameWtf8 + 1], 0
    je .sub_nx
    cmp byte [rbx + FIND_ENTRY64.szNameWtf8 + 1], '.'
    jne .do_sub
    cmp byte [rbx + FIND_ENTRY64.szNameWtf8 + 2], 0
    je .sub_nx

.do_sub:
    mov ecx, 4096
    call arena_alloc64
    jc .sub_close
    mov r15, rax
    mov rcx, rax
    mov rdx, r12
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .sub_app
    mov byte [rax], 0x5C
    inc rax
.sub_app:
    mov rcx, rax
    lea rdx, [rbx + FIND_ENTRY64.szNameWtf8]
    call str_copy64

    mov rcx, r15
    mov rdx, r13
    mov r8, rdi
    mov r9b, r14b
    call exec_for_walk_r64

.sub_nx:
    cmp byte [rel g_exit_flag], 0
    jne .sub_close
    mov rcx, rbp
    mov rdx, rbx
    call hal_find_next64
    jnc .sub_lp

.sub_close:
    mov rcx, rbp
    call qword [rel __imp_FindClose]

.done:
    @epilog_ret
@endproc

;; @proc for_f_process_line64
;; @abi win64_ms
;; @in RCX = input line, RDX = body AST_NODE64*, R8 = FOR /F options block (+0=eol:dword, +8=delims_ptr:qword, +16=tok_mask:dword, +20=star_after:dword, +24=var_char:dword)
;; @out Splits line by delims, binds requested token(s) to %V, %(V+1)..., and executes body AST
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Tokenizes a single FOR /F line according to eol=, delims=, and tokens=m,n-o,* rules.
@proc_frame for_f_process_line64, 488, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 488 = 40 + 28*16
    ; [rsp + 48..255]  : 26 token QWORD pointers (rbx)
    ; [rsp + 256..463] : 26 remainder QWORD pointers (rbx + 208)
    ; [rsp + 464]      : extracted count (dword)
    ; [rsp + 468]      : saved g_for_depth (dword)
    mov rsi, rcx ; input line
    mov r14, rdx ; body AST
    mov rbp, r8  ; options block
    lea rbx, [rsp + 48]

    cmp byte [rsi], 0
    je .done

    xor r12d, r12d ; tok_idx (0-based)
    mov r13, qword [rbp + 8] ; delims_ptr
    cmp byte [r13], 0
    jne .tok_lp

    mov al, byte [rsi]
    cmp al, byte [rbp]
    je .done
    mov rcx, rsi
    call arena_strdup64
    jc .done
    mov qword [rbx], rax
    lea rax, [rel szEmpty64]
    mov qword [rbx + 208], rax
    mov r12d, 1
    jmp .bind_tokens

.tok_lp:
    cmp r12d, 25
    jae .check_eol_first
.delim_lp:
    mov al, byte [rsi]
    test al, al
    jz .check_eol_first
    mov rcx, r13
.d_chk:
    mov dl, byte [rcx]
    test dl, dl
    jz .tok_found_start
    cmp al, dl
    je .is_d
    inc rcx
    jmp .d_chk
.is_d:
    inc rsi
    jmp .delim_lp

.tok_found_start:
    mov r15, rsi ; token end scan
.t_scan:
    mov al, byte [r15]
    test al, al
    jz .t_end
    mov rcx, r13
.d_chk2:
    mov dl, byte [rcx]
    test dl, dl
    jz .not_d2
    cmp al, dl
    je .t_end
    inc rcx
    jmp .d_chk2
.not_d2:
    inc r15
    jmp .t_scan

.t_end:
    mov r8, r15
    sub r8, rsi ; tok_len
    mov qword [rsp + 472], r8
    lea rcx, [r8 + 1]
    call arena_alloc64
    jc .done
    mov rdi, rax
    mov qword [rbx + r12 * 8], rax
    mov rcx, qword [rsp + 472]
    rep movsb
    mov byte [rdi], 0

    mov rsi, r15
.rem_d_lp:
    mov al, byte [rsi]
    test al, al
    jz .save_rem
    mov rcx, r13
.d_chk3:
    mov dl, byte [rcx]
    test dl, dl
    jz .save_rem
    cmp al, dl
    je .is_d3
    inc rcx
    jmp .d_chk3
.is_d3:
    inc rsi
    jmp .rem_d_lp
.save_rem:
    mov qword [rbx + 208 + r12 * 8], rsi
    inc r12d
    cmp byte [rsi], 0
    jne .tok_lp

.check_eol_first:
    test r12d, r12d
    jz .done
    mov rax, qword [rbx]
    mov al, byte [rax]
    cmp al, byte [rbp]
    je .done

.bind_tokens:
    mov dword [rsp + 464], r12d
    mov eax, dword [rel g_for_depth]
    mov dword [rsp + 468], eax

    movzx edx, byte [rbp + 24] ; next var char
    xor esi, esi ; bit index k = 0..24
    xor ecx, ecx ; bound_any_nonempty
.bind_lp:
    cmp esi, 25
    jae .chk_star_tok
    mov eax, dword [rbp + 16] ; tok_mask
    bt eax, esi
    jnc .nx_bit

    lea r8, [rel szEmpty64]
    cmp esi, dword [rsp + 464]
    jae .push_v
    mov r8, qword [rbx + rsi * 8]
    inc ecx
.push_v:
    mov eax, dword [rel g_for_depth]
    cmp eax, W64_FOR_SCOPE_MAX
    jae .nx_bit
    lea r9, [rel g_for_vars]
    mov byte [r9 + rax], dl
    lea r9, [rel g_for_vals]
    mov qword [r9 + rax * 8], r8
    inc dword [rel g_for_depth]
    inc edx
.nx_bit:
    inc esi
    jmp .bind_lp

.chk_star_tok:
    mov eax, dword [rbp + 20] ; star_after
    test eax, eax
    jz .exec_body_chk
    dec eax
    lea r8, [rel szEmpty64]
    cmp eax, dword [rsp + 464]
    jae .push_star
    mov r8, qword [rbx + 208 + rax * 8]
    inc ecx
.push_star:
    mov eax, dword [rel g_for_depth]
    cmp eax, W64_FOR_SCOPE_MAX
    jae .exec_body_chk
    lea r9, [rel g_for_vars]
    mov byte [r9 + rax], dl
    lea r9, [rel g_for_vals]
    mov qword [r9 + rax * 8], r8
    inc dword [rel g_for_depth]

.exec_body_chk:
    test ecx, ecx
    jz .restore_scope
    mov rcx, r14
    call exec_ast_node64

.restore_scope:
    mov eax, dword [rsp + 468]
    mov dword [rel g_for_depth], eax

.done:
    @epilog_ret
@endproc

;; @proc for_f_read_handle_lines64
;; @abi win64_ms
;; @in RCX = readable Win64 HANDLE, RDX = body AST_NODE64*, R8 = FOR /F options block, R9D = skip_count
;; @out Streams lines from RCX, skips R9D lines, and invokes for_f_process_line64
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads lines from a file or anonymous pipe handle for FOR /F.
@proc_frame for_f_read_handle_lines64, 56, rbp, rbx, rsi, rdi, r12, r13
    mov rbx, rcx  ; hRead
    mov rdi, rdx  ; body AST
    mov rbp, r8   ; options block
    mov r12d, r9d ; skip_remaining

    mov ecx, 8192
    call arena_alloc64
    jc .done
    mov r13, rax ; lineBuf

.line_lp:
    cmp byte [rel g_exit_flag], 0
    jne .done
    cmp byte [rel g_goto_pending], 0
    jne .done
    xor esi, esi ; len
.ch_lp:
    mov dword [rsp + 44], 0
    mov byte [rsp + 40], 0
    mov rcx, rbx
    lea rdx, [rsp + 40]
    mov r8d, 1
    lea r9, [rsp + 44]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    test eax, eax
    jz .eof_chk
    cmp dword [rsp + 44], 0
    je .eof_chk
    mov al, byte [rsp + 40]
    cmp al, 0x1A
    je .eof_chk
    cmp al, 0x0A
    je .got_ln
    cmp al, 0x0D
    je .ch_lp
    cmp esi, 8188
    jae .ch_lp
    mov byte [r13 + rsi], al
    inc esi
    jmp .ch_lp

.eof_chk:
    test esi, esi
    jz .done
.got_ln:
    mov byte [r13 + rsi], 0
    test r12d, r12d
    jz .proc_ln
    dec r12d
    jmp .line_lp
.proc_ln:
    test esi, esi
    jz .line_lp
    mov rcx, r13
    mov rdx, rdi
    mov r8, rbp
    call for_f_process_line64
    jmp .line_lp

.done:
    @epilog_ret
@endproc

;; @proc exec_for_f64
;; @abi win64_ms
;; @in RCX = AST_NODE64* (AST_FOR with bSub == FORMODE_PARSE)
;; @out Executes FOR /F ["options"] %V IN (file-set | "string" | 'command' | `command`) DO body
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Full 64-bit FOR /F parser supporting eol=, skip=, delims=, tokens=m,n-o,*, and usebackq!
@proc_frame exec_for_f64, 184, rbp, rbx, rsi, rdi, r12, r13
    ; 6 saved regs (even) -> 184 = 40 + 9*16
    ; Options block [rbp + 0..47]:
    ;   +0: eol (dword), +8: delims_ptr (qword), +16: tok_mask (dword), +20: star_after (dword)
    ;   +24: var_char (dword), +28: skip_count (dword), +32: usebackq (dword), +40: pNode (qword)
    ; Pipe frame [rbp + 48..127]:
    ;   +48..71: SECURITY_ATTRIBUTES64 (24B)
    ;   +72..95: PROCESS_INFORMATION64 (24B)
    ;   +96: hRead (qword), +104: hWrite (qword)
    lea rbp, [rsp + 48]
    mov qword [rbp + 40], rcx

    mov dword [rbp], ';'
    lea rax, [rel szDefDelims64]
    mov qword [rbp + 8], rax
    mov dword [rbp + 16], 1
    mov dword [rbp + 20], 0
    movzx eax, byte [rcx + AST_NODE64.bVarChar]
    mov dword [rbp + 24], eax
    mov dword [rbp + 28], 0
    mov dword [rbp + 32], 0

    mov rsi, qword [rcx + AST_NODE64.pExtra]
    test rsi, rsi
    jz .opts_done
    cmp byte [rsi], '"'
    jne .opt_lp
    inc rsi
.opt_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    mov al, byte [rsi]
    test al, al
    jz .opts_done
    cmp al, '"'
    je .opts_done

    call ascii_upper_al64
    cmp al, 'U'
    je .opt_usebackq
    cmp al, 'E'
    je .opt_eol
    cmp al, 'S'
    je .opt_skip
    cmp al, 'D'
    je .opt_delims
    cmp al, 'T'
    je .opt_tokens
    inc rsi
    jmp .opt_lp

.opt_usebackq:
    mov dword [rbp + 32], 1
    add rsi, 8
    jmp .opt_lp

.opt_eol:
    add rsi, 4
    movzx eax, byte [rsi]
    mov dword [rbp], eax
    test eax, eax
    jz .opts_done
    inc rsi
    jmp .opt_lp

.opt_skip:
    lea rcx, [rsi + 5]
    call sa64_parse_i32_str
    jc .opts_done
    mov dword [rbp + 28], eax
    mov rsi, rdx
    jmp .opt_lp

.opt_delims:
    add rsi, 7
    mov rbx, rsi
    cmp byte [rbx], '"'
    jne .del_scan
    cmp byte [rbx + 1], 0
    jne .del_scan
    lea rax, [rel szEmpty64]
    mov qword [rbp + 8], rax
    jmp .opts_done
.del_scan:
    mov al, byte [rbx]
    test al, al
    jz .del_got
    cmp al, '"'
    jne .del_sp
    cmp byte [rbx + 1], 0
    je .del_got
.del_sp:
    cmp al, 0x20
    jne .del_adv
    cmp byte [rbx + 1], '"'
    jne .del_got
    cmp byte [rbx + 2], 0
    jne .del_got
    inc rbx
    jmp .del_got
.del_adv:
    inc rbx
    jmp .del_scan
.del_got:
    mov r12, rbx
    sub r12, rsi
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .opts_done
    mov qword [rbp + 8], rax
    mov rdi, rax
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rsi, rbx
    jmp .opt_lp

.opt_tokens:
    add rsi, 7
    mov dword [rbp + 16], 0
    xor ebx, ebx ; highest 1-based token seen
.tk_spec_lp:
    mov al, byte [rsi]
    cmp al, '*'
    je .tk_star
    cmp al, '0'
    jb .tk_end
    cmp al, '9'
    ja .tk_end
    mov rcx, rsi
    call sa64_parse_i32_str
    jc .tk_end
    mov rsi, rdx
    test eax, eax
    jz .tk_comma
    mov edi, eax
    mov ebx, eax
    cmp byte [rsi], '-'
    jne .set_one_tok
    lea rcx, [rsi + 1]
    call sa64_parse_i32_str
    jc .set_one_tok
    mov rsi, rdx
    mov ebx, eax
.range_tok_lp:
    cmp edi, ebx
    ja .tk_comma
    lea ecx, [edi - 1]
    cmp ecx, 25
    jae .nx_rtok
    bts dword [rbp + 16], ecx
.nx_rtok:
    inc edi
    jmp .range_tok_lp

.set_one_tok:
    lea ecx, [edi - 1]
    cmp ecx, 25
    jae .tk_comma
    bts dword [rbp + 16], ecx
.tk_comma:
    cmp byte [rsi], '*'
    je .tk_star
    cmp byte [rsi], ','
    jne .tk_end
    inc rsi
    jmp .tk_spec_lp

.tk_star:
    inc rsi
    test ebx, ebx
    jnz .star_after_n
    lea rax, [rel szEmpty64]
    mov qword [rbp + 8], rax
    mov dword [rbp + 16], 1
    jmp .opt_lp
.star_after_n:
    mov dword [rbp + 20], ebx
.tk_end:
    jmp .opt_lp

.opts_done:
    mov rbx, qword [rbp + 40] ; AST_NODE64*
    mov rdi, qword [rbx + AST_NODE64.pChild1] ; body AST
    mov rcx, qword [rbx + AST_NODE64.pStr1]
    call prs_skip_ws64
    mov rsi, rax
    mov al, byte [rsi]
    test al, al
    jz .done

    cmp dword [rbp + 32], 0
    jne .ubq_mode
    cmp al, '"'
    je .lit_dquote
    cmp al, 0x27
    je .cmd_squote
    jmp .fileset_mode

.ubq_mode:
    cmp al, 0x27
    je .lit_squote
    cmp al, '`'
    je .cmd_bquote
    jmp .fileset_mode

.lit_dquote:
    mov r12b, '"'
    jmp .do_lit_str
.lit_squote:
    mov r12b, 0x27
.do_lit_str:
    lea rcx, [rsi + 1]
    call arena_strdup64
    jc .done
    mov rbx, rax
    mov rcx, rbx
    call str_len64
    test rax, rax
    jz .run_lit
    cmp byte [rbx + rax - 1], r12b
    jne .run_lit
    mov byte [rbx + rax - 1], 0
.run_lit:
    cmp dword [rbp + 28], 0
    jne .done
    mov rcx, rbx
    mov rdx, rdi
    mov r8, rbp
    call for_f_process_line64
    jmp .done

.cmd_squote:
    mov r12b, 0x27
    jmp .do_cmd_capture
.cmd_bquote:
    mov r12b, '`'
.do_cmd_capture:
    lea rcx, [rsi + 1]
    call arena_strdup64
    jc .done
    mov rbx, rax
    mov rcx, rbx
    call str_len64
    test rax, rax
    jz .done
    cmp byte [rbx + rax - 1], r12b
    jne .spawn_cap
    mov byte [rbx + rax - 1], 0

.spawn_cap:
    mov dword [rbp + 48 + SECURITY_ATTRIBUTES64.nLength], 24
    mov dword [rbp + 48 + SECURITY_ATTRIBUTES64.dwPad0], 0
    mov qword [rbp + 48 + SECURITY_ATTRIBUTES64.lpSecurityDescriptor], 0
    mov dword [rbp + 48 + SECURITY_ATTRIBUTES64.bInheritHandle], 1
    mov dword [rbp + 48 + SECURITY_ATTRIBUTES64.dwPad1], 0

    lea rcx, [rbp + 96]  ; &hRead
    lea rdx, [rbp + 104] ; &hWrite
    lea r8, [rbp + 48]   ; &SA
    mov r9d, 65536
    call qword [rel __imp_CreatePipe]
    test eax, eax
    jz .done

    mov rcx, qword [rbp + 96]
    call redir_set_non_inheritable64
    mov qword [rbp + 96], rax

    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rcx, rbx
    mov rdx, rax
    mov r8, qword [rbp + 104]
    lea r9, [rbp + 72]
    call pipe_spawn_stage64
    jc .cap_close_both

    mov rcx, qword [rbp + 72 + PROCESS_INFORMATION64.hThread]
    call qword [rel __imp_CloseHandle]
    mov rcx, qword [rbp + 104]
    call qword [rel __imp_CloseHandle]

    mov rcx, qword [rbp + 96]
    mov rdx, rdi
    mov r8, rbp
    mov r9d, dword [rbp + 28]
    call for_f_read_handle_lines64

    mov rcx, qword [rbp + 96]
    call qword [rel __imp_CloseHandle]
    mov rcx, qword [rbp + 72 + PROCESS_INFORMATION64.hProcess]
    mov edx, INFINITE_WAIT
    call qword [rel __imp_WaitForSingleObject]
    mov rcx, qword [rbp + 72 + PROCESS_INFORMATION64.hProcess]
    call qword [rel __imp_CloseHandle]
    jmp .done

.cap_close_both:
    mov rcx, qword [rbp + 96]
    call qword [rel __imp_CloseHandle]
    mov rcx, qword [rbp + 104]
    call qword [rel __imp_CloseHandle]
    jmp .done

.fileset_mode:
    mov rbx, rsi
.fset_lp:
    mov rcx, rbx
    call for_next_set_item64
    mov rbx, rdx
    test rax, rax
    jz .done
    mov rcx, rax
    call tok_strip_quotes64
    jc .done
    mov rcx, rax
    mov edx, GENERIC_READ
    mov r8d, OPEN_EXISTING
    xor r9d, r9d
    call hal_open_file64
    jc .fset_lp
    mov r13, rax
    mov rcx, r13
    mov rdx, rdi
    mov r8, rbp
    mov r9d, dword [rbp + 28]
    call for_f_read_handle_lines64
    mov rcx, r13
    call qword [rel __imp_CloseHandle]
    jmp .fset_lp

.done:
    @epilog_ret
@endproc

;; @proc exec_ast_node64
;; @abi win64_ms
;; @in RCX = AST_NODE64*
;; @out EAX = command exit code (stored in g_last_exit_code)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Recursively executes any 64-bit Arena AST node (NOP, BINARY, BLOCK, IF, FOR, SIMPLE_CMD).
@proc_frame exec_ast_node64, 232, rbp, rbx, rsi, rdi, r12, r13
    ; 6 saved regs (even) -> 232 = 40 + 12*16
    ; [rsp + 48..207] : 160-byte 64-bit redirection save buffer
    ; [rsp + 208..231]: FOR /L number format buffer (24B)
    test rcx, rcx
    jz .done_cur
    cmp byte [rel g_exit_flag], 0
    jne .done_cur
    cmp byte [rel g_goto_pending], 0
    jne .done_cur
    cmp dword [rel g_ctrl_break], 0
    je .dispatch_kind
    mov dword [rel g_ctrl_break], 0
    mov dword [rel g_last_exit_code], 0xC000013A ; STATUS_CONTROL_C_EXIT
    jmp .done_cur

.dispatch_kind:
    mov rbp, rcx
    movzx eax, byte [rbp + AST_NODE64.bKind]
    cmp al, AST_NOP
    je .done_cur
    cmp al, AST_BINARY
    je .do_binary
    cmp al, AST_BLOCK
    je .do_block
    cmp al, AST_IF
    je .do_if
    cmp al, AST_FOR
    je .do_for
    cmp al, AST_SIMPLE_CMD
    je .do_simple
    jmp .done_cur

.do_binary:
    mov al, byte [rbp + AST_NODE64.bSub]
    cmp al, BINOP_PIPE
    je .do_pipe

    mov rcx, qword [rbp + AST_NODE64.pChild1]
    call exec_ast_node64
    cmp byte [rel g_exit_flag], 0
    jne .done_cur
    cmp byte [rel g_goto_pending], 0
    jne .done_cur

    mov al, byte [rbp + AST_NODE64.bSub]
    cmp al, BINOP_SEQ
    je .run_rhs
    cmp al, BINOP_AND
    je .chk_and
    cmp dword [rel g_last_exit_code], 0
    je .done_cur
    jmp .run_rhs
.chk_and:
    cmp dword [rel g_last_exit_code], 0
    jne .done_cur
.run_rhs:
    mov rcx, qword [rbp + AST_NODE64.pChild2]
    call exec_ast_node64
    jmp .done_cur

.do_pipe:
    mov rcx, rbp
    call exec_pipeline64
    jmp .done_cur

.do_block:
    cmp qword [rbp + AST_NODE64.pRedir], 0
    je .blk_no_redir
    mov rcx, qword [rbp + AST_NODE64.pRedir]
    lea rdx, [rsp + 48]
    call redir_apply64
    jc .blk_redir_err
    mov rcx, qword [rbp + AST_NODE64.pChild1]
    call exec_ast_node64
    lea rcx, [rsp + 48]
    call redir_restore64
    jmp .done_cur
.blk_redir_err:
    mov ecx, eax
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    jmp .done_cur
.blk_no_redir:
    mov rcx, qword [rbp + AST_NODE64.pChild1]
    call exec_ast_node64
    jmp .done_cur

.do_if:
    mov rcx, rbp
    call exec_eval_if_cond64
    test eax, eax
    jz .if_else
    mov rcx, qword [rbp + AST_NODE64.pChild1]
    call exec_ast_node64
    jmp .done_cur
.if_else:
    mov rcx, qword [rbp + AST_NODE64.pChild2]
    test rcx, rcx
    jz .done_cur
    call exec_ast_node64
    jmp .done_cur

.do_for:
    mov rcx, qword [rbp + AST_NODE64.pStr1]
    call expand_phase3_64
    jc .done_cur
    mov rcx, rax
    call expand_phase4_64
    jc .done_cur
    mov r12, rax ; expanded set

    mov al, byte [rbp + AST_NODE64.bSub]
    cmp al, FORMODE_PLAIN
    je .for_plain
    cmp al, FORMODE_DIRS
    je .for_dirs
    cmp al, FORMODE_RECURSE
    je .for_r
    cmp al, FORMODE_NUMERIC
    je .for_l
    mov qword [rbp + AST_NODE64.pStr1], r12
    mov rcx, rbp
    call exec_for_f64
    jmp .done_cur

.for_plain:
    mov rcx, r12
    mov rdx, qword [rbp + AST_NODE64.pChild1]
    mov r8b, byte [rbp + AST_NODE64.bVarChar]
    xor r9d, r9d
    call exec_for_plain_or_dirs64
    jmp .done_cur

.for_dirs:
    mov rcx, r12
    mov rdx, qword [rbp + AST_NODE64.pChild1]
    mov r8b, byte [rbp + AST_NODE64.bVarChar]
    mov r9b, 1
    call exec_for_plain_or_dirs64
    jmp .done_cur

.for_r:
    mov rcx, qword [rbp + AST_NODE64.pExtra]
    test rcx, rcx
    jnz .for_r_norm
    mov ecx, 2048
    call arena_alloc64
    jc .done_cur
    mov rbx, rax
    mov rcx, rbx
    mov edx, 2040
    call hal_get_cwd64
    mov rax, rbx
    jmp .for_r_go
.for_r_norm:
    call tok_strip_quotes64
    jc .done_cur
    mov rcx, rax
    call path_normalize64
    jc .done_cur
.for_r_go:
    mov rcx, rax
    mov rdx, r12
    mov r8, qword [rbp + AST_NODE64.pChild1]
    mov r9b, byte [rbp + AST_NODE64.bVarChar]
    call exec_for_walk_r64
    jmp .done_cur

.for_l:
    mov rcx, r12
    call for_next_set_item64
    test rax, rax
    jz .done_cur
    mov rsi, rdx
    mov rcx, rax
    call exp_parse_signed_int64
    mov ebx, eax ; cur = start

    mov rcx, rsi
    call for_next_set_item64
    test rax, rax
    jz .done_cur
    mov rsi, rdx
    mov rcx, rax
    call exp_parse_signed_int64
    mov edi, eax ; step

    mov rcx, rsi
    call for_next_set_item64
    test rax, rax
    jz .done_cur
    mov rcx, rax
    call exp_parse_signed_int64
    mov r13d, eax ; end

.for_l_lp:
    cmp byte [rel g_exit_flag], 0
    jne .done_cur
    cmp byte [rel g_goto_pending], 0
    jne .done_cur
    test edi, edi
    js .for_l_neg
    cmp ebx, r13d
    jg .done_cur
    jmp .for_l_body
.for_l_neg:
    cmp ebx, r13d
    jl .done_cur
.for_l_body:
    mov ecx, ebx
    lea rdx, [rsp + 208]
    call fmt_i32_dec64
    mov cl, byte [rbp + AST_NODE64.bVarChar]
    lea rdx, [rsp + 208]
    mov r8, qword [rbp + AST_NODE64.pChild1]
    call for_bind_run_one64
    test edi, edi
    jz .done_cur
    add ebx, edi
    jmp .for_l_lp

.do_simple:
    mov rcx, qword [rbp + AST_NODE64.pStr1]
    call expand_phase3_64
    jc .done_cur
    mov rcx, rax
    call expand_phase4_64
    jc .done_cur
    mov rbx, rax ; final command line

    cmp dword [rel g_batch_depth], 0
    je .apply_s_redir
    cmp byte [rel g_echo_on], 0
    je .apply_s_redir
    test byte [rbp + AST_NODE64.bFlags], AST_FLAG_QUIET
    jnz .apply_s_redir
    cmp byte [rbx], 0
    je .apply_s_redir
    call cmd_print_prompt64
    mov rcx, rbx
    call con_print_z64
    call con_print_crlf64

.apply_s_redir:
    cmp qword [rbp + AST_NODE64.pRedir], 0
    je .run_s_body
    mov rcx, qword [rbp + AST_NODE64.pRedir]
    lea rdx, [rsp + 48]
    call redir_apply64
    jc .s_redir_err
    mov rcx, rbx
    call exec_simple_line64
    lea rcx, [rsp + 48]
    call redir_restore64
    jmp .done_cur

.s_redir_err:
    mov ecx, eax
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    jmp .done_cur

.run_s_body:
    mov rcx, rbx
    call exec_simple_line64

.done_cur:
    mov eax, dword [rel g_last_exit_code]
    @epilog_ret
@endproc

;; @proc batch_seek_label64
;; @abi win64_ms
;; @in RCX = BATCH_FRAME64*, RDX = target label name (without leading ':')
;; @out Updates RCX->uFileOff to line immediately after matching :label, CF=0; or CF=1 if missing
;; @err CF=1, EAX=ERR_LABEL_NOT_FOUND
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Scans forward from current uFileOff then wraps to byte 0 to locate :label (case-insensitive).
@proc_frame batch_seek_label64, 40, rbp, rbx, rsi, rdi, r12, r13
    mov rbx, rcx ; BATCH_FRAME64*
    mov rbp, rdx ; target label

    mov ecx, 4096
    call arena_alloc64
    jc .not_found
    mov rdi, rax ; scratch line buf

    mov r12, qword [rbx + BATCH_FRAME64.uFileOff]
    xor r13d, r13d ; pass = 0 (forward), 1 (from 0)

.scan_lp:
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, 4090
    call batch_read_raw_line64
    jc .next_pass

    mov rcx, rdi
    call prs_skip_ws64
    cmp byte [rax], ':'
    jne .scan_lp
    inc rax
    cmp byte [rax], ':'
    je .scan_lp
    mov rcx, rax
    call prs_skip_ws64
    mov rsi, rax
    mov rcx, rsi
.lbl_end:
    mov dl, byte [rcx]
    test dl, dl
    jz .cmp_lbl
    cmp dl, 0x20
    je .term_lbl
    cmp dl, 0x09
    je .term_lbl
    inc rcx
    jmp .lbl_end
.term_lbl:
    mov byte [rcx], 0
.cmp_lbl:
    mov rcx, rsi
    mov rdx, rbp
    call str_eq_i64
    test eax, eax
    jnz .found
    jmp .scan_lp

.next_pass:
    test r13d, r13d
    jnz .not_found_restore
    mov r13d, 1
    mov qword [rbx + BATCH_FRAME64.uFileOff], 0
    mov byte [rbx + BATCH_FRAME64.bEncoding], 0
    jmp .scan_lp

.found:
    clc
    @epilog_ret

.not_found_restore:
    mov qword [rbx + BATCH_FRAME64.uFileOff], r12
.not_found:
    mov eax, ERR_LABEL_NOT_FOUND
    stc
    @epilog_ret
@endproc

;; @proc batch_run_file64
;; @abi win64_ms
;; @in RCX = normalized .bat/.cmd path, RDX = raw argument tail, R8B = bIsCallSub (0=file, 1=CALL :label)
;; @out EAX = exit code, CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Pushes a 112-byte BATCH_FRAME64, streams stateless byte-offset lines + (...) blocks, and unwinds SETLOCAL.
@proc_frame batch_run_file64, 40, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    mov rsi, rcx ; batchPath
    mov r12, rdx ; argsRaw
    movzx r13d, r8b

    mov eax, dword [rel g_batch_depth]
    cmp eax, W64_BATCH_MAX
    jae .too_deep

    imul rbx, rax, BATCH_FRAME64_SIZE
    lea rdx, [rel g_batch_stack]
    add rbx, rdx

    mov rdi, rbx
    xor eax, eax
    mov ecx, 14 ; 112 bytes = 14 qwords
    rep stosq

    mov byte [rbx + BATCH_FRAME64.bIsCallSub], r13b
    mov eax, dword [rel g_setlocal_depth]
    mov byte [rbx + BATCH_FRAME64.bSavedSetlocalDepth], al

    mov rcx, rsi
    call str_len64
    lea r14, [rax + 1]
    mov rcx, r14
    call perm_alloc64
    jc .oom
    mov qword [rbx + BATCH_FRAME64.pBatchPath], rax
    mov qword [rbx + BATCH_FRAME64.argvPtrs], rax ; %0
    mov rdi, rax
    mov rcx, r14
    rep movsb

    mov rcx, r12
    test rcx, rcx
    jnz .dup_raw_args
    lea rcx, [rel szEmpty64]
.dup_raw_args:
    call prs_skip_ws64
    mov rsi, rax
    mov rcx, rsi
    call str_len64
    lea r14, [rax + 1]
    mov rcx, r14
    call perm_alloc64
    jc .oom_free_p
    mov qword [rbx + BATCH_FRAME64.pArgsRaw], rax
    mov r12, rsi
    mov rdi, rax
    mov rcx, r14
    rep movsb
    mov rsi, r12

    mov r15d, 1
.arg_lp:
    cmp r15d, 9
    ja .args_done
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .args_done
    cmp byte [rsi], ','
    je .skip_asep
    cmp byte [rsi], ';'
    je .skip_asep
    cmp byte [rsi], '='
    jne .rd_arg
.skip_asep:
    inc rsi
    jmp .arg_lp
.rd_arg:
    mov rcx, rsi
    call prs_read_word64
    jc .args_done
    mov r12, rdx ; next cursor
    mov rsi, rax
    mov rcx, rax
    call str_len64
    lea r14, [rax + 1]
    mov rcx, r14
    call perm_alloc64
    jc .args_done
    mov qword [rbx + BATCH_FRAME64.argvPtrs + r15 * 8], rax
    mov rdi, rax
    mov rcx, r14
    rep movsb
    mov rsi, r12
    inc r15d
    jmp .arg_lp

.args_done:
    inc dword [rel g_batch_depth]

    mov ecx, W64_LINE_MAX
    call perm_alloc64
    jc .pop_frame_oom
    mov rbp, rax ; lineBuf

    cmp byte [rbx + BATCH_FRAME64.bIsCallSub], 0
    je .stmt_loop
    mov rcx, rbx
    lea rdx, [rel g_goto_label]
    call batch_seek_label64
    mov byte [rel g_goto_pending], 0
    jc .cleanup_frame

.stmt_loop:
    cmp byte [rel g_exit_flag], 0
    jne .cleanup_frame
    cmp dword [rel g_ctrl_break], 0
    jne .cleanup_frame

    call arena_reset64

    mov rcx, rbx
    mov rdx, rbp
    mov r8d, W64_LINE_MAX - 8
    call batch_read_logical_line64
    jc .cleanup_frame
    mov rsi, rax ; cur_len

.bal_lp:
    mov rcx, rbp
    call prs_paren_balance64
    test eax, eax
    jz .expand_and_exec
    mov byte [rsi + rbp], 0x0A
    inc rsi
    mov byte [rsi + rbp], 0
    mov r8d, W64_LINE_MAX - 8
    sub r8, rsi
    cmp r8, 8
    jbe .expand_and_exec
    mov rcx, rbx
    lea rdx, [rsi + rbp]
    call batch_read_logical_line64
    jc .expand_and_exec
    add rsi, rax
    jmp .bal_lp

.expand_and_exec:
    mov rcx, rbp
    mov edx, 1
    call expand_phase1_64
    jc .stmt_loop
    mov rcx, rax
    call parse_line_to_ast64
    jc .syn_err_line
    mov rcx, rax
    call exec_ast_node64

    cmp byte [rel g_batch_ret_flag], 0
    jne .on_exit_b
    cmp byte [rel g_goto_pending], 0
    je .stmt_loop

    mov byte [rel g_goto_pending], 0
    lea rcx, [rel g_goto_label]
    lea rdx, [rel szKwEof64]
    call str_eq_i64
    test eax, eax
    jnz .cleanup_frame

    mov rcx, rbx
    lea rdx, [rel g_goto_label]
    call batch_seek_label64
    jnc .stmt_loop
    mov ecx, ERR_LABEL_NOT_FOUND
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    jmp .cleanup_frame

.syn_err_line:
    mov ecx, eax
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    jmp .cleanup_frame

.on_exit_b:
    mov byte [rel g_batch_ret_flag], 0

.cleanup_frame:
    mov rcx, rbp
    call perm_free64

    movzx esi, byte [rbx + BATCH_FRAME64.bSavedSetlocalDepth]
.unwind_sl:
    cmp dword [rel g_setlocal_depth], esi
    jbe .free_frame_ptrs
    call env_endlocal_pop64
    jmp .unwind_sl

.free_frame_ptrs:
    mov rcx, qword [rbx + BATCH_FRAME64.pBatchPath]
    call perm_free64
    mov rcx, qword [rbx + BATCH_FRAME64.pArgsRaw]
    call perm_free64
    mov esi, 1
.fr_ag:
    cmp esi, 9
    ja .dec_depth
    mov rcx, qword [rbx + BATCH_FRAME64.argvPtrs + rsi * 8]
    call perm_free64
    inc esi
    jmp .fr_ag
.dec_depth:
    dec dword [rel g_batch_depth]
    mov eax, dword [rel g_last_exit_code]
    clc
    @epilog_ret

.pop_frame_oom:
    dec dword [rel g_batch_depth]
.oom_free_p:
    mov rcx, qword [rbx + BATCH_FRAME64.pBatchPath]
    call perm_free64
.oom:
.too_deep:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc probe_file_exists_kind64
;; @abi win64_ms
;; @in RCX = candidate file path
;; @out RAX = normalized path, DL = kind (1=.BAT/.CMD, 2=.COM, 3=.EXE/other), CF=0; or CF=1
;; @err CF=1 if missing or is a directory
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Checks file existence (excluding directories) and classifies .BAT/.CMD vs .COM vs .EXE.
@proc_frame probe_file_exists_kind64, 40, rbx, rsi
    mov rsi, rcx
    call hal_get_attr64
    jc .no
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jnz .no

    mov rcx, rsi
    call path_normalize64
    jc .no
    mov rbx, rax
    mov rcx, rbx
    call str_len64
    cmp rax, 4
    jb .kind_exe

    lea rsi, [rbx + rax - 4]
    mov rcx, rsi
    lea rdx, [rel szExtBat64]
    call str_eq_i64
    test eax, eax
    jnz .kind_bat
    mov rcx, rsi
    lea rdx, [rel szExtCmd64]
    call str_eq_i64
    test eax, eax
    jnz .kind_bat
    mov rcx, rsi
    lea rdx, [rel szExtCom64]
    call str_eq_i64
    test eax, eax
    jnz .kind_com

.kind_exe:
    mov rax, rbx
    mov dl, 3
    clc
    @epilog_ret
.kind_bat:
    mov rax, rbx
    mov dl, 1
    clc
    @epilog_ret
.kind_com:
    mov rax, rbx
    mov dl, 2
    clc
    @epilog_ret
.no:
    stc
    @epilog_ret
@endproc

;; @proc probe_one_candidate_exts64
;; @abi win64_ms
;; @in RCX = base candidate path, RDX = non-zero if RCX already has an extension
;; @out RAX = existing full path, DL = kind (1=.BAT/.CMD, 2=.COM, 3=.EXE/other), CF=0; or CF=1
;; @err CF=1 if not found
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Probes RCX directly (when extension present) or appends each %PATHEXT% entry (.COM;.EXE;.BAT;.CMD).
@proc_frame probe_one_candidate_exts64, 48, rbx, rsi, rdi, r12, r13
    mov rbx, rcx
    test rdx, rdx
    jz .try_pathext

    call probe_file_exists_kind64
    jnc .ok

.try_pathext:
    lea rcx, [rel szVarPathext64]
    call env_get64
    test rax, rax
    jnz .got_pext
    lea rax, [rel szDefPathext64]
.got_pext:
    mov rsi, rax

.ext_lp:
    cmp byte [rsi], 0
    je .fail
    cmp byte [rsi], ';'
    jne .ext_tok
    inc rsi
    jmp .ext_lp
.ext_tok:
    mov r12, rsi
.ext_sc:
    mov al, byte [r12]
    test al, al
    jz .build_cand
    cmp al, ';'
    je .build_cand
    inc r12
    jmp .ext_sc

.build_cand:
    mov ecx, 4096
    call arena_alloc64
    jc .fail
    mov r13, rax
    mov rcx, rax
    mov rdx, rbx
    call str_copy64
    mov rdi, rax
    mov rcx, r12
    sub rcx, rsi
    rep movsb
    mov byte [rdi], 0
    mov rcx, r13
    call probe_file_exists_kind64
    jnc .ok
    mov rsi, r12
    jmp .ext_lp

.ok:
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc resolve_executable64
;; @abi win64_ms
;; @in RCX = program name (quoted or unquoted)
;; @out RAX = resolved existing path in arena, DL = kind (1=.BAT/.CMD, 2=.COM, 3=.EXE/other), CF=0; or CF=1
;; @err CF=1, EAX=ERR_BAD_COMMAND
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Searches explicit path, CWD (unless NoDefaultCurrentDirectoryInExePath), and %PATH% across %PATHEXT%.
@proc_frame resolve_executable64, 40, rbp, rbx, rsi, rdi, r12, r13
    call tok_strip_quotes64
    jc .not_found
    mov rbp, rax ; cleanProg

    mov rsi, rbp
    xor ebx, ebx ; has_sep
    xor r12d, r12d ; has_ext_ptr
.scan_p:
    mov al, byte [rsi]
    test al, al
    jz .scan_p_done
    cmp al, 0x5C
    je .mark_sep
    cmp al, '/'
    je .mark_sep
    cmp al, ':'
    je .mark_sep
    cmp al, '.'
    jne .nx_p
    mov r12, rsi
    jmp .nx_p
.mark_sep:
    mov ebx, 1
    xor r12d, r12d
.nx_p:
    inc rsi
    jmp .scan_p

.scan_p_done:
    test ebx, ebx
    jnz .probe_here_only

    lea rcx, [rel szVarNoDefCwd64]
    call env_get_raw64
    test rax, rax
    jnz .search_path_env

    mov rcx, rbp
    mov rdx, r12
    call probe_one_candidate_exts64
    jnc .found_out
    jmp .search_path_env

.probe_here_only:
    mov rcx, rbp
    mov rdx, r12
    call probe_one_candidate_exts64
    jnc .found_out
    jmp .not_found

.search_path_env:
    lea rcx, [rel szVarPath64]
    call env_get64
    test rax, rax
    jz .not_found
    mov rsi, rax

.path_lp:
    cmp byte [rsi], 0
    je .not_found
    cmp byte [rsi], ';'
    jne .dir_start
    inc rsi
    jmp .path_lp
.dir_start:
    mov rbx, rsi
.dir_scan:
    mov al, byte [rbx]
    test al, al
    jz .got_dir
    cmp al, ';'
    je .got_dir
    inc rbx
    jmp .dir_scan

.got_dir:
    mov r13, rbx
    sub r13, rsi
    lea rcx, [r13 + 2048]
    call arena_alloc64
    jc .not_found
    mov r8, rax
    mov rdi, rax
    mov rcx, r13
    rep movsb
    cmp rdi, r8
    jbe .app_prog
    cmp byte [rdi - 1], 0x5C
    je .app_prog
    cmp byte [rdi - 1], '/'
    je .app_prog
    mov byte [rdi], 0x5C
    inc rdi
.app_prog:
    mov rcx, rdi
    mov rdx, rbp
    mov r13, r8
    call str_copy64
    mov rcx, r13
    mov rdx, r12
    call probe_one_candidate_exts64
    jnc .found_out
    mov rsi, rbx
    jmp .path_lp

.not_found:
    mov eax, ERR_BAD_COMMAND
    stc
    @epilog_ret

.found_out:
    clc
    @epilog_ret
@endproc

;; @proc exec_external64
;; @abi win64_ms
;; @in RCX = program token, RDX = raw argument tail, R8D = dwCreationFlags (0 for normal synchronous child)
;; @out EAX = child exit code (stored in g_last_exit_code), CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_BAD_COMMAND
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Resolves and launches .BAT/.CMD scripts, 16-bit .COM/MZ programs (via embedded vm8086!), and Win32/Win64 PE executables.
@proc_frame exec_external64, 248, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 248 = 40 + 13*16
    ; [rsp + 32..79]   : 10-arg CreateProcessW stack space
    ; [rsp + 88..191]  : STARTUPINFOW64 (104B, rbp)
    ; [rsp + 192..215] : PROCESS_INFORMATION64 (24B)
    ; [rsp + 216]      : exitCode scratch (dword)
    mov r12, rdx  ; raw argument tail
    mov r13d, r8d ; dwCreationFlags
    lea rbp, [rsp + 88]

    call resolve_executable64
    jc .not_found
    mov rbx, rax ; resolved path
    cmp dl, 1
    je .run_batch
    cmp dl, 2
    je .run_vm86
    cmp byte [rel g_force_vm86], 0
    jne .run_vm86

    mov ecx, W64_LINE_MAX
    call arena_alloc64
    jc .not_found
    mov r14, rax
    mov byte [r14], '"'
    lea rcx, [r14 + 1]
    mov rdx, rbx
    call str_copy64
    mov byte [rax], '"'
    lea rcx, [rax + 1]
    mov rdx, r12
    call str_copy64

    mov rdi, rbp
    xor eax, eax
    mov ecx, 16 ; 128 bytes (STARTUPINFOW64 104B + PROCESS_INFORMATION64 24B)
    rep stosq

    mov dword [rbp + STARTUPINFOW64.cb], 104
    mov dword [rbp + STARTUPINFOW64.dwFlags], STARTF_USESTDHANDLES
    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + STARTUPINFOW64.hStdInput], rax
    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + STARTUPINFOW64.hStdOutput], rax
    mov ecx, STD_ERROR_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + STARTUPINFOW64.hStdError], rax

    mov rcx, r14
    call wtf8_to_utf16le64
    jc .not_found
    mov rsi, rax ; cmdW

    mov rcx, rbx
    call nt64_path_to_w
    jc .not_found
    mov r15, rax ; appW

    call env_build_unicode_block64
    jc .not_found

    mov edx, r13d
    or edx, CREATE_UNICODE_ENVIRONMENT
    mov rcx, r15
    mov dword [rsp + 32], 1 ; bInheritHandles = TRUE
    mov dword [rsp + 40], edx
    mov qword [rsp + 48], rax
    mov qword [rsp + 56], 0
    mov qword [rsp + 64], rbp
    lea rax, [rbp + 104]
    mov qword [rsp + 72], rax
    mov rdx, rsi
    xor r8d, r8d
    xor r9d, r9d
    call qword [rel __imp_CreateProcessW]
    test eax, eax
    jnz .wait_child

    ; On 64-bit Windows (no NTVDM!) and 64-bit Wine, 16-bit DOS MZ .EXE / .COM fails with
    ; ERROR_BAD_EXE_FORMAT (193) or ERROR_EXE_MACHINE_TYPE_MISMATCH (216) -> execute natively in vm8086!
    call qword [rel __imp_GetLastError]
    cmp eax, 193
    je .run_vm86
    cmp eax, 216
    je .run_vm86
    call map_win32_to_err64
    jmp .err_out

.wait_child:
    mov rcx, qword [rbp + 104 + PROCESS_INFORMATION64.hThread]
    call qword [rel __imp_CloseHandle]
    mov rbx, qword [rbp + 104 + PROCESS_INFORMATION64.hProcess]
    mov rcx, rbx
    mov edx, INFINITE_WAIT
    call qword [rel __imp_WaitForSingleObject]
    mov dword [rsp + 216], 0
    mov rcx, rbx
    lea rdx, [rsp + 216]
    call qword [rel __imp_GetExitCodeProcess]
    mov edi, dword [rsp + 216]
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    mov eax, edi
    mov dword [rel g_last_exit_code], eax
    clc
    @epilog_ret

.run_vm86:
    mov rcx, rbx
    mov rdx, r12
    call vm86_run_image64
    jc .err_out
    mov dword [rel g_last_exit_code], eax
    clc
    @epilog_ret

.run_batch:
    mov rcx, rbx
    mov rdx, r12
    xor r8d, r8d
    call batch_run_file64
    jc .err_out
    mov dword [rel g_last_exit_code], eax
    clc
    @epilog_ret

.not_found:
    mov eax, ERR_BAD_COMMAND
.err_out:
    mov dword [rel g_last_exit_code], 9009
    stc
    @epilog_ret
@endproc
