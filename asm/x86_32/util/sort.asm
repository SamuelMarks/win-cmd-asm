;; @file sort.asm
;; @isa 386
;; @desc Standalone IA-32 SORT.EXE utility (/R, /+n, /M, /L, /REC, /T, /O out, /UNIQUE) with O(N log N) merge sort.

@cpu 386
@format coff32
@safeseh _u32_seh_handler
@global _u32_seh_handler
@global _mainCRTStartup

@include "u_imps32.inc"

@section text

@include "u_common32.inc"

;; @proc sort_cmp_rec
;; @abi win32
;; @in ESI = ptr to record A (0:ptr, 4:len), EDI = ptr to record B (0:ptr, 4:len)
;; @out EAX = -1 if A < B, 0 if A == B, 1 if A > B (honoring /+n and /R)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Case-insensitive collate comparison of two line slices starting at column g_sort_col.
@proc sort_cmp_rec
    push ebx
    push esi
    push edi
    push ebp

    mov ebx, dword [esi] ; ptrA
    mov ecx, dword [esi + 4] ; lenA
    mov ebp, dword [edi] ; ptrB
    mov edx, dword [edi + 4] ; lenB

    mov eax, dword [g_sort_col]
    cmp ecx, eax
    ja .adj_a
    xor ecx, ecx
    jmp .chk_b
.adj_a:
    add ebx, eax
    sub ecx, eax
.chk_b:
    cmp edx, eax
    ja .adj_b
    xor edx, edx
    jmp .cmp_lp
.adj_b:
    add ebp, eax
    sub edx, eax

.cmp_lp:
    test ecx, ecx
    jz .a_end
    test edx, edx
    jz .gt_raw
    mov al, byte [ebx]
    call u32_chr_upper
    mov ah, al
    mov al, byte [ebp]
    call u32_chr_upper
    cmp ah, al
    jb .lt_raw
    ja .gt_raw
    inc ebx
    inc ebp
    dec ecx
    dec edx
    jmp .cmp_lp

.a_end:
    test edx, edx
    jz .eq_out
.lt_raw:
    mov eax, -1
    jmp .apply_rev
.gt_raw:
    mov eax, 1
.apply_rev:
    cmp dword [g_sort_rev], 0
    je .dn
    neg eax
    jmp .dn
.eq_out:
    xor eax, eax
.dn:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc sort_merge_run
;; @abi win32
;; @in ESI = src array of 8B records, EDI = dst array, EBX = left, ECX = mid, EDX = right
;; @out none
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Merges sorted runs [left..mid) and [mid..right) from ESI into EDI.
@proc sort_merge_run
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 20
    mov dword [esp], ebx ; i = left
    mov dword [esp + 4], ecx ; j = mid
    mov dword [esp + 8], ecx ; mid
    mov dword [esp + 12], edx ; right
    mov dword [esp + 16], ebx ; k = left

.m_lp:
    mov eax, dword [esp]
    cmp eax, dword [esp + 8]
    jae .take_r_rem
    mov ecx, dword [esp + 4]
    cmp ecx, dword [esp + 12]
    jae .take_l_rem

    mov edx, dword [esp + 4]
    push esi
    push edi
    lea edi, [esi + edx * 8]
    lea esi, [esi + eax * 8]
    call sort_cmp_rec
    pop edi
    pop esi
    cmp eax, 0
    jg .take_r_one

.take_l_one:
    mov eax, dword [esp]
    mov ecx, dword [esp + 16]
    mov edx, dword [esi + eax * 8]
    mov dword [edi + ecx * 8], edx
    mov edx, dword [esi + eax * 8 + 4]
    mov dword [edi + ecx * 8 + 4], edx
    inc dword [esp]
    inc dword [esp + 16]
    jmp .m_lp

.take_r_one:
    mov eax, dword [esp + 4]
    mov ecx, dword [esp + 16]
    mov edx, dword [esi + eax * 8]
    mov dword [edi + ecx * 8], edx
    mov edx, dword [esi + eax * 8 + 4]
    mov dword [edi + ecx * 8 + 4], edx
    inc dword [esp + 4]
    inc dword [esp + 16]
    jmp .m_lp

.take_l_rem:
    mov eax, dword [esp]
    cmp eax, dword [esp + 8]
    jae .done
    mov ecx, dword [esp + 16]
    mov edx, dword [esi + eax * 8]
    mov dword [edi + ecx * 8], edx
    mov edx, dword [esi + eax * 8 + 4]
    mov dword [edi + ecx * 8 + 4], edx
    inc dword [esp]
    inc dword [esp + 16]
    jmp .take_l_rem

.take_r_rem:
    mov eax, dword [esp + 4]
    cmp eax, dword [esp + 12]
    jae .done
    mov ecx, dword [esp + 16]
    mov edx, dword [esi + eax * 8]
    mov dword [edi + ecx * 8], edx
    mov edx, dword [esi + eax * 8 + 4]
    mov dword [edi + ecx * 8 + 4], edx
    inc dword [esp + 4]
    inc dword [esp + 16]
    jmp .take_r_rem

.done:
    add esp, 20
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
;; @err Exits 1 on missing input file
;; @clobbers eax, ecx, edx
;; @desc Parses SORT switches, loads input into arena, runs bottom-up O(N log N) merge sort, and writes output.
@proc _mainCRTStartup
    push ebx
    push esi
    push edi
    push ebp

    call u32_init
    mov esi, eax
    mov dword [g_sort_rev], 0
    mov dword [g_sort_col], 0
    mov dword [g_sort_uniq], 0
    mov byte [g_u32_arg_b], 0 ; input file
    mov byte [g_u32_arg_c], 0 ; /O output file

.sw_lp:
    call u32_skip_ws
    mov esi, eax
    test dl, dl
    jz .load_data
    cmp dl, '/'
    jne .in_file
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    mov al, byte [g_u32_arg_a + 1]
    cmp al, '?'
    je .help
    cmp al, '+'
    je .sw_col
    call u32_chr_upper
    cmp al, 'R'
    je .sw_r_or_rec
    cmp al, 'U'
    je .sw_u
    cmp al, 'O'
    je .sw_o
    cmp al, 'M'
    je .skip_val
    cmp al, 'L'
    je .skip_val
    cmp al, 'T'
    je .skip_val
    jmp .sw_lp

.sw_r_or_rec:
    mov al, byte [g_u32_arg_a + 2]
    call u32_chr_upper
    cmp al, 'E'
    je .skip_val
    mov dword [g_sort_rev], 1
    jmp .sw_lp
.sw_u:
    mov dword [g_sort_uniq], 1
    jmp .sw_lp
.sw_col:
    push esi
    mov esi, addr(g_u32_arg_a)
    add esi, 2
    call u32_parse_u32
    pop esi
    test eax, eax
    jz .sw_lp
    dec eax
    mov dword [g_sort_col], eax
    jmp .sw_lp
.sw_o:
    mov edi, addr(g_u32_arg_c)
    call u32_read_tok
    mov esi, eax
    jmp .sw_lp
.skip_val:
    mov edi, addr(g_u32_arg_a)
    call u32_read_tok
    mov esi, eax
    jmp .sw_lp

.in_file:
    mov edi, addr(g_u32_arg_b)
    call u32_read_tok
    mov esi, eax
    jmp .sw_lp

.help:
    mov esi, addr(szSortHelp)
    call u32_write_str
    call u32_write_crlf
    xor eax, eax
    call u32_exit
    jmp .done

.load_data:
    ; Layout in 512 KiB arena:
    ; [0 .. 262143]       = raw text buffer
    ; [262144 .. 393215]  = record array A (up to 16,384 x 8B records)
    ; [393216 .. 524287]  = scratch record array B (16,384 x 8B records)
    mov esi, addr(g_u32_arg_b)
    mov edi, dword [g_u32_arena]
    mov ecx, 260000
    mov edx, 1
    call u32_load_stream
    jnc .split_lines
    mov esi, addr(szSortNf)
    call u32_write_err_str
    call u32_write_crlf
    mov eax, 1
    call u32_exit
    jmp .done

.split_lines:
    mov esi, dword [g_u32_arena]
    lea ebp, [esi + eax]
    mov edi, dword [g_u32_arena]
    add edi, 262144
    xor ebx, ebx ; line count

.sp_lp:
    cmp esi, ebp
    jae .sort_it
    cmp byte [esi], 0x1A
    je .sort_it
    cmp ebx, 16380
    jae .sort_it
    mov ecx, esi
.eol:
    cmp ecx, ebp
    jae .got_l
    mov al, byte [ecx]
    cmp al, 13
    je .got_l
    cmp al, 10
    je .got_l
    inc ecx
    jmp .eol
.got_l:
    mov dword [edi + ebx * 8], esi
    mov eax, ecx
    sub eax, esi
    mov dword [edi + ebx * 8 + 4], eax
    inc ebx
    cmp ecx, ebp
    jae .sort_it
    cmp byte [ecx], 13
    jne .c_lf
    inc ecx
.c_lf:
    cmp ecx, ebp
    jae .nx_sp
    cmp byte [ecx], 10
    jne .nx_sp
    inc ecx
.nx_sp:
    mov esi, ecx
    jmp .sp_lp

.sort_it:
    mov dword [g_sort_cnt], ebx
    mov esi, dword [g_u32_arena]
    add esi, 262144 ; active array
    mov edi, dword [g_u32_arena]
    add edi, 393216 ; temp array

    cmp ebx, 2
    jb .emit_out

    mov ebp, 1 ; width = 1, 2, 4, ...
.width_lp:
    cmp ebp, dword [g_sort_cnt]
    jae .emit_out
    xor ebx, ebx ; left = 0
.run_lp:
    cmp ebx, dword [g_sort_cnt]
    jae .swap_arr
    lea ecx, [ebx + ebp]
    cmp ecx, dword [g_sort_cnt]
    jbe .got_mid
    mov ecx, dword [g_sort_cnt]
.got_mid:
    lea edx, [ecx + ebp]
    cmp edx, dword [g_sort_cnt]
    jbe .got_right
    mov edx, dword [g_sort_cnt]
.got_right:
    call sort_merge_run
    lea ebx, [ebx + ebp * 2]
    jmp .run_lp
.swap_arr:
    mov eax, esi
    mov esi, edi
    mov edi, eax
    shl ebp, 1
    jmp .width_lp

.emit_out:
    ; Open /O output file if specified, else write to stdout
    mov dword [g_sort_out_h], 0
    cmp byte [g_u32_arg_c], 0
    je .wr_init
    push esi
    cmp dword [g_u32_is_win9x], 0
    jne .o9x
    mov esi, addr(g_u32_arg_c)
    mov edi, addr(g_u32_wbuf1)
    call u32_u8_to_w
    push 0
    push 0x80
    push 2
    push 0
    push 0
    push 0x40000000
    push addr(g_u32_wbuf1)
    call dword [__imp__CreateFileW@28]
    jmp .o_chk
.o9x:
    push 0
    push 0x80
    push 2
    push 0
    push 0
    push 0x40000000
    push addr(g_u32_arg_c)
    call dword [__imp__CreateFileA@28]
.o_chk:
    pop esi
    cmp eax, -1
    je .wr_init
    mov dword [g_sort_out_h], eax

.wr_init:
    mov ebp, esi ; final sorted array
    xor ebx, ebx
.wr_lp:
    cmp ebx, dword [g_sort_cnt]
    jae .close_out
    cmp dword [g_sort_uniq], 0
    je .do_wr
    test ebx, ebx
    jz .do_wr
    lea esi, [ebp + ebx * 8 - 8]
    lea edi, [ebp + ebx * 8]
    call sort_cmp_rec
    test eax, eax
    jz .nx_wr

.do_wr:
    mov esi, dword [ebp + ebx * 8]
    mov ecx, dword [ebp + ebx * 8 + 4]
    cmp dword [g_sort_out_h], 0
    jne .wr_file
    call u32_write_out
    call u32_write_crlf
    jmp .nx_wr
.wr_file:
    push ebx
    mov ebx, dword [g_sort_out_h]
    xor edx, edx
    call u32_write_handle
    mov esi, addr(szU32Crlf)
    mov ecx, 2
    xor edx, edx
    call u32_write_handle
    pop ebx
.nx_wr:
    inc ebx
    jmp .wr_lp

.close_out:
    mov eax, dword [g_sort_out_h]
    test eax, eax
    jz .ok_exit
    push eax
    call dword [__imp__CloseHandle@4]
.ok_exit:
    xor eax, eax
    call u32_exit
.done:
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret
@endproc

@section rdata

@data db szSortNf, "The system cannot find the file specified.", 0
@data db szSortHelp, "Sorts input and writes results to screen or file (/R /+n /UNIQUE /O out).", 0

@include "u_data32.inc"

@res dword g_sort_rev, 1
@res dword g_sort_col, 1
@res dword g_sort_uniq, 1
@res dword g_sort_cnt, 1
@res dword g_sort_out_h, 1
