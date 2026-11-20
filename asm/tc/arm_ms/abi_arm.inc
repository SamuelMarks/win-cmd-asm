;; @file abi_arm.inc
;; @isa armv7
;; @desc Native LLVM-MC / Microsoft armasm 32-bit ARMv4 and ARMv7-A Thumb-2 ABI header.

// ARM32 Windows ABI: R0-R3 scratch args, R4-R11 callee-saved, R13 (SP) 8B-aligned, R14 (LR).
.equ ARM32_SUBSYS_WINCE, 9
.equ ARM32_SUBSYS_CUI, 3
