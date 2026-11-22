;; @file hal_nt64.inc
;; @isa x64
;; @desc Native x86-64 (*W) Unicode VFS, 32K Long Paths, Reparse/MKLINK (/D /H /J), and ADVAPI32 Registry HAL.

@cpu x64
@format inc

;; @proc nt64_path_to_w
;; @abi win64_ms
;; @in RCX = WTF-8 path string
;; @out RAX = UTF-16LE path in statement arena (auto-promoted to \?\ or \?\UNC\ when >= 240 WCHARs), CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Transcodes WTF-8 path to UTF-16LE and promotes >=240-WCHAR paths to \?\ up to 32,767 WCHARs.
@proc_frame nt64_path_to_w, 32, rbx, rsi, rdi
    call wtf8_to_utf16le64
    jc .fail
    mov rbx, rax
    cmp rdx, 240
    jb .ok_direct

    cmp word [rbx], 0x5C
    jne .chk_drive
    cmp word [rbx + 2], 0x5C
    jne .ok_direct
    mov ax, word [rbx + 4]
    cmp ax, '?'
    je .ok_direct
    cmp ax, '.'
    je .ok_direct

    ; Promote \server\share\... -> \?\UNC\server\share\...
    lea rcx, [rdx + rdx + 24]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r8, rax
    mov word [rdi], 0x5C
    mov word [rdi + 2], 0x5C
    mov word [rdi + 4], '?'
    mov word [rdi + 6], 0x5C
    mov word [rdi + 8], 'U'
    mov word [rdi + 10], 'N'
    mov word [rdi + 12], 'C'
    mov word [rdi + 14], 0x5C
    add rdi, 16
    lea rsi, [rbx + 4]
.cp_unc:
    mov ax, word [rsi]
    mov word [rdi], ax
    add rsi, 2
    add rdi, 2
    test ax, ax
    jnz .cp_unc
    mov rax, r8
    clc
    @epilog_ret

.chk_drive:
    cmp word [rbx + 2], ':'
    jne .ok_direct
    mov ax, word [rbx + 4]
    cmp ax, 0x5C
    je .do_drv_long
    cmp ax, '/'
    jne .ok_direct
.do_drv_long:
    lea rcx, [rdx + rdx + 16]
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov r8, rax
    mov word [rdi], 0x5C
    mov word [rdi + 2], 0x5C
    mov word [rdi + 4], '?'
    mov word [rdi + 6], 0x5C
    add rdi, 8
    mov rsi, rbx
.cp_drv:
    mov ax, word [rsi]
    cmp ax, '/'
    jne .st_drv
    mov ax, 0x5C
.st_drv:
    mov word [rdi], ax
    add rsi, 2
    add rdi, 2
    test ax, ax
    jnz .cp_drv
    mov rax, r8
    clc
    @epilog_ret

.ok_direct:
    mov rax, rbx
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc nt64_query_reparse_tag
;; @abi win64_ms
;; @in RCX = UTF-16LE path
;; @out EAX = ReparseTag (0xA0000003 Junction, 0xA000000C Symlink, 0x8000001B AppExecLink, or 0)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries NTFS/ReFS ReparseTag via DeviceIoControl(FSCTL_GET_REPARSE_POINT).
@proc_frame nt64_query_reparse_tag, 152, rbx, rdi
    xor edx, edx
    mov r8d, FILE_SHARE_READ + FILE_SHARE_WRITE
    xor r9d, r9d
    mov dword [rsp + 32], OPEN_EXISTING
    mov dword [rsp + 40], FILE_FLAG_BACKUP_SEMANTICS + FILE_FLAG_OPEN_REPARSE_POINT
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    cmp rax, INVALID_HANDLE_VALUE
    je .no_tag
    mov rbx, rax

    lea rdi, [rsp + 64]
    mov dword [rsp + 132], 0
    mov rcx, rbx
    mov edx, FSCTL_GET_REPARSE_POINT
    xor r8d, r8d
    xor r9d, r9d
    mov qword [rsp + 32], rdi
    mov dword [rsp + 40], 64
    lea rax, [rsp + 132]
    mov qword [rsp + 48], rax
    mov qword [rsp + 56], 0
    call qword [rel __imp_DeviceIoControl]
    mov edi, dword [rsp + 64]
    mov dword [rsp + 136], eax

    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    cmp dword [rsp + 136], 0
    je .no_tag
    mov eax, edi
    @epilog_ret
.no_tag:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc nt64_fill_find_entry
;; @abi win64_ms
;; @in RCX = WIN32_FIND_DATAW (592B), RDX = FIND_ENTRY64 (560B)
;; @out Populates FIND_ENTRY64 fields and WTF-8 long/8.3 names
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Converts a 592-byte WIN32_FIND_DATAW into a normalized WTF-8 FIND_ENTRY64 structure.
@proc_frame nt64_fill_find_entry, 40, rsi, rdi
    mov rsi, rcx
    mov rdi, rdx

    mov eax, dword [rsi + WIN32_FIND_DATAW.dwFileAttributes]
    mov dword [rdi + FIND_ENTRY64.dwAttr], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.ftLastWriteTime]
    mov dword [rdi + FIND_ENTRY64.ftWriteLo], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.ftLastWriteTime + 4]
    mov dword [rdi + FIND_ENTRY64.ftWriteHi], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.ftCreationTime]
    mov dword [rdi + FIND_ENTRY64.ftCreateLo], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.ftCreationTime + 4]
    mov dword [rdi + FIND_ENTRY64.ftCreateHi], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.ftLastAccessTime]
    mov dword [rdi + FIND_ENTRY64.ftAccessLo], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.ftLastAccessTime + 4]
    mov dword [rdi + FIND_ENTRY64.ftAccessHi], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.nFileSizeLow]
    mov dword [rdi + FIND_ENTRY64.nSizeLo], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.nFileSizeHigh]
    mov dword [rdi + FIND_ENTRY64.nSizeHi], eax
    mov eax, dword [rsi + WIN32_FIND_DATAW.dwReserved0]
    mov dword [rdi + FIND_ENTRY64.dwReparseTag], eax

    lea rcx, [rsi + WIN32_FIND_DATAW.cAlternateFileName]
    mov rdx, -1
    lea r8, [rdi + FIND_ENTRY64.szShort83]
    mov r9d, 32
    call utf16le_to_wtf8_buf64

    lea rcx, [rsi + WIN32_FIND_DATAW.cFileName]
    mov rdx, -1
    lea r8, [rdi + FIND_ENTRY64.szNameWtf8]
    mov r9d, 488
    call utf16le_to_wtf8_buf64
    @epilog_ret
@endproc

;; @proc hal_open_file64
;; @abi win64_ms
;; @in RCX = WTF-8 path, EDX = dwDesiredAccess, R8D = dwCreationDisposition, R9D = bInheritable (0/1)
;; @out RAX = Win64 HANDLE, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Opens or creates a file via CreateFileW with optional inheritable 24-byte SECURITY_ATTRIBUTES64.
@proc_frame hal_open_file64, 96, rbx, rsi, rdi
    mov ebx, edx
    mov esi, r8d
    mov edi, r9d

    call nt64_path_to_w
    jc .fail

    lea r9, [rsp + 64]
    mov dword [r9 + SECURITY_ATTRIBUTES64.nLength], 24
    mov dword [r9 + SECURITY_ATTRIBUTES64.dwPad0], 0
    mov qword [r9 + SECURITY_ATTRIBUTES64.lpSecurityDescriptor], 0
    mov dword [r9 + SECURITY_ATTRIBUTES64.bInheritHandle], edi
    mov dword [r9 + SECURITY_ATTRIBUTES64.dwPad1], 0

    mov rcx, rax
    mov edx, ebx
    mov r8d, FILE_SHARE_READ + FILE_SHARE_WRITE
    mov dword [rsp + 32], esi
    mov dword [rsp + 40], FILE_ATTRIBUTE_NORMAL
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    cmp rax, INVALID_HANDLE_VALUE
    je .win_err
    clc
    @epilog_ret

.win_err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_get_attr64
;; @abi win64_ms
;; @in RCX = WTF-8 path
;; @out EAX = dwFileAttributes, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries file/directory attributes via GetFileAttributesW.
@proc_frame hal_get_attr64, 40
    call nt64_path_to_w
    jc .fail
    mov rcx, rax
    call qword [rel __imp_GetFileAttributesW]
    cmp eax, INVALID_FILE_ATTRIBUTES
    je .err
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_set_attr64
;; @abi win64_ms
;; @in RCX = WTF-8 path, EDX = dwFileAttributes
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Sets file attributes via SetFileAttributesW.
@proc_frame hal_set_attr64, 32, rbx
    mov ebx, edx
    call nt64_path_to_w
    jc .fail
    mov rcx, rax
    mov edx, ebx
    call qword [rel __imp_SetFileAttributesW]
    test eax, eax
    jz .err
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_get_cwd64
;; @abi win64_ms
;; @in RCX = destination WTF-8 buffer, RDX = buffer byte capacity
;; @out Writes NUL-terminated CWD to RCX, RAX = byte length, CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries current directory via GetCurrentDirectoryW (supporting up to 32,767 WCHARs with longPathAware).
@proc_frame hal_get_cwd64, 32, rbx, rsi, rdi
    mov rdi, rcx
    mov rbx, rdx
    mov ecx, 65544
    call arena_alloc64
    jc .fail
    mov rsi, rax
    mov ecx, 32768
    mov rdx, rsi
    call qword [rel __imp_GetCurrentDirectoryW]
    test eax, eax
    jz .err
    mov rcx, rsi
    mov edx, eax
    mov r8, rdi
    mov r9, rbx
    call utf16le_to_wtf8_buf64
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_set_cwd64
;; @abi win64_ms
;; @in RCX = target WTF-8 directory path
;; @out CF=0 on success; CF=1, EAX=ERR_* on error
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Changes process current directory via SetCurrentDirectoryW.
@proc_frame hal_set_cwd64, 40
    call nt64_path_to_w
    jc .fail
    mov rcx, rax
    call qword [rel __imp_SetCurrentDirectoryW]
    test eax, eax
    jz .err
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_fullpath64
;; @abi win64_ms
;; @in RCX = input WTF-8 path
;; @out RAX = canonicalized full path in statement arena, CF=0; or CF=1, EAX=ERR_*
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Resolves full path via GetFullPathNameW (up to 32,768 WCHARs).
@proc_frame hal_fullpath64, 40, rbx, rdi
    call wtf8_to_utf16le64
    jc .fail
    mov rbx, rax
    mov ecx, 65544
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov rcx, rbx
    mov edx, 32768
    mov r8, rdi
    xor r9d, r9d
    call qword [rel __imp_GetFullPathNameW]
    test eax, eax
    jz .err
    mov rcx, rdi
    call utf16le_to_wtf8_arena64
    jc .fail
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_shortpath64
;; @abi win64_ms
;; @in RCX = input WTF-8 path
;; @out RAX = 8.3 short path in statement arena (or full path fallback if 8.3 disabled on volume), CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries 8.3 alias via GetShortPathNameW, falling back cleanly when NTFS 8.3 creation is disabled.
@proc_frame hal_shortpath64, 32, rbx, rsi, rdi
    mov rsi, rcx
    call nt64_path_to_w
    jc .fail
    mov rbx, rax
    mov ecx, 4096
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, 2040
    call qword [rel __imp_GetShortPathNameW]
    test eax, eax
    jz .fallback
    mov rcx, rdi
    call utf16le_to_wtf8_arena64
    jc .fail
    clc
    @epilog_ret
.fallback:
    mov rcx, rsi
    call hal_fullpath64
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_mkdir_one64
;; @abi win64_ms
;; @in RCX = directory path (WTF-8)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Creates one directory via CreateDirectoryW.
@proc_frame hal_mkdir_one64, 40
    call nt64_path_to_w
    jc .fail
    mov rcx, rax
    xor edx, edx
    call qword [rel __imp_CreateDirectoryW]
    test eax, eax
    jz .err
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_rmdir_one64
;; @abi win64_ms
;; @in RCX = directory or directory-junction/symlink path
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Removes an empty directory or unlinks an NTFS <JUNCTION>/<SYMLINKD> without deleting target files.
@proc_frame hal_rmdir_one64, 40
    call nt64_path_to_w
    jc .fail
    mov rcx, rax
    call qword [rel __imp_RemoveDirectoryW]
    test eax, eax
    jz .err
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_delete_file64
;; @abi win64_ms
;; @in RCX = file path (WTF-8), EDX = bForceReadOnly (1 -> strip FILE_ATTRIBUTE_READONLY first)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Deletes a file via DeleteFileW, stripping R/O attribute when /F is set.
@proc_frame hal_delete_file64, 32, rsi
    mov rsi, rcx
    test edx, edx
    jz .do_del
    call hal_get_attr64
    jc .do_del
    test eax, FILE_ATTRIBUTE_READONLY
    jz .do_del
    and eax, -2
    mov rcx, rsi
    mov edx, eax
    call hal_set_attr64
.do_del:
    mov rcx, rsi
    call nt64_path_to_w
    jc .fail
    mov rcx, rax
    call qword [rel __imp_DeleteFileW]
    test eax, eax
    jz .err
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_move_file64
;; @abi win64_ms
;; @in RCX = src path, RDX = dst path, R8D = bOverwrite (1 -> replace existing)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Moves/renames via MoveFileExW(MOVEFILE_COPY_ALLOWED | optional MOVEFILE_REPLACE_EXISTING).
@proc_frame hal_move_file64, 32, rbx, rsi, rdi
    mov rsi, rdx
    mov edi, r8d
    call nt64_path_to_w
    jc .fail
    mov rbx, rax
    mov rcx, rsi
    call nt64_path_to_w
    jc .fail
    mov rdx, rax
    mov rcx, rbx
    mov r8d, MOVEFILE_COPY_ALLOWED
    test edi, edi
    jz .do_mv
    or r8d, MOVEFILE_REPLACE_EXISTING
.do_mv:
    call qword [rel __imp_MoveFileExW]
    test eax, eax
    jz .err
    clc
    @epilog_ret
.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_find_first64
;; @abi win64_ms
;; @in RCX = search pattern (e.g. "C:\dir\*"), RDX = FIND_ENTRY64
;; @out RAX = search HANDLE, CF=0; or CF=1, EAX=ERR_* when no files match
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Starts directory enumeration via FindFirstFileExW (Win7+ FindExInfoBasic) or FindFirstFileW.
@proc_frame hal_find_first64, 64, rbx, rsi, rdi
    mov rdi, rdx
    call nt64_path_to_w
    jc .fail
    mov rbx, rax
    mov ecx, 608
    call arena_alloc64
    jc .fail
    mov rsi, rax
    mov dword [rsi + WIN32_FIND_DATAW.cAlternateFileName], 0

    cmp qword [rel pfnFindFirstFileExW64], 0
    je .std_ff
    cmp dword [rel g_os_major], 6
    jb .std_ff
    cmp byte [rel g_dir_need_short], 0
    jne .std_ff

    mov rcx, rbx
    mov edx, 1
    mov r8, rsi
    xor r9d, r9d
    mov qword [rsp + 32], 0
    mov dword [rsp + 40], 0
    call qword [rel pfnFindFirstFileExW64]
    cmp rax, INVALID_HANDLE_VALUE
    jne .got_h

.std_ff:
    mov rcx, rbx
    mov rdx, rsi
    call qword [rel __imp_FindFirstFileW]
    cmp rax, INVALID_HANDLE_VALUE
    je .err

.got_h:
    mov rbx, rax
    mov rcx, rsi
    mov rdx, rdi
    call nt64_fill_find_entry
    mov rax, rbx
    clc
    @epilog_ret

.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_find_next64
;; @abi win64_ms
;; @in RCX = search HANDLE, RDX = FIND_ENTRY64
;; @out CF=0 on next entry; CF=1 when enumeration ends
;; @err CF=1 on ERROR_NO_MORE_FILES
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Advances directory enumeration via FindNextFileW.
@proc_frame hal_find_next64, 40, rbx, rsi, rdi, r12
    mov rbx, rcx
    mov rdi, rdx
    call arena_mark64
    mov r12, rax
    mov ecx, 608
    call arena_alloc64
    jc .no_more
    mov rsi, rax
    mov dword [rsi + WIN32_FIND_DATAW.cAlternateFileName], 0
    mov rcx, rbx
    mov rdx, rsi
    call qword [rel __imp_FindNextFileW]
    test eax, eax
    jz .no_more
    mov rcx, rsi
    mov rdx, rdi
    call nt64_fill_find_entry
    mov rcx, r12
    call arena_release64
    clc
    @epilog_ret
.no_more:
    mov rcx, r12
    call arena_release64
    stc
    @epilog_ret
@endproc

;; @proc hal_disk_free_bytes64
;; @abi win64_ms
;; @in RCX = root or directory path (e.g. "C:")
;; @out RAX = 64-bit free bytes available to caller, CF=0; or CF=1 on error
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries 64-bit free disk space via GetDiskFreeSpaceExW with GetDiskFreeSpaceW fallback.
@proc_frame hal_disk_free_bytes64, 80, rbx
    call nt64_path_to_w
    jc .fail
    mov rbx, rax
    cmp byte [rel g_sim_nt52], 0
    jne .try_ex
.try_ex:
    mov rcx, rbx
    lea rdx, [rsp + 40]
    lea r8, [rsp + 48]
    lea r9, [rsp + 56]
    call qword [rel __imp_GetDiskFreeSpaceExW]
    test eax, eax
    jz .try_legacy
    mov rax, qword [rsp + 40]
    clc
    @epilog_ret

.try_legacy:
    mov rcx, rbx
    lea rdx, [rsp + 40]
    lea r8, [rsp + 44]
    lea r9, [rsp + 48]
    lea rax, [rsp + 52]
    mov qword [rsp + 32], rax
    call qword [rel __imp_GetDiskFreeSpaceW]
    test eax, eax
    jz .err
    mov eax, dword [rsp + 40]
    mov ecx, dword [rsp + 44]
    imul rax, rcx
    mov ecx, dword [rsp + 48]
    imul rax, rcx
    clc
    @epilog_ret

.err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc nt64_mklink
;; @abi win64_ms
;; @in RCX = link path (WTF-8), RDX = target path (WTF-8), R8D = mode (0=file sym, 1=/D dir sym, 2=/H hardlink, 3=/J junction)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements Vista..11 CreateSymbolicLinkW (+ Win10 0x2 retry), CreateHardLinkW (/H), and NTFS /J junctions.
@proc_frame nt64_mklink, 88, rbp, rbx, rsi, rdi, r12, r13
    mov rsi, rdx
    mov ebp, r8d

    call nt64_path_to_w
    jc .done_err
    mov rbx, rax

    mov rcx, rsi
    call wtf8_to_utf16le64
    jc .done_err
    mov rdi, rax

    cmp ebp, 2
    je .do_hardlink
    cmp ebp, 3
    je .do_junction

    mov rax, qword [rel pfnCreateSymbolicLinkW64]
    test rax, rax
    jz .not_sup

    xor r12d, r12d
    cmp ebp, 1
    jne .try_sym
    mov r12d, SYMBOLIC_LINK_FLAG_DIRECTORY
.try_sym:
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, r12d
    call qword [rel pfnCreateSymbolicLinkW64]
    test al, al
    jnz .ok

    or r12d, SYMBOLIC_LINK_FLAG_UNPRIVILEGED
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, r12d
    call qword [rel pfnCreateSymbolicLinkW64]
    test al, al
    jnz .ok
    jmp .win_err

.do_hardlink:
    mov rax, qword [rel pfnCreateHardLinkW64]
    test rax, rax
    jz .not_sup
    mov rcx, rbx
    mov rdx, rdi
    xor r8d, r8d
    call rax
    test eax, eax
    jnz .ok
    jmp .win_err

.do_junction:
    mov ecx, 2048
    call arena_alloc64
    jc .done_err
    mov rsi, rax
    mov rcx, rdi
    mov edx, 1020
    mov r8, rsi
    xor r9d, r9d
    call qword [rel __imp_GetFullPathNameW]
    test eax, eax
    jnz .got_jfull
    mov rsi, rdi
.got_jfull:
    mov rcx, rbx
    xor edx, edx
    call qword [rel __imp_CreateDirectoryW]
    test eax, eax
    jz .win_err

    mov rcx, rbx
    mov edx, GENERIC_WRITE
    xor r8d, r8d
    xor r9d, r9d
    mov dword [rsp + 32], OPEN_EXISTING
    mov dword [rsp + 40], FILE_FLAG_BACKUP_SEMANTICS + FILE_FLAG_OPEN_REPARSE_POINT
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    cmp rax, INVALID_HANDLE_VALUE
    je .rm_jdir_err
    mov r12, rax

    mov rcx, rsi
    call wcs_len64
    mov rdi, rax
    shl rdi, 1
    lea rcx, [rdi + rdi + 64]
    call arena_alloc64
    jc .close_j_err
    mov r13, rax

    mov dword [r13], IO_REPARSE_TAG_MOUNT_POINT
    lea eax, [edi + 8]
    mov word [r13 + 8], 0
    mov word [r13 + 10], ax
    add eax, 2
    mov word [r13 + 12], ax
    mov word [r13 + 14], di
    lea ecx, [edi + edi + 20]
    mov word [r13 + 4], cx
    mov word [r13 + 6], 0

    mov word [r13 + 16], 0x5C
    mov word [r13 + 18], '?'
    mov word [r13 + 20], '?'
    mov word [r13 + 22], 0x5C
    lea r8, [r13 + 24]
    mov rcx, rdi
    shr rcx, 1
    mov r9, rsi
.cp_sub:
    test rcx, rcx
    jz .sub_done
    mov ax, word [r9]
    mov word [r8], ax
    add r9, 2
    add r8, 2
    dec rcx
    jmp .cp_sub
.sub_done:
    mov word [r8], 0
    add r8, 2
    mov rcx, rdi
    shr rcx, 1
    mov r9, rsi
.cp_prt:
    test rcx, rcx
    jz .prt_done
    mov ax, word [r9]
    mov word [r8], ax
    add r9, 2
    add r8, 2
    dec rcx
    jmp .cp_prt
.prt_done:
    mov word [r8], 0

    movzx r9d, word [r13 + 4]
    add r9d, 8
    mov dword [rsp + 72], 0
    mov rcx, r12
    mov edx, FSCTL_SET_REPARSE_POINT
    mov r8, r13
    mov qword [rsp + 32], 0
    mov dword [rsp + 40], 0
    lea rax, [rsp + 72]
    mov qword [rsp + 48], rax
    mov qword [rsp + 56], 0
    call qword [rel __imp_DeviceIoControl]
    mov esi, eax

    mov rcx, r12
    call qword [rel __imp_CloseHandle]
    test esi, esi
    jnz .ok

.rm_jdir_err:
    call qword [rel __imp_GetLastError]
    mov esi, eax
    mov rcx, rbx
    call qword [rel __imp_RemoveDirectoryW]
    mov eax, esi
    call map_win32_to_err64
    @epilog_ret

.close_j_err:
    mov rcx, r12
    call qword [rel __imp_CloseHandle]
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret

.not_sup:
    mov eax, ERR_INVALID_FUNCTION
    stc
    @epilog_ret

.win_err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
    @epilog_ret

.ok:
    xor eax, eax
    clc
.done_err:
    @epilog_ret
@endproc

;; @proc hal_ensure_advapi64
;; @abi win64_ms
;; @in none
;; @out CF=0 if ADVAPI32 Registry exports resolved; CF=1 if unavailable
;; @err CF=1 when ADVAPI32 cannot be loaded
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Lazily loads ADVAPI32.DLL and resolves 64-bit RegOpenKeyExW, RegQueryValueExW, RegCreateKeyExW, RegSetValueExW, RegCloseKey.
@proc_frame hal_ensure_advapi64, 32, rbx
    cmp byte [rel g_advapi_loaded], 0
    jne .already
    lea rcx, [rel szDllAdvapi32_64]
    call qword [rel __imp_LoadLibraryA]
    mov rbx, rax
    test rbx, rbx
    jz .fail
    mov qword [rel g_hmod_advapi32], rbx

    mov rcx, rbx
    lea rdx, [rel szFnRegOpenKeyExW64]
    call dyn_resolve_proc64
    mov qword [rel pfnRegOpenKeyExW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnRegQueryValExW64]
    call dyn_resolve_proc64
    mov qword [rel pfnRegQueryValueExW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnRegCreateKeyExW64]
    call dyn_resolve_proc64
    mov qword [rel pfnRegCreateKeyExW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnRegSetValExW64]
    call dyn_resolve_proc64
    mov qword [rel pfnRegSetValueExW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnRegDeleteValW64]
    call dyn_resolve_proc64
    mov qword [rel pfnRegDeleteValueW64], rax

    mov rcx, rbx
    lea rdx, [rel szFnRegCloseKey64]
    call dyn_resolve_proc64
    mov qword [rel pfnRegCloseKey64], rax

    mov byte [rel g_advapi_loaded], 1
.already:
    cmp qword [rel pfnRegCloseKey64], 0
    je .fail
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_reg_query_str64
;; @abi win64_ms
;; @in RCX = HKEY root, RDX = subkey (WTF-8), R8 = value name (0 for default), R9 = out WTF-8 buf (>= 2048B)
;; @out CF=0, RAX = out buf on success; CF=1 on missing key/value
;; @err CF=1 if key or string value does not exist
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads a REG_SZ or REG_EXPAND_SZ Registry value via 64-bit ADVAPI32!RegOpenKeyExW + RegQueryValueExW (up to 2048B out).
@proc_frame hal_reg_query_str64, 72, rbx, rsi, rdi, r12, r13, r14
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov r12, r9

    call hal_ensure_advapi64
    jc .fail

    mov rcx, rsi
    call wtf8_to_utf16le64
    jc .fail
    mov r13, rax

    xor r14d, r14d
    test rdi, rdi
    jz .open_k
    mov rcx, rdi
    call wtf8_to_utf16le64
    jc .fail
    mov r14, rax

.open_k:
    mov qword [rsp + 56], 0
    mov rcx, rbx
    mov rdx, r13
    xor r8d, r8d
    mov r9d, KEY_READ_ACCESS
    lea rax, [rsp + 56]
    mov qword [rsp + 32], rax
    call qword [rel pfnRegOpenKeyExW64]
    test eax, eax
    jnz .fail

    mov ecx, 4096
    call arena_alloc64
    jc .close_fail
    mov rsi, rax
    mov dword [rsp + 64], 4094
    mov rcx, qword [rsp + 56]
    mov rdx, r14
    xor r8d, r8d
    xor r9d, r9d
    mov qword [rsp + 32], rsi
    lea rax, [rsp + 64]
    mov qword [rsp + 40], rax
    call qword [rel pfnRegQueryValueExW64]
    mov ebx, eax

    mov rcx, qword [rsp + 56]
    call qword [rel pfnRegCloseKey64]
    test ebx, ebx
    jnz .fail

    mov rcx, rsi
    mov rdx, -1
    mov r8, r12
    mov r9d, 2048
    call utf16le_to_wtf8_buf64
    mov rax, r12
    clc
    @epilog_ret

.close_fail:
    mov rcx, qword [rsp + 56]
    call qword [rel pfnRegCloseKey64]
.fail:
    stc
    @epilog_ret
@endproc

;; @proc hal_reg_set_str64
;; @abi win64_ms
;; @in RCX = HKEY root, RDX = subkey, R8 = value name (0 for default), R9 = WTF-8 string value
;; @out CF=0 on success; CF=1, EAX=ERR_ACCESS_DENIED on failure
;; @err CF=1, EAX=ERR_ACCESS_DENIED
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Creates/opens a Registry key and writes a REG_SZ value via 64-bit ADVAPI32!RegCreateKeyExW + RegSetValueExW.
@proc_frame hal_reg_set_str64, 104, rbx, rsi, rdi, r12, r13, r14
    mov rbx, rcx
    mov rsi, rdx
    mov rdi, r8
    mov r12, r9

    call hal_ensure_advapi64
    jc .fail

    mov rcx, rsi
    call wtf8_to_utf16le64
    jc .fail
    mov r13, rax

    xor r14d, r14d
    test rdi, rdi
    jz .cr_k
    mov rcx, rdi
    call wtf8_to_utf16le64
    jc .fail
    mov r14, rax

.cr_k:
    mov qword [rsp + 80], 0
    mov dword [rsp + 88], 0
    mov rcx, rbx
    mov rdx, r13
    xor r8d, r8d
    xor r9d, r9d
    mov dword [rsp + 32], 0
    mov dword [rsp + 40], KEY_WRITE_ACCESS
    mov qword [rsp + 48], 0
    lea rax, [rsp + 80]
    mov qword [rsp + 56], rax
    lea rax, [rsp + 88]
    mov qword [rsp + 64], rax
    call qword [rel pfnRegCreateKeyExW64]
    test eax, eax
    jnz .fail

    mov rcx, r12
    call wtf8_to_utf16le64
    jc .close_err
    lea r11d, [edx + edx + 2]
    mov rcx, qword [rsp + 80]
    mov rdx, r14
    xor r8d, r8d
    mov r9d, REG_SZ_TYPE
    mov qword [rsp + 32], rax
    mov dword [rsp + 40], r11d
    call qword [rel pfnRegSetValueExW64]
    mov ebx, eax

    mov rcx, qword [rsp + 80]
    call qword [rel pfnRegCloseKey64]
    test ebx, ebx
    jnz .fail
    clc
    @epilog_ret

.close_err:
    mov rcx, qword [rsp + 80]
    call qword [rel pfnRegCloseKey64]
.fail:
    mov eax, ERR_ACCESS_DENIED
    stc
    @epilog_ret
@endproc
