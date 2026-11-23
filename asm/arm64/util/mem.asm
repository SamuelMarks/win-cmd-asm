;; @file mem.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 MEM.EXE utility (/C, /D, /F, /M, /P) via GlobalMemoryStatusEx & GetProcessMemoryInfo.

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc mema64_print_row
;; @abi arm64_ms
;; @in x0 = label string, x1 = 64-bit byte total
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints formatted memory summary row in bytes and KiB.
@proc mema64_print_row
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x1
    bl ua64_write_str
    mov x0, x19
    bl ua64_print_u64
    adr_sym x0, szMema64Bytes
    bl ua64_write_str
    lsr x0, x19, #10
    bl ua64_print_u64
    adr_sym x0, szMema64Kb
    bl ua64_write_str
    bl ua64_write_crlf
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid switch
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 MEM.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_mema64_det
    str wzr, [x0]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .query
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20]
    cmp w0, #47
    b.ne .arg_lp
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #67 ; C
    b.eq .set_d
    cmp w0, #68 ; D
    b.eq .set_d
    cmp w0, #70 ; F
    b.eq .arg_lp
    cmp w0, #80 ; P
    b.eq .arg_lp
    cmp w0, #77 ; M
    b.ne .bad
    ldrb w0, [x20, #2]
    cmp w0, #58
    b.eq .arg_lp
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.set_d:
    adr_sym x0, g_mema64_det
    mov w1, #1
    str w1, [x0]
    b .arg_lp

.help:
    adr_sym x0, szMema64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.bad:
    adr_sym x0, szMema64Bad
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out

.query:
    adr_sym x0, szMema64Hdr
    bl ua64_write_str
    bl ua64_write_crlf
    adr_sym x0, szMema64Sep
    bl ua64_write_str
    bl ua64_write_crlf

    adr_sym x20, g_mema64_stat
    mov x0, #0
.zr:
    cmp x0, #64
    b.hs .call_ex
    strb wzr, [x20, x0]
    add x0, x0, #1
    b .zr
.call_ex:
    mov w0, #64
    str w0, [x20]
    mov x0, x20
    call_iat __imp_GlobalMemoryStatusEx

    adr_sym x0, szMema64TotPhys
    ldr x1, [x20, #8]
    bl mema64_print_row

    adr_sym x0, szMema64AvlPhys
    ldr x1, [x20, #16]
    bl mema64_print_row

    adr_sym x0, szMema64TotPage
    ldr x1, [x20, #24]
    bl mema64_print_row

    adr_sym x0, szMema64AvlPage
    ldr x1, [x20, #32]
    bl mema64_print_row

    adr_sym x0, szMema64TotVirt
    ldr x1, [x20, #40]
    bl mema64_print_row

    adr_sym x0, szMema64AvlVirt
    ldr x1, [x20, #48]
    bl mema64_print_row

    adr_sym x0, g_mema64_det
    ldr w0, [x0]
    cbz w0, .fin

    adr_sym x0, u16Ua64Kernel32
    call_iat __imp_GetModuleHandleW
    cbz x0, .try_ps
    adr_sym x1, szFna64K32Pmi
    call_iat __imp_GetProcAddress
    cbnz x0, .got_pmi
.try_ps:
    adr_sym x0, u16Ua64Psapi
    call_iat __imp_LoadLibraryW
    cbz x0, .fin
    adr_sym x1, szFna64PsPmi
    call_iat __imp_GetProcAddress
    cbz x0, .fin
.got_pmi:
    mov x21, x0
    call_iat __imp_GetCurrentProcess
    adr_sym x1, g_mema64_pmc
    mov w2, #72
    blr x21
    cbz w0, .fin
    adr_sym x0, szMema64ProcWs
    adr_sym x2, g_mema64_pmc
    ldr x1, [x2, #16]
    bl mema64_print_row

.fin:
    mov w0, #0
    bl ua64_exit
.out:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szFna64K32Pmi, "K32GetProcessMemoryInfo", 0
@data db szFna64PsPmi, "GetProcessMemoryInfo", 0
@data db szMema64Hdr, "Memory Type              Total Bytes (KiB)", 0
@data db szMema64Sep, "------------------------ --------------------------", 0
@data db szMema64TotPhys, "Total Physical RAM     : ", 0
@data db szMema64AvlPhys, "Available Physical RAM : ", 0
@data db szMema64TotPage, "Total Page File        : ", 0
@data db szMema64AvlPage, "Available Page File    : ", 0
@data db szMema64TotVirt, "Total Virtual Address  : ", 0
@data db szMema64AvlVirt, "Free Virtual Address   : ", 0
@data db szMema64ProcWs, "Process Working Set    : ", 0
@data db szMema64Bytes, " (", 0
@data db szMema64Kb, "K)", 0
@data db szMema64Bad, "Invalid switch.", 0
@data db szMema64Help, "Displays ARM64 physical, pagefile, and virtual memory usage.", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_mema64_det, 1
@res byte g_mema64_stat, 64
@res byte g_mema64_pmc, 80
