;; @file tree.asm
;; @isa 8086
;; @desc Standalone TREE.COM directory hierarchy visualizer ([drive:][path] [/F] [/A]).

@cpu 8086
@format com
@entry tree_boot

@include "../../inc/dos_equ.inc"

;; @proc tree_boot
;; @abi dos16
;; @in DS=ES=CS=PSP
;; @out Jumps to tree_entry
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc 0100h entrypoint trampoline for TREE.COM.
@proc tree_boot
    jmp tree_entry
@endproc

;; @proc tree_walk
;; @abi dos16
;; @in DS:SI = directory prefix ASCIIZ, CX = depth (0..6)
;; @out Enumerates subdirectories (and files under /F) with ASCII/CP437 tree branches
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Recursively walks FAT subdirectories using stacked 43-byte DTA frames in U_IO_A.
@proc tree_walk
    cmp cx, 6
    jbe .depth_ok
    ret
.depth_ok:
    push bp
    mov bp, sp
    sub sp, 6
    ; [bp-2]=depth, [bp-4]=dta_ptr, [bp-6]=pat_ptr
    mov word [bp - 2], cx

    ; Each recursion level gets 192 bytes in U_IO_A + depth*192: [0..63]=DTA, [64..191]=child path
    mov ax, cx
    mov bx, 192
    mul bx
    add ax, U_IO_A
    mov word [bp - 4], ax
    add ax, 64
    mov word [bp - 6], ax

    ; Build "<dir>\*.*" in [bp - 6]
    mov di, ax
.cp_p:
    mov al, byte [si]
    test al, al
    jz .add_wild
    mov byte [di], al
    inc si
    inc di
    jmp .cp_p
.add_wild:
    cmp di, word [bp - 6]
    jbe .put_star
    cmp byte [di - 1], 0x5C
    je .put_star
    cmp byte [di - 1], ':'
    je .put_star
    mov byte [di], 0x5C
    inc di
.put_star:
    mov word [bp - 6], di ; save leaf insertion offset!
    mov byte [di], '*'
    mov byte [di + 1], '.'
    mov byte [di + 2], '*'
    mov byte [di + 3], 0

    mov dx, word [bp - 4]
    mov ah, 0x1A
    int 0x21

    mov ax, word [bp - 2]
    mov bx, 192
    mul bx
    add ax, (U_IO_A + 64)
    mov dx, ax
    mov cx, 0x0016
    mov ah, 0x4E
    int 0x21
    jnc .ent_lp
    jmp .walk_done

.ent_lp:
    mov bx, word [bp - 4]
    cmp byte [bx + 30], '.'
    jne .not_dot
    jmp .next_ent
.not_dot:
    mov al, byte [bx + 21]
    test al, 0x10
    jnz .is_sub
    cmp byte [bTreeFiles], 0
    jne .show_f
    jmp .next_ent
.show_f:

    ; Print indented file entry
    mov cx, word [bp - 2]
.ind_f:
    test cx, cx
    jz .prt_f
    push cx
    mov si, addr(szTreeInd)
    call u_write_str
    pop cx
    dec cx
    jmp .ind_f
.prt_f:
    mov si, addr(szTreeSp4)
    call u_write_str
    mov si, word [bp - 4]
    add si, 30
    call u_write_str
    call u_write_crlf
    jmp .next_ent

.is_sub:
    mov cx, word [bp - 2]
.ind_d:
    test cx, cx
    jz .prt_d
    push cx
    mov si, addr(szTreeInd)
    call u_write_str
    pop cx
    dec cx
    jmp .ind_d
.prt_d:
    mov si, addr(szTreeBrAscii)
    cmp byte [bTreeAscii], 0
    jne .br_ok
    mov si, addr(szTreeBrOem)
.br_ok:
    call u_write_str
    mov si, word [bp - 4]
    add si, 30
    call u_write_str
    call u_write_crlf

    ; Build child directory path at U_IO_A + depth*192 + 64 and recurse!
    mov si, word [bp - 4]
    add si, 30
    mov di, word [bp - 6]
.cp_sub:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_sub

    mov ax, word [bp - 2]
    mov bx, 192
    mul bx
    add ax, (U_IO_A + 64)
    mov si, ax
    mov cx, word [bp - 2]
    inc cx
    call tree_walk

    ; Restore our DTA before FindNext!
    mov dx, word [bp - 4]
    mov ah, 0x1A
    int 0x21

.next_ent:
    mov ah, 0x4F
    int 0x21
    jc .walk_done
    jmp .ent_lp

.walk_done:
    add sp, 6
    pop bp
    ret
@endproc

;; @proc tree_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=optional [path] [/F] [/A]
;; @out Prints root directory banner and directory tree; exits 0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for TREE.COM supporting CP437 box-drawing, /A ASCII, and /F file listing.
@proc tree_entry
    call u_init
    mov byte [U_ARG_A], 0

.arg_lp:
    call u_skip_ws
    test al, al
    jz .run_tree
    cmp al, '/'
    jne .dir_arg
    mov di, U_ARG_B
    call u_read_tok
    mov al, byte [U_ARG_B + 1]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'F'
    jne .chk_a
    mov byte [bTreeFiles], 1
    jmp .arg_lp
.chk_a:
    cmp al, 'A'
    jne .arg_lp
    mov byte [bTreeAscii], 1
    jmp .arg_lp

.dir_arg:
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szTreeHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.run_tree:
    cmp byte [U_ARG_A], 0
    jne .have_root
    mov ah, 0x19
    int 0x21
    add al, 'A'
    mov byte [U_ARG_A], al
    mov byte [U_ARG_A + 1], ':'
    mov byte [U_ARG_A + 2], 0
    cmp byte [bUDosMajor], 2
    jb .have_root
    mov byte [U_ARG_A + 2], 0x5C
    mov si, (U_ARG_A + 3)
    xor dl, dl
    mov ah, 0x47
    int 0x21

.have_root:
    mov si, U_ARG_A
    call u_write_str
    call u_write_crlf
    cmp byte [bUDosMajor], 2
    jb .done
    mov si, U_ARG_A
    xor cx, cx
    call tree_walk

.done:
    xor al, al
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bTreeFiles, 0
@data db bTreeAscii, 0
@data db szTreeInd, "|   ", 0
@data db szTreeSp4, "    ", 0
@data db szTreeBrAscii, "+---", 0
@data db szTreeBrOem, 0xC3, 0xC4, 0xC4, 0xC4, 0
@data db szTreeHelp, "TREE [drive:][path] [/F] [/A]", 0
