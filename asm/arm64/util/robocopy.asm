;; @file robocopy.asm
;; @isa arm64
;; @desc Standalone ARM64 AArch64 ROBOCOPY.EXE with native CreateThread /MT[:n] worker pool and bitmask ERRORLEVEL 0..16.

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global rba64_worker_proc
@global mainCRTStartup

@include "u_imps_a64.inc"

@section text

@include "u_common_a64.inc"

;; @proc rba64_worker_proc
;; @abi arm64_ms
;; @in x0 = pointer to 16-byte RB_SLOT (0:tid, 4:nthreads, 8:copied, 12:failed)
;; @out w0 = 0
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc PAC/BTI-valid ARM64 worker thread copying its stride of queued file pairs using thread-local UTF-16LE buffers.
@proc rba64_worker_proc
    hint #34
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #4096

    mov x19, x0 ; RB_SLOT*
    ldr w20, [x19] ; tid

.jlp:
    adr_sym x0, g_rba64_job_cnt
    ldr w0, [x0]
    cmp w20, w0
    b.hs .dn

    adr_sym x0, g_ua64_arena
    ldr x21, [x0]
    add x21, x21, x20, lsl #10

    mov x0, x21
    mov x1, sp
    bl ua64_u8_to_w

    add x0, x21, #512
    add x1, sp, #2048
    bl ua64_u8_to_w

    mov x0, sp
    add x1, sp, #2048
    mov w2, #0
    call_iat __imp_CopyFileW
    cbz w0, .fail
    ldr w1, [x19, #8]
    add w1, w1, #1
    str w1, [x19, #8]
    b .nx
.fail:
    ldr w1, [x19, #12]
    add w1, w1, #1
    str w1, [x19, #12]
.nx:
    ldr w1, [x19, #4]
    add w20, w20, w1
    b .jlp
.dn:
    mov w0, #0
    add sp, sp, #4096
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc rba64_sync_dir
;; @abi arm64_ms
;; @in x0 = src dir, x1 = dst dir, w2 = depth
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queues or copies files, purges extras (/PURGE /MIR), moves (/MOV), and recurses (/S /E).
@proc rba64_sync_dir
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #2624

    adr_sym x3, g_rba64_max_lev
    ldr w3, [x3]
    cmp w2, w3
    b.hs .dn
    mov w21, w2 ; depth
    mov x20, x1 ; save dst
    mov x1, x0
    add x0, sp, #544 ; src dir
    bl ua64_strcpy
    add x0, sp, #1056 ; dst dir
    mov x1, x20
    bl ua64_strcpy

    adr_sym x0, g_rba64_list
    ldr w0, [x0]
    cbnz w0, .en
    add x0, sp, #1056
    bl ua64_mkdir

.en:
    add x0, sp, #1568
    add x1, sp, #544
    adr_sym x2, g_rba64_mask
    bl ua64_path_join
    add x0, sp, #1568
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .purge
    mov x19, x0

.slp:
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .nd
    ldrb w0, [sp, #25]
    cbz w0, .snx
    cmp w0, #46
    b.ne .nd
    ldrb w0, [sp, #26]
    cbz w0, .snx
.nd:
    ldr w0, [sp]
    tst w0, #0x10
    b.ne .snx
    adr_sym x1, g_rba64_xf
    ldrb w0, [x1]
    cbz w0, .nxf
    add x0, sp, #24
    bl ua64_str_eq_ci
    cbnz w0, .snx
.nxf:
    add x0, sp, #1568
    add x1, sp, #544
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #2080
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join

    adr_sym x0, g_rba64_nfl
    ldr w0, [x0]
    cbnz w0, .act
    adr_sym x0, szRba64Tab
    bl ua64_write_str
    add x0, sp, #1568
    bl ua64_write_str
    bl ua64_write_crlf

.act:
    adr_sym x0, g_rba64_list
    ldr w0, [x0]
    cbz w0, .real
    adr_sym x0, g_rba64_copied
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]
    adr_sym x0, g_rba64_status
    ldr w1, [x0]
    orr w1, w1, #1
    str w1, [x0]
    b .snx

.real:
    adr_sym x0, g_rba64_mt
    ldr w0, [x0]
    cmp w0, #1
    b.ls .sync
    adr_sym x0, g_rba64_job_cnt
    ldr w1, [x0]
    cmp w1, #480
    b.hs .sync
    adr_sym x2, g_ua64_arena
    ldr x22, [x2]
    add x22, x22, x1, lsl #10
    mov x0, x22
    add x1, sp, #1568
    bl ua64_strcpy
    add x0, x22, #512
    add x1, sp, #2080
    bl ua64_strcpy
    adr_sym x0, g_rba64_job_cnt
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]
    b .snx

.sync:
    add x0, sp, #1568
    add x1, sp, #2080
    mov w2, #0
    bl ua64_copy_file
    cbz w0, .cerr
    adr_sym x0, g_rba64_copied
    ldr w1, [x0]
    add w1, w1, #1
    str w1, [x0]
    adr_sym x0, g_rba64_status
    ldr w1, [x0]
    orr w1, w1, #1
    str w1, [x0]
    adr_sym x0, g_rba64_mov
    ldr w0, [x0]
    cbz w0, .snx
    add x0, sp, #1568
    bl ua64_delete_file
    b .snx
.cerr:
    adr_sym x0, g_rba64_status
    ldr w1, [x0]
    orr w1, w1, #8
    str w1, [x0]

.snx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .slp
    mov x0, x19
    call_iat __imp_FindClose

.purge:
    adr_sym x0, g_rba64_purge
    ldr w0, [x0]
    cbz w0, .sub
    add x0, sp, #2080
    add x1, sp, #1056
    adr_sym x2, szRba64Star
    bl ua64_path_join
    add x0, sp, #2080
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .sub
    mov x19, x0
.plp:
    ldr w0, [sp]
    tst w0, #0x10
    b.ne .pnx
    add x0, sp, #1568
    add x1, sp, #544
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #1568
    bl ua64_get_attr
    cmn w0, #1
    b.ne .pnx
    adr_sym x0, g_rba64_status
    ldr w1, [x0]
    orr w1, w1, #2
    str w1, [x0]
    adr_sym x0, g_rba64_list
    ldr w0, [x0]
    cbnz w0, .pnx
    add x0, sp, #2080
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #2080
    bl ua64_delete_file
.pnx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .plp
    mov x0, x19
    call_iat __imp_FindClose

.sub:
    adr_sym x0, g_rba64_sub
    ldr w0, [x0]
    cbz w0, .dn
    add x0, sp, #1568
    add x1, sp, #544
    adr_sym x2, szRba64Star
    bl ua64_path_join
    add x0, sp, #1568
    mov x1, sp
    bl ua64_find_first
    cmn x0, #1
    b.eq .dn
    mov x19, x0
.dlp:
    ldr w0, [sp]
    tst w0, #0x10
    b.eq .dnx
    ldrb w0, [sp, #24]
    cmp w0, #46
    b.ne .cxd
    ldrb w0, [sp, #25]
    cbz w0, .dnx
    cmp w0, #46
    b.ne .cxd
    ldrb w0, [sp, #26]
    cbz w0, .dnx
.cxd:
    adr_sym x1, g_rba64_xd
    ldrb w0, [x1]
    cbz w0, .go
    add x0, sp, #24
    bl ua64_str_eq_ci
    cbnz w0, .dnx
.go:
    add x0, sp, #1568
    add x1, sp, #544
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #2080
    add x1, sp, #1056
    add x2, sp, #24
    bl ua64_path_join
    add x0, sp, #1568
    add x1, sp, #2080
    add w2, w21, #1
    bl rba64_sync_dir
.dnx:
    mov x0, x19
    mov x1, sp
    bl ua64_find_next
    cbnz w0, .dlp
    mov x0, x19
    call_iat __imp_FindClose
.dn:
    add sp, sp, #2624
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc mainCRTStartup
;; @abi arm64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess with Robocopy bitmask 0..16
;; @err Exits 16 on missing/invalid source or destination
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Entrypoint for ARM64 ROBOCOPY.EXE.
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
    adr_sym x0, g_rba64_sub
    str wzr, [x0]
    adr_sym x0, g_rba64_purge
    str wzr, [x0]
    adr_sym x0, g_rba64_mov
    str wzr, [x0]
    adr_sym x0, g_rba64_list
    str wzr, [x0]
    adr_sym x0, g_rba64_nfl
    str wzr, [x0]
    adr_sym x0, g_rba64_njh
    str wzr, [x0]
    adr_sym x0, g_rba64_njs
    str wzr, [x0]
    adr_sym x0, g_rba64_mt
    mov w1, #1
    str w1, [x0]
    adr_sym x0, g_rba64_max_lev
    mov w1, #32
    str w1, [x0]
    adr_sym x0, g_rba64_status
    str wzr, [x0]
    adr_sym x0, g_rba64_copied
    str wzr, [x0]
    adr_sym x0, g_rba64_job_cnt
    str wzr, [x0]
    adr_sym x0, g_rba64_pos
    str wzr, [x0]
    adr_sym x0, g_rba64_src
    strb wzr, [x0]
    adr_sym x0, g_rba64_dst
    strb wzr, [x0]
    adr_sym x0, g_rba64_xf
    strb wzr, [x0]
    adr_sym x0, g_rba64_xd
    strb wzr, [x0]
    adr_sym x0, g_rba64_mask
    adr_sym x1, szRba64Star
    bl ua64_strcpy

.arg_lp:
    mov x0, x19
    bl ua64_skip_ws
    mov x19, x0
    cbz w1, .run
    cmp w1, #47
    b.ne .pos
    adr_sym x1, g_ua64_arg_a
    bl ua64_read_tok
    mov x19, x0
    adr_sym x20, g_ua64_arg_a
    ldrb w0, [x20, #1]
    cmp w0, #63
    b.eq .help
    bl ua64_chr_upper
    cmp w0, #83 ; S
    b.eq .ss
    cmp w0, #69 ; E
    b.eq .se
    cmp w0, #76 ; L
    b.eq .sl
    cmp w0, #80 ; P
    b.eq .sp_sw
    cmp w0, #77 ; M
    b.eq .sm
    cmp w0, #78 ; N
    b.eq .sn
    cmp w0, #88 ; X
    b.eq .sx
    b .arg_lp
.ss:
    ldrb w0, [x20, #2]
    cbnz w0, .arg_lp
    adr_sym x0, g_rba64_sub
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.se:
    adr_sym x0, g_rba64_sub
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sl:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #69 ; E
    b.eq .slev
    adr_sym x0, g_rba64_list
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.slev:
    ldrb w0, [x20, #4]
    cmp w0, #58 ; :
    b.ne .arg_lp
    add x0, x20, #5
    bl ua64_parse_u32
    adr_sym x1, g_rba64_max_lev
    str w0, [x1]
    b .arg_lp
.sp_sw:
    adr_sym x0, g_rba64_purge
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sm:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #73 ; I
    b.eq .smir
    cmp w0, #79 ; O
    b.eq .smov
    cmp w0, #84 ; T
    b.eq .smt
    b .arg_lp
.smir:
    adr_sym x0, g_rba64_sub
    mov w1, #1
    str w1, [x0]
    adr_sym x0, g_rba64_purge
    str w1, [x0]
    b .arg_lp
.smov:
    adr_sym x0, g_rba64_mov
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.smt:
    mov w0, #8
    ldrb w1, [x20, #3]
    cmp w1, #58 ; :
    b.ne .mts
    add x0, x20, #4
    bl ua64_parse_u32
    cmp w0, #1
    b.lo .mt1
    cmp w0, #16
    b.ls .mts
    mov w0, #16
    b .mts
.mt1:
    mov w0, #1
.mts:
    adr_sym x1, g_rba64_mt
    str w0, [x1]
    b .arg_lp
.sn:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #70 ; F
    b.eq .snfl
    cmp w0, #74 ; J
    b.ne .arg_lp
    ldrb w0, [x20, #3]
    bl ua64_chr_upper
    cmp w0, #72 ; H
    b.eq .snjh
    cmp w0, #83 ; S
    b.eq .snjs
    b .arg_lp
.snfl:
    adr_sym x0, g_rba64_nfl
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.snjh:
    adr_sym x0, g_rba64_njh
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.snjs:
    adr_sym x0, g_rba64_njs
    mov w1, #1
    str w1, [x0]
    b .arg_lp
.sx:
    ldrb w0, [x20, #2]
    bl ua64_chr_upper
    cmp w0, #70 ; F
    b.eq .sxf
    cmp w0, #68 ; D
    b.eq .sxd
    b .arg_lp
.sxf:
    mov x0, x19
    adr_sym x1, g_rba64_xf
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.sxd:
    mov x0, x19
    adr_sym x1, g_rba64_xd
    bl ua64_read_tok
    mov x19, x0
    b .arg_lp
.pos:
    adr_sym x20, g_rba64_pos
    ldr w0, [x20]
    cbz w0, .ps
    cmp w0, #1
    b.eq .pd
    mov x0, x19
    adr_sym x1, g_rba64_mask
    bl ua64_read_tok
    mov x19, x0
    ldr w0, [x20]
    add w0, w0, #1
    str w0, [x20]
    b .arg_lp
.ps:
    mov x0, x19
    adr_sym x1, g_rba64_src
    bl ua64_read_tok
    mov x19, x0
    mov w0, #1
    str w0, [x20]
    b .arg_lp
.pd:
    mov x0, x19
    adr_sym x1, g_rba64_dst
    bl ua64_read_tok
    mov x19, x0
    mov w0, #2
    str w0, [x20]
    b .arg_lp

.help:
    adr_sym x0, szRba64Help
    bl ua64_write_str
    bl ua64_write_crlf
    mov w0, #0
    bl ua64_exit
    b .out

.run:
    adr_sym x0, g_rba64_pos
    ldr w0, [x0]
    cmp w0, #2
    b.lo .err16
    adr_sym x0, g_rba64_src
    bl ua64_get_attr
    cmn w0, #1
    b.eq .err16

    adr_sym x0, g_rba64_njh
    ldr w0, [x0]
    cbnz w0, .do_s
    adr_sym x0, szRba64Ban
    bl ua64_write_str
    bl ua64_write_crlf

.do_s:
    adr_sym x0, g_rba64_src
    adr_sym x1, g_rba64_dst
    mov w2, #0
    bl rba64_sync_dir

    adr_sym x0, g_rba64_mt
    ldr w21, [x0]
    cmp w21, #1
    b.ls .sum
    adr_sym x0, g_rba64_job_cnt
    ldr w0, [x0]
    cbz w0, .sum

    cmp w21, w0
    b.ls .spw
    mov w21, w0
.spw:
    mov w22, #0
.sp_lp:
    cmp w22, w21
    b.hs .wait_w
    adr_sym x23, g_rba64_slots
    add x23, x23, x22, lsl #4
    str w22, [x23]
    str w21, [x23, #4]
    str wzr, [x23, #8]
    str wzr, [x23, #12]

    mov x0, #0 ; lpThreadAttributes
    mov_imm32 x1, 65536 ; dwStackSize
    adr_sym x2, rba64_worker_proc
    mov x3, x23 ; lpParameter
    mov w4, #0 ; dwCreationFlags
    adr_sym x5, g_rba64_tid
    call_iat __imp_CreateThread
    adr_sym x1, g_rba64_threads
    str x0, [x1, x22, lsl #3]
    add w22, w22, #1
    b .sp_lp

.wait_w:
    mov w0, w21
    adr_sym x1, g_rba64_threads
    mov w2, #1 ; bWaitAll
    mov w3, #-1 ; INFINITE
    call_iat __imp_WaitForMultipleObjects

    mov w22, #0
.col_lp:
    cmp w22, w21
    b.hs .sum
    adr_sym x1, g_rba64_threads
    ldr x0, [x1, x22, lsl #3]
    cbz x0, .sc
    call_iat __imp_CloseHandle
.sc:
    adr_sym x23, g_rba64_slots
    add x23, x23, x22, lsl #4
    ldr w0, [x23, #8]
    cbz w0, .cf
    adr_sym x1, g_rba64_copied
    ldr w2, [x1]
    add w2, w2, w0
    str w2, [x1]
    adr_sym x1, g_rba64_status
    ldr w2, [x1]
    orr w2, w2, #1
    str w2, [x1]
.cf:
    ldr w0, [x23, #12]
    cbz w0, .nxc
    adr_sym x1, g_rba64_status
    ldr w2, [x1]
    orr w2, w2, #8
    str w2, [x1]
.nxc:
    add w22, w22, #1
    b .col_lp

.sum:
    adr_sym x0, g_rba64_njs
    ldr w0, [x0]
    cbnz w0, .ex
    adr_sym x0, szRba64Sum
    bl ua64_write_str
    adr_sym x0, g_rba64_copied
    ldr w0, [x0]
    bl ua64_print_u64
    bl ua64_write_crlf
.ex:
    adr_sym x0, g_rba64_status
    ldr w0, [x0]
    bl ua64_exit
    b .out

.err16:
    adr_sym x0, szRba64Err
    bl ua64_write_err_str
    bl ua64_write_crlf
    mov w0, #16
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

@data db szRba64Star, "*", 0
@data db szRba64Tab, "    ", 0
@data db szRba64Ban, "ROBOCOPY :: Robust File Copy for Windows", 0
@data db szRba64Sum, "Files Copied : ", 0
@data db szRba64Err, "ERROR : Invalid ROBOCOPY Source or Destination Directory.", 0
@data db szRba64Help, "Robust File and Folder Copy (src dst [file] /S /E /MIR /PURGE /MOV /MT[:n] /L /XF /XD).", 0

@include "u_rdata_a64.inc"

@section bss

@include "u_bss_a64.inc"
@res qword g_rba64_threads, 16
@res dword g_rba64_sub, 1
@res dword g_rba64_purge, 1
@res dword g_rba64_mov, 1
@res dword g_rba64_list, 1
@res dword g_rba64_nfl, 1
@res dword g_rba64_njh, 1
@res dword g_rba64_njs, 1
@res dword g_rba64_mt, 1
@res dword g_rba64_max_lev, 1
@res dword g_rba64_status, 1
@res dword g_rba64_copied, 1
@res dword g_rba64_job_cnt, 1
@res dword g_rba64_pos, 1
@res dword g_rba64_tid, 1
@res byte g_rba64_slots, 256
@res byte g_rba64_src, 520
@res byte g_rba64_dst, 520
@res byte g_rba64_mask, 260
@res byte g_rba64_xf, 260
@res byte g_rba64_xd, 260
