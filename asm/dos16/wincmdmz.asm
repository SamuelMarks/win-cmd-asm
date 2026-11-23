;; @file wincmdmz.asm
;; @isa 8086
;; @desc Relocatable 16-bit MS-DOS MZ executable target (`WINCMDMZ.EXE`) built via FASM direct `MZ`, JWasm `-mz`/`-omf`, and NASM `-f obj` OMF.

@cpu 8086
@format mz16
@entry mz_entry
@global mz_entry

@section text

;; @proc mz_entry
;; @abi dos16
;; @in DS = ES = PSP segment, CS:IP = relocated _TEXT:0000, SS:SP = relocated _STACK:0400h
;; @out Terminates via INT 21h AH=4Ch (DOS 2.0+) or indirect far jump to PSP:0000h INT 20h (DOS 1.25)
;; @err none
;; @clobbers ax, bx, cx, dx
;; @desc Verifies relocated 8086 MZ segment execution across MS-DOS 1.25, 2.00, 4.00, 7.x, and vm8086.
@proc mz_entry
    push ds
    mov cx, es
    push cs
    pop ds
    mov word [wMzPspSeg], cx
    cld

    mov ah, 0x30
    xor al, al
    int 0x21
    mov bx, ax

    mov dx, addr(szMzBannerPrefix)
    mov ah, 0x09
    int 0x21

    test bl, bl
    jnz .fmt_ver
    mov dx, addr(szMzVer125)
    mov ah, 0x09
    int 0x21
    pop ds
    jmp far [cs:wMzPspOff]

.fmt_ver:
    mov al, bl
    add al, 0x30
    mov byte [bMzMajor], al
    mov al, bh
    xor ah, ah
    mov cl, 10
    div cl
    add al, 0x30
    add ah, 0x30
    mov byte [bMzMinorHi], al
    mov byte [bMzMinorLo], ah

    mov dx, addr(bMzMajor)
    mov ah, 0x09
    int 0x21

    pop ds
    mov ax, 0x4C00
    int 0x21
    ret
@endproc

@data dw wMzPspOff, 0x0000
@data dw wMzPspSeg, 0x0000
@data db szMzBannerPrefix, "WINCMDMZ Pure-ASM MZ Executable [MS-DOS $"
@data db szMzVer125, "1.25]", 13, 10, "$"
@data db bMzMajor, "0."
@data db bMzMinorHi, "0"
@data db bMzMinorLo, "0"
@data db szMzVerTail, "]", 13, 10, "$"
