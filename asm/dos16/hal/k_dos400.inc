;; @file k_dos400.inc
;; @isa 8086
;; @desc MS-DOS 4.00 >32 MiB FAT16 64-bit free space, AX=6C00h, AX=6900h, AH=5Ah, AH=60h, & AH=59h.

@cpu 8086
@format inc

;; @proc k400_ext_open
;; @abi dos16
;; @in DS:SI = ASCIIZ path, BX = open mode, CX = create attr, DX = action flags
;; @out AX = handle, CX = action taken (1=opened, 2=created, 3=replaced), CF=0 on success
;; @err CF=1, AX = ERR_* on failure
;; @clobbers ax, cx
;; @desc Performs atomic MS-DOS 4.00+ Extended Open/Create via INT 21h AX=6C00h.
@proc k400_ext_open
    mov ax, DOS_EXT_OPEN
    int 0x21
    ret
@endproc

;; @proc k400_vol_serial
;; @abi dos16
;; @in BL = drive (0=default, 1=A:, ...), DS:DX = 25-byte DISK_ID4 buffer
;; @out DX:AX = 32-bit Volume Serial Number, [DISK_ID4] populated, CF=0 on success
;; @err CF=1 on DOS < 4.00 or unformatted media
;; @clobbers ax, bx, cx, dx
;; @desc Queries MS-DOS 4.00+ 32-bit Volume Serial Number and label via INT 21h AX=6900h.
@proc k400_vol_serial
    cmp byte [bTrnDosMajor], 4
    jb .unsup
    push dx
    mov bh, 0
    mov ax, DOS_DISK_SERIAL
    int 0x21
    pop bx
    jc .unsup
    mov ax, word [bx + DISK_ID4.dwSerialNum]
    mov dx, word [bx + DISK_ID4.dwSerialNum + 2]
    clc
    ret
.unsup:
    stc
    ret
@endproc

;; @proc fmt_hex16_4
;; @abi dos16
;; @in AX = 16-bit value, DS:DI = 4-byte output buffer
;; @out Writes 4 uppercase hexadecimal digits to DS:DI, advances DI by 4
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Formats a 16-bit word as 4 uppercase hex digits for Volume Serial XXXX-XXXX.
@proc fmt_hex16_4
    mov dx, ax
    mov cx, 4
.hex_lp:
    rol dx, 1
    rol dx, 1
    rol dx, 1
    rol dx, 1
    mov al, dl
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .put_dig
    add al, 7
.put_dig:
    mov byte [di], al
    inc di
    loop .hex_lp
    ret
@endproc

;; @proc k400_mktemp
;; @abi dos16
;; @in DS:DX = directory prefix ending in '' + 13 zero bytes, CX = attr
;; @out AX = open handle, [DS:DX] appended with unique filename via AH=5Ah, CF=0
;; @err CF=1, AX = ERR_* on failure
;; @clobbers ax
;; @desc Creates a guaranteed-unique temporary pipe/spool file via INT 21h AH=5Ah.
@proc k400_mktemp
    mov ah, DOS_CREATE_TEMP
    int 0x21
    ret
@endproc

;; @proc k400_truename
;; @abi dos16
;; @in DS:SI = input path, ES:DI = 128-byte canonical output buffer
;; @out Resolves SUBST/JOIN/UNC/relative path via INT 21h AH=60h, CF=0 on success
;; @err CF=1, AX = ERR_* on DOS < 3.00 or invalid path
;; @clobbers ax
;; @desc Canonicalizes a path via MS-DOS TRUENAME (INT 21h AH=60h).
@proc k400_truename
    cmp byte [bTrnDosMajor], 3
    jb .unsup
    mov ah, DOS_TRUENAME
    int 0x21
    ret
.unsup:
    mov ax, ERR_UNSUPPORTED_ON_ERA
    stc
    ret
@endproc

;; @proc k400_free_space64
;; @abi dos16
;; @in DL = drive (0=default, 1=A:, ...), DS:DI = 8-byte u64 output buffer
;; @out Computes 64-bit free byte total (AX * CX * BX) supporting >32 MiB..2 GiB FAT16
;; @err CF=1, AX=ERR_INVALID_DRIVE when AH=36h returns AX=0xFFFF
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Overflow-free 64-bit disk free space calculator for MS-DOS 2.00..6.22 volumes.
@proc k400_free_space64
    push bp
    mov bp, di
    mov ah, DOS_GET_FREE_SPACE
    int 0x21
    cmp ax, 0xFFFF
    je .bad_drv

    ; Step 1: DX:AX = AX (sectors_per_cluster) * CX (bytes_per_sector) -> up to ~32768..65536
    mul cx
    ; Step 2: 64-bit product = (DX:AX) * BX (avail_clusters)
    mov si, dx
    mul bx
    mov word [bp], ax
    mov cx, dx

    mov ax, si
    mul bx
    add ax, cx
    adc dx, 0
    mov word [bp + 2], ax
    mov word [bp + 4], dx
    mov word [bp + 6], 0

    pop bp
    clc
    ret

.bad_drv:
    pop bp
    mov ax, ERR_INVALID_DRIVE
    stc
    ret
@endproc

;; @proc k400_ext_error
;; @abi dos16
;; @in [bTrnDosMajor]
;; @out AX = extended error code (AH=59h BX=0), BH = error class, BL = action, CH = locus
;; @err Returns AX=ERR_GEN_FAILURE if called on DOS < 3.00
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Queries MS-DOS 4.00 extended error state (preserving DS/ES across INT 21h AH=59h!).
@proc k400_ext_error
    cmp byte [bTrnDosMajor], 3
    jb .fallback
    push ds
    push es
    xor bx, bx
    mov ah, DOS_GET_EXT_ERR
    int 0x21
    pop es
    pop ds
    ret
.fallback:
    mov ax, ERR_GEN_FAILURE
    ret
@endproc
