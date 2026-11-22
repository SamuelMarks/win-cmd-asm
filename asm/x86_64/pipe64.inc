;; @file pipe64.inc
;; @isa x64
;; @desc Left-to-Right FD 0..9 64-bit Redirection Engine & Concurrent CreatePipe Pipeline Executor.

@cpu x64
@format inc

;; @proc redir_fd_to_std_const64
;; @abi win64_ms
;; @in ECX = FD index (0, 1, or 2)
;; @out ECX = STD_INPUT_HANDLE (-10), STD_OUTPUT_HANDLE (-11), or STD_ERROR_HANDLE (-12)
;; @err none
;; @clobbers rcx
;; @desc Maps POSIX/cmd FD 0..2 onto Win64 GetStdHandle/SetStdHandle constants.
@proc redir_fd_to_std_const64
    cmp ecx, 0
    je .in0
    cmp ecx, 1
    je .out1
    mov ecx, STD_ERROR_HANDLE
    ret
.in0:
    mov ecx, STD_INPUT_HANDLE
    ret
.out1:
    mov ecx, STD_OUTPUT_HANDLE
    ret
@endproc

;; @proc redir_set_non_inheritable64
;; @abi win64_ms
;; @in RCX = Win64 HANDLE
;; @out RAX = non-inheritable Win64 HANDLE (replaced via DuplicateHandle(DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS, FALSE))
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Replaces a pipe handle with a non-inheritable duplicate so downstream read handles do not leak into upstream children.
@proc_frame redir_set_non_inheritable64, 80, rbx
    mov rbx, rcx
    call qword [rel __imp_GetCurrentProcess]
    mov qword [rsp + 64], INVALID_HANDLE_VALUE
    mov rcx, rax
    mov rdx, rbx
    mov r8, rax
    lea r9, [rsp + 64]
    mov dword [rsp + 32], 0
    mov dword [rsp + 40], 0
    mov dword [rsp + 48], DUPLICATE_CLOSE_SOURCE + DUPLICATE_SAME_ACCESS
    call qword [rel __imp_DuplicateHandle]
    mov rax, qword [rsp + 64]
    @epilog_ret
@endproc

;; @proc redir_apply64
;; @abi win64_ms
;; @in RCX = AST_REDIR64* linked list head, RDX = 160-byte save buffer (10 orig QWORD handles + 10 owned QWORD handles)
;; @out CF=0 on success; CF=1, EAX=ERR_* on open failure (restoring any partial changes!)
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Evaluates FD 0..9 redirections strictly left-to-right (<, >, >>, n>&m, n<&m) with character device support.
@proc_frame redir_apply64, 168, rbp, rbx, rsi, rdi, r12, r13
    ; 6 saved regs (even) -> 168 = 40 + 8*16
    ; [rsp + 64..143]  : working FD 0..9 QWORD handles (80B)
    ; [rsp + 144..159] : scratch newPos / dupTarget
    mov rsi, rcx
    mov rbp, rdx

    xor ecx, ecx
.init_lp:
    mov qword [rcx * 8 + rbp], INVALID_HANDLE_VALUE
    mov qword [rcx * 8 + rbp + 80], 0
    inc ecx
    cmp ecx, 10
    jb .init_lp

    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp], rax
    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + 8], rax
    mov ecx, STD_ERROR_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + 16], rax

    xor ecx, ecx
.cp_work:
    mov rax, qword [rcx * 8 + rbp]
    mov qword [rsp + 64 + rcx * 8], rax
    inc ecx
    cmp ecx, 10
    jb .cp_work

.node_lp:
    test rsi, rsi
    jz .done_ok

    movzx ebx, byte [rsi + AST_REDIR64.bSrcFd]
    cmp ebx, 9
    ja .next_node

    mov al, byte [rsi + AST_REDIR64.bKind]
    cmp al, REDIR_DUP_OUT
    je .do_dup
    cmp al, REDIR_DUP_IN
    je .do_dup

    mov rcx, qword [rsi + AST_REDIR64.pTargetStr]
    call expand_phase3_64
    jc .err_unwind
    mov rcx, rax
    call expand_phase4_64
    jc .err_unwind
    mov rdi, rax

    mov rcx, rdi
    call dev_match_name64
    test rax, rax
    jz .not_dev_target
    mov rdi, rax
    cmp dword [rdi], 0x004E4F43 ; "CON\0"
    jne .not_dev_target
    cmp byte [rsi + AST_REDIR64.bKind], REDIR_IN
    jne .con_out
    lea rdi, [rel szDevConIn64]
    jmp .not_dev_target
.con_out:
    lea rdi, [rel szDevConOut64]

.not_dev_target:
    mov al, byte [rsi + AST_REDIR64.bKind]
    cmp al, REDIR_IN
    je .open_in
    cmp al, REDIR_OUT_APPEND
    je .open_app

    mov rcx, rdi
    mov edx, GENERIC_WRITE
    mov r8d, CREATE_ALWAYS
    mov r9d, 1
    call hal_open_file64
    jc .err_unwind
    mov r12, rax
    jmp .install_owned

.open_in:
    mov rcx, rdi
    mov edx, GENERIC_READ
    mov r8d, OPEN_EXISTING
    mov r9d, 1
    call hal_open_file64
    jc .err_unwind
    mov r12, rax
    jmp .install_owned

.open_app:
    mov rcx, rdi
    mov edx, GENERIC_WRITE
    mov r8d, OPEN_ALWAYS
    mov r9d, 1
    call hal_open_file64
    jc .err_unwind
    mov r12, rax
    mov rcx, r12
    xor edx, edx
    lea r8, [rsp + 144]
    mov r9d, FILE_END
    call qword [rel __imp_SetFilePointerEx]

.install_owned:
    mov rax, qword [rbx * 8 + rbp + 80]
    test rax, rax
    jz .save_new_h
    cmp rax, INVALID_HANDLE_VALUE
    je .save_new_h
    mov rcx, rax
    call qword [rel __imp_CloseHandle]
.save_new_h:
    mov qword [rsp + 64 + rbx * 8], r12
    mov qword [rbx * 8 + rbp + 80], r12
    cmp ebx, 2
    ja .next_node
    mov ecx, ebx
    call redir_fd_to_std_const64
    mov rdx, r12
    call qword [rel __imp_SetStdHandle]
    jmp .next_node

.do_dup:
    movzx ecx, byte [rsi + AST_REDIR64.bTargetFd]
    cmp ecx, 9
    ja .next_node
    mov r13, qword [rsp + 64 + rcx * 8]
    cmp r13, INVALID_HANDLE_VALUE
    je .next_node

    call qword [rel __imp_GetCurrentProcess]
    mov qword [rsp + 144], INVALID_HANDLE_VALUE
    mov rcx, rax
    mov rdx, r13
    mov r8, rax
    lea r9, [rsp + 144]
    mov dword [rsp + 32], 0
    mov dword [rsp + 40], 1 ; bInheritHandle = TRUE
    mov dword [rsp + 48], DUPLICATE_SAME_ACCESS
    call qword [rel __imp_DuplicateHandle]
    test eax, eax
    jz .next_node
    mov r12, qword [rsp + 144]
    jmp .install_owned

.next_node:
    mov rsi, qword [rsi + AST_REDIR64.pNext]
    jmp .node_lp

.done_ok:
    clc
    @epilog_ret

.err_unwind:
    mov ebx, eax
    mov rcx, rbp
    call redir_restore64
    mov eax, ebx
    stc
    @epilog_ret
@endproc

;; @proc redir_restore64
;; @abi win64_ms
;; @in RCX = 160-byte save buffer populated by redir_apply64
;; @out Restores STD_INPUT/OUTPUT/ERROR handles and closes all temporary owned handles
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Restores original Win64 standard handles and closes newly opened redirection handles.
@proc_frame redir_restore64, 40, rbx, rdi
    mov rdi, rcx

    mov ecx, STD_INPUT_HANDLE
    mov rdx, qword [rdi]
    call qword [rel __imp_SetStdHandle]

    mov ecx, STD_OUTPUT_HANDLE
    mov rdx, qword [rdi + 8]
    call qword [rel __imp_SetStdHandle]

    mov ecx, STD_ERROR_HANDLE
    mov rdx, qword [rdi + 16]
    call qword [rel __imp_SetStdHandle]

    xor ebx, ebx
.close_lp:
    mov rcx, qword [rdi + 80 + rbx * 8]
    test rcx, rcx
    jz .nx
    cmp rcx, INVALID_HANDLE_VALUE
    je .nx
    call qword [rel __imp_CloseHandle]
    mov qword [rdi + 80 + rbx * 8], 0
.nx:
    inc ebx
    cmp ebx, 10
    jb .close_lp

    @epilog_ret
@endproc

;; @proc pipe_node_cmd_text64
;; @abi win64_ms
;; @in RCX = AST_NODE64* pipeline stage
;; @out RAX = command string suitable for `"%COMSPEC%" /D /C "<str>"`, CF=0
;; @err none
;; @clobbers rax
;; @desc Extracts raw command text for an x86-64 pipeline stage node.
@proc pipe_node_cmd_text64
    mov al, byte [rcx + AST_NODE64.bKind]
    cmp al, AST_SIMPLE_CMD
    je .simple
    cmp al, AST_BINARY
    je .bin
    mov rax, qword [rcx + AST_NODE64.pStr1]
    test rax, rax
    jnz .ok
    lea rax, [rel szEmpty64]
    clc
    ret
.simple:
    mov rax, qword [rcx + AST_NODE64.pStr2]
    test rax, rax
    jnz .ok
    mov rax, qword [rcx + AST_NODE64.pStr1]
.ok:
    clc
    ret
.bin:
    mov rax, qword [rcx + AST_NODE64.pStr1]
    clc
    ret
@endproc

;; @proc pipe_spawn_stage64
;; @abi win64_ms
;; @in RCX = stage command string, RDX = hStdIn, R8 = hStdOut, R9 = out PROCESS_INFORMATION64* (24B)
;; @out CF=0 on success; CF=1, EAX=ERR_* on CreateProcessW failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Spawns `"<self.exe>" [--nt52-api-cap] /D /C "<stage>"` with inheritable 64-bit pipe handles.
@proc_frame pipe_spawn_stage64, 216, rbp, rbx, rsi, rdi, r12, r13
    ; 6 saved regs (even) -> 216 = 40 + 11*16
    ; [rsp + 32..79]  : 10-arg CreateProcessW stack space
    ; [rsp + 88..191] : STARTUPINFOW64 (104 bytes, rbp)
    mov rsi, rcx ; stage cmd
    mov rbx, rdx ; hStdIn
    mov r12, r8  ; hStdOut
    mov r13, r9  ; out PI*
    lea rbp, [rsp + 88]

    mov ecx, W64_LINE_MAX
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov byte [rdi], '"'
    lea rcx, [rdi + 1]
    lea rdx, [rel g_self_exe]
    call str_copy64
    mov byte [rax], '"'
    inc rax
    cmp byte [rel g_sim_nt52], 0
    je .no_sim52
    mov rcx, rax
    lea rdx, [rel szPipeSim52Arg64]
    call str_copy64
.no_sim52:
    cmp byte [rel g_delayed_exp], 0
    je .no_v_on
    mov rcx, rax
    lea rdx, [rel szPipeDelayArg64]
    call str_copy64
.no_v_on:
    mov rcx, rax
    lea rdx, [rel szPipeSubArgs64]
    call str_copy64
    mov rcx, rax
    mov rdx, rsi
    call str_copy64
    mov word [rax], 0x0022 ; closing '"' + NUL!
    mov rsi, rdi ; full cmdline WTF-8

    ; Zero STARTUPINFOW64 (104 bytes = 13 qwords)
    mov rdi, rbp
    xor eax, eax
    mov ecx, 13
    rep stosq

    mov dword [rbp + STARTUPINFOW64.cb], 104
    mov dword [rbp + STARTUPINFOW64.dwFlags], STARTF_USESTDHANDLES
    mov qword [rbp + STARTUPINFOW64.hStdInput], rbx
    mov qword [rbp + STARTUPINFOW64.hStdOutput], r12
    mov ecx, STD_ERROR_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + STARTUPINFOW64.hStdError], rax

    mov qword [r13], 0
    mov qword [r13 + 8], 0
    mov qword [r13 + 16], 0

    mov rcx, rsi
    call wtf8_to_utf16le64
    jc .fail
    mov rbx, rax
    call env_build_unicode_block64
    jc .fail

    xor ecx, ecx
    mov rdx, rbx
    xor r8d, r8d
    xor r9d, r9d
    mov dword [rsp + 32], 1 ; bInheritHandles = TRUE
    mov dword [rsp + 40], CREATE_UNICODE_ENVIRONMENT
    mov qword [rsp + 48], rax
    mov qword [rsp + 56], 0
    mov qword [rsp + 64], rbp
    mov qword [rsp + 72], r13
    call qword [rel __imp_CreateProcessW]
    test eax, eax
    jz .win_err
    clc
    @epilog_ret

.win_err:
    call qword [rel __imp_GetLastError]
    call map_win32_to_err64
.fail:
    stc
    @epilog_ret
@endproc

;; @proc exec_pipeline64
;; @abi win64_ms
;; @in RCX = root AST_BINARY (BINOP_PIPE) node
;; @out EAX = exit code of final pipeline stage (also stored in g_last_exit_code), CF=0
;; @err CF=1, EAX=ERR_* on pipe/process creation failure
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Executes N-stage concurrent 64-bit pipelines via CreatePipe(64 KiB) with non-inheritable parent ends.
@proc_frame exec_pipeline64, 520, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 520 = 40 + 30*16
    ; [rbp + 0..127]   : stage AST_NODE64* array (up to 16 stages, 128B)
    ; [rbp + 128..255] : spawned hProcess array (up to 16 QWORDs, 128B)
    ; [rbp + 256..383] : work stack for iterative flatten (128B)
    ; [rbp + 384..407] : SECURITY_ATTRIBUTES64 (24B)
    ; [rbp + 408..431] : scratch PROCESS_INFORMATION64 (24B)
    ; [rbp + 432]      : pipe_hRead (qword)
    ; [rbp + 440]      : pipe_hWrite (qword)
    ; [rbp + 448]      : orig_stdout (qword)
    ; [rbp + 456]      : dup scratch / exitcode scratch (qword)
    lea rbp, [rsp + 56]
    mov rdi, rcx
    xor r12d, r12d ; stage_count
    xor edx, edx   ; work_sp

.flat_descend:
    cmp byte [rdi + AST_NODE64.bKind], AST_BINARY
    jne .flat_leaf
    cmp byte [rdi + AST_NODE64.bSub], BINOP_PIPE
    jne .flat_leaf
    cmp edx, 15
    jae .flat_leaf
    mov rax, qword [rdi + AST_NODE64.pChild2]
    mov qword [rbp + 256 + rdx * 8], rax
    inc edx
    mov rdi, qword [rdi + AST_NODE64.pChild1]
    jmp .flat_descend

.flat_leaf:
    cmp r12d, 16
    jae .flat_pop
    mov qword [rbp + r12 * 8], rdi
    inc r12d
.flat_pop:
    test edx, edx
    jz .flat_done
    dec edx
    mov rdi, qword [rbp + 256 + rdx * 8]
    jmp .flat_descend

.flat_done:
    xor r13d, r13d ; spawned_count

    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov r14, rax ; prev_read_handle

    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + 448], rax

    xor esi, esi ; stage idx i
.stage_lp:
    cmp esi, r12d
    jae .wait_all

    lea eax, [esi + 1]
    cmp eax, r12d
    jae .last_stage

    mov dword [rbp + 384 + SECURITY_ATTRIBUTES64.nLength], 24
    mov dword [rbp + 384 + SECURITY_ATTRIBUTES64.dwPad0], 0
    mov qword [rbp + 384 + SECURITY_ATTRIBUTES64.lpSecurityDescriptor], 0
    mov dword [rbp + 384 + SECURITY_ATTRIBUTES64.bInheritHandle], 1
    mov dword [rbp + 384 + SECURITY_ATTRIBUTES64.dwPad1], 0

    lea rcx, [rbp + 432] ; &hRead
    lea rdx, [rbp + 440] ; &hWrite
    lea r8, [rbp + 384]  ; &SA
    mov r9d, 65536
    call qword [rel __imp_CreatePipe]
    test eax, eax
    jz .pipe_fail

    mov rcx, qword [rbp + 432]
    call redir_set_non_inheritable64
    mov qword [rbp + 432], rax
    mov r15, qword [rbp + 440] ; stage_out
    jmp .do_spawn

.last_stage:
    mov qword [rbp + 432], INVALID_HANDLE_VALUE
    mov qword [rbp + 440], INVALID_HANDLE_VALUE
    mov r15, qword [rbp + 448]

.do_spawn:
    test esi, esi
    jz .in_ready
    call qword [rel __imp_GetCurrentProcess]
    mov qword [rbp + 456], INVALID_HANDLE_VALUE
    mov rcx, rax
    mov rdx, r14
    mov r8, rax
    lea r9, [rbp + 456]
    mov dword [rsp + 32], 0
    mov dword [rsp + 40], 1 ; bInheritHandle = TRUE
    mov dword [rsp + 48], DUPLICATE_CLOSE_SOURCE + DUPLICATE_SAME_ACCESS
    call qword [rel __imp_DuplicateHandle]
    mov r14, qword [rbp + 456]

.in_ready:
    mov rcx, qword [rbp + rsi * 8]
    call pipe_node_cmd_text64
    mov rcx, rax
    mov rdx, r14
    mov r8, r15
    lea r9, [rbp + 408]
    call pipe_spawn_stage64
    jc .spawn_cleanup

    mov rcx, qword [rbp + 408 + PROCESS_INFORMATION64.hThread]
    call qword [rel __imp_CloseHandle]
    mov rax, qword [rbp + 408 + PROCESS_INFORMATION64.hProcess]
    mov qword [rbp + 128 + r13 * 8], rax
    inc r13d

.spawn_cleanup:
    test esi, esi
    jz .chk_close_wr
    mov rcx, r14
    call qword [rel __imp_CloseHandle]
.chk_close_wr:
    mov rcx, qword [rbp + 440]
    cmp rcx, INVALID_HANDLE_VALUE
    je .advance_stage
    call qword [rel __imp_CloseHandle]
.advance_stage:
    mov r14, qword [rbp + 432]
    inc esi
    jmp .stage_lp

.wait_all:
    test r13d, r13d
    jz .pipe_fail

    mov ecx, r13d
    lea rdx, [rbp + 128]
    mov r8d, 1
    mov r9d, INFINITE_WAIT
    call qword [rel __imp_WaitForMultipleObjects]

    lea ebx, [r13d - 1]
    mov dword [rbp + 456], 0
    mov rcx, qword [rbp + 128 + rbx * 8]
    lea rdx, [rbp + 456]
    call qword [rel __imp_GetExitCodeProcess]
    mov edi, dword [rbp + 456]

    xor ebx, ebx
.close_procs:
    cmp ebx, r13d
    jae .done_ok
    mov rcx, qword [rbp + 128 + rbx * 8]
    call qword [rel __imp_CloseHandle]
    inc ebx
    jmp .close_procs

.done_ok:
    mov eax, edi
    mov dword [rel g_last_exit_code], eax
    clc
    @epilog_ret

.pipe_fail:
    mov eax, 1
    mov dword [rel g_last_exit_code], eax
    stc
    @epilog_ret
@endproc
