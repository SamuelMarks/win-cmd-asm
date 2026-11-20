;; @file abi16.inc
;; @isa 8086
;; @desc Native NASM 16-bit Real/Protected-Mode ABI macros and CPU 8086 enforcement.

%ifndef WINCMD_NASM_ABI16_INC
%define WINCMD_NASM_ABI16_INC
%ifdef TARGET_286
CPU 286
%else
CPU 8086
%endif
BITS 16
%endif
