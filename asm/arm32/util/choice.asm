;; @file choice.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 CHOICE.EXE prompt utility (/C[:]choices, /N, /CS, /T nn /D c, /M msg).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc choicea32_match
;; @abi arm_nt
;; @in r0 = input char
;; @out r0 = 1-based index in g_cha32_list, or 0 if not matched
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12
;; @desc Matches r0 against choice list honoring /CS.
@proc choicea32_match
    and r0, r0, #0xFF
    adr_sym r1, g_cha32_cs
    ldr r1, [r1]
    cmp r1, #0
    bne .sc
    cmp r0, #97
    blo .sc
    cmp r0, #122
    bhi .sc
    sub r0, r0, #32
.sc:
    adr_sym r2, g_cha32_list
    mov r3, #0
.lp:
    ldrb r12, [r2, r3]
    cmp r12, #0
    beq .no
    cmp r1, #0
    bne .cc
    cmp r12, #97
    blo .cc
    cmp r12, #122
    bhi .cc
    sub r12, r12, #32
.cc:
    cmp r12, r0
    beq .hit
    add r3, r3, #1
    b .lp
.hit:
    add r0, r3, #1
    bx lr
.no:
    mov r0, #0
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess (1..N or 255)
;; @err Exits 255 on syntax error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 CHOICE.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    sub sp, sp, #8
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_cha32_hide
    str r1, [r0]
    adr_sym r0, g_cha32_cs
    str r1, [r0]
    adr_sym r0, g_cha32_has_to
    str r1, [r0]
    adr_sym r0, g_cha32_to
    str r1, [r0]
    adr_sym r0, g_cha32_def
    strb r1, [r0]
    adr_sym r0, g_cha32_msg
    strb r1, [r0]
    adr_sym r0, g_cha32_list
    mov r1, #89 ; Y
    strb r1, [r0]
    mov r1, #78 ; N
    strb r1, [r0, #1]
    mov r1, #0
    strb r1, [r0, #2]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .prompt
    cmp r1, #47
    bne .mpos
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #78 ; N
    beq .sn
    cmp r0, #67 ; C
    beq .sc
    cmp r0, #84 ; T
    beq .st
    cmp r0, #68 ; D
    beq .sd
    cmp r0, #77 ; M
    beq .sm
    b .bad
.sn:
    adr_sym r0, g_cha32_hide
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sc:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #83
    bne .scl
    ldrb r0, [r5, #3]
    cmp r0, #0
    bne .scl
    adr_sym r0, g_cha32_cs
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.scl:
    add r1, r5, #2
    ldrb r0, [r1]
    cmp r0, #58
    bne .sci
    add r1, r1, #1
.sci:
    ldrb r0, [r1]
    cmp r0, #0
    beq .scn
    adr_sym r0, g_cha32_list
    bl ua32_strcpy
    b .arg_lp
.scn:
    mov r0, r4
    adr_sym r1, g_cha32_list
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.st:
    ldrb r0, [r5, #2]
    cmp r0, #58
    bne .ntt
    ldrb r0, [r5, #3]
    adr_sym r1, g_cha32_def
    strb r0, [r1]
    adr_sym r1, g_cha32_has_to
    mov r0, #1
    str r0, [r1]
    ldrb r0, [r5, #4]
    cmp r0, #44
    bne .arg_lp
    add r0, r5, #5
    bl ua32_parse_u32
    adr_sym r1, g_cha32_to
    str r0, [r1]
    b .arg_lp
.ntt:
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_ua32_arg_b
    bl ua32_parse_u32
    adr_sym r1, g_cha32_to
    str r0, [r1]
    adr_sym r1, g_cha32_has_to
    mov r0, #1
    str r0, [r1]
    b .arg_lp
.sd:
    ldrb r0, [r5, #2]
    cmp r0, #58
    bne .ntd
    ldrb r0, [r5, #3]
    adr_sym r1, g_cha32_def
    strb r0, [r1]
    b .arg_lp
.ntd:
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    adr_sym r1, g_ua32_arg_b
    ldrb r0, [r1]
    adr_sym r1, g_cha32_def
    strb r0, [r1]
    b .arg_lp
.sm:
    mov r0, r4
    adr_sym r1, g_cha32_msg
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.mpos:
    adr_sym r1, g_cha32_msg
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp

.help:
    adr_sym r0, szCha32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #8
    pop {r4, r5, r6, lr}
    bx lr

.bad:
    adr_sym r0, szCha32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #255
    bl ua32_exit
    add sp, sp, #8
    pop {r4, r5, r6, lr}
    bx lr

.prompt:
    adr_sym r5, g_cha32_list
    ldrb r0, [r5]
    cmp r0, #0
    beq .bad
    adr_sym r0, g_cha32_msg
    ldrb r1, [r0]
    cmp r1, #0
    beq .cbr
    bl ua32_write_str
    mov r0, #32
    bl ua32_write_chr
.cbr:
    adr_sym r0, g_cha32_hide
    ldr r0, [r0]
    cmp r0, #0
    bne .win
    mov r0, #91 ; [
    bl ua32_write_chr
    mov r6, #0
.prc:
    ldrb r0, [r5, r6]
    cmp r0, #0
    beq .ebr
    cmp r6, #0
    beq .emc
    mov r0, #44
    bl ua32_write_chr
    ldrb r0, [r5, r6]
.emc:
    adr_sym r1, g_cha32_cs
    ldr r1, [r1]
    cmp r1, #0
    bne .rwc
    bl ua32_chr_upper
.rwc:
    bl ua32_write_chr
    add r6, r6, #1
    b .prc
.ebr:
    adr_sym r0, szCha32Q
    bl ua32_write_str

.win:
    adr_sym r0, g_cha32_has_to
    ldr r0, [r0]
    cmp r0, #0
    beq .rd
    adr_sym r0, g_cha32_to
    ldr r0, [r0]
    cmp r0, #0
    beq .udef
.rd:
    adr_sym r1, g_cha32_in
    mov r0, #0
    strb r0, [r1]
    adr_sym r0, g_ua32_stdin
    ldr r0, [r0]
    mov r2, #1
    adr_sym r3, g_ua32_io_done
    mov r12, #0
    str r12, [sp]
    call_iat __imp_ReadFile
    cmp r0, #0
    beq .eof
    adr_sym r0, g_ua32_io_done
    ldr r0, [r0]
    cmp r0, #0
    beq .eof
    adr_sym r0, g_cha32_in
    ldrb r5, [r0]
    cmp r5, #13
    beq .rd
    cmp r5, #10
    beq .rd
    mov r0, r5
    bl choicea32_match
    cmp r0, #0
    beq .rd
    mov r6, r0
    mov r0, r5
    bl ua32_write_chr
    bl ua32_write_crlf
    mov r0, r6
    bl ua32_exit
    add sp, sp, #8
    pop {r4, r5, r6, lr}
    bx lr

.eof:
    adr_sym r0, g_cha32_def
    ldrb r0, [r0]
    cmp r0, #0
    bne .udef
    mov r0, #255
    bl ua32_exit
    add sp, sp, #8
    pop {r4, r5, r6, lr}
    bx lr
.udef:
    adr_sym r0, g_cha32_def
    ldrb r5, [r0]
    mov r0, r5
    bl choicea32_match
    cmp r0, #0
    beq .bad
    mov r6, r0
    mov r0, r5
    bl ua32_write_chr
    bl ua32_write_crlf
    mov r0, r6
    bl ua32_exit
    add sp, sp, #8
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szCha32Q, "]?", 0
@data db szCha32Syn, "ERROR: Invalid choice syntax.", 0
@data db szCha32Help, "Prompts the user to select one item from a list of choices (/C /N /CS /T /D /M).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_cha32_hide, 1
@res dword g_cha32_cs, 1
@res dword g_cha32_has_to, 1
@res dword g_cha32_to, 1
@res byte g_cha32_def, 4
@res byte g_cha32_in, 4
@res byte g_cha32_list, 128
@res byte g_cha32_msg, 520
