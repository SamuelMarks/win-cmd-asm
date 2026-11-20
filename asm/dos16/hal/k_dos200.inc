;; @file k_dos200.inc
;; @isa 8086
;; @desc MS-DOS 2.00+ ASCIIZ handle, directory, stacked DTA search, and FILES=8 batch HAL.

@cpu 8086
@format inc

;; @proc k200_open
;; @abi dos16
;; @in DS:DX = ASCIIZ path, AL = access mode
;; @out AX = file handle, CF=0 on success (masks AL to 0..2 when bTrnDosMajor < 3!)
;; @err CF=1, AX = ERR_* on failure
;; @clobbers ax
;; @desc Opens an ASCIIZ file via AH=3Dh without passing DOS 3.0+ share bits to MS-DOS 2.00.
@proc k200_open
    cmp byte [bTrnDosMajor], 3
    jae .do_open
    and al, 0x03
    cmp al, 2
    jbe .do_open
    and al, 0x02
.do_open:
    mov ah, DOS_OPEN
    int 0x21
    ret
@endproc

;; @proc k200_create
;; @abi dos16
;; @in DS:DX = ASCIIZ path, CX = file attributes
;; @out AX = file handle, CF=0 on success
;; @err CF=1, AX = ERR_* on failure
;; @clobbers ax
;; @desc Creates or truncates an ASCIIZ file via INT 21h AH=3Ch.
@proc k200_create
    mov ah, DOS_CREAT
    int 0x21
    ret
@endproc

;; @proc k200_close
;; @abi dos16
;; @in BX = file handle
;; @out Handle closed via INT 21h AH=3Eh
;; @err CF=1, AX = ERR_* on invalid handle
;; @clobbers ax
;; @desc Closes an MS-DOS 2.00+ file handle.
@proc k200_close
    mov ah, DOS_CLOSE
    int 0x21
    ret
@endproc

;; @proc k200_read
;; @abi dos16
;; @in BX = handle, DS:DX = buffer, CX = max bytes
;; @out AX = actual bytes read, CF=0 on success
;; @err CF=1, AX = ERR_* on read failure
;; @clobbers ax
;; @desc Reads up to CX bytes from handle BX via INT 21h AH=3Fh.
@proc k200_read
    mov ah, DOS_READ
    int 0x21
    ret
@endproc

;; @proc k200_write
;; @abi dos16
;; @in BX = handle, DS:DX = buffer, CX = byte count (CX=0 truncates at current offset)
;; @out AX = actual bytes written, CF=0 on success
;; @err CF=1, AX = ERR_* on write failure
;; @clobbers ax
;; @desc Writes CX bytes to handle BX via INT 21h AH=40h.
@proc k200_write
    mov ah, DOS_WRITE
    int 0x21
    ret
@endproc

;; @proc k200_seek
;; @abi dos16
;; @in BX = handle, CX:DX = signed 32-bit offset, AL = origin (0=SET, 1=CUR, 2=END)
;; @out DX:AX = new 32-bit offset, CF=0 on success
;; @err CF=1, AX = ERR_* on invalid handle
;; @clobbers ax, dx
;; @desc Repositions file pointer on handle BX via INT 21h AH=42h.
@proc k200_seek
    mov ah, DOS_LSEEK
    int 0x21
    ret
@endproc

;; @proc k200_dup
;; @abi dos16
;; @in BX = existing handle
;; @out AX = duplicated handle, CF=0 on success
;; @err CF=1, AX = ERR_TOO_MANY_OPEN_FILES if FILES=8 table full
;; @clobbers ax
;; @desc Duplicates file handle BX via INT 21h AH=45h.
@proc k200_dup
    mov ah, DOS_DUP
    int 0x21
    ret
@endproc

;; @proc k200_dup2
;; @abi dos16
;; @in BX = source handle, CX = target handle (e.g. 0=stdin, 1=stdout)
;; @out Redirects handle CX to BX via INT 21h AH=46h
;; @err CF=1, AX = ERR_* on failure
;; @clobbers ax
;; @desc Force-duplicates handle BX onto handle CX for DOS 2.00+ redirection.
@proc k200_dup2
    mov ah, DOS_DUP2
    int 0x21
    ret
@endproc

;; @proc k200_check_dir_len
;; @abi dos16
;; @in DS:DX = ASCIIZ directory path
;; @out CF=0 if path <= 63 chars (or DOS 7.x LFN), CF=1 and AX=ERR_PATH_TOO_LONG otherwise
;; @err CF=1, AX=ERR_PATH_TOO_LONG when directory path > 63 bytes on DOS 2.00..6.22
;; @clobbers ax, cx, si
;; @desc Enforces the 63-byte MS-DOS 2.00/4.00 directory path ceiling.
@proc k200_check_dir_len
    cmp byte [bTrnDosMajor], 7
    jae .ok
    mov si, dx
    xor cx, cx
.lp:
    cmp byte [si], 0
    je .chk
    inc si
    inc cx
    jmp .lp
.chk:
    cmp cx, 63
    ja .too_long
.ok:
    clc
    ret
.too_long:
    mov ax, ERR_PATH_TOO_LONG
    stc
    ret
@endproc

;; @proc k200_mkdir
;; @abi dos16
;; @in DS:DX = ASCIIZ directory path
;; @out Directory created via AH=39h, CF=0 on success
;; @err CF=1, AX=ERR_PATH_NOT_FOUND (on DOS 1.25) or ERR_*
;; @clobbers ax, cx, si
;; @desc Creates a subdirectory on MS-DOS 2.00+ (rejecting with ERR_PATH_NOT_FOUND on 1.25).
@proc k200_mkdir
    cmp byte [bTrnDosMajor], 2
    jb .no_subdirs
    call k200_check_dir_len
    jc .done
    mov ah, DOS_MKDIR
    int 0x21
    ret
.no_subdirs:
    mov ax, ERR_PATH_NOT_FOUND
    stc
.done:
    ret
@endproc

;; @proc k200_rmdir
;; @abi dos16
;; @in DS:DX = ASCIIZ directory path
;; @out Directory removed via AH=3Ah, CF=0 on success
;; @err CF=1, AX=ERR_*
;; @clobbers ax, cx, si
;; @desc Removes an empty subdirectory on MS-DOS 2.00+.
@proc k200_rmdir
    cmp byte [bTrnDosMajor], 2
    jb .no_subdirs
    call k200_check_dir_len
    jc .done
    mov ah, DOS_RMDIR
    int 0x21
    ret
.no_subdirs:
    mov ax, ERR_PATH_NOT_FOUND
    stc
.done:
    ret
@endproc

;; @proc k200_chdir
;; @abi dos16
;; @in DS:DX = ASCIIZ directory path
;; @out Active directory changed via AH=3Bh, CF=0 on success
;; @err CF=1, AX=ERR_*
;; @clobbers ax, cx, si
;; @desc Changes current working directory on MS-DOS 2.00+.
@proc k200_chdir
    cmp byte [bTrnDosMajor], 2
    jb .no_subdirs
    call k200_check_dir_len
    jc .done
    mov ah, DOS_CHDIR
    int 0x21
    ret
.no_subdirs:
    mov ax, ERR_PATH_NOT_FOUND
    stc
.done:
    ret
@endproc

;; @proc k200_getcwd
;; @abi dos16
;; @in DL = drive (0=default, 1=A:, ...), DS:SI = 64-byte output buffer
;; @out Writes current directory without leading backslash into DS:SI, CF=0
;; @err CF=1, AX=ERR_INVALID_DRIVE on invalid drive
;; @clobbers ax, si
;; @desc Queries current directory via AH=47h (empty root string on DOS 1.25).
@proc k200_getcwd
    cmp byte [bTrnDosMajor], 2
    jb .dos125_root
    mov ah, DOS_GETCWD
    int 0x21
    ret
.dos125_root:
    mov byte [si], 0
    clc
    ret
@endproc

;; @proc k200_find_first
;; @abi dos16
;; @in DS:DX = ASCIIZ wildcard pattern, CX = search attr, DS:DI = 43-byte DTA_FIND buffer
;; @out Populates [DS:DI] while preserving caller DTA via AH=2Fh/1Ah, CF=0 on match
;; @err CF=1, AX=ERR_FILE_NOT_FOUND / ERR_NO_MORE_FILES
;; @clobbers ax, bx, cx, dx, si
;; @desc Performs stacked-DTA MS-DOS 2.00+ FindFirst (AH=4Eh) without clobbering parent searches.
@proc k200_find_first
    push bp
    mov bp, sp
    push dx
    push es
    push cx
    mov ah, DOS_GET_DTA
    int 0x21
    pop cx
    mov si, es

    mov dx, di
    mov ah, DOS_SET_DTA
    int 0x21

    mov dx, word [bp - 2]
    mov ah, DOS_FIND_FIRST
    int 0x21
    mov cx, ax
    mov al, 0
    jnc .restore_dta
    mov al, 1

.restore_dta:
    push ds
    mov ds, si
    mov dx, bx
    push ax
    mov ah, DOS_SET_DTA
    int 0x21
    pop ax
    pop ds
    pop es
    pop dx
    pop bp
    test al, al
    mov ax, cx
    jnz .err
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc k200_find_next
;; @abi dos16
;; @in DS:DI = 43-byte DTA_FIND buffer initialized by k200_find_first
;; @out Advances search in [DS:DI] while preserving caller DTA, CF=0 on match
;; @err CF=1, AX=ERR_NO_MORE_FILES when exhausted
;; @clobbers ax, bx, cx, dx, si
;; @desc Performs stacked-DTA MS-DOS 2.00+ FindNext (AH=4Fh).
@proc k200_find_next
    push es
    mov ah, DOS_GET_DTA
    int 0x21
    mov si, es

    mov dx, di
    mov ah, DOS_SET_DTA
    int 0x21

    mov ah, DOS_FIND_NEXT
    int 0x21
    mov cx, ax
    mov al, 0
    jnc .restore
    mov al, 1

.restore:
    push ds
    mov ds, si
    mov dx, bx
    push ax
    mov ah, DOS_SET_DTA
    int 0x21
    pop ax
    pop ds
    pop es
    test al, al
    mov ax, cx
    jnz .err
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc k200_bat_getline
;; @abi dos16
;; @in DS:DX = ASCIIZ .BAT path, CX:SI = 32-bit byte offset, DS:DI = 128-byte line buffer
;; @out Reads one line, closes handle IMMEDIATELY (FILES=8 safe!), CX:SI = next byte offset, CF=0
;; @err CF=1 when EOF reached (0 bytes remaining or 0x1A encountered at start)
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Stateless open-seek-read-close batch line reader keeping 0 .BAT handles open!
@proc k200_bat_getline
    push bp
    mov bp, sp
    sub sp, 6
    ; [bp-2]=line_buf, [bp-4]=off_lo, [bp-6]=off_hi
    mov word [bp - 2], di
    mov word [bp - 4], si
    mov word [bp - 6], cx

    xor al, al
    call k200_open
    jnc .opened_bat
    jmp .eof_out
.opened_bat:
    mov bx, ax

    mov cx, word [bp - 6]
    mov dx, word [bp - 4]
    xor al, al
    call k200_seek
    jc .close_eof

    mov dx, word [bp - 2]
    mov cx, 126
    call k200_read
    jc .close_eof
    mov si, ax

    ; Close .BAT handle IMMEDIATELY before parsing or executing anything!
    push si
    call k200_close
    pop si

    test si, si
    jz .eof_out

    mov di, word [bp - 2]
    cmp byte [di], 0x1A
    je .eof_out

    xor cx, cx
.scan_eol:
    cmp cx, si
    jae .terminate_ln
    mov bx, di
    add bx, cx
    mov al, byte [bx]
    cmp al, 0x1A
    je .at_ctrlz
    cmp al, 0x0D
    je .at_cr
    cmp al, 0x0A
    je .at_lf
    inc cx
    jmp .scan_eol

.at_ctrlz:
    mov byte [bx], 0
    ; Advance past 0x1A to EOF on next call
    mov cx, si
    jmp .advance_off

.at_cr:
    mov byte [bx], 0
    inc cx
    cmp cx, si
    jae .advance_off
    cmp byte [bx + 1], 0x0A
    jne .advance_off
    inc cx
    jmp .advance_off

.at_lf:
    mov byte [bx], 0
    inc cx
    jmp .advance_off

.terminate_ln:
    mov bx, di
    add bx, cx
    mov byte [bx], 0

.advance_off:
    mov si, word [bp - 4]
    mov ax, word [bp - 6]
    add si, cx
    adc ax, 0
    mov cx, ax

    add sp, 6
    pop bp
    clc
    ret

.close_eof:
    call k200_close
.eof_out:
    add sp, 6
    pop bp
    stc
    ret
@endproc
