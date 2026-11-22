;; @file cmds32_a.inc
;; @isa 386
;; @desc IA-32 Internal Command Dispatcher & Built-Ins Part A (ECHO, SET, CALL, ASSOC, FTYPE, COLOR, CHCP, PROMPT, etc.).

@cpu 386
@format inc

;; @proc cmd_print_prompt32
;; @abi win32
;; @in none
;; @out Expands %PROMPT% ($P, $G, $L, $B, $Q, $S, $E, $H, $_, $D, $T, $N, $V, $$) and writes to stdout
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Renders the interactive or batch-echo command prompt from %PROMPT% (default $P$G).
@proc cmd_print_prompt32
    push ebx
    push esi
    push edi

    mov esi, addr(szVarPrompt)
    call env_get32
    test eax, eax
    jnz .got_p
    mov eax, addr(szDefPrompt32)
.got_p:
    mov esi, eax
    mov ecx, 4096
    call arena_alloc32
    jc .done
    mov edi, eax
    mov ebx, eax ; out base

.lp:
    mov al, byte [esi]
    test al, al
    jz .emit
    cmp al, '$'
    je .esc
    mov byte [edi], al
    inc esi
    inc edi
    jmp .lp

.esc:
    inc esi
    mov al, byte [esi]
    test al, al
    jz .emit
    inc esi
    call ascii_upper_al
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
    mov ecx, 2040
    call hal_get_cwd32
    jc .lp
    add edi, eax
    jmp .lp
.p_drv:
    push edi
    mov ecx, 260
    call hal_get_cwd32
    pop edi
    jc .lp
    inc edi
    jmp .lp
.p_gt:
    mov byte [edi], '>'
    inc edi
    jmp .lp
.p_lt:
    mov byte [edi], '<'
    inc edi
    jmp .lp
.p_bar:
    mov byte [edi], '|'
    inc edi
    jmp .lp
.p_eq:
    mov byte [edi], '='
    inc edi
    jmp .lp
.p_sp:
    mov byte [edi], ' '
    inc edi
    jmp .lp
.p_esc:
    mov byte [edi], 0x1B
    inc edi
    jmp .lp
.p_bs:
    mov byte [edi], 0x08
    inc edi
    jmp .lp
.p_dlr:
    mov byte [edi], '$'
    inc edi
    jmp .lp
.p_crlf:
    mov word [edi], 0x0A0D
    add edi, 2
    jmp .lp
.p_date:
    push esi
    mov esi, addr(szDynDate)
    call env_get32
    test eax, eax
    jz .pop_s
    mov esi, eax
    call str_copy32
    mov edi, eax
.pop_s:
    pop esi
    jmp .lp
.p_time:
    push esi
    mov esi, addr(szDynTime)
    call env_get32
    test eax, eax
    jz .pop_s
    mov esi, eax
    call str_copy32
    mov edi, eax
    pop esi
    jmp .lp

.emit:
    mov byte [edi], 0
    mov ecx, edi
    sub ecx, ebx
    mov esi, ebx
    call con_write_out32

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc cmd_is_echo_delim32
;; @abi win32
;; @in AL = character immediately after "ECHO"
;; @out CF=0 if valid ECHO<delim> character (., (, /, :, +, =, [, ], ;, \), CF=1 otherwise
;; @err CF=1 if not an ECHO delimiter
;; @clobbers none
;; @desc Checks all 10 authentic cmd.exe / COMMAND.COM glued ECHO<delim> punctuation characters.
@proc cmd_is_echo_delim32
    cmp al, '.'
    je .yes
    cmp al, '('
    je .yes
    cmp al, '/'
    je .yes
    cmp al, ':'
    je .yes
    cmp al, '+'
    je .yes
    cmp al, '='
    je .yes
    cmp al, '['
    je .yes
    cmp al, ']'
    je .yes
    cmp al, ';'
    je .yes
    cmp al, 0x5C ; ''
    je .yes
    stc
    ret
.yes:
    clc
    ret
@endproc

;; @proc exec_simple_line32
;; @abi win32
;; @in ESI = Phase-4-expanded simple command line (redirections already applied)
;; @out Updates g_last_exit_code
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Dispatches bare X: drive changes, ECHO<delim>, internal built-ins, DOSKEY macros, or external programs.
@proc exec_simple_line32
    push ebx
    push esi
    push edi
    push ebp

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .done_noop

    ; Check bare drive change `X:`
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'A'
    jb .chk_echo_glued
    cmp al, 'Z'
    ja .chk_echo_glued
    cmp byte [esi + 1], ':'
    jne .chk_echo_glued
    push esi
    lea esi, [esi + 2]
    call prs_skip_ws32
    cmp byte [eax], 0
    pop esi
    jne .chk_echo_glued
    call cmd_switch_drive32
    jmp .done

.chk_echo_glued:
    ; Check ECHO<delim> (e.g. `ECHO.`, `ECHO(hello`) BEFORE generic word splitting!
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'E'
    jne .split_verb
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'C'
    jne .split_verb
    mov al, byte [esi + 2]
    call ascii_upper_al
    cmp al, 'H'
    jne .split_verb
    mov al, byte [esi + 3]
    call ascii_upper_al
    cmp al, 'O'
    jne .split_verb
    mov al, byte [esi + 4]
    call cmd_is_echo_delim32
    jc .split_verb
    lea esi, [esi + 5]
    call con_print_z32
    call con_print_crlf32
    ; Note: ECHO in cmd.exe does NOT reset ERRORLEVEL on success!
    jmp .done

.split_verb:
    ; Extract verb up to whitespace, '/', ',', ';', '=', '(' (unless quoted executable path `"C:\foo.exe" args`!)
    cmp byte [esi], '"'
    je .external_quoted

    mov ebx, esi
.verb_sc:
    mov al, byte [ebx]
    test al, al
    jz .got_verb
    cmp al, 0x20
    je .got_verb
    cmp al, 0x09
    je .got_verb
    cmp al, '/'
    je .got_verb_slash
    cmp al, '('
    je .got_verb
    cmp al, ','
    je .got_verb
    cmp al, ';'
    je .got_verb
    cmp al, '='
    je .got_verb
    inc ebx
    jmp .verb_sc

.got_verb_slash:
    ; Distinguish `CD/D`, `DIR/B`, `RD/S` (verb has no '' or ':') from relative path `dir/prog.exe`!
    ; If verb matches an internal command, treat '/' as switch start; otherwise re-scan full token as external!
    jmp .got_verb

.got_verb:
    mov ecx, ebx
    sub ecx, esi
    jz .external_quoted
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .done
    mov edi, eax
    push esi ; orig line start
    push edi ; verbBuf
    rep movsb
    mov byte [edi], 0
    pop edi  ; verbBuf
    pop ebp  ; orig line start
    mov esi, ebx ; arg tail (may start at ' ', '/', etc.)

    ; Try internal built-in command table!
    call cmd_try_builtin32
    jnc .done

    ; Not an internal command -> re-parse first whitespace-delimited program token from ebp!
    mov esi, ebp
.external_quoted:
    call prs_read_word32
    jc .done
    mov edi, edx ; raw argument tail
    mov esi, eax ; program token
    xor ecx, ecx
    call exec_external32
    jnc .done
    call con_report_err32
    jmp .done

.done_noop:
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc cmd_switch_drive32
;; @abi win32
;; @in ESI = "X:" token
;; @out Switches process CWD to =X: (or "X:") and updates g_last_exit_code
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements bare drive switching (`C:`, `D:`) restoring saved per-drive CWD from `=X:`.
@proc cmd_switch_drive32
    push ebx
    push esi
    push edi
    sub esp, 16
    mov edi, esp

    mov al, byte [esi]
    call ascii_upper_al
    mov bl, al

    ; Check "=X:" in environment first
    mov byte [edi], '='
    mov byte [edi + 1], bl
    mov byte [edi + 2], ':'
    mov byte [edi + 3], 0
    mov esi, edi
    call env_get_raw32
    test eax, eax
    jnz .set_dir

    mov byte [edi + 4], bl
    mov byte [edi + 5], ':'
    mov byte [edi + 6], 0x5C
    mov byte [edi + 7], 0
    lea eax, [edi + 4]

.set_dir:
    mov esi, eax
    call hal_set_cwd32
    jc .err

    mov ecx, 1024
    call arena_alloc32
    jc .ok
    mov edi, eax
    mov ecx, 1020
    call hal_get_cwd32
    jc .ok
    mov esi, edi
    call path_sync_drive_env32
.ok:
    mov dword [g_last_exit_code], 0
    add esp, 16
    pop edi
    pop esi
    pop ebx
    ret
.err:
    call con_report_err32
    mov dword [g_last_exit_code], 1
    add esp, 16
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc cmd_try_builtin32
;; @abi win32
;; @in EDI = NUL-terminated verb, ESI = raw argument tail
;; @out CF=0 if handled as built-in; CF=1 if external command
;; @err CF=1 when EDI is not an internal command
;; @clobbers eax, ecx, edx
;; @desc Matches all 47 internal cmd.exe / COMMAND.COM built-in commands (including aliases CHDIR, ERASE, MKDIR, RMDIR, RENAME, LOADHIGH).
@proc cmd_try_builtin32
    push ebx
    push esi
    push edi
    mov ebx, esi ; save raw tail
    mov esi, edi ; verb

    mov edi, addr(szCmdEcho)
    call str_eq_i32
    test eax, eax
    jnz .b_echo

    mov edi, addr(szCmdSet)
    call str_eq_i32
    test eax, eax
    jnz .b_set

    mov edi, addr(szCmdCd)
    call str_eq_i32
    test eax, eax
    jnz .b_cd
    mov edi, addr(szCmdChdir)
    call str_eq_i32
    test eax, eax
    jnz .b_cd

    mov edi, addr(szCmdMd)
    call str_eq_i32
    test eax, eax
    jnz .b_md
    mov edi, addr(szCmdMkdir)
    call str_eq_i32
    test eax, eax
    jnz .b_md

    mov edi, addr(szCmdRd)
    call str_eq_i32
    test eax, eax
    jnz .b_rd
    mov edi, addr(szCmdRmdir)
    call str_eq_i32
    test eax, eax
    jnz .b_rd

    mov edi, addr(szCmdDel)
    call str_eq_i32
    test eax, eax
    jnz .b_del
    mov edi, addr(szCmdErase)
    call str_eq_i32
    test eax, eax
    jnz .b_del

    mov edi, addr(szCmdCopy)
    call str_eq_i32
    test eax, eax
    jnz .b_copy

    mov edi, addr(szCmdMove)
    call str_eq_i32
    test eax, eax
    jnz .b_move

    mov edi, addr(szCmdRen)
    call str_eq_i32
    test eax, eax
    jnz .b_ren
    mov edi, addr(szCmdRename)
    call str_eq_i32
    test eax, eax
    jnz .b_ren

    mov edi, addr(szCmdDir)
    call str_eq_i32
    test eax, eax
    jnz .b_dir

    mov edi, addr(szCmdType)
    call str_eq_i32
    test eax, eax
    jnz .b_type

    mov edi, addr(szCmdCall)
    call str_eq_i32
    test eax, eax
    jnz .b_call

    mov edi, addr(szCmdGoto)
    call str_eq_i32
    test eax, eax
    jnz .b_goto

    mov edi, addr(szCmdExit)
    call str_eq_i32
    test eax, eax
    jnz .b_exit

    mov edi, addr(szCmdShift)
    call str_eq_i32
    test eax, eax
    jnz .b_shift

    mov edi, addr(szCmdSetlocal)
    call str_eq_i32
    test eax, eax
    jnz .b_setlocal

    mov edi, addr(szCmdEndlocal)
    call str_eq_i32
    test eax, eax
    jnz .b_endlocal

    mov edi, addr(szCmdPushd)
    call str_eq_i32
    test eax, eax
    jnz .b_pushd

    mov edi, addr(szCmdPopd)
    call str_eq_i32
    test eax, eax
    jnz .b_popd

    mov edi, addr(szCmdMklink)
    call str_eq_i32
    test eax, eax
    jnz .b_mklink

    mov edi, addr(szCmdAssoc)
    call str_eq_i32
    test eax, eax
    jnz .b_assoc

    mov edi, addr(szCmdFtype)
    call str_eq_i32
    test eax, eax
    jnz .b_ftype

    mov edi, addr(szCmdVer)
    call str_eq_i32
    test eax, eax
    jnz .b_ver

    mov edi, addr(szCmdVol)
    call str_eq_i32
    test eax, eax
    jnz .b_vol

    mov edi, addr(szCmdCls)
    call str_eq_i32
    test eax, eax
    jnz .b_cls

    mov edi, addr(szCmdColor)
    call str_eq_i32
    test eax, eax
    jnz .b_color

    mov edi, addr(szCmdTitle)
    call str_eq_i32
    test eax, eax
    jnz .b_title

    mov edi, addr(szCmdChcp)
    call str_eq_i32
    test eax, eax
    jnz .b_chcp

    mov edi, addr(szCmdPath)
    call str_eq_i32
    test eax, eax
    jnz .b_path

    mov edi, addr(szCmdDpath)
    call str_eq_i32
    test eax, eax
    jnz .b_dpath

    mov edi, addr(szCmdPrompt)
    call str_eq_i32
    test eax, eax
    jnz .b_prompt

    mov edi, addr(szCmdPause)
    call str_eq_i32
    test eax, eax
    jnz .b_pause

    mov edi, addr(szCmdDate)
    call str_eq_i32
    test eax, eax
    jnz .b_date

    mov edi, addr(szCmdTime)
    call str_eq_i32
    test eax, eax
    jnz .b_time

    mov edi, addr(szCmdTruename)
    call str_eq_i32
    test eax, eax
    jnz .b_truename

    mov edi, addr(szCmdStart)
    call str_eq_i32
    test eax, eax
    jnz .b_start

    mov edi, addr(szCmdCmd)
    call str_eq_i32
    test eax, eax
    jnz .b_cmd
    mov edi, addr(szCmdCommand)
    call str_eq_i32
    test eax, eax
    jnz .b_cmd

    mov edi, addr(szCmdDoskey)
    call str_eq_i32
    test eax, eax
    jnz .b_doskey

    mov edi, addr(szCmdMode)
    call str_eq_i32
    test eax, eax
    jnz .b_mode

    mov edi, addr(szCmdBreak)
    call str_eq_i32
    test eax, eax
    jnz .b_break

    mov edi, addr(szCmdVerify)
    call str_eq_i32
    test eax, eax
    jnz .b_verify

    mov edi, addr(szCmdKeys)
    call str_eq_i32
    test eax, eax
    jnz .b_keys

    mov edi, addr(szCmdCtty)
    call str_eq_i32
    test eax, eax
    jnz .b_ctty

    mov edi, addr(szCmdLh)
    call str_eq_i32
    test eax, eax
    jnz .b_lh
    mov edi, addr(szCmdLoadhigh)
    call str_eq_i32
    test eax, eax
    jnz .b_lh

    mov edi, addr(szCmdRem)
    call str_eq_i32
    test eax, eax
    jnz .b_rem

    pop edi
    pop esi
    pop ebx
    stc
    ret

.b_echo:
    mov esi, ebx
    call bi_echo32
    jmp .handled
.b_set:
    mov esi, ebx
    call bi_set32
    jmp .handled
.b_cd:
    mov esi, ebx
    call bi_cd32
    jmp .handled
.b_md:
    mov esi, ebx
    call bi_md32
    jmp .handled
.b_rd:
    mov esi, ebx
    call bi_rd32
    jmp .handled
.b_del:
    mov esi, ebx
    call bi_del32
    jmp .handled
.b_copy:
    mov esi, ebx
    call bi_copy32
    jmp .handled
.b_move:
    mov esi, ebx
    call bi_move32
    jmp .handled
.b_ren:
    mov esi, ebx
    call bi_ren32
    jmp .handled
.b_dir:
    mov esi, ebx
    call bi_dir32
    jmp .handled
.b_type:
    mov esi, ebx
    call bi_type32
    jmp .handled
.b_call:
    mov esi, ebx
    call bi_call32
    jmp .handled
.b_goto:
    mov esi, ebx
    call bi_goto32
    jmp .handled
.b_exit:
    mov esi, ebx
    call bi_exit32
    jmp .handled
.b_shift:
    mov esi, ebx
    call bi_shift32
    jmp .handled
.b_setlocal:
    mov esi, ebx
    call bi_setlocal32
    jmp .handled
.b_endlocal:
    call env_endlocal_pop32
    jmp .handled
.b_pushd:
    mov esi, ebx
    call bi_pushd32
    jmp .handled
.b_popd:
    call bi_popd32
    jmp .handled
.b_mklink:
    mov esi, ebx
    call bi_mklink32
    jmp .handled
.b_assoc:
    mov esi, ebx
    call bi_assoc32
    jmp .handled
.b_ftype:
    mov esi, ebx
    call bi_ftype32
    jmp .handled
.b_ver:
    call bi_ver32
    jmp .handled
.b_vol:
    mov esi, ebx
    call bi_vol32
    jmp .handled
.b_cls:
    call bi_cls32
    jmp .handled
.b_color:
    mov esi, ebx
    call bi_color32
    jmp .handled
.b_title:
    mov esi, ebx
    call bi_title32
    jmp .handled
.b_chcp:
    mov esi, ebx
    call bi_chcp32
    jmp .handled
.b_path:
    mov esi, ebx
    mov edi, addr(szVarPath)
    call bi_path_var32
    jmp .handled
.b_dpath:
    mov esi, ebx
    mov edi, addr(szVarDpath)
    call bi_path_var32
    jmp .handled
.b_prompt:
    mov esi, ebx
    call bi_prompt32
    jmp .handled
.b_pause:
    call bi_pause32
    jmp .handled
.b_date:
    mov esi, ebx
    call bi_date32
    jmp .handled
.b_time:
    mov esi, ebx
    call bi_time32
    jmp .handled
.b_truename:
    mov esi, ebx
    call bi_truename32
    jmp .handled
.b_start:
    mov esi, ebx
    call bi_start32
    jmp .handled
.b_cmd:
    mov esi, ebx
    call bi_cmd_subshell32
    jmp .handled
.b_doskey:
    mov esi, ebx
    call bi_doskey32
    jmp .handled
.b_mode:
    mov esi, ebx
    call bi_mode32
    jmp .handled
.b_break:
    mov esi, ebx
    mov edi, addr(g_break_on)
    mov ecx, addr(szMsgBreak)
    call bi_toggle_state32
    jmp .handled
.b_verify:
    mov esi, ebx
    mov edi, addr(g_verify_on)
    mov ecx, addr(szMsgVerify)
    call bi_toggle_state32
    jmp .handled
.b_keys:
    mov esi, ebx
    mov edi, addr(g_keys_on)
    mov ecx, addr(szMsgKeys)
    call bi_toggle_state32
    jmp .handled
.b_ctty:
    mov dword [g_last_exit_code], 0
    jmp .handled
.b_lh:
    mov esi, ebx
    call exec_simple_line32
    jmp .handled
.b_rem:
    jmp .handled

.handled:
    pop edi
    pop esi
    pop ebx
    clc
    ret
@endproc

;; @proc bi_echo32
;; @abi win32
;; @in ESI = raw argument tail (starting with optional single space/tab)
;; @out Prints text or toggles/queries ECHO ON|OFF
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements ECHO, ECHO ON, ECHO OFF, and ECHO <message>.
@proc bi_echo32
    push ebx
    push esi
    push edi

    ; Skip exactly ONE leading space or tab when emitting text!
    mov ebx, esi
    cmp byte [ebx], 0x20
    je .one_ws
    cmp byte [ebx], 0x09
    jne .chk_empty
.one_ws:
    inc ebx

.chk_empty:
    mov esi, ebx
    call prs_skip_ws32
    cmp byte [eax], 0
    je .show_status

    ; Check exact trimmed "ON" or "OFF"
    mov esi, eax
    call arena_strdup32
    jc .print_msg
    mov esi, eax
    ; Trim trailing spaces on candidate
    call str_len32
.tr_lp:
    test ecx, ecx
    jz .print_msg
    mov al, byte [esi + ecx - 1]
    cmp al, 0x20
    je .tr_chop
    cmp al, 0x09
    jne .chk_on_off
.tr_chop:
    dec ecx
    mov byte [esi + ecx], 0
    jmp .tr_lp

.chk_on_off:
    mov edi, addr(szKwOn)
    call str_eq_i32
    test eax, eax
    jnz .set_on
    mov edi, addr(szKwOff)
    call str_eq_i32
    test eax, eax
    jnz .set_off

.print_msg:
    mov esi, ebx
    call con_print_z32
    call con_print_crlf32
    jmp .done

.set_on:
    mov byte [g_echo_on], 1
    jmp .done
.set_off:
    mov byte [g_echo_on], 0
    jmp .done

.show_status:
    mov esi, addr(szEchoIsOn)
    cmp byte [g_echo_on], 0
    jne .prt_st
    mov esi, addr(szEchoIsOff)
.prt_st:
    call con_print_z32

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_set32
;; @abi win32
;; @in ESI = raw argument tail after SET
;; @out Handles SET, SET prefix, SET VAR=val, SET "VAR=val", SET /A expr, and SET /P VAR=prompt
;; @err Updates g_last_exit_code (0 on success, 1 on error / undefined prefix)
;; @clobbers eax, ecx, edx
;; @desc Full NT cmd.exe SET built-in implementation.
@proc bi_set32
    push ebx
    push esi
    push edi
    push ebp

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .list_all

    ; Check /A or /P switches
    cmp byte [esi], '/'
    jne .chk_quoted_set
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'A'
    je .do_set_a
    cmp al, 'P'
    je .do_set_p

.chk_quoted_set:
    ; Support `SET "VAR=value"` (strips outer quotes from assignment!)
    cmp byte [esi], '"'
    jne .find_eq
    inc esi
    call arena_strdup32
    jc .err1
    mov esi, eax
    ; Truncate at last '"' on line!
    call str_len32
.find_last_q:
    test ecx, ecx
    jz .find_eq
    dec ecx
    cmp byte [esi + ecx], '"'
    jne .find_last_q
    mov byte [esi + ecx], 0

.find_eq:
    ; Scan for '=' (skipping index 0 if hidden '=C:')
    mov ebx, esi
    cmp byte [ebx], '='
    jne .eq_sc
    inc ebx
.eq_sc:
    mov al, byte [ebx]
    test al, al
    jz .prefix_filter
    cmp al, '='
    je .assign_var
    inc ebx
    jmp .eq_sc

.assign_var:
    mov ecx, ebx
    sub ecx, esi
    jz .err1
    push ebx
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    pop ebx
    jc .err1
    mov edi, eax
    push edi
    rep movsb
    mov byte [edi], 0
    pop esi ; name
    lea edi, [ebx + 1] ; value
    call env_set32
    jc .err1
    mov dword [g_last_exit_code], 0
    jmp .done

.do_set_a:
    add esi, 2
    call prs_skip_ws32
    mov esi, eax
    call arith_eval32
    jc .set_a_err
    mov ebx, eax
    ; In interactive mode (g_batch_depth == 0), SET /A prints the evaluated result WITHOUT trailing CRLF?
    ; Wait: cmd.exe /C "SET /A 2+3" prints "5" (no CRLF when /C interactive, or prints number)!Wait: let us check real cmd.exe:
    ; `cmd /c "set /a 1+2"` writes `3` without CRLF when interactive/c, or in batch writes nothing!
    ;Wait: in batch scripts `SET /A X=1+2` is silent; in `/C` or interactive `SET /A 1+2` prints `3`!
    cmp dword [g_batch_depth], 0
    jne .ok0
    sub esp, 24
    mov edi, esp
    mov eax, ebx
    call fmt_i32_dec
    mov esi, edi
    call con_write_out32
    add esp, 24
    jmp .ok0

.set_a_err:
    call con_report_err32
    jmp .err1

.do_set_p:
    add esi, 2
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '"'
    jne .sp_eq
    inc esi
    call arena_strdup32
    jc .err1
    mov esi, eax
    call str_len32
    test ecx, ecx
    jz .err1
    cmp byte [esi + ecx - 1], '"'
    jne .sp_eq
    mov byte [esi + ecx - 1], 0
.sp_eq:
    mov ebx, esi
.sp_sc:
    mov al, byte [ebx]
    test al, al
    jz .err1
    cmp al, '='
    je .sp_got
    inc ebx
    jmp .sp_sc
.sp_got:
    mov byte [ebx], 0
    mov ebp, esi       ; varName
    lea esi, [ebx + 1] ; prompt
    cmp byte [esi], 0
    je .sp_read
    call con_print_z32
.sp_read:
    mov ecx, 2048
    call arena_alloc32
    jc .err1
    mov edi, eax
    mov ecx, 2040
    call con_read_line32
    jc .err1
    mov esi, ebp
    call env_set32
    jmp .ok0

.list_all:
    mov esi, dword [g_env_buf]
.all_lp:
    cmp byte [esi], 0
    je .ok0
    ; Hide leading '=' internal drive variables (`=C:=...`) just like `SET` in cmd.exe!
    cmp byte [esi], '='
    je .all_skip
    call con_print_z32
    call con_print_crlf32
.all_skip:
    call str_len32
    lea esi, [esi + ecx + 1]
    jmp .all_lp

.prefix_filter:
    mov ebp, esi ; prefix
    call str_len32
    mov ebx, ecx ; prefix_len
    xor edi, edi ; match_count
    mov esi, dword [g_env_buf]
.pf_lp:
    cmp byte [esi], 0
    je .pf_done
    ; Check if esi[0..ebx] matches ebp[0..ebx] case-insensitively
    xor ecx, ecx
.pf_cmp:
    cmp ecx, ebx
    jae .pf_hit
    mov al, byte [esi + ecx]
    test al, al
    jz .pf_nx
    call ascii_upper_al
    mov dl, al
    mov al, byte [ecx + ebp]
    call ascii_upper_al
    cmp dl, al
    jne .pf_nx
    inc ecx
    jmp .pf_cmp
.pf_hit:
    inc edi
    call con_print_z32
    call con_print_crlf32
.pf_nx:
    call str_len32
    lea esi, [esi + ecx + 1]
    jmp .pf_lp
.pf_done:
    test edi, edi
    jnz .ok0
    mov esi, addr(szEnvNotDef1)
    call con_print_err_z32
    mov esi, ebp
    call con_print_err_z32
    mov esi, addr(szEnvNotDef2)
    call con_print_err_z32
    jmp .err1

.ok0:
    mov dword [g_last_exit_code], 0
    jmp .done
.err1:
    mov dword [g_last_exit_code], 1
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_call32
;; @abi win32
;; @in ESI = raw argument tail after CALL
;; @out Executes `:label args`, `.bat`/`.cmd args`, or re-expanded command line!
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements CALL :subroutine, CALL script.bat, and CALL <command> double-expansion.
@proc bi_call32
    push ebx
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .done

    ; Check `CALL :label [args...]`
    cmp byte [esi], ':'
    jne .not_subr
    cmp dword [g_batch_depth], 0
    je .done
    inc esi
    call prs_read_word32
    jc .done
    mov ebx, edx ; arg tail
    mov esi, eax ; label name
    mov edi, addr(g_goto_label)
    call str_copy32
    mov byte [g_goto_pending], 0

    ; Clone active batch path from current frame!
    mov ecx, dword [g_batch_depth]
    dec ecx
    shl ecx, 6
    mov esi, dword [g_batch_stack + ecx + BATCH_FRAME32.pBatchPath]
    mov edi, ebx
    mov cl, 1 ; bIsCallSub = 1
    call batch_run_file32
    jmp .done

.not_subr:
    ; Re-expand `%` once (`CALL ECHO %%VAR%%`!) then execute!
    xor ecx, ecx
    cmp dword [g_batch_depth], 0
    setne cl
    call expand_phase1_32
    jc .done
    mov esi, eax
    call parse_line_to_ast32
    jc .done
    mov esi, eax
    call exec_ast_node32

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_goto32
;; @abi win32
;; @in ESI = raw argument tail after GOTO
;; @out Latches g_goto_pending = 1 and g_goto_label
;; @err Updates g_last_exit_code = 1 if no label given
;; @clobbers eax, ecx, edx
;; @desc Implements GOTO [:]label and GOTO :EOF.
@proc bi_goto32
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .err
    cmp byte [esi], ':'
    jne .rd
    ; Preserve ":EOF" vs ":label" -> strip leading ':' UNLESS it's ":EOF" (which we compare as "EOF" in batch_run_file32!)
    inc esi
.rd:
    call prs_read_word32
    jc .err
    mov esi, eax
    mov edi, addr(g_goto_label)
    call str_copy32
    mov byte [g_goto_pending], 1
    jmp .done
.err:
    mov eax, ERR_SYNTAX
    call con_report_err32
    mov dword [g_last_exit_code], 1
.done:
    pop edi
    pop esi
    ret
@endproc

;; @proc bi_exit32
;; @abi win32
;; @in ESI = raw argument tail (`[/B] [exitCode]`)
;; @out Sets g_batch_ret_flag (if /B inside batch) or g_exit_flag, plus g_last_exit_code
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements EXIT [exitCode] and EXIT /B [exitCode].
@proc bi_exit32
    push ebx
    push esi
    xor ebx, ebx ; is_b

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .chk_code
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'B'
    jne .chk_code
    mov ebx, 1
    add esi, 2
    call prs_skip_ws32
    mov esi, eax

.chk_code:
    cmp byte [esi], 0
    je .apply_flag
    call exp_parse_signed_int32
    cmp edx, esi
    je .apply_flag
    mov dword [g_last_exit_code], eax

.apply_flag:
    test ebx, ebx
    jz .full_exit
    cmp dword [g_batch_depth], 0
    je .full_exit
    mov byte [g_batch_ret_flag], 1
    jmp .done
.full_exit:
    mov byte [g_exit_flag], 1
.done:
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_shift32
;; @abi win32
;; @in ESI = optional `/0`..`/8`
;; @out Increments bShiftOff (or shifts argvPtrs[n..9] when /n is given) in active BATCH_FRAME32
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements SHIFT and SHIFT /0..8 inside batch files.
@proc bi_shift32
    push ebx
    push esi

    mov ecx, dword [g_batch_depth]
    test ecx, ecx
    jz .done
    dec ecx
    shl ecx, 6
    lea ebx, [g_batch_stack + ecx]

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .std_shift
    movzx eax, byte [esi + 1]
    cmp al, '0'
    jb .std_shift
    cmp al, '8'
    ja .std_shift
    sub eax, '0'
    ; Shift argvPtrs[eax..8] left by 1 slot!
    mov ecx, dword [ebx + BATCH_FRAME32.argvPtrs + eax * 4]
    push eax
    mov eax, ecx
    call perm_free32
    pop ecx
.sh_n_lp:
    cmp ecx, 9
    jae .zero_last
    mov eax, dword [ebx + BATCH_FRAME32.argvPtrs + ecx * 4 + 4]
    mov dword [ebx + BATCH_FRAME32.argvPtrs + ecx * 4], eax
    inc ecx
    jmp .sh_n_lp
.zero_last:
    mov dword [ebx + BATCH_FRAME32.argvPtrs + 36], 0
    jmp .done

.std_shift:
    cmp byte [ebx + BATCH_FRAME32.bShiftOff], 9
    jae .done
    inc byte [ebx + BATCH_FRAME32.bShiftOff]

.done:
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_setlocal32
;; @abi win32
;; @in ESI = optional EnableExtensions / DisableExtensions / EnableDelayedExpansion / DisableDelayedExpansion
;; @out Pushes SETLOCAL snapshot and applies requested mode flags
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements SETLOCAL with optional Extensions and DelayedExpansion mode toggles.
@proc bi_setlocal32
    push esi
    push edi

    call env_setlocal_push32
    jc .err

.opt_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .ok
    call prs_read_word32
    jc .ok
    push edx
    mov esi, eax
    mov edi, addr(szOptEnDelay)
    call str_eq_i32
    test eax, eax
    jnz .en_del
    mov edi, addr(szOptDisDelay)
    call str_eq_i32
    test eax, eax
    jnz .dis_del
    mov edi, addr(szOptEnExt)
    call str_eq_i32
    test eax, eax
    jnz .en_ext
    mov edi, addr(szOptDisExt)
    call str_eq_i32
    test eax, eax
    jnz .dis_ext
    pop esi
    jmp .opt_lp

.en_del:
    mov byte [g_delayed_exp], 1
    pop esi
    jmp .opt_lp
.dis_del:
    mov byte [g_delayed_exp], 0
    pop esi
    jmp .opt_lp
.en_ext:
    mov byte [g_extensions_on], 1
    pop esi
    jmp .opt_lp
.dis_ext:
    mov byte [g_extensions_on], 0
    pop esi
    jmp .opt_lp

.ok:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    ret
.err:
    mov dword [g_last_exit_code], 1
    pop edi
    pop esi
    ret
@endproc

;; @proc bi_ver32
;; @abi win32
;; @in none
;; @out Prints authentic OS version banner (Windows NT major.minor.build or Windows 95/98/Me)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Displays WINCMD32 + authentic OS version queried via RtlGetVersion / PEB / GetVersion.
@proc bi_ver32
    push esi
    push edi
    sub esp, 32
    mov edi, esp

    call con_print_crlf32
    cmp byte [g_is_win9x], 0
    jne .ver_9x

    mov esi, addr(szVerBannerNt1)
    call con_print_z32
    mov eax, dword [g_os_major]
    xor edx, edx
    call fmt_u64_plain32
    mov esi, edi
    call con_write_out32

    mov esi, addr(szDot32)
    mov ecx, 1
    call con_write_out32

    mov eax, dword [g_os_minor]
    xor edx, edx
    call fmt_u64_plain32
    mov esi, edi
    call con_write_out32

    mov esi, addr(szDot32)
    mov ecx, 1
    call con_write_out32

    mov eax, dword [g_os_build]
    xor edx, edx
    call fmt_u64_plain32
    mov esi, edi
    call con_write_out32

    mov esi, addr(szVerBannerNt2)
    call con_print_z32
    jmp .ok

.ver_9x:
    mov esi, addr(szVerBanner9x)
    call con_print_z32

.ok:
    mov dword [g_last_exit_code], 0
    add esp, 32
    pop edi
    pop esi
    ret
@endproc

;; @proc bi_cls32
;; @abi win32
;; @in none
;; @out Clears active Win32 console screen buffer or emits ANSI VT clear sequence
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Clears the console screen via FillConsoleOutputCharacterA/Attribute + SetConsoleCursorPosition.
@proc bi_cls32
    push ebx
    push esi
    sub esp, 28
    mov ebx, esp

    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov esi, eax
    push ebx
    push esi
    call dword [__imp__GetConsoleScreenBufferInfo@8]
    test eax, eax
    jz .done

    ; dwSize.X * dwSize.Y cells from COORD (0,0)
    movzx eax, word [ebx]
    movzx ecx, word [ebx + 2]
    imul ecx, eax
    push ecx ; cellCount

    lea edx, [ebx + 24]
    push edx
    push 0      ; COORD (0,0)
    push ecx
    push 0x20   ; ' '
    push esi
    call dword [__imp__FillConsoleOutputCharacterA@20]

    pop ecx ; cellCount
    movzx eax, word [ebx + 8] ; wAttributes
    lea edx, [ebx + 24]
    push edx
    push 0
    push ecx
    push eax
    push esi
    call dword [__imp__FillConsoleOutputAttribute@20]

    push 0
    push esi
    call dword [__imp__SetConsoleCursorPosition@8]

.done:
    mov dword [g_last_exit_code], 0
    add esp, 28
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_color32
;; @abi win32
;; @in ESI = optional 1 or 2 hex digits (`COLOR 0A`); empty resets to initial g_default_attr
;; @out Sets console attribute (fails with ERRORLEVEL 1 if fg == bg!)
;; @err Sets g_last_exit_code = 1 when foreground == background
;; @clobbers eax, ecx, edx
;; @desc Implements COLOR [attr] with authentic fg != bg validation.
@proc bi_color32
    push ebx
    push esi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    jne .parse_hex
    movzx ebx, word [g_default_attr]
    jmp .apply

.parse_hex:
    xor ebx, ebx
    xor ecx, ecx
.hx_lp:
    mov al, byte [esi + ecx]
    cmp al, 0
    je .hx_done
    cmp al, 0x20
    je .hx_done
    cmp al, 0x09
    je .hx_done
    cmp ecx, 2
    jae .err
    call ascii_upper_al
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
    inc ecx
    jmp .hx_lp

.hx_done:
    cmp ecx, 2
    jb .apply
    ; Reject identical bg and fg nibbles!
    mov eax, ebx
    shr eax, 4
    mov edx, ebx
    and edx, 0x0F
    cmp eax, edx
    je .err

.apply:
    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    push ebx
    push eax
    call dword [__imp__SetConsoleTextAttribute@8]
    mov dword [g_last_exit_code], 0
    pop esi
    pop ebx
    ret

.err:
    mov dword [g_last_exit_code], 1
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_title32
;; @abi win32
;; @in ESI = window title text
;; @out Updates console window title via SetConsoleTitleW (NT) or SetConsoleTitleA (Win9x)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements TITLE [string].
@proc bi_title32
    push esi
    cmp byte [esi], 0x20
    jne .go
    inc esi
.go:
    cmp byte [g_is_win9x], 0
    jne .ansi
    call wtf8_to_utf16le
    jc .done
    push eax
    call dword [__imp__SetConsoleTitleW@4]
    jmp .done
.ansi:
    push esi
    call dword [__imp__SetConsoleTitleA@4]
.done:
    mov dword [g_last_exit_code], 0
    pop esi
    ret
@endproc

;; @proc bi_chcp32
;; @abi win32
;; @in ESI = optional codepage number (e.g. 437, 936, 65001)
;; @out Queries or updates active console codepage + g_dbcs_bitmap
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements CHCP [nnn] supporting UTF-8 (65001) and OEM/DBCS codepages.
@proc bi_chcp32
    push ebx
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .query_cp

    call sa32_parse_i32_str
    jc .err
    mov ebx, eax
    push ebx
    call dword [__imp__SetConsoleCP@4]
    push ebx
    call dword [__imp__SetConsoleOutputCP@4]
    mov dword [g_active_cp], ebx
    mov eax, ebx
    call cp_rebuild_dbcs32
    jmp .print_cp

.query_cp:
    call dword [__imp__GetConsoleOutputCP@0]
    test eax, eax
    jnz .got_q
    mov eax, dword [g_active_cp]
.got_q:
    mov ebx, eax

.print_cp:
    mov esi, addr(szActiveCp32)
    call con_print_z32
    sub esp, 24
    mov edi, esp
    mov eax, ebx
    xor edx, edx
    call fmt_u64_plain32
    mov esi, edi
    call con_write_out32
    add esp, 24
    call con_print_crlf32
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret

.err:
    mov eax, ERR_INVALID_NUMBER
    call con_report_err32
    mov dword [g_last_exit_code], 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_path_var32
;; @abi win32
;; @in ESI = raw argument tail, EDI = variable name ("PATH" or "DPATH")
;; @out Prints `PATH=...` or updates variable (`PATH ;` clears it!)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Shared implementation for PATH and DPATH internal commands.
@proc bi_path_var32
    push ebx
    push esi
    push edi

    mov ebx, edi ; varName
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '='
    jne .chk_empty
    inc esi
    call prs_skip_ws32
    mov esi, eax

.chk_empty:
    cmp byte [esi], 0
    je .show_val

    ; `PATH ;` clears PATH!
    cmp byte [esi], ';'
    jne .set_val
    cmp byte [esi + 1], 0
    jne .set_val
    mov esi, ebx
    xor edi, edi
    call env_set32
    jmp .ok

.set_val:
    mov edi, esi
    mov esi, ebx
    call env_set32
    jmp .ok

.show_val:
    mov esi, ebx
    call con_print_z32
    mov esi, addr(szEqSign32)
    mov ecx, 1
    call con_write_out32
    mov esi, ebx
    call env_get_raw32
    test eax, eax
    jnz .prt_v
    mov eax, addr(szNullPath32)
.prt_v:
    mov esi, eax
    call con_print_z32
    call con_print_crlf32

.ok:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_prompt32
;; @abi win32
;; @in ESI = optional prompt template (empty resets to $P$G!)
;; @out Updates PROMPT variable
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements PROMPT [text].
@proc bi_prompt32
    push esi
    push edi
    call prs_skip_ws32
    mov edi, eax
    cmp byte [edi], 0
    jne .set_it
    mov edi, addr(szDefPrompt32)
.set_it:
    mov esi, addr(szVarPrompt)
    call env_set32
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    ret
@endproc

;; @proc bi_pause32
;; @abi win32
;; @in none
;; @out Prints "Press any key to continue . . . " and reads line/key
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements PAUSE.
@proc bi_pause32
    push esi
    push edi
    mov esi, addr(szPausePrompt32)
    call con_print_z32
    sub esp, 32
    mov edi, esp
    mov ecx, 28
    call con_read_line32
    add esp, 32
    call con_print_crlf32
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    ret
@endproc

;; @proc bi_toggle_state32
;; @abi win32
;; @in ESI = optional ON/OFF, EDI = state byte ptr, ECX = status prefix string
;; @out Updates or prints ON/OFF setting
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Shared handler for BREAK, VERIFY, and KEYS built-in commands.
@proc bi_toggle_state32
    push ebx
    push esi
    push edi
    mov ebx, ecx ; prefix

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .show
    call prs_read_word32
    jc .show
    mov esi, eax
    push edi
    mov edi, addr(szKwOn)
    call str_eq_i32
    pop edi
    test eax, eax
    jnz .turn_on
    push edi
    mov edi, addr(szKwOff)
    call str_eq_i32
    pop edi
    test eax, eax
    jnz .turn_off
    jmp .show

.turn_on:
    mov byte [edi], 1
    jmp .done
.turn_off:
    mov byte [edi], 0
    jmp .done

.show:
    mov esi, ebx
    call con_print_z32
    mov esi, addr(szStateOnCrLf)
    cmp byte [edi], 0
    jne .prt
    mov esi, addr(szStateOffCrLf)
.prt:
    call con_print_z32

.done:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_assoc32
;; @abi win32
;; @in ESI = `[.ext[=[fileType]]]`
;; @out Queries or updates HKCR / HKCU\Software\Classes file extension association via ADVAPI32!
;; @err Sets g_last_exit_code = 2 when extension association is not found
;; @clobbers eax, ecx, edx
;; @desc Real Windows Registry ASSOC built-in (HKCU\Software\Classes write + HKCR read).
@proc bi_assoc32
    push ebx
    push esi
    push edi
    push ebp

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    jne .parse_ext
    ; Default query of ".bat" & ".cmd" when no arg given (or print common registered classes)
    mov esi, addr(szExtBat32)
    call bi_assoc_query_one32
    mov esi, addr(szExtCmd32)
    call bi_assoc_query_one32
    mov dword [g_last_exit_code], 0
    jmp .done

.parse_ext:
    call arena_strdup32
    jc .err
    mov ebp, eax ; ".ext" or ".ext=Type"
    mov ebx, eax
.find_eq:
    mov al, byte [ebx]
    test al, al
    jz .query_only
    cmp al, '='
    je .set_assoc
    inc ebx
    jmp .find_eq

.query_only:
    mov esi, ebp
    call bi_assoc_query_one32
    jc .not_found
    mov dword [g_last_exit_code], 0
    jmp .done

.set_assoc:
    mov byte [ebx], 0
    inc ebx ; fileType
    ; Write to HKCU\Software\Classes\<ext> first (works unprivileged on Vista..11 AND Wine!), fallback HKCR
    mov ecx, 512
    call arena_alloc32
    jc .err
    mov edi, eax
    push eax
    mov esi, addr(szRegSoftClasses)
    call str_copy32
    mov edi, eax
    mov esi, ebp
    call str_copy32
    pop esi ; "Software\Classes\.ext"
    push ebx
    mov ecx, ebx
    xor edi, edi
    mov ebx, HKEY_CURRENT_USER
    call hal_reg_set_str32
    pop ebx
    jc .err

    mov esi, ebp
    call con_print_z32
    mov esi, addr(szEqSign32)
    mov ecx, 1
    call con_write_out32
    mov esi, ebx
    call con_print_z32
    call con_print_crlf32
    mov dword [g_last_exit_code], 0
    jmp .done

.not_found:
    mov esi, addr(szAssocMissing)
    call con_print_err_z32
    mov esi, ebp
    call con_print_err_z32
    call con_print_crlf32
    mov dword [g_last_exit_code], 2
    jmp .done

.err:
    mov dword [g_last_exit_code], 1
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_assoc_query_one32
;; @abi win32
;; @in ESI = ".ext"
;; @out Prints .ext=FileType and returns CF=0; or CF=1 if unassociated
;; @err CF=1 if unassociated
;; @clobbers eax, ecx, edx
;; @desc Queries HKCU\Software\Classes\.ext then HKEY_CLASSES_ROOT\.ext via ADVAPI32.
@proc bi_assoc_query_one32
    push ebx
    push esi
    push edi
    mov ebx, esi

    mov ecx, 1024
    call arena_alloc32
    jc .no
    mov edi, eax ; out val buf

    ; 1. Probe HKCU\Software\Classes\<ext>
    mov ecx, 512
    call arena_alloc32
    jc .no
    push edi
    mov edi, eax
    push eax
    mov esi, addr(szRegSoftClasses)
    call str_copy32
    mov edi, eax
    mov esi, ebx
    call str_copy32
    pop esi ; subkey
    pop ecx ; out val buf
    push ecx
    push ebx
    mov ebx, HKEY_CURRENT_USER
    xor edi, edi
    mov edx, 1020
    call hal_reg_query_str32
    pop ebx
    pop edi
    jnc .print_hit

    ; 2. Probe HKEY_CLASSES_ROOT\<ext>
    push edi
    push ebx
    mov esi, ebx
    mov ecx, edi
    mov ebx, HKEY_CLASSES_ROOT
    xor edi, edi
    mov edx, 1020
    call hal_reg_query_str32
    pop ebx
    pop edi
    jc .no

.print_hit:
    cmp byte [edi], 0
    je .no
    mov esi, ebx
    call con_print_z32
    mov esi, addr(szEqSign32)
    mov ecx, 1
    call con_write_out32
    mov esi, edi
    call con_print_z32
    call con_print_crlf32
    pop edi
    pop esi
    pop ebx
    clc
    ret

.no:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc bi_ftype32
;; @abi win32
;; @in ESI = `[fileType[=[openCmdString]]]`
;; @out Queries or updates `<fileType>\shell\open\command` in Windows Registry via ADVAPI32!
;; @err Sets g_last_exit_code = 2 when fileType is not found
;; @clobbers eax, ecx, edx
;; @desc Real Windows Registry FTYPE built-in.
@proc bi_ftype32
    push ebx
    push esi
    push edi
    push ebp

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    jne .parse_ft
    mov esi, addr(szDefBatFileCls)
    call bi_ftype_query_one32
    mov dword [g_last_exit_code], 0
    jmp .done

.parse_ft:
    call arena_strdup32
    jc .err
    mov ebp, eax
    mov ebx, eax
.sc_eq:
    mov al, byte [ebx]
    test al, al
    jz .q_only
    cmp al, '='
    je .s_ftype
    inc ebx
    jmp .sc_eq

.q_only:
    mov esi, ebp
    call bi_ftype_query_one32
    jc .not_found
    mov dword [g_last_exit_code], 0
    jmp .done

.s_ftype:
    mov byte [ebx], 0
    inc ebx ; openCmdString
    mov ecx, 1024
    call arena_alloc32
    jc .err
    mov edi, eax
    push eax
    mov esi, addr(szRegSoftClasses)
    call str_copy32
    mov edi, eax
    mov esi, ebp
    call str_copy32
    mov edi, eax
    mov esi, addr(szRegShellOpenCmd)
    call str_copy32
    pop esi ; "Software\Classes\<type>\shell\open\command"

    push ebx
    mov ecx, ebx
    xor edi, edi
    mov ebx, HKEY_CURRENT_USER
    call hal_reg_set_str32
    pop ebx
    jc .err

    mov esi, ebp
    call con_print_z32
    mov esi, addr(szEqSign32)
    mov ecx, 1
    call con_write_out32
    mov esi, ebx
    call con_print_z32
    call con_print_crlf32
    mov dword [g_last_exit_code], 0
    jmp .done

.not_found:
    mov esi, addr(szFtypeMissing)
    call con_print_err_z32
    mov dword [g_last_exit_code], 2
    jmp .done
.err:
    mov dword [g_last_exit_code], 1
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_ftype_query_one32
;; @abi win32
;; @in ESI = "fileType"
;; @out Prints fileType=cmd and returns CF=0; or CF=1 if missing
;; @err CF=1 if missing
;; @clobbers eax, ecx, edx
;; @desc Queries `<fileType>\shell\open\command` in HKCU\Software\Classes then HKEY_CLASSES_ROOT.
@proc bi_ftype_query_one32
    push ebx
    push esi
    push edi
    mov ebx, esi

    mov ecx, 2048
    call arena_alloc32
    jc .no
    mov edi, eax ; out buf

    mov ecx, 1024
    call arena_alloc32
    jc .no
    push edi
    mov edi, eax
    push eax
    mov esi, addr(szRegSoftClasses)
    call str_copy32
    mov edi, eax
    mov esi, ebx
    call str_copy32
    mov edi, eax
    mov esi, addr(szRegShellOpenCmd)
    call str_copy32
    pop esi
    pop ecx
    push ecx
    push ebx
    mov ebx, HKEY_CURRENT_USER
    xor edi, edi
    mov edx, 2040
    call hal_reg_query_str32
    pop ebx
    pop edi
    jnc .hit

    ; Fallback HKEY_CLASSES_ROOT\<type>\shell\open\command
    mov ecx, 1024
    call arena_alloc32
    jc .no
    push edi
    mov edi, eax
    push eax
    mov esi, ebx
    call str_copy32
    mov edi, eax
    mov esi, addr(szRegShellOpenCmd)
    call str_copy32
    pop esi
    pop ecx
    push ecx
    push ebx
    mov ebx, HKEY_CLASSES_ROOT
    xor edi, edi
    mov edx, 2040
    call hal_reg_query_str32
    pop ebx
    pop edi
    jc .no

.hit:
    mov esi, ebx
    call con_print_z32
    mov esi, addr(szEqSign32)
    mov ecx, 1
    call con_write_out32
    mov esi, edi
    call con_print_z32
    call con_print_crlf32
    pop edi
    pop esi
    pop ebx
    clc
    ret
.no:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc bi_doskey32
;; @abi win32
;; @in ESI = `/HISTORY`, `/REINSTALL`, `/MACROS`, or `NAME=BODY`
;; @out Prints command history or manages DOSKEY macros
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements DOSKEY /HISTORY, /REINSTALL, /MACROS, and macro registration.
@proc bi_doskey32
    push ebx
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .ok

    cmp byte [esi], '/'
    jne .ok
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'H'
    je .show_hist
    cmp al, 'R'
    je .clear_hist
    jmp .ok

.show_hist:
    xor ebx, ebx
.h_lp:
    cmp ebx, dword [g_hist_count]
    jae .ok
    mov esi, dword [g_hist_ptrs + ebx * 4]
    call con_print_z32
    call con_print_crlf32
    inc ebx
    jmp .h_lp

.clear_hist:
    xor ebx, ebx
.c_lp:
    cmp ebx, dword [g_hist_count]
    jae .c_done
    mov eax, dword [g_hist_ptrs + ebx * 4]
    call perm_free32
    mov dword [g_hist_ptrs + ebx * 4], 0
    inc ebx
    jmp .c_lp
.c_done:
    mov dword [g_hist_count], 0

.ok:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret
@endproc
