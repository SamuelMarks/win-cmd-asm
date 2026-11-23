;; @file findstr.asm
;; @isa 386
;; @desc Standalone IA-32 FINDSTR.EXE utility with Thompson NFA bitset regex engine (/B /E /L /R /S /I /X /V /N /M /O /P /C).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc fs_compile_nfa
;; @abi win32
;; @in ESI = pattern string, EDX = literal_only (1=/L, 0=/R regex)
;; @out g_nfa_len = number of compiled NFA atoms (0..30), g_nfa_bol/g_nfa_eol anchored flags
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Compiles pattern into up to 30 Thompson NFA state slots (op, param, star_flag, class_idx).
@proc fs_compile_nfa
    push ebx
    push esi
    push edi

    mov dword [g_nfa_bol], 0
    mov dword [g_nfa_eol], 0
    mov dword [g_nfa_star_mask], 0
    xor ecx, ecx ; state index

    test edx, edx
    jnz .comp_lp
    cmp byte [esi], '^'
    jne .comp_lp
    mov dword [g_nfa_bol], 1
    inc esi

.comp_lp:
    movzx eax, byte [esi]
    test al, al
    jz .comp_done
    cmp ecx, 30
    jae .comp_done

    test edx, edx
    jnz .lit_atom

    cmp al, '$'
    jne .not_eol
    cmp byte [esi + 1], 0
    jne .not_eol
    mov dword [g_nfa_eol], 1
    inc esi
    jmp .comp_done

.not_eol:
    cmp al, '.'
    jne .not_dot
    mov byte [g_nfa_kind + ecx], 2 ; ANY
    mov byte [g_nfa_ch + ecx], 0
    inc esi
    jmp .chk_star

.not_dot:
    cmp al, 0x5C
    jne .not_esc
    cmp byte [esi + 1], 0
    je .lit_atom
    inc esi
    mov al, byte [esi]
    cmp al, 'd'
    je .esc_digit
    cmp al, 'w'
    je .esc_word
    cmp al, 's'
    je .esc_space
    jmp .lit_atom
.esc_digit:
    mov byte [g_nfa_kind + ecx], 4 ; DIGIT
    inc esi
    jmp .chk_star
.esc_word:
    mov byte [g_nfa_kind + ecx], 5 ; WORD
    inc esi
    jmp .chk_star
.esc_space:
    mov byte [g_nfa_kind + ecx], 6 ; SPACE
    inc esi
    jmp .chk_star

.not_esc:
    cmp al, '['
    jne .lit_atom
    ; Parse character class into 32-byte bitmap at g_nfa_bmaps + ecx*32
    inc esi
    mov edi, ecx
    shl edi, 5
    add edi, addr(g_nfa_bmaps)
    xor ebx, ebx
.zr_bm:
    cmp ebx, 32
    jae .bm_cleared
    mov byte [edi + ebx], 0
    inc ebx
    jmp .zr_bm
.bm_cleared:
    xor ebx, ebx ; invert flag
    cmp byte [esi], '^'
    jne .cls_lp
    mov ebx, 1
    inc esi
.cls_lp:
    movzx eax, byte [esi]
    test al, al
    jz .cls_end
    cmp al, ']'
    jne .cls_item
    inc esi
    jmp .cls_end
.cls_item:
    inc esi
    cmp byte [esi], '-'
    jne .cls_single
    cmp byte [esi + 1], 0
    je .cls_single
    cmp byte [esi + 1], ']'
    je .cls_single
    movzx eax, byte [esi - 1]
    push edx
    movzx edx, byte [esi + 1]
    add esi, 2
.rng_lp:
    cmp eax, edx
    ja .rng_dn
    push eax
    push ecx
    mov ecx, eax
    and ecx, 7
    shr eax, 3
    mov ch, 1
    shl ch, cl
    or byte [edi + eax], ch
    pop ecx
    pop eax
    inc eax
    jmp .rng_lp
.rng_dn:
    pop edx
    jmp .cls_lp

.cls_single:
    push ecx
    mov ecx, eax
    and ecx, 7
    shr eax, 3
    mov ch, 1
    shl ch, cl
    or byte [edi + eax], ch
    pop ecx
    jmp .cls_lp

.cls_end:
    test ebx, ebx
    jz .cls_store
    xor ebx, ebx
.inv_bm:
    cmp ebx, 32
    jae .cls_store
    not byte [edi + ebx]
    inc ebx
    jmp .inv_bm
.cls_store:
    mov byte [g_nfa_kind + ecx], 3 ; BITMAP CLASS
    jmp .chk_star

.lit_atom:
    mov byte [g_nfa_kind + ecx], 1 ; LITERAL
    cmp dword [g_fs_ci], 0
    je .st_lit
    call u32_chr_upper
.st_lit:
    mov byte [g_nfa_ch + ecx], al
    inc esi

.chk_star:
    test edx, edx
    jnz .no_star
    cmp byte [esi], '*'
    jne .no_star
    inc esi
    mov eax, 1
    shl eax, cl
    or dword [g_nfa_star_mask], eax
.no_star:
    inc ecx
    jmp .comp_lp

.comp_done:
    mov dword [g_nfa_len], ecx
    mov eax, 1
    mov cl, byte [g_nfa_len]
    shl eax, cl
    mov dword [g_nfa_accept_bit], eax

    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc fs_eps_closure
;; @abi win32
;; @in EAX = active NFA 32-bit state bitset
;; @out EAX = epsilon-closed NFA 32-bit state bitset across '*' Kleene-star states
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Propagates epsilon transitions i -> i+1 whenever state i has '*' in g_nfa_star_mask.
@proc fs_eps_closure
    mov edx, dword [g_nfa_star_mask]
    test edx, edx
    jz .dn
    mov ecx, 30
.lp:
    mov edx, eax
    and edx, dword [g_nfa_star_mask]
    shl edx, 1
    or eax, edx
    dec ecx
    jnz .lp
.dn:
    ret
@endproc

;; @proc fs_atom_matches
;; @abi win32
;; @in ECX = state index (0..29), AL = input byte
;; @out EAX = 1 if input byte matches state ECX, 0 otherwise
;; @err none
;; @clobbers eax, edx
;; @desc Evaluates a single NFA state atom (literal, '.', [class], \d, \w, \s).
@proc fs_atom_matches
    push ebx
    mov bl, al
    cmp dword [g_fs_ci], 0
    je .got_ch
    call u32_chr_upper
    mov bl, al
.got_ch:
    mov dl, byte [g_nfa_kind + ecx]
    cmp dl, 1
    je .k_lit
    cmp dl, 2
    je .yes
    cmp dl, 3
    je .k_cls
    cmp dl, 4
    je .k_dig
    cmp dl, 5
    je .k_wrd
    cmp dl, 6
    je .k_spc
    jmp .no
.k_lit:
    cmp bl, byte [g_nfa_ch + ecx]
    je .yes
    jmp .no
.k_cls:
    push ecx
    mov edx, ecx
    shl edx, 5
    add edx, addr(g_nfa_bmaps)
    movzx eax, bl
    mov ecx, eax
    and ecx, 7
    shr eax, 3
    mov al, byte [edx + eax]
    shr al, cl
    pop ecx
    and eax, 1
    pop ebx
    ret
.k_dig:
    cmp bl, '0'
    jb .no
    cmp bl, '9'
    jbe .yes
    jmp .no
.k_wrd:
    cmp bl, '_'
    je .yes
    cmp bl, '0'
    jb .no
    cmp bl, '9'
    jbe .yes
    mov al, bl
    call u32_chr_upper
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    jbe .yes
    jmp .no
.k_spc:
    cmp bl, 0x20
    je .yes
    cmp bl, 0x09
    je .yes
    jmp .no
.yes:
    mov eax, 1
    pop ebx
    ret
.no:
    xor eax, eax
    pop ebx
    ret
@endproc

;; @proc fs_eval_nfa
;; @abi win32
;; @in ESI = line start, ECX = line length
;; @out EAX = 1 if line matches NFA & anchor flags, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Runs O(N) bitset Thompson NFA simulation over a single line with zero backtracking.
@proc fs_eval_nfa
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx ; line length

    mov ebx, dword [g_fs_beg]
    or ebx, dword [g_nfa_bol]
    or ebx, dword [g_fs_exact]

    mov eax, 1 ; initial state 0
    call fs_eps_closure
    mov edi, eax ; active state bitset

    xor edx, edx ; byte offset in line
.step_lp:
    ; Check unanchored acceptance if EOL not required
    mov eax, dword [g_fs_end]
    or eax, dword [g_nfa_eol]
    or eax, dword [g_fs_exact]
    jnz .chk_end
    mov eax, edi
    and eax, dword [g_nfa_accept_bit]
    jnz .yes

.chk_end:
    cmp edx, ebp
    jae .at_eol

    ; Transition all active states on byte [esi + edx]
    push ebx
    push edx
    mov bl, byte [esi + edx]
    xor edx, edx ; next_states
    xor ecx, ecx ; state i
.st_lp:
    cmp ecx, dword [g_nfa_len]
    jae .st_dn
    mov eax, 1
    shl eax, cl
    test edi, eax
    jz .st_nx
    mov al, bl
    push edx
    call fs_atom_matches
    pop edx
    test eax, eax
    jz .st_nx
    mov eax, 1
    shl eax, cl
    test eax, dword [g_nfa_star_mask]
    jz .adv_st
    or edx, eax ; self-loop on Kleene star
.adv_st:
    shl eax, 1
    or edx, eax
.st_nx:
    inc ecx
    jmp .st_lp
.st_dn:
    mov eax, edx
    pop edx
    pop ebx
    test ebx, ebx
    jnz .no_reseed
    or eax, 1 ; unanchored start can begin at any column
.no_reseed:
    push edx
    call fs_eps_closure
    pop edx
    mov edi, eax
    inc edx
    jmp .step_lp

.at_eol:
    mov eax, edi
    and eax, dword [g_nfa_accept_bit]
    jnz .yes
    xor eax, eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
.yes:
    mov eax, 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc fs_scan_file
;; @abi win32
;; @in ESI = file path (empty=stdin), EDX = print_fname_prefix (1 or 0)
;; @out EAX = 1 if matched, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Scans a file or stdin line-by-line with /P binary filter, /O offset, /N line num, /M name-only.
@proc fs_scan_file
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 24
    mov dword [esp], esi ; path
    mov dword [esp + 4], edx ; show_prefix
    mov dword [esp + 8], 0 ; matched
    mov dword [esp + 12], 0 ; line_num
    mov dword [esp + 16], 0 ; line_byte_off

    mov edi, dword [g_u32_arena]
    mov ecx, 260000
    xor edx, edx
    cmp dword [g_fs_skip_bin], 0
    jne .raw_ld
    mov edx, 1
.raw_ld:
    call u32_load_stream
    jc .done
    mov ebx, eax

    ; Under /P, skip files containing NUL bytes
    cmp dword [g_fs_skip_bin], 0
    je .scan_st
    mov edi, dword [g_u32_arena]
    xor ecx, ecx
.bin_chk:
    cmp ecx, ebx
    jae .scan_st
    cmp byte [edi + ecx], 0
    je .done
    inc ecx
    jmp .bin_chk

.scan_st:
    mov esi, dword [g_u32_arena]
    mov dword [esp + 20], esi ; base
    lea ebp, [esi + ebx]

.ln_lp:
    cmp esi, ebp
    jae .done
    cmp byte [esi], 0x1A
    je .done
    inc dword [esp + 12]
    mov eax, esi
    sub eax, dword [esp + 20]
    mov dword [esp + 16], eax

    mov edi, esi
.eol_lp:
    cmp edi, ebp
    jae .got_ln
    mov al, byte [edi]
    cmp al, 13
    je .got_ln
    cmp al, 10
    je .got_ln
    inc edi
    jmp .eol_lp

.got_ln:
    mov ecx, edi
    sub ecx, esi
    push edi
    push ecx
    call fs_eval_nfa
    xor eax, dword [g_fs_inv]
    pop ecx
    pop edi
    test eax, eax
    jz .nx_ln

    mov dword [esp + 8], 1
    cmp dword [g_fs_name_only], 0
    je .full_out
    mov esi, dword [esp]
    cmp byte [esi], 0
    je .done
    call u32_write_str
    call u32_write_crlf
    jmp .done

.full_out:
    push edi
    push esi
    push ecx
    cmp dword [esp + 16], 0 ; [esp+4] shifted by 3 pushes
    je .chk_num
    mov esi, dword [esp + 12]
    call u32_write_str
    mov al, ':'
    call u32_write_chr
.chk_num:
    cmp dword [g_fs_nums], 0
    je .chk_off
    mov eax, dword [esp + 24]
    call u32_print_u32
    mov al, ':'
    call u32_write_chr
.chk_off:
    cmp dword [g_fs_offs], 0
    je .emit_body
    mov eax, dword [esp + 28]
    call u32_print_u32
    mov al, ':'
    call u32_write_chr
.emit_body:
    pop ecx
    pop esi
    call u32_write_out
    call u32_write_crlf
    pop edi

.nx_ln:
    cmp edi, ebp
    jae .done
    cmp byte [edi], 13
    jne .lf_c
    inc edi
.lf_c:
    cmp edi, ebp
    jae .set_s
    cmp byte [edi], 10
    jne .set_s
    inc edi
.set_s:
    mov esi, edi
    jmp .ln_lp

.done:
    mov eax, dword [esp + 8]
    add esp, 24
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc fs_walk_dir
;; @abi win32
;; @in ESI = directory path, EDI = leaf file wildcard, ECX = depth
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Enumerates matching files in ESI and recurses under /S.
@proc fs_walk_dir
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 1600
    mov dword [esp + 1568], edi
    mov dword [esp + 1572], ecx
    lea edi, [esp + 1056]
    call u32_strcpy

    lea esi, [esp + 1056]
    mov edi, dword [esp + 1568]
    lea ebx, [esp + 544]
    call u32_path_join

    lea esi, [esp + 544]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .chk_sub
    mov ebp, eax

.f_lp:
    test dword [esp], 0x10
    jnz .nx_f
    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov edx, dword [g_fs_show_pfx]
    call fs_scan_file
    test eax, eax
    jz .nx_f
    mov dword [g_fs_any_hit], 1
.nx_f:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .f_lp
    push ebp
    call dword [__imp__FindClose@4]

.chk_sub:
    cmp dword [g_fs_sub], 0
    je .done
    cmp dword [esp + 1572], 16
    jae .done
    lea esi, [esp + 1056]
    mov edi, addr(szFsStar)
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .done
    mov ebp, eax
.d_lp:
    test dword [esp], 0x10
    jz .nx_d
    cmp byte [esp + 20], '.'
    jne .go_d
    cmp byte [esp + 21], 0
    je .nx_d
    cmp byte [esp + 21], '.'
    jne .go_d
    cmp byte [esp + 22], 0
    je .nx_d
.go_d:
    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov edi, dword [esp + 1568]
    mov ecx, dword [esp + 1572]
    inc ecx
    call fs_walk_dir
.nx_d:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .d_lp
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
;; @out Terminates via ExitProcess (0=matched, 1=none, 2=bad syntax)
;; @err Exits 2 on missing pattern
;; @clobbers eax, ecx, edx
;; @desc Parses FINDSTR switches, compiles NFA regex/literal, and scans files or stdin.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_fs_beg], 0
    mov dword [g_fs_end], 0
    mov dword [g_fs_lit], 0
    mov dword [g_fs_sub], 0
    mov dword [g_fs_ci], 0
    mov dword [g_fs_exact], 0
    mov dword [g_fs_inv], 0
    mov dword [g_fs_nums], 0
    mov dword [g_fs_name_only], 0
    mov dword [g_fs_offs], 0
    mov dword [g_fs_skip_bin], 0
    mov dword [g_fs_has_pat], 0
    mov dword [g_fs_any_hit], 0
    mov dword [g_fs_file_cnt], 0

.sw_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .args_end
    cmp dl, '/'
    jne .pos_tok
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'B'
    je .o_b
    cmp al, 'E'
    je .o_e
    cmp al, 'L'
    je .o_l
    cmp al, 'R'
    je .o_r
    cmp al, 'S'
    je .o_s
    cmp al, 'I'
    je .o_i
    cmp al, 'X'
    je .o_x
    cmp al, 'V'
    je .o_v
    cmp al, 'N'
    je .o_n
    cmp al, 'M'
    je .o_m
    cmp al, 'O'
    je .o_o
    cmp al, 'P'
    je .o_p
    cmp al, 'C'
    je .o_c
    cmp al, 'G'
    je .o_g
    cmp al, 'A'
    je .sw_lp
    cmp al, 'D'
    je .sw_lp
    jmp .bad_syn

.o_b:
    mov dword [g_fs_beg], 1
    jmp .sw_lp
.o_e:
    mov dword [g_fs_end], 1
    jmp .sw_lp
.o_l:
    mov dword [g_fs_lit], 1
    jmp .sw_lp
.o_r:
    mov dword [g_fs_lit], 0
    jmp .sw_lp
.o_s:
    mov dword [g_fs_sub], 1
    mov dword [g_fs_show_pfx], 1
    jmp .sw_lp
.o_i:
    mov dword [g_fs_ci], 1
    jmp .sw_lp
.o_x:
    mov dword [g_fs_exact], 1
    jmp .sw_lp
.o_v:
    mov dword [g_fs_inv], 1
    jmp .sw_lp
.o_n:
    mov dword [g_fs_nums], 1
    jmp .sw_lp
.o_m:
    mov dword [g_fs_name_only], 1
    jmp .sw_lp
.o_o:
    mov dword [g_fs_offs], 1
    jmp .sw_lp
.o_p:
    mov dword [g_fs_skip_bin], 1
    jmp .sw_lp
.o_c:
    cmp byte [g_u32_arg_a + 2], ':'
    jne .bad_syn
    push esi
    mov esi, addr(g_u32_arg_a)
    add esi, 3
    mov edi, addr(g_fs_pat)
    call u32_strcpy
    pop esi
    mov dword [g_fs_has_pat], 1
    mov dword [g_fs_lit], 1
    jmp .sw_lp
.o_g:
    cmp byte [g_u32_arg_a + 2], ':'
    jne .bad_syn
    push esi
    mov esi, addr(g_u32_arg_a)
    add esi, 3
    mov edi, addr(g_fs_pat)
    mov ecx, 512
    mov edx, 1
    call u32_load_stream
    jc .pop_bad
    ; Trim at first CR/LF
    xor ecx, ecx
.trim_g:
    mov al, byte [g_fs_pat + ecx]
    test al, al
    jz .g_ok
    cmp al, 13
    je .term_g
    cmp al, 10
    je .term_g
    inc ecx
    jmp .trim_g
.term_g:
    mov byte [g_fs_pat + ecx], 0
.g_ok:
    pop esi
    mov dword [g_fs_has_pat], 1
    jmp .sw_lp
.pop_bad:
    pop esi
    jmp .bad_syn

.pos_tok:
    cmp dword [g_fs_has_pat], 0
    jne .files_phase
    mov edi, addr(g_fs_pat)
    call u32_read_tok
    mov esi, eax
    mov dword [g_fs_has_pat], 1
    jmp .sw_lp

.files_phase:
    ; Compile NFA once before scanning file arguments
    push esi
    mov esi, addr(g_fs_pat)
    mov edx, dword [g_fs_lit]
    call fs_compile_nfa
    pop esi

    ; Peek ahead to see if >1 file token or /S is present
    push esi
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    call u32_skip_ws
    test dl, dl
    pop esi
    jz .file_lp
    mov dword [g_fs_show_pfx], 1

.file_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .exit_res
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    inc dword [g_fs_file_cnt]

    cmp dword [g_fs_sub], 0
    jne .sub_mode
    push esi
    mov esi, addr(g_u32_arg_b)
    mov edx, dword [g_fs_show_pfx]
    call fs_scan_file
    pop esi
    test eax, eax
    jz .file_lp
    mov dword [g_fs_any_hit], 1
    jmp .file_lp

.sub_mode:
    push esi
    mov edi, addr(g_u32_arg_c)
    mov ecx, 1000
    call u32_get_cwd
    mov esi, addr(g_u32_arg_c)
    mov edi, addr(g_u32_arg_b)
    xor ecx, ecx
    call fs_walk_dir
    pop esi
    jmp .file_lp

.args_end:
    cmp dword [g_fs_has_pat], 0
    je .bad_syn
    mov esi, addr(g_fs_pat)
    mov edx, dword [g_fs_lit]
    call fs_compile_nfa
    mov byte [g_u32_arg_b], 0
    mov esi, addr(g_u32_arg_b)
    xor edx, edx
    call fs_scan_file
    mov dword [g_fs_any_hit], eax

.exit_res:
    cmp dword [g_fs_any_hit], 0
    jne .ex0
    mov eax, 1
    call u32_exit
    jmp .done
.ex0:
    xor eax, eax
    call u32_exit
    jmp .done

.help:
    mov esi, addr(szFsHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_syn:
    mov esi, addr(szFsSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 2
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szFsStar, "*", 0
@data db szFsSyn, "FINDSTR: Bad command line", 0
@data db szFsHelp, "Searches for strings in files using literal or Thompson NFA regular expressions.", 0

@include "u_data32.inc"

@res dword g_fs_beg, 1
@res dword g_fs_end, 1
@res dword g_fs_lit, 1
@res dword g_fs_sub, 1
@res dword g_fs_ci, 1
@res dword g_fs_exact, 1
@res dword g_fs_inv, 1
@res dword g_fs_nums, 1
@res dword g_fs_name_only, 1
@res dword g_fs_offs, 1
@res dword g_fs_skip_bin, 1
@res dword g_fs_has_pat, 1
@res dword g_fs_any_hit, 1
@res dword g_fs_file_cnt, 1
@res dword g_fs_show_pfx, 1
@res dword g_nfa_len, 1
@res dword g_nfa_bol, 1
@res dword g_nfa_eol, 1
@res dword g_nfa_star_mask, 1
@res dword g_nfa_accept_bit, 1
@res byte g_nfa_kind, 32
@res byte g_nfa_ch, 32
@res byte g_nfa_bmaps, 1024
@res byte g_fs_pat, 1024
