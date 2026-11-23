;; @file choice.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 CHOICE.EXE prompt utility (/C[:]choices, /N, /CS, /T nn /D c, /M msg).

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc choicea64_match
;; @abi arm64_ms
;; @in w0 = input char
;; @out w0 = 1-based index in g_cha64_list, or 0 if not matched
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4
;; @desc Matches w0 against choice list honoring /CS.
@proc choicea64_match
    and w0, w0, #0xFF
    adr_sym x1, g_cha64_cs
    ldr w1, [x1]
    cbnz w1, .sc
    cmp w0, #97
    b.lo .sc
    cmp w0, #122
    b.hi .sc
    sub w0, w0, #32
.sc:
    adr_sym x2, g_cha64_list
    mov x3, #0
.lp:
    ldrb w4, [x2, x3]
    cbz w4, .no
    cbnz w1, .cc
    cmp w4, #97
    b.lo .cc
    cmp w4, #122
    b.hi .cc
    sub w4, w4, #32
.cc:
    cmp w4, w0
    b.eq .hit
    add x3, x3, #1
    b .lp
.hit:
    add w0, w3, #1
    ret
.no:
    mov w0, #0
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (1..N or 255)
;; @err Exits 255 on syntax error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 CHOICE.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_cha64_hide
    str wzr, [x0]
    adr_sym x0, g_cha64_cs
    str wzr, [x0]
    adr_sym x0, g_cha64_has_to
    str wzr, [x0]
    adr_sym x0, g_cha64_to
    str wzr, [x0]
    adr_sym x0, g_cha64_def
    strb wzr, [x0]
    adr_sym x0, g_cha64_msg
    strb wzr, [x0]
    adr_sym x0, g_cha64_list
    mov w1, #89 ; Y
    strb w1, [x0]
    mov w1, #78 ; N
    strb w1, [x0, #1]
    strb wzr, [x0, #2]

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .prompt
    cmp w1, #47
    b.ne .mpos
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #78 ; N
    b.eq .sn
    cmp w0, #67 ; C
    b.eq .sc
    cmp w0, #84 ; T
    b.eq .st
    cmp w0, #68 ; D
    b.eq .sd
    cmp w0, #77 ; M
    b.eq .sm
    b .bad
.sn:
    adr_sym x0, g_cha64_hide
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sc:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #83
    b.ne .scl
    ldrb w0, [x20, #3]
    cbnz w0, .scl
    adr_sym x0, g_cha64_cs
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.scl:
    add x1, x20, #2
    ldrb w0, [x1]
    cmp w0, #58
    b.ne .sci
    add x1, x1, #1
.sci:
    ldrb w0, [x1]
    cbz w0, .scn
    adr_sym x0, g_cha64_list
    bl ua64_strcpy
    b .arg_lp
.scn:
    mov x0, x19
    adr_sym x1, g_cha64_list
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.st:
    ldrb w0, [x20, #2]
    cmp w0, #58
    b.ne .ntt
    ldrb w0, [x20, #3]
    adr_sym x1, g_cha64_def
    strb w0, [x1]
    adr_sym x1, g_cha64_has_to
    mov w0, #1
    str w0, [x1]
    ldrb w0, [x20, #4]
    cmp w0, #44
    b.ne .arg_lp
    add x0, x20, #5
    bl ua64_parse_u32
    adr_sym x1, g_cha64_to
    str w0, [x1]
    b .arg_lp
.ntt:
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    adr_sym x0, g_ua64_arg_b
    bl ua64_parse_u32
    adr_sym x1, g_cha64_to
    str w0, [x1]
    adr_sym x1, g_cha64_has_to
    mov w0, #1
    str w0, [x1]
    b .arg_lp
.sd:
    ldrb w0, [x20, #2]
    cmp w0, #58
    b.ne .ntd
    ldrb w0, [x20, #3]
    adr_sym x1, g_cha64_def
    strb w0, [x1]
    b .arg_lp
.ntd:
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    adr_sym x1, g_ua64_arg_b
    ldrb w0, [x1]
    adr_sym x1, g_cha64_def
    strb w0, [x1]
    b .arg_lp
.sm:
    mov x0, x19
    adr_sym x1, g_cha64_msg
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.mpos:
    adr_sym x1, g_cha64_msg
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp

.help:
    adr_sym x0, szCha64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.bad:
    adr_sym x0, szCha64Syn
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #255
    bl ua64_exit
    b .out

.prompt:
    adr_sym x20, g_cha64_list
    ldrb w0, [x20]
    cbz w0, .bad
    adr_sym x0, g_cha64_msg
    ldrb w1, [x0]
    cbz w1, .cbr
    bl ua64_write_str
    mov w0, #32
    bl ua64_write_chr
.cbr:
    adr_sym x0, g_cha64_hide
    ldr w0, [x0]
    cbnz w0, .win
    mov w0, #91 ; [
    bl ua64_write_chr
    mov x21, #0
.prc:
    ldrb w0, [x20, x21]
    cbz w0, .ebr
    cbz x21, .emc
    mov w0, #44
    bl ua64_write_chr
    ldrb w0, [x20, x21]
.emc:
    adr_sym x1, g_cha64_cs
    ldr w1, [x1]
    cbnz w1, .rwc
    bl ua64_chr_upper
.rwc:
    bl ua64_write_chr
    add x21, x21, #1
    b .prc
.ebr:
    adr_sym x0, szCha64Q
    bl ua64_write_str

.win:
    adr_sym x0, g_cha64_has_to
    ldr w0, [x0]
    cbz w0, .rd
    adr_sym x0, g_cha64_to
    ldr w0, [x0]
    cbz w0, .udef
.rd:
    adr_sym x1, g_cha64_in
    strb wzr, [x1]
    adr_sym x0, g_ua64_stdin
    ldr x0, [x0]
    mov w2, #1
    adr_sym x3, g_ua64_io_done
    mov x4, #0
    call_iat __imp_ReadFile
    cbz w0, .eof
    adr_sym x0, g_ua64_io_done
    ldr w0, [x0]
    cbz w0, .eof
    adr_sym x0, g_cha64_in
    ldrb w20, [x0]
    cmp w20, #13
    b.eq .rd
    cmp w20, #10
    b.eq .rd
    mov w0, w20
    bl choicea64_match
    cbz w0, .rd
    mov w21, w0
    mov w0, w20
    bl ua64_write_chr
    bl ua64_write_crlf
    mov w0, w21
    bl ua64_exit
    b .out

.eof:
    adr_sym x0, g_cha64_def
    ldrb w0, [x0]
    cbnz w0, .udef
    mov w0, #255
    bl ua64_exit
    b .out
.udef:
    adr_sym x0, g_cha64_def
    ldrb w20, [x0]
    mov w0, w20
    bl choicea64_match
    cbz w0, .bad
    mov w21, w0
    mov w0, w20
    bl ua64_write_chr
    bl ua64_write_crlf
    mov w0, w21
    bl ua64_exit
.out:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szCha64Q, "]?", 0
@data db szCha64Syn, "ERROR: Invalid choice syntax.", 0
@data db szCha64Help, "Prompts the user to select one item from a list of choices (/C /N /CS /T /D /M).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res dword g_cha64_hide, 1
@res dword g_cha64_cs, 1
@res dword g_cha64_has_to, 1
@res dword g_cha64_to, 1
@res byte g_cha64_def, 4
@res byte g_cha64_in, 4
@res byte g_cha64_list, 128
@res byte g_cha64_msg, 520
