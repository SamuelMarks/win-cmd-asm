;; @file arith16.inc
;; @isa 8086
;; @desc Precedence-climbing 32-bit signed SET /A evaluator (+, -, *, /, %, <<, >>, &, ^, |, parens).

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

;; @proc sa_eval_atom
;; @abi dos16
;; @in DS:SI = expression cursor
;; @out DX:AX = 32-bit value, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_* on unbalanced parens or invalid number
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates parenthesized subexpressions, unary + - ~ !, or integer literals.
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
    call parse_i32_lit
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
;; @desc Evaluates multiplicative tier (*, /, %) left-to-right.
@proc sa_eval_mul
    call sa_eval_atom
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, '*'
    je .do_mul
    cmp al, '/'
    je .do_div
    cmp al, '%'
    je .do_mod
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
;; @desc Evaluates additive tier (+, -) left-to-right.
@proc sa_eval_add
    call sa_eval_mul
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, '+'
    je .do_add
    cmp al, '-'
    je .do_sub
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

;; @proc sa_eval_expr
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = 32-bit bitwise/shift/additive result, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX = ERR_*
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates bitwise (&, ^, |) and additive/multiplicative SET /A expressions.
@proc sa_eval_expr
    call sa_eval_add
    jc .err
.lp:
    push dx
    push ax
    call sa_skip_ws
    cmp al, '&'
    je .do_and
    cmp al, '^'
    je .do_xor
    cmp al, '|'
    je .do_or
    pop ax
    pop dx
    clc
    ret

.do_and:
    inc si
    call sa_eval_add
    jc .pop_err
    pop cx
    pop bx
    and ax, cx
    and dx, bx
    jmp .lp

.do_xor:
    inc si
    call sa_eval_add
    jc .pop_err
    pop cx
    pop bx
    xor ax, cx
    xor dx, bx
    jmp .lp

.do_or:
    inc si
    call sa_eval_add
    jc .pop_err
    pop cx
    pop bx
    or ax, cx
    or dx, bx
    jmp .lp

.pop_err:
    pop cx
    pop cx
.err:
    stc
    ret
@endproc
