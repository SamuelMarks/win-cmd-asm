;; @file arena.inc
;; @isa 8086
;; @desc OOM-safe 16-bit word-aligned bump arena with 0xA55A canary inside TRANSIENT.

@cpu 8086
@format inc

;; @const ARENA_CANARY_MAGIC
;; @desc 16-bit guard word placed at [wArenaLimit] to detect stack/arena collision (0xA55A).
@const ARENA_CANARY_MAGIC = 0xA55A

;; @proc arena_init
;; @abi dos16
;; @in AX = arena base offset, DX = arena limit offset (word-aligned)
;; @out [wArenaBase], [wArenaPtr], [wArenaLimit] initialized and canary written
;; @err none
;; @clobbers bx
;; @desc Initializes the transient statement bump arena and plants the 0xA55A guard word.
@proc arena_init
    mov word [wArenaBase], ax
    mov word [wArenaPtr], ax
    mov word [wArenaLimit], dx
    mov bx, dx
    mov word [bx], ARENA_CANARY_MAGIC
    ret
@endproc

;; @proc arena_reset
;; @abi dos16
;; @in none
;; @out [wArenaPtr] reset to [wArenaBase], CF=0 if canary intact
;; @err CF=1, AX=ERR_MCB_DESTROYED if canary was overwritten
;; @clobbers ax, bx
;; @desc Resets the bump pointer in O(1) and verifies the 0xA55A stack guard word.
@proc arena_reset
    mov ax, word [wArenaBase]
    mov word [wArenaPtr], ax
    mov bx, word [wArenaLimit]
    cmp word [bx], ARENA_CANARY_MAGIC
    jne .corrupt
    xor ax, ax
    clc
    ret
.corrupt:
    mov word [bx], ARENA_CANARY_MAGIC
    mov ax, ERR_MCB_DESTROYED
    stc
    ret
@endproc

;; @proc arena_alloc
;; @abi dos16
;; @in CX = requested byte count
;; @out DI = 2-byte word-aligned allocation offset, CF=0 on success
;; @err CF=1, AX=ERR_OUT_OF_MEMORY on arena exhaustion
;; @clobbers ax, cx, di
;; @desc Allocates CX bytes (rounded up to even word boundary) from the transient arena.
@proc arena_alloc
    mov ax, cx
    inc ax
    and ax, 0xFFFE
    jc .oom
    mov di, word [wArenaPtr]
    add ax, di
    jc .oom
    cmp ax, word [wArenaLimit]
    ja .oom
    mov word [wArenaPtr], ax
    clc
    ret
.oom:
    mov ax, ERR_OUT_OF_MEMORY
    stc
    ret
@endproc
