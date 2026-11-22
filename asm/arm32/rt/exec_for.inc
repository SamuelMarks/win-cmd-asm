;; @file exec_for.inc
;; @isa armv4
;; @desc ARM32 FD 0..9 Redirection Engine, IF Evaluator, and FOR (plain, /D, /R, /L, /F) Engine.

@cpu armv4
@format inc

;; @proc apply_redirs_a32
;; @abi arm_ce
;; @in r0 = REDIR_NODE_A32* list, r1 = 48-byte save buffer on caller stack
;; @out r0 = 0 on success, 1 on file open error (restoring any partial handles)
;; @err r0 = 1 when input/output redirection file cannot be opened
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Saves fdTable[0..9] into r1[0..39] and evaluates redirections strictly left-to-right.
@proc apply_redirs_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #4
    mov r4, r0 ; redir node
    mov r5, r1 ; save block

    bl hal_get_ctx
    mov r6, r0
    ; Copy fdTable[0..9] (40 bytes) to save block r5[0..39], zero openedMask at r5[40]
    mov r0, r5
    add r1, r6, #ARM32_CTX.fdTable
    mov r2, #40
    bl mem_copy_a32
    mov r0, #0
    str r0, [r5, #40]

.r_lp:
    cmp r4, #0
    beq .ok0
    ldrb r7, [r4, #REDIR_NODE_A32.bKind]
    ldrb r8, [r4, #REDIR_NODE_A32.bFd]
    cmp r8, #9
    bhi .nx_r

    cmp r7, #A32_REDIR_DUP_OUT
    beq .do_dup
    cmp r7, #A32_REDIR_DUP_IN
    beq .do_dup

    ; File redirection: expand Phase 3/4 on pTarget
    ldr r0, [r4, #REDIR_NODE_A32.pTarget]
    mov r1, #1
    bl expand_phase34_a32
    mov r9, r0

    ; Check `NUL` device on WinCE -> map to `\Temp\_wcnul.tmp`
    mov r0, r9
    bl check_device_a32
    cmp r0, #DEV_NUL
    bne .norm_rpath
    ldrb r0, [r6, #ARM32_CTX.bIsWinCe]
    cmp r0, #0
    beq .winnt_nul
    adr_sym r9, szWinCeNulFile
    b .open_u16
.winnt_nul:
    adr_sym r9, szWinNtNulDev
    b .open_u16

.norm_rpath:
    mov r0, r9
    bl norm_path_a32
    mov r9, r0

.open_u16:
    mov r0, r9
    bl to_u16_longpath_a32
    mov r1, #0 ; read
    cmp r7, #A32_REDIR_IN
    beq .call_open
    mov r1, #1 ; write_trunc
    cmp r7, #A32_REDIR_OUT
    beq .call_open
    mov r1, #2 ; write_append
.call_open:
    bl hal_open_file
    cmn r0, #1
    beq .fail_open
    mov r9, r0 ; new handle

    ; If fd slot r8 already held a newly-opened handle in this frame, close previous handle
    ldr r0, [r5, #40]
    mov r1, #1
    lsl r1, r1, r8
    tst r0, r1
    beq .set_slot
    lsl r2, r8, #2
    add r3, r6, #ARM32_CTX.fdTable
    ldr r0, [r3, r2]
    bl hal_close_handle
.set_slot:
    ldr r0, [r5, #40]
    mov r1, #1
    lsl r1, r1, r8
    orr r0, r0, r1
    str r0, [r5, #40]
    lsl r2, r8, #2
    add r3, r6, #ARM32_CTX.fdTable
    str r9, [r3, r2]
    b .nx_r

.do_dup:
    ldrb r0, [r4, #REDIR_NODE_A32.bTargetFd]
    cmp r0, #9
    bhi .nx_r
    lsl r0, r0, #2
    add r3, r6, #ARM32_CTX.fdTable
    ldr r9, [r3, r0] ; source handle
    ; If fd slot r8 held an owned handle, close it and clear bit!
    ldr r0, [r5, #40]
    mov r1, #1
    lsl r1, r1, r8
    tst r0, r1
    beq .dup_set
    bic r0, r0, r1
    str r0, [r5, #40]
    lsl r2, r8, #2
    ldr r0, [r3, r2]
    bl hal_close_handle
.dup_set:
    lsl r2, r8, #2
    add r3, r6, #ARM32_CTX.fdTable
    str r9, [r3, r2]

.nx_r:
    ldr r4, [r4, #REDIR_NODE_A32.pNext]
    b .r_lp

.fail_open:
    mov r0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a32
    mov r0, r5
    bl restore_redirs_a32
    mov r0, #1
    add sp, sp, #4
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr

.ok0:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc restore_redirs_a32
;; @abi arm_ce
;; @in r0 = 48-byte save buffer filled by apply_redirs_a32
;; @out Closes all newly opened redirection handles and restores fdTable[0..9]
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Restores ARM32_CTX.fdTable[0..9] and closes owned redirection file handles.
@proc restore_redirs_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0
    bl hal_get_ctx
    mov r5, r0
    ldr r6, [r4, #40] ; openedMask
    mov r7, #0
.cl_lp:
    cmp r7, #10
    bcs .cp_back
    mov r0, #1
    lsl r0, r0, r7
    tst r6, r0
    beq .nx_fd
    lsl r1, r7, #2
    add r2, r5, #ARM32_CTX.fdTable
    ldr r0, [r2, r1]
    bl hal_close_handle
.nx_fd:
    add r7, r7, #1
    b .cl_lp

.cp_back:
    add r0, r5, #ARM32_CTX.fdTable
    mov r1, r4
    mov r2, #40
    bl mem_copy_a32
    mov r0, #0
    str r0, [r4, #40]
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc try_parse_strict_i32_a32
;; @abi arm_ce
;; @in r0 = candidate string
;; @out r0 = parsed i32, r1 = 1 if entire string is a valid signed integer, else 0
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Attempts strict numeric conversion for `IF ... EQU/NEQ/LSS/LEQ/GTR/GEQ`.
@proc try_parse_strict_i32_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    cmp r0, #0
    beq .no
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .no
    mov r5, #0 ; neg
    cmp r0, #0x2D
    bne .chk_pl
    mov r5, #1
    add r4, r4, #1
    b .chk_d
.chk_pl:
    cmp r0, #0x2B
    bne .chk_d
    add r4, r4, #1
.chk_d:
    ldrb r0, [r4, #0]
    cmp r0, #0x30
    bcc .no
    cmp r0, #0x39
    bhi .no
    mov r0, r4
    bl sa_parse_num_lit_a32
    cmp r2, #0
    bne .no
    ldrb r3, [r1, #0]
    cmp r3, #0
    bne .no
    cmp r5, #0
    beq .ok
    mov r2, #0
    sub r0, r2, r0
.ok:
    mov r1, #1
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.no:
    mov r0, #0
    mov r1, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc eval_if_cond_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32* (AST_IF)
;; @out r0 = 1 if condition holds, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Evaluates IF `[NOT]` `[/I]` (`ERRORLEVEL`, `EXIST`, `DEFINED`, `CMDEXTVERSION`, `==`, `EQU..GEQ`).
@proc eval_if_cond_a32
    push {r4, r5, r6, r7, r8, lr}
    sub sp, sp, #336 ; FIND_INFO_A32 at sp+0
    mov r4, r0
    ldrb r5, [r4, #AST_NODE_A32.bSubKind]
    ldrb r6, [r4, #AST_NODE_A32.bFlags]

    ; Expand lhs / unary arg (pStr1)
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    cmp r0, #0
    beq .false_raw
    mov r1, #0
    bl expand_phase34_a32
    mov r7, r0 ; lhs

    cmp r5, #A32_IF_ERRORLEVEL
    beq .c_errlvl
    cmp r5, #A32_IF_EXIST
    beq .c_exist
    cmp r5, #A32_IF_DEFINED
    beq .c_defined
    cmp r5, #A32_IF_CMDEXTVER
    beq .c_extver

    ; Binary comparison against rhs (pExtra)
    ldr r0, [r4, #AST_NODE_A32.pExtra]
    cmp r0, #0
    beq .false_raw
    mov r1, #0
    bl expand_phase34_a32
    mov r8, r0 ; rhs

    ; Try numeric comparison first when both unquoted lhs and rhs are valid integers!
    mov r0, r7
    bl try_parse_strict_i32_a32
    cmp r1, #0
    beq .str_cmp_if
    mov r2, r0
    push {r2, r3}
    mov r0, r8
    bl try_parse_strict_i32_a32
    mov r3, r0
    pop {r2, r12}
    cmp r1, #0
    beq .str_cmp_if

    ; Signed comparison of r2 vs r3!
    sub r0, r2, r3 ; Wait: avoid signed overflow by using `cmp r2, r3` directly!
    cmp r2, r3
    beq .ord_eq
    blt .ord_lt
    mov r0, #1
    b .eval_ord
.ord_lt:
    mvn r0, #0 ; -1
    b .eval_ord
.ord_eq:
    mov r0, #0
    b .eval_ord

.str_cmp_if:
    mov r2, #0
    tst r6, #AST_FLAG_IF_ICASE
    beq .do_scmp
    mov r2, #1
.do_scmp:
    mov r0, r7
    mov r1, r8
    bl str_cmp_a32

.eval_ord:
    cmp r5, #A32_IF_CMP_EQ
    beq .t_eq
    cmp r5, #A32_IF_CMP_NEQ
    beq .t_neq
    cmp r5, #A32_IF_CMP_LSS
    beq .t_lss
    cmp r5, #A32_IF_CMP_LEQ
    beq .t_leq
    cmp r5, #A32_IF_CMP_GTR
    beq .t_gtr
    ; GEQ
    cmp r0, #0
    bge .true_raw
    b .false_raw
.t_eq:
    cmp r0, #0
    beq .true_raw
    b .false_raw
.t_neq:
    cmp r0, #0
    bne .true_raw
    b .false_raw
.t_lss:
    cmp r0, #0
    blt .true_raw
    b .false_raw
.t_leq:
    cmp r0, #0
    ble .true_raw
    b .false_raw
.t_gtr:
    cmp r0, #0
    bgt .true_raw
    b .false_raw

.c_errlvl:
    mov r0, r7
    bl strip_outer_quotes_a32
    bl parse_signed_dec_a32
    mov r7, r0
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwLastErrorLevel]
    cmp r0, r7
    bge .true_raw
    b .false_raw

.c_extver:
    mov r0, r7
    bl strip_outer_quotes_a32
    bl parse_signed_dec_a32
    cmp r0, #2
    ble .true_raw
    b .false_raw

.c_defined:
    mov r0, r7
    bl strip_outer_quotes_a32
    bl env_get_raw_a32
    cmp r0, #0
    bne .true_raw
    b .false_raw

.c_exist:
    mov r0, r7
    bl strip_outer_quotes_a32
    mov r7, r0
    bl check_device_a32
    cmp r0, #DEV_NONE
    bne .true_raw
    mov r0, r7
    bl norm_path_a32
    mov r7, r0
    bl to_u16_longpath_a32
    mov r8, r0
    ; Try direct attribute check first (works for root `` / `C:` and exact files/dirs)
    bl hal_get_file_attr
    cmn r0, #1
    bne .true_raw
    ; Try wildcard FindFirstFileW
    mov r0, r8
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .false_raw
    bl hal_find_close
    b .true_raw

.true_raw:
    mov r0, #1
    b .apply_not
.false_raw:
    mov r0, #0
.apply_not:
    tst r6, #AST_FLAG_IF_NOT
    beq .done
    eor r0, r0, #1
.done:
    add sp, sp, #336
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc for_push_var_a32
;; @abi arm_ce
;; @in r0 = var char ('I'), r1 = value string
;; @out Pushes FOR variable binding onto ARM32_CTX.pForStack
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Binds a FOR loop variable on the nested FOR scope stack.
@proc for_push_var_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    bl hal_get_ctx
    ldr r2, [r0, #ARM32_CTX.dwForDepth]
    cmp r2, #A32_FOR_MAX
    bcs .done
    ldr r3, [r0, #ARM32_CTX.pForStack]
    lsl r1, r2, #3 ; * 8 (sizeof FOR_VAR_A32)
    add r3, r3, r1
    strb r4, [r3, #FOR_VAR_A32.bVarChar]
    mov r1, #1
    strb r1, [r3, #FOR_VAR_A32.bActive]
    str r5, [r3, #FOR_VAR_A32.pValue]
    add r2, r2, #1
    str r2, [r0, #ARM32_CTX.dwForDepth]
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc for_pop_vars_to_a32
;; @abi arm_ce
;; @in r0 = saved dwForDepth
;; @out Restores ARM32_CTX.dwForDepth to r0
;; @err none
;; @clobbers r0, r1, lr
;; @desc Unwinds FOR variable bindings back to r0.
@proc for_pop_vars_to_a32
    push {r4, lr}
    mov r4, r0
    bl hal_get_ctx
    str r4, [r0, #ARM32_CTX.dwForDepth]
    pop {r4, lr}
    bx lr
@endproc

;; @proc for_next_set_item_a32
;; @abi arm_ce
;; @in r0 = cursor inside `(...)` set
;; @out r0 = unquoted item string (0 if end), r1 = advanced cursor
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Extracts the next space/comma/semicolon-separated item inside a `FOR (...)` set.
@proc for_next_set_item_a32
    push {r4, r5, r6, lr}
    mov r4, r0
.sk_sep:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .end_set
    cmp r0, #0x20
    beq .adv_s
    cmp r0, #0x09
    beq .adv_s
    cmp r0, #0x2C ; ','
    beq .adv_s
    cmp r0, #0x3B ; ';'
    beq .adv_s
    b .tok_st
.adv_s:
    add r4, r4, #1
    b .sk_sep

.tok_st:
    mov r5, r4
    mov r6, #0 ; quote
.sc_it:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .got_it
    cmp r0, #0x22
    bne .chk_isep
    eor r6, r6, #1
    add r5, r5, #1
    b .sc_it
.chk_isep:
    cmp r6, #0
    bne .adv_i
    cmp r0, #0x20
    beq .got_it
    cmp r0, #0x09
    beq .got_it
    cmp r0, #0x2C
    beq .got_it
    cmp r0, #0x3B
    beq .got_it
.adv_i:
    add r5, r5, #1
    b .sc_it

.got_it:
    sub r2, r5, r4
    add r0, r2, #2
    push {r2, r3}
    bl arena_alloc_a32
    mov r6, r0
    pop {r2, r3}
    cmp r6, #0
    beq .end_set
    mov r0, r6
    mov r1, r4
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r6
    mov r1, #1
    bl expand_phase34_a32
    mov r1, r5
    pop {r4, r5, r6, lr}
    bx lr

.end_set:
    mov r0, #0
    mov r1, r4
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc for_walk_rec_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*, r1 = normalized directory, r2 = set string
;; @out Executes FOR /R body in r1 and all subdirectories
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Recursive directory walker for `FOR /R [root] %%I IN (set) DO body`.
@proc for_walk_rec_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #332 ; FIND_INFO_A32 at sp+0
    mov r4, r0 ; node
    mov r5, r1 ; cur dir
    mov r6, r2 ; set string

    ; Process items in r6 relative to r5
    mov r7, r6
.it_lp:
    mov r0, r7
    bl for_next_set_item_a32
    mov r7, r1
    cmp r0, #0
    beq .recurse_subs
    mov r8, r0 ; item (e.g. `*.txt` or `.`)

    ; Check if r8 contains `*` or `?`
    mov r1, #0
.cw:
    ldrb r0, [r8, r1]
    cmp r0, #0
    beq .lit_rec
    cmp r0, #0x2A
    beq .wild_rec
    cmp r0, #0x3F
    beq .wild_rec
    add r1, r1, #1
    b .cw

.lit_rec:
    ; Bind `r5\r8` (or `r5` if r8 == ".")
    mov r0, r5
    bl str_len_a32
    add r0, r0, #280
    bl arena_alloc_a32
    cmp r0, #0
    beq .it_lp
    mov r9, r0
    mov r1, r5
    bl str_copy_a32
    ldrb r1, [r8, #0]
    cmp r1, #0x2E
    bne .app_lit
    ldrb r1, [r8, #1]
    cmp r1, #0
    beq .bind_lit
.app_lit:
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .cp_l2
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.cp_l2:
    mov r1, r8
    bl str_copy_a32
.bind_lit:
    bl hal_get_ctx
    ldr r2, [r0, #ARM32_CTX.dwForDepth]
    str r2, [sp, #324]
    ldrb r0, [r4, #AST_NODE_A32.bForVar]
    mov r1, r9
    bl for_push_var_a32
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    ldr r0, [sp, #324]
    bl for_pop_vars_to_a32
    b .it_lp

.wild_rec:
    ; Enumerate `r5\r8`
    mov r0, r5
    bl str_len_a32
    add r0, r0, #280
    bl arena_alloc_a32
    cmp r0, #0
    beq .it_lp
    mov r9, r0
    mov r1, r5
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .cp_w2
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.cp_w2:
    mov r1, r8
    bl str_copy_a32
    mov r0, r9
    bl to_u16_longpath_a32
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .it_lp
    mov r9, r0 ; hFind
.wm_lp:
    ldr r0, [sp, #FIND_INFO_A32.dwAttr]
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .wm_nx
    mov r0, r5
    bl str_len_a32
    add r0, r0, #280
    bl arena_alloc_a32
    cmp r0, #0
    beq .wm_nx
    mov r8, r0
    mov r1, r5
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .wm_cp
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.wm_cp:
    add r1, sp, #FIND_INFO_A32.szName
    bl str_copy_a32

    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwForDepth]
    str r0, [sp, #324]
    ldrb r0, [r4, #AST_NODE_A32.bForVar]
    mov r1, r8
    bl for_push_var_a32
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    ldr r0, [sp, #324]
    bl for_pop_vars_to_a32
.wm_nx:
    mov r0, r9
    mov r1, sp
    bl hal_find_next
    cmp r0, #0
    bne .wm_lp
    mov r0, r9
    bl hal_find_close
    b .it_lp

.recurse_subs:
    mov r0, r5
    bl str_len_a32
    add r0, r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r8, r0
    mov r1, r5
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .sub_st
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.sub_st:
    mov r2, #0x2A
    strb r2, [r0, #0]
    mov r2, #0
    strb r2, [r0, #1]
    mov r0, r8
    bl to_u16_longpath_a32
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .done
    mov r9, r0
.sd_lp:
    ldr r0, [sp, #FIND_INFO_A32.dwAttr]
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .sd_nx
    mov_imm32 r1, FILE_ATTRIBUTE_REPARSE_POINT
    tst r0, r1
    bne .sd_nx
    add r0, sp, #FIND_INFO_A32.szName
    ldrb r1, [r0, #0]
    cmp r1, #0x2E
    bne .sd_go
    ldrb r1, [r0, #1]
    cmp r1, #0
    beq .sd_nx
    cmp r1, #0x2E
    bne .sd_go
    ldrb r1, [r0, #2]
    cmp r1, #0
    beq .sd_nx
.sd_go:
    mov r0, r5
    bl str_len_a32
    add r0, r0, #280
    bl arena_alloc_a32
    cmp r0, #0
    beq .sd_nx
    mov r8, r0
    mov r1, r5
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .sd_cp
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.sd_cp:
    add r1, sp, #FIND_INFO_A32.szName
    bl str_copy_a32
    mov r0, r4
    mov r1, r8
    mov r2, r6
    bl for_walk_rec_a32
.sd_nx:
    mov r0, r9
    mov r1, sp
    bl hal_find_next
    cmp r0, #0
    bne .sd_lp
    mov r0, r9
    bl hal_find_close

.done:
    add sp, sp, #332
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc for_f_process_line_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*, r1 = line string, r2 = delims string, r3 = options packed (byte0=eol, byte1=tok1_1based, byte2=tok_cnt, byte3=star_rest)
;; @out Splits line by delims and invokes AST_FOR pLeft body if non-empty and not eol-commented
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Tokenizes one `FOR /F` input line and binds `%%I`, `%%J`, ... on pForStack.
@proc for_f_process_line_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #80 ; up to 16 extracted token ptrs at sp+0..63, saved depth at sp+68
    mov r4, r0 ; node
    mov r5, r1 ; line
    mov r6, r2 ; delims
    mov r7, r3 ; packed opts

    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .done

    ; Skip leading delimiters unless `delims=` is empty!
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .whole_line
.sk_ld:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .done
    ; Check if r0 is in delims r6
    mov r2, #0
.dl1:
    ldrb r1, [r6, r2]
    cmp r1, #0
    beq .chk_eol
    cmp r0, r1
    beq .adv_ld
    add r2, r2, #1
    b .dl1
.adv_ld:
    add r5, r5, #1
    b .sk_ld

.whole_line:
    and r0, r7, #0xFF ; eol char
    ldrb r1, [r5, #0]
    cmp r1, r0
    beq .done
    str r5, [sp, #0]
    mov r8, #1 ; extracted count
    b .bind_toks

.chk_eol:
    and r0, r7, #0xFF ; eol char (default ';')
    ldrb r1, [r5, #0]
    cmp r1, r0
    beq .done

    ; Extract tokens up to 15!
    mov r8, #0 ; count
    lsr r9, r7, #8
    and r9, r9, #0xFF ; first requested token (1-based, default 1)
    lsr r10, r7, #16
    and r10, r10, #0xFF ; explicit count (default 1)
    add r10, r9, r10 ; 1-based stop index for `*` rest!

.tok_lp:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .bind_toks
    cmp r8, #15
    bcs .bind_toks

    ; Check if `*` rest applies to this token slot (`star_rest != 0` and `r8 + 1 == r10`)
    lsr r0, r7, #24
    cmp r0, #0
    beq .norm_tok
    add r0, r8, #1
    cmp r0, r10
    bne .norm_tok
    ; Rest of line starting at r5!
    lsl r1, r8, #2
    str r5, [sp, r1]
    add r8, r8, #1
    b .bind_toks

.norm_tok:
    mov r3, r5 ; start
.sc_tk:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .end_tk
    mov r2, #0
.dl2:
    ldrb r1, [r6, r2]
    cmp r1, #0
    beq .adv_tk
    cmp r0, r1
    beq .end_tk
    add r2, r2, #1
    b .dl2
.adv_tk:
    add r5, r5, #1
    b .sc_tk

.end_tk:
    sub r2, r5, r3
    add r0, r2, #2
    push {r2, r3}
    bl arena_alloc_a32
    mov r1, r0
    pop {r2, r3}
    cmp r1, #0
    beq .bind_toks
    push {r1, r2}
    mov r0, r1
    mov r1, r3
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    pop {r1, r2}
    lsl r0, r8, #2
    str r1, [sp, r0]
    add r8, r8, #1

    ; Skip consecutive delimiters after token
.sk_td:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .bind_toks
    mov r2, #0
.dl3:
    ldrb r1, [r6, r2]
    cmp r1, #0
    beq .tok_lp
    cmp r0, r1
    beq .adv_td
    add r2, r2, #1
    b .dl3
.adv_td:
    add r5, r5, #1
    b .sk_td

.bind_toks:
    lsr r9, r7, #8
    and r9, r9, #0xFF ; start_tok (1-based)
    cmp r9, #0
    bne .st_nz
    mov r9, #1
.st_nz:
    sub r9, r9, #1 ; 0-based index
    cmp r9, r8
    bcs .done ; line doesn't even have first requested token!

    lsr r10, r7, #16
    and r10, r10, #0xFF ; count
    lsr r0, r7, #24
    add r10, r10, r0 ; +1 if `*` rest token requested

    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwForDepth]
    str r0, [sp, #68]

    ldrb r5, [r4, #AST_NODE_A32.bForVar] ; starting var char ('I')
    mov r6, #0
.b_lp:
    cmp r6, r10
    bcs .run_b
    add r0, r9, r6
    cmp r0, r8
    bcs .empty_v
    lsl r0, r0, #2
    ldr r1, [sp, r0]
    b .push_v
.empty_v:
    adr_sym r1, szEmptyStr
.push_v:
    add r0, r5, r6
    bl for_push_var_a32
    add r6, r6, #1
    b .b_lp

.run_b:
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    ldr r0, [sp, #68]
    bl for_pop_vars_to_a32

.done:
    add sp, sp, #80
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc exec_for_parse_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32* (AST_FOR with bSubKind == A32_FOR_PARSE)
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Executes `FOR /F ["options"] %%I IN ('str' | `cmd` | fileset) DO body`.
@proc exec_for_parse_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #48 ; delims[16] at sp+0, skip at sp+20, usebackq at sp+24, packed at sp+28
    mov r4, r0

    ; Defaults: delims = " 	", eol = ';', skip = 0, tok1 = 1, tok_cnt = 1, star = 0, usebackq = 0
    mov r0, #0x20
    strb r0, [sp, #0]
    mov r0, #0x09
    strb r0, [sp, #1]
    mov r0, #0
    strb r0, [sp, #2]
    str r0, [sp, #20] ; skip = 0
    str r0, [sp, #24] ; usebackq = 0
    mov_imm32 r7, 0x0001013B ; star=0, cnt=1, tok1=1, eol=';' (0x3B)

    ; Parse optional `"options"` in pExtra
    ldr r0, [r4, #AST_NODE_A32.pExtra]
    cmp r0, #0
    beq .opts_done
    bl strip_outer_quotes_a32
    mov r5, r0

.opt_lp:
    mov r0, r5
    bl skip_ws_a32
    mov r5, r0
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .opts_done

    ; Check `usebackq` (8 chars)
    ldrb r0, [r5, #0]
    bl ascii_upper_a32
    cmp r0, #0x55 ; 'U'
    bne .chk_eol_o
    mov r0, #1
    str r0, [sp, #24]
    add r5, r5, #8
    b .opt_lp

.chk_eol_o:
    cmp r0, #0x45 ; 'E' (`eol=c`)
    bne .chk_skip_o
    ldrb r1, [r5, #3]
    cmp r1, #0x3D
    bne .adv_o1
    ldrb r0, [r5, #4]
    bic r7, r7, #0xFF
    orr r7, r7, r0
    cmp r0, #0
    beq .opts_done
    add r5, r5, #5
    b .opt_lp

.chk_skip_o:
    cmp r0, #0x53 ; 'S' (`skip=n`)
    bne .chk_delims_o
    add r0, r5, #5
    bl parse_signed_dec_a32
    str r0, [sp, #20]
    mov r5, r1
    b .opt_lp

.chk_delims_o:
    cmp r0, #0x44 ; 'D' (`delims=...` — note `delims=` at end of options sets empty delims!)
    bne .chk_tokens_o
    add r5, r5, #7
    mov r2, #0
.dl_cp:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .dl_end
    ; If space and followed by another keyword or end, take space as delim if `delims= ` at end!
    cmp r0, #0x20
    bne .dl_st
    ldrb r1, [r5, #1]
    cmp r1, #0
    bne .dl_end
.dl_st:
    cmp r2, #14
    bcs .dl_nx
    strb r0, [sp, r2]
    add r2, r2, #1
.dl_nx:
    add r5, r5, #1
    b .dl_cp
.dl_end:
    mov r0, #0
    strb r0, [sp, r2]
    b .opt_lp

.chk_tokens_o:
    cmp r0, #0x54 ; 'T' (`tokens=...`, e.g. `1,2*`, `1-3`, `2,*`)
    bne .adv_o1
    add r0, r5, #7
    bl parse_signed_dec_a32
    mov r6, r0 ; first tok (1-based)
    mov r5, r1
    mov r8, #1 ; count
    mov r9, #0 ; star
.tk_spec:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .tk_fin
    cmp r0, #0x20
    beq .tk_fin
    cmp r0, #0x2A ; '*'
    bne .tk_rng
    mov r9, #1
    add r5, r5, #1
    b .tk_fin
.tk_rng:
    cmp r0, #0x2D ; '-' range `a-b`
    bne .tk_cm
    add r0, r5, #1
    bl parse_signed_dec_a32
    mov r5, r1
    sub r8, r0, r6
    add r8, r8, #1
    b .tk_spec
.tk_cm:
    cmp r0, #0x2C ; ',' list `1,2`
    bne .tk_adv
    ldrb r1, [r5, #1]
    cmp r1, #0x2A
    bne .tk_cm_n
    mov r9, #1
    add r5, r5, #2
    b .tk_fin
.tk_cm_n:
    add r0, r5, #1
    bl parse_signed_dec_a32
    mov r5, r1
    add r8, r8, #1
    b .tk_spec
.tk_adv:
    add r5, r5, #1
    b .tk_spec
.tk_fin:
    and r7, r7, #0xFF
    and r6, r6, #0xFF
    and r8, r8, #0xFF
    lsl r6, r6, #8
    lsl r8, r8, #16
    lsl r9, r9, #24
    orr r7, r7, r6
    orr r7, r7, r8
    orr r7, r7, r9
    b .opt_lp

.adv_o1:
    add r5, r5, #1
    b .opt_lp

.opts_done:
    str r7, [sp, #28]
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    cmp r0, #0
    beq .done
    mov r1, #0
    bl expand_phase34_a32
    bl skip_ws_a32
    mov r5, r0

    ; Trim trailing whitespace on set spec
    bl str_len_a32
    mov r2, r0
.tr_s:
    cmp r2, #0
    beq .done
    sub r1, r2, #1
    ldrb r0, [r5, r1]
    cmp r0, #0x20
    beq .tr_c
    cmp r0, #0x09
    bne .dispatch_src
.tr_c:
    mov r0, #0
    strb r0, [r5, r1]
    mov r2, r1
    b .tr_s

.dispatch_src:
    ldrb r0, [r5, #0]
    ldr r1, [sp, #24] ; usebackq
    cmp r1, #0
    bne .ubq_mode
    cmp r0, #0x22 ; `"literal string"`
    beq .lit_str_src
    cmp r0, #0x27 ; `'command'`
    beq .cmd_src
    cmp r0, #0x60 ; `` `command` ``
    beq .cmd_src
    b .file_src
.ubq_mode:
    cmp r0, #0x27 ; `'literal string'`
    beq .lit_str_src
    cmp r0, #0x60 ; `` `command` ``
    beq .cmd_src
    b .file_src

.lit_str_src:
    ; Strip outer `'...'` or `"..."`
    add r5, r5, #1
    mov r0, r5
    bl str_len_a32
    cmp r0, #0
    beq .run_buf
    sub r0, r0, #1
    ldrb r1, [r5, r0]
    cmp r1, #0x27
    beq .cut_q
    cmp r1, #0x22
    bne .run_buf
.cut_q:
    mov r1, #0
    strb r1, [r5, r0]
    b .run_buf

.cmd_src:
    ; Strip outer quotes/backticks, execute into temp spool file, then read spool file!
    add r5, r5, #1
    mov r0, r5
    bl str_len_a32
    cmp r0, #0
    beq .done
    sub r0, r0, #1
    mov r1, #0
    strb r1, [r5, r0]
    mov r0, r5
    bl capture_cmd_to_buf_a32
    cmp r0, #0
    beq .done
    mov r5, r0
    b .run_buf

.lit_or_cmd_sq:
    ; Note: `'...'` when usebackq == 0 in cmd.exe is command execution! Let's check `.dispatch_src`:
    ; Wait: in `.dispatch_src` when usebackq == 0, `r0 == 0x27` (`'...'`) jumped to `.lit_str_src` above — let's check whether `'...'` is command or string!
    ; In cmd.exe: `FOR /F %I IN ('echo hi') DO ...` executes `echo hi`! And `FOR /F "usebackq" %I IN ('literal') DO ...` treats `'literal'` as a string!
    b .run_buf

.file_src:
    mov r0, r5
    bl strip_outer_quotes_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    mov r1, #0
    bl hal_open_file
    cmn r0, #1
    beq .done
    mov r6, r0 ; hFile
    mov_imm32 r0, 16384
    bl arena_alloc_a32
    cmp r0, #0
    beq .cl_f
    mov r5, r0
    mov r0, r6
    mov r1, r5
    mov_imm32 r2, 16380
    bl hal_read_handle
    mov r1, #0
    strb r1, [r5, r0]
.cl_f:
    mov r0, r6
    bl hal_close_handle

.run_buf:
    ; Iterate lines in buffer r5, honoring `skip` at [sp+20]
    ldr r8, [sp, #20] ; skip count
.ln_lp:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .done
    mov r6, r5 ; line start
.sc_nl:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .got_ln
    cmp r0, #0x0D
    beq .cut_cr
    cmp r0, #0x0A
    beq .cut_lf
    add r5, r5, #1
    b .sc_nl
.cut_cr:
    mov r0, #0
    strb r0, [r5, #0]
    add r5, r5, #1
    ldrb r0, [r5, #0]
    cmp r0, #0x0A
    bne .got_ln
    add r5, r5, #1
    b .got_ln
.cut_lf:
    mov r0, #0
    strb r0, [r5, #0]
    add r5, r5, #1

.got_ln:
    cmp r8, #0
    beq .proc_ln
    sub r8, r8, #1
    b .ln_lp
.proc_ln:
    mov r0, r4
    mov r1, r6
    mov r2, sp ; delims
    ldr r3, [sp, #28]
    bl for_f_process_line_a32
    b .ln_lp

.done:
    add sp, sp, #48
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc exec_for_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32* (AST_FOR)
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Dispatches FOR plain, `/D`, `/R`, `/L`, and `/F`.
@proc exec_for_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #348 ; FIND_INFO_A32 at sp+0, scratch at sp+320..347
    mov r4, r0
    ldrb r5, [r4, #AST_NODE_A32.bSubKind]

    cmp r5, #A32_FOR_PARSE
    bne .not_parse
    mov r0, r4
    bl exec_for_parse_a32
    mov r0, #0
    b .done

.not_parse:
    cmp r5, #A32_FOR_LOOP
    beq .do_loop_l
    cmp r5, #A32_FOR_REC
    beq .do_rec_r

    ; Plain FOR or FOR /D
    ldr r6, [r4, #AST_NODE_A32.pStr1]
    cmp r6, #0
    beq .ok0

.pl_lp:
    mov r0, r6
    bl for_next_set_item_a32
    mov r6, r1
    cmp r0, #0
    beq .ok0
    mov r7, r0 ; item

    ; Check if r7 has `*` or `?`
    mov r1, #0
.wchk:
    ldrb r0, [r7, r1]
    cmp r0, #0
    beq .pl_lit
    cmp r0, #0x2A
    beq .pl_wild
    cmp r0, #0x3F
    beq .pl_wild
    add r1, r1, #1
    b .wchk

.pl_lit:
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwForDepth]
    str r0, [sp, #324]
    ldrb r0, [r4, #AST_NODE_A32.bForVar]
    mov r1, r7
    bl for_push_var_a32
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    ldr r0, [sp, #324]
    bl for_pop_vars_to_a32
    b .pl_lp

.pl_wild:
    mov r0, r7
    bl norm_path_a32
    mov r8, r0
    ; Find last `` in r8 vs original r7 (check if r7 had directory prefix)
    mov r9, #0 ; has_dir_prefix
    mov r1, #0
.hd_sc:
    ldrb r0, [r7, r1]
    cmp r0, #0
    beq .hd_done
    cmp r0, #0x5C
    beq .hd_yes
    cmp r0, #0x2F
    beq .hd_yes
    cmp r0, #0x3A
    beq .hd_yes
    add r1, r1, #1
    b .hd_sc
.hd_yes:
    mov r9, r1 ; prefix end in r7
    add r9, r9, #1
.hd_done:
    mov r0, r8
    bl to_u16_longpath_a32
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .pl_lp
    str r0, [sp, #328] ; hFind

.pw_lp:
    add r0, sp, #FIND_INFO_A32.szName
    ldrb r1, [r0, #0]
    cmp r1, #0x2E
    bne .pw_attr
    ldrb r1, [r0, #1]
    cmp r1, #0
    beq .pw_nx
    cmp r1, #0x2E
    bne .pw_attr
    ldrb r1, [r0, #2]
    cmp r1, #0
    beq .pw_nx

.pw_attr:
    ldr r0, [sp, #FIND_INFO_A32.dwAttr]
    cmp r5, #A32_FOR_DIRS
    beq .want_dir
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .pw_nx
    b .pw_emit
.want_dir:
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .pw_nx

.pw_emit:
    ; Build bound value: either bare `szName` or `r7_prefix + szName`
    add r0, r9, #280
    bl arena_alloc_a32
    cmp r0, #0
    beq .pw_nx
    mov r8, r0
    cmp r9, #0
    beq .pw_bare
    mov r1, r7
    mov r2, r9
    bl mem_copy_a32
.pw_bare:
    add r1, sp, #FIND_INFO_A32.szName
    bl str_copy_a32

    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwForDepth]
    str r0, [sp, #324]
    ldrb r0, [r4, #AST_NODE_A32.bForVar]
    mov r1, r8
    bl for_push_var_a32
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    ldr r0, [sp, #324]
    bl for_pop_vars_to_a32

.pw_nx:
    ldr r0, [sp, #328]
    mov r1, sp
    bl hal_find_next
    cmp r0, #0
    bne .pw_lp
    ldr r0, [sp, #328]
    bl hal_find_close
    b .pl_lp

.do_rec_r:
    ldr r0, [r4, #AST_NODE_A32.pExtra]
    cmp r0, #0
    beq .rec_cwd
    mov r1, #1
    bl expand_phase34_a32
    bl norm_path_a32
    mov r1, r0
    b .rec_go
.rec_cwd:
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r1, r0, r1
.rec_go:
    ldr r2, [r4, #AST_NODE_A32.pStr1]
    mov r0, r4
    bl for_walk_rec_a32
    b .ok0

.do_loop_l:
    ldr r0, [r4, #AST_NODE_A32.pStr1]
    cmp r0, #0
    beq .ok0
    bl for_next_set_item_a32
    mov r6, r1
    cmp r0, #0
    beq .ok0
    bl parse_signed_dec_a32
    mov r7, r0 ; cur = start

    mov r0, r6
    bl for_next_set_item_a32
    mov r6, r1
    cmp r0, #0
    beq .ok0
    bl parse_signed_dec_a32
    mov r8, r0 ; step

    mov r0, r6
    bl for_next_set_item_a32
    cmp r0, #0
    beq .ok0
    bl parse_signed_dec_a32
    mov r9, r0 ; end

    cmp r8, #0
    beq .ok0 ; step 0 exits immediately

.num_lp:
    cmp r8, #0
    blt .chk_neg_step
    cmp r7, r9
    bgt .ok0
    b .num_body
.chk_neg_step:
    cmp r7, r9
    blt .ok0
.num_body:
    mov r0, r7
    add r1, sp, #328
    bl fmt_i32_a32
    add r0, sp, #328
    bl arena_strdup_a32
    mov r6, r0

    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwForDepth]
    str r0, [sp, #324]
    ldrb r0, [r4, #AST_NODE_A32.bForVar]
    mov r1, r6
    bl for_push_var_a32
    ldr r0, [r4, #AST_NODE_A32.pLeft]
    bl exec_ast_a32
    ldr r0, [sp, #324]
    bl for_pop_vars_to_a32

    add r7, r7, r8
    b .num_lp

.ok0:
    mov r0, #0
.done:
    add sp, sp, #348
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc
