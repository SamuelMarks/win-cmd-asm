;; @file u_common64.inc
;; @isa x64
;; @desc Shared CRT-free x86-64 PE32+ utility runtime (*W Unicode, .pdata/.xdata SEH, CLI, UTF, VFS, 64-bit formatters).

@cpu x64
@format inc

;; @proc u64_strlen
;; @abi win64_ms
;; @in RCX = NUL-terminated UTF-8 string
;; @out RAX = byte length
;; @err none
;; @clobbers rax
;; @desc Counts bytes in NUL-terminated string RCX.
@proc u64_strlen
    xor eax, eax
.lp:
    cmp byte [rcx + rax], 0
    je .dn
    inc rax
    jmp .lp
.dn:
    ret
@endproc

;; @proc u64_strcpy
;; @abi win64_ms
;; @in RCX = dst buffer, RDX = NUL-terminated src string
;; @out RAX = byte length copied
;; @err none
;; @clobbers rax, r8
;; @desc Copies NUL-terminated string from RDX into RCX.
@proc u64_strcpy
    xor eax, eax
.lp:
    mov r8b, byte [rdx + rax]
    mov byte [rcx + rax], r8b
    test r8b, r8b
    jz .dn
    inc rax
    jmp .lp
.dn:
    ret
@endproc

;; @proc u64_chr_upper
;; @abi win64_ms
;; @in AL = ASCII byte
;; @out AL = uppercase ASCII byte
;; @err none
;; @clobbers rax
;; @desc Folds ASCII 'a'..'z' to 'A'..'Z'.
@proc u64_chr_upper
    cmp al, 'a'
    jb .dn
    cmp al, 'z'
    ja .dn
    sub al, 32
.dn:
    ret
@endproc

;; @proc u64_str_eq_ci
;; @abi win64_ms
;; @in RCX = string A, RDX = string B
;; @out EAX = 1 if equal case-insensitively, 0 otherwise
;; @err none
;; @clobbers rax, r8, r9
;; @desc Case-insensitive ASCII string equality comparison.
@proc u64_str_eq_ci
    xor r8d, r8d
.lp:
    mov al, byte [rcx + r8]
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    mov r9b, al
    mov al, byte [rdx + r8]
    cmp al, 'a'
    jb .u2
    cmp al, 'z'
    ja .u2
    sub al, 32
.u2:
    cmp r9b, al
    jne .no
    test r9b, r9b
    jz .yes
    inc r8
    jmp .lp
.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret
@endproc

;; @proc u64_w_to_u8
;; @abi win64_ms
;; @in RCX = UTF-16LE src, RDX = UTF-8 dst, R8 = max dst bytes
;; @out RAX = UTF-8 byte length written (NUL-terminated)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10
;; @desc Pure x86-64 UTF-16LE to UTF-8/WTF-8 transcoder.
@proc u64_w_to_u8
    mov r9, rdx ; start
    lea r10, [rdx + r8 - 4]
.lp:
    movzx eax, word [rcx]
    add rcx, 2
    test eax, eax
    jz .done
    cmp rdx, r10
    jae .done
    cmp eax, 0x80
    jae .u2
    mov byte [rdx], al
    inc rdx
    jmp .lp
.u2:
    cmp eax, 0x800
    jae .u3
    mov r8d, eax
    shr r8d, 6
    or r8b, 0xC0
    mov byte [rdx], r8b
    and al, 0x3F
    or al, 0x80
    mov byte [rdx + 1], al
    add rdx, 2
    jmp .lp
.u3:
    mov r8d, eax
    shr r8d, 12
    or r8b, 0xE0
    mov byte [rdx], r8b
    mov r8d, eax
    shr r8d, 6
    and r8b, 0x3F
    or r8b, 0x80
    mov byte [rdx + 1], r8b
    and al, 0x3F
    or al, 0x80
    mov byte [rdx + 2], al
    add rdx, 3
    jmp .lp
.done:
    mov byte [rdx], 0
    mov rax, rdx
    sub rax, r9
    ret
@endproc

;; @proc u64_u8_to_w
;; @abi win64_ms
;; @in RCX = UTF-8 src, RDX = UTF-16LE dst (2048B)
;; @out RAX = WCHAR count written (excluding NUL)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10
;; @desc Pure x86-64 UTF-8/WTF-8 to UTF-16LE transcoder.
@proc u64_u8_to_w
    mov r9, rdx
    lea r10, [rdx + 2036]
.lp:
    movzx eax, byte [rcx]
    test eax, eax
    jz .done
    cmp rdx, r10
    jae .done
    inc rcx
    cmp eax, 0x80
    jb .emit
    mov r8d, eax
    and r8d, 0xE0
    cmp r8d, 0xC0
    jne .chk3
    movzx r8d, byte [rcx]
    test r8d, r8d
    jz .emit
    inc rcx
    and eax, 0x1F
    shl eax, 6
    and r8d, 0x3F
    or eax, r8d
    jmp .emit
.chk3:
    mov r8d, eax
    and r8d, 0xF0
    cmp r8d, 0xE0
    jne .emit
    movzx r8d, byte [rcx]
    test r8d, r8d
    jz .emit
    and eax, 0x0F
    shl eax, 12
    and r8d, 0x3F
    shl r8d, 6
    or eax, r8d
    movzx r8d, byte [rcx + 1]
    test r8d, r8d
    jz .adv1
    add rcx, 2
    and r8d, 0x3F
    or eax, r8d
    jmp .emit
.adv1:
    inc rcx
.emit:
    mov word [rdx], ax
    add rdx, 2
    jmp .lp
.done:
    mov word [rdx], 0
    mov rax, rdx
    sub rax, r9
    shr rax, 1
    ret
@endproc

;; @proc u64_init
;; @abi win64_ms
;; @in none
;; @out RAX = pointer to command-line argument tail (after argv[0]) in g_u64_cmd
;; @err Terminates with exit code 1 if 512 KiB VirtualAlloc fails
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Initializes std handles, console detection, 512 KiB VirtualAlloc arena, and UTF-8 command line.
@proc_frame u64_init, 48, rbx
    mov ecx, -10
    call qword [rel __imp_GetStdHandle]
    mov qword [rel g_u64_stdin], rax

    mov ecx, -11
    call qword [rel __imp_GetStdHandle]
    mov qword [rel g_u64_stdout], rax

    mov ecx, -12
    call qword [rel __imp_GetStdHandle]
    mov qword [rel g_u64_stderr], rax

    mov rcx, qword [rel g_u64_stdout]
    lea rdx, [rel g_u64_io_done]
    call qword [rel __imp_GetConsoleMode]
    mov dword [rel g_u64_out_is_con], eax

    mov rcx, qword [rel g_u64_stdin]
    lea rdx, [rel g_u64_io_done]
    call qword [rel __imp_GetConsoleMode]
    mov dword [rel g_u64_in_is_con], eax

    xor ecx, ecx
    mov edx, 524288 ; 512 KiB
    mov r8d, 0x3000 ; MEM_COMMIT | MEM_RESERVE
    mov r9d, 4 ; PAGE_READWRITE
    call qword [rel __imp_VirtualAlloc]
    test rax, rax
    jnz .arena_ok
    mov ecx, 1
    call qword [rel __imp_ExitProcess]
.arena_ok:
    mov qword [rel g_u64_arena], rax

    call qword [rel __imp_GetCommandLineW]
    mov rcx, rax
    lea rdx, [rel g_u64_cmd]
    mov r8d, 4092
    call u64_w_to_u8

    lea rax, [rel g_u64_cmd]
.ws0:
    mov dl, byte [rax]
    cmp dl, 0x20
    je .adv0
    cmp dl, 0x09
    jne .tok0
.adv0:
    inc rax
    jmp .ws0
.tok0:
    cmp dl, '"'
    je .q0
.uq0:
    mov dl, byte [rax]
    test dl, dl
    jz .done
    cmp dl, 0x20
    je .done
    cmp dl, 0x09
    je .done
    inc rax
    jmp .uq0
.q0:
    inc rax
.qlp0:
    mov dl, byte [rax]
    test dl, dl
    jz .done
    inc rax
    cmp dl, '"'
    jne .qlp0
.done:
    @epilog_ret
@endproc

;; @proc u64_exit
;; @abi win64_ms
;; @in ECX = process exit code
;; @out Does not return
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Releases work arena and terminates via KERNEL32!ExitProcess.
@proc_frame u64_exit, 48, rbx
    mov ebx, ecx
    mov rcx, qword [rel g_u64_arena]
    test rcx, rcx
    jz .ex
    xor edx, edx
    mov r8d, 0x8000 ; MEM_RELEASE
    call qword [rel __imp_VirtualFree]
.ex:
    mov ecx, ebx
    call qword [rel __imp_ExitProcess]
    @epilog_ret
@endproc

;; @proc u64_write_handle
;; @abi win64_ms
;; @in RCX = handle, RDX = UTF-8 buffer, R8 = byte len, R9D = is_console (1 or 0)
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Writes UTF-8 bytes via WriteConsoleW (console) or WriteFile (pipes/files).
@proc_frame u64_write_handle, 64, rbx, rsi, rdi, r12, r13
    test r8, r8
    jz .done
    mov rbx, rcx ; handle
    mov rsi, rdx ; buf
    mov r12, r8 ; remaining
    test r9d, r9d
    jz .raw_file

.chunk_lp:
    test r12, r12
    jz .done
    mov r13, r12
    cmp r13, 512
    jbe .got_c
    mov r13d, 512
.got_c:
    lea rdi, [rel g_u64_path_tmp]
    xor eax, eax
.cp:
    cmp rax, r13
    jae .cp_dn
    mov cl, byte [rsi + rax]
    mov byte [rdi + rax], cl
    inc rax
    jmp .cp
.cp_dn:
    mov byte [rdi + rax], 0
    lea rcx, [rel g_u64_path_tmp]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    mov r8, rax
    mov rcx, rbx
    lea rdx, [rel g_u64_wbuf1]
    lea r9, [rel g_u64_io_done]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteConsoleW]
    add rsi, r13
    sub r12, r13
    jmp .chunk_lp

.raw_file:
    mov rcx, rbx
    mov rdx, rsi
    mov r8, r12
    lea r9, [rel g_u64_io_done]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]
.done:
    @epilog_ret
@endproc

;; @proc u64_write_out
;; @abi win64_ms
;; @in RCX = UTF-8 bytes, RDX = byte length
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Writes RDX UTF-8 bytes from RCX to standard output.
@proc_frame u64_write_out, 40
    mov r8, rdx
    mov rdx, rcx
    mov rcx, qword [rel g_u64_stdout]
    mov r9d, dword [rel g_u64_out_is_con]
    call u64_write_handle
    @epilog_ret
@endproc

;; @proc u64_write_str
;; @abi win64_ms
;; @in RCX = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Writes NUL-terminated UTF-8 string RCX to standard output.
@proc_frame u64_write_str, 48, rbx
    mov rbx, rcx
    call u64_strlen
    mov rcx, rbx
    mov rdx, rax
    call u64_write_out
    @epilog_ret
@endproc

;; @proc u64_write_err_str
;; @abi win64_ms
;; @in RCX = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Writes NUL-terminated UTF-8 string RCX to standard error.
@proc_frame u64_write_err_str, 48, rbx
    mov rbx, rcx
    call u64_strlen
    mov rcx, qword [rel g_u64_stderr]
    mov rdx, rbx
    mov r8, rax
    xor r9d, r9d
    call u64_write_handle
    @epilog_ret
@endproc

;; @proc u64_write_crlf
;; @abi win64_ms
;; @in none
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Writes CRLF to standard output.
@proc_frame u64_write_crlf, 40
    lea rcx, [rel szU64Crlf]
    mov edx, 2
    call u64_write_out
    @epilog_ret
@endproc

;; @proc u64_write_chr
;; @abi win64_ms
;; @in CL = single byte character
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Writes a single byte character CL to standard output.
@proc_frame u64_write_chr, 40
    mov byte [rel g_u64_num_buf], cl
    lea rcx, [rel g_u64_num_buf]
    mov edx, 1
    call u64_write_out
    @epilog_ret
@endproc

;; @proc u64_print_u64
;; @abi win64_ms
;; @in RCX = 64-bit unsigned integer
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Formats 64-bit unsigned integer RCX in base 10 to standard output.
@proc_frame u64_print_u64, 48, rdi
    lea rdi, [rel g_u64_num_buf + 48]
    mov byte [rdi], 0
    mov rax, rcx
    mov r8d, 10
.lp:
    dec rdi
    xor edx, edx
    div r8
    add dl, '0'
    mov byte [rdi], dl
    test rax, rax
    jnz .lp
    mov rcx, rdi
    call u64_write_str
    @epilog_ret
@endproc

;; @proc u64_print_hex8
;; @abi win64_ms
;; @in CL = 8-bit unsigned value
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints 2 uppercase hex digits for CL.
@proc_frame u64_print_hex8, 48, rbx
    movzx ebx, cl
    mov eax, ebx
    shr eax, 4
    cmp al, 10
    jb .d1
    add al, 7
.d1:
    add al, '0'
    mov cl, al
    call u64_write_chr
    mov eax, ebx
    and al, 0x0F
    cmp al, 10
    jb .d2
    add al, 7
.d2:
    add al, '0'
    mov cl, al
    call u64_write_chr
    @epilog_ret
@endproc

;; @proc u64_print_hex32
;; @abi win64_ms
;; @in ECX = 32-bit unsigned value
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints 8 uppercase hex digits for ECX.
@proc_frame u64_print_hex32, 48, rbx
    mov ebx, ecx
    mov ecx, ebx
    shr ecx, 24
    call u64_print_hex8
    mov ecx, ebx
    shr ecx, 16
    call u64_print_hex8
    mov ecx, ebx
    shr ecx, 8
    call u64_print_hex8
    mov ecx, ebx
    call u64_print_hex8
    @epilog_ret
@endproc

;; @proc u64_skip_ws
;; @abi win64_ms
;; @in RCX = cursor in command line
;; @out RAX = advanced cursor, DL = byte at [RAX]
;; @err none
;; @clobbers rax, rdx
;; @desc Advances past spaces and tabs.
@proc u64_skip_ws
    mov rax, rcx
.lp:
    mov dl, byte [rax]
    cmp dl, 0x20
    je .adv
    cmp dl, 0x09
    jne .dn
.adv:
    inc rax
    jmp .lp
.dn:
    ret
@endproc

;; @proc u64_read_tok
;; @abi win64_ms
;; @in RCX = cursor in command line, RDX = destination token buffer (1024B)
;; @out RAX = updated cursor after token, ECX = token length
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9
;; @desc Reads next whitespace-delimited or quoted token into RDX.
@proc u64_read_tok
    mov r8, rdx ; dst start
    mov rax, rcx
.ws:
    mov r9b, byte [rax]
    cmp r9b, 0x20
    je .ws_a
    cmp r9b, 0x09
    jne .tok
.ws_a:
    inc rax
    jmp .ws
.tok:
    test r9b, r9b
    jz .done
    xor ecx, ecx
.tlp:
    mov r9b, byte [rax]
    test r9b, r9b
    jz .done
    cmp r9b, 0x20
    je .done
    cmp r9b, 0x09
    je .done
    cmp r9b, '"'
    je .in_q
    cmp ecx, 1000
    jae .sk
    mov byte [rdx], r9b
    inc rdx
    inc ecx
.sk:
    inc rax
    jmp .tlp
.in_q:
    inc rax
.qlp:
    mov r9b, byte [rax]
    test r9b, r9b
    jz .done
    inc rax
    cmp r9b, '"'
    je .tlp
    cmp ecx, 1000
    jae .qlp
    mov byte [rdx], r9b
    inc rdx
    inc ecx
    jmp .qlp
.done:
    mov byte [rdx], 0
    mov rcx, rdx
    sub rcx, r8
    ret
@endproc

;; @proc u64_parse_u32
;; @abi win64_ms
;; @in RCX = ASCII decimal digit string
;; @out EAX = parsed u32 value, RDX = pointer to first non-digit
;; @err none
;; @clobbers rax, rdx, r8, r9
;; @desc Parses unsigned decimal integer from RCX.
@proc u64_parse_u32
    xor eax, eax
    mov rdx, rcx
.lp:
    movzx r8d, byte [rdx]
    cmp r8b, '0'
    jb .dn
    cmp r8b, '9'
    ja .dn
    sub r8b, '0'
    mov r9d, eax
    shl eax, 3
    add eax, r9d
    add eax, r9d
    add eax, r8d
    inc rdx
    jmp .lp
.dn:
    ret
@endproc

;; @proc u64_get_attr
;; @abi win64_ms
;; @in RCX = UTF-8 path
;; @out EAX = Win32 file attributes or 0xFFFFFFFF
;; @err EAX = 0xFFFFFFFF
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries file attributes via GetFileAttributesW.
@proc_frame u64_get_attr, 40
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    call qword [rel __imp_GetFileAttributesW]
    @epilog_ret
@endproc

;; @proc u64_set_attr
;; @abi win64_ms
;; @in RCX = UTF-8 path, EDX = Win32 attributes
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Sets file attributes via SetFileAttributesW.
@proc_frame u64_set_attr, 48, rbx
    mov ebx, edx
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    mov edx, ebx
    call qword [rel __imp_SetFileAttributesW]
    @epilog_ret
@endproc

;; @proc u64_copy_file
;; @abi win64_ms
;; @in RCX = UTF-8 src, RDX = UTF-8 dst, R8D = bFailIfExists
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Copies a file via CopyFileW.
@proc_frame u64_copy_file, 40, rbx, rsi
    mov rsi, rdx
    mov ebx, r8d
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    mov rcx, rsi
    lea rdx, [rel g_u64_wbuf2]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    lea rdx, [rel g_u64_wbuf2]
    mov r8d, ebx
    call qword [rel __imp_CopyFileW]
    @epilog_ret
@endproc

;; @proc u64_mkdir
;; @abi win64_ms
;; @in RCX = UTF-8 directory path
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Creates a directory via CreateDirectoryW.
@proc_frame u64_mkdir, 40
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    xor edx, edx
    call qword [rel __imp_CreateDirectoryW]
    @epilog_ret
@endproc

;; @proc u64_delete_file
;; @abi win64_ms
;; @in RCX = UTF-8 file path
;; @out EAX = non-zero on success, 0 on failure
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Deletes a file via DeleteFileW.
@proc_frame u64_delete_file, 40
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    call qword [rel __imp_DeleteFileW]
    @epilog_ret
@endproc

;; @proc u64_get_cwd
;; @abi win64_ms
;; @in RCX = UTF-8 dst buffer, RDX = capacity
;; @out RAX = UTF-8 byte length
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries current working directory in UTF-8.
@proc_frame u64_get_cwd, 40, rbx, rsi
    mov rsi, rcx
    mov rbx, rdx
    mov ecx, 1000
    lea rdx, [rel g_u64_wbuf1]
    call qword [rel __imp_GetCurrentDirectoryW]
    lea rcx, [rel g_u64_wbuf1]
    mov rdx, rsi
    mov r8, rbx
    call u64_w_to_u8
    @epilog_ret
@endproc

;; @proc u64_get_env
;; @abi win64_ms
;; @in RCX = UTF-8 variable name, RDX = UTF-8 dst, R8 = capacity
;; @out RAX = UTF-8 byte length (0 if not set)
;; @err RAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries environment variable via GetEnvironmentVariableW.
@proc_frame u64_get_env, 40, rbx, rsi
    mov rsi, rdx
    mov rbx, r8
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    lea rdx, [rel g_u64_wbuf2]
    mov r8d, 1000
    call qword [rel __imp_GetEnvironmentVariableW]
    test eax, eax
    jz .not_set
    lea rcx, [rel g_u64_wbuf2]
    mov rdx, rsi
    mov r8, rbx
    call u64_w_to_u8
    @epilog_ret
.not_set:
    mov byte [rsi], 0
    xor eax, eax
    @epilog_ret
@endproc

;; @proc u64_open_read
;; @abi win64_ms
;; @in RCX = UTF-8 file/dir path
;; @out RAX = Win64 handle or -1 (INVALID_HANDLE_VALUE)
;; @err RAX = -1
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Opens an existing file or directory handle for reading / metadata query.
@proc_frame u64_open_read, 56
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    mov edx, 0x80000000 ; GENERIC_READ
    mov r8d, 3 ; FILE_SHARE_READ | FILE_SHARE_WRITE
    xor r9d, r9d
    mov dword [rsp + 32], 3 ; OPEN_EXISTING
    mov dword [rsp + 40], 0x02000080 ; FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    @epilog_ret
@endproc

;; @proc u64_load_stream
;; @abi win64_ms
;; @in RCX = UTF-8 path (empty=stdin), RDX = dst buffer, R8 = max bytes, R9D = decode_bom (1 or 0)
;; @out RAX = bytes stored in RDX (NUL-terminated), CF=0 ok / CF=1 open error
;; @err CF=1 on open error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads file or stdin up to R8 bytes and normalizes UTF-8 / UTF-16LE / UTF-16BE BOMs.
@proc_frame u64_load_stream, 64, rbx, rsi, rdi, r12, r13, r14, r15
    mov rdi, rdx ; dst
    mov r12, r8 ; max_bytes
    mov r13d, r9d ; decode_bom
    xor r14d, r14d ; close_flag

    cmp byte [rcx], 0
    jne .open_f
    mov rbx, qword [rel g_u64_stdin]
    jmp .rd_init

.open_f:
    call u64_open_read
    cmp rax, -1
    je .fail
    mov rbx, rax
    mov r14d, 1

.rd_init:
    xor r15d, r15d ; total
.rd_lp:
    mov r8, r12
    sub r8, r15
    jz .eof
    cmp r8, 32768
    jbe .do_rd
    mov r8d, 32768
.do_rd:
    mov rcx, rbx
    lea rdx, [rdi + r15]
    lea r9, [rel g_u64_io_done]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    test eax, eax
    jz .eof
    mov eax, dword [rel g_u64_io_done]
    test eax, eax
    jz .eof
    add r15, rax
    jmp .rd_lp

.eof:
    test r14d, r14d
    jz .chk_bom
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]

.chk_bom:
    mov byte [rdi + r15], 0
    mov byte [rdi + r15 + 1], 0
    test r13d, r13d
    jz .ok_dn
    cmp r15, 2
    jb .ok_dn

    cmp r15, 3
    jb .chk_u16le
    cmp byte [rdi], 0xEF
    jne .chk_u16le
    cmp byte [rdi + 1], 0xBB
    jne .chk_u16le
    cmp byte [rdi + 2], 0xBF
    jne .chk_u16le
    sub r15, 3
    xor ecx, ecx
.sh_u8:
    cmp rcx, r15
    ja .ok_dn
    mov al, byte [rdi + rcx + 3]
    mov byte [rdi + rcx], al
    inc rcx
    jmp .sh_u8

.chk_u16le:
    cmp byte [rdi], 0xFF
    jne .chk_u16be
    cmp byte [rdi + 1], 0xFE
    jne .chk_u16be
    jmp .cvt_u16

.chk_u16be:
    cmp byte [rdi], 0xFE
    jne .ok_dn
    cmp byte [rdi + 1], 0xFF
    jne .ok_dn
    mov ecx, 2
.sw_be:
    lea rax, [rcx + 1]
    cmp rax, r15
    jae .cvt_u16
    mov al, byte [rdi + rcx]
    mov dl, byte [rdi + rcx + 1]
    mov byte [rdi + rcx], dl
    mov byte [rdi + rcx + 1], al
    add rcx, 2
    jmp .sw_be

.cvt_u16:
    mov rsi, qword [rel g_u64_arena]
    add rsi, 262144
    sub r15, 2
    xor ecx, ecx
.cp_w:
    cmp rcx, r15
    jae .cp_w_dn
    mov al, byte [rdi + rcx + 2]
    mov byte [rsi + rcx], al
    inc rcx
    jmp .cp_w
.cp_w_dn:
    mov word [rsi + rcx], 0
    mov rcx, rsi
    mov rdx, rdi
    mov r8, r12
    call u64_w_to_u8
    mov r15, rax

.ok_dn:
    mov rax, r15
    clc
    @epilog_ret
.fail:
    xor eax, eax
    stc
    @epilog_ret
@endproc

;; @proc u64_save_file
;; @abi win64_ms
;; @in RCX = UTF-8 path, RDX = buffer, R8 = byte length
;; @out EAX = 1 on success, 0 on error
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Creates/overwrites file RCX and writes R8 bytes from RDX.
@proc_frame u64_save_file, 64, rbx, rsi, rdi
    mov rsi, rdx
    mov rdi, r8
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    mov edx, 0x40000000 ; GENERIC_WRITE
    xor r8d, r8d
    xor r9d, r9d
    mov dword [rsp + 32], 2 ; CREATE_ALWAYS
    mov dword [rsp + 40], 0x80
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    cmp rax, -1
    je .fail
    mov rbx, rax
    mov rcx, rbx
    mov rdx, rsi
    mov r8, rdi
    lea r9, [rel g_u64_io_done]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    mov eax, 1
    @epilog_ret
.fail:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc u64_find_unpack
;; @abi win64_ms
;; @in RCX = WIN32_FIND_DATAW (592B), RDX = 544B normalized record (0:attr, 4:pad, 8:ft, 16:size64, 24:UTF-8 name[520])
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Unpacks WIN32_FIND_DATAW into a clean 64-bit aligned 544-byte record.
@proc_frame u64_find_unpack, 40, rsi, rdi
    mov rsi, rcx
    mov rdi, rdx
    mov eax, dword [rsi]
    mov dword [rdi], eax
    mov eax, dword [rsi + 20] ; ftLastWriteTime.low
    mov dword [rdi + 8], eax
    mov eax, dword [rsi + 24] ; ftLastWriteTime.high
    mov dword [rdi + 12], eax
    mov edx, dword [rsi + 28] ; nFileSizeHigh
    shl rdx, 32
    mov eax, dword [rsi + 32] ; nFileSizeLow
    or rax, rdx
    mov qword [rdi + 16], rax
    lea rcx, [rsi + 44]
    lea rdx, [rdi + 24]
    mov r8d, 516
    call u64_w_to_u8
    @epilog_ret
@endproc

;; @proc u64_find_first
;; @abi win64_ms
;; @in RCX = UTF-8 wildcard pattern, RDX = 544B normalized entry buffer
;; @out RAX = search handle (-1 if no match)
;; @err RAX = -1
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Starts directory enumeration via FindFirstFileW.
@proc_frame u64_find_first, 40, rbx, rdi
    mov rdi, rdx
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    lea rdx, [rel g_u64_find_raw]
    call qword [rel __imp_FindFirstFileW]
    cmp rax, -1
    je .dn
    mov rbx, rax
    lea rcx, [rel g_u64_find_raw]
    mov rdx, rdi
    call u64_find_unpack
    mov rax, rbx
.dn:
    @epilog_ret
@endproc

;; @proc u64_find_next
;; @abi win64_ms
;; @in RCX = search handle, RDX = 544B normalized entry buffer
;; @out EAX = non-zero on match, 0 when exhausted
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Advances directory enumeration via FindNextFileW.
@proc_frame u64_find_next, 40, rbx, rdi
    mov rdi, rdx
    lea rdx, [rel g_u64_find_raw]
    call qword [rel __imp_FindNextFileW]
    test eax, eax
    jz .dn
    mov ebx, eax
    lea rcx, [rel g_u64_find_raw]
    mov rdx, rdi
    call u64_find_unpack
    mov eax, ebx
.dn:
    @epilog_ret
@endproc

;; @proc u64_path_join
;; @abi win64_ms
;; @in RCX = dst buffer (1024B), RDX = dir UTF-8, R8 = leaf UTF-8
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10
;; @desc Joins directory RDX and leaf R8 into RCX with a single backslash separator.
@proc_frame u64_path_join, 40, rbx, rsi
    mov rbx, rcx
    mov rsi, r8
    call u64_strcpy
    test rax, rax
    jz .only_leaf
    mov dl, byte [rbx + rax - 1]
    cmp dl, 0x5C
    je .at_sep
    cmp dl, '/'
    je .at_sep
    cmp dl, ':'
    je .at_sep
    mov byte [rbx + rax], 0x5C
    inc rax
.at_sep:
    lea rcx, [rbx + rax]
    mov rdx, rsi
    call u64_strcpy
    @epilog_ret
.only_leaf:
    mov rcx, rbx
    mov rdx, rsi
    call u64_strcpy
    @epilog_ret
@endproc
