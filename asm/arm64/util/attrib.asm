;; @file attrib.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 ATTRIB.EXE utility ([+|-][R|A|S|H|I|O|P|U] [path] [/S] [/D] [/L]).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc attra64_flag_bit
;; @abi arm64_ms
;; @in w0 = uppercase attribute char
;; @out w0 = Win32 FILE_ATTRIBUTE_* bitmask (0 if unknown)
;; @err w0 = 0
;; @clobbers x0
;; @desc Maps R, H, S, A, I, O, P, U to Win32 attribute constants.
@proc attra64_flag_bit
    cmp w0, #82 ; R
    b.eq .r
    cmp w0, #72 ; H
    b.eq .h
    cmp w0, #83 ; S
    b.eq .s
    cmp w0, #65 ; A
    b.eq .a
    cmp w0, #79 ; O
    b.eq .o
    cmp w0, #73 ; I
    b.eq .i
    cmp w0, #80 ; P
    b.eq .p
    cmp w0, #85 ; U
    b.eq .u
    mov w0, #0
    ret
.r:
    mov w0, #0x01
    ret
.h:
    mov w0, #0x02
    ret
.s:
    mov w0, #0x04
    ret
.a:
    mov w0, #0x20
    ret
.o:
    mov w0, #0x1000
    ret
.i:
    mov w0, #0x2000
    ret
.p:
    mov w0, #0x00080000
    ret
.u:
    mov w0, #0x00100000
    ret
@endproc

;; @proc attra64_print_entry
;; @abi arm64_ms
;; @in w0 = Win32 attributes, x1 = UTF-8 file path
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints formatted "A  SHR OIPU  path" line.
@proc attra64_print_entry
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov w19, w0
    mov x20, x1

    mov w0, #32
    tst w19, #0x20
    b.eq .p1
    mov w0, #65
.p1:
    bl ua64_write_chr
    mov w0, #32
    bl ua64_write_chr
    mov w0, #32
    tst w19, #0x04
    b.eq .p2
    mov w0, #83
.p2:
    bl ua64_write_chr
    mov w0, #32
    tst w19, #0x02
    b.eq .p3
    mov w0, #72
.p3:
    bl ua64_write_chr
    mov w0, #32
    tst w19, #0x01
    b.eq .p4
    mov w0, #82
.p4:
    bl ua64_write_chr
    mov w0, #32
    bl ua64_write_chr
    mov w0, #32
    tst w19, #0x1000
    b.eq .p5
    mov w0, #79
.p5:
    bl ua64_write_chr
    mov w0, #32
    tst w19, #0x2000
    b.eq .p6
    mov w0, #73
.p6:
    bl ua64_write_chr
    mov w0, #32
    tst w19, #0x00080000
    b.eq .p7
    mov w0, #80
.p7:
    bl ua64_write_chr
    mov w0, #32
    tst w19, #0x00100000
    b.eq .p8
    mov w0, #85
.p8:
    bl ua64_write_chr
    mov w0, #32
    bl ua64_write_chr
    mov w0, #32
    bl ua64_write_chr
    mov x0, x20
    bl ua64_write_str
    bl ua64_write_crlf
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc attra64_apply_one
;; @abi arm64_ms
;; @in x0 = full UTF-8 path, w1 = current attributes
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Applies +set/-clear masks or displays attributes.
@proc attra64_apply_one
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x2, g_attra64_set
    ldr w2, [x2]
    adr_sym x3, g_attra64_clr
    ldr w3, [x3]
    orr w4, w2, w3
    cbz w4, .show
    bic w1, w1, w3
    orr w1, w1, w2
    cbnz w1, .do_set
    mov w1, #0x80
.do_set:
    bl ua64_set_attr
    adr_sym x0, g_attra64_hit
    mov w1, #1
    str w1, [x0]
    ldp x29, x30, [sp], #16
    hint #29
    ret
.show:
    mov x2, x0
    mov w0, w1
    mov x1, x2
    bl attra64_print_entry
    adr_sym x0, g_attra64_hit
    mov w1, #1
    str w1, [x0]
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc attra64_scan_dir
;; @abi arm64_ms
;; @in x0 = dir UTF-8, x1 = leaf pattern UTF-8, w2 = depth
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Enumerates matching entries in x0 and recurses under /S.
@proc attra64_scan_dir
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #2048
    cmp w2, #16
    b.hs .dn
    mov x20, x1 ; leaf
    mov w21, w2 ; depth
    mov x1, x0
    add x0, sp, #1056
    bl ua64_strcpy

    add x0, sp, #544
    add x1, sp, #1056
    mov x2, x20
    bl ua64_path_join

    add x0, sp, #544
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .sub
    mov x19, x0
.elp:
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .nd
    ldrb w0, [sp, #25]
    cbz w0, .enx
    cmp w0, #46
    b.ne .nd
    ldrb w0, [sp, #26]
    cbz w0, .enx
.nd:
    ldr w0, [sp]
    tst w0, #0x10
    b.eq .doi
    adr_sym x1, g_attra64_dirs
    ldr w1, [x1]
    cbz w1, .enx
.doi:
    add x0, sp, #544
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #544
    ldr w1, [sp]
    bl attra64_apply_one
.enx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .elp
    mov x0, x19
    call_iat __imp_FindClose

.sub:
    adr_sym x0, g_attra64_sub
    ldr w0, [x0]
    cbz w0, .dn
    add x0, sp, #544
    add x1, sp, #1056
    adr_sym x2, szAttra64Star
    bl ua64_path_join
    add x0, sp, #544
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .dn
    mov x19, x0
.slp:
    ldr w0, [sp]
    tst w0, #0x10
    b.eq .snx
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .go
    ldrb w0, [sp, #25]
    cbz w0, .snx
    cmp w0, #46
    b.ne .go
    ldrb w0, [sp, #26]
    cbz w0, .snx
.go:
    add x0, sp, #544
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #544
    mov x1, x20
    add w2, w21, #1
    bl attra64_scan_dir
.snx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .slp
    mov x0, x19
    call_iat __imp_FindClose
.dn:
    add sp, sp, #2048
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
;; @desc Entrypoint for ARM64 ATTRIB.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_attra64_set
    str wzr, [x0]
    adr_sym x0, g_attra64_clr
    str wzr, [x0]
    adr_sym x0, g_attra64_sub
    str wzr, [x0]
    adr_sym x0, g_attra64_dirs
    str wzr, [x0]
    adr_sym x0, g_attra64_hit
    str wzr, [x0]
    adr_sym x0, g_ua64_arg_b
    strb wzr, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .run
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20]
    cmp w0, #43 ; +
    b.eq .pm
    cmp w0, #45 ; -
    b.eq .mm
    cmp w0, #47 ; /
    b.eq .sw
    adr_sym x0, g_ua64_arg_b
    mov x1, x20
    bl ua64_strcpy
    b .arg_lp
.pm:
    ldrb w0, [x20, #1]
    bl ua64_chr_upper
    bl attra64_flag_bit
    cbz w0, .bad
    adr_sym x1, g_attra64_set
    ldr w2, [x1]
    orr w2, w2, w0
    str w2, [x1]
    b .arg_lp
.mm:
    ldrb w0, [x20, #1]
    bl ua64_chr_upper
    bl attra64_flag_bit
    cbz w0, .bad
    adr_sym x1, g_attra64_clr
    ldr w2, [x1]
    orr w2, w2, w0
    str w2, [x1]
    b .arg_lp
.sw:
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #83 ; S
    b.eq .ss
    cmp w0, #68 ; D
    b.eq .sd
    cmp w0, #76 ; L
    b.eq .arg_lp
    b .bad
.ss:
    adr_sym x0, g_attra64_sub
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sd:
    adr_sym x0, g_attra64_dirs
    mov w1, #1
    str w1, [x0]
    b .arg_lp

.help:
    adr_sym x0, szAttra64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szAttra64Bad
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out

.run:
    adr_sym x20, g_ua64_arg_b
    ldrb w0, [x20]
    cbnz w0, .hast
    adr_sym x0, g_ua64_arg_c
    mov x1, #1000
    bl ua64_get_cwd
    adr_sym x0, g_ua64_arg_c
    adr_sym x1, szAttra64Star
    mov w2, #0
    bl attra64_scan_dir
    b .chk

.hast:
    mov x0, x20
    bl ua64_strlen
    mov x21, #-1
    mov x1, #0
.sl:
    cmp x1, x0
    b.hs .gsl
    ldrb w2, [x20, x1]
    cmp w2, #0x5C
    b.eq .mk
    cmp w2, #47
    b.eq .mk
    cmp w2, #58
    b.ne .nx
.mk:
    mov x21, x1
.nx:
    add x1, x1, #1
    b .sl
.gsl:
    cmn x21, #1
    b.ne .spp
    adr_sym x0, g_ua64_arg_c
    mov x1, #1000
    bl ua64_get_cwd
    adr_sym x0, g_attra64_leaf
    mov x1, x20
    bl ua64_strcpy
    b .dosc
.spp:
    adr_sym x0, g_ua64_arg_c
    mov x1, #0
.cpd:
    cmp x1, x21
    b.hi .tmd
    ldrb w2, [x20, x1]
    strb w2, [x0, x1]
    add x1, x1, #1
    b .cpd
.tmd:
    strb wzr, [x0, x1]
    adr_sym x0, g_attra64_leaf
    add x1, x20, x21
    add x1, x1, #1
    bl ua64_strcpy
.dosc:
    adr_sym x0, g_ua64_arg_c
    adr_sym x1, g_attra64_leaf
    mov w2, #0
    bl attra64_scan_dir

.chk:
    adr_sym x0, g_attra64_hit
    ldr w0, [x0]
    cbnz w0, .ex0
    adr_sym x0, szAttra64Nf
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out
.ex0:
    mov w0, #0
    bl ua64_exit
.out:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szAttra64Star, "*", 0
@data db szAttra64Nf, "File not found", 0
@data db szAttra64Bad, "Parameter format not correct -", 0
@data db szAttra64Help, "Displays or changes file attributes (+R -R +A -A +S -S +H -H +I -I +O -O +P -P +U -U /S /D /L).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_attra64_set, 1
@res dword g_attra64_clr, 1
@res dword g_attra64_sub, 1
@res dword g_attra64_dirs, 1
@res dword g_attra64_hit, 1
@res byte g_attra64_leaf, 520
