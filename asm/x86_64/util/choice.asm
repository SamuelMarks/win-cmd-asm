;; @file choice.asm
;; @isa x64
;; @desc Standalone x86-64 CHOICE.EXE prompt utility (/C[:]choices, /N, /CS, /T nn /D c, /M msg).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc choice64_match
;; @abi win64_ms
;; @in CL = input char
;; @out EAX = 1-based index in g_ch64_list, or 0 if not matched
;; @err EAX = 0
;; @clobbers rax, rcx, rdx, r8
;; @desc Matches CL against choice list honoring /CS.
@proc choice64_match
    mov dl, cl
    cmp dword [rel g_ch64_cs], 0
    jne .sc
    cmp dl, 'a'
    jb .sc
    cmp dl, 'z'
    ja .sc
    sub dl, 32
.sc:
    xor ecx, ecx
    lea r8, [rel g_ch64_list]
.lp:
    mov al, byte [r8 + rcx]
    test al, al
    jz .no
    cmp dword [rel g_ch64_cs], 0
    jne .cc
    cmp al, 'a'
    jb .cc
    cmp al, 'z'
    ja .cc
    sub al, 32
.cc:
    cmp al, dl
    je .hit
    inc ecx
    jmp .lp
.hit:
    lea eax, [ecx + 1]
    ret
.no:
    xor eax, eax
    ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess (1..N or 255)
;; @err Exits 255 on syntax error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Entrypoint for x86-64 CHOICE.EXE.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_ch64_hide], 0
    mov dword [rel g_ch64_cs], 0
    mov dword [rel g_ch64_has_to], 0
    mov dword [rel g_ch64_to], 0
    mov byte [rel g_ch64_def], 0
    mov byte [rel g_ch64_msg], 0
    mov byte [rel g_ch64_list], 'Y'
    mov byte [rel g_ch64_list + 1], 'N'
    mov byte [rel g_ch64_list + 2], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .prompt
    cmp dl, '/'
    jne .mpos
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'N'
    je .sn
    cmp al, 'C'
    je .sc
    cmp al, 'T'
    je .st
    cmp al, 'D'
    je .sd
    cmp al, 'M'
    je .sm
    jmp .bad
.sn:
    mov dword [rel g_ch64_hide], 1
    jmp .arg_lp
.sc:
    mov al, byte [rel g_u64_arg_a + 2]
    call u64_chr_upper
    cmp al, 'S'
    jne .scl
    cmp byte [rel g_u64_arg_a + 3], 0
    jne .scl
    mov dword [rel g_ch64_cs], 1
    jmp .arg_lp
.scl:
    lea rdx, [rel g_u64_arg_a + 2]
    cmp byte [rdx], ':'
    jne .sci
    inc rdx
.sci:
    cmp byte [rdx], 0
    je .scn
    lea rcx, [rel g_ch64_list]
    call u64_strcpy
    jmp .arg_lp
.scn:
    mov rcx, rsi
    lea rdx, [rel g_ch64_list]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.st:
    cmp byte [rel g_u64_arg_a + 2], ':'
    jne .nt_t
    mov al, byte [rel g_u64_arg_a + 3]
    mov byte [rel g_ch64_def], al
    mov dword [rel g_ch64_has_to], 1
    cmp byte [rel g_u64_arg_a + 4], ','
    jne .arg_lp
    lea rcx, [rel g_u64_arg_a + 5]
    call u64_parse_u32
    mov dword [rel g_ch64_to], eax
    jmp .arg_lp
.nt_t:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    lea rcx, [rel g_u64_arg_b]
    call u64_parse_u32
    mov dword [rel g_ch64_to], eax
    mov dword [rel g_ch64_has_to], 1
    jmp .arg_lp
.sd:
    cmp byte [rel g_u64_arg_a + 2], ':'
    jne .nt_d
    mov al, byte [rel g_u64_arg_a + 3]
    mov byte [rel g_ch64_def], al
    jmp .arg_lp
.nt_d:
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_b]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_b]
    mov byte [rel g_ch64_def], al
    jmp .arg_lp
.sm:
    mov rcx, rsi
    lea rdx, [rel g_ch64_msg]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp
.mpos:
    mov rcx, rsi
    lea rdx, [rel g_ch64_msg]
    call u64_read_tok
    mov rsi, rax
    jmp .arg_lp

.help:
    lea rcx, [rel szCh64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szCh64Syn]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 255
    call u64_exit
    @epilog_ret

.prompt:
    cmp byte [rel g_ch64_list], 0
    je .bad
    cmp byte [rel g_ch64_msg], 0
    je .cbr
    lea rcx, [rel g_ch64_msg]
    call u64_write_str
    mov cl, ' '
    call u64_write_chr
.cbr:
    cmp dword [rel g_ch64_hide], 0
    jne .win
    mov cl, '['
    call u64_write_chr
    xor ebx, ebx
.prc:
    lea rdi, [rel g_ch64_list]
    mov al, byte [rdi + rbx]
    test al, al
    jz .ebr
    test ebx, ebx
    jz .emc
    mov cl, ','
    call u64_write_chr
    lea rdi, [rel g_ch64_list]
    mov al, byte [rdi + rbx]
.emc:
    cmp dword [rel g_ch64_cs], 0
    jne .rwc
    call u64_chr_upper
.rwc:
    mov cl, al
    call u64_write_chr
    inc ebx
    jmp .prc
.ebr:
    lea rcx, [rel szCh64Q]
    call u64_write_str

.win:
    cmp dword [rel g_ch64_has_to], 0
    je .rd
    cmp dword [rel g_ch64_to], 0
    je .udef
.rd:
    mov byte [rel g_ch64_in], 0
    mov rcx, qword [rel g_u64_stdin]
    lea rdx, [rel g_ch64_in]
    mov r8d, 1
    lea r9, [rel g_u64_io_done]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    test eax, eax
    jz .eof
    cmp dword [rel g_u64_io_done], 0
    je .eof
    mov cl, byte [rel g_ch64_in]
    cmp cl, 13
    je .rd
    cmp cl, 10
    je .rd
    mov bl, cl
    call choice64_match
    test eax, eax
    jz .rd
    mov edi, eax
    mov cl, bl
    call u64_write_chr
    call u64_write_crlf
    mov ecx, edi
    call u64_exit
    @epilog_ret

.eof:
    cmp byte [rel g_ch64_def], 0
    jne .udef
    mov ecx, 255
    call u64_exit
    @epilog_ret
.udef:
    mov cl, byte [rel g_ch64_def]
    mov bl, cl
    call choice64_match
    test eax, eax
    jz .bad
    mov edi, eax
    mov cl, bl
    call u64_write_chr
    call u64_write_crlf
    mov ecx, edi
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szCh64Q, "]?", 0
@data db szCh64Syn, "ERROR: Invalid choice syntax.", 0
@data db szCh64Help, "Prompts the user to select one item from a list of choices (/C /N /CS /T /D /M).", 0

@include "u_data64.inc"

@res dword g_ch64_hide, 1
@res dword g_ch64_cs, 1
@res dword g_ch64_has_to, 1
@res dword g_ch64_to, 1
@res byte g_ch64_def, 4
@res byte g_ch64_in, 4
@res byte g_ch64_list, 128
@res byte g_ch64_msg, 520
