;; @file pathnorm.inc
;; @isa 8086
;; @desc Phase 14 multi-era path normalizer (8.3, LFN, UNC, \?\, . / .. collapse, =D: sync, short_83).

@cpu 8086
@format inc

;; @proc path_normalize
;; @abi dos16
;; @in DS:SI = raw input ASCIIZ path, DS:DI = 260-byte destination buffer
;; @out DS:DI = normalized canonical ASCIIZ path, CX = byte length, CF=0
;; @err CF=1, AX = ERR_PATH_NOT_FOUND (DOS 1.25 subdir) or ERR_PATH_TOO_LONG (>63B dir on DOS 2.00/4.00)
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Normalizes '/'->'', preserves '\?' verbatim paths and '\SRV\SHR' UNC roots, and collapses '.'/'..'.
@proc path_normalize
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=src, [bp-4]=dst, [bp-6]=root_floor_off, [bp-8]=is_unc
    mov word [bp - 2], si
    mov word [bp - 4], di
    mov word [bp - 8], 0

    ; 1. Verbatim '\?' prefix in NT_CMD dialect: copy literally without altering slashes or trailing dots!
    cmp byte [si], 0x5C
    jne .not_verbatim
    cmp byte [si + 1], 0x5C
    jne .not_verbatim
    cmp byte [si + 2], '?'
    jne .not_verbatim
    cmp byte [si + 3], 0x5C
    jne .not_verbatim
    xor cx, cx
.vb_cp:
    mov al, byte [si]
    mov byte [di], al
    test al, al
    jz .vb_done
    inc si
    inc di
    inc cx
    jmp .vb_cp
.vb_done:
    add sp, 8
    pop bp
    clc
    ret

.not_verbatim:
    ; 2. MS-DOS 1.25 flat FCB filesystem forbids '/' and '' directory separators!
    cmp byte [bTrnDosMajor], 2
    jae .era_hier
    push si
.chk_125_sep:
    mov al, byte [si]
    test al, al
    jz .ok_125
    cmp al, 0x5C
    je .bad_125
    cmp al, '/'
    je .bad_125
    inc si
    jmp .chk_125_sep
.bad_125:
    pop si
    mov ax, ERR_PATH_NOT_FOUND
    add sp, 8
    pop bp
    stc
    ret
.ok_125:
    pop si
    ; Emit 'D:FILENAME.EXT' for DOS 1.25
    cmp byte [si + 1], ':'
    je .drv_125_given
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [di], al
    mov byte [di + 1], ':'
    add di, 2
    jmp .cp_125_body
.drv_125_given:
    mov al, byte [si]
    call chr_to_upper
    mov byte [di], al
    mov byte [di + 1], ':'
    add si, 2
    add di, 2
.cp_125_body:
    mov al, byte [si]
    call chr_to_upper
    mov byte [di], al
    test al, al
    jz .calc_125_len
    inc si
    inc di
    jmp .cp_125_body
.calc_125_len:
    mov ax, di
    mov di, word [bp - 4]
    sub ax, di
    mov cx, ax
    add sp, 8
    pop bp
    clc
    ret

.era_hier:
    ; 3. Check UNC '\SERVER\SHARE' prefix (two leading slashes/backslashes)
    mov al, byte [si]
    cmp al, '/'
    je .chk_unc2
    cmp al, 0x5C
    jne .seed_drive
.chk_unc2:
    mov ah, byte [si + 1]
    cmp ah, '/'
    je .is_unc_path
    cmp ah, 0x5C
    jne .seed_drive

.is_unc_path:
    mov word [bp - 8], 1
    add si, 2
    mov byte [di], 0x5C
    mov byte [di + 1], 0x5C
    add di, 2
    ; Copy SERVER\SHARE into prefix and set root_floor_off past SHARE!
    mov cx, 2
.unc_hdr_seg:
    mov al, byte [si]
    test al, al
    jz .unc_floor_set
    cmp al, '/'
    je .unc_seg_sep
    cmp al, 0x5C
    je .unc_seg_sep
    call chr_to_upper
    mov byte [di], al
    inc si
    inc di
    jmp .unc_hdr_seg
.unc_seg_sep:
    inc si
    dec cx
    jz .unc_floor_set
    mov byte [di], 0x5C
    inc di
    jmp .unc_hdr_seg
.unc_floor_set:
    mov byte [di], 0x5C
    inc di
    mov word [bp - 6], di
    mov byte [di], 0
    jmp .seg_loop

.seed_drive:
    ; Determine target drive letter ('A'..'Z') and whether path is root-relative vs CWD-relative
    cmp byte [si + 1], ':'
    jne .def_drv
    mov al, byte [si]
    call chr_to_upper
    mov dl, al
    sub dl, 'A'
    inc dl ; 1=A, 2=B, ...
    add si, 2
    jmp .write_drv
.def_drv:
    push si
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    pop si
    mov dl, al
    inc dl
    add al, 'A'

.write_drv:
    mov byte [di], al
    mov byte [di + 1], ':'
    mov byte [di + 2], 0x5C
    add di, 3
    mov word [bp - 6], di
    mov byte [di], 0

    ; If next char is '/' or '', path is root-relative; otherwise seed CWD first!
    mov al, byte [si]
    cmp al, '/'
    je .sk_lead_sep
    cmp al, 0x5C
    je .sk_lead_sep
    push si
    mov si, di
    call k700_getcwd
    pop si
.seek_end_cwd:
    cmp byte [di], 0
    je .cwd_seeded
    inc di
    jmp .seek_end_cwd
.sk_lead_sep:
    inc si
.cwd_seeded:

.seg_loop:
    ; Skip redundant '/' and '' separators
    mov al, byte [si]
    test al, al
    jnz .chk_sl1
    jmp .finish_norm
.chk_sl1:
    cmp al, '/'
    je .adv_sep
    cmp al, 0x5C
    jne .read_comp
.adv_sep:
    inc si
    jmp .seg_loop

.read_comp:
    ; Scan component [SI .. BX) up to next '/', '', or NUL
    mov bx, si
.sc_c:
    mov al, byte [bx]
    test al, al
    jz .got_comp_bounds
    cmp al, '/'
    je .got_comp_bounds
    cmp al, 0x5C
    je .got_comp_bounds
    inc bx
    jmp .sc_c

.got_comp_bounds:
    ; Strip trailing spaces and dots from component [SI .. DX) EXCEPT pure "." and ".."!
    mov cx, bx
    sub cx, si
    cmp cx, 1
    jne .chk_dotdot
    cmp byte [si], '.'
    jne .strip_trail
    mov si, bx
    jmp .seg_loop

.chk_dotdot:
    cmp cx, 2
    jne .strip_trail
    cmp byte [si], '.'
    jne .strip_trail
    cmp byte [si + 1], '.'
    jne .strip_trail
    ; '..' pops one directory component back toward [bp - 6] floor!
    mov si, bx
    cmp di, word [bp - 6]
    jbe .seg_loop
.pop_par:
    dec di
    cmp di, word [bp - 6]
    jbe .term_pop
    cmp byte [di], 0x5C
    jne .pop_par
.term_pop:
    mov byte [di], 0
    jmp .seg_loop

.strip_trail:
    mov dx, bx ; save raw token end in DX
.tr_lp:
    cmp bx, si
    jbe .tr_done
    mov al, byte [bx - 1]
    cmp al, 0x20
    je .dec_tr
    cmp al, '.'
    jne .tr_done
.dec_tr:
    dec bx
    jmp .tr_lp
.tr_done:
    xchg bx, dx ; DX = trimmed end, BX = raw token end!

.emit_comp:
    cmp dx, si
    jbe .next_from_bx
    cmp di, word [bp - 6]
    jbe .cp_comp_chars
    mov byte [di], 0x5C
    inc di
.cp_comp_chars:
    cmp si, dx
    jae .term_comp
    mov al, byte [si]
    cmp byte [bTrnDosMajor], 7
    jae .keep_lfn_case
    call chr_to_upper
.keep_lfn_case:
    mov byte [di], al
    inc si
    inc di
    jmp .cp_comp_chars
.term_comp:
    mov byte [di], 0
.next_from_bx:
    mov si, bx
    jmp .seg_loop

.finish_norm:
    ; If UNC path has nothing after '\SERVER\SHARE', trim trailing '' unless root
    cmp word [bp - 8], 0
    je .calc_final_len
    cmp di, word [bp - 6]
    ja .calc_final_len
    dec di
    mov byte [di], 0

.calc_final_len:
    mov ax, di
    mov di, word [bp - 4]
    sub ax, di
    mov cx, ax

    ; Enforce <= 63-byte directory path limit on strict MS-DOS 2.00 / 4.00 (when not NT_CMD / DOS 7+)
    cmp byte [bTrnDosMajor], 7
    jae .norm_ok
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bDialect]
    pop es
    cmp al, 5
    je .norm_ok
    cmp cx, 66 ; 'A:' (3) + 63B = 66B max
    jbe .norm_ok
    mov ax, ERR_PATH_TOO_LONG
    add sp, 8
    pop bp
    stc
    ret

.norm_ok:
    add sp, 8
    pop bp
    clc
    ret
@endproc

;; @proc gen_short_83
;; @abi dos16
;; @in DS:SI = Long Filename leaf ASCIIZ, DS:DI = 13-byte output buffer
;; @out DS:DI = uppercase 8.3 short alias (e.g. "PROGRA~1.TXT" or "README.TXT"), CX = length
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Generates a deterministic uppercase FAT 8.3 short alias from any LFN leaf name.
@proc gen_short_83
    push bp
    mov bp, di
    ; Locate last '.' in leaf (ignoring leading dots)
    mov bx, si
    xor dx, dx ; last_dot ptr (0 if none)
.skip_lead_dots:
    cmp byte [bx], '.'
    jne .find_ext_dot
    inc bx
    jmp .skip_lead_dots
.find_ext_dot:
    mov si, bx
.dot_scan:
    mov al, byte [si]
    test al, al
    jz .got_dot_pos
    cmp al, '.'
    jne .nx_ds
    mov dx, si
.nx_ds:
    inc si
    jmp .dot_scan

.got_dot_pos:
    ; Copy up to 8 valid uppercase stem chars from BX..DX (or BX..NUL); set CH=1 if lossy
    mov si, bx
    xor cx, cx ; CL = stem chars emitted, CH = lossy flag
.stem_cp:
    cmp si, dx
    je .stem_done
    mov al, byte [si]
    test al, al
    jz .stem_done
    inc si
    cmp al, 0x20
    je .mark_lossy
    cmp al, '.'
    je .mark_lossy
    cmp cl, 8
    jae .mark_lossy_stop
    call chr_to_upper
    cmp al, '+'
    je .sub_us
    cmp al, ','
    je .sub_us
    cmp al, ';'
    je .sub_us
    cmp al, '='
    je .sub_us
    cmp al, '['
    je .sub_us
    cmp al, ']'
    jne .put_st
.sub_us:
    mov al, '_'
    mov ch, 1
.put_st:
    mov byte [di], al
    inc di
    inc cl
    jmp .stem_cp
.mark_lossy:
    mov ch, 1
    jmp .stem_cp
.mark_lossy_stop:
    mov ch, 1

.stem_done:
    ; Check if extension > 3 chars (also triggers ~1 tail!)
    test dx, dx
    jz .apply_tilde
    mov si, dx
    inc si
    xor ah, ah
.cnt_ext:
    mov al, byte [si]
    test al, al
    jz .chk_ext_lossy
    cmp al, 0x20
    je .ext_sp_loss
    inc ah
.nx_ec:
    inc si
    jmp .cnt_ext
.ext_sp_loss:
    mov ch, 1
    jmp .nx_ec
.chk_ext_lossy:
    cmp ah, 3
    jbe .apply_tilde
    mov ch, 1

.apply_tilde:
    test ch, ch
    jz .emit_ext
    ; Truncate stem to <= 6 chars and append '~1'
    cmp cl, 6
    jbe .append_t1
    mov di, bp
    add di, 6
.append_t1:
    mov byte [di], '~'
    mov byte [di + 1], '1'
    add di, 2

.emit_ext:
    test dx, dx
    jz .fin_83
    mov si, dx
    inc si
    mov cl, 3
    xor ch, ch
.ext_lp:
    mov al, byte [si]
    test al, al
    jz .fin_83
    inc si
    cmp al, 0x20
    je .ext_lp
    test ch, ch
    jnz .put_ec
    mov byte [di], '.'
    inc di
    mov ch, 1
.put_ec:
    call chr_to_upper
    mov byte [di], al
    inc di
    dec cl
    jnz .ext_lp

.fin_83:
    mov byte [di], 0
    mov ax, di
    mov di, bp
    sub ax, di
    mov cx, ax
    pop bp
    ret
@endproc

;; @proc sync_drive_cwd_env
;; @abi dos16
;; @in Current DOS default drive and CWD
;; @out Updates hidden '=D:=D:\PATH' entry in wEnvSeg
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Synchronizes hidden per-drive '=A:'..'=Z:' environment variable after CD/PUSHD/POPD.
@proc sync_drive_cwd_env
    push bp
    mov bp, sp
    sub sp, 80
    ; [bp-6]=4B '=D:\0' name, [bp-80..bp-7]=value 'D:\CWD'
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [bp - 6], '='
    mov byte [bp - 5], al
    mov byte [bp - 4], ':'
    mov byte [bp - 3], 0

    lea di, [bp - 80]
    mov byte [di], al
    mov byte [di + 1], ':'
    mov byte [di + 2], 0x5C
    mov byte [di + 3], 0
    cmp byte [bTrnDosMajor], 2
    jb .set_it
    mov si, di
    add si, 3
    xor dl, dl
    call k700_getcwd

.set_it:
    lea si, [bp - 6]
    mov cx, 3
    lea dx, [bp - 80]
    call env_set_var
    add sp, 80
    pop bp
    ret
@endproc
