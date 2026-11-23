;; @file more.asm
;; @isa 386
;; @desc Standalone IA-32 MORE.EXE interactive pager (/E, /C, /P, /S, /Tn, +n [files...], CONIN$ key polling).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc more_wait_key
;; @abi win32
;; @in none
;; @out EAX = 1 if user pressed 'Q' to quit, 0 to continue
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prompts "-- More --" and reads a keystroke from CONIN$ even when stdin is redirected.
@proc more_wait_key
    push ebx
    push esi
    push edi

    mov esi, addr(szMorePrompt)
    call u32_write_str

    cmp dword [g_u32_is_win9x], 0
    jne .open_9x
    mov esi, addr(szU32ConIn)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push 0
    push 0x80
    push 3
    push 0
    push 3
    push 0x80000000
    push addr(g_u32_wbuf1)
    call dword [__imp__CreateFileW@28]
    jmp .chk_h
.open_9x:
    push 0
    push 0x80
    push 3
    push 0
    push 3
    push 0x80000000
    push addr(szU32ConIn)
    call dword [__imp__CreateFileA@28]
.chk_h:
    cmp eax, -1
    je .cont_nl
    mov ebx, eax
    mov byte [g_more_key], 0
    push 0
    push addr(g_u32_io_done)
    push 1
    push addr(g_more_key)
    push ebx
    call dword [__imp__ReadFile@20]
    push ebx
    call dword [__imp__CloseHandle@4]
    call u32_write_crlf
    mov al, byte [g_more_key]
    call u32_chr_upper
    cmp al, 'Q'
    je .quit
.cont_dn:
    xor eax, eax
    pop edi
    pop esi
    pop ebx
    ret
.cont_nl:
    call u32_write_crlf
    jmp .cont_dn
.quit:
    mov eax, 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc more_emit_line
;; @abi win32
;; @in ESI = line bytes, ECX = line length
;; @out EAX = 1 if user quit, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Expands tabs (/Tn) and form-feeds (/P) and triggers CONIN$ page break every 23 console rows.
@proc more_emit_line
    push ebx
    push esi
    push edi

    xor ebx, ebx ; index
.ch_lp:
    cmp ebx, ecx
    jae .eol
    mov al, byte [esi + ebx]
    cmp al, 9
    je .do_tab
    cmp al, 12 ; form feed
    jne .norm_ch
    cmp dword [g_more_ff], 0
    je .norm_ch
    call u32_write_crlf
    mov dword [g_more_rows], 23
    inc ebx
    jmp .ch_lp
.do_tab:
    mov edi, dword [g_more_tab]
.tab_lp:
    test edi, edi
    jz .nx_ch
    mov al, ' '
    push ecx
    call u32_write_chr
    pop ecx
    dec edi
    jmp .tab_lp
.norm_ch:
    push ecx
    call u32_write_chr
    pop ecx
.nx_ch:
    inc ebx
    jmp .ch_lp

.eol:
    call u32_write_crlf
    inc dword [g_more_rows]
    cmp dword [g_u32_out_is_con], 0
    je .ok
    cmp dword [g_more_rows], 23
    jb .ok
    mov dword [g_more_rows], 0
    call more_wait_key
    test eax, eax
    jnz .quit
.ok:
    xor eax, eax
    pop edi
    pop esi
    pop ebx
    ret
.quit:
    mov eax, 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc more_stream_one
;; @abi win32
;; @in ESI = file path (empty = stdin)
;; @out EAX = 1 on error, 0 on ok
;; @err EAX = 1
;; @clobbers eax, ecx, edx
;; @desc Streams a file or stdin through line skipping (+n), blank squeezing (/S), and tab expansion (/Tn).
@proc more_stream_one
    push ebx
    push esi
    push edi
    push ebp

    cmp dword [g_more_clr], 0
    je .ld
    cmp dword [g_u32_out_is_con], 0
    je .ld
    push esi
    mov esi, addr(szMoreAnsiCls)
    call u32_write_str
    pop esi

.ld:
    mov edi, dword [g_u32_arena]
    mov ecx, 260000
    mov edx, 1
    call u32_load_stream
    jc .err

    mov esi, dword [g_u32_arena]
    lea ebp, [esi + eax]
    xor ebx, ebx ; prev_blank

.ln_lp:
    cmp esi, ebp
    jae .ok
    cmp byte [esi], 0x1A
    je .ok
    mov edi, esi
.eol:
    cmp edi, ebp
    jae .got_l
    mov al, byte [edi]
    cmp al, 13
    je .got_l
    cmp al, 10
    je .got_l
    inc edi
    jmp .eol

.got_l:
    cmp dword [g_more_skip], 0
    je .chk_sq
    dec dword [g_more_skip]
    jmp .adv

.chk_sq:
    mov ecx, edi
    sub ecx, esi
    cmp dword [g_more_sqz], 0
    je .emit
    test ecx, ecx
    jnz .non_blank
    test ebx, ebx
    jnz .adv
    mov ebx, 1
    jmp .emit
.non_blank:
    xor ebx, ebx

.emit:
    push edi
    call more_emit_line
    pop edi
    test eax, eax
    jnz .ok

.adv:
    cmp edi, ebp
    jae .ok
    cmp byte [edi], 13
    jne .c_lf
    inc edi
.c_lf:
    cmp edi, ebp
    jae .nx
    cmp byte [edi], 10
    jne .nx
    inc edi
.nx:
    mov esi, edi
    jmp .ln_lp

.ok:
    xor eax, eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
.err:
    mov esi, addr(szMoreNf)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on missing file
;; @clobbers eax, ecx, edx
;; @desc Entrypoint for MORE.EXE supporting /E /C /P /S /Tn +n and multiple files or stdin.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_more_clr], 0
    mov dword [g_more_ff], 0
    mov dword [g_more_sqz], 0
    mov dword [g_more_tab], 8
    mov dword [g_more_skip], 0
    mov dword [g_more_rows], 0
    mov dword [g_more_files], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .finish
    cmp dl, '+'
    je .plus_opt
    cmp dl, '/'
    jne .file_opt
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'E'
    je .arg_lp
    cmp al, 'C'
    je .sw_c
    cmp al, 'P'
    je .sw_p
    cmp al, 'S'
    je .sw_s
    cmp al, 'T'
    je .sw_t
    jmp .arg_lp
.sw_c:
    mov dword [g_more_clr], 1
    jmp .arg_lp
.sw_p:
    mov dword [g_more_ff], 1
    jmp .arg_lp
.sw_s:
    mov dword [g_more_sqz], 1
    jmp .arg_lp
.sw_t:
    push esi
    mov esi, addr(g_u32_arg_a)
    add esi, 2
    call u32_parse_u32
    pop esi
    test eax, eax
    jz .arg_lp
    cmp eax, 16
    ja .arg_lp
    mov dword [g_more_tab], eax
    jmp .arg_lp

.plus_opt:
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    push esi
    mov esi, addr(g_u32_arg_a)
    inc esi
    call u32_parse_u32
    pop esi
    mov dword [g_more_skip], eax
    jmp .arg_lp

.file_opt:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    inc dword [g_more_files]
    push esi
    mov esi, addr(g_u32_arg_b)
    call more_stream_one
    pop esi
    test eax, eax
    jnz .ex1
    jmp .arg_lp

.finish:
    cmp dword [g_more_files], 0
    jne .ex0
    mov byte [g_u32_arg_b], 0
    mov esi, addr(g_u32_arg_b)
    call more_stream_one
    test eax, eax
    jnz .ex1
.ex0:
    xor eax, eax
    call u32_exit
    jmp .done
.ex1:
    mov eax, 1
    call u32_exit
    jmp .done
.help:
    mov esi, addr(szMoreHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szMorePrompt, "-- More -- ", 0
@data db szMoreAnsiCls, 27, "[2J", 27, "[H", 0
@data db szMoreNf, "Cannot access file.", 0
@data db szMoreHelp, "Displays output one screen at a time (/E /C /P /S /Tn +n [files]).", 0

@include "u_data32.inc"

@res dword g_more_clr, 1
@res dword g_more_ff, 1
@res dword g_more_sqz, 1
@res dword g_more_tab, 1
@res dword g_more_skip, 1
@res dword g_more_rows, 1
@res dword g_more_files, 1
@res byte g_more_key, 8
