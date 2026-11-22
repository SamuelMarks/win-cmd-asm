;; @file path32.inc
;; @isa 386
;; @desc Multi-era Win32 path normalizer (=A:..=Z:, UNC, \?\), reserved character devices, and O(N*M) DP globber.

@cpu 386
@format inc

;; @proc dev_match_name32
;; @abi win32
;; @in ESI = candidate path (WTF-8 / ASCII)
;; @out EAX = Win32 device path ("CONIN$", "CONOUT$", "NUL", "AUX", "PRN", "COM1".."LPT9") or 0 if regular file
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Matches DOS/Win32 reserved character devices across bare (NUL), dir-prefixed (C:	mp\NUL), NUL.txt, and CON:.
@proc dev_match_name32
    push ebx
    push esi
    push edi

    ; If prefixed with \?\ (verbatim NT namespace), do NOT intercept reserved DOS device names!
    cmp byte [esi], 0x5C
    jne .find_base
    cmp byte [esi + 1], 0x5C
    jne .find_base
    cmp byte [esi + 2], '?'
    jne .chk_devns
    cmp byte [esi + 3], 0x5C
    je .not_dev
.chk_devns:
    cmp byte [esi + 2], '.'
    jne .find_base
    cmp byte [esi + 3], 0x5C
    jne .find_base
    add esi, 4

.find_base:
    ; Locate last '', '/', or ':' (unless trailing ':'!)
    mov ebx, esi
.scan_sep:
    mov al, byte [esi]
    test al, al
    jz .got_base
    cmp al, 0x5C
    je .after_sep
    cmp al, '/'
    je .after_sep
    cmp al, ':'
    jne .next_ch
    cmp byte [esi + 1], 0
    je .got_base
.after_sep:
    lea ebx, [esi + 1]
.next_ch:
    inc esi
    jmp .scan_sep

.got_base:
    ; Extract stem up to '.' or ':' (max 7 chars) into stack uppercase buffer
    sub esp, 12
    mov edi, esp
    xor ecx, ecx
.stem_lp:
    mov al, byte [ebx + ecx]
    test al, al
    jz .stem_end
    cmp al, '.'
    je .stem_end
    cmp al, ':'
    je .stem_end
    cmp ecx, 8
    jae .not_dev_stk
    call ascii_upper_al
    mov byte [edi + ecx], al
    inc ecx
    jmp .stem_lp
.stem_end:
    mov byte [edi + ecx], 0

    cmp ecx, 3
    je .len3
    cmp ecx, 4
    je .len4
    cmp ecx, 6
    je .len6
    cmp ecx, 7
    je .len7
    jmp .not_dev_stk

.len3:
    cmp dword [edi], 0x004C554E ; "NUL\0"
    je .is_nul
    cmp dword [edi], 0x004E4F43 ; "CON\0"
    je .is_con
    cmp dword [edi], 0x004E5250 ; "PRN\0"
    je .is_prn
    cmp dword [edi], 0x00585541 ; "AUX\0"
    je .is_aux
    jmp .not_dev_stk

.len4:
    ; COM1..COM9 or LPT1..LPT9
    mov al, byte [edi + 3]
    cmp al, '1'
    jb .not_dev_stk
    cmp al, '9'
    ja .not_dev_stk
    mov eax, dword [edi]
    and eax, 0x00FFFFFF
    cmp eax, 0x004D4F43 ; "COM"
    je .is_com_lpt
    cmp eax, 0x0054504C ; "LPT"
    je .is_com_lpt
    jmp .not_dev_stk

.len6:
    cmp dword [edi], 0x244E494E ; check "CONIN$"
    ; Check byte-by-byte for CONIN$ and CLOCK$
    mov esi, edi
    push edi
    mov edi, addr(szDevConIn)
    call str_eq_i32
    pop edi
    test eax, eax
    jnz .is_conin
    mov esi, edi
    mov edi, addr(szDevClock)
    call str_eq_i32
    test eax, eax
    jnz .is_nul
    jmp .not_dev_stk

.len7:
    mov esi, edi
    mov edi, addr(szDevConOut)
    call str_eq_i32
    test eax, eax
    jnz .is_conout
    jmp .not_dev_stk

.is_com_lpt:
    mov esi, edi
    call arena_strdup32
    jc .not_dev_stk
    add esp, 12
    pop edi
    pop esi
    pop ebx
    ret

.is_nul:
    mov eax, addr(szDevNul)
    jmp .ret_dev
.is_con:
    mov eax, addr(szDevCon)
    jmp .ret_dev
.is_conin:
    mov eax, addr(szDevConIn)
    jmp .ret_dev
.is_conout:
    mov eax, addr(szDevConOut)
    jmp .ret_dev
.is_prn:
    mov eax, addr(szDevPrn)
    jmp .ret_dev
.is_aux:
    mov eax, addr(szDevAux)
.ret_dev:
    add esp, 12
    pop edi
    pop esi
    pop ebx
    ret

.not_dev_stk:
    add esp, 12
.not_dev:
    xor eax, eax
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc path_sync_drive_env32
;; @abi win32
;; @in ESI = full normalized directory path (e.g. "C:\Windows")
;; @out Updates hidden "=X:" environment entry when ESI begins with a drive letter
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Synchronizes hidden per-drive "=C:=C:\path" environment variable just like cmd.exe.
@proc path_sync_drive_env32
    push esi
    push edi
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'A'
    jb .done
    cmp al, 'Z'
    ja .done
    cmp byte [esi + 1], ':'
    jne .done

    sub esp, 8
    mov edi, esp
    mov byte [edi], '='
    mov byte [edi + 1], al
    mov byte [edi + 2], ':'
    mov byte [edi + 3], 0
    mov ecx, esi
    mov esi, edi
    mov edi, ecx
    call env_set32
    add esp, 8

.done:
    pop edi
    pop esi
    ret
@endproc

;; @proc path_normalize32
;; @abi win32
;; @in ESI = raw input path (WTF-8 / ANSI)
;; @out EAX = normalized path in statement arena (preserving \?\ verbatim, resolving =X: drive-rel), CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Resolves drive-relative (X:rel via =X:), slashes, . / .., and trailing dots/spaces (except \?\).
@proc path_normalize32
    push ebx
    push esi
    push edi

    ; Verbatim \?\ path: duplicate untouched!
    cmp byte [esi], 0x5C
    jne .chk_drv_rel
    cmp byte [esi + 1], 0x5C
    jne .chk_drv_rel
    cmp byte [esi + 2], '?'
    jne .chk_drv_rel
    cmp byte [esi + 3], 0x5C
    jne .chk_drv_rel
    call arena_strdup32
    jmp .out

.chk_drv_rel:
    ; Check drive-relative path "X:foo" (where [esi+1]==':' and [esi+2]!='' and [esi+2]!='/')
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'A'
    jb .std_full
    cmp al, 'Z'
    ja .std_full
    cmp byte [esi + 1], ':'
    jne .std_full
    mov dl, byte [esi + 2]
    cmp dl, 0x5C
    je .std_full
    cmp dl, '/'
    je .std_full

    ; Lookup hidden "=X:" variable in our shell environment!
    sub esp, 8
    mov edi, esp
    mov byte [edi], '='
    mov byte [edi + 1], al
    mov byte [edi + 2], ':'
    mov byte [edi + 3], 0
    push esi
    mov esi, edi
    call env_get32
    pop esi
    add esp, 8
    test eax, eax
    jz .std_full

    ; Combine "<=X: value>\<rest>" in arena and pass to hal_fullpath32
    mov ebx, eax
    push esi
    mov esi, ebx
    call str_len32
    mov edi, ecx
    pop esi
    push edi
    call str_len32
    pop edi
    lea ecx, [ecx + edi + 8]
    call arena_alloc32
    jc .out
    mov edx, eax
    push edx
    mov edi, edx
    push esi
    mov esi, ebx
    call str_copy32
    pop esi
    mov edi, eax
    cmp edi, edx
    jbe .app_rel
    cmp byte [edi - 1], 0x5C
    je .app_rel
    mov byte [edi], 0x5C
    inc edi
.app_rel:
    add esi, 2
    call str_copy32
    pop esi
    call hal_fullpath32
    jmp .out

.std_full:
    call hal_fullpath32

.out:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc dir_is_case_sensitive32
;; @abi win32
;; @in ESI = directory path (WTF-8)
;; @out EAX = 1 if Win10 1803+ / Win11 directory has FILE_CS_FLAG_CASE_SENSITIVE_DIR, else 0
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Probes NtQueryInformationFile(FileCaseSensitiveInformation = 71) on Windows 10/11 NTFS & Dev Drive ReFS.
@proc dir_is_case_sensitive32
    cmp byte [g_is_win9x], 0
    jne .no
    cmp dword [g_os_major], 10
    jb .no
    cmp dword [pfnNtQueryInfoFile_p20], 0
    je .no

    push ebx
    push esi
    call ntw_path_to_w32
    jc .no_pop
    push 0
    push FILE_FLAG_BACKUP_SEMANTICS
    push OPEN_EXISTING
    push 0
    push FILE_SHARE_READ + FILE_SHARE_WRITE
    push 0x00000080 ; FILE_READ_ATTRIBUTES
    push eax
    call dword [__imp__CreateFileW@28]
    cmp eax, INVALID_HANDLE_VALUE
    je .no_pop
    mov ebx, eax

    ; IO_STATUS_BLOCK (8B) + FILE_CASE_SENSITIVE_INFORMATION (4B Flags)
    sub esp, 16
    mov dword [esp + 8], 0
    lea ecx, [esp + 8]
    mov edx, esp
    push 71 ; FileCaseSensitiveInformation
    push 4
    push ecx
    push edx
    push ebx
    call dword [pfnNtQueryInfoFile_p20]
    mov ecx, dword [esp + 8]
    add esp, 16
    push eax
    push ecx
    push ebx
    call dword [__imp__CloseHandle@4]
    pop ecx
    pop eax
    test eax, eax
    jnz .no_pop
    and ecx, 1
    mov eax, ecx
    pop esi
    pop ebx
    ret

.no_pop:
    pop esi
    pop ebx
.no:
    xor eax, eax
    ret
@endproc

;; @proc matches_win32_glob32
;; @abi win32
;; @in ESI = pattern (supporting *, ?, < DOS_STAR, > DOS_QM, " DOS_DOT), EDI = filename, ECX = bCaseSensitive (0/1)
;; @out EAX = 1 if filename matches pattern, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Zero-backtracking O(N * M) rolling-bitset wildcard matcher immune to *a*a*a*b exponential blowup.
@proc matches_win32_glob32
    push ebx
    push esi
    push edi
    push ebp

    ; Fast universal wildcard shortcuts ("*" or "*.*")
    cmp byte [esi], '*'
    jne .init_dp
    cmp byte [esi + 1], 0
    je .match_yes
    cmp byte [esi + 1], '.'
    jne .init_dp
    cmp byte [esi + 2], '*'
    jne .init_dp
    cmp byte [esi + 3], 0
    je .match_yes

.init_dp:
    ; Stack-allocate two 264-byte state arrays (supports patterns up to 260 bytes with zero heap/recursion!)
    sub esp, 536
    mov ebp, esp
    mov dword [ebp + 528], ecx ; bCaseSensitive

    ; Compute pattern length M (clamped to 260) and last dot index in EDI filename
    call str_len32
    cmp ecx, 260
    jbe .plen_ok
    mov ecx, 260
.plen_ok:
    mov dword [ebp + 532], ecx

    ; Zero cur[0..263]
    push edi
    mov edi, ebp
    xor eax, eax
    mov ecx, 66
    rep stosd
    pop edi

    ; Seed cur[0] = 1 and propagate leading '*' / '<' / '"' epsilon transitions
    mov byte [ebp], 1
    xor ecx, ecx
.seed_lp:
    cmp ecx, dword [ebp + 532]
    jae .outer_str
    cmp byte [ecx + ebp], 0
    je .seed_next
    mov al, byte [esi + ecx]
    cmp al, '*'
    je .seed_eps
    cmp al, '<'
    je .seed_eps
    jmp .seed_next
.seed_eps:
    mov byte [ebp + ecx + 1], 1
.seed_next:
    inc ecx
    jmp .seed_lp

.outer_str:
    movzx ebx, byte [edi]
    test ebx, ebx
    jz .check_accept

    ; Zero next[0..263] at [ebp + 264]
    push edi
    lea edi, [ebp + 264]
    xor eax, eax
    mov ecx, 66
    rep stosd
    pop edi

    xor ecx, ecx
.inner_pat:
    cmp ecx, dword [ebp + 532]
    jae .swap_rows
    cmp byte [ecx + ebp], 0
    je .next_p

    mov al, byte [esi + ecx]
    cmp al, '*'
    je .on_star
    cmp al, '<'
    je .on_dos_star
    cmp al, '?'
    je .on_single
    cmp al, '>'
    je .on_dos_qm
    cmp al, '"'
    je .on_dos_dot

    ; Literal character comparison
    mov dl, bl
    cmp dword [ebp + 528], 0
    jne .cmp_lit
    call ascii_upper_al
    mov ah, al
    mov al, dl
    call ascii_upper_al
    mov dl, al
    mov al, ah
.cmp_lit:
    cmp al, dl
    jne .next_p
.on_single:
    mov byte [ebp + 264 + ecx + 1], 1
    jmp .next_p

.on_star:
    mov byte [ebp + 264 + ecx], 1
    mov byte [ebp + 264 + ecx + 1], 1
    jmp .next_p

.on_dos_star:
    mov byte [ebp + 264 + ecx], 1
    mov byte [ebp + 264 + ecx + 1], 1
    jmp .next_p

.on_dos_qm:
    cmp bl, '.'
    je .dos_qm_eps
    mov byte [ebp + 264 + ecx + 1], 1
    jmp .next_p
.dos_qm_eps:
    mov byte [ebp + ecx + 1], 1
    jmp .next_p

.on_dos_dot:
    cmp bl, '.'
    jne .next_p
    mov byte [ebp + 264 + ecx + 1], 1

.next_p:
    inc ecx
    jmp .inner_pat

.swap_rows:
    ; Copy next -> cur and propagate '*' / '<' epsilons
    push esi
    push edi
    lea esi, [ebp + 264]
    mov edi, ebp
    mov ecx, 66
    rep movsd
    pop edi
    pop esi

    xor ecx, ecx
.eps_lp:
    cmp ecx, dword [ebp + 532]
    jae .adv_char
    cmp byte [ecx + ebp], 0
    je .eps_nx
    mov al, byte [esi + ecx]
    cmp al, '*'
    je .set_eps
    cmp al, '<'
    jne .eps_nx
.set_eps:
    mov byte [ebp + ecx + 1], 1
.eps_nx:
    inc ecx
    jmp .eps_lp

.adv_char:
    inc edi
    jmp .outer_str

.check_accept:
    ; Propagate trailing '*', '>', '"' that can match end-of-filename!
    xor ecx, ecx
.tail_eps:
    cmp ecx, dword [ebp + 532]
    jae .read_final
    cmp byte [ecx + ebp], 0
    je .tail_nx
    mov al, byte [esi + ecx]
    cmp al, '*'
    je .tail_ok
    cmp al, '<'
    je .tail_ok
    cmp al, '>'
    je .tail_ok
    cmp al, '"'
    je .tail_ok
    cmp al, '.'
    jne .tail_nx
    cmp byte [esi + ecx + 1], '*'
    je .tail_ok
    cmp byte [esi + ecx + 1], 0
    jne .tail_nx
.tail_ok:
    mov byte [ebp + ecx + 1], 1
.tail_nx:
    inc ecx
    jmp .tail_eps

.read_final:
    mov ecx, dword [ebp + 532]
    movzx eax, byte [ecx + ebp]
    add esp, 536
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.match_yes:
    mov eax, 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc
