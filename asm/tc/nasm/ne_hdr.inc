;; @file ne_hdr.inc
;; @isa 8086
;; @desc Native NASM Win16 New Executable (NE) segment flag equates.

%ifndef WINCMD_NASM_NE_HDR_INC
%define WINCMD_NASM_NE_HDR_INC
NE_SEG_CODE     equ 0x0000
NE_SEG_DATA     equ 0x0001
NE_SEG_MOVEABLE equ 0x0010
NE_SEG_PRELOAD  equ 0x0040
NE_SEG_RELOCS   equ 0x0100
%endif
