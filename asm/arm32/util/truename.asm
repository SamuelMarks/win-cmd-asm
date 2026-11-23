;; @file truename.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 TRUENAME.EXE canonical path & reparse resolver.

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Resolves input path (or CWD) via GetFinalPathNameByHandleW / GetFullPathNameW.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    adr_sym r0, g_ua32_arg_b
    mov r1, #0
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
    adr_sym r0, g_ua32_arg_a
    ldrb r0, [r0, #1]
    cmp r0, #63
    beq .help
    b .arg_lp
.pos:
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp

.help:
    adr_sym r0, szTna32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.run:
    adr_sym r5, g_ua32_arg_b
    ldrb r0, [r5]
    cmp r0, #0
    bne .res
    mov r0, #46
    strb r0, [r5]
    mov r0, #0
    strb r0, [r5, #1]

.res:
    adr_sym r0, u16Ua32Kernel32
    call_iat __imp_GetModuleHandleW
    cmp r0, #0
    beq .full
    adr_sym r1, szFna32Final
    call_iat __imp_GetProcAddress
    cmp r0, #0
    beq .full
    mov r6, r0

    mov r0, r5
    bl ua32_open_read
    cmn r0, #1
    beq .full
    mov r4, r0
    adr_sym r1, g_ua32_wbuf2
    mov r2, #1000
    mov r3, #0
    blx r6
    mov r6, r0
    mov r0, r4
    call_iat __imp_CloseHandle
    cmp r6, #0
    beq .full
    cmp r6, #1000
    bhs .full
    b .cvt

.full:
    mov r0, r5
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    mov r1, #1000
    adr_sym r2, g_ua32_wbuf2
    mov r3, #0
    call_iat __imp_GetFullPathNameW
    cmp r0, #0
    beq .err

.cvt:
    adr_sym r0, g_ua32_wbuf2
    adr_sym r1, g_ua32_arg_c
    mov r2, #1020
    bl ua32_w_to_u8

    adr_sym r0, g_ua32_arg_c
    ldrb r1, [r0]
    cmp r1, #0x5C
    bne .em
    ldrb r1, [r0, #1]
    cmp r1, #0x5C
    bne .em
    ldrb r1, [r0, #2]
    cmp r1, #63
    bne .em
    ldrb r1, [r0, #3]
    cmp r1, #0x5C
    bne .em
    add r0, r0, #4
.em:
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.err:
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szFna32Final, "GetFinalPathNameByHandleW", 0
@data db szTna32Help, "TRUENAME [drive:][path][filename]", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
