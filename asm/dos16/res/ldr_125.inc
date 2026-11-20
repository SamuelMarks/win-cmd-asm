;; @file ldr_125.inc
;; @isa 8086
;; @desc Resident MS-DOS 1.25 manual PSP builder and .COM / relocated MZ .EXE loader.

@cpu 8086
@format inc

@data dw wChildEntryOff, 0x0100
@data dw wChildEntrySeg, 0
@data dw wChildInitSS, 0
@data dw wChildInitSP, 0xFFFE
@data dw wChildPspSeg, 0

;; @proc ldr125_read_fcb_at
;; @abi dos16
;; @in DS=CS, ES:DX = target DTA buffer, CX = byte count, [fcbResWork] open
;; @out Reads CX bytes into ES:DX via AH=1Ah + AH=27h
;; @err none
;; @clobbers ax, cx, dx
;; @desc Compact helper setting DTA to ES:DX and reading CX bytes from fcbResWork.
@proc ldr125_read_fcb_at
    push ds
    push es
    pop ds
    mov ah, DOS_SET_DTA
    int 0x21
    pop ds
    mov dx, addr(fcbResWork)
    mov ah, DOS_FCB_RAND_READ
    int 0x21
    ret
@endproc

;; @proc ldr125_int22_ret
;; @abi dos16
;; @in Invoked by MS-DOS 1.25 INT 20h / AH=00h via child PSP:0x000A (dwInt22)
;; @out Reloads TRANSIENT on safe resident stack, restores caller stack frame, returns far
;; @err none
;; @clobbers ax, bx, cx, dx, si, di, ss
;; @desc Termination continuation restoring WINCMD supervisor state after DOS 1.25 child exit.
@proc ldr125_int22_ret
    mov ax, cs
    cli
    mov ss, ax
    mov sp, addr(bResMiniStk + 36)
    sti

    push bp
    push ds
    push es
    mov ds, ax
    mov es, ax
    mov word [resState + RES_STATE.dwLastExitCode], 0
    call res_install_vecs
    call res_ensure_transient

    mov es, word [wResSavedSS]
    mov di, word [wResSavedSP]
    mov si, addr(bResSavedStk)
    mov cx, 5
    cld
    rep movsw

    mov ax, word [cs:wResSavedSS]
    mov bx, word [cs:wResSavedSP]
    cli
    mov ss, ax
    mov sp, bx
    sti
    pop es
    pop ds
    pop bp
    xor ax, ax
    clc
    retf
@endproc

;; @proc ldr125_exec_far
;; @abi dos16
;; @in [cs:fcbResWork] initialized 37B FCB, [PSP:0x0080] length+CR command tail
;; @out Loads and executes .COM or MZ .EXE on MS-DOS 1.25, reloads TRANSIENT on exit
;; @err CF=1, AX=ERR_FILE_NOT_FOUND if program open fails
;; @clobbers ax, bx, cx, dx, si, di, ss
;; @desc Manual MS-DOS 1.25 program loader supporting flat .COM and multi-segment MZ .EXE.
@proc ldr125_exec_far
    push bp
    push ds
    push es

    mov word [cs:wResSavedSS], ss
    mov word [cs:wResSavedSP], sp
    mov ax, ss
    mov ds, ax
    mov si, sp
    push cs
    pop es
    mov di, addr(bResSavedStk)
    mov cx, 5
    cld
    rep movsw

    push cs
    pop ds

    mov dx, addr(fcbResWork)
    mov ah, DOS_FCB_OPEN
    int 0x21
    test al, al
    jz .opened_ok
    jmp .err_not_found
.opened_ok:

    xor ax, ax
    mov word [fcbResWork + FCB.wRecSize], 1
    mov word [fcbResWork + FCB.dwRandRec], ax
    mov word [fcbResWork + FCB.dwRandRec + 2], ax

    ; Child PSP starts immediately above RESIDENT paragraphs
    mov ax, word [resState + RES_STATE.wPspSeg]
    add ax, (0x0010 + RES_PARAS)
    mov word [wChildPspSeg], ax
    mov dx, ax
    mov ah, DOS_CREATE_PSP
    int 0x21

    mov es, word [wChildPspSeg]
    mov ax, word [PSP.wMemTop]
    mov word [es:PSP.wMemTop], ax

    mov si, PSP.bCmdLen
    mov di, si
    mov cx, 64
    rep movsw

    mov si, (PSP.bCmdLen + 1)
    mov di, PSP.fcb1
    mov ax, 0x2901
    int 0x21
    mov di, PSP.fcb2
    mov ax, 0x2901
    int 0x21

    ; Read first 28 bytes into bResWorkBuf
    push cs
    pop es
    mov dx, addr(bResWorkBuf)
    mov cx, 28
    call ldr125_read_fcb_at

    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wMagic]
    cmp ax, 0x5A4D
    je .load_mz
    cmp ax, 0x4D5A
    je .load_mz

    ; --- Flat .COM image load at wChildPspSeg:0x0100 ---
    xor ax, ax
    mov word [fcbResWork + FCB.dwRandRec], ax
    mov word [fcbResWork + FCB.dwRandRec + 2], ax
    mov es, word [wChildPspSeg]
    mov dx, 0x0100
    mov cx, word [fcbResWork + FCB.dwFileSize]
    cmp cx, 0xFE00
    jbe .com_sz_ok
    mov cx, 0xFE00
.com_sz_ok:
    call ldr125_read_fcb_at
    mov dx, addr(fcbResWork)
    mov ah, DOS_FCB_CLOSE
    int 0x21

    mov ax, word [wChildPspSeg]
    mov es, ax
    mov word [es:0xFFFE], 0x0000
    mov word [wChildEntryOff], 0x0100
    mov word [wChildEntrySeg], ax
    mov word [wChildInitSS], ax
    mov word [wChildInitSP], 0xFFFE
    jmp .transfer_to_child

.load_mz:
    mov bp, word [wChildPspSeg]
    add bp, 0x0010

    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wHdrParas]
    mov cx, 16
    mul cx
    mov word [fcbResWork + FCB.dwRandRec], ax
    mov word [fcbResWork + FCB.dwRandRec + 2], dx

    mov si, bp
    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wPages]
    mov cx, 512
    mul cx
    sub ax, word [fcbResWork + FCB.dwRandRec]
    sbb dx, word [fcbResWork + FCB.dwRandRec + 2]
    mov di, ax

.mz_chunk_lp:
    mov cx, 0x8000
    test dx, dx
    jnz .do_mz_read
    cmp di, cx
    ja .do_mz_read
    mov cx, di
.do_mz_read:
    jcxz .mz_reloc
    push dx
    mov es, si
    xor dx, dx
    call ldr125_read_fcb_at
    pop dx
    jcxz .mz_reloc
    sub di, cx
    sbb dx, 0
    add si, 0x0800
    jmp .mz_chunk_lp

.mz_reloc:
    mov cx, word [bResWorkBuf + MZ_HDR_MIN.wRelocCnt]
    jcxz .mz_done_reloc
    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wRelocOff]
    mov word [fcbResWork + FCB.dwRandRec], ax
    mov word [fcbResWork + FCB.dwRandRec + 2], 0

.rel_lp:
    push cx
    push cs
    pop es
    mov dx, addr(bResWorkBuf + 28)
    mov cx, 4
    call ldr125_read_fcb_at
    mov di, word [bResWorkBuf + 28]
    mov ax, word [bResWorkBuf + 30]
    add ax, bp
    mov es, ax
    add word [es:di], bp
    pop cx
    loop .rel_lp

.mz_done_reloc:
    mov dx, addr(fcbResWork)
    mov ah, DOS_FCB_CLOSE
    int 0x21

    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wInitCS]
    add ax, bp
    mov word [wChildEntrySeg], ax
    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wInitIP]
    mov word [wChildEntryOff], ax
    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wInitSS]
    add ax, bp
    mov word [wChildInitSS], ax
    mov ax, word [bResWorkBuf + MZ_HDR_MIN.wInitSP]
    mov word [wChildInitSP], ax

.transfer_to_child:
    mov dx, addr(ldr125_int22_ret)
    mov ax, 0x2522
    int 0x21
    mov es, word [wChildPspSeg]
    mov word [es:PSP.dwInt22], addr(ldr125_int22_ret)
    mov word [es:PSP.dwInt22 + 2], cs

    pop es
    pop ds
    pop bp

    mov ax, word [cs:wChildInitSS]
    mov bx, word [cs:wChildInitSP]
    mov dx, word [cs:wChildPspSeg]
    cli
    mov ss, ax
    mov sp, bx
    sti
    mov ds, dx
    mov es, dx
    xor ax, ax
    xor bx, bx
    xor cx, cx
    xor dx, dx
    jmp far [cs:wChildEntryOff]

.err_not_found:
    pop es
    pop ds
    pop bp
    mov ax, ERR_FILE_NOT_FOUND
    stc
    retf
@endproc
