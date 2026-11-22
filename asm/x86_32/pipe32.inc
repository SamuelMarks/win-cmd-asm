;; @file pipe32.inc
;; @isa 386
;; @desc Left-to-Right FD 0..9 Redirection Engine & Deadlock-Free Concurrent CreatePipe Pipeline Executor.

@cpu 386
@format inc

;; @proc redir_fd_to_std_const32
;; @abi win32
;; @in EAX = FD index (0, 1, or 2)
;; @out EAX = STD_INPUT_HANDLE (-10), STD_OUTPUT_HANDLE (-11), or STD_ERROR_HANDLE (-12)
;; @err none
;; @clobbers eax
;; @desc Maps POSIX/cmd FD 0..2 onto Win32 GetStdHandle/SetStdHandle constants.
@proc redir_fd_to_std_const32
    cmp eax, 0
    je .in0
    cmp eax, 1
    je .out1
    mov eax, STD_ERROR_HANDLE
    ret
.in0:
    mov eax, STD_INPUT_HANDLE
    ret
.out1:
    mov eax, STD_OUTPUT_HANDLE
    ret
@endproc

;; @proc redir_set_non_inheritable32
;; @abi win32
;; @in EBX = Win32 HANDLE
;; @out EAX = non-inheritable Win32 HANDLE (either mutated via SetHandleInformation or replaced via DuplicateHandle)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Clears HANDLE_FLAG_INHERIT (NT 3.51+/Win95+) with DuplicateHandle fallback for Windows NT 3.1 RTM!
@proc redir_set_non_inheritable32
    cmp dword [pfnSetHandleInfo_p12], 0
    je .nt31_dup
    push 0
    push HANDLE_FLAG_INHERIT
    push ebx
    call dword [pfnSetHandleInfo_p12]
    test eax, eax
    jz .nt31_dup
    mov eax, ebx
    ret

.nt31_dup:
    call dword [__imp__GetCurrentProcess@0]
    mov ecx, eax
    sub esp, 4
    mov edx, esp
    push DUPLICATE_CLOSE_SOURCE + DUPLICATE_SAME_ACCESS
    push 0 ; bInheritHandle = FALSE
    push 0
    push edx
    push ecx
    push ebx
    push ecx
    call dword [__imp__DuplicateHandle@28]
    pop eax
    ret
@endproc

;; @proc redir_apply32
;; @abi win32
;; @in ESI = AST_REDIR32* linked list head, EDI = 80-byte save buffer (10 original handles + 10 owned flags)
;; @out CF=0 on success; CF=1, EAX=ERR_* on open failure (restoring any partial changes!)
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Evaluates FD 0..9 redirections strictly left-to-right (<, >, >>, n>&m, n<&m) with character device support.
@proc redir_apply32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, edi

    ; Snapshot current handles 0..2 and zero owned flags [ebp+40..80]
    xor ecx, ecx
.init_lp:
    mov dword [ebp + ecx * 4], INVALID_HANDLE_VALUE
    mov dword [ebp + 40 + ecx * 4], 0
    inc ecx
    cmp ecx, 10
    jb .init_lp

    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp], eax
    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + 4], eax
    push STD_ERROR_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + 8], eax

    ; Active working FD table on stack [esp..esp+40] initialized from [ebp..ebp+40]
    sub esp, 40
    xor ecx, ecx
.cp_work:
    mov eax, dword [ebp + ecx * 4]
    mov dword [esp + ecx * 4], eax
    inc ecx
    cmp ecx, 10
    jb .cp_work

.node_lp:
    test esi, esi
    jz .done_ok

    movzx ebx, byte [esi + AST_REDIR32.bSrcFd]
    cmp ebx, 9
    ja .next_node

    mov al, byte [esi + AST_REDIR32.bKind]
    cmp al, REDIR_DUP_OUT
    je .do_dup
    cmp al, REDIR_DUP_IN
    je .do_dup

    ; Expand Phase 3 & Phase 4 on target filename!
    push esi
    mov esi, dword [esi + AST_REDIR32.pTargetStr]
    call expand_phase3_32
    jc .pop_err
    mov esi, eax
    call expand_phase4_32
    pop esi
    jc .err_unwind

    ; Resolve reserved character devices (NUL, CON, CONIN$, CONOUT$, PRN, AUX, COM1..9, LPT1..9)
    mov edi, eax
    push esi
    mov esi, edi
    call dev_match_name32
    pop esi
    test eax, eax
    jz .not_dev_target
    mov edi, eax
    ; Map bare "CON" to CONIN$ for REDIR_IN, CONOUT$ for REDIR_OUT/APPEND!
    cmp dword [edi], 0x004E4F43 ; "CON\0"
    jne .not_dev_target
    cmp byte [esi + AST_REDIR32.bKind], REDIR_IN
    jne .con_out
    mov edi, addr(szDevConIn)
    jmp .not_dev_target
.con_out:
    mov edi, addr(szDevConOut)

.not_dev_target:
    mov al, byte [esi + AST_REDIR32.bKind]
    cmp al, REDIR_IN
    je .open_in
    cmp al, REDIR_OUT_APPEND
    je .open_app

    ; REDIR_OUT ('>'): GENERIC_WRITE, CREATE_ALWAYS, inheritable = 1
    push esi
    mov esi, edi
    mov ecx, GENERIC_WRITE
    mov edx, CREATE_ALWAYS
    mov eax, 1
    call hal_open_file32
    pop esi
    jc .err_unwind
    jmp .install_owned

.open_in:
    push esi
    mov esi, edi
    mov ecx, GENERIC_READ
    mov edx, OPEN_EXISTING
    mov eax, 1
    call hal_open_file32
    pop esi
    jc .err_unwind
    jmp .install_owned

.open_app:
    push esi
    mov esi, edi
    mov ecx, GENERIC_WRITE
    mov edx, OPEN_ALWAYS
    mov eax, 1
    call hal_open_file32
    pop esi
    jc .err_unwind
    push eax
    push FILE_END
    push 0
    push 0
    push eax
    call dword [__imp__SetFilePointer@16]
    pop eax

.install_owned:
    ; Close any previously opened owned handle on this FD before replacing
    mov edx, eax
    cmp dword [ebp + 40 + ebx * 4], 0
    je .save_new_h
    push edx
    push dword [esp + 4 + ebx * 4]
    call dword [__imp__CloseHandle@4]
    pop edx
.save_new_h:
    mov dword [esp + ebx * 4], edx
    mov dword [ebp + 40 + ebx * 4], edx
    cmp ebx, 2
    ja .next_node
    mov eax, ebx
    call redir_fd_to_std_const32
    push edx
    push eax
    call dword [__imp__SetStdHandle@8]
    jmp .next_node

.do_dup:
    movzx ecx, byte [esi + AST_REDIR32.bTargetFd]
    cmp ecx, 9
    ja .next_node
    mov edx, dword [esp + ecx * 4]
    cmp edx, INVALID_HANDLE_VALUE
    je .next_node

    ; DuplicateHandle with bInheritHandle = TRUE so child processes inherit 2>&1!
    push esi
    call dword [__imp__GetCurrentProcess@0]
    mov ecx, eax
    sub esp, 4
    mov edi, esp
    push DUPLICATE_SAME_ACCESS
    push 1 ; bInheritHandle = TRUE
    push 0
    push edi
    push ecx
    push edx
    push ecx
    call dword [__imp__DuplicateHandle@28]
    mov edx, dword [esp]
    add esp, 4
    pop esi
    test eax, eax
    jz .next_node
    jmp .install_owned

.next_node:
    mov esi, dword [esi + AST_REDIR32.pNext]
    jmp .node_lp

.done_ok:
    add esp, 40
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.pop_err:
    pop esi
.err_unwind:
    mov ebx, eax
    add esp, 40
    mov edi, ebp
    call redir_restore32
    mov eax, ebx
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc redir_restore32
;; @abi win32
;; @in EDI = 80-byte save buffer populated by redir_apply32
;; @out Restores STD_INPUT/OUTPUT/ERROR handles and closes all temporary owned handles
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Restores original Win32 standard handles and closes newly opened redirection handles.
@proc redir_restore32
    push ebx
    push edi

    push dword [edi]
    push STD_INPUT_HANDLE
    call dword [__imp__SetStdHandle@8]

    push dword [edi + 4]
    push STD_OUTPUT_HANDLE
    call dword [__imp__SetStdHandle@8]

    push dword [edi + 8]
    push STD_ERROR_HANDLE
    call dword [__imp__SetStdHandle@8]

    xor ebx, ebx
.close_lp:
    mov eax, dword [edi + 40 + ebx * 4]
    test eax, eax
    jz .nx
    cmp eax, INVALID_HANDLE_VALUE
    je .nx
    push eax
    call dword [__imp__CloseHandle@4]
    mov dword [edi + 40 + ebx * 4], 0
.nx:
    inc ebx
    cmp ebx, 10
    jb .close_lp

    pop edi
    pop ebx
    ret
@endproc

;; @proc pipe_node_cmd_text32
;; @abi win32
;; @in ESI = AST_NODE32* pipeline stage
;; @out EAX = command string suitable for `"%COMSPEC%" /S /D /C "<str>"`, CF=0
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Extracts raw command text for a pipeline stage node.
@proc pipe_node_cmd_text32
    mov al, byte [esi + AST_NODE32.bKind]
    cmp al, AST_SIMPLE_CMD
    je .simple
    cmp al, AST_BINARY
    je .bin
    ; For AST_BLOCK / IF / FOR, use pStr1 or fallback empty
    mov eax, dword [esi + AST_NODE32.pStr1]
    test eax, eax
    jnz .ok
    mov eax, addr(szEmpty32)
    clc
    ret
.simple:
    mov eax, dword [esi + AST_NODE32.pStr2]
    test eax, eax
    jnz .ok
    mov eax, dword [esi + AST_NODE32.pStr1]
.ok:
    clc
    ret
.bin:
    mov eax, dword [esi + AST_NODE32.pStr1]
    clc
    ret
@endproc

;; @proc pipe_spawn_stage32
;; @abi win32
;; @in ESI = stage command string, EBX = hStdIn, EDI = hStdOut, EDX = out PROCESS_INFORMATION* (16B)
;; @out CF=0 on success; CF=1, EAX=ERR_* on CreateProcess failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Spawns `"<self.exe>" [--win9x-ansi-sim] /D /C <stage>` with inheritable pipe handles.
@proc pipe_spawn_stage32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 88 ; STARTUPINFO (68B) + saved PI ptr (4B) + env ptr (4B) + pad
    mov ebp, esp
    mov dword [ebp + 72], edx ; out PI*

    ; Build command line: `"<g_self_exe>" [--win9x-ansi-sim] /D /C <ESI>`
    mov ecx, W32_LINE_MAX
    call arena_alloc32
    jc .fail
    mov edx, eax
    push edi ; save hStdOut
    mov edi, edx
    mov byte [edi], '"'
    inc edi
    push esi
    mov esi, addr(g_self_exe)
    call str_copy32
    mov edi, eax
    mov byte [edi], '"'
    inc edi
    cmp byte [g_sim_win9x], 0
    je .no_sim9x
    mov esi, addr(szPipeSim9xArg)
    call str_copy32
    mov edi, eax
.no_sim9x:
    mov esi, addr(szPipeSubArgs)
    call str_copy32
    mov edi, eax
    pop esi
    call str_copy32
    mov word [eax], 0x0022 ; closing '"' + NUL!
    pop ecx ; hStdOut -> ECX

    ; Zero STARTUPINFO (68 bytes = 17 dwords)
    push edx ; cmdline
    push ecx ; hStdOut
    mov edi, ebp
    xor eax, eax
    mov ecx, 17
    rep stosd
    pop ecx ; hStdOut
    pop esi ; cmdline

    mov dword [ebp + STARTUPINFOW.cb], 68
    mov dword [ebp + STARTUPINFOW.dwFlags], STARTF_USESTDHANDLES
    mov dword [ebp + STARTUPINFOW.hStdInput], ebx
    mov dword [ebp + STARTUPINFOW.hStdOutput], ecx
    push esi
    push STD_ERROR_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + STARTUPINFOW.hStdError], eax
    pop esi

    ; Zero caller PROCESS_INFORMATION (16B)
    mov edi, dword [ebp + 72]
    mov dword [edi], 0
    mov dword [edi + 4], 0
    mov dword [edi + 8], 0
    mov dword [edi + 12], 0

    cmp byte [g_is_win9x], 0
    jne .spawn_ansi

    call wtf8_to_utf16le
    jc .fail
    mov ebx, eax
    call env_build_unicode_block32
    jc .fail

    push dword [ebp + 72] ; lpProcessInformation
    push ebp              ; lpStartupInfo
    push 0                ; lpCurrentDirectory (inherit current)
    push eax              ; lpEnvironment (UTF-16LE!)
    push CREATE_UNICODE_ENVIRONMENT
    push 1                ; bInheritHandles = TRUE
    push 0
    push 0
    push ebx              ; lpCommandLine
    push 0                ; lpApplicationName
    call dword [__imp__CreateProcessW@40]
    test eax, eax
    jz .win_err
    jmp .ok

.spawn_ansi:
    push dword [ebp + 72]
    push ebp
    push 0
    push dword [g_env_buf]
    push 0
    push 1 ; bInheritHandles = TRUE
    push 0
    push 0
    push esi
    push 0
    call dword [__imp__CreateProcessA@40]
    test eax, eax
    jz .win_err

.ok:
    add esp, 88
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.win_err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
.fail:
    add esp, 88
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc exec_pipeline32
;; @abi win32
;; @in ESI = root AST_BINARY (BINOP_PIPE) node
;; @out EAX = exit code of final pipeline stage (also stored in g_last_exit_code), CF=0
;; @err CF=1, EAX=ERR_* on pipe/process creation failure
;; @clobbers eax, ecx, edx
;; @desc Executes N-stage concurrent pipelines via CreatePipe(64 KiB) with non-inheritable parent ends.
@proc exec_pipeline32
    push ebx
    push esi
    push edi
    push ebp
    ; Stack layout (368 bytes):
    ; [ebp + 0..64]    : stage AST_NODE32* array (up to 16 stages)
    ; [ebp + 64..128]  : spawned hProcess array (up to 16 handles)
    ; [ebp + 128..144] : SECURITY_ATTRIBUTES32 (12B) + pad
    ; [ebp + 144..160] : scratch PROCESS_INFORMATION (16B)
    ; [ebp + 160]      : stage_count
    ; [ebp + 164]      : spawned_count
    ; [ebp + 168]      : prev_read_handle
    ; [ebp + 172]      : orig_stdin
    ; [ebp + 176]      : orig_stdout
    ; [ebp + 180]      : pipe_hRead
    ; [ebp + 184]      : pipe_hWrite
    sub esp, 200
    mov ebp, esp

    ; Flatten left-leaning / right-leaning BINOP_PIPE chain into [ebp + 0..64]
    xor ecx, ecx ; count
    mov edi, esi
    ; Use iterative stack walk to collect pipe leaves in left-to-right order
    sub esp, 68
    mov ebx, esp
    xor edx, edx ; work_sp
.flat_descend:
    cmp byte [edi + AST_NODE32.bKind], AST_BINARY
    jne .flat_leaf
    cmp byte [edi + AST_NODE32.bSub], BINOP_PIPE
    jne .flat_leaf
    cmp edx, 15
    jae .flat_leaf
    mov eax, dword [edi + AST_NODE32.pChild2]
    mov dword [ebx + edx * 4], eax
    inc edx
    mov edi, dword [edi + AST_NODE32.pChild1]
    jmp .flat_descend

.flat_leaf:
    cmp ecx, 16
    jae .flat_pop
    mov dword [ebp + ecx * 4], edi
    inc ecx
.flat_pop:
    test edx, edx
    jz .flat_done
    dec edx
    mov edi, dword [ebx + edx * 4]
    jmp .flat_descend

.flat_done:
    add esp, 68
    mov dword [ebp + 160], ecx ; stage_count
    mov dword [ebp + 164], 0   ; spawned_count

    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + 172], eax
    mov dword [ebp + 168], eax ; prev_read = orig_stdin

    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + 176], eax

    xor esi, esi ; stage idx i = 0..stage_count-1
.stage_lp:
    cmp esi, dword [ebp + 160]
    jae .wait_all

    mov eax, esi
    inc eax
    cmp eax, dword [ebp + 160]
    jae .last_stage

    ; Create anonymous 64 KiB pipe for stage i -> stage i+1
    mov dword [ebp + 128 + SECURITY_ATTRIBUTES32.nLength], 12
    mov dword [ebp + 128 + SECURITY_ATTRIBUTES32.lpSecurityDescriptor], 0
    mov dword [ebp + 128 + SECURITY_ATTRIBUTES32.bInheritHandle], 1
    lea eax, [ebp + 128]
    lea ecx, [ebp + 184] ; &hWrite
    lea edx, [ebp + 180] ; &hRead
    push 65536
    push eax
    push ecx
    push edx
    call dword [__imp__CreatePipe@16]
    test eax, eax
    jz .pipe_fail

    ; CRITICAL: Make next stage's read end (pipe_hRead) NON-INHERITABLE while spawning stage i
    ; so stage i does not hold a duplicate of its own downstream read handle!
    mov ebx, dword [ebp + 180]
    call redir_set_non_inheritable32
    mov dword [ebp + 180], eax
    mov edi, dword [ebp + 184] ; stage_out = pipe_hWrite (inheritable!)
    jmp .do_spawn

.last_stage:
    mov dword [ebp + 180], INVALID_HANDLE_VALUE
    mov dword [ebp + 184], INVALID_HANDLE_VALUE
    mov edi, dword [ebp + 176] ; stage_out = orig_stdout

.do_spawn:
    ; Make sure prev_read_handle IS inheritable when i > 0!
    cmp esi, 0
    je .in_ready
    call dword [__imp__GetCurrentProcess@0]
    mov ecx, eax
    sub esp, 4
    mov edx, esp
    push DUPLICATE_CLOSE_SOURCE + DUPLICATE_SAME_ACCESS
    push 1 ; bInheritHandle = TRUE
    push 0
    push edx
    push ecx
    push dword [ebp + 168]
    push ecx
    call dword [__imp__DuplicateHandle@28]
    pop eax
    mov dword [ebp + 168], eax

.in_ready:
    push esi
    mov esi, dword [ebp + esi * 4]
    call pipe_node_cmd_text32
    mov esi, eax
    mov ebx, dword [ebp + 168] ; stage_in
    lea edx, [ebp + 144]       ; &PI
    call pipe_spawn_stage32
    pop esi
    jc .spawn_cleanup

    ; Close child hThread immediately; record hProcess
    push dword [ebp + 144 + PROCESS_INFORMATION.hThread]
    call dword [__imp__CloseHandle@4]
    mov ecx, dword [ebp + 164]
    mov eax, dword [ebp + 144 + PROCESS_INFORMATION.hProcess]
    mov dword [ebp + 64 + ecx * 4], eax
    inc ecx
    mov dword [ebp + 164], ecx

.spawn_cleanup:
    ; Close parent's copy of stage_in (when i > 0) and pipe_hWrite immediately so EOF propagates!
    cmp esi, 0
    je .chk_close_wr
    push dword [ebp + 168]
    call dword [__imp__CloseHandle@4]
.chk_close_wr:
    cmp dword [ebp + 184], INVALID_HANDLE_VALUE
    je .advance_stage
    push dword [ebp + 184]
    call dword [__imp__CloseHandle@4]
.advance_stage:
    mov eax, dword [ebp + 180]
    mov dword [ebp + 168], eax
    inc esi
    jmp .stage_lp

.wait_all:
    mov ecx, dword [ebp + 164]
    test ecx, ecx
    jz .pipe_fail

    ; Wait concurrently for all spawned stages via WaitForMultipleObjects
    lea eax, [ebp + 64]
    push INFINITE_WAIT
    push 1 ; bWaitAll = TRUE
    push eax
    push ecx
    call dword [__imp__WaitForMultipleObjects@16]

    ; Query exit code of final stage (spawned_count - 1) and close all hProcess handles
    mov ebx, dword [ebp + 164]
    dec ebx
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push edx
    push dword [ebp + 64 + ebx * 4]
    call dword [__imp__GetExitCodeProcess@8]
    pop edi ; final exit code

    xor ebx, ebx
.close_procs:
    cmp ebx, dword [ebp + 164]
    jae .done_ok
    push dword [ebp + 64 + ebx * 4]
    call dword [__imp__CloseHandle@4]
    inc ebx
    jmp .close_procs

.done_ok:
    mov eax, edi
    mov dword [g_last_exit_code], eax
    add esp, 200
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.pipe_fail:
    mov eax, 1
    mov dword [g_last_exit_code], eax
    add esp, 200
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc
