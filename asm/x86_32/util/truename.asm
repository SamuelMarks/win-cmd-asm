;; @file truename.asm
;; @isa 386
;; @desc Standalone IA-32 TRUENAME.EXE canonical path & reparse resolver (GetFullPathNameW/A + Vista+ GetFinalPathNameByHandleW).

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
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid path
;; @clobbers eax, ecx, edx
;; @desc Resolves input path (or CWD) to canonical full/final path, stripping \?\ verbatim prefix.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov byte [g_u32_arg_b], 0

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
    cmp byte [g_u32_arg_a + 1], '?'
    je .help
    jmp .arg_lp
.pos_arg:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.help:
    mov esi, addr(szTnHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.run:
    cmp byte [g_u32_arg_b], 0
    jne .resolve
    mov byte [g_u32_arg_b], '.'
    mov byte [g_u32_arg_b + 1], 0

.resolve:
    cmp dword [g_u32_is_win9x], 0
    jne .res_9x

    ; Try Vista+ GetFinalPathNameByHandleW on existing file/dir/reparse target first
    push addr(szU32Kernel32)
    call dword [__imp__GetModuleHandleA@4]
    test eax, eax
    jz .full_nt
    push addr(szFnFinalPath)
    push eax
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jz .full_nt
    mov dword [g_pfnFinalPath_p16], eax

    mov esi, addr(g_u32_arg_b)
    call u32_open_read
    cmp eax, -1
    je .full_nt
    mov ebx, eax
    push 0 ; VOLUME_NAME_DOS
    push 1000
    push addr(g_u32_wbuf2)
    push ebx
    call dword [g_pfnFinalPath_p16]
    push eax
    push ebx
    call dword [__imp__CloseHandle@4]
    pop eax
    test eax, eax
    jz .full_nt
    cmp eax, 1000
    jae .full_nt
    mov esi, addr(g_u32_wbuf2)
    mov edi, addr(g_u32_arg_c)
    mov ecx, 1020
    call u32_w_to_u8
    jmp .strip_verbatim

.full_nt:
    mov esi, addr(g_u32_arg_b)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push 0
    push addr(g_u32_wbuf2)
    push 1000
    push addr(g_u32_wbuf1)
    call dword [__imp__GetFullPathNameW@16]
    test eax, eax
    jz .err
    mov esi, addr(g_u32_wbuf2)
    mov edi, addr(g_u32_arg_c)
    mov ecx, 1020
    call u32_w_to_u8
    jmp .strip_verbatim

.res_9x:
    push 0
    push addr(g_u32_arg_c)
    push 1000
    push addr(g_u32_arg_b)
    call dword [__imp__GetFullPathNameA@16]
    test eax, eax
    jz .err

.strip_verbatim:
    mov esi, addr(g_u32_arg_c)
    cmp byte [esi], 0x5C
    jne .emit
    cmp byte [esi + 1], 0x5C
    jne .emit
    cmp byte [esi + 2], '?'
    jne .emit
    cmp byte [esi + 3], 0x5C
    jne .emit
    add esi, 4
.emit:
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.err:
    mov eax, 1
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szFnFinalPath, "GetFinalPathNameByHandleW", 0
@data db szTnHelp, "TRUENAME [drive:][path][filename]", 0

@include "u_data32.inc"

@res dword g_pfnFinalPath_p16, 1
