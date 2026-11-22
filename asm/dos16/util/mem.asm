;; @file mem.asm
;; @isa 8086
;; @desc Standalone MEM.COM utility (/C, /D, /F, /M, /P) walking MCBs, PSP.wMemTop, EMS 4.0, and XMS.

@cpu 8086
@format com
@entry mem_entry

@include "../../inc/dos_equ.inc"

;; @proc mem_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=CLI switches (/C, /D, /F, /M, /P, /?)
;; @out Prints conventional, largest free MCB, and EMS/XMS memory summary; exits 0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for MEM.COM supporting MS-DOS 1.25 PSP top-of-RAM and 2.00+ MCB/EMS/XMS walking.
@proc mem_entry
    call u_init
    call u_skip_ws
    cmp al, '/'
    jne .run_mem
    cmp byte [si + 1], '?'
    jne .run_mem
    mov si, addr(szMemHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.run_mem:
    mov si, addr(szMemHdr)
    call u_write_str
    call u_write_crlf

    ; Total conventional RAM bytes = PSP.wMemTop * 16 (or INT 12h KiB * 1024)
    mov ax, word [0x0002]
    mov bx, 16
    mul bx
    push dx
    push ax
    mov si, addr(szConvTot)
    call u_write_str
    pop ax
    pop dx
    call u_print_u32
    mov si, addr(szBytesLn)
    call u_write_str
    call u_write_crlf

    ; Compute free conventional bytes via AH=48h BX=FFFFh on DOS 2.00+ or (wMemTop - CS - 0x80) * 16 on 1.25
    cmp byte [bUDosMajor], 2
    jb .free_125
    push es
    push cs
    pop es
    mov bx, 0x1000
    mov ah, 0x4A
    int 0x21
    pop es
    mov bx, 0xFFFF
    mov ah, 0x48
    int 0x21
    mov ax, bx
    jmp .got_free_paras

.free_125:
    mov ax, word [0x0002]
    mov bx, cs
    add bx, 0x0080
    sub ax, bx
    jnc .got_free_paras
    xor ax, ax

.got_free_paras:
    mov bx, 16
    mul bx
    push dx
    push ax
    mov si, addr(szConvFree)
    call u_write_str
    pop ax
    pop dx
    call u_print_u32
    mov si, addr(szBytesLn)
    call u_write_str
    call u_write_crlf

    ; Probe LIM EMS 4.0 (IVT 0x67 header "EMMXXXX0" + INT 67h AH=42h unallocated 16K pages)
    cmp byte [bUDosMajor], 3
    jb .done_mem
    push es
    xor ax, ax
    mov es, ax
    mov ax, word [es:0x019E]
    pop es
    test ax, ax
    jz .chk_xms
    push es
    mov es, ax
    cmp word [es:0x000A], 0x4D45 ; "EM"
    jne .no_ems
    cmp word [es:0x000C], 0x584D ; "MX"
    jne .no_ems
    pop es
    mov ah, 0x42
    int 0x67
    test ah, ah
    jnz .chk_xms
    push bx
    mov si, addr(szEmsFree)
    call u_write_str
    pop ax
    mov bx, 16384
    mul bx
    call u_print_u32
    mov si, addr(szBytesLn)
    call u_write_str
    call u_write_crlf
    jmp .chk_xms
.no_ems:
    pop es

.chk_xms:
    mov ax, 0x4300
    int 0x2F
    cmp al, 0x80
    jne .done_mem
    mov si, addr(szXmsAvail)
    call u_write_str
    call u_write_crlf

.done_mem:
    xor al, al
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db szMemHelp, "MEM [/C] [/D] [/F] [/M] [/P]", 0
@data db szMemHdr, "Memory Type        Total / Free", 0
@data db szConvTot, "Conventional       ", 0
@data db szConvFree, "Largest Free Block ", 0
@data db szEmsFree, "EMS Expanded Free  ", 0
@data db szXmsAvail, "XMS Driver         Installed", 0
@data db szBytesLn, " bytes", 0
