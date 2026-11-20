;; @file exec_200.inc
;; @isa 8086
;; @desc Resident MS-DOS 2.00+ SS:SP-destruction-proof INT 21h AX=4B00h EXEC trampoline.

@cpu 8086
@format inc

@data dw wExecRetAx, 0
@data dw wExecChildEnv, 0
@data db bExecCarry, 0
@res byte execBlk200, 14

;; @proc exec200_far
;; @abi dos16
;; @in [cs:bResWorkBuf] ASCIIZ program path, [PSP:0x0080] length+CR command tail
;; @out Spawns child via AX=4B00h, restores SS:SP safely, queries AH=4Dh, reloads TRANSIENT
;; @err CF=1, AX=ERR_* if DOS EXEC fails; CF=0, AX=child exit code on success
;; @clobbers ax, bx, cx, dx, si, di, ss
;; @desc Guards against MS-DOS 2.00 AH=4Bh destroying SS:SP and clobbering high RAM.
@proc exec200_far
    push bp
    push ds
    push es

    ; Snapshot top 10 bytes of caller stack (es, ds, bp, ret_ip, ret_cs) into bResSavedStk
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

    ; Parse FCB1 (PSP:0x005C) and FCB2 (PSP:0x006C) inside WINCMD's idle PSP area
    mov si, (PSP.bCmdLen + 1)
    mov di, PSP.fcb1
    mov ax, 0x2901
    int 0x21
    mov di, PSP.fcb2
    mov ax, 0x2901
    int 0x21

    ; Populate 14-byte EXEC_BLK in RESIDENT
    mov di, addr(execBlk200)
    mov ax, word [resState + RES_STATE.wEnvSeg]
    stosw
    mov ax, PSP.bCmdLen
    stosw
    mov ax, cs
    stosw
    mov ax, PSP.fcb1
    stosw
    mov ax, cs
    stosw
    mov ax, PSP.fcb2
    stosw
    mov ax, cs
    stosw

    mov dx, addr(bResWorkBuf)
    mov bx, addr(execBlk200)
    mov ax, 0x4B00
    int 0x21

    ; CRITICAL: First instructions upon return switch to safe RESIDENT mini-stack!
    mov byte [cs:bExecCarry], 0
    jnc .saved_cf
    mov byte [cs:bExecCarry], 1
.saved_cf:
    mov word [cs:wExecRetAx], ax
    mov ax, cs
    cli
    mov ss, ax
    mov sp, addr(bResMiniStk + 36)
    sti
    mov ds, ax
    mov es, ax

    mov ax, word [wExecChildEnv]
    test ax, ax
    jz .no_free_env
    mov es, ax
    mov ah, DOS_FREE_MEM
    int 0x21
    push cs
    pop es
    mov word [wExecChildEnv], 0
.no_free_env:

    call res_install_vecs

    cmp byte [bExecCarry], 0
    jne .after_code
    mov ah, DOS_GET_EXIT_CODE
    int 0x21
    xor ah, ah
    mov word [resState + RES_STATE.dwLastExitCode], ax
    mov word [wExecRetAx], ax

.after_code:
    call res_ensure_transient

    mov es, word [wResSavedSS]
    mov di, word [wResSavedSP]
    mov si, addr(bResSavedStk)
    mov cx, 5
    cld
    rep movsw

    mov ax, word [wResSavedSS]
    mov bx, word [wResSavedSP]
    cli
    mov ss, ax
    mov sp, bx
    sti

    mov ax, word [cs:wExecRetAx]
    mov bl, byte [cs:bExecCarry]
    pop es
    pop ds
    pop bp
    test bl, bl
    jnz .ret_err
    clc
    retf
.ret_err:
    stc
    retf
@endproc
