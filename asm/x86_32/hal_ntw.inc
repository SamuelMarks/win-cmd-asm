;; @file hal_ntw.inc
;; @isa 386
;; @desc Windows NT 3.1..11 UTF-16LE (*W) HAL backend (\?\ 32K long paths, Reparse/ADS, MKLINK /D /H /J).

@cpu 386
@format inc

;; @proc ntw_path_to_w32
;; @abi win32
;; @in ESI = WTF-8 path string
;; @out EAX = UTF-16LE path in statement arena (auto-promoted to \?\ or \?\UNC\ when >= 240 WCHARs on NT), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Transcodes WTF-8 path to UTF-16LE and promotes long paths (>= 240 WCHARs) to \?\ up to 32,767 WCHARs.
@proc ntw_path_to_w32
    push ebx
    push esi
    push edi

    call wtf8_to_utf16le
    jc .fail
    mov ebx, eax
    cmp ecx, 240
    jb .ok_direct
    cmp byte [g_sim_nt31], 0
    jne .ok_direct

    ; Already prefixed with \?\ or \.\?
    cmp word [ebx], 0x5C
    jne .chk_drive
    cmp word [ebx + 2], 0x5C
    jne .ok_direct
    mov ax, word [ebx + 4]
    cmp ax, '?'
    je .ok_direct
    cmp ax, '.'
    je .ok_direct

    ; Promote \server\share\... -> \?\UNC\server\share\...
    lea ecx, [ecx + ecx + 24]
    call arena_alloc32
    jc .fail
    mov edi, eax
    mov edx, eax
    mov word [edi], 0x5C
    mov word [edi + 2], 0x5C
    mov word [edi + 4], '?'
    mov word [edi + 6], 0x5C
    mov word [edi + 8], 'U'
    mov word [edi + 10], 'N'
    mov word [edi + 12], 'C'
    mov word [edi + 14], 0x5C
    add edi, 16
    lea esi, [ebx + 4]
.cp_unc:
    mov ax, word [esi]
    mov word [edi], ax
    add esi, 2
    add edi, 2
    test ax, ax
    jnz .cp_unc
    mov eax, edx
    jmp .ok

.chk_drive:
    cmp word [ebx + 2], ':'
    jne .ok_direct
    mov ax, word [ebx + 4]
    cmp ax, 0x5C
    je .do_drv_long
    cmp ax, '/'
    jne .ok_direct
.do_drv_long:
    lea ecx, [ecx + ecx + 16]
    call arena_alloc32
    jc .fail
    mov edi, eax
    mov edx, eax
    mov word [edi], 0x5C
    mov word [edi + 2], 0x5C
    mov word [edi + 4], '?'
    mov word [edi + 6], 0x5C
    add edi, 8
    mov esi, ebx
.cp_drv:
    mov ax, word [esi]
    cmp ax, '/'
    jne .st_drv
    mov ax, 0x5C
.st_drv:
    mov word [edi], ax
    add esi, 2
    add edi, 2
    test ax, ax
    jnz .cp_drv
    mov eax, edx
    jmp .ok

.ok_direct:
    mov eax, ebx
.ok:
    pop edi
    pop esi
    pop ebx
    clc
    ret
.fail:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc ntw_query_reparse_tag
;; @abi win32
;; @in ESI = UTF-16LE path
;; @out EAX = ReparseTag (0xA0000003 Junction, 0xA000000C Symlink, 0x8000001B AppExecLink, or 0)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Queries NTFS/ReFS ReparseTag via DeviceIoControl(FSCTL_GET_REPARSE_POINT) when WIN32_FIND_DATAW dwReserved0 is 0.
@proc ntw_query_reparse_tag
    push ebx
    push edi

    push 0
    push FILE_FLAG_BACKUP_SEMANTICS + FILE_FLAG_OPEN_REPARSE_POINT
    push OPEN_EXISTING
    push 0
    push FILE_SHARE_READ + FILE_SHARE_WRITE
    push 0
    push esi
    call dword [__imp__CreateFileW@28]
    cmp eax, INVALID_HANDLE_VALUE
    je .no_tag
    mov ebx, eax

    sub esp, 68
    mov edi, esp
    lea edx, [esp + 64]
    mov dword [edx], 0
    push 0
    push edx
    push 64
    push edi
    push 0
    push 0
    push FSCTL_GET_REPARSE_POINT
    push ebx
    call dword [__imp__DeviceIoControl@32]
    mov ecx, eax
    mov edx, dword [edi]
    add esp, 68

    push ecx
    push edx
    push ebx
    call dword [__imp__CloseHandle@4]
    pop edx
    pop ecx
    test ecx, ecx
    jz .no_tag
    mov eax, edx
    pop edi
    pop ebx
    ret

.no_tag:
    xor eax, eax
    pop edi
    pop ebx
    ret
@endproc

;; @proc ntw_fill_find_entry
;; @abi win32
;; @in ESI = WIN32_FIND_DATAW (592B), EDI = FIND_ENTRY32 (560B)
;; @out Populates FIND_ENTRY32 fields and WTF-8 long/8.3 names
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Converts a 592-byte WIN32_FIND_DATAW into a normalized WTF-8 FIND_ENTRY32 structure.
@proc ntw_fill_find_entry
    push esi
    push edi

    mov eax, dword [esi + WIN32_FIND_DATAW.dwFileAttributes]
    mov dword [edi + FIND_ENTRY32.dwAttr], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.ftLastWriteTime]
    mov dword [edi + FIND_ENTRY32.ftWriteLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.ftLastWriteTime + 4]
    mov dword [edi + FIND_ENTRY32.ftWriteHi], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.ftCreationTime]
    mov dword [edi + FIND_ENTRY32.ftCreateLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.ftCreationTime + 4]
    mov dword [edi + FIND_ENTRY32.ftCreateHi], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.ftLastAccessTime]
    mov dword [edi + FIND_ENTRY32.ftAccessLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.ftLastAccessTime + 4]
    mov dword [edi + FIND_ENTRY32.ftAccessHi], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.nFileSizeLow]
    mov dword [edi + FIND_ENTRY32.nSizeLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.nFileSizeHigh]
    mov dword [edi + FIND_ENTRY32.nSizeHi], eax
    mov eax, dword [esi + WIN32_FIND_DATAW.dwReserved0]
    mov dword [edi + FIND_ENTRY32.dwReparseTag], eax

    push esi
    push edi
    lea esi, [esi + WIN32_FIND_DATAW.cAlternateFileName]
    lea edi, [edi + FIND_ENTRY32.szShort83]
    mov ecx, -1
    mov edx, 32
    call utf16le_to_wtf8_buf
    pop edi
    pop esi

    lea esi, [esi + WIN32_FIND_DATAW.cFileName]
    lea edi, [edi + FIND_ENTRY32.szNameWtf8]
    mov ecx, -1
    mov edx, 488
    call utf16le_to_wtf8_buf

    pop edi
    pop esi
    ret
@endproc

;; @proc ntw_mklink
;; @abi win32
;; @in ESI = link path (WTF-8), EDI = target path (WTF-8), ECX = mode (0=file symlink, 1=/D dir symlink, 2=/H hardlink, 3=/J junction)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Implements Vista..11 CreateSymbolicLinkW (+ Win10 unprivileged 0x2 retry), Win2000+ CreateHardLinkW, and NTFS /J junctions.
@proc ntw_mklink
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx

    call ntw_path_to_w32
    jc .done_err
    mov ebx, eax

    mov esi, edi
    call wtf8_to_utf16le
    jc .done_err
    mov edi, eax

    cmp ebp, 2
    je .do_hardlink
    cmp ebp, 3
    je .do_junction

    ; File or Directory Symbolic Link (Vista+)
    mov eax, dword [pfnCreateSymbolicLinkW_p12]
    test eax, eax
    jz .not_sup

    xor edx, edx
    cmp ebp, 1
    jne .try_sym
    mov edx, SYMBOLIC_LINK_FLAG_DIRECTORY
.try_sym:
    push edx
    push edx
    push edi
    push ebx
    call dword [pfnCreateSymbolicLinkW_p12]
    pop edx
    test al, al
    jnz .ok

    ; Retry with SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE (0x2) on Windows 10 1703+ / 11!
    or edx, SYMBOLIC_LINK_FLAG_UNPRIVILEGED
    push edx
    push edi
    push ebx
    call dword [pfnCreateSymbolicLinkW_p12]
    test al, al
    jnz .ok
    jmp .win_err

.do_hardlink:
    mov eax, dword [pfnCreateHardLinkW_p12]
    test eax, eax
    jz .not_sup
    push 0
    push edi
    push ebx
    call dword [pfnCreateHardLinkW_p12]
    test al, al
    jnz .ok
    jmp .win_err

.do_junction:
    ; NTFS Directory Junction (/J): resolve full target path -> create empty dir -> FSCTL_SET_REPARSE_POINT (\??\Target)
    mov ecx, 2048
    call arena_alloc32
    jc .done_err
    mov esi, eax
    push 0
    push esi
    push 1020
    push edi
    call dword [__imp__GetFullPathNameW@16]
    test eax, eax
    jnz .got_jfull
    mov esi, edi
.got_jfull:
    push 0
    push ebx
    call dword [__imp__CreateDirectoryW@8]
    test eax, eax
    jz .win_err

    ; Open newly created directory with GENERIC_WRITE | FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT
    push 0
    push FILE_FLAG_BACKUP_SEMANTICS + FILE_FLAG_OPEN_REPARSE_POINT
    push OPEN_EXISTING
    push 0
    push 0
    push GENERIC_WRITE
    push ebx
    call dword [__imp__CreateFileW@28]
    cmp eax, INVALID_HANDLE_VALUE
    je .rm_jdir_err
    mov ebp, eax

    ; Build MOUNT_POINT_REPARSE_BUFFER in arena:
    ; +0: ReparseTag = 0xA0000003 (IO_REPARSE_TAG_MOUNT_POINT)
    ; +4: ReparseDataLength (w), +6: Reserved (0)
    ; +8: SubstituteNameOffset (0), +10: SubstituteNameLength (bytes)
    ; +12: PrintNameOffset, +14: PrintNameLength (bytes)
    ; +16: PathBuffer = "\??\<full_target>\0<full_target>\0"
    call wcs_len32
    mov edi, ecx
    shl edi, 1
    lea ecx, [edi + edi + 64]
    call arena_alloc32
    jc .close_j_err
    mov edx, eax

    mov dword [edx], IO_REPARSE_TAG_MOUNT_POINT
    lea eax, [edi + 8]
    mov word [edx + 8], 0
    mov word [edx + 10], ax
    add eax, 2
    mov word [edx + 12], ax
    mov word [edx + 14], di
    lea ecx, [edi + edi + 20]
    mov word [edx + 4], cx
    mov word [edx + 6], 0

    ; Write "\??" prefix + target + NUL + printName + NUL
    mov word [edx + 16], 0x5C
    mov word [edx + 18], '?'
    mov word [edx + 20], '?'
    mov word [edx + 22], 0x5C
    push ebx
    push edx
    lea ebx, [edx + 24]
    mov ecx, edi
    shr ecx, 1
.cp_sub:
    test ecx, ecx
    jz .sub_done
    mov ax, word [esi]
    mov word [ebx], ax
    add esi, 2
    add ebx, 2
    dec ecx
    jmp .cp_sub
.sub_done:
    mov word [ebx], 0
    add ebx, 2
    sub esi, edi
    mov ecx, edi
    shr ecx, 1
.cp_prt:
    test ecx, ecx
    jz .prt_done
    mov ax, word [esi]
    mov word [ebx], ax
    add esi, 2
    add ebx, 2
    dec ecx
    jmp .cp_prt
.prt_done:
    mov word [ebx], 0
    pop edx
    pop ebx

    movzx ecx, word [edx + 4]
    add ecx, 8
    sub esp, 4
    mov esi, esp
    push 0
    push esi
    push 0
    push 0
    push ecx
    push edx
    push FSCTL_SET_REPARSE_POINT
    push ebp
    call dword [__imp__DeviceIoControl@32]
    add esp, 4
    mov esi, eax

    push ebp
    call dword [__imp__CloseHandle@4]
    test esi, esi
    jnz .ok

.rm_jdir_err:
    call dword [__imp__GetLastError@0]
    mov esi, eax
    push ebx
    call dword [__imp__RemoveDirectoryW@4]
    mov eax, esi
    call map_win32_to_err
    jmp .done_err

.close_j_err:
    push ebp
    call dword [__imp__CloseHandle@4]
    mov eax, ERR_OUT_OF_MEMORY
    stc
    jmp .done_err

.not_sup:
    mov eax, ERR_INVALID_FUNCTION
    stc
    jmp .done_err

.win_err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
    jmp .done_err

.ok:
    xor eax, eax
    clc
.done_err:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc
