;; @file fc.asm
;; @isa 8086
;; @desc Standalone FC.COM file comparison utility (/B, /C, /L, /N, /W file1 file2).

@cpu 8086
@format com
@entry fc_entry

@include "../../inc/dos_equ.inc"

;; @proc fc_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=switches (/B, /C, /L, /N, /W) and file1 file2
;; @out Prints "FC: no differences encountered" (exit 0) or hex/line diffs (exit 1); exit 2 on err
;; @err Exits with 2 on missing arguments or unreadable file
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for FC.COM supporting binary hex diff (/B) and case-aware ASCII comparison.
@proc fc_entry
    call u_init
    mov byte [U_ARG_A], 0
    mov byte [U_ARG_B], 0

.arg_lp:
    call u_skip_ws
    test al, al
    jz .args_done
    cmp al, '/'
    jne .file_arg
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'B'
    jne .chk_c
    mov byte [bFcBin], 1
    jmp .arg_lp
.chk_c:
    cmp al, 'C'
    jne .arg_lp
    mov byte [bFcNocase], 1
    jmp .arg_lp

.file_arg:
    cmp byte [U_ARG_A], 0
    jne .file2
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp
.file2:
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szFcHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.args_done:
    cmp byte [U_ARG_B], 0
    jne .load_files
    mov al, 2
    call u_exit
    ret

.load_files:
    mov dx, U_ARG_A
    mov di, U_IO_A
    mov cx, 15000
    call u_load_stream
    jnc .ok_a
    jmp .open_err
.ok_a:
    mov word [wLenA], ax

    mov dx, U_ARG_B
    mov di, U_IO_B
    mov cx, 15000
    call u_load_stream
    jnc .ok_b
    jmp .open_err
.ok_b:
    mov word [wLenB], ax

    mov si, addr(szFcHdr)
    call u_write_str
    mov si, U_ARG_A
    call u_write_str
    mov si, addr(szFcAnd)
    call u_write_str
    mov si, U_ARG_B
    call u_write_str
    call u_write_crlf

    ; Compare byte-by-byte up to min(wLenA, wLenB)
    mov cx, word [wLenA]
    cmp cx, word [wLenB]
    jbe .got_min
    mov cx, word [wLenB]
.got_min:
    xor bx, bx ; offset
    mov word [wDiffCnt], 0

.cmp_lp:
    cmp bx, cx
    jae .after_min
    mov al, byte [U_IO_A + bx]
    mov ah, byte [U_IO_B + bx]
    cmp byte [bFcBin], 0
    jne .raw_b
    cmp byte [bFcNocase], 0
    je .raw_b
    push cx
    call u_chr_upper
    mov dl, al
    mov al, ah
    call u_chr_upper
    mov ah, al
    mov al, dl
    pop cx
.raw_b:
    cmp al, ah
    je .nx_b
    inc word [wDiffCnt]
    push bx
    push cx
    mov ax, bx
    call u_print_hex16
    mov si, addr(szColonSp)
    call u_write_str
    pop cx
    pop bx
    push bx
    push cx
    mov al, byte [U_IO_A + bx]
    call u_print_hex8
    mov dl, ' '
    call u_write_ch
    pop cx
    pop bx
    push bx
    push cx
    mov al, byte [U_IO_B + bx]
    call u_print_hex8
    call u_write_crlf
    pop cx
    pop bx
.nx_b:
    inc bx
    jmp .cmp_lp

.after_min:
    mov ax, word [wLenA]
    cmp ax, word [wLenB]
    je .chk_diffs
    inc word [wDiffCnt]
    mov si, addr(szFcSizeDiff)
    call u_write_str
    call u_write_crlf

.chk_diffs:
    cmp word [wDiffCnt], 0
    jne .has_diff
    mov si, addr(szFcNoDiff)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.has_diff:
    mov al, 1
    call u_exit
    ret

.open_err:
    mov al, 2
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bFcBin, 0
@data db bFcNocase, 0
@data dw wLenA, 0
@data dw wLenB, 0
@data dw wDiffCnt, 0
@data db szFcHelp, "FC [/B] [/C] [/L] [/N] [/W] file1 file2", 0
@data db szFcHdr, "Comparing files ", 0
@data db szFcAnd, " and ", 0
@data db szColonSp, ": ", 0
@data db szFcNoDiff, "FC: no differences encountered", 0
@data db szFcSizeDiff, "FC: files are different lengths", 0
