;; @file wincmd.asm
;; @isa 8086
;; @desc Authentic 3-part MS-DOS 1.25/2.00/4.00/7.x command processor (RESIDENT + TRANSIENT + INIT).

@cpu 8086
@format com3part
@entry res_entry

@include "../inc/err_code.inc"
@include "../inc/dos_equ.inc"
@include "../inc/dos_strc.inc"
@include "../inc/ast_kind.inc"

;; @const TRN_RESERVE_PARAS
;; @desc Paragraphs reserved at top of conventional RAM (40.5 KiB: up to 32K code/rodata + 8.5K scratch/arena).
@const TRN_RESERVE_PARAS = 0x0A20

;; @const TRN_SCRATCH_BASE
;; @desc Start offset of mutable scratch variables inside wTrnSeg (above immutable code/rodata).
@const TRN_SCRATCH_BASE = 0x8000

;; @const wArenaBase
;; @desc Transient bump arena base offset word in scratch RAM.
@const wArenaBase = 0x8000

;; @const wArenaPtr
;; @desc Transient bump arena current pointer word in scratch RAM.
@const wArenaPtr = 0x8002

;; @const wArenaLimit
;; @desc Transient bump arena upper limit word in scratch RAM.
@const wArenaLimit = 0x8004

;; @const bTrnDosMajor
;; @desc Cached DOS major version inside TRANSIENT scratch RAM.
@const bTrnDosMajor = 0x8006

;; @const bTrnDosMinor
;; @desc Cached DOS minor version inside TRANSIENT scratch RAM.
@const bTrnDosMinor = 0x8007

;; @const bTrnEcho
;; @desc Active ECHO ON/OFF state inside TRANSIENT scratch RAM.
@const bTrnEcho = 0x8008

;; @const bTrnExitReq
;; @desc Non-zero when EXIT command requests shell termination.
@const bTrnExitReq = 0x8009

;; @const wTrnExitCode
;; @desc Last command exit code / ERRORLEVEL inside TRANSIENT scratch RAM.
@const wTrnExitCode = 0x800A

;; @const wTrnResSeg
;; @desc Segment of RESIDENT PSP for state callbacks.
@const wTrnResSeg = 0x800C

;; @const bTrnConEof
;; @desc Non-zero when interactive console reaches physical 0x1A EOF.
@const bTrnConEof = 0x800E

;; @const bTrnRedirActive
;; @desc Non-zero when per-statement I/O redirection is active.
@const bTrnRedirActive = 0x800F

;; @const bmDbcsLead
;; @desc 32-byte (256-bit) active DBCS lead-byte lookup bitmap in TRANSIENT scratch RAM.
@const bmDbcsLead = 0x8010

;; @const szNumScratch
;; @desc 32-byte numeric formatting buffer in TRANSIENT scratch RAM.
@const szNumScratch = 0x8030

;; @const wSavedStdinHdl
;; @desc Duplicated original stdin handle during DOS 2.00+ '<' redirection.
@const wSavedStdinHdl = 0x8050

;; @const wSavedStdoutHdl
;; @desc Duplicated original stdout handle during DOS 2.00+ '>' / '>>' redirection.
@const wSavedStdoutHdl = 0x8052

;; @const wTrnLdr125Ptr
;; @desc Far pointer (offset, segment) to resident ldr125_exec_far trampoline.
@const wTrnLdr125Ptr = 0x8054

;; @const wTrnExec200Ptr
;; @desc Far pointer (offset, segment) to resident exec200_far trampoline.
@const wTrnExec200Ptr = 0x8058

;; @const wTrnRed125InPtr
;; @desc Far pointer (offset, segment) to resident red125_install_far trampoline.
@const wTrnRed125InPtr = 0x805C

;; @const wTrnRed125RmPtr
;; @desc Far pointer (offset, segment) to resident red125_remove_far trampoline.
@const wTrnRed125RmPtr = 0x8060

;; @const bTrnIsCall
;; @desc Non-zero when current statement was prefixed with CALL (pushes instead of chaining .BAT).
@const bTrnIsCall = 0x8064

;; @const bForDepth
;; @desc Active FOR loop variable nesting depth (0..6) in TRANSIENT scratch RAM.
@const bForDepth = 0x8065

;; @const wTrnRandSeed
;; @desc 16-bit LCG state for %RANDOM% dynamic variable synthesis.
@const wTrnRandSeed = 0x8066

;; @const bModDlrLen
;; @desc Byte length of active $VAR: name during %~$VAR:n modifier evaluation.
@const bModDlrLen = 0x8068

;; @const bPipeDepth
;; @desc Active spooled pipeline nesting depth (0..9) in TRANSIENT scratch RAM.
@const bPipeDepth = 0x8069

;; @const bForRDepth
;; @desc Active FOR /R directory recursion depth (0..5) in TRANSIENT scratch RAM.
@const bForRDepth = 0x806A

;; @const TRN_HIST_BUF
;; @desc 128-byte COMMAND.COM / DOSKEY template & recall buffer at wTrnSeg:0x8080.
@const TRN_HIST_BUF = 0x8080

;; @const TRN_CMD_BUF
;; @desc 256-byte command line buffer at wTrnSeg:0x8100 populated by INIT, batch, or line_ed.
@const TRN_CMD_BUF = 0x8100

;; @const TRN_BATCH_ARGS
;; @desc 256-byte (4 * 64B) batch argument table for %1..%9 / %* across nested CALL frames.
@const TRN_BATCH_ARGS = 0x8200

;; @const TRN_FOR_VARS
;; @desc 416-byte (6 * 68B) active FOR variable scope stack.
@const TRN_FOR_VARS = 0x8300

;; @const TRN_ENV_BUF
;; @desc 512-byte fallback environment block for MS-DOS 1.25 (when wEnvSeg == 0).
@const TRN_ENV_BUF = 0x84A0

;; @const TRN_CMD_WORK
;; @desc 1,792-byte dedicated command I/O scratch buffer isolated from the AST arena.
@const TRN_CMD_WORK = 0x86A0

;; @const TRN_ARENA_START
;; @desc Start offset of dynamic statement & AST bump arena inside wTrnSeg (0x8DA0..0x9FFE).
@const TRN_ARENA_START = 0x8DA0

;; @const TRN_ARENA_END
;; @desc End offset (canary location) of dynamic statement bump arena inside wTrnSeg.
@const TRN_ARENA_END = 0x9FFE

;; @const TRN_STACK_TOP
;; @desc Top of transient stack inside wTrnSeg (0xA1FE, even-aligned).
@const TRN_STACK_TOP = 0xA1FE

; ============================================================================
; SECTION 1: RESIDENT (ORG 0x0100, <= 2,816 bytes low conventional memory)
; ============================================================================
@section resident

;; @proc res_entry
;; @abi dos16
;; @in CS=DS=ES=SS=PSP
;; @out Transfers control to INIT at (PSP + 0x10 + RES_PARAS + TRN_PARAS):0x0000
;; @err none
;; @clobbers ax, bx, cx, dx, si, di, ss
;; @desc Entrypoint at PSP:0100h followed immediately by 8-byte WINCMD IPC signature.
@proc res_entry
    jmp res_boot_trampoline
    nop
@data db bResSignature, "WINCMD", 0x1A, 0x01
res_boot_trampoline:
    cld
    mov bx, cs
    cli
    mov ss, bx
    mov sp, addr(bResMiniStk + 36)
    sti
    mov dx, addr(res_post_init)
    mov si, addr(resState)
    mov cx, addr(wEmsFrameSeg)
    mov di, addr(wXmsEntryOff)
    mov ax, bx
    add ax, (0x0010 + RES_PARAS + TRN_PARAS)
    mov word [cs:wInitLaunchOff], 0
    mov word [cs:wInitLaunchSeg], ax
    jmp far [cs:wInitLaunchOff]
@endproc

@data dw wInitLaunchOff, 0x0000
@data dw wInitLaunchSeg, 0x0000
@res byte resState, 128
@res byte batchResStack, 192
@data dw wTrnEntryOff, 0x0000
@data dw wTrnEntrySeg, 0x0000
@data dw wSavedSS, 0x0000
@data dw wSavedSP, 0x0000
@data db bNeedTrnCheck, 1
@data dw wResSavedSS, 0x0000
@data dw wResSavedSP, 0x0000
@res byte bResSavedStk, 16
@res byte bResMiniStk, 36
@res byte fcbResWork, 37
@res byte bResWorkBuf, 48

;; @proc res_install_vecs
;; @abi dos16
;; @in CS = RESIDENT PSP segment
;; @out Installs resident INT 23h (Ctrl+Break) and INT 24h (Critical Error) vectors
;; @err none
;; @clobbers ax, dx
;; @desc Hooks INT 23h and INT 24h to resident ISRs on cold boot and after child exit.
@proc res_install_vecs
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, addr(int23_isr)
    mov ax, 0x2523
    int 0x21
    mov dx, addr(int24_isr)
    mov ax, 0x2524
    int 0x21
    pop ds
    ret
@endproc

@include "res/int_23.inc"
@include "res/int_24.inc"
@include "res/red_125.inc"
@include "res/ldr_125.inc"
@include "res/exec_200.inc"
@include "res/ems_40.inc"

;; @proc res_post_init
;; @abi dos16
;; @in CS=PSP, [resState] populated by INIT
;; @out Shrinks MCB to RESIDENT, hooks INT 23h/24h, runs supervisor loop, exits cleanly
;; @err Exits with 1 on unrecoverable transient reload failure
;; @clobbers ax, bx, cx, dx, si, di, ss
;; @desc Resident supervisor lifecycle entered once INIT finishes relocating TRANSIENT.
@proc res_post_init
    push ds
    push es
    mov ax, cs
    mov ds, ax
    mov es, ax

    mov ax, word [resState + RES_STATE.wTrnSeg]
    mov word [wTrnEntrySeg], ax
    mov word [wTrnEntryOff], 0

    ; Shrink PSP MCB down to PSP (0x10) + RES_PARAS on DOS 2.00+
    cmp byte [resState + RES_STATE.bDosMajor], 2
    jb .hooks
    mov bx, (0x0010 + RES_PARAS)
    mov ah, DOS_RESIZE_MEM
    int 0x21

.hooks:
    call res_install_vecs
    call res_seed_trn_scratch
    push es
    mov es, word [wTrnEntrySeg]
    mov di, TRN_HIST_BUF
    mov cx, 64
    xor ax, ax
    cld
    rep stosw
    pop es

.shell_loop:
    cmp byte [bNeedTrnCheck], 0
    je .trn_verified
    call res_ensure_transient
    jc .fatal_reload
    mov byte [bNeedTrnCheck], 0
.trn_verified:

    mov word [cs:wSavedSS], ss
    mov word [cs:wSavedSP], sp
    mov ax, word [cs:wTrnEntrySeg]
    cli
    mov ss, ax
    mov sp, TRN_STACK_TOP
    sti
    mov ds, ax
    mov es, ax
    call far [cs:wTrnEntryOff]
    cli
    mov ss, word [cs:wSavedSS]
    mov sp, word [cs:wSavedSP]
    sti
    mov ax, cs
    mov ds, ax
    mov es, ax

    ; Continue looping while any resident .BAT frame remains active
    cmp byte [resState + RES_STATE.bBatchDepth], 0
    jne .shell_loop

    ; Physical console 0x1A EOF exits cleanly
    push es
    mov es, word [wTrnEntrySeg]
    mov al, byte [es:bTrnConEof]
    pop es
    test al, al
    jnz .terminate

    ; Single-command (/C) terminates once command and any .BAT frames complete
    cmp byte [resState + RES_STATE.bSingleCmd], 1
    je .terminate
    ; Permanent shell (/P) ignores EXIT and keeps running interactive loop
    cmp byte [resState + RES_STATE.bPermShell], 1
    je .shell_loop
    push es
    mov es, word [wTrnEntrySeg]
    mov al, byte [es:bTrnExitReq]
    pop es
    test al, al
    jnz .terminate
    jmp .shell_loop

.fatal_reload:
    mov dx, addr(szReloadFatal)
    mov ah, DOS_PRINT_STR
    int 0x21
    mov byte [resState + RES_STATE.dwLastExitCode], 1

.terminate:
    cmp byte [resState + RES_STATE.bDosMajor], 2
    jb .exit_125
    mov al, byte [resState + RES_STATE.dwLastExitCode]
    mov ah, DOS_EXIT
    int 0x21
    pop es
    pop ds
    ret
.exit_125:
    int 0x20
    pop es
    pop ds
    ret
@endproc

@data db szReloadFatal, "Cannot load WINCMD", 0x0D, 0x0A, '$'

; ============================================================================
; SECTION 2: TRANSIENT (vstart 0x0000, relocated to top of conventional RAM)
; ============================================================================
@section transient

;; @proc trn_entry
;; @abi dos16
;; @in CS=DS=ES=SS=wTrnSeg, SP=TRN_STACK_TOP
;; @out Executes startup tail, active .BAT step, or interactive prompt line, then RETF
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Far entrypoint at wTrnSeg:0000h invoked by RESIDENT supervisor.
@proc trn_entry
    cld
    mov ax, TRN_ARENA_START
    mov dx, TRN_ARENA_END
    call arena_init
    call nls_refresh_dbcs

    ; 1. Check if resident batch stack has an active .BAT frame
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    pop es
    test al, al
    jz .chk_startup_buf

    call trn_step_batch
    jc .sync_out
    cmp byte [bTrnEcho], 0
    je .run_bat_ln
    cmp byte [TRN_CMD_BUF], '@'
    je .run_bat_ln
    cmp byte [TRN_CMD_BUF], 0
    je .run_bat_ln
    call trn_print_prompt
    mov si, TRN_CMD_BUF
    call con_write_asciiz
    call con_write_crlf
.run_bat_ln:
    mov si, TRN_CMD_BUF
    call trn_exec_line
    mov byte [TRN_CMD_BUF], 0
    jmp .sync_out

.chk_startup_buf:
    cmp byte [TRN_CMD_BUF], 0
    je .chk_interactive
    mov si, TRN_CMD_BUF
    call trn_exec_line
    mov byte [TRN_CMD_BUF], 0
    jmp .sync_out

.chk_interactive:
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bSingleCmd]
    pop es
    test al, al
    jnz .sync_out

    cmp byte [bTrnEcho], 0
    je .read_interactive
    call trn_print_prompt
.read_interactive:
    mov di, TRN_CMD_BUF
    call led_read_line
    jc .hit_con_eof
    mov si, TRN_CMD_BUF
    call trn_exec_line
    mov byte [TRN_CMD_BUF], 0
    jmp .sync_out

.hit_con_eof:
    mov byte [bTrnConEof], 1

.sync_out:
    push es
    mov es, word [wTrnResSeg]
    mov ax, word [wTrnExitCode]
    mov word [es:resState + RES_STATE.dwLastExitCode], ax
    mov al, byte [bTrnEcho]
    mov byte [es:resState + RES_STATE.bEchoFlag], al
    cmp byte [bTrnExitReq], 1
    jne .keep_chk
    mov byte [es:resState + RES_STATE.bKeepShell], 0
.keep_chk:
    pop es
    retf
@endproc

@include "core/crc16.inc"
@include "core/arena.inc"
@include "core/math32.inc"
@include "core/math64.inc"
@include "core/str_8086.inc"
@include "core/cp_dbcs.inc"
@include "core/err_msg.inc"
@include "hal/k_dos125.inc"
@include "hal/k_dos200.inc"
@include "hal/k_dos400.inc"
@include "hal/k_dos700.inc"
@include "vfs/devices.inc"
@include "vfs/pathnorm.inc"
@include "vfs/glob_dp.inc"
@include "read/line_ed.inc"
@include "read/stream.inc"
@include "parse/var_ops.inc"
@include "parse/modifier.inc"
@include "parse/phase1.inc"
@include "parse/lexer.inc"
@include "parse/prs_ast.inc"
@include "parse/phase34.inc"
@include "rt/arith16.inc"
@include "rt/env_stk.inc"
@include "cmd/m2_cmds.inc"
@include "cmd/m4_cmds.inc"

;; @proc trn_skip_ws
;; @abi dos16
;; @in DS:SI = line cursor
;; @out DS:SI = non-space/tab/delim cursor, AL = [SI]
;; @err none
;; @clobbers ax, si
;; @desc Skips leading spaces, tabs, and leading '@' silent prefix.
@proc trn_skip_ws
.lp:
    mov al, byte [si]
    cmp al, 0x20
    je .adv
    cmp al, 0x09
    je .adv
    cmp al, '@'
    je .adv
    ret
.adv:
    inc si
    jmp .lp
@endproc

;; @proc trn_match_word
;; @abi dos16
;; @in DS:SI = command cursor, DS:DI = uppercase ASCIIZ keyword
;; @out CF=0 and SI past keyword if matched on word boundary; CF=1 and SI restored otherwise
;; @err none
;; @clobbers ax, bx, si, di
;; @desc Case-insensitive command verb matcher respecting space/dot/slash/EOL delimiters.
@proc trn_match_word
    push si
.lp:
    mov bl, byte [di]
    test bl, bl
    jz .chk_delim
    mov al, byte [si]
    call chr_to_upper
    cmp al, bl
    jne .no
    inc si
    inc di
    jmp .lp
.chk_delim:
    mov al, byte [si]
    cmp al, 0x00
    je .yes
    cmp al, 0x0D
    je .yes
    cmp al, 0x20
    je .yes
    cmp al, 0x09
    je .yes
    cmp al, '.'
    je .yes
    cmp al, '/'
    je .yes
    cmp al, '('
    je .yes
    cmp al, ':'
    je .yes
    cmp al, '<'
    je .yes
    cmp al, '>'
    je .yes
    call lex_is_echo_delim
    jnc .yes
.no:
    pop si
    stc
    ret
.yes:
    pop ax
    clc
    ret
@endproc

;; @proc cmd_ver
;; @abi dos16
;; @in [bTrnDosMajor], [bTrnDosMinor]
;; @out Prints era-accurate WINCMD + MS-DOS version banner, [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements the internal VER command across MS-DOS 1.25, 2.00, 4.00, and 7.x.
@proc cmd_ver
    mov si, addr(szVerPrefix)
    call con_write_asciiz

    xor dx, dx
    mov al, byte [bTrnDosMajor]
    xor ah, ah
    mov di, szNumScratch
    call fmt_i32_dec
    mov dx, di
    call con_write_buf

    mov dx, addr(szDot)
    mov cx, 1
    call con_write_buf

    mov al, byte [bTrnDosMinor]
    cmp al, 10
    jae .min_two
    push ax
    mov dx, addr(szZeroChar)
    mov cx, 1
    call con_write_buf
    pop ax
.min_two:
    xor ah, ah
    xor dx, dx
    mov di, szNumScratch
    call fmt_i32_dec
    mov dx, di
    call con_write_buf

    mov si, addr(szVerSuffix)
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_echo
;; @abi dos16
;; @in DS:SI = cursor immediately after "ECHO"
;; @out Prints message or ECHO status, [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements ECHO, all 10 ECHO<delim> forms, ECHO ON, ECHO OFF, and ECHO <text>.
@proc cmd_echo
    mov al, byte [si]
    cmp al, 0x00
    je .show_state
    cmp al, 0x0D
    je .show_state
    call lex_is_echo_delim
    jnc .delim_echo

    inc si ; consume single separator space/tab after ECHO
    push si
    call lex_skip_hspace
    cmp al, 0x00
    je .pop_show_state
    mov di, addr(szKwOn)
    call trn_match_word
    jnc .chk_on_only
    mov di, addr(szKwOff)
    call trn_match_word
    jnc .chk_off_only
    pop si
    jmp .emit_msg

.delim_echo:
    inc si
    cmp byte [si], 0x00
    je .blank_ln
    cmp byte [si], 0x0D
    je .blank_ln
    jmp .emit_msg

.pop_show_state:
    pop ax
    jmp .show_state

.chk_on_only:
    call lex_skip_hspace
    cmp al, 0x00
    jne .not_toggle
    pop ax
    mov byte [bTrnEcho], 1
    mov word [wTrnExitCode], 0
    ret

.chk_off_only:
    call lex_skip_hspace
    cmp al, 0x00
    jne .not_toggle
    pop ax
    mov byte [bTrnEcho], 0
    mov word [wTrnExitCode], 0
    ret

.not_toggle:
    pop si
    jmp .emit_msg

.show_state:
    mov si, addr(szEchoIsOn)
    cmp byte [bTrnEcho], 0
    jne .pr_st
    mov si, addr(szEchoIsOff)
.pr_st:
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret

.blank_ln:
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret

.emit_msg:
    mov dx, si
    xor cx, cx
.cnt_lp:
    mov al, byte [si]
    cmp al, 0x00
    je .write_it
    cmp al, 0x0D
    je .write_it
    inc si
    inc cx
    jmp .cnt_lp
.write_it:
    call con_write_buf
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc cmd_set
;; @abi dos16
;; @in DS:SI = cursor after "SET"
;; @out Mutates/lists live environment block or evaluates SET /A [VAR=]expr / SET /P VAR=prompt
;; @err Prints diagnostic and sets [wTrnExitCode]=1 on invalid SET /A or syntax error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements SET [var=[val]], SET "var=val", SET /P, and 32-bit SET /A.
@proc cmd_set
    push bp
    mov bp, sp
    sub sp, 6
    ; [bp-2]=lhs_ptr, [bp-4]=lhs_len, [bp-6]=quoted_assign
    mov word [bp - 2], 0
    mov word [bp - 4], 0
    mov word [bp - 6], 0

    call lex_skip_hspace
    cmp al, 0x00
    je .list_all_env
    cmp al, '/'
    je .chk_set_sw
    jmp .assign_or_prefix

.chk_set_sw:
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'A'
    jne .chk_set_p
    jmp .do_set_a
.chk_set_p:
    cmp ah, 'P'
    je .do_set_p
    jmp .assign_or_prefix

.list_all_env:
    call env_ensure_block
    push es
    mov es, ax
    xor bx, bx
    mov cx, ds
    cmp ax, cx
    jne .le_lp
    mov bx, TRN_ENV_BUF
.le_lp:
    cmp byte [es:bx], 0
    je .le_done
    mov di, TRN_CMD_WORK
.le_cp:
    mov al, byte [es:bx]
    mov byte [di], al
    inc bx
    inc di
    test al, al
    jnz .le_cp
    cmp byte [TRN_CMD_WORK], '='
    je .le_lp
    push bx
    mov si, TRN_CMD_WORK
    call con_write_asciiz
    call con_write_crlf
    pop bx
    jmp .le_lp
.le_done:
    pop es
    mov word [wTrnExitCode], 0
    add sp, 6
    pop bp
    ret

.do_set_p:
    add si, 2
    call lex_skip_hspace
    cmp al, '"'
    jne .sp_name
    inc si
    mov word [bp - 6], 1
.sp_name:
    mov word [bp - 2], si
    xor cx, cx
.sp_nlp:
    mov al, byte [si]
    test al, al
    jz .j_set_syn1
    cmp al, '='
    je .sp_got_eq
    inc si
    inc cx
    jmp .sp_nlp
.j_set_syn1:
    jmp .set_syn_err
.sp_got_eq:
    mov word [bp - 4], cx
    inc si
    ; Print prompt (stripping trailing '"' if quoted)
    mov di, TRN_CMD_WORK
.sp_pr_cp:
    mov al, byte [si]
    test al, al
    jz .sp_pr_end
    cmp al, '"'
    je .sp_pr_end
    mov byte [di], al
    inc si
    inc di
    jmp .sp_pr_cp
.sp_pr_end:
    mov byte [di], 0
    mov si, TRN_CMD_WORK
    call con_write_asciiz
    mov di, TRN_CMD_WORK
    add di, 128
    call led_read_line
    mov si, word [bp - 2]
    mov cx, word [bp - 4]
    mov dx, TRN_CMD_WORK
    add dx, 128
    call env_set_var
    mov word [wTrnExitCode], 0
    add sp, 6
    pop bp
    ret

.do_set_a:
    add si, 2
    call lex_skip_hspace
    cmp al, '"'
    jne .sa_run_eval
    inc si
    push si
.sa_strip_rq:
    cmp byte [si], 0
    je .sa_rq_dn
    cmp byte [si], '"'
    jne .sa_rq_nx
    mov byte [si], 0
    jmp .sa_rq_dn
.sa_rq_nx:
    inc si
    jmp .sa_strip_rq
.sa_rq_dn:
    pop si

.sa_run_eval:
    call sa_eval_expr
    jc .sa_err
    mov di, szNumScratch
    call fmt_i32_dec

    ; Print numeric result when NOT inside a .BAT script (interactive / /C cmd)!
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    pop es
    test al, al
    jnz .sa_ok_ret
    mov dx, di
    call con_write_buf
    call con_write_crlf
.sa_ok_ret:
    mov word [wTrnExitCode], 0
    add sp, 6
    pop bp
    ret

.sa_err:
    call err_print_code
    mov word [wTrnExitCode], 1
    add sp, 6
    pop bp
    ret

.set_syn_err:
    mov ax, ERR_SYNTAX
    call err_print_code
    mov word [wTrnExitCode], 1
    add sp, 6
    pop bp
    ret

.assign_or_prefix:
    ; Support SET "VAR=val" (strips outer quotes!) and SET VAR=val
    cmp al, '"'
    jne .scan_plain_eq
    inc si
    mov word [bp - 6], 1
.scan_plain_eq:
    mov bx, si
    xor cx, cx
.peq_lp:
    mov al, byte [si]
    test al, al
    jz .filter_pfx
    cmp al, 0x0D
    je .filter_pfx
    cmp al, '='
    jne .chk_peq_q
    jmp .got_plain_eq
.chk_peq_q:
    cmp al, '"'
    jne .peq_adv
    cmp word [bp - 6], 0
    jne .filter_pfx
.peq_adv:
    inc si
    inc cx
    jmp .peq_lp

.filter_pfx:
    ; Trim trailing spaces from prefix [DS:BX, CX]
.trim_pfx:
    test cx, cx
    jnz .chk_pfx_sp
    jmp .set_syn_err
.chk_pfx_sp:
    mov di, bx
    add di, cx
    cmp byte [di - 1], 0x20
    jne .run_pfx_scan
    dec cx
    jmp .trim_pfx

.run_pfx_scan:
    mov word [bp - 2], bx
    mov word [bp - 4], cx
    call env_ensure_block
    push es
    mov es, ax
    xor bx, bx
    mov dx, ds
    cmp ax, dx
    jne .fp_init
    mov bx, TRN_ENV_BUF
.fp_init:
    xor dx, dx ; match count
.fp_lp:
    cmp byte [es:bx], 0
    je .fp_done
    mov di, TRN_CMD_WORK
.fp_cp:
    mov al, byte [es:bx]
    mov byte [di], al
    inc bx
    inc di
    test al, al
    jnz .fp_cp

    ; Skip hidden '=D:' entries unless prefix itself begins with '='
    mov si, word [bp - 2]
    cmp byte [si], '='
    je .fp_cmp_st
    cmp byte [TRN_CMD_WORK], '='
    je .fp_lp

.fp_cmp_st:
    mov cx, word [bp - 4]
    mov di, TRN_CMD_WORK
.fp_cmp_ch:
    mov al, byte [di]
    test al, al
    jz .fp_lp
    cmp al, '='
    je .fp_lp
    call chr_to_upper
    mov ah, al
    mov al, byte [si]
    call chr_to_upper
    cmp al, ah
    jne .fp_lp
    inc si
    inc di
    loop .fp_cmp_ch

    inc dx
    push bx
    push dx
    mov si, TRN_CMD_WORK
    call con_write_asciiz
    call con_write_crlf
    pop dx
    pop bx
    jmp .fp_lp

.fp_done:
    pop es
    test dx, dx
    jz .fp_none
    mov word [wTrnExitCode], 0
    add sp, 6
    pop bp
    ret
.fp_none:
    mov ax, ERR_FILE_NOT_FOUND
    call trn_err_dos
    mov word [wTrnExitCode], 1
    add sp, 6
    pop bp
    ret

.got_plain_eq:
    test cx, cx
    jnz .have_lhs_len
    jmp .set_syn_err
.have_lhs_len:
    inc si
    ; Copy value to TRN_CMD_WORK (stripping trailing '"' if [bp-6] == 1!)
    mov di, TRN_CMD_WORK
.pv_cp:
    mov al, byte [si]
    test al, al
    jz .pv_end
    cmp al, 0x0D
    je .pv_end
    cmp word [bp - 6], 0
    je .pv_put
    cmp al, '"'
    je .pv_end
.pv_put:
    mov byte [di], al
    inc si
    inc di
    jmp .pv_cp
.pv_end:
    mov byte [di], 0
    mov si, bx
    mov dx, TRN_CMD_WORK
    call env_set_var
    jnc .pv_ok
    jmp .sa_err
.pv_ok:
    mov word [wTrnExitCode], 0
    add sp, 6
    pop bp
    ret
@endproc

;; @proc cmd_exit
;; @abi dos16
;; @in DS:SI = cursor after "EXIT"
;; @out Sets [bTrnExitReq]=1 (and pops batch frame on /B) and optional [wTrnExitCode]
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements EXIT [/B] [exitCode] with automatic SETLOCAL unwinding.
@proc cmd_exit
    call trn_skip_ws
    cmp al, '/'
    jne .full_exit
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'B'
    jne .full_exit
    add si, 2
    push es
    mov es, word [wTrnResSeg]
    cmp byte [es:resState + RES_STATE.bBatchDepth], 0
    je .no_pop_b
    dec byte [es:resState + RES_STATE.bBatchDepth]
.no_pop_b:
    pop es
    push si
    call env_unwind_batch_locals
    pop si
    jmp .parse_ec

.full_exit:
    mov byte [bTrnExitReq], 1
    push es
    mov es, word [wTrnResSeg]
    mov byte [es:resState + RES_STATE.bBatchDepth], 0
    pop es
    push si
    call env_unwind_batch_locals
    pop si
.parse_ec:
    call trn_skip_ws
    cmp al, 0x00
    je .done
    cmp al, 0x0D
    je .done
    call parse_i32_lit
    jc .done
    mov word [wTrnExitCode], ax
.done:
    ret
@endproc

;; @proc trn_exec_line
;; @abi dos16
;; @in DS:SI = raw command line (NUL or CR terminated)
;; @out Expands Phase 1, accumulates multiline (...) blocks, builds Arena AST, and executes it
;; @err Prints diagnostic and sets [wTrnExitCode]=1 on syntax error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Top-level Phase 1 -> Phase 2 AST -> Phase 3/4 execution pipeline.
@proc trn_exec_line
    push bp
    mov bp, sp
    sub sp, 10
    ; [bp-2]=blk_buf (512B), [bp-4]=blk_len, [bp-6]=line_p1 (256B), [bp-8]=paren_depth, [bp-10]=arena_mark
    mov ax, word [wArenaPtr]
    mov word [bp - 10], ax

    push si
    mov cx, 780
    call arena_alloc
    pop si
    jnc .exec_ln_mem
    jmp .done_exec_ln
.exec_ln_mem:
    mov word [bp - 2], di
    add di, 516
    mov word [bp - 6], di

    ; Phase 1 immediate '%' expansion on first physical line!
    mov di, word [bp - 2]
    call phase1_expand
    mov word [bp - 4], cx

    ; Count open '(' depth; if > 0, accumulate continuation lines until balanced!
    xor cx, cx
    mov si, word [bp - 2]
    call lex_count_paren_delta
    mov word [bp - 8], cx

.more_blk_lp:
    cmp word [bp - 8], 0
    je .parse_ast_now

    ; Read next physical line from active batch stream or interactive prompt!
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    pop es
    test al, al
    jz .read_more_con
    mov di, TRN_CMD_BUF
    call stream_read_batch_line
    jc .parse_ast_now
    jmp .got_cont_ln

.read_more_con:
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bSingleCmd]
    pop es
    test al, al
    jnz .parse_ast_now
    mov di, TRN_CMD_BUF
    call led_read_line
    jc .parse_ast_now

.got_cont_ln:
    ; Phase 1 expand continuation line at block-read time (authentic cmd.exe timing!)
    mov si, TRN_CMD_BUF
    mov di, word [bp - 6]
    call phase1_expand

    mov cx, word [bp - 8]
    mov si, word [bp - 6]
    call lex_count_paren_delta
    mov word [bp - 8], cx

    ; Append '\n' + [bp-6] into [bp-2] (up to 508 bytes)
    mov di, word [bp - 2]
    mov bx, word [bp - 4]
    cmp bx, 506
    jae .more_blk_lp
    mov byte [di + bx], 0x0A
    inc bx
    mov si, word [bp - 6]
.app_blk_ch:
    mov al, byte [si]
    test al, al
    jz .app_blk_dn
    cmp bx, 508
    jae .app_blk_dn
    mov byte [di + bx], al
    inc si
    inc bx
    jmp .app_blk_ch
.app_blk_dn:
    mov byte [di + bx], 0
    mov word [bp - 4], bx
    jmp .more_blk_lp

.parse_ast_now:
    mov si, word [bp - 2]
    call prs_parse_line
    jc .ast_syn_err
    call ast_exec_node
    jmp .done_exec_ln

.ast_syn_err:
    call err_print_code
    mov word [wTrnExitCode], 1

.done_exec_ln:
    mov ax, word [bp - 10]
    mov word [wArenaPtr], ax
    add sp, 10
    pop bp
    ret
@endproc

;; @proc trn_exec_stmt
;; @abi dos16
;; @in DS:SI = command string (redirection tokens already stripped)
;; @out Dispatches built-in verb, CALL, or external .COM/.EXE/.BAT
;; @err Prints diagnostic on unrecognized command
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Inner verb dispatcher safe for recursive invocation from AST nodes and CALL.
@proc trn_exec_stmt
    call trn_skip_ws
    cmp al, 0x00
    jne .chk_qmark
    ret

.chk_qmark:
    cmp al, '/'
    jne .verbs
    cmp byte [si + 1], '?'
    jne .verbs
    mov si, addr(szHelpBanner)
    call con_write_asciiz
    call con_write_crlf
    mov word [wTrnExitCode], 0
    ret

.verbs:
    mov di, addr(szKwVer)
    call trn_match_word
    jc .nx_echo
    call cmd_ver
    jmp .cleanup_done
.nx_echo:
    mov di, addr(szKwEcho)
    call trn_match_word
    jc .nx_set
    call cmd_echo
    jmp .cleanup_done
.nx_set:
    mov di, addr(szKwSet)
    call trn_match_word
    jc .nx_goto
    call cmd_set
    jmp .cleanup_done
.nx_goto:
    mov di, addr(szKwGoto)
    call trn_match_word
    jc .nx_shift
    call cmd_goto
    jmp .cleanup_done
.nx_shift:
    mov di, addr(szKwShift)
    call trn_match_word
    jc .nx_exit
    call cmd_shift
    jmp .cleanup_done
.nx_exit:
    mov di, addr(szKwExit)
    call trn_match_word
    jc .nx_cls
    call cmd_exit
    jmp .cleanup_done
.nx_cls:
    mov di, addr(szKwCls)
    call trn_match_word
    jc .nx_dir
    call con_write_crlf
    mov word [wTrnExitCode], 0
    jmp .cleanup_done
.nx_dir:
    mov di, addr(szKwDir)
    call trn_match_word
    jc .nx_type
    call cmd_dir
    jmp .cleanup_done
.nx_type:
    mov di, addr(szKwType)
    call trn_match_word
    jc .nx_copy
    call cmd_type
    jmp .cleanup_done
.nx_copy:
    mov di, addr(szKwCopy)
    call trn_match_word
    jc .nx_del
    call cmd_copy_m4
    jmp .cleanup_done
.nx_del:
    mov di, addr(szKwDel)
    call trn_match_word
    jnc .do_del
    mov di, addr(szKwErase)
    call trn_match_word
    jc .nx_ren
.do_del:
    call cmd_del
    jmp .cleanup_done
.nx_ren:
    mov di, addr(szKwRen)
    call trn_match_word
    jnc .do_ren
    mov di, addr(szKwRename)
    call trn_match_word
    jc .nx_md
.do_ren:
    call cmd_ren
    jmp .cleanup_done
.nx_md:
    mov di, addr(szKwMd)
    call trn_match_word
    jnc .do_md
    mov di, addr(szKwMkdir)
    call trn_match_word
    jc .nx_rd
.do_md:
    call cmd_mkdir
    jmp .cleanup_done
.nx_rd:
    mov di, addr(szKwRd)
    call trn_match_word
    jnc .do_rd
    mov di, addr(szKwRmdir)
    call trn_match_word
    jc .nx_cd
.do_rd:
    call cmd_rmdir
    jmp .cleanup_done
.nx_cd:
    mov di, addr(szKwCd)
    call trn_match_word
    jnc .do_cd
    mov di, addr(szKwChdir)
    call trn_match_word
    jc .nx_chcp
.do_cd:
    xor dl, dl
    call cmd_cd_ex
    jmp .cleanup_done
.nx_chcp:
    mov di, addr(szKwChcp)
    call trn_match_word
    jc .nx_true
    call cmd_chcp
    jmp .cleanup_done
.nx_true:
    mov di, addr(szKwTruename)
    call trn_match_word
    jc .nx_title
    call cmd_truename
    jmp .cleanup_done
.nx_title:
    mov di, addr(szKwTitle)
    call trn_match_word
    jc .nx_vol
    call cmd_title
    jmp .cleanup_done
.nx_vol:
    mov di, addr(szKwVol)
    call trn_match_word
    jc .nx_move
    call cmd_vol
    jmp .cleanup_done
.nx_move:
    mov di, addr(szKwMove)
    call trn_match_word
    jc .nx_path
    call cmd_move
    jmp .cleanup_done
.nx_path:
    mov di, addr(szKwPath)
    call trn_match_word
    jc .nx_dpath
    call cmd_path
    jmp .cleanup_done
.nx_dpath:
    mov di, addr(szKwDpath)
    call trn_match_word
    jc .nx_prompt
    call cmd_dpath
    jmp .cleanup_done
.nx_prompt:
    mov di, addr(szKwPrompt)
    call trn_match_word
    jc .nx_pause
    call cmd_prompt
    jmp .cleanup_done
.nx_pause:
    mov di, addr(szKwPause)
    call trn_match_word
    jc .nx_break
    call cmd_pause
    jmp .cleanup_done
.nx_break:
    mov di, addr(szKwBreak)
    call trn_match_word
    jc .nx_verify
    call cmd_break
    jmp .cleanup_done
.nx_verify:
    mov di, addr(szKwVerify)
    call trn_match_word
    jc .nx_date
    call cmd_verify
    jmp .cleanup_done
.nx_date:
    mov di, addr(szKwDate)
    call trn_match_word
    jc .nx_time
    call cmd_date
    jmp .cleanup_done
.nx_time:
    mov di, addr(szKwTime)
    call trn_match_word
    jc .nx_color
    call cmd_time
    jmp .cleanup_done
.nx_color:
    mov di, addr(szKwColor)
    call trn_match_word
    jc .nx_assoc
    call cmd_color
    jmp .cleanup_done
.nx_assoc:
    mov di, addr(szKwAssoc)
    call trn_match_word
    jc .nx_ftype
    call cmd_assoc
    jmp .cleanup_done
.nx_ftype:
    mov di, addr(szKwFtype)
    call trn_match_word
    jc .nx_keys
    call cmd_ftype
    jmp .cleanup_done
.nx_keys:
    mov di, addr(szKwKeys)
    call trn_match_word
    jc .nx_ctty
    call cmd_keys
    jmp .cleanup_done
.nx_ctty:
    mov di, addr(szKwCtty)
    call trn_match_word
    jc .nx_doskey
    call cmd_ctty
    jmp .cleanup_done
.nx_doskey:
    mov di, addr(szKwDoskey)
    call trn_match_word
    jc .nx_mode
    call cmd_doskey
    jmp .cleanup_done
.nx_mode:
    mov di, addr(szKwMode)
    call trn_match_word
    jc .nx_start
    call cmd_mode
    jmp .cleanup_done
.nx_start:
    mov di, addr(szKwStart)
    call trn_match_word
    jc .nx_mklink
    call cmd_start
    jmp .cleanup_done
.nx_mklink:
    mov di, addr(szKwMklink)
    call trn_match_word
    jc .nx_setlocal
    call cmd_mklink
    jmp .cleanup_done
.nx_setlocal:
    mov di, addr(szKwSetlocal)
    call trn_match_word
    jc .nx_endlocal
    call cmd_setlocal
    jmp .cleanup_done
.nx_endlocal:
    mov di, addr(szKwEndlocal)
    call trn_match_word
    jc .nx_pushd
    call cmd_endlocal
    jmp .cleanup_done
.nx_pushd:
    mov di, addr(szKwPushd)
    call trn_match_word
    jc .nx_popd
    call cmd_pushd
    jmp .cleanup_done
.nx_popd:
    mov di, addr(szKwPopd)
    call trn_match_word
    jc .nx_lh
    call cmd_popd
    jmp .cleanup_done
.nx_lh:
    mov di, addr(szKwLh)
    call trn_match_word
    jnc .do_lh
    mov di, addr(szKwLoadhigh)
    call trn_match_word
    jc .nx_subsh
.do_lh:
    call cmd_loadhigh
    jmp .cleanup_done
.nx_subsh:
    mov di, addr(szKwCmd)
    call trn_match_word
    jnc .do_subsh
    mov di, addr(szKwCommand)
    call trn_match_word
    jc .nx_call
.do_subsh:
    call cmd_subshell
    jmp .cleanup_done
.nx_call:
    mov di, addr(szKwCall)
    call trn_match_word
    jc .nx_drv_chg
    mov byte [bTrnIsCall], 1
    call trn_skip_ws
    cmp al, ':'
    je .call_sub_lbl
    jmp .verbs

.call_sub_lbl:
    ; CALL :label [args] -> push subroutine batch frame cloning current szPath, then cmd_goto!
    push si ; save ':label args'
    inc si
    ; Advance BX past label token to find subroutine args!
    mov bx, si
.sk_clbl:
    mov al, byte [bx]
    test al, al
    jz .got_cargs
    cmp al, 0x20
    je .got_cargs
    cmp al, 0x09
    je .got_cargs
    inc bx
    jmp .sk_clbl
.got_cargs:
    ; Copy active frame's szPath into TRN_CMD_WORK + 1600, then call trn_push_batch_frame!
    push bx
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    test al, al
    jz .no_act_bat
    dec al
    xor ah, ah
    mov cl, 48
    mul cl
    mov si, addr(batchResStack)
    add si, ax
    add si, BATCH_FRAME.szPath
    mov di, TRN_CMD_WORK
    add di, 1600
    mov cx, 42
.cp_cur_p:
    mov al, byte [es:si]
    mov byte [di], al
    inc si
    inc di
    loop .cp_cur_p
    pop es
    pop bx ; BX = arg_tail
    mov si, TRN_CMD_WORK
    add si, 1600
    call trn_push_batch_frame
    pop si ; ':label ...'
    jc .cleanup_done
    call cmd_goto
    jmp .cleanup_done

.no_act_bat:
    pop es
    pop bx
    pop si
    jmp .cleanup_done

.nx_drv_chg:
    ; Check bare drive selection 'A:'..'Z:' (followed only by optional whitespace/EOL)
    cmp byte [si + 1], ':'
    jne .nx_ext
    mov al, byte [si]
    call chr_to_upper
    cmp al, 'A'
    jb .nx_ext
    cmp al, 'Z'
    ja .nx_ext
    push si
    add si, 2
    call trn_skip_ws
    mov ah, byte [si]
    pop si
    cmp ah, 0x00
    je .do_drv_sel
    cmp ah, 0x0D
    jne .nx_ext
.do_drv_sel:
    mov dl, al
    sub dl, 'A'
    push dx
    mov ah, DOS_SELECT_DISK
    int 0x21
    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    pop dx
    cmp al, dl
    jne .bad_drv_sel
    call sync_drive_cwd_env
    mov word [wTrnExitCode], 0
    jmp .cleanup_done
.bad_drv_sel:
    mov ax, ERR_INVALID_DRIVE
    call trn_err_dos
    mov word [wTrnExitCode], 1
    jmp .cleanup_done

.nx_ext:
    call trn_exec_external

.cleanup_done:
    ret
@endproc

@data db szCrLf, 0x0D, 0x0A
@data db szCrLf2, 0x0D, 0x0A
@data db szDot, "."
@data db szZeroChar, "0"
@data db szVerPrefix, "WINCMD Pure-ASM Shell [MS-DOS ", 0
@data db szVerSuffix, "]", 0
@data db szHelpBanner, "WINCMD [/P] [/C cmd] [/K cmd] [/S] [/Q] [/D] [/E:ON|OFF|n] [/V:ON|OFF] [/F] [/MSG]", 0
@data db szEchoIsOn, "ECHO is on", 0
@data db szEchoIsOff, "ECHO is off", 0
@data db szDevNul, "NUL"
@data db szDevCon, "CON"
@data db szDevAux, "AUX"
@data db szDevPrn, "PRN"
@data db szDevComPfx, "COM"
@data db szDevLptPfx, "LPT"
@data db szDevClock, "CLOCK$"
@data db szDevConIn, "CONIN$"
@data db szDevConOut, "CONOUT$"
@data db szSlEnExt, "ENABLEEXTENSIONS"
@data db szSlDisExt, "DISABLEEXTENSIONS"
@data db szSlEnDel, "ENABLEDELAYEDEXPANSION"
@data db szSlDisDel, "DISABLEDELAYEDEXPANSION"
@data db szCopyCatTmp, "_WCPCAT$.TMP", 0
@data db szOneFileMoved, "        1 file(s) moved", 0
@data db szNoPath, "No Path", 0
@data db szPausePrompt, "Press any key to continue . . .", 0
@data db szBreakIs, "BREAK is ", 0
@data db szVerifyIs, "VERIFY is ", 0
@data db szKeysIs, "KEYS is ", 0
@data db szCurDateIs, "The current date is: ", 0
@data db szCurTimeIs, "The current time is: ", 0
@data db szRegFileName, "WINCMD.REG", 0
@data db szModeConStatus, "Status for device CON: Columns=80 Lines=25", 0
@data db szKwRem, "REM", 0
@data db szKwVer, "VER", 0
@data db szKwEcho, "ECHO", 0
@data db szKwSet, "SET", 0
@data db szKwGoto, "GOTO", 0
@data db szKwShift, "SHIFT", 0
@data db szKwIf, "IF", 0
@data db szKwExit, "EXIT", 0
@data db szKwCls, "CLS", 0
@data db szKwDir, "DIR", 0
@data db szKwType, "TYPE", 0
@data db szKwCopy, "COPY", 0
@data db szKwDel, "DEL", 0
@data db szKwErase, "ERASE", 0
@data db szKwRen, "REN", 0
@data db szKwRename, "RENAME", 0
@data db szKwMd, "MD", 0
@data db szKwMkdir, "MKDIR", 0
@data db szKwRd, "RD", 0
@data db szKwRmdir, "RMDIR", 0
@data db szKwCd, "CD", 0
@data db szKwChdir, "CHDIR", 0
@data db szKwChcp, "CHCP", 0
@data db szKwTruename, "TRUENAME", 0
@data db szKwTitle, "TITLE", 0
@data db szKwVol, "VOL", 0
@data db szKwMove, "MOVE", 0
@data db szKwPath, "PATH", 0
@data db szKwDpath, "DPATH", 0
@data db szKwPrompt, "PROMPT", 0
@data db szKwPause, "PAUSE", 0
@data db szKwBreak, "BREAK", 0
@data db szKwVerify, "VERIFY", 0
@data db szKwDate, "DATE", 0
@data db szKwTime, "TIME", 0
@data db szKwColor, "COLOR", 0
@data db szKwAssoc, "ASSOC", 0
@data db szKwFtype, "FTYPE", 0
@data db szKwKeys, "KEYS", 0
@data db szKwCtty, "CTTY", 0
@data db szKwDoskey, "DOSKEY", 0
@data db szKwMode, "MODE", 0
@data db szKwStart, "START", 0
@data db szKwMklink, "MKLINK", 0
@data db szKwSetlocal, "SETLOCAL", 0
@data db szKwEndlocal, "ENDLOCAL", 0
@data db szKwPushd, "PUSHD", 0
@data db szKwPopd, "POPD", 0
@data db szKwLh, "LH", 0
@data db szKwLoadhigh, "LOADHIGH", 0
@data db szKwCmd, "CMD", 0
@data db szKwCommand, "COMMAND", 0
@data db szKwCall, "CALL", 0
@data db szKwOn, "ON", 0
@data db szKwOff, "OFF", 0
@data db szKwNot, "NOT", 0
@data db szKwErrLvl, "ERRORLEVEL", 0
@data db szErrFileNotFound, "File not found", 0
@data db szErrPathNotFound, "Path not found", 0
@data db szErrAccessDenied, "Access denied", 0
@data db szErrOutOfMem, "Out of memory", 0
@data db szErrDivZero, "Divide by zero error.", 0
@data db szErrBadNum, "Invalid number.", 0
@data db szErrBadCmd, "Bad command or file name", 0
@data db szErrBadDrv, "Invalid drive specification", 0
@data db szErrTooManyFiles, "Too many open files", 0
@data db szErrPathLong, "Maximum path length exceeded", 0
@data db szErrUnsupEra, "Function not supported on this DOS version", 0
@data db szErrSyntax, "The syntax of the command is incorrect.", 0

; ============================================================================
; SECTION 3: INIT (vstart 0x0000, freed via AH=4Ah MCB shrink at boot!)
; ============================================================================
@section init

;; @proc init_entry
;; @abi dos16
;; @in BX = PSP seg, DX = res_post_init, SI = resState, CX = wEmsFrameSeg, DI = wXmsEntryOff
;; @out Detects OS/HW, relocates TRANSIENT, sets up EMS/XMS swap, parses switches, jumps to BX:DX
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Cold-boot initializer discarded from conventional memory before supervisor loop starts.
@proc init_entry
    push bp
    push es
    mov bp, bx
    mov word [cs:wInitPsp], bx
    mov word [cs:wInitRetOff], dx
    mov word [cs:wInitResOff], si
    mov word [cs:wInitEmsFrameOff], cx
    mov word [cs:wInitXmsPtrOff], di

    mov es, bx
    mov word [es:si + RES_STATE.wPspSeg], bp
    mov byte [es:si + RES_STATE.bEchoFlag], 1
    mov byte [es:si + RES_STATE.bCmdExt], 1

    ; Compute top-of-memory segment for TRANSIENT from [PSP:0x0002], clamping above RESIDENT
    mov ax, word [es:PSP.wMemTop]
    mov cx, bp
    add cx, (0x0020 + RES_PARAS)
    sub ax, TRN_RESERVE_PARAS
    jc .clamp_trn
    cmp ax, cx
    jae .trn_seg_ok
.clamp_trn:
    mov ax, cx
.trn_seg_ok:
    mov word [es:si + RES_STATE.wTrnSeg], ax
    mov word [es:si + RES_STATE.wTrnParas], TRN_PARAS

    ; Copy default COMSPEC ("A:\WINCMD.COM\0") before scanning parent environment
    push ds
    mov ax, cs
    mov ds, ax
    mov di, si
    add di, RES_STATE.szComspec
    mov si, addr(szDefComspec)
    mov cx, 14
    rep movsb
    pop ds

    call init_detect_os_hw
    call init_scan_env_comspec
    call init_calc_trn_crc
    call init_setup_ems_xms
    call init_parse_tail

    ; Copy 32-word position-independent relocator stub to (wTrnSeg + TRN_RESERVE_PARAS - 0x0020):0000
    mov es, word [cs:wInitPsp]
    mov di, word [cs:wInitResOff]
    mov dx, word [es:di + RES_STATE.wTrnSeg]
    mov ax, dx
    add ax, (TRN_RESERVE_PARAS - 0x0020)
    mov es, ax
    mov word [cs:wRelocStubOff], 0
    mov word [cs:wRelocStubSeg], ax
    push ds
    push cs
    pop ds
    mov si, addr(init_reloc_stub)
    xor di, di
    mov cx, 32
    rep movsw
    pop ds

    mov bx, word [cs:wInitPsp]
    mov es, bx
    mov cx, word [cs:wInitRetOff]
    mov word [es:wInitLaunchOff], cx
    mov word [es:wInitLaunchSeg], bx
    mov ax, bx
    add ax, (0x0010 + RES_PARAS)
    pop es
    pop bp
    jmp far [cs:wRelocStubOff]
@endproc

@include "init/det_dos.inc"

;; @proc init_reloc_stub
;; @abi dos16
;; @in AX = load_trn_seg, DX = wTrnSeg, SS = wInitPsp with [ss:wInitLaunchOff] = res_post_init
;; @out Relocates TRN_BYTE_LEN bytes via overlap-safe STD/CLD REP MOVSW and jumps to res_post_init
;; @err none
;; @clobbers ax, cx, si, di
;; @desc Position-independent high-arena relocator immune to 64 KiB INIT overlap.
@proc init_reloc_stub
    push ds
    push es
    mov ds, ax
    mov es, dx
    mov cx, (TRN_BYTE_LEN / 2)
    cmp ax, dx
    jae .fwd_copy

    mov si, (TRN_BYTE_LEN - 2)
    mov di, si
    std
    rep movsw
    cld
    jmp .done_reloc

.fwd_copy:
    xor si, si
    xor di, di
    cld
    rep movsw

.done_reloc:
    pop es
    pop ds
    jmp far [ss:wInitLaunchOff]
@endproc

;; @proc init_calc_trn_crc
;; @abi dos16
;; @in [cs:wInitPsp]
;; @out Stores initial CRC-16-CCITT in [resState.wTrnCrc16]
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Computes fast algebraic tableless CRC-16-CCITT over pristine TRANSIENT image.
@proc init_calc_trn_crc
    push ds
    push es
    mov ax, word [cs:wInitPsp]
    add ax, (0x0010 + RES_PARAS)
    mov ds, ax
    xor si, si
    mov di, TRN_BYTE_LEN
    mov cl, 4
    mov dx, 0xFFFF
.b_lp:
    lodsb
    xor al, dh
    mov bl, al
    shr bl, cl
    xor al, bl
    mov dh, dl
    mov dl, al
    xor ah, ah
    shl ax, cl
    xor dh, al
    shl ax, 1
    xor dx, ax

    lodsb
    xor al, dh
    mov bl, al
    shr bl, cl
    xor al, bl
    mov dh, dl
    mov dl, al
    xor ah, ah
    shl ax, cl
    xor dh, al
    shl ax, 1
    xor dx, ax

    cmp si, di
    jb .b_lp

    mov es, word [cs:wInitPsp]
    mov di, word [cs:wInitResOff]
    mov word [es:di + RES_STATE.wTrnCrc16], dx
    pop es
    pop ds
    ret
@endproc

;; @proc init_parse_tail
;; @abi dos16
;; @in PSP:0x0080 command tail
;; @out Parses /P,/C,/K,/S,/Q,/D,/A,/U,/E,/V,/F,/MSG,/T switches and queues AUTOEXEC.BAT on /P
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Parses startup switches from PSP command tail into RES_STATE and TRANSIENT buffer.
@proc init_parse_tail
    push ds
    push es
    mov byte [cs:bInitStripQuotes], 0
    mov byte [cs:bInitSkipAuto], 0
    mov ax, word [cs:wInitPsp]
    mov ds, ax
    mov es, ax
    mov bx, word [cs:wInitResOff]
    mov si, 0x0081

.sw_lp:
    mov al, byte [si]
    cmp al, 0x20
    je .sw_ws
    cmp al, 0x09
    je .sw_ws
    cmp al, '-'
    je .chk_long_sw
    cmp al, '/'
    je .is_slash
    jmp .no_more_sw

.chk_long_sw:
    cmp byte [si + 1], '-'
    je .go_long_dialect
    jmp .no_more_sw
.go_long_dialect:
    jmp .sw_dialect

.is_slash:
    mov ah, byte [si + 1]
    cmp ah, '?'
    je .go_help_arg
    and ah, 0xDF
    cmp ah, 'P'
    je .sw_p
    cmp ah, 'C'
    je .go_sw_c
    cmp ah, 'K'
    je .go_sw_k
    cmp ah, 'S'
    je .sw_s
    cmp ah, 'Q'
    je .sw_q
    cmp ah, 'D'
    je .sw_d
    cmp ah, 'V'
    je .sw_v
    cmp ah, 'E'
    je .sw_e
    cmp ah, 'F'
    je .go_sw_f
    cmp ah, 'M'
    je .go_sw_m
    jmp .sw_skip_tok

.go_help_arg:
    jmp .is_help_arg
.go_sw_c:
    jmp .sw_c
.go_sw_k:
    jmp .sw_k
.go_sw_f:
    jmp .sw_f
.go_sw_m:
    jmp .sw_m

.sw_ws:
    inc si
    jmp .sw_lp

.sw_p:
    mov byte [bx + RES_STATE.bPermShell], 1
    add si, 2
    jmp .sw_lp

.sw_s:
    mov byte [cs:bInitStripQuotes], 1
    add si, 2
    jmp .sw_lp

.sw_q:
    mov byte [bx + RES_STATE.bEchoFlag], 0
    push es
    mov es, word [bx + RES_STATE.wTrnSeg]
    mov byte [es:bTrnEcho], 0
    pop es
    add si, 2
    jmp .sw_lp

.sw_d:
    mov byte [cs:bInitSkipAuto], 1
    add si, 2
    jmp .sw_lp

.j_sw_lp:
    jmp .sw_lp

.sw_v:
    add si, 2
    cmp byte [si], ':'
    jne .v_on
    mov al, byte [si + 2]
    and al, 0xDF
    cmp al, 'F'
    je .v_off
.v_on:
    mov byte [bx + RES_STATE.bDelayedExp], 1
    jmp .sw_skip_colon
.v_off:
    mov byte [bx + RES_STATE.bDelayedExp], 0
    jmp .sw_skip_colon

.sw_e:
    add si, 2
    cmp byte [si], ':'
    jne .j_sw_lp
    mov al, byte [si + 1]
    cmp al, '0'
    jb .e_onoff
    cmp al, '9'
    ja .e_onoff
    jmp .sw_skip_colon
.e_onoff:
    mov al, byte [si + 2]
    and al, 0xDF
    cmp al, 'F'
    jne .e_on
    mov byte [bx + RES_STATE.bCmdExt], 0
    jmp .sw_skip_colon
.e_on:
    mov byte [bx + RES_STATE.bCmdExt], 1
    jmp .sw_skip_colon

.sw_f:
    add si, 2
    cmp byte [si], ':'
    je .f_comp
    mov byte [bx + RES_STATE.bAutoFail24], 1
    jmp .sw_lp
.f_comp:
    mov al, byte [si + 2]
    and al, 0xDF
    cmp al, 'F'
    je .f_comp_off
    or byte [bx + RES_STATE.bHwFlags], HW_FILE_COMP
    jmp .sw_skip_colon
.f_comp_off:
    and byte [bx + RES_STATE.bHwFlags], 0x7F
    jmp .sw_skip_colon

.sw_m:
    mov byte [bx + RES_STATE.bMsgResident], 1
    add si, 2
    jmp .sw_tok_tail

.sw_dialect:
    ; Skip "--dialect" up to '=' or whitespace, then parse dialect token
.sk_dl_kw:
    mov al, byte [si]
    cmp al, 0x00
    je .j_copy_cmd
    cmp al, 0x0D
    je .j_copy_cmd
    cmp al, '='
    je .got_dl_sep
    cmp al, 0x20
    je .got_dl_sep
    inc si
    jmp .sk_dl_kw
.got_dl_sep:
    inc si
    cmp byte [si], 0x20
    je .got_dl_sep
    mov al, byte [si]
    and al, 0xDF
    mov ah, 5
    cmp al, 'N'
    je .set_dl_val
    mov ah, 6
    cmp al, 'F'
    je .set_dl_val
    ; Otherwise scan token for '1','2','4','7' major digit (e.g. dos125/dos200/dos400/dos700)
    mov ah, 1
.dl_dig_lp:
    mov al, byte [si]
    cmp al, 0x00
    je .j_copy_cmd
    cmp al, 0x0D
    je .j_copy_cmd
    cmp al, 0x20
    je .dl_apply_end
    cmp al, '/'
    je .dl_apply_end
    cmp al, '1'
    jne .chk_d2
    mov ah, 1
    jmp .dl_consume_end
.chk_d2:
    cmp al, '2'
    jne .chk_d4
    mov ah, 2
    jmp .dl_consume_end
.chk_d4:
    cmp al, '4'
    jne .chk_d7
    mov ah, 4
    jmp .dl_consume_end
.chk_d7:
    cmp al, '7'
    jne .nx_dl_ch
    mov ah, 7
    jmp .dl_consume_end
.nx_dl_ch:
    inc si
    jmp .dl_dig_lp
.set_dl_val:
.dl_consume_end:
    mov byte [bx + RES_STATE.bDialect], ah
    jmp .sw_tok_tail
.dl_apply_end:
    mov byte [bx + RES_STATE.bDialect], ah
    jmp .j_sw_lp2

.j_sw_lp2:
    jmp .sw_lp
.j_copy_cmd:
    jmp .copy_cmd

.sw_skip_tok:
    add si, 2
.sw_tok_tail:
    mov al, byte [si]
    cmp al, 0x00
    je .j_copy_cmd
    cmp al, 0x0D
    je .j_copy_cmd
    cmp al, 0x20
    je .j_sw_lp2
    cmp al, '/'
    je .j_sw_lp2
    inc si
    jmp .sw_tok_tail

.sw_skip_colon:
    cmp byte [si], ':'
    jne .j_sw_lp2
.col_lp:
    inc si
    mov al, byte [si]
    cmp al, 0x00
    je .j_copy_cmd
    cmp al, 0x0D
    je .j_copy_cmd
    cmp al, 0x20
    je .j_sw_lp2
    cmp al, '/'
    je .j_sw_lp2
    jmp .col_lp

.sw_c:
    mov byte [bx + RES_STATE.bSingleCmd], 1
    add si, 2
    jmp .copy_cmd

.sw_k:
    mov byte [bx + RES_STATE.bKeepShell], 1
    add si, 2
    jmp .copy_cmd

.is_help_arg:
    mov byte [bx + RES_STATE.bSingleCmd], 1
.j_cp3:
    jmp .copy_cmd

.no_more_sw:
    ; Under /P (without /D) and empty tail, queue AUTOEXEC.BAT if present on boot drive!
    cmp byte [bx + RES_STATE.bPermShell], 1
    jne .j_cp3
    cmp byte [cs:bInitSkipAuto], 1
    je .j_cp3
    cmp byte [si], 0x00
    je .chk_autoexec
    cmp byte [si], 0x0D
    jne .j_cp3

.chk_autoexec:
    cmp byte [bx + RES_STATE.bDosMajor], 2
    jb .chk_auto125
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, addr(szAutoexecName)
    mov ax, 0x3D00
    int 0x21
    pop ds
    jc .queue_ver_banner
    push bx
    mov bx, ax
    mov ah, DOS_CLOSE
    int 0x21
    pop bx
    jmp .queue_autoexec

.chk_auto125:
    push ds
    push es
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov dx, addr(fcbAutoexec125)
    mov ah, DOS_FCB_OPEN
    int 0x21
    push ax
    test al, al
    jnz .auto125_done
    mov dx, addr(fcbAutoexec125)
    mov ah, DOS_FCB_CLOSE
    int 0x21
.auto125_done:
    pop ax
    pop es
    pop ds
    test al, al
    jnz .queue_ver_banner

.queue_autoexec:
    mov es, word [bx + RES_STATE.wTrnSeg]
    mov di, TRN_CMD_BUF
    push ds
    mov ax, cs
    mov ds, ax
    mov si, addr(szAutoexecName)
    mov cx, 13
    rep movsb
    pop ds
    jmp .done_parse

.queue_ver_banner:
    mov es, word [bx + RES_STATE.wTrnSeg]
    mov di, TRN_CMD_BUF
    mov byte [es:di], 'V'
    mov byte [es:di + 1], 'E'
    mov byte [es:di + 2], 'R'
    mov byte [es:di + 3], 0
    jmp .done_parse

.copy_cmd:
    ; Skip leading spaces after /C or /K, and evaluate /S or 5-rule quote heuristic
.skip_lead_sp:
    cmp byte [si], 0x20
    jne .chk_strip_q
    inc si
    jmp .skip_lead_sp
.chk_strip_q:
    cmp byte [si], '"'
    je .got_lead_q
    jmp .raw_cp
.got_lead_q:
    cmp byte [cs:bInitStripQuotes], 0
    je .run_q5
    jmp .strip_lead_q
.run_q5:

    ; Evaluate 5-rule quote heuristic: exactly 2 quotes (first & last), 0 special chars, >=1 ws
    mov di, si
    inc di
    xor cx, cx
    xor dx, dx
.q5_lp:
    mov al, byte [di]
    cmp al, 0x00
    je .q5_eval
    cmp al, 0x0D
    je .q5_eval
    cmp al, '"'
    jne .q5_not_q
    inc cl
    jmp .q5_nx
.q5_not_q:
    cmp al, 0x20
    je .q5_ws
    cmp al, 0x09
    je .q5_ws
    cmp al, '&'
    je .q5_spec
    cmp al, '<'
    je .q5_spec
    cmp al, '>'
    je .q5_spec
    cmp al, '('
    je .q5_spec
    cmp al, ')'
    je .q5_spec
    cmp al, '@'
    je .q5_spec
    cmp al, '^'
    je .q5_spec
    cmp al, '|'
    jne .q5_nx
.q5_spec:
    mov ch, 1
    jmp .q5_nx
.q5_ws:
    mov dl, 1
.q5_nx:
    inc di
    jmp .q5_lp

.q5_eval:
    cmp cl, 1
    jne .raw_cp
    test ch, ch
    jnz .raw_cp
    test dl, dl
    jz .raw_cp
    cmp byte [di - 1], '"'
    jne .raw_cp
    mov byte [cs:bInitStripQuotes], 1

.strip_lead_q:
    inc si
.raw_cp:
    mov es, word [bx + RES_STATE.wTrnSeg]
    mov di, TRN_CMD_BUF
    mov cx, 126
.cp_lp:
    mov al, byte [si]
    cmp al, 0x00
    je .cp_done
    cmp al, 0x0D
    je .cp_done
    mov byte [es:di], al
    inc si
    inc di
    loop .cp_lp
.cp_done:
    cmp byte [cs:bInitStripQuotes], 0
    je .term_cp
    cmp di, TRN_CMD_BUF
    jbe .term_cp
    cmp byte [es:di - 1], '"'
    jne .term_cp
    dec di
.term_cp:
    mov byte [es:di], 0

.done_parse:
    pop es
    pop ds
    ret
@endproc

@data db szDefComspec, "A:", 0x5C, "WINCMD.COM", 0
@data db szAutoexecName, "AUTOEXEC.BAT", 0
@data db fcbAutoexec125, 0, "AUTOEXECBAT"
@res byte fcbAutoexecRest, 25
@data db bInitStripQuotes, 0
@data db bInitSkipAuto, 0
@data dw wInitRetOff, 0
@data dw wInitPsp, 0
@data dw wInitResOff, 0
@data dw wRelocStubOff, 0
@data dw wRelocStubSeg, 0
