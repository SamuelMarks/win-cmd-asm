;; @file pe32.inc
;; @isa 386
;; @desc Native NASM Win32 PE32 Subsystem 3.10 SAFESEH helper equates.

%ifndef WINCMD_NASM_PE32_INC
%define WINCMD_NASM_PE32_INC
PE32_SUBSYS_CUI       equ 3
PE32_MAJOR_NT31       equ 3
PE32_MINOR_NT31       equ 10
PE_DLLCHAR_DYNBASE    equ 0x0040
PE_DLLCHAR_NXCOMPAT   equ 0x0100
%endif
