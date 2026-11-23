;; @file sort.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 SORT.EXE utility (/R, /+n, /M, /L, /REC, /T, /O out, /UNIQUE) with O(N log N) merge sort.

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc sorta64_cmp_rec
;; @abi arm64_ms
;; @in x0 = ptr to 16B record A (0:ptr64, 8:len64), x1 = ptr to 16B record B
;; @out w0 = -1 if A < B, 0 if A == B, 1 if A > B
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Case-insensitive collate comparison starting at g_sorta64_col.
@proc sorta64_cmp_rec
    ldr x2, [x0]
    ldr x3, [x0, #8]
    ldr x4, [x1]
    ldr x5, [x1, #8]

    adr_sym x0, g_sorta64_col
    ldr w0, [x0]
    cmp x3, x0
    b.hi .aa
    mov x3, #0
    b .cb
.aa:
    add x2, x2, x0
    sub x3, x3, x0
.cb:
    cmp x5, x0
    b.hi .ab
    mov x5, #0
    b .lp
.ab:
    add x4, x4, x0
    sub x5, x5, x0

.lp:
    cbz x3, .aend
    cbz x5, .gt
    ldrb w6, [x2]
    cmp w6, #97
    b.lo .u1
    cmp w6, #122
    b.hi .u1
    sub w6, w6, #32
.u1:
    ldrb w7, [x4]
    cmp w7, #97
    b.lo .u2
    cmp w7, #122
    b.hi .u2
    sub w7, w7, #32
.u2:
    cmp w6, w7
    b.lo .lt
    b.hi .gt
    add x2, x2, #1
    add x4, x4, #1
    sub x3, x3, #1
    sub x5, x5, #1
    b .lp
.aend:
    cbz x5, .eq
.lt:
    mov w0, #-1
    b .rev
.gt:
    mov w0, #1
.rev:
    adr_sym x1, g_sorta64_rev
    ldr w1, [x1]
    cbz w1, .dn
    neg w0, w0
    ret
.eq:
    mov w0, #0
.dn:
    ret
@endproc

;; @proc sorta64_merge_run
;; @abi arm64_ms
;; @in x0 = src array, x1 = dst array, x2 = left, x3 = mid, x4 = right
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Merges sorted runs [left..mid) and [mid..right).
@proc sorta64_merge_run
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!

    mov x19, x0 ; src
    mov x20, x1 ; dst
    mov x21, x2 ; i = left
    mov x22, x3 ; j = mid
    mov x23, x3 ; mid
    mov x24, x4 ; right
    mov x25, x2 ; k = left

.mlp:
    cmp x21, x23
    b.hs .rrem
    cmp x22, x24
    b.hs .lrem

    add x0, x19, x21, lsl #4
    add x1, x19, x22, lsl #4
    bl sorta64_cmp_rec
    cmp w0, #0
    b.gt .tr

    add x0, x19, x21, lsl #4
    add x1, x20, x25, lsl #4
    ldp x2, x3, [x0]
    stp x2, x3, [x1]
    add x21, x21, #1
    add x25, x25, #1
    b .mlp
.tr:
    add x0, x19, x22, lsl #4
    add x1, x20, x25, lsl #4
    ldp x2, x3, [x0]
    stp x2, x3, [x1]
    add x22, x22, #1
    add x25, x25, #1
    b .mlp

.lrem:
    cmp x21, x23
    b.hs .dn
    add x0, x19, x21, lsl #4
    add x1, x20, x25, lsl #4
    ldp x2, x3, [x0]
    stp x2, x3, [x1]
    add x21, x21, #1
    add x25, x25, #1
    b .lrem
.rrem:
    cmp x22, x24
    b.hs .dn
    add x0, x19, x22, lsl #4
    add x1, x20, x25, lsl #4
    ldp x2, x3, [x0]
    stp x2, x3, [x1]
    add x22, x22, #1
    add x25, x25, #1
    b .rrem
.dn:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on missing file
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 SORT.EXE.
@proc mainCRTStartup
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!

    bl ua64_init
    mov x19, x0
    adr_sym x0, g_sorta64_rev
    str wzr, [x0]
    adr_sym x0, g_sorta64_col
    str wzr, [x0]
    adr_sym x0, g_sorta64_uniq
    str wzr, [x0]
    adr_sym x0, g_ua64_arg_b
    strb wzr, [x0]
    adr_sym x0, g_ua64_arg_c
    strb wzr, [x0]

.sw_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .ld
    cmp w1, #47
    b.ne .inf
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    cmp w0, #43 ; +
    b.eq .scol
    bl ua64_chr_upper
    cmp w0, #82 ; R
    b.eq .sr
    cmp w0, #85 ; U
    b.eq .su
    cmp w0, #79 ; O
    b.eq .so
    cmp w0, #77 ; M
    b.eq .skv
    cmp w0, #76 ; L
    b.eq .skv
    cmp w0, #84 ; T
    b.eq .skv
    b .sw_lp
.sr:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #69
    b.eq .skv
    adr_sym x0, g_sorta64_rev
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.su:
    adr_sym x0, g_sorta64_uniq
    mov w1, #1
    str w1, [x0]
    b .sw_lp
.scol:
    add x0, x20, #2
    bl ua64_parse_u32
    cbz w0, .sw_lp
    sub w0, w0, #1
    adr_sym x1, g_sorta64_col
    str w0, [x1]
    b .sw_lp
.so:
    mov x0, x19
    adr_sym x1, g_ua64_arg_c
    bl ua64_read_tok
    mov x19, x0
    b .sw_lp
.skv:
    mov x0, x19
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    b .sw_lp
.inf:
    mov x0, x19
    adr_sym x1, g_ua64_arg_b
    bl ua64_read_tok
    mov x19, x0
    b .sw_lp

.help:
    adr_sym x0, szSorta64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.ld:
    adr_sym x0, g_ua64_arg_b
    adr_sym x4, g_ua64_arena
    ldr x1, [x4]
    mov_imm32 x2, 260000
    mov w3, #1
    bl ua64_load_stream
    cbz w1, .split
    adr_sym x0, szSorta64Nf
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #1
    bl ua64_exit
    b .out

.split:
    adr_sym x4, g_ua64_arena
    ldr x19, [x4] ; cur
    add x20, x19, x0 ; end
    mov_imm32 x4, 262144
    add x21, x19, x4 ; rec A
    mov x22, #0 ; count

.splp:
    cmp x19, x20
    b.hs .dosort
    ldrb w0, [x19]
    cmp w0, #0x1A
    b.eq .dosort
    mov_imm32 x4, 8190
    cmp x22, x4
    b.hs .dosort
    mov x23, x19
.eol:
    cmp x23, x20
    b.hs .gl
    ldrb w0, [x23]
    cmp w0, #13
    b.eq .gl
    cmp w0, #10
    b.eq .gl
    add x23, x23, #1
    b .eol
.gl:
    add x0, x21, x22, lsl #4
    sub x1, x23, x19
    stp x19, x1, [x0]
    add x22, x22, #1
    cmp x23, x20
    b.hs .dosort
    ldrb w0, [x23]
    cmp w0, #13
    b.ne .clf
    add x23, x23, #1
.clf:
    cmp x23, x20
    b.hs .nxs
    ldrb w0, [x23]
    cmp w0, #10
    b.ne .nxs
    add x23, x23, #1
.nxs:
    mov x19, x23
    b .splp

.dosort:
    adr_sym x0, g_ua64_arena
    ldr x0, [x0]
    mov_imm32 x4, 262144
    add x19, x0, x4 ; src
    mov_imm32 x4, 393216
    add x20, x0, x4 ; dst
    cmp x22, #2
    b.lo .emit

    mov x21, #1 ; width
.wlp:
    cmp x21, x22
    b.hs .emit
    mov x23, #0 ; left
.rlp:
    cmp x23, x22
    b.hs .swp
    add x3, x23, x21
    cmp x3, x22
    b.ls .gm
    mov x3, x22
.gm:
    add x4, x3, x21
    cmp x4, x22
    b.ls .gr
    mov x4, x22
.gr:
    mov x0, x19
    mov x1, x20
    mov x2, x23
    bl sorta64_merge_run
    add x23, x23, x21, lsl #1
    b .rlp
.swp:
    mov x0, x19
    mov x19, x20
    mov x20, x0
    lsl x21, x21, #1
    b .wlp

.emit:
    adr_sym x0, g_sorta64_outh
    str xzr, [x0]
    adr_sym x0, g_ua64_arg_c
    ldrb w1, [x0]
    cbz w1, .winit
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    mov_imm32 w1, 0x40000000
    mov w2, #0
    mov x3, #0
    mov w4, #2
    mov w5, #0x80
    mov x6, #0
    call_iat __imp_CreateFileW
    cmn x0, #1
    b.eq .winit
    adr_sym x1, g_sorta64_outh
    str x0, [x1]

.winit:
    mov x21, #0
.wrlp:
    cmp x21, x22
    b.hs .clo
    adr_sym x0, g_sorta64_uniq
    ldr w0, [x0]
    cbz w0, .dow
    cbz x21, .dow
    add x1, x19, x21, lsl #4
    sub x0, x1, #16
    bl sorta64_cmp_rec
    cbz w0, .nxw
.dow:
    add x4, x19, x21, lsl #4
    ldp x0, x1, [x4]
    adr_sym x4, g_sorta64_outh
    ldr x4, [x4]
    cbnz x4, .wrf
    bl ua64_write_out
    bl ua64_write_crlf
    b .nxw
.wrf:
    mov x2, x1
    mov x1, x0
    mov x0, x4
    mov w3, #0
    bl ua64_write_handle
    adr_sym x4, g_sorta64_outh
    ldr x0, [x4]
    adr_sym x1, szUa64Crlf
    mov x2, #2
    mov w3, #0
    bl ua64_write_handle
.nxw:
    add x21, x21, #1
    b .wrlp

.clo:
    adr_sym x0, g_sorta64_outh
    ldr x0, [x0]
    cbz x0, .ex0
    call_iat __imp_CloseHandle
.ex0:
    mov w0, #0
    bl ua64_exit
.out:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

@section rdata

@data db szSorta64Nf, "The system cannot find the file specified.", 0
@data db szSorta64Help, "Sorts input and writes results to screen or file (/R /+n /UNIQUE /O out).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res qword g_sorta64_outh, 1
@res dword g_sorta64_rev, 1
@res dword g_sorta64_col, 1
@res dword g_sorta64_uniq, 1
