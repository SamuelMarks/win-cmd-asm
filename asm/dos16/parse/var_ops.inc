;; @file var_ops.inc
;; @isa 8086
;; @desc Environment block lookup/mutation, dynamic vars, substring (~s,l), and replace (:a=b).

@cpu 8086
@format inc

;; @proc env_ensure_block
;; @abi dos16
;; @in [wTrnResSeg] -> RES_STATE.wEnvSeg
;; @out AX = writable environment segment (allocating/initializing wTrnEnvSeg if 0)
;; @err none
;; @clobbers ax, bx, cx, di
;; @desc Returns active environment segment, using transient fallback block on MS-DOS 1.25.
@proc env_ensure_block
    push es
    mov es, word [wTrnResSeg]
    mov ax, word [es:resState + RES_STATE.wEnvSeg]
    test ax, ax
    jnz .have_seg
    ; On MS-DOS 1.25 (wEnvSeg == 0), use 512B resident/transient env block at wTrnSeg:TRN_ENV_BUF
    mov ax, word [wTrnResSeg]
    mov ax, ds
    mov word [es:resState + RES_STATE.wEnvSeg], ax
.have_seg:
    pop es
    ret
@endproc

;; @proc env_get_var
;; @abi dos16
;; @in DS:SI = var name, CX = name length, DS:DI = 256-byte output value buffer
;; @out CX = value length written at DS:DI (NUL-terminated), CF=0 if defined; CF=1 if undefined
;; @err CF=1 when variable is neither in environment block nor a built-in dynamic variable
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Looks up KEY=VALUE in wEnvSeg case-insensitively, falling back to dynamic variables.
@proc env_get_var
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=name_ptr, [bp-4]=name_len, [bp-6]=out_buf, [bp-8]=env_seg
    mov word [bp - 2], si
    mov word [bp - 4], cx
    mov word [bp - 6], di

    test cx, cx
    jnz .non_empty_name
    jmp .not_found
.non_empty_name:

    call env_ensure_block
    mov word [bp - 8], ax
    mov dx, ds
    cmp ax, dx
    jne .scan_ext_seg
    mov bx, TRN_ENV_BUF
    jmp .start_env_scan
.scan_ext_seg:
    xor bx, bx

.start_env_scan:
    push es
    mov es, word [bp - 8]
.entry_lp:
    cmp byte [es:bx], 0
    je .end_env_blk

    ; Compare ES:BX against DS:[bp-2] of length [bp-4] followed by '='
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, bx
.cmp_ch:
    mov al, byte [es:di]
    cmp al, '='
    je .mismatch_entry
    test al, al
    jz .mismatch_entry
    call chr_to_upper
    mov dl, al
    mov al, byte [si]
    call chr_to_upper
    cmp al, dl
    jne .mismatch_entry
    inc si
    inc di
    loop .cmp_ch

    cmp byte [es:di], '='
    jne .mismatch_entry
    inc di

    ; Found! Copy value from ES:DI into DS:[bp-6]
    mov bx, di
    mov di, word [bp - 6]
    xor cx, cx
.cp_val:
    mov al, byte [es:bx]
    test al, al
    jz .val_done
    cmp cx, 250
    jae .val_done
    mov byte [di], al
    inc bx
    inc di
    inc cx
    jmp .cp_val
.val_done:
    mov byte [di], 0
    pop es
    add sp, 8
    pop bp
    clc
    ret

.mismatch_entry:
    cmp byte [es:bx], 0
    je .nx_entry
    inc bx
    jmp .mismatch_entry
.nx_entry:
    inc bx
    jmp .entry_lp

.end_env_blk:
    pop es

    ; --- Synthesize Unshadowed Dynamic Variables ---
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynErrLvl)
    mov dx, 10
    call slice_eq_nocase
    jc .try_dyn_cd
    jmp .dyn_errorlevel
.try_dyn_cd:

    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynCd)
    mov dx, 2
    call slice_eq_nocase
    jc .try_dyn_extv
    jmp .dyn_cd
.try_dyn_extv:

    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynExtVer)
    mov dx, 13
    call slice_eq_nocase
    jc .try_dyn_rnd
    jmp .dyn_extver
.try_dyn_rnd:

    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynRandom)
    mov dx, 6
    call slice_eq_nocase
    jc .try_dyn_date
    jmp .dyn_random

.try_dyn_date:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynDate)
    mov dx, 4
    call slice_eq_nocase
    jc .try_dyn_time
    jmp .dyn_date

.try_dyn_time:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynTime)
    mov dx, 4
    call slice_eq_nocase
    jc .try_dyn_cmdln
    jmp .dyn_time

.try_dyn_cmdln:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynCmdLine)
    mov dx, 10
    call slice_eq_nocase
    jc .try_dyn_numa
    jmp .dyn_cmdline

.try_dyn_numa:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynNuma)
    mov dx, 21
    call slice_eq_nocase
    jc .try_dyn_eqhex
    jmp .dyn_numa

.try_dyn_eqhex:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynExitHex)
    mov dx, 9
    call slice_eq_nocase
    jc .try_dyn_eqasc
    jmp .dyn_exit_hex

.try_dyn_eqasc:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, addr(szDynExitAsc)
    mov dx, 14
    call slice_eq_nocase
    jc .try_dyn_eqdrv
    jmp .dyn_exit_asc

.try_dyn_eqdrv:
    ; Check '=A:' .. '=Z:' (3-byte name starting with '=' and ending with ':')
    cmp word [bp - 4], 3
    jne .not_found
    mov si, word [bp - 2]
    cmp byte [si], '='
    jne .not_found
    cmp byte [si + 2], ':'
    jne .not_found
    mov al, byte [si + 1]
    call chr_to_upper
    cmp al, 'A'
    jb .not_found
    cmp al, 'Z'
    ja .not_found
    jmp .dyn_eq_drv

.not_found:
    mov di, word [bp - 6]
    mov byte [di], 0
    xor cx, cx
    add sp, 8
    pop bp
    stc
    ret

.dyn_errorlevel:
    mov ax, word [wTrnExitCode]
    xor dx, dx
    mov di, word [bp - 6]
    call fmt_i32_dec
    ; Ensure DI == [bp-6]
    cmp di, word [bp - 6]
    je .el_ok
    mov si, di
    mov di, word [bp - 6]
    push cx
    inc cx
    rep movsb
    pop cx
.el_ok:
    add sp, 8
    pop bp
    clc
    ret

.dyn_cd:
    mov di, word [bp - 6]
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [di], al
    mov byte [di + 1], ':'
    cmp byte [bTrnDosMajor], 2
    jb .cd_125
    mov byte [di + 2], 0x5C
    mov si, di
    add si, 3
    xor dl, dl
    call k700_getcwd
    mov si, word [bp - 6]
    xor cx, cx
.cd_len:
    cmp byte [si], 0
    je .cd_done
    inc si
    inc cx
    jmp .cd_len
.cd_125:
    mov byte [di + 2], 0
    mov cx, 2
.cd_done:
    add sp, 8
    pop bp
    clc
    ret

.dyn_extver:
    mov di, word [bp - 6]
    mov byte [di], '2'
    mov byte [di + 1], 0
    mov cx, 1
    add sp, 8
    pop bp
    clc
    ret

.dyn_random:
    ; Mix wRandSeed with DOS clock ticks (AH=2Ch) -> 0..32767
    mov ah, DOS_GET_TIME
    int 0x21
    mov ax, word [wTrnRandSeed]
    add ax, dx
    add ax, cx
    mov bx, 25173
    mul bx
    add ax, 13849
    mov word [wTrnRandSeed], ax
    and ax, 0x7FFF
    xor dx, dx
    mov di, word [bp - 6]
    call fmt_i32_dec
    add sp, 8
    pop bp
    clc
    ret

.dyn_date:
    mov di, word [bp - 6]
    call fmt_cur_date_str
    add sp, 8
    pop bp
    clc
    ret

.dyn_time:
    mov di, word [bp - 6]
    call fmt_cur_time_str
    add sp, 8
    pop bp
    clc
    ret

.dyn_numa:
    mov di, word [bp - 6]
    mov byte [di], '0'
    mov byte [di + 1], 0
    mov cx, 1
    add sp, 8
    pop bp
    clc
    ret

.dyn_cmdline:
    mov di, word [bp - 6]
    push es
    mov es, word [wTrnResSeg]
    mov es, word [es:resState + RES_STATE.wPspSeg]
    xor ch, ch
    mov cl, byte [es:0x0080]
    cmp cl, 126
    jbe .cmdl_ok
    mov cl, 126
.cmdl_ok:
    mov si, 0x0081
    xor dx, dx
.cmdl_lp:
    test cx, cx
    jz .cmdl_dn
    mov al, byte [es:si]
    cmp al, 0x0D
    je .cmdl_dn
    mov byte [di], al
    inc si
    inc di
    inc dx
    dec cx
    jmp .cmdl_lp
.cmdl_dn:
    pop es
    mov byte [di], 0
    mov cx, dx
    add sp, 8
    pop bp
    clc
    ret

.dyn_exit_hex:
    mov di, word [bp - 6]
    mov byte [di], '0'
    mov byte [di + 1], '0'
    mov byte [di + 2], '0'
    mov byte [di + 3], '0'
    add di, 4
    mov dx, word [wTrnExitCode]
    mov cx, 4
.eh_lp:
    push cx
    mov cl, 4
    rol dx, cl
    pop cx
    mov al, dl
    and al, 0x0F
    cmp al, 10
    jb .eh_dig
    add al, ('A' - 10)
    jmp .eh_put
.eh_dig:
    add al, '0'
.eh_put:
    mov byte [di], al
    inc di
    loop .eh_lp
    mov byte [di], 0
    mov cx, 8
    add sp, 8
    pop bp
    clc
    ret

.dyn_exit_asc:
    mov ax, word [wTrnExitCode]
    cmp al, 0x20
    jb .not_found_near
    cmp al, 0x7E
    ja .not_found_near
    mov di, word [bp - 6]
    mov byte [di], al
    mov byte [di + 1], 0
    mov cx, 1
    add sp, 8
    pop bp
    clc
    ret
.not_found_near:
    jmp .not_found

.dyn_eq_drv:
    ; AL = uppercase drive letter 'A'..'Z'
    mov di, word [bp - 6]
    mov byte [di], al
    mov byte [di + 1], ':'
    mov byte [di + 2], 0x5C
    mov byte [di + 3], 0
    cmp byte [bTrnDosMajor], 2
    jb .eqd_125
    mov dl, al
    sub dl, 'A'
    inc dl
    mov si, di
    add si, 3
    call k700_getcwd
    mov si, word [bp - 6]
    call prs_strlen_si
    add sp, 8
    pop bp
    clc
    ret
.eqd_125:
    mov byte [di + 2], 0
    mov cx, 2
    add sp, 8
    pop bp
    clc
    ret
@endproc

;; @proc env_set_var
;; @abi dos16
;; @in DS:SI = name, CX = name_len, DS:DX = ASCIIZ value (empty string deletes variable!)
;; @out Updates or deletes KEY=VALUE inside active environment block, CF=0
;; @err CF=1, AX=ERR_ENV_FULL if environment block capacity exceeded
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Mutates the live environment block in-place (preserving double-NUL terminator).
@proc env_set_var
    push bp
    push es
    mov bp, sp
    sub sp, 10
    ; [bp-2]=name_ptr, [bp-4]=name_len, [bp-6]=val_ptr, [bp-8]=env_seg, [bp-10]=base_off
    mov word [bp - 2], si
    mov word [bp - 4], cx
    mov word [bp - 6], dx

    call env_ensure_block
    mov word [bp - 8], ax
    mov es, ax
    mov word [bp - 10], 0
    mov cx, ds
    cmp ax, cx
    jne .find_existing
    mov word [bp - 10], TRN_ENV_BUF

.find_existing:
    mov bx, word [bp - 10]
.rm_lp:
    cmp byte [es:bx], 0
    je .append_check
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, bx
.rm_cmp:
    mov al, byte [es:di]
    cmp al, '='
    je .rm_skip
    test al, al
    jz .rm_skip
    call chr_to_upper
    mov dl, al
    mov al, byte [si]
    call chr_to_upper
    cmp al, dl
    jne .rm_skip
    inc si
    inc di
    loop .rm_cmp
    cmp byte [es:di], '='
    jne .rm_skip

    ; Delete matched entry at ES:BX by shifting subsequent entries down!
    mov si, di
.find_entry_end:
    cmp byte [es:si], 0
    je .got_entry_end
    inc si
    jmp .find_entry_end
.got_entry_end:
    inc si
    mov di, bx
.compact_lp:
    mov al, byte [es:si]
    mov byte [es:di], al
    inc si
    inc di
    test al, al
    jnz .compact_lp
    ; Copy second NUL if end of block, else continue compacting!
    cmp byte [es:si], 0
    jne .compact_lp
    mov byte [es:di], 0
    jmp .append_at_end

.rm_skip:
    cmp byte [es:bx], 0
    je .rm_nx
    inc bx
    jmp .rm_skip
.rm_nx:
    inc bx
    jmp .rm_lp

.append_at_end:
    mov bx, word [bp - 10]
.seek_end:
    cmp byte [es:bx], 0
    je .append_check
.sk_e:
    inc bx
    cmp byte [es:bx - 1], 0
    jne .sk_e
    jmp .seek_end

.append_check:
    ; BX now points to trailing NUL of environment block. If value is empty, we are done!
    mov si, word [bp - 6]
    cmp byte [si], 0
    je .ensure_dbl_nul

    ; Measure value length
    xor dx, dx
.vlen:
    cmp byte [si], 0
    je .chk_cap
    inc si
    inc dx
    jmp .vlen
.chk_cap:
    ; Check capacity (512B for TRN_ENV_BUF, 512B default env MCB)
    mov ax, bx
    sub ax, word [bp - 10]
    add ax, word [bp - 4]
    add ax, dx
    add ax, 3
    cmp ax, 508
    ja .env_full

    ; Write NAME=VALUE\0\0 at ES:BX
    mov di, bx
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    rep movsb
    mov byte [es:di], '='
    inc di
    mov si, word [bp - 6]
    mov cx, dx
    test cx, cx
    jz .write_nuls
    rep movsb
.write_nuls:
    mov byte [es:di], 0
    mov byte [es:di + 1], 0
    jmp .set_ok

.ensure_dbl_nul:
    mov byte [es:bx], 0
    mov byte [es:bx + 1], 0
.set_ok:
    add sp, 10
    pop es
    pop bp
    clc
    ret

.env_full:
    add sp, 10
    pop es
    pop bp
    mov ax, ERR_ENV_FULL
    stc
    ret
@endproc

;; @proc var_apply_ops
;; @abi dos16
;; @in DS:SI = raw value (len CX), DS:BX = operator spec after ':' (len DX), DS:DI = 256B out
;; @out DS:DI = transformed NUL-terminated string, CX = out length
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Evaluates ~start[,len], old=new, and *old=new variable transformations.
@proc var_apply_ops
    push bp
    mov bp, sp
    sub sp, 18
    ; [bp-2]=val_ptr, [bp-4]=val_len, [bp-6]=op_ptr, [bp-8]=op_len, [bp-10]=out_buf
    mov word [bp - 2], si
    mov word [bp - 4], cx
    mov word [bp - 6], bx
    mov word [bp - 8], dx
    mov word [bp - 10], di

    test dx, dx
    jz .copy_unchanged
    cmp byte [bx], '~'
    jne .do_replace

    ; --- Substring %VAR:~start[,len]% ---
    mov si, bx
    inc si
    call var_parse_signed16
    ; AX = start offset (if negative, add val_len!)
    test ax, ax
    jns .start_nonneg
    add ax, word [bp - 4]
    jns .start_nonneg
    xor ax, ax
.start_nonneg:
    cmp ax, word [bp - 4]
    jae .empty_out
    mov word [bp - 12], ax

    ; Default length = val_len - start
    mov cx, word [bp - 4]
    sub cx, ax
    cmp byte [si], ','
    jne .emit_substr
    inc si
    call var_parse_signed16
    test ax, ax
    jns .len_pos
    ; Negative len means (val_len - start) + len!
    add ax, cx
    js .empty_out
.len_pos:
    cmp ax, cx
    jae .emit_substr
    mov cx, ax

.emit_substr:
    mov si, word [bp - 2]
    add si, word [bp - 12]
    mov di, word [bp - 10]
    push cx
    test cx, cx
    jz .sub_term
    rep movsb
.sub_term:
    mov byte [di], 0
    pop cx
    mov di, word [bp - 10]
    add sp, 18
    pop bp
    ret

.empty_out:
    mov di, word [bp - 10]
    mov byte [di], 0
    xor cx, cx
    add sp, 18
    pop bp
    ret

.copy_unchanged:
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov di, word [bp - 10]
    push cx
    test cx, cx
    jz .unc_term
    rep movsb
.unc_term:
    mov byte [di], 0
    pop cx
    mov di, word [bp - 10]
    add sp, 18
    pop bp
    ret

.do_replace:
    ; --- Search & Replace %VAR:old=new% or %VAR:*old=new% ---
    mov si, word [bp - 6]
    mov cx, word [bp - 8]
    mov byte [bp - 14], 0
    cmp byte [si], '*'
    jne .find_eq
    mov byte [bp - 14], 1
    inc si
    dec cx
.find_eq:
    mov word [bp - 6], si
    xor dx, dx
.eq_lp:
    test cx, cx
    jz .copy_unchanged
    cmp byte [si], '='
    je .got_eq
    inc si
    inc dx
    dec cx
    jmp .eq_lp
.got_eq:
    ; [bp-6] = old_ptr, DX = old_len; SI+1 = new_ptr, CX-1 = new_len
    test dx, dx
    jz .copy_unchanged
    inc si
    dec cx
    mov word [bp - 12], dx ; old_len
    mov word [bp - 16], si ; new_ptr
    mov word [bp - 18], cx ; new_len

    mov si, word [bp - 2]  ; cur in val
    mov cx, word [bp - 4]  ; rem in val
    mov di, word [bp - 10] ; out cursor

.rep_scan:
    cmp cx, word [bp - 12]
    jb .rep_tail

    ; Compare DS:SI against [bp-6] of length [bp-12] case-insensitively
    push si
    push cx
    push di
    mov di, word [bp - 6]
    mov cx, word [bp - 12]
    mov dx, cx
    call slice_eq_nocase
    pop di
    pop cx
    pop si
    jc .rep_no_match

    ; Matched! If *old=new mode ([bp-14]==1), reset DI to [bp-10] (discarding prefix) once!
    cmp byte [bp - 14], 0
    je .emit_new_tok
    mov di, word [bp - 10]
    push si
    push cx
    mov si, word [bp - 16]
    mov cx, word [bp - 18]
    test cx, cx
    jz .star_rest
    rep movsb
.star_rest:
    pop cx
    pop si
    add si, word [bp - 12]
    sub cx, word [bp - 12]
    jmp .rep_tail

.emit_new_tok:
    push si
    push cx
    mov si, word [bp - 16]
    mov cx, word [bp - 18]
    test cx, cx
    jz .skip_new_cp
    rep movsb
.skip_new_cp:
    pop cx
    pop si
    add si, word [bp - 12]
    sub cx, word [bp - 12]
    jmp .rep_scan

.rep_no_match:
    mov al, byte [si]
    mov byte [di], al
    inc di
    inc si
    dec cx
    jmp .rep_scan

.rep_tail:
    test cx, cx
    jz .rep_done
    rep movsb
.rep_done:
    mov byte [di], 0
    mov cx, di
    sub cx, word [bp - 10]
    mov di, word [bp - 10]
    add sp, 18
    pop bp
    ret
@endproc

;; @proc var_parse_signed16
;; @abi dos16
;; @in DS:SI = decimal/hex/octal integer cursor
;; @out AX = signed 16-bit value, SI advanced
;; @err none
;; @clobbers ax, bx, dx, si
;; @desc Parses a signed 16-bit integer for %VAR:~start,len% substring slicing.
@proc var_parse_signed16
    xor dx, dx
    cmp byte [si], '-'
    jne .chk_p
    mov dx, 1
    inc si
    jmp .digits
.chk_p:
    cmp byte [si], '+'
    jne .digits
    inc si
.digits:
    xor ax, ax
.d_lp:
    mov bl, byte [si]
    cmp bl, '0'
    jb .done
    cmp bl, '9'
    ja .done
    sub bl, '0'
    xor bh, bh
    push dx
    mov dx, 10
    mul dx
    pop dx
    add ax, bx
    inc si
    jmp .d_lp
.done:
    test dx, dx
    jz .ret_v
    neg ax
.ret_v:
    ret
@endproc

@data db szDynErrLvl, "ERRORLEVEL"
@data db szDynCd, "CD"
@data db szDynExtVer, "CMDEXTVERSION"
@data db szDynRandom, "RANDOM"
@data db szDynDate, "DATE"
@data db szDynTime, "TIME"
@data db szDynCmdLine, "CMDCMDLINE"
@data db szDynNuma, "HIGHESTNUMANODENUMBER"
@data db szDynExitHex, "=ExitCode"
@data db szDynExitAsc, "=ExitCodeAscii"
