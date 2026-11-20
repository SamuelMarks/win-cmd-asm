;; @file int_24.inc
;; @isa 8086
;; @desc Resident INT 24h Critical Error handler (Abort/Retry/Ignore on DOS 1-2, Fail on DOS 4+).

@cpu 8086
@format inc

;; @proc int24_tty_puts
;; @abi dos16
;; @in DS:SI = NUL-terminated prompt string
;; @out Emits string via BIOS INT 10h AH=0Eh teletype without re-entering MS-DOS file I/O
;; @err none
;; @clobbers ax, bx, si
;; @desc Re-entrancy-safe BIOS console output helper used inside INT 24h critical errors.
@proc int24_tty_puts
.lp:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .lp
.done:
    ret
@endproc

;; @proc int24_isr
;; @abi dos16
;; @in AH = critical error flags, DI = error code
;; @out AL = 0 (Ignore), 1 (Retry), 2 (Abort), or 3 (Fail on DOS 4.00+)
;; @err none
;; @clobbers ax
;; @desc Handles hardware/media critical errors with era-accurate prompts and /F auto-fail.
@proc int24_isr
    cmp byte [cs:resState + RES_STATE.bAutoFail24], 0
    je .interactive
    xor al, al
    cmp byte [cs:resState + RES_STATE.bDosMajor], 4
    jb .done_iret
    mov al, 3
.done_iret:
    iret

.interactive:
    push bx
    push dx
    push si
    push ds
    push cs
    pop ds

    mov si, addr(szInt24Pfx)
    call int24_tty_puts
    mov si, addr(szInt24Ign)
    cmp byte [resState + RES_STATE.bDosMajor], 4
    jb .show_tail
    mov si, addr(szInt24Fail)
.show_tail:
    call int24_tty_puts

.read_key:
    xor ah, ah
    int 0x16
    mov dl, al
    and al, 0xDF
    mov dh, 2
    cmp al, 'A'
    je .echo_choice
    mov dh, 1
    cmp al, 'R'
    je .echo_choice
    cmp byte [resState + RES_STATE.bDosMajor], 4
    jae .chk_f
    mov dh, 0
    cmp al, 'I'
    je .echo_choice
    jmp .read_key
.chk_f:
    mov dh, 3
    cmp al, 'F'
    jne .read_key

.echo_choice:
    mov al, dl
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    mov si, addr(szInt24CrLf)
    call int24_tty_puts
    mov al, dh

    pop ds
    pop si
    pop dx
    pop bx
    iret
@endproc

@data db szInt24Pfx, 0x0D, 0x0A, "Abort, Retry, ", 0
@data db szInt24Ign, "Ignore? ", 0
@data db szInt24Fail, "Fail? ", 0
@data db szInt24CrLf, 0x0D, 0x0A, 0
