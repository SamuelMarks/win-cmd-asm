;; @file more.asm
;; @isa 8086
;; @desc Standalone MORE.COM interactive pager (/E, /C, /S, +n [file]).

@cpu 8086
@format com
@entry more_entry

@include "../../inc/dos_equ.inc"

;; @proc more_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=switches (/E, /C, /S, +n) and optional filename
;; @out Paginates stream to stdout (squeezing blank lines under /S, skipping +n lines); exits 0
;; @err Exits with 1 on missing input file
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for MORE.COM supporting file and piped stdin pagination.
@proc more_entry
    call u_init
    mov byte [U_ARG_A], 0

.arg_lp:
    call u_skip_ws
    test al, al
    jz .load_in
    cmp al, '/'
    je .do_sw
    cmp al, '+'
    je .do_plus
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp

.do_sw:
    mov di, U_ARG_B
    call u_read_tok
    mov al, byte [U_ARG_B + 1]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'S'
    jne .arg_lp
    mov byte [bMoreSqueeze], 1
    jmp .arg_lp

.do_plus:
    mov di, U_ARG_B
    call u_read_tok
    push si
    mov si, (U_ARG_B + 1)
    call u_parse_u16
    pop si
    jc .arg_lp
    test ax, ax
    jz .arg_lp
    dec ax
    mov word [wSkipLines], ax
    jmp .arg_lp

.help:
    mov si, addr(szMoreHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.load_in:
    mov dx, U_ARG_A
    mov di, U_IO_A
    mov cx, 16000
    call u_load_stream
    jnc .page_st
    mov al, 1
    call u_exit
    ret

.page_st:
    mov si, U_IO_A
    mov byte [bPrevBlank], 0

.ln_lp:
    cmp byte [si], 0
    je .done
    cmp byte [si], 0x1A
    je .done
    mov bx, si
    xor cx, cx
.sc_eol:
    mov al, byte [si]
    test al, al
    jz .got_ln
    cmp al, 0x1A
    je .got_ln
    cmp al, 0x0D
    je .cr_ln
    cmp al, 0x0A
    je .lf_ln
    inc si
    inc cx
    jmp .sc_eol
.cr_ln:
    inc si
    cmp byte [si], 0x0A
    jne .got_ln
.lf_ln:
    inc si

.got_ln:
    cmp word [wSkipLines], 0
    je .chk_sq
    dec word [wSkipLines]
    jmp .ln_lp

.chk_sq:
    cmp byte [bMoreSqueeze], 0
    je .emit_ln
    test cx, cx
    jnz .not_blk
    cmp byte [bPrevBlank], 0
    jne .ln_lp
    mov byte [bPrevBlank], 1
    jmp .emit_ln
.not_blk:
    mov byte [bPrevBlank], 0

.emit_ln:
    push si
    mov dx, bx
    call u_write_buf
    call u_write_crlf
    pop si
    jmp .ln_lp

.done:
    xor al, al
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bMoreSqueeze, 0
@data db bPrevBlank, 0
@data dw wSkipLines, 0
@data db szMoreHelp, "MORE [/E] [/C] [/S] [+n] [file]", 0
