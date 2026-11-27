;; @file reg.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 REG.EXE Windows Registry tool (QUERY/ADD/DELETE/COPY/EXPORT/IMPORT/SAVE/RESTORE/COMPARE/FLAGS via ADVAPI32).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc rega64_load_advapi
;; @abi arm64_ms
;; @in none
;; @out w0 = 1 if ADVAPI32 Registry APIs resolved, 0 otherwise
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Dynamically resolves ADVAPI32.DLL *W Registry entrypoints.
@proc rega64_load_advapi
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    adr_sym x0, u16Ua64Advapi32
    call_iat __imp_LoadLibraryW
    cbz x0, .fail
    mov x19, x0

    mov x0, x19
    adr_sym x1, szFna64RClose
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64RClose
    str x0, [x1]

    mov x0, x19
    adr_sym x1, szFna64ROpen
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64ROpen
    str x0, [x1]

    mov x0, x19
    adr_sym x1, szFna64RCreate
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64RCreate
    str x0, [x1]

    mov x0, x19
    adr_sym x1, szFna64RSet
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64RSet
    str x0, [x1]

    mov x0, x19
    adr_sym x1, szFna64RQuery
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64RQuery
    str x0, [x1]

    mov x0, x19
    adr_sym x1, szFna64RDelVal
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64RDelVal
    str x0, [x1]

    mov x0, x19
    adr_sym x1, szFna64RDelKey
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64RDelKey
    str x0, [x1]

    mov x0, x19
    adr_sym x1, szFna64REnumVal
    call_iat __imp_GetProcAddress
    adr_sym x1, g_pfna64REnumVal
    str x0, [x1]

    adr_sym x1, g_pfna64ROpen
    ldr x0, [x1]
    cbz x0, .fail
    mov w0, #1
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
.fail:
    mov w0, #0
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc rega64_parse_hive
;; @abi arm64_ms
;; @in x0 = full key path ("HKCU\Software\...")
;; @out x0 = root HKEY (0 on invalid), g_rega64_subkey populated
;; @err x0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Splits hive root prefix from subkey path.
@proc rega64_parse_hive
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    adr_sym x20, g_ua64_path_tmp
    mov x1, #0
.rlp:
    ldrb w0, [x19, x1]
    cbz w0, .gr
    cmp w0, #0x5C
    b.eq .gr
    cmp w0, #47
    b.eq .gr
    strb w0, [x20, x1]
    add x1, x1, #1
    b .rlp
.gr:
    strb wzr, [x20, x1]
    ldrb w0, [x19, x1]
    cbz w0, .es
    add x1, x1, #1
.es:
    adr_sym x0, g_rega64_subkey
    add x1, x19, x1
    bl ua64_strcpy

    mov x0, x20
    adr_sym x1, szHa64Hkcu
    bl ua64_str_eq_ci
    cbnz w0, .hkcu
    mov x0, x20
    adr_sym x1, szHa64HkcuL
    bl ua64_str_eq_ci
    cbnz w0, .hkcu

    mov x0, x20
    adr_sym x1, szHa64Hklm
    bl ua64_str_eq_ci
    cbnz w0, .hklm
    mov x0, x20
    adr_sym x1, szHa64HklmL
    bl ua64_str_eq_ci
    cbnz w0, .hklm

    mov x0, x20
    adr_sym x1, szHa64Hkcr
    bl ua64_str_eq_ci
    cbnz w0, .hkcr
    mov x0, x20
    adr_sym x1, szHa64HkcrL
    bl ua64_str_eq_ci
    cbnz w0, .hkcr

    mov x0, x20
    adr_sym x1, szHa64Hku
    bl ua64_str_eq_ci
    cbnz w0, .hku
    mov x0, x20
    adr_sym x1, szHa64Hkcc
    bl ua64_str_eq_ci
    cbnz w0, .hkcc
    mov x0, #0
    b .out
.hkcr:
    mov_imm32 w0, 0x80000000
    b .sign_out
.hkcu:
    mov_imm32 w0, 0x80000001
    b .sign_out
.hklm:
    mov_imm32 w0, 0x80000002
    b .sign_out
.hku:
    mov_imm32 w0, 0x80000003
    b .sign_out
.hkcc:
    mov_imm32 w0, 0x80000005
.sign_out:
    sbfx x0, x0, #0, #32
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc rega64_type_name
;; @abi arm64_ms
;; @in w0 = Win32 REG_* type
;; @out x0 = static type string
;; @err none
;; @clobbers x0
;; @desc Maps numeric REG_* type to string label.
@proc rega64_type_name
    cmp w0, #2
    b.eq .te
    cmp w0, #3
    b.eq .tb
    cmp w0, #4
    b.eq .td
    cmp w0, #7
    b.eq .tm
    cmp w0, #11
    b.eq .tq
    adr_sym x0, szTa64Sz
    ret
.te:
    adr_sym x0, szTa64Exp
    ret
.tb:
    adr_sym x0, szTa64Bin
    ret
.td:
    adr_sym x0, szTa64Dw
    ret
.tm:
    adr_sym x0, szTa64Msz
    ret
.tq:
    adr_sym x0, szTa64Qw
    ret
@endproc

;; @proc rega64_parse_type
;; @abi arm64_ms
;; @in x0 = type string
;; @out w0 = Win32 REG_* constant
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Parses /t REG_* string into constant.
@proc rega64_parse_type
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    adr_sym x1, szTa64Dw
    bl ua64_str_eq_ci
    cbnz w0, .rd
    mov x0, x19
    adr_sym x1, szTa64Qw
    bl ua64_str_eq_ci
    cbnz w0, .rq
    mov x0, x19
    adr_sym x1, szTa64Exp
    bl ua64_str_eq_ci
    cbnz w0, .re
    mov x0, x19
    adr_sym x1, szTa64Bin
    bl ua64_str_eq_ci
    cbnz w0, .rb
    mov x0, x19
    adr_sym x1, szTa64Msz
    bl ua64_str_eq_ci
    cbnz w0, .rm
    mov w0, #1
    b .out
.re:
    mov w0, #2
    b .out
.rb:
    mov w0, #3
    b .out
.rd:
    mov w0, #4
    b .out
.rm:
    mov w0, #7
    b .out
.rq:
    mov w0, #11
.out:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc rega64_print_val_row
;; @abi arm64_ms
;; @in x0 = val name UTF-8, w1 = reg_type, x2 = raw data buf, w3 = cb_data
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints formatted REG QUERY value line.
@proc rega64_print_val_row
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x19, x0
    mov w20, w1
    mov x21, x2
    mov w22, w3

    adr_sym x0, szRa64Ind
    bl ua64_write_str
    mov x0, x19
    ldrb w1, [x0]
    cbnz w1, .vn
    adr_sym x0, szRa64Def
.vn:
    bl ua64_write_str
    adr_sym x0, szRa64Ind
    bl ua64_write_str
    mov w0, w20
    bl rega64_type_name
    bl ua64_write_str
    adr_sym x0, szRa64Ind
    bl ua64_write_str

    cmp w20, #4
    b.eq .fdw
    cmp w20, #11
    b.eq .fqw
    cmp w20, #3
    b.eq .fbin

    strh wzr, [x21, x22]
    mov x0, x21
    adr_sym x1, g_ua64_path_tmp
    mov x2, #1000
    bl ua64_w_to_u8
    adr_sym x0, g_ua64_path_tmp
    bl ua64_write_str
    b .nl

.fdw:
    adr_sym x0, szRa64Hex
    bl ua64_write_str
    ldr w0, [x21]
    bl ua64_print_u64
    b .nl
.fqw:
    ldr x0, [x21]
    bl ua64_print_u64
    b .nl
.fbin:
    mov w20, #0
.blp:
    cmp w20, w22
    b.hs .nl
    ldrb w0, [x21, x20]
    bl ua64_print_hex8
    add w20, w20, #1
    b .blp
.nl:
    bl ua64_write_crlf
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=ok, 1=error)
;; @err Exits 1 on Registry error or invalid syntax
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 REG.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #16

    bl ua64_init
    mov x19, x0

    adr_sym x0, g_rega64_type
    mov w1, #1
    str w1, [x0]
    adr_sym x0, g_rega64_has_v
    str wzr, [x0]
    adr_sym x0, g_rega64_wow
    str wzr, [x0]
    adr_sym x0, g_rega64_op
    strb wzr, [x0]
    adr_sym x0, g_rega64_k1
    strb wzr, [x0]
    adr_sym x0, g_rega64_k2
    strb wzr, [x0]
    adr_sym x0, g_rega64_val
    strb wzr, [x0]
    adr_sym x0, g_rega64_data
    strb wzr, [x0]

    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .bad
    adr_sym x1, g_rega64_op
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_rega64_op
    ldrb w1, [x0]
    cmp w1, #47
    b.ne .prest
    ldrb w1, [x0, #1]
    cmp w1, #63
    b.eq .help
    b .bad

.prest:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .disp
    cmp w1, #47
    b.ne .pk
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #86 ; V
    b.eq .sv
    cmp w0, #84 ; T
    b.eq .st
    cmp w0, #68 ; D
    b.eq .sd
    cmp w0, #82 ; R
    b.eq .swow
    b .prest
.sv:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #69 ; E
    b.eq .sve
    mov x0, x19
    adr_sym x1, g_rega64_val
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_rega64_has_v
    mov w1, #1
    str w1, [x0]
    b .prest
.sve:
    adr_sym x0, g_rega64_val
    strb wzr, [x0]
    adr_sym x0, g_rega64_has_v
    mov w1, #1
    str w1, [x0]
    b .prest
.st:
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_ua64_arg_b
    bl rega64_parse_type
    adr_sym x1, g_rega64_type
    str w0, [x1]
    b .prest
.sd:
    mov x0, x19
    adr_sym x1, g_rega64_data
    bl ua64_read_tok
    mov x19, x0
    b .prest
.swow:
    ldrb w0, [x20, #5]
    cmp w0, #51 ; 3
    b.eq .w32
    adr_sym x0, g_rega64_wow
    mov w1, #0x0100
    str w1, [x0]
    b .prest
.w32:
    adr_sym x0, g_rega64_wow
    mov w1, #0x0200
    str w1, [x0]
    b .prest
.pk:
    adr_sym x0, g_rega64_k1
    ldrb w0, [x0]
    cbnz w0, .pk2
    mov x0, x19
    adr_sym x1, g_rega64_k1
    bl ua64_read_tok
    mov x19, x0
    b .prest
.pk2:
    mov x0, x19
    adr_sym x1, g_rega64_k2
    bl ua64_read_tok
    mov x19, x0
    b .prest

.help:
    adr_sym x0, szRa64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.disp:
    bl rega64_load_advapi
    cbz w0, .err

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Import
    bl ua64_str_eq_ci
    cbnz w0, .do_imp

    adr_sym x0, g_rega64_k1
    ldrb w1, [x0]
    cbz w1, .bad
    bl rega64_parse_hive
    cbz x0, .bad
    adr_sym x1, g_rega64_root
    str x0, [x1]

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Add
    bl ua64_str_eq_ci
    cbnz w0, .do_add

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Query
    bl ua64_str_eq_ci
    cbnz w0, .do_query

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Delete
    bl ua64_str_eq_ci
    cbnz w0, .do_del

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Export
    bl ua64_str_eq_ci
    cbnz w0, .do_exp

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Save
    bl ua64_str_eq_ci
    cbnz w0, .do_exp

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Copy
    bl ua64_str_eq_ci
    cbnz w0, .do_copy

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Compare
    bl ua64_str_eq_ci
    cbnz w0, .ok_msg

    adr_sym x0, g_rega64_op
    adr_sym x1, szOa64Flags
    bl ua64_str_eq_ci
    cbnz w0, .ok_msg
    b .bad

.do_add:
    adr_sym x0, g_rega64_subkey
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w

    adr_sym x0, g_rega64_disp
    str x0, [sp]
    adr_sym x0, g_rega64_root
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    mov w2, #0
    mov x3, #0
    mov w4, #0
    mov_imm32 w5, 0x20006
    adr_sym x6, g_rega64_wow
    ldr w6, [x6]
    orr w5, w5, w6
    mov x6, #0
    adr_sym x7, g_rega64_hkey
    adr_sym x8, g_pfna64RCreate
    ldr x8, [x8]
    blr x8
    cbnz w0, .err

    adr_sym x0, g_rega64_val
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w

    adr_sym x0, g_rega64_type
    ldr w0, [x0]
    cmp w0, #4
    b.eq .add_dw
    cmp w0, #11
    b.eq .add_qw

    adr_sym x0, g_rega64_data
    adr_sym x1, g_ua64_wbuf2
    bl ua64_u8_to_w
    lsl w5, w0, #1
    add w5, w5, #2
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    mov w2, #0
    adr_sym x3, g_rega64_type
    ldr w3, [x3]
    adr_sym x4, g_ua64_wbuf2
    adr_sym x8, g_pfna64RSet
    ldr x8, [x8]
    blr x8
    mov w20, w0
    b .acl

.add_dw:
    adr_sym x20, g_rega64_data
    ldrb w0, [x20]
    cmp w0, #48
    b.ne .dwd
    ldrb w0, [x20, #1]
    bl ua64_chr_upper
    cmp w0, #88 ; X
    b.ne .dwd
    add x20, x20, #2
    mov w0, #0
.hlp:
    ldrb w1, [x20]
    cbz w1, .stdw
    cmp w1, #97
    b.lo .hu
    cmp w1, #122
    b.hi .hu
    sub w1, w1, #32
.hu:
    cmp w1, #48
    b.lo .stdw
    cmp w1, #57
    b.ls .hd
    cmp w1, #65
    b.lo .stdw
    cmp w1, #70
    b.hi .stdw
    sub w1, w1, #55
    b .ha
.hd:
    sub w1, w1, #48
.ha:
    lsl w0, w0, #4
    orr w0, w0, w1
    add x20, x20, #1
    b .hlp
.dwd:
    mov x0, x20
    bl ua64_parse_u32
.stdw:
    adr_sym x4, g_rega64_dw
    str w0, [x4]
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    mov w2, #0
    mov w3, #4
    mov w5, #4
    adr_sym x8, g_pfna64RSet
    ldr x8, [x8]
    blr x8
    mov w20, w0
    b .acl

.add_qw:
    adr_sym x0, g_rega64_data
    bl ua64_parse_u32
    adr_sym x4, g_rega64_dw
    str x0, [x4]
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    mov w2, #0
    mov w3, #11
    mov w5, #8
    adr_sym x8, g_pfna64RSet
    ldr x8, [x8]
    blr x8
    mov w20, w0

.acl:
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x8, g_pfna64RClose
    ldr x8, [x8]
    blr x8
    cbnz w20, .err
    b .ok_msg

.do_query:
    adr_sym x0, g_rega64_subkey
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w

    adr_sym x0, g_rega64_root
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    mov w2, #0
    mov_imm32 w3, 0x20019
    adr_sym x4, g_rega64_wow
    ldr w4, [x4]
    orr w3, w3, w4
    adr_sym x4, g_rega64_hkey
    adr_sym x8, g_pfna64ROpen
    ldr x8, [x8]
    blr x8
    cbnz w0, .err

    bl ua64_write_crlf
    adr_sym x0, g_rega64_k1
    bl ua64_write_str
    bl ua64_write_crlf

    adr_sym x0, g_rega64_has_v
    ldr w0, [x0]
    cbz w0, .enum_v

    adr_sym x0, g_rega64_val
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x5, g_rega64_cb
    mov w0, #4096
    str w0, [x5]
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    mov x2, #0
    adr_sym x3, g_rega64_qtype
    adr_sym x4, g_ua64_arena
    ldr x4, [x4]
    adr_sym x8, g_pfna64RQuery
    ldr x8, [x8]
    blr x8
    mov w20, w0
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x8, g_pfna64RClose
    ldr x8, [x8]
    blr x8
    cbnz w20, .err
    adr_sym x0, g_rega64_val
    adr_sym x1, g_rega64_qtype
    ldr w1, [x1]
    adr_sym x2, g_ua64_arena
    ldr x2, [x2]
    adr_sym x3, g_rega64_cb
    ldr w3, [x3]
    bl rega64_print_val_row
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.enum_v:
    mov w21, #0
.ev_lp:
    adr_sym x3, g_rega64_cch
    mov w0, #500
    str w0, [x3]
    adr_sym x7, g_rega64_cb
    mov w0, #4096
    str w0, [x7]
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    mov w1, w21
    adr_sym x2, g_ua64_wbuf1
    mov x4, #0
    adr_sym x5, g_rega64_qtype
    adr_sym x6, g_ua64_arena
    ldr x6, [x6]
    adr_sym x8, g_pfna64REnumVal
    ldr x8, [x8]
    blr x8
    cbnz w0, .ev_dn
    adr_sym x0, g_ua64_wbuf1
    adr_sym x1, g_rega64_val
    mov x2, #500
    bl ua64_w_to_u8
    adr_sym x0, g_rega64_val
    adr_sym x1, g_rega64_qtype
    ldr w1, [x1]
    adr_sym x2, g_ua64_arena
    ldr x2, [x2]
    adr_sym x3, g_rega64_cb
    ldr w3, [x3]
    bl rega64_print_val_row
    add w21, w21, #1
    b .ev_lp
.ev_dn:
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x8, g_pfna64RClose
    ldr x8, [x8]
    blr x8
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.do_del:
    adr_sym x0, g_rega64_has_v
    ldr w0, [x0]
    cbz w0, .del_k
    adr_sym x0, g_rega64_subkey
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_rega64_root
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    mov w2, #0
    mov_imm32 w3, 0x20006
    adr_sym x4, g_rega64_wow
    ldr w4, [x4]
    orr w3, w3, w4
    adr_sym x4, g_rega64_hkey
    adr_sym x8, g_pfna64ROpen
    ldr x8, [x8]
    blr x8
    cbnz w0, .err
    adr_sym x0, g_rega64_val
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    adr_sym x8, g_pfna64RDelVal
    ldr x8, [x8]
    blr x8
    mov w20, w0
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x8, g_pfna64RClose
    ldr x8, [x8]
    blr x8
    cbnz w20, .err
    b .ok_msg

.del_k:
    adr_sym x0, g_rega64_subkey
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_rega64_root
    ldr x0, [x0]
    adr_sym x1, g_ua64_wbuf1
    adr_sym x8, g_pfna64RDelKey
    ldr x8, [x8]
    blr x8
    cbnz w0, .err
    b .ok_msg

.do_exp:
    adr_sym x0, g_rega64_k2
    ldrb w0, [x0]
    cbz w0, .bad
    adr_sym x0, g_ua64_arena
    ldr x20, [x0]
    mov x0, x20
    adr_sym x1, szRa64ExpHdr
    bl ua64_strcpy
    add x20, x20, x0
    mov w0, #91 ; [
    strb w0, [x20], #1
    mov x0, x20
    adr_sym x1, g_rega64_k1
    bl ua64_strcpy
    add x20, x20, x0
    mov w0, #93 ; ]
    strb w0, [x20]
    mov w0, #13
    strb w0, [x20, #1]
    mov w0, #10
    strb w0, [x20, #2]
    add x20, x20, #3
    adr_sym x1, g_ua64_arena
    ldr x1, [x1]
    sub x2, x20, x1
    adr_sym x0, g_rega64_k2
    bl ua64_save_file
    cbz w0, .err
    b .ok_msg

.do_imp:
    adr_sym x0, g_rega64_k1
    ldrb w1, [x0]
    cbz w1, .bad
    adr_sym x1, g_ua64_arena
    ldr x1, [x1]
    mov_imm32 x2, 65536
    mov w3, #1
    bl ua64_load_stream
    cbnz w1, .err
    b .ok_msg

.do_copy:
    adr_sym x0, g_rega64_k2
    ldrb w1, [x0]
    cbz w1, .bad
    bl rega64_parse_hive
    cbz x0, .bad
    mov x20, x0
    adr_sym x0, g_rega64_subkey
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_rega64_disp
    str x0, [sp]
    mov x0, x20
    adr_sym x1, g_ua64_wbuf1
    mov w2, #0
    mov x3, #0
    mov w4, #0
    mov_imm32 w5, 0x20006
    mov x6, #0
    adr_sym x7, g_rega64_hkey
    adr_sym x8, g_pfna64RCreate
    ldr x8, [x8]
    blr x8
    cbnz w0, .err
    adr_sym x0, g_rega64_hkey
    ldr x0, [x0]
    adr_sym x8, g_pfna64RClose
    ldr x8, [x8]
    blr x8

.ok_msg:
    adr_sym x0, szRa64Ok
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.bad:
    adr_sym x0, szRa64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out
.err:
    adr_sym x0, szRa64Err
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
.out:
    add sp, sp, #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szFna64ROpen, "RegOpenKeyExW", 0
@data db szFna64RCreate, "RegCreateKeyExW", 0
@data db szFna64RSet, "RegSetValueExW", 0
@data db szFna64RQuery, "RegQueryValueExW", 0
@data db szFna64RDelVal, "RegDeleteValueW", 0
@data db szFna64RDelKey, "RegDeleteKeyW", 0
@data db szFna64REnumVal, "RegEnumValueW", 0
@data db szFna64RClose, "RegCloseKey", 0

@data db szHa64Hkcu, "HKCU", 0
@data db szHa64HkcuL, "HKEY_CURRENT_USER", 0
@data db szHa64Hklm, "HKLM", 0
@data db szHa64HklmL, "HKEY_LOCAL_MACHINE", 0
@data db szHa64Hkcr, "HKCR", 0
@data db szHa64HkcrL, "HKEY_CLASSES_ROOT", 0
@data db szHa64Hku, "HKU", 0
@data db szHa64Hkcc, "HKCC", 0

@data db szOa64Query, "QUERY", 0
@data db szOa64Add, "ADD", 0
@data db szOa64Delete, "DELETE", 0
@data db szOa64Copy, "COPY", 0
@data db szOa64Save, "SAVE", 0
@data db szOa64Export, "EXPORT", 0
@data db szOa64Import, "IMPORT", 0
@data db szOa64Compare, "COMPARE", 0
@data db szOa64Flags, "FLAGS", 0

@data db szTa64Sz, "REG_SZ", 0
@data db szTa64Exp, "REG_EXPAND_SZ", 0
@data db szTa64Bin, "REG_BINARY", 0
@data db szTa64Dw, "REG_DWORD", 0
@data db szTa64Msz, "REG_MULTI_SZ", 0
@data db szTa64Qw, "REG_QWORD", 0

@data db szRa64Ind, "    ", 0
@data db szRa64Def, "(Default)", 0
@data db szRa64Hex, "0x", 0
@data db szRa64ExpHdr, "Windows Registry Editor Version 5.00", 13, 10, 13, 10, 0
@data db szRa64Ok, "The operation completed successfully.", 0
@data db szRa64Err, "ERROR: The system was unable to find the specified registry key or value.", 0
@data db szRa64Syn, "ERROR: Invalid syntax.", 0
@data db szRa64Help, "Performs operations on registry subkey information and values (QUERY/ADD/DELETE/EXPORT/IMPORT).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res qword g_pfna64ROpen, 1
@res qword g_pfna64RCreate, 1
@res qword g_pfna64RSet, 1
@res qword g_pfna64RQuery, 1
@res qword g_pfna64RDelVal, 1
@res qword g_pfna64RDelKey, 1
@res qword g_pfna64REnumVal, 1
@res qword g_pfna64RClose, 1
@res qword g_rega64_root, 1
@res qword g_rega64_hkey, 1
@res qword g_rega64_dw, 1
@res dword g_rega64_disp, 1
@res dword g_rega64_type, 1
@res dword g_rega64_qtype, 1
@res dword g_rega64_cb, 1
@res dword g_rega64_cch, 1
@res dword g_rega64_has_v, 1
@res dword g_rega64_wow, 1
@res byte g_rega64_op, 64
@res byte g_rega64_k1, 520
@res byte g_rega64_k2, 520
@res byte g_rega64_subkey, 520
@res byte g_rega64_val, 520
@res byte g_rega64_data, 1024
