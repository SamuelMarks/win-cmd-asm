;; @file replace.asm
;; @isa 386
;; @desc Standalone IA-32 REPLACE.EXE file updater (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc rep_get_mtime
;; @abi win32
;; @in ESI = file path UTF-8, EDI = 8-byte FILETIME output
;; @out EAX = 1 on success, 0 on failure
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Queries last-write FILETIME via OpenRead + GetFileTime.
@proc rep_get_mtime
    push ebx
    push edi
    call u32_open_read
    cmp eax, -1
    je .fail
    mov ebx, eax
    push edi ; lpLastWriteTime
    push 0
    push 0
    push ebx
    call dword [__imp__GetFileTime@16]
    push eax
    push ebx
    call dword [__imp__CloseHandle@4]
    pop eax
    pop edi
    pop ebx
    ret
.fail:
    xor eax, eax
    pop edi
    pop ebx
    ret
@endproc

;; @proc rep_apply_dir
;; @abi win32
;; @in ESI = target directory UTF-8, ECX = depth
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Replaces existing target files (or adds missing under /A) and recurses under /S.
@proc rep_apply_dir
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 1664
    cmp ecx, 16
    jae .dn
    mov dword [esp + 1632], ecx
    lea edi, [esp + 544]
    call u32_strcpy

    lea esi, [esp + 544]
    mov edi, addr(g_rep_leaf)
    lea ebx, [esp + 1088]
    call u32_path_join

    lea esi, [esp + 1088]
    call u32_get_attr
    mov ebx, eax ; target attr (-1 if absent)

    cmp dword [g_rep_add], 0
    jne .add_mode

    cmp ebx, -1
    je .chk_sub
    test ebx, 0x10
    jnz .chk_sub

    ; /U: replace only if target is older than source
    cmp dword [g_rep_older], 0
    je .chk_ro
    lea esi, [esp + 1088]
    lea edi, [esp + 1640] ; target FILETIME
    call rep_get_mtime
    test eax, eax
    jz .chk_ro
    lea eax, [esp + 1640]
    push eax
    push addr(g_rep_src_ft)
    call dword [__imp__CompareFileTime@8]
    cmp eax, 0
    jle .chk_sub

.chk_ro:
    test ebx, 1
    jz .do_rep
    cmp dword [g_rep_ro], 0
    je .chk_sub
    lea esi, [esp + 1088]
    mov ecx, 0x80
    call u32_set_attr

.do_rep:
    mov esi, addr(g_rep_src)
    lea edi, [esp + 1088]
    xor ecx, ecx
    call u32_copy_file
    test eax, eax
    jz .chk_sub
    inc dword [g_rep_cnt]
    jmp .chk_sub

.add_mode:
    cmp ebx, -1
    jne .chk_sub
    mov esi, addr(g_rep_src)
    lea edi, [esp + 1088]
    mov ecx, 1
    call u32_copy_file
    test eax, eax
    jz .chk_sub
    inc dword [g_rep_cnt]

.chk_sub:
    cmp dword [g_rep_sub], 0
    je .dn
    lea esi, [esp + 544]
    mov edi, addr(szRepStar)
    lea ebx, [esp + 1088]
    call u32_path_join
    lea esi, [esp + 1088]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .dn
    mov ebp, eax
.sub_lp:
    test dword [esp], 0x10
    jz .nx
    cmp byte [esp + 20], '.'
    jne .go
    cmp byte [esp + 21], 0
    je .nx
    cmp byte [esp + 21], '.'
    jne .go
    cmp byte [esp + 22], 0
    je .nx
.go:
    lea esi, [esp + 544]
    lea edi, [esp + 20]
    lea ebx, [esp + 1088]
    call u32_path_join
    lea esi, [esp + 1088]
    mov ecx, dword [esp + 1632]
    inc ecx
    call rep_apply_dir
.nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .sub_lp
    push ebp
    call dword [__imp__FindClose@4]
.dn:
    add esp, 1664
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 2=source missing, 3=syntax)
;; @err Exits non-zero on error
;; @clobbers eax, ecx, edx
;; @desc Parses REPLACE switches (/A /P /R /S /W /U) and updates/adds files in target tree.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_rep_add], 0
    mov dword [g_rep_ro], 0
    mov dword [g_rep_sub], 0
    mov dword [g_rep_older], 0
    mov dword [g_rep_cnt], 0
    mov byte [g_rep_src], 0
    mov byte [g_rep_dst], 0

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
    cmp al, 'R'
    je .sw_r
    cmp al, 'S'
    je .sw_s
    cmp al, 'U'
    je .sw_u
    jmp .arg_lp
.sw_a:
    mov dword [g_rep_add], 1
    jmp .arg_lp
.sw_r:
    mov dword [g_rep_ro], 1
    jmp .arg_lp
.sw_s:
    mov dword [g_rep_sub], 1
    jmp .arg_lp
.sw_u:
    mov dword [g_rep_older], 1
    jmp .arg_lp

.pos_arg:
    cmp byte [g_rep_src], 0
    jne .pos_2
    mov edi, addr(g_rep_src)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp
.pos_2:
    mov edi, addr(g_rep_dst)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szRepHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.run:
    cmp byte [g_rep_src], 0
    je .bad_syn
    mov esi, addr(g_rep_src)
    call u32_get_attr
    cmp eax, -1
    je .no_src

    mov esi, addr(g_rep_src)
    mov edi, addr(g_rep_src_ft)
    call rep_get_mtime

    cmp byte [g_rep_dst], 0
    jne .extr_leaf
    mov edi, addr(g_rep_dst)
    mov ecx, 1000
    call u32_get_cwd

.extr_leaf:
    mov esi, addr(g_rep_src)
    call u32_strlen
    mov edx, -1
    xor ecx, ecx
.sl:
    cmp ecx, eax
    jae .sl_dn
    mov bl, byte [g_rep_src + ecx]
    cmp bl, 0x5C
    je .mk
    cmp bl, '/'
    je .mk
    cmp bl, ':'
    jne .nx
.mk:
    mov edx, ecx
.nx:
    inc ecx
    jmp .sl
.sl_dn:
    lea esi, [g_rep_src + edx + 1]
    mov edi, addr(g_rep_leaf)
    call u32_strcpy

    mov esi, addr(g_rep_dst)
    xor ecx, ecx
    call rep_apply_dir

    mov eax, dword [g_rep_cnt]
    call u32_print_u32
    cmp dword [g_rep_add], 0
    jne .added_msg
    mov esi, addr(szRepDone)
    jmp .pr_msg
.added_msg:
    mov esi, addr(szRepAdded)
.pr_msg:
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.no_src:
    mov esi, addr(szRepNoSrc)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 2
    call u32_exit
    jmp .done
.bad_syn:
    mov esi, addr(szRepSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 3
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szRepStar, "*", 0
@data db szRepDone, " file(s) replaced", 0
@data db szRepAdded, " file(s) added", 0
@data db szRepNoSrc, "Source path required", 0
@data db szRepSyn, "Invalid parameter", 0
@data db szRepHelp, "Replaces files in destination directories (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).", 0

@include "u_data32.inc"

@res dword g_rep_add, 1
@res dword g_rep_ro, 1
@res dword g_rep_sub, 1
@res dword g_rep_older, 1
@res dword g_rep_cnt, 1
@res byte g_rep_src_ft, 8
@res byte g_rep_src, 520
@res byte g_rep_dst, 520
@res byte g_rep_leaf, 520
