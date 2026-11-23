;; @file mem.asm
;; @isa 386
;; @desc Standalone IA-32 MEM.EXE utility (/C, /D, /F, /M, /P) via GlobalMemoryStatusEx, GlobalMemoryStatus, & GetProcessMemoryInfo.

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc mem_print_row
;; @abi win32
;; @in ESI = label string, EDX:EAX = 64-bit byte total
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints a formatted memory summary row in bytes and KiB.
@proc mem_print_row
    push ebx
    push esi
    push edi
    mov ebx, eax
    mov edi, edx
    call u32_write_str
    mov eax, ebx
    mov edx, edi
    call u32_print_u64
    mov esi, addr(szMemBytes)
    call u32_write_str
    ; Divide EDI:EBX by 1024 (shift right 10)
    mov eax, ebx
    mov edx, edi
    shrd eax, edx, 10
    shr edx, 10
    call u32_print_u64
    mov esi, addr(szMemKb)
    call u32_write_str
    call u32_write_crlf
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid switch
;; @clobbers eax, ecx, edx
;; @desc Parses MEM switches (/C, /D, /F, /M, /P, /?) and prints physical/pagefile/virtual/working-set statistics.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_mem_detail], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .query_mem
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    cmp byte [g_u32_arg_a], '/'
    jne .arg_lp
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'C'
    je .set_det
    cmp al, 'D'
    je .set_det
    cmp al, 'F'
    je .arg_lp
    cmp al, 'P'
    je .arg_lp
    cmp al, 'M'
    jne .bad_sw
    cmp byte [g_u32_arg_a + 2], ':'
    je .arg_lp
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp
.set_det:
    mov dword [g_mem_detail], 1
    jmp .arg_lp

.help:
    mov esi, addr(szMemHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_sw:
    mov esi, addr(szMemBadSw)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.query_mem:
    mov esi, addr(szMemHdr)
    call u32_write_str
    call u32_write_crlf
    mov esi, addr(szMemSep)
    call u32_write_str
    call u32_write_crlf

    ; Probe GlobalMemoryStatusEx dynamically (Win2000+)
    push addr(szU32Kernel32)
    call dword [__imp__GetModuleHandleA@4]
    test eax, eax
    jz .fallback_32
    mov ebx, eax
    push addr(szFnGlobalMemEx)
    push ebx
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jz .fallback_32
    mov dword [g_pfnGlobalMemEx_p4], eax

    mov edi, addr(g_mem_stat_ex)
    xor ecx, ecx
.zr_ex:
    cmp ecx, 64
    jae .call_ex
    mov byte [edi + ecx], 0
    inc ecx
    jmp .zr_ex
.call_ex:
    mov dword [edi], 64
    push edi
    call dword [g_pfnGlobalMemEx_p4]
    test eax, eax
    jz .fallback_32

    mov esi, addr(szMemTotPhys)
    mov eax, dword [g_mem_stat_ex + 8]
    mov edx, dword [g_mem_stat_ex + 12]
    call mem_print_row

    mov esi, addr(szMemAvlPhys)
    mov eax, dword [g_mem_stat_ex + 16]
    mov edx, dword [g_mem_stat_ex + 20]
    call mem_print_row

    mov esi, addr(szMemTotPage)
    mov eax, dword [g_mem_stat_ex + 24]
    mov edx, dword [g_mem_stat_ex + 28]
    call mem_print_row

    mov esi, addr(szMemAvlPage)
    mov eax, dword [g_mem_stat_ex + 32]
    mov edx, dword [g_mem_stat_ex + 36]
    call mem_print_row

    mov esi, addr(szMemTotVirt)
    mov eax, dword [g_mem_stat_ex + 40]
    mov edx, dword [g_mem_stat_ex + 44]
    call mem_print_row

    mov esi, addr(szMemAvlVirt)
    mov eax, dword [g_mem_stat_ex + 48]
    mov edx, dword [g_mem_stat_ex + 52]
    call mem_print_row
    jmp .chk_proc_ws

.fallback_32:
    mov dword [g_mem_stat32], 32
    push addr(g_mem_stat32)
    call dword [__imp__GlobalMemoryStatus@4]

    mov esi, addr(szMemTotPhys)
    mov eax, dword [g_mem_stat32 + 8]
    xor edx, edx
    call mem_print_row

    mov esi, addr(szMemAvlPhys)
    mov eax, dword [g_mem_stat32 + 12]
    xor edx, edx
    call mem_print_row

    mov esi, addr(szMemTotPage)
    mov eax, dword [g_mem_stat32 + 16]
    xor edx, edx
    call mem_print_row

    mov esi, addr(szMemAvlPage)
    mov eax, dword [g_mem_stat32 + 20]
    xor edx, edx
    call mem_print_row

    mov esi, addr(szMemTotVirt)
    mov eax, dword [g_mem_stat32 + 24]
    xor edx, edx
    call mem_print_row

    mov esi, addr(szMemAvlVirt)
    mov eax, dword [g_mem_stat32 + 28]
    xor edx, edx
    call mem_print_row

.chk_proc_ws:
    cmp dword [g_mem_detail], 0
    je .finish

    ; Probe K32GetProcessMemoryInfo (Win7+) or PSAPI!GetProcessMemoryInfo
    push addr(szU32Kernel32)
    call dword [__imp__GetModuleHandleA@4]
    push addr(szFnK32ProcMem)
    push eax
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jnz .got_pmi
    push addr(szU32Psapi)
    call dword [__imp__LoadLibraryA@4]
    test eax, eax
    jz .finish
    push addr(szFnPsapiProcMem)
    push eax
    call dword [__imp__GetProcAddress@8]
    test eax, eax
    jz .finish
.got_pmi:
    mov dword [g_pfnGetProcMem_p12], eax
    call dword [__imp__GetCurrentProcess@0]
    push 40
    push addr(g_proc_mem_cnt)
    push eax
    call dword [g_pfnGetProcMem_p12]
    test eax, eax
    jz .finish
    mov esi, addr(szMemProcWs)
    mov eax, dword [g_proc_mem_cnt + 12]
    xor edx, edx
    call mem_print_row

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

@data db szFnGlobalMemEx, "GlobalMemoryStatusEx", 0
@data db szFnK32ProcMem, "K32GetProcessMemoryInfo", 0
@data db szFnPsapiProcMem, "GetProcessMemoryInfo", 0
@data db szMemHdr, "Memory Type              Total Bytes (KiB)", 0
@data db szMemSep, "------------------------ --------------------------", 0
@data db szMemTotPhys, "Total Physical RAM     : ", 0
@data db szMemAvlPhys, "Available Physical RAM : ", 0
@data db szMemTotPage, "Total Page File        : ", 0
@data db szMemAvlPage, "Available Page File    : ", 0
@data db szMemTotVirt, "Total Virtual Address  : ", 0
@data db szMemAvlVirt, "Free Virtual Address   : ", 0
@data db szMemProcWs, "Process Working Set    : ", 0
@data db szMemBytes, " (", 0
@data db szMemKb, "K)", 0
@data db szMemBadSw, "Invalid switch.", 0
@data db szMemHelp, "Displays system physical, pagefile, and virtual memory usage.", 0

@include "u_data32.inc"

@res dword g_mem_detail, 1
@res dword g_pfnGlobalMemEx_p4, 1
@res dword g_pfnGetProcMem_p12, 1
@res byte g_mem_stat_ex, 64
@res byte g_mem_stat32, 32
@res byte g_proc_mem_cnt, 48
