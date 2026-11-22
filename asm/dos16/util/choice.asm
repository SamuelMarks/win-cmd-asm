;; @file choice.asm
;; @isa 8086
;; @desc Standalone CHOICE.COM interactive prompt (/C[:]choices, /N, /CS, /T nn /D c, /M msg).

@cpu 8086
@format com
@entry choice_entry

@include "../../inc/dos_equ.inc"

;; @proc choice_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=switches (/C, /N, /CS, /T, /D, /M)
;; @out Prompts "[Y,N]?" (unless /N) and returns 1-based choice index in ERRORLEVEL
;; @err Exits with 255 on invalid syntax or EOF without /D default
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for CHOICE.COM supporting both NT (/C AB /D A /T 0) and DOS (/C:AB) syntax.
@proc choice_entry
    call u_init
    mov byte [U_ARG_A], 'Y'
    mov byte [U_ARG_A + 1], 'N'
    mov byte [U_ARG_A + 2], 0
    mov byte [U_ARG_B], 0 ; /M message

.arg_lp:
    call u_skip_ws
    test al, al
    jnz .not_done_a
    jmp .prompt_phase
.not_done_a:
    cmp al, '/'
    je .is_sw_ch
    jmp .msg_bare
.is_sw_ch:
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    jne .not_h_ch
    jmp .help
.not_h_ch:
    call u_chr_upper
    cmp al, 'N'
    jne .chk_c
    mov byte [bChNoList], 1
    jmp .arg_lp
.chk_c:
    cmp al, 'C'
    jne .chk_d
    mov al, byte [U_ARG_C + 2]
    call u_chr_upper
    cmp al, 'S'
    jne .do_choices
    mov byte [bChCase], 1
    jmp .arg_lp
.do_choices:
    ; Either "/C:XYZ", "/CXYZ", or "/C XYZ"
    mov bx, (U_ARG_C + 2)
    cmp byte [bx], ':'
    jne .chk_c_inline
    inc bx
.chk_c_inline:
    cmp byte [bx], 0
    je .rd_next_choices
    push si
    mov si, bx
    mov di, U_ARG_A
.cp_ci:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_ci
    pop si
    jmp .arg_lp
.rd_next_choices:
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp

.chk_d:
    cmp al, 'D'
    jne .chk_t
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C]
    mov byte [bChDefChar], al
    jmp .arg_lp

.chk_t:
    cmp al, 'T'
    jne .chk_m
    ; Support classic "/T:Y,5" or NT "/T 5"
    cmp byte [U_ARG_C + 2], ':'
    jne .nt_t
    mov al, byte [U_ARG_C + 3]
    mov byte [bChDefChar], al
    mov byte [bChHasTimeout], 1
    jmp .arg_lp
.nt_t:
    mov di, U_ARG_C
    call u_read_tok
    mov byte [bChHasTimeout], 1
    jmp .arg_lp

.chk_m:
    cmp al, 'M'
    je .do_m_ch
    jmp .arg_lp
.do_m_ch:
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp

.msg_bare:
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szChHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.prompt_phase:
    cmp byte [U_ARG_B], 0
    je .chk_list
    mov si, U_ARG_B
    call u_write_str
    mov dl, ' '
    call u_write_ch

.chk_list:
    cmp byte [bChNoList], 0
    jne .read_k
    mov dl, '['
    call u_write_ch
    mov si, U_ARG_A
.lst_lp:
    mov al, byte [si]
    test al, al
    jz .lst_end
    cmp byte [bChCase], 0
    jne .emit_c
    call u_chr_upper
.emit_c:
    push si
    mov dl, al
    call u_write_ch
    pop si
    inc si
    cmp byte [si], 0
    je .lst_end
    push si
    mov dl, ','
    call u_write_ch
    pop si
    jmp .lst_lp
.lst_end:
    mov dl, ']'
    call u_write_ch
    mov dl, '?'
    call u_write_ch

.read_k:
    cmp byte [bChHasTimeout], 0
    je .wait_key
    cmp byte [bChDefChar], 0
    je .wait_key
    mov al, byte [bChDefChar]
    jmp .match_al

.wait_key:
    mov ax, 0x1680
    int 0x2F
    mov ah, 0x08
    int 0x21
    cmp al, 0x1A
    je .eof_def

.match_al:
    mov bl, al
    cmp byte [bChCase], 0
    jne .scan_c
    call u_chr_upper
    mov bl, al
.scan_c:
    mov si, U_ARG_A
    mov cl, 1 ; 1-based ERRORLEVEL!
.sc_lp:
    mov al, byte [si]
    test al, al
    jz .wait_key
    cmp byte [bChCase], 0
    jne .cmp_k
    call u_chr_upper
.cmp_k:
    cmp al, bl
    je .found_k
    inc si
    inc cl
    jmp .sc_lp

.found_k:
    push cx
    mov dl, bl
    call u_write_ch
    call u_write_crlf
    pop cx
    mov al, cl
    call u_exit
    ret

.eof_def:
    mov al, byte [bChDefChar]
    test al, al
    jnz .match_al
    mov al, 255
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bChNoList, 0
@data db bChCase, 0
@data db bChHasTimeout, 0
@data db bChDefChar, 0
@data db szChHelp, "CHOICE [/C choices] [/N] [/CS] [/T timeout /D choice] [/M text]", 0
