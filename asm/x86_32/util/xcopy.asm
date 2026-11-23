;; @file xcopy.asm
;; @isa 386
;; @desc Standalone IA-32 XCOPY.EXE file/tree copier (/A /M /D /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc xc_is_excluded
;; @abi win32
;; @in ESI = candidate path UTF-8
;; @out EAX = 1 if path matches any /EXCLUDE substring, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Checks candidate path against newline-separated substrings loaded in g_xc_excl_buf.
@proc xc_is_excluded
    push ebx
    push esi
    push edi
    push ebp
    cmp dword [g_xc_has_excl], 0
    je .no

    call u32_strlen
    mov ebp, eax ; haystack len
    mov edi, addr(g_xc_excl_buf)

.ln_lp:
    cmp byte [edi], 0
    je .no
    mov ebx, edi
.eol:
    mov al, byte [ebx]
    test al, al
    jz .got_pat
    cmp al, 13
    je .got_pat
    cmp al, 10
    je .got_pat
    inc ebx
    jmp .eol

.got_pat:
    mov edx, ebx
    sub edx, edi
    jz .adv
    cmp ebp, edx
    jb .adv
    mov ecx, ebp
    sub ecx, edx
    inc ecx
    push esi
.pos_lp:
    xor eax, eax
.ch_lp:
    cmp eax, edx
    jae .pop_yes
    push eax
    push ecx
    mov cl, byte [esi + eax]
    mov al, cl
    call u32_chr_upper
    mov ch, al
    mov eax, dword [esp + 4]
    mov al, byte [edi + eax]
    call u32_chr_upper
    cmp ch, al
    pop ecx
    pop eax
    jne .nx_pos
    inc eax
    jmp .ch_lp
.nx_pos:
    inc esi
    dec ecx
    jnz .pos_lp
    pop esi

.adv:
    cmp byte [ebx], 13
    jne .c_lf
    inc ebx
.c_lf:
    cmp byte [ebx], 10
    jne .nx_l
    inc ebx
.nx_l:
    mov edi, ebx
    jmp .ln_lp

.pop_yes:
    pop esi
    mov eax, 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
.no:
    xor eax, eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc xc_copy_one
;; @abi win32
;; @in ESI = src file UTF-8, EDI = dst file UTF-8, ECX = src attributes
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Copies a single file honoring /A, /M, /H, /R, /U, /K, /L, /Q, /F, /T, and /EXCLUDE.
@proc xc_copy_one
    push ebx
    push esi
    push edi
    mov ebx, ecx

    cmp dword [g_xc_tree_only], 0
    jne .dn

    ; Skip hidden/system unless /H
    cmp dword [g_xc_hid], 0
    jne .chk_arch
    test ebx, 0x06
    jnz .dn

.chk_arch:
    cmp dword [g_xc_arch], 0
    je .chk_excl
    test ebx, 0x20
    jz .dn

.chk_excl:
    call xc_is_excluded
    test eax, eax
    jnz .dn

    ; /U: copy only if destination already exists
    cmp dword [g_xc_upd_only], 0
    je .chk_list
    push esi
    mov esi, edi
    call u32_get_attr
    pop esi
    cmp eax, -1
    je .dn

.chk_list:
    cmp dword [g_xc_quiet], 0
    jne .chk_dry
    cmp dword [g_xc_full], 0
    je .pr_src
    call u32_write_str
    push esi
    mov esi, addr(szXcArrow)
    call u32_write_str
    mov esi, edi
    call u32_write_str
    pop esi
    call u32_write_crlf
    jmp .chk_dry
.pr_src:
    call u32_write_str
    call u32_write_crlf

.chk_dry:
    cmp dword [g_xc_list_only], 0
    je .do_cp
    inc dword [g_xc_copied]
    jmp .dn

.do_cp:
    ; Strip read-only on destination if /R
    cmp dword [g_xc_ovr_ro], 0
    je .cp_call
    push esi
    mov esi, edi
    call u32_get_attr
    cmp eax, -1
    je .ro_dn
    test eax, 1
    jz .ro_dn
    mov ecx, eax
    and ecx, 0xFFFFFFFE
    jnz .ro_set
    mov ecx, 0x80
.ro_set:
    call u32_set_attr
.ro_dn:
    pop esi

.cp_call:
    xor ecx, ecx
    call u32_copy_file
    test eax, eax
    jz .dn
    inc dword [g_xc_copied]

    ; Clear read-only on destination unless /K
    cmp dword [g_xc_keep_attr], 0
    jne .chk_m
    push esi
    mov esi, edi
    mov ecx, 0x20
    call u32_set_attr
    pop esi

.chk_m:
    cmp dword [g_xc_clr_arch], 0
    je .dn
    mov ecx, ebx
    and ecx, 0xFFFFFFDF
    jnz .m_set
    mov ecx, 0x80
.m_set:
    call u32_set_attr

.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc xc_copy_dir
;; @abi win32
;; @in ESI = src dir UTF-8, EDI = dst dir UTF-8, ECX = depth
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Copies files in ESI to EDI and recurses under /S or /E.
@proc xc_copy_dir
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 2656
    ; [esp+0..543] = find entry
    ; [esp+544..1055] = src dir
    ; [esp+1056..1567] = dst dir
    ; [esp+1568..2079] = src child
    ; [esp+2080..2591] = dst child
    ; [esp+2592] = depth, [esp+2596] = created_dst
    cmp ecx, 16
    jae .dn
    mov dword [esp + 2592], ecx
    mov dword [esp + 2596], 0
    push edi
    lea edi, [esp + 548]
    call u32_strcpy
    pop esi
    lea edi, [esp + 1056]
    call u32_strcpy

    cmp dword [g_xc_empty], 0
    jne .mk_dst
    cmp dword [g_xc_tree_only], 0
    je .enum_it
.mk_dst:
    cmp dword [g_xc_list_only], 0
    jne .enum_it
    lea esi, [esp + 1056]
    call u32_mkdir
    mov dword [esp + 2596], 1

.enum_it:
    lea esi, [esp + 544]
    mov edi, addr(szXcStar)
    lea ebx, [esp + 1568]
    call u32_path_join
    lea esi, [esp + 1568]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .dn
    mov ebp, eax

.lp:
    cmp byte [esp + 20], '.'
    jne .not_dot
    cmp byte [esp + 21], 0
    je .nx
    cmp byte [esp + 21], '.'
    jne .not_dot
    cmp byte [esp + 22], 0
    je .nx
.not_dot:
    lea esi, [esp + 544]
    lea edi, [esp + 20]
    lea ebx, [esp + 1568]
    call u32_path_join

    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 2080]
    call u32_path_join

    test dword [esp], 0x10
    jnz .sub_d

    cmp dword [esp + 2596], 0
    jne .do_f
    cmp dword [g_xc_list_only], 0
    jne .do_f
    lea esi, [esp + 1056]
    call u32_mkdir
    mov dword [esp + 2596], 1
.do_f:
    lea esi, [esp + 1568]
    lea edi, [esp + 2080]
    mov ecx, dword [esp]
    call xc_copy_one
    jmp .nx

.sub_d:
    cmp dword [g_xc_sub], 0
    je .nx
    lea esi, [esp + 1568]
    lea edi, [esp + 2080]
    mov ecx, dword [esp + 2592]
    inc ecx
    call xc_copy_dir

.nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .lp
    push ebp
    call dword [__imp__FindClose@4]
.dn:
    add esp, 2656
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 4=syntax/init error)
;; @err Exits 4 on invalid syntax
;; @clobbers eax, ecx, edx
;; @desc Entrypoint for XCOPY.EXE supporting single-file and recursive tree copying.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_xc_arch], 0
    mov dword [g_xc_clr_arch], 0
    mov dword [g_xc_sub], 0
    mov dword [g_xc_empty], 0
    mov dword [g_xc_quiet], 0
    mov dword [g_xc_full], 0
    mov dword [g_xc_list_only], 0
    mov dword [g_xc_hid], 0
    mov dword [g_xc_ovr_ro], 0
    mov dword [g_xc_tree_only], 0
    mov dword [g_xc_upd_only], 0
    mov dword [g_xc_keep_attr], 0
    mov dword [g_xc_has_excl], 0
    mov dword [g_xc_copied], 0
    mov byte [g_u32_arg_b], 0
    mov byte [g_u32_arg_c], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pos_arg
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'A'
    je .sw_a
    cmp al, 'M'
    je .sw_m
    cmp al, 'S'
    je .sw_s
    cmp al, 'E'
    je .sw_e_or_excl
    cmp al, 'Q'
    je .sw_q
    cmp al, 'F'
    je .sw_f
    cmp al, 'L'
    je .sw_l
    cmp al, 'H'
    je .sw_h
    cmp al, 'R'
    je .sw_r
    cmp al, 'T'
    je .sw_t
    cmp al, 'U'
    je .sw_u
    cmp al, 'K'
    je .sw_k
    jmp .arg_lp
.sw_a:
    mov dword [g_xc_arch], 1
    jmp .arg_lp
.sw_m:
    mov dword [g_xc_arch], 1
    mov dword [g_xc_clr_arch], 1
    jmp .arg_lp
.sw_s:
    mov dword [g_xc_sub], 1
    jmp .arg_lp
.sw_e_or_excl:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'X'
    je .sw_excl
    mov dword [g_xc_sub], 1
    mov dword [g_xc_empty], 1
    jmp .arg_lp
.sw_excl:
    ; /EXCLUDE:file
    xor ecx, ecx
.find_cl:
    mov al, byte [g_u32_arg_a + ecx]
    test al, al
    jz .arg_lp
    cmp al, ':'
    je .got_cl
    inc ecx
    jmp .find_cl
.got_cl:
    push esi
    lea esi, [g_u32_arg_a + ecx + 1]
    mov edi, addr(g_xc_excl_buf)
    mov ecx, 4000
    mov edx, 1
    call u32_load_stream
    pop esi
    jc .arg_lp
    mov dword [g_xc_has_excl], 1
    jmp .arg_lp
.sw_q:
    mov dword [g_xc_quiet], 1
    jmp .arg_lp
.sw_f:
    mov dword [g_xc_full], 1
    jmp .arg_lp
.sw_l:
    mov dword [g_xc_list_only], 1
    jmp .arg_lp
.sw_h:
    mov dword [g_xc_hid], 1
    jmp .arg_lp
.sw_r:
    mov dword [g_xc_ovr_ro], 1
    jmp .arg_lp
.sw_t:
    mov dword [g_xc_tree_only], 1
    mov dword [g_xc_sub], 1
    jmp .arg_lp
.sw_u:
    mov dword [g_xc_upd_only], 1
    jmp .arg_lp
.sw_k:
    mov dword [g_xc_keep_attr], 1
    jmp .arg_lp

.pos_arg:
    cmp byte [g_u32_arg_b], 0
    jne .pos_2
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp
.pos_2:
    mov edi, addr(g_u32_arg_c)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szXcHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.run:
    cmp byte [g_u32_arg_b], 0
    je .bad_syn
    cmp byte [g_u32_arg_c], 0
    jne .got_dst
    mov edi, addr(g_u32_arg_c)
    mov ecx, 1000
    call u32_get_cwd

.got_dst:
    mov esi, addr(g_u32_arg_b)
    call u32_get_attr
    cmp eax, -1
    je .not_dir_src
    test eax, 0x10
    jz .single_file_src
    mov esi, addr(g_u32_arg_b)
    mov edi, addr(g_u32_arg_c)
    xor ecx, ecx
    call xc_copy_dir
    jmp .summary

.single_file_src:
    mov ebx, eax ; src attr
    mov esi, addr(g_u32_arg_c)
    call u32_get_attr
    cmp eax, -1
    je .direct_dst
    test eax, 0x10
    jz .direct_dst
    ; Extract leaf from g_u32_arg_b and join onto g_u32_arg_c
    mov esi, addr(g_u32_arg_b)
    call u32_strlen
    mov edx, -1
    xor ecx, ecx
.sl_lp:
    cmp ecx, eax
    jae .sl_dn
    mov dl, byte [g_u32_arg_b + ecx]
    cmp dl, 0x5C
    je .sl_mk
    cmp dl, '/'
    jne .sl_nx
.sl_mk:
    mov dword [g_xc_tmp_sep], ecx
    jmp .sl_nx2
.sl_nx:
    jmp .sl_nx3
.sl_nx2:
    mov edx, 1
.sl_nx3:
    inc ecx
    jmp .sl_lp
.sl_dn:
    lea edi, [g_u32_arg_b]
    cmp edx, -1
    je .join_lf
    mov eax, dword [g_xc_tmp_sep]
    lea edi, [g_u32_arg_b + eax + 1]
.join_lf:
    push ebx
    mov esi, addr(g_u32_arg_c)
    mov ebx, addr(g_u32_path_tmp)
    call u32_path_join
    pop ecx
    mov esi, addr(g_u32_arg_b)
    mov edi, addr(g_u32_path_tmp)
    call xc_copy_one
    jmp .summary

.direct_dst:
    mov esi, addr(g_u32_arg_b)
    mov edi, addr(g_u32_arg_c)
    mov ecx, ebx
    call xc_copy_one
    jmp .summary

.not_dir_src:
    mov esi, addr(szXcNf)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 4
    call u32_exit
    jmp .done

.summary:
    mov eax, dword [g_xc_copied]
    call u32_print_u32
    mov esi, addr(szXcSummary)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_syn:
    mov esi, addr(szXcSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 4
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szXcStar, "*", 0
@data db szXcArrow, " -> ", 0
@data db szXcSummary, " File(s) copied", 0
@data db szXcNf, "File not found", 0
@data db szXcSyn, "Invalid number of parameters", 0
@data db szXcHelp, "Copies files and directory trees (/A /M /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).", 0

@include "u_data32.inc"

@res dword g_xc_arch, 1
@res dword g_xc_clr_arch, 1
@res dword g_xc_sub, 1
@res dword g_xc_empty, 1
@res dword g_xc_quiet, 1
@res dword g_xc_full, 1
@res dword g_xc_list_only, 1
@res dword g_xc_hid, 1
@res dword g_xc_ovr_ro, 1
@res dword g_xc_tree_only, 1
@res dword g_xc_upd_only, 1
@res dword g_xc_keep_attr, 1
@res dword g_xc_has_excl, 1
@res dword g_xc_copied, 1
@res dword g_xc_tmp_sep, 1
@res byte g_xc_excl_buf, 4096
