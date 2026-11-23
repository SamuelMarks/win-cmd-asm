;; @file truename.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 TRUENAME.EXE canonical path & reparse resolver.

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Resolves input path (or CWD) via GetFinalPathNameByHandleW / GetFullPathNameW.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_ua64_arg_b
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
    adr_sym x0, g_ua64_arg_a
    ldrb w0, [x0, #1]
    cmp w0, #63
    b.eq .help
    b .arg_lp
.pos:
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp

.help:
    adr_sym x0, szTna64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.run:
    adr_sym x20, g_ua64_arg_b
    ldrb w0, [x20]
    cbnz w0, .res
    mov w0, #46
    strb w0, [x20]
    strb wzr, [x20, #1]

.res:
    adr_sym x0, u16Ua64Kernel32
    call_iat __imp_GetModuleHandleW
    cbz x0, .full
    adr_sym x1, szFna64Final
    call_iat __imp_GetProcAddress
    cbz x0, .full
    mov x21, x0

    mov x0, x20
    bl ua64_open_read
    cmn x0, #1
    b.eq .full
    mov x19, x0
    adr_sym x1, g_ua64_wbuf2
    mov w2, #1000
    mov w3, #0
    blr x21
    mov w21, w0
    mov x0, x19
    call_iat __imp_CloseHandle
    cbz w21, .full
    cmp w21, #1000
    b.hs .full
    b .cvt

.full:
    mov x0, x20
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    mov w1, #1000
    adr_sym x2, g_ua64_wbuf2
    mov x3, #0
    call_iat __imp_GetFullPathNameW
    cbz w0, .err

.cvt:
    adr_sym x0, g_ua64_wbuf2
    adr_sym x1, g_ua64_arg_c
    mov x2, #1020
    bl ua64_w_to_u8

    adr_sym x0, g_ua64_arg_c
    ldrb w1, [x0]
    cmp w1, #0x5C
    b.ne .em
    ldrb w1, [x0, #1]
    cmp w1, #0x5C
    b.ne .em
    ldrb w1, [x0, #2]
    cmp w1, #63
    b.ne .em
    ldrb w1, [x0, #3]
    cmp w1, #0x5C
    b.ne .em
    add x0, x0, #4
.em:
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.err:
    mov w0, #1
    bl ua64_exit
.out:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szFna64Final, "GetFinalPathNameByHandleW", 0
@data db szTna64Help, "TRUENAME [drive:][path][filename]", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
