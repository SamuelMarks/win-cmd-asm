;; @file crc16.inc
;; @isa 8086
;; @desc Tableless CRC-16-CCITT (poly 0x1021, init 0xFFFF) for resident/transient integrity.

@cpu 8086
@format inc

;; @proc crc16_calc
;; @abi dos16
;; @in ES:SI = byte buffer, CX = byte count
;; @out AX = 16-bit CRC-16-CCITT digest
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Computes tableless CRC-16-CCITT over ES:SI..ES:SI+CX-1 using strict 8086 shifts.
@proc crc16_calc
    mov ax, 0xFFFF
    test cx, cx
    jz .done
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
.done:
    ret
@endproc
