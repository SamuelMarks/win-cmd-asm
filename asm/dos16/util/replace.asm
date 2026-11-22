;; @file replace.asm
;; @isa 8086
;; @desc Standalone REPLACE.COM file updater (src [dst] [/A] [/P] [/R] [/S] [/U]).

@cpu 8086
@format com
@entry replace_entry

@include "../../inc/dos_equ.inc"

;; @proc replace_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=source [target_dir_or_file] [/A] [/R] [/U]
;; @out Updates existing file (or adds missing file under /A); prints summary and exits 0
;; @err Exits with 2/3/11 on missing source or syntax error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for REPLACE.COM supporting in-place update and /A add-missing mode.
@proc replace_entry
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
    cmp al, 'A'
    jne .chk_r
    mov byte [bRepAdd], 1
    jmp .arg_lp
.chk_r:
    cmp al, 'R'
    jne .arg_lp
    mov byte [bRepRo], 1
    jmp .arg_lp

.pos_arg:
    cmp byte [U_ARG_A], 0
    jne .dst_arg
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp
.dst_arg:
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szRepHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.args_done:
    cmp byte [U_ARG_A], 0
    jne .prep_dst
    mov al, 11
    call u_exit
    ret

.prep_dst:
    ; Extract leaf filename of U_ARG_A into U_IO_B
    mov si, U_ARG_A
    mov bx, si
.sc_leaf:
    mov al, byte [si]
    test al, al
    jz .got_leaf
    inc si
    cmp al, 0x5C
    je .set_lf
    cmp al, ':'
    jne .sc_leaf
.set_lf:
    mov bx, si
    jmp .sc_leaf
.got_leaf:
    ; Build target file path in U_ARG_C = (U_ARG_B ? U_ARG_B + '' + leaf : leaf)
    mov di, U_ARG_C
    cmp byte [U_ARG_B], 0
    je .cp_lf
    mov si, U_ARG_B
.cp_d:
    mov al, byte [si]
    test al, al
    jz .sep_d
    mov byte [di], al
    inc si
    inc di
    jmp .cp_d
.sep_d:
    cmp byte [di - 1], 0x5C
    je .cp_lf
    cmp byte [di - 1], ':'
    je .cp_lf
    mov byte [di], 0x5C
    inc di
.cp_lf:
    mov si, bx
.cp_l_ch:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_l_ch

    ; Check whether target U_ARG_C currently exists
    mov dx, U_ARG_C
    mov di, U_IO_B
    mov cx, 4
    call u_load_stream
    mov bl, 0 ; exists = 0
    jc .chk_mode
    mov bl, 1
.chk_mode:
    cmp byte [bRepAdd], 0
    jne .add_mode
    ; Replace mode: must ALREADY exist!
    test bl, bl
    jz .zero_done
    jmp .do_write

.add_mode:
    ; Add mode (/A): must NOT exist yet!
    test bl, bl
    jnz .zero_done

.do_write:
    cmp byte [bRepRo], 0
    je .load_src
    cmp byte [bUDosMajor], 2
    jb .load_src
    mov dx, U_ARG_C
    xor cx, cx
    mov ax, 0x4301
    int 0x21

.load_src:
    mov dx, U_ARG_A
    mov di, U_IO_A
    mov cx, 16000
    call u_load_stream
    jc .err2
    mov cx, ax
    mov dx, U_ARG_C
    mov si, U_IO_A
    call u_save_file
    jc .err2

    mov ax, 1
    call u_print_u16
    mov si, addr(szRepReplaced)
    cmp byte [bRepAdd], 0
    je .prt_sum
    mov si, addr(szRepAdded)
.prt_sum:
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.zero_done:
    xor ax, ax
    call u_print_u16
    mov si, addr(szRepReplaced)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.err2:
    mov al, 2
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bRepAdd, 0
@data db bRepRo, 0
@data db szRepReplaced, " file(s) replaced", 0
@data db szRepAdded, " file(s) added", 0
@data db szRepHelp, "REPLACE source [dest] [/A] [/P] [/R] [/S] [/U]", 0
