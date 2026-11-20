;; @file phase34.inc
;; @isa 8086
;; @desc Phase 3 (FOR %I/%~I), Phase 4 (!VAR! & ^ strip), AST executor, pipes, GOTO & SHIFT.

@cpu 8086
@format inc

;; @proc for_lookup_var
;; @abi dos16
;; @in AL = case-sensitive FOR variable char (e.g. 'I')
;; @out DS:SI = ASCIIZ value if active in TRN_FOR_VARS, CF=0; or CF=1 if not bound
;; @err CF=1 when variable AL is not in active FOR scope
;; @clobbers ax, bx, cx, dx, si
;; @desc Searches active FOR scope frames top-down for exact ASCII variable AL.
@proc for_lookup_var
    xor ch, ch
    mov cl, byte [bForDepth]
    test cl, cl
    jz .not_found
.lp:
    dec cl
    push ax
    mov al, cl
    mov bl, 68
    mul bl
    mov si, TRN_FOR_VARS
    add si, ax
    pop ax
    cmp byte [si], al
    je .found
    test cl, cl
    jnz .lp
.not_found:
    stc
    ret
.found:
    add si, 2
    clc
    ret
@endproc

;; @proc for_bind_var
;; @abi dos16
;; @in AL = FOR variable char, DS:SI = ASCIIZ value
;; @out Pushes new FOR variable slot in TRN_FOR_VARS (up to 6 deep), CF=0
;; @err CF=1 if FOR nesting > 6
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Binds a FOR loop variable at the top of TRN_FOR_VARS.
@proc for_bind_var
    mov bl, byte [bForDepth]
    cmp bl, 6
    jae .full
    push ax
    mov al, bl
    mov bh, 68
    mul bh
    mov di, TRN_FOR_VARS
    add di, ax
    pop ax
    mov byte [di], al
    mov byte [di + 1], 0
    add di, 2
    mov cx, 64
.cp:
    mov al, byte [si]
    mov byte [di], al
    test al, al
    jz .done
    inc si
    inc di
    loop .cp
    mov byte [di], 0
.done:
    inc byte [bForDepth]
    clc
    ret
.full:
    stc
    ret
@endproc

;; @proc phase3_expand
;; @abi dos16
;; @in DS:SI = input string, DS:DI = 256B output buffer
;; @out DS:DI = string with active FOR %V and %~[mods]V substituted
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Phase 3 expander substituting active FOR variables and modifiers.
@proc phase3_expand
    cmp byte [bForDepth], 0
    jne .active_for
    ; Fast path when no FOR loop is active: straight copy!
    push di
.fast_cp:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .fast_cp
    pop di
    ret

.active_for:
    push bp
    mov bp, sp
    sub sp, 6
    ; [bp-2]=out_buf, [bp-4]=out_len, [bp-6]=mod_work (256B)
    mov word [bp - 2], di
    mov word [bp - 4], 0

    push si
    mov cx, 260
    call arena_alloc
    pop si
    jnc .p3_mem
    mov di, word [bp - 2]
    mov byte [di], 0
    add sp, 6
    pop bp
    ret
.p3_mem:
    mov word [bp - 6], di

.p3_lp:
    mov al, byte [si]
    test al, al
    jz .p3_done
    cmp al, '%'
    je .p3_pct
    call p34_append_al
    inc si
    jmp .p3_lp

.p3_pct:
    mov al, byte [si + 1]
    test al, al
    jz .emit_lit_pct
    cmp al, '~'
    je .p3_tilde
    push si
    call for_lookup_var
    jc .pop_lit_pct
    call p34_append_asciiz
    pop si
    add si, 2
    jmp .p3_lp

.pop_lit_pct:
    pop si
.emit_lit_pct:
    mov al, '%'
    call p34_append_al
    inc si
    jmp .p3_lp

.p3_tilde:
    push si
    add si, 2
    call mod_parse_flags
    jc .pop_lit_pct
    mov al, byte [si]
    test al, al
    jz .pop_lit_pct
    inc si
    mov cx, si ; CX = post-var SI
    push cx
    push bx
    push dx
    call for_lookup_var
    pop dx
    pop bx
    pop cx
    jc .pop_lit_pct
    ; Replace stacked pre-'%' SI with post-var CX!
    add sp, 2
    push cx
    mov di, word [bp - 6]
    call mod_eval_value
    mov si, word [bp - 6]
    call p34_append_asciiz
    pop si
    jmp .p3_lp

.p3_done:
    mov di, word [bp - 2]
    mov bx, word [bp - 4]
    mov byte [di + bx], 0
    add sp, 6
    pop bp
    ret
@endproc

;; @proc phase4_expand
;; @abi dos16
;; @in DS:SI = Phase-3 string, DS:DI = 256B output buffer
;; @out DS:DI = Phase-4 string (!VAR! expanded when bDelayedExp!=0, unquoted ^ stripped in NT_CMD)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Phase 4 delayed expansion (!VAR!, !VAR:~s,l!, !VAR:a=b!, ^!) and caret stripper.
@proc phase4_expand
    push bp
    mov bp, sp
    sub sp, 12
    ; [bp-2]=out_buf, [bp-4]=out_len, [bp-6]=val_buf, [bp-8]=xf_buf, [bp-10]=delayed_on, [bp-12]=dialect
    mov word [bp - 2], di
    mov word [bp - 4], 0

    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bDelayedExp]
    mov byte [bp - 10], al
    mov al, byte [es:resState + RES_STATE.bDialect]
    mov byte [bp - 12], al
    pop es

    push si
    mov cx, 520
    call arena_alloc
    pop si
    jnc .p4_mem
    mov di, word [bp - 2]
    mov byte [di], 0
    add sp, 12
    pop bp
    ret
.p4_mem:
    mov word [bp - 6], di
    add di, 260
    mov word [bp - 8], di

    xor dl, dl ; quote state (0=unquoted, 1=inside "...")
.p4_lp:
    mov al, byte [si]
    test al, al
    jnz .p4_non_nul
    jmp .p4_done
.p4_non_nul:
    cmp al, '"'
    jne .p4_not_q
    xor dl, 1
    call p34_append_al
    inc si
    jmp .p4_lp

.p4_not_q:
    cmp al, '^'
    jne .p4_chk_bang
    ; '^!' escapes '!' when delayed expansion is active (both quoted & unquoted in cmd.exe!)
    cmp byte [bp - 10], 0
    je .p4_caret_nodelay
    cmp byte [si + 1], '!'
    jne .p4_caret_nodelay
    mov al, '!'
    call p34_append_al
    add si, 2
    jmp .p4_lp

.p4_caret_nodelay:
    ; Outside quotes in NT_CMD/FREEDOS (dialect >= 5), '^c' strips caret and emits 'c'!
    test dl, dl
    jnz .emit_p4_ch
    cmp byte [bp - 12], 5
    jb .emit_p4_ch
    cmp byte [si + 1], 0
    je .emit_p4_ch
    inc si
    mov al, byte [si]
    jmp .emit_p4_ch

.p4_chk_bang:
    cmp al, '!'
    jne .emit_p4_ch
    cmp byte [bp - 10], 0
    je .emit_p4_ch

    ; Scan for closing '!' on same line
    inc si
    mov bx, si ; start of var name
    xor cx, cx ; name len
    xor di, di ; op_ptr (0 if none)
.bang_lp:
    mov al, byte [si]
    test al, al
    jz .unclosed_bang
    cmp al, '!'
    je .closed_bang
    cmp al, ':'
    jne .bang_adv
    test di, di
    jnz .bang_adv
    mov di, si
    inc di
    inc si
    jmp .bang_lp
.bang_adv:
    test di, di
    jnz .bang_op_adv
    inc cx
.bang_op_adv:
    inc si
    jmp .bang_lp

.unclosed_bang:
    mov si, bx
    mov al, '!'
    call p34_append_al
    jmp .p4_lp

.closed_bang:
    mov ax, si ; closing '!' addr
    inc si
    push dx    ; save quote state DL!
    push si    ; save post-'!' cursor
    push ax    ; closing '!' addr
    push di    ; op_ptr

    mov si, bx
    mov di, word [bp - 6]
    call env_get_var
    jc .undef_bang

    pop bx     ; op_ptr
    pop dx     ; closing '!' addr
    test bx, bx
    jz .emit_raw_bang
    sub dx, bx
    mov si, word [bp - 6]
    mov di, word [bp - 8]
    call var_apply_ops
    mov si, word [bp - 8]
    call p34_append_asciiz
    pop si
    pop dx
    jmp .p4_lp

.emit_raw_bang:
    mov si, word [bp - 6]
    call p34_append_asciiz
    pop si
    pop dx
    jmp .p4_lp

.undef_bang:
    ; Undefined !VAR! always expands to empty string!
    pop bx
    pop ax
    pop si
    pop dx
    jmp .p4_lp

.emit_p4_ch:
    call p34_append_al
    inc si
    jmp .p4_lp

.p4_done:
    mov di, word [bp - 2]
    mov bx, word [bp - 4]
    mov byte [di + bx], 0
    add sp, 12
    pop bp
    ret
@endproc

;; @proc p34_append_al
;; @abi dos16
;; @in AL = byte, [bp-2] = out_buf, [bp-4] = out_len
;; @out Appends AL if out_len < 252
;; @err none
;; @clobbers bx, di
;; @desc Shared 1-byte bounded append helper for phase3_expand and phase4_expand.
@proc p34_append_al
    mov bx, word [bp - 4]
    cmp bx, 252
    jae .full
    mov di, word [bp - 2]
    mov byte [di + bx], al
    inc word [bp - 4]
.full:
    ret
@endproc

;; @proc p34_append_asciiz
;; @abi dos16
;; @in DS:SI = ASCIIZ string, [bp-2] = out_buf, [bp-4] = out_len
;; @out Appends string up to 252-byte cap
;; @err none
;; @clobbers ax, bx, si, di
;; @desc Shared ASCIIZ bounded append helper for phase3_expand and phase4_expand.
@proc p34_append_asciiz
.lp:
    mov al, byte [si]
    test al, al
    jz .done
    call p34_append_al
    inc si
    jmp .lp
.done:
    ret
@endproc

;; @proc ast_apply_redir_list
;; @abi dos16
;; @in DI = head of AST_REDIR_NODE list (0 if none), DS:BX = 6-byte save block (in_hdl, out_hdl, err_hdl)
;; @out Saves original handles in [DS:BX] and applies redirections left-to-right, CF=0
;; @err CF=1 on file open/create error (reported via trn_err_dos)
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Applies AST_REDIR_NODE list across DOS 1.25 FCBs and DOS 2.00+ handles (FD 0..2 & 2>&1).
@proc ast_apply_redir_list
    mov word [bx], 0xFFFF
    mov word [bx + 2], 0xFFFF
    mov word [bx + 4], 0xFFFF
    test di, di
    jnz .start
    clc
    ret

.start:
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=save_blk, [bp-4]=cur_node, [bp-6]=exp_p3, [bp-8]=exp_p4
    mov word [bp - 2], bx
    mov word [bp - 4], di

    mov cx, 520
    call arena_alloc
    jc .redir_oom
    mov word [bp - 6], di
    add di, 260
    mov word [bp - 8], di

.node_lp:
    mov di, word [bp - 4]
    test di, di
    jnz .do_one
    add sp, 8
    pop bp
    clc
    ret

.redir_oom:
    mov ax, ERR_OUT_OF_MEMORY
    jmp .redir_err

.do_one:
    mov al, byte [di + AST_REDIR_NODE.bKind]
    cmp al, REDIR_DUP_OUT
    je .do_dup_fd
    cmp al, REDIR_DUP_IN
    je .do_dup_fd

    ; Expand target filename via Phase 3 + Phase 4!
    mov si, word [di + AST_REDIR_NODE.wTarget]
    mov di, word [bp - 6]
    call phase3_expand
    mov si, word [bp - 6]
    mov di, word [bp - 8]
    call phase4_expand

    mov di, word [bp - 4]
    cmp byte [bTrnDosMajor], 2
    jb .do_125_redir

    ; Save original FD (0, 1, or 2) into save_blk if not already saved
    xor bh, bh
    mov bl, byte [di + AST_REDIR_NODE.bFd]
    cmp bl, 2
    ja .next_rnode
    call ast_save_orig_fd
    jc .redir_err

    mov di, word [bp - 4]
    mov al, byte [di + AST_REDIR_NODE.bKind]
    cmp al, REDIR_IN
    je .open_r_in
    cmp al, REDIR_OUT_APPEND
    je .open_r_app

    ; Truncating '>'
    mov dx, word [bp - 8]
    xor cx, cx
    call k200_create
    jc .redir_err
    jmp .dup_onto_fd

.open_r_app:
    mov dx, word [bp - 8]
    mov al, 2
    call k200_open
    jc .creat_r_app
    mov bx, ax
    xor cx, cx
    xor dx, dx
    mov al, 2
    call k200_seek
    mov ax, bx
    jmp .dup_onto_fd
.creat_r_app:
    mov dx, word [bp - 8]
    xor cx, cx
    call k200_create
    jc .redir_err
    jmp .dup_onto_fd

.open_r_in:
    mov dx, word [bp - 8]
    xor al, al
    call k200_open
    jc .redir_err

.dup_onto_fd:
    mov bx, ax
    mov di, word [bp - 4]
    xor ch, ch
    mov cl, byte [di + AST_REDIR_NODE.bFd]
    call k200_dup2
    pushf
    call k200_close
    popf
    jc .redir_err
    jmp .next_rnode

.do_dup_fd:
    cmp byte [bTrnDosMajor], 2
    jb .next_rnode
    xor bh, bh
    mov bl, byte [di + AST_REDIR_NODE.bFd]
    cmp bl, 2
    ja .next_rnode
    call ast_save_orig_fd
    jc .redir_err
    mov di, word [bp - 4]
    mov bx, word [di + AST_REDIR_NODE.wTarget]
    xor ch, ch
    mov cl, byte [di + AST_REDIR_NODE.bFd]
    call k200_dup2
    jc .redir_err
    jmp .next_rnode

.do_125_redir:
    ; Delegate DOS 1.25 FCB '<', '>', '>>' onto resident fcbRedirIn / fcbRedirOut!
    mov al, byte [di + AST_REDIR_NODE.bKind]
    cmp al, REDIR_IN
    jne .r125_out
    push es
    mov es, word [wTrnResSeg]
    mov si, word [bp - 8]
    mov di, addr(fcbRedirIn)
    mov ax, 0x2901
    int 0x21
    push ds
    mov ax, es
    mov ds, ax
    mov dx, addr(fcbRedirIn)
    mov ah, DOS_FCB_OPEN
    int 0x21
    mov word [fcbRedirIn + FCB.wRecSize], 1
    mov word [fcbRedirIn + FCB.dwRandRec], 0
    mov word [fcbRedirIn + FCB.dwRandRec + 2], 0
    or byte [bRedirFlags], REDIR125_IN_ACTIVE
    pop ds
    pop es
    test al, al
    jnz .r125_nf
    call far [wTrnRed125InPtr]
    jmp .next_rnode

.r125_out:
    mov cl, al ; REDIR_OUT_TRUNC (2) vs REDIR_OUT_APPEND (3)
    push cx
    push es
    mov es, word [wTrnResSeg]
    mov si, word [bp - 8]
    mov di, fcbRedirOut
    mov ax, 0x2901
    int 0x21
    pop es
    pop cx
    push ds
    mov ds, word [wTrnResSeg]
    cmp cl, REDIR_OUT_APPEND
    jne .r125_creat
    mov dx, fcbRedirOut
    mov ah, DOS_FCB_OPEN
    int 0x21
    test al, al
    jnz .r125_creat
    mov ax, word [fcbRedirOut + FCB.dwFileSize]
    mov word [fcbRedirOut + FCB.dwRandRec], ax
    mov ax, word [fcbRedirOut + FCB.dwFileSize + 2]
    mov word [fcbRedirOut + FCB.dwRandRec + 2], ax
    jmp .r125_fin
.r125_creat:
    mov dx, fcbRedirOut
    mov ah, DOS_FCB_CREATE
    int 0x21
    test al, al
    jnz .r125_pop_nf
    mov word [fcbRedirOut + FCB.dwRandRec], 0
    mov word [fcbRedirOut + FCB.dwRandRec + 2], 0
.r125_fin:
    mov word [fcbRedirOut + FCB.wRecSize], 1
    or byte [bRedirFlags], REDIR125_OUT_ACTIVE
    pop ds
    call far [wTrnRed125InPtr]

.next_rnode:
    mov di, word [bp - 4]
    mov di, word [di + AST_REDIR_NODE.wNext]
    mov word [bp - 4], di
    jmp .node_lp

.r125_pop_nf:
    pop ds
.r125_nf:
    mov ax, ERR_FILE_NOT_FOUND
.redir_err:
    push ax
    mov bx, word [bp - 2]
    call ast_restore_redir_list
    pop ax
    call trn_err_dos
    add sp, 8
    pop bp
    stc
    ret
@endproc

;; @proc ast_save_orig_fd
;; @abi dos16
;; @in BX = fd (0, 1, or 2), [bp-2] = 6-byte save_blk
;; @out Duplicates BX into save_blk[BX*2] if still 0xFFFF, CF=0
;; @err CF=1, AX = ERR_* if DUP fails
;; @clobbers ax, bx, si
;; @desc Lazily saves original descriptor 0/1/2 once before redirecting it.
@proc ast_save_orig_fd
    mov si, word [bp - 2]
    mov ax, bx
    shl ax, 1
    add si, ax
    cmp word [si], 0xFFFF
    jne .already
    call k200_dup
    jc .fail
    mov word [si], ax
.already:
    clc
    ret
.fail:
    ret
@endproc

;; @proc ast_restore_redir_list
;; @abi dos16
;; @in DS:BX = 6-byte save_blk (or DOS 1.25 resident redir hook)
;; @out Restores descriptors 0, 1, 2 and closes temporary saved handles
;; @err none
;; @clobbers ax, bx, cx, si
;; @desc Tears down scoped AST redirections cleanly across compound blocks and commands.
@proc ast_restore_redir_list
    cmp byte [bTrnDosMajor], 2
    jb .td_125
    mov si, bx
    xor cx, cx
.fd_lp:
    mov bx, word [si]
    mov word [si], 0xFFFF
    cmp bx, 0xFFFF
    je .nx_fd
    push si
    push cx
    call k200_dup2
    call k200_close
    pop cx
    pop si
.nx_fd:
    add si, 2
    inc cx
    cmp cx, 3
    jb .fd_lp
    ret
.td_125:
    call far [wTrnRed125RmPtr]
    ret
@endproc

;; @proc ast_exec_node
;; @abi dos16
;; @in DI = AST_NODE pointer
;; @out Executes AST node (updating wTrnExitCode, environment, VFS, batch stack)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Recursive AST evaluator executing simple cmds, (...) blocks, |, &&, ||, &, IF, and FOR.
@proc ast_exec_node
    test di, di
    jnz .non_null
    ret
.non_null:
    mov al, byte [di + AST_NODE.bKind]
    cmp al, AST_NOP
    je .done_nop
    cmp al, AST_SIMPLE_CMD
    je .exec_simple
    cmp al, AST_BLOCK
    je .go_block
    cmp al, AST_BINARY
    je .go_binary
    cmp al, AST_IF
    je .go_if
    cmp al, AST_FOR
    je .go_for
.done_nop:
    ret

.go_block:
    jmp ast_exec_block
.go_binary:
    jmp ast_exec_binary
.go_if:
    jmp ast_exec_if
.go_for:
    jmp ast_exec_for

.exec_simple:
    push bp
    mov bp, sp
    sub sp, 12
    ; [bp-6..bp-1]=6B redir save, [bp-8]=node, [bp-10]=p3_buf, [bp-12]=p4_buf
    mov word [bp - 8], di

    ; Save resident redir state in case child .COM reloads TRANSIENT!
    mov bx, bp
    sub bx, 6
    mov di, word [di + AST_NODE.wB]
    call ast_apply_redir_list
    jc .sc_exit

    mov cx, 520
    call arena_alloc
    jc .sc_restore
    mov word [bp - 10], di
    add di, 260
    mov word [bp - 12], di

    mov di, word [bp - 8]
    mov si, word [di + AST_NODE.wA]
    mov di, word [bp - 10]
    call phase3_expand
    mov si, word [bp - 10]
    mov di, word [bp - 12]
    call phase4_expand

    mov si, word [bp - 12]
    mov byte [bTrnIsCall], 0
    call trn_exec_stmt

.sc_restore:
    mov bx, bp
    sub bx, 6
    call ast_restore_redir_list
.sc_exit:
    add sp, 12
    pop bp
    ret
@endproc

;; @proc ast_exec_block
;; @abi dos16
;; @in DI = AST_BLOCK node
;; @out Applies outer redirections and executes wA statement chain
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Executes a parenthesized (...) compound block with scoped redirections.
@proc ast_exec_block
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-6..bp-1]=redir save, [bp-8]=cur_stmt
    mov ax, word [di + AST_NODE.wA]
    mov word [bp - 8], ax

    mov bx, bp
    sub bx, 6
    mov di, word [di + AST_NODE.wB]
    call ast_apply_redir_list
    jc .blk_done

.stmt_lp:
    mov di, word [bp - 8]
    test di, di
    jz .blk_restore
    mov ax, word [di + AST_NODE.wNext]
    mov word [bp - 8], ax
    call ast_exec_node
    cmp byte [bTrnExitReq], 0
    jne .blk_restore
    jmp .stmt_lp

.blk_restore:
    mov bx, bp
    sub bx, 6
    call ast_restore_redir_list
.blk_done:
    add sp, 8
    pop bp
    ret
@endproc

;; @proc ast_exec_binary
;; @abi dos16
;; @in DI = AST_BINARY node
;; @out Executes &, &&, ||, or spooled | pipeline
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates binary connectors and DOS spooled pipelines with temp-file cleanup.
@proc ast_exec_binary
    push bp
    mov bp, sp
    sub sp, 18
    ; [bp-2]=node, [bp-8..bp-3]=6B redir save, [bp-18..bp-9]=10B spool filename
    mov word [bp - 2], di
    mov al, byte [di + AST_NODE.bSub]
    cmp al, BINOP_PIPE
    je .do_pipe

    mov di, word [di + AST_NODE.wA]
    call ast_exec_node
    cmp byte [bTrnExitReq], 0
    jne .bin_done

    mov di, word [bp - 2]
    mov al, byte [di + AST_NODE.bSub]
    cmp al, BINOP_SEQ
    je .run_rhs
    cmp al, BINOP_AND
    je .chk_and
    ; BINOP_OR: run RHS only if wTrnExitCode != 0
    cmp word [wTrnExitCode], 0
    je .bin_done
    jmp .run_rhs
.chk_and:
    cmp word [wTrnExitCode], 0
    jne .bin_done
.run_rhs:
    mov di, word [di + AST_NODE.wB]
    call ast_exec_node
.bin_done:
    add sp, 18
    pop bp
    ret

.do_pipe:
    ; Build unique spool filename "_WCP0.TMP" .. "_WCP9.TMP" in arena!
    mov cx, 16
    call arena_alloc
    jc .bin_done
    mov word [bp - 10], di
    inc byte [bPipeDepth]
    mov al, byte [bPipeDepth]
    add al, '0'
    mov byte [di], '_'
    mov byte [di + 1], 'W'
    mov byte [di + 2], 'C'
    mov byte [di + 3], 'P'
    mov byte [di + 4], al
    mov byte [di + 5], '.'
    mov byte [di + 6], 'T'
    mov byte [di + 7], 'M'
    mov byte [di + 8], 'P'
    mov byte [di + 9], 0

    ; 1. Redirect stdout -> spool file and run LHS (wA)
    mov cx, 8
    call arena_alloc
    jc .dec_pipe
    mov byte [di + AST_REDIR_NODE.bKind], REDIR_OUT_TRUNC
    mov byte [di + AST_REDIR_NODE.bFd], 1
    mov ax, word [bp - 10]
    mov word [di + AST_REDIR_NODE.wTarget], ax
    mov word [di + AST_REDIR_NODE.wNext], 0

    mov bx, bp
    sub bx, 8
    call ast_apply_redir_list
    jc .del_spool
    mov si, word [bp - 2]
    mov di, word [si + AST_NODE.wA]
    call ast_exec_node
    mov bx, bp
    sub bx, 8
    call ast_restore_redir_list

    ; 2. Redirect stdin <- spool file and run RHS (wB)
    mov cx, 8
    call arena_alloc
    jc .del_spool
    mov byte [di + AST_REDIR_NODE.bKind], REDIR_IN
    mov byte [di + AST_REDIR_NODE.bFd], 0
    mov ax, word [bp - 10]
    mov word [di + AST_REDIR_NODE.wTarget], ax
    mov word [di + AST_REDIR_NODE.wNext], 0

    mov bx, bp
    sub bx, 8
    call ast_apply_redir_list
    jc .del_spool
    mov si, word [bp - 2]
    mov di, word [si + AST_NODE.wB]
    call ast_exec_node
    mov bx, bp
    sub bx, 8
    call ast_restore_redir_list

.del_spool:
    ; Delete temporary spool file on DOS 2.00+ or DOS 1.25!
    cmp byte [bTrnDosMajor], 2
    jb .del_sp125
    mov dx, word [bp - 10]
    mov ah, DOS_UNLINK
    int 0x21
    jmp .dec_pipe
.del_sp125:
    mov cx, 64
    call arena_alloc
    jc .dec_pipe
    mov si, word [bp - 10]
    call k125_delete
.dec_pipe:
    dec byte [bPipeDepth]
    add sp, 18
    pop bp
    ret
@endproc

;; @proc ast_exec_if
;; @abi dos16
;; @in DI = AST_IF node
;; @out Evaluates unary/binary IF condition and executes wC (then) or wD (else)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates all 11 IF condition kinds including signed 32-bit numeric EQU..GEQ & /I.
@proc ast_exec_if
    push bp
    mov bp, sp
    sub sp, 12
    ; [bp-2]=node, [bp-4]=lhs_exp, [bp-6]=rhs_exp, [bp-8]=tmp_p3, [bp-12..bp-9]=lhs_i32
    mov word [bp - 2], di

    mov cx, 420
    call arena_alloc
    jnc .if_mem
    add sp, 12
    pop bp
    ret
.if_mem:
    mov word [bp - 4], di
    add di, 140
    mov word [bp - 6], di
    add di, 140
    mov word [bp - 8], di

    ; Expand LHS (wA) via Phase 3 + Phase 4
    mov bx, word [bp - 2]
    mov si, word [bx + AST_NODE.wA]
    mov di, word [bp - 8]
    call phase3_expand
    mov si, word [bp - 8]
    mov di, word [bp - 4]
    call phase4_expand

    mov bx, word [bp - 2]
    cmp word [bx + AST_NODE.wB], 0
    je .eval_cond
    mov si, word [bx + AST_NODE.wB]
    mov di, word [bp - 8]
    call phase3_expand
    mov si, word [bp - 8]
    mov di, word [bp - 6]
    call phase4_expand

.eval_cond:
    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.bSub]
    cmp al, IFCMP_ERRORLEVEL
    je .c_errlvl
    cmp al, IFCMP_EXIST
    je .c_exist
    cmp al, IFCMP_DEFINED
    je .c_defined
    cmp al, IFCMP_CMDEXTVER
    je .c_cmdext
    cmp al, IFCMP_STR_EQ
    je .j_str_cmp
    jmp .c_rel_op
.j_str_cmp:
    jmp .c_str_eq

.c_errlvl:
    mov si, word [bp - 4]
    call parse_i32_lit
    mov cx, 0
    jc .apply_not
    cmp word [wTrnExitCode], ax
    jb .apply_not
    mov cx, 1
    jmp .apply_not

.c_cmdext:
    mov si, word [bp - 4]
    call parse_i32_lit
    mov cx, 0
    jc .apply_not
    cmp ax, 2
    ja .apply_not
    mov cx, 1
    jmp .apply_not

.c_defined:
    mov si, word [bp - 4]
    call prs_strlen_si
    mov di, word [bp - 8]
    call env_get_var
    mov cx, 0
    jc .apply_not
    mov cx, 1
    jmp .apply_not

.c_exist:
    cmp byte [bTrnDosMajor], 2
    jb .ex_125
    mov dx, word [bp - 4]
    mov di, word [bp - 8]
    mov cx, 0x0016
    call k200_find_first
    mov cx, 0
    jc .apply_not
    mov cx, 1
    jmp .apply_not
.ex_125:
    mov si, word [bp - 4]
    mov di, word [bp - 8]
    call k125_open
    mov cx, 0
    jc .apply_not
    mov di, word [bp - 8]
    call k125_close
    mov cx, 1
    jmp .apply_not

.c_str_eq:
    mov al, IFCMP_EQU
    jmp .lex_compare

.c_rel_op:
    ; Try parsing BOTH LHS and RHS as complete 32-bit signed integers!
    mov si, word [bp - 4]
    call ast_unquote_inplace
    mov si, word [bp - 6]
    call ast_unquote_inplace

    mov si, word [bp - 4]
    call parse_i32_lit
    jc .fallback_lex
    cmp byte [si], 0
    jne .fallback_lex
    mov word [bp - 12], ax
    mov word [bp - 10], dx

    mov si, word [bp - 6]
    call parse_i32_lit
    jc .fallback_lex
    cmp byte [si], 0
    jne .fallback_lex

    ; Signed 32-bit compare: [bp-10]:[bp-12] vs DX:AX!
    ; Set BL = -1 (<), 0 (==), +1 (>)
    mov cx, word [bp - 10]
    cmp cx, dx
    jl .num_lt
    jg .num_gt
    mov cx, word [bp - 12]
    cmp cx, ax
    jb .num_lt
    ja .num_gt
    xor bl, bl
    jmp .eval_rel_bl
.num_lt:
    mov bl, -1
    jmp .eval_rel_bl
.num_gt:
    mov bl, 1
    jmp .eval_rel_bl

.fallback_lex:
    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.bSub]

.lex_compare:
    ; Lexical compare [bp-4] vs [bp-6] (case-insensitive if IFFLAG_NOCASE set!)
    mov bx, word [bp - 2]
    mov dx, word [bx + AST_NODE.wFlags]
    mov si, word [bp - 4]
    mov di, word [bp - 6]
.lex_lp:
    mov al, byte [si]
    mov ah, byte [di]
    test dx, IFFLAG_NOCASE
    jz .lex_exact
    push ax
    call chr_to_upper
    mov cl, al
    pop ax
    mov al, ah
    call chr_to_upper
    mov ah, al
    mov al, cl
.lex_exact:
    cmp al, ah
    jb .lex_lt
    ja .lex_gt
    test al, al
    jz .lex_eq
    inc si
    inc di
    jmp .lex_lp
.lex_lt:
    mov bl, -1
    jmp .eval_rel_bl
.lex_gt:
    mov bl, 1
    jmp .eval_rel_bl
.lex_eq:
    xor bl, bl

.eval_rel_bl:
    mov si, word [bp - 2]
    mov al, byte [si + AST_NODE.bSub]
    cmp al, IFCMP_STR_EQ
    jne .chk_equ
    mov al, IFCMP_EQU
.chk_equ:
    xor cx, cx
    cmp al, IFCMP_EQU
    jne .chk_neq
    cmp bl, 0
    jne .apply_not
    mov cx, 1
    jmp .apply_not
.chk_neq:
    cmp al, IFCMP_NEQ
    jne .chk_lss
    cmp bl, 0
    je .apply_not
    mov cx, 1
    jmp .apply_not
.chk_lss:
    cmp al, IFCMP_LSS
    jne .chk_leq
    cmp bl, -1
    jne .apply_not
    mov cx, 1
    jmp .apply_not
.chk_leq:
    cmp al, IFCMP_LEQ
    jne .chk_gtr
    cmp bl, 1
    je .apply_not
    mov cx, 1
    jmp .apply_not
.chk_gtr:
    cmp al, IFCMP_GTR
    jne .chk_geq
    cmp bl, 1
    jne .apply_not
    mov cx, 1
    jmp .apply_not
.chk_geq:
    cmp bl, -1
    je .apply_not
    mov cx, 1

.apply_not:
    mov bx, word [bp - 2]
    test word [bx + AST_NODE.wFlags], IFFLAG_NOT
    jz .dispatch_if
    xor cx, 1

.dispatch_if:
    test cx, cx
    jz .run_else
    mov di, word [bx + AST_NODE.wC]
    call ast_exec_node
    add sp, 12
    pop bp
    ret
.run_else:
    mov di, word [bx + AST_NODE.wD]
    test di, di
    jz .if_done
    call ast_exec_node
.if_done:
    add sp, 12
    pop bp
    ret
@endproc

;; @proc ast_unquote_inplace
;; @abi dos16
;; @in DS:SI = ASCIIZ string
;; @out Strips matching outer "..." quotes in-place if present
;; @err none
;; @clobbers ax, cx, si, di
;; @desc Removes surrounding double quotes for numeric IF relational parsing.
@proc ast_unquote_inplace
    cmp byte [si], '"'
    jne .done
    call prs_strlen_si
    cmp cx, 2
    jb .done
    mov di, si
    add di, cx
    cmp byte [di - 1], '"'
    jne .done
    mov byte [di - 1], 0
    mov di, si
    inc si
.sh_lp:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .sh_lp
.done:
    ret
@endproc

;; @proc ast_exec_for
;; @abi dos16
;; @in DI = AST_FOR node
;; @out Executes FOR (plain, /D, /L, /F) with O(1) arena watermark restoration per iteration
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Executes FOR loops while preserving nested FOR variable scopes and arena watermarks.
@proc ast_exec_for
    push bp
    mov bp, sp
    sub sp, 26
    ; [bp-2]=node, [bp-4]=saved_for_depth, [bp-6]=tok_buf(140B), [bp-8]=arena_mark
    ; [bp-12..bp-9]=cur_i32, [bp-16..bp-13]=step_i32, [bp-20..bp-17]=end_i32, [bp-22]=set_cur
    mov word [bp - 2], di
    mov al, byte [bForDepth]
    mov byte [bp - 4], al

    mov cx, 144
    call arena_alloc
    jnc .for_mem_ok
    jmp .for_exit
.for_mem_ok:
    mov word [bp - 6], di
    mov ax, word [wArenaPtr]
    mov word [bp - 8], ax

    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.bSub]
    cmp al, FORMODE_NUMERIC
    je .go_for_l
    cmp al, FORMODE_PARSE
    je .go_for_f

    ; --- FORMODE_PLAIN / FORMODE_DIRS / FORMODE_RECURSE ---
    mov si, word [bx + AST_NODE.wA]
    mov word [bp - 22], si

.set_item_lp:
    mov si, word [bp - 22]
.sk_set_sep:
    mov al, byte [si]
    test al, al
    jnz .chk_sep_ch
    jmp .for_exit
.chk_sep_ch:
    cmp al, 0x20
    je .adv_sep
    cmp al, 0x09
    je .adv_sep
    cmp al, ','
    je .adv_sep
    cmp al, ';'
    je .adv_sep
    jmp .read_item
.adv_sep:
    inc si
    jmp .sk_set_sep

.go_for_l:
    jmp .exec_for_l
.go_for_f:
    jmp .exec_for_f

.read_item:
    mov di, word [bp - 6]
    xor cx, cx
    xor dx, dx ; DL=quote, DH=has_wildcard
.it_lp:
    mov al, byte [si]
    test al, al
    jz .it_done
    cmp al, '"'
    jne .it_nq
    xor dl, 1
    inc si
    jmp .it_lp
.it_nq:
    test dl, dl
    jnz .it_chk_w
    cmp al, 0x20
    je .it_done
    cmp al, 0x09
    je .it_done
    cmp al, ','
    je .it_done
    cmp al, ';'
    je .it_done
.it_chk_w:
    cmp al, '*'
    je .it_wild
    cmp al, '?'
    jne .it_emit
.it_wild:
    mov dh, 1
.it_emit:
    cmp cx, 132
    jae .it_adv
    mov byte [di], al
    inc di
    inc cx
.it_adv:
    inc si
    jmp .it_lp

.it_done:
    mov byte [di], 0
    mov word [bp - 22], si

    test dh, dh
    jnz .expand_wild_item
    mov si, word [bp - 6]
    call ast_run_one_for_iter
    jmp .set_item_lp

.expand_wild_item:
    cmp byte [bTrnDosMajor], 2
    jb .wild_125
    ; Allocate private 48B DTA inside arena before restoring watermark!
    mov cx, 48
    call arena_alloc
    jc .set_item_lp
    mov word [bp - 24], di
    mov ax, word [wArenaPtr]
    mov word [bp - 26], ax

    mov bx, word [bp - 2]
    mov cx, 0x0000
    cmp byte [bx + AST_NODE.bSub], FORMODE_DIRS
    jne .w200_ff
    mov cx, ATTR_DIRECTORY
.w200_ff:
    mov dx, word [bp - 6]
    mov di, word [bp - 24]
    call k200_find_first
    jc .done_wild200
.w200_lp:
    mov bx, word [bp - 24]
    mov si, word [bp - 2]
    cmp byte [si + AST_NODE.bSub], FORMODE_DIRS
    jne .w200_emit
    test byte [bx + DTA_FIND.bAttr], ATTR_DIRECTORY
    jz .w200_nx
    cmp byte [bx + DTA_FIND.szName], '.'
    je .w200_nx
.w200_emit:
    lea si, [bx + DTA_FIND.szName]
    push word [bp - 8]
    mov ax, word [bp - 26]
    mov word [bp - 8], ax
    call ast_run_one_for_iter
    pop word [bp - 8]
.w200_nx:
    mov di, word [bp - 24]
    call k200_find_next
    jnc .w200_lp
.done_wild200:
    mov ax, word [bp - 8]
    mov word [wArenaPtr], ax
    jmp .set_item_lp

.wild_125:
    jmp .set_item_lp

.exec_for_l:
    ; --- FOR /L %I IN (start, step, end) ---
    mov si, word [bx + AST_NODE.wA]
    call ast_skip_num_sep
    call parse_i32_lit
    jc .for_exit
    mov word [bp - 12], ax
    mov word [bp - 10], dx

    call ast_skip_num_sep
    call parse_i32_lit
    jc .for_exit
    mov word [bp - 16], ax
    mov word [bp - 14], dx

    call ast_skip_num_sep
    call parse_i32_lit
    jc .for_exit
    mov word [bp - 20], ax
    mov word [bp - 18], dx

.for_l_lp:
    ; Check termination: if step >= 0, stop when cur > end; if step < 0, stop when cur < end!
    mov ax, word [bp - 12]
    mov dx, word [bp - 10]
    test word [bp - 14], 0x8000
    jnz .chk_neg_step

    cmp dx, word [bp - 18]
    jg .for_exit
    jl .run_l_body
    cmp ax, word [bp - 20]
    ja .for_exit
    jmp .run_l_body

.chk_neg_step:
    cmp dx, word [bp - 18]
    jl .for_exit
    jg .run_l_body
    cmp ax, word [bp - 20]
    jb .for_exit

.run_l_body:
    mov di, word [bp - 6]
    call fmt_i32_dec
    mov si, di
    call ast_run_one_for_iter
    cmp byte [bTrnExitReq], 0
    jne .for_exit

    ; cur += step
    mov ax, word [bp - 16]
    mov dx, word [bp - 14]
    add word [bp - 12], ax
    adc word [bp - 10], dx
    ; Zero step runs once? In cmd.exe step 0 loops unless Ctrl+C; poll Ctrl+Break!
    push es
    mov es, word [wTrnResSeg]
    cmp byte [es:resState + RES_STATE.bCtrlBreakLatched], 0
    pop es
    jne .for_exit
    jmp .for_l_lp

.exec_for_f:
    ; --- FOR /F ["opts"] %I IN ('string' | `cmd` | file) ---
    call ast_exec_for_f

.for_exit:
    mov al, byte [bp - 4]
    mov byte [bForDepth], al
    mov ax, word [bp - 8]
    mov word [wArenaPtr], ax
    add sp, 26
    pop bp
    ret
@endproc

;; @proc ast_skip_num_sep
;; @abi dos16
;; @in DS:SI = cursor inside FOR /L (start, step, end)
;; @out DS:SI advanced past spaces, tabs, commas, semicolons
;; @err none
;; @clobbers ax, si
;; @desc Advances SI to the next integer token in FOR /L.
@proc ast_skip_num_sep
.lp:
    mov al, byte [si]
    cmp al, 0x20
    je .adv
    cmp al, 0x09
    je .adv
    cmp al, ','
    je .adv
    cmp al, ';'
    je .adv
    ret
.adv:
    inc si
    jmp .lp
@endproc

;; @proc ast_run_one_for_iter
;; @abi dos16
;; @in DS:SI = iteration value ASCIIZ, [bp-2]=AST_FOR, [bp-4]=saved_for_depth, [bp-8]=arena_mark
;; @out Binds %V = DS:SI, executes wC body, unwinds FOR scope & arena watermark
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Helper executing a single iteration of AST_FOR in constant arena space.
@proc ast_run_one_for_iter
    mov al, byte [bp - 4]
    mov byte [bForDepth], al
    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.wFlags]
    call for_bind_var
    jc .done
    mov bx, word [bp - 2]
    mov di, word [bx + AST_NODE.wC]
    call ast_exec_node
    mov al, byte [bp - 4]
    mov byte [bForDepth], al
    mov ax, word [bp - 8]
    mov word [wArenaPtr], ax
.done:
    ret
@endproc

;; @proc ast_exec_for_f
;; @abi dos16
;; @in [bp-2] = AST_FOR node, [bp-4] = saved_for_depth, [bp-6] = scratch, [bp-8] = arena_mark
;; @out Tokenizes ('string'), (`cmd`), or (file) lines honoring eol/skip/delims/tokens
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements FOR /F over literal strings, backtick/single-quote commands, and files.
@proc ast_exec_for_f
    push bp
    mov bp, sp
    sub sp, 16
    ; Inherit outer frame via BX = [bp] (caller BP!)
    mov bx, word [bp]
    mov si, word [bx - 2] ; AST_FOR node
    mov word [bp - 2], si
    mov ax, word [bx - 4]
    mov word [bp - 4], ax ; saved_for_depth

    ; Defaults: eol=';' (bp-6 low), skip=0 (bp-8), delims=" 	" (bp-10 ptr), tok2=0 (bp-12)
    mov byte [bp - 6], ';'
    mov word [bp - 8], 0
    mov word [bp - 10], addr(szDefDelims)
    mov byte [bp - 12], 0 ; bit0 = bind %J (2nd token), bit1 = bind * rest

    ; Parse optional "opts" string in wB!
    mov di, word [si + AST_NODE.wB]
    test di, di
    jz .inspect_f_set
    mov si, di
    cmp byte [si], '"'
    jne .opt_lp
    inc si
.opt_lp:
    mov al, byte [si]
    test al, al
    jz .inspect_f_set
    cmp al, '"'
    je .inspect_f_set
    cmp al, 0x20
    jne .chk_opt_eol
    inc si
    jmp .opt_lp
.chk_opt_eol:
    ; Check eol=c
    mov ah, al
    and ah, 0xDF
    cmp ah, 'E'
    jne .chk_opt_skip
    cmp byte [si + 3], '='
    jne .sk_opt_w
    mov al, byte [si + 4]
    mov byte [bp - 6], al
    add si, 5
    jmp .opt_lp
.chk_opt_skip:
    cmp ah, 'S'
    jne .chk_opt_delims
    cmp byte [si + 4], '='
    jne .sk_opt_w
    add si, 5
    call var_parse_signed16
    mov word [bp - 8], ax
    jmp .opt_lp
.chk_opt_delims:
    cmp ah, 'D'
    jne .chk_opt_tokens
    cmp byte [si + 6], '='
    jne .sk_opt_w
    add si, 7
    push si
    mov cx, 16
    call arena_alloc
    pop si
    jc .inspect_f_set
    mov word [bp - 10], di
.cp_dlm:
    mov al, byte [si]
    test al, al
    jz .end_dlm
    cmp al, '"'
    je .end_dlm
    cmp al, 0x20
    jne .put_dlm
    cmp byte [si + 1], 0
    je .put_dlm
    cmp byte [si + 1], '"'
    jne .end_dlm
.put_dlm:
    mov byte [di], al
    inc si
    inc di
    jmp .cp_dlm
.end_dlm:
    mov byte [di], 0
    jmp .opt_lp

.chk_opt_tokens:
    cmp ah, 'T'
    jne .sk_opt_w
    cmp byte [si + 6], '='
    jne .sk_opt_w
    add si, 7
    ; Detect "1,2" or "1,2*" or "1*"
.tok_opt_lp:
    mov al, byte [si]
    test al, al
    jz .inspect_f_set
    cmp al, '"'
    je .inspect_f_set
    cmp al, 0x20
    je .inspect_f_set
    cmp al, '2'
    jne .chk_t_star
    or byte [bp - 12], 1
.chk_t_star:
    cmp al, '*'
    jne .adv_t_opt
    or byte [bp - 12], 2
.adv_t_opt:
    inc si
    jmp .tok_opt_lp

.sk_opt_w:
    inc si
    mov al, byte [si]
    test al, al
    jz .inspect_f_set
    cmp al, '"'
    je .inspect_f_set
    cmp al, 0x20
    jne .sk_opt_w
    jmp .opt_lp

.inspect_f_set:
    mov bx, word [bp - 2]
    mov si, word [bx + AST_NODE.wA]
    call lex_skip_hspace

    ; Case 1: "literal string" (or 'command' / `command`)
    cmp al, '"'
    je .f_lit_str
    cmp al, 0x27 ; ''' command!
    je .f_cmd_quote
    cmp al, '`'
    je .f_cmd_quote

    ; Case 3: File set! Read up to 512B from file into arena and split lines!
    mov cx, 560
    call arena_alloc
    jc .j_f_done
    mov word [bp - 14], di ; file_buf
    ; Trim trailing spaces on filename at SI
    mov dx, di
    add dx, 520
    mov di, dx
    call lex_read_word
    jc .j_f_done
    mov si, dx
    xor cx, cx
    xor dx, dx
    mov di, word [bp - 14]
    call stream_open_read_close
    jc .j_f_done
    mov bx, word [bp - 14]
    add bx, ax
    mov byte [bx], 0
    mov si, word [bp - 14]
    call ast_for_f_run_buffer
.j_f_done:
    jmp .f_done

.f_lit_str:
    inc si
    mov cx, 200
    call arena_alloc
    jc .f_done
    mov dx, di
.cp_flit:
    mov al, byte [si]
    test al, al
    jz .end_flit
    cmp al, '"'
    je .end_flit
    mov byte [di], al
    inc si
    inc di
    jmp .cp_flit
.end_flit:
    mov byte [di], 0
    mov si, dx
    call ast_for_f_run_buffer
    jmp .f_done

.f_cmd_quote:
    mov ah, al ; ''' or '`'
    inc si
    mov cx, 640
    call arena_alloc
    jc .f_done
    mov word [bp - 14], di ; cmd + spool buf
    mov dx, di
.cp_fcmd:
    mov al, byte [si]
    test al, al
    jz .end_fcmd
    cmp al, ah
    je .end_fcmd
    mov byte [di], al
    inc si
    inc di
    jmp .cp_fcmd
.end_fcmd:
    ; Append " > _WCF.TMP" and execute via trn_exec_line, then read & delete _WCF.TMP!
    mov si, addr(szForFSpoolSuffix)
    mov cx, 13
    rep movsb
    mov si, dx
    push word [wArenaPtr]
    call prs_parse_line
    jc .pop_fcmd
    call ast_exec_node
.pop_fcmd:
    pop word [wArenaPtr]

    mov si, addr(szForFSpoolFile)
    xor cx, cx
    xor dx, dx
    mov di, word [bp - 14]
    call stream_open_read_close
    jc .f_done
    mov bx, word [bp - 14]
    add bx, ax
    mov byte [bx], 0
    cmp byte [bTrnDosMajor], 2
    jb .run_fcmd_buf
    mov dx, addr(szForFSpoolFile)
    mov ah, DOS_UNLINK
    int 0x21
.run_fcmd_buf:
    mov si, word [bp - 14]
    call ast_for_f_run_buffer

.f_done:
    add sp, 16
    pop bp
    ret
@endproc

;; @proc ast_for_f_run_buffer
;; @abi dos16
;; @in DS:SI = NUL-terminated multi-line buffer, BP = ast_exec_for_f frame
;; @out Splits lines, skips [bp-8] lines & [bp-6] eol comments, binds tokens, runs DO body
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Line and delimiter tokenizer for FOR /F.
@proc ast_for_f_run_buffer
    mov cx, 260
    call arena_alloc
    jnc .got_ln_buf
    ret
.got_ln_buf:
    mov word [bp - 16], di ; line_buf

.line_lp:
    cmp byte [si], 0
    je .done_buf
    cmp byte [si], 0x1A
    je .done_buf

    mov di, word [bp - 16]
.cp_ln:
    mov al, byte [si]
    test al, al
    jz .ln_end
    cmp al, 0x0D
    je .eat_cr
    cmp al, 0x0A
    je .eat_lf
    mov byte [di], al
    inc si
    inc di
    jmp .cp_ln
.eat_cr:
    inc si
    cmp byte [si], 0x0A
    jne .ln_end
.eat_lf:
    inc si
.ln_end:
    mov byte [di], 0

    ; Save buffer cursor SI!
    push si
    mov si, word [bp - 16]
    cmp byte [si], 0
    je .next_ln

    ; Honor skip=N
    cmp word [bp - 8], 0
    je .chk_eol_c
    dec word [bp - 8]
    jmp .next_ln

.chk_eol_c:
    ; Skip leading delimiters
    call ast_skip_f_delims
    mov al, byte [si]
    test al, al
    jz .next_ln
    cmp al, byte [bp - 6]
    je .next_ln

    ; Extract token 1 at DS:SI
    mov dx, si
    call ast_adv_to_f_delim
    mov ah, byte [si]
    mov byte [si], 0

    ; Bind %V = DX!
    mov al, byte [bp - 4]
    mov byte [bForDepth], al
    mov bx, word [bp - 2]
    push si
    push ax
    mov al, byte [bx + AST_NODE.wFlags]
    mov si, dx
    call for_bind_var
    pop ax
    pop si

    ; Optional token 2 / * rest!
    cmp byte [bp - 12], 0
    je .exec_f_body
    test ah, ah
    jz .bind_empty_t2
    inc si
    call ast_skip_f_delims
    mov dx, si
    test byte [bp - 12], 1
    jz .bind_star_only
    call ast_adv_to_f_delim
    mov ah, byte [si]
    mov byte [si], 0
    push si
    push ax
    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.wFlags]
    inc al
    mov si, dx
    call for_bind_var
    pop ax
    pop si
    test byte [bp - 12], 2
    jz .exec_f_body
    test ah, ah
    jz .exec_f_body
    inc si
    call ast_skip_f_delims
    mov dx, si
    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.wFlags]
    add al, 2
    mov si, dx
    call for_bind_var
    jmp .exec_f_body

.bind_star_only:
    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.wFlags]
    inc al
    mov si, dx
    call for_bind_var
    jmp .exec_f_body

.bind_empty_t2:
    mov bx, word [bp - 2]
    mov al, byte [bx + AST_NODE.wFlags]
    inc al
    mov si, addr(szPrsEmpty)
    call for_bind_var

.exec_f_body:
    push word [wArenaPtr]
    mov bx, word [bp - 2]
    mov di, word [bx + AST_NODE.wC]
    call ast_exec_node
    pop word [wArenaPtr]
    mov al, byte [bp - 4]
    mov byte [bForDepth], al

.next_ln:
    pop si
    jmp .line_lp

.done_buf:
    ret
@endproc

;; @proc ast_skip_f_delims
;; @abi dos16
;; @in DS:SI = line cursor, [bp-10] = ASCIIZ delims string
;; @out Advances DS:SI past any char present in [bp-10]
;; @err none
;; @clobbers ax, di, si
;; @desc Skips FOR /F delimiter characters.
@proc ast_skip_f_delims
.lp:
    mov al, byte [si]
    test al, al
    jz .dn
    mov di, word [bp - 10]
.d_lp:
    cmp byte [di], 0
    je .dn
    cmp al, byte [di]
    je .match
    inc di
    jmp .d_lp
.match:
    inc si
    jmp .lp
.dn:
    ret
@endproc

;; @proc ast_adv_to_f_delim
;; @abi dos16
;; @in DS:SI = token start, [bp-10] = ASCIIZ delims string
;; @out Advances DS:SI until NUL or a char present in [bp-10]
;; @err none
;; @clobbers ax, di, si
;; @desc Scans to end of current FOR /F token.
@proc ast_adv_to_f_delim
.lp:
    mov al, byte [si]
    test al, al
    jz .dn
    mov di, word [bp - 10]
.d_lp:
    cmp byte [di], 0
    je .nx
    cmp al, byte [di]
    je .dn
    inc di
    jmp .d_lp
.nx:
    inc si
    jmp .lp
.dn:
    ret
@endproc

;; @proc cmd_shift
;; @abi dos16
;; @in DS:SI = optional [/0..8] argument
;; @out Increments active BATCH_FRAME.bShiftOff
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Implements SHIFT (and SHIFT /n) on the active resident batch frame.
@proc cmd_shift
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    test al, al
    jz .done
    dec al
    xor ah, ah
    mov cl, 48
    mul cl
    mov bx, addr(batchResStack)
    add bx, ax
    inc byte [es:bx + BATCH_FRAME.bShiftOff]
.done:
    pop es
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_goto
;; @abi dos16
;; @in DS:SI = target label ([:]label or :EOF)
;; @out Seeks active BATCH_FRAME.dwFileOff to matching :label line (or pops frame on :EOF)
;; @err Prints Label not found on missing label
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements GOTO [:]label and GOTO :EOF with wrap-around scan from 0.
@proc cmd_goto
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=lbl_buf (64B), [bp-4]=lbl_len, [bp-6]=line_buf (256B), [bp-8]=frame_off

    call lex_skip_hspace
    cmp al, ':'
    jne .read_lbl
    inc si
.read_lbl:
    push si
    mov cx, 330
    call arena_alloc
    pop si
    jc .goto_done
    mov word [bp - 2], di
    add di, 68
    mov word [bp - 6], di

    mov di, word [bp - 2]
    call lex_read_word
    jc .goto_done
    mov word [bp - 4], cx

    ; Check active batch depth
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    test al, al
    jnz .have_bat
    pop es
    jmp .goto_done
.have_bat:
    dec al
    xor ah, ah
    mov cl, 48
    mul cl
    add ax, addr(batchResStack)
    mov word [bp - 8], ax
    pop es

    ; Check :EOF (case-insensitive) -> pop active batch frame!
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szGotoEof)
    mov dx, 3
    call slice_eq_nocase
    jc .scan_from_zero
    push es
    mov es, word [wTrnResSeg]
    dec byte [es:resState + RES_STATE.bBatchDepth]
    pop es
    mov word [wTrnExitCode], 0
    jmp .goto_done

.scan_from_zero:
    ; Reset dwFileOff = 0 and scan lines via stream_read_batch_line until ':label' matches!
    push es
    mov es, word [wTrnResSeg]
    mov bx, word [bp - 8]
    mov word [es:bx + BATCH_FRAME.dwFileOff], 0
    mov word [es:bx + BATCH_FRAME.dwFileOff + 2], 0
    pop es

.lbl_scan_lp:
    push word [wArenaPtr]
    mov di, word [bp - 6]
    call stream_read_batch_line
    pop word [wArenaPtr]
    jc .lbl_missing

    mov si, word [bp - 6]
    call lex_skip_hspace
    cmp al, ':'
    jne .lbl_scan_lp
    inc si
    call lex_skip_hspace
    ; Measure label word length at SI
    mov di, si
    xor cx, cx
.m_ll:
    mov al, byte [di]
    test al, al
    jz .cmp_lbl
    cmp al, 0x20
    je .cmp_lbl
    cmp al, 0x09
    je .cmp_lbl
    inc di
    inc cx
    jmp .m_ll
.cmp_lbl:
    mov di, word [bp - 2]
    mov dx, word [bp - 4]
    call slice_eq_nocase
    jc .lbl_scan_lp

    mov word [wTrnExitCode], 0
    jmp .goto_done

.lbl_missing:
    mov ax, ERR_LABEL_NOT_FOUND
    call err_print_code
    mov word [wTrnExitCode], 1

.goto_done:
    add sp, 8
    pop bp
    ret
@endproc

@data db bPipeDepth, 0
@data db szDefDelims, " ", 0x09, 0
@data db szForFSpoolSuffix, " > _WCF.TMP", 0
@data db szForFSpoolFile, "_WCF.TMP", 0
@data db szGotoEof, "EOF"
