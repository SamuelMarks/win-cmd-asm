;; @file hal_a64.inc
;; @isa arm64
;; @desc Windows 10 & Windows 11 ARM64 (0xAA64 PE32+, Subsystem 10.0) HAL, X18 TEB/PEB, PAC/BTI, SEH/Ctrl+C, CreatePipe, & dynamic APIs.

@cpu arm64
@format inc

;; @proc a64_ctrl_handler
;; @abi win_arm64
;; @in w0 = dwCtrlType (0=CTRL_C_EVENT, 1=CTRL_BREAK_EVENT, 2=CTRL_CLOSE_EVENT)
;; @out w0 = 1 (TRUE = handled) or 0
;; @err none
;; @clobbers x0, x1, x2
;; @desc Thread-safe SetConsoleCtrlHandler callback latching bCtrlBreak with DMB ISH barrier.
@proc a64_ctrl_handler
    hint #34
    cmp w0, #2
    b.hi .unhandled
    adr_sym x1, g_a64_ctx
    mov w2, #1
    dmb ish
    strb w2, [x1, #ARM64_CTX.bCtrlBreak]
    dmb ish
    mov w0, #1
    ret
.unhandled:
    mov w0, #0
    ret
@endproc

;; @proc a64_seh_filter
;; @abi win_arm64
;; @in x0 = EXCEPTION_POINTERS*
;; @out w0 = 1 (EXCEPTION_EXECUTE_HANDLER)
;; @err none
;; @clobbers x0
;; @desc Top-level SetUnhandledExceptionFilter guard preventing silent crashes on external/device faults.
@proc a64_seh_filter
    hint #34
    mov w0, #1
    ret
@endproc

;; @proc hal_get_ctx_a64
;; @abi win_arm64
;; @in none
;; @out x0 = pointer to g_a64_ctx in .bss
;; @err none
;; @clobbers x0
;; @desc Loads the 64-bit address of g_a64_ctx via PC-relative ADRP/ADD.
@proc hal_get_ctx_a64
    adr_sym x0, g_a64_ctx
    ret
@endproc

;; @proc hal_get_str_pool_a64
;; @abi win_arm64
;; @in none
;; @out x0 = pointer to g_a64_str_pool in .rdata
;; @err none
;; @clobbers x0
;; @desc Loads the 64-bit address of g_a64_str_pool via PC-relative ADRP/ADD.
@proc hal_get_str_pool_a64
    adr_sym x0, g_a64_str_pool
    ret
@endproc

;; @proc hal_virtual_alloc_a64
;; @abi win_arm64
;; @in x0 = lpAddress, x1 = dwSize, w2 = flAllocationType, w3 = flProtect
;; @out x0 = allocated base address or 0
;; @err x0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Calls KERNEL32!VirtualAlloc (zero CRT).
@proc hal_virtual_alloc_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_VirtualAlloc
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_virtual_free_a64
;; @abi win_arm64
;; @in x0 = lpAddress, x1 = dwSize, w2 = dwFreeType
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Calls KERNEL32!VirtualFree.
@proc hal_virtual_free_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_VirtualFree
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_populate_find_a64
;; @abi win_arm64
;; @in x0 = WIN32_FIND_DATAW* (592 bytes), x1 = FIND_INFO_A64* (320 bytes)
;; @out Populates FIND_INFO_A64 (attrs, 64-bit size, 64-bit write time, reparse tag, short & long names)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Converts Win32 WIN32_FIND_DATAW into normalized 320-byte FIND_INFO_A64.
@proc hal_populate_find_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov x20, x1

    ldr w0, [x19, #0] ; dwFileAttributes
    str w0, [x20, #FIND_INFO_A64.dwAttr]
    str wzr, [x20, #FIND_INFO_A64.dwPad0]

    ldr w1, [x19, #28] ; nFileSizeHigh
    ldr w0, [x19, #32] ; nFileSizeLow
    orr x0, x0, x1, lsl #32
    str x0, [x20, #FIND_INFO_A64.uSize]

    ldr w0, [x19, #20] ; ftLastWriteTime.dwLowDateTime
    ldr w1, [x19, #24] ; ftLastWriteTime.dwHighDateTime
    orr x0, x0, x1, lsl #32
    str x0, [x20, #FIND_INFO_A64.uWriteTime]

    ldr w0, [x19, #36] ; dwReserved0 = ReparseTag when ATTR_REPARSE_POINT set
    str w0, [x20, #FIND_INFO_A64.dwReparseTag]

    add x0, x19, #532 ; cAlternateFileName[14]
    mov x1, #14
    add x2, x20, #FIND_INFO_A64.szShort
    bl u16_to_wtf8_buf_a64

    add x0, x19, #44 ; cFileName[260]
    mov x1, #260
    add x2, x20, #FIND_INFO_A64.szName
    bl u16_to_wtf8_buf_a64

    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_resolve_dyn_apis_a64
;; @abi win_arm64
;; @in x0 = ARM64_CTX*
;; @out Populates dynamic NTDLL, KERNEL32, and ADVAPI32 function pointers in ARM64_CTX
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Resolves RtlGetVersion, NtQueryInformationFile, CreateSymbolicLinkW, GetMachineTypeAttributes, and Registry APIs.
@proc hal_resolve_dyn_apis_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    ; KERNEL32.DLL dynamic Win10/11 exports
    adr_sym x0, u16Kernel32_a64
    call_iat __imp_GetModuleHandleW
    mov x20, x0
    cbz x20, .ntdll

    mov x0, x20
    adr_sym x1, szCreateSymbolicLinkW_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnCreateSymbolicLinkW]

    mov x0, x20
    adr_sym x1, szCreateHardLinkW_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnCreateHardLinkW]

    mov x0, x20
    adr_sym x1, szCreatePseudoConsole_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnCreatePseudoConsole]

    mov x0, x20
    adr_sym x1, szGetMachineTypeAttributes_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnGetMachineTypeAttributes]

.ntdll:
    adr_sym x0, u16Ntdll_a64
    call_iat __imp_GetModuleHandleW
    mov x20, x0
    cbz x20, .advapi

    mov x0, x20
    adr_sym x1, szRtlGetVersion_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnRtlGetVersion]

    mov x0, x20
    adr_sym x1, szNtQueryInformationFile_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnNtQueryInformationFile]

.advapi:
    adr_sym x0, u16Advapi32_a64
    call_iat __imp_LoadLibraryW
    mov x20, x0
    cbz x20, .done

    mov x0, x20
    adr_sym x1, szRegOpenKeyExW_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnRegOpenKeyExW]

    mov x0, x20
    adr_sym x1, szRegCreateKeyExW_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnRegCreateKeyExW]

    mov x0, x20
    adr_sym x1, szRegQueryValueExW_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnRegQueryValueExW]

    mov x0, x20
    adr_sym x1, szRegSetValueExW_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnRegSetValueExW]

    mov x0, x20
    adr_sym x1, szRegCloseKey_a64
    call_iat __imp_GetProcAddress
    str x0, [x19, #ARM64_CTX.pfnRegCloseKey]

.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_init_platform_a64
;; @abi win_arm64
;; @in none
;; @out w0 = 0 on success, ERR_OUT_OF_MEMORY on failure
;; @err w0 != 0 on arena allocation failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Initializes Windows 10/11 ARM64 X18 TEB/PEB OS version, SEH/Ctrl+C, VT/ConPTY console, CWD, and environment.
@proc hal_init_platform_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #560
    mov x29, sp

    bl hal_get_ctx_a64
    mov x19, x0

    mov x0, x19
    mov x1, #2560
    bl mem_zero_a64

    bl arena_init_a64
    cbnz w0, .init_fail

    mov w1, #1
    strb w1, [x19, #ARM64_CTX.bEchoOn]
    strb w1, [x19, #ARM64_CTX.bExtensions]
    strb w1, [x19, #ARM64_CTX.bFileCompl]
    strb w1, [x19, #ARM64_CTX.bBreakOn]
    mov w1, #0x07
    strh w1, [x19, #ARM64_CTX.wDefaultAttr]
    strh w1, [x19, #ARM64_CTX.wCurrentAttr]

    ; Read Windows ARM64 TEB directly from X18 (read-only!) -> 64-bit PEB at [X18 + 0x60]
    cbz x18, .def_a64_ver
    ldr x2, [x18, #0x60]
    cbz x2, .def_a64_ver
    ldr w0, [x2, #0x118] ; PEB.OSMajorVersion (64-bit PEB offset)
    cbz w0, .def_a64_ver
    str w0, [x19, #ARM64_CTX.dwOsMajor]
    ldr w0, [x2, #0x11C] ; PEB.OSMinorVersion
    str w0, [x19, #ARM64_CTX.dwOsMinor]
    ldrh w0, [x2, #0x120] ; PEB.OSBuildNumber
    str w0, [x19, #ARM64_CTX.dwOsBuild]
    b .after_peb
.def_a64_ver:
    mov w0, #10
    str w0, [x19, #ARM64_CTX.dwOsMajor]
    str wzr, [x19, #ARM64_CTX.dwOsMinor]
    mov_imm32 w0, 22631
    str w0, [x19, #ARM64_CTX.dwOsBuild]

.after_peb:
    ; Register SEH unhandled filter + Ctrl+C / Ctrl+Break handler
    adr_sym x0, a64_seh_filter
    call_iat __imp_SetUnhandledExceptionFilter
    adr_sym x0, a64_ctrl_handler
    mov w1, #1
    call_iat __imp_SetConsoleCtrlHandler

    ; Standard handles (-10, -11, -12)
    mov w0, #-10 ; STD_INPUT_HANDLE
    call_iat __imp_GetStdHandle
    str x0, [x19, #ARM64_CTX.hStdIn]
    mov w0, #-11 ; STD_OUTPUT_HANDLE
    call_iat __imp_GetStdHandle
    str x0, [x19, #ARM64_CTX.hStdOut]
    mov w0, #-12 ; STD_ERROR_HANDLE
    call_iat __imp_GetStdHandle
    str x0, [x19, #ARM64_CTX.hStdErr]

    add x2, x19, #ARM64_CTX.fdTable
    ldr x0, [x19, #ARM64_CTX.hStdIn]
    str x0, [x2, #0]
    ldr x0, [x19, #ARM64_CTX.hStdOut]
    str x0, [x2, #8]
    ldr x0, [x19, #ARM64_CTX.hStdErr]
    str x0, [x2, #16]

    ; Enable Windows 10 / 11 Virtual Terminal Processing (0x0004) on hStdOut when console
    ldr x0, [x19, #ARM64_CTX.hStdOut]
    mov x1, sp
    call_iat __imp_GetConsoleMode
    cbz w0, .no_vt
    ldr w1, [sp, #0]
    orr w1, w1, #0x0004 ; ENABLE_VIRTUAL_TERMINAL_PROCESSING
    ldr x0, [x19, #ARM64_CTX.hStdOut]
    call_iat __imp_SetConsoleMode
    cbz w0, .no_vt
    mov w0, #1
    strb w0, [x19, #ARM64_CTX.bVtEnabled]
.no_vt:
    ldr x0, [x19, #ARM64_CTX.hStdOut]
    add x1, sp, #16
    call_iat __imp_GetConsoleScreenBufferInfo
    cbz w0, .no_csbi
    ldrh w0, [sp, #24] ; wAttributes at +8
    strh w0, [x19, #ARM64_CTX.wDefaultAttr]
    strh w0, [x19, #ARM64_CTX.wCurrentAttr]
.no_csbi:
    call_iat __imp_GetConsoleOutputCP
    cbnz w0, .got_cp
    mov_imm32 w0, 65001
.got_cp:
    str w0, [x19, #ARM64_CTX.dwCodePage]

    call_iat __imp_GetTickCount
    str w0, [x19, #ARM64_CTX.dwRandomSeed]

    mov x0, x19
    bl hal_resolve_dyn_apis_a64

    ; Probe Windows 11 ARM64 native & Prism x64 machine type attributes when available
    ldr x16, [x19, #ARM64_CTX.pfnGetMachineTypeAttributes]
    cbz x16, .skip_mta
    mov_imm32 w0, 0xAA64
    mov x1, sp
    blr x16
.skip_mta:

    ; Capture GetCommandLineW into permanent pool
    call_iat __imp_GetCommandLineW
    mov x20, x0
    cbz x20, .def_cmd
    mov x0, #0
.cmd_wlen:
    lsl x2, x0, #1
    ldrh w1, [x20, x2]
    cbz w1, .cmd_alloc
    add x0, x0, #1
    b .cmd_wlen
.cmd_alloc:
    mov x21, x0
    mov x2, #3
    mul x0, x21, x2
    add x0, x0, #16
    bl perm_alloc_a64
    cbz x0, .def_cmd
    mov x22, x0
    mov x2, x22
    mov x0, x20
    mov x1, x21
    bl u16_to_wtf8_buf_a64
    str x22, [x19, #ARM64_CTX.pCmdLineRaw]
    b .got_cmd
.def_cmd:
    adr_sym x0, szEmptyStr_a64
    str x0, [x19, #ARM64_CTX.pCmdLineRaw]
.got_cmd:

    ; Capture GetModuleFileNameW(NULL, ...) into permanent pool
    mov x0, #520
    bl perm_alloc_a64
    mov x20, x0
    mov x0, #0
    add x1, sp, #32
    mov w2, #256
    call_iat __imp_GetModuleFileNameW
    cbz w0, .def_self
    uxtw x1, w0
    add x0, sp, #32
    mov x2, x20
    bl u16_to_wtf8_buf_a64
    str x20, [x19, #ARM64_CTX.pSelfExePath]
    b .got_self
.def_self:
    adr_sym x0, szA64DefSelf
    str x0, [x19, #ARM64_CTX.pSelfExePath]
.got_self:

    ; Capture GetCurrentDirectoryW into szCwd and drvCwdsPtrs
    mov w0, #256
    add x1, sp, #32
    call_iat __imp_GetCurrentDirectoryW
    cbz w0, .def_cwd
    uxtw x1, w0
    add x0, sp, #32
    add x2, x19, #ARM64_CTX.szCwd
    bl u16_to_wtf8_buf_a64
    b .sync_drv
.def_cwd:
    add x0, x19, #ARM64_CTX.szCwd
    adr_sym x1, szA64DefCwd
    bl str_copy_a64
.sync_drv:
    add x20, x19, #ARM64_CTX.szCwd
    ldrb w0, [x20, #0]
    bl ascii_upper_a64
    sub w0, w0, #0x41
    cmp w0, #26
    b.hs .load_env
    mov w21, w0
    mov x0, #512
    bl perm_alloc_a64
    cbz x0, .load_env
    mov x22, x0
    mov x1, x20
    bl str_copy_a64
    uxtw x1, w21
    lsl x1, x1, #3
    add x2, x19, #ARM64_CTX.drvCwdsPtrs
    str x22, [x2, x1]

.load_env:
    ; Import UTF-16LE environment block from GetEnvironmentStringsW into pEnvBlock
    call_iat __imp_GetEnvironmentStringsW
    mov x20, x0
    cbz x20, .fallback_env
    ldrh w0, [x20, #0]
    cbz w0, .free_and_fb

    ldr x21, [x19, #ARM64_CTX.pEnvBlock]
    mov x22, x20 ; cursor in UTF-16LE env
.env_lp:
    ldrh w0, [x22, #0]
    cbz w0, .env_fin
    mov x0, #0
.env_slen:
    lsl x2, x0, #1
    ldrh w1, [x22, x2]
    cbz w1, .env_conv
    add x0, x0, #1
    b .env_slen
.env_conv:
    mov x4, x0
    stp x4, xzr, [sp, #-16]!
    mov x0, x22
    mov x1, x4
    mov x2, x21
    bl u16_to_wtf8_buf_a64
    ldp x4, xzr, [sp], #16
    mov x0, x21
    bl str_len_a64
    add x21, x21, x0
    add x21, x21, #1
    add x4, x4, #1
    add x22, x22, x4, lsl #1
    b .env_lp
.env_fin:
    strb wzr, [x21, #0]
    add x21, x21, #1
    ldr x0, [x19, #ARM64_CTX.pEnvBlock]
    sub x1, x21, x0
    str w1, [x19, #ARM64_CTX.dwEnvLen]
    mov x0, x20
    call_iat __imp_FreeEnvironmentStringsW
    b .init_ok

.free_and_fb:
    mov x0, x20
    call_iat __imp_FreeEnvironmentStringsW
.fallback_env:
    ldr x20, [x19, #ARM64_CTX.pEnvBlock]
    adr_sym x21, szA64DefaultEnv
    mov x22, #0
.fb_lp:
    ldrb w0, [x21, x22]
    strb w0, [x20, x22]
    add x22, x22, #1
    cbnz w0, .fb_lp
    ldrb w0, [x21, x22]
    strb w0, [x20, x22]
    add x22, x22, #1
    cbnz w0, .fb_lp
    str w22, [x19, #ARM64_CTX.dwEnvLen]

.init_ok:
    mov w0, #0
    b .exit
.init_fail:
    mov w0, #ERR_OUT_OF_MEMORY
.exit:
    add sp, sp, #560
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_write_handle_a64
;; @abi win_arm64
;; @in x0 = HANDLE, x1 = buffer, w2 = byte length
;; @out w0 = bytes written
;; @err w0 = 0 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes raw bytes to an OS file/pipe/console handle via KERNEL32!WriteFile.
@proc hal_write_handle_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp
    str wzr, [sp, #0]
    mov x3, sp
    mov x4, #0
    call_iat __imp_WriteFile
    cbz w0, .err
    ldr w0, [sp, #0]
    b .done
.err:
    mov w0, #0
.done:
    add sp, sp, #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_write_std_a64
;; @abi win_arm64
;; @in x0 = HANDLE, x1 = WTF-8 bytes, x2 = byte length
;; @out w0 = 1 on success
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes WTF-8 text via WriteConsoleW when handle is a console, or WriteFile (UTF-16LE under /U, else UTF-8) for files/pipes.
@proc hal_write_std_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp
    mov x19, x0
    mov x20, x1
    mov x21, x2

    cbz x21, .ok

    ; Test if handle is console via GetConsoleMode
    mov x0, x19
    mov x1, sp
    call_iat __imp_GetConsoleMode
    cbz w0, .not_con

    ; Convert slice to NUL-terminated copy in arena -> UTF-16LE -> WriteConsoleW
    add x0, x21, #4
    bl arena_alloc_a64
    cbz x0, .raw_fb
    mov x22, x0
    mov x1, x20
    mov x2, x21
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x22
    bl wtf8_to_u16_a64
    cbz x0, .raw_fb
    mov w2, w1
    mov x1, x0
    mov x0, x19
    add x3, sp, #8
    mov x4, #0
    call_iat __imp_WriteConsoleW
    b .ok

.not_con:
    bl hal_get_ctx_a64
    ldrb w0, [x0, #ARM64_CTX.bUnicodeOut]
    cbz w0, .raw_fb
    ; /U active: transcode to UTF-16LE before WriteFile!
    add x0, x21, #4
    bl arena_alloc_a64
    cbz x0, .raw_fb
    mov x22, x0
    mov x1, x20
    mov x2, x21
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x22
    bl wtf8_to_u16_a64
    cbz x0, .raw_fb
    lsl w2, w1, #1
    mov x1, x0
    mov x0, x19
    bl hal_write_handle_a64
    b .ok

.raw_fb:
    mov x0, x19
    mov x1, x20
    mov w2, w21
    bl hal_write_handle_a64
.ok:
    mov w0, #1
    add sp, sp, #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_read_handle_a64
;; @abi win_arm64
;; @in x0 = HANDLE, x1 = buffer, w2 = max bytes
;; @out w0 = bytes read
;; @err w0 = 0 on EOF/error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Reads raw bytes from an OS handle via KERNEL32!ReadFile.
@proc hal_read_handle_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp
    str wzr, [sp, #0]
    mov x3, sp
    mov x4, #0
    call_iat __imp_ReadFile
    cbz w0, .eof
    ldr w0, [sp, #0]
    b .done
.eof:
    mov w0, #0
.done:
    add sp, sp, #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_open_file_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE path, w1 = mode (0 = read, 1 = write-trunc, 2 = write-append)
;; @out x0 = 64-bit HANDLE (or -1 INVALID_HANDLE_VALUE on error)
;; @err x0 = -1 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Opens or creates a file with inheritable SECURITY_ATTRIBUTES via KERNEL32!CreateFileW.
@proc hal_open_file_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp
    mov x19, x0
    mov w20, w1

    ; 24-byte 64-bit SECURITY_ATTRIBUTES at sp+0: {nLength=24, lpSecurityDescriptor=0, bInheritHandle=1}
    mov w0, #24
    str w0, [sp, #0]
    str wzr, [sp, #4]
    str xzr, [sp, #8]
    mov w0, #1
    str w0, [sp, #16]
    str wzr, [sp, #20]

    cmp w20, #0
    b.eq .open_rd
    cmp w20, #1
    b.eq .open_tr

    ; Append mode (2): OPEN_ALWAYS (4), GENERIC_WRITE (0x40000000), then seek to end
    mov x0, x19
    mov_imm32 w1, 0x40000000
    mov w2, #3 ; FILE_SHARE_READ | FILE_SHARE_WRITE
    mov x3, sp
    mov w4, #4 ; OPEN_ALWAYS
    mov w5, #0x80 ; FILE_ATTRIBUTE_NORMAL
    mov x6, #0
    call_iat __imp_CreateFileW
    cmn x0, #1
    b.eq .done
    mov x19, x0
    mov x1, #0
    mov x2, #0
    mov w3, #2 ; FILE_END
    call_iat __imp_SetFilePointerEx
    mov x0, x19
    b .done

.open_tr:
    mov x0, x19
    mov_imm32 w1, 0x40000000
    mov w2, #3
    mov x3, sp
    mov w4, #2 ; CREATE_ALWAYS
    mov w5, #0x80
    mov x6, #0
    call_iat __imp_CreateFileW
    b .done

.open_rd:
    mov x0, x19
    mov_imm32 w1, 0x80000000 ; GENERIC_READ
    mov w2, #7 ; FILE_SHARE_READ | WRITE | DELETE
    mov x3, sp
    mov w4, #3 ; OPEN_EXISTING
    mov w5, #0x80
    mov x6, #0
    call_iat __imp_CreateFileW

.done:
    add sp, sp, #32
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_seek_file_a64
;; @abi win_arm64
;; @in x0 = HANDLE, x1 = signed 64-bit offset, w2 = method (0=BEGIN, 1=CURRENT, 2=END)
;; @out x0 = new 64-bit file offset
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Seeks a 64-bit file handle via KERNEL32!SetFilePointerEx.
@proc hal_seek_file_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp
    str xzr, [sp, #0]
    mov w3, w2
    mov x2, sp
    call_iat __imp_SetFilePointerEx
    ldr x0, [sp, #0]
    add sp, sp, #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_close_handle_a64
;; @abi win_arm64
;; @in x0 = HANDLE
;; @out w0 = BOOL
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Closes a non-standard handle via KERNEL32!CloseHandle.
@proc hal_close_handle_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    cbz x19, .skip
    cmn x19, #1
    b.eq .skip
    bl hal_get_ctx_a64
    ldr x1, [x0, #ARM64_CTX.hStdIn]
    cmp x19, x1
    b.eq .skip
    ldr x1, [x0, #ARM64_CTX.hStdOut]
    cmp x19, x1
    b.eq .skip
    ldr x1, [x0, #ARM64_CTX.hStdErr]
    cmp x19, x1
    b.eq .skip
    mov x0, x19
    call_iat __imp_CloseHandle
    b .done
.skip:
    mov w0, #1
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_get_file_attr_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE path
;; @out w0 = dwFileAttributes (0xFFFFFFFF if missing)
;; @err w0 = 0xFFFFFFFF on missing path
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries file/directory attributes via KERNEL32!GetFileAttributesW.
@proc hal_get_file_attr_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_GetFileAttributesW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_set_file_attr_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE path, w1 = dwFileAttributes
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Updates file attributes via KERNEL32!SetFileAttributesW.
@proc hal_set_file_attr_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_SetFileAttributesW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_delete_file_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE path
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Deletes a file via KERNEL32!DeleteFileW.
@proc hal_delete_file_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_DeleteFileW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_mkdir_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE path
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Creates a directory via KERNEL32!CreateDirectoryW.
@proc hal_mkdir_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x1, #0
    call_iat __imp_CreateDirectoryW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_rmdir_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE path
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Removes an empty directory or unlinks a directory junction/symlink via KERNEL32!RemoveDirectoryW.
@proc hal_rmdir_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_RemoveDirectoryW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_move_file_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE src, x1 = UTF-16LE dst
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Moves or renames a file/directory via KERNEL32!MoveFileExW (REPLACE_EXISTING | COPY_ALLOWED).
@proc hal_move_file_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov w2, #3 ; MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED
    call_iat __imp_MoveFileExW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_copy_file_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE src, x1 = UTF-16LE dst
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Copies a file via KERNEL32!CopyFileW (bFailIfExists = FALSE).
@proc hal_copy_file_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov w2, #0
    call_iat __imp_CopyFileW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_find_first_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE wildcard pattern, x1 = FIND_INFO_A64*
;; @out x0 = search handle (or -1 on error)
;; @err x0 = -1 when no matching entries exist
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Starts directory enumeration via KERNEL32!FindFirstFileW.
@proc hal_find_first_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #608
    mov x29, sp
    mov x19, x1

    mov x1, sp
    call_iat __imp_FindFirstFileW
    cmn x0, #1
    b.eq .done
    mov x20, x0
    mov x0, sp
    mov x1, x19
    bl hal_populate_find_a64
    mov x0, x20
.done:
    add sp, sp, #608
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_find_next_a64
;; @abi win_arm64
;; @in x0 = search handle, x1 = FIND_INFO_A64*
;; @out w0 = 1 if next entry populated, 0 when exhausted
;; @err w0 = 0 on exhaustion
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Advances directory enumeration via KERNEL32!FindNextFileW.
@proc hal_find_next_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #608
    mov x29, sp
    mov x19, x1

    mov x1, sp
    call_iat __imp_FindNextFileW
    cbz w0, .done
    mov x0, sp
    mov x1, x19
    bl hal_populate_find_a64
    mov w0, #1
.done:
    add sp, sp, #608
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_find_close_a64
;; @abi win_arm64
;; @in x0 = search handle
;; @out w0 = BOOL
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Closes a search handle via KERNEL32!FindClose.
@proc hal_find_close_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_FindClose
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_get_local_time_a64
;; @abi win_arm64
;; @in x0 = SYSTEMTIME* (16 bytes)
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries local wall-clock time via KERNEL32!GetLocalTime.
@proc hal_get_local_time_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_GetLocalTime
    mov w0, #0
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_set_local_time_a64
;; @abi win_arm64
;; @in x0 = SYSTEMTIME* (16 bytes)
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Sets local wall-clock time via KERNEL32!SetLocalTime.
@proc hal_set_local_time_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_SetLocalTime
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_get_tick_count_a64
;; @abi win_arm64
;; @in none
;; @out w0 = millisecond tick count
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries millisecond uptime via KERNEL32!GetTickCount.
@proc hal_get_tick_count_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_GetTickCount
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_get_disk_free_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE directory, x1 = 8-byte u64 output pointer
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries 64-bit disk free bytes via KERNEL32!GetDiskFreeSpaceExW.
@proc hal_get_disk_free_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp
    add x2, sp, #0
    add x3, sp, #8
    call_iat __imp_GetDiskFreeSpaceExW
    add sp, sp, #32
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_set_console_attr_a64
;; @abi win_arm64
;; @in w0 = 16-bit console attribute
;; @out w0 = BOOL
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Sets console foreground/background attribute via KERNEL32!SetConsoleTextAttribute.
@proc hal_set_console_attr_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov w19, w0
    bl hal_get_ctx_a64
    strh w19, [x0, #ARM64_CTX.wCurrentAttr]
    ldr x0, [x0, #ARM64_CTX.hStdOut]
    mov w1, w19
    call_iat __imp_SetConsoleTextAttribute
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_clear_console_a64
;; @abi win_arm64
;; @in none
;; @out w0 = 1
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Clears console screen via VT `ESC[2J ESC[H` when ConPTY/VT enabled, or FillConsoleOutputCharacterW + SetConsoleCursorPosition.
@proc hal_clear_console_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp
    bl hal_get_ctx_a64
    mov x19, x0
    ldrb w1, [x19, #ARM64_CTX.bVtEnabled]
    cbz w1, .classic_cls
    adr_sym x0, szA64VtClear
    bl out_str_a64
    b .done
.classic_cls:
    ldr x0, [x19, #ARM64_CTX.hStdOut]
    mov w1, #0x20
    mov_imm32 w2, 8000
    mov w3, #0
    mov x4, sp
    call_iat __imp_FillConsoleOutputCharacterW
    ldr x0, [x19, #ARM64_CTX.hStdOut]
    mov w1, #0
    call_iat __imp_SetConsoleCursorPosition
.done:
    mov w0, #1
    add sp, sp, #32
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_set_title_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE title string
;; @out w0 = BOOL
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Updates console window title via KERNEL32!SetConsoleTitleW.
@proc hal_set_title_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_SetConsoleTitleW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_get_set_cp_a64
;; @abi win_arm64
;; @in w0 = new codepage (or 0 to query active codepage)
;; @out w0 = active codepage
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries or sets console input/output codepage (including CP65001 UTF-8).
@proc hal_get_set_cp_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov w19, w0
    cbz w19, .query
    mov w0, w19
    call_iat __imp_SetConsoleCP
    mov w0, w19
    call_iat __imp_SetConsoleOutputCP
    bl hal_get_ctx_a64
    str w19, [x0, #ARM64_CTX.dwCodePage]
    mov w0, w19
    b .done
.query:
    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwCodePage]
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_mklink_a64
;; @abi win_arm64
;; @in w0 = kind (0=file symlink, 1=/D dir symlink, 2=/H hardlink, 3=/J junction), x1 = UTF-16LE link, x2 = UTF-16LE target
;; @out w0 = 0 on success or ERR_ACCESS_DENIED
;; @err w0 != 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Creates file/dir symlinks (with Win10+ ALLOW_UNPRIVILEGED_CREATE), hard links, or NTFS directory junctions.
@proc hal_mklink_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #560
    mov x29, sp
    mov w19, w0
    mov x20, x1
    mov x21, x2

    cmp w19, #2
    b.eq .hard
    cmp w19, #3
    b.eq .junc

    ; Symlink (file=0, /D=1) via CreateSymbolicLinkW with SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (0x2)
    bl hal_get_ctx_a64
    ldr x22, [x0, #ARM64_CTX.pfnCreateSymbolicLinkW]
    cbz x22, .fail_mk
    and w2, w19, #1
    orr w2, w2, #2 ; SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    mov x0, x20
    mov x1, x21
    blr x22
    cbnz w0, .ok_mk
    ; Retry without unprivileged flag
    and w2, w19, #1
    mov x0, x20
    mov x1, x21
    blr x22
    cbz w0, .fail_mk
    b .ok_mk

.hard:
    bl hal_get_ctx_a64
    ldr x22, [x0, #ARM64_CTX.pfnCreateHardLinkW]
    cbz x22, .fail_mk
    mov x0, x20
    mov x1, x21
    mov x2, #0
    blr x22
    cbz w0, .fail_mk
    b .ok_mk

.junc:
    ; Create directory + FSCTL_SET_REPARSE_POINT (IO_REPARSE_TAG_MOUNT_POINT = 0xA0000003)
    mov x0, x20
    mov x1, #0
    call_iat __imp_CreateDirectoryW

    mov x0, x20
    mov_imm32 w1, 0x40000000 ; GENERIC_WRITE
    mov w2, #0
    mov x3, #0
    mov w4, #3 ; OPEN_EXISTING
    mov_imm32 w5, 0x02200000 ; FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT
    mov x6, #0
    call_iat __imp_CreateFileW
    cmn x0, #1
    b.eq .fail_mk
    mov x22, x0 ; hDir

    ; Zero 512-byte REPARSE_DATA_BUFFER at sp+16
    add x0, sp, #16
    mov x1, #512
    bl mem_zero_a64

    mov_imm32 w0, 0xA0000003 ; IO_REPARSE_TAG_MOUNT_POINT
    str w0, [sp, #16]        ; ReparseTag (+0)

    ; Copy `\??` + target UTF-16LE into PathBuffer at sp+36 (+20 from buffer start)
    add x1, sp, #36
    mov w0, #0x5C
    strh w0, [x1, #0]
    mov w0, #0x3F
    strh w0, [x1, #2]
    strh w0, [x1, #4]
    mov w0, #0x5C
    strh w0, [x1, #6]
    add x1, x1, #8
    mov x2, x21
    mov w3, #8 ; byte length of SubstituteName
.j_cp:
    ldrh w0, [x2, #0]
    cbz w0, .j_cp_done
    strh w0, [x1, #0]
    add x1, x1, #2
    add x2, x2, #2
    add w3, w3, #2
    cmp w3, #480
    b.lo .j_cp
.j_cp_done:
    strh wzr, [x1, #0]
    strh w3, [sp, #26] ; SubstituteNameLength (+10)
    add w0, w3, #2
    strh w0, [sp, #28] ; PrintNameOffset (+12)
    add w0, w3, #12
    strh w0, [sp, #20] ; ReparseDataLength (+4)
    add w3, w0, #8     ; nInBufferSize

    mov x0, x22
    mov_imm32 w1, 0x000900A4 ; FSCTL_SET_REPARSE_POINT
    add x2, sp, #16
    mov x4, #0
    mov w5, #0
    add x6, sp, #536
    mov x7, #0
    call_iat __imp_DeviceIoControl
    mov w19, w0
    mov x0, x22
    call_iat __imp_CloseHandle
    cbz w19, .fail_mk

.ok_mk:
    mov w0, #0
    b .exit_mk
.fail_mk:
    mov w0, #ERR_ACCESS_DENIED
.exit_mk:
    add sp, sp, #560
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_resolve_appexeclink_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE candidate path, x1 = output WTF-8 resolved path buffer (512B)
;; @out w0 = 1 if IO_REPARSE_TAG_APPEXECLINK resolved, else 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Resolves Windows 10/11 WindowsApps IO_REPARSE_TAG_APPEXECLINK (0x8000001B) execution aliases.
@proc hal_resolve_appexeclink_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #544
    mov x29, sp
    mov x19, x1

    mov_imm32 w1, 0x00000080 ; FILE_READ_ATTRIBUTES
    mov w2, #7
    mov x3, #0
    mov w4, #3 ; OPEN_EXISTING
    mov_imm32 w5, 0x02200000 ; BACKUP_SEMANTICS | OPEN_REPARSE_POINT
    mov x6, #0
    call_iat __imp_CreateFileW
    cmn x0, #1
    b.eq .not_ael
    mov x20, x0

    mov_imm32 w1, 0x000900A8 ; FSCTL_GET_REPARSE_POINT
    mov x2, #0
    mov w3, #0
    add x4, sp, #16
    mov w5, #512
    mov x6, sp
    mov x7, #0
    call_iat __imp_DeviceIoControl
    mov w2, w0
    stp x2, xzr, [sp, #-16]!
    mov x0, x20
    call_iat __imp_CloseHandle
    ldp x2, xzr, [sp], #16
    cbz w2, .not_ael

    ldr w0, [sp, #16]
    mov_imm32 w1, 0x8000001B ; IO_REPARSE_TAG_APPEXECLINK
    cmp w0, w1
    b.ne .not_ael

    ; AppExecLink payload at +8 (+12 after version ULONG): pkgId\0 appUserModelId\0 targetExe\0
    add x0, sp, #28
    mov w2, #0 ; skip 2 NUL-terminated UTF-16LE strings to reach 3rd string (target exe)
.skp_s:
    cmp w2, #2
    b.eq .got_tgt
    ldrh w1, [x0, #0]
    add x0, x0, #2
    cbnz w1, .skp_s
    add w2, w2, #1
    b .skp_s
.got_tgt:
    mov x1, #240
    mov x2, x19
    bl u16_to_wtf8_buf_a64
    mov w0, #1
    b .done
.not_ael:
    mov w0, #0
.done:
    add sp, sp, #544
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_reg_query_str_a64
;; @abi win_arm64
;; @in x0 = hRoot, x1 = UTF-16LE subkey, x2 = UTF-16LE valname (or 0), x3 = WTF-8 out buf (512B)
;; @out w0 = 0 on success or ERR_FILE_NOT_FOUND
;; @err w0 = ERR_FILE_NOT_FOUND when missing
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries a REG_SZ/REG_EXPAND_SZ Registry value via dynamically loaded ADVAPI32.DLL.
@proc hal_reg_query_str_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #560
    mov x29, sp
    mov x19, x2 ; valname
    mov x20, x3 ; out buf
    mov x21, x0 ; hRoot
    mov x22, x1 ; subkey

    bl hal_get_ctx_a64
    ldr x16, [x0, #ARM64_CTX.pfnRegOpenKeyExW]
    cbz x16, .nf

    str xzr, [sp, #0] ; hKey (8 bytes)
    mov x0, x21
    mov x1, x22
    mov w2, #0
    mov_imm32 w3, 0x00020019 ; KEY_READ
    mov x4, sp
    blr x16
    cbnz w0, .nf

    ldr x21, [sp, #0] ; hKey
    mov w0, #512
    str w0, [sp, #8]  ; cbData
    str wzr, [sp, #12] ; dwType
    bl hal_get_ctx_a64
    ldr x16, [x0, #ARM64_CTX.pfnRegQueryValueExW]
    mov x0, x21
    mov x1, x19
    mov x2, #0
    add x3, sp, #12 ; &dwType
    add x4, sp, #32 ; lpData
    add x5, sp, #8  ; &cbData
    blr x16
    mov w19, w0

    bl hal_get_ctx_a64
    ldr x16, [x0, #ARM64_CTX.pfnRegCloseKey]
    mov x0, x21
    blr x16
    cbnz w19, .nf

    add x0, sp, #32
    mov x1, #255
    mov x2, x20
    bl u16_to_wtf8_buf_a64
    mov w0, #0
    b .done
.nf:
    mov w0, #ERR_FILE_NOT_FOUND
.done:
    add sp, sp, #560
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_reg_set_str_a64
;; @abi win_arm64
;; @in x0 = hRoot, x1 = UTF-16LE subkey, x2 = UTF-16LE valname (or 0), x3 = UTF-16LE data
;; @out w0 = 0 on success or ERR_ACCESS_DENIED
;; @err w0 = ERR_ACCESS_DENIED on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes a REG_SZ Registry value via dynamically loaded ADVAPI32.DLL.
@proc hal_reg_set_str_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #48
    mov x29, sp
    mov x19, x2 ; valname
    mov x20, x3 ; u16 data
    mov x21, x0 ; hRoot
    mov x22, x1 ; subkey

    bl hal_get_ctx_a64
    ldr x16, [x0, #ARM64_CTX.pfnRegCreateKeyExW]
    cbz x16, .den

    str xzr, [sp, #16] ; hKey
    str wzr, [sp, #24] ; dwDisposition
    add x0, sp, #24
    str x0, [sp, #0]   ; 9th arg: lpdwDisposition
    mov x0, x21
    mov x1, x22
    mov w2, #0
    mov x3, #0
    mov w4, #0
    mov_imm32 w5, 0x00020006 ; KEY_WRITE
    mov x6, #0
    add x7, sp, #16    ; 8th arg: phkResult
    blr x16
    cbnz w0, .den

    ldr x21, [sp, #16] ; hKey
    mov w22, #0
.wlen:
    uxtw x2, w22
    ldrh w1, [x20, x2]
    add w22, w22, #2
    cbnz w1, .wlen

    bl hal_get_ctx_a64
    ldr x16, [x0, #ARM64_CTX.pfnRegSetValueExW]
    mov x0, x21
    mov x1, x19
    mov w2, #0
    mov w3, #1 ; REG_SZ
    mov x4, x20
    mov w5, w22
    blr x16
    mov w19, w0

    bl hal_get_ctx_a64
    ldr x16, [x0, #ARM64_CTX.pfnRegCloseKey]
    mov x0, x21
    blr x16
    cbnz w19, .den
    mov w0, #0
    b .done
.den:
    mov w0, #ERR_ACCESS_DENIED
.done:
    add sp, sp, #48
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_build_u16_env_a64
;; @abi win_arm64
;; @in none
;; @out x0 = UTF-16LE double-NUL environment block in arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Transcodes active WTF-8 environment block into CREATE_UNICODE_ENVIRONMENT UTF-16LE block.
@proc hal_build_u16_env_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    bl hal_get_ctx_a64
    ldr x19, [x0, #ARM64_CTX.pEnvBlock]
    ldr w20, [x0, #ARM64_CTX.dwEnvLen]
    uxtw x0, w20
    add x0, x0, #16
    lsl x0, x0, #1
    bl arena_alloc_a64
    cbz x0, .done
    mov x21, x0 ; out base
    mov x22, x0 ; out cursor
.lp:
    ldrb w0, [x19, #0]
    cbz w0, .fin
    mov x0, x19
    bl wtf8_to_u16_a64
    cbz x0, .fin
    lsl x2, x1, #1
    add x2, x2, #2 ; include NUL wchar
    mov x1, x0
    mov x0, x22
    bl mem_copy_a64
    mov x22, x0
.adv:
    ldrb w0, [x19, #0]
    add x19, x19, #1
    cbnz w0, .adv
    b .lp
.fin:
    strh wzr, [x22, #0]
    strh wzr, [x22, #2]
    mov x0, x21
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_spawn_process_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE app, x1 = UTF-16LE cmdline, x2 = UTF-16LE cwd, w3 = waitFlag
;; @out w0 = exit code, -2 if 16-bit DOS .COM/MZ (vm8086 handoff), or -1 on error
;; @err w0 < 0 on error or vm8086 handoff
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Spawns a 64-bit Win32 process with inherited stdio handles and CREATE_UNICODE_ENVIRONMENT, handing .COM/MZ to vm8086.
@proc hal_spawn_process_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #176
    mov x29, sp
    mov x19, x0 ; app
    mov x20, x1 ; cmd
    mov x21, x2 ; cwd
    mov w22, w3 ; wait

    ; Build 64-bit STARTUPINFOW (104 bytes) at sp+16 and PROCESS_INFORMATION (24 bytes) at sp+128
    add x0, sp, #16
    mov x1, #144
    bl mem_zero_a64
    mov w0, #104
    str w0, [sp, #16]        ; cb = 104
    mov_imm32 w0, 0x00000100 ; STARTF_USESTDHANDLES
    str w0, [sp, #76]        ; dwFlags (+60)
    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.fdTable
    ldr x2, [x1, #0]
    str x2, [sp, #96]  ; hStdInput (+80)
    ldr x2, [x1, #8]
    str x2, [sp, #104] ; hStdOutput (+88)
    ldr x2, [x1, #16]
    str x2, [sp, #112] ; hStdError (+96)

    bl hal_build_u16_env_a64
    mov x23, x0

    add x0, sp, #16
    str x0, [sp, #0]  ; 9th arg: lpStartupInfo
    add x0, sp, #128
    str x0, [sp, #8]  ; 10th arg: lpProcessInformation
    mov x0, x19
    mov x1, x20
    mov x2, #0
    mov x3, #0
    mov w4, #1        ; bInheritHandles = TRUE
    mov_imm32 w5, 0x00000400 ; CREATE_UNICODE_ENVIRONMENT
    mov x6, x23
    mov x7, x21
    call_iat __imp_CreateProcessW
    cbz w0, .chk_dos_err

    ldr x19, [sp, #128] ; hProcess (8B)
    ldr x0, [sp, #136]  ; hThread (8B)
    call_iat __imp_CloseHandle
    cbz w22, .no_wt
    mov x0, x19
    mov w1, #-1
    call_iat __imp_WaitForSingleObject
    str wzr, [sp, #160]
    mov x0, x19
    add x1, sp, #160
    call_iat __imp_GetExitCodeProcess
    mov x0, x19
    call_iat __imp_CloseHandle
    ldr w0, [sp, #160]
    b .done

.no_wt:
    mov x0, x19
    call_iat __imp_CloseHandle
    mov w0, #0
    b .done

.chk_dos_err:
    call_iat __imp_GetLastError
    cmp w0, #193 ; ERROR_BAD_EXE_FORMAT
    b.eq .handoff
    cmp w0, #216 ; ERROR_EXE_MACHINE_TYPE_MISMATCH
    b.eq .handoff
    mov w0, #-1
    b .done
.handoff:
    mov w0, #-2
.done:
    add sp, sp, #176
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_build_pipe_subcmd_a64
;; @abi win_arm64
;; @in x0 = WTF-8 stage command string
;; @out x0 = UTF-16LE `"<SelfExe>" /S /D /C "<stage>"` in arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Formats a subshell invocation command line for concurrent pipeline execution.
@proc hal_build_pipe_subcmd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl str_len_a64
    add x0, x0, #640
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov x21, x0
    mov w1, #0x22
    strb w1, [x21, #0]
    add x21, x21, #1
    bl hal_get_ctx_a64
    ldr x1, [x0, #ARM64_CTX.pSelfExePath]
    mov x0, x21
    bl str_copy_a64
    mov x21, x0
    adr_sym x1, szPipeSubFlags_a64
    mov x0, x21
    bl str_copy_a64
    mov x21, x0
    mov x1, x19
    mov x0, x21
    bl str_copy_a64
    mov w1, #0x22
    strb w1, [x0, #0]
    strb wzr, [x0, #1]
    mov x0, x20
    bl wtf8_to_u16_a64
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_spawn_pipe_child_a64
;; @abi win_arm64
;; @in x0 = hIn, x1 = hOut, x2 = hErr, x3 = UTF-16LE cmdline
;; @out x0 = hProcess (or 0 on failure)
;; @err x0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Spawns an asynchronous 64-bit pipeline stage child process with explicit stdio handles.
@proc hal_spawn_pipe_child_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #160
    mov x29, sp
    mov x19, x0
    mov x20, x1
    mov x21, x2
    mov x22, x3

    add x0, sp, #16
    mov x1, #136
    bl mem_zero_a64
    mov w0, #104
    str w0, [sp, #16]
    mov_imm32 w0, 0x00000100 ; STARTF_USESTDHANDLES
    str w0, [sp, #76]
    str x19, [sp, #96]
    str x20, [sp, #104]
    str x21, [sp, #112]

    bl hal_build_u16_env_a64
    mov x19, x0

    bl hal_get_ctx_a64
    ldr x0, [x0, #ARM64_CTX.pSelfExePath]
    bl wtf8_to_u16_a64
    mov x20, x0

    add x0, sp, #16
    str x0, [sp, #0]  ; lpStartupInfo
    add x0, sp, #128
    str x0, [sp, #8]  ; lpProcessInformation
    mov x0, x20
    mov x1, x22
    mov x2, #0
    mov x3, #0
    mov w4, #1
    mov_imm32 w5, 0x00000400
    mov x6, x19
    mov x7, #0
    call_iat __imp_CreateProcessW
    cbz w0, .fail
    ldr x0, [sp, #136] ; hThread
    call_iat __imp_CloseHandle
    ldr x0, [sp, #128] ; hProcess
    b .done
.fail:
    mov x0, #0
.done:
    add sp, sp, #160
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_run_pipe_concurrent_a64
;; @abi win_arm64
;; @in x0 = WTF-8 left stage command, x1 = WTF-8 right stage command
;; @out w0 = right stage exit code (or -1 if CreatePipe unavailable)
;; @err w0 = -1 on pipe creation failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Executes `left | right` concurrently via CreatePipe(64 KiB) + SetHandleInformation + parallel subshells.
@proc hal_run_pipe_concurrent_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #64
    mov x29, sp
    mov x19, x0 ; left
    mov x20, x1 ; right

    ; 24-byte 64-bit SECURITY_ATTRIBUTES at sp+0: {nLength=24, lpSecurityDescriptor=0, bInheritHandle=1}
    mov w0, #24
    str w0, [sp, #0]
    str wzr, [sp, #4]
    str xzr, [sp, #8]
    mov w0, #1
    str w0, [sp, #16]
    str wzr, [sp, #20]
    str xzr, [sp, #32] ; hReadPipe
    str xzr, [sp, #40] ; hWritePipe

    add x0, sp, #32
    add x1, sp, #40
    mov x2, sp
    mov_imm32 w3, 0x00010000 ; 64 KiB pipe buffer hint
    call_iat __imp_CreatePipe
    cbz w0, .pipe_fail

    ldr x21, [sp, #32] ; hReadPipe
    ldr x22, [sp, #40] ; hWritePipe

    mov x0, x19
    bl hal_build_pipe_subcmd_a64
    mov x23, x0
    mov x0, x20
    bl hal_build_pipe_subcmd_a64
    mov x24, x0

    ; Clear inherit on hReadPipe while spawning left stage
    mov x0, x21
    mov w1, #1 ; HANDLE_FLAG_INHERIT
    mov w2, #0
    call_iat __imp_SetHandleInformation

    bl hal_get_ctx_a64
    add x4, x0, #ARM64_CTX.fdTable
    ldr x0, [x4, #0]  ; stdin
    mov x1, x22       ; stdout = hWritePipe
    ldr x2, [x4, #16] ; stderr
    mov x3, x23
    bl hal_spawn_pipe_child_a64
    mov x23, x0 ; hProcLeft

    ; Close parent write end immediately so EOF propagates
    mov x0, x22
    call_iat __imp_CloseHandle

    ; Re-enable inherit on hReadPipe for right stage
    mov x0, x21
    mov w1, #1
    mov w2, #1
    call_iat __imp_SetHandleInformation

    bl hal_get_ctx_a64
    add x4, x0, #ARM64_CTX.fdTable
    mov x0, x21       ; stdin = hReadPipe
    ldr x1, [x4, #8]  ; stdout
    ldr x2, [x4, #16] ; stderr
    mov x3, x24
    bl hal_spawn_pipe_child_a64
    mov x24, x0 ; hProcRight

    mov x0, x21
    call_iat __imp_CloseHandle

    cbz x23, .wt_right
    mov x0, x23
    mov w1, #-1
    call_iat __imp_WaitForSingleObject
    mov x0, x23
    call_iat __imp_CloseHandle
.wt_right:
    mov w19, #1
    cbz x24, .pipe_done
    mov x0, x24
    mov w1, #-1
    call_iat __imp_WaitForSingleObject
    str wzr, [sp, #48]
    mov x0, x24
    add x1, sp, #48
    call_iat __imp_GetExitCodeProcess
    ldr w19, [sp, #48]
    mov x0, x24
    call_iat __imp_CloseHandle
.pipe_done:
    mov w0, w19
    b .exit
.pipe_fail:
    mov w0, #-1
.exit:
    add sp, sp, #64
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_check_case_sensitive_dir_a64
;; @abi win_arm64
;; @in x0 = UTF-16LE directory path
;; @out w0 = 1 if Win10/11 FileCaseSensitiveInformation flag bit 0 is set, else 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries NtQueryInformationFile(FileCaseSensitiveInformation = 71) on Windows 10 1803+ / Windows 11 NTFS & Dev Drive ReFS.
@proc hal_check_case_sensitive_dir_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp
    mov x19, x0

    bl hal_get_ctx_a64
    ldr x20, [x0, #ARM64_CTX.pfnNtQueryInformationFile]
    cbz x20, .not_cs

    mov x0, x19
    mov_imm32 w1, 0x00000080 ; FILE_READ_ATTRIBUTES
    mov w2, #7
    mov x3, #0
    mov w4, #3 ; OPEN_EXISTING
    mov_imm32 w5, 0x02000000 ; FILE_FLAG_BACKUP_SEMANTICS
    mov x6, #0
    call_iat __imp_CreateFileW
    cmn x0, #1
    b.eq .not_cs
    mov x19, x0

    str wzr, [sp, #16]
    mov x0, x19
    mov x1, sp       ; IO_STATUS_BLOCK (16 bytes)
    add x2, sp, #16  ; FILE_CASE_SENSITIVE_INFORMATION (4 bytes)
    mov w3, #4
    mov w4, #71      ; FileCaseSensitiveInformation
    blr x20
    mov w20, w0
    mov x0, x19
    call_iat __imp_CloseHandle
    cbnz w20, .not_cs
    ldr w0, [sp, #16]
    and w0, w0, #1
    b .done
.not_cs:
    mov w0, #0
.done:
    add sp, sp, #32
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc hal_exit_process_a64
;; @abi win_arm64
;; @in w0 = exit code
;; @out Terminates process via KERNEL32!ExitProcess
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Terminates the current ARM64 process with exit code w0.
@proc hal_exit_process_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    call_iat __imp_ExitProcess
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
