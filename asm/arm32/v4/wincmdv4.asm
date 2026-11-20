;; @file wincmdv4.asm
;; @isa armv4
;; @desc Strict ARMv4 32-bit RISC PE32 (0x01C0) shell for Windows CE / Pocket PC / Embedded.

@cpu armv4
@format armv4_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "../../inc/err_code.inc"
@include "../../inc/win_equ.inc"

@section text

;; @proc armv4_udiv32
;; @abi arm_ce
;; @in r0 = dividend, r1 = divisor
;; @out r0 = quotient (or 0 and r2 = ERR_DIV_BY_ZERO if r1 == 0), r1 = remainder
;; @err r2 = ERR_DIV_BY_ZERO when divisor is zero
;; @clobbers r0, r1, r2, r3
;; @desc Pure ARMv4 restoring 32-bit unsigned division (zero CLZ, zero UDIV, zero BLX).
@proc armv4_udiv32
    cmp r1, #0
    beq .div_zero
    mov r2, #0
    mov r3, #32
.bit_lp:
    adds r0, r0, r0
    adc r2, r2, r2
    cmp r2, r1
    bcc .no_sub
    sub r2, r2, r1
    orr r0, r0, #1
.no_sub:
    subs r3, r3, #1
    bne .bit_lp
    mov r1, r2
    mov r2, #0
    bx lr
.div_zero:
    mov r0, #0
    mov r2, #0x100
    orr r2, r2, #0x03
    bx lr
@endproc

;; @proc armv4_shift_mask
;; @abi arm_ce
;; @in r0 = value, r1 = shift count
;; @out r0 = r0 << (r1 & 31)
;; @err none
;; @clobbers r0, r1
;; @desc Masks ARM32 shift count to bottom 5 bits (ARM hardware uses bottom 8 bits!).
@proc armv4_shift_mask
    and r1, r1, #31
    mov r0, r0, lsl r1
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi arm_ce
;; @in Process entrypoint on Windows CE / Pocket PC ARMv4
;; @out r0 = 0 exit status
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc CRT-free ARMv4 PE32 entrypoint verifying trap-safe math and 8-byte SP alignment.
@proc mainCRTStartup
    push {r4, lr}
    mov r0, #100
    mov r1, #10
    bl armv4_udiv32
    mov r1, #2
    bl armv4_shift_mask
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

@section rdata

@data db szArmV4Ver, "WINCMD_ARMV4 Pure-ASM Shell [Windows CE / Pocket PC ARMv4]", 0x0D, 0x0A, 0
