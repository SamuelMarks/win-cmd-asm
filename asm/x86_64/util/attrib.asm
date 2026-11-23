;; @file attrib.asm
;; @isa x64
;; @desc Standalone x86-64 ATTRIB.EXE utility ([+|-][R|A|S|H|I|O|P|U] [path] [/S] [/D] [/L]).

@cpu x64
@format coff64
@entry mainCRTStartup
@global mainCRTStartup

@include "u_imps64.inc"

@section text

@include "u_common64.inc"

;; @proc attr64_flag_bit
;; @abi win64_ms
;; @in AL = uppercase attribute letter
;; @out EAX = Win32 attribute bitmask (0 if unrecognized)
;; @err EAX = 0
;; @clobbers rax
;; @desc Maps R, H, S, A, I, O, P, U to Win32 FILE_ATTRIBUTE_* constants.
@proc attr64_flag_bit
    cmp al, 'R'
    je .r
    cmp al, 'H'
    je .h
    cmp al, 'S'
    je .s
    cmp al, 'A'
    je .a
    cmp al, 'O'
    je .o
    cmp al, 'I'
    je .i
    cmp al, 'P'
    je .p
    cmp al, 'U'
    je .u
    xor eax, eax
    ret
.r:
    mov eax, 0x01
    ret
.h:
    mov eax, 0x02
    ret
.s:
    mov eax, 0x04
    ret
.a:
    mov eax, 0x20
    ret
.o:
    mov eax, 0x1000
    ret
.i:
    mov eax, 0x2000
    ret
.p:
    mov eax, 0x00080000
    ret
.u:
    mov eax, 0x00100000
    ret
@endproc

;; @proc attr64_print_entry
;; @abi win64_ms
;; @in ECX = Win32 attributes, RDX = UTF-8 file path
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Prints formatted "A  SHR OIPU  path" line.
@proc_frame attr64_print_entry, 40, rbx, rsi
    mov ebx, ecx
    mov rsi, rdx

    mov cl, ' '
    test ebx, 0x20
    jz .p1
    mov cl, 'A'
.p1:
    call u64_write_chr
    mov cl, ' '
    call u64_write_chr
    mov cl, ' '
    test ebx, 0x04
    jz .p2
    mov cl, 'S'
.p2:
    call u64_write_chr
    mov cl, ' '
    test ebx, 0x02
    jz .p3
    mov cl, 'H'
.p3:
    call u64_write_chr
    mov cl, ' '
    test ebx, 0x01
    jz .p4
    mov cl, 'R'
.p4:
    call u64_write_chr
    mov cl, ' '
    call u64_write_chr
    mov cl, ' '
    test ebx, 0x1000
    jz .p5
    mov cl, 'O'
.p5:
    call u64_write_chr
    mov cl, ' '
    test ebx, 0x2000
    jz .p6
    mov cl, 'I'
.p6:
    call u64_write_chr
    mov cl, ' '
    test ebx, 0x00080000
    jz .p7
    mov cl, 'P'
.p7:
    call u64_write_chr
    mov cl, ' '
    test ebx, 0x00100000
    jz .p8
    mov cl, 'U'
.p8:
    call u64_write_chr
    mov cl, ' '
    call u64_write_chr
    mov cl, ' '
    call u64_write_chr
    mov rcx, rsi
    call u64_write_str
    call u64_write_crlf
    @epilog_ret
@endproc

;; @proc attr64_apply_one
;; @abi win64_ms
;; @in RCX = full UTF-8 path, EDX = current attributes
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Applies +set/-clear masks or prints attributes for one item.
@proc_frame attr64_apply_one, 40
    mov eax, dword [rel g_attr64_set]
    or eax, dword [rel g_attr64_clr]
    jz .show
    mov eax, dword [rel g_attr64_clr]
    not eax
    and edx, eax
    or edx, dword [rel g_attr64_set]
    jnz .do_set
    mov edx, 0x80
.do_set:
    call u64_set_attr
    mov dword [rel g_attr64_hit], 1
    @epilog_ret
.show:
    mov r8, rcx
    mov ecx, edx
    mov rdx, r8
    call attr64_print_entry
    mov dword [rel g_attr64_hit], 1
    @epilog_ret
@endproc

;; @proc attr64_scan_dir
;; @abi win64_ms
;; @in RCX = dir UTF-8, RDX = leaf pattern UTF-8, R8D = depth
;; @out none
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Enumerates matching entries in RCX and recurses under /S.
@proc_frame attr64_scan_dir, 1664, rbx, rsi, rdi, r12, r13
    cmp r8d, 16
    jae .done
    mov r12, rdx ; leaf
    mov r13d, r8d ; depth
    mov rdx, rcx
    lea rcx, [rsp + 1120]
    call u64_strcpy

    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    mov r8, r12
    call u64_path_join

    lea rcx, [rsp + 608]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .chk_sub
    mov rbx, rax

.ent_lp:
    cmp byte [rsp + 88], '.'
    jne .not_dot
    cmp byte [rsp + 89], 0
    je .nx_ent
    cmp byte [rsp + 89], '.'
    jne .not_dot
    cmp byte [rsp + 90], 0
    je .nx_ent
.not_dot:
    mov eax, dword [rsp + 64]
    test eax, 0x10
    jz .do_item
    cmp dword [rel g_attr64_dirs], 0
    je .nx_ent
.do_item:
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 608]
    mov edx, dword [rsp + 64]
    call attr64_apply_one

.nx_ent:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .ent_lp

    mov rcx, rbx
    call qword [rel __imp_FindClose]

.chk_sub:
    cmp dword [rel g_attr64_sub], 0
    je .done
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rel szAttr64Star]
    call u64_path_join
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 64]
    call u64_find_first
    cmp rax, -1
    je .done
    mov rbx, rax
.sub_lp:
    test dword [rsp + 64], 0x10
    jz .nx_sub
    cmp byte [rsp + 88], '.'
    jne .go_sub
    cmp byte [rsp + 89], 0
    je .nx_sub
    cmp byte [rsp + 89], '.'
    jne .go_sub
    cmp byte [rsp + 90], 0
    je .nx_sub
.go_sub:
    lea rcx, [rsp + 608]
    lea rdx, [rsp + 1120]
    lea r8, [rsp + 88]
    call u64_path_join
    lea rcx, [rsp + 608]
    mov rdx, r12
    lea r8d, [r13d + 1]
    call attr64_scan_dir
.nx_sub:
    mov rcx, rbx
    lea rdx, [rsp + 64]
    call u64_find_next
    test eax, eax
    jnz .sub_lp
    mov rcx, rbx
    call qword [rel __imp_FindClose]

.done:
    @epilog_ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid switch or missing target
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Parses ATTRIB switches and modifiers and runs scan.
@proc_frame mainCRTStartup, 48, rbx, rsi, rdi
    call u64_init
    mov rsi, rax
    mov dword [rel g_attr64_set], 0
    mov dword [rel g_attr64_clr], 0
    mov dword [rel g_attr64_sub], 0
    mov dword [rel g_attr64_dirs], 0
    mov dword [rel g_attr64_hit], 0
    mov byte [rel g_u64_arg_b], 0

.arg_lp:
    mov rcx, rsi
    call u64_skip_ws
    mov rsi, rax
    test dl, dl
    jz .run
    mov rcx, rsi
    lea rdx, [rel g_u64_arg_a]
    call u64_read_tok
    mov rsi, rax
    mov al, byte [rel g_u64_arg_a]
    cmp al, '+'
    je .plus_m
    cmp al, '-'
    je .minus_m
    cmp al, '/'
    je .sw_m
    lea rcx, [rel g_u64_arg_b]
    lea rdx, [rel g_u64_arg_a]
    call u64_strcpy
    jmp .arg_lp

.plus_m:
    mov al, byte [rel g_u64_arg_a + 1]
    call u64_chr_upper
    call attr64_flag_bit
    test eax, eax
    jz .bad
    or dword [rel g_attr64_set], eax
    jmp .arg_lp

.minus_m:
    mov al, byte [rel g_u64_arg_a + 1]
    call u64_chr_upper
    call attr64_flag_bit
    test eax, eax
    jz .bad
    or dword [rel g_attr64_clr], eax
    jmp .arg_lp

.sw_m:
    mov al, byte [rel g_u64_arg_a + 1]
    cmp al, '?'
    je .help
    call u64_chr_upper
    cmp al, 'S'
    je .sw_s
    cmp al, 'D'
    je .sw_d
    cmp al, 'L'
    je .arg_lp
    jmp .bad
.sw_s:
    mov dword [rel g_attr64_sub], 1
    jmp .arg_lp
.sw_d:
    mov dword [rel g_attr64_dirs], 1
    jmp .arg_lp

.help:
    lea rcx, [rel szAttr64Help]
    call u64_write_str
    call u64_write_crlf
    xor ecx, ecx
    call u64_exit
    @epilog_ret

.bad:
    lea rcx, [rel szAttr64Bad]
    call u64_write_err_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret

.run:
    cmp byte [rel g_u64_arg_b], 0
    jne .has_t
    lea rcx, [rel g_u64_arg_c]
    mov edx, 1000
    call u64_get_cwd
    lea rcx, [rel g_u64_arg_c]
    lea rdx, [rel szAttr64Star]
    xor r8d, r8d
    call attr64_scan_dir
    jmp .chk_hit

.has_t:
    lea rcx, [rel g_u64_arg_b]
    call u64_strlen
    mov rdx, -1
    xor ebx, ebx
    lea rdi, [rel g_u64_arg_b]
.sl:
    cmp rbx, rax
    jae .got_sl
    mov cl, byte [rdi + rbx]
    cmp cl, 0x5C
    je .mk
    cmp cl, '/'
    je .mk
    cmp cl, ':'
    jne .nx
.mk:
    mov rdx, rbx
.nx:
    inc rbx
    jmp .sl
.got_sl:
    cmp rdx, -1
    jne .split_p
    lea rcx, [rel g_u64_arg_c]
    mov edx, 1000
    call u64_get_cwd
    lea rcx, [rel g_attr64_leaf]
    lea rdx, [rel g_u64_arg_b]
    call u64_strcpy
    jmp .do_sc
.split_p:
    xor ebx, ebx
    lea rcx, [rel g_u64_arg_c]
.cp_d:
    cmp rbx, rdx
    ja .tm_d
    mov al, byte [rdi + rbx]
    mov byte [rcx + rbx], al
    inc rbx
    jmp .cp_d
.tm_d:
    mov byte [rcx + rbx], 0
    lea rcx, [rel g_attr64_leaf]
    lea rdx, [rdi + rdx + 1]
    call u64_strcpy
.do_sc:
    lea rcx, [rel g_u64_arg_c]
    lea rdx, [rel g_attr64_leaf]
    xor r8d, r8d
    call attr64_scan_dir

.chk_hit:
    cmp dword [rel g_attr64_hit], 0
    jne .ex0
    lea rcx, [rel szAttr64Nf]
    call u64_write_str
    call u64_write_crlf
    mov ecx, 1
    call u64_exit
    @epilog_ret
.ex0:
    xor ecx, ecx
    call u64_exit
    @epilog_ret
@endproc

@section rdata

@data db szAttr64Star, "*", 0
@data db szAttr64Nf, "File not found", 0
@data db szAttr64Bad, "Parameter format not correct -", 0
@data db szAttr64Help, "Displays or changes file attributes (+R -R +A -A +S -S +H -H +I -I +O -O +P -P +U -U /S /D /L).", 0

@include "u_data64.inc"

@res dword g_attr64_set, 1
@res dword g_attr64_clr, 1
@res dword g_attr64_sub, 1
@res dword g_attr64_dirs, 1
@res dword g_attr64_hit, 1
@res byte g_attr64_leaf, 520
