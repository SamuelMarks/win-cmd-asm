;; @file tree.asm
;; @isa 386
;; @desc Standalone IA-32 TREE.EXE directory visualizer ([drive:][path] [/F] [/A] with Unicode & ASCII box-drawing).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc tree_indent
;; @abi win32
;; @in ECX = nesting depth (0..15)
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints ancestor vertical bar connectors up to depth ECX.
@proc tree_indent
    push ebx
    push esi
    push edi
    mov edi, ecx
    xor ebx, ebx
.lp:
    cmp ebx, edi
    jae .dn
    cmp byte [g_tree_last + ebx], 0
    jne .blank
    cmp dword [g_tree_ascii], 0
    jne .bar_asc
    mov esi, addr(szTreeUniBar)
    call u32_write_str
    jmp .nx
.bar_asc:
    mov esi, addr(szTreeAscBar)
    call u32_write_str
    jmp .nx
.blank:
    mov esi, addr(szTreeBlank)
    call u32_write_str
.nx:
    inc ebx
    jmp .lp
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc tree_walk
;; @abi win32
;; @in ESI = directory path UTF-8, ECX = depth (0..15)
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Recursively prints files (/F) and subdirectories with box-drawing connectors.
@proc tree_walk
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 1600
    ; [esp+0..543] = find entry
    ; [esp+544..1055] = child/wildcard path
    ; [esp+1056..1567] = current dir path
    ; [esp+1568] = depth, [esp+1572] = subdirs total, [esp+1576] = subdirs seen
    cmp ecx, 15
    jae .done
    mov dword [esp + 1568], ecx
    lea edi, [esp + 1056]
    call u32_strcpy

    ; Count subdirectories first so last-child connector (└── vs ├──) is exact
    mov dword [esp + 1572], 0
    lea esi, [esp + 1056]
    mov edi, addr(szTreeStar)
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .done
    mov ebp, eax

.cnt_lp:
    cmp byte [esp + 20], '.'
    jne .cnt_chk
    cmp byte [esp + 21], 0
    je .cnt_nx
    cmp byte [esp + 21], '.'
    jne .cnt_chk
    cmp byte [esp + 22], 0
    je .cnt_nx
.cnt_chk:
    test dword [esp], 0x10
    jnz .cnt_dir
    cmp dword [g_tree_files], 0
    je .cnt_nx
    ; Print file line under /F
    mov ecx, dword [esp + 1568]
    call tree_indent
    mov esi, addr(szTreeBlank)
    call u32_write_str
    lea esi, [esp + 20]
    call u32_write_str
    call u32_write_crlf
    jmp .cnt_nx
.cnt_dir:
    inc dword [esp + 1572]
.cnt_nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .cnt_lp
    push ebp
    call dword [__imp__FindClose@4]

    cmp dword [esp + 1572], 0
    je .done

    ; Second pass: render each subdirectory and recurse
    mov dword [esp + 1576], 0
    lea esi, [esp + 1056]
    mov edi, addr(szTreeStar)
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .done
    mov ebp, eax

.dir_lp:
    test dword [esp], 0x10
    jz .dir_nx
    cmp byte [esp + 20], '.'
    jne .dir_ok
    cmp byte [esp + 21], 0
    je .dir_nx
    cmp byte [esp + 21], '.'
    jne .dir_ok
    cmp byte [esp + 22], 0
    je .dir_nx
.dir_ok:
    inc dword [esp + 1576]
    mov ecx, dword [esp + 1568]
    call tree_indent

    mov eax, dword [esp + 1576]
    cmp eax, dword [esp + 1572]
    je .is_last

    mov ecx, dword [esp + 1568]
    mov byte [g_tree_last + ecx], 0
    cmp dword [g_tree_ascii], 0
    jne .mid_asc
    mov esi, addr(szTreeUniMid)
    jmp .pr_branch
.mid_asc:
    mov esi, addr(szTreeAscMid)
    jmp .pr_branch

.is_last:
    mov ecx, dword [esp + 1568]
    mov byte [g_tree_last + ecx], 1
    cmp dword [g_tree_ascii], 0
    jne .end_asc
    mov esi, addr(szTreeUniEnd)
    jmp .pr_branch
.end_asc:
    mov esi, addr(szTreeAscEnd)

.pr_branch:
    call u32_write_str
    lea esi, [esp + 20]
    call u32_write_str
    call u32_write_crlf

    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov ecx, dword [esp + 1568]
    inc ecx
    call tree_walk

.dir_nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .dir_lp
    push ebp
    call dword [__imp__FindClose@4]

.done:
    add esp, 1600
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid root directory
;; @clobbers eax, ecx, edx
;; @desc Parses TREE switches (/F, /A, /?) and renders directory hierarchy.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_tree_files], 0
    mov eax, dword [g_u32_is_win9x]
    mov dword [g_tree_ascii], eax
    mov byte [g_u32_arg_b], 0

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
    cmp al, 'F'
    je .sw_f
    cmp al, 'A'
    je .sw_a
    jmp .arg_lp
.sw_f:
    mov dword [g_tree_files], 1
    jmp .arg_lp
.sw_a:
    mov dword [g_tree_ascii], 1
    jmp .arg_lp
.pos_arg:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szTreeHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.run:
    cmp byte [g_u32_arg_b], 0
    jne .chk_dir
    mov edi, addr(g_u32_arg_b)
    mov ecx, 1000
    call u32_get_cwd

.chk_dir:
    mov esi, addr(g_u32_arg_b)
    call u32_get_attr
    cmp eax, -1
    je .bad_dir
    test eax, 0x10
    jz .bad_dir

    mov esi, addr(szTreeHdr)
    call u32_write_str
    call u32_write_crlf
    mov esi, addr(g_u32_arg_b)
    call u32_write_str
    call u32_write_crlf

    mov esi, addr(g_u32_arg_b)
    xor ecx, ecx
    call tree_walk

    xor eax, eax
    call u32_exit
    jmp .done

.bad_dir:
    mov esi, addr(szTreeInv)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szTreeStar, "*", 0
@data db szTreeHdr, "Folder PATH listing", 0
@data db szTreeInv, "Invalid path", 0
@data db szTreeAscMid, "+---", 0
@data db szTreeAscEnd, 0x5C, "---", 0
@data db szTreeAscBar, "|   ", 0
@data db szTreeUniMid, 0xE2, 0x94, 0x9C, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTreeUniEnd, 0xE2, 0x94, 0x94, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTreeUniBar, 0xE2, 0x94, 0x82, "   ", 0
@data db szTreeBlank, "    ", 0
@data db szTreeHelp, "Graphically displays the folder structure of a drive or path ([path] [/F] [/A]).", 0

@include "u_data32.inc"

@res dword g_tree_files, 1
@res dword g_tree_ascii, 1
@res byte g_tree_last, 32
