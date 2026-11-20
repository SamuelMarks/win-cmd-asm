;; @file wincmd_m.asm
;; @isa 8086
;; @desc External MS-DOS 4.00+ WINCMD.MSG indexed extended error message table (/MSG file).

@cpu 8086
@format bin16

;; @const MSG_FILE_MAGIC
;; @desc 4-byte ASCII signature ("WMSG") identifying WINCMD.MSG on disk.
@const MSG_FILE_MAGIC = 0x4753

@data db bMsgHdrMagic, "WMSG", 0x01, 0x00
@data dw wMsgCount, 8

@data dw msgIdx1, 0x0001
@data db szMsg1, "Invalid function number", 0
@data dw msgIdx2, 0x0002
@data db szMsg2, "File not found", 0
@data dw msgIdx3, 0x0003
@data db szMsg3, "Path not found", 0
@data dw msgIdx4, 0x0004
@data db szMsg4, "Too many open files", 0
@data dw msgIdx5, 0x0005
@data db szMsg5, "Access denied", 0
@data dw msgIdx15, 0x000F
@data db szMsg15, "Invalid drive specification", 0
@data dw msgIdx80, 0x0050
@data db szMsg80, "File exists", 0
@data dw msgIdx83, 0x0053
@data db szMsg83, "Fail on INT 24", 0
@data dw msgEnd, 0x0000
