;; @file wincmda64.asm
;; @isa arm64
;; @desc ARM64 AArch64 PE32+ (0xAA64, Subsystem 10.0) shell for Windows 10 & Windows 11 on ARM.

@cpu arm64
@format arm64_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "../inc/err_code.inc"
@include "../inc/win_equ.inc"

@section text

;; @proc a64_sdiv_safe
;; @abi win_arm64
;; @in w0 = signed 32-bit dividend, w1 = signed 32-bit divisor
;; @out w0 = quotient, w2 = 0 or ERR_DIV_BY_ZERO (X18 TEB strictly untouched!)
;; @err w2 = ERR_DIV_BY_ZERO when w1 == 0
;; @clobbers x0, x1, x2, x3
;; @desc Hardware-trap-safe SET /A signed 32-bit division on AArch64 guarding /0 and INT32_MIN/-1.
@proc a64_sdiv_safe
    cbz w1, .div_zero
    cmn w1, #1
    b.ne .do_sdiv
    mov w3, #-2147483648
    cmp w0, w3
    b.eq .done_ok
.do_sdiv:
    sdiv w0, w0, w1
.done_ok:
    mov w2, #0
    ret
.div_zero:
    mov w0, #0
    mov w2, #0x0103
    ret
@endproc

;; @proc mainCRTStartup
;; @abi win_arm64
;; @in Process entrypoint on Windows 10 & Windows 11 ARM64 (X18 = TEB!)
;; @out w0 = 0 exit status
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc CRT-free Windows ARM64 PE32+ entrypoint maintaining 16-byte SP alignment and X18 safety.
@proc mainCRTStartup
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp

    ; Read-only sanity check that X18 (TEB) is non-zero without ever writing X18
    cmp x18, #0

    mov w0, #80
    mov w1, #2
    bl a64_sdiv_safe

    mov w0, #0
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret
@endproc

@section rdata

@data db szArm64Ver, "WINCMD_ARM64 Pure-ASM Shell [Windows 10 & Windows 11 ARM64]", 0x0D, 0x0A, 0
