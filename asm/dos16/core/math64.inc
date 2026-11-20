;; @file math64.inc
;; @isa 8086
;; @desc 64-bit unsigned integer math and comma-grouped formatter for >32 MiB FAT16 & >2 GiB FAT32.

@cpu 8086
@format inc

;; @proc u64_zero
;; @abi dos16
;; @in DS:DI = 8-byte u64 buffer
;; @out [DI..DI+7] = 0
;; @err none
;; @clobbers ax
;; @desc Zeroes an 8-byte little-endian unsigned 64-bit integer at DS:DI.
@proc u64_zero
    xor ax, ax
    mov word [di], ax
    mov word [di + 2], ax
    mov word [di + 4], ax
    mov word [di + 6], ax
    ret
@endproc

;; @proc u64_add
;; @abi dos16
;; @in DS:DI = 8-byte u64 accumulator, DS:SI = 8-byte u64 addend
;; @out [DI] += [SI], CF = carry out of bit 63
;; @err none
;; @clobbers ax
;; @desc Adds 64-bit [DS:SI] into [DS:DI] across four 16-bit words.
@proc u64_add
    mov ax, word [si]
    add word [di], ax
    mov ax, word [si + 2]
    adc word [di + 2], ax
    mov ax, word [si + 4]
    adc word [di + 4], ax
    mov ax, word [si + 6]
    adc word [di + 6], ax
    ret
@endproc

;; @proc u64_sub
;; @abi dos16
;; @in DS:DI = 8-byte u64 minuend, DS:SI = 8-byte u64 subtrahend
;; @out [DI] -= [SI], CF = borrow out of bit 63
;; @err none
;; @clobbers ax
;; @desc Subtracts 64-bit [DS:SI] from [DS:DI] across four 16-bit words.
@proc u64_sub
    mov ax, word [si]
    sub word [di], ax
    mov ax, word [si + 2]
    sbb word [di + 2], ax
    mov ax, word [si + 4]
    sbb word [di + 4], ax
    mov ax, word [si + 6]
    sbb word [di + 6], ax
    ret
@endproc

;; @proc u64_divmod10
;; @abi dos16
;; @in DS:SI = 8-byte u64 value (modified in-place to quotient / 10)
;; @out DL = remainder (0..9), ZF=1 if new quotient == 0
;; @err none
;; @clobbers ax, bx, cx, dx
;; @desc Divides 64-bit [DS:SI] in-place by 10 from MSW to LSW without overflow traps.
@proc u64_divmod10
    xor dx, dx
    mov bx, 10

    mov ax, word [si + 6]
    div bx
    mov word [si + 6], ax
    mov cx, ax

    mov ax, word [si + 4]
    div bx
    mov word [si + 4], ax
    or cx, ax

    mov ax, word [si + 2]
    div bx
    mov word [si + 2], ax
    or cx, ax

    mov ax, word [si]
    div bx
    mov word [si], ax
    or cx, ax
    ret
@endproc

;; @proc fmt_u64_commas
;; @abi dos16
;; @in DS:SI = 8-byte u64 scratch copy, DS:DI = 32-byte destination buffer, BL = sep (',' or 0)
;; @out DS:DI = start of formatted string, CX = character length
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Formats a 64-bit integer in decimal with optional 3-digit comma separators.
@proc fmt_u64_commas
    push bp
    mov bp, di
    add di, 30
    mov byte [di], 0
    mov bh, bl
    xor ch, ch
.dig_lp:
    push bx
    push cx
    call u64_divmod10
    pop cx
    pop bx
    jz .last_digit
    add dl, '0'
    dec di
    mov byte [di], dl
    inc ch
    cmp ch, 3
    jne .dig_lp
    xor ch, ch
    test bh, bh
    jz .dig_lp
    dec di
    mov byte [di], bh
    jmp .dig_lp

.last_digit:
    add dl, '0'
    dec di
    mov byte [di], dl
    mov ax, bp
    add ax, 30
    sub ax, di
    mov cx, ax
    pop bp
    ret
@endproc
