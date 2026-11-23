;; @file choice.asm
;; @isa 386
;; @desc Standalone IA-32 CHOICE.EXE prompt utility (/C[:]choices, /N, /CS, /T nn /D c, /M msg).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc choice_match_key
;; @abi win32
;; @in AL = input character
;; @out EAX = 1-based index in g_ch_list, or 0 if not matched
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Matches character AL against configured choice list honoring /CS.
@proc choice_match_key
    push ebx
    mov bl, al
    cmp dword [g_ch_cs], 0
    jne .scan
    call u32_chr_upper
    mov bl, al
.scan:
    xor ecx, ecx
.lp:
    mov al, byte [g_ch_list + ecx]
    test al, al
    jz .no
    cmp dword [g_ch_cs], 0
    jne .cmp_c
    push ecx
    call u32_chr_upper
    pop ecx
.cmp_c:
    cmp al, bl
    je .hit
    inc ecx
    jmp .lp
.hit:
    lea eax, [ecx + 1]
    pop ebx
    ret
.no:
    xor eax, eax
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (1..N = selected index, 255 = invalid syntax)
;; @err Exits 255 on syntax error
;; @clobbers eax, ecx, edx
;; @desc Parses CHOICE switches (both NT and classic DOS /T:c,nn forms) and returns 1-based ERRORLEVEL.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_ch_hide], 0
    mov dword [g_ch_cs], 0
    mov dword [g_ch_has_to], 0
    mov dword [g_ch_timeout], 0
    mov byte [g_ch_def], 0
    mov byte [g_ch_msg], 0
    mov byte [g_ch_list], 'Y'
    mov byte [g_ch_list + 1], 'N'
    mov byte [g_ch_list + 2], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .prompt
    cmp dl, '/'
    jne .msg_pos
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'N'
    je .sw_n
    cmp al, 'C'
    je .sw_c_or_cs
    cmp al, 'T'
    je .sw_t
    cmp al, 'D'
    je .sw_d
    cmp al, 'M'
    je .sw_m
    jmp .bad_syn

.sw_n:
    mov dword [g_ch_hide], 1
    jmp .arg_lp
.sw_c_or_cs:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'S'
    jne .sw_c_list
    cmp byte [g_u32_arg_a + 3], 0
    jne .sw_c_list
    mov dword [g_ch_cs], 1
    jmp .arg_lp
.sw_c_list:
    lea eax, [g_u32_arg_a + 2]
    cmp byte [eax], ':'
    jne .chk_inline_c
    inc eax
.chk_inline_c:
    cmp byte [eax], 0
    je .next_c_tok
    push esi
    mov esi, eax
    mov edi, addr(g_ch_list)
    call u32_strcpy
    pop esi
    jmp .arg_lp
.next_c_tok:
    mov edi, addr(g_ch_list)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.sw_t:
    ; Support "/T:Y,5" or "/T 5"
    cmp byte [g_u32_arg_a + 2], ':'
    jne .nt_t
    mov al, byte [g_u32_arg_a + 3]
    mov byte [g_ch_def], al
    mov dword [g_ch_has_to], 1
    cmp byte [g_u32_arg_a + 4], ','
    jne .arg_lp
    push esi
    lea esi, [g_u32_arg_a + 5]
    call u32_parse_u32
    pop esi
    mov dword [g_ch_timeout], eax
    jmp .arg_lp
.nt_t:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    push esi
    mov esi, addr(g_u32_arg_b)
    call u32_parse_u32
    pop esi
    mov dword [g_ch_timeout], eax
    mov dword [g_ch_has_to], 1
    jmp .arg_lp

.sw_d:
    cmp byte [g_u32_arg_a + 2], ':'
    jne .nt_d
    mov al, byte [g_u32_arg_a + 3]
    mov byte [g_ch_def], al
    jmp .arg_lp
.nt_d:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_b]
    mov byte [g_ch_def], al
    jmp .arg_lp

.sw_m:
    mov edi, addr(g_ch_msg)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.msg_pos:
    mov edi, addr(g_ch_msg)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szChHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_syn:
    mov esi, addr(szChSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 255
    call u32_exit
    jmp .done

.prompt:
    cmp byte [g_ch_list], 0
    je .bad_syn
    cmp byte [g_ch_msg], 0
    je .chk_brack
    mov esi, addr(g_ch_msg)
    call u32_write_str
    mov al, ' '
    call u32_write_chr

.chk_brack:
    cmp dword [g_ch_hide], 0
    jne .wait_in
    mov al, '['
    call u32_write_chr
    xor ebx, ebx
.pr_c:
    mov al, byte [g_ch_list + ebx]
    test al, al
    jz .end_br
    test ebx, ebx
    jz .emit_c
    mov al, ','
    call u32_write_chr
    mov al, byte [g_ch_list + ebx]
.emit_c:
    cmp dword [g_ch_cs], 0
    jne .raw_c
    call u32_chr_upper
.raw_c:
    call u32_write_chr
    inc ebx
    jmp .pr_c
.end_br:
    mov esi, addr(szChQ)
    call u32_write_str

.wait_in:
    cmp dword [g_ch_has_to], 0
    je .rd_lp
    cmp dword [g_ch_timeout], 0
    je .use_def

.rd_lp:
    mov byte [g_ch_in], 0
    push 0
    push addr(g_u32_io_done)
    push 1
    push addr(g_ch_in)
    push dword [g_u32_stdin]
    call dword [__imp__ReadFile@20]
    test eax, eax
    jz .eof_in
    cmp dword [g_u32_io_done], 0
    je .eof_in
    mov al, byte [g_ch_in]
    cmp al, 13
    je .rd_lp
    cmp al, 10
    je .rd_lp
    mov bl, al
    call choice_match_key
    test eax, eax
    jz .rd_lp
    mov edi, eax
    mov al, bl
    call u32_write_chr
    call u32_write_crlf
    mov eax, edi
    call u32_exit
    jmp .done

.eof_in:
    cmp byte [g_ch_def], 0
    jne .use_def
    mov eax, 255
    call u32_exit
    jmp .done

.use_def:
    mov al, byte [g_ch_def]
    mov bl, al
    call choice_match_key
    test eax, eax
    jz .bad_syn
    mov edi, eax
    mov al, bl
    call u32_write_chr
    call u32_write_crlf
    mov eax, edi
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szChQ, "]?", 0
@data db szChSyn, "ERROR: Invalid choice syntax.", 0
@data db szChHelp, "Prompts the user to select one item from a list of choices (/C /N /CS /T /D /M).", 0

@include "u_data32.inc"

@res dword g_ch_hide, 1
@res dword g_ch_cs, 1
@res dword g_ch_has_to, 1
@res dword g_ch_timeout, 1
@res byte g_ch_def, 4
@res byte g_ch_in, 4
@res byte g_ch_list, 128
@res byte g_ch_msg, 520
