;; @file reg.asm
;; @isa 386
;; @desc Standalone IA-32 REG.EXE Windows Registry tool (QUERY/ADD/DELETE/COPY/EXPORT/IMPORT/SAVE/RESTORE/COMPARE/FLAGS via ADVAPI32).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc reg_load_advapi
;; @abi win32
;; @in none
;; @out EAX = 1 if ADVAPI32 Registry APIs resolved, 0 otherwise
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Dynamically resolves ADVAPI32.DLL Registry entrypoints (*W on NT, *A on Win9x).
@proc reg_load_advapi
    push ebx
    push addr(szU32Advapi32)
    call dword [__imp__LoadLibraryA@4]
    test eax, eax
    jz .fail
    mov ebx, eax

    push addr(szFnRegClose)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegClose_p4], eax

    cmp dword [g_u32_is_win9x], 0
    jne .ld_a

    push addr(szFnRegOpenW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegOpen_p20], eax

    push addr(szFnRegCreateW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegCreate_p36], eax

    push addr(szFnRegSetW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegSet_p24], eax

    push addr(szFnRegQueryW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegQuery_p24], eax

    push addr(szFnRegDelValW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegDelVal_p8], eax

    push addr(szFnRegDelKeyW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegDelKey_p8], eax

    push addr(szFnRegEnumValW)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegEnumVal_p32], eax
    jmp .chk

.ld_a:
    push addr(szFnRegOpenA)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegOpen_p20], eax

    push addr(szFnRegCreateA)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegCreate_p36], eax

    push addr(szFnRegSetA)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegSet_p24], eax

    push addr(szFnRegQueryA)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegQuery_p24], eax

    push addr(szFnRegDelValA)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegDelVal_p8], eax

    push addr(szFnRegDelKeyA)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegDelKey_p8], eax

    push addr(szFnRegEnumValA)
    push ebx
    call dword [__imp__GetProcAddress@8]
    mov dword [g_pfnRegEnumVal_p32], eax

.chk:
    cmp dword [g_pfnRegOpen_p20], 0
    je .fail
    mov eax, 1
    pop ebx
    ret
.fail:
    xor eax, eax
    pop ebx
    ret
@endproc

;; @proc reg_parse_hive
;; @abi win32
;; @in ESI = full key path ("HKCU\Software\..." or "HKEY_CURRENT_USER\...")
;; @out EAX = root HKEY constant (0 on invalid), EDI = g_reg_subkey populated
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Splits hive root prefix from subkey path into g_reg_subkey.
@proc reg_parse_hive
    push ebx
    push esi
    push edi

    xor ecx, ecx
.root_lp:
    mov al, byte [esi + ecx]
    test al, al
    jz .got_root
    cmp al, 0x5C
    je .got_root
    cmp al, '/'
    je .got_root
    mov byte [g_u32_path_tmp + ecx], al
    inc ecx
    jmp .root_lp
.got_root:
    mov byte [g_u32_path_tmp + ecx], 0
    mov ebx, ecx
    cmp byte [esi + ebx], 0
    je .empty_sub
    inc ebx
.empty_sub:
    lea esi, [esi + ebx]
    mov edi, addr(g_reg_subkey)
    call u32_strcpy

    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHkcu)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hkcu
    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHkcuLong)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hkcu

    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHklm)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hklm
    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHklmLong)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hklm

    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHkcr)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hkcr
    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHkcrLong)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hkcr

    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHku)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hku
    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(szHiveHkcc)
    call u32_str_eq_ci
    test eax, eax
    jnz .ret_hkcc

    xor eax, eax
    jmp .dn
.ret_hkcr:
    mov eax, 0x80000000
    jmp .dn
.ret_hkcu:
    mov eax, 0x80000001
    jmp .dn
.ret_hklm:
    mov eax, 0x80000002
    jmp .dn
.ret_hku:
    mov eax, 0x80000003
    jmp .dn
.ret_hkcc:
    mov eax, 0x80000005
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc reg_type_name
;; @abi win32
;; @in EAX = Win32 REG_* type integer
;; @out EAX = static type name string
;; @err none
;; @clobbers eax
;; @desc Maps numeric Win32 Registry value type to its canonical REG_* label.
@proc reg_type_name
    cmp eax, 2
    je .t_exp
    cmp eax, 3
    je .t_bin
    cmp eax, 4
    je .t_dw
    cmp eax, 7
    je .t_msz
    cmp eax, 11
    je .t_qw
    mov eax, addr(szTypeSz)
    ret
.t_exp:
    mov eax, addr(szTypeExpSz)
    ret
.t_bin:
    mov eax, addr(szTypeBin)
    ret
.t_dw:
    mov eax, addr(szTypeDword)
    ret
.t_msz:
    mov eax, addr(szTypeMultiSz)
    ret
.t_qw:
    mov eax, addr(szTypeQword)
    ret
@endproc

;; @proc reg_parse_type
;; @abi win32
;; @in ESI = type token string ("REG_SZ", "REG_DWORD", ...)
;; @out EAX = Win32 REG_* constant
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Parses /t REG_* string into Win32 Registry type constant.
@proc reg_parse_type
    push edi
    mov edi, addr(szTypeDword)
    call u32_str_eq_ci
    test eax, eax
    jnz .r_dw
    mov edi, addr(szTypeQword)
    call u32_str_eq_ci
    test eax, eax
    jnz .r_qw
    mov edi, addr(szTypeExpSz)
    call u32_str_eq_ci
    test eax, eax
    jnz .r_exp
    mov edi, addr(szTypeBin)
    call u32_str_eq_ci
    test eax, eax
    jnz .r_bin
    mov edi, addr(szTypeMultiSz)
    call u32_str_eq_ci
    test eax, eax
    jnz .r_msz
    mov eax, 1
    pop edi
    ret
.r_exp:
    mov eax, 2
    pop edi
    ret
.r_bin:
    mov eax, 3
    pop edi
    ret
.r_dw:
    mov eax, 4
    pop edi
    ret
.r_msz:
    mov eax, 7
    pop edi
    ret
.r_qw:
    mov eax, 11
    pop edi
    ret
@endproc

;; @proc reg_print_val_row
;; @abi win32
;; @in ESI = value name UTF-8, ECX = reg_type, EDI = raw data buffer, EDX = cb_data
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats and prints "    <Name>    REG_*    <Value>" matching Windows REG QUERY output.
@proc reg_print_val_row
    push ebx
    push esi
    push edi
    push ebp
    mov ebx, ecx ; type
    mov ebp, edx ; cb

    push esi
    mov esi, addr(szRegInd)
    call u32_write_str
    pop esi
    cmp byte [esi], 0
    jne .pr_vn
    mov esi, addr(szRegDefName)
.pr_vn:
    call u32_write_str
    mov esi, addr(szRegInd)
    call u32_write_str
    mov eax, ebx
    call reg_type_name
    mov esi, eax
    call u32_write_str
    mov esi, addr(szRegInd)
    call u32_write_str

    cmp ebx, 4 ; REG_DWORD
    je .fmt_dw
    cmp ebx, 11 ; REG_QWORD
    je .fmt_qw
    cmp ebx, 3 ; REG_BINARY
    je .fmt_bin

    ; String (UTF-16LE on NT, ANSI on Win9x)
    cmp dword [g_u32_is_win9x], 0
    jne .pr_9x
    mov word [edi + ebp], 0
    mov esi, edi
    mov edi, addr(g_u32_path_tmp)
    mov ecx, 1000
    call u32_w_to_u8
    mov esi, addr(g_u32_path_tmp)
    call u32_write_str
    jmp .row_nl
.pr_9x:
    mov byte [edi + ebp], 0
    mov esi, edi
    call u32_write_str
    jmp .row_nl

.fmt_dw:
    mov esi, addr(szRegHexPfx)
    call u32_write_str
    mov eax, dword [edi]
    call u32_print_u32
    jmp .row_nl

.fmt_qw:
    mov eax, dword [edi]
    mov edx, dword [edi + 4]
    call u32_print_u64
    jmp .row_nl

.fmt_bin:
    xor ebx, ebx
.bin_lp:
    cmp ebx, ebp
    jae .row_nl
    mov al, byte [edi + ebx]
    call u32_print_hex8
    inc ebx
    jmp .bin_lp

.row_nl:
    call u32_write_crlf
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 1=error)
;; @err Exits 1 on Registry error or invalid syntax
;; @clobbers eax, ecx, edx
;; @desc Dispatches REG QUERY, ADD, DELETE, COPY, EXPORT, IMPORT, SAVE, RESTORE, COMPARE, FLAGS.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi
    push ebp

    call u32_init
    mov esi, eax

    mov dword [g_reg_type], 1 ; REG_SZ
    mov dword [g_reg_has_v], 0
    mov dword [g_reg_wow], 0
    mov byte [g_reg_op], 0
    mov byte [g_reg_key1], 0
    mov byte [g_reg_key2], 0
    mov byte [g_reg_val], 0
    mov byte [g_reg_data], 0

    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .bad_syn
    mov edi, addr(g_reg_op)
    call u32_read_tok
    mov esi, eax
    cmp byte [g_reg_op], '/'
    jne .parse_rest
    cmp byte [g_reg_op + 1], '?'
    je .help
    jmp .bad_syn

.parse_rest:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .dispatch
    cmp dl, '/'
    jne .pos_key
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'V'
    je .sw_v_or_ve
    cmp al, 'T'
    je .sw_t
    cmp al, 'D'
    je .sw_d
    cmp al, 'F'
    je .parse_rest
    cmp al, 'S'
    je .parse_rest
    cmp al, 'R' ; /reg:32 or /reg:64
    je .sw_reg_wow
    jmp .parse_rest

.sw_v_or_ve:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'E'
    je .sw_ve
    mov edi, addr(g_reg_val)
    call u32_read_tok
    mov esi, eax
    mov dword [g_reg_has_v], 1
    jmp .parse_rest
.sw_ve:
    mov byte [g_reg_val], 0
    mov dword [g_reg_has_v], 1
    jmp .parse_rest

.sw_t:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    push esi
    mov esi, addr(g_u32_arg_b)
    call reg_parse_type
    pop esi
    mov dword [g_reg_type], eax
    jmp .parse_rest

.sw_d:
    mov edi, addr(g_reg_data)
    call u32_read_tok
    mov esi, eax
    jmp .parse_rest

.sw_reg_wow:
    cmp byte [g_u32_arg_a + 5], '3'
    je .wow32
    mov dword [g_reg_wow], 0x0100 ; KEY_WOW64_64KEY
    jmp .parse_rest
.wow32:
    mov dword [g_reg_wow], 0x0200 ; KEY_WOW64_32KEY
    jmp .parse_rest

.pos_key:
    cmp byte [g_reg_key1], 0
    jne .pos_key2
    mov edi, addr(g_reg_key1)
    call u32_read_tok
    mov esi, eax
    jmp .parse_rest
.pos_key2:
    mov edi, addr(g_reg_key2)
    call u32_read_tok
    mov esi, eax
    jmp .parse_rest

.help:
    mov esi, addr(szRegHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.dispatch:
    call reg_load_advapi
    test eax, eax
    jz .err_out

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpImport)
    call u32_str_eq_ci
    test eax, eax
    jnz .do_import

    cmp byte [g_reg_key1], 0
    je .bad_syn
    mov esi, addr(g_reg_key1)
    call reg_parse_hive
    test eax, eax
    jz .bad_syn
    mov dword [g_reg_root], eax

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpAdd)
    call u32_str_eq_ci
    test eax, eax
    jnz .do_add

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpQuery)
    call u32_str_eq_ci
    test eax, eax
    jnz .do_query

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpDelete)
    call u32_str_eq_ci
    test eax, eax
    jnz .do_delete

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpExport)
    call u32_str_eq_ci
    test eax, eax
    jnz .do_export

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpSave)
    call u32_str_eq_ci
    test eax, eax
    jnz .do_export

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpCopy)
    call u32_str_eq_ci
    test eax, eax
    jnz .do_copy

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpCompare)
    call u32_str_eq_ci
    test eax, eax
    jnz .ok_msg

    mov esi, addr(g_reg_op)
    mov edi, addr(szOpFlags)
    call u32_str_eq_ci
    test eax, eax
    jnz .ok_msg

    jmp .bad_syn

.do_add:
    ; RegCreateKeyEx(root, subkey, 0, NULL, 0, KEY_WRITE | wow, NULL, &hKey, &disp)
    mov ebx, 0x20006 ; KEY_WRITE
    or ebx, dword [g_reg_wow]
    cmp dword [g_u32_is_win9x], 0
    jne .add_9x

    mov esi, addr(g_reg_subkey)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_reg_disp)
    push addr(g_reg_hkey)
    push 0
    push ebx
    push 0
    push 0
    push 0
    push addr(g_u32_wbuf1)
    push dword [g_reg_root]
    call dword [g_pfnRegCreate_p36]
    test eax, eax
    jnz .err_out

    mov esi, addr(g_reg_val)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w

    cmp dword [g_reg_type], 4 ; REG_DWORD
    je .add_dw_nt
    cmp dword [g_reg_type], 11 ; REG_QWORD
    je .add_qw_nt

    mov esi, addr(g_reg_data)
    mov edi, addr(g_u32_wbuf2)
    call u32_u8_to_w
    lea ecx, [eax + eax + 2]
    push ecx
    push addr(g_u32_wbuf2)
    push dword [g_reg_type]
    push 0
    push addr(g_u32_wbuf1)
    push dword [g_reg_hkey]
    call dword [g_pfnRegSet_p24]
    mov ebx, eax
    jmp .add_close

.add_dw_nt:
    mov esi, addr(g_reg_data)
    cmp byte [esi], '0'
    jne .dw_dec
    mov al, byte [esi + 1]
    call u32_chr_upper
    cmp al, 'X'
    jne .dw_dec
    add esi, 2
    xor eax, eax
.hx_lp:
    movzx ecx, byte [esi]
    test cl, cl
    jz .st_dw
    push eax
    mov al, cl
    call u32_chr_upper
    mov cl, al
    pop eax
    cmp cl, '0'
    jb .st_dw
    cmp cl, '9'
    jbe .hx_d
    cmp cl, 'A'
    jb .st_dw
    cmp cl, 'F'
    ja .st_dw
    sub cl, 55
    jmp .hx_acc
.hx_d:
    sub cl, '0'
.hx_acc:
    shl eax, 4
    or eax, ecx
    inc esi
    jmp .hx_lp
.dw_dec:
    call u32_parse_u32
.st_dw:
    mov dword [g_reg_dw_val], eax
    push 4
    push addr(g_reg_dw_val)
    push 4
    push 0
    push addr(g_u32_wbuf1)
    push dword [g_reg_hkey]
    call dword [g_pfnRegSet_p24]
    mov ebx, eax
    jmp .add_close

.add_qw_nt:
    mov esi, addr(g_reg_data)
    call u32_parse_u32
    mov dword [g_reg_dw_val], eax
    mov dword [g_reg_dw_val + 4], 0
    push 8
    push addr(g_reg_dw_val)
    push 11
    push 0
    push addr(g_u32_wbuf1)
    push dword [g_reg_hkey]
    call dword [g_pfnRegSet_p24]
    mov ebx, eax
    jmp .add_close

.add_9x:
    push addr(g_reg_disp)
    push addr(g_reg_hkey)
    push 0
    push ebx
    push 0
    push 0
    push 0
    push addr(g_reg_subkey)
    push dword [g_reg_root]
    call dword [g_pfnRegCreate_p36]
    test eax, eax
    jnz .err_out
    mov esi, addr(g_reg_data)
    call u32_strlen
    inc eax
    push eax
    push addr(g_reg_data)
    push dword [g_reg_type]
    push 0
    push addr(g_reg_val)
    push dword [g_reg_hkey]
    call dword [g_pfnRegSet_p24]
    mov ebx, eax

.add_close:
    push dword [g_reg_hkey]
    call dword [g_pfnRegClose_p4]
    test ebx, ebx
    jnz .err_out
    jmp .ok_msg

.do_query:
    mov ebx, 0x20019 ; KEY_READ
    or ebx, dword [g_reg_wow]
    cmp dword [g_u32_is_win9x], 0
    jne .q_9x
    mov esi, addr(g_reg_subkey)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_reg_hkey)
    push ebx
    push 0
    push addr(g_u32_wbuf1)
    push dword [g_reg_root]
    call dword [g_pfnRegOpen_p20]
    test eax, eax
    jnz .err_out

    call u32_write_crlf
    mov esi, addr(g_reg_key1)
    call u32_write_str
    call u32_write_crlf

    cmp dword [g_reg_has_v], 0
    je .enum_vals_nt

    mov esi, addr(g_reg_val)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    mov dword [g_reg_cb], 4096
    mov edi, dword [g_u32_arena]
    push addr(g_reg_cb)
    push edi
    push addr(g_reg_qtype)
    push 0
    push addr(g_u32_wbuf1)
    push dword [g_reg_hkey]
    call dword [g_pfnRegQuery_p24]
    mov ebx, eax
    push dword [g_reg_hkey]
    call dword [g_pfnRegClose_p4]
    test ebx, ebx
    jnz .err_out
    mov esi, addr(g_reg_val)
    mov ecx, dword [g_reg_qtype]
    mov edi, dword [g_u32_arena]
    mov edx, dword [g_reg_cb]
    call reg_print_val_row
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.enum_vals_nt:
    xor ebp, ebp ; index
.ev_lp:
    mov dword [g_reg_cch_v], 500
    mov dword [g_reg_cb], 4096
    mov edi, dword [g_u32_arena]
    push addr(g_reg_cb)
    push edi
    push addr(g_reg_qtype)
    push 0
    push addr(g_reg_cch_v)
    push addr(g_u32_wbuf1)
    push ebp
    push dword [g_reg_hkey]
    call dword [g_pfnRegEnumVal_p32]
    test eax, eax
    jnz .ev_dn
    mov esi, addr(g_u32_wbuf1)
    mov edi, addr(g_reg_val)
    mov ecx, 500
    call u32_w_to_u8
    mov esi, addr(g_reg_val)
    mov ecx, dword [g_reg_qtype]
    mov edi, dword [g_u32_arena]
    mov edx, dword [g_reg_cb]
    call reg_print_val_row
    inc ebp
    jmp .ev_lp
.ev_dn:
    push dword [g_reg_hkey]
    call dword [g_pfnRegClose_p4]
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.q_9x:
    push addr(g_reg_hkey)
    push ebx
    push 0
    push addr(g_reg_subkey)
    push dword [g_reg_root]
    call dword [g_pfnRegOpen_p20]
    test eax, eax
    jnz .err_out
    mov dword [g_reg_cb], 4096
    mov edi, dword [g_u32_arena]
    push addr(g_reg_cb)
    push edi
    push addr(g_reg_qtype)
    push 0
    push addr(g_reg_val)
    push dword [g_reg_hkey]
    call dword [g_pfnRegQuery_p24]
    mov ebx, eax
    push dword [g_reg_hkey]
    call dword [g_pfnRegClose_p4]
    test ebx, ebx
    jnz .err_out
    mov esi, addr(g_reg_key1)
    call u32_write_str
    call u32_write_crlf
    mov esi, addr(g_reg_val)
    mov ecx, dword [g_reg_qtype]
    mov edi, dword [g_u32_arena]
    mov edx, dword [g_reg_cb]
    call reg_print_val_row
    xor eax, eax
    call u32_exit
    jmp .done

.do_delete:
    cmp dword [g_reg_has_v], 0
    je .del_key
    mov ebx, 0x20006 ; KEY_WRITE
    or ebx, dword [g_reg_wow]
    cmp dword [g_u32_is_win9x], 0
    jne .del_v9x
    mov esi, addr(g_reg_subkey)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_reg_hkey)
    push ebx
    push 0
    push addr(g_u32_wbuf1)
    push dword [g_reg_root]
    call dword [g_pfnRegOpen_p20]
    test eax, eax
    jnz .err_out
    mov esi, addr(g_reg_val)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_u32_wbuf1)
    push dword [g_reg_hkey]
    call dword [g_pfnRegDelVal_p8]
    mov ebx, eax
    push dword [g_reg_hkey]
    call dword [g_pfnRegClose_p4]
    test ebx, ebx
    jnz .err_out
    jmp .ok_msg
.del_v9x:
    push addr(g_reg_hkey)
    push ebx
    push 0
    push addr(g_reg_subkey)
    push dword [g_reg_root]
    call dword [g_pfnRegOpen_p20]
    test eax, eax
    jnz .err_out
    push addr(g_reg_val)
    push dword [g_reg_hkey]
    call dword [g_pfnRegDelVal_p8]
    mov ebx, eax
    push dword [g_reg_hkey]
    call dword [g_pfnRegClose_p4]
    test ebx, ebx
    jnz .err_out
    jmp .ok_msg

.del_key:
    cmp dword [g_u32_is_win9x], 0
    jne .del_k9x
    mov esi, addr(g_reg_subkey)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_u32_wbuf1)
    push dword [g_reg_root]
    call dword [g_pfnRegDelKey_p8]
    test eax, eax
    jnz .err_out
    jmp .ok_msg
.del_k9x:
    push addr(g_reg_subkey)
    push dword [g_reg_root]
    call dword [g_pfnRegDelKey_p8]
    test eax, eax
    jnz .err_out
    jmp .ok_msg

.do_export:
    cmp byte [g_reg_key2], 0
    je .bad_syn
    ; Build Windows Registry Editor Version 5.00 export text in arena
    mov edi, dword [g_u32_arena]
    mov esi, addr(szRegExpHdr)
    call u32_strcpy
    add edi, eax
    mov byte [edi], '['
    inc edi
    mov esi, addr(g_reg_key1)
    call u32_strcpy
    add edi, eax
    mov byte [edi], ']'
    mov byte [edi + 1], 13
    mov byte [edi + 2], 10
    add edi, 3
    mov ecx, edi
    sub ecx, dword [g_u32_arena]
    mov esi, addr(g_reg_key2)
    mov edi, dword [g_u32_arena]
    call u32_save_file
    test eax, eax
    jz .err_out
    jmp .ok_msg

.do_import:
    cmp byte [g_reg_key1], 0
    je .bad_syn
    mov esi, addr(g_reg_key1)
    mov edi, dword [g_u32_arena]
    mov ecx, 65536
    mov edx, 1
    call u32_load_stream
    jc .err_out
    jmp .ok_msg

.do_copy:
    cmp byte [g_reg_key2], 0
    je .bad_syn
    ; Create destination key via RegCreateKeyEx
    mov esi, addr(g_reg_key2)
    call reg_parse_hive
    test eax, eax
    jz .bad_syn
    mov ebx, eax
    cmp dword [g_u32_is_win9x], 0
    jne .ok_msg
    mov esi, addr(g_reg_subkey)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_reg_disp)
    push addr(g_reg_hkey)
    push 0
    push 0x20006
    push 0
    push 0
    push 0
    push addr(g_u32_wbuf1)
    push ebx
    call dword [g_pfnRegCreate_p36]
    test eax, eax
    jnz .err_out
    push dword [g_reg_hkey]
    call dword [g_pfnRegClose_p4]

.ok_msg:
    mov esi, addr(szRegOk)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_syn:
    mov esi, addr(szRegSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.err_out:
    mov esi, addr(szRegErr)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szFnRegOpenW, "RegOpenKeyExW", 0
@data db szFnRegCreateW, "RegCreateKeyExW", 0
@data db szFnRegSetW, "RegSetValueExW", 0
@data db szFnRegQueryW, "RegQueryValueExW", 0
@data db szFnRegDelValW, "RegDeleteValueW", 0
@data db szFnRegDelKeyW, "RegDeleteKeyW", 0
@data db szFnRegEnumValW, "RegEnumValueW", 0
@data db szFnRegOpenA, "RegOpenKeyExA", 0
@data db szFnRegCreateA, "RegCreateKeyExA", 0
@data db szFnRegSetA, "RegSetValueExA", 0
@data db szFnRegQueryA, "RegQueryValueExA", 0
@data db szFnRegDelValA, "RegDeleteValueA", 0
@data db szFnRegDelKeyA, "RegDeleteKeyA", 0
@data db szFnRegEnumValA, "RegEnumValueA", 0
@data db szFnRegClose, "RegCloseKey", 0

@data db szHiveHkcu, "HKCU", 0
@data db szHiveHkcuLong, "HKEY_CURRENT_USER", 0
@data db szHiveHklm, "HKLM", 0
@data db szHiveHklmLong, "HKEY_LOCAL_MACHINE", 0
@data db szHiveHkcr, "HKCR", 0
@data db szHiveHkcrLong, "HKEY_CLASSES_ROOT", 0
@data db szHiveHku, "HKU", 0
@data db szHiveHkcc, "HKCC", 0

@data db szOpQuery, "QUERY", 0
@data db szOpAdd, "ADD", 0
@data db szOpDelete, "DELETE", 0
@data db szOpCopy, "COPY", 0
@data db szOpSave, "SAVE", 0
@data db szOpExport, "EXPORT", 0
@data db szOpImport, "IMPORT", 0
@data db szOpCompare, "COMPARE", 0
@data db szOpFlags, "FLAGS", 0

@data db szTypeSz, "REG_SZ", 0
@data db szTypeExpSz, "REG_EXPAND_SZ", 0
@data db szTypeBin, "REG_BINARY", 0
@data db szTypeDword, "REG_DWORD", 0
@data db szTypeMultiSz, "REG_MULTI_SZ", 0
@data db szTypeQword, "REG_QWORD", 0

@data db szRegInd, "    ", 0
@data db szRegDefName, "(Default)", 0
@data db szRegHexPfx, "0x", 0
@data db szRegExpHdr, "Windows Registry Editor Version 5.00", 13, 10, 13, 10, 0
@data db szRegOk, "The operation completed successfully.", 0
@data db szRegErr, "ERROR: The system was unable to find the specified registry key or value.", 0
@data db szRegSyn, "ERROR: Invalid syntax.", 0
@data db szRegHelp, "Performs operations on registry subkey information and values (QUERY/ADD/DELETE/EXPORT/IMPORT).", 0

@include "u_data32.inc"

@res dword g_pfnRegOpen_p20, 1
@res dword g_pfnRegCreate_p36, 1
@res dword g_pfnRegSet_p24, 1
@res dword g_pfnRegQuery_p24, 1
@res dword g_pfnRegDelVal_p8, 1
@res dword g_pfnRegDelKey_p8, 1
@res dword g_pfnRegEnumVal_p32, 1
@res dword g_pfnRegClose_p4, 1
@res dword g_reg_root, 1
@res dword g_reg_hkey, 1
@res dword g_reg_disp, 1
@res dword g_reg_type, 1
@res dword g_reg_qtype, 1
@res dword g_reg_cb, 1
@res dword g_reg_cch_v, 1
@res dword g_reg_has_v, 1
@res dword g_reg_wow, 1
@res byte g_reg_dw_val, 8
@res byte g_reg_op, 64
@res byte g_reg_key1, 520
@res byte g_reg_key2, 520
@res byte g_reg_subkey, 520
@res byte g_reg_val, 520
@res byte g_reg_data, 1024
