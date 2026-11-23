;; @file u_common32.inc
;; @isa 386
;; @desc Shared CRT-free IA-32 PE32 utility runtime (NT 3.1..11 *W Unicode + Win95..Me *A ANSI, CLI, UTF, VFS, formatters).

@cpu 386
@format inc

;; @proc _u32_seh_handler
;; @abi cdecl
;; @in [esp+4] = EXCEPTION_RECORD*, [esp+8] = EstablisherFrame, [esp+12] = CONTEXT*, [esp+16] = DispatcherContext
;; @out eax = 1 (ExceptionContinueSearch)
;; @err none
;; @clobbers eax
;; @desc /SAFESEH-registered structured exception handler for standalone IA-32 utilities.
@proc _u32_seh_handler
    mov eax, 1
    ret
@endproc

;; @proc u32_strlen
;; @abi win32
;; @in ESI = NUL-terminated byte string
;; @out EAX = byte length (excluding NUL)
;; @err none
;; @clobbers eax
;; @desc Counts bytes in a NUL-terminated string without modifying ESI.
@proc u32_strlen
    xor eax, eax
.lp:
    cmp byte [esi + eax], 0
    je .dn
    inc eax
    jmp .lp
.dn:
    ret
@endproc

;; @proc u32_strcpy
;; @abi win32
;; @in ESI = NUL-terminated source, EDI = destination buffer
;; @out EAX = byte length copied (destination NUL-terminated)
;; @err none
;; @clobbers eax, edx
;; @desc Copies a NUL-terminated string from ESI to EDI preserving ESI and EDI.
@proc u32_strcpy
    xor eax, eax
.lp:
    mov dl, byte [esi + eax]
    mov byte [edi + eax], dl
    test dl, dl
    jz .dn
    inc eax
    jmp .lp
.dn:
    ret
@endproc

;; @proc u32_chr_upper
;; @abi win32
;; @in AL = ASCII byte
;; @out AL = uppercase ASCII byte
;; @err none
;; @clobbers eax
;; @desc Folds ASCII 'a'..'z' to 'A'..'Z'.
@proc u32_chr_upper
    cmp al, 'a'
    jb .dn
    cmp al, 'z'
    ja .dn
    sub al, 32
.dn:
    ret
@endproc

;; @proc u32_str_eq_ci
;; @abi win32
;; @in ESI = string A, EDI = string B
;; @out EAX = 1 if equal case-insensitively, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Case-insensitive ASCII string equality check.
@proc u32_str_eq_ci
    xor ecx, ecx
.lp:
    mov al, byte [esi + ecx]
    call u32_chr_upper
    mov dl, al
    mov al, byte [edi + ecx]
    call u32_chr_upper
    cmp dl, al
    jne .no
    test dl, dl
    jz .yes
    inc ecx
    jmp .lp
.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret
@endproc

;; @proc u32_w_to_u8
;; @abi win32
;; @in ESI = UTF-16LE NUL-terminated source, EDI = UTF-8 destination, ECX = max dst bytes
;; @out EAX = UTF-8 byte length written (NUL-terminated)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Pure-i386 UTF-16LE/WTF-16 to UTF-8/WTF-8 transcoder.
@proc u32_w_to_u8
    push ebx
    push esi
    push edi
    mov ebx, edi
    lea edx, [edi + ecx - 4]
.lp:
    movzx eax, word [esi]
    add esi, 2
    test eax, eax
    jz .done
    cmp edi, edx
    jae .done
    cmp eax, 0x80
    jae .u2
    mov byte [edi], al
    inc edi
    jmp .lp
.u2:
    cmp eax, 0x800
    jae .u3
    mov ecx, eax
    shr ecx, 6
    or cl, 0xC0
    mov byte [edi], cl
    and al, 0x3F
    or al, 0x80
    mov byte [edi + 1], al
    add edi, 2
    jmp .lp
.u3:
    mov ecx, eax
    shr ecx, 12
    or cl, 0xE0
    mov byte [edi], cl
    mov ecx, eax
    shr ecx, 6
    and cl, 0x3F
    or cl, 0x80
    mov byte [edi + 1], cl
    and al, 0x3F
    or al, 0x80
    mov byte [edi + 2], al
    add edi, 3
    jmp .lp
.done:
    mov byte [edi], 0
    mov eax, edi
    sub eax, ebx
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_u8_to_w
;; @abi win32
;; @in ESI = UTF-8 NUL-terminated source, EDI = UTF-16LE destination (up to 1020 WCHARs)
;; @out EAX = WCHAR count written (excluding NUL)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Pure-i386 UTF-8/WTF-8 to UTF-16LE transcoder.
@proc u32_u8_to_w
    push ebx
    push esi
    push edi
    mov ebx, edi
    lea edx, [edi + 2036]
.lp:
    movzx eax, byte [esi]
    test eax, eax
    jz .done
    cmp edi, edx
    jae .done
    inc esi
    cmp eax, 0x80
    jb .emit
    mov ecx, eax
    and ecx, 0xE0
    cmp ecx, 0xC0
    jne .chk3
    movzx ecx, byte [esi]
    test ecx, ecx
    jz .emit
    inc esi
    and eax, 0x1F
    shl eax, 6
    and ecx, 0x3F
    or eax, ecx
    jmp .emit
.chk3:
    mov ecx, eax
    and ecx, 0xF0
    cmp ecx, 0xE0
    jne .emit
    movzx ecx, byte [esi]
    test ecx, ecx
    jz .emit
    movzx eax, al
    and eax, 0x0F
    shl eax, 12
    and ecx, 0x3F
    shl ecx, 6
    or eax, ecx
    movzx ecx, byte [esi + 1]
    test ecx, ecx
    jz .adv1
    add esi, 2
    and ecx, 0x3F
    or eax, ecx
    jmp .emit
.adv1:
    inc esi
.emit:
    mov word [edi], ax
    add edi, 2
    jmp .lp
.done:
    mov word [edi], 0
    mov eax, edi
    sub eax, ebx
    shr eax, 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_init
;; @abi win32
;; @in none
;; @out EAX = pointer to command-line argument tail (after argv[0]) in g_u32_cmd
;; @err Terminates with exit code 1 if 512 KiB work arena cannot be allocated
;; @clobbers eax, ecx, edx
;; @desc Initializes OS mode (*W vs *A), std handles, 512 KiB VirtualAlloc arena, and UTF-8 command line.
@proc u32_init
    push ebx
    push esi
    push edi

    call dword [__imp__GetVersion@0]
    shr eax, 31
    mov dword [g_u32_is_win9x], eax

    push -10
    call dword [__imp__GetStdHandle@4]
    mov dword [g_u32_stdin], eax

    push -11
    call dword [__imp__GetStdHandle@4]
    mov dword [g_u32_stdout], eax

    push -12
    call dword [__imp__GetStdHandle@4]
    mov dword [g_u32_stderr], eax

    push addr(g_u32_io_done)
    push dword [g_u32_stdout]
    call dword [__imp__GetConsoleMode@8]
    mov dword [g_u32_out_is_con], eax

    push addr(g_u32_io_done)
    push dword [g_u32_stdin]
    call dword [__imp__GetConsoleMode@8]
    mov dword [g_u32_in_is_con], eax

    push 4 ; PAGE_READWRITE
    push 0x3000 ; MEM_COMMIT | MEM_RESERVE
    push 524288 ; 512 KiB
    push 0
    call dword [__imp__VirtualAlloc@16]
    test eax, eax
    jnz .arena_ok
    push 1
    call dword [__imp__ExitProcess@4]
.arena_ok:
    mov dword [g_u32_arena], eax

    cmp dword [g_u32_is_win9x], 0
    jne .cmd_9x
    call dword [__imp__GetCommandLineW@0]
    mov esi, eax
    mov edi, addr(g_u32_cmd)
    mov ecx, 4092
    call u32_w_to_u8
    jmp .skip_exe

.cmd_9x:
    call dword [__imp__GetCommandLineA@0]
    mov esi, eax
    mov edi, addr(g_u32_cmd)
    xor ecx, ecx
.cp9x:
    cmp ecx, 4092
    jae .term9x
    mov dl, byte [esi + ecx]
    mov byte [edi + ecx], dl
    test dl, dl
    jz .skip_exe
    inc ecx
    jmp .cp9x
.term9x:
    mov byte [edi + ecx], 0

.skip_exe:
    mov eax, addr(g_u32_cmd)
.ws0:
    mov dl, byte [eax]
    cmp dl, 0x20
    je .adv0
    cmp dl, 0x09
    jne .tok0
.adv0:
    inc eax
    jmp .ws0
.tok0:
    cmp dl, '"'
    je .q0
.uq0:
    mov dl, byte [eax]
    test dl, dl
    jz .done
    cmp dl, 0x20
    je .done
    cmp dl, 0x09
    je .done
    inc eax
    jmp .uq0
.q0:
    inc eax
.qlp0:
    mov dl, byte [eax]
    test dl, dl
    jz .done
    inc eax
    cmp dl, '"'
    jne .qlp0
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_exit
;; @abi win32
;; @in EAX = process exit code
;; @out Does not return
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Releases work arena if present and terminates via KERNEL32!ExitProcess.
@proc u32_exit
    push ebx
    mov ebx, eax
    mov eax, dword [g_u32_arena]
    test eax, eax
    jz .do_exit
    push 0x8000 ; MEM_RELEASE
    push 0
    push eax
    call dword [__imp__VirtualFree@12]
.do_exit:
    push ebx
    call dword [__imp__ExitProcess@4]
    pop ebx
    ret
@endproc

;; @proc u32_write_handle
;; @abi win32
;; @in EBX = handle, ESI = UTF-8 buffer, ECX = byte length, EDX = is_console (1 or 0)
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes UTF-8 bytes via WriteConsoleW (NT console) or WriteFile (pipes/files/Win9x).
@proc u32_write_handle
    push ebx
    push esi
    push edi
    test ecx, ecx
    jz .done
    test edx, edx
    jz .raw_file
    cmp dword [g_u32_is_win9x], 0
    jne .raw_file

    ; Transcode chunks up to 512 bytes into g_u32_wbuf1 and WriteConsoleW
    mov edi, ecx
.chunk_lp:
    test edi, edi
    jz .done
    mov ecx, edi
    cmp ecx, 512
    jbe .got_chunk
    mov ecx, 512
.got_chunk:
    push edi
    push esi
    ; Copy chunk to g_u32_path_tmp NUL-terminated
    mov edi, addr(g_u32_path_tmp)
    xor edx, edx
.cp_c:
    cmp edx, ecx
    jae .cp_c_dn
    mov al, byte [esi + edx]
    mov byte [edi + edx], al
    inc edx
    jmp .cp_c
.cp_c_dn:
    mov byte [edi + edx], 0
    mov esi, addr(g_u32_path_tmp)
    mov edi, addr(g_u32_wbuf1)
    push ecx
    call u32_u8_to_w
    mov edx, eax
    pop ecx
    push 0
    push addr(g_u32_io_done)
    push edx
    push addr(g_u32_wbuf1)
    push ebx
    call dword [__imp__WriteConsoleW@20]
    pop esi
    pop edi
    add esi, ecx
    sub edi, ecx
    jmp .chunk_lp

.raw_file:
    push 0
    push addr(g_u32_io_done)
    push ecx
    push esi
    push ebx
    call dword [__imp__WriteFile@20]
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_write_out
;; @abi win32
;; @in ESI = UTF-8 bytes, ECX = byte length
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes ECX UTF-8 bytes to standard output.
@proc u32_write_out
    push ebx
    mov ebx, dword [g_u32_stdout]
    mov edx, dword [g_u32_out_is_con]
    call u32_write_handle
    pop ebx
    ret
@endproc

;; @proc u32_write_str
;; @abi win32
;; @in ESI = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes NUL-terminated UTF-8 string to standard output.
@proc u32_write_str
    call u32_strlen
    mov ecx, eax
    call u32_write_out
    ret
@endproc

;; @proc u32_write_err_str
;; @abi win32
;; @in ESI = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes NUL-terminated UTF-8 string to standard error.
@proc u32_write_err_str
    push ebx
    call u32_strlen
    mov ecx, eax
    mov ebx, dword [g_u32_stderr]
    xor edx, edx
    call u32_write_handle
    pop ebx
    ret
@endproc

;; @proc u32_write_crlf
;; @abi win32
;; @in none
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes CRLF to standard output.
@proc u32_write_crlf
    push esi
    mov esi, addr(szU32Crlf)
    mov ecx, 2
    call u32_write_out
    pop esi
    ret
@endproc

;; @proc u32_write_chr
;; @abi win32
;; @in AL = single byte character
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Writes a single byte character to standard output.
@proc u32_write_chr
    push esi
    mov byte [g_u32_num_buf], al
    mov esi, addr(g_u32_num_buf)
    mov ecx, 1
    call u32_write_out
    pop esi
    ret
@endproc

;; @proc u32_print_u32
;; @abi win32
;; @in EAX = unsigned 32-bit integer
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats EAX in base 10 and writes to standard output.
@proc u32_print_u32
    push ebx
    push esi
    push edi
    mov edi, addr(g_u32_num_buf)
    add edi, 31
    mov byte [edi], 0
    mov ebx, 10
.lp:
    dec edi
    xor edx, edx
    div ebx
    add dl, '0'
    mov byte [edi], dl
    test eax, eax
    jnz .lp
    mov esi, edi
    call u32_write_str
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_print_u64
;; @abi win32
;; @in EDX:EAX = unsigned 64-bit integer
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Formats 64-bit unsigned integer EDX:EAX in base 10 without 64-bit hardware div overflow traps.
@proc u32_print_u64
    push ebx
    push esi
    push edi
    mov edi, addr(g_u32_num_buf)
    add edi, 48
    mov byte [edi], 0
    mov ebx, eax ; low 32
    mov esi, edx ; high 32
.lp:
    dec edi
    ; Divide ESI:EBX by 10 -> quotient ESI:EBX, remainder EDX
    mov ecx, 10
    xor edx, edx
    mov eax, esi
    div ecx
    mov esi, eax
    mov eax, ebx
    div ecx
    mov ebx, eax
    add dl, '0'
    mov byte [edi], dl
    mov eax, ebx
    or eax, esi
    jnz .lp
    mov esi, edi
    call u32_write_str
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_print_hex8
;; @abi win32
;; @in AL = 8-bit value
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints 2 uppercase hex digits for AL.
@proc u32_print_hex8
    push ebx
    movzx ebx, al
    mov eax, ebx
    shr eax, 4
    cmp al, 10
    jb .d1
    add al, 7
.d1:
    add al, '0'
    call u32_write_chr
    mov eax, ebx
    and al, 0x0F
    cmp al, 10
    jb .d2
    add al, 7
.d2:
    add al, '0'
    call u32_write_chr
    pop ebx
    ret
@endproc

;; @proc u32_print_hex32
;; @abi win32
;; @in EAX = 32-bit value
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints 8 uppercase hex digits for EAX.
@proc u32_print_hex32
    push ebx
    mov ebx, eax
    shr eax, 24
    call u32_print_hex8
    mov eax, ebx
    shr eax, 16
    call u32_print_hex8
    mov eax, ebx
    shr eax, 8
    call u32_print_hex8
    mov eax, ebx
    call u32_print_hex8
    pop ebx
    ret
@endproc

;; @proc u32_skip_ws
;; @abi win32
;; @in ESI = cursor in command line
;; @out EAX = advanced cursor at first non-whitespace byte, DL = byte at [EAX]
;; @err none
;; @clobbers eax, edx
;; @desc Advances past spaces and tabs.
@proc u32_skip_ws
    mov eax, esi
.lp:
    mov dl, byte [eax]
    cmp dl, 0x20
    je .adv
    cmp dl, 0x09
    jne .dn
.adv:
    inc eax
    jmp .lp
.dn:
    ret
@endproc

;; @proc u32_read_tok
;; @abi win32
;; @in ESI = cursor in command line, EDI = destination token buffer (1024B)
;; @out EAX = updated cursor after token, ECX = token length
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Reads next whitespace-delimited or double-quoted token into EDI (stripping outer quotes).
@proc u32_read_tok
    push ebx
    push esi
    push edi
    call u32_skip_ws
    mov esi, eax
    mov ebx, edi
    test dl, dl
    jz .done
    xor ecx, ecx
.tok_lp:
    mov dl, byte [esi]
    test dl, dl
    jz .done
    cmp dl, 0x20
    je .done
    cmp dl, 0x09
    je .done
    cmp dl, '"'
    je .in_q
    cmp ecx, 1000
    jae .skip_ch
    mov byte [edi], dl
    inc edi
    inc ecx
.skip_ch:
    inc esi
    jmp .tok_lp
.in_q:
    inc esi
.qlp:
    mov dl, byte [esi]
    test dl, dl
    jz .done
    inc esi
    cmp dl, '"'
    je .tok_lp
    cmp ecx, 1000
    jae .qlp
    mov byte [edi], dl
    inc edi
    inc ecx
    jmp .qlp
.done:
    mov byte [edi], 0
    mov eax, esi
    mov ecx, edi
    sub ecx, ebx
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_parse_u32
;; @abi win32
;; @in ESI = ASCII decimal digit string
;; @out EAX = parsed u32 value, EDX = pointer to first non-digit
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Parses unsigned decimal integer from ESI.
@proc u32_parse_u32
    push ebx
    xor eax, eax
    mov edx, esi
.lp:
    movzx ecx, byte [edx]
    cmp cl, '0'
    jb .dn
    cmp cl, '9'
    ja .dn
    sub cl, '0'
    mov ebx, eax
    shl eax, 3
    add eax, ebx
    add eax, ebx
    add eax, ecx
    inc edx
    jmp .lp
.dn:
    pop ebx
    ret
@endproc

;; @proc u32_get_attr
;; @abi win32
;; @in ESI = UTF-8 path
;; @out EAX = Win32 file attributes or 0xFFFFFFFF
;; @err EAX = 0xFFFFFFFF
;; @clobbers eax, ecx, edx
;; @desc Queries file attributes via GetFileAttributesW (NT) or GetFileAttributesA (Win9x).
@proc u32_get_attr
    push edi
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_u32_wbuf1)
    call dword [__imp__GetFileAttributesW@4]
    pop edi
    ret
.a9x:
    push esi
    call dword [__imp__GetFileAttributesA@4]
    pop edi
    ret
@endproc

;; @proc u32_set_attr
;; @abi win32
;; @in ESI = UTF-8 path, ECX = Win32 file attributes
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Sets file attributes via SetFileAttributesW (NT) or SetFileAttributesA (Win9x).
@proc u32_set_attr
    push edi
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    push ecx
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    pop ecx
    push ecx
    push addr(g_u32_wbuf1)
    call dword [__imp__SetFileAttributesW@8]
    pop edi
    ret
.a9x:
    push ecx
    push esi
    call dword [__imp__SetFileAttributesA@8]
    pop edi
    ret
@endproc

;; @proc u32_copy_file
;; @abi win32
;; @in ESI = UTF-8 src, EDI = UTF-8 dst, ECX = bFailIfExists (0 or 1)
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Copies a file via CopyFileW (NT) or CopyFileA (Win9x).
@proc u32_copy_file
    push ebx
    push esi
    push edi
    mov ebx, ecx
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    push edi
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    pop esi
    mov edi, addr(g_u32_wbuf2)
    call u32_u8_to_w
    push ebx
    push addr(g_u32_wbuf2)
    push addr(g_u32_wbuf1)
    call dword [__imp__CopyFileW@12]
    jmp .dn
.a9x:
    push ebx
    push edi
    push esi
    call dword [__imp__CopyFileA@12]
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_mkdir
;; @abi win32
;; @in ESI = UTF-8 directory path
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Creates directory via CreateDirectoryW (NT) or CreateDirectoryA (Win9x).
@proc u32_mkdir
    push edi
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push 0
    push addr(g_u32_wbuf1)
    call dword [__imp__CreateDirectoryW@8]
    pop edi
    ret
.a9x:
    push 0
    push esi
    call dword [__imp__CreateDirectoryA@8]
    pop edi
    ret
@endproc

;; @proc u32_delete_file
;; @abi win32
;; @in ESI = UTF-8 file path
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Deletes a file via DeleteFileW (NT) or DeleteFileA (Win9x).
@proc u32_delete_file
    push edi
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_u32_wbuf1)
    call dword [__imp__DeleteFileW@4]
    pop edi
    ret
.a9x:
    push esi
    call dword [__imp__DeleteFileA@4]
    pop edi
    ret
@endproc

;; @proc u32_get_cwd
;; @abi win32
;; @in EDI = UTF-8 destination buffer, ECX = capacity
;; @out EAX = UTF-8 byte length
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Queries current working directory in UTF-8.
@proc u32_get_cwd
    push ebx
    push esi
    push edi
    mov ebx, ecx
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    push addr(g_u32_wbuf1)
    push 1000
    call dword [__imp__GetCurrentDirectoryW@8]
    mov esi, addr(g_u32_wbuf1)
    mov ecx, ebx
    call u32_w_to_u8
    jmp .dn
.a9x:
    push edi
    push ebx
    call dword [__imp__GetCurrentDirectoryA@8]
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_get_env
;; @abi win32
;; @in ESI = ASCII/UTF-8 variable name, EDI = UTF-8 destination, ECX = capacity
;; @out EAX = byte length (0 if not set)
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Queries environment variable via GetEnvironmentVariableW (NT) or GetEnvironmentVariableA (Win9x).
@proc u32_get_env
    push ebx
    push esi
    push edi
    mov ebx, ecx
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    push edi
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push 1000
    push addr(g_u32_wbuf2)
    push addr(g_u32_wbuf1)
    call dword [__imp__GetEnvironmentVariableW@12]
    pop edi
    test eax, eax
    jz .not_set
    mov esi, addr(g_u32_wbuf2)
    mov ecx, ebx
    call u32_w_to_u8
    jmp .dn
.not_set:
    mov byte [edi], 0
    xor eax, eax
    jmp .dn
.a9x:
    push ebx
    push edi
    push esi
    call dword [__imp__GetEnvironmentVariableA@12]
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_open_read
;; @abi win32
;; @in ESI = UTF-8 path
;; @out EAX = Win32 file handle or -1 (INVALID_HANDLE_VALUE)
;; @err EAX = -1
;; @clobbers eax, ecx, edx
;; @desc Opens an existing file for reading with FILE_SHARE_READ | FILE_SHARE_WRITE.
@proc u32_open_read
    push edi
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push 0
    push 0x02000080 ; FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS
    push 3 ; OPEN_EXISTING
    push 0
    push 3 ; FILE_SHARE_READ | FILE_SHARE_WRITE
    push 0x80000000 ; GENERIC_READ
    push addr(g_u32_wbuf1)
    call dword [__imp__CreateFileW@28]
    pop edi
    ret
.a9x:
    push 0
    push 0x80
    push 3
    push 0
    push 3
    push 0x80000000
    push esi
    call dword [__imp__CreateFileA@28]
    pop edi
    ret
@endproc

;; @proc u32_load_stream
;; @abi win32
;; @in ESI = UTF-8 path (empty string = stdin), EDI = dst buffer, ECX = max bytes, EDX = decode_bom (1=text BOM, 0=raw)
;; @out EAX = bytes stored in EDI (NUL-terminated), CF=0 on success
;; @err CF=1 on open error
;; @clobbers eax, ecx, edx
;; @desc Reads file or stdin up to ECX bytes and optionally normalizes UTF-8 / UTF-16LE / UTF-16BE BOMs.
@proc u32_load_stream
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 16
    mov dword [esp], edi ; dst
    mov dword [esp + 4], ecx ; max_bytes
    mov dword [esp + 8], edx ; decode_bom
    mov dword [esp + 12], 0 ; close_flag

    cmp byte [esi], 0
    jne .open_f
    mov ebx, dword [g_u32_stdin]
    jmp .read_lp_init

.open_f:
    call u32_open_read
    cmp eax, -1
    je .fail
    mov ebx, eax
    mov dword [esp + 12], 1

.read_lp_init:
    xor ebp, ebp ; total read
.read_lp:
    mov ecx, dword [esp + 4]
    sub ecx, ebp
    jz .eof
    cmp ecx, 32768
    jbe .do_rd
    mov ecx, 32768
.do_rd:
    mov eax, dword [esp]
    add eax, ebp
    push 0
    push addr(g_u32_io_done)
    push ecx
    push eax
    push ebx
    call dword [__imp__ReadFile@20]
    test eax, eax
    jz .eof
    mov eax, dword [g_u32_io_done]
    test eax, eax
    jz .eof
    add ebp, eax
    jmp .read_lp

.eof:
    cmp dword [esp + 12], 0
    je .chk_bom
    push ebx
    call dword [__imp__CloseHandle@4]

.chk_bom:
    mov edi, dword [esp]
    mov byte [edi + ebp], 0
    mov byte [edi + ebp + 1], 0
    cmp dword [esp + 8], 0
    je .ok_done
    cmp ebp, 2
    jb .ok_done

    ; UTF-8 BOM: EF BB BF
    cmp ebp, 3
    jb .chk_u16le
    cmp byte [edi], 0xEF
    jne .chk_u16le
    cmp byte [edi + 1], 0xBB
    jne .chk_u16le
    cmp byte [edi + 2], 0xBF
    jne .chk_u16le
    sub ebp, 3
    xor ecx, ecx
.sh_u8:
    cmp ecx, ebp
    ja .ok_done
    mov al, byte [edi + ecx + 3]
    mov byte [edi + ecx], al
    inc ecx
    jmp .sh_u8

.chk_u16le:
    cmp byte [edi], 0xFF
    jne .chk_u16be
    cmp byte [edi + 1], 0xFE
    jne .chk_u16be
    jmp .cvt_u16

.chk_u16be:
    cmp byte [edi], 0xFE
    jne .ok_done
    cmp byte [edi + 1], 0xFF
    jne .ok_done
    ; Swap big-endian words in place starting at offset 2
    mov ecx, 2
.sw_be:
    lea eax, [ecx + 1]
    cmp eax, ebp
    jae .cvt_u16
    mov al, byte [edi + ecx]
    mov dl, byte [edi + ecx + 1]
    mov byte [edi + ecx], dl
    mov byte [edi + ecx + 1], al
    add ecx, 2
    jmp .sw_be

.cvt_u16:
    ; Copy UTF-16LE payload [edi+2 .. edi+ebp] to arena tail, then transcode back into edi
    mov esi, dword [g_u32_arena]
    add esi, 262144
    xor ecx, ecx
    sub ebp, 2
.cp_w:
    cmp ecx, ebp
    jae .cp_w_dn
    mov al, byte [edi + ecx + 2]
    mov byte [esi + ecx], al
    inc ecx
    jmp .cp_w
.cp_w_dn:
    mov word [esi + ecx], 0
    mov ecx, dword [esp + 4]
    call u32_w_to_u8
    mov ebp, eax

.ok_done:
    mov eax, ebp
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.fail:
    xor eax, eax
    add esp, 16
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc u32_save_file
;; @abi win32
;; @in ESI = UTF-8 path, EDI = buffer, ECX = byte length
;; @out EAX = 1 on success, 0 on error
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Creates/overwrites file ESI and writes ECX bytes from EDI.
@proc u32_save_file
    push ebx
    push esi
    push edi
    mov ebx, ecx
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    push edi
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    pop edi
    push 0
    push 0x80
    push 2 ; CREATE_ALWAYS
    push 0
    push 0
    push 0x40000000 ; GENERIC_WRITE
    push addr(g_u32_wbuf1)
    call dword [__imp__CreateFileW@28]
    jmp .opened
.a9x:
    push 0
    push 0x80
    push 2
    push 0
    push 0
    push 0x40000000
    push esi
    call dword [__imp__CreateFileA@28]
.opened:
    cmp eax, -1
    je .fail
    mov esi, eax
    push 0
    push addr(g_u32_io_done)
    push ebx
    push edi
    push esi
    call dword [__imp__WriteFile@20]
    push esi
    call dword [__imp__CloseHandle@4]
    mov eax, 1
    jmp .dn
.fail:
    xor eax, eax
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_find_unpack
;; @abi win32
;; @in ESI = raw WIN32_FIND_DATAW/A buffer, EDI = normalized entry (0:attr, 4:ftLo, 8:ftHi, 12:szHi, 16:szLo, 20:UTF-8 name[520])
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Unpacks WIN32_FIND_DATAW (NT) or WIN32_FIND_DATAA (Win9x) into a uniform 544-byte record.
@proc u32_find_unpack
    push esi
    push edi
    mov eax, dword [esi]
    mov dword [edi], eax
    mov eax, dword [esi + 20] ; ftLastWriteTime.dwLowDateTime
    mov dword [edi + 4], eax
    mov eax, dword [esi + 24] ; ftLastWriteTime.dwHighDateTime
    mov dword [edi + 8], eax
    mov eax, dword [esi + 28] ; nFileSizeHigh
    mov dword [edi + 12], eax
    mov eax, dword [esi + 32] ; nFileSizeLow
    mov dword [edi + 16], eax
    add esi, 44
    add edi, 20
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    mov ecx, 516
    call u32_w_to_u8
    jmp .dn
.a9x:
    call u32_strcpy
.dn:
    pop edi
    pop esi
    ret
@endproc

;; @proc u32_find_first
;; @abi win32
;; @in ESI = UTF-8 wildcard pattern, EDI = 544-byte normalized entry buffer
;; @out EAX = search handle (-1 if no match)
;; @err EAX = -1
;; @clobbers eax, ecx, edx
;; @desc Starts directory enumeration via FindFirstFileW (NT) or FindFirstFileA (Win9x).
@proc u32_find_first
    push ebx
    push esi
    push edi
    mov ebx, edi
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push addr(g_u32_find_raw)
    push addr(g_u32_wbuf1)
    call dword [__imp__FindFirstFileW@8]
    jmp .chk
.a9x:
    push addr(g_u32_find_raw)
    push esi
    call dword [__imp__FindFirstFileA@8]
.chk:
    cmp eax, -1
    je .dn
    push eax
    mov esi, addr(g_u32_find_raw)
    mov edi, ebx
    call u32_find_unpack
    pop eax
.dn:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc u32_find_next
;; @abi win32
;; @in EBX = search handle, EDI = 544-byte normalized entry buffer
;; @out EAX = non-zero on match, 0 when exhausted
;; @err EAX = 0
;; @clobbers eax, ecx, edx
;; @desc Advances directory enumeration via FindNextFileW (NT) or FindNextFileA (Win9x).
@proc u32_find_next
    push esi
    push edi
    cmp dword [g_u32_is_win9x], 0
    jne .a9x
    push addr(g_u32_find_raw)
    push ebx
    call dword [__imp__FindNextFileW@8]
    jmp .chk
.a9x:
    push addr(g_u32_find_raw)
    push ebx
    call dword [__imp__FindNextFileA@8]
.chk:
    test eax, eax
    jz .dn
    push eax
    mov esi, addr(g_u32_find_raw)
    call u32_find_unpack
    pop eax
.dn:
    pop edi
    pop esi
    ret
@endproc

;; @proc u32_path_join
;; @abi win32
;; @in ESI = directory UTF-8, EDI = leaf UTF-8, EBX = output buffer (1024B)
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Joins directory ESI and leaf EDI into EBX with a single backslash separator.
@proc u32_path_join
    push esi
    push edi
    mov ecx, edi
    mov edi, ebx
    call u32_strcpy
    test eax, eax
    jz .copy_leaf
    mov dl, byte [ebx + eax - 1]
    cmp dl, 0x5C
    je .at_sep
    cmp dl, '/'
    je .at_sep
    cmp dl, ':'
    je .at_sep
    mov byte [ebx + eax], 0x5C
    inc eax
.at_sep:
    lea edi, [ebx + eax]
    mov esi, ecx
    call u32_strcpy
    pop edi
    pop esi
    ret
.copy_leaf:
    mov edi, ebx
    mov esi, ecx
    call u32_strcpy
    pop edi
    pop esi
    ret
@endproc
