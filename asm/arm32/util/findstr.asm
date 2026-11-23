;; @file findstr.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 FINDSTR.EXE with Thompson NFA bitset regex engine (/B /E /L /R /S /I /X /V /N /M /O /P /C).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc fsa32_compile_nfa
;; @abi arm_nt
;; @in r0 = pattern string, r1 = literal_only (1=/L, 0=/R)
;; @out Populates 30-state Thompson NFA tables
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Compiles pattern into Thompson NFA state slots.
@proc fsa32_compile_nfa
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; pat
    mov r5, r1 ; lit_only
    mov r6, #0 ; state idx
    mov r7, #0 ; star_mask

    mov r0, #0
    adr_sym r1, g_nfaa32_bol
    str r0, [r1]
    adr_sym r1, g_nfaa32_eol
    str r0, [r1]

    cmp r5, #0
    bne .clp
    ldrb r0, [r4]
    cmp r0, #94 ; ^
    bne .clp
    mov r0, #1
    adr_sym r1, g_nfaa32_bol
    str r0, [r1]
    add r4, r4, #1

.clp:
    ldrb r0, [r4]
    cmp r0, #0
    beq .cdn
    cmp r6, #30
    bhs .cdn

    cmp r5, #0
    bne .lit

    cmp r0, #36 ; $
    bne .neol
    ldrb r1, [r4, #1]
    cmp r1, #0
    bne .neol
    mov r1, #1
    adr_sym r2, g_nfaa32_eol
    str r1, [r2]
    add r4, r4, #1
    b .cdn

.neol:
    cmp r0, #46 ; .
    bne .ndot
    adr_sym r1, g_nfaa32_kind
    mov r2, #2
    strb r2, [r1, r6]
    add r4, r4, #1
    b .cstar

.ndot:
    cmp r0, #0x5C
    bne .nesc
    ldrb r1, [r4, #1]
    cmp r1, #0
    beq .lit
    add r4, r4, #1
    ldrb r0, [r4]
    adr_sym r1, g_nfaa32_kind
    cmp r0, #100 ; d
    beq .ed
    cmp r0, #119 ; w
    beq .ew
    cmp r0, #115 ; s
    beq .es
    b .lit
.ed:
    mov r2, #4
    strb r2, [r1, r6]
    add r4, r4, #1
    b .cstar
.ew:
    mov r2, #5
    strb r2, [r1, r6]
    add r4, r4, #1
    b .cstar
.es:
    mov r2, #6
    strb r2, [r1, r6]
    add r4, r4, #1
    b .cstar

.nesc:
    cmp r0, #91 ; [
    bne .lit
    add r4, r4, #1
    adr_sym r8, g_nfaa32_bmaps
    add r8, r8, r6, lsl #5
    mov r1, #0
    mov r2, #0
.zr:
    cmp r1, #32
    bhs .zrd
    strb r2, [r8, r1]
    add r1, r1, #1
    b .zr
.zrd:
    mov r9, #0 ; inv
    ldrb r0, [r4]
    cmp r0, #94 ; ^
    bne .clslp
    mov r9, #1
    add r4, r4, #1
.clslp:
    ldrb r0, [r4]
    cmp r0, #0
    beq .clsend
    cmp r0, #93 ; ]
    bne .clsit
    add r4, r4, #1
    b .clsend
.clsit:
    add r4, r4, #1
    ldrb r1, [r4]
    cmp r1, #45 ; -
    bne .cls1
    ldrb r1, [r4, #1]
    cmp r1, #0
    beq .cls1
    cmp r1, #93
    beq .cls1
    add r4, r4, #2
.rng:
    cmp r0, r1
    bhi .clslp
    and r2, r0, #7
    lsr r3, r0, #3
    mov r10, #1
    lsl r10, r10, r2
    ldrb r12, [r8, r3]
    orr r12, r12, r10
    strb r12, [r8, r3]
    add r0, r0, #1
    b .rng
.cls1:
    and r2, r0, #7
    lsr r3, r0, #3
    mov r10, #1
    lsl r10, r10, r2
    ldrb r12, [r8, r3]
    orr r12, r12, r10
    strb r12, [r8, r3]
    b .clslp
.clsend:
    cmp r9, #0
    beq .clsst
    mov r1, #0
.inv:
    cmp r1, #32
    bhs .clsst
    ldrb r2, [r8, r1]
    mvn r2, r2
    strb r2, [r8, r1]
    add r1, r1, #1
    b .inv
.clsst:
    adr_sym r1, g_nfaa32_kind
    mov r2, #3
    strb r2, [r1, r6]
    b .cstar

.lit:
    adr_sym r1, g_fsa32_ci
    ldr r1, [r1]
    cmp r1, #0
    beq .stlit
    bl ua32_chr_upper
.stlit:
    adr_sym r1, g_nfaa32_kind
    mov r2, #1
    strb r2, [r1, r6]
    adr_sym r1, g_nfaa32_ch
    strb r0, [r1, r6]
    add r4, r4, #1

.cstar:
    cmp r5, #0
    bne .nst
    ldrb r0, [r4]
    cmp r0, #42 ; *
    bne .nst
    add r4, r4, #1
    mov r0, #1
    lsl r0, r0, r6
    orr r7, r7, r0
.nst:
    add r6, r6, #1
    b .clp

.cdn:
    adr_sym r0, g_nfaa32_len
    str r6, [r0]
    adr_sym r0, g_nfaa32_star
    str r7, [r0]
    mov r0, #1
    lsl r0, r0, r6
    adr_sym r1, g_nfaa32_acc
    str r0, [r1]
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc fsa32_eps
;; @abi arm_nt
;; @in r0 = 32-bit state bitset
;; @out r0 = epsilon-closed state bitset
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Propagates Kleene-star epsilon transitions.
@proc fsa32_eps
    adr_sym r1, g_nfaa32_star
    ldr r1, [r1]
    cmp r1, #0
    beq .dn
    mov r2, #30
.lp:
    and r3, r0, r1
    lsl r3, r3, #1
    orr r0, r0, r3
    subs r2, r2, #1
    bne .lp
.dn:
    bx lr
@endproc

;; @proc fsa32_atom
;; @abi arm_nt
;; @in r0 = state idx, r1 = input byte
;; @out r0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Evaluates one NFA state atom against r1.
@proc fsa32_atom
    adr_sym r2, g_fsa32_ci
    ldr r2, [r2]
    cmp r2, #0
    beq .gc
    cmp r1, #97
    blo .gc
    cmp r1, #122
    bhi .gc
    sub r1, r1, #32
.gc:
    adr_sym r2, g_nfaa32_kind
    ldrb r2, [r2, r0]
    cmp r2, #1
    beq .kl
    cmp r2, #2
    beq .yes
    cmp r2, #3
    beq .kc
    cmp r2, #4
    beq .kd
    cmp r2, #5
    beq .kw
    cmp r2, #6
    beq .ks
    b .no
.kl:
    adr_sym r2, g_nfaa32_ch
    ldrb r2, [r2, r0]
    cmp r1, r2
    beq .yes
    b .no
.kc:
    adr_sym r2, g_nfaa32_bmaps
    add r2, r2, r0, lsl #5
    and r3, r1, #7
    lsr r12, r1, #3
    ldrb r0, [r2, r12]
    lsr r0, r0, r3
    and r0, r0, #1
    bx lr
.kd:
    cmp r1, #48
    blo .no
    cmp r1, #57
    bls .yes
    b .no
.kw:
    cmp r1, #95
    beq .yes
    cmp r1, #48
    blo .no
    cmp r1, #57
    bls .yes
    bic r1, r1, #0x20
    cmp r1, #65
    blo .no
    cmp r1, #90
    bls .yes
    b .no
.ks:
    cmp r1, #32
    beq .yes
    cmp r1, #9
    beq .yes
.no:
    mov r0, #0
    bx lr
.yes:
    mov r0, #1
    bx lr
@endproc

;; @proc fsa32_eval
;; @abi arm_nt
;; @in r0 = line start, r1 = line len
;; @out r0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc O(N) Thompson NFA evaluation over a line.
@proc fsa32_eval
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; line
    mov r5, r1 ; len

    adr_sym r0, g_fsa32_beg
    ldr r8, [r0]
    adr_sym r0, g_nfaa32_bol
    ldr r0, [r0]
    orr r8, r8, r0
    adr_sym r0, g_fsa32_exact
    ldr r0, [r0]
    orr r8, r8, r0

    adr_sym r0, g_fsa32_end
    ldr r9, [r0]
    adr_sym r0, g_nfaa32_eol
    ldr r0, [r0]
    orr r9, r9, r0
    adr_sym r0, g_fsa32_exact
    ldr r0, [r0]
    orr r9, r9, r0

    mov r0, #1
    bl fsa32_eps
    mov r6, r0 ; active
    mov r7, #0 ; pos

.step:
    cmp r9, #0
    bne .ce
    adr_sym r0, g_nfaa32_acc
    ldr r0, [r0]
    tst r6, r0
    bne .yes
.ce:
    cmp r7, r5
    bhs .ateol

    mov r10, #0 ; next_states
    mov r0, #0 ; i
    push {r8, r9}
    mov r8, #0 ; i
    adr_sym r1, g_nfaa32_len
    ldr r9, [r1]
.stlp:
    cmp r8, r9
    bhs .stdn
    mov r0, #1
    lsl r0, r0, r8
    tst r6, r0
    beq .stnx
    mov r0, r8
    ldrb r1, [r4, r7]
    bl fsa32_atom
    cmp r0, #0
    beq .stnx
    mov r0, #1
    lsl r0, r0, r8
    adr_sym r1, g_nfaa32_star
    ldr r1, [r1]
    tst r0, r1
    beq .advs
    orr r10, r10, r0
.advs:
    lsl r0, r0, #1
    orr r10, r10, r0
.stnx:
    add r8, r8, #1
    b .stlp
.stdn:
    pop {r8, r9}
    mov r0, r10
    cmp r8, #0
    bne .noseed
    orr r0, r0, #1
.noseed:
    bl fsa32_eps
    mov r6, r0
    add r7, r7, #1
    b .step

.ateol:
    adr_sym r0, g_nfaa32_acc
    ldr r0, [r0]
    tst r6, r0
    bne .yes
    mov r0, #0
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
.yes:
    mov r0, #1
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc fsa32_scan_file
;; @abi arm_nt
;; @in r0 = file path (empty=stdin), r1 = show_pfx
;; @out r0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Scans file or stdin line-by-line.
@proc fsa32_scan_file
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #8 ; [sp]=matched
    mov r4, r0 ; path
    mov r5, r1 ; show_pfx
    mov r0, #0
    str r0, [sp]
    mov r9, #0 ; line_no

    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 260000
    mov r3, #1
    adr_sym r0, g_fsa32_skip_bin
    ldr r0, [r0]
    cmp r0, #0
    beq .ld
    mov r3, #0
.ld:
    mov r0, r4
    bl ua32_load_stream
    cmp r1, #0
    bne .dn
    mov r6, r0

    adr_sym r0, g_fsa32_skip_bin
    ldr r0, [r0]
    cmp r0, #0
    beq .st
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov r2, #0
.bchk:
    cmp r2, r6
    bhs .st
    ldrb r3, [r1, r2]
    cmp r3, #0
    beq .dn
    add r2, r2, #1
    b .bchk

.st:
    adr_sym r0, g_ua32_arena
    ldr r7, [r0]
    add r6, r7, r6 ; end

.ln_lp:
    cmp r7, r6
    bhs .dn
    ldrb r0, [r7]
    cmp r0, #0x1A
    beq .dn
    add r9, r9, #1
    adr_sym r0, g_ua32_arena
    ldr r0, [r0]
    sub r8, r7, r0 ; byte offset
    mov r10, r7
.eol:
    cmp r10, r6
    bhs .gl
    ldrb r0, [r10]
    cmp r0, #13
    beq .gl
    cmp r0, #10
    beq .gl
    add r10, r10, #1
    b .eol
.gl:
    mov r0, r7
    sub r1, r10, r7
    bl fsa32_eval
    adr_sym r1, g_fsa32_inv
    ldr r1, [r1]
    eor r0, r0, r1
    cmp r0, #0
    beq .adv

    mov r0, #1
    str r0, [sp]
    adr_sym r0, g_fsa32_name_only
    ldr r0, [r0]
    cmp r0, #0
    beq .full
    ldrb r0, [r4]
    cmp r0, #0
    beq .dn
    mov r0, r4
    bl ua32_write_str
    bl ua32_write_crlf
    b .dn

.full:
    cmp r5, #0
    beq .cn
    mov r0, r4
    bl ua32_write_str
    mov r0, #58
    bl ua32_write_chr
.cn:
    adr_sym r0, g_fsa32_nums
    ldr r0, [r0]
    cmp r0, #0
    beq .co
    mov r0, r9
    bl ua32_print_u32
    mov r0, #58
    bl ua32_write_chr
.co:
    adr_sym r0, g_fsa32_offs
    ldr r0, [r0]
    cmp r0, #0
    beq .bd
    mov r0, r8
    bl ua32_print_u32
    mov r0, #58
    bl ua32_write_chr
.bd:
    mov r0, r7
    sub r1, r10, r7
    bl ua32_write_out
    bl ua32_write_crlf

.adv:
    cmp r10, r6
    bhs .dn
    ldrb r0, [r10]
    cmp r0, #13
    bne .clf
    add r10, r10, #1
.clf:
    cmp r10, r6
    bhs .nx
    ldrb r0, [r10]
    cmp r0, #10
    bne .nx
    add r10, r10, #1
.nx:
    mov r7, r10
    b .ln_lp

.dn:
    ldr r0, [sp]
    add sp, sp, #8
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc fsa32_walk_dir
;; @abi arm_nt
;; @in r0 = dir, r1 = mask, r2 = depth
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Recursively walks directory (/S).
@proc fsa32_walk_dir
    push {r4, r5, r6, lr}
    sub sp, sp, #2048
    cmp r2, #16
    bhs .dn
    mov r5, r1
    mov r6, r2
    mov r1, r0
    add r0, sp, #1056
    bl ua32_strcpy

    add r0, sp, #544
    add r1, sp, #1056
    mov r2, r5
    bl ua32_path_join
    add r0, sp, #544
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .sub
    mov r4, r0
.flp:
    ldr r0, [sp]
    tst r0, #0x10
    bne .fnx
    add r0, sp, #544
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #544
    adr_sym r1, g_fsa32_show_pfx
    ldr r1, [r1]
    bl fsa32_scan_file
    cmp r0, #0
    beq .fnx
    adr_sym r0, g_fsa32_hit
    mov r1, #1
    str r1, [r0]
.fnx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .flp
    mov r0, r4
    call_iat __imp_FindClose

.sub:
    adr_sym r0, g_fsa32_sub
    ldr r0, [r0]
    cmp r0, #0
    beq .dn
    add r0, sp, #544
    add r1, sp, #1056
    adr_sym r2, szFsa32Star
    bl ua32_path_join
    add r0, sp, #544
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .dn
    mov r4, r0
.dlp:
    ldr r0, [sp]
    tst r0, #0x10
    beq .dnx
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .go
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .dnx
    cmp r0, #46
    bne .go
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .dnx
.go:
    add r0, sp, #544
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #544
    mov r1, r5
    add r2, r6, #1
    bl fsa32_walk_dir
.dnx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .dlp
    mov r0, r4
    call_iat __imp_FindClose
.dn:
    add sp, sp, #2048
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 2 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 FINDSTR.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_fsa32_beg
    str r1, [r0]
    adr_sym r0, g_fsa32_end
    str r1, [r0]
    adr_sym r0, g_fsa32_lit
    str r1, [r0]
    adr_sym r0, g_fsa32_sub
    str r1, [r0]
    adr_sym r0, g_fsa32_ci
    str r1, [r0]
    adr_sym r0, g_fsa32_exact
    str r1, [r0]
    adr_sym r0, g_fsa32_inv
    str r1, [r0]
    adr_sym r0, g_fsa32_nums
    str r1, [r0]
    adr_sym r0, g_fsa32_name_only
    str r1, [r0]
    adr_sym r0, g_fsa32_offs
    str r1, [r0]
    adr_sym r0, g_fsa32_skip_bin
    str r1, [r0]
    adr_sym r0, g_fsa32_has_pat
    str r1, [r0]
    adr_sym r0, g_fsa32_hit
    str r1, [r0]
    adr_sym r0, g_fsa32_show_pfx
    str r1, [r0]

.sw_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .args_end
    cmp r1, #47
    bne .pos_t
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #66 ; B
    beq .ob
    cmp r0, #69 ; E
    beq .oe
    cmp r0, #76 ; L
    beq .ol
    cmp r0, #82 ; R
    beq .or
    cmp r0, #83 ; S
    beq .os
    cmp r0, #73 ; I
    beq .oi
    cmp r0, #88 ; X
    beq .ox
    cmp r0, #86 ; V
    beq .ov
    cmp r0, #78 ; N
    beq .on
    cmp r0, #77 ; M
    beq .om
    cmp r0, #79 ; O
    beq .oo
    cmp r0, #80 ; P
    beq .op
    cmp r0, #67 ; C
    beq .oc
    cmp r0, #71 ; G
    beq .og
    b .sw_lp
.ob:
    adr_sym r0, g_fsa32_beg
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.oe:
    adr_sym r0, g_fsa32_end
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.ol:
    adr_sym r0, g_fsa32_lit
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.or:
    adr_sym r0, g_fsa32_lit
    mov r1, #0
    str r1, [r0]
    b .sw_lp
.os:
    mov r1, #1
    adr_sym r0, g_fsa32_sub
    str r1, [r0]
    adr_sym r0, g_fsa32_show_pfx
    str r1, [r0]
    b .sw_lp
.oi:
    adr_sym r0, g_fsa32_ci
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.ox:
    adr_sym r0, g_fsa32_exact
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.ov:
    adr_sym r0, g_fsa32_inv
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.on:
    adr_sym r0, g_fsa32_nums
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.om:
    adr_sym r0, g_fsa32_name_only
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.oo:
    adr_sym r0, g_fsa32_offs
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.op:
    adr_sym r0, g_fsa32_skip_bin
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.oc:
    ldrb r0, [r5, #2]
    cmp r0, #58
    bne .bad
    adr_sym r0, g_fsa32_pat
    add r1, r5, #3
    bl ua32_strcpy
    mov r1, #1
    adr_sym r0, g_fsa32_has_pat
    str r1, [r0]
    adr_sym r0, g_fsa32_lit
    str r1, [r0]
    b .sw_lp
.og:
    ldrb r0, [r5, #2]
    cmp r0, #58
    bne .bad
    add r0, r5, #3
    adr_sym r1, g_fsa32_pat
    mov r2, #512
    mov r3, #1
    bl ua32_load_stream
    cmp r1, #0
    bne .bad
    adr_sym r5, g_fsa32_pat
    mov r1, #0
.tg:
    ldrb r0, [r5, r1]
    cmp r0, #0
    beq .gok
    cmp r0, #13
    beq .tmg
    cmp r0, #10
    beq .tmg
    add r1, r1, #1
    b .tg
.tmg:
    mov r0, #0
    strb r0, [r5, r1]
.gok:
    adr_sym r0, g_fsa32_has_pat
    mov r1, #1
    str r1, [r0]
    b .sw_lp

.pos_t:
    adr_sym r0, g_fsa32_has_pat
    ldr r0, [r0]
    cmp r0, #0
    bne .fphase
    mov r0, r4
    adr_sym r1, g_fsa32_pat
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_fsa32_has_pat
    mov r1, #1
    str r1, [r0]
    b .sw_lp

.fphase:
    adr_sym r0, g_fsa32_pat
    adr_sym r1, g_fsa32_lit
    ldr r1, [r1]
    bl fsa32_compile_nfa

    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    bl ua32_skip_ws
    cmp r1, #0
    beq .flp
    adr_sym r0, g_fsa32_show_pfx
    mov r1, #1
    str r1, [r0]

.flp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .exr
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_fsa32_sub
    ldr r0, [r0]
    cmp r0, #0
    bne .subm
    adr_sym r0, g_ua32_arg_b
    adr_sym r1, g_fsa32_show_pfx
    ldr r1, [r1]
    bl fsa32_scan_file
    cmp r0, #0
    beq .flp
    adr_sym r0, g_fsa32_hit
    mov r1, #1
    str r1, [r0]
    b .flp
.subm:
    adr_sym r0, g_ua32_arg_c
    mov r1, #1000
    bl ua32_get_cwd
    adr_sym r0, g_ua32_arg_c
    adr_sym r1, g_ua32_arg_b
    mov r2, #0
    bl fsa32_walk_dir
    b .flp

.args_end:
    adr_sym r0, g_fsa32_has_pat
    ldr r0, [r0]
    cmp r0, #0
    beq .bad
    adr_sym r0, g_fsa32_pat
    adr_sym r1, g_fsa32_lit
    ldr r1, [r1]
    bl fsa32_compile_nfa
    adr_sym r0, g_ua32_arg_b
    mov r1, #0
    strb r1, [r0]
    bl fsa32_scan_file
    adr_sym r1, g_fsa32_hit
    str r0, [r1]

.exr:
    adr_sym r0, g_fsa32_hit
    ldr r0, [r0]
    cmp r0, #0
    bne .ex0
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.ex0:
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.help:
    adr_sym r0, szFsa32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.bad:
    adr_sym r0, szFsa32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #2
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szFsa32Star, "*", 0
@data db szFsa32Syn, "FINDSTR: Bad command line", 0
@data db szFsa32Help, "Searches for strings in files using literal or Thompson NFA regular expressions.", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_nfaa32_len, 1
@res dword g_nfaa32_bol, 1
@res dword g_nfaa32_eol, 1
@res dword g_nfaa32_star, 1
@res dword g_nfaa32_acc, 1
@res dword g_fsa32_beg, 1
@res dword g_fsa32_end, 1
@res dword g_fsa32_lit, 1
@res dword g_fsa32_sub, 1
@res dword g_fsa32_ci, 1
@res dword g_fsa32_exact, 1
@res dword g_fsa32_inv, 1
@res dword g_fsa32_nums, 1
@res dword g_fsa32_name_only, 1
@res dword g_fsa32_offs, 1
@res dword g_fsa32_skip_bin, 1
@res dword g_fsa32_has_pat, 1
@res dword g_fsa32_hit, 1
@res dword g_fsa32_show_pfx, 1
@res byte g_nfaa32_kind, 32
@res byte g_nfaa32_ch, 32
@res byte g_nfaa32_bmaps, 1024
@res byte g_fsa32_pat, 1024
