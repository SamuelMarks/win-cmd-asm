;; @file red_125.inc
;; @isa 8086
;; @desc Resident MS-DOS 1.25 INT 21h character-I/O redirector (<, >, >>, | onto FCBs).

@cpu 8086
@format inc

;; @const REDIR125_IN_ACTIVE
;; @desc Bit 0 of bRedirFlags: character stdin redirected to fcbRedirIn.
@const REDIR125_IN_ACTIVE = 0x01

;; @const REDIR125_OUT_ACTIVE
;; @desc Bit 1 of bRedirFlags: character stdout redirected to fcbRedirOut.
@const REDIR125_OUT_ACTIVE = 0x02

;; @const REDIR125_BUSY
;; @desc Bit 7 of bRedirFlags: re-entrancy guard while performing underlying FCB I/O.
@const REDIR125_BUSY = 0x80

;; @const fcbRedirOut
;; @desc 37-byte DOS 1.25 stdout redirect FCB placed in unused DOS 1.25 PSP bytes 0x0034..0x0058.
@const fcbRedirOut = 0x0034

@data db bRedirFlags, 0
@data db bRedirIoByte, 0
@data dw wResSavedInHdl, 0xFFFF
@data dw wResSavedOutHdl, 0xFFFF
@data dw wOrigInt21Off, 0
@data dw wOrigInt21Seg, 0
@data dw wTrackedDtaOff, 0x0080
@data dw wTrackedDtaSeg, 0
@res byte fcbRedirIn, 37

;; @proc red125_call_orig21
;; @abi dos16
;; @in Registers prepared for underlying MS-DOS 1.25 INT 21h call
;; @out Executes original DOS INT 21h handler via REDIR125_BUSY pass-through
;; @err Propagates DOS status in AL / CF
;; @clobbers ax
;; @desc Invokes INT 21h with REDIR125_BUSY set so red125_int21_isr chains straight to DOS.
@proc red125_call_orig21
    or byte [cs:bRedirFlags], REDIR125_BUSY
    int 0x21
    pushf
    and byte [cs:bRedirFlags], 0x7F
    popf
    ret
@endproc

;; @proc red125_fcb_byte_io
;; @abi dos16
;; @in AH = DOS_FCB_RAND_READ (0x27) or DOS_FCB_RAND_WRITE (0x28), DX = resident FCB offset
;; @out CX = records transferred (0 or 1), AL = status, caller DTA preserved
;; @err none
;; @clobbers ax, cx
;; @desc Shared 1-byte FCB random I/O helper swapping DTA to bRedirIoByte and restoring it.
@proc red125_fcb_byte_io
    push dx
    push ds
    push ax
    push dx
    push cs
    pop ds
    mov dx, addr(bRedirIoByte)
    mov ah, DOS_SET_DTA
    call red125_call_orig21
    pop dx
    pop ax
    mov cx, 1
    call red125_call_orig21
    push ax
    push cx
    mov ds, word [cs:wTrackedDtaSeg]
    mov dx, word [cs:wTrackedDtaOff]
    mov ah, DOS_SET_DTA
    call red125_call_orig21
    pop cx
    pop ax
    pop ds
    pop dx
    ret
@endproc

;; @proc red125_get_char
;; @abi dos16
;; @in [cs:fcbRedirIn] open with wRecSize=1
;; @out AL = next byte (or 0x1A on EOF), CF=1 on EOF
;; @err CF=1 when EOF or Ctrl+Z (0x1A) reached
;; @clobbers ax
;; @desc Reads one byte from fcbRedirIn while preserving caller DTA on MS-DOS 1.25.
@proc red125_get_char
    push cx
    push dx
    mov dx, addr(fcbRedirIn)
    mov ah, DOS_FCB_RAND_READ
    call red125_fcb_byte_io
    jcxz .eof
    mov al, byte [cs:bRedirIoByte]
    cmp al, 0x1A
    je .eof
    pop dx
    pop cx
    clc
    ret
.eof:
    mov al, 0x1A
    pop dx
    pop cx
    stc
    ret
@endproc

;; @proc red125_put_char
;; @abi dos16
;; @in DL = character byte to write to [cs:fcbRedirOut]
;; @out Byte appended via FCB random write (wRecSize=1), caller DTA restored
;; @err none
;; @clobbers ax
;; @desc Writes one byte to fcbRedirOut while preserving caller DTA on MS-DOS 1.25.
@proc red125_put_char
    push cx
    push dx
    mov byte [cs:bRedirIoByte], dl
    mov dx, fcbRedirOut
    mov ah, DOS_FCB_RAND_WRITE
    call red125_fcb_byte_io
    pop dx
    pop cx
    ret
@endproc

;; @proc red125_int21_isr
;; @abi dos16
;; @in AH = INT 21h function, caller registers
;; @out Redirects character stdin (01/06/07/08/0A/0B) and stdout (02/06/09) when active
;; @err Chains all non-redirected functions transparently to dwOrigInt21
;; @clobbers ax
;; @desc MS-DOS 1.25 character-I/O redirector ISR enabling <, >, >>, and | without handles.
@proc red125_int21_isr
    test byte [cs:bRedirFlags], REDIR125_BUSY
    jnz .chain

    cmp ah, DOS_SET_DTA
    jne .chk_redir
    mov word [cs:wTrackedDtaOff], dx
    mov word [cs:wTrackedDtaSeg], ds
    jmp .chain

.chk_redir:
    test byte [cs:bRedirFlags], REDIR125_IN_ACTIVE
    jz .chk_out

    cmp ah, DOS_CON_IN_ECHO
    je .in_echo
    cmp ah, DOS_RAW_IN_NOECHO
    je .in_noecho
    cmp ah, DOS_CON_IN_NOECHO
    je .in_noecho
    cmp ah, DOS_BUF_INPUT
    je .in_buffered
    cmp ah, DOS_CHECK_STDIN
    je .in_status
    cmp ah, DOS_RAW_CON_IO
    jne .chk_out
    cmp dl, 0xFF
    je .in_raw06

.chk_out:
    test byte [cs:bRedirFlags], REDIR125_OUT_ACTIVE
    jz .chain

    cmp ah, DOS_CON_OUT
    je .go_out_dl
    cmp ah, DOS_PRINT_STR
    je .go_out_str09
    cmp ah, DOS_RAW_CON_IO
    jne .chain
    cmp dl, 0xFF
    jne .go_out_dl

.chain:
    jmp far [cs:wOrigInt21Off]

.go_out_dl:
    jmp .out_dl
.go_out_str09:
    jmp .out_str09

.in_noecho:
    call red125_get_char
    iret

.in_echo:
    call red125_get_char
    push ax
    push dx
    mov dl, al
    mov ah, DOS_CON_OUT
    int 0x21
    pop dx
    pop ax
    iret

.in_raw06:
    push bp
    mov bp, sp
    call red125_get_char
    jc .raw06_eof
    and word [bp + 6], 0xFFBF
    pop bp
    iret
.raw06_eof:
    xor al, al
    or word [bp + 6], 0x0040
    pop bp
    iret

.in_status:
    push bx
    mov ax, word [cs:fcbRedirIn + FCB.dwRandRec]
    mov bx, word [cs:fcbRedirIn + FCB.dwRandRec + 2]
    sub ax, word [cs:fcbRedirIn + FCB.dwFileSize]
    sbb bx, word [cs:fcbRedirIn + FCB.dwFileSize + 2]
    mov al, 0x00
    jnc .stat_ret
    mov al, 0xFF
.stat_ret:
    pop bx
    iret

.in_buffered:
    push bx
    push cx
    push si
    mov si, dx
    mov cl, byte [si]
    xor bx, bx
.buf_lp:
    cmp bl, cl
    jae .buf_end
    call red125_get_char
    jc .buf_end
    cmp al, 0x0D
    je .buf_eat_lf
    cmp al, 0x0A
    je .buf_end
    mov byte [bx + si + 2], al
    inc bx
    jmp .buf_lp
.buf_eat_lf:
    call red125_get_char
.buf_end:
    mov byte [si + 1], bl
    mov byte [bx + si + 2], 0x0D
    pop si
    pop cx
    pop bx
    iret

.out_dl:
    call red125_put_char
    mov al, dl
    iret

.out_str09:
    push dx
    push si
    mov si, dx
.str09_lp:
    lodsb
    cmp al, '$'
    je .str09_done
    mov dl, al
    call red125_put_char
    jmp .str09_lp
.str09_done:
    pop si
    pop dx
    mov al, '$'
    iret
@endproc

;; @proc red125_install_far
;; @abi dos16
;; @in [cs:bRedirFlags] configured with REDIR125_IN_ACTIVE / REDIR125_OUT_ACTIVE
;; @out Saves IVT[0x21] (if not already hooked) and installs red125_int21_isr
;; @err none
;; @clobbers ax, dx
;; @desc Far-callable resident entry installing the MS-DOS 1.25 INT 21h redirector hook.
@proc red125_install_far
    push ds
    push es
    cmp word [cs:wOrigInt21Seg], 0
    jne .set_hook
    xor ax, ax
    mov es, ax
    cli
    mov ax, word [es:0x0084]
    mov word [cs:wOrigInt21Off], ax
    mov ax, word [es:0x0086]
    mov word [cs:wOrigInt21Seg], ax
    sti
.set_hook:
    push cs
    pop ds
    mov ax, word [resState + RES_STATE.wPspSeg]
    mov word [wTrackedDtaSeg], ax
    mov word [wTrackedDtaOff], 0x0080
    mov dx, addr(red125_int21_isr)
    mov ax, 0x2521
    call red125_call_orig21
    pop es
    pop ds
    retf
@endproc

;; @proc red125_remove_far
;; @abi dos16
;; @in none
;; @out Restores original INT 21h vector and closes any open resident redirector FCBs
;; @err none
;; @clobbers ax, dx
;; @desc Far-callable resident entry tearing down MS-DOS 1.25 redirection cleanly.
@proc red125_remove_far
    push ds
    cmp word [cs:wOrigInt21Seg], 0
    je .done
    mov ds, word [cs:wOrigInt21Seg]
    mov dx, word [cs:wOrigInt21Off]
    mov ax, 0x2521
    call red125_call_orig21

    push cs
    pop ds
    test byte [bRedirFlags], REDIR125_IN_ACTIVE
    jz .chk_close_out
    mov dx, addr(fcbRedirIn)
    mov ah, DOS_FCB_CLOSE
    int 0x21
.chk_close_out:
    test byte [bRedirFlags], REDIR125_OUT_ACTIVE
    jz .clear_state
    mov dx, fcbRedirOut
    mov ah, DOS_FCB_CLOSE
    int 0x21
.clear_state:
    mov byte [bRedirFlags], 0
    mov word [wOrigInt21Seg], 0
.done:
    pop ds
    retf
@endproc
