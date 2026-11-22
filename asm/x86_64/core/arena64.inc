;; @file arena64.inc
;; @isa x64
;; @desc VirtualAlloc MEM_RESERVE + on-demand 64 KiB MEM_COMMIT 64-bit bump arena and persistent pool for x86-64.

@cpu x64
@format inc

;; @proc arena_init64
;; @abi win64_ms
;; @in none
;; @out CF=0 on success; CF=1, EAX=ERR_OUT_OF_MEMORY on failure
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY if both 8 MiB and 1 MiB reservations fail
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reserves a contiguous 64-bit VirtualAlloc statement arena (8 MiB std, 1 MiB fallback) and commits first 64 KiB.
@proc_frame arena_init64, 32, rbx
    mov ebx, W64_ARENA_RESERVE_STD
    xor ecx, ecx
    mov edx, ebx
    mov r8d, MEM_RESERVE
    mov r9d, PAGE_READWRITE
    call qword [rel __imp_VirtualAlloc]
    test rax, rax
    jnz .reserved

    mov ebx, W64_ARENA_RESERVE_LOW
    xor ecx, ecx
    mov edx, ebx
    mov r8d, MEM_RESERVE
    mov r9d, PAGE_READWRITE
    call qword [rel __imp_VirtualAlloc]
    test rax, rax
    jz .oom

.reserved:
    mov qword [rel g_arena_base], rax
    mov qword [rel g_arena_ptr], rax
    lea rdx, [rax + rbx]
    mov qword [rel g_arena_limit], rdx

    mov rcx, rax
    mov edx, W64_ARENA_COMMIT_GRAN
    mov r8d, MEM_COMMIT
    mov r9d, PAGE_READWRITE
    call qword [rel __imp_VirtualAlloc]
    test rax, rax
    jz .oom

    mov rax, qword [rel g_arena_base]
    add rax, W64_ARENA_COMMIT_GRAN
    mov qword [rel g_arena_committed], rax
    clc
    @epilog_ret

.oom:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc arena_reset64
;; @abi win64_ms
;; @in none
;; @out Resets [g_arena_ptr] to [g_arena_base] in O(1) without syscalls
;; @err none
;; @clobbers rax
;; @desc Resets the 64-bit statement bump arena pointer between top-level commands.
@proc arena_reset64
    mov rax, qword [rel g_arena_base]
    mov qword [rel g_arena_ptr], rax
    ret
@endproc

;; @proc arena_mark64
;; @abi win64_ms
;; @in none
;; @out RAX = current 64-bit arena pointer snapshot
;; @err none
;; @clobbers rax
;; @desc Captures the current 64-bit bump pointer for scoped scratch release.
@proc arena_mark64
    mov rax, qword [rel g_arena_ptr]
    ret
@endproc

;; @proc arena_release64
;; @abi win64_ms
;; @in RCX = previously captured 64-bit mark pointer
;; @out Rewinds [g_arena_ptr] to RCX if valid
;; @err none
;; @clobbers none
;; @desc Rewinds the bump arena pointer to a saved mark within [g_arena_base..g_arena_committed].
@proc arena_release64
    cmp rcx, qword [rel g_arena_base]
    jb .ignore
    cmp rcx, qword [rel g_arena_committed]
    ja .ignore
    mov qword [rel g_arena_ptr], rcx
.ignore:
    ret
@endproc

;; @proc arena_alloc64
;; @abi win64_ms
;; @in RCX = requested byte count
;; @out RAX = 8-byte aligned zero-syscall 64-bit bump pointer, CF=0; or CF=1, EAX=ERR_OUT_OF_MEMORY
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY on exhaustion
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Allocates 8-byte aligned memory from the 64-bit statement arena, committing 64 KiB pages on demand.
@proc_frame arena_alloc64, 32, rbx, rdi, r12
    mov rax, rcx
    test rax, rax
    jnz .align_sz
    mov eax, 8
.align_sz:
    add rax, 7
    and rax, -8

    mov rdi, qword [rel g_arena_ptr]
    mov rbx, rdi
    add rbx, rax
    jc .oom
    cmp rbx, qword [rel g_arena_limit]
    ja .oom

    cmp rbx, qword [rel g_arena_committed]
    jbe .ok

    mov r12, rbx
    sub r12, qword [rel g_arena_committed]
    add r12, W64_ARENA_COMMIT_GRAN - 1
    and r12, -W64_ARENA_COMMIT_GRAN

    mov rax, qword [rel g_arena_committed]
    add rax, r12
    cmp rax, qword [rel g_arena_limit]
    jbe .do_commit
    mov r12, qword [rel g_arena_limit]
    sub r12, qword [rel g_arena_committed]

.do_commit:
    mov rcx, qword [rel g_arena_committed]
    mov rdx, r12
    mov r8d, MEM_COMMIT
    mov r9d, PAGE_READWRITE
    call qword [rel __imp_VirtualAlloc]
    test rax, rax
    jz .oom
    add qword [rel g_arena_committed], r12

.ok:
    mov qword [rel g_arena_ptr], rbx
    mov rax, rdi
    clc
    @epilog_ret

.oom:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc arena_strdup64
;; @abi win64_ms
;; @in RCX = NUL-terminated source string
;; @out RAX = NUL-terminated copy in statement arena, CF=0; or CF=1 on OOM
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Duplicates a NUL-terminated string into the 8-byte aligned 64-bit statement arena.
@proc_frame arena_strdup64, 32, rbx, rsi, rdi
    mov rsi, rcx
    xor ebx, ebx
.len_lp:
    cmp byte [rsi + rbx], 0
    je .got_len
    inc rbx
    jmp .len_lp
.got_len:
    inc rbx
    mov rcx, rbx
    call arena_alloc64
    jc .fail
    mov rdi, rax
    mov rdx, rax
    mov rcx, rbx
    rep movsb
    mov rax, rdx
    clc
    @epilog_ret
.fail:
    stc
    @epilog_ret
@endproc

;; @proc perm_alloc64
;; @abi win64_ms
;; @in RCX = byte count
;; @out RAX = committed 64-bit VirtualAlloc block (zero-initialized by OS), CF=0; or CF=1, EAX=ERR_OUT_OF_MEMORY
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Allocates a persistent heap-free 64-bit memory block via VirtualAlloc(MEM_RESERVE | MEM_COMMIT).
@proc_frame perm_alloc64, 40
    mov rdx, rcx
    xor ecx, ecx
    mov r8d, MEM_RESERVE + MEM_COMMIT
    mov r9d, PAGE_READWRITE
    call qword [rel __imp_VirtualAlloc]
    test rax, rax
    jz .oom
    clc
    @epilog_ret
.oom:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    @epilog_ret
@endproc

;; @proc perm_free64
;; @abi win64_ms
;; @in RCX = VirtualAlloc base pointer (ignored if 0)
;; @out Releases block via VirtualFree(MEM_RELEASE)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Releases a persistent 64-bit VirtualAlloc block safely.
@proc_frame perm_free64, 40
    test rcx, rcx
    jz .done
    xor edx, edx
    mov r8d, MEM_RELEASE
    call qword [rel __imp_VirtualFree]
.done:
    @epilog_ret
@endproc
