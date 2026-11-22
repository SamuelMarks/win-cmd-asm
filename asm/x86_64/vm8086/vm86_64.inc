;; @file vm86_64.inc
;; @isa x64
;; @desc Embedded x86-64 vm8086 .COM & DOS MZ .EXE Virtual Executor (1 MiB + 64 KiB RAM + 64-Bit INT 21h Bridge).

@cpu x64
@format inc

;; @const VM86_MEM_BYTES64
;; @desc 1 MiB + 64 KiB (1,114,112 bytes) isolated virtual 8086 HMA-safe address space.
@const VM86_MEM_BYTES64 = 0x00110000

;; @const VM86_MAX_HANDLES64
;; @desc Maximum mapped DOS file handles (0..19) per vm8086 guest invocation.
@const VM86_MAX_HANDLES64 = 20

;; @struc VM86_CTX64
;; @desc 256-byte self-contained x86-64 virtual 8086 CPU + DOS PSP/DTA/64-bit-handle state block.
@struc VM86_CTX64, 256
    @field pMem, qword, 1
    @field regs16, word, 8
    @field sregs16, word, 4
    @field regIp, word, 1
    @field regFlags, word, 1
    @field wPspSeg, word, 1
    @field wDtaSeg, word, 1
    @field wDtaOff, word, 1
    @field bHalted, byte, 1
    @field bExitCode, byte, 1
    @field bSegOvr, byte, 1
    @field bRepMode, byte, 1
    @field wAlignPad, word, 1
    @field dwStepsLeft, dword, 1
    @field dwPad0, dword, 1
    @field hFindGuest, qword, 1
    @field hostHandles, qword, 20
    @field qwPad, qword, 4
@endstruc

;; @proc vm86_lin64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DX = 16-bit segment, R8W = 16-bit offset
;; @out RAX = 64-bit host pointer inside [pMem .. pMem + 0x10FFFF]
;; @err none
;; @clobbers rax, r9
;; @desc Computes 20-bit + HMA linear guest pointer (seg * 16 + off) inside VM86_CTX64.pMem.
@proc vm86_lin64
    movzx eax, dx
    shl eax, 4
    movzx r9d, r8w
    add eax, r9d
    cmp eax, VM86_MEM_BYTES64 - 4
    jbe .ok
    and eax, 0x000FFFFF
.ok:
    add rax, qword [rcx + VM86_CTX64.pMem]
    ret
@endproc

;; @proc vm86_eff_seg64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DL = default sreg index (3=DS, 2=SS, 0=ES)
;; @out AX = effective 16-bit segment honoring active segment override prefix
;; @err none
;; @clobbers rax, rdx
;; @desc Resolves active segment override (ES=0, CS=1, SS=2, DS=3) or default segment register.
@proc vm86_eff_seg64
    mov al, byte [rcx + VM86_CTX64.bSegOvr]
    cmp al, 0xFF
    je .def
    mov dl, al
.def:
    movzx edx, dl
    mov ax, word [rcx + VM86_CTX64.sregs16 + rdx * 2]
    ret
@endproc

;; @proc vm86_fetch8_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*
;; @out AL = byte at CS:IP, increments IP
;; @err none
;; @clobbers rax, rdx, r8, r9
;; @desc Fetches one instruction byte at CS:IP without modifying RCX.
@proc vm86_fetch8_64
    mov dx, word [rcx + VM86_CTX64.sregs16 + 2]
    mov r8w, word [rcx + VM86_CTX64.regIp]
    inc word [rcx + VM86_CTX64.regIp]
    movzx eax, dx
    shl eax, 4
    movzx r9d, r8w
    add eax, r9d
    cmp eax, VM86_MEM_BYTES64 - 4
    jbe .ok
    and eax, 0x000FFFFF
.ok:
    add rax, qword [rcx + VM86_CTX64.pMem]
    movzx eax, byte [rax]
    ret
@endproc

;; @proc vm86_fetch16_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*
;; @out AX = 16-bit word at CS:IP, advances IP by 2
;; @err none
;; @clobbers rax, rdx, r8, r9, r10
;; @desc Fetches a 16-bit little-endian immediate/displacement at CS:IP.
@proc_frame vm86_fetch16_64, 40
    call vm86_fetch8_64
    mov r10d, eax
    call vm86_fetch8_64
    shl eax, 8
    or eax, r10d
    @epilog_ret
@endproc

;; @proc vm86_push16_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DX = 16-bit word to push
;; @out Decrements guest SP by 2 and writes DX at SS:SP
;; @err none
;; @clobbers rax, rdx, r8, r9, r10
;; @desc Pushes a 16-bit word onto the virtual 8086 SS:SP stack.
@proc_frame vm86_push16_64, 40
    mov r10d, edx
    mov r8w, word [rcx + VM86_CTX64.regs16 + 8]
    sub r8w, 2
    mov word [rcx + VM86_CTX64.regs16 + 8], r8w
    mov dx, word [rcx + VM86_CTX64.sregs16 + 4]
    call vm86_lin64
    mov word [rax], r10w
    @epilog_ret
@endproc

;; @proc vm86_pop16_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*
;; @out AX = 16-bit word popped from SS:SP, increments guest SP by 2
;; @err none
;; @clobbers rax, rdx, r8, r9
;; @desc Pops a 16-bit word from the virtual 8086 SS:SP stack.
@proc_frame vm86_pop16_64, 40
    mov r8w, word [rcx + VM86_CTX64.regs16 + 8]
    mov dx, word [rcx + VM86_CTX64.sregs16 + 4]
    add word [rcx + VM86_CTX64.regs16 + 8], 2
    call vm86_lin64
    movzx eax, word [rax]
    @epilog_ret
@endproc

;; @proc vm86_set_szp8_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DL = 8-bit result
;; @out Updates ZF (0x40), SF (0x80), and PF (0x04) in regFlags
;; @err none
;; @clobbers rax, r8
;; @desc Updates 8086 Zero, Sign, and Parity flags from an 8-bit result.
@proc vm86_set_szp8_64
    mov ax, word [rcx + VM86_CTX64.regFlags]
    and ax, 0xFF3B
    test dl, dl
    setz r8b
    movzx r8d, r8b
    shl r8d, 6
    or eax, r8d
    test dl, dl
    sets r8b
    movzx r8d, r8b
    shl r8d, 7
    or eax, r8d
    test dl, dl
    setp r8b
    movzx r8d, r8b
    shl r8d, 2
    or eax, r8d
    mov word [rcx + VM86_CTX64.regFlags], ax
    ret
@endproc

;; @proc vm86_set_szp16_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DX = 16-bit result
;; @out Updates ZF (0x40), SF (0x80), and PF (0x04) in regFlags
;; @err none
;; @clobbers rax, r8
;; @desc Updates 8086 Zero, Sign, and Parity flags from a 16-bit result.
@proc vm86_set_szp16_64
    mov ax, word [rcx + VM86_CTX64.regFlags]
    and ax, 0xFF3B
    test dx, dx
    setz r8b
    movzx r8d, r8b
    shl r8d, 6
    or eax, r8d
    test dx, dx
    sets r8b
    movzx r8d, r8b
    shl r8d, 7
    or eax, r8d
    test dl, dl
    setp r8b
    movzx r8d, r8b
    shl r8d, 2
    or eax, r8d
    mov word [rcx + VM86_CTX64.regFlags], ax
    ret
@endproc

;; @proc vm86_alu16_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DL = alu_op (0..7), R8W = dst, R9W = src, R10B = is_word (0/1)
;; @out AX = result, updates CF/PF/ZF/SF/OF in regFlags without touching RSP!
;; @err none
;; @clobbers rax, rdx, r8, r9, r10, r11
;; @desc Executes an 8-bit or 16-bit 8086 ALU operation capturing exact host flags via setcc (100% Nocona/SEH safe).
@proc vm86_alu16_64
    movzx r11d, word [rcx + VM86_CTX64.regFlags]
    mov eax, r8d
    test r10b, r10b
    jnz .do16

    cmp dl, 0
    je .add8
    cmp dl, 1
    je .or8
    cmp dl, 2
    je .adc8
    cmp dl, 3
    je .sbb8
    cmp dl, 4
    je .and8
    cmp dl, 5
    je .sub8
    cmp dl, 6
    je .xor8
    cmp al, r9b
    jmp .cap_flags

.add8:
    add al, r9b
    jmp .cap_flags
.or8:
    or al, r9b
    jmp .cap_flags
.adc8:
    bt r11d, 0
    adc al, r9b
    jmp .cap_flags
.sbb8:
    bt r11d, 0
    sbb al, r9b
    jmp .cap_flags
.and8:
    and al, r9b
    jmp .cap_flags
.sub8:
    sub al, r9b
    jmp .cap_flags
.xor8:
    xor al, r9b
    jmp .cap_flags

.do16:
    cmp dl, 0
    je .add16
    cmp dl, 1
    je .or16
    cmp dl, 2
    je .adc16
    cmp dl, 3
    je .sbb16
    cmp dl, 4
    je .and16
    cmp dl, 5
    je .sub16
    cmp dl, 6
    je .xor16
    cmp ax, r9w
    jmp .cap_flags

.add16:
    add ax, r9w
    jmp .cap_flags
.or16:
    or ax, r9w
    jmp .cap_flags
.adc16:
    bt r11d, 0
    adc ax, r9w
    jmp .cap_flags
.sbb16:
    bt r11d, 0
    sbb ax, r9w
    jmp .cap_flags
.and16:
    and ax, r9w
    jmp .cap_flags
.sub16:
    sub ax, r9w
    jmp .cap_flags
.xor16:
    xor ax, r9w

.cap_flags:
    ; Capture CF(0), PF(2), ZF(6), SF(7), OF(11) without clobbering EFLAGS before reading all 5!
    setc dl
    setp r8b
    setz r9b
    sets r10b
    movzx edx, dl
    movzx r8d, r8b
    movzx r9d, r9b
    movzx r10d, r10b
    jno .no_of
    or edx, 0x0800
.no_of:
    shl r8d, 2
    shl r9d, 6
    shl r10d, 7
    and r11d, 0x0600 ; keep DF | IF
    or r11d, edx
    or r11d, r8d
    or r11d, r9d
    or r11d, r10d
    or r11d, 0x0002
    mov word [rcx + VM86_CTX64.regFlags], r11w
    ret
@endproc

;; @proc vm86_reg_ptr64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, EDX = reg index (0..7), R8B = is_word (0/1)
;; @out RAX = 64-bit host pointer to 8-bit or 16-bit guest register inside VM86_CTX64
;; @err none
;; @clobbers rax, rdx
;; @desc Resolves host address of guest general register EDX (8-bit AL..BH or 16-bit AX..DI).
@proc vm86_reg_ptr64
    and edx, 7
    test r8b, r8b
    jnz .w16
    cmp edx, 4
    jae .hi8
    lea rax, [rcx + VM86_CTX64.regs16 + rdx * 2]
    ret
.hi8:
    sub edx, 4
    lea rax, [rcx + VM86_CTX64.regs16 + rdx * 2 + 1]
    ret
.w16:
    lea rax, [rcx + VM86_CTX64.regs16 + rdx * 2]
    ret
@endproc

;; @proc vm86_decode_modrm64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DL = modrm byte, R8B = is_word (0/1)
;; @out RAX = 64-bit host pointer to r/m8 or r/m16, EDX = reg field (0..7)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Resolves 16-bit 8086 ModR/M effective address inside VM86_CTX64 or guest RAM.
@proc_frame vm86_decode_modrm64, 48, rbx, rsi, rdi, r12, r13
    mov rbx, rcx ; ctx
    movzx esi, dl ; modrm
    movzx r12d, r8b ; is_word

    mov eax, esi
    shr eax, 6
    cmp eax, 3
    je .is_reg

    mov edi, eax ; mod (0..2)
    mov ecx, esi
    and ecx, 7 ; r/m
    mov r13d, 3 ; default DS
    xor r8d, r8d ; off16
    cmp ecx, 0
    je .rm0
    cmp ecx, 1
    je .rm1
    cmp ecx, 2
    je .rm2
    cmp ecx, 3
    je .rm3
    cmp ecx, 4
    je .rm4
    cmp ecx, 5
    je .rm5
    cmp ecx, 6
    je .rm6
    mov r8w, word [rbx + VM86_CTX64.regs16 + 6]
    jmp .disp
.rm0:
    mov r8w, word [rbx + VM86_CTX64.regs16 + 6]
    add r8w, word [rbx + VM86_CTX64.regs16 + 12]
    jmp .disp
.rm1:
    mov r8w, word [rbx + VM86_CTX64.regs16 + 6]
    add r8w, word [rbx + VM86_CTX64.regs16 + 14]
    jmp .disp
.rm2:
    mov r13d, 2
    mov r8w, word [rbx + VM86_CTX64.regs16 + 10]
    add r8w, word [rbx + VM86_CTX64.regs16 + 12]
    jmp .disp
.rm3:
    mov r13d, 2
    mov r8w, word [rbx + VM86_CTX64.regs16 + 10]
    add r8w, word [rbx + VM86_CTX64.regs16 + 14]
    jmp .disp
.rm4:
    mov r8w, word [rbx + VM86_CTX64.regs16 + 12]
    jmp .disp
.rm5:
    mov r8w, word [rbx + VM86_CTX64.regs16 + 14]
    jmp .disp
.rm6:
    cmp edi, 0
    je .disp16_direct
    mov r13d, 2
    mov r8w, word [rbx + VM86_CTX64.regs16 + 10]
    jmp .disp

.disp16_direct:
    mov rcx, rbx
    call vm86_fetch16_64
    movzx r12d, ax
    mov rcx, rbx
    mov dl, r13b
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, r12w
    mov rcx, rbx
    call vm86_lin64
    jmp .ret_reg_fld

.disp:
    mov r12d, r8d
    cmp edi, 1
    je .disp8
    cmp edi, 2
    je .disp16
    jmp .lin_ea
.disp8:
    mov rcx, rbx
    call vm86_fetch8_64
    movsx eax, al
    add r12w, ax
    jmp .lin_ea
.disp16:
    mov rcx, rbx
    call vm86_fetch16_64
    add r12w, ax
.lin_ea:
    mov rcx, rbx
    mov dl, r13b
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, r12w
    mov rcx, rbx
    call vm86_lin64
    jmp .ret_reg_fld

.is_reg:
    mov rcx, rbx
    mov edx, esi
    mov r8b, r12b
    call vm86_reg_ptr64

.ret_reg_fld:
    mov edx, esi
    shr edx, 3
    and edx, 7
    @epilog_ret
@endproc

;; @proc vm86_dos_int21_64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*
;; @out Bridges DOS INT 21h syscalls onto Win64 console, environment, time, memory, and VFS
;; @err Sets guest CF (bit 0 of regFlags) and AX = ERR_* on DOS syscall error
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Emulates MS-DOS INT 21h for 16-bit .COM and MZ .EXE guest programs inside 64-bit WINCMD64.EXE!
@proc_frame vm86_dos_int21_64, 344, rbp, rbx, rsi, rdi, r12, r13
    ; 6 saved regs (even) -> 344 = 40 + 19*16
    mov rbp, rcx
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    movzx eax, byte [rbp + VM86_CTX64.regs16 + 1] ; AH

    cmp al, 0x00
    je .exit_00
    cmp al, 0x4C
    je .exit_4c
    cmp al, 0x02
    je .ch_out
    cmp al, 0x06
    je .raw_io
    cmp al, 0x09
    je .prt_dollar
    cmp al, 0x19
    je .get_drv
    cmp al, 0x1A
    je .set_dta
    cmp al, 0x25
    je .done
    cmp al, 0x2A
    je .get_date
    cmp al, 0x2C
    je .get_time
    cmp al, 0x2F
    je .get_dta
    cmp al, 0x30
    je .dos_ver
    cmp al, 0x35
    je .get_vec
    cmp al, 0x39
    je .mkdir_g
    cmp al, 0x3A
    je .rmdir_g
    cmp al, 0x3C
    je .creat_h
    cmp al, 0x3D
    je .open_h
    cmp al, 0x3E
    je .close_h
    cmp al, 0x3F
    je .read_h
    cmp al, 0x40
    je .write_h
    cmp al, 0x41
    je .unlink_g
    cmp al, 0x42
    je .lseek_h
    cmp al, 0x43
    je .attr_h
    cmp al, 0x44
    je .ioctl_h
    cmp al, 0x47
    je .getcwd_h
    cmp al, 0x48
    je .alloc_mem
    cmp al, 0x49
    je .done
    cmp al, 0x4A
    je .done
    cmp al, 0x51
    je .get_psp
    cmp al, 0x56
    je .rename_g
    cmp al, 0x5E
    je .netbios_name
    cmp al, 0x60
    je .truename_h
    cmp al, 0x62
    je .get_psp

    or word [rbp + VM86_CTX64.regFlags], 1
    mov word [rbp + VM86_CTX64.regs16], ERR_INVALID_FUNCTION
    jmp .done

.exit_00:
    mov byte [rbp + VM86_CTX64.bExitCode], 0
    mov byte [rbp + VM86_CTX64.bHalted], 1
    jmp .done

.exit_4c:
    mov al, byte [rbp + VM86_CTX64.regs16]
    mov byte [rbp + VM86_CTX64.bExitCode], al
    mov byte [rbp + VM86_CTX64.bHalted], 1
    jmp .done

.ch_out:
    mov al, byte [rbp + VM86_CTX64.regs16 + 4]
    mov byte [rsp + 48], al
    lea rcx, [rsp + 48]
    mov edx, 1
    call con_write_out64
    jmp .done

.raw_io:
    mov al, byte [rbp + VM86_CTX64.regs16 + 4]
    cmp al, 0xFF
    jne .ch_out
    or word [rbp + VM86_CTX64.regFlags], 0x0040
    mov byte [rbp + VM86_CTX64.regs16], 0
    jmp .done

.prt_dollar:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rsi, rax
    xor edx, edx
.dlr_lp:
    cmp edx, 16384
    jae .dlr_out
    cmp byte [rsi + rdx], '$'
    je .dlr_out
    inc edx
    jmp .dlr_lp
.dlr_out:
    mov rcx, rsi
    call con_write_out64
    jmp .done

.get_drv:
    lea rcx, [rsp + 64]
    mov edx, 260
    call hal_get_cwd64
    jc .drv_c
    mov al, byte [rsp + 64]
    call ascii_upper_al64
    sub al, 'A'
    mov byte [rbp + VM86_CTX64.regs16], al
    jmp .done
.drv_c:
    mov byte [rbp + VM86_CTX64.regs16], 2
    jmp .done

.set_dta:
    mov ax, word [rbp + VM86_CTX64.sregs16 + 6]
    mov word [rbp + VM86_CTX64.wDtaSeg], ax
    mov ax, word [rbp + VM86_CTX64.regs16 + 4]
    mov word [rbp + VM86_CTX64.wDtaOff], ax
    jmp .done

.get_dta:
    mov ax, word [rbp + VM86_CTX64.wDtaSeg]
    mov word [rbp + VM86_CTX64.sregs16], ax
    mov ax, word [rbp + VM86_CTX64.wDtaOff]
    mov word [rbp + VM86_CTX64.regs16 + 6], ax
    jmp .done

.dos_ver:
    mov word [rbp + VM86_CTX64.regs16], 0x0005
    mov word [rbp + VM86_CTX64.regs16 + 6], 0xFF00
    mov word [rbp + VM86_CTX64.regs16 + 2], 0
    jmp .done

.get_vec:
    mov word [rbp + VM86_CTX64.sregs16], 0xF000
    mov word [rbp + VM86_CTX64.regs16 + 6], 0x0000
    jmp .done

.get_psp:
    mov ax, word [rbp + VM86_CTX64.wPspSeg]
    mov word [rbp + VM86_CTX64.regs16 + 6], ax
    jmp .done

.get_date:
    lea rbx, [rsp + 48]
    mov rcx, rbx
    call qword [rel __imp_GetLocalTime]
    mov ax, word [rbx + SYSTEMTIME16.wYear]
    mov word [rbp + VM86_CTX64.regs16 + 2], ax
    movzx eax, byte [rbx + SYSTEMTIME16.wMonth]
    shl eax, 8
    mov al, byte [rbx + SYSTEMTIME16.wDay]
    mov word [rbp + VM86_CTX64.regs16 + 4], ax
    mov al, byte [rbx + SYSTEMTIME16.wDayOfWeek]
    mov byte [rbp + VM86_CTX64.regs16], al
    jmp .done

.get_time:
    lea rbx, [rsp + 48]
    mov rcx, rbx
    call qword [rel __imp_GetLocalTime]
    movzx eax, byte [rbx + SYSTEMTIME16.wHour]
    shl eax, 8
    mov al, byte [rbx + SYSTEMTIME16.wMinute]
    mov word [rbp + VM86_CTX64.regs16 + 2], ax
    movzx eax, word [rbx + SYSTEMTIME16.wMilliseconds]
    xor edx, edx
    mov ecx, 10
    div ecx
    movzx ecx, byte [rbx + SYSTEMTIME16.wSecond]
    shl ecx, 8
    or eax, ecx
    mov word [rbp + VM86_CTX64.regs16 + 4], ax
    jmp .done

.mkdir_g:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rcx, rax
    call hal_mkdir_one64
    jc .dos_err
    jmp .done

.rmdir_g:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rcx, rax
    call hal_rmdir_one64
    jc .dos_err
    jmp .done

.unlink_g:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rcx, rax
    xor edx, edx
    call hal_delete_file64
    jc .dos_err
    jmp .done

.rename_g:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rsi, rax
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    call vm86_lin64
    mov rcx, rsi
    mov rdx, rax
    xor r8d, r8d
    call hal_move_file64
    jc .dos_err
    jmp .done

.open_h:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rsi, rax
    mov al, byte [rbp + VM86_CTX64.regs16]
    and al, 3
    mov edx, GENERIC_READ
    cmp al, 0
    je .do_open_guest
    mov edx, GENERIC_WRITE
    cmp al, 1
    je .do_open_guest
    mov edx, GENERIC_READ + GENERIC_WRITE
.do_open_guest:
    mov rcx, rsi
    mov r8d, OPEN_EXISTING
    xor r9d, r9d
    call hal_open_file64
    jc .dos_err
    jmp .alloc_guest_fd

.creat_h:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rcx, rax
    mov edx, GENERIC_READ + GENERIC_WRITE
    mov r8d, CREATE_ALWAYS
    xor r9d, r9d
    call hal_open_file64
    jc .dos_err

.alloc_guest_fd:
    mov ebx, 5
.fd_lp:
    cmp ebx, VM86_MAX_HANDLES64
    jae .too_many_h
    cmp qword [rbx * 8 + rbp + VM86_CTX64.hostHandles], INVALID_HANDLE_VALUE
    je .got_fd
    inc ebx
    jmp .fd_lp
.got_fd:
    mov qword [rbx * 8 + rbp + VM86_CTX64.hostHandles], rax
    mov word [rbp + VM86_CTX64.regs16], bx
    jmp .done
.too_many_h:
    mov rcx, rax
    call qword [rel __imp_CloseHandle]
    mov eax, ERR_TOO_MANY_OPEN_FILES
    jmp .dos_err

.close_h:
    movzx ebx, word [rbp + VM86_CTX64.regs16 + 6]
    cmp ebx, 5
    jb .done
    cmp ebx, VM86_MAX_HANDLES64
    jae .bad_h
    mov rcx, qword [rbx * 8 + rbp + VM86_CTX64.hostHandles]
    cmp rcx, INVALID_HANDLE_VALUE
    je .bad_h
    call qword [rel __imp_CloseHandle]
    mov qword [rbx * 8 + rbp + VM86_CTX64.hostHandles], INVALID_HANDLE_VALUE
    jmp .done

.read_h:
    movzx ebx, word [rbp + VM86_CTX64.regs16 + 6]
    cmp ebx, VM86_MAX_HANDLES64
    jae .bad_h
    mov rdi, qword [rbx * 8 + rbp + VM86_CTX64.hostHandles]
    cmp rdi, INVALID_HANDLE_VALUE
    je .bad_h
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rsi, rax
    movzx r8d, word [rbp + VM86_CTX64.regs16 + 2]
    mov dword [rsp + 48], 0
    mov rcx, rdi
    mov rdx, rsi
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    test eax, eax
    jz .bad_h
    mov eax, dword [rsp + 48]
    mov word [rbp + VM86_CTX64.regs16], ax
    jmp .done

.write_h:
    movzx ebx, word [rbp + VM86_CTX64.regs16 + 6]
    cmp ebx, VM86_MAX_HANDLES64
    jae .bad_h
    mov rdi, qword [rbx * 8 + rbp + VM86_CTX64.hostHandles]
    cmp rdi, INVALID_HANDLE_VALUE
    je .bad_h
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rsi, rax
    movzx r12d, word [rbp + VM86_CTX64.regs16 + 2]
    cmp ebx, 2
    ja .write_raw_h
    mov rcx, rdi
    mov rdx, rsi
    mov r8, r12
    call con_write_handle64
    mov word [rbp + VM86_CTX64.regs16], r12w
    jmp .done
.write_raw_h:
    mov dword [rsp + 48], 0
    mov rcx, rdi
    mov rdx, rsi
    mov r8d, r12d
    lea r9, [rsp + 48]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]
    test eax, eax
    jz .bad_h
    mov eax, dword [rsp + 48]
    mov word [rbp + VM86_CTX64.regs16], ax
    jmp .done

.lseek_h:
    movzx ebx, word [rbp + VM86_CTX64.regs16 + 6]
    cmp ebx, VM86_MAX_HANDLES64
    jae .bad_h
    mov rdi, qword [rbx * 8 + rbp + VM86_CTX64.hostHandles]
    cmp rdi, INVALID_HANDLE_VALUE
    je .bad_h
    movzx eax, word [rbp + VM86_CTX64.regs16 + 2]
    shl eax, 16
    mov ax, word [rbp + VM86_CTX64.regs16 + 4]
    movsxd rdx, eax
    movzx r9d, byte [rbp + VM86_CTX64.regs16]
    mov qword [rsp + 48], 0
    mov rcx, rdi
    lea r8, [rsp + 48]
    call qword [rel __imp_SetFilePointerEx]
    test eax, eax
    jz .bad_h
    mov rax, qword [rsp + 48]
    mov word [rbp + VM86_CTX64.regs16], ax
    shr rax, 16
    mov word [rbp + VM86_CTX64.regs16 + 4], ax
    jmp .done

.attr_h:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rcx, rax
    cmp byte [rbp + VM86_CTX64.regs16], 0
    jne .set_attr_g
    call hal_get_attr64
    jc .dos_err
    and eax, 0x3F
    mov word [rbp + VM86_CTX64.regs16 + 2], ax
    jmp .done
.set_attr_g:
    movzx edx, word [rbp + VM86_CTX64.regs16 + 2]
    call hal_set_attr64
    jc .dos_err
    jmp .done

.ioctl_h:
    cmp byte [rbp + VM86_CTX64.regs16], 0
    jne .done
    mov word [rbp + VM86_CTX64.regs16 + 4], 0x0083
    jmp .done

.getcwd_h:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    call vm86_lin64
    mov rdi, rax
    lea rcx, [rsp + 64]
    mov edx, 256
    call hal_get_cwd64
    jc .dos_err
    lea rdx, [rsp + 64]
    cmp byte [rdx + 1], ':'
    jne .cp_cwd_g
    add rdx, 2
    cmp byte [rdx], 0x5C
    jne .cp_cwd_g
    inc rdx
.cp_cwd_g:
    mov rcx, rdi
    call str_copy64
    jmp .done

.alloc_mem:
    mov word [rbp + VM86_CTX64.regs16], 0x6000
    jmp .done

.netbios_name:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 4]
    call vm86_lin64
    mov rdi, rax
    mov r13, rax
    mov al, 0x20
    mov ecx, 15
    rep stosb
    mov byte [rdi], 0
    lea rcx, [rel szVarCompName64]
    call env_get64
    test rax, rax
    jz .nb_done
    xor ecx, ecx
.nb_cp:
    cmp ecx, 15
    jae .nb_done
    mov dl, byte [rax + rcx]
    test dl, dl
    jz .nb_done
    mov byte [r13 + rcx], dl
    inc ecx
    jmp .nb_cp
.nb_done:
    mov word [rbp + VM86_CTX64.regs16 + 2], 0x0100
    jmp .done

.truename_h:
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 6]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    call vm86_lin64
    mov rcx, rax
    call path_normalize64
    jc .dos_err
    mov rsi, rax
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    call vm86_lin64
    mov rcx, rax
    mov rdx, rsi
    call str_copy64
    jmp .done

.bad_h:
    mov eax, ERR_INVALID_HANDLE
.dos_err:
    or word [rbp + VM86_CTX64.regFlags], 1
    mov word [rbp + VM86_CTX64.regs16], ax

.done:
    @epilog_ret
@endproc
