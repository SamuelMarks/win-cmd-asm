;; @file hostname.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 HOSTNAME.EXE computer network name reporter (GetComputerNameExW / GetComputerNameW).

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
;; @out Terminates via ExitProcess (0=ok, 1=invalid option)
;; @err Exits 1 on invalid option
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints computer DNS/NetBIOS hostname via GetComputerNameExW or GetComputerNameW.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    bl ua64_init
    bl ua64_skip_ws
    cbz w1, .query
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    adr_sym x0, g_ua64_arg_a
    ldrb w1, [x0]
    cmp w1, #47
    b.ne .bad
    ldrb w1, [x0, #1]
    cmp w1, #63
    b.ne .bad
    adr_sym x0, szHna64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out
.bad:
    adr_sym x0, szHna64Err
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out

.query:
    adr_sym x2, g_hna64_sz
    mov w0, #500
    str w0, [x2]
    mov w0, #1 ; ComputerNameDnsHostname
    adr_sym x1, g_ua64_wbuf1
    call_iat __imp_GetComputerNameExW
    cbnz w0, .cvt

    adr_sym x1, g_hna64_sz
    mov w0, #500
    str w0, [x1]
    adr_sym x0, g_ua64_wbuf1
    call_iat __imp_GetComputerNameW
    cbz w0, .env

.cvt:
    adr_sym x0, g_ua64_wbuf1
    adr_sym x1, g_ua64_arg_b
    mov x2, #1000
    bl ua64_w_to_u8
    b .pr

.env:
    adr_sym x0, szHna64Env
    adr_sym x1, g_ua64_arg_b
    mov x2, #1000
    bl ua64_get_env
    cbnz x0, .pr
    adr_sym x0, g_ua64_arg_b
    adr_sym x1, szHna64Def
    bl ua64_strcpy

.pr:
    adr_sym x0, g_ua64_arg_b
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szHna64Env, "COMPUTERNAME", 0
@data db szHna64Def, "WINARM64", 0
@data db szHna64Help, "Prints the name of the current host.", 0
@data db szHna64Err, "HOSTNAME: Invalid option.", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_hna64_sz, 1
