;; @file stream.inc
;; @isa 8086
;; @desc Phase 12 stateless byte-offset batch reader (UTF-8/UTF-16LE/BE BOM, 0x1A, CR/LF/CRLF, ^ fold).

@cpu 8086
@format inc

;; @const BF_ACTIVE
;; @desc Bit 0 of BATCH_FRAME.bFlags: frame slot is active.
@const BF_ACTIVE = 0x01

;; @const BF_UTF16LE
;; @desc Bit 1 of BATCH_FRAME.bFlags: batch stream detected FF FE BOM at offset 0.
@const BF_UTF16LE = 0x02

;; @const BF_UTF16BE
;; @desc Bit 2 of BATCH_FRAME.bFlags: batch stream detected FE FF BOM at offset 0.
@const BF_UTF16BE = 0x04

;; @proc stream_open_read_close
;; @abi dos16
;; @in DS:SI = ASCIIZ batch path, CX:DX = 32-bit file byte offset, DS:DI = 256B raw buffer
;; @out AX = raw byte count read (0..256), handle/FCB closed immediately, CF=0
;; @err CF=1 if file missing or open failed
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Statelessly opens, seeks to CX:DX, reads up to 256 raw bytes, and closes the file.
@proc stream_open_read_close
    push bp
    mov bp, sp
    sub sp, 6
    ; [bp-2]=dst_buf, [bp-4]=off_lo, [bp-6]=off_hi
    mov word [bp - 2], di
    mov word [bp - 4], dx
    mov word [bp - 6], cx

    cmp byte [bTrnDosMajor], 2
    jb .dos125_io

    cmp byte [bTrnDosMajor], 7
    jb .open_200
    xor bx, bx
    xor cx, cx
    mov dx, 0x0001
    stc
    mov ax, DOS_LFN_EXT_OPEN
    int 0x21
    jnc .have_hdl

.open_200:
    mov dx, si
    xor al, al
    call k200_open
    jc .fail_io

.have_hdl:
    mov bx, ax
    mov dx, word [bp - 4]
    mov cx, word [bp - 6]
    xor al, al
    call k200_seek
    jc .close_err

    mov dx, word [bp - 2]
    mov cx, 256
    call k200_read
    jc .close_err
    push ax
    call k200_close
    pop ax
    add sp, 6
    pop bp
    clc
    ret

.close_err:
    call k200_close
.fail_io:
    add sp, 6
    pop bp
    stc
    ret

.dos125_io:
    ; Use 64B XFCB scratch right above [bp-2]+256
    mov di, word [bp - 2]
    add di, 256
    call k125_open
    jc .fail_io

    mov di, word [bp - 2]
    add di, 256
    mov ax, word [bp - 4]
    mov word [di + XFCB.fcb + FCB.dwRandRec], ax
    mov ax, word [bp - 6]
    mov word [di + XFCB.fcb + FCB.dwRandRec + 2], ax

    mov dx, word [bp - 2]
    mov cx, 256
    call k125_read
    push cx
    mov di, word [bp - 2]
    add di, 256
    call k125_close
    pop ax
    add sp, 6
    pop bp
    clc
    ret
@endproc

;; @proc stream_decode_unit
;; @abi dos16
;; @in DS:SI = raw buffer cursor, CX = remaining raw bytes, BL = BATCH_FRAME.bFlags
;; @out AL = decoded byte, DX = raw bytes consumed (1 or 2), SI += DX, CX -= DX, CF=0
;; @err CF=1 if CX == 0 (or < 2 in UTF-16 mode)
;; @clobbers ax, dx, si, cx
;; @desc Decodes one logical byte/code-unit from ASCII/UTF-8 or UTF-16LE/BE raw buffer.
@proc stream_decode_unit
    test cx, cx
    jz .no_more
    test bl, (BF_UTF16LE + BF_UTF16BE)
    jnz .u16
    mov al, byte [si]
    inc si
    dec cx
    mov dx, 1
    clc
    ret

.u16:
    cmp cx, 2
    jb .no_more
    test bl, BF_UTF16BE
    jnz .u16_be
    mov al, byte [si]
    mov ah, byte [si + 1]
    jmp .u16_conv
.u16_be:
    mov ah, byte [si]
    mov al, byte [si + 1]
.u16_conv:
    add si, 2
    sub cx, 2
    mov dx, 2
    test ah, ah
    jz .u16_ok
    ; Non-ASCII BMP code unit maps to '?' placeholder on 8-bit OEM DOS console unless UTF-8
    mov al, '?'
.u16_ok:
    clc
    ret

.no_more:
    stc
    ret
@endproc

;; @proc stream_read_batch_line
;; @abi dos16
;; @in Top BATCH_FRAME in resident batchResStack, DS:DI = 256-byte logical line output buffer
;; @out Logical line NUL-terminated at DS:DI, BATCH_FRAME.dwFileOff advanced by exact raw bytes, CF=0
;; @err CF=1 when batch stream reaches EOF (frame automatically popped)
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Phase 12 stateless reader supporting BOMs, UTF-16LE/BE, 0x1A EOF, CR/LF/CRLF, & ^ folding.
@proc stream_read_batch_line
    push bp
    mov bp, sp
    sub sp, 14
    ; [bp-2]  = out_buf
    ; [bp-4]  = out_len
    ; [bp-6]  = frame_off (in wTrnResSeg)
    ; [bp-8]  = raw_scratch (320B in arena)
    ; [bp-10] = path_copy (48B in arena)
    ; [bp-12] = dialect
    ; [bp-14] = flags
    mov word [bp - 2], di
    mov word [bp - 4], 0

    mov cx, 384
    call arena_alloc
    jnc .got_mem
    jmp .eof_pop
.got_mem:
    mov word [bp - 8], di
    add di, 324
    mov word [bp - 10], di

    ; Locate top active BATCH_FRAME in resident batchResStack
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bDialect]
    mov byte [bp - 12], al
    mov al, byte [es:resState + RES_STATE.bBatchDepth]
    test al, al
    jnz .have_depth
    pop es
    jmp .eof_fail
.have_depth:
    dec al
    xor ah, ah
    mov cl, 48
    mul cl
    add ax, addr(batchResStack)
    mov word [bp - 6], ax
    mov bx, ax
    mov al, byte [es:bx + BATCH_FRAME.bFlags]
    mov byte [bp - 14], al

    ; Copy szPath from ES:BX to DS:[bp-10]
    mov di, word [bp - 10]
    lea si, [bx + BATCH_FRAME.szPath]
    mov cx, 42
.cp_path:
    mov al, byte [es:si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jz .path_copied
    loop .cp_path
    mov byte [di], 0
.path_copied:
    pop es

.refill_chunk:
    push es
    mov es, word [wTrnResSeg]
    mov bx, word [bp - 6]
    mov dx, word [es:bx + BATCH_FRAME.dwFileOff]
    mov cx, word [es:bx + BATCH_FRAME.dwFileOff + 2]
    pop es

    mov si, word [bp - 10]
    mov di, word [bp - 8]
    call stream_open_read_close
    jc .j_eof_pop
    test ax, ax
    jnz .got_raw
    cmp word [bp - 4], 0
    je .j_eof_pop
    jmp .finish_ok_line
.j_eof_pop:
    jmp .eof_pop

.got_raw:
    mov cx, ax
    mov si, word [bp - 8]

    ; At offset 0, probe UTF-8 BOM (EF BB BF), UTF-16LE (FF FE), or UTF-16BE (FE FF)
    push es
    mov es, word [wTrnResSeg]
    mov bx, word [bp - 6]
    mov ax, word [es:bx + BATCH_FRAME.dwFileOff]
    or ax, word [es:bx + BATCH_FRAME.dwFileOff + 2]
    pop es
    jnz .decode_lp

    cmp cx, 3
    jb .chk_u16_bom
    cmp byte [si], 0xEF
    jne .chk_u16_bom
    cmp byte [si + 1], 0xBB
    jne .chk_u16_bom
    cmp byte [si + 2], 0xBF
    jne .chk_u16_bom
    add si, 3
    sub cx, 3
    mov dx, 3
    call stream_adv_frame_off
    jmp .decode_lp

.chk_u16_bom:
    cmp cx, 2
    jb .decode_lp
    cmp byte [si], 0xFF
    jne .chk_be_bom
    cmp byte [si + 1], 0xFE
    jne .decode_lp
    or byte [bp - 14], BF_UTF16LE
    jmp .apply_u16_bom
.chk_be_bom:
    cmp byte [si], 0xFE
    jne .decode_lp
    cmp byte [si + 1], 0xFF
    jne .decode_lp
    or byte [bp - 14], BF_UTF16BE
.apply_u16_bom:
    push es
    mov es, word [wTrnResSeg]
    mov bx, word [bp - 6]
    mov al, byte [bp - 14]
    mov byte [es:bx + BATCH_FRAME.bFlags], al
    pop es
    add si, 2
    sub cx, 2
    mov dx, 2
    call stream_adv_frame_off

.decode_lp:
    mov bl, byte [bp - 14]
    call stream_decode_unit
    jc .chk_need_more

    ; Ctrl+Z (0x1A) in non-UTF16 mode acts as immediate physical EOF
    test bl, (BF_UTF16LE + BF_UTF16BE)
    jnz .not_ctrlz
    cmp al, 0x1A
    jne .not_ctrlz
    ; Advance past 0x1A to EOF so next step pops frame cleanly
    mov dx, 1
    add dx, cx
    call stream_adv_frame_off
    cmp word [bp - 4], 0
    jne .finish_ok_line
    jmp .eof_pop

.chk_need_more:
    cmp word [bp - 4], 0
    je .eof_pop
    ; If line already started without newline at EOF, return it!
    jmp .finish_ok_line

.not_ctrlz:
    cmp al, 0x0D
    je .got_cr
    cmp al, 0x0A
    je .got_lf

    ; Append character AL to logical line buffer (up to 252 chars)
    push ax
    call stream_adv_frame_off
    pop ax
    mov di, word [bp - 2]
    mov bx, word [bp - 4]
    cmp bx, 252
    jae .decode_lp
    mov byte [di + bx], al
    inc word [bp - 4]
    jmp .decode_lp

.got_cr:
    call stream_adv_frame_off
    ; Peek whether immediate next unit is 0x0A (CRLF)
    push si
    push cx
    mov bl, byte [bp - 14]
    call stream_decode_unit
    jc .cr_no_lf
    cmp al, 0x0A
    jne .cr_not_lf
    add sp, 4
    call stream_adv_frame_off
    jmp .check_caret_fold
.cr_not_lf:
.cr_no_lf:
    pop cx
    pop si
    jmp .check_caret_fold

.got_lf:
    call stream_adv_frame_off

.check_caret_fold:
    ; In NT_CMD (5) and FREEDOS (6) dialects, trailing unescaped '^' folds newline!
    cmp byte [bp - 12], 5
    jb .finish_ok_line
    mov bx, word [bp - 4]
    test bx, bx
    jz .finish_ok_line
    mov di, word [bp - 2]
    cmp byte [di + bx - 1], '^'
    jne .finish_ok_line
    ; Check that '^' wasn't itself escaped by a preceding '^'
    cmp bx, 2
    jb .do_fold_caret
    cmp byte [di + bx - 2], '^'
    je .finish_ok_line
.do_fold_caret:
    dec word [bp - 4]
    test cx, cx
    jz .refill_now
    jmp .decode_lp
.refill_now:
    jmp .refill_chunk

.finish_ok_line:
    mov di, word [bp - 2]
    mov bx, word [bp - 4]
    mov byte [di + bx], 0
    add sp, 14
    pop bp
    clc
    ret

.eof_pop:
    push es
    mov es, word [wTrnResSeg]
    cmp byte [es:resState + RES_STATE.bBatchDepth], 0
    je .no_dec_b
    dec byte [es:resState + RES_STATE.bBatchDepth]
.no_dec_b:
    pop es
.eof_fail:
    add sp, 14
    pop bp
    stc
    ret
@endproc

;; @proc stream_adv_frame_off
;; @abi dos16
;; @in DX = raw byte delta, [bp - 6] = resident BATCH_FRAME offset
;; @out Adds DX to [es:bx + BATCH_FRAME.dwFileOff]
;; @err none
;; @clobbers bx
;; @desc Advances the resident 32-bit batch byte offset by DX bytes.
@proc stream_adv_frame_off
    push es
    mov es, word [wTrnResSeg]
    mov bx, word [bp - 6]
    add word [es:bx + BATCH_FRAME.dwFileOff], dx
    adc word [es:bx + BATCH_FRAME.dwFileOff + 2], 0
    pop es
    ret
@endproc
