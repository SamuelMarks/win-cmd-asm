;; @file find.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 FIND.EXE utility (/V, /C, /N, /I, /OFF[LINE] "string" [files...]).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc finda64_line_has
;; @abi arm64_ms
;; @in x0 = line start, x1 = line len, x2 = needle, x3 = needle len
;; @out w0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Substring search honoring g_finda64_ci (/I).
@proc finda64_line_has
    cbz x3, .no
    cmp x1, x3
    b.lo .no
    sub x1, x1, x3
    add x1, x1, #1
    adr_sym x4, g_finda64_ci
    ldr w4, [x4]
.pos:
    mov x5, #0
.ch:
    cmp x5, x3
    b.hs .yes
    ldrb w6, [x0, x5]
    ldrb w7, [x2, x5]
    cbz w4, .raw
    cmp w6, #97
    b.lo .u1
    cmp w6, #122
    b.hi .u1
    sub w6, w6, #32
.u1:
    cmp w7, #97
    b.lo .u2
    cmp w7, #122
    b.hi .u2
    sub w7, w7, #32
.u2:
.raw:
    cmp w6, w7
    b.ne .nxp
    add x5, x5, #1
    b .ch
.nxp:
    add x0, x0, #1
    subs x1, x1, #1
    b.ne .pos
.no:
    mov w0, #0
    ret
.yes:
    mov w0, #1
    ret
@endproc

;; @proc finda64_process_one
;; @abi arm64_ms
;; @in x0 = file path UTF-8 (empty=stdin), w1 = show_banner (1 or 0)
;; @out w0 = 1 if matched, 0 if none, 2 on open error
;; @err w0 = 2
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Loads stream and scans lines for g_finda64_needle.
@proc finda64_process_one
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!

    mov x19, x0 ; path
    mov w20, w1 ; banner
    mov x21, #0 ; matches
    mov x22, #0 ; line_no

    adr_sym x4, g_ua64_arena
    ldr x1, [x4]
    mov_imm32 x2, 260000
    mov w3, #1
    bl ua64_load_stream
    cbz w1, .loaded
    adr_sym x0, szFinda64Nf
    bl ua64_write_err_str
    mov x0, x19
    bl ua64_write_err_str
    adr_sym x0, szUa64Crlf
    bl ua64_write_err_str
    mov w0, #2
    b .out

.loaded:
    mov x23, x0
    cbz w20, .st
    adr_sym x0, g_finda64_cnt_only
    ldr w0, [x0]
    cbnz w0, .st
    adr_sym x0, szFinda64Hdr
    bl ua64_write_str
    mov x0, x19
    bl ua64_write_str
    bl ua64_write_crlf

.st:
    adr_sym x0, g_ua64_arena
    ldr x24, [x0] ; cur
    add x23, x24, x23 ; end

.ln_lp:
    cmp x24, x23
    b.hs .after
    ldrb w0, [x24]
    cmp w0, #0x1A
    b.eq .after
    add x22, x22, #1
    mov x25, x24
.eol:
    cmp x25, x23
    b.hs .gl
    ldrb w0, [x25]
    cmp w0, #13
    b.eq .gl
    cmp w0, #10
    b.eq .gl
    add x25, x25, #1
    b .eol
.gl:
    mov x0, x24
    sub x26, x25, x24
    mov x1, x26
    adr_sym x2, g_finda64_needle
    adr_sym x4, g_finda64_nlen
    ldr w3, [x4]
    bl finda64_line_has
    adr_sym x4, g_finda64_inv
    ldr w4, [x4]
    eor w0, w0, w4
    cbz w0, .adv

    add x21, x21, #1
    adr_sym x0, g_finda64_cnt_only
    ldr w0, [x0]
    cbnz w0, .adv

    adr_sym x0, g_finda64_nums
    ldr w0, [x0]
    cbz w0, .emit
    mov w0, #91
    bl ua64_write_chr
    mov x0, x22
    bl ua64_print_u64
    mov w0, #93
    bl ua64_write_chr
.emit:
    mov x0, x24
    mov x1, x26
    bl ua64_write_out
    bl ua64_write_crlf

.adv:
    cmp x25, x23
    b.hs .after
    ldrb w0, [x25]
    cmp w0, #13
    b.ne .clf
    add x25, x25, #1
.clf:
    cmp x25, x23
    b.hs .nx
    ldrb w0, [x25]
    cmp w0, #10
    b.ne .nx
    add x25, x25, #1
.nx:
    mov x24, x25
    b .ln_lp

.after:
    adr_sym x0, g_finda64_cnt_only
    ldr w0, [x0]
    cbz w0, .ret
    cbz w20, .bc
    adr_sym x0, szFinda64Hdr
    bl ua64_write_str
    mov x0, x19
    bl ua64_write_str
    adr_sym x0, szFinda64Col
    bl ua64_write_str
.bc:
    mov x0, x21
    bl ua64_print_u64
    bl ua64_write_crlf
.ret:
    mov w0, #0
    cbz x21, .out
    mov w0, #1
.out:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 2 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 FIND.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_finda64_inv
    str wzr, [x0]
    adr_sym x0, g_finda64_cnt_only
    str wzr, [x0]
    adr_sym x0, g_finda64_nums
    str wzr, [x0]
    adr_sym x0, g_finda64_ci
    str wzr, [x0]
    adr_sym x0, g_finda64_has_n
    str wzr, [x0]
    adr_sym x0, g_finda64_files
    str wzr, [x0]
    adr_sym x0, g_finda64_hit
    str wzr, [x0]
    adr_sym x0, g_finda64_err
    str wzr, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .fin
    cmp w1, #34
    b.eq .nof
    cmp w1, #47
    b.ne .nof
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #86 ; V
    b.eq .sv
    cmp w0, #67 ; C
    b.eq .sc
    cmp w0, #78 ; N
    b.eq .sn
    cmp w0, #73 ; I
    b.eq .si
    cmp w0, #79 ; O
    b.eq .arg_lp
    b .bad
.sv:
    adr_sym x0, g_finda64_inv
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sc:
    adr_sym x0, g_finda64_cnt_only
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sn:
    adr_sym x0, g_finda64_nums
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.si:
    adr_sym x0, g_finda64_ci
    mov w1, #1
    str w1, [x0]
    b .arg_lp

.nof:
    adr_sym x0, g_finda64_has_n
    ldr w0, [x0]
    cbnz w0, .farg
    cmp w1, #34
    b.ne .bad
    mov x0, x19
    adr_sym x1, g_finda64_needle
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_finda64_nlen
    str w1, [x0]
    adr_sym x0, g_finda64_has_n
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.farg:
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_finda64_files
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]
    adr_sym x0, g_ua64_arg_b
    mov w1, #1
    bl finda64_process_one
    cmp w0, #1
    b.eq .hf
    cmp w0, #2
    b.ne .arg_lp
    adr_sym x0, g_finda64_err
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.hf:
    adr_sym x0, g_finda64_hit
    mov w1, #1
    str w1, [x0]
    b .arg_lp

.help:
    adr_sym x0, szFinda64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szFinda64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #2
    bl ua64_exit
    b .out

.fin:
    adr_sym x0, g_finda64_has_n
    ldr w0, [x0]
    cbz w0, .bad
    adr_sym x0, g_finda64_files
    ldr w0, [x0]
    cbnz w0, .exc
    adr_sym x0, g_ua64_arg_b
    strb wzr, [x0]
    mov w1, #0
    bl finda64_process_one
    cmp w0, #1
    b.ne .exc
    adr_sym x0, g_finda64_hit
    mov w1, #1
    str w1, [x0]
.exc:
    adr_sym x0, g_finda64_hit
    ldr w0, [x0]
    cbnz w0, .ex0
    adr_sym x0, g_finda64_err
    ldr w0, [x0]
    cbnz w0, .ex2
    mov w0, #1
    bl ua64_exit
    b .out
.ex2:
    mov w0, #2
    bl ua64_exit
    b .out
.ex0:
    mov w0, #0
    bl ua64_exit
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szFinda64Hdr, "---------- ", 0
@data db szFinda64Col, ": ", 0
@data db szFinda64Nf, "File not found - ", 0
@data db szFinda64Syn, "FIND: Parameter format not correct", 0
@data db szFinda64Help, "Searches for a text string in a file or files (/V /C /N /I /OFF).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_finda64_inv, 1
@res dword g_finda64_cnt_only, 1
@res dword g_finda64_nums, 1
@res dword g_finda64_ci, 1
@res dword g_finda64_has_n, 1
@res dword g_finda64_nlen, 1
@res dword g_finda64_files, 1
@res dword g_finda64_hit, 1
@res dword g_finda64_err, 1
@res byte g_finda64_needle, 1024
