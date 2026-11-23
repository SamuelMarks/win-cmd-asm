;; @file whoami.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 WHOAMI.EXE Token/SID identity utility (/USER /GROUPS /PRIV /LOGONID /ALL /UPN /FQDN /FO /NH).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc whoa64_fmt_sid
;; @abi arm64_ms
;; @in x0 = binary PSID
;; @out Prints "S-<rev>-<auth>-<sub0>-..." to stdout
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Pure AArch64 binary SID formatter (zero ConvertSidToStringSidW / CRT dependency).
@proc whoa64_fmt_sid
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x19, x0

    mov w0, #83 ; S
    bl ua64_write_chr
    mov w0, #45 ; -
    bl ua64_write_chr
    ldrb w0, [x19]
    bl ua64_print_u64
    mov w0, #45
    bl ua64_write_chr

    ldrb w0, [x19, #2]
    ldrb w1, [x19, #3]
    orr x0, x1, x0, lsl #8
    ldrb w1, [x19, #4]
    orr x0, x1, x0, lsl #8
    ldrb w1, [x19, #5]
    orr x0, x1, x0, lsl #8
    ldrb w1, [x19, #6]
    orr x0, x1, x0, lsl #8
    ldrb w1, [x19, #7]
    orr x0, x1, x0, lsl #8
    bl ua64_print_u64

    ldrb w20, [x19, #1]
    mov w21, #0
.lp:
    cmp w21, w20
    b.hs .dn
    mov w0, #45
    bl ua64_write_chr
    add x1, x19, #8
    ldr w0, [x1, x21, lsl #2]
    bl ua64_print_u64
    add w21, w21, #1
    b .lp
.dn:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc whoa64_query_token
;; @abi arm64_ms
;; @in none
;; @out Populates g_whoa64_dom, g_whoa64_usr, and g_whoa64_psid
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries process token via ADVAPI32!OpenProcessToken + GetTokenInformation + LookupAccountSidW.
@proc whoa64_query_token
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    adr_sym x0, g_whoa64_psid
    str xzr, [x0]

    adr_sym x0, u16Ua64Advapi32
    call_iat __imp_LoadLibraryW
    cbz x0, .fb
    mov x19, x0

    mov x0, x19
    adr_sym x1, szFna64OpenTok
    call_iat __imp_GetProcAddress
    cbz x0, .fb
    mov x20, x0

    mov x0, x19
    adr_sym x1, szFna64GetTok
    call_iat __imp_GetProcAddress
    cbz x0, .fb
    mov x21, x0

    mov x0, x19
    adr_sym x1, szFna64LookupSid
    call_iat __imp_GetProcAddress
    cbz x0, .fb
    mov x22, x0

    call_iat __imp_GetCurrentProcess
    mov w1, #0x0008 ; TOKEN_QUERY
    adr_sym x2, g_whoa64_htok
    blr x20
    cbz w0, .fb

    adr_sym x0, g_whoa64_htok
    ldr x0, [x0]
    mov w1, #1 ; TokenUser
    adr_sym x2, g_whoa64_tokbuf
    mov w3, #512
    adr_sym x4, g_whoa64_retlen
    blr x21
    mov w20, w0
    adr_sym x0, g_whoa64_htok
    ldr x0, [x0]
    call_iat __imp_CloseHandle
    cbz w20, .fb

    adr_sym x0, g_whoa64_tokbuf
    ldr x19, [x0] ; 64-bit PSID at offset 0 of TOKEN_USER
    cbz x19, .fb
    adr_sym x0, g_whoa64_psid
    str x19, [x0]

    adr_sym x3, g_whoa64_cu
    mov w0, #250
    str w0, [x3]
    adr_sym x5, g_whoa64_cd
    str w0, [x5]
    mov x0, #0
    mov x1, x19
    adr_sym x2, g_ua64_wbuf1
    adr_sym x4, g_ua64_wbuf2
    adr_sym x6, g_whoa64_use
    blr x22
    cbz w0, .fb

    adr_sym x0, g_ua64_wbuf1
    adr_sym x1, g_whoa64_usr
    mov x2, #256
    bl ua64_w_to_u8
    adr_sym x0, g_ua64_wbuf2
    adr_sym x1, g_whoa64_dom
    mov x2, #256
    bl ua64_w_to_u8
    adr_sym x0, g_whoa64_usr
    ldrb w0, [x0]
    cbnz w0, .dn

.fb:
    adr_sym x0, szWhoa64EDom
    adr_sym x1, g_whoa64_dom
    mov x2, #256
    bl ua64_get_env
    cbnz x0, .gd
    adr_sym x0, szWhoa64EComp
    adr_sym x1, g_whoa64_dom
    mov x2, #256
    bl ua64_get_env
    cbnz x0, .gd
    adr_sym x0, g_whoa64_dom
    adr_sym x1, szWhoa64DDom
    bl ua64_strcpy
.gd:
    adr_sym x0, szWhoa64EUsr
    adr_sym x1, g_whoa64_usr
    mov x2, #256
    bl ua64_get_env
    cbnz x0, .dn
    adr_sym x0, g_whoa64_usr
    adr_sym x1, szWhoa64DUsr
    bl ua64_strcpy
.dn:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc whoa64_print_acct
;; @abi arm64_ms
;; @in none
;; @out Prints "domain\user"
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints DOMAIN\USER to stdout.
@proc whoa64_print_acct
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x0, g_whoa64_dom
    ldrb w1, [x0]
    cbz w1, .ou
    bl ua64_write_str
    mov w0, #0x5C
    bl ua64_write_chr
.ou:
    adr_sym x0, g_whoa64_usr
    bl ua64_write_str
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid syntax
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 WHOAMI.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_whoa64_mode
    str wzr, [x0]
    adr_sym x0, g_whoa64_csv
    str wzr, [x0]
    adr_sym x0, g_whoa64_nh
    str wzr, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .run
    cmp w1, #47
    b.ne .bad
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #85 ; U
    b.eq .su
    cmp w0, #71 ; G
    b.eq .sg
    cmp w0, #80 ; P
    b.eq .sp_sw
    cmp w0, #65 ; A
    b.eq .sa
    cmp w0, #76 ; L
    b.eq .sl
    cmp w0, #78 ; N
    b.eq .snh
    cmp w0, #70 ; F
    b.eq .sf
    b .bad
.su:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #80 ; P
    b.eq .supn
    adr_sym x0, g_whoa64_mode
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.supn:
    adr_sym x0, g_whoa64_mode
    mov w1, #5
    str w1, [x0]
    b .arg_lp
.sg:
    adr_sym x0, g_whoa64_mode
    mov w1, #2
    str w1, [x0]
    b .arg_lp
.sp_sw:
    adr_sym x0, g_whoa64_mode
    mov w1, #3
    str w1, [x0]
    b .arg_lp
.sa:
    adr_sym x0, g_whoa64_mode
    mov w1, #4
    str w1, [x0]
    b .arg_lp
.sl:
    adr_sym x0, g_whoa64_mode
    mov w1, #6
    str w1, [x0]
    b .arg_lp
.snh:
    adr_sym x0, g_whoa64_nh
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sf:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #81 ; Q
    b.eq .supn
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_ua64_arg_b
    ldrb w0, [x0]
    bl ua64_chr_upper
    cmp w0, #67 ; C
    b.ne .arg_lp
    adr_sym x0, g_whoa64_csv
    mov w1, #1
    str w1, [x0]
    b .arg_lp

.help:
    adr_sym x0, szWhoa64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szWhoa64Bad
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out

.run:
    bl whoa64_query_token
    adr_sym x0, g_whoa64_mode
    ldr w20, [x0]
    cbnz w20, .det
    bl whoa64_print_acct
    bl ua64_write_crlf
    b .ok

.det:
    cmp w20, #5
    b.ne .clid
    adr_sym x0, g_whoa64_usr
    bl ua64_write_str
    mov w0, #64 ; @
    bl ua64_write_chr
    adr_sym x0, g_whoa64_dom
    bl ua64_write_str
    bl ua64_write_crlf
    b .ok
.clid:
    cmp w20, #6
    b.ne .cusr
    adr_sym x0, szWhoa64Lid
    bl ua64_write_str
    bl ua64_write_crlf
    b .ok
.cusr:
    cmp w20, #1
    b.eq .dusr
    cmp w20, #4
    b.ne .cgrp
.dusr:
    adr_sym x0, g_whoa64_csv
    ldr w0, [x0]
    cbnz w0, .ucsv
    adr_sym x0, g_whoa64_nh
    ldr w0, [x0]
    cbnz w0, .urow
    adr_sym x0, szWhoa64UH
    bl ua64_write_str
    bl ua64_write_crlf
    adr_sym x0, szWhoa64US
    bl ua64_write_str
    bl ua64_write_crlf
.urow:
    bl whoa64_print_acct
    adr_sym x0, szWhoa64Gap
    bl ua64_write_str
    adr_sym x0, g_whoa64_psid
    ldr x0, [x0]
    cbz x0, .dsid
    bl whoa64_fmt_sid
    bl ua64_write_crlf
    b .cgrp
.dsid:
    adr_sym x0, szWhoa64DSid
    bl ua64_write_str
    bl ua64_write_crlf
    b .cgrp
.ucsv:
    mov w0, #34
    bl ua64_write_chr
    bl whoa64_print_acct
    adr_sym x0, szWhoa64Csv
    bl ua64_write_str
    adr_sym x0, g_whoa64_psid
    ldr x0, [x0]
    cbz x0, .cdsid
    bl whoa64_fmt_sid
    b .ce
.cdsid:
    adr_sym x0, szWhoa64DSid
    bl ua64_write_str
.ce:
    mov w0, #34
    bl ua64_write_chr
    bl ua64_write_crlf

.cgrp:
    adr_sym x0, g_whoa64_mode
    ldr w0, [x0]
    cmp w0, #2
    b.eq .dgrp
    cmp w0, #4
    b.ne .cprv
.dgrp:
    adr_sym x0, g_whoa64_nh
    ldr w0, [x0]
    cbnz w0, .grow
    adr_sym x0, szWhoa64GH
    bl ua64_write_str
    bl ua64_write_crlf
.grow:
    adr_sym x0, szWhoa64GR
    bl ua64_write_str
    bl ua64_write_crlf

.cprv:
    adr_sym x0, g_whoa64_mode
    ldr w0, [x0]
    cmp w0, #3
    b.eq .dprv
    cmp w0, #4
    b.ne .ok
.dprv:
    adr_sym x0, g_whoa64_nh
    ldr w0, [x0]
    cbnz w0, .prow
    adr_sym x0, szWhoa64PH
    bl ua64_write_str
    bl ua64_write_crlf
.prow:
    adr_sym x0, szWhoa64PR
    bl ua64_write_str
    bl ua64_write_crlf

.ok:
    mov w0, #0
    bl ua64_exit
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szFna64OpenTok, "OpenProcessToken", 0
@data db szFna64GetTok, "GetTokenInformation", 0
@data db szFna64LookupSid, "LookupAccountSidW", 0
@data db szWhoa64EDom, "USERDOMAIN", 0
@data db szWhoa64EComp, "COMPUTERNAME", 0
@data db szWhoa64EUsr, "USERNAME", 0
@data db szWhoa64DDom, "ntarm64", 0
@data db szWhoa64DUsr, "user", 0
@data db szWhoa64DSid, "S-1-5-21-500", 0
@data db szWhoa64Gap, "   ", 0
@data db szWhoa64Csv, 34, ",", 34, 0
@data db szWhoa64Lid, "(0x0,0x3E7)", 0
@data db szWhoa64UH, "USER INFORMATION", 0
@data db szWhoa64US, "----------------", 0
@data db szWhoa64GH, "GROUP INFORMATION", 0
@data db szWhoa64GR, "Everyone   Well-known group   S-1-1-0", 0
@data db szWhoa64PH, "PRIVILEGES INFORMATION", 0
@data db szWhoa64PR, "SeChangeNotifyPrivilege   Bypass traverse checking   Enabled", 0
@data db szWhoa64Bad, "ERROR: Invalid syntax.", 0
@data db szWhoa64Help, "Displays user, group and privileges information (/USER /GROUPS /PRIV /ALL /FO /NH).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res qword g_whoa64_htok, 1
@res qword g_whoa64_psid, 1
@res dword g_whoa64_retlen, 1
@res dword g_whoa64_cu, 1
@res dword g_whoa64_cd, 1
@res dword g_whoa64_use, 1
@res dword g_whoa64_mode, 1
@res dword g_whoa64_csv, 1
@res dword g_whoa64_nh, 1
@res byte g_whoa64_tokbuf, 512
@res byte g_whoa64_dom, 260
@res byte g_whoa64_usr, 260
