;; @file abi_a64.inc
;; @isa arm64
;; @desc Native LLVM-MC / Microsoft armasm64 64-bit Windows ARM64 ABI header (X18 = TEB).

// Windows ARM64 ABI: X0-X7 args, X8 indirect ret, X9-X17 scratch, X18 RESERVED (TEB!),
// X19-X28 callee-saved, X29 (FP), X30 (LR), SP 16B-aligned.
.equ A64_HINT_BTI_C, 0xD503245F
.equ A64_HINT_PACIASP, 0xD503233F
.equ A64_HINT_AUTIASP, 0xD50323BF
