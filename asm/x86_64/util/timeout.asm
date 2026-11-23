;; @file timeout.asm
;; @isa x64
;; @desc Standalone x86-64 TIMEOUT.EXE countdown utility ([/T] seconds [/NOBREAK]).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 1=syntax error)
;; @err Exits 1 on invalid syntax
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 TIMEOUT.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_to64_nb], 0
    mov dword [rel g_to64_has], 0
    mov dword [rel g_to64_sec], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .npos
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'N'
    je .snb
    cmp al, 'T'
    jne .bad
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    jmp .psec
.snb:
    mov dword [rel g_to64_nb], 1
    jmp .arg_lp
.npos:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
.psec:
    cmp byte [rel g_u64_arg_b], '-'
    jne .pos_s
    cmp byte [rel g_u64_arg_b + 1], '1'
    jne .bad
    cmp byte [rel g_u64_arg_b + 2], 0
    jne .bad
    mov dword [rel g_to64_sec], 0
    mov dword [rel g_to64_has], 1
    jmp .arg_lp
.pos_s:
    lea rcx, [rel g_u64_arg_b]
    call u64_parse_u32
    cmp byte [rdx], 0
    jne .bad
    mov dword [rel g_to64_sec], eax
    mov dword [rel g_to64_has], 1
    jmp .arg_lp

.help:
    lea rcx, [rel szTo64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szTo64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret

.run:
    cmp dword [rel g_to64_has], 0
    je .bad
    call u64_write_crlf
    lea rcx, [rel szTo64W1]
    call u64_write_str
    mov ecx, dword [rel g_to64_sec]
    call u64_print_u64
    cmp dword [rel g_to64_nb], 0
    jne .pnb
    lea rcx, [rel szTo64Key]
    jmp .pm
.pnb:
    lea rcx, [rel szTo64Nb]
.pm:
    call u64_write_str
    call u64_write_crlf

    mov ebx, dword [rel g_to64_sec]
.slp:
    test ebx, ebx
    jz .fin
    cmp dword [rel g_to64_nb], 0
    jne .slp_s
    cmp dword [rel g_u64_in_is_con], 0
    je .slp_s
    mov rcx, qword [rel g_u64_stdin]
    mov edx, 1000
    call qword [rel __imp_WaitForSingleObject]
    test eax, eax
    jz .fin
    dec ebx
    jmp .slp
.slp_s:
    mov ecx, 1000
    call qword [rel __imp_Sleep]
    dec ebx
    jmp .slp
.fin:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szTo64W1, "Waiting for ", 0
@data db szTo64Key, " seconds, press a key to continue ...", 0
@data db szTo64Nb, " seconds, press CTRL+C to quit ...", 0
@data db szTo64Syn, "ERROR: Invalid syntax.", 0
@data db szTo64Help, "Accepts a timeout parameter to wait for the specified time period ([/T] sec [/NOBREAK]).", 0

@include "u_data64.inc"

@res dword g_to64_nb, 1
@res dword g_to64_has, 1
@res dword g_to64_sec, 1
