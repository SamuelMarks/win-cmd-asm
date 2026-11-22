;; @file truename.asm
;; @isa 8086
;; @desc Standalone TRUENAME.COM canonical path resolver across MS-DOS 1.25..8.0.

@cpu 8086
@format com
@entry truename_entry

@include "../../inc/dos_equ.inc"

;; @proc truename_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=optional [drive:][path]
;; @out Prints canonicalized uppercase path via AH=60h (DOS 4.00+) or drive+CWD resolver; exits 0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for TRUENAME.COM supporting MS-DOS 1.25, 2.00, 4.00, and 7.x.
@proc truename_entry
    call u_init
    mov di, U_ARG_A
    call u_read_tok
    jnc .chk_help
    mov byte [U_ARG_A], '.'
    mov byte [U_ARG_A + 1], 0
    jmp .resolve

.chk_help:
    cmp byte [U_ARG_A], '/'
    jne .resolve
    cmp byte [U_ARG_A + 1], '?'
    jne .resolve
    mov si, addr(szTnHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.resolve:
    cmp byte [bUDosMajor], 4
    jb .fallback_norm
    mov si, U_ARG_A
    mov di, U_ARG_B
    mov ah, 0x60
    int 0x21
    jc .fallback_norm
    jmp .print_b

.fallback_norm:
    mov si, U_ARG_A
    mov di, U_ARG_B
    cmp byte [si + 1], ':'
    je .has_drv
    mov ah, 0x19
    int 0x21
    add al, 'A'
    mov byte [di], al
    mov byte [di + 1], ':'
    add di, 2
    jmp .chk_125
.has_drv:
    mov al, byte [si]
    call u_chr_upper
    mov byte [di], al
    mov byte [di + 1], ':'
    add si, 2
    add di, 2

.chk_125:
    cmp byte [bUDosMajor], 2
    jb .cp_rest
    cmp byte [si], 0x5C
    je .skip_ld
    cmp byte [si], '/'
    je .skip_ld
    mov byte [di], 0x5C
    inc di
    push si
    mov si, di
    mov dl, byte [U_ARG_B]
    sub dl, 'A'
    inc dl
    mov ah, 0x47
    int 0x21
.sc_end:
    cmp byte [di], 0
    je .cwd_dn
    inc di
    jmp .sc_end
.cwd_dn:
    pop si
    cmp byte [si], '.'
    jne .chk_rel_add
    cmp byte [si + 1], 0
    je .term_b
.chk_rel_add:
    cmp byte [si], 0
    je .term_b
    cmp byte [di - 1], 0x5C
    je .cp_rest
    mov byte [di], 0x5C
    inc di
    jmp .cp_rest

.skip_ld:
    mov byte [di], 0x5C
    inc di
    inc si

.cp_rest:
    mov al, byte [si]
    test al, al
    jz .term_b
    cmp al, '/'
    jne .up_c
    mov al, 0x5C
    jmp .put_c
.up_c:
    call u_chr_upper
.put_c:
    mov byte [di], al
    inc si
    inc di
    jmp .cp_rest
.term_b:
    mov byte [di], 0

.print_b:
    mov si, U_ARG_B
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db szTnHelp, "TRUENAME [drive:][path][filename]", 0
