;; @file where.asm
;; @isa x64
;; @desc Standalone x86-64 WHERE.EXE executable locator ([/R dir] [/Q] [/F] [/T] pattern / $ENV:pattern).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc where64_emit_hit
;; @abi win64_ms
;; @in RCX = matched path UTF-8, RDX = 64-bit file size
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Records hit and prints path (honoring /Q, /F, /T).
@proc_frame where64_emit_hit, 48, rsi
    mov rsi, rcx
    mov dword [rel g_wh64_found], 1
    cmp dword [rel g_wh64_quiet], 0
    jne .dn
    cmp dword [rel g_wh64_time], 0
    je .cq
    mov rcx, rdx
    call u64_print_u64
    lea rcx, [rel szWh64Pad]
    call u64_write_str
.cq:
    cmp dword [rel g_wh64_quote], 0
    je .pl
    mov cl, '"'
    call u64_write_chr
    mov rcx, rsi
    call u64_write_str
    mov cl, '"'
    call u64_write_chr
    call u64_write_crlf
    @epilog_ret
.pl:
    mov rcx, rsi
    call u64_write_str
    call u64_write_crlf
.dn:
    @epilog_ret
@endproc

;; @proc where64_probe_one
;; @abi win64_ms
;; @in RCX = candidate path/wildcard, RDX = parent dir
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Enumerates matches for candidate RCX in directory RDX.
@proc_frame where64_probe_one, 1160, rbx, rsi
    mov rsi, rdx ; dir
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .dn
    mov rbx, rax
.lp:
    test dword [rsp + 64], 0x10
    jnz .nx
    lea rcx, [rsp + 608]
    mov rdx, rsi
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 608]
    mov rdx, qword [rsp + 80]
    call where64_emit_hit
.nx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .lp
    mov rcx, rbx
    call qword [rel __imp_FindClose]
.dn:
    @epilog_ret
@endproc

;; @proc where64_probe_dir
;; @abi win64_ms
;; @in RCX = directory UTF-8, RDX = pattern UTF-8
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Probes exact pattern and .COM/.EXE/.BAT/.CMD extensions in RCX.
@proc_frame where64_probe_dir, 1128, rbx, rsi, rdi, r12
    mov rsi, rcx ; dir
    mov rdi, rdx ; pat

    xor ecx, ecx
    xor r12d, r12d
.dot:
    mov al, byte [rdi + rcx]
    test al, al
    jz .exact
    cmp al, '.'
    jne .dnx
    mov r12d, 1
.dnx:
    inc ecx
    jmp .dot

.exact:
    lea rcx, [rsp + 64]
    mov rdx, rsi
    mov r8, rdi
    call u64_path_join
    lea rcx, [rsp + 64]
    mov rdx, rsi
    call where64_probe_one

    test r12d, r12d
    jnz .dn

    lea rcx, [rsp + 64]
    mov rdx, rsi
    mov r8, rdi
    call u64_path_join
    lea rcx, [rsp + 64]
    call u64_strlen
    mov rbx, rax

    lea rcx, [rsp + 64 + rbx]
    lea rdx, [rel szWh64Com]
    call u64_strcpy
    lea rcx, [rsp + 64]
    mov rdx, rsi
    call where64_probe_one

    lea rcx, [rsp + 64 + rbx]
    lea rdx, [rel szWh64Exe]
    call u64_strcpy
    lea rcx, [rsp + 64]
    mov rdx, rsi
    call where64_probe_one

    lea rcx, [rsp + 64 + rbx]
    lea rdx, [rel szWh64Bat]
    call u64_strcpy
    lea rcx, [rsp + 64]
    mov rdx, rsi
    call where64_probe_one

    lea rcx, [rsp + 64 + rbx]
    lea rdx, [rel szWh64Cmd]
    call u64_strcpy
    lea rcx, [rsp + 64]
    mov rdx, rsi
    call where64_probe_one
.dn:
    @epilog_ret
@endproc

;; @proc where64_recurse
;; @abi win64_ms
;; @in RCX = dir UTF-8, RDX = pattern UTF-8, R8D = depth
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Recursively searches directory tree (/R dir).
@proc_frame where64_recurse, 1664, rbx, rsi, rdi, r12, r13
    cmp r8d, 16
    jae .dn
    mov r12, rdx
    mov r13d, r8d
    mov rdx, rcx
    lea rcx, [rsp + 1120]
    call u64_strcpy

    lea rcx, [rsp + 1120]
    mov rdx, r12
    call where64_probe_dir

    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rel szWh64Star]
    call u64_path_join
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .dn
    mov rbx, rax
.lp:
    test dword [rsp + 64], 0x10
    jz .nx
    cmp byte [rsp + 88], '.'
    jne .go
    cmp byte [rsp + 89], 0
    je .nx
    cmp byte [rsp + 89], '.'
    jne .go
    cmp byte [rsp + 90], 0
    je .nx
.go:
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 608]
    mov rdx, r12
    lea r8d, [r13d + 1]
    call where64_recurse
.nx:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .lp
    mov rcx, rbx
    call qword [rel __imp_FindClose]
.dn:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=found, 1=not found, 2=error)
;; @err Exits 2 on missing pattern
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 WHERE.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_wh64_quiet], 0
    mov dword [rel g_wh64_quote], 0
    mov dword [rel g_wh64_time], 0
    mov dword [rel g_wh64_rec], 0
    mov dword [rel g_wh64_found], 0
    mov byte [rel g_wh64_pat], 0
    mov byte [rel g_wh64_rdir], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .run
    cmp dl, '/'
    jne .pat
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'Q'
    je .sq
    cmp al, 'F'
    je .sf
    cmp al, 'T'
    je .st
    cmp al, 'R'
    je .sr
    jmp .bad
.sq:
    mov dword [rel g_wh64_quiet], 1
    jmp .arg_lp
.sf:
    mov dword [rel g_wh64_quote], 1
    jmp .arg_lp
.st:
    mov dword [rel g_wh64_time], 1
    jmp .arg_lp
.sr:
    mov dword [rel g_wh64_rec], 1
    mov rcx, rsi
    lea rdx, [rel g_wh64_rdir]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.pat:
    mov rcx, rsi
    lea rdx, [rel g_wh64_pat]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp

.help:
    lea rcx, [rel szWh64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szWh64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 2
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_wh64_pat], 0
    je .bad

    cmp dword [rel g_wh64_rec], 0
    je .chk_env
    lea rcx, [rel g_wh64_rdir]
    lea rdx, [rel g_wh64_pat]
    xor r8d, r8d
    call where64_recurse
    jmp .fin

.chk_env:
    cmp byte [rel g_wh64_pat], '$'
    jne .std
    lea rsi, [rel g_wh64_pat + 1]
    lea rdi, [rel g_u64_arg_a]
    xor ecx, ecx
.fc:
    mov al, byte [rsi + rcx]
    test al, al
    jz .std
    cmp al, ':'
    je .gc
    mov byte [rdi + rcx], al
    inc ecx
    jmp .fc
.gc:
    mov byte [rdi + rcx], 0
    lea rdx, [rsi + rcx + 1]
    lea rcx, [rel g_u64_arg_b]
    call u64_strcpy
    lea rcx, [rel g_wh64_pat]
    lea rdx, [rel g_u64_arg_b]
    call u64_strcpy
    lea rcx, [rel g_u64_arg_a]
    mov rdx, qword [rel g_u64_arena]
    mov r8d, 32768
    call u64_get_env
    jmp .walk

.std:
    lea rcx, [rel g_u64_arg_c]
    mov edx, 1000
    call u64_get_cwd
    lea rcx, [rel g_u64_arg_c]
    lea rdx, [rel g_wh64_pat]
    call where64_probe_dir

    lea rcx, [rel szWh64Path]
    mov rdx, qword [rel g_u64_arena]
    mov r8d, 32768
    call u64_get_env

.walk:
    mov rsi, qword [rel g_u64_arena]
.slp:
    cmp byte [rsi], 0
    je .fin
    lea rdi, [rel g_u64_arg_c]
.cps:
    mov al, byte [rsi]
    test al, al
    jz .gs
    cmp al, ';'
    je .as
    mov byte [rdi], al
    inc rsi
    inc rdi
    jmp .cps
.as:
    inc rsi
.gs:
    mov byte [rdi], 0
    cmp byte [rel g_u64_arg_c], 0
    je .slp
    lea rcx, [rel g_u64_arg_c]
    lea rdx, [rel g_wh64_pat]
    call where64_probe_dir
    jmp .slp

.fin:
    cmp dword [rel g_wh64_found], 0
    jne .ex0
    cmp dword [rel g_wh64_quiet], 0
    jne .ex1
    lea rcx, [rel szWh64Nf]
    call u64_write_err_str
    call u64_write_crlf
.ex1:
    mov ecx, 1
    call u64_exit
    @epilog_ret
.ex0:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szWh64Path, "PATH", 0
@data db szWh64Star, "*", 0
@data db szWh64Com, ".COM", 0
@data db szWh64Exe, ".EXE", 0
@data db szWh64Bat, ".BAT", 0
@data db szWh64Cmd, ".CMD", 0
@data db szWh64Pad, "   ", 0
@data db szWh64Nf, "INFO: Could not find files for the given pattern(s).", 0
@data db szWh64Syn, "ERROR: Invalid syntax.", 0
@data db szWh64Help, "Displays the location of files that match the search pattern (/R /Q /F /T).", 0

@include "u_data64.inc"

@res dword g_wh64_quiet, 1
@res dword g_wh64_quote, 1
@res dword g_wh64_time, 1
@res dword g_wh64_rec, 1
@res dword g_wh64_found, 1
@res byte g_wh64_pat, 520
@res byte g_wh64_rdir, 520
