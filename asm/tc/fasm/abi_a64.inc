;; @file abi_a64.inc
;; @isa arm64
;; @desc Native FASMARM 64-bit Windows ARM64 ABI header (X18 reserved TEB, 16B SP alignment).

; HINT-encoded backward-compatible PAC/BTI opcodes on ARMv8.0+
A64_HINT_BTI_C   = 0xD503245F
A64_HINT_PACIASP = 0xD503233F
A64_HINT_AUTIASP = 0xD50323BF
