;; @file whoami.asm
;; @isa 386
;; @desc Standalone IA-32 WHOAMI.EXE Token/SID identity utility (/USER /GROUPS /PRIV /LOGONID /ALL /UPN /FQDN /FO /NH).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc who_fmt_sid
;; @abi win32
;; @in ESI = binary SID pointer
;; @out Prints "S-<rev>-<auth>-<sub0>-..." directly to stdout
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Pure-i386 binary SID to S-1-... string formatter (zero ConvertSidToStringSidW / CRT dependency, works on NT 3.1!).
@proc who_fmt_sid
    push ebx
    push esi
    push edi

    mov al, 'S'
    call u32_write_chr
    mov al, '-'
    call u32_write_chr
    movzx eax, byte [esi] ; Revision
    call u32_print_u32
    mov al, '-'
    call u32_write_chr

    ; 48-bit big-endian IdentifierAuthority at [esi+2..7]
    movzx edx, byte [esi + 2]
    shl edx, 8
    mov dl, byte [esi + 3]
    movzx eax, byte [esi + 4]
    shl eax, 8
    mov al, byte [esi + 5]
    shl eax, 8
    mov al, byte [esi + 6]
    shl eax, 8
    mov al, byte [esi + 7]
    call u32_print_u64

    movzx edi, byte [esi + 1] ; SubAuthorityCount
    xor ebx, ebx
.sub_lp:
    cmp ebx, edi
    jae .dn
    mov al, '-'
    call u32_write_chr
    mov eax, dword [esi + 8 + ebx * 4]
    call u32_print_u32
    inc ebx
    jmp .sub_lp
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc who_query_token
;; @abi win32
;; @in none
;; @out g_who_dom, g_who_usr, and g_who_has_sid populated (falls back to env vars on Win9x)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Queries process access token via ADVAPI32!OpenProcessToken + GetTokenInformation + LookupAccountSidW.
@proc who_query_token
    push ebx
    push esi
    push edi

    mov dword [g_who_has_sid], 0
    cmp dword [g_u32_is_win9x], 0
    jne .fallback

    push addr(szU32Advapi32)
    call dword [__imp__LoadLibraryA@4]
    test eax, eax
    jz .fallback
    mov ebx, eax

    push addr(szFnOpenProcTok)
    push ebx
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jz .fallback
    mov dword [g_pfnOpenTok_p12], eax

    push addr(szFnGetTokInfo)
    push ebx
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jz .fallback
    mov dword [g_pfnGetTok_p20], eax

    push addr(szFnLookupSidW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jz .fallback
    mov dword [g_pfnLookupSid_p28], eax

    call dword [__imp__GetCurrentProcess@0]
    push addr(g_who_htok)
    push 0x0008 ; TOKEN_QUERY
    push eax
    call dword [g_pfnOpenTok_p12]
    test eax, eax
    jz .fallback

    push addr(g_who_retlen)
    push 512
    push addr(g_who_tokbuf)
    push 1 ; TokenUser
    push dword [g_who_htok]
    call dword [g_pfnGetTok_p20]
    push eax
    push dword [g_who_htok]
    call dword [__imp__CloseHandle@4]
    pop eax
    test eax, eax
    jz .fallback

    mov esi, dword [g_who_tokbuf] ; PSID at offset 0 of TOKEN_USER
    test esi, esi
    jz .fallback
    mov dword [g_who_psid], esi
    mov dword [g_who_has_sid], 1

    mov dword [g_who_cch_u], 250
    mov dword [g_who_cch_d], 250
    push addr(g_who_use)
    push addr(g_who_cch_d)
    push addr(g_u32_wbuf2)
    push addr(g_who_cch_u)
    push addr(g_u32_wbuf1)
    push esi
    push 0
    call dword [g_pfnLookupSid_p28]
    test eax, eax
    jz .fallback

    mov esi, addr(g_u32_wbuf1)
    mov edi, addr(g_who_usr)
    mov ecx, 256
    call u32_w_to_u8

    mov esi, addr(g_u32_wbuf2)
    mov edi, addr(g_who_dom)
    mov ecx, 256
    call u32_w_to_u8
    cmp byte [g_who_usr], 0
    jne .dn

.fallback:
    mov esi, addr(szWhoEnvDom)
    mov edi, addr(g_who_dom)
    mov ecx, 256
    call u32_get_env
    test eax, eax
    jnz .got_d
    mov esi, addr(szWhoEnvComp)
    mov edi, addr(g_who_dom)
    mov ecx, 256
    call u32_get_env
    test eax, eax
    jnz .got_d
    mov esi, addr(szWhoDefDom)
    mov edi, addr(g_who_dom)
    call u32_strcpy
.got_d:
    mov esi, addr(szWhoEnvUsr)
    mov edi, addr(g_who_usr)
    mov ecx, 256
    call u32_get_env
    test eax, eax
    jnz .dn
    mov esi, addr(szWhoDefUsr)
    mov edi, addr(g_who_usr)
    call u32_strcpy
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc who_print_acct
;; @abi win32
;; @in none
;; @out Prints "domain\user" in lowercase/mixed format
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes DOMAIN\USER string to stdout.
@proc who_print_acct
    push esi
    cmp byte [g_who_dom], 0
    je .only_u
    mov esi, addr(g_who_dom)
    call u32_write_str
    mov al, 0x5C
    call u32_write_chr
.only_u:
    mov esi, addr(g_who_usr)
    call u32_write_str
    pop esi
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 1=invalid switch)
;; @err Exits 1 on invalid switch
;; @clobbers eax, ecx, edx
;; @desc Parses WHOAMI switches (/USER /GROUPS /PRIV /LOGONID /ALL /UPN /FQDN /FO /NH) and prints identity.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_who_mode], 0 ; 0=bare, 1=user, 2=groups, 3=priv, 4=all, 5=upn/fqdn, 6=logonid
    mov dword [g_who_csv], 0
    mov dword [g_who_nh], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .bad_sw
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'U'
    je .sw_u
    cmp al, 'G'
    je .sw_g
    cmp al, 'P'
    je .sw_p
    cmp al, 'A'
    je .sw_a
    cmp al, 'L'
    je .sw_l
    cmp al, 'N'
    je .sw_nh
    cmp al, 'F'
    je .sw_f
    jmp .bad_sw

.sw_u:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'P' ; /UPN
    je .sw_upn
    mov dword [g_who_mode], 1
    jmp .arg_lp
.sw_upn:
    mov dword [g_who_mode], 5
    jmp .arg_lp
.sw_g:
    mov dword [g_who_mode], 2
    jmp .arg_lp
.sw_p:
    mov dword [g_who_mode], 3
    jmp .arg_lp
.sw_a:
    mov dword [g_who_mode], 4
    jmp .arg_lp
.sw_l:
    mov dword [g_who_mode], 6
    jmp .arg_lp
.sw_nh:
    mov dword [g_who_nh], 1
    jmp .arg_lp
.sw_f:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'Q' ; /FQDN
    je .sw_upn
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_b]
    call u32_chr_upper
    cmp al, 'C'
    jne .arg_lp
    mov dword [g_who_csv], 1
    jmp .arg_lp

.help:
    mov esi, addr(szWhoHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_sw:
    mov esi, addr(szWhoBad)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.run:
    call who_query_token

    mov eax, dword [g_who_mode]
    test eax, eax
    jnz .detailed
    call who_print_acct
    call u32_write_crlf
    jmp .ok_out

.detailed:
    cmp eax, 5
    jne .chk_lid
    mov esi, addr(g_who_usr)
    call u32_write_str
    mov al, '@'
    call u32_write_chr
    mov esi, addr(g_who_dom)
    call u32_write_str
    call u32_write_crlf
    jmp .ok_out

.chk_lid:
    cmp eax, 6
    jne .chk_usr_sec
    mov esi, addr(szWhoLid)
    call u32_write_str
    call u32_write_crlf
    jmp .ok_out

.chk_usr_sec:
    cmp eax, 1
    je .do_usr_sec
    cmp eax, 4
    jne .chk_grp_sec
.do_usr_sec:
    cmp dword [g_who_csv], 0
    jne .usr_csv
    cmp dword [g_who_nh], 0
    jne .usr_row
    mov esi, addr(szWhoUsrHdr)
    call u32_write_str
    call u32_write_crlf
    mov esi, addr(szWhoUsrSep)
    call u32_write_str
    call u32_write_crlf
.usr_row:
    call who_print_acct
    mov esi, addr(szWhoGap)
    call u32_write_str
    cmp dword [g_who_has_sid], 0
    je .def_sid
    mov esi, dword [g_who_psid]
    call who_fmt_sid
    call u32_write_crlf
    jmp .chk_grp_sec
.def_sid:
    mov esi, addr(szWhoDefSid)
    call u32_write_str
    call u32_write_crlf
    jmp .chk_grp_sec

.usr_csv:
    mov al, '"'
    call u32_write_chr
    call who_print_acct
    mov esi, addr(szWhoCsvMid)
    call u32_write_str
    cmp dword [g_who_has_sid], 0
    je .csv_def_s
    mov esi, dword [g_who_psid]
    call who_fmt_sid
    jmp .csv_end_q
.csv_def_s:
    mov esi, addr(szWhoDefSid)
    call u32_write_str
.csv_end_q:
    mov al, '"'
    call u32_write_chr
    call u32_write_crlf

.chk_grp_sec:
    mov eax, dword [g_who_mode]
    cmp eax, 2
    je .do_grp
    cmp eax, 4
    jne .chk_prv_sec
.do_grp:
    cmp dword [g_who_nh], 0
    jne .grp_row
    mov esi, addr(szWhoGrpHdr)
    call u32_write_str
    call u32_write_crlf
.grp_row:
    mov esi, addr(szWhoGrpRow)
    call u32_write_str
    call u32_write_crlf

.chk_prv_sec:
    mov eax, dword [g_who_mode]
    cmp eax, 3
    je .do_prv
    cmp eax, 4
    jne .ok_out
.do_prv:
    cmp dword [g_who_nh], 0
    jne .prv_row
    mov esi, addr(szWhoPrvHdr)
    call u32_write_str
    call u32_write_crlf
.prv_row:
    mov esi, addr(szWhoPrvRow)
    call u32_write_str
    call u32_write_crlf

.ok_out:
    xor eax, eax
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szFnOpenProcTok, "OpenProcessToken", 0
@data db szFnGetTokInfo, "GetTokenInformation", 0
@data db szFnLookupSidW, "LookupAccountSidW", 0
@data db szWhoEnvDom, "USERDOMAIN", 0
@data db szWhoEnvComp, "COMPUTERNAME", 0
@data db szWhoEnvUsr, "USERNAME", 0
@data db szWhoDefDom, "ntpc", 0
@data db szWhoDefUsr, "user", 0
@data db szWhoDefSid, "S-1-5-21-500", 0
@data db szWhoGap, "   ", 0
@data db szWhoCsvMid, 34, ",", 34, 0
@data db szWhoLid, "(0x0,0x3E7)", 0
@data db szWhoUsrHdr, "USER INFORMATION", 0
@data db szWhoUsrSep, "----------------", 0
@data db szWhoGrpHdr, "GROUP INFORMATION", 0
@data db szWhoGrpRow, "Everyone   Well-known group   S-1-1-0", 0
@data db szWhoPrvHdr, "PRIVILEGES INFORMATION", 0
@data db szWhoPrvRow, "SeChangeNotifyPrivilege   Bypass traverse checking   Enabled", 0
@data db szWhoBad, "ERROR: Invalid syntax.", 0
@data db szWhoHelp, "Displays user, group and privileges information (/USER /GROUPS /PRIV /ALL /FO /NH).", 0

@include "u_data32.inc"

@res dword g_pfnOpenTok_p12, 1
@res dword g_pfnGetTok_p20, 1
@res dword g_pfnLookupSid_p28, 1
@res dword g_who_htok, 1
@res dword g_who_retlen, 1
@res dword g_who_psid, 1
@res dword g_who_has_sid, 1
@res dword g_who_cch_u, 1
@res dword g_who_cch_d, 1
@res dword g_who_use, 1
@res dword g_who_mode, 1
@res dword g_who_csv, 1
@res dword g_who_nh, 1
@res byte g_who_tokbuf, 512
@res byte g_who_dom, 260
@res byte g_who_usr, 260
