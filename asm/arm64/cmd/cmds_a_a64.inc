;; @file cmds_a_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 Internal Command Lookup & Built-Ins Part A (PROMPT, ECHO, SET, ASSOC, FTYPE, DOSKEY, SHIFT, EXIT, etc.).

@cpu arm64
@format inc

;; @proc print_prompt_a64
;; @abi win_arm64
;; @in none
;; @out Renders %PROMPT% ($P, $G, $L, $B, $Q, $S, $E, $H, $_, $D, $T, $N, $$) to stdout
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Formats and prints the active interactive or batch-echo prompt string.
@proc print_prompt_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    adr_sym x0, szVarPrompt_a64
    bl env_get_a64
    cbnz x0, .got_p
    mov w0, #STR_OFF_DEF_PROMPT
    bl str_ptr_a64
.got_p:
    mov x19, x0 ; template
    mov_imm32 x0, 2048
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0 ; out base
    mov x21, x0 ; out cursor

.lp:
    ldrb w0, [x19, #0]
    cbz w0, .emit
    cmp w0, #0x24
    b.eq .esc
    strb w0, [x21, #0]
    add x19, x19, #1
    add x21, x21, #1
    b .lp

.esc:
    add x19, x19, #1
    ldrb w0, [x19, #0]
    cbz w0, .emit
    add x19, x19, #1
    bl ascii_upper_a64
    cmp w0, #0x50
    b.eq .p_cwd
    cmp w0, #0x47
    b.eq .p_gt
    cmp w0, #0x4C
    b.eq .p_lt
    cmp w0, #0x42
    b.eq .p_bar
    cmp w0, #0x51
    b.eq .p_eq
    cmp w0, #0x53
    b.eq .p_sp
    cmp w0, #0x45
    b.eq .p_esc
    cmp w0, #0x48
    b.eq .p_bs
    cmp w0, #0x5F
    b.eq .p_crlf
    cmp w0, #0x24
    b.eq .p_dlr
    cmp w0, #0x4E
    b.eq .p_drv
    cmp w0, #0x44
    b.eq .p_date
    cmp w0, #0x54
    b.eq .p_time
    b .lp

.p_cwd:
    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.szCwd
    mov x0, x21
    bl str_copy_a64
    mov x21, x0
    b .lp
.p_drv:
    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.szCwd
    ldrb w0, [x1, #0]
    strb w0, [x21, #0]
    add x21, x21, #1
    b .lp
.p_gt:
    mov w0, #0x3E
    b .st1
.p_lt:
    mov w0, #0x3C
    b .st1
.p_bar:
    mov w0, #0x7C
    b .st1
.p_eq:
    mov w0, #0x3D
    b .st1
.p_sp:
    mov w0, #0x20
    b .st1
.p_esc:
    mov w0, #0x1B
    b .st1
.p_dlr:
    mov w0, #0x24
.st1:
    strb w0, [x21, #0]
    add x21, x21, #1
    b .lp
.p_bs:
    cmp x21, x20
    b.ls .lp
    sub x21, x21, #1
    b .lp
.p_crlf:
    mov w0, #0x0D
    strb w0, [x21, #0]
    mov w0, #0x0A
    strb w0, [x21, #1]
    add x21, x21, #2
    b .lp
.p_date:
    mov w0, #0
    bl fmt_dyn_date_time_a64
    cbz x0, .lp
    mov x1, x0
    mov x0, x21
    bl str_copy_a64
    mov x21, x0
    b .lp
.p_time:
    mov w0, #1
    bl fmt_dyn_date_time_a64
    cbz x0, .lp
    mov x1, x0
    mov x0, x21
    bl str_copy_a64
    mov x21, x0
    b .lp

.emit:
    strb wzr, [x21, #0]
    mov x0, x20
    bl out_str_a64
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc lookup_builtin_id_a64
;; @abi win_arm64
;; @in x0 = unquoted command name string
;; @out w0 = CMD_* id (or CMD_NONE = 0 if external)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Matches internal command names and aliases against g_a64_cmd_tbl.
@proc lookup_builtin_id_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    adr_sym x20, g_a64_cmd_tbl

.tbl_lp:
    ldrb w0, [x20, #0]
    cbz w0, .not_found
    add x1, x20, #1
    mov x0, x19
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .matched

    add x20, x20, #1
.sk_nm:
    ldrb w0, [x20, #0]
    add x20, x20, #1
    cbnz w0, .sk_nm
    b .tbl_lp

.matched:
    ldrb w0, [x20, #0]
    b .done
.not_found:
    mov w0, #CMD_NONE
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc extract_raw_tail_a64
;; @abi win_arm64
;; @in x0 = raw command line (pStr1)
;; @out x0 = trimmed raw argument tail after the command name (with any redirections stripped)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Extracts the unsplit argument tail after word0 while stripping `<`/`>` redirections.
@proc extract_raw_tail_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    cbz x0, .ret_empty
    bl skip_ws_a64
    mov x19, x0

.skip_lead_red:
    ldrb w0, [x19, #0]
    cmp w0, #0x3C
    b.eq .sk_red_1
    cmp w0, #0x3E
    b.eq .sk_red_1
    cmp w0, #0x30
    b.lo .past_lead
    cmp w0, #0x39
    b.hi .past_lead
    ldrb w1, [x19, #1]
    cmp w1, #0x3C
    b.eq .sk_red_2
    cmp w1, #0x3E
    b.ne .past_lead
.sk_red_2:
    add x19, x19, #1
.sk_red_1:
    add x19, x19, #1
    ldrb w0, [x19, #0]
    cmp w0, #0x3E
    b.ne .sk_red_t
    add x19, x19, #1
.sk_red_t:
    ldrb w0, [x19, #0]
    cmp w0, #0x26
    b.ne .sk_red_tok
    add x19, x19, #2
    mov x0, x19
    bl skip_ws_a64
    mov x19, x0
    b .skip_lead_red
.sk_red_tok:
    mov x0, x19
    bl scan_cmd_token_a64
    mov x0, x1
    bl skip_ws_a64
    mov x19, x0
    b .skip_lead_red

.past_lead:
    mov x0, x19
    bl scan_cmd_token_a64
    cbz x0, .ret_empty
    mov x0, x1
    ldrb w1, [x0, #0]
    cmp w1, #0x20
    b.ne .no_sp1
    add x0, x0, #1
.no_sp1:
    mov x19, x0

    mov x20, x19
    mov w21, #0
.sc_end:
    ldrb w0, [x20, #0]
    cbz w0, .got_end
    cmp w0, #0x22
    b.ne .chk_c
    eor w21, w21, #1
    add x20, x20, #1
    b .sc_end
.chk_c:
    cbnz w21, .adv_e
    cmp w0, #0x3C
    b.eq .got_end
    cmp w0, #0x3E
    b.eq .got_end
    cmp w0, #0x30
    b.lo .adv_e
    cmp w0, #0x39
    b.hi .adv_e
    ldrb w1, [x20, #1]
    cmp w1, #0x3E
    b.eq .chk_prev_sp
    cmp w1, #0x3C
    b.ne .adv_e
.chk_prev_sp:
    cmp x20, x19
    b.eq .got_end
    sub x2, x20, #1
    ldrb w1, [x2, #0]
    cmp w1, #0x20
    b.eq .got_end
.adv_e:
    add x20, x20, #1
    b .sc_end

.got_end:
    sub x22, x20, x19
    ldrb w0, [x20, #0]
    cbz w0, .alloc_tail
    cbz x22, .alloc_tail
    sub x1, x20, #1
    ldrb w0, [x1, #0]
    cmp w0, #0x20
    b.ne .alloc_tail
    sub x22, x22, #1
.alloc_tail:
    add x0, x22, #2
    bl arena_alloc_a64
    cbz x0, .ret_empty
    mov x21, x0
    mov x1, x19
    mov x2, x22
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x21
    mov w1, #0
    bl expand_phase34_a64
    b .done

.ret_empty:
    mov x0, #8
    bl arena_alloc_a64
    cbz x0, .done
    strb wzr, [x0, #0]
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_echo_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*, w1 = argc, x2 = expanded raw argv
;; @out w0 = exit code (0)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements ECHO, ECHO ON, ECHO OFF, ECHO., ECHO(, and arbitrary text output.
@proc bi_echo_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; node
    mov w20, w1 ; argc
    mov x21, x2 ; argv

    ldrb w0, [x19, #AST_NODE_A64.bFlags]
    tbnz w0, #4, .delim_mode ; 0x10

    cmp w20, #2
    b.lo .show_status
    ldr x0, [x21, #8]
    bl skip_ws_a64
    ldrb w1, [x0, #0]
    cbz w1, .show_status

    mov x19, x0
    adr_sym x1, szKwOn_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .set_on
    mov x0, x19
    adr_sym x1, szKwOff_a64
    mov w2, #1
    bl str_cmp_a64
    cbz w0, .set_off

    ldr x0, [x21, #8]
    bl out_str_a64
    bl out_crlf_a64
    b .ok0

.delim_mode:
    cmp w20, #2
    b.lo .just_nl
    ldr x0, [x21, #8]
    bl out_str_a64
.just_nl:
    bl out_crlf_a64
    b .ok0

.set_on:
    bl hal_get_ctx_a64
    mov w1, #1
    strb w1, [x0, #ARM64_CTX.bEchoOn]
    b .ok0

.set_off:
    bl hal_get_ctx_a64
    strb wzr, [x0, #ARM64_CTX.bEchoOn]
    b .ok0

.show_status:
    bl hal_get_ctx_a64
    ldrb w0, [x0, #ARM64_CTX.bEchoOn]
    cbz w0, .prt_off
    mov w0, #STR_OFF_ECHO_ON
    bl str_ptr_a64
    bl out_str_a64
    b .ok0
.prt_off:
    mov w0, #STR_OFF_ECHO_OFF
    bl str_ptr_a64
    bl out_str_a64

.ok0:
    mov w0, #0
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_set_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*
;; @out w0 = exit code (0 on success, 1 on undefined prefix or SET /A error)
;; @err w0 = 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements `SET`, `SET prefix`, `SET VAR=val`, `SET "VAR=val"`, `SET /A expr`, and `SET /P VAR=prompt`.
@proc bi_set_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp

    ldr x0, [x0, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    bl skip_ws_a64
    mov x19, x0

    ldrb w0, [x19, #0]
    cbz w0, .list_all

    cmp w0, #0x2F
    b.ne .chk_quoted_set
    ldrb w0, [x19, #1]
    bl ascii_upper_a64
    cmp w0, #0x41
    b.eq .do_set_a
    cmp w0, #0x50
    b.eq .do_set_p

.chk_quoted_set:
    ldrb w0, [x19, #0]
    cmp w0, #0x22
    b.ne .scan_eq
    add x19, x19, #1
    mov x0, x19
    bl str_len_a64
.qs_bk:
    cbz x0, .scan_eq
    sub x0, x0, #1
    ldrb w1, [x19, x0]
    cmp w1, #0x22
    b.ne .qs_bk
    strb wzr, [x19, x0]

.scan_eq:
    ldrb w0, [x19, #0]
    cmp w0, #0x3D
    b.eq .syn_err
    mov x20, #0
.eq_lp:
    ldrb w0, [x19, x20]
    cbz w0, .prefix_query
    cmp w0, #0x3D
    b.eq .assign_kv
    add x20, x20, #1
    b .eq_lp

.assign_kv:
    add x0, x20, #2
    bl arena_alloc_a64
    cbz x0, .ret_err
    mov x21, x0
    mov x1, x19
    mov x2, x20
    bl mem_copy_a64
    strb wzr, [x0, #0]
    add x1, x19, x20
    add x1, x1, #1
    mov x0, x21
    bl env_set_a64
    mov w0, #0
    b .done

.prefix_query:
    mov x0, x19
    bl str_len_a64
    mov x20, x0
    bl hal_get_ctx_a64
    ldr x21, [x0, #ARM64_CTX.pEnvBlock]
    mov w22, #0
.pf_lp:
    ldrb w0, [x21, #0]
    cbz w0, .pf_done
    cmp w0, #0x3D
    b.eq .pf_nx
    mov x0, x21
    mov x1, x19
    mov x2, x20
    bl str_ncmp_ci_a64
    cbnz w0, .pf_nx
    mov x0, x21
    bl out_str_a64
    bl out_crlf_a64
    add w22, w22, #1
.pf_nx:
    ldrb w0, [x21, #0]
    add x21, x21, #1
    cbnz w0, .pf_nx
    b .pf_lp
.pf_done:
    cbz w22, .env_undef
    mov w0, #0
    b .done

.env_undef:
    mov_imm32 w0, STR_OFF_ENV_UNDEF
    bl str_ptr_a64
    bl err_str_a64
    b .ret_err

.list_all:
    bl hal_get_ctx_a64
    ldr x21, [x0, #ARM64_CTX.pEnvBlock]
.la_lp:
    ldrb w0, [x21, #0]
    cbz w0, .ok0
    cmp w0, #0x3D
    b.eq .la_nx
    mov x0, x21
    bl out_str_a64
    bl out_crlf_a64
.la_nx:
    ldrb w0, [x21, #0]
    add x21, x21, #1
    cbnz w0, .la_nx
    b .la_lp

.do_set_a:
    add x0, x19, #2
    bl eval_set_a_a64
    cbnz w1, .sa_err
    mov w20, w0
    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwBatchDepth]
    cbnz w0, .ok0
    mov w0, w20
    mov x1, sp
    bl fmt_i32_a64
    mov x0, sp
    bl out_str_a64
    mov w0, #0
    b .done

.sa_err:
    mov w0, w1
    bl print_err_code_a64
    b .ret_err

.do_set_p:
    add x0, x19, #2
    bl skip_ws_a64
    bl strip_outer_quotes_a64
    mov x19, x0
    mov x20, #0
.sp_eq:
    ldrb w0, [x19, x20]
    cbz w0, .syn_err
    cmp w0, #0x3D
    b.eq .sp_got
    add x20, x20, #1
    b .sp_eq
.sp_got:
    add x0, x20, #2
    bl arena_alloc_a64
    cbz x0, .ret_err
    mov x21, x0
    mov x1, x19
    mov x2, x20
    bl mem_copy_a64
    strb wzr, [x0, #0]
    add x0, x19, x20
    add x0, x0, #1
    ldrb w1, [x0, #0]
    cbz w1, .sp_rd
    bl out_str_a64
.sp_rd:
    mov_imm32 x0, 1024
    bl arena_alloc_a64
    cbz x0, .ret_err
    mov x22, x0
    mov w1, #1020
    bl read_stdin_line_a64
    cbz w0, .ret_err
    mov x0, x21
    mov x1, x22
    bl env_set_a64
    mov w0, #0
    b .done

.syn_err:
    mov_imm32 w0, ERR_SYNTAX_ERROR
    bl print_err_code_a64
.ret_err:
    mov w0, #1
    b .done
.ok0:
    mov w0, #0
.done:
    add sp, sp, #32
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_ver_a64
;; @abi win_arm64
;; @in none
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints authentic OS & shell version banner (`Microsoft Windows [Version major.minor.build]`) queried via X18 TEB -> PEB.
@proc bi_ver_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp

    bl out_crlf_a64
    bl hal_get_ctx_a64
    mov x19, x0
    mov w0, #STR_OFF_BANNER
    bl str_ptr_a64
    bl out_str_a64

    ldr w0, [x19, #ARM64_CTX.dwOsMajor]
    mov x1, sp
    bl fmt_i32_a64
    mov x0, sp
    bl out_str_a64
    adr_sym x0, szDot_a64
    bl out_str_a64

    ldr w0, [x19, #ARM64_CTX.dwOsMinor]
    mov x1, sp
    bl fmt_i32_a64
    mov x0, sp
    bl out_str_a64
    adr_sym x0, szDot_a64
    bl out_str_a64

    ldr w0, [x19, #ARM64_CTX.dwOsBuild]
    mov x1, sp
    bl fmt_i32_a64
    mov x0, sp
    bl out_str_a64
    adr_sym x0, szVerClose_a64
    bl out_str_a64

    mov w0, #0
    add sp, sp, #32
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_assoc_ftype_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*, w1 = is_ftype (0 = ASSOC, 1 = FTYPE)
;; @out w0 = 0 on success, 1 if queried association not found
;; @err w0 = 1 if missing
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries or updates Software\Classes file associations (`ASSOC` and `FTYPE`) via ADVAPI32 Registry HAL.
@proc bi_assoc_ftype_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov w20, w1 ; is_ftype

    ldr x0, [x0, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    bl skip_ws_a64
    bl strip_outer_quotes_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cbz w0, .ok0

    mov x21, #0
.eq_sc:
    ldrb w0, [x19, x21]
    cbz w0, .query_only
    cmp w0, #0x3D
    b.eq .set_val
    add x21, x21, #1
    b .eq_sc

.set_val:
    add x0, x21, #2
    bl arena_alloc_a64
    cbz x0, .err1
    mov x22, x0 ; stem
    mov x1, x19
    mov x2, x21
    bl mem_copy_a64
    strb wzr, [x0, #0]
    add x23, x19, x21
    add x23, x23, #1 ; value

    mov_imm32 x0, 512
    bl arena_alloc_a64
    cbz x0, .err1
    mov x21, x0
    adr_sym x1, szRegClassesPrefix_a64
    bl str_copy_a64
    mov x1, x22
    bl str_copy_a64
    cbz w20, .w_reg
    adr_sym x1, szRegShellOpenCmd_a64
    bl str_copy_a64
.w_reg:
    mov x0, x21
    bl wtf8_to_u16_a64
    mov x24, x0 ; u16 subkey
    mov x0, x23
    bl wtf8_to_u16_a64
    mov x3, x0
    mov_imm32 x0, 0x80000001 ; HKCU
    sbfx x0, x0, #0, #32
    mov x1, x24
    mov x2, #0
    bl hal_reg_set_str_a64
    cbnz w0, .err1

    mov x0, x22
    bl out_str_a64
    adr_sym x0, szEqStr_a64
    bl out_str_a64
    mov x0, x23
    bl out_str_a64
    bl out_crlf_a64
    b .ok0

.query_only:
    mov_imm32 x0, 512
    bl arena_alloc_a64
    cbz x0, .err1
    mov x21, x0
    adr_sym x1, szRegClassesPrefix_a64
    bl str_copy_a64
    mov x1, x19
    bl str_copy_a64
    cbz w20, .q_reg
    adr_sym x1, szRegShellOpenCmd_a64
    bl str_copy_a64
.q_reg:
    mov x0, x21
    bl wtf8_to_u16_a64
    mov x21, x0
    mov_imm32 x0, 528
    bl arena_alloc_a64
    cbz x0, .err1
    mov x22, x0
    mov_imm32 x0, 0x80000001 ; HKCU
    sbfx x0, x0, #0, #32
    mov x1, x21
    mov x2, #0
    mov x3, x22
    bl hal_reg_query_str_a64
    cbz w0, .got_q
    mov_imm32 x0, 0x80000002 ; HKLM
    sbfx x0, x0, #0, #32
    mov x1, x21
    mov x2, #0
    mov x3, x22
    bl hal_reg_query_str_a64
    cbnz w0, .err1
.got_q:
    mov x0, x19
    bl out_str_a64
    adr_sym x0, szEqStr_a64
    bl out_str_a64
    mov x0, x22
    bl out_str_a64
    bl out_crlf_a64
.ok0:
    mov w0, #0
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

;; @proc bi_color_a64
;; @abi win_arm64
;; @in w0 = argc, x1 = argv
;; @out w0 = 0 on success, 1 if fg == bg
;; @err w0 = 1 when foreground nibble equals background nibble
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements COLOR [attr]: resets default on bare COLOR, rejects fg == bg with exit code 1.
@proc bi_color_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    cmp w0, #2
    b.lo .reset_def
    ldr x19, [x1, #8]
    ldrb w0, [x19, #0]
    cbz w0, .reset_def

    mov w20, #0
    mov x21, #0
.hex_lp:
    ldrb w0, [x19, x21]
    cbz w0, .chk_cnt
    cmp x21, #2
    b.hs .bad_c
    cmp w0, #0x30
    b.lo .bad_c
    cmp w0, #0x39
    b.ls .hd
    bl ascii_upper_a64
    cmp w0, #0x41
    b.lo .bad_c
    cmp w0, #0x46
    b.hi .bad_c
    sub w0, w0, #0x37
    b .ha
.hd:
    sub w0, w0, #0x30
.ha:
    lsl w20, w20, #4
    orr w20, w20, w0
    add x21, x21, #1
    b .hex_lp
.chk_cnt:
    cmp x21, #2
    b.lo .apply_c
    lsr w0, w20, #4
    and w1, w20, #0x0F
    cmp w0, w1
    b.eq .bad_c
.apply_c:
    mov w0, w20
    bl hal_set_console_attr_a64
    mov w0, #0
    b .done

.reset_def:
    bl hal_get_ctx_a64
    ldrh w0, [x0, #ARM64_CTX.wDefaultAttr]
    bl hal_set_console_attr_a64
    mov w0, #0
    b .done
.bad_c:
    mov w0, #1
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_doskey_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*, w1 = argc, x2 = argv
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements DOSKEY `/HISTORY`, `/MACROS`, `/REINSTALL`, and `NAME=BODY` macro registration.
@proc bi_doskey_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp

    ldr x0, [x0, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cbz w0, .ok0

    cmp w0, #0x2F
    b.ne .macro_def
    ldrb w0, [x19, #1]
    bl ascii_upper_a64
    cmp w0, #0x48
    b.eq .show_hist
    cmp w0, #0x4D
    b.eq .show_macs
    cmp w0, #0x52
    b.eq .reinstall
    b .ok0

.show_hist:
    bl hal_get_ctx_a64
    mov x19, x0
    ldr w20, [x19, #ARM64_CTX.dwHistCount]
    ldr x21, [x19, #ARM64_CTX.pHistRing]
    mov w22, #0
.h_lp:
    cmp w22, w20
    b.hs .ok0
    uxtw x0, w22
    lsl x0, x0, #3
    ldr x0, [x21, x0]
    bl out_str_a64
    bl out_crlf_a64
    add w22, w22, #1
    b .h_lp

.show_macs:
    bl hal_get_ctx_a64
    mov x19, x0
    ldr w20, [x19, #ARM64_CTX.dwMacroCount]
    ldr x21, [x19, #ARM64_CTX.pMacroNames]
    ldr x22, [x19, #ARM64_CTX.pMacroBodies]
    mov w23, #0
.m_lp:
    cmp w23, w20
    b.hs .ok0
    uxtw x1, w23
    lsl x1, x1, #3
    ldr x0, [x21, x1]
    bl out_str_a64
    adr_sym x0, szEqStr_a64
    bl out_str_a64
    uxtw x1, w23
    lsl x1, x1, #3
    ldr x0, [x22, x1]
    bl out_str_a64
    bl out_crlf_a64
    add w23, w23, #1
    b .m_lp

.reinstall:
    bl hal_get_ctx_a64
    str wzr, [x0, #ARM64_CTX.dwHistCount]
    str wzr, [x0, #ARM64_CTX.dwMacroCount]
    b .ok0

.macro_def:
    mov x0, x19
    bl strip_outer_quotes_a64
    mov x19, x0
    mov x20, #0
.meq:
    ldrb w0, [x19, x20]
    cbz w0, .ok0
    cmp w0, #0x3D
    b.eq .mgot
    add x20, x20, #1
    b .meq
.mgot:
    add x0, x20, #2
    bl perm_alloc_a64
    cbz x0, .ok0
    mov x21, x0
    mov x1, x19
    mov x2, x20
    bl mem_copy_a64
    strb wzr, [x0, #0]

    add x0, x19, x20
    add x0, x0, #1
    bl perm_strdup_a64
    cbz x0, .ok0
    mov x22, x0

    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwMacroCount]
    cmp w1, #A64_MACRO_MAX
    b.hs .ok0
    uxtw x2, w1
    lsl x2, x2, #3
    ldr x3, [x0, #ARM64_CTX.pMacroNames]
    str x21, [x3, x2]
    ldr x3, [x0, #ARM64_CTX.pMacroBodies]
    str x22, [x3, x2]
    add w1, w1, #1
    str w1, [x0, #ARM64_CTX.dwMacroCount]

.ok0:
    mov w0, #0
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc bi_misc_simple_a64
;; @abi win_arm64
;; @in w0 = cmd_id, x1 = AST_NODE_A64*, w2 = argc, x3 = argv
;; @out w0 = command exit code
;; @err w0 != 0 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Handles CLS, TITLE, CHCP, PAUSE, PATH, PROMPT, DPATH, DATE, TIME, BREAK, VERIFY, KEYS, CTTY, MODE, SHIFT [/n], EXIT.
@proc bi_misc_simple_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp
    mov w19, w0 ; id
    mov x20, x1 ; node
    mov w21, w2 ; argc
    mov x22, x3 ; argv

    cmp w19, #CMD_CLS
    b.eq .do_cls
    cmp w19, #CMD_TITLE
    b.eq .do_title
    cmp w19, #CMD_CHCP
    b.eq .do_chcp
    cmp w19, #CMD_PAUSE
    b.eq .do_pause
    cmp w19, #CMD_PATH
    b.eq .do_path
    cmp w19, #CMD_PROMPT
    b.eq .do_prompt
    cmp w19, #CMD_DPATH
    b.eq .do_dpath
    cmp w19, #CMD_DATE
    b.eq .do_date
    cmp w19, #CMD_TIME
    b.eq .do_time
    cmp w19, #CMD_BREAK
    b.eq .do_break
    cmp w19, #CMD_VERIFY
    b.eq .do_verify
    cmp w19, #CMD_KEYS
    b.eq .do_keys
    cmp w19, #CMD_SHIFT
    b.eq .do_shift
    cmp w19, #CMD_EXIT
    b.eq .do_exit
    cmp w19, #CMD_MODE
    b.eq .do_mode
    b .ok0

.do_cls:
    bl hal_clear_console_a64
    b .ok0

.do_title:
    ldr x0, [x20, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    bl wtf8_to_u16_a64
    cbz x0, .ok0
    bl hal_set_title_a64
    b .ok0

.do_chcp:
    cmp w21, #2
    b.lo .show_cp
    ldr x0, [x22, #8]
    bl parse_signed_dec_a64
    cmp x0, #0
    b.le .err1
    bl hal_get_set_cp_a64
.show_cp:
    adr_sym x0, szActiveCpMsg_a64
    bl out_str_a64
    bl hal_get_ctx_a64
    ldr w0, [x0, #ARM64_CTX.dwCodePage]
    mov x1, sp
    bl fmt_i32_a64
    mov x0, sp
    bl out_str_a64
    bl out_crlf_a64
    b .ok0

.do_pause:
    mov_imm32 w0, STR_OFF_PAUSE_MSG
    bl str_ptr_a64
    bl out_str_a64
    mov x0, sp
    mov w1, #16
    bl read_stdin_line_a64
    bl out_crlf_a64
    b .ok0

.do_path:
    adr_sym x19, szVarPath_a64
    b .env_shortcut
.do_prompt:
    adr_sym x19, szVarPrompt_a64
    b .env_shortcut
.do_dpath:
    adr_sym x19, szVarDpath_a64
.env_shortcut:
    ldr x0, [x20, #AST_NODE_A64.pStr1]
    bl extract_raw_tail_a64
    bl skip_ws_a64
    ldrb w1, [x0, #0]
    cbz w1, .show_env_sc
    cmp w1, #0x3B
    b.ne .set_env_sc
    ldrb w2, [x0, #1]
    cbnz w2, .set_env_sc
    mov x0, x19
    bl env_unset_a64
    b .ok0
.set_env_sc:
    cmp w1, #0x3D
    b.ne .set_env_go
    add x0, x0, #1
.set_env_go:
    mov x1, x0
    mov x0, x19
    bl env_set_a64
    b .ok0
.show_env_sc:
    mov x0, x19
    bl out_str_a64
    adr_sym x0, szEqStr_a64
    bl out_str_a64
    mov x0, x19
    bl env_get_a64
    cbz x0, .sc_nl
    bl out_str_a64
.sc_nl:
    bl out_crlf_a64
    b .ok0

.do_date:
    mov w0, #0
    bl fmt_dyn_date_time_a64
    bl out_str_a64
    bl out_crlf_a64
    b .ok0

.do_time:
    mov w0, #1
    bl fmt_dyn_date_time_a64
    bl out_str_a64
    bl out_crlf_a64
    b .ok0

.do_break:
    adr_sym x0, szBreakMsg_a64
    bl out_str_a64
    b .ok0

.do_verify:
    cmp w21, #2
    b.lo .show_verf
    ldr x0, [x22, #8]
    adr_sym x1, szKwOn_a64
    mov w2, #1
    bl str_cmp_a64
    mov w23, w0
    bl hal_get_ctx_a64
    cmp w23, #0
    cset w1, eq
    strb w1, [x0, #ARM64_CTX.bVerifyFlag]
    b .ok0
.show_verf:
    bl hal_get_ctx_a64
    ldrb w0, [x0, #ARM64_CTX.bVerifyFlag]
    cbz w0, .prt_voff
    adr_sym x0, szVerifyOn_a64
    bl out_str_a64
    b .ok0
.prt_voff:
    adr_sym x0, szVerifyOff_a64
    bl out_str_a64
    b .ok0

.do_keys:
    adr_sym x0, szKeysMsg_a64
    bl out_str_a64
    b .ok0

.do_mode:
    adr_sym x0, szModeMsg_a64
    bl out_str_a64
    b .ok0

.do_shift:
    mov w23, #0 ; shift start index (0..8)
    cmp w21, #2
    b.lo .sh_go
    ldr x0, [x22, #8]
    ldrb w1, [x0, #0]
    cmp w1, #0x2F
    b.ne .sh_go
    ldrb w1, [x0, #1]
    cmp w1, #0x30
    b.lo .sh_go
    cmp w1, #0x38
    b.hi .sh_go
    sub w23, w1, #0x30
.sh_go:
    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .ok0
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x0, #ARM64_CTX.pBatchStack]
    add x2, x2, x1
    cbnz w23, .sh_at_n
    ldr w0, [x2, #BATCH_FRAME_A64.dwShiftOff]
    add w0, w0, #1
    str w0, [x2, #BATCH_FRAME_A64.dwShiftOff]
    b .ok0
.sh_at_n:
    ldr w0, [x2, #BATCH_FRAME_A64.dwShiftOff]
    add w3, w0, w23 ; physical index in pArgVec
    ldr w4, [x2, #BATCH_FRAME_A64.dwArgCount]
    cmp w3, w4
    b.hs .ok0
    ldr x5, [x2, #BATCH_FRAME_A64.pArgVec]
    sub w4, w4, #1
    str w4, [x2, #BATCH_FRAME_A64.dwArgCount]
.sh_mv:
    cmp w3, w4
    b.hs .ok0
    add w0, w3, #1
    uxtw x0, w0
    lsl x0, x0, #3
    ldr x1, [x5, x0]
    uxtw x0, w3
    lsl x0, x0, #3
    str x1, [x5, x0]
    add w3, w3, #1
    b .sh_mv

.do_exit:
    mov w19, #0 ; /B flag
    mov w20, #0 ; has explicit code
    mov w23, #0 ; parsed code
    mov w24, #1
.ex_lp:
    cmp w24, w21
    b.hs .ex_apply
    uxtw x0, w24
    lsl x0, x0, #3
    ldr x0, [x22, x0]
    ldrb w1, [x0, #0]
    cmp w1, #0x2F
    b.ne .ex_code
    ldrb w0, [x0, #1]
    bl ascii_upper_a64
    cmp w0, #0x42
    b.ne .ex_nx
    mov w19, #1
    b .ex_nx
.ex_code:
    bl parse_signed_dec_a64
    mov w23, w0
    mov w20, #1
.ex_nx:
    add w24, w24, #1
    b .ex_lp

.ex_apply:
    bl hal_get_ctx_a64
    cbnz w20, .got_exc
    ldr w23, [x0, #ARM64_CTX.dwLastErrorLevel]
.got_exc:
    cbz w19, .exit_proc
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .exit_proc
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x0, #ARM64_CTX.pBatchStack]
    add x2, x2, x1
    mov x1, #-1
    str x1, [x2, #BATCH_FRAME_A64.uFileOff]
    mov w0, w23
    b .done

.exit_proc:
    str w23, [x0, #ARM64_CTX.dwExitCode]
    mov w1, #1
    strb w1, [x0, #ARM64_CTX.bShouldExit]
    mov w0, w23
    b .done

.err1:
    mov w0, #1
    b .done
.ok0:
    mov w0, #0
.done:
    add sp, sp, #32
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
