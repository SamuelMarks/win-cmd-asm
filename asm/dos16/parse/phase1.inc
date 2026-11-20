;; @file phase1.inc
;; @isa 8086
;; @desc Phase 1 immediate % parameter (%0..%9, %*, %~...) & %VAR% / %VAR:...% expander.

@cpu 8086
@format inc

;; @proc batch_get_nth_arg
;; @abi dos16
;; @in AL = parameter index 0..9 (pre-shift), DS:DI = 128B output buffer
;; @out DS:DI = NUL-terminated parameter string, CX = byte length
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Retrieves %0 (script path) or %1..%9 (shifted by BATCH_FRAME.bShiftOff) from TRN_BATCH_ARGS.
@proc batch_get_nth_arg
    push bp
    push es
    mov bp, di

    mov es, word [wTrnResSeg]
    mov bl, byte [es:resState + RES_STATE.bBatchDepth]
    test bl, bl
    jnz .have_bdepth
    jmp .empty_arg
.have_bdepth:
    dec bl
    xor bh, bh

    test al, al
    jnz .nth_pos

    ; %0: return top BATCH_FRAME.szPath (or original %0 token in TRN_BATCH_ARGS)
    mov ax, bx
    mov cl, 48
    mul cl
    mov si, addr(batchResStack)
    add si, ax
    add si, BATCH_FRAME.szPath
    xor cx, cx
.cp_p0:
    mov al, byte [es:si]
    mov byte [di], al
    test al, al
    jz .j_done_a0
    inc si
    inc di
    inc cx
    jmp .cp_p0
.j_done_a0:
    jmp .done_arg

.nth_pos:
    ; Compute effective 1-based token index = AL + bShiftOff
    mov dh, al
    mov ax, bx
    mov cl, 48
    mul cl
    mov si, addr(batchResStack)
    add si, ax
    add dh, byte [es:si + BATCH_FRAME.bShiftOff]

    ; Locate frame's 64-byte arg tail in TRN_BATCH_ARGS + frame_idx * 64
    mov ax, bx
    mov cl, 64
    mul cl
    mov si, TRN_BATCH_ARGS
    add si, ax

.tok_loop:
    ; Skip whitespace/delimiters (space, tab, comma, semicolon, '=')
.sk_delim:
    mov al, byte [si]
    test al, al
    jnz .chk_d_sp
    jmp .empty_arg
.chk_d_sp:
    cmp al, 0x20
    je .adv_d
    cmp al, 0x09
    je .adv_d
    cmp al, ','
    je .adv_d
    cmp al, ';'
    je .adv_d
    cmp al, '='
    je .adv_d
    jmp .at_tok
.adv_d:
    inc si
    jmp .sk_delim

.at_tok:
    dec dh
    jz .copy_this_tok

    ; Skip over this token (respecting "..." quotes!)
    xor dl, dl
.sk_tok_ch:
    mov al, byte [si]
    test al, al
    jz .empty_arg
    cmp al, '"'
    jne .sk_nq
    xor dl, 1
    inc si
    jmp .sk_tok_ch
.sk_nq:
    test dl, dl
    jnz .sk_adv
    cmp al, 0x20
    je .tok_loop
    cmp al, 0x09
    je .tok_loop
    cmp al, ','
    je .tok_loop
    cmp al, ';'
    je .tok_loop
    cmp al, '='
    je .tok_loop
.sk_adv:
    inc si
    jmp .sk_tok_ch

.copy_this_tok:
    mov di, bp
    xor cx, cx
    xor dl, dl
.cp_tok_ch:
    mov al, byte [si]
    test al, al
    jz .term_tok
    cmp al, '"'
    jne .cp_nq
    xor dl, 1
    jmp .emit_tc
.cp_nq:
    test dl, dl
    jnz .emit_tc
    cmp al, 0x20
    je .term_tok
    cmp al, 0x09
    je .term_tok
    cmp al, ','
    je .term_tok
    cmp al, ';'
    je .term_tok
    cmp al, '='
    je .term_tok
.emit_tc:
    mov byte [di], al
    inc si
    inc di
    inc cx
    jmp .cp_tok_ch

.term_tok:
    mov byte [di], 0
    mov di, bp
    jmp .done_arg

.empty_arg:
    mov di, bp
    mov byte [di], 0
    xor cx, cx
.done_arg:
    pop es
    pop bp
    ret
@endproc

;; @proc phase1_expand
;; @abi dos16
;; @in DS:SI = raw logical line, DS:DI = 256-byte expanded output buffer
;; @out DS:DI = Phase-1 expanded line (NUL-terminated), CX = output length
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Expands %%, %*, %0..%9, %~[mods]0..9, %VAR%, and %VAR:...% accurately.
@proc phase1_expand
    push bp
    mov bp, sp
    sub sp, 10
    ; [bp-2]=out_buf, [bp-4]=out_len, [bp-6]=work_buf (256B), [bp-8]=xform_buf (256B), [bp-10]=in_batch
    mov word [bp - 2], di
    mov word [bp - 4], 0

    push si
    mov cx, 520
    call arena_alloc
    pop si
    jnc .p1_mem_ok
    mov di, word [bp - 2]
    mov byte [di], 0
    xor cx, cx
    add sp, 10
    pop bp
    ret
.p1_mem_ok:
    mov word [bp - 6], di
    add di, 260
    mov word [bp - 8], di

    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    mov byte [bp - 10], al
    pop es

.p1_lp:
    mov al, byte [si]
    test al, al
    jnz .p1_non_nul
    jmp .p1_done
.p1_non_nul:
    cmp al, '%'
    je .got_pct

    ; DBCS atomic 2-byte copy so trail byte '%' (if any) is never misparsed!
    push ax
    call dbcs_is_lead
    pop ax
    jnc .emit_single
    call p1_append_al
    inc si
    mov al, byte [si]
    test al, al
    jnz .emit_single
    jmp .p1_done
.emit_single:
    call p1_append_al
    inc si
    jmp .p1_lp

.got_pct:
    inc si
    mov al, byte [si]
    test al, al
    jnz .pct_non_end
    ; Trailing bare '%': dropped in batch mode, kept in interactive REPL!
    cmp byte [bp - 10], 0
    jne .j_p1_done
    mov al, '%'
    call p1_append_al
.j_p1_done:
    jmp .p1_done

.pct_non_end:
    ; 1. '%%' -> single '%' in batch mode (which naturally lowers %%I -> %I for Phase 3!), '%%' in REPL
    cmp al, '%'
    jne .chk_star
    mov al, '%'
    call p1_append_al
    cmp byte [bp - 10], 0
    jne .adv_dbl_pct
    mov al, '%'
    call p1_append_al
.adv_dbl_pct:
    inc si
    jmp .p1_lp

.chk_star:
    ; 2. '%*' -> full raw argument tail of current batch frame!
    cmp al, '*'
    jne .chk_tilde
    cmp byte [bp - 10], 0
    je .lit_star
    inc si
    push si
    mov bl, byte [bp - 10]
    dec bl
    xor bh, bh
    mov ax, bx
    mov cl, 64
    mul cl
    mov si, TRN_BATCH_ARGS
    add si, ax
.sk_lead_star:
    cmp byte [si], 0x20
    jne .cp_star_lp
    inc si
    jmp .sk_lead_star
.cp_star_lp:
    mov al, byte [si]
    test al, al
    jz .done_star
    call p1_append_al
    inc si
    jmp .cp_star_lp
.done_star:
    pop si
    jmp .p1_lp
.lit_star:
    mov al, '%'
    call p1_append_al
    jmp .p1_lp

.chk_tilde:
    ; 3. '%~[modifiers]0..9' in batch mode
    cmp al, '~'
    jne .chk_digit
    cmp byte [bp - 10], 0
    je .scan_var_name
    push si
    inc si
    call mod_parse_flags
    jc .restore_not_mod
    mov al, byte [si]
    cmp al, '0'
    jb .restore_not_mod
    cmp al, '9'
    ja .restore_not_mod
    sub al, '0'
    inc si
    ; Discard saved pre-'~' SI and push post-digit SI, then BX (mod_mask) and DX ($VAR ptr)
    pop cx
    push si
    push bx
    push dx
    mov di, word [bp - 6]
    call batch_get_nth_arg
    pop dx
    pop bx
    mov si, word [bp - 6]
    mov di, word [bp - 8]
    call mod_eval_value
    mov si, word [bp - 8]
    call p1_append_asciiz
    pop si
    jmp .p1_lp

.restore_not_mod:
    pop si
    jmp .scan_var_name

.chk_digit:
    ; 4. '%0'..'9' positional batch argument!
    cmp al, '0'
    jb .scan_var_name
    cmp al, '9'
    ja .scan_var_name
    cmp byte [bp - 10], 0
    je .scan_var_name
    sub al, '0'
    inc si
    push si
    mov di, word [bp - 6]
    call batch_get_nth_arg
    mov si, word [bp - 6]
    call p1_append_asciiz
    pop si
    jmp .p1_lp

.scan_var_name:
    ; 5. Scan for closing '%' (and optional ':' operator delimiter)
    ; SI currently points right after opening '%'
    mov bx, si      ; BX = start of var name
    xor cx, cx      ; CX = name length
    xor dx, dx      ; DX = ptr to ':' + 1 (0 if no ':')
.vn_lp:
    mov al, byte [si]
    test al, al
    jz .unclosed_pct
    cmp al, '%'
    je .closed_var
    cmp al, ':'
    jne .vn_adv
    test dx, dx
    jnz .vn_adv
    mov dx, si
    inc dx
    inc si
    jmp .vn_lp
.vn_adv:
    test dx, dx
    jnz .vn_op_adv
    inc cx
.vn_op_adv:
    inc si
    jmp .vn_lp

.unclosed_pct:
    ; Unclosed '%...' -> keep '%' literal and resume at BX ( crucial for '%I' FOR loop vars in REPL!)
    mov si, bx
    mov al, '%'
    call p1_append_al
    jmp .p1_lp

.closed_var:
    ; Closing '%' at SI! Advance SI past '%'
    mov di, si      ; DI = address of closing '%'
    inc si
    push si         ; save cursor after closing '%'
    push di         ; save closing '%' addr
    push dx         ; save op_ptr (0 if none)
    push bx         ; save var_start

    mov si, bx
    mov di, word [bp - 6]
    call env_get_var
    jc .undef_var

    ; Defined! Apply optional ':~s,l' or ':old=new' if op_ptr != 0
    pop bx          ; discard var_start
    pop bx          ; BX = op_ptr
    pop dx          ; DX = closing '%' addr
    test bx, bx
    jz .append_raw_var
    sub dx, bx      ; DX = op_len
    mov si, word [bp - 6]
    mov di, word [bp - 8]
    call var_apply_ops
    mov si, word [bp - 8]
    call p1_append_asciiz
    pop si
    jmp .p1_lp

.append_raw_var:
    mov si, word [bp - 6]
    call p1_append_asciiz
    pop si
    jmp .p1_lp

.undef_var:
    pop bx          ; BX = var_start
    pop dx          ; discard op_ptr
    pop cx          ; CX = closing '%' addr
    pop si          ; SI = after closing '%'
    ; In batch mode, undefined %VAR% expands to empty; in REPL mode, stays literal %VAR%!
    cmp byte [bp - 10], 0
    jne .j_p1_lp2
    mov al, '%'
    call p1_append_al
    push si
    mov si, bx
.lit_undef_lp:
    cmp si, cx
    ja .lit_undef_done
    mov al, byte [si]
    call p1_append_al
    inc si
    jmp .lit_undef_lp
.lit_undef_done:
    pop si
.j_p1_lp2:
    jmp .p1_lp

.p1_done:
    mov di, word [bp - 2]
    mov bx, word [bp - 4]
    mov byte [di + bx], 0
    mov cx, bx
    add sp, 10
    pop bp
    ret
@endproc

;; @proc p1_append_al
;; @abi dos16
;; @in AL = byte, [bp-2] = out_buf, [bp-4] = out_len
;; @out Appends AL if out_len < 252
;; @err none
;; @clobbers bx, di
;; @desc Safe bounded 1-byte append helper for phase1_expand.
@proc p1_append_al
    mov bx, word [bp - 4]
    cmp bx, 252
    jae .full
    mov di, word [bp - 2]
    mov byte [di + bx], al
    inc word [bp - 4]
.full:
    ret
@endproc

;; @proc p1_append_asciiz
;; @abi dos16
;; @in DS:SI = ASCIIZ string, [bp-2] = out_buf, [bp-4] = out_len
;; @out Appends string up to 252-byte cap
;; @err none
;; @clobbers ax, bx, si, di
;; @desc Appends an ASCIIZ string into phase1_expand output buffer.
@proc p1_append_asciiz
.lp:
    mov al, byte [si]
    test al, al
    jz .done
    call p1_append_al
    inc si
    jmp .lp
.done:
    ret
@endproc
