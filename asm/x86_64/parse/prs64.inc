;; @file prs64.inc
;; @isa x64
;; @desc Context-sensitive Phase 2 Lexer & 64-Bit Arena AST Parser (blocks, IF, FOR, |, &&, ||, &, FD 0..9 redirs).

@cpu x64
@format inc

;; @proc prs_alloc_node64
;; @abi win64_ms
;; @in CL = bKind (AST_*)
;; @out RAX = zeroed 64-byte AST_NODE64* with bKind=CL, CF=0; or CF=1 on OOM
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Allocates and zero-initializes a 64-byte AST_NODE64 in the statement arena.
@proc_frame prs_alloc_node64, 40, rbx, rdi
    movzx ebx, cl
    mov ecx, AST_NODE64_SIZE
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov rdx, rax
    xor eax, eax
    mov ecx, 8
    rep stosq
    mov byte [rdx + AST_NODE64.bKind], bl
    mov rax, rdx
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc prs_skip_ws64
;; @abi win64_ms
;; @in RCX = string cursor
;; @out RAX = cursor advanced past ASCII space (0x20) and horizontal tab (0x09)
;; @err none
;; @clobbers rax
;; @desc Advances cursor past ASCII space and horizontal tab.
@proc prs_skip_ws64
    mov rax, rcx
.lp:
    cmp byte [rax], 0x20
    je .adv
    cmp byte [rax], 0x09
    je .adv
    ret
.adv:
    inc rax
    jmp .lp
@endproc

;; @proc prs_paren_balance64
;; @abi win64_ms
;; @in RCX = logical line buffer
;; @out EAX = net unquoted/unescaped '(' minus ')' count (> 0 means multi-line block needs more lines!)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Computes net unquoted parenthesis depth (ignoring REM and :: comment lines!).
@proc_frame prs_paren_balance64, 32, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '@'
    jne .chk_col
    lea rcx, [rsi + 1]
    call prs_skip_ws64
    mov rsi, rax
.chk_col:
    cmp byte [rsi], ':'
    je .zero
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'R'
    jne .scan_init
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'E'
    jne .scan_init
    mov al, byte [rsi + 2]
    call ascii_upper_al64
    cmp al, 'M'
    jne .scan_init
    mov al, byte [rsi + 3]
    cmp al, 0
    je .zero
    cmp al, 0x20
    je .zero
    cmp al, 0x09
    je .zero

.scan_init:
    xor ecx, ecx ; depth
    xor edx, edx ; in_quote
.lp:
    mov al, byte [rsi]
    test al, al
    jz .done
    cmp al, '"'
    jne .chk_caret
    xor edx, 1
    inc rsi
    jmp .lp
.chk_caret:
    test edx, edx
    jnz .adv
    cmp al, '^'
    jne .chk_par
    cmp byte [rsi + 1], 0
    je .done
    add rsi, 2
    jmp .lp
.chk_par:
    cmp al, '('
    jne .chk_close
    inc ecx
    jmp .adv
.chk_close:
    cmp al, ')'
    jne .adv
    test ecx, ecx
    jz .adv
    dec ecx
.adv:
    inc rsi
    jmp .lp

.zero:
    xor ecx, ecx
.done:
    mov eax, ecx
    @epilog_ret
@endproc

;; @proc prs_find_top_op64
;; @abi win64_ms
;; @in RCX = start, RDX = end, R8B = mode (0 = lowest '&'/LF, 1 = rightmost '&&'/'||', 2 = leftmost '|')
;; @out RAX = operator ptr in [RCX..RDX), DL = BINOP_*, R8D = operator char len (1 or 2); or RAX = 0 if none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Scans [RCX..RDX) outside quotes, carets, and nested (...) for binary command operators.
@proc_frame prs_find_top_op64, 40, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    mov rsi, rcx ; cur
    mov r15, rcx ; orig_start
    mov rdi, rdx ; end
    movzx ebp, r8b
    xor ebx, ebx ; recorded hit
    xor r12d, r12d ; recorded BINOP_*
    xor r13d, r13d ; recorded op len

    call prs_skip_ws64
    mov r9, rax
    cmp byte [r9], '@'
    jne .c_rem0
    lea rcx, [r9 + 1]
    call prs_skip_ws64
    mov r9, rax
.c_rem0:
    cmp byte [r9], ':'
    je .abort_rem
    mov al, byte [r9]
    call ascii_upper_al64
    cmp al, 'R'
    jne .resume_scan
    mov al, byte [r9 + 1]
    call ascii_upper_al64
    cmp al, 'E'
    jne .resume_scan
    mov al, byte [r9 + 2]
    call ascii_upper_al64
    cmp al, 'M'
    jne .resume_scan
    mov al, byte [r9 + 3]
    cmp al, 0
    je .abort_rem
    cmp al, 0x20
    je .abort_rem
    cmp al, 0x09
    je .abort_rem
    jmp .resume_scan

.abort_rem:
    xor eax, eax
    xor edx, edx
    xor r8d, r8d
    @epilog_ret

.resume_scan:
    xor r10d, r10d ; paren_depth
    xor r14d, r14d ; in_quote

.scan_lp:
    cmp rsi, rdi
    jae .finish
    mov al, byte [rsi]
    cmp al, '"'
    jne .s_caret
    xor r14d, 1
    inc rsi
    jmp .scan_lp

.s_caret:
    test r14d, r14d
    jnz .s_adv
    cmp al, '^'
    jne .s_par
    add rsi, 2
    jmp .scan_lp

.s_par:
    cmp al, '('
    jne .s_rpar
    inc r10d
    inc rsi
    jmp .scan_lp
.s_rpar:
    cmp al, ')'
    jne .s_ops
    test r10d, r10d
    jz .s_adv
    dec r10d
    inc rsi
    jmp .scan_lp

.s_ops:
    test r10d, r10d
    jnz .s_adv
    test r14d, r14d
    jnz .s_adv

    cmp al, '&'
    je .on_amp
    cmp al, '|'
    je .on_bar
    cmp al, 0x0A
    je .on_nl
    jmp .s_adv

.on_nl:
    cmp ebp, 0
    jne .s_adv
    mov rax, rsi
    mov dl, BINOP_SEQ
    mov r8d, 1
    @epilog_ret

.on_amp:
    cmp rsi, r15
    jbe .chk_dbl_amp
    mov cl, byte [rsi - 1]
    cmp cl, '>'
    je .s_adv
    cmp cl, '<'
    je .s_adv
.chk_dbl_amp:
    lea rax, [rsi + 1]
    cmp rax, rdi
    jae .single_amp
    cmp byte [rsi + 1], '&'
    jne .single_amp
    cmp ebp, 1
    jne .skip2
    mov rbx, rsi
    mov r12d, BINOP_AND
    mov r13d, 2
.skip2:
    add rsi, 2
    jmp .scan_lp

.single_amp:
    cmp ebp, 0
    jne .s_adv
    mov rax, rsi
    mov dl, BINOP_SEQ
    mov r8d, 1
    @epilog_ret

.on_bar:
    lea rax, [rsi + 1]
    cmp rax, rdi
    jae .single_bar
    cmp byte [rsi + 1], '|'
    jne .single_bar
    cmp ebp, 1
    jne .skip2
    mov rbx, rsi
    mov r12d, BINOP_OR
    mov r13d, 2
    add rsi, 2
    jmp .scan_lp

.single_bar:
    cmp ebp, 2
    jne .s_adv
    mov rax, rsi
    mov dl, BINOP_PIPE
    mov r8d, 1
    @epilog_ret

.s_adv:
    inc rsi
    jmp .scan_lp

.finish:
    mov rax, rbx
    mov edx, r12d
    mov r8d, r13d
    @epilog_ret
@endproc

;; @proc prs_extract_redirs64
;; @abi win64_ms
;; @in RCX = raw simple command slice (NUL-terminated)
;; @out RAX = cleaned command text in arena, RDX = linked list head of AST_REDIR64*, CF=0
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Extracts arbitrary leading/mid-line/trailing FD 0..9 redirections (<, >, >>, n>&m, n<&m) in left-to-right order.
@proc_frame prs_extract_redirs64, 56, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 56 = 40 + 16
    ; r12 = head AST_REDIR64*, r13 = tail AST_REDIR64*, r14 = cleaned_cmd base, r15d = in_quote
    mov rsi, rcx
    xor r12d, r12d
    xor r13d, r13d
    xor r15d, r15d

    call str_len64
    lea rcx, [rax + 8]
    call arena_alloc64
    jc .oom
    mov rdi, rax
    mov r14, rax

.lp:
    mov al, byte [rsi]
    test al, al
    jz .done
    cmp al, '"'
    jne .chk_caret
    xor r15d, 1
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.chk_caret:
    test r15d, r15d
    jnz .copy_ch
    cmp al, '^'
    jne .chk_digit_fd
    mov byte [rdi], al
    inc rsi
    inc rdi
    mov al, byte [rsi]
    test al, al
    jz .done
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.chk_digit_fd:
    cmp al, '0'
    jb .chk_bare_redir
    cmp al, '9'
    ja .chk_bare_redir
    mov dl, byte [rsi + 1]
    cmp dl, '>'
    je .verify_fd_boundary
    cmp dl, '<'
    jne .copy_ch
.verify_fd_boundary:
    cmp rdi, r14
    je .got_explicit_fd
    mov cl, byte [rdi - 1]
    cmp cl, 0x20
    je .got_explicit_fd
    cmp cl, 0x09
    jne .copy_ch
.got_explicit_fd:
    movzx ebx, al
    sub ebx, '0'
    inc rsi
    jmp .parse_redir_op

.chk_bare_redir:
    cmp al, '<'
    je .bare_in
    cmp al, '>'
    je .bare_out
    jmp .copy_ch

.bare_in:
    xor ebx, ebx
    jmp .parse_redir_op
.bare_out:
    mov ebx, 1

.parse_redir_op:
    mov al, byte [rsi]
    inc rsi
    mov ebp, REDIR_IN
    cmp al, '<'
    je .chk_dup
    mov ebp, REDIR_OUT_TRUNC
    cmp byte [rsi], '>'
    jne .chk_dup
    mov ebp, REDIR_OUT_APPEND
    inc rsi

.chk_dup:
    cmp byte [rsi], '&'
    jne .redir_target_file
    inc rsi
    movzx eax, byte [rsi]
    cmp al, '0'
    jb .syn_err
    cmp al, '9'
    ja .syn_err
    sub eax, '0'
    inc rsi
    mov dword [rsp + 32], eax ; bTargetFd
    mov qword [rsp + 40], 0   ; pTargetStr = NULL
    cmp ebp, REDIR_IN
    je .set_dupin
    mov ebp, REDIR_DUP_OUT
    jmp .alloc_rnode
.set_dupin:
    mov ebp, REDIR_DUP_IN
    jmp .alloc_rnode

.redir_target_file:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .syn_err

    mov ecx, 2048
    call arena_alloc64
    jc .oom
    mov r8, rax ; targetBuf
    mov r9, rax ; write cursor
    xor ecx, ecx ; quote state
.tf_lp:
    mov al, byte [rsi]
    test al, al
    jz .tf_done
    cmp al, '"'
    jne .tf_caret
    xor ecx, 1
    inc rsi
    jmp .tf_lp
.tf_caret:
    test ecx, ecx
    jnz .tf_emit
    cmp al, '^'
    jne .tf_delim
    inc rsi
    mov al, byte [rsi]
    test al, al
    jz .tf_done
    jmp .tf_emit
.tf_delim:
    cmp al, 0x20
    je .tf_done
    cmp al, 0x09
    je .tf_done
    cmp al, '<'
    je .tf_done
    cmp al, '>'
    je .tf_done
    cmp al, '&'
    je .tf_done
    cmp al, '|'
    je .tf_done
    cmp al, ')'
    je .tf_done
.tf_emit:
    mov byte [r9], al
    inc rsi
    inc r9
    jmp .tf_lp
.tf_done:
    mov byte [r9], 0
    cmp byte [r8], 0
    je .syn_err
    mov dword [rsp + 32], 0
    mov qword [rsp + 40], r8

.alloc_rnode:
    mov ecx, AST_REDIR64_SIZE
    call arena_alloc64
    jc .oom
    mov byte [rax + AST_REDIR64.bKind], bpl
    mov byte [rax + AST_REDIR64.bSrcFd], bl
    mov edx, dword [rsp + 32]
    mov byte [rax + AST_REDIR64.bTargetFd], dl
    mov byte [rax + AST_REDIR64.bPad], 0
    mov dword [rax + AST_REDIR64.dwPad], 0
    mov rdx, qword [rsp + 40]
    mov qword [rax + AST_REDIR64.pTargetStr], rdx
    mov qword [rax + AST_REDIR64.pNext], 0

    test r12, r12
    jnz .link_tail
    mov r12, rax
    mov r13, rax
    jmp .lp
.link_tail:
    mov qword [r13 + AST_REDIR64.pNext], rax
    mov r13, rax
    jmp .lp

.copy_ch:
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.done:
    mov byte [rdi], 0
    mov rax, r14
    mov rdx, r12
    clc
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
    stc
    @epilog_ret

.oom:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc prs_read_word64
;; @abi win64_ms
;; @in RCX = cursor
;; @out RAX = arena token string, RDX = next cursor, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads one whitespace-delimited token (respecting "..." and ^ escapes) from RCX.
@proc_frame prs_read_word64, 40, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .empty_err
    mov rbx, rax
    xor ecx, ecx
.scan:
    mov al, byte [rbx]
    test al, al
    jz .got
    cmp al, '"'
    jne .sc_c
    xor ecx, 1
    inc rbx
    jmp .scan
.sc_c:
    test ecx, ecx
    jnz .sc_adv
    cmp al, '^'
    jne .sc_ws
    cmp byte [rbx + 1], 0
    je .got
    add rbx, 2
    jmp .scan
.sc_ws:
    cmp al, 0x20
    je .got
    cmp al, 0x09
    je .got
.sc_adv:
    inc rbx
    jmp .scan

.got:
    mov r12, rbx
    sub r12, rsi
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r8, rax
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rax, r8
    mov rdx, rbx
    clc
    @epilog_ret
.empty_err:
    mov eax, ERR_SYNTAX
.fail:
    stc
    @epilog_ret
@endproc

;; @proc prs_parse_stmt_end64
;; @abi win64_ms
;; @in RCX = start of statement, EDX = stop_at_else (0/1)
;; @out RAX = end pointer (stops at top-level ')' or EOL, or top-level ' ELSE ' when EDX=1)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Scans to end of a sub-statement inside IF / FOR / block, stopping before top-level ELSE when requested.
@proc_frame prs_parse_stmt_end64, 40, rbx, rsi, r12, r13
    mov rsi, rcx
    mov ebx, edx
    xor r12d, r12d ; paren_depth
    xor r13d, r13d ; in_quote
.lp:
    mov al, byte [rsi]
    test al, al
    jz .done
    cmp al, '"'
    jne .c_car
    xor r13d, 1
    inc rsi
    jmp .lp
.c_car:
    test r13d, r13d
    jnz .adv
    cmp al, '^'
    jne .c_lp
    cmp byte [rsi + 1], 0
    je .done
    add rsi, 2
    jmp .lp
.c_lp:
    cmp al, '('
    jne .c_rp
    inc r12d
    inc rsi
    jmp .lp
.c_rp:
    cmp al, ')'
    jne .c_else
    test r12d, r12d
    jz .done
    dec r12d
    inc rsi
    jmp .lp
.c_else:
    test ebx, ebx
    jz .adv
    test r12d, r12d
    jnz .adv
    cmp al, 0x20
    je .try_else_kw
    cmp al, 0x09
    jne .adv
.try_else_kw:
    mov rcx, rsi
    call prs_skip_ws64
    mov r8, rax
    mov al, byte [r8]
    call ascii_upper_al64
    cmp al, 'E'
    jne .adv
    mov al, byte [r8 + 1]
    call ascii_upper_al64
    cmp al, 'L'
    jne .adv
    mov al, byte [r8 + 2]
    call ascii_upper_al64
    cmp al, 'S'
    jne .adv
    mov al, byte [r8 + 3]
    call ascii_upper_al64
    cmp al, 'E'
    jne .adv
    mov al, byte [r8 + 4]
    cmp al, 0x20
    je .done
    cmp al, 0x09
    je .done
    cmp al, '('
    je .done

.adv:
    inc rsi
    jmp .lp
.done:
    mov rax, rsi
    @epilog_ret
@endproc

;; @proc prs_parse_if64
;; @abi win64_ms
;; @in RCX = cursor right after "IF" keyword, DL = quiet flag
;; @out RAX = AST_NODE64* (AST_IF), CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses IF [NOT] [/I] (ERRORLEVEL|EXIST|DEFINED|CMDEXTVERSION|==|EQU..GEQ) <then> [ELSE <else>].
@proc_frame prs_parse_if64, 48, rbp, rbx, rsi, rdi, r12
    mov rsi, rcx
    movzx ebx, dl

    mov cl, AST_IF
    call prs_alloc_node64
    jc .fail
    mov rbp, rax
    or byte [rbp + AST_NODE64.bFlags], bl

.flag_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '/'
    jne .chk_not
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'I'
    jne .chk_not
    mov al, byte [rsi + 2]
    cmp al, 0x20
    je .set_icase
    cmp al, 0x09
    jne .chk_not
.set_icase:
    or byte [rbp + AST_NODE64.bFlags], IFFLAG_NOCASE
    add rsi, 2
    jmp .flag_lp

.chk_not:
    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov r12, rdx
    mov rcx, rax
    lea rdx, [rel szKwNot64]
    call str_eq_i64
    test eax, eax
    jz .parse_cond
    or byte [rbp + AST_NODE64.bFlags], IFFLAG_NOT
    mov rsi, r12
    jmp .flag_lp

.parse_cond:
    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov rbx, rax ; word1
    mov rdi, rdx ; cursor after word1

    mov rcx, rbx
    lea rdx, [rel szKwErrorlevel64]
    call str_eq_i64
    test eax, eax
    jnz .if_errlvl

    mov rcx, rbx
    lea rdx, [rel szKwExist64]
    call str_eq_i64
    test eax, eax
    jnz .if_exist

    mov rcx, rbx
    lea rdx, [rel szKwDefined64]
    call str_eq_i64
    test eax, eax
    jnz .if_defined

    mov rcx, rbx
    lea rdx, [rel szKwCmdExtVer64]
    call str_eq_i64
    test eax, eax
    jnz .if_extver

    mov rsi, rbx
.scan_inline_eq:
    mov al, byte [rsi]
    test al, al
    jz .two_tok_cmp
    cmp al, '='
    jne .adv_ineq
    cmp byte [rsi + 1], '='
    je .split_inline_eq
.adv_ineq:
    inc rsi
    jmp .scan_inline_eq

.split_inline_eq:
    mov byte [rsi], 0
    mov qword [rbp + AST_NODE64.pStr1], rbx
    mov byte [rbp + AST_NODE64.bSub], IFCMP_STR_EQ
    add rsi, 2
    cmp byte [rsi], 0
    jne .inline_has_rhs
    mov rcx, rdi
    call prs_read_word64
    jc .fail
    mov qword [rbp + AST_NODE64.pStr2], rax
    mov rsi, rdx
    jmp .parse_then
.inline_has_rhs:
    mov rcx, rsi
    call arena_strdup64
    jc .fail
    mov qword [rbp + AST_NODE64.pStr2], rax
    mov rsi, rdi
    jmp .parse_then

.two_tok_cmp:
    mov qword [rbp + AST_NODE64.pStr1], rbx
    mov rcx, rdi
    call prs_read_word64
    jc .fail
    mov rbx, rax ; op word
    mov rsi, rdx ; cursor after op

    cmp byte [rbx], '='
    jne .chk_ext_ops
    cmp byte [rbx + 1], '='
    jne .syn_err
    mov byte [rbp + AST_NODE64.bSub], IFCMP_STR_EQ
    cmp byte [rbx + 2], 0
    je .read_rhs_word
    lea rcx, [rbx + 2]
    call arena_strdup64
    jc .fail
    mov qword [rbp + AST_NODE64.pStr2], rax
    jmp .parse_then

.chk_ext_ops:
    mov rcx, rbx
    lea rdx, [rel szKwEqu64]
    call str_eq_i64
    test eax, eax
    jnz .op_equ
    mov rcx, rbx
    lea rdx, [rel szKwNeq64]
    call str_eq_i64
    test eax, eax
    jnz .op_neq
    mov rcx, rbx
    lea rdx, [rel szKwLss64]
    call str_eq_i64
    test eax, eax
    jnz .op_lss
    mov rcx, rbx
    lea rdx, [rel szKwLeq64]
    call str_eq_i64
    test eax, eax
    jnz .op_leq
    mov rcx, rbx
    lea rdx, [rel szKwGtr64]
    call str_eq_i64
    test eax, eax
    jnz .op_gtr
    mov rcx, rbx
    lea rdx, [rel szKwGeq64]
    call str_eq_i64
    test eax, eax
    jnz .op_geq
    jmp .syn_err

.op_equ:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_EQU
    jmp .read_rhs_word
.op_neq:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_NEQ
    jmp .read_rhs_word
.op_lss:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_LSS
    jmp .read_rhs_word
.op_leq:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_LEQ
    jmp .read_rhs_word
.op_gtr:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_GTR
    jmp .read_rhs_word
.op_geq:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_GEQ

.read_rhs_word:
    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov qword [rbp + AST_NODE64.pStr2], rax
    mov rsi, rdx
    jmp .parse_then

.if_errlvl:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_ERRORLEVEL
    jmp .unary_if_arg
.if_exist:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_EXIST
    jmp .unary_if_arg
.if_defined:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_DEFINED
    jmp .unary_if_arg
.if_extver:
    mov byte [rbp + AST_NODE64.bSub], IFCMP_CMDEXTVER
.unary_if_arg:
    mov rcx, rdi
    call prs_read_word64
    jc .fail
    mov qword [rbp + AST_NODE64.pStr1], rax
    mov rsi, rdx

.parse_then:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .syn_err

    cmp byte [rsi], '('
    jne .unparen_then
    mov rbx, rsi
    xor ecx, ecx
    xor edx, edx
.bal_then:
    mov al, byte [rbx]
    test al, al
    jz .syn_err
    cmp al, '"'
    jne .bt_car
    xor edx, 1
    inc rbx
    jmp .bal_then
.bt_car:
    test edx, edx
    jnz .bt_adv
    cmp al, '^'
    jne .bt_lp
    cmp byte [rbx + 1], 0
    je .syn_err
    add rbx, 2
    jmp .bal_then
.bt_lp:
    cmp al, '('
    jne .bt_rp
    inc ecx
    inc rbx
    jmp .bal_then
.bt_rp:
    cmp al, ')'
    jne .bt_adv
    dec ecx
    inc rbx
    test ecx, ecx
    jz .got_paren_then
    jmp .bal_then
.bt_adv:
    inc rbx
    jmp .bal_then

.got_paren_then:
    mov rcx, rsi
    mov rdx, rbx
    call prs_parse_slice64
    jc .fail
    mov qword [rbp + AST_NODE64.pChild1], rax
    mov rsi, rbx
    jmp .check_else

.unparen_then:
    mov rcx, rsi
    mov edx, 1
    call prs_parse_stmt_end64
    mov rbx, rax
    mov rcx, rsi
    mov rdx, rbx
    call prs_parse_slice64
    jc .fail
    mov qword [rbp + AST_NODE64.pChild1], rax
    mov rsi, rbx

.check_else:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .ok
    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov r12, rdx
    mov rcx, rax
    lea rdx, [rel szKwElse64]
    call str_eq_i64
    test eax, eax
    jz .ok
    mov rcx, r12
    call prs_skip_ws64
    mov rsi, rax
    mov rcx, rsi
    call str_len64
    mov rcx, rsi
    lea rdx, [rsi + rax]
    call prs_parse_slice64
    jc .fail
    mov qword [rbp + AST_NODE64.pChild2], rax

.ok:
    mov rax, rbp
    clc
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
    stc
.fail:
    @epilog_ret
@endproc

;; @proc prs_parse_for64
;; @abi win64_ms
;; @in RCX = cursor right after "FOR" keyword, DL = quiet flag
;; @out RAX = AST_NODE64* (AST_FOR), CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses FOR [/D | /R [root] | /L | /F ["opts"]] %V IN (set) DO <body>.
@proc_frame prs_parse_for64, 48, rbp, rbx, rsi, rdi, r12
    mov rsi, rcx
    movzx ebx, dl

    mov cl, AST_FOR
    call prs_alloc_node64
    jc .fail
    mov rbp, rax
    or byte [rbp + AST_NODE64.bFlags], bl
    mov byte [rbp + AST_NODE64.bSub], FORMODE_PLAIN

.sw_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '/'
    jne .read_var
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    add rsi, 2
    cmp al, 'D'
    je .sw_d
    cmp al, 'L'
    je .sw_l
    cmp al, 'R'
    je .sw_r
    cmp al, 'F'
    je .sw_f
    jmp .syn_err

.sw_d:
    mov byte [rbp + AST_NODE64.bSub], FORMODE_DIRS
    jmp .sw_lp
.sw_l:
    mov byte [rbp + AST_NODE64.bSub], FORMODE_NUMERIC
    jmp .sw_lp
.sw_r:
    mov byte [rbp + AST_NODE64.bSub], FORMODE_RECURSE
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '%'
    je .read_var
    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov qword [rbp + AST_NODE64.pExtra], rax
    mov rsi, rdx
    jmp .read_var

.sw_f:
    mov byte [rbp + AST_NODE64.bSub], FORMODE_PARSE
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '%'
    je .read_var
    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov qword [rbp + AST_NODE64.pExtra], rax
    mov rsi, rdx

.read_var:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '%'
    jne .syn_err
    inc rsi
    cmp byte [rsi], '%'
    jne .got_vch
    inc rsi
.got_vch:
    mov al, byte [rsi]
    test al, al
    jz .syn_err
    mov byte [rbp + AST_NODE64.bVarChar], al
    inc rsi

    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov r12, rdx
    mov rcx, rax
    lea rdx, [rel szKwIn64]
    call str_eq_i64
    test eax, eax
    jz .syn_err
    mov rsi, r12

    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '('
    jne .syn_err
    inc rsi
    mov rbx, rsi
    mov ecx, 1
    xor edx, edx
.set_lp:
    mov al, byte [rbx]
    test al, al
    jz .syn_err
    cmp al, '"'
    jne .st_rp
    xor edx, 1
    inc rbx
    jmp .set_lp
.st_rp:
    test edx, edx
    jnz .st_adv
    cmp al, '('
    jne .st_cl
    inc ecx
    inc rbx
    jmp .set_lp
.st_cl:
    cmp al, ')'
    jne .st_adv
    dec ecx
    jz .got_set
.st_adv:
    inc rbx
    jmp .set_lp

.got_set:
    mov r12, rbx
    sub r12, rsi
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov qword [rbp + AST_NODE64.pStr1], rax
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    lea rsi, [rbx + 1]

    mov rcx, rsi
    call prs_read_word64
    jc .fail
    mov r12, rdx
    mov rcx, rax
    lea rdx, [rel szKwDo64]
    call str_eq_i64
    test eax, eax
    jz .syn_err
    mov rsi, r12

    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .syn_err
    mov rcx, rsi
    call str_len64
    mov rcx, rsi
    lea rdx, [rsi + rax]
    call prs_parse_slice64
    jc .fail
    mov qword [rbp + AST_NODE64.pChild1], rax

    mov rax, rbp
    clc
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
    stc
.fail:
    @epilog_ret
@endproc

;; @proc prs_parse_slice64
;; @abi win64_ms
;; @in RCX = slice start, RDX = slice end
;; @out RAX = AST_NODE64*, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Recursive-descent Phase 2 AST builder over [RCX..RDX) for &, &&/||, |, (...), IF, FOR, and simple commands.
@proc_frame prs_parse_slice64, 48, rbp, rbx, rsi, rdi, r12, r13, r14
    ; 7 saved regs (odd) -> 48 = 32 + 16
    mov rsi, rcx
    mov rdi, rdx

.trim_l:
    cmp rsi, rdi
    jae .empty_nop
    mov al, byte [rsi]
    cmp al, 0x20
    je .adv_l
    cmp al, 0x09
    je .adv_l
    cmp al, 0x0D
    je .adv_l
    cmp al, 0x0A
    jne .trim_r
.adv_l:
    inc rsi
    jmp .trim_l

.trim_r:
    cmp rdi, rsi
    jbe .empty_nop
    mov al, byte [rdi - 1]
    cmp al, 0x20
    je .dec_r
    cmp al, 0x09
    je .dec_r
    cmp al, 0x0D
    je .dec_r
    cmp al, 0x0A
    jne .check_binops
.dec_r:
    dec rdi
    jmp .trim_r

.check_binops:
    mov rcx, rsi
    mov rdx, rdi
    xor r8d, r8d
    call prs_find_top_op64
    test rax, rax
    jnz .build_binop

    mov rcx, rsi
    mov rdx, rdi
    mov r8b, 1
    call prs_find_top_op64
    test rax, rax
    jnz .build_binop

    mov rcx, rsi
    mov rdx, rdi
    mov r8b, 2
    call prs_find_top_op64
    test rax, rax
    jnz .build_binop

    mov r12, rdi
    sub r12, rsi
    lea rcx, [r12 + 1]
    call arena_alloc64
    jc .fail
    mov r13, rax
    mov rdi, rax
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    mov rsi, r13

    xor ebx, ebx ; quiet flag
.at_lp:
    cmp byte [rsi], '@'
    jne .after_at
    mov bl, AST_FLAG_QUIET
    lea rcx, [rsi + 1]
    call prs_skip_ws64
    mov rsi, rax
    jmp .at_lp

.after_at:
    cmp byte [rsi], 0
    je .empty_nop
    cmp byte [rsi], ':'
    je .empty_nop
    cmp byte [rsi], '('
    je .parse_block

    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'R'
    jne .chk_if_for
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'E'
    jne .chk_if_for
    mov al, byte [rsi + 2]
    call ascii_upper_al64
    cmp al, 'M'
    jne .chk_if_for
    mov al, byte [rsi + 3]
    cmp al, 0
    je .empty_nop
    cmp al, 0x20
    je .empty_nop
    cmp al, 0x09
    je .empty_nop
    cmp al, '.'
    je .empty_nop
    cmp al, '/'
    je .empty_nop

.chk_if_for:
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'I'
    jne .chk_for_kw
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'F'
    jne .chk_for_kw
    mov al, byte [rsi + 2]
    cmp al, 0x20
    je .go_if
    cmp al, 0x09
    je .go_if
    cmp al, '/'
    jne .chk_for_kw
.go_if:
    lea rcx, [rsi + 2]
    mov dl, bl
    call prs_parse_if64
    @epilog_ret

.chk_for_kw:
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'F'
    jne .simple_cmd
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'O'
    jne .simple_cmd
    mov al, byte [rsi + 2]
    call ascii_upper_al64
    cmp al, 'R'
    jne .simple_cmd
    mov al, byte [rsi + 3]
    cmp al, 0x20
    je .go_for
    cmp al, 0x09
    je .go_for
    cmp al, '/'
    jne .simple_cmd
.go_for:
    lea rcx, [rsi + 3]
    mov dl, bl
    call prs_parse_for64
    @epilog_ret

.parse_block:
    lea r12, [rsi + 1]
    mov edx, 1 ; depth
    xor ecx, ecx ; quote
.blk_scan:
    mov al, byte [r12]
    test al, al
    jz .syn_err
    cmp al, '"'
    jne .bk_lp
    xor ecx, 1
    inc r12
    jmp .blk_scan
.bk_lp:
    test ecx, ecx
    jnz .bk_adv
    cmp al, '('
    jne .bk_rp
    inc edx
    inc r12
    jmp .blk_scan
.bk_rp:
    cmp al, ')'
    jne .bk_adv
    dec edx
    jz .got_blk_end
.bk_adv:
    inc r12
    jmp .blk_scan

.got_blk_end:
    mov cl, AST_BLOCK
    call prs_alloc_node64
    jc .fail
    mov rbp, rax
    mov qword [rbp + AST_NODE64.pStr1], r13
    and bl, AST_FLAG_QUIET
    or byte [rbp + AST_NODE64.bFlags], bl

    lea rcx, [rsi + 1]
    mov rdx, r12
    call prs_parse_slice64
    jc .fail
    mov qword [rbp + AST_NODE64.pChild1], rax

    lea rcx, [r12 + 1]
    call prs_extract_redirs64
    jc .fail
    mov qword [rbp + AST_NODE64.pRedir], rdx
    mov rax, rbp
    clc
    @epilog_ret

.simple_cmd:
    mov rbp, rsi
    mov rcx, rsi
    call prs_extract_redirs64
    jc .fail
    mov rdi, rdx ; pRedir
    mov rcx, rax
    call prs_skip_ws64
    mov rsi, rax

    mov cl, AST_SIMPLE_CMD
    call prs_alloc_node64
    jc .fail
    and bl, AST_FLAG_QUIET
    or byte [rax + AST_NODE64.bFlags], bl
    mov qword [rax + AST_NODE64.pStr1], rsi
    mov qword [rax + AST_NODE64.pStr2], rbp
    mov qword [rax + AST_NODE64.pRedir], rdi
    clc
    @epilog_ret

.build_binop:
    ; RAX = op_pos, DL = BINOP_*, R8D = op_len
    mov rbx, rax
    mov r12d, edx
    mov r13d, r8d

    mov cl, AST_BINARY
    call prs_alloc_node64
    jc .fail
    mov rbp, rax
    mov byte [rbp + AST_NODE64.bSub], r12b

    mov r14, rdi
    sub r14, rsi
    lea rcx, [r14 + 1]
    call arena_alloc64
    jc .fail
    mov qword [rbp + AST_NODE64.pStr1], rax
    mov r8, rsi
    mov r9, rdi
    mov rdi, rax
    mov rcx, r14
    rep movsb
    mov byte [rdi], 0
    mov rsi, r8
    mov rdi, r9

    mov rcx, rsi
    mov rdx, rbx
    call prs_parse_slice64
    jc .fail
    mov qword [rbp + AST_NODE64.pChild1], rax

    lea rcx, [rbx + r13]
    mov rdx, rdi
    call prs_parse_slice64
    jc .fail
    mov qword [rbp + AST_NODE64.pChild2], rax
    mov rax, rbp
    clc
    @epilog_ret

.empty_nop:
    mov cl, AST_NOP
    call prs_alloc_node64
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
    stc
.fail:
    @epilog_ret
@endproc

;; @proc parse_line_to_ast64
;; @abi win64_ms
;; @in RCX = NUL-terminated Phase-1-expanded command line
;; @out RAX = root AST_NODE64*, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entry point parsing a complete logical line or multi-line compound block into a 64-bit Arena AST.
@proc_frame parse_line_to_ast64, 32, rsi
    mov rsi, rcx
    call str_len64
    mov rcx, rsi
    lea rdx, [rsi + rax]
    call prs_parse_slice64
    @epilog_ret
@endproc
