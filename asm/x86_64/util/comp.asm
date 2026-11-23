;; @file comp.asm
;; @isa x64
;; @desc Standalone x86-64 COMP.EXE file comparison utility (/D, /A, /L, /N=num, /C, /M file1 file2).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc comp64_fmt_byte
;; @abi win64_ms
;; @in ECX = byte value (0..255)
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints byte value as ASCII (/A), decimal (/D), or hex (default).
@proc_frame comp64_fmt_byte, 40
    cmp dword [rel g_cmp64_asc], 0
    jne .asc
    cmp dword [rel g_cmp64_dec], 0
    jne .dec
    call u64_print_hex8
    @epilog_ret
.dec:
    movzx ecx, cl
    call u64_print_u64
    @epilog_ret
.asc:
    call u64_write_chr
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=mismatch, 2=error)
;; @err Exits 2 on missing file
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 COMP.EXE.
@proc_frame mainCRTStartup, 64, rbx, rsi, rdi, r12, r13, r14, r15
    call u64_init
    mov rsi, rax
    mov dword [rel g_cmp64_dec], 0
    mov dword [rel g_cmp64_asc], 0
    mov dword [rel g_cmp64_ln], 0
    mov dword [rel g_cmp64_ci], 0
    mov dword [rel g_cmp64_max_ln], 0
    mov dword [rel g_cmp64_no_p], 0
    mov byte [rel g_u64_arg_b], 0
    mov byte [rel g_u64_arg_c], 0

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
    cmp al, 'D'
    je .sd
    cmp al, 'A'
    je .sa
    cmp al, 'L'
    je .sl
    cmp al, 'C'
    je .sc
    cmp al, 'M'
    je .sm
    cmp al, 'N'
    je .sn
    jmp .arg_lp
.sd:
    mov dword [rel g_cmp64_dec], 1
    jmp .arg_lp
.sa:
    mov dword [rel g_cmp64_asc], 1
    jmp .arg_lp
.sl:
    mov dword [rel g_cmp64_ln], 1
    jmp .arg_lp
.sc:
    mov dword [rel g_cmp64_ci], 1
    jmp .arg_lp
.sm:
    mov dword [rel g_cmp64_no_p], 1
    jmp .arg_lp
.sn:
    cmp byte [rel g_u64_arg_a + 2], '='
    jne .arg_lp
    lea rcx, [rel g_u64_arg_a + 3]
    call u64_parse_u32
    mov dword [rel g_cmp64_max_ln], eax
    jmp .arg_lp
.pos:
    cmp byte [rel g_u64_arg_b], 0
    jne .pos2
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.pos2:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_c]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp

.help:
    lea rcx, [rel szCmp64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_u64_arg_b], 0
    je .err
    cmp byte [rel g_u64_arg_c], 0
    je .err

    lea rcx, [rel g_u64_arg_b]
    mov rdx, qword [rel g_u64_arena]
    mov r8d, 220000
    xor r9d, r9d
    call u64_load_stream
    jc .err
    mov r12, rax

    lea rcx, [rel g_u64_arg_c]
    mov rdx, qword [rel g_u64_arena]
    add rdx, 262144
    mov r8d, 220000
    xor r9d, r9d
    call u64_load_stream
    jc .err
    mov r13, rax

    lea rcx, [rel szCmp64H1]
    call u64_write_str
    lea rcx, [rel g_u64_arg_b]
    call u64_write_str
    lea rcx, [rel szCmp64H2]
    call u64_write_str
    lea rcx, [rel g_u64_arg_c]
    call u64_write_str
    lea rcx, [rel szCmp64Dots]
    call u64_write_str
    call u64_write_crlf

    xor r15d, r15d ; errs
    cmp dword [rel g_cmp64_max_ln], 0
    jne .st
    cmp r12, r13
    je .st
    lea rcx, [rel szCmp64Diff]
    call u64_write_str
    call u64_write_crlf
    mov r15d, 1
    jmp .prompt

.st:
    mov r14, r12
    cmp r14, r13
    jbe .gmin
    mov r14, r13
.gmin:
    mov rsi, qword [rel g_u64_arena]
    lea rdi, [rsi + 262144]
    xor ebx, ebx
    mov dword [rel g_cmp64_cur_ln], 1

.lp:
    cmp rbx, r14
    jae .fin
    mov al, byte [rsi + rbx]
    cmp al, 10
    jne .nonl
    inc dword [rel g_cmp64_cur_ln]
    mov ecx, dword [rel g_cmp64_max_ln]
    test ecx, ecx
    jz .nonl_ld
    mov edx, dword [rel g_cmp64_cur_ln]
    dec edx
    cmp edx, ecx
    jae .fin
.nonl_ld:
    mov al, byte [rsi + rbx]
.nonl:
    mov dl, byte [rdi + rbx]
    cmp dword [rel g_cmp64_ci], 0
    je .raw
    call u64_chr_upper
    mov cl, al
    mov al, dl
    call u64_chr_upper
    cmp cl, al
    je .nx
    jmp .mm
.raw:
    cmp al, dl
    je .nx

.mm:
    inc r15d
    cmp r15d, 10
    ja .fin
    cmp dword [rel g_cmp64_ln], 0
    jne .pr_ln
    lea rcx, [rel szCmp64EOff]
    call u64_write_str
    mov ecx, ebx
    call u64_print_hex32
    jmp .vals
.pr_ln:
    lea rcx, [rel szCmp64ELn]
    call u64_write_str
    mov ecx, dword [rel g_cmp64_cur_ln]
    call u64_print_u64
.vals:
    call u64_write_crlf
    lea rcx, [rel szCmp64F1]
    call u64_write_str
    movzx ecx, byte [rsi + rbx]
    call comp64_fmt_byte
    call u64_write_crlf
    lea rcx, [rel szCmp64F2]
    call u64_write_str
    movzx ecx, byte [rdi + rbx]
    call comp64_fmt_byte
    call u64_write_crlf
.nx:
    inc rbx
    jmp .lp

.fin:
    test r15d, r15d
    jnz .prompt
    lea rcx, [rel szCmp64Ok]
    call u64_write_str
    call u64_write_crlf

.prompt:
    cmp dword [rel g_cmp64_no_p], 0
    jne .ex
    cmp dword [rel g_u64_in_is_con], 0
    je .ex
    lea rcx, [rel szCmp64More]
    call u64_write_str
    call u64_write_crlf
.ex:
    test r15d, r15d
    jnz .ex1
    xor ecx, ecx
    call u64_exit
    @epilog_ret
.ex1:
    mov ecx, 1
    call u64_exit
    @epilog_ret
.err:
    lea rcx, [rel szCmp64Open]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 2
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szCmp64H1, "Comparing ", 0
@data db szCmp64H2, " and ", 0
@data db szCmp64Dots, "...", 0
@data db szCmp64Ok, "Files compare OK", 0
@data db szCmp64Diff, "Files are different sizes", 0
@data db szCmp64EOff, "Compare error at OFFSET ", 0
@data db szCmp64ELn, "Compare error at LINE ", 0
@data db szCmp64F1, "file1 = ", 0
@data db szCmp64F2, "file2 = ", 0
@data db szCmp64More, "Compare more files (Y/N) ? N", 0
@data db szCmp64Open, "Can't find/open file", 0
@data db szCmp64Help, "Compares the contents of two files or sets of files (/D /A /L /N=num /C /M).", 0

@include "u_data64.inc"

@res dword g_cmp64_dec, 1
@res dword g_cmp64_asc, 1
@res dword g_cmp64_ln, 1
@res dword g_cmp64_ci, 1
@res dword g_cmp64_max_ln, 1
@res dword g_cmp64_no_p, 1
@res dword g_cmp64_cur_ln, 1
