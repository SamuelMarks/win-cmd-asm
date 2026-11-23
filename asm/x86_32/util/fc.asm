;; @file fc.asm
;; @isa 386
;; @desc Standalone IA-32 FC.EXE file comparison utility (/A /B /C /L /LBn /N /OFF /T /U /W file1 file2).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc fc_lines_eq
;; @abi win32
;; @in ESI = line A, ECX = len A, EDI = line B, EDX = len B
;; @out EAX = 1 if equal (honoring /C and /W), 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Compares two text lines honoring /C case-folding and /W whitespace compression.
@proc fc_lines_eq
    push ebx
    push esi
    push edi
    push ebp
    lea ebx, [esi + ecx]
    lea ebp, [edi + edx]

    cmp dword [g_fc_ws], 0
    je .lp
.trim_lead_a:
    cmp esi, ebx
    jae .trim_lead_b
    mov al, byte [esi]
    cmp al, 0x20
    je .adv_la
    cmp al, 0x09
    jne .trim_lead_b
.adv_la:
    inc esi
    jmp .trim_lead_a
.trim_lead_b:
    cmp edi, ebp
    jae .lp
    mov al, byte [edi]
    cmp al, 0x20
    je .adv_lb
    cmp al, 0x09
    jne .lp
.adv_lb:
    inc edi
    jmp .trim_lead_b

.lp:
    cmp esi, ebx
    jae .a_done
    cmp edi, ebp
    jae .b_done_chk_a

    cmp dword [g_fc_ws], 0
    je .ch_cmp
    mov al, byte [esi]
    cmp al, 0x20
    je .ws_a
    cmp al, 0x09
    jne .ch_cmp
.ws_a:
    mov dl, byte [edi]
    cmp dl, 0x20
    je .skip_both_ws
    cmp dl, 0x09
    jne .no
.skip_both_ws:
    inc esi
    cmp esi, ebx
    jae .skip_wb
    mov al, byte [esi]
    cmp al, 0x20
    je .skip_both_ws
    cmp al, 0x09
    je .skip_both_ws
.skip_wb:
    inc edi
    cmp edi, ebp
    jae .lp
    mov dl, byte [edi]
    cmp dl, 0x20
    je .skip_wb
    cmp dl, 0x09
    je .skip_wb
    jmp .lp

.ch_cmp:
    mov al, byte [esi]
    mov dl, byte [edi]
    cmp dword [g_fc_ci], 0
    je .raw
    call u32_chr_upper
    mov cl, al
    mov al, dl
    call u32_chr_upper
    cmp cl, al
    jne .no
    inc esi
    inc edi
    jmp .lp
.raw:
    cmp al, dl
    jne .no
    inc esi
    inc edi
    jmp .lp

.a_done:
    cmp dword [g_fc_ws], 0
    je .chk_b_end
.trail_b:
    cmp edi, ebp
    jae .yes
    mov dl, byte [edi]
    cmp dl, 0x20
    je .adv_tb
    cmp dl, 0x09
    jne .no
.adv_tb:
    inc edi
    jmp .trail_b
.chk_b_end:
    cmp edi, ebp
    je .yes
    jmp .no

.b_done_chk_a:
    cmp dword [g_fc_ws], 0
    je .no
.trail_a:
    cmp esi, ebx
    jae .yes
    mov al, byte [esi]
    cmp al, 0x20
    je .adv_ta
    cmp al, 0x09
    jne .no
.adv_ta:
    inc esi
    jmp .trail_a

.yes:
    mov eax, 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
.no:
    xor eax, eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=different, 2=missing file/syntax)
;; @err Exits 2 on missing file
;; @clobbers eax, ecx, edx
;; @desc Compares two files in binary (/B) or line (/L /C /N /W /U) mode.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi
    push ebp

    call u32_init
    mov esi, eax
    mov dword [g_fc_bin], 0
    mov dword [g_fc_ci], 0
    mov dword [g_fc_nums], 0
    mov dword [g_fc_ws], 0
    mov byte [g_u32_arg_b], 0
    mov byte [g_u32_arg_c], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run_fc
    cmp dl, '/'
    jne .pos_arg
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'B'
    je .sw_b
    cmp al, 'C'
    je .sw_c
    cmp al, 'N'
    je .sw_n
    cmp al, 'W'
    je .sw_w
    jmp .arg_lp
.sw_b:
    mov dword [g_fc_bin], 1
    jmp .arg_lp
.sw_c:
    mov dword [g_fc_ci], 1
    jmp .arg_lp
.sw_n:
    mov dword [g_fc_nums], 1
    jmp .arg_lp
.sw_w:
    mov dword [g_fc_ws], 1
    jmp .arg_lp

.pos_arg:
    cmp byte [g_u32_arg_b], 0
    jne .pos_b
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp
.pos_b:
    mov edi, addr(g_u32_arg_c)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szFcHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.run_fc:
    cmp byte [g_u32_arg_b], 0
    je .bad_syn
    cmp byte [g_u32_arg_c], 0
    je .bad_syn

    ; Arena [0..220000] = file A, [262144..482144] = file B
    mov edx, 1
    cmp dword [g_fc_bin], 0
    je .ld_a
    xor edx, edx
.ld_a:
    mov esi, addr(g_u32_arg_b)
    mov edi, dword [g_u32_arena]
    mov ecx, 220000
    call u32_load_stream
    jc .nf_err
    mov dword [g_fc_len_a], eax

    mov edx, 1
    cmp dword [g_fc_bin], 0
    je .ld_b
    xor edx, edx
.ld_b:
    mov esi, addr(g_u32_arg_c)
    mov edi, dword [g_u32_arena]
    add edi, 262144
    mov ecx, 220000
    call u32_load_stream
    jc .nf_err
    mov dword [g_fc_len_b], eax

    mov esi, addr(szFcBanner1)
    call u32_write_str
    mov esi, addr(g_u32_arg_b)
    call u32_write_str
    mov esi, addr(szFcBanner2)
    call u32_write_str
    mov esi, addr(g_u32_arg_c)
    call u32_write_str
    call u32_write_crlf

    cmp dword [g_fc_bin], 0
    je .text_cmp

    ; Binary comparison (/B)
    mov esi, dword [g_u32_arena]
    mov edi, esi
    add edi, 262144
    mov ebp, dword [g_fc_len_a]
    cmp ebp, dword [g_fc_len_b]
    jbe .got_min
    mov ebp, dword [g_fc_len_b]
.got_min:
    xor ebx, ebx ; byte offset
    mov dword [g_fc_diffs], 0
.bin_lp:
    cmp ebx, ebp
    jae .bin_len_chk
    mov al, byte [esi + ebx]
    cmp al, byte [edi + ebx]
    je .bin_nx
    mov dword [g_fc_diffs], 1
    mov eax, ebx
    call u32_print_hex32
    push esi
    mov esi, addr(szFcColSp)
    call u32_write_str
    pop esi
    mov al, byte [esi + ebx]
    call u32_print_hex8
    mov al, ' '
    call u32_write_chr
    mov al, byte [edi + ebx]
    call u32_print_hex8
    call u32_write_crlf
.bin_nx:
    inc ebx
    jmp .bin_lp

.bin_len_chk:
    mov eax, dword [g_fc_len_a]
    cmp eax, dword [g_fc_len_b]
    je .chk_diffs
    mov dword [g_fc_diffs], 1
    mov esi, addr(szFcLonger)
    call u32_write_str
    call u32_write_crlf
    jmp .chk_diffs

.text_cmp:
    mov esi, dword [g_u32_arena]
    mov eax, esi
    add eax, dword [g_fc_len_a]
    mov dword [g_fc_end_a], eax

    mov edi, dword [g_u32_arena]
    add edi, 262144
    mov eax, edi
    add eax, dword [g_fc_len_b]
    mov dword [g_fc_end_b], eax

    mov dword [g_fc_line_no], 0
    mov dword [g_fc_diffs], 0

.txt_lp:
    cmp esi, dword [g_fc_end_a]
    jae .a_eof
    cmp edi, dword [g_fc_end_b]
    jae .diff_tail

    inc dword [g_fc_line_no]
    mov ebx, esi
.eol_a:
    cmp ebx, dword [g_fc_end_a]
    jae .got_a
    mov al, byte [ebx]
    cmp al, 13
    je .got_a
    cmp al, 10
    je .got_a
    inc ebx
    jmp .eol_a
.got_a:
    mov ebp, edi
.eol_b:
    cmp ebp, dword [g_fc_end_b]
    jae .got_b
    mov al, byte [ebp]
    cmp al, 13
    je .got_b
    cmp al, 10
    je .got_b
    inc ebp
    jmp .eol_b
.got_b:
    mov ecx, ebx
    sub ecx, esi
    mov edx, ebp
    sub edx, edi
    push ecx
    push edx
    call fc_lines_eq
    pop edx
    pop ecx
    test eax, eax
    jnz .adv_both

    mov dword [g_fc_diffs], 1
    push esi
    push edi
    push ecx
    push edx

    mov esi, addr(szFcStars)
    call u32_write_str
    mov esi, addr(g_u32_arg_b)
    call u32_write_str
    call u32_write_crlf
    cmp dword [g_fc_nums], 0
    je .pr_la
    mov eax, dword [g_fc_line_no]
    call u32_print_u32
    mov esi, addr(szFcColSp)
    call u32_write_str
.pr_la:
    mov esi, dword [esp + 12]
    mov ecx, dword [esp + 4]
    call u32_write_out
    call u32_write_crlf

    mov esi, addr(szFcStars)
    call u32_write_str
    mov esi, addr(g_u32_arg_c)
    call u32_write_str
    call u32_write_crlf
    cmp dword [g_fc_nums], 0
    je .pr_lb
    mov eax, dword [g_fc_line_no]
    call u32_print_u32
    mov esi, addr(szFcColSp)
    call u32_write_str
.pr_lb:
    mov esi, dword [esp + 8]
    mov ecx, dword [esp]
    call u32_write_out
    call u32_write_crlf
    mov esi, addr(szFcStarsEnd)
    call u32_write_str
    call u32_write_crlf

    pop edx
    pop ecx
    pop edi
    pop esi

.adv_both:
    cmp ebx, dword [g_fc_end_a]
    jae .set_a
    cmp byte [ebx], 13
    jne .ca_lf
    inc ebx
.ca_lf:
    cmp ebx, dword [g_fc_end_a]
    jae .set_a
    cmp byte [ebx], 10
    jne .set_a
    inc ebx
.set_a:
    mov esi, ebx

    cmp ebp, dword [g_fc_end_b]
    jae .set_b
    cmp byte [ebp], 13
    jne .cb_lf
    inc ebp
.cb_lf:
    cmp ebp, dword [g_fc_end_b]
    jae .set_b
    cmp byte [ebp], 10
    jne .set_b
    inc ebp
.set_b:
    mov edi, ebp
    jmp .txt_lp

.a_eof:
    cmp edi, dword [g_fc_end_b]
    je .chk_diffs
.diff_tail:
    mov dword [g_fc_diffs], 1
    mov esi, addr(szFcStars)
    call u32_write_str
    mov esi, addr(g_u32_arg_b)
    call u32_write_str
    call u32_write_crlf
    mov esi, addr(szFcStars)
    call u32_write_str
    mov esi, addr(g_u32_arg_c)
    call u32_write_str
    call u32_write_crlf
    mov esi, addr(szFcStarsEnd)
    call u32_write_str
    call u32_write_crlf

.chk_diffs:
    cmp dword [g_fc_diffs], 0
    jne .ex1
    mov esi, addr(szFcNoDiff)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done
.ex1:
    mov eax, 1
    call u32_exit
    jmp .done
.nf_err:
    mov esi, addr(szFcNf)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 2
    call u32_exit
    jmp .done
.bad_syn:
    mov esi, addr(szFcSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 2
    call u32_exit
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szFcBanner1, "Comparing files ", 0
@data db szFcBanner2, " and ", 0
@data db szFcNoDiff, "FC: no differences encountered", 0
@data db szFcStars, "***** ", 0
@data db szFcStarsEnd, "*****", 0
@data db szFcColSp, ": ", 0
@data db szFcLonger, "FC: files are different sizes", 0
@data db szFcNf, "FC: cannot open file", 0
@data db szFcSyn, "FC: Insufficient number of file specifications", 0
@data db szFcHelp, "Compares two files or sets of files and displays the differences (/B /C /L /N /U /W).", 0

@include "u_data32.inc"

@res dword g_fc_bin, 1
@res dword g_fc_ci, 1
@res dword g_fc_nums, 1
@res dword g_fc_ws, 1
@res dword g_fc_len_a, 1
@res dword g_fc_len_b, 1
@res dword g_fc_end_a, 1
@res dword g_fc_end_b, 1
@res dword g_fc_line_no, 1
@res dword g_fc_diffs, 1
