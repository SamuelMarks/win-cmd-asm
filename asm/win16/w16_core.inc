;; @file w16_core.inc
;; @isa 8086
;; @desc Shared Win16 resource payloads, CS: import table, WndProc, message loop & DGROUP strings.

@cpu 8086
@format inc

; --- Embedded Resource Payloads (0x0200..0x02FF) ---
; 1. RT_ICON #1 at 0x0200 (144 bytes: 16B header + 64B AND mask + 64B XOR terminal prompt icon)
@data dw icoHdr, 0x0001, 0x0001, 0x0010, 0x0010, 0x0002, 0x0001, 0x0001, 0x0000
@res word icoAndMask, 32
@data dw icoXorTop, 0xFFFF, 0x8001, 0x9C01, 0xA201, 0xA001, 0xA201, 0x9C01, 0x8001
@res word icoXorRest, 24

; 2. RT_GROUP_ICON #1 at 0x0290 (32 bytes: NEWHEADER + RESDIR pointing to RT_ICON #1)
@data dw grpIcoHdr, 0x0000, 0x0001, 0x0001
@data db grpIcoDim, 16, 16, 2, 0
@data dw grpIcoPlanes, 1, 1, 144, 0, 1
@res byte padToMenu02B0, (0x02B0 - ($ - wincmd_seg_base))

; 3. RT_MENU #1 at 0x02B0 (64 bytes: MENUHEADER + "&Edit" popup: Copy=101, Paste=102, Clear=103)
@data dw mnuHdr, 0x0000, 0x0000
@data dw mnuPopupFlags, 0x0090
@data db mnuPopupEdit, "&Edit", 0
@data dw mnuItemCopy, 0x0000, IDM_EDIT_COPY
@data db mnuStrCopy, "&Copy", 0
@data dw mnuItemPaste, 0x0000, IDM_EDIT_PASTE
@data db mnuStrPaste, "&Paste", 0
@data dw mnuItemClear, 0x0080, IDM_EDIT_CLEAR
@data db mnuStrClear, "C&lear", 0
@res byte padToText0300, (0x0300 - ($ - wincmd_seg_base))

; ============================================================================
; Segment 1: _TEXT (File Offset 0x0300, Segment Offset 0x0000)
; ============================================================================
w16_text_base:

;; @proc w16_seg1_trampoline
;; @abi win16
;; @in Entry at Segment 1 : Offset 0x0000 from Win16 NE loader
;; @out Jumps past fixed CS: import table to w16_entry
;; @err none
;; @clobbers none
;; @desc 4-byte entry jump placing IMP_AHINCR at exact _TEXT+0x0004.
@proc w16_seg1_trampoline
    jmp w16_entry
@endproc
@data db bTrampPad, 0x90

; --- 56 Fixed Import Slots (_TEXT + 0x0004 .. _TEXT + 0x00E7) ---
@data dw impSlotAhIncr, 0xFFFF, 0x0000
@data dd impSlotInitTask, 0x0000FFFF
@data dd impSlotWaitEvent, 0x0000FFFF
@data dd impSlotGetVersion, 0x0000FFFF
@data dd impSlotSetErrMode, 0x0000FFFF
@data dd impSlotGlobalAlloc, 0x0000FFFF
@data dd impSlotGlobalLock, 0x0000FFFF
@data dd impSlotGlobalUnlock, 0x0000FFFF
@data dd impSlotGlobalFree, 0x0000FFFF
@data dd impSlotGetModH, 0x0000FFFF
@data dd impSlotLoadLib, 0x0000FFFF
@data dd impSlotGetProc, 0x0000FFFF
@data dd impSlotMakeProc, 0x0000FFFF
@data dd impSlotFreeProc, 0x0000FFFF
@data dd impSlotLoadMod, 0x0000FFFF
@data dd impSlotGetModUse, 0x0000FFFF
@data dd impSlotOpenFile, 0x0000FFFF
@data dd impSlotLClose, 0x0000FFFF
@data dd impSlotLRead, 0x0000FFFF
@data dd impSlotLCreat, 0x0000FFFF
@data dd impSlotLLSeek, 0x0000FFFF
@data dd impSlotLWrite, 0x0000FFFF
@data dd impSlotInitApp, 0x0000FFFF
@data dd impSlotPostQuit, 0x0000FFFF
@data dd impSlotGetClientRc, 0x0000FFFF
@data dd impSlotSetWinText, 0x0000FFFF
@data dd impSlotBeginPaint, 0x0000FFFF
@data dd impSlotEndPaint, 0x0000FFFF
@data dd impSlotCreateWin, 0x0000FFFF
@data dd impSlotShowWin, 0x0000FFFF
@data dd impSlotRegClass, 0x0000FFFF
@data dd impSlotScrollWin, 0x0000FFFF
@data dd impSlotSetScrPos, 0x0000FFFF
@data dd impSlotSetScrRng, 0x0000FFFF
@data dd impSlotDefWndProc, 0x0000FFFF
@data dd impSlotGetMessage, 0x0000FFFF
@data dd impSlotTransMsg, 0x0000FFFF
@data dd impSlotDispMsg, 0x0000FFFF
@data dd impSlotUpdateWin, 0x0000FFFF
@data dd impSlotInvalRect, 0x0000FFFF
@data dd impSlotOpenClip, 0x0000FFFF
@data dd impSlotCloseClip, 0x0000FFFF
@data dd impSlotEmptyClip, 0x0000FFFF
@data dd impSlotSetClip, 0x0000FFFF
@data dd impSlotGetClip, 0x0000FFFF
@data dd impSlotLoadMenu, 0x0000FFFF
@data dd impSlotCreateCaret, 0x0000FFFF
@data dd impSlotDestroyCaret, 0x0000FFFF
@data dd impSlotSetCaretPos, 0x0000FFFF
@data dd impSlotShowCaret, 0x0000FFFF
@data dd impSlotLoadCursor, 0x0000FFFF
@data dd impSlotLoadIcon, 0x0000FFFF
@data dd impSlotTextOut, 0x0000FFFF
@data dd impSlotSelectObj, 0x0000FFFF
@data dd impSlotGetStockObj, 0x0000FFFF
@data dd impSlotGetTextMet, 0x0000FFFF
@data dd impSlotTrn286, 0x0000FFFF

@res byte padToWndProc03E8, (0x03E8 - ($ - wincmd_seg_base))

; --- Exported FAR PASCAL WndProc Callback at exact _TEXT + 0x00E8 (W16_WNDPROC_OFF) ---
; Stack frame after `inc bp; push bp; mov bp, sp; push ds`:
;   [bp + 14] = hWnd     (WORD)
;   [bp + 12] = uMsg     (WORD)
;   [bp + 10] = wParam   (WORD)
;   [bp +  8] = HIWORD(lParam)
;   [bp +  6] = LOWORD(lParam)
;; @proc w16_wndproc
;; @abi win16
;; @in Stack: hWnd (+14), uMsg (+12), wParam (+10), lParam (+6..+9); AX = DGROUP from MakeProcInstance
;; @out DX:AX = LRESULT (0 when handled, DefWindowProc otherwise); cleans 10 arg bytes via RETF 10
;; @err none
;; @clobbers ax, bx, cx, dx
;; @desc Real-Mode-walkable & 286 Protected-Mode selector-safe Win16 terminal window procedure.
@proc w16_wndproc
    mov ax, ds
    nop
    inc bp
    push bp
    mov bp, sp
    push ds
    mov ds, ax
    push es
    push si
    push di

    mov ax, word [bp + 12]
    cmp ax, WM_CREATE
    je .on_create
    cmp ax, WM_PAINT
    je .on_paint
    cmp ax, WM_CHAR
    je .on_char
    cmp ax, WM_KEYDOWN
    je .on_keydown
    cmp ax, WM_SETFOCUS
    je .on_setfocus
    cmp ax, WM_KILLFOCUS
    je .on_killfocus
    jmp .chk_more

.on_create:
    mov ax, word [bp + 14]
    mov word [wW16HWnd], ax
    cmp word [pfnDragAccept_p4 + 2], 0
    jne .do_drag_reg
    jmp .ret_zero
.do_drag_reg:
    push ax
    mov ax, 1
    push ax
    call far [pfnDragAccept_p4]
    jmp .ret_zero

.on_paint:
    mov ax, word [bp + 14]
    call w16_term_paint
    jmp .ret_zero

.on_char:
    mov ax, word [bp + 10]
    call w16_handle_char
    jmp .ret_zero

.on_keydown:
    mov ax, word [bp + 10]
    call w16_handle_keydown
    jmp .ret_zero

.on_setfocus:
    call w16_caret_sync
    jmp .ret_zero

.on_killfocus:
    call far [cs:IMP_DESTROYCARET_P0]
    jmp .ret_zero

.chk_more:
    cmp ax, WM_SIZE
    je .on_size
    cmp ax, WM_VSCROLL
    je .on_vscroll
    cmp ax, WM_HSCROLL
    je .on_hscroll
    cmp ax, WM_COMMAND
    je .on_command
    cmp ax, WM_DROPFILES
    je .on_drop
    cmp ax, WM_IME_REPORT
    je .on_ime
    cmp ax, WM_DESTROY
    je .on_destroy

    ; Default: forward to USER.107 DefWindowProc(hWnd, uMsg, wParam, lParam)
    push word [bp + 14]
    push word [bp + 12]
    push word [bp + 10]
    push word [bp + 8]
    push word [bp + 6]
    call far [cs:IMP_DEFWINDOWPROC_P10]
    jmp .epilog

.on_size:
    call w16_on_resize
    jmp .ret_zero

.on_vscroll:
    mov ax, word [bp + 14]
    mov bx, word [bp + 10]
    mov cx, word [bp + 6]
    mov dx, 1
    call w16_on_scroll
    jmp .ret_zero

.on_hscroll:
    mov ax, word [bp + 14]
    mov bx, word [bp + 10]
    mov cx, word [bp + 6]
    xor dx, dx
    call w16_on_scroll
    jmp .ret_zero

.on_command:
    mov ax, word [bp + 10]
    call w16_on_menu_cmd
    jmp .ret_zero

.on_drop:
    mov ax, word [bp + 10]
    call w16_handle_dropfiles
    jmp .ret_zero

.on_ime:
    mov ax, word [bp + 10]
    mov cx, word [bp + 6]
    mov dx, word [bp + 8]
    call w16_handle_ime
    jmp .ret_zero

.on_destroy:
    push word [wW16ExitCode]
    call far [cs:IMP_POSTQUITMESSAGE_P2]

.ret_zero:
    xor ax, ax
    xor dx, dx
.epilog:
    pop di
    pop si
    pop es
    pop ds
    pop bp
    dec bp
    retf 10
@endproc

;; @proc w16_on_resize
;; @abi win16
;; @in [BP+6] = client width px, [BP+8] = client height px, DS = DGROUP
;; @out Recomputes wW16Cols (20..132) and wW16Rows (5..50) on Windows 2.0..3.2
;; @err none
;; @clobbers ax, bx, dx
;; @desc Reflows terminal character grid dimensions dynamically on WM_SIZE.
@proc w16_on_resize
    mov ax, word [bp + 6]
    xor dx, dx
    mov bx, word [wW16CharW]
    test bx, bx
    jz .done
    div bx
    cmp ax, 20
    jae .c_max
    mov ax, 20
.c_max:
    cmp ax, 132
    jbe .c_set
    mov ax, 132
.c_set:
    mov word [wW16Cols], ax

    mov ax, word [bp + 8]
    xor dx, dx
    mov bx, word [wW16CharH]
    test bx, bx
    jz .done
    div bx
    cmp ax, 5
    jae .r_max
    mov ax, 5
.r_max:
    cmp ax, 50
    jbe .r_set
    mov ax, 50
.r_set:
    mov word [wW16Rows], ax
.done:
    ret
@endproc

;; @proc w16_on_scroll
;; @abi win16
;; @in AX = hWnd, BX = scroll code (0=UP, 1=DOWN, 4=THUMBPOS), CX = pos, DX = bar (1=VERT, 0=HORZ)
;; @out Updates wW16ScrollY or wW16ScrollX and calls USER.62 SetScrollPos
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Handles vertical and horizontal terminal scrollbar messages.
@proc w16_on_scroll
    push es
    mov si, wW16ScrollX
    test dx, dx
    jz .adj
    mov si, wW16ScrollY
.adj:
    cmp bx, 0
    je .line_up
    cmp bx, 1
    je .line_dn
    cmp bx, 4
    je .thumb
    jmp .sync_bar
.line_up:
    cmp word [si], 0
    je .sync_bar
    dec word [si]
    jmp .sync_bar
.line_dn:
    cmp word [si], 25
    jae .sync_bar
    inc word [si]
    jmp .sync_bar
.thumb:
    mov word [si], cx
.sync_bar:
    push ax
    push dx
    push word [si]
    mov ax, 1
    push ax
    call far [cs:IMP_SETSCROLLPOS_P8]
    pop es
    ret
@endproc

;; @proc w16_on_menu_cmd
;; @abi win16
;; @in AX = menu item ID (IDM_EDIT_COPY, IDM_EDIT_PASTE, IDM_EDIT_CLEAR)
;; @out Dispatches Clipboard Copy, Clipboard Paste, or Terminal Clear
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Handles WM_COMMAND from embedded RT_MENU #1.
@proc w16_on_menu_cmd
    cmp ax, IDM_EDIT_COPY
    je .do_copy
    cmp ax, IDM_EDIT_PASTE
    je .do_paste
    cmp ax, IDM_EDIT_CLEAR
    je .do_clear
    ret
.do_copy:
    call w16_clip_copy_row0
    ret
.do_paste:
    call w16_clip_paste
    ret
.do_clear:
    call w16_term_clear
    ret
@endproc

@include "w16_dbcs.inc"
@include "w16_hal.inc"
@include "w16_term.inc"
@include "w16_exec.inc"

;; @proc w16_winmain_lifecycle
;; @abi win16
;; @in DS = DGROUP (InitTask already invoked, hInstance/cmdline saved)
;; @out Registers class, creates tiled (Win 1.0) or overlapping (Win 2.0+) window, runs message pump
;; @err Returns exit code in AL
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Full CRT-free Win16 GUI terminal lifecycle across Windows 1.01..3.2.
@proc w16_winmain_lifecycle
    push es

    ; WaitEvent(0) + InitApp(hInstance)
    xor ax, ax
    push ax
    call far [cs:IMP_WAITEVENT_P2]
    push word [wW16HInst]
    call far [cs:IMP_INITAPP_P2]
    test ax, ax
    jnz .initapp_ok
    jmp .abort_err
.initapp_ok:

    ; Probe GetVersion (KERNEL.3): AL == 1 -> Windows 1.x tiled window mode!
    call far [cs:IMP_GETVERSION_P0]
    mov word [wW16VerWord], ax
    mov byte [bW16IsWin1x], 0
    cmp al, 1
    jne .ver_done
    mov byte [bW16IsWin1x], 1
.ver_done:

    call w16_init_dyn_apis
    call w16_term_alloc
    jnc .alloc_ok
    jmp .abort_err
.alloc_ok:

    ; Create Real-Mode-walkable WndProc thunk via MakeProcInstance(CS:W16_WNDPROC_OFF, hInst)
    push cs
    mov ax, W16_WNDPROC_OFF
    push ax
    push word [wW16HInst]
    call far [cs:IMP_MAKEPROCINSTANCE_P6]
    mov word [dW16WndProcInst], ax
    mov word [dW16WndProcInst + 2], dx

    ; Populate WNDCLASS16 (26 bytes) in bW16WndClass
    mov word [bW16WndClass], 0x0003
    mov word [bW16WndClass + 2], ax
    mov word [bW16WndClass + 4], dx
    mov word [bW16WndClass + 6], 0
    mov word [bW16WndClass + 8], 0
    mov ax, word [wW16HInst]
    mov word [bW16WndClass + 10], ax

    ; Load embedded RT_ICON #1 (MAKEINTRESOURCE(1) = 0x0000:0x0001)
    push word [wW16HInst]
    xor ax, ax
    push ax
    mov ax, 1
    push ax
    call far [cs:IMP_LOADICON_P6]
    mov word [bW16WndClass + 12], ax

    ; Load standard IDC_ARROW (0x0000:0x7F00)
    xor ax, ax
    push ax
    push ax
    mov ax, 0x7F00
    push ax
    call far [cs:IMP_LOADCURSOR_P6]
    mov word [bW16WndClass + 14], ax

    ; BLACK_BRUSH (4) via GetStockObject
    mov ax, 4
    push ax
    call far [cs:IMP_GETSTOCKOBJECT_P2]
    mov word [bW16WndClass + 16], ax

    ; Menu resource MAKEINTRESOURCE(1) = 0x0000:0x0001
    mov word [bW16WndClass + 18], 1
    mov word [bW16WndClass + 20], 0
    mov word [bW16WndClass + 22], (szW16Class - w16_data_base)
    mov word [bW16WndClass + 24], ds

    push ds
    mov ax, bW16WndClass
    push ax
    call far [cs:IMP_REGISTERCLASS_P4]
    test ax, ax
    jnz .reg_ok
    jmp .abort_err
.reg_ok:

    ; CreateWindow:
    ;   On Windows 1.0 (bW16IsWin1x == 1): pass CW_USEDEFAULT (0x8000) for x, y, cx, cy!
    ;   On Windows 2.0..3.2: pass overlapping resizable geometry (40, 30, 640, 320) with scrollbars!
    push ds
    mov ax, (szW16Class - w16_data_base)
    push ax
    push ds
    mov ax, (szW16Title - w16_data_base)
    push ax
    ; dwStyle = WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL (0x00CF_0000 | 0x0030_0000 = 0x00FF_0000)
    mov ax, 0x00FF
    push ax
    xor ax, ax
    push ax

    cmp byte [bW16IsWin1x], 0
    je .win2_coords
    mov ax, CW_USEDEFAULT16
    push ax
    push ax
    push ax
    push ax
    jmp .finish_create

.win2_coords:
    mov ax, 40
    push ax
    mov ax, 30
    push ax
    mov ax, 640
    push ax
    mov ax, 320
    push ax

.finish_create:
    xor ax, ax
    push ax
    push ax
    push word [wW16HInst]
    push ax
    push ax
    call far [cs:IMP_CREATEWINDOW_P30]
    test ax, ax
    jnz .wnd_ok
    jmp .abort_err
.wnd_ok:
    mov word [wW16HWnd], ax

    ; Set scrollbar ranges (0..25)
    push ax
    mov bx, 1
    push bx
    xor bx, bx
    push bx
    mov bx, 25
    push bx
    mov bx, 1
    push bx
    call far [cs:IMP_SETSCROLLRANGE_P10]

    ; Process startup command line (/C, /K, /F, /Q, --internal-dos)
    call w16_run_startup_cmdline

    push word [wW16HWnd]
    push word [wW16CmdShow]
    call far [cs:IMP_SHOWWINDOW_P4]

    push word [wW16HWnd]
    call far [cs:IMP_UPDATEWINDOW_P2]

.msg_loop:
    push ds
    mov ax, bW16MsgStruct
    push ax
    xor ax, ax
    push ax
    push ax
    push ax
    call far [cs:IMP_GETMESSAGE_P10]
    test ax, ax
    jz .msg_quit

    push ds
    mov ax, bW16MsgStruct
    push ax
    call far [cs:IMP_TRANSLATEMESSAGE_P4]

    push ds
    mov ax, bW16MsgStruct
    push ax
    call far [cs:IMP_DISPATCHMESSAGE_P4]
    jmp .msg_loop

.msg_quit:
    ; Cleanup screen GlobalAlloc + MakeProcInstance thunk
    cmp word [hW16ScreenMem], 0
    je .free_thunk
    push word [hW16ScreenMem]
    call far [cs:IMP_GLOBALUNLOCK_P2]
    push word [hW16ScreenMem]
    call far [cs:IMP_GLOBALFREE_P2]
.free_thunk:
    push word [dW16WndProcInst + 2]
    push word [dW16WndProcInst]
    call far [cs:IMP_FREEPROCINSTANCE_P4]

    mov ax, word [bW16MsgStruct + 4]
    pop es
    ret

.abort_err:
    mov ax, 1
    pop es
    ret
@endproc
