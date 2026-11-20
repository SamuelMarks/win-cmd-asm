;; @file w16_286.asm
;; @isa 286
;; @desc Protected-Mode 80286 Win16 New Executable (NE, expver 3.00) for Win 2.1x/286 & 3.0-3.2.

@cpu 286
@format bin16

@include "../inc/win_equ.inc"
@include "../inc/ne_strc.inc"

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
@res byte pad286To0080, (128 - ($ - wincmd_seg_base))

; --- 64-byte NE Header (0x0080..0x00BF: PROTMODE 0x030A, 80286 flag 0x02, expver 0x0300) ---
@data dw ne286Magic, IMAGE_OS2_SIGNATURE
@data db ne286Ver, 5, 10
@data dw ne286EntTab, 0x0078, 0x0002
@data dd ne286Crc, 0x00000000
@data dw ne286FlagsAuto, 0x030A, 0x0002
@data dw ne286HeapStack, 0x2000, 0x2000
@data dd ne286CsIp, 0x00010000
@data dd ne286SsSp, 0x00020000
@data dw ne286Counts, 0x0002, 0x0001, 0x0014
@data dw ne286Offsets, 0x0040, 0x0050, 0x0052, 0x005E, 0x0060
@data dd ne286NresOff, 0x00000100
@data dw ne286Misc1, 0x0000, 0x0004, 0x0000
@data db ne286ExeTypeFlags, 0x02, 0x02
@data dw ne286ThunksExpVer, 0x0000, 0x0000, 0x0000, 0x0300

; --- NE Segment Table (0x00C0..0x00CF) ---
@data dw s286Text, 0x0012, 0x0060, 0x0050, 0x0060
@data dw s286Data, 0x0018, 0x0040, 0x0051, 0x2040

@data dw r286Align, 0x0000
@data db r286ModName, 9, "WINCMD286", 0x00, 0x00, 0x00
@data dw m286RefKernel, 0x0001
@data db i286NameTab, 0x00, 6, "KERNEL", 0x00
@res byte pad286To00F8, (248 - ($ - wincmd_seg_base))
@data db e286TabEnd, 0x00, 0x00
@res byte pad286To0100, (256 - ($ - wincmd_seg_base))
@data db nr286Desc, 16, "WINCMD286 Shell ", 0x00, 0x00, 0x00
@res byte pad286To0120, (288 - ($ - wincmd_seg_base))

; --- Segment 1: _TEXT (.286p selector-clean Protected-Mode code!) ---
;; @proc w286_entry
;; @abi win16
;; @in DS = DGROUP LDT selector
;; @out Exercises 80286 PUSHA/POPA and selector-safe Protected-Mode exit
;; @err none
;; @clobbers ax
;; @desc 80286 Protected-Mode Win16 entrypoint for Windows 2.1x/286, 3.0, 3.1/3.11 & 3.2.
@proc w286_entry
    push bp
    mov bp, sp
    push ds
    pusha
    mov ax, 0x1680
    int 0x2F
    popa
    pop ds
    pop bp
    mov ax, 0x4C00
    int 0x21
    retf
@endproc

@res byte pad286To0180, (384 - ($ - wincmd_seg_base))
@res word w286DataHdr, 8
@data db szW286Banner, "WINCMD286 [Protected Mode 286]", 0
@res byte pad286To01C0, (448 - ($ - wincmd_seg_base))
