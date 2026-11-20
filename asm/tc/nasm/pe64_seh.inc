;; @file pe64_seh.inc
;; @isa x64
;; @desc Native NASM Win64 .pdata/.xdata UNWIND_INFO opcode constants.

%ifndef WINCMD_NASM_PE64_SEH_INC
%define WINCMD_NASM_PE64_SEH_INC
UWOP_PUSH_NONVOL      equ 0
UWOP_ALLOC_LARGE      equ 1
UWOP_ALLOC_SMALL      equ 2
UWOP_SET_FPREG        equ 3
%endif
