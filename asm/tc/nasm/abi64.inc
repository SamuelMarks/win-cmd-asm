;; @file abi64.inc
;; @isa x64
;; @desc Native NASM 64-bit Microsoft x64 ABI header (RIP-relative default, 32B shadow space).

%ifndef WINCMD_NASM_ABI64_INC
%define WINCMD_NASM_ABI64_INC
CPU X64
BITS 64
DEFAULT REL
%endif
