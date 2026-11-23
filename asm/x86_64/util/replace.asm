;; @file replace.asm
;; @isa x64
;; @desc Standalone x86-64 REPLACE.EXE file updater (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc rep64_get_mtime
;; @abi win64_ms
;; @in RCX = file path UTF-8, RDX = 8B FILETIME output
;; @out EAX = 1 on success, 0 on failure
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries last-write FILETIME via OpenRead + GetFileTime.
@proc_frame rep64_get_mtime, 40, rbx, rdi
    mov rdi, rdx
    call u64_open_read
    cmp rax, -1
    je .fail
    mov rbx, rax
    mov rcx, rbx
    xor edx, edx
    xor r8d, r8d
    mov r9, rdi
    call qword [rel __imp_GetFileTime]
    mov edi, eax
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    mov eax, edi
    @epilog_ret
.fail:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc rep64_apply_dir
;; @abi win64_ms
;; @in RCX = target directory UTF-8, EDX = depth
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Replaces existing target files (or adds under /A) and recurses under /S.
@proc_frame rep64_apply_dir, 1720, rbx, rsi, rdi, r12
    cmp edx, 16
    jae .dn
    mov r12d, edx
    mov rdx, rcx
    lea rcx, [rsp + 608]
    call u64_strcpy

    lea rcx, [rsp + 1120]
    lea rdx, [rsp + 608]
    lea r8, [rel g_rep64_leaf]
    call u64_path_join

    lea rcx, [rsp + 1120]
    call u64_get_attr
    mov ebx, eax

    cmp dword [rel g_rep64_add], 0
    jne .add_m

    cmp ebx, -1
    je .sub
    test ebx, 0x10
    jnz .sub

    cmp dword [rel g_rep64_older], 0
    je .cro
    lea rcx, [rsp + 1120]
    lea rdx, [rsp + 1664]
    call rep64_get_mtime
    test eax, eax
    jz .cro
    lea rcx, [rel g_rep64_sft]
    lea rdx, [rsp + 1664]
    call qword [rel __imp_CompareFileTime]
    cmp eax, 0
    jle .sub

.cro:
    test ebx, 1
    jz .do_r
    cmp dword [rel g_rep64_ro], 0
    je .sub
    lea rcx, [rsp + 1120]
    mov edx, 0x80
    call u64_set_attr
.do_r:
    lea rcx, [rel g_rep64_src]
    lea rdx, [rsp + 1120]
    xor r8d, r8d
    call u64_copy_file
    test eax, eax
    jz .sub
    inc dword [rel g_rep64_cnt]
    jmp .sub

.add_m:
    cmp ebx, -1
    jne .sub
    lea rcx, [rel g_rep64_src]
    lea rdx, [rsp + 1120]
    mov r8d, 1
    call u64_copy_file
    test eax, eax
    jz .sub
    inc dword [rel g_rep64_cnt]

.sub:
    cmp dword [rel g_rep64_sub], 0
    je .dn
    lea rcx, [rsp + 1120]
    lea rdx, [rsp + 608]
    lea r8, [rel szRep64Star]
    call u64_path_join
    lea rcx, [rsp + 1120]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .dn
    mov rbx, rax
.slp:
    test dword [rsp + 64], 0x10
    jz .snx
    cmp byte [rsp + 88], '.'
    jne .go
    cmp byte [rsp + 89], 0
    je .snx
    cmp byte [rsp + 89], '.'
    jne .go
    cmp byte [rsp + 90], 0
    je .snx
.go:
    lea rcx, [rsp + 1120]
    lea rdx, [rsp + 608]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 1120]
    lea edx, [r12d + 1]
    call rep64_apply_dir
.snx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .slp
    mov rcx, rbx
    call qword [rel __imp_FindClose]
.dn:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits non-zero on error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 REPLACE.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_rep64_add], 0
    mov dword [rel g_rep64_ro], 0
    mov dword [rel g_rep64_sub], 0
    mov dword [rel g_rep64_older], 0
    mov dword [rel g_rep64_cnt], 0
    mov byte [rel g_rep64_src], 0
    mov byte [rel g_rep64_dst], 0

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
    cmp al, 'A'
    je .sa
    cmp al, 'R'
    je .sr
    cmp al, 'S'
    je .ss
    cmp al, 'U'
    je .su
    jmp .arg_lp
.sa:
    mov dword [rel g_rep64_add], 1
    jmp .arg_lp
.sr:
    mov dword [rel g_rep64_ro], 1
    jmp .arg_lp
.ss:
    mov dword [rel g_rep64_sub], 1
    jmp .arg_lp
.su:
    mov dword [rel g_rep64_older], 1
    jmp .arg_lp
.pos:
    cmp byte [rel g_rep64_src], 0
    jne .pos2
    mov rcx, rsi
    lea rdx, [rel g_rep64_src]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.pos2:
    mov rcx, rsi
    lea rdx, [rel g_rep64_dst]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp

.help:
    lea rcx, [rel szRep64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_rep64_src], 0
    je .bad
    lea rcx, [rel g_rep64_src]
    call u64_get_attr
    cmp eax, -1
    je .nosrc

    lea rcx, [rel g_rep64_src]
    lea rdx, [rel g_rep64_sft]
    call rep64_get_mtime

    cmp byte [rel g_rep64_dst], 0
    jne .leaf
    lea rcx, [rel g_rep64_dst]
    mov edx, 1000
    call u64_get_cwd
.leaf:
    lea rcx, [rel g_rep64_src]
    call u64_strlen
    mov rdx, -1
    xor ecx, ecx
    lea rdi, [rel g_rep64_src]
.sl:
    cmp rcx, rax
    jae .sld
    mov bl, byte [rdi + rcx]
    cmp bl, 0x5C
    je .mk
    cmp bl, '/'
    je .mk
    cmp bl, ':'
    jne .nx
.mk:
    mov rdx, rcx
.nx:
    inc rcx
    jmp .sl
.sld:
    lea rdx, [rdi + rdx + 1]
    lea rcx, [rel g_rep64_leaf]
    call u64_strcpy

    lea rcx, [rel g_rep64_dst]
    xor edx, edx
    call rep64_apply_dir

    mov ecx, dword [rel g_rep64_cnt]
    call u64_print_u64
    cmp dword [rel g_rep64_add], 0
    jne .added
    lea rcx, [rel szRep64Done]
    jmp .pm
.added:
    lea rcx, [rel szRep64Added]
.pm:
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.nosrc:
    lea rcx, [rel szRep64NoSrc]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 2
    call u64_exit
    @epilog_ret
.bad:
    lea rcx, [rel szRep64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 3
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szRep64Star, "*", 0
@data db szRep64Done, " file(s) replaced", 0
@data db szRep64Added, " file(s) added", 0
@data db szRep64NoSrc, "Source path required", 0
@data db szRep64Syn, "Invalid parameter", 0
@data db szRep64Help, "Replaces files in destination directories (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).", 0

@include "u_data64.inc"

@res byte g_rep64_sft, 8
@res dword g_rep64_add, 1
@res dword g_rep64_ro, 1
@res dword g_rep64_sub, 1
@res dword g_rep64_older, 1
@res dword g_rep64_cnt, 1
@res byte g_rep64_src, 520
@res byte g_rep64_dst, 520
@res byte g_rep64_leaf, 520
