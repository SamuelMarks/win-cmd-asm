;; @file wincmd1.asm
;; @isa 8086
;; @desc Compact TINY125 (<= 20 KiB) 3-part MS-DOS 1.25 FCB-only shell (WINCMD1.COM).

@cpu 8086
@format com3part
@entry res1_boot

@include "../inc/err_code.inc"
@include "../inc/dos_equ.inc"
@include "../inc/dos_strc.inc"

;; @const T1_RESERVE_PARAS
;; @desc Paragraphs reserved at top of 64 KiB conventional RAM for TINY125 TRANSIENT (12 KiB).
@const T1_RESERVE_PARAS = 0x0300

;; @const T1_CMD_BUF
;; @desc 128-byte command line buffer inside TINY125 TRANSIENT scratch RAM.
@const T1_CMD_BUF = 0x2000

;; @const T1_FCB_A
;; @desc 64-byte FCB/XFCB scratch buffer A inside TINY125 TRANSIENT.
@const T1_FCB_A = 0x2080

;; @const T1_FCB_B
;; @desc 64-byte FCB/XFCB scratch buffer B inside TINY125 TRANSIENT.
@const T1_FCB_B = 0x20C0

;; @const T1_DTA_BUF
;; @desc 64-byte private DTA buffer inside TINY125 TRANSIENT.
@const T1_DTA_BUF = 0x2100

;; @const T1_IO_BUF
;; @desc 1,024-byte FCB file transfer / FAT12 sector buffer inside TINY125 TRANSIENT.
@const T1_IO_BUF = 0x2140

;; @const T1_STACK_TOP
;; @desc Top of stack inside TINY125 TRANSIENT (0x2FFE, even-aligned).
@const T1_STACK_TOP = 0x2FFE

@section resident

;; @proc res1_boot
;; @abi dos16
;; @in CS=DS=ES=SS=PSP
;; @out Relocates TINY125 TRANSIENT and runs command loop
;; @err Exits with 1 on fatal error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint at 0100h for compact MS-DOS 1.25 WINCMD1.COM shell.
@proc res1_boot
    cld
    mov bx, cs
    mov dx, addr(res1_post_init)
    mov ax, bx
    add ax, (0x0010 + RES_PARAS + TRN_PARAS)
    mov word [cs:wT1InitOff], 0
    mov word [cs:wT1InitSeg], ax
    jmp far [cs:wT1InitOff]
@endproc

@data db sigTiny125, "WINCMD", 0x1A, 0x01
@data dw wT1InitOff, 0
@data dw wT1InitSeg, 0

@data dw wT1TrnOff, 0
@data dw wT1TrnSeg, 0
@data dw wT1SavedSS, 0
@data dw wT1SavedSP, 0
@data db bT1OneShot, 0
@data db bT1ExitReq, 0
@data db bT1ExitCode, 0

;; @proc res1_post_init
;; @abi dos16
;; @in CS=PSP, [wT1TrnSeg] initialized by INIT
;; @out Dispatches into relocated TINY125 TRANSIENT and exits via AH=4Ch or INT 20h
;; @err none
;; @clobbers ax, bx, cx, dx, si, di, ss
;; @desc Ultra-compact resident supervisor (<= 512 bytes low RAM!) for 64 KiB 8088 systems.
@proc res1_post_init
    push ds
    push es
    mov ax, cs
    mov ds, ax
    mov es, ax

.loop:
    mov word [cs:wT1SavedSS], ss
    mov word [cs:wT1SavedSP], sp
    mov ax, word [cs:wT1TrnSeg]
    cli
    mov ss, ax
    mov sp, T1_STACK_TOP
    sti
    mov ds, ax
    mov es, ax
    call far [cs:wT1TrnOff]
    cli
    mov ss, word [cs:wT1SavedSS]
    mov sp, word [cs:wT1SavedSP]
    sti
    push cs
    pop ds
    push cs
    pop es
    cmp byte [bT1OneShot], 1
    je .exit_shell
    cmp byte [bT1ExitReq], 1
    jne .loop

.exit_shell:
    mov ax, 0x3000
    int 0x21
    pop es
    pop ds
    cmp al, 2
    jb .d125
    mov al, byte [cs:bT1ExitCode]
    mov ah, 0x4C
    int 0x21
    ret
.d125:
    int 0x20
    ret
@endproc

@section transient

;; @proc t1_entry
;; @abi dos16
;; @in CS=DS=ES=SS=wT1TrnSeg, T1_CMD_BUF populated on /C or interactive line
;; @out Executes VER, ECHO,CLS, DIR, TYPE, DEL, REN, EXIT on MS-DOS 1.25 FCB kernel
;; @err Sets [es:bT1ExitCode]=1 on error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Relocated TINY125 transient command interpreter.
@proc t1_entry
    cld
    cmp byte [T1_CMD_BUF], 0
    jne .run_buf
    ; Print "A>" prompt and read buffered line via AH=0Ah
    mov ah, 0x19
    int 0x21
    add al, 'A'
    mov dl, al
    mov ah, 0x02
    int 0x21
    mov dl, '>'
    mov ah, 0x02
    int 0x21
    mov byte [T1_IO_BUF], 120
    mov byte [T1_IO_BUF + 1], 0
    mov dx, T1_IO_BUF
    mov ah, 0x0A
    int 0x21
    call t1_crlf
    xor ch, ch
    mov cl, byte [T1_IO_BUF + 1]
    mov si, (T1_IO_BUF + 2)
    mov di, T1_CMD_BUF
    rep movsb
    mov byte [di], 0

.run_buf:
    mov si, T1_CMD_BUF
    call t1_skip_ws
    test al, al
    jnz .chk_ver
    retf

.chk_ver:
    mov di, addr(szT1Ver)
    call t1_match_kw
    jc .nx_echo
    mov si, addr(szT1Banner)
    call t1_puts
    call t1_crlf
    jmp .ok

.nx_echo:
    mov di, addr(szT1Echo)
    call t1_match_kw
    jc .nx_dir
    cmp byte [si], '.'
    je .echo_nl
    cmp byte [si], 0x20
    jne .echo_txt
    inc si
.echo_txt:
    call t1_puts
.echo_nl:
    call t1_crlf
    jmp .ok

.nx_dir:
    mov di, addr(szT1Dir)
    call t1_match_kw
    jc .nx_type
    call t1_cmd_dir
    jmp .done

.nx_type:
    mov di, addr(szT1Type)
    call t1_match_kw
    jc .nx_del
    call t1_cmd_type
    jmp .done

.nx_del:
    mov di, addr(szT1Del)
    call t1_match_kw
    jc .nx_exit
    call t1_cmd_del
    jmp .done

.nx_exit:
    mov di, addr(szT1Exit)
    call t1_match_kw
    jc .bad
    push es
    mov es, word [wT1ResSeg]
    mov byte [es:bT1ExitReq], 1
    pop es
    jmp .ok

.bad:
    mov si, addr(szT1Bad)
    call t1_puts
    call t1_crlf
    push es
    mov es, word [wT1ResSeg]
    mov byte [es:bT1ExitCode], 1
    pop es
    jmp .done

.ok:
    push es
    mov es, word [wT1ResSeg]
    mov byte [es:bT1ExitCode], 0
    pop es
.done:
    mov byte [T1_CMD_BUF], 0
    retf
@endproc

;; @proc t1_skip_ws
;; @abi dos16
;; @in DS:SI = cursor
;; @out DS:SI = non-space/non-'@' cursor, AL = [SI]
;; @err none
;; @clobbers ax, si
;; @desc Skips leading spaces, tabs, and '@'.
@proc t1_skip_ws
.lp:
    mov al, byte [si]
    cmp al, 0x20
    je .adv
    cmp al, 0x09
    je .adv
    cmp al, '@'
    je .adv
    ret
.adv:
    inc si
    jmp .lp
@endproc

;; @proc t1_puts
;; @abi dos16
;; @in DS:SI = ASCIIZ string
;; @out Prints string via INT 21h AH=02h
;; @err none
;; @clobbers ax, dx, si
;; @desc Writes NUL-terminated string on MS-DOS 1.25+.
@proc t1_puts
.lp:
    mov dl, byte [si]
    test dl, dl
    jz .dn
    mov ah, 0x02
    int 0x21
    inc si
    jmp .lp
.dn:
    ret
@endproc

;; @proc t1_crlf
;; @abi dos16
;; @in none
;; @out Prints CR+LF
;; @err none
;; @clobbers ax, dx
;; @desc Writes CR+LF via INT 21h AH=02h.
@proc t1_crlf
    mov dl, 0x0D
    mov ah, 0x02
    int 0x21
    mov dl, 0x0A
    mov ah, 0x02
    int 0x21
    ret
@endproc

;; @proc t1_match_kw
;; @abi dos16
;; @in DS:SI = line cursor, DS:DI = uppercase keyword ASCIIZ
;; @out CF=0 and SI advanced if matched at word boundary; CF=1 and SI preserved otherwise
;; @err CF=1 on mismatch
;; @clobbers ax, bx, si, di
;; @desc Matches an internal TINY125 command keyword case-insensitively.
@proc t1_match_kw
    mov bx, si
.lp:
    mov ah, byte [di]
    test ah, ah
    jz .chk_bnd
    mov al, byte [si]
    cmp al, 'a'
    jb .up
    cmp al, 'z'
    ja .up
    sub al, 0x20
.up:
    cmp al, ah
    jne .no
    inc si
    inc di
    jmp .lp
.chk_bnd:
    mov al, byte [si]
    test al, al
    jz .yes
    cmp al, 0x20
    je .yes
    cmp al, '.'
    je .yes
    cmp al, '/'
    je .yes
.no:
    mov si, bx
    stc
    ret
.yes:
    clc
    ret
@endproc

;; @proc t1_prep_fcb
;; @abi dos16
;; @in DS:SI = filename cursor, DS:DI = 37-byte FCB buffer
;; @out Parses filename via AH=29h AL=01h into DS:DI
;; @err none
;; @clobbers ax, cx, si, di
;; @desc Zeroes and populates a 37-byte MS-DOS 1.25 FCB.
@proc t1_prep_fcb
    push di
    mov cx, 19
    xor ax, ax
    rep stosw
    pop di
    call t1_skip_ws
    mov ax, 0x2901
    int 0x21
    ret
@endproc

;; @proc t1_cmd_dir
;; @abi dos16
;; @in DS:SI = optional FCB wildcard argument
;; @out Enumerates matching directory entries via FCB AH=11h/12h
;; @err Sets [bT1ExitCode]=1 if no files match
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements MS-DOS 1.25 FCB-based DIR in WINCMD1.COM.
@proc t1_cmd_dir
    call t1_skip_ws
    test al, al
    jnz .parse_arg
    mov si, addr(szT1Wild)
.parse_arg:
    mov di, T1_FCB_A
    call t1_prep_fcb
    mov dx, T1_DTA_BUF
    mov ah, 0x1A
    int 0x21
    mov dx, T1_FCB_A
    mov ah, 0x11
    int 0x21
    test al, al
    jnz .nf
.lp:
    mov si, (T1_DTA_BUF + 1)
    mov cx, 8
.n8:
    mov dl, byte [si]
    mov ah, 0x02
    int 0x21
    inc si
    loop .n8
    mov dl, ' '
    mov ah, 0x02
    int 0x21
    mov cx, 3
.e3:
    mov dl, byte [si]
    mov ah, 0x02
    int 0x21
    inc si
    loop .e3
    call t1_crlf
    mov dx, T1_FCB_A
    mov ah, 0x12
    int 0x21
    test al, al
    jz .lp
    mov dx, 0x0080
    mov ah, 0x1A
    int 0x21
    ret
.nf:
    mov dx, 0x0080
    mov ah, 0x1A
    int 0x21
    mov si, addr(szT1Nf)
    call t1_puts
    call t1_crlf
    push es
    mov es, word [wT1ResSeg]
    mov byte [es:bT1ExitCode], 1
    pop es
    ret
@endproc

;; @proc t1_cmd_type
;; @abi dos16
;; @in DS:SI = filename
;; @out Streams file via FCB wRecSize=1 random block read AH=27h
;; @err Prints File not found on missing file
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements MS-DOS 1.25 byte-exact FCB TYPE in WINCMD1.COM.
@proc t1_cmd_type
    mov di, T1_FCB_A
    call t1_prep_fcb
    mov dx, T1_FCB_A
    mov ah, 0x0F
    int 0x21
    test al, al
    jnz .nf
    mov word [T1_FCB_A + 14], 1
    mov word [T1_FCB_A + 33], 0
    mov word [T1_FCB_A + 35], 0
    mov dx, T1_IO_BUF
    mov ah, 0x1A
    int 0x21
.rd_lp:
    mov cx, 512
    mov dx, T1_FCB_A
    mov ah, 0x27
    int 0x21
    jcxz .cls
    mov si, T1_IO_BUF
.ch_lp:
    mov dl, byte [si]
    cmp dl, 0x1A
    je .cls
    mov ah, 0x02
    int 0x21
    inc si
    loop .ch_lp
    jmp .rd_lp
.cls:
    mov dx, 0x0080
    mov ah, 0x1A
    int 0x21
    mov dx, T1_FCB_A
    mov ah, 0x10
    int 0x21
    ret
.nf:
    mov si, addr(szT1Nf)
    call t1_puts
    call t1_crlf
    push es
    mov es, word [wT1ResSeg]
    mov byte [es:bT1ExitCode], 1
    pop es
    ret
@endproc

;; @proc t1_cmd_del
;; @abi dos16
;; @in DS:SI = filename/wildcard
;; @out Deletes matching file(s) via FCB AH=13h
;; @err Prints File not found if AH=13h returns FFh
;; @clobbers ax, cx, dx, si, di
;; @desc Implements MS-DOS 1.25 FCB DEL in WINCMD1.COM.
@proc t1_cmd_del
    mov di, T1_FCB_A
    call t1_prep_fcb
    mov dx, T1_FCB_A
    mov ah, 0x13
    int 0x21
    test al, al
    jnz .nf
    ret
.nf:
    mov si, addr(szT1Nf)
    call t1_puts
    call t1_crlf
    push es
    mov es, word [wT1ResSeg]
    mov byte [es:bT1ExitCode], 1
    pop es
    ret
@endproc

@data dw wT1ResSeg, 0
@data db szT1Ver, "VER", 0
@data db szT1Echo, "ECHO", 0
@data db szT1Dir, "DIR", 0
@data db szT1Type, "TYPE", 0
@data db szT1Del, "DEL", 0
@data db szT1Exit, "EXIT", 0
@data db szT1Wild, "*.*", 0
@data db szT1Banner, "WINCMD1 Tiny125 Pure-ASM Shell [MS-DOS 1.25]", 0
@data db szT1Nf, "File not found", 0
@data db szT1Bad, "Bad command or file name", 0

@section init

;; @proc init1_entry
;; @abi dos16
;; @in BX = PSP seg, DX = res1_post_init
;; @out Relocates TINY125 TRANSIENT to top of RAM, parses /C tail, jumps to BX:DX
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Cold-boot relocator for compact WINCMD1.COM (<= 20 KiB TINY125 profile).
@proc init1_entry
    mov es, bx
    mov ax, word [es:PSP.wMemTop]
    mov cx, bx
    add cx, (0x0020 + RES_PARAS)
    sub ax, T1_RESERVE_PARAS
    jc .clamp
    cmp ax, cx
    jae .seg_ok
.clamp:
    mov ax, cx
.seg_ok:
    mov word [es:wT1TrnSeg], ax
    mov word [es:wT1TrnOff], 0

    ; Relocate TRANSIENT from (BX + 0x10 + RES_PARAS) -> AX (overlap-safe!)
    push bx
    push dx
    mov dx, ax ; dst
    mov ax, bx
    add ax, (0x0010 + RES_PARAS) ; src
    push ds
    push es
    mov ds, ax
    mov es, dx
    mov cx, (TRN_BYTE_LEN / 2)
    cmp ax, dx
    jae .fwd
    mov si, (TRN_BYTE_LEN - 2)
    mov di, si
    std
    rep movsw
    cld
    jmp .rel_dn
.fwd:
    xor si, si
    xor di, di
    cld
    rep movsw
.rel_dn:
    mov word [es:wT1ResSeg], bx
    mov byte [es:T1_CMD_BUF], 0
    pop es
    pop ds
    pop dx
    pop bx

    ; Parse optional "/C <cmd>" from PSP:0x0081 into relocated T1_CMD_BUF
    push ds
    push es
    mov ds, bx
    mov es, word [wT1TrnSeg]
    xor ch, ch
    mov cl, byte [0x0080]
    mov si, 0x0081
.sk_sp:
    test cx, cx
    jz .tail_dn
    cmp byte [si], 0x20
    jne .chk_sl
    inc si
    dec cx
    jmp .sk_sp
.chk_sl:
    cmp byte [si], '/'
    jne .tail_dn
    cmp cx, 2
    jb .tail_dn
    mov al, byte [si + 1]
    and al, 0xDF
    cmp al, 'C'
    jne .tail_dn
    mov byte [bT1OneShot], 1
    add si, 2
    sub cx, 2
.sk_c_sp:
    test cx, cx
    jz .tail_dn
    cmp byte [si], 0x20
    jne .cp_cmd
    inc si
    dec cx
    jmp .sk_c_sp
.cp_cmd:
    mov di, T1_CMD_BUF
.cp_c_lp:
    test cx, cx
    jz .term_c
    mov al, byte [si]
    cmp al, 0x0D
    je .term_c
    mov byte [es:di], al
    inc si
    inc di
    dec cx
    jmp .cp_c_lp
.term_c:
    mov byte [es:di], 0
.tail_dn:
    pop es
    pop ds

    ; Jump to BX:DX (res1_post_init) with balanced stack
    mov word [cs:wInit1RetOff], dx
    mov word [cs:wInit1RetSeg], bx
    jmp far [cs:wInit1RetOff]
@endproc

@data dw wInit1RetOff, 0
@data dw wInit1RetSeg, 0
