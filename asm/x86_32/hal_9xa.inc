;; @file hal_9xa.inc
;; @isa 386
;; @desc Windows 95/98/Me ANSI/DBCS (*A) backend + unified dual-mode (*W / *A) Win32 VFS/Registry/Process HAL.

@cpu 386
@format inc

;; @struc WIN32_FIND_DATAA_320
;; @desc 320-byte aligned Win32 ANSI WIN32_FIND_DATAA buffer (318B + 2B pad) for Win95/98/Me FindFirstFileA.
@struc WIN32_FIND_DATAA_320, 320
    @field dwFileAttributes, dword, 1
    @field ftCreationTime, qword, 1
    @field ftLastAccessTime, qword, 1
    @field ftLastWriteTime, qword, 1
    @field nFileSizeHigh, dword, 1
    @field nFileSizeLow, dword, 1
    @field dwReserved0, dword, 1
    @field dwReserved1, dword, 1
    @field cFileName, byte, 260
    @field cAlternateFileName, byte, 14
    @field wPad, word, 1
@endstruc

;; @proc w9x_fill_find_entry
;; @abi win32
;; @in ESI = WIN32_FIND_DATAA_320, EDI = FIND_ENTRY32
;; @out Populates FIND_ENTRY32 from ANSI/DBCS WIN32_FIND_DATAA
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Copies attributes, 64-bit sizes/timestamps, 8.3 short name, and ANSI/DBCS filename into FIND_ENTRY32.
@proc w9x_fill_find_entry
    push esi
    push edi

    mov eax, dword [esi + WIN32_FIND_DATAA_320.dwFileAttributes]
    mov dword [edi + FIND_ENTRY32.dwAttr], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.ftLastWriteTime]
    mov dword [edi + FIND_ENTRY32.ftWriteLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.ftLastWriteTime + 4]
    mov dword [edi + FIND_ENTRY32.ftWriteHi], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.ftCreationTime]
    mov dword [edi + FIND_ENTRY32.ftCreateLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.ftCreationTime + 4]
    mov dword [edi + FIND_ENTRY32.ftCreateHi], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.ftLastAccessTime]
    mov dword [edi + FIND_ENTRY32.ftAccessLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.ftLastAccessTime + 4]
    mov dword [edi + FIND_ENTRY32.ftAccessHi], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.nFileSizeLow]
    mov dword [edi + FIND_ENTRY32.nSizeLo], eax
    mov eax, dword [esi + WIN32_FIND_DATAA_320.nFileSizeHigh]
    mov dword [edi + FIND_ENTRY32.nSizeHi], eax
    mov dword [edi + FIND_ENTRY32.dwReparseTag], 0

    push esi
    push edi
    lea esi, [esi + WIN32_FIND_DATAA_320.cAlternateFileName]
    lea edi, [edi + FIND_ENTRY32.szShort83]
    call str_copy32
    pop edi
    pop esi

    lea esi, [esi + WIN32_FIND_DATAA_320.cFileName]
    lea edi, [edi + FIND_ENTRY32.szNameWtf8]
    call str_copy32

    pop edi
    pop esi
    ret
@endproc

;; @proc hal_open_file32
;; @abi win32
;; @in ESI = WTF-8/ANSI path, ECX = dwDesiredAccess, EDX = dwCreationDisposition, EAX = bInheritable (0/1)
;; @out EAX = Win32 HANDLE, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Opens or creates a file via CreateFileW (NT 3.1..11) or CreateFileA (Win95/98/Me) with optional inheritable SA.
@proc hal_open_file32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 16
    mov ebp, esp

    mov dword [ebp + SECURITY_ATTRIBUTES32.nLength], 12
    mov dword [ebp + SECURITY_ATTRIBUTES32.lpSecurityDescriptor], 0
    mov dword [ebp + SECURITY_ATTRIBUTES32.bInheritHandle], eax
    mov ebx, ecx
    mov edi, edx

    cmp byte [g_is_win9x], 0
    jne .use_ansi

    call ntw_path_to_w32
    jc .ret_err
    push 0
    push FILE_ATTRIBUTE_NORMAL
    push edi
    push ebp
    push FILE_SHARE_READ + FILE_SHARE_WRITE
    push ebx
    push eax
    call dword [__imp__CreateFileW@28]
    jmp .chk_h

.use_ansi:
    push 0
    push FILE_ATTRIBUTE_NORMAL
    push edi
    push ebp
    push FILE_SHARE_READ + FILE_SHARE_WRITE
    push ebx
    push esi
    call dword [__imp__CreateFileA@28]

.chk_h:
    cmp eax, INVALID_HANDLE_VALUE
    je .win_err
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.win_err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.ret_err:
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc hal_get_attr32
;; @abi win32
;; @in ESI = path (WTF-8 / ANSI)
;; @out EAX = dwFileAttributes, CF=0; or CF=1, EAX=ERR_* if missing
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Queries file/directory attributes via GetFileAttributesW (NT) or GetFileAttributesA (Win9x).
@proc hal_get_attr32
    cmp byte [g_is_win9x], 0
    jne .ansi
    call ntw_path_to_w32
    jc .fail
    push eax
    call dword [__imp__GetFileAttributesW@4]
    jmp .chk
.ansi:
    push esi
    call dword [__imp__GetFileAttributesA@4]
.chk:
    cmp eax, INVALID_FILE_ATTRIBUTES
    je .err
    clc
    ret
.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    stc
    ret
@endproc

;; @proc hal_set_attr32
;; @abi win32
;; @in ESI = path, ECX = dwFileAttributes
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Sets file attributes via SetFileAttributesW (NT) or SetFileAttributesA (Win9x).
@proc hal_set_attr32
    push ebx
    mov ebx, ecx
    cmp byte [g_is_win9x], 0
    jne .ansi
    call ntw_path_to_w32
    jc .fail
    push ebx
    push eax
    call dword [__imp__SetFileAttributesW@8]
    jmp .chk
.ansi:
    push ebx
    push esi
    call dword [__imp__SetFileAttributesA@8]
.chk:
    test eax, eax
    jz .err
    pop ebx
    clc
    ret
.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    pop ebx
    stc
    ret
@endproc

;; @proc hal_get_cwd32
;; @abi win32
;; @in EDI = destination WTF-8 buffer, ECX = buffer byte capacity
;; @out EDI = NUL-terminated current working directory, EAX = byte length, CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Queries current directory via GetCurrentDirectoryW (NT, up to 32K WCHARs) or GetCurrentDirectoryA (Win9x).
@proc hal_get_cwd32
    push ebx
    push esi
    push edi
    mov ebx, ecx

    cmp byte [g_is_win9x], 0
    jne .ansi

    mov ecx, 65544
    call arena_alloc32
    jc .fail
    mov esi, eax
    push esi
    push 32768
    call dword [__imp__GetCurrentDirectoryW@8]
    test eax, eax
    jz .err
    mov ecx, eax
    mov edx, ebx
    call utf16le_to_wtf8_buf
    pop edi
    pop esi
    pop ebx
    clc
    ret

.ansi:
    push edi
    push ebx
    call dword [__imp__GetCurrentDirectoryA@8]
    test eax, eax
    jz .err
    pop edi
    pop esi
    pop ebx
    clc
    ret

.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc hal_set_cwd32
;; @abi win32
;; @in ESI = target directory path
;; @out CF=0 on success; CF=1, EAX=ERR_* on error
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Changes process current directory via SetCurrentDirectoryW (NT) or SetCurrentDirectoryA (Win9x).
@proc hal_set_cwd32
    cmp byte [g_is_win9x], 0
    jne .ansi
    call ntw_path_to_w32
    jc .fail
    push eax
    call dword [__imp__SetCurrentDirectoryW@4]
    jmp .chk
.ansi:
    push esi
    call dword [__imp__SetCurrentDirectoryA@4]
.chk:
    test eax, eax
    jz .err
    clc
    ret
.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    stc
    ret
@endproc

;; @proc hal_fullpath32
;; @abi win32
;; @in ESI = input path
;; @out EAX = canonicalized full path in statement arena, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Resolves full path via GetFullPathNameW (NT) or GetFullPathNameA (Win9x).
@proc hal_fullpath32
    push ebx
    push esi
    push edi

    cmp byte [g_is_win9x], 0
    jne .ansi

    call wtf8_to_utf16le
    jc .fail
    mov ebx, eax
    mov ecx, 65544
    call arena_alloc32
    jc .fail
    mov edi, eax
    push 0
    push edi
    push 32768
    push ebx
    call dword [__imp__GetFullPathNameW@16]
    test eax, eax
    jz .err
    mov esi, edi
    call utf16le_to_wtf8_arena
    jc .fail
    pop edi
    pop esi
    pop ebx
    clc
    ret

.ansi:
    mov ecx, 1024
    call arena_alloc32
    jc .fail
    mov edi, eax
    push 0
    push edi
    push 1020
    push esi
    call dword [__imp__GetFullPathNameA@16]
    test eax, eax
    jz .err
    mov eax, edi
    pop edi
    pop esi
    pop ebx
    clc
    ret

.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc hal_shortpath32
;; @abi win32
;; @in ESI = input path
;; @out EAX = 8.3 short path in statement arena (or full path fallback on NT 3.1 RTM!), CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Queries 8.3 alias via dynamic GetShortPathNameW/A (NT 3.5+/Win95+), falling back cleanly on NT 3.1.
@proc hal_shortpath32
    push ebx
    push esi
    push edi

    cmp byte [g_is_win9x], 0
    jne .ansi

    cmp dword [pfnGetShortPathNameW_p12], 0
    je .fallback
    call ntw_path_to_w32
    jc .fail
    mov ebx, eax
    mov ecx, 4096
    call arena_alloc32
    jc .fail
    mov edi, eax
    push 2040
    push edi
    push ebx
    call dword [pfnGetShortPathNameW_p12]
    test eax, eax
    jz .fallback
    mov esi, edi
    call utf16le_to_wtf8_arena
    jc .fail
    pop edi
    pop esi
    pop ebx
    clc
    ret

.ansi:
    cmp dword [pfnGetShortPathNameA_p12], 0
    je .fallback
    mov ecx, 520
    call arena_alloc32
    jc .fail
    mov edi, eax
    push 512
    push edi
    push esi
    call dword [pfnGetShortPathNameA_p12]
    test eax, eax
    jz .fallback
    mov eax, edi
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fallback:
    call hal_fullpath32
    jc .fail
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

;; @proc hal_mkdir_one32
;; @abi win32
;; @in ESI = single directory path
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Creates one directory via CreateDirectoryW (NT) or CreateDirectoryA (Win9x).
@proc hal_mkdir_one32
    cmp byte [g_is_win9x], 0
    jne .ansi
    call ntw_path_to_w32
    jc .fail
    push 0
    push eax
    call dword [__imp__CreateDirectoryW@8]
    jmp .chk
.ansi:
    push 0
    push esi
    call dword [__imp__CreateDirectoryA@8]
.chk:
    test eax, eax
    jz .err
    clc
    ret
.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    stc
    ret
@endproc

;; @proc hal_rmdir_one32
;; @abi win32
;; @in ESI = directory or directory-junction/symlink path
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Removes an empty directory or unlinks an NTFS <JUNCTION>/<SYMLINKD> without touching target files.
@proc hal_rmdir_one32
    cmp byte [g_is_win9x], 0
    jne .ansi
    call ntw_path_to_w32
    jc .fail
    push eax
    call dword [__imp__RemoveDirectoryW@4]
    jmp .chk
.ansi:
    push esi
    call dword [__imp__RemoveDirectoryA@4]
.chk:
    test eax, eax
    jz .err
    clc
    ret
.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    stc
    ret
@endproc

;; @proc hal_delete_file32
;; @abi win32
;; @in ESI = file path, ECX = bForceReadOnly (1 -> strip FILE_ATTRIBUTE_READONLY first)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Deletes a file via DeleteFileW (NT) or DeleteFileA (Win9x), stripping R/O attribute when /F is set.
@proc hal_delete_file32
    test ecx, ecx
    jz .do_del
    call hal_get_attr32
    jc .do_del
    test eax, FILE_ATTRIBUTE_READONLY
    jz .do_del
    and eax, -2
    mov ecx, eax
    call hal_set_attr32
.do_del:
    cmp byte [g_is_win9x], 0
    jne .ansi
    call ntw_path_to_w32
    jc .fail
    push eax
    call dword [__imp__DeleteFileW@4]
    jmp .chk
.ansi:
    push esi
    call dword [__imp__DeleteFileA@4]
.chk:
    test eax, eax
    jz .err
    clc
    ret
.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    stc
    ret
@endproc

;; @proc hal_move_file32
;; @abi win32
;; @in ESI = src path, EDI = dst path, ECX = bOverwrite (1 -> replace existing)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Moves/renames via MoveFileExW (NT) or MoveFileA + CopyFileA/DeleteFileA fallback (Win9x / NT 3.1).
@proc hal_move_file32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx

    cmp byte [g_is_win9x], 0
    jne .ansi

    call ntw_path_to_w32
    jc .fail
    mov ebx, eax
    mov esi, edi
    call ntw_path_to_w32
    jc .fail
    mov edi, eax

    cmp dword [pfnMoveFileExW_p12], 0
    je .nt31_move
    mov edx, MOVEFILE_COPY_ALLOWED
    test ebp, ebp
    jz .call_mvex
    or edx, MOVEFILE_REPLACE_EXISTING
.call_mvex:
    push edx
    push edi
    push ebx
    call dword [pfnMoveFileExW_p12]
    test eax, eax
    jnz .ok
    jmp .err

.nt31_move:
    test ebp, ebp
    jz .nt31_try
    push edi
    call dword [__imp__DeleteFileW@4]
.nt31_try:
    push edi
    push ebx
    call dword [__imp__MoveFileW@8]
    test eax, eax
    jnz .ok
    jmp .err

.ansi:
    test ebp, ebp
    jz .ansi_mv
    push edi
    call dword [__imp__DeleteFileA@4]
.ansi_mv:
    push edi
    push esi
    call dword [__imp__MoveFileA@8]
    test eax, eax
    jnz .ok
    ; Cross-drive Win9x fallback: CopyFileA + DeleteFileA
    xor eax, eax
    test ebp, ebp
    setz al
    push eax
    push edi
    push esi
    call dword [__imp__CopyFileA@12]
    test eax, eax
    jz .err
    push esi
    call dword [__imp__DeleteFileA@4]
    jmp .ok

.ok:
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret
.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc hal_find_first32
;; @abi win32
;; @in ESI = search pattern (e.g. "C:\dir\*"), EDI = FIND_ENTRY32
;; @out EAX = search HANDLE, CF=0; or CF=1, EAX=ERR_* when no files match
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Starts directory enumeration via FindFirstFileExW (Win7+ FindExInfoBasic) / FindFirstFileW / FindFirstFileA.
@proc hal_find_first32
    push ebx
    push esi
    push edi

    cmp byte [g_is_win9x], 0
    jne .ansi

    call ntw_path_to_w32
    jc .fail
    mov ebx, eax
    mov ecx, 608
    call arena_alloc32
    jc .fail
    mov esi, eax

    ; Zero cAlternateFileName in case Win7+ FindExInfoBasic (level 1) skips populating 8.3 name
    mov dword [esi + WIN32_FIND_DATAW.cAlternateFileName], 0

    cmp dword [pfnFindFirstFileExW_p24], 0
    je .nt_std
    cmp dword [g_os_major], 6
    jb .nt_std
    cmp byte [g_dir_need_short], 0
    jne .nt_std

    ; FindExInfoBasic = 1, FindExSearchNameMatch = 0
    push 0
    push 0
    push 0
    push esi
    push 1
    push ebx
    call dword [pfnFindFirstFileExW_p24]
    cmp eax, INVALID_HANDLE_VALUE
    jne .got_w

.nt_std:
    push esi
    push ebx
    call dword [__imp__FindFirstFileW@8]
    cmp eax, INVALID_HANDLE_VALUE
    je .err
.got_w:
    mov ebx, eax
    call ntw_fill_find_entry
    mov eax, ebx
    pop edi
    pop esi
    pop ebx
    clc
    ret

.ansi:
    mov ecx, 328
    call arena_alloc32
    jc .fail
    mov ebx, eax
    push ebx
    push esi
    call dword [__imp__FindFirstFileA@8]
    cmp eax, INVALID_HANDLE_VALUE
    je .err
    mov esi, ebx
    mov ebx, eax
    call w9x_fill_find_entry
    mov eax, ebx
    pop edi
    pop esi
    pop ebx
    clc
    ret

.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc hal_find_next32
;; @abi win32
;; @in EBX = search HANDLE, EDI = FIND_ENTRY32
;; @out CF=0 on next entry; CF=1 when enumeration ends
;; @err CF=1 on ERROR_NO_MORE_FILES
;; @clobbers eax, ecx, edx
;; @desc Advances directory enumeration via FindNextFileW (NT) or FindNextFileA (Win9x).
@proc hal_find_next32
    push esi
    call arena_mark32
    push eax

    cmp byte [g_is_win9x], 0
    jne .ansi

    mov ecx, 608
    call arena_alloc32
    jc .no_more
    mov esi, eax
    mov dword [esi + WIN32_FIND_DATAW.cAlternateFileName], 0
    push esi
    push ebx
    call dword [__imp__FindNextFileW@8]
    test eax, eax
    jz .no_more
    call ntw_fill_find_entry
    pop eax
    call arena_release32
    pop esi
    clc
    ret

.ansi:
    mov ecx, 328
    call arena_alloc32
    jc .no_more
    mov esi, eax
    push esi
    push ebx
    call dword [__imp__FindNextFileA@8]
    test eax, eax
    jz .no_more
    call w9x_fill_find_entry
    pop eax
    call arena_release32
    pop esi
    clc
    ret

.no_more:
    pop eax
    call arena_release32
    pop esi
    stc
    ret
@endproc

;; @proc hal_disk_free64
;; @abi win32
;; @in ESI = root or directory path (e.g. "C:")
;; @out EDX:EAX = 64-bit free bytes available to caller, CF=0; or CF=1 on error
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Queries 64-bit free space via GetDiskFreeSpaceExW/A (NT 4.0+/OSR2+) with GetDiskFreeSpaceW/A fallback (NT 3.1/Win95 RTM).
@proc hal_disk_free64
    push ebx
    push esi
    push edi
    sub esp, 32
    mov edi, esp

    cmp byte [g_is_win9x], 0
    jne .ansi

    call ntw_path_to_w32
    jc .fail
    mov ebx, eax
    cmp dword [pfnGetDiskFreeSpaceExW_p16], 0
    je .nt31_free
    lea eax, [edi + 16]
    lea ecx, [edi + 8]
    push eax
    push ecx
    push edi
    push ebx
    call dword [pfnGetDiskFreeSpaceExW_p16]
    test eax, eax
    jz .nt31_free
    mov eax, dword [edi]
    mov edx, dword [edi + 4]
    jmp .ok

.nt31_free:
    lea eax, [edi + 12]
    lea ecx, [edi + 8]
    lea edx, [edi + 4]
    push eax
    push ecx
    push edx
    push edi
    push ebx
    call dword [__imp__GetDiskFreeSpaceW@20]
    test eax, eax
    jz .err
    jmp .calc_clusters

.ansi:
    cmp dword [pfnGetDiskFreeSpaceExA_p16], 0
    je .w95_rtm_free
    lea eax, [edi + 16]
    lea ecx, [edi + 8]
    push eax
    push ecx
    push edi
    push esi
    call dword [pfnGetDiskFreeSpaceExA_p16]
    test eax, eax
    jz .w95_rtm_free
    mov eax, dword [edi]
    mov edx, dword [edi + 4]
    jmp .ok

.w95_rtm_free:
    lea eax, [edi + 12]
    lea ecx, [edi + 8]
    lea edx, [edi + 4]
    push eax
    push ecx
    push edx
    push edi
    push esi
    call dword [__imp__GetDiskFreeSpaceA@20]
    test eax, eax
    jz .err

.calc_clusters:
    ; [edi]=SectorsPerCluster, [edi+4]=BytesPerSector, [edi+8]=NumberOfFreeClusters
    mov eax, dword [edi]
    xor edx, edx
    mov ecx, dword [edi + 4]
    call u64_mul_u32
    mov ecx, dword [edi + 8]
    call u64_mul_u32

.ok:
    add esp, 32
    pop edi
    pop esi
    pop ebx
    clc
    ret

.err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    add esp, 32
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc hal_ensure_advapi32
;; @abi win32
;; @in none
;; @out CF=0 if ADVAPI32 Registry exports resolved; CF=1 if unavailable
;; @err CF=1 when ADVAPI32 cannot be loaded
;; @clobbers eax, ecx, edx
;; @desc Lazily loads ADVAPI32.DLL and resolves RegOpenKeyEx, RegQueryValueEx, RegCreateKeyEx, RegSetValueEx, RegCloseKey.
@proc hal_ensure_advapi32
    cmp byte [g_advapi_loaded], 0
    jne .already
    push ebx
    push esi
    push addr(szDllAdvapi32)
    call dword [__imp__LoadLibraryA@4]
    mov ebx, eax
    test ebx, ebx
    jz .fail_pop
    mov dword [g_hmod_advapi32], ebx

    mov esi, addr(szFnRegOpenKeyExW)
    call dyn_resolve_proc
    mov dword [pfnRegOpenKeyExW_p20], eax
    mov esi, addr(szFnRegOpenKeyExA)
    call dyn_resolve_proc
    mov dword [pfnRegOpenKeyExA_p20], eax

    mov esi, addr(szFnRegQueryValExW)
    call dyn_resolve_proc
    mov dword [pfnRegQueryValueExW_p24], eax
    mov esi, addr(szFnRegQueryValExA)
    call dyn_resolve_proc
    mov dword [pfnRegQueryValueExA_p24], eax

    mov esi, addr(szFnRegCreateKeyExW)
    call dyn_resolve_proc
    mov dword [pfnRegCreateKeyExW_p36], eax
    mov esi, addr(szFnRegCreateKeyExA)
    call dyn_resolve_proc
    mov dword [pfnRegCreateKeyExA_p36], eax

    mov esi, addr(szFnRegSetValExW)
    call dyn_resolve_proc
    mov dword [pfnRegSetValueExW_p24], eax
    mov esi, addr(szFnRegSetValExA)
    call dyn_resolve_proc
    mov dword [pfnRegSetValueExA_p24], eax

    mov esi, addr(szFnRegDeleteValW)
    call dyn_resolve_proc
    mov dword [pfnRegDeleteValueW_p8], eax
    mov esi, addr(szFnRegDeleteValA)
    call dyn_resolve_proc
    mov dword [pfnRegDeleteValueA_p8], eax

    mov esi, addr(szFnRegCloseKey)
    call dyn_resolve_proc
    mov dword [pfnRegCloseKey_p4], eax

    mov byte [g_advapi_loaded], 1
    pop esi
    pop ebx
.already:
    cmp dword [pfnRegCloseKey_p4], 0
    je .fail
    clc
    ret
.fail_pop:
    pop esi
    pop ebx
.fail:
    stc
    ret
@endproc

;; @proc hal_reg_query_str32
;; @abi win32
;; @in EBX = HKEY root, ESI = subkey (WTF-8/ANSI), EDI = value name (NULL/"" for default), ECX = out WTF-8 buf, EDX = out cap
;; @out CF=0, EAX = out buf on success; CF=1 on missing key/value
;; @err CF=1 if key or string value does not exist
;; @clobbers eax, ecx, edx
;; @desc Reads a REG_SZ or REG_EXPAND_SZ Registry value via ADVAPI32 (*W on NT, *A on Win9x).
@proc hal_reg_query_str32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 20
    mov ebp, esp
    mov dword [ebp + 8], ecx
    mov dword [ebp + 12], edx

    call hal_ensure_advapi32
    jc .fail

    cmp byte [g_is_win9x], 0
    jne .ansi

    call wtf8_to_utf16le
    jc .fail
    mov ecx, eax
    xor edx, edx
    test edi, edi
    jz .open_w
    push ecx
    mov esi, edi
    call wtf8_to_utf16le
    pop ecx
    jc .fail
    mov edx, eax
.open_w:
    mov dword [ebp + 4], edx
    push ebp
    push KEY_READ_ACCESS
    push 0
    push ecx
    push ebx
    call dword [pfnRegOpenKeyExW_p20]
    test eax, eax
    jnz .fail

    mov ecx, 4096
    call arena_alloc32
    jc .close_fail
    mov esi, eax
    mov dword [ebp + 16], 4094
    lea eax, [ebp + 16]
    push eax
    push esi
    push 0
    push 0
    push dword [ebp + 4]
    push dword [ebp]
    call dword [pfnRegQueryValueExW_p24]
    mov ebx, eax
    push dword [ebp]
    call dword [pfnRegCloseKey_p4]
    test ebx, ebx
    jnz .fail

    mov edi, dword [ebp + 8]
    mov edx, dword [ebp + 12]
    mov ecx, -1
    call utf16le_to_wtf8_buf
    mov eax, dword [ebp + 8]
    add esp, 20
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.ansi:
    push ebp
    push KEY_READ_ACCESS
    push 0
    push esi
    push ebx
    call dword [pfnRegOpenKeyExA_p20]
    test eax, eax
    jnz .fail

    mov eax, dword [ebp + 12]
    mov dword [ebp + 4], eax
    lea eax, [ebp + 4]
    push eax
    push dword [ebp + 8]
    push 0
    push 0
    push edi
    push dword [ebp]
    call dword [pfnRegQueryValueExA_p24]
    mov esi, eax
    push dword [ebp]
    call dword [pfnRegCloseKey_p4]
    test esi, esi
    jnz .fail
    mov eax, dword [ebp + 8]
    add esp, 20
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.close_fail:
    push dword [ebp]
    call dword [pfnRegCloseKey_p4]
.fail:
    add esp, 20
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc hal_reg_set_str32
;; @abi win32
;; @in EBX = HKEY root, ESI = subkey, EDI = value name (0 for default), ECX = WTF-8/ANSI string value
;; @out CF=0 on success; CF=1, EAX=ERR_ACCESS_DENIED on failure
;; @err CF=1, EAX=ERR_ACCESS_DENIED
;; @clobbers eax, ecx, edx
;; @desc Creates/opens a Registry key and writes a REG_SZ value via ADVAPI32 (*W on NT, *A on Win9x).
@proc hal_reg_set_str32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 16
    mov ebp, esp
    mov dword [ebp + 8], ecx

    call hal_ensure_advapi32
    jc .fail

    cmp byte [g_is_win9x], 0
    jne .ansi

    call wtf8_to_utf16le
    jc .fail
    mov ecx, eax
    xor edx, edx
    test edi, edi
    jz .cr_w
    push ecx
    mov esi, edi
    call wtf8_to_utf16le
    pop ecx
    jc .fail
    mov edx, eax
.cr_w:
    mov dword [ebp + 4], edx
    lea eax, [ebp + 12]
    push eax
    push ebp
    push 0
    push KEY_WRITE_ACCESS
    push 0
    push 0
    push 0
    push ecx
    push ebx
    call dword [pfnRegCreateKeyExW_p36]
    test eax, eax
    jnz .fail

    mov esi, dword [ebp + 8]
    call wtf8_to_utf16le
    jc .close_err
    lea ecx, [ecx + ecx + 2]
    push ecx
    push eax
    push REG_SZ_TYPE
    push 0
    push dword [ebp + 4]
    push dword [ebp]
    call dword [pfnRegSetValueExW_p24]
    mov ebx, eax
    push dword [ebp]
    call dword [pfnRegCloseKey_p4]
    test ebx, ebx
    jnz .fail
    jmp .ok

.ansi:
    lea eax, [ebp + 12]
    push eax
    push ebp
    push 0
    push KEY_WRITE_ACCESS
    push 0
    push 0
    push 0
    push esi
    push ebx
    call dword [pfnRegCreateKeyExA_p36]
    test eax, eax
    jnz .fail

    mov esi, dword [ebp + 8]
    call str_len32
    inc ecx
    push ecx
    push esi
    push REG_SZ_TYPE
    push 0
    push edi
    push dword [ebp]
    call dword [pfnRegSetValueExA_p24]
    mov ebx, eax
    push dword [ebp]
    call dword [pfnRegCloseKey_p4]
    test ebx, ebx
    jnz .fail

.ok:
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.close_err:
    push dword [ebp]
    call dword [pfnRegCloseKey_p4]
.fail:
    mov eax, ERR_ACCESS_DENIED
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc
