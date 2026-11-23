;; @file more.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 MORE.EXE interactive pager (/E, /C, /P, /S, /Tn, +n [files...], CONIN$ key polling).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc morea64_wait_key
;; @abi arm64_ms
;; @in none
;; @out w0 = 1 if user quit ('Q'), 0 to continue
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prompts "-- More -- " and reads keystroke from CONIN$.
@proc morea64_wait_key
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    adr_sym x0, szMorea64Prompt
    bl ua64_write_str

    adr_sym x0, u16Ua64ConIn
    mov_imm32 w1, 0x80000000
    mov w2, #3
    mov x3, #0
    mov w4, #3
    mov w5, #0x80
    mov x6, #0
    call_iat __imp_CreateFileW
    cmn x0, #1
    b.eq .cnl
    mov x19, x0
    adr_sym x1, g_morea64_key
    strb wzr, [x1]
    mov x2, #1
    adr_sym x3, g_ua64_io_done
    mov x4, #0
    call_iat __imp_ReadFile
    mov x0, x19
    call_iat __imp_CloseHandle
    bl ua64_write_crlf
    adr_sym x0, g_morea64_key
    ldrb w0, [x0]
    bl ua64_chr_upper
    cmp w0, #81 ; Q
    b.eq .qt
    mov w0, #0
    b .dn
.cnl:
    bl ua64_write_crlf
    mov w0, #0
    b .dn
.qt:
    mov w0, #1
.dn:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc morea64_emit_line
;; @abi arm64_ms
;; @in x0 = line bytes, x1 = line len
;; @out w0 = 1 if user quit, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Expands tabs (/Tn) and form-feeds (/P) and triggers CONIN$ pager.
@proc morea64_emit_line
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x19, x0
    mov x20, x1
    mov x21, #0
.clp:
    cmp x21, x20
    b.hs .eol
    ldrb w0, [x19, x21]
    cmp w0, #9
    b.eq .tab
    cmp w0, #12
    b.ne .nrm
    adr_sym x1, g_morea64_ff
    ldr w1, [x1]
    cbz w1, .nrm
    bl ua64_write_crlf
    adr_sym x1, g_morea64_rows
    mov w2, #23
    str w2, [x1]
    add x21, x21, #1
    b .clp
.tab:
    adr_sym x1, g_morea64_tab
    ldr w22, [x1]
.tlp:
    cbz w22, .nx
    mov w0, #32
    bl ua64_write_chr
    sub w22, w22, #1
    b .tlp
.nrm:
    bl ua64_write_chr
.nx:
    add x21, x21, #1
    b .clp

.eol:
    bl ua64_write_crlf
    adr_sym x1, g_morea64_rows
    ldr w2, [x1]
    add w2, w2, #1
    str w2, [x1]
    adr_sym x0, g_ua64_out_is_con
    ldr w0, [x0]
    cbz w0, .ok
    cmp w2, #23
    b.lo .ok
    str wzr, [x1]
    bl morea64_wait_key
    cbnz w0, .qt
.ok:
    mov w0, #0
    b .dn
.qt:
    mov w0, #1
.dn:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc morea64_stream_one
;; @abi arm64_ms
;; @in x0 = file path (empty=stdin)
;; @out w0 = 1 on error, 0 on ok
;; @err w0 = 1
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Streams file or stdin through MORE filters.
@proc morea64_stream_one
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x19, x0
    adr_sym x0, g_morea64_clr
    ldr w0, [x0]
    cbz w0, .ld
    adr_sym x0, g_ua64_out_is_con
    ldr w0, [x0]
    cbz w0, .ld
    adr_sym x0, szMorea64Cls
    bl ua64_write_str
.ld:
    mov x0, x19
    adr_sym x4, g_ua64_arena
    ldr x1, [x4]
    mov_imm32 x2, 260000
    mov w3, #1
    bl ua64_load_stream
    cbnz w1, .err

    adr_sym x4, g_ua64_arena
    ldr x20, [x4]
    add x21, x20, x0
    mov w23, #0 ; prev_blank

.ln_lp:
    cmp x20, x21
    b.hs .ok
    ldrb w0, [x20]
    cmp w0, #0x1A
    b.eq .ok
    mov x22, x20
.eol:
    cmp x22, x21
    b.hs .gl
    ldrb w0, [x22]
    cmp w0, #13
    b.eq .gl
    cmp w0, #10
    b.eq .gl
    add x22, x22, #1
    b .eol
.gl:
    adr_sym x0, g_morea64_skip
    ldr w1, [x0]
    cbz w1, .sq
    sub w1, w1, #1
    str w1, [x0]
    b .adv
.sq:
    sub x1, x22, x20
    adr_sym x0, g_morea64_sqz
    ldr w0, [x0]
    cbz w0, .em
    cbnz x1, .nb
    cbnz w23, .adv
    mov w23, #1
    b .em
.nb:
    mov w23, #0
.em:
    mov x0, x20
    sub x1, x22, x20
    bl morea64_emit_line
    cbnz w0, .ok
.adv:
    cmp x22, x21
    b.hs .ok
    ldrb w0, [x22]
    cmp w0, #13
    b.ne .clf
    add x22, x22, #1
.clf:
    cmp x22, x21
    b.hs .nx
    ldrb w0, [x22]
    cmp w0, #10
    b.ne .nx
    add x22, x22, #1
.nx:
    mov x20, x22
    b .ln_lp
.ok:
    mov w0, #0
    b .dn
.err:
    adr_sym x0, szMorea64Nf
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
.dn:
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
;; @err Exits 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 MORE.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_morea64_clr
    str wzr, [x0]
    adr_sym x0, g_morea64_ff
    str wzr, [x0]
    adr_sym x0, g_morea64_sqz
    str wzr, [x0]
    adr_sym x0, g_morea64_skip
    str wzr, [x0]
    adr_sym x0, g_morea64_rows
    str wzr, [x0]
    adr_sym x0, g_morea64_files
    str wzr, [x0]
    adr_sym x0, g_morea64_tab
    mov w1, #8
    str w1, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .fin
    cmp w1, #43 ; +
    b.eq .plus
    cmp w1, #47
    b.ne .farg
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #67 ; C
    b.eq .sc
    cmp w0, #80 ; P
    b.eq .sp
    cmp w0, #83 ; S
    b.eq .ss
    cmp w0, #84 ; T
    b.eq .st
    b .arg_lp
.sc:
    adr_sym x0, g_morea64_clr
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sp:
    adr_sym x0, g_morea64_ff
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.ss:
    adr_sym x0, g_morea64_sqz
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.st:
    add x0, x20, #2
    bl ua64_parse_u32
    cbz w0, .arg_lp
    cmp w0, #16
    b.hi .arg_lp
    adr_sym x1, g_morea64_tab
    str w0, [x1]
    b .arg_lp
.plus:
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_ua64_arg_a
    add x0, x0, #1
    bl ua64_parse_u32
    adr_sym x1, g_morea64_skip
    str w0, [x1]
    b .arg_lp
.farg:
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_morea64_files
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]
    adr_sym x0, g_ua64_arg_b
    bl morea64_stream_one
    cbnz w0, .ex1
    b .arg_lp

.fin:
    adr_sym x0, g_morea64_files
    ldr w0, [x0]
    cbnz w0, .ex0
    adr_sym x0, g_ua64_arg_b
    strb wzr, [x0]
    bl morea64_stream_one
    cbnz w0, .ex1
.ex0:
    mov w0, #0
    bl ua64_exit
    b .out
.ex1:
    mov w0, #1
    bl ua64_exit
    b .out
.help:
    adr_sym x0, szMorea64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szMorea64Prompt, "-- More -- ", 0
@data db szMorea64Cls, 27, "[2J", 27, "[H", 0
@data db szMorea64Nf, "Cannot access file.", 0
@data db szMorea64Help, "Displays output one screen at a time (/E /C /P /S /Tn +n [files]).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_morea64_clr, 1
@res dword g_morea64_ff, 1
@res dword g_morea64_sqz, 1
@res dword g_morea64_tab, 1
@res dword g_morea64_skip, 1
@res dword g_morea64_rows, 1
@res dword g_morea64_files, 1
@res byte g_morea64_key, 8
