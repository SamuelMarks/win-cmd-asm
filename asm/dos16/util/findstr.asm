;; @file findstr.asm
;; @isa 8086
;; @desc Standalone FINDSTR.COM utility (/B, /E, /L, /R, /I, /X, /V, /N, /M, /C:"str" pattern [file]).

@cpu 8086
@format com
@entry findstr_boot

@include "../../inc/dos_equ.inc"

;; @proc findstr_boot
;; @abi dos16
;; @in DS=ES=CS=PSP
;; @out Jumps to findstr_entry
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc 0100h entrypoint trampoline for FINDSTR.COM.
@proc findstr_boot
    jmp findstr_entry
@endproc

;; @proc fs_match_here
;; @abi dos16
;; @in DS:SI = text cursor, CX = remaining text len, DS:DI = regex cursor
;; @out CF=0 if regex matches at DS:SI, CF=1 otherwise
;; @err CF=1 on mismatch
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Matches '.', '*', '$', and literal/case-folded characters on bounded lines.
@proc fs_match_here
.m_lp:
    mov al, byte [di]
    test al, al
    jz .yes
    cmp byte [di + 1], '*'
    je .do_star
    cmp al, '$'
    jne .chk_txt
    cmp byte [di + 1], 0
    jne .chk_txt
    test cx, cx
    jz .yes
    jmp .no

.chk_txt:
    test cx, cx
    jz .no
    cmp al, '.'
    je .adv_one
    mov ah, byte [si]
    cmp byte [bFsNocase], 0
    je .cmp_exact
    call u_chr_upper
    mov bl, al
    mov al, ah
    call u_chr_upper
    cmp al, bl
    jne .no
    jmp .adv_one
.cmp_exact:
    cmp al, ah
    jne .no
.adv_one:
    inc si
    inc di
    dec cx
    jmp .m_lp

.do_star:
    mov dl, al ; repeated atom ('.' or char)
    add di, 2
.star_lp:
    push si
    push cx
    push di
    push dx
    call fs_match_here
    pop dx
    pop di
    pop cx
    pop si
    jnc .yes
    test cx, cx
    jz .no
    cmp dl, '.'
    je .star_eat
    mov al, dl
    mov ah, byte [si]
    cmp byte [bFsNocase], 0
    je .star_ex
    call u_chr_upper
    mov bl, al
    mov al, ah
    call u_chr_upper
    cmp al, bl
    jne .no
    jmp .star_eat
.star_ex:
    cmp al, ah
    jne .no
.star_eat:
    inc si
    dec cx
    jmp .star_lp

.yes:
    clc
    ret
.no:
    stc
    ret
@endproc

;; @proc fs_match_line
;; @abi dos16
;; @in DS:SI = line start, CX = line len, U_ARG_A = pattern
;; @out CF=0 if line matches pattern/flags, CF=1 otherwise
;; @err CF=1 on mismatch
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates literal (/L) or regex (/R, /B, /E, /X, '^', '$') match against one line.
@proc fs_match_line
    cmp byte [bFsLit], 0
    je .regex_mode
    mov di, U_ARG_A
    mov dl, byte [bFsNocase]
    call u_contains_sub
    ret

.regex_mode:
    mov di, U_ARG_A
    cmp byte [bFsBeg], 0
    jne .anchored
    cmp byte [bFsExact], 0
    jne .exact_ln
    cmp byte [di], '^'
    jne .unanchored
    inc di
.anchored:
    call fs_match_here
    ret

.exact_ln:
    ; Append '$' semantics: must consume entire CX!
    push si
    push cx
    mov si, U_ARG_A
    call u_strlen
    mov bx, cx
    pop cx
    pop si
    mov byte [U_ARG_A + bx], '$'
    mov byte [U_ARG_A + bx + 1], 0
    push bx
    mov di, U_ARG_A
    call fs_match_here
    pop bx
    mov byte [U_ARG_A + bx], 0
    ret

.unanchored:
    push si
    push cx
    mov di, U_ARG_A
    call fs_match_here
    pop cx
    pop si
    jnc .hit
    test cx, cx
    jz .miss
    inc si
    dec cx
    jmp .unanchored
.hit:
    clc
    ret
.miss:
    stc
    ret
@endproc

;; @proc findstr_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=switches, pattern, optional file
;; @out Prints matching lines or /M filename; exits 0 (match), 1 (no match), 2 (error)
;; @err Exits with 2 on syntax/open error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for FINDSTR.COM supporting regex (/R), literal (/L), /I, /V, /N, /B, /X, /M.
@proc findstr_entry
    call u_init
    mov byte [U_ARG_A], 0
    mov byte [U_ARG_B], 0

.arg_lp:
    call u_skip_ws
    test al, al
    jnz .not_end_a
    jmp .args_done
.not_end_a:
    cmp al, '/'
    je .is_sw_a
    jmp .pos_tok
.is_sw_a:
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    jne .not_h_a
    jmp .help
.not_h_a:
    call u_chr_upper
    cmp al, 'I'
    jne .f_v
    mov byte [bFsNocase], 1
    jmp .arg_lp
.f_v:
    cmp al, 'V'
    jne .f_n
    mov byte [bFsInv], 1
    jmp .arg_lp
.f_n:
    cmp al, 'N'
    jne .f_b
    mov byte [bFsNum], 1
    jmp .arg_lp
.f_b:
    cmp al, 'B'
    jne .f_x
    mov byte [bFsBeg], 1
    jmp .arg_lp
.f_x:
    cmp al, 'X'
    jne .f_l
    mov byte [bFsExact], 1
    jmp .arg_lp
.f_l:
    cmp al, 'L'
    jne .f_m
    mov byte [bFsLit], 1
    jmp .arg_lp
.f_m:
    cmp al, 'M'
    jne .f_c
    mov byte [bFsNameOnly], 1
    jmp .arg_lp
.f_c:
    cmp al, 'C'
    jne .arg_lp
    cmp byte [U_ARG_C + 2], ':'
    jne .arg_lp
    push si
    mov si, (U_ARG_C + 3)
    mov di, U_ARG_A
.cp_fc:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_fc
    pop si
    mov byte [bFsLit], 1
    mov byte [bFsHasPat], 1
    jmp .arg_lp

.help:
    mov si, addr(szFsHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.pos_tok:
    cmp byte [bFsHasPat], 0
    jne .file_tok
    mov di, U_ARG_A
    call u_read_tok
    mov byte [bFsHasPat], 1
    jmp .arg_lp
.file_tok:
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp

.args_done:
    cmp byte [bFsHasPat], 0
    jne .load_src
    mov al, 2
    call u_exit
    ret

.load_src:
    mov dx, U_ARG_B
    mov di, U_IO_A
    mov cx, 16000
    call u_load_stream
    jnc .scan_st
    mov al, 2
    call u_exit
    ret

.scan_st:
    mov word [wFsLn], 0
    mov word [wFsHits], 0
    mov si, U_IO_A

.ln_lp:
    cmp byte [si], 0
    je .done_scan
    cmp byte [si], 0x1A
    je .done_scan
    inc word [wFsLn]
    mov bx, si
    xor cx, cx
.sc_eol:
    mov al, byte [si]
    test al, al
    jz .have_ln
    cmp al, 0x1A
    je .have_ln
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
    jne .have_ln
.lf_ln:
    inc si

.have_ln:
    push si
    push bx
    push cx
    mov si, bx
    call fs_match_line
    mov al, 0
    jc .inv_chk
    mov al, 1
.inv_chk:
    xor al, byte [bFsInv]
    pop cx
    pop bx
    pop si
    test al, al
    jz .ln_lp

    inc word [wFsHits]
    cmp byte [bFsNameOnly], 0
    je .prt_hit
    mov si, U_ARG_B
    call u_write_str
    call u_write_crlf
    jmp .done_scan

.prt_hit:
    push si
    push bx
    push cx
    cmp byte [bFsNum], 0
    je .prt_txt
    mov ax, word [wFsLn]
    call u_print_u16
    mov dl, ':'
    call u_write_ch
.prt_txt:
    pop cx
    pop dx
    call u_write_buf
    call u_write_crlf
    pop si
    jmp .ln_lp

.done_scan:
    cmp word [wFsHits], 0
    je .no_hit
    xor al, al
    call u_exit
    ret
.no_hit:
    mov al, 1
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bFsNocase, 0
@data db bFsInv, 0
@data db bFsNum, 0
@data db bFsBeg, 0
@data db bFsExact, 0
@data db bFsLit, 0
@data db bFsNameOnly, 0
@data db bFsHasPat, 0
@data dw wFsLn, 0
@data dw wFsHits, 0
@data db szFsHelp, "FINDSTR [/B] [/E] [/L] [/R] [/I] [/X] [/V] [/N] [/M] [/C:str] pattern [file]", 0
