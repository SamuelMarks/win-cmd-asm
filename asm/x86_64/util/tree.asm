;; @file tree.asm
;; @isa x64
;; @desc Standalone x86-64 TREE.EXE directory visualizer ([drive:][path] [/F] [/A]).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc tree64_indent
;; @abi win64_ms
;; @in ECX = nesting depth (0..15)
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints ancestor vertical bar connectors up to depth ECX.
@proc_frame tree64_indent, 40, rbx, rdi
    mov edi, ecx
    xor ebx, ebx
.lp:
    cmp ebx, edi
    jae .dn
    lea rax, [rel g_tree64_last]
    cmp byte [rax + rbx], 0
    jne .blank
    cmp dword [rel g_tree64_asc], 0
    jne .basc
    lea rcx, [rel szTr64UniBar]
    call u64_write_str
    jmp .nx
.basc:
    lea rcx, [rel szTr64AscBar]
    call u64_write_str
    jmp .nx
.blank:
    lea rcx, [rel szTr64Blank]
    call u64_write_str
.nx:
    inc ebx
    jmp .lp
.dn:
    @epilog_ret
@endproc

;; @proc tree64_walk
;; @abi win64_ms
;; @in RCX = dir path UTF-8, EDX = depth
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Recursively prints files (/F) and subdirectories with Unicode or ASCII connectors.
@proc_frame tree64_walk, 1664, rbx, rsi, rdi, r12, r13
    cmp edx, 15
    jae .dn
    mov r12d, edx ; depth
    mov rdx, rcx
    lea rcx, [rsp + 1120]
    call u64_strcpy

    xor r13d, r13d ; subdir count
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rel szTr64Star]
    call u64_path_join
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .dn
    mov rbx, rax

.cnt_lp:
    cmp byte [rsp + 88], '.'
    jne .cnt_c
    cmp byte [rsp + 89], 0
    je .cnt_nx
    cmp byte [rsp + 89], '.'
    jne .cnt_c
    cmp byte [rsp + 90], 0
    je .cnt_nx
.cnt_c:
    test dword [rsp + 64], 0x10
    jnz .is_d
    cmp dword [rel g_tree64_files], 0
    je .cnt_nx
    mov ecx, r12d
    call tree64_indent
    lea rcx, [rel szTr64Blank]
    call u64_write_str
    lea rcx, [rsp + 88]
    call u64_write_str
    call u64_write_crlf
    jmp .cnt_nx
.is_d:
    inc r13d
.cnt_nx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .cnt_lp
    mov rcx, rbx
    call qword [rel __imp_FindClose]

    test r13d, r13d
    jz .dn

    xor esi, esi ; seen
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rel szTr64Star]
    call u64_path_join
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .dn
    mov rbx, rax

.dlp:
    test dword [rsp + 64], 0x10
    jz .dnx
    cmp byte [rsp + 88], '.'
    jne .dok
    cmp byte [rsp + 89], 0
    je .dnx
    cmp byte [rsp + 89], '.'
    jne .dok
    cmp byte [rsp + 90], 0
    je .dnx
.dok:
    inc esi
    mov ecx, r12d
    call tree64_indent

    lea rdi, [rel g_tree64_last]
    cmp esi, r13d
    je .last
    mov byte [rdi + r12], 0
    cmp dword [rel g_tree64_asc], 0
    jne .masc
    lea rcx, [rel szTr64UniMid]
    jmp .prb
.masc:
    lea rcx, [rel szTr64AscMid]
    jmp .prb
.last:
    mov byte [rdi + r12], 1
    cmp dword [rel g_tree64_asc], 0
    jne .easc
    lea rcx, [rel szTr64UniEnd]
    jmp .prb
.easc:
    lea rcx, [rel szTr64AscEnd]
.prb:
    call u64_write_str
    lea rcx, [rsp + 88]
    call u64_write_str
    call u64_write_crlf

    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 608]
    lea edx, [r12d + 1]
    call tree64_walk
.dnx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .dlp
    mov rcx, rbx
    call qword [rel __imp_FindClose]
.dn:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid path
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 TREE.EXE.
@proc_frame mainCRTStartup, 48, rsi
    call u64_init
    mov rsi, rax
    mov dword [rel g_tree64_files], 0
    mov dword [rel g_tree64_asc], 0
    mov byte [rel g_u64_arg_b], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pos
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'F'
    je .sf
    cmp al, 'A'
    je .sa
    jmp .arg_lp
.sf:
    mov dword [rel g_tree64_files], 1
    jmp .arg_lp
.sa:
    mov dword [rel g_tree64_asc], 1
    jmp .arg_lp
.pos:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp

.help:
    lea rcx, [rel szTr64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_u64_arg_b], 0
    jne .chk
    lea rcx, [rel g_u64_arg_b]
    mov edx, 1000
    call u64_get_cwd
.chk:
    lea rcx, [rel g_u64_arg_b]
    call u64_get_attr
    cmp eax, -1
    je .bad
    test eax, 0x10
    jz .bad

    lea rcx, [rel szTr64Hdr]
    call u64_write_str
    call u64_write_crlf
    lea rcx, [rel g_u64_arg_b]
    call u64_write_str
    call u64_write_crlf

    lea rcx, [rel g_u64_arg_b]
    xor edx, edx
    call tree64_walk
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szTr64Inv]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szTr64Star, "*", 0
@data db szTr64Hdr, "Folder PATH listing", 0
@data db szTr64Inv, "Invalid path", 0
@data db szTr64AscMid, "+---", 0
@data db szTr64AscEnd, 0x5C, "---", 0
@data db szTr64AscBar, "|   ", 0
@data db szTr64UniMid, 0xE2, 0x94, 0x9C, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTr64UniEnd, 0xE2, 0x94, 0x94, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTr64UniBar, 0xE2, 0x94, 0x82, "   ", 0
@data db szTr64Blank, "    ", 0
@data db szTr64Help, "Graphically displays the folder structure of a drive or path ([path] [/F] [/A]).", 0

@include "u_data64.inc"

@res dword g_tree64_files, 1
@res dword g_tree64_asc, 1
@res byte g_tree64_last, 32
