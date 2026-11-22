;; @file timeout.asm
;; @isa 8086
;; @desc Standalone TIMEOUT.COM countdown utility ([/T] seconds [/NOBREAK]).

@cpu 8086
@format com
@entry timeout_entry

@include "../../inc/dos_equ.inc"

;; @proc timeout_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=[/T] seconds [/NOBREAK]
;; @out Prints countdown status and waits for timer/key (yielding VM slice AX=1680h); exits 0
;; @err Exits with 1 on missing or invalid seconds argument
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for TIMEOUT.COM supporting 0-second immediate return and BIOS tick polling.
@proc timeout_entry
    call u_init
    mov byte [bHasSec], 0

.arg_lp:
    call u_skip_ws
    test al, al
    jz .args_done
    cmp al, '/'
    jne .sec_tok
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'T'
    je .sec_tok
    cmp al, 'N'
    jne .arg_lp
    mov byte [bNoBreak], 1
    jmp .arg_lp

.sec_tok:
    mov di, U_ARG_A
    call u_read_tok
    jc .args_done
    push si
    mov si, U_ARG_A
    call u_parse_u16
    pop si
    jnc .ok_sec
    jmp .bad_syn
.ok_sec:
    mov word [wWaitSec], ax
    mov byte [bHasSec], 1
    jmp .arg_lp

.help:
    mov si, addr(szToHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.args_done:
    cmp byte [bHasSec], 0
    je .bad_syn

    mov si, addr(szWaitPfx)
    call u_write_str
    mov ax, word [wWaitSec]
    call u_print_u16
    mov si, addr(szWaitSfx)
    call u_write_str
    call u_write_crlf

    cmp word [wWaitSec], 0
    je .done

    ; Poll stdin status AH=0Bh (unless /NOBREAK) + BIOS tick counter 0040:006C
    mov cx, word [wWaitSec]
.sec_lp:
    mov ax, 0x1680
    int 0x2F
    cmp byte [bNoBreak], 0
    jne .tick_step
    mov ah, 0x0B
    int 0x21
    test al, al
    jz .tick_step
    mov ah, 0x08
    int 0x21
    jmp .done
.tick_step:
    push es
    mov ax, 0x0040
    mov es, ax
    mov dx, word [es:0x006C]
    pop es
    ; Bounded poll so non-ticking emulators never hang while real BIOS/QEMU ticks advance!
    mov bx, 256
.wait_tick:
    mov ax, 0x1680
    int 0x2F
    push es
    mov ax, 0x0040
    mov es, ax
    mov ax, word [es:0x006C]
    pop es
    cmp ax, dx
    jne .nx_sec
    dec bx
    jnz .wait_tick
.nx_sec:
    loop .sec_lp

.done:
    xor al, al
    call u_exit
    ret

.bad_syn:
    mov al, 1
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bHasSec, 0
@data db bNoBreak, 0
@data dw wWaitSec, 0
@data db szWaitPfx, "Waiting for ", 0
@data db szWaitSfx, " seconds, press a key to continue ...", 0
@data db szToHelp, "TIMEOUT [/T] seconds [/NOBREAK]", 0
