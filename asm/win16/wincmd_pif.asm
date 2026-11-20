;; @file wincmd_pif.asm
;; @isa 8086
;; @desc Classic 369-byte Windows 1.0..3.2 WINOLDAP Program Information File (WINCMD.PIF).

@cpu 8086
@format bin16

@include "../inc/ne_strc.inc"

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
