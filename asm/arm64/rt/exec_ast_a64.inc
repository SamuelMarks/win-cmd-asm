;; @file exec_ast_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 AST Executor, Concurrent/Spooled Pipelines, Batch/:label Engine, PATHEXT/AppExecLink/vm8086 Spawner, and Shell Main.

@cpu arm64
@format inc

;; @proc make_spool_path_a64
;; @abi win_arm64
;; @in none
;; @out x0 = unique WTF-8 temporary spool file path in arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Generates `%TEMP%\_WCP<seq>.TMP` (or `<CWD>\_WCP<seq>.TMP` when TEMP is unset).
@proc make_spool_path_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp

    bl hal_get_ctx_a64
    mov x19, x0
    ldr w20, [x19, #ARM64_CTX.dwRandomSeed]
    add w20, w20, #1
    str w20, [x19, #ARM64_CTX.dwRandomSeed]
    and w20, w20, #0xFFFF

    adr_sym x0, szVarTemp_a64
    bl env_get_a64
    cbnz x0, .got_tmp
    add x0, x19, #ARM64_CTX.szCwd
.got_tmp:
    mov x21, x0

    mov_imm32 x0, 560
    bl arena_alloc_a64
    cbz x0, .done
    mov x19, x0
    mov x1, x21
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .app_p
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.app_p:
    adr_sym x1, szSpoolPrefix_a64
    bl str_copy_a64
    mov x21, x0
    mov w0, w20
    mov x1, sp
    bl fmt_i32_a64
    mov x0, x21
    mov x1, sp
    bl str_copy_a64
    adr_sym x1, szSpoolExt_a64
    bl str_copy_a64
    mov x0, x19
.done:
    add sp, sp, #32
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc capture_cmd_to_buf_a64
;; @abi win_arm64
;; @in x0 = command string to execute
;; @out x0 = captured stdout WTF-8 buffer in arena (or 0 on failure)
;; @err x0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Captures stdout of a command into an arena buffer via a temporary spool file for `FOR /F`.
@proc capture_cmd_to_buf_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; cmd

    bl make_spool_path_a64
    cbz x0, .fail
    mov x20, x0 ; spool path

    mov x0, x20
    bl to_u16_longpath_a64
    mov w1, #1
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .fail
    mov x21, x0 ; hSpool

    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.fdTable
    ldr x22, [x1, #8] ; save fdTable[1]
    str x21, [x1, #8]

    mov x0, x19
    mov w1, #0
    bl exec_line_str_a64

    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.fdTable
    str x22, [x1, #8]
    mov x0, x21
    bl hal_close_handle_a64

    mov x0, x20
    bl to_u16_longpath_a64
    mov w1, #0
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .fail
    mov x21, x0

    mov_imm32 x0, 16384
    bl arena_alloc_a64
    mov x22, x0
    cbz x22, .cl_del
    mov x0, x21
    mov x1, x22
    mov_imm32 w2, 16380
    bl hal_read_handle_a64
    uxtw x0, w0
    strb wzr, [x22, x0]
.cl_del:
    mov x0, x21
    bl hal_close_handle_a64
    mov x0, x20
    bl to_u16_longpath_a64
    bl hal_delete_file_a64
    mov x0, x22
    b .exit

.fail:
    mov x0, #0
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc exec_pipe_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64* (AST_PIPE)
;; @out w0 = pipeline right-stage exit code
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Runs concurrent `CreatePipe` pipeline on Windows 10/11 ARM64 (or `%TEMP%` spooled pipeline when redirected).
@proc exec_pipe_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    bl hal_get_ctx_a64
    mov x20, x0
    add x1, x20, #ARM64_CTX.fdTable
    ldr x0, [x1, #0]
    ldr x2, [x20, #ARM64_CTX.hStdIn]
    cmp x0, x2
    b.ne .spool_pipe
    ldr x0, [x1, #8]
    ldr x2, [x20, #ARM64_CTX.hStdOut]
    cmp x0, x2
    b.ne .spool_pipe

    ldr x0, [x19, #AST_NODE_A64.pExtra]
    cbz x0, .spool_pipe
    mov w1, #0
    bl expand_phase34_a64
    mov x21, x0
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    cbz x0, .spool_pipe
    mov w1, #0
    bl expand_phase34_a64
    mov x1, x0
    mov x0, x21
    bl hal_run_pipe_concurrent_a64
    cmn w0, #1
    b.eq .spool_pipe
    str w0, [x20, #ARM64_CTX.dwLastErrorLevel]
    b .exit

.spool_pipe:
    bl make_spool_path_a64
    cbz x0, .pipe_err
    mov x21, x0 ; spool path

    mov x0, x21
    bl to_u16_longpath_a64
    mov w1, #1
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .pipe_err
    mov x22, x0 ; hOutSpool

    add x1, x20, #ARM64_CTX.fdTable
    ldr x23, [x1, #8]
    str x22, [x1, #8]
    mov w0, #0
    mov x1, #0
    bl setlocal_push_a64
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    bl endlocal_pop_a64
    add x1, x20, #ARM64_CTX.fdTable
    str x23, [x1, #8]
    mov x0, x22
    bl hal_close_handle_a64

    mov x0, x21
    bl to_u16_longpath_a64
    mov w1, #0
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .del_err
    mov x22, x0 ; hInSpool

    add x1, x20, #ARM64_CTX.fdTable
    ldr x23, [x1, #0]
    str x22, [x1, #0]
    mov w0, #0
    mov x1, #0
    bl setlocal_push_a64
    ldr x0, [x19, #AST_NODE_A64.pRight]
    bl exec_ast_a64
    mov w24, w0
    bl endlocal_pop_a64
    add x1, x20, #ARM64_CTX.fdTable
    str x23, [x1, #0]
    mov x0, x22
    bl hal_close_handle_a64

    mov x0, x21
    bl to_u16_longpath_a64
    bl hal_delete_file_a64
    str w24, [x20, #ARM64_CTX.dwLastErrorLevel]
    mov w0, w24
    b .exit

.del_err:
    mov x0, x21
    bl to_u16_longpath_a64
    bl hal_delete_file_a64
.pipe_err:
    mov w0, #1
    str w0, [x20, #ARM64_CTX.dwLastErrorLevel]
.exit:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc goto_label_a64
;; @abi win_arm64
;; @in x0 = target label string (with or without leading `:`)
;; @out w0 = 0 on success (updates active BATCH_FRAME_A64.uFileOff), 1 if label not found
;; @err w0 = 1 when label is missing
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements `GOTO [:]label` and `GOTO :EOF` by scanning the active batch file.
@proc goto_label_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x3A
    b.ne .chk_eof
    add x19, x19, #1

.chk_eof:
    bl hal_get_ctx_a64
    mov x20, x0
    ldr w1, [x20, #ARM64_CTX.dwBatchDepth]
    cbz w1, .err1
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x20, #ARM64_CTX.pBatchStack]
    add x21, x2, x1 ; active BATCH_FRAME_A64*

    mov x0, x19
    adr_sym x1, szKwEof_a64
    mov w2, #1
    bl str_cmp_a64
    cbnz w0, .scan_file
    mov x0, #-1
    str x0, [x21, #BATCH_FRAME_A64.uFileOff]
    mov w0, #1
    strb w0, [x21, #BATCH_FRAME_A64.bJumped]
    mov w0, #0
    b .exit

.scan_file:
    mov x0, x19
    bl str_len_a64
    mov x23, x0 ; target label len

    str xzr, [x21, #BATCH_FRAME_A64.uFileOff]
    strb wzr, [x21, #BATCH_FRAME_A64.bBomKind]

    mov_imm32 x0, A64_LINE_MAX
    bl arena_alloc_a64
    cbz x0, .err1
    mov x22, x0 ; line buf

.ln_lp:
    mov x0, x21
    mov x1, x22
    bl batch_read_raw_line_a64
    cbz w0, .not_found
    mov x0, x22
    bl skip_ws_a64
    ldrb w1, [x0, #0]
    cmp w1, #0x3A
    b.ne .ln_lp
    ldrb w1, [x0, #1]
    cmp w1, #0x3A
    b.eq .ln_lp
    add x0, x0, #1
    bl skip_ws_a64
    mov x24, x0 ; candidate label

    mov x0, x24
    mov x1, x19
    mov x2, x23
    bl str_ncmp_ci_a64
    cbnz w0, .ln_lp

    ldrb w0, [x24, x23]
    cbz w0, .found
    cmp w0, #0x20
    b.eq .found
    cmp w0, #0x09
    b.eq .found
    cmp w0, #0x3A
    b.eq .found
    b .ln_lp

.found:
    mov w0, #1
    strb w0, [x21, #BATCH_FRAME_A64.bJumped]
    mov w0, #0
    b .exit

.not_found:
    mov x0, #-1
    str x0, [x21, #BATCH_FRAME_A64.uFileOff]
    mov w0, #1
    strb w0, [x21, #BATCH_FRAME_A64.bJumped]
.err1:
    mov w0, #1
.exit:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc run_top_batch_frame_a64
;; @abi win_arm64
;; @in none
;; @out w0 = dwLastErrorLevel after batch/subroutine frame finishes
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Executes statements from the top BATCH_FRAME_A64 until EOF, `EXIT /B`, or `EXIT`, unwinding SETLOCALs!
@proc run_top_batch_frame_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp

    bl hal_get_ctx_a64
    mov x19, x0
    ldr w20, [x19, #ARM64_CTX.dwBatchDepth]
    sub w1, w20, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x19, #ARM64_CTX.pBatchStack]
    add x21, x2, x1 ; active frame

    mov_imm32 x0, A64_LINE_MAX
    bl arena_alloc_a64
    cbz x0, .unwind
    mov x22, x0

.b_loop:
    ldrb w0, [x19, #ARM64_CTX.bShouldExit]
    cbnz w0, .unwind
    ldrb w0, [x19, #ARM64_CTX.bCtrlBreak]
    cbnz w0, .unwind
    ldr x0, [x21, #BATCH_FRAME_A64.uFileOff]
    cmn x0, #1
    b.eq .unwind

    ldr x0, [x19, #ARM64_CTX.pArenaPtr]
    str x0, [sp, #0]

    mov x0, x21
    mov x1, x22
    bl batch_read_logical_line_a64
    cbz w0, .unwind_restore_arena

    strb wzr, [x21, #BATCH_FRAME_A64.bJumped]
    mov x0, x22
    mov w1, #1
    bl exec_line_str_a64

    ldr x0, [sp, #0]
    str x0, [x19, #ARM64_CTX.pArenaPtr]
    b .b_loop

.unwind_restore_arena:
    ldr x0, [sp, #0]
    str x0, [x19, #ARM64_CTX.pArenaPtr]
.unwind:
    ldr w20, [x21, #BATCH_FRAME_A64.dwSetlocalBase]
.sl_pop_lp:
    ldr w0, [x19, #ARM64_CTX.dwSetlocalDepth]
    cmp w0, w20
    b.ls .pop_frame
    bl endlocal_pop_a64
    b .sl_pop_lp

.pop_frame:
    ldr w0, [x19, #ARM64_CTX.dwBatchDepth]
    cbz w0, .ret_ec
    sub w0, w0, #1
    str w0, [x19, #ARM64_CTX.dwBatchDepth]
.ret_ec:
    ldr w0, [x19, #ARM64_CTX.dwLastErrorLevel]
    add sp, sp, #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc invoke_batch_or_sub_a64
;; @abi win_arm64
;; @in x0 = normalized batch path (or `:label`), w1 = argc, x2 = argv, x3 = raw_args_tail
;; @out w0 = exit code
;; @err w0 = 1 on stack overflow or missing label/file
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Pushes a BATCH_FRAME_A64 for `.BAT`/`.CMD` or `CALL :label` and runs it to completion.
@proc invoke_batch_or_sub_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    mov x29, sp
    mov x20, x0 ; path or `:label`
    mov w21, w1 ; argc
    mov x22, x2 ; argv
    mov x23, x3 ; raw tail

    bl hal_get_ctx_a64
    mov x19, x0
    ldr w24, [x19, #ARM64_CTX.dwBatchDepth]
    cmp w24, #A64_BATCH_MAX
    b.hs .err1

    uxtw x0, w24
    lsl x0, x0, #6
    ldr x1, [x19, #ARM64_CTX.pBatchStack]
    add x25, x1, x0 ; new BATCH_FRAME_A64*
    mov x0, x25
    mov x1, #64
    bl mem_zero_a64

    ldr w0, [x19, #ARM64_CTX.dwSetlocalDepth]
    str w0, [x25, #BATCH_FRAME_A64.dwSetlocalBase]
    ldr w0, [x19, #ARM64_CTX.dwCodePage]
    str w0, [x25, #BATCH_FRAME_A64.dwSavedCodePage]
    mov x0, x23
    bl perm_strdup_a64
    str x0, [x25, #BATCH_FRAME_A64.pRawArgs]

    uxtw x0, w21
    lsl x0, x0, #3
    add x0, x0, #16
    bl perm_alloc_a64
    cbz x0, .err1
    mov x26, x0
    str x26, [x25, #BATCH_FRAME_A64.pArgVec]
    str w21, [x25, #BATCH_FRAME_A64.dwArgCount]

    mov w23, #0
.cp_av:
    cmp w23, w21
    b.hs .av_done
    uxtw x1, w23
    lsl x1, x1, #3
    ldr x0, [x22, x1]
    bl perm_strdup_a64
    uxtw x1, w23
    lsl x1, x1, #3
    str x0, [x26, x1]
    add w23, w23, #1
    b .cp_av

.av_done:
    ldrb w0, [x20, #0]
    cmp w0, #0x3A
    b.ne .ext_bat_file
    cbz w24, .err1
    sub w0, w24, #1
    uxtw x0, w0
    lsl x0, x0, #6
    ldr x1, [x19, #ARM64_CTX.pBatchStack]
    add x1, x1, x0
    ldr x0, [x1, #BATCH_FRAME_A64.pFilePath]
    str x0, [x25, #BATCH_FRAME_A64.pFilePath]
    mov w0, #1
    strb w0, [x25, #BATCH_FRAME_A64.bIsCallSub]
    add w24, w24, #1
    str w24, [x19, #ARM64_CTX.dwBatchDepth]

    mov x0, x20
    bl goto_label_a64
    cbnz w0, .sub_missing
    bl run_top_batch_frame_a64
    b .exit

.sub_missing:
    ldr w0, [x19, #ARM64_CTX.dwBatchDepth]
    sub w0, w0, #1
    str w0, [x19, #ARM64_CTX.dwBatchDepth]
    b .err1

.ext_bat_file:
    mov x0, x20
    bl perm_strdup_a64
    str x0, [x25, #BATCH_FRAME_A64.pFilePath]
    add w24, w24, #1
    str w24, [x19, #ARM64_CTX.dwBatchDepth]
    bl run_top_batch_frame_a64
    b .exit

.err1:
    mov w0, #1
.exit:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc probe_candidate_file_a64
;; @abi win_arm64
;; @in x0 = candidate WTF-8 file path
;; @out x0 = resolved WTF-8 file path (resolving AppExecLink aliases if applicable), or 0 if not a file
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Checks file existence and resolves Windows 10/11 IO_REPARSE_TAG_APPEXECLINK execution aliases.
@proc probe_candidate_file_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    bl to_u16_longpath_a64
    cbz x0, .nf
    mov x20, x0
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .nf
    tbnz w0, #4, .nf ; skip directories (`0x10`)
    tbz w0, #10, .ret_cand ; not reparse (`0x400`)

    mov_imm32 x0, 528
    bl arena_alloc_a64
    cbz x0, .ret_cand
    mov x21, x0
    mov x0, x20
    mov x1, x21
    bl hal_resolve_appexeclink_a64
    cbz w0, .ret_cand
    mov x0, x21
    b .done

.ret_cand:
    mov x0, x19
    b .done
.nf:
    mov x0, #0
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc probe_file_with_pathext_a64
;; @abi win_arm64
;; @in x0 = candidate base path (normalized WTF-8), w1 = has_explicit_ext
;; @out x0 = resolved existing file path in arena (or 0 if not found)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Probes an executable/batch path directly and across `;`-separated `%PATHEXT%` extensions.
@proc probe_file_with_pathext_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; base

    bl probe_candidate_file_a64
    cbnz x0, .done

    adr_sym x0, szVarPathext_a64
    bl env_get_a64
    cbnz x0, .got_pe
    adr_sym x0, szDefPathext_a64
.got_pe:
    mov x20, x0

.pe_lp:
    ldrb w0, [x20, #0]
    cbz w0, .not_found
    cmp w0, #0x3B
    b.ne .ext_st
    add x20, x20, #1
    b .pe_lp
.ext_st:
    mov x21, x20
.ext_sc:
    ldrb w0, [x21, #0]
    cbz w0, .got_ext
    cmp w0, #0x3B
    b.eq .got_ext
    add x21, x21, #1
    b .ext_sc

.got_ext:
    sub x22, x21, x20 ; ext len
    mov x0, x19
    bl str_len_a64
    add x0, x0, x22
    add x0, x0, #8
    bl arena_alloc_a64
    cbz x0, .not_found
    mov x23, x0
    mov x1, x19
    bl str_copy_a64
    mov x1, x20
    mov x2, x22
    bl mem_copy_a64
    strb wzr, [x0, #0]

    mov x0, x23
    bl probe_candidate_file_a64
    cbnz x0, .done
    mov x20, x21
    b .pe_lp

.not_found:
    mov x0, #0
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc resolve_executable_a64
;; @abi win_arm64
;; @in x0 = unquoted command name
;; @out x0 = resolved full WTF-8 path in arena (or 0 if not found)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Resolves external `.COM`/`.EXE`/`.BAT`/`.CMD` across explicit path, CWD (honoring NoDefaultCurrentDirectoryInExePath), and `%PATH%`.
@proc resolve_executable_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    mov x1, #0
    mov w20, #0 ; has_sep
    mov w21, #0 ; has_dot
.ch_lp:
    ldrb w0, [x19, x1]
    cbz w0, .ch_done
    cmp w0, #0x5C
    b.eq .sep_hit
    cmp w0, #0x2F
    b.eq .sep_hit
    cmp w0, #0x3A
    b.eq .sep_hit
    cmp w0, #0x2E
    b.ne .ch_nx
    mov w21, #1
    b .ch_nx
.sep_hit:
    mov w20, #1
    mov w21, #0
.ch_nx:
    add x1, x1, #1
    b .ch_lp

.ch_done:
    cbz w20, .search_cwd_path
    mov x0, x19
    bl norm_path_a64
    mov w1, w21
    bl probe_file_with_pathext_a64
    b .done

.search_cwd_path:
    adr_sym x0, szNoDefCwdEnv_a64
    bl env_get_raw_a64
    cbnz x0, .search_path_only

    mov x0, x19
    bl norm_path_a64
    mov w1, w21
    bl probe_file_with_pathext_a64
    cbnz x0, .done

.search_path_only:
    adr_sym x0, szVarPath_a64
    bl env_get_a64
    cbz x0, .not_found
    mov x22, x0

.p_lp:
    ldrb w0, [x22, #0]
    cbz w0, .not_found
    cmp w0, #0x3B
    b.ne .p_st
    add x22, x22, #1
    b .p_lp
.p_st:
    mov x23, x22
.p_sc:
    ldrb w0, [x23, #0]
    cbz w0, .p_seg
    cmp w0, #0x3B
    b.eq .p_seg
    add x23, x23, #1
    b .p_sc

.p_seg:
    sub x24, x23, x22
    cbz x24, .p_nx
    add x0, x24, #16
    bl arena_alloc_a64
    cbz x0, .not_found
    mov x20, x0
    mov x1, x22
    mov x2, x24
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x20
    bl strip_outer_quotes_a64
    mov x20, x0
    bl str_len_a64
    mov x24, x0
    mov x0, x19
    bl str_len_a64
    add x0, x0, x24
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .not_found
    mov x24, x0
    mov x1, x20
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .p_app
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.p_app:
    mov x1, x19
    bl str_copy_a64

    mov x0, x24
    bl norm_path_a64
    mov w1, w21
    bl probe_file_with_pathext_a64
    cbnz x0, .done
.p_nx:
    mov x22, x23
    b .p_lp

.not_found:
    mov x0, #0
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc check_ext_kind_a64
;; @abi win_arm64
;; @in x0 = resolved file path
;; @out w0 = 1 if `.BAT`/`.CMD`, 2 if `.COM`, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Classifies file extension for batch script vs `.COM` vm8086 vs native PE dispatch.
@proc check_ext_kind_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl str_len_a64
    cmp x0, #4
    b.lo .other
    sub x0, x0, #4
    add x19, x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x2E
    b.ne .other

    mov x0, x19
    adr_sym x1, szExtBat_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .is_bat

    mov x0, x19
    adr_sym x1, szExtCmd_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .is_bat

    mov x0, x19
    adr_sym x1, szExtCom_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .is_com

.other:
    mov w0, #0
    b .done
.is_bat:
    mov w0, #1
    b .done
.is_com:
    mov w0, #2
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc launch_resolved_target_a64
;; @abi win_arm64
;; @in x0 = resolved path, w1 = argc, x2 = raw_argv, x3 = raw_tail, w4 = flags (bit0=is_call, bit1=wait)
;; @out w0 = exit code
;; @err w0 = 9009 / 1 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Executes a resolved `.BAT`/`.CMD`, `.COM` (via vm8086), or `.EXE` (via CreateProcessW with MZ vm8086 fallback).
@proc launch_resolved_target_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; path
    mov w20, w1 ; argc
    mov x21, x2 ; argv
    mov x22, x3 ; raw tail
    mov w23, w4 ; flags

    mov x0, x19
    bl check_ext_kind_a64
    cmp w0, #1
    b.eq .do_bat
    cmp w0, #2
    b.eq .do_com

    mov x0, x19
    bl str_len_a64
    mov x24, x0
    mov x0, x22
    bl str_len_a64
    add x0, x0, x24
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .err1
    mov x24, x0
    mov w1, #0x22
    strb w1, [x24, #0]
    add x0, x24, #1
    mov x1, x19
    bl str_copy_a64
    mov w1, #0x22
    strb w1, [x0, #0]
    add x0, x0, #1
    ldrb w1, [x22, #0]
    cbz w1, .fin_cl
    mov w1, #0x20
    strb w1, [x0, #0]
    add x0, x0, #1
    mov x1, x22
    bl str_copy_a64
    b .spawn_pe
.fin_cl:
    strb wzr, [x0, #0]

.spawn_pe:
    mov x0, x19
    bl to_u16_longpath_a64
    mov x20, x0 ; u16 app
    mov x0, x24
    bl wtf8_to_u16_a64
    mov x21, x0 ; u16 cmdline
    bl hal_get_ctx_a64
    add x0, x0, #ARM64_CTX.szCwd
    bl wtf8_to_u16_a64
    mov x2, x0 ; u16 cwd
    mov x0, x20
    mov x1, x21
    lsr w3, w23, #1
    and w3, w3, #1
    bl hal_spawn_process_a64
    cmn w0, #2
    b.eq .do_com
    cmn w0, #1
    b.eq .err1
    b .done

.do_com:
    mov x0, x22
    bl str_len_a64
    add x0, x0, #8
    bl arena_alloc_a64
    cbz x0, .err1
    mov x24, x0
    ldrb w1, [x22, #0]
    cbz w1, .empty_ct
    mov w1, #0x20
    strb w1, [x24, #0]
    add x0, x24, #1
    mov x1, x22
    bl str_copy_a64
    b .go_vm
.empty_ct:
    strb wzr, [x24, #0]
.go_vm:
    mov x0, x19
    mov x1, x24
    bl vm86_run_image_a64
    cbnz w1, .vm_err
    b .done
.vm_err:
    mov w0, w1
    bl print_err_code_a64
    mov w0, #1
    b .done

.do_bat:
    tbnz w23, #0, .call_bat
    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .call_bat
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x0, #ARM64_CTX.pBatchStack]
    add x2, x2, x1
    mov x3, #-1
    str x3, [x2, #BATCH_FRAME_A64.uFileOff]
.call_bat:
    mov x0, x19
    mov w1, w20
    mov x2, x21
    mov x3, x22
    bl invoke_batch_or_sub_a64
    b .done

.err1:
    mov w0, #1
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc exec_simple_cmd_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64* (AST_SIMPLE_CMD)
;; @out w0 = exit code
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Expands Phase 3/4 arguments, applies FD 0..9 redirections, and dispatches built-ins, batch scripts, PE, or vm8086!
@proc exec_simple_cmd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #96 ; redir save block (96B) at sp+0
    mov x29, sp
    mov x19, x0
    ldr w20, [x19, #AST_NODE_A64.dwArgc]

    ldr x0, [x19, #AST_NODE_A64.pRedir]
    mov x1, sp
    bl apply_redirs_a64
    cbnz w0, .redir_failed

    cbz w20, .ok_restore

    uxtw x0, w20
    lsl x0, x0, #3
    add x0, x0, #16
    bl arena_alloc_a64
    mov x22, x0 ; unquoted argv
    uxtw x0, w20
    lsl x0, x0, #3
    add x0, x0, #16
    bl arena_alloc_a64
    mov x23, x0 ; raw argv
    cbz x22, .err_restore
    cbz x23, .err_restore

    ldr x21, [x19, #AST_NODE_A64.pArgv]
    mov w24, #0
.exp_lp:
    cmp w24, w20
    b.hs .exp_done
    uxtw x1, w24
    lsl x1, x1, #3
    ldr x0, [x21, x1]
    mov w1, #0
    bl expand_phase34_a64
    uxtw x1, w24
    lsl x1, x1, #3
    str x0, [x23, x1]

    ldr x0, [x21, x1]
    mov w1, #1
    bl expand_phase34_a64
    uxtw x1, w24
    lsl x1, x1, #3
    str x0, [x22, x1]
    add w24, w24, #1
    b .exp_lp

.exp_done:
    ldr x24, [x22, #0] ; cmd0
    ldrb w0, [x24, #0]
    cbz w0, .ok_restore

    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .after_echo
    ldrb w1, [x0, #ARM64_CTX.bEchoOn]
    cbz w1, .after_echo
    ldrb w1, [x19, #AST_NODE_A64.bFlags]
    tbnz w1, #0, .after_echo
    bl print_prompt_a64
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    mov w1, #0
    bl expand_phase34_a64
    bl out_str_a64
    bl out_crlf_a64

.after_echo:
    mov x0, x24
    bl str_len_a64
    cmp x0, #2
    b.ne .chk_builtin
    ldrb w0, [x24, #1]
    cmp w0, #0x3A
    b.ne .chk_builtin
    mov x0, x24
    bl norm_path_a64
    bl set_drv_cwd_a64
    mov w25, #0
    b .restore_ret

.chk_builtin:
    mov x0, x24
    bl lookup_builtin_id_a64
    cmp w0, #CMD_NONE
    b.eq .external_cmd
    mov w25, w0 ; cmd_id

    cmp w25, #CMD_ECHO
    b.eq .c_echo
    cmp w25, #CMD_SET
    b.eq .c_set
    cmp w25, #CMD_SETLOCAL
    b.eq .c_setlocal
    cmp w25, #CMD_ENDLOCAL
    b.eq .c_endlocal
    cmp w25, #CMD_CD
    b.eq .c_cd
    cmp w25, #CMD_PUSHD
    b.eq .c_pushd
    cmp w25, #CMD_POPD
    b.eq .c_popd
    cmp w25, #CMD_MD
    b.eq .c_md
    cmp w25, #CMD_RD
    b.eq .c_rd
    cmp w25, #CMD_MKLINK
    b.eq .c_mklink
    cmp w25, #CMD_DEL
    b.eq .c_del
    cmp w25, #CMD_COPY
    b.eq .c_copy
    cmp w25, #CMD_MOVE
    b.eq .c_move
    cmp w25, #CMD_REN
    b.eq .c_ren
    cmp w25, #CMD_DIR
    b.eq .c_dir
    cmp w25, #CMD_TYPE
    b.eq .c_type_tv
    cmp w25, #CMD_TRUENAME
    b.eq .c_type_tv
    cmp w25, #CMD_VOL
    b.eq .c_type_tv
    cmp w25, #CMD_VER
    b.eq .c_ver
    cmp w25, #CMD_ASSOC
    b.eq .c_assoc
    cmp w25, #CMD_FTYPE
    b.eq .c_ftype
    cmp w25, #CMD_COLOR
    b.eq .c_color
    cmp w25, #CMD_DOSKEY
    b.eq .c_doskey
    cmp w25, #CMD_GOTO
    b.eq .c_goto
    cmp w25, #CMD_CALL
    b.eq .c_call
    cmp w25, #CMD_START
    b.eq .c_start
    cmp w25, #CMD_CMD
    b.eq .c_subcmd
    cmp w25, #CMD_LH
    b.eq .c_lh

    mov w0, w25
    mov x1, x19
    mov w2, w20
    mov x3, x22
    bl bi_misc_simple_a64
    mov w25, w0
    b .restore_ret

.c_echo:
    mov x0, x19
    mov w1, w20
    mov x2, x23
    bl bi_echo_a64
    mov w25, w0
    b .restore_ret
.c_set:
    mov x0, x19
    bl bi_set_a64
    mov w25, w0
    b .restore_ret
.c_setlocal:
    mov w0, w20
    mov x1, x22
    bl setlocal_push_a64
    mov w25, w0
    b .restore_ret
.c_endlocal:
    bl endlocal_pop_a64
    mov w25, w0
    b .restore_ret
.c_cd:
    mov x0, x19
    bl bi_cd_a64
    mov w25, w0
    b .restore_ret
.c_pushd:
    mov x0, x19
    bl bi_pushd_a64
    mov w25, w0
    b .restore_ret
.c_popd:
    bl bi_popd_a64
    mov w25, w0
    b .restore_ret
.c_md:
    mov w0, w20
    mov x1, x22
    bl bi_md_a64
    mov w25, w0
    b .restore_ret
.c_rd:
    mov w0, w20
    mov x1, x22
    bl bi_rd_a64
    mov w25, w0
    b .restore_ret
.c_mklink:
    mov w0, w20
    mov x1, x22
    bl bi_mklink_a64
    mov w25, w0
    b .restore_ret
.c_del:
    mov w0, w20
    mov x1, x22
    bl bi_del_a64
    mov w25, w0
    b .restore_ret
.c_copy:
    mov w0, w20
    mov x1, x22
    bl bi_copy_a64
    mov w25, w0
    b .restore_ret
.c_move:
    mov w0, w20
    mov x1, x22
    mov w2, #0
    bl bi_move_ren_a64
    mov w25, w0
    b .restore_ret
.c_ren:
    mov w0, w20
    mov x1, x22
    mov w2, #1
    bl bi_move_ren_a64
    mov w25, w0
    b .restore_ret
.c_dir:
    mov w0, w20
    mov x1, x22
    bl bi_dir_a64
    mov w25, w0
    b .restore_ret
.c_type_tv:
    mov w0, w25
    mov w1, w20
    mov x2, x22
    bl bi_type_truename_vol_a64
    mov w25, w0
    b .restore_ret
.c_ver:
    bl bi_ver_a64
    mov w25, w0
    b .restore_ret
.c_assoc:
    mov x0, x19
    mov w1, #0
    bl bi_assoc_ftype_a64
    mov w25, w0
    b .restore_ret
.c_ftype:
    mov x0, x19
    mov w1, #1
    bl bi_assoc_ftype_a64
    mov w25, w0
    b .restore_ret
.c_color:
    mov w0, w20
    mov x1, x22
    bl bi_color_a64
    mov w25, w0
    b .restore_ret
.c_doskey:
    mov x0, x19
    mov w1, w20
    mov x2, x22
    bl bi_doskey_a64
    mov w25, w0
    b .restore_ret

.c_goto:
    cmp w20, #2
    b.lo .err_restore
    ldr x0, [x22, #8]
    bl goto_label_a64
    mov w25, w0
    b .restore_ret

.c_lh:
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    mov w1, #0
    bl exec_line_str_a64
    mov w25, w0
    b .restore_ret

.c_call:
    cmp w20, #2
    b.lo .ok_restore
    ldr x24, [x22, #8]
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    mov x21, x0

    ldrb w0, [x24, #0]
    cmp w0, #0x3A
    b.ne .call_ext_or_bi
    mov x0, x21
    bl extract_raw_tail_a64
    mov x3, x0
    mov x0, x24
    sub w1, w20, #1
    add x2, x23, #8
    bl invoke_batch_or_sub_a64
    mov w25, w0
    b .restore_ret

.call_ext_or_bi:
    mov x0, x24
    bl lookup_builtin_id_a64
    cmp w0, #CMD_NONE
    b.eq .call_file
    mov x0, x21
    mov w1, #0
    bl exec_line_str_a64
    mov w25, w0
    b .restore_ret
.call_file:
    mov x0, x24
    bl resolve_executable_a64
    cbz x0, .bad_cmd
    mov x24, x0
    mov x0, x21
    bl extract_raw_tail_a64
    mov x3, x0
    mov x0, x24
    sub w1, w20, #1
    add x2, x23, #8
    mov w4, #3
    bl launch_resolved_target_a64
    mov w25, w0
    b .restore_ret

.c_start:
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    mov x21, x0
.st_sw:
    mov x0, x21
    bl skip_ws_a64
    mov x21, x0
    ldrb w0, [x21, #0]
    cbz w0, .ok_restore
    cmp w0, #0x22
    b.ne .st_chk_sl
    mov x0, x21
    bl scan_cmd_token_a64
    mov x26, x1
    mov x0, x26
    bl skip_ws_a64
    ldrb w0, [x0, #0]
    cbz w0, .st_exec
    mov x21, x26
    b .st_sw
.st_chk_sl:
    cmp w0, #0x2F
    b.ne .st_exec
    ldrb w0, [x21, #1]
    bl ascii_upper_a64
    cmp w0, #0x44
    b.ne .st_sk1
    add x0, x21, #2
    bl scan_cmd_token_a64
    mov x21, x1
    bl unquote_dup_a64
    bl norm_path_a64
    bl set_drv_cwd_a64
    b .st_sw
.st_sk1:
    mov x0, x21
    bl scan_cmd_token_a64
    mov x21, x1
    b .st_sw
.st_exec:
    mov x0, x21
    mov w1, #0
    bl exec_line_str_a64
    mov w25, w0
    b .restore_ret

.c_subcmd:
    bl hal_get_ctx_a64
    ldr x24, [x0, #ARM64_CTX.pSelfExePath]
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    mov x3, x0
    mov x0, x24
    mov w1, w20
    mov x2, x23
    mov w4, #2
    bl launch_resolved_target_a64
    mov w25, w0
    b .restore_ret

.external_cmd:
    mov x0, x24
    bl resolve_executable_a64
    cbz x0, .bad_cmd
    mov x24, x0
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    mov x3, x0
    mov x0, x24
    mov w1, w20
    mov x2, x23
    mov w4, #2
    bl launch_resolved_target_a64
    mov w25, w0
    b .restore_ret

.bad_cmd:
    mov_imm32 w0, ERR_BAD_COMMAND
    bl print_err_code_a64
    mov_imm32 w25, 9009
    b .restore_ret

.ok_restore:
    mov w25, #0
    b .restore_ret
.err_restore:
    mov w25, #1
.restore_ret:
    mov x0, sp
    bl restore_redirs_a64
    bl hal_get_ctx_a64
    str w25, [x0, #ARM64_CTX.dwLastErrorLevel]
    mov w0, w25
    b .exit

.redir_failed:
    bl hal_get_ctx_a64
    mov w1, #1
    str w1, [x0, #ARM64_CTX.dwLastErrorLevel]
    mov w0, #1
.exit:
    add sp, sp, #96
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc exec_ast_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*
;; @out w0 = exit code
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Recursive Arena AST evaluator across simple commands, blocks, `&`, `&&`, `||`, `|`, `IF`, and `FOR`.
@proc exec_ast_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #96 ; redir save block (96B) at sp+0 for AST_BLOCK
    mov x29, sp
    mov x19, x0
    cbz x19, .ok0

    bl hal_get_ctx_a64
    ldrb w1, [x0, #ARM64_CTX.bShouldExit]
    cbnz w1, .cur_err
    ldrb w1, [x0, #ARM64_CTX.bCtrlBreak]
    cbnz w1, .cur_err
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .chk_kind
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x0, #ARM64_CTX.pBatchStack]
    add x2, x2, x1
    ldrb w1, [x2, #BATCH_FRAME_A64.bJumped]
    cbnz w1, .cur_err

.chk_kind:
    ldrb w1, [x19, #AST_NODE_A64.bKind]
    cmp w1, #AST_SIMPLE_CMD
    b.eq .do_simple
    cmp w1, #AST_BLOCK
    b.eq .do_block
    cmp w1, #AST_SEQ
    b.eq .do_seq
    cmp w1, #AST_AND
    b.eq .do_and
    cmp w1, #AST_OR
    b.eq .do_or
    cmp w1, #AST_PIPE
    b.eq .do_pipe
    cmp w1, #AST_IF
    b.eq .do_if
    cmp w1, #AST_FOR
    b.eq .do_for
    b .cur_err

.do_simple:
    mov x0, x19
    bl exec_simple_cmd_a64
    b .done

.do_block:
    ldr x0, [x19, #AST_NODE_A64.pRedir]
    mov x1, sp
    bl apply_redirs_a64
    cbnz w0, .blk_fail
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    mov w20, w0
    mov x0, sp
    bl restore_redirs_a64
    mov w0, w20
    b .done
.blk_fail:
    bl hal_get_ctx_a64
    mov w1, #1
    str w1, [x0, #ARM64_CTX.dwLastErrorLevel]
    mov w0, #1
    b .done

.do_seq:
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    ldr x0, [x19, #AST_NODE_A64.pRight]
    bl exec_ast_a64
    b .done

.do_and:
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    cbnz w0, .done
    ldr x0, [x19, #AST_NODE_A64.pRight]
    bl exec_ast_a64
    b .done

.do_or:
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    cbz w0, .done
    ldr x0, [x19, #AST_NODE_A64.pRight]
    bl exec_ast_a64
    b .done

.do_pipe:
    mov x0, x19
    bl exec_pipe_a64
    b .done

.do_if:
    mov x0, x19
    bl eval_if_cond_a64
    cbz w0, .if_else
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    b .done
.if_else:
    ldr x0, [x19, #AST_NODE_A64.pRight]
    cbz x0, .cur_err
    bl exec_ast_a64
    b .done

.do_for:
    mov x0, x19
    bl exec_for_a64
    b .done

.cur_err:
    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwLastErrorLevel]
    b .done
.ok0:
    mov w0, #0
.done:
    add sp, sp, #96
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc exec_line_str_a64
;; @abi win_arm64
;; @in x0 = logical line string, w1 = in_batch
;; @out w0 = exit code
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Runs Phase 1 `%` expansion, builds Arena AST, and executes it.
@proc exec_line_str_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    bl expand_phase1_a64
    cbz x0, .ok0
    bl parse_line_ast_a64
    cbz x0, .cur_ec
    bl exec_ast_a64
    b .done
.cur_ec:
    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwLastErrorLevel]
    b .done
.ok0:
    mov w0, #0
.done:
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc strip_cmd_s_quotes_a64
;; @abi win_arm64
;; @in x0 = raw `/C` or `/K` tail, w1 = force_s_strip
;; @out x0 = command string with outer quotes stripped per `cmd.exe`'s 5-rule `/C`/`/S` heuristic
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements the authentic `cmd.exe` `/C` + `/S` outer-quote-stripping algorithm.
@proc strip_cmd_s_quotes_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov w20, w1
    bl skip_ws_a64
    mov x19, x0
    bl str_len_a64
    mov x21, x0
    cmp x21, #2
    b.lo .keep
    ldrb w0, [x19, #0]
    cmp w0, #0x22
    b.ne .keep
    sub x1, x21, #1
    ldrb w0, [x19, x1]
    cmp w0, #0x22
    b.ne .keep

    cbnz w20, .do_strip

    mov x1, #0
    mov w2, #0
    mov w3, #0
.q_sc:
    ldrb w0, [x19, x1]
    cbz w0, .q_done
    cmp w0, #0x22
    b.ne .q_sp
    add w2, w2, #1
    b .q_nx
.q_sp:
    cmp w0, #0x26
    b.eq .sp_hit
    cmp w0, #0x3C
    b.eq .sp_hit
    cmp w0, #0x3E
    b.eq .sp_hit
    cmp w0, #0x28
    b.eq .sp_hit
    cmp w0, #0x29
    b.eq .sp_hit
    cmp w0, #0x7C
    b.ne .q_nx
.sp_hit:
    mov w3, #1
.q_nx:
    add x1, x1, #1
    b .q_sc
.q_done:
    cmp w2, #2
    b.ne .keep
    cbz w3, .keep
.do_strip:
    mov x0, x19
    bl unquote_dup_a64
    b .exit
.keep:
    mov x0, x19
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc shell_main_a64
;; @abi win_arm64
;; @in none
;; @out w0 = process exit code
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Initializes the ARM64 platform HAL, probes >4 KiB stack via __chkstk (X15), parses startup switches, runs AutoRun, and enters `/C` or REPL.
@proc shell_main_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    ; Probe 4,160-byte stack frame via Windows ARM64 __chkstk (X15 = bytes / 16 = 260)
    mov x15, #260
    bl __chkstk
    sub sp, sp, #4096
    sub sp, sp, #64
    mov x29, sp

    bl hal_get_ctx_a64
    mov x19, x0
    bl hal_init_platform_a64
    cbnz w0, .fatal_exit

    ldr x20, [x19, #ARM64_CTX.pCmdLineRaw]
    cbz x20, .repl_start
    mov x0, x20
    bl scan_cmd_token_a64
    mov x20, x1

    mov w21, #0 ; mode: 0=REPL, 1=/C, 2=/K
    mov w22, #0 ; /S flag
    mov w23, #0 ; /D skip AutoRun
    mov w24, #0 ; /Q quiet

.sw_loop:
    mov x0, x20
    bl skip_ws_a64
    mov x20, x0
    ldrb w0, [x20, #0]
    cbz w0, .after_switches
    cmp w0, #0x2F
    b.ne .after_switches

    ldrb w0, [x20, #1]
    bl ascii_upper_a64
    cmp w0, #0x43
    b.eq .got_c
    cmp w0, #0x4B
    b.eq .got_k
    cmp w0, #0x53
    b.eq .sw_s
    cmp w0, #0x44
    b.eq .sw_d
    cmp w0, #0x51
    b.eq .sw_q
    cmp w0, #0x55
    b.eq .sw_u
    cmp w0, #0x41
    b.eq .sw_a
    cmp w0, #0x56
    b.eq .sw_v
    cmp w0, #0x45
    b.eq .sw_e
    cmp w0, #0x46
    b.eq .sw_f
    mov x0, x20
    bl scan_cmd_token_a64
    mov x20, x1
    b .sw_loop

.sw_s:
    mov w22, #1
    add x20, x20, #2
    b .sw_loop
.sw_d:
    mov w23, #1
    add x20, x20, #2
    b .sw_loop
.sw_q:
    mov w24, #1
    strb wzr, [x19, #ARM64_CTX.bEchoOn]
    add x20, x20, #2
    b .sw_loop
.sw_u:
    mov w0, #1
    strb w0, [x19, #ARM64_CTX.bUnicodeOut]
    add x20, x20, #2
    b .sw_loop
.sw_a:
    strb wzr, [x19, #ARM64_CTX.bUnicodeOut]
    add x20, x20, #2
    b .sw_loop
.sw_v:
    mov x0, x20
    bl scan_cmd_token_a64
    mov x20, x1
    mov x25, x0
    mov w26, #1
    ldrb w2, [x25, #2]
    cmp w2, #0x3A
    b.ne .set_v
    ldrb w0, [x25, #4]
    bl ascii_upper_a64
    cmp w0, #0x46
    b.ne .set_v
    mov w26, #0
.set_v:
    strb w26, [x19, #ARM64_CTX.bDelayedExp]
    b .sw_loop

.sw_e:
    mov x0, x20
    bl scan_cmd_token_a64
    mov x20, x1
    mov x25, x0
    mov w26, #1
    ldrb w2, [x25, #2]
    cmp w2, #0x3A
    b.ne .set_e
    ldrb w0, [x25, #4]
    bl ascii_upper_a64
    cmp w0, #0x46
    b.ne .set_e
    mov w26, #0
.set_e:
    strb w26, [x19, #ARM64_CTX.bExtensions]
    b .sw_loop

.sw_f:
    mov x0, x20
    bl scan_cmd_token_a64
    mov x20, x1
    b .sw_loop

.got_c:
    mov w21, #1
    add x20, x20, #2
    b .after_switches
.got_k:
    mov w21, #2
    add x20, x20, #2

.after_switches:
    cbnz w23, .chk_ck
    adr_sym x0, szRegCmdProcKey_a64
    bl wtf8_to_u16_a64
    mov x25, x0
    adr_sym x0, szRegAutoRunVal_a64
    bl wtf8_to_u16_a64
    mov x2, x0
    mov_imm32 x0, 0x80000001 ; HKCU (sign-extended by Win32/64 reg mock or 0xFFFFFFFF80000001)
    sbfx x0, x0, #0, #32
    mov x1, x25
    mov x3, sp
    bl hal_reg_query_str_a64
    cbnz w0, .chk_ck
    ldrb w0, [sp, #0]
    cbz w0, .chk_ck
    mov x0, sp
    mov w1, #0
    bl exec_line_str_a64

.chk_ck:
    cbz w21, .repl_banner
    mov x0, x20
    ldrb w1, [x0, #0]
    cmp w1, #0x20
    b.ne .strip_ck
    add x0, x0, #1
.strip_ck:
    mov w1, w22
    bl strip_cmd_s_quotes_a64
    mov w1, #0
    bl exec_line_str_a64
    cmp w21, #1
    b.eq .normal_exit
    b .repl_loop

.repl_banner:
    cbnz w24, .repl_loop
    bl bi_ver_a64
    bl out_crlf_a64

.repl_start:
.repl_loop:
    ldrb w0, [x19, #ARM64_CTX.bShouldExit]
    cbnz w0, .normal_exit

    bl arena_reset_a64
    strb wzr, [x19, #ARM64_CTX.bCtrlBreak]

    ldrb w0, [x19, #ARM64_CTX.bEchoOn]
    cbz w0, .rd_repl
    bl print_prompt_a64

.rd_repl:
    mov x0, sp
    mov_imm32 w1, 4000
    bl read_stdin_line_a64
    cbz w0, .normal_exit

    mov x0, sp
    bl doskey_add_history_a64
    mov x0, sp
    bl doskey_expand_macro_a64
    mov w1, #0
    bl exec_line_str_a64
    b .repl_loop

.normal_exit:
    ldrb w0, [x19, #ARM64_CTX.bShouldExit]
    cbnz w0, .use_explicit_exit
    ldr w0, [x19, #ARM64_CTX.dwLastErrorLevel]
    b .fin_ret
.use_explicit_exit:
    ldr w0, [x19, #ARM64_CTX.dwExitCode]
    b .fin_ret

.fatal_exit:
    mov w0, #1
.fin_ret:
    add sp, sp, #4096
    add sp, sp, #64
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
