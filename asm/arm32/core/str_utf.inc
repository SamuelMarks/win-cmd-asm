;; @file str_utf.inc
;; @isa armv4
;; @desc ARM32 VirtualAlloc bump arena, byte-safe unaligned helpers, WTF-8 <-> UTF-16LE transcoders, 64-bit formatters, and ERR_* messages.

@cpu armv4
@format inc

;; @const STR_OFF_BANNER
;; @desc Offset of shell version banner prefix in g_a32_str_pool.
@const STR_OFF_BANNER = 0

;; @const STR_OFF_CRLF
;; @desc Offset of CRLF ("\r\n") in g_a32_str_pool.
@const STR_OFF_CRLF = 36

;; @const STR_OFF_DEF_PROMPT
;; @desc Offset of default "$P$G" prompt string in g_a32_str_pool.
@const STR_OFF_DEF_PROMPT = 39

;; @const STR_OFF_ECHO_ON
;; @desc Offset of "ECHO is on.\r\n" in g_a32_str_pool.
@const STR_OFF_ECHO_ON = 44

;; @const STR_OFF_ECHO_OFF
;; @desc Offset of "ECHO is off.\r\n" in g_a32_str_pool.
@const STR_OFF_ECHO_OFF = 58

;; @const STR_OFF_SYNTAX_ERR
;; @desc Offset of syntax error message in g_a32_str_pool.
@const STR_OFF_SYNTAX_ERR = 73

;; @const STR_OFF_FILE_NOT_FOUND
;; @desc Offset of file-not-found message in g_a32_str_pool.
@const STR_OFF_FILE_NOT_FOUND = 115

;; @const STR_OFF_PATH_NOT_FOUND
;; @desc Offset of path-not-found message in g_a32_str_pool.
@const STR_OFF_PATH_NOT_FOUND = 152

;; @const STR_OFF_ACCESS_DENIED
;; @desc Offset of access-denied message in g_a32_str_pool.
@const STR_OFF_ACCESS_DENIED = 197

;; @const STR_OFF_DIV_ZERO
;; @desc Offset of divide-by-zero error message in g_a32_str_pool.
@const STR_OFF_DIV_ZERO = 214

;; @const STR_OFF_INV_NUM
;; @desc Offset of invalid-number error message in g_a32_str_pool.
@const STR_OFF_INV_NUM = 232

;; @const STR_OFF_BAD_CMD
;; @desc Offset of unrecognized-command error message in g_a32_str_pool.
@const STR_OFF_BAD_CMD = 250

;; @const STR_OFF_PAUSE_MSG
;; @desc Offset of PAUSE prompt message in g_a32_str_pool.
@const STR_OFF_PAUSE_MSG = 306

;; @const STR_OFF_DIR_TAG
;; @desc Offset of "    <DIR>          " tag in g_a32_str_pool.
@const STR_OFF_DIR_TAG = 339

;; @const STR_OFF_JUNC_TAG
;; @desc Offset of "    <JUNCTION>     " tag in g_a32_str_pool.
@const STR_OFF_JUNC_TAG = 359

;; @const STR_OFF_SYMLINKD_TAG
;; @desc Offset of "    <SYMLINKD>     " tag in g_a32_str_pool.
@const STR_OFF_SYMLINKD_TAG = 379

;; @const STR_OFF_SYMLINK_TAG
;; @desc Offset of "    <SYMLINK>      " tag in g_a32_str_pool.
@const STR_OFF_SYMLINK_TAG = 399

;; @const STR_OFF_LINK_CREATED
;; @desc Offset of MKLINK confirmation message in g_a32_str_pool.
@const STR_OFF_LINK_CREATED = 419

;; @const STR_OFF_VERIFY_ON
;; @desc Offset of "VERIFY is on.\r\n" in g_a32_str_pool.
@const STR_OFF_VERIFY_ON = 435

;; @const STR_OFF_VERIFY_OFF
;; @desc Offset of "VERIFY is off.\r\n" in g_a32_str_pool.
@const STR_OFF_VERIFY_OFF = 451

;; @const STR_OFF_BREAK_ON
;; @desc Offset of "BREAK is on\r\n" in g_a32_str_pool.
@const STR_OFF_BREAK_ON = 468

;; @const STR_OFF_BREAK_OFF
;; @desc Offset of "BREAK is off\r\n" in g_a32_str_pool.
@const STR_OFF_BREAK_OFF = 482

;; @const STR_OFF_KEYS_ON
;; @desc Offset of "KEYS is on.\r\n" in g_a32_str_pool.
@const STR_OFF_KEYS_ON = 497

;; @const STR_OFF_KEYS_OFF
;; @desc Offset of "KEYS is off.\r\n" in g_a32_str_pool.
@const STR_OFF_KEYS_OFF = 511

;; @const STR_OFF_VOL_HDR
;; @desc Offset of " Volume in drive C is Windows\r\n" in g_a32_str_pool.
@const STR_OFF_VOL_HDR = 526

;; @const STR_OFF_COPIED_1
;; @desc Offset of "        1 file(s) copied.\r\n" in g_a32_str_pool.
@const STR_OFF_COPIED_1 = 558

;; @const STR_OFF_ENV_UNDEF
;; @desc Offset of "Environment variable not defined\r\n" in g_a32_str_pool.
@const STR_OFF_ENV_UNDEF = 586

;; @proc a32_load_u16_ua
;; @abi arm_ce
;; @in r0 = potentially unaligned byte pointer
;; @out r0 = zero-extended 16-bit little-endian integer
;; @err none
;; @clobbers r0, r1, r2
;; @desc Byte-by-byte little-endian u16 load strictly safe on ARMv4 without alignment faults.
@proc a32_load_u16_ua
    ldrb r1, [r0, #0]
    ldrb r2, [r0, #1]
    lsl r2, r2, #8
    orr r0, r1, r2
    bx lr
@endproc

;; @proc a32_store_u16_ua
;; @abi arm_ce
;; @in r0 = potentially unaligned byte pointer, r1 = 16-bit value
;; @out Writes 2 little-endian bytes to [r0]
;; @err none
;; @clobbers r2
;; @desc Byte-by-byte little-endian u16 store strictly safe on ARMv4 without alignment faults.
@proc a32_store_u16_ua
    strb r1, [r0, #0]
    lsr r2, r1, #8
    strb r2, [r0, #1]
    bx lr
@endproc

;; @proc a32_load_u32_ua
;; @abi arm_ce
;; @in r0 = potentially unaligned byte pointer
;; @out r0 = 32-bit little-endian integer
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Byte-by-byte little-endian u32 load strictly safe on ARMv4 without alignment faults.
@proc a32_load_u32_ua
    ldrb r1, [r0, #0]
    ldrb r2, [r0, #1]
    ldrb r3, [r0, #2]
    ldrb r0, [r0, #3]
    lsl r2, r2, #8
    orr r1, r1, r2
    lsl r3, r3, #16
    orr r1, r1, r3
    lsl r0, r0, #24
    orr r0, r1, r0
    bx lr
@endproc

;; @proc a32_store_u32_ua
;; @abi arm_ce
;; @in r0 = potentially unaligned byte pointer, r1 = 32-bit value
;; @out Writes 4 little-endian bytes to [r0]
;; @err none
;; @clobbers r2
;; @desc Byte-by-byte little-endian u32 store strictly safe on ARMv4 without alignment faults.
@proc a32_store_u32_ua
    strb r1, [r0, #0]
    lsr r2, r1, #8
    strb r2, [r0, #1]
    lsr r2, r1, #16
    strb r2, [r0, #2]
    lsr r2, r1, #24
    strb r2, [r0, #3]
    bx lr
@endproc

;; @proc str_ptr_a32
;; @abi arm_ce
;; @in r0 = STR_OFF_* byte offset
;; @out r0 = NUL-terminated string pointer in g_a32_str_pool
;; @err none
;; @clobbers r0, r1, lr
;; @desc Resolves a constant string pool offset into a runtime pointer.
@proc str_ptr_a32
    push {r4, lr}
    mov r4, r0
    bl hal_get_str_pool
    add r0, r0, r4
    pop {r4, lr}
    bx lr
@endproc

;; @proc str_len_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated byte string
;; @out r0 = byte length (excluding NUL)
;; @err none
;; @clobbers r0, r1, r2
;; @desc Computes the byte length of a NUL-terminated string.
@proc str_len_a32
    mov r1, r0
    cmp r1, #0
    beq .zero
.lp:
    ldrb r2, [r1, #0]
    cmp r2, #0
    beq .done
    add r1, r1, #1
    b .lp
.done:
    sub r0, r1, r0
    bx lr
.zero:
    mov r0, #0
    bx lr
@endproc

;; @proc mem_copy_a32
;; @abi arm_ce
;; @in r0 = dst, r1 = src, r2 = byte count
;; @out r0 = dst + count
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Copies r2 bytes forward from r1 to r0 safely for arbitrary alignments.
@proc mem_copy_a32
.lp:
    cmp r2, #0
    beq .done
    ldrb r3, [r1, #0]
    strb r3, [r0, #0]
    add r1, r1, #1
    add r0, r0, #1
    sub r2, r2, #1
    b .lp
.done:
    bx lr
@endproc

;; @proc mem_zero_a32
;; @abi arm_ce
;; @in r0 = dst, r1 = byte count
;; @out Fills r1 bytes at r0 with 0
;; @err none
;; @clobbers r0, r1, r2
;; @desc Zero-fills r1 bytes starting at r0.
@proc mem_zero_a32
    mov r2, #0
.lp:
    cmp r1, #0
    beq .done
    strb r2, [r0, #0]
    add r0, r0, #1
    sub r1, r1, #1
    b .lp
.done:
    bx lr
@endproc

;; @proc str_copy_a32
;; @abi arm_ce
;; @in r0 = dst, r1 = NUL-terminated src
;; @out r0 = pointer to trailing NUL written in dst
;; @err none
;; @clobbers r0, r1, r2
;; @desc Copies a NUL-terminated string from r1 to r0 and returns pointer to NUL.
@proc str_copy_a32
.lp:
    ldrb r2, [r1, #0]
    strb r2, [r0, #0]
    cmp r2, #0
    beq .done
    add r1, r1, #1
    add r0, r0, #1
    b .lp
.done:
    bx lr
@endproc

;; @proc ascii_upper_a32
;; @abi arm_ce
;; @in r0 = byte character
;; @out r0 = uppercase ASCII character
;; @err none
;; @clobbers r0
;; @desc Converts ASCII 'a'..'z' to 'A'..'Z'.
@proc ascii_upper_a32
    cmp r0, #0x61
    bcc .done
    cmp r0, #0x7A
    bhi .done
    sub r0, r0, #0x20
.done:
    bx lr
@endproc

;; @proc str_cmp_a32
;; @abi arm_ce
;; @in r0 = s1, r1 = s2, r2 = ignore_case (1 = case-insensitive, 0 = exact)
;; @out r0 = signed difference (<0, 0, >0)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Compares two NUL-terminated strings either case-sensitively or case-insensitively.
@proc str_cmp_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
.lp:
    ldrb r0, [r4, #0]
    cmp r6, #0
    beq .no_u1
    bl ascii_upper_a32
.no_u1:
    mov r3, r0
    ldrb r0, [r5, #0]
    cmp r6, #0
    beq .no_u2
    push {r3, r6}
    bl ascii_upper_a32
    pop {r3, r6}
.no_u2:
    subs r2, r3, r0
    bne .diff
    cmp r3, #0
    beq .eq
    add r4, r4, #1
    add r5, r5, #1
    b .lp
.diff:
    mov r0, r2
    pop {r4, r5, r6, lr}
    bx lr
.eq:
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc str_ncmp_ci_a32
;; @abi arm_ce
;; @in r0 = s1, r1 = s2, r2 = byte count
;; @out r0 = 0 if equal up to r2 bytes (case-insensitive), non-zero otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Compares up to r2 bytes of s1 and s2 case-insensitively.
@proc str_ncmp_ci_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, r1
    mov r6, r2
.lp:
    cmp r6, #0
    beq .eq
    ldrb r0, [r4, #0]
    bl ascii_upper_a32
    mov r3, r0
    ldrb r0, [r5, #0]
    push {r3, r6}
    bl ascii_upper_a32
    pop {r3, r6}
    subs r2, r3, r0
    bne .diff
    cmp r3, #0
    beq .eq
    add r4, r4, #1
    add r5, r5, #1
    sub r6, r6, #1
    b .lp
.diff:
    mov r0, r2
    pop {r4, r5, r6, lr}
    bx lr
.eq:
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc arena_init_a32
;; @abi arm_ce
;; @in r0 = ARM32_CTX*
;; @out r0 = 0 on success or ERR_OUT_OF_MEMORY
;; @err r0 = ERR_OUT_OF_MEMORY when VirtualAlloc fails
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reserves VirtualAlloc statement arena (4 MiB std, 256 KiB WinCE fallback), 64 KiB env block, and 256 KiB persistent pool.
@proc arena_init_a32
    push {r4, r5, r6, lr}
    mov r4, r0

    mov r0, #0
    mov_imm32 r1, A32_ARENA_STD
    mov_imm32 r2, MEM_RESERVE
    mov r3, #PAGE_READWRITE
    mov r5, r1
    bl hal_virtual_alloc
    cmp r0, #0
    bne .got_res

    mov r0, #0
    mov_imm32 r1, A32_ARENA_LOW
    mov_imm32 r2, MEM_RESERVE
    mov r3, #PAGE_READWRITE
    mov r5, r1
    bl hal_virtual_alloc
    cmp r0, #0
    beq .oom

.got_res:
    str r0, [r4, #ARM32_CTX.pArenaBase]
    str r0, [r4, #ARM32_CTX.pArenaPtr]
    add r1, r0, r5
    str r1, [r4, #ARM32_CTX.pArenaLimit]

    mov_imm32 r1, A32_ARENA_GRAN
    mov_imm32 r2, MEM_COMMIT
    mov r3, #PAGE_READWRITE
    bl hal_virtual_alloc
    cmp r0, #0
    beq .oom

    ldr r0, [r4, #ARM32_CTX.pArenaBase]
    mov_imm32 r1, A32_ARENA_GRAN
    add r0, r0, r1
    str r0, [r4, #ARM32_CTX.pArenaCommit]

    ; Commit 64 KiB active environment block + 256 KiB persistent pool (batch/setlocal/macro/pushd stacks)
    mov r0, #0
    mov_imm32 r1, 0x00050000
    mov_imm32 r2, 0x00003000
    mov r3, #PAGE_READWRITE
    bl hal_virtual_alloc
    cmp r0, #0
    beq .oom

    str r0, [r4, #ARM32_CTX.pEnvBlock]
    mov_imm32 r1, A32_ENV_CAP
    str r1, [r4, #ARM32_CTX.dwEnvCap]
    mov r2, #0
    strb r2, [r0, #0]
    strb r2, [r0, #1]
    mov r2, #2
    str r2, [r4, #ARM32_CTX.dwEnvLen]

    add r0, r0, r1
    str r0, [r4, #ARM32_CTX.pPermPoolPtr]
    mov_imm32 r1, 0x00040000
    add r1, r0, r1
    str r1, [r4, #ARM32_CTX.pPermPoolEnd]

    ; Carve fixed runtime stacks from persistent pool
    mov r0, #2048
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pBatchStack]
    mov r0, #512
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pSetlocalStack]
    mov r0, #128
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pPushdStack]
    mov r0, #128
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pForStack]
    mov r0, #128
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pMacroNames]
    mov r0, #128
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pMacroBodies]
    mov r0, #128
    bl perm_alloc_a32
    str r0, [r4, #ARM32_CTX.pHistRing]

    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr

.oom:
    mov r0, #ERR_OUT_OF_MEMORY
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc perm_alloc_a32
;; @abi arm_ce
;; @in r0 = byte count
;; @out r0 = 8-byte-aligned persistent pointer (or 0 on OOM)
;; @err r0 = 0 when persistent pool exhausted
;; @clobbers r0, r1, r2, r3, lr
;; @desc Allocates permanent storage (for SETLOCAL snapshots, PUSHD entries, macros, history).
@proc perm_alloc_a32
    push {r4, lr}
    add r4, r0, #7
    bic r4, r4, #7
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.pPermPoolPtr]
    ldr r2, [r0, #ARM32_CTX.pPermPoolEnd]
    add r3, r1, r4
    cmp r3, r2
    bhi .oom
    str r3, [r0, #ARM32_CTX.pPermPoolPtr]
    mov r0, r1
    pop {r4, lr}
    bx lr
.oom:
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc arena_reset_a32
;; @abi arm_ce
;; @in none
;; @out Resets statement bump arena pointer in O(1) without syscalls
;; @err none
;; @clobbers r0, r1, lr
;; @desc Resets pArenaPtr back to pArenaBase between top-level statements.
@proc arena_reset_a32
    push {r4, lr}
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.pArenaBase]
    str r1, [r0, #ARM32_CTX.pArenaPtr]
    pop {r4, lr}
    bx lr
@endproc

;; @proc arena_alloc_a32
;; @abi arm_ce
;; @in r0 = requested byte count
;; @out r0 = 8-byte-aligned zeroed/ready pointer (or 0 on OOM)
;; @err r0 = 0 when reservation exhausted
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Allocates 8-byte-aligned memory from the bump arena, committing 64 KiB pages on demand.
@proc arena_alloc_a32
    push {r4, r5, r6, lr}
    sub sp, sp, #8
    cmp r0, #0
    bne .non_zero
    mov r0, #8
.non_zero:
    add r4, r0, #7
    bic r4, r4, #7
    bl hal_get_ctx
    mov r5, r0
    ldr r6, [r5, #ARM32_CTX.pArenaPtr]
    add r1, r6, r4
    ldr r2, [r5, #ARM32_CTX.pArenaLimit]
    cmp r1, r2
    bhi .oom
    ldr r3, [r5, #ARM32_CTX.pArenaCommit]
    cmp r1, r3
    bls .fast_ok

    str r1, [sp, #0]
    sub r2, r1, r3
    mov_imm32 r0, 0x0000FFFF
    add r2, r2, r0
    bic r2, r2, #0xFF
    mov_imm32 r0, 0x0000FF00
    bic r2, r2, r0
    mov r0, r3
    mov r1, r2
    mov r4, r2
    mov_imm32 r2, MEM_COMMIT
    mov r3, #PAGE_READWRITE
    bl hal_virtual_alloc
    cmp r0, #0
    beq .oom

    ldr r3, [r5, #ARM32_CTX.pArenaCommit]
    add r3, r3, r4
    str r3, [r5, #ARM32_CTX.pArenaCommit]
    ldr r1, [sp, #0]

.fast_ok:
    str r1, [r5, #ARM32_CTX.pArenaPtr]
    mov r0, r6
    add sp, sp, #8
    pop {r4, r5, r6, lr}
    bx lr

.oom:
    mov r0, #0
    add sp, sp, #8
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc arena_strdup_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated string
;; @out r0 = duplicated NUL-terminated string in bump arena (or 0 on OOM)
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Duplicates a NUL-terminated string into the bump arena.
@proc arena_strdup_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    bl str_len_a32
    add r5, r0, #1
    mov r0, r5
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r3, r0
    mov r1, r4
    mov r2, r5
    push {r3, r4}
    bl mem_copy_a32
    pop {r3, r4}
    mov r0, r3
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc wtf8_to_u16_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated WTF-8/UTF-8 string
;; @out r0 = NUL-terminated UTF-16LE buffer in arena (2-byte aligned), r1 = WCHAR count
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Transcodes WTF-8 (including unpaired NTFS surrogates) to UTF-16LE in the statement arena.
@proc wtf8_to_u16_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0
    bl str_len_a32
    add r0, r0, #2
    lsl r0, r0, #1
    bl arena_alloc_a32
    cmp r0, #0
    beq .oom
    mov r5, r0 ; out base
    mov r6, r0 ; out cursor

.lp:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .done
    cmp r0, #0x80
    bcc .ascii

    ; Check 2-byte, 3-byte, or 4-byte UTF-8 / WTF-8
    and r1, r0, #0xE0
    cmp r1, #0xC0
    beq .two_b
    and r1, r0, #0xF0
    cmp r1, #0xE0
    beq .three_b
    and r1, r0, #0xF8
    cmp r1, #0xF0
    beq .four_b

    ; Raw high byte fallback -> Latin-1 / CP1252 direct widen
.ascii:
    strh r0, [r6, #0]
    add r6, r6, #2
    add r4, r4, #1
    b .lp

.two_b:
    ldrb r1, [r4, #1]
    and r2, r1, #0xC0
    cmp r2, #0x80
    bne .ascii
    and r0, r0, #0x1F
    and r1, r1, #0x3F
    lsl r0, r0, #6
    orr r0, r0, r1
    strh r0, [r6, #0]
    add r6, r6, #2
    add r4, r4, #2
    b .lp

.three_b:
    ldrb r1, [r4, #1]
    ldrb r2, [r4, #2]
    and r3, r1, #0xC0
    cmp r3, #0x80
    bne .ascii
    and r3, r2, #0xC0
    cmp r3, #0x80
    bne .ascii
    and r0, r0, #0x0F
    and r1, r1, #0x3F
    and r2, r2, #0x3F
    lsl r0, r0, #12
    lsl r1, r1, #6
    orr r0, r0, r1
    orr r0, r0, r2
    strh r0, [r6, #0]
    add r6, r6, #2
    add r4, r4, #3
    b .lp

.four_b:
    ldrb r1, [r4, #1]
    ldrb r2, [r4, #2]
    ldrb r3, [r4, #3]
    and r7, r0, #0x07
    and r1, r1, #0x3F
    and r2, r2, #0x3F
    and r3, r3, #0x3F
    lsl r7, r7, #18
    lsl r1, r1, #12
    lsl r2, r2, #6
    orr r7, r7, r1
    orr r7, r7, r2
    orr r7, r7, r3
    mov_imm32 r8, 0x00010000
    sub r7, r7, r8
    lsr r0, r7, #10
    mov_imm32 r8, 0x0000D800
    add r0, r0, r8
    strh r0, [r6, #0]
    mov_imm32 r8, 0x000003FF
    and r0, r7, r8
    mov_imm32 r8, 0x0000DC00
    add r0, r0, r8
    strh r0, [r6, #2]
    add r6, r6, #4
    add r4, r4, #4
    b .lp

.done:
    mov r0, #0
    strh r0, [r6, #0]
    sub r1, r6, r5
    lsr r1, r1, #1
    mov r0, r5
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.oom:
    mov r0, #0
    mov r1, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc u16_to_wtf8_buf_a32
;; @abi arm_ce
;; @in r0 = UTF-16LE src (potentially unaligned), r1 = max WCHARs (-1 for NUL-terminated), r2 = dst byte buf
;; @out r0 = bytes written (excluding trailing NUL)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Transcodes UTF-16LE (combining surrogate pairs, preserving lone surrogates as WTF-8) into r2.
@proc u16_to_wtf8_buf_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0 ; src
    mov r5, r1 ; rem wchars
    mov r6, r2 ; dst base
    mov r7, r2 ; dst cursor

.lp:
    cmp r5, #0
    beq .done
    mov r0, r4
    bl a32_load_u16_ua
    cmp r5, #0
    bmi .chk_nul
    b .got_wc
.chk_nul:
    cmp r0, #0
    beq .done
.got_wc:
    cmp r0, #0
    beq .done
    add r4, r4, #2
    cmp r5, #0
    bmi .after_dec
    sub r5, r5, #1
.after_dec:
    cmp r0, #0x80
    bcc .one_b
    mov_imm32 r8, 0x00000800
    cmp r0, r8
    bcc .two_b

    ; Check high surrogate 0xD800..0xDBFF
    mov_imm32 r8, 0x0000FC00
    and r1, r0, r8
    mov_imm32 r8, 0x0000D800
    cmp r1, r8
    bne .three_b
    cmp r5, #0
    beq .three_b
    mov r8, r0
    mov r0, r4
    bl a32_load_u16_ua
    mov r2, r0
    mov r0, r8
    mov_imm32 r3, 0x0000FC00
    and r1, r2, r3
    mov_imm32 r3, 0x0000DC00
    cmp r1, r3
    bne .three_b

    ; Combine surrogate pair into 4-byte UTF-8
    add r4, r4, #2
    cmp r5, #0
    bmi .comb_pair
    sub r5, r5, #1
.comb_pair:
    mov_imm32 r3, 0x000003FF
    and r0, r0, r3
    and r2, r2, r3
    lsl r0, r0, #10
    orr r0, r0, r2
    mov_imm32 r3, 0x00010000
    add r0, r0, r3
    lsr r1, r0, #18
    and r1, r1, #0x07
    orr r1, r1, #0xF0
    strb r1, [r7, #0]
    lsr r1, r0, #12
    and r1, r1, #0x3F
    orr r1, r1, #0x80
    strb r1, [r7, #1]
    lsr r1, r0, #6
    and r1, r1, #0x3F
    orr r1, r1, #0x80
    strb r1, [r7, #2]
    and r1, r0, #0x3F
    orr r1, r1, #0x80
    strb r1, [r7, #3]
    add r7, r7, #4
    b .lp

.one_b:
    strb r0, [r7, #0]
    add r7, r7, #1
    b .lp

.two_b:
    lsr r1, r0, #6
    orr r1, r1, #0xC0
    strb r1, [r7, #0]
    and r1, r0, #0x3F
    orr r1, r1, #0x80
    strb r1, [r7, #1]
    add r7, r7, #2
    b .lp

.three_b:
    lsr r1, r0, #12
    orr r1, r1, #0xE0
    strb r1, [r7, #0]
    lsr r1, r0, #6
    and r1, r1, #0x3F
    orr r1, r1, #0x80
    strb r1, [r7, #1]
    and r1, r0, #0x3F
    orr r1, r1, #0x80
    strb r1, [r7, #2]
    add r7, r7, #3
    b .lp

.done:
    mov r0, #0
    strb r0, [r7, #0]
    sub r0, r7, r6
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc u16_to_wtf8_arena_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated UTF-16LE string
;; @out r0 = NUL-terminated WTF-8 string in bump arena
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Measures a UTF-16LE string and transcodes it into a newly allocated WTF-8 arena buffer.
@proc u16_to_wtf8_arena_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, #0
.cnt:
    mov r0, r4
    add r0, r0, r5
    bl a32_load_u16_ua
    cmp r0, #0
    beq .alloc
    add r5, r5, #2
    b .cnt
.alloc:
    lsr r0, r5, #1
    mov r1, #3
    mul r2, r0, r1
    add r0, r2, #4
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r6, r0
    mov r0, r4
    mvn r1, #0
    mov r2, r6
    bl u16_to_wtf8_buf_a32
    mov r0, r6
.done:
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc fmt_i32_a32
;; @abi arm_ce
;; @in r0 = signed 32-bit integer, r1 = output buffer (>= 16 bytes)
;; @out r0 = byte length written (NUL-terminated)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Formats signed 32-bit integer r0 into decimal ASCII in r1.
@proc fmt_i32_a32
    push {r4, r5, r6, lr}
    sub sp, sp, #16
    mov r4, r1 ; out base
    mov r5, r1 ; out cursor
    cmp r0, #0
    bge .pos
    mov r2, #0x2D
    strb r2, [r5, #0]
    add r5, r5, #1
    mov r2, #0
    sub r0, r2, r0
.pos:
    mov r6, #0 ; digit count
.dig_lp:
    mov r1, #10
    bl math_udiv32
    add r1, r1, #0x30
    strb r1, [sp, r6]
    add r6, r6, #1
    cmp r0, #0
    bne .dig_lp
.rev:
    sub r6, r6, #1
    ldrb r2, [sp, r6]
    strb r2, [r5, #0]
    add r5, r5, #1
    cmp r6, #0
    bne .rev
    mov r2, #0
    strb r2, [r5, #0]
    sub r0, r5, r4
    add sp, sp, #16
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc fmt_u64_commas_a32
;; @abi arm_ce
;; @in r0 = u64_lo, r1 = u64_hi, r2 = output buffer (>= 32 bytes), r3 = use_commas (1 = yes, 0 = plain)
;; @out r0 = byte length written (NUL-terminated)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Formats unsigned 64-bit integer (r1:r0) with optional thousand-grouping commas.
@proc fmt_u64_commas_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #36
    mov r4, r0 ; lo
    mov r5, r1 ; hi
    mov r6, r2 ; out
    mov r7, r3 ; commas
    mov r8, #0 ; len
    mov r9, #0 ; group3

.lp64:
    cmp r5, #0
    bne .div_hi
    mov r0, r4
    mov r1, #10
    bl math_udiv32
    mov r4, r0
    mov r2, r1
    b .emit_dig

.div_hi:
    ; Two-limb 64-bit / 10 via 16-bit chunks
    mov r0, r5
    mov r1, #10
    bl math_udiv32
    mov r5, r0
    ; Combine remainder r1 (0..9) with top 16 bits of r4
    lsr r0, r4, #16
    lsl r1, r1, #16
    orr r0, r0, r1
    mov r1, #10
    bl math_udiv32
    lsl r3, r0, #16
    str r3, [sp, #32]
    mov_imm32 r0, 0x0000FFFF
    and r0, r4, r0
    lsl r1, r1, #16
    orr r0, r0, r1
    mov r1, #10
    bl math_udiv32
    ldr r3, [sp, #32]
    orr r4, r3, r0
    mov r2, r1

.emit_dig:
    cmp r7, #0
    beq .no_comma
    cmp r9, #3
    bne .no_comma
    mov r3, #0x2C
    strb r3, [sp, r8]
    add r8, r8, #1
    mov r9, #0
.no_comma:
    add r2, r2, #0x30
    strb r2, [sp, r8]
    add r8, r8, #1
    add r9, r9, #1
    orrs r0, r4, r5
    bne .lp64

    mov r1, r6
.rev_lp:
    sub r8, r8, #1
    ldrb r2, [sp, r8]
    strb r2, [r1, #0]
    add r1, r1, #1
    cmp r8, #0
    bne .rev_lp
    mov r2, #0
    strb r2, [r1, #0]
    sub r0, r1, r6
    add sp, sp, #36
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc out_str_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated WTF-8 string
;; @out Writes string to active FD 1 (stdout)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes a NUL-terminated string to FD 1 (stdout).
@proc out_str_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    bl str_len_a32
    mov r5, r0
    cmp r5, #0
    beq .done
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.fdTable]
    ; Wait: fdTable[1] is at ARM32_CTX.fdTable + 4!
    bl hal_get_ctx
    add r0, r0, #ARM32_CTX.fdTable
    ldr r0, [r0, #4]
    mov r1, r4
    mov r2, r5
    bl hal_write_handle
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc err_str_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated WTF-8 string
;; @out Writes string to active FD 2 (stderr)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes a NUL-terminated string to FD 2 (stderr).
@proc err_str_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    bl str_len_a32
    mov r5, r0
    cmp r5, #0
    beq .done
    bl hal_get_ctx
    add r0, r0, #ARM32_CTX.fdTable
    ldr r0, [r0, #8]
    mov r1, r4
    mov r2, r5
    bl hal_write_handle
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc out_crlf_a32
;; @abi arm_ce
;; @in none
;; @out Writes "\r\n" to FD 1 (stdout)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Emits a CRLF newline to stdout.
@proc out_crlf_a32
    push {r4, lr}
    mov r0, #STR_OFF_CRLF
    bl str_ptr_a32
    bl out_str_a32
    pop {r4, lr}
    bx lr
@endproc

;; @proc print_err_code_a32
;; @abi arm_ce
;; @in r0 = ERR_* code
;; @out Prints era-accurate error message to FD 2 (stderr)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Maps canonical ERR_* enum code to message and writes it to stderr.
@proc print_err_code_a32
    push {r4, lr}
    cmp r0, #ERR_FILE_NOT_FOUND
    beq .fnf
    cmp r0, #ERR_PATH_NOT_FOUND
    beq .pnf
    cmp r0, #ERR_ACCESS_DENIED
    beq .acc
    mov_imm32 r1, ERR_DIV_BY_ZERO
    cmp r0, r1
    beq .divz
    mov_imm32 r1, ERR_INVALID_NUMBER
    cmp r0, r1
    beq .invn
    mov_imm32 r1, ERR_BAD_COMMAND
    cmp r0, r1
    beq .badc
    mov r0, #STR_OFF_SYNTAX_ERR
    b .emit
.fnf:
    mov r0, #STR_OFF_FILE_NOT_FOUND
    b .emit
.pnf:
    mov r0, #STR_OFF_PATH_NOT_FOUND
    b .emit
.acc:
    mov r0, #STR_OFF_ACCESS_DENIED
    b .emit
.divz:
    mov r0, #STR_OFF_DIV_ZERO
    b .emit
.invn:
    mov r0, #STR_OFF_INV_NUM
    b .emit
.badc:
    mov_imm32 r0, STR_OFF_BAD_CMD
.emit:
    bl str_ptr_a32
    bl err_str_a32
    pop {r4, lr}
    bx lr
@endproc
