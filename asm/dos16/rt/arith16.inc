;; @file arith16.inc
;; @isa 8086
;; @desc Full 11-tier precedence 32-bit signed SET /A evaluator (parens, unary, * / %, + -, << >>, &, ^, |, =..>>=, comma, 'c', bare vars).

@cpu 8086
@format inc

;; @proc sa_skip_ws
;; @abi dos16
;; @in DS:SI = expression cursor
;; @out DS:SI = non-whitespace cursor, AL = [SI]
;; @err none
;; @clobbers ax, si
;; @desc Advances SI past spaces and tabs inside SET /A expressions.
@proc sa_skip_ws
.lp:
    mov al, byte [si]
    cmp al, 0x20
    je .adv
    cmp al, 0x09
    je .adv
    ret
.adv:
    inc si
    jmp .lp
@endproc

;; @proc sa_is_ident_start
;; @abi dos16
;; @in AL = ASCII byte
;; @out CF=0 if AL is A..Z, a..z, or '_'; CF=1 otherwise
;; @err CF=1 if not an identifier start char
;; @clobbers none
;; @desc Tests whether AL begins a bare SET /A variable identifier.
@proc sa_is_ident_start
    cmp al, '_'
    je .ok
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    jbe .ok
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
.ok:
    clc
    ret
.no:
    stc
    ret
@endproc

;; @proc sa_is_ident_cont
;; @abi dos16
;; @in AL = ASCII byte
;; @out CF=0 if AL is A..Z, a..z, 0..9, or '_'; CF=1 otherwise
;; @err CF=1 if not an identifier continuation char
;; @clobbers none
;; @desc Tests whether AL continues a bare SET /A variable identifier.
@proc sa_is_ident_cont
    call sa_is_ident_start
    jnc .ok
    cmp al, '0'
    jb .no
    cmp al, '9'
    ja .no
.ok:
    clc
    ret
.no:
    stc
    ret
@endproc

;; @proc sa_read_var_val
;; @abi dos16
;; @in DS:BX = variable name, CX = name length
;; @out DX:AX = 32-bit value of variable (0 if undefined or non-numeric), CF=0
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Queries environment variable BX[0..CX] and parses its 32-bit signed value (default 0).
@proc sa_read_var_val
    push bp
    push si
    mov bp, sp
    sub sp, 2
    mov ax, word [wArenaPtr]
    mov word [bp - 2], ax

    push bx
    push cx
    mov cx, 136
    call arena_alloc
    pop cx
    pop si
    jc .ret_zero
    push di
    call env_get_var
    pop si
    jc .ret_zero
    call sa_skip_ws
    test al, al
    jz .ret_zero
    call parse_i32_lit
    jc .ret_zero
    jmp .done

.ret_zero:
    xor ax, ax
    xor dx, dx
.done:
    mov bx, word [bp - 2]
    mov word [wArenaPtr], bx
    add sp, 2
    pop si
    pop bp
    clc
    ret
@endproc

;; @proc sa_eval_atom
;; @abi dos16
;; @in DS:SI = expression cursor
;; @out DX:AX = 32-bit value, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_* on unbalanced parens or invalid number
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates (expr), unary + - ~ !, 'c' char literals, bare variables, or 0x/0/dec literals.
@proc sa_eval_atom
    call sa_skip_ws
    cmp al, '('
    je .paren
    cmp al, '-'
    je .unary_neg
    cmp al, '+'
    je .unary_pos
    cmp al, '~'
    je .unary_not
    cmp al, '!'
    je .unary_lnot
    cmp al, 0x27
    je .char_lit
    call sa_is_ident_start
    jnc .bare_var
    call parse_i32_lit
    ret

.char_lit:
    mov al, byte [si + 1]
    test al, al
    jz .syn_err
    cmp byte [si + 2], 0x27
    jne .syn_err
    xor ah, ah
    xor dx, dx
    add si, 3
    clc
    ret
.syn_err:
    mov ax, ERR_SYNTAX
    stc
    ret

.bare_var:
    mov bx, si
    xor cx, cx
.bv_lp:
    mov al, byte [si]
    call sa_is_ident_cont
    jc .bv_got
    inc si
    inc cx
    jmp .bv_lp
.bv_got:
    call sa_read_var_val
    ret

.paren:
    inc si
    call sa_eval_expr
    jc .err
    push dx
    push ax
    call sa_skip_ws
    cmp al, ')'
    jne .bad_paren
    inc si
    pop ax
    pop dx
    clc
    ret
.bad_paren:
    pop ax
    pop dx
    mov ax, ERR_UNBALANCED_PARENS
    stc
    ret

.unary_pos:
    inc si
    call sa_eval_atom
    ret

.unary_neg:
    inc si
    call sa_eval_atom
    jc .err
    call u32_neg
    clc
    ret

.unary_not:
    inc si
    call sa_eval_atom
    jc .err
    not dx
    not ax
    clc
    ret

.unary_lnot:
    inc si
    call sa_eval_atom
    jc .err
    or ax, dx
    mov dx, 0
    jz .set_one
    xor ax, ax
    clc
    ret
.set_one:
    mov ax, 1
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc sa_eval_mul
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit term result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_DIV_BY_ZERO or syntax error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates multiplicative tier (*, /, %) left-to-right (ignoring *=, /=, %=).
@proc sa_eval_mul
    call sa_eval_atom
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp byte [si + 1], '='
    je .done_ok
    cmp al, '*'
    je .do_mul
    cmp al, '/'
    je .do_div
    cmp al, '%'
    je .do_mod
.done_ok:
    pop ax
    pop dx
    clc
    ret

.do_mul:
    inc si
    call sa_eval_atom
    jc .pop_err
    mov bx, dx
    mov cx, ax
    pop ax
    pop dx
    push si
    call u32_mul
    pop si
    jmp .lp

.do_div:
    inc si
    call sa_eval_atom
    jc .pop_err
    mov bx, dx
    mov cx, ax
    pop ax
    pop dx
    push si
    call idiv32
    pop si
    jc .err
    jmp .lp

.do_mod:
    inc si
    call sa_eval_atom
    jc .pop_err
    mov bx, dx
    mov cx, ax
    pop ax
    pop dx
    push si
    call idiv32
    mov dx, si
    mov ax, di
    pop si
    jc .err
    jmp .lp

.pop_err:
    pop cx
    pop cx
.err:
    stc
    ret
@endproc

;; @proc sa_eval_add
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit additive result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates additive tier (+, -) left-to-right (ignoring +=, -=).
@proc sa_eval_add
    call sa_eval_mul
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp byte [si + 1], '='
    je .done_ok
    cmp al, '+'
    je .do_add
    cmp al, '-'
    je .do_sub
.done_ok:
    pop ax
    pop dx
    clc
    ret

.do_add:
    inc si
    call sa_eval_mul
    jc .pop_err
    pop cx
    pop bx
    add ax, cx
    adc dx, bx
    jmp .lp

.do_sub:
    inc si
    call sa_eval_mul
    jc .pop_err
    mov cx, ax
    mov bx, dx
    pop ax
    pop dx
    sub ax, cx
    sbb dx, bx
    jmp .lp

.pop_err:
    pop cx
    pop cx
.err:
    stc
    ret
@endproc

;; @proc sa_shl32_masked
;; @abi dos16
;; @in DX:AX = 32-bit value, CL = shift count
;; @out DX:AX = DX:AX << (CL & 31)
;; @err none
;; @clobbers ax, cx, dx
;; @desc Performs hardware-consistent 32-bit left shift masked to 0..31 on strict 8086.
@proc sa_shl32_masked
    and cl, 31
    jz .done
.lp:
    shl ax, 1
    rcl dx, 1
    dec cl
    jnz .lp
.done:
    ret
@endproc

;; @proc sa_sar32_masked
;; @abi dos16
;; @in DX:AX = signed 32-bit value, CL = shift count
;; @out DX:AX = DX:AX >> (CL & 31) (arithmetic sign-preserving)
;; @err none
;; @clobbers ax, cx, dx
;; @desc Performs hardware-consistent 32-bit arithmetic right shift masked to 0..31 on 8086.
@proc sa_sar32_masked
    and cl, 31
    jz .done
.lp:
    sar dx, 1
    rcr ax, 1
    dec cl
    jnz .lp
.done:
    ret
@endproc

;; @proc sa_eval_shift
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit shift result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates bitwise shift tier (<<, >>) with count & 31 (ignoring <<=, >>=).
@proc sa_eval_shift
    call sa_eval_add
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, '<'
    je .chk_shl
    cmp al, '>'
    je .chk_shr
.done_ok:
    pop ax
    pop dx
    clc
    ret

.chk_shl:
    cmp byte [si + 1], '<'
    jne .done_ok
    cmp byte [si + 2], '='
    je .done_ok
    add si, 2
    call sa_eval_add
    jc .pop_err
    mov cl, al
    pop ax
    pop dx
    call sa_shl32_masked
    jmp .lp

.chk_shr:
    cmp byte [si + 1], '>'
    jne .done_ok
    cmp byte [si + 2], '='
    je .done_ok
    add si, 2
    call sa_eval_add
    jc .pop_err
    mov cl, al
    pop ax
    pop dx
    call sa_sar32_masked
    jmp .lp

.pop_err:
    pop cx
    pop cx
.err:
    stc
    ret
@endproc

;; @proc sa_eval_bitand
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit bitwise AND/XOR/OR result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates bitwise & tier (higher precedence than ^ and |; ignores &=).
@proc sa_eval_bitand
    call sa_eval_shift
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, '&'
    jne .done_ok
    cmp byte [si + 1], '='
    je .done_ok
    inc si
    call sa_eval_shift
    jc .pop_err
    pop cx
    pop bx
    and ax, cx
    and dx, bx
    jmp .lp
.done_ok:
    pop ax
    pop dx
    clc
    ret
.pop_err:
    pop cx
    pop cx
.err:
    stc
    ret
@endproc

;; @proc sa_eval_bitxor
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit bitwise XOR result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates bitwise ^ tier (between & and |; ignores ^=).
@proc sa_eval_bitxor
    call sa_eval_bitand
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, '^'
    jne .done_ok
    cmp byte [si + 1], '='
    je .done_ok
    inc si
    call sa_eval_bitand
    jc .pop_err
    pop cx
    pop bx
    xor ax, cx
    xor dx, bx
    jmp .lp
.done_ok:
    pop ax
    pop dx
    clc
    ret
.pop_err:
    pop cx
    pop cx
.err:
    stc
    ret
@endproc

;; @proc sa_eval_bitor
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit bitwise OR result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates bitwise | tier (ignores |=).
@proc sa_eval_bitor
    call sa_eval_bitxor
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, '|'
    jne .done_ok
    cmp byte [si + 1], '='
    je .done_ok
    inc si
    call sa_eval_bitxor
    jc .pop_err
    pop cx
    pop bx
    or ax, cx
    or dx, bx
    jmp .lp
.done_ok:
    pop ax
    pop dx
    clc
    ret
.pop_err:
    pop cx
    pop cx
.err:
    stc
    ret
@endproc

;; @proc sa_apply_compound_op
;; @abi dos16
;; @in BL = op code (0='=', 1='+', 2='-', 3='*', 4='/', 5='%', 6='&', 7='^', 8='|', 9='<', 10='>'),
;; @in [bp-10]:[bp-12] = old LHS 32-bit value (DX:AX), DX:AX = RHS 32-bit value
;; @out DX:AX = combined 32-bit result, CF=0 on success
;; @err CF=1, AX = ERR_DIV_BY_ZERO on /= 0 or %= 0
;; @clobbers ax, bx, cx, dx, di
;; @desc Combines old variable value and RHS for all 11 SET /A assignment operators.
@proc sa_apply_compound_op
    cmp bl, 0
    jne .not_plain
    clc
    ret
.not_plain:
    cmp bl, 1
    jne .chk_sub
    add ax, word [bp - 12]
    adc dx, word [bp - 10]
    clc
    ret
.chk_sub:
    cmp bl, 2
    jne .chk_mul
    mov cx, ax
    mov bx, dx
    mov ax, word [bp - 12]
    mov dx, word [bp - 10]
    sub ax, cx
    sbb dx, bx
    clc
    ret
.chk_mul:
    cmp bl, 3
    jne .chk_div
    mov bx, dx
    mov cx, ax
    mov ax, word [bp - 12]
    mov dx, word [bp - 10]
    push si
    call u32_mul
    pop si
    clc
    ret
.chk_div:
    cmp bl, 4
    jne .chk_mod
    mov bx, dx
    mov cx, ax
    mov ax, word [bp - 12]
    mov dx, word [bp - 10]
    push si
    call idiv32
    pop si
    ret
.chk_mod:
    cmp bl, 5
    jne .chk_and
    mov bx, dx
    mov cx, ax
    mov ax, word [bp - 12]
    mov dx, word [bp - 10]
    push si
    call idiv32
    mov dx, si
    mov ax, di
    pop si
    ret
.chk_and:
    cmp bl, 6
    jne .chk_xor
    and ax, word [bp - 12]
    and dx, word [bp - 10]
    clc
    ret
.chk_xor:
    cmp bl, 7
    jne .chk_or
    xor ax, word [bp - 12]
    xor dx, word [bp - 10]
    clc
    ret
.chk_or:
    cmp bl, 8
    jne .chk_shl
    or ax, word [bp - 12]
    or dx, word [bp - 10]
    clc
    ret
.chk_shl:
    cmp bl, 9
    jne .do_shr
    mov cl, al
    mov ax, word [bp - 12]
    mov dx, word [bp - 10]
    call sa_shl32_masked
    clc
    ret
.do_shr:
    mov cl, al
    mov ax, word [bp - 12]
    mov dx, word [bp - 10]
    call sa_sar32_masked
    clc
    ret
@endproc

;; @proc sa_eval_assign
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit assigned or evaluated value, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Right-associative assignment tier (= *= /= %= += -= &= ^= |= <<= >>=).
@proc sa_eval_assign
    call sa_skip_ws
    call sa_is_ident_start
    jnc .try_id
    call sa_eval_bitor
    ret
.try_id:
    push bp
    mov bp, sp
    sub sp, 12
    ; [bp-2]=saved_si, [bp-4]=id_ptr, [bp-6]=id_len, [bp-8]=op_code, [bp-10]=lhs_hi, [bp-12]=lhs_lo
    mov word [bp - 2], si
    mov word [bp - 4], si
    xor cx, cx
.id_lp:
    mov al, byte [si]
    call sa_is_ident_cont
    jc .id_end
    inc si
    inc cx
    jmp .id_lp
.id_end:
    mov word [bp - 6], cx
    call sa_skip_ws

    ; Classify assignment operator into BL (0..10) and advance SI!
    cmp al, '='
    jne .chk_2op
    cmp byte [si + 1], '='
    je .not_assign
    mov bl, 0
    inc si
    jmp .have_assign_op

.chk_2op:
    cmp byte [si + 1], '='
    jne .chk_3op
    mov bl, 1
    cmp al, '+'
    je .adv2
    mov bl, 2
    cmp al, '-'
    je .adv2
    mov bl, 3
    cmp al, '*'
    je .adv2
    mov bl, 4
    cmp al, '/'
    je .adv2
    mov bl, 5
    cmp al, '%'
    je .adv2
    mov bl, 6
    cmp al, '&'
    je .adv2
    mov bl, 7
    cmp al, '^'
    je .adv2
    mov bl, 8
    cmp al, '|'
    je .adv2
    jmp .not_assign
.adv2:
    add si, 2
    jmp .have_assign_op

.chk_3op:
    cmp al, '<'
    jne .chk_shr_eq
    cmp byte [si + 1], '<'
    jne .not_assign
    cmp byte [si + 2], '='
    jne .not_assign
    mov bl, 9
    add si, 3
    jmp .have_assign_op
.chk_shr_eq:
    cmp al, '>'
    jne .not_assign
    cmp byte [si + 1], '>'
    jne .not_assign
    cmp byte [si + 2], '='
    jne .not_assign
    mov bl, 10
    add si, 3
    jmp .have_assign_op

.not_assign:
    mov si, word [bp - 2]
    add sp, 12
    pop bp
    call sa_eval_bitor
    ret

.have_assign_op:
    mov byte [bp - 8], bl
    ; Evaluate RHS recursively (right-associative: A = B = 5!)
    call sa_eval_assign
    jc .asgn_err
    push dx
    push ax

    ; If compound op (bl != 0), read current value of LHS variable!
    cmp byte [bp - 8], 0
    je .combine_val
    mov bx, word [bp - 4]
    mov cx, word [bp - 6]
    call sa_read_var_val
    mov word [bp - 12], ax
    mov word [bp - 10], dx

.combine_val:
    pop ax
    pop dx
    mov bl, byte [bp - 8]
    call sa_apply_compound_op
    jc .asgn_err

    ; Format DX:AX into szNumScratch and store into environment!
    push si
    push dx
    push ax
    mov di, szNumScratch
    call fmt_i32_dec
    mov dx, di
    mov si, word [bp - 4]
    mov cx, word [bp - 6]
    call env_set_var
    pop ax
    pop dx
    pop si
    add sp, 12
    pop bp
    clc
    ret

.asgn_err:
    add sp, 12
    pop bp
    stc
    ret
@endproc

;; @proc sa_eval_expr
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = final 32-bit signed result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Top-level comma-separated SET /A evaluator returning the final expression's value.
@proc sa_eval_expr
    call sa_eval_assign
    jc .err
.comma_lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, ','
    jne .done
    pop ax
    pop dx
    inc si
    call sa_eval_assign
    jnc .comma_lp
    ret
.done:
    pop ax
    pop dx
    clc
    ret
.err:
    stc
    ret
@endproc
