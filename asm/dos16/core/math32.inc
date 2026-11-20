;; @file math32.inc
;; @isa 8086
;; @desc Hardware-trap-safe (INT 00h-free) 32-bit signed/unsigned arithmetic for SET /A on 8086.

@cpu 8086
@format inc

;; @proc u32_neg
;; @abi dos16
;; @in DX:AX = 32-bit value
;; @out DX:AX = two's-complement negation (-DX:AX)
;; @err none
;; @clobbers ax, dx
;; @desc Negates a 32-bit integer in DX:AX without 80386 registers.
@proc u32_neg
    not dx
    not ax
    add ax, 1
    adc dx, 0
    ret
@endproc

;; @proc u32_mul
;; @abi dos16
;; @in DX:AX = lhs, BX:CX = rhs
;; @out DX:AX = low 32 bits of (lhs * rhs)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Wrapping 32x32->32-bit multiplication using three 16x16 MULs on strict 8086.
@proc u32_mul
    mov si, dx
    mov di, ax
    ; high_partial = (lhs_lo * rhs_hi) + (lhs_hi * rhs_lo)
    mov ax, di
    mul bx
    mov bx, ax
    mov ax, si
    mul cx
    add bx, ax
    ; full_lo = lhs_lo * rhs_lo
    mov ax, di
    mul cx
    add dx, bx
    ret
@endproc

;; @proc udiv32
;; @abi dos16
;; @in DX:AX = unsigned dividend, BX:CX = unsigned divisor
;; @out DX:AX = quotient, SI:DI = remainder, CF=0 on success
;; @err CF=1, AX=ERR_DIV_BY_ZERO if BX:CX == 0
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Bit-serial 32-bit unsigned restoring division immune to 8086 INT 00h quotient traps.
@proc udiv32
    push bp
    mov si, bx
    or si, cx
    jz .div_zero

    xor si, si
    xor di, di
    mov bp, 32
.bit_lp:
    shl ax, 1
    rcl dx, 1
    rcl di, 1
    rcl si, 1
    cmp si, bx
    jb .next_bit
    ja .sub_div
    cmp di, cx
    jb .next_bit
.sub_div:
    sub di, cx
    sbb si, bx
    or ax, 1
.next_bit:
    dec bp
    jnz .bit_lp

    pop bp
    clc
    ret

.div_zero:
    pop bp
    mov ax, ERR_DIV_BY_ZERO
    stc
    ret
@endproc

;; @proc idiv32
;; @abi dos16
;; @in DX:AX = signed 32-bit dividend, BX:CX = signed 32-bit divisor
;; @out DX:AX = signed quotient, SI:DI = signed remainder, CF=0 on success
;; @err CF=1, AX=ERR_DIV_BY_ZERO if divisor == 0
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Signed 32-bit SET /A division handling /0 and INT32_MIN / -1 without hardware traps.
@proc idiv32
    push bp
    mov bp, sp
    sub sp, 4

    mov si, bx
    or si, cx
    jz .div_zero

    ; Check INT32_MIN (0x80000000) / -1 (0xFFFFFFFF) -> wrapping 0x80000000, rem 0
    cmp dx, 0x8000
    jne .signs
    test ax, ax
    jnz .signs
    cmp bx, 0xFFFF
    jne .signs
    cmp cx, 0xFFFF
    jne .signs
    xor si, si
    xor di, di
    add sp, 4
    pop bp
    clc
    ret

.signs:
    ; [bp-2] = dividend sign (0 or 1), [bp-4] = quotient sign (0 or 1)
    mov word [bp - 2], 0
    mov word [bp - 4], 0
    test dx, 0x8000
    jz .chk_rhs
    mov word [bp - 2], 1
    mov word [bp - 4], 1
    call u32_neg
.chk_rhs:
    test bx, 0x8000
    jz .do_udiv
    xor word [bp - 4], 1
    not bx
    not cx
    add cx, 1
    adc bx, 0
.do_udiv:
    call udiv32
    cmp word [bp - 4], 0
    je .chk_rem
    call u32_neg
.chk_rem:
    cmp word [bp - 2], 0
    je .done
    not si
    not di
    add di, 1
    adc si, 0
.done:
    add sp, 4
    pop bp
    clc
    ret

.div_zero:
    add sp, 4
    pop bp
    mov ax, ERR_DIV_BY_ZERO
    stc
    ret
@endproc

;; @proc shl32_masked
;; @abi dos16
;; @in DX:AX = value, CL = shift count
;; @out DX:AX = value << (CL & 31)
;; @err none
;; @clobbers ax, cx, dx
;; @desc Shifts DX:AX left by (CL & 31) matching IA-32/x64/ARM64 SET /A semantics.
@proc shl32_masked
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

;; @proc sar32_masked
;; @abi dos16
;; @in DX:AX = signed value, CL = shift count
;; @out DX:AX = arithmetic right shift by (CL & 31)
;; @err none
;; @clobbers ax, cx, dx
;; @desc Shifts signed DX:AX right by (CL & 31) matching IA-32/x64/ARM64 SET /A semantics.
@proc sar32_masked
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
