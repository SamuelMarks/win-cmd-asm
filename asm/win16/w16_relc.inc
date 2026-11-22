;; @file w16_relc.inc
;; @isa 8086
;; @desc 56 shared NE_RELOC_ITEM fixup records for KERNEL, USER, and GDI Windows 1.01+ ordinals.

@cpu 8086
@format inc

; Each 8-byte NE_RELOC_ITEM: bAddrType, bRelocType, wOffset, wTarget1 (mod), wTarget2 (ordinal)
; Module 1 = KERNEL (22 relocations: __AHINCR + 21 FAR PASCAL entrypoints)
@data db relAhIncr, 2, 1
@data dw relAhIncrW, IMP_AHINCR, 1, 114

@data db relK01, 3, 1
@data dw relK01W, IMP_INITTASK_P0, 1, 91
@data db relK02, 3, 1
@data dw relK02W, IMP_WAITEVENT_P2, 1, 30
@data db relK03, 3, 1
@data dw relK03W, IMP_GETVERSION_P0, 1, 3
@data db relK04, 3, 1
@data dw relK04W, IMP_SETERRORMODE_P2, 1, 107
@data db relK05, 3, 1
@data dw relK05W, IMP_GLOBALALLOC_P6, 1, 15
@data db relK06, 3, 1
@data dw relK06W, IMP_GLOBALLOCK_P2, 1, 18
@data db relK07, 3, 1
@data dw relK07W, IMP_GLOBALUNLOCK_P2, 1, 19
@data db relK08, 3, 1
@data dw relK08W, IMP_GLOBALFREE_P2, 1, 17
@data db relK09, 3, 1
@data dw relK09W, IMP_GETMODULEHANDLE_P4, 1, 47
@data db relK10, 3, 1
@data dw relK10W, IMP_LOADLIBRARY_P4, 1, 95
@data db relK11, 3, 1
@data dw relK11W, IMP_GETPROCADDRESS_P6, 1, 50
@data db relK12, 3, 1
@data dw relK12W, IMP_MAKEPROCINSTANCE_P6, 1, 51
@data db relK13, 3, 1
@data dw relK13W, IMP_FREEPROCINSTANCE_P4, 1, 52
@data db relK14, 3, 1
@data dw relK14W, IMP_LOADMODULE_P8, 1, 45
@data db relK15, 3, 1
@data dw relK15W, IMP_GETMODULEUSAGE_P2, 1, 48
@data db relK16, 3, 1
@data dw relK16W, IMP_OPENFILE_P10, 1, 74
@data db relK17, 3, 1
@data dw relK17W, IMP_LCLOSE_P2, 1, 81
@data db relK18, 3, 1
@data dw relK18W, IMP_LREAD_P8, 1, 82
@data db relK19, 3, 1
@data dw relK19W, IMP_LCREAT_P6, 1, 83
@data db relK20, 3, 1
@data dw relK20W, IMP_LLSEEK_P8, 1, 84
@data db relK21, 3, 1
@data dw relK21W, IMP_LWRITE_P8, 1, 85

; Module 2 = USER (30 FAR PASCAL entrypoints)
@data db relU01, 3, 1
@data dw relU01W, IMP_INITAPP_P2, 2, 5
@data db relU02, 3, 1
@data dw relU02W, IMP_POSTQUITMESSAGE_P2, 2, 6
@data db relU03, 3, 1
@data dw relU03W, IMP_GETCLIENTRECT_P6, 2, 32
@data db relU04, 3, 1
@data dw relU04W, IMP_SETWINDOWTEXT_P6, 2, 36
@data db relU05, 3, 1
@data dw relU05W, IMP_BEGINPAINT_P6, 2, 39
@data db relU06, 3, 1
@data dw relU06W, IMP_ENDPAINT_P6, 2, 40
@data db relU07, 3, 1
@data dw relU07W, IMP_CREATEWINDOW_P30, 2, 41
@data db relU08, 3, 1
@data dw relU08W, IMP_SHOWWINDOW_P4, 2, 42
@data db relU09, 3, 1
@data dw relU09W, IMP_REGISTERCLASS_P4, 2, 57
@data db relU10, 3, 1
@data dw relU10W, IMP_SCROLLWINDOW_P14, 2, 61
@data db relU11, 3, 1
@data dw relU11W, IMP_SETSCROLLPOS_P8, 2, 62
@data db relU12, 3, 1
@data dw relU12W, IMP_SETSCROLLRANGE_P10, 2, 64
@data db relU13, 3, 1
@data dw relU13W, IMP_DEFWINDOWPROC_P10, 2, 107
@data db relU14, 3, 1
@data dw relU14W, IMP_GETMESSAGE_P10, 2, 108
@data db relU15, 3, 1
@data dw relU15W, IMP_TRANSLATEMESSAGE_P4, 2, 113
@data db relU16, 3, 1
@data dw relU16W, IMP_DISPATCHMESSAGE_P4, 2, 114
@data db relU17, 3, 1
@data dw relU17W, IMP_UPDATEWINDOW_P2, 2, 124
@data db relU18, 3, 1
@data dw relU18W, IMP_INVALIDATERECT_P8, 2, 125
@data db relU19, 3, 1
@data dw relU19W, IMP_OPENCLIPBOARD_P2, 2, 137
@data db relU20, 3, 1
@data dw relU20W, IMP_CLOSECLIPBOARD_P0, 2, 138
@data db relU21, 3, 1
@data dw relU21W, IMP_EMPTYCLIPBOARD_P0, 2, 139
@data db relU22, 3, 1
@data dw relU22W, IMP_SETCLIPBOARDDATA_P4, 2, 141
@data db relU23, 3, 1
@data dw relU23W, IMP_GETCLIPBOARDDATA_P2, 2, 142
@data db relU24, 3, 1
@data dw relU24W, IMP_LOADMENU_P6, 2, 150
@data db relU25, 3, 1
@data dw relU25W, IMP_CREATECARET_P8, 2, 163
@data db relU26, 3, 1
@data dw relU26W, IMP_DESTROYCARET_P0, 2, 164
@data db relU27, 3, 1
@data dw relU27W, IMP_SETCARETPOS_P4, 2, 165
@data db relU28, 3, 1
@data dw relU28W, IMP_SHOWCARET_P2, 2, 166
@data db relU29, 3, 1
@data dw relU29W, IMP_LOADCURSOR_P6, 2, 173
@data db relU30, 3, 1
@data dw relU30W, IMP_LOADICON_P6, 2, 176

; Module 3 = GDI (4 FAR PASCAL entrypoints)
@data db relG01, 3, 1
@data dw relG01W, IMP_TEXTOUT_P12, 3, 33
@data db relG02, 3, 1
@data dw relG02W, IMP_SELECTOBJECT_P4, 3, 45
@data db relG03, 3, 1
@data dw relG03W, IMP_GETSTOCKOBJECT_P2, 3, 87
@data db relG04, 3, 1
@data dw relG04W, IMP_GETTEXTMETRICS_P6, 3, 91
