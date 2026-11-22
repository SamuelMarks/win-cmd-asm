;; @file env_sa_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 WTF-8 Environment Manager, Dynamic Variables, 32-level SETLOCAL stack, and 11-tier SET /A evaluator.

@cpu arm64
@format inc

;; @proc env_key_match_a64
;; @abi win_arm64
;; @in x0 = entry pointer ("KEY=VAL"), x1 = search key ("KEY" or "=C:")
;; @out x0 = pointer to value right after `=` if matched, else 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Case-insensitively compares an environment block entry key against x1, supporting hidden `=C:` keys.
@proc env_key_match_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov x20, x1

    ; Hidden drive keys start with `=`: match leading `=` literally first
    ldrb w0, [x20, #0]
    cmp w0, #0x3D
    b.ne .cmp_lp
    ldrb w1, [x19, #0]
    cmp w1, #0x3D
    b.ne .no_match
    add x19, x19, #1
    add x20, x20, #1

.cmp_lp:
    ldrb w0, [x20, #0]
    cbz w0, .chk_eq
    ldrb w1, [x19, #0]
    cbz w1, .no_match
    cmp w1, #0x3D
    b.eq .no_match
    mov w4, w1
    bl ascii_upper_a64
    mov w5, w0
    mov w0, w4
    bl ascii_upper_a64
    cmp w0, w5
    b.ne .no_match
    add x19, x19, #1
    add x20, x20, #1
    b .cmp_lp

.chk_eq:
    ldrb w0, [x19, #0]
    cmp w0, #0x3D
    b.ne .no_match
    add x0, x19, #1
    b .done

.no_match:
    mov x0, #0
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc env_get_raw_a64
;; @abi win_arm64
;; @in x0 = variable name (NUL-terminated WTF-8)
;; @out x0 = direct pointer into pEnvBlock value (or 0 if not set)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Searches ARM64_CTX.pEnvBlock (`KEY=VAL\0...\0\0`) case-insensitively.
@proc env_get_raw_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    cbz x19, .not_found
    ldrb w0, [x19, #0]
    cbz w0, .not_found

    bl hal_get_ctx_a64
    ldr x20, [x0, #ARM64_CTX.pEnvBlock]
    cbz x20, .not_found

.entry_lp:
    ldrb w0, [x20, #0]
    cbz w0, .not_found
    mov x0, x20
    mov x1, x19
    bl env_key_match_a64
    cbnz x0, .found
.skip_nul:
    ldrb w0, [x20, #0]
    add x20, x20, #1
    cbnz w0, .skip_nul
    b .entry_lp

.not_found:
    mov x0, #0
.found:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc fmt_hex8_a64
;; @abi win_arm64
;; @in w0 = 32-bit unsigned value, x1 = 9-byte output buffer
;; @out Writes 8 uppercase hex digits + NUL into x1
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Formats dwLastErrorLevel as 8 uppercase hex digits for `%=ExitCode%`.
@proc fmt_hex8_a64
    mov w2, #28
.hex_lp:
    lsrv w3, w0, w2
    and w3, w3, #0x0F
    cmp w3, #10
    b.lo .dig
    add w3, w3, #0x37
    b .st
.dig:
    add w3, w3, #0x30
.st:
    strb w3, [x1, #0]
    add x1, x1, #1
    subs w2, w2, #4
    b.ge .hex_lp
    strb wzr, [x1, #0]
    ret
@endproc

;; @proc fmt_dyn_date_time_a64
;; @abi win_arm64
;; @in w0 = mode (0 = DATE "YYYY-MM-DD", 1 = TIME "HH:MM:SS.CC" with leading space if hour < 10)
;; @out x0 = arena string
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Synthesizes dynamic `%DATE%` or `%TIME%` from hal_get_local_time_a64.
@proc fmt_dyn_date_time_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #32 ; SYSTEMTIME (16B) at sp+0
    mov x29, sp
    mov w19, w0

    mov x0, sp
    bl hal_get_local_time_a64

    mov x0, #32
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov x21, x0

    cbnz w19, .do_time

    ; Format YYYY-MM-DD
    ldrh w0, [sp, #0] ; wYear
    mov w1, #100
    udiv w2, w0, w1
    msub w22, w2, w1, w0
    mov w0, w2
    mov x1, x21
    bl fmt_2dig_a64
    mov x21, x0
    mov w0, w22
    mov x1, x21
    bl fmt_2dig_a64
    mov x21, x0
    mov w1, #0x2D
    strb w1, [x21, #0]
    add x21, x21, #1
    ldrh w0, [sp, #2] ; wMonth
    mov x1, x21
    bl fmt_2dig_a64
    mov x21, x0
    mov w1, #0x2D
    strb w1, [x21, #0]
    add x21, x21, #1
    ldrh w0, [sp, #6] ; wDay
    mov x1, x21
    bl fmt_2dig_a64
    strb wzr, [x0, #0]
    mov x0, x20
    b .done

.do_time:
    ; Format HH:MM:SS.CC (leading space if wHour < 10)
    ldrh w0, [sp, #8] ; wHour
    cmp w0, #10
    b.hs .hr_2dig
    mov w1, #0x20
    strb w1, [x21, #0]
    add w0, w0, #0x30
    strb w0, [x21, #1]
    add x21, x21, #2
    b .after_hr
.hr_2dig:
    mov x1, x21
    bl fmt_2dig_a64
    mov x21, x0
.after_hr:
    mov w1, #0x3A
    strb w1, [x21, #0]
    add x21, x21, #1
    ldrh w0, [sp, #10] ; wMinute
    mov x1, x21
    bl fmt_2dig_a64
    mov x21, x0
    mov w1, #0x3A
    strb w1, [x21, #0]
    add x21, x21, #1
    ldrh w0, [sp, #12] ; wSecond
    mov x1, x21
    bl fmt_2dig_a64
    mov x21, x0
    mov w1, #0x2E
    strb w1, [x21, #0]
    add x21, x21, #1
    ldrh w0, [sp, #14] ; wMilliseconds
    mov w1, #10
    udiv w0, w0, w1
    mov x1, x21
    bl fmt_2dig_a64
    strb wzr, [x0, #0]
    mov x0, x20

.done:
    add sp, sp, #32
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc env_get_a64
;; @abi win_arm64
;; @in x0 = variable name (NUL-terminated WTF-8)
;; @out x0 = arena string value (or 0 if undefined)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Looks up an explicit environment variable or synthesizes dynamic `%CD%`, `%ERRORLEVEL%`, `%RANDOM%`, `%DATE%`, `%TIME%`, `%=ExitCode%`, `%=A:%`..`%=Z:%`.
@proc env_get_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    bl env_get_raw_a64
    cbz x0, .try_dyn
    bl arena_strdup_a64
    b .exit

.try_dyn:
    bl hal_get_ctx_a64
    mov x20, x0
    ldrb w0, [x20, #ARM64_CTX.bExtensions]
    cbz w0, .chk_hidden_drv

    mov x0, x19
    adr_sym x1, szDynCd_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_cd

    mov x0, x19
    adr_sym x1, szDynErrLvl_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_errlvl

    mov x0, x19
    adr_sym x1, szDynExtVer_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_extver

    mov x0, x19
    adr_sym x1, szDynCmdLine_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_cmdline

    mov x0, x19
    adr_sym x1, szDynRandom_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_random

    mov x0, x19
    adr_sym x1, szDynDate_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_date

    mov x0, x19
    adr_sym x1, szDynTime_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_time

    mov x0, x19
    adr_sym x1, szDynNuma_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_numa

    mov x0, x19
    adr_sym x1, szDynExitCode_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_exit_hex

    mov x0, x19
    adr_sym x1, szDynExitAscii_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .dyn_exit_asc

.chk_hidden_drv:
    ldrb w0, [x19, #0]
    cmp w0, #0x3D
    b.ne .not_def
    ldrb w0, [x19, #1]
    bl ascii_upper_a64
    sub w0, w0, #0x41
    cmp w0, #26
    b.hs .not_def
    ldrb w1, [x19, #2]
    cmp w1, #0x3A
    b.ne .not_def
    ldrb w1, [x19, #3]
    cbnz w1, .not_def
    uxtw x0, w0
    lsl x0, x0, #3
    add x1, x20, #ARM64_CTX.drvCwdsPtrs
    ldr x0, [x1, x0]
    cbz x0, .not_def
    bl arena_strdup_a64
    b .exit

.dyn_cd:
    add x0, x20, #ARM64_CTX.szCwd
    bl arena_strdup_a64
    b .exit

.dyn_errlvl:
    mov x0, #24
    bl arena_alloc_a64
    cbz x0, .not_def
    mov x21, x0
    ldr w0, [x20, #ARM64_CTX.dwLastErrorLevel]
    mov x1, x21
    bl fmt_i32_a64
    mov x0, x21
    b .exit

.dyn_extver:
    mov x0, #16
    bl arena_alloc_a64
    cbz x0, .not_def
    mov w1, #0x32
    strb w1, [x0, #0]
    strb wzr, [x0, #1]
    b .exit

.dyn_cmdline:
    ldr x0, [x20, #ARM64_CTX.pCmdLineRaw]
    cbz x0, .not_def
    bl arena_strdup_a64
    b .exit

.dyn_random:
    ldr w0, [x20, #ARM64_CTX.dwRandomSeed]
    mov_imm32 w1, 214013
    mul w2, w0, w1
    mov_imm32 w1, 2531011
    add w2, w2, w1
    str w2, [x20, #ARM64_CTX.dwRandomSeed]
    lsr w21, w2, #16
    and w21, w21, #0x7FFF
    mov x0, #16
    bl arena_alloc_a64
    cbz x0, .not_def
    mov x22, x0
    mov w0, w21
    mov x1, x22
    bl fmt_i32_a64
    mov x0, x22
    b .exit

.dyn_date:
    mov w0, #0
    bl fmt_dyn_date_time_a64
    b .exit

.dyn_time:
    mov w0, #1
    bl fmt_dyn_date_time_a64
    b .exit

.dyn_numa:
    mov x0, #16
    bl arena_alloc_a64
    cbz x0, .not_def
    mov w1, #0x30
    strb w1, [x0, #0]
    strb wzr, [x0, #1]
    b .exit

.dyn_exit_hex:
    mov x0, #16
    bl arena_alloc_a64
    cbz x0, .not_def
    mov x21, x0
    ldr w0, [x20, #ARM64_CTX.dwLastErrorLevel]
    mov x1, x21
    bl fmt_hex8_a64
    mov x0, x21
    b .exit

.dyn_exit_asc:
    ldr w21, [x20, #ARM64_CTX.dwLastErrorLevel]
    and w21, w21, #0xFF
    cmp w21, #0x20
    b.lo .not_def
    cmp w21, #0x7E
    b.hi .not_def
    mov x0, #16
    bl arena_alloc_a64
    cbz x0, .not_def
    strb w21, [x0, #0]
    strb wzr, [x0, #1]
    b .exit

.not_def:
    mov x0, #0
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc env_unset_a64
;; @abi win_arm64
;; @in x0 = variable name
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Removes any existing `NAME=...` entry from ARM64_CTX.pEnvBlock.
@proc env_unset_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    bl hal_get_ctx_a64
    mov x20, x0
    ldr x21, [x20, #ARM64_CTX.pEnvBlock]
    cbz x21, .done

.scan_lp:
    ldrb w0, [x21, #0]
    cbz w0, .done
    mov x0, x21
    mov x1, x19
    bl env_key_match_a64
    cbnz x0, .remove_it

.next_e:
    ldrb w0, [x21, #0]
    add x21, x21, #1
    cbnz w0, .next_e
    b .scan_lp

.remove_it:
    mov x22, x21
.find_end:
    ldrb w0, [x22, #0]
    add x22, x22, #1
    cbnz w0, .find_end

    ldr x0, [x20, #ARM64_CTX.pEnvBlock]
    ldr w1, [x20, #ARM64_CTX.dwEnvLen]
    uxtw x1, w1
    add x2, x0, x1
    sub x2, x2, x22
    sub x3, x22, x21
    sub x1, x1, x3
    str w1, [x20, #ARM64_CTX.dwEnvLen]
    mov x0, x21
    mov x1, x22
    bl mem_copy_a64
    b .scan_lp

.done:
    mov w0, #0
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc env_set_a64
;; @abi win_arm64
;; @in x0 = variable name, x1 = value string (NULL or empty unsets variable!)
;; @out w0 = 0 on success or ERR_OUT_OF_MEMORY
;; @err w0 = ERR_OUT_OF_MEMORY if 64 KiB block is full
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Sets or deletes `NAME=VALUE` in ARM64_CTX.pEnvBlock.
@proc env_set_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov x20, x1

    bl env_unset_a64
    cbz x20, .ok
    ldrb w0, [x20, #0]
    cbz w0, .ok

    mov x0, x19
    bl str_len_a64
    mov x21, x0
    mov x0, x20
    bl str_len_a64
    mov x22, x0

    bl hal_get_ctx_a64
    mov x23, x0
    ldr w0, [x23, #ARM64_CTX.dwEnvLen]
    cmp w0, #2
    b.hs .has_len
    mov w0, #1
.has_len:
    sub w1, w0, #1
    uxtw x1, w1
    add x2, x1, x21
    add x2, x2, x22
    add x2, x2, #3
    ldr w3, [x23, #ARM64_CTX.dwEnvCap]
    uxtw x3, w3
    cmp x2, x3
    b.hi .oom

    ldr x0, [x23, #ARM64_CTX.pEnvBlock]
    add x0, x0, x1
    mov x1, x19
    str w2, [x23, #ARM64_CTX.dwEnvLen]
    bl str_copy_a64
    mov w1, #0x3D
    strb w1, [x0, #0]
    add x0, x0, #1
    mov x1, x20
    bl str_copy_a64
    strb wzr, [x0, #0]
    strb wzr, [x0, #1]

.ok:
    mov w0, #0
    b .exit
.oom:
    mov w0, #ERR_OUT_OF_MEMORY
.exit:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc setlocal_push_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv (qword array of WTF-8 pointers)
;; @out w0 = 0 on success or ERR_OUT_OF_MEMORY
;; @err w0 != 0 on stack overflow or OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Pushes a SETLOCAL snapshot (up to 32 levels) and applies optional extension/delayed-expansion switches.
@proc setlocal_push_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov w21, w0 ; argc
    mov x22, x1 ; argv

    bl hal_get_ctx_a64
    mov x19, x0
    ldr w20, [x19, #ARM64_CTX.dwSetlocalDepth]
    cmp w20, #A64_SETLOCAL_MAX
    b.hs .oom

    ldr x0, [x19, #ARM64_CTX.pSetlocalStack]
    uxtw x1, w20
    lsl x1, x1, #5 ; * 32 (sizeof SETLOCAL_SNAP_A64)
    add x23, x0, x1

    ldr w0, [x19, #ARM64_CTX.dwEnvLen]
    uxtw x0, w0
    add x0, x0, #16
    bl perm_alloc_a64
    cbz x0, .oom
    str x0, [x23, #SETLOCAL_SNAP_A64.pEnvCopy]
    ldr w2, [x19, #ARM64_CTX.dwEnvLen]
    str w2, [x23, #SETLOCAL_SNAP_A64.dwEnvLen]
    uxtw x2, w2
    ldr x1, [x19, #ARM64_CTX.pEnvBlock]
    bl mem_copy_a64

    mov x0, #512
    bl perm_alloc_a64
    cbz x0, .oom
    str x0, [x23, #SETLOCAL_SNAP_A64.pCwdCopy]
    add x1, x19, #ARM64_CTX.szCwd
    bl str_copy_a64

    ldrb w0, [x19, #ARM64_CTX.bExtensions]
    strb w0, [x23, #SETLOCAL_SNAP_A64.bExtFlag]
    ldrb w0, [x19, #ARM64_CTX.bDelayedExp]
    strb w0, [x23, #SETLOCAL_SNAP_A64.bDelayedFlag]

    add w20, w20, #1
    str w20, [x19, #ARM64_CTX.dwSetlocalDepth]

    mov w20, #1
.arg_lp:
    cmp w20, w21
    b.hs .done_ok
    uxtw x0, w20
    lsl x0, x0, #3
    ldr x0, [x22, x0]
    bl strip_outer_quotes_a64
    mov x24, x0

    adr_sym x1, szEnableDelExp_a64
    mov w2, #1
    bl str_cmp_a64
    cbnz w0, .chk_dis_del
    mov w0, #1
    strb w0, [x19, #ARM64_CTX.bDelayedExp]
    b .nx_arg
.chk_dis_del:
    mov x0, x24
    adr_sym x1, szDisableDelExp_a64
    mov w2, #1
    bl str_cmp_a64
    cbnz w0, .chk_en_ext
    strb wzr, [x19, #ARM64_CTX.bDelayedExp]
    b .nx_arg
.chk_en_ext:
    mov x0, x24
    adr_sym x1, szEnableExt_a64
    mov w2, #1
    bl str_cmp_a64
    cbnz w0, .chk_dis_ext
    mov w0, #1
    strb w0, [x19, #ARM64_CTX.bExtensions]
    b .nx_arg
.chk_dis_ext:
    mov x0, x24
    adr_sym x1, szDisableExt_a64
    mov w2, #1
    bl str_cmp_a64
    cbnz w0, .nx_arg
    strb wzr, [x19, #ARM64_CTX.bExtensions]
.nx_arg:
    add w20, w20, #1
    b .arg_lp

.done_ok:
    mov w0, #0
    b .exit
.oom:
    mov w0, #ERR_OUT_OF_MEMORY
.exit:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc endlocal_pop_a64
;; @abi win_arm64
;; @in none
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Pops the top SETLOCAL snapshot if dwSetlocalDepth > 0, restoring environment, CWD, and flags.
@proc endlocal_pop_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    bl hal_get_ctx_a64
    mov x19, x0
    ldr w20, [x19, #ARM64_CTX.dwSetlocalDepth]
    cbz w20, .done
    sub w20, w20, #1
    str w20, [x19, #ARM64_CTX.dwSetlocalDepth]

    ldr x0, [x19, #ARM64_CTX.pSetlocalStack]
    uxtw x1, w20
    lsl x1, x1, #5
    add x21, x0, x1

    ldr x0, [x19, #ARM64_CTX.pEnvBlock]
    ldr x1, [x21, #SETLOCAL_SNAP_A64.pEnvCopy]
    ldr w2, [x21, #SETLOCAL_SNAP_A64.dwEnvLen]
    str w2, [x19, #ARM64_CTX.dwEnvLen]
    uxtw x2, w2
    bl mem_copy_a64

    ldr x0, [x21, #SETLOCAL_SNAP_A64.pCwdCopy]
    bl set_drv_cwd_a64

    ldrb w0, [x21, #SETLOCAL_SNAP_A64.bExtFlag]
    strb w0, [x19, #ARM64_CTX.bExtensions]
    ldrb w0, [x21, #SETLOCAL_SNAP_A64.bDelayedFlag]
    strb w0, [x19, #ARM64_CTX.bDelayedExp]

.done:
    mov w0, #0
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc sa_skip_ws_a64
;; @abi win_arm64
;; @in x0 = cursor
;; @out x0 = cursor past spaces, tabs, and quotes
;; @err none
;; @clobbers x0, x1
;; @desc Skips whitespace and double-quote characters inside `SET /A` expressions.
@proc sa_skip_ws_a64
.lp:
    ldrb w1, [x0, #0]
    cmp w1, #0x20
    b.eq .adv
    cmp w1, #0x09
    b.eq .adv
    cmp w1, #0x22
    b.eq .adv
    ret
.adv:
    add x0, x0, #1
    b .lp
@endproc

;; @proc sa_parse_num_lit_a64
;; @abi win_arm64
;; @in x0 = cursor starting at digit `'0'..'9'`
;; @out w0 = 32-bit value, x1 = advanced cursor, w2 = 0 or ERR_INVALID_NUMBER
;; @err w2 = ERR_INVALID_NUMBER on invalid octal (`08`/`09`) or malformed hex (`0x`)
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Parses decimal, `0x` hexadecimal, or `0` octal integer literals with strict `08`/`09` rejection.
@proc sa_parse_num_lit_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov w20, #0 ; accumulator

    ldrb w0, [x19, #0]
    cmp w0, #0x30
    b.ne .dec_lp
    ldrb w1, [x19, #1]
    cmp w1, #0x78
    b.eq .hex_start
    cmp w1, #0x58
    b.eq .hex_start
    cmp w1, #0x30
    b.lo .single_zero
    cmp w1, #0x39
    b.ls .oct_start

.single_zero:
    add x1, x19, #1
    mov w0, #0
    mov w2, #0
    b .exit

.hex_start:
    add x19, x19, #2
    mov w21, #0 ; hex digit count
.hex_lp:
    ldrb w0, [x19, #0]
    cmp w0, #0x30
    b.lo .hex_end
    cmp w0, #0x39
    b.ls .hex_dig
    bl ascii_upper_a64
    cmp w0, #0x41
    b.lo .hex_end
    cmp w0, #0x46
    b.hi .hex_end
    sub w0, w0, #0x37
    b .hex_acc
.hex_dig:
    sub w0, w0, #0x30
.hex_acc:
    lsl w20, w20, #4
    orr w20, w20, w0
    add x19, x19, #1
    add w21, w21, #1
    b .hex_lp
.hex_end:
    cbz w21, .inv_num
    b .chk_trail_alpha

.oct_start:
    add x19, x19, #1
.oct_lp:
    ldrb w0, [x19, #0]
    cmp w0, #0x30
    b.lo .chk_trail_alpha
    cmp w0, #0x37
    b.ls .oct_dig
    cmp w0, #0x39
    b.ls .inv_num ; 08 or 09 -> ERR_INVALID_NUMBER!
    b .chk_trail_alpha
.oct_dig:
    sub w0, w0, #0x30
    lsl w20, w20, #3
    orr w20, w20, w0
    add x19, x19, #1
    b .oct_lp

.dec_lp:
    ldrb w0, [x19, #0]
    cmp w0, #0x30
    b.lo .chk_trail_alpha
    cmp w0, #0x39
    b.hi .chk_trail_alpha
    sub w0, w0, #0x30
    mov w1, #10
    mul w2, w20, w1
    add w20, w2, w0
    add x19, x19, #1
    b .dec_lp

.chk_trail_alpha:
    ldrb w0, [x19, #0]
    bl ascii_upper_a64
    cmp w0, #0x41
    b.lo .ok_lit
    cmp w0, #0x5A
    b.ls .inv_num
    cmp w0, #0x5F
    b.eq .inv_num
.ok_lit:
    mov w0, w20
    mov x1, x19
    mov w2, #0
    b .exit

.inv_num:
    mov w0, #0
    mov x1, x19
    mov_imm32 w2, ERR_INVALID_NUMBER
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc sa_is_ident_char_a64
;; @abi win_arm64
;; @in w0 = byte char
;; @out w0 = 1 if valid SET /A identifier char, else 0
;; @err none
;; @clobbers x0, x1
;; @desc Checks if w0 can belong to a bare variable identifier inside `SET /A`.
@proc sa_is_ident_char_a64
    cmp w0, #0x30
    b.lo .chk_special
    cmp w0, #0x39
    b.ls .yes
    cmp w0, #0x41
    b.lo .chk_special
    cmp w0, #0x5A
    b.ls .yes
    cmp w0, #0x61
    b.lo .chk_under
    cmp w0, #0x7A
    b.ls .yes
.chk_under:
    cmp w0, #0x5F
    b.eq .yes
    cmp w0, #0x80
    b.hs .yes
.chk_special:
    cmp w0, #0x24
    b.eq .yes
    cmp w0, #0x23
    b.eq .yes
    cmp w0, #0x40
    b.eq .yes
    mov w0, #0
    ret
.yes:
    mov w0, #1
    ret
@endproc

;; @proc sa_check_assign_op_a64
;; @abi win_arm64
;; @in x0 = cursor after identifier
;; @out w0 = assign op code (0=none, 1:`=`, 2:`*=`, 3:`/=`, 4:`%=`, 5:`+=`, 6:`-=`, 7:`&=`, 8:`^=`, 9:`|=`, 10:`<<=`, 11:`>>=`), x1 = cursor past op
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Checks if the next token after a variable name in `SET /A` is an assignment operator (excluding `==`).
@proc sa_check_assign_op_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    bl sa_skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    ldrb w1, [x19, #1]

    cmp w0, #0x3D
    b.ne .chk_two
    cmp w1, #0x3D
    b.eq .none
    mov w0, #1
    add x1, x19, #1
    b .exit

.chk_two:
    cmp w1, #0x3D
    b.ne .chk_shift_asgn
    add x1, x19, #2
    cmp w0, #0x2A
    b.eq .op_mul
    cmp w0, #0x2F
    b.eq .op_div
    cmp w0, #0x25
    b.eq .op_mod
    cmp w0, #0x2B
    b.eq .op_add
    cmp w0, #0x2D
    b.eq .op_sub
    cmp w0, #0x26
    b.eq .op_and
    cmp w0, #0x5E
    b.eq .op_xor
    cmp w0, #0x7C
    b.eq .op_or
    b .none

.chk_shift_asgn:
    ldrb w2, [x19, #2]
    cmp w2, #0x3D
    b.ne .none
    cmp w0, #0x3C
    b.ne .chk_shr_a
    cmp w1, #0x3C
    b.ne .none
    mov w0, #10
    add x1, x19, #3
    b .exit
.chk_shr_a:
    cmp w0, #0x3E
    b.ne .none
    cmp w1, #0x3E
    b.ne .none
    mov w0, #11
    add x1, x19, #3
    b .exit

.op_mul:
    mov w0, #2
    b .exit
.op_div:
    mov w0, #3
    b .exit
.op_mod:
    mov w0, #4
    b .exit
.op_add:
    mov w0, #5
    b .exit
.op_sub:
    mov w0, #6
    b .exit
.op_and:
    mov w0, #7
    b .exit
.op_xor:
    mov w0, #8
    b .exit
.op_or:
    mov w0, #9
    b .exit
.none:
    mov w0, #0
    mov x1, x19
.exit:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc sa_apply_binop_a64
;; @abi win_arm64
;; @in w0 = op (2..11 matching `*=..>>=`), w1 = lhs, w2 = rhs
;; @out w0 = result, w1 = 0 or ERR_DIV_BY_ZERO
;; @err w1 = ERR_DIV_BY_ZERO on `/ 0` or `% 0`
;; @clobbers x0, x1, x2, x3
;; @desc Evaluates a 32-bit wrapping binary operator with trap-safe division and `& 31` shift masking.
@proc sa_apply_binop_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    cmp w0, #2
    b.eq .do_mul
    cmp w0, #3
    b.eq .do_div
    cmp w0, #4
    b.eq .do_mod
    cmp w0, #5
    b.eq .do_add
    cmp w0, #6
    b.eq .do_sub
    cmp w0, #7
    b.eq .do_and
    cmp w0, #8
    b.eq .do_xor
    cmp w0, #9
    b.eq .do_or
    cmp w0, #10
    b.eq .do_shl
    ; op == 11 (`>>`)
    and w2, w2, #31
    asrv w0, w1, w2
    mov w1, #0
    b .exit
.do_shl:
    and w2, w2, #31
    lslv w0, w1, w2
    mov w1, #0
    b .exit
.do_mul:
    mul w0, w1, w2
    mov w1, #0
    b .exit
.do_div:
    mov w0, w1
    mov w1, w2
    bl math_sdiv32_a64
    mov w1, w2
    b .exit
.do_mod:
    mov w0, w1
    mov w1, w2
    bl math_sdiv32_a64
    mov w0, w1
    mov w1, w2
    b .exit
.do_add:
    add w0, w1, w2
    mov w1, #0
    b .exit
.do_sub:
    sub w0, w1, w2
    mov w1, #0
    b .exit
.do_and:
    and w0, w1, w2
    mov w1, #0
    b .exit
.do_xor:
    eor w0, w1, w2
    mov w1, #0
    b .exit
.do_or:
    orr w0, w1, w2
    mov w1, #0
.exit:
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc sa_read_var_i32_a64
;; @abi win_arm64
;; @in x0 = variable name string
;; @out w0 = parsed signed 32-bit value (0 if undefined or empty)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Reads a variable from the environment and parses its numeric value for `SET /A` (defaulting to 0).
@proc sa_read_var_i32_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp

    bl env_get_a64
    cbz x0, .zero
    bl sa_skip_ws_a64
    mov x19, x0
    mov w20, #0 ; neg
    ldrb w0, [x19, #0]
    cmp w0, #0x2D
    b.ne .chk_p
    mov w20, #1
    add x19, x19, #1
    b .parse_d
.chk_p:
    cmp w0, #0x2B
    b.ne .parse_d
    add x19, x19, #1
.parse_d:
    ldrb w0, [x19, #0]
    cmp w0, #0x30
    b.lo .zero
    cmp w0, #0x39
    b.hi .zero
    mov x0, x19
    bl sa_parse_num_lit_a64
    cbnz w2, .zero
    cbz w20, .done
    neg w0, w0
    b .done
.zero:
    mov w0, #0
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc sa_parse_primary_a64
;; @abi win_arm64
;; @in x0 = cursor
;; @out w0 = value, x1 = advanced cursor, w2 = 0 or ERR_*
;; @err w2 != 0 on syntax/div0/invalid-number error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Parses unary `+ - ! ~`, `( expr )`, `'c'` char literals, numbers, bare variables, and assignments!
@proc sa_parse_primary_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp

    bl sa_skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cbz w0, .syn_err

    cmp w0, #0x2B
    b.eq .un_plus
    cmp w0, #0x2D
    b.eq .un_minus
    cmp w0, #0x21
    b.eq .un_not
    cmp w0, #0x7E
    b.eq .un_bitnot
    cmp w0, #0x28
    b.eq .paren_grp
    cmp w0, #0x27
    b.eq .char_lit

    cmp w0, #0x30
    b.lo .chk_ident
    cmp w0, #0x39
    b.ls .num_lit

.chk_ident:
    bl sa_is_ident_char_a64
    cbz w0, .syn_err

    mov x20, x19
.id_lp:
    ldrb w0, [x20, #0]
    bl sa_is_ident_char_a64
    cbz w0, .id_end
    add x20, x20, #1
    b .id_lp
.id_end:
    sub x21, x20, x19
    add x0, x21, #2
    bl arena_alloc_a64
    cbz x0, .syn_err
    mov x22, x0 ; var name
    mov x1, x19
    mov x2, x21
    bl mem_copy_a64
    strb wzr, [x0, #0]

    mov x0, x20
    bl sa_check_assign_op_a64
    cbz w0, .just_var
    mov w21, w0 ; assign op (1..11)
    mov x0, x1 ; cursor after op
    mov w1, #2 ; right-associative assignment prec = 2
    bl sa_parse_prec_a64
    cbnz w2, .done
    mov w23, w0 ; rhs value
    mov x20, x1 ; new cursor

    cmp w21, #1
    b.eq .store_var
    mov x0, x22
    bl sa_read_var_i32_a64
    mov w1, w0 ; current var lhs
    mov w0, w21
    mov w2, w23
    bl sa_apply_binop_a64
    cbnz w1, .err_w1
    mov w23, w0

.store_var:
    mov w0, w23
    mov x1, sp
    bl fmt_i32_a64
    mov x0, x22
    mov x1, sp
    bl env_set_a64
    mov w0, w23
    mov x1, x20
    mov w2, #0
    b .done

.just_var:
    mov x0, x22
    bl sa_read_var_i32_a64
    mov x1, x20
    mov w2, #0
    b .done

.un_plus:
    add x0, x19, #1
    bl sa_parse_primary_a64
    b .done
.un_minus:
    add x0, x19, #1
    bl sa_parse_primary_a64
    cbnz w2, .done
    neg w0, w0
    b .done
.un_not:
    add x0, x19, #1
    bl sa_parse_primary_a64
    cbnz w2, .done
    cmp w0, #0
    cset w0, eq
    b .done
.un_bitnot:
    add x0, x19, #1
    bl sa_parse_primary_a64
    cbnz w2, .done
    mvn w0, w0
    b .done

.paren_grp:
    add x0, x19, #1
    mov w1, #1
    bl sa_parse_prec_a64
    cbnz w2, .done
    mov w20, w0
    mov x0, x1
    bl sa_skip_ws_a64
    ldrb w1, [x0, #0]
    cmp w1, #0x29
    b.ne .unbal_paren
    add x1, x0, #1
    mov w0, w20
    mov w2, #0
    b .done

.char_lit:
    ldrb w0, [x19, #1]
    cbz w0, .syn_err
    add x1, x19, #2
    ldrb w2, [x1, #0]
    cmp w2, #0x27
    b.ne .no_close_sq
    add x1, x1, #1
.no_close_sq:
    mov w2, #0
    b .done

.num_lit:
    mov x0, x19
    bl sa_parse_num_lit_a64
    b .done

.err_w1:
    mov w0, #0
    mov w2, w1
    mov x1, x20
    b .done
.unbal_paren:
    mov w0, #0
    mov x1, x19
    mov_imm32 w2, ERR_SYNTAX_ERROR
    b .done
.syn_err:
    mov w0, #0
    mov x1, x19
    mov_imm32 w2, ERR_SYNTAX_ERROR
.done:
    add sp, sp, #32
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc sa_peek_infix_a64
;; @abi win_arm64
;; @in x0 = cursor
;; @out w0 = precedence (0 if none), w1 = op code (1=comma, 2..11=binop), x2 = cursor past operator
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Inspects the next binary operator and returns its Pratt precedence tier (1..8).
@proc sa_peek_infix_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    bl sa_skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    ldrb w1, [x19, #1]

    cmp w0, #0x2C
    b.eq .comma

    cmp w1, #0x3D
    b.eq .no_op

    cmp w0, #0x7C
    b.eq .bit_or
    cmp w0, #0x5E
    b.eq .bit_xor
    cmp w0, #0x26
    b.eq .bit_and
    cmp w0, #0x2B
    b.eq .add_op
    cmp w0, #0x2D
    b.eq .sub_op
    cmp w0, #0x2A
    b.eq .mul_op
    cmp w0, #0x2F
    b.eq .div_op
    cmp w0, #0x25
    b.eq .mod_op

    cmp w0, #0x3C
    b.ne .chk_shr
    cmp w1, #0x3C
    b.ne .no_op
    ldrb w2, [x19, #2]
    cmp w2, #0x3D
    b.eq .no_op
    mov w0, #6
    mov w1, #10
    add x2, x19, #2
    b .exit

.chk_shr:
    cmp w0, #0x3E
    b.ne .no_op
    cmp w1, #0x3E
    b.ne .no_op
    ldrb w2, [x19, #2]
    cmp w2, #0x3D
    b.eq .no_op
    mov w0, #6
    mov w1, #11
    add x2, x19, #2
    b .exit

.comma:
    mov w0, #1
    mov w1, #1
    add x2, x19, #1
    b .exit
.bit_or:
    mov w0, #3
    mov w1, #9
    add x2, x19, #1
    b .exit
.bit_xor:
    mov w0, #4
    mov w1, #8
    add x2, x19, #1
    b .exit
.bit_and:
    mov w0, #5
    mov w1, #7
    add x2, x19, #1
    b .exit
.add_op:
    mov w0, #7
    mov w1, #5
    add x2, x19, #1
    b .exit
.sub_op:
    mov w0, #7
    mov w1, #6
    add x2, x19, #1
    b .exit
.mul_op:
    mov w0, #8
    mov w1, #2
    add x2, x19, #1
    b .exit
.div_op:
    mov w0, #8
    mov w1, #3
    add x2, x19, #1
    b .exit
.mod_op:
    mov w0, #8
    mov w1, #4
    add x2, x19, #1
    b .exit
.no_op:
    mov w0, #0
    mov w1, #0
    mov x2, x19
.exit:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc sa_parse_prec_a64
;; @abi win_arm64
;; @in x0 = cursor, w1 = min_prec (1..9)
;; @out w0 = i32 value, x1 = advanced cursor, w2 = 0 or ERR_*
;; @err w2 != 0 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Precedence-climbing Pratt expression evaluator for 11-tier `SET /A` expressions.
@proc sa_parse_prec_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov w20, w1 ; min_prec

    bl sa_parse_primary_a64
    cbnz w2, .done
    mov w19, w0 ; lhs
    mov x21, x1 ; cursor

.infix_lp:
    mov x0, x21
    bl sa_peek_infix_a64
    cmp w0, w20
    b.lo .ok_done
    mov w22, w0 ; op_prec
    mov w23, w1 ; op_code
    mov x0, x2 ; cursor past op
    add w1, w22, #1 ; left-associative next_min_prec
    bl sa_parse_prec_a64
    cbnz w2, .done
    mov x21, x1
    cmp w23, #1 ; comma returns rhs
    b.ne .apply_op
    mov w19, w0
    b .infix_lp

.apply_op:
    mov w2, w0 ; rhs
    mov w1, w19 ; lhs
    mov w0, w23 ; op
    bl sa_apply_binop_a64
    cbnz w1, .err_op
    mov w19, w0
    b .infix_lp

.err_op:
    mov w0, #0
    mov w2, w1
    mov x1, x21
    b .done
.ok_done:
    mov w0, w19
    mov x1, x21
    mov w2, #0
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc eval_set_a_a64
;; @abi win_arm64
;; @in x0 = expression string
;; @out w0 = 32-bit signed result, w1 = 0 on success or ERR_*
;; @err w1 != 0 on syntax, invalid number, or divide-by-zero error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Evaluates a complete `SET /A` expression string and verifies no trailing garbage remains.
@proc eval_set_a_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp

    bl sa_skip_ws_a64
    ldrb w1, [x0, #0]
    cbz w1, .empty_err
    mov w1, #1
    bl sa_parse_prec_a64
    cbnz w2, .ret_err
    mov w19, w0
    mov x0, x1
    bl sa_skip_ws_a64
    ldrb w1, [x0, #0]
    cbnz w1, .empty_err
    mov w0, w19
    mov w1, #0
    b .exit

.ret_err:
    mov w1, w2
    mov w0, #0
    b .exit
.empty_err:
    mov w0, #0
    mov_imm32 w1, ERR_SYNTAX_ERROR
.exit:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
