;; @file hostname.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 HOSTNAME.EXE computer network name reporter.

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
;; @err Exits 1 on invalid option
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints computer DNS/NetBIOS hostname via GetComputerNameExW or GetComputerNameW.
@proc mainCRTStartup
    push {r4, lr}
    bl ua32_init
    bl ua32_skip_ws
    cmp r1, #0
    beq .query
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    adr_sym r0, g_ua32_arg_a
    ldrb r1, [r0]
    cmp r1, #47
    bne .bad
    ldrb r1, [r0, #1]
    cmp r1, #63
    bne .bad
    adr_sym r0, szHna32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, lr}
    bx lr
.bad:
    adr_sym r0, szHna32Err
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    pop {r4, lr}
    bx lr

.query:
    adr_sym r2, g_hna32_sz
    mov r0, #500
    str r0, [r2]
    mov r0, #1 ; ComputerNameDnsHostname
    adr_sym r1, g_ua32_wbuf1
    call_iat __imp_GetComputerNameExW
    cmp r0, #0
    bne .cvt

    adr_sym r1, g_hna32_sz
    mov r0, #500
    str r0, [r1]
    adr_sym r0, g_ua32_wbuf1
    call_iat __imp_GetComputerNameW
    cmp r0, #0
    beq .env

.cvt:
    adr_sym r0, g_ua32_wbuf1
    adr_sym r1, g_ua32_arg_b
    mov r2, #1000
    bl ua32_w_to_u8
    b .pr
.env:
    adr_sym r0, szHna32Env
    adr_sym r1, g_ua32_arg_b
    mov r2, #1000
    bl ua32_get_env
    cmp r0, #0
    bne .pr
    adr_sym r0, g_ua32_arg_b
    adr_sym r1, szHna32Def
    bl ua32_strcpy
.pr:
    adr_sym r0, g_ua32_arg_b
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, lr}
    bx lr
@endproc

@section rdata

@data db szHna32Env, "COMPUTERNAME", 0
@data db szHna32Def, "ARM32PC", 0
@data db szHna32Help, "Prints the name of the current host.", 0
@data db szHna32Err, "HOSTNAME: Invalid option.", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_hna32_sz, 1
