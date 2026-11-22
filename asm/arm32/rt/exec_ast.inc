;; @file exec_ast.inc
;; @isa armv4
;; @desc ARM32 AST Executor, Concurrent/Spooled Pipelines, Batch/:label Engine, PATHEXT/vm8086 Spawner, and Shell Main.

@cpu armv4
@format inc

;; @proc make_spool_path_a32
;; @abi arm_ce
;; @in none
;; @out r0 = unique WTF-8 temporary spool file path in arena
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Generates `%TEMP%\_wcp<seq>.tmp` (or `\Temp\_wcp<seq>.tmp` on Windows CE).
@proc make_spool_path_a32
    push {r4, r5, r6, lr}
    sub sp, sp, #24
    bl hal_get_ctx
    mov r4, r0
    ldr r5, [r4, #ARM32_CTX.dwPipeSeq]
    add r5, r5, #1
    str r5, [r4, #ARM32_CTX.dwPipeSeq]

    adr_sym r0, szVarTemp
    bl env_get_a32
    cmp r0, #0
    bne .got_tmp
    ldrb r0, [r4, #ARM32_CTX.bIsWinCe]
    cmp r0, #0
    beq .cwd_tmp
    adr_sym r0, szWinCeTempDir
    b .got_tmp
.cwd_tmp:
    mov_imm32 r0, ARM32_CTX.szCwd
    add r0, r4, r0
.got_tmp:
    mov r6, r0
    ; Ensure temp directory exists on WinCE
    ldrb r0, [r4, #ARM32_CTX.bIsWinCe]
    cmp r0, #0
    beq .fmt_sp
    mov r0, r6
    bl to_u16_longpath_a32
    bl hal_mkdir

.fmt_sp:
    mov_imm32 r0, 560
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r4, r0
    mov r1, r6
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .app_p
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.app_p:
    adr_sym r1, szSpoolPrefix
    bl str_copy_a32
    mov r6, r0
    mov r0, r5
    mov r1, sp
    bl fmt_i32_a32
    mov r0, r6
    mov r1, sp
    bl str_copy_a32
    adr_sym r1, szSpoolExt
    bl str_copy_a32
    mov r0, r4
.done:
    add sp, sp, #24
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc capture_cmd_to_buf_a32
;; @abi arm_ce
;; @in r0 = command string to execute
;; @out r0 = captured stdout WTF-8 buffer in arena (or 0 on failure)
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Captures stdout of a command into an arena buffer via a temporary spool file for `FOR /F`.
@proc capture_cmd_to_buf_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0 ; cmd

    bl make_spool_path_a32
    cmp r0, #0
    beq .fail
    mov r5, r0 ; spool path

    mov r0, r5
    bl to_u16_longpath_a32
    mov r1, #1 ; write_trunc
    bl hal_open_file
    cmn r0, #1
    beq .fail
    mov r6, r0 ; hSpool

    bl hal_get_ctx
    add r1, r0, #ARM32_CTX.fdTable
    ldr r7, [r1, #4] ; save old fdTable[1]
    str r6, [r1, #4]

    mov r0, r4
    mov r1, #0
    bl exec_line_str_a32

    bl hal_get_ctx
    add r1, r0, #ARM32_CTX.fdTable
    str r7, [r1, #4] ; restore fdTable[1]
    mov r0, r6
    bl hal_close_handle

    ; Read captured output back and delete spool file
    mov r0, r5
    bl to_u16_longpath_a32
    mov r1, #0
    bl hal_open_file
    cmn r0, #1
    beq .fail
    mov r6, r0

    mov_imm32 r0, 16384
    bl arena_alloc_a32
    mov r7, r0
    cmp r7, #0
    beq .cl_del
    mov r0, r6
    mov r1, r7
    mov_imm32 r2, 16380
    bl hal_read_handle
    mov r1, #0
    strb r1, [r7, r0]
.cl_del:
    mov r0, r6
    bl hal_close_handle
    mov r0, r5
    bl to_u16_longpath_a32
    bl hal_delete_file
    mov r0, r7
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr

.fail:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc exec_pipe_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32* (AST_PIPE)
;; @out r0 = pipeline right-stage exit code
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Runs concurrent `CreatePipe` pipeline on ARMNT (or `%TEMP%` spooled pipeline when redirected/WinCE!).
@proc exec_pipe_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0

    ; If stdout is currently redirected to an internal spool/file handle != hStdOut, or on WinCE,
    ; check if concurrent CreatePipe applies (only when fdTable[0]==hStdIn and fdTable[1]==hStdOut!)
    bl hal_get_ctx
    mov r5, r0
    add r1, r5, #ARM32_CTX.fdTable
    ldr r0, [r1, #0]
    ldr r2, [r5, #ARM32_CTX.hStdIn]
    cmp r0, r2
    bne .spool_pipe
    ldr r0, [r1, #4]
    ldr r2, [r5, #ARM32_CTX.hStdOut]
    cmp r0, r2
    bne .spool_pipe

    ldr r0, [r4, #AST_NODE_A32.pExtra]
    cmp r0, #0
    beq .spool_pipe
    mov r1, #0
    bl expand_phase34_a32
    mov r6, r0
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    cmp r0, #0
    beq .spool_pipe
    mov r1, #0
    bl expand_phase34_a32
    mov r1, r0
    mov r0, r6
    bl hal_run_pipe_concurrent
    cmn r0, #1
    beq .spool_pipe
    str r0, [r5, #ARM32_CTX.dwLastErrorLevel]
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.spool_pipe:
    ; Sequential `%TEMP%` spooled pipeline (isolated with SETLOCAL/ENDLOCAL per stage!)
    bl make_spool_path_a32
    cmp r0, #0
    beq .pipe_err
    mov r6, r0 ; spool path

    ; Stage 1: write left AST stdout into spool file
    mov r0, r6
    bl to_u16_longpath_a32
    mov r1, #1
    bl hal_open_file
    cmn r0, #1
    beq .pipe_err
    mov r7, r0 ; hOutSpool

    add r1, r5, #ARM32_CTX.fdTable
    ldr r8, [r1, #4] ; saved stdout
    str r7, [r1, #4]
    mov r0, #0
    mov r1, #0
    bl setlocal_push_a32
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    bl endlocal_pop_a32
    add r1, r5, #ARM32_CTX.fdTable
    str r8, [r1, #4]
    mov r0, r7
    bl hal_close_handle

    ; Stage 2: read right AST stdin from spool file
    mov r0, r6
    bl to_u16_longpath_a32
    mov r1, #0
    bl hal_open_file
    cmn r0, #1
    beq .del_err
    mov r7, r0 ; hInSpool

    add r1, r5, #ARM32_CTX.fdTable
    ldr r8, [r1, #0] ; saved stdin
    str r7, [r1, #0]
    mov r0, #0
    mov r1, #0
    bl setlocal_push_a32
    ldr r0, [r4, #AST_NODE_A32.pRight]
    bl exec_ast_a32
    mov r4, r0 ; right exit code
    bl endlocal_pop_a32
    add r1, r5, #ARM32_CTX.fdTable
    str r8, [r1, #0]
    mov r0, r7
    bl hal_close_handle

    mov r0, r6
    bl to_u16_longpath_a32
    bl hal_delete_file
    str r4, [r5, #ARM32_CTX.dwLastErrorLevel]
    mov r0, r4
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.del_err:
    mov r0, r6
    bl to_u16_longpath_a32
    bl hal_delete_file
.pipe_err:
    mov r0, #1
    str r0, [r5, #ARM32_CTX.dwLastErrorLevel]
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc goto_label_a32
;; @abi arm_ce
;; @in r0 = target label string (with or without leading `:`)
;; @out r0 = 0 on success (updates active BATCH_FRAME_A32.uFileOffLo/Hi), 1 if label not found
;; @err r0 = 1 when label is missing
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements `GOTO [:]label` and `GOTO :EOF` by scanning the active batch file.
@proc goto_label_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0x3A ; ':'
    bne .chk_eof
    add r4, r4, #1

.chk_eof:
    bl hal_get_ctx
    mov r5, r0
    ldr r1, [r5, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .err1
    sub r1, r1, #1
    lsl r1, r1, #6
    ldr r2, [r5, #ARM32_CTX.pBatchStack]
    add r6, r2, r1 ; active BATCH_FRAME_A32*

    ; Check `EOF` case-insensitively
    mov r0, r4
    adr_sym r1, szKwEof
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    bne .scan_file
    mvn r0, #0
    str r0, [r6, #BATCH_FRAME_A32.uFileOffHi]
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.scan_file:
    mov r0, r4
    bl str_len_a32
    mov r8, r0 ; target label len

    ; Rewind active frame to offset 0 (preserving BOM if already detected: 0, 2, or 3)
    mov r0, #0
    str r0, [r6, #BATCH_FRAME_A32.uFileOffLo]
    str r0, [r6, #BATCH_FRAME_A32.uFileOffHi]
    mov r0, #0
    strb r0, [r6, #BATCH_FRAME_A32.bBomKind]

    mov_imm32 r0, A32_LINE_MAX
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r7, r0 ; line buf

.ln_lp:
    mov r0, r6
    mov r1, r7
    bl batch_read_raw_line_a32
    cmp r0, #0
    beq .not_found
    mov r0, r7
    bl skip_ws_a32
    ldrb r1, [r0, #0]
    cmp r1, #0x3A ; ':'
    bne .ln_lp
    ldrb r1, [r0, #1]
    cmp r1, #0x3A ; skip `::` comments
    beq .ln_lp
    add r0, r0, #1
    bl skip_ws_a32
    mov r5, r0 ; candidate label start

    ; Compare r8 chars case-insensitively, followed by NUL/space/tab/colon
    mov r3, #0
.cmp_l:
    cmp r3, r8
    beq .chk_term
    ldrb r0, [r5, r3]
    cmp r0, #0
    beq .ln_lp
    push {r3, r4}
    bl ascii_upper_a32
    mov r2, r0
    ldr r3, [sp, #0]
    ldr r4, [sp, #4]
    ldrb r0, [r4, r3]
    push {r2, r3}
    bl ascii_upper_a32
    pop {r2, r3}
    mov r1, r0
    pop {r3, r4}
    cmp r1, r2
    bne .ln_lp
    add r3, r3, #1
    b .cmp_l

.chk_term:
    ldrb r0, [r5, r8]
    cmp r0, #0
    beq .found
    cmp r0, #0x20
    beq .found
    cmp r0, #0x09
    beq .found
    cmp r0, #0x3A
    beq .found
    b .ln_lp

.found:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.not_found:
    mvn r0, #0
    str r0, [r6, #BATCH_FRAME_A32.uFileOffHi]
.err1:
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc run_top_batch_frame_a32
;; @abi arm_ce
;; @in none
;; @out r0 = dwLastErrorLevel after batch/subroutine frame finishes
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Executes statements from the top BATCH_FRAME_A32 until EOF, `EXIT /B`, or `EXIT`, unwinding SETLOCALs!
@proc run_top_batch_frame_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    bl hal_get_ctx
    mov r4, r0
    ldr r5, [r4, #ARM32_CTX.dwBatchDepth]
    sub r1, r5, #1
    lsl r1, r1, #6
    ldr r2, [r4, #ARM32_CTX.pBatchStack]
    add r6, r2, r1 ; active frame

    mov_imm32 r0, A32_LINE_MAX
    bl arena_alloc_a32
    cmp r0, #0
    beq .unwind
    mov r7, r0

.b_loop:
    ldrb r0, [r4, #ARM32_CTX.bShouldExit]
    cmp r0, #0
    bne .unwind
    ldrb r0, [r4, #ARM32_CTX.bCtrlBreak]
    cmp r0, #0
    bne .unwind
    ldr r0, [r6, #BATCH_FRAME_A32.uFileOffHi]
    cmn r0, #1 ; `:EOF` sentinel `0xFFFFFFFF`
    beq .unwind

    ldr r0, [r4, #ARM32_CTX.pArenaPtr]
    str r0, [sp, #0]

    mov r0, r6
    mov r1, r7
    bl batch_read_logical_line_a32
    cmp r0, #0
    beq .unwind_restore_arena

    mov r0, r7
    mov r1, #1 ; in_batch = 1
    bl exec_line_str_a32

    ldr r0, [sp, #0]
    str r0, [r4, #ARM32_CTX.pArenaPtr]
    b .b_loop

.unwind_restore_arena:
    ldr r0, [sp, #0]
    str r0, [r4, #ARM32_CTX.pArenaPtr]
.unwind:
    ; Pop any SETLOCAL snapshots created inside this batch frame
    ldr r5, [r6, #BATCH_FRAME_A32.dwSetlocalBase]
.sl_pop_lp:
    ldr r0, [r4, #ARM32_CTX.dwSetlocalDepth]
    cmp r0, r5
    bls .pop_frame
    bl endlocal_pop_a32
    b .sl_pop_lp

.pop_frame:
    ldr r0, [r4, #ARM32_CTX.dwBatchDepth]
    cmp r0, #0
    beq .ret_ec
    sub r0, r0, #1
    str r0, [r4, #ARM32_CTX.dwBatchDepth]
.ret_ec:
    ldr r0, [r4, #ARM32_CTX.dwLastErrorLevel]
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc invoke_batch_or_sub_a32
;; @abi arm_ce
;; @in r0 = normalized batch path (or `:label` for CALL subroutine), r1 = argc, r2 = argv, r3 = raw_args_tail
;; @out r0 = exit code
;; @err r0 = 1 on stack overflow or missing label/file
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Pushes a BATCH_FRAME_A32 for `.BAT`/`.CMD` or `CALL :label` and runs it to completion.
@proc invoke_batch_or_sub_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r5, r0 ; path or `:label`
    mov r6, r1 ; argc
    mov r7, r2 ; argv
    mov r8, r3 ; raw tail

    bl hal_get_ctx
    mov r4, r0
    ldr r9, [r4, #ARM32_CTX.dwBatchDepth]
    cmp r9, #A32_BATCH_MAX
    bcs .err1

    lsl r0, r9, #6
    ldr r1, [r4, #ARM32_CTX.pBatchStack]
    add r10, r1, r0 ; new BATCH_FRAME_A32*
    mov r0, r10
    mov r1, #64
    bl mem_zero_a32

    ldr r0, [r4, #ARM32_CTX.dwSetlocalDepth]
    str r0, [r10, #BATCH_FRAME_A32.dwSetlocalBase]
    ldr r0, [r4, #ARM32_CTX.dwCodePage]
    str r0, [r10, #BATCH_FRAME_A32.dwSavedCodePage]
    str r8, [r10, #BATCH_FRAME_A32.pRawArgs]

    ; Copy argv[0..argc] (stripping quotes? NO — preserve raw tokens so `%1` has quotes and `%~1` strips quotes!)
    lsl r0, r6, #2
    add r0, r0, #8
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    str r0, [r10, #BATCH_FRAME_A32.pArgVec]
    str r6, [r10, #BATCH_FRAME_A32.dwArgCount]
    mov r1, r7
    lsl r2, r6, #2
    bl mem_copy_a32

    ; Check if `r5` starts with `:` -> subroutine `CALL :label` inside parent batch file!
    ldrb r0, [r5, #0]
    cmp r0, #0x3A
    bne .ext_bat_file
    cmp r9, #0
    beq .err1
    sub r0, r9, #1
    lsl r0, r0, #6
    ldr r1, [r4, #ARM32_CTX.pBatchStack]
    add r1, r1, r0
    ldr r0, [r1, #BATCH_FRAME_A32.pFilePath]
    str r0, [r10, #BATCH_FRAME_A32.pFilePath]
    mov r0, #1
    strb r0, [r10, #BATCH_FRAME_A32.bIsCallSub]
    add r9, r9, #1
    str r9, [r4, #ARM32_CTX.dwBatchDepth]

    mov r0, r5
    bl goto_label_a32
    cmp r0, #0
    bne .sub_missing
    bl run_top_batch_frame_a32
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.sub_missing:
    ldr r0, [r4, #ARM32_CTX.dwBatchDepth]
    sub r0, r0, #1
    str r0, [r4, #ARM32_CTX.dwBatchDepth]
    b .err1

.ext_bat_file:
    str r5, [r10, #BATCH_FRAME_A32.pFilePath]
    add r9, r9, #1
    str r9, [r4, #ARM32_CTX.dwBatchDepth]
    bl run_top_batch_frame_a32
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.err1:
    mov r0, #1
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc probe_file_with_pathext_a32
;; @abi arm_ce
;; @in r0 = candidate base path (normalized WTF-8), r1 = has_explicit_ext (1 if stem already has `.ext`, else 0)
;; @out r0 = resolved existing file path in arena (or 0 if not found)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Probes an executable/batch path directly and across `;`-separated `%PATHEXT%` extensions.
@proc probe_file_with_pathext_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0 ; base
    mov r5, r1 ; has_ext

    ; Probe exact path first (must exist and NOT be a directory!)
    mov r0, r4
    bl to_u16_longpath_a32
    bl hal_get_file_attr
    cmn r0, #1
    beq .try_exts
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .try_exts
    mov r0, r4
    b .done

.try_exts:
    adr_sym r0, szVarPathext
    bl env_get_a32
    cmp r0, #0
    bne .got_pe
    adr_sym r0, szDefPathext
.got_pe:
    mov r6, r0 ; cursor in PATHEXT

.pe_lp:
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .not_found
    cmp r0, #0x3B ; ';'
    bne .ext_st
    add r6, r6, #1
    b .pe_lp
.ext_st:
    mov r7, r6
.ext_sc:
    ldrb r0, [r7, #0]
    cmp r0, #0
    beq .got_ext
    cmp r0, #0x3B
    beq .got_ext
    add r7, r7, #1
    b .ext_sc

.got_ext:
    sub r5, r7, r6 ; ext len
    mov r0, r4
    bl str_len_a32
    add r0, r0, r5
    add r0, r0, #8
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_found
    mov r3, r0
    push {r3, r4}
    mov r1, r4
    bl str_copy_a32
    mov r1, r6
    mov r2, r5
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    pop {r3, r4}

    mov r0, r3
    push {r3, r4}
    bl to_u16_longpath_a32
    bl hal_get_file_attr
    pop {r3, r4}
    cmn r0, #1
    beq .nx_ext
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .nx_ext
    mov r0, r3
    b .done

.nx_ext:
    mov r6, r7
    b .pe_lp

.not_found:
    mov r0, #0
.done:
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc resolve_executable_a32
;; @abi arm_ce
;; @in r0 = unquoted command name
;; @out r0 = resolved full WTF-8 path in arena (or 0 if not found)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Resolves external `.COM`/`.EXE`/`.BAT`/`.CMD` across explicit path, CWD (honoring NoDefaultCurrentDirectoryInExePath), and `%PATH%`.
@proc resolve_executable_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0

    ; Check if r4 contains ``, `/`, or `:` -> explicit path, do NOT search %PATH%
    mov r1, #0
    mov r5, #0 ; has_sep
    mov r6, #0 ; has_dot_in_leaf
.ch_lp:
    ldrb r0, [r4, r1]
    cmp r0, #0
    beq .ch_done
    cmp r0, #0x5C
    beq .sep_hit
    cmp r0, #0x2F
    beq .sep_hit
    cmp r0, #0x3A
    beq .sep_hit
    cmp r0, #0x2E
    bne .ch_nx
    mov r6, #1
    b .ch_nx
.sep_hit:
    mov r5, #1
    mov r6, #0
.ch_nx:
    add r1, r1, #1
    b .ch_lp

.ch_done:
    cmp r5, #0
    beq .search_cwd_path
    mov r0, r4
    bl norm_path_a32
    mov r1, r6
    bl probe_file_with_pathext_a32
    b .done

.search_cwd_path:
    ; Check `%NoDefaultCurrentDirectoryInExePath%` (Vista..11 security env var!)
    adr_sym r0, szNoDefCwdEnv
    bl env_get_raw_a32
    cmp r0, #0
    bne .search_path_only

    mov r0, r4
    bl norm_path_a32
    mov r1, r6
    bl probe_file_with_pathext_a32
    cmp r0, #0
    bne .done

.search_path_only:
    adr_sym r0, szVarPath
    bl env_get_a32
    cmp r0, #0
    beq .not_found
    mov r7, r0 ; PATH cursor

.p_lp:
    ldrb r0, [r7, #0]
    cmp r0, #0
    beq .not_found
    cmp r0, #0x3B
    bne .p_st
    add r7, r7, #1
    b .p_lp
.p_st:
    mov r8, r7
.p_sc:
    ldrb r0, [r8, #0]
    cmp r0, #0
    beq .p_seg
    cmp r0, #0x3B
    beq .p_seg
    add r8, r8, #1
    b .p_sc

.p_seg:
    sub r2, r8, r7
    cmp r2, #0
    beq .p_nx
    mov r0, r4
    push {r2, r3}
    bl str_len_a32
    pop {r2, r3}
    add r0, r0, r2
    add r0, r0, #16
    push {r2, r3}
    bl arena_alloc_a32
    mov r5, r0
    pop {r2, r3}
    cmp r5, #0
    beq .not_found
    mov r0, r5
    mov r1, r7
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r5
    bl strip_outer_quotes_a32
    mov r5, r0
    bl str_len_a32
    add r2, r0, #280
    mov r0, r2
    bl arena_alloc_a32
    cmp r0, #0
    beq .not_found
    mov r3, r0
    push {r3, r4}
    mov r1, r5
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .p_app
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.p_app:
    ldr r1, [sp, #4] ; r4
    bl str_copy_a32
    pop {r3, r4}

    mov r0, r3
    bl norm_path_a32
    mov r1, r6
    bl probe_file_with_pathext_a32
    cmp r0, #0
    bne .done
.p_nx:
    mov r7, r8
    b .p_lp

.not_found:
    mov r0, #0
.done:
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc check_ext_kind_a32
;; @abi arm_ce
;; @in r0 = resolved file path
;; @out r0 = 1 if `.BAT`/`.CMD`, 2 if `.COM`, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Classifies file extension for batch script vs `.COM` vm8086 vs native PE dispatch.
@proc check_ext_kind_a32
    push {r4, lr}
    mov r4, r0
    bl str_len_a32
    cmp r0, #4
    bcc .other
    sub r0, r0, #4
    add r4, r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0x2E ; '.'
    bne .other

    mov r0, r4
    adr_sym r1, szExtBat
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .is_bat

    mov r0, r4
    adr_sym r1, szExtCmd
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .is_bat

    mov r0, r4
    adr_sym r1, szExtCom
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .is_com

.other:
    mov r0, #0
    pop {r4, lr}
    bx lr
.is_bat:
    mov r0, #1
    pop {r4, lr}
    bx lr
.is_com:
    mov r0, #2
    pop {r4, lr}
    bx lr
@endproc

;; @proc launch_resolved_target_a32
;; @abi arm_ce
;; @in r0 = resolved path, r1 = argc, r2 = raw_argv, r3 = raw_tail, [sp+0] = is_call_or_wait (bit0=is_call, bit1=wait)
;; @out r0 = exit code
;; @err r0 = 9009 / 1 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Executes a resolved `.BAT`/`.CMD`, `.COM` (via vm8086), or `.EXE` (via CreateProcessW with MZ vm8086 fallback).
@proc launch_resolved_target_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #4
    ldr r8, [sp, #32] ; flags (bit0=is_call, bit1=wait)
    mov r4, r0 ; path
    mov r5, r1 ; argc
    mov r6, r2 ; argv
    mov r7, r3 ; raw tail

    mov r0, r4
    bl check_ext_kind_a32
    cmp r0, #1
    beq .do_bat
    cmp r0, #2
    beq .do_com

    ; Native PE `.EXE` (or 16-bit DOS MZ `.EXE`!): build full command line `"path" raw_tail`
    mov r0, r4
    bl str_len_a32
    mov r9, r0
    mov r0, r7
    bl str_len_a32
    add r0, r0, r9
    add r0, r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r9, r0
    mov r1, #0x22
    strb r1, [r9, #0]
    add r0, r9, #1
    mov r1, r4
    bl str_copy_a32
    mov r1, #0x22
    strb r1, [r0, #0]
    add r0, r0, #1
    ldrb r1, [r7, #0]
    cmp r1, #0
    beq .fin_cl
    mov r1, #0x20
    strb r1, [r0, #0]
    add r0, r0, #1
    mov r1, r7
    bl str_copy_a32
    b .spawn_pe
.fin_cl:
    mov r1, #0
    strb r1, [r0, #0]

.spawn_pe:
    mov r0, r4
    bl to_u16_longpath_a32
    mov r5, r0 ; u16 app
    mov r0, r9
    bl wtf8_to_u16_a32
    mov r6, r0 ; u16 cmdline
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r0, r0, r1
    bl wtf8_to_u16_a32
    mov r2, r0 ; u16 cwd
    mov r0, r5
    mov r1, r6
    lsr r3, r8, #1
    and r3, r3, #1 ; waitFlag
    bl hal_spawn_process
    cmn r0, #2 ; -2 -> 16-bit DOS MZ `.EXE` handoff to vm8086!
    beq .do_com
    cmn r0, #1
    beq .err1
    b .done

.do_com:
    ; Prepend leading space to raw tail for PSP:0x80 DOS convention (` arg1 arg2`)
    mov r0, r7
    bl str_len_a32
    add r0, r0, #8
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r9, r0
    ldrb r1, [r7, #0]
    cmp r1, #0
    beq .empty_ct
    mov r1, #0x20
    strb r1, [r9, #0]
    add r0, r9, #1
    mov r1, r7
    bl str_copy_a32
    b .go_vm
.empty_ct:
    mov r1, #0
    strb r1, [r9, #0]
.go_vm:
    mov r0, r4
    mov r1, r9
    bl vm86_run_image_a32
    cmp r1, #0
    bne .vm_err
    b .done
.vm_err:
    mov r0, r1
    bl print_err_code_a32
    mov r0, #1
    b .done

.do_bat:
    ; If currently inside a batch file AND `is_call == 0`, chain (replace current batch frame)!
    tst r8, #1
    bne .call_bat
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .call_bat
    ; Mark caller batch frame `:EOF` after child batch finishes so un-CALLed batch chains!
    sub r1, r1, #1
    lsl r1, r1, #6
    ldr r2, [r0, #ARM32_CTX.pBatchStack]
    add r2, r2, r1
    mvn r3, #0
    str r3, [r2, #BATCH_FRAME_A32.uFileOffHi]
.call_bat:
    mov r0, r4
    mov r1, r5
    mov r2, r6
    mov r3, r7
    bl invoke_batch_or_sub_a32
    b .done

.err1:
    mov r0, #1
.done:
    add sp, sp, #4
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc exec_simple_cmd_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32* (AST_SIMPLE_CMD)
;; @out r0 = exit code
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Expands Phase 3/4 arguments, applies FD 0..9 redirections, and dispatches built-ins, batch scripts, PE, or vm8086!
@proc exec_simple_cmd_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #64 ; redir save block (48B) at sp+8, stack arg at sp+0
    mov r4, r0
    ldr r5, [r4, #AST_NODE_A32.dwArgc]

    ; Apply redirections even when dwArgc == 0 (e.g. `> empty.txt` truncates file!)
    ldr r0, [r4, #AST_NODE_A32.pRedir]
    add r1, sp, #8
    bl apply_redirs_a32
    cmp r0, #0
    bne .redir_failed

    cmp r5, #0
    beq .ok_restore

    ; Build unquoted expanded argv (r7) AND raw-quote-preserved expanded argv (r8, for `%1` in batch CALLs!)
    lsl r0, r5, #2
    add r0, r0, #8
    bl arena_alloc_a32
    mov r7, r0
    lsl r0, r5, #2
    add r0, r0, #8
    bl arena_alloc_a32
    mov r8, r0
    cmp r7, #0
    beq .err_restore
    cmp r8, #0
    beq .err_restore

    ldr r6, [r4, #AST_NODE_A32.pArgs]
    mov r9, #0
.exp_lp:
    cmp r9, r5
    bcs .exp_done
    lsl r1, r9, #2
    ldr r0, [r6, r1]
    mov r1, #0 ; keep quotes for raw_argv (ECHO body, batch `%1`, etc.)
    bl expand_phase34_a32
    lsl r1, r9, #2
    str r0, [r8, r1]

    lsl r1, r9, #2
    ldr r0, [r6, r1]
    mov r1, #1 ; strip quotes & carets for unquoted argv!
    bl expand_phase34_a32
    lsl r1, r9, #2
    str r0, [r7, r1]
    add r9, r9, #1
    b .exp_lp

.exp_done:
    ldr r9, [r7, #0] ; cmd0 (unquoted)
    ldrb r0, [r9, #0]
    cmp r0, #0
    beq .ok_restore

    ; Batch command echo if in batch, bEchoOn != 0, and !(bFlags & AST_FLAG_NO_ECHO)
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .after_echo
    ldrb r1, [r0, #ARM32_CTX.bEchoOn]
    cmp r1, #0
    beq .after_echo
    ldrb r1, [r4, #AST_NODE_A32.bFlags]
    tst r1, #AST_FLAG_NO_ECHO
    bne .after_echo
    bl print_prompt_a32
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    mov r1, #0
    bl expand_phase34_a32
    bl out_str_a32
    bl out_crlf_a32

.after_echo:
    ; Bare drive switch `X:` (len == 2 and [1] == ':')
    mov r0, r9
    bl str_len_a32
    cmp r0, #2
    bne .chk_builtin
    ldrb r0, [r9, #1]
    cmp r0, #0x3A
    bne .chk_builtin
    mov r0, r9
    bl norm_path_a32
    bl set_drv_cwd_a32
    mov r10, #0
    b .restore_ret

.chk_builtin:
    mov r0, r9
    bl lookup_builtin_id_a32
    cmp r0, #CMD_NONE
    beq .external_cmd
    mov r10, r0 ; cmd_id

    cmp r10, #CMD_ECHO
    beq .c_echo
    cmp r10, #CMD_SET
    beq .c_set
    cmp r10, #CMD_SETLOCAL
    beq .c_setlocal
    cmp r10, #CMD_ENDLOCAL
    beq .c_endlocal
    cmp r10, #CMD_CD
    beq .c_cd
    cmp r10, #CMD_PUSHD
    beq .c_pushd
    cmp r10, #CMD_POPD
    beq .c_popd
    cmp r10, #CMD_MD
    beq .c_md
    cmp r10, #CMD_RD
    beq .c_rd
    cmp r10, #CMD_MKLINK
    beq .c_mklink
    cmp r10, #CMD_DEL
    beq .c_del
    cmp r10, #CMD_COPY
    beq .c_copy
    cmp r10, #CMD_MOVE
    beq .c_move
    cmp r10, #CMD_REN
    beq .c_ren
    cmp r10, #CMD_DIR
    beq .c_dir
    cmp r10, #CMD_TYPE
    beq .c_type_tv
    cmp r10, #CMD_TRUENAME
    beq .c_type_tv
    cmp r10, #CMD_VOL
    beq .c_type_tv
    cmp r10, #CMD_VER
    beq .c_ver
    cmp r10, #CMD_ASSOC
    beq .c_assoc
    cmp r10, #CMD_FTYPE
    beq .c_ftype
    cmp r10, #CMD_COLOR
    beq .c_color
    cmp r10, #CMD_DOSKEY
    beq .c_doskey
    cmp r10, #CMD_GOTO
    beq .c_goto
    cmp r10, #CMD_CALL
    beq .c_call
    cmp r10, #CMD_START
    beq .c_start
    cmp r10, #CMD_CMD
    beq .c_subcmd
    cmp r10, #CMD_LH
    beq .c_lh

    ; Remaining misc built-ins (CLS, TITLE, CHCP, PAUSE, PATH, PROMPT, DPATH, DATE, TIME, BREAK, VERIFY, KEYS, CTTY, MODE, SHIFT, EXIT)
    mov r0, r10
    mov r1, r4
    mov r2, r5
    mov r3, r7
    bl bi_misc_simple_a32
    mov r10, r0
    b .restore_ret

.c_echo:
    mov r0, r4
    mov r1, r5
    mov r2, r8 ; raw argv preserves quotes in ECHO!
    bl bi_echo_a32
    mov r10, r0
    b .restore_ret
.c_set:
    mov r0, r4
    bl bi_set_a32
    mov r10, r0
    b .restore_ret
.c_setlocal:
    mov r0, r5
    mov r1, r7
    bl setlocal_push_a32
    mov r10, r0
    b .restore_ret
.c_endlocal:
    bl endlocal_pop_a32
    mov r10, r0
    b .restore_ret
.c_cd:
    mov r0, r4
    bl bi_cd_a32
    mov r10, r0
    b .restore_ret
.c_pushd:
    mov r0, r4
    bl bi_pushd_a32
    mov r10, r0
    b .restore_ret
.c_popd:
    bl bi_popd_a32
    mov r10, r0
    b .restore_ret
.c_md:
    mov r0, r5
    mov r1, r7
    bl bi_md_a32
    mov r10, r0
    b .restore_ret
.c_rd:
    mov r0, r5
    mov r1, r7
    bl bi_rd_a32
    mov r10, r0
    b .restore_ret
.c_mklink:
    mov r0, r5
    mov r1, r7
    bl bi_mklink_a32
    mov r10, r0
    b .restore_ret
.c_del:
    mov r0, r5
    mov r1, r7
    bl bi_del_a32
    mov r10, r0
    b .restore_ret
.c_copy:
    mov r0, r5
    mov r1, r7
    bl bi_copy_a32
    mov r10, r0
    b .restore_ret
.c_move:
    mov r0, r5
    mov r1, r7
    mov r2, #0
    bl bi_move_ren_a32
    mov r10, r0
    b .restore_ret
.c_ren:
    mov r0, r5
    mov r1, r7
    mov r2, #1
    bl bi_move_ren_a32
    mov r10, r0
    b .restore_ret
.c_dir:
    mov r0, r5
    mov r1, r7
    bl bi_dir_a32
    mov r10, r0
    b .restore_ret
.c_type_tv:
    mov r0, r10
    mov r1, r5
    mov r2, r7
    bl bi_type_truename_vol_a32
    mov r10, r0
    b .restore_ret
.c_ver:
    bl bi_ver_a32
    mov r10, r0
    b .restore_ret
.c_assoc:
    mov r0, r4
    mov r1, #0
    bl bi_assoc_ftype_a32
    mov r10, r0
    b .restore_ret
.c_ftype:
    mov r0, r4
    mov r1, #1
    bl bi_assoc_ftype_a32
    mov r10, r0
    b .restore_ret
.c_color:
    mov r0, r5
    mov r1, r7
    bl bi_color_a32
    mov r10, r0
    b .restore_ret
.c_doskey:
    mov r0, r4
    mov r1, r5
    mov r2, r7
    bl bi_doskey_a32
    mov r10, r0
    b .restore_ret

.c_goto:
    cmp r5, #2
    bcc .err_restore
    ldr r0, [r7, #4]
    bl goto_label_a32
    mov r10, r0
    b .restore_ret

.c_lh:
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    mov r1, #0
    bl exec_line_str_a32
    mov r10, r0
    b .restore_ret

.c_call:
    cmp r5, #2
    bcc .ok_restore
    ldr r9, [r7, #4] ; target after `CALL`
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    mov r6, r0 ; tail starting at target

    ; Check `CALL :label args...`
    ldrb r0, [r9, #0]
    cmp r0, #0x3A
    bne .call_ext_or_bi
    mov r0, r6
    bl extract_raw_tail_a32
    mov r3, r0 ; `%*` after `:label`
    mov r0, r9
    sub r1, r5, #1
    add r2, r8, #4 ; raw_argv + 1 (`%0 = :label`, `%1 = arg1`, ...)
    bl invoke_batch_or_sub_a32
    mov r10, r0
    b .restore_ret

.call_ext_or_bi:
    ; Check if target is a built-in (e.g. `CALL SET VAR=val` or `CALL ECHO %X%` re-expansion!)
    mov r0, r9
    bl lookup_builtin_id_a32
    cmp r0, #CMD_NONE
    beq .call_file
    mov r0, r6
    mov r1, #0
    bl exec_line_str_a32
    mov r10, r0
    b .restore_ret
.call_file:
    mov r0, r9
    bl resolve_executable_a32
    cmp r0, #0
    beq .bad_cmd
    mov r9, r0
    mov r0, r6
    bl extract_raw_tail_a32
    mov r3, r0
    mov r0, #3 ; is_call=1 | wait=1
    str r0, [sp, #0]
    mov r0, r9
    sub r1, r5, #1
    add r2, r8, #4
    bl launch_resolved_target_a32
    mov r10, r0
    b .restore_ret

.c_start:
    ; Parse `/B`, `/WAIT`, `/D path`, optional `"title"`, then execute remaining command
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    mov r6, r0
.st_sw:
    mov r0, r6
    bl skip_ws_a32
    mov r6, r0
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .ok_restore
    cmp r0, #0x22 ; optional `"title"` if followed by more tokens
    bne .st_chk_sl
    mov r0, r6
    bl scan_cmd_token_a32
    mov r2, r1
    push {r2, r3}
    mov r0, r2
    bl skip_ws_a32
    ldrb r0, [r0, #0]
    pop {r2, r3}
    cmp r0, #0
    beq .st_exec
    mov r6, r2
    b .st_sw
.st_chk_sl:
    cmp r0, #0x2F
    bne .st_exec
    ldrb r0, [r6, #1]
    bl ascii_upper_a32
    cmp r0, #0x44 ; `/D path`
    bne .st_sk1
    add r0, r6, #2
    bl scan_cmd_token_a32
    mov r6, r1
    bl strip_outer_quotes_a32
    bl norm_path_a32
    bl set_drv_cwd_a32
    b .st_sw
.st_sk1:
    mov r0, r6
    bl scan_cmd_token_a32
    mov r6, r1
    b .st_sw
.st_exec:
    mov r0, r6
    mov r1, #0
    bl exec_line_str_a32
    mov r10, r0
    b .restore_ret

.c_subcmd:
    ; Spawn subshell `pSelfExePath` with raw tail!
    bl hal_get_ctx
    ldr r9, [r0, #ARM32_CTX.pSelfExePath]
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    mov r3, r0
    mov r0, #2 ; wait=1
    str r0, [sp, #0]
    mov r0, r9
    mov r1, r5
    mov r2, r8
    bl launch_resolved_target_a32
    mov r10, r0
    b .restore_ret

.external_cmd:
    mov r0, r9
    bl resolve_executable_a32
    cmp r0, #0
    beq .bad_cmd
    mov r9, r0
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    mov r3, r0
    mov r0, #2 ; is_call=0, wait=1
    str r0, [sp, #0]
    mov r0, r9
    mov r1, r5
    mov r2, r8
    bl launch_resolved_target_a32
    mov r10, r0
    b .restore_ret

.bad_cmd:
    mov_imm32 r0, ERR_BAD_COMMAND
    bl print_err_code_a32
    mov_imm32 r10, 9009
    b .restore_ret

.ok_restore:
    mov r10, #0
    b .restore_ret
.err_restore:
    mov r10, #1
.restore_ret:
    add r0, sp, #8
    bl restore_redirs_a32
    bl hal_get_ctx
    str r10, [r0, #ARM32_CTX.dwLastErrorLevel]
    mov r0, r10
    add sp, sp, #64
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.redir_failed:
    bl hal_get_ctx
    mov r1, #1
    str r1, [r0, #ARM32_CTX.dwLastErrorLevel]
    mov r0, #1
    add sp, sp, #64
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc exec_ast_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*
;; @out r0 = exit code
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Recursive Arena AST evaluator across simple commands, blocks, `&`, `&&`, `||`, `|`, `IF`, and `FOR`.
@proc exec_ast_a32
    push {r4, r5, lr}
    sub sp, sp, #52 ; redir save block (48B) at sp+0 for AST_BLOCK
    mov r4, r0
    cmp r4, #0
    beq .ok0

    bl hal_get_ctx
    ldrb r1, [r0, #ARM32_CTX.bShouldExit]
    cmp r1, #0
    bne .cur_err
    ldrb r1, [r0, #ARM32_CTX.bCtrlBreak]
    cmp r1, #0
    bne .cur_err

    ldrb r1, [r4, #AST_NODE_A32.bKind]
    cmp r1, #AST_SIMPLE_CMD
    beq .do_simple
    cmp r1, #AST_BLOCK
    beq .do_block
    cmp r1, #AST_SEQ
    beq .do_seq
    cmp r1, #AST_AND
    beq .do_and
    cmp r1, #AST_OR
    beq .do_or
    cmp r1, #AST_PIPE
    beq .do_pipe
    cmp r1, #AST_IF
    beq .do_if
    cmp r1, #AST_FOR
    beq .do_for
    ; AST_REM / AST_NOP -> leave dwLastErrorLevel unchanged (or 0)
    b .cur_err

.do_simple:
    mov r0, r4
    bl exec_simple_cmd_a32
    b .done

.do_block:
    ldr r0, [r4, #AST_NODE_A32.pRedir]
    mov r1, sp
    bl apply_redirs_a32
    cmp r0, #0
    bne .blk_fail
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    mov r5, r0
    mov r0, sp
    bl restore_redirs_a32
    mov r0, r5
    b .done
.blk_fail:
    bl hal_get_ctx
    mov r1, #1
    str r1, [r0, #ARM32_CTX.dwLastErrorLevel]
    mov r0, #1
    b .done

.do_seq:
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    ldr r0, [r4, #AST_NODE_A32.pRight]
    bl exec_ast_a32
    b .done

.do_and:
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    cmp r0, #0
    bne .done
    ldr r0, [r4, #AST_NODE_A32.pRight]
    bl exec_ast_a32
    b .done

.do_or:
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    cmp r0, #0
    beq .done
    ldr r0, [r4, #AST_NODE_A32.pRight]
    bl exec_ast_a32
    b .done

.do_pipe:
    mov r0, r4
    bl exec_pipe_a32
    b .done

.do_if:
    mov r0, r4
    bl eval_if_cond_a32
    cmp r0, #0
    beq .if_else
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    b .done
.if_else:
    ldr r0, [r4, #AST_NODE_A32.pRight]
    cmp r0, #0
    beq .cur_err
    bl exec_ast_a32
    b .done

.do_for:
    mov r0, r4
    bl exec_for_a32
    b .done

.cur_err:
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwLastErrorLevel]
    b .done
.ok0:
    mov r0, #0
.done:
    add sp, sp, #52
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc exec_line_str_a32
;; @abi arm_ce
;; @in r0 = logical line string, r1 = in_batch (1 = batch file, 0 = interactive / `/C`)
;; @out r0 = exit code
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Runs Phase 1 `%` expansion, builds Arena AST, and executes it.
@proc exec_line_str_a32
    push {r4, lr}
    bl expand_phase1_a32
    cmp r0, #0
    beq .ok0
    bl parse_line_ast_a32
    cmp r0, #0
    beq .cur_ec
    bl exec_ast_a32
    pop {r4, lr}
    bx lr
.cur_ec:
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwLastErrorLevel]
    pop {r4, lr}
    bx lr
.ok0:
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc strip_cmd_s_quotes_a32
;; @abi arm_ce
;; @in r0 = raw `/C` or `/K` tail, r1 = force_s_strip (1 if `/S` was specified)
;; @out r0 = command string with outer quotes stripped per `cmd.exe`'s 5-rule `/C`/`/S` heuristic
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements the authentic `cmd.exe` `/C` + `/S` outer-quote-stripping algorithm.
@proc strip_cmd_s_quotes_a32
    push {r4, r5, r6, lr}
    mov r5, r1 ; force_s
    bl skip_ws_a32
    mov r4, r0
    bl str_len_a32
    mov r6, r0
    cmp r6, #2
    bcc .keep
    ldrb r0, [r4, #0]
    cmp r0, #0x22
    bne .keep
    sub r1, r6, #1
    ldrb r0, [r4, r1]
    cmp r0, #0x22
    bne .keep

    cmp r5, #0
    bne .do_strip ; `/S` always strips first and last quote!

    ; Without `/S`: count total quotes — if != 2, keep outer quotes UNLESS special chars?
    ; Wait: Rule 1: exactly 2 quotes AND any special char `&<>()@^|` -> strip! Or if exactly 2 quotes and first token is NOT executable?
    ; Actually in cmd.exe: if quote_count == 2 and has `&<>()@^|` -> strip; if quote_count == 2 and no whitespace -> strip!
    mov r1, #0
    mov r2, #0 ; quote_cnt
    mov r3, #0 ; has_special
.q_sc:
    ldrb r0, [r4, r1]
    cmp r0, #0
    beq .q_done
    cmp r0, #0x22
    bne .q_sp
    add r2, r2, #1
    b .q_nx
.q_sp:
    cmp r0, #0x26
    beq .sp_hit
    cmp r0, #0x3C
    beq .sp_hit
    cmp r0, #0x3E
    beq .sp_hit
    cmp r0, #0x28
    beq .sp_hit
    cmp r0, #0x29
    beq .sp_hit
    cmp r0, #0x7C
    bne .q_nx
.sp_hit:
    mov r3, #1
.q_nx:
    add r1, r1, #1
    b .q_sc
.q_done:
    cmp r2, #2
    bne .keep
    cmp r3, #0
    beq .keep
.do_strip:
    mov r0, r4
    bl strip_outer_quotes_a32
    pop {r4, r5, r6, lr}
    bx lr
.keep:
    mov r0, r4
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc shell_main_a32
;; @abi arm_ce
;; @in none
;; @out r0 = process exit code
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Initializes the ARM32 platform HAL, parses startup switches (`/C`, `/K`, `/S`, `/Q`, `/D`, `/A`, `/U`, `/E`, `/V`, `/F`, `/T`), runs AutoRun, and enters `/C` or interactive REPL!
@proc shell_main_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    ; Allocate 4,160-byte stack frame so __chkstk (> 4 KiB guard-page probe) is exercised cleanly on ARMNT!
    mov_imm32 r4, 1040 ; 1040 words = 4,160 bytes for __chkstk!
    sub sp, sp, #4096
    sub sp, sp, #64

    bl hal_get_ctx
    mov r4, r0
    mov r0, r4
    bl hal_init_platform
    cmp r0, #0
    bne .fatal_exit

    ; Parse startup command line past argv[0] (except on WinCE where GetCommandLineW has NO argv[0]!)
    ldr r5, [r4, #ARM32_CTX.pCmdLineRaw]
    cmp r5, #0
    beq .repl_start
    ldrb r0, [r4, #ARM32_CTX.bIsWinCe]
    cmp r0, #0
    bne .parse_switches
    ; Skip argv[0] executable path on ARMNT
    mov r0, r5
    bl scan_cmd_token_a32
    mov r5, r1

.parse_switches:
    mov r6, #0 ; mode: 0=REPL, 1=/C, 2=/K
    mov r7, #0 ; /S flag
    mov r8, #0 ; /D skip AutoRun flag
    mov r9, #0 ; /Q quiet flag

.sw_loop:
    mov r0, r5
    bl skip_ws_a32
    mov r5, r0
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .after_switches
    cmp r0, #0x2F ; '/'
    bne .after_switches

    ldrb r0, [r5, #1]
    bl ascii_upper_a32
    cmp r0, #0x43 ; '/C'
    beq .got_c
    cmp r0, #0x4B ; '/K'
    beq .got_k
    cmp r0, #0x53 ; '/S'
    beq .sw_s
    cmp r0, #0x44 ; '/D'
    beq .sw_d
    cmp r0, #0x51 ; '/Q'
    beq .sw_q
    cmp r0, #0x55 ; '/U'
    beq .sw_u
    cmp r0, #0x41 ; '/A'
    beq .sw_a
    cmp r0, #0x56 ; '/V[:ON|:OFF]'
    beq .sw_v
    cmp r0, #0x45 ; '/E[:ON|:OFF]'
    beq .sw_e
    cmp r0, #0x46 ; '/F[:ON|:OFF]'
    beq .sw_f
    ; Skip unknown switch token (e.g. `/T:bf`, `/P`)
    mov r0, r5
    bl scan_cmd_token_a32
    mov r5, r1
    b .sw_loop

.sw_s:
    mov r7, #1
    add r5, r5, #2
    b .sw_loop
.sw_d:
    mov r8, #1
    add r5, r5, #2
    b .sw_loop
.sw_q:
    mov r9, #1
    mov r0, #0
    strb r0, [r4, #ARM32_CTX.bEchoOn]
    add r5, r5, #2
    b .sw_loop
.sw_u:
    mov r0, #1
    strb r0, [r4, #ARM32_CTX.bUnicodeOut]
    add r5, r5, #2
    b .sw_loop
.sw_a:
    mov r0, #0
    strb r0, [r4, #ARM32_CTX.bUnicodeOut]
    add r5, r5, #2
    b .sw_loop
.sw_v:
    mov r0, r5
    bl scan_cmd_token_a32
    mov r5, r1
    ; Default `/V` or `/V:ON` enables delayed expansion; `/V:OFF` disables
    mov r1, #1
    ldrb r2, [r0, #2]
    cmp r2, #0x3A
    bne .set_v
    ldrb r2, [r0, #4]
    push {r1, r2}
    mov r0, r2
    bl ascii_upper_a32
    mov r3, r0
    pop {r1, r2}
    cmp r3, #0x46 ; 'F' in `:OFF`
    bne .set_v
    mov r1, #0
.set_v:
    strb r1, [r4, #ARM32_CTX.bDelayedExp]
    b .sw_loop

.sw_e:
    mov r0, r5
    bl scan_cmd_token_a32
    mov r5, r1
    mov r1, #1
    ldrb r2, [r0, #2]
    cmp r2, #0x3A
    bne .set_e
    ldrb r2, [r0, #4]
    push {r1, r2}
    mov r0, r2
    bl ascii_upper_a32
    mov r3, r0
    pop {r1, r2}
    cmp r3, #0x46 ; 'F' in `:OFF`
    bne .set_e
    mov r1, #0
.set_e:
    strb r1, [r4, #ARM32_CTX.bExtensions]
    b .sw_loop

.sw_f:
    mov r0, r5
    bl scan_cmd_token_a32
    mov r5, r1
    b .sw_loop

.got_c:
    mov r6, #1
    add r5, r5, #2
    b .after_switches
.got_k:
    mov r6, #2
    add r5, r5, #2

.after_switches:
    ; Execute HKCU\Software\Microsoft\Command Processor\AutoRun unless `/D`!
    cmp r8, #0
    bne .chk_ck
    adr_sym r0, szRegCmdProcKey
    bl wtf8_to_u16_a32
    mov r10, r0
    adr_sym r0, szRegAutoRunVal
    bl wtf8_to_u16_a32
    mov r2, r0
    mov_imm32 r0, 0x80000001 ; HKCU
    mov r1, r10
    mov r3, sp               ; 512-byte buffer at sp+0
    bl hal_reg_query_str
    cmp r0, #0
    bne .chk_ck
    ldrb r0, [sp, #0]
    cmp r0, #0
    beq .chk_ck
    mov r0, sp
    mov r1, #0
    bl exec_line_str_a32

.chk_ck:
    cmp r6, #0
    beq .repl_banner
    ; `/C` or `/K` command execution
    mov r0, r5
    ldrb r1, [r0, #0]
    cmp r1, #0x20
    bne .strip_ck
    add r0, r0, #1
.strip_ck:
    mov r1, r7
    bl strip_cmd_s_quotes_a32
    mov r1, #0
    bl exec_line_str_a32
    cmp r6, #1 ; `/C` exits immediately!
    beq .normal_exit
    b .repl_loop

.repl_banner:
    cmp r9, #0
    bne .repl_loop
    bl bi_ver_a32
    bl out_crlf_a32

.repl_start:
.repl_loop:
    ldrb r0, [r4, #ARM32_CTX.bShouldExit]
    cmp r0, #0
    bne .normal_exit

    ; Reset per-statement bump arena at each top-level interactive prompt!
    bl arena_reset_a32

    mov r0, #0
    strb r0, [r4, #ARM32_CTX.bCtrlBreak]

    ldrb r0, [r4, #ARM32_CTX.bEchoOn]
    cmp r0, #0
    beq .rd_repl
    bl print_prompt_a32

.rd_repl:
    mov r0, sp
    mov_imm32 r1, 4000
    bl read_stdin_line_a32
    cmp r0, #0
    beq .normal_exit

    mov r0, sp
    bl doskey_add_history_a32
    mov r0, sp
    bl doskey_expand_macro_a32
    mov r1, #0
    bl exec_line_str_a32
    b .repl_loop

.normal_exit:
    ldrb r0, [r4, #ARM32_CTX.bShouldExit]
    cmp r0, #0
    bne .use_explicit_exit
    ldr r0, [r4, #ARM32_CTX.dwLastErrorLevel]
    b .fin_ret
.use_explicit_exit:
    ldr r0, [r4, #ARM32_CTX.dwExitCode]
.fin_ret:
    add sp, sp, #4096
    add sp, sp, #64
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.fatal_exit:
    add sp, sp, #4096
    add sp, sp, #64
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc
