;; @file findstr.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 FINDSTR.EXE with 64-bit Thompson NFA bitset regex engine (/B /E /L /R /S /I /X /V /N /M /O /P /C).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc fsa64_compile_nfa
;; @abi arm64_ms
;; @in x0 = pattern string, w1 = literal_only (1=/L, 0=/R)
;; @out Populates 60-state Thompson NFA tables
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Compiles pattern into up to 60 Thompson NFA state slots.
@proc fsa64_compile_nfa
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    mov x19, x0 ; pat
    mov w20, w1 ; lit_only
    mov x21, #0 ; state idx
    mov x22, #0 ; star_mask

    adr_sym x0, g_nfaa64_bol
    str wzr, [x0]
    adr_sym x0, g_nfaa64_eol
    str wzr, [x0]

    cbnz w20, .clp
    ldrb w0, [x19]
    cmp w0, #94 ; ^
    b.ne .clp
    adr_sym x1, g_nfaa64_bol
    mov w2, #1
    str w2, [x1]
    add x19, x19, #1

.clp:
    ldrb w0, [x19]
    cbz w0, .cdn
    cmp x21, #60
    b.hs .cdn

    cbnz w20, .lit

    cmp w0, #36 ; $
    b.ne .neol
    ldrb w1, [x19, #1]
    cbnz w1, .neol
    adr_sym x1, g_nfaa64_eol
    mov w2, #1
    str w2, [x1]
    add x19, x19, #1
    b .cdn

.neol:
    cmp w0, #46 ; .
    b.ne .ndot
    adr_sym x1, g_nfaa64_kind
    mov w2, #2
    strb w2, [x1, x21]
    add x19, x19, #1
    b .cstar

.ndot:
    cmp w0, #0x5C
    b.ne .nesc
    ldrb w1, [x19, #1]
    cbz w1, .lit
    add x19, x19, #1
    ldrb w0, [x19]
    adr_sym x1, g_nfaa64_kind
    cmp w0, #100
    b.eq .ed
    cmp w0, #119
    b.eq .ew
    cmp w0, #115
    b.eq .es
    b .lit
.ed:
    mov w2, #4
    strb w2, [x1, x21]
    add x19, x19, #1
    b .cstar
.ew:
    mov w2, #5
    strb w2, [x1, x21]
    add x19, x19, #1
    b .cstar
.es:
    mov w2, #6
    strb w2, [x1, x21]
    add x19, x19, #1
    b .cstar

.nesc:
    cmp w0, #91 ; [
    b.ne .lit
    add x19, x19, #1
    adr_sym x8, g_nfaa64_bmaps
    add x8, x8, x21, lsl #5
    mov x1, #0
.zr:
    cmp x1, #32
    b.hs .zrd
    strb wzr, [x8, x1]
    add x1, x1, #1
    b .zr
.zrd:
    mov w9, #0
    ldrb w0, [x19]
    cmp w0, #94
    b.ne .clslp
    mov w9, #1
    add x19, x19, #1
.clslp:
    ldrb w0, [x19]
    cbz w0, .clsend
    cmp w0, #93
    b.ne .clsit
    add x19, x19, #1
    b .clsend
.clsit:
    add x19, x19, #1
    ldrb w1, [x19]
    cmp w1, #45
    b.ne .cls1
    ldrb w1, [x19, #1]
    cbz w1, .cls1
    cmp w1, #93
    b.eq .cls1
    add x19, x19, #2
.rng:
    cmp w0, w1
    b.hi .clslp
    and w2, w0, #7
    lsr w3, w0, #3
    mov w4, #1
    lsl w4, w4, w2
    ldrb w5, [x8, x3]
    orr w5, w5, w4
    strb w5, [x8, x3]
    add w0, w0, #1
    b .rng
.cls1:
    and w2, w0, #7
    lsr w3, w0, #3
    mov w4, #1
    lsl w4, w4, w2
    ldrb w5, [x8, x3]
    orr w5, w5, w4
    strb w5, [x8, x3]
    b .clslp
.clsend:
    cbz w9, .clsst
    mov x1, #0
.inv:
    cmp x1, #32
    b.hs .clsst
    ldrb w2, [x8, x1]
    mvn w2, w2
    strb w2, [x8, x1]
    add x1, x1, #1
    b .inv
.clsst:
    adr_sym x1, g_nfaa64_kind
    mov w2, #3
    strb w2, [x1, x21]
    b .cstar

.lit:
    adr_sym x1, g_fsa64_ci
    ldr w1, [x1]
    cbz w1, .stlit
    bl ua64_chr_upper
.stlit:
    adr_sym x1, g_nfaa64_kind
    mov w2, #1
    strb w2, [x1, x21]
    adr_sym x1, g_nfaa64_ch
    strb w0, [x1, x21]
    add x19, x19, #1

.cstar:
    cbnz w20, .nst
    ldrb w0, [x19]
    cmp w0, #42 ; *
    b.ne .nst
    add x19, x19, #1
    mov x0, #1
    lsl x0, x0, x21
    orr x22, x22, x0
.nst:
    add x21, x21, #1
    b .clp

.cdn:
    adr_sym x0, g_nfaa64_len
    str w21, [x0]
    adr_sym x0, g_nfaa64_star
    str x22, [x0]
    mov x0, #1
    lsl x0, x0, x21
    adr_sym x1, g_nfaa64_acc
    str x0, [x1]

    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc fsa64_eps
;; @abi arm64_ms
;; @in x0 = 64-bit state bitset
;; @out x0 = epsilon-closed state bitset
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Propagates Kleene-star epsilon transitions across 64-bit bitset.
@proc fsa64_eps
    adr_sym x1, g_nfaa64_star
    ldr x1, [x1]
    cbz x1, .dn
    mov w2, #60
.lp:
    and x3, x0, x1
    lsl x3, x3, #1
    orr x0, x0, x3
    subs w2, w2, #1
    b.ne .lp
.dn:
    ret
@endproc

;; @proc fsa64_atom
;; @abi arm64_ms
;; @in x0 = state idx, w1 = input byte
;; @out w0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Evaluates one NFA state atom against w1.
@proc fsa64_atom
    adr_sym x2, g_fsa64_ci
    ldr w2, [x2]
    cbz w2, .gc
    cmp w1, #97
    b.lo .gc
    cmp w1, #122
    b.hi .gc
    sub w1, w1, #32
.gc:
    adr_sym x2, g_nfaa64_kind
    ldrb w2, [x2, x0]
    cmp w2, #1
    b.eq .kl
    cmp w2, #2
    b.eq .yes
    cmp w2, #3
    b.eq .kc
    cmp w2, #4
    b.eq .kd
    cmp w2, #5
    b.eq .kw
    cmp w2, #6
    b.eq .ks
    b .no
.kl:
    adr_sym x2, g_nfaa64_ch
    ldrb w2, [x2, x0]
    cmp w1, w2
    b.eq .yes
    b .no
.kc:
    adr_sym x2, g_nfaa64_bmaps
    add x2, x2, x0, lsl #5
    and w3, w1, #7
    lsr w4, w1, #3
    ldrb w0, [x2, x4]
    lsr w0, w0, w3
    and w0, w0, #1
    ret
.kd:
    cmp w1, #48
    b.lo .no
    cmp w1, #57
    b.ls .yes
    b .no
.kw:
    cmp w1, #95
    b.eq .yes
    cmp w1, #48
    b.lo .no
    cmp w1, #57
    b.ls .yes
    and w1, w1, #0xFFFFFFDF
    cmp w1, #65
    b.lo .no
    cmp w1, #90
    b.ls .yes
    b .no
.ks:
    cmp w1, #32
    b.eq .yes
    cmp w1, #9
    b.eq .yes
.no:
    mov w0, #0
    ret
.yes:
    mov w0, #1
    ret
@endproc

;; @proc fsa64_eval
;; @abi arm64_ms
;; @in x0 = line start, x1 = line len
;; @out w0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc O(N) 64-bit bitset Thompson NFA evaluator.
@proc fsa64_eval
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!

    mov x19, x0 ; line
    mov x20, x1 ; len

    adr_sym x0, g_fsa64_beg
    ldr w23, [x0]
    adr_sym x0, g_nfaa64_bol
    ldr w0, [x0]
    orr w23, w23, w0
    adr_sym x0, g_fsa64_exact
    ldr w0, [x0]
    orr w23, w23, w0

    adr_sym x0, g_fsa64_end
    ldr w24, [x0]
    adr_sym x0, g_nfaa64_eol
    ldr w0, [x0]
    orr w24, w24, w0
    adr_sym x0, g_fsa64_exact
    ldr w0, [x0]
    orr w24, w24, w0

    mov x0, #1
    bl fsa64_eps
    mov x21, x0 ; active
    mov x22, #0 ; pos

.step:
    cbnz w24, .ce
    adr_sym x0, g_nfaa64_acc
    ldr x0, [x0]
    tst x21, x0
    b.ne .yes
.ce:
    cmp x22, x20
    b.hs .ateol

    mov x8, #0 ; next_states
    mov x9, #0 ; state i
    adr_sym x10, g_nfaa64_len
    ldr w10, [x10]
    adr_sym x11, g_nfaa64_star
    ldr x11, [x11]
.stlp:
    cmp w9, w10
    b.hs .stdn
    mov x0, #1
    lsl x0, x0, x9
    tst x21, x0
    b.eq .stnx
    mov x0, x9
    ldrb w1, [x19, x22]
    bl fsa64_atom
    cbz w0, .stnx
    mov x0, #1
    lsl x0, x0, x9
    tst x0, x11
    b.eq .advs
    orr x8, x8, x0
.advs:
    lsl x0, x0, #1
    orr x8, x8, x0
.stnx:
    add x9, x9, #1
    b .stlp
.stdn:
    mov x0, x8
    cbnz w23, .noseed
    orr x0, x0, #1
.noseed:
    bl fsa64_eps
    mov x21, x0
    add x22, x22, #1
    b .step

.ateol:
    adr_sym x0, g_nfaa64_acc
    ldr x0, [x0]
    tst x21, x0
    b.ne .yes
    mov w0, #0
    b .out
.yes:
    mov w0, #1
.out:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc fsa64_scan_file
;; @abi arm64_ms
;; @in x0 = file path (empty=stdin), w1 = show_pfx
;; @out w0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Scans file or stdin line-by-line.
@proc fsa64_scan_file
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #16

    mov x19, x0 ; path
    mov w20, w1 ; show_pfx
    str wzr, [sp] ; matched
    mov x21, #0 ; line_no

    adr_sym x4, g_ua64_arena
    ldr x1, [x4]
    mov_imm32 x2, 260000
    mov w3, #1
    adr_sym x4, g_fsa64_skip_bin
    ldr w4, [x4]
    cbz w4, .ld
    mov w3, #0
.ld:
    mov x0, x19
    bl ua64_load_stream
    cbnz w1, .dn
    mov x22, x0

    adr_sym x0, g_fsa64_skip_bin
    ldr w0, [x0]
    cbz w0, .st
    adr_sym x1, g_ua64_arena
    ldr x1, [x1]
    mov x2, #0
.bchk:
    cmp x2, x22
    b.hs .st
    ldrb w3, [x1, x2]
    cbz w3, .dn
    add x2, x2, #1
    b .bchk

.st:
    adr_sym x0, g_ua64_arena
    ldr x23, [x0]
    add x22, x23, x22 ; end

.ln_lp:
    cmp x23, x22
    b.hs .dn
    ldrb w0, [x23]
    cmp w0, #0x1A
    b.eq .dn
    add x21, x21, #1
    adr_sym x0, g_ua64_arena
    ldr x0, [x0]
    sub x24, x23, x0 ; byte off
    mov x25, x23
.eol:
    cmp x25, x22
    b.hs .gl
    ldrb w0, [x25]
    cmp w0, #13
    b.eq .gl
    cmp w0, #10
    b.eq .gl
    add x25, x25, #1
    b .eol
.gl:
    mov x0, x23
    sub x26, x25, x23
    mov x1, x26
    bl fsa64_eval
    adr_sym x1, g_fsa64_inv
    ldr w1, [x1]
    eor w0, w0, w1
    cbz w0, .adv

    mov w0, #1
    str w0, [sp]
    adr_sym x0, g_fsa64_name_only
    ldr w0, [x0]
    cbz w0, .full
    ldrb w0, [x19]
    cbz w0, .dn
    mov x0, x19
    bl ua64_write_str
    bl ua64_write_crlf
    b .dn

.full:
    cbz w20, .cn
    mov x0, x19
    bl ua64_write_str
    mov w0, #58
    bl ua64_write_chr
.cn:
    adr_sym x0, g_fsa64_nums
    ldr w0, [x0]
    cbz w0, .co
    mov x0, x21
    bl ua64_print_u64
    mov w0, #58
    bl ua64_write_chr
.co:
    adr_sym x0, g_fsa64_offs
    ldr w0, [x0]
    cbz w0, .bd
    mov x0, x24
    bl ua64_print_u64
    mov w0, #58
    bl ua64_write_chr
.bd:
    mov x0, x23
    mov x1, x26
    bl ua64_write_out
    bl ua64_write_crlf

.adv:
    cmp x25, x22
    b.hs .dn
    ldrb w0, [x25]
    cmp w0, #13
    b.ne .clf
    add x25, x25, #1
.clf:
    cmp x25, x22
    b.hs .nx
    ldrb w0, [x25]
    cmp w0, #10
    b.ne .nx
    add x25, x25, #1
.nx:
    mov x23, x25
    b .ln_lp

.dn:
    ldr w0, [sp]
    add sp, sp, #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc fsa64_walk_dir
;; @abi arm64_ms
;; @in x0 = dir, x1 = mask, w2 = depth
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Recursively walks directory (/S).
@proc fsa64_walk_dir
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
.flp:
    ldr w0, [sp]
    tst w0, #0x10
    b.ne .fnx
    add x0, sp, #544
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #544
    adr_sym x1, g_fsa64_show_pfx
    ldr w1, [x1]
    bl fsa64_scan_file
    cbz w0, .fnx
    adr_sym x0, g_fsa64_hit
    mov w1, #1
    str w1, [x0]
.fnx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .flp
    mov x0, x19
    call_iat __imp_FindClose

.sub:
    adr_sym x0, g_fsa64_sub
    ldr w0, [x0]
    cbz w0, .dn
    add x0, sp, #544
    add x1, sp, #1056
    adr_sym x2, szFsa64Star
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
    b.ne .go
    ldrb w0, [sp, #25]
    cbz w0, .dnx
    cmp w0, #46
    b.ne .go
    ldrb w0, [sp, #26]
    cbz w0, .dnx
.go:
    add x0, sp, #544
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #544
    mov x1, x20
    add w2, w21, #1
    bl fsa64_walk_dir
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
;; @err Exits 2 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 FINDSTR.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_fsa64_beg
    str wzr, [x0]
    adr_sym x0, g_fsa64_end
    str wzr, [x0]
    adr_sym x0, g_fsa64_lit
    str wzr, [x0]
    adr_sym x0, g_fsa64_sub
    str wzr, [x0]
    adr_sym x0, g_fsa64_ci
    str wzr, [x0]
    adr_sym x0, g_fsa64_exact
    str wzr, [x0]
    adr_sym x0, g_fsa64_inv
    str wzr, [x0]
    adr_sym x0, g_fsa64_nums
    str wzr, [x0]
    adr_sym x0, g_fsa64_name_only
    str wzr, [x0]
    adr_sym x0, g_fsa64_offs
    str wzr, [x0]
    adr_sym x0, g_fsa64_skip_bin
    str wzr, [x0]
    adr_sym x0, g_fsa64_has_pat
    str wzr, [x0]
    adr_sym x0, g_fsa64_hit
    str wzr, [x0]
    adr_sym x0, g_fsa64_show_pfx
    str wzr, [x0]

.sw_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .args_end
    cmp w1, #47
    b.ne .post
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #66 ; B
    b.eq .ob
    cmp w0, #69 ; E
    b.eq .oe
    cmp w0, #76 ; L
    b.eq .ol
    cmp w0, #82 ; R
    b.eq .or
    cmp w0, #83 ; S
    b.eq .os
    cmp w0, #73 ; I
    b.eq .oi
    cmp w0, #88 ; X
    b.eq .ox
    cmp w0, #86 ; V
    b.eq .ov
    cmp w0, #78 ; N
    b.eq .on
    cmp w0, #77 ; M
    b.eq .om
    cmp w0, #79 ; O
    b.eq .oo
    cmp w0, #80 ; P
    b.eq .op
    cmp w0, #67 ; C
    b.eq .oc
    cmp w0, #71 ; G
    b.eq .og
    b .sw_lp
.ob:
    adr_sym x0, g_fsa64_beg
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.oe:
    adr_sym x0, g_fsa64_end
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.ol:
    adr_sym x0, g_fsa64_lit
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.or:
    adr_sym x0, g_fsa64_lit
    str wzr, [x0]
    b .sw_lp
.os:
    mov w1, #1
    adr_sym x0, g_fsa64_sub
    str w1, [x0]
    adr_sym x0, g_fsa64_show_pfx
    str w1, [x0]
    b .sw_lp
.oi:
    adr_sym x0, g_fsa64_ci
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.ox:
    adr_sym x0, g_fsa64_exact
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.ov:
    adr_sym x0, g_fsa64_inv
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.on:
    adr_sym x0, g_fsa64_nums
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.om:
    adr_sym x0, g_fsa64_name_only
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.oo:
    adr_sym x0, g_fsa64_offs
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.op:
    adr_sym x0, g_fsa64_skip_bin
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.oc:
    ldrb w0, [x20, #2]
    cmp w0, #58
    b.ne .bad
    adr_sym x0, g_fsa64_pat
    add x1, x20, #3
    bl ua64_strcpy
    mov w1, #1
    adr_sym x0, g_fsa64_has_pat
    str w1, [x0]
    adr_sym x0, g_fsa64_lit
    str w1, [x0]
    b .sw_lp
.og:
    ldrb w0, [x20, #2]
    cmp w0, #58
    b.ne .bad
    add x0, x20, #3
    adr_sym x1, g_fsa64_pat
    mov x2, #512
    mov w3, #1
    bl ua64_load_stream
    cbnz w1, .bad
    adr_sym x20, g_fsa64_pat
    mov x1, #0
.tg:
    ldrb w0, [x20, x1]
    cbz w0, .gok
    cmp w0, #13
    b.eq .tmg
    cmp w0, #10
    b.eq .tmg
    add x1, x1, #1
    b .tg
.tmg:
    strb wzr, [x20, x1]
.gok:
    adr_sym x0, g_fsa64_has_pat
    mov w1, #1
    str w1, [x0]
    b .sw_lp

.post:
    adr_sym x0, g_fsa64_has_pat
    ldr w0, [x0]
    cbnz w0, .fphase
    mov x0, x19
    adr_sym x1, g_fsa64_pat
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_fsa64_has_pat
    mov w1, #1
    str w1, [x0]
    b .sw_lp

.fphase:
    adr_sym x0, g_fsa64_pat
    adr_sym x1, g_fsa64_lit
    ldr w1, [x1]
    bl fsa64_compile_nfa

    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    bl ua64_skip_ws
    cbz w1, .flp
    adr_sym x0, g_fsa64_show_pfx
    mov w1, #1
    str w1, [x0]

.flp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .exr
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_fsa64_sub
    ldr w0, [x0]
    cbnz w0, .subm
    adr_sym x0, g_ua64_arg_b
    adr_sym x1, g_fsa64_show_pfx
    ldr w1, [x1]
    bl fsa64_scan_file
    cbz w0, .flp
    adr_sym x0, g_fsa64_hit
    mov w1, #1
    str w1, [x0]
    b .flp
.subm:
    adr_sym x0, g_ua64_arg_c
    mov x1, #1000
    bl ua64_get_cwd
    adr_sym x0, g_ua64_arg_c
    adr_sym x1, g_ua64_arg_b
    mov w2, #0
    bl fsa64_walk_dir
    b .flp

.args_end:
    adr_sym x0, g_fsa64_has_pat
    ldr w0, [x0]
    cbz w0, .bad
    adr_sym x0, g_fsa64_pat
    adr_sym x1, g_fsa64_lit
    ldr w1, [x1]
    bl fsa64_compile_nfa
    adr_sym x0, g_ua64_arg_b
    strb wzr, [x0]
    mov w1, #0
    bl fsa64_scan_file
    adr_sym x1, g_fsa64_hit
    str w0, [x1]

.exr:
    adr_sym x0, g_fsa64_hit
    ldr w0, [x0]
    cbnz w0, .ex0
    mov w0, #1
    bl ua64_exit
    b .out
.ex0:
    mov w0, #0
    bl ua64_exit
    b .out
.help:
    adr_sym x0, szFsa64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szFsa64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #2
    bl ua64_exit
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szFsa64Star, "*", 0
@data db szFsa64Syn, "FINDSTR: Bad command line", 0
@data db szFsa64Help, "Searches for strings in files using literal or 64-bit Thompson NFA regular expressions.", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res qword g_nfaa64_star, 1
@res qword g_nfaa64_acc, 1
@res dword g_nfaa64_len, 1
@res dword g_nfaa64_bol, 1
@res dword g_nfaa64_eol, 1
@res dword g_fsa64_beg, 1
@res dword g_fsa64_end, 1
@res dword g_fsa64_lit, 1
@res dword g_fsa64_sub, 1
@res dword g_fsa64_ci, 1
@res dword g_fsa64_exact, 1
@res dword g_fsa64_inv, 1
@res dword g_fsa64_nums, 1
@res dword g_fsa64_name_only, 1
@res dword g_fsa64_offs, 1
@res dword g_fsa64_skip_bin, 1
@res dword g_fsa64_has_pat, 1
@res dword g_fsa64_hit, 1
@res dword g_fsa64_show_pfx, 1
@res byte g_nfaa64_kind, 64
@res byte g_nfaa64_ch, 64
@res byte g_nfaa64_bmaps, 2048
@res byte g_fsa64_pat, 1024
