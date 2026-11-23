;; @file find.asm
;; @isa x64
;; @desc Standalone x86-64 FIND.EXE utility (/V, /C, /N, /I, /OFF[LINE] "string" [files...], UTF-8/UTF-16 BOM aware).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc find64_line_has
;; @abi win64_ms
;; @in RCX = line start, RDX = line len, R8 = needle, R9 = needle len
;; @out EAX = 1 if needle occurs in line, 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Substring search honoring g_find64_ci (/I).
@proc find64_line_has
    test r9, r9
    jz .no
    cmp rdx, r9
    jb .no
    sub rdx, r9
    inc rdx
.pos_lp:
    xor r10d, r10d
.ch_lp:
    cmp r10, r9
    jae .yes
    mov al, byte [rcx + r10]
    mov r11b, byte [r8 + r10]
    cmp dword [rel g_find64_ci], 0
    je .raw
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    cmp r11b, 'a'
    jb .u2
    cmp r11b, 'z'
    ja .u2
    sub r11b, 32
.u2:
.raw:
    cmp al, r11b
    jne .nx_pos
    inc r10
    jmp .ch_lp
.nx_pos:
    inc rcx
    dec rdx
    jnz .pos_lp
.no:
    xor eax, eax
    ret
.yes:
    mov eax, 1
    ret
@endproc

;; @proc find64_process_one
;; @abi win64_ms
;; @in RCX = file path UTF-8 (empty=stdin), EDX = show_banner (1 or 0)
;; @out EAX = 1 if matched, 0 if zero matches, 2 on open failure
;; @err EAX = 2
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Loads stream and scans lines for g_find64_needle.
@proc_frame find64_process_one, 64, rbx, rsi, rdi, r12, r13, r14, r15
    mov r12, rcx ; path
    mov r13d, edx ; show_banner
    xor r14d, r14d ; match_cnt
    xor r15d, r15d ; line_num

    mov rdx, qword [rel g_u64_arena]
    mov r8d, 260000
    mov r9d, 1
    call u64_load_stream
    jnc .loaded
    lea rcx, [rel szFind64Nf]
    call u64_write_err_str
    mov rcx, r12
    call u64_write_err_str
    lea rcx, [rel szU64Crlf]
    call u64_write_err_str
    mov eax, 2
    @epilog_ret

.loaded:
    mov rbx, rax
    test r13d, r13d
    jz .scan_init
    cmp dword [rel g_find64_cnt_only], 0
    jne .scan_init
    lea rcx, [rel szFind64Hdr]
    call u64_write_str
    mov rcx, r12
    call u64_write_str
    call u64_write_crlf

.scan_init:
    mov rsi, qword [rel g_u64_arena]
    add rbx, rsi ; end ptr

.ln_lp:
    cmp rsi, rbx
    jae .after
    cmp byte [rsi], 0x1A
    je .after
    inc r15
    mov rdi, rsi
.eol:
    cmp rdi, rbx
    jae .got_l
    mov al, byte [rdi]
    cmp al, 13
    je .got_l
    cmp al, 10
    je .got_l
    inc rdi
    jmp .eol

.got_l:
    mov rdx, rdi
    sub rdx, rsi
    mov qword [rsp + 48], rdx ; save line len
    mov rcx, rsi
    lea r8, [rel g_find64_needle]
    mov r9d, dword [rel g_find64_nlen]
    call find64_line_has
    xor eax, dword [rel g_find64_inv]
    test eax, eax
    jz .adv

    inc r14
    cmp dword [rel g_find64_cnt_only], 0
    jne .adv

    cmp dword [rel g_find64_nums], 0
    je .emit_l
    mov cl, '['
    call u64_write_chr
    mov rcx, r15
    call u64_print_u64
    mov cl, ']'
    call u64_write_chr
.emit_l:
    mov rcx, rsi
    mov rdx, qword [rsp + 48]
    call u64_write_out
    call u64_write_crlf

.adv:
    cmp rdi, rbx
    jae .after
    cmp byte [rdi], 13
    jne .c_lf
    inc rdi
.c_lf:
    cmp rdi, rbx
    jae .nx
    cmp byte [rdi], 10
    jne .nx
    inc rdi
.nx:
    mov rsi, rdi
    jmp .ln_lp

.after:
    cmp dword [rel g_find64_cnt_only], 0
    je .ret_r
    test r13d, r13d
    jz .bare_c
    lea rcx, [rel szFind64Hdr]
    call u64_write_str
    mov rcx, r12
    call u64_write_str
    lea rcx, [rel szFind64Col]
    call u64_write_str
.bare_c:
    mov rcx, r14
    call u64_print_u64
    call u64_write_crlf

.ret_r:
    xor eax, eax
    test r14, r14
    jz .dn
    mov eax, 1
.dn:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=matched, 1=none, 2=error)
;; @err Exits 2 on error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses FIND switches, quoted needle, and file arguments.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_find64_inv], 0
    mov dword [rel g_find64_cnt_only], 0
    mov dword [rel g_find64_nums], 0
    mov dword [rel g_find64_ci], 0
    mov dword [rel g_find64_has_n], 0
    mov dword [rel g_find64_files], 0
    mov dword [rel g_find64_hit], 0
    mov dword [rel g_find64_err], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .finish
    cmp dl, '"'
    je .needle_or_f
    cmp dl, '/'
    jne .needle_or_f
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'V'
    je .sw_v
    cmp al, 'C'
    je .sw_c
    cmp al, 'N'
    je .sw_n
    cmp al, 'I'
    je .sw_i
    cmp al, 'O'
    je .arg_lp
    jmp .bad
.sw_v:
    mov dword [rel g_find64_inv], 1
    jmp .arg_lp
.sw_c:
    mov dword [rel g_find64_cnt_only], 1
    jmp .arg_lp
.sw_n:
    mov dword [rel g_find64_nums], 1
    jmp .arg_lp
.sw_i:
    mov dword [rel g_find64_ci], 1
    jmp .arg_lp

.needle_or_f:
    cmp dword [rel g_find64_has_n], 0
    jne .file_arg
    cmp dl, '"'
    jne .bad
    mov rcx, rsi
    lea rdx, [rel g_find64_needle]
    call u64_read_tok
    mov rsi, rax
    mov dword [rel g_find64_nlen], ecx
    mov dword [rel g_find64_has_n], 1
    jmp .arg_lp

.file_arg:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    inc dword [rel g_find64_files]
    lea rcx, [rel g_u64_arg_b]
    mov edx, 1
    call find64_process_one
    cmp eax, 1
    je .hit_f
    cmp eax, 2
    jne .arg_lp
    mov dword [rel g_find64_err], 1
    jmp .arg_lp
.hit_f:
    mov dword [rel g_find64_hit], 1
    jmp .arg_lp

.help:
    lea rcx, [rel szFind64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szFind64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 2
    call u64_exit
    @epilog_ret

.finish:
    cmp dword [rel g_find64_has_n], 0
    je .bad
    cmp dword [rel g_find64_files], 0
    jne .ex_code
    mov byte [rel g_u64_arg_b], 0
    lea rcx, [rel g_u64_arg_b]
    xor edx, edx
    call find64_process_one
    cmp eax, 1
    jne .ex_code
    mov dword [rel g_find64_hit], 1

.ex_code:
    cmp dword [rel g_find64_hit], 0
    jne .ex0
    cmp dword [rel g_find64_err], 0
    jne .ex2
    mov ecx, 1
    call u64_exit
    @epilog_ret
.ex2:
    mov ecx, 2
    call u64_exit
    @epilog_ret
.ex0:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szFind64Hdr, "---------- ", 0
@data db szFind64Col, ": ", 0
@data db szFind64Nf, "File not found - ", 0
@data db szFind64Syn, "FIND: Parameter format not correct", 0
@data db szFind64Help, "Searches for a text string in a file or files (/V /C /N /I /OFF).", 0

@include "u_data64.inc"

@res dword g_find64_inv, 1
@res dword g_find64_cnt_only, 1
@res dword g_find64_nums, 1
@res dword g_find64_ci, 1
@res dword g_find64_has_n, 1
@res dword g_find64_nlen, 1
@res dword g_find64_files, 1
@res dword g_find64_hit, 1
@res dword g_find64_err, 1
@res byte g_find64_needle, 1024
