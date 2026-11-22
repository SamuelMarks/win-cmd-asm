;; @file w16_main.asm
;; @isa 8086
;; @desc Universal 8086 Win16 New Executable (NE, expver 1.01) for Windows 1.0, 2.0, 2.1x, 3.0, 3.1/3.11 & 3.2.

@cpu 8086
@format bin16

@include "../inc/win_equ.inc"
@include "../inc/ne_strc.inc"
@include "w16_equ.inc"

; --- 64-byte MZ Header (0x0000..0x003F) pointing to NE header at 0x0080 ---
@data dw mzHdrWords, IMAGE_DOS_SIGNATURE, 0x0080, 0x0001, 0x0000, 0x0004, 0x0000, 0xFFFF
@data dw mzHdrRegs, 0x0000, 0x00B8, 0x0000, 0x0000, 0x0000, 0x0040, 0x0000
@res word mzHdrPad, 16
@data dd mzLfanew, 0x00000080

; --- 64-byte Real-Mode DOS Stub (0x0040..0x007F) ---
;; @proc w16_dos_stub
;; @abi dos16
;; @in CS=DS=PSP
;; @out Prints Windows requirement notice and exits with code 1
;; @err none
;; @clobbers ax, dx
;; @desc Real-mode MS-DOS fallback stub embedded at offset 0x0040 of WINCMD16.EXE.
@proc w16_dos_stub
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, 0x0014
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C01
    int 0x21
    pop ds
    ret
@endproc

@data db szDosStubMsg, "This program requires Microsoft Windows.", 0x0D, 0x0A, '$'
@res byte padTo0080, (0x0080 - ($ - wincmd_seg_base))

; --- 64-byte Win16 NE Header (0x0080..0x00BF, IMAGE_OS2_HEADER, expver 1.01) ---
; ne_heap = 0x4000 (16 KiB local heap), ne_stack = 0x2000 (8 KiB stack) in DGROUP!
@data dw neMagic, IMAGE_OS2_SIGNATURE
@data db neLinkerVer, 5, 10
@data dw neEntTab, 0x0130, 0x000A
@data dd neCrc, 0x00000000
@data dw neFlagsAuto, 0x0302, 0x0002
@data dw neHeapStack, 0x4000, 0x2000
@data dd neCsIp, 0x00010000
@data dd neSsSp, 0x00020000
@data dw neCounts, 0x0002, 0x0003, 0x0013
@data dw neOffsets, 0x0040, 0x0058, 0x00A0, 0x00B8, 0x00C0
@data dd neNresOff, 0x000001C0
@data dw neMisc1, 0x0001, 0x0004, 0x0000
@data db neExeTypeFlags, 0x02, 0x00
@data dw neThunksExpVer, 0x0000, 0x0000, 0x0000, 0x0101

; --- NE Segment Table (0x00C0..0x00D7: _TEXT @ 0x0300 w/ RELOCINFO, _DATA @ 0x2800) ---
@data dw seg1Text, 0x0030, 0x2300, 0x0150, 0x2300
@data dw seg2Data, 0x0280, 0x0400, 0x0051, 0x2000
@res word seg3Unused, 4

; --- NE Resource Table (0x00D8..0x011F: RT_ICON, RT_GROUP_ICON, RT_MENU) ---
@include "w16_rsrc.inc"
@res byte padTo0120, (0x0120 - ($ - wincmd_seg_base))

; --- NE Resident Name Table (0x0120..0x0137: "WINCMD16" ord 0, "WNDPROC" ord 1) ---
@data db resModName, 8, "WINCMD16", 0x00, 0x00
@data db resExpWnd, 7, "WNDPROC", 0x01, 0x00, 0x00
@res byte padTo0138, (0x0138 - ($ - wincmd_seg_base))

; --- NE Module Reference Table (0x0138..0x013F: KERNEL=1, USER=8, GDI=13) ---
@data dw modRefTab, 0x0001, 0x0008, 0x000D, 0x0000

; --- NE Imported Names Table (0x0140..0x01AF) ---
@data db impNames, 0x00, 6, "KERNEL", 4, "USER", 3, "GDI", 0x00
@res byte padTo01B0, (0x01B0 - ($ - wincmd_seg_base))

; --- NE Entry Table (0x01B0..0x01BF: movable exported ordinal 1 -> Seg 1 : 0x00E8) ---
@data db entBundle1, 0x01, 0xFF, 0x03, 0xCD, 0x3F, 0x01
@data dw entWndOff, W16_WNDPROC_OFF
@data db entEnd, 0x00, 0x00
@res byte padTo01C0, (0x01C0 - ($ - wincmd_seg_base))

; --- NE Non-Resident Name Table (0x01C0..0x01FF) ---
@data db nresDesc, 15, "WINCMD16 Shell ", 0x00, 0x00, 0x00
@res byte padTo0200, (0x0200 - ($ - wincmd_seg_base))

; --- Shared Resources (0x0200..0x02FF) + Segment 1 _TEXT Core (0x0300..) ---
@include "w16_core.inc"

;; @proc w16_yield_timeslice
;; @abi win16
;; @in none
;; @out Releases current VM timeslice via INT 2Fh AX=1680h
;; @err none
;; @clobbers ax
;; @desc Idle CPU release helper safe across Real, 286 Standard, and 386 Enhanced Mode.
@proc w16_yield_timeslice
    mov ax, 0x1680
    int 0x2F
    ret
@endproc

;; @proc w16_entry
;; @abi win16
;; @in DS = DGROUP selector, CX = heap size, DI = hInstance
;; @out Invokes InitTask + w16_winmain_lifecycle and terminates via INT 21h AH=4Ch
;; @err Exits with status 1 if InitTask fails
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Universal 8086 Real/Protected-Mode Win16 entrypoint (Windows 1.01..3.2).
@proc w16_entry
    mov ax, ds
    nop
    inc bp
    push bp
    mov bp, sp
    push ds
    mov ds, ax

    ; InitTask (KERNEL.91): returns AX!=0, DX=nCmdShow, ES:BX=lpCmdLine, DI=hInstance
    push es
    call far [cs:IMP_INITTASK_P0]
    test ax, ax
    jz .init_fail
    mov word [wW16HInst], di
    mov word [wW16CmdShow], dx
    mov word [wW16CmdLineOff], bx
    mov word [wW16CmdLineSel], es
    pop es

    call w16_yield_timeslice
    call w16_winmain_lifecycle
    mov ah, 0x4C
    int 0x21
    jmp .done

.init_fail:
    pop es
    mov ax, 0x4C01
    int 0x21

.done:
    pop ds
    pop bp
    dec bp
    retf
@endproc

@res byte padTextTo2600, (0x2600 - ($ - wincmd_seg_base))

; --- Segment 1 Per-Segment Relocation Table (0x2600..0x27FF: 56 NE_RELOC_ITEMs) ---
@data dw wSeg1RelocCount, 56
@include "w16_relc.inc"
@res byte padRelocTo2800, (0x2800 - ($ - wincmd_seg_base))

; --- Segment 2: _DATA DGROUP (0x2800..0x2BFF) ---
@include "w16_data.inc"
