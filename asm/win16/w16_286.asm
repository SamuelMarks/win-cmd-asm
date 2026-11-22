;; @file w16_286.asm
;; @isa 286
;; @desc Protected-Mode 80286 Win16 New Executable (NE, expver 3.00, discardable _TRN286) for Win 2.1x/286 & 3.0-3.2.

@cpu 286
@format bin16

@include "../inc/win_equ.inc"
@include "../inc/ne_strc.inc"
@include "w16_equ.inc"

; --- 64-byte MZ Header (0x0000..0x003F) ---
@data dw mz286Words, IMAGE_DOS_SIGNATURE, 0x0080, 0x0001, 0x0000, 0x0004, 0x0000, 0xFFFF
@data dw mz286Regs, 0x0000, 0x00B8, 0x0000, 0x0000, 0x0000, 0x0040, 0x0000
@res word mz286Pad, 16
@data dd mz286Lfanew, 0x00000080

; --- 64-byte DOS Stub (0x0040..0x007F) ---
;; @proc w286_dos_stub
;; @abi dos16
;; @in CS=DS=PSP
;; @out Prints Protected-Mode Windows requirement and exits with 1
;; @err none
;; @clobbers ax, dx
;; @desc Real-mode fallback stub for WINCMD286.EXE.
@proc w286_dos_stub
    push ds
    mov ax, cs
    mov ds, ax
    mov dx, 0x0014
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C01
    int 0x21
    pop ds
    ret
@endproc

@data db sz286StubMsg, "WINCMD286 requires Windows/286 2.1+", 0x0D, 0x0A, '$'
@res byte pad286To0080, (0x0080 - ($ - wincmd_seg_base))

; --- 64-byte NE Header (0x0080..0x00BF: PROTMODE 0x030A, 80286 flag 0x02, expver 0x0300, 3 segments) ---
@data dw ne286Magic, IMAGE_OS2_SIGNATURE
@data db ne286Ver, 5, 10
@data dw ne286EntTab, 0x0130, 0x000A
@data dd ne286Crc, 0x00000000
@data dw ne286FlagsAuto, 0x030A, 0x0002
@data dw ne286HeapStack, 0x4000, 0x2000
@data dd ne286CsIp, 0x00010000
@data dd ne286SsSp, 0x00020000
@data dw ne286Counts, 0x0003, 0x0003, 0x0014
@data dw ne286Offsets, 0x0040, 0x0058, 0x00A0, 0x00B8, 0x00C0
@data dd ne286NresOff, 0x000001C0
@data dw ne286Misc1, 0x0001, 0x0004, 0x0000
@data db ne286ExeTypeFlags, 0x02, 0x02
@data dw ne286ThunksExpVer, 0x0000, 0x0000, 0x0000, 0x0300

; --- NE Segment Table (0x00C0..0x00D7: _TEXT, _DATA, + DISCARDABLE _TRN286 @ 0x2C00 wFlags=0x1050!) ---
@data dw s286Text, 0x0030, 0x2300, 0x0150, 0x2300
@data dw s286Data, 0x0280, 0x0400, 0x0051, 0x2000
@data dw s286Trn, 0x02C0, 0x0200, 0x1050, 0x0200

; --- NE Resource Table (0x00D8..0x011F) ---
@include "w16_rsrc.inc"
@res byte pad286To0120, (0x0120 - ($ - wincmd_seg_base))

; --- NE Resident Name Table (0x0120..0x0137) ---
@data db r286ModName, 9, "WINCMD286", 0x00, 0x00
@data db r286ExpWnd, 7, "WNDPROC", 0x01, 0x00, 0x00
@res byte pad286To0138, (0x0138 - ($ - wincmd_seg_base))

; --- NE Module Reference Table (0x0138..0x013F) ---
@data dw m286RefTab, 0x0001, 0x0008, 0x000D, 0x0000

; --- NE Imported Names Table (0x0140..0x01AF) ---
@data db i286Names, 0x00, 6, "KERNEL", 4, "USER", 3, "GDI", 0x00
@res byte pad286To01B0, (0x01B0 - ($ - wincmd_seg_base))

; --- NE Entry Table (0x01B0..0x01BF) ---
@data db e286Bundle, 0x01, 0xFF, 0x03, 0xCD, 0x3F, 0x01
@data dw e286WndOff, W16_WNDPROC_OFF
@data db e286End, 0x00, 0x00
@res byte pad286To01C0, (0x01C0 - ($ - wincmd_seg_base))

; --- NE Non-Resident Name Table (0x01C0..0x01FF) ---
@data db nr286Desc, 16, "WINCMD286 Shell ", 0x00, 0x00, 0x00
@res byte pad286To0200, (0x0200 - ($ - wincmd_seg_base))

; --- Shared Resources (0x0200..0x02FF) + Segment 1 _TEXT Core (0x0300..) ---
@include "w16_core.inc"

;; @proc w16_entry
;; @abi win16
;; @in DS = DGROUP LDT selector, DI = hInstance
;; @out Runs .286p ENTER/LEAVE/PUSHA/POPA, invokes discardable _TRN286, runs Win16 terminal
;; @err Exits with status 1 if InitTask fails
;; @clobbers ax, bx, cx, dx, si, di
;; @desc 80286 Protected-Mode entrypoint tuned for Windows 2.1x/286, 3.0, 3.1, WfW 3.11 & 3.2.
@proc w16_entry
    enter 4, 0
    push ds
    push es

    call far [cs:IMP_INITTASK_P0]
    test ax, ax
    jz .init_fail
    mov word [wW16HInst], di
    mov word [wW16CmdShow], dx
    mov word [wW16CmdLineOff], bx
    mov word [wW16CmdLineSel], es

    ; Invoke discardable Segment 3 (_TRN286) via INTERNALREF far pointer!
    call far [cs:IMP_TRN286_ENTRY_P0]

    call w16_winmain_lifecycle
    pop es
    pop ds
    leave
    mov ah, 0x4C
    int 0x21
    retf

.init_fail:
    pop es
    pop ds
    leave
    mov ax, 0x4C01
    int 0x21
    retf
@endproc

@res byte pad286TextTo2600, (0x2600 - ($ - wincmd_seg_base))

; --- Segment 1 Per-Segment Relocation Table (0x2600..0x27FF: 57 NE_RELOC_ITEMs) ---
@data dw w286RelocCount, 57
@include "w16_relc.inc"
; Relocation #57: INTERNALREF (bAddrType=3 FAR_ADDR, bRelocType=0 INTERNALREF, Seg=3 _TRN286, Off=0)
@data db relTrn286, 3, 0
@data dw relTrn286W, IMP_TRN286_ENTRY_P0, 3, 0x0000
@res byte pad286RelocTo2800, (0x2800 - ($ - wincmd_seg_base))

; --- Segment 2: _DATA DGROUP (0x2800..0x2BFF) ---
@include "w16_data.inc"

; --- Segment 3: _TRN286 (0x2C00..0x2DFF, PRELOAD | MOVEABLE | DISCARDABLE = 0x1050) ---
;; @proc w286_trn_init
;; @abi win16
;; @in DS = DGROUP LDT selector
;; @out Exercises .286p PUSHA/POPA, SHL imm, PUSH imm, ENTER/LEAVE and sets bW16Trn286Mark=1
;; @err none
;; @clobbers ax
;; @desc Discardable 80286 Protected-Mode transient segment routine.
@proc w286_trn_init
    enter 2, 0
    pusha
    push 0x0021
    pop ax
    shl ax, 2
    mov ax, 0x1680
    int 0x2F
    mov byte [bW16Trn286Mark], 1
    popa
    leave
    retf
@endproc

@res byte pad286TrnTo2E00, (0x2E00 - ($ - wincmd_seg_base))
