;; @file whoami.asm
;; @isa 8086
;; @desc Standalone WHOAMI.COM security identity reporter (/USER, /GROUPS, /PRIV, /ALL, /FO).

@cpu 8086
@format com
@entry whoami_entry

@include "../../inc/dos_equ.inc"

;; @proc whoami_entry
;; @abi dos16
;; @in DS=ES=CS=PSP, [0x0080]=optional [/USER] [/GROUPS] [/PRIV] [/ALL] [/FO CSV|LIST|TABLE]
;; @out Prints "domain\username" (plus SID S-1-5-21-0-500 on /USER or /ALL); exits 0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for WHOAMI.COM resolving %USERDOMAIN%\%USERNAME% or DOS single-user fallback.
@proc whoami_entry
    call u_init

.arg_lp:
    call u_skip_ws
    test al, al
    jz .emit_id
    cmp al, '/'
    jne .sk_tok
    mov di, U_ARG_C
    call u_read_tok
    mov al, byte [U_ARG_C + 1]
    cmp al, '?'
    je .help
    call u_chr_upper
    cmp al, 'U'
    je .set_sid
    cmp al, 'A'
    jne .arg_lp
.set_sid:
    mov byte [bShowSid], 1
    jmp .arg_lp
.sk_tok:
    mov di, U_ARG_C
    call u_read_tok
    jmp .arg_lp

.help:
    mov si, addr(szWhoHelp)
    call u_write_str
    call u_write_crlf
    xor al, al
    call u_exit
    ret

.emit_id:
    mov si, addr(szEnvDomain)
    mov di, U_ARG_A
    call u_get_env_var
    jnc .got_dom
    mov si, addr(szEnvComp)
    mov di, U_ARG_A
    call u_get_env_var
    jnc .got_dom
    mov si, addr(szDefDom)
    mov di, U_ARG_A
    mov cx, 6
    rep movsb
.got_dom:
    mov si, addr(szEnvUser)
    mov di, U_ARG_B
    call u_get_env_var
    jnc .got_usr
    mov si, addr(szDefUsr)
    mov di, U_ARG_B
    mov cx, 5
    rep movsb
.got_usr:
    mov si, U_ARG_A
    call u_write_str
    mov dl, 0x5C
    call u_write_ch
    mov si, U_ARG_B
    call u_write_str
    cmp byte [bShowSid], 0
    je .done_ln
    mov si, addr(szDefSid)
    call u_write_str
.done_ln:
    call u_write_crlf
    xor al, al
    call u_exit
    ret
@endproc

@include "u_common.inc"

@data db bShowSid, 0
@data db szEnvDomain, "USERDOMAIN", 0
@data db szEnvComp, "COMPUTERNAME", 0
@data db szEnvUser, "USERNAME", 0
@data db szDefDom, "dospc", 0
@data db szDefUsr, "user", 0
@data db szDefSid, " S-1-5-21-0-500", 0
@data db szWhoHelp, "WHOAMI [/USER] [/GROUPS] [/PRIV] [/ALL] [/FO TABLE|LIST|CSV]", 0
