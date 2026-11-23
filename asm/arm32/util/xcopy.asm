;; @file xcopy.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 XCOPY.EXE file/tree copier (/A /M /D /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc xca32_is_excl
;; @abi arm_nt
;; @in r0 = candidate path UTF-8
;; @out r0 = 1 if excluded, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Checks path against /EXCLUDE substrings.
@proc xca32_is_excl
    push {r4, r5, r6, r7, r8, lr}
    adr_sym r1, g_xca32_has_excl
    ldr r1, [r1]
    cmp r1, #0
    beq .no
    mov r4, r0
    bl ua32_strlen
    mov r5, r0 ; hlen
    adr_sym r6, g_xca32_excl
.ln:
    ldrb r0, [r6]
    cmp r0, #0
    beq .no
    mov r7, r6
.eol:
    ldrb r0, [r7]
    cmp r0, #0
    beq .got
    cmp r0, #13
    beq .got
    cmp r0, #10
    beq .got
    add r7, r7, #1
    b .eol
.got:
    subs r8, r7, r6
    beq .adv
    cmp r5, r8
    blo .adv
    sub r1, r5, r8
    add r1, r1, #1
    mov r2, r4
.pos:
    mov r3, #0
.ch:
    cmp r3, r8
    bhs .yes
    ldrb r0, [r2, r3]
    cmp r0, #97
    blo .u1
    cmp r0, #122
    bhi .u1
    sub r0, r0, #32
.u1:
    ldrb r12, [r6, r3]
    cmp r12, #97
    blo .u2
    cmp r12, #122
    bhi .u2
    sub r12, r12, #32
.u2:
    cmp r0, r12
    bne .nxp
    add r3, r3, #1
    b .ch
.nxp:
    add r2, r2, #1
    subs r1, r1, #1
    bne .pos
.adv:
    ldrb r0, [r7]
    cmp r0, #13
    bne .clf
    add r7, r7, #1
.clf:
    ldrb r0, [r7]
    cmp r0, #10
    bne .nxl
    add r7, r7, #1
.nxl:
    mov r6, r7
    b .ln
.yes:
    mov r0, #1
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
.no:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc xca32_copy_one
;; @abi arm_nt
;; @in r0 = src, r1 = dst, r2 = src_attr
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Copies a single file honoring XCOPY switches.
@proc xca32_copy_one
    push {r4, r5, r6, lr}
    adr_sym r3, g_xca32_tree
    ldr r3, [r3]
    cmp r3, #0
    bne .dn
    mov r4, r0
    mov r5, r1
    mov r6, r2

    adr_sym r0, g_xca32_hid
    ldr r0, [r0]
    cmp r0, #0
    bne .ca
    tst r6, #0x06
    bne .dn
.ca:
    adr_sym r0, g_xca32_arch
    ldr r0, [r0]
    cmp r0, #0
    beq .ce
    tst r6, #0x20
    beq .dn
.ce:
    mov r0, r4
    bl xca32_is_excl
    cmp r0, #0
    bne .dn

    adr_sym r0, g_xca32_upd
    ldr r0, [r0]
    cmp r0, #0
    beq .cl
    mov r0, r5
    bl ua32_get_attr
    cmn r0, #1
    beq .dn
.cl:
    adr_sym r0, g_xca32_quiet
    ldr r0, [r0]
    cmp r0, #0
    bne .cd
    mov r0, r4
    bl ua32_write_str
    adr_sym r0, g_xca32_full
    ldr r0, [r0]
    cmp r0, #0
    beq .nl
    adr_sym r0, szXca32Arrow
    bl ua32_write_str
    mov r0, r5
    bl ua32_write_str
.nl:
    bl ua32_write_crlf
.cd:
    adr_sym r0, g_xca32_list
    ldr r0, [r0]
    cmp r0, #0
    beq .cp
    adr_sym r0, g_xca32_copied
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    b .dn
.cp:
    adr_sym r0, g_xca32_ro
    ldr r0, [r0]
    cmp r0, #0
    beq .docp
    mov r0, r5
    bl ua32_get_attr
    cmn r0, #1
    beq .docp
    tst r0, #1
    beq .docp
    mov r0, r5
    mov r1, #0x80
    bl ua32_set_attr
.docp:
    mov r0, r4
    mov r1, r5
    mov r2, #0
    bl ua32_copy_file
    cmp r0, #0
    beq .dn
    adr_sym r0, g_xca32_copied
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]

    adr_sym r0, g_xca32_keep
    ldr r0, [r0]
    cmp r0, #0
    bne .cm
    mov r0, r5
    mov r1, #0x20
    bl ua32_set_attr
.cm:
    adr_sym r0, g_xca32_clr_a
    ldr r0, [r0]
    cmp r0, #0
    beq .dn
    bics r1, r6, #0x20
    bne .ms
    mov r1, #0x80
.ms:
    mov r0, r4
    bl ua32_set_attr
.dn:
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc xca32_copy_dir
;; @abi arm_nt
;; @in r0 = src dir, r1 = dst dir, r2 = depth
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Copies directory tree.
@proc xca32_copy_dir
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #3072
    sub sp, sp, #4
    cmp r2, #16
    bhs .dn
    mov r6, r2 ; depth
    mov r7, #0 ; created
    mov r5, r1 ; dst
    mov r1, r0
    add r0, sp, #544 ; src dir
    bl ua32_strcpy
    add r0, sp, #1056 ; dst dir
    mov r1, r5
    bl ua32_strcpy

    adr_sym r0, g_xca32_empty
    ldr r0, [r0]
    cmp r0, #0
    bne .mk
    adr_sym r0, g_xca32_tree
    ldr r0, [r0]
    cmp r0, #0
    beq .en
.mk:
    adr_sym r0, g_xca32_list
    ldr r0, [r0]
    cmp r0, #0
    bne .en
    add r0, sp, #1056
    bl ua32_mkdir
    mov r7, #1

.en:
    add r0, sp, #1568
    add r1, sp, #544
    adr_sym r2, szXca32Star
    bl ua32_path_join
    add r0, sp, #1568
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .dn
    mov r4, r0
.lp:
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .nd
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .nx
    cmp r0, #46
    bne .nd
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .nx
.nd:
    add r0, sp, #1568
    add r1, sp, #544
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #2080
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join

    ldr r0, [sp]
    tst r0, #0x10
    bne .sub

    cmp r7, #0
    bne .df
    adr_sym r0, g_xca32_list
    ldr r0, [r0]
    cmp r0, #0
    bne .df
    add r0, sp, #1056
    bl ua32_mkdir
    mov r7, #1
.df:
    add r0, sp, #1568
    add r1, sp, #2080
    ldr r2, [sp]
    bl xca32_copy_one
    b .nx
.sub:
    adr_sym r0, g_xca32_sub
    ldr r0, [r0]
    cmp r0, #0
    beq .nx
    add r0, sp, #1568
    add r1, sp, #2080
    add r2, r6, #1
    bl xca32_copy_dir
.nx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .lp
    mov r0, r4
    call_iat __imp_FindClose
.dn:
    add sp, sp, #4
    add sp, sp, #3072
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 4 on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 XCOPY.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_xca32_arch
    str r1, [r0]
    adr_sym r0, g_xca32_clr_a
    str r1, [r0]
    adr_sym r0, g_xca32_sub
    str r1, [r0]
    adr_sym r0, g_xca32_empty
    str r1, [r0]
    adr_sym r0, g_xca32_quiet
    str r1, [r0]
    adr_sym r0, g_xca32_full
    str r1, [r0]
    adr_sym r0, g_xca32_list
    str r1, [r0]
    adr_sym r0, g_xca32_hid
    str r1, [r0]
    adr_sym r0, g_xca32_ro
    str r1, [r0]
    adr_sym r0, g_xca32_tree
    str r1, [r0]
    adr_sym r0, g_xca32_upd
    str r1, [r0]
    adr_sym r0, g_xca32_keep
    str r1, [r0]
    adr_sym r0, g_xca32_has_excl
    str r1, [r0]
    adr_sym r0, g_xca32_copied
    str r1, [r0]
    adr_sym r0, g_ua32_arg_b
    strb r1, [r0]
    adr_sym r0, g_ua32_arg_c
    strb r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .run
    cmp r1, #47
    bne .pos
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #65 ; A
    beq .sa
    cmp r0, #77 ; M
    beq .sm
    cmp r0, #83 ; S
    beq .ss
    cmp r0, #69 ; E
    beq .se
    cmp r0, #81 ; Q
    beq .sq
    cmp r0, #70 ; F
    beq .sf
    cmp r0, #76 ; L
    beq .sl
    cmp r0, #72 ; H
    beq .sh
    cmp r0, #82 ; R
    beq .sr
    cmp r0, #84 ; T
    beq .st
    cmp r0, #85 ; U
    beq .su
    cmp r0, #75 ; K
    beq .sk
    b .arg_lp
.sa:
    adr_sym r0, g_xca32_arch
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sm:
    mov r1, #1
    adr_sym r0, g_xca32_arch
    str r1, [r0]
    adr_sym r0, g_xca32_clr_a
    str r1, [r0]
    b .arg_lp
.ss:
    adr_sym r0, g_xca32_sub
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.se:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #88 ; X (/EXCLUDE)
    beq .sex
    mov r1, #1
    adr_sym r0, g_xca32_sub
    str r1, [r0]
    adr_sym r0, g_xca32_empty
    str r1, [r0]
    b .arg_lp
.sex:
    mov r1, #0
.fcl:
    ldrb r0, [r5, r1]
    cmp r0, #0
    beq .arg_lp
    cmp r0, #58
    beq .gcl
    add r1, r1, #1
    b .fcl
.gcl:
    add r0, r5, r1
    add r0, r0, #1
    adr_sym r1, g_xca32_excl
    mov_imm32 r2, 4000
    mov r3, #1
    bl ua32_load_stream
    cmp r1, #0
    bne .arg_lp
    adr_sym r0, g_xca32_has_excl
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sq:
    adr_sym r0, g_xca32_quiet
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sf:
    adr_sym r0, g_xca32_full
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sl:
    adr_sym r0, g_xca32_list
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sh:
    adr_sym r0, g_xca32_hid
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sr:
    adr_sym r0, g_xca32_ro
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.st:
    mov r1, #1
    adr_sym r0, g_xca32_tree
    str r1, [r0]
    adr_sym r0, g_xca32_sub
    str r1, [r0]
    b .arg_lp
.su:
    adr_sym r0, g_xca32_upd
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sk:
    adr_sym r0, g_xca32_keep
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.pos:
    adr_sym r0, g_ua32_arg_b
    ldrb r0, [r0]
    cmp r0, #0
    bne .pos2
    mov r0, r4
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.pos2:
    mov r0, r4
    adr_sym r1, g_ua32_arg_c
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp

.help:
    adr_sym r0, szXca32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.run:
    adr_sym r0, g_ua32_arg_b
    ldrb r0, [r0]
    cmp r0, #0
    beq .bad
    adr_sym r0, g_ua32_arg_c
    ldrb r0, [r0]
    cmp r0, #0
    bne .gd
    adr_sym r0, g_ua32_arg_c
    mov r1, #1000
    bl ua32_get_cwd
.gd:
    adr_sym r0, g_ua32_arg_b
    bl ua32_get_attr
    cmn r0, #1
    beq .nf
    tst r0, #0x10
    beq .sf1
    adr_sym r0, g_ua32_arg_b
    adr_sym r1, g_ua32_arg_c
    mov r2, #0
    bl xca32_copy_dir
    b .sum
.sf1:
    mov r5, r0
    adr_sym r0, g_ua32_arg_c
    bl ua32_get_attr
    cmn r0, #1
    beq .dd
    tst r0, #0x10
    beq .dd
    adr_sym r6, g_ua32_arg_b
    mov r0, r6
    bl ua32_strlen
    mvn r2, #0
    mov r1, #0
.sll:
    cmp r1, r0
    bhs .sld
    ldrb r3, [r6, r1]
    cmp r3, #0x5C
    beq .slm
    cmp r3, #47
    bne .sln
.slm:
    mov r2, r1
.sln:
    add r1, r1, #1
    b .sll
.sld:
    add r2, r6, r2
    add r2, r2, #1
    adr_sym r0, g_ua32_path_tmp
    adr_sym r1, g_ua32_arg_c
    bl ua32_path_join
    adr_sym r0, g_ua32_arg_b
    adr_sym r1, g_ua32_path_tmp
    mov r2, r5
    bl xca32_copy_one
    b .sum
.dd:
    adr_sym r0, g_ua32_arg_b
    adr_sym r1, g_ua32_arg_c
    mov r2, r5
    bl xca32_copy_one
.sum:
    adr_sym r0, g_xca32_copied
    ldr r0, [r0]
    bl ua32_print_u32
    adr_sym r0, szXca32Sum
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.nf:
    adr_sym r0, szXca32Nf
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #4
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.bad:
    adr_sym r0, szXca32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #4
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szXca32Star, "*", 0
@data db szXca32Arrow, " -> ", 0
@data db szXca32Sum, " File(s) copied", 0
@data db szXca32Nf, "File not found", 0
@data db szXca32Syn, "Invalid number of parameters", 0
@data db szXca32Help, "Copies files and directory trees (/A /M /S /E /I /Q /F /L /H /R /T /U /K /Y /EXCLUDE).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_xca32_arch, 1
@res dword g_xca32_clr_a, 1
@res dword g_xca32_sub, 1
@res dword g_xca32_empty, 1
@res dword g_xca32_quiet, 1
@res dword g_xca32_full, 1
@res dword g_xca32_list, 1
@res dword g_xca32_hid, 1
@res dword g_xca32_ro, 1
@res dword g_xca32_tree, 1
@res dword g_xca32_upd, 1
@res dword g_xca32_keep, 1
@res dword g_xca32_has_excl, 1
@res dword g_xca32_copied, 1
@res byte g_xca32_excl, 4096
