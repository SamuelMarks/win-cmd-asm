;; @file err_msg.inc
;; @isa 8086
;; @desc Era-compatible console output (DOS 1.25 AH=02h & DOS 2.00+ AH=40h) and ERR_* messages.

@cpu 8086
@format inc

;; @proc con_write_buf
;; @abi dos16
;; @in DS:DX = byte buffer, CX = byte count
;; @out Bytes written to stdout (via AH=40h on DOS 2.00+ or AH=02h loop on DOS 1.25)
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Writes CX bytes from DS:DX to standard output across MS-DOS 1.25 through 8.0.
@proc con_write_buf
    test cx, cx
    jz .done
    cmp byte [bTrnDosMajor], 2
    jb .dos125_lp
    mov bx, 1
    mov ah, 0x40
    int 0x21
    ret
.dos125_lp:
    mov si, dx
.ch_lp:
    mov dl, byte [si]
    mov ah, 0x02
    int 0x21
    inc si
    loop .ch_lp
.done:
    ret
@endproc

;; @proc con_write_asciiz
;; @abi dos16
;; @in DS:SI = NUL-terminated string
;; @out String written to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Computes length of NUL-terminated string at DS:SI and writes it to stdout.
@proc con_write_asciiz
    mov dx, si
    xor cx, cx
.len_lp:
    cmp byte [si], 0
    je .emit
    inc si
    inc cx
    jmp .len_lp
.emit:
    call con_write_buf
    ret
@endproc

;; @proc con_write_crlf
;; @abi dos16
;; @in none
;; @out CRLF written to stdout
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Writes CR+LF (0x0D, 0x0A) to standard output.
@proc con_write_crlf
    mov dx, addr(szCrLf)
    mov cx, 2
    call con_write_buf
    ret
@endproc

;; @proc err_print_code
;; @abi dos16
;; @in AX = ERR_* code
;; @out Error message line printed to stdout/stderr
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Maps unified ERR_* code to era-authentic diagnostic string and prints it.
@proc err_print_code
    mov si, addr(szErrFileNotFound)
    cmp ax, ERR_FILE_NOT_FOUND
    je .print
    mov si, addr(szErrPathNotFound)
    cmp ax, ERR_PATH_NOT_FOUND
    je .print
    mov si, addr(szErrAccessDenied)
    cmp ax, ERR_ACCESS_DENIED
    je .print
    mov si, addr(szErrOutOfMem)
    cmp ax, ERR_OUT_OF_MEMORY
    je .print
    mov si, addr(szErrDivZero)
    cmp ax, ERR_DIV_BY_ZERO
    je .print
    mov si, addr(szErrBadNum)
    cmp ax, ERR_INVALID_NUMBER
    je .print
    mov si, addr(szErrBadCmd)
    cmp ax, ERR_BAD_COMMAND
    je .print
    mov si, addr(szErrBadDrv)
    cmp ax, ERR_INVALID_DRIVE
    je .print
    mov si, addr(szErrTooManyFiles)
    cmp ax, ERR_TOO_MANY_OPEN_FILES
    je .print
    mov si, addr(szErrPathLong)
    cmp ax, ERR_PATH_TOO_LONG
    je .print
    mov si, addr(szErrUnsupEra)
    cmp ax, ERR_UNSUPPORTED_ON_ERA
    je .print
    mov si, addr(szErrSyntax)
.print:
    call con_write_asciiz
    call con_write_crlf
    ret
@endproc
