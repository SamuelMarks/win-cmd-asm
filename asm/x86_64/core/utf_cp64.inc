;; @file utf_cp64.inc
;; @isa x64
;; @desc Pure-ASM WTF-8 / UTF-8 <-> UTF-16LE transcoders, string helpers, and canonical ERR_* messages for x86-64.

@cpu x64
@format inc

;; @proc str_len64
;; @abi win64_ms
;; @in RCX = NUL-terminated string
;; @out RAX = byte length (excluding NUL)
;; @err none
;; @clobbers rax, rdx
;; @desc Computes byte length of a NUL-terminated string without modifying RCX.
@proc str_len64
    xor eax, eax
.lp:
    mov dl, byte [rcx + rax]
    test dl, dl
    jz .done
    inc rax
    jmp .lp
.done:
    ret
@endproc

;; @proc wcs_len64
;; @abi win64_ms
;; @in RCX = NUL-terminated UTF-16LE string
;; @out RAX = WCHAR count (excluding trailing 0x0000)
;; @err none
;; @clobbers rax, rdx
;; @desc Computes WCHAR code-unit length of a UTF-16LE string without modifying RCX.
@proc wcs_len64
    xor eax, eax
.lp:
    mov dx, word [rcx + rax * 2]
    test dx, dx
    jz .done
    inc rax
    jmp .lp
.done:
    ret
@endproc

;; @proc str_copy64
;; @abi win64_ms
;; @in RCX = destination buffer, RDX = source NUL-terminated string
;; @out Copies string + NUL, RAX = destination end (pointing at NUL)
;; @err none
;; @clobbers rax, rdx, r8
;; @desc Copies a NUL-terminated string from RDX to RCX, returning pointer to trailing NUL.
@proc str_copy64
    mov rax, rcx
    xor r8d, r8d
.lp:
    mov r8b, byte [rdx]
    mov byte [rax], r8b
    test r8b, r8b
    jz .done
    inc rdx
    inc rax
    jmp .lp
.done:
    ret
@endproc

;; @proc ascii_upper_al64
;; @abi win64_ms
;; @in AL = byte
;; @out AL = uppercase ASCII if 'a'..'z', else unchanged
;; @err none
;; @clobbers rax
;; @desc Folds ASCII lowercase 'a'..'z' to 'A'..'Z' in AL.
@proc ascii_upper_al64
    cmp al, 'a'
    jb .done
    cmp al, 'z'
    ja .done
    sub al, 32
.done:
    ret
@endproc

;; @proc str_eq_i64
;; @abi win64_ms
;; @in RCX = string A, RDX = string B
;; @out EAX = 1 if equal case-insensitively, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9
;; @desc Compares two NUL-terminated strings case-insensitively.
@proc str_eq_i64
.lp:
    movzx eax, byte [rcx]
    movzx r8d, byte [rdx]
    test eax, eax
    jz .chk_end
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    cmp r8b, 'a'
    jb .u2
    cmp r8b, 'z'
    ja .u2
    sub r8b, 32
.u2:
    cmp al, r8b
    jne .ne
    inc rcx
    inc rdx
    jmp .lp
.chk_end:
    test r8d, r8d
    jnz .ne
    mov eax, 1
    ret
.ne:
    xor eax, eax
    ret
@endproc

;; @proc str_cmp_i64
;; @abi win64_ms
;; @in RCX = string A, RDX = string B
;; @out EAX < 0 if A < B, 0 if A == B, > 0 if A > B (case-insensitive)
;; @err none
;; @clobbers rax, rcx, rdx, r8
;; @desc Lexicographically compares two NUL-terminated strings case-insensitively.
@proc str_cmp_i64
.lp:
    movzx eax, byte [rcx]
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    movzx r8d, byte [rdx]
    cmp r8b, 'a'
    jb .u2
    cmp r8b, 'z'
    ja .u2
    sub r8b, 32
.u2:
    sub eax, r8d
    jnz .done
    test r8d, r8d
    jz .done
    inc rcx
    inc rdx
    jmp .lp
.done:
    ret
@endproc

;; @proc str_istarts_with64
;; @abi win64_ms
;; @in RCX = candidate string, RDX = prefix string, R8 = prefix byte length
;; @out EAX = 1 if RCX begins with RDX (case-insensitive), 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9
;; @desc Tests whether RCX starts with the R8-byte prefix RDX case-insensitively.
@proc str_istarts_with64
.lp:
    test r8, r8
    jz .yes
    movzx eax, byte [rcx]
    movzx r9d, byte [rdx]
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    cmp r9b, 'a'
    jb .u2
    cmp r9b, 'z'
    ja .u2
    sub r9b, 32
.u2:
    cmp al, r9b
    jne .no
    inc rcx
    inc rdx
    dec r8
    jmp .lp
.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret
@endproc

;; @proc wtf8_to_utf16le64
;; @abi win64_ms
;; @in RCX = NUL-terminated WTF-8 / UTF-8 string
;; @out RAX = NUL-terminated UTF-16LE buffer in statement arena, RDX = WCHAR count (excl. NUL), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY on arena exhaustion
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Converts WTF-8 / UTF-8 (including unpaired NTFS surrogates and astral pairs) to UTF-16LE in arena.
@proc_frame wtf8_to_utf16le64, 32, rbx, rsi, rdi
    mov rsi, rcx
    call str_len64
    lea rcx, [rax + rax + 8]
    call arena_alloc64
    jc .oom
    mov rdi, rax
    mov rbx, rax

.dec_lp:
    movzx eax, byte [rsi]
    test eax, eax
    jz .done
    cmp eax, 0x80
    jb .ascii

    mov edx, eax
    and edx, 0xE0
    cmp edx, 0xC0
    jne .chk_3b
    movzx ecx, byte [rsi + 1]
    mov edx, ecx
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    and eax, 0x1F
    shl eax, 6
    and ecx, 0x3F
    or eax, ecx
    mov word [rdi], ax
    add rdi, 2
    add rsi, 2
    jmp .dec_lp

.chk_3b:
    mov edx, eax
    and edx, 0xF0
    cmp edx, 0xE0
    jne .chk_4b
    movzx ecx, byte [rsi + 1]
    mov edx, ecx
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    movzx r8d, byte [rsi + 2]
    mov edx, r8d
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    and eax, 0x0F
    shl eax, 12
    and ecx, 0x3F
    shl ecx, 6
    and r8d, 0x3F
    or eax, ecx
    or eax, r8d
    mov word [rdi], ax
    add rdi, 2
    add rsi, 3
    jmp .dec_lp

.chk_4b:
    mov edx, eax
    and edx, 0xF8
    cmp edx, 0xF0
    jne .raw_byte
    movzx ecx, byte [rsi + 1]
    mov edx, ecx
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    movzx r8d, byte [rsi + 2]
    mov edx, r8d
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    movzx r9d, byte [rsi + 3]
    mov edx, r9d
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    and eax, 0x07
    shl eax, 18
    and ecx, 0x3F
    shl ecx, 12
    and r8d, 0x3F
    shl r8d, 6
    and r9d, 0x3F
    or eax, ecx
    or eax, r8d
    or eax, r9d
    sub eax, 0x10000
    mov ecx, eax
    shr eax, 10
    add eax, 0xD800
    mov word [rdi], ax
    and ecx, 0x03FF
    add ecx, 0xDC00
    mov word [rdi + 2], cx
    add rdi, 4
    add rsi, 4
    jmp .dec_lp

.raw_byte:
    movzx eax, byte [rsi]
.ascii:
    mov word [rdi], ax
    add rdi, 2
    inc rsi
    jmp .dec_lp

.done:
    mov word [rdi], 0
    mov rdx, rdi
    sub rdx, rbx
    shr rdx, 1
    mov rax, rbx
    clc
    @epilog_ret

.oom:
    stc
    @epilog_ret
@endproc

;; @proc utf16le_to_wtf8_buf64
;; @abi win64_ms
;; @in RCX = UTF-16LE string, RDX = WCHAR count (or -1 if NUL-terminated), R8 = output buffer, R9 = max bytes (incl. NUL)
;; @out Writes NUL-terminated WTF-8 string to R8, RAX = byte length (excl. NUL)
;; @err none (truncates cleanly before R9 limit)
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Transcodes UTF-16LE (combining valid surrogate pairs into 4B UTF-8, preserving lone surrogates as 3B WTF-8).
@proc_frame utf16le_to_wtf8_buf64, 32, rbx, rsi, rdi
    mov rsi, rcx
    mov rdi, r8
    mov r11, r8
    lea rbx, [r8 + r9 - 1]
    cmp rdx, -1
    jne .have_cnt
    call wcs_len64
    mov rdx, rax
.have_cnt:
    mov rcx, rdx

.u16_lp:
    test rcx, rcx
    jz .finish
    movzx eax, word [rsi]
    add rsi, 2
    dec rcx

    cmp eax, 0x80
    jae .non_ascii
    cmp rdi, rbx
    jae .finish
    mov byte [rdi], al
    inc rdi
    jmp .u16_lp

.non_ascii:
    cmp eax, 0x0800
    jae .chk_surr
    lea r10, [rdi + 2]
    cmp r10, rbx
    ja .finish
    mov edx, eax
    shr eax, 6
    or al, 0xC0
    mov byte [rdi], al
    and dl, 0x3F
    or dl, 0x80
    mov byte [rdi + 1], dl
    add rdi, 2
    jmp .u16_lp

.chk_surr:
    mov edx, eax
    and edx, 0xFC00
    cmp edx, 0xD800
    jne .emit_3b
    test rcx, rcx
    jz .emit_3b
    movzx edx, word [rsi]
    mov r10d, edx
    and r10d, 0xFC00
    cmp r10d, 0xDC00
    jne .emit_3b

    lea r10, [rdi + 4]
    cmp r10, rbx
    ja .finish
    add rsi, 2
    dec rcx
    sub eax, 0xD800
    shl eax, 10
    sub edx, 0xDC00
    add eax, edx
    add eax, 0x10000

    mov edx, eax
    shr edx, 18
    or dl, 0xF0
    mov byte [rdi], dl
    mov edx, eax
    shr edx, 12
    and dl, 0x3F
    or dl, 0x80
    mov byte [rdi + 1], dl
    mov edx, eax
    shr edx, 6
    and dl, 0x3F
    or dl, 0x80
    mov byte [rdi + 2], dl
    and al, 0x3F
    or al, 0x80
    mov byte [rdi + 3], al
    add rdi, 4
    jmp .u16_lp

.emit_3b:
    lea r10, [rdi + 3]
    cmp r10, rbx
    ja .finish
    mov edx, eax
    shr edx, 12
    or dl, 0xE0
    mov byte [rdi], dl
    mov edx, eax
    shr edx, 6
    and dl, 0x3F
    or dl, 0x80
    mov byte [rdi + 1], dl
    and al, 0x3F
    or al, 0x80
    mov byte [rdi + 2], al
    add rdi, 3
    jmp .u16_lp

.finish:
    mov byte [rdi], 0
    mov rax, rdi
    sub rax, r11
    @epilog_ret
@endproc

;; @proc utf16le_to_wtf8_arena64
;; @abi win64_ms
;; @in RCX = NUL-terminated UTF-16LE string
;; @out RAX = NUL-terminated WTF-8 string in statement arena, RDX = byte length, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Allocates worst-case 3*WCHAR+8 bytes in statement arena and transcodes UTF-16LE to WTF-8.
@proc_frame utf16le_to_wtf8_arena64, 32, rbx, rsi, rdi
    mov rsi, rcx
    call wcs_len64
    mov rbx, rax
    lea rcx, [rbx + rbx * 2 + 8]
    call arena_alloc64
    jc .oom
    mov rdi, rax
    mov rcx, rsi
    mov rdx, rbx
    mov r8, rdi
    lea r9, [rbx + rbx * 2 + 4]
    call utf16le_to_wtf8_buf64
    mov rdx, rax
    mov rax, rdi
    clc
    @epilog_ret
.oom:
    stc
    @epilog_ret
@endproc

;; @proc map_win32_to_err64
;; @abi win64_ms
;; @in EAX = GetLastError() Win32 error code
;; @out EAX = unified canonical ERR_* code (1..511), CF=1
;; @err Always sets CF=1 with canonical ERR_* in EAX
;; @clobbers rax
;; @desc Maps Win32/Win64 GetLastError() values deterministically onto the shared canonical ERR_* enum.
@proc map_win32_to_err64
    test eax, eax
    jnz .chk_range
    mov eax, ERR_GEN_FAILURE
    stc
    ret
.chk_range:
    cmp eax, 80
    je .exists
    cmp eax, 183
    je .exists
    cmp eax, 145
    je .not_empty
    cmp eax, 206
    je .path_nf
    cmp eax, 267
    je .path_nf
    cmp eax, 1314
    je .access
    cmp eax, 0x58
    jbe .direct
    mov eax, ERR_GEN_FAILURE
    stc
    ret
.exists:
    mov eax, ERR_FILE_EXISTS
    stc
    ret
.not_empty:
    mov eax, ERR_ACCESS_DENIED
    stc
    ret
.path_nf:
    mov eax, ERR_PATH_NOT_FOUND
    stc
    ret
.access:
    mov eax, ERR_ACCESS_DENIED
    stc
    ret
.direct:
    stc
    ret
@endproc

;; @proc err_fallback_str64
;; @abi win64_ms
;; @in ECX = canonical ERR_* code
;; @out RAX = 64-bit pointer to built-in English message string
;; @err none
;; @clobbers rax
;; @desc Returns built-in fallback error text for canonical ERR_* codes.
@proc err_fallback_str64
    cmp ecx, ERR_FILE_NOT_FOUND
    je .fnf
    cmp ecx, ERR_PATH_NOT_FOUND
    je .pnf
    cmp ecx, ERR_ACCESS_DENIED
    je .acc
    cmp ecx, ERR_FILE_EXISTS
    je .fex
    cmp ecx, ERR_OUT_OF_MEMORY
    je .oom
    cmp ecx, ERR_SYNTAX
    je .syn
    cmp ecx, ERR_DIV_BY_ZERO
    je .dbz
    cmp ecx, ERR_INVALID_NUMBER
    je .num
    cmp ecx, ERR_BAD_COMMAND
    je .badc
    cmp ecx, ERR_LABEL_NOT_FOUND
    je .lbl
    lea rax, [rel szErrGen64]
    ret
.fnf:
    lea rax, [rel szErrFnf64]
    ret
.pnf:
    lea rax, [rel szErrPnf64]
    ret
.acc:
    lea rax, [rel szErrAcc64]
    ret
.fex:
    lea rax, [rel szErrExists64]
    ret
.oom:
    lea rax, [rel szErrOom64]
    ret
.syn:
    lea rax, [rel szErrSyntax64]
    ret
.dbz:
    lea rax, [rel szErrDivZero64]
    ret
.num:
    lea rax, [rel szErrInvNum64]
    ret
.badc:
    lea rax, [rel szErrBadCmd64]
    ret
.lbl:
    lea rax, [rel szErrBadLbl64]
    ret
@endproc
