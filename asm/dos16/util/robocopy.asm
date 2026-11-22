;; @file robocopy.asm
;; @isa 8086
;; @desc Standalone ROBOCOPY.COM directory replicator returning authentic bitmask ERRORLEVEL 0..16.

@cpu 8086
@format com
@entry robocopy_boot

@include "../../inc/dos_equ.inc"

;; @proc robocopy_boot
;; @abi dos16
;; @in DS=ES=CS=PSP
;; @out Jumps to robocopy_entry
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc 0100h entrypoint trampoline for ROBOCOPY.COM.
@proc robocopy_boot
    jmp robocopy_entry
@endproc

;; @proc rb_build_path
;; @abi dos16
;; @in DS:SI = dir ASCIIZ, DS:BX = leaf ASCIIZ, DS:DI = output buffer
;; @out Writes "<dir>\<leaf>\0" to DS:DI
;; @err none
;; @clobbers ax, dx, si, di
;; @desc Joins a directory path and leaf filename cleanly with a single backslash.
@proc rb_build_path
    mov dx, di
.cp_d:
    mov al, byte [si]
    test al, al
    jz .sep
    mov byte [di], al
    inc si
    inc di
    jmp .cp_d
.sep:
    cmp di, dx
    jbe .cp_l
    cmp byte [di - 1], 0x5C
    je .cp_l
    cmp byte [di - 1], ':'
    je .cp_l
    mov byte [di], 0x5C
    inc di
.cp_l:
    mov si, bx
.cp_lc:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_lc
    ret
@endproc

;; @proc robocopy_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=src dst [mask] [/S] [/E] [/PURGE] [/MIR] [/MOV] [/L]
;; @out Copies new/changed files, optionally purges extras (/PURGE /MIR); exits bitmask 0/1/2/3/16
;; @err Exits with 16 on missing src/dst arguments
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for ROBOCOPY.COM with authentic Windows Robocopy bitmask exit codes.
@proc robocopy_entry
    call u_init
    mov byte [U_ARG_A], 0 ; src dir
    mov byte [U_ARG_B], 0 ; dst dir
    mov byte [U_ARG_C], '*'
    mov byte [U_ARG_C + 1], '.'
    mov byte [U_ARG_C + 2], '*'
    mov byte [U_ARG_C + 3], 0

.arg_lp:
    call u_skip_ws
    test al, al
    jz .args_done
    cmp al, '/'
    jne .pos_arg
    mov di, (U_IO_B + 4096)
    call u_read_tok
    mov al, byte [U_IO_B + 4097]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'L'
    jne .chk_p
    mov byte [bRbList], 1
    jmp .arg_lp
.chk_p:
    cmp al, 'P' ; /PURGE
    jne .chk_m
    mov byte [bRbPurge], 1
    jmp .arg_lp
.chk_m:
    cmp al, 'M' ; /MIR or /MOV
    jne .arg_lp
    mov al, byte [U_IO_B + 4098]
    call u_chr_upper
    cmp al, 'I'
    jne .chk_mov
    mov byte [bRbPurge], 1
    jmp .arg_lp
.chk_mov:
    cmp al, 'O'
    jne .arg_lp
    mov byte [bRbMov], 1
    jmp .arg_lp

.pos_arg:
    cmp byte [U_ARG_A], 0
    jne .pos2
    mov di, U_ARG_A
    call u_read_tok
    jmp .arg_lp
.pos2:
    cmp byte [U_ARG_B], 0
    jne .pos3
    mov di, U_ARG_B
    call u_read_tok
    jmp .arg_lp
.pos3:
    mov di, U_ARG_C
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szRbHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.args_done:
    cmp byte [U_ARG_B], 0
    jne .start_sync
    mov al, 16
    call u_exit
    ret

.start_sync:
    cmp byte [bUDosMajor], 2
    jae .mk_dst
    mov al, 16
    call u_exit
    ret

.mk_dst:
    cmp byte [bRbList], 0
    jne .enum_src
    mov dx, U_ARG_B
    mov ah, 0x39
    int 0x21

.enum_src:
    ; Build "<src>\<mask>" at U_IO_B + 512
    mov si, U_ARG_A
    mov bx, U_ARG_C
    mov di, (U_IO_B + 512)
    call rb_build_path

    mov dx, U_DTA_WORK
    mov ah, 0x1A
    int 0x21
    mov dx, (U_IO_B + 512)
    mov cx, 0x0020
    mov ah, 0x4E
    int 0x21
    jnc .src_lp
    jmp .chk_purge

.src_lp:
    mov si, U_ARG_A
    mov bx, (U_DTA_WORK + 30)
    mov di, (U_IO_B + 768)
    call rb_build_path

    mov si, U_ARG_B
    mov bx, (U_DTA_WORK + 30)
    mov di, (U_IO_B + 1024)
    call rb_build_path

    ; Load source into U_IO_A
    mov dx, (U_IO_B + 768)
    mov di, U_IO_A
    mov cx, 15000
    call u_load_stream
    jc .nx_src
    mov word [wRbSrcLen], ax

    ; Check if destination already has identical bytes (skip copy -> exit bit 0 stays 0!)
    mov dx, (U_IO_B + 1024)
    mov di, (U_IO_B + 2048)
    mov cx, 15000
    call u_load_stream
    jc .do_cp
    cmp ax, word [wRbSrcLen]
    jne .do_cp
    mov cx, ax
    mov si, U_IO_A
    mov di, (U_IO_B + 2048)
    repe cmpsb
    je .nx_src

.do_cp:
    or byte [bRbStatus], 1 ; Bit 0 = 1+ files copied!
    mov si, (U_DTA_WORK + 30)
    call u_write_str
    call u_write_crlf
    cmp byte [bRbList], 0
    jne .nx_src
    mov dx, (U_IO_B + 1024)
    mov si, U_IO_A
    mov cx, word [wRbSrcLen]
    call u_save_file
    cmp byte [bRbMov], 0
    je .nx_src
    mov dx, (U_IO_B + 768)
    mov ah, 0x41
    int 0x21

.nx_src:
    mov dx, U_DTA_WORK
    mov ah, 0x1A
    int 0x21
    mov ah, 0x4F
    int 0x21
    jc .chk_purge
    jmp .src_lp

.chk_purge:
    cmp byte [bRbPurge], 0
    je .finish
    ; Enumerate dst\*.* and unlink any file absent from src!
    mov si, U_ARG_B
    mov bx, U_ARG_C
    mov di, (U_IO_B + 512)
    call rb_build_path

    mov dx, U_DTA_WORK
    mov ah, 0x1A
    int 0x21
    mov dx, (U_IO_B + 512)
    mov cx, 0x0020
    mov ah, 0x4E
    int 0x21
    jc .finish

.prg_lp:
    mov si, U_ARG_A
    mov bx, (U_DTA_WORK + 30)
    mov di, (U_IO_B + 768)
    call rb_build_path
    mov dx, (U_IO_B + 768)
    mov ax, 0x3D00
    int 0x21
    jc .purge_it
    mov bx, ax
    mov ah, 0x3E
    int 0x21
    jmp .nx_prg

.purge_it:
    or byte [bRbStatus], 2 ; Bit 1 = extra files detected/purged!
    cmp byte [bRbList], 0
    jne .nx_prg
    mov si, U_ARG_B
    mov bx, (U_DTA_WORK + 30)
    mov di, (U_IO_B + 1024)
    call rb_build_path
    mov dx, (U_IO_B + 1024)
    mov ah, 0x41
    int 0x21

.nx_prg:
    mov dx, U_DTA_WORK
    mov ah, 0x1A
    int 0x21
    mov ah, 0x4F
    int 0x21
    jnc .prg_lp

.finish:
    mov al, byte [bRbStatus]
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bRbList, 0
@data db bRbPurge, 0
@data db bRbMov, 0
@data db bRbStatus, 0
@data dw wRbSrcLen, 0
@data db szRbHelp, "ROBOCOPY source destination [file] [/S] [/E] [/PURGE] [/MIR] [/MOV] [/L]", 0
