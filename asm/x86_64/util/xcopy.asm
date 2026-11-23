;; @file xcopy.asm
;; @isa x64
;; @desc Standalone x86-64 XCOPY.EXE file/tree copier (/A /M /D /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc xc64_is_excluded
;; @abi win64_ms
;; @in RCX = candidate path UTF-8
;; @out EAX = 1 if path matches any /EXCLUDE substring, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Checks candidate path against newline-separated substrings in g_xc64_excl.
@proc_frame xc64_is_excluded, 48, rbx, rsi, rdi, r12, r13
    cmp dword [rel g_xc64_has_excl], 0
    je .no
    mov rsi, rcx
    call u64_strlen
    mov r12, rax
    lea rdi, [rel g_xc64_excl]

.ln_lp:
    cmp byte [rdi], 0
    je .no
    mov rbx, rdi
.eol:
    mov al, byte [rbx]
    test al, al
    jz .got
    cmp al, 13
    je .got
    cmp al, 10
    je .got
    inc rbx
    jmp .eol
.got:
    mov r13, rbx
    sub r13, rdi
    jz .adv
    cmp r12, r13
    jb .adv
    mov r8, r12
    sub r8, r13
    inc r8
    mov r9, rsi
.pos:
    xor r10d, r10d
.ch:
    cmp r10, r13
    jae .yes
    mov al, byte [r9 + r10]
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    mov dl, byte [rdi + r10]
    cmp dl, 'a'
    jb .u2
    cmp dl, 'z'
    ja .u2
    sub dl, 32
.u2:
    cmp al, dl
    jne .nxp
    inc r10
    jmp .ch
.nxp:
    inc r9
    dec r8
    jnz .pos

.adv:
    cmp byte [rbx], 13
    jne .clf
    inc rbx
.clf:
    cmp byte [rbx], 10
    jne .nxl
    inc rbx
.nxl:
    mov rdi, rbx
    jmp .ln_lp
.yes:
    mov eax, 1
    @epilog_ret
.no:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc xc64_copy_one
;; @abi win64_ms
;; @in RCX = src UTF-8, RDX = dst UTF-8, R8D = src attributes
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Copies a single file honoring XCOPY filter and attribute switches.
@proc_frame xc64_copy_one, 48, rbx, rsi, rdi
    cmp dword [rel g_xc64_tree], 0
    jne .dn
    mov rsi, rcx
    mov rdi, rdx
    mov ebx, r8d

    cmp dword [rel g_xc64_hid], 0
    jne .ca
    test ebx, 0x06
    jnz .dn
.ca:
    cmp dword [rel g_xc64_arch], 0
    je .ce
    test ebx, 0x20
    jz .dn
.ce:
    mov rcx, rsi
    call xc64_is_excluded
    test eax, eax
    jnz .dn

    cmp dword [rel g_xc64_upd], 0
    je .cl
    mov rcx, rdi
    call u64_get_attr
    cmp eax, -1
    je .dn

.cl:
    cmp dword [rel g_xc64_quiet], 0
    jne .cd
    mov rcx, rsi
    call u64_write_str
    cmp dword [rel g_xc64_full], 0
    je .nl
    lea rcx, [rel szXc64Arrow]
    call u64_write_str
    mov rcx, rdi
    call u64_write_str
.nl:
    call u64_write_crlf

.cd:
    cmp dword [rel g_xc64_list], 0
    je .cp
    inc dword [rel g_xc64_copied]
    @epilog_ret

.cp:
    cmp dword [rel g_xc64_ro], 0
    je .docp
    mov rcx, rdi
    call u64_get_attr
    cmp eax, -1
    je .docp
    test eax, 1
    jz .docp
    mov rcx, rdi
    mov edx, 0x80
    call u64_set_attr

.docp:
    mov rcx, rsi
    mov rdx, rdi
    xor r8d, r8d
    call u64_copy_file
    test eax, eax
    jz .dn
    inc dword [rel g_xc64_copied]

    cmp dword [rel g_xc64_keep], 0
    jne .cm
    mov rcx, rdi
    mov edx, 0x20
    call u64_set_attr
.cm:
    cmp dword [rel g_xc64_clr_a], 0
    je .dn
    and ebx, 0xFFFFFFDF
    jnz .ms
    mov ebx, 0x80
.ms:
    mov rcx, rsi
    mov edx, ebx
    call u64_set_attr
.dn:
    @epilog_ret
@endproc

;; @proc xc64_copy_dir
;; @abi win64_ms
;; @in RCX = src dir, RDX = dst dir, R8D = depth
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Copies directory contents and recurses under /S or /E.
@proc_frame xc64_copy_dir, 2720, rbx, rsi, rdi, r12, r13
    cmp r8d, 16
    jae .dn
    mov r12d, r8d ; depth
    xor r13d, r13d ; created_dst
    mov rsi, rdx ; save dst
    mov rdx, rcx
    lea rcx, [rsp + 608] ; src dir
    call u64_strcpy
    lea rcx, [rsp + 1120] ; dst dir
    mov rdx, rsi
    call u64_strcpy

    cmp dword [rel g_xc64_empty], 0
    jne .mk
    cmp dword [rel g_xc64_tree], 0
    je .en
.mk:
    cmp dword [rel g_xc64_list], 0
    jne .en
    lea rcx, [rsp + 1120]
    call u64_mkdir
    mov r13d, 1

.en:
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 608]
    lea r8, [rel szXc64Star]
    call u64_path_join
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .dn
    mov rbx, rax

.lp:
    cmp byte [rsp + 88], '.'
    jne .nd
    cmp byte [rsp + 89], 0
    je .nx
    cmp byte [rsp + 89], '.'
    jne .nd
    cmp byte [rsp + 90], 0
    je .nx
.nd:
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 608]
    lea r8, [rsp + 88]
    call u64_path_join

    lea rcx, [rsp + 2144]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join

    test dword [rsp + 64], 0x10
    jnz .sub

    test r13d, r13d
    jnz .df
    cmp dword [rel g_xc64_list], 0
    jne .df
    lea rcx, [rsp + 1120]
    call u64_mkdir
    mov r13d, 1
.df:
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 2144]
    mov r8d, dword [rsp + 64]
    call xc64_copy_one
    jmp .nx

.sub:
    cmp dword [rel g_xc64_sub], 0
    je .nx
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 2144]
    lea r8d, [r12d + 1]
    call xc64_copy_dir
.nx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .lp
    mov rcx, rbx
    call qword [rel __imp_FindClose]
.dn:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 4=error)
;; @err Exits 4 on syntax/missing error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 XCOPY.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_xc64_arch], 0
    mov dword [rel g_xc64_clr_a], 0
    mov dword [rel g_xc64_sub], 0
    mov dword [rel g_xc64_empty], 0
    mov dword [rel g_xc64_quiet], 0
    mov dword [rel g_xc64_full], 0
    mov dword [rel g_xc64_list], 0
    mov dword [rel g_xc64_hid], 0
    mov dword [rel g_xc64_ro], 0
    mov dword [rel g_xc64_tree], 0
    mov dword [rel g_xc64_upd], 0
    mov dword [rel g_xc64_keep], 0
    mov dword [rel g_xc64_has_excl], 0
    mov dword [rel g_xc64_copied], 0
    mov byte [rel g_u64_arg_b], 0
    mov byte [rel g_u64_arg_c], 0

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
    cmp al, 'M'
    je .sm
    cmp al, 'S'
    je .ss
    cmp al, 'E'
    je .se
    cmp al, 'Q'
    je .sq
    cmp al, 'F'
    je .sf
    cmp al, 'L'
    je .sl
    cmp al, 'H'
    je .sh
    cmp al, 'R'
    je .sr
    cmp al, 'T'
    je .st
    cmp al, 'U'
    je .su
    cmp al, 'K'
    je .sk
    jmp .arg_lp
.sa:
    mov dword [rel g_xc64_arch], 1
    jmp .arg_lp
.sm:
    mov dword [rel g_xc64_arch], 1
    mov dword [rel g_xc64_clr_a], 1
    jmp .arg_lp
.ss:
    mov dword [rel g_xc64_sub], 1
    jmp .arg_lp
.se:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'X'
    je .sex
    mov dword [rel g_xc64_sub], 1
    mov dword [rel g_xc64_empty], 1
    jmp .arg_lp
.sex:
    lea rdi, [rel g_u64_arg_a]
    xor ecx, ecx
.fcl:
    mov al, byte [rdi + rcx]
    test al, al
    jz .arg_lp
    cmp al, ':'
    je .gcl
    inc ecx
    jmp .fcl
.gcl:
    lea rcx, [rdi + rcx + 1]
    lea rdx, [rel g_xc64_excl]
    mov r8d, 4000
    mov r9d, 1
    call u64_load_stream
    jc .arg_lp
    mov dword [rel g_xc64_has_excl], 1
    jmp .arg_lp
.sq:
    mov dword [rel g_xc64_quiet], 1
    jmp .arg_lp
.sf:
    mov dword [rel g_xc64_full], 1
    jmp .arg_lp
.sl:
    mov dword [rel g_xc64_list], 1
    jmp .arg_lp
.sh:
    mov dword [rel g_xc64_hid], 1
    jmp .arg_lp
.sr:
    mov dword [rel g_xc64_ro], 1
    jmp .arg_lp
.st:
    mov dword [rel g_xc64_tree], 1
    mov dword [rel g_xc64_sub], 1
    jmp .arg_lp
.su:
    mov dword [rel g_xc64_upd], 1
    jmp .arg_lp
.sk:
    mov dword [rel g_xc64_keep], 1
    jmp .arg_lp
.pos:
    cmp byte [rel g_u64_arg_b], 0
    jne .pos2
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.pos2:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_c]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp

.help:
    lea rcx, [rel szXc64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_u64_arg_b], 0
    je .bad
    cmp byte [rel g_u64_arg_c], 0
    jne .gd
    lea rcx, [rel g_u64_arg_c]
    mov edx, 1000
    call u64_get_cwd
.gd:
    lea rcx, [rel g_u64_arg_b]
    call u64_get_attr
    cmp eax, -1
    je .nf
    test eax, 0x10
    jz .sfile
    lea rcx, [rel g_u64_arg_b]
    lea rdx, [rel g_u64_arg_c]
    xor r8d, r8d
    call xc64_copy_dir
    jmp .sum

.sfile:
    mov ebx, eax
    lea rcx, [rel g_u64_arg_c]
    call u64_get_attr
    cmp eax, -1
    je .ddst
    test eax, 0x10
    jz .ddst

    lea rcx, [rel g_u64_arg_b]
    call u64_strlen
    mov rdx, -1
    xor ecx, ecx
    lea rdi, [rel g_u64_arg_b]
.sll:
    cmp rcx, rax
    jae .sld
    mov r8b, byte [rdi + rcx]
    cmp r8b, 0x5C
    je .slm
    cmp r8b, '/'
    jne .sln
.slm:
    mov rdx, rcx
.sln:
    inc rcx
    jmp .sll
.sld:
    lea r8, [rdi + rdx + 1]
    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel g_u64_arg_c]
    call u64_path_join
    lea rcx, [rel g_u64_arg_b]
    lea rdx, [rel g_u64_path_tmp]
    mov r8d, ebx
    call xc64_copy_one
    jmp .sum

.ddst:
    lea rcx, [rel g_u64_arg_b]
    lea rdx, [rel g_u64_arg_c]
    mov r8d, ebx
    call xc64_copy_one

.sum:
    mov ecx, dword [rel g_xc64_copied]
    call u64_print_u64
    lea rcx, [rel szXc64Sum]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.nf:
    lea rcx, [rel szXc64Nf]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 4
    call u64_exit
    @epilog_ret
.bad:
    lea rcx, [rel szXc64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 4
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szXc64Star, "*", 0
@data db szXc64Arrow, " -> ", 0
@data db szXc64Sum, " File(s) copied", 0
@data db szXc64Nf, "File not found", 0
@data db szXc64Syn, "Invalid number of parameters", 0
@data db szXc64Help, "Copies files and directory trees (/A /M /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).", 0

@include "u_data64.inc"

@res dword g_xc64_arch, 1
@res dword g_xc64_clr_a, 1
@res dword g_xc64_sub, 1
@res dword g_xc64_empty, 1
@res dword g_xc64_quiet, 1
@res dword g_xc64_full, 1
@res dword g_xc64_list, 1
@res dword g_xc64_hid, 1
@res dword g_xc64_ro, 1
@res dword g_xc64_tree, 1
@res dword g_xc64_upd, 1
@res dword g_xc64_keep, 1
@res dword g_xc64_has_excl, 1
@res dword g_xc64_copied, 1
@res byte g_xc64_excl, 4096
