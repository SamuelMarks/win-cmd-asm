;; @file dos_strc.inc
;; @isa all
;; @desc Exact Phase 19 assemble-time byte-asserted MS-DOS 1.25/2.00/4.00/7.x structures.

@format inc

;; @struc PSP
;; @desc 256-byte MS-DOS Program Segment Prefix (0x00..0xFF).
@struc PSP, 256
    @field wInt20, word, 1
    @field wMemTop, word, 1
    @field bRes1, byte, 1
    @field bCall5, byte, 5
    @field dwInt22, dword, 1
    @field dwInt23, dword, 1
    @field dwInt24, dword, 1
    @field wParentPsp, word, 1
    @field bJft, byte, 20
    @field wEnvSeg, word, 1
    @field dwSavedStack, dword, 1
    @field wJftSize, word, 1
    @field dwJftPtr, dword, 1
    @field bRes2, byte, 24
    @field bDosCall, byte, 3
    @field bRes3, byte, 9
    @field fcb1, byte, 16
    @field fcb2, byte, 20
    @field bCmdLen, byte, 1
    @field szCmdTail, byte, 127
@endstruc

;; @struc FCB
;; @desc 37-byte MS-DOS 1.25+ Standard File Control Block.
@struc FCB, 37
    @field bDrive, byte, 1
    @field szName, byte, 8
    @field szExt, byte, 3
    @field wCurBlock, word, 1
    @field wRecSize, word, 1
    @field dwFileSize, dword, 1
    @field wDate, word, 1
    @field wTime, word, 1
    @field bReserved, byte, 8
    @field bCurRec, byte, 1
    @field dwRandRec, dword, 1
@endstruc

;; @struc XFCB
;; @desc 44-byte MS-DOS Extended File Control Block (0xFF + 5 zeros + attr + FCB).
@struc XFCB, 44
    @field bFlag, byte, 1
    @field bZero, byte, 5
    @field bAttr, byte, 1
    @field fcb, byte, 37
@endstruc

;; @struc DTA_FIND
;; @desc 43-byte MS-DOS 2.00+ AH=4Eh/4Fh FindFirst/FindNext Disk Transfer Area buffer.
@struc DTA_FIND, 43
    @field bReserved, byte, 21
    @field bAttr, byte, 1
    @field wTime, word, 1
    @field wDate, word, 1
    @field dwSize, dword, 1
    @field szName, byte, 13
@endstruc

;; @struc WIN32_FIND_DATAA
;; @desc 318-byte MS-DOS 7.x / Windows 95 AX=714Eh Long Filename FindData buffer.
@struc WIN32_FIND_DATAA, 318
    @field dwFileAttributes, dword, 1
    @field qwCreationTime, qword, 1
    @field qwLastAccessTime, qword, 1
    @field qwLastWriteTime, qword, 1
    @field dwFileSizeHigh, dword, 1
    @field dwFileSizeLow, dword, 1
    @field dwReserved0, dword, 1
    @field dwReserved1, dword, 1
    @field cFileName, byte, 260
    @field cAlternateFileName, byte, 14
@endstruc

;; @struc EXT_FREE_SPACE
;; @desc 44-byte MS-DOS 7.10+ FAT32 AX=7303h Get_ExtFreeSpace structure.
@struc EXT_FREE_SPACE, 44
    @field wSize, word, 1
    @field wVersion, word, 1
    @field dwSectorsPerCluster, dword, 1
    @field dwBytesPerSector, dword, 1
    @field dwFreeClusters, dword, 1
    @field dwTotalClusters, dword, 1
    @field dwAvailPhysSectors, dword, 1
    @field dwTotalPhysSectors, dword, 1
    @field dwAvailAllocUnits, dword, 1
    @field dwTotalAllocUnits, dword, 1
    @field bReserved, byte, 8
@endstruc

;; @struc EXEC_BLK
;; @desc 14-byte MS-DOS 2.00+ AH=4Bh AL=00h Load and Execute parameter block.
@struc EXEC_BLK, 14
    @field wEnvSeg, word, 1
    @field dwCmdTail, dword, 1
    @field dwFcb1, dword, 1
    @field dwFcb2, dword, 1
@endstruc

;; @struc MCB
;; @desc 16-byte MS-DOS Arena Memory Control Block paragraph header.
@struc MCB, 16
    @field bType, byte, 1
    @field wOwner, word, 1
    @field wSize, word, 1
    @field bReserved, byte, 3
    @field szName, byte, 8
@endstruc

;; @struc MZ_HDR_MIN
;; @desc 28-byte classic MS-DOS 1.25+ MZ .EXE relocation header.
@struc MZ_HDR_MIN, 28
    @field wMagic, word, 1
    @field wLastPageBytes, word, 1
    @field wPages, word, 1
    @field wRelocCnt, word, 1
    @field wHdrParas, word, 1
    @field wMinAlloc, word, 1
    @field wMaxAlloc, word, 1
    @field wInitSS, word, 1
    @field wInitSP, word, 1
    @field wChecksum, word, 1
    @field wInitIP, word, 1
    @field wInitCS, word, 1
    @field wRelocOff, word, 1
    @field wOverlay, word, 1
@endstruc

;; @struc MZ_HDR
;; @desc 64-byte extended MZ .EXE header including e_lfanew (0x3C) NE/PE offset.
@struc MZ_HDR, 64
    @field wMagic, word, 1
    @field wLastPageBytes, word, 1
    @field wPages, word, 1
    @field wRelocCnt, word, 1
    @field wHdrParas, word, 1
    @field wMinAlloc, word, 1
    @field wMaxAlloc, word, 1
    @field wInitSS, word, 1
    @field wInitSP, word, 1
    @field wChecksum, word, 1
    @field wInitIP, word, 1
    @field wInitCS, word, 1
    @field wRelocOff, word, 1
    @field wOverlay, word, 1
    @field wReserved1, word, 4
    @field wOemId, word, 1
    @field wOemInfo, word, 1
    @field wReserved2, word, 10
    @field dwLfanew, dword, 1
@endstruc

;; @struc DISK_ID4
;; @desc 25-byte MS-DOS 4.00+ AX=6900h disk serial number and volume label block.
@struc DISK_ID4, 25
    @field wInfoLevel, word, 1
    @field dwSerialNum, dword, 1
    @field szVolLabel, byte, 11
    @field szFsType, byte, 8
@endstruc

;; @struc RES_STATE
;; @desc 128-byte persistent resident shell state block inside WINCMD.COM RESIDENT.
@struc RES_STATE, 128
    @field bSig, byte, 8
    @field wDosVer, word, 1
    @field bDosMajor, byte, 1
    @field bDosMinor, byte, 1
    @field bHwFlags, byte, 1
    @field bDialect, byte, 1
    @field wPspSeg, word, 1
    @field wEnvSeg, word, 1
    @field wTrnSeg, word, 1
    @field wTrnParas, word, 1
    @field wTrnCrc16, word, 1
    @field wEmsHandle, word, 1
    @field wXmsHandle, word, 1
    @field dwLastExitCode, dword, 1
    @field bEchoFlag, byte, 1
    @field bPermShell, byte, 1
    @field bSingleCmd, byte, 1
    @field bKeepShell, byte, 1
    @field bDelayedExp, byte, 1
    @field bCmdExt, byte, 1
    @field bAutoFail24, byte, 1
    @field bMsgResident, byte, 1
    @field bCtrlBreakLatched, byte, 1
    @field bBatchDepth, byte, 1
    @field wSetlocalTop, word, 1
    @field wPushdTop, word, 1
    @field bSetlocalCnt, byte, 1
    @field bColorAttr, byte, 1
    @field szComspec, byte, 80
@endstruc

;; @struc BATCH_FRAME
;; @desc 48-byte resident batch execution frame surviving child spawns and transient reloads.
@struc BATCH_FRAME, 48
    @field dwFileOff, dword, 1
    @field bShiftOff, byte, 1
    @field bFlags, byte, 1
    @field szPath, byte, 42
@endstruc

;; @struc XMS_MOVE_BLK
;; @desc 16-byte XMS Function 0Bh Extended Memory Move Specification structure.
@struc XMS_MOVE_BLK, 16
    @field dwLength, dword, 1
    @field wSrcHandle, word, 1
    @field dwSrcOffset, dword, 1
    @field wDstHandle, word, 1
    @field dwDstOffset, dword, 1
@endstruc
