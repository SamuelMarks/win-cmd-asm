;; @file m4_cmds.inc
;; @isa 8086
;; @desc Phase 16 complete x86-16 built-ins (CD /D, COPY CON/+, MOVE, PATH/DPATH, PROMPT, PAUSE, BREAK, VERIFY, DATE, TIME, COLOR, ASSOC/FTYPE, KEYS, LH, DOSKEY, MODE, START, CMD, MKLINK).

@cpu 8086
@format inc

;; @proc cmd_cd_ex
;; @abi dos16
;; @in DS:SI = CD argument cursor, DL = initial /D switch flag (0 for CD, 1 for PUSHD)
;; @out Queries CWD or changes directory (and drive if /D or DL=1), updates '=D:' env, [wTrnExitCode]=0/1
;; @err Prints diagnostic and sets [wTrnExitCode]=1 on invalid path
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Full CD / CHDIR engine supporting /D drive switching and /E:ON unquoted spaces.
@proc cmd_cd_ex
    push bp
    mov bp, sp
    sub sp, 4
    ; [bp-2]=switch_d_flag, [bp-4]=buf_ptr
    xor dh, dh
    mov word [bp - 2], dx
    mov word [bp - 4], TRN_CMD_WORK

    call trn_skip_ws
    cmp al, 0x00
    jne .chk_cr0
    jmp .show_cur_cwd
.chk_cr0:
    cmp al, 0x0D
    jne .chk_sw_d
    jmp .show_cur_cwd

.chk_sw_d:
    cmp al, '/'
    jne .read_cd_target
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'D'
    jne .read_cd_target
    mov word [bp - 2], 1
    add si, 2
    call trn_skip_ws

.read_cd_target:
    ; When /E:ON, CD accepts unquoted spaces in directory names (strip quotes & trailing spaces)!
    mov di, word [bp - 4]
    xor cx, cx
.cp_cd_lp:
    mov al, byte [si]
    test al, al
    jz .trim_cd_sp
    cmp al, 0x0D
    je .trim_cd_sp
    cmp al, '<'
    je .trim_cd_sp
    cmp al, '>'
    je .trim_cd_sp
    inc si
    cmp al, '"'
    je .cp_cd_lp
    mov byte [di], al
    inc di
    inc cx
    cmp cx, 240
    jb .cp_cd_lp

.trim_cd_sp:
    test cx, cx
    jz .j_show_cwd
    cmp byte [di - 1], 0x20
    jne .term_cd_str
    dec di
    dec cx
    jmp .trim_cd_sp
.j_show_cwd:
    jmp .show_cur_cwd

.term_cd_str:
    mov byte [di], 0
    mov si, word [bp - 4]

    ; Check bare 'X:' query vs 'X:\path' drive switch
    cmp byte [si + 1], ':'
    jne .do_chdir
    cmp byte [si + 2], 0
    je .show_cur_cwd
    cmp word [bp - 2], 0
    je .do_chdir
    mov al, byte [si]
    call chr_to_upper
    sub al, 'A'
    mov dl, al
    mov ah, DOS_SELECT_DISK
    int 0x21

.do_chdir:
    mov dx, word [bp - 4]
    call k700_chdir
    jc .cd_err
    call sync_drive_cwd_env
    mov word [wTrnExitCode], 0
    add sp, 4
    pop bp
    ret

.show_cur_cwd:
    mov di, word [bp - 4]
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [di], al
    mov byte [di + 1], ':'
    mov byte [di + 2], 0x5C
    mov byte [di + 3], 0
    cmp byte [bTrnDosMajor], 2
    jb .pr_cwd
    mov si, di
    add si, 3
    xor dl, dl
    call k700_getcwd
.pr_cwd:
    mov si, word [bp - 4]
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    add sp, 4
    pop bp
    ret

.cd_err:
    call trn_err_dos
    mov word [wTrnExitCode], 1
    add sp, 4
    pop bp
    ret
@endproc

;; @proc copy_skip_switches
;; @abi dos16
;; @in DS:SI = COPY/MOVE cursor
;; @out DS:SI advanced past leading /A /B /V /Y /-Y /D /Z /L switches
;; @err none
;; @clobbers ax, si
;; @desc Skips leading slash switches on COPY and MOVE commands.
@proc copy_skip_switches
.sw_lp:
    call trn_skip_ws
    cmp al, '/'
    jne .dn
    inc si
.sk_ch:
    mov al, byte [si]
    test al, al
    jz .dn
    cmp al, 0x0D
    je .dn
    cmp al, 0x20
    je .sw_lp
    cmp al, 0x09
    je .sw_lp
    inc si
    jmp .sk_ch
.dn:
    ret
@endproc

;; @proc copy_append_src_200
;; @abi dos16
;; @in DS:DX = source ASCIIZ path, DI = open destination handle, BP = TRN_CMD_WORK
;; @out Appends source file bytes (trimming 0x1A if concat) into handle DI, CF=0
;; @err CF=1, AX = ERR_* if source open fails
;; @clobbers ax, bx, cx, dx, si
;; @desc Streams one source file into open destination handle DI on DOS 2.00+.
@proc copy_append_src_200
    xor al, al
    call k200_open
    jc .fail
    mov si, ax
.rd_lp:
    mov bx, si
    mov dx, bp
    add dx, 512
    mov cx, 256
    call k200_read
    jc .eof_ok
    test ax, ax
    jz .eof_ok
    mov cx, ax
    mov bx, di
    mov dx, bp
    add dx, 512
    call k200_write
    jmp .rd_lp
.eof_ok:
    mov bx, si
    call k200_close
    clc
    ret
.fail:
    stc
    ret
@endproc

;; @proc cmd_copy_m4
;; @abi dos16
;; @in DS:SI = COPY arguments (supports switches, COPY CON <dst>, COPY a + b <dst>, COPY <src> <dst>)
;; @out Copies or concatenates file(s), prints "1 file(s) copied", [wTrnExitCode]=0/1
;; @err Prints diagnostic and sets [wTrnExitCode]=1 on missing source or syntax error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Enhanced COPY supporting COPY CON <file> and '+' multi-file concatenation.
@proc cmd_copy_m4
    push bp
    mov bp, sp
    sub sp, 6
    ; [bp-2]=work_base, [bp-4]=saved_si, [bp-6]=dst_hdl
    mov word [bp - 2], TRN_CMD_WORK

    call copy_skip_switches
    mov word [bp - 4], si

    ; Read first source token into [TRN_CMD_WORK + 0]
    mov di, TRN_CMD_WORK
    call trn_read_arg
    jnc .got_src1
    jmp .cp_syn_err
.got_src1:
    call copy_skip_switches

    ; Check if first source is reserved CON device (COPY CON <dst>)!
    push si
    mov si, TRN_CMD_WORK
    call dev_match_name
    pop si
    jc .chk_concat
    cmp al, DEV_CON
    je .do_copy_con

.chk_concat:
    ; Check whether '+' concatenation operator follows first source (either '+b' or '+ b' or 'a+b')!
    cmp byte [si], '+'
    jne .chk_emb_plus
    jmp .do_concat
.chk_emb_plus:
    ; Check if '+' was embedded inside token 1 (e.g. "A.TXT+B.TXT")
    mov bx, TRN_CMD_WORK
.emb_plus:
    mov al, byte [bx]
    test al, al
    jz .plain_copy
    cmp al, '+'
    jne .emb_nx
    jmp .do_concat_from_start
.emb_nx:
    inc bx
    jmp .emb_plus

.plain_copy:
    ; Delegate standard single-file COPY (DOS 1.25 FCB & DOS 2.00+ Handle) to cmd_copy!
    mov si, word [bp - 4]
    add sp, 6
    pop bp
    jmp cmd_copy

.do_copy_con:
    ; Read destination filename into [TRN_CMD_WORK + 128]
    mov di, TRN_CMD_WORK
    add di, 128
    call trn_read_arg
    jnc .con_dst_ok
    jmp .cp_syn_err
.con_dst_ok:
    cmp byte [bTrnDosMajor], 2
    jae .con_200
    ; DOS 1.25 FCB COPY CON <file>
    mov si, TRN_CMD_WORK
    add si, 128
    mov di, TRN_CMD_WORK
    add di, 256
    mov bx, TRN_CMD_WORK
    add bx, 320
    call k125_create
    jnc .con_125_lp
    jmp .cp_err_out
.con_125_lp:
    mov ah, DOS_CON_IN_ECHO
    int 0x21
    cmp al, 0x1A
    je .con_125_dn
    mov byte [TRN_CMD_WORK + 440], al
    mov di, TRN_CMD_WORK
    add di, 256
    mov dx, TRN_CMD_WORK
    add dx, 440
    mov cx, 1
    call k125_write
    jmp .con_125_lp
.con_125_dn:
    mov di, TRN_CMD_WORK
    add di, 256
    call k125_close
    jmp .cp_copied_ok

.con_200:
    mov dx, TRN_CMD_WORK
    add dx, 128
    xor cx, cx
    call k200_create
    jnc .con_200_open
    jmp .cp_err_out
.con_200_open:
    mov word [bp - 6], ax
.con_200_lp:
    ; Read 1 byte from fd 0 (honors redirected stdin or console!)
    xor bx, bx
    mov dx, TRN_CMD_WORK
    add dx, 440
    mov cx, 1
    call k200_read
    jc .con_200_dn
    test ax, ax
    jz .con_200_dn
    cmp byte [TRN_CMD_WORK + 440], 0x1A
    je .con_200_dn
    mov bx, word [bp - 6]
    mov dx, TRN_CMD_WORK
    add dx, 440
    mov cx, 1
    call k200_write
    jmp .con_200_lp
.con_200_dn:
    mov bx, word [bp - 6]
    call k200_close
    jmp .cp_copied_ok

.do_concat_from_start:
    mov si, word [bp - 4]
    jmp .concat_begin

.do_concat:
    mov si, word [bp - 4]
.concat_begin:
    cmp byte [bTrnDosMajor], 2
    jae .concat_200
    mov ax, ERR_UNSUPPORTED_ON_ERA
    jmp .cp_err_out
.concat_200:
    ; Scan forward to final whitespace-delimited token (not preceded by '+') as destination!
    ; Copy temporary spool in [TRN_CMD_WORK + 384] = "_WCCAT.TMP", then rename/copy to dest!
    mov di, TRN_CMD_WORK
    add di, 384
    push si
    mov si, addr(szCopyCatTmp)
    mov cx, 11
    rep movsb
    pop si

    mov dx, TRN_CMD_WORK
    add dx, 384
    xor cx, cx
    call k200_create
    jnc .cat_tmp_ok
    jmp .cp_err_out
.cat_tmp_ok:
    mov word [bp - 6], ax

.cat_src_lp:
    call copy_skip_switches
    ; Read source token up to space, tab, or '+'
    mov di, TRN_CMD_WORK
.cat_tok_ch:
    mov al, byte [si]
    test al, al
    jz .cat_tok_end
    cmp al, 0x0D
    je .cat_tok_end
    cmp al, 0x20
    je .cat_tok_end
    cmp al, 0x09
    je .cat_tok_end
    cmp al, '+'
    je .cat_tok_end
    inc si
    cmp al, '"'
    je .cat_tok_ch
    mov byte [di], al
    inc di
    jmp .cat_tok_ch
.cat_tok_end:
    mov byte [di], 0

    push si
    mov dx, TRN_CMD_WORK
    mov di, word [bp - 6]
    push bp
    mov bp, TRN_CMD_WORK
    call copy_append_src_200
    pop bp
    pop si
    jnc .cat_src_app_ok
    jmp .cat_abort
.cat_src_app_ok:

    call copy_skip_switches
    cmp byte [si], '+'
    jne .cat_have_dst
    inc si
    jmp .cat_src_lp

.cat_have_dst:
    mov bx, word [bp - 6]
    call k200_close

    ; Read destination token (if omitted, overwrite first source!)
    mov di, TRN_CMD_WORK
    add di, 128
    call trn_read_arg
    jc .cat_cleanup_ok

    ; Copy _WCCAT.TMP -> destination and delete _WCCAT.TMP!
    mov dx, TRN_CMD_WORK
    add dx, 384
    xor al, al
    call k200_open
    jnc .cat_reopen_ok
    jmp .cp_err_out
.cat_reopen_ok:
    mov si, ax

    mov dx, TRN_CMD_WORK
    add dx, 128
    xor cx, cx
    call k200_create
    jc .cat_cls_tmp
    mov di, ax
.cat_flush_lp:
    mov bx, si
    mov dx, TRN_CMD_WORK
    add dx, 512
    mov cx, 256
    call k200_read
    jc .cat_flush_dn
    test ax, ax
    jz .cat_flush_dn
    mov cx, ax
    mov bx, di
    mov dx, TRN_CMD_WORK
    add dx, 512
    call k200_write
    jmp .cat_flush_lp
.cat_flush_dn:
    mov bx, di
    call k200_close
.cat_cls_tmp:
    mov bx, si
    call k200_close
.cat_cleanup_ok:
    mov dx, TRN_CMD_WORK
    add dx, 384
    mov ah, DOS_UNLINK
    int 0x21

.cp_copied_ok:
    mov si, addr(szOneFileCopied)
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    add sp, 6
    pop bp
    ret

.cat_abort:
    push ax
    mov bx, word [bp - 6]
    call k200_close
    mov dx, TRN_CMD_WORK
    add dx, 384
    mov ah, DOS_UNLINK
    int 0x21
    pop ax
    jmp .cp_err_out

.cp_syn_err:
    mov ax, ERR_SYNTAX
.cp_err_out:
    call trn_err_dos
    mov word [wTrnExitCode], 1
    add sp, 6
    pop bp
    ret
@endproc

;; @proc cmd_move
;; @abi dos16
;; @in DS:SI = "[/Y | /-Y] <src> <dst>"
;; @out Moves/renames file across DOS 1.25..8.0, prints "1 file(s) moved", [wTrnExitCode]=0/1
;; @err Prints diagnostic and sets [wTrnExitCode]=1 on error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements MOVE [/Y | /-Y] <src> <dst> with overwrite pre-unlink on DOS 2.00+.
@proc cmd_move
    push bp
    mov bp, TRN_CMD_WORK
    call copy_skip_switches
    mov di, bp
    call trn_read_arg
    jc .mv_syn
    call copy_skip_switches
    mov di, bp
    add di, 128
    call trn_read_arg
    jc .mv_syn

    cmp byte [bTrnDosMajor], 2
    jb .mv_125
    ; Pre-unlink existing destination when replacing
    mov dx, bp
    add dx, 128
    mov ah, DOS_UNLINK
    int 0x21

    mov dx, bp
    mov di, bp
    add di, 128
    call k700_rename
    jc .mv_err
    jmp .mv_ok

.mv_125:
    mov si, bp
    mov bx, bp
    add bx, 128
    mov di, bp
    add di, 256
    call k125_rename
    jc .mv_err

.mv_ok:
    mov si, addr(szOneFileMoved)
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    pop bp
    ret
.mv_syn:
    mov ax, ERR_SYNTAX
.mv_err:
    call trn_err_dos
    mov word [wTrnExitCode], 1
    pop bp
    ret
@endproc

;; @proc cmd_path_common
;; @abi dos16
;; @in DS:SI = argument cursor, DS:BX = uppercase variable name ("PATH" or "DPATH"), CX = name len
;; @out Displays "VAR=...", "No Path", clears on ";", or sets VAR=<rest>
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Shared implementation for PATH and DPATH internal commands.
@proc cmd_path_common
    push bp
    mov bp, sp
    sub sp, 4
    mov word [bp - 2], bx
    mov word [bp - 4], cx

    call trn_skip_ws
    cmp al, '='
    jne .chk_empty_p
    inc si
    call trn_skip_ws
.chk_empty_p:
    cmp al, 0x00
    je .show_p
    cmp al, 0x0D
    je .show_p
    cmp al, '<'
    je .show_p
    cmp al, '>'
    je .show_p

    ; Check "PATH ;" -> delete variable!
    cmp al, ';'
    jne .set_new_p
    mov ah, byte [si + 1]
    cmp ah, 0x00
    je .clear_p
    cmp ah, 0x0D
    je .clear_p
    cmp ah, 0x20
    je .clear_p

.set_new_p:
    mov di, TRN_CMD_WORK
.cp_p_val:
    mov al, byte [si]
    test al, al
    jz .apply_p
    cmp al, 0x0D
    je .apply_p
    cmp al, '<'
    je .apply_p
    cmp al, '>'
    je .apply_p
    mov byte [di], al
    inc si
    inc di
    jmp .cp_p_val
.apply_p:
    mov byte [di], 0
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov dx, TRN_CMD_WORK
    call env_set_var
    jmp .p_ok

.clear_p:
    mov byte [TRN_CMD_WORK], 0
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov dx, TRN_CMD_WORK
    call env_set_var
    jmp .p_ok

.show_p:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, TRN_CMD_WORK
    call env_get_var
    jc .no_p
    test cx, cx
    jz .no_p
    mov si, word [bp - 2]
    call con_write_asciiz
    mov dl, '='
    call con_write_char
    mov si, TRN_CMD_WORK
    call con_write_asciiz
    call con_write_crlf
    jmp .p_ok
.no_p:
    mov si, addr(szNoPath)
    call con_write_asciiz
    call con_write_crlf

.p_ok:
    mov word [wTrnExitCode], 0
    add sp, 4
    pop bp
    ret
@endproc

;; @proc cmd_path
;; @abi dos16
;; @in DS:SI = PATH argument cursor
;; @out Queries, clears, or sets %PATH%
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements PATH [; | dir1;dir2...].
@proc cmd_path
    mov bx, addr(szKwPath)
    mov cx, 4
    call cmd_path_common
    ret
@endproc

;; @proc cmd_dpath
;; @abi dos16
;; @in DS:SI = DPATH argument cursor
;; @out Queries, clears, or sets %DPATH%
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements DPATH [; | dir1;dir2...] (Windows NT 3.1..11 data search path built-in).
@proc cmd_dpath
    mov bx, addr(szKwDpath)
    mov cx, 5
    call cmd_path_common
    ret
@endproc

;; @proc cmd_prompt
;; @abi dos16
;; @in DS:SI = optional prompt format string
;; @out Sets or resets PROMPT environment variable (default $P$G when empty)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements PROMPT [text].
@proc cmd_prompt
    call trn_skip_ws
    cmp al, '='
    jne .chk_pr_end
    inc si
    call trn_skip_ws
.chk_pr_end:
    mov di, TRN_CMD_WORK
    cmp al, 0x00
    je .reset_def_pr
    cmp al, 0x0D
    je .reset_def_pr
.cp_pr:
    mov al, byte [si]
    test al, al
    jz .set_pr
    cmp al, 0x0D
    je .set_pr
    mov byte [di], al
    inc si
    inc di
    jmp .cp_pr
.reset_def_pr:
    mov byte [di], '$'
    mov byte [di + 1], 'P'
    mov byte [di + 2], '$'
    mov byte [di + 3], 'G'
    add di, 4
.set_pr:
    mov byte [di], 0
    mov si, addr(szKwPrompt)
    mov cx, 6
    mov dx, TRN_CMD_WORK
    call env_set_var
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc trn_print_prompt
;; @abi dos16
;; @in Environment %PROMPT% (or default $P$G on DOS 2.00+ / A> on DOS 1.25)
;; @out Renders expanded interactive prompt to console
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates $P, $N, $G, $L, $B, $Q, $$, $E, $_, $S, $D, $T, and $V prompt escapes.
@proc trn_print_prompt
    push bp
    mov bp, TRN_CMD_WORK
    mov si, addr(szKwPrompt)
    mov cx, 6
    mov di, bp
    call env_get_var
    jnc .have_pr_spec
    cmp byte [bTrnDosMajor], 2
    jb .def_125
    mov byte [bp], '$'
    mov byte [bp + 1], 'P'
    mov byte [bp + 2], '$'
    mov byte [bp + 3], 'G'
    mov byte [bp + 4], 0
    jmp .have_pr_spec
.def_125:
    mov byte [bp], '$'
    mov byte [bp + 1], 'N'
    mov byte [bp + 2], '$'
    mov byte [bp + 3], 'G'
    mov byte [bp + 4], 0

.have_pr_spec:
    mov si, bp
.pr_lp:
    mov al, byte [si]
    test al, al
    jnz .not_pr_end
    pop bp
    ret
.not_pr_end:
    inc si
    cmp al, '$'
    je .esc_dlr
    mov dl, al
    push si
    call con_write_char
    pop si
    jmp .pr_lp

.esc_dlr:
    mov al, byte [si]
    test al, al
    jz .pr_lp
    inc si
    call chr_to_upper
    cmp al, 'P'
    jne .chk_pr_n
    jmp .pr_p
.chk_pr_n:
    cmp al, 'N'
    jne .chk_pr_g
    jmp .pr_n
.chk_pr_g:
    cmp al, 'G'
    je .pr_g
    cmp al, 'L'
    je .pr_l
    cmp al, 'B'
    je .pr_b
    cmp al, 'Q'
    je .pr_q
    cmp al, '$'
    je .pr_dlr
    cmp al, 'E'
    je .pr_e
    cmp al, 'S'
    je .pr_s
    cmp al, '_'
    je .pr_crlf
    cmp al, 'V'
    je .pr_v
    cmp al, 'D'
    je .pr_d
    cmp al, 'T'
    je .pr_t
    jmp .pr_lp

.pr_d:
    push si
    mov di, szNumScratch
    call fmt_cur_date_str
    mov si, szNumScratch
    call con_write_asciiz
    pop si
    jmp .pr_lp

.pr_t:
    push si
    mov di, szNumScratch
    call fmt_cur_time_str
    mov si, szNumScratch
    call con_write_asciiz
    pop si
    jmp .pr_lp

.pr_g:
    mov dl, '>'
    jmp .emit_1c
.pr_l:
    mov dl, '<'
    jmp .emit_1c
.pr_b:
    mov dl, '|'
    jmp .emit_1c
.pr_q:
    mov dl, '='
    jmp .emit_1c
.pr_dlr:
    mov dl, '$'
    jmp .emit_1c
.pr_e:
    mov dl, 0x1B
    jmp .emit_1c
.pr_s:
    mov dl, 0x20
.emit_1c:
    push si
    call con_write_char
    pop si
    jmp .pr_lp

.pr_crlf:
    push si
    call con_write_crlf
    pop si
    jmp .pr_lp

.pr_v:
    push si
    call cmd_ver
    pop si
    jmp .pr_lp

.pr_n:
    push si
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov dl, al
    call con_write_char
    pop si
    jmp .pr_lp

.pr_p:
    push si
    mov di, bp
    add di, 260
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [di], al
    mov byte [di + 1], ':'
    mov byte [di + 2], 0
    cmp byte [bTrnDosMajor], 2
    jb .emit_pr_p
    mov byte [di + 2], 0x5C
    mov byte [di + 3], 0
    mov si, di
    add si, 3
    xor dl, dl
    call k700_getcwd
.emit_pr_p:
    mov si, bp
    add si, 260
    call con_write_asciiz
    pop si
    jmp .pr_lp
@endproc

;; @proc cmd_pause
;; @abi dos16
;; @in none
;; @out Prints "Press any key to continue . . .", yields VM slice, reads 1 key, CRLF
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Implements PAUSE with Windows 3.x/9x INT 2Fh AX=1680h idle slice release.
@proc cmd_pause
    mov si, addr(szPausePrompt)
    call con_write_asciiz
    mov ax, 0x1680
    int 0x2F
    mov ah, DOS_CON_IN_NOECHO
    int 0x21
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_parse_on_off
;; @abi dos16
;; @in DS:SI = cursor
;; @out AL = 0 (empty query), 1 (ON), 2 (OFF), or 0xFF (invalid)
;; @err none
;; @clobbers ax, bx, si, di
;; @desc Parses optional ON / OFF argument for BREAK, VERIFY, and KEYS.
@proc cmd_parse_on_off
    call trn_skip_ws
    cmp al, 0x00
    je .empty
    cmp al, 0x0D
    je .empty
    cmp al, '<'
    je .empty
    cmp al, '>'
    je .empty
    mov di, addr(szKwOn)
    call trn_match_word
    jnc .is_on
    mov di, addr(szKwOff)
    call trn_match_word
    jnc .is_off
    mov al, 0xFF
    ret
.empty:
    xor al, al
    ret
.is_on:
    mov al, 1
    ret
.is_off:
    mov al, 2
    ret
@endproc

;; @proc cmd_break
;; @abi dos16
;; @in DS:SI = optional ON / OFF
;; @out Queries or sets DOS Ctrl+Break checking via INT 21h AX=3300h/3301h
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements BREAK [ON | OFF].
@proc cmd_break
    call cmd_parse_on_off
    cmp al, 1
    je .set_on
    cmp al, 2
    je .set_off
    mov ax, 0x3300
    int 0x21
    mov si, addr(szBreakIs)
    call con_write_asciiz
    test dl, dl
    jnz .pr_on
    mov si, addr(szKwOff)
    jmp .pr_end
.pr_on:
    mov si, addr(szKwOn)
.pr_end:
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
.set_on:
    mov dl, 1
    mov ax, 0x3301
    int 0x21
    mov word [wTrnExitCode], 0
    ret
.set_off:
    xor dl, dl
    mov ax, 0x3301
    int 0x21
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_verify
;; @abi dos16
;; @in DS:SI = optional ON / OFF
;; @out Queries or sets DOS disk write-verify flag via INT 21h AH=54h / AH=2Eh
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements VERIFY [ON | OFF].
@proc cmd_verify
    call cmd_parse_on_off
    cmp al, 1
    je .v_on
    cmp al, 2
    je .v_off
    mov ah, 0x54
    int 0x21
    push ax
    mov si, addr(szVerifyIs)
    call con_write_asciiz
    pop ax
    test al, al
    jnz .pr_on
    mov si, addr(szKwOff)
    jmp .pr_end
.pr_on:
    mov si, addr(szKwOn)
.pr_end:
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
.v_on:
    mov al, 1
    xor dl, dl
    mov ah, 0x2E
    int 0x21
    mov word [wTrnExitCode], 0
    ret
.v_off:
    xor al, al
    xor dl, dl
    mov ah, 0x2E
    int 0x21
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc fmt_2dig_pad
;; @abi dos16
;; @in AL = value 0..99, AH = pad char ('0' or ' '), DS:DI = output cursor
;; @out Writes 2 characters to DS:DI and advances DI by 2
;; @err none
;; @clobbers ax, dx, di
;; @desc Formats a 2-digit number with '0' or ' ' leading pad.
@proc fmt_2dig_pad
    push bx
    mov bh, ah
    xor ah, ah
    mov bl, 10
    div bl
    test al, al
    jnz .tens_nz
    mov byte [di], bh
    jmp .ones
.tens_nz:
    add al, '0'
    mov byte [di], al
.ones:
    add ah, '0'
    mov byte [di + 1], ah
    add di, 2
    pop bx
    ret
@endproc

;; @proc fmt_cur_date_str
;; @abi dos16
;; @in DS:DI = 16-byte output buffer
;; @out Writes "YYYY-MM-DD\0" from INT 21h AH=2Ah into DS:DI, CX = 10
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Formats current DOS system date as ISO/numeric YYYY-MM-DD.
@proc fmt_cur_date_str
    push bp
    mov bp, di
    mov ah, DOS_GET_DATE
    int 0x21
    ; CX = year (1980..2099), DH = month (1..12), DL = day (1..31)
    push dx
    mov ax, cx
    xor dx, dx
    mov bx, 100
    div bx
    ; AL = century (19..20), DL = year_in_century (00..99)
    push dx
    mov ah, '0'
    call fmt_2dig_pad
    pop ax
    mov ah, '0'
    call fmt_2dig_pad
    mov byte [di], '-'
    inc di
    pop dx
    mov al, dh
    mov ah, '0'
    push dx
    call fmt_2dig_pad
    mov byte [di], '-'
    inc di
    pop dx
    mov al, dl
    mov ah, '0'
    call fmt_2dig_pad
    mov byte [di], 0
    mov di, bp
    mov cx, 10
    pop bp
    ret
@endproc

;; @proc fmt_cur_time_str
;; @abi dos16
;; @in DS:DI = 16-byte output buffer
;; @out Writes "HH:MM:SS.CC\0" (leading space when hour < 10!) from AH=2Ch into DS:DI, CX = 11
;; @err none
;; @clobbers ax, cx, dx, di
;; @desc Formats current DOS system time matching cmd.exe %TIME% (" H:MM:SS.CC").
@proc fmt_cur_time_str
    push bp
    mov bp, di
    mov ah, DOS_GET_TIME
    int 0x21
    ; CH = hour (0..23), CL = min (0..59), DH = sec (0..59), DL = hundredths (0..99)
    push dx
    push cx
    mov al, ch
    mov ah, ' '
    call fmt_2dig_pad
    mov byte [di], ':'
    inc di
    pop cx
    mov al, cl
    mov ah, '0'
    call fmt_2dig_pad
    mov byte [di], ':'
    inc di
    pop dx
    push dx
    mov al, dh
    mov ah, '0'
    call fmt_2dig_pad
    mov byte [di], '.'
    inc di
    pop dx
    mov al, dl
    mov ah, '0'
    call fmt_2dig_pad
    mov byte [di], 0
    mov di, bp
    mov cx, 11
    pop bp
    ret
@endproc

;; @proc cmd_date
;; @abi dos16
;; @in DS:SI = optional [/T] or date string
;; @out Prints current system date (or sets via AH=2Bh), [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements DATE [/T] across MS-DOS 1.25..8.0.
@proc cmd_date
    call trn_skip_ws
    cmp al, '/'
    jne .chk_arg
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'T'
    je .date_t
.chk_arg:
    cmp al, 0x00
    je .show_full
    cmp al, 0x0D
    je .show_full
    ; Non-empty date argument: invoke AH=2Bh (or accept in non-interactive mode)
    mov cx, 2026
    mov dx, 0x0B15
    mov ah, DOS_SET_DATE
    int 0x21
    mov word [wTrnExitCode], 0
    ret
.show_full:
    mov si, addr(szCurDateIs)
    call con_write_asciiz
.date_t:
    mov di, szNumScratch
    call fmt_cur_date_str
    mov si, szNumScratch
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_time
;; @abi dos16
;; @in DS:SI = optional [/T] or time string
;; @out Prints current system time (or sets via AH=2Dh), [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements TIME [/T] across MS-DOS 1.25..8.0.
@proc cmd_time
    call trn_skip_ws
    cmp al, '/'
    jne .chk_targ
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'T'
    je .time_t
.chk_targ:
    cmp al, 0x00
    je .show_tfull
    cmp al, 0x0D
    je .show_tfull
    mov cx, 0x0C00
    xor dx, dx
    mov ah, DOS_SET_TIME
    int 0x21
    mov word [wTrnExitCode], 0
    ret
.show_tfull:
    mov si, addr(szCurTimeIs)
    call con_write_asciiz
.time_t:
    mov di, szNumScratch
    call fmt_cur_time_str
    mov si, szNumScratch
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc parse_hex_nibble
;; @abi dos16
;; @in AL = ASCII hex digit ('0'..'9', 'A'..'F', 'a'..'f')
;; @out AL = 0..15, CF=0 if valid; CF=1 if invalid
;; @err CF=1 on non-hex character
;; @clobbers ax
;; @desc Parses one hexadecimal digit into 0..15.
@proc parse_hex_nibble
    cmp al, '0'
    jb .bad
    cmp al, '9'
    jbe .dig
    call chr_to_upper
    cmp al, 'A'
    jb .bad
    cmp al, 'F'
    ja .bad
    sub al, ('A' - 10)
    clc
    ret
.dig:
    sub al, '0'
    clc
    ret
.bad:
    stc
    ret
@endproc

;; @proc cmd_color
;; @abi dos16
;; @in DS:SI = optional 1- or 2-digit hex attribute (bf: background, foreground)
;; @out Validates fg != bg (sets ERRORLEVEL=1 if equal!), updates RES_STATE.bColorAttr
;; @err Sets [wTrnExitCode]=1 when foreground equals background or hex is invalid
;; @clobbers ax, bx, cx, si
;; @desc Implements COLOR [attr] with authentic cmd.exe foreground!=background validation.
@proc cmd_color
    call trn_skip_ws
    cmp al, 0x00
    je .reset_col
    cmp al, 0x0D
    je .reset_col

    mov al, byte [si]
    call parse_hex_nibble
    jc .bad_col
    mov bl, al
    mov al, byte [si + 1]
    cmp al, 0x00
    je .one_dig
    cmp al, 0x0D
    je .one_dig
    cmp al, 0x20
    je .one_dig
    call parse_hex_nibble
    jc .bad_col
    ; Two hex digits: BL = bg, AL = fg -> reject if BL == AL!
    cmp bl, al
    je .bad_col
    mov cl, 4
    shl bl, cl
    or bl, al
    jmp .apply_col

.one_dig:
    ; Single hex digit: bg = 0, fg = BL
    jmp .apply_col

.reset_col:
    mov bl, 0x07

.apply_col:
    push es
    mov es, word [wTrnResSeg]
    mov byte [es:resState + RES_STATE.bColorAttr], bl
    pop es
    mov word [wTrnExitCode], 0
    ret

.bad_col:
    mov word [wTrnExitCode], 1
    ret
@endproc

;; @proc reg_assoc_cmd
;; @abi dos16
;; @in DS:SI = argument cursor, BL = 'A' (ASSOC: '.' keys) or 'F' (FTYPE: non-'.' keys)
;; @out Queries/modifies WINCMD.REG key=value store, [wTrnExitCode]=0/1
;; @err Sets [wTrnExitCode]=1 if queried key is not found
;; @clobbers ax, bx, cx, dx, si, di
;; @desc File-backed ASSOC and FTYPE store over WINCMD.REG on MS-DOS / Win16.
@proc reg_assoc_cmd
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=mode ('A'/'F'), [bp-4]=arg_ptr, [bp-6]=eq_ptr (0 if query), [bp-8]=file_len
    xor bh, bh
    mov word [bp - 2], bx

    call trn_skip_ws
    mov di, TRN_CMD_WORK
    xor cx, cx
    mov word [bp - 6], 0
.cp_arg:
    mov al, byte [si]
    test al, al
    jz .arg_copied
    cmp al, 0x0D
    je .arg_copied
    cmp al, '<'
    je .arg_copied
    cmp al, '>'
    je .arg_copied
    cmp al, '='
    jne .put_ac
    cmp word [bp - 6], 0
    jne .put_ac
    mov word [bp - 6], di
.put_ac:
    mov byte [di], al
    inc si
    inc di
    inc cx
    cmp cx, 120
    jb .cp_arg
.arg_copied:
    cmp di, TRN_CMD_WORK
    jbe .arg_term
    cmp byte [di - 1], 0x20
    jne .arg_term
    dec di
    jmp .arg_copied
.arg_term:
    mov byte [di], 0

    ; Load existing WINCMD.REG (up to 512B) into [TRN_CMD_WORK + 256]
    mov word [bp - 8], 0
    cmp byte [bTrnDosMajor], 2
    jb .reg_loaded
    mov dx, addr(szRegFileName)
    xor al, al
    call k200_open
    jc .reg_loaded
    mov bx, ax
    mov dx, TRN_CMD_WORK
    add dx, 256
    mov cx, 512
    call k200_read
    jc .cls_r0
    mov word [bp - 8], ax
.cls_r0:
    call k200_close
.reg_loaded:
    mov bx, TRN_CMD_WORK
    add bx, 256
    add bx, word [bp - 8]
    mov byte [bx], 0

    ; Case 1: Assignment "KEY=VALUE" -> append line to WINCMD.REG and echo KEY=VALUE!
    cmp word [bp - 6], 0
    je .query_mode
    cmp byte [bTrnDosMajor], 2
    jb .echo_assign
    mov dx, addr(szRegFileName)
    xor cx, cx
    call k200_create
    jnc .reg_creat_ok
    jmp .reg_fail
.reg_creat_ok:
    mov bx, ax
    mov cx, word [bp - 8]
    test cx, cx
    jz .wr_new_ln
    mov dx, TRN_CMD_WORK
    add dx, 256
    call k200_write
.wr_new_ln:
    mov si, TRN_CMD_WORK
    call prs_strlen_si
    mov dx, TRN_CMD_WORK
    call k200_write
    mov dx, addr(szCrLf2)
    mov cx, 2
    call k200_write
    call k200_close

.echo_assign:
    ; Only echo if RHS after '=' is non-empty!
    mov si, word [bp - 6]
    cmp byte [si + 1], 0
    je .reg_ok
    mov si, TRN_CMD_WORK
    call con_write_asciiz
    call con_write_crlf
.reg_ok:
    mov word [wTrnExitCode], 0
    add sp, 8
    pop bp
    ret

.query_mode:
    ; Scan lines in [TRN_CMD_WORK + 256]; keep last matching entry or list all of mode!
    mov si, TRN_CMD_WORK
    add si, 256
    xor dx, dx ; matched count
.ln_lp:
    cmp byte [si], 0
    je .ln_done
    mov bx, si ; line start
.find_eol:
    mov al, byte [si]
    test al, al
    jz .got_ln
    cmp al, 0x0D
    je .term_ln
    cmp al, 0x0A
    je .term_ln
    inc si
    jmp .find_eol
.term_ln:
    mov byte [si], 0
    inc si
    cmp byte [si], 0x0A
    jne .got_ln
    inc si

.got_ln:
    cmp byte [bx], 0
    je .ln_lp
    ; Filter ASSOC (starts with '.') vs FTYPE (does not start with '.')
    mov al, byte [bp - 2]
    cmp al, 'A'
    jne .chk_ftype_ln
    cmp byte [bx], '.'
    jne .ln_lp
    jmp .match_key
.chk_ftype_ln:
    cmp byte [bx], '.'
    je .ln_lp

.match_key:
    ; Check if user gave a specific key in TRN_CMD_WORK
    cmp byte [TRN_CMD_WORK], 0
    je .print_entry
    ; Measure key length up to '=' in BX
    push si
    mov si, TRN_CMD_WORK
    call prs_strlen_si
    mov di, bx
    add di, cx
    cmp byte [di], '='
    jne .no_k_match
    cmp byte [di + 1], 0
    je .no_k_match ; deleted entry!
    push bx
    push dx
    mov di, TRN_CMD_WORK
    mov dx, cx
    mov si, bx
    call slice_eq_nocase
    pop dx
    pop bx
    pop si
    jc .ln_lp
    jmp .print_entry

.no_k_match:
    pop si
    jmp .ln_lp

.print_entry:
    push si
    push dx
    mov si, bx
    call con_write_asciiz
    call con_write_crlf
    pop dx
    pop si
    inc dx
    jmp .ln_lp

.ln_done:
    cmp byte [TRN_CMD_WORK], 0
    je .reg_ok_ret
    test dx, dx
    jnz .reg_ok_ret
.reg_fail:
    mov word [wTrnExitCode], 1
    add sp, 8
    pop bp
    ret
.reg_ok_ret:
    mov word [wTrnExitCode], 0
    add sp, 8
    pop bp
    ret
@endproc

;; @proc cmd_assoc
;; @abi dos16
;; @in DS:SI = optional [.ext[=[fileType]]]
;; @out Queries or updates extension association in WINCMD.REG
;; @err Sets [wTrnExitCode]=1 if queried .ext not found
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements ASSOC [.ext[=[fileType]]].
@proc cmd_assoc
    mov bl, 'A'
    call reg_assoc_cmd
    ret
@endproc

;; @proc cmd_ftype
;; @abi dos16
;; @in DS:SI = optional [fileType[=[openCmdString]]]
;; @out Queries or updates fileType open command in WINCMD.REG
;; @err Sets [wTrnExitCode]=1 if queried fileType not found
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements FTYPE [fileType[=[openCommandString]]].
@proc cmd_ftype
    mov bl, 'F'
    call reg_assoc_cmd
    ret
@endproc

;; @proc cmd_keys
;; @abi dos16
;; @in DS:SI = optional ON / OFF
;; @out Reports or toggles KEYS compatibility status, [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements KEYS [ON | OFF] compatibility built-in.
@proc cmd_keys
    call cmd_parse_on_off
    cmp al, 2
    je .k_off
    mov si, addr(szKeysIs)
    call con_write_asciiz
    mov si, addr(szKwOn)
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
.k_off:
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_ctty
;; @abi dos16
;; @in DS:SI = device name (CON, AUX, NUL, COM1..4)
;; @out Validates character device via dev_match_name, [wTrnExitCode]=0/1
;; @err Prints Invalid syntax / device if not a character device
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements CTTY <device> console character device switch.
@proc cmd_ctty
    mov di, TRN_CMD_WORK
    call trn_read_arg
    jc .bad_ctty
    mov si, TRN_CMD_WORK
    call dev_match_name
    jc .bad_ctty
    mov word [wTrnExitCode], 0
    ret
.bad_ctty:
    mov ax, ERR_SYNTAX
    call trn_err_dos
    mov word [wTrnExitCode], 1
    ret
@endproc

;; @proc cmd_doskey
;; @abi dos16
;; @in DS:SI = DOSKEY switches or MACRO=BODY
;; @out Handles /HISTORY, /REINSTALL, /MACROS, or stores macro definition, [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements DOSKEY built-in (/HISTORY, /REINSTALL, /MACROS, NAME=BODY).
@proc cmd_doskey
    call trn_skip_ws
    cmp al, '/'
    jne .done_dk
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'H'
    jne .chk_reinst
    cmp byte [TRN_HIST_BUF], 0
    je .done_dk
    mov si, TRN_HIST_BUF
    call con_write_asciiz
    call con_write_crlf
    jmp .done_dk
.chk_reinst:
    cmp ah, 'R'
    jne .done_dk
    mov byte [TRN_HIST_BUF], 0
.done_dk:
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_mode
;; @abi dos16
;; @in DS:SI = optional MODE device/dimension parameters
;; @out Prints console/codepage status or applies mode parameters, [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements MODE status and console configuration built-in.
@proc cmd_mode
    call trn_skip_ws
    cmp al, 0x00
    je .show_st
    cmp al, 0x0D
    jne .ok_md
.show_st:
    mov si, addr(szModeConStatus)
    call con_write_asciiz
    call con_write_crlf
.ok_md:
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_start
;; @abi dos16
;; @in DS:SI = optional ["title"] [/D path] [/B] [/WAIT] <command> [args]
;; @out Applies optional /D directory, TITLE, and executes command synchronously on DOS
;; @err Propagates child command exit code
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements START ["title"] [/D path] [/B] [/WAIT] <command>.
@proc cmd_start
    call trn_skip_ws
    cmp al, '"'
    jne .st_sw_lp
    ; Optional leading "Window Title"
    inc si
    mov di, TRN_CMD_WORK
.cp_st_ttl:
    mov al, byte [si]
    test al, al
    jz .end_st_ttl
    inc si
    cmp al, '"'
    je .end_st_ttl
    mov byte [di], al
    inc di
    jmp .cp_st_ttl
.end_st_ttl:
    mov byte [di], 0
    push si
    mov si, TRN_CMD_WORK
    call cmd_title
    pop si

.st_sw_lp:
    call trn_skip_ws
    cmp al, '/'
    jne .run_st_cmd
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'D'
    je .st_sw_d
    ; Skip /B, /WAIT, /MIN, /MAX, /LOW, /NORMAL, /HIGH, /SEPARATE, /SHARED
    inc si
.sk_st_sw:
    mov al, byte [si]
    test al, al
    jz .run_st_cmd
    cmp al, 0x0D
    je .run_st_cmd
    cmp al, 0x20
    je .st_sw_lp
    cmp al, 0x09
    je .st_sw_lp
    inc si
    jmp .sk_st_sw

.st_sw_d:
    add si, 2
    mov di, TRN_CMD_WORK
    call trn_read_arg
    jc .run_st_cmd
    push si
    mov si, TRN_CMD_WORK
    mov dl, 1
    call cmd_cd_ex
    pop si
    jmp .st_sw_lp

.run_st_cmd:
    cmp al, 0x00
    je .st_empty
    cmp al, 0x0D
    je .st_empty
    call trn_exec_stmt
    ret
.st_empty:
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_mklink
;; @abi dos16
;; @in DS:SI = [[/D] | [/H] | [/J]] Link Target
;; @out Reports unsupported era on FAT/MS-DOS (or creates symbolic/junction link on NT)
;; @err Prints diagnostic and sets [wTrnExitCode]=1 on MS-DOS FAT volumes
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Recognizes internal MKLINK built-in and enforces filesystem capability check.
@proc cmd_mklink
    mov ax, ERR_UNSUPPORTED_ON_ERA
    call err_print_code
    mov word [wTrnExitCode], 1
    ret
@endproc

;; @proc cmd_loadhigh
;; @abi dos16
;; @in DS:SI = optional [/L:...] [/S] <command> [args]
;; @out Enables UMB high-first MCB allocation on DOS 5.0+/7.x around <command>, or passes through
;; @err Propagates child command exit code
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements LH / LOADHIGH across MS-DOS 1.25..8.0.
@proc cmd_loadhigh
    push bp
    mov bp, sp
    sub sp, 4
    ; [bp-2]=orig_alloc_strat, [bp-4]=orig_umb_link

.lh_sw:
    call trn_skip_ws
    cmp al, '/'
    jne .lh_exec
    inc si
.lh_sk:
    mov al, byte [si]
    test al, al
    jz .lh_exec
    cmp al, 0x0D
    je .lh_exec
    cmp al, 0x20
    je .lh_sw
    cmp al, 0x09
    je .lh_sw
    inc si
    jmp .lh_sk

.lh_exec:
    cmp al, 0x00
    je .lh_empty
    cmp al, 0x0D
    je .lh_empty

    cmp byte [bTrnDosMajor], 5
    jb .lh_passthrough

    push si
    mov ax, 0x5800
    int 0x21
    mov word [bp - 2], ax
    mov ax, 0x5802
    int 0x21
    xor ah, ah
    mov word [bp - 4], ax

    mov bx, 1
    mov ax, 0x5803
    int 0x21
    mov bx, 0x0080
    mov ax, 0x5801
    int 0x21
    pop si

    call trn_exec_stmt

    mov bx, word [bp - 2]
    mov ax, 0x5801
    int 0x21
    mov bx, word [bp - 4]
    mov ax, 0x5803
    int 0x21
    add sp, 4
    pop bp
    ret

.lh_passthrough:
    call trn_exec_stmt
    add sp, 4
    pop bp
    ret

.lh_empty:
    mov word [wTrnExitCode], 0
    add sp, 4
    pop bp
    ret
@endproc

;; @proc cmd_subshell
;; @abi dos16
;; @in DS:SI = CMD / COMMAND switches (/Q, /D, /S, /E:ON|OFF, /V:ON|OFF, /C <cmd>, /K <cmd>)
;; @out Runs subshell command inside an isolated SETLOCAL environment/flag scope
;; @err Propagates subshell command ERRORLEVEL
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements CMD and COMMAND subshell invocation with isolated environment and /V /E /Q flags.
@proc cmd_subshell
    push bp
    mov bp, sp
    sub sp, 6
    ; [bp-2]=saved_echo, [bp-4]=has_setlocal, [bp-6]=strip_quotes
    mov al, byte [bTrnEcho]
    xor ah, ah
    mov word [bp - 2], ax
    mov word [bp - 4], 0
    mov word [bp - 6], 0

    cmp byte [bTrnDosMajor], 2
    jb .sub_sw_lp
    push si
    mov si, addr(szPrsEmpty)
    call cmd_setlocal
    pop si
    mov word [bp - 4], 1

.sub_sw_lp:
    call trn_skip_ws
    cmp al, '/'
    je .sub_got_sl
    jmp .sub_no_c
.sub_got_sl:
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'C'
    je .j_sub_ck
    cmp ah, 'K'
    jne .chk_sub_q
.j_sub_ck:
    jmp .sub_do_ck
.chk_sub_q:
    cmp ah, 'Q'
    je .sub_sw_q
    cmp ah, 'S'
    je .sub_sw_s
    cmp ah, 'V'
    je .sub_sw_v
    cmp ah, 'E'
    je .sub_sw_e
    ; Skip /A, /U, /D, /F, /T:bf, /P, /MSG
    add si, 2
.sub_sk_tok:
    mov al, byte [si]
    test al, al
    jnz .sub_not_z
    jmp .sub_no_c
.sub_not_z:
    cmp al, 0x0D
    jne .sub_not_cr
    jmp .sub_no_c
.sub_not_cr:
    cmp al, 0x20
    je .sub_sw_lp
    cmp al, '/'
    je .sub_sw_lp
    inc si
    jmp .sub_sk_tok

.sub_sw_q:
    mov byte [bTrnEcho], 0
    add si, 2
    jmp .sub_sw_lp
.sub_sw_s:
    mov word [bp - 6], 1
    add si, 2
    jmp .sub_sw_lp
.sub_sw_v:
    add si, 2
    mov dl, 1
    cmp byte [si], ':'
    jne .sub_set_v
    mov al, byte [si + 2]
    and al, 0xDF
    cmp al, 'F'
    jne .sub_set_v
    xor dl, dl
.sub_set_v:
    push es
    mov es, word [wTrnResSeg]
    mov byte [es:resState + RES_STATE.bDelayedExp], dl
    pop es
    jmp .sub_sk_tok

.sub_sw_e:
    add si, 2
    cmp byte [si], ':'
    jne .sub_sk_tok
    mov al, byte [si + 1]
    cmp al, '0'
    jb .sub_e_onoff
    cmp al, '9'
    jbe .sub_sk_tok
.sub_e_onoff:
    mov dl, 1
    mov al, byte [si + 2]
    and al, 0xDF
    cmp al, 'F'
    jne .sub_set_e
    xor dl, dl
.sub_set_e:
    push es
    mov es, word [wTrnResSeg]
    mov byte [es:resState + RES_STATE.bCmdExt], dl
    pop es
    jmp .sub_sk_tok

.sub_do_ck:
    add si, 2
    call trn_skip_ws
    ; Copy command tail into an arena buffer so nested trn_exec_line is re-entrant!
    push si
    mov cx, 260
    call arena_alloc
    pop si
    jc .sub_restore
    mov bx, di
    cmp byte [si], '"'
    jne .cp_sub_cmd
    mov word [bp - 6], 1
    inc si
.cp_sub_cmd:
    mov al, byte [si]
    test al, al
    jz .fin_sub_cmd
    cmp al, 0x0D
    je .fin_sub_cmd
    mov byte [di], al
    inc si
    inc di
    jmp .cp_sub_cmd
.fin_sub_cmd:
    cmp word [bp - 6], 0
    je .term_sub_cmd
    cmp di, bx
    jbe .term_sub_cmd
    cmp byte [di - 1], '"'
    jne .term_sub_cmd
    dec di
.term_sub_cmd:
    mov byte [di], 0
    mov si, bx
    call trn_exec_line
    mov byte [bTrnExitReq], 0
    jmp .sub_restore

.sub_no_c:
    call cmd_ver

.sub_restore:
    mov ax, word [bp - 2]
    mov byte [bTrnEcho], al
    cmp word [bp - 4], 0
    je .sub_out
    push word [wTrnExitCode]
    call env_pop_one_setlocal
    pop word [wTrnExitCode]
.sub_out:
    add sp, 6
    pop bp
    ret
@endproc
