;; @file u_rdata_a64.inc
;; @isa arm64
;; @desc Shared read-only strings for ARM64 AArch64 PE32+ utilities.

@cpu arm64
@format inc

@data db szUa64Crlf, 13, 10, 0
@align 2
@data dw u16Ua64Kernel32, 'K', 'E', 'R', 'N', 'E', 'L', '3', '2', '.', 'D', 'L', 'L', 0
@data dw u16Ua64Advapi32, 'A', 'D', 'V', 'A', 'P', 'I', '3', '2', '.', 'D', 'L', 'L', 0
@data dw u16Ua64Psapi, 'P', 'S', 'A', 'P', 'I', '.', 'D', 'L', 'L', 0
@data dw u16Ua64ConIn, 'C', 'O', 'N', 'I', 'N', '$', 0
