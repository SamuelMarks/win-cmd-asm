;; @file glob_dp.inc
;; @isa 8086
;; @desc Phase 14 zero-backtracking O(N*M) bitset wildcard engine (matches_win32_glob & matches_dos_fcb_glob).

@cpu 8086
@format inc

;; @proc glob_set_bit
;; @abi dos16
;; @in SS:BX = 32-byte (256-bit) bitmap, CX = bit index (0..255)
;; @out Sets bit CX in SS:[BX]
;; @err none
;; @clobbers ax, dx, di
;; @desc Sets state bit CX inside a 256-bit stack-allocated dynamic-programming state vector.
@proc glob_set_bit
    cmp cx, 255
    ja .out
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1
    mov dx, cx
    and dl, 7
    mov al, 1
    push cx
    mov cl, dl
    shl al, cl
    pop cx
    or byte [ss:bx + di], al
.out:
    ret
@endproc

;; @proc glob_test_bit
;; @abi dos16
;; @in SS:BX = 32-byte (256-bit) bitmap, CX = bit index (0..255)
;; @out ZF=0 (JNZ) if bit CX is 1; ZF=1 (JZ) if bit CX is 0
;; @err none
;; @clobbers ax, dx, di
;; @desc Tests state bit CX inside a 256-bit stack-allocated dynamic-programming state vector.
@proc glob_test_bit
    cmp cx, 255
    ja .zero
    mov di, cx
    shr di, 1
    shr di, 1
    shr di, 1
    mov dx, cx
    and dl, 7
    mov al, 1
    push cx
    mov cl, dl
    shl al, cl
    pop cx
    test byte [ss:bx + di], al
    ret
.zero:
    xor al, al
    test al, al
    ret
@endproc

;; @proc glob_propagate_eps
;; @abi dos16
;; @in DS:SI = pattern ASCIIZ, DX = pattern length, SS:BX = 32B state bitmap, [bp-70] = cand_pos, [bp-72] = cand_len, [bp-74] = last_dot_idx
;; @out Propagates zero-character transitions ('*', '<' DOS_STAR, '>' DOS_QM at dot/end, '"' DOS_DOT at end)
;; @err none
;; @clobbers ax, cx, di
;; @desc Closes epsilon wildcard transitions left-to-right in O(M) without recursion.
@proc glob_propagate_eps
    xor cx, cx
.eps_lp:
    cmp cx, dx
    jae .eps_dn
    push dx
    call glob_test_bit
    pop dx
    jz .eps_nx

    mov di, si
    add di, cx
    mov al, byte [di]
    cmp al, '*'
    je .set_nx
    cmp al, '<'
    je .set_nx
    cmp al, '"'
    jne .chk_qm_eps
    ; DOS_DOT ('"') matches empty at end-of-name!
    mov ax, word [bp - 70]
    cmp ax, word [bp - 72]
    je .set_nx
    jmp .eps_nx
.chk_qm_eps:
    cmp al, '>'
    jne .eps_nx
    ; DOS_QM ('>') matches empty right before '.' or at end-of-name!
    mov ax, word [bp - 70]
    cmp ax, word [bp - 72]
    je .set_nx
    push si
    mov si, word [bp - 68]
    add si, ax
    cmp byte [si], '.'
    pop si
    jne .eps_nx
.set_nx:
    inc cx
    push dx
    call glob_set_bit
    pop dx
    jmp .eps_lp
.eps_nx:
    inc cx
    jmp .eps_lp
.eps_dn:
    ret
@endproc

;; @proc matches_win32_glob
;; @abi dos16
;; @in DS:SI = wildcard pattern ASCIIZ (*, ?, <, >, "), DS:DI = candidate filename ASCIIZ
;; @out CF=0 if candidate matches pattern; CF=1 if it does not match
;; @err CF=1 on non-match
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Zero-backtracking O(N*M) rolling-bitset DP wildcard matcher immune to *a*a*a*b blowup.
@proc matches_win32_glob
    push bp
    mov bp, sp
    sub sp, 76
    ; [bp-32..bp-1]=cur_bits (32B), [bp-64..bp-33]=nxt_bits (32B)
    ; [bp-66]=pat_ptr, [bp-68]=cand_ptr, [bp-70]=cand_pos, [bp-72]=cand_len, [bp-74]=last_dot_idx, [bp-76]=pat_len
    mov word [bp - 66], si
    mov word [bp - 68], di

    ; Measure pattern length (clamped to 254 states)
    call prs_strlen_si
    cmp cx, 254
    jbe .plen_ok
    mov cx, 254
.plen_ok:
    mov word [bp - 76], cx

    ; Measure candidate length and locate index of final '.' (or 0xFFFF if no '.')
    mov si, di
    xor cx, cx
    mov dx, 0xFFFF
.clen_lp:
    mov al, byte [si]
    test al, al
    jz .clen_dn
    cmp al, '.'
    jne .clen_nx
    mov dx, cx
.clen_nx:
    inc si
    inc cx
    jmp .clen_lp
.clen_dn:
    mov word [bp - 72], cx
    mov word [bp - 74], dx
    mov word [bp - 70], 0

    ; Zero cur_bits [bp-32..bp-1] and set state 0
    push es
    push ss
    pop es
    lea di, [bp - 32]
    mov cx, 16
    xor ax, ax
    rep stosw
    pop es

    lea bx, [bp - 32]
    xor cx, cx
    call glob_set_bit

    mov si, word [bp - 66]
    mov dx, word [bp - 76]
    call glob_propagate_eps

.char_lp:
    mov ax, word [bp - 70]
    cmp ax, word [bp - 72]
    jb .have_cand_ch
    jmp .check_accept
.have_cand_ch:

    ; Zero nxt_bits [bp-64..bp-33]
    push es
    push ss
    pop es
    lea di, [bp - 64]
    mov cx, 16
    xor ax, ax
    rep stosw
    pop es

    ; Load uppercase current candidate char into DH
    mov si, word [bp - 68]
    add si, word [bp - 70]
    mov al, byte [si]
    call chr_to_upper
    mov dh, al

    xor cx, cx
.state_lp:
    cmp cx, word [bp - 76]
    jae .advance_row

    lea bx, [bp - 32]
    push dx
    call glob_test_bit
    pop dx
    jz .next_st

    mov si, word [bp - 66]
    add si, cx
    mov al, byte [si]

    cmp al, '*'
    jne .chk_dstar
    ; '*' stays in state CX on any character!
    lea bx, [bp - 64]
    push dx
    call glob_set_bit
    pop dx
    jmp .next_st

.chk_dstar:
    cmp al, '<'
    jne .chk_qmark
    ; DOS_STAR ('<') consumes any char EXCEPT the final '.'!
    cmp dh, '.'
    jne .stay_dstar
    mov ax, word [bp - 70]
    cmp ax, word [bp - 74]
    je .next_st
.stay_dstar:
    lea bx, [bp - 64]
    push dx
    call glob_set_bit
    pop dx
    jmp .next_st

.chk_qmark:
    cmp al, '?'
    je .match_one

    cmp al, '>'
    jne .chk_ddot
    ; DOS_QM ('>') consumes any single non-dot character
    cmp dh, '.'
    je .next_st
    jmp .match_one

.chk_ddot:
    cmp al, '"'
    jne .chk_lit
    cmp dh, '.'
    je .match_one
    jmp .next_st

.chk_lit:
    call chr_to_upper
    cmp al, dh
    jne .next_st

.match_one:
    inc cx
    lea bx, [bp - 64]
    push dx
    call glob_set_bit
    pop dx
    dec cx

.next_st:
    inc cx
    jmp .state_lp

.advance_row:
    ; Copy nxt_bits -> cur_bits, increment cand_pos, and propagate epsilons!
    push ds
    push es
    push ss
    pop ds
    push ss
    pop es
    lea si, [bp - 64]
    lea di, [bp - 32]
    mov cx, 16
    rep movsw
    pop es
    pop ds

    inc word [bp - 70]
    lea bx, [bp - 32]
    mov si, word [bp - 66]
    mov dx, word [bp - 76]
    call glob_propagate_eps
    jmp .char_lp

.check_accept:
    lea bx, [bp - 32]
    mov cx, word [bp - 76]
    call glob_test_bit
    jz .no_match
    add sp, 76
    pop bp
    clc
    ret
.no_match:
    add sp, 76
    pop bp
    stc
    ret
@endproc

;; @proc fcb_expand_11
;; @abi dos16
;; @in DS:SI = ASCIIZ filename or wildcard, SS:DI = 11-byte output FCB buffer
;; @out Expands stem (8B) and ext (3B) space/question-padded uppercase into SS:DI
;; @err none
;; @clobbers ax, cx, si, di
;; @desc Converts a filename/pattern into classic 11-byte DOS FCB 8+3 format ('*' -> '?').
@proc fcb_expand_11
    push es
    push ss
    pop es
    push di
    mov cx, 11
    mov al, 0x20
    rep stosb
    pop di

    mov cx, 8
.st_lp:
    mov al, byte [si]
    test al, al
    jz .done
    cmp al, '.'
    je .at_dot
    cmp al, '*'
    je .fill_st_q
    call chr_to_upper
    mov byte [es:di], al
    inc si
    inc di
    loop .st_lp
.sk_to_dot:
    mov al, byte [si]
    test al, al
    jz .done
    inc si
    cmp al, '.'
    jne .sk_to_dot
    jmp .do_ext

.fill_st_q:
    mov al, '?'
    rep stosb
    jmp .sk_to_dot

.at_dot:
    inc si
    add di, cx
.do_ext:
    mov cx, 3
.ex_lp:
    mov al, byte [si]
    test al, al
    jz .done
    cmp al, '*'
    je .fill_ex_q
    call chr_to_upper
    mov byte [es:di], al
    inc si
    inc di
    loop .ex_lp
    jmp .done

.fill_ex_q:
    mov al, '?'
    rep stosb

.done:
    pop es
    ret
@endproc

;; @proc matches_dos_fcb_glob
;; @abi dos16
;; @in DS:SI = FCB wildcard pattern ASCIIZ, DS:DI = candidate filename ASCIIZ
;; @out CF=0 if candidate matches 8+3 FCB semantics; CF=1 otherwise
;; @err CF=1 on mismatch
;; @clobbers ax, bx, cx, si, di
;; @desc Classic MS-DOS 8+3 FCB wildcard comparator ('*' fills field with '?', '?' matches space).
@proc matches_dos_fcb_glob
    push bp
    mov bp, sp
    sub sp, 24
    ; [bp-12..bp-2]=pat11, [bp-24..bp-14]=cand11
    push di
    lea di, [bp - 12]
    call fcb_expand_11
    pop si
    lea di, [bp - 24]
    call fcb_expand_11

    xor si, si
    mov cx, 11
.cmp_lp:
    mov al, byte [bp + si - 12]
    cmp al, '?'
    je .nx
    cmp al, byte [bp + si - 24]
    jne .miss
.nx:
    inc si
    loop .cmp_lp
    add sp, 24
    pop bp
    clc
    ret
.miss:
    add sp, 24
    pop bp
    stc
    ret
@endproc
