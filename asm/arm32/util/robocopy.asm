;; @file robocopy.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 ROBOCOPY.EXE with CreateThread /MT[:n] worker pool and bitmask ERRORLEVEL 0..16.

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global rba32_worker_proc
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc rba32_worker_proc
;; @abi arm_nt
;; @in r0 = pointer to 16-byte RB_SLOT (0:tid, 4:nthreads, 8:copied, 12:failed)
;; @out r0 = 0
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc GUARD_CF-valid ARM32 worker thread copying its stride of queued file pairs using thread-local stack buffers.
@proc rba32_worker_proc
    push {r4, r5, r6, lr}
    sub sp, sp, #3072
    mov r4, r0 ; slot
    ldr r5, [r4] ; tid
.jlp:
    adr_sym r0, g_rba32_job_cnt
    ldr r0, [r0]
    cmp r5, r0
    bhs .dn
    adr_sym r0, g_ua32_arena
    ldr r6, [r0]
    add r6, r6, r5, lsl #10

    mov r0, r6
    mov r1, sp
    bl ua32_u8_to_w

    add r0, r6, #512
    add r1, sp, #1536
    bl ua32_u8_to_w

    mov r0, sp
    add r1, sp, #1536
    mov r2, #0
    call_iat __imp_CopyFileW
    cmp r0, #0
    beq .fl
    ldr r0, [r4, #8]
    add r0, r0, #1
    str r0, [r4, #8]
    b .nx
.fl:
    ldr r0, [r4, #12]
    add r0, r0, #1
    str r0, [r4, #12]
.nx:
    ldr r0, [r4, #4]
    add r5, r5, r0
    b .jlp
.dn:
    mov r0, #0
    add sp, sp, #3072
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc rba32_sync_dir
;; @abi arm_nt
;; @in r0 = src dir, r1 = dst dir, r2 = depth
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Replicates directory contents, purges extras (/PURGE /MIR), and recurses (/S /E).
@proc rba32_sync_dir
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #3072
    sub sp, sp, #4
    adr_sym r3, g_rba32_max_lev
    ldr r3, [r3]
    cmp r2, r3
    bhs .dn
    mov r6, r2
    mov r5, r1
    mov r1, r0
    add r0, sp, #544
    bl ua32_strcpy
    add r0, sp, #1056
    mov r1, r5
    bl ua32_strcpy

    adr_sym r0, g_rba32_list
    ldr r0, [r0]
    cmp r0, #0
    bne .en
    add r0, sp, #1056
    bl ua32_mkdir

.en:
    add r0, sp, #1568
    add r1, sp, #544
    adr_sym r2, g_rba32_mask
    bl ua32_path_join
    add r0, sp, #1568
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .pg
    mov r4, r0
.slp:
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .nd
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .snx
    cmp r0, #46
    bne .nd
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .snx
.nd:
    ldr r0, [sp]
    tst r0, #0x10
    bne .snx
    adr_sym r0, g_rba32_xf
    ldrb r1, [r0]
    cmp r1, #0
    beq .nxf
    mov r1, r0
    add r0, sp, #20
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .snx
.nxf:
    add r0, sp, #1568
    add r1, sp, #544
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #2080
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join

    adr_sym r0, g_rba32_nfl
    ldr r0, [r0]
    cmp r0, #0
    bne .act
    adr_sym r0, szRba32Tab
    bl ua32_write_str
    add r0, sp, #1568
    bl ua32_write_str
    bl ua32_write_crlf
.act:
    adr_sym r0, g_rba32_list
    ldr r0, [r0]
    cmp r0, #0
    beq .rl
    adr_sym r0, g_rba32_copied
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    adr_sym r0, g_rba32_status
    ldr r1, [r0]
    orr r1, r1, #1
    str r1, [r0]
    b .snx
.rl:
    adr_sym r0, g_rba32_mt
    ldr r0, [r0]
    cmp r0, #1
    bls .sync
    adr_sym r0, g_rba32_job_cnt
    ldr r1, [r0]
    cmp r1, #480
    bhs .sync
    add r2, r1, #1
    str r2, [r0]
    adr_sym r0, g_ua32_arena
    ldr r7, [r0]
    add r7, r7, r1, lsl #10
    mov r0, r7
    add r1, sp, #1568
    bl ua32_strcpy
    add r0, r7, #512
    add r1, sp, #2080
    bl ua32_strcpy
    b .snx
.sync:
    add r0, sp, #1568
    add r1, sp, #2080
    mov r2, #0
    bl ua32_copy_file
    cmp r0, #0
    beq .cerr
    adr_sym r0, g_rba32_copied
    ldr r1, [r0]
    add r1, r1, #1
    str r1, [r0]
    adr_sym r0, g_rba32_status
    ldr r1, [r0]
    orr r1, r1, #1
    str r1, [r0]
    adr_sym r0, g_rba32_mov
    ldr r0, [r0]
    cmp r0, #0
    beq .snx
    add r0, sp, #1568
    bl ua32_delete_file
    b .snx
.cerr:
    adr_sym r0, g_rba32_status
    ldr r1, [r0]
    orr r1, r1, #8
    str r1, [r0]
.snx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .slp
    mov r0, r4
    call_iat __imp_FindClose

.pg:
    adr_sym r0, g_rba32_purge
    ldr r0, [r0]
    cmp r0, #0
    beq .sub
    add r0, sp, #2080
    add r1, sp, #1056
    adr_sym r2, szRba32Star
    bl ua32_path_join
    add r0, sp, #2080
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .sub
    mov r4, r0
.plp:
    ldr r0, [sp]
    tst r0, #0x10
    bne .pnx
    add r0, sp, #1568
    add r1, sp, #544
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #1568
    bl ua32_get_attr
    cmn r0, #1
    bne .pnx
    adr_sym r0, g_rba32_status
    ldr r1, [r0]
    orr r1, r1, #2
    str r1, [r0]
    adr_sym r0, g_rba32_list
    ldr r0, [r0]
    cmp r0, #0
    bne .pnx
    add r0, sp, #2080
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #2080
    bl ua32_delete_file
.pnx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .plp
    mov r0, r4
    call_iat __imp_FindClose

.sub:
    adr_sym r0, g_rba32_sub
    ldr r0, [r0]
    cmp r0, #0
    beq .dn
    add r0, sp, #1568
    add r1, sp, #544
    adr_sym r2, szRba32Star
    bl ua32_path_join
    add r0, sp, #1568
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
    bne .cxd
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .dnx
    cmp r0, #46
    bne .cxd
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .dnx
.cxd:
    adr_sym r0, g_rba32_xd
    ldrb r1, [r0]
    cmp r1, #0
    beq .go
    mov r1, r0
    add r0, sp, #20
    bl ua32_str_eq_ci
    cmp r0, #0
    bne .dnx
.go:
    add r0, sp, #1568
    add r1, sp, #544
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #2080
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #1568
    add r1, sp, #2080
    add r2, r6, #1
    bl rba32_sync_dir
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
    add sp, sp, #3072
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint
;; @out Terminates via ExitProcess with Robocopy bitmask 0..16
;; @err Exits 16 on invalid source/destination
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 ROBOCOPY.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #12
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_rba32_sub
    str r1, [r0]
    adr_sym r0, g_rba32_purge
    str r1, [r0]
    adr_sym r0, g_rba32_mov
    str r1, [r0]
    adr_sym r0, g_rba32_list
    str r1, [r0]
    adr_sym r0, g_rba32_nfl
    str r1, [r0]
    adr_sym r0, g_rba32_njh
    str r1, [r0]
    adr_sym r0, g_rba32_njs
    str r1, [r0]
    adr_sym r0, g_rba32_status
    str r1, [r0]
    adr_sym r0, g_rba32_copied
    str r1, [r0]
    adr_sym r0, g_rba32_job_cnt
    str r1, [r0]
    adr_sym r0, g_rba32_pos
    str r1, [r0]
    adr_sym r0, g_rba32_src
    strb r1, [r0]
    adr_sym r0, g_rba32_dst
    strb r1, [r0]
    adr_sym r0, g_rba32_xf
    strb r1, [r0]
    adr_sym r0, g_rba32_xd
    strb r1, [r0]
    adr_sym r0, g_rba32_mt
    mov r1, #1
    str r1, [r0]
    adr_sym r0, g_rba32_max_lev
    mov r1, #32
    str r1, [r0]
    adr_sym r0, g_rba32_mask
    adr_sym r1, szRba32Star
    bl ua32_strcpy

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
    cmp r0, #83 ; S
    beq .ss
    cmp r0, #69 ; E
    beq .se
    cmp r0, #76 ; L
    beq .sl
    cmp r0, #80 ; P
    beq .sp
    cmp r0, #77 ; M
    beq .sm
    cmp r0, #78 ; N
    beq .sn
    cmp r0, #88 ; X
    beq .sx
    b .arg_lp
.ss:
    ldrb r0, [r5, #2]
    cmp r0, #0
    bne .arg_lp
    adr_sym r0, g_rba32_sub
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.se:
    adr_sym r0, g_rba32_sub
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sl:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #69 ; /LEV:n
    beq .slev
    adr_sym r0, g_rba32_list
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.slev:
    ldrb r0, [r5, #4]
    cmp r0, #58
    bne .arg_lp
    add r0, r5, #5
    bl ua32_parse_u32
    adr_sym r1, g_rba32_max_lev
    str r0, [r1]
    b .arg_lp
.sp:
    adr_sym r0, g_rba32_purge
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sm:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #73 ; /MIR
    beq .smir
    cmp r0, #79 ; /MOV
    beq .smov
    cmp r0, #84 ; /MT
    beq .smt
    b .arg_lp
.smir:
    mov r1, #1
    adr_sym r0, g_rba32_sub
    str r1, [r0]
    adr_sym r0, g_rba32_purge
    str r1, [r0]
    b .arg_lp
.smov:
    adr_sym r0, g_rba32_mov
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.smt:
    adr_sym r0, g_rba32_mt
    mov r1, #8
    str r1, [r0]
    ldrb r0, [r5, #3]
    cmp r0, #58
    bne .arg_lp
    add r0, r5, #4
    bl ua32_parse_u32
    cmp r0, #1
    blo .mt1
    cmp r0, #16
    bls .mts
    mov r0, #16
    b .mts
.mt1:
    mov r0, #1
.mts:
    adr_sym r1, g_rba32_mt
    str r0, [r1]
    b .arg_lp
.sn:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #70
    beq .snfl
    cmp r0, #74
    bne .arg_lp
    ldrb r0, [r5, #3]
    bl ua32_chr_upper
    cmp r0, #72
    beq .snjh
    cmp r0, #83
    beq .snjs
    b .arg_lp
.snfl:
    adr_sym r0, g_rba32_nfl
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.snjh:
    adr_sym r0, g_rba32_njh
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.snjs:
    adr_sym r0, g_rba32_njs
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sx:
    ldrb r0, [r5, #2]
    bl ua32_chr_upper
    cmp r0, #70
    beq .sxf
    cmp r0, #68
    beq .sxd
    b .arg_lp
.sxf:
    mov r0, r4
    adr_sym r1, g_rba32_xf
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.sxd:
    mov r0, r4
    adr_sym r1, g_rba32_xd
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.pos:
    adr_sym r5, g_rba32_pos
    ldr r0, [r5]
    cmp r0, #0
    beq .ps
    cmp r0, #1
    beq .pd
    mov r0, r4
    adr_sym r1, g_rba32_mask
    bl ua32_read_tok
    mov r4, r0
    ldr r0, [r5]
    add r0, r0, #1
    str r0, [r5]
    b .arg_lp
.ps:
    mov r0, r4
    adr_sym r1, g_rba32_src
    bl ua32_read_tok
    mov r4, r0
    mov r0, #1
    str r0, [r5]
    b .arg_lp
.pd:
    mov r0, r4
    adr_sym r1, g_rba32_dst
    bl ua32_read_tok
    mov r4, r0
    mov r0, #2
    str r0, [r5]
    b .arg_lp

.help:
    adr_sym r0, szRba32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    add sp, sp, #12
    pop {r4, r5, r6, r7, lr}
    bx lr

.run:
    adr_sym r0, g_rba32_pos
    ldr r0, [r0]
    cmp r0, #2
    blo .err16
    adr_sym r0, g_rba32_src
    bl ua32_get_attr
    cmn r0, #1
    beq .err16

    adr_sym r0, g_rba32_njh
    ldr r0, [r0]
    cmp r0, #0
    bne .dos
    adr_sym r0, szRba32Ban
    bl ua32_write_str
    bl ua32_write_crlf
.dos:
    adr_sym r0, g_rba32_src
    adr_sym r1, g_rba32_dst
    mov r2, #0
    bl rba32_sync_dir

    adr_sym r0, g_rba32_mt
    ldr r6, [r0]
    cmp r6, #1
    bls .sum
    adr_sym r0, g_rba32_job_cnt
    ldr r1, [r0]
    cmp r1, #0
    beq .sum
    cmp r6, r1
    bls .spw
    mov r6, r1
.spw:
    mov r5, #0
.splp:
    cmp r5, r6
    bhs .waitw
    adr_sym r7, g_rba32_slots
    add r7, r7, r5, lsl #4
    str r5, [r7]
    str r6, [r7, #4]
    mov r0, #0
    str r0, [r7, #8]
    str r0, [r7, #12]

    mov r0, #0
    mov_imm32 r1, 65536
    adr_sym r2, rba32_worker_proc
    orr r2, r2, #1 ; Thumb-2 function pointer LSB = 1
    mov r3, r7
    mov r12, #0
    str r12, [sp]
    adr_sym r12, g_rba32_tid
    str r12, [sp, #4]
    call_iat __imp_CreateThread
    adr_sym r1, g_rba32_threads
    str r0, [r1, r5, lsl #2]
    add r5, r5, #1
    b .splp

.waitw:
    mov r0, r6
    adr_sym r1, g_rba32_threads
    mov r2, #1
    mvn r3, #0
    call_iat __imp_WaitForMultipleObjects

    mov r5, #0
.collp:
    cmp r5, r6
    bhs .sum
    adr_sym r1, g_rba32_threads
    ldr r0, [r1, r5, lsl #2]
    cmp r0, #0
    beq .sc
    call_iat __imp_CloseHandle
.sc:
    adr_sym r7, g_rba32_slots
    add r7, r7, r5, lsl #4
    ldr r0, [r7, #8]
    cmp r0, #0
    beq .cf
    adr_sym r1, g_rba32_copied
    ldr r2, [r1]
    add r2, r2, r0
    str r2, [r1]
    adr_sym r1, g_rba32_status
    ldr r2, [r1]
    orr r2, r2, #1
    str r2, [r1]
.cf:
    ldr r0, [r7, #12]
    cmp r0, #0
    beq .nxc
    adr_sym r1, g_rba32_status
    ldr r2, [r1]
    orr r2, r2, #8
    str r2, [r1]
.nxc:
    add r5, r5, #1
    b .collp

.sum:
    adr_sym r0, g_rba32_njs
    ldr r0, [r0]
    cmp r0, #0
    bne .ex
    adr_sym r0, szRba32Sum
    bl ua32_write_str
    adr_sym r0, g_rba32_copied
    ldr r0, [r0]
    bl ua32_print_u32
    bl ua32_write_crlf
.ex:
    adr_sym r0, g_rba32_status
    ldr r0, [r0]
    bl ua32_exit
    add sp, sp, #12
    pop {r4, r5, r6, r7, lr}
    bx lr
.err16:
    adr_sym r0, szRba32Err
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #16
    bl ua32_exit
    add sp, sp, #12
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

@section rdata

@data db szRba32Star, "*", 0
@data db szRba32Tab, "    ", 0
@data db szRba32Ban, "ROBOCOPY :: Robust File Copy for Windows", 0
@data db szRba32Sum, "Files Copied : ", 0
@data db szRba32Err, "ERROR : Invalid ROBOCOPY Source or Destination Directory.", 0
@data db szRba32Help, "Robust File and Folder Copy (src dst [file] /S /E /MIR /PURGE /MOV /MT[:n] /L /XF /XD).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_rba32_threads, 16
@res dword g_rba32_sub, 1
@res dword g_rba32_purge, 1
@res dword g_rba32_mov, 1
@res dword g_rba32_list, 1
@res dword g_rba32_nfl, 1
@res dword g_rba32_njh, 1
@res dword g_rba32_njs, 1
@res dword g_rba32_mt, 1
@res dword g_rba32_max_lev, 1
@res dword g_rba32_status, 1
@res dword g_rba32_copied, 1
@res dword g_rba32_job_cnt, 1
@res dword g_rba32_pos, 1
@res dword g_rba32_tid, 1
@res byte g_rba32_slots, 256
@res byte g_rba32_src, 520
@res byte g_rba32_dst, 520
@res byte g_rba32_mask, 260
@res byte g_rba32_xf, 260
@res byte g_rba32_xd, 260
