;; @file u_common.inc
;; @isa 8086
;; @desc Shared CRT-free 8086 .COM runtime (DOS 1.25 FCB + 2.00..8.0 Handle I/O, CLI, env, formatters).

@cpu 8086
@format inc

;; @const U_CMD_BUF
;; @desc 128-byte NUL-terminated PSP command tail buffer at 0x1800.
@const U_CMD_BUF = 0x1800

;; @const U_ARG_A
;; @desc 128-byte parsed argument buffer A at 0x1880.
@const U_ARG_A = 0x1880

;; @const U_ARG_B
;; @desc 128-byte parsed argument buffer B at 0x1900.
@const U_ARG_B = 0x1900

;; @const U_ARG_C
;; @desc 128-byte parsed argument buffer C at 0x1980.
@const U_ARG_C = 0x1980

;; @const U_FCB_WORK
;; @desc 64-byte MS-DOS 1.25 FCB/XFCB scratch area at 0x1A00.
@const U_FCB_WORK = 0x1A00

;; @const U_DTA_WORK
;; @desc 128-byte MS-DOS DTA scratch buffer at 0x1A40.
@const U_DTA_WORK = 0x1A40

;; @const U_NUM_BUF
;; @desc 32-byte numeric formatting buffer at 0x1AC0.
@const U_NUM_BUF = 0x1AC0

;; @const U_IO_A
;; @desc 16 KiB primary file/stream buffer in .COM RAM (0x2000..0x5FFF).
@const U_IO_A = 0x2000

;; @const U_IO_B
;; @desc 16 KiB secondary file/sort/diff buffer in .COM RAM (0x6000..0x9FFF).
@const U_IO_B = 0x6000

;; @proc u_init
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=command tail
;; @out [bUDosMajor], [bUDosMinor] initialized; DS:SI = U_CMD_BUF (NUL-terminated tail)
;; @err none
;; @clobbers ax, cx, si, di
;; @desc Detects MS-DOS 1.25..8.0 version and copies PSP command tail into U_CMD_BUF.
@proc u_init
    cld
    mov ax, 0x3000
    int 0x21
    test al, al
    jnz .ver_ok
    mov al, 1
    mov ah, 25
.ver_ok:
    mov byte [bUDosMajor], al
    mov byte [bUDosMinor], ah

    xor ch, ch
    mov cl, byte [0x0080]
    cmp cl, 126
    jbe .len_ok
    mov cl, 126
.len_ok:
    mov si, 0x0081
    mov di, U_CMD_BUF
.cp_lp:
    test cx, cx
    jz .cp_dn
    mov al, byte [si]
    cmp al, 0x0D
    je .cp_dn
    mov byte [di], al
    inc si
    inc di
    dec cx
    jmp .cp_lp
.cp_dn:
    mov byte [di], 0
    mov si, U_CMD_BUF
    ret
@endproc

;; @proc u_exit
;; @abi dos16
;; @in AL = process exit code (0..255)
;; @out Terminates via AH=4Ch on DOS 2.00+ or INT 20h on DOS 1.25
;; @err none
;; @clobbers ax
;; @desc Era-safe .COM process exit supporting MS-DOS 1.25 through Windows 11 vm8086/NTVDM.
@proc u_exit
    cmp byte [bUDosMajor], 2
    jb .exit_125
    mov ah, 0x4C
    int 0x21
    ret
.exit_125:
    int 0x20
    ret
@endproc

;; @proc u_skip_ws
;; @abi dos16
;; @in DS:SI = cursor
;; @out DS:SI = first non-space/tab byte, AL = [SI]
;; @err none
;; @clobbers ax, si
;; @desc Advances SI past ASCII spaces (0x20) and tabs (0x09).
@proc u_skip_ws
.lp:
    mov al, byte [si]
    cmp al, 0x20
    je .adv
    cmp al, 0x09
    je .adv
    ret
.adv:
    inc si
    jmp .lp
@endproc

;; @proc u_read_tok
;; @abi dos16
;; @in DS:SI = command cursor, DS:DI = 128-byte destination buffer
;; @out DS:DI = NUL-terminated token (outer quotes stripped), DS:SI advanced, CF=0 if non-empty
;; @err CF=1 when cursor is at NUL
;; @clobbers ax, cx, si, di
;; @desc Extracts the next whitespace-delimited or double-quoted CLI argument.
@proc u_read_tok
    call u_skip_ws
    test al, al
    jz .empty
    xor cx, cx
    mov ah, 0
.ch_lp:
    mov al, byte [si]
    test al, al
    jz .done
    cmp al, '"'
    jne .chk_sp
    inc si
    xor ah, 1
    jmp .ch_lp
.chk_sp:
    test ah, ah
    jnz .put_ch
    cmp al, 0x20
    je .done
    cmp al, 0x09
    je .done
.put_ch:
    cmp cx, 124
    jae .sk_ch
    mov byte [di], al
    inc di
    inc cx
.sk_ch:
    inc si
    jmp .ch_lp
.done:
    mov byte [di], 0
    clc
    ret
.empty:
    mov byte [di], 0
    stc
    ret
@endproc

;; @proc u_chr_upper
;; @abi dos16
;; @in AL = ASCII character
;; @out AL = uppercase ASCII character
;; @err none
;; @clobbers ax
;; @desc Folds lowercase ASCII 'a'..'z' to 'A'..'Z'.
@proc u_chr_upper
    cmp al, 'a'
    jb .dn
    cmp al, 'z'
    ja .dn
    sub al, 0x20
.dn:
    ret
@endproc

;; @proc u_strlen
;; @abi dos16
;; @in DS:SI = NUL-terminated string
;; @out CX = byte length (SI preserved)
;; @err none
;; @clobbers ax, cx
;; @desc Measures byte length of NUL-terminated string at DS:SI without mutating SI.
@proc u_strlen
    push si
    xor cx, cx
.lp:
    mov al, byte [si]
    test al, al
    jz .dn
    inc si
    inc cx
    jmp .lp
.dn:
    pop si
    ret
@endproc

;; @proc u_streq_nocase
;; @abi dos16
;; @in DS:SI = string A, DS:DI = string B
;; @out CF=0 if equal case-insensitively, CF=1 if unequal (BX, CX, DX preserved)
;; @err CF=1 on mismatch
;; @clobbers ax, si, di
;; @desc Compares two NUL-terminated strings case-insensitively without clobbering BX/CX/DX.
@proc u_streq_nocase
.lp:
    mov al, byte [si]
    call u_chr_upper
    mov ah, al
    mov al, byte [di]
    call u_chr_upper
    cmp al, ah
    jne .no
    test al, al
    jz .yes
    inc si
    inc di
    jmp .lp
.yes:
    clc
    ret
.no:
    stc
    ret
@endproc

;; @proc u_contains_sub
;; @abi dos16
;; @in DS:SI = haystack slice, CX = haystack len, DS:DI = needle ASCIIZ, DL = 1 (nocase) or 0 (exact)
;; @out CF=0 if needle appears in haystack, CF=1 if absent
;; @err CF=1 when substring is not present
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Bounded substring search supporting both exact and case-insensitive matching.
@proc u_contains_sub
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=hay_ptr, [bp-4]=hay_len, [bp-6]=ndl_ptr, [bp-8]=ndl_len
    mov word [bp - 2], si
    mov word [bp - 4], cx
    mov word [bp - 6], di
    mov si, di
    call u_strlen
    mov word [bp - 8], cx
    test cx, cx
    jz .hit

.pos_lp:
    mov ax, word [bp - 4]
    cmp ax, word [bp - 8]
    jb .miss
    mov si, word [bp - 2]
    mov di, word [bp - 6]
    mov cx, word [bp - 8]
.cmp_ch:
    mov al, byte [si]
    mov ah, byte [di]
    test dl, dl
    jz .raw_eq
    call u_chr_upper
    mov bl, al
    mov al, ah
    call u_chr_upper
    mov ah, al
    mov al, bl
.raw_eq:
    cmp al, ah
    jne .adv_pos
    inc si
    inc di
    loop .cmp_ch
.hit:
    add sp, 8
    pop bp
    clc
    ret
.adv_pos:
    inc word [bp - 2]
    dec word [bp - 4]
    jmp .pos_lp
.miss:
    add sp, 8
    pop bp
    stc
    ret
@endproc

;; @proc u_write_buf
;; @abi dos16
;; @in DS:DX = byte buffer, CX = byte count
;; @out Writes CX bytes to stdout across DOS 1.25 (AH=02h) and DOS 2.00+ (AH=40h)
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Era-dispatching stdout writer honoring DOS 1.25 FCB and DOS 2.00+ handle redirection.
@proc u_write_buf
    test cx, cx
    jz .dn
    cmp byte [bUDosMajor], 2
    jb .d125
    mov bx, 1
    mov ah, 0x40
    int 0x21
    ret
.d125:
    mov si, dx
.lp:
    mov dl, byte [si]
    mov ah, 0x02
    int 0x21
    inc si
    loop .lp
.dn:
    ret
@endproc

;; @proc u_write_ch
;; @abi dos16
;; @in DL = character byte
;; @out Writes 1 character to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Writes a single character to stdout cleanly across MS-DOS 1.25..8.0.
@proc u_write_ch
    mov byte [U_NUM_BUF + 30], dl
    mov dx, (U_NUM_BUF + 30)
    mov cx, 1
    call u_write_buf
    ret
@endproc

;; @proc u_write_str
;; @abi dos16
;; @in DS:SI = NUL-terminated string
;; @out Writes string to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Writes an ASCIIZ string at DS:SI to stdout.
@proc u_write_str
    call u_strlen
    mov dx, si
    call u_write_buf
    ret
@endproc

;; @proc u_write_crlf
;; @abi dos16
;; @in none
;; @out Writes CR+LF (0x0D, 0x0A) to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Emits a standard DOS CR+LF newline sequence.
@proc u_write_crlf
    mov dx, addr(szUCrLf)
    mov cx, 2
    call u_write_buf
    ret
@endproc

;; @proc u_print_u32
;; @abi dos16
;; @in DX:AX = unsigned 32-bit integer
;; @out Prints decimal digits to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Formats unsigned 32-bit DX:AX in base 10 using overflow-safe 2-stage word division.
@proc u_print_u32
    mov di, (U_NUM_BUF + 20)
    mov byte [di], 0
    mov bx, 10
.div_lp:
    mov cx, ax
    mov ax, dx
    xor dx, dx
    div bx
    xchg ax, cx
    div bx
    add dl, '0'
    dec di
    mov byte [di], dl
    mov dx, cx
    mov si, ax
    or si, dx
    jnz .div_lp
    mov si, di
    call u_write_str
    ret
@endproc

;; @proc u_print_u16
;; @abi dos16
;; @in AX = unsigned 16-bit integer
;; @out Prints decimal digits to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Zero-extends AX to 32 bits and prints decimal representation.
@proc u_print_u16
    xor dx, dx
    call u_print_u32
    ret
@endproc

;; @proc u_hex_nib
;; @abi dos16
;; @in AL = nibble 0..15
;; @out AL = uppercase ASCII hex digit '0'..'9' / 'A'..'F'
;; @err none
;; @clobbers ax
;; @desc Converts a 4-bit nibble in AL into its uppercase ASCII hexadecimal character.
@proc u_hex_nib
    cmp al, 10
    jb .dig
    add al, ('A' - 10)
    ret
.dig:
    add al, '0'
    ret
@endproc

;; @proc u_print_hex8
;; @abi dos16
;; @in AL = 8-bit value
;; @out Prints 2 uppercase hex digits to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Formats AL as two uppercase hexadecimal characters.
@proc u_print_hex8
    mov bl, al
    mov cl, 4
    shr al, cl
    call u_hex_nib
    mov byte [U_NUM_BUF], al
    mov al, bl
    and al, 0x0F
    call u_hex_nib
    mov byte [U_NUM_BUF + 1], al
    mov dx, U_NUM_BUF
    mov cx, 2
    call u_write_buf
    ret
@endproc

;; @proc u_print_hex16
;; @abi dos16
;; @in AX = 16-bit value
;; @out Prints 4 uppercase hex digits to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Formats AX as four uppercase hexadecimal characters.
@proc u_print_hex16
    push ax
    mov al, ah
    call u_print_hex8
    pop ax
    call u_print_hex8
    ret
@endproc

;; @proc u_parse_u16
;; @abi dos16
;; @in DS:SI = decimal digit cursor
;; @out AX = parsed u16, DS:SI advanced, CF=0 if at least 1 digit parsed
;; @err CF=1 if [SI] is not '0'..'9'
;; @clobbers ax, bx, cx, dx, si
;; @desc Parses an unsigned 16-bit decimal integer from DS:SI.
@proc u_parse_u16
    xor ax, ax
    xor cx, cx
.lp:
    mov bl, byte [si]
    cmp bl, '0'
    jb .dn
    cmp bl, '9'
    ja .dn
    sub bl, '0'
    xor bh, bh
    mov dx, 10
    mul dx
    add ax, bx
    inc si
    inc cx
    jmp .lp
.dn:
    test cx, cx
    jz .err
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc u_load_stream
;; @abi dos16
;; @in DS:DX = ASCIIZ path (or empty [DX]==0 for stdin), DS:DI = buffer, CX = max bytes
;; @out AX = bytes loaded (NUL terminator appended at DI+AX), CF=0 on success
;; @err CF=1 on missing file or open error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Reads up to CX bytes from file or stdin across MS-DOS 1.25 FCB and 2.00+ Handle APIs.
@proc u_load_stream
    push bp
    mov bp, sp
    sub sp, 8
    ; [bp-2]=path, [bp-4]=buf, [bp-6]=max, [bp-8]=count
    mov word [bp - 2], dx
    mov word [bp - 4], di
    mov word [bp - 6], cx
    mov word [bp - 8], 0

    mov si, dx
    cmp byte [si], 0
    je .from_stdin

    cmp byte [bUDosMajor], 2
    jb .load_125

    mov ax, 0x3D00
    int 0x21
    jnc .open200_ok
    jmp .fail
.open200_ok:
    mov bx, ax
    mov dx, word [bp - 4]
    mov cx, word [bp - 6]
    mov ah, 0x3F
    int 0x21
    jc .cls_fail
    mov word [bp - 8], ax
    mov ah, 0x3E
    int 0x21
    jmp .finish_ok
.cls_fail:
    mov ah, 0x3E
    int 0x21
    jmp .fail

.from_stdin:
    cmp byte [bUDosMajor], 2
    jb .stdin_125
    xor bx, bx
    mov dx, word [bp - 4]
    mov cx, word [bp - 6]
    mov ah, 0x3F
    int 0x21
    jc .finish_ok
    mov word [bp - 8], ax
    jmp .finish_ok

.stdin_125:
    mov di, word [bp - 4]
    mov cx, word [bp - 6]
.s125_lp:
    test cx, cx
    jz .finish_ok
    push cx
    mov ah, 0x08
    int 0x21
    pop cx
    cmp al, 0x1A
    je .finish_ok
    mov byte [di], al
    inc di
    inc word [bp - 8]
    dec cx
    jmp .s125_lp

.load_125:
    mov di, U_FCB_WORK
    mov cx, 19
    xor ax, ax
    rep stosw
    mov si, word [bp - 2]
    mov di, U_FCB_WORK
    mov ax, 0x2901
    int 0x21
    cmp al, 0xFF
    je .fail
    mov dx, U_FCB_WORK
    mov ah, 0x0F
    int 0x21
    test al, al
    jnz .fail
    mov word [U_FCB_WORK + 14], 1
    mov word [U_FCB_WORK + 33], 0
    mov word [U_FCB_WORK + 35], 0
    mov dx, word [bp - 4]
    mov ah, 0x1A
    int 0x21
    mov cx, word [bp - 6]
    mov dx, U_FCB_WORK
    mov ah, 0x27
    int 0x21
    mov word [bp - 8], cx
    mov dx, 0x0080
    mov ah, 0x1A
    int 0x21
    mov dx, U_FCB_WORK
    mov ah, 0x10
    int 0x21

.finish_ok:
    mov ax, word [bp - 8]
    mov di, word [bp - 4]
    add di, ax
    mov byte [di], 0
    add sp, 8
    pop bp
    clc
    ret
.fail:
    add sp, 8
    pop bp
    stc
    ret
@endproc

;; @proc u_save_file
;; @abi dos16
;; @in DS:DX = ASCIIZ path, DS:SI = data buffer, CX = byte count
;; @out Creates/truncates file and writes CX bytes, CF=0 on success
;; @err CF=1 on create/write failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Writes CX bytes to DS:DX across MS-DOS 1.25 FCB and 2.00+ Handle APIs.
@proc u_save_file
    push bp
    mov bp, sp
    sub sp, 6
    mov word [bp - 2], dx
    mov word [bp - 4], si
    mov word [bp - 6], cx

    cmp byte [bUDosMajor], 2
    jb .save_125

    xor cx, cx
    mov ah, 0x3C
    int 0x21
    jc .fail
    mov bx, ax
    mov dx, word [bp - 4]
    mov cx, word [bp - 6]
    test cx, cx
    jz .cls_200
    mov ah, 0x40
    int 0x21
    jc .cls_err200
.cls_200:
    mov ah, 0x3E
    int 0x21
    add sp, 6
    pop bp
    clc
    ret
.cls_err200:
    mov ah, 0x3E
    int 0x21
    jmp .fail

.save_125:
    mov di, U_FCB_WORK
    mov cx, 19
    xor ax, ax
    rep stosw
    mov si, word [bp - 2]
    mov di, U_FCB_WORK
    mov ax, 0x2901
    int 0x21
    cmp al, 0xFF
    je .fail
    mov dx, U_FCB_WORK
    mov ah, 0x16
    int 0x21
    test al, al
    jnz .fail
    mov word [U_FCB_WORK + 14], 1
    mov word [U_FCB_WORK + 33], 0
    mov word [U_FCB_WORK + 35], 0
    mov dx, word [bp - 4]
    mov ah, 0x1A
    int 0x21
    mov cx, word [bp - 6]
    mov dx, U_FCB_WORK
    mov ah, 0x28
    int 0x21
    mov dx, 0x0080
    mov ah, 0x1A
    int 0x21
    mov dx, U_FCB_WORK
    mov ah, 0x10
    int 0x21
    add sp, 6
    pop bp
    clc
    ret
.fail:
    add sp, 6
    pop bp
    stc
    ret
@endproc

;; @proc u_get_env_var
;; @abi dos16
;; @in DS:SI = ASCIIZ variable name, DS:DI = 128-byte output buffer
;; @out Copies environment value from [PSP:0x002C] into DS:DI, CF=0 if found
;; @err CF=1 if variable absent or wEnvSeg == 0
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Queries the parent PSP environment block case-insensitively.
@proc u_get_env_var
    push bp
    push es
    mov bp, sp
    sub sp, 6
    mov word [bp - 2], si
    mov word [bp - 4], di
    call u_strlen
    mov word [bp - 6], cx

    mov ax, word [0x002C]
    test ax, ax
    jz .not_found
    mov es, ax
    xor bx, bx

.entry_lp:
    cmp byte [es:bx], 0
    je .not_found
    mov si, word [bp - 2]
    mov cx, word [bp - 6]
    mov di, bx
.cmp_k:
    mov al, byte [es:di]
    cmp al, '='
    je .skip_e
    test al, al
    jz .skip_e
    call u_chr_upper
    mov dl, al
    mov al, byte [si]
    call u_chr_upper
    cmp al, dl
    jne .skip_e
    inc si
    inc di
    loop .cmp_k
    cmp byte [es:di], '='
    jne .skip_e
    inc di
    mov si, word [bp - 4]
    mov cx, 124
.cp_v:
    mov al, byte [es:di]
    mov byte [si], al
    test al, al
    jz .found
    inc di
    inc si
    loop .cp_v
    mov byte [si], 0
.found:
    add sp, 6
    pop es
    pop bp
    clc
    ret

.skip_e:
    cmp byte [es:bx], 0
    je .nx_e
    inc bx
    jmp .skip_e
.nx_e:
    inc bx
    jmp .entry_lp

.not_found:
    mov di, word [bp - 4]
    mov byte [di], 0
    add sp, 6
    pop es
    pop bp
    stc
    ret
@endproc

@data db bUDosMajor, 2
@data db bUDosMinor, 0
@data db szUCrLf, 0x0D, 0x0A
