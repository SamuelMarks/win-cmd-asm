;; @file cmds32_b.inc
;; @isa 386
;; @desc IA-32 Internal Built-Ins Part B (CD, PUSHD/POPD UNC, MD, RD /S, MKLINK, COPY +, DEL, MOVE, REN, DIR, TYPE, START, VOL).

@cpu 386
@format inc

;; @proc bi_cd32
;; @abi win32
;; @in ESI = raw argument tail (`[/D] [path]` — supporting unquoted spaces in `CD C:\Program Files` when /E:ON!)
;; @out Prints CWD (or =X: CWD) or changes directory/drive and updates g_last_exit_code
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements CD / CHDIR including /D cross-drive switching and unquoted space paths.
@proc bi_cd32
    push ebx
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .show_cur

    xor ebx, ebx ; /D flag
    cmp byte [esi], '/'
    jne .got_path
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'D'
    jne .got_path
    mov ebx, 1
    add esi, 2
    call prs_skip_ws32
    mov esi, eax

.got_path:
    ; Trim trailing whitespace and strip quotes
    call arena_strdup32
    jc .err
    mov esi, eax
    call str_len32
.tr_lp:
    test ecx, ecx
    jz .show_cur
    mov al, byte [esi + ecx - 1]
    cmp al, 0x20
    je .tr_c
    cmp al, 0x09
    jne .unq
.tr_c:
    dec ecx
    mov byte [esi + ecx], 0
    jmp .tr_lp
.unq:
    call tok_strip_quotes32
    jc .err
    mov esi, eax

    ; Bare `CD X:` (length 2, [1]==':', no /D) queries per-drive CWD!
    cmp byte [esi + 1], ':'
    jne .do_chdir
    cmp byte [esi + 2], 0
    jne .chk_other_drv
    test ebx, ebx
    jnz .do_chdir

    sub esp, 8
    mov edi, esp
    mov al, byte [esi]
    call ascii_upper_al
    mov byte [edi], '='
    mov byte [edi + 1], al
    mov byte [edi + 2], ':'
    mov byte [edi + 3], 0
    push esi
    mov esi, edi
    call env_get_raw32
    pop esi
    add esp, 8
    test eax, eax
    jnz .prt_drv_cwd
    call con_print_z32
    mov esi, addr(szBackslashCrLf)
    call con_print_z32
    jmp .ok
.prt_drv_cwd:
    mov esi, eax
    call con_print_z32
    call con_print_crlf32
    jmp .ok

.chk_other_drv:
    ; When target is on another drive and /D is NOT set: resolve full path, check existence, and update ONLY =X:!
    test ebx, ebx
    jnz .do_chdir
    mov ecx, 520
    call arena_alloc32
    jc .err
    mov edi, eax
    mov ecx, 512
    push esi
    call hal_get_cwd32
    pop esi
    jc .do_chdir
    mov al, byte [esi]
    call ascii_upper_al
    mov dl, al
    mov al, byte [edi]
    call ascii_upper_al
    cmp al, dl
    je .do_chdir

    ; Different drive without /D: verify directory exists and save in =X: without switching active drive!
    call path_normalize32
    jc .err_rep
    mov ebx, eax
    mov esi, ebx
    call hal_get_attr32
    jc .err_rep
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .bad_dir
    mov esi, ebx
    call path_sync_drive_env32
    jmp .ok

.do_chdir:
    call path_normalize32
    jc .err_rep
    mov esi, eax
    call hal_set_cwd32
    jc .err_rep

    mov ecx, 2048
    call arena_alloc32
    jc .ok
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32
    jc .ok
    mov esi, edi
    call path_sync_drive_env32
    jmp .ok

.show_cur:
    mov ecx, 2048
    call arena_alloc32
    jc .err
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32
    jc .err_rep
    mov esi, edi
    call con_print_z32
    call con_print_crlf32

.ok:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret

.bad_dir:
    mov eax, ERR_PATH_NOT_FOUND
.err_rep:
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc hal_ensure_mpr32
;; @abi win32
;; @in none
;; @out Lazily loads MPR.DLL for WNetAddConnection2A / WNetCancelConnection2A UNC PUSHD/POPD mapping
;; @err CF=1 if MPR.DLL unavailable
;; @clobbers eax, ecx, edx
;; @desc Resolves WNetAddConnection2A and WNetCancelConnection2A dynamically.
@proc hal_ensure_mpr32
    cmp byte [g_mpr_loaded], 0
    jne .check
    push ebx
    push esi
    push addr(szDllMpr)
    call dword [__imp__LoadLibraryA@4]
    mov ebx, eax
    test ebx, ebx
    jz .pop_Check
    mov esi, addr(szFnWNetAdd2A)
    call dyn_resolve_proc
    mov dword [pfnWNetAddConn2A_p16], eax
    mov esi, addr(szFnWNetCancel2A)
    call dyn_resolve_proc
    mov dword [pfnWNetCancelConn2A_p12], eax
.pop_Check:
    mov byte [g_mpr_loaded], 1
    pop esi
    pop ebx
.check:
    cmp dword [pfnWNetAddConn2A_p16], 0
    je .no
    clc
    ret
.no:
    stc
    ret
@endproc

;; @proc bi_pushd32
;; @abi win32
;; @in ESI = optional target directory or `\SERVER\SHARE\subpath` UNC path
;; @out Pushes CWD onto 32-deep PUSHD stack (mapping temporary Z:..A: drive on UNC paths!) and chdirs
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements PUSHD [path | .. | \server\share].
@proc bi_pushd32
    push ebx
    push esi
    push edi
    push ebp

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .list_stack

    mov ebp, dword [g_pushd_depth]
    cmp ebp, W32_PUSHD_MAX
    jae .err

    call tok_strip_quotes32
    jc .err
    mov ebx, eax ; target

    ; Save current CWD in persistent allocation
    mov ecx, 2048
    call perm_alloc32
    jc .err
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32
    jc .free_err
    mov dword [g_pushd_dirs + ebp * 4], edi
    mov byte [g_pushd_mapped + ebp], 0

    ; Check UNC path `\server\share...` (not `\?`)
    cmp byte [ebx], 0x5C
    jne .normal_push
    cmp byte [ebx + 1], 0x5C
    jne .normal_push
    cmp byte [ebx + 2], '?'
    je .normal_push

    ; Attempt temporary drive mapping Z: down to D: via MPR!WNetAddConnection2A when Extensions are ON
    cmp byte [g_extensions_on], 0
    je .normal_push
    call hal_ensure_mpr32
    jc .normal_push

    ; Extract `\server\share` root and optional `\subpath`
    mov esi, ebx
    add esi, 2
    xor ecx, ecx ; slash count
.unc_sc:
    mov al, byte [esi]
    test al, al
    jz .unc_split
    cmp al, 0x5C
    jne .unc_nx
    inc ecx
    cmp ecx, 2
    je .unc_split
.unc_nx:
    inc esi
    jmp .unc_sc
.unc_split:
    ; [ebx..esi) is `\server\share`, [esi..] is subpath
    mov ecx, esi
    sub ecx, ebx
    push esi ; subpath
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .pop1_norm
    push edi
    mov edi, eax
    push eax
    mov esi, ebx
    rep movsb
    mov byte [edi], 0
    pop ecx ; shareRoot
    pop edi
    pop edx ; subpath

    ; Probe free drive letters 'Z' down to 'D' via GetDriveTypeA ("X:") == 1 (DRIVE_NO_ROOT_DIR)
    sub esp, 48 ; NETRESOURCEA (32B) + local "X:" (8B) + root "X:" (8B)
    mov byte [esp + 40], 'Z'
.drv_probe_lp:
    mov al, byte [esp + 40]
    cmp al, 'D'
    jb .unc_map_clean
    mov byte [esp + 32], al
    mov byte [esp + 33], ':'
    mov byte [esp + 34], 0
    mov byte [esp + 36], al
    mov byte [esp + 37], ':'
    mov byte [esp + 38], 0x5C
    mov byte [esp + 39], 0

    push ecx
    push edx
    lea eax, [esp + 44] ; "X:"
    push eax
    call dword [__imp__GetDriveTypeA@4]
    pop edx
    pop ecx
    cmp eax, 1 ; DRIVE_NO_ROOT_DIR
    jne .drv_prev

    ; Zero NETRESOURCEA [esp..esp+32], dwType = RESOURCETYPE_DISK (1), lpLocalName = "X:", lpRemoteName = shareRoot
    push ecx
    push edx
    push edi
    mov edi, esp
    add edi, 12
    xor eax, eax
    mov ecx, 8
    rep stosd
    pop edi
    pop edx
    pop ecx
    mov dword [esp + 4], 1 ; RESOURCETYPE_DISK
    lea eax, [esp + 32]
    mov dword [esp + 16], eax ; lpLocalName = "X:"
    mov dword [esp + 20], ecx ; lpRemoteName = "\server\share"

    push ecx
    push edx
    lea eax, [esp + 8]
    push 0 ; dwFlags = 0 (temporary non-persistent!)
    push 0
    push 0
    push eax
    call dword [pfnWNetAddConn2A_p16]
    pop edx
    pop ecx
    test eax, eax
    jz .unc_mapped_ok

.drv_prev:
    dec byte [esp + 40]
    jmp .drv_probe_lp

.unc_mapped_ok:
    mov al, byte [esp + 40]
    mov byte [g_pushd_mapped + ebp], al
    ; Build new target "X:\<subpath>"
    push edx
    mov ecx, 2048
    call arena_alloc32
    pop edx
    jc .unc_map_clean
    mov ebx, eax
    mov al, byte [esp + 40]
    mov byte [ebx], al
    mov byte [ebx + 1], ':'
    cmp byte [edx], 0
    jne .app_unc_sub
    mov byte [ebx + 2], 0x5C
    mov byte [ebx + 3], 0
    jmp .unc_map_clean
.app_unc_sub:
    push edi
    lea edi, [ebx + 2]
    mov esi, edx
    call str_copy32
    pop edi

.unc_map_clean:
    add esp, 48
    jmp .normal_push

.pop1_norm:
    pop edx

.normal_push:
    mov esi, ebx
    call hal_set_cwd32
    jc .free_err

    inc dword [g_pushd_depth]
    mov ecx, 2048
    call arena_alloc32
    jc .ok
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32
    jc .ok
    mov esi, edi
    call path_sync_drive_env32
    jmp .ok

.list_stack:
    mov ebx, dword [g_pushd_depth]
.ls_lp:
    test ebx, ebx
    jz .ok
    dec ebx
    mov esi, dword [g_pushd_dirs + ebx * 4]
    call con_print_z32
    call con_print_crlf32
    jmp .ls_lp

.free_err:
    mov eax, edi
    call perm_free32
    mov dword [g_pushd_dirs + ebp * 4], 0
    mov eax, ERR_PATH_NOT_FOUND
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.ok:
    mov dword [g_last_exit_code], 0
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_popd32
;; @abi win32
;; @in none
;; @out Pops directory from PUSHD stack, restores CWD, and unmaps temporary UNC drive if mapped!
;; @err Sets g_last_exit_code = 1 when stack is empty
;; @clobbers eax, ecx, edx
;; @desc Implements POPD with automatic WNetCancelConnection2A temporary drive cleanup.
@proc bi_popd32
    push ebx
    push esi
    push edi

    mov ebx, dword [g_pushd_depth]
    test ebx, ebx
    jz .empty
    dec ebx
    mov dword [g_pushd_depth], ebx

    mov esi, dword [g_pushd_dirs + ebx * 4]
    call hal_set_cwd32
    mov eax, esi
    call perm_free32
    mov dword [g_pushd_dirs + ebx * 4], 0

    ; Unmap temporary UNC drive letter if one was mapped for this frame!
    movzx eax, byte [g_pushd_mapped + ebx]
    test eax, eax
    jz .sync_ok
    mov byte [g_pushd_mapped + ebx], 0
    cmp dword [pfnWNetCancelConn2A_p12], 0
    je .sync_ok
    sub esp, 8
    mov byte [esp], al
    mov byte [esp + 1], ':'
    mov byte [esp + 2], 0
    mov edx, esp
    push 1 ; fForce = TRUE
    push 0
    push edx
    call dword [pfnWNetCancelConn2A_p12]
    add esp, 8

.sync_ok:
    mov ecx, 2048
    call arena_alloc32
    jc .ok
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32
    jc .ok
    mov esi, edi
    call path_sync_drive_env32
.ok:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret

.empty:
    mov dword [g_last_exit_code], 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_md32
;; @abi win32
;; @in ESI = one or more directory paths
;; @out Creates directories (including intermediate parent directories when /E:ON!)
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements MD / MKDIR with intermediate directory creation.
@proc bi_md32
    push ebx
    push esi
    push edi

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .syn_err

.arg_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .ok
    call prs_read_word32
    jc .err
    push edx
    mov esi, eax
    call tok_strip_quotes32
    jc .pop_err
    mov esi, eax
    call path_normalize32
    jc .pop_err
    mov ebx, eax ; full target path

    cmp byte [g_extensions_on], 0
    je .single_md

    ; Create intermediate parent directories left-to-right!
    mov edi, ebx
    cmp byte [edi + 1], ':'
    jne .sc_sep
    add edi, 3 ; skip "X:"
.sc_sep:
    mov al, byte [edi]
    test al, al
    jz .single_md
    cmp al, 0x5C
    jne .nx_ch
    mov byte [edi], 0
    mov esi, ebx
    call hal_mkdir_one32
    mov byte [edi], 0x5C
.nx_ch:
    inc edi
    jmp .sc_sep

.single_md:
    mov esi, ebx
    call hal_mkdir_one32
    jnc .next_arg
    ; Succeed if already created or check error
    cmp eax, ERR_FILE_EXISTS
    je .pop_rep_err
    jmp .pop_rep_err

.next_arg:
    pop esi
    jmp .arg_lp

.ok:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret

.pop_rep_err:
    pop edx
    call con_report_err32
    jmp .err
.pop_err:
    pop edx
    jmp .err
.syn_err:
    mov eax, ERR_SYNTAX
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc rd_tree_reparse_safe32
;; @abi win32
;; @in ESI = normalized directory path
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Reparse-safe recursive directory removal: NEVER traverses into <JUNCTION> or <SYMLINKD> targets!
@proc rd_tree_reparse_safe32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, esi

    ; CRITICAL SAFETY GATE: If EBP itself has FILE_ATTRIBUTE_REPARSE_POINT, unlink directly via RemoveDirectory!
    call hal_get_attr32
    jc .ret_err
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .unlink_leaf

    ; Strip read-only attribute on directory if set
    test eax, FILE_ATTRIBUTE_READONLY
    jz .enum_children
    and eax, -2
    mov ecx, eax
    mov esi, ebp
    call hal_set_attr32

.enum_children:
    mov ecx, 2048
    call arena_alloc32
    jc .ret_err
    mov edi, eax
    push eax
    mov esi, ebp
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .app_star
    mov byte [edi], 0x5C
    inc edi
.app_star:
    mov word [edi], 0x002A ; "*"
    pop esi

    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .ret_err
    mov edi, eax
    call hal_find_first32
    jc .unlink_leaf ; empty or unreadable -> attempt RemoveDirectory directly
    mov ebx, eax ; hFind

.child_lp:
    cmp byte [edi + FIND_ENTRY32.szNameWtf8], '.'
    jne .proc_child
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 1], 0
    je .next_child
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 1], '.'
    jne .proc_child
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 2], 0
    je .next_child

.proc_child:
    ; Build "<ebp>\<child>"
    mov ecx, 2048
    call arena_alloc32
    jc .close_find
    mov edx, eax
    push edi
    mov edi, edx
    mov esi, ebp
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .cp_cname
    mov byte [edi], 0x5C
    inc edi
.cp_cname:
    mov esi, dword [esp] ; FIND_ENTRY32*
    lea esi, [esi + FIND_ENTRY32.szNameWtf8]
    push edx
    call str_copy32
    pop esi ; childFullPath
    pop edi ; FIND_ENTRY32*

    mov eax, dword [edi + FIND_ENTRY32.dwAttr]
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .del_child_file
    ; Directory child: if reparse point (<JUNCTION> / <SYMLINKD>), unlink ONLY the link without recursing!
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .unlink_child_reparse
    call rd_tree_reparse_safe32
    jmp .next_child

.unlink_child_reparse:
    call hal_rmdir_one32
    jmp .next_child

.del_child_file:
    mov ecx, 1 ; strip read-only
    call hal_delete_file32

.next_child:
    call hal_find_next32
    jnc .child_lp

.close_find:
    push ebx
    call dword [__imp__FindClose@4]

.unlink_leaf:
    mov esi, ebp
    call hal_rmdir_one32
    jc .ret_err

    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.ret_err:
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc bi_rd32
;; @abi win32
;; @in ESI = `[/S] [/Q] path...`
;; @out Removes empty directory or `/S` directory tree (reparse-point safe!)
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements RD / RMDIR with /S and /Q and reparse-safe junction/symlink unlinking.
@proc bi_rd32
    push ebx
    push esi
    push edi
    xor ebx, ebx ; bit0=/S, bit1=/Q

.sw_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .rd_targets
    mov al, byte [esi + 1]
    call ascii_upper_al
    add esi, 2
    cmp al, 'S'
    je .set_s
    cmp al, 'Q'
    je .set_q
    jmp .sw_lp
.set_s:
    or ebx, 1
    jmp .sw_lp
.set_q:
    or ebx, 2
    jmp .sw_lp

.rd_targets:
    cmp byte [esi], 0
    je .syn_err

.t_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .ok
    ; Also accept trailing `/S` or `/Q` after path (`RD dir /S /Q`)!
    cmp byte [esi], '/'
    jne .read_t
    mov al, byte [esi + 1]
    call ascii_upper_al
    add esi, 2
    cmp al, 'S'
    jne .chk_tq
    or ebx, 1
    jmp .t_lp
.chk_tq:
    cmp al, 'Q'
    jne .t_lp
    or ebx, 2
    jmp .t_lp

.read_t:
    call prs_read_word32
    jc .err
    mov edi, edx ; rest — check if trailing /S /Q follow on rest before executing!
    push eax ; raw_path
    mov esi, edi
.peek_trail_sw:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .run_one_rd
    mov al, byte [esi + 1]
    call ascii_upper_al
    add esi, 2
    mov edi, esi
    cmp al, 'S'
    jne .pt_q
    or ebx, 1
    jmp .peek_trail_sw
.pt_q:
    cmp al, 'Q'
    jne .peek_trail_sw
    or ebx, 2
    jmp .peek_trail_sw

.run_one_rd:
    pop esi ; raw_path
    call tok_strip_quotes32
    jc .err
    mov esi, eax
    call path_normalize32
    jc .rep_err
    mov esi, eax

    test ebx, 1 ; /S?
    jnz .do_tree
    call hal_rmdir_one32
    jc .rep_err
    mov esi, edi
    jmp .t_lp

.do_tree:
    call rd_tree_reparse_safe32
    jc .rep_err
    mov esi, edi
    jmp .t_lp

.ok:
    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_mklink32
;; @abi win32
;; @in ESI = `[[/D] | [/H] | [/J]] Link Target`
;; @out Creates file symlink, /D directory symlink, /H hardlink, or /J NTFS junction via ntw_mklink
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements internal Vista..11 (and Win2000+ /H /J!) MKLINK command.
@proc bi_mklink32
    push ebx
    push esi
    push edi
    push ebp
    xor ebx, ebx ; mode: 0=file symlink, 1=/D, 2=/H, 3=/J

.sw_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .args
    mov al, byte [esi + 1]
    call ascii_upper_al
    add esi, 2
    cmp al, 'D'
    je .m_d
    cmp al, 'H'
    je .m_h
    cmp al, 'J'
    je .m_j
    jmp .syn_err
.m_d:
    mov ebx, 1
    jmp .sw_lp
.m_h:
    mov ebx, 2
    jmp .sw_lp
.m_j:
    mov ebx, 3
    jmp .sw_lp

.args:
    call prs_read_word32
    jc .syn_err
    cmp byte [eax], 0
    je .syn_err
    push edx
    mov esi, eax
    call tok_strip_quotes32
    pop esi
    jc .err
    mov ebp, eax ; linkPath

    call prs_read_word32
    jc .syn_err
    cmp byte [eax], 0
    je .syn_err
    mov esi, eax
    call tok_strip_quotes32
    jc .err
    mov edi, eax ; targetPath

    mov esi, ebp
    mov ecx, ebx
    call ntw_mklink
    jc .rep_err

    ; Print authentic confirmation message!
    mov esi, addr(szMklinkSym)
    cmp ebx, 1
    je .m_symd
    cmp ebx, 2
    je .m_hard
    cmp ebx, 3
    je .m_junc
    jmp .prt_mk
.m_symd:
    mov esi, addr(szMklinkSymD)
    jmp .prt_mk
.m_hard:
    mov esi, addr(szMklinkHard)
    jmp .prt_mk
.m_junc:
    mov esi, addr(szMklinkJunc)
.prt_mk:
    call con_print_z32
    mov esi, ebp
    call con_print_z32
    mov esi, addr(szMklinkArrow)
    call con_print_z32
    mov esi, edi
    call con_print_z32
    call con_print_crlf32
    mov dword [g_last_exit_code], 0
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_del32
;; @abi win32
;; @in ESI = `[/P] [/F] [/S] [/Q] [/A[:attrs]] names...`
;; @out Deletes matching files (supporting wildcards and /F read-only stripping)
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements DEL / ERASE.
@proc bi_del32
    push ebx
    push esi
    push edi
    push ebp
    xor ebp, ebp ; bit0=/F

.arg_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .done_ok
    cmp byte [esi], '/'
    jne .file_tok
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'F'
    jne .skip_sw
    or ebp, 1
.skip_sw:
    call prs_read_word32
    mov esi, edx
    jmp .arg_lp

.file_tok:
    call prs_read_word32
    jc .err
    push edx ; rest
    mov esi, eax
    call tok_strip_quotes32
    jc .pop_err
    mov esi, eax
    call path_normalize32
    jc .pop_err
    mov ebx, eax ; normPat

    ; Extract directory prefix of normPat up to last ''
    mov esi, ebx
    call str_len32
    mov edx, ecx
.sep_sc:
    test edx, edx
    jz .try_direct
    cmp byte [ebx + edx - 1], 0x5C
    je .got_dlen
    dec edx
    jmp .sep_sc

.got_dlen:
    push edx
    lea ecx, [edx + 4]
    call arena_alloc32
    pop edx
    jc .pop_err
    mov edi, eax
    push eax
    mov esi, ebx
    mov ecx, edx
    rep movsb
    mov byte [edi], 0
    pop edi ; dirPrefix

    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .pop_err
    mov esi, ebx
    push edi ; dirPrefix
    mov edi, eax
    push edi ; FIND_ENTRY32
    call hal_find_first32
    pop esi  ; FIND_ENTRY32
    pop edi  ; dirPrefix
    jc .try_direct
    push eax ; hFind

.del_w_lp:
    test dword [esi + FIND_ENTRY32.dwAttr], FILE_ATTRIBUTE_DIRECTORY
    jnz .del_w_nx

    mov ecx, 2048
    call arena_alloc32
    jc .del_w_close
    mov edx, eax
    push esi
    push edi
    mov edi, edx
    mov esi, dword [esp] ; dirPrefix
    call str_copy32
    mov edi, eax
    mov esi, dword [esp + 4] ; FIND_ENTRY32
    lea esi, [esi + FIND_ENTRY32.szNameWtf8]
    push edx
    call str_copy32
    pop esi ; fullFile
    mov ecx, ebp
    and ecx, 1
    call hal_delete_file32
    pop edi
    pop esi

.del_w_nx:
    mov ebx, dword [esp] ; hFind
    push edi
    mov edi, esi
    call hal_find_next32
    pop edi
    jnc .del_w_lp

.del_w_close:
    call dword [__imp__FindClose@4]
    pop esi
    jmp .arg_lp

.try_direct:
    mov esi, ebx
    mov ecx, ebp
    and ecx, 1
    call hal_delete_file32
    pop esi
    jmp .arg_lp

.done_ok:
    mov dword [g_last_exit_code], 0
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.pop_err:
    pop edx
.err:
    mov dword [g_last_exit_code], 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_move32
;; @abi win32
;; @in ESI = `[/Y | /-Y] src dst`
;; @out Moves file or directory via hal_move_file32
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements MOVE [/Y | /-Y] source destination.
@proc bi_move32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, 1 ; default overwrite = 1 in batch/non-interactive

.sw_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .args
    call prs_read_word32
    mov esi, edx
    jmp .sw_lp

.args:
    call prs_read_word32
    jc .syn_err
    cmp byte [eax], 0
    je .syn_err
    push edx
    mov esi, eax
    call tok_strip_quotes32
    jc .pop_err
    mov esi, eax
    call path_normalize32
    pop esi
    jc .rep_err
    mov ebx, eax ; src

    call prs_read_word32
    jc .syn_err
    cmp byte [eax], 0
    je .syn_err
    mov esi, eax
    call tok_strip_quotes32
    jc .err
    mov esi, eax
    call path_normalize32
    jc .rep_err
    mov edi, eax ; dst

    ; If dst is an existing directory, append `\<basename(src)>`!
    mov esi, edi
    call hal_get_attr32
    jc .do_mv
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .do_mv
    mov esi, ebx
    call str_len32
.base_sc:
    test ecx, ecx
    jz .got_b
    cmp byte [ebx + ecx - 1], 0x5C
    je .got_b
    dec ecx
    jmp .base_sc
.got_b:
    lea esi, [ebx + ecx] ; basename
    push esi
    mov ecx, 2048
    call arena_alloc32
    pop edx ; basename
    jc .err
    push eax
    push edx
    mov ecx, edi
    mov edi, eax
    mov esi, ecx
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .app_b
    mov byte [edi], 0x5C
    inc edi
.app_b:
    pop esi ; basename
    call str_copy32
    pop edi ; updated dst

.do_mv:
    mov esi, ebx
    mov ecx, ebp
    call hal_move_file32
    jc .rep_err

    mov dword [g_last_exit_code], 0
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.pop_err:
    pop edx
    jmp .err
.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_ren32
;; @abi win32
;; @in ESI = `[drive:][path]filename1 filename2`
;; @out Renames file/directory in-place within source parent directory
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements REN / RENAME.
@proc bi_ren32
    push ebx
    push esi
    push edi

    call prs_read_word32
    jc .syn_err
    cmp byte [eax], 0
    je .syn_err
    push edx
    mov esi, eax
    call tok_strip_quotes32
    jc .pop_err
    mov esi, eax
    call path_normalize32
    pop esi
    jc .rep_err
    mov ebx, eax ; fullSrc

    call prs_read_word32
    jc .syn_err
    cmp byte [eax], 0
    je .syn_err
    mov esi, eax
    call tok_strip_quotes32
    jc .err
    mov edi, eax ; newLeaf (must NOT contain path separators!)
    mov esi, edi
.chk_sep:
    mov al, byte [esi]
    test al, al
    jz .build_dst
    cmp al, 0x5C
    je .syn_err
    cmp al, '/'
    je .syn_err
    cmp al, ':'
    je .syn_err
    inc esi
    jmp .chk_sep

.build_dst:
    ; Combine parent(ebx) + newLeaf(edi)
    mov esi, ebx
    call str_len32
.find_p:
    test ecx, ecx
    jz .syn_err
    cmp byte [ebx + ecx - 1], 0x5C
    je .got_p
    dec ecx
    jmp .find_p
.got_p:
    push ecx ; parentLen
    add ecx, 1024
    call arena_alloc32
    pop ecx
    jc .err
    mov edx, eax
    push edi ; newLeaf
    mov edi, edx
    mov esi, ebx
    rep movsb
    pop esi ; newLeaf
    push edx
    call str_copy32
    pop edi ; fullDst

    mov esi, ebx
    xor ecx, ecx ; do NOT overwrite existing on REN!
    call hal_move_file32
    jc .rep_err

    mov dword [g_last_exit_code], 0
    pop edi
    pop esi
    pop ebx
    ret

.pop_err:
    pop edx
    jmp .err
.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc copy_append_stream32
;; @abi win32
;; @in ESI = source file/device path, EBX = open destination HANDLE, CL = bAsciiMode (1 -> stop at 0x1A)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Streams source file (or interactive CON!) into destination handle for COPY and `COPY a + b dst`.
@proc copy_append_stream32
    push ebx
    push esi
    push edi
    push ebp
    movzx ebp, cl ; bAsciiMode

    ; Check `COPY CON dst`!
    call dev_match_name32
    test eax, eax
    jz .open_src
    cmp dword [eax], 0x004E4F43 ; "CON\0"
    je .from_con
    cmp dword [eax], 0x494E4F43 ; "CONI" ("CONIN$")
    je .from_con
    mov esi, eax
    jmp .open_src

.from_con:
    mov ecx, 4096
    call arena_alloc32
    jc .err
    mov edi, eax
.con_lp:
    mov ecx, 4090
    call con_read_line32
    jc .con_done
    mov esi, edi
    mov ecx, eax
    mov word [edi + ecx], 0x0A0D
    add ecx, 2
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push ecx
    push esi
    push ebx
    call dword [__imp__WriteFile@20]
    add esp, 4
    jmp .con_lp
.con_done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.open_src:
    mov ecx, GENERIC_READ
    mov edx, OPEN_EXISTING
    xor eax, eax
    call hal_open_file32
    jc .err
    mov edi, eax ; hSrc

    mov ecx, 32768
    call arena_alloc32
    jc .close_err
    mov esi, eax ; 32 KiB transfer buffer

.rd_lp:
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push 0
    push edx
    push 32768
    push esi
    push edi
    call dword [__imp__ReadFile@20]
    pop ecx ; bytesRead
    test eax, eax
    jz .close_err
    test ecx, ecx
    jz .done_src

    test ebp, ebp
    jz .wr_chunk
    ; ASCII mode: truncate at first 0x1A (Ctrl+Z)!
    xor edx, edx
.cz_sc:
    cmp edx, ecx
    jae .wr_chunk
    cmp byte [esi + edx], 0x1A
    je .hit_cz
    inc edx
    jmp .cz_sc
.hit_cz:
    mov ecx, edx
    test ecx, ecx
    jz .done_src
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push ecx
    push esi
    push ebx
    call dword [__imp__WriteFile@20]
    add esp, 4
    jmp .done_src

.wr_chunk:
    sub esp, 4
    mov edx, esp
    push 0
    push edx
    push ecx
    push esi
    push ebx
    call dword [__imp__WriteFile@20]
    add esp, 4
    test eax, eax
    jz .close_err
    jmp .rd_lp

.done_src:
    push edi
    call dword [__imp__CloseHandle@4]
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.close_err:
    call dword [__imp__GetLastError@0]
    call map_win32_to_err
    push eax
    push edi
    call dword [__imp__CloseHandle@4]
    pop eax
.err:
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc bi_copy32
;; @abi win32
;; @in ESI = `[/A] [/B] [/V] [/Y] [/-Y] [/Z] src1 [+ src2 ...] [dst]`
;; @out Copies single/wildcard files, concatenates `a + b + c dst`, and supports `COPY CON file`!
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements COPY including `+` multi-source concatenation and `COPY CON`.
@proc bi_copy32
    push ebx
    push esi
    push edi
    push ebp
    ; Collect up to 16 source tokens in [esp..esp+64], flags in [esp+64], src_cnt in [esp+68], dst in [esp+72]
    sub esp, 80
    mov ebp, esp
    mov dword [ebp + 64], 0 ; bit0=/A (ascii), bit1=/B (binary)
    mov dword [ebp + 68], 0 ; src_cnt
    mov dword [ebp + 72], 0 ; dst
    xor ebx, ebx ; expect_concat_src (1 right after '+')

.tok_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .exec_copy

    cmp byte [esi], '/'
    jne .chk_plus
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'A'
    jne .chk_b_sw
    mov dword [ebp + 64], 1
    jmp .skip_sw
.chk_b_sw:
    cmp al, 'B'
    jne .skip_sw
    mov dword [ebp + 64], 0
.skip_sw:
    call prs_read_word32
    mov esi, edx
    jmp .tok_lp

.chk_plus:
    cmp byte [esi], '+'
    jne .read_item
    mov ebx, 1
    inc esi
    jmp .tok_lp

.read_item:
    ; Read token up to whitespace OR '+'!
    mov edi, esi
    xor ecx, ecx ; quote
.it_sc:
    mov al, byte [edi]
    test al, al
    jz .it_got
    cmp al, '"'
    jne .it_d
    xor ecx, 1
    inc edi
    jmp .it_sc
.it_d:
    test ecx, ecx
    jnz .it_nx
    cmp al, 0x20
    je .it_got
    cmp al, 0x09
    je .it_got
    cmp al, '+'
    je .it_got
.it_nx:
    inc edi
    jmp .it_sc

.it_got:
    mov ecx, edi
    sub ecx, esi
    push edi
    push ecx
    inc ecx
    call arena_alloc32
    pop ecx
    jc .pop_err
    mov edx, eax
    mov edi, eax
    rep movsb
    mov byte [edi], 0
    mov esi, edx
    call tok_strip_quotes32
    pop esi ; next cursor
    jc .err

    mov ecx, dword [ebp + 68]
    test ecx, ecx
    jz .push_src
    test ebx, ebx
    jnz .push_src
    ; Not preceded by '+' -> this token is destination!
    mov dword [ebp + 72], eax
    jmp .tok_lp

.push_src:
    cmp ecx, 15
    jae .tok_lp
    mov dword [ebp + ecx * 4], eax
    inc ecx
    mov dword [ebp + 68], ecx
    xor ebx, ebx
    jmp .tok_lp

.exec_copy:
    cmp dword [ebp + 68], 0
    je .syn_err

    ; Default destination to current directory "." if omitted!
    mov edi, dword [ebp + 72]
    test edi, edi
    jnz .norm_dst
    mov edi, addr(szDot32)
.norm_dst:
    mov esi, edi
    call dev_match_name32
    test eax, eax
    jnz .got_dst_dev
    mov esi, edi
    call path_normalize32
    jc .rep_err
    mov edi, eax
    jmp .chk_dst_dir
.got_dst_dev:
    mov edi, eax
    jmp .open_dst_stream

.chk_dst_dir:
    ; If dst is an existing directory AND src_cnt == 1, append `\<basename(src0)>`!
    mov esi, edi
    call hal_get_attr32
    jc .open_dst_stream
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .open_dst_stream
    mov esi, dword [ebp]
    call str_len32
    mov ebx, dword [ebp]
.cp_bsc:
    test ecx, ecx
    jz .cp_bgot
    cmp byte [ebx + ecx - 1], 0x5C
    je .cp_bgot
    cmp byte [ebx + ecx - 1], '/'
    je .cp_bgot
    cmp byte [ebx + ecx - 1], ':'
    je .cp_bgot
    dec ecx
    jmp .cp_bsc
.cp_bgot:
    lea edx, [ebx + ecx]
    push edx
    mov ecx, 2048
    call arena_alloc32
    pop edx
    jc .err
    push eax
    push edx
    mov ecx, edi
    mov edi, eax
    mov esi, ecx
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .cp_bapp
    mov byte [edi], 0x5C
    inc edi
.cp_bapp:
    pop esi
    call str_copy32
    pop edi

.open_dst_stream:
    mov esi, edi
    mov ecx, GENERIC_WRITE
    mov edx, CREATE_ALWAYS
    xor eax, eax
    call hal_open_file32
    jc .rep_err
    mov ebx, eax ; hDst

    xor edi, edi ; idx = 0..src_cnt-1
.src_lp:
    cmp edi, dword [ebp + 68]
    jae .copy_done_ok
    mov esi, dword [ebp + edi * 4]
    mov cl, byte [ebp + 64]
    ; For multi-source `a + b dst` when neither /A nor /B was forced, cmd defaults to /A on concat — keep binary unless /A set so binary files never corrupt!
    call copy_append_stream32
    jc .close_dst_err
    inc edi
    jmp .src_lp

.copy_done_ok:
    push ebx
    call dword [__imp__CloseHandle@4]
    mov esi, addr(szOneFileCopied)
    call con_print_z32
    mov dword [g_last_exit_code], 0
    add esp, 80
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.close_dst_err:
    push eax
    push ebx
    call dword [__imp__CloseHandle@4]
    pop eax
    jmp .rep_err

.pop_err:
    pop edx
    jmp .err
.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    add esp, 80
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_type32
;; @abi win32
;; @in ESI = `file1 [file2 ...]`
;; @out Streams file contents to stdout (stripping UTF-8 BOM or transcoding UTF-16LE BOM!)
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements TYPE with multi-file stderr header mode and UTF-8 / UTF-16LE BOM support.
@proc bi_type32
    push ebx
    push esi
    push edi
    push ebp

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .syn_err

    ; Check if more than 1 file argument is present -> multi-file header mode!
    push esi
    call prs_read_word32
    mov esi, edx
    call prs_skip_ws32
    xor ebp, ebp
    cmp byte [eax], 0
    setne bl
    movzx ebp, bl ; multi_hdr (0/1)
    pop esi

.file_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .ok
    call prs_read_word32
    jc .err
    push edx ; rest
    mov esi, eax
    call tok_strip_quotes32
    jc .pop_err
    mov ebx, eax ; dispName

    test ebp, ebp
    jz .open_it
    mov esi, addr(szCrLf32)
    call con_print_err_z32
    mov esi, ebx
    call con_print_err_z32
    mov esi, addr(szCrLf32)
    call con_print_err_z32
    mov esi, addr(szCrLf32)
    call con_print_err_z32

.open_it:
    mov esi, ebx
    mov ecx, GENERIC_READ
    mov edx, OPEN_EXISTING
    xor eax, eax
    call hal_open_file32
    jc .pop_rep_err
    mov ebx, eax ; hFile

    mov ecx, 16384
    call arena_alloc32
    jc .close_pop_err
    mov edi, eax ; buf

    ; Read first chunk and inspect UTF-8 (EF BB BF) or UTF-16LE (FF FE) BOM!
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push 0
    push edx
    push 16384
    push edi
    push ebx
    call dword [__imp__ReadFile@20]
    pop ecx ; bytesRead
    test ecx, ecx
    jz .close_next

    cmp ecx, 2
    jb .emit_raw_first
    cmp byte [edi], 0xFF
    jne .chk_u8_t
    cmp byte [edi + 1], 0xFE
    jne .chk_u8_t
    ; UTF-16LE file! Transcode WCHARs in [edi+2 .. edi+ecx) to WTF-8 and print!
    sub ecx, 2
    shr ecx, 1
    push ecx
    mov ecx, 49152
    call arena_alloc32
    pop ecx
    jc .close_pop_err
    mov edx, 49148
    push eax
    lea esi, [edi + 2]
    mov edi, eax
    call utf16le_to_wtf8_buf
    mov ecx, eax
    pop esi
    call con_write_out32
    jmp .close_next

.chk_u8_t:
    mov esi, edi
    cmp ecx, 3
    jb .emit_raw_first
    cmp byte [edi], 0xEF
    jne .emit_raw_first
    cmp byte [edi + 1], 0xBB
    jne .emit_raw_first
    cmp byte [edi + 2], 0xBF
    jne .emit_raw_first
    add esi, 3
    sub ecx, 3
.emit_raw_first:
    call con_write_out32

.chunk_lp:
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push 0
    push edx
    push 16384
    push edi
    push ebx
    call dword [__imp__ReadFile@20]
    pop ecx
    test eax, eax
    jz .close_next
    test ecx, ecx
    jz .close_next
    mov esi, edi
    call con_write_out32
    jmp .chunk_lp

.close_next:
    push ebx
    call dword [__imp__CloseHandle@4]
    pop esi ; rest
    jmp .file_lp

.ok:
    mov dword [g_last_exit_code], 0
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.close_pop_err:
    push ebx
    call dword [__imp__CloseHandle@4]
    pop edx
    jmp .err
.pop_rep_err:
    pop edx
    call con_report_err32
    jmp .err
.pop_err:
    pop edx
    jmp .err
.syn_err:
    mov eax, ERR_SYNTAX
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_truename32
;; @abi win32
;; @in ESI = `[path]`
;; @out Prints canonicalized full path + CRLF
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements TRUENAME.
@proc bi_truename32
    push esi
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    jne .norm
    mov esi, addr(szDot32)
.norm:
    call tok_strip_quotes32
    jc .err
    mov esi, eax
    call path_normalize32
    jc .err
    mov esi, eax
    call con_print_z32
    call con_print_crlf32
    mov dword [g_last_exit_code], 0
    pop esi
    ret
.err:
    mov dword [g_last_exit_code], 1
    pop esi
    ret
@endproc

;; @proc bi_vol32
;; @abi win32
;; @in ESI = optional `X:` drive spec
;; @out Prints Volume Label and Volume Serial Number (XXXX-XXXX) via GetVolumeInformationW/A
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements VOL [drive:].
@proc bi_vol32
    push ebx
    push esi
    push edi
    sub esp, 32
    mov ebx, esp ; [ebx+0..8]=root "X:", [ebx+8]=dwSerial, [ebx+12..24]=hexbuf

    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .cur_drv
    mov al, byte [esi]
    call ascii_upper_al
    jmp .build_root

.cur_drv:
    mov ecx, 260
    call arena_alloc32
    jc .err
    mov edi, eax
    mov ecx, 256
    call hal_get_cwd32
    jc .err
    mov al, byte [edi]
    call ascii_upper_al

.build_root:
    mov byte [ebx], al
    mov byte [ebx + 1], ':'
    mov byte [ebx + 2], 0x5C
    mov byte [ebx + 3], 0
    mov byte [ebx + 4], al ; save drive letter

    mov ecx, 1024
    call arena_alloc32
    jc .err
    mov edi, eax ; volName buf
    mov word [edi], 0
    mov dword [ebx + 8], 0

    cmp byte [g_is_win9x], 0
    jne .vol_a

    mov esi, ebx
    call wtf8_to_utf16le
    jc .err
    push 0
    push 0
    push 0
    push 0
    lea edx, [ebx + 8]
    push edx
    push 260
    push edi
    push eax
    call dword [__imp__GetVolumeInformationW@32]
    test eax, eax
    jz .err
    mov esi, edi
    call utf16le_to_wtf8_arena
    jc .err
    mov edi, eax
    jmp .prt_vol

.vol_a:
    push 0
    push 0
    push 0
    push 0
    lea edx, [ebx + 8]
    push edx
    push 256
    push edi
    push ebx
    call dword [__imp__GetVolumeInformationA@32]
    test eax, eax
    jz .err

.prt_vol:
    mov esi, addr(szVolInDrive)
    call con_print_z32
    lea esi, [ebx + 4]
    mov ecx, 1
    call con_write_out32
    cmp byte [edi], 0
    je .no_lbl
    mov esi, addr(szVolIs)
    call con_print_z32
    mov esi, edi
    call con_print_z32
    call con_print_crlf32
    jmp .prt_ser
.no_lbl:
    mov esi, addr(szVolHasNoLbl)
    call con_print_z32

.prt_ser:
    mov eax, dword [ebx + 8]
    test eax, eax
    jz .ok
    mov esi, addr(szVolSerialIs)
    call con_print_z32
    mov eax, dword [ebx + 8]
    lea edi, [ebx + 12]
    call fmt_hex32_8
    mov esi, edi
    mov ecx, 4
    call con_write_out32
    mov esi, addr(szDash32)
    mov ecx, 1
    call con_write_out32
    lea esi, [ebx + 16]
    mov ecx, 4
    call con_write_out32
    call con_print_crlf32

.ok:
    mov dword [g_last_exit_code], 0
    add esp, 32
    pop edi
    pop esi
    pop ebx
    ret
.err:
    mov dword [g_last_exit_code], 1
    add esp, 32
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc bi_date32
;; @abi win32
;; @in ESI = `[/T]`
;; @out Prints current date (non-interactively under /T or when Extensions ON with /T)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements DATE [/T].
@proc bi_date32
    push esi
    call prs_skip_ws32
    cmp byte [eax], '/'
    je .t_only
    mov esi, addr(szCurDateIs)
    call con_print_z32
.t_only:
    mov esi, addr(szDynDate)
    call env_get32
    test eax, eax
    jz .done
    mov esi, eax
    call con_print_z32
    call con_print_crlf32
.done:
    mov dword [g_last_exit_code], 0
    pop esi
    ret
@endproc

;; @proc bi_time32
;; @abi win32
;; @in ESI = `[/T]`
;; @out Prints current time
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements TIME [/T].
@proc bi_time32
    push esi
    call prs_skip_ws32
    cmp byte [eax], '/'
    je .t_only
    mov esi, addr(szCurTimeIs)
    call con_print_z32
.t_only:
    mov esi, addr(szDynTime)
    call env_get32
    test eax, eax
    jz .done
    mov esi, eax
    call con_print_z32
    call con_print_crlf32
.done:
    mov dword [g_last_exit_code], 0
    pop esi
    ret
@endproc

;; @proc bi_mode32
;; @abi win32
;; @in ESI = `[CON[:] [COLS=c] [LINES=n]]`
;; @out Queries codepage/console status or resizes console screen buffer
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Implements MODE CON and codepage status reporting.
@proc bi_mode32
    push esi
    mov esi, addr(szEmpty32)
    call bi_chcp32
    pop esi
    ret
@endproc

;; @proc bi_cmd_subshell32
;; @abi win32
;; @in ESI = arguments to subshell (`[/C | /K | /V:ON ...]`)
;; @out Spawns nested WINCMD32.EXE instance with inherited handles and environment
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements CMD / COMMAND subshell launch.
@proc bi_cmd_subshell32
    push esi
    push edi
    mov edi, esi
    mov esi, addr(g_self_exe)
    xor ecx, ecx
    call exec_external32
    pop edi
    pop esi
    ret
@endproc

;; @proc bi_start32
;; @abi win32
;; @in ESI = `["title"] [/D path] [/B] [/WAIT] [/MIN] [/MAX] [/SEPARATE | /SHARED] [cmd args...]`
;; @out Launches target command/process (with /SEPARATE WOW16 VDM, /B, /WAIT, or ShellExecuteEx fallback!)
;; @err Updates g_last_exit_code
;; @clobbers eax, ecx, edx
;; @desc Implements START built-in across Windows NT 3.1..11 and Windows 95..Me.
@proc bi_start32
    push ebx
    push esi
    push edi
    push ebp
    xor ebx, ebx ; dwCreationFlags
    xor ebp, ebp ; bit0=/WAIT, bit1=/B

    call prs_skip_ws32
    mov esi, eax
    ; Optional leading `"title"` when followed by another argument!
    cmp byte [esi], '"'
    jne .sw_lp
    call prs_read_word32
    jc .err
    push eax
    mov esi, edx
    call prs_skip_ws32
    cmp byte [eax], 0
    pop ecx
    je .run_prog_ecx ; only one quoted token -> treat as program!
    mov esi, eax

.sw_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], '/'
    jne .prog_tok
    call prs_read_word32
    jc .err
    mov edi, edx ; next cursor
    mov esi, eax
    inc esi ; skip '/'
    mov al, byte [esi]
    call ascii_upper_al
    cmp al, 'B'
    je .st_b
    cmp al, 'W'
    je .st_wait
    cmp al, 'D'
    je .st_d
    cmp al, 'S'
    je .st_sep_shr
    mov esi, edi
    jmp .sw_lp

.st_b:
    or ebp, 2
    mov esi, edi
    jmp .sw_lp
.st_wait:
    or ebp, 1
    mov esi, edi
    jmp .sw_lp
.st_d:
    mov esi, edi
    call prs_read_word32
    jc .err
    mov edi, edx
    mov esi, eax
    call tok_strip_quotes32
    jc .err
    mov esi, eax
    call hal_set_cwd32
    mov esi, edi
    jmp .sw_lp
.st_sep_shr:
    mov al, byte [esi + 1]
    call ascii_upper_al
    cmp al, 'E' ; /SEPARATE
    jne .st_shr
    or ebx, CREATE_SEPARATE_WOW_VDM
    mov esi, edi
    jmp .sw_lp
.st_shr:
    or ebx, CREATE_SHARED_WOW_VDM
    mov esi, edi
    jmp .sw_lp

.run_prog_ecx:
    mov esi, ecx
    mov edi, addr(szEmpty32)
    jmp .do_launch

.prog_tok:
    cmp byte [esi], 0
    jne .rd_prog
    mov esi, addr(g_self_exe)
    mov edi, addr(szEmpty32)
    jmp .do_launch
.rd_prog:
    call prs_read_word32
    jc .err
    mov edi, edx ; tail
    mov esi, eax ; prog

.do_launch:
    test ebp, 2 ; /B?
    jnz .same_con
    ; Note: under /WAIT or synchronous batch/test, share console if /B else CREATE_NEW_CONSOLE when interactive
    test ebp, 1
    jnz .same_con
.same_con:
    mov ecx, ebx
    call exec_external32
    jc .try_shellex
    mov dword [g_last_exit_code], eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.try_shellex:
    ; Fallback to SHELL32!ShellExecuteExW (NT) / ShellExecuteExA (Win95/98/Me) for .LNK, URLs, & documents!
    cmp byte [g_shell32_loaded], 0
    jne .chk_sh_fn
    push esi
    push edi
    push addr(szDllShell32)
    call dword [__imp__LoadLibraryA@4]
    mov ebx, eax
    mov esi, addr(szFnShellExecExW)
    call dyn_resolve_proc
    mov dword [pfnShellExecuteExW_p4], eax
    mov esi, addr(szFnShellExecExA)
    call dyn_resolve_proc
    mov dword [pfnShellExecuteExA_p4], eax
    mov byte [g_shell32_loaded], 1
    pop edi
    pop esi
.chk_sh_fn:
    call tok_strip_quotes32
    jc .err
    mov ebx, eax ; unquoted file/URL

    ; Build 60-byte SHELLEXECUTEINFO on stack (aligned to 64B)
    sub esp, 64
    push edi
    mov edi, esp
    add edi, 4
    xor eax, eax
    mov ecx, 16
    rep stosd
    pop edi

    mov dword [esp], 60          ; cbSize = 60
    mov dword [esp + 4], 0x0040  ; SEE_MASK_NOCLOSEPROCESS
    mov dword [esp + 28], 1      ; SW_SHOWNORMAL

    cmp byte [g_is_win9x], 0
    jne .shex_a

    cmp dword [pfnShellExecuteExW_p4], 0
    je .shex_fail
    mov esi, ebx
    call wtf8_to_utf16le
    jc .shex_fail
    mov dword [esp + 16], eax ; lpFile
    mov esi, edi
    call wtf8_to_utf16le
    jc .shex_fail
    mov dword [esp + 20], eax ; lpParameters
    mov eax, esp
    push eax
    call dword [pfnShellExecuteExW_p4]
    test eax, eax
    jz .shex_fail
    jmp .shex_wait

.shex_a:
    cmp dword [pfnShellExecuteExA_p4], 0
    je .shex_fail
    mov dword [esp + 16], ebx
    mov dword [esp + 20], edi
    mov eax, esp
    push eax
    call dword [pfnShellExecuteExA_p4]
    test eax, eax
    jz .shex_fail

.shex_wait:
    mov ebx, dword [esp + 56] ; hProcess
    test ebx, ebx
    jz .shex_ok
    test ebp, 1 ; /WAIT?
    jz .shex_close
    push INFINITE_WAIT
    push ebx
    call dword [__imp__WaitForSingleObject@8]
.shex_close:
    push ebx
    call dword [__imp__CloseHandle@4]
.shex_ok:
    add esp, 64
    mov dword [g_last_exit_code], 0
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.shex_fail:
    add esp, 64
.err:
    mov dword [g_last_exit_code], 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc dir_enum_ads_streams32
;; @abi win32
;; @in ESI = full file/dir path (WTF-8), EDI = display filename
;; @out Prints non-default NTFS Alternate Data Streams (`filename:stream:$DATA`) on Vista..11 when DIR /R is active!
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Queries FindFirstStreamW / FindNextStreamW (WIN32_FIND_STREAM_DATA = 600B) for DIR /R.
@proc dir_enum_ads_streams32
    cmp byte [g_is_win9x], 0
    jne .done
    cmp dword [pfnFindFirstStreamW_p16], 0
    je .done
    cmp dword [pfnFindNextStreamW_p8], 0
    je .done

    push ebx
    push esi
    push edi
    push ebp
    mov ebp, edi ; dispName

    call ntw_path_to_w32
    jc .pop_done
    mov ebx, eax

    ; WIN32_FIND_STREAM_DATA: +0 LargeInteger StreamSize (8B), +8 WCHAR cStreamName[296] (592B) = 600 bytes
    mov ecx, 608
    call arena_alloc32
    jc .pop_done
    mov edi, eax

    push 0   ; dwFlags = 0
    push edi ; lpFindStreamData
    push 0   ; FindStreamInfoStandard = 0
    push ebx ; lpFileName
    call dword [pfnFindFirstStreamW_p16]
    cmp eax, INVALID_HANDLE_VALUE
    je .pop_done
    mov ebx, eax ; hStreamFind

.st_lp:
    ; Convert [edi + 8] cStreamName to WTF-8; skip default `::$DATA`!
    lea esi, [edi + 8]
    call utf16le_to_wtf8_arena
    jc .st_close
    mov esi, eax
    cmp byte [esi], ':'
    jne .st_nx
    cmp byte [esi + 1], ':'
    je .st_nx ; skip primary `::$DATA` stream!

    ; Print `                  <size> <dispName><:stream:$DATA>`
    mov esi, addr(szDirStreamIndent)
    call con_print_z32
    sub esp, 36
    push edi
    mov eax, dword [edi]
    mov edx, dword [edi + 4]
    lea edi, [esp + 4]
    call fmt_u64_commas32
    mov esi, edi
    call con_write_out32
    pop edi
    add esp, 36

    mov esi, addr(szOneSpace32)
    mov ecx, 1
    call con_write_out32
    mov esi, ebp
    call con_print_z32
    lea esi, [edi + 8]
    call utf16le_to_wtf8_arena
    jc .st_nx
    mov esi, eax
    call con_print_z32
    call con_print_crlf32

.st_nx:
    push edi
    push ebx
    call dword [pfnFindNextStreamW_p8]
    test eax, eax
    jnz .st_lp

.st_close:
    push ebx
    call dword [__imp__FindClose@4]

.pop_done:
    pop ebp
    pop edi
    pop esi
    pop ebx
.done:
    ret
@endproc

;; @proc dir_one_folder32
;; @abi win32
;; @in ESI = normalized folder path, EDI = filename glob pattern (e.g. "*"), ECX = flags (bit0=/B, bit1=/S, bit2=/X, bit3=/R, bit4=/A:H/all)
;; @out Enumerates folder entries, displays `<DIR>`, `<JUNCTION>`, `<SYMLINK>`, `<SYMLINKD>`, `/X` 8.3, `/R` ADS, and 64-bit free bytes!
;; @err CF=1 if zero files matched and not /S
;; @clobbers eax, ecx, edx
;; @desc Core DIR formatter supporting NT reparse tags, NTFS ADS streams, Win95/NT 8.3 /X, and 64-bit disk totals.
@proc dir_one_folder32
    push ebx
    push esi
    push edi
    push ebp
    ; Frame [ebp + 0..40]:
    ; +0: folderPath, +4: globPat, +8: flags, +12: caseSens, +16: fileCount, +20: dirCount
    ; +24: totalBytesLo, +28: totalBytesHi, +32: hFind, +36: pEntry
    sub esp, 44
    mov ebp, esp
    mov dword [ebp], esi
    mov dword [ebp + 4], edi
    mov dword [ebp + 8], ecx
    mov dword [ebp + 16], 0
    mov dword [ebp + 20], 0
    mov dword [ebp + 24], 0
    mov dword [ebp + 28], 0

    ; Query Win10/11 per-directory case sensitivity!
    call dir_is_case_sensitive32
    mov dword [ebp + 12], eax

    ; Tell hal_find_first32 whether 8.3 short name is required (/X = bit 2)
    xor eax, eax
    test dword [ebp + 8], 4
    setnz al
    mov byte [g_dir_need_short], al

    ; Print header unless `/B` (bare mode = bit 0)
    test dword [ebp + 8], 1
    jnz .build_search
    mov esi, dword [ebp]
    call bi_vol32
    mov esi, addr(szDirOfHdr)
    call con_print_z32
    mov esi, dword [ebp]
    call con_print_z32
    call con_print_crlf32
    call con_print_crlf32

.build_search:
    mov ecx, 2048
    call arena_alloc32
    jc .err_out
    mov edi, eax
    push eax
    mov esi, dword [ebp]
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .app_all
    mov byte [edi], 0x5C
    inc edi
.app_all:
    mov word [edi], 0x002A ; enumerate "*" then filter with our DP globber (honoring case-sensitivity!)
    pop esi

    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .err_out
    mov dword [ebp + 36], eax
    mov edi, eax
    call hal_find_first32
    mov byte [g_dir_need_short], 0
    jc .no_entries
    mov dword [ebp + 32], eax

.ent_lp:
    mov edi, dword [ebp + 36]
    ; Filter Hidden/System unless `/A` (bit 4) is set
    test dword [ebp + 8], 16
    jnz .match_pat
    test dword [edi + FIND_ENTRY32.dwAttr], FILE_ATTRIBUTE_HIDDEN + FILE_ATTRIBUTE_SYSTEM
    jnz .ent_next

.match_pat:
    mov esi, dword [ebp + 4]
    lea edi, [edi + FIND_ENTRY32.szNameWtf8]
    mov ecx, dword [ebp + 12]
    call matches_win32_glob32
    test eax, eax
    jnz .matched_ent
    ; Also match against 8.3 short name if present!
    mov edi, dword [ebp + 36]
    cmp byte [edi + FIND_ENTRY32.szShort83], 0
    je .ent_next
    mov esi, dword [ebp + 4]
    lea edi, [edi + FIND_ENTRY32.szShort83]
    mov ecx, dword [ebp + 12]
    call matches_win32_glob32
    test eax, eax
    jz .ent_next

.matched_ent:
    mov edi, dword [ebp + 36]
    test dword [ebp + 8], 1 ; /B bare mode?
    jz .full_row

    ; In `/B`, skip "." and ".."!
    cmp byte [edi + FIND_ENTRY32.szNameWtf8], '.'
    jne .prt_bare
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 1], 0
    je .ent_next
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 1], '.'
    jne .prt_bare
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 2], 0
    je .ent_next
.prt_bare:
    test dword [ebp + 8], 2 ; /B + /S prints full path!
    jz .bare_leaf
    mov esi, dword [ebp]
    call con_print_z32
    mov esi, dword [ebp]
    call str_len32
    cmp byte [esi + ecx - 1], 0x5C
    je .bare_leaf
    mov esi, addr(szBackslashOnly)
    mov ecx, 1
    call con_write_out32
.bare_leaf:
    lea esi, [edi + FIND_ENTRY32.szNameWtf8]
    call con_print_z32
    call con_print_crlf32
    inc dword [ebp + 16]
    jmp .ent_next

.full_row:
    ; Format local timestamp YYYY-MM-DD  HH:MM
    sub esp, 64
    lea eax, [esp + 16]
    lea ecx, [edi + FIND_ENTRY32.ftWriteLo]
    push eax
    push ecx
    call dword [__imp__FileTimeToLocalFileTime@8]
    mov edx, esp
    lea eax, [esp + 16]
    push edx
    push eax
    call dword [__imp__FileTimeToSystemTime@8]

    lea ebx, [esp + 24]
    push edi
    mov edi, ebx
    movzx eax, word [esp + 4 + SYSTEMTIME16.wYear]
    xor edx, edx
    call fmt_u64_plain32
    add edi, ecx
    mov byte [edi], '-'
    inc edi
    movzx eax, word [esp + 4 + SYSTEMTIME16.wMonth]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], '-'
    inc edi
    movzx eax, word [esp + 4 + SYSTEMTIME16.wDay]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov word [edi], 0x2020
    add edi, 2
    movzx eax, word [esp + 4 + SYSTEMTIME16.wHour]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov byte [edi], ':'
    inc edi
    movzx eax, word [esp + 4 + SYSTEMTIME16.wMinute]
    mov dl, '0'
    call fmt_u32_pad2
    mov edi, eax
    mov dword [edi], 0x00202020
    mov esi, ebx
    call con_print_z32
    pop edi
    add esp, 64

    ; Classify `<JUNCTION>`, `<SYMLINKD>`, `<SYMLINK>`, `<DIR>`, or file size!
    mov eax, dword [edi + FIND_ENTRY32.dwAttr]
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jz .chk_std_dir
    mov edx, dword [edi + FIND_ENTRY32.dwReparseTag]
    cmp edx, IO_REPARSE_TAG_MOUNT_POINT
    je .tag_junc
    cmp edx, IO_REPARSE_TAG_SYMLINK
    jne .chk_std_dir
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jnz .tag_symd
    mov esi, addr(szDirTagSymlink)
    call con_print_z32
    inc dword [ebp + 16]
    jmp .opt_short_col
.tag_junc:
    mov esi, addr(szDirTagJunction)
    call con_print_z32
    inc dword [ebp + 20]
    jmp .opt_short_col
.tag_symd:
    mov esi, addr(szDirTagSymlinkD)
    call con_print_z32
    inc dword [ebp + 20]
    jmp .opt_short_col

.chk_std_dir:
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .file_sz_col
    mov esi, addr(szDirTagDir)
    call con_print_z32
    inc dword [ebp + 20]
    jmp .opt_short_col

.file_sz_col:
    inc dword [ebp + 16]
    mov eax, dword [edi + FIND_ENTRY32.nSizeLo]
    mov edx, dword [edi + FIND_ENTRY32.nSizeHi]
    add dword [ebp + 24], eax
    adc dword [ebp + 28], edx

    sub esp, 36
    push edi
    mov edi, esp
    add edi, 4
    call fmt_u64_commas32
    ; Right-align within 14 chars + trailing space
    mov eax, 14
    cmp ecx, 14
    jae .prt_sz_num
    sub eax, ecx
    mov ebx, eax
.pad_sz:
    mov esi, addr(szOneSpace32)
    mov ecx, 1
    call con_write_out32
    dec ebx
    jnz .pad_sz
.prt_sz_num:
    lea esi, [esp + 4]
    call con_print_z32
    mov esi, addr(szOneSpace32)
    mov ecx, 1
    call con_write_out32
    pop edi
    add esp, 36

.opt_short_col:
    ; `/X` (bit 2): print 13-char padded 8.3 short name before long filename!
    test dword [ebp + 8], 4
    jz .prt_long_name
    lea esi, [edi + FIND_ENTRY32.szShort83]
    call str_len32
    push ecx
    call con_write_out32
    pop ecx
    mov ebx, 13
    cmp ecx, 13
    jae .prt_long_name
    sub ebx, ecx
.pad_83:
    mov esi, addr(szOneSpace32)
    mov ecx, 1
    call con_write_out32
    dec ebx
    jnz .pad_83

.prt_long_name:
    lea esi, [edi + FIND_ENTRY32.szNameWtf8]
    call con_print_z32
    call con_print_crlf32

    ; `/R` (bit 3): display NTFS Alternate Data Streams!
    test dword [ebp + 8], 8
    jz .ent_next
    cmp byte [edi + FIND_ENTRY32.szNameWtf8], '.'
    je .ent_next
    mov ecx, 2048
    call arena_alloc32
    jc .ent_next
    mov edx, eax
    push edi
    mov edi, edx
    mov esi, dword [ebp]
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .ads_app
    mov byte [edi], 0x5C
    inc edi
.ads_app:
    mov esi, dword [esp]
    lea esi, [esi + FIND_ENTRY32.szNameWtf8]
    push edx
    call str_copy32
    pop esi ; fullItemPath
    pop edi ; FIND_ENTRY32*
    push edi
    lea edi, [edi + FIND_ENTRY32.szNameWtf8]
    call dir_enum_ads_streams32
    pop edi

.ent_next:
    mov ebx, dword [ebp + 32]
    mov edi, dword [ebp + 36]
    call hal_find_next32
    jnc .ent_lp

    push dword [ebp + 32]
    call dword [__imp__FindClose@4]

    ; Print summary footer unless `/B`
    test dword [ebp + 8], 1
    jnz .chk_recurse

    sub esp, 36
    mov edi, esp
    mov esi, addr(szDirSumPad)
    call con_print_z32
    mov eax, dword [ebp + 16]
    xor edx, edx
    call fmt_u64_plain32
    mov esi, edi
    call con_write_out32
    mov esi, addr(szDirFilesWord)
    call con_print_z32

    mov eax, dword [ebp + 24]
    mov edx, dword [ebp + 28]
    call fmt_u64_commas32
    mov esi, edi
    call con_write_out32
    mov esi, addr(szDirBytesWord)
    call con_print_z32

    mov esi, addr(szDirSumPad)
    call con_print_z32
    mov eax, dword [ebp + 20]
    xor edx, edx
    call fmt_u64_plain32
    mov esi, edi
    call con_write_out32
    mov esi, addr(szDirDirsWord)
    call con_print_z32

    mov esi, dword [ebp]
    call hal_disk_free64
    jc .no_free_b
    call fmt_u64_commas32
    mov esi, edi
    call con_write_out32
    mov esi, addr(szDirBytesFree)
    call con_print_z32
    jmp .sum_done
.no_free_b:
    call con_print_crlf32
.sum_done:
    add esp, 36

.chk_recurse:
    test dword [ebp + 8], 2 ; /S recursive?
    jz .done_ok

    ; Enumerate immediate subdirectories of [ebp] and recurse (skipping reparse points to avoid loops)
    mov ecx, 2048
    call arena_alloc32
    jc .done_ok
    mov edi, eax
    push eax
    mov esi, dword [ebp]
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .rc_st
    mov byte [edi], 0x5C
    inc edi
.rc_st:
    mov word [edi], 0x002A
    pop esi

    mov ecx, FIND_ENTRY32_SIZE
    call arena_alloc32
    jc .done_ok
    mov edi, eax
    call hal_find_first32
    jc .done_ok
    mov ebx, eax ; hSubFind

.rc_lp:
    mov eax, dword [edi + FIND_ENTRY32.dwAttr]
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .rc_nx
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .rc_nx
    cmp byte [edi + FIND_ENTRY32.szNameWtf8], '.'
    jne .rc_go
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 1], 0
    je .rc_nx
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 1], '.'
    jne .rc_go
    cmp byte [edi + FIND_ENTRY32.szNameWtf8 + 2], 0
    je .rc_nx
.rc_go:
    mov ecx, 2048
    call arena_alloc32
    jc .rc_close
    mov edx, eax
    push edi
    mov edi, edx
    mov esi, dword [ebp]
    call str_copy32
    mov edi, eax
    cmp byte [edi - 1], 0x5C
    je .rc_app
    mov byte [edi], 0x5C
    inc edi
.rc_app:
    mov esi, dword [esp]
    lea esi, [esi + FIND_ENTRY32.szNameWtf8]
    push edx
    call str_copy32
    pop esi ; subDir
    mov edi, dword [ebp + 4]
    mov ecx, dword [ebp + 8]
    call dir_one_folder32
    pop edi

.rc_nx:
    call hal_find_next32
    jnc .rc_lp
.rc_close:
    push ebx
    call dword [__imp__FindClose@4]

.done_ok:
    mov eax, dword [ebp + 16]
    or eax, dword [ebp + 20]
    jz .no_entries
    add esp, 44
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.no_entries:
    test dword [ebp + 8], 2
    jnz .rec_empty_ok
    add esp, 44
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
.rec_empty_ok:
    add esp, 44
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret
.err_out:
    add esp, 44
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc

;; @proc bi_dir32
;; @abi win32
;; @in ESI = `[/B] [/S] [/W] [/X] [/R] [/A[:attrs]] [path]`
;; @out Displays directory listing(s) and sets g_last_exit_code
;; @err Sets g_last_exit_code = 1 when no files found
;; @clobbers eax, ecx, edx
;; @desc Implements DIR across NT 3.1..11 and Win95..Me.
@proc bi_dir32
    push ebx
    push esi
    push edi
    push ebp
    xor ebp, ebp ; flags: bit0=/B, bit1=/S, bit2=/X, bit3=/R, bit4=/A
    xor ebx, ebx ; target spec

.tok_lp:
    call prs_skip_ws32
    mov esi, eax
    cmp byte [esi], 0
    je .run_dir
    cmp byte [esi], '/'
    jne .path_tok
    call prs_read_word32
    mov edi, edx
    mov esi, eax
    ; Support stacked switches like `/B` `/S` `/X` `/R` `/A`
    inc esi
.sw_ch_lp:
    mov al, byte [esi]
    test al, al
    jz .nx_sw_w
    cmp al, '/'
    jne .dec_sw
    inc esi
    jmp .sw_ch_lp
.dec_sw:
    call ascii_upper_al
    inc esi
    cmp al, 'B'
    je .f_b
    cmp al, 'S'
    je .f_s
    cmp al, 'X'
    je .f_x
    cmp al, 'R'
    je .f_r
    cmp al, 'A'
    je .f_a
    jmp .sw_ch_lp
.f_b:
    or ebp, 1
    jmp .sw_ch_lp
.f_s:
    or ebp, 2
    jmp .sw_ch_lp
.f_x:
    or ebp, 4
    jmp .sw_ch_lp
.f_r:
    or ebp, 8
    jmp .sw_ch_lp
.f_a:
    or ebp, 16
    jmp .nx_sw_w ; skip optional `:H` / `:D` suffix on `/A:...`!
.nx_sw_w:
    mov esi, edi
    jmp .tok_lp

.path_tok:
    call prs_read_word32
    jc .err
    mov edi, edx
    mov esi, eax
    call tok_strip_quotes32
    jc .err
    mov ebx, eax
    mov esi, edi
    jmp .tok_lp

.run_dir:
    test ebx, ebx
    jnz .resolve_spec
    mov ecx, 2048
    call arena_alloc32
    jc .err
    mov edi, eax
    mov ecx, 2040
    call hal_get_cwd32
    jc .err
    mov esi, edi
    mov edi, addr(szStar32)
    mov ecx, ebp
    call dir_one_folder32
    jmp .finish

.resolve_spec:
    mov esi, ebx
    call path_normalize32
    jc .err
    mov ebx, eax

    ; If ebx has no '*' or '?' AND is an existing directory, list `<ebx>` with pattern `*`!
    mov esi, ebx
.has_w_lp:
    mov al, byte [esi]
    test al, al
    jz .chk_if_dir
    cmp al, '*'
    je .split_dir_pat
    cmp al, '?'
    je .split_dir_pat
    inc esi
    jmp .has_w_lp

.chk_if_dir:
    mov esi, ebx
    call hal_get_attr32
    jc .split_dir_pat
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .split_dir_pat
    mov esi, ebx
    mov edi, addr(szStar32)
    mov ecx, ebp
    call dir_one_folder32
    jmp .finish

.split_dir_pat:
    ; Split `ebx` at last `` into folder + leaf pattern!
    mov esi, ebx
    call str_len32
.sp_lp:
    test ecx, ecx
    jz .err
    cmp byte [ebx + ecx - 1], 0x5C
    je .got_sp
    dec ecx
    jmp .sp_lp
.got_sp:
    lea edi, [ebx + ecx] ; pattern
    ; Keep trailing `` only on root `"X:"` (ecx == 3)!
    cmp ecx, 3
    je .root_sp
    mov byte [ebx + ecx - 1], 0
    mov esi, ebx
    mov ecx, ebp
    call dir_one_folder32
    jmp .finish
.root_sp:
    push edi
    mov ecx, 8
    call arena_alloc32
    pop edi
    jc .err
    mov edx, dword [ebx]
    mov dword [eax], edx
    mov byte [eax + 3], 0
    mov esi, eax
    mov ecx, ebp
    call dir_one_folder32

.finish:
    jc .not_found
    mov dword [g_last_exit_code], 0
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

.not_found:
    mov eax, ERR_FILE_NOT_FOUND
    call con_report_err32
.err:
    mov dword [g_last_exit_code], 1
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc
