;; @file where.asm
;; @isa armv7
;; @desc Standalone ARM32 Thumb-2 WHERE.EXE executable locator ([/R dir] [/Q] [/F] [/T] pattern / $ENV:pattern).

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps_a32.inc"

@section text

@include "u_common_a32.inc"

;; @proc wherea32_emit_hit
;; @abi arm_nt
;; @in r0 = matched path UTF-8, r1 = low32 size, r2 = high32 size
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Records hit and prints path (honoring /Q, /F, /T).
@proc wherea32_emit_hit
    push {r4, lr}
    mov r4, r0
    adr_sym r0, g_wha32_found
    mov r3, #1
    str r3, [r0]
    adr_sym r0, g_wha32_quiet
    ldr r0, [r0]
    cmp r0, #0
    bne .dn
    adr_sym r0, g_wha32_time
    ldr r0, [r0]
    cmp r0, #0
    beq .cq
    mov r0, r1
    mov r1, r2
    bl ua32_print_u64
    adr_sym r0, szWha32Pad
    bl ua32_write_str
.cq:
    adr_sym r0, g_wha32_quote
    ldr r0, [r0]
    cmp r0, #0
    beq .pl
    mov r0, #34
    bl ua32_write_chr
    mov r0, r4
    bl ua32_write_str
    mov r0, #34
    bl ua32_write_chr
    bl ua32_write_crlf
    pop {r4, lr}
    bx lr
.pl:
    mov r0, r4
    bl ua32_write_str
    bl ua32_write_crlf
.dn:
    pop {r4, lr}
    bx lr
@endproc

;; @proc wherea32_probe_one
;; @abi arm_nt
;; @in r0 = candidate path/wildcard, r1 = parent dir
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Enumerates matches for r0 in directory r1.
@proc wherea32_probe_one
    push {r4, r5, lr}
    sub sp, sp, #1024
    sub sp, sp, #68 ; 1092 + 12 = 1104 (8B aligned)
    mov r5, r1
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .dn
    mov r4, r0
.lp:
    ldr r0, [sp]
    tst r0, #0x10
    bne .nx
    add r0, sp, #544
    mov r1, r5
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #544
    ldr r1, [sp, #16]
    ldr r2, [sp, #12]
    bl wherea32_emit_hit
.nx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .lp
    mov r0, r4
    call_iat __imp_FindClose
.dn:
    add sp, sp, #68
    add sp, sp, #1024
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc wherea32_probe_dir
;; @abi arm_nt
;; @in r0 = dir UTF-8, r1 = pattern UTF-8
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Probes exact pattern and .COM/.EXE/.BAT/.CMD extensions in r0.
@proc wherea32_probe_dir
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #1024
    sub sp, sp, #4
    mov r4, r0 ; dir
    mov r5, r1 ; pat

    mov r2, #0
    mov r6, #0
.dot:
    ldrb r0, [r5, r2]
    cmp r0, #0
    beq .ex
    cmp r0, #46
    bne .dnx
    mov r6, #1
.dnx:
    add r2, r2, #1
    b .dot

.ex:
    mov r0, sp
    mov r1, r4
    mov r2, r5
    bl ua32_path_join
    mov r0, sp
    mov r1, r4
    bl wherea32_probe_one

    cmp r6, #0
    bne .dn

    mov r0, sp
    mov r1, r4
    mov r2, r5
    bl ua32_path_join
    mov r0, sp
    bl ua32_strlen
    mov r7, r0

    add r0, sp, r7
    adr_sym r1, szWha32Com
    bl ua32_strcpy
    mov r0, sp
    mov r1, r4
    bl wherea32_probe_one

    add r0, sp, r7
    adr_sym r1, szWha32Exe
    bl ua32_strcpy
    mov r0, sp
    mov r1, r4
    bl wherea32_probe_one

    add r0, sp, r7
    adr_sym r1, szWha32Bat
    bl ua32_strcpy
    mov r0, sp
    mov r1, r4
    bl wherea32_probe_one

    add r0, sp, r7
    adr_sym r1, szWha32Cmd
    bl ua32_strcpy
    mov r0, sp
    mov r1, r4
    bl wherea32_probe_one
.dn:
    add sp, sp, #4
    add sp, sp, #1024
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc wherea32_recurse
;; @abi arm_nt
;; @in r0 = dir, r1 = pat, r2 = depth
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Recursively searches directory tree (/R dir).
@proc wherea32_recurse
    push {r4, r5, r6, lr}
    sub sp, sp, #2048
    cmp r2, #16
    bhs .dn
    mov r5, r1
    mov r6, r2
    mov r1, r0
    add r0, sp, #1056
    bl ua32_strcpy

    add r0, sp, #1056
    mov r1, r5
    bl wherea32_probe_dir

    add r0, sp, #544
    add r1, sp, #1056
    adr_sym r2, szWha32Star
    bl ua32_path_join
    add r0, sp, #544
    mov r1, sp
    bl ua32_find_first
    cmn r0, #1
    beq .dn
    mov r4, r0
.lp:
    ldr r0, [sp]
    tst r0, #0x10
    beq .nx
    ldrb r0, [sp, #20]
    cmp r0, #46
    bne .go
    ldrb r0, [sp, #21]
    cmp r0, #0
    beq .nx
    cmp r0, #46
    bne .go
    ldrb r0, [sp, #22]
    cmp r0, #0
    beq .nx
.go:
    add r0, sp, #544
    add r1, sp, #1056
    add r2, sp, #20
    bl ua32_path_join
    add r0, sp, #544
    mov r1, r5
    add r2, r6, #1
    bl wherea32_recurse
.nx:
    mov r0, r4
    mov r1, sp
    bl ua32_find_next
    cmp r0, #0
    bne .lp
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
;; @err Exits 2 on missing pattern
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Entrypoint for ARM32 WHERE.EXE.
@proc mainCRTStartup
    push {r4, r5, r6, lr}
    bl ua32_init
    mov r4, r0
    mov r1, #0
    adr_sym r0, g_wha32_quiet
    str r1, [r0]
    adr_sym r0, g_wha32_quote
    str r1, [r0]
    adr_sym r0, g_wha32_time
    str r1, [r0]
    adr_sym r0, g_wha32_rec
    str r1, [r0]
    adr_sym r0, g_wha32_found
    str r1, [r0]
    adr_sym r0, g_wha32_pat
    strb r1, [r0]
    adr_sym r0, g_wha32_rdir
    strb r1, [r0]

.arg_lp:
    mov r0, r4
    bl ua32_skip_ws
    mov r4, r0
    cmp r1, #0
    beq .run
    cmp r1, #47
    bne .pat
    adr_sym r1, g_ua32_arg_a
    bl ua32_read_tok
    mov r4, r0
    adr_sym r5, g_ua32_arg_a
    ldrb r0, [r5, #1]
    cmp r0, #63
    beq .help
    bl ua32_chr_upper
    cmp r0, #81 ; Q
    beq .sq
    cmp r0, #70 ; F
    beq .sf
    cmp r0, #84 ; T
    beq .st
    cmp r0, #82 ; R
    beq .sr
    b .bad
.sq:
    adr_sym r0, g_wha32_quiet
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sf:
    adr_sym r0, g_wha32_quote
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.st:
    adr_sym r0, g_wha32_time
    mov r1, #1
    str r1, [r0]
    b .arg_lp
.sr:
    adr_sym r0, g_wha32_rec
    mov r1, #1
    str r1, [r0]
    mov r0, r4
    adr_sym r1, g_wha32_rdir
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp
.pat:
    adr_sym r1, g_wha32_pat
    bl ua32_read_tok
    mov r4, r0
    b .arg_lp

.help:
    adr_sym r0, szWha32Help
    bl ua32_write_str
    bl ua32_write_crlf
    mov r0, #0
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr
.bad:
    adr_sym r0, szWha32Syn
    bl ua32_write_err_str
    bl ua32_write_crlf
    mov r0, #2
    bl ua32_exit
    pop {r4, r5, r6, lr}
    bx lr

.run:
    adr_sym r5, g_wha32_pat
    ldrb r0, [r5]
    cmp r0, #0
    beq .bad

    adr_sym r0, g_wha32_rec
    ldr r0, [r0]
    cmp r0, #0
    beq .cenv
    adr_sym r0, g_wha32_rdir
    mov r1, r5
    mov r2, #0
    bl wherea32_recurse
    b .fin

.cenv:
    ldrb r0, [r5]
    cmp r0, #36 ; $
    bne .std
    add r0, r5, #1
    adr_sym r1, g_ua32_arg_a
    mov r2, #0
.fc:
    ldrb r3, [r0, r2]
    cmp r3, #0
    beq .std
    cmp r3, #58 ; :
    beq .gc
    strb r3, [r1, r2]
    add r2, r2, #1
    b .fc
.gc:
    mov r3, #0
    strb r3, [r1, r2]
    add r1, r0, r2
    add r1, r1, #1
    adr_sym r0, g_ua32_arg_b
    bl ua32_strcpy
    mov r0, r5
    adr_sym r1, g_ua32_arg_b
    bl ua32_strcpy
    adr_sym r0, g_ua32_arg_a
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 32768
    bl ua32_get_env
    b .walk

.std:
    adr_sym r0, g_ua32_arg_c
    mov r1, #1000
    bl ua32_get_cwd
    adr_sym r0, g_ua32_arg_c
    mov r1, r5
    bl wherea32_probe_dir

    adr_sym r0, szWha32Path
    adr_sym r1, g_ua32_arena
    ldr r1, [r1]
    mov_imm32 r2, 32768
    bl ua32_get_env

.walk:
    adr_sym r0, g_ua32_arena
    ldr r4, [r0]
.slp:
    ldrb r0, [r4]
    cmp r0, #0
    beq .fin
    adr_sym r6, g_ua32_arg_c
.cps:
    ldrb r0, [r4]
    cmp r0, #0
    beq .gs
    cmp r0, #59 ; ;
    beq .as
    strb r0, [r6]
    add r4, r4, #1
    add r6, r6, #1
    b .cps
.as:
    add r4, r4, #1
.gs:
    mov r0, #0
    strb r0, [r6]
    adr_sym r0, g_ua32_arg_c
    ldrb r1, [r0]
    cmp r1, #0
    beq .slp
    mov r1, r5
    bl wherea32_probe_dir
    b .slp

.fin:
    adr_sym r0, g_wha32_found
    ldr r0, [r0]
    cmp r0, #0
    bne .ex0
    adr_sym r0, g_wha32_quiet
    ldr r0, [r0]
    cmp r0, #0
    bne .ex1
    adr_sym r0, szWha32Nf
    bl ua32_write_err_str
    bl ua32_write_crlf
.ex1:
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

@data db szWha32Path, "PATH", 0
@data db szWha32Star, "*", 0
@data db szWha32Com, ".COM", 0
@data db szWha32Exe, ".EXE", 0
@data db szWha32Bat, ".BAT", 0
@data db szWha32Cmd, ".CMD", 0
@data db szWha32Pad, "   ", 0
@data db szWha32Nf, "INFO: Could not find files for the given pattern(s).", 0
@data db szWha32Syn, "ERROR: Invalid syntax.", 0
@data db szWha32Help, "Displays the location of files that match the search pattern (/R /Q /F /T).", 0

@include "u_rdata_a32.inc"

@section bss

@include "u_bss_a32.inc"
@res dword g_wha32_quiet, 1
@res dword g_wha32_quote, 1
@res dword g_wha32_time, 1
@res dword g_wha32_rec, 1
@res dword g_wha32_found, 1
@res byte g_wha32_pat, 520
@res byte g_wha32_rdir, 520
