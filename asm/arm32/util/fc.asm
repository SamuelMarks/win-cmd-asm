;; @file fc.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 FC.EXE file comparison utility (/A /B /C /L /LBn /N /OFF /T /U /W file1 file2).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc fca32_lines_eq
;; @abi arm_nt
;; @in r0 = line A, r1 = len A, r2 = line B, r3 = len B
;; @out r0 = 1 if equal, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Compares two text lines honoring /C and /W.
@proc fca32_lines_eq
    push {r4, r5, r6, r7}
    add r4, r0, r1 ; end A
    add r5, r2, r3 ; end B
    adr_sym r6, g_fca32_ws
    ldr r6, [r6]
    adr_sym r7, g_fca32_ci
    ldr r7, [r7]

    cmp r6, #0
    beq .lp
.tla:
    cmp r0, r4
    bhs .tlb
    ldrb r1, [r0]
    cmp r1, #32
    beq .ala
    cmp r1, #9
    bne .tlb
.ala:
    add r0, r0, #1
    b .tla
.tlb:
    cmp r2, r5
    bhs .lp
    ldrb r1, [r2]
    cmp r1, #32
    beq .alb
    cmp r1, #9
    bne .lp
.alb:
    add r2, r2, #1
    b .tlb

.lp:
    cmp r0, r4
    bhs .adn
    cmp r2, r5
    bhs .bdn

    cmp r6, #0
    beq .cc
    ldrb r1, [r0]
    cmp r1, #32
    beq .wsa
    cmp r1, #9
    bne .cc
.wsa:
    ldrb r3, [r2]
    cmp r3, #32
    beq .skw
    cmp r3, #9
    bne .no
.skw:
    add r0, r0, #1
    cmp r0, r4
    bhs .skwb
    ldrb r1, [r0]
    cmp r1, #32
    beq .skw
    cmp r1, #9
    beq .skw
.skwb:
    add r2, r2, #1
    cmp r2, r5
    bhs .lp
    ldrb r3, [r2]
    cmp r3, #32
    beq .skwb
    cmp r3, #9
    beq .skwb
    b .lp

.cc:
    ldrb r1, [r0]
    ldrb r3, [r2]
    cmp r7, #0
    beq .raw
    cmp r1, #97
    blo .u1
    cmp r1, #122
    bhi .u1
    sub r1, r1, #32
.u1:
    cmp r3, #97
    blo .u2
    cmp r3, #122
    bhi .u2
    sub r3, r3, #32
.u2:
.raw:
    cmp r1, r3
    bne .no
    add r0, r0, #1
    add r2, r2, #1
    b .lp

.adn:
    cmp r6, #0
    beq .cbe
.trb:
    cmp r2, r5
    bhs .yes
    ldrb r3, [r2]
    cmp r3, #32
    beq .atb
    cmp r3, #9
    bne .no
.atb:
    add r2, r2, #1
    b .trb
.cbe:
    cmp r2, r5
    beq .yes
    b .no

.bdn:
    cmp r6, #0
    beq .no
.tra:
    cmp r0, r4
    bhs .yes
    ldrb r1, [r0]
    cmp r1, #32
    beq .ata
    cmp r1, #9
    bne .no
.ata:
    add r0, r0, #1
    b .tra

.yes:
    mov r0, #1
    pop {r4, r5, r6, r7}
    bx lr
.no:
    mov r0, #0
    pop {r4, r5, r6, r7}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=diffs, 2=error)
;; @err Exits 2 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 FC.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #16
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_fca32_bin
    str r1, [r0]
    adr_sym r0, g_fca32_ci
    str r1, [r0]
    adr_sym r0, g_fca32_nums
    str r1, [r0]
    adr_sym r0, g_fca32_ws
    str r1, [r0]
    adr_sym r0, g_ua32_arg_b
    strb r1, [r0]
    adr_sym r0, g_ua32_arg_c
    strb r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .run
    cmp r1, #47
    bne .pos
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #66 ; B
    beq .sb
    cmp r0, #67 ; C
    beq .sc
    cmp r0, #78 ; N
    beq .sn
    cmp r0, #87 ; W
    beq .sw
    b .arg_lp
.sb:
    adr_sym r0, g_fca32_bin
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sc:
    adr_sym r0, g_fca32_ci
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sn:
    adr_sym r0, g_fca32_nums
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sw:
    adr_sym r0, g_fca32_ws
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.pos:
    adr_sym r0, g_ua32_arg_b
    ldrb r0, [r0]
    cmp r0, #0
    bne .pos2
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.pos2:
    mov r0, r4
    adr_sym r1, g_ua32_arg_c
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp

.help:
    adr_sym r0, szFca32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.run:
    adr_sym r0, g_ua32_arg_b
    ldrb r0, [r0]
    cmp r0, #0
    beq .bad
    adr_sym r0, g_ua32_arg_c
    ldrb r0, [r0]
    cmp r0, #0
    beq .bad

    mov r3, #1
    adr_sym r0, g_fca32_bin
    ldr r0, [r0]
    cmp r0, #0
    beq .lda
    mov r3, #0
.lda:
    adr_sym r0, g_ua32_arg_b
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 220000
    bl ua32_load_stream
    cmp r1, #0
    bne .nf
    mov r5, r0 ; len A

    mov r3, #1
    adr_sym r0, g_fca32_bin
    ldr r0, [r0]
    cmp r0, #0
    beq .ldb
    mov r3, #0
.ldb:
    adr_sym r0, g_ua32_arg_c
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r12, 262144
    add r1, r1, r12
    mov_imm32 r2, 220000
    bl ua32_load_stream
    cmp r1, #0
    bne .nf
    mov r6, r0 ; len B

    adr_sym r0, szFca32Ban1
    bl ua32_write_str
    adr_sym r0, g_ua32_arg_b
    bl ua32_write_str
    adr_sym r0, szFca32Ban2
    bl ua32_write_str
    adr_sym r0, g_ua32_arg_c
    bl ua32_write_str
    bl ua32_write_crlf

    adr_sym r0, g_fca32_diffs
    mov r1, #0
    str r1, [r0]

    adr_sym r0, g_fca32_bin
    ldr r0, [r0]
    cmp r0, #0
    beq .txt

    adr_sym r0, g_ua32_arena
    ldr r7, [r0]
    mov_imm32 r12, 262144
    add r8, r7, r12
    mov r9, r5
    cmp r9, r6
    bls .gmin
    mov r9, r6
.gmin:
    mov r10, #0
.blp:
    cmp r10, r9
    bhs .blen
    ldrb r0, [r7, r10]
    ldrb r1, [r8, r10]
    cmp r0, r1
    beq .bnx
    adr_sym r0, g_fca32_diffs
    mov r1, #1
    str r1, [r0]
    mov r0, r10
    bl ua32_print_hex32
    adr_sym r0, szFca32Col
    bl ua32_write_str
    ldrb r0, [r7, r10]
    bl ua32_print_hex8
    mov r0, #32
    bl ua32_write_chr
    ldrb r0, [r8, r10]
    bl ua32_print_hex8
    bl ua32_write_crlf
.bnx:
    add r10, r10, #1
    b .blp
.blen:
    cmp r5, r6
    beq .chkd
    adr_sym r0, g_fca32_diffs
    mov r1, #1
    str r1, [r0]
    adr_sym r0, szFca32Long
    bl ua32_write_str
    bl ua32_write_crlf
    b .chkd

.txt:
    adr_sym r0, g_ua32_arena
    ldr r7, [r0]
    add r5, r7, r5 ; end A
    mov_imm32 r12, 262144
    add r8, r7, r12
    add r6, r8, r6 ; end B
    mov r0, #0
    str r0, [sp] ; line_no

.tlp:
    cmp r7, r5
    bhs .aeof
    cmp r8, r6
    bhs .dtail
    ldr r0, [sp]
    add r0, r0, #1
    str r0, [sp]

    mov r9, r7
.ea:
    cmp r9, r5
    bhs .ga
    ldrb r0, [r9]
    cmp r0, #13
    beq .ga
    cmp r0, #10
    beq .ga
    add r9, r9, #1
    b .ea
.ga:
    mov r10, r8
.eb:
    cmp r10, r6
    bhs .gb
    ldrb r0, [r10]
    cmp r0, #13
    beq .gb
    cmp r0, #10
    beq .gb
    add r10, r10, #1
    b .eb
.gb:
    mov r0, r7
    sub r1, r9, r7
    mov r2, r8
    sub r3, r10, r8
    str r1, [sp, #4]
    str r3, [sp, #8]
    bl fca32_lines_eq
    cmp r0, #0
    bne .adv

    adr_sym r0, g_fca32_diffs
    mov r1, #1
    str r1, [r0]
    adr_sym r0, szFca32Stars
    bl ua32_write_str
    adr_sym r0, g_ua32_arg_b
    bl ua32_write_str
    bl ua32_write_crlf
    adr_sym r0, g_fca32_nums
    ldr r0, [r0]
    cmp r0, #0
    beq .pla
    ldr r0, [sp]
    bl ua32_print_u32
    adr_sym r0, szFca32Col
    bl ua32_write_str
.pla:
    mov r0, r7
    ldr r1, [sp, #4]
    bl ua32_write_out
    bl ua32_write_crlf

    adr_sym r0, szFca32Stars
    bl ua32_write_str
    adr_sym r0, g_ua32_arg_c
    bl ua32_write_str
    bl ua32_write_crlf
    adr_sym r0, g_fca32_nums
    ldr r0, [r0]
    cmp r0, #0
    beq .plb
    ldr r0, [sp]
    bl ua32_print_u32
    adr_sym r0, szFca32Col
    bl ua32_write_str
.plb:
    mov r0, r8
    ldr r1, [sp, #8]
    bl ua32_write_out
    bl ua32_write_crlf
    adr_sym r0, szFca32End
    bl ua32_write_str
    bl ua32_write_crlf

.adv:
    cmp r9, r5
    bhs .sa
    ldrb r0, [r9]
    cmp r0, #13
    bne .calf
    add r9, r9, #1
.calf:
    cmp r9, r5
    bhs .sa
    ldrb r0, [r9]
    cmp r0, #10
    bne .sa
    add r9, r9, #1
.sa:
    mov r7, r9

    cmp r10, r6
    bhs .sb2
    ldrb r0, [r10]
    cmp r0, #13
    bne .cblf
    add r10, r10, #1
.cblf:
    cmp r10, r6
    bhs .sb2
    ldrb r0, [r10]
    cmp r0, #10
    bne .sb2
    add r10, r10, #1
.sb2:
    mov r8, r10
    b .tlp

.aeof:
    cmp r8, r6
    beq .chkd
.dtail:
    adr_sym r0, g_fca32_diffs
    mov r1, #1
    str r1, [r0]
    adr_sym r0, szFca32Long
    bl ua32_write_str
    bl ua32_write_crlf

.chkd:
    adr_sym r0, g_fca32_diffs
    ldr r0, [r0]
    cmp r0, #0
    bne .ex1
    adr_sym r0, szFca32Ok
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
.ex1:
    mov r0, #1
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
.nf:
    adr_sym r0, szFca32Nf
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #2
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
.bad:
    adr_sym r0, szFca32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #2
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

@section rdata

@data db szFca32Ban1, "Comparing files ", 0
@data db szFca32Ban2, " and ", 0
@data db szFca32Ok, "FC: no differences encountered", 0
@data db szFca32Stars, "***** ", 0
@data db szFca32End, "*****", 0
@data db szFca32Col, ": ", 0
@data db szFca32Long, "FC: files are different sizes", 0
@data db szFca32Nf, "FC: cannot open file", 0
@data db szFca32Syn, "FC: Insufficient number of file specifications", 0
@data db szFca32Help, "Compares two files or sets of files and displays the differences (/B /C /L /N /U /W).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_fca32_bin, 1
@res dword g_fca32_ci, 1
@res dword g_fca32_nums, 1
@res dword g_fca32_ws, 1
@res dword g_fca32_diffs, 1
