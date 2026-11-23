;; @file comp.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 COMP.EXE file comparison utility (/D, /A, /L, /N=num, /C, /M file1 file2).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc compa32_fmt_byte
;; @abi arm_nt
;; @in r0 = byte value (0..255)
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints byte value as ASCII (/A), decimal (/D), or hex (default).
@proc compa32_fmt_byte
    push {r4, lr}
    adr_sym r1, g_cmpa32_asc
    ldr r1, [r1]
    cmp r1, #0
    bne .asc
    adr_sym r1, g_cmpa32_dec
    ldr r1, [r1]
    cmp r1, #0
    bne .dec
    bl ua32_print_hex8
    pop {r4, lr}
    bx lr
.dec:
    and r0, r0, #0xFF
    bl ua32_print_u32
    pop {r4, lr}
    bx lr
.asc:
    bl ua32_write_chr
    pop {r4, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=mismatch, 2=error)
;; @err Exits 2 on missing file
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 COMP.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_cmpa32_dec
    str r1, [r0]
    adr_sym r0, g_cmpa32_asc
    str r1, [r0]
    adr_sym r0, g_cmpa32_ln
    str r1, [r0]
    adr_sym r0, g_cmpa32_ci
    str r1, [r0]
    adr_sym r0, g_cmpa32_max_ln
    str r1, [r0]
    adr_sym r0, g_cmpa32_no_p
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
    cmp r0, #68 ; D
    beq .sd
    cmp r0, #65 ; A
    beq .sa
    cmp r0, #76 ; L
    beq .sl
    cmp r0, #67 ; C
    beq .sc
    cmp r0, #77 ; M
    beq .sm
    cmp r0, #78 ; N
    beq .sn
    b .arg_lp
.sd:
    adr_sym r0, g_cmpa32_dec
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sa:
    adr_sym r0, g_cmpa32_asc
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sl:
    adr_sym r0, g_cmpa32_ln
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sc:
    adr_sym r0, g_cmpa32_ci
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sm:
    adr_sym r0, g_cmpa32_no_p
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sn:
    ldrb r0, [r5, #2]
    cmp r0, #61 ; =
    bne .arg_lp
    add r0, r5, #3
    bl ua32_parse_u32
    adr_sym r1, g_cmpa32_max_ln
    str r0, [r1]
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
    adr_sym r0, szCmpa32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.run:
    adr_sym r0, g_ua32_arg_b
    ldrb r0, [r0]
    cmp r0, #0
    beq .err
    adr_sym r0, g_ua32_arg_c
    ldrb r0, [r0]
    cmp r0, #0
    beq .err

    adr_sym r0, g_ua32_arg_b
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 220000
    mov r3, #0
    bl ua32_load_stream
    cmp r1, #0
    bne .err
    mov r5, r0

    adr_sym r0, g_ua32_arg_c
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r12, 262144
    add r1, r1, r12
    mov_imm32 r2, 220000
    mov r3, #0
    bl ua32_load_stream
    cmp r1, #0
    bne .err
    mov r6, r0

    adr_sym r0, szCmpa32H1
    bl ua32_write_str
    adr_sym r0, g_ua32_arg_b
    bl ua32_write_str
    adr_sym r0, szCmpa32H2
    bl ua32_write_str
    adr_sym r0, g_ua32_arg_c
    bl ua32_write_str
    adr_sym r0, szCmpa32Dots
    bl ua32_write_str
    bl ua32_write_crlf

    mov r10, #0 ; errs
    adr_sym r0, g_cmpa32_max_ln
    ldr r0, [r0]
    cmp r0, #0
    bne .st
    cmp r5, r6
    beq .st
    adr_sym r0, szCmpa32Diff
    bl ua32_write_str
    bl ua32_write_crlf
    mov r10, #1
    b .prompt

.st:
    mov r9, r5
    cmp r9, r6
    bls .gmin
    mov r9, r6
.gmin:
    adr_sym r0, g_ua32_arena
    ldr r7, [r0]
    mov_imm32 r12, 262144
    add r8, r7, r12
    mov r4, #0 ; offset
    adr_sym r0, g_cmpa32_cur_ln
    mov r1, #1
    str r1, [r0]

.lp:
    cmp r4, r9
    bhs .fin
    ldrb r0, [r7, r4]
    cmp r0, #10
    bne .nonl
    adr_sym r1, g_cmpa32_cur_ln
    ldr r2, [r1]
    add r2, r2, #1
    str r2, [r1]
    adr_sym r1, g_cmpa32_max_ln
    ldr r1, [r1]
    cmp r1, #0
    beq .nonl
    sub r2, r2, #1
    cmp r2, r1
    bhs .fin
.nonl:
    ldrb r1, [r8, r4]
    adr_sym r2, g_cmpa32_ci
    ldr r2, [r2]
    cmp r2, #0
    beq .raw
    cmp r0, #97
    blo .u1
    cmp r0, #122
    bhi .u1
    sub r0, r0, #32
.u1:
    cmp r1, #97
    blo .u2
    cmp r1, #122
    bhi .u2
    sub r1, r1, #32
.u2:
.raw:
    cmp r0, r1
    beq .nx

    add r10, r10, #1
    cmp r10, #10
    bhi .fin
    adr_sym r0, g_cmpa32_ln
    ldr r0, [r0]
    cmp r0, #0
    bne .prln
    adr_sym r0, szCmpa32EOff
    bl ua32_write_str
    mov r0, r4
    bl ua32_print_hex32
    b .vals
.prln:
    adr_sym r0, szCmpa32ELn
    bl ua32_write_str
    adr_sym r0, g_cmpa32_cur_ln
    ldr r0, [r0]
    bl ua32_print_u32
.vals:
    bl ua32_write_crlf
    adr_sym r0, szCmpa32F1
    bl ua32_write_str
    ldrb r0, [r7, r4]
    bl compa32_fmt_byte
    bl ua32_write_crlf
    adr_sym r0, szCmpa32F2
    bl ua32_write_str
    ldrb r0, [r8, r4]
    bl compa32_fmt_byte
    bl ua32_write_crlf
.nx:
    add r4, r4, #1
    b .lp

.fin:
    cmp r10, #0
    bne .prompt
    adr_sym r0, szCmpa32Ok
    bl ua32_write_str
    bl ua32_write_crlf
.prompt:
    adr_sym r0, g_cmpa32_no_p
    ldr r0, [r0]
    cmp r0, #0
    bne .ex
    adr_sym r0, g_ua32_in_is_con
    ldr r0, [r0]
    cmp r0, #0
    beq .ex
    adr_sym r0, szCmpa32More
    bl ua32_write_str
    bl ua32_write_crlf
.ex:
    cmp r10, #0
    bne .ex1
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
.ex1:
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
.err:
    adr_sym r0, szCmpa32Open
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #2
    bl ua32_exit
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

@section rdata

@data db szCmpa32H1, "Comparing ", 0
@data db szCmpa32H2, " and ", 0
@data db szCmpa32Dots, "...", 0
@data db szCmpa32Ok, "Files compare OK", 0
@data db szCmpa32Diff, "Files are different sizes", 0
@data db szCmpa32EOff, "Compare error at OFFSET ", 0
@data db szCmpa32ELn, "Compare error at LINE ", 0
@data db szCmpa32F1, "file1 = ", 0
@data db szCmpa32F2, "file2 = ", 0
@data db szCmpa32More, "Compare more files (Y/N) ? N", 0
@data db szCmpa32Open, "Can't find/open file", 0
@data db szCmpa32Help, "Compares the contents of two files or sets of files (/D /A /L /N=num /C /M).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_cmpa32_dec, 1
@res dword g_cmpa32_asc, 1
@res dword g_cmpa32_ln, 1
@res dword g_cmpa32_ci, 1
@res dword g_cmpa32_max_ln, 1
@res dword g_cmpa32_no_p, 1
@res dword g_cmpa32_cur_ln, 1
