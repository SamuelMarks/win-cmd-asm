;; @file timeout.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 TIMEOUT.EXE countdown utility ([/T] seconds [/NOBREAK]).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 1=syntax error)
;; @err Exits 1 on invalid syntax
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 TIMEOUT.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_toa64_nb
    str wzr, [x0]
    adr_sym x0, g_toa64_has
    str wzr, [x0]
    adr_sym x0, g_toa64_sec
    str wzr, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .run
    cmp w1, #47
    b.ne .npos
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #78 ; N
    b.eq .snb
    cmp w0, #84 ; T
    b.ne .bad
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    b .psec
.snb:
    adr_sym x0, g_toa64_nb
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.npos:
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
.psec:
    adr_sym x20, g_ua64_arg_b
    ldrb w0, [x20]
    cmp w0, #45
    b.ne .poss
    ldrb w0, [x20, #1]
    cmp w0, #49
    b.ne .bad
    ldrb w0, [x20, #2]
    cbnz w0, .bad
    adr_sym x0, g_toa64_sec
    str wzr, [x0]
    adr_sym x0, g_toa64_has
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.poss:
    mov x0, x20
    bl ua64_parse_u32
    ldrb w2, [x1]
    cbnz w2, .bad
    adr_sym x1, g_toa64_sec
    str w0, [x1]
    adr_sym x1, g_toa64_has
    mov w0, #1
    str w0, [x1]
    b .arg_lp

.help:
    adr_sym x0, szToa64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szToa64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out

.run:
    adr_sym x0, g_toa64_has
    ldr w0, [x0]
    cbz w0, .bad
    bl ua64_write_crlf
    adr_sym x0, szToa64W1
    bl ua64_write_str
    adr_sym x0, g_toa64_sec
    ldr w20, [x0]
    mov w0, w20
    bl ua64_print_u64
    adr_sym x0, g_toa64_nb
    ldr w0, [x0]
    cbnz w0, .pnb
    adr_sym x0, szToa64Key
    b .pm
.pnb:
    adr_sym x0, szToa64Nb
.pm:
    bl ua64_write_str
    bl ua64_write_crlf

.slp:
    cbz w20, .fin
    adr_sym x0, g_toa64_nb
    ldr w0, [x0]
    cbnz w0, .slps
    adr_sym x0, g_ua64_in_is_con
    ldr w0, [x0]
    cbz w0, .slps
    adr_sym x0, g_ua64_stdin
    ldr x0, [x0]
    mov w1, #1000
    call_iat __imp_WaitForSingleObject
    cbz w0, .fin
    sub w20, w20, #1
    b .slp
.slps:
    mov w0, #1000
    call_iat __imp_Sleep
    sub w20, w20, #1
    b .slp
.fin:
    mov w0, #0
    bl ua64_exit
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szToa64W1, "Waiting for ", 0
@data db szToa64Key, " seconds, press a key to continue ...", 0
@data db szToa64Nb, " seconds, press CTRL+C to quit ...", 0
@data db szToa64Syn, "ERROR: Invalid syntax.", 0
@data db szToa64Help, "Accepts a timeout parameter to wait for the specified time period ([/T] sec [/NOBREAK]).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_toa64_nb, 1
@res dword g_toa64_has, 1
@res dword g_toa64_sec, 1
