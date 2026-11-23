;; @file fc.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 FC.EXE file comparison utility (/A /B /C /L /LBn /N /OFF /T /U /W file1 file2).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc fca64_lines_eq
;; @abi arm64_ms
;; @in x0 = line A, x1 = len A, x2 = line B, x3 = len B
;; @out w0 = 1 if equal, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Compares two text lines honoring /C and /W.
@proc fca64_lines_eq
    add x4, x0, x1
    add x5, x2, x3
    adr_sym x6, g_fca64_ws
    ldr w6, [x6]
    adr_sym x7, g_fca64_ci
    ldr w7, [x7]

    cbz w6, .lp
.tla:
    cmp x0, x4
    b.hs .tlb
    ldrb w1, [x0]
    cmp w1, #32
    b.eq .ala
    cmp w1, #9
    b.ne .tlb
.ala:
    add x0, x0, #1
    b .tla
.tlb:
    cmp x2, x5
    b.hs .lp
    ldrb w1, [x2]
    cmp w1, #32
    b.eq .alb
    cmp w1, #9
    b.ne .lp
.alb:
    add x2, x2, #1
    b .tlb

.lp:
    cmp x0, x4
    b.hs .adn
    cmp x2, x5
    b.hs .bdn

    cbz w6, .cc
    ldrb w1, [x0]
    cmp w1, #32
    b.eq .wsa
    cmp w1, #9
    b.ne .cc
.wsa:
    ldrb w3, [x2]
    cmp w3, #32
    b.eq .skw
    cmp w3, #9
    b.ne .no
.skw:
    add x0, x0, #1
    cmp x0, x4
    b.hs .skwb
    ldrb w1, [x0]
    cmp w1, #32
    b.eq .skw
    cmp w1, #9
    b.eq .skw
.skwb:
    add x2, x2, #1
    cmp x2, x5
    b.hs .lp
    ldrb w3, [x2]
    cmp w3, #32
    b.eq .skwb
    cmp w3, #9
    b.eq .skwb
    b .lp

.cc:
    ldrb w1, [x0]
    ldrb w3, [x2]
    cbz w7, .raw
    cmp w1, #97
    b.lo .u1
    cmp w1, #122
    b.hi .u1
    sub w1, w1, #32
.u1:
    cmp w3, #97
    b.lo .u2
    cmp w3, #122
    b.hi .u2
    sub w3, w3, #32
.u2:
.raw:
    cmp w1, w3
    b.ne .no
    add x0, x0, #1
    add x2, x2, #1
    b .lp

.adn:
    cbz w6, .cbe
.trb:
    cmp x2, x5
    b.hs .yes
    ldrb w3, [x2]
    cmp w3, #32
    b.eq .atb
    cmp w3, #9
    b.ne .no
.atb:
    add x2, x2, #1
    b .trb
.cbe:
    cmp x2, x5
    b.eq .yes
    b .no

.bdn:
    cbz w6, .no
.tra:
    cmp x0, x4
    b.hs .yes
    ldrb w1, [x0]
    cmp w1, #32
    b.eq .ata
    cmp w1, #9
    b.ne .no
.ata:
    add x0, x0, #1
    b .tra

.yes:
    mov w0, #1
    ret
.no:
    mov w0, #0
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=diffs, 2=error)
;; @err Exits 2 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 FC.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #16

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_fca64_bin
    str wzr, [x0]
    adr_sym x0, g_fca64_ci
    str wzr, [x0]
    adr_sym x0, g_fca64_nums
    str wzr, [x0]
    adr_sym x0, g_fca64_ws
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
    cmp w0, #66 ; B
    b.eq .sb
    cmp w0, #67 ; C
    b.eq .sc
    cmp w0, #78 ; N
    b.eq .sn
    cmp w0, #87 ; W
    b.eq .sw
    b .arg_lp
.sb:
    adr_sym x0, g_fca64_bin
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sc:
    adr_sym x0, g_fca64_ci
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sn:
    adr_sym x0, g_fca64_nums
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sw:
    adr_sym x0, g_fca64_ws
    mov w1, #1
    str w1, [x0]
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
    adr_sym x0, szFca64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.run:
    adr_sym x0, g_ua64_arg_b
    ldrb w0, [x0]
    cbz w0, .bad
    adr_sym x0, g_ua64_arg_c
    ldrb w0, [x0]
    cbz w0, .bad

    mov w3, #1
    adr_sym x0, g_fca64_bin
    ldr w0, [x0]
    cbz w0, .lda
    mov w3, #0
.lda:
    adr_sym x0, g_ua64_arg_b
    adr_sym x4, g_ua64_arena
    ldr x1, [x4]
    mov_imm32 x2, 220000
    bl ua64_load_stream
    cbnz w1, .nf
    mov x20, x0 ; len A

    mov w3, #1
    adr_sym x0, g_fca64_bin
    ldr w0, [x0]
    cbz w0, .ldb
    mov w3, #0
.ldb:
    adr_sym x0, g_ua64_arg_c
    adr_sym x4, g_ua64_arena
    ldr x1, [x4]
    mov_imm32 x4, 262144
    add x1, x1, x4
    mov_imm32 x2, 220000
    bl ua64_load_stream
    cbnz w1, .nf
    mov x21, x0 ; len B

    adr_sym x0, szFca64Ban1
    bl ua64_write_str
    adr_sym x0, g_ua64_arg_b
    bl ua64_write_str
    adr_sym x0, szFca64Ban2
    bl ua64_write_str
    adr_sym x0, g_ua64_arg_c
    bl ua64_write_str
    bl ua64_write_crlf

    adr_sym x0, g_fca64_diffs
    str wzr, [x0]

    adr_sym x0, g_fca64_bin
    ldr w0, [x0]
    cbz w0, .txt

    adr_sym x0, g_ua64_arena
    ldr x22, [x0]
    mov_imm32 x4, 262144
    add x23, x22, x4
    mov x24, x20
    cmp x24, x21
    b.ls .gmin
    mov x24, x21
.gmin:
    mov x25, #0
.blp:
    cmp x25, x24
    b.hs .blen
    ldrb w0, [x22, x25]
    ldrb w1, [x23, x25]
    cmp w0, w1
    b.eq .bnx
    adr_sym x0, g_fca64_diffs
    mov w1, #1
    str w1, [x0]
    mov w0, w25
    bl ua64_print_hex32
    adr_sym x0, szFca64Col
    bl ua64_write_str
    ldrb w0, [x22, x25]
    bl ua64_print_hex8
    mov w0, #32
    bl ua64_write_chr
    ldrb w0, [x23, x25]
    bl ua64_print_hex8
    bl ua64_write_crlf
.bnx:
    add x25, x25, #1
    b .blp
.blen:
    cmp x20, x21
    b.eq .chkd
    adr_sym x0, g_fca64_diffs
    mov w1, #1
    str w1, [x0]
    adr_sym x0, szFca64Long
    bl ua64_write_str
    bl ua64_write_crlf
    b .chkd

.txt:
    adr_sym x0, g_ua64_arena
    ldr x22, [x0]
    add x20, x22, x20 ; end A
    mov_imm32 x4, 262144
    add x23, x22, x4
    add x21, x23, x21 ; end B
    mov x26, #0 ; line_no

.tlp:
    cmp x22, x20
    b.hs .aeof
    cmp x23, x21
    b.hs .dtail
    add x26, x26, #1

    mov x24, x22
.ea:
    cmp x24, x20
    b.hs .ga
    ldrb w0, [x24]
    cmp w0, #13
    b.eq .ga
    cmp w0, #10
    b.eq .ga
    add x24, x24, #1
    b .ea
.ga:
    mov x25, x23
.eb:
    cmp x25, x21
    b.hs .gb
    ldrb w0, [x25]
    cmp w0, #13
    b.eq .gb
    cmp w0, #10
    b.eq .gb
    add x25, x25, #1
    b .eb
.gb:
    mov x0, x22
    sub x1, x24, x22
    mov x2, x23
    sub x3, x25, x23
    stp x1, x3, [sp]
    bl fca64_lines_eq
    cbnz w0, .adv

    adr_sym x0, g_fca64_diffs
    mov w1, #1
    str w1, [x0]
    adr_sym x0, szFca64Stars
    bl ua64_write_str
    adr_sym x0, g_ua64_arg_b
    bl ua64_write_str
    bl ua64_write_crlf
    adr_sym x0, g_fca64_nums
    ldr w0, [x0]
    cbz w0, .pla
    mov x0, x26
    bl ua64_print_u64
    adr_sym x0, szFca64Col
    bl ua64_write_str
.pla:
    mov x0, x22
    ldr x1, [sp]
    bl ua64_write_out
    bl ua64_write_crlf

    adr_sym x0, szFca64Stars
    bl ua64_write_str
    adr_sym x0, g_ua64_arg_c
    bl ua64_write_str
    bl ua64_write_crlf
    adr_sym x0, g_fca64_nums
    ldr w0, [x0]
    cbz w0, .plb
    mov x0, x26
    bl ua64_print_u64
    adr_sym x0, szFca64Col
    bl ua64_write_str
.plb:
    mov x0, x23
    ldr x1, [sp, #8]
    bl ua64_write_out
    bl ua64_write_crlf
    adr_sym x0, szFca64End
    bl ua64_write_str
    bl ua64_write_crlf

.adv:
    cmp x24, x20
    b.hs .sa
    ldrb w0, [x24]
    cmp w0, #13
    b.ne .calf
    add x24, x24, #1
.calf:
    cmp x24, x20
    b.hs .sa
    ldrb w0, [x24]
    cmp w0, #10
    b.ne .sa
    add x24, x24, #1
.sa:
    mov x22, x24

    cmp x25, x21
    b.hs .sb2
    ldrb w0, [x25]
    cmp w0, #13
    b.ne .cblf
    add x25, x25, #1
.cblf:
    cmp x25, x21
    b.hs .sb2
    ldrb w0, [x25]
    cmp w0, #10
    b.ne .sb2
    add x25, x25, #1
.sb2:
    mov x23, x25
    b .tlp

.aeof:
    cmp x23, x21
    b.eq .chkd
.dtail:
    adr_sym x0, g_fca64_diffs
    mov w1, #1
    str w1, [x0]
    adr_sym x0, szFca64Long
    bl ua64_write_str
    bl ua64_write_crlf

.chkd:
    adr_sym x0, g_fca64_diffs
    ldr w0, [x0]
    cbnz w0, .ex1
    adr_sym x0, szFca64Ok
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.ex1:
    mov w0, #1
    bl ua64_exit
    b .out
.nf:
    adr_sym x0, szFca64Nf
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #2
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szFca64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #2
    bl ua64_exit
.out:
    add sp, sp, #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szFca64Ban1, "Comparing files ", 0
@data db szFca64Ban2, " and ", 0
@data db szFca64Ok, "FC: no differences encountered", 0
@data db szFca64Stars, "***** ", 0
@data db szFca64End, "*****", 0
@data db szFca64Col, ": ", 0
@data db szFca64Long, "FC: files are different sizes", 0
@data db szFca64Nf, "FC: cannot open file", 0
@data db szFca64Syn, "FC: Insufficient number of file specifications", 0
@data db szFca64Help, "Compares two files or sets of files and displays the differences (/B /C /L /N /U /W).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_fca64_bin, 1
@res dword g_fca64_ci, 1
@res dword g_fca64_nums, 1
@res dword g_fca64_ws, 1
@res dword g_fca64_diffs, 1
