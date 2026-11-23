;; @file sort.asm
;; @isa x64
;; @desc Standalone x86-64 SORT.EXE utility (/R, /+n, /M, /L, /REC, /T, /O out, /UNIQUE) with O(N log N) merge sort.

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc sort64_cmp_rec
;; @abi win64_ms
;; @in RCX = ptr to 16B record A (0:ptr64, 8:len64), RDX = ptr to 16B record B
;; @out EAX = -1 if A < B, 0 if A == B, 1 if A > B
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Case-insensitive collate comparison starting at g_sort64_col.
@proc sort64_cmp_rec
    mov r8, qword [rcx]
    mov r9, qword [rcx + 8]
    mov r10, qword [rdx]
    mov r11, qword [rdx + 8]

    mov eax, dword [rel g_sort64_col]
    cmp r9, rax
    ja .adj_a
    xor r9d, r9d
    jmp .chk_b
.adj_a:
    add r8, rax
    sub r9, rax
.chk_b:
    cmp r11, rax
    ja .adj_b
    xor r11d, r11d
    jmp .lp
.adj_b:
    add r10, rax
    sub r11, rax

.lp:
    test r9, r9
    jz .a_end
    test r11, r11
    jz .gt
    mov al, byte [r8]
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    mov dl, byte [r10]
    cmp dl, 'a'
    jb .u2
    cmp dl, 'z'
    ja .u2
    sub dl, 32
.u2:
    cmp al, dl
    jb .lt
    ja .gt
    inc r8
    inc r10
    dec r9
    dec r11
    jmp .lp

.a_end:
    test r11, r11
    jz .eq
.lt:
    mov eax, -1
    jmp .rev
.gt:
    mov eax, 1
.rev:
    cmp dword [rel g_sort64_rev], 0
    je .dn
    neg eax
    jmp .dn
.eq:
    xor eax, eax
.dn:
    ret
@endproc

;; @proc sort64_merge_run
;; @abi win64_ms
;; @in RCX = src array of 16B records, RDX = dst array, R8 = left, R9 = mid, [rsp+40] inside caller = right (passed via g_sort64_right)
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Merges sorted runs [left..mid) and [mid..right) from RCX into RDX.
@proc_frame sort64_merge_run, 48, rbx, rsi, rdi, r12, r13, r14, r15
    mov rsi, rcx ; src
    mov rdi, rdx ; dst
    mov r12, r8 ; i = left
    mov r13, r9 ; j = mid
    mov r14, r9 ; mid
    mov r15, r8 ; k = left
    mov rbx, qword [rel g_sort64_right]

.m_lp:
    cmp r12, r14
    jae .r_rem
    cmp r13, rbx
    jae .l_rem

    mov rax, r12
    shl rax, 4
    lea rcx, [rsi + rax]
    mov rax, r13
    shl rax, 4
    lea rdx, [rsi + rax]
    call sort64_cmp_rec
    cmp eax, 0
    jg .take_r

    mov rax, r12
    shl rax, 4
    mov rcx, r15
    shl rcx, 4
    mov r8, qword [rsi + rax]
    mov r9, qword [rsi + rax + 8]
    mov qword [rdi + rcx], r8
    mov qword [rdi + rcx + 8], r9
    inc r12
    inc r15
    jmp .m_lp

.take_r:
    mov rax, r13
    shl rax, 4
    mov rcx, r15
    shl rcx, 4
    mov r8, qword [rsi + rax]
    mov r9, qword [rsi + rax + 8]
    mov qword [rdi + rcx], r8
    mov qword [rdi + rcx + 8], r9
    inc r13
    inc r15
    jmp .m_lp

.l_rem:
    cmp r12, r14
    jae .dn
    mov rax, r12
    shl rax, 4
    mov rcx, r15
    shl rcx, 4
    mov r8, qword [rsi + rax]
    mov r9, qword [rsi + rax + 8]
    mov qword [rdi + rcx], r8
    mov qword [rdi + rcx + 8], r9
    inc r12
    inc r15
    jmp .l_rem

.r_rem:
    cmp r13, rbx
    jae .dn
    mov rax, r13
    shl rax, 4
    mov rcx, r15
    shl rcx, 4
    mov r8, qword [rsi + rax]
    mov r9, qword [rsi + rax + 8]
    mov qword [rdi + rcx], r8
    mov qword [rdi + rcx + 8], r9
    inc r13
    inc r15
    jmp .r_rem

.dn:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on missing input file
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses SORT switches, runs O(N log N) bottom-up merge sort, and writes sorted lines.
@proc_frame mainCRTStartup, 64, rbx, rsi, rdi, r12, r13, r14, r15
    call u64_init
    mov rsi, rax
    mov dword [rel g_sort64_rev], 0
    mov dword [rel g_sort64_col], 0
    mov dword [rel g_sort64_uniq], 0
    mov byte [rel g_u64_arg_b], 0
    mov byte [rel g_u64_arg_c], 0

.sw_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .load_in
    cmp dl, '/'
    jne .in_f
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    cmp al, '+'
    je .sw_col
    call u64_chr_upper
    cmp al, 'R'
    je .sw_r
    cmp al, 'U'
    je .sw_u
    cmp al, 'O'
    je .sw_o
    cmp al, 'M'
    je .sk_v
    cmp al, 'L'
    je .sk_v
    cmp al, 'T'
    je .sk_v
    jmp .sw_lp
.sw_r:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'E'
    je .sk_v
    mov dword [rel g_sort64_rev], 1
    jmp .sw_lp
.sw_u:
    mov dword [rel g_sort64_uniq], 1
    jmp .sw_lp
.sw_col:
    lea rcx, [rel g_u64_arg_a + 2]
    call u64_parse_u32
    test eax, eax
    jz .sw_lp
    dec eax
    mov dword [rel g_sort64_col], eax
    jmp .sw_lp
.sw_o:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_c]
    call u64_read_tok
    mov rsi, rax
    jmp .sw_lp
.sk_v:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    jmp .sw_lp
.in_f:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    jmp .sw_lp

.help:
    lea rcx, [rel szSort64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.load_in:
    ; Arena [0..262143] = text, [262144..393215] = 8192 x 16B recs A, [393216..524287] = 8192 x 16B recs B
    lea rcx, [rel g_u64_arg_b]
    mov rdx, qword [rel g_u64_arena]
    mov r8d, 260000
    mov r9d, 1
    call u64_load_stream
    jnc .split
    lea rcx, [rel szSort64Nf]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret

.split:
    mov rsi, qword [rel g_u64_arena]
    lea r12, [rsi + rax] ; end
    mov rdi, rsi
    add rdi, 262144
    xor ebx, ebx ; count

.sp_lp:
    cmp rsi, r12
    jae .do_sort
    cmp byte [rsi], 0x1A
    je .do_sort
    cmp ebx, 8190
    jae .do_sort
    mov rcx, rsi
.eol:
    cmp rcx, r12
    jae .got_l
    mov al, byte [rcx]
    cmp al, 13
    je .got_l
    cmp al, 10
    je .got_l
    inc rcx
    jmp .eol
.got_l:
    mov rax, rbx
    shl rax, 4
    mov qword [rdi + rax], rsi
    mov rdx, rcx
    sub rdx, rsi
    mov qword [rdi + rax + 8], rdx
    inc rbx
    cmp rcx, r12
    jae .do_sort
    cmp byte [rcx], 13
    jne .clf
    inc rcx
.clf:
    cmp rcx, r12
    jae .nx_s
    cmp byte [rcx], 10
    jne .nx_s
    inc rcx
.nx_s:
    mov rsi, rcx
    jmp .sp_lp

.do_sort:
    mov r12, rbx ; total count
    mov r13, qword [rel g_u64_arena]
    add r13, 262144 ; src
    mov r14, qword [rel g_u64_arena]
    add r14, 393216 ; dst
    cmp r12, 2
    jb .emit

    mov r15d, 1 ; width
.w_lp:
    cmp r15, r12
    jae .emit
    xor ebx, ebx ; left
.r_lp:
    cmp rbx, r12
    jae .swp
    lea r9, [rbx + r15]
    cmp r9, r12
    jbe .gm
    mov r9, r12
.gm:
    lea rax, [r9 + r15]
    cmp rax, r12
    jbe .gr
    mov rax, r12
.gr:
    mov qword [rel g_sort64_right], rax
    mov rcx, r13
    mov rdx, r14
    mov r8, rbx
    call sort64_merge_run
    lea rbx, [rbx + r15 * 2]
    jmp .r_lp
.swp:
    mov rax, r13
    mov r13, r14
    mov r14, rax
    shl r15, 1
    jmp .w_lp

.emit:
    mov qword [rel g_sort64_outh], 0
    cmp byte [rel g_u64_arg_c], 0
    je .wr_lp_init
    lea rcx, [rel g_u64_arg_c]
    lea rdx, [rel g_u64_wbuf1]
    call u64_u8_to_w
    lea rcx, [rel g_u64_wbuf1]
    mov edx, 0x40000000
    xor r8d, r8d
    xor r9d, r9d
    mov dword [rsp + 32], 2
    mov dword [rsp + 40], 0x80
    mov qword [rsp + 48], 0
    call qword [rel __imp_CreateFileW]
    cmp rax, -1
    je .wr_lp_init
    mov qword [rel g_sort64_outh], rax

.wr_lp_init:
    xor ebx, ebx
.wr_lp:
    cmp rbx, r12
    jae .close_o
    cmp dword [rel g_sort64_uniq], 0
    je .do_w
    test rbx, rbx
    jz .do_w
    mov rax, rbx
    shl rax, 4
    lea rcx, [r13 + rax - 16]
    lea rdx, [r13 + rax]
    call sort64_cmp_rec
    test eax, eax
    jz .nx_w
.do_w:
    mov rax, rbx
    shl rax, 4
    mov rcx, qword [r13 + rax]
    mov rdx, qword [r13 + rax + 8]
    cmp qword [rel g_sort64_outh], 0
    jne .wr_f
    call u64_write_out
    call u64_write_crlf
    jmp .nx_w
.wr_f:
    mov r8, rdx
    mov rdx, rcx
    mov rcx, qword [rel g_sort64_outh]
    xor r9d, r9d
    call u64_write_handle
    mov rcx, qword [rel g_sort64_outh]
    lea rdx, [rel szU64Crlf]
    mov r8d, 2
    xor r9d, r9d
    call u64_write_handle
.nx_w:
    inc rbx
    jmp .wr_lp

.close_o:
    mov rcx, qword [rel g_sort64_outh]
    test rcx, rcx
    jz .ex0
    call qword [rel __imp_CloseHandle]
.ex0:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szSort64Nf, "The system cannot find the file specified.", 0
@data db szSort64Help, "Sorts input and writes results to screen or file (/R /+n /UNIQUE /O out).", 0

@include "u_data64.inc"

@res qword g_sort64_right, 1
@res qword g_sort64_outh, 1
@res dword g_sort64_rev, 1
@res dword g_sort64_col, 1
@res dword g_sort64_uniq, 1
