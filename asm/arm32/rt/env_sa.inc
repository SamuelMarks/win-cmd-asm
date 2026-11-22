;; @file env_sa.inc
;; @isa armv4
;; @desc ARM32 WTF-8 Environment Manager, Dynamic Variables, 32-level SETLOCAL stack, and 11-tier SET /A evaluator.

@cpu armv4
@format inc

;; @proc env_key_match_a32
;; @abi arm_ce
;; @in r0 = entry pointer ("KEY=VAL"), r1 = search key ("KEY" or "=C:")
;; @out r0 = pointer to value right after `=` if matched, else 0
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Case-insensitively compares an environment block entry key against r1, supporting hidden `=C:` keys.
@proc env_key_match_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1

    ; Hidden drive keys start with `=`: match leading `=` literally first
    ldrb r0, [r5, #0]
    cmp r0, #0x3D ; '='
    bne .cmp_lp
    ldrb r1, [r4, #0]
    cmp r1, #0x3D
    bne .no_match
    add r4, r4, #1
    add r5, r5, #1

.cmp_lp:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .chk_eq
    ldrb r1, [r4, #0]
    cmp r1, #0
    beq .no_match
    cmp r1, #0x3D
    beq .no_match
    bl ascii_upper_a32
    mov r2, r0
    mov r0, r1
    bl ascii_upper_a32
    cmp r0, r2
    bne .no_match
    add r4, r4, #1
    add r5, r5, #1
    b .cmp_lp

.chk_eq:
    ldrb r0, [r4, #0]
    cmp r0, #0x3D
    bne .no_match
    add r0, r4, #1
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr

.no_match:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc env_get_raw_a32
;; @abi arm_ce
;; @in r0 = variable name (NUL-terminated WTF-8)
;; @out r0 = direct pointer into pEnvBlock value (or 0 if not set)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Searches ARM32_CTX.pEnvBlock (`KEY=VAL\0...\0\0`) case-insensitively.
@proc env_get_raw_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    cmp r4, #0
    beq .not_found
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .not_found

    bl hal_get_ctx
    ldr r5, [r0, #ARM32_CTX.pEnvBlock]
    cmp r5, #0
    beq .not_found

.entry_lp:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .not_found
    mov r0, r5
    mov r1, r4
    bl env_key_match_a32
    cmp r0, #0
    bne .found
    ; Advance r5 past trailing NUL of current entry
.skip_nul:
    ldrb r0, [r5, #0]
    add r5, r5, #1
    cmp r0, #0
    bne .skip_nul
    b .entry_lp

.not_found:
    mov r0, #0
.found:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc fmt_hex8_a32
;; @abi arm_ce
;; @in r0 = 32-bit unsigned value, r1 = 9-byte output buffer
;; @out Writes 8 uppercase hex digits + NUL into r1
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Formats dwLastErrorLevel as 8 uppercase hex digits for `%=ExitCode%`.
@proc fmt_hex8_a32
    mov r2, #28
.hex_lp:
    lsr r3, r0, r2
    and r3, r3, #0x0F
    cmp r3, #10
    bcc .dig
    add r3, r3, #0x37 ; 'A' - 10
    b .st
.dig:
    add r3, r3, #0x30
.st:
    strb r3, [r1, #0]
    add r1, r1, #1
    subs r2, r2, #4
    bge .hex_lp
    mov r3, #0
    strb r3, [r1, #0]
    bx lr
@endproc

;; @proc fmt_dyn_date_time_a32
;; @abi arm_ce
;; @in r0 = mode (0 = DATE "YYYY-MM-DD", 1 = TIME "HH:MM:SS.CC" with leading space if hour < 10)
;; @out r0 = arena string
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Synthesizes dynamic `%DATE%` or `%TIME%` from hal_get_local_time.
@proc fmt_dyn_date_time_a32
    push {r4, r5, r6, lr}
    sub sp, sp, #24 ; SYSTEMTIME (16B) at sp+0
    mov r4, r0
    mov r0, sp
    bl hal_get_local_time
    mov r0, #32
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r5, r0
    mov r6, r0

    cmp r4, #0
    bne .do_time

    ; Format YYYY-MM-DD
    ldrh r0, [sp, #0] ; wYear
    mov r1, #100
    bl math_udiv32
    mov r4, r1
    mov r1, r6
    bl fmt_2dig_a32
    mov r6, r0
    mov r0, r4
    mov r1, r6
    bl fmt_2dig_a32
    mov r6, r0
    mov r1, #0x2D
    strb r1, [r6, #0]
    add r6, r6, #1
    ldrh r0, [sp, #2] ; wMonth
    mov r1, r6
    bl fmt_2dig_a32
    mov r6, r0
    mov r1, #0x2D
    strb r1, [r6, #0]
    add r6, r6, #1
    ldrh r0, [sp, #6] ; wDay
    mov r1, r6
    bl fmt_2dig_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r5
    b .done

.do_time:
    ; Format HH:MM:SS.CC (leading space if wHour < 10)
    ldrh r0, [sp, #8] ; wHour
    cmp r0, #10
    bcs .hr_2dig
    mov r1, #0x20
    strb r1, [r6, #0]
    add r0, r0, #0x30
    strb r0, [r6, #1]
    add r6, r6, #2
    b .after_hr
.hr_2dig:
    mov r1, r6
    bl fmt_2dig_a32
    mov r6, r0
.after_hr:
    mov r1, #0x3A
    strb r1, [r6, #0]
    add r6, r6, #1
    ldrh r0, [sp, #10] ; wMinute
    mov r1, r6
    bl fmt_2dig_a32
    mov r6, r0
    mov r1, #0x3A
    strb r1, [r6, #0]
    add r6, r6, #1
    ldrh r0, [sp, #12] ; wSecond
    mov r1, r6
    bl fmt_2dig_a32
    mov r6, r0
    mov r1, #0x2E
    strb r1, [r6, #0]
    add r6, r6, #1
    ldrh r0, [sp, #14] ; wMilliseconds
    mov r1, #10
    bl math_udiv32
    mov r1, r6
    bl fmt_2dig_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r5

.done:
    add sp, sp, #24
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc env_get_a32
;; @abi arm_ce
;; @in r0 = variable name (NUL-terminated WTF-8)
;; @out r0 = arena string value (or 0 if undefined)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Looks up an explicit environment variable or synthesizes dynamic `%CD%`, `%ERRORLEVEL%`, `%RANDOM%`, `%DATE%`, `%TIME%`, `%=ExitCode%`, `%=A:%`..`%=Z:%`.
@proc env_get_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    bl env_get_raw_a32
    cmp r0, #0
    beq .try_dyn
    bl arena_strdup_a32
    pop {r4, r5, r6, lr}
    bx lr

.try_dyn:
    bl hal_get_ctx
    mov r5, r0
    ldrb r0, [r5, #ARM32_CTX.bExtensions]
    cmp r0, #0
    beq .chk_hidden_drv

    ; Check dynamic variable names
    adr_sym r1, szDynCd
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_cd

    adr_sym r1, szDynErrLvl
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_errlvl

    adr_sym r1, szDynExtVer
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_extver

    adr_sym r1, szDynCmdLine
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_cmdline

    adr_sym r1, szDynRandom
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_random

    adr_sym r1, szDynDate
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_date

    adr_sym r1, szDynTime
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_time

    adr_sym r1, szDynNuma
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_numa

    adr_sym r1, szDynExitCode
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_exit_hex

    adr_sym r1, szDynExitAscii
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .dyn_exit_asc

.chk_hidden_drv:
    ; Check `=A:` .. `=Z:` per-drive CWD
    ldrb r0, [r4, #0]
    cmp r0, #0x3D
    bne .not_def
    ldrb r0, [r4, #1]
    bl ascii_upper_a32
    cmp r0, #0x41
    bcc .not_def
    cmp r0, #0x5A
    bhi .not_def
    ldrb r1, [r4, #2]
    cmp r1, #0x3A
    bne .not_def
    ldrb r1, [r4, #3]
    cmp r1, #0
    bne .not_def
    sub r0, r0, #0x41
    lsl r0, r0, #2
    mov_imm32 r1, ARM32_CTX.drvCwdsPtrs
    add r1, r5, r1
    ldr r0, [r1, r0]
    cmp r0, #0
    beq .not_def
    bl arena_strdup_a32
    pop {r4, r5, r6, lr}
    bx lr

.dyn_cd:
    mov_imm32 r0, ARM32_CTX.szCwd
    add r0, r5, r0
    bl arena_strdup_a32
    pop {r4, r5, r6, lr}
    bx lr

.dyn_errlvl:
    mov r0, #24
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_def
    mov r6, r0
    ldr r0, [r5, #ARM32_CTX.dwLastErrorLevel]
    mov r1, r6
    bl fmt_i32_a32
    mov r0, r6
    pop {r4, r5, r6, lr}
    bx lr

.dyn_extver:
    mov r0, #4
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_def
    mov r1, #0x32 ; '2'
    strb r1, [r0, #0]
    mov r1, #0
    strb r1, [r0, #1]
    pop {r4, r5, r6, lr}
    bx lr

.dyn_cmdline:
    ldr r0, [r5, #ARM32_CTX.pCmdLineRaw]
    cmp r0, #0
    beq .not_def
    bl arena_strdup_a32
    pop {r4, r5, r6, lr}
    bx lr

.dyn_random:
    ldr r0, [r5, #ARM32_CTX.dwRandomSeed]
    mov_imm32 r1, 214013
    mul r2, r0, r1
    mov_imm32 r1, 2531011
    add r2, r2, r1
    str r2, [r5, #ARM32_CTX.dwRandomSeed]
    lsr r6, r2, #16
    mov_imm32 r1, 0x7FFF
    and r6, r6, r1
    mov r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_def
    mov r4, r0
    mov r0, r6
    mov r1, r4
    bl fmt_i32_a32
    mov r0, r4
    pop {r4, r5, r6, lr}
    bx lr

.dyn_date:
    mov r0, #0
    bl fmt_dyn_date_time_a32
    pop {r4, r5, r6, lr}
    bx lr

.dyn_time:
    mov r0, #1
    bl fmt_dyn_date_time_a32
    pop {r4, r5, r6, lr}
    bx lr

.dyn_numa:
    mov r0, #4
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_def
    mov r1, #0x30 ; '0'
    strb r1, [r0, #0]
    mov r1, #0
    strb r1, [r0, #1]
    pop {r4, r5, r6, lr}
    bx lr

.dyn_exit_hex:
    mov r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_def
    mov r6, r0
    ldr r0, [r5, #ARM32_CTX.dwLastErrorLevel]
    mov r1, r6
    bl fmt_hex8_a32
    mov r0, r6
    pop {r4, r5, r6, lr}
    bx lr

.dyn_exit_asc:
    ldr r6, [r5, #ARM32_CTX.dwLastErrorLevel]
    and r6, r6, #0xFF
    cmp r6, #0x20
    bcc .not_def
    cmp r6, #0x7E
    bhi .not_def
    mov r0, #4
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_def
    strb r6, [r0, #0]
    mov r1, #0
    strb r1, [r0, #1]
    pop {r4, r5, r6, lr}
    bx lr

.not_def:
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc env_unset_a32
;; @abi arm_ce
;; @in r0 = variable name
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Removes any existing `NAME=...` entry from ARM32_CTX.pEnvBlock.
@proc env_unset_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0
    bl hal_get_ctx
    mov r5, r0
    ldr r6, [r5, #ARM32_CTX.pEnvBlock]
    cmp r6, #0
    beq .done

.scan_lp:
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .done
    mov r0, r6
    mov r1, r4
    bl env_key_match_a32
    cmp r0, #0
    bne .remove_it

.next_e:
    ldrb r0, [r6, #0]
    add r6, r6, #1
    cmp r0, #0
    bne .next_e
    b .scan_lp

.remove_it:
    mov r7, r6
.find_end:
    ldrb r0, [r7, #0]
    add r7, r7, #1
    cmp r0, #0
    bne .find_end
    ; Shift [r7 .. pEnvBlock + dwEnvLen] down to r6
    ldr r0, [r5, #ARM32_CTX.pEnvBlock]
    ldr r1, [r5, #ARM32_CTX.dwEnvLen]
    add r2, r0, r1
    sub r2, r2, r7
    sub r3, r7, r6
    sub r1, r1, r3
    str r1, [r5, #ARM32_CTX.dwEnvLen]
    mov r0, r6
    mov r1, r7
    bl mem_copy_a32
    b .scan_lp

.done:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc env_set_a32
;; @abi arm_ce
;; @in r0 = variable name, r1 = value string (NULL or empty unsets variable!)
;; @out r0 = 0 on success or ERR_OUT_OF_MEMORY
;; @err r0 = ERR_OUT_OF_MEMORY if 64 KiB block is full
;; @clobbers r0, r1, r2, r3, lr
;; @desc Sets or deletes `NAME=VALUE` in ARM32_CTX.pEnvBlock.
@proc env_set_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0
    mov r5, r1
    bl env_unset_a32
    cmp r5, #0
    beq .ok
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .ok

    mov r0, r4
    bl str_len_a32
    mov r6, r0
    mov r0, r5
    bl str_len_a32
    mov r7, r0

    bl hal_get_ctx
    mov r8, r0
    ldr r0, [r8, #ARM32_CTX.dwEnvLen]
    cmp r0, #2
    bcs .has_len
    mov r0, #1
.has_len:
    ; New total = (dwEnvLen - 1) + key_len + 1 + val_len + 2
    sub r1, r0, #1
    add r2, r1, r6
    add r2, r2, r7
    add r2, r2, #3
    ldr r3, [r8, #ARM32_CTX.dwEnvCap]
    cmp r2, r3
    bhi .oom

    ldr r0, [r8, #ARM32_CTX.pEnvBlock]
    add r0, r0, r1
    mov r1, r4
    str r2, [r8, #ARM32_CTX.dwEnvLen]
    bl str_copy_a32
    mov r1, #0x3D ; '='
    strb r1, [r0, #0]
    add r0, r0, #1
    mov r1, r5
    bl str_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    strb r1, [r0, #1]

.ok:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
.oom:
    mov r0, #ERR_OUT_OF_MEMORY
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc setlocal_push_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv (for optional EnableDelayedExpansion / EnableExtensions flags)
;; @out r0 = 0 on success or ERR_OUT_OF_MEMORY
;; @err r0 != 0 on stack overflow or OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Pushes a SETLOCAL snapshot (up to 32 levels) and applies optional extension/delayed-expansion switches.
@proc setlocal_push_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r6, r0 ; argc
    mov r7, r1 ; argv

    bl hal_get_ctx
    mov r4, r0
    ldr r5, [r4, #ARM32_CTX.dwSetlocalDepth]
    cmp r5, #A32_SETLOCAL_MAX
    bcs .oom

    ldr r0, [r4, #ARM32_CTX.pSetlocalStack]
    lsl r1, r5, #4 ; * 16 (sizeof SETLOCAL_SNAP_A32)
    add r8, r0, r1

    ldr r0, [r4, #ARM32_CTX.dwEnvLen]
    add r0, r0, #8
    bl perm_alloc_a32
    cmp r0, #0
    beq .oom
    str r0, [r8, #SETLOCAL_SNAP_A32.pEnvCopy]
    ldr r2, [r4, #ARM32_CTX.dwEnvLen]
    str r2, [r8, #SETLOCAL_SNAP_A32.dwEnvLen]
    ldr r1, [r4, #ARM32_CTX.pEnvBlock]
    bl mem_copy_a32

    mov_imm32 r0, 512
    bl perm_alloc_a32
    cmp r0, #0
    beq .oom
    str r0, [r8, #SETLOCAL_SNAP_A32.pCwdCopy]
    mov_imm32 r1, ARM32_CTX.szCwd
    add r1, r4, r1
    bl str_copy_a32

    ldrb r0, [r4, #ARM32_CTX.bExtensions]
    strb r0, [r8, #SETLOCAL_SNAP_A32.bExtFlag]
    ldrb r0, [r4, #ARM32_CTX.bDelayedExp]
    strb r0, [r8, #SETLOCAL_SNAP_A32.bDelayedFlag]

    add r5, r5, #1
    str r5, [r4, #ARM32_CTX.dwSetlocalDepth]

    ; Parse optional arguments argv[1..argc]
    mov r5, #1
.arg_lp:
    cmp r5, r6
    bcs .done_ok
    lsl r0, r5, #2
    ldr r0, [r7, r0]
    bl strip_outer_quotes_a32
    mov r8, r0

    adr_sym r1, szEnableDelExp
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    bne .chk_dis_del
    mov r0, #1
    strb r0, [r4, #ARM32_CTX.bDelayedExp]
    b .nx_arg
.chk_dis_del:
    mov r0, r8
    adr_sym r1, szDisableDelExp
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    bne .chk_en_ext
    mov r0, #0
    strb r0, [r4, #ARM32_CTX.bDelayedExp]
    b .nx_arg
.chk_en_ext:
    mov r0, r8
    adr_sym r1, szEnableExt
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    bne .chk_dis_ext
    mov r0, #1
    strb r0, [r4, #ARM32_CTX.bExtensions]
    b .nx_arg
.chk_dis_ext:
    mov r0, r8
    adr_sym r1, szDisableExt
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    bne .nx_arg
    mov r0, #0
    strb r0, [r4, #ARM32_CTX.bExtensions]
.nx_arg:
    add r5, r5, #1
    b .arg_lp

.done_ok:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
.oom:
    mov r0, #ERR_OUT_OF_MEMORY
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc endlocal_pop_a32
;; @abi arm_ce
;; @in none
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Pops the top SETLOCAL snapshot if dwSetlocalDepth > 0, restoring environment, CWD, and flags.
@proc endlocal_pop_a32
    push {r4, r5, r6, lr}
    bl hal_get_ctx
    mov r4, r0
    ldr r5, [r4, #ARM32_CTX.dwSetlocalDepth]
    cmp r5, #0
    beq .done
    sub r5, r5, #1
    str r5, [r4, #ARM32_CTX.dwSetlocalDepth]

    ldr r0, [r4, #ARM32_CTX.pSetlocalStack]
    lsl r1, r5, #4
    add r6, r0, r1

    ldr r0, [r4, #ARM32_CTX.pEnvBlock]
    ldr r1, [r6, #SETLOCAL_SNAP_A32.pEnvCopy]
    ldr r2, [r6, #SETLOCAL_SNAP_A32.dwEnvLen]
    str r2, [r4, #ARM32_CTX.dwEnvLen]
    bl mem_copy_a32

    ldr r0, [r6, #SETLOCAL_SNAP_A32.pCwdCopy]
    bl set_drv_cwd_a32

    ldrb r0, [r6, #SETLOCAL_SNAP_A32.bExtFlag]
    strb r0, [r4, #ARM32_CTX.bExtensions]
    ldrb r0, [r6, #SETLOCAL_SNAP_A32.bDelayedFlag]
    strb r0, [r4, #ARM32_CTX.bDelayedExp]

.done:
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc sa_skip_ws_a32
;; @abi arm_ce
;; @in r0 = cursor
;; @out r0 = cursor past spaces, tabs, and quotes
;; @err none
;; @clobbers r0, r1
;; @desc Skips whitespace and double-quote characters inside `SET /A` expressions.
@proc sa_skip_ws_a32
.lp:
    ldrb r1, [r0, #0]
    cmp r1, #0x20
    beq .adv
    cmp r1, #0x09
    beq .adv
    cmp r1, #0x22
    beq .adv
    bx lr
.adv:
    add r0, r0, #1
    b .lp
@endproc

;; @proc sa_parse_num_lit_a32
;; @abi arm_ce
;; @in r0 = cursor starting at digit `'0'..'9'`
;; @out r0 = 32-bit value, r1 = advanced cursor, r2 = 0 or ERR_INVALID_NUMBER
;; @err r2 = ERR_INVALID_NUMBER on invalid octal (`08`/`09`) or malformed hex (`0x`)
;; @clobbers r0, r1, r2, r3, lr
;; @desc Parses decimal, `0x` hexadecimal, or `0` octal integer literals with strict `08`/`09` rejection.
@proc sa_parse_num_lit_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    mov r5, #0 ; accumulator

    ldrb r0, [r4, #0]
    cmp r0, #0x30 ; '0'
    bne .dec_lp
    ldrb r1, [r4, #1]
    cmp r1, #0x78 ; 'x'
    beq .hex_start
    cmp r1, #0x58 ; 'X'
    beq .hex_start
    ; Check if followed by digit/alpha -> octal!
    cmp r1, #0x30
    bcc .single_zero
    cmp r1, #0x39
    bls .oct_start
    b .single_zero

.single_zero:
    add r1, r4, #1
    mov r0, #0
    mov r2, #0
    pop {r4, r5, r6, lr}
    bx lr

.hex_start:
    add r4, r4, #2
    mov r6, #0 ; count
.hex_lp:
    ldrb r0, [r4, #0]
    cmp r0, #0x30
    bcc .hex_end
    cmp r0, #0x39
    bls .hex_dig
    bl ascii_upper_a32
    cmp r0, #0x41
    bcc .hex_end
    cmp r0, #0x46
    bhi .hex_end
    sub r0, r0, #0x37
    b .hex_acc
.hex_dig:
    sub r0, r0, #0x30
.hex_acc:
    lsl r5, r5, #4
    orr r5, r5, r0
    add r4, r4, #1
    add r6, r6, #1
    b .hex_lp
.hex_end:
    cmp r6, #0
    beq .inv_num
    b .chk_trail_alpha

.oct_start:
    add r4, r4, #1
.oct_lp:
    ldrb r0, [r4, #0]
    cmp r0, #0x30
    bcc .chk_trail_alpha
    cmp r0, #0x37
    bls .oct_dig
    cmp r0, #0x39
    bls .inv_num ; 08 or 09 -> ERR_INVALID_NUMBER!
    b .chk_trail_alpha
.oct_dig:
    sub r0, r0, #0x30
    lsl r5, r5, #3
    orr r5, r5, r0
    add r4, r4, #1
    b .oct_lp

.dec_lp:
    ldrb r0, [r4, #0]
    cmp r0, #0x30
    bcc .chk_trail_alpha
    cmp r0, #0x39
    bhi .chk_trail_alpha
    sub r0, r0, #0x30
    mov r1, #10
    mul r2, r5, r1
    add r5, r2, r0
    add r4, r4, #1
    b .dec_lp

.chk_trail_alpha:
    ldrb r0, [r4, #0]
    bl ascii_upper_a32
    cmp r0, #0x41
    bcc .ok_lit
    cmp r0, #0x5A
    bls .inv_num
    cmp r0, #0x5F
    beq .inv_num
.ok_lit:
    mov r0, r5
    mov r1, r4
    mov r2, #0
    pop {r4, r5, r6, lr}
    bx lr

.inv_num:
    mov r0, #0
    mov r1, r4
    mov_imm32 r2, ERR_INVALID_NUMBER
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc sa_is_ident_char_a32
;; @abi arm_ce
;; @in r0 = byte char
;; @out r0 = 1 if valid SET /A identifier char, else 0
;; @err none
;; @clobbers r0, r1
;; @desc Checks if r0 can belong to a bare variable identifier inside `SET /A`.
@proc sa_is_ident_char_a32
    cmp r0, #0x30
    bcc .chk_special
    cmp r0, #0x39
    bls .yes
    cmp r0, #0x41
    bcc .chk_special
    cmp r0, #0x5A
    bls .yes
    cmp r0, #0x61
    bcc .chk_under
    cmp r0, #0x7A
    bls .yes
.chk_under:
    cmp r0, #0x5F ; '_'
    beq .yes
    cmp r0, #0x80
    bcs .yes
.chk_special:
    cmp r0, #0x24 ; '$'
    beq .yes
    cmp r0, #0x23 ; '#'
    beq .yes
    cmp r0, #0x40 ; '@'
    beq .yes
    mov r0, #0
    bx lr
.yes:
    mov r0, #1
    bx lr
@endproc

;; @proc sa_check_assign_op_a32
;; @abi arm_ce
;; @in r0 = cursor after identifier
;; @out r0 = assign op code (0=none, 1:`=`, 2:`*=`, 3:`/=`, 4:`%=`, 5:`+=`, 6:`-=`, 7:`&=`, 8:`^=`, 9:`|=`, 10:`<<=`, 11:`>>=`), r1 = cursor past op
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Checks if the next token after a variable name in `SET /A` is an assignment operator (excluding `==`).
@proc sa_check_assign_op_a32
    push {r4, lr}
    bl sa_skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    ldrb r1, [r4, #1]

    cmp r0, #0x3D ; '='
    bne .chk_two
    cmp r1, #0x3D ; '==' is NOT assignment
    beq .none
    mov r0, #1
    add r1, r4, #1
    pop {r4, lr}
    bx lr

.chk_two:
    cmp r1, #0x3D
    bne .chk_shift_asgn
    add r1, r4, #2
    cmp r0, #0x2A ; '*='
    beq .op_mul
    cmp r0, #0x2F ; '/='
    beq .op_div
    cmp r0, #0x25 ; '%='
    beq .op_mod
    cmp r0, #0x2B ; '+='
    beq .op_add
    cmp r0, #0x2D ; '-='
    beq .op_sub
    cmp r0, #0x26 ; '&='
    beq .op_and
    cmp r0, #0x5E ; '^='
    beq .op_xor
    cmp r0, #0x7C ; '|='
    beq .op_or
    b .none

.chk_shift_asgn:
    ldrb r2, [r4, #2]
    cmp r2, #0x3D
    bne .none
    cmp r0, #0x3C
    bne .chk_shr_a
    cmp r1, #0x3C
    bne .none
    mov r0, #10
    add r1, r4, #3
    pop {r4, lr}
    bx lr
.chk_shr_a:
    cmp r0, #0x3E
    bne .none
    cmp r1, #0x3E
    bne .none
    mov r0, #11
    add r1, r4, #3
    pop {r4, lr}
    bx lr

.op_mul:
    mov r0, #2
    pop {r4, lr}
    bx lr
.op_div:
    mov r0, #3
    pop {r4, lr}
    bx lr
.op_mod:
    mov r0, #4
    pop {r4, lr}
    bx lr
.op_add:
    mov r0, #5
    pop {r4, lr}
    bx lr
.op_sub:
    mov r0, #6
    pop {r4, lr}
    bx lr
.op_and:
    mov r0, #7
    pop {r4, lr}
    bx lr
.op_xor:
    mov r0, #8
    pop {r4, lr}
    bx lr
.op_or:
    mov r0, #9
    pop {r4, lr}
    bx lr
.none:
    mov r0, #0
    mov r1, r4
    pop {r4, lr}
    bx lr
@endproc

;; @proc sa_apply_binop_a32
;; @abi arm_ce
;; @in r0 = op (2..11 matching `*=..>>=`), r1 = lhs, r2 = rhs
;; @out r0 = result, r1 = 0 or ERR_DIV_BY_ZERO
;; @err r1 = ERR_DIV_BY_ZERO on `/ 0` or `% 0`
;; @clobbers r0, r1, r2, r3, lr
;; @desc Evaluates a 32-bit wrapping binary operator with trap-safe division and `& 31` shift masking.
@proc sa_apply_binop_a32
    push {r4, lr}
    cmp r0, #2
    beq .do_mul
    cmp r0, #3
    beq .do_div
    cmp r0, #4
    beq .do_mod
    cmp r0, #5
    beq .do_add
    cmp r0, #6
    beq .do_sub
    cmp r0, #7
    beq .do_and
    cmp r0, #8
    beq .do_xor
    cmp r0, #9
    beq .do_or
    cmp r0, #10
    beq .do_shl
    ; op == 11 (`>>`)
    and r2, r2, #31
    asr r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
.do_shl:
    and r2, r2, #31
    lsl r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
.do_mul:
    mul r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
.do_div:
    mov r0, r1
    mov r1, r2
    bl math_sdiv32
    mov r1, r2
    pop {r4, lr}
    bx lr
.do_mod:
    mov r0, r1
    mov r1, r2
    bl math_sdiv32
    mov r0, r1
    mov r1, r2
    pop {r4, lr}
    bx lr
.do_add:
    add r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
.do_sub:
    sub r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
.do_and:
    and r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
.do_xor:
    eor r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
.do_or:
    orr r0, r1, r2
    mov r1, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc sa_read_var_i32_a32
;; @abi arm_ce
;; @in r0 = variable name string
;; @out r0 = parsed signed 32-bit value (0 if undefined or empty)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reads a variable from the environment and parses its numeric value for `SET /A` (defaulting to 0).
@proc sa_read_var_i32_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    bl env_get_a32
    cmp r0, #0
    beq .zero
    bl sa_skip_ws_a32
    mov r4, r0
    mov r5, #0 ; neg
    ldrb r0, [r4, #0]
    cmp r0, #0x2D
    bne .chk_p
    mov r5, #1
    add r4, r4, #1
    b .parse_d
.chk_p:
    cmp r0, #0x2B
    bne .parse_d
    add r4, r4, #1
.parse_d:
    ldrb r0, [r4, #0]
    cmp r0, #0x30
    bcc .zero
    cmp r0, #0x39
    bhi .zero
    mov r0, r4
    bl sa_parse_num_lit_a32
    cmp r2, #0
    bne .zero
    cmp r5, #0
    beq .done
    mov r1, #0
    sub r0, r1, r0
    b .done
.zero:
    mov r0, #0
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc sa_parse_primary_a32
;; @abi arm_ce
;; @in r0 = cursor
;; @out r0 = value, r1 = advanced cursor, r2 = 0 or ERR_*
;; @err r2 != 0 on syntax/div0/invalid-number error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Parses unary `+ - ! ~`, `( expr )`, `'c'` char literals, numbers, bare variables, and assignments!
@proc sa_parse_primary_a32
    push {r4, r5, r6, r7, r8, lr}
    sub sp, sp, #24
    bl sa_skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .syn_err

    ; Unary operators `+`, `-`, `!`, `~`
    cmp r0, #0x2B ; '+'
    beq .un_plus
    cmp r0, #0x2D ; '-'
    beq .un_minus
    cmp r0, #0x21 ; '!'
    beq .un_not
    cmp r0, #0x7E ; '~'
    beq .un_bitnot
    cmp r0, #0x28 ; '('
    beq .paren_grp
    cmp r0, #0x27 ; ''' char literal
    beq .char_lit

    ; Digit literal?
    cmp r0, #0x30
    bcc .chk_ident
    cmp r0, #0x39
    bls .num_lit

.chk_ident:
    bl sa_is_ident_char_a32
    cmp r0, #0
    beq .syn_err

    ; Scan identifier [r4 .. r5)
    mov r5, r4
.id_lp:
    ldrb r0, [r5, #0]
    bl sa_is_ident_char_a32
    cmp r0, #0
    beq .id_end
    add r5, r5, #1
    b .id_lp
.id_end:
    sub r6, r5, r4
    add r0, r6, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .syn_err
    mov r7, r0 ; var name
    mov r1, r4
    mov r2, r6
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]

    ; Check if followed by assignment operator!
    mov r0, r5
    bl sa_check_assign_op_a32
    cmp r0, #0
    beq .just_var
    mov r6, r0 ; assign op (1..11)
    mov r0, r1 ; cursor after op
    mov r1, #2 ; right-associative assignment precedence = 2
    bl sa_parse_prec_a32
    cmp r2, #0
    bne .done
    mov r8, r0 ; rhs value
    mov r5, r1 ; new cursor

    cmp r6, #1 ; plain `=`
    beq .store_var
    mov r0, r7
    bl sa_read_var_i32_a32
    mov r1, r0 ; current var lhs
    mov r0, r6
    mov r2, r8
    bl sa_apply_binop_a32
    cmp r1, #0
    bne .err_r1
    mov r8, r0

.store_var:
    mov r0, r8
    mov r1, sp
    bl fmt_i32_a32
    mov r0, r7
    mov r1, sp
    bl env_set_a32
    mov r0, r8
    mov r1, r5
    mov r2, #0
    b .done

.just_var:
    mov r0, r7
    bl sa_read_var_i32_a32
    mov r1, r5
    mov r2, #0
    b .done

.un_plus:
    add r0, r4, #1
    bl sa_parse_primary_a32
    b .done
.un_minus:
    add r0, r4, #1
    bl sa_parse_primary_a32
    cmp r2, #0
    bne .done
    mov r3, #0
    sub r0, r3, r0
    b .done
.un_not:
    add r0, r4, #1
    bl sa_parse_primary_a32
    cmp r2, #0
    bne .done
    cmp r0, #0
    bne .un_not_z
    mov r0, #1
    b .done
.un_not_z:
    mov r0, #0
    b .done
.un_bitnot:
    add r0, r4, #1
    bl sa_parse_primary_a32
    cmp r2, #0
    bne .done
    mvn r0, r0
    b .done

.paren_grp:
    add r0, r4, #1
    mov r1, #1 ; comma precedence inside `(...)`
    bl sa_parse_prec_a32
    cmp r2, #0
    bne .done
    mov r5, r0
    mov r0, r1
    bl sa_skip_ws_a32
    ldrb r1, [r0, #0]
    cmp r1, #0x29 ; ')'
    bne .unbal_paren
    add r1, r0, #1
    mov r0, r5
    mov r2, #0
    b .done

.char_lit:
    ldrb r0, [r4, #1]
    cmp r0, #0
    beq .syn_err
    add r1, r4, #2
    ldrb r2, [r1, #0]
    cmp r2, #0x27
    bne .no_close_sq
    add r1, r1, #1
.no_close_sq:
    mov r2, #0
    b .done

.num_lit:
    mov r0, r4
    bl sa_parse_num_lit_a32
    b .done

.err_r1:
    mov r0, #0
    mov r2, r1
    mov r1, r5
    b .done
.unbal_paren:
    mov r0, #0
    mov r1, r4
    mov_imm32 r2, ERR_SYNTAX_ERROR
    b .done
.syn_err:
    mov r0, #0
    mov r1, r4
    mov_imm32 r2, ERR_SYNTAX_ERROR
.done:
    add sp, sp, #24
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc sa_peek_infix_a32
;; @abi arm_ce
;; @in r0 = cursor
;; @out r0 = precedence (0 if none), r1 = op code (1=comma, 2..11=binop), r2 = cursor past operator
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Inspects the next binary operator and returns its Pratt precedence tier (1..8).
@proc sa_peek_infix_a32
    push {r4, lr}
    bl sa_skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    ldrb r1, [r4, #1]

    cmp r0, #0x2C ; ','
    beq .comma

    ; Exclude compound assignments (`+=`, `<<=`, etc.) here
    cmp r1, #0x3D
    beq .no_op

    cmp r0, #0x7C ; '|'
    beq .bit_or
    cmp r0, #0x5E ; '^'
    beq .bit_xor
    cmp r0, #0x26 ; '&'
    beq .bit_and
    cmp r0, #0x2B ; '+'
    beq .add_op
    cmp r0, #0x2D ; '-'
    beq .sub_op
    cmp r0, #0x2A ; '*'
    beq .mul_op
    cmp r0, #0x2F ; '/'
    beq .div_op
    cmp r0, #0x25 ; '%'
    beq .mod_op

    cmp r0, #0x3C
    bne .chk_shr
    cmp r1, #0x3C
    bne .no_op
    ldrb r2, [r4, #2]
    cmp r2, #0x3D
    beq .no_op
    mov r0, #6
    mov r1, #10
    add r2, r4, #2
    pop {r4, lr}
    bx lr

.chk_shr:
    cmp r0, #0x3E
    bne .no_op
    cmp r1, #0x3E
    bne .no_op
    ldrb r2, [r4, #2]
    cmp r2, #0x3D
    beq .no_op
    mov r0, #6
    mov r1, #11
    add r2, r4, #2
    pop {r4, lr}
    bx lr

.comma:
    mov r0, #1
    mov r1, #1
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.bit_or:
    mov r0, #3
    mov r1, #9
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.bit_xor:
    mov r0, #4
    mov r1, #8
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.bit_and:
    mov r0, #5
    mov r1, #7
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.add_op:
    mov r0, #7
    mov r1, #5
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.sub_op:
    mov r0, #7
    mov r1, #6
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.mul_op:
    mov r0, #8
    mov r1, #2
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.div_op:
    mov r0, #8
    mov r1, #3
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.mod_op:
    mov r0, #8
    mov r1, #4
    add r2, r4, #1
    pop {r4, lr}
    bx lr
.no_op:
    mov r0, #0
    mov r1, #0
    mov r2, r4
    pop {r4, lr}
    bx lr
@endproc

;; @proc sa_parse_prec_a32
;; @abi arm_ce
;; @in r0 = cursor, r1 = min_prec (1..9)
;; @out r0 = i32 value, r1 = advanced cursor, r2 = 0 or ERR_*
;; @err r2 != 0 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Precedence-climbing Pratt expression evaluator for 11-tier `SET /A` expressions.
@proc sa_parse_prec_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r5, r1 ; min_prec
    bl sa_parse_primary_a32
    cmp r2, #0
    bne .done
    mov r4, r0 ; lhs
    mov r6, r1 ; cursor

.infix_lp:
    mov r0, r6
    bl sa_peek_infix_a32
    cmp r0, r5
    bcc .ok_done
    mov r7, r0 ; op_prec
    mov r8, r1 ; op_code
    mov r0, r2 ; cursor past op
    add r1, r7, #1 ; left-associative next_min_prec = prec + 1
    bl sa_parse_prec_a32
    cmp r2, #0
    bne .done
    mov r6, r1 ; updated cursor
    cmp r8, #1 ; comma operator returns rhs!
    bne .apply_op
    mov r4, r0
    b .infix_lp

.apply_op:
    mov r2, r0 ; rhs
    mov r1, r4 ; lhs
    mov r0, r8 ; op
    bl sa_apply_binop_a32
    cmp r1, #0
    bne .err_op
    mov r4, r0
    b .infix_lp

.err_op:
    mov r0, #0
    mov r2, r1
    mov r1, r6
    b .done
.ok_done:
    mov r0, r4
    mov r1, r6
    mov r2, #0
.done:
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc eval_set_a_a32
;; @abi arm_ce
;; @in r0 = expression string
;; @out r0 = 32-bit signed result, r1 = 0 on success or ERR_*
;; @err r1 != 0 on syntax, invalid number, or divide-by-zero error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Evaluates a complete `SET /A` expression string and verifies no trailing garbage remains.
@proc eval_set_a_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    bl sa_skip_ws_a32
    ldrb r1, [r0, #0]
    cmp r1, #0
    beq .empty_err
    mov r1, #1
    bl sa_parse_prec_a32
    cmp r2, #0
    bne .ret_err
    mov r4, r0
    mov r0, r1
    bl sa_skip_ws_a32
    ldrb r1, [r0, #0]
    cmp r1, #0
    bne .empty_err
    mov r0, r4
    mov r1, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.ret_err:
    mov r1, r2
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.empty_err:
    mov r0, #0
    mov_imm32 r1, ERR_SYNTAX_ERROR
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc
