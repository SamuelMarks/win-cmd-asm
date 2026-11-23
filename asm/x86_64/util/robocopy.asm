;; @file robocopy.asm
;; @isa x64
;; @desc Standalone x86-64 ROBOCOPY.EXE with native CreateThread /MT[:n] worker pool and bitmask ERRORLEVEL 0..16.

@cpu x64
@format coff64
@entry mainCRTStartup
@global rb64_worker_proc
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc rb64_worker_proc
;; @abi win64_ms
;; @in RCX = pointer to 16-byte RB_SLOT (0:tid, 4:nthreads, 8:copied, 12:failed)
;; @out EAX = 0
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc GUARD_CF/CET-valid Win64 worker thread copying its stride of queued file pairs using thread-local UTF-16LE buffers.
@proc_frame rb64_worker_proc, 4160, rbx, rsi, rdi
    mov rdi, rcx ; RB_SLOT*
    mov ebx, dword [rdi] ; tid

.jlp:
    cmp ebx, dword [rel g_rb64_job_cnt]
    jae .dn
    mov rsi, rbx
    shl rsi, 10
    add rsi, qword [rel g_u64_arena]

    mov rcx, rsi
    lea rdx, [rsp + 64]
    call u64_u8_to_w

    lea rcx, [rsi + 512]
    lea rdx, [rsp + 2112]
    call u64_u8_to_w

    lea rcx, [rsp + 64]
    lea rdx, [rsp + 2112]
    xor r8d, r8d
    call qword [rel __imp_CopyFileW]
    test eax, eax
    jz .fail
    inc dword [rdi + 8]
    jmp .nx
.fail:
    inc dword [rdi + 12]
.nx:
    add ebx, dword [rdi + 4]
    jmp .jlp
.dn:
    xor eax, eax
    @epilog_ret
@endproc

;; @proc rb64_sync_dir
;; @abi win64_ms
;; @in RCX = src dir, RDX = dst dir, R8D = depth
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Queues or copies files, purges extras (/PURGE /MIR), moves (/MOV), and recurses (/S /E).
@proc_frame rb64_sync_dir, 2720, rbx, rsi, rdi, r12, r13
    cmp r8d, dword [rel g_rb64_max_lev]
    jae .dn
    mov r12d, r8d
    mov rsi, rdx
    mov rdx, rcx
    lea rcx, [rsp + 608]
    call u64_strcpy
    lea rcx, [rsp + 1120]
    mov rdx, rsi
    call u64_strcpy

    cmp dword [rel g_rb64_list], 0
    jne .en
    lea rcx, [rsp + 1120]
    call u64_mkdir

.en:
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 608]
    lea r8, [rel g_rb64_mask]
    call u64_path_join
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .purge
    mov rbx, rax

.slp:
    cmp byte [rsp + 88], '.'
    jne .nd
    cmp byte [rsp + 89], 0
    je .snx
    cmp byte [rsp + 89], '.'
    jne .nd
    cmp byte [rsp + 90], 0
    je .snx
.nd:
    test dword [rsp + 64], 0x10
    jnz .snx
    cmp byte [rel g_rb64_xf], 0
    je .nxf
    lea rcx, [rsp + 88]
    lea rdx, [rel g_rb64_xf]
    call u64_str_eq_ci
    test eax, eax
    jnz .snx
.nxf:
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 608]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 2144]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join

    cmp dword [rel g_rb64_nfl], 0
    jne .act
    lea rcx, [rel szRb64Tab]
    call u64_write_str
    lea rcx, [rsp + 1632]
    call u64_write_str
    call u64_write_crlf

.act:
    cmp dword [rel g_rb64_list], 0
    je .real
    inc dword [rel g_rb64_copied]
    or dword [rel g_rb64_status], 1
    jmp .snx

.real:
    cmp dword [rel g_rb64_mt], 1
    jbe .sync
    mov eax, dword [rel g_rb64_job_cnt]
    cmp eax, 480
    jae .sync
    shl rax, 10
    add rax, qword [rel g_u64_arena]
    mov r13, rax
    mov rcx, r13
    lea rdx, [rsp + 1632]
    call u64_strcpy
    lea rcx, [r13 + 512]
    lea rdx, [rsp + 2144]
    call u64_strcpy
    inc dword [rel g_rb64_job_cnt]
    jmp .snx

.sync:
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 2144]
    xor r8d, r8d
    call u64_copy_file
    test eax, eax
    jz .cerr
    inc dword [rel g_rb64_copied]
    or dword [rel g_rb64_status], 1
    cmp dword [rel g_rb64_mov], 0
    je .snx
    lea rcx, [rsp + 1632]
    call u64_delete_file
    jmp .snx
.cerr:
    or dword [rel g_rb64_status], 8

.snx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .slp
    mov rcx, rbx
    call qword [rel __imp_FindClose]

.purge:
    cmp dword [rel g_rb64_purge], 0
    je .sub
    lea rcx, [rsp + 2144]
    lea rdx, [rsp + 1120]
    lea r8, [rel szRb64Star]
    call u64_path_join
    lea rcx, [rsp + 2144]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .sub
    mov rbx, rax
.plp:
    test dword [rsp + 64], 0x10
    jnz .pnx
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 608]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 1632]
    call u64_get_attr
    cmp eax, -1
    jne .pnx
    or dword [rel g_rb64_status], 2
    cmp dword [rel g_rb64_list], 0
    jne .pnx
    lea rcx, [rsp + 2144]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 2144]
    call u64_delete_file
.pnx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .plp
    mov rcx, rbx
    call qword [rel __imp_FindClose]

.sub:
    cmp dword [rel g_rb64_sub], 0
    je .dn
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 608]
    lea r8, [rel szRb64Star]
    call u64_path_join
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .dn
    mov rbx, rax
.dlp:
    test dword [rsp + 64], 0x10
    jz .dnx
    cmp byte [rsp + 88], '.'
    jne .cxd
    cmp byte [rsp + 89], 0
    je .dnx
    cmp byte [rsp + 89], '.'
    jne .cxd
    cmp byte [rsp + 90], 0
    je .dnx
.cxd:
    cmp byte [rel g_rb64_xd], 0
    je .go
    lea rcx, [rsp + 88]
    lea rdx, [rel g_rb64_xd]
    call u64_str_eq_ci
    test eax, eax
    jnz .dnx
.go:
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 608]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 2144]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 1632]
    lea rdx, [rsp + 2144]
    lea r8d, [r12d + 1]
    call rb64_sync_dir
.dnx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .dlp
    mov rcx, rbx
    call qword [rel __imp_FindClose]
.dn:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess with Robocopy bitmask 0..16
;; @err Exits 16 on missing/invalid source or destination
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 ROBOCOPY.EXE.
@proc_frame mainCRTStartup, 64, rbx, rsi, rdi, r12, r13
    call u64_init
    mov rsi, rax
    mov dword [rel g_rb64_sub], 0
    mov dword [rel g_rb64_purge], 0
    mov dword [rel g_rb64_mov], 0
    mov dword [rel g_rb64_list], 0
    mov dword [rel g_rb64_nfl], 0
    mov dword [rel g_rb64_njh], 0
    mov dword [rel g_rb64_njs], 0
    mov dword [rel g_rb64_mt], 1
    mov dword [rel g_rb64_max_lev], 32
    mov dword [rel g_rb64_status], 0
    mov dword [rel g_rb64_copied], 0
    mov dword [rel g_rb64_job_cnt], 0
    mov dword [rel g_rb64_pos], 0
    mov byte [rel g_rb64_src], 0
    mov byte [rel g_rb64_dst], 0
    mov byte [rel g_rb64_xf], 0
    mov byte [rel g_rb64_xd], 0
    lea rcx, [rel g_rb64_mask]
    lea rdx, [rel szRb64Star]
    call u64_strcpy

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pos
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'S'
    je .ss
    cmp al, 'E'
    je .se
    cmp al, 'L'
    je .sl
    cmp al, 'P'
    je .sp
    cmp al, 'M'
    je .sm
    cmp al, 'N'
    je .sn
    cmp al, 'X'
    je .sx
    jmp .arg_lp
.ss:
    cmp byte [rel g_u64_arg_a + 2], 0
    jne .arg_lp
    mov dword [rel g_rb64_sub], 1
    jmp .arg_lp
.se:
    mov dword [rel g_rb64_sub], 1
    jmp .arg_lp
.sl:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'E'
    je .slev
    mov dword [rel g_rb64_list], 1
    jmp .arg_lp
.slev:
    cmp byte [rel g_u64_arg_a + 4], ':'
    jne .arg_lp
    lea rcx, [rel g_u64_arg_a + 5]
    call u64_parse_u32
    mov dword [rel g_rb64_max_lev], eax
    jmp .arg_lp
.sp:
    mov dword [rel g_rb64_purge], 1
    jmp .arg_lp
.sm:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'I'
    je .smir
    cmp al, 'O'
    je .smov
    cmp al, 'T'
    je .smt
    jmp .arg_lp
.smir:
    mov dword [rel g_rb64_sub], 1
    mov dword [rel g_rb64_purge], 1
    jmp .arg_lp
.smov:
    mov dword [rel g_rb64_mov], 1
    jmp .arg_lp
.smt:
    mov dword [rel g_rb64_mt], 8
    cmp byte [rel g_u64_arg_a + 3], ':'
    jne .arg_lp
    lea rcx, [rel g_u64_arg_a + 4]
    call u64_parse_u32
    cmp eax, 1
    jb .mt1
    cmp eax, 16
    jbe .mts
    mov eax, 16
    jmp .mts
.mt1:
    mov eax, 1
.mts:
    mov dword [rel g_rb64_mt], eax
    jmp .arg_lp
.sn:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'F'
    je .snfl
    cmp al, 'J'
    jne .arg_lp
    mov al, byte [rel g_u64_arg_a + 3]
    call u64_chr_upper
    cmp al, 'H'
    je .snjh
    cmp al, 'S'
    je .snjs
    jmp .arg_lp
.snfl:
    mov dword [rel g_rb64_nfl], 1
    jmp .arg_lp
.snjh:
    mov dword [rel g_rb64_njh], 1
    jmp .arg_lp
.snjs:
    mov dword [rel g_rb64_njs], 1
    jmp .arg_lp
.sx:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'F'
    je .sxf
    cmp al, 'D'
    je .sxd
    jmp .arg_lp
.sxf:
    mov rcx, rsi
    lea rdx, [rel g_rb64_xf]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.sxd:
    mov rcx, rsi
    lea rdx, [rel g_rb64_xd]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.pos:
    cmp dword [rel g_rb64_pos], 0
    je .ps
    cmp dword [rel g_rb64_pos], 1
    je .pd
    mov rcx, rsi
    lea rdx, [rel g_rb64_mask]
    call u64_read_tok
    mov rsi, rax
    inc dword [rel g_rb64_pos]
    jmp .arg_lp
.ps:
    mov rcx, rsi
    lea rdx, [rel g_rb64_src]
    call u64_read_tok
    mov rsi, rax
    inc dword [rel g_rb64_pos]
    jmp .arg_lp
.pd:
    mov rcx, rsi
    lea rdx, [rel g_rb64_dst]
    call u64_read_tok
    mov rsi, rax
    inc dword [rel g_rb64_pos]
    jmp .arg_lp

.help:
    lea rcx, [rel szRb64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.run:
    cmp dword [rel g_rb64_pos], 2
    jb .err16
    lea rcx, [rel g_rb64_src]
    call u64_get_attr
    cmp eax, -1
    je .err16

    cmp dword [rel g_rb64_njh], 0
    jne .do_s
    lea rcx, [rel szRb64Ban]
    call u64_write_str
    call u64_write_crlf

.do_s:
    lea rcx, [rel g_rb64_src]
    lea rdx, [rel g_rb64_dst]
    xor r8d, r8d
    call rb64_sync_dir

    cmp dword [rel g_rb64_mt], 1
    jbe .sum
    cmp dword [rel g_rb64_job_cnt], 0
    je .sum

    mov r12d, dword [rel g_rb64_mt]
    cmp r12d, dword [rel g_rb64_job_cnt]
    jbe .spw
    mov r12d, dword [rel g_rb64_job_cnt]
.spw:
    xor ebx, ebx
.sp_lp:
    cmp ebx, r12d
    jae .wait_w
    mov rax, rbx
    shl rax, 4
    lea rdi, [rel g_rb64_slots]
    add rdi, rax
    mov dword [rdi], ebx
    mov dword [rdi + 4], r12d
    mov dword [rdi + 8], 0
    mov dword [rdi + 12], 0

    xor ecx, ecx ; lpThreadAttributes
    mov edx, 65536 ; dwStackSize
    lea r8, [rel rb64_worker_proc]
    mov r9, rdi ; lpParameter
    mov dword [rsp + 32], 0
    lea rax, [rel g_rb64_tid]
    mov qword [rsp + 40], rax
    call qword [rel __imp_CreateThread]
    lea rcx, [rel g_rb64_threads]
    mov qword [rcx + rbx * 8], rax
    inc ebx
    jmp .sp_lp

.wait_w:
    mov ecx, r12d
    lea rdx, [rel g_rb64_threads]
    mov r8d, 1 ; bWaitAll
    mov r9d, -1 ; INFINITE
    call qword [rel __imp_WaitForMultipleObjects]

    xor ebx, ebx
.col_lp:
    cmp ebx, r12d
    jae .sum
    lea rax, [rel g_rb64_threads]
    mov rcx, qword [rax + rbx * 8]
    test rcx, rcx
    jz .sc
    call qword [rel __imp_CloseHandle]
.sc:
    mov rax, rbx
    shl rax, 4
    lea rdi, [rel g_rb64_slots]
    add rdi, rax
    mov eax, dword [rdi + 8]
    test eax, eax
    jz .cf
    add dword [rel g_rb64_copied], eax
    or dword [rel g_rb64_status], 1
.cf:
    cmp dword [rdi + 12], 0
    je .nxc
    or dword [rel g_rb64_status], 8
.nxc:
    inc ebx
    jmp .col_lp

.sum:
    cmp dword [rel g_rb64_njs], 0
    jne .ex
    lea rcx, [rel szRb64Sum]
    call u64_write_str
    mov ecx, dword [rel g_rb64_copied]
    call u64_print_u64
    call u64_write_crlf
.ex:
    mov ecx, dword [rel g_rb64_status]
    call u64_exit
    @epilog_ret

.err16:
    lea rcx, [rel szRb64Err]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 16
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szRb64Star, "*", 0
@data db szRb64Tab, "    ", 0
@data db szRb64Ban, "ROBOCOPY :: Robust File Copy for Windows", 0
@data db szRb64Sum, "Files Copied : ", 0
@data db szRb64Err, "ERROR : Invalid ROBOCOPY Source or Destination Directory.", 0
@data db szRb64Help, "Robust File and Folder Copy (src dst [file] /S /E /MIR /PURGE /MOV /MT[:n] /L /XF /XD).", 0

@include "u_data64.inc"

@res qword g_rb64_threads, 16
@res dword g_rb64_sub, 1
@res dword g_rb64_purge, 1
@res dword g_rb64_mov, 1
@res dword g_rb64_list, 1
@res dword g_rb64_nfl, 1
@res dword g_rb64_njh, 1
@res dword g_rb64_njs, 1
@res dword g_rb64_mt, 1
@res dword g_rb64_max_lev, 1
@res dword g_rb64_status, 1
@res dword g_rb64_copied, 1
@res dword g_rb64_job_cnt, 1
@res dword g_rb64_pos, 1
@res dword g_rb64_tid, 1
@res byte g_rb64_slots, 256
@res byte g_rb64_src, 520
@res byte g_rb64_dst, 520
@res byte g_rb64_mask, 260
@res byte g_rb64_xf, 260
@res byte g_rb64_xd, 260
