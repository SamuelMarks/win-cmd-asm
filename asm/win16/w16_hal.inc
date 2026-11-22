;; @file w16_hal.inc
;; @isa 8086
;; @desc Selector-safe Win16 HAL (__AHINCR huge pointers, dynamic 3.0..3.2 APIs, VFS & WfW 3.11 UNC).

@cpu 8086
@format inc

;; @proc w16_str_len
;; @abi win16
;; @in DS:SI = NUL-terminated string
;; @out CX = byte length (excluding NUL)
;; @err none
;; @clobbers ax, cx
;; @desc Computes byte length of a NUL-terminated string in DGROUP without modifying SI.
@proc w16_str_len
    push si
    xor cx, cx
.lp:
    mov al, byte [si]
    test al, al
    jz .done
    inc si
    inc cx
    jmp .lp
.done:
    pop si
    ret
@endproc

;; @proc w16_str_copy
;; @abi win16
;; @in DS:SI = source NUL-terminated string, DS:DI = destination buffer in DGROUP
;; @out Copies string including NUL terminator
;; @err none
;; @clobbers ax, si, di
;; @desc Selector-safe intra-DGROUP string copy without ES mutation.
@proc w16_str_copy
.lp:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .lp
    ret
@endproc

;; @proc w16_huge_step_test
;; @abi win16
;; @in DS = DGROUP
;; @out Allocates >64 KiB (70 KiB) GlobalAlloc block, steps selector via KERNEL.114 __AHINCR, and frees
;; @err CF=1 if GlobalAlloc fails
;; @clobbers ax, bx, cx, dx
;; @desc Verifies 286 Protected-Mode selector-safe >64 KiB stepping via __AHINCR without #GP.
@proc w16_huge_step_test
    push es
    ; GlobalAlloc(GMEM_MOVEABLE_ZEROINIT, 0x0001_1800 = 71,680 bytes)
    mov ax, GMEM_MOVEABLE_ZEROINIT
    push ax
    mov ax, 0x0001
    push ax
    mov ax, 0x1800
    push ax
    call far [cs:IMP_GLOBALALLOC_P6]
    test ax, ax
    jz .fail
    mov word [hW16HugeTest], ax

    push ax
    call far [cs:IMP_GLOBALLOCK_P2]
    test dx, dx
    jz .free_fail

    ; Write sentinel to tile 0 (DX:0000), then step DX by [cs:IMP_AHINCR] to tile 1 (64 KiB+)!
    mov es, dx
    mov word [es:0x0000], 0x5AA5
    add dx, word [cs:IMP_AHINCR]
    mov es, dx
    mov word [es:0x0010], 0xA55A
    cmp word [es:0x0010], 0xA55A
    jne .unlock_fail

    push word [hW16HugeTest]
    call far [cs:IMP_GLOBALUNLOCK_P2]
    push word [hW16HugeTest]
    call far [cs:IMP_GLOBALFREE_P2]
    mov word [hW16HugeTest], 0
    pop es
    clc
    ret

.unlock_fail:
    push word [hW16HugeTest]
    call far [cs:IMP_GLOBALUNLOCK_P2]
.free_fail:
    push word [hW16HugeTest]
    call far [cs:IMP_GLOBALFREE_P2]
.fail:
    pop es
    stc
    ret
@endproc

;; @proc w16_resolve_proc
;; @abi win16
;; @in AX = hModule (may be 0), SI = offset in DGROUP of uppercase export name, DI = DGROUP slot (4B)
;; @out Writes DX:AX FARPROC into [DI..DI+3] (or 0:0 if module/symbol absent)
;; @err none
;; @clobbers ax, bx, cx, dx
;; @desc Resolves a single optional Win 3.0/3.1/WfW 3.11/3.2 API via GetProcAddress.
@proc w16_resolve_proc
    push es
    mov word [di], 0
    mov word [di + 2], 0
    test ax, ax
    jz .done
    push ax
    push ds
    push si
    call far [cs:IMP_GETPROCADDRESS_P6]
    mov word [di], ax
    mov word [di + 2], dx
.done:
    pop es
    ret
@endproc

;; @proc w16_init_dyn_apis
;; @abi win16
;; @in DS = DGROUP
;; @out Resolves Win 3.0+ WinExec/GetFreeSpace, 3.1 SHELL/TOOLHELP, WfW 3.11 WNet*, and 3.2 DBCS
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Populates optional 3.x dynamic function pointers while keeping Windows 1.0/2.0 boot clean.
@proc w16_init_dyn_apis
    push es

    ; 1. KERNEL dynamic exports (WinExec, GetFreeSpace, IsDBCSLeadByte)
    push ds
    mov ax, (szModKernel - w16_data_base)
    push ax
    call far [cs:IMP_GETMODULEHANDLE_P4]
    mov bx, ax

    mov ax, bx
    mov si, (szFnWinExec - w16_data_base)
    mov di, pfnWinExec_p6
    push bx
    call w16_resolve_proc
    pop bx

    mov ax, bx
    mov si, (szFnGetFreeSpace - w16_data_base)
    mov di, pfnGetFreeSpace_p2
    push bx
    call w16_resolve_proc
    pop bx

    mov ax, bx
    mov si, (szFnIsDbcs - w16_data_base)
    mov di, pfnIsDbcs_p2
    call w16_resolve_proc

    ; 2. USER / WFWNET exports (GetFreeSystemResources, WNetAddConnection, WNetCancelConnection)
    push ds
    mov ax, (szModUser - w16_data_base)
    push ax
    call far [cs:IMP_GETMODULEHANDLE_P4]
    mov bx, ax

    mov ax, bx
    mov si, (szFnGetSysRes - w16_data_base)
    mov di, pfnGetSysRes_p2
    push bx
    call w16_resolve_proc
    pop bx

    mov ax, bx
    mov si, (szFnWNetAdd - w16_data_base)
    mov di, pfnWNetAdd_p12
    push bx
    call w16_resolve_proc
    pop bx

    mov ax, bx
    mov si, (szFnWNetCancel - w16_data_base)
    mov di, pfnWNetCancel_p6
    call w16_resolve_proc

    ; 3. SHELL.DLL (Windows 3.1+ DragAcceptFiles, DragQueryFile, DragFinish)
    push ds
    mov ax, (szModShell - w16_data_base)
    push ax
    call far [cs:IMP_GETMODULEHANDLE_P4]
    mov bx, ax
    mov ax, bx
    mov si, (szFnDragAccept - w16_data_base)
    mov di, pfnDragAccept_p4
    push bx
    call w16_resolve_proc
    pop bx
    mov ax, bx
    mov si, (szFnDragQuery - w16_data_base)
    mov di, pfnDragQuery_p10
    push bx
    call w16_resolve_proc
    pop bx
    mov ax, bx
    mov si, (szFnDragFinish - w16_data_base)
    mov di, pfnDragFinish_p2
    call w16_resolve_proc

    ; 4. TOOLHELP.DLL (Windows 3.1+ MemManInfo)
    push ds
    mov ax, (szModToolhelp - w16_data_base)
    push ax
    call far [cs:IMP_GETMODULEHANDLE_P4]
    mov si, (szFnMemManInfo - w16_data_base)
    mov di, pfnMemManInfo_p4
    call w16_resolve_proc

    call w16_init_dbcs
    pop es
    ret
@endproc

;; @proc w16_vfs_open_read
;; @abi win16
;; @in DS:SI = filename in DGROUP
;; @out CF=0 and AX = HFILE on success; CF=1 on error
;; @err CF=1 when file not found
;; @clobbers ax, bx, cx, dx
;; @desc Opens an existing file for reading via KERNEL.74 OpenFile(OF_READ = 0).
@proc w16_vfs_open_read
    push es
    push ds
    push si
    push ds
    mov ax, bW16OfStruct
    push ax
    xor ax, ax
    push ax
    call far [cs:IMP_OPENFILE_P10]
    cmp ax, 0xFFFF
    je .err
    pop es
    clc
    ret
.err:
    pop es
    stc
    ret
@endproc

;; @proc w16_vfs_delete
;; @abi win16
;; @in DS:SI = filename in DGROUP
;; @out CF=0 on success, CF=1 if missing
;; @err CF=1 when OpenFile(OF_DELETE = 0x0200) fails
;; @clobbers ax, bx, cx, dx
;; @desc Deletes a file via KERNEL.74 OpenFile(OF_DELETE).
@proc w16_vfs_delete
    push es
    push ds
    push si
    push ds
    mov ax, bW16OfStruct
    push ax
    mov ax, 0x0200
    push ax
    call far [cs:IMP_OPENFILE_P10]
    cmp ax, 0xFFFF
    je .err
    pop es
    clc
    ret
.err:
    pop es
    stc
    ret
@endproc
