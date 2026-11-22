;; @file w16_term.inc
;; @isa 8086
;; @desc Backend 3 Win16 GDI Character-Cell Terminal (80x25..132x50, Caret, Scroll, Clipboard, Drop, IME).

@cpu 8086
@format inc

;; @proc w16_term_alloc
;; @abi win16
;; @in DS = DGROUP
;; @out Allocates selector-safe 132x50x2 cell buffer via GlobalAlloc/GlobalLock and clears it
;; @err CF=1 if GlobalAlloc fails
;; @clobbers ax, bx, cx, dx, di
;; @desc Initializes Win16 terminal geometry (80x25 default, 8x12 cell) and screen selector.
@proc w16_term_alloc
    push es
    mov word [wW16Rows], 25
    mov word [wW16Cols], 80
    mov word [wW16CharW], 8
    mov word [wW16CharH], 12
    mov byte [bW16InsertMode], 1
    mov byte [bW16EchoFlag], 1

    ; Allocate 13,312 bytes (0x3400 >= 132 * 50 * 2) via GlobalAlloc(GMEM_MOVEABLE_ZEROINIT)
    mov ax, GMEM_MOVEABLE_ZEROINIT
    push ax
    xor ax, ax
    push ax
    mov ax, 0x3400
    push ax
    call far [cs:IMP_GLOBALALLOC_P6]
    test ax, ax
    jz .fail
    mov word [hW16ScreenMem], ax

    push ax
    call far [cs:IMP_GLOBALLOCK_P2]
    test dx, dx
    jz .fail
    mov word [wW16ScreenSel], dx

    call w16_term_clear
    call w16_huge_step_test
    pop es
    clc
    ret
.fail:
    pop es
    stc
    ret
@endproc

;; @proc w16_term_clear
;; @abi win16
;; @in DS = DGROUP, [wW16ScreenSel] valid
;; @out Fills 132x50 cells with (' ', 0x07) and resets cursor/scroll to (0, 0)
;; @err none
;; @clobbers ax, cx, di
;; @desc Clears the character+attribute screen buffer without segment arithmetic.
@proc w16_term_clear
    cmp word [wW16ScreenSel], 0
    je .done
    push es
    mov es, word [wW16ScreenSel]
    xor di, di
    mov cx, 6600
    mov ax, 0x0720
    cld
    rep stosw
    pop es
    mov word [wW16CurRow], 0
    mov word [wW16CurCol], 0
    mov word [wW16ScrollY], 0
    mov word [wW16ScrollX], 0
.done:
    ret
@endproc

;; @proc w16_term_scroll_up
;; @abi win16
;; @in DS = DGROUP
;; @out Scrolls 132-col cell buffer up 1 row and invokes USER.61 ScrollWindow when hWnd != 0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Advances terminal viewport by one line and clears the bottom row.
@proc w16_term_scroll_up
    cmp word [wW16ScreenSel], 0
    je .done
    push es
    push ds
    mov ax, word [wW16ScreenSel]
    mov es, ax
    mov ds, ax
    mov si, 264
    xor di, di
    mov cx, 6468
    cld
    rep movsw
    mov cx, 132
    mov ax, 0x0720
    rep stosw
    pop ds

    cmp word [wW16HWnd], 0
    je .pop_done
    push word [wW16HWnd]
    xor ax, ax
    push ax
    mov ax, word [wW16CharH]
    neg ax
    push ax
    xor ax, ax
    push ax
    push ax
    push ax
    push ax
    call far [cs:IMP_SCROLLWINDOW_P14]
.pop_done:
    pop es
.done:
    ret
@endproc

;; @proc w16_term_newline
;; @abi win16
;; @in DS = DGROUP
;; @out Moves cursor to col 0 of next row, scrolling if at bottom of active viewport
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Advances terminal cursor row and scrolls when wW16CurRow reaches wW16Rows.
@proc w16_term_newline
    mov word [wW16CurCol], 0
    mov ax, word [wW16CurRow]
    inc ax
    cmp ax, word [wW16Rows]
    jb .store_row
    call w16_term_scroll_up
    mov ax, word [wW16Rows]
    dec ax
.store_row:
    mov word [wW16CurRow], ax
    ret
@endproc

;; @proc w16_term_cell_off
;; @abi win16
;; @in AX = row (0..49), BX = col (0..131)
;; @out DI = byte offset ((row * 132 + col) * 2) inside wW16ScreenSel
;; @err none
;; @clobbers ax, dx, di
;; @desc Computes cell offset in 132-column backing buffer using 8086 shifts/adds.
@proc w16_term_cell_off
    ; row * 132 = (row << 7) + (row << 2)
    mov dx, ax
    shl ax, 1
    shl ax, 1
    mov di, ax
    mov ax, dx
    mov dx, 7
.sh_lp:
    shl ax, 1
    dec dx
    jnz .sh_lp
    add di, ax
    add di, bx
    shl di, 1
    ret
@endproc

;; @proc w16_term_put_raw
;; @abi win16
;; @in AL = char byte, AH = attr byte, DS = DGROUP
;; @out Stores (AL, AH) at (wW16CurRow, wW16CurCol) and increments wW16CurCol
;; @err none
;; @clobbers bx, dx, di
;; @desc Writes one cell into the locked screen selector without wrapping.
@proc w16_term_put_raw
    cmp word [wW16ScreenSel], 0
    je .done
    push es
    push ax
    mov ax, word [wW16CurRow]
    mov bx, word [wW16CurCol]
    call w16_term_cell_off
    mov es, word [wW16ScreenSel]
    pop ax
    mov word [es:di], ax
    inc word [wW16CurCol]
    pop es
.done:
    ret
@endproc

;; @proc w16_term_write_str
;; @abi win16
;; @in DS:SI = NUL-terminated string (ASCII/OEM or Windows 3.2 GB2312/CP936 DBCS)
;; @out Writes string to terminal screen buffer, keeping 2-byte DBCS glyphs intact across wraps
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Renders text into the Win16 cell buffer with 2-cell wide CJK glyph pairing.
@proc w16_term_write_str
.lp:
    mov al, byte [si]
    test al, al
    jz .done
    cmp al, 0x0D
    je .do_cr
    cmp al, 0x0A
    je .do_lf
    cmp al, 0x08
    je .do_bs

    call w16_is_dbcs_lead
    jnc .sbcs_char
    cmp byte [si + 1], 0
    je .sbcs_char

    ; 2-cell wide GB2312/CP936 DBCS pair: wrap BEFORE lead byte if < 2 columns remain!
    mov bx, word [wW16CurCol]
    inc bx
    cmp bx, word [wW16Cols]
    jb .emit_pair
    push si
    call w16_term_newline
    pop si
.emit_pair:
    mov al, byte [si]
    mov ah, 0x07
    push si
    call w16_term_put_raw
    pop si
    mov al, byte [si + 1]
    mov ah, 0x87
    push si
    call w16_term_put_raw
    pop si
    add si, 2
    jmp .lp

.sbcs_char:
    mov bx, word [wW16CurCol]
    cmp bx, word [wW16Cols]
    jb .emit_one
    push si
    call w16_term_newline
    pop si
.emit_one:
    mov al, byte [si]
    mov ah, 0x07
    push si
    call w16_term_put_raw
    pop si
    inc si
    jmp .lp

.do_cr:
    mov word [wW16CurCol], 0
    inc si
    jmp .lp
.do_lf:
    push si
    call w16_term_newline
    pop si
    inc si
    jmp .lp
.do_bs:
    cmp word [wW16CurCol], 0
    je .bs_adv
    dec word [wW16CurCol]
.bs_adv:
    inc si
    jmp .lp
.done:
    ret
@endproc

;; @proc w16_term_writeln
;; @abi win16
;; @in DS:SI = NUL-terminated string
;; @out Writes string followed by CR+LF into Win16 terminal buffer
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Convenience line writer for built-in Win16 shell commands.
@proc w16_term_writeln
    call w16_term_write_str
    call w16_term_newline
    ret
@endproc

;; @proc w16_caret_sync
;; @abi win16
;; @in DS = DGROUP, [wW16HWnd] != 0
;; @out Recreates/positions underline (insert) or block (overstrike) blinking caret
;; @err none
;; @clobbers ax, bx, cx, dx
;; @desc Updates Win16 caret shape and pixel coordinates for current (row, col).
@proc w16_caret_sync
    cmp word [wW16HWnd], 0
    je .done
    push es
    push word [wW16HWnd]
    xor ax, ax
    push ax
    push word [wW16CharW]
    mov ax, 2
    cmp byte [bW16InsertMode], 0
    jne .got_h
    mov ax, word [wW16CharH]
.got_h:
    push ax
    call far [cs:IMP_CREATECARET_P8]

    mov ax, word [wW16CurCol]
    mul word [wW16CharW]
    push ax
    mov ax, word [wW16CurRow]
    mul word [wW16CharH]
    cmp byte [bW16InsertMode], 0
    je .push_y
    add ax, word [wW16CharH]
    sub ax, 2
.push_y:
    push ax
    call far [cs:IMP_SETCARETPOS_P4]

    push word [wW16HWnd]
    call far [cs:IMP_SHOWCARET_P2]
    pop es
.done:
    ret
@endproc

;; @proc w16_term_paint
;; @abi win16
;; @in AX = hWnd, DS = DGROUP
;; @out Renders visible screen buffer rows via BeginPaint, OEM_FIXED_FONT, TextOut & EndPaint
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Handles WM_PAINT without ever splitting a 2-byte GB2312 character at scroll edges.
@proc w16_term_paint
    push es
    mov bx, ax
    push bx
    push ds
    mov ax, bW16PaintStruct
    push ax
    call far [cs:IMP_BEGINPAINT_P6]
    mov si, ax

    ; Select OEM_FIXED_FONT (10) and query GetTextMetrics
    mov ax, 10
    push ax
    call far [cs:IMP_GETSTOCKOBJECT_P2]
    push si
    push ax
    call far [cs:IMP_SELECTOBJECT_P4]

    push si
    push ds
    mov ax, bW16TextMetric
    push ax
    call far [cs:IMP_GETTEXTMETRICS_P6]
    mov ax, word [bW16TextMetric]
    test ax, ax
    jz .keep_metrics
    mov word [wW16CharH], ax
    mov ax, word [bW16TextMetric + 20]
    test ax, ax
    jz .keep_metrics
    mov word [wW16CharW], ax

.keep_metrics:
    cmp word [wW16ScreenSel], 0
    jne .start_rows
    jmp .end_paint

.start_rows:
    xor cx, cx
.row_lp:
    cmp cx, word [wW16Rows]
    jb .row_in_range
    jmp .end_paint
.row_in_range:
    push cx
    push si

    mov ax, cx
    add ax, word [wW16ScrollY]
    cmp ax, 50
    jb .row_mapped
    jmp .skip_row
.row_mapped:
    mov bx, word [wW16ScrollX]
    call w16_term_cell_off

    ; Avoid starting mid-DBCS trail byte (attr bit 0x80) when horizontally scrolled
    mov es, word [wW16ScreenSel]
    cmp word [wW16ScrollX], 0
    je .extract_row
    test byte [es:di + 1], 0x80
    jz .extract_row
    sub di, 2

.extract_row:
    mov cx, word [wW16Cols]
    xor bx, bx
    xor dx, dx
.col_lp:
    mov al, byte [es:di]
    mov byte [bx + bW16RowRender], al
    add di, 2
    inc bx
    cmp al, 0x20
    je .col_next
    mov dx, bx
.col_next:
    loop .col_lp

    test dx, dx
    jz .skip_row

    ; If last non-space char is a DBCS lead byte, include its trail cell too
    mov bx, dx
    dec bx
    mov al, byte [bx + bW16RowRender]
    push dx
    call w16_is_dbcs_lead
    pop dx
    jnc .emit_textout
    cmp dx, word [wW16Cols]
    jae .emit_textout
    inc dx

.emit_textout:
    pop si
    pop cx
    push cx
    push si

    ; TextOut(hdc=SI, x=0, y=CX*wW16CharH, DS:bW16RowRender, cb=DX)
    mov di, dx
    mov ax, cx
    mul word [wW16CharH]
    push si
    xor bx, bx
    push bx
    push ax
    push ds
    mov ax, bW16RowRender
    push ax
    push di
    call far [cs:IMP_TEXTOUT_P12]

.skip_row:
    pop si
    pop cx
    inc cx
    jmp .row_lp

.end_paint:
    push word [wW16HWnd]
    push ds
    mov ax, bW16PaintStruct
    push ax
    call far [cs:IMP_ENDPAINT_P6]
    pop es
    ret
@endproc

;; @proc w16_clip_copy_row0
;; @abi win16
;; @in DS = DGROUP
;; @out Copies row 0 of screen buffer to Win16 Clipboard as CF_OEMTEXT
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements Edit -> Copy via GlobalAlloc + SetClipboardData(CF_OEMTEXT).
@proc w16_clip_copy_row0
    cmp word [wW16ScreenSel], 0
    je .done
    push es
    mov ax, GMEM_MOVEABLE_ZEROINIT
    push ax
    xor ax, ax
    push ax
    mov ax, 160
    push ax
    call far [cs:IMP_GLOBALALLOC_P6]
    test ax, ax
    jz .pop_done
    mov si, ax

    push si
    call far [cs:IMP_GLOBALLOCK_P2]
    test dx, dx
    jz .pop_done

    mov es, word [wW16ScreenSel]
    xor di, di
    xor bx, bx
    xor cx, cx
.cp_lp:
    cmp bx, word [wW16Cols]
    jae .cp_trim
    mov al, byte [es:di]
    mov byte [bx + bW16RowRender], al
    add di, 2
    inc bx
    cmp al, 0x20
    je .cp_lp
    mov cx, bx
    jmp .cp_lp

.cp_trim:
    mov es, dx
    xor bx, bx
.wr_lp:
    cmp bx, cx
    jae .wr_nul
    mov al, byte [bx + bW16RowRender]
    mov byte [es:bx], al
    inc bx
    jmp .wr_lp
.wr_nul:
    mov byte [es:bx], 0

    push si
    call far [cs:IMP_GLOBALUNLOCK_P2]

    push word [wW16HWnd]
    call far [cs:IMP_OPENCLIPBOARD_P2]
    call far [cs:IMP_EMPTYCLIPBOARD_P0]
    mov ax, CF_OEMTEXT16
    push ax
    push si
    call far [cs:IMP_SETCLIPBOARDDATA_P4]
    call far [cs:IMP_CLOSECLIPBOARD_P0]

.pop_done:
    pop es
.done:
    ret
@endproc

;; @proc w16_input_append_byte
;; @abi win16
;; @in AL = raw byte to append to bW16InputBuf, DS = DGROUP
;; @out Appends byte if wW16LineLen < 250
;; @err none
;; @clobbers bx
;; @desc Appends one byte to the interactive Win16 command line buffer.
@proc w16_input_append_byte
    mov bx, word [wW16LineLen]
    cmp bx, 250
    jae .done
    mov byte [bx + bW16InputBuf], al
    inc bx
    mov byte [bx + bW16InputBuf], 0
    mov word [wW16LineLen], bx
.done:
    ret
@endproc

;; @proc w16_clip_paste
;; @abi win16
;; @in DS = DGROUP
;; @out Reads CF_OEMTEXT (or CF_TEXT fallback) from Win16 Clipboard into bW16InputBuf
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements Edit -> Paste / Shift+Ins in the Win16 terminal.
@proc w16_clip_paste
    push es
    push word [wW16HWnd]
    call far [cs:IMP_OPENCLIPBOARD_P2]
    test ax, ax
    jz .done

    mov ax, CF_OEMTEXT16
    push ax
    call far [cs:IMP_GETCLIPBOARDDATA_P2]
    test ax, ax
    jnz .have_hmem
    mov ax, CF_TEXT16
    push ax
    call far [cs:IMP_GETCLIPBOARDDATA_P2]
    test ax, ax
    jz .close_clip

.have_hmem:
    mov si, ax
    push si
    call far [cs:IMP_GLOBALLOCK_P2]
    test dx, dx
    jz .close_clip
    mov es, dx
    xor di, di
.paste_lp:
    mov al, byte [es:di]
    test al, al
    jz .unlock_clip
    cmp al, 0x0D
    je .paste_submit
    cmp al, 0x0A
    je .paste_skip
    push es
    push di
    call w16_input_append_byte
    mov byte [bW16ScratchA], al
    mov byte [bW16ScratchA + 1], 0
    push si
    mov si, bW16ScratchA
    call w16_term_write_str
    pop si
    pop di
    pop es
.paste_skip:
    inc di
    jmp .paste_lp

.paste_submit:
    push es
    push si
    call w16_submit_input_line
    pop si
    pop es

.unlock_clip:
    push si
    call far [cs:IMP_GLOBALUNLOCK_P2]
.close_clip:
    call far [cs:IMP_CLOSECLIPBOARD_P0]
.done:
    pop es
    ret
@endproc

;; @proc w16_handle_dropfiles
;; @abi win16
;; @in AX = hDrop (wParam of WM_DROPFILES), DS = DGROUP
;; @out Queries dropped path via SHELL!DragQueryFile, appends quoted path to input, calls DragFinish
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Handles Windows 3.1 WM_DROPFILES drag-and-drop into the command line.
@proc w16_handle_dropfiles
    cmp word [pfnDragQuery_p10 + 2], 0
    je .done
    push es
    mov si, ax
    push si
    xor ax, ax
    push ax
    push ds
    mov ax, bW16ScratchA
    push ax
    mov ax, 240
    push ax
    call far [pfnDragQuery_p10]

    mov al, '"'
    call w16_input_append_byte
    mov di, bW16ScratchA
.cp_lp:
    mov al, byte [di]
    test al, al
    jz .cp_end
    call w16_input_append_byte
    inc di
    jmp .cp_lp
.cp_end:
    mov al, '"'
    call w16_input_append_byte

    push si
    mov si, bW16ScratchA
    call w16_term_write_str
    pop si

    cmp word [pfnDragFinish_p2 + 2], 0
    je .pop_done
    push si
    call far [pfnDragFinish_p2]
.pop_done:
    pop es
.done:
    ret
@endproc

;; @proc w16_handle_ime
;; @abi win16
;; @in AX = wParam (IR_DBCSCHAR or IR_STRING), DX:CX = lParam, DS = DGROUP
;; @out Appends 2-byte GB2312/CP936 characters to input buffer and echoes to terminal
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Handles Windows 3.2 Simplified Chinese WM_IME_REPORT messages.
@proc w16_handle_ime
    cmp ax, IR_DBCSCHAR
    je .single_dbcs
    cmp ax, IR_STRING
    je .ime_string
    ret

.single_dbcs:
    ; lParam low word CX holds lead byte in CH and trail byte in CL (or CL/CH)
    mov al, ch
    test al, al
    jz .done_ret
    mov byte [bW16ScratchA], ch
    mov byte [bW16ScratchA + 1], cl
    mov byte [bW16ScratchA + 2], 0
    mov al, ch
    call w16_input_append_byte
    mov al, cl
    call w16_input_append_byte
    mov si, bW16ScratchA
    call w16_term_write_str
.done_ret:
    ret

.ime_string:
    push es
    mov si, cx
    push si
    call far [cs:IMP_GLOBALLOCK_P2]
    test dx, dx
    jz .ime_pop
    mov es, dx
    xor di, di
.istr_lp:
    mov al, byte [es:di]
    test al, al
    jz .istr_unlock
    mov byte [di + bW16ScratchA], al
    call w16_input_append_byte
    inc di
    cmp di, 240
    jb .istr_lp
.istr_unlock:
    mov byte [di + bW16ScratchA], 0
    push si
    call far [cs:IMP_GLOBALUNLOCK_P2]
    mov si, bW16ScratchA
    call w16_term_write_str
.ime_pop:
    pop es
    ret
@endproc

;; @proc w16_submit_input_line
;; @abi win16
;; @in DS = DGROUP, bW16InputBuf holds NUL-terminated command
;; @out Saves command to DOSKEY history, echoes CRLF, runs w16_exec_line, prints prompt
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Commits the current interactive line to execution inside Win16.
@proc w16_submit_input_line
    call w16_term_newline
    cmp word [wW16LineLen], 0
    je .after_exec

    mov si, bW16InputBuf
    mov di, bW16LastCmdBuf
    call w16_str_copy
    mov ax, word [wW16LineLen]
    mov word [wW16LastLen], ax

    mov si, bW16InputBuf
    call w16_exec_line

.after_exec:
    mov word [wW16LineLen], 0
    mov byte [bW16InputBuf], 0
    cmp byte [bW16EchoFlag], 0
    je .done
    mov si, bW16CwdBuf
    call w16_term_write_str
    mov si, (szPromptGt - w16_data_base)
    call w16_term_write_str
.done:
    ret
@endproc

;; @proc w16_handle_char
;; @abi win16
;; @in AL = WM_CHAR character code, DS = DGROUP
;; @out Processes Ctrl+C (0x03), Ctrl+V (0x16), Backspace (DBCS-aware), Enter, or printable byte
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Interactive WM_CHAR line-editor handler with 2-byte GB2312 backspace protection.
@proc w16_handle_char
    cmp al, 0x03
    je .ctrl_c
    cmp al, 0x16
    je .ctrl_v
    cmp al, 0x08
    je .bksp
    cmp al, 0x0D
    je .enter_key
    cmp al, 0x20
    jb .done

    mov byte [bW16ScratchA], al
    mov byte [bW16ScratchA + 1], 0
    call w16_input_append_byte
    mov si, bW16ScratchA
    call w16_term_write_str
    call w16_caret_sync
.done:
    ret

.ctrl_c:
    mov byte [bW16CtrlBreak], 1
    mov word [wW16LineLen], 0
    mov byte [bW16InputBuf], 0
    mov si, (szCtrlC - w16_data_base)
    call w16_term_writeln
    ret

.ctrl_v:
    call w16_clip_paste
    call w16_caret_sync
    ret

.enter_key:
    call w16_submit_input_line
    call w16_caret_sync
    ret

.bksp:
    mov bx, word [wW16LineLen]
    test bx, bx
    jz .done
    mov si, bW16InputBuf
    call w16_prev_char_len
    sub word [wW16LineLen], cx
    mov bx, word [wW16LineLen]
    mov byte [bx + bW16InputBuf], 0
    cmp word [wW16CurCol], cx
    jb .zero_col
    sub word [wW16CurCol], cx
    jmp .erase_cells
.zero_col:
    mov word [wW16CurCol], 0
.erase_cells:
    call w16_caret_sync
    ret
@endproc

;; @proc w16_handle_keydown
;; @abi win16
;; @in AX = virtual key code (wParam of WM_KEYDOWN), DS = DGROUP
;; @out Handles VK_CANCEL (0x03), VK_INSERT (0x2D), VK_UP (0x26), VK_F1 (0x70), VK_F3 (0x72)
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Processes non-character DOSKEY/COMMAND.COM editing keys in Win16.
@proc w16_handle_keydown
    cmp ax, 0x03
    je .vk_cancel
    cmp ax, 0x2D
    je .vk_ins
    cmp ax, 0x26
    je .vk_recall
    cmp ax, 0x72
    je .vk_recall
    cmp ax, 0x70
    je .vk_f1
    ret

.vk_cancel:
    mov byte [bW16CtrlBreak], 1
    ret

.vk_ins:
    xor byte [bW16InsertMode], 1
    call w16_caret_sync
    ret

.vk_recall:
    cmp word [wW16LastLen], 0
    je .done
    mov si, bW16LastCmdBuf
    mov di, bW16InputBuf
    call w16_str_copy
    mov ax, word [wW16LastLen]
    mov word [wW16LineLen], ax
    mov si, bW16InputBuf
    call w16_term_write_str
    call w16_caret_sync
    ret

.vk_f1:
    mov bx, word [wW16LineLen]
    cmp bx, word [wW16LastLen]
    jae .done
    mov al, byte [bx + bW16LastCmdBuf]
    call w16_handle_char
.done:
    ret
@endproc
