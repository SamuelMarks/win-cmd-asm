;; @file devices.inc
;; @isa 8086
;; @desc Phase 14 reserved character device recognizer (CON, NUL, PRN, AUX, CLOCK$, CONIN$, CONOUT$, COM1..9, LPT1..9).

@cpu 8086
@format inc

;; @const DEV_NONE
;; @desc Path does not name a reserved DOS/Windows character device.
@const DEV_NONE = 0x00

;; @const DEV_NUL
;; @desc Reserved NUL bit-bucket device (discards writes, immediate EOF on reads).
@const DEV_NUL = 0x01

;; @const DEV_CON
;; @desc Reserved interactive console device (CON, CONIN$, CONOUT$).
@const DEV_CON = 0x02

;; @const DEV_AUX
;; @desc Reserved serial auxiliary device (AUX, COM1..COM9).
@const DEV_AUX = 0x03

;; @const DEV_PRN
;; @desc Reserved parallel printer device (PRN, LPT1..LPT9).
@const DEV_PRN = 0x04

;; @const DEV_CLOCK
;; @desc Reserved MS-DOS real-time clock character device (CLOCK$).
@const DEV_CLOCK = 0x05

;; @proc dev_match_name
;; @abi dos16
;; @in DS:SI = ASCIIZ path (bare, colon-suffixed, extension-suffixed, or directory/\.\ prefixed)
;; @out AL = DEV_* code (1..5), CF=0 if reserved device; AL=0, CF=1 if regular file path (SI preserved)
;; @err CF=1 when path is not a reserved character device
;; @clobbers ax, bx, cx, dx, di
;; @desc Matches CON/NUL/PRN/AUX/CLOCK$/CONIN$/CONOUT$/COM1..9/LPT1..9 across bare, C:\DIR\NUL.TXT, and \.\NUL forms.
@proc dev_match_name
    push si

    ; Locate final leaf component after last '', '/', or drive ':' (unless trailing 'DEV:')
    mov bx, si
.scan_sep:
    mov al, byte [si]
    test al, al
    jz .got_leaf
    cmp al, 0x5C
    je .after_sep
    cmp al, '/'
    je .after_sep
    cmp al, ':'
    jne .next_ch
    ; Treat ':' as drive prefix only when followed by non-NUL (e.g. "C:NUL", not "NUL:")
    cmp byte [si + 1], 0
    je .next_ch
.after_sep:
    inc si
    mov bx, si
    jmp .scan_sep
.next_ch:
    inc si
    jmp .scan_sep

.got_leaf:
    ; Measure stem length in CX up to NUL, '.', ':', or space
    mov si, bx
    xor cx, cx
.stem_lp:
    mov al, byte [si]
    test al, al
    jz .chk_len
    cmp al, '.'
    je .chk_len
    cmp al, ':'
    je .chk_trailing_colon
    cmp al, 0x20
    je .chk_len
    inc si
    inc cx
    jmp .stem_lp

.chk_trailing_colon:
    ; After 'DEV:', only NUL or whitespace is valid
    cmp byte [si + 1], 0
    jne .j_not_dev

.chk_len:
    mov si, bx
    cmp cx, 3
    je .len3
    cmp cx, 4
    je .len4
    cmp cx, 6
    jne .chk_l7
    jmp .len6
.chk_l7:
    cmp cx, 7
    jne .j_not_dev
    jmp .len7
.j_not_dev:
    jmp .not_dev

.len3:
    push bx
    mov di, addr(szDevNul)
    mov dx, 3
    call slice_eq_nocase
    pop bx
    jc .try_con3
    jmp .ret_nul
.try_con3:

    push bx
    mov si, bx
    mov cx, 3
    mov di, addr(szDevCon)
    mov dx, 3
    call slice_eq_nocase
    pop bx
    jc .try_aux3
    jmp .ret_con
.try_aux3:

    push bx
    mov si, bx
    mov cx, 3
    mov di, addr(szDevAux)
    mov dx, 3
    call slice_eq_nocase
    pop bx
    jnc .ret_aux

    mov si, bx
    mov cx, 3
    mov di, addr(szDevPrn)
    mov dx, 3
    call slice_eq_nocase
    jnc .ret_prn
    jmp .not_dev

.len4:
    ; Check COM1..COM9 or LPT1..LPT9
    mov al, byte [bx + 3]
    cmp al, '1'
    jb .not_dev
    cmp al, '9'
    ja .not_dev

    push bx
    mov si, bx
    mov cx, 3
    mov di, addr(szDevComPfx)
    mov dx, 3
    call slice_eq_nocase
    pop bx
    jnc .ret_aux

    mov si, bx
    mov cx, 3
    mov di, addr(szDevLptPfx)
    mov dx, 3
    call slice_eq_nocase
    jnc .ret_prn
    jmp .not_dev

.len6:
    push bx
    mov di, addr(szDevClock)
    mov dx, 6
    call slice_eq_nocase
    pop bx
    jnc .ret_clock

    mov si, bx
    mov cx, 6
    mov di, addr(szDevConIn)
    mov dx, 6
    call slice_eq_nocase
    jnc .ret_con
    jmp .not_dev

.len7:
    mov di, addr(szDevConOut)
    mov dx, 7
    call slice_eq_nocase
    jnc .ret_con
    jmp .not_dev

.ret_nul:
    mov al, DEV_NUL
    pop si
    clc
    ret
.ret_con:
    mov al, DEV_CON
    pop si
    clc
    ret
.ret_aux:
    mov al, DEV_AUX
    pop si
    clc
    ret
.ret_prn:
    mov al, DEV_PRN
    pop si
    clc
    ret
.ret_clock:
    mov al, DEV_CLOCK
    pop si
    clc
    ret
.not_dev:
    xor al, al
    pop si
    stc
    ret
@endproc
