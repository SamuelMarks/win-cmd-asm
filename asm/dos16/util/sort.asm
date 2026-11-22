;; @file sort.asm
;; @isa 8086
;; @desc Standalone SORT.COM utility (/R, /+n, /UNIQUE, /O out [file]).

@cpu 8086
@format com
@entry sort_boot

@include "../../inc/dos_equ.inc"

;; @proc sort_boot
;; @abi dos16
;; @in DS=ES=CS=PSP
;; @out Jumps to sort_entry
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc 0100h entrypoint trampoline for SORT.COM.
@proc sort_boot
    jmp sort_entry
@endproc

;; @proc sort_cmp_lines
;; @abi dos16
;; @in DS:SI = line A ASCIIZ, DS:DI = line B ASCIIZ
;; @out AL = 0 (equal), 1 (A > B in sort order), 0xFF (A < B in sort order)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Compares two lines case-insensitively starting at column /+n, honoring /R reverse flag.
@proc sort_cmp_lines
    mov cx, word [wSortCol]
.adv_a:
    test cx, cx
    jz .adv_b_init
    cmp byte [si], 0
    je .adv_b_init
    inc si
    dec cx
    jmp .adv_a
.adv_b_init:
    mov cx, word [wSortCol]
.adv_b:
    test cx, cx
    jz .cmp_lp
    cmp byte [di], 0
    je .cmp_lp
    inc di
    dec cx
    jmp .adv_b

.cmp_lp:
    mov al, byte [si]
    call u_chr_upper
    mov bl, al
    mov al, byte [di]
    call u_chr_upper
    cmp bl, al
    jb .lt
    ja .gt
    test al, al
    jz .eq
    inc si
    inc di
    jmp .cmp_lp
.eq:
    xor al, al
    ret
.lt:
    mov al, 0xFF
    cmp byte [bSortRev], 0
    je .dn
    mov al, 1
    ret
.gt:
    mov al, 1
    cmp byte [bSortRev], 0
    je .dn
    mov al, 0xFF
.dn:
    ret
@endproc

;; @proc sort_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=switches (/R, /+n, /UNIQUE, /O out) and optional input file
;; @out Sorts input lines and writes them to stdout or /O <file>; exits 0
;; @err Exits with 1 on missing input file
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for SORT.COM supporting stdin/file input, /R, /+n, /UNIQUE, and /O <out>.
@proc sort_entry
    call u_init
    mov byte [U_ARG_A], 0 ; input file
    mov byte [U_ARG_B], 0 ; /O output file

.sw_lp:
    call u_skip_ws
    test al, al
    jz .load_in
    cmp al, '/'
    jne .in_tok
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    je .help
    cmp al, '+'
    je .col_sw
    call u_chr_upper
    cmp al, 'R'
    jne .chk_u
    mov byte [bSortRev], 1
    jmp .sw_lp
.chk_u:
    cmp al, 'U'
    jne .chk_o
    mov byte [bSortUniq], 1
    jmp .sw_lp
.chk_o:
    cmp al, 'O'
    jne .sw_lp
    mov di, U_ARG_B
    call u_read_tok
    jmp .sw_lp

.col_sw:
    push si
    mov si, (U_ARG_C + 2)
    call u_parse_u16
    pop si
    jc .sw_lp
    test ax, ax
    jz .sw_lp
    dec ax
    mov word [wSortCol], ax
    jmp .sw_lp

.in_tok:
    mov di, U_ARG_A
    call u_read_tok
    jmp .sw_lp

.help:
    mov si, addr(szSortHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.load_in:
    mov dx, U_ARG_A
    mov di, U_IO_A
    mov cx, 15000
    call u_load_stream
    jnc .split_lines
    mov al, 1
    call u_exit
    ret

.split_lines:
    ; Build pointer array in U_IO_B (up to 1024 words = 2048 bytes)
    mov si, U_IO_A
    mov di, U_IO_B
    xor cx, cx

.sp_lp:
    cmp byte [si], 0
    je .sort_ptrs
    cmp byte [si], 0x1A
    je .sort_ptrs
    cmp cx, 1020
    jae .sort_ptrs
    mov word [di], si
    add di, 2
    inc cx
.find_end:
    mov al, byte [si]
    test al, al
    jz .sort_ptrs
    cmp al, 0x1A
    je .term_z
    cmp al, 0x0D
    je .term_cr
    cmp al, 0x0A
    je .term_lf
    inc si
    jmp .find_end
.term_z:
    mov byte [si], 0
    jmp .sort_ptrs
.term_cr:
    mov byte [si], 0
    inc si
    cmp byte [si], 0x0A
    jne .sp_lp
    inc si
    jmp .sp_lp
.term_lf:
    mov byte [si], 0
    inc si
    jmp .sp_lp

.sort_ptrs:
    mov word [wLineCnt], cx
    cmp cx, 2
    jb .emit_sorted

    ; Insertion sort over word pointers at U_IO_B[0 .. wLineCnt-1]
    mov bx, 2 ; byte index i*2 (i = 1..n-1)
.outer_lp:
    mov ax, word [wLineCnt]
    shl ax, 1
    cmp bx, ax
    jae .emit_sorted
    mov dx, word [U_IO_B + bx] ; key_ptr
    mov word [wInnerIdx], bx
.inner_lp:
    mov si, word [wInnerIdx]
    test si, si
    jz .place_key
    push bx
    push dx
    mov si, word [U_IO_B + si - 2]
    mov di, dx
    call sort_cmp_lines
    pop dx
    pop bx
    cmp al, 1
    jne .place_key
    mov si, word [wInnerIdx]
    mov ax, word [U_IO_B + si - 2]
    mov word [U_IO_B + si], ax
    sub word [wInnerIdx], 2
    jmp .inner_lp
.place_key:
    mov si, word [wInnerIdx]
    mov word [U_IO_B + si], dx
    add bx, 2
    jmp .outer_lp

.emit_sorted:
    cmp byte [U_ARG_B], 0
    jne .emit_to_file
    xor bx, bx
.out_lp:
    mov ax, word [wLineCnt]
    shl ax, 1
    cmp bx, ax
    jae .done_ok
    cmp byte [bSortUniq], 0
    je .do_prt
    test bx, bx
    jz .do_prt
    push bx
    mov si, word [U_IO_B + bx - 2]
    mov di, word [U_IO_B + bx]
    call u_streq_nocase
    pop bx
    jnc .nx_out
.do_prt:
    push bx
    mov si, word [U_IO_B + bx]
    call u_write_str
    call u_write_crlf
    pop bx
.nx_out:
    add bx, 2
    jmp .out_lp

.emit_to_file:
    ; Flatten sorted lines into U_IO_B + 2048 and call u_save_file!
    mov di, (U_IO_B + 2048)
    xor bx, bx
.fl_lp:
    mov ax, word [wLineCnt]
    shl ax, 1
    cmp bx, ax
    jae .fl_save
    mov si, word [U_IO_B + bx]
.fl_ch:
    mov al, byte [si]
    test al, al
    jz .fl_eol
    mov byte [di], al
    inc si
    inc di
    jmp .fl_ch
.fl_eol:
    mov byte [di], 0x0D
    mov byte [di + 1], 0x0A
    add di, 2
    add bx, 2
    jmp .fl_lp
.fl_save:
    mov cx, di
    sub cx, (U_IO_B + 2048)
    mov dx, U_ARG_B
    mov si, (U_IO_B + 2048)
    call u_save_file

.done_ok:
    xor al, al
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bSortRev, 0
@data db bSortUniq, 0
@data dw wSortCol, 0
@data dw wLineCnt, 0
@data dw wInnerIdx, 0
@data db szSortHelp, "SORT [/R] [/+n] [/UNIQUE] [/O out] [file]", 0
