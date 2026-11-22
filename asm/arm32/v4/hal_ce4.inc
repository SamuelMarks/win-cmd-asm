;; @file hal_ce4.inc
;; @isa armv4
;; @desc Strict ARMv4 (0x01C0) Windows CE / Pocket PC / Embedded Compact COREDLL.DLL HAL and trap-safe math.

@cpu armv4
@format inc

;; @proc hal_get_ctx
;; @abi arm_ce
;; @in none
;; @out r0 = pointer to g_a32_ctx in .bss
;; @err none
;; @clobbers r0
;; @desc Loads the 32-bit address of the central ARM32_CTX runtime state using pure ARMv4 instructions.
@proc hal_get_ctx
    adr_sym r0, g_a32_ctx
    bx lr
@endproc

;; @proc hal_get_str_pool
;; @abi arm_ce
;; @in none
;; @out r0 = pointer to g_a32_str_pool in .rdata
;; @err none
;; @clobbers r0
;; @desc Loads the 32-bit address of the constant string pool using pure ARMv4 instructions.
@proc hal_get_str_pool
    adr_sym r0, g_a32_str_pool
    bx lr
@endproc

;; @proc math_udiv32
;; @abi arm_ce
;; @in r0 = unsigned dividend, r1 = unsigned divisor
;; @out r0 = quotient, r1 = remainder, r2 = 0 (or ERR_DIV_BY_ZERO if r1 == 0)
;; @err r2 = ERR_DIV_BY_ZERO on zero divisor
;; @clobbers r0, r1, r2, r3
;; @desc CLZ-free, UDIV-free 32-bit restoring unsigned division strictly legal on ARMv4.
@proc math_udiv32
    cmp r1, #0
    beq .div_zero
    push {r4, r5}
    mov r2, #0 ; quotient
    mov r3, #0 ; remainder
    mov r4, #32
.bit_lp:
    adds r0, r0, r0
    adc r3, r3, r3
    lsl r2, r2, #1
    cmp r3, r1
    bcc .no_sub
    sub r3, r3, r1
    orr r2, r2, #1
.no_sub:
    subs r4, r4, #1
    bne .bit_lp
    mov r0, r2
    mov r1, r3
    mov r2, #0
    pop {r4, r5}
    bx lr
.div_zero:
    mov r0, #0
    mov r1, #0
    mov_imm32 r2, ERR_DIV_BY_ZERO
    bx lr
@endproc

;; @proc math_sdiv32
;; @abi arm_ce
;; @in r0 = signed 32-bit dividend, r1 = signed 32-bit divisor
;; @out r0 = signed quotient, r1 = signed remainder, r2 = 0 (or ERR_DIV_BY_ZERO)
;; @err r2 = ERR_DIV_BY_ZERO when divisor is 0
;; @clobbers r0, r1, r2, r3
;; @desc Trap-safe signed 32-bit division guarding /0 and INT32_MIN / -1 for SET /A parity on ARMv4.
@proc math_sdiv32
    cmp r1, #0
    beq .sdiv_zero
    push {r4, r5, r6, lr}
    ; Guard INT32_MIN / -1 -> 0x80000000, rem 0
    mov_imm32 r2, 0x80000000
    cmp r0, r2
    bne .norm
    cmn r1, #1
    bne .norm
    mov r1, #0
    mov r2, #0
    pop {r4, r5, r6, lr}
    bx lr
.norm:
    eor r4, r0, r1 ; sign of quotient in bit 31
    mov r5, r0     ; sign of remainder in bit 31
    cmp r0, #0
    bge .abs_num_ok
    mov r6, #0
    sub r0, r6, r0
.abs_num_ok:
    cmp r1, #0
    bge .abs_den_ok
    mov r6, #0
    sub r1, r6, r1
.abs_den_ok:
    bl math_udiv32
    cmp r4, #0
    bge .q_pos
    mov r6, #0
    sub r0, r6, r0
.q_pos:
    cmp r5, #0
    bge .r_pos
    mov r6, #0
    sub r1, r6, r1
.r_pos:
    mov r2, #0
    pop {r4, r5, r6, lr}
    bx lr
.sdiv_zero:
    mov r0, #0
    mov r1, #0
    mov_imm32 r2, ERR_DIV_BY_ZERO
    bx lr
@endproc

;; @proc hal_virtual_alloc
;; @abi arm_ce
;; @in r0 = lpAddress, r1 = dwSize, r2 = flAllocationType, r3 = flProtect
;; @out r0 = allocated address or 0
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Invokes COREDLL!VirtualAlloc via strict ARMv4 indirect BX call.
@proc hal_virtual_alloc
    push {r4, lr}
    call_iat __imp_VirtualAlloc
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_virtual_free
;; @abi arm_ce
;; @in r0 = lpAddress, r1 = dwSize, r2 = dwFreeType
;; @out r0 = non-zero on success
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Invokes COREDLL!VirtualFree.
@proc hal_virtual_free
    push {r4, lr}
    call_iat __imp_VirtualFree
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_populate_find_ce4
;; @abi arm_ce
;; @in r0 = WIN32_FIND_DATAW*, r1 = FIND_INFO_A32*
;; @out Populates FIND_INFO_A32 from WinCE WIN32_FIND_DATAW
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Extracts attributes, 64-bit size, write FILETIME, and cFileName from WinCE WIN32_FIND_DATAW.
@proc hal_populate_find_ce4
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
    mov r0, #0
    strb r0, [r5, #FIND_INFO_A32.szShort]
    ; WinCE WIN32_FIND_DATAW has dwOID at +36 and cFileName[260] at +40 (standard Win32 has cFileName at +44; check +40 != 0 else +44)
    add r0, r4, #40
    ldrh r1, [r0, #0]
    cmp r1, #0
    bne .conv_name
    add r0, r4, #44
.conv_name:
    mov r1, #260
    add r2, r5, #FIND_INFO_A32.szName
    bl u16_to_wtf8_buf_a32
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_init_platform
;; @abi arm_ce
;; @in r0 = ARM32_CTX*
;; @out r0 = 0 on success or ERR_* on failure
;; @err r0 != 0 on VirtualAlloc failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Initializes Windows CE 4.x+ ARMv4 platform state, COREDLL handles, rootless CWD ``, and arena.
@proc hal_init_platform
    push {r4, r5, r6, lr}
    sub sp, sp, #288
    mov r4, r0

    mov r1, #1
    strb r1, [r4, #ARM32_CTX.bIsWinCe]
    strb r1, [r4, #ARM32_CTX.bEchoOn]
    strb r1, [r4, #ARM32_CTX.bExtensions]
    strb r1, [r4, #ARM32_CTX.bFileCompl]
    strb r1, [r4, #ARM32_CTX.bBreakOn]
    mov r1, #0x07
    strh r1, [r4, #ARM32_CTX.wDefaultAttr]
    strh r1, [r4, #ARM32_CTX.wCurrentAttr]
    mov_imm32 r1, 65001
    str r1, [r4, #ARM32_CTX.dwCodePage]

    ; Standard WinCE pseudo console file handles (0=stdin, 1=stdout, 2=stderr)
    mov r1, #0
    str r1, [r4, #ARM32_CTX.hStdIn]
    mov r1, #1
    str r1, [r4, #ARM32_CTX.hStdOut]
    mov r1, #2
    str r1, [r4, #ARM32_CTX.hStdErr]
    add r2, r4, #ARM32_CTX.fdTable
    mov r1, #0
    str r1, [r2, #0]
    mov r1, #1
    str r1, [r2, #4]
    mov r1, #2
    str r1, [r2, #8]

    ; Query OS version via COREDLL!GetVersionExW (OSVERSIONINFOW = 276 bytes)
    mov r0, sp
    mov r1, #276
    bl mem_zero_a32
    mov_imm32 r0, 276
    str r0, [sp, #0]
    mov r0, sp
    call_iat __imp_GetVersionExW
    cmp r0, #0
    beq .def_ce_ver
    ldr r0, [sp, #4]
    str r0, [r4, #ARM32_CTX.dwOsMajor]
    ldr r0, [sp, #8]
    str r0, [r4, #ARM32_CTX.dwOsMinor]
    ldr r0, [sp, #12]
    str r0, [r4, #ARM32_CTX.dwOsBuild]
    b .after_ver
.def_ce_ver:
    mov r0, #4
    str r0, [r4, #ARM32_CTX.dwOsMajor]
    mov r0, #20
    str r0, [r4, #ARM32_CTX.dwOsMinor]
    mov_imm32 r0, 1088
    str r0, [r4, #ARM32_CTX.dwOsBuild]

.after_ver:
    call_iat __imp_GetTickCount
    orr r0, r0, #1
    str r0, [r4, #ARM32_CTX.dwRandomSeed]

    ; Rootless Windows CE initial CWD = ""
    add r0, r4, #ARM32_CTX.szCwd
    mov r1, #0x5C
    strb r1, [r0, #0]
    mov r1, #0
    strb r1, [r0, #1]

    ; Initialize VirtualAlloc arena & persistent pools
    mov r0, r4
    bl arena_init_a32
    cmp r0, #0
    bne .exit_init

    ; Populate executable path & raw command line
    mov r0, #0
    mov r1, sp
    mov r2, #128
    call_iat __imp_GetModuleFileNameW
    cmp r0, #0
    beq .def_self
    mov r0, sp
    bl u16_to_wtf8_arena_a32
    mov r5, r0
    bl str_len_a32
    add r0, r0, #2
    bl perm_alloc_a32
    mov r6, r0
    mov r1, r5
    bl str_copy_a32
    str r6, [r4, #ARM32_CTX.pSelfExePath]
    b .got_self
.def_self:
    mov r0, #32
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pSelfExePath]
    adr_sym r1, szCeDefComspec
    bl str_copy_a32

.got_self:
    call_iat __imp_GetCommandLineW
    cmp r0, #0
    beq .no_cmd
    bl u16_to_wtf8_arena_a32
    mov r5, r0
    bl str_len_a32
    add r0, r0, #2
    bl perm_alloc_a32
    mov r6, r0
    mov r1, r5
    bl str_copy_a32
    str r6, [r4, #ARM32_CTX.pCmdLineRaw]
.no_cmd:
    ; Seed default WinCE environment block (PATH, COMSPEC, PROMPT, OS)
    ldr r0, [r4, #ARM32_CTX.pEnvBlock]
    adr_sym r1, szCeDefaultEnv
    mov r2, #szCeDefaultEnv_len
    bl mem_copy_a32
    mov r0, #szCeDefaultEnv_len
    str r0, [r4, #ARM32_CTX.dwEnvLen]

    mov r0, #0
.exit_init:
    add sp, sp, #288
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc hal_write_handle
;; @abi arm_ce
;; @in r0 = handle, r1 = WTF-8 buffer, r2 = byte length
;; @out r0 = 0 on success or ERR_WRITE_FAULT
;; @err r0 = ERR_WRITE_FAULT on WriteFile error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes WTF-8 (or /U UTF-16LE) bytes to a COREDLL file/console handle.
@proc hal_write_handle
    push {r4, r5, r6, lr}
    sub sp, sp, #16
    mov r4, r0 ; handle
    mov r5, r1 ; buf
    mov r6, r2 ; len
    cmp r6, #0
    beq .ok

    bl hal_get_ctx
    ldrb r0, [r0, #ARM32_CTX.bUnicodeOut]
    cmp r0, #0
    beq .raw_write
    ; If /U active and handle > 2 (redirected file/pipe), emit UTF-16LE
    cmp r4, #2
    bls .raw_write
    mov r0, r6
    add r0, r0, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .raw_write
    mov r3, r0
    mov r1, r5
    mov r2, r6
    push {r3, r4}
    bl mem_copy_a32
    mov r2, #0
    strb r2, [r0, #0]
    pop {r3, r4}
    mov r0, r3
    bl wtf8_to_u16_a32
    cmp r0, #0
    beq .raw_write
    mov r5, r0
    lsl r6, r1, #1

.raw_write:
    mov r0, #0
    str r0, [sp, #0] ; lpOverlapped = NULL on stack arg #5
    mov r0, r4
    mov r1, r5
    mov r2, r6
    add r3, sp, #8   ; &dwWritten
    call_iat __imp_WriteFile
    cmp r0, #0
    beq .fail
.ok:
    mov r0, #0
    add sp, sp, #16
    pop {r4, r5, r6, lr}
    bx lr
.fail:
    mov r0, #ERR_WRITE_FAULT
    add sp, sp, #16
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc hal_read_handle
;; @abi arm_ce
;; @in r0 = handle, r1 = dst buffer, r2 = max bytes
;; @out r0 = bytes read (0 on EOF)
;; @err r0 = 0 on EOF/failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reads up to r2 bytes from a COREDLL handle via ReadFile.
@proc hal_read_handle
    push {r4, lr}
    sub sp, sp, #16
    mov r3, #0
    str r3, [sp, #0]
    str r3, [sp, #8]
    add r3, sp, #8
    call_iat __imp_ReadFile
    cmp r0, #0
    beq .eof
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
;; @abi arm_ce
;; @in r0 = UTF-16LE path, r1 = mode (0=read, 1=write_trunc, 2=write_append, 3=rw_open)
;; @out r0 = file handle or -1 on error
;; @err r0 = -1 (INVALID_HANDLE_VALUE) on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Opens or creates a WinCE file via COREDLL!CreateFileW and seeks to end on append mode.
@proc hal_open_file
    push {r4, r5, lr}
    sub sp, sp, #20
    mov r4, r0 ; path
    mov r5, r1 ; mode

    cmp r5, #0
    beq .m_read
    cmp r5, #1
    beq .m_trunc
    cmp r5, #2
    beq .m_append
    mov_imm32 r1, 0xC0000000
    mov r2, #OPEN_EXISTING
    b .do_open
.m_read:
    mov_imm32 r1, GENERIC_READ
    mov r2, #OPEN_EXISTING
    b .do_open
.m_trunc:
    mov_imm32 r1, GENERIC_WRITE
    mov r2, #CREATE_ALWAYS
    b .do_open
.m_append:
    mov_imm32 r1, GENERIC_WRITE
    mov r2, #OPEN_ALWAYS

.do_open:
    str r2, [sp, #0] ; dwCreationDisposition
    mov r3, #FILE_ATTRIBUTE_NORMAL
    str r3, [sp, #4] ; dwFlagsAndAttributes
    mov r3, #0
    str r3, [sp, #8] ; hTemplateFile
    mov r0, r4
    mov r2, #3       ; FILE_SHARE_READ | FILE_SHARE_WRITE
    mov r3, #0       ; lpSecurityAttributes
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .done
    cmp r5, #2
    bne .done
    mov r4, r0
    mov r1, #0
    mov r2, #0
    mov r3, #2 ; FILE_END
    call_iat __imp_SetFilePointer
    mov r0, r4
.done:
    add sp, sp, #20
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_seek_file
;; @abi arm_ce
;; @in r0 = handle, r1 = off_lo, r2 = off_hi, r3 = method (0=BEGIN, 1=CUR, 2=END)
;; @out r0 = new_off_lo, r1 = new_off_hi (or r0 = -1 on error)
;; @err r0 = -1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Seeks a WinCE file handle via COREDLL!SetFilePointer.
@proc hal_seek_file
    push {r4, lr}
    sub sp, sp, #8
    str r2, [sp, #0]
    add r2, sp, #0
    call_iat __imp_SetFilePointer
    ldr r1, [sp, #0]
    add sp, sp, #8
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_close_handle
;; @abi arm_ce
;; @in r0 = handle
;; @out r0 = non-zero on success
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Closes a COREDLL handle when > 2 (skipping pseudo stdio handles 0..2).
@proc hal_close_handle
    cmp r0, #2
    bls .pseudo_ok
    cmn r0, #1
    beq .pseudo_ok
    push {r4, lr}
    call_iat __imp_CloseHandle
    pop {r4, lr}
    bx lr
.pseudo_ok:
    mov r0, #1
    bx lr
@endproc

;; @proc hal_get_file_attr
;; @abi arm_ce
;; @in r0 = UTF-16LE path
;; @out r0 = Win32 attributes or -1 on error
;; @err r0 = -1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries file attributes via COREDLL!GetFileAttributesW.
@proc hal_get_file_attr
    push {r4, lr}
    call_iat __imp_GetFileAttributesW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_set_file_attr
;; @abi arm_ce
;; @in r0 = UTF-16LE path, r1 = dwFileAttributes
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Updates file attributes via COREDLL!SetFileAttributesW.
@proc hal_set_file_attr
    push {r4, lr}
    call_iat __imp_SetFileAttributesW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_delete_file
;; @abi arm_ce
;; @in r0 = UTF-16LE path
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Deletes a file via COREDLL!DeleteFileW.
@proc hal_delete_file
    push {r4, lr}
    call_iat __imp_DeleteFileW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_mkdir
;; @abi arm_ce
;; @in r0 = UTF-16LE path
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Creates a directory via COREDLL!CreateDirectoryW.
@proc hal_mkdir
    push {r4, lr}
    mov r1, #0
    call_iat __imp_CreateDirectoryW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_rmdir
;; @abi arm_ce
;; @in r0 = UTF-16LE path
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Removes an empty directory or reparse link via COREDLL!RemoveDirectoryW.
@proc hal_rmdir
    push {r4, lr}
    call_iat __imp_RemoveDirectoryW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_move_file
;; @abi arm_ce
;; @in r0 = UTF-16LE src, r1 = UTF-16LE dst
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Moves/renames a file or directory on WinCE (deleting existing destination first if needed).
@proc hal_move_file
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    call_iat __imp_MoveFileW
    cmp r0, #0
    bne .done
    mov r0, r5
    call_iat __imp_DeleteFileW
    mov r0, r4
    mov r1, r5
    call_iat __imp_MoveFileW
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_copy_file
;; @abi arm_ce
;; @in r0 = UTF-16LE src, r1 = UTF-16LE dst, r2 = fail_if_exists
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Copies a file via COREDLL!CopyFileW.
@proc hal_copy_file
    push {r4, lr}
    call_iat __imp_CopyFileW
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_find_first
;; @abi arm_ce
;; @in r0 = UTF-16LE wildcard pattern, r1 = FIND_INFO_A32*
;; @out r0 = search handle or -1 on no match
;; @err r0 = -1 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Starts a directory enumeration via COREDLL!FindFirstFileW.
@proc hal_find_first
    push {r4, r5, lr}
    sub sp, sp, #604
    mov r4, r1
    mov r1, sp
    push {r0, r1}
    mov r0, sp
    add r0, r0, #8
    mov_imm32 r1, 596
    bl mem_zero_a32
    pop {r0, r1}
    call_iat __imp_FindFirstFileW
    cmn r0, #1
    beq .done
    mov r5, r0
    mov r0, sp
    mov r1, r4
    bl hal_populate_find_ce4
    mov r0, r5
.done:
    add sp, sp, #604
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_find_next
;; @abi arm_ce
;; @in r0 = search handle, r1 = FIND_INFO_A32*
;; @out r0 = BOOL (1 = entry populated, 0 = end)
;; @err r0 = 0 when enumeration finishes
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Continues a directory enumeration via COREDLL!FindNextFileW.
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
    cmp r0, #0
    beq .done
    mov r0, sp
    mov r1, r5
    bl hal_populate_find_ce4
    mov r0, #1
.done:
    add sp, sp, #604
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_find_close
;; @abi arm_ce
;; @in r0 = search handle
;; @out r0 = BOOL
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Closes a search handle via COREDLL!FindClose.
@proc hal_find_close
    push {r4, lr}
    call_iat __imp_FindClose
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_local_time
;; @abi arm_ce
;; @in r0 = SYSTEMTIME* (16 bytes, 2-byte aligned)
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries local wall-clock time via COREDLL!GetLocalTime.
@proc hal_get_local_time
    push {r4, lr}
    call_iat __imp_GetLocalTime
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_set_local_time
;; @abi arm_ce
;; @in r0 = SYSTEMTIME* (16 bytes)
;; @out r0 = BOOL
;; @err r0 = 0 on privilege/validation error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Sets local wall-clock time via COREDLL!SetLocalTime.
@proc hal_set_local_time
    push {r4, lr}
    call_iat __imp_SetLocalTime
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_tick_count
;; @abi arm_ce
;; @in none
;; @out r0 = millisecond tick count
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries millisecond uptime via COREDLL!GetTickCount.
@proc hal_get_tick_count
    push {r4, lr}
    call_iat __imp_GetTickCount
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_get_disk_free
;; @abi arm_ce
;; @in r0 = UTF-16LE directory path, r1 = 8-byte output pointer (u64 free bytes)
;; @out r0 = BOOL
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries 64-bit free object-store/storage-card space via COREDLL!GetDiskFreeSpaceExW.
@proc hal_get_disk_free
    push {r4, lr}
    sub sp, sp, #24
    mov r4, r1
    add r1, sp, #0  ; lpFreeBytesAvailableToCaller
    add r2, sp, #8  ; lpTotalNumberOfBytes
    add r3, sp, #16 ; lpTotalNumberOfFreeBytes
    call_iat __imp_GetDiskFreeSpaceExW
    cmp r0, #0
    beq .fail
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
;; @abi arm_ce
;; @in r0 = console attribute byte
;; @out r0 = 1
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Stores current console color attribute in ARM32_CTX on Windows CE.
@proc hal_set_console_attr
    push {r4, lr}
    mov r4, r0
    bl hal_get_ctx
    strh r4, [r0, #ARM32_CTX.wCurrentAttr]
    mov r0, #1
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_clear_console
;; @abi arm_ce
;; @in none
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Emits ANSI/VT clear screen sequence on Windows CE console output.
@proc hal_clear_console
    push {r4, lr}
    adr_sym r0, szCeVtClear
    bl out_str_a32
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_set_title
;; @abi arm_ce
;; @in r0 = UTF-16LE title
;; @out r0 = 1
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Stores active window title into ARM32_CTX.szTitle on Windows CE.
@proc hal_set_title
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    bl hal_get_ctx
    mov r5, r0
    mov r0, r4
    mov r1, #120
    mov_imm32 r2, ARM32_CTX.szTitle
    add r2, r5, r2
    bl u16_to_wtf8_buf_a32
    mov r0, #1
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc hal_get_set_cp
;; @abi arm_ce
;; @in r0 = new codepage (0 = query current)
;; @out r0 = active codepage
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries or updates active shell codepage (65001 UTF-8, 437, 932, 936, etc.) on WinCE.
@proc hal_get_set_cp
    push {r4, lr}
    mov r4, r0
    bl hal_get_ctx
    cmp r4, #0
    beq .get_only
    str r4, [r0, #ARM32_CTX.dwCodePage]
.get_only:
    ldr r0, [r0, #ARM32_CTX.dwCodePage]
    pop {r4, lr}
    bx lr
@endproc

;; @proc hal_mklink
;; @abi arm_ce
;; @in r0 = mode (0=file, 1=/D, 2=/H, 3=/J), r1 = UTF-16LE link, r2 = UTF-16LE target
;; @out r0 = 0 on success or ERR_*
;; @err r0 = ERR_ACCESS_DENIED when unsupported on FAT object store
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements MKLINK /J via DeviceIoControl FSCTL_SET_REPARSE_POINT on WinCE (or file copy fallback for /H).
@proc hal_mklink
    push {r4, r5, r6, lr}
    sub sp, sp, #544
    mov r4, r0 ; mode
    mov r5, r1 ; link
    mov r6, r2 ; target

    cmp r4, #2
    bne .try_junc
    ; /H hardlink fallback via CopyFileW on WinCE object store
    mov r0, r6
    mov r1, r5
    mov r2, #1
    call_iat __imp_CopyFileW
    cmp r0, #0
    beq .fail_mk
    mov r0, #0
    add sp, sp, #544
    pop {r4, r5, r6, lr}
    bx lr

.try_junc:
    mov r0, r5
    mov r1, #0
    call_iat __imp_CreateDirectoryW
    ; Open directory for reparse tag
    mov r0, #OPEN_EXISTING
    str r0, [sp, #0]
    mov_imm32 r0, 0x02200000 ; FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT
    str r0, [sp, #4]
    mov r0, #0
    str r0, [sp, #8]
    mov r0, r5
    mov_imm32 r1, GENERIC_WRITE
    mov r2, #3
    mov r3, #0
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .fail_mk
    mov r4, r0

    ; Build MOUNT_POINT_REPARSE_BUFFER at sp+16
    add r0, sp, #16
    mov_imm32 r1, 512
    bl mem_zero_a32
    mov_imm32 r0, 0xA0000003 ; IO_REPARSE_TAG_MOUNT_POINT
    str r0, [sp, #16]
    ; Copy target WCHARs into PathBuffer at sp+32
    add r1, sp, #32
    mov r2, r6
    mov r3, #0
.cp_w:
    ldrh r0, [r2, #0]
    cmp r0, #0
    beq .cp_w_done
    strh r0, [r1, #0]
    add r1, r1, #2
    add r2, r2, #2
    add r3, r3, #2
    cmp r3, #480
    bcc .cp_w
.cp_w_done:
    mov r0, #0
    strh r0, [r1, #0]
    strh r3, [sp, #26] ; SubstituteNameLength
    add r0, r3, #2
    strh r0, [sp, #28] ; PrintNameOffset
    add r0, r3, #12
    strh r0, [sp, #20] ; ReparseDataLength
    add r3, r0, #8     ; total buffer bytes

    mov r0, #0
    str r0, [sp, #12]
    add r0, sp, #12
    str r0, [sp, #8]  ; lpBytesReturned
    mov r0, #0
    str r0, [sp, #4]  ; nOutBufferSize
    str r0, [sp, #0]  ; lpOutBuffer
    ; DeviceIoControl(hDevice, FSCTL_SET_REPARSE_POINT=0x000900A4, lpInBuf, nInSize, NULL, 0, &ret, NULL)
    mov r0, #0
    str r0, [sp, #12]
    add r1, sp, #532
    str r1, [sp, #8]
    str r0, [sp, #12]
    mov r0, r4
    mov_imm32 r1, 0x000900A4
    add r2, sp, #16
    call_iat __imp_DeviceIoControl
    mov r5, r0
    mov r0, r4
    call_iat __imp_CloseHandle
    cmp r5, #0
    beq .fail_mk
    mov r0, #0
    add sp, sp, #544
    pop {r4, r5, r6, lr}
    bx lr

.fail_mk:
    mov r0, #ERR_ACCESS_DENIED
    add sp, sp, #544
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc hal_reg_query_str
;; @abi arm_ce
;; @in r0 = hRoot, r1 = UTF-16LE subkey, r2 = UTF-16LE valname (or 0 for default), r3 = WTF-8 out buf (512B)
;; @out r0 = 0 on success or ERR_FILE_NOT_FOUND
;; @err r0 = ERR_FILE_NOT_FOUND when key/value missing
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries a string value from Windows CE Registry via COREDLL!RegOpenKeyExW + RegQueryValueExW.
@proc hal_reg_query_str
    push {r4, r5, r6, lr}
    sub sp, sp, #544
    mov r4, r2 ; valname
    mov r5, r3 ; out buf

    add r2, sp, #16 ; &hKey
    str r2, [sp, #0]
    mov r2, #0
    mov_imm32 r3, 0x00020019 ; KEY_READ
    call_iat __imp_RegOpenKeyExW
    cmp r0, #0
    bne .not_found

    ldr r6, [sp, #16]
    mov r0, #512
    str r0, [sp, #20] ; cbData
    add r0, sp, #20
    str r0, [sp, #4]  ; lpcbData
    add r0, sp, #32
    str r0, [sp, #0]  ; lpData
    mov r0, r6
    mov r1, r4
    mov r2, #0
    add r3, sp, #24   ; lpType
    call_iat __imp_RegQueryValueExW
    mov r4, r0
    mov r0, r6
    call_iat __imp_RegCloseKey
    cmp r4, #0
    bne .not_found

    add r0, sp, #32
    mov r1, #255
    mov r2, r5
    bl u16_to_wtf8_buf_a32
    mov r0, #0
    add sp, sp, #544
    pop {r4, r5, r6, lr}
    bx lr

.not_found:
    mov r0, #ERR_FILE_NOT_FOUND
    add sp, sp, #544
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc hal_reg_set_str
;; @abi arm_ce
;; @in r0 = hRoot, r1 = UTF-16LE subkey, r2 = UTF-16LE valname (or 0), r3 = UTF-16LE data
;; @out r0 = 0 on success or ERR_ACCESS_DENIED
;; @err r0 = ERR_ACCESS_DENIED on write failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Creates/updates a REG_SZ Registry value via COREDLL!RegCreateKeyExW + RegSetValueExW.
@proc hal_reg_set_str
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #36
    mov r4, r2 ; valname
    mov r5, r3 ; u16 data

    mov r2, #0
    str r2, [sp, #0]  ; dwOptions
    mov_imm32 r3, 0x00020006 ; KEY_WRITE
    str r3, [sp, #4]  ; samDesired
    str r2, [sp, #8]  ; lpSecurityAttributes
    add r3, sp, #24
    str r3, [sp, #12] ; phkResult
    add r3, sp, #28
    str r3, [sp, #16] ; lpdwDisposition
    mov r3, #0        ; lpClass
    call_iat __imp_RegCreateKeyExW
    cmp r0, #0
    bne .denied

    ldr r6, [sp, #24]
    ; Measure WCHAR byte length including NUL
    mov r7, #0
.wlen:
    add r0, r5, r7
    ldrh r1, [r0, #0]
    add r7, r7, #2
    cmp r1, #0
    bne .wlen

    str r7, [sp, #4] ; cbData
    str r5, [sp, #0] ; lpData
    mov r0, r6
    mov r1, r4
    mov r2, #0
    mov r3, #1       ; REG_SZ
    call_iat __imp_RegSetValueExW
    mov r4, r0
    mov r0, r6
    call_iat __imp_RegCloseKey
    cmp r4, #0
    bne .denied
    mov r0, #0
    add sp, sp, #36
    pop {r4, r5, r6, r7, lr}
    bx lr

.denied:
    mov r0, #ERR_ACCESS_DENIED
    add sp, sp, #36
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc hal_spawn_process
;; @abi arm_ce
;; @in r0 = UTF-16LE appPath, r1 = UTF-16LE cmdLine, r2 = UTF-16LE cwd, r3 = waitFlag (1=wait)
;; @out r0 = child exit code, -2 if 16-bit DOS .COM/MZ (handoff to vm8086), or -1 on error
;; @err r0 < 0 on spawn failure or DOS handoff
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Spawns a Windows CE ARMv4 process via COREDLL!CreateProcessW, detecting 16-bit DOS .COM/.EXE for vm8086.
@proc hal_spawn_process
    push {r4, r5, r6, lr}
    sub sp, sp, #64
    mov r4, r0 ; app
    mov r5, r1 ; cmd
    mov r6, r3 ; wait

    ; Zero PROCESS_INFORMATION at sp+32 (16 bytes)
    add r0, sp, #32
    mov r1, #16
    bl mem_zero_a32

    add r0, sp, #32
    str r0, [sp, #20] ; lpProcessInformation
    mov r0, #0
    str r0, [sp, #16] ; lpStartupInfo
    str r0, [sp, #12] ; lpCurrentDirectory (WinCE requires NULL!)
    str r0, [sp, #8]  ; lpEnvironment (WinCE requires NULL!)
    str r0, [sp, #4]  ; dwCreationFlags
    str r0, [sp, #0]  ; bInheritHandles
    mov r0, r4
    mov r1, r5
    mov r2, #0
    mov r3, #0
    call_iat __imp_CreateProcessW
    cmp r0, #0
    beq .chk_dos

    ldr r4, [sp, #32] ; hProcess
    ldr r0, [sp, #36] ; hThread
    call_iat __imp_CloseHandle
    cmp r6, #0
    beq .no_wait
    mov r0, r4
    mvn r1, #0 ; INFINITE
    call_iat __imp_WaitForSingleObject
    mov r0, r4
    add r1, sp, #48
    call_iat __imp_GetExitCodeProcess
    mov r0, r4
    call_iat __imp_CloseHandle
    ldr r0, [sp, #48]
    add sp, sp, #64
    pop {r4, r5, r6, lr}
    bx lr

.no_wait:
    mov r0, r4
    call_iat __imp_CloseHandle
    mov r0, #0
    add sp, sp, #64
    pop {r4, r5, r6, lr}
    bx lr

.chk_dos:
    call_iat __imp_GetLastError
    cmp r0, #193 ; ERROR_BAD_EXE_FORMAT
    beq .vm86_handoff
    mvn r0, #0
    add sp, sp, #64
    pop {r4, r5, r6, lr}
    bx lr
.vm86_handoff:
    mvn r0, #1 ; -2 -> vm8086 handoff
    add sp, sp, #64
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc hal_run_pipe_concurrent
;; @abi arm_ce
;; @in r0 = WTF-8 left command, r1 = WTF-8 right command
;; @out r0 = -1 (instructs runtime to use WinCE \Temp spooled pipeline engine)
;; @err r0 = -1 always on WinCE
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Signals pipeless Windows CE fallback so runtime uses \Temp spooled pipelines.
@proc hal_run_pipe_concurrent
    mvn r0, #0
    bx lr
@endproc

;; @proc hal_check_case_sensitive_dir
;; @abi arm_ce
;; @in r0 = UTF-16LE directory path
;; @out r0 = 0 (Windows CE object store & FAT are case-insensitive)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Returns 0 on Windows CE where directories are always case-insensitive.
@proc hal_check_case_sensitive_dir
    mov r0, #0
    bx lr
@endproc

;; @proc hal_exit_process
;; @abi arm_ce
;; @in r0 = exit code
;; @out Terminates current Windows CE process via COREDLL!TerminateProcess(0x42, code)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Terminates the current WinCE process with the specified exit code.
@proc hal_exit_process
    push {r4, lr}
    mov r1, r0
    mov r0, #0x42 ; SH_CURPROC pseudo-handle on Windows CE
    call_iat __imp_TerminateProcess
    mov r0, r1
    pop {r4, lr}
    bx lr
@endproc
