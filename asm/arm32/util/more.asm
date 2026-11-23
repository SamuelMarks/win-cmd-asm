;; @file more.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 MORE.EXE interactive pager (/E, /C, /P, /S, /Tn, +n [files...], CONIN$ key polling).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc morea32_wait_key
;; @abi arm_nt
;; @in none
;; @out r0 = 1 if user quit ('Q'), 0 to continue
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prompts "-- More -- " and reads a keystroke from CONIN$.
@proc morea32_wait_key
    push {r4, lr}
    sub sp, sp, #16
    adr_sym r0, szMorea32Prompt
    bl ua32_write_str

    adr_sym r0, u16Ua32ConIn
    mov_imm32 r1, 0x80000000
    mov r2, #3
    mov r3, #0
    mov r12, #3
    str r12, [sp]
    mov r12, #0x80
    str r12, [sp, #4]
    mov r12, #0
    str r12, [sp, #8]
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .cnl
    mov r4, r0
    adr_sym r1, g_morea32_key
    mov r2, #0
    strb r2, [r1]
    mov r2, #1
    adr_sym r3, g_ua32_io_done
    mov r12, #0
    str r12, [sp]
    call_iat __imp_ReadFile
    mov r0, r4
    call_iat __imp_CloseHandle
    bl ua32_write_crlf
    adr_sym r0, g_morea32_key
    ldrb r0, [r0]
    bl ua32_chr_upper
    cmp r0, #81 ; Q
    beq .qt
    mov r0, #0
    add sp, sp, #16
    pop {r4, lr}
    bx lr
.cnl:
    bl ua32_write_crlf
    mov r0, #0
    add sp, sp, #16
    pop {r4, lr}
    bx lr
.qt:
    mov r0, #1
    add sp, sp, #16
    pop {r4, lr}
    bx lr
@endproc

;; @proc morea32_emit_line
;; @abi arm_nt
;; @in r0 = line bytes, r1 = line len
;; @out r0 = 1 if user quit, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Expands tabs (/Tn) and form-feeds (/P) and triggers CONIN$ pager.
@proc morea32_emit_line
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    mov r6, #0
.clp:
    cmp r6, r5
    bhs .eol
    ldrb r0, [r4, r6]
    cmp r0, #9
    beq .tab
    cmp r0, #12
    bne .nrm
    adr_sym r1, g_morea32_ff
    ldr r1, [r1]
    cmp r1, #0
    beq .nrm
    bl ua32_write_crlf
    adr_sym r1, g_morea32_rows
    mov r2, #23
    str r2, [r1]
    add r6, r6, #1
    b .clp
.tab:
    adr_sym r1, g_morea32_tab
    ldr r7, [r1]
.tlp:
    cmp r7, #0
    beq .nx
    mov r0, #32
    bl ua32_write_chr
    sub r7, r7, #1
    b .tlp
.nrm:
    bl ua32_write_chr
.nx:
    add r6, r6, #1
    b .clp

.eol:
    bl ua32_write_crlf
    adr_sym r1, g_morea32_rows
    ldr r2, [r1]
    add r2, r2, #1
    str r2, [r1]
    adr_sym r0, g_ua32_out_is_con
    ldr r0, [r0]
    cmp r0, #0
    beq .ok
    cmp r2, #23
    blo .ok
    mov r0, #0
    str r0, [r1]
    bl morea32_wait_key
    cmp r0, #0
    bne .qt
.ok:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
.qt:
    mov r0, #1
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc morea32_stream_one
;; @abi arm_nt
;; @in r0 = file path (empty=stdin)
;; @out r0 = 1 on error, 0 on ok
;; @err r0 = 1
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Streams file or stdin through MORE filters.
@proc morea32_stream_one
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0
    adr_sym r0, g_morea32_clr
    ldr r0, [r0]
    cmp r0, #0
    beq .ld
    adr_sym r0, g_ua32_out_is_con
    ldr r0, [r0]
    cmp r0, #0
    beq .ld
    adr_sym r0, szMorea32Cls
    bl ua32_write_str
.ld:
    mov r0, r4
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 260000
    mov r3, #1
    bl ua32_load_stream
    cmp r1, #0
    bne .err

    adr_sym r1, g_ua32_arena
    ldr r5, [r1]
    add r6, r5, r0
    mov r8, #0 ; prev_blank

.ln_lp:
    cmp r5, r6
    bhs .ok
    ldrb r0, [r5]
    cmp r0, #0x1A
    beq .ok
    mov r7, r5
.eol:
    cmp r7, r6
    bhs .gl
    ldrb r0, [r7]
    cmp r0, #13
    beq .gl
    cmp r0, #10
    beq .gl
    add r7, r7, #1
    b .eol
.gl:
    adr_sym r0, g_morea32_skip
    ldr r1, [r0]
    cmp r1, #0
    beq .sq
    sub r1, r1, #1
    str r1, [r0]
    b .adv
.sq:
    sub r1, r7, r5
    adr_sym r0, g_morea32_sqz
    ldr r0, [r0]
    cmp r0, #0
    beq .em
    cmp r1, #0
    bne .nb
    cmp r8, #0
    bne .adv
    mov r8, #1
    b .em
.nb:
    mov r8, #0
.em:
    mov r0, r5
    sub r1, r7, r5
    bl morea32_emit_line
    cmp r0, #0
    bne .ok
.adv:
    cmp r7, r6
    bhs .ok
    ldrb r0, [r7]
    cmp r0, #13
    bne .clf
    add r7, r7, #1
.clf:
    cmp r7, r6
    bhs .nx
    ldrb r0, [r7]
    cmp r0, #10
    bne .nx
    add r7, r7, #1
.nx:
    mov r5, r7
    b .ln_lp
.ok:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
.err:
    adr_sym r0, szMorea32Nf
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on missing file
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 MORE.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_morea32_clr
    str r1, [r0]
    adr_sym r0, g_morea32_ff
    str r1, [r0]
    adr_sym r0, g_morea32_sqz
    str r1, [r0]
    adr_sym r0, g_morea32_skip
    str r1, [r0]
    adr_sym r0, g_morea32_rows
    str r1, [r0]
    adr_sym r0, g_morea32_files
    str r1, [r0]
    adr_sym r0, g_morea32_tab
    mov r1, #8
    str r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .fin
    cmp r1, #43 ; +
    beq .plus
    cmp r1, #47
    bne .farg
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #67 ; C
    beq .sc
    cmp r0, #80 ; P
    beq .sp
    cmp r0, #83 ; S
    beq .ss
    cmp r0, #84 ; T
    beq .st
    b .arg_lp
.sc:
    adr_sym r0, g_morea32_clr
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sp:
    adr_sym r0, g_morea32_ff
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.ss:
    adr_sym r0, g_morea32_sqz
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.st:
    add r0, r5, #2
    bl ua32_parse_u32
    cmp r0, #0
    beq .arg_lp
    cmp r0, #16
    bhi .arg_lp
    adr_sym r1, g_morea32_tab
    str r0, [r1]
    b .arg_lp
.plus:
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_ua32_arg_a
    add r0, r0, #1
    bl ua32_parse_u32
    adr_sym r1, g_morea32_skip
    str r0, [r1]
    b .arg_lp
.farg:
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_morea32_files
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    adr_sym r0, g_ua32_arg_b
    bl morea32_stream_one
    cmp r0, #0
    bne .ex1
    b .arg_lp

.fin:
    adr_sym r0, g_morea32_files
    ldr r0, [r0]
    cmp r0, #0
    bne .ex0
    adr_sym r0, g_ua32_arg_b
    mov r1, #0
    strb r1, [r0]
    bl morea32_stream_one
    cmp r0, #0
    bne .ex1
.ex0:
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.ex1:
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.help:
    adr_sym r0, szMorea32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szMorea32Prompt, "-- More -- ", 0
@data db szMorea32Cls, 27, "[2J", 27, "[H", 0
@data db szMorea32Nf, "Cannot access file.", 0
@data db szMorea32Help, "Displays output one screen at a time (/E /C /P /S /Tn +n [files]).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_morea32_clr, 1
@res dword g_morea32_ff, 1
@res dword g_morea32_sqz, 1
@res dword g_morea32_tab, 1
@res dword g_morea32_skip, 1
@res dword g_morea32_rows, 1
@res dword g_morea32_files, 1
@res byte g_morea32_key, 8
