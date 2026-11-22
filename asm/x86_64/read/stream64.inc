;; @file stream64.inc
;; @isa x64
;; @desc Stateless 64-bit SetFilePointerEx BatchFrameReader (UTF-8 BOM, UTF-16LE/BE BOM, 0x1A EOF, and ^ folding).

@cpu x64
@format inc

;; @proc batch_read_raw_line64
;; @abi win64_ms
;; @in RCX = BATCH_FRAME64*, RDX = destination WTF-8 buffer, R8 = max byte capacity
;; @out RAX = byte length, CF=0; or CF=1 on EOF (handle is ALWAYS closed before returning!)
;; @err CF=1 on EOF or unreadable batch file
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Opens batch file, detects BOM at offset 0, seeks via SetFilePointerEx, reads 1 physical line, updates uFileOff, and closes handle.
@proc_frame batch_read_raw_line64, 584, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 584 = 40 + 34*16 (8 + 64 + 584 = 656 = 41*16)
    ; [rsp + 32]      : ReadFile 5th arg (lpOverlapped = 0)
    ; [rsp + 48..559] : 512-byte chunk buffer (rbp)
    ; [rsp + 560]     : bytesRead (dword)
    ; [rsp + 568]     : scratch newPos (qword)
    mov rbx, rcx
    mov rdi, rdx
    mov r14, r8
    lea rbp, [rsp + 48]

    mov rcx, qword [rbx + BATCH_FRAME64.pBatchPath]
    mov edx, GENERIC_READ
    mov r8d, OPEN_EXISTING
    xor r9d, r9d
    call hal_open_file64
    jc .eof
    mov r12, rax ; hFile

    cmp qword [rbx + BATCH_FRAME64.uFileOff], 0
    jne .seek_pos
    cmp byte [rbx + BATCH_FRAME64.bEncoding], 0
    jne .seek_pos

    mov dword [rsp + 560], 0
    mov rcx, r12
    mov rdx, rbp
    mov r8d, 4
    lea r9, [rsp + 560]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    mov ecx, dword [rsp + 560]
    mov byte [rbx + BATCH_FRAME64.bEncoding], 1
    cmp ecx, 2
    jb .seek_pos

    cmp byte [rbp], 0xFF
    jne .chk_be_bom
    cmp byte [rbp + 1], 0xFE
    jne .chk_be_bom
    mov byte [rbx + BATCH_FRAME64.bEncoding], 2 ; UTF-16LE
    mov qword [rbx + BATCH_FRAME64.uFileOff], 2
    jmp .seek_pos

.chk_be_bom:
    cmp byte [rbp], 0xFE
    jne .chk_u8_bom
    cmp byte [rbp + 1], 0xFF
    jne .chk_u8_bom
    mov byte [rbx + BATCH_FRAME64.bEncoding], 3 ; UTF-16BE
    mov qword [rbx + BATCH_FRAME64.uFileOff], 2
    jmp .seek_pos

.chk_u8_bom:
    cmp ecx, 3
    jb .seek_pos
    cmp byte [rbp], 0xEF
    jne .seek_pos
    cmp byte [rbp + 1], 0xBB
    jne .seek_pos
    cmp byte [rbp + 2], 0xBF
    jne .seek_pos
    mov qword [rbx + BATCH_FRAME64.uFileOff], 3

.seek_pos:
    mov rcx, r12
    mov rdx, qword [rbx + BATCH_FRAME64.uFileOff]
    lea r8, [rsp + 568]
    mov r9d, FILE_BEGIN
    call qword [rel __imp_SetFilePointerEx]
    test eax, eax
    jz .close_eof

    cmp byte [rbx + BATCH_FRAME64.bEncoding], 2
    jae .read_utf16

    xor esi, esi ; output byte count
.byte_chunk:
    mov dword [rsp + 560], 0
    mov rcx, r12
    mov rdx, rbp
    mov r8d, 512
    lea r9, [rsp + 560]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    mov r13d, dword [rsp + 560]
    test eax, eax
    jz .byte_eof_chk
    test r13d, r13d
    jz .byte_eof_chk

    xor r15d, r15d
.byte_scan:
    cmp r15d, r13d
    jae .byte_chunk
    mov al, byte [r15 + rbp]
    cmp al, 0x1A
    je .hit_ctrlz
    inc r15d
    inc qword [rbx + BATCH_FRAME64.uFileOff]
    cmp al, 0x0A
    je .close_ok
    cmp al, 0x0D
    je .check_lf_after_cr
    lea rax, [r14 - 1]
    cmp rsi, rax
    jae .byte_scan
    mov al, byte [r15 + rbp - 1]
    mov byte [rdi + rsi], al
    inc rsi
    jmp .byte_scan

.check_lf_after_cr:
    cmp r15d, r13d
    jb .peek_buf_lf
    mov dword [rsp + 560], 0
    mov rcx, r12
    mov rdx, rbp
    mov r8d, 1
    lea r9, [rsp + 560]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    cmp dword [rsp + 560], 1
    jne .close_ok
    cmp byte [rbp], 0x0A
    jne .close_ok
    inc qword [rbx + BATCH_FRAME64.uFileOff]
    jmp .close_ok

.peek_buf_lf:
    cmp byte [r15 + rbp], 0x0A
    jne .close_ok
    inc qword [rbx + BATCH_FRAME64.uFileOff]
    jmp .close_ok

.hit_ctrlz:
    add qword [rbx + BATCH_FRAME64.uFileOff], r13
    test rsi, rsi
    jz .close_eof
    jmp .close_ok

.byte_eof_chk:
    test rsi, rsi
    jz .close_eof
    jmp .close_ok

.read_utf16:
    call arena_mark64
    mov r15, rax
    mov ecx, 8196
    call arena_alloc64
    jc .rel_close_eof
    mov rsi, rax ; WCHAR accum
    xor r13d, r13d ; WCHAR count

.u16_lp:
    mov dword [rsp + 560], 0
    mov rcx, r12
    mov rdx, rbp
    mov r8d, 2
    lea r9, [rsp + 560]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    cmp dword [rsp + 560], 2
    jb .u16_eof

    movzx eax, word [rbp]
    cmp byte [rbx + BATCH_FRAME64.bEncoding], 3
    jne .u16_got_w
    rol ax, 8
.u16_got_w:
    cmp ax, 0x001A
    je .u16_eof
    add qword [rbx + BATCH_FRAME64.uFileOff], 2
    cmp ax, 0x000A
    je .u16_done
    cmp ax, 0x000D
    je .u16_cr
    cmp r13d, 4090
    jae .u16_lp
    mov word [rsi + r13 * 2], ax
    inc r13d
    jmp .u16_lp

.u16_cr:
    mov dword [rsp + 560], 0
    mov rcx, r12
    mov rdx, rbp
    mov r8d, 2
    lea r9, [rsp + 560]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    cmp dword [rsp + 560], 2
    jb .u16_done
    movzx eax, word [rbp]
    cmp byte [rbx + BATCH_FRAME64.bEncoding], 3
    jne .u16_chk_lf
    rol ax, 8
.u16_chk_lf:
    cmp ax, 0x000A
    jne .u16_done
    add qword [rbx + BATCH_FRAME64.uFileOff], 2
    jmp .u16_done

.u16_eof:
    test r13d, r13d
    jz .rel_close_eof

.u16_done:
    mov rcx, rsi
    mov rdx, r13
    mov r8, rdi
    mov r9, r14
    call utf16le_to_wtf8_buf64
    mov rsi, rax
    mov rcx, r15
    call arena_release64
    jmp .close_ok_no_nul

.rel_close_eof:
    mov rcx, r15
    call arena_release64
.close_eof:
    mov rcx, r12
    call qword [rel __imp_CloseHandle]
.eof:
    mov byte [rdi], 0
    xor eax, eax
    stc
    @epilog_ret

.close_ok:
    mov byte [rdi + rsi], 0
.close_ok_no_nul:
    mov rcx, r12
    call qword [rel __imp_CloseHandle]
    mov rax, rsi
    clc
    @epilog_ret
@endproc

;; @proc batch_read_logical_line64
;; @abi win64_ms
;; @in RCX = BATCH_FRAME64*, RDX = destination WTF-8 buffer, R8 = max byte capacity
;; @out RAX = logical line byte length, CF=0; or CF=1 on EOF
;; @err CF=1 on EOF
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Reads a logical batch line, folding trailing unescaped '^' continuations in NT_CMD mode.
@proc_frame batch_read_logical_line64, 40, rbp, rbx, rsi, rdi
    mov rbx, rcx
    mov rdi, rdx
    mov rbp, r8
    xor esi, esi

    mov r8, rbp
    cmp r8, 4
    jbe .done_ok
    call batch_read_raw_line64
    jc .ret_eof

    add rsi, rax
    cmp byte [rel g_extensions_on], 0
    je .done_ok
    test rsi, rsi
    jz .done_ok

.chk_fold:
    mov rcx, rsi
    xor edx, edx
.cnt_caret:
    test rcx, rcx
    jz .chk_odd
    cmp byte [rdi + rcx - 1], '^'
    jne .chk_odd
    inc edx
    dec rcx
    jmp .cnt_caret

.chk_odd:
    test edx, 1
    jz .done_ok
    dec rsi
    mov byte [rdi + rsi], 0
    mov r8, rbp
    sub r8, rsi
    cmp r8, 4
    jbe .done_ok
    mov rcx, rbx
    lea rdx, [rdi + rsi]
    call batch_read_raw_line64
    jc .done_ok
    add rsi, rax
    test rax, rax
    jz .done_ok
    jmp .chk_fold

.done_ok:
    mov rax, rsi
    clc
    @epilog_ret

.ret_eof:
    xor eax, eax
    stc
    @epilog_ret
@endproc
