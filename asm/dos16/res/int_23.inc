;; @file int_23.inc
;; @isa 8086
;; @desc Resident INT 23h Ctrl+C / Ctrl+Break ISR latching asynchronous abort safely.

@cpu 8086
@format inc

;; @proc int23_isr
;; @abi dos16
;; @in Hardware or MS-DOS Ctrl+C / Ctrl+Break software interrupt
;; @out Sets [cs:resState + RES_STATE.bCtrlBreakLatched] = 1 with CF=0 (no DOS re-entrancy)
;; @err none
;; @clobbers none
;; @desc Latches Ctrl+Break inside RESIDENT so TRANSIENT polls safely between statements.
@proc int23_isr
    mov byte [cs:resState + RES_STATE.bCtrlBreakLatched], 1
    clc
    iret
@endproc
