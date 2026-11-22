;; @file where.asm
;; @isa 8086
;; @desc Standalone WHERE.COM executable locator ([/R dir] [/Q] pattern / $ENV:pattern).

@cpu 8086
@format com
@entry where_boot

@include "../../inc/dos_equ.inc"

;; @proc where_boot
;; @abi dos16
;; @in DS=ES=CS=PSP
;; @out Jumps to where_entry
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc 0100h entrypoint trampoline for WHERE.COM.
@proc where_boot
    jmp where_entry
@endproc

;; @proc where_probe_one
;; @abi dos16
;; @in DS:SI = candidate full path ASCIIZ
;; @out Prints path (unless /Q) and increments [wWhereHits] if file exists
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Tests existence of DS:SI across DOS 1.25 FCB and DOS 2.00+ Handle APIs.
@proc where_probe_one
    push si
    cmp byte [bUDosMajor], 2
    jb .p125
    mov dx, si
    mov ax, 0x3D00
    int 0x21
    jc .miss
    mov bx, ax
    mov ah, 0x3E
    int 0x21
    jmp .hit

.p125:
    mov di, U_FCB_WORK
    mov cx, 19
    xor ax, ax
    rep stosw
    pop si
    push si
    mov di, U_FCB_WORK
    mov ax, 0x2901
    int 0x21
    cmp al, 0xFF
    je .miss
    mov dx, U_FCB_WORK
    mov ah, 0x0F
    int 0x21
    test al, al
    jnz .miss
    mov dx, U_FCB_WORK
    mov ah, 0x10
    int 0x21

.hit:
    inc word [wWhereHits]
    pop si
    cmp byte [bWhereQuiet], 0
    jne .dn
    call u_write_str
    call u_write_crlf
.dn:
    ret
.miss:
    pop si
    ret
@endproc

;; @proc where_app_and_probe
;; @abi dos16
;; @in DS:SI = 5-byte ".EXT\0" suffix, [wStemEnd] = end of stem in U_ARG_C
;; @out Appends suffix to U_ARG_C and calls where_probe_one
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Appends one PATHEXT suffix to U_ARG_C and probes existence.
@proc where_app_and_probe
    mov di, word [wStemEnd]
    mov cx, 5
    rep movsb
    mov si, U_ARG_C
    call where_probe_one
    ret
@endproc

;; @proc where_probe_dir
;; @abi dos16
;; @in DS:SI = directory prefix ASCIIZ, U_ARG_A = target name
;; @out Checks exact match plus .COM, .EXE, .BAT, .CMD extensions in directory DS:SI
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Probes one search directory across PATHEXT extensions.
@proc where_probe_dir
    mov di, U_ARG_C
.cp_d:
    mov al, byte [si]
    test al, al
    jz .sep_chk
    cmp al, '"'
    je .sk_q
    mov byte [di], al
    inc di
.sk_q:
    inc si
    jmp .cp_d
.sep_chk:
    cmp di, U_ARG_C
    jbe .cp_nm
    cmp byte [di - 1], 0x5C
    je .cp_nm
    cmp byte [di - 1], ':'
    je .cp_nm
    mov byte [di], 0x5C
    inc di
.cp_nm:
    mov si, U_ARG_A
    xor dx, dx ; has_dot
.nm_lp:
    mov al, byte [si]
    test al, al
    jz .nm_dn
    cmp al, '.'
    jne .nm_put
    mov dx, 1
.nm_put:
    mov byte [di], al
    inc si
    inc di
    jmp .nm_lp
.nm_dn:
    mov byte [di], 0
    mov word [wStemEnd], di

    test dx, dx
    jz .try_exts
    mov si, U_ARG_C
    call where_probe_one
    ret

.try_exts:
    mov si, addr(szWExtCom)
    call where_app_and_probe
    mov si, addr(szWExtExe)
    call where_app_and_probe
    mov si, addr(szWExtBat)
    call where_app_and_probe
    mov si, addr(szWExtCmd)
    call where_app_and_probe
    ret
@endproc

;; @proc where_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=[/R dir] [/Q] [$ENV:]pattern
;; @out Prints matching file paths; exits 0 if found, 1 if not found, 2 on syntax error
;; @err Exits with 1 when no matches found or 2 on missing pattern
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for WHERE.COM searching CWD (or /R dir) and %PATH% / $ENV: directories.
@proc where_entry
    call u_init
    mov byte [U_ARG_A], 0 ; pattern
    mov byte [U_ARG_B], 0 ; optional /R root

.arg_lp:
    call u_skip_ws
    test al, al
    jz .args_done
    cmp al, '/'
    jne .pat_tok
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'Q'
    jne .chk_r
    mov byte [bWhereQuiet], 1
    jmp .arg_lp
.chk_r:
    cmp al, 'R'
    jne .arg_lp
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp

.pat_tok:
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szWhereHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.args_done:
    cmp byte [U_ARG_A], 0
    jne .start_search
    mov al, 2
    call u_exit
    ret

.start_search:
    mov word [wWhereHits], 0
    cmp byte [U_ARG_B], 0
    je .chk_dlr_env
    mov si, U_ARG_B
    call where_probe_dir
    jmp .finish

.chk_dlr_env:
    ; Check "$ENV:pattern" syntax
    cmp byte [U_ARG_A], '$'
    jne .probe_cwd_and_path
    mov si, (U_ARG_A + 1)
    mov di, U_IO_B
.sc_col:
    mov al, byte [si]
    test al, al
    jz .probe_cwd_and_path
    cmp al, ':'
    je .got_dlr_col
    mov byte [di], al
    inc si
    inc di
    jmp .sc_col
.got_dlr_col:
    mov byte [di], 0
    inc si
    mov di, U_ARG_A
.sh_pat:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .sh_pat
    mov si, U_IO_B
    mov di, U_IO_A
    call u_get_env_var
    jc .finish
    jmp .walk_semi_list

.probe_cwd_and_path:
    ; Probe CWD first
    mov byte [U_IO_B], 0
    mov ah, 0x19
    int 0x21
    add al, 'A'
    mov byte [U_IO_B], al
    mov byte [U_IO_B + 1], ':'
    mov byte [U_IO_B + 2], 0
    cmp byte [bUDosMajor], 2
    jb .cwd_ready
    mov byte [U_IO_B + 2], 0x5C
    mov si, (U_IO_B + 3)
    xor dl, dl
    mov ah, 0x47
    int 0x21
.cwd_ready:
    mov si, U_IO_B
    call where_probe_dir

    mov si, addr(szWherePath)
    mov di, U_IO_A
    call u_get_env_var
    jc .finish

.walk_semi_list:
    mov si, U_IO_A
.seg_lp:
    cmp byte [si], ';'
    jne .chk_end
    inc si
    jmp .seg_lp
.chk_end:
    cmp byte [si], 0
    je .finish
    mov di, U_IO_B
.cp_seg:
    mov al, byte [si]
    test al, al
    jz .seg_dn
    cmp al, ';'
    je .adv_s
    mov byte [di], al
    inc si
    inc di
    jmp .cp_seg
.adv_s:
    inc si
.seg_dn:
    mov byte [di], 0
    push si
    mov si, U_IO_B
    call where_probe_dir
    pop si
    jmp .seg_lp

.finish:
    cmp word [wWhereHits], 0
    je .not_found
    xor al, al
    call u_exit
    ret
.not_found:
    cmp byte [bWhereQuiet], 0
    jne .exit1
    mov si, addr(szWhereNf)
    call u_write_str
    call u_write_crlf
.exit1:
    mov al, 1
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bWhereQuiet, 0
@data dw wWhereHits, 0
@data dw wStemEnd, 0
@data db szWherePath, "PATH", 0
@data db szWExtCom, ".COM", 0
@data db szWExtExe, ".EXE", 0
@data db szWExtBat, ".BAT", 0
@data db szWExtCmd, ".CMD", 0
@data db szWhereNf, "INFO: Could not find files for the given pattern(s).", 0
@data db szWhereHelp, "WHERE [/R dir] [/Q] pattern", 0
