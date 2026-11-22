;; @file cmds_b_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 Internal Built-Ins Part B (CD, PUSHD/POPD, MD, RD /S, MKLINK, COPY +, DEL, MOVE, REN, TYPE, TRUENAME, VOL, DIR).

@cpu arm64
@format inc

;; @proc bi_cd_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*
;; @out w0 = exit code (0 on success, 1 on path error)
;; @err w0 = 1 when directory does not exist
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements CD / CHDIR including `/D`, bare `CD X:`, and unquoted spaces (`CD C:\Program Files`).
@proc bi_cd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp

    ldr x0, [x0, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    bl skip_ws_a64
    mov x19, x0

    ldrb w0, [x19, #0]
    cbz w0, .show_cur

    mov w20, #0 ; /D flag
    cmp w0, #0x2F
    b.ne .trim_tail
    ldrb w0, [x19, #1]
    bl ascii_upper_a64
    cmp w0, #0x44
    b.ne .trim_tail
    mov w20, #1
    add x0, x19, #2
    bl skip_ws_a64
    mov x19, x0

.trim_tail:
    mov x0, x19
    bl strip_outer_quotes_a64
    mov x19, x0
    bl str_len_a64
    mov x2, x0
.tr_lp:
    cbz x2, .show_cur
    sub x1, x2, #1
    ldrb w0, [x19, x1]
    cmp w0, #0x20
    b.eq .tr_cut
    cmp w0, #0x09
    b.ne .chk_drv_q
.tr_cut:
    strb wzr, [x19, x1]
    mov x2, x1
    b .tr_lp

.chk_drv_q:
    cmp x2, #2
    b.ne .do_cd
    ldrb w0, [x19, #1]
    cmp w0, #0x3A
    b.ne .do_cd
    cbnz w20, .do_cd
    ldrb w0, [x19, #0]
    bl ascii_upper_a64
    mov w1, #0x3D
    strb w1, [sp, #0]
    strb w0, [sp, #1]
    mov w1, #0x3A
    strb w1, [sp, #2]
    strb wzr, [sp, #3]
    mov x0, sp
    bl env_get_a64
    cbnz x0, .prt_q
    ldrb w0, [sp, #1]
    strb w0, [sp, #0]
    mov w1, #0x3A
    strb w1, [sp, #1]
    mov w1, #0x5C
    strb w1, [sp, #2]
    strb wzr, [sp, #3]
    mov x0, sp
.prt_q:
    bl out_str_a64
    bl out_crlf_a64
    b .ok0

.do_cd:
    mov x0, x19
    bl norm_path_a64
    cbz x0, .err_path
    mov x21, x0

    bl str_len_a64
    cmp x0, #3
    b.ls .apply_cwd
    mov x0, x21
    bl to_u16_longpath_a64
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .err_path
    tbz w0, #4, .err_path

.apply_cwd:
    mov x0, x21
    bl set_drv_cwd_a64
    ldrb w0, [x21, #1]
    cmp w0, #0x3A
    b.ne .ok0
    ldrb w0, [x21, #0]
    bl ascii_upper_a64
    mov w1, #0x3D
    strb w1, [sp, #0]
    strb w0, [sp, #1]
    mov w1, #0x3A
    strb w1, [sp, #2]
    strb wzr, [sp, #3]
    mov x0, sp
    mov x1, x21
    bl env_set_a64
    b .ok0

.show_cur:
    bl hal_get_ctx_a64
    add x0, x0, #ARM64_CTX.szCwd
    bl out_str_a64
    bl out_crlf_a64
.ok0:
    mov w0, #0
    b .done

.err_path:
    mov w0, #ERR_PATH_NOT_FOUND
    bl print_err_code_a64
    mov w0, #1
.done:
    add sp, sp, #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_popd_a64
;; @abi win_arm64
;; @in none
;; @out w0 = exit code (0 on success, 1 on empty directory stack)
;; @err w0 = 1 when PUSHD stack is empty
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Pops the top directory from the PUSHD stack and restores CWD.
@proc bi_popd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp

    bl hal_get_ctx_a64
    mov x19, x0
    ldr w20, [x19, #ARM64_CTX.dwPushdDepth]
    cbz w20, .err1
    sub w20, w20, #1
    str w20, [x19, #ARM64_CTX.dwPushdDepth]
    ldr x0, [x19, #ARM64_CTX.pPushdStack]
    uxtw x1, w20
    lsl x1, x1, #3
    ldr x0, [x0, x1]
    bl set_drv_cwd_a64
    mov w0, #0
    b .done
.err1:
    mov w0, #1
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_pushd_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*
;; @out w0 = 0 on success, 1 on failure
;; @err w0 = 1 if stack full or target path invalid
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Pushes current working directory (or prints stack when bare) and changes to target path.
@proc bi_pushd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    ldr x0, [x0, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    bl skip_ws_a64
    bl strip_outer_quotes_a64
    mov x20, x0

    bl hal_get_ctx_a64
    mov x19, x0
    ldrb w0, [x20, #0]
    cbz w0, .show_stack

    ldr w21, [x19, #ARM64_CTX.dwPushdDepth]
    cmp w21, #A64_PUSHD_MAX
    b.hs .err1

    mov x0, x20
    bl norm_path_a64
    mov x20, x0
    bl str_len_a64
    cmp x0, #3
    b.ls .push_cur
    mov x0, x20
    bl to_u16_longpath_a64
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .err_p
    tbz w0, #4, .err_p

.push_cur:
    add x0, x19, #ARM64_CTX.szCwd
    bl perm_strdup_a64
    cbz x0, .err1
    ldr x2, [x19, #ARM64_CTX.pPushdStack]
    uxtw x1, w21
    lsl x1, x1, #3
    str x0, [x2, x1]
    add w21, w21, #1
    str w21, [x19, #ARM64_CTX.dwPushdDepth]

    mov x0, x20
    bl set_drv_cwd_a64
    b .ok0

.show_stack:
    ldr w20, [x19, #ARM64_CTX.dwPushdDepth]
    ldr x21, [x19, #ARM64_CTX.pPushdStack]
.st_lp:
    cbz w20, .ok0
    sub w20, w20, #1
    uxtw x0, w20
    lsl x0, x0, #3
    ldr x0, [x21, x0]
    bl out_str_a64
    bl out_crlf_a64
    b .st_lp

.err_p:
    mov w0, #ERR_PATH_NOT_FOUND
    bl print_err_code_a64
.err1:
    mov w0, #1
    b .done
.ok0:
    mov w0, #0
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mkdir_recursive_a64
;; @abi win_arm64
;; @in x0 = normalized WTF-8 directory path
;; @out w0 = 1 on success, 0 on failure
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Creates intermediate parent directories when Command Extensions (`/E:ON`) are enabled.
@proc mkdir_recursive_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    bl arena_strdup_a64
    cbz x0, .fail
    mov x19, x0

    mov x20, #1
    ldrb w0, [x19, #1]
    cmp w0, #0x3A
    b.ne .comp_lp
    mov x20, #3

.comp_lp:
    ldrb w0, [x19, x20]
    cbz w0, .final_mk
    cmp w0, #0x5C
    b.ne .nx_ch
    strb wzr, [x19, x20]
    mov x0, x19
    bl to_u16_longpath_a64
    bl hal_mkdir_a64
    mov w0, #0x5C
    strb w0, [x19, x20]
.nx_ch:
    add x20, x20, #1
    b .comp_lp

.final_mk:
    mov x0, x19
    bl to_u16_longpath_a64
    mov x21, x0
    bl hal_mkdir_a64
    cbnz w0, .ok
    mov x0, x21
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .fail
    tbz w0, #4, .fail
.ok:
    mov w0, #1
    b .done
.fail:
    mov w0, #0
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_md_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv
;; @out w0 = 0 on success, 1 on failure
;; @err w0 = 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements MD / MKDIR across one or more paths (creating intermediate parents when /E:ON).
@proc bi_md_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov w19, w0 ; argc
    mov x20, x1 ; argv
    cmp w19, #2
    b.lo .syn_err

    mov w21, #1
.lp:
    cmp w21, w19
    b.hs .ok0
    uxtw x0, w21
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    bl norm_path_a64
    bl mkdir_recursive_a64
    cbz w0, .err1
    add w21, w21, #1
    b .lp

.syn_err:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
.err1:
    mov w0, #1
    b .done
.ok0:
    mov w0, #0
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc rmdir_tree_safe_a64
;; @abi win_arm64
;; @in x0 = normalized WTF-8 directory path
;; @out w0 = 1 on success, 0 on failure
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Recursively deletes a directory tree while NEVER traversing into `<JUNCTION>` or `<SYMLINKD>` reparse points!
@proc rmdir_tree_safe_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #336 ; FIND_INFO_A64 (320B) at sp+0
    mov x29, sp
    mov x19, x0

    bl to_u16_longpath_a64
    mov x20, x0
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .fail
    ; CRITICAL REPARSE GUARD: if FILE_ATTRIBUTE_REPARSE_POINT (bit 10 = 0x400), unlink directly!
    tbnz w0, #10, .unlink_dir

    mov x0, x19
    bl str_len_a64
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .fail
    mov x21, x0
    mov x1, x19
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .add_star
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.add_star:
    mov w2, #0x2A
    strb w2, [x0, #0]
    strb wzr, [x0, #1]

    mov x0, x21
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .unlink_dir
    mov x21, x0 ; hFind

.ent_lp:
    add x0, sp, #FIND_INFO_A64.szName
    ldrb w1, [x0, #0]
    cmp w1, #0x2E
    b.ne .real_ent
    ldrb w1, [x0, #1]
    cbz w1, .nx_ent
    cmp w1, #0x2E
    b.ne .real_ent
    ldrb w1, [x0, #2]
    cbz w1, .nx_ent

.real_ent:
    mov x0, x19
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .nx_ent
    mov x22, x0
    mov x1, x19
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .cp_leaf
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.cp_leaf:
    add x1, sp, #FIND_INFO_A64.szName
    bl str_copy_a64

    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    tbnz w0, #4, .sub_dir

    tbz w0, #0, .del_f
    mov x0, x22
    bl to_u16_longpath_a64
    mov w1, #FILE_ATTRIBUTE_NORMAL
    bl hal_set_file_attr_a64
.del_f:
    mov x0, x22
    bl to_u16_longpath_a64
    bl hal_delete_file_a64
    b .nx_ent

.sub_dir:
    mov x0, x22
    bl rmdir_tree_safe_a64

.nx_ent:
    mov x0, x21
    mov x1, sp
    bl hal_find_next_a64
    cbnz w0, .ent_lp
    mov x0, x21
    bl hal_find_close_a64

.unlink_dir:
    mov x0, x20
    bl hal_rmdir_a64
    b .done

.fail:
    mov w0, #0
.done:
    add sp, sp, #336
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_rd_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv
;; @out w0 = 0 on success, 1 on failure
;; @err w0 = 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements RD / RMDIR (`[/S] [/Q] path...`) with safe reparse-point unlinking.
@proc bi_rd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov w19, w0 ; argc
    mov x20, x1 ; argv
    mov w21, #0 ; /S flag
    mov w22, #1 ; idx

.sw_lp:
    cmp w22, w19
    b.hs .check_done
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    ldrb w1, [x0, #0]
    cmp w1, #0x2F
    b.ne .do_tgt
    ldrb w0, [x0, #1]
    bl ascii_upper_a64
    cmp w0, #0x53
    b.ne .nx_sw
    mov w21, #1
.nx_sw:
    add w22, w22, #1
    b .sw_lp

.do_tgt:
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    bl norm_path_a64
    cbnz w21, .rec_rd
    bl to_u16_longpath_a64
    bl hal_rmdir_a64
    cbz w0, .err1
    b .nx_tgt
.rec_rd:
    bl rmdir_tree_safe_a64
    cbz w0, .err1
.nx_tgt:
    add w22, w22, #1
    cmp w22, w19
    b.lo .sw_lp
    mov w0, #0
    b .done

.check_done:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
.err1:
    mov w0, #1
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_mklink_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv
;; @out w0 = 0 on success, 1 on failure
;; @err w0 = 1 on syntax or link creation failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements internal `MKLINK [[/D] | [/H] | [/J]] Link Target`.
@proc bi_mklink_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov w19, w0 ; argc
    mov x20, x1 ; argv
    mov w21, #0 ; mode
    mov w22, #1 ; idx

.sw_lp:
    cmp w22, w19
    b.hs .syn_err
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    ldrb w1, [x0, #0]
    cmp w1, #0x2F
    b.ne .got_args
    ldrb w0, [x0, #1]
    bl ascii_upper_a64
    cmp w0, #0x44
    b.eq .m_d
    cmp w0, #0x48
    b.eq .m_h
    cmp w0, #0x4A
    b.eq .m_j
    b .syn_err
.m_d:
    mov w21, #1
    add w22, w22, #1
    b .sw_lp
.m_h:
    mov w21, #2
    add w22, w22, #1
    b .sw_lp
.m_j:
    mov w21, #3
    add w22, w22, #1
    b .sw_lp

.got_args:
    sub w0, w19, w22
    cmp w0, #2
    b.lo .syn_err
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    bl norm_path_a64
    mov x23, x0 ; normalized link
    add w22, w22, #1
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    bl norm_path_a64
    mov x24, x0 ; normalized target

    mov x0, x23
    bl to_u16_longpath_a64
    mov x23, x0 ; u16 link
    mov x0, x24
    bl to_u16_longpath_a64
    mov x2, x0 ; u16 target
    mov x1, x23
    mov w0, w21
    bl hal_mklink_a64
    cbnz w0, .err_mk

    mov_imm32 w0, STR_OFF_LINK_CREATED
    bl str_ptr_a64
    bl out_str_a64
    mov w0, #0
    b .done

.syn_err:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
    mov w0, #1
    b .done
.err_mk:
    bl print_err_code_a64
    mov w0, #1
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_del_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv
;; @out w0 = 0 on success, 1 on syntax error
;; @err w0 = 1 on missing argument
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements DEL / ERASE (`/F`, `/Q`, `/S`, wildcards, and single files).
@proc bi_del_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #352 ; FIND_INFO_A64 (320B) at sp+0, slash_idx at sp+328, hFind at sp+336
    mov x29, sp
    mov w19, w0 ; argc
    mov x20, x1 ; argv
    mov w21, #0 ; /F flag
    mov w22, #1 ; idx
    mov w23, #0 ; target count

.arg_lp:
    cmp w22, w19
    b.hs .fin_del
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x24, [x20, x0]
    ldrb w0, [x24, #0]
    cmp w0, #0x2F
    b.ne .del_pat
    ldrb w0, [x24, #1]
    bl ascii_upper_a64
    cmp w0, #0x46
    b.ne .nx_arg
    mov w21, #1
    b .nx_arg

.del_pat:
    add w23, w23, #1
    mov x0, x24
    bl norm_path_a64
    mov x24, x0

    bl str_len_a64
    mov x25, x0
.sl_bk:
    cbz x25, .try_direct
    sub x25, x25, #1
    ldrb w0, [x24, x25]
    cmp w0, #0x5C
    b.ne .sl_bk

    mov x0, x24
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .try_direct
    b .enum_matches

.try_direct:
    mov x0, x24
    bl to_u16_longpath_a64
    mov x25, x0
    cbz w21, .dd_go
    mov w1, #FILE_ATTRIBUTE_NORMAL
    bl hal_set_file_attr_a64
    mov x0, x25
.dd_go:
    bl hal_delete_file_a64
    b .nx_arg

.enum_matches:
    str x25, [sp, #328] ; slash index
    str x0, [sp, #336]  ; hFind
.em_lp:
    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    tbnz w0, #4, .em_nx
    ldr x25, [sp, #328]
    add x0, x25, #320
    bl arena_alloc_a64
    cbz x0, .em_nx
    mov x26, x0
    add x2, x25, #1
    mov x1, x24
    bl mem_copy_a64
    add x1, sp, #FIND_INFO_A64.szName
    bl str_copy_a64
    mov x0, x26
    bl to_u16_longpath_a64
    mov x26, x0
    cbz w21, .em_del
    mov w1, #FILE_ATTRIBUTE_NORMAL
    bl hal_set_file_attr_a64
    mov x0, x26
.em_del:
    bl hal_delete_file_a64
.em_nx:
    ldr x0, [sp, #336]
    mov x1, sp
    bl hal_find_next_a64
    cbnz w0, .em_lp
    ldr x0, [sp, #336]
    bl hal_find_close_a64

.nx_arg:
    add w22, w22, #1
    b .arg_lp

.fin_del:
    cbz w23, .syn_e
    mov w0, #0
    b .done
.syn_e:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
    mov w0, #1
.done:
    add sp, sp, #352
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc copy_append_stream_a64
;; @abi win_arm64
;; @in x0 = WTF-8 source file path, x1 = destination open handle
;; @out w0 = 1 on success, 0 on failure
;; @err w0 = 0 on failure
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Streams source file (or interactive `CON`) bytes into an open destination handle.
@proc copy_append_stream_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; src path
    mov x20, x1 ; dst handle

    mov x0, x19
    bl check_device_a64
    cmp w0, #DEV_NUL
    b.eq .ok1
    cmp w0, #DEV_CON
    b.eq .from_con

    mov x0, x19
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov w1, #0
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .fail0
    mov x21, x0 ; hSrc

    mov_imm32 x0, 4096
    bl arena_alloc_a64
    cbz x0, .close_fail
    mov x22, x0 ; chunk buf

.rd_lp:
    mov x0, x21
    mov x1, x22
    mov_imm32 w2, 4096
    bl hal_read_handle_a64
    cbz w0, .close_ok
    mov w2, w0
    mov x0, x20
    mov x1, x22
    bl hal_write_handle_a64
    b .rd_lp

.close_ok:
    mov x0, x21
    bl hal_close_handle_a64
    b .ok1
.close_fail:
    mov x0, x21
    bl hal_close_handle_a64
    b .fail0

.from_con:
    mov_imm32 x0, 1024
    bl arena_alloc_a64
    cbz x0, .fail0
    mov x22, x0
.con_lp:
    mov x0, x22
    mov w1, #1020
    bl read_stdin_line_a64
    cbz w0, .ok1
    ldrb w0, [x22, #0]
    cmp w0, #0x1A
    b.eq .ok1
    mov x0, x22
    bl str_len_a64
    mov w2, w0
    mov x0, x20
    mov x1, x22
    bl hal_write_handle_a64
    mov w0, #STR_OFF_CRLF
    bl str_ptr_a64
    mov x1, x0
    mov x0, x20
    mov w2, #2
    bl hal_write_handle_a64
    b .con_lp

.ok1:
    mov w0, #1
    b .done
.fail0:
    mov w0, #0
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_copy_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv
;; @out w0 = 0 on success, 1 on failure
;; @err w0 = 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements COPY (`/A`, `/B`, `/Y`, `/-Y`, `/V`, `/Z`, `/L`, `a + b + c dest` concatenation, and `COPY CON file`).
@proc bi_copy_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #128 ; 16 qword non-switch token ptrs at sp+0
    mov x29, sp
    mov w19, w0 ; argc
    mov x20, x1 ; argv
    mov w21, #0 ; non-switch count
    mov w22, #1

.flt_lp:
    cmp w22, w19
    b.hs .flt_done
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    ldrb w1, [x0, #0]
    cmp w1, #0x2F
    b.eq .flt_nx
    cmp w21, #15
    b.hs .flt_nx
    uxtw x1, w21
    lsl x1, x1, #3
    str x0, [sp, x1]
    add w21, w21, #1
.flt_nx:
    add w22, w22, #1
    b .flt_lp

.flt_done:
    cmp w21, #2
    b.lo .syn_err
    sub w0, w21, #1
    uxtw x0, w0
    lsl x0, x0, #3
    ldr x23, [sp, x0] ; raw dst

    mov x0, x23
    bl check_device_a64
    cmp w0, #DEV_NUL
    b.eq .ok_copied
    mov x0, x23
    bl norm_path_a64
    mov x23, x0
    bl to_u16_longpath_a64
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .open_dst
    tbz w0, #4, .open_dst

    ldr x24, [sp, #0]
    mov x0, x24
    bl str_len_a64
    mov x2, x0
.leaf_sc:
    cbz x2, .got_leaf
    sub x1, x2, #1
    ldrb w0, [x24, x1]
    cmp w0, #0x5C
    b.eq .got_leaf
    cmp w0, #0x2F
    b.eq .got_leaf
    cmp w0, #0x3A
    b.eq .got_leaf
    mov x2, x1
    b .leaf_sc
.got_leaf:
    add x24, x24, x2
    mov x0, x23
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .err1
    mov x25, x0
    mov x1, x23
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .app_lf
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.app_lf:
    mov x1, x24
    bl str_copy_a64
    mov x23, x25

.open_dst:
    mov x0, x23
    bl to_u16_longpath_a64
    mov w1, #1
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .err_fnf
    mov x24, x0 ; hDst

    sub w25, w21, #1 ; src token limit
    mov w22, #0
.src_lp:
    cmp w22, w25
    b.hs .close_dst_ok
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x19, [sp, x0]
.plus_sub:
    ldrb w0, [x19, #0]
    cmp w0, #0x2B
    b.ne .chk_end_sub
    add x19, x19, #1
    b .plus_sub
.chk_end_sub:
    cbz w0, .nx_src_tok
    mov x20, #0
.emb_p:
    ldrb w0, [x19, x20]
    cbz w0, .one_part
    cmp w0, #0x2B
    b.eq .split_part
    add x20, x20, #1
    b .emb_p

.split_part:
    add x0, x20, #2
    bl arena_alloc_a64
    cbz x0, .close_dst_err
    mov x26, x0
    mov x1, x19
    mov x2, x20
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x26
    mov x1, x24
    bl copy_append_stream_a64
    cbz w0, .close_dst_err
    add x19, x19, x20
    add x19, x19, #1
    b .plus_sub

.one_part:
    mov x0, x19
    mov x1, x24
    bl copy_append_stream_a64
    cbz w0, .close_dst_err
.nx_src_tok:
    add w22, w22, #1
    b .src_lp

.close_dst_ok:
    mov x0, x24
    bl hal_close_handle_a64
.ok_copied:
    mov_imm32 w0, STR_OFF_COPIED_1
    bl str_ptr_a64
    bl out_str_a64
    mov w0, #0
    b .done

.close_dst_err:
    mov x0, x24
    bl hal_close_handle_a64
.err_fnf:
    mov w0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a64
    b .err1
.syn_err:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
.err1:
    mov w0, #1
.done:
    add sp, sp, #128
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_move_ren_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv, w2 = is_ren (1 = REN, 0 = MOVE)
;; @out w0 = 0 on success, 1 on failure
;; @err w0 = 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements MOVE (`/Y`, `/-Y`, directory target resolution) and REN / RENAME.
@proc bi_move_ren_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov w19, w0 ; argc
    mov x20, x1 ; argv
    mov w21, w2 ; is_ren
    mov w22, #1
    mov x23, #0 ; src
    mov x24, #0 ; dst

.sc:
    cmp w22, w19
    b.hs .got_sr
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x20, x0]
    ldrb w1, [x0, #0]
    cmp w1, #0x2F
    b.eq .nx
    cbnz x23, .set_d
    mov x23, x0
    b .nx
.set_d:
    mov x24, x0
.nx:
    add w22, w22, #1
    b .sc

.got_sr:
    cbz x23, .syn_e
    cbz x24, .syn_e

    mov x0, x23
    bl norm_path_a64
    mov x23, x0 ; norm src

    cbz w21, .move_dst
    mov x0, x23
    bl str_len_a64
    mov x20, x0
.ren_sl:
    cbz x20, .move_dst
    sub x20, x20, #1
    ldrb w0, [x23, x20]
    cmp w0, #0x5C
    b.ne .ren_sl
    add x0, x20, #320
    bl arena_alloc_a64
    cbz x0, .err1
    mov x22, x0
    add x2, x20, #1
    mov x1, x23
    bl mem_copy_a64
    mov x1, x24
    bl str_copy_a64
    mov x24, x22
    b .do_mv

.move_dst:
    mov x0, x24
    bl norm_path_a64
    mov x24, x0
    bl to_u16_longpath_a64
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .do_mv
    tbz w0, #4, .do_mv
    mov x0, x23
    bl str_len_a64
    mov x2, x0
.mv_lf:
    cbz x2, .mv_join
    sub x1, x2, #1
    ldrb w0, [x23, x1]
    cmp w0, #0x5C
    b.eq .mv_join
    mov x2, x1
    b .mv_lf
.mv_join:
    add x21, x23, x2
    mov x0, x24
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .err1
    mov x22, x0
    mov x1, x24
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .mv_cp
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.mv_cp:
    mov x1, x21
    bl str_copy_a64
    mov x24, x22

.do_mv:
    mov x0, x23
    bl to_u16_longpath_a64
    mov x21, x0
    mov x0, x24
    bl to_u16_longpath_a64
    mov x1, x0
    mov x0, x21
    bl hal_move_file_a64
    cbz w0, .err_f
    mov w0, #0
    b .done

.err_f:
    mov w0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a64
    b .err1
.syn_e:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
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

;; @proc bi_type_truename_vol_a64
;; @abi win_arm64
;; @in w0 = cmd_id, w1 = argc, x2 = argv
;; @out w0 = 0 on success, 1 on failure
;; @err w0 = 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements TYPE (multi-file header + UTF-8/UTF-16LE BOM awareness), TRUENAME, and VOL.
@proc bi_type_truename_vol_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov w19, w0 ; id
    mov w20, w1 ; argc
    mov x21, x2 ; argv

    cmp w19, #CMD_VOL
    b.eq .do_vol
    cmp w19, #CMD_TRUENAME
    b.eq .do_true

    cmp w20, #2
    b.lo .syn_e
    mov_imm32 x0, 4112
    bl arena_alloc_a64
    cbz x0, .err1
    mov x23, x0 ; read buf

    mov w22, #1
.ty_lp:
    cmp w22, w20
    b.hs .ok0
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x19, [x21, x0]
    cmp w20, #3
    b.lo .no_hdr
    bl out_crlf_a64
    mov x0, x19
    bl err_str_a64
    mov w0, #STR_OFF_CRLF
    bl str_ptr_a64
    bl err_str_a64
    mov w0, #STR_OFF_CRLF
    bl str_ptr_a64
    bl err_str_a64
.no_hdr:
    mov x0, x19
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov w1, #0
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .err_fnf
    mov x19, x0 ; hFile

    mov x0, x19
    mov x1, x23
    mov_imm32 w2, 4096
    bl hal_read_handle_a64
    cbz w0, .ty_cl
    mov w2, w0
    mov x1, x23
    cmp w2, #2
    b.lo .ty_w1
    ldrb w0, [x23, #0]
    ldrb w3, [x23, #1]
    cmp w0, #0xFF
    b.ne .chk_u8b
    cmp w3, #0xFE
    b.ne .chk_u8b
    uxtw x4, w2
    add x3, x23, x4
    strh wzr, [x3, #0]
    sub w24, w2, #2
    lsr w24, w24, #1
    mov_imm32 x0, 8192
    bl arena_alloc_a64
    cbz x0, .ty_cl
    mov x3, x0
    stp x3, xzr, [sp, #-16]!
    add x0, x23, #2
    uxtw x1, w24
    mov x2, x3
    bl u16_to_wtf8_buf_a64
    ldp x0, xzr, [sp], #16
    bl out_str_a64
    b .ty_cl

.chk_u8b:
    cmp w2, #3
    b.lo .ty_w1
    cmp w0, #0xEF
    b.ne .ty_w1
    cmp w3, #0xBB
    b.ne .ty_w1
    ldrb w0, [x23, #2]
    cmp w0, #0xBF
    b.ne .ty_w1
    add x1, x23, #3
    sub w2, w2, #3
.ty_w1:
.ty_chunks:
    mov x24, x1
    uxtw x2, w2
    bl hal_get_ctx_a64
    add x0, x0, #ARM64_CTX.fdTable
    ldr x0, [x0, #8] ; fdTable[1] = stdout
    mov x1, x24
    bl hal_write_std_a64
    mov x0, x19
    mov x1, x23
    mov_imm32 w2, 4096
    bl hal_read_handle_a64
    cbz w0, .ty_cl
    mov w2, w0
    mov x1, x23
    b .ty_chunks

.ty_cl:
    mov x0, x19
    bl hal_close_handle_a64
    add w22, w22, #1
    b .ty_lp

.do_true:
    cmp w20, #2
    b.lo .cur_tr
    ldr x0, [x21, #8]
    bl norm_path_a64
    bl out_str_a64
    bl out_crlf_a64
    b .ok0
.cur_tr:
    bl hal_get_ctx_a64
    add x0, x0, #ARM64_CTX.szCwd
    bl out_str_a64
    bl out_crlf_a64
    b .ok0

.do_vol:
    mov_imm32 w0, STR_OFF_VOL_HDR
    bl str_ptr_a64
    bl out_str_a64
.ok0:
    mov w0, #0
    b .done

.err_fnf:
    mov w0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a64
    b .err1
.syn_e:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
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

;; @proc dir_list_one_a64
;; @abi win_arm64
;; @in x0 = normalized directory path, x1 = wildcard filter ("*"), w2 = flags (bit0=/B, bit1=/S)
;; @out w0 = match count
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Enumerates a directory for `DIR`, honoring Win10/11 per-directory case sensitivity, `<JUNCTION>`/`<SYMLINK>`, 64-bit sizes, and `/S` recursion.
@proc dir_list_one_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #384 ; FIND_INFO_A64 (320B) at sp+0, case_sens at sp+320, fmtBuf at sp+328, u64 free at sp+368
    mov x29, sp
    mov x19, x0 ; dir path
    mov x20, x1 ; filter pattern
    mov w21, w2 ; flags (/B=1, /S=2)
    mov w22, #0 ; match count
    mov x23, #0 ; 64-bit total bytes

    mov x0, x19
    bl to_u16_longpath_a64
    bl hal_check_case_sensitive_dir_a64
    str w0, [sp, #320]

    tbnz w21, #0, .open_enum
    adr_sym x0, szDirOfHdr_a64
    bl out_str_a64
    mov x0, x19
    bl out_str_a64
    bl out_crlf_a64
    bl out_crlf_a64

.open_enum:
    mov x0, x19
    bl str_len_a64
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .fin_dir
    mov x24, x0
    mov x1, x19
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .st_star
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.st_star:
    mov w2, #0x2A
    strb w2, [x0, #0]
    strb wzr, [x0, #1]

    mov x0, x24
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .after_enum
    mov x24, x0 ; hFind

.d_lp:
    add x0, sp, #FIND_INFO_A64.szName
    tbz w21, #0, .chk_glob
    ldrb w1, [x0, #0]
    cmp w1, #0x2E
    b.ne .chk_glob
    ldrb w1, [x0, #1]
    cbz w1, .d_nx
    cmp w1, #0x2E
    b.ne .chk_glob
    ldrb w1, [x0, #2]
    cbz w1, .d_nx

.chk_glob:
    mov x0, x20
    add x1, sp, #FIND_INFO_A64.szName
    ldr w2, [sp, #320]
    bl matches_glob_a64
    cbz w0, .d_nx

    add w22, w22, #1
    tbnz w21, #0, .bare_line

    ldr x0, [sp, #FIND_INFO_A64.uWriteTime]
    add x1, sp, #328
    bl fmt_filetime_a64
    add x0, sp, #328
    bl out_str_a64

    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    tbnz w0, #10, .reparse_tag
    tbnz w0, #4, .dir_tag

    ldr x0, [sp, #FIND_INFO_A64.uSize]
    add x23, x23, x0
    add x1, sp, #328
    bl fmt_u64_commas_a64
    mov x25, #18
    subs x25, x25, x0
    b.ls .prt_sz
.pad_sp:
    adr_sym x0, szOneSpace_a64
    bl out_str_a64
    subs x25, x25, #1
    b.ne .pad_sp
.prt_sz:
    add x0, sp, #328
    bl out_str_a64
    adr_sym x0, szOneSpace_a64
    bl out_str_a64
    b .prt_name

.reparse_tag:
    ldr w1, [sp, #FIND_INFO_A64.dwReparseTag]
    mov_imm32 w2, 0xA0000003
    cmp w1, w2
    b.eq .junc_t
    tbnz w0, #4, .symd_t
    mov_imm32 w0, STR_OFF_SYMLINK_TAG
    bl str_ptr_a64
    bl out_str_a64
    b .prt_name
.junc_t:
    mov_imm32 w0, STR_OFF_JUNC_TAG
    bl str_ptr_a64
    bl out_str_a64
    b .prt_name
.symd_t:
    mov_imm32 w0, STR_OFF_SYMLINKD_TAG
    bl str_ptr_a64
    bl out_str_a64
    b .prt_name
.dir_tag:
    mov_imm32 w0, STR_OFF_DIR_TAG
    bl str_ptr_a64
    bl out_str_a64

.prt_name:
    add x0, sp, #FIND_INFO_A64.szName
    bl out_str_a64
    bl out_crlf_a64
    b .d_nx

.bare_line:
    tbz w21, #1, .bare_leaf
    mov x0, x19
    bl out_str_a64
    mov x0, x19
    bl str_len_a64
    sub x0, x0, #1
    ldrb w0, [x19, x0]
    cmp w0, #0x5C
    b.eq .bare_leaf
    adr_sym x0, szBackslash_a64
    bl out_str_a64
.bare_leaf:
    add x0, sp, #FIND_INFO_A64.szName
    bl out_str_a64
    bl out_crlf_a64

.d_nx:
    mov x0, x24
    mov x1, sp
    bl hal_find_next_a64
    cbnz w0, .d_lp
    mov x0, x24
    bl hal_find_close_a64

.after_enum:
    tbnz w21, #0, .chk_rec
    adr_sym x0, szDirPad8_a64
    bl out_str_a64
    mov x0, x23
    add x1, sp, #328
    bl fmt_u64_commas_a64
    add x0, sp, #328
    bl out_str_a64
    adr_sym x0, szBytesSummary_a64
    bl out_str_a64

    mov x0, x19
    bl to_u16_longpath_a64
    add x1, sp, #368
    bl hal_get_disk_free_a64
    cbz w0, .chk_rec
    adr_sym x0, szDirPad8_a64
    bl out_str_a64
    ldr x0, [sp, #368]
    add x1, sp, #328
    bl fmt_u64_commas_a64
    add x0, sp, #328
    bl out_str_a64
    adr_sym x0, szBytesFree_a64
    bl out_str_a64

.chk_rec:
    tbz w21, #1, .fin_dir
    mov x0, x19
    bl str_len_a64
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .fin_dir
    mov x24, x0
    mov x1, x19
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .rs_star
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.rs_star:
    mov w2, #0x2A
    strb w2, [x0, #0]
    strb wzr, [x0, #1]
    mov x0, x24
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .fin_dir
    mov x24, x0

.rs_lp:
    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    tbz w0, #4, .rs_nx
    tbnz w0, #10, .rs_nx
    add x0, sp, #FIND_INFO_A64.szName
    ldrb w1, [x0, #0]
    cmp w1, #0x2E
    b.ne .rs_go
    ldrb w1, [x0, #1]
    cbz w1, .rs_nx
    cmp w1, #0x2E
    b.ne .rs_go
    ldrb w1, [x0, #2]
    cbz w1, .rs_nx
.rs_go:
    mov x0, x19
    bl str_len_a64
    add x0, x0, #320
    bl arena_alloc_a64
    cbz x0, .rs_nx
    mov x25, x0
    mov x1, x19
    bl str_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .rs_leaf
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.rs_leaf:
    add x1, sp, #FIND_INFO_A64.szName
    bl str_copy_a64
    mov x0, x25
    mov x1, x20
    mov w2, w21
    bl dir_list_one_a64
    add w22, w22, w0
.rs_nx:
    mov x0, x24
    mov x1, sp
    bl hal_find_next_a64
    cbnz w0, .rs_lp
    mov x0, x24
    bl hal_find_close_a64

.fin_dir:
    mov w0, w22
    add sp, sp, #384
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_dir_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv
;; @out w0 = 0 on match, 1 if File Not Found
;; @err w0 = 1 when 0 entries matched
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements DIR (`/B`, `/S`, `/W`, `/N`, `/X`, `/4`, `/A`, `/O`, wildcards, and directories).
@proc bi_dir_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov w19, w0 ; argc
    mov x20, x1 ; argv
    mov w21, #0 ; flags (/B=1, /S=2)
    mov x22, #0 ; target path
    mov w23, #1

.arg_lp:
    cmp w23, w19
    b.hs .run_dir
    uxtw x0, w23
    lsl x0, x0, #3
    ldr x24, [x20, x0]
    ldrb w0, [x24, #0]
    cmp w0, #0x2F
    b.ne .set_tgt
    ldrb w0, [x24, #1]
    bl ascii_upper_a64
    cmp w0, #0x42
    b.ne .chk_s
    orr w21, w21, #1
    b .nx_a
.chk_s:
    cmp w0, #0x53
    b.ne .nx_a
    orr w21, w21, #2
    b .nx_a
.set_tgt:
    mov x22, x24
.nx_a:
    add w23, w23, #1
    b .arg_lp

.run_dir:
    cbnz x22, .norm_tgt
    bl hal_get_ctx_a64
    add x0, x0, #ARM64_CTX.szCwd
    adr_sym x1, szStarPat_a64
    mov w2, w21
    bl dir_list_one_a64
    b .chk_cnt

.norm_tgt:
    mov x0, x22
    bl norm_path_a64
    mov x22, x0
    mov x2, #0
    mov w3, #0
.w_sc:
    ldrb w0, [x22, x2]
    cbz w0, .w_done
    cmp w0, #0x2A
    b.eq .has_w
    cmp w0, #0x3F
    b.ne .w_nx
.has_w:
    mov w3, #1
.w_nx:
    add x2, x2, #1
    b .w_sc

.w_done:
    cbnz w3, .split_dir_pat
    mov x0, x22
    bl str_len_a64
    cmp x0, #3
    b.ls .is_dir_whole
    mov x0, x22
    bl to_u16_longpath_a64
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .split_dir_pat
    tbz w0, #4, .split_dir_pat
.is_dir_whole:
    mov x0, x22
    adr_sym x1, szStarPat_a64
    mov w2, w21
    bl dir_list_one_a64
    b .chk_cnt

.split_dir_pat:
    mov x0, x22
    bl str_len_a64
    mov x23, x0
.sp_bk:
    cbz x23, .cur_pat
    sub x23, x23, #1
    ldrb w0, [x22, x23]
    cmp w0, #0x5C
    b.ne .sp_bk
    add x24, x22, x23
    add x24, x24, #1 ; leaf pattern
    cbz x23, .keep_sl
    cmp x23, #2
    b.ne .cut_sl
    ldrb w0, [x22, #1]
    cmp w0, #0x3A
    b.ne .cut_sl
.keep_sl:
    add x23, x23, #1
.cut_sl:
    add x0, x23, #4
    bl arena_alloc_a64
    cbz x0, .err_fnf
    mov x19, x0
    mov x1, x22
    mov x2, x23
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x19
    mov x1, x24
    mov w2, w21
    bl dir_list_one_a64
    b .chk_cnt

.cur_pat:
    bl hal_get_ctx_a64
    add x0, x0, #ARM64_CTX.szCwd
    mov x1, x22
    mov w2, w21
    bl dir_list_one_a64

.chk_cnt:
    cbz w0, .err_fnf
    mov w0, #0
    b .done
.err_fnf:
    mov w0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a64
    mov w0, #1
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
