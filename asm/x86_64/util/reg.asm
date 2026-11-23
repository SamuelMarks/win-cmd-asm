;; @file reg.asm
;; @isa x64
;; @desc Standalone x86-64 REG.EXE Windows Registry tool (QUERY/ADD/DELETE/COPY/EXPORT/IMPORT/SAVE/RESTORE/COMPARE/FLAGS via ADVAPI32).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc reg64_load_advapi
;; @abi win64_ms
;; @in none
;; @out EAX = 1 if ADVAPI32 Registry APIs resolved, 0 otherwise
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Dynamically resolves ADVAPI32.DLL *W Registry entrypoints.
@proc_frame reg64_load_advapi, 48, rbx
    lea rcx, [rel szU64Advapi32]
    call qword [rel __imp_LoadLibraryA]
    test rax, rax
    jz .fail
    mov rbx, rax

    mov rcx, rbx
    lea rdx, [rel szFn64RClose]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64RClose], rax

    mov rcx, rbx
    lea rdx, [rel szFn64ROpen]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64ROpen], rax

    mov rcx, rbx
    lea rdx, [rel szFn64RCreate]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64RCreate], rax

    mov rcx, rbx
    lea rdx, [rel szFn64RSet]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64RSet], rax

    mov rcx, rbx
    lea rdx, [rel szFn64RQuery]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64RQuery], rax

    mov rcx, rbx
    lea rdx, [rel szFn64RDelVal]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64RDelVal], rax

    mov rcx, rbx
    lea rdx, [rel szFn64RDelKey]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64RDelKey], rax

    mov rcx, rbx
    lea rdx, [rel szFn64REnumVal]
    call qword [rel __imp_GetProcAddress]
    mov qword [rel g_pfn64REnumVal], rax

    cmp qword [rel g_pfn64ROpen], 0
    je .fail
    mov eax, 1
    @epilog_ret
.fail:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc reg64_parse_hive
;; @abi win64_ms
;; @in RCX = full key path ("HKCU\Software\...")
;; @out RAX = root HKEY (0 on invalid), g_reg64_subkey populated
;; @err RAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Splits hive root prefix from subkey path.
@proc_frame reg64_parse_hive, 48, rbx, rsi, rdi
    mov rsi, rcx
    lea rdi, [rel g_u64_path_tmp]
    xor ecx, ecx
.rlp:
    mov al, byte [rsi + rcx]
    test al, al
    jz .gr
    cmp al, 0x5C
    je .gr
    cmp al, '/'
    je .gr
    mov byte [rdi + rcx], al
    inc ecx
    jmp .rlp
.gr:
    mov byte [rdi + rcx], 0
    mov ebx, ecx
    cmp byte [rsi + rbx], 0
    je .es
    inc ebx
.es:
    lea rcx, [rel g_reg64_subkey]
    lea rdx, [rsi + rbx]
    call u64_strcpy

    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64Hkcu]
    call u64_str_eq_ci
    test eax, eax
    jnz .hkcu
    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64HkcuL]
    call u64_str_eq_ci
    test eax, eax
    jnz .hkcu

    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64Hklm]
    call u64_str_eq_ci
    test eax, eax
    jnz .hklm
    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64HklmL]
    call u64_str_eq_ci
    test eax, eax
    jnz .hklm

    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64Hkcr]
    call u64_str_eq_ci
    test eax, eax
    jnz .hkcr
    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64HkcrL]
    call u64_str_eq_ci
    test eax, eax
    jnz .hkcr

    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64Hku]
    call u64_str_eq_ci
    test eax, eax
    jnz .hku
    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel szH64Hkcc]
    call u64_str_eq_ci
    test eax, eax
    jnz .hkcc
    xor eax, eax
    @epilog_ret
.hkcr:
    mov eax, 0x80000000
    @epilog_ret
.hkcu:
    mov eax, 0x80000001
    @epilog_ret
.hklm:
    mov eax, 0x80000002
    @epilog_ret
.hku:
    mov eax, 0x80000003
    @epilog_ret
.hkcc:
    mov eax, 0x80000005
    @epilog_ret
@endproc

;; @proc reg64_type_name
;; @abi win64_ms
;; @in ECX = Win32 REG_* type
;; @out RAX = static type string
;; @err none
;; @clobbers rax
;; @desc Maps numeric REG_* type to string label.
@proc reg64_type_name
    cmp ecx, 2
    je .te
    cmp ecx, 3
    je .tb
    cmp ecx, 4
    je .td
    cmp ecx, 7
    je .tm
    cmp ecx, 11
    je .tq
    lea rax, [rel szT64Sz]
    ret
.te:
    lea rax, [rel szT64Exp]
    ret
.tb:
    lea rax, [rel szT64Bin]
    ret
.td:
    lea rax, [rel szT64Dw]
    ret
.tm:
    lea rax, [rel szT64Msz]
    ret
.tq:
    lea rax, [rel szT64Qw]
    ret
@endproc

;; @proc reg64_parse_type
;; @abi win64_ms
;; @in RCX = type string
;; @out EAX = Win32 REG_* constant
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9
;; @desc Parses /t REG_* string into constant.
@proc_frame reg64_parse_type, 48, rsi
    mov rsi, rcx
    lea rdx, [rel szT64Dw]
    call u64_str_eq_ci
    test eax, eax
    jnz .rd
    mov rcx, rsi
    lea rdx, [rel szT64Qw]
    call u64_str_eq_ci
    test eax, eax
    jnz .rq
    mov rcx, rsi
    lea rdx, [rel szT64Exp]
    call u64_str_eq_ci
    test eax, eax
    jnz .re
    mov rcx, rsi
    lea rdx, [rel szT64Bin]
    call u64_str_eq_ci
    test eax, eax
    jnz .rb
    mov rcx, rsi
    lea rdx, [rel szT64Msz]
    call u64_str_eq_ci
    test eax, eax
    jnz .rm
    mov eax, 1
    @epilog_ret
.re:
    mov eax, 2
    @epilog_ret
.rb:
    mov eax, 3
    @epilog_ret
.rd:
    mov eax, 4
    @epilog_ret
.rm:
    mov eax, 7
    @epilog_ret
.rq:
    mov eax, 11
    @epilog_ret
@endproc

;; @proc reg64_print_val_row
;; @abi win64_ms
;; @in RCX = val name UTF-8, EDX = reg_type, R8 = raw data buf, R9D = cb_data
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints formatted REG QUERY value line.
@proc_frame reg64_print_val_row, 40, rbx, rsi, rdi, r12
    mov rsi, rcx
    mov ebx, edx
    mov rdi, r8
    mov r12d, r9d

    lea rcx, [rel szR64Ind]
    call u64_write_str
    mov rcx, rsi
    cmp byte [rcx], 0
    jne .vn
    lea rcx, [rel szR64Def]
.vn:
    call u64_write_str
    lea rcx, [rel szR64Ind]
    call u64_write_str
    mov ecx, ebx
    call reg64_type_name
    mov rcx, rax
    call u64_write_str
    lea rcx, [rel szR64Ind]
    call u64_write_str

    cmp ebx, 4
    je .fdw
    cmp ebx, 11
    je .fqw
    cmp ebx, 3
    je .fbin

    mov word [rdi + r12], 0
    mov rcx, rdi
    lea rdx, [rel g_u64_path_tmp]
    mov r8d, 1000
    call u64_w_to_u8
    lea rcx, [rel g_u64_path_tmp]
    call u64_write_str
    jmp .nl

.fdw:
    lea rcx, [rel szR64Hex]
    call u64_write_str
    mov ecx, dword [rdi]
    call u64_print_u64
    jmp .nl
.fqw:
    mov rcx, qword [rdi]
    call u64_print_u64
    jmp .nl
.fbin:
    xor ebx, ebx
.blp:
    cmp ebx, r12d
    jae .nl
    mov cl, byte [rdi + rbx]
    call u64_print_hex8
    inc ebx
    jmp .blp
.nl:
    call u64_write_crlf
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 1=error)
;; @err Exits 1 on Registry error or invalid syntax
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 REG.EXE.
@proc_frame mainCRTStartup, 80, rbx, rsi, rdi, r12, r13
    call u64_init
    mov rsi, rax

    mov dword [rel g_reg64_type], 1
    mov dword [rel g_reg64_has_v], 0
    mov dword [rel g_reg64_wow], 0
    mov byte [rel g_reg64_op], 0
    mov byte [rel g_reg64_k1], 0
    mov byte [rel g_reg64_k2], 0
    mov byte [rel g_reg64_val], 0
    mov byte [rel g_reg64_data], 0

    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .bad
    mov rcx, rsi
    lea rdx, [rel g_reg64_op]
    call u64_read_tok
    mov rsi, rax
    cmp byte [rel g_reg64_op], '/'
    jne .prest
    cmp byte [rel g_reg64_op + 1], '?'
    je .help
    jmp .bad

.prest:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .disp
    cmp dl, '/'
    jne .pk
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'V'
    je .sv
    cmp al, 'T'
    je .st
    cmp al, 'D'
    je .sd
    cmp al, 'R'
    je .swow
    jmp .prest
.sv:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'E'
    je .sve
    mov rcx, rsi
    lea rdx, [rel g_reg64_val]
    call u64_read_tok
    mov rsi, rax
    mov dword [rel g_reg64_has_v], 1
    jmp .prest
.sve:
    mov byte [rel g_reg64_val], 0
    mov dword [rel g_reg64_has_v], 1
    jmp .prest
.st:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    lea rcx, [rel g_u64_arg_b]
    call reg64_parse_type
    mov dword [rel g_reg64_type], eax
    jmp .prest
.sd:
    mov rcx, rsi
    lea rdx, [rel g_reg64_data]
    call u64_read_tok
    mov rsi, rax
    jmp .prest
.swow:
    cmp byte [rel g_u64_arg_a + 5], '3'
    je .w32
    mov dword [rel g_reg64_wow], 0x0100
    jmp .prest
.w32:
    mov dword [rel g_reg64_wow], 0x0200
    jmp .prest
.pk:
    cmp byte [rel g_reg64_k1], 0
    jne .pk2
    mov rcx, rsi
    lea rdx, [rel g_reg64_k1]
    call u64_read_tok
    mov rsi, rax
    jmp .prest
.pk2:
    mov rcx, rsi
    lea rdx, [rel g_reg64_k2]
    call u64_read_tok
    mov rsi, rax
    jmp .prest

.help:
    lea rcx, [rel szR64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.disp:
    call reg64_load_advapi
    test eax, eax
    jz .err

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Import]
    call u64_str_eq_ci
    test eax, eax
    jnz .do_imp

    cmp byte [rel g_reg64_k1], 0
    je .bad
    lea rcx, [rel g_reg64_k1]
    call reg64_parse_hive
    test rax, rax
    jz .bad
    mov qword [rel g_reg64_root], rax

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Add]
    call u64_str_eq_ci
    test eax, eax
    jnz .do_add

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Query]
    call u64_str_eq_ci
    test eax, eax
    jnz .do_query

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Delete]
    call u64_str_eq_ci
    test eax, eax
    jnz .do_del

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Export]
    call u64_str_eq_ci
    test eax, eax
    jnz .do_exp

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Save]
    call u64_str_eq_ci
    test eax, eax
    jnz .do_exp

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Copy]
    call u64_str_eq_ci
    test eax, eax
    jnz .do_copy

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Compare]
    call u64_str_eq_ci
    test eax, eax
    jnz .ok_msg

    lea rcx, [rel g_reg64_op]
    lea rdx, [rel szO64Flags]
    call u64_str_eq_ci
    test eax, eax
    jnz .ok_msg
    jmp .bad

.do_add:
    lea rcx, [rel g_reg64_subkey]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w

    mov rcx, qword [rel g_reg64_root]
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    xor r9d, r9d
    mov dword [rsp + 32], 0
    mov eax, 0x20006
    or eax, dword [rel g_reg64_wow]
    mov dword [rsp + 40], eax
    mov qword [rsp + 48], 0
    lea rax, [rel g_reg64_hkey]
    mov qword [rsp + 56], rax
    lea rax, [rel g_reg64_disp]
    mov qword [rsp + 64], rax
    call qword [rel g_pfn64RCreate]
    test eax, eax
    jnz .err

    lea rcx, [rel g_reg64_val]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w

    cmp dword [rel g_reg64_type], 4
    je .add_dw
    cmp dword [rel g_reg64_type], 11
    je .add_qw

    lea rcx, [rel g_reg64_data]
    lea rdx, [rel g_u64_wbuf2]
    call u64_u8_to_w
    lea eax, [eax + eax + 2]
    mov dword [rsp + 40], eax
    lea rdx, [rel g_u64_wbuf2]
    mov qword [rsp + 32], rdx
    mov rcx, qword [rel g_reg64_hkey]
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    mov r9d, dword [rel g_reg64_type]
    call qword [rel g_pfn64RSet]
    mov ebx, eax
    jmp .acl

.add_dw:
    lea rcx, [rel g_reg64_data]
    cmp byte [rcx], '0'
    jne .dwd
    mov al, byte [rcx + 1]
    call u64_chr_upper
    cmp al, 'X'
    jne .dwd
    add rcx, 2
    xor eax, eax
.hlp:
    movzx edx, byte [rcx]
    test dl, dl
    jz .stdw
    cmp dl, 'a'
    jb .hu
    cmp dl, 'z'
    ja .hu
    sub dl, 32
.hu:
    cmp dl, '0'
    jb .stdw
    cmp dl, '9'
    jbe .hd
    cmp dl, 'A'
    jb .stdw
    cmp dl, 'F'
    ja .stdw
    sub dl, 55
    jmp .ha
.hd:
    sub dl, '0'
.ha:
    shl eax, 4
    or eax, edx
    inc rcx
    jmp .hlp
.dwd:
    call u64_parse_u32
.stdw:
    mov dword [rel g_reg64_dw], eax
    mov rcx, qword [rel g_reg64_hkey]
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    mov r9d, 4
    lea rax, [rel g_reg64_dw]
    mov qword [rsp + 32], rax
    mov dword [rsp + 40], 4
    call qword [rel g_pfn64RSet]
    mov ebx, eax
    jmp .acl

.add_qw:
    lea rcx, [rel g_reg64_data]
    call u64_parse_u32
    mov qword [rel g_reg64_dw], rax
    mov rcx, qword [rel g_reg64_hkey]
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    mov r9d, 11
    lea rax, [rel g_reg64_dw]
    mov qword [rsp + 32], rax
    mov dword [rsp + 40], 8
    call qword [rel g_pfn64RSet]
    mov ebx, eax

.acl:
    mov rcx, qword [rel g_reg64_hkey]
    call qword [rel g_pfn64RClose]
    test ebx, ebx
    jnz .err
    jmp .ok_msg

.do_query:
    lea rcx, [rel g_reg64_subkey]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w

    mov rcx, qword [rel g_reg64_root]
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    mov r9d, 0x20019
    or r9d, dword [rel g_reg64_wow]
    lea rax, [rel g_reg64_hkey]
    mov qword [rsp + 32], rax
    call qword [rel g_pfn64ROpen]
    test eax, eax
    jnz .err

    call u64_write_crlf
    lea rcx, [rel g_reg64_k1]
    call u64_write_str
    call u64_write_crlf

    cmp dword [rel g_reg64_has_v], 0
    je .enum_v

    lea rcx, [rel g_reg64_val]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    mov dword [rel g_reg64_cb], 4096
    mov rcx, qword [rel g_reg64_hkey]
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    lea r9, [rel g_reg64_qtype]
    mov rax, qword [rel g_u64_arena]
    mov qword [rsp + 32], rax
    lea rax, [rel g_reg64_cb]
    mov qword [rsp + 40], rax
    call qword [rel g_pfn64RQuery]
    mov ebx, eax
    mov rcx, qword [rel g_reg64_hkey]
    call qword [rel g_pfn64RClose]
    test ebx, ebx
    jnz .err
    lea rcx, [rel g_reg64_val]
    mov edx, dword [rel g_reg64_qtype]
    mov r8, qword [rel g_u64_arena]
    mov r9d, dword [rel g_reg64_cb]
    call reg64_print_val_row
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.enum_v:
    xor r12d, r12d
.ev_lp:
    mov dword [rel g_reg64_cch], 500
    mov dword [rel g_reg64_cb], 4096
    mov rcx, qword [rel g_reg64_hkey]
    mov edx, r12d
    lea r8, [rel g_u64_wbuf1]
    lea r9, [rel g_reg64_cch]
    mov qword [rsp + 32], 0
    lea rax, [rel g_reg64_qtype]
    mov qword [rsp + 40], rax
    mov rax, qword [rel g_u64_arena]
    mov qword [rsp + 48], rax
    lea rax, [rel g_reg64_cb]
    mov qword [rsp + 56], rax
    call qword [rel g_pfn64REnumVal]
    test eax, eax
    jnz .ev_dn
    lea rcx, [rel g_u64_wbuf1]
    lea rdx, [rel g_reg64_val]
    mov r8d, 500
    call u64_w_to_u8
    lea rcx, [rel g_reg64_val]
    mov edx, dword [rel g_reg64_qtype]
    mov r8, qword [rel g_u64_arena]
    mov r9d, dword [rel g_reg64_cb]
    call reg64_print_val_row
    inc r12d
    jmp .ev_lp
.ev_dn:
    mov rcx, qword [rel g_reg64_hkey]
    call qword [rel g_pfn64RClose]
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.do_del:
    cmp dword [rel g_reg64_has_v], 0
    je .del_k
    lea rcx, [rel g_reg64_subkey]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    mov rcx, qword [rel g_reg64_root]
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    mov r9d, 0x20006
    or r9d, dword [rel g_reg64_wow]
    lea rax, [rel g_reg64_hkey]
    mov qword [rsp + 32], rax
    call qword [rel g_pfn64ROpen]
    test eax, eax
    jnz .err
    lea rcx, [rel g_reg64_val]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    mov rcx, qword [rel g_reg64_hkey]
    lea rdx, [rel g_u64_wbuf1]
    call qword [rel g_pfn64RDelVal]
    mov ebx, eax
    mov rcx, qword [rel g_reg64_hkey]
    call qword [rel g_pfn64RClose]
    test ebx, ebx
    jnz .err
    jmp .ok_msg

.del_k:
    lea rcx, [rel g_reg64_subkey]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    mov rcx, qword [rel g_reg64_root]
    lea rdx, [rel g_u64_wbuf1]
    call qword [rel g_pfn64RDelKey]
    test eax, eax
    jnz .err
    jmp .ok_msg

.do_exp:
    cmp byte [rel g_reg64_k2], 0
    je .bad
    mov rdi, qword [rel g_u64_arena]
    mov rcx, rdi
    lea rdx, [rel szR64ExpHdr]
    call u64_strcpy
    add rdi, rax
    mov byte [rdi], '['
    inc rdi
    mov rcx, rdi
    lea rdx, [rel g_reg64_k1]
    call u64_strcpy
    add rdi, rax
    mov byte [rdi], ']'
    mov byte [rdi + 1], 13
    mov byte [rdi + 2], 10
    add rdi, 3
    mov r8, rdi
    sub r8, qword [rel g_u64_arena]
    lea rcx, [rel g_reg64_k2]
    mov rdx, qword [rel g_u64_arena]
    call u64_save_file
    test eax, eax
    jz .err
    jmp .ok_msg

.do_imp:
    cmp byte [rel g_reg64_k1], 0
    je .bad
    lea rcx, [rel g_reg64_k1]
    mov rdx, qword [rel g_u64_arena]
    mov r8d, 65536
    mov r9d, 1
    call u64_load_stream
    jc .err
    jmp .ok_msg

.do_copy:
    cmp byte [rel g_reg64_k2], 0
    je .bad
    lea rcx, [rel g_reg64_k2]
    call reg64_parse_hive
    test rax, rax
    jz .bad
    mov rbx, rax
    lea rcx, [rel g_reg64_subkey]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    mov rcx, rbx
    lea rdx, [rel g_u64_wbuf1]
    xor r8d, r8d
    xor r9d, r9d
    mov dword [rsp + 32], 0
    mov dword [rsp + 40], 0x20006
    mov qword [rsp + 48], 0
    lea rax, [rel g_reg64_hkey]
    mov qword [rsp + 56], rax
    lea rax, [rel g_reg64_disp]
    mov qword [rsp + 64], rax
    call qword [rel g_pfn64RCreate]
    test eax, eax
    jnz .err
    mov rcx, qword [rel g_reg64_hkey]
    call qword [rel g_pfn64RClose]

.ok_msg:
    lea rcx, [rel szR64Ok]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szR64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret
.err:
    lea rcx, [rel szR64Err]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szFn64ROpen, "RegOpenKeyExW", 0
@data db szFn64RCreate, "RegCreateKeyExW", 0
@data db szFn64RSet, "RegSetValueExW", 0
@data db szFn64RQuery, "RegQueryValueExW", 0
@data db szFn64RDelVal, "RegDeleteValueW", 0
@data db szFn64RDelKey, "RegDeleteKeyW", 0
@data db szFn64REnumVal, "RegEnumValueW", 0
@data db szFn64RClose, "RegCloseKey", 0

@data db szH64Hkcu, "HKCU", 0
@data db szH64HkcuL, "HKEY_CURRENT_USER", 0
@data db szH64Hklm, "HKLM", 0
@data db szH64HklmL, "HKEY_LOCAL_MACHINE", 0
@data db szH64Hkcr, "HKCR", 0
@data db szH64HkcrL, "HKEY_CLASSES_ROOT", 0
@data db szH64Hku, "HKU", 0
@data db szH64Hkcc, "HKCC", 0

@data db szO64Query, "QUERY", 0
@data db szO64Add, "ADD", 0
@data db szO64Delete, "DELETE", 0
@data db szO64Copy, "COPY", 0
@data db szO64Save, "SAVE", 0
@data db szO64Export, "EXPORT", 0
@data db szO64Import, "IMPORT", 0
@data db szO64Compare, "COMPARE", 0
@data db szO64Flags, "FLAGS", 0

@data db szT64Sz, "REG_SZ", 0
@data db szT64Exp, "REG_EXPAND_SZ", 0
@data db szT64Bin, "REG_BINARY", 0
@data db szT64Dw, "REG_DWORD", 0
@data db szT64Msz, "REG_MULTI_SZ", 0
@data db szT64Qw, "REG_QWORD", 0

@data db szR64Ind, "    ", 0
@data db szR64Def, "(Default)", 0
@data db szR64Hex, "0x", 0
@data db szR64ExpHdr, "Windows Registry Editor Version 5.00", 13, 10, 13, 10, 0
@data db szR64Ok, "The operation completed successfully.", 0
@data db szR64Err, "ERROR: The system was unable to find the specified registry key or value.", 0
@data db szR64Syn, "ERROR: Invalid syntax.", 0
@data db szR64Help, "Performs operations on registry subkey information and values (QUERY/ADD/DELETE/EXPORT/IMPORT).", 0

@include "u_data64.inc"

@res qword g_pfn64ROpen, 1
@res qword g_pfn64RCreate, 1
@res qword g_pfn64RSet, 1
@res qword g_pfn64RQuery, 1
@res qword g_pfn64RDelVal, 1
@res qword g_pfn64RDelKey, 1
@res qword g_pfn64REnumVal, 1
@res qword g_pfn64RClose, 1
@res qword g_reg64_root, 1
@res qword g_reg64_hkey, 1
@res qword g_reg64_dw, 1
@res dword g_reg64_disp, 1
@res dword g_reg64_type, 1
@res dword g_reg64_qtype, 1
@res dword g_reg64_cb, 1
@res dword g_reg64_cch, 1
@res dword g_reg64_has_v, 1
@res dword g_reg64_wow, 1
@res byte g_reg64_op, 64
@res byte g_reg64_k1, 520
@res byte g_reg64_k2, 520
@res byte g_reg64_subkey, 520
@res byte g_reg64_val, 520
@res byte g_reg64_data, 1024
