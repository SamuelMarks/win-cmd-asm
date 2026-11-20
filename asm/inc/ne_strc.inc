;; @file ne_strc.inc
;; @isa all
;; @desc Exact Phase 19 assemble-time byte-asserted Win16 New Executable (NE) and .PIF structures.

@format inc

;; @struc IMAGE_OS2_HEADER
;; @desc 64-byte 16-bit Windows 1.0..3.2 New Executable (NE) module header.
@struc IMAGE_OS2_HEADER, 64
    @field ne_magic, word, 1
    @field ne_ver, byte, 1
    @field ne_rev, byte, 1
    @field ne_enttab, word, 1
    @field ne_cbenttab, word, 1
    @field ne_crc, dword, 1
    @field ne_flags, word, 1
    @field ne_autodata, word, 1
    @field ne_heap, word, 1
    @field ne_stack, word, 1
    @field ne_csip, dword, 1
    @field ne_sssp, dword, 1
    @field ne_cseg, word, 1
    @field ne_cmod, word, 1
    @field ne_cbnrestab, word, 1
    @field ne_segtab, word, 1
    @field ne_rsrctab, word, 1
    @field ne_restab, word, 1
    @field ne_modtab, word, 1
    @field ne_imptab, word, 1
    @field ne_nrestab, dword, 1
    @field ne_cmovent, word, 1
    @field ne_align, word, 1
    @field ne_cres, word, 1
    @field ne_exetyp, byte, 1
    @field ne_flagsothers, byte, 1
    @field ne_pretthunks, word, 1
    @field ne_psegrefbytes, word, 1
    @field ne_swaparea, word, 1
    @field ne_expver, word, 1
@endstruc

;; @struc NE_SEG_ENTRY
;; @desc 8-byte Win16 NE segment table descriptor.
@struc NE_SEG_ENTRY, 8
    @field wSectorOff, word, 1
    @field wByteLen, word, 1
    @field wFlags, word, 1
    @field wMinAlloc, word, 1
@endstruc

;; @struc NE_RELOC_ITEM
;; @desc 8-byte Win16 NE per-segment fixup relocation record.
@struc NE_RELOC_ITEM, 8
    @field bAddrType, byte, 1
    @field bRelocType, byte, 1
    @field wOffset, word, 1
    @field wTarget1, word, 1
    @field wTarget2, word, 1
@endstruc

;; @struc WNDCLASS16
;; @desc 26-byte Windows 1.0..3.2 RegisterClass WNDCLASS structure.
@struc WNDCLASS16, 26
    @field wStyle, word, 1
    @field lpfnWndProc, dword, 1
    @field cbClsExtra, word, 1
    @field cbWndExtra, word, 1
    @field hInstance, word, 1
    @field hIcon, word, 1
    @field hCursor, word, 1
    @field hbrBackground, word, 1
    @field lpszMenuName, dword, 1
    @field lpszClassName, dword, 1
@endstruc

;; @struc MSG16
;; @desc 18-byte Windows 1.0..3.2 GetMessage/DispatchMessage MSG structure.
@struc MSG16, 18
    @field hwnd, word, 1
    @field message, word, 1
    @field wParam, word, 1
    @field lParam, dword, 1
    @field time, dword, 1
    @field ptX, word, 1
    @field ptY, word, 1
@endstruc

;; @struc PAINTSTRUCT16
;; @desc 32-byte Windows 1.0..3.2 BeginPaint/EndPaint PAINTSTRUCT structure.
@struc PAINTSTRUCT16, 32
    @field hdc, word, 1
    @field fErase, word, 1
    @field rcLeft, word, 1
    @field rcTop, word, 1
    @field rcRight, word, 1
    @field rcBottom, word, 1
    @field fRestore, word, 1
    @field fIncUpdate, word, 1
    @field rgbReserved, byte, 16
@endstruc

;; @struc PIF_HDR_STD
;; @desc 369-byte classic Windows 1.x/2.x/3.x Program Information File (.PIF) base block.
@struc PIF_HDR_STD, 369
    @field bRes0, byte, 1
    @field bChecksum, byte, 1
    @field szTitle, byte, 30
    @field wMaxMem, word, 1
    @field wMinMem, word, 1
    @field szProgName, byte, 63
    @field bFlags1, byte, 1
    @field bDefDrive, byte, 1
    @field szDefDir, byte, 64
    @field szCmdParams, byte, 64
    @field bInitScreen, byte, 1
    @field bTextPages, byte, 1
    @field bFirstInt, byte, 1
    @field bLastInt, byte, 1
    @field bRows, byte, 1
    @field bCols, byte, 1
    @field bWinPosY, byte, 1
    @field bWinPosX, byte, 1
    @field wSysMem, word, 1
    @field szSharedProg, byte, 64
    @field szSharedData, byte, 64
    @field bFlags2, byte, 1
    @field bFlags3, byte, 1
@endstruc
