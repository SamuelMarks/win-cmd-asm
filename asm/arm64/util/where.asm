;; @file where.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 WHERE.EXE executable locator ([/R dir] [/Q] [/F] [/T] pattern / $ENV:pattern).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc wherea64_emit_hit
;; @abi arm64_ms
;; @in x0 = matched path UTF-8, x1 = 64-bit size
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Records hit and prints path (honoring /Q, /F, /T).
@proc wherea64_emit_hit
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    mov x20, x1
    adr_sym x0, g_wha64_found
    mov w2, #1
    str w2, [x0]
    adr_sym x0, g_wha64_quiet
    ldr w0, [x0]
    cbnz w0, .dn
    adr_sym x0, g_wha64_time
    ldr w0, [x0]
    cbz w0, .cq
    mov x0, x20
    bl ua64_print_u64
    adr_sym x0, szWha64Pad
    bl ua64_write_str
.cq:
    adr_sym x0, g_wha64_quote
    ldr w0, [x0]
    cbz w0, .pl
    mov w0, #34
    bl ua64_write_chr
    mov x0, x19
    bl ua64_write_str
    mov w0, #34
    bl ua64_write_chr
    bl ua64_write_crlf
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
.pl:
    mov x0, x19
    bl ua64_write_str
    bl ua64_write_crlf
.dn:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc wherea64_probe_one
;; @abi arm64_ms
;; @in x0 = candidate path/wildcard, x1 = parent dir
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Enumerates matches for x0 in directory x1.
@proc wherea64_probe_one
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    sub sp, sp, #1600
    mov x20, x1
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .dn
    mov x19, x0
.lp:
    ldr w0, [sp]
    tst w0, #0x10
    b.ne .nx
    add x0, sp, #544
    mov x1, x20
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #544
    ldr x1, [sp, #16]
    bl wherea64_emit_hit
.nx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .lp
    mov x0, x19
    call_iat __imp_FindClose
.dn:
    add sp, sp, #1600
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc wherea64_probe_dir
;; @abi arm64_ms
;; @in x0 = dir UTF-8, x1 = pattern UTF-8
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Probes exact pattern and .COM/.EXE/.BAT/.CMD extensions in x0.
@proc wherea64_probe_dir
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #1024
    mov x19, x0 ; dir
    mov x20, x1 ; pat

    mov x2, #0
    mov w21, #0
.dot:
    ldrb w0, [x20, x2]
    cbz w0, .ex
    cmp w0, #46
    b.ne .dnx
    mov w21, #1
.dnx:
    add x2, x2, #1
    b .dot

.ex:
    mov x0, sp
    mov x1, x19
    mov x2, x20
    bl ua64_path_join
    mov x0, sp
    mov x1, x19
    bl wherea64_probe_one

    cbnz w21, .dn

    mov x0, sp
    mov x1, x19
    mov x2, x20
    bl ua64_path_join
    mov x0, sp
    bl ua64_strlen
    mov x22, x0

    add x0, sp, x22
    adr_sym x1, szWha64Com
    bl ua64_strcpy
    mov x0, sp
    mov x1, x19
    bl wherea64_probe_one

    add x0, sp, x22
    adr_sym x1, szWha64Exe
    bl ua64_strcpy
    mov x0, sp
    mov x1, x19
    bl wherea64_probe_one

    add x0, sp, x22
    adr_sym x1, szWha64Bat
    bl ua64_strcpy
    mov x0, sp
    mov x1, x19
    bl wherea64_probe_one

    add x0, sp, x22
    adr_sym x1, szWha64Cmd
    bl ua64_strcpy
    mov x0, sp
    mov x1, x19
    bl wherea64_probe_one
.dn:
    add sp, sp, #1024
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc wherea64_recurse
;; @abi arm64_ms
;; @in x0 = dir, x1 = pat, w2 = depth
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Recursively searches directory tree (/R dir).
@proc wherea64_recurse
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #2048
    cmp w2, #16
    b.hs .dn
    mov x20, x1
    mov w21, w2
    mov x1, x0
    add x0, sp, #1056
    bl ua64_strcpy

    add x0, sp, #1056
    mov x1, x20
    bl wherea64_probe_dir

    add x0, sp, #544
    add x1, sp, #1056
    adr_sym x2, szWha64Star
    bl ua64_path_join
    add x0, sp, #544
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .dn
    mov x19, x0
.lp:
    ldr w0, [sp]
    tst w0, #0x10
    b.eq .nx
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .go
    ldrb w0, [sp, #25]
    cbz w0, .nx
    cmp w0, #46
    b.ne .go
    ldrb w0, [sp, #26]
    cbz w0, .nx
.go:
    add x0, sp, #544
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #544
    mov x1, x20
    add w2, w21, #1
    bl wherea64_recurse
.nx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .lp
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
;; @err Exits 2 on missing pattern
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 WHERE.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_wha64_quiet
    str wzr, [x0]
    adr_sym x0, g_wha64_quote
    str wzr, [x0]
    adr_sym x0, g_wha64_time
    str wzr, [x0]
    adr_sym x0, g_wha64_rec
    str wzr, [x0]
    adr_sym x0, g_wha64_found
    str wzr, [x0]
    adr_sym x0, g_wha64_pat
    strb wzr, [x0]
    adr_sym x0, g_wha64_rdir
    strb wzr, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .run
    cmp w1, #47
    b.ne .pat
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #81 ; Q
    b.eq .sq
    cmp w0, #70 ; F
    b.eq .sf
    cmp w0, #84 ; T
    b.eq .st
    cmp w0, #82 ; R
    b.eq .sr
    b .bad
.sq:
    adr_sym x0, g_wha64_quiet
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sf:
    adr_sym x0, g_wha64_quote
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.st:
    adr_sym x0, g_wha64_time
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sr:
    adr_sym x0, g_wha64_rec
    mov w1, #1
    str w1, [x0]
    mov x0, x19
    adr_sym x1, g_wha64_rdir
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.pat:
    adr_sym x1, g_wha64_pat
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp

.help:
    adr_sym x0, szWha64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szWha64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #2
    bl ua64_exit
    b .out

.run:
    adr_sym x20, g_wha64_pat
    ldrb w0, [x20]
    cbz w0, .bad

    adr_sym x0, g_wha64_rec
    ldr w0, [x0]
    cbz w0, .cenv
    adr_sym x0, g_wha64_rdir
    mov x1, x20
    mov w2, #0
    bl wherea64_recurse
    b .fin

.cenv:
    ldrb w0, [x20]
    cmp w0, #36 ; $
    b.ne .std
    add x0, x20, #1
    adr_sym x1, g_ua64_arg_a
    mov x2, #0
.fc:
    ldrb w3, [x0, x2]
    cbz w3, .std
    cmp w3, #58 ; :
    b.eq .gc
    strb w3, [x1, x2]
    add x2, x2, #1
    b .fc
.gc:
    strb wzr, [x1, x2]
    add x1, x0, x2
    add x1, x1, #1
    adr_sym x0, g_ua64_arg_b
    bl ua64_strcpy
    mov x0, x20
    adr_sym x1, g_ua64_arg_b
    bl ua64_strcpy
    adr_sym x0, g_ua64_arg_a
    adr_sym x1, g_ua64_arena
    ldr x1, [x1]
    mov_imm32 x2, 32768
    bl ua64_get_env
    b .walk

.std:
    adr_sym x0, g_ua64_arg_c
    mov x1, #1000
    bl ua64_get_cwd
    adr_sym x0, g_ua64_arg_c
    mov x1, x20
    bl wherea64_probe_dir

    adr_sym x0, szWha64Path
    adr_sym x1, g_ua64_arena
    ldr x1, [x1]
    mov_imm32 x2, 32768
    bl ua64_get_env

.walk:
    adr_sym x0, g_ua64_arena
    ldr x19, [x0]
.slp:
    ldrb w0, [x19]
    cbz w0, .fin
    adr_sym x21, g_ua64_arg_c
.cps:
    ldrb w0, [x19]
    cbz w0, .gs
    cmp w0, #59 ; ;
    b.eq .as
    strb w0, [x21], #1
    add x19, x19, #1
    b .cps
.as:
    add x19, x19, #1
.gs:
    strb wzr, [x21]
    adr_sym x0, g_ua64_arg_c
    ldrb w1, [x0]
    cbz w1, .slp
    mov x1, x20
    bl wherea64_probe_dir
    b .slp

.fin:
    adr_sym x0, g_wha64_found
    ldr w0, [x0]
    cbnz w0, .ex0
    adr_sym x0, g_wha64_quiet
    ldr w0, [x0]
    cbnz w0, .ex1
    adr_sym x0, szWha64Nf
    bl ua64_write_err_str
    bl ua64_write_crlf
.ex1:
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

@data db szWha64Path, "PATH", 0
@data db szWha64Star, "*", 0
@data db szWha64Com, ".COM", 0
@data db szWha64Exe, ".EXE", 0
@data db szWha64Bat, ".BAT", 0
@data db szWha64Cmd, ".CMD", 0
@data db szWha64Pad, "   ", 0
@data db szWha64Nf, "INFO: Could not find files for the given pattern(s).", 0
@data db szWha64Syn, "ERROR: Invalid syntax.", 0
@data db szWha64Help, "Displays the location of files that match the search pattern (/R /Q /F /T).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_wha64_quiet, 1
@res dword g_wha64_quote, 1
@res dword g_wha64_time, 1
@res dword g_wha64_rec, 1
@res dword g_wha64_found, 1
@res byte g_wha64_pat, 520
@res byte g_wha64_rdir, 520
