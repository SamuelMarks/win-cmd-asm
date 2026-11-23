;; @file str_utf_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 strings, WTF-8 <-> UTF-16LE transcoders, \?\ 32K promoter, trap-safe 32/64-bit math, and VirtualAlloc bump arena.

@cpu arm64
@format inc

;; @const STR_OFF_BANNER
;; @desc Offset of banner prefix in g_a64_str_pool.
@const STR_OFF_BANNER = 0

;; @const STR_OFF_CRLF
;; @desc Offset of CRLF sequence in g_a64_str_pool.
@const STR_OFF_CRLF = 36

;; @const STR_OFF_DEF_PROMPT
;; @desc Offset of default $P$G prompt in g_a64_str_pool.
@const STR_OFF_DEF_PROMPT = 39

;; @const STR_OFF_ECHO_ON
;; @desc Offset of ECHO is on message in g_a64_str_pool.
@const STR_OFF_ECHO_ON = 44

;; @const STR_OFF_ECHO_OFF
;; @desc Offset of ECHO is off message in g_a64_str_pool.
@const STR_OFF_ECHO_OFF = 58

;; @const STR_OFF_SYNTAX_ERR
;; @desc Offset of syntax error message in g_a64_str_pool.
@const STR_OFF_SYNTAX_ERR = 73

;; @const STR_OFF_FILE_NOT_FOUND
;; @desc Offset of file-not-found message in g_a64_str_pool.
@const STR_OFF_FILE_NOT_FOUND = 115

;; @const STR_OFF_PATH_NOT_FOUND
;; @desc Offset of path-not-found message in g_a64_str_pool.
@const STR_OFF_PATH_NOT_FOUND = 152

;; @const STR_OFF_ACCESS_DENIED
;; @desc Offset of access-denied message in g_a64_str_pool.
@const STR_OFF_ACCESS_DENIED = 197

;; @const STR_OFF_DIV_ZERO
;; @desc Offset of divide-by-zero message in g_a64_str_pool.
@const STR_OFF_DIV_ZERO = 214

;; @const STR_OFF_INV_NUM
;; @desc Offset of invalid-number message in g_a64_str_pool.
@const STR_OFF_INV_NUM = 232

;; @const STR_OFF_BAD_CMD
;; @desc Offset of unrecognized-command message in g_a64_str_pool.
@const STR_OFF_BAD_CMD = 250

;; @const STR_OFF_PAUSE_MSG
;; @desc Offset of PAUSE prompt message in g_a64_str_pool.
@const STR_OFF_PAUSE_MSG = 306

;; @const STR_OFF_DIR_TAG
;; @desc Offset of <DIR> tag in g_a64_str_pool.
@const STR_OFF_DIR_TAG = 339

;; @const STR_OFF_JUNC_TAG
;; @desc Offset of <JUNCTION> tag in g_a64_str_pool.
@const STR_OFF_JUNC_TAG = 359

;; @const STR_OFF_SYMLINKD_TAG
;; @desc Offset of <SYMLINKD> tag in g_a64_str_pool.
@const STR_OFF_SYMLINKD_TAG = 379

;; @const STR_OFF_SYMLINK_TAG
;; @desc Offset of <SYMLINK> tag in g_a64_str_pool.
@const STR_OFF_SYMLINK_TAG = 399

;; @const STR_OFF_LINK_CREATED
;; @desc Offset of Link created message in g_a64_str_pool.
@const STR_OFF_LINK_CREATED = 419

;; @const STR_OFF_VOL_HDR
;; @desc Offset of Volume header in g_a64_str_pool.
@const STR_OFF_VOL_HDR = 526

;; @const STR_OFF_COPIED_1
;; @desc Offset of 1 file(s) copied message in g_a64_str_pool.
@const STR_OFF_COPIED_1 = 558

;; @const STR_OFF_ENV_UNDEF
;; @desc Offset of Environment variable not defined message in g_a64_str_pool.
@const STR_OFF_ENV_UNDEF = 586

;; @proc str_ptr_a64
;; @abi win_arm64
;; @in w0 = byte offset into g_a64_str_pool
;; @out x0 = 64-bit pointer into g_a64_str_pool
;; @err none
;; @clobbers x0, x1
;; @desc Resolves a constant message string pointer from g_a64_str_pool via ADRP+ADD.
@proc str_ptr_a64
    uxtw x1, w0
    adr_sym x0, g_a64_str_pool
    add x0, x0, x1
    ret
@endproc

;; @proc ascii_upper_a64
;; @abi win_arm64
;; @in w0 = byte character
;; @out w0 = ASCII uppercased character
;; @err none
;; @clobbers x0, x1
;; @desc Folds 'a'..'z' to 'A'..'Z' in registers without touching X18.
@proc ascii_upper_a64
    and w0, w0, #0xFF
    sub w1, w0, #0x61
    cmp w1, #26
    b.hs .done
    sub w0, w0, #0x20
.done:
    ret
@endproc

;; @proc str_len_a64
;; @abi win_arm64
;; @in x0 = NUL-terminated string pointer (or 0)
;; @out x0 = byte length
;; @err none
;; @clobbers x0, x1, x2
;; @desc Computes NUL-terminated byte length using CBZ guard.
@proc str_len_a64
    cbz x0, .zero
    mov x1, x0
.lp:
    ldrb w2, [x1, #0]
    cbz w2, .done
    add x1, x1, #1
    b .lp
.done:
    sub x0, x1, x0
    ret
.zero:
    mov x0, #0
    ret
@endproc

;; @proc mem_copy_a64
;; @abi win_arm64
;; @in x0 = dst, x1 = src, x2 = byte count
;; @out x0 = dst + count
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Copies x2 bytes forward from x1 to x0, returning x0 + x2.
@proc mem_copy_a64
.q_lp:
    cmp x2, #8
    b.lo .b_lp
    ldr x3, [x1], #8
    str x3, [x0], #8
    sub x2, x2, #8
    b .q_lp
.b_lp:
    cbz x2, .done
    ldrb w3, [x1], #1
    strb w3, [x0], #1
    sub x2, x2, #1
    b .b_lp
.done:
    ret
@endproc

;; @proc mem_zero_a64
;; @abi win_arm64
;; @in x0 = dst, x1 = byte count
;; @out Fills x1 bytes at x0 with 0
;; @err none
;; @clobbers x0, x1
;; @desc Zero-fills x1 bytes starting at x0 using 16-byte STP XZR, XZR stores.
@proc mem_zero_a64
.p_lp:
    cmp x1, #16
    b.lo .z_tail
    stp xzr, xzr, [x0], #16
    sub x1, x1, #16
    b .p_lp
.z_tail:
    cbz x1, .done
    strb wzr, [x0], #1
    sub x1, x1, #1
    b .z_tail
.done:
    ret
@endproc

;; @proc str_copy_a64
;; @abi win_arm64
;; @in x0 = dst, x1 = src
;; @out x0 = pointer to NUL byte written at end of dst
;; @err none
;; @clobbers x0, x1, x2
;; @desc Copies NUL-terminated string x1 into x0 and returns pointer to trailing NUL.
@proc str_copy_a64
    cbz x1, .nul_only
.lp:
    ldrb w2, [x1, #0]
    strb w2, [x0, #0]
    cbz w2, .done
    add x0, x0, #1
    add x1, x1, #1
    b .lp
.nul_only:
    strb wzr, [x0, #0]
.done:
    ret
@endproc

;; @proc str_cmp_a64
;; @abi win_arm64
;; @in x0 = s1, x1 = s2, w2 = icase (1 = case-insensitive, 0 = exact)
;; @out w0 = signed <0, 0, >0 ordering
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Compares two NUL-terminated strings either case-sensitively or case-insensitively.
@proc str_cmp_a64
    mov x4, x0
    mov x5, x1
.lp:
    ldrb w0, [x4, #0]
    ldrb w3, [x5, #0]
    cbz w2, .cmp_b
    sub w1, w0, #0x61
    cmp w1, #26
    b.hs .u2
    sub w0, w0, #0x20
.u2:
    sub w1, w3, #0x61
    cmp w1, #26
    b.hs .cmp_b
    sub w3, w3, #0x20
.cmp_b:
    subs w1, w0, w3
    b.ne .diff
    cbz w0, .eq
    add x4, x4, #1
    add x5, x5, #1
    b .lp
.eq:
    mov w0, #0
    ret
.diff:
    mov w0, w1
    ret
@endproc

;; @proc skip_ws_a64
;; @abi win_arm64
;; @in x0 = string cursor
;; @out x0 = cursor past leading spaces and tabs
;; @err none
;; @clobbers x0, x1
;; @desc Advances x0 past ASCII spaces (0x20) and tabs (0x09).
@proc skip_ws_a64
.lp:
    ldrb w1, [x0, #0]
    cmp w1, #0x20
    b.eq .adv
    cmp w1, #0x09
    b.ne .done
.adv:
    add x0, x0, #1
    b .lp
.done:
    ret
@endproc

;; @proc strip_outer_quotes_a64
;; @abi win_arm64
;; @in x0 = mutable string buffer
;; @out x0 = unquoted string if enclosed in matching `"..."`
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Strips a single pair of outer double-quotes in place when length >= 2.
@proc strip_outer_quotes_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    cbz x0, .done
    ldrb w1, [x0, #0]
    cmp w1, #0x22
    b.ne .done
    mov x3, x0
    bl str_len_a64
    cmp x0, #2
    b.lo .ret_orig
    sub x2, x0, #1
    ldrb w1, [x3, x2]
    cmp w1, #0x22
    b.ne .ret_orig
    strb wzr, [x3, x2]
    add x0, x3, #1
    b .done
.ret_orig:
    mov x0, x3
.done:
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc math_sdiv32_a64
;; @abi win_arm64
;; @in w0 = signed 32-bit dividend, w1 = signed 32-bit divisor
;; @out w0 = quotient, w1 = remainder, w2 = 0 (or ERR_DIV_BY_ZERO when w1 == 0)
;; @err w2 = ERR_DIV_BY_ZERO on / 0
;; @clobbers x0, x1, x2, x3
;; @desc Trap-safe wrapping 32-bit SET /A division on AArch64 guarding / 0 and INT32_MIN / -1.
@proc math_sdiv32_a64
    cbz w1, .div_zero
    cmn w1, #1
    b.ne .do_div
    mov w3, #-2147483648
    cmp w0, w3
    b.ne .do_div
    mov w1, #0
    mov w2, #0
    ret
.do_div:
    sdiv w2, w0, w1
    msub w1, w2, w1, w0
    mov w0, w2
    mov w2, #0
    ret
.div_zero:
    mov w0, #0
    mov w1, #0
    mov_imm32 w2, ERR_DIV_BY_ZERO
    ret
@endproc

;; @proc fmt_i32_a64
;; @abi win_arm64
;; @in w0 = signed 32-bit value, x1 = output buffer (>= 16 bytes)
;; @out x0 = pointer to NUL terminator in x1
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6
;; @desc Formats a signed 32-bit integer in decimal ASCII.
@proc fmt_i32_a64
    mov x4, x1
    sxtw x2, w0
    cmp x2, #0
    b.ge .pos
    mov w3, #0x2D
    strb w3, [x4, #0]
    add x4, x4, #1
    neg x2, x2
.pos:
    cbnz x2, .conv
    mov w3, #0x30
    strb w3, [x4, #0]
    strb wzr, [x4, #1]
    add x0, x4, #1
    ret
.conv:
    mov x5, x4
    mov x6, #10
.d_lp:
    cbz x2, .rev
    udiv x3, x2, x6
    msub x0, x3, x6, x2
    add w0, w0, #0x30
    strb w0, [x4, #0]
    add x4, x4, #1
    mov x2, x3
    b .d_lp
.rev:
    strb wzr, [x4, #0]
    mov x0, x4
    sub x4, x4, #1
.r_lp:
    cmp x5, x4
    b.hs .done
    ldrb w1, [x5, #0]
    ldrb w2, [x4, #0]
    strb w2, [x5, #0]
    strb w1, [x4, #0]
    add x5, x5, #1
    sub x4, x4, #1
    b .r_lp
.done:
    ret
@endproc

;; @proc fmt_u64_commas_a64
;; @abi win_arm64
;; @in x0 = unsigned 64-bit integer, x1 = output buffer (>= 32 bytes)
;; @out x0 = byte length of formatted string
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Formats a 64-bit unsigned integer with thousands separators (e.g. `10,737,418,240`).
@proc fmt_u64_commas_a64
    mov x4, x1
    cbnz x0, .nz
    mov w2, #0x30
    strb w2, [x4, #0]
    strb wzr, [x4, #1]
    mov x0, #1
    ret
.nz:
    mov x2, x0
    mov x5, x4
    mov x6, #10
    mov w7, #0 ; digit group counter
.lp:
    cbz x2, .rev
    cmp w7, #3
    b.ne .dig
    mov w3, #0x2C
    strb w3, [x4, #0]
    add x4, x4, #1
    mov w7, #0
.dig:
    udiv x3, x2, x6
    msub x0, x3, x6, x2
    add w0, w0, #0x30
    strb w0, [x4, #0]
    add x4, x4, #1
    add w7, w7, #1
    mov x2, x3
    b .lp
.rev:
    strb wzr, [x4, #0]
    sub x0, x4, x1 ; length
    sub x4, x4, #1
.r_lp:
    cmp x5, x4
    b.hs .done
    ldrb w2, [x5, #0]
    ldrb w3, [x4, #0]
    strb w3, [x5, #0]
    strb w2, [x4, #0]
    add x5, x5, #1
    sub x4, x4, #1
    b .r_lp
.done:
    ret
@endproc

;; @proc parse_signed_dec_a64
;; @abi win_arm64
;; @in x0 = ASCII string
;; @out x0 = signed 64-bit value (w0 = low 32 bits), x1 = pointer to first non-digit
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Parses an optional `+/-` sign followed by base-10 digits.
@proc parse_signed_dec_a64
    mov x1, x0
    mov w4, #0 ; neg
    ldrb w2, [x1, #0]
    cmp w2, #0x2D
    b.ne .chk_p
    mov w4, #1
    add x1, x1, #1
    b .digs
.chk_p:
    cmp w2, #0x2B
    b.ne .digs
    add x1, x1, #1
.digs:
    mov x0, #0
    mov x3, #10
.lp:
    ldrb w2, [x1, #0]
    sub w2, w2, #0x30
    cmp w2, #10
    b.hs .fin
    uxtw x2, w2
    mul x0, x0, x3
    add x0, x0, x2
    add x1, x1, #1
    b .lp
.fin:
    cbz w4, .done
    neg x0, x0
.done:
    ret
@endproc

;; @proc arena_init_a64
;; @abi win_arm64
;; @in none
;; @out w0 = 0 on success, ERR_OUT_OF_MEMORY on failure
;; @err w0 != 0 on VirtualAlloc failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Reserves the 4 MiB statement arena, commits the first 64 KiB page block, and allocates persistent stacks.
@proc arena_init_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp

    bl hal_get_ctx_a64
    mov x19, x0

    mov x0, #0
    mov_imm32 x1, A64_ARENA_RESERVE
    mov_imm32 w2, 0x2000 ; MEM_RESERVE
    mov w3, #4 ; PAGE_READWRITE
    bl hal_virtual_alloc_a64
    cbz x0, .oom
    mov x20, x0
    str x20, [x19, #ARM64_CTX.pArenaBase]
    str x20, [x19, #ARM64_CTX.pArenaPtr]
    mov_imm32 x1, A64_ARENA_RESERVE
    add x1, x20, x1
    str x1, [x19, #ARM64_CTX.pArenaLimit]

    mov x0, x20
    mov_imm32 x1, A64_ARENA_CHUNK
    mov_imm32 w2, 0x1000 ; MEM_COMMIT
    mov w3, #4
    bl hal_virtual_alloc_a64
    cbz x0, .oom
    mov_imm32 x1, A64_ARENA_CHUNK
    add x1, x20, x1
    str x1, [x19, #ARM64_CTX.pArenaCommitEnd]

    ; Commit 192 KiB persistent pool + fixed Batch/SETLOCAL/PUSHD/FOR/DOSKEY/Env blocks
    mov x0, #0
    mov_imm32 x1, 196608
    mov_imm32 w2, 0x3000 ; MEM_RESERVE | MEM_COMMIT
    mov w3, #4
    bl hal_virtual_alloc_a64
    cbz x0, .oom
    str x0, [x19, #ARM64_CTX.pBatchStack] ; 32 * 64 = 2048B
    add x0, x0, #2048
    str x0, [x19, #ARM64_CTX.pSetlocalStack] ; 32 * 32 = 1024B
    add x0, x0, #1024
    str x0, [x19, #ARM64_CTX.pPushdStack] ; 32 * 8 = 256B
    add x0, x0, #256
    str x0, [x19, #ARM64_CTX.pForStack] ; 32 * 16 = 512B
    add x0, x0, #512
    str x0, [x19, #ARM64_CTX.pHistRing] ; 64 * 8 = 512B
    add x0, x0, #512
    str x0, [x19, #ARM64_CTX.pMacroNames] ; 32 * 8 = 256B
    add x0, x0, #256
    str x0, [x19, #ARM64_CTX.pMacroBodies] ; 32 * 8 = 256B
    add x0, x0, #256
    str x0, [x19, #ARM64_CTX.pEnvBlock] ; 64 KiB mutable WTF-8 env block
    mov_imm32 x1, A64_ENV_MAX
    str w1, [x19, #ARM64_CTX.dwEnvCap]
    add x0, x0, x1
    str x0, [x19, #ARM64_CTX.pPermPoolPtr]
    mov_imm32 x1, 126208
    add x1, x0, x1
    str x1, [x19, #ARM64_CTX.pPermPoolEnd]

    mov w0, #0
    b .exit
.oom:
    mov w0, #ERR_OUT_OF_MEMORY
.exit:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc arena_alloc_a64
;; @abi win_arm64
;; @in x0 = requested byte size
;; @out x0 = 16-byte-aligned zeroed buffer pointer (or 0 on OOM)
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Allocates 16-byte-aligned memory from the statement arena, committing 64 KiB pages on demand.
@proc arena_alloc_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    add x20, x0, #15
    and x20, x20, #-16
    cmp x20, #0
    b.ne .sz_ok
    mov x20, #16
.sz_ok:
    bl hal_get_ctx_a64
    mov x19, x0
    ldr x21, [x19, #ARM64_CTX.pArenaPtr]
    add x22, x21, x20
    ldr x0, [x19, #ARM64_CTX.pArenaLimit]
    cmp x22, x0
    b.hi .oom

.chk_commit:
    ldr x0, [x19, #ARM64_CTX.pArenaCommitEnd]
    cmp x22, x0
    b.ls .commit_ok
    mov_imm32 x1, A64_ARENA_CHUNK
    mov w2, #0x1000
    mov w3, #4
    bl hal_virtual_alloc_a64
    cbz x0, .oom
    ldr x0, [x19, #ARM64_CTX.pArenaCommitEnd]
    mov_imm32 x1, A64_ARENA_CHUNK
    add x0, x0, x1
    str x0, [x19, #ARM64_CTX.pArenaCommitEnd]
    b .chk_commit

.commit_ok:
    str x22, [x19, #ARM64_CTX.pArenaPtr]
    mov x0, x21
    mov x1, x20
    bl mem_zero_a64
    mov x0, x21
    b .done
.oom:
    mov x0, #0
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc arena_reset_a64
;; @abi win_arm64
;; @in none
;; @out Resets ARM64_CTX.pArenaPtr to pArenaBase in O(1) without syscalls
;; @err none
;; @clobbers x0, x1
;; @desc Resets the statement arena pointer in 2 instructions.
@proc arena_reset_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    bl hal_get_ctx_a64
    ldr x1, [x0, #ARM64_CTX.pArenaBase]
    str x1, [x0, #ARM64_CTX.pArenaPtr]
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc perm_alloc_a64
;; @abi win_arm64
;; @in x0 = requested byte size
;; @out x0 = 16-byte-aligned persistent buffer pointer (or 0 on OOM)
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Allocates persistent heap storage (for environment snapshots, PUSHD, CWD, DOSKEY).
@proc perm_alloc_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    add x20, x0, #15
    and x20, x20, #-16
    bl hal_get_ctx_a64
    mov x19, x0
    ldr x21, [x19, #ARM64_CTX.pPermPoolPtr]
    add x22, x21, x20
    ldr x0, [x19, #ARM64_CTX.pPermPoolEnd]
    cmp x22, x0
    b.ls .fit

    ; Allocate a new 128 KiB persistent block
    mov x0, #0
    mov_imm32 x1, 131072
    cmp x20, x1
    b.ls .do_va
    add x1, x20, #4096
.do_va:
    mov x22, x1
    mov_imm32 w2, 0x3000
    mov w3, #4
    bl hal_virtual_alloc_a64
    cbz x0, .oom
    mov x21, x0
    add x1, x21, x22
    str x1, [x19, #ARM64_CTX.pPermPoolEnd]
    add x22, x21, x20

.fit:
    str x22, [x19, #ARM64_CTX.pPermPoolPtr]
    mov x0, x21
    mov x1, x20
    bl mem_zero_a64
    mov x0, x21
    b .done
.oom:
    mov x0, #0
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc arena_strdup_a64
;; @abi win_arm64
;; @in x0 = WTF-8 string pointer
;; @out x0 = duplicated WTF-8 string in statement arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Duplicates a NUL-terminated string into the statement arena.
@proc arena_strdup_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl str_len_a64
    add x20, x0, #1
    mov x0, x20
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov x1, x19
    bl str_copy_a64
    mov x0, x20
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc perm_strdup_a64
;; @abi win_arm64
;; @in x0 = WTF-8 string pointer
;; @out x0 = duplicated WTF-8 string in persistent pool
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Duplicates a NUL-terminated string into the persistent pool for batch frames, PUSHD, and DOSKEY.
@proc perm_strdup_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl str_len_a64
    add x20, x0, #1
    mov x0, x20
    bl perm_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov x1, x19
    bl str_copy_a64
    mov x0, x20
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc wtf8_to_u16_a64
;; @abi win_arm64
;; @in x0 = NUL-terminated WTF-8 string
;; @out x0 = NUL-terminated UTF-16LE WCHAR* in statement arena, x1 = WCHAR count
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Transcodes WTF-8 (ASCII, 2/3/4-byte UTF-8, and unpaired surrogates) to UTF-16LE.
@proc wtf8_to_u16_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl str_len_a64
    lsl x0, x0, #1
    add x0, x0, #8
    bl arena_alloc_a64
    cbz x0, .oom
    mov x20, x0 ; base
    mov x1, x20 ; out WCHAR*
    mov x2, x19 ; src byte*

.lp:
    ldrb w3, [x2, #0]
    cbz w3, .fin
    tbz w3, #7, .ascii
    and w4, w3, #0xE0
    cmp w4, #0xC0
    b.eq .two_b
    and w4, w3, #0xF0
    cmp w4, #0xE0
    b.eq .three_b
    and w4, w3, #0xF8
    cmp w4, #0xF0
    b.eq .four_b
    ; Raw fallback byte
    strh w3, [x1, #0]
    add x1, x1, #2
    add x2, x2, #1
    b .lp
.ascii:
    strh w3, [x1, #0]
    add x1, x1, #2
    add x2, x2, #1
    b .lp
.two_b:
    ldrb w4, [x2, #1]
    and w3, w3, #0x1F
    and w4, w4, #0x3F
    lsl w3, w3, #6
    orr w3, w3, w4
    strh w3, [x1, #0]
    add x1, x1, #2
    add x2, x2, #2
    b .lp
.three_b:
    ldrb w4, [x2, #1]
    ldrb w5, [x2, #2]
    and w3, w3, #0x0F
    and w4, w4, #0x3F
    and w5, w5, #0x3F
    lsl w3, w3, #12
    lsl w4, w4, #6
    orr w3, w3, w4
    orr w3, w3, w5
    strh w3, [x1, #0]
    add x1, x1, #2
    add x2, x2, #3
    b .lp
.four_b:
    ldrb w4, [x2, #1]
    ldrb w5, [x2, #2]
    ldrb w6, [x2, #3]
    and w3, w3, #0x07
    and w4, w4, #0x3F
    and w5, w5, #0x3F
    and w6, w6, #0x3F
    lsl w3, w3, #18
    lsl w4, w4, #12
    lsl w5, w5, #6
    orr w3, w3, w4
    orr w3, w3, w5
    orr w3, w3, w6
    mov_imm32 w4, 0x10000
    sub w3, w3, w4
    lsr w4, w3, #10
    mov_imm32 w5, 0xD800
    add w4, w4, w5
    and w3, w3, #0x3FF
    mov_imm32 w5, 0xDC00
    add w3, w3, w5
    strh w4, [x1, #0]
    strh w3, [x1, #2]
    add x1, x1, #4
    add x2, x2, #4
    b .lp
.fin:
    strh wzr, [x1, #0]
    sub x1, x1, x20
    lsr x1, x1, #1
    mov x0, x20
    b .done
.oom:
    mov x0, #0
    mov x1, #0
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc u16_to_wtf8_buf_a64
;; @abi win_arm64
;; @in x0 = src UTF-16LE WCHAR* (potentially unaligned), x1 = max WCHARs (-1 for NUL-terminated), x2 = dst WTF-8 buffer
;; @out x0 = bytes written (excluding trailing NUL)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Transcodes UTF-16LE (including surrogate pairs and unpaired surrogates) to WTF-8 in x2.
@proc u16_to_wtf8_buf_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x2 ; dst base
    mov x20, x2 ; dst cursor
    cbz x0, .fin
.lp:
    cbz x1, .fin
    ldrb w2, [x0, #0]
    ldrb w3, [x0, #1]
    orr w2, w2, w3, lsl #8
    cbz w2, .fin
    add x0, x0, #2
    cmp x1, #0
    b.lt .after_dec
    sub x1, x1, #1
.after_dec:
    cmp w2, #0x80
    b.lo .one_b
    cmp w2, #0x800
    b.lo .two_b
    ; Check high surrogate 0xD800..0xDBFF followed by low surrogate 0xDC00..0xDFFF
    mov_imm32 w4, 0xFC00
    and w3, w2, w4
    mov_imm32 w5, 0xD800
    cmp w3, w5
    b.ne .three_b
    cbz x1, .three_b
    ldrb w3, [x0, #0]
    ldrb w5, [x0, #1]
    orr w3, w3, w5, lsl #8
    and w4, w3, w4
    mov_imm32 w5, 0xDC00
    cmp w4, w5
    b.ne .three_b
    add x0, x0, #2
    cmp x1, #0
    b.lt .comb_sur
    sub x1, x1, #1
.comb_sur:
    and w2, w2, #0x3FF
    and w3, w3, #0x3FF
    lsl w2, w2, #10
    orr w2, w2, w3
    mov_imm32 w4, 0x10000
    add w2, w2, w4
    lsr w3, w2, #18
    orr w3, w3, #0xF0
    strb w3, [x20, #0]
    lsr w3, w2, #12
    and w3, w3, #0x3F
    orr w3, w3, #0x80
    strb w3, [x20, #1]
    lsr w3, w2, #6
    and w3, w3, #0x3F
    orr w3, w3, #0x80
    strb w3, [x20, #2]
    and w3, w2, #0x3F
    orr w3, w3, #0x80
    strb w3, [x20, #3]
    add x20, x20, #4
    b .lp
.one_b:
    strb w2, [x20, #0]
    add x20, x20, #1
    b .lp
.two_b:
    lsr w3, w2, #6
    orr w3, w3, #0xC0
    strb w3, [x20, #0]
    and w3, w2, #0x3F
    orr w3, w3, #0x80
    strb w3, [x20, #1]
    add x20, x20, #2
    b .lp
.three_b:
    lsr w3, w2, #12
    orr w3, w3, #0xE0
    strb w3, [x20, #0]
    lsr w3, w2, #6
    and w3, w3, #0x3F
    orr w3, w3, #0x80
    strb w3, [x20, #1]
    and w3, w2, #0x3F
    orr w3, w3, #0x80
    strb w3, [x20, #2]
    add x20, x20, #3
    b .lp
.fin:
    strb wzr, [x20, #0]
    sub x0, x20, x19
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc to_u16_longpath_a64
;; @abi win_arm64
;; @in x0 = normalized WTF-8 path
;; @out x0 = UTF-16LE WCHAR* (automatically promoted to `\?` or `\?\UNC` when >= 248 chars!)
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Promotes paths >= 248 chars to `\?` / `\?\UNC` up to 32,767 WCHARs on Windows 10 & 11 ARM64.
@proc to_u16_longpath_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl str_len_a64
    cmp x0, #248
    b.lo .direct

    ; Already starts with `\?` or `\.`?
    ldrb w1, [x19, #0]
    cmp w1, #0x5C
    b.ne .promote_drv
    ldrb w1, [x19, #1]
    cmp w1, #0x5C
    b.ne .direct
    ldrb w1, [x19, #2]
    cmp w1, #0x3F
    b.eq .direct
    cmp w1, #0x2E
    b.eq .direct

    ; UNC `\server\share` -> `\?\UNC\server\share`
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov w1, #0x5C
    strb w1, [x20, #0]
    strb w1, [x20, #1]
    mov w2, #0x3F
    strb w2, [x20, #2]
    strb w1, [x20, #3]
    mov w2, #0x55
    strb w2, [x20, #4]
    mov w2, #0x4E
    strb w2, [x20, #5]
    mov w2, #0x43
    strb w2, [x20, #6]
    strb w1, [x20, #7]
    add x0, x20, #8
    add x1, x19, #2
    bl str_copy_a64
    mov x0, x20
    bl wtf8_to_u16_a64
    b .done

.promote_drv:
    ldrb w1, [x19, #1]
    cmp w1, #0x3A
    b.ne .direct
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov w1, #0x5C
    strb w1, [x20, #0]
    strb w1, [x20, #1]
    mov w2, #0x3F
    strb w2, [x20, #2]
    strb w1, [x20, #3]
    add x0, x20, #4
    mov x1, x19
    bl str_copy_a64
    mov x0, x20
    bl wtf8_to_u16_a64
    b .done

.direct:
    mov x0, x19
    bl wtf8_to_u16_a64
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc out_write_fd_a64
;; @abi win_arm64
;; @in w0 = FD (1 = stdout, 2 = stderr), x1 = WTF-8 bytes, x2 = byte length
;; @out Writes to redirected FD handle or console
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes x2 WTF-8 bytes to active FD w0 (`fdTable[w0]` or `hStdOut`/`hStdErr`).
@proc out_write_fd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    cbz x2, .done
    mov w19, w0
    mov x20, x1
    mov x4, x2
    stp x4, xzr, [sp, #-16]!
    bl hal_get_ctx_a64
    ldp x2, xzr, [sp], #16
    uxtw x3, w19
    lsl x3, x3, #3
    add x3, x0, x3
    ldr x0, [x3, #ARM64_CTX.fdTable]
    mov x1, x20
    bl hal_write_std_a64
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc out_str_a64
;; @abi win_arm64
;; @in x0 = NUL-terminated WTF-8 string
;; @out Writes string to FD 1 (stdout)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes a NUL-terminated WTF-8 string to FD 1 (stdout).
@proc out_str_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    cbz x0, .done
    mov x19, x0
    bl str_len_a64
    mov x2, x0
    mov w0, #1
    mov x1, x19
    bl out_write_fd_a64
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc err_str_a64
;; @abi win_arm64
;; @in x0 = NUL-terminated WTF-8 string
;; @out Writes string to FD 2 (stderr)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes a NUL-terminated WTF-8 string to FD 2 (stderr).
@proc err_str_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    cbz x0, .done
    mov x19, x0
    bl str_len_a64
    mov x2, x0
    mov w0, #2
    mov x1, x19
    bl out_write_fd_a64
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc out_crlf_a64
;; @abi win_arm64
;; @in none
;; @out Writes CRLF to FD 1 (stdout)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Emits CRLF to FD 1.
@proc out_crlf_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov w0, #STR_OFF_CRLF
    bl str_ptr_a64
    bl out_str_a64
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc print_err_code_a64
;; @abi win_arm64
;; @in w0 = canonical ERR_* code
;; @out Prints matching error string to FD 2 (stderr)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Maps canonical ERR_* codes to deterministic error messages on FD 2.
@proc print_err_code_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    cmp w0, #ERR_FILE_NOT_FOUND
    b.eq .fnf
    cmp w0, #ERR_PATH_NOT_FOUND
    b.eq .pnf
    cmp w0, #ERR_ACCESS_DENIED
    b.eq .acc
    mov_imm32 w1, ERR_DIV_BY_ZERO
    cmp w0, w1
    b.eq .dvz
    mov_imm32 w1, ERR_INVALID_NUMBER
    cmp w0, w1
    b.eq .inv
    mov_imm32 w1, ERR_BAD_COMMAND
    cmp w0, w1
    b.eq .bad
    mov w0, #STR_OFF_SYNTAX_ERR
    b .emit
.fnf:
    mov w0, #STR_OFF_FILE_NOT_FOUND
    b .emit
.pnf:
    mov w0, #STR_OFF_PATH_NOT_FOUND
    b .emit
.acc:
    mov w0, #STR_OFF_ACCESS_DENIED
    b .emit
.dvz:
    mov w0, #STR_OFF_DIV_ZERO
    b .emit
.inv:
    mov w0, #STR_OFF_INV_NUM
    b .emit
.bad:
    mov w0, #STR_OFF_BAD_CMD
.emit:
    bl str_ptr_a64
    bl err_str_a64
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
