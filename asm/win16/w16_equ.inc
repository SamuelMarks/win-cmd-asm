;; @file w16_equ.inc
;; @isa 8086
;; @desc Win16 NE import slot offsets, DGROUP scratch addresses, and Windows 1.0..3.2 GUI/IME equates.

@cpu 8086
@format inc

; --- Fixed CS: Import Pointer Table Offsets inside Segment 1 (_TEXT @ 0x0300) ---
;; @const IMP_AHINCR
;; @desc Offset in _TEXT of KERNEL.114 (__AHINCR) Protected-Mode selector increment word.
@const IMP_AHINCR = 0x0004

;; @const IMP_INITTASK_P0
;; @desc Offset in _TEXT of KERNEL.91 InitTask(void) import pointer.
@const IMP_INITTASK_P0 = 0x0008

;; @const IMP_WAITEVENT_P2
;; @desc Offset in _TEXT of KERNEL.30 WaitEvent(hTask) import pointer.
@const IMP_WAITEVENT_P2 = 0x000C

;; @const IMP_GETVERSION_P0
;; @desc Offset in _TEXT of KERNEL.3 GetVersion(void) import pointer.
@const IMP_GETVERSION_P0 = 0x0010

;; @const IMP_SETERRORMODE_P2
;; @desc Offset in _TEXT of KERNEL.107 SetErrorMode(fuMode) import pointer.
@const IMP_SETERRORMODE_P2 = 0x0014

;; @const IMP_GLOBALALLOC_P6
;; @desc Offset in _TEXT of KERNEL.15 GlobalAlloc(wFlags, dwBytes) import pointer.
@const IMP_GLOBALALLOC_P6 = 0x0018

;; @const IMP_GLOBALLOCK_P2
;; @desc Offset in _TEXT of KERNEL.18 GlobalLock(hMem) import pointer.
@const IMP_GLOBALLOCK_P2 = 0x001C

;; @const IMP_GLOBALUNLOCK_P2
;; @desc Offset in _TEXT of KERNEL.19 GlobalUnlock(hMem) import pointer.
@const IMP_GLOBALUNLOCK_P2 = 0x0020

;; @const IMP_GLOBALFREE_P2
;; @desc Offset in _TEXT of KERNEL.17 GlobalFree(hMem) import pointer.
@const IMP_GLOBALFREE_P2 = 0x0024

;; @const IMP_GETMODULEHANDLE_P4
;; @desc Offset in _TEXT of KERNEL.47 GetModuleHandle(lpModName) import pointer.
@const IMP_GETMODULEHANDLE_P4 = 0x0028

;; @const IMP_LOADLIBRARY_P4
;; @desc Offset in _TEXT of KERNEL.95 LoadLibrary(lpLibFileName) import pointer.
@const IMP_LOADLIBRARY_P4 = 0x002C

;; @const IMP_GETPROCADDRESS_P6
;; @desc Offset in _TEXT of KERNEL.50 GetProcAddress(hMod, lpProcName) import pointer.
@const IMP_GETPROCADDRESS_P6 = 0x0030

;; @const IMP_MAKEPROCINSTANCE_P6
;; @desc Offset in _TEXT of KERNEL.51 MakeProcInstance(lpProc, hInst) import pointer.
@const IMP_MAKEPROCINSTANCE_P6 = 0x0034

;; @const IMP_FREEPROCINSTANCE_P4
;; @desc Offset in _TEXT of KERNEL.52 FreeProcInstance(lpProc) import pointer.
@const IMP_FREEPROCINSTANCE_P4 = 0x0038

;; @const IMP_LOADMODULE_P8
;; @desc Offset in _TEXT of KERNEL.45 LoadModule(lpModName, lpParamBlk) import pointer.
@const IMP_LOADMODULE_P8 = 0x003C

;; @const IMP_GETMODULEUSAGE_P2
;; @desc Offset in _TEXT of KERNEL.48 GetModuleUsage(hModule) import pointer.
@const IMP_GETMODULEUSAGE_P2 = 0x0040

;; @const IMP_OPENFILE_P10
;; @desc Offset in _TEXT of KERNEL.74 OpenFile(lpFile, lpOf, wStyle) import pointer.
@const IMP_OPENFILE_P10 = 0x0044

;; @const IMP_LCLOSE_P2
;; @desc Offset in _TEXT of KERNEL.81 _lclose(hFile) import pointer.
@const IMP_LCLOSE_P2 = 0x0048

;; @const IMP_LREAD_P8
;; @desc Offset in _TEXT of KERNEL.82 _lread(hFile, lpBuf, wBytes) import pointer.
@const IMP_LREAD_P8 = 0x004C

;; @const IMP_LCREAT_P6
;; @desc Offset in _TEXT of KERNEL.83 _lcreat(lpPath, iAttr) import pointer.
@const IMP_LCREAT_P6 = 0x0050

;; @const IMP_LLSEEK_P8
;; @desc Offset in _TEXT of KERNEL.84 _llseek(hFile, lOff, iOrigin) import pointer.
@const IMP_LLSEEK_P8 = 0x0054

;; @const IMP_LWRITE_P8
;; @desc Offset in _TEXT of KERNEL.85 _lwrite(hFile, lpBuf, wBytes) import pointer.
@const IMP_LWRITE_P8 = 0x0058

;; @const IMP_INITAPP_P2
;; @desc Offset in _TEXT of USER.5 InitApp(hInstance) import pointer.
@const IMP_INITAPP_P2 = 0x005C

;; @const IMP_POSTQUITMESSAGE_P2
;; @desc Offset in _TEXT of USER.6 PostQuitMessage(nExitCode) import pointer.
@const IMP_POSTQUITMESSAGE_P2 = 0x0060

;; @const IMP_GETCLIENTRECT_P6
;; @desc Offset in _TEXT of USER.32 GetClientRect(hWnd, lpRect) import pointer.
@const IMP_GETCLIENTRECT_P6 = 0x0064

;; @const IMP_SETWINDOWTEXT_P6
;; @desc Offset in _TEXT of USER.36 SetWindowText(hWnd, lpStr) import pointer.
@const IMP_SETWINDOWTEXT_P6 = 0x0068

;; @const IMP_BEGINPAINT_P6
;; @desc Offset in _TEXT of USER.39 BeginPaint(hWnd, lpPaint) import pointer.
@const IMP_BEGINPAINT_P6 = 0x006C

;; @const IMP_ENDPAINT_P6
;; @desc Offset in _TEXT of USER.40 EndPaint(hWnd, lpPaint) import pointer.
@const IMP_ENDPAINT_P6 = 0x0070

;; @const IMP_CREATEWINDOW_P30
;; @desc Offset in _TEXT of USER.41 CreateWindow(11 args = 30 bytes) import pointer.
@const IMP_CREATEWINDOW_P30 = 0x0074

;; @const IMP_SHOWWINDOW_P4
;; @desc Offset in _TEXT of USER.42 ShowWindow(hWnd, nCmdShow) import pointer.
@const IMP_SHOWWINDOW_P4 = 0x0078

;; @const IMP_REGISTERCLASS_P4
;; @desc Offset in _TEXT of USER.57 RegisterClass(lpWndClass) import pointer.
@const IMP_REGISTERCLASS_P4 = 0x007C

;; @const IMP_SCROLLWINDOW_P14
;; @desc Offset in _TEXT of USER.61 ScrollWindow(hWnd, dx, dy, lpRc, lpClip) import pointer.
@const IMP_SCROLLWINDOW_P14 = 0x0080

;; @const IMP_SETSCROLLPOS_P8
;; @desc Offset in _TEXT of USER.62 SetScrollPos(hWnd, fnBar, nPos, fRedraw) import pointer.
@const IMP_SETSCROLLPOS_P8 = 0x0084

;; @const IMP_SETSCROLLRANGE_P10
;; @desc Offset in _TEXT of USER.64 SetScrollRange(hWnd, fnBar, nMin, nMax, fRedraw) import pointer.
@const IMP_SETSCROLLRANGE_P10 = 0x0088

;; @const IMP_DEFWINDOWPROC_P10
;; @desc Offset in _TEXT of USER.107 DefWindowProc(hWnd, uMsg, wParam, lParam) import pointer.
@const IMP_DEFWINDOWPROC_P10 = 0x008C

;; @const IMP_GETMESSAGE_P10
;; @desc Offset in _TEXT of USER.108 GetMessage(lpMsg, hWnd, wMin, wMax) import pointer.
@const IMP_GETMESSAGE_P10 = 0x0090

;; @const IMP_TRANSLATEMESSAGE_P4
;; @desc Offset in _TEXT of USER.113 TranslateMessage(lpMsg) import pointer.
@const IMP_TRANSLATEMESSAGE_P4 = 0x0094

;; @const IMP_DISPATCHMESSAGE_P4
;; @desc Offset in _TEXT of USER.114 DispatchMessage(lpMsg) import pointer.
@const IMP_DISPATCHMESSAGE_P4 = 0x0098

;; @const IMP_UPDATEWINDOW_P2
;; @desc Offset in _TEXT of USER.124 UpdateWindow(hWnd) import pointer.
@const IMP_UPDATEWINDOW_P2 = 0x009C

;; @const IMP_INVALIDATERECT_P8
;; @desc Offset in _TEXT of USER.125 InvalidateRect(hWnd, lpRect, bErase) import pointer.
@const IMP_INVALIDATERECT_P8 = 0x00A0

;; @const IMP_OPENCLIPBOARD_P2
;; @desc Offset in _TEXT of USER.137 OpenClipboard(hWnd) import pointer.
@const IMP_OPENCLIPBOARD_P2 = 0x00A4

;; @const IMP_CLOSECLIPBOARD_P0
;; @desc Offset in _TEXT of USER.138 CloseClipboard(void) import pointer.
@const IMP_CLOSECLIPBOARD_P0 = 0x00A8

;; @const IMP_EMPTYCLIPBOARD_P0
;; @desc Offset in _TEXT of USER.139 EmptyClipboard(void) import pointer.
@const IMP_EMPTYCLIPBOARD_P0 = 0x00AC

;; @const IMP_SETCLIPBOARDDATA_P4
;; @desc Offset in _TEXT of USER.141 SetClipboardData(uFmt, hMem) import pointer.
@const IMP_SETCLIPBOARDDATA_P4 = 0x00B0

;; @const IMP_GETCLIPBOARDDATA_P2
;; @desc Offset in _TEXT of USER.142 GetClipboardData(uFmt) import pointer.
@const IMP_GETCLIPBOARDDATA_P2 = 0x00B4

;; @const IMP_LOADMENU_P6
;; @desc Offset in _TEXT of USER.150 LoadMenu(hInst, lpMenuName) import pointer.
@const IMP_LOADMENU_P6 = 0x00B8

;; @const IMP_CREATECARET_P8
;; @desc Offset in _TEXT of USER.163 CreateCaret(hWnd, hBmp, nW, nH) import pointer.
@const IMP_CREATECARET_P8 = 0x00BC

;; @const IMP_DESTROYCARET_P0
;; @desc Offset in _TEXT of USER.164 DestroyCaret(void) import pointer.
@const IMP_DESTROYCARET_P0 = 0x00C0

;; @const IMP_SETCARETPOS_P4
;; @desc Offset in _TEXT of USER.165 SetCaretPos(X, Y) import pointer.
@const IMP_SETCARETPOS_P4 = 0x00C4

;; @const IMP_SHOWCARET_P2
;; @desc Offset in _TEXT of USER.166 ShowCaret(hWnd) import pointer.
@const IMP_SHOWCARET_P2 = 0x00C8

;; @const IMP_LOADCURSOR_P6
;; @desc Offset in _TEXT of USER.173 LoadCursor(hInst, lpCursorName) import pointer.
@const IMP_LOADCURSOR_P6 = 0x00CC

;; @const IMP_LOADICON_P6
;; @desc Offset in _TEXT of USER.176 LoadIcon(hInst, lpIconName) import pointer.
@const IMP_LOADICON_P6 = 0x00D0

;; @const IMP_TEXTOUT_P12
;; @desc Offset in _TEXT of GDI.33 TextOut(hdc, x, y, lpStr, cbStr) import pointer.
@const IMP_TEXTOUT_P12 = 0x00D4

;; @const IMP_SELECTOBJECT_P4
;; @desc Offset in _TEXT of GDI.45 SelectObject(hdc, hgdiobj) import pointer.
@const IMP_SELECTOBJECT_P4 = 0x00D8

;; @const IMP_GETSTOCKOBJECT_P2
;; @desc Offset in _TEXT of GDI.87 GetStockObject(fnObject) import pointer.
@const IMP_GETSTOCKOBJECT_P2 = 0x00DC

;; @const IMP_GETTEXTMETRICS_P6
;; @desc Offset in _TEXT of GDI.91 GetTextMetrics(hdc, lptm) import pointer.
@const IMP_GETTEXTMETRICS_P6 = 0x00E0

;; @const IMP_TRN286_ENTRY_P0
;; @desc Offset in _TEXT of INTERNALREF far pointer to discardable _TRN286 segment on 286.
@const IMP_TRN286_ENTRY_P0 = 0x00E4

;; @const W16_WNDPROC_OFF
;; @desc Exact offset in _TEXT (0x00E8) of exported FAR PASCAL WndProc callback.
@const W16_WNDPROC_OFF = 0x00E8

; --- Win16 Window Message, Style, Clipboard, Menu & IME Constants ---
;; @const WM_CREATE
;; @desc Win16 window creation message (0x0001).
@const WM_CREATE = 0x0001

;; @const WM_SETFOCUS
;; @desc Win16 keyboard focus acquired message (0x0007).
@const WM_SETFOCUS = 0x0007

;; @const WM_KILLFOCUS
;; @desc Win16 keyboard focus lost message (0x0008).
@const WM_KILLFOCUS = 0x0008

;; @const WM_QUIT
;; @desc Win16 message loop quit message (0x0012).
@const WM_QUIT = 0x0012

;; @const WM_COMMAND
;; @desc Win16 menu/accelerator command message (0x0111).
@const WM_COMMAND = 0x0111

;; @const WM_HSCROLL
;; @desc Win16 horizontal scrollbar message (0x0114).
@const WM_HSCROLL = 0x0114

;; @const WM_VSCROLL
;; @desc Win16 vertical scrollbar message (0x0115).
@const WM_VSCROLL = 0x0115

;; @const WM_DROPFILES
;; @desc Windows 3.1 SHELL.DLL file drag-and-drop message (0x0233).
@const WM_DROPFILES = 0x0233

;; @const IR_STRING
;; @desc WM_IME_REPORT wParam for locked global DBCS string handle (0x0140).
@const IR_STRING = 0x0140

;; @const IR_DBCSCHAR
;; @desc WM_IME_REPORT wParam for single 2-byte GB2312/CP936 character in lParam (0x0160).
@const IR_DBCSCHAR = 0x0160

;; @const CF_TEXT16
;; @desc Win16 ANSI text clipboard format (1).
@const CF_TEXT16 = 1

;; @const CF_OEMTEXT16
;; @desc Win16 OEM text clipboard format (7).
@const CF_OEMTEXT16 = 7

;; @const GMEM_MOVEABLE_ZEROINIT
;; @desc GlobalAlloc flags GMEM_MOVEABLE (0x0002) | GMEM_ZEROINIT (0x0040) = 0x0042.
@const GMEM_MOVEABLE_ZEROINIT = 0x0042

;; @const SEM_FAILCRITICALERRORS16
;; @desc Win16 SetErrorMode flag suppressing modal critical-error dialogs (0x0001).
@const SEM_FAILCRITICALERRORS16 = 0x0001

;; @const IDM_EDIT_COPY
;; @desc Menu item ID for Edit -> Copy first screen line to CF_OEMTEXT clipboard (101).
@const IDM_EDIT_COPY = 101

;; @const IDM_EDIT_PASTE
;; @desc Menu item ID for Edit -> Paste CF_OEMTEXT clipboard into command input (102).
@const IDM_EDIT_PASTE = 102

;; @const IDM_EDIT_CLEAR
;; @desc Menu item ID for Edit -> Clear terminal viewport (103).
@const IDM_EDIT_CLEAR = 103

; --- DGROUP (_DATA) Scratch Variable Offsets (0x0400..0x1FFF, zero-initialized) ---
;; @const wW16HInst
;; @desc Current task HINSTANCE saved at startup.
@const wW16HInst = 0x0400

;; @const wW16HWnd
;; @desc Main terminal window HWND returned by CreateWindow.
@const wW16HWnd = 0x0402

;; @const wW16CmdShow
;; @desc Initial ShowWindow nCmdShow value from InitTask.
@const wW16CmdShow = 0x0404

;; @const wW16CmdLineOff
;; @desc Initial command-line offset from InitTask BX.
@const wW16CmdLineOff = 0x0406

;; @const wW16CmdLineSel
;; @desc Initial command-line selector from InitTask ES.
@const wW16CmdLineSel = 0x0408

;; @const wW16VerWord
;; @desc Raw GetVersion word (AL=major, AH=minor).
@const wW16VerWord = 0x040A

;; @const bW16IsWin1x
;; @desc Non-zero when running on Windows 1.0x (tiled CW_USEDEFAULT mode).
@const bW16IsWin1x = 0x040C

;; @const bW16HasDbcs
;; @desc Non-zero when Windows 3.2 GB2312/CP936 DBCS lead bytes are active.
@const bW16HasDbcs = 0x040D

;; @const bW16CtrlBreak
;; @desc Asynchronous Ctrl+C / VK_CANCEL cancellation latch.
@const bW16CtrlBreak = 0x040E

;; @const bW16FailCrit
;; @desc Non-zero when /F auto-fail critical-error switch was passed.
@const bW16FailCrit = 0x040F

;; @const bW16InternalDos
;; @desc Non-zero when --internal-dos in-process .COM stdout capture is active.
@const bW16InternalDos = 0x0410

;; @const bW16BatchClose
;; @desc Non-zero when /C one-shot command execution requests window exit after run.
@const bW16BatchClose = 0x0411

;; @const bW16InsertMode
;; @desc Cursor mode: 1 = insert (underline caret), 0 = overstrike (block caret).
@const bW16InsertMode = 0x0412

;; @const bW16EchoFlag
;; @desc Active ECHO ON (1) / ECHO OFF (0) state.
@const bW16EchoFlag = 0x0413

;; @const wW16ExitCode
;; @desc Last command exit code / ERRORLEVEL.
@const wW16ExitCode = 0x0414

;; @const wW16Rows
;; @desc Active terminal screen rows (25..50).
@const wW16Rows = 0x0416

;; @const wW16Cols
;; @desc Active terminal screen columns (80..132).
@const wW16Cols = 0x0418

;; @const wW16CurRow
;; @desc Current cursor row (0-based).
@const wW16CurRow = 0x041A

;; @const wW16CurCol
;; @desc Current cursor column (0-based).
@const wW16CurCol = 0x041C

;; @const wW16CharW
;; @desc Fixed-pitch character cell pixel width from GetTextMetrics (default 8).
@const wW16CharW = 0x041E

;; @const wW16CharH
;; @desc Fixed-pitch character cell pixel height from GetTextMetrics (default 12).
@const wW16CharH = 0x0420

;; @const wW16ScrollY
;; @desc Current vertical scrollbar row offset.
@const wW16ScrollY = 0x0422

;; @const wW16ScrollX
;; @desc Current horizontal scrollbar column offset.
@const wW16ScrollX = 0x0424

;; @const hW16ScreenMem
;; @desc HGLOBAL handle of selector-safe 132x50 character+attribute screen buffer.
@const hW16ScreenMem = 0x0426

;; @const wW16ScreenSel
;; @desc Locked LDT selector for the character+attribute screen buffer.
@const wW16ScreenSel = 0x0428

;; @const hW16HugeTest
;; @desc Optional >64 KiB GlobalAlloc handle stepped via __AHINCR.
@const hW16HugeTest = 0x042A

;; @const dW16WndProcInst
;; @desc MakeProcInstance thunk FARPROC (4B OFF:SEL) registered in WNDCLASS16.
@const dW16WndProcInst = 0x042C

; --- Dynamic Win 3.0 / 3.1 / WfW 3.11 / Win 3.2 FAR PASCAL Function Pointers (DGROUP) ---
;; @const pfnWinExec_p6
;; @desc Dynamic KERNEL.166 WinExec(lpCmdLine, uCmdShow) pointer (0 on Win 1.x/2.x).
@const pfnWinExec_p6 = 0x0430

;; @const pfnGetFreeSpace_p2
;; @desc Dynamic KERNEL.169 GetFreeSpace(wFlags) pointer.
@const pfnGetFreeSpace_p2 = 0x0434

;; @const pfnIsDbcs_p2
;; @desc Dynamic KERNEL.207 IsDBCSLeadByte(bTestChar) pointer (active on Win 3.2).
@const pfnIsDbcs_p2 = 0x0438

;; @const pfnGetSysRes_p2
;; @desc Dynamic USER.284 GetFreeSystemResources(fuSysResource) pointer.
@const pfnGetSysRes_p2 = 0x043C

;; @const pfnDragAccept_p4
;; @desc Dynamic SHELL.DLL DragAcceptFiles(hWnd, fAccept) pointer.
@const pfnDragAccept_p4 = 0x0440

;; @const pfnDragQuery_p10
;; @desc Dynamic SHELL.DLL DragQueryFile(hDrop, iFile, lpszFile, cb) pointer.
@const pfnDragQuery_p10 = 0x0444

;; @const pfnDragFinish_p2
;; @desc Dynamic SHELL.DLL DragFinish(hDrop) pointer.
@const pfnDragFinish_p2 = 0x0448

;; @const pfnMemManInfo_p4
;; @desc Dynamic TOOLHELP.DLL MemManInfo(lpInfo) pointer.
@const pfnMemManInfo_p4 = 0x044C

;; @const pfnWNetAdd_p12
;; @desc Dynamic WfW 3.11 WNetAddConnection(lpNetPath, lpPass, lpLocal) pointer (12B args).
@const pfnWNetAdd_p12 = 0x0450

;; @const pfnWNetCancel_p6
;; @desc Dynamic WfW 3.11 WNetCancelConnection(lpName, fForce) pointer (6B args).
@const pfnWNetCancel_p6 = 0x0454

; --- Scratch Buffers & Tables in DGROUP (0x0460..0x1FFF) ---
;; @const bmW16DbcsLead
;; @desc 32-byte (256-bit) O(1) DBCS lead-byte bitmap populated via IsDBCSLeadByte.
@const bmW16DbcsLead = 0x0460

;; @const bW16WndClass
;; @desc 26-byte WNDCLASS16 scratch structure for RegisterClass.
@const bW16WndClass = 0x0480

;; @const bW16MsgStruct
;; @desc 18-byte MSG16 scratch structure for GetMessage/DispatchMessage.
@const bW16MsgStruct = 0x04A0

;; @const bW16PaintStruct
;; @desc 32-byte PAINTSTRUCT16 scratch structure for BeginPaint/EndPaint.
@const bW16PaintStruct = 0x04B4

;; @const bW16TextMetric
;; @desc 32-byte TEXTMETRIC scratch structure for GetTextMetrics.
@const bW16TextMetric = 0x04D8

;; @const bW16MemManBuf
;; @desc 36-byte TOOLHELP MEMMANINFO scratch structure (dwSize = 34).
@const bW16MemManBuf = 0x04FC

;; @const bW16OfStruct
;; @desc 136-byte OFSTRUCT scratch buffer for OpenFile.
@const bW16OfStruct = 0x0520

;; @const wW16LineLen
;; @desc Current byte length of interactive input line in bW16InputBuf.
@const wW16LineLen = 0x05AC

;; @const wW16LineCur
;; @desc Current cursor byte index inside bW16InputBuf.
@const wW16LineCur = 0x05AE

;; @const wW16LastLen
;; @desc Saved length of previous command template (for F1/F3/Up recall).
@const wW16LastLen = 0x05B0

;; @const wW16PushdDepth
;; @desc Current PUSHD directory/UNC drive stack depth (0..4).
@const wW16PushdDepth = 0x05B2

;; @const bW16PushdUncDrv
;; @desc Mapped drive letter ('Z') if top of PUSHD stack mounted a WfW 3.11 UNC share.
@const bW16PushdUncDrv = 0x05B4

;; @const bW16Trn286Mark
;; @desc Non-zero marker set by discardable _TRN286 segment on WINCMD286.EXE.
@const bW16Trn286Mark = 0x05B5

;; @const bW16RowRender
;; @desc 160-byte contiguous character row extraction scratch for GDI TextOut.
@const bW16RowRender = 0x05C0

;; @const bW16InputBuf
;; @desc 256-byte active interactive command line buffer.
@const bW16InputBuf = 0x0660

;; @const bW16LastCmdBuf
;; @desc 256-byte previous command template buffer for DOSKEY Up / F1 / F3.
@const bW16LastCmdBuf = 0x0760

;; @const bW16ScratchA
;; @desc 260-byte general path/token/dropfile scratch buffer A.
@const bW16ScratchA = 0x0860

;; @const bW16ScratchB
;; @desc 260-byte general file/batch line scratch buffer B.
@const bW16ScratchB = 0x0968

;; @const bW16CwdBuf
;; @desc 128-byte current working directory string inside Win16 shell.
@const bW16CwdBuf = 0x0A70

;; @const bW16PushdPrev
;; @desc 128-byte saved previous CWD for PUSHD/POPD.
@const bW16PushdPrev = 0x0AF0

;; @const bW16EnvBlock
;; @desc 1024-byte in-process KEY=VALUE\0...\0\0 environment block (0x0B80..0x0F7F).
@const bW16EnvBlock = 0x0B80
