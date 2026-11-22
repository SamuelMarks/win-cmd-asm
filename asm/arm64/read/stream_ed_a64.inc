;; @file stream_ed_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 stateless byte-offset BatchFrameReader (UTF-8 BOM, UTF-16LE/BE, 0x1A EOF, caret folding) & DOSKEY line reader.

@cpu arm64
@format inc

;; @proc batch_read_raw_line_a64
;; @abi win_arm64
;; @in x0 = BATCH_FRAME_A64*, x1 = output WTF-8 buffer (A64_LINE_MAX)
;; @out w0 = 1 if line read, 0 on EOF
;; @err w0 = 0 on EOF or file open error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Opens batch file, seeks to 64-bit uFileOff, reads one physical line (UTF-8/OEM or UTF-16LE/BE), updates uFileOff, and closes handle!
@proc batch_read_raw_line_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #544
    mov x29, sp
    mov x19, x0 ; frame
    mov x20, x1 ; out buf

    ldr x0, [x19, #BATCH_FRAME_A64.pFilePath]
    bl to_u16_longpath_a64
    cbz x0, .eof
    mov w1, #0 ; read mode
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .eof
    mov x21, x0 ; hFile

    ; Detect BOM on first read at offset 0
    ldr x1, [x19, #BATCH_FRAME_A64.uFileOff]
    cbnz x1, .seek_pos
    ldrb w2, [x19, #BATCH_FRAME_A64.bBomKind]
    cbnz w2, .seek_pos

    mov x0, x21
    mov x1, sp
    mov w2, #4
    bl hal_read_handle_a64
    cmp w0, #2
    b.lo .reset_0
    ldrb w1, [sp, #0]
    ldrb w2, [sp, #1]
    cmp w1, #0xFF
    b.ne .chk_be_bom
    cmp w2, #0xFE
    b.ne .chk_be_bom
    mov w3, #2 ; UTF-16LE
    strb w3, [x19, #BATCH_FRAME_A64.bBomKind]
    mov x3, #2
    str x3, [x19, #BATCH_FRAME_A64.uFileOff]
    b .seek_pos
.chk_be_bom:
    cmp w1, #0xFE
    b.ne .chk_u8_bom
    cmp w2, #0xFF
    b.ne .chk_u8_bom
    mov w3, #3 ; UTF-16BE
    strb w3, [x19, #BATCH_FRAME_A64.bBomKind]
    mov x3, #2
    str x3, [x19, #BATCH_FRAME_A64.uFileOff]
    b .seek_pos
.chk_u8_bom:
    cmp w0, #3
    b.lo .reset_0
    cmp w1, #0xEF
    b.ne .reset_0
    cmp w2, #0xBB
    b.ne .reset_0
    ldrb w3, [sp, #2]
    cmp w3, #0xBF
    b.ne .reset_0
    mov w3, #1 ; UTF-8 BOM
    strb w3, [x19, #BATCH_FRAME_A64.bBomKind]
    mov x3, #3
    str x3, [x19, #BATCH_FRAME_A64.uFileOff]
    b .seek_pos
.reset_0:
    mov w3, #1
    strb w3, [x19, #BATCH_FRAME_A64.bBomKind]

.seek_pos:
    mov x0, x21
    ldr x1, [x19, #BATCH_FRAME_A64.uFileOff]
    mov w2, #0 ; FILE_BEGIN
    bl hal_seek_file_a64

    ldrb w22, [x19, #BATCH_FRAME_A64.bBomKind]
    cmp w22, #2
    b.hs .read_u16

    ; UTF-8 / ASCII / OEM chunked byte reader (up to 512 bytes per chunk)
    mov x0, x21
    mov x1, sp
    mov w2, #512
    bl hal_read_handle_a64
    mov w23, w0 ; bytes read
    mov x0, x21
    bl hal_close_handle_a64

    cbz w23, .eof
    uxtw x23, w23
    mov x24, #0 ; raw bytes consumed
    mov x25, #0 ; output bytes emitted

.u8_lp:
    cmp x24, x23
    b.hs .u8_done
    ldrb w0, [sp, x24]
    cmp w0, #0x1A ; Ctrl+Z immediate EOF
    b.eq .u8_ctrlz
    add x24, x24, #1
    cmp w0, #0x0A ; LF
    b.eq .u8_done
    cmp w0, #0x0D ; CR
    b.ne .u8_store
    cmp x24, x23
    b.hs .u8_done
    ldrb w1, [sp, x24]
    cmp w1, #0x0A
    b.ne .u8_done
    add x24, x24, #1
    b .u8_done
.u8_store:
    strb w0, [x20, x25]
    add x25, x25, #1
    b .u8_lp

.u8_ctrlz:
    cbz x25, .eof_advance_z
    b .u8_done
.eof_advance_z:
    ldr x0, [x19, #BATCH_FRAME_A64.uFileOff]
    add x0, x0, #1
    str x0, [x19, #BATCH_FRAME_A64.uFileOff]
    b .eof

.u8_done:
    strb wzr, [x20, x25]
    ldr x0, [x19, #BATCH_FRAME_A64.uFileOff]
    add x0, x0, x24
    str x0, [x19, #BATCH_FRAME_A64.uFileOff]
    mov w0, #1
    b .exit

.read_u16:
    ; UTF-16LE (2) or UTF-16BE (3) streaming transcoder with exact byte offset tracking
    mov x23, x20 ; out cursor
    mov x24, #0  ; physical bytes consumed
    mov w25, #0  ; got any code unit

.u16_lp:
    mov x0, x21
    mov x1, sp
    mov w2, #2
    bl hal_read_handle_a64
    cmp w0, #2
    b.lo .u16_done
    mov w25, #1
    add x24, x24, #2
    ldrb w0, [sp, #0]
    ldrb w1, [sp, #1]
    cmp w22, #3
    b.eq .swap_be
    orr w0, w0, w1, lsl #8
    b .got_u16c
.swap_be:
    orr w0, w1, w0, lsl #8
.got_u16c:
    cmp w0, #0x000A
    b.eq .u16_done
    cmp w0, #0x000D
    b.ne .emit_u16c
    ; Peek next WCHAR for 0x000A
    mov x0, x21
    mov x1, sp
    mov w2, #2
    bl hal_read_handle_a64
    cmp w0, #2
    b.lo .u16_done
    ldrb w0, [sp, #0]
    ldrb w1, [sp, #1]
    cmp w22, #3
    b.eq .be_lf
    orr w0, w0, w1, lsl #8
    b .chk_u16lf
.be_lf:
    orr w0, w1, w0, lsl #8
.chk_u16lf:
    cmp w0, #0x000A
    b.ne .u16_done
    add x24, x24, #2
    b .u16_done

.emit_u16c:
    strh w0, [sp, #8]
    strh wzr, [sp, #10]
    add x0, sp, #8
    mov x1, #1
    mov x2, x23
    bl u16_to_wtf8_buf_a64
    mov x0, x23
    bl str_len_a64
    add x23, x23, x0
    b .u16_lp

.u16_done:
    mov x0, x21
    bl hal_close_handle_a64
    cbz w25, .eof
    strb wzr, [x23, #0]
    ldr x0, [x19, #BATCH_FRAME_A64.uFileOff]
    add x0, x0, x24
    str x0, [x19, #BATCH_FRAME_A64.uFileOff]
    mov w0, #1
    b .exit

.eof:
    strb wzr, [x20, #0]
    mov w0, #0
.exit:
    add sp, sp, #544
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc batch_read_logical_line_a64
;; @abi win_arm64
;; @in x0 = BATCH_FRAME_A64*, x1 = output WTF-8 buffer (A64_LINE_MAX)
;; @out w0 = 1 if logical line read, 0 on EOF
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Reads a logical batch line, folding trailing unescaped `^` + newline continuations when extensions enabled.
@proc batch_read_logical_line_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov x20, x1

    bl batch_read_raw_line_a64
    cbz w0, .done

.chk_caret:
    bl hal_get_ctx_a64
    ldrb w0, [x0, #ARM64_CTX.bExtensions]
    cbz w0, .ok_line

    mov x0, x20
    bl str_len_a64
    mov x21, x0
    cbz x21, .ok_line

    mov x22, #0
    mov x1, x21
.cnt_car:
    cbz x1, .eval_odd
    sub x1, x1, #1
    ldrb w2, [x20, x1]
    cmp w2, #0x5E
    b.ne .eval_odd
    add x22, x22, #1
    b .cnt_car
.eval_odd:
    tbz x22, #0, .ok_line

    sub x21, x21, #1
    strb wzr, [x20, x21]
    mov x0, x19
    add x1, x20, x21
    bl batch_read_raw_line_a64
    cbnz w0, .chk_caret

.ok_line:
    mov w0, #1
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc doskey_add_history_a64
;; @abi win_arm64
;; @in x0 = NUL-terminated command line
;; @out Appends non-empty line to 64-bit DOSKEY history ring (up to A64_HIST_MAX)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Stores an interactive line in ARM64_CTX.pHistRing.
@proc doskey_add_history_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    ldrb w1, [x19, #0]
    cbz w1, .done

    bl hal_get_ctx_a64
    mov x20, x0
    ldr w21, [x20, #ARM64_CTX.dwHistCount]
    cmp w21, #A64_HIST_MAX
    b.hs .done

    mov x0, x19
    bl str_len_a64
    add x0, x0, #2
    bl perm_alloc_a64
    cbz x0, .done
    ldr x1, [x20, #ARM64_CTX.pHistRing]
    uxtw x2, w21
    lsl x2, x2, #3
    str x0, [x1, x2]
    add w21, w21, #1
    str w21, [x20, #ARM64_CTX.dwHistCount]
    mov x1, x19
    bl str_copy_a64
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc extract_nth_ws_arg_a64
;; @abi win_arm64
;; @in w0 = 0-based arg index, x1 = whitespace-separated argument tail
;; @out x0 = arena NUL-terminated token (empty string if out of range)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Extracts the Nth space/quote-delimited argument from x1.
@proc extract_nth_ws_arg_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov w19, w0 ; target idx
    mov x20, x1 ; cursor
.tok_lp:
    ldrb w0, [x20, #0]
    cmp w0, #0x20
    b.eq .sk
    cmp w0, #0x09
    b.eq .sk
    b .tok_st
.sk:
    add x20, x20, #1
    b .tok_lp
.tok_st:
    cbz w0, .empty
    mov x21, x20
    mov w22, #0
.scan_t:
    ldrb w0, [x20, #0]
    cbz w0, .tok_end
    cmp w0, #0x22
    b.ne .chk_sp
    eor w22, w22, #1
    add x20, x20, #1
    b .scan_t
.chk_sp:
    cbnz w22, .adv_t
    cmp w0, #0x20
    b.eq .tok_end
    cmp w0, #0x09
    b.eq .tok_end
.adv_t:
    add x20, x20, #1
    b .scan_t
.tok_end:
    cbz w19, .copy_out
    sub w19, w19, #1
    b .tok_lp
.copy_out:
    sub x22, x20, x21
    add x0, x22, #2
    bl arena_alloc_a64
    cbz x0, .empty
    mov x19, x0
    mov x1, x21
    mov x2, x22
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x19
    b .exit
.empty:
    mov x0, #8
    bl arena_alloc_a64
    cbz x0, .exit
    strb wzr, [x0, #0]
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc doskey_expand_macro_a64
;; @abi win_arm64
;; @in x0 = input interactive line
;; @out x0 = expanded line (or original x0 if first token is not a registered DOSKEY macro)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Expands registered DOSKEY macros including `$1`..`$9`, `$*`, and `$T` (`&`).
@proc doskey_expand_macro_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl skip_ws_a64
    mov x19, x0

    mov x20, #0 ; name len
.nm_lp:
    ldrb w0, [x19, x20]
    cbz w0, .lookup
    cmp w0, #0x20
    b.eq .lookup
    cmp w0, #0x09
    b.eq .lookup
    add x20, x20, #1
    b .nm_lp

.lookup:
    cbz x20, .ret_orig
    bl hal_get_ctx_a64
    mov x21, x0
    ldr w22, [x21, #ARM64_CTX.dwMacroCount]
    mov w23, #0
.m_lp:
    cmp w23, w22
    b.hs .ret_orig
    ldr x0, [x21, #ARM64_CTX.pMacroNames]
    uxtw x1, w23
    lsl x1, x1, #3
    ldr x24, [x0, x1]
    mov x0, x24
    bl str_len_a64
    cmp x0, x20
    b.ne .m_nx
    mov x0, x24
    mov x1, x19
    mov x2, x20
    bl str_ncmp_ci_a64
    cbz w0, .found_macro
.m_nx:
    add w23, w23, #1
    b .m_lp

.found_macro:
    ldr x0, [x21, #ARM64_CTX.pMacroBodies]
    uxtw x1, w23
    lsl x1, x1, #3
    ldr x24, [x0, x1] ; body template
    add x25, x19, x20
    mov x0, x25
    bl skip_ws_a64
    mov x25, x0

    mov_imm32 x0, A64_LINE_MAX
    bl arena_alloc_a64
    cbz x0, .ret_orig
    mov x21, x0 ; out base
    mov x22, x0 ; out cursor

.b_lp:
    ldrb w0, [x24, #0]
    cbz w0, .exp_done
    cmp w0, #0x24
    b.ne .b_lit
    ldrb w1, [x24, #1]
    cmp w1, #0x2A
    b.eq .b_all
    cmp w1, #0x54
    b.eq .b_sep
    cmp w1, #0x74
    b.eq .b_sep
    cmp w1, #0x31
    b.lo .b_lit
    cmp w1, #0x39
    b.hi .b_lit
    sub w0, w1, #0x31
    mov x1, x25
    bl extract_nth_ws_arg_a64
    mov x1, x0
    mov x0, x22
    bl str_copy_a64
    mov x22, x0
    add x24, x24, #2
    b .b_lp
.b_all:
    mov x0, x22
    mov x1, x25
    bl str_copy_a64
    mov x22, x0
    add x24, x24, #2
    b .b_lp
.b_sep:
    mov w0, #0x20
    strb w0, [x22, #0]
    mov w0, #0x26
    strb w0, [x22, #1]
    mov w0, #0x20
    strb w0, [x22, #2]
    add x22, x22, #3
    add x24, x24, #2
    b .b_lp
.b_lit:
    strb w0, [x22, #0]
    add x22, x22, #1
    add x24, x24, #1
    b .b_lp

.exp_done:
    strb wzr, [x22, #0]
    mov x0, x21
    b .exit

.ret_orig:
    mov x0, x19
.exit:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc read_stdin_line_a64
;; @abi win_arm64
;; @in x0 = output WTF-8 buffer, x1 = max bytes
;; @out w0 = 1 if line read, 0 on EOF
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Reads a line from active FD 0 (stdin) handling CRLF, LF, Ctrl+Z, and Backspace.
@proc read_stdin_line_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp
    mov x19, x0
    sub x20, x1, #2
    mov x21, #0 ; len
    mov w22, #0 ; got any

.ch_lp:
    bl hal_get_ctx_a64
    ldr x0, [x0, #ARM64_CTX.fdTable]
    mov x1, sp
    mov w2, #1
    bl hal_read_handle_a64
    cbz w0, .eof_chk
    ldrb w0, [sp, #0]
    cmp w0, #0x1A
    b.eq .eof_chk
    mov w22, #1
    cmp w0, #0x0A
    b.eq .line_done
    cmp w0, #0x0D
    b.eq .ch_lp
    cmp w0, #0x08
    b.ne .store_c
    cbz x21, .ch_lp
    sub x21, x21, #1
    b .ch_lp
.store_c:
    cmp x21, x20
    b.hs .ch_lp
    strb w0, [x19, x21]
    add x21, x21, #1
    b .ch_lp

.eof_chk:
    cbz w22, .ret_eof
.line_done:
    strb wzr, [x19, x21]
    mov w0, #1
    b .exit
.ret_eof:
    strb wzr, [x19, #0]
    mov w0, #0
.exit:
    add sp, sp, #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
