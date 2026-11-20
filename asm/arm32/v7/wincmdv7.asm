;; @file wincmdv7.asm
;; @isa armv7
;; @desc ARMv7-A Thumb-2 UAL PE32 (0x01C4 ARMNT, Subsystem 6.02) shell for Windows RT 8/8.1 & Win10 ARM32.

@cpu armv7
@format armv7_pe
@entry mainCRTStartup
@global mainCRTStartup

@include "../../inc/err_code.inc"
@include "../../inc/win_equ.inc"

@section text

;; @proc armv7_sdiv_safe
;; @abi aapcs_vfp
;; @in r0 = signed 32-bit dividend, r1 = signed 32-bit divisor
;; @out r0 = quotient, r2 = 0 on success or ERR_DIV_BY_ZERO on /0
;; @err r2 = ERR_DIV_BY_ZERO when r1 == 0
;; @clobbers r0, r1, r2, r3
;; @desc Trap-safe SET /A signed division guarding /0 and INT32_MIN / -1 before SDIV.
@proc armv7_sdiv_safe
    cbz r1, .div_zero
    cmn r1, #1
    bne .do_sdiv
    movs r3, #1
    lsls r3, r3, #31
    cmp r0, r3
    beq .min_neg1
.do_sdiv:
    sdiv r0, r0, r1
    movs r2, #0
    bx lr
.min_neg1:
    movs r2, #0
    bx lr
.div_zero:
    movs r0, #0
    movw r2, #0x0103
    bx lr
@endproc

;; @proc mainCRTStartup
;; @abi aapcs_vfp
;; @in Process entrypoint on Windows RT 8.0/8.1 and Windows 10 ARM32
;; @out r0 = 0 exit status
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc CRT-free ARMv7-A Thumb-2 entrypoint (8B SP-aligned, IT-clean).
@proc mainCRTStartup
    push {r4, lr}
    movs r0, #80
    movs r1, #2
    bl armv7_sdiv_safe
    movs r0, #0
    pop {r4, lr}
    bx lr
@endproc

@section rdata

@data db szArmV7Ver, "WINCMD_ARM32 Pure-ASM Shell [Windows RT 8/8.1 & Win10 ARMNT]", 0x0D, 0x0A, 0
