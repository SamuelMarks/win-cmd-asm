;; @file hostname.asm
;; @isa 386
;; @desc Standalone IA-32 HOSTNAME.EXE computer network name reporter (GetComputerNameExW / GetComputerNameW / GetComputerNameA).

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
;; @out Terminates via ExitProcess (0=ok, 1=invalid switch)
;; @err Exits 1 on invalid switch
;; @clobbers eax, ecx, edx
;; @desc Prints computer DNS/NetBIOS hostname via GetComputerNameExW or GetComputerNameW/A.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .query
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    cmp byte [g_u32_arg_a], '/'
    jne .bad
    cmp byte [g_u32_arg_a + 1], '?'
    jne .bad
    mov esi, addr(szHnHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done
.bad:
    mov esi, addr(szHnErr)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.query:
    cmp dword [g_u32_is_win9x], 0
    jne .q9x

    ; Probe GetComputerNameExW (Win2000+) with ComputerNameDnsHostname = 1
    push addr(szU32Kernel32)
    call dword [__imp__GetModuleHandleA@4]
    test eax, eax
    jz .qw
    push addr(szFnCompExW)
    push eax
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jz .qw
    mov dword [g_pfnCompEx_p12], eax
    mov dword [g_hn_sz], 500
    push addr(g_hn_sz)
    push addr(g_u32_wbuf1)
    push 1 ; ComputerNameDnsHostname
    call dword [g_pfnCompEx_p12]
    test eax, eax
    jnz .emit_w

.qw:
    mov dword [g_hn_sz], 500
    push addr(g_hn_sz)
    push addr(g_u32_wbuf1)
    call dword [__imp__GetComputerNameW@8]
    test eax, eax
    jz .env_fb
.emit_w:
    mov esi, addr(g_u32_wbuf1)
    mov edi, addr(g_u32_arg_b)
    mov ecx, 1000
    call u32_w_to_u8
    jmp .pr

.q9x:
    mov dword [g_hn_sz], 500
    push addr(g_hn_sz)
    push addr(g_u32_arg_b)
    call dword [__imp__GetComputerNameA@8]
    test eax, eax
    jnz .pr

.env_fb:
    mov esi, addr(szHnEnv)
    mov edi, addr(g_u32_arg_b)
    mov ecx, 1000
    call u32_get_env
    test eax, eax
    jnz .pr
    mov esi, addr(szHnDef)
    mov edi, addr(g_u32_arg_b)
    call u32_strcpy

.pr:
    mov esi, addr(g_u32_arg_b)
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

@data db szFnCompExW, "GetComputerNameExW", 0
@data db szHnEnv, "COMPUTERNAME", 0
@data db szHnDef, "WINPC", 0
@data db szHnHelp, "Prints the name of the current host.", 0
@data db szHnErr, "HOSTNAME: Invalid option.", 0

@include "u_data32.inc"

@res dword g_pfnCompEx_p12, 1
@res dword g_hn_sz, 1
