;; @file stream_ed.inc
;; @isa armv4
;; @desc ARM32 stateless byte-offset BatchFrameReader (UTF-8 BOM, UTF-16LE/BE, 0x1A EOF, caret folding) & DOSKEY line reader.

@cpu armv4
@format inc

;; @proc batch_read_raw_line_a32
;; @abi arm_ce
;; @in r0 = BATCH_FRAME_A32*, r1 = output WTF-8 buffer (A32_LINE_MAX)
;; @out r0 = 1 if line read, 0 on EOF
;; @err r0 = 0 on EOF or file open error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Opens batch file, seeks to uFileOff, reads one physical line (UTF-8/OEM or UTF-16LE/BE), updates uFileOff, and closes handle!
@proc batch_read_raw_line_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #528
    mov r4, r0 ; frame
    mov r5, r1 ; out buf

    ldr r0, [r4, #BATCH_FRAME_A32.pFilePath]
    bl to_u16_longpath_a32
    cmp r0, #0
    beq .eof
    mov r1, #0 ; read mode
    bl hal_open_file
    cmn r0, #1
    beq .eof
    mov r6, r0 ; hFile

    ; Detect BOM on first read at offset 0
    ldr r1, [r4, #BATCH_FRAME_A32.uFileOffLo]
    ldr r2, [r4, #BATCH_FRAME_A32.uFileOffHi]
    orrs r3, r1, r2
    bne .seek_pos
    ldrb r3, [r4, #BATCH_FRAME_A32.bBomKind]
    cmp r3, #0
    bne .seek_pos

    mov r0, r6
    mov r1, sp
    mov r2, #4
    bl hal_read_handle
    cmp r0, #2
    bcc .reset_0
    ldrb r1, [sp, #0]
    ldrb r2, [sp, #1]
    cmp r1, #0xFF
    bne .chk_be_bom
    cmp r2, #0xFE
    bne .chk_be_bom
    mov r3, #2 ; UTF-16LE
    strb r3, [r4, #BATCH_FRAME_A32.bBomKind]
    mov r3, #2
    str r3, [r4, #BATCH_FRAME_A32.uFileOffLo]
    b .seek_pos
.chk_be_bom:
    cmp r1, #0xFE
    bne .chk_u8_bom
    cmp r2, #0xFF
    bne .chk_u8_bom
    mov r3, #3 ; UTF-16BE
    strb r3, [r4, #BATCH_FRAME_A32.bBomKind]
    mov r3, #2
    str r3, [r4, #BATCH_FRAME_A32.uFileOffLo]
    b .seek_pos
.chk_u8_bom:
    cmp r0, #3
    bcc .reset_0
    cmp r1, #0xEF
    bne .reset_0
    cmp r2, #0xBB
    bne .reset_0
    ldrb r3, [sp, #2]
    cmp r3, #0xBF
    bne .reset_0
    mov r3, #1 ; UTF-8 BOM
    strb r3, [r4, #BATCH_FRAME_A32.bBomKind]
    mov r3, #3
    str r3, [r4, #BATCH_FRAME_A32.uFileOffLo]
    b .seek_pos
.reset_0:
    mov r3, #1
    strb r3, [r4, #BATCH_FRAME_A32.bBomKind]

.seek_pos:
    mov r0, r6
    ldr r1, [r4, #BATCH_FRAME_A32.uFileOffLo]
    ldr r2, [r4, #BATCH_FRAME_A32.uFileOffHi]
    mov r3, #0 ; FILE_BEGIN
    bl hal_seek_file

    ; Read up to 512 bytes chunk-by-chunk (or full line up to A32_LINE_MAX)
    ldrb r7, [r4, #BATCH_FRAME_A32.bBomKind]
    cmp r7, #2
    bhs .read_u16_mode

    ; Byte-oriented UTF-8 / OEM mode (exact byte offset tracking!)
    mov r8, #0 ; out len
    mov r9, #0 ; raw bytes consumed
    mov r10, #0 ; got any byte

.byte_chunk:
    mov r0, r6
    mov r1, sp
    mov r2, #512
    bl hal_read_handle
    cmp r0, #0
    beq .byte_done
    mov r7, r0 ; chunk len
    mov r3, #0 ; chunk idx

.byte_scan:
    cmp r3, r7
    bhs .byte_chunk
    ldrb r0, [sp, r3]
    cmp r0, #0x1A ; Ctrl+Z immediate DOS/ASCII EOF!
    beq .ctrl_z
    mov r10, #1
    add r3, r3, #1
    add r9, r9, #1
    cmp r0, #0x0A ; LF
    beq .byte_done
    cmp r0, #0x0D ; CR
    beq .on_cr
    mov_imm32 r1, 16380
    cmp r8, r1
    bhs .byte_scan
    strb r0, [r5, r8]
    add r8, r8, #1
    b .byte_scan

.on_cr:
    ; Peek next byte for CRLF
    cmp r3, r7
    bcc .peek_in_buf
    ; Read 1 extra byte to check LF
    mov r0, r6
    mov r1, sp
    mov r2, #1
    bl hal_read_handle
    cmp r0, #0
    beq .byte_done
    ldrb r0, [sp, #0]
    cmp r0, #0x0A
    bne .byte_done
    add r9, r9, #1
    b .byte_done
.peek_in_buf:
    ldrb r0, [sp, r3]
    cmp r0, #0x0A
    bne .byte_done
    add r9, r9, #1
    b .byte_done

.ctrl_z:
    ; Advance past 0x1A so subsequent reads stay at EOF
    cmp r10, #0
    bne .byte_done
    mov r0, r6
    bl hal_close_handle
    b .eof

.byte_done:
    mov r0, r6
    bl hal_close_handle
    cmp r10, #0
    beq .eof
    mov r0, #0
    strb r0, [r5, r8]
    ldr r0, [r4, #BATCH_FRAME_A32.uFileOffLo]
    ldr r1, [r4, #BATCH_FRAME_A32.uFileOffHi]
    adds r0, r0, r9
    adc r1, r1, #0
    str r0, [r4, #BATCH_FRAME_A32.uFileOffLo]
    str r1, [r4, #BATCH_FRAME_A32.uFileOffHi]
    mov r0, #1
    add sp, sp, #528
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.read_u16_mode:
    ; UTF-16LE (r7=2) or UTF-16BE (r7=3): read 2 bytes per code unit, tracking exact physical bytes!
    mov r8, r5 ; out cursor
    mov r9, #0 ; raw bytes consumed
    mov r10, #0 ; got any unit
.u16_lp:
    mov r0, r6
    mov r1, sp
    mov r2, #2
    bl hal_read_handle
    cmp r0, #2
    bcc .u16_done
    mov r10, #1
    add r9, r9, #2
    ldrb r0, [sp, #0]
    ldrb r1, [sp, #1]
    cmp r7, #3
    beq .swap_be
    lsl r1, r1, #8
    orr r0, r0, r1
    b .got_u16c
.swap_be:
    lsl r0, r0, #8
    orr r0, r0, r1
.got_u16c:
    cmp r0, #0x000A
    beq .u16_done
    cmp r0, #0x000D
    bne .emit_u16c
    ; Peek next WCHAR for 0x000A
    mov r0, r6
    mov r1, sp
    mov r2, #2
    bl hal_read_handle
    cmp r0, #2
    bcc .u16_done
    ldrb r0, [sp, #0]
    ldrb r1, [sp, #1]
    cmp r7, #3
    beq .be_lf
    lsl r1, r1, #8
    orr r0, r0, r1
    b .chk_u16lf
.be_lf:
    lsl r0, r0, #8
    orr r0, r0, r1
.chk_u16lf:
    cmp r0, #0x000A
    bne .u16_done
    add r9, r9, #2
    b .u16_done

.emit_u16c:
    ; Encode code unit r0 into WTF-8 at r8
    strh r0, [sp, #4]
    mov r1, #0
    strh r1, [sp, #6]
    add r0, sp, #4
    mov r1, #1
    mov r2, r8
    bl u16_to_wtf8_buf_a32
    add r8, r8, r0
    b .u16_lp

.u16_done:
    mov r0, r6
    bl hal_close_handle
    cmp r10, #0
    beq .eof
    mov r0, #0
    strb r0, [r8, #0]
    ldr r0, [r4, #BATCH_FRAME_A32.uFileOffLo]
    ldr r1, [r4, #BATCH_FRAME_A32.uFileOffHi]
    adds r0, r0, r9
    adc r1, r1, #0
    str r0, [r4, #BATCH_FRAME_A32.uFileOffLo]
    str r1, [r4, #BATCH_FRAME_A32.uFileOffHi]
    mov r0, #1
    add sp, sp, #528
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.eof:
    mov r0, #0
    strb r0, [r5, #0]
    add sp, sp, #528
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc batch_read_logical_line_a32
;; @abi arm_ce
;; @in r0 = BATCH_FRAME_A32*, r1 = output WTF-8 buffer (A32_LINE_MAX)
;; @out r0 = 1 if logical line read, 0 on EOF
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reads a logical batch line, folding trailing unescaped `^` + newline continuations when extensions enabled.
@proc batch_read_logical_line_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1

    bl batch_read_raw_line_a32
    cmp r0, #0
    beq .done

.chk_caret:
    bl hal_get_ctx
    ldrb r0, [r0, #ARM32_CTX.bExtensions]
    cmp r0, #0
    beq .ok_line

    mov r0, r5
    bl str_len_a32
    mov r6, r0
    cmp r6, #0
    beq .ok_line

    ; Count trailing consecutive `^` characters: odd count means line continuation!
    mov r7, #0
    mov r1, r6
.cnt_car:
    cmp r1, #0
    beq .eval_odd
    sub r1, r1, #1
    ldrb r2, [r5, r1]
    cmp r2, #0x5E ; '^'
    bne .eval_odd
    add r7, r7, #1
    b .cnt_car
.eval_odd:
    tst r7, #1
    beq .ok_line

    ; Strip the final `^` and append next physical line!
    sub r6, r6, #1
    mov r2, #0
    strb r2, [r5, r6]
    mov r0, r4
    add r1, r5, r6
    bl batch_read_raw_line_a32
    cmp r0, #0
    bne .chk_caret

.ok_line:
    mov r0, #1
.done:
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc doskey_add_history_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated command line
;; @out Appends non-empty line to DOSKEY history ring (up to A32_HIST_MAX)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Stores an interactive line in ARM32_CTX.pHistRing.
@proc doskey_add_history_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    ldrb r1, [r4, #0]
    cmp r1, #0
    beq .done
    bl hal_get_ctx
    mov r5, r0
    ldr r6, [r5, #ARM32_CTX.dwHistCount]
    cmp r6, #A32_HIST_MAX
    bhs .done
    mov r0, r4
    bl str_len_a32
    add r0, r0, #2
    bl perm_alloc_a32
    cmp r0, #0
    beq .done
    ldr r1, [r5, #ARM32_CTX.pHistRing]
    lsl r2, r6, #2
    str r0, [r1, r2]
    add r6, r6, #1
    str r6, [r5, #ARM32_CTX.dwHistCount]
    mov r1, r4
    bl str_copy_a32
.done:
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc doskey_expand_macro_a32
;; @abi arm_ce
;; @in r0 = input interactive line
;; @out r0 = expanded line (or original r0 if first token is not a registered DOSKEY macro)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Expands registered DOSKEY macros including `$1`..`$9`, `$*`, and `$T` (`&`).
@proc doskey_expand_macro_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0
    ; Skip leading whitespace
.sk_ws:
    ldrb r0, [r4, #0]
    cmp r0, #0x20
    beq .adv_ws
    cmp r0, #0x09
    beq .adv_ws
    b .got_tok
.adv_ws:
    add r4, r4, #1
    b .sk_ws
.got_tok:
    mov r5, #0 ; name len
.nm_lp:
    ldrb r0, [r4, r5]
    cmp r0, #0
    beq .lookup
    cmp r0, #0x20
    beq .lookup
    cmp r0, #0x09
    beq .lookup
    add r5, r5, #1
    b .nm_lp

.lookup:
    cmp r5, #0
    beq .ret_orig
    bl hal_get_ctx
    mov r6, r0
    ldr r7, [r6, #ARM32_CTX.dwMacroCount]
    mov r8, #0
.m_lp:
    cmp r8, r7
    bhs .ret_orig
    ldr r0, [r6, #ARM32_CTX.pMacroNames]
    lsl r1, r8, #2
    ldr r9, [r0, r1]
    mov r0, r9
    bl str_len_a32
    cmp r0, r5
    bne .m_nx
    mov r0, r9
    mov r1, r4
    mov r2, r5
    bl str_ncmp_ci_a32
    cmp r0, #0
    beq .found_macro
.m_nx:
    add r8, r8, #1
    b .m_lp

.found_macro:
    ldr r0, [r6, #ARM32_CTX.pMacroBodies]
    lsl r1, r8, #2
    ldr r9, [r0, r1] ; body template
    add r10, r4, r5  ; raw args tail
.sk_arg_ws:
    ldrb r0, [r10, #0]
    cmp r0, #0x20
    bne .alloc_exp
    add r10, r10, #1
    b .sk_arg_ws

.alloc_exp:
    mov_imm32 r0, A32_LINE_MAX
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_orig
    mov r6, r0 ; out base
    mov r7, r0 ; out cursor

.b_lp:
    ldrb r0, [r9, #0]
    cmp r0, #0
    beq .exp_done
    cmp r0, #0x24 ; '$'
    bne .b_lit
    ldrb r1, [r9, #1]
    cmp r1, #0x2A ; '$*'
    beq .b_all
    cmp r1, #0x54 ; '$T'
    beq .b_sep
    cmp r1, #0x74 ; '$t'
    beq .b_sep
    cmp r1, #0x31
    bcc .b_lit
    cmp r1, #0x39
    bhi .b_lit
    ; $1..$9 positional argument extraction
    sub r0, r1, #0x31
    mov r1, r10
    bl extract_nth_ws_arg_a32
    mov r1, r0
    mov r0, r7
    bl str_copy_a32
    mov r7, r0
    add r9, r9, #2
    b .b_lp
.b_all:
    mov r0, r7
    mov r1, r10
    bl str_copy_a32
    mov r7, r0
    add r9, r9, #2
    b .b_lp
.b_sep:
    mov r0, #0x20
    strb r0, [r7, #0]
    mov r0, #0x26 ; '&'
    strb r0, [r7, #1]
    mov r0, #0x20
    strb r0, [r7, #2]
    add r7, r7, #3
    add r9, r9, #2
    b .b_lp
.b_lit:
    strb r0, [r7, #0]
    add r7, r7, #1
    add r9, r9, #1
    b .b_lp

.exp_done:
    mov r0, #0
    strb r0, [r7, #0]
    mov r0, r6
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ret_orig:
    mov r0, r4
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc extract_nth_ws_arg_a32
;; @abi arm_ce
;; @in r0 = 0-based arg index, r1 = whitespace-separated argument tail
;; @out r0 = arena NUL-terminated token (empty string if out of range)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Extracts the Nth space/quote-delimited argument from r1.
@proc extract_nth_ws_arg_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0 ; target idx
    mov r5, r1 ; cursor
.tok_lp:
    ldrb r0, [r5, #0]
    cmp r0, #0x20
    beq .sk
    cmp r0, #0x09
    beq .sk
    b .tok_st
.sk:
    add r5, r5, #1
    b .tok_lp
.tok_st:
    cmp r0, #0
    beq .empty
    mov r6, r5
    mov r7, #0 ; quote state
.scan_t:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .tok_end
    cmp r0, #0x22
    bne .chk_sp
    eor r7, r7, #1
    add r5, r5, #1
    b .scan_t
.chk_sp:
    cmp r7, #0
    bne .adv_t
    cmp r0, #0x20
    beq .tok_end
    cmp r0, #0x09
    beq .tok_end
.adv_t:
    add r5, r5, #1
    b .scan_t
.tok_end:
    cmp r4, #0
    beq .copy_out
    sub r4, r4, #1
    b .tok_lp
.copy_out:
    sub r7, r5, r6
    add r0, r7, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .empty
    mov r4, r0
    mov r1, r6
    mov r2, r7
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r4
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
.empty:
    mov r0, #4
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_z
    mov r1, #0
    strb r1, [r0, #0]
.ret_z:
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc read_stdin_line_a32
;; @abi arm_ce
;; @in r0 = output WTF-8 buffer, r1 = max bytes
;; @out r0 = 1 if line read, 0 on EOF
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reads a line from active FD 0 (stdin) handling CRLF, LF, Backspace, and Tab completion.
@proc read_stdin_line_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #12
    mov r4, r0 ; buf
    sub r5, r1, #2 ; max
    mov r6, #0 ; len
    mov r7, #0 ; got any

.ch_lp:
    bl hal_get_ctx
    add r0, r0, #ARM32_CTX.fdTable
    ldr r0, [r0, #0]
    mov r1, sp
    mov r2, #1
    bl hal_read_handle
    cmp r0, #0
    beq .eof_chk
    ldrb r0, [sp, #0]
    cmp r0, #0x1A ; Ctrl+Z
    beq .eof_chk
    mov r7, #1
    cmp r0, #0x0A ; LF
    beq .line_done
    cmp r0, #0x0D ; CR
    beq .ch_lp
    cmp r0, #0x08 ; Backspace
    bne .store_c
    cmp r6, #0
    beq .ch_lp
    sub r6, r6, #1
    b .ch_lp
.store_c:
    cmp r6, r5
    bhs .ch_lp
    strb r0, [r4, r6]
    add r6, r6, #1
    b .ch_lp

.eof_chk:
    cmp r7, #0
    beq .ret_eof
.line_done:
    mov r0, #0
    strb r0, [r4, r6]
    mov r0, #1
    add sp, sp, #12
    pop {r4, r5, r6, r7, lr}
    bx lr
.ret_eof:
    mov r0, #0
    strb r0, [r4, #0]
    add sp, sp, #12
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc
