;; @file mem.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 MEM.EXE utility (/C, /D, /F, /M, /P) via GlobalMemoryStatusEx & GetProcessMemoryInfo.

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc mema32_print_row
;; @abi arm_nt
;; @in r0 = label string, r2 = low32 bytes, r3 = high32 bytes
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints formatted memory summary row in bytes and KiB.
@proc mema32_print_row
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r2
    mov r5, r3
    bl ua32_write_str
    mov r0, r4
    mov r1, r5
    bl ua32_print_u64
    adr_sym r0, szMema32Bytes
    bl ua32_write_str
    lsr r0, r4, #10
    lsl r2, r5, #22
    orr r0, r0, r2
    lsr r1, r5, #10
    bl ua32_print_u64
    adr_sym r0, szMema32Kb
    bl ua32_write_str
    bl ua32_write_crlf
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid switch
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Parses MEM switches and prints physical, pagefile, virtual, and working-set statistics.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    adr_sym r0, g_mema32_det
    mov r1, #0
    str r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .query
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5]
    cmp r0, #47
    bne .arg_lp
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #67 ; C
    beq .set_d
    cmp r0, #68 ; D
    beq .set_d
    cmp r0, #70 ; F
    beq .arg_lp
    cmp r0, #80 ; P
    beq .arg_lp
    cmp r0, #77 ; M
    bne .bad
    ldrb r0, [r5, #2]
    cmp r0, #58
    beq .arg_lp
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.set_d:
    adr_sym r0, g_mema32_det
    mov r1, #1
    str r1, [r0]
    b .arg_lp

.help:
    adr_sym r0, szMema32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.bad:
    adr_sym r0, szMema32Bad
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.query:
    adr_sym r0, szMema32Hdr
    bl ua32_write_str
    bl ua32_write_crlf
    adr_sym r0, szMema32Sep
    bl ua32_write_str
    bl ua32_write_crlf

    adr_sym r5, g_mema32_stat
    mov r0, #0
    mov r1, #0
.zr:
    cmp r0, #64
    bhs .call_ex
    strb r1, [r5, r0]
    add r0, r0, #1
    b .zr
.call_ex:
    mov r0, #64
    str r0, [r5]
    mov r0, r5
    call_iat __imp_GlobalMemoryStatusEx

    adr_sym r0, szMema32TotPhys
    ldr r2, [r5, #8]
    ldr r3, [r5, #12]
    bl mema32_print_row

    adr_sym r0, szMema32AvlPhys
    ldr r2, [r5, #16]
    ldr r3, [r5, #20]
    bl mema32_print_row

    adr_sym r0, szMema32TotPage
    ldr r2, [r5, #24]
    ldr r3, [r5, #28]
    bl mema32_print_row

    adr_sym r0, szMema32AvlPage
    ldr r2, [r5, #32]
    ldr r3, [r5, #36]
    bl mema32_print_row

    adr_sym r0, szMema32TotVirt
    ldr r2, [r5, #40]
    ldr r3, [r5, #44]
    bl mema32_print_row

    adr_sym r0, szMema32AvlVirt
    ldr r2, [r5, #48]
    ldr r3, [r5, #52]
    bl mema32_print_row

    adr_sym r0, g_mema32_det
    ldr r0, [r0]
    cmp r0, #0
    beq .fin

    adr_sym r0, u16Ua32Kernel32
    call_iat __imp_GetModuleHandleW
    cmp r0, #0
    beq .try_ps
    adr_sym r1, szFna32K32Pmi
    call_iat __imp_GetProcAddress
    cmp r0, #0
    bne .got_pmi
.try_ps:
    adr_sym r0, u16Ua32Psapi
    call_iat __imp_LoadLibraryW
    cmp r0, #0
    beq .fin
    adr_sym r1, szFna32PsPmi
    call_iat __imp_GetProcAddress
    cmp r0, #0
    beq .fin
.got_pmi:
    mov r6, r0
    call_iat __imp_GetCurrentProcess
    adr_sym r1, g_mema32_pmc
    mov r2, #40
    blx r6
    cmp r0, #0
    beq .fin
    adr_sym r0, szMema32ProcWs
    adr_sym r1, g_mema32_pmc
    ldr r2, [r1, #12]
    mov r3, #0
    bl mema32_print_row

.fin:
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szFna32K32Pmi, "K32GetProcessMemoryInfo", 0
@data db szFna32PsPmi, "GetProcessMemoryInfo", 0
@data db szMema32Hdr, "Memory Type              Total Bytes (KiB)", 0
@data db szMema32Sep, "------------------------ --------------------------", 0
@data db szMema32TotPhys, "Total Physical RAM     : ", 0
@data db szMema32AvlPhys, "Available Physical RAM : ", 0
@data db szMema32TotPage, "Total Page File        : ", 0
@data db szMema32AvlPage, "Available Page File    : ", 0
@data db szMema32TotVirt, "Total Virtual Address  : ", 0
@data db szMema32AvlVirt, "Free Virtual Address   : ", 0
@data db szMema32ProcWs, "Process Working Set    : ", 0
@data db szMema32Bytes, " (", 0
@data db szMema32Kb, "K)", 0
@data db szMema32Bad, "Invalid switch.", 0
@data db szMema32Help, "Displays ARM32 physical, pagefile, and virtual memory usage.", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_mema32_det, 1
@res byte g_mema32_stat, 64
@res byte g_mema32_pmc, 48
