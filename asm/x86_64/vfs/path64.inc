;; @file path64.inc
;; @isa x64
;; @desc Win64 path normalizer (=A:..=Z:, UNC, \?\), reserved character devices, Win10/11 case-sensitive probe, and O(N*M) DP globber.

@cpu x64
@format inc

;; @proc dev_match_name64
;; @abi win64_ms
;; @in RCX = candidate path (WTF-8)
;; @out RAX = Win64 device path ("CONIN$", "CONOUT$", "NUL", "AUX", "PRN", "COM1".."LPT9") or 0 if regular file
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Matches DOS/Win64 reserved character devices unless prefixed with verbatim \?\.
@proc_frame dev_match_name64, 64, rbx, rsi, rdi
    mov rsi, rcx
    cmp byte [rsi], 0x5C
    jne .find_base
    cmp byte [rsi + 1], 0x5C
    jne .find_base
    cmp byte [rsi + 2], '?'
    jne .chk_devns
    cmp byte [rsi + 3], 0x5C
    je .not_dev
.chk_devns:
    cmp byte [rsi + 2], '.'
    jne .find_base
    cmp byte [rsi + 3], 0x5C
    jne .find_base
    add rsi, 4

.find_base:
    mov rbx, rsi
.scan_sep:
    mov al, byte [rsi]
    test al, al
    jz .got_base
    cmp al, 0x5C
    je .after_sep
    cmp al, '/'
    je .after_sep
    cmp al, ':'
    jne .next_ch
    cmp byte [rsi + 1], 0
    je .got_base
.after_sep:
    lea rbx, [rsi + 1]
.next_ch:
    inc rsi
    jmp .scan_sep

.got_base:
    lea rdi, [rsp + 32]
    xor ecx, ecx
.stem_lp:
    mov al, byte [rbx + rcx]
    test al, al
    jz .stem_end
    cmp al, '.'
    je .stem_end
    cmp al, ':'
    je .stem_end
    cmp ecx, 8
    jae .not_dev
    call ascii_upper_al64
    mov byte [rdi + rcx], al
    inc ecx
    jmp .stem_lp
.stem_end:
    mov byte [rdi + rcx], 0

    cmp ecx, 3
    je .len3
    cmp ecx, 4
    je .len4
    cmp ecx, 6
    je .len6
    cmp ecx, 7
    je .len7
    jmp .not_dev

.len3:
    cmp dword [rdi], 0x004C554E ; "NUL\0"
    je .is_nul
    cmp dword [rdi], 0x004E4F43 ; "CON\0"
    je .is_con
    cmp dword [rdi], 0x004E5250 ; "PRN\0"
    je .is_prn
    cmp dword [rdi], 0x00585541 ; "AUX\0"
    je .is_aux
    jmp .not_dev

.len4:
    mov al, byte [rdi + 3]
    cmp al, '1'
    jb .not_dev
    cmp al, '9'
    ja .not_dev
    mov eax, dword [rdi]
    and eax, 0x00FFFFFF
    cmp eax, 0x004D4F43 ; "COM"
    je .is_com_lpt
    cmp eax, 0x0054504C ; "LPT"
    je .is_com_lpt
    jmp .not_dev

.len6:
    mov rcx, rdi
    lea rdx, [rel szDevConIn64]
    call str_eq_i64
    test eax, eax
    jnz .is_conin
    mov rcx, rdi
    lea rdx, [rel szDevClock64]
    call str_eq_i64
    test eax, eax
    jnz .is_nul
    jmp .not_dev

.len7:
    mov rcx, rdi
    lea rdx, [rel szDevConOut64]
    call str_eq_i64
    test eax, eax
    jnz .is_conout
    jmp .not_dev

.is_com_lpt:
    mov rcx, rdi
    call arena_strdup64
    jc .not_dev
    @epilog_ret

.is_nul:
    lea rax, [rel szDevNul64]
    @epilog_ret
.is_con:
    lea rax, [rel szDevCon64]
    @epilog_ret
.is_conin:
    lea rax, [rel szDevConIn64]
    @epilog_ret
.is_conout:
    lea rax, [rel szDevConOut64]
    @epilog_ret
.is_prn:
    lea rax, [rel szDevPrn64]
    @epilog_ret
.is_aux:
    lea rax, [rel szDevAux64]
    @epilog_ret

.not_dev:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc path_sync_drive_env64
;; @abi win64_ms
;; @in RCX = full normalized directory path (e.g. "C:\Windows")
;; @out Updates hidden "=X:" environment entry when RCX begins with a drive letter
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Synchronizes hidden per-drive "=C:=C:\path" environment variable just like cmd.exe.
@proc_frame path_sync_drive_env64, 48, rsi
    mov rsi, rcx
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'A'
    jb .done
    cmp al, 'Z'
    ja .done
    cmp byte [rsi + 1], ':'
    jne .done

    lea rcx, [rsp + 32]
    mov byte [rcx], '='
    mov byte [rcx + 1], al
    mov byte [rcx + 2], ':'
    mov byte [rcx + 3], 0
    mov rdx, rsi
    call env_set64
.done:
    @epilog_ret
@endproc

;; @proc path_normalize64
;; @abi win64_ms
;; @in RCX = raw input WTF-8 path
;; @out RAX = normalized path in statement arena (preserving \?\ verbatim, resolving =X: drive-rel), CF=0
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Resolves drive-relative (X:rel via =X:), slashes, . / .., and verbatim \?\ paths.
@proc_frame path_normalize64, 48, rbx, rsi, rdi, r12, r13
    mov rsi, rcx

    cmp byte [rsi], 0x5C
    jne .chk_drv_rel
    cmp byte [rsi + 1], 0x5C
    jne .chk_drv_rel
    cmp byte [rsi + 2], '?'
    jne .chk_drv_rel
    cmp byte [rsi + 3], 0x5C
    jne .chk_drv_rel
    mov rcx, rsi
    call arena_strdup64
    @epilog_ret

.chk_drv_rel:
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'A'
    jb .std_full
    cmp al, 'Z'
    ja .std_full
    cmp byte [rsi + 1], ':'
    jne .std_full
    mov dl, byte [rsi + 2]
    cmp dl, 0x5C
    je .std_full
    cmp dl, '/'
    je .std_full

    lea rcx, [rsp + 32]
    mov byte [rcx], '='
    mov byte [rcx + 1], al
    mov byte [rcx + 2], ':'
    mov byte [rcx + 3], 0
    call env_get64
    test rax, rax
    jz .std_full

    mov rbx, rax
    mov rcx, rbx
    call str_len64
    mov r12, rax
    mov rcx, rsi
    call str_len64
    lea rcx, [rax + r12 + 8]
    call arena_alloc64
    jc .out
    mov r13, rax
    mov rcx, r13
    mov rdx, rbx
    call str_copy64
    mov rdi, rax
    cmp rdi, r13
    jbe .app_rel
    cmp byte [rdi - 1], 0x5C
    je .app_rel
    mov byte [rdi], 0x5C
    inc rdi
.app_rel:
    mov rcx, rdi
    lea rdx, [rsi + 2]
    call str_copy64
    mov rcx, r13
    call hal_fullpath64
    @epilog_ret

.std_full:
    mov rcx, rsi
    call hal_fullpath64
.out:
    @epilog_ret
@endproc

;; @proc dir_is_case_sensitive64
;; @abi win64_ms
;; @in RCX = directory path (WTF-8)
;; @out EAX = 1 if Win10 1803+ / Win11 directory has FILE_CS_FLAG_CASE_SENSITIVE_DIR, else 0
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Probes NtQueryInformationFile(FileCaseSensitiveInformation = 71) with 16-byte 64-bit IO_STATUS_BLOCK.
@proc_frame dir_is_case_sensitive64, 80, rbx
    cmp dword [rel g_os_major], 10
    jb .no
    cmp qword [rel pfnNtQueryInfoFile64], 0
    je .no

    call nt64_path_to_w
    jc .no
    mov rcx, rax
    mov edx, 0x00000080 ; FILE_READ_ATTRIBUTES
    mov r8d, FILE_SHARE_READ + FILE_SHARE_WRITE
    xor r9d, r9d
    mov dword [rsp + 32], OPEN_EXISTING
    mov dword [rsp + 40], FILE_FLAG_BACKUP_SEMANTICS
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    cmp rax, INVALID_HANDLE_VALUE
    je .no
    mov rbx, rax

    ; [rsp + 56 .. 71]: 16-byte IO_STATUS_BLOCK, [rsp + 72]: 4-byte FILE_CASE_SENSITIVE_INFORMATION
    mov qword [rsp + 56], 0
    mov qword [rsp + 64], 0
    mov dword [rsp + 72], 0
    mov rcx, rbx
    lea rdx, [rsp + 56]
    lea r8, [rsp + 72]
    mov r9d, 4
    mov dword [rsp + 32], 71 ; FileCaseSensitiveInformation
    call qword [rel pfnNtQueryInfoFile64]
    mov dword [rsp + 76], eax

    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    cmp dword [rsp + 76], 0
    jne .no
    mov eax, dword [rsp + 72]
    and eax, 1
    @epilog_ret

.no:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc matches_win32_glob64
;; @abi win64_ms
;; @in RCX = pattern (*, ?, <, >, "), RDX = filename, R8D = bCaseSensitive (0/1)
;; @out EAX = 1 if filename matches pattern, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Zero-backtracking O(N * M) rolling-state wildcard matcher immune to exponential *a*a*a*b blowup.
@proc_frame matches_win32_glob64, 584, rbp, rbx, rsi, rdi
    mov rsi, rcx
    mov rdi, rdx

    cmp byte [rsi], '*'
    jne .init_dp
    cmp byte [rsi + 1], 0
    je .match_yes
    cmp byte [rsi + 1], '.'
    jne .init_dp
    cmp byte [rsi + 2], '*'
    jne .init_dp
    cmp byte [rsi + 3], 0
    je .match_yes

.init_dp:
    lea rbp, [rsp + 32]
    mov dword [rbp + 528], r8d

    mov rcx, rsi
    call str_len64
    cmp rax, 260
    jbe .plen_ok
    mov eax, 260
.plen_ok:
    mov dword [rbp + 532], eax

    mov r8, rdi
    mov rdi, rbp
    xor eax, eax
    mov ecx, 66
    rep stosd
    mov rdi, r8

    mov byte [rbp], 1
    xor ecx, ecx
.seed_lp:
    cmp ecx, dword [rbp + 532]
    jae .outer_str
    cmp byte [rcx + rbp], 0
    je .seed_next
    mov al, byte [rsi + rcx]
    cmp al, '*'
    je .seed_eps
    cmp al, '<'
    je .seed_eps
    jmp .seed_next
.seed_eps:
    mov byte [rcx + rbp + 1], 1
.seed_next:
    inc ecx
    jmp .seed_lp

.outer_str:
    movzx ebx, byte [rdi]
    test ebx, ebx
    jz .check_accept

    mov r8, rdi
    lea rdi, [rbp + 264]
    xor eax, eax
    mov ecx, 66
    rep stosd
    mov rdi, r8

    xor ecx, ecx
.inner_pat:
    cmp ecx, dword [rbp + 532]
    jae .swap_rows
    cmp byte [rcx + rbp], 0
    je .next_p

    mov al, byte [rsi + rcx]
    cmp al, '*'
    je .on_star
    cmp al, '<'
    je .on_star
    cmp al, '?'
    je .on_single
    cmp al, '>'
    je .on_dos_qm
    cmp al, '"'
    je .on_dos_dot

    mov dl, bl
    cmp dword [rbp + 528], 0
    jne .cmp_lit
    call ascii_upper_al64
    mov r9b, al
    mov al, dl
    call ascii_upper_al64
    mov dl, al
    mov al, r9b
.cmp_lit:
    cmp al, dl
    jne .next_p
.on_single:
    mov byte [rcx + rbp + 265], 1
    jmp .next_p

.on_star:
    mov byte [rcx + rbp + 264], 1
    mov byte [rcx + rbp + 265], 1
    jmp .next_p

.on_dos_qm:
    cmp bl, '.'
    je .dos_qm_eps
    mov byte [rcx + rbp + 265], 1
    jmp .next_p
.dos_qm_eps:
    mov byte [rcx + rbp + 1], 1
    jmp .next_p

.on_dos_dot:
    cmp bl, '.'
    jne .next_p
    mov byte [rcx + rbp + 265], 1

.next_p:
    inc ecx
    jmp .inner_pat

.swap_rows:
    mov r8, rsi
    mov r9, rdi
    lea rsi, [rbp + 264]
    mov rdi, rbp
    mov ecx, 66
    rep movsd
    mov rsi, r8
    mov rdi, r9

    xor ecx, ecx
.eps_lp:
    cmp ecx, dword [rbp + 532]
    jae .adv_char
    cmp byte [rcx + rbp], 0
    je .eps_nx
    mov al, byte [rsi + rcx]
    cmp al, '*'
    je .set_eps
    cmp al, '<'
    jne .eps_nx
.set_eps:
    mov byte [rcx + rbp + 1], 1
.eps_nx:
    inc ecx
    jmp .eps_lp

.adv_char:
    inc rdi
    jmp .outer_str

.check_accept:
    xor ecx, ecx
.tail_eps:
    cmp ecx, dword [rbp + 532]
    jae .read_final
    cmp byte [rcx + rbp], 0
    je .tail_nx
    mov al, byte [rsi + rcx]
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
    cmp byte [rsi + rcx + 1], '*'
    je .tail_ok
    cmp byte [rsi + rcx + 1], 0
    jne .tail_nx
.tail_ok:
    mov byte [rcx + rbp + 1], 1
.tail_nx:
    inc ecx
    jmp .tail_eps

.read_final:
    mov ecx, dword [rbp + 532]
    movzx eax, byte [rcx + rbp]
    @epilog_ret

.match_yes:
    mov eax, 1
    @epilog_ret
@endproc
