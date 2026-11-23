;; @file findstr.asm
;; @isa x64
;; @desc Standalone x86-64 FINDSTR.EXE utility with 64-bit Thompson NFA bitset regex engine (/B /E /L /R /S /I /X /V /N /M /O /P /C).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc fs64_compile_nfa
;; @abi win64_ms
;; @in RCX = pattern string, EDX = literal_only (1=/L, 0=/R)
;; @out Populates NFA tables (up to 60 states using native 64-bit state bitset!)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Compiles pattern into up to 60 Thompson NFA state slots.
@proc_frame fs64_compile_nfa, 48, rbx, rsi, rdi
    mov rsi, rcx
    mov dword [rel g_nfa64_bol], 0
    mov dword [rel g_nfa64_eol], 0
    mov qword [rel g_nfa64_star], 0
    xor r8d, r8d ; state index

    test edx, edx
    jnz .comp_lp
    cmp byte [rsi], '^'
    jne .comp_lp
    mov dword [rel g_nfa64_bol], 1
    inc rsi

.comp_lp:
    movzx eax, byte [rsi]
    test al, al
    jz .comp_dn
    cmp r8d, 60
    jae .comp_dn

    test edx, edx
    jnz .lit_atom

    cmp al, '$'
    jne .not_eol
    cmp byte [rsi + 1], 0
    jne .not_eol
    mov dword [rel g_nfa64_eol], 1
    inc rsi
    jmp .comp_dn

.not_eol:
    cmp al, '.'
    jne .not_dot
    lea rdi, [rel g_nfa64_kind]
    mov byte [rdi + r8], 2 ; ANY
    inc rsi
    jmp .chk_star

.not_dot:
    cmp al, 0x5C
    jne .not_esc
    cmp byte [rsi + 1], 0
    je .lit_atom
    inc rsi
    mov al, byte [rsi]
    lea rdi, [rel g_nfa64_kind]
    cmp al, 'd'
    je .esc_d
    cmp al, 'w'
    je .esc_w
    cmp al, 's'
    je .esc_s
    jmp .lit_atom
.esc_d:
    mov byte [rdi + r8], 4
    inc rsi
    jmp .chk_star
.esc_w:
    mov byte [rdi + r8], 5
    inc rsi
    jmp .chk_star
.esc_s:
    mov byte [rdi + r8], 6
    inc rsi
    jmp .chk_star

.not_esc:
    cmp al, '['
    jne .lit_atom
    inc rsi
    mov rdi, r8
    shl rdi, 5
    lea r9, [rel g_nfa64_bmaps]
    add rdi, r9
    xor ebx, ebx
.zr:
    cmp ebx, 32
    jae .bm_clr
    mov byte [rdi + rbx], 0
    inc ebx
    jmp .zr
.bm_clr:
    xor ebx, ebx
    cmp byte [rsi], '^'
    jne .cls_lp
    mov ebx, 1
    inc rsi
.cls_lp:
    movzx eax, byte [rsi]
    test al, al
    jz .cls_end
    cmp al, ']'
    jne .cls_item
    inc rsi
    jmp .cls_end
.cls_item:
    inc rsi
    cmp byte [rsi], '-'
    jne .cls_one
    cmp byte [rsi + 1], 0
    je .cls_one
    cmp byte [rsi + 1], ']'
    je .cls_one
    movzx eax, byte [rsi - 1]
    movzx r9d, byte [rsi + 1]
    add rsi, 2
.rng:
    cmp eax, r9d
    ja .cls_lp
    mov ecx, eax
    and ecx, 7
    mov r10d, eax
    shr r10d, 3
    mov r11b, 1
    shl r11b, cl
    or byte [rdi + r10], r11b
    inc eax
    jmp .rng
.cls_one:
    mov ecx, eax
    and ecx, 7
    shr eax, 3
    mov r11b, 1
    shl r11b, cl
    or byte [rdi + rax], r11b
    jmp .cls_lp
.cls_end:
    test ebx, ebx
    jz .cls_st
    xor ebx, ebx
.inv:
    cmp ebx, 32
    jae .cls_st
    not byte [rdi + rbx]
    inc ebx
    jmp .inv
.cls_st:
    lea rdi, [rel g_nfa64_kind]
    mov byte [rdi + r8], 3
    jmp .chk_star

.lit_atom:
    cmp dword [rel g_fs64_ci], 0
    je .st_l
    call u64_chr_upper
.st_l:
    lea rdi, [rel g_nfa64_kind]
    mov byte [rdi + r8], 1
    lea rdi, [rel g_nfa64_ch]
    mov byte [rdi + r8], al
    inc rsi

.chk_star:
    test edx, edx
    jnz .no_st
    cmp byte [rsi], '*'
    jne .no_st
    inc rsi
    mov eax, 1
    mov ecx, r8d
    shl rax, cl
    or qword [rel g_nfa64_star], rax
.no_st:
    inc r8d
    jmp .comp_lp

.comp_dn:
    mov dword [rel g_nfa64_len], r8d
    mov eax, 1
    mov ecx, r8d
    shl rax, cl
    mov qword [rel g_nfa64_acc], rax
    @epilog_ret
@endproc

;; @proc fs64_eps
;; @abi win64_ms
;; @in RCX = 64-bit NFA state bitset
;; @out RAX = epsilon-closed 64-bit state bitset
;; @err none
;; @clobbers rax, rcx, rdx, r8
;; @desc Propagates Kleene-star epsilon transitions across 64-bit bitset.
@proc fs64_eps
    mov rax, rcx
    mov r8, qword [rel g_nfa64_star]
    test r8, r8
    jz .dn
    mov edx, 60
.lp:
    mov rcx, rax
    and rcx, r8
    shl rcx, 1
    or rax, rcx
    dec edx
    jnz .lp
.dn:
    ret
@endproc

;; @proc fs64_atom_match
;; @abi win64_ms
;; @in ECX = state index, DL = input byte
;; @out EAX = 1 if matches, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9
;; @desc Evaluates one NFA state atom against DL.
@proc fs64_atom_match
    mov al, dl
    cmp dword [rel g_fs64_ci], 0
    je .got_c
    cmp al, 'a'
    jb .got_c
    cmp al, 'z'
    ja .got_c
    sub al, 32
.got_c:
    mov r8d, ecx
    lea r9, [rel g_nfa64_kind]
    mov dl, byte [r9 + r8]
    cmp dl, 1
    je .lit
    cmp dl, 2
    je .yes
    cmp dl, 3
    je .cls
    cmp dl, 4
    je .dig
    cmp dl, 5
    je .wrd
    cmp dl, 6
    je .spc
    jmp .no
.lit:
    lea r9, [rel g_nfa64_ch]
    cmp al, byte [r9 + r8]
    je .yes
    jmp .no
.cls:
    shl r8, 5
    lea r9, [rel g_nfa64_bmaps]
    add r9, r8
    movzx edx, al
    mov ecx, edx
    and ecx, 7
    shr edx, 3
    mov al, byte [r9 + rdx]
    shr al, cl
    and eax, 1
    ret
.dig:
    cmp al, '0'
    jb .no
    cmp al, '9'
    jbe .yes
    jmp .no
.wrd:
    cmp al, '_'
    je .yes
    cmp al, '0'
    jb .no
    cmp al, '9'
    jbe .yes
    and al, 0xDF
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    jbe .yes
    jmp .no
.spc:
    cmp al, 0x20
    je .yes
    cmp al, 0x09
    je .yes
.no:
    xor eax, eax
    ret
.yes:
    mov eax, 1
    ret
@endproc

;; @proc fs64_eval_nfa
;; @abi win64_ms
;; @in RCX = line start, RDX = line length
;; @out EAX = 1 if line matches NFA, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc O(N) 64-bit bitset Thompson NFA evaluator.
@proc_frame fs64_eval_nfa, 48, rbx, rsi, rdi, r12, r13
    mov rsi, rcx
    mov r12, rdx ; len

    mov r13d, dword [rel g_fs64_beg]
    or r13d, dword [rel g_nfa64_bol]
    or r13d, dword [rel g_fs64_exact]

    mov ecx, 1
    call fs64_eps
    mov rdi, rax ; active bitset
    xor ebx, ebx ; pos

.step_lp:
    mov eax, dword [rel g_fs64_end]
    or eax, dword [rel g_nfa64_eol]
    or eax, dword [rel g_fs64_exact]
    jnz .chk_end
    mov rax, rdi
    and rax, qword [rel g_nfa64_acc]
    jnz .yes

.chk_end:
    cmp rbx, r12
    jae .at_eol

    xor r10d, r10d ; next_states (64-bit in r10)
    xor r11d, r11d ; state i
.st_lp:
    cmp r11d, dword [rel g_nfa64_len]
    jae .st_dn
    mov eax, 1
    mov ecx, r11d
    shl rax, cl
    test rdi, rax
    jz .st_nx
    mov ecx, r11d
    mov dl, byte [rsi + rbx]
    call fs64_atom_match
    test eax, eax
    jz .st_nx
    mov eax, 1
    mov ecx, r11d
    shl rax, cl
    test rax, qword [rel g_nfa64_star]
    jz .adv_s
    or r10, rax
.adv_s:
    shl rax, 1
    or r10, rax
.st_nx:
    inc r11d
    jmp .st_lp
.st_dn:
    mov rcx, r10
    test r13d, r13d
    jnz .no_seed
    or rcx, 1
.no_seed:
    call fs64_eps
    mov rdi, rax
    inc rbx
    jmp .step_lp

.at_eol:
    mov rax, rdi
    and rax, qword [rel g_nfa64_acc]
    jnz .yes
    xor eax, eax
    @epilog_ret
.yes:
    mov eax, 1
    @epilog_ret
@endproc

;; @proc fs64_scan_file
;; @abi win64_ms
;; @in RCX = file path (empty=stdin), EDX = show_prefix (1 or 0)
;; @out EAX = 1 if matched, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Scans file or stdin line-by-line.
@proc_frame fs64_scan_file, 64, rbx, rsi, rdi, r12, r13, r14, r15
    mov r12, rcx ; path
    mov r13d, edx ; show_pfx
    mov dword [rsp + 48], 0 ; matched
    xor r14d, r14d ; line_num

    mov rdx, qword [rel g_u64_arena]
    mov r8d, 260000
    mov r9d, 1
    cmp dword [rel g_fs64_skip_bin], 0
    je .ld
    xor r9d, r9d
.ld:
    call u64_load_stream
    jc .done
    mov rbx, rax

    cmp dword [rel g_fs64_skip_bin], 0
    je .st
    mov rdi, qword [rel g_u64_arena]
    xor ecx, ecx
.bchk:
    cmp rcx, rbx
    jae .st
    cmp byte [rdi + rcx], 0
    je .done
    inc rcx
    jmp .bchk

.st:
    mov rsi, qword [rel g_u64_arena]
    lea rbx, [rsi + rbx] ; end

.ln_lp:
    cmp rsi, rbx
    jae .done
    cmp byte [rsi], 0x1A
    je .done
    inc r14
    mov r15, rsi
    sub r15, qword [rel g_u64_arena] ; byte offset
    mov rdi, rsi
.eol:
    cmp rdi, rbx
    jae .got_l
    mov al, byte [rdi]
    cmp al, 13
    je .got_l
    cmp al, 10
    je .got_l
    inc rdi
    jmp .eol

.got_l:
    mov rdx, rdi
    sub rdx, rsi
    mov qword [rsp + 56], rdx
    mov rcx, rsi
    call fs64_eval_nfa
    xor eax, dword [rel g_fs64_inv]
    test eax, eax
    jz .adv

    mov dword [rsp + 48], 1
    cmp dword [rel g_fs64_name_only], 0
    je .full
    cmp byte [r12], 0
    je .done
    mov rcx, r12
    call u64_write_str
    call u64_write_crlf
    jmp .done

.full:
    test r13d, r13d
    jz .chk_n
    mov rcx, r12
    call u64_write_str
    mov cl, ':'
    call u64_write_chr
.chk_n:
    cmp dword [rel g_fs64_nums], 0
    je .chk_o
    mov rcx, r14
    call u64_print_u64
    mov cl, ':'
    call u64_write_chr
.chk_o:
    cmp dword [rel g_fs64_offs], 0
    je .body
    mov rcx, r15
    call u64_print_u64
    mov cl, ':'
    call u64_write_chr
.body:
    mov rcx, rsi
    mov rdx, qword [rsp + 56]
    call u64_write_out
    call u64_write_crlf

.adv:
    cmp rdi, rbx
    jae .done
    cmp byte [rdi], 13
    jne .clf
    inc rdi
.clf:
    cmp rdi, rbx
    jae .nx
    cmp byte [rdi], 10
    jne .nx
    inc rdi
.nx:
    mov rsi, rdi
    jmp .ln_lp

.done:
    mov eax, dword [rsp + 48]
    @epilog_ret
@endproc

;; @proc fs64_walk_dir
;; @abi win64_ms
;; @in RCX = dir path, RDX = file mask, R8D = depth
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Enumerates matching files and recurses under /S.
@proc_frame fs64_walk_dir, 1664, rbx, rsi, rdi, r12, r13
    cmp r8d, 16
    jae .dn
    mov r12, rdx
    mov r13d, r8d
    mov rdx, rcx
    lea rcx, [rsp + 1120]
    call u64_strcpy

    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    mov r8, r12
    call u64_path_join
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .sub
    mov rbx, rax
.flp:
    test dword [rsp + 64], 0x10
    jnz .fnx
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 608]
    mov edx, dword [rel g_fs64_show_pfx]
    call fs64_scan_file
    test eax, eax
    jz .fnx
    mov dword [rel g_fs64_hit], 1
.fnx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .flp
    mov rcx, rbx
    call qword [rel __imp_FindClose]

.sub:
    cmp dword [rel g_fs64_sub], 0
    je .dn
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rel szFs64Star]
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
    jne .go
    cmp byte [rsp + 89], 0
    je .dnx
    cmp byte [rsp + 89], '.'
    jne .go
    cmp byte [rsp + 90], 0
    je .dnx
.go:
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 608]
    mov rdx, r12
    lea r8d, [r13d + 1]
    call fs64_walk_dir
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
;; @out Terminates via ExitProcess (0=matched, 1=none, 2=bad syntax)
;; @err Exits 2 on error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 FINDSTR.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_fs64_beg], 0
    mov dword [rel g_fs64_end], 0
    mov dword [rel g_fs64_lit], 0
    mov dword [rel g_fs64_sub], 0
    mov dword [rel g_fs64_ci], 0
    mov dword [rel g_fs64_exact], 0
    mov dword [rel g_fs64_inv], 0
    mov dword [rel g_fs64_nums], 0
    mov dword [rel g_fs64_name_only], 0
    mov dword [rel g_fs64_offs], 0
    mov dword [rel g_fs64_skip_bin], 0
    mov dword [rel g_fs64_has_pat], 0
    mov dword [rel g_fs64_hit], 0
    mov dword [rel g_fs64_show_pfx], 0

.sw_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .args_end
    cmp dl, '/'
    jne .pos_tok
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'B'
    je .ob
    cmp al, 'E'
    je .oe
    cmp al, 'L'
    je .ol
    cmp al, 'R'
    je .or
    cmp al, 'S'
    je .os
    cmp al, 'I'
    je .oi
    cmp al, 'X'
    je .ox
    cmp al, 'V'
    je .ov
    cmp al, 'N'
    je .on
    cmp al, 'M'
    je .om
    cmp al, 'O'
    je .oo
    cmp al, 'P'
    je .op
    cmp al, 'C'
    je .oc
    cmp al, 'G'
    je .og
    jmp .sw_lp
.ob:
    mov dword [rel g_fs64_beg], 1
    jmp .sw_lp
.oe:
    mov dword [rel g_fs64_end], 1
    jmp .sw_lp
.ol:
    mov dword [rel g_fs64_lit], 1
    jmp .sw_lp
.or:
    mov dword [rel g_fs64_lit], 0
    jmp .sw_lp
.os:
    mov dword [rel g_fs64_sub], 1
    mov dword [rel g_fs64_show_pfx], 1
    jmp .sw_lp
.oi:
    mov dword [rel g_fs64_ci], 1
    jmp .sw_lp
.ox:
    mov dword [rel g_fs64_exact], 1
    jmp .sw_lp
.ov:
    mov dword [rel g_fs64_inv], 1
    jmp .sw_lp
.on:
    mov dword [rel g_fs64_nums], 1
    jmp .sw_lp
.om:
    mov dword [rel g_fs64_name_only], 1
    jmp .sw_lp
.oo:
    mov dword [rel g_fs64_offs], 1
    jmp .sw_lp
.op:
    mov dword [rel g_fs64_skip_bin], 1
    jmp .sw_lp
.oc:
    cmp byte [rel g_u64_arg_a + 2], ':'
    jne .bad
    lea rcx, [rel g_fs64_pat]
    lea rdx, [rel g_u64_arg_a + 3]
    call u64_strcpy
    mov dword [rel g_fs64_has_pat], 1
    mov dword [rel g_fs64_lit], 1
    jmp .sw_lp
.og:
    cmp byte [rel g_u64_arg_a + 2], ':'
    jne .bad
    lea rcx, [rel g_u64_arg_a + 3]
    lea rdx, [rel g_fs64_pat]
    mov r8d, 512
    mov r9d, 1
    call u64_load_stream
    jc .bad
    lea rdi, [rel g_fs64_pat]
    xor ecx, ecx
.tg:
    mov al, byte [rdi + rcx]
    test al, al
    jz .gok
    cmp al, 13
    je .tmg
    cmp al, 10
    je .tmg
    inc ecx
    jmp .tg
.tmg:
    mov byte [rdi + rcx], 0
.gok:
    mov dword [rel g_fs64_has_pat], 1
    jmp .sw_lp

.pos_tok:
    cmp dword [rel g_fs64_has_pat], 0
    jne .files_phase
    mov rcx, rsi
    lea rdx, [rel g_fs64_pat]
    call u64_read_tok
    mov rsi, rax
    mov dword [rel g_fs64_has_pat], 1
    jmp .sw_lp

.files_phase:
    lea rcx, [rel g_fs64_pat]
    mov edx, dword [rel g_fs64_lit]
    call fs64_compile_nfa

    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rcx, rax
    call u64_skip_ws
    test dl, dl
    jz .flp
    mov dword [rel g_fs64_show_pfx], 1

.flp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .ex_res
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    cmp dword [rel g_fs64_sub], 0
    jne .sub_m
    lea rcx, [rel g_u64_arg_b]
    mov edx, dword [rel g_fs64_show_pfx]
    call fs64_scan_file
    test eax, eax
    jz .flp
    mov dword [rel g_fs64_hit], 1
    jmp .flp
.sub_m:
    lea rcx, [rel g_u64_arg_c]
    mov edx, 1000
    call u64_get_cwd
    lea rcx, [rel g_u64_arg_c]
    lea rdx, [rel g_u64_arg_b]
    xor r8d, r8d
    call fs64_walk_dir
    jmp .flp

.args_end:
    cmp dword [rel g_fs64_has_pat], 0
    je .bad
    lea rcx, [rel g_fs64_pat]
    mov edx, dword [rel g_fs64_lit]
    call fs64_compile_nfa
    mov byte [rel g_u64_arg_b], 0
    lea rcx, [rel g_u64_arg_b]
    xor edx, edx
    call fs64_scan_file
    mov dword [rel g_fs64_hit], eax

.ex_res:
    cmp dword [rel g_fs64_hit], 0
    jne .ex0
    mov ecx, 1
    call u64_exit
    @epilog_ret
.ex0:
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.help:
    lea rcx, [rel szFs64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szFs64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 2
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szFs64Star, "*", 0
@data db szFs64Syn, "FINDSTR: Bad command line", 0
@data db szFs64Help, "Searches for strings in files using literal or 64-bit Thompson NFA regular expressions.", 0

@include "u_data64.inc"

@res qword g_nfa64_star, 1
@res qword g_nfa64_acc, 1
@res dword g_nfa64_len, 1
@res dword g_nfa64_bol, 1
@res dword g_nfa64_eol, 1
@res dword g_fs64_beg, 1
@res dword g_fs64_end, 1
@res dword g_fs64_lit, 1
@res dword g_fs64_sub, 1
@res dword g_fs64_ci, 1
@res dword g_fs64_exact, 1
@res dword g_fs64_inv, 1
@res dword g_fs64_nums, 1
@res dword g_fs64_name_only, 1
@res dword g_fs64_offs, 1
@res dword g_fs64_skip_bin, 1
@res dword g_fs64_has_pat, 1
@res dword g_fs64_hit, 1
@res dword g_fs64_show_pfx, 1
@res byte g_nfa64_kind, 64
@res byte g_nfa64_ch, 64
@res byte g_nfa64_bmaps, 2048
@res byte g_fs64_pat, 1024
