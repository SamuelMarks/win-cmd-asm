;; @file det_dos.inc
;; @isa 8086
;; @desc Cold-boot DOS 1.25..8.0 version, WINOLDAP, BIOS/TTY, env, EMS/XMS, and switch detector.

@cpu 8086
@format inc

;; @proc init_detect_os_hw
;; @abi dos16
;; @in [cs:wInitPsp], [cs:wInitResOff]
;; @out Populates wDosVer, bDosMajor/Minor, bDialect, bHwFlags, wEnvSeg in RES_STATE
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Detects MS-DOS 1.25 (AL=0->1.25), 2.00, 4.00, 7.x, WINOLDAP 1600h/4680h, and BIOS console.
@proc init_detect_os_hw
    push es
    xor al, al
    mov ah, DOS_GET_VERSION
    int 0x21
    test al, al
    jnz .ver_ok
    mov al, 1
    mov ah, 25
.ver_ok:
    mov es, word [cs:wInitPsp]
    mov di, word [cs:wInitResOff]
    mov word [es:di + RES_STATE.wDosVer], ax
    mov byte [es:di + RES_STATE.bDosMajor], al
    mov byte [es:di + RES_STATE.bDosMinor], ah

    ; Select default era dialect from bDosMajor
    mov dl, DIALECT_DOS125
    cmp al, 2
    jb .set_dial
    mov dl, DIALECT_DOS200
    cmp al, 4
    jb .set_dial
    mov dl, DIALECT_DOS400
    cmp al, 7
    jb .set_dial
    mov dl, DIALECT_DOS700
.set_dial:
    mov byte [es:di + RES_STATE.bDialect], dl

    ; Check console device (DOS 2.00+ IOCTL AX=4400h on stdin BX=0; default BIOS on DOS 1.25)
    mov ch, HW_BIOS_CON
    cmp byte [es:di + RES_STATE.bDosMajor], 2
    jae .probe_ioctl
    jmp .store_hw
.probe_ioctl:
    xor bx, bx
    mov ax, 0x4400
    int 0x21
    jc .chk_win
    and dx, 0x0081
    cmp dx, 0x0081
    je .chk_win
    xor ch, ch

.chk_win:
    ; Probe Windows 3.x/9x Enhanced Mode (INT 2Fh AX=1600h) and Standard Mode (AX=4680h)
    cmp byte [es:di + RES_STATE.bDosMajor], 3
    jb .chk_lfn
    push cx
    mov ax, MULTIPLEX_WIN_ENH_CHECK
    int 0x2F
    pop cx
    cmp al, 0x00
    je .chk_win_std
    cmp al, 0x80
    je .chk_win_std
    or ch, HW_WIN_ENH
    jmp .chk_lfn
.chk_win_std:
    push cx
    mov ax, MULTIPLEX_WIN_STD_CHECK
    int 0x2F
    pop cx
    test ax, ax
    jnz .chk_lfn
    or ch, HW_WIN_STD

.chk_lfn:
    cmp byte [es:di + RES_STATE.bDosMajor], 7
    jb .store_hw
    push ds
    push es
    mov ax, cs
    mov ds, ax
    mov es, ax
    push cx
    mov dx, addr(szRootProbe)
    mov di, addr(bLfnFsBuf)
    mov cx, 16
    stc
    mov ax, DOS_LFN_VOL_INFO
    int 0x21
    pop cx
    jc .probe_fat32
    test bx, 0x4000
    jz .probe_fat32
    or ch, HW_LFN_VOL
.probe_fat32:
    push cx
    mov dx, addr(szRootProbe)
    mov di, addr(bFat32ProbeBuf)
    mov cx, 44
    mov word [di], 44
    stc
    mov ax, DOS_FAT32_FREE_SPACE
    int 0x21
    pop cx
    jc .done_7x
    or ch, HW_FAT32_VOL
.done_7x:
    pop es
    pop ds

.store_hw:
    mov es, word [cs:wInitPsp]
    mov di, word [cs:wInitResOff]
    mov byte [es:di + RES_STATE.bHwFlags], ch
    mov ax, word [es:PSP.wEnvSeg]
    mov word [es:di + RES_STATE.wEnvSeg], ax
    pop es
    ret
@endproc

;; @proc init_scan_env_comspec
;; @abi dos16
;; @in [RES_STATE.wEnvSeg], [RES_STATE.bDosMajor]
;; @out Extracts COMSPEC= from environment (stopping strictly at \0\0 on DOS < 3!)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Scans parent environment block safely without reading past \0\0 on MS-DOS 2.00.
@proc init_scan_env_comspec
    push ds
    push es
    mov es, word [cs:wInitPsp]
    mov bx, word [cs:wInitResOff]
    cmp byte [es:bx + RES_STATE.bDosMajor], 2
    jb .early_done1
    mov ax, word [es:bx + RES_STATE.wEnvSeg]
    test ax, ax
    jnz .start_scan
.early_done1:
    jmp .done
.start_scan:
    mov ds, ax
    xor si, si

.env_lp:
    cmp byte [si], 0
    je .at_end_env
    ; Check for "COMSPEC=" prefix case-insensitively
    mov di, si
    mov dx, addr(szComspecKey)
    mov cx, 8
.cmp_key:
    mov al, byte [di]
    cmp al, 'a'
    jb .up_ok
    cmp al, 'z'
    ja .up_ok
    sub al, 0x20
.up_ok:
    push bx
    mov bx, dx
    cmp al, byte [cs:bx]
    pop bx
    jne .skip_entry
    inc di
    inc dx
    loop .cmp_key

    ; Found COMSPEC= at DS:DI; copy up to 79 bytes into RES_STATE.szComspec
    mov si, di
    mov di, bx
    add di, RES_STATE.szComspec
    mov cx, 79
.cp_val:
    mov al, byte [si]
    mov byte [es:di], al
    test al, al
    jz .done
    inc si
    inc di
    loop .cp_val
    mov byte [es:di], 0
    jmp .done

.skip_entry:
    cmp byte [si], 0
    je .next_entry
    inc si
    jmp .skip_entry
.next_entry:
    inc si
    jmp .env_lp

.at_end_env:
    ; CRITICAL: Stop strictly at \0\0 on MS-DOS < 3.00 (no argv[0] tail exists!)
    cmp byte [es:bx + RES_STATE.bDosMajor], 3
    jb .done
    inc si
    cmp word [si], 1
    jne .done
    add si, 2
    cmp byte [si], 0
    je .done
    mov di, bx
    add di, RES_STATE.szComspec
    mov cx, 79
.cp_self:
    mov al, byte [si]
    mov byte [es:di], al
    test al, al
    jz .done
    inc si
    inc di
    loop .cp_self
    mov byte [es:di], 0

.done:
    pop es
    pop ds
    ret
@endproc

;; @proc init_setup_ems_xms
;; @abi dos16
;; @in Relocated TRANSIENT at [RES_STATE.wTrnSeg], TRN_BYTE_LEN
;; @out Snapshots TRANSIENT into LIM EMS 4.0 ("WINCMD\0\0") or XMS if available
;; @err Falls back silently to %COMSPEC% disk reload if neither EMS nor XMS is present
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Initializes sub-millisecond EMS 4.0 or XMS transient swap on MS-DOS 4.00+.
@proc init_setup_ems_xms
    push ds
    push es
    mov es, word [cs:wInitPsp]
    mov bx, word [cs:wInitResOff]
    cmp byte [es:bx + RES_STATE.bDosMajor], 3
    jae .probe_ems
    jmp .done
.probe_ems:

    ; Probe IVT[0x67] device header at offset 0x000A for "EMMXXXX0"
    xor ax, ax
    mov ds, ax
    mov ax, word [0x019E]
    test ax, ax
    jz .j_xms
    mov ds, ax
    mov si, 0x000A
    mov di, addr(szEmmSig)
    mov cx, 8
.chk_emm:
    mov al, byte [si]
    push bx
    mov bx, di
    cmp al, byte [cs:bx]
    pop bx
    jne .j_xms
    inc si
    inc di
    loop .chk_emm
    jmp .ems_sig_ok
.j_xms:
    jmp .try_xms
.ems_sig_ok:

    ; EMS status & page frame query
    mov ah, 0x40
    int EMS_INT
    test ah, ah
    jnz .j_xms
    mov ah, 0x41
    int EMS_INT
    test ah, ah
    jnz .j_xms
    mov cx, bx

    ; Allocate 2 logical 16 KiB pages (AH=43h, BX=2 -> 32 KiB)
    mov bx, 2
    mov ah, 0x43
    int EMS_INT
    test ah, ah
    jnz .j_try_xms1

    ; Name handle "WINCMD\0\0" via LIM EMS 4.0 AX=5301h
    mov ax, cs
    mov ds, ax
    mov si, addr(szEmsName)
    mov ax, 0x5301
    int EMS_INT

    ; Map page 0 and copy first 16 KiB of TRANSIENT snapshot into EMS page frame
    xor bx, bx
    mov ax, 0x4400
    int EMS_INT
    test ah, ah
    jnz .j_try_xms1
    jmp .ems_p0_ok
.j_try_xms1:
    jmp .try_xms
.ems_p0_ok:

    mov es, word [cs:wInitPsp]
    mov di, word [cs:wInitResOff]
    mov word [es:di + RES_STATE.wEmsHandle], dx
    or byte [es:di + RES_STATE.bHwFlags], HW_EMS_OK
    mov bx, word [cs:wInitEmsFrameOff]
    mov word [es:bx], cx

    push dx
    push cx
    mov ax, word [cs:wInitPsp]
    add ax, (0x0010 + RES_PARAS)
    mov ds, ax
    mov es, cx
    xor si, si
    xor di, di
    mov cx, 0x2000 ; 8,192 words = 16,384 bytes in page 0!
    cld
    rep movsw
    pop cx
    pop dx

    ; Map logical page 1 into physical page 0 and copy tail (TRN_BYTE_LEN - 16384)!
    mov bx, 1
    mov ax, 0x4400
    int EMS_INT
    test ah, ah
    jnz .try_xms
    mov ax, word [cs:wInitPsp]
    add ax, (0x0010 + RES_PARAS + 0x0400)
    mov ds, ax
    mov es, cx
    xor si, si
    xor di, di
    mov cx, ((TRN_BYTE_LEN - 0x4000) / 2)
    cld
    rep movsw
    jmp .done

.try_xms:
    mov ax, MULTIPLEX_XMS_CHECK
    int 0x2F
    cmp al, 0x80
    je .have_xms
    jmp .done
.have_xms:
    mov ax, MULTIPLEX_XMS_ENTRY
    int 0x2F
    mov word [cs:wTmpXmsOff], bx
    mov word [cs:wTmpXmsSeg], es

    ; Allocate 24 KiB XMS block (AH=09h, DX=24)
    mov dx, 24
    mov ah, 0x09
    call far [cs:wTmpXmsOff]
    cmp ax, 1
    jne .done

    mov es, word [cs:wInitPsp]
    mov di, word [cs:wInitResOff]
    mov word [es:di + RES_STATE.wXmsHandle], dx
    mov bx, word [cs:wInitXmsPtrOff]
    mov ax, word [cs:wTmpXmsOff]
    mov word [es:bx], ax
    mov ax, word [cs:wTmpXmsSeg]
    mov word [es:bx + 2], ax

    mov ax, cs
    mov ds, ax
    mov word [xmsInitBlk + XMS_MOVE_BLK.dwLength], TRN_BYTE_LEN
    mov word [xmsInitBlk + XMS_MOVE_BLK.dwLength + 2], 0
    mov word [xmsInitBlk + XMS_MOVE_BLK.wSrcHandle], 0
    mov word [xmsInitBlk + XMS_MOVE_BLK.dwSrcOffset], 0
    mov ax, word [cs:wInitPsp]
    add ax, (0x0010 + RES_PARAS)
    mov word [xmsInitBlk + XMS_MOVE_BLK.dwSrcOffset + 2], ax
    mov word [xmsInitBlk + XMS_MOVE_BLK.wDstHandle], dx
    mov word [xmsInitBlk + XMS_MOVE_BLK.dwDstOffset], 0
    mov word [xmsInitBlk + XMS_MOVE_BLK.dwDstOffset + 2], 0
    mov si, addr(xmsInitBlk)
    mov ah, 0x0B
    call far [cs:wTmpXmsOff]
    cmp ax, 1
    jne .done
    or byte [es:di + RES_STATE.bHwFlags], HW_XMS_OK

.done:
    pop es
    pop ds
    ret
@endproc

@data db szRootProbe, "C:", 0x5C, 0
@data db szComspecKey, "COMSPEC="
@data db szEmmSig, "EMMXXXX0"
@data db szEmsName, "WINCMD", 0, 0
@data dw wTmpXmsOff, 0
@data dw wTmpXmsSeg, 0
@data dw wInitEmsFrameOff, 0
@data dw wInitXmsPtrOff, 0
@res byte bLfnFsBuf, 16
@res byte bFat32ProbeBuf, 44
@res byte xmsInitBlk, 16
