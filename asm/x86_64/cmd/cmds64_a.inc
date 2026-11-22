;; @file cmds64_a.inc
;; @isa x64
;; @desc x86-64 Internal Command Dispatcher & Built-Ins Part A (ECHO, SET, CALL, ASSOC, FTYPE, COLOR, CHCP, PROMPT, etc.).

@cpu x64
@format inc

;; @proc cmd_print_prompt64
;; @abi win64_ms
;; @in none
;; @out Expands %PROMPT% ($P, $G, $L, $B, $Q, $S, $E, $H, $_, $D, $T, $N, $$) and writes to stdout
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Renders the interactive or batch-echo command prompt from %PROMPT% (default $P$G).
@proc_frame cmd_print_prompt64, 56, rbx, rsi, rdi, r12
    lea rcx, [rel szVarPrompt64]
    call env_get64
    test rax, rax
    jnz .got_p
    lea rax, [rel szDefPrompt64]
.got_p:
    mov rsi, rax
    mov ecx, 4096
    call arena_alloc64
    jc .done
    mov rdi, rax
    mov rbx, rax ; out base

.lp:
    mov al, byte [rsi]
    test al, al
    jz .emit
    cmp al, '$'
    je .esc
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .lp

.esc:
    inc rsi
    mov al, byte [rsi]
    test al, al
    jz .emit
    inc rsi
    call ascii_upper_al64
    cmp al, 'P'
    je .p_cwd
    cmp al, 'G'
    je .p_gt
    cmp al, 'L'
    je .p_lt
    cmp al, 'B'
    je .p_bar
    cmp al, 'Q'
    je .p_eq
    cmp al, 'S'
    je .p_sp
    cmp al, 'E'
    je .p_esc
    cmp al, 'H'
    je .p_bs
    cmp al, '_'
    je .p_crlf
    cmp al, '$'
    je .p_dlr
    cmp al, 'N'
    je .p_drv
    cmp al, 'D'
    je .p_date
    cmp al, 'T'
    je .p_time
    jmp .lp

.p_cwd:
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .lp
    add rdi, rax
    jmp .lp
.p_drv:
    mov rcx, rdi
    mov edx, 260
    call hal_get_cwd64
    jc .lp
    inc rdi
    jmp .lp
.p_gt:
    mov byte [rdi], '>'
    inc rdi
    jmp .lp
.p_lt:
    mov byte [rdi], '<'
    inc rdi
    jmp .lp
.p_bar:
    mov byte [rdi], '|'
    inc rdi
    jmp .lp
.p_eq:
    mov byte [rdi], '='
    inc rdi
    jmp .lp
.p_sp:
    mov byte [rdi], 0x20
    inc rdi
    jmp .lp
.p_esc:
    mov byte [rdi], 0x1B
    inc rdi
    jmp .lp
.p_bs:
    cmp rdi, rbx
    jbe .lp
    dec rdi
    jmp .lp
.p_crlf:
    mov byte [rdi], 0x0D
    mov byte [rdi + 1], 0x0A
    add rdi, 2
    jmp .lp
.p_dlr:
    mov byte [rdi], '$'
    inc rdi
    jmp .lp
.p_date:
    lea rcx, [rel szVarDate64]
    call env_get64
    test rax, rax
    jz .lp
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    jmp .lp
.p_time:
    lea rcx, [rel szVarTime64]
    call env_get64
    test rax, rax
    jz .lp
    mov rcx, rdi
    mov rdx, rax
    call str_copy64
    mov rdi, rax
    jmp .lp

.emit:
    mov rdx, rdi
    sub rdx, rbx
    mov rcx, rbx
    call con_write_out64
.done:
    @epilog_ret
@endproc

;; @proc cmd_is_echo_delim64
;; @abi win64_ms
;; @in CL = candidate delimiter byte immediately following `ECHO`
;; @out EAX = 1 if CL is one of `. ( / : + = [ ] ; `, else 0
;; @err none
;; @clobbers rax
;; @desc Recognizes all 10 authentic cmd.exe / COMMAND.COM `ECHO<delim>` punctuation delimiters.
@proc cmd_is_echo_delim64
    cmp cl, '.'
    je .yes
    cmp cl, '('
    je .yes
    cmp cl, '/'
    je .yes
    cmp cl, ':'
    je .yes
    cmp cl, '+'
    je .yes
    cmp cl, '='
    je .yes
    cmp cl, '['
    je .yes
    cmp cl, ']'
    je .yes
    cmp cl, ';'
    je .yes
    cmp cl, 0x5C
    je .yes
    xor eax, eax
    ret
.yes:
    mov eax, 1
    ret
@endproc

;; @proc exec_simple_line64
;; @abi win64_ms
;; @in RCX = Phase-3/Phase-4 expanded simple command line
;; @out Dispatches drive switch, ECHO<delim>, internal built-in, or external .COM/.EXE/.BAT/.CMD
;; @err Sets g_last_exit_code on error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Core simple-command dispatcher invoked by AST_SIMPLE_CMD and CALL re-expansion.
@proc_frame exec_simple_line64, 40, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
.strip_at:
    cmp byte [rsi], '@'
    jne .after_at
    inc rsi
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    jmp .strip_at

.after_at:
    cmp byte [rsi], 0
    je .done_noop
    cmp byte [rsi], ':'
    je .done_noop

    ; Check bare drive switch `X:`
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'A'
    jb .chk_echo_d
    cmp al, 'Z'
    ja .chk_echo_d
    cmp byte [rsi + 1], ':'
    jne .chk_echo_d
    lea rcx, [rsi + 2]
    call prs_skip_ws64
    cmp byte [rax], 0
    jne .chk_echo_d
    mov cl, byte [rsi]
    call cmd_switch_drive64
    jmp .done

.chk_echo_d:
    ; Check `ECHO<delim>`
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'E'
    jne .parse_verb
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'C'
    jne .parse_verb
    mov al, byte [rsi + 2]
    call ascii_upper_al64
    cmp al, 'H'
    jne .parse_verb
    mov al, byte [rsi + 3]
    call ascii_upper_al64
    cmp al, 'O'
    jne .parse_verb
    mov cl, byte [rsi + 4]
    call cmd_is_echo_delim64
    test eax, eax
    jz .parse_verb
    lea rcx, [rsi + 5]
    call con_print_z64
    call con_print_crlf64
    mov dword [rel g_last_exit_code], 0
    jmp .done

.parse_verb:
    mov rcx, rsi
    call prs_read_word64
    jc .done_noop
    mov rbx, rdx ; raw argument tail cursor (preserves exact leading spaces)
    mov rcx, rax
    call tok_strip_quotes64
    jc .done
    mov rdi, rax ; unquoted verb

    ; Support `CD..`, `CD`, `MD\foo`, `RD\foo`, `DIR/B` (delimited by `/`, ``, or `..`)
    cmp byte [rsi], '"'
    je .try_bi
    xor r12d, r12d
.sc_sl:
    mov al, byte [rdi + r12]
    test al, al
    jz .chk_dotdot
    cmp al, '/'
    je .split_verb_at
    inc r12
    jmp .sc_sl
.chk_dotdot:
    mov al, byte [rdi]
    call ascii_upper_al64
    cmp al, 'C'
    jne .try_bi
    mov al, byte [rdi + 1]
    call ascii_upper_al64
    cmp al, 'D'
    jne .try_bi
    cmp byte [rdi + 2], '.'
    je .split_cd2
    cmp byte [rdi + 2], 0x5C
    je .split_cd2
    jmp .try_bi
.split_cd2:
    mov r12d, 2
.split_verb_at:
    test r12, r12
    jz .try_bi
    mov byte [rdi + r12], 0
    lea rbx, [rsi + r12]

.try_bi:
    mov rcx, rdi
    mov rdx, rbx
    call cmd_try_builtin64
    jnc .done

    ; If we split at `/` or `..` for a candidate built-in (like `DIR/B`) and it wasn't a built-in,
    ; restore the full external path token (e.g. `build/nasm/dos16/HOSTNAME.COM`)!
    test r12, r12
    jz .chk_ext_chain
    mov rcx, rsi
    call prs_read_word64
    jc .done_noop
    mov rbx, rdx
    mov rcx, rax
    call tok_strip_quotes64
    jc .done
    mov rdi, rax

.chk_ext_chain:
    ; Not a built-in -> launch external .COM/.EXE/.BAT/.CMD (or un-CALLed batch chain!)
    cmp dword [rel g_batch_depth], 0
    je .spawn_norm
    mov rcx, rdi
    call resolve_executable64
    jc .bad_cmd
    cmp dl, 1 ; .BAT / .CMD -> chain (replace current batch frame)!
    jne .spawn_norm

    mov eax, dword [rel g_batch_depth]
    dec eax
    imul r12, rax, BATCH_FRAME64_SIZE
    lea rcx, [rel g_batch_stack]
    add r12, rcx
    mov rcx, qword [r12 + BATCH_FRAME64.pBatchPath]
    call perm_free64
    mov rcx, rax ; wait: reload resolved path via resolve_executable64
    mov rcx, rdi
    call resolve_executable64
    jc .bad_cmd
    mov rsi, rax
    mov rcx, rsi
    call str_len64
    lea rcx, [rax + 1]
    call perm_alloc64
    jc .done
    mov qword [r12 + BATCH_FRAME64.pBatchPath], rax
    mov qword [r12 + BATCH_FRAME64.argvPtrs], rax
    mov rcx, rax
    mov rdx, rsi
    call str_copy64
    mov qword [r12 + BATCH_FRAME64.uFileOff], 0
    mov byte [r12 + BATCH_FRAME64.bEncoding], 0
    jmp .done

.spawn_norm:
    mov rcx, rdi
    mov rdx, rbx
    xor r8d, r8d
    call exec_external64
    jnc .done

.bad_cmd:
    lea rcx, [rel szErrBadCmd64]
    call con_print_err_z64
    lea rcx, [rel szCrLf64]
    call con_print_err_z64
    mov dword [rel g_last_exit_code], 9009
    jmp .done

.done_noop:
.done:
    @epilog_ret
@endproc

;; @proc cmd_switch_drive64
;; @abi win64_ms
;; @in CL = drive letter ('A'..'Z' or 'a'..'z')
;; @out Switches process current directory to hidden `=X:` CWD or `X:`
;; @err Prints error and sets g_last_exit_code = 1 on invalid drive
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements `C:` drive switching synchronized with hidden `=C:` environment entries.
@proc_frame cmd_switch_drive64, 56, rbx, rdi
    mov al, cl
    call ascii_upper_al64
    mov bl, al

    mov byte [rsp + 32], '='
    mov byte [rsp + 33], bl
    mov byte [rsp + 34], ':'
    mov byte [rsp + 35], 0
    lea rcx, [rsp + 32]
    call env_get_raw64
    test rax, rax
    jnz .set_dir

    mov byte [rsp + 32], bl
    mov byte [rsp + 33], ':'
    mov byte [rsp + 34], 0x5C
    mov byte [rsp + 35], 0
    lea rax, [rsp + 32]

.set_dir:
    mov rcx, rax
    call hal_set_cwd64
    jc .err

    mov ecx, 1024
    call arena_alloc64
    jc .ok
    mov rdi, rax
    mov rcx, rdi
    mov edx, 1020
    call hal_get_cwd64
    jc .ok
    mov rcx, rdi
    call path_sync_drive_env64
.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
.err:
    mov ecx, eax
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc cmd_try_builtin64
;; @abi win64_ms
;; @in RCX = NUL-terminated verb, RDX = raw argument tail
;; @out CF=0 if handled as built-in; CF=1 if external command
;; @err CF=1 when RCX is not an internal command
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Matches all 47 internal cmd.exe / COMMAND.COM built-in commands (including aliases CHDIR, ERASE, MKDIR, RMDIR, RENAME, LOADHIGH).
@proc_frame cmd_try_builtin64, 40, rbx, rsi
    mov rsi, rcx ; verb
    mov rbx, rdx ; raw tail

    mov rcx, rsi
    lea rdx, [rel szCmdEcho64]
    call str_eq_i64
    test eax, eax
    jnz .b_echo

    mov rcx, rsi
    lea rdx, [rel szCmdSet64]
    call str_eq_i64
    test eax, eax
    jnz .b_set

    mov rcx, rsi
    lea rdx, [rel szCmdCd64]
    call str_eq_i64
    test eax, eax
    jnz .b_cd
    mov rcx, rsi
    lea rdx, [rel szCmdChdir64]
    call str_eq_i64
    test eax, eax
    jnz .b_cd

    mov rcx, rsi
    lea rdx, [rel szCmdMd64]
    call str_eq_i64
    test eax, eax
    jnz .b_md
    mov rcx, rsi
    lea rdx, [rel szCmdMkdir64]
    call str_eq_i64
    test eax, eax
    jnz .b_md

    mov rcx, rsi
    lea rdx, [rel szCmdRd64]
    call str_eq_i64
    test eax, eax
    jnz .b_rd
    mov rcx, rsi
    lea rdx, [rel szCmdRmdir64]
    call str_eq_i64
    test eax, eax
    jnz .b_rd

    mov rcx, rsi
    lea rdx, [rel szCmdDel64]
    call str_eq_i64
    test eax, eax
    jnz .b_del
    mov rcx, rsi
    lea rdx, [rel szCmdErase64]
    call str_eq_i64
    test eax, eax
    jnz .b_del

    mov rcx, rsi
    lea rdx, [rel szCmdCopy64]
    call str_eq_i64
    test eax, eax
    jnz .b_copy

    mov rcx, rsi
    lea rdx, [rel szCmdMove64]
    call str_eq_i64
    test eax, eax
    jnz .b_move

    mov rcx, rsi
    lea rdx, [rel szCmdRen64]
    call str_eq_i64
    test eax, eax
    jnz .b_ren
    mov rcx, rsi
    lea rdx, [rel szCmdRename64]
    call str_eq_i64
    test eax, eax
    jnz .b_ren

    mov rcx, rsi
    lea rdx, [rel szCmdDir64]
    call str_eq_i64
    test eax, eax
    jnz .b_dir

    mov rcx, rsi
    lea rdx, [rel szCmdType64]
    call str_eq_i64
    test eax, eax
    jnz .b_type

    mov rcx, rsi
    lea rdx, [rel szCmdCall64]
    call str_eq_i64
    test eax, eax
    jnz .b_call

    mov rcx, rsi
    lea rdx, [rel szCmdGoto64]
    call str_eq_i64
    test eax, eax
    jnz .b_goto

    mov rcx, rsi
    lea rdx, [rel szCmdExit64]
    call str_eq_i64
    test eax, eax
    jnz .b_exit

    mov rcx, rsi
    lea rdx, [rel szCmdShift64]
    call str_eq_i64
    test eax, eax
    jnz .b_shift

    mov rcx, rsi
    lea rdx, [rel szCmdSetlocal64]
    call str_eq_i64
    test eax, eax
    jnz .b_setlocal

    mov rcx, rsi
    lea rdx, [rel szCmdEndlocal64]
    call str_eq_i64
    test eax, eax
    jnz .b_endlocal

    mov rcx, rsi
    lea rdx, [rel szCmdPushd64]
    call str_eq_i64
    test eax, eax
    jnz .b_pushd

    mov rcx, rsi
    lea rdx, [rel szCmdPopd64]
    call str_eq_i64
    test eax, eax
    jnz .b_popd

    mov rcx, rsi
    lea rdx, [rel szCmdMklink64]
    call str_eq_i64
    test eax, eax
    jnz .b_mklink

    mov rcx, rsi
    lea rdx, [rel szCmdAssoc64]
    call str_eq_i64
    test eax, eax
    jnz .b_assoc

    mov rcx, rsi
    lea rdx, [rel szCmdFtype64]
    call str_eq_i64
    test eax, eax
    jnz .b_ftype

    mov rcx, rsi
    lea rdx, [rel szCmdVer64]
    call str_eq_i64
    test eax, eax
    jnz .b_ver

    mov rcx, rsi
    lea rdx, [rel szCmdVol64]
    call str_eq_i64
    test eax, eax
    jnz .b_vol

    mov rcx, rsi
    lea rdx, [rel szCmdCls64]
    call str_eq_i64
    test eax, eax
    jnz .b_cls

    mov rcx, rsi
    lea rdx, [rel szCmdColor64]
    call str_eq_i64
    test eax, eax
    jnz .b_color

    mov rcx, rsi
    lea rdx, [rel szCmdTitle64]
    call str_eq_i64
    test eax, eax
    jnz .b_title

    mov rcx, rsi
    lea rdx, [rel szCmdChcp64]
    call str_eq_i64
    test eax, eax
    jnz .b_chcp

    mov rcx, rsi
    lea rdx, [rel szCmdPath64]
    call str_eq_i64
    test eax, eax
    jnz .b_path

    mov rcx, rsi
    lea rdx, [rel szCmdDpath64]
    call str_eq_i64
    test eax, eax
    jnz .b_dpath

    mov rcx, rsi
    lea rdx, [rel szCmdPrompt64]
    call str_eq_i64
    test eax, eax
    jnz .b_prompt

    mov rcx, rsi
    lea rdx, [rel szCmdPause64]
    call str_eq_i64
    test eax, eax
    jnz .b_pause

    mov rcx, rsi
    lea rdx, [rel szCmdDate64]
    call str_eq_i64
    test eax, eax
    jnz .b_date

    mov rcx, rsi
    lea rdx, [rel szCmdTime64]
    call str_eq_i64
    test eax, eax
    jnz .b_time

    mov rcx, rsi
    lea rdx, [rel szCmdTruename64]
    call str_eq_i64
    test eax, eax
    jnz .b_truename

    mov rcx, rsi
    lea rdx, [rel szCmdStart64]
    call str_eq_i64
    test eax, eax
    jnz .b_start

    mov rcx, rsi
    lea rdx, [rel szCmdCmd64]
    call str_eq_i64
    test eax, eax
    jnz .b_cmd
    mov rcx, rsi
    lea rdx, [rel szCmdCommand64]
    call str_eq_i64
    test eax, eax
    jnz .b_cmd

    mov rcx, rsi
    lea rdx, [rel szCmdDoskey64]
    call str_eq_i64
    test eax, eax
    jnz .b_doskey

    mov rcx, rsi
    lea rdx, [rel szCmdMode64]
    call str_eq_i64
    test eax, eax
    jnz .b_mode

    mov rcx, rsi
    lea rdx, [rel szCmdBreak64]
    call str_eq_i64
    test eax, eax
    jnz .b_break

    mov rcx, rsi
    lea rdx, [rel szCmdVerify64]
    call str_eq_i64
    test eax, eax
    jnz .b_verify

    mov rcx, rsi
    lea rdx, [rel szCmdKeys64]
    call str_eq_i64
    test eax, eax
    jnz .b_keys

    mov rcx, rsi
    lea rdx, [rel szCmdCtty64]
    call str_eq_i64
    test eax, eax
    jnz .b_ctty

    mov rcx, rsi
    lea rdx, [rel szCmdLh64]
    call str_eq_i64
    test eax, eax
    jnz .b_lh
    mov rcx, rsi
    lea rdx, [rel szCmdLoadhigh64]
    call str_eq_i64
    test eax, eax
    jnz .b_lh

    mov rcx, rsi
    lea rdx, [rel szCmdRem64]
    call str_eq_i64
    test eax, eax
    jnz .b_rem

    stc
    @epilog_ret

.b_echo:
    mov rcx, rbx
    call bi_echo64
    jmp .handled
.b_set:
    mov rcx, rbx
    call bi_set64
    jmp .handled
.b_cd:
    mov rcx, rbx
    call bi_cd64
    jmp .handled
.b_md:
    mov rcx, rbx
    call bi_md64
    jmp .handled
.b_rd:
    mov rcx, rbx
    call bi_rd64
    jmp .handled
.b_del:
    mov rcx, rbx
    call bi_del64
    jmp .handled
.b_copy:
    mov rcx, rbx
    call bi_copy64
    jmp .handled
.b_move:
    mov rcx, rbx
    call bi_move64
    jmp .handled
.b_ren:
    mov rcx, rbx
    call bi_ren64
    jmp .handled
.b_dir:
    mov rcx, rbx
    call bi_dir64
    jmp .handled
.b_type:
    mov rcx, rbx
    call bi_type64
    jmp .handled
.b_call:
    mov rcx, rbx
    call bi_call64
    jmp .handled
.b_goto:
    mov rcx, rbx
    call bi_goto64
    jmp .handled
.b_exit:
    mov rcx, rbx
    call bi_exit64
    jmp .handled
.b_shift:
    mov rcx, rbx
    call bi_shift64
    jmp .handled
.b_setlocal:
    mov rcx, rbx
    call bi_setlocal64
    jmp .handled
.b_endlocal:
    call env_endlocal_pop64
    mov dword [rel g_last_exit_code], 0
    jmp .handled
.b_pushd:
    mov rcx, rbx
    call bi_pushd64
    jmp .handled
.b_popd:
    call bi_popd64
    jmp .handled
.b_mklink:
    mov rcx, rbx
    call bi_mklink64
    jmp .handled
.b_assoc:
    mov rcx, rbx
    call bi_assoc64
    jmp .handled
.b_ftype:
    mov rcx, rbx
    call bi_ftype64
    jmp .handled
.b_ver:
    call bi_ver64
    jmp .handled
.b_vol:
    mov rcx, rbx
    call bi_vol64
    jmp .handled
.b_cls:
    call bi_cls64
    jmp .handled
.b_color:
    mov rcx, rbx
    call bi_color64
    jmp .handled
.b_title:
    mov rcx, rbx
    call bi_title64
    jmp .handled
.b_chcp:
    mov rcx, rbx
    call bi_chcp64
    jmp .handled
.b_path:
    mov rcx, rbx
    lea rdx, [rel szVarPath64]
    call bi_path_var64
    jmp .handled
.b_dpath:
    mov rcx, rbx
    lea rdx, [rel szVarDpath64]
    call bi_path_var64
    jmp .handled
.b_prompt:
    mov rcx, rbx
    call bi_prompt64
    jmp .handled
.b_pause:
    call bi_pause64
    jmp .handled
.b_date:
    mov rcx, rbx
    call bi_date64
    jmp .handled
.b_time:
    mov rcx, rbx
    call bi_time64
    jmp .handled
.b_truename:
    mov rcx, rbx
    call bi_truename64
    jmp .handled
.b_start:
    mov rcx, rbx
    call bi_start64
    jmp .handled
.b_cmd:
    mov rcx, rbx
    call bi_cmd_subshell64
    jmp .handled
.b_doskey:
    mov rcx, rbx
    call bi_doskey64
    jmp .handled
.b_mode:
    mov rcx, rbx
    call bi_mode64
    jmp .handled
.b_break:
    mov rcx, rbx
    lea rdx, [rel szMsgBreak64]
    lea r8, [rel g_break_on]
    call bi_toggle_state64
    jmp .handled
.b_verify:
    mov rcx, rbx
    lea rdx, [rel szMsgVerify64]
    lea r8, [rel g_verify_on]
    call bi_toggle_state64
    jmp .handled
.b_keys:
    mov rcx, rbx
    lea rdx, [rel szMsgKeys64]
    lea r8, [rel g_keys_on]
    call bi_toggle_state64
    jmp .handled
.b_ctty:
    mov dword [rel g_last_exit_code], 0
    jmp .handled
.b_lh:
    mov rcx, rbx
    call exec_simple_line64
    jmp .handled
.b_rem:

.handled:
    clc
    @epilog_ret
@endproc

;; @proc bi_echo64
;; @abi win64_ms
;; @in RCX = raw argument tail (including leading single space if present)
;; @out Prints ECHO status, toggles ON/OFF, or writes message + CRLF
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements ECHO [ON | OFF | message] preserving exact leading whitespace after single separator.
@proc_frame bi_echo64, 40, rbx, rsi
    mov rsi, rcx
    mov rbx, rcx
    cmp byte [rbx], 0x20
    je .step1
    cmp byte [rbx], 0x09
    jne .chk_empty
.step1:
    inc rbx

.chk_empty:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .show_state

    mov rcx, rsi
    call prs_read_word64
    jc .print_msg
    mov rcx, rdx
    mov rsi, rax
    call prs_skip_ws64
    cmp byte [rax], 0
    jne .print_msg

    mov rcx, rsi
    lea rdx, [rel szOn64]
    call str_eq_i64
    test eax, eax
    jnz .turn_on

    mov rcx, rsi
    lea rdx, [rel szOff64]
    call str_eq_i64
    test eax, eax
    jnz .turn_off
    jmp .print_msg

.turn_on:
    mov byte [rel g_echo_on], 1
    jmp .done_zero
.turn_off:
    mov byte [rel g_echo_on], 0
    jmp .done_zero

.show_state:
    lea rcx, [rel szEchoIsOff64]
    cmp byte [rel g_echo_on], 0
    je .pr_st
    lea rcx, [rel szEchoIsOn64]
.pr_st:
    call con_print_z64
    call con_print_crlf64
    jmp .done_zero

.print_msg:
    mov rcx, rbx
    call con_print_z64
    call con_print_crlf64

.done_zero:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_set64
;; @abi win64_ms
;; @in RCX = raw argument tail
;; @out Lists environment, sets/deletes variable, evaluates SET /A, or prompts SET /P
;; @err Sets g_last_exit_code = 1 on missing prefix or SET /A error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements SET, SET prefix, SET "VAR=val", SET /A expr, and SET /P VAR=prompt.
@proc_frame bi_set64, 56, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .list_all

    cmp byte [rsi], '/'
    jne .chk_quoted
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'A'
    je .do_arith
    cmp al, 'P'
    je .do_prompt

.chk_quoted:
    cmp byte [rsi], '"'
    jne .plain_assign
    inc rsi
    mov rcx, rsi
    call arena_strdup64
    jc .err_1
    mov rsi, rax
    mov rcx, rsi
    call str_len64
    mov rdx, rax
.find_last_q:
    test rdx, rdx
    jz .plain_assign
    dec rdx
    cmp byte [rsi + rdx], '"'
    jne .find_last_q
    mov byte [rsi + rdx], 0

.plain_assign:
    mov rcx, rsi
    call arena_strdup64
    jc .err_1
    mov rsi, rax
    xor ecx, ecx
.eq_sc:
    mov al, byte [rsi + rcx]
    test al, al
    jz .prefix_query
    cmp al, '='
    je .has_eq
    inc rcx
    jmp .eq_sc

.has_eq:
    test rcx, rcx
    jz .err_syn
    mov byte [rsi + rcx], 0
    lea rdx, [rsi + rcx + 1]
    mov rcx, rsi
    call env_set64
    jc .err_1
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.prefix_query:
    mov rbx, rsi
    mov rcx, rbx
    call str_len64
    mov r12, rax ; prefix len
    mov rsi, qword [rel g_env_buf]
    xor edi, edi ; match count
.pf_lp:
    cmp byte [rsi], 0
    je .pf_done
    cmp byte [rsi], '='
    je .pf_nx
    mov rcx, rsi
    mov rdx, rbx
    mov r8, r12
    call str_istarts_with64
    test eax, eax
    jz .pf_nx
    mov rcx, rsi
    call con_print_z64
    call con_print_crlf64
    inc edi
.pf_nx:
    mov rcx, rsi
    call str_len64
    lea rsi, [rsi + rax + 1]
    jmp .pf_lp
.pf_done:
    test edi, edi
    jz .pf_miss
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
.pf_miss:
    lea rcx, [rel szEnvNotDef64]
    call con_print_err_z64
    jmp .err_1

.list_all:
    mov rsi, qword [rel g_env_buf]
.la_lp:
    cmp byte [rsi], 0
    je .ok_0
    cmp byte [rsi], '='
    je .la_nx
    mov rcx, rsi
    call con_print_z64
    call con_print_crlf64
.la_nx:
    mov rcx, rsi
    call str_len64
    lea rsi, [rsi + rax + 1]
    jmp .la_lp

.do_arith:
    add rsi, 2
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    mov rcx, rsi
    call tok_strip_quotes64
    jc .err_1
    mov rcx, rax
    call arith_eval64
    jc .sa_err
    mov ebx, eax
    cmp dword [rel g_batch_depth], 0
    jne .ok_0
    mov ecx, ebx
    lea rdx, [rsp + 32]
    call fmt_i32_dec64
    mov edx, eax
    lea rcx, [rsp + 32]
    call con_write_out64
    jmp .ok_0

.sa_err:
    mov ecx, eax
    call con_report_err64
    jmp .err_1

.do_prompt:
    add rsi, 2
    mov rcx, rsi
    call prs_skip_ws64
    mov rcx, rax
    call tok_strip_quotes64
    jc .err_1
    mov rsi, rax
    xor ecx, ecx
.sp_eq:
    mov al, byte [rsi + rcx]
    test al, al
    jz .err_syn
    cmp al, '='
    je .sp_got
    inc rcx
    jmp .sp_eq
.sp_got:
    mov byte [rsi + rcx], 0
    mov rbx, rsi ; varName
    lea rcx, [rsi + rcx + 1]
    cmp byte [rcx], 0
    je .sp_read
    call con_print_z64
.sp_read:
    mov ecx, 2048
    call arena_alloc64
    jc .err_1
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    xor r8d, r8d
    call con_read_line64
    jc .err_1
    cmp byte [rdi], 0
    je .err_1
    mov rcx, rbx
    mov rdx, rdi
    call env_set64
.ok_0:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.err_syn:
    mov ecx, ERR_SYNTAX
    call con_report_err64
.err_1:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_call64
;; @abi win64_ms
;; @in RCX = raw argument tail (`:label [args]`, `batch.cmd [args]`, or `cmd [args]`)
;; @out Executes subroutine, nested batch frame, or re-expanded command line
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements CALL :label, CALL file.bat, and CALL re-expansion (`CALL ECHO %%VAR%%`).
@proc_frame bi_call64, 40, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .done

    cmp byte [rsi], ':'
    jne .not_lbl

    cmp dword [rel g_batch_depth], 0
    je .err_syn
    inc rsi ; skip ':'
    mov rcx, rsi
    call prs_read_word64
    jc .err_syn
    mov rbx, rdx ; args tail
    mov rsi, rax ; label name

    lea rcx, [rel g_goto_label]
    mov rdx, rsi
    call str_copy64

    mov eax, dword [rel g_batch_depth]
    dec eax
    imul r12, rax, BATCH_FRAME64_SIZE
    lea rcx, [rel g_batch_stack]
    add r12, rcx
    mov rcx, qword [r12 + BATCH_FRAME64.pBatchPath]
    mov rdx, rbx
    mov r8b, 1 ; bIsCallSub = 1
    call batch_run_file64
    jc .lbl_err
    mov dword [rel g_last_exit_code], eax
    jmp .done

.lbl_err:
    mov ecx, ERR_LABEL_NOT_FOUND
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    jmp .done

.not_lbl:
    ; Re-expand Phase 1 `%VAR%` once (`CALL SET /P ...`, `CALL ECHO %%FOO%%`)
    mov rcx, rsi
    xor edx, edx ; bInBatch = 0 so %% -> % works and %VAR% re-expands!
    call expand_phase1_64
    jc .done
    mov rsi, rax

    mov rcx, rsi
    call prs_read_word64
    jc .done
    mov rbx, rdx ; raw args tail
    mov rcx, rax
    call tok_strip_quotes64
    jc .done
    mov rdi, rax

    mov rcx, rdi
    call resolve_executable64
    jc .reexec_cmd
    cmp dl, 1
    jne .reexec_cmd

    mov rcx, rax
    mov rdx, rbx
    xor r8d, r8d
    call batch_run_file64
    jc .done
    mov dword [rel g_last_exit_code], eax
    jmp .done

.reexec_cmd:
    mov rcx, rsi
    call exec_simple_line64
    jmp .done

.err_syn:
    mov ecx, ERR_SYNTAX
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
.done:
    @epilog_ret
@endproc

;; @proc bi_goto64
;; @abi win64_ms
;; @in RCX = `[:]label` or `:EOF`
;; @out Latches g_goto_pending + g_goto_label for active batch frame
;; @err Reports syntax error if empty
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements GOTO [:]label and GOTO :EOF (`CMDEXTVERSION=2`).
@proc_frame bi_goto64, 40, rsi, rdi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .err
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rsi, rax

    ; GOTO :EOF (with leading ':') exits current batch script/subroutine when Extensions ON!
    cmp byte [rsi], ':'
    jne .copy_lbl
    lea rcx, [rsi + 1]
    lea rdx, [rel szEofLbl64]
    call str_eq_i64
    test eax, eax
    jnz .keep_colon
    inc rsi
.keep_colon:
.copy_lbl:
    lea rcx, [rel g_goto_label]
    mov rdx, rsi
    call str_copy64
    mov byte [rel g_goto_pending], 1
    @epilog_ret

.err:
    mov ecx, ERR_SYNTAX
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_exit64
;; @abi win64_ms
;; @in RCX = `[/B] [exitCode]`
;; @out Sets g_batch_ret_flag (for /B in batch) or g_exit_flag (process exit) + g_last_exit_code
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements EXIT [exitCode] and EXIT /B [exitCode].
@proc_frame bi_exit64, 40, rbx, rsi
    call prs_skip_ws64
    mov rsi, rax
    xor ebx, ebx ; /B flag

    cmp byte [rsi], '/'
    jne .parse_code
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'B'
    jne .parse_code
    mov ebx, 1
    add rsi, 2
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax

.parse_code:
    cmp byte [rsi], 0
    je .apply_flags
    mov rcx, rsi
    call exp_parse_signed_int64
    mov dword [rel g_last_exit_code], eax

.apply_flags:
    test ebx, ebx
    jz .full_exit
    cmp dword [rel g_batch_depth], 0
    je .full_exit
    mov byte [rel g_batch_ret_flag], 1
    @epilog_ret

.full_exit:
    mov byte [rel g_exit_flag], 1
    @epilog_ret
@endproc

;; @proc bi_shift64
;; @abi win64_ms
;; @in RCX = optional `/0`..`/8` start parameter index
;; @out Shifts active BATCH_FRAME64 argvPtrs[n..8] left and refills %9 from remaining pArgsRaw
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements SHIFT [/n] (`n = 0..8`).
@proc_frame bi_shift64, 40, rbx, rsi
    cmp dword [rel g_batch_depth], 0
    je .done

    call prs_skip_ws64
    mov rsi, rax
    mov ecx, 1 ; default shift starts at %1 unless /0..8 given
    cmp byte [rsi], '/'
    jne .do_sh
    movzx eax, byte [rsi + 1]
    cmp al, '0'
    jb .do_sh
    cmp al, '8'
    ja .do_sh
    sub eax, '0'
    mov ecx, eax

.do_sh:
    mov eax, dword [rel g_batch_depth]
    dec eax
    imul rbx, rax, BATCH_FRAME64_SIZE
    lea rdx, [rel g_batch_stack]
    add rbx, rdx

    mov rax, qword [rbx + BATCH_FRAME64.argvPtrs + rcx * 8]
    mov rsi, rcx
    mov rcx, rax
    call perm_free64
    mov rcx, rsi

.sh_lp:
    cmp ecx, 9
    jae .clear_9
    mov rax, qword [rbx + BATCH_FRAME64.argvPtrs + rcx * 8 + 8]
    mov qword [rbx + BATCH_FRAME64.argvPtrs + rcx * 8], rax
    inc ecx
    jmp .sh_lp
.clear_9:
    mov qword [rbx + BATCH_FRAME64.argvPtrs + 9 * 8], 0
    mov dword [rel g_last_exit_code], 0
.done:
    @epilog_ret
@endproc

;; @proc bi_setlocal64
;; @abi win64_ms
;; @in RCX = `[EnableDelayedExpansion | DisableDelayedExpansion] [EnableExtensions | DisableExtensions]`
;; @out Pushes SETLOCAL frame and toggles delayed expansion / extensions flags
;; @err Sets g_last_exit_code = 1 on stack overflow
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements SETLOCAL with EnableDelayedExpansion and EnableExtensions options.
@proc_frame bi_setlocal64, 40, rsi, rdi
    mov rsi, rcx
    call env_setlocal_push64
    jc .err

.opt_lp:
    mov rcx, rsi
    call prs_read_word64
    jc .ok
    mov rdi, rdx ; next cursor
    mov rsi, rax
    mov rcx, rsi
    lea rdx, [rel szOptEnDelay64]
    call str_eq_i64
    test eax, eax
    jnz .en_del
    mov rcx, rsi
    lea rdx, [rel szOptDisDelay64]
    call str_eq_i64
    test eax, eax
    jnz .dis_del
    mov rcx, rsi
    lea rdx, [rel szOptEnExt64]
    call str_eq_i64
    test eax, eax
    jnz .en_ext
    mov rcx, rsi
    lea rdx, [rel szOptDisExt64]
    call str_eq_i64
    test eax, eax
    jnz .dis_ext
    mov rsi, rdi
    jmp .opt_lp

.en_del:
    mov byte [rel g_delayed_exp], 1
    mov rsi, rdi
    jmp .opt_lp
.dis_del:
    mov byte [rel g_delayed_exp], 0
    mov rsi, rdi
    jmp .opt_lp
.en_ext:
    mov byte [rel g_extensions_on], 1
    mov rsi, rdi
    jmp .opt_lp
.dis_ext:
    mov byte [rel g_extensions_on], 0
    mov rsi, rdi
    jmp .opt_lp

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_ver64
;; @abi win64_ms
;; @in none
;; @out Prints authentic 64-bit OS version banner (Windows NT major.minor.build x86-64 PE32+)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Displays WINCMD64 + authentic OS version queried via RtlGetVersion / PEB.
@proc_frame bi_ver64, 72, rsi, rdi
    lea rdi, [rsp + 32]
    call con_print_crlf64

    lea rcx, [rel szVerBannerNt1_64]
    call con_print_z64

    mov ecx, dword [rel g_os_major]
    mov rdx, rdi
    call fmt_u64_plain64
    mov edx, eax
    mov rcx, rdi
    call con_write_out64

    lea rcx, [rel szDot64]
    mov edx, 1
    call con_write_out64

    mov ecx, dword [rel g_os_minor]
    mov rdx, rdi
    call fmt_u64_plain64
    mov edx, eax
    mov rcx, rdi
    call con_write_out64

    lea rcx, [rel szDot64]
    mov edx, 1
    call con_write_out64

    mov ecx, dword [rel g_os_build]
    mov rdx, rdi
    call fmt_u64_plain64
    mov edx, eax
    mov rcx, rdi
    call con_write_out64

    lea rcx, [rel szVerBannerNt2_64]
    call con_print_z64

    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_cls64
;; @abi win64_ms
;; @in none
;; @out Clears active Win64 console screen buffer via FillConsoleOutputCharacterW/Attribute + SetConsoleCursorPosition
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements CLS for 64-bit Windows consoles and ConPTY sessions.
@proc_frame bi_cls64, 80, rbx, rsi, rdi
    lea rbx, [rsp + 48] ; CONSOLE_SCREEN_BUFFER_INFO (22B) + written (4B at +24)

    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rsi, rax
    mov rcx, rsi
    mov rdx, rbx
    call qword [rel __imp_GetConsoleScreenBufferInfo]
    test eax, eax
    jz .done

    movzx eax, word [rbx]
    movzx edi, word [rbx + 2]
    imul edi, eax ; cellCount

    mov rcx, rsi
    mov edx, 0x20
    mov r8d, edi
    xor r9d, r9d
    lea rax, [rbx + 24]
    mov qword [rsp + 32], rax
    call qword [rel __imp_FillConsoleOutputCharacterW]

    mov rcx, rsi
    movzx edx, word [rbx + 8] ; wAttributes
    mov r8d, edi
    xor r9d, r9d
    lea rax, [rbx + 24]
    mov qword [rsp + 32], rax
    call qword [rel __imp_FillConsoleOutputAttribute]

    mov rcx, rsi
    xor edx, edx
    call qword [rel __imp_SetConsoleCursorPosition]

.done:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_color64
;; @abi win64_ms
;; @in RCX = optional 1- or 2-digit hex attribute (`bf`)
;; @out Sets console foreground/background attribute (rejecting `fg == bg`!) or restores startup attribute
;; @err Sets g_last_exit_code = 1 when fg == bg or invalid hex digit
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements COLOR [attr] matching cmd.exe `fg != bg` rule.
@proc_frame bi_color64, 40, rbx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    jne .parse_hex
    movzx ebx, word [rel g_default_attr]
    jmp .apply

.parse_hex:
    xor ebx, ebx
    xor r8d, r8d ; digit count
.hx_lp:
    mov al, byte [rsi]
    test al, al
    jz .hx_done
    cmp al, 0x20
    je .hx_done
    cmp al, 0x09
    je .hx_done
    inc rsi
    cmp r8d, 2
    jae .err
    call ascii_upper_al64
    cmp al, '0'
    jb .err
    cmp al, '9'
    jbe .dig
    cmp al, 'A'
    jb .err
    cmp al, 'F'
    ja .err
    sub al, 'A' - 10
    jmp .acc
.dig:
    sub al, '0'
.acc:
    shl ebx, 4
    movzx eax, al
    or ebx, eax
    inc r8d
    jmp .hx_lp

.hx_done:
    cmp r8d, 2
    jb .apply
    mov eax, ebx
    shr eax, 4
    mov edx, ebx
    and edx, 0x0F
    cmp eax, edx
    je .err

.apply:
    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov rcx, rax
    mov edx, ebx
    call qword [rel __imp_SetConsoleTextAttribute]
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_title64
;; @abi win64_ms
;; @in RCX = window title text
;; @out Updates console window title via SetConsoleTitleW
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements TITLE [string].
@proc_frame bi_title64, 32, rsi
    mov rsi, rcx
    cmp byte [rsi], 0x20
    jne .go
    inc rsi
.go:
    mov rcx, rsi
    call wtf8_to_utf16le64
    jc .done
    mov rcx, rax
    call qword [rel __imp_SetConsoleTitleW]
.done:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_chcp64
;; @abi win64_ms
;; @in RCX = optional codepage number (e.g. 437, 936, 65001)
;; @out Queries or updates active console codepage + g_active_cp
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements CHCP [nnn] supporting UTF-8 (65001) and OEM/DBCS codepages.
@proc_frame bi_chcp64, 72, rbx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .show_cp

    mov rcx, rsi
    call exp_parse_signed_int64
    test eax, eax
    jle .err
    mov ebx, eax
    mov ecx, ebx
    call qword [rel __imp_SetConsoleCP]
    mov ecx, ebx
    call qword [rel __imp_SetConsoleOutputCP]
    test eax, eax
    jz .err
    mov dword [rel g_active_cp], ebx

.show_cp:
    lea rcx, [rel szActiveCp64]
    call con_print_z64
    mov ecx, dword [rel g_active_cp]
    lea rdx, [rsp + 32]
    call fmt_u64_plain64
    mov edx, eax
    lea rcx, [rsp + 32]
    call con_write_out64
    call con_print_crlf64
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.err:
    mov ecx, ERR_INVALID_NUMBER
    call con_report_err64
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_path_var64
;; @abi win64_ms
;; @in RCX = raw argument tail, RDX = variable name (`PATH` or `DPATH`)
;; @out Displays `VAR=value` (`VAR=(null)` if unset), clears on `;`, or updates `%VAR%`
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements both PATH and DPATH built-in commands.
@proc_frame bi_path_var64, 32, rbx, rsi, rdi
    mov rsi, rcx
    mov rbx, rdx ; var name

    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .show

    cmp byte [rsi], ';'
    jne .chk_eq
    lea rcx, [rsi + 1]
    call prs_skip_ws64
    cmp byte [rax], 0
    jne .set_it
    mov rcx, rbx
    xor edx, edx
    call env_set64
    jmp .ok

.chk_eq:
    cmp byte [rsi], '='
    jne .set_it
    inc rsi
.set_it:
    mov rcx, rbx
    mov rdx, rsi
    call env_set64
    jmp .ok

.show:
    mov rcx, rbx
    call con_print_z64
    lea rcx, [rel szEqSign64]
    mov edx, 1
    call con_write_out64
    mov rcx, rbx
    call env_get64
    test rax, rax
    jnz .pr_val
    lea rax, [rel szNullParen64]
.pr_val:
    mov rcx, rax
    call con_print_z64
    call con_print_crlf64
.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_prompt64
;; @abi win64_ms
;; @in RCX = optional prompt string
;; @out Sets or resets `%PROMPT%`
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements PROMPT [text].
@proc_frame bi_prompt64, 32, rsi
    call prs_skip_ws64
    mov rsi, rax
    lea rcx, [rel szVarPrompt64]
    mov rdx, rsi
    call env_set64
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_pause64
;; @abi win64_ms
;; @in none
;; @out Prints `Press any key to continue . . .` and waits for console/stdin input
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements PAUSE.
@proc_frame bi_pause64, 32, rdi
    lea rcx, [rel szPauseMsg64]
    call con_print_z64
    mov ecx, 64
    call arena_alloc64
    jc .done
    mov rdi, rax
    mov rcx, rdi
    mov edx, 60
    xor r8d, r8d
    call con_read_line64
    call con_print_crlf64
.done:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_toggle_state64
;; @abi win64_ms
;; @in RCX = raw tail, RDX = status prefix (`BREAK is `, `VERIFY is `, `KEYS is `), R8 = byte state ptr
;; @out Reports or updates ON/OFF flag
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Shared implementation of BREAK, VERIFY, and KEYS compatibility built-ins.
@proc_frame bi_toggle_state64, 32, rbx, rsi, rdi
    mov rsi, rcx
    mov rdi, rdx
    mov rbx, r8

    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .show
    mov rcx, rsi
    call prs_read_word64
    jc .show
    mov rsi, rax
    mov rcx, rsi
    lea rdx, [rel szOn64]
    call str_eq_i64
    test eax, eax
    jnz .set_on
    mov rcx, rsi
    lea rdx, [rel szOff64]
    call str_eq_i64
    test eax, eax
    jnz .set_off
    jmp .ok
.set_on:
    mov byte [rbx], 1
    jmp .ok
.set_off:
    mov byte [rbx], 0
    jmp .ok
.show:
    mov rcx, rdi
    call con_print_z64
    lea rcx, [rel szOffWord64]
    cmp byte [rbx], 0
    je .pr
    lea rcx, [rel szOnWord64]
.pr:
    call con_print_z64
    call con_print_crlf64
.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_assoc64
;; @abi win64_ms
;; @in RCX = `[.ext[=[fileType]]]`
;; @out Queries or updates HKCU\Software\Classes\<ext> / HKCR\<ext> via 64-bit ADVAPI32 Registry APIs
;; @err Sets g_last_exit_code = 2 when extension association is not found
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements ASSOC backed by real 64-bit Windows Registry (`HKCU\Software\Classes` + `HKCR`).
@proc_frame bi_assoc64, 40, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .done_ok

    mov rcx, rsi
    call arena_strdup64
    jc .err
    mov rsi, rax

    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rdi, rax ; subkey "Software\Classes\.ext"
    mov rcx, rdi
    lea rdx, [rel szRegSoftClasses64]
    call str_copy64
    mov rbx, rax ; append point

    xor ecx, ecx
.eq_lp:
    mov al, byte [rsi + rcx]
    test al, al
    jz .query_ext
    cmp al, '='
    je .modify_ext
    inc rcx
    jmp .eq_lp

.modify_ext:
    mov byte [rsi + rcx], 0
    lea r12, [rsi + rcx + 1] ; fileType value
    mov rcx, rbx
    mov rdx, rsi
    call str_copy64

    mov rcx, HKEY_CURRENT_USER
    mov rdx, rdi
    xor r8d, r8d ; default value
    mov r9, r12
    call hal_reg_set_str64
    jc .err

    cmp byte [r12], 0
    je .done_ok
    mov rcx, rsi
    call con_print_z64
    lea rcx, [rel szEqSign64]
    mov edx, 1
    call con_write_out64
    mov rcx, r12
    call con_print_z64
    call con_print_crlf64
    jmp .done_ok

.query_ext:
    mov rcx, rbx
    mov rdx, rsi
    call str_copy64

    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov r12, rax ; outBuf

    mov rcx, HKEY_CURRENT_USER
    mov rdx, rdi
    xor r8d, r8d
    mov r9, r12
    call hal_reg_query_str64
    jnc .got_assoc

    mov rcx, HKEY_CLASSES_ROOT
    mov rdx, rsi
    xor r8d, r8d
    mov r9, r12
    call hal_reg_query_str64
    jc .not_found

.got_assoc:
    cmp byte [r12], 0
    je .not_found
    mov rcx, rsi
    call con_print_z64
    lea rcx, [rel szEqSign64]
    mov edx, 1
    call con_write_out64
    mov rcx, r12
    call con_print_z64
    call con_print_crlf64
.done_ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.not_found:
    lea rcx, [rel szAssocNotFound64]
    call con_print_err_z64
    mov dword [rel g_last_exit_code], 2
    @epilog_ret
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_ftype64
;; @abi win64_ms
;; @in RCX = `[fileType[=[openCommandString]]]`
;; @out Queries or updates `<fileType>\Shell\Open\Command` in 64-bit Windows Registry
;; @err Sets g_last_exit_code = 2 when fileType is not found
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements FTYPE backed by real 64-bit Windows Registry (`HKCU\Software\Classes` + `HKCR`).
@proc_frame bi_ftype64, 32, rbx, rsi, rdi, r12, r13
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .done_ok

    mov rcx, rsi
    call arena_strdup64
    jc .err
    mov rsi, rax

    xor ecx, ecx
.eq_lp:
    mov al, byte [rsi + rcx]
    test al, al
    jz .query_ft
    cmp al, '='
    je .modify_ft
    inc rcx
    jmp .eq_lp

.modify_ft:
    mov byte [rsi + rcx], 0
    lea r12, [rsi + rcx + 1]

    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov rcx, rdi
    lea rdx, [rel szRegSoftClasses64]
    call str_copy64
    mov rcx, rax
    mov rdx, rsi
    call str_copy64
    mov rcx, rax
    lea rdx, [rel szRegShellOpenCmd64]
    call str_copy64

    mov rcx, HKEY_CURRENT_USER
    mov rdx, rdi
    xor r8d, r8d
    mov r9, r12
    call hal_reg_set_str64
    jc .err

    cmp byte [r12], 0
    je .done_ok
    mov rcx, rsi
    call con_print_z64
    lea rcx, [rel szEqSign64]
    mov edx, 1
    call con_write_out64
    mov rcx, r12
    call con_print_z64
    call con_print_crlf64
    jmp .done_ok

.query_ft:
    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov rcx, rdi
    lea rdx, [rel szRegSoftClasses64]
    call str_copy64
    mov rcx, rax
    mov rdx, rsi
    call str_copy64
    mov rcx, rax
    lea rdx, [rel szRegShellOpenCmd64]
    call str_copy64

    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov r13, rax ; outBuf

    mov rcx, HKEY_CURRENT_USER
    mov rdx, rdi
    xor r8d, r8d
    mov r9, r13
    call hal_reg_query_str64
    jnc .got_ft

    mov ecx, 2048
    call arena_alloc64
    jc .not_found
    mov rbx, rax
    mov rcx, rbx
    mov rdx, rsi
    call str_copy64
    mov rcx, rax
    lea rdx, [rel szRegShellOpenCmd64]
    call str_copy64

    mov rcx, HKEY_CLASSES_ROOT
    mov rdx, rbx
    xor r8d, r8d
    mov r9, r13
    call hal_reg_query_str64
    jc .not_found

.got_ft:
    cmp byte [r13], 0
    je .not_found
    mov rcx, rsi
    call con_print_z64
    lea rcx, [rel szEqSign64]
    mov edx, 1
    call con_write_out64
    mov rcx, r13
    call con_print_z64
    call con_print_crlf64
.done_ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.not_found:
    lea rcx, [rel szFtypeNotFound64]
    call con_print_err_z64
    mov dword [rel g_last_exit_code], 2
    @epilog_ret
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_doskey64
;; @abi win64_ms
;; @in RCX = `/HISTORY`, `/REINSTALL`, `/MACROS`, or `NAME=BODY`
;; @out Prints command history or manages DOSKEY macros
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements DOSKEY /HISTORY, /REINSTALL, /MACROS, and macro registration.
@proc_frame bi_doskey64, 40, rbx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .ok

    cmp byte [rsi], '/'
    jne .ok
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'H'
    je .show_hist
    cmp al, 'R'
    je .clear_hist
    jmp .ok

.show_hist:
    xor ebx, ebx
.h_lp:
    cmp ebx, dword [rel g_hist_count]
    jae .ok
    lea rcx, [rel g_hist_ptrs]
    mov rcx, qword [rcx + rbx * 8]
    call con_print_z64
    call con_print_crlf64
    inc ebx
    jmp .h_lp

.clear_hist:
    xor ebx, ebx
.c_lp:
    cmp ebx, dword [rel g_hist_count]
    jae .c_done
    lea rdx, [rel g_hist_ptrs]
    mov rcx, qword [rdx + rbx * 8]
    mov qword [rdx + rbx * 8], 0
    call perm_free64
    inc ebx
    jmp .c_lp
.c_done:
    mov dword [rel g_hist_count], 0

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc
