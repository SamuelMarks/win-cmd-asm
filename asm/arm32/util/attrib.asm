;; @file attrib.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 ATTRIB.EXE utility ([+|-][R|A|S|H|I|O|P|U] [path] [/S] [/D] [/L]).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc attra32_flag_bit
;; @abi arm_nt
;; @in r0 = uppercase attribute char
;; @out r0 = Win32 FILE_ATTRIBUTE_* bitmask (0 if unknown)
;; @err r0 = 0
;; @clobbers r0
;; @desc Maps R, H, S, A, I, O, P, U to Win32 attribute constants.
@proc attra32_flag_bit
    cmp r0, #82 ; R
    beq .r
    cmp r0, #72 ; H
    beq .h
    cmp r0, #83 ; S
    beq .s
    cmp r0, #65 ; A
    beq .a
    cmp r0, #79 ; O
    beq .o
    cmp r0, #73 ; I
    beq .i
    cmp r0, #80 ; P
    beq .p
    cmp r0, #85 ; U
    beq .u
    mov r0, #0
    bx lr
.r:
    mov r0, #0x01
    bx lr
.h:
    mov r0, #0x02
    bx lr
.s:
    mov r0, #0x04
    bx lr
.a:
    mov r0, #0x20
    bx lr
.o:
    mov r0, #0x1000
    bx lr
.i:
    mov r0, #0x2000
    bx lr
.p:
    mov r0, #0x00080000
    bx lr
.u:
    mov r0, #0x00100000
    bx lr
@endproc

;; @proc attra32_print_entry
;; @abi arm_nt
;; @in r0 = Win32 attributes, r1 = UTF-8 file path
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints formatted "A  SHR OIPU  path" line.
@proc attra32_print_entry
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1

    mov r0, #32
    tst r4, #0x20
    beq .p1
    mov r0, #65 ; A
.p1:
    bl ua32_write_chr
    mov r0, #32
    bl ua32_write_chr
    mov r0, #32
    tst r4, #0x04
    beq .p2
    mov r0, #83 ; S
.p2:
    bl ua32_write_chr
    mov r0, #32
    tst r4, #0x02
    beq .p3
    mov r0, #72 ; H
.p3:
    bl ua32_write_chr
    mov r0, #32
    tst r4, #0x01
    beq .p4
    mov r0, #82 ; R
.p4:
    bl ua32_write_chr
    mov r0, #32
    bl ua32_write_chr
    mov r0, #32
    tst r4, #0x1000
    beq .p5
    mov r0, #79 ; O
.p5:
    bl ua32_write_chr
    mov r0, #32
    tst r4, #0x2000
    beq .p6
    mov r0, #73 ; I
.p6:
    bl ua32_write_chr
    mov r0, #32
    tst r4, #0x00080000
    beq .p7
    mov r0, #80 ; P
.p7:
    bl ua32_write_chr
    mov r0, #32
    tst r4, #0x00100000
    beq .p8
    mov r0, #85 ; U
.p8:
    bl ua32_write_chr
    mov r0, #32
    bl ua32_write_chr
    mov r0, #32
    bl ua32_write_chr
    mov r0, r5
    bl ua32_write_str
    bl ua32_write_crlf
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc attra32_apply_one
;; @abi arm_nt
;; @in r0 = full UTF-8 path, r1 = current attributes
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Applies +set/-clear masks or displays attributes.
@proc attra32_apply_one
    push {r4, lr}
    adr_sym r2, g_attra32_set
    ldr r2, [r2]
    adr_sym r3, g_attra32_clr
    ldr r3, [r3]
    orrs r12, r2, r3
    beq .show
    bic r1, r1, r3
    orrs r1, r1, r2
    bne .do_set
    mov r1, #0x80
.do_set:
    bl ua32_set_attr
    adr_sym r0, g_attra32_hit
    mov r1, #1
    str r1, [r0]
    pop {r4, lr}
    bx lr
.show:
    mov r2, r0
    mov r0, r1
    mov r1, r2
    bl attra32_print_entry
    adr_sym r0, g_attra32_hit
    mov r1, #1
    str r1, [r0]
    pop {r4, lr}
    bx lr
@endproc

;; @proc attra32_scan_dir
;; @abi arm_nt
;; @in r0 = dir UTF-8, r1 = leaf pattern UTF-8, r2 = depth
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Enumerates matching entries in r0 and recurses under /S.
@proc attra32_scan_dir
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #2048
    sub sp, sp, #4
    ; [sp+0..543] = find entry, [sp+544..1055] = child path, [sp+1056..1567] = dir copy
    cmp r2, #16
    bhs .dn
    mov r5, r1 ; leaf
    mov r6, r2 ; depth
    mov r1, r0
    add r0, sp, #1056
    bl ua32_strcpy

    add r0, sp, #544
    add r1, sp, #1056
    mov r2, r5
    bl ua32_path_join

    add r0, sp, #544
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .sub
    mov r4, r0

.elp:
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .nd
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .enx
    cmp r0, #46
    bne .nd
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .enx
.nd:
    ldr r0, [sp]
    tst r0, #0x10
    beq .do_i
    adr_sym r1, g_attra32_dirs
    ldr r1, [r1]
    cmp r1, #0
    beq .enx
.do_i:
    add r0, sp, #544
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #544
    ldr r1, [sp]
    bl attra32_apply_one
.enx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .elp
    mov r0, r4
    call_iat __imp_FindClose

.sub:
    adr_sym r0, g_attra32_sub
    ldr r0, [r0]
    cmp r0, #0
    beq .dn
    add r0, sp, #544
    add r1, sp, #1056
    adr_sym r2, szAttra32Star
    bl ua32_path_join
    add r0, sp, #544
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .dn
    mov r4, r0
.slp:
    ldr r0, [sp]
    tst r0, #0x10
    beq .snx
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .go
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .snx
    cmp r0, #46
    bne .go
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .snx
.go:
    add r0, sp, #544
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #544
    mov r1, r5
    add r2, r6, #1
    bl attra32_scan_dir
.snx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .slp
    mov r0, r4
    call_iat __imp_FindClose
.dn:
    add sp, sp, #4
    add sp, sp, #2048
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 ATTRIB.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_attra32_set
    str r1, [r0]
    adr_sym r0, g_attra32_clr
    str r1, [r0]
    adr_sym r0, g_attra32_sub
    str r1, [r0]
    adr_sym r0, g_attra32_dirs
    str r1, [r0]
    adr_sym r0, g_attra32_hit
    str r1, [r0]
    adr_sym r0, g_ua32_arg_b
    strb r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .run
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5]
    cmp r0, #43 ; +
    beq .pm
    cmp r0, #45 ; -
    beq .mm
    cmp r0, #47 ; /
    beq .sw
    adr_sym r0, g_ua32_arg_b
    mov r1, r5
    bl ua32_strcpy
    b .arg_lp
.pm:
    ldrb r0, [r5, #1]
    bl ua32_chr_upper
    bl attra32_flag_bit
    cmp r0, #0
    beq .bad
    adr_sym r1, g_attra32_set
    ldr r2, [r1]
    orr r2, r2, r0
    str r2, [r1]
    b .arg_lp
.mm:
    ldrb r0, [r5, #1]
    bl ua32_chr_upper
    bl attra32_flag_bit
    cmp r0, #0
    beq .bad
    adr_sym r1, g_attra32_clr
    ldr r2, [r1]
    orr r2, r2, r0
    str r2, [r1]
    b .arg_lp
.sw:
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #83 ; S
    beq .ss
    cmp r0, #68 ; D
    beq .sd
    cmp r0, #76 ; L
    beq .arg_lp
    b .bad
.ss:
    adr_sym r0, g_attra32_sub
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sd:
    adr_sym r0, g_attra32_dirs
    mov r1, #1
    str r1, [r0]
    b .arg_lp

.help:
    adr_sym r0, szAttra32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.bad:
    adr_sym r0, szAttra32Bad
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.run:
    adr_sym r5, g_ua32_arg_b
    ldrb r0, [r5]
    cmp r0, #0
    bne .has_t
    adr_sym r0, g_ua32_arg_c
    mov r1, #1000
    bl ua32_get_cwd
    adr_sym r0, g_ua32_arg_c
    adr_sym r1, szAttra32Star
    mov r2, #0
    bl attra32_scan_dir
    b .chk

.has_t:
    mov r0, r5
    bl ua32_strlen
    mvn r6, #0 ; last sep = -1
    mov r1, #0
.sl:
    cmp r1, r0
    bhs .gsl
    ldrb r2, [r5, r1]
    cmp r2, #0x5C
    beq .mk
    cmp r2, #47
    beq .mk
    cmp r2, #58
    bne .nx
.mk:
    mov r6, r1
.nx:
    add r1, r1, #1
    b .sl
.gsl:
    cmn r6, #1
    bne .sp_p
    adr_sym r0, g_ua32_arg_c
    mov r1, #1000
    bl ua32_get_cwd
    adr_sym r0, g_attra32_leaf
    mov r1, r5
    bl ua32_strcpy
    b .dosc
.sp_p:
    adr_sym r0, g_ua32_arg_c
    mov r1, #0
.cpd:
    cmp r1, r6
    bhi .tmd
    ldrb r2, [r5, r1]
    strb r2, [r0, r1]
    add r1, r1, #1
    b .cpd
.tmd:
    mov r2, #0
    strb r2, [r0, r1]
    adr_sym r0, g_attra32_leaf
    add r1, r5, r6
    add r1, r1, #1
    bl ua32_strcpy
.dosc:
    adr_sym r0, g_ua32_arg_c
    adr_sym r1, g_attra32_leaf
    mov r2, #0
    bl attra32_scan_dir

.chk:
    adr_sym r0, g_attra32_hit
    ldr r0, [r0]
    cmp r0, #0
    bne .ex0
    adr_sym r0, szAttra32Nf
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.ex0:
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szAttra32Star, "*", 0
@data db szAttra32Nf, "File not found", 0
@data db szAttra32Bad, "Parameter format not correct -", 0
@data db szAttra32Help, "Displays or changes file attributes (+R -R +A -A +S -S +H -H +I -I +O -O +P -P +U -U /S /D /L).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_attra32_set, 1
@res dword g_attra32_clr, 1
@res dword g_attra32_sub, 1
@res dword g_attra32_dirs, 1
@res dword g_attra32_hit, 1
@res byte g_attra32_leaf, 520
