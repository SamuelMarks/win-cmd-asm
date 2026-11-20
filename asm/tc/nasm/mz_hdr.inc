;; @file mz_hdr.inc
;; @isa 8086
;; @desc Native NASM MZ DOS executable header emission helper macro.

%ifndef WINCMD_NASM_MZ_HDR_INC
%define WINCMD_NASM_MZ_HDR_INC
%macro EMIT_MZ_STUB_HDR 1 ; %1 = e_lfanew offset
    dw 0x5A4D, 0x0080, 0x0001, 0x0000, 0x0004, 0x0000, 0xFFFF
    dw 0x0000, 0x00B8, 0x0000, 0x0000, 0x0000, 0x0040, 0x0000
    times 16 dw 0
    dd %1
%endmacro
%endif
