;; @file comp.asm
;; @isa 386
;; @desc Standalone IA-32 COMP.EXE file comparison utility (/D, /A, /L, /N=num, /C, /M file1 file2).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=mismatch, 2=error)
;; @err Exits 2 on missing file
;; @clobbers eax, ecx, edx
;; @desc Compares two files byte-by-byte or up to /N=num lines with /D, /A, /L, /C, and interactive prompt.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi
    push ebp

    call u32_init
    mov esi, eax
    mov dword [g_cmp_dec], 0
    mov dword [g_cmp_asc], 0
    mov dword [g_cmp_ln], 0
    mov dword [g_cmp_ci], 0
    mov dword [g_cmp_max_ln], 0
    mov dword [g_cmp_no_prompt], 0
    mov byte [g_u32_arg_b], 0
    mov byte [g_u32_arg_c], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pos_arg
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'D'
    je .sw_d
    cmp al, 'A'
    je .sw_a
    cmp al, 'L'
    je .sw_l
    cmp al, 'C'
    je .sw_c
    cmp al, 'M'
    je .sw_m
    cmp al, 'N'
    je .sw_n
    jmp .arg_lp
.sw_d:
    mov dword [g_cmp_dec], 1
    jmp .arg_lp
.sw_a:
    mov dword [g_cmp_asc], 1
    jmp .arg_lp
.sw_l:
    mov dword [g_cmp_ln], 1
    jmp .arg_lp
.sw_c:
    mov dword [g_cmp_ci], 1
    jmp .arg_lp
.sw_m:
    mov dword [g_cmp_no_prompt], 1
    jmp .arg_lp
.sw_n:
    cmp byte [g_u32_arg_a + 2], '='
    jne .arg_lp
    push esi
    mov esi, addr(g_u32_arg_a)
    add esi, 3
    call u32_parse_u32
    pop esi
    mov dword [g_cmp_max_ln], eax
    jmp .arg_lp

.pos_arg:
    cmp byte [g_u32_arg_b], 0
    jne .pos_2
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp
.pos_2:
    mov edi, addr(g_u32_arg_c)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szCmpHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.run:
    cmp byte [g_u32_arg_b], 0
    je .err_open
    cmp byte [g_u32_arg_c], 0
    je .err_open

    mov esi, addr(g_u32_arg_b)
    mov edi, dword [g_u32_arena]
    mov ecx, 220000
    xor edx, edx
    call u32_load_stream
    jc .err_open
    mov dword [g_cmp_len_a], eax

    mov esi, addr(g_u32_arg_c)
    mov edi, dword [g_u32_arena]
    add edi, 262144
    mov ecx, 220000
    xor edx, edx
    call u32_load_stream
    jc .err_open
    mov dword [g_cmp_len_b], eax

    mov esi, addr(szCmpHdr1)
    call u32_write_str
    mov esi, addr(g_u32_arg_b)
    call u32_write_str
    mov esi, addr(szCmpHdr2)
    call u32_write_str
    mov esi, addr(g_u32_arg_c)
    call u32_write_str
    mov esi, addr(szCmpDots)
    call u32_write_str
    call u32_write_crlf

    mov eax, dword [g_cmp_len_a]
    cmp dword [g_cmp_max_ln], 0
    jne .start_cmp
    cmp eax, dword [g_cmp_len_b]
    je .start_cmp
    mov esi, addr(szCmpDiffSz)
    call u32_write_str
    call u32_write_crlf
    mov dword [g_cmp_errs], 1
    jmp .prompt_more

.start_cmp:
    mov ebp, dword [g_cmp_len_a]
    cmp ebp, dword [g_cmp_len_b]
    jbe .got_min
    mov ebp, dword [g_cmp_len_b]
.got_min:
    mov esi, dword [g_u32_arena]
    mov edi, esi
    add edi, 262144
    xor ebx, ebx ; offset
    mov dword [g_cmp_cur_ln], 1
    mov dword [g_cmp_errs], 0

.cmp_lp:
    cmp ebx, ebp
    jae .finish_cmp
    mov al, byte [esi + ebx]
    cmp al, 10
    jne .no_nl
    inc dword [g_cmp_cur_ln]
    mov eax, dword [g_cmp_max_ln]
    test eax, eax
    jz .load_b
    mov edx, dword [g_cmp_cur_ln]
    dec edx
    cmp edx, eax
    jae .finish_cmp
.load_b:
    mov al, byte [esi + ebx]
.no_nl:
    mov dl, byte [edi + ebx]
    cmp dword [g_cmp_ci], 0
    je .raw_b
    call u32_chr_upper
    mov cl, al
    mov al, dl
    call u32_chr_upper
    cmp cl, al
    je .nx_b
    jmp .mismatch
.raw_b:
    cmp al, dl
    je .nx_b

.mismatch:
    inc dword [g_cmp_errs]
    cmp dword [g_cmp_errs], 10
    ja .finish_cmp

    push esi
    cmp dword [g_cmp_ln], 0
    jne .pr_ln_err
    mov esi, addr(szCmpErrOff)
    call u32_write_str
    mov eax, ebx
    call u32_print_hex32
    jmp .pr_vals
.pr_ln_err:
    mov esi, addr(szCmpErrLn)
    call u32_write_str
    mov eax, dword [g_cmp_cur_ln]
    call u32_print_u32

.pr_vals:
    call u32_write_crlf
    mov esi, addr(szCmpF1)
    call u32_write_str
    pop esi
    movzx eax, byte [ebx + esi]
    call comp_fmt_byte
    call u32_write_crlf
    push esi
    mov esi, addr(szCmpF2)
    call u32_write_str
    pop esi
    movzx eax, byte [ebx + edi]
    call comp_fmt_byte
    call u32_write_crlf

.nx_b:
    inc ebx
    jmp .cmp_lp

.finish_cmp:
    cmp dword [g_cmp_errs], 0
    jne .prompt_more
    mov esi, addr(szCmpOk)
    call u32_write_str
    call u32_write_crlf

.prompt_more:
    cmp dword [g_cmp_no_prompt], 0
    jne .exit_res
    cmp dword [g_u32_in_is_con], 0
    je .exit_res
    mov esi, addr(szCmpMore)
    call u32_write_str
    call u32_write_crlf

.exit_res:
    cmp dword [g_cmp_errs], 0
    jne .ex1
    xor eax, eax
    call u32_exit
    jmp .done
.ex1:
    mov eax, 1
    call u32_exit
    jmp .done
.err_open:
    mov esi, addr(szCmpOpenErr)
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

;; @proc comp_fmt_byte
;; @abi win32
;; @in EAX = byte value (0..255)
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints byte value as ASCII (/A), decimal (/D), or hex (default).
@proc comp_fmt_byte
    cmp dword [g_cmp_asc], 0
    jne .as_chr
    cmp dword [g_cmp_dec], 0
    jne .as_dec
    call u32_print_hex8
    ret
.as_dec:
    call u32_print_u32
    ret
.as_chr:
    call u32_write_chr
    ret
@endproc

@section rdata

@data db szCmpHdr1, "Comparing ", 0
@data db szCmpHdr2, " and ", 0
@data db szCmpDots, "...", 0
@data db szCmpOk, "Files compare OK", 0
@data db szCmpDiffSz, "Files are different sizes", 0
@data db szCmpErrOff, "Compare error at OFFSET ", 0
@data db szCmpErrLn, "Compare error at LINE ", 0
@data db szCmpF1, "file1 = ", 0
@data db szCmpF2, "file2 = ", 0
@data db szCmpMore, "Compare more files (Y/N) ? N", 0
@data db szCmpOpenErr, "Can't find/open file", 0
@data db szCmpHelp, "Compares the contents of two files or sets of files (/D /A /L /N=num /C /M).", 0

@include "u_data32.inc"

@res dword g_cmp_dec, 1
@res dword g_cmp_asc, 1
@res dword g_cmp_ln, 1
@res dword g_cmp_ci, 1
@res dword g_cmp_max_ln, 1
@res dword g_cmp_no_prompt, 1
@res dword g_cmp_len_a, 1
@res dword g_cmp_len_b, 1
@res dword g_cmp_cur_ln, 1
@res dword g_cmp_errs, 1
