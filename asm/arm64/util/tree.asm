;; @file tree.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 TREE.EXE directory visualizer ([drive:][path] [/F] [/A]).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc treea64_indent
;; @abi arm64_ms
;; @in w0 = depth (0..15)
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints vertical bar connectors up to depth w0.
@proc treea64_indent
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov w19, w0
    mov w20, #0
.lp:
    cmp w20, w19
    b.hs .dn
    adr_sym x0, g_treea64_last
    ldrb w0, [x0, x20]
    cbnz w0, .bl
    adr_sym x0, g_treea64_asc
    ldr w0, [x0]
    cbnz w0, .ba
    adr_sym x0, szTra64UniBar
    bl ua64_write_str
    b .nx
.ba:
    adr_sym x0, szTra64AscBar
    bl ua64_write_str
    b .nx
.bl:
    adr_sym x0, szTra64Blank
    bl ua64_write_str
.nx:
    add w20, w20, #1
    b .lp
.dn:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc treea64_walk
;; @abi arm64_ms
;; @in x0 = dir path UTF-8, w1 = depth
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Recursively prints files (/F) and subdirectories.
@proc treea64_walk
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #2048
    cmp w1, #15
    b.hs .dn
    mov w20, w1 ; depth
    mov x1, x0
    add x0, sp, #1056
    bl ua64_strcpy

    mov w21, #0 ; subdir total
    add x0, sp, #544
    add x1, sp, #1056
    adr_sym x2, szTra64Star
    bl ua64_path_join
    add x0, sp, #544
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .dn
    mov x19, x0
.clp:
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .cc
    ldrb w0, [sp, #25]
    cbz w0, .cnx
    cmp w0, #46
    b.ne .cc
    ldrb w0, [sp, #26]
    cbz w0, .cnx
.cc:
    ldr w0, [sp]
    tst w0, #0x10
    b.ne .isd
    adr_sym x0, g_treea64_files
    ldr w0, [x0]
    cbz w0, .cnx
    mov w0, w20
    bl treea64_indent
    adr_sym x0, szTra64Blank
    bl ua64_write_str
    add x0, sp, #24
    bl ua64_write_str
    bl ua64_write_crlf
    b .cnx
.isd:
    add w21, w21, #1
.cnx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .clp
    mov x0, x19
    call_iat __imp_FindClose

    cbz w21, .dn

    mov w22, #0 ; seen
    add x0, sp, #544
    add x1, sp, #1056
    adr_sym x2, szTra64Star
    bl ua64_path_join
    add x0, sp, #544
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .dn
    mov x19, x0
.dlp:
    ldr w0, [sp]
    tst w0, #0x10
    b.eq .dnx
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .dok
    ldrb w0, [sp, #25]
    cbz w0, .dnx
    cmp w0, #46
    b.ne .dok
    ldrb w0, [sp, #26]
    cbz w0, .dnx
.dok:
    add w22, w22, #1
    mov w0, w20
    bl treea64_indent

    adr_sym x1, g_treea64_last
    cmp w22, w21
    b.eq .last
    strb wzr, [x1, x20]
    adr_sym x0, g_treea64_asc
    ldr w0, [x0]
    cbnz w0, .ma
    adr_sym x0, szTra64UniMid
    b .prb
.ma:
    adr_sym x0, szTra64AscMid
    b .prb
.last:
    mov w0, #1
    strb w0, [x1, x20]
    adr_sym x0, g_treea64_asc
    ldr w0, [x0]
    cbnz w0, .ea
    adr_sym x0, szTra64UniEnd
    b .prb
.ea:
    adr_sym x0, szTra64AscEnd
.prb:
    bl ua64_write_str
    add x0, sp, #24
    bl ua64_write_str
    bl ua64_write_crlf

    add x0, sp, #544
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #544
    add w1, w20, #1
    bl treea64_walk
.dnx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .dlp
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
;; @err Exits 1 on invalid path
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 TREE.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_treea64_files
    str wzr, [x0]
    adr_sym x0, g_treea64_asc
    str wzr, [x0]
    adr_sym x0, g_ua64_arg_b
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
    cmp w0, #70 ; F
    b.eq .sf
    cmp w0, #65 ; A
    b.eq .sa
    b .arg_lp
.sf:
    adr_sym x0, g_treea64_files
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sa:
    adr_sym x0, g_treea64_asc
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.pos:
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp

.help:
    adr_sym x0, szTra64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.run:
    adr_sym x0, g_ua64_arg_b
    ldrb w0, [x0]
    cbnz w0, .chk
    adr_sym x0, g_ua64_arg_b
    mov x1, #1000
    bl ua64_get_cwd
.chk:
    adr_sym x0, g_ua64_arg_b
    bl ua64_get_attr
    cmn w0, #1
    b.eq .bad
    tst w0, #0x10
    b.eq .bad

    adr_sym x0, szTra64Hdr
    bl ua64_write_str
    bl ua64_write_crlf
    adr_sym x0, g_ua64_arg_b
    bl ua64_write_str
    bl ua64_write_crlf

    adr_sym x0, g_ua64_arg_b
    mov w1, #0
    bl treea64_walk
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szTra64Inv
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szTra64Star, "*", 0
@data db szTra64Hdr, "Folder PATH listing", 0
@data db szTra64Inv, "Invalid path", 0
@data db szTra64AscMid, "+---", 0
@data db szTra64AscEnd, 0x5C, "---", 0
@data db szTra64AscBar, "|   ", 0
@data db szTra64UniMid, 0xE2, 0x94, 0x9C, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTra64UniEnd, 0xE2, 0x94, 0x94, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTra64UniBar, 0xE2, 0x94, 0x82, "   ", 0
@data db szTra64Blank, "    ", 0
@data db szTra64Help, "Graphically displays the folder structure of a drive or path ([path] [/F] [/A]).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_treea64_files, 1
@res dword g_treea64_asc, 1
@res byte g_treea64_last, 32
