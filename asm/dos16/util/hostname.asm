;; @file hostname.asm
;; @isa 8086
;; @desc Standalone HOSTNAME.COM computer network name reporter.

@cpu 8086
@format com
@entry hostname_entry

@include "../../inc/dos_equ.inc"

;; @proc hostname_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=optional /?
;; @out Prints %COMPUTERNAME%, NetBIOS INT 21h AX=5E00h machine name, or "DOSPC"; exits 0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for HOSTNAME.COM across MS-DOS 1.25..8.0 and Windows VDM/vm8086.
@proc hostname_entry
    call u_init
    call u_skip_ws
    cmp al, '/'
    jne .query_host
    cmp byte [si + 1], '?'
    jne .query_host
    mov si, addr(szHnHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.query_host:
    mov si, addr(szEnvCompName)
    mov di, U_ARG_A
    call u_get_env_var
    jnc .print_a

    ; Probe MS-DOS 3.1+ / WfW 3.11 NetBIOS Get Machine Name (INT 21h AX=5E00h)
    cmp byte [bUDosMajor], 3
    jb .use_def
    mov dx, U_ARG_A
    mov ax, 0x5E00
    int 0x21
    jc .use_def
    test ch, ch
    jz .use_def
    ; Trim trailing spaces from 15-byte NetBIOS name
    mov di, (U_ARG_A + 15)
    mov byte [di], 0
.tr_nb:
    cmp di, U_ARG_A
    jbe .chk_nb_nonempty
    cmp byte [di - 1], 0x20
    jne .chk_nb_nonempty
    dec di
    mov byte [di], 0
    jmp .tr_nb
.chk_nb_nonempty:
    cmp byte [U_ARG_A], 0
    jne .print_a

.use_def:
    mov si, addr(szDefHost)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.print_a:
    mov si, U_ARG_A
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db szEnvCompName, "COMPUTERNAME", 0
@data db szDefHost, "DOSPC", 0
@data db szHnHelp, "HOSTNAME", 0
