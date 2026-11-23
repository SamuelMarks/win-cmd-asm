;; @file tree.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 TREE.EXE directory visualizer ([drive:][path] [/F] [/A]).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc treea32_indent
;; @abi arm_nt
;; @in r0 = depth (0..15)
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints vertical bar connectors up to depth r0.
@proc treea32_indent
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, #0
.lp:
    cmp r5, r4
    bhs .dn
    adr_sym r0, g_treea32_last
    ldrb r0, [r0, r5]
    cmp r0, #0
    bne .bl
    adr_sym r0, g_treea32_asc
    ldr r0, [r0]
    cmp r0, #0
    bne .ba
    adr_sym r0, szTra32UniBar
    bl ua32_write_str
    b .nx
.ba:
    adr_sym r0, szTra32AscBar
    bl ua32_write_str
    b .nx
.bl:
    adr_sym r0, szTra32Blank
    bl ua32_write_str
.nx:
    add r5, r5, #1
    b .lp
.dn:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc treea32_walk
;; @abi arm_nt
;; @in r0 = dir path UTF-8, r1 = depth
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Recursively prints files (/F) and subdirectories.
@proc treea32_walk
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #2048
    sub sp, sp, #4
    cmp r1, #15
    bhs .dn
    mov r5, r1 ; depth
    mov r1, r0
    add r0, sp, #1056
    bl ua32_strcpy

    mov r6, #0 ; subdir total
    add r0, sp, #544
    add r1, sp, #1056
    adr_sym r2, szTra32Star
    bl ua32_path_join
    add r0, sp, #544
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .dn
    mov r4, r0
.clp:
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .cc
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .cnx
    cmp r0, #46
    bne .cc
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .cnx
.cc:
    ldr r0, [sp]
    tst r0, #0x10
    bne .isd
    adr_sym r0, g_treea32_files
    ldr r0, [r0]
    cmp r0, #0
    beq .cnx
    mov r0, r5
    bl treea32_indent
    adr_sym r0, szTra32Blank
    bl ua32_write_str
    add r0, sp, #20
    bl ua32_write_str
    bl ua32_write_crlf
    b .cnx
.isd:
    add r6, r6, #1
.cnx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .clp
    mov r0, r4
    call_iat __imp_FindClose

    cmp r6, #0
    beq .dn

    mov r7, #0 ; seen
    add r0, sp, #544
    add r1, sp, #1056
    adr_sym r2, szTra32Star
    bl ua32_path_join
    add r0, sp, #544
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .dn
    mov r4, r0
.dlp:
    ldr r0, [sp]
    tst r0, #0x10
    beq .dnx
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .dok
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .dnx
    cmp r0, #46
    bne .dok
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .dnx
.dok:
    add r7, r7, #1
    mov r0, r5
    bl treea32_indent

    adr_sym r1, g_treea32_last
    cmp r7, r6
    beq .last
    mov r0, #0
    strb r0, [r1, r5]
    adr_sym r0, g_treea32_asc
    ldr r0, [r0]
    cmp r0, #0
    bne .ma
    adr_sym r0, szTra32UniMid
    b .prb
.ma:
    adr_sym r0, szTra32AscMid
    b .prb
.last:
    mov r0, #1
    strb r0, [r1, r5]
    adr_sym r0, g_treea32_asc
    ldr r0, [r0]
    cmp r0, #0
    bne .ea
    adr_sym r0, szTra32UniEnd
    b .prb
.ea:
    adr_sym r0, szTra32AscEnd
.prb:
    bl ua32_write_str
    add r0, sp, #20
    bl ua32_write_str
    bl ua32_write_crlf

    add r0, sp, #544
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #544
    add r1, r5, #1
    bl treea32_walk
.dnx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .dlp
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
;; @err Exits 1 on invalid path
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 TREE.EXE.
@proc mainCRTStartup
    push {r4, r5, lr}
    sub sp, sp, #4
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_treea32_files
    str r1, [r0]
    adr_sym r0, g_treea32_asc
    str r1, [r0]
    adr_sym r0, g_ua32_arg_b
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
    cmp r0, #70 ; F
    beq .sf
    cmp r0, #65 ; A
    beq .sa
    b .arg_lp
.sf:
    adr_sym r0, g_treea32_files
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sa:
    adr_sym r0, g_treea32_asc
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.pos:
    adr_sym r1, g_ua32_arg_b
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp

.help:
    adr_sym r0, szTra32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr

.run:
    adr_sym r0, g_ua32_arg_b
    ldrb r0, [r0]
    cmp r0, #0
    bne .chk
    adr_sym r0, g_ua32_arg_b
    mov r1, #1000
    bl ua32_get_cwd
.chk:
    adr_sym r0, g_ua32_arg_b
    bl ua32_get_attr
    cmn r0, #1
    beq .bad
    tst r0, #0x10
    beq .bad

    adr_sym r0, szTra32Hdr
    bl ua32_write_str
    bl ua32_write_crlf
    adr_sym r0, g_ua32_arg_b
    bl ua32_write_str
    bl ua32_write_crlf

    adr_sym r0, g_ua32_arg_b
    mov r1, #0
    bl treea32_walk
    mov r0, #0
    bl ua32_exit
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.bad:
    adr_sym r0, szTra32Inv
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #1
    bl ua32_exit
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

@section rdata

@data db szTra32Star, "*", 0
@data db szTra32Hdr, "Folder PATH listing", 0
@data db szTra32Inv, "Invalid path", 0
@data db szTra32AscMid, "+---", 0
@data db szTra32AscEnd, 0x5C, "---", 0
@data db szTra32AscBar, "|   ", 0
@data db szTra32UniMid, 0xE2, 0x94, 0x9C, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTra32UniEnd, 0xE2, 0x94, 0x94, 0xE2, 0x94, 0x80, 0xE2, 0x94, 0x80, " ", 0
@data db szTra32UniBar, 0xE2, 0x94, 0x82, "   ", 0
@data db szTra32Blank, "    ", 0
@data db szTra32Help, "Graphically displays the folder structure of a drive or path ([path] [/F] [/A]).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_treea32_files, 1
@res dword g_treea32_asc, 1
@res byte g_treea32_last, 32
