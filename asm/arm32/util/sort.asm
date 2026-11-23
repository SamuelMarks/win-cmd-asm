;; @file sort.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 SORT.EXE utility (/R, /+n, /M, /L, /REC, /T, /O out, /UNIQUE) with O(N log N) merge sort.

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc sorta32_cmp_rec
;; @abi arm_nt
;; @in r0 = ptr to 8B record A (0:ptr, 4:len), r1 = ptr to 8B record B
;; @out r0 = -1 if A < B, 0 if A == B, 1 if A > B
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Case-insensitive collate comparison starting at g_sorta32_col.
@proc sorta32_cmp_rec
    push {r4, r5, r6}
    ldr r2, [r0] ; ptrA
    ldr r3, [r0, #4] ; lenA
    ldr r4, [r1] ; ptrB
    ldr r5, [r1, #4] ; lenB

    adr_sym r0, g_sorta32_col
    ldr r0, [r0]
    cmp r3, r0
    bhi .aa
    mov r3, #0
    b .cb
.aa:
    add r2, r2, r0
    sub r3, r3, r0
.cb:
    cmp r5, r0
    bhi .ab
    mov r5, #0
    b .lp
.ab:
    add r4, r4, r0
    sub r5, r5, r0

.lp:
    cmp r3, #0
    beq .aend
    cmp r5, #0
    beq .gt
    ldrb r6, [r2]
    cmp r6, #97
    blo .u1
    cmp r6, #122
    bhi .u1
    sub r6, r6, #32
.u1:
    ldrb r12, [r4]
    cmp r12, #97
    blo .u2
    cmp r12, #122
    bhi .u2
    sub r12, r12, #32
.u2:
    cmp r6, r12
    blo .lt
    bhi .gt
    add r2, r2, #1
    add r4, r4, #1
    sub r3, r3, #1
    sub r5, r5, #1
    b .lp

.aend:
    cmp r5, #0
    beq .eq
.lt:
    mvn r0, #0
    b .rev
.gt:
    mov r0, #1
.rev:
    adr_sym r1, g_sorta32_rev
    ldr r1, [r1]
    cmp r1, #0
    beq .dn
    rsb r0, r0, #0
    b .dn
.eq:
    mov r0, #0
.dn:
    pop {r4, r5, r6}
    bx lr
@endproc

;; @proc sorta32_merge_run
;; @abi arm_nt
;; @in r0 = src array, r1 = dst array, r2 = left, r3 = mid (g_sorta32_right = right)
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Merges sorted runs [left..mid) and [mid..right).
@proc sorta32_merge_run
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; src
    mov r5, r1 ; dst
    mov r6, r2 ; i = left
    mov r7, r3 ; j = mid
    mov r8, r3 ; mid
    mov r9, r2 ; k = left
    adr_sym r0, g_sorta32_right
    ldr r10, [r0] ; right

.mlp:
    cmp r6, r8
    bhs .rrem
    cmp r7, r10
    bhs .lrem

    add r0, r4, r6, lsl #3
    add r1, r4, r7, lsl #3
    bl sorta32_cmp_rec
    cmp r0, #0
    bgt .tr

    add r0, r4, r6, lsl #3
    add r1, r5, r9, lsl #3
    ldr r2, [r0]
    ldr r3, [r0, #4]
    str r2, [r1]
    str r3, [r1, #4]
    add r6, r6, #1
    add r9, r9, #1
    b .mlp
.tr:
    add r0, r4, r7, lsl #3
    add r1, r5, r9, lsl #3
    ldr r2, [r0]
    ldr r3, [r0, #4]
    str r2, [r1]
    str r3, [r1, #4]
    add r7, r7, #1
    add r9, r9, #1
    b .mlp

.lrem:
    cmp r6, r8
    bhs .dn
    add r0, r4, r6, lsl #3
    add r1, r5, r9, lsl #3
    ldr r2, [r0]
    ldr r3, [r0, #4]
    str r2, [r1]
    str r3, [r1, #4]
    add r6, r6, #1
    add r9, r9, #1
    b .lrem
.rrem:
    cmp r7, r10
    bhs .dn
    add r0, r4, r7, lsl #3
    add r1, r5, r9, lsl #3
    ldr r2, [r0]
    ldr r3, [r0, #4]
    str r2, [r1]
    str r3, [r1, #4]
    add r7, r7, #1
    add r9, r9, #1
    b .rrem
.dn:
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on missing file
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 SORT.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #16
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_sorta32_rev
    str r1, [r0]
    adr_sym r0, g_sorta32_col
    str r1, [r0]
    adr_sym r0, g_sorta32_uniq
    str r1, [r0]
    adr_sym r0, g_ua32_arg_b
    strb r1, [r0]
    adr_sym r0, g_ua32_arg_c
    strb r1, [r0]

.sw_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .ld
    cmp r1, #47
    bne .inf
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    cmp r0, #43 ; +
    beq .scol
    bl ua32_chr_upper
    cmp r0, #82 ; R
    beq .sr
    cmp r0, #85 ; U
    beq .su
    cmp r0, #79 ; O
    beq .so
    cmp r0, #77 ; M
    beq .skv
    cmp r0, #76 ; L
    beq .skv
    cmp r0, #84 ; T
    beq .skv
    b .sw_lp
.sr:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #69
    beq .skv
    adr_sym r0, g_sorta32_rev
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.su:
    adr_sym r0, g_sorta32_uniq
    mov r1, #1
    str r1, [r0]
    b .sw_lp
.scol:
    add r0, r5, #2
    bl ua32_parse_u32
    cmp r0, #0
    beq .sw_lp
    sub r0, r0, #1
    adr_sym r1, g_sorta32_col
    str r0, [r1]
    b .sw_lp
.so:
    mov r0, r4
    adr_sym r1, g_ua32_arg_c
    bl ua32_read_tok
    mov r4, r0
    b .sw_lp
.skv:
    mov r0, r4
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    b .sw_lp
.inf:
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    b .sw_lp

.help:
    adr_sym r0, szSorta32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ld:
    adr_sym r0, g_ua32_arg_b
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 260000
    mov r3, #1
    bl ua32_load_stream
    cmp r1, #0
    beq .split
    adr_sym r0, szSorta32Nf
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.split:
    adr_sym r1, g_ua32_arena
    ldr r5, [r1] ; cur
    add r6, r5, r0 ; end
    mov_imm32 r12, 262144
    add r7, r5, r12 ; rec array A
    mov r8, #0 ; count

.splp:
    cmp r5, r6
    bhs .dosort
    ldrb r0, [r5]
    cmp r0, #0x1A
    beq .dosort
    mov_imm32 r12, 16380
    cmp r8, r12
    bhs .dosort
    mov r9, r5
.eol:
    cmp r9, r6
    bhs .gl
    ldrb r0, [r9]
    cmp r0, #13
    beq .gl
    cmp r0, #10
    beq .gl
    add r9, r9, #1
    b .eol
.gl:
    add r0, r7, r8, lsl #3
    str r5, [r0]
    sub r1, r9, r5
    str r1, [r0, #4]
    add r8, r8, #1
    cmp r9, r6
    bhs .dosort
    ldrb r0, [r9]
    cmp r0, #13
    bne .clf
    add r9, r9, #1
.clf:
    cmp r9, r6
    bhs .nxs
    ldrb r0, [r9]
    cmp r0, #10
    bne .nxs
    add r9, r9, #1
.nxs:
    mov r5, r9
    b .splp

.dosort:
    adr_sym r0, g_ua32_arena
    ldr r0, [r0]
    mov_imm32 r12, 262144
    add r5, r0, r12 ; src
    mov_imm32 r12, 393216
    add r6, r0, r12 ; dst
    cmp r8, #2
    blo .emit

    mov r9, #1 ; width
.wlp:
    cmp r9, r8
    bhs .emit
    mov r10, #0 ; left
.rlp:
    cmp r10, r8
    bhs .swp
    add r3, r10, r9
    cmp r3, r8
    bls .gm
    mov r3, r8
.gm:
    add r0, r3, r9
    cmp r0, r8
    bls .gr
    mov r0, r8
.gr:
    adr_sym r1, g_sorta32_right
    str r0, [r1]
    mov r0, r5
    mov r1, r6
    mov r2, r10
    bl sorta32_merge_run
    add r10, r10, r9, lsl #1
    b .rlp
.swp:
    mov r0, r5
    mov r5, r6
    mov r6, r0
    lsl r9, r9, #1
    b .wlp

.emit:
    adr_sym r0, g_sorta32_outh
    mov r1, #0
    str r1, [r0]
    adr_sym r0, g_ua32_arg_c
    ldrb r1, [r0]
    cmp r1, #0
    beq .winit
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    mov_imm32 r1, 0x40000000
    mov r2, #0
    mov r3, #0
    mov r12, #2
    str r12, [sp]
    mov r12, #0x80
    str r12, [sp, #4]
    mov r12, #0
    str r12, [sp, #8]
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .winit
    adr_sym r1, g_sorta32_outh
    str r0, [r1]

.winit:
    mov r9, #0
.wrlp:
    cmp r9, r8
    bhs .clo
    adr_sym r0, g_sorta32_uniq
    ldr r0, [r0]
    cmp r0, #0
    beq .dow
    cmp r9, #0
    beq .dow
    add r1, r5, r9, lsl #3
    sub r0, r1, #8
    bl sorta32_cmp_rec
    cmp r0, #0
    beq .nxw
.dow:
    add r2, r5, r9, lsl #3
    ldr r0, [r2]
    ldr r1, [r2, #4]
    adr_sym r3, g_sorta32_outh
    ldr r3, [r3]
    cmp r3, #0
    bne .wrf
    bl ua32_write_out
    bl ua32_write_crlf
    b .nxw
.wrf:
    mov r2, r1
    mov r1, r0
    mov r0, r3
    mov r3, #0
    bl ua32_write_handle
    adr_sym r0, g_sorta32_outh
    ldr r0, [r0]
    adr_sym r1, szUa32Crlf
    mov r2, #2
    mov r3, #0
    bl ua32_write_handle
.nxw:
    add r9, r9, #1
    b .wrlp

.clo:
    adr_sym r0, g_sorta32_outh
    ldr r0, [r0]
    cmp r0, #0
    beq .ex0
    call_iat __imp_CloseHandle
.ex0:
    mov r0, #0
    bl ua32_exit
    add sp, sp, #16
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

@section rdata

@data db szSorta32Nf, "The system cannot find the file specified.", 0
@data db szSorta32Help, "Sorts input and writes results to screen or file (/R /+n /UNIQUE /O out).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_sorta32_rev, 1
@res dword g_sorta32_col, 1
@res dword g_sorta32_uniq, 1
@res dword g_sorta32_right, 1
@res dword g_sorta32_outh, 1
