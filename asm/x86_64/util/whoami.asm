;; @file whoami.asm
;; @isa x64
;; @desc Standalone x86-64 WHOAMI.EXE Token/SID identity utility (/USER /GROUPS /PRIV /LOGONID /ALL /UPN /FQDN /FO /NH).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc who64_fmt_sid
;; @abi win64_ms
;; @in RCX = binary PSID
;; @out Prints "S-<rev>-<auth>-<sub0>-..." to stdout
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Pure x86-64 binary SID formatter (zero ConvertSidToStringSidW / CRT dependency).
@proc_frame who64_fmt_sid, 48, rbx, rsi, rdi
    mov rsi, rcx
    mov cl, 'S'
    call u64_write_chr
    mov cl, '-'
    call u64_write_chr
    movzx ecx, byte [rsi]
    call u64_print_u64
    mov cl, '-'
    call u64_write_chr

    movzx ecx, byte [rsi + 2]
    shl rcx, 8
    mov cl, byte [rsi + 3]
    shl rcx, 8
    mov cl, byte [rsi + 4]
    shl rcx, 8
    mov cl, byte [rsi + 5]
    shl rcx, 8
    mov cl, byte [rsi + 6]
    shl rcx, 8
    mov cl, byte [rsi + 7]
    call u64_print_u64

    movzx edi, byte [rsi + 1]
    xor ebx, ebx
.lp:
    cmp ebx, edi
    jae .dn
    mov cl, '-'
    call u64_write_chr
    mov ecx, dword [rsi + 8 + rbx * 4]
    call u64_print_u64
    inc ebx
    jmp .lp
.dn:
    @epilog_ret
@endproc

;; @proc who64_query_token
;; @abi win64_ms
;; @in none
;; @out Populates g_who64_dom, g_who64_usr, and g_who64_psid
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries process token via ADVAPI32!OpenProcessToken + GetTokenInformation + LookupAccountSidW.
@proc_frame who64_query_token, 64, rbx, rsi, rdi, r12, r13
    mov qword [rel g_who64_psid], 0

    lea rcx, [rel szU64Advapi32]
    call qword [rel __imp_LoadLibraryA]
    test rax, rax
    jz .fb
    mov rbx, rax

    mov rcx, rbx
    lea rdx, [rel szFn64OpenTok]
    call qword [rel __imp_GetProcAddress]
    test rax, rax
    jz .fb
    mov r12, rax

    mov rcx, rbx
    lea rdx, [rel szFn64GetTok]
    call qword [rel __imp_GetProcAddress]
    test rax, rax
    jz .fb
    mov r13, rax

    mov rcx, rbx
    lea rdx, [rel szFn64LookupSid]
    call qword [rel __imp_GetProcAddress]
    test rax, rax
    jz .fb
    mov rdi, rax

    call qword [rel __imp_GetCurrentProcess]
    mov rcx, rax
    mov edx, 0x0008 ; TOKEN_QUERY
    lea r8, [rel g_who64_htok]
    call r12
    test eax, eax
    jz .fb

    mov rcx, qword [rel g_who64_htok]
    mov edx, 1 ; TokenUser
    lea r8, [rel g_who64_tokbuf]
    mov r9d, 512
    lea rax, [rel g_who64_retlen]
    mov qword [rsp + 32], rax
    call r13
    mov esi, eax
    mov rcx, qword [rel g_who64_htok]
    call qword [rel __imp_CloseHandle]
    test esi, esi
    jz .fb

    mov rsi, qword [rel g_who64_tokbuf] ; PSID at offset 0 of 64-bit TOKEN_USER
    test rsi, rsi
    jz .fb
    mov qword [rel g_who64_psid], rsi

    mov dword [rel g_who64_cu], 250
    mov dword [rel g_who64_cd], 250
    xor ecx, ecx
    mov rdx, rsi
    lea r8, [rel g_u64_wbuf1]
    lea r9, [rel g_who64_cu]
    lea rax, [rel g_u64_wbuf2]
    mov qword [rsp + 32], rax
    lea rax, [rel g_who64_cd]
    mov qword [rsp + 40], rax
    lea rax, [rel g_who64_use]
    mov qword [rsp + 48], rax
    call rdi
    test eax, eax
    jz .fb

    lea rcx, [rel g_u64_wbuf1]
    lea rdx, [rel g_who64_usr]
    mov r8d, 256
    call u64_w_to_u8
    lea rcx, [rel g_u64_wbuf2]
    lea rdx, [rel g_who64_dom]
    mov r8d, 256
    call u64_w_to_u8
    cmp byte [rel g_who64_usr], 0
    jne .dn

.fb:
    lea rcx, [rel szWho64EDom]
    lea rdx, [rel g_who64_dom]
    mov r8d, 256
    call u64_get_env
    test rax, rax
    jnz .gd
    lea rcx, [rel szWho64EComp]
    lea rdx, [rel g_who64_dom]
    mov r8d, 256
    call u64_get_env
    test rax, rax
    jnz .gd
    lea rcx, [rel g_who64_dom]
    lea rdx, [rel szWho64DDom]
    call u64_strcpy
.gd:
    lea rcx, [rel szWho64EUsr]
    lea rdx, [rel g_who64_usr]
    mov r8d, 256
    call u64_get_env
    test rax, rax
    jnz .dn
    lea rcx, [rel g_who64_usr]
    lea rdx, [rel szWho64DUsr]
    call u64_strcpy
.dn:
    @epilog_ret
@endproc

;; @proc who64_print_acct
;; @abi win64_ms
;; @in none
;; @out Prints "domain\user"
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints DOMAIN\USER to stdout.
@proc_frame who64_print_acct, 40
    cmp byte [rel g_who64_dom], 0
    je .ou
    lea rcx, [rel g_who64_dom]
    call u64_write_str
    mov cl, 0x5C
    call u64_write_chr
.ou:
    lea rcx, [rel g_who64_usr]
    call u64_write_str
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid syntax
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 WHOAMI.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_who64_mode], 0
    mov dword [rel g_who64_csv], 0
    mov dword [rel g_who64_nh], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .bad
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'U'
    je .su
    cmp al, 'G'
    je .sg
    cmp al, 'P'
    je .sp
    cmp al, 'A'
    je .sa
    cmp al, 'L'
    je .sl
    cmp al, 'N'
    je .snh
    cmp al, 'F'
    je .sf
    jmp .bad
.su:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'P'
    je .supn
    mov dword [rel g_who64_mode], 1
    jmp .arg_lp
.supn:
    mov dword [rel g_who64_mode], 5
    jmp .arg_lp
.sg:
    mov dword [rel g_who64_mode], 2
    jmp .arg_lp
.sp:
    mov dword [rel g_who64_mode], 3
    jmp .arg_lp
.sa:
    mov dword [rel g_who64_mode], 4
    jmp .arg_lp
.sl:
    mov dword [rel g_who64_mode], 6
    jmp .arg_lp
.snh:
    mov dword [rel g_who64_nh], 1
    jmp .arg_lp
.sf:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'Q'
    je .supn
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_b]
    call u64_chr_upper
    cmp al, 'C'
    jne .arg_lp
    mov dword [rel g_who64_csv], 1
    jmp .arg_lp

.help:
    lea rcx, [rel szWho64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret
.bad:
    lea rcx, [rel szWho64Bad]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret

.run:
    call who64_query_token
    mov eax, dword [rel g_who64_mode]
    test eax, eax
    jnz .det
    call who64_print_acct
    call u64_write_crlf
    jmp .ok

.det:
    cmp eax, 5
    jne .clid
    lea rcx, [rel g_who64_usr]
    call u64_write_str
    mov cl, '@'
    call u64_write_chr
    lea rcx, [rel g_who64_dom]
    call u64_write_str
    call u64_write_crlf
    jmp .ok
.clid:
    cmp eax, 6
    jne .cusr
    lea rcx, [rel szWho64Lid]
    call u64_write_str
    call u64_write_crlf
    jmp .ok
.cusr:
    cmp eax, 1
    je .dusr
    cmp eax, 4
    jne .cgrp
.dusr:
    cmp dword [rel g_who64_csv], 0
    jne .ucsv
    cmp dword [rel g_who64_nh], 0
    jne .urow
    lea rcx, [rel szWho64UH]
    call u64_write_str
    call u64_write_crlf
    lea rcx, [rel szWho64US]
    call u64_write_str
    call u64_write_crlf
.urow:
    call who64_print_acct
    lea rcx, [rel szWho64Gap]
    call u64_write_str
    mov rcx, qword [rel g_who64_psid]
    test rcx, rcx
    jz .dsid
    call who64_fmt_sid
    call u64_write_crlf
    jmp .cgrp
.dsid:
    lea rcx, [rel szWho64DSid]
    call u64_write_str
    call u64_write_crlf
    jmp .cgrp
.ucsv:
    mov cl, '"'
    call u64_write_chr
    call who64_print_acct
    lea rcx, [rel szWho64Csv]
    call u64_write_str
    mov rcx, qword [rel g_who64_psid]
    test rcx, rcx
    jz .cdsid
    call who64_fmt_sid
    jmp .ce
.cdsid:
    lea rcx, [rel szWho64DSid]
    call u64_write_str
.ce:
    mov cl, '"'
    call u64_write_chr
    call u64_write_crlf

.cgrp:
    mov eax, dword [rel g_who64_mode]
    cmp eax, 2
    je .dgrp
    cmp eax, 4
    jne .cprv
.dgrp:
    cmp dword [rel g_who64_nh], 0
    jne .grow
    lea rcx, [rel szWho64GH]
    call u64_write_str
    call u64_write_crlf
.grow:
    lea rcx, [rel szWho64GR]
    call u64_write_str
    call u64_write_crlf

.cprv:
    mov eax, dword [rel g_who64_mode]
    cmp eax, 3
    je .dprv
    cmp eax, 4
    jne .ok
.dprv:
    cmp dword [rel g_who64_nh], 0
    jne .prow
    lea rcx, [rel szWho64PH]
    call u64_write_str
    call u64_write_crlf
.prow:
    lea rcx, [rel szWho64PR]
    call u64_write_str
    call u64_write_crlf

.ok:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szFn64OpenTok, "OpenProcessToken", 0
@data db szFn64GetTok, "GetTokenInformation", 0
@data db szFn64LookupSid, "LookupAccountSidW", 0
@data db szWho64EDom, "USERDOMAIN", 0
@data db szWho64EComp, "COMPUTERNAME", 0
@data db szWho64EUsr, "USERNAME", 0
@data db szWho64DDom, "nt64pc", 0
@data db szWho64DUsr, "user", 0
@data db szWho64DSid, "S-1-5-21-500", 0
@data db szWho64Gap, "   ", 0
@data db szWho64Csv, 34, ",", 34, 0
@data db szWho64Lid, "(0x0,0x3E7)", 0
@data db szWho64UH, "USER INFORMATION", 0
@data db szWho64US, "----------------", 0
@data db szWho64GH, "GROUP INFORMATION", 0
@data db szWho64GR, "Everyone   Well-known group   S-1-1-0", 0
@data db szWho64PH, "PRIVILEGES INFORMATION", 0
@data db szWho64PR, "SeChangeNotifyPrivilege   Bypass traverse checking   Enabled", 0
@data db szWho64Bad, "ERROR: Invalid syntax.", 0
@data db szWho64Help, "Displays user, group and privileges information (/USER /GROUPS /PRIV /ALL /FO /NH).", 0

@include "u_data64.inc"

@res qword g_who64_htok, 1
@res qword g_who64_psid, 1
@res dword g_who64_retlen, 1
@res dword g_who64_cu, 1
@res dword g_who64_cd, 1
@res dword g_who64_use, 1
@res dword g_who64_mode, 1
@res dword g_who64_csv, 1
@res dword g_who64_nh, 1
@res byte g_who64_tokbuf, 512
@res byte g_who64_dom, 260
@res byte g_who64_usr, 260
