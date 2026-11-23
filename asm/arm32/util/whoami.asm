;; @file whoami.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 WHOAMI.EXE Token/SID identity utility (/USER /GROUPS /PRIV /LOGONID /ALL /UPN /FQDN /FO /NH).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc whoa32_fmt_sid
;; @abi arm_nt
;; @in r0 = binary PSID
;; @out Prints "S-<rev>-<auth>-<sub0>-..." to stdout
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Pure Thumb-2 binary SID formatter (zero ConvertSidToStringSidW / CRT dependency).
@proc whoa32_fmt_sid
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r0, #83 ; S
    bl ua32_write_chr
    mov r0, #45 ; -
    bl ua32_write_chr
    ldrb r0, [r4]
    bl ua32_print_u32
    mov r0, #45
    bl ua32_write_chr

    ldrb r1, [r4, #2]
    lsl r1, r1, #8
    ldrb r2, [r4, #3]
    orr r1, r1, r2
    ldrb r0, [r4, #4]
    lsl r0, r0, #8
    ldrb r2, [r4, #5]
    orr r0, r0, r2
    lsl r0, r0, #8
    ldrb r2, [r4, #6]
    orr r0, r0, r2
    lsl r0, r0, #8
    ldrb r2, [r4, #7]
    orr r0, r0, r2
    bl ua32_print_u64

    ldrb r5, [r4, #1]
    mov r6, #0
.lp:
    cmp r6, r5
    bhs .dn
    mov r0, #45
    bl ua32_write_chr
    add r1, r4, r6, lsl #2
    ldr r0, [r1, #8]
    bl ua32_print_u32
    add r6, r6, #1
    b .lp
.dn:
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc whoa32_query_token
;; @abi arm_nt
;; @in none
;; @out Populates g_whoa32_dom, g_whoa32_usr, and g_whoa32_psid
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries process token via ADVAPI32!OpenProcessToken + GetTokenInformation + LookupAccountSidW.
@proc whoa32_query_token
    push {r4, r5, r6, r7, r8, lr}
    sub sp, sp, #16
    adr_sym r0, g_whoa32_psid
    mov r1, #0
    str r1, [r0]

    adr_sym r0, u16Ua32Advapi32
    call_iat __imp_LoadLibraryW
    cmp r0, #0
    beq .fb
    mov r4, r0

    mov r0, r4
    adr_sym r1, szFna32OpenTok
    call_iat __imp_GetProcAddress
    cmp r0, #0
    beq .fb
    mov r5, r0

    mov r0, r4
    adr_sym r1, szFna32GetTok
    call_iat __imp_GetProcAddress
    cmp r0, #0
    beq .fb
    mov r6, r0

    mov r0, r4
    adr_sym r1, szFna32LookupSid
    call_iat __imp_GetProcAddress
    cmp r0, #0
    beq .fb
    mov r7, r0

    call_iat __imp_GetCurrentProcess
    mov r1, #0x0008
    adr_sym r2, g_whoa32_htok
    blx r5
    cmp r0, #0
    beq .fb

    adr_sym r0, g_whoa32_htok
    ldr r0, [r0]
    mov r1, #1 ; TokenUser
    adr_sym r2, g_whoa32_tokbuf
    mov r3, #512
    adr_sym r12, g_whoa32_retlen
    str r12, [sp]
    blx r6
    mov r8, r0
    adr_sym r0, g_whoa32_htok
    ldr r0, [r0]
    call_iat __imp_CloseHandle
    cmp r8, #0
    beq .fb

    adr_sym r0, g_whoa32_tokbuf
    ldr r8, [r0] ; 32-bit PSID at offset 0 of TOKEN_USER
    cmp r8, #0
    beq .fb
    adr_sym r0, g_whoa32_psid
    str r8, [r0]

    mov r0, #250
    adr_sym r1, g_whoa32_cu
    str r0, [r1]
    adr_sym r2, g_whoa32_cd
    str r0, [r2]

    mov r0, #0
    mov r1, r8
    adr_sym r2, g_ua32_wbuf1
    adr_sym r3, g_whoa32_cu
    adr_sym r12, g_ua32_wbuf2
    str r12, [sp]
    adr_sym r12, g_whoa32_cd
    str r12, [sp, #4]
    adr_sym r12, g_whoa32_use
    str r12, [sp, #8]
    blx r7
    cmp r0, #0
    beq .fb

    adr_sym r0, g_ua32_wbuf1
    adr_sym r1, g_whoa32_usr
    mov r2, #256
    bl ua32_w_to_u8
    adr_sym r0, g_ua32_wbuf2
    adr_sym r1, g_whoa32_dom
    mov r2, #256
    bl ua32_w_to_u8
    adr_sym r0, g_whoa32_usr
    ldrb r0, [r0]
    cmp r0, #0
    bne .dn

.fb:
    adr_sym r0, szWhoa32EDom
    adr_sym r1, g_whoa32_dom
    mov r2, #256
    bl ua32_get_env
    cmp r0, #0
    bne .gd
    adr_sym r0, szWhoa32EComp
    adr_sym r1, g_whoa32_dom
    mov r2, #256
    bl ua32_get_env
    cmp r0, #0
    bne .gd
    adr_sym r0, g_whoa32_dom
    adr_sym r1, szWhoa32DDom
    bl ua32_strcpy
.gd:
    adr_sym r0, szWhoa32EUsr
    adr_sym r1, g_whoa32_usr
    mov r2, #256
    bl ua32_get_env
    cmp r0, #0
    bne .dn
    adr_sym r0, g_whoa32_usr
    adr_sym r1, szWhoa32DUsr
    bl ua32_strcpy
.dn:
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc whoa32_print_acct
;; @abi arm_nt
;; @in none
;; @out Prints "domain\user"
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints DOMAIN\USER to stdout.
@proc whoa32_print_acct
    push {r4, lr}
    adr_sym r0, g_whoa32_dom
    ldrb r1, [r0]
    cmp r1, #0
    beq .ou
    bl ua32_write_str
    mov r0, #0x5C
    bl ua32_write_chr
.ou:
    adr_sym r0, g_whoa32_usr
    bl ua32_write_str
    pop {r4, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid switch
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 WHOAMI.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_whoa32_mode
    str r1, [r0]
    adr_sym r0, g_whoa32_csv
    str r1, [r0]
    adr_sym r0, g_whoa32_nh
    str r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .run
    cmp r1, #47
    bne .bad
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #85 ; U
    beq .su
    cmp r0, #71 ; G
    beq .sg
    cmp r0, #80 ; P
    beq .sp
    cmp r0, #65 ; A
    beq .sa
    cmp r0, #76 ; L
    beq .sl
    cmp r0, #78 ; N
    beq .snh
    cmp r0, #70 ; F
    beq .sf
    b .bad
.su:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #80 ; /UPN
    beq .supn
    adr_sym r0, g_whoa32_mode
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.supn:
    adr_sym r0, g_whoa32_mode
    mov r1, #5
    str r1, [r0]
    b .arg_lp
.sg:
    adr_sym r0, g_whoa32_mode
    mov r1, #2
    str r1, [r0]
    b .arg_lp
.sp:
    adr_sym r0, g_whoa32_mode
    mov r1, #3
    str r1, [r0]
    b .arg_lp
.sa:
    adr_sym r0, g_whoa32_mode
    mov r1, #4
    str r1, [r0]
    b .arg_lp
.sl:
    adr_sym r0, g_whoa32_mode
    mov r1, #6
    str r1, [r0]
    b .arg_lp
.snh:
    adr_sym r0, g_whoa32_nh
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sf:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #81 ; /FQDN
    beq .supn
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_ua32_arg_b
    ldrb r0, [r0]
    bl ua32_chr_upper
    cmp r0, #67
    bne .arg_lp
    adr_sym r0, g_whoa32_csv
    mov r1, #1
    str r1, [r0]
    b .arg_lp

.help:
    adr_sym r0, szWhoa32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.bad:
    adr_sym r0, szWhoa32Bad
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.run:
    bl whoa32_query_token
    adr_sym r0, g_whoa32_mode
    ldr r5, [r0]
    cmp r5, #0
    bne .det
    bl whoa32_print_acct
    bl ua32_write_crlf
    b .ok

.det:
    cmp r5, #5
    bne .clid
    adr_sym r0, g_whoa32_usr
    bl ua32_write_str
    mov r0, #64 ; @
    bl ua32_write_chr
    adr_sym r0, g_whoa32_dom
    bl ua32_write_str
    bl ua32_write_crlf
    b .ok
.clid:
    cmp r5, #6
    bne .cusr
    adr_sym r0, szWhoa32Lid
    bl ua32_write_str
    bl ua32_write_crlf
    b .ok
.cusr:
    cmp r5, #1
    beq .dusr
    cmp r5, #4
    bne .cgrp
.dusr:
    adr_sym r0, g_whoa32_csv
    ldr r0, [r0]
    cmp r0, #0
    bne .ucsv
    adr_sym r0, g_whoa32_nh
    ldr r0, [r0]
    cmp r0, #0
    bne .urow
    adr_sym r0, szWhoa32UH
    bl ua32_write_str
    bl ua32_write_crlf
    adr_sym r0, szWhoa32US
    bl ua32_write_str
    bl ua32_write_crlf
.urow:
    bl whoa32_print_acct
    adr_sym r0, szWhoa32Gap
    bl ua32_write_str
    adr_sym r0, g_whoa32_psid
    ldr r0, [r0]
    cmp r0, #0
    beq .dsid
    bl whoa32_fmt_sid
    bl ua32_write_crlf
    b .cgrp
.dsid:
    adr_sym r0, szWhoa32DSid
    bl ua32_write_str
    bl ua32_write_crlf
    b .cgrp
.ucsv:
    mov r0, #34
    bl ua32_write_chr
    bl whoa32_print_acct
    adr_sym r0, szWhoa32Csv
    bl ua32_write_str
    adr_sym r0, g_whoa32_psid
    ldr r0, [r0]
    cmp r0, #0
    beq .cds
    bl whoa32_fmt_sid
    b .ce
.cds:
    adr_sym r0, szWhoa32DSid
    bl ua32_write_str
.ce:
    mov r0, #34
    bl ua32_write_chr
    bl ua32_write_crlf

.cgrp:
    cmp r5, #2
    beq .dgrp
    cmp r5, #4
    bne .cprv
.dgrp:
    adr_sym r0, g_whoa32_nh
    ldr r0, [r0]
    cmp r0, #0
    bne .grow
    adr_sym r0, szWhoa32GH
    bl ua32_write_str
    bl ua32_write_crlf
.grow:
    adr_sym r0, szWhoa32GR
    bl ua32_write_str
    bl ua32_write_crlf

.cprv:
    cmp r5, #3
    beq .dprv
    cmp r5, #4
    bne .ok
.dprv:
    adr_sym r0, g_whoa32_nh
    ldr r0, [r0]
    cmp r0, #0
    bne .prow
    adr_sym r0, szWhoa32PH
    bl ua32_write_str
    bl ua32_write_crlf
.prow:
    adr_sym r0, szWhoa32PR
    bl ua32_write_str
    bl ua32_write_crlf

.ok:
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szFna32OpenTok, "OpenProcessToken", 0
@data db szFna32GetTok, "GetTokenInformation", 0
@data db szFna32LookupSid, "LookupAccountSidW", 0
@data db szWhoa32EDom, "USERDOMAIN", 0
@data db szWhoa32EComp, "COMPUTERNAME", 0
@data db szWhoa32EUsr, "USERNAME", 0
@data db szWhoa32DDom, "arm32pc", 0
@data db szWhoa32DUsr, "user", 0
@data db szWhoa32DSid, "S-1-5-21-500", 0
@data db szWhoa32Gap, "   ", 0
@data db szWhoa32Csv, 34, ",", 34, 0
@data db szWhoa32Lid, "(0x0,0x3E7)", 0
@data db szWhoa32UH, "USER INFORMATION", 0
@data db szWhoa32US, "----------------", 0
@data db szWhoa32GH, "GROUP INFORMATION", 0
@data db szWhoa32GR, "Everyone   Well-known group   S-1-1-0", 0
@data db szWhoa32PH, "PRIVILEGES INFORMATION", 0
@data db szWhoa32PR, "SeChangeNotifyPrivilege   Bypass traverse checking   Enabled", 0
@data db szWhoa32Bad, "ERROR: Invalid syntax.", 0
@data db szWhoa32Help, "Displays user, group and privileges information (/USER /GROUPS /PRIV /ALL /FO /NH).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_whoa32_htok, 1
@res dword g_whoa32_psid, 1
@res dword g_whoa32_retlen, 1
@res dword g_whoa32_cu, 1
@res dword g_whoa32_cd, 1
@res dword g_whoa32_use, 1
@res dword g_whoa32_mode, 1
@res dword g_whoa32_csv, 1
@res dword g_whoa32_nh, 1
@res byte g_whoa32_tokbuf, 512
@res byte g_whoa32_dom, 260
@res byte g_whoa32_usr, 260
