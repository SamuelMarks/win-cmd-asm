;; @file cmds_b_a32.inc
;; @isa armv4
;; @desc ARM32 Internal Built-Ins Part B (CD, PUSHD/POPD, MD, RD /S, MKLINK, COPY +, DEL, MOVE, REN, TYPE, TRUENAME, VOL, DIR).

@cpu armv4
@format inc

;; @proc bi_cd_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*
;; @out r0 = exit code (0 on success, 1 on path error)
;; @err r0 = 1 when directory does not exist
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements CD / CHDIR including `/D`, bare `CD X:`, unquoted spaces (`CD C:\Program Files`), and WinCE rootless `\Storage Card`.
@proc bi_cd_a32
    push {r4, r5, r6, lr}
    sub sp, sp, #16
    ldr r0, [r0, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    bl skip_ws_a32
    mov r4, r0

    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .show_cur

    mov r5, #0 ; /D flag
    cmp r0, #0x2F ; '/'
    bne .trim_tail
    ldrb r0, [r4, #1]
    bl ascii_upper_a32
    cmp r0, #0x44 ; 'D'
    bne .trim_tail
    mov r5, #1
    add r0, r4, #2
    bl skip_ws_a32
    mov r4, r0

.trim_tail:
    mov r0, r4
    bl strip_outer_quotes_a32
    mov r4, r0
    bl str_len_a32
    mov r2, r0
.tr_lp:
    cmp r2, #0
    beq .show_cur
    sub r1, r2, #1
    ldrb r0, [r4, r1]
    cmp r0, #0x20
    beq .tr_cut
    cmp r0, #0x09
    bne .chk_drv_q
.tr_cut:
    mov r0, #0
    strb r0, [r4, r1]
    mov r2, r1
    b .tr_lp

.chk_drv_q:
    ; Bare `CD X:` (len == 2, [1] == ':', no /D) prints `=X:`
    cmp r2, #2
    bne .do_cd
    ldrb r0, [r4, #1]
    cmp r0, #0x3A
    bne .do_cd
    cmp r5, #0
    bne .do_cd
    ldrb r0, [r4, #0]
    bl ascii_upper_a32
    mov r1, #0x3D
    strb r1, [sp, #0]
    strb r0, [sp, #1]
    mov r1, #0x3A
    strb r1, [sp, #2]
    mov r1, #0
    strb r1, [sp, #3]
    mov r0, sp
    bl env_get_a32
    cmp r0, #0
    bne .prt_q
    ldrb r0, [sp, #1]
    strb r0, [sp, #0]
    mov r1, #0x3A
    strb r1, [sp, #1]
    mov r1, #0x5C
    strb r1, [sp, #2]
    mov r1, #0
    strb r1, [sp, #3]
    mov r0, sp
.prt_q:
    bl out_str_a32
    bl out_crlf_a32
    mov r0, #0
    b .done

.do_cd:
    mov r0, r4
    bl norm_path_a32
    cmp r0, #0
    beq .err_path
    mov r6, r0

    ; Root paths (`` on WinCE or `X:` len <= 3) always exist; deeper paths verify directory attribute
    mov r0, r6
    bl str_len_a32
    cmp r0, #3
    bls .apply_cwd
    mov r0, r6
    bl to_u16_longpath_a32
    bl hal_get_file_attr
    cmn r0, #1
    beq .err_path
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .err_path

.apply_cwd:
    mov r0, r6
    bl set_drv_cwd_a32
    ; Also sync `=X:` hidden env key if drive-lettered
    ldrb r0, [r6, #1]
    cmp r0, #0x3A
    bne .ok0
    ldrb r0, [r6, #0]
    bl ascii_upper_a32
    mov r1, #0x3D
    strb r1, [sp, #0]
    strb r0, [sp, #1]
    mov r1, #0x3A
    strb r1, [sp, #2]
    mov r1, #0
    strb r1, [sp, #3]
    mov r0, sp
    mov r1, r6
    bl env_set_a32
    b .ok0

.show_cur:
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r0, r0, r1
    bl out_str_a32
    bl out_crlf_a32
.ok0:
    mov r0, #0
    b .done

.err_path:
    mov r0, #ERR_PATH_NOT_FOUND
    bl print_err_code_a32
    mov r0, #1
.done:
    add sp, sp, #16
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc bi_popd_a32
;; @abi arm_ce
;; @in none
;; @out r0 = exit code (0 on success, 1 on empty directory stack)
;; @err r0 = 1 when PUSHD stack is empty
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Pops the top directory from the PUSHD stack and restores CWD.
@proc bi_popd_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    bl hal_get_ctx
    mov r4, r0
    ldr r5, [r4, #ARM32_CTX.dwPushdDepth]
    cmp r5, #0
    beq .err1
    sub r5, r5, #1
    str r5, [r4, #ARM32_CTX.dwPushdDepth]
    ldr r0, [r4, #ARM32_CTX.pPushdStack]
    lsl r1, r5, #2
    ldr r0, [r0, r1]
    bl set_drv_cwd_a32
    mov r0, #0
    b .done
.err1:
    mov r0, #1
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc bi_pushd_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*
;; @out r0 = 0 on success, 1 on failure
;; @err r0 = 1 if stack full or target path invalid
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Pushes current working directory (or prints stack when bare) and changes to target path.
@proc bi_pushd_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r6, r0
    ldr r0, [r6, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    bl skip_ws_a32
    bl strip_outer_quotes_a32
    mov r5, r0

    bl hal_get_ctx
    mov r4, r0
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .show_stack

    ldr r7, [r4, #ARM32_CTX.dwPushdDepth]
    cmp r7, #A32_PUSHD_MAX
    bcs .err1

    ; Verify target directory first
    mov r0, r5
    bl norm_path_a32
    mov r5, r0
    bl str_len_a32
    cmp r0, #3
    bls .push_cur
    mov r0, r5
    bl to_u16_longpath_a32
    bl hal_get_file_attr
    cmn r0, #1
    beq .err_p
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .err_p

.push_cur:
    mov_imm32 r0, 512
    bl perm_alloc_a32
    cmp r0, #0
    beq .err1
    mov r2, r0
    mov_imm32 r1, ARM32_CTX.szCwd
    add r1, r4, r1
    push {r2, r3}
    bl str_copy_a32
    pop {r2, r3}
    ldr r0, [r4, #ARM32_CTX.pPushdStack]
    lsl r1, r7, #2
    str r2, [r0, r1]
    add r7, r7, #1
    str r7, [r4, #ARM32_CTX.dwPushdDepth]

    mov r0, r5
    bl set_drv_cwd_a32
    mov r0, #0
    b .done

.show_stack:
    ldr r5, [r4, #ARM32_CTX.dwPushdDepth]
    ldr r6, [r4, #ARM32_CTX.pPushdStack]
.st_lp:
    cmp r5, #0
    beq .ok0
    sub r5, r5, #1
    lsl r0, r5, #2
    ldr r0, [r6, r0]
    bl out_str_a32
    bl out_crlf_a32
    b .st_lp

.err_p:
    mov r0, #ERR_PATH_NOT_FOUND
    bl print_err_code_a32
.err1:
    mov r0, #1
    b .done
.ok0:
    mov r0, #0
.done:
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc mkdir_recursive_a32
;; @abi arm_ce
;; @in r0 = normalized WTF-8 directory path
;; @out r0 = 1 on success, 0 on failure
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Creates intermediate parent directories when Command Extensions (`/E:ON`) are enabled.
@proc mkdir_recursive_a32
    push {r4, r5, r6, lr}
    mov r0, r0
    bl arena_strdup_a32
    cmp r0, #0
    beq .fail
    mov r4, r0

    ; Skip drive root `X:` or WinCE root ``
    mov r5, #1
    ldrb r0, [r4, #1]
    cmp r0, #0x3A
    bne .comp_lp
    mov r5, #3

.comp_lp:
    ldrb r0, [r4, r5]
    cmp r0, #0
    beq .final_mk
    cmp r0, #0x5C
    bne .nx_ch
    mov r6, #0
    strb r6, [r4, r5]
    mov r0, r4
    bl to_u16_longpath_a32
    bl hal_mkdir
    mov r6, #0x5C
    strb r6, [r4, r5]
.nx_ch:
    add r5, r5, #1
    b .comp_lp

.final_mk:
    mov r0, r4
    bl to_u16_longpath_a32
    mov r6, r0
    bl hal_mkdir
    cmp r0, #0
    bne .ok
    ; Succeed if already exists as directory
    mov r0, r6
    bl hal_get_file_attr
    cmn r0, #1
    beq .fail
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .fail
.ok:
    mov r0, #1
    pop {r4, r5, r6, lr}
    bx lr
.fail:
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc bi_md_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv
;; @out r0 = 0 on success, 1 on failure
;; @err r0 = 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements MD / MKDIR across one or more paths (creating intermediate parents when /E:ON).
@proc bi_md_a32
    push {r4, r5, r6, lr}
    mov r4, r0 ; argc
    mov r5, r1 ; argv
    cmp r4, #2
    bcc .syn_err

    mov r6, #1
.lp:
    cmp r6, r4
    bcs .ok0
    lsl r0, r6, #2
    ldr r0, [r5, r0]
    bl norm_path_a32
    bl mkdir_recursive_a32
    cmp r0, #0
    beq .err1
    add r6, r6, #1
    b .lp

.syn_err:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
.err1:
    mov r0, #1
    pop {r4, r5, r6, lr}
    bx lr
.ok0:
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc rmdir_tree_safe_a32
;; @abi arm_ce
;; @in r0 = normalized WTF-8 directory path
;; @out r0 = 1 on success, 0 on failure
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Recursively deletes a directory tree while NEVER traversing into `<JUNCTION>` or `<SYMLINKD>` reparse points!
@proc rmdir_tree_safe_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #340 ; FIND_INFO_A32 (320B) at sp+0
    mov r4, r0

    mov r0, r4
    bl to_u16_longpath_a32
    mov r5, r0
    bl hal_get_file_attr
    cmn r0, #1
    beq .fail
    ; CRITICAL REPARSE GUARD: if FILE_ATTRIBUTE_REPARSE_POINT (0x0400), unlink directly via hal_rmdir!
    mov_imm32 r1, FILE_ATTRIBUTE_REPARSE_POINT
    tst r0, r1
    bne .unlink_dir

    ; Enumerate `r4\*`
    mov r0, r4
    bl str_len_a32
    add r0, r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .fail
    mov r6, r0
    mov r1, r4
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .add_star
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.add_star:
    mov r2, #0x2A ; '*'
    strb r2, [r0, #0]
    mov r2, #0
    strb r2, [r0, #1]

    mov r0, r6
    bl to_u16_longpath_a32
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .unlink_dir
    mov r6, r0 ; hFind

.ent_lp:
    add r0, sp, #FIND_INFO_A32.szName
    ldrb r1, [r0, #0]
    cmp r1, #0x2E ; '.' or '..'
    bne .real_ent
    ldrb r1, [r0, #1]
    cmp r1, #0
    beq .nx_ent
    cmp r1, #0x2E
    bne .real_ent
    ldrb r1, [r0, #2]
    cmp r1, #0
    beq .nx_ent

.real_ent:
    ; Build child path `r4\szName`
    mov r0, r4
    bl str_len_a32
    mov_imm32 r1, 300
    add r0, r0, r1
    bl arena_alloc_a32
    cmp r0, #0
    beq .nx_ent
    mov r7, r0
    mov r1, r4
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .cp_leaf
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.cp_leaf:
    add r1, sp, #FIND_INFO_A32.szName
    bl str_copy_a32

    ldr r0, [sp, #FIND_INFO_A32.dwAttr]
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .sub_dir

    ; Strip read-only if set, then delete file
    tst r0, #FILE_ATTRIBUTE_READONLY
    beq .del_f
    mov r0, r7
    bl to_u16_longpath_a32
    mov r1, #FILE_ATTRIBUTE_NORMAL
    bl hal_set_file_attr
.del_f:
    mov r0, r7
    bl to_u16_longpath_a32
    bl hal_delete_file
    b .nx_ent

.sub_dir:
    mov r0, r7
    bl rmdir_tree_safe_a32

.nx_ent:
    mov r0, r6
    mov r1, sp
    bl hal_find_next
    cmp r0, #0
    bne .ent_lp

    mov r0, r6
    bl hal_find_close

.unlink_dir:
    mov r0, r5
    bl hal_rmdir
    add sp, sp, #340
    pop {r4, r5, r6, r7, lr}
    bx lr

.fail:
    mov r0, #0
    add sp, sp, #340
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc bi_rd_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv
;; @out r0 = 0 on success, 1 on failure
;; @err r0 = 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements RD / RMDIR (`[/S] [/Q] path...`) with safe reparse-point unlinking.
@proc bi_rd_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0 ; argc
    mov r5, r1 ; argv
    mov r6, #0 ; /S flag
    mov r7, #1 ; arg index

.sw_lp:
    cmp r7, r4
    bcs .check_done
    lsl r0, r7, #2
    ldr r0, [r5, r0]
    ldrb r1, [r0, #0]
    cmp r1, #0x2F
    bne .do_tgt
    ldrb r0, [r0, #1]
    bl ascii_upper_a32
    cmp r0, #0x53 ; '/S'
    bne .nx_sw
    mov r6, #1
.nx_sw:
    add r7, r7, #1
    b .sw_lp

.do_tgt:
    lsl r0, r7, #2
    ldr r0, [r5, r0]
    bl norm_path_a32
    cmp r6, #0
    bne .rec_rd
    bl to_u16_longpath_a32
    bl hal_rmdir
    cmp r0, #0
    beq .err1
    b .nx_tgt
.rec_rd:
    bl rmdir_tree_safe_a32
    cmp r0, #0
    beq .err1
.nx_tgt:
    add r7, r7, #1
    cmp r7, r4
    bcc .sw_lp
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr

.check_done:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
.err1:
    mov r0, #1
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc bi_mklink_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv
;; @out r0 = 0 on success, 1 on failure
;; @err r0 = 1 on syntax or link creation failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements internal `MKLINK [[/D] | [/H] | [/J]] Link Target`.
@proc bi_mklink_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0 ; argc
    mov r5, r1 ; argv
    mov r6, #0 ; mode (0=file, 1=/D, 2=/H, 3=/J)
    mov r7, #1 ; idx

.sw_lp:
    cmp r7, r4
    bcs .syn_err
    lsl r0, r7, #2
    ldr r0, [r5, r0]
    ldrb r1, [r0, #0]
    cmp r1, #0x2F
    bne .got_args
    ldrb r0, [r0, #1]
    bl ascii_upper_a32
    cmp r0, #0x44 ; 'D'
    beq .m_d
    cmp r0, #0x48 ; 'H'
    beq .m_h
    cmp r0, #0x4A ; 'J'
    beq .m_j
    b .syn_err
.m_d:
    mov r6, #1
    add r7, r7, #1
    b .sw_lp
.m_h:
    mov r6, #2
    add r7, r7, #1
    b .sw_lp
.m_j:
    mov r6, #3
    add r7, r7, #1
    b .sw_lp

.got_args:
    sub r0, r4, r7
    cmp r0, #2
    bcc .syn_err
    lsl r0, r7, #2
    ldr r0, [r5, r0]
    bl norm_path_a32
    mov r8, r0 ; normalized link
    add r7, r7, #1
    lsl r0, r7, #2
    ldr r0, [r5, r0]
    bl norm_path_a32
    mov r7, r0 ; normalized target

    mov r0, r8
    bl to_u16_longpath_a32
    mov r4, r0 ; u16 link
    mov r0, r7
    bl to_u16_longpath_a32
    mov r2, r0 ; u16 target
    mov r1, r4
    mov r0, r6
    bl hal_mklink
    cmp r0, #0
    bne .err_mk
.ok_msg:
    mov_imm32 r0, STR_OFF_LINK_CREATED
    bl str_ptr_a32
    bl out_str_a32
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.syn_err:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
.err_mk:
    bl print_err_code_a32
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc bi_del_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv
;; @out r0 = 0 on success, 1 on syntax error
;; @err r0 = 1 on missing argument
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements DEL / ERASE (`/F`, `/Q`, `/S`, wildcards, and single files).
@proc bi_del_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #340 ; FIND_INFO_A32 at sp+0
    mov r4, r0 ; argc
    mov r5, r1 ; argv
    mov r6, #0 ; /F force read-only
    mov r7, #1 ; idx
    mov r9, #0 ; target count

.arg_lp:
    cmp r7, r4
    bcs .fin_del
    lsl r0, r7, #2
    ldr r8, [r5, r0]
    ldrb r0, [r8, #0]
    cmp r0, #0x2F
    bne .del_pat
    ldrb r0, [r8, #1]
    bl ascii_upper_a32
    cmp r0, #0x46 ; '/F'
    bne .nx_arg
    mov r6, #1
    b .nx_arg

.del_pat:
    add r9, r9, #1
    mov r0, r8
    bl norm_path_a32
    mov r8, r0

    ; Extract parent directory prefix of r8
    mov r0, r8
    bl str_len_a32
    mov r2, r0
.sl_bk:
    cmp r2, #0
    beq .try_direct
    sub r2, r2, #1
    ldrb r0, [r8, r2]
    cmp r0, #0x5C
    bne .sl_bk
    ; r8[0..r2+1] is parent prefix!
    push {r2, r3}
    mov r0, r8
    bl to_u16_longpath_a32
    add r1, sp, #8
    bl hal_find_first
    pop {r2, r3}
    cmn r0, #1
    beq .try_direct
    b .enum_matches

.try_direct:
    mov r0, r8
    bl to_u16_longpath_a32
    cmp r6, #0
    beq .dd_go
    push {r0, r1}
    mov r1, #FILE_ATTRIBUTE_NORMAL
    bl hal_set_file_attr
    pop {r0, r1}
.dd_go:
    bl hal_delete_file
    b .nx_arg

.enum_matches:
    str r2, [sp, #328] ; slash index
    str r0, [sp, #332] ; hFind
.em_lp:
    ldr r0, [sp, #FIND_INFO_A32.dwAttr]
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .em_nx
    ldr r2, [sp, #328]
    add r0, r2, #300
    bl arena_alloc_a32
    cmp r0, #0
    beq .em_nx
    mov r3, r0
    ldr r2, [sp, #328]
    add r2, r2, #1
    mov r1, r8
    push {r3, r4}
    bl mem_copy_a32
    add r1, sp, #8
    add r1, r1, #FIND_INFO_A32.szName
    bl str_copy_a32
    pop {r3, r4}
    mov r0, r3
    bl to_u16_longpath_a32
    cmp r6, #0
    beq .em_del
    push {r0, r1}
    mov r1, #FILE_ATTRIBUTE_NORMAL
    bl hal_set_file_attr
    pop {r0, r1}
.em_del:
    bl hal_delete_file
.em_nx:
    ldr r0, [sp, #332]
    mov r1, sp
    bl hal_find_next
    cmp r0, #0
    bne .em_lp
    ldr r0, [sp, #332]
    bl hal_find_close

.nx_arg:
    add r7, r7, #1
    b .arg_lp

.fin_del:
    cmp r9, #0
    beq .syn_e
    mov r0, #0
    add sp, sp, #340
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
.syn_e:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
    mov r0, #1
    add sp, sp, #340
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc copy_append_stream_a32
;; @abi arm_ce
;; @in r0 = WTF-8 source file path, r1 = destination open handle
;; @out r0 = 1 on success, 0 on failure
;; @err r0 = 0 on failure
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Streams source file (or interactive `CON`) bytes into an open destination handle.
@proc copy_append_stream_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0 ; src path
    mov r5, r1 ; dst handle

    mov r0, r4
    bl check_device_a32
    cmp r0, #DEV_NUL
    beq .ok1
    cmp r0, #DEV_CON
    beq .from_con

    mov r0, r4
    bl norm_path_a32
    bl to_u16_longpath_a32
    mov r1, #0
    bl hal_open_file
    cmn r0, #1
    beq .fail0
    mov r6, r0 ; hSrc

    mov_imm32 r0, 4096
    bl arena_alloc_a32
    cmp r0, #0
    beq .close_fail
    mov r7, r0 ; chunk buf

.rd_lp:
    mov r0, r6
    mov r1, r7
    mov_imm32 r2, 4096
    bl hal_read_handle
    cmp r0, #0
    beq .close_ok
    mov r2, r0
    mov r0, r5
    mov r1, r7
    bl hal_write_handle
    b .rd_lp

.close_ok:
    mov r0, r6
    bl hal_close_handle
    b .ok1
.close_fail:
    mov r0, r6
    bl hal_close_handle
    b .fail0

.from_con:
    mov_imm32 r0, 1024
    bl arena_alloc_a32
    cmp r0, #0
    beq .fail0
    mov r7, r0
.con_lp:
    mov r0, r7
    mov r1, #1020
    bl read_stdin_line_a32
    cmp r0, #0
    beq .ok1
    ldrb r0, [r7, #0]
    cmp r0, #0x1A ; Ctrl+Z
    beq .ok1
    mov r0, r7
    bl str_len_a32
    mov r2, r0
    mov r0, r5
    mov r1, r7
    bl hal_write_handle
    mov r0, #STR_OFF_CRLF
    bl str_ptr_a32
    mov r1, r0
    mov r0, r5
    mov r2, #2
    bl hal_write_handle
    b .con_lp

.ok1:
    mov r0, #1
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
.fail0:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc bi_copy_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv
;; @out r0 = 0 on success, 1 on failure
;; @err r0 = 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements COPY (`/A`, `/B`, `/Y`, `/-Y`, `/V`, `/Z`, `a + b + c dest` concatenation, and `COPY CON file`).
@proc bi_copy_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #64 ; clean non-switch tokens at sp+0 (up to 16 ptrs)
    mov r4, r0 ; argc
    mov r5, r1 ; argv
    mov r6, #0 ; non-switch count
    mov r7, #1

.flt_lp:
    cmp r7, r4
    bcs .flt_done
    lsl r0, r7, #2
    ldr r0, [r5, r0]
    ldrb r1, [r0, #0]
    cmp r1, #0x2F ; skip `/Y`, `/B`, `/A`, `/V`, `/Z`, etc.
    beq .flt_nx
    cmp r6, #15
    bcs .flt_nx
    lsl r1, r6, #2
    str r0, [sp, r1]
    add r6, r6, #1
.flt_nx:
    add r7, r7, #1
    b .flt_lp

.flt_done:
    cmp r6, #2
    bcc .syn_err
    ; Destination is ALWAYS the last token sp[(r6-1)*4]
    sub r0, r6, #1
    lsl r0, r0, #2
    ldr r8, [sp, r0] ; raw dst

    ; Check if dst is an existing directory -> append leaf of first source!
    mov r0, r8
    bl check_device_a32
    cmp r0, #DEV_NUL
    beq .ok_copied
    mov r0, r8
    bl norm_path_a32
    mov r8, r0
    bl to_u16_longpath_a32
    bl hal_get_file_attr
    cmn r0, #1
    beq .open_dst
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .open_dst

    ; Append leaf of sp[0] onto directory r8
    ldr r9, [sp, #0]
    mov r0, r9
    bl str_len_a32
    mov r2, r0
.leaf_sc:
    cmp r2, #0
    beq .got_leaf
    sub r1, r2, #1
    ldrb r0, [r9, r1]
    cmp r0, #0x5C
    beq .got_leaf
    cmp r0, #0x2F
    beq .got_leaf
    cmp r0, #0x3A
    beq .got_leaf
    mov r2, r1
    b .leaf_sc
.got_leaf:
    add r9, r9, r2
    mov r0, r8
    bl str_len_a32
    add r0, r0, #280
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r10, r0
    mov r1, r8
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .app_lf
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.app_lf:
    mov r1, r9
    bl str_copy_a32
    mov r8, r10

.open_dst:
    mov r0, r8
    bl to_u16_longpath_a32
    mov r1, #1 ; write_trunc
    bl hal_open_file
    cmn r0, #1
    beq .err_fnf
    mov r9, r0 ; hDst

    ; Stream all source tokens 0 .. r6-2 (skipping standalone `+` tokens and splitting `a+b` tokens!)
    sub r10, r6, #1 ; src token limit
    mov r7, #0
.src_lp:
    cmp r7, r10
    bcs .close_dst_ok
    lsl r0, r7, #2
    ldr r4, [sp, r0]
    ; Skip leading/standalone `+`
.plus_sub:
    ldrb r0, [r4, #0]
    cmp r0, #0x2B ; '+'
    bne .chk_end_sub
    add r4, r4, #1
    b .plus_sub
.chk_end_sub:
    cmp r0, #0
    beq .nx_src_tok
    ; Find any embedded `+` inside token `a+b`
    mov r5, #0
.emb_p:
    ldrb r0, [r4, r5]
    cmp r0, #0
    beq .one_part
    cmp r0, #0x2B
    beq .split_part
    add r5, r5, #1
    b .emb_p

.split_part:
    add r0, r5, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .close_dst_err
    mov r6, r0
    mov r1, r4
    mov r2, r5
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r6
    mov r1, r9
    bl copy_append_stream_a32
    cmp r0, #0
    beq .close_dst_err
    add r4, r4, r5
    add r4, r4, #1
    b .plus_sub

.one_part:
    mov r0, r4
    mov r1, r9
    bl copy_append_stream_a32
    cmp r0, #0
    beq .close_dst_err
.nx_src_tok:
    add r7, r7, #1
    b .src_lp

.close_dst_ok:
    mov r0, r9
    bl hal_close_handle
.ok_copied:
    mov_imm32 r0, STR_OFF_COPIED_1
    bl str_ptr_a32
    bl out_str_a32
    mov r0, #0
    add sp, sp, #64
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.close_dst_err:
    mov r0, r9
    bl hal_close_handle
.err_fnf:
    mov r0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a32
    b .err1
.syn_err:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
.err1:
    mov r0, #1
    add sp, sp, #64
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc bi_move_ren_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv, r2 = is_ren (1 = REN, 0 = MOVE)
;; @out r0 = 0 on success, 1 on failure
;; @err r0 = 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements MOVE (`/Y`, `/-Y`, directory target resolution) and REN / RENAME.
@proc bi_move_ren_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0 ; argc
    mov r5, r1 ; argv
    mov r6, r2 ; is_ren
    mov r7, #1
    mov r8, #0 ; src
    mov r3, #0 ; dst

.sc:
    cmp r7, r4
    bcs .got_sr
    lsl r0, r7, #2
    ldr r0, [r5, r0]
    ldrb r1, [r0, #0]
    cmp r1, #0x2F
    beq .nx
    cmp r8, #0
    bne .set_d
    mov r8, r0
    b .nx
.set_d:
    mov r3, r0
.nx:
    add r7, r7, #1
    b .sc

.got_sr:
    cmp r8, #0
    beq .syn_e
    cmp r3, #0
    beq .syn_e
    mov r5, r3 ; raw dst

    mov r0, r8
    bl norm_path_a32
    mov r8, r0 ; norm src

    cmp r6, #0
    beq .move_dst
    ; For `REN dir\old.txt new.txt`, combine parent directory of r8 with bare leaf `r5`!
    mov r0, r8
    bl str_len_a32
    mov r2, r0
.ren_sl:
    cmp r2, #0
    beq .move_dst
    sub r2, r2, #1
    ldrb r0, [r8, r2]
    cmp r0, #0x5C
    bne .ren_sl
    add r0, r2, #280
    push {r2, r3}
    bl arena_alloc_a32
    mov r7, r0
    pop {r2, r3}
    cmp r7, #0
    beq .err1
    add r2, r2, #1
    mov r0, r7
    mov r1, r8
    bl mem_copy_a32
    mov r1, r5
    bl str_copy_a32
    mov r5, r7
    b .do_mv

.move_dst:
    mov r0, r5
    bl norm_path_a32
    mov r5, r0
    ; Check if r5 is an existing directory -> append leaf of r8!
    bl to_u16_longpath_a32
    bl hal_get_file_attr
    cmn r0, #1
    beq .do_mv
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .do_mv
    mov r0, r8
    bl str_len_a32
    mov r2, r0
.mv_lf:
    cmp r2, #0
    beq .mv_join
    sub r1, r2, #1
    ldrb r0, [r8, r1]
    cmp r0, #0x5C
    beq .mv_join
    mov r2, r1
    b .mv_lf
.mv_join:
    add r6, r8, r2
    mov r0, r5
    bl str_len_a32
    add r0, r0, #280
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r7, r0
    mov r1, r5
    bl str_copy_a32
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
    mov r1, r6
    bl str_copy_a32
    mov r5, r7

.do_mv:
    mov r0, r8
    bl to_u16_longpath_a32
    mov r6, r0
    mov r0, r5
    bl to_u16_longpath_a32
    mov r1, r0
    mov r0, r6
    bl hal_move_file
    cmp r0, #0
    beq .err_f
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.err_f:
    mov r0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a32
    b .err1
.syn_e:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
.err1:
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc bi_type_truename_vol_a32
;; @abi arm_ce
;; @in r0 = cmd_id, r1 = argc, r2 = argv
;; @out r0 = 0 on success, 1 on failure
;; @err r0 = 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements TYPE (including UTF-8 BOM & UTF-16LE BOM awareness), TRUENAME, and VOL.
@proc bi_type_truename_vol_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0 ; id
    mov r5, r1 ; argc
    mov r6, r2 ; argv

    cmp r4, #CMD_VOL
    beq .do_vol
    cmp r4, #CMD_TRUENAME
    beq .do_true

    ; CMD_TYPE: require >= 1 file argument
    cmp r5, #2
    bcc .syn_e
    mov_imm32 r0, 4096
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r8, r0 ; read buf

    mov r7, #1
.ty_lp:
    cmp r7, r5
    bcs .ok0
    lsl r0, r7, #2
    ldr r4, [r6, r0]
    cmp r5, #3
    bcc .no_hdr
    bl out_crlf_a32
    mov r0, r4
    bl err_str_a32
    mov r0, #STR_OFF_CRLF
    bl str_ptr_a32
    bl err_str_a32
    bl err_str_a32
.no_hdr:
    mov r0, r4
    bl norm_path_a32
    bl to_u16_longpath_a32
    mov r1, #0
    bl hal_open_file
    cmn r0, #1
    beq .err_fnf
    mov r4, r0 ; hFile

    ; Read first chunk and strip UTF-8 BOM or transcode UTF-16LE BOM if present!
    mov r0, r4
    mov r1, r8
    mov_imm32 r2, 4096
    bl hal_read_handle
    cmp r0, #0
    beq .ty_cl
    mov r2, r0
    mov r1, r8
    cmp r2, #2
    bcc .ty_w1
    ldrb r0, [r8, #0]
    ldrb r3, [r8, #1]
    cmp r0, #0xFF
    bne .chk_u8b
    cmp r3, #0xFE
    bne .chk_u8b
    ; UTF-16LE buffer! NUL-terminate WCHARs and transcode
    mov r0, #0
    add r3, r8, r2
    strh r0, [r3, #0]
    add r0, r8, #2
    sub r1, r2, #2
    lsr r1, r1, #1
    mov_imm32 r3, 8192
    push {r1, r2}
    mov r0, r3
    bl arena_alloc_a32
    mov r3, r0
    pop {r1, r2}
    cmp r3, #0
    beq .ty_cl
    add r0, r8, #2
    mov r2, r3
    push {r3, r4}
    bl u16_to_wtf8_buf_a32
    pop {r3, r4}
    mov r0, r3
    bl out_str_a32
    b .ty_cl

.chk_u8b:
    cmp r2, #3
    bcc .ty_w1
    cmp r0, #0xEF
    bne .ty_w1
    cmp r3, #0xBB
    bne .ty_w1
    ldrb r0, [r8, #2]
    cmp r0, #0xBF
    bne .ty_w1
    add r1, r8, #3
    sub r2, r2, #3
.ty_w1:
.ty_chunks:
    bl hal_get_ctx
    add r0, r0, #ARM32_CTX.fdTable
    ldr r0, [r0, #4]
    bl hal_write_handle
    mov r0, r4
    mov r1, r8
    mov_imm32 r2, 4096
    bl hal_read_handle
    cmp r0, #0
    beq .ty_cl
    mov r2, r0
    mov r1, r8
    b .ty_chunks

.ty_cl:
    mov r0, r4
    bl hal_close_handle
    add r7, r7, #1
    b .ty_lp

.do_true:
    cmp r5, #2
    bcc .cur_tr
    ldr r0, [r6, #4]
    bl norm_path_a32
    bl out_str_a32
    bl out_crlf_a32
    b .ok0
.cur_tr:
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r0, r0, r1
    bl out_str_a32
    bl out_crlf_a32
    b .ok0

.do_vol:
    mov_imm32 r0, STR_OFF_VOL_HDR
    bl str_ptr_a32
    bl out_str_a32
.ok0:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.err_fnf:
    mov r0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a32
    b .err1
.syn_e:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
.err1:
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc dir_list_one_a32
;; @abi arm_ce
;; @in r0 = normalized directory path, r1 = wildcard filter ("*"), r2 = flags (bit0=/B, bit1=/S)
;; @out r0 = match count
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Enumerates a directory for `DIR`, honoring Win10/11 per-directory case sensitivity, `<JUNCTION>`/`<SYMLINK>`, 64-bit sizes, and `/S` recursion.
@proc dir_list_one_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #384 ; FIND_INFO_A32 (320B) at sp+0, scratch at sp+320..383
    mov r4, r0 ; dir path
    mov r5, r1 ; filter pattern
    mov r6, r2 ; flags (/B=1, /S=2)
    mov r7, #0 ; file/entry count
    mov r8, #0 ; total bytes lo
    mov r9, #0 ; total bytes hi

    ; Check Win10/11 per-directory case sensitivity!
    mov r0, r4
    bl to_u16_longpath_a32
    bl hal_check_case_sensitive_dir
    str r0, [sp, #320] ; case_sensitive flag

    tst r6, #1 ; /B bare mode omits header
    bne .open_enum
    adr_sym r0, szDirOfHdr
    bl out_str_a32
    mov r0, r4
    bl out_str_a32
    bl out_crlf_a32
    bl out_crlf_a32

.open_enum:
    ; Enumerate `r4\*` and filter via matches_glob_a32
    mov r0, r4
    bl str_len_a32
    add r0, r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .fin_dir
    mov r10, r0
    mov r1, r4
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .st_star
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.st_star:
    mov r2, #0x2A
    strb r2, [r0, #0]
    mov r2, #0
    strb r2, [r0, #1]

    mov r0, r10
    bl to_u16_longpath_a32
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .after_enum
    mov r10, r0 ; hFind

.d_lp:
    ; Check pattern match against szName (and skip `.`/`..` in `/B`)
    add r0, sp, #FIND_INFO_A32.szName
    tst r6, #1
    beq .chk_glob
    ldrb r1, [r0, #0]
    cmp r1, #0x2E
    bne .chk_glob
    ldrb r1, [r0, #1]
    cmp r1, #0
    beq .d_nx
    cmp r1, #0x2E
    bne .chk_glob
    ldrb r1, [r0, #2]
    cmp r1, #0
    beq .d_nx

.chk_glob:
    mov r0, r5
    add r1, sp, #FIND_INFO_A32.szName
    ldr r2, [sp, #320]
    bl matches_glob_a32
    cmp r0, #0
    beq .d_nx

    add r7, r7, #1
    tst r6, #1
    bne .bare_line

    ; Standard line: YYYY-MM-DD  HH:MM  (<DIR> | <JUNCTION> | <SYMLINKD> | <SYMLINK> | 18-col size)  name
    ldr r0, [sp, #FIND_INFO_A32.ftWriteLo]
    ldr r1, [sp, #FIND_INFO_A32.ftWriteHi]
    add r2, sp, #328
    bl fmt_filetime_a32
    add r0, sp, #328
    bl out_str_a32

    ldr r0, [sp, #FIND_INFO_A32.dwAttr]
    mov_imm32 r1, FILE_ATTRIBUTE_REPARSE_POINT
    tst r0, r1
    bne .reparse_tag
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .dir_tag

    ; Regular file: accumulate 64-bit size and right-align size field
    ldr r0, [sp, #FIND_INFO_A32.uSizeLo]
    ldr r1, [sp, #FIND_INFO_A32.uSizeHi]
    adds r8, r8, r0
    adc r9, r9, r1
    add r2, sp, #328
    mov r3, #1
    bl fmt_u64_commas_a32
    ; Pad up to 18 columns
    mov r2, #18
    subs r2, r2, r0
    bls .prt_sz
.pad_sp:
    push {r2, r3}
    adr_sym r0, szOneSpace
    bl out_str_a32
    pop {r2, r3}
    subs r2, r2, #1
    bne .pad_sp
.prt_sz:
    add r0, sp, #328
    bl out_str_a32
    adr_sym r0, szOneSpace
    bl out_str_a32
    b .prt_name

.reparse_tag:
    ldr r1, [sp, #FIND_INFO_A32.dwReparseTag]
    mov_imm32 r2, 0xA0000003 ; IO_REPARSE_TAG_MOUNT_POINT
    cmp r1, r2
    beq .junc_t
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    bne .symd_t
    mov_imm32 r0, STR_OFF_SYMLINK_TAG
    bl str_ptr_a32
    bl out_str_a32
    b .prt_name
.junc_t:
    mov_imm32 r0, STR_OFF_JUNC_TAG
    bl str_ptr_a32
    bl out_str_a32
    b .prt_name
.symd_t:
    mov_imm32 r0, STR_OFF_SYMLINKD_TAG
    bl str_ptr_a32
    bl out_str_a32
    b .prt_name
.dir_tag:
    mov_imm32 r0, STR_OFF_DIR_TAG
    bl str_ptr_a32
    bl out_str_a32

.prt_name:
    add r0, sp, #FIND_INFO_A32.szName
    bl out_str_a32
    bl out_crlf_a32
    b .d_nx

.bare_line:
    tst r6, #2 ; `/B /S` prints full path!
    beq .bare_leaf
    mov r0, r4
    bl out_str_a32
    mov r0, r4
    bl str_len_a32
    sub r0, r0, #1
    ldrb r0, [r4, r0]
    cmp r0, #0x5C
    beq .bare_leaf
    adr_sym r0, szBackslash
    bl out_str_a32
.bare_leaf:
    add r0, sp, #FIND_INFO_A32.szName
    bl out_str_a32
    bl out_crlf_a32

.d_nx:
    mov r0, r10
    mov r1, sp
    bl hal_find_next
    cmp r0, #0
    bne .d_lp
    mov r0, r10
    bl hal_find_close

.after_enum:
    tst r6, #1
    bne .chk_rec
    ; Print summary footer: total bytes + free disk bytes!
    adr_sym r0, szDirPad8
    bl out_str_a32
    mov r0, r8
    mov r1, r9
    add r2, sp, #328
    mov r3, #1
    bl fmt_u64_commas_a32
    add r0, sp, #328
    bl out_str_a32
    adr_sym r0, szBytesSummary
    bl out_str_a32

    mov r0, r4
    bl to_u16_longpath_a32
    add r1, sp, #368
    bl hal_get_disk_free
    cmp r0, #0
    beq .chk_rec
    adr_sym r0, szDirPad8
    bl out_str_a32
    ldr r0, [sp, #368]
    ldr r1, [sp, #372]
    add r2, sp, #328
    mov r3, #1
    bl fmt_u64_commas_a32
    add r0, sp, #328
    bl out_str_a32
    adr_sym r0, szBytesFree
    bl out_str_a32

.chk_rec:
    tst r6, #2 ; `/S` recursive subdirectory walk
    beq .fin_dir
    mov r0, r4
    bl str_len_a32
    add r0, r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .fin_dir
    mov r10, r0
    mov r1, r4
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .rs_star
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.rs_star:
    mov r2, #0x2A
    strb r2, [r0, #0]
    mov r2, #0
    strb r2, [r0, #1]
    mov r0, r10
    bl to_u16_longpath_a32
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .fin_dir
    mov r10, r0

.rs_lp:
    ldr r0, [sp, #FIND_INFO_A32.dwAttr]
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .rs_nx
    mov_imm32 r1, FILE_ATTRIBUTE_REPARSE_POINT
    tst r0, r1
    bne .rs_nx ; do not recurse into junction loops
    add r0, sp, #FIND_INFO_A32.szName
    ldrb r1, [r0, #0]
    cmp r1, #0x2E
    bne .rs_go
    ldrb r1, [r0, #1]
    cmp r1, #0
    beq .rs_nx
    cmp r1, #0x2E
    bne .rs_go
    ldrb r1, [r0, #2]
    cmp r1, #0
    beq .rs_nx
.rs_go:
    mov r0, r4
    bl str_len_a32
    add r0, r0, #300
    bl arena_alloc_a32
    cmp r0, #0
    beq .rs_nx
    mov r8, r0
    mov r1, r4
    bl str_copy_a32
    sub r1, r0, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    beq .rs_leaf
    mov r2, #0x5C
    strb r2, [r0, #0]
    add r0, r0, #1
.rs_leaf:
    add r1, sp, #FIND_INFO_A32.szName
    bl str_copy_a32
    mov r0, r8
    mov r1, r5
    mov r2, r6
    bl dir_list_one_a32
    add r7, r7, r0
.rs_nx:
    mov r0, r10
    mov r1, sp
    bl hal_find_next
    cmp r0, #0
    bne .rs_lp
    mov r0, r10
    bl hal_find_close

.fin_dir:
    mov r0, r7
    add sp, sp, #384
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc bi_dir_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv
;; @out r0 = 0 on match, 1 if File Not Found
;; @err r0 = 1 when 0 entries matched
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements DIR (`/B`, `/S`, `/W`, `/N`, `/X`, `/4`, `/A`, `/O`, wildcards, and directories).
@proc bi_dir_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0 ; argc
    mov r5, r1 ; argv
    mov r6, #0 ; flags (bit0=/B, bit1=/S)
    mov r7, #0 ; target path arg (0 = CWD)
    mov r8, #1

.arg_lp:
    cmp r8, r4
    bcs .run_dir
    lsl r0, r8, #2
    ldr r2, [r5, r0]
    ldrb r0, [r2, #0]
    cmp r0, #0x2F
    bne .set_tgt
    ldrb r0, [r2, #1]
    bl ascii_upper_a32
    cmp r0, #0x42 ; '/B'
    bne .chk_s
    orr r6, r6, #1
    b .nx_a
.chk_s:
    cmp r0, #0x53 ; '/S'
    bne .nx_a
    orr r6, r6, #2
    b .nx_a
.set_tgt:
    mov r7, r2
.nx_a:
    add r8, r8, #1
    b .arg_lp

.run_dir:
    cmp r7, #0
    bne .norm_tgt
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r0, r0, r1
    adr_sym r1, szStarPat
    mov r2, r6
    bl dir_list_one_a32
    b .chk_cnt

.norm_tgt:
    mov r0, r7
    bl norm_path_a32
    mov r7, r0
    ; Check if r7 contains `*` or `?` -> split into dir + pattern!
    mov r2, #0
    mov r3, #0 ; has_wild
.w_sc:
    ldrb r0, [r7, r2]
    cmp r0, #0
    beq .w_done
    cmp r0, #0x2A
    beq .has_w
    cmp r0, #0x3F
    bne .w_nx
.has_w:
    mov r3, #1
.w_nx:
    add r2, r2, #1
    b .w_sc

.w_done:
    cmp r3, #0
    bne .split_dir_pat
    ; Check if r7 is an existing directory
    mov r0, r7
    bl str_len_a32
    cmp r0, #3
    bls .is_dir_whole
    mov r0, r7
    bl to_u16_longpath_a32
    bl hal_get_file_attr
    cmn r0, #1
    beq .split_dir_pat
    tst r0, #FILE_ATTRIBUTE_DIRECTORY
    beq .split_dir_pat
.is_dir_whole:
    mov r0, r7
    adr_sym r1, szStarPat
    mov r2, r6
    bl dir_list_one_a32
    b .chk_cnt

.split_dir_pat:
    mov r0, r7
    bl str_len_a32
    mov r2, r0
.sp_bk:
    cmp r2, #0
    beq .cur_pat
    sub r2, r2, #1
    ldrb r0, [r7, r2]
    cmp r0, #0x5C
    bne .sp_bk
    add r5, r7, r2
    add r5, r5, #1 ; leaf pattern
    ; If r2 == 0 (`\foo`) or r2 == 2 (`C:\foo`), keep trailing ``!
    cmp r2, #0
    beq .keep_sl
    cmp r2, #2
    bne .cut_sl
    ldrb r0, [r7, #1]
    cmp r0, #0x3A
    bne .cut_sl
.keep_sl:
    add r2, r2, #1
.cut_sl:
    add r0, r2, #4
    push {r2, r3}
    bl arena_alloc_a32
    mov r4, r0
    pop {r2, r3}
    cmp r4, #0
    beq .err_fnf
    mov r0, r4
    mov r1, r7
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r4
    mov r1, r5
    mov r2, r6
    bl dir_list_one_a32
    b .chk_cnt

.cur_pat:
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r0, r0, r1
    mov r1, r7
    mov r2, r6
    bl dir_list_one_a32

.chk_cnt:
    cmp r0, #0
    beq .err_fnf
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
.err_fnf:
    mov r0, #ERR_FILE_NOT_FOUND
    bl print_err_code_a32
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc
