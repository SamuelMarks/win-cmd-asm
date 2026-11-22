;; @file attrib.asm
;; @isa 8086
;; @desc Standalone ATTRIB.COM utility ([+|-][R|A|S|H] [path] [/S] [/D]).

@cpu 8086
@format com
@entry attrib_entry

@include "../../inc/dos_equ.inc"

;; @proc attrib_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=CLI flags (+R, -R, +A, -A, +S, -S, +H, -H) and path
;; @out Queries or modifies FAT file attributes via INT 21h AX=4300h/4301h
;; @err Exits with 1 on missing file or unsupported DOS 1.25 modify
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for ATTRIB.COM supporting query, set (+), and clear (-) of R/A/S/H attributes.
@proc attrib_entry
    call u_init
    mov word [wSetMask], 0
    mov word [wClrMask], 0
    mov byte [U_ARG_A], 0

.arg_lp:
    mov di, U_ARG_B
    call u_read_tok
    jc .args_done
    mov al, byte [U_ARG_B]
    cmp al, '/'
    jne .chk_pm
    cmp byte [U_ARG_B + 1], '?'
    jne .arg_lp
    mov si, addr(szAttrHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.chk_pm:
    cmp al, '+'
    je .do_plus
    cmp al, '-'
    je .do_minus
    ; Copy target filename into U_ARG_A
    push si
    mov si, U_ARG_B
    mov di, U_ARG_A
.cp_fn:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_fn
    pop si
    jmp .arg_lp

.do_plus:
    mov al, byte [U_ARG_B + 1]
    call attr_flag_bit
    or word [wSetMask], cx
    jmp .arg_lp

.do_minus:
    mov al, byte [U_ARG_B + 1]
    call attr_flag_bit
    or word [wClrMask], cx
    jmp .arg_lp

.args_done:
    cmp byte [bUDosMajor], 2
    jae .dos200_attr
    mov si, addr(szAttrUnsup)
    call u_write_str
    call u_write_crlf
    mov al, 1
    call u_exit
    ret

.dos200_attr:
    cmp byte [U_ARG_A], 0
    jne .have_target
    ; Default to "*.*" enumeration when no path given
    mov dx, U_DTA_WORK
    mov ah, 0x1A
    int 0x21
    mov dx, addr(szWildAll)
    mov cx, 0x0027
    mov ah, 0x4E
    int 0x21
    jc .not_found
.enum_lp:
    mov cl, byte [U_DTA_WORK + 21]
    xor ch, ch
    mov si, (U_DTA_WORK + 30)
    call attr_print_line
    mov ah, 0x4F
    int 0x21
    jnc .enum_lp
    xor al, al
    call u_exit
    ret

.have_target:
    mov dx, U_ARG_A
    mov ax, 0x4300
    int 0x21
    jc .not_found

    mov ax, word [wSetMask]
    or ax, word [wClrMask]
    jz .show_one

    mov ax, word [wClrMask]
    not ax
    and cx, ax
    or cx, word [wSetMask]
    mov dx, U_ARG_A
    mov ax, 0x4301
    int 0x21
    jc .not_found
    xor al, al
    call u_exit
    ret

.show_one:
    mov si, U_ARG_A
    call attr_print_line
    xor al, al
    call u_exit
    ret

.not_found:
    mov si, addr(szAttrNf)
    call u_write_str
    call u_write_crlf
    mov al, 1
    call u_exit
    ret
@endproc

;; @proc attr_flag_bit
;; @abi dos16
;; @in AL = attribute letter (R, A, S, H)
;; @out CX = DOS attribute bitmask (0 if unrecognized)
;; @err none
;; @clobbers ax, cx
;; @desc Maps R/A/S/H letter to FAT attribute bitmask.
@proc attr_flag_bit
    call u_chr_upper
    xor cx, cx
    cmp al, 'R'
    jne .chk_a
    mov cx, 0x0001
    ret
.chk_a:
    cmp al, 'A'
    jne .chk_s
    mov cx, 0x0020
    ret
.chk_s:
    cmp al, 'S'
    jne .chk_h
    mov cx, 0x0004
    ret
.chk_h:
    cmp al, 'H'
    jne .dn
    mov cx, 0x0002
.dn:
    ret
@endproc

;; @proc attr_print_line
;; @abi dos16
;; @in CX = attribute bits, DS:SI = filename ASCIIZ
;; @out Prints "A  SH R  filename" formatted line
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Renders authentic ATTRIB column layout ("A  SH R  <path>").
@proc attr_print_line
    push si
    mov di, U_NUM_BUF
    mov cx, cx
    mov al, ' '
    test cl, 0x20
    jz .put_a
    mov al, 'A'
.put_a:
    mov byte [di], al
    mov byte [di + 1], ' '
    mov byte [di + 2], ' '
    mov al, ' '
    test cl, 0x04
    jz .put_s
    mov al, 'S'
.put_s:
    mov byte [di + 3], al
    mov al, ' '
    test cl, 0x02
    jz .put_h
    mov al, 'H'
.put_h:
    mov byte [di + 4], al
    mov byte [di + 5], ' '
    mov al, ' '
    test cl, 0x01
    jz .put_r
    mov al, 'R'
.put_r:
    mov byte [di + 6], al
    mov byte [di + 7], ' '
    mov byte [di + 8], ' '
    mov byte [di + 9], 0
    mov si, U_NUM_BUF
    call u_write_str
    pop si
    call u_write_str
    call u_write_crlf
    ret
@endproc

@include "u_common.inc"

@data dw wSetMask, 0
@data dw wClrMask, 0
@data db szWildAll, "*.*", 0
@data db szAttrHelp, "ATTRIB [+R|-R] [+A|-A] [+S|-S] [+H|-H] [file]", 0
@data db szAttrNf, "File not found", 0
@data db szAttrUnsup, "ATTRIB modify requires DOS 2.00+", 0
