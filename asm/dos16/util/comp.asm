;; @file comp.asm
;; @isa 8086
;; @desc Standalone COMP.COM file comparison utility (/D, /A, /L, /N=num, /C file1 file2).

@cpu 8086
@format com
@entry comp_entry

@include "../../inc/dos_equ.inc"

;; @proc comp_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=switches (/D, /A, /L, /C) and file1 file2
;; @out Prints "Files compare OK" (exit 0) or "Compare error at OFFSET xxxx" (exit 1)
;; @err Exits with 2 on missing files
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for COMP.COM comparing two files strictly by length and byte/case-folded content.
@proc comp_entry
    call u_init
    mov byte [U_ARG_A], 0
    mov byte [U_ARG_B], 0

.arg_lp:
    call u_skip_ws
    test al, al
    jz .args_done
    cmp al, '/'
    jne .pos_arg
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'C'
    jne .arg_lp
    mov byte [bCompNocase], 1
    jmp .arg_lp

.pos_arg:
    cmp byte [U_ARG_A], 0
    jne .arg2
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp
.arg2:
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szCompHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.args_done:
    cmp byte [U_ARG_B], 0
    jne .load_both
    mov al, 2
    call u_exit
    ret

.load_both:
    mov dx, U_ARG_A
    mov di, U_IO_A
    mov cx, 15000
    call u_load_stream
    jnc .ok_a
    jmp .err2
.ok_a:
    mov word [wCompLenA], ax

    mov dx, U_ARG_B
    mov di, U_IO_B
    mov cx, 15000
    call u_load_stream
    jnc .ok_b
    jmp .err2
.ok_b:
    cmp ax, word [wCompLenA]
    jne .diff_size

    mov cx, ax
    xor bx, bx
    mov word [wCompErrs], 0
.lp:
    cmp bx, cx
    jae .done_cmp
    mov al, byte [U_IO_A + bx]
    mov ah, byte [U_IO_B + bx]
    cmp byte [bCompNocase], 0
    je .raw
    push cx
    call u_chr_upper
    mov dl, al
    mov al, ah
    call u_chr_upper
    cmp dl, al
    pop cx
    je .nx
    jmp .bad_off
.raw:
    cmp al, ah
    je .nx
.bad_off:
    inc word [wCompErrs]
    push bx
    push cx
    mov si, addr(szCompErrAt)
    call u_write_str
    pop cx
    pop bx
    push bx
    push cx
    mov ax, bx
    call u_print_hex16
    call u_write_crlf
    pop cx
    pop bx
.nx:
    inc bx
    jmp .lp

.done_cmp:
    cmp word [wCompErrs], 0
    jne .err1
    mov si, addr(szCompOk)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.diff_size:
    mov si, addr(szCompDiffSz)
    call u_write_str
    call u_write_crlf
.err1:
    mov al, 1
    call u_exit
    ret
.err2:
    mov al, 2
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bCompNocase, 0
@data dw wCompLenA, 0
@data dw wCompErrs, 0
@data db szCompHelp, "COMP [/D] [/A] [/L] [/N=num] [/C] file1 file2", 0
@data db szCompOk, "Files compare OK", 0
@data db szCompErrAt, "Compare error at OFFSET ", 0
@data db szCompDiffSz, "Files are different sizes", 0
