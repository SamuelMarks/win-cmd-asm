;; @file k_dos125.inc
;; @isa 8086
;; @desc MS-DOS 1.25 FCB/XFCB byte-granular file/search HAL and INT 25h FAT12 free-space engine.

@cpu 8086
@format inc

;; @proc k125_init_xfcb
;; @abi dos16
;; @in DS:SI = ASCIIZ filename, ES:DI = 44-byte XFCB buffer, DL = allow_wildcards (0 or 1), DH = bAttr
;; @out Initializes XFCB (0xFF + 5 zeros + bAttr + 37B FCB), CF=0 on success
;; @err CF=1, AX=ERR_PATH_NOT_FOUND if '/' or '' present; ERR_SYNTAX on illegal FAT chars
;; @clobbers ax, bx, cx, si, di
;; @desc Validates flat DOS 1.25 8.3 name and populates 44-byte XFCB + embedded 37-byte FCB.
@proc k125_init_xfcb
    push bp
    mov bp, di

    ; Scan DS:SI for forbidden directory separators ('/' or '') and illegal characters
    push si
.scan_lp:
    mov al, byte [si]
    test al, al
    jz .scan_ok
    cmp al, 0x20
    je .scan_ok
    cmp al, 0x0D
    je .scan_ok
    cmp al, '/'
    je .err_path
    cmp al, 0x5C
    je .err_path
    cmp al, '"'
    je .err_syn
    cmp al, '+'
    je .err_syn
    cmp al, ','
    je .err_syn
    cmp al, ';'
    je .err_syn
    cmp al, '<'
    je .err_syn
    cmp al, '='
    je .err_syn
    cmp al, '>'
    je .err_syn
    cmp al, '['
    je .err_syn
    cmp al, ']'
    je .err_syn
    cmp al, '|'
    je .err_syn
    test dl, dl
    jnz .next_ch
    cmp al, '*'
    je .err_syn
    cmp al, '?'
    je .err_syn
.next_ch:
    inc si
    jmp .scan_lp

.scan_ok:
    pop si
    ; Zero entire 44-byte XFCB, then populate header and call AH=29h on embedded FCB (+7)
    mov di, bp
    mov cx, 22
    xor ax, ax
    rep stosw

    mov byte [es:bp + XFCB.bFlag], 0xFF
    mov byte [es:bp + XFCB.bAttr], dh
    mov di, bp
    add di, XFCB.fcb
    mov ax, 0x2901
    int 0x21
    cmp al, 0xFF
    je .err_drive
    mov word [es:bp + XFCB.fcb + FCB.wRecSize], 1
    pop bp
    clc
    ret

.err_path:
    pop si
    pop bp
    mov ax, ERR_PATH_NOT_FOUND
    stc
    ret
.err_syn:
    pop si
    pop bp
    mov ax, ERR_SYNTAX
    stc
    ret
.err_drive:
    pop bp
    mov ax, ERR_INVALID_DRIVE
    stc
    ret
@endproc

;; @proc k125_open
;; @abi dos16
;; @in DS:SI = ASCIIZ filename, ES:DI = 44-byte XFCB buffer
;; @out Opens embedded FCB (+7) with wRecSize=1 and dwRandRec=0, CF=0 on success
;; @err CF=1, AX=ERR_FILE_NOT_FOUND or ERR_PATH_NOT_FOUND
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Opens an existing flat file on MS-DOS 1.25 with byte-granular wRecSize=1.
@proc k125_open
    push bp
    mov bp, di
    xor dx, dx
    call k125_init_xfcb
    jc .fail
    mov dx, bp
    add dx, XFCB.fcb
    mov ah, DOS_FCB_OPEN
    int 0x21
    test al, al
    jnz .not_found
    mov word [bp + XFCB.fcb + FCB.wRecSize], 1
    mov word [bp + XFCB.fcb + FCB.dwRandRec], 0
    mov word [bp + XFCB.fcb + FCB.dwRandRec + 2], 0
    pop bp
    clc
    ret
.not_found:
    mov ax, ERR_FILE_NOT_FOUND
    stc
.fail:
    pop bp
    ret
@endproc

;; @proc k125_create
;; @abi dos16
;; @in DS:SI = ASCIIZ filename, ES:DI = 44-byte XFCB buffer, DS:BX = 64-byte scratch DTA
;; @out Pre-checks ATTR_READONLY via XFCB AH=11h, creates/truncates with wRecSize=1
;; @err CF=1, AX=ERR_ACCESS_DENIED if read-only target exists, or ERR_CANNOT_MAKE
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Creates or truncates a file on MS-DOS 1.25 while honoring read-only protection.
@proc k125_create
    push bp
    mov bp, di
    push bx
    mov dx, 0x0700
    call k125_init_xfcb
    pop bx
    jc .fail

    ; Pre-check existing entry via XFCB AH=11h for ATTR_READONLY
    mov dx, bx
    mov ah, DOS_SET_DTA
    int 0x21
    mov dx, bp
    mov ah, DOS_FCB_FIND_FIRST
    int 0x21
    test al, al
    jnz .do_create
    ; In XFCB DTA result: [bx]=0xFF, [bx+6]=matched attr, [bx+7+12]=dir attr byte
    test byte [bx + 19], ATTR_READONLY
    jnz .denied

.do_create:
    mov dx, bp
    add dx, XFCB.fcb
    mov ah, DOS_FCB_CREATE
    int 0x21
    test al, al
    jnz .cant_make
    mov word [bp + XFCB.fcb + FCB.wRecSize], 1
    mov word [bp + XFCB.fcb + FCB.dwRandRec], 0
    mov word [bp + XFCB.fcb + FCB.dwRandRec + 2], 0
    pop bp
    clc
    ret

.denied:
    pop bp
    mov ax, ERR_ACCESS_DENIED
    stc
    ret
.cant_make:
    pop bp
    mov ax, ERR_CANNOT_MAKE
    stc
    ret
.fail:
    pop bp
    ret
@endproc

;; @proc k125_read
;; @abi dos16
;; @in DS:DI = 44-byte XFCB, DS:DX = buffer, CX = requested byte count
;; @out CX = actual bytes read, dwRandRec advanced automatically by AH=27h, CF=0
;; @err none (CX=0 signals EOF cleanly)
;; @clobbers ax, cx, dx
;; @desc Performs byte-granular random block read via FCB AH=27h (wRecSize=1).
@proc k125_read
    test cx, cx
    jz .zero_ok
    mov ah, DOS_SET_DTA
    int 0x21
    mov dx, di
    add dx, XFCB.fcb
    mov ah, DOS_FCB_RAND_READ
    int 0x21
.zero_ok:
    clc
    ret
@endproc

;; @proc k125_write
;; @abi dos16
;; @in DS:DI = 44-byte XFCB, DS:DX = buffer, CX = byte count (CX=0 truncates/resizes!)
;; @out CX = bytes written, CF=0 on success
;; @err CF=1, AX=ERR_DISK_FULL if AL == 0x01
;; @clobbers ax, cx, dx
;; @desc Performs byte-granular random block write or CX=0 resize via FCB AH=28h.
@proc k125_write
    mov ah, DOS_SET_DTA
    int 0x21
    mov dx, di
    add dx, XFCB.fcb
    mov ah, DOS_FCB_RAND_WRITE
    int 0x21
    cmp al, 0x01
    je .full
    clc
    ret
.full:
    mov ax, ERR_DISK_FULL
    stc
    ret
@endproc

;; @proc k125_close
;; @abi dos16
;; @in DS:DI = 44-byte XFCB
;; @out Flushes size/date/time to directory via FCB AH=10h
;; @err CF=1, AX=ERR_GEN_FAILURE if AL == 0xFF
;; @clobbers ax, dx
;; @desc Closes an open MS-DOS 1.25 FCB.
@proc k125_close
    mov dx, di
    add dx, XFCB.fcb
    mov ah, DOS_FCB_CLOSE
    int 0x21
    cmp al, 0xFF
    je .err
    clc
    ret
.err:
    mov ax, ERR_GEN_FAILURE
    stc
    ret
@endproc

;; @proc k125_delete
;; @abi dos16
;; @in DS:SI = wildcard or plain filename, ES:DI = 44-byte XFCB scratch
;; @out Deletes matching root entries via FCB AH=13h
;; @err CF=1, AX=ERR_FILE_NOT_FOUND if no files matched
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Deletes one or more flat 8.3 files on MS-DOS 1.25.
@proc k125_delete
    push bp
    mov bp, di
    mov dx, 0x0001
    call k125_init_xfcb
    jc .fail
    mov dx, bp
    add dx, XFCB.fcb
    mov ah, DOS_FCB_DELETE
    int 0x21
    test al, al
    jnz .not_found
    pop bp
    clc
    ret
.not_found:
    mov ax, ERR_FILE_NOT_FOUND
    stc
.fail:
    pop bp
    ret
@endproc

;; @proc k125_rename
;; @abi dos16
;; @in DS:SI = source pattern, DS:BX = target pattern, ES:DI = 44-byte XFCB scratch
;; @out Populates dual rename FCB (target at FCB+0x10) and invokes AH=17h
;; @err CF=1, AX=ERR_FILE_NOT_FOUND or ERR_FILE_EXISTS on collision
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Performs wildcard-capable MS-DOS 1.25 FCB rename (REN *.TXT *.BAK).
@proc k125_rename
    push bp
    mov bp, di
    push bx
    mov dx, 0x0001
    call k125_init_xfcb
    pop bx
    jc .fail

    ; Parse second pattern at DS:BX into temporary FCB at ES:(BP + XFCB.fcb + 16)
    push si
    mov si, bx
    mov di, bp
    add di, (XFCB.fcb + 16)
    mov ax, 0x2901
    int 0x21
    pop si
    cmp al, 0xFF
    je .bad_drv

    mov dx, bp
    add dx, XFCB.fcb
    mov ah, DOS_FCB_RENAME
    int 0x21
    test al, al
    jnz .ren_err
    pop bp
    clc
    ret
.bad_drv:
    pop bp
    mov ax, ERR_INVALID_DRIVE
    stc
    ret
.ren_err:
    pop bp
    mov ax, ERR_FILE_NOT_FOUND
    stc
    ret
.fail:
    pop bp
    ret
@endproc

;; @proc k125_free_space64
;; @abi dos16
;; @in DL = drive (0=default, 1=A:, ...), DS:DI = 8-byte u64 output, DS:BX = 512B sector scratch
;; @out Writes exact free byte count into [DS:DI] via AH=1Ch + INT 25h FAT12 scan
;; @err CF=1, AX=ERR_INVALID_DRIVE if drive invalid
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Computes MS-DOS 1.25 FAT12 free space guarding DS across AH=1Ch and FLAGS across INT 25h.
@proc k125_free_space64
    push bp
    mov bp, sp
    sub sp, 10
    ; [bp-2]=out_u64, [bp-4]=sec_buf, [bp-6]=bytes_per_clus, [bp-8]=tot_clus, [bp-10]=drv0
    mov word [bp - 2], di
    mov word [bp - 4], bx

    test dl, dl
    jnz .explicit_drv
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    mov byte [bp - 10], al
    mov dl, al
    inc dl
    jmp .query_1c
.explicit_drv:
    mov al, dl
    dec al
    mov byte [bp - 10], al

.query_1c:
    ; CRITICAL: INT 21h AH=1Ch overwrites DS with DOS internal FAT media byte pointer!
    push ds
    mov ah, DOS_GET_DRIVE_DATA
    int 0x21
    pop ds
    cmp al, 0xFF
    je .bad_drv

    mov word [bp - 8], dx
    xor ah, ah
    mul cx
    mov word [bp - 6], ax

    ; Read FAT12 sector 1 (and sector 2 into +512 if available, or single 512B sector) via INT 25h
    mov al, byte [bp - 10]
    mov bx, word [bp - 4]
    mov cx, 2
    mov dx, 1
    int 0x25
    pop si
    jc .bad_drv

    ; Count free 12-bit clusters (value == 0x000) across clusters 2 .. (tot_clus + 1)
    xor di, di
    mov cx, 2
    mov dx, word [bp - 8]
    inc dx

.clus_lp:
    cmp cx, dx
    ja .calc_bytes
    ; byte_off = cx + (cx >> 1)
    mov si, cx
    shr si, 1
    add si, cx
    cmp si, 1020
    jae .calc_bytes
    add si, word [bp - 4]
    mov ax, word [si]
    test cx, 1
    jz .even_clus
    shr ax, 1
    shr ax, 1
    shr ax, 1
    shr ax, 1
    jmp .chk_zero
.even_clus:
    and ax, 0x0FFF
.chk_zero:
    test ax, ax
    jnz .next_clus
    inc di
.next_clus:
    inc cx
    jmp .clus_lp

.calc_bytes:
    mov ax, di
    mul word [bp - 6]
    mov di, word [bp - 2]
    mov word [di], ax
    mov word [di + 2], dx
    mov word [di + 4], 0
    mov word [di + 6], 0

    add sp, 10
    pop bp
    clc
    ret

.bad_drv:
    add sp, 10
    pop bp
    mov ax, ERR_INVALID_DRIVE
    stc
    ret
@endproc
