;; @file ems_40.inc
;; @isa 8086
;; @desc Resident 3-tier TRANSIENT integrity checker & reloader (LIM EMS 4.0 -> XMS -> %COMSPEC%).

@cpu 8086
@format inc

@data dw wEmsFrameSeg, 0
@data dw wXmsEntryOff, 0
@data dw wXmsEntrySeg, 0
@data dw wFarCbTbl, ldr125_exec_far, exec200_far, red125_install_far, red125_remove_far

;; @proc res_crc16_check
;; @abi dos16
;; @in [cs:wTrnEntrySeg], TRN_BYTE_LEN
;; @out AX = computed CRC-16-CCITT over relocated TRANSIENT image
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Tableless resident CRC-16-CCITT verifier detecting high-RAM overwrites by children.
@proc res_crc16_check
    push es
    mov es, word [cs:wTrnEntrySeg]
    xor si, si
    mov cx, TRN_BYTE_LEN
    mov ax, 0xFFFF
.byte_lp:
    mov dh, al
    mov al, byte [es:si]
    inc si
    xor al, ah
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    shr bl, 1
    xor al, bl
    mov dl, al
    mov bl, al
    shl bl, 1
    shl bl, 1
    shl bl, 1
    shl bl, 1
    xor dh, bl
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    shl ax, 1
    xor ax, dx
    loop .byte_lp
    pop es
    ret
@endproc

;; @proc res_seed_trn_scratch
;; @abi dos16
;; @in [cs:resState], [cs:wTrnEntrySeg]
;; @out Re-populates TRANSIENT scratch globals after a reload from EMS/XMS/COMSPEC
;; @err none
;; @clobbers ax, cx, si, di
;; @desc Restores cached DOS version, echo flag, last ERRORLEVEL, and resident far callbacks.
@proc res_seed_trn_scratch
    push ds
    push es
    push cs
    pop ds
    mov es, word [wTrnEntrySeg]
    mov di, TRN_SCRATCH_BASE
    mov cx, 64
    xor ax, ax
    rep stosw

    mov ax, word [resState + RES_STATE.bDosMajor]
    mov word [es:bTrnDosMajor], ax
    mov al, byte [resState + RES_STATE.bEchoFlag]
    mov byte [es:bTrnEcho], al
    mov ax, word [resState + RES_STATE.dwLastExitCode]
    mov word [es:wTrnExitCode], ax
    mov ax, word [resState + RES_STATE.wPspSeg]
    mov word [es:wTrnResSeg], ax

    mov si, addr(wFarCbTbl)
    mov di, wTrnLdr125Ptr
    mov cx, 4
.cb_lp:
    lodsw
    stosw
    mov ax, cs
    stosw
    loop .cb_lp

    pop es
    pop ds
    ret
@endproc

;; @proc res_ensure_transient
;; @abi dos16
;; @in [cs:resState], [cs:wTrnEntrySeg]
;; @out Ensures TRANSIENT code/rodata passes CRC-16 via EMS 4.0 -> XMS -> %COMSPEC% reload
;; @err Prints floppy-swap prompt up to 3 attempts if %COMSPEC% missing or corrupt
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Authentic 3-tier transient command processor reloader across MS-DOS 1.25..8.0.
@proc res_ensure_transient
    push ds
    push es
    push cs
    pop ds

    call res_crc16_check
    cmp ax, word [resState + RES_STATE.wTrnCrc16]
    jne .need_reload
    jmp .ok_done
.need_reload:

    ; --- Tier 1: LIM EMS 4.0 Page-Frame Instant Restore (INT 67h) ---
    test byte [resState + RES_STATE.bHwFlags], HW_EMS_OK
    jz .try_xms
    mov dx, word [resState + RES_STATE.wEmsHandle]
    xor bx, bx
    mov ax, 0x4400
    int EMS_INT
    test ah, ah
    jnz .try_xms

    push ds
    mov es, word [wTrnEntrySeg]
    mov ds, word [wEmsFrameSeg]
    xor si, si
    xor di, di
    mov cx, 0x2000
    cld
    rep movsw
    pop ds

    mov dx, word [resState + RES_STATE.wEmsHandle]
    mov bx, 1
    mov ax, 0x4400
    int EMS_INT
    test ah, ah
    jnz .try_xms

    push ds
    mov ax, word [wTrnEntrySeg]
    add ax, 0x0400
    mov es, ax
    mov ds, word [wEmsFrameSeg]
    xor si, si
    xor di, di
    mov cx, ((TRN_BYTE_LEN - 0x4000) / 2)
    cld
    rep movsw
    pop ds

    call res_crc16_check
    cmp ax, word [resState + RES_STATE.wTrnCrc16]
    jne .try_xms
    jmp .reseeded_done

.try_xms:
    ; --- Tier 2: XMS Extended Memory Block Restore (INT 2Fh AX=4310h Fn 0Bh) ---
    test byte [resState + RES_STATE.bHwFlags], HW_XMS_OK
    jz .try_disk
    push cs
    pop es
    mov di, addr(bResWorkBuf)
    mov ax, TRN_BYTE_LEN
    stosw
    xor ax, ax
    stosw
    mov ax, word [resState + RES_STATE.wXmsHandle]
    stosw
    xor ax, ax
    stosw
    stosw
    stosw
    stosw
    mov ax, word [wTrnEntrySeg]
    stosw

    mov si, addr(bResWorkBuf)
    mov ah, 0x0B
    call far [cs:wXmsEntryOff]
    cmp ax, 1
    jne .try_disk

    call res_crc16_check
    cmp ax, word [resState + RES_STATE.wTrnCrc16]
    jne .try_disk
    jmp .reseeded_done

.try_disk:
    ; --- Tier 3: %COMSPEC% Disk Reload (Handle on DOS 2.00+, FCB on DOS 1.25) ---
    mov di, 3
.disk_attempt:
    push di
    cmp byte [resState + RES_STATE.bDosMajor], 2
    jb .disk_dos125

    mov dx, addr(resState + RES_STATE.szComspec)
    mov ax, 0x3D00
    int 0x21
    jnc .got_comspec200
    jmp .disk_retry
.got_comspec200:
    mov bx, ax
    xor cx, cx
    mov dx, RES_BYTE_LEN
    mov ax, 0x4200
    int 0x21

    push ds
    mov ds, word [wTrnEntrySeg]
    xor dx, dx
    mov cx, TRN_BYTE_LEN
    mov ah, DOS_READ
    int 0x21
    pop ds
    mov ah, DOS_CLOSE
    int 0x21
    jmp .disk_verify

.disk_dos125:
    push cs
    pop es
    mov si, addr(resState + RES_STATE.szComspec)
    mov di, addr(bResWorkBuf)
    mov cx, 46
.cp_cs125:
    mov al, byte [si]
    inc si
    cmp al, 0x5C
    je .cp_cs125
    cmp al, '/'
    je .cp_cs125
    mov byte [es:di], al
    inc di
    test al, al
    jz .parse_cs125
    loop .cp_cs125
    mov byte [es:di], 0
.parse_cs125:
    mov si, addr(bResWorkBuf)
    mov di, addr(fcbResWork)
    mov ax, 0x2901
    int 0x21
    mov dx, addr(fcbResWork)
    mov ah, DOS_FCB_OPEN
    int 0x21
    test al, al
    jnz .disk_retry

    mov word [fcbResWork + FCB.wRecSize], 1
    mov word [fcbResWork + FCB.dwRandRec], RES_BYTE_LEN
    mov word [fcbResWork + FCB.dwRandRec + 2], 0
    mov es, word [wTrnEntrySeg]
    xor dx, dx
    mov cx, TRN_BYTE_LEN
    call ldr125_read_fcb_at
    mov dx, addr(fcbResWork)
    mov ah, DOS_FCB_CLOSE
    int 0x21

.disk_verify:
    pop di
    call res_crc16_check
    cmp ax, word [resState + RES_STATE.wTrnCrc16]
    je .reseeded_done

    push di
.disk_retry:
    pop di
    dec di
    jz .fail_done
    mov dx, addr(szInsertComspec)
    mov ah, DOS_PRINT_STR
    int 0x21
    mov ah, DOS_CON_IN_NOECHO
    int 0x21
    cmp al, 0x1A
    je .fail_done
    jmp .disk_attempt

.reseeded_done:
    call res_seed_trn_scratch
.ok_done:
    pop es
    pop ds
    clc
    ret
.fail_done:
    pop es
    pop ds
    stc
    ret
@endproc

@data db szInsertComspec, 0x0D, 0x0A, "Insert WINCMD disk", 0x0D, 0x0A, '$'
