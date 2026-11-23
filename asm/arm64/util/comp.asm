;; @file comp.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 COMP.EXE file comparison utility (/D, /A, /L, /N=num, /C, /M file1 file2).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc compa64_fmt_byte
;; @abi arm64_ms
;; @in w0 = byte value (0..255)
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints byte value as ASCII (/A), decimal (/D), or hex (default).
@proc compa64_fmt_byte
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x1, g_cmpa64_asc
    ldr w1, [x1]
    cbnz w1, .asc
    adr_sym x1, g_cmpa64_dec
    ldr w1, [x1]
    cbnz w1, .dec
    bl ua64_print_hex8
    ldp x29, x30, [sp], #16
    hint #29
    ret
.dec:
    and x0, x0, #0xFF
    bl ua64_print_u64
    ldp x29, x30, [sp], #16
    hint #29
    ret
.asc:
    bl ua64_write_chr
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=mismatch, 2=error)
;; @err Exits 2 on missing file
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 COMP.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_cmpa64_dec
    str wzr, [x0]
    adr_sym x0, g_cmpa64_asc
    str wzr, [x0]
    adr_sym x0, g_cmpa64_ln
    str wzr, [x0]
    adr_sym x0, g_cmpa64_ci
    str wzr, [x0]
    adr_sym x0, g_cmpa64_max_ln
    str wzr, [x0]
    adr_sym x0, g_cmpa64_no_p
    str wzr, [x0]
    adr_sym x0, g_ua64_arg_b
    strb wzr, [x0]
    adr_sym x0, g_ua64_arg_c
    strb wzr, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .run
    cmp w1, #47
    b.ne .pos
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #68 ; D
    b.eq .sd
    cmp w0, #65 ; A
    b.eq .sa
    cmp w0, #76 ; L
    b.eq .sl
    cmp w0, #67 ; C
    b.eq .sc
    cmp w0, #77 ; M
    b.eq .sm
    cmp w0, #78 ; N
    b.eq .sn
    b .arg_lp
.sd:
    adr_sym x0, g_cmpa64_dec
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sa:
    adr_sym x0, g_cmpa64_asc
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sl:
    adr_sym x0, g_cmpa64_ln
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sc:
    adr_sym x0, g_cmpa64_ci
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sm:
    adr_sym x0, g_cmpa64_no_p
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sn:
    ldrb w0, [x20, #2]
    cmp w0, #61 ; =
    b.ne .arg_lp
    add x0, x20, #3
    bl ua64_parse_u32
    adr_sym x1, g_cmpa64_max_ln
    str w0, [x1]
    b .arg_lp
.pos:
    adr_sym x0, g_ua64_arg_b
    ldrb w0, [x0]
    cbnz w0, .pos2
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.pos2:
    mov x0, x19
    adr_sym x1, g_ua64_arg_c
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp

.help:
    adr_sym x0, szCmpa64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.run:
    adr_sym x0, g_ua64_arg_b
    ldrb w0, [x0]
    cbz w0, .err
    adr_sym x0, g_ua64_arg_c
    ldrb w0, [x0]
    cbz w0, .err

    adr_sym x0, g_ua64_arg_b
    adr_sym x1, g_ua64_arena
    ldr x1, [x1]
    mov_imm32 x2, 220000
    mov w3, #0
    bl ua64_load_stream
    cbnz w1, .err
    mov x20, x0

    adr_sym x0, g_ua64_arg_c
    adr_sym x1, g_ua64_arena
    ldr x1, [x1]
    mov_imm32 x4, 262144
    add x1, x1, x4
    mov_imm32 x2, 220000
    mov w3, #0
    bl ua64_load_stream
    cbnz w1, .err
    mov x21, x0

    adr_sym x0, szCmpa64H1
    bl ua64_write_str
    adr_sym x0, g_ua64_arg_b
    bl ua64_write_str
    adr_sym x0, szCmpa64H2
    bl ua64_write_str
    adr_sym x0, g_ua64_arg_c
    bl ua64_write_str
    adr_sym x0, szCmpa64Dots
    bl ua64_write_str
    bl ua64_write_crlf

    mov w25, #0 ; errs
    adr_sym x0, g_cmpa64_max_ln
    ldr w0, [x0]
    cbnz w0, .st
    cmp x20, x21
    b.eq .st
    adr_sym x0, szCmpa64Diff
    bl ua64_write_str
    bl ua64_write_crlf
    mov w25, #1
    b .prompt

.st:
    mov x24, x20
    cmp x24, x21
    b.ls .gmin
    mov x24, x21
.gmin:
    adr_sym x0, g_ua64_arena
    ldr x22, [x0]
    mov_imm32 x4, 262144
    add x23, x22, x4
    mov x19, #0 ; offset
    adr_sym x0, g_cmpa64_cur_ln
    mov w1, #1
    str w1, [x0]

.lp:
    cmp x19, x24
    b.hs .fin
    ldrb w0, [x22, x19]
    cmp w0, #10
    b.ne .nonl
    adr_sym x1, g_cmpa64_cur_ln
    ldr w2, [x1]
    add w2, w2, #1
    str w2, [x1]
    adr_sym x1, g_cmpa64_max_ln
    ldr w1, [x1]
    cbz w1, .nonl
    sub w2, w2, #1
    cmp w2, w1
    b.hs .fin
.nonl:
    ldrb w1, [x23, x19]
    adr_sym x2, g_cmpa64_ci
    ldr w2, [x2]
    cbz w2, .raw
    cmp w0, #97
    b.lo .u1
    cmp w0, #122
    b.hi .u1
    sub w0, w0, #32
.u1:
    cmp w1, #97
    b.lo .u2
    cmp w1, #122
    b.hi .u2
    sub w1, w1, #32
.u2:
.raw:
    cmp w0, w1
    b.eq .nx

    add w25, w25, #1
    cmp w25, #10
    b.hi .fin
    adr_sym x0, g_cmpa64_ln
    ldr w0, [x0]
    cbnz w0, .prln
    adr_sym x0, szCmpa64EOff
    bl ua64_write_str
    mov w0, w19
    bl ua64_print_hex32
    b .vals
.prln:
    adr_sym x0, szCmpa64ELn
    bl ua64_write_str
    adr_sym x0, g_cmpa64_cur_ln
    ldr w0, [x0]
    bl ua64_print_u64
.vals:
    bl ua64_write_crlf
    adr_sym x0, szCmpa64F1
    bl ua64_write_str
    ldrb w0, [x22, x19]
    bl compa64_fmt_byte
    bl ua64_write_crlf
    adr_sym x0, szCmpa64F2
    bl ua64_write_str
    ldrb w0, [x23, x19]
    bl compa64_fmt_byte
    bl ua64_write_crlf
.nx:
    add x19, x19, #1
    b .lp

.fin:
    cbnz w25, .prompt
    adr_sym x0, szCmpa64Ok
    bl ua64_write_str
    bl ua64_write_crlf
.prompt:
    adr_sym x0, g_cmpa64_no_p
    ldr w0, [x0]
    cbnz w0, .ex
    adr_sym x0, g_ua64_in_is_con
    ldr w0, [x0]
    cbz w0, .ex
    adr_sym x0, szCmpa64More
    bl ua64_write_str
    bl ua64_write_crlf
.ex:
    cbnz w25, .ex1
    mov w0, #0
    bl ua64_exit
    b .out
.ex1:
    mov w0, #1
    bl ua64_exit
    b .out
.err:
    adr_sym x0, szCmpa64Open
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #2
    bl ua64_exit
.out:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szCmpa64H1, "Comparing ", 0
@data db szCmpa64H2, " and ", 0
@data db szCmpa64Dots, "...", 0
@data db szCmpa64Ok, "Files compare OK", 0
@data db szCmpa64Diff, "Files are different sizes", 0
@data db szCmpa64EOff, "Compare error at OFFSET ", 0
@data db szCmpa64ELn, "Compare error at LINE ", 0
@data db szCmpa64F1, "file1 = ", 0
@data db szCmpa64F2, "file2 = ", 0
@data db szCmpa64More, "Compare more files (Y/N) ? N", 0
@data db szCmpa64Open, "Can't find/open file", 0
@data db szCmpa64Help, "Compares the contents of two files or sets of files (/D /A /L /N=num /C /M).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_cmpa64_dec, 1
@res dword g_cmpa64_asc, 1
@res dword g_cmpa64_ln, 1
@res dword g_cmpa64_ci, 1
@res dword g_cmpa64_max_ln, 1
@res dword g_cmpa64_no_p, 1
@res dword g_cmpa64_cur_ln, 1
