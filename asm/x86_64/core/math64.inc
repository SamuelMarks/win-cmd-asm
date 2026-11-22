;; @file math64.inc
;; @isa x64
;; @desc Trap-safe 32-bit SET /A arithmetic, native 64-bit RAX math, and decimal/comma/hex formatters for x86-64.

@cpu x64
@format inc

;; @proc sdiv32_safe64
;; @abi win64_ms
;; @in ECX = signed 32-bit dividend, EDX = signed 32-bit divisor
;; @out EAX = quotient, EDX = remainder, CF=0; or CF=1, EAX=ERR_DIV_BY_ZERO when EDX == 0
;; @err CF=1, EAX=ERR_DIV_BY_ZERO on division by zero
;; @clobbers rax, rdx, r8
;; @desc Performs trap-safe two's-complement 32-bit division guarding /0 and INT32_MIN / -1 before IDIV.
@proc sdiv32_safe64
    mov eax, ecx
    mov r8d, edx
    test r8d, r8d
    jz .div_zero
    cmp r8d, -1
    jne .do_idiv
    cmp eax, 0x80000000
    jne .do_idiv
    xor edx, edx
    clc
    ret
.do_idiv:
    cdq
    idiv r8d
    clc
    ret
.div_zero:
    mov eax, ERR_DIV_BY_ZERO
    xor edx, edx
    stc
    ret
@endproc

;; @proc fmt_u64_plain64
;; @abi win64_ms
;; @in RCX = 64-bit unsigned value, RDX = output buffer (>= 24 bytes)
;; @out Writes NUL-terminated ASCII decimal string to RDX, EAX = byte length
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10
;; @desc Formats 64-bit unsigned RCX as plain decimal ASCII into RDX using native 64-bit DIV.
@proc_frame fmt_u64_plain64, 40
    mov rax, rcx
    mov r8, rdx
    xor r9d, r9d
    mov r10d, 10
.dig_lp:
    xor edx, edx
    div r10
    add dl, '0'
    mov byte [rsp + r9], dl
    inc r9
    test rax, rax
    jnz .dig_lp

    mov ecx, r9d
.rev_lp:
    dec r9
    mov al, byte [rsp + r9]
    mov byte [r8], al
    inc r8
    test r9, r9
    jnz .rev_lp
    mov byte [r8], 0
    mov eax, ecx
    @epilog_ret
@endproc

;; @proc fmt_u64_commas64
;; @abi win64_ms
;; @in RCX = 64-bit unsigned value, RDX = output buffer (>= 32 bytes)
;; @out Writes thousand-grouped decimal string to RDX (e.g. "1,234,567"), EAX = byte length
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Formats 64-bit unsigned RCX with thousand-grouping commas for DIR byte summaries.
@proc_frame fmt_u64_commas64, 40
    mov rax, rcx
    mov r8, rdx
    mov r11, rdx
    xor r9d, r9d
    mov r10d, 10
.collect:
    xor edx, edx
    div r10
    add dl, '0'
    mov byte [rsp + r9], dl
    inc r9
    test rax, rax
    jnz .collect

    mov ecx, 3
.emit_lp:
    dec r9
    mov al, byte [rsp + r9]
    mov byte [r8], al
    inc r8
    test r9, r9
    jz .finish
    mov eax, r9d
    xor edx, edx
    div ecx
    test edx, edx
    jnz .emit_lp
    mov byte [r8], ','
    inc r8
    jmp .emit_lp

.finish:
    mov byte [r8], 0
    mov rax, r8
    sub rax, r11
    @epilog_ret
@endproc

;; @proc fmt_i32_dec64
;; @abi win64_ms
;; @in ECX = signed 32-bit integer, RDX = output buffer (>= 16 bytes)
;; @out Writes NUL-terminated signed decimal string to RDX, EAX = byte length
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Formats a signed 32-bit integer (including INT32_MIN -2147483648) into RDX.
@proc_frame fmt_i32_dec64, 32, rbx
    mov ebx, 0
    movsxd rax, ecx
    test rax, rax
    jns .pos
    mov byte [rdx], '-'
    inc rdx
    neg rax
    mov ebx, 1
.pos:
    mov rcx, rax
    call fmt_u64_plain64
    add eax, ebx
    @epilog_ret
@endproc

;; @proc fmt_hex32_8_64
;; @abi win64_ms
;; @in ECX = 32-bit value, RDX = output buffer (>= 9 bytes)
;; @out Writes 8-character uppercase hex string + NUL to RDX, EAX = 8
;; @err none
;; @clobbers rax, rcx, rdx, r8
;; @desc Formats ECX as an 8-digit zero-padded uppercase hex string (%=ExitCode% and Volume Serial).
@proc fmt_hex32_8_64
    mov r8d, 8
.hex_lp:
    rol ecx, 4
    mov al, cl
    and al, 0x0F
    cmp al, 10
    jb .is_dig
    add al, 'A' - 10
    jmp .store
.is_dig:
    add al, '0'
.store:
    mov byte [rdx], al
    inc rdx
    dec r8d
    jnz .hex_lp
    mov byte [rdx], 0
    mov eax, 8
    ret
@endproc

;; @proc fmt_u32_pad2_64
;; @abi win64_ms
;; @in ECX = 0..99 value, DL = pad char ('0' or ' '), R8 = output cursor
;; @out Writes 2 chars at R8, RAX = R8 + 2
;; @err none
;; @clobbers rax, rcx, rdx, r9
;; @desc Formats a 2-digit date/time field with configurable leading zero or leading space.
@proc fmt_u32_pad2_64
    mov r9b, dl
    mov eax, ecx
    xor edx, edx
    mov ecx, 10
    div ecx
    test eax, eax
    jnz .ten_dig
    mov byte [r8], r9b
    jmp .ones
.ten_dig:
    add al, '0'
    mov byte [r8], al
.ones:
    add dl, '0'
    mov byte [r8 + 1], dl
    lea rax, [r8 + 2]
    ret
@endproc
