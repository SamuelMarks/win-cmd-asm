;; @file hal_dyn64.inc
;; @isa x64
;; @desc Progressive x86-64 GetModuleHandleW/LoadLibraryW + GetProcAddress capability loader (XP Pro x64 .. Windows 11).

@cpu x64
@format inc

;; @proc dyn_resolve_proc64
;; @abi win64_ms
;; @in RCX = HMODULE (0 -> returns 0), RDX = ASCII export name
;; @out RAX = 64-bit function pointer or 0
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Resolves a Win64 export via GetProcAddress safely when HMODULE is non-NULL.
@proc_frame dyn_resolve_proc64, 40
    test rcx, rcx
    jz .null_mod
    call qword [rel __imp_GetProcAddress]
    @epilog_ret
.null_mod:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc hal_read_peb_osver64
;; @abi win64_ms
;; @in none (x64 TEB at GS:[0], PEB64 at GS:[0x60] with OSMajorVersion at +0x118, OSMinorVersion at +0x11C, OSBuildNumber at +0x120)
;; @out Updates g_os_major, g_os_minor, g_os_build via NTDLL!RtlGetVersion or PEB64 fallback
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries authentic 64-bit OS version bypassing GetVersion shims on Windows 8.1/10/11.
@proc_frame hal_read_peb_osver64, 328, rbx, rdi
    mov rax, qword [rel pfnRtlGetVersion64]
    test rax, rax
    jz .try_peb64

    ; OSVERSIONINFOW is 276 bytes; zero [rsp + 32 .. rsp + 311]
    lea rdi, [rsp + 32]
    mov rbx, rdi
    xor eax, eax
    mov ecx, 70
    rep stosd
    mov dword [rbx], 276
    mov rcx, rbx
    call qword [rel pfnRtlGetVersion64]
    test eax, eax
    jnz .try_peb64
    mov eax, dword [rbx + 4]
    mov dword [rel g_os_major], eax
    mov eax, dword [rbx + 8]
    mov dword [rel g_os_minor], eax
    mov eax, dword [rbx + 12]
    and eax, 0xFFFF
    mov dword [rel g_os_build], eax
    @epilog_ret

.try_peb64:
    ; Fallback: read PEB64 via GS:[0x60] -> +0x118/+0x11C/+0x120
    db 0x65, 0x48, 0x8B, 0x04, 0x25, 0x60, 0x00, 0x00, 0x00 ; mov rax, qword [gs:0x60]
    test rax, rax
    jz .done
    mov ecx, dword [rax + 0x118]
    test ecx, ecx
    jz .done
    mov dword [rel g_os_major], ecx
    mov edx, dword [rax + 0x11C]
    mov dword [rel g_os_minor], edx
    movzx ecx, word [rax + 0x120]
    mov dword [rel g_os_build], ecx
.done:
    @epilog_ret
@endproc

;; @proc hal_init_platform64
;; @abi win64_ms
;; @in g_sim_nt52 (1 if --nt52-api-cap)
;; @out Populates g_os_major/minor/build, g_active_cp, and post-NT-5.2 dynamic API pointers
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Detects XP Pro x64 / Server 2003 SP1 .. Windows 11 capabilities and resolves dynamic Vista..11 APIs.
@proc_frame hal_init_platform64, 32, rbx
    call qword [rel __imp_GetVersion]
    mov edx, eax
    and eax, 0xFF
    mov dword [rel g_os_major], eax
    mov eax, edx
    shr eax, 8
    and eax, 0xFF
    mov dword [rel g_os_minor], eax
    mov eax, edx
    shr eax, 16
    and eax, 0x7FFF
    mov dword [rel g_os_build], eax

    call qword [rel __imp_GetACP]
    mov dword [rel g_active_cp], eax

    lea rcx, [rel szDllKernel32_64]
    call qword [rel __imp_GetModuleHandleA]
    mov rbx, rax
    mov qword [rel g_hmod_kernel32], rbx

    ; Windows 2000 / XP Pro x64 baseline dynamic APIs (CreateHardLinkW, FindFirstFileExW)
    mov rcx, rbx
    lea rdx, [rel szFnCreateHardLinkW64]
    call dyn_resolve_proc64
    mov qword [rel pfnCreateHardLinkW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnFindFirstExW64]
    call dyn_resolve_proc64
    mov qword [rel pfnFindFirstFileExW64], rax

    ; If --nt52-api-cap simulation is active, cap OS at NT 5.2 Build 3790 (XP Pro x64 / Server 2003 SP1)
    cmp byte [rel g_sim_nt52], 0
    jne .cap_nt52

    ; Windows Vista / 7+: CreateSymbolicLinkW, GetFinalPathNameByHandleW, FindFirstStreamW, GetActiveProcessorGroupCount
    mov rcx, rbx
    lea rdx, [rel szFnCreateSymLinkW64]
    call dyn_resolve_proc64
    mov qword [rel pfnCreateSymbolicLinkW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnGetFinalPathW64]
    call dyn_resolve_proc64
    mov qword [rel pfnGetFinalPathByHandleW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnFindFirstStreamW64]
    call dyn_resolve_proc64
    mov qword [rel pfnFindFirstStreamW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnFindNextStreamW64]
    call dyn_resolve_proc64
    mov qword [rel pfnFindNextStreamW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnGetProcGroupCnt64]
    call dyn_resolve_proc64
    mov qword [rel pfnGetActiveProcGroupCnt64], rax

    ; Windows 10 1809+ / Windows 11: CreatePseudoConsole, ResizePseudoConsole, ClosePseudoConsole, GetMachineTypeAttributes
    mov rcx, rbx
    lea rdx, [rel szFnCreatePseudoCon64]
    call dyn_resolve_proc64
    mov qword [rel pfnCreatePseudoConsole64], rax

    mov rcx, rbx
    lea rdx, [rel szFnResizePseudoCon64]
    call dyn_resolve_proc64
    mov qword [rel pfnResizePseudoConsole64], rax

    mov rcx, rbx
    lea rdx, [rel szFnClosePseudoCon64]
    call dyn_resolve_proc64
    mov qword [rel pfnClosePseudoConsole64], rax

    mov rcx, rbx
    lea rdx, [rel szFnGetMachineAttr64]
    call dyn_resolve_proc64
    mov qword [rel pfnGetMachineTypeAttr64], rax

    ; Resolve NTDLL!RtlGetVersion & NtQueryInformationFile
    lea rcx, [rel szDllNtdll64]
    call qword [rel __imp_GetModuleHandleA]
    mov rbx, rax

    mov rcx, rbx
    lea rdx, [rel szFnRtlGetVersion64]
    call dyn_resolve_proc64
    mov qword [rel pfnRtlGetVersion64], rax

    mov rcx, rbx
    lea rdx, [rel szFnNtQueryInfoFile64]
    call dyn_resolve_proc64
    mov qword [rel pfnNtQueryInfoFile64], rax

    call hal_read_peb_osver64
    @epilog_ret

.cap_nt52:
    mov dword [rel g_os_major], 5
    mov dword [rel g_os_minor], 2
    mov dword [rel g_os_build], 3790
    @epilog_ret
@endproc
