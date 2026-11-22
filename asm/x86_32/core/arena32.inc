;; @file arena32.inc
;; @isa 386
;; @desc VirtualAlloc MEM_RESERVE + on-demand 64 KiB MEM_COMMIT bump arena and persistent pool for IA-32.

@cpu 386
@format inc

;; @proc arena_init32
;; @abi win32
;; @in none
;; @out CF=0 on success; CF=1, EAX=ERR_OUT_OF_MEMORY on failure
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY if both 4 MiB and 256 KiB reservations fail
;; @clobbers eax, ecx, edx
;; @desc Reserves a contiguous VirtualAlloc statement arena (4 MiB std, 256 KiB low-RAM fallback) and commits first 64 KiB.
@proc arena_init32
    push ebx
    mov ebx, W32_ARENA_RESERVE_STD
    push PAGE_READWRITE
    push MEM_RESERVE
    push ebx
    push 0
    call dword [__imp__VirtualAlloc@16]
    test eax, eax
    jnz .reserved

    mov ebx, W32_ARENA_RESERVE_LOW
    push PAGE_READWRITE
    push MEM_RESERVE
    push ebx
    push 0
    call dword [__imp__VirtualAlloc@16]
    test eax, eax
    jz .oom

.reserved:
    mov dword [g_arena_base], eax
    mov dword [g_arena_ptr], eax
    lea edx, [eax + ebx]
    mov dword [g_arena_limit], edx

    push PAGE_READWRITE
    push MEM_COMMIT
    push W32_ARENA_COMMIT_GRAN
    push eax
    call dword [__imp__VirtualAlloc@16]
    test eax, eax
    jz .oom

    mov eax, dword [g_arena_base]
    add eax, W32_ARENA_COMMIT_GRAN
    mov dword [g_arena_committed], eax
    pop ebx
    clc
    ret

.oom:
    mov eax, ERR_OUT_OF_MEMORY
    pop ebx
    stc
    ret
@endproc

;; @proc arena_reset32
;; @abi win32
;; @in none
;; @out Resets [g_arena_ptr] to [g_arena_base] in O(1) without syscalls
;; @err none
;; @clobbers eax
;; @desc Resets the statement bump arena pointer in two instructions between top-level commands.
@proc arena_reset32
    mov eax, dword [g_arena_base]
    mov dword [g_arena_ptr], eax
    ret
@endproc

;; @proc arena_mark32
;; @abi win32
;; @in none
;; @out EAX = current arena pointer snapshot
;; @err none
;; @clobbers eax
;; @desc Captures the current bump pointer for scoped scratch release.
@proc arena_mark32
    mov eax, dword [g_arena_ptr]
    ret
@endproc

;; @proc arena_release32
;; @abi win32
;; @in EAX = previously captured mark pointer
;; @out Rewinds [g_arena_ptr] to EAX if valid
;; @err none
;; @clobbers none
;; @desc Rewinds the bump arena pointer to a saved mark within [g_arena_base..g_arena_committed].
@proc arena_release32
    cmp eax, dword [g_arena_base]
    jb .ignore
    cmp eax, dword [g_arena_committed]
    ja .ignore
    mov dword [g_arena_ptr], eax
.ignore:
    ret
@endproc

;; @proc arena_alloc32
;; @abi win32
;; @in ECX = requested byte count
;; @out EAX = 8-byte aligned zero-syscall bump pointer, CF=0; or CF=1, EAX=ERR_OUT_OF_MEMORY
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY on exhaustion
;; @clobbers eax, ecx, edx
;; @desc Allocates 8-byte aligned memory from the statement arena, committing 64 KiB pages on demand.
@proc arena_alloc32
    push ebx
    push edi
    mov eax, ecx
    test eax, eax
    jnz .align_sz
    mov eax, 8
.align_sz:
    add eax, 7
    and eax, -8

    mov edi, dword [g_arena_ptr]
    mov ebx, edi
    add ebx, eax
    jc .oom
    cmp ebx, dword [g_arena_limit]
    ja .oom

.check_commit:
    cmp ebx, dword [g_arena_committed]
    jbe .ok

    mov edx, ebx
    sub edx, dword [g_arena_committed]
    add edx, W32_ARENA_COMMIT_GRAN - 1
    and edx, -W32_ARENA_COMMIT_GRAN

    mov eax, dword [g_arena_committed]
    add eax, edx
    cmp eax, dword [g_arena_limit]
    jbe .do_commit
    mov edx, dword [g_arena_limit]
    sub edx, dword [g_arena_committed]

.do_commit:
    push edx
    push PAGE_READWRITE
    push MEM_COMMIT
    push edx
    push dword [g_arena_committed]
    call dword [__imp__VirtualAlloc@16]
    pop edx
    test eax, eax
    jz .oom
    add dword [g_arena_committed], edx

.ok:
    mov dword [g_arena_ptr], ebx
    mov eax, edi
    pop edi
    pop ebx
    clc
    ret

.oom:
    mov eax, ERR_OUT_OF_MEMORY
    pop edi
    pop ebx
    stc
    ret
@endproc

;; @proc arena_strdup32
;; @abi win32
;; @in ESI = NUL-terminated source string
;; @out EAX = NUL-terminated copy in statement arena, CF=0; or CF=1 on OOM
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Duplicates a NUL-terminated string into the 8-byte aligned statement arena.
@proc arena_strdup32
    push esi
    push edi
    xor ecx, ecx
.len_lp:
    cmp byte [esi + ecx], 0
    je .got_len
    inc ecx
    jmp .len_lp
.got_len:
    inc ecx
    push ecx
    call arena_alloc32
    pop ecx
    jc .fail
    mov edi, eax
    mov edx, eax
    rep movsb
    mov eax, edx
    pop edi
    pop esi
    clc
    ret
.fail:
    pop edi
    pop esi
    stc
    ret
@endproc

;; @proc perm_alloc32
;; @abi win32
;; @in ECX = byte count
;; @out EAX = committed VirtualAlloc block (zero-initialized by OS), CF=0; or CF=1, EAX=ERR_OUT_OF_MEMORY
;; @err CF=1, EAX=ERR_OUT_OF_MEMORY
;; @clobbers eax, ecx, edx
;; @desc Allocates a persistent heap-free memory block via VirtualAlloc(MEM_RESERVE | MEM_COMMIT).
@proc perm_alloc32
    push PAGE_READWRITE
    push MEM_RESERVE + MEM_COMMIT
    push ecx
    push 0
    call dword [__imp__VirtualAlloc@16]
    test eax, eax
    jz .oom
    clc
    ret
.oom:
    mov eax, ERR_OUT_OF_MEMORY
    stc
    ret
@endproc

;; @proc perm_free32
;; @abi win32
;; @in EAX = VirtualAlloc base pointer (ignored if 0)
;; @out Releases block via VirtualFree(MEM_RELEASE)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Releases a persistent VirtualAlloc block safely.
@proc perm_free32
    test eax, eax
    jz .done
    push MEM_RELEASE
    push 0
    push eax
    call dword [__imp__VirtualFree@12]
.done:
    ret
@endproc
