;; @file replace.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 REPLACE.EXE file updater (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc repa32_get_mtime
;; @abi arm_nt
;; @in r0 = file path UTF-8, r1 = 8B FILETIME output
;; @out r0 = 1 on success, 0 on failure
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries last-write FILETIME.
@proc repa32_get_mtime
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r5, r1
    bl ua32_open_read
    cmn r0, #1
    beq .fl
    mov r4, r0
    mov r1, #0
    mov r2, #0
    mov r3, r5
    call_iat __imp_GetFileTime
    mov r5, r0
    mov r0, r4
    call_iat __imp_CloseHandle
    mov r0, r5
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.fl:
    mov r0, #0
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc repa32_apply_dir
;; @abi arm_nt
;; @in r0 = target dir, r1 = depth
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Replaces or adds target files and recurses under /S.
@proc repa32_apply_dir
    push {r4, r5, r6, lr}
    sub sp, sp, #2048
    cmp r1, #16
    bhs .dn
    mov r5, r1
    mov r1, r0
    add r0, sp, #544
    bl ua32_strcpy

    add r0, sp, #1056
    add r1, sp, #544
    adr_sym r2, g_repa32_leaf
    bl ua32_path_join

    add r0, sp, #1056
    bl ua32_get_attr
    mov r6, r0

    adr_sym r0, g_repa32_add
    ldr r0, [r0]
    cmp r0, #0
    bne .addm

    cmn r6, #1
    beq .sub
    tst r6, #0x10
    bne .sub

    adr_sym r0, g_repa32_older
    ldr r0, [r0]
    cmp r0, #0
    beq .cro
    add r0, sp, #1056
    add r1, sp, #1600
    bl repa32_get_mtime
    cmp r0, #0
    beq .cro
    adr_sym r0, g_repa32_sft
    add r1, sp, #1600
    call_iat __imp_CompareFileTime
    cmp r0, #0
    ble .sub

.cro:
    tst r6, #1
    beq .dor
    adr_sym r0, g_repa32_ro
    ldr r0, [r0]
    cmp r0, #0
    beq .sub
    add r0, sp, #1056
    mov r1, #0x80
    bl ua32_set_attr
.dor:
    adr_sym r0, g_repa32_src
    add r1, sp, #1056
    mov r2, #0
    bl ua32_copy_file
    cmp r0, #0
    beq .sub
    adr_sym r0, g_repa32_cnt
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    b .sub

.addm:
    cmn r6, #1
    bne .sub
    adr_sym r0, g_repa32_src
    add r1, sp, #1056
    mov r2, #1
    bl ua32_copy_file
    cmp r0, #0
    beq .sub
    adr_sym r0, g_repa32_cnt
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]

.sub:
    adr_sym r0, g_repa32_sub
    ldr r0, [r0]
    cmp r0, #0
    beq .dn
    add r0, sp, #1056
    add r1, sp, #544
    adr_sym r2, szRepa32Star
    bl ua32_path_join
    add r0, sp, #1056
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
    add r0, sp, #1056
    add r1, sp, #544
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #1056
    add r1, r5, #1
    bl repa32_apply_dir
.snx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .slp
    mov r0, r4
    call_iat __imp_FindClose
.dn:
    add sp, sp, #2048
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits non-zero on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 REPLACE.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_repa32_add
    str r1, [r0]
    adr_sym r0, g_repa32_ro
    str r1, [r0]
    adr_sym r0, g_repa32_sub
    str r1, [r0]
    adr_sym r0, g_repa32_older
    str r1, [r0]
    adr_sym r0, g_repa32_cnt
    str r1, [r0]
    adr_sym r0, g_repa32_src
    strb r1, [r0]
    adr_sym r0, g_repa32_dst
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
    cmp r0, #82 ; R
    beq .sr
    cmp r0, #83 ; S
    beq .ss
    cmp r0, #85 ; U
    beq .su
    b .arg_lp
.sa:
    adr_sym r0, g_repa32_add
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sr:
    adr_sym r0, g_repa32_ro
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.ss:
    adr_sym r0, g_repa32_sub
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.su:
    adr_sym r0, g_repa32_older
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.pos:
    adr_sym r0, g_repa32_src
    ldrb r0, [r0]
    cmp r0, #0
    bne .pos2
    mov r0, r4
    adr_sym r1, g_repa32_src
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.pos2:
    mov r0, r4
    adr_sym r1, g_repa32_dst
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp

.help:
    adr_sym r0, szRepa32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.run:
    adr_sym r5, g_repa32_src
    ldrb r0, [r5]
    cmp r0, #0
    beq .bad
    mov r0, r5
    bl ua32_get_attr
    cmn r0, #1
    beq .nosrc

    mov r0, r5
    adr_sym r1, g_repa32_sft
    bl repa32_get_mtime

    adr_sym r0, g_repa32_dst
    ldrb r0, [r0]
    cmp r0, #0
    bne .leaf
    adr_sym r0, g_repa32_dst
    mov r1, #1000
    bl ua32_get_cwd
.leaf:
    mov r0, r5
    bl ua32_strlen
    mvn r2, #0
    mov r1, #0
.sl:
    cmp r1, r0
    bhs .sld
    ldrb r3, [r5, r1]
    cmp r3, #0x5C
    beq .mk
    cmp r3, #47
    beq .mk
    cmp r3, #58
    bne .nx
.mk:
    mov r2, r1
.nx:
    add r1, r1, #1
    b .sl
.sld:
    add r1, r5, r2
    add r1, r1, #1
    adr_sym r0, g_repa32_leaf
    bl ua32_strcpy

    adr_sym r0, g_repa32_dst
    mov r1, #0
    bl repa32_apply_dir

    adr_sym r0, g_repa32_cnt
    ldr r0, [r0]
    bl ua32_print_u32
    adr_sym r0, g_repa32_add
    ldr r0, [r0]
    cmp r0, #0
    bne .addm
    adr_sym r0, szRepa32Done
    b .pm
.addm:
    adr_sym r0, szRepa32Added
.pm:
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.nosrc:
    adr_sym r0, szRepa32NoSrc
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #2
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.bad:
    adr_sym r0, szRepa32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #3
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
@endproc

@section rdata

@data db szRepa32Star, "*", 0
@data db szRepa32Done, " file(s) replaced", 0
@data db szRepa32Added, " file(s) added", 0
@data db szRepa32NoSrc, "Source path required", 0
@data db szRepa32Syn, "Invalid parameter", 0
@data db szRepa32Help, "Replaces files in destination directories (src [dst] [/A] [/P] [/R] [/S] [/W] [/U]).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res byte g_repa32_sft, 8
@res dword g_repa32_add, 1
@res dword g_repa32_ro, 1
@res dword g_repa32_sub, 1
@res dword g_repa32_older, 1
@res dword g_repa32_cnt, 1
@res byte g_repa32_src, 520
@res byte g_repa32_dst, 520
@res byte g_repa32_leaf, 520
