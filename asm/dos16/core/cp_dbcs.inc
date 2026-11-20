;; @file cp_dbcs.inc
;; @isa 8086
;; @desc MS-DOS 4.00+ NLS codepage (AX=6601h/6602h) and DBCS lead-byte bitmap (AX=6507h) engine.

@cpu 8086
@format inc

;; @proc nls_get_cp
;; @abi dos16
;; @in [bTrnDosMajor]
;; @out BX = active codepage (437 fallback on DOS < 4.00), DX = system codepage, CF=0 on success
;; @err CF=1, AX=ERR_* if DOS 4.00+ AX=6601h fails
;; @clobbers ax, bx, dx
;; @desc Queries active global codepage via INT 21h AX=6601h on MS-DOS 4.00+.
@proc nls_get_cp
    cmp byte [bTrnDosMajor], 4
    jb .fallback_437
    mov ax, 0x6601
    int 0x21
    jc .err
    clc
    ret
.fallback_437:
    mov bx, 437
    mov dx, 437
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc nls_refresh_dbcs
;; @abi dos16
;; @in [bTrnDosMajor]
;; @out Populates 32-byte bmDbcsLead bitmap via INT 21h AX=6507h on MS-DOS 4.00+
;; @err Clears bmDbcsLead to all-zero (SBCS) if unsupported or no DBCS ranges active
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Queries DOS 4.00+ DBCS lead-byte vector and sets bits in bmDbcsLead[0..31].
@proc nls_refresh_dbcs
    push ds
    push es
    mov ax, cs
    mov ds, ax
    mov es, ax

    ; Clear 32-byte (256-bit) bmDbcsLead bitmap
    mov di, bmDbcsLead
    mov cx, 16
    xor ax, ax
    rep stosw

    cmp byte [bTrnDosMajor], 4
    jb .done

    ; Buffer at [wArenaBase] holds 5-byte info block (0x07 + far ptr to DBCS table)
    mov di, word [wArenaBase]
    mov bx, 0xFFFF
    mov dx, 0xFFFF
    mov cx, 5
    mov ax, 0x6507
    int 0x21
    jc .done

    mov si, word [di + 1]
    mov ax, word [di + 3]
    test ax, ax
    jz .done
    mov ds, ax

    ; First word at DS:SI is byte length of table, followed by (start, end) pairs terminated by 0,0
    add si, 2
.pair_lp:
    mov al, byte [si]
    mov ah, byte [si + 1]
    add si, 2
    test al, al
    jz .done
    cmp al, ah
    ja .pair_lp

.range_lp:
    mov bl, al
    xor bh, bh
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov cl, al
    and cl, 7
    mov ch, 1
    shl ch, cl
    or byte [es:bmDbcsLead + bx], ch
    cmp al, ah
    je .pair_lp
    inc al
    jmp .range_lp

.done:
    pop es
    pop ds
    ret
@endproc

;; @proc nls_set_cp
;; @abi dos16
;; @in BX = requested codepage number (e.g. 437, 850, 932, 936)
;; @out Switches active codepage via AX=6602h (verifying NLSFUNC) and refreshes bmDbcsLead
;; @err CF=1, AX=ERR_UNSUPPORTED_ON_ERA on DOS < 4.00 or AX=ERR_* on DOS failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements CHCP nnn switching and immediate DBCS lead-byte table reload.
@proc nls_set_cp
    cmp byte [bTrnDosMajor], 4
    jb .unsup
    push bx
    mov ax, MULTIPLEX_NLSFUNC_CHECK
    int 0x2F
    pop bx
    mov ax, 0x6602
    int 0x21
    jc .fail
    call nls_refresh_dbcs
    clc
    ret
.unsup:
    mov ax, ERR_UNSUPPORTED_ON_ERA
    stc
    ret
.fail:
    stc
    ret
@endproc
