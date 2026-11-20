;; @file prs_ast.inc
;; @isa 8086
;; @desc Phase 2b recursive-descent Arena AST parser (blocks, IF/ELSE, FOR, |, &&, ||, &, FD 0..9).

@cpu 8086
@format inc

;; @proc prs_alloc_node
;; @abi dos16
;; @in AL = AST_* kind, AH = subkind
;; @out DI = zeroed 16-byte AST_NODE in arena with bKind=AL, bSub=AH, CF=0
;; @err CF=1, AX=ERR_OUT_OF_MEMORY on arena exhaustion
;; @clobbers ax, cx, di
;; @desc Allocates and zero-initializes a 16-byte AST_NODE from the statement arena.
@proc prs_alloc_node
    push ax
    mov cx, 16
    call arena_alloc
    jc .oom
    push di
    mov cx, 8
    xor ax, ax
    rep stosw
    pop di
    pop ax
    mov byte [di + AST_NODE.bKind], al
    mov byte [di + AST_NODE.bSub], ah
    clc
    ret
.oom:
    pop cx
    stc
    ret
@endproc

;; @proc prs_strlen_si
;; @abi dos16
;; @in DS:SI = ASCIIZ string
;; @out CX = byte length (DS:SI preserved)
;; @err none
;; @clobbers cx
;; @desc Computes ASCIIZ length in CX while preserving SI.
@proc prs_strlen_si
    push si
    xor cx, cx
.lp:
    cmp byte [si], 0
    je .dn
    inc si
    inc cx
    jmp .lp
.dn:
    pop si
    ret
@endproc

;; @proc prs_try_redir
;; @abi dos16
;; @in DS:SI = cursor (at potential '<', '>', or '0'..'9' followed by '<'/'>')
;; @out DI = AST_REDIR_NODE (8B), DS:SI advanced past target, CF=0; or CF=1 if not a redir
;; @err CF=1 if not a redirection (AX=0) or syntax error (AX=ERR_SYNTAX)
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Parses '<file', '>file', '>>file', 'n>file', 'n>&m', 'n<&m' into an AST_REDIR_NODE.
@proc prs_try_redir
    mov al, byte [si]
    cmp al, '<'
    je .in_fd0
    cmp al, '>'
    je .out_fd1
    cmp al, '0'
    jb .not_redir
    cmp al, '9'
    ja .not_redir
    mov bl, byte [si + 1]
    cmp bl, '<'
    je .digit_in
    cmp bl, '>'
    je .digit_out
.not_redir:
    xor ax, ax
    stc
    ret

.in_fd0:
    xor dl, dl
    inc si
    jmp .parse_in_body
.digit_in:
    mov dl, al
    sub dl, '0'
    add si, 2
.parse_in_body:
    mov dh, REDIR_IN
    cmp byte [si], '&'
    jne .redir_file_target
    mov dh, REDIR_DUP_IN
    inc si
    jmp .redir_dup_digit

.out_fd1:
    mov dl, 1
    inc si
    jmp .parse_out_body
.digit_out:
    mov dl, al
    sub dl, '0'
    add si, 2
.parse_out_body:
    mov dh, REDIR_OUT_TRUNC
    cmp byte [si], '>'
    jne .chk_out_dup
    mov dh, REDIR_OUT_APPEND
    inc si
    jmp .redir_file_target
.chk_out_dup:
    cmp byte [si], '&'
    jne .redir_file_target
    mov dh, REDIR_DUP_OUT
    inc si

.redir_dup_digit:
    mov al, byte [si]
    cmp al, '0'
    jb .syn_fail
    cmp al, '9'
    ja .syn_fail
    sub al, '0'
    xor ah, ah
    inc si
    push si
    push dx
    push ax
    mov cx, 8
    call arena_alloc
    pop ax
    pop dx
    pop si
    jc .syn_fail
    mov byte [di + AST_REDIR_NODE.bKind], dh
    mov byte [di + AST_REDIR_NODE.bFd], dl
    mov word [di + AST_REDIR_NODE.wTarget], ax
    mov word [di + AST_REDIR_NODE.wNext], 0
    clc
    ret

.redir_file_target:
    push dx
    mov cx, 136
    call arena_alloc
    pop dx
    jc .syn_fail
    push dx
    call lex_read_word
    pop dx
    jc .syn_fail
    push si
    mov si, di
    mov bx, di
    cmp byte [si], '"'
    jne .cp_rf
    inc si
.cp_rf:
    mov al, byte [si]
    test al, al
    jz .rf_end
    cmp al, '"'
    je .rf_skq
    mov byte [bx], al
    inc bx
.rf_skq:
    inc si
    jmp .cp_rf
.rf_end:
    mov byte [bx], 0
    mov bx, di
    mov cx, 8
    call arena_alloc
    pop si
    jc .syn_fail
    mov byte [di + AST_REDIR_NODE.bKind], dh
    mov byte [di + AST_REDIR_NODE.bFd], dl
    mov word [di + AST_REDIR_NODE.wTarget], bx
    mov word [di + AST_REDIR_NODE.wNext], 0
    clc
    ret

.syn_fail:
    mov ax, ERR_SYNTAX
    stc
    ret
@endproc

;; @proc prs_append_redir
;; @abi dos16
;; @in BX = address of head pointer word, DI = new AST_REDIR_NODE
;; @out Appends DI to end of singly-linked AST_REDIR_NODE list
;; @err none
;; @clobbers bx
;; @desc Preserves strict left-to-right redirection evaluation order.
@proc prs_append_redir
    cmp word [bx], 0
    jne .walk
    mov word [bx], di
    ret
.walk:
    mov bx, word [bx]
    add bx, AST_REDIR_NODE.wNext
    jmp prs_append_redir
@endproc

;; @proc prs_parse_line
;; @abi dos16
;; @in DS:SI = Phase-1 expanded command/block buffer
;; @out DI = root AST_NODE, CF=0 on success
;; @err CF=1, AX = ERR_SYNTAX or ERR_OUT_OF_MEMORY
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint parsing a complete statement list (&, &&, ||, |, blocks, IF, FOR).
@proc prs_parse_line
    call prs_parse_seq
    ret
@endproc

;; @proc prs_parse_block_list
;; @abi dos16
;; @in DS:SI = cursor immediately after '('
;; @out DI = head AST_NODE linked via AST_NODE.wNext, DS:SI advanced past matching ')', CF=0
;; @err CF=1, AX = ERR_SYNTAX on missing ')' or inner syntax error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Parses newline- or &-separated statements inside (...) compound blocks.
@proc prs_parse_block_list
    push bp
    mov bp, sp
    sub sp, 4
    ; [bp-2]=head, [bp-4]=tail_node
    mov word [bp - 2], 0
    mov word [bp - 4], 0

.blk_lp:
    call lex_skip_hspace
    mov al, byte [si]
    cmp al, 0x0D
    je .eat_nl
    cmp al, 0x0A
    je .eat_nl
    cmp al, ')'
    je .got_rparen
    test al, al
    jz .missing_rparen

    call prs_parse_seq
    jc .blk_err
    cmp word [bp - 2], 0
    jne .link_tail
    mov word [bp - 2], di
    mov word [bp - 4], di
    jmp .blk_lp
.link_tail:
    mov bx, word [bp - 4]
    mov word [bx + AST_NODE.wNext], di
    mov word [bp - 4], di
    jmp .blk_lp

.eat_nl:
    inc si
    jmp .blk_lp

.got_rparen:
    inc si
    mov di, word [bp - 2]
    test di, di
    jnz .blk_ok
    push si
    mov al, AST_NOP
    xor ah, ah
    call prs_alloc_node
    pop si
    jc .blk_err
.blk_ok:
    add sp, 4
    pop bp
    clc
    ret

.missing_rparen:
    mov ax, ERR_SYNTAX
.blk_err:
    add sp, 4
    pop bp
    stc
    ret
@endproc

;; @proc prs_parse_seq
;; @abi dos16
;; @in DS:SI = cursor
;; @out DI = AST_NODE (chain of BINOP_SEQ '&' over prs_parse_cond), DS:SI advanced, CF=0
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Lowest-precedence tier parsing 'stmt1 & stmt2'.
@proc prs_parse_seq
    call prs_parse_cond
    jc .fail
.seq_lp:
    push di
    call lex_skip_hspace
    call lex_peek_op
    cmp al, TOK_AMP
    jne .done_seq
    inc si
    call prs_parse_cond
    jc .pop_fail
    mov dx, di
    pop bx
    push si
    mov al, AST_BINARY
    mov ah, BINOP_SEQ
    call prs_alloc_node
    pop si
    jc .fail
    mov word [di + AST_NODE.wA], bx
    mov word [di + AST_NODE.wB], dx
    jmp .seq_lp

.done_seq:
    pop di
    clc
    ret
.pop_fail:
    pop bx
.fail:
    stc
    ret
@endproc

;; @proc prs_parse_cond
;; @abi dos16
;; @in DS:SI = cursor
;; @out DI = AST_NODE (left-associative '&&' and '||' over prs_parse_pipe), CF=0
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Middle-precedence tier parsing 'cmd1 && cmd2 || cmd3'.
@proc prs_parse_cond
    call prs_parse_pipe
    jc .fail
.cond_lp:
    push di
    call lex_skip_hspace
    call lex_peek_op
    cmp al, TOK_AND
    je .got_and
    cmp al, TOK_OR
    je .got_or
    pop di
    clc
    ret

.got_and:
    mov cl, BINOP_AND
    jmp .eat_cond
.got_or:
    mov cl, BINOP_OR
.eat_cond:
    add si, 2
    push cx
    call prs_parse_pipe
    pop cx
    jc .pop_fail
    mov dx, di
    pop bx
    push si
    mov al, AST_BINARY
    mov ah, cl
    call prs_alloc_node
    pop si
    jc .fail
    mov word [di + AST_NODE.wA], bx
    mov word [di + AST_NODE.wB], dx
    jmp .cond_lp

.pop_fail:
    pop bx
.fail:
    stc
    ret
@endproc

;; @proc prs_parse_pipe
;; @abi dos16
;; @in DS:SI = cursor
;; @out DI = AST_NODE (left-associative '|' over prs_parse_primary), CF=0
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Highest binary connector tier parsing 'cmd1 | cmd2 | cmd3'.
@proc prs_parse_pipe
    call prs_parse_primary
    jc .fail
.pipe_lp:
    push di
    call lex_skip_hspace
    call lex_peek_op
    cmp al, TOK_PIPE
    jne .done_pipe
    inc si
    call prs_parse_primary
    jc .pop_fail
    mov dx, di
    pop bx
    push si
    mov al, AST_BINARY
    mov ah, BINOP_PIPE
    call prs_alloc_node
    pop si
    jc .fail
    mov word [di + AST_NODE.wA], bx
    mov word [di + AST_NODE.wB], dx
    jmp .pipe_lp

.done_pipe:
    pop di
    clc
    ret
.pop_fail:
    pop bx
.fail:
    stc
    ret
@endproc

;; @proc prs_parse_primary
;; @abi dos16
;; @in DS:SI = cursor
;; @out DI = AST_NODE (AST_BLOCK, AST_IF, AST_FOR, AST_NOP, or AST_SIMPLE_CMD), CF=0
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Parses leading '@', leading redirections, (...), IF, FOR, REM, :label, or simple command.
@proc prs_parse_primary
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=quiet_at, [bp-4]=redir_head, [bp-6]=word_buf, [bp-8]=pre_word_si
    mov word [bp - 2], 0
    mov word [bp - 4], 0

.eat_at:
    call lex_skip_hspace
    cmp al, '@'
    jne .chk_label
    mov word [bp - 2], 1
    inc si
    jmp .eat_at

.chk_label:
    cmp al, ':'
    jne .lead_redirs
.sk_lbl_ln:
    mov al, byte [si]
    test al, al
    jz .ret_nop
    cmp al, 0x0D
    je .ret_nop
    cmp al, 0x0A
    je .ret_nop
    inc si
    jmp .sk_lbl_ln
.ret_nop:
    push si
    mov al, AST_NOP
    xor ah, ah
    call prs_alloc_node
    pop si
    add sp, 8
    pop bp
    ret

.lead_redirs:
    call lex_skip_hspace
    call prs_try_redir
    jc .after_lead_redir
    mov bx, bp
    sub bx, 4
    call prs_append_redir
    jmp .lead_redirs
.after_lead_redir:
    test ax, ax
    jz .chk_open_blk
    jmp .prs_err

.chk_open_blk:
    call lex_skip_hspace
    cmp al, '('
    jne .chk_kw_head

    inc si
    call prs_parse_block_list
    jc .j_prs_err1
    mov dx, di

.trail_blk_redir:
    push dx
    call lex_skip_hspace
    call prs_try_redir
    pop dx
    jc .finish_blk_node
    push dx
    mov bx, bp
    sub bx, 4
    call prs_append_redir
    pop dx
    jmp .trail_blk_redir

.j_prs_err1:
    jmp .prs_err

.finish_blk_node:
    test ax, ax
    jnz .j_prs_err1
    push si
    push dx
    mov al, AST_BLOCK
    xor ah, ah
    call prs_alloc_node
    pop dx
    pop si
    jc .j_prs_err1
    mov ax, word [bp - 2]
    mov word [di + AST_NODE.wFlags], ax
    mov word [di + AST_NODE.wA], dx
    mov ax, word [bp - 4]
    mov word [di + AST_NODE.wB], ax
    add sp, 8
    pop bp
    clc
    ret

.chk_kw_head:
    mov word [bp - 8], si
    push si
    mov cx, 132
    call arena_alloc
    pop si
    jc .j_prs_err1
    mov word [bp - 6], di

    call lex_read_word
    jc .empty_primary

    ; 1. REM comment check (never evaluates mid-line redirections or connectors!)
    push si
    mov si, word [bp - 6]
    call prs_strlen_si
    mov di, addr(szPrsRem)
    mov dx, 3
    call slice_eq_nocase
    pop si
    jc .chk_kw_if
.sk_rem_rest:
    mov al, byte [si]
    test al, al
    jz .j_ret_nop
    cmp al, 0x0D
    je .j_ret_nop
    cmp al, 0x0A
    je .j_ret_nop
    cmp al, ')'
    je .j_ret_nop
    inc si
    jmp .sk_rem_rest
.j_ret_nop:
    jmp .ret_nop

.chk_kw_if:
    push si
    mov si, word [bp - 6]
    call prs_strlen_si
    mov di, addr(szPrsIf)
    mov dx, 2
    call slice_eq_nocase
    pop si
    jc .chk_kw_for
    call prs_parse_if_after_kw
    jc .j_prs_err2
    add sp, 8
    pop bp
    clc
    ret

.chk_kw_for:
    push si
    mov si, word [bp - 6]
    call prs_strlen_si
    mov di, addr(szPrsFor)
    mov dx, 3
    call slice_eq_nocase
    pop si
    jc .parse_simple_cmd
    call prs_parse_for_after_kw
    jc .j_prs_err2
    add sp, 8
    pop bp
    clc
    ret

.j_prs_err2:
    jmp .prs_err

.empty_primary:
    mov si, word [bp - 8]
    cmp word [bp - 4], 0
    je .j_ret_nop
    push si
    mov al, AST_SIMPLE_CMD
    mov ah, byte [bp - 2]
    call prs_alloc_node
    pop si
    jc .j_prs_err2
    mov word [di + AST_NODE.wA], addr(szPrsEmpty)
    mov ax, word [bp - 4]
    mov word [di + AST_NODE.wB], ax
    add sp, 8
    pop bp
    clc
    ret

.parse_simple_cmd:
    ; Scan from [bp-8] up to unquoted/unescaped '&', '|', ')', CR, LF, NUL,
    ; extracting any mid-line/trailing redirections into [bp-4] and copying clean command text!
    mov si, word [bp - 8]
    push si
    mov cx, 256
    call arena_alloc
    pop si
    jc .j_prs_err2
    mov word [bp - 6], di ; clean cmd buffer
    xor cx, cx            ; clean cmd len
    xor dx, dx            ; DL=quote state (0/1), DH=inner '(' balance

    ; Special rule: ECHO<delim> (e.g. ECHO(hello) does NOT treat '(' right after ECHO as a block!
    mov al, byte [si]
    and al, 0xDF
    cmp al, 'E'
    jne .sc_lp
    mov al, byte [si + 1]
    and al, 0xDF
    cmp al, 'C'
    jne .sc_lp
    mov al, byte [si + 2]
    and al, 0xDF
    cmp al, 'H'
    jne .sc_lp
    mov al, byte [si + 3]
    and al, 0xDF
    cmp al, 'O'
    jne .sc_lp
    mov al, byte [si + 4]
    call lex_is_echo_delim
    jc .sc_lp
    ; Copy "ECHO" + delimiter (5 chars) directly into clean cmd buffer!
    mov bx, 5
.cp_echo5:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    inc cx
    dec bx
    jnz .cp_echo5

.sc_lp:
    mov al, byte [si]
    test al, al
    jz .j_sc_done
    cmp al, '"'
    jne .sc_not_q
    xor dl, 1
    jmp .sc_emit

.sc_not_q:
    test dl, dl
    jnz .j_sc_emit

    cmp al, 0x0D
    je .j_sc_done
    cmp al, 0x0A
    je .j_sc_done
    cmp al, '&'
    je .j_sc_done
    cmp al, '|'
    je .j_sc_done
    cmp al, '('
    jne .sc_chk_rp
    inc dh
.j_sc_emit:
    jmp .sc_emit
.sc_chk_rp:
    cmp al, ')'
    jne .sc_chk_caret
    test dh, dh
    jz .j_sc_done
    dec dh
    jmp .sc_emit
.j_sc_done:
    jmp .sc_done

.sc_chk_caret:
    ; Check '^' escape in NT_CMD/FREEDOS dialect
    cmp al, '^'
    jne .sc_chk_redir
    push es
    mov es, word [wTrnResSeg]
    mov ah, byte [es:resState + RES_STATE.bDialect]
    pop es
    cmp ah, 5
    jb .sc_chk_redir
    cmp byte [si + 1], 0
    je .sc_emit
    cmp cx, 250
    jae .sc_sk_c2
    mov byte [di], al
    inc di
    inc cx
    inc si
    mov al, byte [si]
    jmp .sc_emit
.sc_sk_c2:
    add si, 2
    jmp .sc_lp

.sc_chk_redir:
    ; Only treat '0'..'9' + '<'/'>' as a redirection when at start of a whitespace boundary!
    cmp al, '<'
    je .sc_do_redir
    cmp al, '>'
    je .sc_do_redir
    cmp al, '0'
    jb .sc_emit
    cmp al, '9'
    ja .sc_emit
    cmp cx, 0
    je .sc_try_d_redir
    cmp byte [di - 1], 0x20
    je .sc_try_d_redir
    cmp byte [di - 1], 0x09
    jne .sc_emit
.sc_try_d_redir:
    mov ah, byte [si + 1]
    cmp ah, '<'
    je .sc_do_redir
    cmp ah, '>'
    jne .sc_emit

.sc_do_redir:
    push di
    push cx
    push dx
    call prs_try_redir
    pop dx
    jc .sc_redir_err
    mov bx, bp
    sub bx, 4
    call prs_append_redir
    pop cx
    pop di
    jmp .sc_lp

.sc_redir_err:
    pop cx
    pop di
    jmp .prs_err

.sc_emit:
    cmp cx, 252
    jae .sc_adv_only
    mov byte [di], al
    inc di
    inc cx
.sc_adv_only:
    inc si
    jmp .sc_lp

.sc_done:
    mov byte [di], 0
    push si
    mov al, AST_SIMPLE_CMD
    mov ah, byte [bp - 2]
    call prs_alloc_node
    pop si
    jc .prs_err
    mov ax, word [bp - 6]
    mov word [di + AST_NODE.wA], ax
    mov ax, word [bp - 4]
    mov word [di + AST_NODE.wB], ax
    add sp, 8
    pop bp
    clc
    ret

.prs_err:
    add sp, 8
    pop bp
    stc
    ret
@endproc

;; @proc prs_parse_if_after_kw
;; @abi dos16
;; @in DS:SI = cursor after "IF"
;; @out DI = AST_IF node (with optional ELSE branch), DS:SI advanced, CF=0
;; @err CF=1, AX = ERR_SYNTAX
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Parses IF [/I] [NOT] (ERRORLEVEL|EXIST|DEFINED|CMDEXTVERSION|s1==s2|s1 OP s2) stmt [ELSE stmt].
@proc prs_parse_if_after_kw
    push bp
    mov bp, sp
    sub sp, 14
    ; [bp-2]=flags, [bp-4]=cmp_kind, [bp-6]=lhs_ptr, [bp-8]=rhs_ptr, [bp-10]=then_ast, [bp-12]=else_ast, [bp-14]=tok_buf
    mov word [bp - 2], 0
    mov word [bp - 4], 0
    mov word [bp - 6], 0
    mov word [bp - 8], 0
    mov word [bp - 12], 0

    push si
    mov cx, 136
    call arena_alloc
    pop si
    jnc .if_mem_ok
    jmp .if_err
.if_mem_ok:
    mov word [bp - 14], di

.if_prefix_lp:
    call lex_skip_hspace
    cmp al, '/'
    jne .chk_if_not
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'I'
    jne .chk_if_not
    or word [bp - 2], IFFLAG_NOCASE
    add si, 2
    jmp .if_prefix_lp

.chk_if_not:
    push si
    mov di, word [bp - 14]
    call lex_read_word
    pop bx ; pre-word SI
    jnc .got_if_w1
    jmp .if_syn

.got_if_w1:
    ; Check NOT
    push si
    push bx
    mov si, word [bp - 14]
    call prs_strlen_si
    mov di, addr(szPrsNot)
    mov dx, 3
    call slice_eq_nocase
    pop bx
    pop si
    jc .chk_unary_kws
    xor word [bp - 2], IFFLAG_NOT
    jmp .if_prefix_lp

.chk_unary_kws:
    ; Check ERRORLEVEL, EXIST, DEFINED, CMDEXTVERSION
    push si
    mov si, word [bp - 14]
    call prs_strlen_si
    mov di, addr(szPrsErrLvl)
    mov dx, 10
    call slice_eq_nocase
    pop si
    jnc .unary_errlvl

    push si
    mov si, word [bp - 14]
    call prs_strlen_si
    mov di, addr(szPrsExist)
    mov dx, 5
    call slice_eq_nocase
    pop si
    jnc .unary_exist

    push si
    mov si, word [bp - 14]
    call prs_strlen_si
    mov di, addr(szPrsDefined)
    mov dx, 7
    call slice_eq_nocase
    pop si
    jnc .unary_defined

    push si
    mov si, word [bp - 14]
    call prs_strlen_si
    mov di, addr(szPrsCmdExt)
    mov dx, 13
    call slice_eq_nocase
    pop si
    jnc .unary_cmdext

    jmp .binary_if_cond

.unary_errlvl:
    mov byte [bp - 4], IFCMP_ERRORLEVEL
    jmp .read_unary_arg
.unary_exist:
    mov byte [bp - 4], IFCMP_EXIST
    jmp .read_unary_arg
.unary_defined:
    mov byte [bp - 4], IFCMP_DEFINED
    jmp .read_unary_arg
.unary_cmdext:
    mov byte [bp - 4], IFCMP_CMDEXTVER

.read_unary_arg:
    push si
    mov cx, 136
    call arena_alloc
    pop si
    jc .j_if_err1
    mov word [bp - 6], di
    call lex_read_word
    jc .j_if_syn1
    jmp .parse_then_branch

.j_if_err1:
    jmp .if_err
.j_if_syn1:
    jmp .if_syn

.binary_if_cond:
    ; Check if '==' was glued inside [bp-14] (e.g. 'IF "%A%"=="1"' or 'IF a==b')!
    mov di, word [bp - 14]
    xor dx, dx ; quote toggle? No, even inside "a"=="b", '==' sits outside quotes!
    mov bx, di
.scan_glued_eq:
    mov al, byte [bx]
    test al, al
    jz .separate_op
    cmp al, '"'
    jne .sg_nq
    xor dl, 1
    inc bx
    jmp .scan_glued_eq
.sg_nq:
    test dl, dl
    jnz .sg_adv
    cmp al, '='
    jne .sg_adv
    cmp byte [bx + 1], '='
    je .split_glued_eq
.sg_adv:
    inc bx
    jmp .scan_glued_eq

.split_glued_eq:
    mov byte [bx], 0
    mov word [bp - 6], di
    mov byte [bp - 4], IFCMP_STR_EQ
    add bx, 2
    cmp byte [bx], 0
    je .j_read_rhs
    mov word [bp - 8], bx
    jmp .parse_then_branch
.j_read_rhs:
    jmp .read_rhs_word

.separate_op:
    ; Copy [bp-14] into fresh LHS buffer so [bp-14] can read operator token!
    push si
    mov si, word [bp - 14]
    call prs_strlen_si
    inc cx
    push cx
    call arena_alloc
    pop cx
    jc .pop_if_err
    mov word [bp - 6], di
    mov si, word [bp - 14]
    rep movsb
    pop si

    ; Read operator token into [bp-14] (==, EQU, NEQ, LSS, LEQ, GTR, GEQ)
    mov di, word [bp - 14]
    call lex_read_word
    jc .j_if_syn2

    mov bx, word [bp - 14]
    cmp byte [bx], '='
    jne .chk_3op
    cmp byte [bx + 1], '='
    jne .j_if_syn2
    mov byte [bp - 4], IFCMP_STR_EQ
    cmp byte [bx + 2], 0
    je .j_read_rhs
    add bx, 2
    mov word [bp - 8], bx
    jmp .parse_then_branch

.j_if_syn2:
    jmp .if_syn

.pop_if_err:
    pop si
    jmp .if_err

.chk_3op:
    push si
    mov si, word [bp - 14]
    call prs_strlen_si
    cmp cx, 3
    jne .pop_if_syn

    mov di, addr(szPrsEqu)
    mov dx, 3
    call slice_eq_nocase
    mov al, IFCMP_EQU
    jnc .got_3op

    mov si, word [bp - 14]
    mov cx, 3
    mov di, addr(szPrsNeq)
    mov dx, 3
    call slice_eq_nocase
    mov al, IFCMP_NEQ
    jnc .got_3op

    mov si, word [bp - 14]
    mov cx, 3
    mov di, addr(szPrsLss)
    mov dx, 3
    call slice_eq_nocase
    mov al, IFCMP_LSS
    jnc .got_3op

    mov si, word [bp - 14]
    mov cx, 3
    mov di, addr(szPrsLeq)
    mov dx, 3
    call slice_eq_nocase
    mov al, IFCMP_LEQ
    jnc .got_3op

    mov si, word [bp - 14]
    mov cx, 3
    mov di, addr(szPrsGtr)
    mov dx, 3
    call slice_eq_nocase
    mov al, IFCMP_GTR
    jnc .got_3op

    mov si, word [bp - 14]
    mov cx, 3
    mov di, addr(szPrsGeq)
    mov dx, 3
    call slice_eq_nocase
    mov al, IFCMP_GEQ
    jnc .got_3op

.pop_if_syn:
    pop si
    jmp .if_syn

.got_3op:
    pop si
    mov byte [bp - 4], al

.read_rhs_word:
    push si
    mov cx, 136
    call arena_alloc
    pop si
    jc .if_err
    mov word [bp - 8], di
    call lex_read_word
    jc .if_syn

.parse_then_branch:
    call prs_parse_cond
    jc .if_err
    mov word [bp - 10], di

    ; Check optional ELSE keyword!
    call lex_skip_hspace
    push si
    mov di, word [bp - 14]
    call lex_read_word
    jc .no_else
    push si
    mov si, word [bp - 14]
    call prs_strlen_si
    mov di, addr(szPrsElse)
    mov dx, 4
    call slice_eq_nocase
    pop si
    jc .no_else
    add sp, 2 ; discard pre-ELSE SI
    call prs_parse_seq
    jc .if_err
    mov word [bp - 12], di
    jmp .build_if_node

.no_else:
    pop si

.build_if_node:
    push si
    mov al, AST_IF
    mov ah, byte [bp - 4]
    call prs_alloc_node
    pop si
    jc .if_err
    mov ax, word [bp - 2]
    mov word [di + AST_NODE.wFlags], ax
    mov ax, word [bp - 6]
    mov word [di + AST_NODE.wA], ax
    mov ax, word [bp - 8]
    mov word [di + AST_NODE.wB], ax
    mov ax, word [bp - 10]
    mov word [di + AST_NODE.wC], ax
    mov ax, word [bp - 12]
    mov word [di + AST_NODE.wD], ax
    add sp, 14
    pop bp
    clc
    ret

.if_syn:
    mov ax, ERR_SYNTAX
.if_err:
    add sp, 14
    pop bp
    stc
    ret
@endproc

;; @proc prs_parse_for_after_kw
;; @abi dos16
;; @in DS:SI = cursor after "FOR"
;; @out DI = AST_FOR node, DS:SI advanced, CF=0
;; @err CF=1, AX = ERR_SYNTAX
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Parses FOR [plain | /D | /R [root] | /L | /F ["opts"]] %V IN (set) DO body.
@proc prs_parse_for_after_kw
    push bp
    mov bp, sp
    sub sp, 12
    ; [bp-2]=mode, [bp-4]=var_ch, [bp-6]=opts_or_root, [bp-8]=set_ptr, [bp-10]=tok_buf
    mov word [bp - 2], FORMODE_PLAIN
    mov word [bp - 6], 0

    push si
    mov cx, 140
    call arena_alloc
    pop si
    jnc .for_mem_ok
    jmp .for_err
.for_mem_ok:
    mov word [bp - 10], di

    call lex_skip_hspace
    cmp al, '/'
    jne .read_for_var
    mov ah, byte [si + 1]
    and ah, 0xDF
    add si, 2
    cmp ah, 'D'
    je .mode_d
    cmp ah, 'L'
    je .mode_l
    cmp ah, 'R'
    je .mode_r
    cmp ah, 'F'
    je .mode_f
    jmp .for_syn

.mode_d:
    mov word [bp - 2], FORMODE_DIRS
    jmp .read_for_var
.mode_l:
    mov word [bp - 2], FORMODE_NUMERIC
    jmp .read_for_var
.mode_r:
    mov word [bp - 2], FORMODE_RECURSE
    call lex_skip_hspace
    cmp al, '%'
    je .read_for_var
    push si
    mov cx, 136
    call arena_alloc
    pop si
    jc .j_for_err1
    mov word [bp - 6], di
    call lex_read_word
    jc .j_for_syn1
    jmp .read_for_var

.mode_f:
    mov word [bp - 2], FORMODE_PARSE
    call lex_skip_hspace
    cmp al, '"'
    jne .read_for_var
    push si
    mov cx, 136
    call arena_alloc
    pop si
    jc .j_for_err1
    mov word [bp - 6], di
    call lex_read_word
    jc .j_for_syn1

.read_for_var:
    call lex_skip_hspace
    cmp al, '%'
    jne .j_for_syn1
    inc si
    cmp byte [si], '%'
    jne .got_fv
    inc si
.got_fv:
    mov al, byte [si]
    test al, al
    jz .j_for_syn1
    xor ah, ah
    mov word [bp - 4], ax
    inc si

    ; Expect IN
    mov di, word [bp - 10]
    call lex_read_word
    jc .j_for_syn1
    push si
    mov si, word [bp - 10]
    call prs_strlen_si
    mov di, addr(szPrsIn)
    mov dx, 2
    call slice_eq_nocase
    pop si
    jc .j_for_syn1

    ; Expect '(' ... ')' set!
    call lex_skip_hspace
    cmp al, '('
    jne .j_for_syn1
    inc si

    push si
    mov cx, 180
    call arena_alloc
    pop si
    jc .j_for_err1
    mov word [bp - 8], di
    xor cx, cx
    xor dl, dl
.for_set_lp:
    mov al, byte [si]
    test al, al
    jz .j_for_syn1
    cmp al, '"'
    jne .fs_nq
    xor dl, 1
    jmp .fs_emit
.fs_nq:
    test dl, dl
    jnz .fs_emit
    cmp al, ')'
    je .for_set_done
    cmp al, 0x0D
    je .fs_sp
    cmp al, 0x0A
    jne .fs_emit
.fs_sp:
    mov al, 0x20
.fs_emit:
    cmp cx, 176
    jae .fs_adv
    mov byte [di], al
    inc di
    inc cx
.fs_adv:
    inc si
    jmp .for_set_lp

.j_for_err1:
    jmp .for_err
.j_for_syn1:
    jmp .for_syn

.for_set_done:
    mov byte [di], 0
    inc si ; eat ')'

    ; Expect DO
    mov di, word [bp - 10]
    call lex_read_word
    jc .for_syn
    push si
    mov si, word [bp - 10]
    call prs_strlen_si
    mov di, addr(szPrsDo)
    mov dx, 2
    call slice_eq_nocase
    pop si
    jc .for_syn

    ; Parse DO body (prs_parse_seq)
    call prs_parse_seq
    jc .for_err
    mov bx, di ; body AST

    push si
    push bx
    mov al, AST_FOR
    mov ah, byte [bp - 2]
    call prs_alloc_node
    pop bx
    pop si
    jc .for_err
    mov ax, word [bp - 4]
    mov word [di + AST_NODE.wFlags], ax
    mov ax, word [bp - 8]
    mov word [di + AST_NODE.wA], ax
    mov ax, word [bp - 6]
    mov word [di + AST_NODE.wB], ax
    mov word [di + AST_NODE.wC], bx
    add sp, 12
    pop bp
    clc
    ret

.for_syn:
    mov ax, ERR_SYNTAX
.for_err:
    add sp, 12
    pop bp
    stc
    ret
@endproc

@data db szPrsEmpty, 0
@data db szPrsRem, "REM"
@data db szPrsIf, "IF"
@data db szPrsElse, "ELSE"
@data db szPrsNot, "NOT"
@data db szPrsErrLvl, "ERRORLEVEL"
@data db szPrsExist, "EXIST"
@data db szPrsDefined, "DEFINED"
@data db szPrsCmdExt, "CMDEXTVERSION"
@data db szPrsEqu, "EQU"
@data db szPrsNeq, "NEQ"
@data db szPrsLss, "LSS"
@data db szPrsLeq, "LEQ"
@data db szPrsGtr, "GTR"
@data db szPrsGeq, "GEQ"
@data db szPrsFor, "FOR"
@data db szPrsIn, "IN"
@data db szPrsDo, "DO"
