;; @file attrib.asm
;; @isa 386
;; @desc Standalone IA-32 ATTRIB.EXE utility ([+|-][R|A|S|H|I|O|P|U] [path] [/S] [/D] [/L]).

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc attr_flag_bit
;; @abi win32
;; @in AL = uppercase attribute letter
;; @out EAX = Win32 attribute bitmask (0 if unrecognized)
;; @err EAX = 0
;; @clobbers eax
;; @desc Maps R, H, S, A, I, O, P, U to Win32 FILE_ATTRIBUTE_* constants.
@proc attr_flag_bit
    cmp al, 'R'
    je .b_r
    cmp al, 'H'
    je .b_h
    cmp al, 'S'
    je .b_s
    cmp al, 'A'
    je .b_a
    cmp al, 'O'
    je .b_o
    cmp al, 'I'
    je .b_i
    cmp al, 'P'
    je .b_p
    cmp al, 'U'
    je .b_u
    xor eax, eax
    ret
.b_r:
    mov eax, 0x00000001
    ret
.b_h:
    mov eax, 0x00000002
    ret
.b_s:
    mov eax, 0x00000004
    ret
.b_a:
    mov eax, 0x00000020
    ret
.b_o:
    mov eax, 0x00001000
    ret
.b_i:
    mov eax, 0x00002000
    ret
.b_p:
    mov eax, 0x00080000
    ret
.b_u:
    mov eax, 0x00100000
    ret
@endproc

;; @proc attr_print_entry
;; @abi win32
;; @in ECX = Win32 attributes, ESI = UTF-8 file path
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Prints formatted "A  SHR OIPU  path" line matching Windows ATTRIB.EXE layout.
@proc attr_print_entry
    push ebx
    push esi
    mov ebx, ecx

    mov al, ' '
    test ebx, 0x20
    jz .p1
    mov al, 'A'
.p1:
    call u32_write_chr
    mov al, ' '
    call u32_write_chr
    mov al, ' '
    test ebx, 0x04
    jz .p2
    mov al, 'S'
.p2:
    call u32_write_chr
    mov al, ' '
    test ebx, 0x02
    jz .p3
    mov al, 'H'
.p3:
    call u32_write_chr
    mov al, ' '
    test ebx, 0x01
    jz .p4
    mov al, 'R'
.p4:
    call u32_write_chr
    mov al, ' '
    call u32_write_chr
    mov al, ' '
    test ebx, 0x1000
    jz .p5
    mov al, 'O'
.p5:
    call u32_write_chr
    mov al, ' '
    test ebx, 0x2000
    jz .p6
    mov al, 'I'
.p6:
    call u32_write_chr
    mov al, ' '
    test ebx, 0x00080000
    jz .p7
    mov al, 'P'
.p7:
    call u32_write_chr
    mov al, ' '
    test ebx, 0x00100000
    jz .p8
    mov al, 'U'
.p8:
    call u32_write_chr
    mov al, ' '
    call u32_write_chr
    mov al, ' '
    call u32_write_chr
    call u32_write_str
    call u32_write_crlf
    pop esi
    pop ebx
    ret
@endproc

;; @proc attr_apply_one
;; @abi win32
;; @in ESI = full UTF-8 path, ECX = current attributes
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Applies +set/-clear masks or prints attributes for a single entry.
@proc attr_apply_one
    mov eax, dword [g_attr_set]
    or eax, dword [g_attr_clr]
    jz .show
    mov eax, dword [g_attr_clr]
    not eax
    and ecx, eax
    or ecx, dword [g_attr_set]
    jnz .do_set
    mov ecx, 0x80 ; FILE_ATTRIBUTE_NORMAL when all bits cleared
.do_set:
    call u32_set_attr
    mov dword [g_attr_matched], 1
    ret
.show:
    call attr_print_entry
    mov dword [g_attr_matched], 1
    ret
@endproc

;; @proc attr_scan_dir
;; @abi win32
;; @in ESI = directory UTF-8, EDI = leaf pattern UTF-8, ECX = depth (max 16)
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Enumerates matching entries in ESI and optionally recurses into subdirectories (/S).
@proc attr_scan_dir
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 1600
    ; [esp+0..543] = 544B find entry
    ; [esp+544..1055] = search pattern / child path
    ; [esp+1056..1567] = saved dir copy
    ; [esp+1568] = leaf ptr, [esp+1572] = depth
    mov dword [esp + 1568], edi
    mov dword [esp + 1572], ecx
    lea edi, [esp + 1056]
    call u32_strcpy

    lea esi, [esp + 1056]
    mov edi, dword [esp + 1568]
    lea ebx, [esp + 544]
    call u32_path_join

    lea esi, [esp + 544]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .chk_recurse
    mov ebp, eax

.ent_lp:
    cmp byte [esp + 20], '.'
    jne .not_dot
    cmp byte [esp + 21], 0
    je .nx_ent
    cmp byte [esp + 21], '.'
    jne .not_dot
    cmp byte [esp + 22], 0
    je .nx_ent
.not_dot:
    mov ecx, dword [esp]
    test ecx, 0x10
    jz .do_item
    cmp dword [g_attr_dirs], 0
    je .nx_ent
.do_item:
    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov ecx, dword [esp]
    call attr_apply_one

.nx_ent:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .ent_lp

    push ebp
    call dword [__imp__FindClose@4]

.chk_recurse:
    cmp dword [g_attr_sub], 0
    je .done
    cmp dword [esp + 1572], 16
    jae .done

    lea esi, [esp + 1056]
    mov edi, addr(szAttrStar)
    lea ebx, [esp + 544]
    call u32_path_join

    lea esi, [esp + 544]
    mov edi, esp
    call u32_find_first
    cmp eax, -1
    je .done
    mov ebp, eax

.sub_lp:
    test dword [esp], 0x10
    jz .nx_sub
    cmp byte [esp + 20], '.'
    jne .go_sub
    cmp byte [esp + 21], 0
    je .nx_sub
    cmp byte [esp + 21], '.'
    jne .go_sub
    cmp byte [esp + 22], 0
    je .nx_sub
.go_sub:
    lea esi, [esp + 1056]
    lea edi, [esp + 20]
    lea ebx, [esp + 544]
    call u32_path_join
    lea esi, [esp + 544]
    mov edi, dword [esp + 1568]
    mov ecx, dword [esp + 1572]
    inc ecx
    call attr_scan_dir

.nx_sub:
    mov ebx, ebp
    mov edi, esp
    call u32_find_next
    test eax, eax
    jnz .sub_lp

    push ebp
    call dword [__imp__FindClose@4]

.done:
    add esp, 1600
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint
;; @out Terminates via ExitProcess
;; @err Exits 1 on invalid switch or missing target
;; @clobbers eax, ecx, edx
;; @desc Parses ATTRIB switches and modifiers and runs single-target or wildcard/recursive scan.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi

    call u32_init
    mov esi, eax
    mov dword [g_attr_set], 0
    mov dword [g_attr_clr], 0
    mov dword [g_attr_sub], 0
    mov dword [g_attr_dirs], 0
    mov dword [g_attr_link], 0
    mov dword [g_attr_matched], 0
    mov byte [g_u32_arg_b], 0

.arg_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .run
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a]
    cmp al, '+'
    je .plus_mod
    cmp al, '-'
    je .minus_mod
    cmp al, '/'
    je .sw_mod
    push esi
    mov esi, addr(g_u32_arg_a)
    mov edi, addr(g_u32_arg_b)
    call u32_strcpy
    pop esi
    jmp .arg_lp

.plus_mod:
    mov al, byte [g_u32_arg_a + 1]
    call u32_chr_upper
    call attr_flag_bit
    test eax, eax
    jz .bad_arg
    or dword [g_attr_set], eax
    jmp .arg_lp

.minus_mod:
    mov al, byte [g_u32_arg_a + 1]
    call u32_chr_upper
    call attr_flag_bit
    test eax, eax
    jz .bad_arg
    or dword [g_attr_clr], eax
    jmp .arg_lp

.sw_mod:
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    call u32_chr_upper
    cmp al, 'S'
    je .sw_s
    cmp al, 'D'
    je .sw_d
    cmp al, 'L'
    je .sw_l
    jmp .bad_arg
.sw_s:
    mov dword [g_attr_sub], 1
    jmp .arg_lp
.sw_d:
    mov dword [g_attr_dirs], 1
    jmp .arg_lp
.sw_l:
    mov dword [g_attr_link], 1
    jmp .arg_lp

.help:
    mov esi, addr(szAttrHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.bad_arg:
    mov esi, addr(szAttrBad)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.run:
    cmp byte [g_u32_arg_b], 0
    jne .has_target
    mov edi, addr(g_u32_arg_c)
    mov ecx, 1000
    call u32_get_cwd
    mov esi, addr(g_u32_arg_c)
    mov edi, addr(szAttrStar)
    xor ecx, ecx
    call attr_scan_dir
    jmp .chk_res

.has_target:
    ; Split g_u32_arg_b into parent dir (g_u32_arg_c) and leaf pattern (g_attr_leaf)
    mov esi, addr(g_u32_arg_b)
    call u32_strlen
    mov ecx, eax
    mov edx, -1
    xor ebx, ebx
.find_sep:
    cmp ebx, ecx
    jae .got_sep
    mov al, byte [g_u32_arg_b + ebx]
    cmp al, 0x5C
    je .mark_sep
    cmp al, '/'
    je .mark_sep
    cmp al, ':'
    jne .nx_ch
.mark_sep:
    mov edx, ebx
.nx_ch:
    inc ebx
    jmp .find_sep

.got_sep:
    cmp edx, -1
    jne .split_path
    mov edi, addr(g_u32_arg_c)
    mov ecx, 1000
    call u32_get_cwd
    mov esi, addr(g_u32_arg_b)
    mov edi, addr(g_attr_leaf)
    call u32_strcpy
    jmp .do_scan

.split_path:
    xor ebx, ebx
.cp_dir:
    cmp ebx, edx
    ja .term_dir
    mov al, byte [g_u32_arg_b + ebx]
    mov byte [g_u32_arg_c + ebx], al
    inc ebx
    jmp .cp_dir
.term_dir:
    mov byte [g_u32_arg_c + ebx], 0
    lea esi, [g_u32_arg_b + edx + 1]
    mov edi, addr(g_attr_leaf)
    call u32_strcpy

.do_scan:
    mov esi, addr(g_u32_arg_c)
    mov edi, addr(g_attr_leaf)
    xor ecx, ecx
    call attr_scan_dir

.chk_res:
    cmp dword [g_attr_matched], 0
    jne .ok_out
    mov esi, addr(szAttrNf)
    call u32_write_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.ok_out:
    xor eax, eax
    call u32_exit
.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szAttrStar, "*", 0
@data db szAttrNf, "File not found", 0
@data db szAttrBad, "Parameter format not correct -", 0
@data db szAttrHelp, "Displays or changes file attributes (+R -R +A -A +S -S +H -H +I -I +O -O +P -P +U -U /S /D /L).", 0

@include "u_data32.inc"

@res dword g_attr_set, 1
@res dword g_attr_clr, 1
@res dword g_attr_sub, 1
@res dword g_attr_dirs, 1
@res dword g_attr_link, 1
@res dword g_attr_matched, 1
@res byte g_attr_leaf, 520
