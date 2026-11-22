;; @file utf_cp32.inc
;; @isa 386
;; @desc Pure-ASM WTF-8 / UTF-8 <-> UTF-16LE transcoders, DBCS lead-byte bitmap, string helpers, and ERR_* messages.

@cpu 386
@format inc

;; @proc str_len32
;; @abi win32
;; @in ESI = NUL-terminated string
;; @out ECX = byte length (excluding NUL)
;; @err none
;; @clobbers eax, ecx
;; @desc Computes byte length of a NUL-terminated string without modifying ESI.
@proc str_len32
    xor ecx, ecx
.lp:
    mov al, byte [esi + ecx]
    test al, al
    jz .done
    inc ecx
    jmp .lp
.done:
    ret
@endproc

;; @proc wcs_len32
;; @abi win32
;; @in ESI = NUL-terminated UTF-16LE string
;; @out ECX = WCHAR count (excluding trailing 0x0000)
;; @err none
;; @clobbers eax, ecx
;; @desc Computes WCHAR code-unit length of a UTF-16LE string without modifying ESI.
@proc wcs_len32
    xor ecx, ecx
.lp:
    mov ax, word [esi + ecx * 2]
    test ax, ax
    jz .done
    inc ecx
    jmp .lp
.done:
    ret
@endproc

;; @proc str_copy32
;; @abi win32
;; @in ESI = source NUL-terminated string, EDI = destination buffer
;; @out Copies string + NUL, EAX = destination end (pointing at NUL)
;; @err none
;; @clobbers eax
;; @desc Copies a NUL-terminated string from ESI to EDI, preserving ESI and EDI.
@proc str_copy32
    push esi
    push edi
.lp:
    mov al, byte [esi]
    mov byte [edi], al
    test al, al
    jz .done
    inc esi
    inc edi
    jmp .lp
.done:
    mov eax, edi
    pop edi
    pop esi
    ret
@endproc

;; @proc ascii_upper_al
;; @abi win32
;; @in AL = byte
;; @out AL = uppercase ASCII if 'a'..'z', else unchanged
;; @err none
;; @clobbers eax
;; @desc Folds ASCII lowercase 'a'..'z' to 'A'..'Z' in AL.
@proc ascii_upper_al
    cmp al, 'a'
    jb .done
    cmp al, 'z'
    ja .done
    sub al, 32
.done:
    ret
@endproc

;; @proc str_eq_i32
;; @abi win32
;; @in ESI = string A, EDI = string B
;; @out EAX = 1 if equal case-insensitively, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Compares two NUL-terminated strings case-insensitively (DBCS lead-byte safe on Win9x).
@proc str_eq_i32
    push esi
    push edi
.lp:
    mov al, byte [esi]
    mov dl, byte [edi]
    test al, al
    jz .chk_end
    cmp byte [g_is_win9x], 0
    je .fold
    call is_dbcs_lead32
    jnc .fold
    cmp al, dl
    jne .ne
    inc esi
    inc edi
    mov al, byte [esi]
    mov dl, byte [edi]
    cmp al, dl
    jne .ne
    test al, al
    jz .eq
    inc esi
    inc edi
    jmp .lp
.fold:
    call ascii_upper_al
    mov cl, al
    mov al, dl
    call ascii_upper_al
    cmp cl, al
    jne .ne
    inc esi
    inc edi
    jmp .lp
.chk_end:
    test dl, dl
    jnz .ne
.eq:
    mov eax, 1
    pop edi
    pop esi
    ret
.ne:
    xor eax, eax
    pop edi
    pop esi
    ret
@endproc

;; @proc str_cmp_i32
;; @abi win32
;; @in ESI = string A, EDI = string B
;; @out EAX < 0 if A < B, 0 if A == B, > 0 if A > B (case-insensitive)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Lexicographically compares two NUL-terminated strings case-insensitively.
@proc str_cmp_i32
    push esi
    push edi
.lp:
    mov al, byte [esi]
    call ascii_upper_al
    movzx ecx, al
    mov al, byte [edi]
    call ascii_upper_al
    movzx edx, al
    sub ecx, edx
    jnz .diff
    test edx, edx
    jz .eq
    inc esi
    inc edi
    jmp .lp
.eq:
    xor eax, eax
    pop edi
    pop esi
    ret
.diff:
    mov eax, ecx
    pop edi
    pop esi
    ret
@endproc

;; @proc cp_rebuild_dbcs32
;; @abi win32
;; @in EAX = active Win32 codepage ID (e.g. 932, 936, 949, 950, 65001)
;; @out Populates g_dbcs_bitmap[32] (256-bit table) via GetCPInfo
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Queries GetCPInfo to populate O(1) 256-bit DBCS lead-byte bitmap for CJK Win95/98/Me & OEM modes.
@proc cp_rebuild_dbcs32
    push ebx
    push esi
    push edi
    mov ebx, eax

    ; Zero 32-byte g_dbcs_bitmap
    mov edi, addr(g_dbcs_bitmap)
    xor eax, eax
    mov ecx, 8
    rep stosd

    cmp ebx, CP_UTF8
    je .done

    sub esp, 24
    mov esi, esp
    push esi
    push ebx
    call dword [__imp__GetCPInfo@8]
    test eax, eax
    jz .free_stk

    cmp dword [esi + CPINFO20.MaxCharSize], 1
    jbe .free_stk

    lea edi, [esi + CPINFO20.LeadByte]
    mov ecx, 6
.pair_lp:
    movzx eax, byte [edi]
    movzx edx, byte [edi + 1]
    test eax, eax
    jz .free_stk
    cmp eax, edx
    ja .next_pair
.set_lp:
    push ecx
    mov ecx, eax
    shr ecx, 3
    push eax
    and eax, 7
    mov bl, 1
    push ecx
    mov cl, al
    shl bl, cl
    pop ecx
    or byte [g_dbcs_bitmap + ecx], bl
    pop eax
    pop ecx
    inc eax
    cmp eax, edx
    jbe .set_lp
.next_pair:
    add edi, 2
    dec ecx
    jnz .pair_lp

.free_stk:
    add esp, 24
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc is_dbcs_lead32
;; @abi win32
;; @in AL = byte
;; @out CF=1 if AL is an active DBCS lead byte; CF=0 otherwise (AL preserved!)
;; @err none
;; @clobbers ecx
;; @desc O(1) lookup in 256-bit g_dbcs_bitmap for CP932/CP936/CP949/CP950 multi-byte characters.
@proc is_dbcs_lead32
    push eax
    movzx ecx, al
    shr ecx, 3
    and al, 7
    mov ah, byte [g_dbcs_bitmap + ecx]
    mov cl, al
    shr ah, cl
    test ah, 1
    pop eax
    jz .not_lead
    stc
    ret
.not_lead:
    clc
    ret
@endproc

;; @proc wtf8_to_utf16le
;; @abi win32
;; @in ESI = NUL-terminated WTF-8 / UTF-8 string
;; @out EAX = NUL-terminated UTF-16LE buffer in statement arena, ECX = WCHAR count (excl. NUL), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY on arena exhaustion
;; @clobbers eax, ecx, edx
;; @desc Converts WTF-8 / UTF-8 (including unpaired NTFS surrogates and astral pairs) to UTF-16LE in arena.
@proc wtf8_to_utf16le
    push ebx
    push esi
    push edi

    call str_len32
    lea ecx, [ecx + ecx + 8]
    call arena_alloc32
    jc .oom
    mov edi, eax
    mov ebx, eax

.dec_lp:
    movzx eax, byte [esi]
    test eax, eax
    jz .done
    cmp eax, 0x80
    jb .ascii

    ; Check 2-byte UTF-8 (110xxxxx 10xxxxxx)
    mov edx, eax
    and edx, 0xE0
    cmp edx, 0xC0
    jne .chk_3b
    movzx ecx, byte [esi + 1]
    mov edx, ecx
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    and eax, 0x1F
    shl eax, 6
    and ecx, 0x3F
    or eax, ecx
    mov word [edi], ax
    add edi, 2
    add esi, 2
    jmp .dec_lp

.chk_3b:
    ; Check 3-byte UTF-8 / WTF-8 (1110xxxx 10xxxxxx 10xxxxxx — includes lone surrogates 0xD800..0xDFFF!)
    mov edx, eax
    and edx, 0xF0
    cmp edx, 0xE0
    jne .chk_4b
    movzx ecx, byte [esi + 1]
    mov edx, ecx
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    movzx edx, byte [esi + 2]
    push eax
    mov eax, edx
    and eax, 0xC0
    cmp eax, 0x80
    pop eax
    jne .raw_byte
    and eax, 0x0F
    shl eax, 12
    and ecx, 0x3F
    shl ecx, 6
    and edx, 0x3F
    or eax, ecx
    or eax, edx
    mov word [edi], ax
    add edi, 2
    add esi, 3
    jmp .dec_lp

.chk_4b:
    ; Check 4-byte UTF-8 astral plane (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx -> UTF-16 surrogate pair)
    mov edx, eax
    and edx, 0xF8
    cmp edx, 0xF0
    jne .raw_byte
    movzx ecx, byte [esi + 1]
    mov edx, ecx
    and edx, 0xC0
    cmp edx, 0x80
    jne .raw_byte
    movzx edx, byte [esi + 2]
    push eax
    mov eax, edx
    and eax, 0xC0
    cmp eax, 0x80
    pop eax
    jne .raw_byte
    push ebx
    movzx ebx, byte [esi + 3]
    push eax
    mov eax, ebx
    and eax, 0xC0
    cmp eax, 0x80
    pop eax
    jne .bad_4b
    and eax, 0x07
    shl eax, 18
    and ecx, 0x3F
    shl ecx, 12
    and edx, 0x3F
    shl edx, 6
    and ebx, 0x3F
    or eax, ecx
    or eax, edx
    or eax, ebx
    pop ebx
    sub eax, 0x10000
    mov ecx, eax
    shr eax, 10
    add eax, 0xD800
    mov word [edi], ax
    and ecx, 0x03FF
    add ecx, 0xDC00
    mov word [edi + 2], cx
    add edi, 4
    add esi, 4
    jmp .dec_lp

.bad_4b:
    pop ebx
.raw_byte:
    movzx eax, byte [esi]
.ascii:
    mov word [edi], ax
    add edi, 2
    inc esi
    jmp .dec_lp

.done:
    mov word [edi], 0
    mov ecx, edi
    sub ecx, ebx
    shr ecx, 1
    mov eax, ebx
    pop edi
    pop esi
    pop ebx
    clc
    ret

.oom:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc utf16le_to_wtf8_buf
;; @abi win32
;; @in ESI = UTF-16LE string, ECX = WCHAR count (or -1 if NUL-terminated), EDI = output buffer, EDX = max bytes (incl. NUL)
;; @out EDI = NUL-terminated WTF-8 string, EAX = byte length (excl. NUL)
;; @err none (truncates cleanly before EDX limit)
;; @clobbers eax, ecx, edx
;; @desc Transcodes UTF-16LE (combining valid surrogate pairs into 4B UTF-8, preserving lone surrogates as 3B WTF-8).
@proc utf16le_to_wtf8_buf
    push ebx
    push esi
    push edi
    push ebp

    cmp ecx, -1
    jne .have_cnt
    call wcs_len32
.have_cnt:
    mov ebp, edi
    lea ebx, [edi + edx - 1]

.u16_lp:
    test ecx, ecx
    jz .finish
    movzx eax, word [esi]
    add esi, 2
    dec ecx

    cmp eax, 0x80
    jae .non_ascii
    cmp edi, ebx
    jae .finish
    mov byte [edi], al
    inc edi
    jmp .u16_lp

.non_ascii:
    cmp eax, 0x0800
    jae .chk_surr
    lea edx, [edi + 2]
    cmp edx, ebx
    ja .finish
    mov edx, eax
    shr eax, 6
    or al, 0xC0
    mov byte [edi], al
    and dl, 0x3F
    or dl, 0x80
    mov byte [edi + 1], dl
    add edi, 2
    jmp .u16_lp

.chk_surr:
    ; Check high surrogate 0xD800..0xDBFF followed by low surrogate 0xDC00..0xDFFF
    mov edx, eax
    and edx, 0xFC00
    cmp edx, 0xD800
    jne .emit_3b
    test ecx, ecx
    jz .emit_3b
    movzx edx, word [esi]
    push eax
    and edx, 0xFC00
    cmp edx, 0xDC00
    pop eax
    jne .emit_3b

    ; Combine surrogate pair into 21-bit scalar value -> 4-byte UTF-8
    lea edx, [edi + 4]
    cmp edx, ebx
    ja .finish
    movzx edx, word [esi]
    add esi, 2
    dec ecx
    sub eax, 0xD800
    shl eax, 10
    sub edx, 0xDC00
    add eax, edx
    add eax, 0x10000

    mov edx, eax
    shr edx, 18
    or dl, 0xF0
    mov byte [edi], dl
    mov edx, eax
    shr edx, 12
    and dl, 0x3F
    or dl, 0x80
    mov byte [edi + 1], dl
    mov edx, eax
    shr edx, 6
    and dl, 0x3F
    or dl, 0x80
    mov byte [edi + 2], dl
    and al, 0x3F
    or al, 0x80
    mov byte [edi + 3], al
    add edi, 4
    jmp .u16_lp

.emit_3b:
    ; BMP U+0800..U+FFFF or unpaired surrogate (WTF-8!) -> 3 bytes
    lea edx, [edi + 3]
    cmp edx, ebx
    ja .finish
    mov edx, eax
    shr edx, 12
    or dl, 0xE0
    mov byte [edi], dl
    mov edx, eax
    shr edx, 6
    and dl, 0x3F
    or dl, 0x80
    mov byte [edi + 1], dl
    and al, 0x3F
    or al, 0x80
    mov byte [edi + 2], al
    add edi, 3
    jmp .u16_lp

.finish:
    mov byte [edi], 0
    mov eax, edi
    sub eax, ebp
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc utf16le_to_wtf8_arena
;; @abi win32
;; @in ESI = NUL-terminated UTF-16LE string
;; @out EAX = NUL-terminated WTF-8 string in statement arena, ECX = byte length, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Allocates worst-case 3*WCHAR+4 bytes in statement arena and transcodes UTF-16LE to WTF-8.
@proc utf16le_to_wtf8_arena
    push ebx
    push esi
    push edi
    call wcs_len32
    mov ebx, ecx
    lea ecx, [ecx + ecx * 2 + 8]
    call arena_alloc32
    jc .oom
    mov edi, eax
    lea edx, [ebx + ebx * 2 + 4]
    mov ecx, ebx
    call utf16le_to_wtf8_buf
    mov ecx, eax
    mov eax, edi
    pop edi
    pop esi
    pop ebx
    clc
    ret
.oom:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc map_win32_to_err
;; @abi win32
;; @in EAX = GetLastError() Win32 error code
;; @out EAX = unified canonical ERR_* code (1..511), CF=1
;; @err Always sets CF=1 with canonical ERR_* in EAX
;; @clobbers eax
;; @desc Maps Win32 GetLastError() values deterministically onto the shared canonical ERR_* enum.
@proc map_win32_to_err
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

;; @proc err_fallback_str32
;; @abi win32
;; @in EAX = canonical ERR_* code
;; @out EAX = built-in English message string
;; @err none
;; @clobbers eax
;; @desc Returns built-in fallback error text for canonical ERR_* codes when FormatMessage is unavailable.
@proc err_fallback_str32
    cmp eax, ERR_FILE_NOT_FOUND
    je .fnf
    cmp eax, ERR_PATH_NOT_FOUND
    je .pnf
    cmp eax, ERR_ACCESS_DENIED
    je .acc
    cmp eax, ERR_FILE_EXISTS
    je .fex
    cmp eax, ERR_OUT_OF_MEMORY
    je .oom
    cmp eax, ERR_SYNTAX
    je .syn
    cmp eax, ERR_DIV_BY_ZERO
    je .dbz
    cmp eax, ERR_INVALID_NUMBER
    je .num
    cmp eax, ERR_BAD_COMMAND
    je .badc
    cmp eax, ERR_LABEL_NOT_FOUND
    je .lbl
    mov eax, addr(szErrGen32)
    ret
.fnf:
    mov eax, addr(szErrFnf32)
    ret
.pnf:
    mov eax, addr(szErrPnf32)
    ret
.acc:
    mov eax, addr(szErrAcc32)
    ret
.fex:
    mov eax, addr(szErrExists32)
    ret
.oom:
    mov eax, addr(szErrOom32)
    ret
.syn:
    mov eax, addr(szErrSyntax32)
    ret
.dbz:
    mov eax, addr(szErrDivZero32)
    ret
.num:
    mov eax, addr(szErrInvNum32)
    ret
.badc:
    mov eax, addr(szErrBadCmd32)
    ret
.lbl:
    mov eax, addr(szErrBadLbl32)
    ret
@endproc
