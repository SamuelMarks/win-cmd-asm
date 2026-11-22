;; @file cmds64_b.inc
;; @isa x64
;; @desc x86-64 Internal Built-Ins Part B (CD, PUSHD/POPD UNC, MD, RD /S, MKLINK, COPY +, DEL, MOVE, REN, DIR, TYPE, START, VOL).

@cpu x64
@format inc

;; @proc bi_cd64
;; @abi win64_ms
;; @in RCX = raw argument tail (`[/D] [path]` — supporting unquoted spaces in `CD C:\Program Files` when /E:ON!)
;; @out Prints CWD (or =X: CWD) or changes directory/drive and updates g_last_exit_code
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements CD / CHDIR including /D cross-drive switching and unquoted space paths.
@proc_frame bi_cd64, 56, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .show_cur

    xor ebx, ebx ; /D flag
    cmp byte [rsi], '/'
    jne .got_path
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'D'
    jne .got_path
    mov ebx, 1
    add rsi, 2
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax

.got_path:
    mov rcx, rsi
    call arena_strdup64
    jc .err
    mov rsi, rax
    mov rcx, rsi
    call str_len64
    mov rdx, rax
.tr_lp:
    test rdx, rdx
    jz .show_cur
    mov al, byte [rsi + rdx - 1]
    cmp al, 0x20
    je .tr_c
    cmp al, 0x09
    jne .unq
.tr_c:
    dec rdx
    mov byte [rsi + rdx], 0
    jmp .tr_lp

.unq:
    mov rcx, rsi
    call tok_strip_quotes64
    jc .err
    mov rsi, rax

    ; Bare `CD X:` (length 2, [1]==':', no /D) queries per-drive CWD!
    cmp byte [rsi + 1], ':'
    jne .do_chdir
    cmp byte [rsi + 2], 0
    jne .chk_other_drv
    test ebx, ebx
    jnz .do_chdir

    mov al, byte [rsi]
    call ascii_upper_al64
    mov bl, al
    mov byte [rsp + 32], '='
    mov byte [rsp + 33], bl
    mov byte [rsp + 34], ':'
    mov byte [rsp + 35], 0
    lea rcx, [rsp + 32]
    call env_get_raw64
    test rax, rax
    jnz .prt_drv_cwd

    mov byte [rsp + 32], bl
    mov byte [rsp + 33], ':'
    mov byte [rsp + 34], 0x5C
    mov byte [rsp + 35], 0
    lea rax, [rsp + 32]
.prt_drv_cwd:
    mov rcx, rax
    call con_print_z64
    call con_print_crlf64
    jmp .ok

.chk_other_drv:
    ; If target is on another drive and `/D` was NOT specified, update `=X:` without switching active drive!
    test ebx, ebx
    jnz .do_chdir
    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .do_chdir
    mov al, byte [rdi]
    call ascii_upper_al64
    mov dl, al
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, dl
    je .do_chdir

    mov rcx, rsi
    call path_normalize64
    jc .err_msg
    mov r12, rax
    mov rcx, r12
    call hal_get_attr64
    jc .err_msg
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .not_dir
    mov rcx, r12
    call path_sync_drive_env64
    jmp .ok

.do_chdir:
    mov rcx, rsi
    call path_normalize64
    jc .err_msg
    mov rcx, rax
    call hal_set_cwd64
    jc .err_msg

    mov ecx, 2048
    call arena_alloc64
    jc .ok
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .ok
    mov rcx, rdi
    call path_sync_drive_env64
    jmp .ok

.show_cur:
    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .err_msg
    mov rcx, rdi
    call con_print_z64
    call con_print_crlf64

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.not_dir:
    mov eax, ERR_PATH_NOT_FOUND
.err_msg:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc hal_ensure_mpr64
;; @abi win64_ms
;; @in none
;; @out Loads MPR.DLL on demand and resolves WNetAddConnection2W / WNetCancelConnection2W
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Lazy-loads MPR.DLL so PUSHD \SERVER\SHARE maps temporary Z:..A: network drives without static MPR import.
@proc_frame hal_ensure_mpr64, 32, rbx
    cmp byte [rel g_mpr_loaded], 0
    jne .done
    mov byte [rel g_mpr_loaded], 1
    lea rcx, [rel szDllMpr64]
    call qword [rel __imp_LoadLibraryA]
    test rax, rax
    jz .done
    mov rbx, rax
    mov rcx, rbx
    lea rdx, [rel szFnWNetAddConn2W64]
    call dyn_resolve_proc64
    mov qword [rel pfnWNetAddConn2W64], rax
    mov rcx, rbx
    lea rdx, [rel szFnWNetCancel2W64]
    call dyn_resolve_proc64
    mov qword [rel pfnWNetCancelConn2W64], rax
.done:
    @epilog_ret
@endproc

;; @proc bi_pushd64
;; @abi win64_ms
;; @in RCX = `[path | \server\share\subpath]`
;; @out Pushes current CWD onto PUSHD stack, maps temporary Z:..D: drive on UNC paths, and switches directory
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements PUSHD including UNC share temporary drive-letter mapping via MPR!WNetAddConnection2W.
@proc_frame bi_pushd64, 104, rbp, rbx, rsi, rdi, r12, r13
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .list_stack

    mov eax, dword [rel g_pushd_depth]
    cmp eax, W64_PUSHD_MAX
    jae .err
    mov ebp, eax ; slot index

    mov rcx, rsi
    call arena_strdup64
    jc .err
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rbx, rax ; target path

    mov ecx, 2048
    call perm_alloc64
    jc .err
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .free_err
    lea rcx, [rel g_pushd_dirs]
    mov qword [rcx + rbp * 8], rdi
    lea rcx, [rel g_pushd_mapped]
    mov byte [rcx + rbp], 0

    ; Check if target starts with `` (UNC network path, excluding `\?` / `\.`)!
    cmp byte [rbx], 0x5C
    jne .normal_push
    cmp byte [rbx + 1], 0x5C
    jne .normal_push
    cmp byte [rbx + 2], '?'
    je .normal_push
    cmp byte [rbx + 2], '.'
    je .normal_push

    call hal_ensure_mpr64
    cmp qword [rel pfnWNetAddConn2W64], 0
    je .normal_push

    ; Split UNC server+share (r12) and subpath tail (r13)
    mov rcx, rbx
    call arena_strdup64
    jc .normal_push
    mov r12, rax
    lea rdi, [rax + 2]
    xor ecx, ecx
.unc_sc:
    mov al, byte [rdi]
    test al, al
    jz .unc_split_done
    cmp al, 0x5C
    jne .unc_nx
    inc ecx
    cmp ecx, 2
    je .unc_cut
.unc_nx:
    inc rdi
    jmp .unc_sc
.unc_cut:
    mov byte [rdi], 0
    mov rax, rdi
    sub rax, r12
    lea r13, [rbx + rax]
    jmp .unc_got_parts
.unc_split_done:
    lea r13, [rel szBackslashOnly64]

.unc_got_parts:
    mov rcx, r12
    call wtf8_to_utf16le64
    jc .normal_push
    mov r12, rax ; remoteNameW

    ; Scan Z: down to D: for a free drive letter via WNetAddConnection2W
    mov esi, 'Z'
.drv_lp:
    cmp esi, 'D'
    jb .normal_push

    ; LocalNameW at [rsp + 32..39]: WCHAR 'Z', ':', 0
    mov word [rsp + 32], si
    mov word [rsp + 34], ':'
    mov word [rsp + 36], 0

    ; Zero 48-byte NETRESOURCEW at [rsp + 48..95]
    lea rdi, [rsp + 48]
    xor eax, eax
    mov ecx, 6
    rep stosq

    mov dword [rsp + 48 + 4], 1 ; dwType = RESOURCETYPE_DISK
    lea rax, [rsp + 32]
    mov qword [rsp + 48 + 16], rax ; lpLocalName
    mov qword [rsp + 48 + 24], r12 ; lpRemoteName

    lea rcx, [rsp + 48]
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    call qword [rel pfnWNetAddConn2W64]
    test eax, eax
    jz .mapped_ok
    dec esi
    jmp .drv_lp

.mapped_ok:
    lea rcx, [rel g_pushd_mapped]
    mov byte [rcx + rbp], sil
    mov ecx, 2048
    call arena_alloc64
    jc .normal_push
    mov rbx, rax
    mov byte [rbx], sil
    mov byte [rbx + 1], ':'
    lea rcx, [rbx + 2]
    mov rdx, r13
    call str_copy64

.normal_push:
    mov rcx, rbx
    call path_normalize64
    jc .free_err
    mov rcx, rax
    call hal_set_cwd64
    jc .free_err

    inc dword [rel g_pushd_depth]
    mov ecx, 2048
    call arena_alloc64
    jc .ok
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .ok
    mov rcx, rdi
    call path_sync_drive_env64
    jmp .ok

.list_stack:
    mov ebx, dword [rel g_pushd_depth]
.ls_lp:
    test ebx, ebx
    jz .ok
    dec ebx
    lea rcx, [rel g_pushd_dirs]
    mov rcx, qword [rcx + rbx * 8]
    call con_print_z64
    call con_print_crlf64
    jmp .ls_lp

.free_err:
    mov rcx, rdi
    call perm_free64
    lea rcx, [rel g_pushd_dirs]
    mov qword [rcx + rbp * 8], 0
    mov ecx, ERR_PATH_NOT_FOUND
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_popd64
;; @abi win64_ms
;; @in none
;; @out Pops directory from PUSHD stack, restores CWD, and unmaps temporary UNC drive if mapped!
;; @err Sets g_last_exit_code = 1 when stack is empty
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements POPD with automatic WNetCancelConnection2W temporary drive cleanup.
@proc_frame bi_popd64, 48, rbx, rsi, rdi
    mov ebx, dword [rel g_pushd_depth]
    test ebx, ebx
    jz .empty
    dec ebx
    mov dword [rel g_pushd_depth], ebx

    lea rcx, [rel g_pushd_dirs]
    mov rsi, qword [rcx + rbx * 8]
    mov qword [rcx + rbx * 8], 0
    mov rcx, rsi
    call hal_set_cwd64
    mov rcx, rsi
    call perm_free64

    lea rcx, [rel g_pushd_mapped]
    movzx eax, byte [rcx + rbx]
    test eax, eax
    jz .sync_ok
    mov byte [rcx + rbx], 0
    cmp qword [rel pfnWNetCancelConn2W64], 0
    je .sync_ok

    mov word [rsp + 32], ax
    mov word [rsp + 34], ':'
    mov word [rsp + 36], 0
    lea rcx, [rsp + 32]
    xor edx, edx
    mov r8d, 1 ; fForce = TRUE
    call qword [rel pfnWNetCancelConn2W64]

.sync_ok:
    mov ecx, 2048
    call arena_alloc64
    jc .ok
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .ok
    mov rcx, rdi
    call path_sync_drive_env64
.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.empty:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_md64
;; @abi win64_ms
;; @in RCX = `path...`
;; @out Creates target directory (plus intermediate parent directories when Command Extensions /E:ON!)
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements MD / MKDIR including multi-level intermediate creation under CMDEXTVERSION=2.
@proc_frame bi_md64, 40, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .syn_err

.arg_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .ok

    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov r12, rdx ; next cursor
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call path_normalize64
    jc .rep_err
    mov rbx, rax ; full target path

    cmp byte [rel g_extensions_on], 0
    je .single_md

    mov rdi, rbx
    cmp byte [rdi + 1], ':'
    jne .sc_sep
    add rdi, 3
.sc_sep:
    mov al, byte [rdi]
    test al, al
    jz .single_md
    cmp al, 0x5C
    jne .nx_ch
    mov byte [rdi], 0
    mov rcx, rbx
    call hal_mkdir_one64
    mov byte [rdi], 0x5C
.nx_ch:
    inc rdi
    jmp .sc_sep

.single_md:
    mov rcx, rbx
    call hal_mkdir_one64
    jc .rep_err

    mov rsi, r12
    jmp .arg_lp

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc rd_tree_reparse_safe64
;; @abi win64_ms
;; @in RCX = normalized directory path
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reparse-safe recursive directory removal: NEVER traverses into <JUNCTION> or <SYMLINKD> targets!
@proc_frame rd_tree_reparse_safe64, 48, rbp, rbx, rsi, rdi, r12
    mov rbp, rcx

    call hal_get_attr64
    jc .ret_err
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .unlink_leaf

    test eax, FILE_ATTRIBUTE_READONLY
    jz .enum_children
    and eax, -2
    mov edx, eax
    mov rcx, rbp
    call hal_set_attr64

.enum_children:
    mov ecx, 2048
    call arena_alloc64
    jc .ret_err
    mov rsi, rax
    mov rcx, rsi
    mov rdx, rbp
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .app_star
    mov byte [rax], 0x5C
    inc rax
.app_star:
    mov word [rax], 0x002A ; "*"

    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .ret_err
    mov rdi, rax
    mov rcx, rsi
    mov rdx, rdi
    call hal_find_first64
    jc .unlink_leaf
    mov rbx, rax ; hFind

.child_lp:
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8], '.'
    jne .proc_child
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 1], 0
    je .next_child
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 1], '.'
    jne .proc_child
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 2], 0
    je .next_child

.proc_child:
    mov ecx, 2048
    call arena_alloc64
    jc .close_find
    mov r12, rax
    mov rcx, r12
    mov rdx, rbp
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .cp_cname
    mov byte [rax], 0x5C
    inc rax
.cp_cname:
    mov rcx, rax
    lea rdx, [rdi + FIND_ENTRY64.szNameWtf8]
    call str_copy64

    mov eax, dword [rdi + FIND_ENTRY64.dwAttr]
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .del_child_file
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .unlink_child_reparse
    mov rcx, r12
    call rd_tree_reparse_safe64
    jmp .next_child

.unlink_child_reparse:
    mov rcx, r12
    call hal_rmdir_one64
    jmp .next_child

.del_child_file:
    mov rcx, r12
    mov edx, 1
    call hal_delete_file64

.next_child:
    mov rcx, rbx
    mov rdx, rdi
    call hal_find_next64
    jnc .child_lp

.close_find:
    mov rcx, rbx
    call qword [rel __imp_FindClose]

.unlink_leaf:
    mov rcx, rbp
    call hal_rmdir_one64
    jc .ret_err
    clc
    @epilog_ret

.ret_err:
    stc
    @epilog_ret
@endproc

;; @proc bi_rd64
;; @abi win64_ms
;; @in RCX = `[/S] [/Q] path...`
;; @out Removes empty directory or `/S` directory tree (reparse-point safe!)
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements RD / RMDIR with /S and /Q and reparse-safe junction/symlink unlinking.
@proc_frame bi_rd64, 40, rbx, rsi, rdi, r12
    mov rsi, rcx
    xor ebx, ebx ; bit0=/S, bit1=/Q

.sw_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '/'
    jne .rd_targets
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    add rsi, 2
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
    cmp byte [rsi], 0
    je .syn_err

.t_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .ok
    cmp byte [rsi], '/'
    jne .read_t
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    add rsi, 2
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
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov rdi, rdx ; rest
    mov r12, rax ; raw_path
    mov rsi, rdi
.peek_trail_sw:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '/'
    jne .run_one_rd
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    add rsi, 2
    mov rdi, rsi
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
    mov rcx, r12
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call path_normalize64
    jc .rep_err
    mov r12, rax

    test ebx, 1
    jnz .do_tree
    mov rcx, r12
    call hal_rmdir_one64
    jc .rep_err
    mov rsi, rdi
    jmp .t_lp

.do_tree:
    mov rcx, r12
    call rd_tree_reparse_safe64
    jc .rep_err
    mov rsi, rdi
    jmp .t_lp

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_mklink64
;; @abi win64_ms
;; @in RCX = `[[/D] | [/H] | [/J]] Link Target`
;; @out Creates file symlink, /D directory symlink, /H hardlink, or /J NTFS junction via nt64_mklink
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements internal Vista..11 (and XP x64 /H /J!) MKLINK command.
@proc_frame bi_mklink64, 40, rbp, rbx, rsi, rdi
    mov rsi, rcx
    xor ebx, ebx ; mode: 0=file symlink, 1=/D, 2=/H, 3=/J

.sw_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '/'
    jne .args
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    add rsi, 2
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
    mov rcx, rsi
    call prs_read_word64
    jc .syn_err
    cmp byte [rax], 0
    je .syn_err
    mov rsi, rdx
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rbp, rax ; linkPath

    mov rcx, rsi
    call prs_read_word64
    jc .syn_err
    cmp byte [rax], 0
    je .syn_err
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rdi, rax ; targetPath

    mov rcx, rbp
    mov rdx, rdi
    mov r8d, ebx
    call nt64_mklink
    jc .rep_err

    lea rcx, [rel szMklinkSym64]
    cmp ebx, 1
    je .m_symd
    cmp ebx, 2
    je .m_hard
    cmp ebx, 3
    je .m_junc
    jmp .prt_mk
.m_symd:
    lea rcx, [rel szMklinkSymD64]
    jmp .prt_mk
.m_hard:
    lea rcx, [rel szMklinkHard64]
    jmp .prt_mk
.m_junc:
    lea rcx, [rel szMklinkJunc64]
.prt_mk:
    call con_print_z64
    mov rcx, rbp
    call con_print_z64
    lea rcx, [rel szMklinkArrow64]
    call con_print_z64
    mov rcx, rdi
    call con_print_z64
    call con_print_crlf64
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_del64
;; @abi win64_ms
;; @in RCX = `[/P] [/F] [/S] [/Q] [/A[:attrs]] names...`
;; @out Deletes matching files (supporting wildcards and /F read-only stripping)
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements DEL / ERASE.
@proc_frame bi_del64, 40, rbp, rbx, rsi, rdi, r12, r13
    mov rsi, rcx
    xor ebp, ebp ; bit0=/F

.arg_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .done_ok
    cmp byte [rsi], '/'
    jne .file_tok
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'F'
    jne .skip_sw
    or ebp, 1
.skip_sw:
    mov rcx, rsi
    call prs_read_word64
    mov rsi, rdx
    jmp .arg_lp

.file_tok:
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov r13, rdx ; rest
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call path_normalize64
    jc .err
    mov rbx, rax ; normPat

    mov rcx, rbx
    call str_len64
    mov rdx, rax
.sep_sc:
    test rdx, rdx
    jz .try_direct
    cmp byte [rbx + rdx - 1], 0x5C
    je .got_dlen
    dec rdx
    jmp .sep_sc

.got_dlen:
    mov r12, rdx ; dirPrefix len
    lea rcx, [r12 + 4]
    call arena_alloc64
    jc .err
    mov rdi, rax ; dirPrefix
    mov rsi, rbx
    mov rcx, r12
    rep movsb
    mov byte [rdi], 0
    sub rdi, r12

    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .err
    mov r12, rax ; FIND_ENTRY64*
    mov rcx, rbx
    mov rdx, r12
    call hal_find_first64
    jc .try_direct
    mov rbx, rax ; hFind

.del_w_lp:
    test dword [r12 + FIND_ENTRY64.dwAttr], FILE_ATTRIBUTE_DIRECTORY
    jnz .del_w_nx

    mov ecx, 2048
    call arena_alloc64
    jc .del_w_close
    mov rsi, rax
    mov rcx, rsi
    mov rdx, rdi
    call str_copy64
    mov rcx, rax
    lea rdx, [r12 + FIND_ENTRY64.szNameWtf8]
    call str_copy64

    mov rcx, rsi
    mov edx, ebp
    and edx, 1
    call hal_delete_file64

.del_w_nx:
    mov rcx, rbx
    mov rdx, r12
    call hal_find_next64
    jnc .del_w_lp

.del_w_close:
    mov rcx, rbx
    call qword [rel __imp_FindClose]
    mov rsi, r13
    jmp .arg_lp

.try_direct:
    mov rcx, rbx
    mov edx, ebp
    and edx, 1
    call hal_delete_file64
    mov rsi, r13
    jmp .arg_lp

.done_ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_move64
;; @abi win64_ms
;; @in RCX = `[/Y | /-Y] src dst`
;; @out Moves file or directory via hal_move_file64
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements MOVE [/Y | /-Y] source destination.
@proc_frame bi_move64, 48, rbp, rbx, rsi, rdi, r12
    mov rsi, rcx
    mov ebp, 1 ; default overwrite = 1

.sw_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '/'
    jne .args
    mov rcx, rsi
    call prs_read_word64
    mov rsi, rdx
    jmp .sw_lp

.args:
    mov rcx, rsi
    call prs_read_word64
    jc .syn_err
    cmp byte [rax], 0
    je .syn_err
    mov rsi, rdx
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call path_normalize64
    jc .rep_err
    mov rbx, rax ; src

    mov rcx, rsi
    call prs_read_word64
    jc .syn_err
    cmp byte [rax], 0
    je .syn_err
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call path_normalize64
    jc .rep_err
    mov rdi, rax ; dst

    mov rcx, rdi
    call hal_get_attr64
    jc .do_mv
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .do_mv

    mov rcx, rbx
    call str_len64
    mov rdx, rax
.base_sc:
    test rdx, rdx
    jz .got_b
    cmp byte [rbx + rdx - 1], 0x5C
    je .got_b
    dec rdx
    jmp .base_sc
.got_b:
    lea r12, [rbx + rdx] ; basename
    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rsi, rax
    mov rcx, rsi
    mov rdx, rdi
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .app_b
    mov byte [rax], 0x5C
    inc rax
.app_b:
    mov rcx, rax
    mov rdx, r12
    call str_copy64
    mov rdi, rsi

.do_mv:
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, ebp
    call hal_move_file64
    jc .rep_err

    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_ren64
;; @abi win64_ms
;; @in RCX = `[drive:][path]filename1 filename2`
;; @out Renames file/directory in-place within source parent directory
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements REN / RENAME.
@proc_frame bi_ren64, 40, rbx, rsi, rdi, r12
    mov rsi, rcx
    mov rcx, rsi
    call prs_read_word64
    jc .syn_err
    cmp byte [rax], 0
    je .syn_err
    mov rsi, rdx
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call path_normalize64
    jc .rep_err
    mov rbx, rax ; fullSrc

    mov rcx, rsi
    call prs_read_word64
    jc .syn_err
    cmp byte [rax], 0
    je .syn_err
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rdi, rax ; newLeaf

    mov rcx, rdi
.chk_sep:
    mov al, byte [rcx]
    test al, al
    jz .build_dst
    cmp al, 0x5C
    je .syn_err
    cmp al, '/'
    je .syn_err
    cmp al, ':'
    je .syn_err
    inc rcx
    jmp .chk_sep

.build_dst:
    mov rcx, rbx
    call str_len64
    mov r12, rax
.find_p:
    test r12, r12
    jz .syn_err
    cmp byte [rbx + r12 - 1], 0x5C
    je .got_p
    dec r12
    jmp .find_p
.got_p:
    lea rcx, [r12 + 1024]
    call arena_alloc64
    jc .err
    mov rsi, rax ; fullDst
    mov rdx, rdi ; save newLeaf
    mov rdi, rsi
    mov r8, rsi
    mov rsi, rbx
    mov rcx, r12
    rep movsb
    mov rcx, rdi
    call str_copy64
    mov rdi, r8

    mov rcx, rbx
    mov rdx, rdi
    xor r8d, r8d
    call hal_move_file64
    jc .rep_err

    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc copy_append_stream64
;; @abi win64_ms
;; @in RCX = source file/device path, RDX = open destination HANDLE, R8B = bAsciiMode (1 -> stop at 0x1A)
;; @out CF=0 on success; CF=1, EAX=ERR_* on failure
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Streams source file (or interactive CON!) into destination handle for COPY and `COPY a + b dst`.
@proc_frame copy_append_stream64, 64, rbp, rbx, rsi, rdi, r12
    mov rsi, rcx
    mov rbx, rdx ; hDst
    movzx ebp, r8b

    mov rcx, rsi
    call dev_match_name64
    test rax, rax
    jz .open_src
    cmp dword [rax], 0x004E4F43 ; "CON\0"
    je .from_con
    cmp dword [rax], 0x494E4F43 ; "CONI" ("CONIN$")
    je .from_con
    mov rsi, rax
    jmp .open_src

.from_con:
    mov ecx, 4096
    call arena_alloc64
    jc .err
    mov rdi, rax
.con_lp:
    mov rcx, rdi
    mov edx, 4090
    xor r8d, r8d
    call con_read_line64
    jc .con_done
    mov word [rdi + rax], 0x0A0D
    lea r8d, [eax + 2]
    mov dword [rsp + 48], 0
    mov rcx, rbx
    mov rdx, rdi
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]
    jmp .con_lp
.con_done:
    clc
    @epilog_ret

.open_src:
    mov rcx, rsi
    mov edx, GENERIC_READ
    mov r8d, OPEN_EXISTING
    xor r9d, r9d
    call hal_open_file64
    jc .err
    mov rdi, rax ; hSrc

    mov ecx, 32768
    call arena_alloc64
    jc .close_err
    mov rsi, rax ; 32 KiB transfer buffer

.rd_lp:
    mov dword [rsp + 48], 0
    mov rcx, rdi
    mov rdx, rsi
    mov r8d, 32768
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    test eax, eax
    jz .close_err
    mov r12d, dword [rsp + 48]
    test r12d, r12d
    jz .done_src

    test ebp, ebp
    jz .wr_chunk
    xor edx, edx
.cz_sc:
    cmp edx, r12d
    jae .wr_chunk
    cmp byte [rsi + rdx], 0x1A
    je .hit_cz
    inc edx
    jmp .cz_sc
.hit_cz:
    mov r12d, edx
    test r12d, r12d
    jz .done_src
    mov dword [rsp + 48], 0
    mov rcx, rbx
    mov rdx, rsi
    mov r8d, r12d
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]
    jmp .done_src

.wr_chunk:
    mov dword [rsp + 48], 0
    mov rcx, rbx
    mov rdx, rsi
    mov r8d, r12d
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]
    test eax, eax
    jz .close_err
    jmp .rd_lp

.done_src:
    mov rcx, rdi
    call qword [rel __imp_CloseHandle]
    clc
    @epilog_ret

.close_err:
    call qword [rel __imp_GetLastError]
    mov ecx, eax
    call map_win32_to_err64
    mov r12d, eax
    mov rcx, rdi
    call qword [rel __imp_CloseHandle]
    mov eax, r12d
.err:
    stc
    @epilog_ret
@endproc

;; @proc bi_copy64
;; @abi win64_ms
;; @in RCX = `[/A] [/B] [/V] [/Y] [/-Y] [/Z] src1 [+ src2 ...] [dst]`
;; @out Copies single/wildcard files, concatenates `a + b + c dst`, and supports `COPY CON file`!
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements COPY including `+` multi-source concatenation and `COPY CON`.
@proc_frame bi_copy64, 200, rbp, rbx, rsi, rdi, r12, r13
    ; [rsp + 48..175] : up to 16 source qword ptrs (rbp + 0..127)
    ; [rbp + 128]     : ascii flag (dword)
    ; [rbp + 132]     : src_cnt (dword)
    ; [rbp + 136]     : dst ptr (qword)
    mov rsi, rcx
    lea rbp, [rsp + 48]
    mov dword [rbp + 128], 0
    mov dword [rbp + 132], 0
    mov qword [rbp + 136], 0
    xor ebx, ebx ; expect_concat_src

.tok_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .exec_copy

    cmp byte [rsi], '/'
    jne .chk_plus
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'A'
    jne .chk_b_sw
    mov dword [rbp + 128], 1
    jmp .skip_sw
.chk_b_sw:
    cmp al, 'B'
    jne .skip_sw
    mov dword [rbp + 128], 0
.skip_sw:
    mov rcx, rsi
    call prs_read_word64
    mov rsi, rdx
    jmp .tok_lp

.chk_plus:
    cmp byte [rsi], '+'
    jne .read_item
    mov ebx, 1
    inc rsi
    jmp .tok_lp

.read_item:
    mov rdi, rsi
    xor ecx, ecx
.it_sc:
    mov al, byte [rdi]
    test al, al
    jz .it_got
    cmp al, '"'
    jne .it_d
    xor ecx, 1
    inc rdi
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
    inc rdi
    jmp .it_sc

.it_got:
    mov r12, rdi ; next cursor
    mov r13, rdi
    sub r13, rsi ; token len
    lea rcx, [r13 + 1]
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov rdx, rax
    mov rcx, r13
    rep movsb
    mov byte [rdi], 0
    mov rcx, rdx
    call tok_strip_quotes64
    mov rsi, r12
    jc .err

    mov ecx, dword [rbp + 132]
    test ecx, ecx
    jz .push_src
    test ebx, ebx
    jnz .push_src
    mov qword [rbp + 136], rax
    jmp .tok_lp

.push_src:
    cmp ecx, 15
    jae .tok_lp
    mov qword [rbp + rcx * 8], rax
    inc ecx
    mov dword [rbp + 132], ecx
    xor ebx, ebx
    jmp .tok_lp

.exec_copy:
    cmp dword [rbp + 132], 0
    je .syn_err

    mov rdi, qword [rbp + 136]
    test rdi, rdi
    jnz .norm_dst
    lea rdi, [rel szDot64]
.norm_dst:
    mov rcx, rdi
    call dev_match_name64
    test rax, rax
    jnz .got_dst_dev
    mov rcx, rdi
    call path_normalize64
    jc .rep_err
    mov rdi, rax
    jmp .chk_dst_dir
.got_dst_dev:
    mov rdi, rax
    jmp .open_dst_stream

.chk_dst_dir:
    mov rcx, rdi
    call hal_get_attr64
    jc .open_dst_stream
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .open_dst_stream
    mov rbx, qword [rbp]
    mov rcx, rbx
    call str_len64
    mov rdx, rax
.cp_bsc:
    test rdx, rdx
    jz .cp_bgot
    cmp byte [rbx + rdx - 1], 0x5C
    je .cp_bgot
    cmp byte [rbx + rdx - 1], '/'
    je .cp_bgot
    cmp byte [rbx + rdx - 1], ':'
    je .cp_bgot
    dec rdx
    jmp .cp_bsc
.cp_bgot:
    lea r12, [rbx + rdx]
    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rsi, rax
    mov rcx, rsi
    mov rdx, rdi
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .cp_bapp
    mov byte [rax], 0x5C
    inc rax
.cp_bapp:
    mov rcx, rax
    mov rdx, r12
    call str_copy64
    mov rdi, rsi

.open_dst_stream:
    mov rcx, rdi
    mov edx, GENERIC_WRITE
    mov r8d, CREATE_ALWAYS
    xor r9d, r9d
    call hal_open_file64
    jc .rep_err
    mov rbx, rax ; hDst

    xor edi, edi
.src_lp:
    cmp edi, dword [rbp + 132]
    jae .copy_done_ok
    mov rcx, qword [rbp + rdi * 8]
    mov rdx, rbx
    mov r8b, byte [rbp + 128]
    call copy_append_stream64
    jc .close_dst_err
    inc edi
    jmp .src_lp

.copy_done_ok:
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    lea rcx, [rel szOneFileCopied64]
    call con_print_z64
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.close_dst_err:
    mov r12d, eax
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    mov eax, r12d
    jmp .rep_err

.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_type64
;; @abi win64_ms
;; @in RCX = `file1 [file2 ...]`
;; @out Streams file contents to stdout (stripping UTF-8 BOM or transcoding UTF-16LE BOM!)
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements TYPE with multi-file stderr header mode and UTF-8 / UTF-16LE BOM support.
@proc_frame bi_type64, 64, rbp, rbx, rsi, rdi, r12
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .syn_err

    mov rcx, rsi
    call prs_read_word64
    mov rcx, rdx
    call prs_skip_ws64
    xor ebp, ebp
    cmp byte [rax], 0
    setne bl
    movzx ebp, bl ; multi_hdr (0/1)

.file_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .ok
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov r12, rdx ; rest
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rbx, rax ; dispName

    test ebp, ebp
    jz .open_it
    lea rcx, [rel szCrLf64]
    call con_print_err_z64
    mov rcx, rbx
    call con_print_err_z64
    lea rcx, [rel szCrLf64]
    call con_print_err_z64
    lea rcx, [rel szCrLf64]
    call con_print_err_z64

.open_it:
    mov rcx, rbx
    mov edx, GENERIC_READ
    mov r8d, OPEN_EXISTING
    xor r9d, r9d
    call hal_open_file64
    jc .rep_err
    mov rbx, rax ; hFile

    mov ecx, 16384
    call arena_alloc64
    jc .close_err
    mov rdi, rax ; buf

    mov dword [rsp + 48], 0
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, 16384
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    mov ecx, dword [rsp + 48]
    test ecx, ecx
    jz .close_next

    cmp ecx, 2
    jb .emit_raw_first
    cmp byte [rdi], 0xFF
    jne .chk_u8_t
    cmp byte [rdi + 1], 0xFE
    jne .chk_u8_t
    sub ecx, 2
    shr ecx, 1
    mov esi, ecx ; wlen
    mov ecx, 49152
    call arena_alloc64
    jc .close_err
    mov r8, rax
    mov qword [rsp + 56], rax
    lea rcx, [rdi + 2]
    mov edx, esi
    mov r9d, 49148
    call utf16le_to_wtf8_buf64
    mov rdx, rax
    mov rcx, qword [rsp + 56]
    call con_write_out64
    jmp .close_next

.chk_u8_t:
    mov rsi, rdi
    mov edx, ecx
    cmp edx, 3
    jb .emit_raw_s
    cmp byte [rdi], 0xEF
    jne .emit_raw_s
    cmp byte [rdi + 1], 0xBB
    jne .emit_raw_s
    cmp byte [rdi + 2], 0xBF
    jne .emit_raw_s
    add rsi, 3
    sub edx, 3
    jmp .emit_raw_s
.emit_raw_first:
    mov rsi, rdi
    mov edx, ecx
.emit_raw_s:
    mov rcx, rsi
    call con_write_out64

.chunk_lp:
    mov dword [rsp + 48], 0
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, 16384
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    test eax, eax
    jz .close_next
    mov edx, dword [rsp + 48]
    test edx, edx
    jz .close_next
    mov rcx, rdi
    call con_write_out64
    jmp .chunk_lp

.close_next:
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    mov rsi, r12
    jmp .file_lp

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.close_err:
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    jmp .err
.syn_err:
    mov eax, ERR_SYNTAX
.rep_err:
    mov ecx, eax
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_truename64
;; @abi win64_ms
;; @in RCX = `[path]`
;; @out Prints canonicalized full path + CRLF
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements TRUENAME.
@proc_frame bi_truename64, 40, rbx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    jne .norm
    lea rsi, [rel szDot64]
.norm:
    mov rcx, rsi
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call path_normalize64
    jc .err
    mov rcx, rax
    call con_print_z64
    call con_print_crlf64
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_vol64
;; @abi win64_ms
;; @in RCX = optional `X:` drive spec
;; @out Prints Volume Label and Volume Serial Number (XXXX-XXXX) via GetVolumeInformationW
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements VOL [drive:].
@proc_frame bi_vol64, 96, rbx, rsi, rdi
    ; [rsp + 64..71] = "X:", [rsp + 72] = dwSerial, [rsp + 76..88] = hexbuf
    lea rbx, [rsp + 64]
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .cur_drv
    mov al, byte [rsi]
    call ascii_upper_al64
    jmp .build_root

.cur_drv:
    mov ecx, 260
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov rcx, rdi
    mov edx, 256
    call hal_get_cwd64
    jc .err
    mov al, byte [rdi]
    call ascii_upper_al64

.build_root:
    mov byte [rbx], al
    mov byte [rbx + 1], ':'
    mov byte [rbx + 2], 0x5C
    mov byte [rbx + 3], 0
    mov byte [rbx + 4], al

    mov ecx, 1024
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov word [rdi], 0
    mov dword [rbx + 8], 0

    mov rcx, rbx
    call wtf8_to_utf16le64
    jc .err
    mov rcx, rax
    mov rdx, rdi
    mov r8d, 260
    lea r9, [rbx + 8]
    mov qword [rsp + 32], 0
    mov qword [rsp + 40], 0
    mov qword [rsp + 48], 0
    mov dword [rsp + 56], 0
    call qword [rel __imp_GetVolumeInformationW]
    test eax, eax
    jz .err

    mov rcx, rdi
    call utf16le_to_wtf8_arena64
    jc .err
    mov rdi, rax

    lea rcx, [rel szVolInDrive64]
    call con_print_z64
    lea rcx, [rbx + 4]
    mov edx, 1
    call con_write_out64
    cmp byte [rdi], 0
    je .no_lbl
    lea rcx, [rel szVolIs64]
    call con_print_z64
    mov rcx, rdi
    call con_print_z64
    call con_print_crlf64
    jmp .prt_ser
.no_lbl:
    lea rcx, [rel szVolHasNoLbl64]
    call con_print_z64

.prt_ser:
    mov eax, dword [rbx + 8]
    test eax, eax
    jz .ok
    lea rcx, [rel szVolSerialIs64]
    call con_print_z64
    mov ecx, dword [rbx + 8]
    lea rdx, [rbx + 12]
    call fmt_hex32_8_64
    lea rcx, [rbx + 12]
    mov edx, 4
    call con_write_out64
    lea rcx, [rel szDash64]
    mov edx, 1
    call con_write_out64
    lea rcx, [rbx + 16]
    mov edx, 4
    call con_write_out64
    call con_print_crlf64

.ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc bi_date64
;; @abi win64_ms
;; @in RCX = `[/T]`
;; @out Prints current date
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements DATE [/T].
@proc_frame bi_date64, 32, rsi
    call prs_skip_ws64
    cmp byte [rax], '/'
    je .t_only
    lea rcx, [rel szCurDateIs64]
    call con_print_z64
.t_only:
    lea rcx, [rel szVarDate64]
    call env_get64
    test rax, rax
    jz .done
    mov rcx, rax
    call con_print_z64
    call con_print_crlf64
.done:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_time64
;; @abi win64_ms
;; @in RCX = `[/T]`
;; @out Prints current time
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements TIME [/T].
@proc_frame bi_time64, 32, rsi
    call prs_skip_ws64
    cmp byte [rax], '/'
    je .t_only
    lea rcx, [rel szCurTimeIs64]
    call con_print_z64
.t_only:
    lea rcx, [rel szVarTime64]
    call env_get64
    test rax, rax
    jz .done
    mov rcx, rax
    call con_print_z64
    call con_print_crlf64
.done:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret
@endproc

;; @proc bi_mode64
;; @abi win64_ms
;; @in RCX = `[CON[:] [COLS=c] [LINES=n]]`
;; @out Queries codepage/console status
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements MODE CON and codepage status reporting.
@proc_frame bi_mode64, 32, rsi
    lea rcx, [rel szEmpty64]
    call bi_chcp64
    @epilog_ret
@endproc

;; @proc bi_cmd_subshell64
;; @abi win64_ms
;; @in RCX = arguments to subshell (`[/C | /K | /V:ON ...]`)
;; @out Spawns nested WINCMD64.EXE instance with inherited handles and environment
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements CMD / COMMAND subshell launch.
@proc_frame bi_cmd_subshell64, 32, rsi
    mov rdx, rcx
    lea rcx, [rel g_self_exe]
    xor r8d, r8d
    call exec_external64
    @epilog_ret
@endproc

;; @proc bi_start64
;; @abi win64_ms
;; @in RCX = `["title"] [/D path] [/B] [/WAIT] [/MIN] [/MAX] [/NODE n] [/AFFINITY hex] [cmd args...]`
;; @out Launches target command/process (with /B, /WAIT, or 112-byte ShellExecuteExW fallback!)
;; @err Updates g_last_exit_code
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements START built-in across Windows XP Pro x64 through Windows 11.
@proc_frame bi_start64, 160, rbp, rbx, rsi, rdi, r12
    mov rsi, rcx
    xor ebx, ebx ; dwCreationFlags
    xor ebp, ebp ; bit0=/WAIT, bit1=/B

    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '"'
    jne .sw_lp
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov r12, rax
    mov rcx, rdx
    call prs_skip_ws64
    cmp byte [rax], 0
    je .run_prog_r12
    mov rsi, rax

.sw_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], '/'
    jne .prog_tok
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov rdi, rdx ; next cursor
    mov rsi, rax
    inc rsi
    mov al, byte [rsi]
    call ascii_upper_al64
    cmp al, 'B'
    je .st_b
    cmp al, 'W'
    je .st_wait
    cmp al, 'D'
    je .st_d
    cmp al, 'N'
    je .st_node
    cmp al, 'A'
    je .st_aff
    mov rsi, rdi
    jmp .sw_lp

.st_b:
    or ebp, 2
    mov rsi, rdi
    jmp .sw_lp
.st_wait:
    or ebp, 1
    mov rsi, rdi
    jmp .sw_lp
.st_d:
    mov rcx, rdi
    call prs_read_word64
    jc .err
    mov rdi, rdx
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rcx, rax
    call hal_set_cwd64
    mov rsi, rdi
    jmp .sw_lp
.st_node:
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'O' ; /NODE vs /NORMAL
    jne .skip_st_sw
    mov rcx, rdi
    call prs_read_word64
    jc .err
    mov rsi, rdx
    jmp .sw_lp
.st_aff:
    mov al, byte [rsi + 1]
    call ascii_upper_al64
    cmp al, 'F' ; /AFFINITY vs /ABOVENORMAL
    jne .skip_st_sw
    mov rcx, rdi
    call prs_read_word64
    jc .err
    mov rsi, rdx
    jmp .sw_lp
.skip_st_sw:
    mov rsi, rdi
    jmp .sw_lp

.run_prog_r12:
    mov rsi, r12
    lea rdi, [rel szEmpty64]
    jmp .do_launch

.prog_tok:
    cmp byte [rsi], 0
    jne .rd_prog
    lea rsi, [rel g_self_exe]
    lea rdi, [rel szEmpty64]
    jmp .do_launch
.rd_prog:
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov rdi, rdx ; tail
    mov rsi, rax ; prog

.do_launch:
    mov rcx, rsi
    mov rdx, rdi
    mov r8d, ebx
    call exec_external64
    jc .try_shellex
    mov dword [rel g_last_exit_code], eax
    @epilog_ret

.try_shellex:
    cmp byte [rel g_shell32_loaded], 0
    jne .chk_sh_fn
    mov byte [rel g_shell32_loaded], 1
    lea rcx, [rel szDllShell32_64]
    call qword [rel __imp_LoadLibraryA]
    test rax, rax
    jz .err
    mov rcx, rax
    lea rdx, [rel szFnShellExecExW64]
    call dyn_resolve_proc64
    mov qword [rel pfnShellExecuteExW64], rax
.chk_sh_fn:
    cmp qword [rel pfnShellExecuteExW64], 0
    je .err

    mov rcx, rsi
    call tok_strip_quotes64
    jc .err
    mov r12, rax

    ; Zero 112-byte SHELLEXECUTEINFOW64 at [rsp + 40..151]
    mov rbx, rdi ; save tail
    lea rdi, [rsp + 40]
    xor eax, eax
    mov ecx, 14
    rep stosq

    mov dword [rsp + 40 + SHELLEXECUTEINFOW64.cbSize], 112
    mov dword [rsp + 40 + SHELLEXECUTEINFOW64.fMask], 0x0040 ; SEE_MASK_NOCLOSEPROCESS
    mov dword [rsp + 40 + SHELLEXECUTEINFOW64.nShow], 1      ; SW_SHOWNORMAL

    mov rcx, r12
    call wtf8_to_utf16le64
    jc .err
    mov qword [rsp + 40 + SHELLEXECUTEINFOW64.lpFile], rax
    mov rcx, rbx
    call wtf8_to_utf16le64
    jc .err
    mov qword [rsp + 40 + SHELLEXECUTEINFOW64.lpParameters], rax

    lea rcx, [rsp + 40]
    call qword [rel pfnShellExecuteExW64]
    test eax, eax
    jz .err

    mov rbx, qword [rsp + 40 + SHELLEXECUTEINFOW64.hProcess]
    test rbx, rbx
    jz .shex_ok
    test ebp, 1
    jz .shex_close
    mov rcx, rbx
    mov edx, INFINITE_WAIT
    call qword [rel __imp_WaitForSingleObject]
.shex_close:
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
.shex_ok:
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc

;; @proc dir_enum_ads_streams64
;; @abi win64_ms
;; @in RCX = full file/dir path (WTF-8), RDX = display filename
;; @out Prints non-default NTFS Alternate Data Streams (`filename:stream:$DATA`) on Vista..11 when DIR /R is active!
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queries FindFirstStreamW / FindNextStreamW (WIN32_FIND_STREAM_DATA = 600B) for DIR /R.
@proc_frame dir_enum_ads_streams64, 72, rbp, rbx, rsi, rdi
    cmp qword [rel pfnFindFirstStreamW64], 0
    je .done
    cmp qword [rel pfnFindNextStreamW64], 0
    je .done

    mov rsi, rcx
    mov rbp, rdx ; dispName

    mov rcx, rsi
    call nt64_path_to_w
    jc .done
    mov rbx, rax

    mov ecx, 608
    call arena_alloc64
    jc .done
    mov rdi, rax

    mov rcx, rbx
    xor edx, edx ; FindStreamInfoStandard = 0
    mov r8, rdi
    xor r9d, r9d
    call qword [rel pfnFindFirstStreamW64]
    cmp rax, INVALID_HANDLE_VALUE
    je .done
    mov rbx, rax ; hStreamFind

.st_lp:
    lea rcx, [rdi + 8]
    call utf16le_to_wtf8_arena64
    jc .st_close
    mov rsi, rax
    cmp byte [rsi], ':'
    jne .st_nx
    cmp byte [rsi + 1], ':'
    je .st_nx ; skip primary `::$DATA`

    lea rcx, [rel szDirStreamIndent64]
    call con_print_z64
    mov rcx, qword [rdi]
    lea rdx, [rsp + 32]
    call fmt_u64_commas64
    mov edx, eax
    lea rcx, [rsp + 32]
    call con_write_out64

    lea rcx, [rel szOneSpace64]
    mov edx, 1
    call con_write_out64
    mov rcx, rbp
    call con_print_z64
    mov rcx, rsi
    call con_print_z64
    call con_print_crlf64

.st_nx:
    mov rcx, rbx
    mov rdx, rdi
    call qword [rel pfnFindNextStreamW64]
    test eax, eax
    jnz .st_lp

.st_close:
    mov rcx, rbx
    call qword [rel __imp_FindClose]
.done:
    @epilog_ret
@endproc

;; @proc dir_one_folder64
;; @abi win64_ms
;; @in RCX = normalized folder path, RDX = filename glob pattern (e.g. "*"), R8D = flags (bit0=/B, bit1=/S, bit2=/X, bit3=/R, bit4=/A)
;; @out Enumerates folder entries, displays `<DIR>`, `<JUNCTION>`, `<SYMLINK>`, `<SYMLINKD>`, `/X` 8.3, `/R` ADS, and 64-bit free bytes!
;; @err CF=1 if zero files matched and not /S
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Core 64-bit DIR formatter supporting NT reparse tags, NTFS ADS streams, 8.3 /X, and 64-bit disk totals.
@proc_frame dir_one_folder64, 168, rbp, rbx, rsi, rdi, r12, r13
    ; [rbp + 0]  : folderPath (qword)
    ; [rbp + 8]  : globPat (qword)
    ; [rbp + 16] : flags (dword)
    ; [rbp + 20] : caseSens (dword)
    ; [rbp + 24] : fileCount (dword)
    ; [rbp + 28] : dirCount (dword)
    ; [rbp + 32] : totalBytes (qword)
    ; [rbp + 40] : hFind (qword)
    ; [rbp + 48] : pEntry (qword)
    ; [rbp + 56..119] : SYSTEMTIME / FILETIME / fmt scratch (64B)
    lea rbp, [rsp + 40]
    mov qword [rbp], rcx
    mov qword [rbp + 8], rdx
    mov dword [rbp + 16], r8d
    mov dword [rbp + 24], 0
    mov dword [rbp + 28], 0
    mov qword [rbp + 32], 0

    call dir_is_case_sensitive64
    mov dword [rbp + 20], eax

    xor eax, eax
    test dword [rbp + 16], 4
    setnz al
    mov byte [rel g_dir_need_short], al

    test dword [rbp + 16], 1
    jnz .build_search
    mov rcx, qword [rbp]
    call bi_vol64
    lea rcx, [rel szDirOfHdr64]
    call con_print_z64
    mov rcx, qword [rbp]
    call con_print_z64
    call con_print_crlf64
    call con_print_crlf64

.build_search:
    mov ecx, 2048
    call arena_alloc64
    jc .err_out
    mov rsi, rax
    mov rcx, rsi
    mov rdx, qword [rbp]
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .app_all
    mov byte [rax], 0x5C
    inc rax
.app_all:
    mov word [rax], 0x002A ; "*"

    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .err_out
    mov qword [rbp + 48], rax
    mov rdx, rax
    mov rcx, rsi
    call hal_find_first64
    mov byte [rel g_dir_need_short], 0
    jc .no_entries
    mov qword [rbp + 40], rax

.ent_lp:
    mov rdi, qword [rbp + 48]
    test dword [rbp + 16], 16
    jnz .match_pat
    test dword [rdi + FIND_ENTRY64.dwAttr], FILE_ATTRIBUTE_HIDDEN + FILE_ATTRIBUTE_SYSTEM
    jnz .ent_next

.match_pat:
    mov rcx, qword [rbp + 8]
    lea rdx, [rdi + FIND_ENTRY64.szNameWtf8]
    mov r8d, dword [rbp + 20]
    call matches_win32_glob64
    test eax, eax
    jnz .matched_ent

    mov rdi, qword [rbp + 48]
    cmp byte [rdi + FIND_ENTRY64.szShort83], 0
    je .ent_next
    mov rcx, qword [rbp + 8]
    lea rdx, [rdi + FIND_ENTRY64.szShort83]
    mov r8d, dword [rbp + 20]
    call matches_win32_glob64
    test eax, eax
    jz .ent_next

.matched_ent:
    mov rdi, qword [rbp + 48]
    test dword [rbp + 16], 1
    jz .full_row

    cmp byte [rdi + FIND_ENTRY64.szNameWtf8], '.'
    jne .prt_bare
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 1], 0
    je .ent_next
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 1], '.'
    jne .prt_bare
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 2], 0
    je .ent_next
.prt_bare:
    test dword [rbp + 16], 2
    jz .bare_leaf
    mov rcx, qword [rbp]
    call con_print_z64
    mov rcx, qword [rbp]
    call str_len64
    mov rcx, qword [rbp]
    cmp byte [rcx + rax - 1], 0x5C
    je .bare_leaf
    lea rcx, [rel szBackslashOnly64]
    mov edx, 1
    call con_write_out64
.bare_leaf:
    lea rcx, [rdi + FIND_ENTRY64.szNameWtf8]
    call con_print_z64
    call con_print_crlf64
    inc dword [rbp + 24]
    jmp .ent_next

.full_row:
    ; [rbp + 56..63] = localFileTime, [rbp + 64..79] = SYSTEMTIME16, [rbp + 80..119] = fmt text
    lea rcx, [rdi + FIND_ENTRY64.ftWriteLo]
    lea rdx, [rbp + 56]
    call qword [rel __imp_FileTimeToLocalFileTime]
    lea rcx, [rbp + 56]
    lea rdx, [rbp + 64]
    call qword [rel __imp_FileTimeToSystemTime]

    lea rbx, [rbp + 80]
    movzx ecx, word [rbp + 64 + SYSTEMTIME16.wYear]
    mov rdx, rbx
    call fmt_u64_plain64
    lea r12, [rbx + rax]
    mov byte [r12], '-'
    inc r12
    movzx ecx, word [rbp + 64 + SYSTEMTIME16.wMonth]
    mov dl, '0'
    mov r8, r12
    call fmt_u32_pad2_64
    mov r12, rax
    mov byte [r12], '-'
    inc r12
    movzx ecx, word [rbp + 64 + SYSTEMTIME16.wDay]
    mov dl, '0'
    mov r8, r12
    call fmt_u32_pad2_64
    mov r12, rax
    mov word [r12], 0x2020
    add r12, 2
    movzx ecx, word [rbp + 64 + SYSTEMTIME16.wHour]
    mov dl, '0'
    mov r8, r12
    call fmt_u32_pad2_64
    mov r12, rax
    mov byte [r12], ':'
    inc r12
    movzx ecx, word [rbp + 64 + SYSTEMTIME16.wMinute]
    mov dl, '0'
    mov r8, r12
    call fmt_u32_pad2_64
    mov dword [rax], 0x00202020
    mov rcx, rbx
    call con_print_z64

    mov eax, dword [rdi + FIND_ENTRY64.dwAttr]
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jz .chk_std_dir
    mov edx, dword [rdi + FIND_ENTRY64.dwReparseTag]
    cmp edx, IO_REPARSE_TAG_MOUNT_POINT
    je .tag_junc
    cmp edx, IO_REPARSE_TAG_SYMLINK
    jne .chk_std_dir
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jnz .tag_symd
    lea rcx, [rel szDirTagSymlink64]
    call con_print_z64
    inc dword [rbp + 24]
    jmp .opt_short_col
.tag_junc:
    lea rcx, [rel szDirTagJunction64]
    call con_print_z64
    inc dword [rbp + 28]
    jmp .opt_short_col
.tag_symd:
    lea rcx, [rel szDirTagSymlinkD64]
    call con_print_z64
    inc dword [rbp + 28]
    jmp .opt_short_col

.chk_std_dir:
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .file_sz_col
    lea rcx, [rel szDirTagDir64]
    call con_print_z64
    inc dword [rbp + 28]
    jmp .opt_short_col

.file_sz_col:
    inc dword [rbp + 24]
    mov eax, dword [rdi + FIND_ENTRY64.nSizeLo]
    mov edx, dword [rdi + FIND_ENTRY64.nSizeHi]
    shl rdx, 32
    or rax, rdx
    add qword [rbp + 32], rax

    mov rcx, rax
    lea rdx, [rbp + 80]
    call fmt_u64_commas64
    mov r12d, eax
    cmp r12d, 14
    jae .prt_sz_num
    mov ebx, 14
    sub ebx, r12d
.pad_sz:
    lea rcx, [rel szOneSpace64]
    mov edx, 1
    call con_write_out64
    dec ebx
    jnz .pad_sz
.prt_sz_num:
    lea rcx, [rbp + 80]
    call con_print_z64
    lea rcx, [rel szOneSpace64]
    mov edx, 1
    call con_write_out64

.opt_short_col:
    test dword [rbp + 16], 4
    jz .prt_long_name
    lea rcx, [rdi + FIND_ENTRY64.szShort83]
    call str_len64
    mov r12, rax
    lea rcx, [rdi + FIND_ENTRY64.szShort83]
    mov rdx, r12
    call con_write_out64
    cmp r12d, 13
    jae .prt_long_name
    mov ebx, 13
    sub ebx, r12d
.pad_83:
    lea rcx, [rel szOneSpace64]
    mov edx, 1
    call con_write_out64
    dec ebx
    jnz .pad_83

.prt_long_name:
    lea rcx, [rdi + FIND_ENTRY64.szNameWtf8]
    call con_print_z64
    call con_print_crlf64

    test dword [rbp + 16], 8
    jz .ent_next
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8], '.'
    je .ent_next
    mov ecx, 2048
    call arena_alloc64
    jc .ent_next
    mov r13, rax
    mov rcx, r13
    mov rdx, qword [rbp]
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .ads_app
    mov byte [rax], 0x5C
    inc rax
.ads_app:
    mov rcx, rax
    lea rdx, [rdi + FIND_ENTRY64.szNameWtf8]
    call str_copy64
    mov rcx, r13
    lea rdx, [rdi + FIND_ENTRY64.szNameWtf8]
    call dir_enum_ads_streams64

.ent_next:
    mov rcx, qword [rbp + 40]
    mov rdx, qword [rbp + 48]
    call hal_find_next64
    jnc .ent_lp

    mov rcx, qword [rbp + 40]
    call qword [rel __imp_FindClose]

    test dword [rbp + 16], 1
    jnz .chk_recurse

    lea rdi, [rbp + 80]
    lea rcx, [rel szDirSumPad64]
    call con_print_z64
    mov ecx, dword [rbp + 24]
    mov rdx, rdi
    call fmt_u64_plain64
    mov edx, eax
    mov rcx, rdi
    call con_write_out64
    lea rcx, [rel szDirFilesWord64]
    call con_print_z64

    mov rcx, qword [rbp + 32]
    mov rdx, rdi
    call fmt_u64_commas64
    mov edx, eax
    mov rcx, rdi
    call con_write_out64
    lea rcx, [rel szDirBytesWord64]
    call con_print_z64

    lea rcx, [rel szDirSumPad64]
    call con_print_z64
    mov ecx, dword [rbp + 28]
    mov rdx, rdi
    call fmt_u64_plain64
    mov edx, eax
    mov rcx, rdi
    call con_write_out64
    lea rcx, [rel szDirDirsWord64]
    call con_print_z64

    mov rcx, qword [rbp]
    call hal_disk_free_bytes64
    jc .no_free_b
    mov rcx, rax
    mov rdx, rdi
    call fmt_u64_commas64
    mov edx, eax
    mov rcx, rdi
    call con_write_out64
    lea rcx, [rel szDirBytesFree64]
    call con_print_z64
    jmp .chk_recurse
.no_free_b:
    call con_print_crlf64

.chk_recurse:
    test dword [rbp + 16], 2
    jz .done_ok

    mov ecx, 2048
    call arena_alloc64
    jc .done_ok
    mov rsi, rax
    mov rcx, rsi
    mov rdx, qword [rbp]
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .rc_st
    mov byte [rax], 0x5C
    inc rax
.rc_st:
    mov word [rax], 0x002A

    mov ecx, FIND_ENTRY64_SIZE
    call arena_alloc64
    jc .done_ok
    mov rdi, rax
    mov rcx, rsi
    mov rdx, rdi
    call hal_find_first64
    jc .done_ok
    mov rbx, rax ; hSubFind

.rc_lp:
    mov eax, dword [rdi + FIND_ENTRY64.dwAttr]
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .rc_nx
    test eax, FILE_ATTRIBUTE_REPARSE_POINT
    jnz .rc_nx
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8], '.'
    jne .rc_go
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 1], 0
    je .rc_nx
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 1], '.'
    jne .rc_go
    cmp byte [rdi + FIND_ENTRY64.szNameWtf8 + 2], 0
    je .rc_nx
.rc_go:
    mov ecx, 2048
    call arena_alloc64
    jc .rc_close
    mov r12, rax
    mov rcx, r12
    mov rdx, qword [rbp]
    call str_copy64
    cmp byte [rax - 1], 0x5C
    je .rc_app
    mov byte [rax], 0x5C
    inc rax
.rc_app:
    mov rcx, rax
    lea rdx, [rdi + FIND_ENTRY64.szNameWtf8]
    call str_copy64
    mov rcx, r12
    mov rdx, qword [rbp + 8]
    mov r8d, dword [rbp + 16]
    call dir_one_folder64

.rc_nx:
    mov rcx, rbx
    mov rdx, rdi
    call hal_find_next64
    jnc .rc_lp
.rc_close:
    mov rcx, rbx
    call qword [rel __imp_FindClose]

.done_ok:
    mov eax, dword [rbp + 24]
    or eax, dword [rbp + 28]
    jz .no_entries
    clc
    @epilog_ret

.no_entries:
    test dword [rbp + 16], 2
    jnz .rec_empty_ok
    stc
    @epilog_ret
.rec_empty_ok:
    clc
    @epilog_ret
.err_out:
    stc
    @epilog_ret
@endproc

;; @proc bi_dir64
;; @abi win64_ms
;; @in RCX = `[/B] [/S] [/W] [/X] [/R] [/A[:attrs]] [path]`
;; @out Displays directory listing(s) and sets g_last_exit_code
;; @err Sets g_last_exit_code = 1 when no files found
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Implements DIR across Windows XP Pro x64 through Windows 11.
@proc_frame bi_dir64, 48, rbp, rbx, rsi, rdi, r12
    mov rsi, rcx
    xor ebp, ebp ; flags: bit0=/B, bit1=/S, bit2=/X, bit3=/R, bit4=/A
    xor ebx, ebx ; target spec

.tok_lp:
    mov rcx, rsi
    call prs_skip_ws64
    mov rsi, rax
    cmp byte [rsi], 0
    je .run_dir
    cmp byte [rsi], '/'
    jne .path_tok
    mov rcx, rsi
    call prs_read_word64
    mov rdi, rdx
    mov rsi, rax
    inc rsi
.sw_ch_lp:
    mov al, byte [rsi]
    test al, al
    jz .nx_sw_w
    cmp al, '/'
    jne .dec_sw
    inc rsi
    jmp .sw_ch_lp
.dec_sw:
    call ascii_upper_al64
    inc rsi
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
    jmp .nx_sw_w
.nx_sw_w:
    mov rsi, rdi
    jmp .tok_lp

.path_tok:
    mov rcx, rsi
    call prs_read_word64
    jc .err
    mov rdi, rdx
    mov rcx, rax
    call tok_strip_quotes64
    jc .err
    mov rbx, rax
    mov rsi, rdi
    jmp .tok_lp

.run_dir:
    test rbx, rbx
    jnz .resolve_spec
    mov ecx, 2048
    call arena_alloc64
    jc .err
    mov rdi, rax
    mov rcx, rdi
    mov edx, 2040
    call hal_get_cwd64
    jc .err
    mov rcx, rdi
    lea rdx, [rel szStar64]
    mov r8d, ebp
    call dir_one_folder64
    jmp .finish

.resolve_spec:
    mov rcx, rbx
    call path_normalize64
    jc .err
    mov rbx, rax

    mov rsi, rbx
.has_w_lp:
    mov al, byte [rsi]
    test al, al
    jz .chk_if_dir
    cmp al, '*'
    je .split_dir_pat
    cmp al, '?'
    je .split_dir_pat
    inc rsi
    jmp .has_w_lp

.chk_if_dir:
    mov rcx, rbx
    call hal_get_attr64
    jc .split_dir_pat
    test eax, FILE_ATTRIBUTE_DIRECTORY
    jz .split_dir_pat
    mov rcx, rbx
    lea rdx, [rel szStar64]
    mov r8d, ebp
    call dir_one_folder64
    jmp .finish

.split_dir_pat:
    mov rcx, rbx
    call str_len64
    mov r12, rax
.sp_lp:
    test r12, r12
    jz .err
    cmp byte [rbx + r12 - 1], 0x5C
    je .got_sp
    dec r12
    jmp .sp_lp
.got_sp:
    lea rdi, [rbx + r12] ; pattern
    cmp r12, 3
    je .root_sp
    mov byte [rbx + r12 - 1], 0
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, ebp
    call dir_one_folder64
    jmp .finish
.root_sp:
    mov ecx, 8
    call arena_alloc64
    jc .err
    mov edx, dword [rbx]
    mov dword [rax], edx
    mov byte [rax + 3], 0
    mov rcx, rax
    mov rdx, rdi
    mov r8d, ebp
    call dir_one_folder64

.finish:
    jc .not_found
    mov dword [rel g_last_exit_code], 0
    @epilog_ret

.not_found:
    mov ecx, ERR_FILE_NOT_FOUND
    call con_report_err64
.err:
    mov dword [rel g_last_exit_code], 1
    @epilog_ret
@endproc
