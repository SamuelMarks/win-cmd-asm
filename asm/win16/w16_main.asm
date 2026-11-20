;; @file w16_main.asm
;; @isa 8086
;; @desc Universal 8086 Win16 New Executable (NE, expver 1.01) for Windows 1.0, 2.0, 2.1x, 3.0, 3.1/3.11 & 3.2.

@cpu 8086
@format bin16

@include "../inc/win_equ.inc"
@include "../inc/ne_strc.inc"

; --- 64-byte MZ Header (0x0000..0x003F) pointing to NE header at 0x0080 ---
@data dw mzHdrWords, IMAGE_DOS_SIGNATURE, 0x0080, 0x0001, 0x0000, 0x0004, 0x0000, 0xFFFF
@data dw mzHdrRegs, 0x0000, 0x00B8, 0x0000, 0x0000, 0x0000, 0x0040, 0x0000
@res word mzHdrPad, 16
@data dd mzLfanew, 0x00000080

; --- 64-byte Real-Mode DOS Stub (0x0040..0x007F) ---
;; @proc w16_dos_stub
;; @abi dos16
;; @in CS=DS=PSP
;; @out Prints Windows requirement notice and exits with code 1
;; @err none
;; @clobbers ax, dx
;; @desc Real-mode MS-DOS fallback stub embedded at offset 0x0040 of WINCMD16.EXE.
@proc w16_dos_stub
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

@data db szDosStubMsg, "WINCMD16 requires Microsoft Windows.", 0x0D, 0x0A, '$'
@res byte padTo0080, (128 - ($ - wincmd_seg_base))

; --- 64-byte Win16 NE Header (0x0080..0x00BF, IMAGE_OS2_HEADER) ---
@data dw neMagic, IMAGE_OS2_SIGNATURE
@data db neLinkerVer, 5, 10
@data dw neEntTab, 0x0078, 0x0002
@data dd neCrc, 0x00000000
@data dw neFlagsAuto, 0x0302, 0x0002
@data dw neHeapStack, 0x1000, 0x1000
@data dd neCsIp, 0x00010000
@data dd neSsSp, 0x00020000
@data dw neCounts, 0x0002, 0x0001, 0x0013
@data dw neOffsets, 0x0040, 0x0050, 0x0052, 0x005E, 0x0060
@data dd neNresOff, 0x00000100
@data dw neMisc1, 0x0000, 0x0004, 0x0000
@data db neExeTypeFlags, 0x02, 0x00
@data dw neThunksExpVer, 0x0000, 0x0000, 0x0000, 0x0101

; --- NE Segment Table (0x00C0..0x00CF: _TEXT @ 0x0120, _DATA @ 0x0180) ---
@data dw seg1Text, 0x0012, 0x0060, 0x0050, 0x0060
@data dw seg2Data, 0x0018, 0x0040, 0x0051, 0x1040

; --- NE Empty Resource Table (0x00D0..0x00D1) ---
@data dw rsrcAlign, 0x0000

; --- NE Resident Name Table (0x00D2..0x00DD: "WINCMD16", ord 0) ---
@data db resModName, 8, "WINCMD16", 0x00, 0x00, 0x00

; --- NE Module Reference & Import Name Tables (0x00DE..0x00F7: "KERNEL") ---
@data dw modRefKernel, 0x0001
@data db impNameTab, 0x00, 6, "KERNEL", 0x00
@res byte padTo00F8, (248 - ($ - wincmd_seg_base))

; --- NE Entry Table (0x00F8..0x00FF) ---
@data db entTabEnd, 0x00, 0x00
@res byte padTo0100, (256 - ($ - wincmd_seg_base))

; --- NE Non-Resident Name Table (0x0100..0x011F) ---
@data db nresDesc, 15, "WINCMD16 Shell ", 0x00, 0x00, 0x00
@res byte padTo0120, (288 - ($ - wincmd_seg_base))

; --- Segment 1: _TEXT (0x0120..0x017F, strict 8086 + selector-clean!) ---
;; @proc w16_entry
;; @abi win16
;; @in DS = DGROUP selector, CX = heap size, DI = hInstance
;; @out Terminates cleanly via INT 21h AH=4Ch without segment arithmetic
;; @err none
;; @clobbers ax, bx, dx
;; @desc Real/Protected-Mode walkable Win16 entrypoint compatible with Windows 1.01..3.2.
@proc w16_entry
    ; Standard Win16 Real-Mode walkable FAR PASCAL prologue
    mov ax, ds
    nop
    inc bp
    push bp
    mov bp, sp
    push ds
    mov ds, ax

    call w16_yield_timeslice

    pop ds
    pop bp
    dec bp
    mov ax, 0x4C00
    int 0x21
    retf
@endproc

;; @proc w16_yield_timeslice
;; @abi win16
;; @in none
;; @out Releases current VM timeslice via INT 2Fh AX=1680h under Win 3.x/WINOLDAP
;; @err none
;; @clobbers ax
;; @desc Idle CPU release helper safe across Real, 286 Standard, and 386 Enhanced Mode.
@proc w16_yield_timeslice
    mov ax, 0x1680
    int 0x2F
    ret
@endproc

;; @proc w16_dbcs_check_cp936
;; @abi win16
;; @in AL = lead byte candidate
;; @out CF=1 if AL in GB2312/CP936 range (0xA1..0xFE) for Windows 3.2 Chinese edition
;; @err none
;; @clobbers none
;; @desc Selector-safe GB2312/CP936 DBCS lead-byte detector for Windows 3.2 CJK support.
@proc w16_dbcs_check_cp936
    cmp al, 0xA1
    jb .ascii
    cmp al, 0xFE
    ja .ascii
    stc
    ret
.ascii:
    clc
    ret
@endproc

@res byte padTo0180, (384 - ($ - wincmd_seg_base))

; --- Segment 2: _DATA DGROUP (0x0180..0x01BF, 16-byte Win16 instance header + banner) ---
@res word w16DataHdr, 8
@data db szW16Class, "WINCMD16_TTY", 0
@data db szW16Banner, "WINCMD16 [Windows 1.0-3.2]", 0
@res byte padTo01C0, (448 - ($ - wincmd_seg_base))
