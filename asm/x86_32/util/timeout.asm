;; @file timeout.asm
;; @isa 386
;; @desc Standalone IA-32 TIMEOUT.EXE countdown utility ([/T] seconds [/NOBREAK]).

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
;; @out Terminates via ExitProcess (0=ok, 1=syntax error)
;; @err Exits 1 on missing/invalid seconds
;; @clobbers eax, ecx, edx
;; @desc Parses TIMEOUT switches ([/T] seconds [/NOBREAK]) and waits via Sleep/WaitForSingleObject.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_to_nobreak], 0
    mov dword [g_to_has_sec], 0
    mov dword [g_to_sec], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .num_pos
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'N'
    je .sw_nb
    cmp al, 'T'
    jne .bad_syn
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .parse_sec

.sw_nb:
    mov dword [g_to_nobreak], 1
    jmp .arg_lp

.num_pos:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax

.parse_sec:
    cmp byte [g_u32_arg_b], '-'
    jne .pos_sec
    cmp byte [g_u32_arg_b + 1], '1'
    jne .bad_syn
    cmp byte [g_u32_arg_b + 2], 0
    jne .bad_syn
    mov dword [g_to_sec], 0
    mov dword [g_to_has_sec], 1
    jmp .arg_lp
.pos_sec:
    push esi
    mov esi, addr(g_u32_arg_b)
    call u32_parse_u32
    mov cl, byte [edx]
    pop esi
    test cl, cl
    jnz .bad_syn
    mov dword [g_to_sec], eax
    mov dword [g_to_has_sec], 1
    jmp .arg_lp

.help:
    mov esi, addr(szToHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_syn:
    mov esi, addr(szToSyn)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.run:
    cmp dword [g_to_has_sec], 0
    je .bad_syn

    call u32_write_crlf
    mov esi, addr(szToWait1)
    call u32_write_str
    mov eax, dword [g_to_sec]
    call u32_print_u32
    cmp dword [g_to_nobreak], 0
    jne .pr_nb
    mov esi, addr(szToWaitKey)
    jmp .pr_msg
.pr_nb:
    mov esi, addr(szToWaitNb)
.pr_msg:
    call u32_write_str
    call u32_write_crlf

    mov ebx, dword [g_to_sec]
.sec_lp:
    test ebx, ebx
    jz .finish
    cmp dword [g_to_nobreak], 0
    jne .do_sleep
    cmp dword [g_u32_in_is_con], 0
    je .do_sleep
    push 1000
    push dword [g_u32_stdin]
    call dword [__imp__WaitForSingleObject@8]
    test eax, eax
    jz .finish
    dec ebx
    jmp .sec_lp
.do_sleep:
    push 1000
    call dword [__imp__Sleep@4]
    dec ebx
    jmp .sec_lp

.finish:
    xor eax, eax
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szToWait1, "Waiting for ", 0
@data db szToWaitKey, " seconds, press a key to continue ...", 0
@data db szToWaitNb, " seconds, press CTRL+C to quit ...", 0
@data db szToSyn, "ERROR: Invalid syntax.", 0
@data db szToHelp, "Accepts a timeout parameter to wait for the specified time period ([/T] sec [/NOBREAK]).", 0

@include "u_data32.inc"

@res dword g_to_nobreak, 1
@res dword g_to_has_sec, 1
@res dword g_to_sec, 1
