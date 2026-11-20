;; @file k_dos700.inc
;; @isa 8086
;; @desc MS-DOS 7.0/7.10/8.00 (Windows 95/98/Me) LFN (AX=71xxh) and >2 GiB FAT32 (AX=7303h) HAL.

@cpu 8086
@format inc

;; @proc k700_probe_lfn
;; @abi dos16
;; @in DS:DX = root path (e.g. "C:"), ES:DI = 16-byte FS name scratch
;; @out CF=0 if LFN API (AX=71xxh) active on volume, CF=1 otherwise
;; @err CF=1 when LFN unsupported
;; @clobbers ax, bx, cx
;; @desc Probes Windows 95 Long Filename volume support via INT 21h AX=71A0h.
@proc k700_probe_lfn
    cmp byte [bTrnDosMajor], 7
    jb .no_lfn
    mov cx, 16
    stc
    mov ax, DOS_LFN_VOL_INFO
    int 0x21
    jc .no_lfn
    test bx, 0x4000
    jz .no_lfn
    clc
    ret
.no_lfn:
    stc
    ret
@endproc

;; @proc k700_mkdir
;; @abi dos16
;; @in DS:DX = ASCIIZ long directory path
;; @out Creates LFN directory via AX=7139h (falls back to AH=39h if 7100h unsupported)
;; @err CF=1, AX = ERR_*
;; @clobbers ax, cx, si
;; @desc Creates a directory supporting 260-byte Long Filenames on MS-DOS 7.x+.
@proc k700_mkdir
    cmp byte [bTrnDosMajor], 7
    jb .use_200
    stc
    mov ax, DOS_LFN_MKDIR
    int 0x21
    jnc .ok
    cmp ax, 0x7100
    je .use_200
    stc
    ret
.use_200:
    call k200_mkdir
    ret
.ok:
    clc
    ret
@endproc

;; @proc k700_rmdir
;; @abi dos16
;; @in DS:DX = ASCIIZ long directory path
;; @out Removes LFN directory via AX=713Ah (falls back to AH=3Ah if 7100h unsupported)
;; @err CF=1, AX = ERR_*
;; @clobbers ax, cx, si
;; @desc Removes an empty directory supporting Long Filenames on MS-DOS 7.x+.
@proc k700_rmdir
    cmp byte [bTrnDosMajor], 7
    jb .use_200
    stc
    mov ax, DOS_LFN_RMDIR
    int 0x21
    jnc .ok
    cmp ax, 0x7100
    je .use_200
    stc
    ret
.use_200:
    call k200_rmdir
    ret
.ok:
    clc
    ret
@endproc

;; @proc k700_chdir
;; @abi dos16
;; @in DS:DX = ASCIIZ long directory path
;; @out Changes current directory via AX=713Bh (falls back to AH=3Bh if 7100h unsupported)
;; @err CF=1, AX = ERR_*
;; @clobbers ax, cx, si
;; @desc Changes current directory supporting 260-byte Long Filenames on MS-DOS 7.x+.
@proc k700_chdir
    cmp byte [bTrnDosMajor], 7
    jb .use_200
    stc
    mov ax, DOS_LFN_CHDIR
    int 0x21
    jnc .ok
    cmp ax, 0x7100
    je .use_200
    stc
    ret
.use_200:
    call k200_chdir
    ret
.ok:
    clc
    ret
@endproc

;; @proc k700_getcwd
;; @abi dos16
;; @in DL = drive (0=default, 1=A:, ...), DS:SI = 260-byte output buffer
;; @out Populates LFN current directory via AX=7147h (fallback to AH=47h)
;; @err CF=1, AX = ERR_*
;; @clobbers ax, si
;; @desc Queries up to 260-byte Long Filename working directory on MS-DOS 7.x+.
@proc k700_getcwd
    cmp byte [bTrnDosMajor], 7
    jb .use_200
    stc
    mov ax, DOS_LFN_GETCWD
    int 0x21
    jnc .ok
    cmp ax, 0x7100
    je .use_200
    stc
    ret
.use_200:
    call k200_getcwd
    ret
.ok:
    clc
    ret
@endproc

;; @proc k700_delete
;; @abi dos16
;; @in DS:DX = ASCIIZ LFN path/pattern, SI = wildcard flag (0 or 1)
;; @out Deletes file(s) via AX=7141h (fallback to AH=41h)
;; @err CF=1, AX = ERR_*
;; @clobbers ax, cx
;; @desc Deletes files supporting Long Filenames on MS-DOS 7.x+.
@proc k700_delete
    cmp byte [bTrnDosMajor], 7
    jb .use_200
    mov cx, 0x0027
    stc
    mov ax, DOS_LFN_DELETE
    int 0x21
    jnc .ok
    cmp ax, 0x7100
    je .use_200
    stc
    ret
.use_200:
    mov ah, DOS_UNLINK
    int 0x21
    ret
.ok:
    clc
    ret
@endproc

;; @proc k700_rename
;; @abi dos16
;; @in DS:DX = old LFN path, ES:DI = new LFN path
;; @out Renames file/directory via AX=7156h (fallback to AH=56h)
;; @err CF=1, AX = ERR_*
;; @clobbers ax
;; @desc Renames or moves a file supporting Long Filenames on MS-DOS 7.x+.
@proc k700_rename
    cmp byte [bTrnDosMajor], 7
    jb .use_200
    stc
    mov ax, DOS_LFN_RENAME
    int 0x21
    jnc .ok
    cmp ax, 0x7100
    je .use_200
    stc
    ret
.use_200:
    mov ah, DOS_RENAME
    int 0x21
    ret
.ok:
    clc
    ret
@endproc

;; @proc k700_truename
;; @abi dos16
;; @in DS:SI = source path, ES:DI = 260B output, CL = mode (0=canon, 1=short 8.3, 2=long LFN)
;; @out Writes canonical/short/long path to ES:DI via AX=7160h
;; @err CF=1, AX = ERR_*
;; @clobbers ax, cx
;; @desc Resolves canonical, 8.3 short (%~s1), or LFN long (%~f1) path on MS-DOS 7.x+.
@proc k700_truename
    cmp byte [bTrnDosMajor], 7
    jb .use_400
    mov ch, 0
    stc
    mov ax, DOS_LFN_TRUENAME
    int 0x21
    jnc .ok
    cmp ax, 0x7100
    je .use_400
    stc
    ret
.use_400:
    call k400_truename
    ret
.ok:
    clc
    ret
@endproc

;; @proc k700_find_first
;; @abi dos16
;; @in DS:DX = LFN wildcard pattern, CX = attr mask, ES:DI = 318-byte WIN32_FIND_DATAA
;; @out AX = LFN search handle, [ES:DI] populated (SI=1 DOS date/time format), CF=0
;; @err CF=1, AX = ERR_* (or 0x7100 if LFN unsupported)
;; @clobbers ax, cx, si
;; @desc Starts a Windows 95 LFN directory enumeration via INT 21h AX=714Eh.
@proc k700_find_first
    mov si, 1
    stc
    mov ax, DOS_LFN_FIND_FIRST
    int 0x21
    ret
@endproc

;; @proc k700_find_next
;; @abi dos16
;; @in BX = LFN search handle, ES:DI = 318-byte WIN32_FIND_DATAA
;; @out Populates next LFN entry into [ES:DI], CF=0 on match
;; @err CF=1 when no more files
;; @clobbers ax, si
;; @desc Advances a Windows 95 LFN directory enumeration via INT 21h AX=714Fh.
@proc k700_find_next
    mov si, 1
    stc
    mov ax, DOS_LFN_FIND_NEXT
    int 0x21
    ret
@endproc

;; @proc k700_find_close
;; @abi dos16
;; @in BX = LFN search handle from k700_find_first
;; @out Releases kernel LFN search handle via INT 21h AX=71A1h
;; @err CF=1 on invalid handle
;; @clobbers ax
;; @desc Mandatory Windows 95 LFN FindClose preventing VMM search handle leaks.
@proc k700_find_close
    stc
    mov ax, DOS_LFN_FIND_CLOSE
    int 0x21
    ret
@endproc

;; @proc k700_fat32_free64
;; @abi dos16
;; @in DS:DX = root path ("C:"), DS:DI = 8-byte u64 output, ES:BX = 44B EXT_FREE_SPACE scratch
;; @out Computes 64-bit >2 GiB FAT32 free byte count into [DS:DI] via AX=7303h
;; @err CF=1 if FAT32 AX=7303h unsupported on current DOS kernel
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Queries Windows 95 OSR2 / 98 / Me FAT32 multi-gigabyte free space without 2 GiB clamp.
@proc k700_fat32_free64
    cmp byte [bTrnDosMajor], 7
    jae .ver_ok7
    stc
    ret
.ver_ok7:
    push bp
    mov bp, sp
    sub sp, 4
    mov word [bp - 2], di
    mov word [bp - 4], bx

    mov di, bx
    mov word [es:di], 44
    mov word [es:di + 2], 0
    mov cx, 44
    stc
    mov ax, DOS_FAT32_FREE_SPACE
    int 0x21
    jc .early_fail
    cmp ax, 0x7300
    jne .calc_fat32
.early_fail:
    jmp .fail_pop
.calc_fat32:

    ; Step 1: bytes_per_cluster (32-bit DX:AX) = dwSectorsPerCluster * dwBytesPerSector
    mov bx, word [bp - 4]
    mov dx, word [bx + EXT_FREE_SPACE.dwSectorsPerCluster + 2]
    mov ax, word [bx + EXT_FREE_SPACE.dwSectorsPerCluster]
    mov cx, word [bx + EXT_FREE_SPACE.dwBytesPerSector]
    push bx
    xor bx, bx
    call u32_mul
    pop si

    ; Step 2: 64-bit free_bytes = (DX:AX bytes_per_cluster) * (dwFreeClusters hi:lo)
    ; bpc_lo = AX, bpc_hi = DX; fc_lo = [si+12], fc_hi = [si+14]
    mov di, word [bp - 2]
    push dx
    push ax
    ; low_32 * fc_lo -> [di..di+3]
    mov cx, word [si + EXT_FREE_SPACE.dwFreeClusters]
    mul cx
    mov word [di], ax
    mov word [di + 2], dx
    mov word [di + 4], 0
    mov word [di + 6], 0

    ; low_32 * fc_hi -> add into [di+2..di+5]
    pop ax
    push ax
    mov cx, word [si + EXT_FREE_SPACE.dwFreeClusters + 2]
    mul cx
    add word [di + 2], ax
    adc word [di + 4], dx
    adc word [di + 6], 0

    ; hi_32 * fc_lo -> add into [di+2..di+5]
    pop ax
    pop dx
    push dx
    mov ax, dx
    mov cx, word [si + EXT_FREE_SPACE.dwFreeClusters]
    mul cx
    add word [di + 2], ax
    adc word [di + 4], dx
    adc word [di + 6], 0

    ; hi_32 * fc_hi -> add into [di+4..di+7]
    pop ax
    mov cx, word [si + EXT_FREE_SPACE.dwFreeClusters + 2]
    mul cx
    add word [di + 4], ax
    adc word [di + 6], dx

    add sp, 4
    pop bp
    clc
    ret

.fail_pop:
    add sp, 4
    pop bp
.unsup:
    stc
    ret
@endproc
