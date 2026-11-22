;; @file expand32.inc
;; @isa 386
;; @desc 5-Phase Expander Phases 1 (%, %*, %~0..9, %VAR:a=b%, %VAR:~s,l%), 3 (FOR %%I), and 4 (!VAR! + caret strip).

@cpu 386
@format inc

;; @proc exp_apply_var_expr32
;; @abi win32
;; @in ESI = "NAME" or "NAME:old=new" or "NAME:*old=new" or "NAME:~s[,l]"
;; @out EAX = expanded string in arena (or "" if undefined), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Evaluates %VAR%, %VAR:~start[,len]%, %VAR:old=new%, and %VAR:*old=new% in the statement arena.
@proc exp_apply_var_expr32
    push ebx
    push esi
    push edi
    push ebp

    ; Find ':' separator (if any, skipping leading '=' on hidden =C: vars!)
    mov ebx, esi
    cmp byte [ebx], '='
    jne .scan_colon
    inc ebx
.scan_colon:
    mov al, byte [ebx]
    test al, al
    jz .plain_var
    cmp al, ':'
    je .has_colon
    inc ebx
    jmp .scan_colon

.plain_var:
    call env_get32
    test eax, eax
    jnz .ok
    mov eax, addr(szEmpty32)
    jmp .ok

.has_colon:
    ; Extract base variable name [esi..ebx]
    mov ecx, ebx
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .fail
    mov edi, eax
    push edi
    rep movsb
    mov byte [edi], 0
    pop esi
    call env_get32
    test eax, eax
    jnz .got_base_val
    mov eax, addr(szEmpty32)
    jmp .ok

.got_base_val:
    mov ebp, eax ; base value
    inc ebx ; past ':'
    cmp byte [ebx], '~'
    je .do_substr

    ; --- Substring replacement: :old=new or :*old=new ---
    xor edx, edx
    cmp byte [ebx], '*'
    jne .find_eq
    mov edx, 1 ; star prefix delete-to-match
    inc ebx
.find_eq:
    push edx
    mov esi, ebx
.eq_lp:
    mov al, byte [esi]
    test al, al
    jz .no_eq_pop
    cmp al, '='
    je .got_eq
    inc esi
    jmp .eq_lp
.no_eq_pop:
    pop edx
    mov eax, ebp
    jmp .ok

.got_eq:
    ; Copy old=[ebx..esi], new=[esi+1..]
    mov ecx, esi
    sub ecx, ebx
    test ecx, ecx
    jz .no_eq_pop
    push esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    pop edx ; '=' ptr
    jc .fail_pop1
    mov edi, eax
    push edx
    push edi
    mov esi, ebx
    rep movsb
    mov byte [edi], 0
    pop ebx ; old_pat
    pop esi ; '=' ptr
    inc esi ; new_rep
    pop edx ; is_star

    test edx, edx
    jnz .star_rep

    ; Global case-insensitive replace of ebx (old_pat) with esi (new_rep) inside ebp (base_val)
    mov ecx, W32_LINE_MAX
    call arena_alloc32
    jc .fail
    mov edi, eax
    push eax ; result start
    push esi ; new_rep
    mov esi, ebx
    call str_len32
    mov edx, ecx ; old_len
    pop ecx ; new_rep

.rep_scan:
    cmp byte [ebp], 0
    je .rep_done
    ; Compare ebp[0..edx] with ebx[0..edx] case-insensitively
    push ecx
    push edx
    xor ecx, ecx
.cmp_sub:
    cmp ecx, edx
    jae .sub_matched
    mov al, byte [ecx + ebp]
    test al, al
    jz .sub_nomatch
    call ascii_upper_al
    mov ah, al
    mov al, byte [ebx + ecx]
    call ascii_upper_al
    cmp ah, al
    jne .sub_nomatch
    inc ecx
    jmp .cmp_sub

.sub_matched:
    pop edx
    pop ecx
    add ebp, edx
    push esi
    mov esi, ecx
    call str_copy32
    pop esi
    mov edi, eax
    jmp .rep_scan

.sub_nomatch:
    pop edx
    pop ecx
    mov al, byte [ebp]
    mov byte [edi], al
    inc ebp
    inc edi
    jmp .rep_scan

.rep_done:
    mov byte [edi], 0
    pop eax
    jmp .ok

.star_rep:
    ; :*old=new replaces everything from start of string through first match of old with new!
    push esi ; new_rep
    mov esi, ebx
    call str_len32
    mov edx, ecx ; old_len
    mov esi, ebp ; scan cursor
.st_scan:
    cmp byte [esi], 0
    je .st_nomatch
    xor ecx, ecx
.st_cmp:
    cmp ecx, edx
    jae .st_hit
    mov al, byte [esi + ecx]
    test al, al
    jz .st_nomatch
    call ascii_upper_al
    mov ah, al
    mov al, byte [ebx + ecx]
    call ascii_upper_al
    cmp ah, al
    jne .st_adv
    inc ecx
    jmp .st_cmp
.st_adv:
    inc esi
    jmp .st_scan
.st_hit:
    add esi, edx ; tail after old
    pop ebx ; new_rep
    mov ecx, W32_LINE_MAX
    call arena_alloc32
    jc .fail
    mov edi, eax
    push eax
    push esi
    mov esi, ebx
    call str_copy32
    mov edi, eax
    pop esi
    call str_copy32
    pop eax
    jmp .ok
.st_nomatch:
    pop ebx
    mov eax, ebp
    jmp .ok

.do_substr:
    ; --- Substring slice: :~start[,len] (supports negative start & negative len!) ---
    inc ebx ; past '~'
    mov esi, ebp
    call str_len32
    mov edi, ecx ; total_len

    mov esi, ebx
    call exp_parse_signed_int32
    mov ebx, edx ; next cursor
    ; EAX = start
    test eax, eax
    jns .st_nonneg
    add eax, edi
    jns .st_nonneg
    xor eax, eax
.st_nonneg:
    cmp eax, edi
    jae .empty_res
    mov edx, edi
    sub edx, eax ; default remaining len
    push eax ; start_idx

    cmp byte [ebx], ','
    jne .slice_copy
    lea esi, [ebx + 1]
    push edx
    call exp_parse_signed_int32
    pop edx
    ; EAX = requested len
    test eax, eax
    jns .len_pos
    add eax, edx
    js .empty_pop1
.len_pos:
    cmp eax, edx
    jae .slice_copy
    mov edx, eax

.slice_copy:
    pop ebx ; start_idx
    lea ecx, [edx + 4]
    push edx
    call arena_alloc32
    pop edx
    jc .fail
    mov edi, eax
    push eax
    lea esi, [ebx + ebp]
    mov ecx, edx
    rep movsb
    mov byte [edi], 0
    pop eax
    jmp .ok

.empty_pop1:
    pop eax
.empty_res:
    mov eax, addr(szEmpty32)
.ok:
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fail_pop1:
    pop edx
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc exp_parse_signed_int32
;; @abi win32
;; @in ESI = ASCII signed integer cursor
;; @out EAX = signed 32-bit value, EDX = updated cursor
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Parses an optional +/- sign and decimal/hex/octal integer for %VAR:~s,l% slicing.
@proc exp_parse_signed_int32
    push esi
    xor ecx, ecx
    cmp byte [esi], '-'
    jne .chk_pl
    mov ecx, 1
    inc esi
    jmp .go
.chk_pl:
    cmp byte [esi], '+'
    jne .go
    inc esi
.go:
    push ecx
    call sa32_parse_i32_str
    pop ecx
    jc .zero
    test ecx, ecx
    jz .done
    neg eax
    pop esi
    ret
.zero:
    xor eax, eax
    mov edx, esi
.done:
    pop esi
    ret
@endproc

;; @proc exp_format_tilde32
;; @abi win32
;; @in ESI = raw parameter/variable path value, ECX = modifier bitmask (bit0=f,1=d,2=p,3=n,4=x,5=s,6=a,7=t,8=z)
;; @out EAX = formatted result in statement arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Implements %~0..9 and %%~I path/attribute/timestamp/64-bit-size modifiers.
@proc exp_format_tilde32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx

    ; Strip surrounding double quotes from ESI first
    call arena_strdup32
    jc .fail
    mov ebx, eax
    cmp byte [ebx], '"'
    jne .unq_done
    inc ebx
    mov esi, ebx
    call str_len32
    test ecx, ecx
    jz .unq_done
    cmp byte [ebx + ecx - 1], '"'
    jne .unq_done
    mov byte [ebx + ecx - 1], 0
.unq_done:
    test ebp, ebp
    jnz .has_mods
    mov eax, ebx
    jmp .ok

.has_mods:
    ; Resolve full path (or short 8.3 path when bit 5 's' is set with f/d/p/n/x)
    mov esi, ebx
    test ebp, 0x20
    jz .resolve_full
    call hal_shortpath32
    jnc .got_full
.resolve_full:
    call path_normalize32
    jnc .got_full
    mov eax, ebx
.got_full:
    mov ebx, eax ; canonical path

    mov ecx, 4096
    call arena_alloc32
    jc .fail
    mov edi, eax
    mov byte [edi], 0
    push edi ; output start

    ; Bit 6: 'a' -> "--rahsd--"
    test ebp, 0x40
    jz .chk_tz
    mov esi, ebx
    call hal_get_attr32
    jc .attr_skip
    mov edx, eax
    mov byte [edi], '-'
    test edx, FILE_ATTRIBUTE_DIRECTORY
    jz .a_r
    mov byte [edi], 'd'
.a_r:
    mov byte [edi + 1], '-'
    test edx, FILE_ATTRIBUTE_READONLY
    jz .a_a
    mov byte [edi + 1], 'r'
.a_a:
    mov byte [edi + 2], '-'
    test edx, FILE_ATTRIBUTE_ARCHIVE
    jz .a_h
    mov byte [edi + 2], 'a'
.a_h:
    mov byte [edi + 3], '-'
    test edx, FILE_ATTRIBUTE_HIDDEN
    jz .a_s
    mov byte [edi + 3], 'h'
.a_s:
    mov byte [edi + 4], '-'
    test edx, FILE_ATTRIBUTE_SYSTEM
    jz .a_pad
    mov byte [edi + 4], 's'
.a_pad:
    mov dword [edi + 5], 0x202D2D2D ; "--- "
    add edi, 9
    mov byte [edi], 0
.attr_skip:

.chk_tz:
    ; Bit 7 ('t') or Bit 8 ('z') -> query FIND_ENTRY32
    test ebp, 0x180
    jz .chk_path_parts
    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .fail_pop
    mov esi, ebx
    push edi
    mov edi, eax
    push edi
    call hal_find_first32
    pop esi ; FIND_ENTRY32
    pop edi
    jc .chk_path_parts
    push eax
    call dword [__imp__FindClose@4]

    test ebp, 0x80 ; 't'
    jz .chk_z
    sub esp, 24
    lea eax, [esp + 16] ; local FILETIME
    lea ecx, [esi + FIND_ENTRY32.ftWriteLo]
    push eax
    push ecx
    call dword [__imp__FileTimeToLocalFileTime@8]
    mov edx, esp ; SYSTEMTIME16
    lea eax, [esp + 16]
    push edx
    push eax
    call dword [__imp__FileTimeToSystemTime@8]
    movzx eax, word [esp + SYSTEMTIME16.wYear]
    xor edx, edx
    push esi
    call fmt_u64_plain32
    pop esi
    add edi, ecx
    mov byte [edi], '-'
    inc edi
    movzx eax, word [esp + SYSTEMTIME16.wMonth]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], '-'
    inc edi
    movzx eax, word [esp + SYSTEMTIME16.wDay]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], ' '
    inc edi
    movzx eax, word [esp + SYSTEMTIME16.wHour]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], ':'
    inc edi
    movzx eax, word [esp + SYSTEMTIME16.wMinute]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], ' '
    inc edi
    mov byte [edi], 0
    add esp, 24

.chk_z:
    test ebp, 0x100 ; 'z'
    jz .chk_path_parts
    mov eax, dword [esi + FIND_ENTRY32.nSizeLo]
    mov edx, dword [esi + FIND_ENTRY32.nSizeHi]
    call fmt_u64_plain32
    add edi, ecx
    mov byte [edi], ' '
    inc edi
    mov byte [edi], 0

.chk_path_parts:
    ; Bit 0 ('f') or sole Bit 5 ('s') -> full/short path directly
    test ebp, 0x01
    jnz .emit_full
    cmp ebp, 0x20
    je .emit_full

    test ebp, 0x1E ; d|p|n|x
    jz .finish_trim

    ; Locate last '' and last '.' after last '' inside ebx
    mov esi, ebx
    xor ecx, ecx ; last_slash ptr
    xor edx, edx ; last_dot ptr
.sp_scan:
    mov al, byte [esi]
    test al, al
    jz .sp_done
    cmp al, 0x5C
    jne .sp_dot
    mov ecx, esi
    xor edx, edx
    jmp .sp_nx
.sp_dot:
    cmp al, '.'
    jne .sp_nx
    mov edx, esi
.sp_nx:
    inc esi
    jmp .sp_scan
.sp_done:
    ; Bit 1 ('d'): drive "X:"
    test ebp, 0x02
    jz .part_p
    cmp byte [ebx + 1], ':'
    jne .part_p
    mov al, byte [ebx]
    mov byte [edi], al
    mov byte [edi + 1], ':'
    add edi, 2
    mov byte [edi], 0

.part_p:
    ; Bit 2 ('p'): path from after "X:" up to and including last_slash
    test ebp, 0x04
    jz .part_n
    test ecx, ecx
    jz .part_n
    mov esi, ebx
    cmp byte [esi + 1], ':'
    jne .cp_p_lp
    add esi, 2
.cp_p_lp:
    cmp esi, ecx
    ja .part_n
    mov al, byte [esi]
    mov byte [edi], al
    inc esi
    inc edi
    jmp .cp_p_lp

.part_n:
    ; Bit 3 ('n'): filename after last_slash up to last_dot (or end)
    test ebp, 0x08
    jz .part_x
    mov esi, ebx
    test ecx, ecx
    jz .n_start_ok
    lea esi, [ecx + 1]
.n_start_ok:
.cp_n_lp:
    mov al, byte [esi]
    test al, al
    jz .part_x
    cmp esi, edx
    je .part_x
    mov byte [edi], al
    inc esi
    inc edi
    jmp .cp_n_lp

.part_x:
    ; Bit 4 ('x'): extension starting at last_dot
    test ebp, 0x10
    jz .finish_nul
    test edx, edx
    jz .finish_nul
    mov esi, edx
    call str_copy32
    mov edi, eax
    jmp .finish_trim

.emit_full:
    mov esi, ebx
    call str_copy32
    mov edi, eax

.finish_nul:
    mov byte [edi], 0
.finish_trim:
    pop eax
    ; Trim trailing space if only a/t/z were requested
    mov esi, eax
    call str_len32
    test ecx, ecx
    jz .ok
    test ebp, 0x3F
    jnz .ok
    cmp byte [eax + ecx - 1], ' '
    jne .ok
    mov byte [eax + ecx - 1], 0

.ok:
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fail_pop:
    pop eax
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc exp_parse_tilde_mask32
;; @abi win32
;; @in ESI = cursor right after '~'
;; @out ECX = modifier bitmask, EAX = cursor at parameter digit / FOR var char, CF=0; or CF=1 if invalid
;; @err CF=1 when no valid target char follows modifiers
;; @clobbers eax, ecx, edx
;; @desc Parses f,d,p,n,x,s,a,t,z modifier letters after %~ or %%~.
@proc exp_parse_tilde_mask32
    xor ecx, ecx
    mov edx, esi
.lp:
    mov al, byte [edx]
    test al, al
    jz .bad
    call ascii_upper_al
    cmp al, 'F'
    je .m_f
    cmp al, 'D'
    je .m_d
    cmp al, 'P'
    je .m_p
    cmp al, 'N'
    je .m_n
    cmp al, 'X'
    je .m_x
    cmp al, 'S'
    je .m_s
    cmp al, 'A'
    je .m_a
    cmp al, 'T'
    je .m_t
    cmp al, 'Z'
    je .m_z
    jmp .done_chk
.m_f:
    or ecx, 0x001
    inc edx
    jmp .lp
.m_d:
    or ecx, 0x002
    inc edx
    jmp .lp
.m_p:
    or ecx, 0x004
    inc edx
    jmp .lp
.m_n:
    or ecx, 0x008
    inc edx
    jmp .lp
.m_x:
    or ecx, 0x010
    inc edx
    jmp .lp
.m_s:
    or ecx, 0x020
    inc edx
    jmp .lp
.m_a:
    or ecx, 0x040
    inc edx
    jmp .lp
.m_t:
    or ecx, 0x080
    inc edx
    jmp .lp
.m_z:
    or ecx, 0x100
    inc edx
    jmp .lp

.done_chk:
    ; If final char is a digit '0'..'9', we stopped cleanly!
    mov al, byte [edx]
    cmp al, '0'
    jb .chk_backtrack
    cmp al, '9'
    jbe .ok
    ; Otherwise for FOR variables (e.g. %%~dpF where 'F' was consumed as m_f!), if we consumed at least 1 modifier letter
    ; and current char is non-alpha, step back 1 char when caller is Phase 3!
.chk_backtrack:
    mov eax, edx
    clc
    ret
.ok:
    mov eax, edx
    clc
    ret
.bad:
    stc
    ret
@endproc

;; @proc expand_phase1_32
;; @abi win32
;; @in ESI = raw logical line, ECX = bInBatch (1 inside .bat/.cmd, 0 interactive / /C)
;; @out EAX = Phase-1-expanded string in statement arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Expands %%, %*, %0..%9, %~[mods]0..9, and %VAR...% while preserving %%I for Phase 3.
@proc expand_phase1_32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx ; bInBatch

    mov ecx, W32_LINE_MAX
    call arena_alloc32
    jc .fail
    mov edi, eax
    push eax ; output base

.scan:
    mov al, byte [esi]
    test al, al
    jz .done
    cmp al, '%'
    je .on_pct
    mov byte [edi], al
    inc esi
    inc edi
    jmp .scan

.on_pct:
    mov al, byte [esi + 1]
    test al, al
    jz .lit_pct

    ; Check %%: in batch, if followed by FOR variable or non-%, emit literal '%' (Wait: keep %%V if FOR?
    ; In authentic cmd.exe: Phase 1 turns EVERY '%%' into a SINGLE '%' unconditionally!
    ; Then in Phase 3 (when AST_FOR runs its body), '%I' and '%~fI' are expanded!
    ; Wait: what about interactive 'FOR %I'? In interactive mode, '%I' is NOT expanded by %VAR% because there is no closing '%'!
    ; Let's verify that! YES! In real cmd.exe, Phase 1 turns `%%` -> `%` in batch mode, so `%%I` becomes `%I` in the AST,
    ; and then `AST_FOR` expands `%I` and `%~[mods]I` on each iteration identically for both batch `%%I` and interactive `%I`!)
    cmp al, '%'
    jne .chk_batch_params
    test ebp, ebp
    jz .interactive_dbl_pct
    mov byte [edi], '%'
    add esi, 2
    inc edi
    jmp .scan
.interactive_dbl_pct:
    mov byte [edi], '%'
    mov byte [edi + 1], '%'
    add esi, 2
    add edi, 2
    jmp .scan

.chk_batch_params:
    test ebp, ebp
    jz .chk_env_var

    cmp al, '*'
    jne .chk_tilde_param
    add esi, 2
    call batch_get_star32
    push esi
    mov esi, eax
    call str_copy32
    pop esi
    mov edi, eax
    jmp .scan

.chk_tilde_param:
    cmp al, '~'
    jne .chk_digit_param
    cmp byte [g_extensions_on], 0
    je .chk_digit_param
    push esi
    add esi, 2
    call exp_parse_tilde_mask32
    jc .tilde_revert
    mov dl, byte [eax]
    cmp dl, '0'
    jb .tilde_revert
    cmp dl, '9'
    ja .tilde_revert
    mov ebx, eax ; ptr to digit
    pop edx ; discard saved esi
    movzx eax, dl
    sub eax, '0'
    push ecx ; mask
    call batch_get_arg32
    pop ecx
    mov esi, eax
    call exp_format_tilde32
    jc .fail_pop
    mov esi, eax
    call str_copy32
    mov edi, eax
    lea esi, [ebx + 1]
    jmp .scan

.tilde_revert:
    pop esi

.chk_digit_param:
    mov al, byte [esi + 1]
    cmp al, '0'
    jb .chk_env_var
    cmp al, '9'
    ja .chk_env_var
    movzx eax, al
    sub eax, '0'
    add esi, 2
    push esi
    call batch_get_arg32
    mov esi, eax
    call str_copy32
    pop esi
    mov edi, eax
    jmp .scan

.chk_env_var:
    ; Scan forward from [esi+1] for closing '%' on the same line
    lea ebx, [esi + 1]
.find_close_pct:
    mov al, byte [ebx]
    test al, al
    jz .no_close_pct
    cmp al, '%'
    je .got_close_pct
    inc ebx
    jmp .find_close_pct

.no_close_pct:
    ; In batch mode, an unclosed %VAR drops the '%' or in interactive mode keeps literal '%'!
    ; Wait: in interactive mode `FOR %I IN (...)`, `%I ` has no closing `%`, so keeping literal `%` preserves `%I` for FOR!
    ; In batch mode, `%%I` already turned into `%I` via `.on_pct` above, while lone `%foo` in batch drops!
    test ebp, ebp
    jz .lit_pct
    inc esi
    jmp .scan

.got_close_pct:
    mov ecx, ebx
    sub ecx, esi
    dec ecx ; inner len
    jz .lit_pct
    push ebx
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .fail_pop2
    mov edx, eax
    push edi
    mov edi, edx
    inc esi
    rep movsb
    mov byte [edi], 0
    pop edi
    mov esi, edx
    call exp_apply_var_expr32
    pop ebx
    jc .fail_pop
    mov esi, eax
    call str_copy32
    mov edi, eax
    lea esi, [ebx + 1]
    jmp .scan

.lit_pct:
    mov byte [edi], '%'
    inc esi
    inc edi
    jmp .scan

.done:
    mov byte [edi], 0
    pop eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fail_pop2:
    pop ebx
.fail_pop:
    pop eax
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc batch_get_star32
;; @abi win32
;; @in none (reads top of g_batch_stack)
;; @out EAX = raw argument tail (%*) or ""
;; @err none
;; @clobbers eax, ecx
;; @desc Returns raw %* argument tail of the active batch frame.
@proc batch_get_star32
    mov ecx, dword [g_batch_depth]
    test ecx, ecx
    jz .empty
    dec ecx
    shl ecx, 6 ; * 64 (BATCH_FRAME32)
    mov eax, dword [g_batch_stack + ecx + BATCH_FRAME32.pArgsRaw]
    test eax, eax
    jz .empty
    ret
.empty:
    mov eax, addr(szEmpty32)
    ret
@endproc

;; @proc batch_get_arg32
;; @abi win32
;; @in EAX = requested parameter index (0..9)
;; @out EAX = parameter value string (or "" if out of range)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Returns %0..%9 from active BATCH_FRAME32 accounting for SHIFT [/n] offset.
@proc batch_get_arg32
    mov ecx, dword [g_batch_depth]
    test ecx, ecx
    jz .empty
    dec ecx
    shl ecx, 6
    lea edx, [g_batch_stack + ecx]
    test eax, eax
    jz .get_slot
    movzx ecx, byte [edx + BATCH_FRAME32.bShiftOff]
    add eax, ecx
    cmp eax, 9
    ja .empty
.get_slot:
    mov eax, dword [edx + BATCH_FRAME32.argvPtrs + eax * 4]
    test eax, eax
    jz .empty
    ret
.empty:
    mov eax, addr(szEmpty32)
    ret
@endproc

;; @proc for_lookup_var32
;; @abi win32
;; @in AL = case-sensitive FOR variable character (e.g. 'I', 'a')
;; @out EAX = current bound value pointer, or 0 if unbound
;; @err none
;; @clobbers eax, ecx
;; @desc Searches active FOR scope stack from innermost to outermost for variable AL.
@proc for_lookup_var32
    mov ecx, dword [g_for_depth]
.lp:
    test ecx, ecx
    jz .none
    dec ecx
    cmp byte [g_for_vars + ecx], al
    je .hit
    jmp .lp
.hit:
    mov eax, dword [g_for_vals + ecx * 4]
    ret
.none:
    xor eax, eax
    ret
@endproc

;; @proc expand_phase3_32
;; @abi win32
;; @in ESI = token/command string
;; @out EAX = FOR-variable-expanded string in arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Substitutes active %V and %~[mods]V FOR loop variables (case-sensitively, matching cmd.exe!).
@proc expand_phase3_32
    cmp dword [g_for_depth], 0
    jne .active
    mov eax, esi
    clc
    ret
.active:
    push ebx
    push esi
    push edi

    mov ecx, W32_LINE_MAX
    call arena_alloc32
    jc .fail
    mov edi, eax
    push eax

.lp:
    mov al, byte [esi]
    test al, al
    jz .done
    cmp al, '%'
    je .chk_for
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.chk_for:
    ; Support both %V (post-Phase-1) and %%V (nested/unexpanded)
    mov ebx, esi
    inc ebx
    cmp byte [ebx], '%'
    jne .at_body
    inc ebx
.at_body:
    mov al, byte [ebx]
    test al, al
    jz .lit
    cmp al, '~'
    je .try_tilde

    call for_lookup_var32
    test eax, eax
    jz .lit
    mov esi, eax
    call str_copy32
    mov edi, eax
    lea esi, [ebx + 1]
    jmp .lp

.try_tilde:
    ; Greedily match ~[f|d|p|n|x|s|a|t|z]* followed by an active FOR variable!
    inc ebx
    xor ecx, ecx ; mask
.t_lp:
    mov al, byte [ebx]
    test al, al
    jz .lit
    ; If [ebx] is an active FOR variable AND next char is NOT a valid modifier+FOR-var, or check FOR var at [ebx+1] first!
    push ecx
    mov al, byte [ebx + 1]
    test al, al
    jz .try_cur_for
    call for_lookup_var32
    pop ecx
    test eax, eax
    jnz .consume_mod_first
    jmp .try_cur_for_no_pop

.try_cur_for:
    pop ecx
.try_cur_for_no_pop:
    push ecx
    mov al, byte [ebx]
    call for_lookup_var32
    pop ecx
    test eax, eax
    jnz .got_tilde_for

.consume_mod_first:
    mov al, byte [ebx]
    call ascii_upper_al
    cmp al, 'F'
    je .tf
    cmp al, 'D'
    je .td
    cmp al, 'P'
    je .tp
    cmp al, 'N'
    je .tn
    cmp al, 'X'
    je .tx
    cmp al, 'S'
    je .ts
    cmp al, 'A'
    je .ta
    cmp al, 'T'
    je .tt
    cmp al, 'Z'
    je .tz
    jmp .lit
.tf:
    or ecx, 0x001
    inc ebx
    jmp .t_lp
.td:
    or ecx, 0x002
    inc ebx
    jmp .t_lp
.tp:
    or ecx, 0x004
    inc ebx
    jmp .t_lp
.tn:
    or ecx, 0x008
    inc ebx
    jmp .t_lp
.tx:
    or ecx, 0x010
    inc ebx
    jmp .t_lp
.ts:
    or ecx, 0x020
    inc ebx
    jmp .t_lp
.ta:
    or ecx, 0x040
    inc ebx
    jmp .t_lp
.tt:
    or ecx, 0x080
    inc ebx
    jmp .t_lp
.tz:
    or ecx, 0x100
    inc ebx
    jmp .t_lp

.got_tilde_for:
    mov esi, eax ; bound FOR value
    call exp_format_tilde32
    jc .fail_pop
    mov esi, eax
    call str_copy32
    mov edi, eax
    lea esi, [ebx + 1]
    jmp .lp

.lit:
    mov al, byte [esi]
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.done:
    mov byte [edi], 0
    pop eax
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fail_pop:
    pop eax
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc expand_phase4_32
;; @abi win32
;; @in ESI = Phase-3 string
;; @out EAX = Phase-4 delayed-expanded and caret-unquoted string in arena, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Performs Phase 4 !VAR! delayed expansion (when /V:ON) and unquoted ^ escape stripping.
@proc expand_phase4_32
    push ebx
    push esi
    push edi
    push ebp

    mov ecx, W32_LINE_MAX
    call arena_alloc32
    jc .fail
    mov edi, eax
    push eax
    xor ebp, ebp ; in_quotes flag (0/1)

.lp:
    mov al, byte [esi]
    test al, al
    jz .done
    cmp al, '"'
    jne .chk_caret
    xor ebp, 1
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.chk_caret:
    cmp al, '^'
    jne .chk_bang
    test ebp, ebp
    jnz .chk_quoted_caret_bang
    ; Outside quotes: ^c -> literal c (including ^!)
    mov al, byte [esi + 1]
    test al, al
    jz .inc_done
    mov byte [edi], al
    add esi, 2
    inc edi
    jmp .lp

.chk_quoted_caret_bang:
    ; Inside quotes when /V:ON: ^! -> literal '!'
    cmp byte [g_delayed_exp], 0
    je .emit_raw
    cmp byte [esi + 1], '!'
    jne .emit_raw
    mov byte [edi], '!'
    add esi, 2
    inc edi
    jmp .lp

.chk_bang:
    cmp al, '!'
    jne .emit_raw
    cmp byte [g_delayed_exp], 0
    je .emit_raw

    lea ebx, [esi + 1]
.find_bang:
    mov al, byte [ebx]
    test al, al
    jz .emit_raw
    cmp al, '!'
    je .got_bang
    inc ebx
    jmp .find_bang

.got_bang:
    mov ecx, ebx
    sub ecx, esi
    dec ecx
    jz .skip_empty_bang
    push ebx
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .fail_pop2
    mov edx, eax
    push edi
    mov edi, edx
    inc esi
    rep movsb
    mov byte [edi], 0
    pop edi
    mov esi, edx
    call exp_apply_var_expr32
    pop ebx
    jc .fail_pop
    mov esi, eax
    call str_copy32
    mov edi, eax
    lea esi, [ebx + 1]
    jmp .lp

.skip_empty_bang:
    add esi, 2
    jmp .lp

.emit_raw:
    mov al, byte [esi]
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.inc_done:
    inc esi
.done:
    mov byte [edi], 0
    pop eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fail_pop2:
    pop ebx
.fail_pop:
    pop eax
.fail:
    mov eax, ERR_OUT_OF_MEMORY
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc
