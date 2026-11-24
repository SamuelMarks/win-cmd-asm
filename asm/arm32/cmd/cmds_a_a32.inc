;; @file cmds_a_a32.inc
;; @isa armv4
;; @desc ARM32 Internal Command Lookup & Built-Ins Part A (PROMPT, ECHO, SET, ASSOC, FTYPE, DOSKEY, CALL, GOTO, etc.).

@cpu armv4
@format inc

;; @proc print_prompt_a32
;; @abi arm_ce
;; @in none
;; @out Renders %PROMPT% ($P, $G, $L, $B, $Q, $S, $E, $H, $_, $D, $T, $N, $$) to stdout
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Formats and prints the active interactive or batch-echo prompt string.
@proc print_prompt_a32
    push {r4, r5, r6, lr}
    adr_sym r0, szVarPrompt
    bl env_get_a32
    cmp r0, #0
    bne .got_p
    mov r0, #STR_OFF_DEF_PROMPT
    bl str_ptr_a32
.got_p:
    mov r4, r0 ; template
    mov_imm32 r0, 2048
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r5, r0 ; out base
    mov r6, r0 ; out cursor

.lp:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .emit
    cmp r0, #0x24 ; '$'
    beq .esc
    strb r0, [r6, #0]
    add r4, r4, #1
    add r6, r6, #1
    b .lp

.esc:
    add r4, r4, #1
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .emit
    add r4, r4, #1
    bl ascii_upper_a32
    cmp r0, #0x50 ; 'P'
    beq .p_cwd
    cmp r0, #0x47 ; 'G'
    beq .p_gt
    cmp r0, #0x4C ; 'L'
    beq .p_lt
    cmp r0, #0x42 ; 'B'
    beq .p_bar
    cmp r0, #0x51 ; 'Q'
    beq .p_eq
    cmp r0, #0x53 ; 'S'
    beq .p_sp
    cmp r0, #0x45 ; 'E'
    beq .p_esc
    cmp r0, #0x48 ; 'H'
    beq .p_bs
    cmp r0, #0x5F ; '_'
    beq .p_crlf
    cmp r0, #0x24 ; '$'
    beq .p_dlr
    cmp r0, #0x4E ; 'N'
    beq .p_drv
    cmp r0, #0x44 ; 'D'
    beq .p_date
    cmp r0, #0x54 ; 'T'
    beq .p_time
    b .lp

.p_cwd:
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r1, r0, r1
    mov r0, r6
    bl str_copy_a32
    mov r6, r0
    b .lp
.p_drv:
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r1, r0, r1
    ldrb r0, [r1, #0]
    strb r0, [r6, #0]
    add r6, r6, #1
    b .lp
.p_gt:
    mov r0, #0x3E
    b .st1
.p_lt:
    mov r0, #0x3C
    b .st1
.p_bar:
    mov r0, #0x7C
    b .st1
.p_eq:
    mov r0, #0x3D
    b .st1
.p_sp:
    mov r0, #0x20
    b .st1
.p_esc:
    mov r0, #0x1B
    b .st1
.p_dlr:
    mov r0, #0x24
.st1:
    strb r0, [r6, #0]
    add r6, r6, #1
    b .lp
.p_bs:
    cmp r6, r5
    bls .lp
    sub r6, r6, #1
    b .lp
.p_crlf:
    mov r0, #0x0D
    strb r0, [r6, #0]
    mov r0, #0x0A
    strb r0, [r6, #1]
    add r6, r6, #2
    b .lp
.p_date:
    mov r0, #0
    bl fmt_dyn_date_time_a32
    cmp r0, #0
    beq .lp
    mov r1, r0
    mov r0, r6
    bl str_copy_a32
    mov r6, r0
    b .lp
.p_time:
    mov r0, #1
    bl fmt_dyn_date_time_a32
    cmp r0, #0
    beq .lp
    mov r1, r0
    mov r0, r6
    bl str_copy_a32
    mov r6, r0
    b .lp

.emit:
    mov r0, #0
    strb r0, [r6, #0]
    mov r0, r5
    bl out_str_a32
.done:
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc lookup_builtin_id_a32
;; @abi arm_ce
;; @in r0 = unquoted command name string
;; @out r0 = CMD_* id (or CMD_NONE = 0 if external)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Matches internal command names and aliases (CHDIR, ERASE, MKDIR, RMDIR, RENAME, LOADHIGH, COMMAND) against g_a32_cmd_tbl.
@proc lookup_builtin_id_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    adr_sym r5, g_a32_cmd_tbl

.tbl_lp:
    ldrb r0, [r5, #0] ; cmd id
    cmp r0, #0
    beq .not_found
    add r1, r5, #1    ; NUL-terminated uppercase name
    mov r0, r4
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .matched

    ; Advance r5 past id byte + name + NUL
    add r5, r5, #1
.sk_nm:
    ldrb r0, [r5, #0]
    add r5, r5, #1
    cmp r0, #0
    bne .sk_nm
    b .tbl_lp

.matched:
    ldrb r0, [r5, #0]
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr

.not_found:
    mov r0, #CMD_NONE
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc extract_raw_tail_a32
;; @abi arm_ce
;; @in r0 = raw command line (pStr1)
;; @out r0 = trimmed raw argument tail after the command name (with any redirections stripped)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Extracts the unsplit argument tail after word0 while stripping `<`/`>` redirections.
@proc extract_raw_tail_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    cmp r0, #0
    beq .ret_empty
    bl skip_ws_a32
    mov r4, r0

    ; Skip leading redirections if any before command token
.skip_lead_red:
    ldrb r0, [r4, #0]
    cmp r0, #0x3C
    beq .sk_red_1
    cmp r0, #0x3E
    beq .sk_red_1
    cmp r0, #0x30
    bcc .past_lead
    cmp r0, #0x39
    bhi .past_lead
    ldrb r1, [r4, #1]
    cmp r1, #0x3C
    beq .sk_red_2
    cmp r1, #0x3E
    bne .past_lead
.sk_red_2:
    add r4, r4, #1
.sk_red_1:
    add r4, r4, #1
    ldrb r0, [r4, #0]
    cmp r0, #0x3E
    bne .sk_red_t
    add r4, r4, #1
.sk_red_t:
    ldrb r0, [r4, #0]
    cmp r0, #0x26
    bne .sk_red_tok
    add r4, r4, #2
    mov r0, r4
    bl skip_ws_a32
    mov r4, r0
    b .skip_lead_red
.sk_red_tok:
    mov r0, r4
    bl scan_cmd_token_a32
    mov r0, r1
    bl skip_ws_a32
    mov r4, r0
    b .skip_lead_red

.past_lead:
    ; Skip command name token itself
    mov r0, r4
    bl scan_cmd_token_a32
    mov r0, r1
    cmp r0, #0
    beq .ret_empty
    ldrb r1, [r0, #0]
    cmp r1, #0x20
    bne .no_sp1
    add r0, r0, #1
.no_sp1:
    mov r4, r0

    ; Copy up to any unquoted redirection `<` or `>`
    mov r5, r4
    mov r6, #0 ; quote
.sc_end:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .got_end
    cmp r0, #0x22
    bne .chk_c
    eor r6, r6, #1
    add r5, r5, #1
    b .sc_end
.chk_c:
    cmp r6, #0
    bne .adv_e
    cmp r0, #0x3C
    beq .got_end
    cmp r0, #0x3E
    beq .got_end
    cmp r0, #0x30
    bcc .adv_e
    cmp r0, #0x39
    bhi .adv_e
    ldrb r1, [r5, #1]
    cmp r1, #0x3E
    beq .chk_prev_sp
    cmp r1, #0x3C
    bne .adv_e
.chk_prev_sp:
    cmp r5, r4
    beq .got_end
    sub r2, r5, #1
    ldrb r1, [r2, #0]
    cmp r1, #0x20
    beq .got_end
.adv_e:
    add r5, r5, #1
    b .sc_end

.got_end:
    sub r7, r5, r4
    ; Trim trailing space before redirection if redirection stopped us
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .alloc_tail
    cmp r7, #0
    beq .alloc_tail
    sub r1, r5, #1
    ldrb r0, [r1, #0]
    cmp r0, #0x20
    bne .alloc_tail
    sub r7, r7, #1
.alloc_tail:
    add r0, r7, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_empty
    mov r6, r0
    mov r1, r4
    mov r2, r7
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r6
    mov r1, #0
    bl expand_phase34_a32
    b .done

.ret_empty:
    mov r0, #4
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r1, #0
    strb r1, [r0, #0]
.done:
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc bi_echo_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*, r1 = argc, r2 = expanded argv
;; @out r0 = exit code (0)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements ECHO, ECHO ON, ECHO OFF, ECHO., ECHO(, and arbitrary text output.
@proc bi_echo_a32
    push {r4, r5, r6, lr}
    mov r4, r0 ; node
    mov r5, r1 ; argc
    mov r6, r2 ; argv

    ldrb r0, [r4, #AST_NODE_A32.bFlags]
    tst r0, #AST_FLAG_ECHO_DELIM
    bne .delim_mode

    cmp r5, #2
    bcc .show_status
    ldr r0, [r6, #4] ; argv[1]
    bl skip_ws_a32
    ldrb r1, [r0, #0]
    cmp r1, #0
    beq .show_status

    ; Check `ON` or `OFF` (ignoring trailing spaces)
    mov r4, r0
    adr_sym r1, szKwOn
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .set_on
    mov r0, r4
    adr_sym r1, szKwOff
    mov r2, #1
    bl str_cmp_a32
    cmp r0, #0
    beq .set_off

    ldr r0, [r6, #4]
    bl out_str_a32
    bl out_crlf_a32
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr

.delim_mode:
    cmp r5, #2
    bcc .just_nl
    ldr r0, [r6, #4]
    bl out_str_a32
.just_nl:
    bl out_crlf_a32
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr

.set_on:
    bl hal_get_ctx
    mov r1, #1
    strb r1, [r0, #ARM32_CTX.bEchoOn]
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr

.set_off:
    bl hal_get_ctx
    mov r1, #0
    strb r1, [r0, #ARM32_CTX.bEchoOn]
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr

.show_status:
    bl hal_get_ctx
    ldrb r0, [r0, #ARM32_CTX.bEchoOn]
    cmp r0, #0
    beq .prt_off
    mov r0, #STR_OFF_ECHO_ON
    bl str_ptr_a32
    bl out_str_a32
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
.prt_off:
    mov r0, #STR_OFF_ECHO_OFF
    bl str_ptr_a32
    bl out_str_a32
    mov r0, #0
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc bi_set_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*
;; @out r0 = exit code (0 on success, 1 on undefined prefix or SET /A error)
;; @err r0 = 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements `SET`, `SET prefix`, `SET VAR=val`, `SET "VAR=val"`, `SET /A expr`, and `SET /P VAR=prompt`.
@proc bi_set_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #28
    ldr r0, [r0, #AST_NODE_A32.pStr1]
    mov r1, #0
    bl expand_phase34_a32
    bl extract_raw_tail_a32
    bl skip_ws_a32
    mov r4, r0

    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .list_all

    ; Check `/A` or `/P`
    cmp r0, #0x2F ; '/'
    bne .chk_quoted_set
    ldrb r0, [r4, #1]
    bl ascii_upper_a32
    cmp r0, #0x41 ; 'A'
    beq .do_set_a
    cmp r0, #0x50 ; 'P'
    beq .do_set_p

.chk_quoted_set:
    ldrb r0, [r4, #0]
    cmp r0, #0x22 ; `SET "VAR=VAL"`!
    bne .scan_eq
    add r4, r4, #1
    mov r0, r4
    bl str_len_a32
.qs_bk:
    cmp r0, #0
    beq .scan_eq
    sub r0, r0, #1
    ldrb r1, [r4, r0]
    cmp r1, #0x22
    bne .qs_bk
    mov r1, #0
    strb r1, [r4, r0]

.scan_eq:
    ; Look for `=` in r4 (remember hidden vars would start at index 1 if r4[0]=='=' -> error)
    ldrb r0, [r4, #0]
    cmp r0, #0x3D
    beq .syn_err
    mov r5, #0
.eq_lp:
    ldrb r0, [r4, r5]
    cmp r0, #0
    beq .prefix_query
    cmp r0, #0x3D
    beq .assign_kv
    add r5, r5, #1
    b .eq_lp

.assign_kv:
    add r0, r5, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_err
    mov r6, r0
    mov r1, r4
    mov r2, r5
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    add r1, r4, r5
    add r1, r1, #1
    mov r0, r6
    bl env_set_a32
    mov r0, #0
    b .done

.prefix_query:
    mov r0, r4
    bl str_len_a32
    mov r5, r0 ; prefix len
    bl hal_get_ctx
    ldr r6, [r0, #ARM32_CTX.pEnvBlock]
    mov r7, #0 ; match count
.pf_lp:
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .pf_done
    ldrb r0, [r6, #0]
    cmp r0, #0x3D ; skip hidden `=C:` entries
    beq .pf_nx
    ; Check prefix match of length r5
    mov r3, #0
.pf_c:
    cmp r3, r5
    beq .pf_hit
    ldrb r0, [r6, r3]
    cmp r0, #0
    beq .pf_nx
    cmp r0, #0x3D
    beq .pf_nx
    bl ascii_upper_a32
    mov r2, r0
    ldrb r0, [r4, r3]
    bl ascii_upper_a32
    cmp r0, r2
    bne .pf_nx
    add r3, r3, #1
    b .pf_c
.pf_hit:
    mov r0, r6
    bl out_str_a32
    bl out_crlf_a32
    add r7, r7, #1
.pf_nx:
    ldrb r0, [r6, #0]
    add r6, r6, #1
    cmp r0, #0
    bne .pf_nx
    b .pf_lp
.pf_done:
    cmp r7, #0
    beq .ret_err
    mov r0, #0
    b .done

.list_all:
    bl hal_get_ctx
    ldr r6, [r0, #ARM32_CTX.pEnvBlock]
.la_lp:
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .ok0
    cmp r0, #0x3D ; hide `=C:` entries in bare `SET`
    beq .la_nx
    mov r0, r6
    bl out_str_a32
    bl out_crlf_a32
.la_nx:
    ldrb r0, [r6, #0]
    add r6, r6, #1
    cmp r0, #0
    bne .la_nx
    b .la_lp

.do_set_a:
    add r0, r4, #2
    bl eval_set_a_a32
    cmp r1, #0
    bne .sa_err
    mov r5, r0
    ; In interactive mode (dwBatchDepth == 0), print the result!
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r0, #0
    bne .ok0
    mov r0, r5
    mov r1, sp
    bl fmt_i32_a32
    mov r0, sp
    bl out_str_a32
    mov r0, #0
    b .done

.sa_err:
    mov r0, r1
    bl print_err_code_a32
    b .ret_err

.do_set_p:
    add r0, r4, #2
    bl skip_ws_a32
    bl strip_outer_quotes_a32
    mov r4, r0
    mov r5, #0
.sp_eq:
    ldrb r0, [r4, r5]
    cmp r0, #0
    beq .syn_err
    cmp r0, #0x3D
    beq .sp_got
    add r5, r5, #1
    b .sp_eq
.sp_got:
    add r0, r5, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_err
    mov r6, r0
    mov r1, r4
    mov r2, r5
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    add r0, r4, r5
    add r0, r0, #1
    ldrb r1, [r0, #0]
    cmp r1, #0
    beq .sp_rd
    bl out_str_a32
.sp_rd:
    mov_imm32 r0, 1024
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_err
    mov r7, r0
    mov r1, #1020
    bl read_stdin_line_a32
    cmp r0, #0
    beq .ret_err
    mov r0, r6
    mov r1, r7
    bl env_set_a32
    mov r0, #0
    b .done

.syn_err:
    mov_imm32 r0, ERR_SYNTAX_ERROR
    bl print_err_code_a32
.ret_err:
    mov r0, #1
    b .done
.ok0:
    mov r0, #0
.done:
    add sp, sp, #28
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc bi_ver_a32
;; @abi arm_ce
;; @in none
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints authentic OS & shell version banner (`Windows CE` on ARMv4 or `Microsoft Windows [Version major.minor.build]` on ARMNT).
@proc bi_ver_a32
    push {r4, r5, lr}
    sub sp, sp, #36
    bl out_crlf_a32
    bl hal_get_ctx
    mov r4, r0
    mov r0, #STR_OFF_BANNER
    bl str_ptr_a32
    bl out_str_a32

    ldr r0, [r4, #ARM32_CTX.dwOsMajor]
    mov r1, sp
    bl fmt_i32_a32
    mov r0, sp
    bl out_str_a32
    adr_sym r0, szDot
    bl out_str_a32

    ldr r0, [r4, #ARM32_CTX.dwOsMinor]
    mov r1, sp
    bl fmt_i32_a32
    mov r0, sp
    bl out_str_a32
    adr_sym r0, szDot
    bl out_str_a32

    ldr r0, [r4, #ARM32_CTX.dwOsBuild]
    mov r1, sp
    bl fmt_i32_a32
    mov r0, sp
    bl out_str_a32
    adr_sym r0, szVerClose
    bl out_str_a32

    mov r0, #0
    add sp, sp, #36
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc bi_assoc_ftype_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*, r1 = is_ftype (0 = ASSOC, 1 = FTYPE)
;; @out r0 = 0 on success, 1 if queried association not found
;; @err r0 = 1 if missing
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries or updates HKLM\Software\Classes file associations (`ASSOC` and `FTYPE`) via Registry HAL.
@proc bi_assoc_ftype_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r5, r1 ; is_ftype
    ldr r0, [r0, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    bl skip_ws_a32
    bl strip_outer_quotes_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .ok0

    ; Find `=`
    mov r6, #0
.eq_sc:
    ldrb r0, [r4, r6]
    cmp r0, #0
    beq .query_only
    cmp r0, #0x3D
    beq .set_val
    add r6, r6, #1
    b .eq_sc

.set_val:
    add r0, r6, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r7, r0 ; key stem (e.g. `.txt` or `txtfile`)
    mov r1, r4
    mov r2, r6
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    add r8, r4, r6
    add r8, r8, #1 ; value

    ; Build subkey `Software\Classes\<stem>` (or `...\shell\open\command` for FTYPE)
    mov_imm32 r0, 512
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r6, r0
    adr_sym r1, szRegClassesPrefix
    bl str_copy_a32
    mov r1, r7
    bl str_copy_a32
    cmp r5, #0
    beq .w_reg
    adr_sym r1, szRegShellOpenCmd
    bl str_copy_a32
.w_reg:
    mov r0, r6
    bl wtf8_to_u16_a32
    mov r4, r0
    mov r0, r8
    bl wtf8_to_u16_a32
    mov r3, r0
    mov_imm32 r0, 0x80000001 ; HKCU\Software\Classes (unprivileged-safe on WinRT/Win10 & WinCE!)
    mov r1, r4
    mov r2, #0               ; default (unnamed) value
    bl hal_reg_set_str
    cmp r0, #0
    bne .err1
    ; Echo `stem=val`
    mov r0, r7
    bl out_str_a32
    adr_sym r0, szEqStr
    bl out_str_a32
    mov r0, r8
    bl out_str_a32
    bl out_crlf_a32
    mov r0, #0
    b .done

.query_only:
    mov_imm32 r0, 512
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r6, r0
    adr_sym r1, szRegClassesPrefix
    bl str_copy_a32
    mov r1, r4
    bl str_copy_a32
    cmp r5, #0
    beq .q_reg
    adr_sym r1, szRegShellOpenCmd
    bl str_copy_a32
.q_reg:
    mov r0, r6
    bl wtf8_to_u16_a32
    mov r6, r0
    mov_imm32 r0, 520
    bl arena_alloc_a32
    cmp r0, #0
    beq .err1
    mov r7, r0
    ; Try HKCU\Software\Classes first, then HKLM\Software\Classes
    mov_imm32 r0, 0x80000001
    mov r1, r6
    mov r2, #0
    mov r3, r7
    bl hal_reg_query_str
    cmp r0, #0
    beq .got_q
    mov_imm32 r0, 0x80000002
    mov r1, r6
    mov r2, #0
    mov r3, r7
    bl hal_reg_query_str
    cmp r0, #0
    bne .err1
.got_q:
    mov r0, r4
    bl out_str_a32
    adr_sym r0, szEqStr
    bl out_str_a32
    mov r0, r7
    bl out_str_a32
    bl out_crlf_a32
.ok0:
    mov r0, #0
    b .done
.err1:
    mov r0, #1
.done:
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc bi_color_a32
;; @abi arm_ce
;; @in r0 = argc, r1 = argv
;; @out r0 = 0 on success, 1 if fg == bg
;; @err r0 = 1 when foreground nibble equals background nibble
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements COLOR [attr]: resets default on bare COLOR, rejects fg == bg with exit code 1.
@proc bi_color_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    cmp r0, #2
    bcc .reset_def
    ldr r4, [r1, #4]
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .reset_def

    mov r5, #0 ; parsed attr
    mov r2, #0 ; count
.hex_lp:
    ldrb r0, [r4, r2]
    cmp r0, #0
    beq .chk_cnt
    cmp r2, #2
    bcs .bad_c
    cmp r0, #0x30
    bcc .bad_c
    cmp r0, #0x39
    bls .hd
    bl ascii_upper_a32
    cmp r0, #0x41
    bcc .bad_c
    cmp r0, #0x46
    bhi .bad_c
    sub r0, r0, #0x37
    b .ha
.hd:
    sub r0, r0, #0x30
.ha:
    lsl r5, r5, #4
    orr r5, r5, r0
    add r2, r2, #1
    b .hex_lp
.chk_cnt:
    cmp r2, #2
    bcc .apply_c
    lsr r0, r5, #4
    and r1, r5, #0x0F
    cmp r0, r1
    beq .bad_c
.apply_c:
    mov r0, r5
    bl hal_set_console_attr
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr

.reset_def:
    bl hal_get_ctx
    ldrh r0, [r0, #ARM32_CTX.wDefaultAttr]
    bl hal_set_console_attr
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.bad_c:
    mov r0, #1
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc bi_doskey_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*, r1 = argc, r2 = argv
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Implements DOSKEY `/HISTORY`, `/MACROS`, `/REINSTALL`, and `NAME=BODY` macro registration.
@proc bi_doskey_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r5, r1
    mov r6, r2
    ldr r0, [r0, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    bl skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .ok0

    cmp r0, #0x2F ; '/'
    bne .macro_def
    ldrb r0, [r4, #1]
    bl ascii_upper_a32
    cmp r0, #0x48 ; '/HISTORY'
    beq .show_hist
    cmp r0, #0x4D ; '/MACROS'
    beq .show_macs
    cmp r0, #0x52 ; '/REINSTALL'
    beq .reinstall
    b .ok0

.show_hist:
    bl hal_get_ctx
    mov r4, r0
    ldr r5, [r4, #ARM32_CTX.dwHistCount]
    ldr r6, [r4, #ARM32_CTX.pHistRing]
    mov r7, #0
.h_lp:
    cmp r7, r5
    bcs .ok0
    lsl r0, r7, #2
    ldr r0, [r6, r0]
    bl out_str_a32
    bl out_crlf_a32
    add r7, r7, #1
    b .h_lp

.show_macs:
    bl hal_get_ctx
    mov r4, r0
    ldr r5, [r4, #ARM32_CTX.dwMacroCount]
    ldr r6, [r4, #ARM32_CTX.pMacroNames]
    ldr r7, [r4, #ARM32_CTX.pMacroBodies]
    mov r3, #0
.m_lp:
    cmp r3, r5
    bcs .ok0
    push {r3, r4}
    lsl r1, r3, #2
    ldr r0, [r6, r1]
    bl out_str_a32
    adr_sym r0, szEqStr
    bl out_str_a32
    ldr r3, [sp, #0]
    lsl r1, r3, #2
    ldr r0, [r7, r1]
    bl out_str_a32
    bl out_crlf_a32
    pop {r3, r4}
    add r3, r3, #1
    b .m_lp

.reinstall:
    bl hal_get_ctx
    mov r1, #0
    str r1, [r0, #ARM32_CTX.dwHistCount]
    str r1, [r0, #ARM32_CTX.dwMacroCount]
    b .ok0

.macro_def:
    mov r0, r4
    bl strip_outer_quotes_a32
    mov r4, r0
    mov r5, #0
.meq:
    ldrb r0, [r4, r5]
    cmp r0, #0
    beq .ok0
    cmp r0, #0x3D
    beq .mgot
    add r5, r5, #1
    b .meq
.mgot:
    add r0, r5, #2
    bl perm_alloc_a32
    cmp r0, #0
    beq .ok0
    mov r6, r0
    mov r1, r4
    mov r2, r5
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]

    add r4, r4, r5
    add r4, r4, #1
    mov r0, r4
    bl str_len_a32
    add r0, r0, #2
    bl perm_alloc_a32
    cmp r0, #0
    beq .ok0
    mov r7, r0
    mov r1, r4
    bl str_copy_a32

    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwMacroCount]
    cmp r1, #A32_MACRO_MAX
    bcs .ok0
    lsl r2, r1, #2
    ldr r3, [r0, #ARM32_CTX.pMacroNames]
    str r6, [r3, r2]
    ldr r3, [r0, #ARM32_CTX.pMacroBodies]
    str r7, [r3, r2]
    add r1, r1, #1
    str r1, [r0, #ARM32_CTX.dwMacroCount]

.ok0:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc bi_misc_simple_a32
;; @abi arm_ce
;; @in r0 = cmd_id, r1 = AST_NODE_A32*, r2 = argc, r3 = argv
;; @out r0 = command exit code
;; @err r0 != 0 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Handles CLS, TITLE, CHCP, PAUSE, PATH, PROMPT, DPATH, DATE, TIME, BREAK, VERIFY, KEYS, CTTY, LH, MODE, SHIFT, EXIT.
@proc bi_misc_simple_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #20
    mov r4, r0 ; id
    mov r5, r1 ; node
    mov r6, r2 ; argc
    mov r7, r3 ; argv

    cmp r4, #CMD_CLS
    beq .do_cls
    cmp r4, #CMD_TITLE
    beq .do_title
    cmp r4, #CMD_CHCP
    beq .do_chcp
    cmp r4, #CMD_PAUSE
    beq .do_pause
    cmp r4, #CMD_PATH
    beq .do_path
    cmp r4, #CMD_PROMPT
    beq .do_prompt
    cmp r4, #CMD_DPATH
    beq .do_dpath
    cmp r4, #CMD_DATE
    beq .do_date
    cmp r4, #CMD_TIME
    beq .do_time
    cmp r4, #CMD_BREAK
    beq .do_break
    cmp r4, #CMD_VERIFY
    beq .do_verify
    cmp r4, #CMD_KEYS
    beq .do_keys
    cmp r4, #CMD_SHIFT
    beq .do_shift
    cmp r4, #CMD_EXIT
    beq .do_exit
    cmp r4, #CMD_MODE
    beq .do_mode
    mov r0, #0
    b .done

.do_cls:
    bl hal_clear_console
    mov r0, #0
    b .done

.do_title:
    ldr r0, [r5, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    bl wtf8_to_u16_a32
    cmp r0, #0
    beq .ok0
    bl hal_set_title
    mov r0, #0
    b .done

.do_chcp:
    cmp r6, #2
    bcc .show_cp
    ldr r0, [r7, #4]
    bl parse_signed_dec_a32
    cmp r0, #0
    ble .err1
    bl hal_get_set_cp
    b .show_cp
.show_cp:
    adr_sym r0, szActiveCpMsg
    bl out_str_a32
    bl hal_get_ctx
    ldr r0, [r0, #ARM32_CTX.dwCodePage]
    mov r1, sp
    bl fmt_i32_a32
    mov r0, sp
    bl out_str_a32
    bl out_crlf_a32
    mov r0, #0
    b .done

.do_pause:
    mov_imm32 r0, STR_OFF_PAUSE_MSG
    bl str_ptr_a32
    bl out_str_a32
    mov r0, sp
    mov r1, #8
    bl read_stdin_line_a32
    bl out_crlf_a32
    mov r0, #0
    b .done

.do_path:
    adr_sym r4, szVarPath
    b .env_shortcut
.do_prompt:
    adr_sym r4, szVarPrompt
    b .env_shortcut
.do_dpath:
    adr_sym r4, szVarDpath
.env_shortcut:
    ldr r0, [r5, #AST_NODE_A32.pStr1]
    bl extract_raw_tail_a32
    bl skip_ws_a32
    ldrb r1, [r0, #0]
    cmp r1, #0
    beq .show_env_sc
    cmp r1, #0x3B ; `PATH ;` clears PATH!
    bne .set_env_sc
    ldrb r2, [r0, #1]
    cmp r2, #0
    bne .set_env_sc
    mov r0, r4
    bl env_unset_a32
    mov r0, #0
    b .done
.set_env_sc:
    cmp r1, #0x3D ; optional `PATH=...`
    bne .set_env_go
    add r0, r0, #1
.set_env_go:
    mov r1, r0
    mov r0, r4
    bl env_set_a32
    mov r0, #0
    b .done
.show_env_sc:
    mov r0, r4
    bl out_str_a32
    adr_sym r0, szEqStr
    bl out_str_a32
    mov r0, r4
    bl env_get_a32
    cmp r0, #0
    beq .sc_nl
    bl out_str_a32
.sc_nl:
    bl out_crlf_a32
    mov r0, #0
    b .done

.do_date:
    mov r0, #0
    bl fmt_dyn_date_time_a32
    bl out_str_a32
    bl out_crlf_a32
    mov r0, #0
    b .done

.do_time:
    mov r0, #1
    bl fmt_dyn_date_time_a32
    bl out_str_a32
    bl out_crlf_a32
    mov r0, #0
    b .done

.do_break:
    adr_sym r0, szBreakMsg
    bl out_str_a32
    mov r0, #0
    b .done

.do_verify:
    cmp r6, #2
    bcc .show_verf
    ldr r0, [r7, #4]
    adr_sym r1, szKwOn
    mov r2, #1
    bl str_cmp_a32
    bl hal_get_ctx
    mov r2, r0
    cmp r2, #0
    bne .verf_off
    mov r1, #1
    b .verf_st
.verf_off:
    bl hal_get_ctx
    mov r1, #0
.verf_st:
    strb r1, [r0, #ARM32_CTX.bVerifyOn]
    mov r0, #0
    b .done
.show_verf:
    bl hal_get_ctx
    ldrb r0, [r0, #ARM32_CTX.bVerifyOn]
    cmp r0, #0
    beq .prt_voff
    adr_sym r0, szVerifyOn
    bl out_str_a32
    mov r0, #0
    b .done
.prt_voff:
    adr_sym r0, szVerifyOff
    bl out_str_a32
    mov r0, #0
    b .done

.do_keys:
    adr_sym r0, szKeysMsg
    bl out_str_a32
    mov r0, #0
    b .done

.do_mode:
    adr_sym r0, szModeMsg
    bl out_str_a32
    mov r0, #0
    b .done

.do_shift:
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .ok0
    sub r1, r1, #1
    lsl r1, r1, #6
    ldr r2, [r0, #ARM32_CTX.pBatchStack]
    add r2, r2, r1
    ldr r0, [r2, #BATCH_FRAME_A32.dwShiftBase]
    add r0, r0, #1
    str r0, [r2, #BATCH_FRAME_A32.dwShiftBase]
    mov r0, #0
    b .done

.do_exit:
    mov r4, #0 ; /B flag
    mov r5, #0 ; has explicit code
    mov r3, #0 ; parsed code
    mov r2, #1
.ex_lp:
    cmp r2, r6
    bcs .ex_apply
    lsl r0, r2, #2
    ldr r0, [r7, r0]
    ldrb r1, [r0, #0]
    cmp r1, #0x2F ; '/'
    bne .ex_code
    ldrb r1, [r0, #1]
    push {r2, r3}
    mov r0, r1
    bl ascii_upper_a32
    pop {r2, r3}
    cmp r0, #0x42 ; 'B'
    bne .ex_nx
    mov r4, #1
    b .ex_nx
.ex_code:
    push {r2, r4}
    bl parse_signed_dec_a32
    mov r3, r0
    pop {r2, r4}
    mov r5, #1
.ex_nx:
    add r2, r2, #1
    b .ex_lp

.ex_apply:
    bl hal_get_ctx
    cmp r5, #0
    bne .got_exc
    ldr r3, [r0, #ARM32_CTX.dwLastErrorLevel]
.got_exc:
    cmp r4, #0
    beq .exit_proc
    ; `EXIT /B`: if inside a batch frame, unwind top batch frame; otherwise exit process!
    ldr r1, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .exit_proc
    sub r1, r1, #1
    lsl r1, r1, #6
    ldr r2, [r0, #ARM32_CTX.pBatchStack]
    add r2, r2, r1
    mvn r1, #0
    str r1, [r2, #BATCH_FRAME_A32.uFileOffHi] ; mark frame EOF!
    mov r1, #1
    strb r1, [r2, #BATCH_FRAME_A32.bJumped]
    mov r0, r3
    b .done

.exit_proc:
    str r3, [r0, #ARM32_CTX.dwExitCode]
    mov r1, #1
    strb r1, [r0, #ARM32_CTX.bShouldExit]
    mov r0, r3
    b .done

.err1:
    mov r0, #1
    b .done
.ok0:
    mov r0, #0
.done:
    add sp, sp, #20
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc
