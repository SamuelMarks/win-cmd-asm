;; @file reg.asm
;; @isa 8086
;; @desc Standalone REG.COM utility (QUERY, ADD, DELETE, EXPORT, IMPORT over WINCMD.REG).

@cpu 8086
@format com
@entry reg_entry

@include "../../inc/dos_equ.inc"

;; @proc reg_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=subcommand (QUERY|ADD|DELETE|EXPORT|IMPORT) Key [/v Val] [/t Type] [/d Data] [/f]
;; @out Reads or mutates WINCMD.REG key/value store and prints authentic reg.exe output; exits 0/1
;; @err Exits with 1 on missing key/value or invalid subcommand
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for REG.COM backing Registry operations on DOS/Win16 via WINCMD.REG.
@proc reg_entry
    call u_init
    mov di, U_ARG_A ; subcommand
    call u_read_tok
    jnc .got_sub
    jmp .bad_syn
.got_sub:
    cmp byte [U_ARG_A], '/'
    jne .parse_rest
    cmp byte [U_ARG_A + 1], '?'
    je .show_h
    jmp .bad_syn
.show_h:
    mov si, addr(szRegHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.parse_rest:
    mov di, U_ARG_B ; key name or file
    call u_read_tok
    jnc .init_opts
    jmp .bad_syn
.init_opts:
    mov byte [U_ARG_C], 0        ; /v value name ("(Default)" if /ve)
    mov byte [U_IO_B], 'R'       ; /t type default = "REG_SZ"
    mov byte [U_IO_B + 1], 'E'
    mov byte [U_IO_B + 2], 'G'
    mov byte [U_IO_B + 3], '_'
    mov byte [U_IO_B + 4], 'S'
    mov byte [U_IO_B + 5], 'Z'
    mov byte [U_IO_B + 6], 0
    mov byte [U_IO_B + 128], 0   ; /d data
    mov byte [U_IO_B + 256], 0   ; secondary file (EXPORT <key> <file>)

.opt_lp:
    call u_skip_ws
    test al, al
    jz .dispatch
    cmp al, '/'
    jne .extra_pos
    mov di, (U_IO_B + 384)
    call u_read_tok
    mov al, byte [U_IO_B + 385]
    call u_chr_upper
    cmp al, 'V'
    je .opt_v
    cmp al, 'T'
    je .opt_t
    cmp al, 'D'
    je .opt_d
    jmp .opt_lp

.opt_v:
    mov al, byte [U_IO_B + 386]
    call u_chr_upper
    cmp al, 'E'
    je .opt_ve
    mov di, U_ARG_C
    call u_read_tok
    jmp .opt_lp
.opt_ve:
    push si
    mov si, addr(szRegDefVal)
    mov di, U_ARG_C
    mov cx, 10
    rep movsb
    pop si
    jmp .opt_lp

.opt_t:
    mov di, U_IO_B
    call u_read_tok
    jmp .opt_lp
.opt_d:
    mov di, (U_IO_B + 128)
    call u_read_tok
    jmp .opt_lp
.extra_pos:
    mov di, (U_IO_B + 256)
    call u_read_tok
    jmp .opt_lp

.dispatch:
    ; Load existing WINCMD.REG into U_IO_A (0 bytes if not yet created)
    mov dx, addr(szRegStore)
    mov di, U_IO_A
    mov cx, 12000
    call u_load_stream
    jnc .store_ok
    xor ax, ax
    mov byte [U_IO_A], 0
.store_ok:
    mov word [wStoreLen], ax

    mov al, byte [U_ARG_A]
    call u_chr_upper
    cmp al, 'A' ; ADD
    je .do_add
    cmp al, 'Q' ; QUERY
    je .j_query
    cmp al, 'D' ; DELETE
    je .j_delete
    cmp al, 'E' ; EXPORT
    je .j_export
    cmp al, 'I' ; IMPORT
    je .j_import
    jmp .bad_syn

.j_query:
    jmp .do_query
.j_delete:
    jmp .do_delete
.j_export:
    jmp .do_export
.j_import:
    jmp .do_import

.do_add:
    ; Format entry line "<Key>|<Val>|<Type>|<Data><CRLF>" at U_IO_A + wStoreLen
    mov di, U_IO_A
    add di, word [wStoreLen]
    mov si, U_ARG_B
    call reg_app_str
    mov byte [di], '|'
    inc di
    mov si, U_ARG_C
    cmp byte [si], 0
    jne .add_vn
    mov si, addr(szRegDefVal)
.add_vn:
    call reg_app_str
    mov byte [di], '|'
    inc di
    mov si, U_IO_B
    call reg_app_str
    mov byte [di], '|'
    inc di
    mov si, (U_IO_B + 128)
    call reg_app_str
    mov byte [di], 0x0D
    mov byte [di + 1], 0x0A
    add di, 2

    mov cx, di
    sub cx, U_IO_A
    mov dx, addr(szRegStore)
    mov si, U_IO_A
    call u_save_file
    jnc .add_ok
    jmp .bad_syn
.add_ok:
    mov si, addr(szRegSuccess)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.do_query:
    ; Scan U_IO_A lines matching prefix "<Key>|"
    mov si, U_ARG_B
    call u_strlen
    mov word [wKeyLen], cx
    mov word [wRegHits], 0
    mov si, U_IO_A

.q_lp:
    cmp byte [si], 0
    jne .q_not_end
    jmp .q_done
.q_not_end:
    mov bx, si ; line start
.q_eol:
    mov al, byte [si]
    test al, al
    jz .q_got
    cmp al, 0x0D
    je .q_cr
    cmp al, 0x0A
    je .q_lf
    inc si
    jmp .q_eol
.q_cr:
    mov byte [si], 0
    inc si
    cmp byte [si], 0x0A
    jne .q_got
.q_lf:
    mov byte [si], 0
    inc si

.q_got:
    push si ; next line
    ; Check if line [bx] starts with U_ARG_B + '|'
    mov cx, word [wKeyLen]
    mov di, bx
    mov si, U_ARG_B
.q_kc:
    mov al, byte [di]
    call u_chr_upper
    mov ah, al
    mov al, byte [si]
    call u_chr_upper
    cmp al, ah
    jne .q_skip
    inc si
    inc di
    loop .q_kc
    cmp byte [di], '|'
    jne .q_skip
    inc di ; DI = Val|Type|Data

    cmp word [wRegHits], 0
    jne .q_prt_val
    push di
    mov si, U_ARG_B
    call u_write_str
    call u_write_crlf
    pop di

.q_prt_val:
    inc word [wRegHits]
    push di
    mov si, addr(szRegIndent)
    call u_write_str
    pop si
.q_vc:
    mov al, byte [si]
    test al, al
    jz .q_veol
    inc si
    cmp al, '|'
    jne .q_put
    push si
    mov si, addr(szRegIndent)
    call u_write_str
    pop si
    jmp .q_vc
.q_put:
    push si
    mov dl, al
    call u_write_ch
    pop si
    jmp .q_vc
.q_veol:
    call u_write_crlf

.q_skip:
    pop si
    jmp .q_lp

.q_done:
    cmp word [wRegHits], 0
    jne .q_ok
    jmp .bad_syn
.q_ok:
    xor al, al
    call u_exit
    ret

.do_delete:
    ; Copy all lines in U_IO_A NOT starting with U_ARG_B + '|' into U_IO_B + 512!
    mov si, U_ARG_B
    call u_strlen
    mov word [wKeyLen], cx
    mov si, U_IO_A
    mov di, (U_IO_B + 512)
    mov word [wRegHits], 0

.d_lp:
    cmp byte [si], 0
    je .d_save
    mov bx, si
.d_eol:
    mov al, byte [si]
    test al, al
    jz .d_got
    inc si
    cmp al, 0x0A
    jne .d_eol
.d_got:
    mov dx, si ; line end (past LF)
    ; Compare [bx] with U_ARG_B + '|'
    push dx
    push di
    mov cx, word [wKeyLen]
    mov di, bx
    mov si, U_ARG_B
.d_kc:
    mov al, byte [di]
    call u_chr_upper
    mov ah, al
    mov al, byte [si]
    call u_chr_upper
    cmp al, ah
    jne .d_keep
    inc si
    inc di
    loop .d_kc
    cmp byte [di], '|'
    jne .d_keep
    ; Matched -> drop this line!
    inc word [wRegHits]
    pop di
    pop si
    jmp .d_lp

.d_keep:
    pop di
    pop dx
    mov si, bx
.d_cp:
    cmp si, dx
    jae .d_lp
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    jmp .d_cp

.d_save:
    cmp word [wRegHits], 0
    jne .d_ok
    jmp .bad_syn
.d_ok:
    mov cx, di
    sub cx, (U_IO_B + 512)
    mov dx, addr(szRegStore)
    mov si, (U_IO_B + 512)
    call u_save_file
    mov si, addr(szRegSuccess)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.do_export:
    mov dx, (U_IO_B + 256)
    cmp byte [U_IO_B + 256], 0
    je .bad_syn
    mov si, U_IO_A
    mov cx, word [wStoreLen]
    call u_save_file
    jc .bad_syn
    mov si, addr(szRegSuccess)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.do_import:
    mov dx, U_ARG_B
    mov di, U_IO_B
    mov cx, 12000
    call u_load_stream
    jc .bad_syn
    mov cx, ax
    mov dx, addr(szRegStore)
    mov si, U_IO_B
    call u_save_file
    jc .bad_syn
    mov si, addr(szRegSuccess)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.bad_syn:
    mov si, addr(szRegErr)
    call u_write_str
    call u_write_crlf
    mov al, 1
    call u_exit
    ret
@endproc

;; @proc reg_app_str
;; @abi dos16
;; @in DS:SI = source ASCIIZ, DS:DI = destination cursor
;; @out Copies non-NUL bytes to DS:DI and advances DI
;; @err none
;; @clobbers ax, si, di
;; @desc Appends an ASCIIZ field into the WINCMD.REG line builder.
@proc reg_app_str
.lp:
    mov al, byte [si]
    test al, al
    jz .dn
    mov byte [di], al
    inc si
    inc di
    jmp .lp
.dn:
    ret
@endproc

@include "u_common.inc"

@data dw wStoreLen, 0
@data dw wKeyLen, 0
@data dw wRegHits, 0
@data db szRegStore, "WINCMD.REG", 0
@data db szRegDefVal, "(Default)", 0
@data db szRegIndent, "    ", 0
@data db szRegSuccess, "The operation completed successfully.", 0
@data db szRegErr, "ERROR: Unable to find the specified registry key or value.", 0
@data db szRegHelp, "REG [QUERY | ADD | DELETE | EXPORT | IMPORT] Key [/v Val] [/t Type] [/d Data] [/f]", 0
