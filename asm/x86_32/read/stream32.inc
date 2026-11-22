;; @file stream32.inc
;; @isa 386
;; @desc Stateless byte-offset BatchFrameReader (UTF-8 BOM, UTF-16LE/BE BOM, CP65001, 0x1A EOF, and ^ folding).

@cpu 386
@format inc

;; @proc batch_read_raw_line32
;; @abi win32
;; @in EBX = BATCH_FRAME32*, EDI = destination WTF-8 buffer, ECX = max byte capacity
;; @out EAX = byte length, CF=0; or CF=1 on EOF (handle is ALWAYS closed before returning!)
;; @err CF=1 on EOF or unreadable batch file
;; @clobbers eax, ecx, edx
;; @desc Opens batch file, detects BOM at offset 0, seeks to uFileOffLo, reads 1 physical line, updates offset, and closes handle.
@proc batch_read_raw_line32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 528
    mov ebp, esp
    mov dword [ebp + 516], ecx

    mov esi, dword [ebx + BATCH_FRAME32.pBatchPath]
    mov ecx, GENERIC_READ
    mov edx, OPEN_EXISTING
    xor eax, eax
    call hal_open_file32
    jc .eof
    mov dword [ebp + 520], eax ; hFile

    ; At offset 0 with unknown encoding (0), inspect BOM (EF BB BF, FF FE, FE FF)
    cmp dword [ebx + BATCH_FRAME32.uFileOffLo], 0
    jne .seek_pos
    cmp byte [ebx + BATCH_FRAME32.bEncoding], 0
    jne .seek_pos

    lea edx, [ebp + 524]
    mov dword [edx], 0
    push 0
    push edx
    push 4
    push ebp
    push dword [ebp + 520]
    call dword [__imp__ReadFile@20]
    mov ecx, dword [ebp + 524]
    mov byte [ebx + BATCH_FRAME32.bEncoding], 1 ; 1 = Byte/UTF-8/OEM
    cmp ecx, 2
    jb .seek_pos

    cmp byte [ebp], 0xFF
    jne .chk_be_bom
    cmp byte [ebp + 1], 0xFE
    jne .chk_be_bom
    mov byte [ebx + BATCH_FRAME32.bEncoding], 2 ; 2 = UTF-16LE
    mov dword [ebx + BATCH_FRAME32.uFileOffLo], 2
    jmp .seek_pos

.chk_be_bom:
    cmp byte [ebp], 0xFE
    jne .chk_u8_bom
    cmp byte [ebp + 1], 0xFF
    jne .chk_u8_bom
    mov byte [ebx + BATCH_FRAME32.bEncoding], 3 ; 3 = UTF-16BE
    mov dword [ebx + BATCH_FRAME32.uFileOffLo], 2
    jmp .seek_pos

.chk_u8_bom:
    cmp ecx, 3
    jb .seek_pos
    cmp byte [ebp], 0xEF
    jne .seek_pos
    cmp byte [ebp + 1], 0xBB
    jne .seek_pos
    cmp byte [ebp + 2], 0xBF
    jne .seek_pos
    mov dword [ebx + BATCH_FRAME32.uFileOffLo], 3

.seek_pos:
    push FILE_BEGIN
    push 0
    push dword [ebx + BATCH_FRAME32.uFileOffLo]
    push dword [ebp + 520]
    call dword [__imp__SetFilePointer@16]
    cmp eax, -1
    je .close_eof

    cmp byte [ebx + BATCH_FRAME32.bEncoding], 2
    jae .read_utf16

    ; --- Byte / OEM / CP65001 UTF-8 physical line reader ---
    xor esi, esi ; output byte count
.byte_chunk:
    lea edx, [ebp + 524]
    mov dword [edx], 0
    push 0
    push edx
    push 512
    push ebp
    push dword [ebp + 520]
    call dword [__imp__ReadFile@20]
    mov ecx, dword [ebp + 524]
    test eax, eax
    jz .byte_eof_chk
    test ecx, ecx
    jz .byte_eof_chk

    xor edx, edx ; chunk index
.byte_scan:
    cmp edx, ecx
    jae .byte_chunk
    mov al, byte [edx + ebp]
    cmp al, 0x1A ; Ctrl+Z EOF
    je .hit_ctrlz
    inc edx
    inc dword [ebx + BATCH_FRAME32.uFileOffLo]
    cmp al, 0x0A
    je .close_ok
    cmp al, 0x0D
    je .check_lf_after_cr
    mov eax, dword [ebp + 516]
    dec eax
    cmp esi, eax
    jae .byte_scan
    mov al, byte [ebp + edx - 1]
    mov byte [edi + esi], al
    inc esi
    jmp .byte_scan

.check_lf_after_cr:
    cmp edx, ecx
    jb .peek_buf_lf
    ; Read 1 extra byte to see if LF follows CR across 512B boundary
    push ecx
    lea eax, [ebp + 524]
    mov dword [eax], 0
    push 0
    push eax
    push 1
    push ebp
    push dword [ebp + 520]
    call dword [__imp__ReadFile@20]
    pop ecx
    cmp dword [ebp + 524], 1
    jne .close_ok
    cmp byte [ebp], 0x0A
    jne .close_ok
    inc dword [ebx + BATCH_FRAME32.uFileOffLo]
    jmp .close_ok

.peek_buf_lf:
    cmp byte [edx + ebp], 0x0A
    jne .close_ok
    inc dword [ebx + BATCH_FRAME32.uFileOffLo]
    jmp .close_ok

.hit_ctrlz:
    ; Advance uFileOffLo past file end so subsequent reads stay at EOF
    add dword [ebx + BATCH_FRAME32.uFileOffLo], ecx
    test esi, esi
    jz .close_eof
    jmp .close_ok

.byte_eof_chk:
    test esi, esi
    jz .close_eof
    jmp .close_ok

.read_utf16:
    ; --- UTF-16LE / UTF-16BE physical line reader tracking exact 2-byte WCHAR offsets ---
    call arena_mark32
    push eax
    mov ecx, 8196
    call arena_alloc32
    jc .pop_close_eof
    mov esi, eax ; WCHAR accum
    xor ecx, ecx ; WCHAR count
    mov dword [ebp + 512], ecx

.u16_lp:
    lea edx, [ebp + 524]
    mov dword [edx], 0
    push 0
    push edx
    push 2
    push ebp
    push dword [ebp + 520]
    call dword [__imp__ReadFile@20]
    cmp dword [ebp + 524], 2
    jb .u16_eof

    movzx eax, word [ebp]
    cmp byte [ebx + BATCH_FRAME32.bEncoding], 3
    jne .u16_got_w
    xchg al, ah
.u16_got_w:
    cmp ax, 0x001A
    je .u16_eof
    add dword [ebx + BATCH_FRAME32.uFileOffLo], 2
    cmp ax, 0x000A
    je .u16_done
    cmp ax, 0x000D
    je .u16_cr
    mov ecx, dword [ebp + 512]
    cmp ecx, 4090
    jae .u16_lp
    mov word [esi + ecx * 2], ax
    inc ecx
    mov dword [ebp + 512], ecx
    jmp .u16_lp

.u16_cr:
    lea edx, [ebp + 524]
    mov dword [edx], 0
    push 0
    push edx
    push 2
    push ebp
    push dword [ebp + 520]
    call dword [__imp__ReadFile@20]
    cmp dword [ebp + 524], 2
    jb .u16_done
    movzx eax, word [ebp]
    cmp byte [ebx + BATCH_FRAME32.bEncoding], 3
    jne .u16_chk_lf
    xchg al, ah
.u16_chk_lf:
    cmp ax, 0x000A
    jne .u16_done
    add dword [ebx + BATCH_FRAME32.uFileOffLo], 2
    jmp .u16_done

.u16_eof:
    cmp dword [ebp + 512], 0
    je .pop_close_eof

.u16_done:
    mov ecx, dword [ebp + 512]
    mov edx, dword [ebp + 516]
    call utf16le_to_wtf8_buf
    mov esi, eax
    pop eax
    call arena_release32
    jmp .close_ok_no_nul

.pop_close_eof:
    pop eax
    call arena_release32
.close_eof:
    push dword [ebp + 520]
    call dword [__imp__CloseHandle@4]
.eof:
    mov byte [edi], 0
    xor eax, eax
    add esp, 528
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret

.close_ok:
    mov byte [edi + esi], 0
.close_ok_no_nul:
    push dword [ebp + 520]
    call dword [__imp__CloseHandle@4]
    mov eax, esi
    add esp, 528
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret
@endproc

;; @proc batch_read_logical_line32
;; @abi win32
;; @in EBX = BATCH_FRAME32*, EDI = destination WTF-8 buffer, ECX = max byte capacity
;; @out EAX = logical line byte length, CF=0; or CF=1 on EOF
;; @err CF=1 on EOF
;; @clobbers eax, ecx, edx
;; @desc Reads a logical batch line, folding trailing unescaped '^' continuations in NT_CMD mode.
@proc batch_read_logical_line32
    push ebx
    push esi
    push edi
    push ebp
    mov ebp, ecx
    xor esi, esi ; current total len

.more_lp:
    mov ecx, ebp
    sub ecx, esi
    cmp ecx, 4
    jbe .done_ok
    push edi
    add edi, esi
    call batch_read_raw_line32
    pop edi
    jc .on_eof

    add esi, eax
    cmp byte [g_extensions_on], 0
    je .done_ok
    test esi, esi
    jz .done_ok

    ; Count consecutive trailing '^' characters: odd count folds next physical line!
    mov ecx, esi
    xor edx, edx
.cnt_caret:
    test ecx, ecx
    jz .chk_odd
    cmp byte [edi + ecx - 1], '^'
    jne .chk_odd
    inc edx
    dec ecx
    jmp .cnt_caret

.chk_odd:
    test edx, 1
    jz .done_ok
    ; Strip single trailing '^' and append next physical line (even if empty!)
    dec esi
    mov byte [edi + esi], 0
    mov ecx, ebp
    sub ecx, esi
    push edi
    add edi, esi
    call batch_read_raw_line32
    pop edi
    jc .done_ok
    add esi, eax
    jmp .chk_again

.chk_again:
    ; Allow multi-line ^ chains
    test eax, eax
    jz .done_ok
    mov ecx, esi
    xor edx, edx
    jmp .cnt_caret

.on_eof:
    test esi, esi
    jz .ret_eof

.done_ok:
    mov eax, esi
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.ret_eof:
    xor eax, eax
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc
