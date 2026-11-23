;; @file timeout.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 TIMEOUT.EXE countdown utility ([/T] seconds [/NOBREAK]).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 1=syntax error)
;; @err Exits 1 on invalid syntax
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 TIMEOUT.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_toa32_nb
    str r1, [r0]
    adr_sym r0, g_toa32_has
    str r1, [r0]
    adr_sym r0, g_toa32_sec
    str r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .run
    cmp r1, #47
    bne .npos
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #78 ; N
    beq .snb
    cmp r0, #84 ; T
    bne .bad
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    b .psec
.snb:
    adr_sym r0, g_toa32_nb
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.npos:
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
.psec:
    adr_sym r5, g_ua32_arg_b
    ldrb r0, [r5]
    cmp r0, #45
    bne .poss
    ldrb r0, [r5, #1]
    cmp r0, #49
    bne .bad
    ldrb r0, [r5, #2]
    cmp r0, #0
    bne .bad
    adr_sym r0, g_toa32_sec
    mov r1, #0
    str r1, [r0]
    adr_sym r0, g_toa32_has
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.poss:
    mov r0, r5
    bl ua32_parse_u32
    ldrb r2, [r1]
    cmp r2, #0
    bne .bad
    adr_sym r1, g_toa32_sec
    str r0, [r1]
    adr_sym r1, g_toa32_has
    mov r0, #1
    str r0, [r1]
    b .arg_lp

.help:
    adr_sym r0, szToa32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.bad:
    adr_sym r0, szToa32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.run:
    adr_sym r0, g_toa32_has
    ldr r0, [r0]
    cmp r0, #0
    beq .bad
    bl ua32_write_crlf
    adr_sym r0, szToa32W1
    bl ua32_write_str
    adr_sym r0, g_toa32_sec
    ldr r5, [r0]
    mov r0, r5
    bl ua32_print_u32
    adr_sym r0, g_toa32_nb
    ldr r0, [r0]
    cmp r0, #0
    bne .pnb
    adr_sym r0, szToa32Key
    b .pm
.pnb:
    adr_sym r0, szToa32Nb
.pm:
    bl ua32_write_str
    bl ua32_write_crlf

.slp:
    cmp r5, #0
    beq .fin
    adr_sym r0, g_toa32_nb
    ldr r0, [r0]
    cmp r0, #0
    bne .slps
    adr_sym r0, g_ua32_in_is_con
    ldr r0, [r0]
    cmp r0, #0
    beq .slps
    adr_sym r0, g_ua32_stdin
    ldr r0, [r0]
    mov r1, #1000
    call_iat __imp_WaitForSingleObject
    cmp r0, #0
    beq .fin
    sub r5, r5, #1
    b .slp
.slps:
    mov r0, #1000
    call_iat __imp_Sleep
    sub r5, r5, #1
    b .slp
.fin:
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szToa32W1, "Waiting for ", 0
@data db szToa32Key, " seconds, press a key to continue ...", 0
@data db szToa32Nb, " seconds, press CTRL+C to quit ...", 0
@data db szToa32Syn, "ERROR: Invalid syntax.", 0
@data db szToa32Help, "Accepts a timeout parameter to wait for the specified time period ([/T] sec [/NOBREAK]).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_toa32_nb, 1
@res dword g_toa32_has, 1
@res dword g_toa32_sec, 1
