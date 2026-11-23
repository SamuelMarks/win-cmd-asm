;; @file hostname.asm
;; @isa x64
;; @desc Standalone x86-64 HOSTNAME.EXE computer network name reporter (GetComputerNameExW / GetComputerNameW).

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
;; @out Terminates via ExitProcess (0=ok, 1=invalid option)
;; @err Exits 1 on invalid option
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints computer DNS/NetBIOS hostname via GetComputerNameExW or GetComputerNameW.
@proc_frame mainCRTStartup, 48, rsi
    call u64_init
    mov rsi, rax
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .query
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    cmp byte [rel g_u64_arg_a], '/'
    jne .bad
    cmp byte [rel g_u64_arg_a + 1], '?'
    jne .bad
    lea rcx, [rel szHn64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret
.bad:
    lea rcx, [rel szHn64Err]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret

.query:
    mov dword [rel g_hn64_sz], 500
    mov ecx, 1 ; ComputerNameDnsHostname
    lea rdx, [rel g_u64_wbuf1]
    lea r8, [rel g_hn64_sz]
    call qword [rel __imp_GetComputerNameExW]
    test eax, eax
    jnz .cvt

    mov dword [rel g_hn64_sz], 500
    lea rcx, [rel g_u64_wbuf1]
    lea rdx, [rel g_hn64_sz]
    call qword [rel __imp_GetComputerNameW]
    test eax, eax
    jz .env

.cvt:
    lea rcx, [rel g_u64_wbuf1]
    lea rdx, [rel g_u64_arg_b]
    mov r8d, 1000
    call u64_w_to_u8
    jmp .pr

.env:
    lea rcx, [rel szHn64Env]
    lea rdx, [rel g_u64_arg_b]
    mov r8d, 1000
    call u64_get_env
    test rax, rax
    jnz .pr
    lea rcx, [rel g_u64_arg_b]
    lea rdx, [rel szHn64Def]
    call u64_strcpy

.pr:
    lea rcx, [rel g_u64_arg_b]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szHn64Env, "COMPUTERNAME", 0
@data db szHn64Def, "WIN64PC", 0
@data db szHn64Help, "Prints the name of the current host.", 0
@data db szHn64Err, "HOSTNAME: Invalid option.", 0

@include "u_data64.inc"

@res dword g_hn64_sz, 1
