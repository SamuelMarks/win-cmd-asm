;; @file find.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 FIND.EXE utility (/V, /C, /N, /I, /OFF[LINE] "string" [files...]).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc finda32_line_has
;; @abi arm_nt
;; @in r0 = line start, r1 = line len, r2 = needle, r3 = needle len
;; @out r0 = 1 if matched, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Substring search honoring g_finda32_ci (/I).
@proc finda32_line_has
    push {r4, r5, r6, r7}
    cmp r3, #0
    beq .no
    cmp r1, r3
    blo .no
    sub r1, r1, r3
    add r1, r1, #1
    adr_sym r4, g_finda32_ci
    ldr r4, [r4]
.pos:
    mov r5, #0
.ch:
    cmp r5, r3
    bhs .yes
    ldrb r6, [r0, r5]
    ldrb r7, [r2, r5]
    cmp r4, #0
    beq .raw
    cmp r6, #97
    blo .u1
    cmp r6, #122
    bhi .u1
    sub r6, r6, #32
.u1:
    cmp r7, #97
    blo .u2
    cmp r7, #122
    bhi .u2
    sub r7, r7, #32
.u2:
.raw:
    cmp r6, r7
    bne .nxp
    add r5, r5, #1
    b .ch
.nxp:
    add r0, r0, #1
    subs r1, r1, #1
    bne .pos
.no:
    mov r0, #0
    pop {r4, r5, r6, r7}
    bx lr
.yes:
    mov r0, #1
    pop {r4, r5, r6, r7}
    bx lr
@endproc

;; @proc finda32_process_one
;; @abi arm_nt
;; @in r0 = file path UTF-8 (empty=stdin), r1 = show_banner (1 or 0)
;; @out r0 = 1 if matched, 0 if none, 2 on open error
;; @err r0 = 2
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Loads stream and scans lines for g_finda32_needle.
@proc finda32_process_one
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; path
    mov r5, r1 ; banner
    mov r8, #0 ; matches
    mov r9, #0 ; line_no

    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 260000
    mov r3, #1
    bl ua32_load_stream
    cmp r1, #0
    beq .loaded
    adr_sym r0, szFinda32Nf
    bl ua32_write_err_str
    mov r0, r4
    bl ua32_write_err_str
    adr_sym r0, szUa32Crlf
    bl ua32_write_err_str
    mov r0, #2
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.loaded:
    mov r6, r0
    cmp r5, #0
    beq .st
    adr_sym r0, g_finda32_cnt_only
    ldr r0, [r0]
    cmp r0, #0
    bne .st
    adr_sym r0, szFinda32Hdr
    bl ua32_write_str
    mov r0, r4
    bl ua32_write_str
    bl ua32_write_crlf

.st:
    adr_sym r0, g_ua32_arena
    ldr r7, [r0] ; cur
    add r6, r7, r6 ; end

.ln_lp:
    cmp r7, r6
    bhs .after
    ldrb r0, [r7]
    cmp r0, #0x1A
    beq .after
    add r9, r9, #1
    mov r10, r7
.eol:
    cmp r10, r6
    bhs .got_l
    ldrb r0, [r10]
    cmp r0, #13
    beq .got_l
    cmp r0, #10
    beq .got_l
    add r10, r10, #1
    b .eol

.got_l:
    mov r0, r7
    sub r1, r10, r7
    adr_sym r2, g_finda32_needle
    adr_sym r3, g_finda32_nlen
    ldr r3, [r3]
    bl finda32_line_has
    adr_sym r1, g_finda32_inv
    ldr r1, [r1]
    eor r0, r0, r1
    cmp r0, #0
    beq .adv

    add r8, r8, #1
    adr_sym r0, g_finda32_cnt_only
    ldr r0, [r0]
    cmp r0, #0
    bne .adv

    adr_sym r0, g_finda32_nums
    ldr r0, [r0]
    cmp r0, #0
    beq .emit
    mov r0, #91 ; [
    bl ua32_write_chr
    mov r0, r9
    bl ua32_print_u32
    mov r0, #93 ; ]
    bl ua32_write_chr
.emit:
    mov r0, r7
    sub r1, r10, r7
    bl ua32_write_out
    bl ua32_write_crlf

.adv:
    cmp r10, r6
    bhs .after
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

.after:
    adr_sym r0, g_finda32_cnt_only
    ldr r0, [r0]
    cmp r0, #0
    beq .ret
    cmp r5, #0
    beq .bc
    adr_sym r0, szFinda32Hdr
    bl ua32_write_str
    mov r0, r4
    bl ua32_write_str
    adr_sym r0, szFinda32Col
    bl ua32_write_str
.bc:
    mov r0, r8
    bl ua32_print_u32
    bl ua32_write_crlf
.ret:
    mov r0, #0
    cmp r8, #0
    beq .dn
    mov r0, #1
.dn:
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 2 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 FIND.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_finda32_inv
    str r1, [r0]
    adr_sym r0, g_finda32_cnt_only
    str r1, [r0]
    adr_sym r0, g_finda32_nums
    str r1, [r0]
    adr_sym r0, g_finda32_ci
    str r1, [r0]
    adr_sym r0, g_finda32_has_n
    str r1, [r0]
    adr_sym r0, g_finda32_files
    str r1, [r0]
    adr_sym r0, g_finda32_hit
    str r1, [r0]
    adr_sym r0, g_finda32_err
    str r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .fin
    cmp r1, #34
    beq .nof
    cmp r1, #47
    bne .nof
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #86 ; V
    beq .sv
    cmp r0, #67 ; C
    beq .sc
    cmp r0, #78 ; N
    beq .sn
    cmp r0, #73 ; I
    beq .si
    cmp r0, #79 ; O
    beq .arg_lp
    b .bad
.sv:
    adr_sym r0, g_finda32_inv
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sc:
    adr_sym r0, g_finda32_cnt_only
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sn:
    adr_sym r0, g_finda32_nums
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.si:
    adr_sym r0, g_finda32_ci
    mov r1, #1
    str r1, [r0]
    b .arg_lp

.nof:
    adr_sym r0, g_finda32_has_n
    ldr r0, [r0]
    cmp r0, #0
    bne .farg
    cmp r1, #34
    bne .bad
    mov r0, r4
    adr_sym r1, g_finda32_needle
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_finda32_nlen
    str r1, [r0]
    adr_sym r0, g_finda32_has_n
    mov r1, #1
    str r1, [r0]
    b .arg_lp

.farg:
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_finda32_files
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    adr_sym r0, g_ua32_arg_b
    mov r1, #1
    bl finda32_process_one
    cmp r0, #1
    beq .hf
    cmp r0, #2
    bne .arg_lp
    adr_sym r0, g_finda32_err
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.hf:
    adr_sym r0, g_finda32_hit
    mov r1, #1
    str r1, [r0]
    b .arg_lp

.help:
    adr_sym r0, szFinda32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.bad:
    adr_sym r0, szFinda32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #2
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.fin:
    adr_sym r0, g_finda32_has_n
    ldr r0, [r0]
    cmp r0, #0
    beq .bad
    adr_sym r0, g_finda32_files
    ldr r0, [r0]
    cmp r0, #0
    bne .ex_c
    adr_sym r0, g_ua32_arg_b
    mov r1, #0
    strb r1, [r0]
    bl finda32_process_one
    cmp r0, #1
    bne .ex_c
    adr_sym r0, g_finda32_hit
    mov r1, #1
    str r1, [r0]

.ex_c:
    adr_sym r0, g_finda32_hit
    ldr r0, [r0]
    cmp r0, #0
    bne .ex0
    adr_sym r0, g_finda32_err
    ldr r0, [r0]
    cmp r0, #0
    bne .ex2
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.ex2:
    mov r0, #2
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.ex0:
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szFinda32Hdr, "---------- ", 0
@data db szFinda32Col, ": ", 0
@data db szFinda32Nf, "File not found - ", 0
@data db szFinda32Syn, "FIND: Parameter format not correct", 0
@data db szFinda32Help, "Searches for a text string in a file or files (/V /C /N /I /OFF).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_finda32_inv, 1
@res dword g_finda32_cnt_only, 1
@res dword g_finda32_nums, 1
@res dword g_finda32_ci, 1
@res dword g_finda32_has_n, 1
@res dword g_finda32_nlen, 1
@res dword g_finda32_files, 1
@res dword g_finda32_hit, 1
@res dword g_finda32_err, 1
@res byte g_finda32_needle, 1024
