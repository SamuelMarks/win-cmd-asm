;; @file prs32.inc
;; @isa 386
;; @desc Context-sensitive Phase 2 Lexer & 32-Bit Arena AST Parser (blocks, IF, FOR, |, &&, ||, &, FD 0..9 redirs).

@cpu 386
@format inc

;; @proc prs_alloc_node32
;; @abi win32
;; @in AL = bKind (AST_*)
;; @out EAX = zeroed 32-byte AST_NODE32* with bKind=AL, CF=0; or CF=1 on OOM
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Allocates and zero-initializes a 32-byte AST_NODE32 in the statement arena.
@proc prs_alloc_node32
    push edi
    movzx edx, al
    push edx
    mov ecx, AST_NODE32_SIZE
    call arena_alloc32
    pop edx
    jc .fail
    mov edi, eax
    push eax
    xor eax, eax
    mov ecx, 8
    rep stosd
    pop eax
    mov byte [eax + AST_NODE32.bKind], dl
    pop edi
    clc
    ret
.fail:
    pop edi
    stc
    ret
@endproc

;; @proc prs_skip_ws32
;; @abi win32
;; @in ESI = string cursor
;; @out EAX = cursor advanced past spaces, tabs, and standard cmd delimiters (, ; = excluded when caller only skips ws)
;; @err none
;; @clobbers eax
;; @desc Advances cursor past ASCII space (0x20) and horizontal tab (0x09).
@proc prs_skip_ws32
    mov eax, esi
.lp:
    cmp byte [eax], 0x20
    je .adv
    cmp byte [eax], 0x09
    je .adv
    ret
.adv:
    inc eax
    jmp .lp
@endproc

;; @proc prs_paren_balance32
;; @abi win32
;; @in ESI = logical line buffer
;; @out EAX = net unquoted/unescaped '(' minus ')' count (> 0 means multi-line block needs more lines!)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Computes net unquoted parenthesis depth (ignoring REM and :: comment lines!).
@proc prs_paren_balance32
    push esi
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '@'
    jne .chk_col
    inc esi
    call prs_skip_ws32
    mov esi, eax
.chk_col:
    cmp byte [esi], ':'
    je .zero
    ; Check leading REM<ws/eol>
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'R'
    jne .scan_init
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'E'
    jne .scan_init
    mov al, byte [esi + 2]
    call ascii_upper_al
    cmp al, 'M'
    jne .scan_init
    mov al, byte [esi + 3]
    cmp al, 0
    je .zero
    cmp al, 0x20
    je .zero
    cmp al, 0x09
    je .zero

.scan_init:
    xor ecx, ecx ; depth
    xor edx, edx ; in_quote (0/1)
.lp:
    mov al, byte [esi]
    test al, al
    jz .done
    cmp al, '"'
    jne .chk_caret
    xor edx, 1
    inc esi
    jmp .lp
.chk_caret:
    test edx, edx
    jnz .adv
    cmp al, '^'
    jne .chk_par
    cmp byte [esi + 1], 0
    je .done
    add esi, 2
    jmp .lp
.chk_par:
    cmp al, '('
    jne .chk_close
    inc ecx
    jmp .adv
.chk_close:
    cmp al, ')'
    jne .adv
    test ecx, ecx
    jz .adv
    dec ecx
.adv:
    inc esi
    jmp .lp

.zero:
    xor ecx, ecx
.done:
    mov eax, ecx
    pop esi
    ret
@endproc

;; @proc prs_find_top_op32
;; @abi win32
;; @in ESI = start, EDI = end, CL = mode (0 = find lowest '&', 1 = find rightmost '&&'/'||', 2 = find leftmost '|')
;; @out EAX = operator position in [ESI..EDI), DL = BINOP_*, DH = operator char len (1 or 2); or EAX = 0 if none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Scans [ESI..EDI) outside quotes, carets, and nested (...) for binary command operators.
@proc prs_find_top_op32
    push ebx
    push esi
    push edi
    push ebp

    movzx ebp, cl ; mode
    xor ebx, ebx  ; recorded hit (for rightmost '&&'/'||')
    sub esp, 12
    mov dword [esp], 0     ; paren_depth
    mov dword [esp + 4], 0 ; in_quote
    mov dword [esp + 8], 0 ; recorded_op (DL in low byte, DH in byte 1)

    ; Do NOT split inside a REM comment! Check if trimmed start is REM or :label!
    push esi
    call prs_skip_ws32
    cmp byte [eax], '@'
    jne .c_rem0
    inc eax
    mov esi, eax
    call prs_skip_ws32
.c_rem0:
    cmp byte [eax], ':'
    je .abort_rem
    mov dl, byte [eax]
    push eax
    mov al, dl
    call ascii_upper_al
    mov dl, al
    pop eax
    cmp dl, 'R'
    jne .resume_scan
    push eax
    mov al, byte [eax + 1]
    call ascii_upper_al
    mov dl, al
    pop eax
    cmp dl, 'E'
    jne .resume_scan
    push eax
    mov al, byte [eax + 2]
    call ascii_upper_al
    mov dl, al
    pop eax
    cmp dl, 'M'
    jne .resume_scan
    mov dl, byte [eax + 3]
    cmp dl, 0
    je .abort_rem
    cmp dl, 0x20
    je .abort_rem
    cmp dl, 0x09
    je .abort_rem
    jmp .resume_scan
.abort_rem:
    pop esi
    xor eax, eax
    jmp .out

.resume_scan:
    pop esi

.scan_lp:
    cmp esi, edi
    jae .finish
    mov al, byte [esi]
    cmp al, '"'
    jne .s_caret
    xor dword [esp + 4], 1
    inc esi
    jmp .scan_lp

.s_caret:
    cmp dword [esp + 4], 0
    jne .s_adv
    cmp al, '^'
    jne .s_par
    add esi, 2
    jmp .scan_lp

.s_par:
    cmp al, '('
    jne .s_rpar
    inc dword [esp]
    inc esi
    jmp .scan_lp
.s_rpar:
    cmp al, ')'
    jne .s_ops
    cmp dword [esp], 0
    je .s_adv
    dec dword [esp]
    inc esi
    jmp .scan_lp

.s_ops:
    cmp dword [esp], 0
    jne .s_adv
    cmp dword [esp + 4], 0
    jne .s_adv

    cmp al, '&'
    je .on_amp
    cmp al, '|'
    je .on_bar
    cmp al, 0x0A ; multi-line block statement separator!
    je .on_nl
    jmp .s_adv

.on_nl:
    cmp ebp, 0
    jne .s_adv
    mov eax, esi
    mov dl, BINOP_SEQ
    mov dh, 1
    jmp .out

.on_amp:
    ; Ignore >&1 / <&0 handle duplication!
    cmp esi, dword [esp + 20] ; original ESI on stack (esp+12 + edi(0)+esi(4)=16? Wait: push ebx(24),esi(20),edi(16),ebp(12) -> orig esi is [esp+20]!)
    jbe .chk_dbl_amp
    mov ah, byte [esi - 1]
    cmp ah, '>'
    je .s_adv
    cmp ah, '<'
    je .s_adv
.chk_dbl_amp:
    lea eax, [esi + 1]
    cmp eax, edi
    jae .single_amp
    cmp byte [esi + 1], '&'
    jne .single_amp
    ; Found '&&'
    cmp ebp, 1
    jne .skip2
    mov ebx, esi
    mov word [esp + 8], 0x0200 + BINOP_AND ; DH=2, DL=BINOP_AND
.skip2:
    add esi, 2
    jmp .scan_lp

.single_amp:
    cmp ebp, 0
    jne .s_adv
    mov eax, esi
    mov dl, BINOP_SEQ
    mov dh, 1
    jmp .out

.on_bar:
    lea eax, [esi + 1]
    cmp eax, edi
    jae .single_bar
    cmp byte [esi + 1], '|'
    jne .single_bar
    ; Found '||'
    cmp ebp, 1
    jne .skip2
    mov ebx, esi
    mov word [esp + 8], 0x0200 + BINOP_OR ; DH=2, DL=BINOP_OR
    add esi, 2
    jmp .scan_lp

.single_bar:
    cmp ebp, 2
    jne .s_adv
    mov eax, esi
    mov dl, BINOP_PIPE
    mov dh, 1
    jmp .out

.s_adv:
    inc esi
    jmp .scan_lp

.finish:
    mov eax, ebx
    mov dx, word [esp + 8]

.out:
    add esp, 12
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc prs_extract_redirs32
;; @abi win32
;; @in ESI = raw simple command slice (NUL-terminated)
;; @out EAX = cleaned command text in arena, EDX = linked list head of AST_REDIR32*, CF=0
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Extracts arbitrary leading/mid-line/trailing FD 0..9 redirections (<, >, >>, n>&m, n<&m) in left-to-right order.
@proc prs_extract_redirs32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 16
    mov ebp, esp
    mov dword [ebp], 0     ; head AST_REDIR32*
    mov dword [ebp + 4], 0 ; tail AST_REDIR32*
    mov dword [ebp + 8], 0 ; in_quote

    call str_len32
    lea ecx, [ecx + 8]
    call arena_alloc32
    jc .oom
    mov edi, eax
    mov dword [ebp + 12], eax ; cleaned_cmd base

.lp:
    mov al, byte [esi]
    test al, al
    jz .done
    cmp al, '"'
    jne .chk_caret
    xor dword [ebp + 8], 1
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.chk_caret:
    cmp dword [ebp + 8], 0
    jne .copy_ch
    cmp al, '^'
    jne .chk_digit_fd
    mov byte [edi], al
    inc esi
    inc edi
    mov al, byte [esi]
    test al, al
    jz .done
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.chk_digit_fd:
    ; Check explicit digit FD '0'..'9' immediately followed by '<' or '>' (when preceded by start or whitespace!)
    cmp al, '0'
    jb .chk_bare_redir
    cmp al, '9'
    ja .chk_bare_redir
    mov ah, byte [esi + 1]
    cmp ah, '>'
    je .VerifyFdBoundary
    cmp ah, '<'
    jne .copy_ch
.VerifyFdBoundary:
    ; Must be at start of cleaned buffer or preceded by space/tab!
    cmp edi, dword [ebp + 12]
    je .got_explicit_fd
    mov cl, byte [edi - 1]
    cmp cl, 0x20
    je .got_explicit_fd
    cmp cl, 0x09
    jne .copy_ch
.got_explicit_fd:
    movzx ebx, al
    sub ebx, '0' ; bSrcFd = 0..9
    inc esi      ; point at '<' or '>'
    jmp .parse_redir_op

.chk_bare_redir:
    cmp al, '<'
    je .bare_in
    cmp al, '>'
    je .bare_out
    jmp .copy_ch

.bare_in:
    xor ebx, ebx ; default stdin = 0
    jmp .parse_redir_op
.bare_out:
    mov ebx, 1   ; default stdout = 1

.parse_redir_op:
    ; [esi] is '<' or '>'; ebx = bSrcFd
    mov al, byte [esi]
    inc esi
    mov dl, REDIR_IN
    cmp al, '<'
    je .chk_dup
    mov dl, REDIR_OUT_TRUNC
    cmp byte [esi], '>'
    jne .chk_dup
    mov dl, REDIR_OUT_APPEND
    inc esi

.chk_dup:
    cmp byte [esi], '&'
    jne .redir_target_file
    inc esi
    mov al, byte [esi]
    cmp al, '0'
    jb .syn_err
    cmp al, '9'
    ja .syn_err
    sub al, '0'
    inc esi
    mov dh, al ; bTargetFd
    cmp dl, REDIR_IN
    je .set_dupin
    mov dl, REDIR_DUP_OUT
    jmp .alloc_rnode
.set_dupin:
    mov dl, REDIR_DUP_IN
    jmp .alloc_rnode

.redir_target_file:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .syn_err

    ; Scan target filename token (honoring "..." quotes and ^ escapes)
    push edx
    mov ecx, 2048
    call arena_alloc32
    pop edx
    jc .oom
    push edi
    mov edi, eax
    push eax ; targetBuf
    xor ecx, ecx ; quote state
.tf_lp:
    mov al, byte [esi]
    test al, al
    jz .tf_done
    cmp al, '"'
    jne .tf_caret
    xor ecx, 1
    inc esi
    jmp .tf_lp
.tf_caret:
    test ecx, ecx
    jnz .tf_emit
    cmp al, '^'
    jne .tf_delim
    inc esi
    mov al, byte [esi]
    test al, al
    jz .tf_done
    jmp .tf_emit
.tf_delim:
    cmp al, 0x20
    je .tf_done
    cmp al, 0x09
    je .tf_done
    cmp al, '<'
    je .tf_done
    cmp al, '>'
    je .tf_done
    cmp al, '&'
    je .tf_done
    cmp al, '|'
    je .tf_done
    cmp al, ')'
    je .tf_done
.tf_emit:
    mov byte [edi], al
    inc esi
    inc edi
    jmp .tf_lp
.tf_done:
    mov byte [edi], 0
    pop ecx ; targetBuf
    pop edi
    cmp byte [ecx], 0
    je .syn_err
    xor dh, dh
    jmp .alloc_rnode_with_str

.alloc_rnode:
    xor ecx, ecx
.alloc_rnode_with_str:
    push ecx ; pTargetStr
    push edx ; dl=bKind, dh=bTargetFd
    mov ecx, AST_REDIR32_SIZE
    call arena_alloc32
    pop edx
    pop ecx
    jc .oom
    mov byte [eax + AST_REDIR32.bKind], dl
    mov byte [eax + AST_REDIR32.bSrcFd], bl
    mov byte [eax + AST_REDIR32.bTargetFd], dh
    mov dword [eax + AST_REDIR32.pTargetStr], ecx
    mov dword [eax + AST_REDIR32.pNext], 0

    cmp dword [ebp], 0
    jne .link_tail
    mov dword [ebp], eax
    mov dword [ebp + 4], eax
    jmp .lp
.link_tail:
    mov edx, dword [ebp + 4]
    mov dword [edx + AST_REDIR32.pNext], eax
    mov dword [ebp + 4], eax
    jmp .lp

.copy_ch:
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.done:
    mov byte [edi], 0
    mov eax, dword [ebp + 12]
    mov edx, dword [ebp]
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.syn_err:
    mov eax, ERR_SYNTAX
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret

.oom:
    mov eax, ERR_OUT_OF_MEMORY
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc prs_read_word32
;; @abi win32
;; @in ESI = cursor
;; @out EAX = arena token string (retaining quotes if caller wants or unquoted? Keeps quotes intact!), EDX = next cursor, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Reads one whitespace-delimited token (respecting "..." and ^ escapes) from ESI.
@proc prs_read_word32
    push ebx
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    mov ebx, esi
    xor ecx, ecx ; in_quote
.scan:
    mov al, byte [ebx]
    test al, al
    jz .got
    cmp al, '"'
    jne .sc_c
    xor ecx, 1
    inc ebx
    jmp .scan
.sc_c:
    test ecx, ecx
    jnz .sc_adv
    cmp al, '^'
    jne .sc_ws
    cmp byte [ebx + 1], 0
    je .got
    add ebx, 2
    jmp .scan
.sc_ws:
    cmp al, 0x20
    je .got
    cmp al, 0x09
    je .got
.sc_adv:
    inc ebx
    jmp .scan

.got:
    mov ecx, ebx
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .fail
    mov edi, eax
    push eax
    rep movsb
    mov byte [edi], 0
    pop eax
    mov edx, ebx
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

;; @proc prs_parse_stmt_end32
;; @abi win32
;; @in ESI = start of statement/condition arg
;; @out EAX = end pointer (stops at top-level ')' or EOL, or top-level ' ELSE ' when ECX=1)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Scans to end of a sub-statement inside IF / FOR / block, stopping before top-level ELSE when requested.
@proc prs_parse_stmt_end32
    push ebx
    push esi
    mov ebx, ecx ; stop_at_else (0/1)
    xor ecx, ecx ; paren_depth
    xor edx, edx ; in_quote
.lp:
    mov al, byte [esi]
    test al, al
    jz .done
    cmp al, '"'
    jne .c_car
    xor edx, 1
    inc esi
    jmp .lp
.c_car:
    test edx, edx
    jnz .adv
    cmp al, '^'
    jne .c_lp
    cmp byte [esi + 1], 0
    je .done
    add esi, 2
    jmp .lp
.c_lp:
    cmp al, '('
    jne .c_rp
    inc ecx
    inc esi
    jmp .lp
.c_rp:
    cmp al, ')'
    jne .c_else
    test ecx, ecx
    jz .done
    dec ecx
    inc esi
    jmp .lp
.c_else:
    test ebx, ebx
    jz .adv
    test ecx, ecx
    jnz .adv
    cmp al, 0x20
    je .try_else_kw
    cmp al, 0x09
    jne .adv
.try_else_kw:
    push esi
    call prs_skip_ws32
    mov esi, eax
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'E'
    jne .no_else
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'L'
    jne .no_else
    mov al, byte [esi + 2]
    call ascii_upper_al
    cmp al, 'S'
    jne .no_else
    mov al, byte [esi + 3]
    call ascii_upper_al
    cmp al, 'E'
    jne .no_else
    mov al, byte [esi + 4]
    cmp al, 0x20
    je .hit_else
    cmp al, 0x09
    je .hit_else
    cmp al, '('
    je .hit_else
.no_else:
    pop esi
    jmp .adv
.hit_else:
    pop esi
    jmp .done

.adv:
    inc esi
    jmp .lp
.done:
    mov eax, esi
    pop esi
    pop ebx
    ret
@endproc

;; @proc prs_parse_if32
;; @abi win32
;; @in ESI = cursor right after "IF" keyword, BL = quiet flag
;; @out EAX = AST_NODE32* (AST_IF), CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Parses IF [NOT] [/I] (ERRORLEVEL|EXIST|DEFINED|CMDEXTVERSION|==|EQU..GEQ) <then> [ELSE <else>].
@proc prs_parse_if32
    push ebx
    push esi
    push edi
    push ebp

    mov al, AST_IF
    call prs_alloc_node32
    jc .fail
    mov ebp, eax
    or byte [ebp + AST_NODE32.bFlags], bl

    ; Optional /I and NOT (in either order)
.flag_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .chk_not
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'I'
    jne .chk_not
    mov al, byte [esi + 2]
    cmp al, 0x20
    je .set_icase
    cmp al, 0x09
    jne .chk_not
.set_icase:
    or byte [ebp + AST_NODE32.bFlags], IFFLAG_NOCASE
    add esi, 2
    jmp .flag_lp

.chk_not:
    call prs_read_word32
    jc .fail
    mov edi, addr(szKwNot)
    push edx
    push esi
    mov esi, eax
    call str_eq_i32
    pop esi
    pop edx
    test eax, eax
    jz .parse_cond
    or byte [ebp + AST_NODE32.bFlags], IFFLAG_NOT
    mov esi, edx
    jmp .flag_lp

.parse_cond:
    ; Read first word of condition at ESI
    call prs_read_word32
    jc .fail
    mov ebx, eax ; word1
    mov edi, edx ; cursor after word1

    ; Check ERRORLEVEL, EXIST, DEFINED, CMDEXTVERSION
    push esi
    mov esi, ebx
    push edi
    mov edi, addr(szKwErrorlevel)
    call str_eq_i32
    pop edi
    pop esi
    test eax, eax
    jnz .if_errlvl

    push esi
    mov esi, ebx
    push edi
    mov edi, addr(szKwExist)
    call str_eq_i32
    pop edi
    pop esi
    test eax, eax
    jnz .if_exist

    push esi
    mov esi, ebx
    push edi
    mov edi, addr(szKwDefined)
    call str_eq_i32
    pop edi
    pop esi
    test eax, eax
    jnz .if_defined

    push esi
    mov esi, ebx
    push edi
    mov edi, addr(szKwCmdExtVer)
    call str_eq_i32
    pop edi
    pop esi
    test eax, eax
    jnz .if_extver

    ; Check if word1 itself contains "==" (e.g. `IF "%A%"=="1"` without spaces around `==`!)
    mov esi, ebx
.scan_inline_eq:
    mov al, byte [esi]
    test al, al
    jz .two_tok_cmp
    cmp al, '='
    jne .adv_ineq
    cmp byte [esi + 1], '='
    je .split_inline_eq
.adv_ineq:
    inc esi
    jmp .scan_inline_eq

.split_inline_eq:
    mov byte [esi], 0
    mov dword [ebp + AST_NODE32.pStr1], ebx
    mov byte [ebp + AST_NODE32.bSub], IFCMP_STR_EQ
    add esi, 2
    cmp byte [esi], 0
    jne .inline_has_rhs
    ; RHS was separated by space (`IF a== b`)
    mov esi, edi
    call prs_read_word32
    jc .fail
    mov dword [ebp + AST_NODE32.pStr2], eax
    mov esi, edx
    jmp .parse_then
.inline_has_rhs:
    call arena_strdup32
    jc .fail
    mov dword [ebp + AST_NODE32.pStr2], eax
    mov esi, edi
    jmp .parse_then

.two_tok_cmp:
    ; Word1 is LHS; read operator token (`==` or `EQU`..`GEQ`) then RHS token!
    mov dword [ebp + AST_NODE32.pStr1], ebx
    mov esi, edi
    call prs_read_word32
    jc .fail
    mov ebx, eax ; op word
    mov esi, edx ; cursor after op

    ; Check if op starts with "==" (e.g. `IF a ==b` or `IF a == b`)
    cmp byte [ebx], '='
    jne .chk_ext_ops
    cmp byte [ebx + 1], '='
    jne .syn_err
    mov byte [ebp + AST_NODE32.bSub], IFCMP_STR_EQ
    cmp byte [ebx + 2], 0
    je .read_rhs_word
    lea esi, [ebx + 2]
    push edx
    call arena_strdup32
    pop esi
    jc .fail
    mov dword [ebp + AST_NODE32.pStr2], eax
    jmp .parse_then

.chk_ext_ops:
    push esi
    mov esi, ebx
    mov edi, addr(szKwEqu)
    call str_eq_i32
    test eax, eax
    jnz .op_equ
    mov edi, addr(szKwNeq)
    call str_eq_i32
    test eax, eax
    jnz .op_neq
    mov edi, addr(szKwLss)
    call str_eq_i32
    test eax, eax
    jnz .op_lss
    mov edi, addr(szKwLeq)
    call str_eq_i32
    test eax, eax
    jnz .op_leq
    mov edi, addr(szKwGtr)
    call str_eq_i32
    test eax, eax
    jnz .op_gtr
    mov edi, addr(szKwGeq)
    call str_eq_i32
    test eax, eax
    jnz .op_geq
    pop esi
    jmp .syn_err

.op_equ:
    mov dl, IFCMP_EQU
    jmp .got_ext_op
.op_neq:
    mov dl, IFCMP_NEQ
    jmp .got_ext_op
.op_lss:
    mov dl, IFCMP_LSS
    jmp .got_ext_op
.op_leq:
    mov dl, IFCMP_LEQ
    jmp .got_ext_op
.op_gtr:
    mov dl, IFCMP_GTR
    jmp .got_ext_op
.op_geq:
    mov dl, IFCMP_GEQ
.got_ext_op:
    pop esi
    mov byte [ebp + AST_NODE32.bSub], dl
.read_rhs_word:
    call prs_read_word32
    jc .fail
    mov dword [ebp + AST_NODE32.pStr2], eax
    mov esi, edx
    jmp .parse_then

.if_errlvl:
    mov byte [ebp + AST_NODE32.bSub], IFCMP_ERRORLEVEL
    jmp .unary_if_arg
.if_exist:
    mov byte [ebp + AST_NODE32.bSub], IFCMP_EXIST
    jmp .unary_if_arg
.if_defined:
    mov byte [ebp + AST_NODE32.bSub], IFCMP_DEFINED
    jmp .unary_if_arg
.if_extver:
    mov byte [ebp + AST_NODE32.bSub], IFCMP_CMDEXTVER
.unary_if_arg:
    mov esi, edi
    call prs_read_word32
    jc .fail
    mov dword [ebp + AST_NODE32.pStr1], eax
    mov esi, edx

.parse_then:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .syn_err

    ; If THEN branch is parenthesized `( ... )`, find matching `)` then check for optional `ELSE`!
    cmp byte [esi], '('
    jne .unparen_then
    mov ebx, esi
    xor ecx, ecx
    xor edx, edx
.bal_then:
    mov al, byte [ebx]
    test al, al
    jz .syn_err
    cmp al, '"'
    jne .bt_car
    xor edx, 1
    inc ebx
    jmp .bal_then
.bt_car:
    test edx, edx
    jnz .bt_adv
    cmp al, '^'
    jne .bt_lp
    cmp byte [ebx + 1], 0
    je .syn_err
    add ebx, 2
    jmp .bal_then
.bt_lp:
    cmp al, '('
    jne .bt_rp
    inc ecx
    inc ebx
    jmp .bal_then
.bt_rp:
    cmp al, ')'
    jne .bt_adv
    dec ecx
    inc ebx
    test ecx, ecx
    jz .got_paren_then
    jmp .bal_then
.bt_adv:
    inc ebx
    jmp .bal_then

.got_paren_then:
    mov edi, ebx
    call prs_parse_slice32
    jc .fail
    mov dword [ebp + AST_NODE32.pChild1], eax
    mov esi, ebx
    jmp .check_else

.unparen_then:
    mov ecx, 1 ; stop before top-level ELSE!
    call prs_parse_stmt_end32
    mov edi, eax
    mov ebx, eax
    call prs_parse_slice32
    jc .fail
    mov dword [ebp + AST_NODE32.pChild1], eax
    mov esi, ebx

.check_else:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .ok
    call prs_read_word32
    jc .fail
    mov edi, addr(szKwElse)
    push edx
    push esi
    mov esi, eax
    call str_eq_i32
    pop esi
    pop edx
    test eax, eax
    jz .ok
    mov esi, edx
    call prs_skip_ws32
    mov esi, eax
    call str_len32
    lea edi, [esi + ecx]
    call prs_parse_slice32
    jc .fail
    mov dword [ebp + AST_NODE32.pChild2], eax

.ok:
    mov eax, ebp
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.syn_err:
    mov eax, ERR_SYNTAX
    stc
.fail:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc prs_parse_for32
;; @abi win32
;; @in ESI = cursor right after "FOR" keyword, BL = quiet flag
;; @out EAX = AST_NODE32* (AST_FOR), CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Parses FOR [/D | /R [root] | /L | /F ["opts"]] %V IN (set) DO <body>.
@proc prs_parse_for32
    push ebx
    push esi
    push edi
    push ebp

    mov al, AST_FOR
    call prs_alloc_node32
    jc .fail
    mov ebp, eax
    or byte [ebp + AST_NODE32.bFlags], bl
    mov byte [ebp + AST_NODE32.bSub], FORMODE_PLAIN

.sw_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .read_var
    mov al, byte [esi + 1]
    call ascii_upper_al
    add esi, 2
    cmp al, 'D'
    je .sw_d
    cmp al, 'L'
    je .sw_l
    cmp al, 'R'
    je .sw_r
    cmp al, 'F'
    je .sw_f
    jmp .syn_err

.sw_d:
    mov byte [ebp + AST_NODE32.bSub], FORMODE_DIRS
    jmp .sw_lp
.sw_l:
    mov byte [ebp + AST_NODE32.bSub], FORMODE_NUMERIC
    jmp .sw_lp
.sw_r:
    mov byte [ebp + AST_NODE32.bSub], FORMODE_RECURSE
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '%'
    je .read_var
    call prs_read_word32
    jc .fail
    mov dword [ebp + AST_NODE32.pExtra], eax ; optional root dir for FOR /R
    mov esi, edx
    jmp .read_var

.sw_f:
    mov byte [ebp + AST_NODE32.bSub], FORMODE_PARSE
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '%'
    je .read_var
    ; Optional "options" string before %V
    call prs_read_word32
    jc .fail
    mov dword [ebp + AST_NODE32.pExtra], eax
    mov esi, edx

.read_var:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '%'
    jne .syn_err
    inc esi
    cmp byte [esi], '%'
    jne .got_vch
    inc esi
.got_vch:
    mov al, byte [esi]
    test al, al
    jz .syn_err
    mov byte [ebp + AST_NODE32.bVarChar], al
    inc esi

    ; Expect IN
    call prs_read_word32
    jc .fail
    mov edi, addr(szKwIn)
    push edx
    push esi
    mov esi, eax
    call str_eq_i32
    pop esi
    pop edx
    test eax, eax
    jz .syn_err
    mov esi, edx

    ; Expect '(' ... ')' set
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '('
    jne .syn_err
    inc esi
    mov ebx, esi
    mov ecx, 1
    xor edx, edx ; quote
.set_lp:
    mov al, byte [ebx]
    test al, al
    jz .syn_err
    cmp al, '"'
    jne .st_rp
    xor edx, 1
    inc ebx
    jmp .set_lp
.st_rp:
    test edx, edx
    jnz .st_adv
    cmp al, '('
    jne .st_cl
    inc ecx
    inc ebx
    jmp .set_lp
.st_cl:
    cmp al, ')'
    jne .st_adv
    dec ecx
    jz .got_set
.st_adv:
    inc ebx
    jmp .set_lp

.got_set:
    mov ecx, ebx
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .fail
    mov edi, eax
    mov dword [ebp + AST_NODE32.pStr1], eax ; raw inside-(...) set
    rep movsb
    mov byte [edi], 0
    lea esi, [ebx + 1] ; past ')'

    ; Expect DO
    call prs_read_word32
    jc .fail
    mov edi, addr(szKwDo)
    push edx
    push esi
    mov esi, eax
    call str_eq_i32
    pop esi
    pop edx
    test eax, eax
    jz .syn_err
    mov esi, edx

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .syn_err
    call str_len32
    lea edi, [esi + ecx]
    call prs_parse_slice32
    jc .fail
    mov dword [ebp + AST_NODE32.pChild1], eax

    mov eax, ebp
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.syn_err:
    mov eax, ERR_SYNTAX
    stc
.fail:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc prs_parse_slice32
;; @abi win32
;; @in ESI = slice start, EDI = slice end
;; @out EAX = AST_NODE32*, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_SYNTAX / ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Recursive-descent Phase 2 AST builder over [ESI..EDI) for &, &&/||, |, (...), IF, FOR, and simple commands.
@proc prs_parse_slice32
    push ebx
    push esi
    push edi
    push ebp

    ; Trim leading/trailing spaces, tabs, CR, LF
.trim_l:
    cmp esi, edi
    jae .empty_nop
    mov al, byte [esi]
    cmp al, 0x20
    je .adv_l
    cmp al, 0x09
    je .adv_l
    cmp al, 0x0D
    je .adv_l
    cmp al, 0x0A
    jne .trim_r
.adv_l:
    inc esi
    jmp .trim_l

.trim_r:
    cmp edi, esi
    jbe .empty_nop
    mov al, byte [edi - 1]
    cmp al, 0x20
    je .dec_r
    cmp al, 0x09
    je .dec_r
    cmp al, 0x0D
    je .dec_r
    cmp al, 0x0A
    jne .check_binops
.dec_r:
    dec edi
    jmp .trim_r

.check_binops:
    ; Tier 1 (lowest): '&' or LF (BINOP_SEQ)
    mov cl, 0
    call prs_find_top_op32
    test eax, eax
    jnz .build_binop

    ; Tier 2: '&&' / '||' (left-associative -> split at rightmost!)
    mov cl, 1
    call prs_find_top_op32
    test eax, eax
    jnz .build_binop

    ; Tier 3: '|' (pipeline)
    mov cl, 2
    call prs_find_top_op32
    test eax, eax
    jnz .build_binop

    ; Copy [esi..edi) into NUL-terminated arena slice
    mov ecx, edi
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .fail
    mov ebx, eax
    push edi
    mov edi, eax
    rep movsb
    mov byte [edi], 0
    pop edi
    mov esi, ebx

    ; Check leading '@' echo suppression
    xor ebx, ebx ; quiet flag
.at_lp:
    cmp byte [esi], '@'
    jne .after_at
    mov bl, AST_FLAG_QUIET
    inc esi
    call prs_skip_ws32
    mov esi, eax
    jmp .at_lp

.after_at:
    cmp byte [esi], 0
    je .empty_nop

    ; ':label' or '::' comment -> AST_NOP
    cmp byte [esi], ':'
    je .empty_nop

    ; Check parenthesized block `( ... ) [redirs]`
    cmp byte [esi], '('
    je .parse_block

    ; Check REM comment (before redirection extraction — REM never evaluates > or <!)
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'R'
    jne .chk_if_for
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'E'
    jne .chk_if_for
    mov al, byte [esi + 2]
    call ascii_upper_al
    cmp al, 'M'
    jne .chk_if_for
    mov al, byte [esi + 3]
    cmp al, 0
    je .empty_nop
    cmp al, 0x20
    je .empty_nop
    cmp al, 0x09
    je .empty_nop
    cmp al, '.'
    je .empty_nop
    cmp al, '/'
    je .empty_nop

.chk_if_for:
    ; Check IF<ws>
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'I'
    jne .chk_for_kw
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'F'
    jne .chk_for_kw
    mov al, byte [esi + 2]
    cmp al, 0x20
    je .go_if
    cmp al, 0x09
    je .go_if
    cmp al, '/'
    jne .chk_for_kw
.go_if:
    add esi, 2
    call prs_parse_if32
    jmp .out

.chk_for_kw:
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'F'
    jne .simple_cmd
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'O'
    jne .simple_cmd
    mov al, byte [esi + 2]
    call ascii_upper_al
    cmp al, 'R'
    jne .simple_cmd
    mov al, byte [esi + 3]
    cmp al, 0x20
    je .go_for
    cmp al, 0x09
    je .go_for
    cmp al, '/'
    jne .simple_cmd
.go_for:
    add esi, 3
    call prs_parse_for32
    jmp .out

.parse_block:
    ; Find matching ')' for '(' at [esi], then parse trailing redirections!
    lea ecx, [esi + 1]
    mov edx, 1 ; depth
    xor eax, eax ; quote
.blk_scan:
    mov bh, byte [ecx]
    test bh, bh
    jz .syn_err
    cmp bh, '"'
    jne .bk_lp
    xor al, 1
    inc ecx
    jmp .blk_scan
.bk_lp:
    test al, al
    jnz .bk_adv
    cmp bh, '('
    jne .bk_rp
    inc edx
    inc ecx
    jmp .blk_scan
.bk_rp:
    cmp bh, ')'
    jne .bk_adv
    dec edx
    jz .got_blk_end
.bk_adv:
    inc ecx
    jmp .blk_scan

.got_blk_end:
    ; Inner slice is [esi+1 .. ecx); trailing redirs at [ecx+1..]
    push ecx ; ')' ptr
    mov al, AST_BLOCK
    call prs_alloc_node32
    pop ecx
    jc .fail
    mov ebp, eax
    and bl, AST_FLAG_QUIET
    or byte [ebp + AST_NODE32.bFlags], bl

    push ecx
    inc esi
    mov edi, ecx
    call prs_parse_slice32
    pop ecx
    jc .fail
    mov dword [ebp + AST_NODE32.pChild1], eax

    lea esi, [ecx + 1]
    call prs_extract_redirs32
    jc .fail
    mov dword [ebp + AST_NODE32.pRedir], edx
    mov eax, ebp
    clc
    jmp .out

.simple_cmd:
    ; Save raw slice in pStr2 (used when reconstructing pipeline child stage commands!)
    mov ebp, esi
    call prs_extract_redirs32
    jc .fail
    mov edi, edx ; pRedir
    mov esi, eax
    call prs_skip_ws32
    mov esi, eax

    mov al, AST_SIMPLE_CMD
    call prs_alloc_node32
    jc .fail
    and bl, AST_FLAG_QUIET
    or byte [eax + AST_NODE32.bFlags], bl
    mov dword [eax + AST_NODE32.pStr1], esi ; cleaned command + args
    mov dword [eax + AST_NODE32.pStr2], ebp ; raw slice with redirs
    mov dword [eax + AST_NODE32.pRedir], edi
    clc
    jmp .out

.build_binop:
    ; EAX = op_pos, DL = BINOP_*, DH = op_len (1 or 2)
    mov ebx, eax ; op_pos
    push edx
    mov al, AST_BINARY
    call prs_alloc_node32
    pop edx
    jc .fail
    mov ebp, eax
    mov byte [ebp + AST_NODE32.bSub], dl

    ; Record full raw [esi..edi) slice in pStr1 so pipeline stages can spawn compound subtrees cleanly!
    push edx
    mov ecx, edi
    sub ecx, esi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    pop edx
    jc .fail
    mov dword [ebp + AST_NODE32.pStr1], eax
    push esi
    push edi
    mov edi, eax
    rep movsb
    mov byte [edi], 0
    pop edi
    pop esi

    ; Parse LHS [esi .. ebx)
    push edx
    push edi
    mov edi, ebx
    call prs_parse_slice32
    pop edi
    pop edx
    jc .fail
    mov dword [ebp + AST_NODE32.pChild1], eax

    ; Parse RHS [ebx + dh .. edi)
    movzx ecx, dh
    lea esi, [ebx + ecx]
    call prs_parse_slice32
    jc .fail
    mov dword [ebp + AST_NODE32.pChild2], eax
    mov eax, ebp
    clc
    jmp .out

.empty_nop:
    mov al, AST_NOP
    call prs_alloc_node32
    jmp .out

.syn_err:
    mov eax, ERR_SYNTAX
    stc
.fail:
.out:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc parse_line_to_ast32
;; @abi win32
;; @in ESI = NUL-terminated Phase-1-expanded command line
;; @out EAX = root AST_NODE32*, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Entry point parsing a complete logical line or multi-line compound block into a 32-bit Arena AST.
@proc parse_line_to_ast32
    push edi
    call str_len32
    lea edi, [esi + ecx]
    call prs_parse_slice32
    pop edi
    ret
@endproc
