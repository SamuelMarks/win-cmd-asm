;; @file hal_rt7.inc
;; @isa armv7
;; @desc ARMv7-A Thumb-2 (0x01C4 ARMNT) Windows RT 8.0/8.1 & Windows 10 ARM32 HAL, TEB/PEB, SEH/Ctrl+C, CreatePipe, & dynamic APIs.

@cpu armv7
@format inc

;; @proc a32_ctrl_handler
;; @abi arm_nt
;; @in r0 = dwCtrlType (0=CTRL_C_EVENT, 1=CTRL_BREAK_EVENT, 2=CTRL_CLOSE_EVENT)
;; @out r0 = 1 (TRUE = handled)
;; @err none
;; @clobbers r0, r1, r2
;; @desc Thread-safe SetConsoleCtrlHandler callback latching bCtrlBreak with DMB ISH barrier.
@proc a32_ctrl_handler
    cmp r0, #2
    bhi .unhandled
    adr_sym r1, g_a32_ctx
    mov r2, #1
    dmb ish
    strb r2, [r1, #ARM32_CTX.bCtrlBreak]
    dmb ish
    mov r0, #1
    bx lr
.unhandled:
    mov r0, #0
    bx lr
@endproc

;; @proc a32_seh_filter
;; @abi arm_nt
;; @in r0 = EXCEPTION_POINTERS*
;; @out r0 = 1 (EXCEPTION_EXECUTE_HANDLER)
;; @err none
;; @clobbers r0
;; @desc Top-level SetUnhandledExceptionFilter guard preventing silent crashes on external/device faults.
@proc a32_seh_filter
    mov r0, #1
    bx lr
@endproc

;; @proc hal_get_ctx
;; @abi arm_nt
;; @in none
;; @out r0 = pointer to g_a32_ctx in .bss
;; @err none
;; @clobbers r0
;; @desc Loads the 32-bit address of g_a32_ctx via Thumb-2 MOVW/MOVT.
@proc hal_get_ctx
    adr_sym r0, g_a32_ctx
    bx lr
@endproc

;; @proc hal_get_str_pool
;; @abi arm_nt
;; @in none
;; @out r0 = pointer to g_a32_str_pool in .rdata
;; @err none
;; @clobbers r0
;; @desc Loads the 32-bit address of g_a32_str_pool via Thumb-2 MOVW/MOVT.
@proc hal_get_str_pool
    adr_sym r0, g_a32_str_pool
    bx lr
@endproc

;; @proc math_udiv32
;; @abi arm_nt
;; @in r0 = unsigned dividend, r1 = unsigned divisor
;; @out r0 = quotient, r1 = remainder, r2 = 0 (or ERR_DIV_BY_ZERO if r1 == 0)
;; @err r2 = ERR_DIV_BY_ZERO when divisor is 0
;; @clobbers r0, r1, r2, r3
;; @desc Hardware ARMv7-A UDIV + remainder with explicit zero-divisor pre-guard.
@proc math_udiv32
    cbz r1, .udiv_zero
    udiv r2, r0, r1
    mul r3, r2, r1
    sub r1, r0, r3
    mov r0, r2
    mov r2, #0
    bx lr
.udiv_zero:
    mov r0, #0
    mov r1, #0
    mov_imm32 r2, ERR_DIV_BY_ZERO
    bx lr
@endproc

;; @proc math_sdiv32
;; @abi arm_nt
;; @in r0 = signed dividend, r1 = signed divisor
;; @out r0 = signed quotient, r1 = signed remainder, r2 = 0 (or ERR_DIV_BY_ZERO)
;; @err r2 = ERR_DIV_BY_ZERO when divisor is 0
;; @clobbers r0, r1, r2, r3
;; @desc Hardware ARMv7-A SDIV guarding both /0 and INT32_MIN / -1 for exact SET /A parity.
@proc math_sdiv32
    cbz r1, .sdiv_zero
    mov_imm32 r2, 0x80000000
    cmp r0, r2
    bne .do_sdiv
    cmn r1, #1
    bne .do_sdiv
    mov r1, #0
    mov r2, #0
    bx lr
.do_sdiv:
    sdiv r2, r0, r1
    mul r3, r2, r1
    sub r1, r0, r3
    mov r0, r2
    mov r2, #0
    bx lr
.sdiv_zero:
    mov r0, #0
    mov r1, #0
    mov_imm32 r2, ERR_DIV_BY_ZERO
    bx lr
@endproc

;; @proc hal_virtual_alloc
;; @abi arm_nt
;; @in r0 = lpAddress, r1 = dwSize, r2 = flAllocationType, r3 = flProtect
;; @out r0 = allocated pointer or 0
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Invokes KERNEL32!VirtualAlloc.
@proc hal_virtual_alloc
    push {r4, lr}
    call_iat __imp_VirtualAlloc
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_virtual_free
;; @abi arm_nt
;; @in r0 = lpAddress, r1 = dwSize, r2 = dwFreeType
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Invokes KERNEL32!VirtualFree.
@proc hal_virtual_free
    push {r4, lr}
    call_iat __imp_VirtualFree
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_load_dyn_apis_rt7
;; @abi arm_nt
;; @in r0 = ARM32_CTX*
;; @out Populates progressive dynamic API pointers in ARM32_CTX
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Dynamically resolves NTDLL, ADVAPI32, SHELL32, and Win8/10/11 KERNEL32 capabilities.
@proc hal_load_dyn_apis_rt7
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0

    ; KERNEL32 optional Win8/10/11 APIs
    adr_sym r0, u16Kernel32
    call_iat __imp_GetModuleHandleW
    mov r5, r0
    cmp r5, #0
    beq .try_ntdll

    mov r0, r5
    adr_sym r1, szCreateSymbolicLinkW
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnCreateSymbolicLinkW]

    mov r0, r5
    adr_sym r1, szCreateHardLinkW
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnCreateHardLinkW]

    mov r0, r5
    adr_sym r1, szCreatePseudoConsole
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnCreatePseudoConsole]

    mov r0, r5
    adr_sym r1, szGetMachineTypeAttributes
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnGetMachineTypeAttributes]

.try_ntdll:
    adr_sym r0, u16Ntdll
    call_iat __imp_GetModuleHandleW
    mov r5, r0
    cbz r5, .try_advapi

    mov r0, r5
    adr_sym r1, szRtlGetVersion
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnRtlGetVersion]

    mov r0, r5
    adr_sym r1, szNtQueryInformationFile
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnNtQueryInformationFile]

.try_advapi:
    adr_sym r0, u16Advapi32
    call_iat __imp_LoadLibraryW
    mov r5, r0
    cmp r5, #0
    beq .done

    mov r0, r5
    adr_sym r1, szRegOpenKeyExW
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnRegOpenKeyExW]

    mov r0, r5
    adr_sym r1, szRegCreateKeyExW
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnRegCreateKeyExW]

    mov r0, r5
    adr_sym r1, szRegQueryValueExW
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnRegQueryValueExW]

    mov r0, r5
    adr_sym r1, szRegSetValueExW
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnRegSetValueExW]

    mov r0, r5
    adr_sym r1, szRegCloseKey
    call_iat __imp_GetProcAddress
    str r0, [r4, #ARM32_CTX.pfnRegCloseKey]

.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_init_platform
;; @abi arm_nt
;; @in r0 = ARM32_CTX*
;; @out r0 = 0 on success or ERR_* on failure
;; @err r0 != 0 on VirtualAlloc failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Initializes Windows RT 8.0/8.1 & Win10 ARMNT TEB/PEB OS version, SEH/Ctrl+C, VT console, CWD, and env.
@proc hal_init_platform
    push {r4, r5, r6, lr}
    sub sp, sp, #552
    mov r4, r0

    mov r1, #0
    strb r1, [r4, #ARM32_CTX.bIsWinCe]
    mov r1, #1
    strb r1, [r4, #ARM32_CTX.bEchoOn]
    strb r1, [r4, #ARM32_CTX.bExtensions]
    strb r1, [r4, #ARM32_CTX.bFileCompl]
    strb r1, [r4, #ARM32_CTX.bBreakOn]
    mov r1, #0x07
    strh r1, [r4, #ARM32_CTX.wDefaultAttr]
    strh r1, [r4, #ARM32_CTX.wCurrentAttr]

    ; Read TEB via ARMNT coprocessor register (MRC p15, 0, Rd, c13, c0, 2) -> PEB at [TEB + 0x30]
    mrc p15, 0, r1, c13, c0, 2
    cbz r1, .def_rt_ver
    ldr r2, [r1, #0x30]
    cbz r2, .def_rt_ver
    ldr r0, [r2, #0xA4] ; PEB.OSMajorVersion (32-bit PEB offset)
    cmp r0, #0
    beq .def_rt_ver
    str r0, [r4, #ARM32_CTX.dwOsMajor]
    ldr r0, [r2, #0xA8] ; PEB.OSMinorVersion
    str r0, [r4, #ARM32_CTX.dwOsMinor]
    ldrh r0, [r2, #0xAC] ; PEB.OSBuildNumber
    str r0, [r4, #ARM32_CTX.dwOsBuild]
    b .after_peb
.def_rt_ver:
    mov r0, #10
    str r0, [r4, #ARM32_CTX.dwOsMajor]
    mov r0, #0
    str r0, [r4, #ARM32_CTX.dwOsMinor]
    mov_imm32 r0, 19045
    str r0, [r4, #ARM32_CTX.dwOsBuild]

.after_peb:
    ; Register SEH unhandled filter + Ctrl+C / Ctrl+Break handler
    adr_sym r0, a32_seh_filter
    orr r0, r0, #1
    call_iat __imp_SetUnhandledExceptionFilter
    adr_sym r0, a32_ctrl_handler
    orr r0, r0, #1
    mov r1, #1
    call_iat __imp_SetConsoleCtrlHandler

    ; Standard handles (-10, -11, -12)
    mvn r0, #9 ; -10 STD_INPUT_HANDLE
    call_iat __imp_GetStdHandle
    str r0, [r4, #ARM32_CTX.hStdIn]
    mvn r0, #10 ; -11 STD_OUTPUT_HANDLE
    call_iat __imp_GetStdHandle
    str r0, [r4, #ARM32_CTX.hStdOut]
    mvn r0, #11 ; -12 STD_ERROR_HANDLE
    call_iat __imp_GetStdHandle
    str r0, [r4, #ARM32_CTX.hStdErr]

    add r2, r4, #ARM32_CTX.fdTable
    ldr r0, [r4, #ARM32_CTX.hStdIn]
    str r0, [r2, #0]
    ldr r0, [r4, #ARM32_CTX.hStdOut]
    str r0, [r2, #4]
    ldr r0, [r4, #ARM32_CTX.hStdErr]
    str r0, [r2, #8]

    ; Probe Windows 10/11 Virtual Terminal Processing (0x0004)
    ldr r0, [r4, #ARM32_CTX.hStdOut]
    add r1, sp, #0
    call_iat __imp_GetConsoleMode
    cbz r0, .no_vt
    ldr r1, [sp, #0]
    orr r1, r1, #0x0004 ; ENABLE_VIRTUAL_TERMINAL_PROCESSING
    ldr r0, [r4, #ARM32_CTX.hStdOut]
    call_iat __imp_SetConsoleMode
    cbz r0, .no_vt
    mov r0, #1
    strb r0, [r4, #ARM32_CTX.bVtEnabled]
.no_vt:
    call_iat __imp_GetConsoleOutputCP
    cmp r0, #0
    it eq
    moveq r0, #65001
    str r0, [r4, #ARM32_CTX.dwCodePage]

    call_iat __imp_GetTickCount
    orr r0, r0, #1
    str r0, [r4, #ARM32_CTX.dwRandomSeed]

    mov r0, r4
    bl hal_load_dyn_apis_rt7

    ; Override OS version via NTDLL!RtlGetVersion if available
    ldr r5, [r4, #ARM32_CTX.pfnRtlGetVersion]
    cbz r5, .after_rtl_ver
    mov r0, sp
    mov_imm32 r1, 284
    bl mem_zero_a32
    mov_imm32 r0, 284
    str r0, [sp, #0]
    mov r0, sp
    blx r5
    cmp r0, #0
    bne .after_rtl_ver
    ldr r0, [sp, #4]
    str r0, [r4, #ARM32_CTX.dwOsMajor]
    ldr r0, [sp, #8]
    str r0, [r4, #ARM32_CTX.dwOsMinor]
    ldr r0, [sp, #12]
    str r0, [r4, #ARM32_CTX.dwOsBuild]

.after_rtl_ver:
    mov r0, r4
    bl arena_init_a32
    cmp r0, #0
    bne .exit_rt_init

    ; Initial CWD via GetCurrentDirectoryW
    mov r0, #260
    add r1, sp, #16
    call_iat __imp_GetCurrentDirectoryW
    cbz r0, .def_rt_cwd
    add r0, sp, #16
    mov r1, #260
    add r2, r4, #ARM32_CTX.szCwd
    bl u16_to_wtf8_buf_a32
    b .got_rt_cwd
.def_rt_cwd:
    add r0, r4, #ARM32_CTX.szCwd
    adr_sym r1, szRtDefCwd
    bl str_copy_a32

.got_rt_cwd:
    ; Self executable path
    mov r0, #0
    add r1, sp, #16
    mov r2, #260
    call_iat __imp_GetModuleFileNameW
    cbz r0, .def_rt_self
    add r0, sp, #16
    bl u16_to_wtf8_arena_a32
    mov r5, r0
    bl str_len_a32
    add r0, r0, #2
    bl perm_alloc_a32
    mov r6, r0
    mov r1, r5
    bl str_copy_a32
    str r6, [r4, #ARM32_CTX.pSelfExePath]
    b .got_rt_self
.def_rt_self:
    mov r0, #32
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pSelfExePath]
    adr_sym r1, szRtDefSelf
    bl str_copy_a32

.got_rt_self:
    call_iat __imp_GetCommandLineW
    cbz r0, .load_env
    bl u16_to_wtf8_arena_a32
    mov r5, r0
    bl str_len_a32
    add r0, r0, #2
    bl perm_alloc_a32
    mov r6, r0
    mov r1, r5
    bl str_copy_a32
    str r6, [r4, #ARM32_CTX.pCmdLineRaw]

.load_env:
    call_iat __imp_GetEnvironmentStringsW
    mov r5, r0
    cmp r5, #0
    beq .def_rt_env
    ; Transcode double-NUL UTF-16LE environment block into pEnvBlock
    mov r6, r5 ; u16 cursor
    ldr r2, [r4, #ARM32_CTX.pEnvBlock]
.env_lp:
    ldrh r0, [r6, #0]
    cbz r0, .env_done
    mov r0, r6
    mvn r1, #0
    push {r2, r3}
    bl u16_to_wtf8_buf_a32
    mov r3, r0
    pop {r2, r12}
    add r2, r2, r3
    add r2, r2, #1 ; advance past NUL
.skip_w:
    ldrh r0, [r6, #0]
    add r6, r6, #2
    cmp r0, #0
    bne .skip_w
    b .env_lp
.env_done:
    mov r0, #0
    strb r0, [r2, #0]
    add r2, r2, #1
    ldr r0, [r4, #ARM32_CTX.pEnvBlock]
    sub r2, r2, r0
    str r2, [r4, #ARM32_CTX.dwEnvLen]
    mov r0, r5
    call_iat __imp_FreeEnvironmentStringsW
    mov r0, #0
    b .exit_rt_init

.def_rt_env:
    ldr r0, [r4, #ARM32_CTX.pEnvBlock]
    adr_sym r1, szRtDefaultEnv
    mov r2, #szRtDefaultEnv_len
    bl mem_copy_a32
    mov r0, #szRtDefaultEnv_len
    str r0, [r4, #ARM32_CTX.dwEnvLen]
    mov r0, #0

.exit_rt_init:
    add sp, sp, #552
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc hal_write_handle
;; @abi arm_nt
;; @in r0 = handle, r1 = WTF-8 buffer, r2 = byte length
;; @out r0 = 0 on success or ERR_WRITE_FAULT
;; @err r0 = ERR_WRITE_FAULT on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes to a Win32 console (WriteConsoleW) or file/pipe (WriteFile, honoring /U UTF-16LE).
@proc hal_write_handle
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #20
    mov r4, r0 ; handle
    mov r5, r1 ; buf
    mov r6, r2 ; len
    cmp r6, #0
    beq .ok

    ; Check if handle is a real console screen buffer via GetConsoleMode
    mov r0, r4
    add r1, sp, #12
    call_iat __imp_GetConsoleMode
    cmp r0, #0
    beq .not_con

    ; Copy slice + NUL and transcode to UTF-16LE for WriteConsoleW
    add r0, r6, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_con
    mov r7, r0
    mov r1, r5
    mov r2, r6
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r7
    bl wtf8_to_u16_a32
    cbz r0, .not_con
    mov r2, r1 ; cchToWrite
    mov r1, r0 ; lpBuffer
    mov r0, #0
    str r0, [sp, #0] ; lpReserved
    mov r0, r4
    add r3, sp, #8
    call_iat __imp_WriteConsoleW
    b .ok

.not_con:
    bl hal_get_ctx
    ldrb r0, [r0, #ARM32_CTX.bUnicodeOut]
    cbz r0, .raw_file
    add r0, r6, #2
    bl arena_alloc_a32
    cbz r0, .raw_file
    mov r7, r0
    mov r1, r5
    mov r2, r6
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r7
    bl wtf8_to_u16_a32
    cbz r0, .raw_file
    mov r5, r0
    lsl r6, r1, #1

.raw_file:
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, r5
    mov r2, r6
    add r3, sp, #8
    call_iat __imp_WriteFile
    cbz r0, .fail
.ok:
    mov r0, #0
    add sp, sp, #20
    pop {r4, r5, r6, r7, lr}
    bx lr
.fail:
    mov r0, #ERR_WRITE_FAULT
    add sp, sp, #20
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc hal_read_handle
;; @abi arm_nt
;; @in r0 = handle, r1 = dst buffer, r2 = max bytes
;; @out r0 = bytes read (0 on EOF)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reads bytes from a console or file/pipe handle via ReadFile.
@proc hal_read_handle
    push {r4, lr}
    sub sp, sp, #16
    mov r3, #0
    str r3, [sp, #0]
    str r3, [sp, #8]
    add r3, sp, #8
    call_iat __imp_ReadFile
    cbz r0, .eof
    ldr r0, [sp, #8]
    add sp, sp, #16
    pop {r4, lr}
    bx lr
.eof:
    mov r0, #0
    add sp, sp, #16
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_open_file
;; @abi arm_nt
;; @in r0 = UTF-16LE path, r1 = mode (0=read, 1=write_trunc, 2=write_append, 3=rw_open)
;; @out r0 = handle or -1
;; @err r0 = -1 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Opens/creates an inheritable file handle via KERNEL32!CreateFileW and seeks to EOF on append.
@proc hal_open_file
    push {r4, r5, lr}
    sub sp, sp, #36
    mov r4, r0
    mov r5, r1

    ; Inheritable SECURITY_ATTRIBUTES at sp+16 (nLength=12, lpSec=0, bInheritHandle=1)
    mov r2, #12
    str r2, [sp, #16]
    mov r2, #0
    str r2, [sp, #20]
    mov r2, #1
    str r2, [sp, #24]

    cmp r5, #0
    beq .m_rd
    cmp r5, #1
    beq .m_tr
    cmp r5, #2
    beq .m_ap
    mov_imm32 r1, 0xC0000000
    mov r2, #OPEN_EXISTING
    b .call_cf
.m_rd:
    mov_imm32 r1, GENERIC_READ
    mov r2, #OPEN_EXISTING
    b .call_cf
.m_tr:
    mov_imm32 r1, GENERIC_WRITE
    mov r2, #CREATE_ALWAYS
    b .call_cf
.m_ap:
    mov_imm32 r1, GENERIC_WRITE
    mov r2, #OPEN_ALWAYS

.call_cf:
    str r2, [sp, #0]
    mov r3, #FILE_ATTRIBUTE_NORMAL
    str r3, [sp, #4]
    mov r3, #0
    str r3, [sp, #8]
    mov r0, r4
    mov r2, #7 ; FILE_SHARE_READ | WRITE | DELETE
    add r3, sp, #16
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .done
    cmp r5, #2
    bne .done
    mov r4, r0
    mov r1, #0
    mov r2, #0
    mov r3, #2 ; FILE_END
    bl hal_seek_file
    mov r0, r4
.done:
    add sp, sp, #36
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_seek_file
;; @abi arm_nt
;; @in r0 = handle, r1 = off_lo, r2 = off_hi, r3 = method
;; @out r0 = new_lo, r1 = new_hi (or r0 = -1 on error)
;; @err r0 = -1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Seeks a 64-bit file offset via KERNEL32!SetFilePointerEx (note ARM32 64-bit arg alignment in r2:r3!).
@proc hal_seek_file
    push {r4, lr}
    sub sp, sp, #16
    ; On ARMNT ABI, 64-bit liDistanceToMove is passed in even-aligned register pair r2:r3!
    str r3, [sp, #4] ; dwMoveMethod (arg #4 on stack!)
    add r3, sp, #8
    str r3, [sp, #0] ; lpNewFilePointer (arg #3 on stack!)
    mov r3, r2       ; hi32 -> r3
    mov r2, r1       ; lo32 -> r2
    call_iat __imp_SetFilePointerEx
    cbz r0, .err
    ldr r0, [sp, #8]
    ldr r1, [sp, #12]
    add sp, sp, #16
    pop {r4, lr}
    bx lr
.err:
    mvn r0, #0
    mvn r1, #0
    add sp, sp, #16
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_close_handle
;; @abi arm_nt
;; @in r0 = handle
;; @out r0 = BOOL
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Closes a Win32 handle via KERNEL32!CloseHandle (protecting standard console handles).
@proc hal_close_handle
    push {r4, lr}
    cmn r0, #1
    beq .skip
    cbz r0, .skip
    mov r4, r0
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.hStdIn]
    cmp r4, r1
    beq .skip
    ldr r1, [r0, #ARM32_CTX.hStdOut]
    cmp r4, r1
    beq .skip
    ldr r1, [r0, #ARM32_CTX.hStdErr]
    cmp r4, r1
    beq .skip
    mov r0, r4
    call_iat __imp_CloseHandle
    pop {r4, lr}
    bx lr
.skip:
    mov r0, #1
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_file_attr
;; @abi arm_nt
;; @in r0 = UTF-16LE path
;; @out r0 = attributes or -1
;; @err r0 = -1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries file attributes via KERNEL32!GetFileAttributesW.
@proc hal_get_file_attr
    push {r4, lr}
    call_iat __imp_GetFileAttributesW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_set_file_attr
;; @abi arm_nt
;; @in r0 = UTF-16LE path, r1 = attributes
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Updates file attributes via KERNEL32!SetFileAttributesW.
@proc hal_set_file_attr
    push {r4, lr}
    call_iat __imp_SetFileAttributesW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_delete_file
;; @abi arm_nt
;; @in r0 = UTF-16LE path
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Deletes a file via KERNEL32!DeleteFileW.
@proc hal_delete_file
    push {r4, lr}
    call_iat __imp_DeleteFileW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_mkdir
;; @abi arm_nt
;; @in r0 = UTF-16LE path
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Creates a directory via KERNEL32!CreateDirectoryW.
@proc hal_mkdir
    push {r4, lr}
    mov r1, #0
    call_iat __imp_CreateDirectoryW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_rmdir
;; @abi arm_nt
;; @in r0 = UTF-16LE path
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Removes a directory or NTFS junction/directory symlink via KERNEL32!RemoveDirectoryW.
@proc hal_rmdir
    push {r4, lr}
    call_iat __imp_RemoveDirectoryW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_move_file
;; @abi arm_nt
;; @in r0 = UTF-16LE src, r1 = UTF-16LE dst
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Atomically moves/replaces a file or directory via KERNEL32!MoveFileExW(MOVEFILE_REPLACE_EXISTING|COPY_ALLOWED).
@proc hal_move_file
    push {r4, lr}
    mov r2, #3 ; MOVEFILE_REPLACE_EXISTING (1) | MOVEFILE_COPY_ALLOWED (2)
    call_iat __imp_MoveFileExW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_copy_file
;; @abi arm_nt
;; @in r0 = UTF-16LE src, r1 = UTF-16LE dst, r2 = fail_if_exists
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Copies a file via KERNEL32!CopyFileW.
@proc hal_copy_file
    push {r4, lr}
    call_iat __imp_CopyFileW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_populate_find_rt7
;; @abi arm_nt
;; @in r0 = WIN32_FIND_DATAW* (592 bytes), r1 = FIND_INFO_A32*
;; @out Populates FIND_INFO_A32
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Extracts attributes, 64-bit size, write FILETIME, reparse tag, cAlternateFileName, and cFileName.
@proc hal_populate_find_rt7
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    ldr r0, [r4, #0]
    str r0, [r5, #FIND_INFO_A32.dwAttr]
    ldr r0, [r4, #20]
    str r0, [r5, #FIND_INFO_A32.ftWriteLo]
    ldr r0, [r4, #24]
    str r0, [r5, #FIND_INFO_A32.ftWriteHi]
    ldr r0, [r4, #28]
    str r0, [r5, #FIND_INFO_A32.uSizeHi]
    ldr r0, [r4, #32]
    str r0, [r5, #FIND_INFO_A32.uSizeLo]
    ldr r0, [r4, #36]
    str r0, [r5, #FIND_INFO_A32.dwReparseTag]
    add r0, r4, #44
    mov r1, #260
    add r2, r5, #FIND_INFO_A32.szName
    bl u16_to_wtf8_buf_a32
    mov_imm32 r0, 564
    add r0, r4, r0
    mov r1, #14
    add r2, r5, #FIND_INFO_A32.szShort
    bl u16_to_wtf8_buf_a32
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_find_first
;; @abi arm_nt
;; @in r0 = UTF-16LE pattern, r1 = FIND_INFO_A32*
;; @out r0 = search handle or -1
;; @err r0 = -1 on no match
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Starts directory enumeration via FindFirstFileExW (FindExInfoBasic on Win7+/RT+) or FindFirstFileW.
@proc hal_find_first
    push {r4, r5, lr}
    sub sp, sp, #604
    mov r4, r1
    mov r5, r0
    mov r0, sp
    mov_imm32 r1, 596
    bl mem_zero_a32
    mov r0, r5
    mov r1, sp
    call_iat __imp_FindFirstFileW
    cmn r0, #1
    beq .done
    mov r5, r0
    mov r0, sp
    mov r1, r4
    bl hal_populate_find_rt7
    mov r0, r5
.done:
    add sp, sp, #604
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_find_next
;; @abi arm_nt
;; @in r0 = search handle, r1 = FIND_INFO_A32*
;; @out r0 = BOOL
;; @err r0 = 0 at end
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Continues directory enumeration via KERNEL32!FindNextFileW.
@proc hal_find_next
    push {r4, r5, lr}
    sub sp, sp, #604
    mov r4, r0
    mov r5, r1
    mov r0, sp
    mov_imm32 r1, 596
    bl mem_zero_a32
    mov r0, r4
    mov r1, sp
    call_iat __imp_FindNextFileW
    cbz r0, .done
    mov r0, sp
    mov r1, r5
    bl hal_populate_find_rt7
    mov r0, #1
.done:
    add sp, sp, #604
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_find_close
;; @abi arm_nt
;; @in r0 = search handle
;; @out r0 = BOOL
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Closes a search handle via KERNEL32!FindClose.
@proc hal_find_close
    push {r4, lr}
    call_iat __imp_FindClose
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_local_time
;; @abi arm_nt
;; @in r0 = SYSTEMTIME* (16 bytes)
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries local wall-clock time via KERNEL32!GetLocalTime.
@proc hal_get_local_time
    push {r4, lr}
    call_iat __imp_GetLocalTime
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_set_local_time
;; @abi arm_nt
;; @in r0 = SYSTEMTIME* (16 bytes)
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Sets local wall-clock time via KERNEL32!SetLocalTime.
@proc hal_set_local_time
    push {r4, lr}
    call_iat __imp_SetLocalTime
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_tick_count
;; @abi arm_nt
;; @in none
;; @out r0 = millisecond tick count
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries millisecond uptime via KERNEL32!GetTickCount.
@proc hal_get_tick_count
    push {r4, lr}
    call_iat __imp_GetTickCount
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_disk_free
;; @abi arm_nt
;; @in r0 = UTF-16LE directory, r1 = 8-byte u64 output pointer
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries 64-bit disk free bytes via KERNEL32!GetDiskFreeSpaceExW.
@proc hal_get_disk_free
    push {r4, lr}
    sub sp, sp, #24
    mov r4, r1
    add r1, sp, #0
    add r2, sp, #8
    add r3, sp, #16
    call_iat __imp_GetDiskFreeSpaceExW
    cbz r0, .fail
    ldr r1, [sp, #0]
    ldr r2, [sp, #4]
    str r1, [r4, #0]
    str r2, [r4, #4]
    mov r0, #1
.fail:
    add sp, sp, #24
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_set_console_attr
;; @abi arm_nt
;; @in r0 = wAttributes
;; @out r0 = BOOL
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Sets console foreground/background attribute via KERNEL32!SetConsoleTextAttribute.
@proc hal_set_console_attr
    push {r4, lr}
    mov r4, r0
    bl hal_get_ctx
    strh r4, [r0, #ARM32_CTX.wCurrentAttr]
    ldr r0, [r0, #ARM32_CTX.hStdOut]
    mov r1, r4
    call_iat __imp_SetConsoleTextAttribute
    mov r0, #1
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_clear_console
;; @abi arm_nt
;; @in none
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Clears the Win32 console screen buffer (FillConsoleOutputCharacterW/Attribute + cursor home) or VT sequence.
@proc hal_clear_console
    push {r4, r5, lr}
    sub sp, sp, #36
    bl hal_get_ctx
    mov r4, r0
    ldrb r1, [r4, #ARM32_CTX.bVtEnabled]
    cbz r1, .classic_cls
    adr_sym r0, szRtVtClear
    bl out_str_a32
    b .done
.classic_cls:
    ldr r0, [r4, #ARM32_CTX.hStdOut]
    add r1, sp, #8 ; CONSOLE_SCREEN_BUFFER_INFO (22 bytes)
    call_iat __imp_GetConsoleScreenBufferInfo
    cbz r0, .done
    ldrh r1, [sp, #8]  ; dwSize.X
    ldrh r2, [sp, #10] ; dwSize.Y
    mul r5, r1, r2
    add r0, sp, #4
    str r0, [sp, #0]
    ldr r0, [r4, #ARM32_CTX.hStdOut]
    mov r1, #0x20
    mov r2, r5
    mov r3, #0 ; COORD {0,0}
    call_iat __imp_FillConsoleOutputCharacterW
    ldr r0, [r4, #ARM32_CTX.hStdOut]
    mov r1, #0
    call_iat __imp_SetConsoleCursorPosition
.done:
    mov r0, #0
    add sp, sp, #36
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_set_title
;; @abi arm_nt
;; @in r0 = UTF-16LE title
;; @out r0 = BOOL
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Updates the console window title via KERNEL32!SetConsoleTitleW.
@proc hal_set_title
    push {r4, lr}
    call_iat __imp_SetConsoleTitleW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_set_cp
;; @abi arm_nt
;; @in r0 = new codepage (0 = query)
;; @out r0 = active codepage
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries or switches console input/output codepage (including CP65001 UTF-8) via SetConsoleCP/SetConsoleOutputCP.
@proc hal_get_set_cp
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    bl hal_get_ctx
    mov r5, r0
    cbz r4, .query_only
    mov r0, r4
    call_iat __imp_SetConsoleCP
    mov r0, r4
    call_iat __imp_SetConsoleOutputCP
    str r4, [r5, #ARM32_CTX.dwCodePage]
.query_only:
    ldr r0, [r5, #ARM32_CTX.dwCodePage]
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_mklink
;; @abi arm_nt
;; @in r0 = mode (0=file symlink, 1=/D dir symlink, 2=/H hardlink, 3=/J NTFS junction), r1 = u16 link, r2 = u16 target
;; @out r0 = 0 on success or ERR_*
;; @err r0 != 0 on privilege or filesystem error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements MKLINK (/D, /H, /J, file symlink + Developer Mode unprivileged flag 0x2) on Windows RT/10 ARMNT.
@proc hal_mklink
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #556
    mov r4, r0 ; mode
    mov r5, r1 ; link
    mov r6, r2 ; target

    bl hal_get_ctx
    mov r7, r0

    cmp r4, #2
    beq .do_hard
    cmp r4, #3
    beq .do_junc

    ; Symbolic link (0 = file, 1 = SYMBOLIC_LINK_FLAG_DIRECTORY)
    ldr r3, [r7, #ARM32_CTX.pfnCreateSymbolicLinkW]
    cmp r3, #0
    beq .fail_mk
    mov r0, r5
    mov r1, r6
    mov r2, r4 ; 0 or 1
    blx r3
    and r0, r0, #0xFF
    cmp r0, #0
    bne .ok_mk
    ; Retry with SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (0x2) on Windows 10 1703+!
    ldr r3, [r7, #ARM32_CTX.pfnCreateSymbolicLinkW]
    mov r0, r5
    mov r1, r6
    orr r2, r4, #2
    blx r3
    and r0, r0, #0xFF
    cmp r0, #0
    bne .ok_mk
    b .fail_mk

.do_hard:
    ldr r3, [r7, #ARM32_CTX.pfnCreateHardLinkW]
    cmp r3, #0
    beq .fail_mk
    mov r0, r5
    mov r1, r6
    mov r2, #0
    blx r3
    cmp r0, #0
    beq .fail_mk
    b .ok_mk

.do_junc:
    mov r0, r5
    mov r1, #0
    call_iat __imp_CreateDirectoryW
    mov r0, #OPEN_EXISTING
    str r0, [sp, #0]
    mov_imm32 r0, 0x02200000 ; FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT
    str r0, [sp, #4]
    mov r0, #0
    str r0, [sp, #8]
    mov r0, r5
    mov_imm32 r1, GENERIC_WRITE
    mov r2, #7
    mov r3, #0
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .fail_mk
    mov r4, r0

    add r0, sp, #24
    mov_imm32 r1, 512
    bl mem_zero_a32
    mov_imm32 r0, 0xA0000003 ; IO_REPARSE_TAG_MOUNT_POINT
    str r0, [sp, #24]
    ; Prefix "\??" + target into PathBuffer at sp+40
    add r1, sp, #40
    mov r0, #0x5C
    strh r0, [r1, #0]
    mov r0, #0x3F
    strh r0, [r1, #2]
    strh r0, [r1, #4]
    mov r0, #0x5C
    strh r0, [r1, #6]
    add r1, r1, #8
    mov r2, r6
    mov r3, #8
.j_cp:
    ldrh r0, [r2, #0]
    cbz r0, .j_cp_done
    strh r0, [r1, #0]
    add r1, r1, #2
    add r2, r2, #2
    add r3, r3, #2
    cmp r3, #480
    bcc .j_cp
.j_cp_done:
    mov r0, #0
    strh r0, [r1, #0]
    strh r3, [sp, #34] ; SubstituteNameLength
    add r0, r3, #2
    strh r0, [sp, #36] ; PrintNameOffset
    add r0, r3, #12
    strh r0, [sp, #28] ; ReparseDataLength
    add r3, r0, #8     ; nInBufferSize

    mov r0, #0
    str r0, [sp, #0]
    str r0, [sp, #4]
    add r1, sp, #540
    str r1, [sp, #8]
    str r0, [sp, #12]
    mov r0, r4
    mov_imm32 r1, 0x000900A4 ; FSCTL_SET_REPARSE_POINT
    add r2, sp, #24
    call_iat __imp_DeviceIoControl
    mov r5, r0
    mov r0, r4
    call_iat __imp_CloseHandle
    cbz r5, .fail_mk

.ok_mk:
    mov r0, #0
    add sp, sp, #556
    pop {r4, r5, r6, r7, lr}
    bx lr
.fail_mk:
    mov r0, #ERR_ACCESS_DENIED
    add sp, sp, #556
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc hal_reg_query_str
;; @abi arm_nt
;; @in r0 = hRoot, r1 = UTF-16LE subkey, r2 = UTF-16LE valname (or 0), r3 = WTF-8 out buf (512B)
;; @out r0 = 0 on success or ERR_FILE_NOT_FOUND
;; @err r0 = ERR_FILE_NOT_FOUND when missing
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries a REG_SZ/REG_EXPAND_SZ Registry value via dynamically loaded ADVAPI32.DLL.
@proc hal_reg_query_str
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #548
    mov r4, r2
    mov r5, r3
    mov r6, r0
    mov r7, r1

    bl hal_get_ctx
    ldr r12, [r0, #ARM32_CTX.pfnRegOpenKeyExW]
    cmp r12, #0
    beq .nf

    add r2, sp, #16
    str r2, [sp, #0]
    mov r0, r6
    mov r1, r7
    mov r2, #0
    mov_imm32 r3, 0x00020019 ; KEY_READ
    blx r12
    cmp r0, #0
    bne .nf

    ldr r6, [sp, #16]
    mov r0, #512
    str r0, [sp, #20]
    add r0, sp, #20
    str r0, [sp, #4]
    add r0, sp, #32
    str r0, [sp, #0]
    bl hal_get_ctx
    ldr r12, [r0, #ARM32_CTX.pfnRegQueryValueExW]
    mov r0, r6
    mov r1, r4
    mov r2, #0
    add r3, sp, #24
    blx r12
    mov r4, r0
    bl hal_get_ctx
    ldr r12, [r0, #ARM32_CTX.pfnRegCloseKey]
    mov r0, r6
    blx r12
    cmp r4, #0
    bne .nf

    add r0, sp, #32
    mov r1, #255
    mov r2, r5
    bl u16_to_wtf8_buf_a32
    mov r0, #0
    add sp, sp, #548
    pop {r4, r5, r6, r7, lr}
    bx lr
.nf:
    mov r0, #ERR_FILE_NOT_FOUND
    add sp, sp, #548
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc hal_reg_set_str
;; @abi arm_nt
;; @in r0 = hRoot, r1 = UTF-16LE subkey, r2 = UTF-16LE valname (or 0), r3 = UTF-16LE data
;; @out r0 = 0 on success or ERR_ACCESS_DENIED
;; @err r0 = ERR_ACCESS_DENIED on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes a REG_SZ Registry value via dynamically loaded ADVAPI32.DLL.
@proc hal_reg_set_str
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #44
    mov r4, r2
    mov r5, r3
    mov r6, r0
    mov r7, r1

    bl hal_get_ctx
    ldr r12, [r0, #ARM32_CTX.pfnRegCreateKeyExW]
    cmp r12, #0
    beq .den

    mov r2, #0
    str r2, [sp, #0]
    mov_imm32 r3, 0x00020006 ; KEY_WRITE
    str r3, [sp, #4]
    str r2, [sp, #8]
    add r3, sp, #24
    str r3, [sp, #12]
    add r3, sp, #28
    str r3, [sp, #16]
    mov r0, r6
    mov r1, r7
    mov r3, #0
    blx r12
    cmp r0, #0
    bne .den

    ldr r6, [sp, #24]
    mov r7, #0
.wlen2:
    add r0, r5, r7
    ldrh r1, [r0, #0]
    add r7, r7, #2
    cmp r1, #0
    bne .wlen2

    str r7, [sp, #4]
    str r5, [sp, #0]
    bl hal_get_ctx
    ldr r12, [r0, #ARM32_CTX.pfnRegSetValueExW]
    mov r0, r6
    mov r1, r4
    mov r2, #0
    mov r3, #1 ; REG_SZ
    blx r12
    mov r4, r0
    bl hal_get_ctx
    ldr r12, [r0, #ARM32_CTX.pfnRegCloseKey]
    mov r0, r6
    blx r12
    cmp r4, #0
    bne .den
    mov r0, #0
    add sp, sp, #44
    pop {r4, r5, r6, r7, lr}
    bx lr
.den:
    mov r0, #ERR_ACCESS_DENIED
    add sp, sp, #44
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc hal_build_u16_env_rt7
;; @abi arm_nt
;; @in none
;; @out r0 = UTF-16LE double-NUL environment block in arena
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Transcodes active WTF-8 environment block into CREATE_UNICODE_ENVIRONMENT UTF-16LE block.
@proc hal_build_u16_env_rt7
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    bl hal_get_ctx
    ldr r4, [r0, #ARM32_CTX.pEnvBlock]
    ldr r5, [r0, #ARM32_CTX.dwEnvLen]
    add r0, r5, #8
    lsl r0, r0, #1
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r6, r0 ; out base
    mov r7, r0 ; out cursor
.lp:
    ldrb r0, [r4, #0]
    cbz r0, .fin
    mov r0, r4
    bl wtf8_to_u16_a32
    cbz r0, .fin
    lsl r2, r1, #1
    add r2, r2, #2 ; include NUL wchar
    mov r1, r0
    mov r0, r7
    bl mem_copy_a32
    mov r7, r0
.adv:
    ldrb r0, [r4, #0]
    add r4, r4, #1
    cmp r0, #0
    bne .adv
    b .lp
.fin:
    mov r0, #0
    strh r0, [r7, #0]
    strh r0, [r7, #2]
    mov r0, r6
.done:
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc hal_spawn_process
;; @abi arm_nt
;; @in r0 = UTF-16LE app, r1 = UTF-16LE cmdline, r2 = UTF-16LE cwd, r3 = waitFlag
;; @out r0 = exit code, -2 if 16-bit DOS .COM/MZ (vm8086 handoff), or -1 on error
;; @err r0 < 0 on error or vm8086 handoff
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Spawns a Win32 process with inherited stdio handles and CREATE_UNICODE_ENVIRONMENT, handing .COM/MZ to vm8086.
@proc hal_spawn_process
    push {r4, r5, r6, r7, r8, lr}
    sub sp, sp, #128
    mov r4, r0 ; app
    mov r5, r1 ; cmd
    mov r6, r2 ; cwd
    mov r7, r3 ; wait

    ; Build STARTUPINFOW (68 bytes) at sp+24 and PROCESS_INFORMATION (16 bytes) at sp+96
    add r0, sp, #24
    mov r1, #96
    bl mem_zero_a32
    mov r0, #68
    str r0, [sp, #24]
    mov_imm32 r0, 0x00000100 ; STARTF_USESTDHANDLES
    str r0, [sp, #68]        ; dwFlags (+44)
    bl hal_get_ctx
    add r1, r0, #ARM32_CTX.fdTable
    ldr r2, [r1, #0]
    str r2, [sp, #80] ; hStdInput (+56)
    ldr r2, [r1, #4]
    str r2, [sp, #84] ; hStdOutput (+60)
    ldr r2, [r1, #8]
    str r2, [sp, #88] ; hStdError (+64)

    bl hal_build_u16_env_rt7
    mov r8, r0

    add r0, sp, #96
    str r0, [sp, #20] ; lpProcessInformation
    add r0, sp, #24
    str r0, [sp, #16] ; lpStartupInfo
    str r6, [sp, #12] ; lpCurrentDirectory
    str r8, [sp, #8]  ; lpEnvironment
    mov_imm32 r0, 0x00000400 ; CREATE_UNICODE_ENVIRONMENT
    str r0, [sp, #4]  ; dwCreationFlags
    mov r0, #1
    str r0, [sp, #0]  ; bInheritHandles
    mov r0, r4
    mov r1, r5
    mov r2, #0
    mov r3, #0
    call_iat __imp_CreateProcessW
    cmp r0, #0
    beq .chk_dos_err

    ldr r4, [sp, #96]  ; hProcess
    ldr r0, [sp, #100] ; hThread
    call_iat __imp_CloseHandle
    cbz r7, .no_wt
    mov r0, r4
    mvn r1, #0
    call_iat __imp_WaitForSingleObject
    mov r0, r4
    add r1, sp, #112
    call_iat __imp_GetExitCodeProcess
    mov r0, r4
    call_iat __imp_CloseHandle
    ldr r0, [sp, #112]
    add sp, sp, #128
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.no_wt:
    mov r0, r4
    call_iat __imp_CloseHandle
    mov r0, #0
    add sp, sp, #128
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.chk_dos_err:
    call_iat __imp_GetLastError
    cmp r0, #193 ; ERROR_BAD_EXE_FORMAT
    beq .handoff
    cmp r0, #216 ; ERROR_EXE_MACHINE_TYPE_MISMATCH
    beq .handoff
    mvn r0, #0
    add sp, sp, #128
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
.handoff:
    mvn r0, #1 ; -2 -> vm8086
    add sp, sp, #128
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc hal_run_pipe_concurrent
;; @abi arm_nt
;; @in r0 = WTF-8 left stage command, r1 = WTF-8 right stage command
;; @out r0 = right stage exit code (or -1 if CreatePipe unavailable)
;; @err r0 = -1 on pipe creation failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Executes `left | right` concurrently via CreatePipe(64 KiB) + SetHandleInformation + parallel subshells.
@proc hal_run_pipe_concurrent
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #240
    mov r4, r0 ; left WTF-8
    mov r5, r1 ; right WTF-8

    ; Inheritable SECURITY_ATTRIBUTES at sp+32
    mov r0, #12
    str r0, [sp, #32]
    mov r0, #0
    str r0, [sp, #36]
    mov r0, #1
    str r0, [sp, #40]

    add r0, sp, #44 ; &hReadPipe
    add r1, sp, #48 ; &hWritePipe
    add r2, sp, #32
    mov_imm32 r3, 0x00010000 ; 64 KiB pipe buffer hint
    call_iat __imp_CreatePipe
    cmp r0, #0
    beq .pipe_fail

    ldr r6, [sp, #44] ; hReadPipe
    ldr r7, [sp, #48] ; hWritePipe

    ; Build left cmdline: `"<SelfExe>" /S /D /C "<left>"` and right cmdline
    mov r0, r4
    bl hal_build_pipe_subcmd_rt7
    mov r8, r0
    mov r0, r5
    bl hal_build_pipe_subcmd_rt7
    mov r9, r0

    ; Mark hReadPipe non-inheritable while spawning left writer
    mov r0, r6
    mov r1, #1 ; HANDLE_FLAG_INHERIT
    mov r2, #0
    call_iat __imp_SetHandleInformation

    ; Spawn left stage with stdout = hWritePipe
    bl hal_get_ctx
    add r1, r0, #ARM32_CTX.fdTable
    ldr r0, [r1, #0] ; stdin
    mov r1, r7       ; stdout = hWritePipe
    bl hal_get_ctx
    add r2, r0, #ARM32_CTX.fdTable
    ldr r2, [r2, #8] ; stderr
    mov r3, r8
    bl hal_spawn_pipe_child_rt7
    mov r8, r0 ; hProcLeft

    ; Close parent write end immediately so EOF propagates!
    mov r0, r7
    call_iat __imp_CloseHandle

    ; Re-enable inherit on hReadPipe for right reader
    mov r0, r6
    mov r1, #1
    mov r2, #1
    call_iat __imp_SetHandleInformation

    ; Spawn right stage with stdin = hReadPipe
    mov r0, r6
    bl hal_get_ctx
    add r2, r0, #ARM32_CTX.fdTable
    ldr r1, [r2, #4] ; stdout
    ldr r2, [r2, #8] ; stderr
    mov r0, r6
    mov r3, r9
    bl hal_spawn_pipe_child_rt7
    mov r9, r0 ; hProcRight

    mov r0, r6
    call_iat __imp_CloseHandle

    ; Wait for both children and return right stage ExitCode
    cmp r8, #0
    beq .wt_right
    mov r0, r8
    mvn r1, #0
    call_iat __imp_WaitForSingleObject
    mov r0, r8
    call_iat __imp_CloseHandle
.wt_right:
    mov r10, #1
    cmp r9, #0
    beq .pipe_done
    mov r0, r9
    mvn r1, #0
    call_iat __imp_WaitForSingleObject
    mov r0, r9
    add r1, sp, #52
    call_iat __imp_GetExitCodeProcess
    ldr r10, [sp, #52]
    mov r0, r9
    call_iat __imp_CloseHandle
.pipe_done:
    mov r0, r10
    add sp, sp, #240
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.pipe_fail:
    mvn r0, #0
    add sp, sp, #240
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc hal_build_pipe_subcmd_rt7
;; @abi arm_nt
;; @in r0 = WTF-8 stage command string
;; @out r0 = UTF-16LE `"<SelfExe>" /S /D /C "<stage>"` in arena
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Formats a subshell invocation command line for concurrent pipeline execution.
@proc hal_build_pipe_subcmd_rt7
    push {r4, r5, r6, lr}
    mov r4, r0
    bl str_len_a32
    mov_imm32 r1, 600
    add r0, r0, r1
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r5, r0
    mov r6, r0
    mov r1, #0x22 ; '"'
    strb r1, [r6, #0]
    add r6, r6, #1
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.pSelfExePath]
    mov r0, r6
    bl str_copy_a32
    mov r6, r0
    adr_sym r1, szPipeSubFlags
    mov r0, r6
    bl str_copy_a32
    mov r6, r0
    mov r1, r4
    mov r0, r6
    bl str_copy_a32
    mov r1, #0x22 ; closing '"'
    strb r1, [r0, #0]
    mov r1, #0
    strb r1, [r0, #1]
    mov r0, r5
    bl wtf8_to_u16_a32
.done:
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc hal_spawn_pipe_child_rt7
;; @abi arm_nt
;; @in r0 = hIn, r1 = hOut, r2 = hErr, r3 = UTF-16LE cmdline
;; @out r0 = hProcess (or 0 on failure)
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Spawns an asynchronous pipeline stage child process with explicit stdio handles.
@proc hal_spawn_pipe_child_rt7
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #116
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r7, r3

    add r0, sp, #24
    mov r1, #88
    bl mem_zero_a32
    mov r0, #68
    str r0, [sp, #24]
    mov_imm32 r0, 0x00000100 ; STARTF_USESTDHANDLES
    str r0, [sp, #68]
    str r4, [sp, #80]
    str r5, [sp, #84]
    str r6, [sp, #88]

    bl hal_build_u16_env_rt7
    mov r4, r0

    add r0, sp, #96
    str r0, [sp, #20]
    add r0, sp, #24
    str r0, [sp, #16]
    mov r0, #0
    str r0, [sp, #12]
    str r4, [sp, #8]
    mov_imm32 r0, 0x00000400
    str r0, [sp, #4]
    mov r0, #1
    str r0, [sp, #0]
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.pSelfExePath]
    bl wtf8_to_u16_a32
    mov r1, r7
    mov r2, #0
    mov r3, #0
    call_iat __imp_CreateProcessW
    cbz r0, .fail
    ldr r0, [sp, #100] ; hThread
    call_iat __imp_CloseHandle
    ldr r0, [sp, #96]  ; hProcess
    add sp, sp, #116
    pop {r4, r5, r6, r7, lr}
    bx lr
.fail:
    mov r0, #0
    add sp, sp, #116
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc hal_check_case_sensitive_dir
;; @abi arm_nt
;; @in r0 = UTF-16LE directory path
;; @out r0 = 1 if Win10/11 FileCaseSensitiveInformation flag bit 0 is set, else 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries NtQueryInformationFile(FileCaseSensitiveInformation = 71) on Windows 10 1803+ / Windows 11.
@proc hal_check_case_sensitive_dir
    push {r4, r5, lr}
    sub sp, sp, #36
    mov r4, r0
    bl hal_get_ctx
    ldr r5, [r0, #ARM32_CTX.pfnNtQueryInformationFile]
    cmp r5, #0
    beq .not_cs

    mov r0, #OPEN_EXISTING
    str r0, [sp, #0]
    mov_imm32 r0, 0x02000000 ; FILE_FLAG_BACKUP_SEMANTICS
    str r0, [sp, #4]
    mov r0, #0
    str r0, [sp, #8]
    mov r0, r4
    mov_imm32 r1, 0x00000080 ; FILE_READ_ATTRIBUTES
    mov r2, #7
    mov r3, #0
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .not_cs
    mov r4, r0

    mov r0, #0
    str r0, [sp, #24]
    mov r0, #71 ; FileCaseSensitiveInformation
    str r0, [sp, #0]
    mov r0, r4
    add r1, sp, #16 ; IO_STATUS_BLOCK
    add r2, sp, #24 ; FILE_CASE_SENSITIVE_INFORMATION
    mov r3, #4
    blx r5
    mov r5, r0
    mov r0, r4
    call_iat __imp_CloseHandle
    cmp r5, #0
    bne .not_cs
    ldr r0, [sp, #24]
    and r0, r0, #1
    add sp, sp, #36
    pop {r4, r5, lr}
    bx lr
.not_cs:
    mov r0, #0
    add sp, sp, #36
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_exit_process
;; @abi arm_nt
;; @in r0 = exit code
;; @out Terminates process via KERNEL32!ExitProcess
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Terminates the current ARMNT process with exit code r0.
@proc hal_exit_process
    push {r4, lr}
    call_iat __imp_ExitProcess
    pop {r4, lr}
    bx lr
@endproc
