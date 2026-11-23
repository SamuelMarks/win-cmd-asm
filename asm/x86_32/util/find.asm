;; @file find.asm
;; @isa 386
;; @desc Standalone IA-32 FIND.EXE utility (/V, /C, /N, /I, /OFF[LINE] "string" [files...], UTF-8/UTF-16 BOM aware).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc find_line_has
;; @abi win32
;; @in ESI = line start, ECX = line len, EDI = needle, EDX = needle len
;; @out EAX = 1 if needle occurs in line, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Substring search honoring g_find_ci (/I).
@proc find_line_has
    push ebx
    push esi
    push edi
    push ebp
    test edx, edx
    jz .no
    cmp ecx, edx
    jb .no
    sub ecx, edx
    inc ecx ; positions to test
    mov ebp, edx
.pos_lp:
    xor ebx, ebx
.ch_lp:
    cmp ebx, ebp
    jae .yes
    mov al, byte [esi + ebx]
    mov dl, byte [edi + ebx]
    cmp dword [g_find_ci], 0
    je .raw_cmp
    push ecx
    call u32_chr_upper
    mov cl, al
    mov al, dl
    call u32_chr_upper
    cmp cl, al
    pop ecx
    jne .next_pos
    inc ebx
    jmp .ch_lp
.raw_cmp:
    cmp al, dl
    jne .next_pos
    inc ebx
    jmp .ch_lp
.next_pos:
    inc esi
    dec ecx
    jnz .pos_lp
.no:
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

;; @proc find_process_one
;; @abi win32
;; @in ESI = file path UTF-8 (empty = stdin), EDX = show_banner (1 = file arg, 0 = stdin)
;; @out EAX = 1 if at least one line matched, 0 if zero matches, 2 if file open failed
;; @err EAX = 2 on open failure
;; @clobbers eax, ecx, edx
;; @desc Loads stream (decoding UTF-8/UTF-16 BOMs) and scans lines for g_find_needle.
@proc find_process_one
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 16
    mov dword [esp], esi ; path
    mov dword [esp + 4], edx ; show_banner
    mov dword [esp + 8], 0 ; match_count
    mov dword [esp + 12], 0 ; line_num

    mov edi, dword [g_u32_arena]
    mov ecx, 260000
    mov edx, 1
    call u32_load_stream
    jnc .loaded
    mov esi, addr(szFindNf)
    call u32_write_err_str
    mov esi, dword [esp]
    call u32_write_err_str
    mov esi, addr(szU32Crlf)
    call u32_write_err_str
    mov eax, 2
    jmp .done

.loaded:
    mov ebx, eax ; total bytes
    cmp dword [esp + 4], 0
    je .scan_init
    cmp dword [g_find_cnt_only], 0
    jne .scan_init
    mov esi, addr(szFindHdr)
    call u32_write_str
    mov esi, dword [esp]
    call u32_write_str
    call u32_write_crlf

.scan_init:
    mov esi, dword [g_u32_arena]
    lea ebp, [esi + ebx]

.ln_lp:
    cmp esi, ebp
    jae .after_scan
    cmp byte [esi], 0x1A
    je .after_scan
    inc dword [esp + 12]
    mov edi, esi
.find_eol:
    cmp edi, ebp
    jae .got_ln
    mov al, byte [edi]
    cmp al, 13
    je .got_ln
    cmp al, 10
    je .got_ln
    inc edi
    jmp .find_eol

.got_ln:
    mov ecx, edi
    sub ecx, esi
    push edi
    push ecx
    mov edi, addr(g_find_needle)
    mov edx, dword [g_find_nlen]
    call find_line_has
    xor eax, dword [g_find_inv]
    pop ecx
    pop edi
    test eax, eax
    jz .adv_eol

    inc dword [esp + 8]
    cmp dword [g_find_cnt_only], 0
    jne .adv_eol

    cmp dword [g_find_nums], 0
    je .emit_ln
    push esi
    push ecx
    mov al, '['
    call u32_write_chr
    mov eax, dword [esp + 20] ; [esp+12] shifted by 2 pushes
    call u32_print_u32
    mov al, ']'
    call u32_write_chr
    pop ecx
    pop esi
.emit_ln:
    call u32_write_out
    call u32_write_crlf

.adv_eol:
    cmp edi, ebp
    jae .after_scan
    cmp byte [edi], 13
    jne .chk_lf
    inc edi
.chk_lf:
    cmp edi, ebp
    jae .set_nx
    cmp byte [edi], 10
    jne .set_nx
    inc edi
.set_nx:
    mov esi, edi
    jmp .ln_lp

.after_scan:
    cmp dword [g_find_cnt_only], 0
    je .ret_res
    cmp dword [esp + 4], 0
    je .bare_cnt
    mov esi, addr(szFindHdr)
    call u32_write_str
    mov esi, dword [esp]
    call u32_write_str
    mov esi, addr(szFindCol)
    call u32_write_str
.bare_cnt:
    mov eax, dword [esp + 8]
    call u32_print_u32
    call u32_write_crlf

.ret_res:
    xor eax, eax
    cmp dword [esp + 8], 0
    je .done
    mov eax, 1
.done:
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=matched, 1=none, 2=error)
;; @err Exits 2 on syntax/file error
;; @clobbers eax, ecx, edx
;; @desc Parses FIND switches, quoted needle, and zero or more file arguments.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_find_inv], 0
    mov dword [g_find_cnt_only], 0
    mov dword [g_find_nums], 0
    mov dword [g_find_ci], 0
    mov dword [g_find_has_needle], 0
    mov dword [g_find_files], 0
    mov dword [g_find_any_hit], 0
    mov dword [g_find_had_err], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .finish_args
    cmp dl, '"'
    je .needle_or_file
    cmp dl, '/'
    jne .needle_or_file
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'V'
    je .sw_v
    cmp al, 'C'
    je .sw_c
    cmp al, 'N'
    je .sw_n
    cmp al, 'I'
    je .sw_i
    cmp al, 'O'
    je .arg_lp
    jmp .bad_syn
.sw_v:
    mov dword [g_find_inv], 1
    jmp .arg_lp
.sw_c:
    mov dword [g_find_cnt_only], 1
    jmp .arg_lp
.sw_n:
    mov dword [g_find_nums], 1
    jmp .arg_lp
.sw_i:
    mov dword [g_find_ci], 1
    jmp .arg_lp

.needle_or_file:
    cmp dword [g_find_has_needle], 0
    jne .file_arg
    cmp dl, '"'
    jne .bad_syn
    mov edi, addr(g_find_needle)
    call u32_read_tok
    mov esi, eax
    mov dword [g_find_nlen], ecx
    mov dword [g_find_has_needle], 1
    jmp .arg_lp

.file_arg:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    inc dword [g_find_files]
    push esi
    mov esi, addr(g_u32_arg_b)
    mov edx, 1
    call find_process_one
    pop esi
    cmp eax, 1
    je .hit_f
    cmp eax, 2
    jne .arg_lp
    mov dword [g_find_had_err], 1
    jmp .arg_lp
.hit_f:
    mov dword [g_find_any_hit], 1
    jmp .arg_lp

.help:
    mov esi, addr(szFindHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_syn:
    mov esi, addr(szFindSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 2
    call u32_exit
    jmp .done

.finish_args:
    cmp dword [g_find_has_needle], 0
    je .bad_syn
    cmp dword [g_find_files], 0
    jne .exit_code
    mov byte [g_u32_arg_b], 0
    mov esi, addr(g_u32_arg_b)
    xor edx, edx
    call find_process_one
    cmp eax, 1
    jne .exit_code
    mov dword [g_find_any_hit], 1

.exit_code:
    cmp dword [g_find_any_hit], 0
    jne .ex0
    cmp dword [g_find_had_err], 0
    jne .ex2
    mov eax, 1
    call u32_exit
    jmp .done
.ex2:
    mov eax, 2
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

@data db szFindHdr, "---------- ", 0
@data db szFindCol, ": ", 0
@data db szFindNf, "File not found - ", 0
@data db szFindSyn, "FIND: Parameter format not correct", 0
@data db szFindHelp, "Searches for a text string in a file or files (/V /C /N /I /OFF).", 0

@include "u_data32.inc"

@res dword g_find_inv, 1
@res dword g_find_cnt_only, 1
@res dword g_find_nums, 1
@res dword g_find_ci, 1
@res dword g_find_has_needle, 1
@res dword g_find_nlen, 1
@res dword g_find_files, 1
@res dword g_find_any_hit, 1
@res dword g_find_had_err, 1
@res byte g_find_needle, 1024
