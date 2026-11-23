;; @file reg.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 REG.EXE Windows Registry tool (QUERY/ADD/DELETE/COPY/EXPORT/IMPORT/SAVE/RESTORE/COMPARE/FLAGS via ADVAPI32).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc rega32_load_advapi
;; @abi arm_nt
;; @in none
;; @out r0 = 1 if ADVAPI32 Registry APIs resolved, 0 otherwise
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Dynamically resolves ADVAPI32.DLL *W Registry entrypoints.
@proc rega32_load_advapi
    push {r4, lr}
    adr_sym r0, u16Ua32Advapi32
    call_iat __imp_LoadLibraryW
    cmp r0, #0
    beq .fl
    mov r4, r0

    mov r0, r4
    adr_sym r1, szFna32RClose
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32RClose
    str r0, [r1]

    mov r0, r4
    adr_sym r1, szFna32ROpen
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32ROpen
    str r0, [r1]

    mov r0, r4
    adr_sym r1, szFna32RCreate
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32RCreate
    str r0, [r1]

    mov r0, r4
    adr_sym r1, szFna32RSet
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32RSet
    str r0, [r1]

    mov r0, r4
    adr_sym r1, szFna32RQuery
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32RQuery
    str r0, [r1]

    mov r0, r4
    adr_sym r1, szFna32RDelVal
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32RDelVal
    str r0, [r1]

    mov r0, r4
    adr_sym r1, szFna32RDelKey
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32RDelKey
    str r0, [r1]

    mov r0, r4
    adr_sym r1, szFna32REnumVal
    call_iat __imp_GetProcAddress
    adr_sym r1, g_pfna32REnumVal
    str r0, [r1]

    adr_sym r0, g_pfna32ROpen
    ldr r0, [r0]
    cmp r0, #0
    beq .fl
    mov r0, #1
    pop {r4, lr}
    bx lr
.fl:
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc rega32_parse_hive
;; @abi arm_nt
;; @in r0 = full key path
;; @out r0 = root HKEY (0 on invalid), g_rega32_subkey populated
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Splits hive root prefix from subkey path.
@proc rega32_parse_hive
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    adr_sym r5, g_ua32_path_tmp
    mov r1, #0
.rlp:
    ldrb r2, [r4, r1]
    cmp r2, #0
    beq .gr
    cmp r2, #0x5C
    beq .gr
    cmp r2, #47
    beq .gr
    strb r2, [r5, r1]
    add r1, r1, #1
    b .rlp
.gr:
    mov r2, #0
    strb r2, [r5, r1]
    ldrb r2, [r4, r1]
    cmp r2, #0
    beq .es
    add r1, r1, #1
.es:
    adr_sym r0, g_rega32_subkey
    add r1, r4, r1
    bl ua32_strcpy

    mov r0, r5
    adr_sym r1, szHa32Hkcu
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hkcu
    mov r0, r5
    adr_sym r1, szHa32HkcuL
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hkcu

    mov r0, r5
    adr_sym r1, szHa32Hklm
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hklm
    mov r0, r5
    adr_sym r1, szHa32HklmL
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hklm

    mov r0, r5
    adr_sym r1, szHa32Hkcr
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hkcr
    mov r0, r5
    adr_sym r1, szHa32HkcrL
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hkcr

    mov r0, r5
    adr_sym r1, szHa32Hku
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hku
    mov r0, r5
    adr_sym r1, szHa32Hkcc
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .hkcc
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.hkcr:
    mov_imm32 r0, 0x80000000
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.hkcu:
    mov_imm32 r0, 0x80000001
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.hklm:
    mov_imm32 r0, 0x80000002
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.hku:
    mov_imm32 r0, 0x80000003
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.hkcc:
    mov_imm32 r0, 0x80000005
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc rega32_type_name
;; @abi arm_nt
;; @in r0 = Win32 REG_* type
;; @out r0 = static type string
;; @err none
;; @clobbers r0
;; @desc Maps numeric REG_* type to string label.
@proc rega32_type_name
    cmp r0, #2
    beq .te
    cmp r0, #3
    beq .tb
    cmp r0, #4
    beq .td
    cmp r0, #7
    beq .tm
    cmp r0, #11
    beq .tq
    adr_sym r0, szTa32Sz
    bx lr
.te:
    adr_sym r0, szTa32Exp
    bx lr
.tb:
    adr_sym r0, szTa32Bin
    bx lr
.td:
    adr_sym r0, szTa32Dw
    bx lr
.tm:
    adr_sym r0, szTa32Msz
    bx lr
.tq:
    adr_sym r0, szTa32Qw
    bx lr
@endproc

;; @proc rega32_parse_type
;; @abi arm_nt
;; @in r0 = type string
;; @out r0 = Win32 REG_* constant
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Parses /t REG_* string into constant.
@proc rega32_parse_type
    push {r4, lr}
    mov r4, r0
    adr_sym r1, szTa32Dw
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .rd
    mov r0, r4
    adr_sym r1, szTa32Qw
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .rq
    mov r0, r4
    adr_sym r1, szTa32Exp
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .re
    mov r0, r4
    adr_sym r1, szTa32Bin
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .rb
    mov r0, r4
    adr_sym r1, szTa32Msz
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .rm
    mov r0, #1
    pop {r4, lr}
    bx lr
.re:
    mov r0, #2
    pop {r4, lr}
    bx lr
.rb:
    mov r0, #3
    pop {r4, lr}
    bx lr
.rd:
    mov r0, #4
    pop {r4, lr}
    bx lr
.rm:
    mov r0, #7
    pop {r4, lr}
    bx lr
.rq:
    mov r0, #11
    pop {r4, lr}
    bx lr
@endproc

;; @proc rega32_print_val_row
;; @abi arm_nt
;; @in r0 = val name UTF-8, r1 = reg_type, r2 = raw data buf, r3 = cb_data
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints formatted REG QUERY value line.
@proc rega32_print_val_row
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    mov r6, r2
    mov r7, r3

    adr_sym r0, szRa32Ind
    bl ua32_write_str
    mov r0, r4
    ldrb r1, [r0]
    cmp r1, #0
    bne .vn
    adr_sym r0, szRa32Def
.vn:
    bl ua32_write_str
    adr_sym r0, szRa32Ind
    bl ua32_write_str
    mov r0, r5
    bl rega32_type_name
    bl ua32_write_str
    adr_sym r0, szRa32Ind
    bl ua32_write_str

    cmp r5, #4
    beq .fdw
    cmp r5, #11
    beq .fqw
    cmp r5, #3
    beq .fbin

    mov r0, #0
    strh r0, [r6, r7]
    mov r0, r6
    adr_sym r1, g_ua32_path_tmp
    mov r2, #1000
    bl ua32_w_to_u8
    adr_sym r0, g_ua32_path_tmp
    bl ua32_write_str
    b .nl
.fdw:
    adr_sym r0, szRa32Hex
    bl ua32_write_str
    ldr r0, [r6]
    bl ua32_print_u32
    b .nl
.fqw:
    ldr r0, [r6]
    ldr r1, [r6, #4]
    bl ua32_print_u64
    b .nl
.fbin:
    mov r4, #0
.blp:
    cmp r4, r7
    bhs .nl
    ldrb r0, [r6, r4]
    bl ua32_print_hex8
    add r4, r4, #1
    b .blp
.nl:
    bl ua32_write_crlf
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on Registry error or invalid syntax
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 REG.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #28
    bl ua32_init
    mov r4, r0

    mov r1, #1
    adr_sym r0, g_rega32_type
    str r1, [r0]
    mov r1, #0
    adr_sym r0, g_rega32_has_v
    str r1, [r0]
    adr_sym r0, g_rega32_wow
    str r1, [r0]
    adr_sym r0, g_rega32_op
    strb r1, [r0]
    adr_sym r0, g_rega32_k1
    strb r1, [r0]
    adr_sym r0, g_rega32_k2
    strb r1, [r0]
    adr_sym r0, g_rega32_val
    strb r1, [r0]
    adr_sym r0, g_rega32_data
    strb r1, [r0]

    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .bad
    adr_sym r1, g_rega32_op
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_rega32_op
    ldrb r0, [r5]
    cmp r0, #47
    bne .prest
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    b .bad

.prest:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .disp
    cmp r1, #47
    bne .pk
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
    cmp r0, #84 ; T
    beq .st
    cmp r0, #68 ; D
    beq .sd
    cmp r0, #82 ; /reg:32 /reg:64
    beq .swow
    b .prest
.sv:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #69
    beq .sve
    mov r0, r4
    adr_sym r1, g_rega32_val
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_rega32_has_v
    mov r1, #1
    str r1, [r0]
    b .prest
.sve:
    adr_sym r0, g_rega32_val
    mov r1, #0
    strb r1, [r0]
    adr_sym r0, g_rega32_has_v
    mov r1, #1
    str r1, [r0]
    b .prest
.st:
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    adr_sym r0, g_ua32_arg_b
    bl rega32_parse_type
    adr_sym r1, g_rega32_type
    str r0, [r1]
    b .prest
.sd:
    mov r0, r4
    adr_sym r1, g_rega32_data
    bl ua32_read_tok
    mov r4, r0
    b .prest
.swow:
    ldrb r0, [r5, #5]
    cmp r0, #51 ; '3'
    beq .w32
    mov r1, #0x0100
    b .stwow
.w32:
    mov r1, #0x0200
.stwow:
    adr_sym r0, g_rega32_wow
    str r1, [r0]
    b .prest
.pk:
    adr_sym r0, g_rega32_k1
    ldrb r1, [r0]
    cmp r1, #0
    bne .pk2
    mov r1, r0
    mov r0, r4
    bl ua32_read_tok
    mov r4, r0
    b .prest
.pk2:
    mov r0, r4
    adr_sym r1, g_rega32_k2
    bl ua32_read_tok
    mov r4, r0
    b .prest

.help:
    adr_sym r0, szRa32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #28
    pop {r4, r5, r6, r7, lr}
    bx lr

.disp:
    bl rega32_load_advapi
    cmp r0, #0
    beq .err

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Import
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .do_imp

    adr_sym r0, g_rega32_k1
    ldrb r1, [r0]
    cmp r1, #0
    beq .bad
    bl rega32_parse_hive
    cmp r0, #0
    beq .bad
    adr_sym r1, g_rega32_root
    str r0, [r1]

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Add
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .do_add

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Query
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .do_query

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Delete
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .do_del

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Export
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .do_exp

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Save
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .do_exp

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Copy
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .do_copy

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Compare
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .ok_msg

    adr_sym r0, g_rega32_op
    adr_sym r1, szOa32Flags
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .ok_msg
    b .bad

.do_add:
    adr_sym r0, g_rega32_subkey
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w

    adr_sym r0, g_rega32_root
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    mov r3, #0
    mov r12, #0
    str r12, [sp]
    mov_imm32 r12, 0x20006
    adr_sym r5, g_rega32_wow
    ldr r5, [r5]
    orr r12, r12, r5
    str r12, [sp, #4]
    mov r12, #0
    str r12, [sp, #8]
    adr_sym r12, g_rega32_hkey
    str r12, [sp, #12]
    adr_sym r12, g_rega32_disp
    str r12, [sp, #16]
    adr_sym r5, g_pfna32RCreate
    ldr r5, [r5]
    blx r5
    cmp r0, #0
    bne .err

    adr_sym r0, g_rega32_val
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w

    adr_sym r0, g_rega32_type
    ldr r6, [r0]
    cmp r6, #4
    beq .add_dw
    cmp r6, #11
    beq .add_qw

    adr_sym r0, g_rega32_data
    adr_sym r1, g_ua32_wbuf2
    bl ua32_u8_to_w
    lsl r0, r0, #1
    add r0, r0, #2
    str r0, [sp, #4]
    adr_sym r12, g_ua32_wbuf2
    str r12, [sp]
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    mov r3, r6
    adr_sym r5, g_pfna32RSet
    ldr r5, [r5]
    blx r5
    mov r6, r0
    b .acl

.add_dw:
    adr_sym r5, g_rega32_data
    ldrb r0, [r5]
    cmp r0, #48
    bne .dwd
    ldrb r0, [r5, #1]
    bl ua32_chr_upper
    cmp r0, #88
    bne .dwd
    add r5, r5, #2
    mov r0, #0
.hlp:
    ldrb r1, [r5]
    cmp r1, #0
    beq .stdw
    cmp r1, #97
    blo .hu
    cmp r1, #122
    bhi .hu
    sub r1, r1, #32
.hu:
    cmp r1, #48
    blo .stdw
    cmp r1, #57
    bls .hd
    cmp r1, #65
    blo .stdw
    cmp r1, #70
    bhi .stdw
    sub r1, r1, #55
    b .ha
.hd:
    sub r1, r1, #48
.ha:
    lsl r0, r0, #4
    orr r0, r0, r1
    add r5, r5, #1
    b .hlp
.dwd:
    mov r0, r5
    bl ua32_parse_u32
.stdw:
    adr_sym r1, g_rega32_dw
    str r0, [r1]
    str r1, [sp]
    mov r12, #4
    str r12, [sp, #4]
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    mov r3, #4
    adr_sym r5, g_pfna32RSet
    ldr r5, [r5]
    blx r5
    mov r6, r0
    b .acl

.add_qw:
    adr_sym r0, g_rega32_data
    bl ua32_parse_u32
    adr_sym r1, g_rega32_dw
    str r0, [r1]
    mov r2, #0
    str r2, [r1, #4]
    str r1, [sp]
    mov r12, #8
    str r12, [sp, #4]
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    mov r3, #11
    adr_sym r5, g_pfna32RSet
    ldr r5, [r5]
    blx r5
    mov r6, r0

.acl:
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r5, g_pfna32RClose
    ldr r5, [r5]
    blx r5
    cmp r6, #0
    bne .err
    b .ok_msg

.do_query:
    adr_sym r0, g_rega32_subkey
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w

    adr_sym r0, g_rega32_root
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    mov_imm32 r3, 0x20019
    adr_sym r5, g_rega32_wow
    ldr r5, [r5]
    orr r3, r3, r5
    adr_sym r12, g_rega32_hkey
    str r12, [sp]
    adr_sym r5, g_pfna32ROpen
    ldr r5, [r5]
    blx r5
    cmp r0, #0
    bne .err

    bl ua32_write_crlf
    adr_sym r0, g_rega32_k1
    bl ua32_write_str
    bl ua32_write_crlf

    adr_sym r0, g_rega32_has_v
    ldr r0, [r0]
    cmp r0, #0
    beq .enum_v

    adr_sym r0, g_rega32_val
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w

    adr_sym r0, g_rega32_cb
    mov r1, #4096
    str r1, [r0]
    str r0, [sp, #4]
    adr_sym r0, g_ua32_arena
    ldr r0, [r0]
    str r0, [sp]
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    adr_sym r3, g_rega32_qtype
    adr_sym r5, g_pfna32RQuery
    ldr r5, [r5]
    blx r5
    mov r6, r0
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r5, g_pfna32RClose
    ldr r5, [r5]
    blx r5
    cmp r6, #0
    bne .err
    adr_sym r0, g_rega32_val
    adr_sym r1, g_rega32_qtype
    ldr r1, [r1]
    adr_sym r2, g_ua32_arena
    ldr r2, [r2]
    adr_sym r3, g_rega32_cb
    ldr r3, [r3]
    bl rega32_print_val_row
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #28
    pop {r4, r5, r6, r7, lr}
    bx lr

.enum_v:
    mov r7, #0
.evlp:
    mov r0, #500
    adr_sym r1, g_rega32_cch
    str r0, [r1]
    mov r0, #4096
    adr_sym r2, g_rega32_cb
    str r0, [r2]
    mov r12, #0
    str r12, [sp]
    adr_sym r12, g_rega32_qtype
    str r12, [sp, #4]
    adr_sym r12, g_ua32_arena
    ldr r12, [r12]
    str r12, [sp, #8]
    str r2, [sp, #12]
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    mov r1, r7
    adr_sym r2, g_ua32_wbuf1
    adr_sym r3, g_rega32_cch
    adr_sym r5, g_pfna32REnumVal
    ldr r5, [r5]
    blx r5
    cmp r0, #0
    bne .evdn
    adr_sym r0, g_ua32_wbuf1
    adr_sym r1, g_rega32_val
    mov r2, #500
    bl ua32_w_to_u8
    adr_sym r0, g_rega32_val
    adr_sym r1, g_rega32_qtype
    ldr r1, [r1]
    adr_sym r2, g_ua32_arena
    ldr r2, [r2]
    adr_sym r3, g_rega32_cb
    ldr r3, [r3]
    bl rega32_print_val_row
    add r7, r7, #1
    b .evlp
.evdn:
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r5, g_pfna32RClose
    ldr r5, [r5]
    blx r5
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #28
    pop {r4, r5, r6, r7, lr}
    bx lr

.do_del:
    adr_sym r0, g_rega32_has_v
    ldr r0, [r0]
    cmp r0, #0
    beq .delk
    adr_sym r0, g_rega32_subkey
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_rega32_root
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    mov_imm32 r3, 0x20006
    adr_sym r5, g_rega32_wow
    ldr r5, [r5]
    orr r3, r3, r5
    adr_sym r12, g_rega32_hkey
    str r12, [sp]
    adr_sym r5, g_pfna32ROpen
    ldr r5, [r5]
    blx r5
    cmp r0, #0
    bne .err
    adr_sym r0, g_rega32_val
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    adr_sym r5, g_pfna32RDelVal
    ldr r5, [r5]
    blx r5
    mov r6, r0
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r5, g_pfna32RClose
    ldr r5, [r5]
    blx r5
    cmp r6, #0
    bne .err
    b .ok_msg

.delk:
    adr_sym r0, g_rega32_subkey
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_rega32_root
    ldr r0, [r0]
    adr_sym r1, g_ua32_wbuf1
    adr_sym r5, g_pfna32RDelKey
    ldr r5, [r5]
    blx r5
    cmp r0, #0
    bne .err
    b .ok_msg

.do_exp:
    adr_sym r0, g_rega32_k2
    ldrb r0, [r0]
    cmp r0, #0
    beq .bad
    adr_sym r0, g_ua32_arena
    ldr r6, [r0]
    mov r0, r6
    adr_sym r1, szRa32ExpHdr
    bl ua32_strcpy
    add r7, r6, r0
    mov r0, #91 ; [
    strb r0, [r7]
    add r7, r7, #1
    mov r0, r7
    adr_sym r1, g_rega32_k1
    bl ua32_strcpy
    add r7, r7, r0
    mov r0, #93 ; ]
    strb r0, [r7]
    mov r0, #13
    strb r0, [r7, #1]
    mov r0, #10
    strb r0, [r7, #2]
    add r7, r7, #3
    adr_sym r0, g_rega32_k2
    mov r1, r6
    sub r2, r7, r6
    bl ua32_save_file
    cmp r0, #0
    beq .err
    b .ok_msg

.do_imp:
    adr_sym r0, g_rega32_k1
    ldrb r1, [r0]
    cmp r1, #0
    beq .bad
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 65536
    mov r3, #1
    bl ua32_load_stream
    cmp r1, #0
    bne .err
    b .ok_msg

.do_copy:
    adr_sym r0, g_rega32_k2
    ldrb r1, [r0]
    cmp r1, #0
    beq .bad
    bl rega32_parse_hive
    cmp r0, #0
    beq .bad
    mov r6, r0
    adr_sym r0, g_rega32_subkey
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    mov r0, r6
    adr_sym r1, g_ua32_wbuf1
    mov r2, #0
    mov r3, #0
    mov r12, #0
    str r12, [sp]
    mov_imm32 r12, 0x20006
    str r12, [sp, #4]
    mov r12, #0
    str r12, [sp, #8]
    adr_sym r12, g_rega32_hkey
    str r12, [sp, #12]
    adr_sym r12, g_rega32_disp
    str r12, [sp, #16]
    adr_sym r5, g_pfna32RCreate
    ldr r5, [r5]
    blx r5
    cmp r0, #0
    bne .err
    adr_sym r0, g_rega32_hkey
    ldr r0, [r0]
    adr_sym r5, g_pfna32RClose
    ldr r5, [r5]
    blx r5

.ok_msg:
    adr_sym r0, szRa32Ok
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #28
    pop {r4, r5, r6, r7, lr}
    bx lr
.bad:
    adr_sym r0, szRa32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    add sp, sp, #28
    pop {r4, r5, r6, r7, lr}
    bx lr
.err:
    adr_sym r0, szRa32Err
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    add sp, sp, #28
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

@section rdata

@data db szFna32ROpen, "RegOpenKeyExW", 0
@data db szFna32RCreate, "RegCreateKeyExW", 0
@data db szFna32RSet, "RegSetValueExW", 0
@data db szFna32RQuery, "RegQueryValueExW", 0
@data db szFna32RDelVal, "RegDeleteValueW", 0
@data db szFna32RDelKey, "RegDeleteKeyW", 0
@data db szFna32REnumVal, "RegEnumValueW", 0
@data db szFna32RClose, "RegCloseKey", 0

@data db szHa32Hkcu, "HKCU", 0
@data db szHa32HkcuL, "HKEY_CURRENT_USER", 0
@data db szHa32Hklm, "HKLM", 0
@data db szHa32HklmL, "HKEY_LOCAL_MACHINE", 0
@data db szHa32Hkcr, "HKCR", 0
@data db szHa32HkcrL, "HKEY_CLASSES_ROOT", 0
@data db szHa32Hku, "HKU", 0
@data db szHa32Hkcc, "HKCC", 0

@data db szOa32Query, "QUERY", 0
@data db szOa32Add, "ADD", 0
@data db szOa32Delete, "DELETE", 0
@data db szOa32Copy, "COPY", 0
@data db szOa32Save, "SAVE", 0
@data db szOa32Export, "EXPORT", 0
@data db szOa32Import, "IMPORT", 0
@data db szOa32Compare, "COMPARE", 0
@data db szOa32Flags, "FLAGS", 0

@data db szTa32Sz, "REG_SZ", 0
@data db szTa32Exp, "REG_EXPAND_SZ", 0
@data db szTa32Bin, "REG_BINARY", 0
@data db szTa32Dw, "REG_DWORD", 0
@data db szTa32Msz, "REG_MULTI_SZ", 0
@data db szTa32Qw, "REG_QWORD", 0

@data db szRa32Ind, "    ", 0
@data db szRa32Def, "(Default)", 0
@data db szRa32Hex, "0x", 0
@data db szRa32ExpHdr, "Windows Registry Editor Version 5.00", 13, 10, 13, 10, 0
@data db szRa32Ok, "The operation completed successfully.", 0
@data db szRa32Err, "ERROR: The system was unable to find the specified registry key or value.", 0
@data db szRa32Syn, "ERROR: Invalid syntax.", 0
@data db szRa32Help, "Performs operations on registry subkey information and values (QUERY/ADD/DELETE/EXPORT/IMPORT).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res byte g_rega32_dw, 8
@res dword g_pfna32ROpen, 1
@res dword g_pfna32RCreate, 1
@res dword g_pfna32RSet, 1
@res dword g_pfna32RQuery, 1
@res dword g_pfna32RDelVal, 1
@res dword g_pfna32RDelKey, 1
@res dword g_pfna32REnumVal, 1
@res dword g_pfna32RClose, 1
@res dword g_rega32_root, 1
@res dword g_rega32_hkey, 1
@res dword g_rega32_disp, 1
@res dword g_rega32_type, 1
@res dword g_rega32_qtype, 1
@res dword g_rega32_cb, 1
@res dword g_rega32_cch, 1
@res dword g_rega32_has_v, 1
@res dword g_rega32_wow, 1
@res byte g_rega32_op, 64
@res byte g_rega32_k1, 520
@res byte g_rega32_k2, 520
@res byte g_rega32_subkey, 520
@res byte g_rega32_val, 520
@res byte g_rega32_data, 1024
