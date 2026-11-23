;; @file more.asm
;; @isa x64
;; @desc Standalone x86-64 MORE.EXE interactive pager (/E, /C, /P, /S, /Tn, +n [files...], CONIN$ key polling).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc more64_wait_key
;; @abi win64_ms
;; @in none
;; @out EAX = 1 if user pressed 'Q' to quit, 0 to continue
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prompts "-- More --" and reads a keystroke from CONIN$ even when stdin is redirected.
@proc_frame more64_wait_key, 64, rbx
    lea rcx, [rel szMore64Prompt]
    call u64_write_str

    lea rcx, [rel u16U64ConIn]
    mov edx, 0x80000000
    mov r8d, 3
    xor r9d, r9d
    mov dword [rsp + 32], 3
    mov dword [rsp + 40], 0x80
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    cmp rax, -1
    je .cont_nl
    mov rbx, rax
    mov byte [rel g_more64_key], 0
    mov rcx, rbx
    lea rdx, [rel g_more64_key]
    mov r8d, 1
    lea r9, [rel g_u64_io_done]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    call u64_write_crlf
    mov al, byte [rel g_more64_key]
    call u64_chr_upper
    cmp al, 'Q'
    je .quit
    xor eax, eax
    @epilog_ret
.cont_nl:
    call u64_write_crlf
    xor eax, eax
    @epilog_ret
.quit:
    mov eax, 1
    @epilog_ret
@endproc

;; @proc more64_emit_line
;; @abi win64_ms
;; @in RCX = line bytes, RDX = line len
;; @out EAX = 1 if user quit, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Expands tabs (/Tn) and form-feeds (/P) and triggers CONIN$ page break every 23 console rows.
@proc_frame more64_emit_line, 40, rbx, rsi, rdi, r12
    mov rsi, rcx
    mov r12, rdx
    xor ebx, ebx
.ch_lp:
    cmp rbx, r12
    jae .eol
    mov al, byte [rsi + rbx]
    cmp al, 9
    je .tab
    cmp al, 12
    jne .norm
    cmp dword [rel g_more64_ff], 0
    je .norm
    call u64_write_crlf
    mov dword [rel g_more64_rows], 23
    inc rbx
    jmp .ch_lp
.tab:
    mov edi, dword [rel g_more64_tab]
.tlp:
    test edi, edi
    jz .nx
    mov cl, ' '
    call u64_write_chr
    dec edi
    jmp .tlp
.norm:
    mov cl, al
    call u64_write_chr
.nx:
    inc rbx
    jmp .ch_lp

.eol:
    call u64_write_crlf
    inc dword [rel g_more64_rows]
    cmp dword [rel g_u64_out_is_con], 0
    je .ok
    cmp dword [rel g_more64_rows], 23
    jb .ok
    mov dword [rel g_more64_rows], 0
    call more64_wait_key
    test eax, eax
    jnz .quit
.ok:
    xor eax, eax
    @epilog_ret
.quit:
    mov eax, 1
    @epilog_ret
@endproc

;; @proc more64_stream_one
;; @abi win64_ms
;; @in RCX = file path (empty=stdin)
;; @out EAX = 1 on error, 0 on ok
;; @err EAX = 1
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Streams file or stdin with +n line skip, /S squeeze, and /Tn tab expansion.
@proc_frame more64_stream_one, 48, rbx, rsi, rdi, r12, r13
    mov r12, rcx
    cmp dword [rel g_more64_clr], 0
    je .ld
    cmp dword [rel g_u64_out_is_con], 0
    je .ld
    lea rcx, [rel szMore64Cls]
    call u64_write_str
.ld:
    mov rcx, r12
    mov rdx, qword [rel g_u64_arena]
    mov r8d, 260000
    mov r9d, 1
    call u64_load_stream
    jc .err

    mov rsi, qword [rel g_u64_arena]
    lea r13, [rsi + rax]
    xor ebx, ebx ; prev_blank

.ln_lp:
    cmp rsi, r13
    jae .ok
    cmp byte [rsi], 0x1A
    je .ok
    mov rdi, rsi
.eol:
    cmp rdi, r13
    jae .got
    mov al, byte [rdi]
    cmp al, 13
    je .got
    cmp al, 10
    je .got
    inc rdi
    jmp .eol
.got:
    cmp dword [rel g_more64_skip], 0
    je .sq
    dec dword [rel g_more64_skip]
    jmp .adv
.sq:
    mov rdx, rdi
    sub rdx, rsi
    cmp dword [rel g_more64_sqz], 0
    je .emit
    test rdx, rdx
    jnz .non_b
    test ebx, ebx
    jnz .adv
    mov ebx, 1
    jmp .emit
.non_b:
    xor ebx, ebx
.emit:
    mov rcx, rsi
    call more64_emit_line
    test eax, eax
    jnz .ok
.adv:
    cmp rdi, r13
    jae .ok
    cmp byte [rdi], 13
    jne .clf
    inc rdi
.clf:
    cmp rdi, r13
    jae .nx
    cmp byte [rdi], 10
    jne .nx
    inc rdi
.nx:
    mov rsi, rdi
    jmp .ln_lp
.ok:
    xor eax, eax
    @epilog_ret
.err:
    lea rcx, [rel szMore64Nf]
    call u64_write_err_str
    call u64_write_crlf
    mov eax, 1
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on missing file
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 MORE.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_more64_clr], 0
    mov dword [rel g_more64_ff], 0
    mov dword [rel g_more64_sqz], 0
    mov dword [rel g_more64_tab], 8
    mov dword [rel g_more64_skip], 0
    mov dword [rel g_more64_rows], 0
    mov dword [rel g_more64_files], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .fin
    cmp dl, '+'
    je .plus
    cmp dl, '/'
    jne .f_arg
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'C'
    je .sc
    cmp al, 'P'
    je .sp
    cmp al, 'S'
    je .ss
    cmp al, 'T'
    je .st
    jmp .arg_lp
.sc:
    mov dword [rel g_more64_clr], 1
    jmp .arg_lp
.sp:
    mov dword [rel g_more64_ff], 1
    jmp .arg_lp
.ss:
    mov dword [rel g_more64_sqz], 1
    jmp .arg_lp
.st:
    lea rcx, [rel g_u64_arg_a + 2]
    call u64_parse_u32
    test eax, eax
    jz .arg_lp
    cmp eax, 16
    ja .arg_lp
    mov dword [rel g_more64_tab], eax
    jmp .arg_lp
.plus:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    lea rcx, [rel g_u64_arg_a + 1]
    call u64_parse_u32
    mov dword [rel g_more64_skip], eax
    jmp .arg_lp
.f_arg:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    inc dword [rel g_more64_files]
    lea rcx, [rel g_u64_arg_b]
    call more64_stream_one
    test eax, eax
    jnz .ex1
    jmp .arg_lp

.fin:
    cmp dword [rel g_more64_files], 0
    jne .ex0
    mov byte [rel g_u64_arg_b], 0
    lea rcx, [rel g_u64_arg_b]
    call more64_stream_one
    test eax, eax
    jnz .ex1
.ex0:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
.ex1:
    mov ecx, 1
    call u64_exit
    @epilog_ret
.help:
    lea rcx, [rel szMore64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szMore64Prompt, "-- More -- ", 0
@data db szMore64Cls, 27, "[2J", 27, "[H", 0
@data db szMore64Nf, "Cannot access file.", 0
@data db szMore64Help, "Displays output one screen at a time (/E /C /P /S /Tn +n [files]).", 0

@include "u_data64.inc"

@res dword g_more64_clr, 1
@res dword g_more64_ff, 1
@res dword g_more64_sqz, 1
@res dword g_more64_tab, 1
@res dword g_more64_skip, 1
@res dword g_more64_rows, 1
@res dword g_more64_files, 1
@res byte g_more64_key, 8
