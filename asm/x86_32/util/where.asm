;; @file where.asm
;; @isa 386
;; @desc Standalone IA-32 WHERE.EXE executable locator ([/R dir] [/Q] [/F] [/T] pattern / $ENV:pattern).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc where_emit_hit
;; @abi win32
;; @in ESI = full matched path UTF-8, EDX:EAX = 64-bit file size
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Records hit and prints path (honoring /Q quiet, /F quotes, and /T size).
@proc where_emit_hit
    push ebx
    push esi
    mov dword [g_wh_found], 1
    cmp dword [g_wh_quiet], 0
    jne .dn
    cmp dword [g_wh_time], 0
    je .chk_q
    push esi
    call u32_print_u64
    mov esi, addr(szWhPad)
    call u32_write_str
    pop esi
.chk_q:
    cmp dword [g_wh_quote], 0
    je .plain
    mov al, '"'
    call u32_write_chr
    call u32_write_str
    mov al, '"'
    call u32_write_chr
    call u32_write_crlf
    jmp .dn
.plain:
    call u32_write_str
    call u32_write_crlf
.dn:
    pop esi
    pop ebx
    ret
@endproc

;; @proc where_probe_one
;; @abi win32
;; @in ESI = candidate path or wildcard UTF-8, EDI = parent dir UTF-8
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Enumerates matches for candidate ESI in directory EDI (including AppExecLink reparse files).
@proc where_probe_one
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 1088
    mov dword [esp + 1064], edi ; parent dir

    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .dn
    mov ebp, eax

.lp:
    test dword [esp], 0x10
    jnz .nx
    mov esi, dword [esp + 1064]
    lea edi, [esp + 20]
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov eax, dword [esp + 16]
    mov edx, dword [esp + 12]
    call where_emit_hit
.nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .lp
    push ebp
    call dword [__imp__FindClose@4]
.dn:
    add esp, 1088
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc where_probe_dir
;; @abi win32
;; @in ESI = directory UTF-8, EDI = pattern UTF-8
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Probes exact pattern EDI and PATHEXT suffixes (.COM, .EXE, .BAT, .CMD) inside directory ESI.
@proc where_probe_dir
    push ebx
    push esi
    push edi
    sub esp, 1056
    mov dword [esp + 1028], esi ; dir
    mov dword [esp + 1032], edi ; pat

    ; Check if pattern already has an extension dot
    xor ecx, ecx
    xor edx, edx
.chk_dot:
    mov al, byte [edi + ecx]
    test al, al
    jz .probe_exact
    cmp al, '.'
    jne .nx_c
    mov edx, 1
.nx_c:
    inc ecx
    jmp .chk_dot

.probe_exact:
    mov dword [esp + 1036], edx
    mov ebx, esp
    call u32_path_join
    mov esi, esp
    mov edi, dword [esp + 1028]
    call where_probe_one

    cmp dword [esp + 1036], 0
    jne .dn

    ; Try .COM, .EXE, .BAT, .CMD
    mov esi, dword [esp + 1028]
    mov edi, dword [esp + 1032]
    mov ebx, esp
    call u32_path_join
    mov esi, esp
    call u32_strlen
    mov dword [esp + 1040], eax

    lea edi, [esp + eax]
    mov esi, addr(szWhCom)
    call u32_strcpy
    mov esi, esp
    mov edi, dword [esp + 1028]
    call where_probe_one

    mov eax, dword [esp + 1040]
    lea edi, [esp + eax]
    mov esi, addr(szWhExe)
    call u32_strcpy
    mov esi, esp
    mov edi, dword [esp + 1028]
    call where_probe_one

    mov eax, dword [esp + 1040]
    lea edi, [esp + eax]
    mov esi, addr(szWhBat)
    call u32_strcpy
    mov esi, esp
    mov edi, dword [esp + 1028]
    call where_probe_one

    mov eax, dword [esp + 1040]
    lea edi, [esp + eax]
    mov esi, addr(szWhCmd)
    call u32_strcpy
    mov esi, esp
    mov edi, dword [esp + 1028]
    call where_probe_one

.dn:
    add esp, 1056
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc where_recurse
;; @abi win32
;; @in ESI = directory UTF-8, EDI = pattern UTF-8, ECX = depth
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Recursively searches directory tree (/R dir) for pattern EDI.
@proc where_recurse
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 1600
    cmp ecx, 16
    jae .dn
    mov dword [esp + 1568], edi
    mov dword [esp + 1572], ecx
    lea edi, [esp + 1056]
    call u32_strcpy

    lea esi, [esp + 1056]
    mov edi, dword [esp + 1568]
    call where_probe_dir

    lea esi, [esp + 1056]
    mov edi, addr(szWhStar)
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
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
    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov edi, dword [esp + 1568]
    mov ecx, dword [esp + 1572]
    inc ecx
    call where_recurse
.nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .sub_lp
    push ebp
    call dword [__imp__FindClose@4]
.dn:
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
;; @out Terminates via ExitProcess (0=found, 1=not found, 2=error)
;; @err Exits 2 on missing pattern
;; @clobbers eax, ecx, edx
;; @desc Parses WHERE switches (/R, /Q, /F, /T, $ENV:pattern) and locates matching files.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_wh_quiet], 0
    mov dword [g_wh_quote], 0
    mov dword [g_wh_time], 0
    mov dword [g_wh_rec], 0
    mov dword [g_wh_found], 0
    mov byte [g_wh_pat], 0
    mov byte [g_wh_rdir], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pat_arg
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'Q'
    je .sw_q
    cmp al, 'F'
    je .sw_f
    cmp al, 'T'
    je .sw_t
    cmp al, 'R'
    je .sw_r
    jmp .bad_syn
.sw_q:
    mov dword [g_wh_quiet], 1
    jmp .arg_lp
.sw_f:
    mov dword [g_wh_quote], 1
    jmp .arg_lp
.sw_t:
    mov dword [g_wh_time], 1
    jmp .arg_lp
.sw_r:
    mov dword [g_wh_rec], 1
    mov edi, addr(g_wh_rdir)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.pat_arg:
    mov edi, addr(g_wh_pat)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szWhHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_syn:
    mov esi, addr(szWhSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 2
    call u32_exit
    jmp .done

.run:
    cmp byte [g_wh_pat], 0
    je .bad_syn

    cmp dword [g_wh_rec], 0
    je .chk_env_pat
    mov esi, addr(g_wh_rdir)
    mov edi, addr(g_wh_pat)
    xor ecx, ecx
    call where_recurse
    jmp .finish

.chk_env_pat:
    ; Check "$ENV:pattern" syntax
    cmp byte [g_wh_pat], '$'
    jne .std_path_search
    xor ecx, ecx
.find_col:
    mov al, byte [g_wh_pat + 1 + ecx]
    test al, al
    jz .std_path_search
    cmp al, ':'
    je .got_env_col
    mov byte [g_u32_arg_a + ecx], al
    inc ecx
    jmp .find_col
.got_env_col:
    mov byte [g_u32_arg_a + ecx], 0
    lea esi, [g_wh_pat + 2 + ecx]
    mov edi, addr(g_u32_arg_b)
    call u32_strcpy
    mov esi, addr(g_u32_arg_b)
    mov edi, addr(g_wh_pat)
    call u32_strcpy

    mov esi, addr(g_u32_arg_a)
    mov edi, dword [g_u32_arena]
    mov ecx, 32768
    call u32_get_env
    jmp .walk_semi

.std_path_search:
    ; Search CWD first
    mov edi, addr(g_u32_arg_c)
    mov ecx, 1000
    call u32_get_cwd
    mov esi, addr(g_u32_arg_c)
    mov edi, addr(g_wh_pat)
    call where_probe_dir

    mov esi, addr(szWhPathVar)
    mov edi, dword [g_u32_arena]
    mov ecx, 32768
    call u32_get_env

.walk_semi:
    mov esi, dword [g_u32_arena]
.semi_lp:
    cmp byte [esi], 0
    je .finish
    mov edi, addr(g_u32_arg_c)
.cp_seg:
    mov al, byte [esi]
    test al, al
    jz .got_seg
    cmp al, ';'
    je .adv_seg
    mov byte [edi], al
    inc esi
    inc edi
    jmp .cp_seg
.adv_seg:
    inc esi
.got_seg:
    mov byte [edi], 0
    cmp byte [g_u32_arg_c], 0
    je .semi_lp
    push esi
    mov esi, addr(g_u32_arg_c)
    mov edi, addr(g_wh_pat)
    call where_probe_dir
    pop esi
    jmp .semi_lp

.finish:
    cmp dword [g_wh_found], 0
    jne .ex0
    cmp dword [g_wh_quiet], 0
    jne .ex1
    mov esi, addr(szWhNf)
    call u32_write_err_str
    call u32_write_crlf
.ex1:
    mov eax, 1
    call u32_exit
    jmp .done
.ex0:
    xor eax, eax
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szWhPathVar, "PATH", 0
@data db szWhStar, "*", 0
@data db szWhCom, ".COM", 0
@data db szWhExe, ".EXE", 0
@data db szWhBat, ".BAT", 0
@data db szWhCmd, ".CMD", 0
@data db szWhPad, "   ", 0
@data db szWhNf, "INFO: Could not find files for the given pattern(s).", 0
@data db szWhSyn, "ERROR: Invalid syntax.", 0
@data db szWhHelp, "Displays the location of files that match the search pattern (/R /Q /F /T).", 0

@include "u_data32.inc"

@res dword g_wh_quiet, 1
@res dword g_wh_quote, 1
@res dword g_wh_time, 1
@res dword g_wh_rec, 1
@res dword g_wh_found, 1
@res byte g_wh_pat, 520
@res byte g_wh_rdir, 520
