;; @file truename.asm
;; @isa x64
;; @desc Standalone x86-64 TRUENAME.EXE canonical path & reparse resolver (GetFullPathNameW + Vista+ GetFinalPathNameByHandleW).

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
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid path
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Resolves input path (or CWD) to canonical full/final path, stripping \?\ verbatim prefix.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov byte [rel g_u64_arg_b], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pos
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    cmp byte [rel g_u64_arg_a + 1], '?'
    je .help
    jmp .arg_lp
.pos:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp

.help:
    lea rcx, [rel szTn64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_u64_arg_b], 0
    jne .res
    mov byte [rel g_u64_arg_b], '.'
    mov byte [rel g_u64_arg_b + 1], 0

.res:
    lea rcx, [rel szU64Kernel32]
    call qword [rel __imp_GetModuleHandleA]
    test rax, rax
    jz .full
    mov rcx, rax
    lea rdx, [rel szFn64Final]
    call qword [rel __imp_GetProcAddress]
    test rax, rax
    jz .full
    mov rdi, rax ; GetFinalPathNameByHandleW

    lea rcx, [rel g_u64_arg_b]
    call u64_open_read
    cmp rax, -1
    je .full
    mov rbx, rax
    mov rcx, rbx
    lea rdx, [rel g_u64_wbuf2]
    mov r8d, 1000
    xor r9d, r9d
    call rdi
    mov edi, eax
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    test edi, edi
    jz .full
    cmp edi, 1000
    jae .full
    jmp .cvt

.full:
    lea rcx, [rel g_u64_arg_b]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    mov edx, 1000
    lea r8, [rel g_u64_wbuf2]
    xor r9d, r9d
    call qword [rel __imp_GetFullPathNameW]
    test eax, eax
    jz .err

.cvt:
    lea rcx, [rel g_u64_wbuf2]
    lea rdx, [rel g_u64_arg_c]
    mov r8d, 1020
    call u64_w_to_u8

    lea rcx, [rel g_u64_arg_c]
    cmp byte [rcx], 0x5C
    jne .emit
    cmp byte [rcx + 1], 0x5C
    jne .emit
    cmp byte [rcx + 2], '?'
    jne .emit
    cmp byte [rcx + 3], 0x5C
    jne .emit
    add rcx, 4
.emit:
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret
.err:
    mov ecx, 1
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szFn64Final, "GetFinalPathNameByHandleW", 0
@data db szTn64Help, "TRUENAME [drive:][path][filename]", 0

@include "u_data64.inc"
