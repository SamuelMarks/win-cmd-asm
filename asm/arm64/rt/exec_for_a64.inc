;; @file exec_for_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 FD 0..9 Redirection Engine, IF Evaluator, and FOR (plain, /D, /R, /L, /F) Engine.

@cpu arm64
@format inc

;; @proc apply_redirs_a64
;; @abi win_arm64
;; @in x0 = REDIR_NODE_A64* list, x1 = 96-byte save buffer on caller stack
;; @out w0 = 0 on success, 1 on file open error (restoring any partial handles)
;; @err w0 = 1 when input/output redirection file cannot be opened
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Saves fdTable[0..9] (80 bytes) into x1[0..79] and evaluates redirections strictly left-to-right.
@proc apply_redirs_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; redir node
    mov x20, x1 ; save block

    bl hal_get_ctx_a64
    mov x21, x0
    mov x0, x20
    add x1, x21, #ARM64_CTX.fdTable
    mov x2, #80
    bl mem_copy_a64
    str wzr, [x20, #80]

.r_lp:
    cbz x19, .ok0
    ldrb w22, [x19, #REDIR_NODE_A64.bKind]
    ldrb w23, [x19, #REDIR_NODE_A64.bSrcFd]
    cmp w23, #9
    b.hi .nx_r

    cmp w22, #A64_REDIR_DUP_OUT
    b.eq .do_dup
    cmp w22, #A64_REDIR_DUP_IN
    b.eq .do_dup

    ldr x0, [x19, #REDIR_NODE_A64.pTarget]
    mov w1, #1
    bl expand_phase34_a64
    mov x24, x0

    mov x0, x24
    bl check_device_a64
    cmp w0, #DEV_NUL
    b.ne .norm_rpath
    adr_sym x24, szWinNtNulDev_a64
    b .open_u16

.norm_rpath:
    mov x0, x24
    bl norm_path_a64
    mov x24, x0

.open_u16:
    mov x0, x24
    bl to_u16_longpath_a64
    sub w1, w22, #1 ; A64_REDIR_IN(1)->0, OUT(2)->1, APP(3)->2
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .fail_open
    mov x24, x0 ; new HANDLE

    ; Close previous owned handle in slot w23 if already redirected in this list
    ldr w0, [x20, #80]
    mov w1, #1
    lslv w1, w1, w23
    tst w0, w1
    b.eq .set_owned
    uxtw x2, w23
    lsl x2, x2, #3
    add x3, x21, #ARM64_CTX.fdTable
    ldr x0, [x3, x2]
    bl hal_close_handle_a64
.set_owned:
    ldr w0, [x20, #80]
    mov w1, #1
    lslv w1, w1, w23
    orr w0, w0, w1
    str w0, [x20, #80]
    uxtw x2, w23
    lsl x2, x2, #3
    add x3, x21, #ARM64_CTX.fdTable
    str x24, [x3, x2]
    b .nx_r

.do_dup:
    ldrb w0, [x19, #REDIR_NODE_A64.bDstFd]
    cmp w0, #9
    b.hi .nx_r
    uxtw x0, w0
    lsl x0, x0, #3
    add x3, x21, #ARM64_CTX.fdTable
    ldr x24, [x3, x0]
    ldr w0, [x20, #80]
    mov w1, #1
    lslv w1, w1, w23
    tst w0, w1
    b.eq .dup_set
    bic w0, w0, w1
    str w0, [x20, #80]
    uxtw x2, w23
    lsl x2, x2, #3
    ldr x0, [x3, x2]
    bl hal_close_handle_a64
.dup_set:
    uxtw x2, w23
    lsl x2, x2, #3
    add x3, x21, #ARM64_CTX.fdTable
    str x24, [x3, x2]

.nx_r:
    ldr x19, [x19, #REDIR_NODE_A64.pNext]
    b .r_lp

.fail_open:
    mov w0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a64
    mov x0, x20
    bl restore_redirs_a64
    mov w0, #1
    b .exit

.ok0:
    mov w0, #0
.exit:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc restore_redirs_a64
;; @abi win_arm64
;; @in x0 = 96-byte save buffer filled by apply_redirs_a64
;; @out Closes all newly opened redirection handles and restores fdTable[0..9]
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Restores ARM64_CTX.fdTable[0..9] and closes owned redirection file handles.
@proc restore_redirs_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl hal_get_ctx_a64
    mov x20, x0
    ldr w21, [x19, #80]
    mov w22, #0
.cl_lp:
    cmp w22, #10
    b.hs .cp_back
    mov w0, #1
    lslv w0, w0, w22
    tst w21, w0
    b.eq .nx_fd
    uxtw x1, w22
    lsl x1, x1, #3
    add x2, x20, #ARM64_CTX.fdTable
    ldr x0, [x2, x1]
    bl hal_close_handle_a64
.nx_fd:
    add w22, w22, #1
    b .cl_lp

.cp_back:
    add x0, x20, #ARM64_CTX.fdTable
    mov x1, x19
    mov x2, #80
    bl mem_copy_a64
    str wzr, [x19, #80]
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc try_parse_strict_i32_a64
;; @abi win_arm64
;; @in x0 = candidate string
;; @out w0 = parsed i32, w1 = 1 if entire string is a valid signed integer, else 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Attempts strict numeric conversion for `IF ... EQU/NEQ/LSS/LEQ/GTR/GEQ`.
@proc try_parse_strict_i32_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    cbz x0, .no
    mov x19, x0
    ldrb w0, [x19, #0]
    cbz w0, .no
    mov w20, #0
    cmp w0, #0x2D
    b.ne .chk_pl
    mov w20, #1
    add x19, x19, #1
    b .chk_d
.chk_pl:
    cmp w0, #0x2B
    b.ne .chk_d
    add x19, x19, #1
.chk_d:
    ldrb w0, [x19, #0]
    cmp w0, #0x30
    b.lo .no
    cmp w0, #0x39
    b.hi .no
    mov x0, x19
    bl sa_parse_num_lit_a64
    cbnz w2, .no
    ldrb w3, [x1, #0]
    cbnz w3, .no
    cbz w20, .ok
    neg w0, w0
.ok:
    mov w1, #1
    b .done
.no:
    mov w0, #0
    mov w1, #0
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc eval_if_cond_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64* (AST_IF)
;; @out w0 = 1 if condition holds, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Evaluates IF `[NOT]` `[/I]` (`ERRORLEVEL`, `EXIST`, `DEFINED`, `CMDEXTVERSION`, `==`, `EQU..GEQ`).
@proc eval_if_cond_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #336 ; FIND_INFO_A64 (320B) at sp+0
    mov x29, sp
    mov x19, x0
    ldrb w20, [x19, #AST_NODE_A64.bSubKind]
    ldrb w21, [x19, #AST_NODE_A64.bFlags]

    ldr x0, [x19, #AST_NODE_A64.pStr1]
    cbz x0, .false_raw
    mov w1, #0
    bl expand_phase34_a64
    mov x22, x0 ; lhs

    cmp w20, #A64_IF_ERRORLEVEL
    b.eq .c_errlvl
    cmp w20, #A64_IF_EXIST
    b.eq .c_exist
    cmp w20, #A64_IF_DEFINED
    b.eq .c_defined
    cmp w20, #A64_IF_CMDEXTVER
    b.eq .c_extver

    ldr x0, [x19, #AST_NODE_A64.pExtra]
    cbz x0, .false_raw
    mov w1, #0
    bl expand_phase34_a64
    mov x23, x0 ; rhs

    mov x0, x22
    bl try_parse_strict_i32_a64
    cbz w1, .str_cmp_if
    mov w24, w0
    mov x0, x23
    bl try_parse_strict_i32_a64
    cbz w1, .str_cmp_if

    cmp w24, w0
    b.eq .ord_eq
    b.lt .ord_lt
    mov w0, #1
    b .eval_ord
.ord_lt:
    mov w0, #-1
    b .eval_ord
.ord_eq:
    mov w0, #0
    b .eval_ord

.str_cmp_if:
    mov w2, #0
    tst w21, #AST_FLAG_IF_ICASE
    b.eq .do_scmp
    mov w2, #1
.do_scmp:
    mov x0, x22
    mov x1, x23
    bl str_cmp_a64

.eval_ord:
    cmp w20, #A64_IF_CMP_EQ
    b.eq .t_eq
    cmp w20, #A64_IF_CMP_NEQ
    b.eq .t_neq
    cmp w20, #A64_IF_CMP_LSS
    b.eq .t_lss
    cmp w20, #A64_IF_CMP_LEQ
    b.eq .t_leq
    cmp w20, #A64_IF_CMP_GTR
    b.eq .t_gtr
    cmp w0, #0
    b.ge .true_raw
    b .false_raw
.t_eq:
    cbz w0, .true_raw
    b .false_raw
.t_neq:
    cbnz w0, .true_raw
    b .false_raw
.t_lss:
    cmp w0, #0
    b.lt .true_raw
    b .false_raw
.t_leq:
    cmp w0, #0
    b.le .true_raw
    b .false_raw
.t_gtr:
    cmp w0, #0
    b.gt .true_raw
    b .false_raw

.c_errlvl:
    mov x0, x22
    bl unquote_dup_a64
    bl parse_signed_dec_a64
    mov w22, w0
    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwLastErrorLevel]
    cmp w0, w22
    b.ge .true_raw
    b .false_raw

.c_extver:
    mov x0, x22
    bl unquote_dup_a64
    bl parse_signed_dec_a64
    cmp x0, #2
    b.le .true_raw
    b .false_raw

.c_defined:
    mov x0, x22
    bl unquote_dup_a64
    bl env_get_raw_a64
    cbnz x0, .true_raw
    b .false_raw

.c_exist:
    mov x0, x22
    bl unquote_dup_a64
    mov x22, x0
    bl check_device_a64
    cmp w0, #DEV_NONE
    b.ne .true_raw
    mov x0, x22
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov x23, x0
    cbz x23, .false_raw
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.ne .true_raw
    mov x0, x23
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .false_raw
    bl hal_find_close_a64
    b .true_raw

.true_raw:
    mov w0, #1
    b .apply_not
.false_raw:
    mov w0, #0
.apply_not:
    tst w21, #AST_FLAG_IF_NOT
    b.eq .done
    eor w0, w0, #1
.done:
    add sp, sp, #336
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc for_push_var_a64
;; @abi win_arm64
;; @in w0 = var char ('I'), x1 = value string
;; @out Pushes FOR variable binding onto ARM64_CTX.pForStack
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Binds a FOR loop variable on the nested FOR scope stack (16B FOR_VAR_A64).
@proc for_push_var_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov w19, w0
    mov x20, x1
    bl hal_get_ctx_a64
    ldr w2, [x0, #ARM64_CTX.dwForDepth]
    cmp w2, #A64_FOR_MAX
    b.hs .done
    ldr x3, [x0, #ARM64_CTX.pForStack]
    uxtw x1, w2
    lsl x1, x1, #4 ; * 16 (sizeof FOR_VAR_A64)
    add x3, x3, x1
    strb w19, [x3, #FOR_VAR_A64.bVarChar]
    mov w1, #1
    strb w1, [x3, #FOR_VAR_A64.bActive]
    str x20, [x3, #FOR_VAR_A64.pValue]
    add w2, w2, #1
    str w2, [x0, #ARM64_CTX.dwForDepth]
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc for_pop_vars_to_a64
;; @abi win_arm64
;; @in w0 = saved dwForDepth
;; @out Restores ARM64_CTX.dwForDepth to w0
;; @err none
;; @clobbers x0, x1
;; @desc Unwinds FOR variable bindings back to w0.
@proc for_pop_vars_to_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov w19, w0
    bl hal_get_ctx_a64
    str w19, [x0, #ARM64_CTX.dwForDepth]
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc for_next_set_item_a64
;; @abi win_arm64
;; @in x0 = cursor inside `(...)` set
;; @out x0 = unquoted item string (0 if end), x1 = advanced cursor
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Extracts the next space/comma/semicolon-separated item inside a `FOR (...)` set.
@proc for_next_set_item_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
.sk_sep:
    ldrb w0, [x19, #0]
    cbz w0, .end_set
    cmp w0, #0x20
    b.eq .adv_s
    cmp w0, #0x09
    b.eq .adv_s
    cmp w0, #0x2C
    b.eq .adv_s
    cmp w0, #0x3B
    b.eq .adv_s
    b .tok_st
.adv_s:
    add x19, x19, #1
    b .sk_sep

.tok_st:
    mov x20, x19
    mov w21, #0
.sc_it:
    ldrb w0, [x20, #0]
    cbz w0, .got_it
    cmp w0, #0x22
    b.ne .chk_isep
    eor w21, w21, #1
    add x20, x20, #1
    b .sc_it
.chk_isep:
    cbnz w21, .adv_i
    cmp w0, #0x20
    b.eq .got_it
    cmp w0, #0x09
    b.eq .got_it
    cmp w0, #0x2C
    b.eq .got_it
    cmp w0, #0x3B
    b.eq .got_it
.adv_i:
    add x20, x20, #1
    b .sc_it

.got_it:
    sub x21, x20, x19
    add x0, x21, #2
    bl arena_alloc_a64
    cbz x0, .end_set
    mov x22, x0
    mov x1, x19
    mov x2, x21
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x22
    mov w1, #1
    bl expand_phase34_a64
    mov x1, x20
    b .done

.end_set:
    mov x0, #0
    mov x1, x19
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc for_walk_rec_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*, x1 = normalized directory, x2 = set string
;; @out Executes FOR /R body in x1 and all non-reparse subdirectories
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Recursive directory walker for `FOR /R [root] %%I IN (set) DO body`.
@proc for_walk_rec_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #336 ; FIND_INFO_A64 (320B) at sp+0, saved depth at sp+324
    mov x29, sp
    mov x19, x0 ; node
    mov x20, x1 ; cur dir
    mov x21, x2 ; set string

    mov x22, x21
.it_lp:
    mov x0, x22
    bl for_next_set_item_a64
    mov x22, x1
    cbz x0, .recurse_subs
    mov x23, x0 ; item

    mov x1, #0
.cw:
    ldrb w0, [x23, x1]
    cbz w0, .lit_rec
    cmp w0, #0x2A
    b.eq .wild_rec
    cmp w0, #0x3F
    b.eq .wild_rec
    add x1, x1, #1
    b .cw

.lit_rec:
    mov x0, x20
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .it_lp
    mov x24, x0
    mov x1, x20
    bl str_copy_a64
    ldrb w1, [x23, #0]
    cmp w1, #0x2E
    b.ne .app_lit
    ldrb w1, [x23, #1]
    cbz w1, .bind_lit
.app_lit:
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .cp_l2
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.cp_l2:
    mov x1, x23
    bl str_copy_a64
.bind_lit:
    bl hal_get_ctx_a64
    ldr w2, [x0, #ARM64_CTX.dwForDepth]
    str w2, [sp, #324]
    ldrb w0, [x19, #AST_NODE_A64.bForVar]
    mov x1, x24
    bl for_push_var_a64
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    ldr w0, [sp, #324]
    bl for_pop_vars_to_a64
    b .it_lp

.wild_rec:
    mov x0, x20
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .it_lp
    mov x24, x0
    mov x1, x20
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .cp_w2
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.cp_w2:
    mov x1, x23
    bl str_copy_a64
    mov x0, x24
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .it_lp
    mov x24, x0 ; hFind
.wm_lp:
    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    tbnz w0, #4, .wm_nx
    mov x0, x20
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .wm_nx
    mov x23, x0
    mov x1, x20
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .wm_cp
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.wm_cp:
    add x1, sp, #FIND_INFO_A64.szName
    bl str_copy_a64

    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwForDepth]
    str w0, [sp, #324]
    ldrb w0, [x19, #AST_NODE_A64.bForVar]
    mov x1, x23
    bl for_push_var_a64
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    ldr w0, [sp, #324]
    bl for_pop_vars_to_a64
.wm_nx:
    mov x0, x24
    mov x1, sp
    bl hal_find_next_a64
    cbnz w0, .wm_lp
    mov x0, x24
    bl hal_find_close_a64
    b .it_lp

.recurse_subs:
    mov x0, x20
    bl str_len_a64
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .done
    mov x23, x0
    mov x1, x20
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .sub_st
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.sub_st:
    mov w2, #0x2A
    strb w2, [x0, #0]
    strb wzr, [x0, #1]
    mov x0, x23
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .done
    mov x24, x0
.sd_lp:
    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    tbz w0, #4, .sd_nx
    tbnz w0, #10, .sd_nx ; skip reparse points (`0x400`)!
    add x0, sp, #FIND_INFO_A64.szName
    ldrb w1, [x0, #0]
    cmp w1, #0x2E
    b.ne .sd_go
    ldrb w1, [x0, #1]
    cbz w1, .sd_nx
    cmp w1, #0x2E
    b.ne .sd_go
    ldrb w1, [x0, #2]
    cbz w1, .sd_nx
.sd_go:
    mov x0, x20
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .sd_nx
    mov x23, x0
    mov x1, x20
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .sd_cp
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.sd_cp:
    add x1, sp, #FIND_INFO_A64.szName
    bl str_copy_a64
    mov x0, x19
    mov x1, x23
    mov x2, x21
    bl for_walk_rec_a64
.sd_nx:
    mov x0, x24
    mov x1, sp
    bl hal_find_next_a64
    cbnz w0, .sd_lp
    mov x0, x24
    bl hal_find_close_a64

.done:
    add sp, sp, #336
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc for_f_process_line_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*, x1 = line string, x2 = delims string, w3 = options packed (byte0=eol, byte1=tok1_1based, byte2=tok_cnt, byte3=star_rest)
;; @out Splits line by delims and invokes AST_FOR pLeft body if non-empty and not eol-commented
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Tokenizes one `FOR /F` input line and binds `%%I`, `%%J`, ... on pForStack.
@proc for_f_process_line_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #144 ; 16 qword token ptrs at sp+0..127, saved depth at sp+128
    mov x29, sp
    mov x19, x0 ; node
    mov x20, x1 ; line
    mov x21, x2 ; delims
    mov w22, w3 ; packed opts

    ldrb w0, [x20, #0]
    cbz w0, .done

    ldrb w0, [x21, #0]
    cbz w0, .whole_line
.sk_ld:
    ldrb w0, [x20, #0]
    cbz w0, .done
    mov x2, #0
.dl1:
    ldrb w1, [x21, x2]
    cbz w1, .chk_eol
    cmp w0, w1
    b.eq .adv_ld
    add x2, x2, #1
    b .dl1
.adv_ld:
    add x20, x20, #1
    b .sk_ld

.whole_line:
    and w0, w22, #0xFF
    ldrb w1, [x20, #0]
    cmp w1, w0
    b.eq .done
    str x20, [sp, #0]
    mov w23, #1
    b .bind_toks

.chk_eol:
    and w0, w22, #0xFF
    ldrb w1, [x20, #0]
    cmp w1, w0
    b.eq .done

    mov w23, #0 ; count
    lsr w24, w22, #8
    and w24, w24, #0xFF ; first requested token (1-based)
    lsr w25, w22, #16
    and w25, w25, #0xFF ; explicit count
    add w25, w24, w25 ; 1-based stop index for `*` rest

.tok_lp:
    ldrb w0, [x20, #0]
    cbz w0, .bind_toks
    cmp w23, #15
    b.hs .bind_toks

    lsr w0, w22, #24
    cbz w0, .norm_tok
    add w0, w23, #1
    cmp w0, w25
    b.ne .norm_tok
    uxtw x1, w23
    lsl x1, x1, #3
    str x20, [sp, x1]
    add w23, w23, #1
    b .bind_toks

.norm_tok:
    mov x26, x20 ; start
.sc_tk:
    ldrb w0, [x20, #0]
    cbz w0, .got_tk
    mov x2, #0
.dl2:
    ldrb w1, [x21, x2]
    cbz w1, .adv_tk
    cmp w0, w1
    b.eq .got_tk
    add x2, x2, #1
    b .dl2
.adv_tk:
    add x20, x20, #1
    b .sc_tk

.got_tk:
    sub x24, x20, x26 ; len
    add x0, x24, #2
    bl arena_alloc_a64
    cbz x0, .bind_toks
    mov x3, x0
    uxtw x4, w23
    lsl x4, x4, #3
    str x3, [sp, x4]
    add w23, w23, #1
    mov x1, x26
    mov x2, x24
    bl mem_copy_a64
    strb wzr, [x0, #0]

.sk_td:
    ldrb w0, [x20, #0]
    cbz w0, .bind_toks
    mov x2, #0
.dl3:
    ldrb w1, [x21, x2]
    cbz w1, .tok_lp
    cmp w0, w1
    b.eq .adv_td
    add x2, x2, #1
    b .dl3
.adv_td:
    add x20, x20, #1
    b .sk_td

.bind_toks:
    lsr w24, w22, #8
    and w24, w24, #0xFF
    cbnz w24, .st_nz
    mov w24, #1
.st_nz:
    sub w24, w24, #1
    cmp w24, w23
    b.hs .done

    lsr w25, w22, #16
    and w25, w25, #0xFF
    lsr w0, w22, #24
    add w25, w25, w0

    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwForDepth]
    str w0, [sp, #128]

    ldrb w20, [x19, #AST_NODE_A64.bForVar]
    mov w21, #0
.b_lp:
    cmp w21, w25
    b.hs .run_b
    add w0, w24, w21
    cmp w0, w23
    b.hs .empty_v
    uxtw x0, w0
    lsl x0, x0, #3
    ldr x1, [sp, x0]
    b .push_v
.empty_v:
    adr_sym x1, szEmptyStr_a64
.push_v:
    add w0, w20, w21
    bl for_push_var_a64
    add w21, w21, #1
    b .b_lp

.run_b:
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    ldr w0, [sp, #128]
    bl for_pop_vars_to_a64

.done:
    add sp, sp, #144
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc exec_for_parse_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64* (AST_FOR with bSubKind == A64_FOR_PARSE)
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Executes `FOR /F ["options"] %%I IN ("str" | 'cmd' | `cmd` | fileset) DO body`.
@proc exec_for_parse_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #48 ; delims[16] at sp+0, skip at sp+20, usebackq at sp+24, packed at sp+28
    mov x29, sp
    mov x19, x0

    mov w0, #0x20
    strb w0, [sp, #0]
    mov w0, #0x09
    strb w0, [sp, #1]
    strb wzr, [sp, #2]
    str wzr, [sp, #20] ; skip = 0
    str wzr, [sp, #24] ; usebackq = 0
    mov_imm32 w22, 0x0001013B ; star=0, cnt=1, tok1=1, eol=';'

    ldr x0, [x19, #AST_NODE_A64.pExtra]
    cbz x0, .opts_done
    bl unquote_dup_a64
    mov x20, x0
    cbz x20, .opts_done

.opt_lp:
    mov x0, x20
    bl skip_ws_a64
    mov x20, x0
    ldrb w0, [x20, #0]
    cbz w0, .opts_done

    bl ascii_upper_a64
    cmp w0, #0x55 ; 'U' (`usebackq`)
    b.ne .chk_eol_o
    mov w0, #1
    str w0, [sp, #24]
    add x20, x20, #8
    b .opt_lp

.chk_eol_o:
    cmp w0, #0x45 ; 'E' (`eol=c`)
    b.ne .chk_skip_o
    ldrb w1, [x20, #3]
    cmp w1, #0x3D
    b.ne .adv_o1
    ldrb w0, [x20, #4]
    bic w22, w22, #0xFF
    orr w22, w22, w0
    cbz w0, .opts_done
    add x20, x20, #5
    b .opt_lp

.chk_skip_o:
    cmp w0, #0x53 ; 'S' (`skip=n`)
    b.ne .chk_delims_o
    add x0, x20, #5
    bl parse_signed_dec_a64
    str w0, [sp, #20]
    mov x20, x1
    b .opt_lp

.chk_delims_o:
    cmp w0, #0x44 ; 'D' (`delims=...`)
    b.ne .chk_tokens_o
    add x20, x20, #7
    mov x2, #0
.dl_cp:
    ldrb w0, [x20, #0]
    cbz w0, .dl_end
    cmp w0, #0x20
    b.ne .dl_st
    ldrb w1, [x20, #1]
    cbnz w1, .dl_end
.dl_st:
    cmp x2, #14
    b.hs .dl_nx
    strb w0, [sp, x2]
    add x2, x2, #1
.dl_nx:
    add x20, x20, #1
    b .dl_cp
.dl_end:
    strb wzr, [sp, x2]
    b .opt_lp

.chk_tokens_o:
    cmp w0, #0x54 ; 'T' (`tokens=...`)
    b.ne .adv_o1
    add x0, x20, #7
    bl parse_signed_dec_a64
    mov w21, w0 ; first tok
    mov x20, x1
    mov w23, #1 ; count
    mov w24, #0 ; star
.tk_spec:
    ldrb w0, [x20, #0]
    cbz w0, .tk_fin
    cmp w0, #0x20
    b.eq .tk_fin
    cmp w0, #0x2A
    b.ne .tk_rng
    mov w24, #1
    add x20, x20, #1
    b .tk_fin
.tk_rng:
    cmp w0, #0x2D
    b.ne .tk_cm
    add x0, x20, #1
    bl parse_signed_dec_a64
    mov x20, x1
    sub w23, w0, w21
    add w23, w23, #1
    b .tk_spec
.tk_cm:
    cmp w0, #0x2C
    b.ne .tk_adv
    ldrb w1, [x20, #1]
    cmp w1, #0x2A
    b.ne .tk_cm_n
    mov w24, #1
    add x20, x20, #2
    b .tk_fin
.tk_cm_n:
    add x0, x20, #1
    bl parse_signed_dec_a64
    mov x20, x1
    add w23, w23, #1
    b .tk_spec
.tk_adv:
    add x20, x20, #1
    b .tk_spec
.tk_fin:
    and w22, w22, #0xFF
    and w21, w21, #0xFF
    and w23, w23, #0xFF
    lsl w21, w21, #8
    lsl w23, w23, #16
    lsl w24, w24, #24
    orr w22, w22, w21
    orr w22, w22, w23
    orr w22, w22, w24
    b .opt_lp

.adv_o1:
    add x20, x20, #1
    b .opt_lp

.opts_done:
    str w22, [sp, #28]
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    cbz x0, .done
    mov w1, #0
    bl expand_phase34_a64
    bl skip_ws_a64
    mov x20, x0

    bl str_len_a64
    mov x2, x0
.tr_s:
    cbz x2, .done
    sub x1, x2, #1
    ldrb w0, [x20, x1]
    cmp w0, #0x20
    b.eq .tr_c
    cmp w0, #0x09
    b.ne .dispatch_src
.tr_c:
    strb wzr, [x20, x1]
    mov x2, x1
    b .tr_s

.dispatch_src:
    ldrb w0, [x20, #0]
    ldr w1, [sp, #24] ; usebackq
    cbnz w1, .ubq_mode
    cmp w0, #0x22 ; `"literal string"`
    b.eq .lit_str_src
    cmp w0, #0x27 ; `'command'`
    b.eq .cmd_src
    cmp w0, #0x60 ; `` `command` ``
    b.eq .cmd_src
    b .file_src
.ubq_mode:
    cmp w0, #0x27 ; `'literal string'`
    b.eq .lit_str_src
    cmp w0, #0x60 ; `` `command` ``
    b.eq .cmd_src
    b .file_src

.lit_str_src:
    add x20, x20, #1
    mov x0, x20
    bl str_len_a64
    cbz x0, .run_buf
    sub x0, x0, #1
    ldrb w1, [x20, x0]
    cmp w1, #0x27
    b.eq .cut_q
    cmp w1, #0x22
    b.ne .run_buf
.cut_q:
    strb wzr, [x20, x0]
    b .run_buf

.cmd_src:
    add x20, x20, #1
    mov x0, x20
    bl str_len_a64
    cbz x0, .done
    sub x0, x0, #1
    strb wzr, [x20, x0]
    mov x0, x20
    bl capture_cmd_to_buf_a64
    cbz x0, .done
    mov x20, x0
    b .run_buf

.file_src:
    mov x0, x20
    bl unquote_dup_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov w1, #0
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .done
    mov x21, x0 ; hFile
    mov_imm32 x0, 16384
    bl arena_alloc_a64
    cbz x0, .cl_f
    mov x20, x0
    mov x0, x21
    mov x1, x20
    mov_imm32 w2, 16380
    bl hal_read_handle_a64
    uxtw x0, w0
    strb wzr, [x20, x0]
    mov x0, x21
    bl hal_close_handle_a64
    b .run_buf
.cl_f:
    mov x0, x21
    bl hal_close_handle_a64
    b .done

.run_buf:
    ldr w23, [sp, #20] ; skip count
.ln_lp:
    ldrb w0, [x20, #0]
    cbz w0, .done
    mov x21, x20 ; line start
.sc_nl:
    ldrb w0, [x20, #0]
    cbz w0, .got_ln
    cmp w0, #0x0D
    b.eq .cut_cr
    cmp w0, #0x0A
    b.eq .cut_lf
    add x20, x20, #1
    b .sc_nl
.cut_cr:
    strb wzr, [x20, #0]
    add x20, x20, #1
    ldrb w0, [x20, #0]
    cmp w0, #0x0A
    b.ne .got_ln
    add x20, x20, #1
    b .got_ln
.cut_lf:
    strb wzr, [x20, #0]
    add x20, x20, #1

.got_ln:
    cbz w23, .proc_ln
    sub w23, w23, #1
    b .ln_lp
.proc_ln:
    mov x0, x19
    mov x1, x21
    mov x2, sp
    ldr w3, [sp, #28]
    bl for_f_process_line_a64
    b .ln_lp

.done:
    mov w0, #0
    add sp, sp, #48
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc exec_for_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64* (AST_FOR)
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Dispatches FOR plain, `/D`, `/R`, `/L`, and `/F`.
@proc exec_for_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #352 ; FIND_INFO_A64 (320B) at sp+0, saved depth at sp+324, hFind at sp+328, numBuf at sp+336
    mov x29, sp
    mov x19, x0
    ldrb w20, [x19, #AST_NODE_A64.bSubKind]

    cmp w20, #A64_FOR_PARSE
    b.ne .not_parse
    mov x0, x19
    bl exec_for_parse_a64
    b .ok0

.not_parse:
    cmp w20, #A64_FOR_LOOP
    b.eq .do_loop_l
    cmp w20, #A64_FOR_REC
    b.eq .do_rec_r

    ldr x21, [x19, #AST_NODE_A64.pStr1]
    cbz x21, .ok0

.pl_lp:
    mov x0, x21
    bl for_next_set_item_a64
    mov x21, x1
    cbz x0, .ok0
    mov x22, x0

    mov x1, #0
.wchk:
    ldrb w0, [x22, x1]
    cbz w0, .pl_lit
    cmp w0, #0x2A
    b.eq .pl_wild
    cmp w0, #0x3F
    b.eq .pl_wild
    add x1, x1, #1
    b .wchk

.pl_lit:
    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwForDepth]
    str w0, [sp, #324]
    ldrb w0, [x19, #AST_NODE_A64.bForVar]
    mov x1, x22
    bl for_push_var_a64
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    ldr w0, [sp, #324]
    bl for_pop_vars_to_a64
    b .pl_lp

.pl_wild:
    mov x0, x22
    bl norm_path_a64
    mov x23, x0
    mov x24, #0 ; prefix len in x22
    mov x1, #0
.hd_sc:
    ldrb w0, [x22, x1]
    cbz w0, .hd_done
    cmp w0, #0x5C
    b.eq .hd_yes
    cmp w0, #0x2F
    b.eq .hd_yes
    cmp w0, #0x3A
    b.eq .hd_yes
    add x1, x1, #1
    b .hd_sc
.hd_yes:
    add x24, x1, #1
    b .hd_nx
.hd_nx:
    add x1, x1, #1
    b .hd_sc
.hd_done:
    mov x0, x23
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .pl_lp
    str x0, [sp, #328]

.pw_lp:
    add x0, sp, #FIND_INFO_A64.szName
    ldrb w1, [x0, #0]
    cmp w1, #0x2E
    b.ne .pw_attr
    ldrb w1, [x0, #1]
    cbz w1, .pw_nx
    cmp w1, #0x2E
    b.ne .pw_attr
    ldrb w1, [x0, #2]
    cbz w1, .pw_nx

.pw_attr:
    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    cmp w20, #A64_FOR_DIRS
    b.eq .want_dir
    tbnz w0, #4, .pw_nx
    b .pw_emit
.want_dir:
    tbz w0, #4, .pw_nx

.pw_emit:
    add x0, x24, #320
    bl arena_alloc_a64
    cbz x0, .pw_nx
    mov x23, x0
    cbz x24, .pw_bare
    mov x1, x22
    mov x2, x24
    bl mem_copy_a64
.pw_bare:
    add x1, sp, #FIND_INFO_A64.szName
    bl str_copy_a64

    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwForDepth]
    str w0, [sp, #324]
    ldrb w0, [x19, #AST_NODE_A64.bForVar]
    mov x1, x23
    bl for_push_var_a64
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    ldr w0, [sp, #324]
    bl for_pop_vars_to_a64

.pw_nx:
    ldr x0, [sp, #328]
    mov x1, sp
    bl hal_find_next_a64
    cbnz w0, .pw_lp
    ldr x0, [sp, #328]
    bl hal_find_close_a64
    b .pl_lp

.do_rec_r:
    ldr x0, [x19, #AST_NODE_A64.pExtra]
    cbz x0, .rec_cwd
    mov w1, #1
    bl expand_phase34_a64
    bl norm_path_a64
    mov x1, x0
    b .rec_go
.rec_cwd:
    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.szCwd
.rec_go:
    ldr x2, [x19, #AST_NODE_A64.pStr1]
    mov x0, x19
    bl for_walk_rec_a64
    b .ok0

.do_loop_l:
    ldr x0, [x19, #AST_NODE_A64.pStr1]
    cbz x0, .ok0
    bl for_next_set_item_a64
    mov x21, x1
    cbz x0, .ok0
    bl parse_signed_dec_a64
    mov w22, w0 ; cur = start

    mov x0, x21
    bl for_next_set_item_a64
    mov x21, x1
    cbz x0, .ok0
    bl parse_signed_dec_a64
    mov w23, w0 ; step

    mov x0, x21
    bl for_next_set_item_a64
    cbz x0, .ok0
    bl parse_signed_dec_a64
    mov w24, w0 ; end

    cbz w23, .ok0

.num_lp:
    cmp w23, #0
    b.lt .chk_neg_step
    cmp w22, w24
    b.gt .ok0
    b .num_body
.chk_neg_step:
    cmp w22, w24
    b.lt .ok0
.num_body:
    mov w0, w22
    add x1, sp, #336
    bl fmt_i32_a64
    add x0, sp, #336
    bl arena_strdup_a64
    mov x21, x0

    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwForDepth]
    str w0, [sp, #324]
    ldrb w0, [x19, #AST_NODE_A64.bForVar]
    mov x1, x21
    bl for_push_var_a64
    ldr x0, [x19, #AST_NODE_A64.pLeft]
    bl exec_ast_a64
    ldr w0, [sp, #324]
    bl for_pop_vars_to_a64

    add w22, w22, w23
    b .num_lp

.ok0:
    mov w0, #0
    add sp, sp, #352
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
