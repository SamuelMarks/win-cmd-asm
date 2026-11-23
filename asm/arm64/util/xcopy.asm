;; @file xcopy.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 XCOPY.EXE file/tree copier (/A /M /D /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc xca64_is_excluded
;; @abi arm64_ms
;; @in x0 = candidate path UTF-8
;; @out w0 = 1 if path matches any /EXCLUDE substring, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Checks candidate path against newline-separated substrings in g_xca64_excl.
@proc xca64_is_excluded
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    adr_sym x1, g_xca64_has_excl
    ldr w1, [x1]
    cbz w1, .no
    mov x19, x0
    bl ua64_strlen
    mov x20, x0
    adr_sym x21, g_xca64_excl

.ln_lp:
    ldrb w0, [x21]
    cbz w0, .no
    mov x22, x21
.eol:
    ldrb w0, [x22]
    cbz w0, .got
    cmp w0, #13
    b.eq .got
    cmp w0, #10
    b.eq .got
    add x22, x22, #1
    b .eol
.got:
    subs x4, x22, x21
    b.eq .adv
    cmp x20, x4
    b.lo .adv
    sub x5, x20, x4
    add x5, x5, #1
    mov x6, x19
.pos:
    mov x7, #0
.ch:
    cmp x7, x4
    b.hs .yes
    ldrb w0, [x6, x7]
    cmp w0, #97
    b.lo .u1
    cmp w0, #122
    b.hi .u1
    sub w0, w0, #32
.u1:
    ldrb w1, [x21, x7]
    cmp w1, #97
    b.lo .u2
    cmp w1, #122
    b.hi .u2
    sub w1, w1, #32
.u2:
    cmp w0, w1
    b.ne .nxp
    add x7, x7, #1
    b .ch
.nxp:
    add x6, x6, #1
    subs x5, x5, #1
    b.ne .pos

.adv:
    ldrb w0, [x22]
    cmp w0, #13
    b.ne .clf
    add x22, x22, #1
.clf:
    ldrb w0, [x22]
    cmp w0, #10
    b.ne .nxl
    add x22, x22, #1
.nxl:
    mov x21, x22
    b .ln_lp
.yes:
    mov w0, #1
    b .out
.no:
    mov w0, #0
.out:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc xca64_copy_one
;; @abi arm64_ms
;; @in x0 = src UTF-8, x1 = dst UTF-8, w2 = src attributes
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Copies a single file honoring XCOPY filter and attribute switches.
@proc xca64_copy_one
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    adr_sym x3, g_xca64_tree
    ldr w3, [x3]
    cbnz w3, .dn
    mov x19, x0
    mov x20, x1
    mov w21, w2

    adr_sym x3, g_xca64_hid
    ldr w3, [x3]
    cbnz w3, .ca
    tst w21, #0x06
    b.ne .dn
.ca:
    adr_sym x3, g_xca64_arch
    ldr w3, [x3]
    cbz w3, .ce
    tst w21, #0x20
    b.eq .dn
.ce:
    mov x0, x19
    bl xca64_is_excluded
    cbnz w0, .dn

    adr_sym x3, g_xca64_upd
    ldr w3, [x3]
    cbz w3, .cl
    mov x0, x20
    bl ua64_get_attr
    cmn w0, #1
    b.eq .dn

.cl:
    adr_sym x3, g_xca64_quiet
    ldr w3, [x3]
    cbnz w3, .cd
    mov x0, x19
    bl ua64_write_str
    adr_sym x3, g_xca64_full
    ldr w3, [x3]
    cbz w3, .nl
    adr_sym x0, szXca64Arrow
    bl ua64_write_str
    mov x0, x20
    bl ua64_write_str
.nl:
    bl ua64_write_crlf

.cd:
    adr_sym x3, g_xca64_list
    ldr w3, [x3]
    cbz w3, .cp
    adr_sym x3, g_xca64_copied
    ldr w4, [x3]
    add w4, w4, #1
    str w4, [x3]
    b .dn

.cp:
    adr_sym x3, g_xca64_ro
    ldr w3, [x3]
    cbz w3, .docp
    mov x0, x20
    bl ua64_get_attr
    cmn w0, #1
    b.eq .docp
    tst w0, #1
    b.eq .docp
    mov x0, x20
    mov w1, #0x80
    bl ua64_set_attr

.docp:
    mov x0, x19
    mov x1, x20
    mov w2, #0
    bl ua64_copy_file
    cbz w0, .dn
    adr_sym x3, g_xca64_copied
    ldr w4, [x3]
    add w4, w4, #1
    str w4, [x3]

    adr_sym x3, g_xca64_keep
    ldr w3, [x3]
    cbnz w3, .cm
    mov x0, x20
    mov w1, #0x20
    bl ua64_set_attr
.cm:
    adr_sym x3, g_xca64_clr_a
    ldr w3, [x3]
    cbz w3, .dn
    mov w4, #0x20
    bic w21, w21, w4
    cbnz w21, .ms
    mov w21, #0x80
.ms:
    mov x0, x19
    mov w1, w21
    bl ua64_set_attr
.dn:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc xca64_copy_dir
;; @abi arm64_ms
;; @in x0 = src dir, x1 = dst dir, w2 = depth
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Copies directory contents and recurses under /S or /E.
@proc xca64_copy_dir
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #2624
    cmp w2, #16
    b.hs .dn
    mov w21, w2 ; depth
    mov w22, #0 ; created_dst
    mov x20, x1 ; save dst
    mov x1, x0
    add x0, sp, #544 ; src dir
    bl ua64_strcpy
    add x0, sp, #1056 ; dst dir
    mov x1, x20
    bl ua64_strcpy

    adr_sym x0, g_xca64_empty
    ldr w0, [x0]
    cbnz w0, .mk
    adr_sym x0, g_xca64_tree
    ldr w0, [x0]
    cbz w0, .en
.mk:
    adr_sym x0, g_xca64_list
    ldr w0, [x0]
    cbnz w0, .en
    add x0, sp, #1056
    bl ua64_mkdir
    mov w22, #1

.en:
    add x0, sp, #1568
    add x1, sp, #544
    adr_sym x2, szXca64Star
    bl ua64_path_join
    add x0, sp, #1568
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .dn
    mov x19, x0

.lp:
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .nd
    ldrb w0, [sp, #25]
    cbz w0, .nx
    cmp w0, #46
    b.ne .nd
    ldrb w0, [sp, #26]
    cbz w0, .nx
.nd:
    add x0, sp, #1568
    add x1, sp, #544
    add x2, sp, #24
    bl ua64_path_join

    add x0, sp, #2080
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join

    ldr w0, [sp]
    tst w0, #0x10
    b.ne .sub

    cbnz w22, .df
    adr_sym x0, g_xca64_list
    ldr w0, [x0]
    cbnz w0, .df
    add x0, sp, #1056
    bl ua64_mkdir
    mov w22, #1
.df:
    add x0, sp, #1568
    add x1, sp, #2080
    ldr w2, [sp]
    bl xca64_copy_one
    b .nx

.sub:
    adr_sym x0, g_xca64_sub
    ldr w0, [x0]
    cbz w0, .nx
    add x0, sp, #1568
    add x1, sp, #2080
    add w2, w21, #1
    bl xca64_copy_dir
.nx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .lp
    mov x0, x19
    call_iat __imp_FindClose
.dn:
    add sp, sp, #2624
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 4=error)
;; @err Exits 4 on syntax/missing error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 XCOPY.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_xca64_arch
    str wzr, [x0]
    adr_sym x0, g_xca64_clr_a
    str wzr, [x0]
    adr_sym x0, g_xca64_sub
    str wzr, [x0]
    adr_sym x0, g_xca64_empty
    str wzr, [x0]
    adr_sym x0, g_xca64_quiet
    str wzr, [x0]
    adr_sym x0, g_xca64_full
    str wzr, [x0]
    adr_sym x0, g_xca64_list
    str wzr, [x0]
    adr_sym x0, g_xca64_hid
    str wzr, [x0]
    adr_sym x0, g_xca64_ro
    str wzr, [x0]
    adr_sym x0, g_xca64_tree
    str wzr, [x0]
    adr_sym x0, g_xca64_upd
    str wzr, [x0]
    adr_sym x0, g_xca64_keep
    str wzr, [x0]
    adr_sym x0, g_xca64_has_excl
    str wzr, [x0]
    adr_sym x0, g_xca64_copied
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
    cmp w0, #65 ; A
    b.eq .sa
    cmp w0, #77 ; M
    b.eq .sm
    cmp w0, #83 ; S
    b.eq .ss
    cmp w0, #69 ; E
    b.eq .se
    cmp w0, #81 ; Q
    b.eq .sq
    cmp w0, #70 ; F
    b.eq .sf
    cmp w0, #76 ; L
    b.eq .sl
    cmp w0, #72 ; H
    b.eq .sh
    cmp w0, #82 ; R
    b.eq .sr
    cmp w0, #84 ; T
    b.eq .st
    cmp w0, #85 ; U
    b.eq .su
    cmp w0, #75 ; K
    b.eq .sk
    b .arg_lp
.sa:
    adr_sym x0, g_xca64_arch
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sm:
    adr_sym x0, g_xca64_arch
    mov w1, #1
    str w1, [x0]
    adr_sym x0, g_xca64_clr_a
    str w1, [x0]
    b .arg_lp
.ss:
    adr_sym x0, g_xca64_sub
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.se:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #88 ; X
    b.eq .sex
    adr_sym x0, g_xca64_sub
    mov w1, #1
    str w1, [x0]
    adr_sym x0, g_xca64_empty
    str w1, [x0]
    b .arg_lp
.sex:
    mov x1, #0
.fcl:
    ldrb w0, [x20, x1]
    cbz w0, .arg_lp
    cmp w0, #58 ; :
    b.eq .gcl
    add x1, x1, #1
    b .fcl
.gcl:
    add x0, x20, x1
    add x0, x0, #1
    adr_sym x1, g_xca64_excl
    mov x2, #4000
    mov w3, #1
    bl ua64_load_stream
    cbnz w1, .arg_lp
    adr_sym x0, g_xca64_has_excl
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sq:
    adr_sym x0, g_xca64_quiet
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sf:
    adr_sym x0, g_xca64_full
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sl:
    adr_sym x0, g_xca64_list
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sh:
    adr_sym x0, g_xca64_hid
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sr:
    adr_sym x0, g_xca64_ro
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.st:
    adr_sym x0, g_xca64_tree
    mov w1, #1
    str w1, [x0]
    adr_sym x0, g_xca64_sub
    str w1, [x0]
    b .arg_lp
.su:
    adr_sym x0, g_xca64_upd
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sk:
    adr_sym x0, g_xca64_keep
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
    adr_sym x0, szXca64Help
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
    cbnz w0, .gd
    adr_sym x0, g_ua64_arg_c
    mov x1, #1000
    bl ua64_get_cwd
.gd:
    adr_sym x0, g_ua64_arg_b
    bl ua64_get_attr
    cmn w0, #1
    b.eq .nf
    tst w0, #0x10
    b.eq .sfile
    adr_sym x0, g_ua64_arg_b
    adr_sym x1, g_ua64_arg_c
    mov w2, #0
    bl xca64_copy_dir
    b .sum

.sfile:
    mov w21, w0
    adr_sym x0, g_ua64_arg_c
    bl ua64_get_attr
    cmn w0, #1
    b.eq .ddst
    tst w0, #0x10
    b.eq .ddst

    adr_sym x20, g_ua64_arg_b
    mov x0, x20
    bl ua64_strlen
    mov x2, #-1
    mov x1, #0
.sll:
    cmp x1, x0
    b.hs .sld
    ldrb w3, [x20, x1]
    cmp w3, #0x5C
    b.eq .slm
    cmp w3, #47
    b.ne .sln
.slm:
    mov x2, x1
.sln:
    add x1, x1, #1
    b .sll
.sld:
    add x2, x20, x2
    add x2, x2, #1
    adr_sym x0, g_ua64_path_tmp
    adr_sym x1, g_ua64_arg_c
    bl ua64_path_join
    adr_sym x0, g_ua64_arg_b
    adr_sym x1, g_ua64_path_tmp
    mov w2, w21
    bl xca64_copy_one
    b .sum

.ddst:
    adr_sym x0, g_ua64_arg_b
    adr_sym x1, g_ua64_arg_c
    mov w2, w21
    bl xca64_copy_one

.sum:
    adr_sym x0, g_xca64_copied
    ldr w0, [x0]
    bl ua64_print_u64
    adr_sym x0, szXca64Sum
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.nf:
    adr_sym x0, szXca64Nf
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #4
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szXca64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #4
    bl ua64_exit
.out:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szXca64Star, "*", 0
@data db szXca64Arrow, " -> ", 0
@data db szXca64Sum, " File(s) copied", 0
@data db szXca64Nf, "File not found", 0
@data db szXca64Syn, "Invalid number of parameters", 0
@data db szXca64Help, "Copies files and directory trees (/A /M /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_xca64_arch, 1
@res dword g_xca64_clr_a, 1
@res dword g_xca64_sub, 1
@res dword g_xca64_empty, 1
@res dword g_xca64_quiet, 1
@res dword g_xca64_full, 1
@res dword g_xca64_list, 1
@res dword g_xca64_hid, 1
@res dword g_xca64_ro, 1
@res dword g_xca64_tree, 1
@res dword g_xca64_upd, 1
@res dword g_xca64_keep, 1
@res dword g_xca64_has_excl, 1
@res dword g_xca64_copied, 1
@res byte g_xca64_excl, 4096
