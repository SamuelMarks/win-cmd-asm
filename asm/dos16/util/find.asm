;; @file find.asm
;; @isa 8086
;; @desc Standalone FIND.COM utility (/V, /C, /N, /I, /OFF "string" [file]).

@cpu 8086
@format com
@entry find_entry

@include "../../inc/dos_equ.inc"

;; @proc find_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=switches, "needle", optional filename
;; @out Prints matching lines (or /C count, /N [line] numbers); exits 0 (match), 1 (no match), 2 (err)
;; @err Exits with 2 on missing needle or file open error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for FIND.COM supporting stdin and file streams across MS-DOS 1.25..8.0.
@proc find_entry
    call u_init
    mov byte [bOptV], 0
    mov byte [bOptC], 0
    mov byte [bOptN], 0
    mov byte [bOptI], 0
    mov byte [U_ARG_A], 0 ; needle
    mov byte [U_ARG_B], 0 ; filename (empty = stdin)
    mov byte [bGotNeedle], 0

.parse_lp:
    call u_skip_ws
    test al, al
    jz .args_ready
    cmp al, '/'
    jne .read_pos_arg
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    je .show_help
    call u_chr_upper
    cmp al, 'V'
    jne .chk_c
    mov byte [bOptV], 1
    jmp .parse_lp
.chk_c:
    cmp al, 'C'
    jne .chk_n
    mov byte [bOptC], 1
    jmp .parse_lp
.chk_n:
    cmp al, 'N'
    jne .chk_i
    mov byte [bOptN], 1
    jmp .parse_lp
.chk_i:
    cmp al, 'I'
    jne .parse_lp
    mov byte [bOptI], 1
    jmp .parse_lp

.show_help:
    mov si, addr(szFindHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.read_pos_arg:
    cmp byte [bGotNeedle], 0
    jne .read_file_arg
    mov di, U_ARG_A
    call u_read_tok
    mov byte [bGotNeedle], 1
    jmp .parse_lp
.read_file_arg:
    mov di, U_ARG_B
    call u_read_tok
    jmp .parse_lp

.args_ready:
    cmp byte [bGotNeedle], 0
    jne .load_in
    mov si, addr(szFindHelp)
    call u_write_str
    call u_write_crlf
    mov al, 2
    call u_exit
    ret

.load_in:
    mov dx, U_ARG_B
    mov di, U_IO_A
    mov cx, 16000
    call u_load_stream
    jnc .scan_lines
    mov si, addr(szFindNf)
    call u_write_str
    call u_write_crlf
    mov al, 2
    call u_exit
    ret

.scan_lines:
    mov word [wLineNo], 0
    mov word [wMatchCnt], 0
    mov si, U_IO_A

.line_lp:
    cmp byte [si], 0
    jne .not_eof
    jmp .scan_done
.not_eof:
    cmp byte [si], 0x1A
    jne .go_ln
    jmp .scan_done
.go_ln:
    inc word [wLineNo]
    mov bx, si
    xor cx, cx
.find_eol:
    mov al, byte [si]
    test al, al
    jz .got_line
    cmp al, 0x1A
    je .got_line
    cmp al, 0x0D
    je .eat_crlf
    cmp al, 0x0A
    je .eat_lf
    inc si
    inc cx
    jmp .find_eol
.eat_crlf:
    inc si
    cmp byte [si], 0x0A
    jne .got_line
.eat_lf:
    inc si

.got_line:
    push si ; next line cursor
    push bx ; line start
    push cx ; line len
    mov si, bx
    mov di, U_ARG_A
    mov dl, byte [bOptI]
    call u_contains_sub
    mov al, 0
    jc .chk_inv
    mov al, 1
.chk_inv:
    xor al, byte [bOptV]
    pop cx
    pop bx
    pop si
    test al, al
    jz .line_lp

    inc word [wMatchCnt]
    cmp byte [bOptC], 0
    jne .line_lp

    push si
    push bx
    push cx
    cmp byte [bOptN], 0
    je .emit_body
    mov dl, '['
    call u_write_ch
    mov ax, word [wLineNo]
    call u_print_u16
    mov dl, ']'
    call u_write_ch
.emit_body:
    pop cx
    pop dx ; DX = line start BX!
    call u_write_buf
    call u_write_crlf
    pop si
    jmp .line_lp

.scan_done:
    cmp byte [bOptC], 0
    je .exit_status
    cmp byte [U_ARG_B], 0
    je .cnt_only
    mov si, addr(szFindSep)
    call u_write_str
    mov si, U_ARG_B
    call u_write_str
    mov si, addr(szColonSp)
    call u_write_str
.cnt_only:
    mov ax, word [wMatchCnt]
    call u_print_u16
    call u_write_crlf

.exit_status:
    cmp word [wMatchCnt], 0
    je .no_matches
    xor al, al
    call u_exit
    ret
.no_matches:
    mov al, 1
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bOptV, 0
@data db bOptC, 0
@data db bOptN, 0
@data db bOptI, 0
@data db bGotNeedle, 0
@data dw wLineNo, 0
@data dw wMatchCnt, 0
@data db szFindSep, "---------- ", 0
@data db szColonSp, ": ", 0
@data db szFindHelp, "FIND [/V] [/C] [/N] [/I] string [file]", 0
@data db szFindNf, "File not found", 0
