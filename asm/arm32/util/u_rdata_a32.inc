;; @file u_rdata_a32.inc
;; @isa armv7
;; @desc Shared read-only strings for ARM32 Thumb-2 PE32 utilities.

@cpu armv7
@format inc

@data db szUa32Crlf, 13, 10, 0
@align 2
@data dw u16Ua32Kernel32, 'K', 'E', 'R', 'N', 'E', 'L', '3', '2', '.', 'D', 'L', 'L', 0
@data dw u16Ua32Advapi32, 'A', 'D', 'V', 'A', 'P', 'I', '3', '2', '.', 'D', 'L', 'L', 0
@data dw u16Ua32Psapi, 'P', 'S', 'A', 'P', 'I', '.', 'D', 'L', 'L', 0
@data dw u16Ua32ConIn, 'C', 'O', 'N', 'I', 'N', '$', 0
