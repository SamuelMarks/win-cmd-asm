;; @file replace.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 REPLACE.EXE file updater (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc repa64_get_mtime
;; @abi arm64_ms
;; @in x0 = file path UTF-8, x1 = 8B FILETIME output
;; @out w0 = 1 on success, 0 on failure
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries last-write FILETIME via OpenRead + GetFileTime.
@proc repa64_get_mtime
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x20, x1
    bl ua64_open_read
    cmn x0, #1
    b.eq .fail
    mov x19, x0
    mov x0, x19
    mov x1, #0
    mov x2, #0
    mov x3, x20
    call_iat __imp_GetFileTime
    mov w20, w0
    mov x0, x19
    call_iat __imp_CloseHandle
    mov w0, w20
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
.fail:
    mov w0, #0
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc repa64_apply_dir
;; @abi arm64_ms
;; @in x0 = target directory UTF-8, w1 = depth
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Replaces existing target files (or adds under /A) and recurses under /S.
@proc repa64_apply_dir
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #1664
    cmp w1, #16
    b.hs .dn
    mov w21, w1
    mov x1, x0
    add x0, sp, #544
    bl ua64_strcpy

    add x0, sp, #1056
    add x1, sp, #544
    adr_sym x2, g_repa64_leaf
    bl ua64_path_join

    add x0, sp, #1056
    bl ua64_get_attr
    mov w20, w0

    adr_sym x0, g_repa64_add
    ldr w0, [x0]
    cbnz w0, .add_m

    cmn w20, #1
    b.eq .sub
    tst w20, #0x10
    b.ne .sub

    adr_sym x0, g_repa64_older
    ldr w0, [x0]
    cbz w0, .cro
    add x0, sp, #1056
    add x1, sp, #1600
    bl repa64_get_mtime
    cbz w0, .cro
    adr_sym x0, g_repa64_sft
    add x1, sp, #1600
    call_iat __imp_CompareFileTime
    cmp w0, #0
    b.le .sub

.cro:
    tst w20, #1
    b.eq .do_r
    adr_sym x0, g_repa64_ro
    ldr w0, [x0]
    cbz w0, .sub
    add x0, sp, #1056
    mov w1, #0x80
    bl ua64_set_attr
.do_r:
    adr_sym x0, g_repa64_src
    add x1, sp, #1056
    mov w2, #0
    bl ua64_copy_file
    cbz w0, .sub
    adr_sym x0, g_repa64_cnt
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]
    b .sub

.add_m:
    cmn w20, #1
    b.ne .sub
    adr_sym x0, g_repa64_src
    add x1, sp, #1056
    mov w2, #1
    bl ua64_copy_file
    cbz w0, .sub
    adr_sym x0, g_repa64_cnt
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]

.sub:
    adr_sym x0, g_repa64_sub
    ldr w0, [x0]
    cbz w0, .dn
    add x0, sp, #1056
    add x1, sp, #544
    adr_sym x2, szRepa64Star
    bl ua64_path_join
    add x0, sp, #1056
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
    add x0, sp, #1056
    add x1, sp, #544
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #1056
    add w1, w21, #1
    bl repa64_apply_dir
.snx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .slp
    mov x0, x19
    call_iat __imp_FindClose
.dn:
    add sp, sp, #1664
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
;; @err Exits non-zero on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 REPLACE.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_repa64_add
    str wzr, [x0]
    adr_sym x0, g_repa64_ro
    str wzr, [x0]
    adr_sym x0, g_repa64_sub
    str wzr, [x0]
    adr_sym x0, g_repa64_older
    str wzr, [x0]
    adr_sym x0, g_repa64_cnt
    str wzr, [x0]
    adr_sym x0, g_repa64_src
    strb wzr, [x0]
    adr_sym x0, g_repa64_dst
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
    cmp w0, #82 ; R
    b.eq .sr
    cmp w0, #83 ; S
    b.eq .ss
    cmp w0, #85 ; U
    b.eq .su
    b .arg_lp
.sa:
    adr_sym x0, g_repa64_add
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sr:
    adr_sym x0, g_repa64_ro
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.ss:
    adr_sym x0, g_repa64_sub
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.su:
    adr_sym x0, g_repa64_older
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.pos:
    adr_sym x0, g_repa64_src
    ldrb w0, [x0]
    cbnz w0, .pos2
    mov x0, x19
    adr_sym x1, g_repa64_src
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.pos2:
    mov x0, x19
    adr_sym x1, g_repa64_dst
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp

.help:
    adr_sym x0, szRepa64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.run:
    adr_sym x20, g_repa64_src
    ldrb w0, [x20]
    cbz w0, .bad
    mov x0, x20
    bl ua64_get_attr
    cmn w0, #1
    b.eq .nosrc

    mov x0, x20
    adr_sym x1, g_repa64_sft
    bl repa64_get_mtime

    adr_sym x0, g_repa64_dst
    ldrb w1, [x0]
    cbnz w1, .leaf
    mov x1, #1000
    bl ua64_get_cwd
.leaf:
    mov x0, x20
    bl ua64_strlen
    mov x2, #-1
    mov x1, #0
.sl:
    cmp x1, x0
    b.hs .sld
    ldrb w3, [x20, x1]
    cmp w3, #0x5C
    b.eq .mk
    cmp w3, #47
    b.eq .mk
    cmp w3, #58
    b.ne .nx
.mk:
    mov x2, x1
.nx:
    add x1, x1, #1
    b .sl
.sld:
    add x1, x20, x2
    add x1, x1, #1
    adr_sym x0, g_repa64_leaf
    bl ua64_strcpy

    adr_sym x0, g_repa64_dst
    mov w1, #0
    bl repa64_apply_dir

    adr_sym x0, g_repa64_cnt
    ldr w0, [x0]
    bl ua64_print_u64
    adr_sym x0, g_repa64_add
    ldr w0, [x0]
    cbnz w0, .added
    adr_sym x0, szRepa64Done
    b .pm
.added:
    adr_sym x0, szRepa64Added
.pm:
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.nosrc:
    adr_sym x0, szRepa64NoSrc
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #2
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szRepa64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #3
    bl ua64_exit
.out:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szRepa64Star, "*", 0
@data db szRepa64Done, " file(s) replaced", 0
@data db szRepa64Added, " file(s) added", 0
@data db szRepa64NoSrc, "Source path required", 0
@data db szRepa64Syn, "Invalid parameter", 0
@data db szRepa64Help, "Replaces files in destination directories (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res qword g_repa64_sft, 1
@res dword g_repa64_add, 1
@res dword g_repa64_ro, 1
@res dword g_repa64_sub, 1
@res dword g_repa64_older, 1
@res dword g_repa64_cnt, 1
@res byte g_repa64_src, 520
@res byte g_repa64_dst, 520
@res byte g_repa64_leaf, 520
