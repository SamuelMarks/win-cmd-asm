;; @file fc.asm
;; @isa x64
;; @desc Standalone x86-64 FC.EXE file comparison utility (/A /B /C /L /LBn /N /OFF /T /U /W file1 file2).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc fc64_lines_eq
;; @abi win64_ms
;; @in RCX = line A, RDX = len A, R8 = line B, R9 = len B
;; @out EAX = 1 if equal (honoring /C and /W), 0 otherwise
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Compares two text lines honoring /C case-folding and /W whitespace compression.
@proc fc64_lines_eq
    lea r10, [rcx + rdx] ; end A
    lea r11, [r8 + r9] ; end B

    cmp dword [rel g_fc64_ws], 0
    je .lp
.tla:
    cmp rcx, r10
    jae .tlb
    mov al, byte [rcx]
    cmp al, 0x20
    je .ala
    cmp al, 0x09
    jne .tlb
.ala:
    inc rcx
    jmp .tla
.tlb:
    cmp r8, r11
    jae .lp
    mov al, byte [r8]
    cmp al, 0x20
    je .alb
    cmp al, 0x09
    jne .lp
.alb:
    inc r8
    jmp .tlb

.lp:
    cmp rcx, r10
    jae .a_dn
    cmp r8, r11
    jae .b_dn

    cmp dword [rel g_fc64_ws], 0
    je .cc
    mov al, byte [rcx]
    cmp al, 0x20
    je .wsa
    cmp al, 0x09
    jne .cc
.wsa:
    mov dl, byte [r8]
    cmp dl, 0x20
    je .skw
    cmp dl, 0x09
    jne .no
.skw:
    inc rcx
    cmp rcx, r10
    jae .skwb
    mov al, byte [rcx]
    cmp al, 0x20
    je .skw
    cmp al, 0x09
    je .skw
.skwb:
    inc r8
    cmp r8, r11
    jae .lp
    mov dl, byte [r8]
    cmp dl, 0x20
    je .skwb
    cmp dl, 0x09
    je .skwb
    jmp .lp

.cc:
    mov al, byte [rcx]
    mov dl, byte [r8]
    cmp dword [rel g_fc64_ci], 0
    je .raw
    cmp al, 'a'
    jb .u1
    cmp al, 'z'
    ja .u1
    sub al, 32
.u1:
    cmp dl, 'a'
    jb .u2
    cmp dl, 'z'
    ja .u2
    sub dl, 32
.u2:
.raw:
    cmp al, dl
    jne .no
    inc rcx
    inc r8
    jmp .lp

.a_dn:
    cmp dword [rel g_fc64_ws], 0
    je .cbe
.trb:
    cmp r8, r11
    jae .yes
    mov dl, byte [r8]
    cmp dl, 0x20
    je .atb
    cmp dl, 0x09
    jne .no
.atb:
    inc r8
    jmp .trb
.cbe:
    cmp r8, r11
    je .yes
    jmp .no

.b_dn:
    cmp dword [rel g_fc64_ws], 0
    je .no
.tra:
    cmp rcx, r10
    jae .yes
    mov al, byte [rcx]
    cmp al, 0x20
    je .ata
    cmp al, 0x09
    jne .no
.ata:
    inc rcx
    jmp .tra

.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (0=identical, 1=different, 2=missing/syntax)
;; @err Exits 2 on error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 FC.EXE.
@proc_frame mainCRTStartup, 64, rbx, rsi, rdi, r12, r13, r14, r15
    call u64_init
    mov rsi, rax
    mov dword [rel g_fc64_bin], 0
    mov dword [rel g_fc64_ci], 0
    mov dword [rel g_fc64_nums], 0
    mov dword [rel g_fc64_ws], 0
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
    cmp al, 'B'
    je .sb
    cmp al, 'C'
    je .sc
    cmp al, 'N'
    je .sn
    cmp al, 'W'
    je .sw
    jmp .arg_lp
.sb:
    mov dword [rel g_fc64_bin], 1
    jmp .arg_lp
.sc:
    mov dword [rel g_fc64_ci], 1
    jmp .arg_lp
.sn:
    mov dword [rel g_fc64_nums], 1
    jmp .arg_lp
.sw:
    mov dword [rel g_fc64_ws], 1
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
    lea rcx, [rel szFc64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_u64_arg_b], 0
    je .bad
    cmp byte [rel g_u64_arg_c], 0
    je .bad

    mov r9d, 1
    cmp dword [rel g_fc64_bin], 0
    je .lda
    xor r9d, r9d
.lda:
    lea rcx, [rel g_u64_arg_b]
    mov rdx, qword [rel g_u64_arena]
    mov r8d, 220000
    call u64_load_stream
    jc .nf
    mov r12, rax ; len A

    mov r9d, 1
    cmp dword [rel g_fc64_bin], 0
    je .ldb
    xor r9d, r9d
.ldb:
    lea rcx, [rel g_u64_arg_c]
    mov rdx, qword [rel g_u64_arena]
    add rdx, 262144
    mov r8d, 220000
    call u64_load_stream
    jc .nf
    mov r13, rax ; len B

    lea rcx, [rel szFc64Ban1]
    call u64_write_str
    lea rcx, [rel g_u64_arg_b]
    call u64_write_str
    lea rcx, [rel szFc64Ban2]
    call u64_write_str
    lea rcx, [rel g_u64_arg_c]
    call u64_write_str
    call u64_write_crlf

    mov dword [rel g_fc64_diffs], 0
    cmp dword [rel g_fc64_bin], 0
    je .txt

    mov rsi, qword [rel g_u64_arena]
    lea rdi, [rsi + 262144]
    mov r14, r12
    cmp r14, r13
    jbe .gmin
    mov r14, r13
.gmin:
    xor ebx, ebx
.blp:
    cmp rbx, r14
    jae .blen
    mov al, byte [rsi + rbx]
    cmp al, byte [rdi + rbx]
    je .bnx
    mov dword [rel g_fc64_diffs], 1
    mov ecx, ebx
    call u64_print_hex32
    lea rcx, [rel szFc64Col]
    call u64_write_str
    mov cl, byte [rsi + rbx]
    call u64_print_hex8
    mov cl, ' '
    call u64_write_chr
    mov cl, byte [rdi + rbx]
    call u64_print_hex8
    call u64_write_crlf
.bnx:
    inc rbx
    jmp .blp
.blen:
    cmp r12, r13
    je .chk_d
    mov dword [rel g_fc64_diffs], 1
    lea rcx, [rel szFc64Long]
    call u64_write_str
    call u64_write_crlf
    jmp .chk_d

.txt:
    mov rsi, qword [rel g_u64_arena]
    lea r12, [rsi + r12] ; end A
    mov rdi, qword [rel g_u64_arena]
    add rdi, 262144
    lea r13, [rdi + r13] ; end B
    xor r15d, r15d ; line_no

.tlp:
    cmp rsi, r12
    jae .aeof
    cmp rdi, r13
    jae .dtail
    inc r15

    mov rbx, rsi
.ea:
    cmp rbx, r12
    jae .ga
    mov al, byte [rbx]
    cmp al, 13
    je .ga
    cmp al, 10
    je .ga
    inc rbx
    jmp .ea
.ga:
    mov r14, rdi
.eb:
    cmp r14, r13
    jae .gb
    mov al, byte [r14]
    cmp al, 13
    je .gb
    cmp al, 10
    je .gb
    inc r14
    jmp .eb
.gb:
    mov rdx, rbx
    sub rdx, rsi
    mov r9, r14
    sub r9, rdi
    mov qword [rsp + 40], rdx
    mov qword [rsp + 48], r9
    mov rcx, rsi
    mov r8, rdi
    call fc64_lines_eq
    test eax, eax
    jnz .adv

    mov dword [rel g_fc64_diffs], 1
    lea rcx, [rel szFc64Stars]
    call u64_write_str
    lea rcx, [rel g_u64_arg_b]
    call u64_write_str
    call u64_write_crlf
    cmp dword [rel g_fc64_nums], 0
    je .pla
    mov rcx, r15
    call u64_print_u64
    lea rcx, [rel szFc64Col]
    call u64_write_str
.pla:
    mov rcx, rsi
    mov rdx, qword [rsp + 40]
    call u64_write_out
    call u64_write_crlf

    lea rcx, [rel szFc64Stars]
    call u64_write_str
    lea rcx, [rel g_u64_arg_c]
    call u64_write_str
    call u64_write_crlf
    cmp dword [rel g_fc64_nums], 0
    je .plb
    mov rcx, r15
    call u64_print_u64
    lea rcx, [rel szFc64Col]
    call u64_write_str
.plb:
    mov rcx, rdi
    mov rdx, qword [rsp + 48]
    call u64_write_out
    call u64_write_crlf
    lea rcx, [rel szFc64End]
    call u64_write_str
    call u64_write_crlf

.adv:
    cmp rbx, r12
    jae .sa
    cmp byte [rbx], 13
    jne .calf
    inc rbx
.calf:
    cmp rbx, r12
    jae .sa
    cmp byte [rbx], 10
    jne .sa
    inc rbx
.sa:
    mov rsi, rbx

    cmp r14, r13
    jae .sb2
    cmp byte [r14], 13
    jne .cblf
    inc r14
.cblf:
    cmp r14, r13
    jae .sb2
    cmp byte [r14], 10
    jne .sb2
    inc r14
.sb2:
    mov rdi, r14
    jmp .tlp

.aeof:
    cmp rdi, r13
    je .chk_d
.dtail:
    mov dword [rel g_fc64_diffs], 1
    lea rcx, [rel szFc64Long]
    call u64_write_str
    call u64_write_crlf

.chk_d:
    cmp dword [rel g_fc64_diffs], 0
    jne .ex1
    lea rcx, [rel szFc64Ok]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret
.ex1:
    mov ecx, 1
    call u64_exit
    @epilog_ret
.nf:
    lea rcx, [rel szFc64Nf]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 2
    call u64_exit
    @epilog_ret
.bad:
    lea rcx, [rel szFc64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 2
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szFc64Ban1, "Comparing files ", 0
@data db szFc64Ban2, " and ", 0
@data db szFc64Ok, "FC: no differences encountered", 0
@data db szFc64Stars, "***** ", 0
@data db szFc64End, "*****", 0
@data db szFc64Col, ": ", 0
@data db szFc64Long, "FC: files are different sizes", 0
@data db szFc64Nf, "FC: cannot open file", 0
@data db szFc64Syn, "FC: Insufficient number of file specifications", 0
@data db szFc64Help, "Compares two files or sets of files and displays the differences (/B /C /L /N /U /W).", 0

@include "u_data64.inc"

@res dword g_fc64_bin, 1
@res dword g_fc64_ci, 1
@res dword g_fc64_nums, 1
@res dword g_fc64_ws, 1
@res dword g_fc64_diffs, 1
