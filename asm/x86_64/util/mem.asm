;; @file mem.asm
;; @isa x64
;; @desc Standalone x86-64 MEM.EXE utility (/C, /D, /F, /M, /P) via GlobalMemoryStatusEx & GetProcessMemoryInfo.

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc mem64_print_row
;; @abi win64_ms
;; @in RCX = label string, RDX = 64-bit byte total
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints a formatted memory summary row in bytes and KiB.
@proc_frame mem64_print_row, 48, rbx
    mov rbx, rdx
    call u64_write_str
    mov rcx, rbx
    call u64_print_u64
    lea rcx, [rel szMem64Bytes]
    call u64_write_str
    mov rcx, rbx
    shr rcx, 10
    call u64_print_u64
    lea rcx, [rel szMem64Kb]
    call u64_write_str
    call u64_write_crlf
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid switch
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses MEM switches (/C, /D, /F, /M, /P, /?) and prints 64-bit physical/pagefile/virtual/working-set statistics.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_mem64_det], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .query
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    cmp byte [rel g_u64_arg_a], '/'
    jne .arg_lp
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'C'
    je .set_d
    cmp al, 'D'
    je .set_d
    cmp al, 'F'
    je .arg_lp
    cmp al, 'P'
    je .arg_lp
    cmp al, 'M'
    jne .bad
    cmp byte [rel g_u64_arg_a + 2], ':'
    je .arg_lp
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.set_d:
    mov dword [rel g_mem64_det], 1
    jmp .arg_lp

.help:
    lea rcx, [rel szMem64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szMem64Bad]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret

.query:
    lea rcx, [rel szMem64Hdr]
    call u64_write_str
    call u64_write_crlf
    lea rcx, [rel szMem64Sep]
    call u64_write_str
    call u64_write_crlf

    lea rdi, [rel g_mem64_stat]
    xor eax, eax
.zr:
    cmp eax, 64
    jae .call_ex
    mov byte [rdi + rax], 0
    inc eax
    jmp .zr
.call_ex:
    mov dword [rdi], 64
    mov rcx, rdi
    call qword [rel __imp_GlobalMemoryStatusEx]

    lea rcx, [rel szMem64TotPhys]
    mov rdx, qword [rel g_mem64_stat + 8]
    call mem64_print_row

    lea rcx, [rel szMem64AvlPhys]
    mov rdx, qword [rel g_mem64_stat + 16]
    call mem64_print_row

    lea rcx, [rel szMem64TotPage]
    mov rdx, qword [rel g_mem64_stat + 24]
    call mem64_print_row

    lea rcx, [rel szMem64AvlPage]
    mov rdx, qword [rel g_mem64_stat + 32]
    call mem64_print_row

    lea rcx, [rel szMem64TotVirt]
    mov rdx, qword [rel g_mem64_stat + 40]
    call mem64_print_row

    lea rcx, [rel szMem64AvlVirt]
    mov rdx, qword [rel g_mem64_stat + 48]
    call mem64_print_row

    cmp dword [rel g_mem64_det], 0
    je .finish

    lea rcx, [rel szU64Kernel32]
    call qword [rel __imp_GetModuleHandleA]
    mov rcx, rax
    lea rdx, [rel szFn64K32Pmi]
    call qword [rel __imp_GetProcAddress]
    test rax, rax
    jnz .got_pmi
    lea rcx, [rel szU64Psapi]
    call qword [rel __imp_LoadLibraryA]
    test rax, rax
    jz .finish
    mov rcx, rax
    lea rdx, [rel szFn64PsapiPmi]
    call qword [rel __imp_GetProcAddress]
    test rax, rax
    jz .finish
.got_pmi:
    mov rbx, rax
    call qword [rel __imp_GetCurrentProcess]
    mov rcx, rax
    lea rdx, [rel g_mem64_pmc]
    mov r8d, 72
    call rbx
    test eax, eax
    jz .finish
    lea rcx, [rel szMem64ProcWs]
    mov rdx, qword [rel g_mem64_pmc + 16]
    call mem64_print_row

.finish:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szFn64K32Pmi, "K32GetProcessMemoryInfo", 0
@data db szFn64PsapiPmi, "GetProcessMemoryInfo", 0
@data db szMem64Hdr, "Memory Type              Total Bytes (KiB)", 0
@data db szMem64Sep, "------------------------ --------------------------", 0
@data db szMem64TotPhys, "Total Physical RAM     : ", 0
@data db szMem64AvlPhys, "Available Physical RAM : ", 0
@data db szMem64TotPage, "Total Page File        : ", 0
@data db szMem64AvlPage, "Available Page File    : ", 0
@data db szMem64TotVirt, "Total Virtual Address  : ", 0
@data db szMem64AvlVirt, "Free Virtual Address   : ", 0
@data db szMem64ProcWs, "Process Working Set    : ", 0
@data db szMem64Bytes, " (", 0
@data db szMem64Kb, "K)", 0
@data db szMem64Bad, "Invalid switch.", 0
@data db szMem64Help, "Displays 64-bit physical, pagefile, and virtual memory usage.", 0

@include "u_data64.inc"

@res dword g_mem64_det, 1
@res byte g_mem64_stat, 64
@res byte g_mem64_pmc, 80
