;; @file m2_cmds.inc
;; @isa 8086
;; @desc Era-dispatching VFS commands (DIR/TYPE/COPY/DEL/REN/MD/RD/CD/CHCP/TRUENAME/VOL/TITLE),
;; @desc redirection (<, >, >>), stateless FILES=8 .BAT runner, and child .COM/.EXE spawner.

@cpu 8086
@format inc

;; @proc trn_read_arg
;; @abi dos16
;; @in DS:SI = command cursor, DS:DI = destination ASCIIZ buffer (max 120 bytes)
;; @out Copies next token (quoted or space/redir-delimited) to DS:DI, advances SI, CF=0 if non-empty
;; @err CF=1 if at EOL or redirection token
;; @clobbers ax, cx, si, di
;; @desc Extracts one argument token from DS:SI into DS:DI, honoring double quotes.
@proc trn_read_arg
    call trn_skip_ws
    cmp al, 0x00
    je .empty
    cmp al, 0x0D
    je .empty
    cmp al, '<'
    je .empty
    cmp al, '>'
    je .empty

    mov cx, 118
    cmp al, '"'
    je .quoted
.unq_lp:
    mov al, byte [si]
    cmp al, 0x00
    je .done_ok
    cmp al, 0x0D
    je .done_ok
    cmp al, 0x20
    je .done_ok
    cmp al, 0x09
    je .done_ok
    cmp al, '<'
    je .done_ok
    cmp al, '>'
    je .done_ok
    mov byte [di], al
    inc si
    inc di
    loop .unq_lp
    jmp .done_ok

.quoted:
    inc si
.q_lp:
    mov al, byte [si]
    cmp al, 0x00
    je .done_ok
    cmp al, 0x0D
    je .done_ok
    inc si
    cmp al, '"'
    je .done_ok
    mov byte [di], al
    inc di
    loop .q_lp

.done_ok:
    mov byte [di], 0
    clc
    ret
.empty:
    mov byte [di], 0
    stc
    ret
@endproc

;; @proc trn_print_u16
;; @abi dos16
;; @in AX = unsigned 16-bit integer
;; @out Prints decimal digits to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Formats AX as unsigned 32-bit zero-extended integer and writes it to console.
@proc trn_print_u16
    xor dx, dx
    mov di, szNumScratch
    call fmt_i32_dec
    mov dx, di
    call con_write_buf
    ret
@endproc

;; @proc trn_print_u64_free
;; @abi dos16
;; @in DS:SI = 8-byte u64 free byte count
;; @out Prints comma-grouped 64-bit byte total followed by bytes free footer
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Formats and prints era-accurate 64-bit free disk space footer for DIR.
@proc trn_print_u64_free
    mov bl, ','
    mov di, szNumScratch
    call fmt_u64_commas
    mov dx, di
    call con_write_buf
    mov si, addr(szBytesFree)
    call con_write_asciiz
    call con_write_crlf
    ret
@endproc

;; @proc trn_err_dos
;; @abi dos16
;; @in AX = ERR_* code (or raw DOS error code on DOS 4.00+ extended lookup)
;; @out Queries AH=59h / WINCMD.MSG if /MSG or DOS 4.00+, prints error, sets [wTrnExitCode]=1
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Era-aware error reporter supporting MS-DOS 4.00 AH=59h and external WINCMD.MSG.
@proc trn_err_dos
    push bp
    mov bp, ax
    cmp bp, 0x0100
    jae .builtin_msg
    cmp byte [bTrnDosMajor], 4
    jb .builtin_msg

    ; Check if external WINCMD.MSG lookup is available on disk
    mov dx, addr(szMsgFileName)
    xor al, al
    call k200_open
    jc .builtin_msg
    mov bx, ax
    mov dx, word [wArenaBase]
    mov cx, 256
    call k200_read
    push ax
    call k200_close
    pop cx
    cmp cx, 8
    jb .builtin_msg
    mov si, word [wArenaBase]
    cmp word [si], 0x4D57
    jne .builtin_msg
    cmp word [si + 2], 0x4753
    jne .builtin_msg
    add si, 8
.msg_scan:
    mov ax, word [si]
    test ax, ax
    jz .builtin_msg
    add si, 2
    cmp ax, bp
    je .found_ext_msg
.skip_z:
    cmp byte [si], 0
    je .next_msg
    inc si
    jmp .skip_z
.next_msg:
    inc si
    jmp .msg_scan

.found_ext_msg:
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 1
    pop bp
    ret

.builtin_msg:
    mov ax, bp
    call err_print_code
    mov word [wTrnExitCode], 1
    pop bp
    ret
@endproc

;; @proc cmd_dir
;; @abi dos16
;; @in DS:SI = optional wildcard argument
;; @out Enumerates directory entries and prints free space across DOS 1.25, 2.00, 4.00, and 7.x
;; @err Prints File not found and sets ERRORLEVEL=1 if no matches
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements DIR across FCB (1.25), DTA (2.00), Serial/>32M (4.00), and LFN/FAT32 (7.x).
@proc cmd_dir
    push bp
    mov bp, TRN_CMD_WORK
    ; Scratch layout in arena:
    ; [bp + 0..127]   = pattern string
    ; [bp + 128..191] = XFCB / DISK_ID4 / EXT_FREE_SPACE
    ; [bp + 192..271] = 64B DTA / u64 free buf
    ; [bp + 272..600] = 318B WIN32_FIND_DATAA (DOS 7.x) or 1024B FAT12 sector buf (DOS 1.25)
    mov di, bp
    call trn_read_arg
    jnc .has_pat
    mov byte [bp], '*'
    mov byte [bp + 1], '.'
    mov byte [bp + 2], '*'
    mov byte [bp + 3], 0
.has_pat:

    cmp byte [bTrnDosMajor], 2
    jae .dir_ge200
    jmp .dir_dos125
.dir_ge200:

    ; On DOS 4.00+, query and print 32-bit Volume Serial Number (XXXX-XXXX) if present
    cmp byte [bTrnDosMajor], 4
    jb .chk_dos7_lfn
    mov bl, 0
    mov dx, bp
    add dx, 128
    call k400_vol_serial
    jc .chk_dos7_lfn
    push ax
    push dx
    mov si, addr(szVolSerialPfx)
    call con_write_asciiz
    pop ax
    mov di, szNumScratch
    call fmt_hex16_4
    mov byte [di], '-'
    inc di
    pop ax
    call fmt_hex16_4
    mov byte [di], 0
    mov si, szNumScratch
    call con_write_asciiz
    call con_write_crlf

.chk_dos7_lfn:
    cmp byte [bTrnDosMajor], 7
    jb .dir_dos200
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bHwFlags]
    pop es
    test al, HW_LFN_VOL
    jz .dir_dos200

    ; --- MS-DOS 7.x Long Filename Enumeration (AX=714Eh / 714Fh / 71A1h) ---
    mov dx, bp
    mov di, bp
    add di, 272
    mov cx, 0x0016
    call k700_find_first
    jnc .got_lfn_hdl
    jmp .not_found
.got_lfn_hdl:
    mov bx, ax

.lfn_lp:
    push bx
    mov si, bp
    add si, (272 + WIN32_FIND_DATAA.cAlternateFileName)
    cmp byte [si], 0
    jne .prt_short
    mov si, bp
    add si, (272 + WIN32_FIND_DATAA.cFileName)
.prt_short:
    call con_write_asciiz
    test byte [bp + 272 + WIN32_FIND_DATAA.dwFileAttributes], ATTR_DIRECTORY
    jz .lfn_sp
    mov si, addr(szDirTag)
    call con_write_asciiz
    jmp .prt_lfn_full
.lfn_sp:
    mov si, addr(szTwoSpaces)
    call con_write_asciiz
.prt_lfn_full:
    mov si, bp
    add si, (272 + WIN32_FIND_DATAA.cFileName)
    call con_write_asciiz
    call con_write_crlf

    pop bx
    mov di, bp
    add di, 272
    call k700_find_next
    jnc .lfn_lp

    ; Mandatory FindClose (AX=71A1h) so Win9x search handle never leaks!
    call k700_find_close

    ; Try FAT32 >2 GiB free space (AX=7303h), falling back to k400_free_space64
    mov dx, addr(szCurRoot)
    mov di, bp
    add di, 192
    mov bx, bp
    add bx, 128
    call k700_fat32_free64
    jnc .show_free_u64
    jmp .free_200_400

    ; --- MS-DOS 2.00 / 4.00 DTA_FIND Enumeration (AH=4Eh / 4Fh) ---
.dir_dos200:
    mov dx, bp
    mov di, bp
    add di, 192
    mov cx, 0x0016
    call k200_find_first
    jnc .dta_lp
    jmp .not_found

.dta_lp:
    mov si, bp
    add si, (192 + DTA_FIND.szName)
    call con_write_asciiz
    test byte [bp + 192 + DTA_FIND.bAttr], ATTR_DIRECTORY
    jz .dta_eol
    mov si, addr(szDirTag)
    call con_write_asciiz
.dta_eol:
    call con_write_crlf
    mov di, bp
    add di, 192
    call k200_find_next
    jnc .dta_lp

.free_200_400:
    xor dl, dl
    mov di, bp
    add di, 192
    call k400_free_space64
    jc .j_done_dir
.show_free_u64:
    mov si, bp
    add si, 192
    call trn_print_u64_free
.j_done_dir:
    jmp .done_ok

    ; --- MS-DOS 1.25 FCB/XFCB Enumeration (AH=11h / 12h + INT 25h FAT12 Free Space) ---
.dir_dos125:
    mov si, bp
    mov di, bp
    add di, 128
    mov dx, 0x0001
    call k125_init_xfcb
    jc .err_ax

    mov dx, bp
    add dx, 192
    mov ah, DOS_SET_DTA
    int 0x21
    mov dx, bp
    add dx, (128 + XFCB.fcb)
    mov ah, DOS_FCB_FIND_FIRST
    int 0x21
    test al, al
    jnz .not_found

.fcb_lp:
    ; Print 8-char name + space + 3-char ext from [bp + 192 + 1..11]
    mov dx, bp
    add dx, 193
    mov cx, 8
    call con_write_buf
    mov dx, addr(szOneSpace)
    mov cx, 1
    call con_write_buf
    mov dx, bp
    add dx, 201
    mov cx, 3
    call con_write_buf
    call con_write_crlf

    mov dx, bp
    add dx, 192
    mov ah, DOS_SET_DTA
    int 0x21
    mov dx, bp
    add dx, (128 + XFCB.fcb)
    mov ah, DOS_FCB_FIND_NEXT
    int 0x21
    test al, al
    jz .fcb_lp

    xor dl, dl
    mov di, bp
    add di, 192
    mov bx, bp
    add bx, 272
    call k125_free_space64
    jc .done_ok
    mov si, bp
    add si, 192
    call trn_print_u64_free

.done_ok:
    mov word [wTrnExitCode], 0
    pop bp
    ret

.not_found:
    mov ax, ERR_FILE_NOT_FOUND
.err_ax:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_type
;; @abi dos16
;; @in DS:SI = filename argument
;; @out Streams file contents to stdout (honoring 0x1A EOF in ASCII mode)
;; @err Prints diagnostic on missing file or syntax error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements TYPE across MS-DOS 1.25 FCBs and MS-DOS 2.00..7.x handles.
@proc cmd_type
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jnc .type_arg_ok
    jmp .syn_err
.type_arg_ok:

    cmp byte [bTrnDosMajor], 2
    jae .type_ge200
    jmp .type_125
.type_ge200:

    cmp byte [bTrnDosMajor], 7
    jb .chk_type_400
    mov si, bp
    xor bx, bx
    xor cx, cx
    mov dx, 0x0001
    stc
    mov ax, DOS_LFN_EXT_OPEN
    int 0x21
    jnc .got_hdl

.chk_type_400:
    cmp byte [bTrnDosMajor], 4
    jb .open_200
    mov si, bp
    xor bx, bx
    xor cx, cx
    mov dx, 0x0001
    call k400_ext_open
    jnc .got_hdl
.open_200:
    mov dx, bp
    xor al, al
    call k200_open
    jnc .got_hdl
    jmp .err_out
.got_hdl:
    mov bx, ax
.rd_200:
    mov dx, bp
    add dx, 256
    mov cx, 256
    call k200_read
    jc .cls_200
    test ax, ax
    jz .cls_200
    ; Trim at 0x1A Ctrl+Z if present
    mov si, bp
    add si, 256
    xor cx, cx
.scan_z:
    cmp cx, ax
    jae .emit_200
    mov di, si
    add di, cx
    cmp byte [di], 0x1A
    je .hit_z
    inc cx
    jmp .scan_z
.hit_z:
    push bx
    mov dx, si
    call con_write_buf
    pop bx
    jmp .cls_200
.emit_200:
    push bx
    mov dx, si
    call con_write_buf
    pop bx
    jmp .rd_200
.cls_200:
    call k200_close
    mov word [wTrnExitCode], 0
    pop bp
    ret

.type_125:
    mov si, bp
    mov di, bp
    add di, 128
    call k125_open
    jc .err_out
.rd_125:
    mov di, bp
    add di, 128
    mov dx, bp
    add dx, 256
    mov cx, 128
    call k125_read
    test cx, cx
    jz .cls_125
    mov si, bp
    add si, 256
    xor bx, bx
.z_125:
    cmp bx, cx
    jae .out_125
    cmp byte [si + bx], 0x1A
    je .out_125_z
    inc bx
    jmp .z_125
.out_125_z:
    mov dx, si
    mov cx, bx
    call con_write_buf
    jmp .cls_125
.out_125:
    mov dx, si
    call con_write_buf
    jmp .rd_125
.cls_125:
    mov di, bp
    add di, 128
    call k125_close
    mov word [wTrnExitCode], 0
    pop bp
    ret

.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_copy
;; @abi dos16
;; @in DS:SI = "<src> <dst>"
;; @out Copies source file to destination across DOS 1.25 FCBs and DOS 2.00..7.x handles
;; @err Prints diagnostic on missing source or read-only destination
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements single-file COPY across MS-DOS 1.25..8.0.
@proc cmd_copy
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .j_syn_cp
    mov di, bp
    add di, 128
    call trn_read_arg
    jnc .cp_args_ok
.j_syn_cp:
    jmp .syn_err
.cp_args_ok:

    cmp byte [bTrnDosMajor], 2
    jb .copy_125

    mov dx, bp
    xor al, al
    call k200_open
    jnc .cp_src_ok
    jmp .err_out
.cp_src_ok:
    mov si, ax

    mov dx, bp
    add dx, 128
    xor cx, cx
    call k200_create
    jc .close_src_err
    mov di, ax

.cp_lp200:
    mov bx, si
    mov dx, bp
    add dx, 256
    mov cx, 512
    call k200_read
    jc .done_200
    test ax, ax
    jz .done_200
    mov cx, ax
    mov bx, di
    mov dx, bp
    add dx, 256
    call k200_write
    jmp .cp_lp200

.done_200:
    mov bx, di
    call k200_close
    mov bx, si
    call k200_close
    mov si, addr(szOneFileCopied)
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    pop bp
    ret

.close_src_err:
    push ax
    mov bx, si
    call k200_close
    pop ax
    jmp .err_out

.copy_125:
    mov si, bp
    mov di, bp
    add di, 256
    call k125_open
    jc .err_out

    mov si, bp
    add si, 128
    mov di, bp
    add di, 304
    mov bx, bp
    add bx, 360
    call k125_create
    jc .cls_125_src_err

.cp_lp125:
    mov di, bp
    add di, 256
    mov dx, bp
    add dx, 440
    mov cx, 256
    call k125_read
    test cx, cx
    jz .done_125
    mov di, bp
    add di, 304
    mov dx, bp
    add dx, 440
    call k125_write
    jc .cls_both_err125
    jmp .cp_lp125

.done_125:
    mov di, bp
    add di, 304
    call k125_close
    mov di, bp
    add di, 256
    call k125_close
    mov si, addr(szOneFileCopied)
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    pop bp
    ret

.cls_both_err125:
    push ax
    mov di, bp
    add di, 304
    call k125_close
    pop ax
.cls_125_src_err:
    push ax
    mov di, bp
    add di, 256
    call k125_close
    pop ax
    jmp .err_out

.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_del
;; @abi dos16
;; @in DS:SI = target filename or wildcard
;; @out Deletes file(s) via k125_delete, k200 AH=41h, or k700_delete
;; @err Prints diagnostic on error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements DEL / ERASE across MS-DOS 1.25..8.0.
@proc cmd_del
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .syn_err

    cmp byte [bTrnDosMajor], 2
    jb .del_125
    mov dx, bp
    mov si, 1
    call k700_delete
    jc .err_out
    mov word [wTrnExitCode], 0
    pop bp
    ret

.del_125:
    mov si, bp
    mov di, bp
    add di, 128
    call k125_delete
    jc .err_out
    mov word [wTrnExitCode], 0
    pop bp
    ret

.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_ren
;; @abi dos16
;; @in DS:SI = "<old> <new>"
;; @out Renames file(s) via k125_rename (wildcard AH=17h) or k700_rename (7156h / 56h)
;; @err Prints diagnostic on error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements REN / RENAME across MS-DOS 1.25..8.0.
@proc cmd_ren
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .syn_err
    mov di, bp
    add di, 128
    call trn_read_arg
    jc .syn_err

    cmp byte [bTrnDosMajor], 2
    jb .ren_125
    mov dx, bp
    mov di, bp
    add di, 128
    call k700_rename
    jc .err_out
    mov word [wTrnExitCode], 0
    pop bp
    ret

.ren_125:
    mov si, bp
    mov bx, bp
    add bx, 128
    mov di, bp
    add di, 256
    call k125_rename
    jc .err_out
    mov word [wTrnExitCode], 0
    pop bp
    ret

.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_mkdir
;; @abi dos16
;; @in DS:SI = directory path
;; @out Creates directory via k700_mkdir / k200_mkdir (rejects on DOS 1.25!)
;; @err Prints Path not found on DOS 1.25 or syntax/path error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements MD / MKDIR across MS-DOS 1.25..8.0.
@proc cmd_mkdir
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .syn_err
    mov dx, bp
    call k700_mkdir
    jc .err_out
    mov word [wTrnExitCode], 0
    pop bp
    ret
.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_rmdir
;; @abi dos16
;; @in DS:SI = directory path
;; @out Removes directory via k700_rmdir / k200_rmdir
;; @err Prints diagnostic on failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements RD / RMDIR across MS-DOS 1.25..8.0.
@proc cmd_rmdir
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .syn_err
    mov dx, bp
    call k700_rmdir
    jc .err_out
    mov word [wTrnExitCode], 0
    pop bp
    ret
.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_chdir
;; @abi dos16
;; @in DS:SI = optional directory path (prints CWD if empty!)
;; @out Queries or changes current directory via k700_getcwd/chdir and k200_getcwd/chdir
;; @err Prints Path not found on DOS 1.25 when path argument given
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements CD / CHDIR across MS-DOS 1.25..8.0.
@proc cmd_chdir
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .show_cwd
    mov dx, bp
    call k700_chdir
    jc .err_out
    mov word [wTrnExitCode], 0
    pop bp
    ret

.show_cwd:
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [bp], al
    mov byte [bp + 1], ':'
    mov byte [bp + 2], 0x5C
    mov si, bp
    add si, 3
    xor dl, dl
    call k700_getcwd
    mov si, bp
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    pop bp
    ret

.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_chcp
;; @abi dos16
;; @in DS:SI = optional codepage number
;; @out Queries or switches global codepage and refreshes DBCS lead-byte bitmap
;; @err Prints diagnostic if unsupported (< DOS 4.00) or NLSFUNC absent
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements CHCP [nnn] on MS-DOS 4.00+ with automatic AX=6507h DBCS refresh.
@proc cmd_chcp
    call trn_skip_ws
    cmp al, 0x00
    je .query_cp
    cmp al, 0x0D
    je .query_cp
    cmp al, '<'
    je .query_cp
    cmp al, '>'
    je .query_cp

    call parse_i32_lit
    jc .syn_err
    mov bx, ax
    call nls_set_cp
    jc .err_out

.query_cp:
    call nls_get_cp
    jc .err_out
    push bx
    mov si, addr(szActiveCp)
    call con_write_asciiz
    pop ax
    call trn_print_u16
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret

.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    ret
@endproc

;; @proc cmd_truename
;; @abi dos16
;; @in DS:SI = path argument
;; @out Prints canonicalized path via k700_truename / k400_truename
;; @err Prints diagnostic if unsupported (< DOS 3.00) or invalid path
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements internal TRUENAME built-in command.
@proc cmd_truename
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .syn_err
    mov si, bp
    mov di, bp
    add di, 128
    xor cl, cl
    call k700_truename
    jc .err_out
    mov si, bp
    add si, 128
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    pop bp
    ret
.syn_err:
    mov ax, ERR_SYNTAX
.err_out:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc cmd_title
;; @abi dos16
;; @in DS:SI = new title string
;; @out Updates WINOLDAP window title via INT 2Fh AX=168Eh DX=0000h when under Windows
;; @err none
;; @clobbers ax, cx, dx, si, di
;; @desc Synchronizes DOS session window title bar under Windows 3.x/9x WINOLDAP.
@proc cmd_title
    call trn_skip_ws
    mov di, TRN_CMD_WORK
    mov cx, 78
.cp_t:
    mov al, byte [si]
    cmp al, 0x00
    je .set_t
    cmp al, 0x0D
    je .set_t
    mov byte [di], al
    inc si
    inc di
    loop .cp_t
.set_t:
    mov byte [di], 0
    mov di, TRN_CMD_WORK
    xor dx, dx
    mov ax, MULTIPLEX_WINOLDAP_TITLE
    int 0x2F
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_vol
;; @abi dos16
;; @in DS:SI = optional drive argument (e.g. "A:")
;; @out Prints Volume in drive X label (XFCB bAttr=0x08) and DOS 4.00+ Volume Serial Number
;; @err Prints Invalid drive specification on invalid drive letter
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Queries root XFCB volume label on MS-DOS 1.25..7.x and AX=6900h serial on DOS 4.00+.
@proc cmd_vol
    push bp
    mov bp, TRN_CMD_WORK
    mov di, bp
    call trn_read_arg
    jc .def_drv
    cmp byte [bp + 1], ':'
    jne .j_bad_drv
    mov al, byte [bp]
    call chr_to_upper
    cmp al, 'A'
    jb .j_bad_drv
    cmp al, 'Z'
    ja .j_bad_drv
    mov dh, al
    sub al, 'A'
    inc al
    mov dl, al
    jmp .got_drv
.j_bad_drv:
    jmp .bad_drv

.def_drv:
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    mov dh, al
    add dh, 'A'
    mov dl, al
    inc dl

.got_drv:
    push dx
    mov si, addr(szVolInDrv)
    call con_write_asciiz
    pop dx
    push dx
    mov byte [szNumScratch], dh
    mov byte [szNumScratch + 1], 0
    mov si, szNumScratch
    call con_write_asciiz

    ; Build 44-byte XFCB at [bp + 64] with 0xFF, 5 zeros, bAttr=0x08 (ATTR_VOLUME), drive, 11 '?'
    mov di, bp
    add di, 64
    mov cx, 22
    xor ax, ax
    rep stosw
    mov byte [bp + 64], 0xFF
    mov byte [bp + 64 + 6], ATTR_VOLID
    pop dx
    push dx
    mov byte [bp + 64 + 7], dl
    mov di, bp
    add di, (64 + 8)
    mov cx, 11
    mov al, '?'
    rep stosb

    ; Set temporary DTA at [bp + 128], call AH=11h, and restore DTA to PSP:0x0080
    mov dx, bp
    add dx, 128
    mov ah, DOS_SET_DTA
    int 0x21
    mov dx, bp
    add dx, 64
    mov ah, DOS_FCB_FIND_FIRST
    int 0x21
    push ax
    push ds
    mov ds, word [wTrnResSeg]
    mov dx, 0x0080
    mov ah, DOS_SET_DTA
    int 0x21
    pop ds
    pop ax

    test al, al
    jnz .no_lbl
    test byte [bp + 128 + 19], ATTR_VOLID
    jz .no_lbl

    mov si, addr(szVolIs)
    call con_write_asciiz
    mov di, bp
    add di, (128 + 8 + 11)
    mov cx, 11
.trim_vol:
    cmp byte [di - 1], 0x20
    jne .term_vol
    dec di
    loop .trim_vol
.term_vol:
    mov byte [di], 0
    mov si, bp
    add si, (128 + 8)
    call con_write_asciiz
    call con_write_crlf
    jmp .chk_ser

.no_lbl:
    mov si, addr(szVolNoLbl)
    call con_write_asciiz
    call con_write_crlf

.chk_ser:
    pop dx
    cmp byte [bTrnDosMajor], 4
    jb .vol_ok
    mov bl, dl
    mov dx, bp
    add dx, 200
    call k400_vol_serial
    jc .vol_ok
    push ax
    push dx
    mov si, addr(szVolSerialPfx)
    call con_write_asciiz
    pop ax
    mov di, szNumScratch
    call fmt_hex16_4
    mov byte [di], '-'
    inc di
    pop ax
    call fmt_hex16_4
    mov byte [di], 0
    mov si, szNumScratch
    call con_write_asciiz
    call con_write_crlf

.vol_ok:
    mov word [wTrnExitCode], 0
    pop bp
    ret

.bad_drv:
    mov ax, ERR_INVALID_DRIVE
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc trn_has_ext
;; @abi dos16
;; @in DS:SI = ASCIIZ filename, DS:DI = 4-char uppercase ".EXT"
;; @out CF=0 if filename ends with .EXT case-insensitively, CF=1 otherwise
;; @err none
;; @clobbers ax, bx, cx, si, di
;; @desc Checks whether an ASCIIZ path ends with a specific 4-character extension.
@proc trn_has_ext
    xor bx, bx
.len_lp:
    mov al, byte [bx + si]
    test al, al
    jz .chk_len
    inc bx
    jmp .len_lp
.chk_len:
    cmp bx, 4
    jb .no
    add si, bx
    sub si, 4
    mov cx, 4
.cmp_lp:
    mov al, byte [si]
    call chr_to_upper
    cmp al, byte [di]
    jne .no
    inc si
    inc di
    loop .cmp_lp
    clc
    ret
.no:
    stc
    ret
@endproc

;; @proc trn_push_batch_frame
;; @abi dos16
;; @in DS:SI = ASCIIZ .BAT path
;; @out Pushes a new BATCH_FRAME onto resident batchResStack (max depth 4), CF=0
;; @err CF=1, AX=ERR_BATCH_NEST_LIMIT if nesting depth >= 4
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Allocates a resident batch frame surviving child spawns and transient reloads.
@proc trn_push_batch_frame
    push bp
    push es
    push bx ; save arg_tail ptr!
    mov bp, TRN_CMD_WORK
    add bp, 1480

    cmp byte [bTrnDosMajor], 2
    jb .canon_done
    cmp byte [si + 1], ':'
    je .canon_done
    cmp byte [si], 0x5C
    je .canon_done
    cmp byte [si], '/'
    je .canon_done

    push si
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    add al, 'A'
    mov byte [bp], al
    mov byte [bp + 1], ':'
    mov byte [bp + 2], 0x5C
    mov si, bp
    add si, 3
    xor dl, dl
    call k700_getcwd
    mov di, bp
.seek_cwd_end:
    cmp byte [di], 0
    je .got_cwd_end
    inc di
    jmp .seek_cwd_end
.got_cwd_end:
    cmp byte [di - 1], 0x5C
    je .append_rel_bat
    mov byte [di], 0x5C
    inc di
.append_rel_bat:
    pop si
.cp_rel_bat:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_rel_bat
    mov si, bp
.canon_done:

    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    cmp byte [bTrnIsCall], 0
    jne .push_new
    test al, al
    jz .push_new
    ; Un-CALLed .BAT inside an active .BAT chains in-place (replaces top frame)!
    dec al
    xor dx, dx
    jmp .fill_slot

.push_new:
    cmp al, 4
    jae .overflow
    mov dl, 1

.fill_slot:
    mov bh, al ; save slot index 0..3 in BH
    xor ah, ah
    mov cl, 48
    mul cl
    mov di, addr(batchResStack)
    add di, ax

    mov word [es:di + BATCH_FRAME.dwFileOff], 0
    mov word [es:di + BATCH_FRAME.dwFileOff + 2], 0
    mov byte [es:di + BATCH_FRAME.bShiftOff], 0
    mov byte [es:di + BATCH_FRAME.bFlags], 1

    add di, BATCH_FRAME.szPath
    mov cx, 41
.cp_p:
    mov al, byte [si]
    mov byte [es:di], al
    test al, al
    jz .inc_d
    inc si
    inc di
    loop .cp_p
    mov byte [es:di], 0

.inc_d:
    test dl, dl
    jz .cp_bat_args
    inc byte [es:resState + RES_STATE.bBatchDepth]

.cp_bat_args:
    ; Copy argument tail from stacked BX into DS:(TRN_BATCH_ARGS + slot * 64)!
    pop si ; SI = arg_tail (or 0 if none)
    mov al, bh
    xor ah, ah
    mov cl, 64
    mul cl
    mov di, TRN_BATCH_ARGS
    add di, ax
    test si, si
    jz .empty_bat_args
    mov cx, 63
.cp_ba_lp:
    mov al, byte [si]
    test al, al
    jz .empty_bat_args
    cmp al, 0x0D
    je .empty_bat_args
    mov byte [di], al
    inc si
    inc di
    loop .cp_ba_lp
.empty_bat_args:
    mov byte [di], 0

.ok_ret:
    pop es
    pop bp
    clc
    ret

.overflow:
    pop bx
    pop es
    pop bp
    mov ax, ERR_BATCH_NEST_LIMIT
    stc
    ret
@endproc

;; @proc trn_spawn_prog
;; @abi dos16
;; @in DS:BX = resolved ASCIIZ .COM/.EXE path, DS:SI = raw argument tail
;; @out Spawns external child via ldr125_exec_far (DOS 1.25) or exec200_far (DOS 2.00+)
;; @err CF=1, AX=ERR_* if program missing or load failed
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Invokes resident child-process loader trampoline and syncs [wTrnExitCode].
@proc trn_spawn_prog
    push bp
    push ds
    push es
    mov es, word [wTrnResSeg]

    cmp byte [bTrnDosMajor], 2
    jb .spawn_125

    ; Copy ASCIIZ path to shared resident bResWorkBuf and format tail in PSP:0x0080
    push si
    mov si, bx
    mov di, addr(bResWorkBuf)
    mov cx, 47
.cp_p200:
    mov al, byte [si]
    mov byte [es:di], al
    test al, al
    jz .tail_200
    inc si
    inc di
    loop .cp_p200
    mov byte [es:di], 0

.tail_200:
    pop si
    mov di, (PSP.bCmdLen + 1)
    xor cx, cx
.cp_t200:
    mov al, byte [si]
    cmp al, 0x00
    je .fin_t200
    cmp al, 0x0D
    je .fin_t200
    cmp al, '<'
    je .fin_t200
    cmp al, '>'
    je .fin_t200
    cmp cx, 124
    jae .fin_t200
    mov byte [es:di], al
    inc si
    inc di
    inc cx
    jmp .cp_t200
.fin_t200:
    mov byte [es:di], 0x0D
    mov byte [es:PSP.bCmdLen], cl

    call far [wTrnExec200Ptr]
    jc .fail_pop
    mov word [wTrnExitCode], ax
    pop es
    pop ds
    pop bp
    clc
    ret

.spawn_125:
    push si
    mov si, bx
    mov di, addr(fcbResWork)
    mov ax, 0x2901
    int 0x21
    pop si
    cmp al, 0xFF
    je .bad_125

    mov di, (PSP.bCmdLen + 1)
    xor cx, cx
.cp_t125:
    mov al, byte [si]
    cmp al, 0x00
    je .fin_t125
    cmp al, 0x0D
    je .fin_t125
    cmp al, '<'
    je .fin_t125
    cmp al, '>'
    je .fin_t125
    cmp cx, 124
    jae .fin_t125
    mov byte [es:di], al
    inc si
    inc di
    inc cx
    jmp .cp_t125
.fin_t125:
    mov byte [es:di], 0x0D
    mov byte [es:PSP.bCmdLen], cl

    call far [wTrnLdr125Ptr]
    jc .fail_pop
    pop es
    pop ds
    pop bp
    mov word [wTrnExitCode], 0
    clc
    ret

.bad_125:
    mov ax, ERR_BAD_COMMAND
.fail_pop:
    pop es
    pop ds
    pop bp
    stc
    ret
@endproc

;; @proc trn_build_cand
;; @abi dos16
;; @in DS:BP = base command token, DS:SI = 4-char ".EXT" ASCIIZ suffix
;; @out Writes "<token>.EXT\0" to DS:[BP + 128]
;; @err none
;; @clobbers ax, cx, si, di
;; @desc Appends a 4-character extension (.COM/.EXE/.BAT) onto candidate buffer [BP + 128].
@proc trn_build_cand
    push si
    mov si, bp
    mov di, bp
    add di, 128
.bc_lp:
    mov al, byte [si]
    test al, al
    jz .bc_ext
    mov byte [di], al
    inc si
    inc di
    jmp .bc_lp
.bc_ext:
    pop si
    mov cx, 5
    rep movsb
    ret
@endproc

;; @proc trn_exec_external
;; @abi dos16
;; @in DS:SI = command line starting at external command token
;; @out Resolves explicit or .COM/.EXE/.BAT extension and launches program or batch frame
;; @err Prints Bad command or file name and sets [wTrnExitCode]=1 if not found
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Searches CWD for .COM, .EXE, or .BAT and dispatches to child loader or batch stack.
@proc trn_exec_external
    push bp
    mov bp, TRN_CMD_WORK
    add bp, 640
    mov di, bp
    call trn_read_arg
    jnc .ext_arg_ok
    jmp .bad_cmd
.ext_arg_ok:
    mov dx, si
    mov word [bp + 390], dx

    ; Check if explicit .BAT, .COM, or .EXE extension was typed
    push dx
    mov si, bp
    mov di, addr(szExtBat)
    call trn_has_ext
    pop dx
    jc .not_exp_bat
    jmp .launch_bat
.not_exp_bat:

    push dx
    mov si, bp
    mov di, addr(szExtCom)
    call trn_has_ext
    pop dx
    jnc .j_direct_bin

    push dx
    mov si, bp
    mov di, addr(szExtExe)
    call trn_has_ext
    pop dx
    jc .try_cands
.j_direct_bin:
    jmp .try_direct_bin

.try_cands:
    ; Append .COM -> .EXE -> .BAT in [bp + 128] and try each in order
    mov si, addr(szExtCom)
    call trn_build_cand
    push dx
    mov bx, bp
    add bx, 128
    mov si, dx
    call trn_spawn_prog
    pop dx
    jc .try_cand_exe
    jmp .ok_done

.try_cand_exe:
    mov si, addr(szExtExe)
    call trn_build_cand
    push dx
    mov bx, bp
    add bx, 128
    mov si, dx
    call trn_spawn_prog
    pop dx
    jc .try_cand_bat
    jmp .ok_done

.try_cand_bat:
    mov si, addr(szExtBat)
    call trn_build_cand
    ; Verify .BAT exists before pushing batch frame
    cmp byte [bTrnDosMajor], 2
    jb .chk_bat125
    mov dx, bp
    add dx, 128
    xor al, al
    call k200_open
    jc .j_bad_cmd1
    mov bx, ax
    call k200_close
    mov si, bp
    add si, 128
    mov bx, word [bp + 390]
    call trn_push_batch_frame
    jc .j_err_ax1
    jmp .ok_done

.j_bad_cmd1:
    jmp .bad_cmd
.j_err_ax1:
    jmp .err_ax

.chk_bat125:
    mov si, bp
    add si, 128
    mov di, bp
    add di, 260
    call k125_open
    jc .bad_cmd
    mov di, bp
    add di, 260
    call k125_close
    mov si, bp
    add si, 128
    mov bx, word [bp + 390]
    call trn_push_batch_frame
    jc .err_ax
    jmp .ok_done

.try_direct_bin:
    mov bx, bp
    mov si, dx
    call trn_spawn_prog
    jnc .ok_done
    jmp .bad_cmd

.launch_bat:
    cmp byte [bTrnDosMajor], 2
    jb .chk_exp_bat125
    mov dx, bp
    xor al, al
    call k200_open
    jc .bad_cmd
    mov bx, ax
    call k200_close
    mov si, bp
    mov bx, word [bp + 390]
    call trn_push_batch_frame
    jc .err_ax
    jmp .ok_done

.chk_exp_bat125:
    mov si, bp
    mov di, bp
    add di, 260
    call k125_open
    jc .bad_cmd
    mov di, bp
    add di, 260
    call k125_close
    mov si, bp
    mov bx, word [bp + 390]
    call trn_push_batch_frame
    jc .err_ax

.ok_done:
    pop bp
    ret

.bad_cmd:
    mov ax, ERR_BAD_COMMAND
.err_ax:
    call trn_err_dos
    pop bp
    ret
@endproc

;; @proc trn_setup_redir
;; @abi dos16
;; @in DS:SI = command string (modified in-place to NUL-terminate before first < or >)
;; @out Configures stdin/stdout redirection (red_125 on DOS 1.25; DUP/DUP2 on DOS 2.00+), CF=0
;; @err CF=1 on file open/create error (already reported via trn_err_dos)
;; @clobbers ax, bx, cx, dx, di
;; @desc Parses <, >, and >> redirections while preserving FILES=8 handle discipline.
@proc trn_setup_redir
    push bp
    push si

    xor bx, bx
    xor dl, dl
.scan_r:
    mov al, byte [si]
    cmp al, 0x00
    je .j_done_r
    cmp al, 0x0D
    je .j_done_r
    cmp al, '"'
    jne .not_q
    xor dl, 1
    inc si
    jmp .scan_r
.j_done_r:
    jmp .done_ok
.not_q:
    test dl, dl
    jnz .adv_r
    cmp al, '<'
    je .got_in
    cmp al, '>'
    jne .adv_r
    jmp .got_out
.adv_r:
    inc si
    jmp .scan_r

.j_syn_r1:
    jmp .syn_err
.j_err_r1:
    jmp .err_ax
.j_not_found_r1:
    jmp .not_found

.got_in:
    mov byte [si], 0
    inc si
    mov bp, TRN_CMD_WORK
    add bp, 1200
    mov di, bp
    call trn_read_arg
    jc .j_syn_r1

    cmp byte [bTrnDosMajor], 2
    jb .in_125

    push es
    mov es, word [wTrnResSeg]
    cmp word [es:wResSavedInHdl], 0xFFFF
    pop es
    jne .open_in200
    xor bx, bx
    call k200_dup
    jc .j_err_r1
    push es
    mov es, word [wTrnResSeg]
    mov word [es:wResSavedInHdl], ax
    pop es
.open_in200:
    mov dx, bp
    xor al, al
    call k200_open
    jc .j_err_r1
    mov bx, ax
    xor cx, cx
    call k200_dup2
    pushf
    call k200_close
    popf
    jc .j_err_r1
    mov byte [bTrnRedirActive], 1
    xor dl, dl
    jmp .scan_r

.in_125:
    push si
    push es
    mov es, word [wTrnResSeg]
    mov si, bp
    mov di, addr(fcbRedirIn)
    mov ax, 0x2901
    int 0x21
    push ds
    mov ax, es
    mov ds, ax
    mov dx, addr(fcbRedirIn)
    mov ah, DOS_FCB_OPEN
    int 0x21
    mov word [fcbRedirIn + FCB.wRecSize], 1
    mov word [fcbRedirIn + FCB.dwRandRec], 0
    mov word [fcbRedirIn + FCB.dwRandRec + 2], 0
    or byte [bRedirFlags], REDIR125_IN_ACTIVE
    pop ds
    pop es
    pop si
    test al, al
    jnz .j_nf_r2
    call far [wTrnRed125InPtr]
    mov byte [bTrnRedirActive], 1
    xor dl, dl
    jmp .scan_r

.j_nf_r2:
    jmp .not_found
.j_syn_r2:
    jmp .syn_err
.j_err_r2:
    jmp .err_ax
.j_pop_err_r2:
    jmp .pop_err_ax

.got_out:
    mov byte [si], 0
    inc si
    xor cx, cx
    cmp byte [si], '>'
    jne .parse_out_fn
    inc si
    mov cx, 1
.parse_out_fn:
    push cx
    mov bp, TRN_CMD_WORK
    add bp, 1340
    mov di, bp
    call trn_read_arg
    pop cx
    jc .j_syn_r2

    cmp byte [bTrnDosMajor], 2
    jb .out_125

    push cx
    push es
    mov es, word [wTrnResSeg]
    cmp word [es:wResSavedOutHdl], 0xFFFF
    pop es
    jne .open_out200
    mov bx, 1
    call k200_dup
    jc .j_pop_err_r2
    push es
    mov es, word [wTrnResSeg]
    mov word [es:wResSavedOutHdl], ax
    pop es
.open_out200:
    pop cx
    test cx, cx
    jz .creat_out200
    mov dx, bp
    mov al, 2
    call k200_open
    jc .creat_out200
    mov bx, ax
    xor cx, cx
    xor dx, dx
    mov al, 2
    call k200_seek
    jmp .dup_out200
.creat_out200:
    mov dx, bp
    xor cx, cx
    call k200_create
    jc .j_err_r3
    mov bx, ax
.dup_out200:
    mov cx, 1
    call k200_dup2
    pushf
    call k200_close
    popf
    jc .j_err_r3
    mov byte [bTrnRedirActive], 1
    xor dl, dl
    jmp .scan_r
.j_err_r3:
    jmp .err_ax

.out_125:
    push si
    push cx
    push es
    mov es, word [wTrnResSeg]
    mov si, bp
    mov di, fcbRedirOut
    mov ax, 0x2901
    int 0x21
    pop es
    pop cx
    pop si

    push si
    push ds
    mov ds, word [wTrnResSeg]
    test cx, cx
    jz .creat_125
    mov dx, fcbRedirOut
    mov ah, DOS_FCB_OPEN
    int 0x21
    test al, al
    jnz .creat_125
    mov ax, word [fcbRedirOut + FCB.dwFileSize]
    mov word [fcbRedirOut + FCB.dwRandRec], ax
    mov ax, word [fcbRedirOut + FCB.dwFileSize + 2]
    mov word [fcbRedirOut + FCB.dwRandRec + 2], ax
    jmp .fin_out125
.creat_125:
    mov dx, fcbRedirOut
    mov ah, DOS_FCB_CREATE
    int 0x21
    test al, al
    jnz .fail_out125
    mov word [fcbRedirOut + FCB.dwRandRec], 0
    mov word [fcbRedirOut + FCB.dwRandRec + 2], 0
.fin_out125:
    mov word [fcbRedirOut + FCB.wRecSize], 1
    or byte [bRedirFlags], REDIR125_OUT_ACTIVE
    pop ds
    pop si
    call far [wTrnRed125InPtr]
    mov byte [bTrnRedirActive], 1
    xor dl, dl
    jmp .scan_r

.fail_out125:
    pop ds
    pop si
    mov ax, ERR_ACCESS_DENIED
    jmp .err_ax

.pop_err_ax:
    pop cx
    jmp .err_ax
.not_found:
    mov ax, ERR_FILE_NOT_FOUND
    jmp .err_ax
.syn_err:
    mov ax, ERR_SYNTAX
.err_ax:
    call trn_teardown_redir
    call trn_err_dos
    pop si
    pop bp
    stc
    ret

.done_ok:
    pop si
    pop bp
    clc
    ret
@endproc

;; @proc trn_teardown_redir
;; @abi dos16
;; @in [bTrnRedirActive], [wSavedStdinHdl], [wSavedStdoutHdl]
;; @out Restores original console handles (DOS 2.00+) or unhooks INT 21h (DOS 1.25)
;; @err none
;; @clobbers ax, bx, cx, dx
;; @desc Tears down per-statement I/O redirection without leaking file handles.
@proc trn_teardown_redir
    cmp byte [bTrnDosMajor], 2
    jb .td_125

    push es
    mov es, word [wTrnResSeg]
    mov bx, word [es:wResSavedInHdl]
    mov word [es:wResSavedInHdl], 0xFFFF
    pop es
    cmp bx, 0xFFFF
    je .chk_out
    xor cx, cx
    call k200_dup2
    call k200_close
.chk_out:
    push es
    mov es, word [wTrnResSeg]
    mov bx, word [es:wResSavedOutHdl]
    mov word [es:wResSavedOutHdl], 0xFFFF
    pop es
    cmp bx, 0xFFFF
    je .done
    mov cx, 1
    call k200_dup2
    call k200_close
    ret

.td_125:
    call far [wTrnRed125RmPtr]
.done:
    ret
@endproc

;; @proc trn_step_batch
;; @abi dos16
;; @in Top BATCH_FRAME in resident batchResStack
;; @out Reads next line statelessly into TRN_CMD_BUF and advances dwFileOff (or pops frame on EOF)
;; @err CF=1 if batch frame hit EOF and was popped
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Stateless batch frame stepper keeping 0 .BAT handles open during execution!
@proc trn_step_batch
    push bp
    mov bp, TRN_CMD_WORK

    ; Poll Ctrl+C latch before fetching next batch statement
    push es
    mov es, word [wTrnResSeg]
    cmp byte [es:resState + RES_STATE.bCtrlBreakLatched], 0
    je .no_break
    mov byte [es:resState + RES_STATE.bCtrlBreakLatched], 0
    pop es
    mov si, addr(szTermBatchPrompt)
    call con_write_asciiz
    mov ah, DOS_CON_IN_ECHO
    int 0x21
    push ax
    call con_write_crlf
    pop ax
    call chr_to_upper
    cmp al, 'Y'
    jne .fetch_frame
    push es
    mov es, word [wTrnResSeg]
    mov byte [es:resState + RES_STATE.bBatchDepth], 0
    pop es
    pop bp
    stc
    ret
.no_break:
    pop es

.fetch_frame:
    mov di, TRN_CMD_BUF
    call stream_read_batch_line
    pop bp
    ret
@endproc

@data db szCurRoot, "C:", 0x5C, 0
@data db szOneSpace, " "
@data db szTwoSpaces, "  ", 0
@data db szDirTag, " <DIR> ", 0
@data db szBytesFree, " bytes free", 0
@data db szVolInDrv, " Volume in drive ", 0
@data db szVolIs, " is ", 0
@data db szVolNoLbl, " has no label", 0
@data db szVolSerialPfx, " Volume Serial Number is ", 0
@data db szOneFileCopied, "        1 file(s) copied", 0
@data db szActiveCp, "Active code page: ", 0
@data db szMsgFileName, "WINCMD.MSG", 0
@data db szTermBatchPrompt, "Terminate batch job (Y/N)? ", 0
@data db szExtCom, ".COM", 0
@data db szExtExe, ".EXE", 0
@data db szExtBat, ".BAT", 0
