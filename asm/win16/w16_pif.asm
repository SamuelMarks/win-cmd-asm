;; @file w16_pif.asm
;; @isa 8086
;; @desc Authentic 545-byte Windows 1.0..3.2 WINOLDAP PIF (369B std + WINDOWS 286 3.0 + WINDOWS 386 3.0).

@cpu 8086
@format bin16

@include "../inc/ne_strc.inc"

; --- Classic 369-byte Windows 1.x / 2.x Standard PIF Header (0x0000..0x0170) ---
@data db bPifRes0, 0x00, 0x78
@data db szPifTitle, "WINCMD Pure-ASM Shell         "
@data dw wPifMemLimits, 640, 128
@data db szPifProg, "WINCMD.COM", 0
@res byte padPifProg, 52
@data db bPifFlags1Drive, 0x10, 0x00
@res byte szPifDefDir, 64
@res byte szPifCmdParams, 64
@data db bPifScreenRegs, 0x7F, 0x01, 0x00, 0xFF, 25, 80, 0, 0
@data dw wPifSysMem, 7
@res byte szPifSharedProg, 64
@res byte szPifSharedData, 64
@data db bPifFlags23, 0x00, 0x00

; --- Section 0: MICROSOFT PIFEX Root Extension Header (0x0171..0x0186, 22B) ---
@data db szPifExRoot, "MICROSOFT PIFEX", 0
@data dw wPifExRootLinks, 0x0187, 0x0000, 0x0171

; --- Section 1: WINDOWS 286 3.0 Standard-Mode Extension (0x0187..0x01A2, 22B + 6B) ---
@data db szPifEx286, "WINDOWS 286 3.0", 0
@data dw wPifEx286Links, 0x01A3, 0x019D, 0x0006
@data dw wPif286Data, 1024, 0, 0x0000

; --- Section 2: WINDOWS 386 3.0 Enhanced-Mode Extension (0x01A3..0x0220, 22B + 104B) ---
@data db szPifEx386, "WINDOWS 386 3.0", 0
@data dw wPifEx386Links, 0xFFFF, 0x01B9, 0x0068
; 104-byte 386 Enhanced block: MaxMem=640, MinMem=128, Fg=100, Bg=50, EMS=1024/0, XMS=1024/0,
; Flags=Windowed+Background+DetectIdle (0x0002000A), VideoFlags=Text(0x001F), + 64B CmdParams
@data dw wPif386Limits, 640, 128, 100, 50, 1024, 0, 1024, 0
@data dd dPif386Flags, 0x0002000A, 0x0000001F
@res word wPif386Rsvd, 8
@res byte szPif386Params, 64
