;; @file w16_dbcs.inc
;; @isa 8086
;; @desc Windows 3.2 Simplified Chinese (GB2312 / CP936) IsDBCSLeadByte bitmap & WM_IME_REPORT engine.

@cpu 8086
@format inc

;; @proc w16_init_dbcs
;; @abi win16
;; @in DS = DGROUP, pfnIsDbcs_p2 populated if KERNEL.207 is present
;; @out Populates 32-byte bmW16DbcsLead bitmap and sets bW16HasDbcs=1 when DBCS active
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Probes 0x80..0xFF via KERNEL!IsDBCSLeadByte for Windows 3.2 CP936/GB2312 support.
@proc w16_init_dbcs
    push es
    mov byte [bW16HasDbcs], 0
    mov di, bmW16DbcsLead
    mov cx, 16
.clr_lp:
    mov word [di], 0
    add di, 2
    loop .clr_lp

    cmp word [pfnIsDbcs_p2 + 2], 0
    je .done

    mov si, 0x0080
.probe_lp:
    push si
    call far [pfnIsDbcs_p2]
    test ax, ax
    jz .next_ch

    mov byte [bW16HasDbcs], 1
    mov bx, si
    shr bx, 1
    shr bx, 1
    shr bx, 1
    mov cx, si
    and cl, 7
    mov al, 1
    shl al, cl
    or byte [bx + bmW16DbcsLead], al

.next_ch:
    inc si
    cmp si, 0x0100
    jb .probe_lp

.done:
    pop es
    ret
@endproc

;; @proc w16_is_dbcs_lead
;; @abi win16
;; @in AL = candidate character byte, DS = DGROUP
;; @out CF=1 if AL is a DBCS lead byte (e.g. GB2312 0x81..0xFE), CF=0 otherwise; AL preserved
;; @err none
;; @clobbers cx
;; @desc Constant-time O(1) 256-bit DBCS lead-byte test safe in 8086 Real and 286 Protected Mode.
@proc w16_is_dbcs_lead
    cmp byte [bW16HasDbcs], 0
    je .not_lead
    push bx
    push ax
    xor bh, bh
    mov bl, al
    shr bl, 1
    shr bl, 1
    shr bl, 1
    mov cl, al
    and cl, 7
    mov ch, 1
    shl ch, cl
    test byte [bx + bmW16DbcsLead], ch
    pop ax
    pop bx
    jz .not_lead
    stc
    ret
.not_lead:
    clc
    ret
@endproc

;; @proc w16_prev_char_len
;; @abi win16
;; @in DS:SI = buffer start, BX = current byte cursor (> 0)
;; @out CX = byte length of character ending at BX (2 for GB2312 DBCS pair, 1 for SBCS)
;; @err none
;; @clobbers ax, cx, dx
;; @desc Scans forward from buffer start so Backspace/Left never splits a 2-byte GB2312 glyph.
@proc w16_prev_char_len
    mov cx, 1
    cmp byte [bW16HasDbcs], 0
    je .done
    xor dx, dx
.scan_lp:
    cmp dx, bx
    jae .done
    push bx
    mov bx, dx
    mov al, byte [si + bx]
    pop bx
    call w16_is_dbcs_lead
    jc .step2
    mov cx, 1
    inc dx
    jmp .scan_lp
.step2:
    mov cx, 2
    add dx, 2
    jmp .scan_lp
.done:
    ret
@endproc
