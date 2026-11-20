;; @file str_8086.inc
;; @isa 8086
;; @desc Bounded string slices, ASCII/NLS case-folding, O(1) DBCS lead-byte test, and formatters.

@cpu 8086
@format inc

;; @proc chr_to_upper
;; @abi dos16
;; @in AL = ASCII/OEM byte
;; @out AL = uppercase byte
;; @err none
;; @clobbers ax
;; @desc Converts ASCII a..z to A..Z cleanly without side effects.
@proc chr_to_upper
    cmp al, 'a'
    jb .done
    cmp al, 'z'
    ja .done
    sub al, 0x20
.done:
    ret
@endproc

;; @proc dbcs_is_lead
;; @abi dos16
;; @in AL = byte to inspect, bmDbcsLead = 32-byte (256-bit) bitmap
;; @out CF=1 if AL is an active DBCS lead byte, CF=0 otherwise
;; @err none
;; @clobbers bx, cx
;; @desc Performs O(1) bit lookup in bmDbcsLead[AL >> 3] & (1 << (AL & 7)) on strict 8086.
@proc dbcs_is_lead
    mov bl, al
    xor bh, bh
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov cl, al
    and cl, 7
    mov ch, 1
    shl ch, cl
    test byte [bmDbcsLead + bx], ch
    jz .not_lead
    stc
    ret
.not_lead:
    clc
    ret
@endproc

;; @proc slice_eq_nocase
;; @abi dos16
;; @in DS:SI = slice A, CX = len A, DS:DI = slice B, DX = len B
;; @out ZF=1 (CF=0) if equal case-insensitively, CF=1 if unequal
;; @err none
;; @clobbers ax, bx, cx, si, di
;; @desc Compares two pointer+length byte slices case-insensitively in O(N).
@proc slice_eq_nocase
    cmp cx, dx
    jne .diff
    test cx, cx
    jz .same
.lp:
    mov al, byte [si]
    call chr_to_upper
    mov bl, al
    mov al, byte [di]
    call chr_to_upper
    cmp al, bl
    jne .diff
    inc si
    inc di
    loop .lp
.same:
    xor ax, ax
    clc
    ret
.diff:
    or ax, 1
    stc
    ret
@endproc

;; @proc fmt_i32_dec
;; @abi dos16
;; @in DX:AX = signed 32-bit integer, DS:DI = 16-byte scratch buffer
;; @out DS:DI = start of formatted text, CX = byte length
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Formats a signed 32-bit integer (e.g. SET /A result) into ASCII decimal.
@proc fmt_i32_dec
    push bp
    mov bp, di
    test dx, 0x8000
    jz .pos
    mov byte [di], '-'
    inc bp
    call u32_neg
.pos:
    mov si, bp
    add si, 12
    mov byte [si], 0
    mov di, si
.div_lp:
    xor bx, bx
    mov cx, 10
    push di
    call udiv32
    mov bx, di
    pop di
    add bl, '0'
    dec di
    mov byte [di], bl
    mov cx, ax
    or cx, dx
    jnz .div_lp

    ; Check if leading '-' was written before digits
    mov ax, bp
    sub ax, 1
    cmp di, bp
    je .calc_len
    mov si, di
    mov di, bp
.copy_up:
    mov al, byte [si]
    mov byte [di], al
    test al, al
    jz .copied
    inc si
    inc di
    jmp .copy_up
.copied:
    mov cx, di
    mov di, bp
    cmp byte [bp - 1], '-'
    jne .calc_sub
    dec di
.calc_sub:
    sub cx, di
    pop bp
    ret

.calc_len:
    mov cx, 12
    cmp byte [bp - 1], '-'
    jne .ret_done
    dec di
    inc cx
.ret_done:
    pop bp
    ret
@endproc

;; @proc parse_i32_lit
;; @abi dos16
;; @in DS:SI = cursor
;; @out DX:AX = parsed 32-bit integer, DS:SI = advanced, CF=0 on success
;; @err CF=1, AX=ERR_INVALID_NUMBER on invalid octal (08/09) or non-digit
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Parses decimal, 0x hex, or 0 octal integer literals for SET /A and IF.
@proc parse_i32_lit
    push bp
    xor bp, bp
    mov al, byte [si]
    cmp al, '-'
    jne .chk_plus
    mov bp, 1
    inc si
    jmp .start_num
.chk_plus:
    cmp al, '+'
    jne .start_num
    inc si
.start_num:
    xor dx, dx
    xor ax, ax
    mov bl, byte [si]
    cmp bl, '0'
    jb .bad_num
    cmp bl, '9'
    ja .bad_num
    cmp bl, '0'
    jne .dec_lp
    inc si
    mov bl, byte [si]
    cmp bl, 'x'
    je .hex_start
    cmp bl, 'X'
    je .hex_start
    jmp .oct_lp

.hex_start:
    inc si
    mov bl, byte [si]
    call hex_val_bl
    jc .bad_num
.hex_lp:
    mov bl, byte [si]
    call hex_val_bl
    jc .apply_sign
    mov cl, 4
    call shl32_masked
    xor bh, bh
    or ax, bx
    inc si
    jmp .hex_lp

.oct_lp:
    mov bl, byte [si]
    cmp bl, '0'
    jb .apply_sign
    cmp bl, '9'
    ja .apply_sign
    cmp bl, '7'
    ja .bad_num
    sub bl, '0'
    mov cl, 3
    call shl32_masked
    xor bh, bh
    add ax, bx
    adc dx, 0
    inc si
    jmp .oct_lp

.bad_num:
    pop bp
    mov ax, ERR_INVALID_NUMBER
    stc
    ret

.dec_lp:
    mov bl, byte [si]
    cmp bl, '0'
    jb .apply_sign
    cmp bl, '9'
    ja .apply_sign
    sub bl, '0'
    xor bh, bh
    push si
    push bx
    xor bx, bx
    mov cx, 10
    call u32_mul
    pop bx
    pop si
    add ax, bx
    adc dx, 0
    inc si
    jmp .dec_lp

.apply_sign:
    test bp, bp
    jz .ok
    call u32_neg
.ok:
    pop bp
    clc
    ret
@endproc

;; @proc hex_val_bl
;; @abi dos16
;; @in BL = ASCII char
;; @out BL = 0..15, CF=0 if hex digit; CF=1 otherwise
;; @err none
;; @clobbers bx
;; @desc Converts ASCII hex digit in BL to 0..15.
@proc hex_val_bl
    cmp bl, '0'
    jb .no
    cmp bl, '9'
    ja .chk_hi
    sub bl, '0'
    clc
    ret
.chk_hi:
    and bl, 0xDF
    cmp bl, 'A'
    jb .no
    cmp bl, 'F'
    ja .no
    sub bl, 55
    clc
    ret
.no:
    stc
    ret
@endproc
