;; @file hal_dyn.inc
;; @isa 386
;; @desc Progressive GetModuleHandle/LoadLibrary + GetProcAddress capability loader (NT 3.1..Windows 11 & Win9x).

@cpu 386
@format inc

;; @proc dyn_resolve_proc
;; @abi win32
;; @in EBX = HMODULE (0 -> returns 0), ESI = ASCII export name
;; @out EAX = function pointer or 0
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Resolves an export via GetProcAddress safely when HMODULE is non-NULL.
@proc dyn_resolve_proc
    test ebx, ebx
    jz .null_mod
    push esi
    push ebx
    call dword [__imp__GetProcAddress@8]
    ret
.null_mod:
    xor eax, eax
    ret
@endproc

;; @proc hal_read_peb_osver32
;; @abi win32
;; @in none (NT 3.51..11 user-mode IA-32 TEB at FS:[0x18], PEB at FS:[0x30])
;; @out Updates g_os_major, g_os_minor, g_os_build from RtlGetVersion or PEB (+0xA4/+0xA8/+0xAC)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Queries authentic OS version bypassing Windows 8.1/10/11 GetVersion manifest shims.
@proc hal_read_peb_osver32
    push ebx
    push edi

    mov eax, dword [pfnRtlGetVersion_p4]
    test eax, eax
    jz .try_peb

    ; OSVERSIONINFOW is 276 bytes; zero-fill and set dwOSVersionInfoSize = 276
    sub esp, 280
    mov edi, esp
    xor eax, eax
    mov ecx, 70
    rep stosd
    mov ebx, esp
    mov dword [ebx], 276
    push ebx
    call dword [pfnRtlGetVersion_p4]
    test eax, eax
    jnz .free_rtl
    mov eax, dword [ebx + 4]
    mov dword [g_os_major], eax
    mov eax, dword [ebx + 8]
    mov dword [g_os_minor], eax
    mov eax, dword [ebx + 12]
    and eax, 0xFFFF
    mov dword [g_os_build], eax
    add esp, 280
    pop edi
    pop ebx
    ret

.free_rtl:
    add esp, 280

.try_peb:
    cmp byte [g_is_win9x], 0
    jne .done
    mov eax, dword [fs:0x30]
    test eax, eax
    jz .done
    mov ecx, dword [eax + 0xA4]
    test ecx, ecx
    jz .done
    mov dword [g_os_major], ecx
    mov edx, dword [eax + 0xA8]
    mov dword [g_os_minor], edx
    movzx ecx, word [eax + 0xAC]
    mov dword [g_os_build], ecx

.done:
    pop edi
    pop ebx
    ret
@endproc

;; @proc hal_init_platform32
;; @abi win32
;; @in g_sim_win9x (1 if --win9x-ansi-sim), g_sim_nt31 (1 if --nt31-api-cap)
;; @out Populates g_is_win9x, g_os_major/minor/build, g_dbcs_bitmap, and NT_DYN_API pointers
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Detects Win95/98/Me vs NT 3.1..11 and resolves post-NT-3.1 APIs progressively.
@proc hal_init_platform32
    push ebx
    push esi
    push edi

    call dword [__imp__GetVersion@0]
    mov edx, eax
    and eax, 0xFF
    mov dword [g_os_major], eax
    mov eax, edx
    shr eax, 8
    and eax, 0xFF
    mov dword [g_os_minor], eax
    mov eax, edx
    shr eax, 16
    and eax, 0x7FFF
    mov dword [g_os_build], eax

    test edx, 0x80000000
    setnz al
    or al, byte [g_sim_win9x]
    mov byte [g_is_win9x], al

    ; Initialize active codepage & DBCS lead-byte bitmap
    call dword [__imp__GetACP@0]
    mov dword [g_active_cp], eax
    call cp_rebuild_dbcs32

    ; Resolve KERNEL32 dynamic exports
    push addr(szDllKernel32)
    call dword [__imp__GetModuleHandleA@4]
    mov ebx, eax
    mov dword [g_hmod_kernel32], ebx

    ; Environment strings (resolves GetEnvironmentStringsW, GetEnvironmentStringsA, GetEnvironmentStrings)
    mov esi, addr(szFnGetEnvStrW)
    call dyn_resolve_proc
    mov dword [pfnGetEnvStringsW_p0], eax

    mov esi, addr(szFnFreeEnvStrW)
    call dyn_resolve_proc
    mov dword [pfnFreeEnvStringsW_p4], eax

    mov esi, addr(szFnGetEnvStrA)
    call dyn_resolve_proc
    test eax, eax
    jnz .got_enva
    mov esi, addr(szFnGetEnvStrLegacy)
    call dyn_resolve_proc
.got_enva:
    mov dword [pfnGetEnvStringsA_p0], eax

    mov esi, addr(szFnFreeEnvStrA)
    call dyn_resolve_proc
    mov dword [pfnFreeEnvStringsA_p4], eax

    ; If --nt31-api-cap simulation is active, leave all post-NT-3.1 APIs NULL to test NT 3.1 fallbacks!
    cmp byte [g_sim_nt31], 0
    jne .cap_nt31

    ; NT 3.5 / Win95+: GetShortPathNameW / A
    mov esi, addr(szFnGetShortPathW)
    call dyn_resolve_proc
    mov dword [pfnGetShortPathNameW_p12], eax

    mov esi, addr(szFnGetShortPathA)
    call dyn_resolve_proc
    mov dword [pfnGetShortPathNameA_p12], eax

    ; NT 3.51 / Win95+: SetHandleInformation
    mov esi, addr(szFnSetHandleInfo)
    call dyn_resolve_proc
    mov dword [pfnSetHandleInfo_p12], eax

    ; NT 3.1+ (NT-only): MoveFileExW
    mov esi, addr(szFnMoveFileExW)
    call dyn_resolve_proc
    mov dword [pfnMoveFileExW_p12], eax

    ; NT 4.0 / Win95 OSR2+: GetDiskFreeSpaceExW / A, FindFirstFileExW, CopyFileExW, SwitchToThread
    mov esi, addr(szFnGetDiskFreeExW)
    call dyn_resolve_proc
    mov dword [pfnGetDiskFreeSpaceExW_p16], eax

    mov esi, addr(szFnGetDiskFreeExA)
    call dyn_resolve_proc
    mov dword [pfnGetDiskFreeSpaceExA_p16], eax

    mov esi, addr(szFnFindFirstExW)
    call dyn_resolve_proc
    mov dword [pfnFindFirstFileExW_p24], eax

    mov esi, addr(szFnCopyFileExW)
    call dyn_resolve_proc
    mov dword [pfnCopyFileExW_p24], eax

    ; Win 2000+: GetLongPathNameW, CreateHardLinkW, CreateJobObjectW
    mov esi, addr(szFnGetLongPathW)
    call dyn_resolve_proc
    mov dword [pfnGetLongPathNameW_p12], eax

    mov esi, addr(szFnCreateHardLinkW)
    call dyn_resolve_proc
    mov dword [pfnCreateHardLinkW_p12], eax

    ; Win XP / 2003+: AttachConsole, Wow64DisableWow64FsRedirection, Wow64RevertWow64FsRedirection
    mov esi, addr(szFnWow64Disable)
    call dyn_resolve_proc
    mov dword [pfnWow64DisableRedir_p4], eax

    mov esi, addr(szFnWow64Revert)
    call dyn_resolve_proc
    mov dword [pfnWow64RevertRedir_p4], eax

    ; Vista / Win 7+: CreateSymbolicLinkW, FindFirstStreamW, FindNextStreamW, GetActiveProcessorGroupCount
    mov esi, addr(szFnCreateSymLinkW)
    call dyn_resolve_proc
    mov dword [pfnCreateSymbolicLinkW_p12], eax

    mov esi, addr(szFnFindFirstStreamW)
    call dyn_resolve_proc
    mov dword [pfnFindFirstStreamW_p16], eax

    mov esi, addr(szFnFindNextStreamW)
    call dyn_resolve_proc
    mov dword [pfnFindNextStreamW_p8], eax

    mov esi, addr(szFnGetProcGroupCnt)
    call dyn_resolve_proc
    mov dword [pfnGetActiveProcGroupCnt_p0], eax

    ; Win 10 / Win 11: CreatePseudoConsole, GetMachineTypeAttributes
    mov esi, addr(szFnCreatePseudoCon)
    call dyn_resolve_proc
    mov dword [pfnCreatePseudoConsole_p20], eax

    mov esi, addr(szFnGetMachineAttr)
    call dyn_resolve_proc
    mov dword [pfnGetMachineTypeAttr_p8], eax

    ; Resolve NTDLL!RtlGetVersion & NtQueryInformationFile on NT
    cmp byte [g_is_win9x], 0
    jne .skip_ntdll
    push addr(szDllNtdll)
    call dword [__imp__GetModuleHandleA@4]
    mov ebx, eax
    mov esi, addr(szFnRtlGetVersion)
    call dyn_resolve_proc
    mov dword [pfnRtlGetVersion_p4], eax

    mov esi, addr(szFnNtQueryInfoFile)
    call dyn_resolve_proc
    mov dword [pfnNtQueryInfoFile_p20], eax

    call hal_read_peb_osver32
    jmp .done

.cap_nt31:
    mov dword [g_os_major], 3
    mov dword [g_os_minor], 10
    mov dword [g_os_build], 528

.skip_ntdll:
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc
