;; @file exec_ast32.inc
;; @isa 386
;; @desc IA-32 AST Executor, FOR (/D, /R, /L, /F) Engine, Batch Frame/Label Runner, and PATHEXT/vm8086 Spawner.

@cpu 386
@format inc

;; @proc tok_strip_quotes32
;; @abi win32
;; @in ESI = NUL-terminated string
;; @out EAX = copy in arena with outer/embedded double quotes stripped, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Strips surrounding/inline '"' characters from an argument token in the statement arena.
@proc tok_strip_quotes32
    push esi
    push edi
    call str_len32
    inc ecx
    call arena_alloc32
    jc .fail
    mov edi, eax
    mov edx, eax
.lp:
    mov al, byte [esi]
    test al, al
    jz .done
    cmp al, '"'
    je .skip
    mov byte [edi], al
    inc edi
.skip:
    inc esi
    jmp .lp
.done:
    mov byte [edi], 0
    mov eax, edx
    pop edi
    pop esi
    clc
    ret
.fail:
    pop edi
    pop esi
    stc
    ret
@endproc

;; @proc exec_eval_if_cond32
;; @abi win32
;; @in ESI = AST_NODE32* (AST_IF)
;; @out EAX = 1 if condition holds (after NOT), 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Evaluates IF [NOT] [/I] (ERRORLEVEL | EXIST | DEFINED | CMDEXTVERSION | == | EQU..GEQ).
@proc exec_eval_if_cond32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, esi

    ; Expand Phase 3 + Phase 4 on pStr1 (and pStr2 if non-NULL)
    mov esi, dword [ebp + AST_NODE32.pStr1]
    test esi, esi
    jnz .exp1
    mov esi, addr(szEmpty32)
.exp1:
    call expand_phase3_32
    jc .false_raw
    mov esi, eax
    call expand_phase4_32
    jc .false_raw
    mov ebx, eax ; lhs

    xor edi, edi
    mov esi, dword [ebp + AST_NODE32.pStr2]
    test esi, esi
    jz .dispatch_sub
    call expand_phase3_32
    jc .false_raw
    mov esi, eax
    call expand_phase4_32
    jc .false_raw
    mov edi, eax ; rhs

.dispatch_sub:
    movzx eax, byte [ebp + AST_NODE32.bSub]
    cmp al, IFCMP_ERRORLEVEL
    je .c_errlvl
    cmp al, IFCMP_EXIST
    je .c_exist
    cmp al, IFCMP_DEFINED
    je .c_defined
    cmp al, IFCMP_CMDEXTVER
    je .c_extver
    cmp al, IFCMP_STR_EQ
    je .c_eq_str

    ; Extended comparisons EQU, NEQ, LSS, LEQ, GTR, GEQ:
    ; Strip outer quotes for numeric probe; if BOTH parse completely as signed i32, compare numerically!
    mov esi, ebx
    call tok_strip_quotes32
    jc .c_lex_cmp
    mov esi, eax
    call exp_parse_signed_int32
    cmp byte [edx], 0
    jne .c_lex_cmp
    cmp edx, esi
    je .c_lex_cmp
    push eax ; lhs_num

    mov esi, edi
    call tok_strip_quotes32
    jc .pop_lex_cmp
    mov esi, eax
    call exp_parse_signed_int32
    cmp byte [edx], 0
    jne .pop_lex_cmp
    cmp edx, esi
    je .pop_lex_cmp
    mov ecx, eax ; rhs_num
    pop eax      ; lhs_num
    jmp .cmp_i32_vals

.pop_lex_cmp:
    pop eax
.c_lex_cmp:
    test byte [ebp + AST_NODE32.bFlags], IFFLAG_NOCASE
    jnz .lex_i
    mov esi, ebx
    call str_cmp_exact32
    mov ecx, 0
    jmp .cmp_i32_vals
.lex_i:
    mov esi, ebx
    call str_cmp_i32
    mov ecx, 0

.cmp_i32_vals:
    ; Compare signed EAX vs ECX according to bSub (IFCMP_EQU..IFCMP_GEQ)
    mov dl, byte [ebp + AST_NODE32.bSub]
    xor ebx, ebx
    cmp eax, ecx
    je .ord_eq
    jl .ord_lt
    ; EAX > ECX
    cmp dl, IFCMP_NEQ
    je .true_raw
    cmp dl, IFCMP_GTR
    je .true_raw
    cmp dl, IFCMP_GEQ
    je .true_raw
    jmp .false_raw
.ord_eq:
    cmp dl, IFCMP_EQU
    je .true_raw
    cmp dl, IFCMP_LEQ
    je .true_raw
    cmp dl, IFCMP_GEQ
    je .true_raw
    jmp .false_raw
.ord_lt:
    cmp dl, IFCMP_NEQ
    je .true_raw
    cmp dl, IFCMP_LSS
    je .true_raw
    cmp dl, IFCMP_LEQ
    je .true_raw
    jmp .false_raw

.c_eq_str:
    mov esi, ebx
    test byte [ebp + AST_NODE32.bFlags], IFFLAG_NOCASE
    jnz .eq_icase
    call str_cmp_exact32
    test eax, eax
    jz .true_raw
    jmp .false_raw
.eq_icase:
    call str_eq_i32
    test eax, eax
    jnz .true_raw
    jmp .false_raw

.c_errlvl:
    mov esi, ebx
    call tok_strip_quotes32
    jc .false_raw
    mov esi, eax
    call exp_parse_signed_int32
    cmp dword [g_last_exit_code], eax
    jge .true_raw
    jmp .false_raw

.c_exist:
    mov esi, ebx
    call tok_strip_quotes32
    jc .false_raw
    mov esi, eax
    call dev_match_name32
    test eax, eax
    jnz .true_raw
    ; Support wildcards via hal_find_first32 AND root directories ("C:") via hal_get_attr32!
    call hal_get_attr32
    jnc .true_raw
    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .false_raw
    mov edi, eax
    call hal_find_first32
    jc .false_raw
    push eax
    call dword [__imp__FindClose@4]
    jmp .true_raw

.c_defined:
    mov esi, ebx
    call tok_strip_quotes32
    jc .false_raw
    mov esi, eax
    call env_get_raw32
    test eax, eax
    jnz .true_raw
    jmp .false_raw

.c_extver:
    mov esi, ebx
    call tok_strip_quotes32
    jc .false_raw
    mov esi, eax
    call exp_parse_signed_int32
    cmp eax, 2
    jle .true_raw
    jmp .false_raw

.true_raw:
    mov eax, 1
    jmp .apply_not
.false_raw:
    xor eax, eax
.apply_not:
    test byte [ebp + AST_NODE32.bFlags], IFFLAG_NOT
    jz .done
    xor eax, 1
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc str_cmp_exact32
;; @abi win32
;; @in ESI = string A, EDI = string B
;; @out EAX < 0 if A < B, 0 if A == B, > 0 if A > B (exact byte comparison)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Performs exact case-sensitive lexicographical comparison of two NUL-terminated strings.
@proc str_cmp_exact32
    push esi
    push edi
.lp:
    movzx eax, byte [esi]
    movzx edx, byte [edi]
    sub eax, edx
    jnz .done
    test edx, edx
    jz .done
    inc esi
    inc edi
    jmp .lp
.done:
    pop edi
    pop esi
    ret
@endproc

;; @proc for_bind_run_one32
;; @abi win32
;; @in AL = variable char, ESI = bound value string, EDI = body AST_NODE32*
;; @out Executes body AST with %AL bound to ESI
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Pushes one FOR variable binding onto g_for_vars/g_for_vals, runs body AST, and pops scope.
@proc for_bind_run_one32
    push esi
    mov ecx, dword [g_for_depth]
    cmp ecx, W32_FOR_SCOPE_MAX
    jae .done
    mov byte [g_for_vars + ecx], al
    mov dword [g_for_vals + ecx * 4], esi
    inc ecx
    mov dword [g_for_depth], ecx

    mov esi, edi
    call exec_ast_node32

    dec dword [g_for_depth]
.done:
    pop esi
    ret
@endproc

;; @proc for_next_set_item32
;; @abi win32
;; @in ESI = set cursor
;; @out EAX = item string in arena (or 0 when set ends), EDX = updated cursor
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Reads next space/tab/comma/semicolon-separated item from a FOR (...) set.
@proc for_next_set_item32
    push ebx
    push esi
    push edi

.skip_sep:
    mov al, byte [esi]
    test al, al
    jz .end_set
    cmp al, 0x20
    je .adv_sep
    cmp al, 0x09
    je .adv_sep
    cmp al, ','
    je .adv_sep
    cmp al, ';'
    je .adv_sep
    cmp al, 0x0D
    je .adv_sep
    cmp al, 0x0A
    jne .tok_start
.adv_sep:
    inc esi
    jmp .skip_sep

.tok_start:
    mov ebx, esi
    xor ecx, ecx ; quote
.scan:
    mov al, byte [ebx]
    test al, al
    jz .got
    cmp al, '"'
    jne .chk_d
    xor ecx, 1
    inc ebx
    jmp .scan
.chk_d:
    test ecx, ecx
    jnz .adv_t
    cmp al, 0x20
    je .got
    cmp al, 0x09
    je .got
    cmp al, ','
    je .got
    cmp al, ';'
    je .got
    cmp al, 0x0D
    je .got
    cmp al, 0x0A
    je .got
.adv_t:
    inc ebx
    jmp .scan

.got:
    mov ecx, ebx
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .end_set
    mov edi, eax
    push eax
    rep movsb
    mov byte [edi], 0
    pop eax
    mov edx, ebx
    pop edi
    pop esi
    pop ebx
    ret

.end_set:
    xor eax, eax
    mov edx, esi
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc exec_for_plain_or_dirs32
;; @abi win32
;; @in ESI = set string, EDI = body AST_NODE32*, BL = var char, BH = bDirsOnly (0=files, 1=/D dirs)
;; @out Iterates literal items and wildcard expansions
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Executes plain FOR and FOR /D over literal items and filesystem wildcard matches.
@proc exec_for_plain_or_dirs32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, esi

.item_lp:
    cmp byte [g_exit_flag], 0
    jne .done
    cmp byte [g_goto_pending], 0
    jne .done
    cmp dword [g_ctrl_break], 0
    jne .done

    mov esi, ebp
    call for_next_set_item32
    mov ebp, edx
    test eax, eax
    jz .done

    mov esi, eax
    call tok_strip_quotes32
    jc .done
    mov esi, eax

    ; Check if item contains '*' or '?'
    xor ecx, ecx
.chk_wild:
    mov al, byte [esi + ecx]
    test al, al
    jz .is_literal
    cmp al, '*'
    je .is_wild
    cmp al, '?'
    je .is_wild
    inc ecx
    jmp .chk_wild

.is_literal:
    mov al, bl
    call for_bind_run_one32
    jmp .item_lp

.is_wild:
    ; Extract directory prefix of pattern (up to last '' or '/') so matched names keep prefix!
    push esi ; pattern
    call str_len32
    mov edx, ecx
.find_psep:
    test edx, edx
    jz .no_prefix
    mov al, byte [esi + edx - 1]
    cmp al, 0x5C
    je .has_prefix
    cmp al, '/'
    je .has_prefix
    cmp al, ':'
    je .has_prefix
    dec edx
    jmp .find_psep
.no_prefix:
    mov eax, addr(szEmpty32)
    jmp .got_prefix
.has_prefix:
    push edx
    lea ecx, [edx + 4]
    call arena_alloc32
    pop edx
    jc .pop_pat_done
    push edi
    mov edi, eax
    push eax
    mov ecx, edx
    rep movsb
    mov byte [edi], 0
    pop eax
    pop edi
.got_prefix:
    pop esi ; pattern
    push eax ; dirPrefix [esp]

    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .pop_pref_done
    push edi ; save body AST
    mov edi, eax
    push edi ; FIND_ENTRY32
    call hal_find_first32
    pop esi  ; FIND_ENTRY32
    pop edi  ; body AST
    jc .pop_pref_done
    push eax ; hFind [esp], dirPrefix [esp+4]

.wild_lp:
    ; Skip "." and ".."
    cmp byte [esi + FIND_ENTRY32.szNameWtf8], '.'
    jne .chk_kind_w
    cmp byte [esi + FIND_ENTRY32.szNameWtf8 + 1], 0
    je .wild_next
    cmp byte [esi + FIND_ENTRY32.szNameWtf8 + 1], '.'
    jne .chk_kind_w
    cmp byte [esi + FIND_ENTRY32.szNameWtf8 + 2], 0
    je .wild_next

.chk_kind_w:
    mov eax, dword [esi + FIND_ENTRY32.dwAttr]
    test bh, bh
    jnz .want_dir
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jnz .wild_next
    jmp .emit_match
.want_dir:
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .wild_next

.emit_match:
    ; Build "<dirPrefix><szNameWtf8>"
    mov ecx, 1024
    call arena_alloc32
    jc .wild_close
    mov edx, eax
    push esi
    push edi
    mov edi, edx
    mov esi, dword [esp + 12] ; dirPrefix
    call str_copy32
    mov edi, eax
    mov esi, dword [esp + 4] ; FIND_ENTRY32
    lea esi, [esi + FIND_ENTRY32.szNameWtf8]
    call str_copy32
    pop edi
    mov esi, edx
    mov al, bl
    call for_bind_run_one32
    pop esi

.wild_next:
    cmp byte [g_exit_flag], 0
    jne .wild_close
    cmp byte [g_goto_pending], 0
    jne .wild_close
    push ebx
    push edi
    mov ebx, dword [esp + 8] ; hFind
    mov edi, esi             ; FIND_ENTRY32
    call hal_find_next32
    pop edi
    pop ebx
    jnc .wild_lp

.wild_close:
    call dword [__imp__FindClose@4]
.pop_pref_done:
    pop eax ; dirPrefix
    jmp .item_lp

.pop_pat_done:
    pop eax
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc exec_for_walk_r32
;; @abi win32
;; @in ESI = current root dir, EDX = raw set string, EDI = body AST_NODE32*, BL = var char
;; @out Recursively walks directory tree and expands FOR set in each directory
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements FOR /R [[drive:]path] %V IN (set) DO body.
@proc exec_for_walk_r32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 12
    mov ebp, esp
    mov dword [ebp], esi     ; curDir
    mov dword [ebp + 4], edx ; setStr

    ; First run set items inside curDir: build "<curDir>\<item>" for each item in setStr
    mov esi, edx
.r_set_lp:
    mov edx, esi
    call for_next_set_item32
    mov dword [ebp + 8], edx
    test eax, eax
    jz .recurse_subdirs

    mov esi, eax
    call tok_strip_quotes32
    jc .done
    mov ebx, eax ; item (Wait: preserve bl=varChar in stack or high word!)
    ; Restore bl cleanly: reload from caller ebx on stack [ebp + 24]!
    mov ecx, 2048
    call arena_alloc32
    jc .done
    push edi
    mov edi, eax
    push eax
    mov esi, dword [ebp]
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .r_app_it
    mov byte [edi], 0x5C
    inc edi
.r_app_it:
    mov esi, ebx
    call str_copy32
    pop esi ; combined "<curDir>\<item>"
    pop edi ; body AST
    mov ebx, dword [ebp + 24] ; restore BL = var char
    xor bh, bh
    ; If item is "." just bind "<curDir>" or "<curDir>\."
    call exec_for_plain_or_dirs32
    mov esi, dword [ebp + 8]
    jmp .r_set_lp

.recurse_subdirs:
    ; Enumerate "<curDir>\*" for subdirectories (excluding "." and ".." and reparse points? Wait: cmd FOR /R skips junctions/symlinks or recurses? Skipping reparse loops prevents infinite cycles!)
    mov ecx, 2048
    call arena_alloc32
    jc .done
    push edi
    mov edi, eax
    push eax
    mov esi, dword [ebp]
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .r_star
    mov byte [edi], 0x5C
    inc edi
.r_star:
    mov word [edi], 0x002A ; "*"
    pop esi
    pop edi

    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .done
    mov ebx, eax ; FIND_ENTRY32
    push edi
    mov edi, ebx
    call hal_find_first32
    pop edi
    jc .done
    push eax ; hFind

.sub_lp:
    mov eax, dword [ebx + FIND_ENTRY32.dwAttr]
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .sub_nx
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .sub_nx
    cmp byte [ebx + FIND_ENTRY32.szNameWtf8], '.'
    jne .do_sub
    cmp byte [ebx + FIND_ENTRY32.szNameWtf8 + 1], 0
    je .sub_nx
    cmp byte [ebx + FIND_ENTRY32.szNameWtf8 + 1], '.'
    jne .do_sub
    cmp byte [ebx + FIND_ENTRY32.szNameWtf8 + 2], 0
    je .sub_nx

.do_sub:
    mov ecx, 2048
    call arena_alloc32
    jc .sub_close
    push edi
    mov edi, eax
    push eax
    mov esi, dword [ebp]
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .sub_app
    mov byte [edi], 0x5C
    inc edi
.sub_app:
    lea esi, [ebx + FIND_ENTRY32.szNameWtf8]
    call str_copy32
    pop esi ; childDir
    pop edi ; body AST
    mov edx, dword [ebp + 4]
    push ebx
    mov ebx, dword [ebp + 24]
    call exec_for_walk_r32
    pop ebx

.sub_nx:
    cmp byte [g_exit_flag], 0
    jne .sub_close
    push ebx
    push edi
    mov edi, ebx
    mov ebx, dword [esp + 8] ; hFind
    call hal_find_next32
    pop edi
    pop ebx
    jnc .sub_lp

.sub_close:
    call dword [__imp__FindClose@4]

.done:
    add esp, 12
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc for_f_process_line32
;; @abi win32
;; @in ESI = input line, EDI = body AST_NODE32*, EBP = FOR /F options block ([ebp+0]=eol, [ebp+4]=delims_ptr, [ebp+8]=tok_mask, [ebp+12]=star_after, [ebp+16]=var_char)
;; @out Splits line by delims, binds requested token(s) to %V, %(V+1)..., and executes body AST
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Tokenizes a single FOR /F line according to eol=, delims=, and tokens=m,n-o,* rules.
@proc for_f_process_line32
    push ebx
    push esi
    push edi

    cmp byte [esi], 0
    je .done

    ; Extract up to 26 tokens into stack array [esp .. esp+104] and trailing rest pointers [esp+104..esp+208]
    sub esp, 216
    mov ebx, esp
    xor ecx, ecx ; tok_idx (0-based, so token 1 is idx 0!)

    ; If delims == "", entire line is token 1!
    mov edx, dword [ebp + 4] ; delims_ptr
    cmp byte [edx], 0
    jne .skip_lead_delims

    ; Check EOL on raw line[0]
    mov al, byte [esi]
    cmp al, byte [ebp]
    je .free_done
    call arena_strdup32
    jc .free_done
    mov dword [ebx], eax
    mov dword [ebx + 104], addr(szEmpty32)
    mov ecx, 1
    jmp .bind_tokens

.skip_lead_delims:
.tok_lp:
    cmp ecx, 25
    jae .check_eol_first
.delim_lp:
    mov al, byte [esi]
    test al, al
    jz .check_eol_first
    push ecx
    mov ecx, dword [ebp + 4]
.d_chk:
    mov dl, byte [ecx]
    test dl, dl
    jz .not_d
    cmp al, dl
    je .is_d
    inc ecx
    jmp .d_chk
.is_d:
    pop ecx
    inc esi
    jmp .delim_lp
.not_d:
    pop ecx

    ; Start of token at ESI; scan until delimiter or NUL
    mov edx, esi
.t_scan:
    mov al, byte [edx]
    test al, al
    jz .t_end
    push ecx
    mov ecx, dword [ebp + 4]
.d_chk2:
    mov ah, byte [ecx]
    test ah, ah
    jz .not_d2
    cmp al, ah
    je .is_d2
    inc ecx
    jmp .d_chk2
.is_d2:
    pop ecx
    jmp .t_end
.not_d2:
    pop ecx
    inc edx
    jmp .t_scan

.t_end:
    ; Copy token [esi..edx) into arena
    push ecx
    push edx
    mov ecx, edx
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .pop2_abort
    push edi
    mov edi, eax
    push eax
    rep movsb
    mov byte [edi], 0
    pop eax
    pop edi
    pop edx ; end of token
    pop ecx ; tok_idx
    mov dword [ebx + ecx * 4], eax

    ; Skip delimiters after token to find start of `*` remainder!
    mov esi, edx
.rem_d_lp:
    mov al, byte [esi]
    test al, al
    jz .save_rem
    push ecx
    mov ecx, dword [ebp + 4]
.d_chk3:
    mov ah, byte [ecx]
    test ah, ah
    jz .not_d3
    cmp al, ah
    je .is_d3
    inc ecx
    jmp .d_chk3
.is_d3:
    pop ecx
    inc esi
    jmp .rem_d_lp
.not_d3:
    pop ecx
.save_rem:
    mov dword [ebx + 104 + ecx * 4], esi
    inc ecx
    cmp byte [esi], 0
    jne .tok_lp

.check_eol_first:
    test ecx, ecx
    jz .free_done
    ; Check eol character against first character of token 0
    mov eax, dword [ebx]
    mov al, byte [eax]
    cmp al, byte [ebp]
    je .free_done

.bind_tokens:
    ; Bind every set bit k (0..24) in [ebp + 8] (tok_mask) to sequential variables starting at [ebp + 16] (var_char)
    mov dword [ebx + 208], ecx ; extracted count
    mov eax, dword [g_for_depth]
    mov dword [ebx + 212], eax ; saved g_for_depth

    movzx edx, byte [ebp + 16] ; next var char (e.g. 'I', 'J', 'K'...)
    xor esi, esi ; bit index k = 0..24
    xor ecx, ecx ; bound_any_nonempty
.bind_lp:
    cmp esi, 25
    jae .chk_star_tok
    mov eax, dword [ebp + 8]
    bt eax, esi
    jnc .nx_bit

    mov eax, addr(szEmpty32)
    cmp esi, dword [ebx + 208]
    jae .push_v
    mov eax, dword [ebx + esi * 4]
    inc ecx
.push_v:
    push ecx
    mov ecx, dword [g_for_depth]
    cmp ecx, W32_FOR_SCOPE_MAX
    jae .pop_c
    mov byte [g_for_vars + ecx], dl
    mov dword [g_for_vals + ecx * 4], eax
    inc dword [g_for_depth]
    inc edx
.pop_c:
    pop ecx
.nx_bit:
    inc esi
    jmp .bind_lp

.chk_star_tok:
    ; If star_after ([ebp+12]) > 0, bind remainder after token (star_after - 1) to next var char DL!
    mov eax, dword [ebp + 12]
    test eax, eax
    jz .exec_body_chk
    dec eax
    mov esi, addr(szEmpty32)
    cmp eax, dword [ebx + 208]
    jae .push_star
    mov esi, dword [ebx + 104 + eax * 4]
    inc ecx
.push_star:
    push ecx
    mov ecx, dword [g_for_depth]
    cmp ecx, W32_FOR_SCOPE_MAX
    jae .pop_c2
    mov byte [g_for_vars + ecx], dl
    mov dword [g_for_vals + ecx * 4], esi
    inc dword [g_for_depth]
.pop_c2:
    pop ecx

.exec_body_chk:
    ; Authentic cmd.exe rule: if first requested token did not exist on this line (ecx == 0), skip body!
    test ecx, ecx
    jz .restore_scope
    mov esi, edi
    call exec_ast_node32

.restore_scope:
    mov eax, dword [ebx + 212]
    mov dword [g_for_depth], eax
    jmp .free_done

.pop2_abort:
    pop edx
    pop ecx
.free_done:
    add esp, 216
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc exec_for_f32
;; @abi win32
;; @in ESI = AST_NODE32* (AST_FOR with bSub == FOR_PARSE_F)
;; @out Executes FOR /F ["options"] %V IN (file-set | "string" | 'command' | `command`) DO body
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Full FOR /F parser supporting eol=, skip=, delims=, tokens=m,n-o,*, and usebackq!
@proc exec_for_f32
    push ebx
    push esi
    push edi
    push ebp
    ; Options frame [ebp + 0..32]:
    ; +0: eol byte (default ';')
    ; +4: delims ptr (default " 	")
    ; +8: tok_mask (default bit 0 = token 1)
    ; +12: star_after (0 = none, k = remainder after 1-based token k; -1 if tokens=*)
    ; +16: var_char
    ; +20: skip_count
    ; +24: usebackq (0/1)
    ; +28: saved AST_NODE32*
    sub esp, 36
    mov ebp, esp
    mov dword [ebp + 28], esi

    mov dword [ebp], ';'
    mov eax, addr(szDefDelims32)
    mov dword [ebp + 4], eax
    mov dword [ebp + 8], 1 ; token 1
    mov dword [ebp + 12], 0
    movzx eax, byte [esi + AST_NODE32.bVarChar]
    mov dword [ebp + 16], eax
    mov dword [ebp + 20], 0
    mov dword [ebp + 24], 0

    ; Parse optional options string in pExtra
    mov esi, dword [esi + AST_NODE32.pExtra]
    test esi, esi
    jz .opts_done
    cmp byte [esi], '"'
    jne .opt_lp
    inc esi
.opt_lp:
    call prs_skip_ws32
    mov esi, eax
    mov al, byte [esi]
    test al, al
    jz .opts_done
    cmp al, '"'
    je .opts_done

    ; Check usebackq, eol=, skip=, delims=, tokens=
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'U'
    je .opt_usebackq
    cmp al, 'E'
    je .opt_eol
    cmp al, 'S'
    je .opt_skip
    cmp al, 'D'
    je .opt_delims
    cmp al, 'T'
    je .opt_tokens
    inc esi
    jmp .opt_lp

.opt_usebackq:
    mov dword [ebp + 24], 1
    add esi, 8
    jmp .opt_lp

.opt_eol:
    ; "eol=c"
    add esi, 4
    movzx eax, byte [esi]
    mov dword [ebp], eax
    test eax, eax
    jz .opts_done
    inc esi
    jmp .opt_lp

.opt_skip:
    ; "skip=n"
    add esi, 5
    call sa32_parse_i32_str
    jc .opts_done
    mov dword [ebp + 20], eax
    mov esi, edx
    jmp .opt_lp

.opt_delims:
    ; "delims=xxx" (consumes until space or closing '"' — UNLESS closing '"' is preceded by 'delims= "'!)
    add esi, 7
    mov ebx, esi
    ; If `delims="`, empty delimiter set!
    cmp byte [ebx], '"'
    jne .del_scan
    cmp byte [ebx + 1], 0
    jne .del_scan
    mov eax, addr(szEmpty32)
    mov dword [ebp + 4], eax
    jmp .opts_done
.del_scan:
    mov al, byte [ebx]
    test al, al
    jz .del_got
    cmp al, '"'
    jne .del_sp
    cmp byte [ebx + 1], 0
    je .del_got
.del_sp:
    cmp al, 0x20
    jne .del_adv
    ; If `delims= "` at very end of options string, include space in delims!
    cmp byte [ebx + 1], '"'
    jne .del_got
    cmp byte [ebx + 2], 0
    jne .del_got
    inc ebx
    jmp .del_got
.del_adv:
    inc ebx
    jmp .del_scan
.del_got:
    mov ecx, ebx
    sub ecx, esi
    push ebx
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    pop ebx
    jc .opts_done
    mov dword [ebp + 4], eax
    mov edi, eax
    rep movsb
    mov byte [edi], 0
    mov esi, ebx
    jmp .opt_lp

.opt_tokens:
    ; "tokens=1,2-4,*"
    add esi, 7
    mov dword [ebp + 8], 0
    xor ebx, ebx ; highest 1-based token seen
.tk_spec_lp:
    mov al, byte [esi]
    cmp al, '*'
    je .tk_star
    cmp al, '0'
    jb .tk_end
    cmp al, '9'
    ja .tk_end
    call sa32_parse_i32_str
    jc .tk_end
    mov esi, edx
    test eax, eax
    jz .tk_comma
    mov edi, eax ; start_tok (1-based)
    mov ebx, eax
    cmp byte [esi], '-'
    jne .set_one_tok
    inc esi
    call sa32_parse_i32_str
    jc .set_one_tok
    mov esi, edx
    mov ebx, eax ; end_tok
.range_tok_lp:
    cmp edi, ebx
    ja .tk_comma
    lea ecx, [edi - 1]
    cmp ecx, 25
    jae .nx_rtok
    bts dword [ebp + 8], ecx
.nx_rtok:
    inc edi
    jmp .range_tok_lp

.set_one_tok:
    lea ecx, [edi - 1]
    cmp ecx, 25
    jae .tk_comma
    bts dword [ebp + 8], ecx
.tk_comma:
    cmp byte [esi], '*'
    je .tk_star
    cmp byte [esi], ','
    jne .tk_end
    inc esi
    jmp .tk_spec_lp

.tk_star:
    inc esi
    test ebx, ebx
    jnz .star_after_n
    ; tokens=* -> entire line after leading delims! Set delims=""
    mov eax, addr(szEmpty32)
    mov dword [ebp + 4], eax
    mov dword [ebp + 8], 1
    jmp .tk_end
.star_after_n:
    mov dword [ebp + 12], ebx
.tk_end:
    jmp .opt_lp

.opts_done:
    mov ebx, dword [ebp + 28] ; AST_NODE32*
    mov esi, dword [ebx + AST_NODE32.pStr1] ; raw inside-(...) set
    mov edi, dword [ebx + AST_NODE32.pChild1] ; body AST

    call prs_skip_ws32
    mov esi, eax
    mov al, byte [esi]
    test al, al
    jz .done

    ; Dispatch mode:
    ; Standard (usebackq=0): "..." -> literal string, '...' -> command, else -> file-set
    ; usebackq=1:            '...' -> literal string, `...` -> command, else -> file-set (supporting "quoted file.txt"!)
    cmp dword [ebp + 24], 0
    jne .ubq_mode

    cmp al, '"'
    je .lit_dquote
    cmp al, 0x27 ; '''
    je .cmd_squote
    jmp .fileset_mode

.ubq_mode:
    cmp al, 0x27 ; '''
    je .lit_squote
    cmp al, '`'
    je .cmd_bquote
    jmp .fileset_mode

.lit_dquote:
    mov dl, '"'
    jmp .do_lit_str
.lit_squote:
    mov dl, 0x27
.do_lit_str:
    inc esi
    push edx
    call arena_strdup32
    pop edx
    jc .done
    mov ebx, eax
    mov esi, ebx
    call str_len32
    test ecx, ecx
    jz .run_lit
    cmp byte [ebx + ecx - 1], dl
    jne .run_lit
    mov byte [ebx + ecx - 1], 0
.run_lit:
    cmp dword [ebp + 20], 0 ; skip > 0 skips the single literal line!
    jne .done
    mov esi, ebx
    call for_f_process_line32
    jmp .done

.cmd_squote:
    mov dl, 0x27
    jmp .do_cmd_capture
.cmd_bquote:
    mov dl, '`'
.do_cmd_capture:
    inc esi
    push edx
    call arena_strdup32
    pop edx
    jc .done
    mov ebx, eax
    mov esi, ebx
    call str_len32
    test ecx, ecx
    jz .done
    cmp byte [ebx + ecx - 1], dl
    jne .spawn_cap
    mov byte [ebx + ecx - 1], 0
.spawn_cap:
    ; Spawn child via CreatePipe + pipe_spawn_stage32 and read stdout lines!
    sub esp, 40 ; SA(12) + PI(16) + hRead(4) + hWrite(4) + skip_rem(4)
    mov dword [esp], 12
    mov dword [esp + 4], 0
    mov dword [esp + 8], 1 ; bInheritHandle = TRUE
    mov eax, esp
    lea ecx, [esp + 32] ; &hWrite
    lea edx, [esp + 28] ; &hRead
    push 65536
    push eax
    push ecx
    push edx
    call dword [__imp__CreatePipe@16]
    test eax, eax
    jz .cap_free_stk

    push ebx ; cmd
    mov ebx, dword [esp + 32] ; hRead
    call redir_set_non_inheritable32
    mov dword [esp + 32], eax
    pop esi ; cmd

    push edi ; save body AST
    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov ebx, eax
    mov edi, dword [esp + 36] ; hWrite
    lea edx, [esp + 16]       ; &PI
    call pipe_spawn_stage32
    pop edi ; body AST
    jc .cap_close_both

    push dword [esp + 16 + PROCESS_INFORMATION.hThread]
    call dword [__imp__CloseHandle@4]
    push dword [esp + 32] ; close parent hWrite so EOF triggers when child finishes!
    call dword [__imp__CloseHandle@4]

    mov ebx, dword [esp + 28] ; hRead
    mov eax, dword [ebp + 20] ; skip_count
    mov dword [esp + 36], eax
    call for_f_read_handle_lines32

    push ebx
    call dword [__imp__CloseHandle@4]
    push INFINITE_WAIT
    push dword [esp + 16 + 4 + PROCESS_INFORMATION.hProcess]
    call dword [__imp__WaitForSingleObject@8]
    push dword [esp + 16 + PROCESS_INFORMATION.hProcess]
    call dword [__imp__CloseHandle@4]
    add esp, 40
    jmp .done

.cap_close_both:
    push dword [esp + 28]
    call dword [__imp__CloseHandle@4]
    push dword [esp + 32]
    call dword [__imp__CloseHandle@4]
.cap_free_stk:
    add esp, 40
    jmp .done

.fileset_mode:
    mov ebx, esi
.fset_lp:
    mov esi, ebx
    call for_next_set_item32
    mov ebx, edx
    test eax, eax
    jz .done
    mov esi, eax
    call tok_strip_quotes32
    jc .done
    mov esi, eax
    mov ecx, GENERIC_READ
    mov edx, OPEN_EXISTING
    xor eax, eax
    call hal_open_file32
    jc .fset_lp
    push ebx
    mov ebx, eax ; hFile
    sub esp, 4
    mov eax, dword [ebp + 20]
    mov dword [esp], eax
    call for_f_read_handle_lines32
    add esp, 4
    push ebx
    call dword [__imp__CloseHandle@4]
    pop ebx
    jmp .fset_lp

.done:
    add esp, 36
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc for_f_read_handle_lines32
;; @abi win32
;; @in EBX = readable Win32 HANDLE, EDI = body AST_NODE32*, EBP = FOR /F options block, [ESP+4] or EAX -> EAX = skip_count
;; @out Streams lines from EBX, skips EAX lines, and invokes for_f_process_line32
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Reads lines from a file or anonymous pipe handle for FOR /F.
@proc for_f_read_handle_lines32
    push ebx
    push esi
    push edi
    sub esp, 16
    mov dword [esp + 8], eax ; skip_remaining

    mov ecx, 8192
    call arena_alloc32
    jc .done
    mov dword [esp + 12], eax ; lineBuf

.line_lp:
    cmp byte [g_exit_flag], 0
    jne .done
    cmp byte [g_goto_pending], 0
    jne .done
    xor esi, esi ; len
.ch_lp:
    mov ecx, esp ; 1-byte read buf at [esp], bytesRead at [esp+4]
    lea edx, [esp + 4]
    mov dword [edx], 0
    push 0
    push edx
    push 1
    push ecx
    push ebx
    call dword [__imp__ReadFile@20]
    test eax, eax
    jz .eof_chk
    cmp dword [esp + 4], 0
    je .eof_chk
    mov al, byte [esp]
    cmp al, 0x1A
    je .eof_chk
    cmp al, 0x0A
    je .got_ln
    cmp al, 0x0D
    je .ch_lp
    cmp esi, 8188
    jae .ch_lp
    mov edx, dword [esp + 12]
    mov byte [edx + esi], al
    inc esi
    jmp .ch_lp

.eof_chk:
    test esi, esi
    jz .done
.got_ln:
    mov edx, dword [esp + 12]
    mov byte [edx + esi], 0
    cmp dword [esp + 8], 0
    je .proc_ln
    dec dword [esp + 8]
    jmp .line_lp
.proc_ln:
    test esi, esi
    jz .line_lp
    mov esi, edx
    call for_f_process_line32
    jmp .line_lp

.done:
    add esp, 16
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc exec_ast_node32
;; @abi win32
;; @in ESI = AST_NODE32*
;; @out EAX = command exit code (stored in g_last_exit_code for executed commands)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Recursively executes any 32-bit Arena AST node (NOP, BINARY, BLOCK, IF, FOR, SIMPLE_CMD).
@proc exec_ast_node32
    push ebx
    push esi
    push edi
    push ebp

    test esi, esi
    jz .done_cur
    cmp byte [g_exit_flag], 0
    jne .done_cur
    cmp byte [g_goto_pending], 0
    jne .done_cur
    cmp dword [g_ctrl_break], 0
    je .dispatch_kind
    mov dword [g_ctrl_break], 0
    mov dword [g_last_exit_code], 0xC000013A ; STATUS_CONTROL_C_EXIT
    jmp .done_cur

.dispatch_kind:
    mov ebp, esi
    movzx eax, byte [ebp + AST_NODE32.bKind]
    cmp al, AST_NOP
    je .done_cur
    cmp al, AST_BINARY
    je .do_binary
    cmp al, AST_BLOCK
    je .do_block
    cmp al, AST_IF
    je .do_if
    cmp al, AST_FOR
    je .do_for
    cmp al, AST_SIMPLE_CMD
    je .do_simple
    jmp .done_cur

.do_binary:
    mov al, byte [ebp + AST_NODE32.bSub]
    cmp al, BINOP_PIPE
    je .do_pipe

    mov esi, dword [ebp + AST_NODE32.pChild1]
    call exec_ast_node32
    cmp byte [g_exit_flag], 0
    jne .done_cur
    cmp byte [g_goto_pending], 0
    jne .done_cur

    mov al, byte [ebp + AST_NODE32.bSub]
    cmp al, BINOP_SEQ
    je .run_rhs
    cmp al, BINOP_AND
    je .chk_and
    ; BINOP_OR: run RHS only when g_last_exit_code != 0
    cmp dword [g_last_exit_code], 0
    je .done_cur
    jmp .run_rhs
.chk_and:
    cmp dword [g_last_exit_code], 0
    jne .done_cur
.run_rhs:
    mov esi, dword [ebp + AST_NODE32.pChild2]
    call exec_ast_node32
    jmp .done_cur

.do_pipe:
    mov esi, ebp
    call exec_pipeline32
    jmp .done_cur

.do_block:
    cmp dword [ebp + AST_NODE32.pRedir], 0
    je .blk_no_redir
    sub esp, 80
    mov edi, esp
    mov esi, dword [ebp + AST_NODE32.pRedir]
    call redir_apply32
    jc .blk_redir_err
    mov esi, dword [ebp + AST_NODE32.pChild1]
    call exec_ast_node32
    mov edi, esp
    call redir_restore32
    add esp, 80
    jmp .done_cur
.blk_redir_err:
    call con_report_err32
    mov dword [g_last_exit_code], 1
    add esp, 80
    jmp .done_cur
.blk_no_redir:
    mov esi, dword [ebp + AST_NODE32.pChild1]
    call exec_ast_node32
    jmp .done_cur

.do_if:
    mov esi, ebp
    call exec_eval_if_cond32
    test eax, eax
    jz .if_else
    mov esi, dword [ebp + AST_NODE32.pChild1]
    call exec_ast_node32
    jmp .done_cur
.if_else:
    mov esi, dword [ebp + AST_NODE32.pChild2]
    test esi, esi
    jz .done_cur
    call exec_ast_node32
    jmp .done_cur

.do_for:
    ; Expand Phase 3 + Phase 4 on FOR set string first!
    mov esi, dword [ebp + AST_NODE32.pStr1]
    call expand_phase3_32
    jc .done_cur
    mov esi, eax
    call expand_phase4_32
    jc .done_cur
    mov edx, eax ; expanded set

    mov al, byte [ebp + AST_NODE32.bSub]
    cmp al, FORMODE_PLAIN
    je .for_plain
    cmp al, FORMODE_DIRS
    je .for_dirs
    cmp al, FORMODE_RECURSE
    je .for_r
    cmp al, FORMODE_NUMERIC
    je .for_l
    mov dword [ebp + AST_NODE32.pStr1], edx
    mov esi, ebp
    call exec_for_f32
    jmp .done_cur

.for_plain:
    mov esi, edx
    mov edi, dword [ebp + AST_NODE32.pChild1]
    mov bl, byte [ebp + AST_NODE32.bVarChar]
    xor bh, bh
    call exec_for_plain_or_dirs32
    jmp .done_cur

.for_dirs:
    mov esi, edx
    mov edi, dword [ebp + AST_NODE32.pChild1]
    mov bl, byte [ebp + AST_NODE32.bVarChar]
    mov bh, 1
    call exec_for_plain_or_dirs32
    jmp .done_cur

.for_r:
    push edx ; setStr
    mov esi, dword [ebp + AST_NODE32.pExtra]
    test esi, esi
    jnz .for_r_norm
    mov ecx, 1024
    call arena_alloc32
    pop edx
    jc .done_cur
    push edx
    mov edi, eax
    mov ecx, 1020
    call hal_get_cwd32
    mov eax, edi
    jmp .for_r_go
.for_r_norm:
    call tok_strip_quotes32
    mov esi, eax
    call path_normalize32
.for_r_go:
    pop edx ; setStr
    mov esi, eax
    mov edi, dword [ebp + AST_NODE32.pChild1]
    movzx ebx, byte [ebp + AST_NODE32.bVarChar]
    call exec_for_walk_r32
    jmp .done_cur

.for_l:
    ; Parse (start, step, end) from EDX
    mov esi, edx
    call for_next_set_item32
    test eax, eax
    jz .done_cur
    mov esi, edx
    push esi
    mov esi, eax
    call exp_parse_signed_int32
    mov ebx, eax ; cur = start
    pop esi

    call for_next_set_item32
    test eax, eax
    jz .done_cur
    mov esi, edx
    push esi
    mov esi, eax
    call exp_parse_signed_int32
    mov edi, eax ; step
    pop esi

    call for_next_set_item32
    test eax, eax
    jz .done_cur
    mov esi, eax
    call exp_parse_signed_int32
    push eax ; [esp] = end

.for_l_lp:
    cmp byte [g_exit_flag], 0
    jne .for_l_end
    cmp byte [g_goto_pending], 0
    jne .for_l_end
    mov ecx, dword [esp] ; end
    test edi, edi
    js .for_l_neg
    cmp ebx, ecx
    jg .for_l_end
    jmp .for_l_body
.for_l_neg:
    cmp ebx, ecx
    jl .for_l_end
.for_l_body:
    sub esp, 24
    push edi
    mov edi, esp
    add edi, 4
    mov eax, ebx
    call fmt_i32_dec
    mov esi, edi
    mov edi, dword [ebp + AST_NODE32.pChild1]
    mov al, byte [ebp + AST_NODE32.bVarChar]
    call for_bind_run_one32
    pop edi
    add esp, 24
    test edi, edi
    jz .for_l_end
    add ebx, edi
    jmp .for_l_lp
.for_l_end:
    pop eax
    jmp .done_cur

.do_simple:
    ; Expand Phase 3 + Phase 4 on simple command line
    mov esi, dword [ebp + AST_NODE32.pStr1]
    call expand_phase3_32
    jc .done_cur
    mov esi, eax
    call expand_phase4_32
    jc .done_cur
    mov ebx, eax ; final command line

    ; Optional batch command echo (when ECHO ON and not @quiet)
    cmp dword [g_batch_depth], 0
    je .apply_s_redir
    cmp byte [g_echo_on], 0
    je .apply_s_redir
    test byte [ebp + AST_NODE32.bFlags], AST_FLAG_QUIET
    jnz .apply_s_redir
    cmp byte [ebx], 0
    je .apply_s_redir
    call cmd_print_prompt32
    mov esi, ebx
    call con_print_z32
    call con_print_crlf32

.apply_s_redir:
    cmp dword [ebp + AST_NODE32.pRedir], 0
    je .run_s_body
    sub esp, 80
    mov edi, esp
    mov esi, dword [ebp + AST_NODE32.pRedir]
    call redir_apply32
    jc .s_redir_err
    mov esi, ebx
    call exec_simple_line32
    mov edi, esp
    call redir_restore32
    add esp, 80
    jmp .done_cur

.s_redir_err:
    call con_report_err32
    mov dword [g_last_exit_code], 1
    add esp, 80
    jmp .done_cur

.run_s_body:
    mov esi, ebx
    call exec_simple_line32

.done_cur:
    mov eax, dword [g_last_exit_code]
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc batch_run_file32
;; @abi win32
;; @in ESI = normalized .bat/.cmd path, EDI = raw argument tail, CL = bIsCallSub (0=file, 1=CALL :label)
;; @out EAX = exit code, CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Pushes a BATCH_FRAME32, streams stateless byte-offset lines + multi-line (...) blocks, and unwinds SETLOCAL.
@proc batch_run_file32
    push ebx
    push esi
    push edi
    push ebp

    mov eax, dword [g_batch_depth]
    cmp eax, W32_BATCH_MAX
    jae .too_deep

    shl eax, 6 ; * 64 (BATCH_FRAME32)
    lea ebx, [g_batch_stack + eax]

    ; Zero frame (64 bytes = 16 dwords)
    push ecx
    push edi
    mov edi, ebx
    xor eax, eax
    mov ecx, 16
    rep stosd
    pop edi
    pop ecx

    mov byte [ebx + BATCH_FRAME32.bIsCallSub], cl
    mov eax, dword [g_setlocal_depth]
    mov byte [ebx + BATCH_FRAME32.bSavedSetlocalDepth], al

    ; Allocate persistent copies of batch path and argument tokens so arena_reset32 never invalidates them!
    call str_len32
    inc ecx
    push ecx
    call perm_alloc32
    pop ecx
    jc .oom
    mov dword [ebx + BATCH_FRAME32.pBatchPath], eax
    mov dword [ebx + BATCH_FRAME32.argvPtrs], eax ; %0
    push edi
    mov edi, eax
    rep movsb
    pop edi

    mov esi, edi
    test esi, esi
    jnz .dup_raw_args
    mov esi, addr(szEmpty32)
.dup_raw_args:
    call prs_skip_ws32
    mov esi, eax
    call str_len32
    inc ecx
    push ecx
    call perm_alloc32
    pop ecx
    jc .oom_free_p
    mov dword [ebx + BATCH_FRAME32.pArgsRaw], eax
    push esi
    mov edi, eax
    rep movsb
    pop esi

    ; Parse up to 9 positional tokens (%1..%9) into persistent buffers
    mov ebp, 1
.arg_lp:
    cmp ebp, 9
    ja .args_done
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .args_done
    ; Also skip ',' / ';' / '=' argument separators!
    cmp byte [esi], ','
    je .skip_asep
    cmp byte [esi], ';'
    je .skip_asep
    cmp byte [esi], '='
    jne .rd_arg
.skip_asep:
    inc esi
    jmp .arg_lp
.rd_arg:
    call prs_read_word32
    jc .args_done
    push edx
    mov esi, eax
    call str_len32
    inc ecx
    push ecx
    call perm_alloc32
    pop ecx
    pop edx
    jc .args_done
    mov dword [ebx + BATCH_FRAME32.argvPtrs + ebp * 4], eax
    mov edi, eax
    rep movsb
    mov esi, edx
    inc ebp
    jmp .arg_lp

.args_done:
    inc dword [g_batch_depth]

    ; Allocate persistent 16 KiB line accumulation buffer for this batch frame
    mov ecx, W32_LINE_MAX
    call perm_alloc32
    jc .pop_frame_oom
    mov ebp, eax ; lineBuf

    ; If invoked via `CALL :label args`, jump immediately to target label!
    cmp byte [ebx + BATCH_FRAME32.bIsCallSub], 0
    je .stmt_loop
    mov esi, addr(g_goto_label)
    call batch_seek_label32
    mov byte [g_goto_pending], 0
    jc .cleanup_frame

.stmt_loop:
    cmp byte [g_exit_flag], 0
    jne .cleanup_frame
    cmp dword [g_ctrl_break], 0
    jne .cleanup_frame

    call arena_reset32

    mov edi, ebp
    mov ecx, W32_LINE_MAX - 8
    call batch_read_logical_line32
    jc .cleanup_frame
    mov esi, eax ; cur_len

    ; Accumulate continuation lines while unquoted '(' depth > 0!
.bal_lp:
    push esi
    mov esi, ebp
    call prs_paren_balance32
    pop esi
    test eax, eax
    jz .expand_and_exec
    mov byte [esi + ebp], 0x0A
    inc esi
    mov byte [esi + ebp], 0
    mov ecx, W32_LINE_MAX - 8
    sub ecx, esi
    cmp ecx, 8
    jbe .expand_and_exec
    lea edi, [esi + ebp]
    call batch_read_logical_line32
    jc .expand_and_exec
    add esi, eax
    jmp .bal_lp

.expand_and_exec:
    mov esi, ebp
    mov ecx, 1 ; bInBatch = 1
    call expand_phase1_32
    jc .stmt_loop
    mov esi, eax
    call parse_line_to_ast32
    jc .syn_err_line
    mov esi, eax
    call exec_ast_node32

    cmp byte [g_batch_ret_flag], 0
    jne .on_exit_b
    cmp byte [g_goto_pending], 0
    je .stmt_loop

    ; Handle pending GOTO :label or GOTO :EOF
    mov byte [g_goto_pending], 0
    mov esi, addr(g_goto_label)
    mov edi, addr(szKwEof)
    call str_eq_i32
    test eax, eax
    jnz .cleanup_frame

    mov esi, addr(g_goto_label)
    call batch_seek_label32
    jnc .stmt_loop
    mov eax, ERR_LABEL_NOT_FOUND
    call con_report_err32
    mov dword [g_last_exit_code], 1
    jmp .cleanup_frame

.syn_err_line:
    call con_report_err32
    mov dword [g_last_exit_code], 1
    jmp .cleanup_frame

.on_exit_b:
    mov byte [g_batch_ret_flag], 0

.cleanup_frame:
    mov eax, ebp
    call perm_free32

    ; Auto-unwind any SETLOCAL levels pushed inside this batch frame!
    movzx esi, byte [ebx + BATCH_FRAME32.bSavedSetlocalDepth]
.unwind_sl:
    cmp dword [g_setlocal_depth], esi
    jbe .free_frame_ptrs
    call env_endlocal_pop32
    jmp .unwind_sl

.free_frame_ptrs:
    mov eax, dword [ebx + BATCH_FRAME32.pBatchPath]
    call perm_free32
    mov eax, dword [ebx + BATCH_FRAME32.pArgsRaw]
    call perm_free32
    mov esi, 1
.fr_ag:
    cmp esi, 9
    ja .dec_depth
    mov eax, dword [ebx + BATCH_FRAME32.argvPtrs + esi * 4]
    call perm_free32
    inc esi
    jmp .fr_ag
.dec_depth:
    dec dword [g_batch_depth]
    mov eax, dword [g_last_exit_code]
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.pop_frame_oom:
    dec dword [g_batch_depth]
.oom_free_p:
    mov eax, dword [ebx + BATCH_FRAME32.pBatchPath]
    call perm_free32
.oom:
.too_deep:
    mov eax, ERR_OUT_OF_MEMORY
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc batch_seek_label32
;; @abi win32
;; @in EBX = BATCH_FRAME32*, ESI = target label name (without leading ':')
;; @out Updates EBX->uFileOffLo to line immediately after matching :label, CF=0; or CF=1 if missing
;; @err CF=1, EAX=ERR_LABEL_NOT_FOUND
;; @clobbers eax, ecx, edx
;; @desc Scans forward from current uFileOffLo then wraps to byte 0 to locate :label (case-insensitive).
@proc batch_seek_label32
    push ebx
    push esi
    push edi
    push ebp

    mov ebp, esi ; target label
    mov ecx, 4096
    call arena_alloc32
    jc .not_found
    mov edi, eax ; scratch line buf

    mov eax, dword [ebx + BATCH_FRAME32.uFileOffLo]
    push eax ; saved start offset [esp]
    xor esi, esi ; pass = 0 (forward), 1 (from 0)

.scan_lp:
    mov ecx, 4090
    call batch_read_raw_line32
    jc .next_pass

    push esi
    mov esi, edi
    call prs_skip_ws32
    cmp byte [eax], ':'
    jne .pop_cont
    inc eax
    cmp byte [eax], ':' ; '::' is a comment, never a GOTO target!
    je .pop_cont
    mov esi, eax
    call prs_skip_ws32
    mov esi, eax
    ; Terminate label token at first whitespace / '+' / ':'
    mov ecx, esi
.lbl_end:
    mov dl, byte [ecx]
    test dl, dl
    jz .cmp_lbl
    cmp dl, 0x20
    je .term_lbl
    cmp dl, 0x09
    je .term_lbl
    inc ecx
    jmp .lbl_end
.term_lbl:
    mov byte [ecx], 0
.cmp_lbl:
    push edi
    mov edi, ebp
    call str_eq_i32
    pop edi
    pop esi
    test eax, eax
    jnz .found
    jmp .scan_lp

.pop_cont:
    pop esi
    jmp .scan_lp

.next_pass:
    test esi, esi
    jnz .not_found_pop
    mov esi, 1
    mov dword [ebx + BATCH_FRAME32.uFileOffLo], 0
    mov byte [ebx + BATCH_FRAME32.bEncoding], 0
    jmp .scan_lp

.found:
    pop eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.not_found_pop:
    pop eax
    mov dword [ebx + BATCH_FRAME32.uFileOffLo], eax
.not_found:
    mov eax, ERR_LABEL_NOT_FOUND
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc resolve_executable32
;; @abi win32
;; @in ESI = program name (quoted or unquoted)
;; @out EAX = resolved existing path in arena, DL = kind (1=.BAT/.CMD, 2=.COM, 3=.EXE/other), CF=0; or CF=1
;; @err CF=1, EAX=ERR_BAD_COMMAND
;; @clobbers eax, ecx, edx
;; @desc Searches explicit path, CWD (unless NoDefaultCurrentDirectoryInExePath), and %PATH% across %PATHEXT%.
@proc resolve_executable32
    push ebx
    push esi
    push edi
    push ebp

    call tok_strip_quotes32
    jc .not_found
    mov ebp, eax ; cleanProg

    ; Check if cleanProg already has an extension ('.' after last '' or '/')
    mov esi, ebp
    xor ebx, ebx ; has_sep (0/1)
    xor edi, edi ; has_ext_ptr
.scan_p:
    mov al, byte [esi]
    test al, al
    jz .scan_p_done
    cmp al, 0x5C
    je .mark_sep
    cmp al, '/'
    je .mark_sep
    cmp al, ':'
    je .mark_sep
    cmp al, '.'
    jne .nx_p
    mov edi, esi
    jmp .nx_p
.mark_sep:
    mov ebx, 1
    xor edi, edi
.nx_p:
    inc esi
    jmp .scan_p

.scan_p_done:
    ; If explicit path separator is present, only probe CWD/explicit path (with PATHEXT if no ext)
    test ebx, ebx
    jnz .probe_here_only

    ; Check %NoDefaultCurrentDirectoryInExePath%: if NOT defined, probe CWD first!
    mov esi, addr(szVarNoDefCwd)
    call env_get_raw32
    test eax, eax
    jnz .search_path_env

    mov esi, ebp
    mov ecx, edi
    call probe_one_candidate_exts32
    jnc .found_out
    jmp .search_path_env

.probe_here_only:
    mov esi, ebp
    mov ecx, edi
    call probe_one_candidate_exts32
    jnc .found_out
    jmp .not_found

.search_path_env:
    mov esi, addr(szVarPath)
    call env_get32
    test eax, eax
    jz .not_found
    mov esi, eax ; PATH cursor

.path_lp:
    cmp byte [esi], 0
    je .not_found
    cmp byte [esi], ';'
    jne .dir_start
    inc esi
    jmp .path_lp
.dir_start:
    mov ebx, esi
.dir_scan:
    mov al, byte [ebx]
    test al, al
    jz .got_dir
    cmp al, ';'
    je .got_dir
    inc ebx
    jmp .dir_scan

.got_dir:
    ; Build "<dir>\<cleanProg>" in arena
    mov ecx, ebx
    sub ecx, esi
    push ebx
    push edi ; has_ext_ptr
    push ecx
    add ecx, 1024
    call arena_alloc32
    pop ecx
    jc .pop2_nf
    mov edx, eax
    mov edi, eax
    rep movsb
    cmp edi, edx
    jbe .app_prog
    cmp byte [edi - 1], 0x5C
    je .app_prog
    cmp byte [edi - 1], '/'
    je .app_prog
    mov byte [edi], 0x5C
    inc edi
.app_prog:
    mov esi, ebp
    call str_copy32
    pop ecx ; has_ext_ptr
    mov esi, edx
    call probe_one_candidate_exts32
    pop ebx
    jnc .found_out
    mov esi, ebx
    jmp .path_lp

.pop2_nf:
    pop edi
    pop ebx
.not_found:
    mov eax, ERR_BAD_COMMAND
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret

.found_out:
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret
@endproc

;; @proc probe_one_candidate_exts32
;; @abi win32
;; @in ESI = base candidate path, ECX = non-zero if ESI already has an extension
;; @out EAX = existing full path, DL = kind (1=.BAT/.CMD, 2=.COM, 3=.EXE/other), CF=0; or CF=1
;; @err CF=1 if not found
;; @clobbers eax, ecx, edx
;; @desc Probes ESI directly (when extension present) or appends each %PATHEXT% entry (.COM;.EXE;.BAT;.CMD).
@proc probe_one_candidate_exts32
    push ebx
    push esi
    push edi
    mov ebx, esi

    test ecx, ecx
    jz .try_pathext

    call probe_file_exists_kind32
    jnc .ok
    jmp .try_pathext

.try_pathext:
    mov esi, addr(szVarPathext)
    call env_get32
    test eax, eax
    jnz .got_pext
    mov eax, addr(szDefPathext32)
.got_pext:
    mov esi, eax

.ext_lp:
    cmp byte [esi], 0
    je .fail
    cmp byte [esi], ';'
    jne .ext_tok
    inc esi
    jmp .ext_lp
.ext_tok:
    mov edi, esi
.ext_sc:
    mov al, byte [edi]
    test al, al
    jz .build_cand
    cmp al, ';'
    je .build_cand
    inc edi
    jmp .ext_sc

.build_cand:
    push esi
    push edi
    mov ecx, 2048
    call arena_alloc32
    pop edi
    pop esi
    jc .fail
    mov edx, eax
    push esi
    push edi
    push edx
    mov edi, edx
    mov esi, ebx
    call str_copy32
    mov edi, eax
    mov esi, dword [esp + 8] ; ext start
    mov ecx, dword [esp + 4] ; ext end
    sub ecx, esi
    rep movsb
    mov byte [edi], 0
    pop esi ; cand path
    call probe_file_exists_kind32
    pop edi
    pop ecx ; discard saved ext start
    jnc .ok
    mov esi, edi
    jmp .ext_lp

.ok:
    pop edi
    pop esi
    pop ebx
    clc
    ret
.fail:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc probe_file_exists_kind32
;; @abi win32
;; @in ESI = candidate file path
;; @out EAX = normalized path, DL = kind (1=.BAT/.CMD, 2=.COM, 3=.EXE/other), CF=0; or CF=1
;; @err CF=1 if missing or is a directory
;; @clobbers eax, ecx, edx
;; @desc Checks file existence (excluding directories) and classifies .BAT/.CMD vs .COM vs .EXE.
@proc probe_file_exists_kind32
    push ebx
    push esi
    push edi

    call hal_get_attr32
    jc .no
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jnz .no

    call path_normalize32
    jc .no
    mov ebx, eax
    mov esi, ebx
    call str_len32
    cmp ecx, 4
    jb .kind_exe

    lea esi, [ebx + ecx - 4]
    mov edi, addr(szExtBat32)
    call str_eq_i32
    test eax, eax
    jnz .kind_bat
    mov edi, addr(szExtCmd32)
    call str_eq_i32
    test eax, eax
    jnz .kind_bat
    mov edi, addr(szExtCom32)
    call str_eq_i32
    test eax, eax
    jnz .kind_com

.kind_exe:
    mov eax, ebx
    mov dl, 3
    jmp .yes
.kind_bat:
    mov eax, ebx
    mov dl, 1
    jmp .yes
.kind_com:
    mov eax, ebx
    mov dl, 2
.yes:
    pop edi
    pop esi
    pop ebx
    clc
    ret
.no:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc exec_external32
;; @abi win32
;; @in ESI = program token, EDI = raw argument tail, ECX = dwCreationFlags (0 for normal synchronous child)
;; @out EAX = child exit code (stored in g_last_exit_code), CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_BAD_COMMAND
;; @clobbers eax, ecx, edx
;; @desc Resolves and launches .BAT/.CMD scripts, .COM/MZ programs (via vm8086 or VDM), and Win32/64 PE executables.
@proc exec_external32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 96
    mov ebp, esp
    mov dword [ebp + 88], ecx ; dwCreationFlags

    call resolve_executable32
    jc .not_found
    mov ebx, eax ; resolved path
    cmp dl, 1
    je .run_batch
    cmp dl, 2
    je .run_vm86
    cmp byte [g_force_vm86], 0
    jne .try_vm86_first

    ; Build full command line: `"<resolved_path>"<raw_tail>`
    mov ecx, W32_LINE_MAX
    call arena_alloc32
    jc .not_found
    mov edx, eax
    push edi
    mov edi, edx
    mov byte [edi], '"'
    inc edi
    mov esi, ebx
    call str_copy32
    mov edi, eax
    mov byte [edi], '"'
    inc edi
    pop esi ; raw_tail
    push esi
    call str_copy32
    pop edi ; raw_tail
    mov esi, edx ; cmdline

    ; Zero STARTUPINFO [ebp+0..68] & PROCESS_INFORMATION [ebp+68..84]
    push edi
    mov edi, ebp
    xor eax, eax
    mov ecx, 21
    rep stosd
    pop edi

    mov dword [ebp + STARTUPINFOW.cb], 68
    mov dword [ebp + STARTUPINFOW.dwFlags], STARTF_USESTDHANDLES
    push esi
    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + STARTUPINFOW.hStdInput], eax
    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + STARTUPINFOW.hStdOutput], eax
    push STD_ERROR_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + STARTUPINFOW.hStdError], eax
    pop esi

    cmp byte [g_is_win9x], 0
    jne .cp_ansi

    push edi
    mov esi, esi
    call wtf8_to_utf16le
    pop edi
    jc .not_found
    mov dword [ebp + 84], eax ; cmdW
    mov esi, ebx
    call ntw_path_to_w32
    jc .not_found
    mov ecx, eax ; appW
    push ecx
    call env_build_unicode_block32
    pop ecx
    jc .not_found

    mov edx, dword [ebp + 88]
    or edx, CREATE_UNICODE_ENVIRONMENT
    lea esi, [ebp + 68]
    push esi              ; lpProcessInformation
    push ebp              ; lpStartupInfo
    push 0                ; lpCurrentDirectory
    push eax              ; lpEnvironment
    push edx              ; dwCreationFlags
    push 1                ; bInheritHandles
    push 0
    push 0
    push dword [ebp + 84] ; lpCommandLine
    push ecx              ; lpApplicationName
    call dword [__imp__CreateProcessW@40]
    test eax, eax
    jnz .wait_child
    jmp .chk_bad_exe_vm86

.cp_ansi:
    lea edx, [ebp + 68]
    push edx
    push ebp
    push 0
    push dword [g_env_buf]
    push dword [ebp + 88]
    push 1
    push 0
    push 0
    push esi
    push ebx
    call dword [__imp__CreateProcessA@40]
    test eax, eax
    jnz .wait_child

.chk_bad_exe_vm86:
    ; On 64-bit Windows / WOW64 / Wine without NTVDM, 16-bit .COM and DOS MZ .EXE fail with
    ; ERROR_BAD_EXE_FORMAT (193) or ERROR_EXE_MACHINE_TYPE_MISMATCH (216) -> run in embedded vm8086!
    call dword [__imp__GetLastError@0]
    cmp eax, 193
    je .run_vm86
    cmp eax, 216
    je .run_vm86
    call map_win32_to_err
    jmp .err_out

.wait_child:
    push dword [ebp + 68 + PROCESS_INFORMATION.hThread]
    call dword [__imp__CloseHandle@4]
    mov ebx, dword [ebp + 68 + PROCESS_INFORMATION.hProcess]
    push INFINITE_WAIT
    push ebx
    call dword [__imp__WaitForSingleObject@8]
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push edx
    push ebx
    call dword [__imp__GetExitCodeProcess@8]
    pop edi
    push ebx
    call dword [__imp__CloseHandle@4]
    mov eax, edi
    mov dword [g_last_exit_code], eax
    jmp .ok

.try_vm86_first:
.run_vm86:
    mov esi, ebx
    call vm86_run_image32
    jc .err_out
    mov dword [g_last_exit_code], eax
    jmp .ok

.run_batch:
    mov esi, ebx
    xor cl, cl
    call batch_run_file32
    jc .err_out
    mov dword [g_last_exit_code], eax

.ok:
    add esp, 96
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.not_found:
    mov eax, ERR_BAD_COMMAND
.err_out:
    mov dword [g_last_exit_code], 9009
    add esp, 96
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc
