;; @file modifier.inc
;; @isa 8086
;; @desc Parameter & FOR modifier evaluator (%~[f|d|p|n|x|s|a|t|z|$VAR:]0..9 / %%~I).

@cpu 8086
@format inc

;; @const MOD_STRIP
;; @desc Bit 0 of BL: ~ quote-strip active.
@const MOD_STRIP = 0x01
;; @const MOD_FULL
;; @desc Bit 1 of BL: f full canonical path.
@const MOD_FULL = 0x02
;; @const MOD_DRV
;; @desc Bit 2 of BL: d drive letter with colon.
@const MOD_DRV = 0x04
;; @const MOD_PATH
;; @desc Bit 3 of BL: p directory path with leading/trailing backslash.
@const MOD_PATH = 0x08
;; @const MOD_NAME
;; @desc Bit 4 of BL: n filename stem without extension.
@const MOD_NAME = 0x10
;; @const MOD_EXT
;; @desc Bit 5 of BL: x filename extension including leading dot.
@const MOD_EXT = 0x20
;; @const MOD_SHORT
;; @desc Bit 6 of BL: s 8.3 short alias path.
@const MOD_SHORT = 0x40
;; @const MOD_ATTR
;; @desc Bit 7 of BL: a '--ra----' file attribute flags.
@const MOD_ATTR = 0x80

;; @proc mod_parse_flags
;; @abi dos16
;; @in DS:SI = cursor right after '~'
;; @out BX = parsed modifier bitmask (BL = MOD_*, BH: bit0=t, bit1=z, bit2=$VAR),
;; @out DX = $VAR name pointer (len in DH if bit2 set), DS:SI = target char ('0'..'9' or FOR var), CF=0
;; @err CF=1 if invalid modifier syntax (caller treats %~... literally)
;; @clobbers ax, bx, cx, dx, si
;; @desc Parses a modifier chain after '~' up to the parameter digit or FOR loop variable.
@proc mod_parse_flags
    mov bx, MOD_STRIP
    xor dx, dx
.m_lp:
    mov al, byte [si]
    test al, al
    jz .j_bad_m
    cmp al, '0'
    jb .chk_dlr
    cmp al, '9'
    jbe .j_done_m
.chk_dlr:
    cmp al, '$'
    je .parse_dollar_var

    ; If [si+1] is NUL or space, AL is the FOR variable char itself (not a modifier)!
    cmp byte [si + 1], 0
    je .j_done_m
    cmp byte [si + 1], 0x20
    je .j_done_m

    mov ah, al
    and ah, 0xDF
    cmp ah, 'F'
    je .is_f
    cmp ah, 'D'
    je .is_d
    cmp ah, 'P'
    je .is_p
    cmp ah, 'N'
    je .is_n
    cmp ah, 'X'
    je .is_x
    cmp ah, 'S'
    je .is_s
    cmp ah, 'A'
    je .is_a
    cmp ah, 'T'
    je .is_t
    cmp ah, 'Z'
    je .is_z
.j_done_m:
    jmp .done_ok
.j_bad_m:
    jmp .bad_mod

.is_f:
    or bl, (MOD_FULL + MOD_DRV + MOD_PATH + MOD_NAME + MOD_EXT)
    jmp .adv_m
.is_d:
    or bl, MOD_DRV
    jmp .adv_m
.is_p:
    or bl, MOD_PATH
    jmp .adv_m
.is_n:
    or bl, MOD_NAME
    jmp .adv_m
.is_x:
    or bl, MOD_EXT
    jmp .adv_m
.is_s:
    or bl, MOD_SHORT
    jmp .adv_m
.is_a:
    or bl, MOD_ATTR
    jmp .adv_m
.is_t:
    or bh, 0x01
    jmp .adv_m
.is_z:
    or bh, 0x02
.adv_m:
    inc si
    jmp .m_lp

.parse_dollar_var:
    inc si
    mov dx, si
    xor cl, cl
.dlr_lp:
    mov al, byte [si]
    test al, al
    jz .bad_mod
    cmp al, ':'
    je .got_dlr_colon
    inc si
    inc cl
    cmp cl, 30
    ja .bad_mod
    jmp .dlr_lp
.got_dlr_colon:
    test cl, cl
    jz .bad_mod
    inc si
    or bh, 0x04
    mov byte [bModDlrLen], cl
    jmp .m_lp

.done_ok:
    clc
    ret
.bad_mod:
    stc
    ret
@endproc

;; @proc mod_eval_value
;; @abi dos16
;; @in DS:SI = raw value ASCIIZ, BX = modifier mask, DX = $VAR ptr, DS:DI = 256B output buf
;; @out DS:DI = formatted modifier result (NUL-terminated), CX = length
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Strips quotes and formats path/attr/time/size/$PATH components into DS:DI.
@proc mod_eval_value
    push bp
    mov bp, sp
    sub sp, 16
    ; [bp-2]=out_buf, [bp-4]=mod_bx, [bp-6]=dlr_ptr, [bp-8]=clean_buf, [bp-10]=full_buf
    ; [bp-12]=last_slash_off, [bp-14]=last_dot_off, [bp-16]=out_cur
    mov word [bp - 2], di
    mov word [bp - 16], di
    mov word [bp - 4], bx
    mov word [bp - 6], dx

    push si
    mov cx, 320
    call arena_alloc
    pop si
    jnc .mod_mem_ok
    mov di, word [bp - 2]
    mov byte [di], 0
    xor cx, cx
    add sp, 16
    pop bp
    ret
.mod_mem_ok:
    mov word [bp - 8], di
    add di, 140
    mov word [bp - 10], di

    ; 1. Strip surrounding double quotes from DS:SI into [bp-8]
    cmp byte [si], '"'
    jne .cp_unq
    inc si
.cp_unq:
    mov di, word [bp - 8]
    xor cx, cx
.unq_lp:
    mov al, byte [si]
    test al, al
    jz .unq_done
    cmp cx, 130
    jae .unq_done
    mov byte [di], al
    inc si
    inc di
    inc cx
    jmp .unq_lp
.unq_done:
    cmp cx, 0
    je .unq_term
    cmp byte [di - 1], '"'
    jne .unq_term
    dec di
    dec cx
.unq_term:
    mov byte [di], 0

    ; If ONLY MOD_STRIP (0x0001) is set, copy [bp-8] directly to [bp-2] and return!
    cmp word [bp - 4], MOD_STRIP
    jne .resolve_path
    mov si, word [bp - 8]
    mov di, word [bp - 2]
    push cx
    inc cx
    rep movsb
    pop cx
    mov di, word [bp - 2]
    add sp, 16
    pop bp
    ret

.resolve_path:
    ; 2. Handle $VAR: search if BH bit 2 set
    test byte [bp - 3], 0x04
    jnz .do_dlr_search
    jmp .canon_full
.do_dlr_search:
    mov si, word [bp - 6]
    xor ch, ch
    mov cl, byte [bModDlrLen]
    mov di, word [bp - 10]
    call env_get_var
    jc .empty_mod_out
    ; Search semicolon-separated directories in [bp-10] for [bp-8]
    mov si, word [bp - 10]
.path_seg_lp:
    cmp byte [si], 0
    je .empty_mod_out
    mov di, szNumScratch ; temporary candidate? Use arena!
    push si
    mov cx, 140
    call arena_alloc
    pop si
    jc .empty_mod_out
    mov dx, di
.cp_dir_seg:
    mov al, byte [si]
    test al, al
    jz .end_dir_seg
    cmp al, ';'
    je .semi_dir_seg
    cmp al, '/'
    jne .keep_ds_ch
    mov al, 0x5C
.keep_ds_ch:
    mov byte [di], al
    inc si
    inc di
    jmp .cp_dir_seg
.semi_dir_seg:
    inc si
.end_dir_seg:
    cmp di, dx
    je .app_leaf
    cmp byte [di - 1], 0x5C
    je .app_leaf
    mov byte [di], 0x5C
    inc di
.app_leaf:
    push si
    mov si, word [bp - 8]
.cp_leaf_s:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_leaf_s
    pop si

    ; Probe if candidate at DX exists!
    push si
    push dx
    cmp byte [bTrnDosMajor], 2
    jb .probe_125
    xor al, al
    call k200_open
    jc .not_in_seg
    mov bx, ax
    call k200_close
    jmp .found_in_seg
.probe_125:
    mov si, dx
    mov cx, 64
    call arena_alloc
    jc .not_in_seg
    call k125_open
    jc .not_in_seg
    call k125_close
.found_in_seg:
    pop dx
    pop si
    mov word [bp - 8], dx
    jmp .canon_full
.not_in_seg:
    pop dx
    pop si
    jmp .path_seg_lp

.empty_mod_out:
    mov di, word [bp - 2]
    mov byte [di], 0
    xor cx, cx
    add sp, 16
    pop bp
    ret

.canon_full:
    ; 3. Build canonical full path "D:\DIR\FILE.EXT" into [bp-10]
    mov si, word [bp - 8]
    mov di, word [bp - 10]
    cmp byte [bTrnDosMajor], 7
    jb .canon_pre7
    mov cl, 0
    test byte [bp - 4], MOD_SHORT
    jz .do_700_tn
    mov cl, 1
.do_700_tn:
    call k700_truename
    jnc .got_full_path

.canon_pre7:
    mov si, word [bp - 8]
    mov di, word [bp - 10]
    cmp byte [si + 1], ':'
    jne .def_drv_mod
    mov al, byte [si]
    call chr_to_upper
    mov byte [di], al
    mov byte [di + 1], ':'
    add si, 2
    add di, 2
    jmp .chk_abs_mod
.def_drv_mod:
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [di], al
    mov byte [di + 1], ':'
    add di, 2

.chk_abs_mod:
    cmp byte [bTrnDosMajor], 2
    jb .cp_rest_norm
    cmp byte [si], 0x5C
    je .cp_rest_norm
    cmp byte [si], '/'
    je .cp_rest_norm
    mov byte [di], 0x5C
    inc di
    push si
    mov si, di
    xor dl, dl
    call k700_getcwd
.sk_cwd_m:
    cmp byte [di], 0
    je .cwd_m_end
    inc di
    jmp .sk_cwd_m
.cwd_m_end:
    cmp byte [di - 1], 0x5C
    je .pop_si_m
    mov byte [di], 0x5C
    inc di
.pop_si_m:
    pop si

.cp_rest_norm:
    cmp byte [bTrnDosMajor], 2
    jae .norm_lp
    mov byte [di], 0x5C
    inc di
.norm_lp:
    mov al, byte [si]
    cmp al, '/'
    jne .not_fsl
    mov al, 0x5C
.not_fsl:
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .norm_lp

.got_full_path:
    ; 4. Emit requested metadata prefixes: a (attr), t (time), z (size)
    test byte [bp - 4], MOD_ATTR
    jz .chk_mod_time
    mov dx, word [bp - 8]
    mov cl, 0x20
    cmp byte [bTrnDosMajor], 2
    jb .fmt_attr_str
    xor al, al
    mov ah, DOS_CHMOD
    int 0x21
    jnc .fmt_attr_str
    xor cx, cx
.fmt_attr_str:
    mov di, word [bp - 16]
    mov byte [di], '-'
    test cl, ATTR_DIRECTORY
    jz .a_r
    mov byte [di], 'd'
.a_r:
    mov byte [di + 1], '-'
    test cl, ATTR_READONLY
    jz .a_a
    mov byte [di + 1], 'r'
.a_a:
    mov byte [di + 2], '-'
    test cl, ATTR_ARCHIVE
    jz .a_h
    mov byte [di + 2], 'a'
.a_h:
    mov byte [di + 3], '-'
    test cl, ATTR_HIDDEN
    jz .a_s
    mov byte [di + 3], 'h'
.a_s:
    mov byte [di + 4], '-'
    test cl, ATTR_SYSTEM
    jz .a_pad
    mov byte [di + 4], 's'
.a_pad:
    mov byte [di + 5], '-'
    mov byte [di + 6], '-'
    mov byte [di + 7], '-'
    mov byte [di + 8], '-'
    mov byte [di + 9], ' '
    add word [bp - 16], 10

.chk_mod_time:
    test byte [bp - 3], 0x01
    jz .chk_mod_size
    mov si, addr(szDefModTime)
    mov di, word [bp - 16]
    mov cx, 17
    rep movsb
    mov word [bp - 16], di

.chk_mod_size:
    test byte [bp - 3], 0x02
    jz .split_path_parts
    ; Query file byte size via k200_find_first or k125_open
    xor ax, ax
    xor dx, dx
    cmp byte [bTrnDosMajor], 2
    jb .sz_125
    mov cx, 64
    call arena_alloc
    jc .emit_sz_num
    mov dx, word [bp - 8]
    mov cx, 0x0007
    push di
    call k200_find_first
    pop bx
    jc .sz_zero
    mov ax, word [bx + DTA_FIND.dwSize]
    mov dx, word [bx + DTA_FIND.dwSize + 2]
    jmp .emit_sz_num
.sz_125:
    mov cx, 64
    call arena_alloc
    jc .sz_zero
    mov si, word [bp - 8]
    push di
    call k125_open
    pop bx
    jc .sz_zero
    mov ax, word [bx + XFCB.fcb + FCB.dwFileSize]
    mov dx, word [bx + XFCB.fcb + FCB.dwFileSize + 2]
    push ax
    push dx
    mov di, bx
    call k125_close
    pop dx
    pop ax
    jmp .emit_sz_num
.sz_zero:
    xor ax, ax
    xor dx, dx
.emit_sz_num:
    mov di, szNumScratch
    call fmt_i32_dec
    mov si, di
    mov di, word [bp - 16]
    rep movsb
    mov byte [di], ' '
    inc di
    mov word [bp - 16], di

.split_path_parts:
    ; If none of f/d/p/n/x/s are set (e.g. only %~z1 or %~a1), trim trailing space & finish!
    mov al, byte [bp - 4]
    and al, (MOD_FULL + MOD_DRV + MOD_PATH + MOD_NAME + MOD_EXT + MOD_SHORT)
    jnz .do_path_slices
    mov di, word [bp - 16]
    cmp di, word [bp - 2]
    jbe .j_fin_mod
    cmp byte [di - 1], ' '
    jne .j_fin_mod
    dec di
    mov word [bp - 16], di
.j_fin_mod:
    jmp .fin_mod

.do_path_slices:
    ; If ONLY MOD_SHORT is set without d/p/n/x, treat as f + s!
    cmp al, MOD_SHORT
    jne .find_slashes
    or byte [bp - 4], (MOD_DRV + MOD_PATH + MOD_NAME + MOD_EXT)

.find_slashes:
    ; Locate last '' and last '.' (after last '') inside [bp-10] ("D:\DIR\FILE.EXT")
    mov si, word [bp - 10]
    mov word [bp - 12], si
    add word [bp - 12], 2 ; default slash at [bp-10]+2
    mov word [bp - 14], 0 ; 0 = no dot
.scan_sp:
    mov al, byte [si]
    test al, al
    jz .got_sp_bounds
    cmp al, 0x5C
    jne .chk_sp_dot
    mov word [bp - 12], si
    mov word [bp - 14], 0
    jmp .nx_sp
.chk_sp_dot:
    cmp al, '.'
    jne .nx_sp
    mov word [bp - 14], si
.nx_sp:
    inc si
    jmp .scan_sp

.got_sp_bounds:
    ; SI points to trailing NUL of [bp-10]!
    cmp word [bp - 14], 0
    jne .have_dot_ptr
    mov word [bp - 14], si
.have_dot_ptr:
    mov bx, si ; BX = end of string

    mov di, word [bp - 16]
    ; d: [bp-10 .. bp-10 + 2]
    test byte [bp - 4], MOD_DRV
    jz .part_p
    mov si, word [bp - 10]
    movsb
    movsb
.part_p:
    ; p: [bp-10 + 2 .. last_slash + 1]
    test byte [bp - 4], MOD_PATH
    jz .part_n
    mov si, word [bp - 10]
    add si, 2
    mov cx, word [bp - 12]
    inc cx
    sub cx, si
    jbe .part_n
    rep movsb
.part_n:
    ; n: [last_slash + 1 .. last_dot]
    test byte [bp - 4], MOD_NAME
    jz .part_x
    mov si, word [bp - 12]
    inc si
    mov cx, word [bp - 14]
    sub cx, si
    jbe .part_x
    rep movsb
.part_x:
    ; x: [last_dot .. end]
    test byte [bp - 4], MOD_EXT
    jz .save_di_end
    mov si, word [bp - 14]
    mov cx, bx
    sub cx, si
    jbe .save_di_end
    rep movsb
.save_di_end:
    mov word [bp - 16], di

.fin_mod:
    mov di, word [bp - 16]
    mov byte [di], 0
    mov cx, di
    sub cx, word [bp - 2]
    mov di, word [bp - 2]
    add sp, 16
    pop bp
    ret
@endproc

@data db bModDlrLen, 0
@data db szDefModTime, "11/21/2026 14:30 "
