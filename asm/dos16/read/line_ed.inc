;; @file line_ed.inc
;; @isa 8086
;; @desc Dual-backend (IBM PC BIOS INT 16h/10h & Pure DOS TTY ANSI/VT52) interactive line editor.

@cpu 8086
@format inc

;; @const KEY_ACT_CHAR
;; @desc Normal printable character returned in AL.
@const KEY_ACT_CHAR = 0x00

;; @const KEY_ACT_ENTER
;; @desc Enter / CR key pressed.
@const KEY_ACT_ENTER = 0x01

;; @const KEY_ACT_BKSP
;; @desc Backspace key pressed.
@const KEY_ACT_BKSP = 0x02

;; @const KEY_ACT_ESC
;; @desc Escape (clear line) pressed.
@const KEY_ACT_ESC = 0x03

;; @const KEY_ACT_TAB
;; @desc Tab (/F:ON filename completion) pressed.
@const KEY_ACT_TAB = 0x04

;; @const KEY_ACT_UP
;; @desc DOSKEY Up-arrow history recall pressed.
@const KEY_ACT_UP = 0x05

;; @const KEY_ACT_DOWN
;; @desc DOSKEY Down-arrow history recall pressed.
@const KEY_ACT_DOWN = 0x06

;; @const KEY_ACT_F1
;; @desc COMMAND.COM F1 copy-one-template-char pressed.
@const KEY_ACT_F1 = 0x07

;; @const KEY_ACT_F3
;; @desc COMMAND.COM F3 copy-remaining-template pressed.
@const KEY_ACT_F3 = 0x08

;; @const KEY_ACT_EOF
;; @desc Ctrl+Z / EOF reached on non-interactive input.
@const KEY_ACT_EOF = 0x09

;; @proc led_emit_ch
;; @abi dos16
;; @in DL = character byte
;; @out Writes single byte to console stdout across DOS 1.25..8.0
;; @err none
;; @clobbers ax, dx
;; @desc Outputs one console echo character via INT 21h AH=02h.
@proc led_emit_ch
    mov ah, DOS_CON_OUT
    int 0x21
    ret
@endproc

;; @proc led_erase_one
;; @abi dos16
;; @in none
;; @out Emits BS, Space, BS to erase one character visually on BIOS and TTY terminals
;; @err none
;; @clobbers ax, dx
;; @desc Destructively backs up one character column on any terminal backend.
@proc led_erase_one
    mov dl, 0x08
    call led_emit_ch
    mov dl, 0x20
    call led_emit_ch
    mov dl, 0x08
    call led_emit_ch
    ret
@endproc

;; @proc led_read_key
;; @abi dos16
;; @in [wTrnResSeg] -> RES_STATE.bHwFlags
;; @out AH = KEY_ACT_*, AL = ASCII byte (when KEY_ACT_CHAR)
;; @err Returns AH=KEY_ACT_EOF on 0x1A stream EOF
;; @clobbers ax, bx, dx
;; @desc Reads next key via BIOS INT 16h (yielding WINOLDAP 1680h!) or TTY ANSI/VT52 parser.
@proc led_read_key
    push es
    mov es, word [wTrnResSeg]
    mov bl, byte [es:resState + RES_STATE.bHwFlags]
    pop es

    test bl, HW_BIOS_CON
    jz .tty_read

.bios_poll:
    mov ah, 0x01
    int 0x16
    jnz .bios_fetch
    test bl, (HW_WIN_ENH + HW_WIN_STD)
    jz .bios_fetch
    push bx
    mov ax, MULTIPLEX_IDLE_SLICE
    int 0x2F
    pop bx

.bios_fetch:
    xor ah, ah
    int 0x16
    cmp al, 0x00
    je .scan_ext
    cmp al, 0xE0
    je .scan_ext
    jmp .classify_ascii

.tty_read:
    mov ah, DOS_CON_IN_NOECHO
    int 0x21
    test al, al
    jnz .chk_tty_esc
    mov ah, DOS_CON_IN_NOECHO
    int 0x21
    mov ah, al
    jmp .scan_ext

.chk_tty_esc:
    cmp al, 0x1B
    jne .classify_ascii
    mov ah, DOS_CHECK_STDIN
    int 0x21
    test al, al
    jz .ret_esc
    mov ah, DOS_CON_IN_NOECHO
    int 0x21
    cmp al, '['
    jne .vt52_seq
    mov ah, DOS_CON_IN_NOECHO
    int 0x21
.vt52_seq:
    cmp al, 'A'
    je .ret_up
    cmp al, 'B'
    je .ret_down
    cmp al, 'C'
    je .ret_f1
    cmp al, 'D'
    je .ret_bksp
    jmp .ret_esc

.scan_ext:
    cmp ah, 0x48
    je .ret_up
    cmp ah, 0x50
    je .ret_down
    cmp ah, 0x3B
    je .ret_f1
    cmp ah, 0x3D
    je .ret_f3
    cmp ah, 0x4D
    je .ret_f1
    cmp ah, 0x4B
    je .ret_bksp
    jmp .bios_poll

.classify_ascii:
    cmp al, 0x0D
    je .ret_enter
    cmp al, 0x0A
    je .ret_enter
    cmp al, 0x08
    je .ret_bksp
    cmp al, 0x7F
    je .ret_bksp
    cmp al, 0x1B
    je .ret_esc
    cmp al, 0x09
    je .ret_tab
    cmp al, 0x1A
    je .ret_eof
    cmp al, 0x06
    je .ret_f6_ctrlz
    mov ah, KEY_ACT_CHAR
    ret

.ret_f6_ctrlz:
    mov al, 0x1A
    mov ah, KEY_ACT_CHAR
    ret
.ret_enter:
    mov ah, KEY_ACT_ENTER
    ret
.ret_bksp:
    mov ah, KEY_ACT_BKSP
    ret
.ret_esc:
    mov ah, KEY_ACT_ESC
    ret
.ret_tab:
    mov ah, KEY_ACT_TAB
    ret
.ret_up:
    mov ah, KEY_ACT_UP
    ret
.ret_down:
    mov ah, KEY_ACT_DOWN
    ret
.ret_f1:
    mov ah, KEY_ACT_F1
    ret
.ret_f3:
    mov ah, KEY_ACT_F3
    ret
.ret_eof:
    mov ah, KEY_ACT_EOF
    ret
@endproc

;; @proc led_tab_complete
;; @abi dos16
;; @in DS:BP = line buffer, CX = current length
;; @out Updates CX and echoes completed filename when /F:ON matches
;; @err Leaves CX unchanged if /F:OFF, DOS < 2.00, or no match
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Helper performing /F:ON Tab filename completion inside led_read_line.
@proc led_tab_complete
    push es
    mov es, word [wTrnResSeg]
    mov al, byte [es:resState + RES_STATE.bHwFlags]
    pop es
    test al, HW_FILE_COMP
    jz .early_tab_ret
    cmp byte [bTrnDosMajor], 2
    jae .start_tab
.early_tab_ret:
    ret
.start_tab:

    mov si, cx
.tok_back:
    test si, si
    jz .build_pat
    mov bx, bp
    add bx, si
    cmp byte [bx - 1], 0x20
    je .build_pat
    dec si
    jmp .tok_back

.build_pat:
    push cx
    push si
    mov di, TRN_CMD_WORK
    mov bx, bp
    add bx, si
    mov dx, cx
    sub dx, si
.cp_pfx:
    test dx, dx
    jz .add_wild
    mov al, byte [bx]
    mov byte [di], al
    inc bx
    inc di
    dec dx
    jmp .cp_pfx
.add_wild:
    mov byte [di], '*'
    mov byte [di + 1], '.'
    mov byte [di + 2], '*'
    mov byte [di + 3], 0

    mov dx, TRN_CMD_WORK
    mov di, dx
    add di, 128
    cmp byte [bTrnDosMajor], 7
    jb .find_tab_200
    mov cx, 0x0010
    call k700_find_first
    jc .find_tab_200
    mov bx, ax
    call k700_find_close
    mov dx, TRN_CMD_WORK
    add dx, (128 + WIN32_FIND_DATAA.cFileName)
    jmp .got_tab_name

.find_tab_200:
    mov dx, TRN_CMD_WORK
    mov di, dx
    add di, 128
    mov cx, 0x0010
    call k200_find_first
    jnc .ok_tab200
    jmp .tab_fail
.ok_tab200:
    mov dx, TRN_CMD_WORK
    add dx, (128 + DTA_FIND.szName)

.got_tab_name:
    pop si
    pop cx

.erase_tok:
    cmp cx, si
    jbe .ins_match
    dec cx
    push cx
    push si
    push dx
    call led_erase_one
    pop dx
    pop si
    pop cx
    jmp .erase_tok

.ins_match:
    ; Check whether matched filename at DS:DX contains a space requiring auto-quotes
    mov di, dx
    xor ah, ah
.chk_sp_lp:
    mov al, byte [di]
    test al, al
    jz .after_sp_chk
    cmp al, 0x20
    jne .nx_sp_ch
    mov ah, 1
    jmp .after_sp_chk
.nx_sp_ch:
    inc di
    jmp .chk_sp_lp

.after_sp_chk:
    mov di, dx
    mov dh, ah
    test dh, dh
    jz .ins_lp
    cmp cx, 124
    jae .tab_done
    mov bx, bp
    add bx, cx
    mov byte [bx], '"'
    inc cx
    push cx
    push dx
    push di
    mov dl, '"'
    call led_emit_ch
    pop di
    pop dx
    pop cx

.ins_lp:
    mov al, byte [di]
    test al, al
    jz .close_q_chk
    cmp cx, 124
    jae .close_q_chk
    mov bx, bp
    add bx, cx
    mov byte [bx], al
    inc cx
    inc di
    push cx
    push dx
    push di
    mov dl, al
    call led_emit_ch
    pop di
    pop dx
    pop cx
    jmp .ins_lp

.close_q_chk:
    test dh, dh
    jz .tab_done
    cmp cx, 125
    jae .tab_done
    mov bx, bp
    add bx, cx
    mov byte [bx], '"'
    inc cx
    push cx
    mov dl, '"'
    call led_emit_ch
    pop cx
    jmp .tab_done

.tab_fail:
    pop si
    pop cx
.tab_done:
    ret
@endproc

;; @proc led_winoldap_copy
;; @abi dos16
;; @in BP = edited line buffer in DS, CX = current byte length
;; @out Copies active line to Windows Clipboard as CF_OEMTEXT (DX=7) via INT 2Fh AX=1701h..1708h
;; @err none
;; @clobbers ax, bx, dx, si, di
;; @desc Exposes Windows 3.x/9x WINOLDAP Clipboard copy from DOS interactive sessions.
@proc led_winoldap_copy
    push cx
    mov ax, 0x1700
    int 0x2F
    cmp ax, 0x1700
    je .done
    mov ax, 0x1701
    int 0x2F
    test ax, ax
    jz .done
    mov ax, 0x1702
    int 0x2F
    pop cx
    push cx
    mov si, bp
    mov di, TRN_CMD_WORK
    mov bx, cx
    cld
    rep movsb
    mov byte [di], 0
    mov cx, bx
    inc cx
    xor si, si
    mov bx, TRN_CMD_WORK
    mov dx, 7
    mov ax, 0x1703
    int 0x2F
    mov ax, 0x1708
    int 0x2F
.done:
    pop cx
    ret
@endproc

;; @proc led_winoldap_paste
;; @abi dos16
;; @in BP = edited line buffer in DS, CX = current byte length
;; @out Appends CF_OEMTEXT/CF_TEXT from WINOLDAP Clipboard (INT 2Fh AX=1701h..1708h) and updates CX
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Exposes Windows 3.x/9x WINOLDAP Clipboard paste into DOS interactive sessions.
@proc led_winoldap_paste
    mov si, cx
    mov ax, 0x1700
    int 0x2F
    cmp ax, 0x1700
    je .done_restore
    mov ax, 0x1701
    int 0x2F
    test ax, ax
    jz .done_restore

    mov dx, 7
    mov ax, 0x1704
    int 0x2F
    test ax, ax
    jnz .fetch_clip
    mov dx, 1
    mov ax, 0x1704
    int 0x2F
    test ax, ax
    jz .close_clip

.fetch_clip:
    cmp ax, 256
    ja .close_clip
    mov bx, TRN_CMD_WORK
    mov ax, 0x1705
    int 0x2F
    test ax, ax
    jz .close_clip

    mov di, TRN_CMD_WORK
.append_lp:
    mov al, byte [di]
    test al, al
    jz .close_clip
    cmp al, 0x0D
    je .close_clip
    cmp al, 0x0A
    je .skip_lf
    cmp si, 125
    jae .close_clip
    mov bx, bp
    add bx, si
    mov byte [bx], al
    inc si
    push si
    push di
    mov dl, al
    call led_emit_ch
    pop di
    pop si
.skip_lf:
    inc di
    jmp .append_lp

.close_clip:
    push si
    mov ax, 0x1708
    int 0x2F
    pop si
.done_restore:
    mov cx, si
    ret
@endproc

;; @proc led_read_line
;; @abi dos16
;; @in DS:DI = 128-byte line buffer, TRN_HIST_BUF = 128-byte template/history buffer
;; @out NUL-terminated edited line at DS:DI, CX = length, CF=0 (or CF=1 on EOF)
;; @err CF=1 when stdin reaches 0x1A EOF with 0 chars read
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Interactive line editor supporting F1/F3 template, DOSKEY recall, WINOLDAP clip, and DBCS Bksp.
@proc led_read_line
    push bp
    mov bp, di
    xor cx, cx

.key_lp:
    push cx
    call led_read_key
    pop cx

    cmp ah, KEY_ACT_ENTER
    jne .not_ent
    jmp .finish_line
.not_ent:
    cmp ah, KEY_ACT_EOF
    jne .not_eof
    jmp .got_eof
.not_eof:
    cmp ah, KEY_ACT_BKSP
    je .do_bksp
    cmp ah, KEY_ACT_ESC
    je .go_esc
    cmp ah, KEY_ACT_F1
    je .go_f1
    cmp ah, KEY_ACT_F3
    je .go_f3
    cmp ah, KEY_ACT_UP
    je .go_up
    cmp ah, KEY_ACT_DOWN
    je .go_esc
    cmp ah, KEY_ACT_TAB
    je .go_tab
    cmp al, 0x16
    je .go_paste
    cmp al, 0x0B
    je .go_copy

    cmp cx, 125
    jae .key_lp
    mov bx, bp
    add bx, cx
    mov byte [bx], al
    inc cx
    push cx
    mov dl, al
    call led_emit_ch
    pop cx
    jmp .key_lp

.go_paste:
    call led_winoldap_paste
    jmp .key_lp
.go_copy:
    call led_winoldap_copy
    jmp .key_lp
.go_esc:
    jmp .do_esc
.go_f1:
    jmp .do_f1
.go_f3:
    jmp .do_f3
.go_up:
    jmp .do_up
.go_tab:
    call led_tab_complete
    jmp .key_lp

.j_key0:
    jmp .key_lp

.do_bksp:
    test cx, cx
    jz .j_key0
    dec cx
    push cx
    call led_erase_one
    pop cx
    test cx, cx
    jz .j_key0
    mov bx, bp
    add bx, cx
    dec bx
    mov al, byte [bx]
    push cx
    call dbcs_is_lead
    pop cx
    jnc .j_key0
    dec cx
    push cx
    call led_erase_one
    pop cx
    jmp .key_lp

.do_esc:
    test cx, cx
    jz .j_key1
    dec cx
    push cx
    call led_erase_one
    pop cx
    jmp .do_esc

.j_key1:
    jmp .key_lp

.do_f1:
    cmp cx, 125
    jae .j_key1
    mov bx, TRN_HIST_BUF
    add bx, cx
    mov al, byte [bx]
    test al, al
    jz .j_key1
    mov bx, bp
    add bx, cx
    mov byte [bx], al
    inc cx
    push cx
    mov dl, al
    call led_emit_ch
    pop cx
    jmp .key_lp

.do_up:
    test cx, cx
    jz .do_f3
    dec cx
    push cx
    call led_erase_one
    pop cx
    jmp .do_up

.do_f3:
    mov bx, TRN_HIST_BUF
    add bx, cx
    mov al, byte [bx]
    test al, al
    jz .j_key1
    cmp cx, 125
    jae .j_key1
    mov si, bp
    add si, cx
    mov byte [si], al
    inc cx
    push cx
    mov dl, al
    call led_emit_ch
    pop cx
    jmp .do_f3

.finish_line:
    mov bx, bp
    add bx, cx
    mov byte [bx], 0
    push cx
    call con_write_crlf
    pop cx
    test cx, cx
    jz .ret_ok
    mov si, bp
    mov di, TRN_HIST_BUF
    push cx
    inc cx
    rep movsb
    pop cx
.ret_ok:
    mov di, bp
    pop bp
    clc
    ret

.got_eof:
    test cx, cx
    jnz .finish_line
    mov byte [bp], 0
    pop bp
    stc
    ret
@endproc
