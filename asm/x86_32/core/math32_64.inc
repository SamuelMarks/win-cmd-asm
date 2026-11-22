;; @file math32_64.inc
;; @isa 386
;; @desc Hardware-trap-safe 32-bit SET /A arithmetic, 64-bit EDX:EAX file-size math, and decimal/comma formatters.

@cpu 386
@format inc

;; @proc sdiv32_safe
;; @abi win32
;; @in EAX = signed 32-bit dividend, ECX = signed 32-bit divisor
;; @out EAX = quotient, EDX = remainder, CF=0; or CF=1, EAX=ERR_DIV_BY_ZERO when ECX == 0
;; @err CF=1, EAX=ERR_DIV_BY_ZERO on division by zero
;; @clobbers eax, edx
;; @desc Performs trap-safe two's-complement 32-bit division guarding /0 and INT32_MIN / -1 before IDIV.
@proc sdiv32_safe
    test ecx, ecx
    jz .div_zero
    cmp ecx, -1
    jne .do_idiv
    cmp eax, 0x80000000
    jne .do_idiv
    xor edx, edx
    clc
    ret
.do_idiv:
    cdq
    idiv ecx
    clc
    ret
.div_zero:
    mov eax, ERR_DIV_BY_ZERO
    xor edx, edx
    stc
    ret
@endproc

;; @proc shl32_safe
;; @abi win32
;; @in EAX = 32-bit value, ECX = shift count
;; @out EAX = EAX << (ECX & 31)
;; @err none
;; @clobbers eax, ecx
;; @desc Masks shift count to 0..31 and shifts EAX left identically across all 5 ISAs.
@proc shl32_safe
    and ecx, 31
    shl eax, cl
    ret
@endproc

;; @proc sar32_safe
;; @abi win32
;; @in EAX = signed 32-bit value, ECX = shift count
;; @out EAX = EAX >> (ECX & 31) arithmetic
;; @err none
;; @clobbers eax, ecx
;; @desc Masks shift count to 0..31 and arithmetic-shifts EAX right identically across all 5 ISAs.
@proc sar32_safe
    and ecx, 31
    sar eax, cl
    ret
@endproc

;; @proc u64_add32
;; @abi win32
;; @in EDX:EAX = augend, [ESP+4]/[ESP+8] or ESI:ECX -> ECX = addend_lo, ESI = addend_hi
;; @out EDX:EAX = 64-bit unsigned sum
;; @err none
;; @clobbers eax, edx
;; @desc Adds 64-bit unsigned ESI:ECX into EDX:EAX on pure i386.
@proc u64_add32
    add eax, ecx
    adc edx, esi
    ret
@endproc

;; @proc u64_mul_u32
;; @abi win32
;; @in EDX:EAX = 64-bit multiplicand, ECX = 32-bit multiplier
;; @out EDX:EAX = low 64 bits of unsigned product
;; @err none
;; @clobbers eax, edx
;; @desc Multiplies 64-bit unsigned EDX:EAX by 32-bit unsigned ECX on pure i386.
@proc u64_mul_u32
    push ebx
    push esi
    mov ebx, eax
    mov esi, edx
    mov eax, esi
    mul ecx
    mov esi, eax
    mov eax, ebx
    mul ecx
    add edx, esi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u64_divmod10_32
;; @abi win32
;; @in EDX:EAX = 64-bit unsigned dividend
;; @out EDX:EAX = 64-bit quotient, ECX = remainder (0..9)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Divides 64-bit unsigned EDX:EAX by 10 without 32-bit DIV overflow traps.
@proc u64_divmod10_32
    push ebx
    push esi
    mov ebx, eax
    mov eax, edx
    xor edx, edx
    mov ecx, 10
    div ecx
    mov esi, eax
    mov eax, ebx
    div ecx
    mov ecx, edx
    mov edx, esi
    pop esi
    pop ebx
    ret
@endproc

;; @proc fmt_u64_plain32
;; @abi win32
;; @in EDX:EAX = 64-bit unsigned value, EDI = output buffer (>= 24 bytes)
;; @out EDI = NUL-terminated ASCII decimal string, ECX = byte length
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats 64-bit unsigned EDX:EAX as plain decimal ASCII into EDI.
@proc fmt_u64_plain32
    push ebx
    push esi
    push edi
    sub esp, 24
    mov ebx, esp
    xor esi, esi
.dig_lp:
    call u64_divmod10_32
    add cl, '0'
    mov byte [ebx + esi], cl
    inc esi
    mov ecx, eax
    or ecx, edx
    jnz .dig_lp

    mov ecx, esi
.rev_lp:
    dec esi
    mov al, byte [ebx + esi]
    mov byte [edi], al
    inc edi
    test esi, esi
    jnz .rev_lp
    mov byte [edi], 0

    add esp, 24
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc fmt_u64_commas32
;; @abi win32
;; @in EDX:EAX = 64-bit unsigned value, EDI = output buffer (>= 32 bytes)
;; @out EDI = NUL-terminated thousand-grouped decimal string (e.g. "1,234,567"), ECX = length
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats 64-bit unsigned EDX:EAX with thousand-grouping commas for DIR byte summaries.
@proc fmt_u64_commas32
    push ebx
    push esi
    push edi
    sub esp, 28
    mov ebx, esp
    xor esi, esi
.collect:
    call u64_divmod10_32
    add cl, '0'
    mov byte [ebx + esi], cl
    inc esi
    mov ecx, eax
    or ecx, edx
    jnz .collect

    mov edx, edi
.emit_lp:
    dec esi
    mov al, byte [ebx + esi]
    mov byte [edi], al
    inc edi
    test esi, esi
    jz .finish
    mov eax, esi
    push edx
    xor edx, edx
    mov ecx, 3
    div ecx
    test edx, edx
    pop edx
    jnz .emit_lp
    mov byte [edi], ','
    inc edi
    jmp .emit_lp

.finish:
    mov byte [edi], 0
    mov ecx, edi
    sub ecx, edx
    add esp, 28
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc fmt_i32_dec
;; @abi win32
;; @in EAX = signed 32-bit integer, EDI = output buffer (>= 16 bytes)
;; @out EDI = NUL-terminated signed decimal string, ECX = length
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats a signed 32-bit integer (including INT32_MIN -2147483648) into EDI.
@proc fmt_i32_dec
    push edi
    mov edx, edi
    test eax, eax
    jns .pos
    mov byte [edi], '-'
    inc edi
    neg eax
.pos:
    push edx
    xor edx, edx
    call fmt_u64_plain32
    pop edx
    pop edi
    cmp byte [edi], '-'
    jne .done
    inc ecx
.done:
    ret
@endproc

;; @proc fmt_hex32_8
;; @abi win32
;; @in EAX = 32-bit value, EDI = output buffer (>= 9 bytes)
;; @out EDI = 8-character uppercase hexadecimal string + NUL, ECX = 8
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats EAX as an 8-digit zero-padded uppercase hex string (used for %=ExitCode% and Volume Serial).
@proc fmt_hex32_8
    push edi
    mov edx, eax
    mov ecx, 8
.hex_lp:
    rol edx, 4
    mov al, dl
    and al, 0x0F
    cmp al, 10
    jb .is_dig
    add al, 'A' - 10
    jmp .store
.is_dig:
    add al, '0'
.store:
    mov byte [edi], al
    inc edi
    dec ecx
    jnz .hex_lp
    mov byte [edi], 0
    mov ecx, 8
    pop edi
    ret
@endproc

;; @proc fmt_u32_pad2
;; @abi win32
;; @in EAX = 0..99 value, DL = pad char ('0' or ' '), EDI = output cursor
;; @out Writes 2 chars at EDI, EAX = EDI + 2 (EDI preserved)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats a 2-digit date/time field with configurable leading zero or leading space.
@proc fmt_u32_pad2
    push edx
    xor edx, edx
    mov ecx, 10
    div ecx
    pop ecx
    test eax, eax
    jnz .ten_dig
    mov byte [edi], cl
    jmp .ones
.ten_dig:
    add al, '0'
    mov byte [edi], al
.ones:
    add dl, '0'
    mov byte [edi + 1], dl
    lea eax, [edi + 2]
    ret
@endproc
