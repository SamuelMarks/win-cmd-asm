;; @file xcopy.asm
;; @isa 8086
;; @desc Standalone XCOPY.COM file/tree copier (src dst [/S] [/E] [/Y] [/Q] [/F] [/L] [/I]).

@cpu 8086
@format com
@entry xcopy_boot

@include "../../inc/dos_equ.inc"

;; @proc xcopy_boot
;; @abi dos16
;; @in DS=ES=CS=PSP
;; @out Jumps to xcopy_entry
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc 0100h entrypoint trampoline for XCOPY.COM.
@proc xcopy_boot
    jmp xcopy_entry
@endproc

;; @proc xcopy_one_file
;; @abi dos16
;; @in DS:DX = source path, DS:DI = destination path
;; @out Copies file (unless /L list-only), prints path (unless /Q), increments [wXcCount]
;; @err CF=1 on read/write failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Copies a single file from DS:DX to DS:DI across MS-DOS 1.25..8.0.
@proc xcopy_one_file
    push dx
    push di
    cmp byte [bXcQuiet], 0
    jne .after_echo
    mov si, dx
    call u_write_str
    cmp byte [bXcFull], 0
    je .echo_eol
    mov si, addr(szXcArrow)
    call u_write_str
    pop di
    push di
    mov si, di
    call u_write_str
.echo_eol:
    call u_write_crlf
.after_echo:
    pop di
    pop dx

    cmp byte [bXcListOnly], 0
    jne .cnt_only

    push di
    mov di, U_IO_A
    mov cx, 16000
    call u_load_stream
    pop dx
    jc .fail
    mov cx, ax
    mov si, U_IO_A
    call u_save_file
    jc .fail

.cnt_only:
    inc word [wXcCount]
    clc
    ret
.fail:
    stc
    ret
@endproc

;; @proc xcopy_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=src [dst] [/S] [/E] [/Y] [/Q] [/F] [/L] [/I]
;; @out Copies matching file(s) or directory contents and prints "N File(s) copied"
;; @err Exits with 4 on missing arguments or 1 on missing source
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for XCOPY.COM supporting single-file and wildcard directory copy.
@proc xcopy_entry
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
    cmp al, 'Q'
    jne .chk_f
    mov byte [bXcQuiet], 1
    jmp .arg_lp
.chk_f:
    cmp al, 'F'
    jne .chk_l
    mov byte [bXcFull], 1
    jmp .arg_lp
.chk_l:
    cmp al, 'L'
    jne .chk_i
    mov byte [bXcListOnly], 1
    jmp .arg_lp
.chk_i:
    cmp al, 'I'
    jne .arg_lp
    mov byte [bXcMkdir], 1
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
    mov si, addr(szXcHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.args_done:
    cmp byte [U_ARG_A], 0
    jne .chk_dst
    mov al, 4
    call u_exit
    ret

.chk_dst:
    mov word [wXcCount], 0
    cmp byte [bXcMkdir], 0
    je .chk_wild
    cmp byte [bUDosMajor], 2
    jb .chk_wild
    cmp byte [bXcListOnly], 0
    jne .chk_wild
    mov dx, U_ARG_B
    mov ah, 0x39
    int 0x21

.chk_wild:
    ; Check if U_ARG_A contains '*' or '?' on DOS 2.00+
    cmp byte [bUDosMajor], 2
    jb .single_cp
    mov si, U_ARG_A
.w_lp:
    mov al, byte [si]
    test al, al
    jz .single_cp
    cmp al, '*'
    je .wild_cp
    cmp al, '?'
    je .wild_cp
    inc si
    jmp .w_lp

.single_cp:
    mov dx, U_ARG_A
    mov di, U_ARG_B
    call xcopy_one_file
    jnc .j_sum
    jmp .err_nf
.j_sum:
    jmp .summary

.wild_cp:
    ; Extract source directory prefix up to last '' or ':' into U_IO_B
    mov si, U_ARG_A
    mov bx, si ; last sep + 1
.sc_sep:
    mov al, byte [si]
    test al, al
    jz .got_pfx
    inc si
    cmp al, 0x5C
    je .upd_bx
    cmp al, ':'
    jne .sc_sep
.upd_bx:
    mov bx, si
    jmp .sc_sep
.got_pfx:
    mov si, U_ARG_A
    mov di, U_IO_B
.cp_pfx:
    cmp si, bx
    jae .pfx_dn
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    jmp .cp_pfx
.pfx_dn:
    mov word [wSrcLeafOff], di

    mov dx, U_DTA_WORK
    mov ah, 0x1A
    int 0x21
    mov dx, U_ARG_A
    mov cx, 0x0020
    mov ah, 0x4E
    int 0x21
    jc .summary

.dta_lp:
    ; Build full src in U_IO_B and full dst in U_ARG_C
    mov si, (U_DTA_WORK + 30)
    mov di, word [wSrcLeafOff]
.cp_sl:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_sl

    mov si, U_ARG_B
    mov di, U_ARG_C
.cp_dp:
    mov al, byte [si]
    test al, al
    jz .dp_sep
    mov byte [di], al
    inc si
    inc di
    jmp .cp_dp
.dp_sep:
    cmp di, U_ARG_C
    jbe .app_dl
    cmp byte [di - 1], 0x5C
    je .app_dl
    mov byte [di], 0x5C
    inc di
.app_dl:
    mov si, (U_DTA_WORK + 30)
.cp_dl:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_dl

    mov dx, U_IO_B
    mov di, U_ARG_C
    call xcopy_one_file

    mov dx, U_DTA_WORK
    mov ah, 0x1A
    int 0x21
    mov ah, 0x4F
    int 0x21
    jnc .dta_lp

.summary:
    mov ax, word [wXcCount]
    call u_print_u16
    mov si, addr(szXcSummary)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.err_nf:
    mov al, 4
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bXcQuiet, 0
@data db bXcFull, 0
@data db bXcListOnly, 0
@data db bXcMkdir, 0
@data dw wXcCount, 0
@data dw wSrcLeafOff, 0
@data db szXcArrow, " -> ", 0
@data db szXcSummary, " File(s) copied", 0
@data db szXcHelp, "XCOPY source [destination] [/S] [/E] [/Y] [/Q] [/F] [/L] [/I]", 0
