;; @file robocopy.asm
;; @isa 386
;; @desc Standalone IA-32 ROBOCOPY.EXE with native CreateThread /MT[:n] worker pool and bitmask ERRORLEVEL 0..16.

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc rb_u8_to_w_local
;; @abi win32
;; @in ESI = UTF-8 source, EDI = thread-local UTF-16LE buffer (2048B)
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Thread-safe UTF-8 to UTF-16LE converter using caller's thread-local stack buffer.
@proc rb_u8_to_w_local
    call u32_u8_to_w
    ret
@endproc

;; @proc rb_worker_proc
;; @abi stdcall
;; @in [esp+4] = pointer to 16-byte RB_SLOT (0:tid, 4:nthreads, 8:copied, 12:failed)
;; @out EAX = 0
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Preemptive Win32 worker thread copying assigned stride of queued file pairs in g_u32_arena.
@proc rb_worker_proc
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 4096 ; [esp+0..2047] = wsrc, [esp+2048..4095] = wdst

    mov ebp, dword [esp + 4116] ; RB_SLOT* (4096 + 4*4 + 4)
    mov ebx, dword [ebp] ; tid

.job_lp:
    cmp ebx, dword [g_rb_job_cnt]
    jae .done
    ; Each job in g_u32_arena is 1024 bytes: [0..511]=src UTF-8, [512..1023]=dst UTF-8
    mov esi, ebx
    shl esi, 10
    add esi, dword [g_u32_arena]
    lea edi, [esi + 512]

    cmp dword [g_u32_is_win9x], 0
    jne .cp_9x
    push edi
    lea edi, [esp + 4]
    call rb_u8_to_w_local
    pop esi
    lea edi, [esp + 2048]
    call rb_u8_to_w_local
    push 0
    lea eax, [esp + 2052]
    push eax
    lea eax, [esp + 8]
    push eax
    call dword [__imp__CopyFileW@12]
    jmp .chk_cp

.cp_9x:
    push 0
    push edi
    push esi
    call dword [__imp__CopyFileA@12]

.chk_cp:
    test eax, eax
    jz .cp_fail
    inc dword [ebp + 8]
    jmp .nx_job
.cp_fail:
    inc dword [ebp + 12]
.nx_job:
    add ebx, dword [ebp + 4]
    jmp .job_lp

.done:
    xor eax, eax
    add esp, 4096
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret 4
@endproc

;; @proc rb_sync_dir
;; @abi win32
;; @in ESI = src dir UTF-8, EDI = dst dir UTF-8, ECX = depth
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Queues or copies files, purges extras (/PURGE /MIR), moves (/MOV /MOVE), and recurses (/S /E).
@proc rb_sync_dir
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 2656
    cmp ecx, dword [g_rb_max_lev]
    jae .dn
    mov dword [esp + 2592], ecx
    push edi
    lea edi, [esp + 548]
    call u32_strcpy
    pop esi
    lea edi, [esp + 1056]
    call u32_strcpy

    cmp dword [g_rb_list], 0
    jne .enum_src
    lea esi, [esp + 1056]
    call u32_mkdir

.enum_src:
    lea esi, [esp + 544]
    mov edi, addr(g_rb_mask)
    lea ebx, [esp + 1568]
    call u32_path_join
    lea esi, [esp + 1568]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .chk_purge
    mov ebp, eax

.src_lp:
    cmp byte [esp + 20], '.'
    jne .not_dot
    cmp byte [esp + 21], 0
    je .src_nx
    cmp byte [esp + 21], '.'
    jne .not_dot
    cmp byte [esp + 22], 0
    je .src_nx
.not_dot:
    test dword [esp], 0x10
    jnz .src_nx

    ; Check /XF excluded filename
    cmp byte [g_rb_xf], 0
    je .not_xf
    lea esi, [esp + 20]
    mov edi, addr(g_rb_xf)
    call u32_str_eq_ci
    test eax, eax
    jnz .src_nx
.not_xf:
    lea esi, [esp + 544]
    lea edi, [esp + 20]
    lea ebx, [esp + 1568]
    call u32_path_join

    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 2080]
    call u32_path_join

    cmp dword [g_rb_nfl], 0
    jne .do_act
    mov esi, addr(szRbTab)
    call u32_write_str
    lea esi, [esp + 1568]
    call u32_write_str
    call u32_write_crlf

.do_act:
    cmp dword [g_rb_list], 0
    je .real_cp
    inc dword [g_rb_copied]
    or dword [g_rb_status], 1
    jmp .src_nx

.real_cp:
    cmp dword [g_rb_mt], 1
    jbe .sync_cp
    mov eax, dword [g_rb_job_cnt]
    cmp eax, 480
    jae .sync_cp
    ; Enqueue into g_u32_arena[eax * 1024]
    shl eax, 10
    add eax, dword [g_u32_arena]
    push eax
    lea esi, [esp + 1572]
    mov edi, eax
    call u32_strcpy
    pop eax
    lea esi, [esp + 2080]
    lea edi, [eax + 512]
    call u32_strcpy
    inc dword [g_rb_job_cnt]
    jmp .chk_mov

.sync_cp:
    lea esi, [esp + 1568]
    lea edi, [esp + 2080]
    xor ecx, ecx
    call u32_copy_file
    test eax, eax
    jz .cp_err
    inc dword [g_rb_copied]
    or dword [g_rb_status], 1
    jmp .chk_mov
.cp_err:
    or dword [g_rb_status], 8
    jmp .src_nx

.chk_mov:
    cmp dword [g_rb_mov], 0
    je .src_nx
    cmp dword [g_rb_mt], 1
    ja .src_nx
    lea esi, [esp + 1568]
    call u32_delete_file

.src_nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .src_lp
    push ebp
    call dword [__imp__FindClose@4]

.chk_purge:
    cmp dword [g_rb_purge], 0
    je .chk_sub
    lea esi, [esp + 1056]
    mov edi, addr(szRbStar)
    lea ebx, [esp + 2080]
    call u32_path_join
    lea esi, [esp + 2080]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .chk_sub
    mov ebp, eax
.pg_lp:
    test dword [esp], 0x10
    jnz .pg_nx
    lea esi, [esp + 544]
    lea edi, [esp + 20]
    lea ebx, [esp + 1568]
    call u32_path_join
    lea esi, [esp + 1568]
    call u32_get_attr
    cmp eax, -1
    jne .pg_nx
    or dword [g_rb_status], 2
    cmp dword [g_rb_list], 0
    jne .pg_nx
    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 2080]
    call u32_path_join
    lea esi, [esp + 2080]
    call u32_delete_file
.pg_nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .pg_lp
    push ebp
    call dword [__imp__FindClose@4]

.chk_sub:
    cmp dword [g_rb_sub], 0
    je .dn
    lea esi, [esp + 544]
    mov edi, addr(szRbStar)
    lea ebx, [esp + 1568]
    call u32_path_join
    lea esi, [esp + 1568]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .dn
    mov ebp, eax
.sub_lp:
    test dword [esp], 0x10
    jz .sub_nx
    cmp byte [esp + 20], '.'
    jne .chk_xd
    cmp byte [esp + 21], 0
    je .sub_nx
    cmp byte [esp + 21], '.'
    jne .chk_xd
    cmp byte [esp + 22], 0
    je .sub_nx
.chk_xd:
    cmp byte [g_rb_xd], 0
    je .go_sub
    lea esi, [esp + 20]
    mov edi, addr(g_rb_xd)
    call u32_str_eq_ci
    test eax, eax
    jnz .sub_nx
.go_sub:
    lea esi, [esp + 544]
    lea edi, [esp + 20]
    lea ebx, [esp + 1568]
    call u32_path_join
    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 2080]
    call u32_path_join
    lea esi, [esp + 1568]
    lea edi, [esp + 2080]
    mov ecx, dword [esp + 2592]
    inc ecx
    call rb_sync_dir
.sub_nx:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .sub_lp
    push ebp
    call dword [__imp__FindClose@4]

.dn:
    add esp, 2656
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess with Robocopy bitmask 0..16
;; @err Exits 16 on missing/invalid source or destination
;; @clobbers eax, ecx, edx
;; @desc Parses ROBOCOPY options (including /MT[:n] worker pool), replicates directories, and exits with 0..16 bitmask.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi
    push ebp

    call u32_init
    mov esi, eax
    mov dword [g_rb_sub], 0
    mov dword [g_rb_purge], 0
    mov dword [g_rb_mov], 0
    mov dword [g_rb_list], 0
    mov dword [g_rb_nfl], 0
    mov dword [g_rb_njh], 0
    mov dword [g_rb_njs], 0
    mov dword [g_rb_mt], 1
    mov dword [g_rb_max_lev], 32
    mov dword [g_rb_status], 0
    mov dword [g_rb_copied], 0
    mov dword [g_rb_job_cnt], 0
    mov dword [g_rb_pos_cnt], 0
    mov byte [g_rb_src], 0
    mov byte [g_rb_dst], 0
    mov byte [g_rb_xf], 0
    mov byte [g_rb_xd], 0
    push esi
    mov esi, addr(szRbStar)
    mov edi, addr(g_rb_mask)
    call u32_strcpy
    pop esi

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pos_arg
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'S'
    je .sw_s
    cmp al, 'E'
    je .sw_e
    cmp al, 'L'
    je .sw_l_or_lev
    cmp al, 'P'
    je .sw_p
    cmp al, 'M'
    je .sw_m_grp
    cmp al, 'N'
    je .sw_n_grp
    cmp al, 'X'
    je .sw_x_grp
    jmp .arg_lp

.sw_s:
    mov al, byte [g_u32_arg_a + 2]
    test al, al
    jnz .arg_lp
    mov dword [g_rb_sub], 1
    jmp .arg_lp
.sw_e:
    mov dword [g_rb_sub], 1
    jmp .arg_lp
.sw_l_or_lev:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'E'
    je .sw_lev
    mov dword [g_rb_list], 1
    jmp .arg_lp
.sw_lev:
    cmp byte [g_u32_arg_a + 4], ':'
    jne .arg_lp
    push esi
    lea esi, [g_u32_arg_a + 5]
    call u32_parse_u32
    pop esi
    mov dword [g_rb_max_lev], eax
    jmp .arg_lp
.sw_p:
    mov dword [g_rb_purge], 1
    jmp .arg_lp
.sw_m_grp:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'I' ; /MIR
    je .sw_mir
    cmp al, 'O' ; /MOV or /MOVE
    je .sw_mov
    cmp al, 'T' ; /MT or /MT:n
    je .sw_mt
    jmp .arg_lp
.sw_mir:
    mov dword [g_rb_sub], 1
    mov dword [g_rb_purge], 1
    jmp .arg_lp
.sw_mov:
    mov dword [g_rb_mov], 1
    jmp .arg_lp
.sw_mt:
    mov dword [g_rb_mt], 8
    cmp byte [g_u32_arg_a + 3], ':'
    jne .arg_lp
    push esi
    lea esi, [g_u32_arg_a + 4]
    call u32_parse_u32
    pop esi
    cmp eax, 1
    jb .mt_min
    cmp eax, 16
    jbe .mt_set
    mov eax, 16
    jmp .mt_set
.mt_min:
    mov eax, 1
.mt_set:
    mov dword [g_rb_mt], eax
    jmp .arg_lp

.sw_n_grp:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'F' ; /NFL
    je .sw_nfl
    cmp al, 'J' ; /NJH or /NJS
    jne .arg_lp
    mov al, byte [g_u32_arg_a + 3]
    call u32_chr_upper
    cmp al, 'H'
    je .sw_njh
    cmp al, 'S'
    je .sw_njs
    jmp .arg_lp
.sw_nfl:
    mov dword [g_rb_nfl], 1
    jmp .arg_lp
.sw_njh:
    mov dword [g_rb_njh], 1
    jmp .arg_lp
.sw_njs:
    mov dword [g_rb_njs], 1
    jmp .arg_lp

.sw_x_grp:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'F'
    je .sw_xf
    cmp al, 'D'
    je .sw_xd
    jmp .arg_lp
.sw_xf:
    mov edi, addr(g_rb_xf)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp
.sw_xd:
    mov edi, addr(g_rb_xd)
    call u32_read_tok
    mov esi, eax
    jmp .arg_lp

.pos_arg:
    cmp dword [g_rb_pos_cnt], 0
    je .p_src
    cmp dword [g_rb_pos_cnt], 1
    je .p_dst
    mov edi, addr(g_rb_mask)
    call u32_read_tok
    mov esi, eax
    inc dword [g_rb_pos_cnt]
    jmp .arg_lp
.p_src:
    mov edi, addr(g_rb_src)
    call u32_read_tok
    mov esi, eax
    inc dword [g_rb_pos_cnt]
    jmp .arg_lp
.p_dst:
    mov edi, addr(g_rb_dst)
    call u32_read_tok
    mov esi, eax
    inc dword [g_rb_pos_cnt]
    jmp .arg_lp

.help:
    mov esi, addr(szRbHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.run:
    cmp dword [g_rb_pos_cnt], 2
    jb .err16
    mov esi, addr(g_rb_src)
    call u32_get_attr
    cmp eax, -1
    je .err16

    cmp dword [g_rb_njh], 0
    jne .do_sync
    mov esi, addr(szRbBanner)
    call u32_write_str
    call u32_write_crlf

.do_sync:
    mov esi, addr(g_rb_src)
    mov edi, addr(g_rb_dst)
    xor ecx, ecx
    call rb_sync_dir

    ; Execute queued /MT worker pool if /MT > 1 and jobs were queued
    cmp dword [g_rb_mt], 1
    jbe .summary
    cmp dword [g_rb_job_cnt], 0
    je .summary

    mov ebp, dword [g_rb_mt]
    cmp ebp, dword [g_rb_job_cnt]
    jbe .spawn_workers
    mov ebp, dword [g_rb_job_cnt]

.spawn_workers:
    xor ebx, ebx ; tid = 0..ebp-1
.sp_lp:
    cmp ebx, ebp
    jae .wait_workers
    mov edi, ebx
    shl edi, 4
    add edi, addr(g_rb_slots)
    mov dword [edi], ebx ; tid
    mov dword [edi + 4], ebp ; nthreads
    mov dword [edi + 8], 0 ; copied
    mov dword [edi + 12], 0 ; failed

    push addr(g_rb_tid_tmp) ; lpThreadId
    push 0 ; dwCreationFlags
    push edi ; lpParameter
    push addr(rb_worker_proc) ; lpStartAddress
    push 65536 ; dwStackSize
    push 0 ; lpThreadAttributes
    call dword [__imp__CreateThread@24]
    mov dword [g_rb_threads + ebx * 4], eax
    inc ebx
    jmp .sp_lp

.wait_workers:
    push -1 ; INFINITE
    push 1 ; bWaitAll = TRUE
    push addr(g_rb_threads)
    push ebp
    call dword [__imp__WaitForMultipleObjects@16]

    xor ebx, ebx
.collect_lp:
    cmp ebx, ebp
    jae .summary
    mov eax, dword [g_rb_threads + ebx * 4]
    test eax, eax
    jz .sum_slot
    push eax
    call dword [__imp__CloseHandle@4]
.sum_slot:
    mov edi, ebx
    shl edi, 4
    add edi, addr(g_rb_slots)
    mov eax, dword [edi + 8]
    test eax, eax
    jz .chk_f
    add dword [g_rb_copied], eax
    or dword [g_rb_status], 1
.chk_f:
    cmp dword [edi + 12], 0
    je .nx_col
    or dword [g_rb_status], 8
.nx_col:
    inc ebx
    jmp .collect_lp

.summary:
    cmp dword [g_rb_njs], 0
    jne .exit_rb
    mov esi, addr(szRbSum)
    call u32_write_str
    mov eax, dword [g_rb_copied]
    call u32_print_u32
    call u32_write_crlf

.exit_rb:
    mov eax, dword [g_rb_status]
    call u32_exit
    jmp .done

.err16:
    mov esi, addr(szRbErr16)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 16
    call u32_exit
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szRbStar, "*", 0
@data db szRbTab, "    ", 0
@data db szRbBanner, "ROBOCOPY :: Robust File Copy for Windows", 0
@data db szRbSum, "Files Copied : ", 0
@data db szRbErr16, "ERROR : Invalid ROBOCOPY Source or Destination Directory.", 0
@data db szRbHelp, "Robust File and Folder Copy (src dst [file] /S /E /MIR /PURGE /MOV /MT[:n] /L /XF /XD).", 0

@include "u_data32.inc"

@res dword g_rb_sub, 1
@res dword g_rb_purge, 1
@res dword g_rb_mov, 1
@res dword g_rb_list, 1
@res dword g_rb_nfl, 1
@res dword g_rb_njh, 1
@res dword g_rb_njs, 1
@res dword g_rb_mt, 1
@res dword g_rb_max_lev, 1
@res dword g_rb_status, 1
@res dword g_rb_copied, 1
@res dword g_rb_job_cnt, 1
@res dword g_rb_pos_cnt, 1
@res dword g_rb_tid_tmp, 1
@res dword g_rb_threads, 16
@res byte g_rb_slots, 256
@res byte g_rb_src, 520
@res byte g_rb_dst, 520
@res byte g_rb_mask, 260
@res byte g_rb_xf, 260
@res byte g_rb_xd, 260
