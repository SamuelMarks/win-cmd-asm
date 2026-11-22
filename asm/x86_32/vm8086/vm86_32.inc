;; @file vm86_32.inc
;; @isa 386
;; @desc Embedded IA-32 vm8086 .COM & DOS MZ .EXE Virtual Executor (1 MiB + 64 KiB RAM + INT 21h/10h/16h Bridge).

@cpu 386
@format inc

;; @const VM86_MEM_BYTES
;; @desc 1 MiB + 64 KiB (1,114,112 bytes) isolated virtual 8086 HMA-safe address space.
@const VM86_MEM_BYTES = 0x00110000

;; @const VM86_MAX_HANDLES
;; @desc Maximum mapped DOS file handles (0..19) per vm8086 guest invocation.
@const VM86_MAX_HANDLES = 20

;; @struc VM86_CTX32
;; @desc 192-byte self-contained virtual 8086 CPU + DOS PSP/DTA/handle state block.
@struc VM86_CTX32, 192
    @field pMem, dword, 1
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
    @field hFindGuest, dword, 1
    @field hostHandles, dword, 20
    @field dwPad, dword, 15
@endstruc

;; @proc vm86_lin32
;; @abi win32
;; @in EBP = VM86_CTX32*, AX = 16-bit segment, DX = 16-bit offset
;; @out EAX = host pointer inside [pMem .. pMem + 0x10FFFF]
;; @err none
;; @clobbers eax, ecx
;; @desc Computes 20-bit + HMA linear guest pointer (seg * 16 + off) inside VM86_CTX32.pMem.
@proc vm86_lin32
    movzx eax, ax
    shl eax, 4
    movzx ecx, dx
    add eax, ecx
    cmp eax, VM86_MEM_BYTES - 4
    jbe .ok
    and eax, 0x000FFFFF
.ok:
    add eax, dword [ebp + VM86_CTX32.pMem]
    ret
@endproc

;; @proc vm86_eff_seg32
;; @abi win32
;; @in EBP = VM86_CTX32*, CL = default sreg index (3=DS, 2=SS, 0=ES)
;; @out AX = effective 16-bit segment honoring active segment override prefix
;; @err none
;; @clobbers eax, ecx
;; @desc Resolves active segment override (ES=0, CS=1, SS=2, DS=3) or default segment register.
@proc vm86_eff_seg32
    mov al, byte [ebp + VM86_CTX32.bSegOvr]
    cmp al, 0xFF
    je .def
    mov cl, al
.def:
    movzx ecx, cl
    mov ax, word [ebp + VM86_CTX32.sregs16 + ecx * 2]
    ret
@endproc

;; @proc vm86_fetch8
;; @abi win32
;; @in EBP = VM86_CTX32*
;; @out AL = byte at CS:IP, increments IP
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Fetches one instruction byte at CS:IP.
@proc vm86_fetch8
    mov ax, word [ebp + VM86_CTX32.sregs16 + 2] ; CS (index 1)
    mov dx, word [ebp + VM86_CTX32.regIp]
    inc word [ebp + VM86_CTX32.regIp]
    call vm86_lin32
    movzx eax, byte [eax]
    ret
@endproc

;; @proc vm86_fetch16
;; @abi win32
;; @in EBP = VM86_CTX32*
;; @out AX = 16-bit word at CS:IP, advances IP by 2
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Fetches a 16-bit little-endian immediate/displacement at CS:IP.
@proc vm86_fetch16
    call vm86_fetch8
    push eax
    call vm86_fetch8
    pop ecx
    shl eax, 8
    or eax, ecx
    ret
@endproc

;; @proc vm86_push16
;; @abi win32
;; @in EBP = VM86_CTX32*, BX = 16-bit word to push
;; @out Decrements guest SP by 2 and writes BX at SS:SP
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Pushes a 16-bit word onto the virtual 8086 SS:SP stack.
@proc vm86_push16
    mov dx, word [ebp + VM86_CTX32.regs16 + 8] ; SP (index 4)
    sub dx, 2
    mov word [ebp + VM86_CTX32.regs16 + 8], dx
    mov ax, word [ebp + VM86_CTX32.sregs16 + 4] ; SS (index 2)
    call vm86_lin32
    mov word [eax], bx
    ret
@endproc

;; @proc vm86_pop16
;; @abi win32
;; @in EBP = VM86_CTX32*
;; @out AX = 16-bit word popped from SS:SP, increments guest SP by 2
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Pops a 16-bit word from the virtual 8086 SS:SP stack.
@proc vm86_pop16
    mov dx, word [ebp + VM86_CTX32.regs16 + 8]
    mov ax, word [ebp + VM86_CTX32.sregs16 + 4]
    add word [ebp + VM86_CTX32.regs16 + 8], 2
    call vm86_lin32
    movzx eax, word [eax]
    ret
@endproc

;; @proc vm86_set_szp8
;; @abi win32
;; @in EBP = VM86_CTX32*, AL = 8-bit result
;; @out Updates ZF (0x40) and SF (0x80) in regFlags
;; @err none
;; @clobbers ecx
;; @desc Updates 8086 Zero and Sign flags from an 8-bit result.
@proc vm86_set_szp8
    mov cx, word [ebp + VM86_CTX32.regFlags]
    and cx, 0xFF3B
    test al, al
    jnz .nz
    or cx, 0x0040
.nz:
    test al, 0x80
    jz .ns
    or cx, 0x0080
.ns:
    mov word [ebp + VM86_CTX32.regFlags], cx
    ret
@endproc

;; @proc vm86_set_szp16
;; @abi win32
;; @in EBP = VM86_CTX32*, AX = 16-bit result
;; @out Updates ZF (0x40) and SF (0x80) in regFlags
;; @err none
;; @clobbers ecx
;; @desc Updates 8086 Zero and Sign flags from a 16-bit result.
@proc vm86_set_szp16
    mov cx, word [ebp + VM86_CTX32.regFlags]
    and cx, 0xFF3B
    test ax, ax
    jnz .nz
    or cx, 0x0040
.nz:
    test ax, 0x8000
    jz .ns
    or cx, 0x0080
.ns:
    mov word [ebp + VM86_CTX32.regFlags], cx
    ret
@endproc

;; @proc vm86_alu16
;; @abi win32
;; @in EBP = VM86_CTX32*, CL = alu_op (0..7: ADD,OR,ADC,SBB,AND,SUB,XOR,CMP), AX = dst, DX = src, CH = is_word (0/1)
;; @out AX = result (unchanged when CMP=7!), updates CF/OF/SF/ZF in regFlags
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Executes an 8-bit or 16-bit 8086 ALU operation using real host i386 flags for exact CF/OF/SF/ZF parity!
@proc vm86_alu16
    push ebx
    mov bx, word [ebp + VM86_CTX32.regFlags]
    test ch, ch
    jnz .do16

    ; 8-bit ALU op
    cmp cl, 0
    je .add8
    cmp cl, 1
    je .or8
    cmp cl, 2
    je .adc8
    cmp cl, 3
    je .sbb8
    cmp cl, 4
    je .and8
    cmp cl, 5
    je .sub8
    cmp cl, 6
    je .xor8
    cmp al, dl
    pushfd
    pop ecx
    jmp .store_flags

.add8:
    add al, dl
    pushfd
    pop ecx
    jmp .store_flags
.or8:
    or al, dl
    pushfd
    pop ecx
    jmp .store_flags
.adc8:
    bt ebx, 0
    adc al, dl
    pushfd
    pop ecx
    jmp .store_flags
.sbb8:
    bt ebx, 0
    sbb al, dl
    pushfd
    pop ecx
    jmp .store_flags
.and8:
    and al, dl
    pushfd
    pop ecx
    jmp .store_flags
.sub8:
    sub al, dl
    pushfd
    pop ecx
    jmp .store_flags
.xor8:
    xor al, dl
    pushfd
    pop ecx
    jmp .store_flags

.do16:
    cmp cl, 0
    je .add16
    cmp cl, 1
    je .or16
    cmp cl, 2
    je .adc16
    cmp cl, 3
    je .sbb16
    cmp cl, 4
    je .and16
    cmp cl, 5
    je .sub16
    cmp cl, 6
    je .xor16
    cmp ax, dx
    pushfd
    pop ecx
    jmp .store_flags

.add16:
    add ax, dx
    pushfd
    pop ecx
    jmp .store_flags
.or16:
    or ax, dx
    pushfd
    pop ecx
    jmp .store_flags
.adc16:
    bt ebx, 0
    adc ax, dx
    pushfd
    pop ecx
    jmp .store_flags
.sbb16:
    bt ebx, 0
    sbb ax, dx
    pushfd
    pop ecx
    jmp .store_flags
.and16:
    and ax, dx
    pushfd
    pop ecx
    jmp .store_flags
.sub16:
    sub ax, dx
    pushfd
    pop ecx
    jmp .store_flags
.xor16:
    xor ax, dx
    pushfd
    pop ecx

.store_flags:
    ; Preserve DF (0x0400) and IF (0x0200), copy arithmetic flags 0x08D5 (OF,SF,ZF,AF,PF,CF) from host EFLAGS!
    and bx, 0x0600
    and cx, 0x08D5
    or bx, cx
    or bx, 0x0002
    mov word [ebp + VM86_CTX32.regFlags], bx
    pop ebx
    ret
@endproc

;; @proc vm86_decode_modrm32
;; @abi win32
;; @in EBP = VM86_CTX32*, AL = modrm byte, CH = is_word (0/1)
;; @out EAX = host pointer to r/m8 or r/m16 in VM86_CTX32 or guest RAM, ECX = reg field (0..7)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Resolves 16-bit 8086 ModR/M effective address (register or [BX+SI/DI], [BP+SI/DI], [SI], [DI], [BP], [BX]).
@proc vm86_decode_modrm32
    push ebx
    push esi
    push edi
    mov bl, al
    mov bh, ch ; is_word

    mov al, bl
    shr al, 6 ; mod (0..3)
    cmp al, 3
    je .is_reg

    ; Memory effective address
    mov cl, bl
    and cl, 7 ; r/m
    mov ch, 3 ; default DS
    xor dx, dx
    cmp cl, 0
    je .rm0
    cmp cl, 1
    je .rm1
    cmp cl, 2
    je .rm2
    cmp cl, 3
    je .rm3
    cmp cl, 4
    je .rm4
    cmp cl, 5
    je .rm5
    cmp cl, 6
    je .rm6
    mov dx, word [ebp + VM86_CTX32.regs16 + 6] ; [BX]
    jmp .disp
.rm0:
    mov dx, word [ebp + VM86_CTX32.regs16 + 6] ; BX
    add dx, word [ebp + VM86_CTX32.regs16 + 12] ; + SI
    jmp .disp
.rm1:
    mov dx, word [ebp + VM86_CTX32.regs16 + 6] ; BX
    add dx, word [ebp + VM86_CTX32.regs16 + 14] ; + DI
    jmp .disp
.rm2:
    mov ch, 2 ; SS
    mov dx, word [ebp + VM86_CTX32.regs16 + 10] ; BP
    add dx, word [ebp + VM86_CTX32.regs16 + 12] ; + SI
    jmp .disp
.rm3:
    mov ch, 2 ; SS
    mov dx, word [ebp + VM86_CTX32.regs16 + 10] ; BP
    add dx, word [ebp + VM86_CTX32.regs16 + 14] ; + DI
    jmp .disp
.rm4:
    mov dx, word [ebp + VM86_CTX32.regs16 + 12] ; SI
    jmp .disp
.rm5:
    mov dx, word [ebp + VM86_CTX32.regs16 + 14] ; DI
    jmp .disp
.rm6:
    cmp al, 0
    je .disp16_direct
    mov ch, 2 ; SS
    mov dx, word [ebp + VM86_CTX32.regs16 + 10] ; BP
    jmp .disp

.disp16_direct:
    push ecx
    call vm86_fetch16
    mov dx, ax
    pop ecx
    mov cl, ch
    push edx
    call vm86_eff_seg32
    pop edx
    call vm86_lin32
    mov edi, eax
    jmp .ret_reg_fld

.disp:
    cmp al, 1
    je .disp8
    cmp al, 2
    je .disp16
    jmp .lin_ea
.disp8:
    push ecx
    push edx
    call vm86_fetch8
    movsx eax, al
    pop edx
    pop ecx
    add dx, ax
    jmp .lin_ea
.disp16:
    push ecx
    push edx
    call vm86_fetch16
    pop edx
    pop ecx
    add dx, ax
.lin_ea:
    mov cl, ch
    push edx
    call vm86_eff_seg32
    pop edx
    call vm86_lin32
    mov edi, eax
    jmp .ret_reg_fld

.is_reg:
    movzx ecx, bl
    and ecx, 7
    test bh, bh
    jnz .reg16_ptr
    ; 8-bit register mapping: 0=AL,1=CL,2=DL,3=BL, 4=AH,5=CH,6=DH,7=BH
    cmp ecx, 4
    jae .reg8_hi
    lea edi, [ebp + VM86_CTX32.regs16 + ecx * 2]
    jmp .ret_reg_fld
.reg8_hi:
    sub ecx, 4
    lea edi, [ebp + VM86_CTX32.regs16 + ecx * 2 + 1]
    jmp .ret_reg_fld
.reg16_ptr:
    lea edi, [ebp + VM86_CTX32.regs16 + ecx * 2]

.ret_reg_fld:
    mov eax, edi
    movzx ecx, bl
    shr ecx, 3
    and ecx, 7
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc vm86_reg_ptr32
;; @abi win32
;; @in EBP = VM86_CTX32*, ECX = reg index (0..7), DL = is_word (0/1)
;; @out EAX = host pointer to 8-bit or 16-bit guest register inside VM86_CTX32
;; @err none
;; @clobbers eax, ecx
;; @desc Resolves host address of guest general register ECX (8-bit AL..BH or 16-bit AX..DI).
@proc vm86_reg_ptr32
    and ecx, 7
    test dl, dl
    jnz .w16
    cmp ecx, 4
    jae .hi8
    lea eax, [ebp + VM86_CTX32.regs16 + ecx * 2]
    ret
.hi8:
    sub ecx, 4
    lea eax, [ebp + VM86_CTX32.regs16 + ecx * 2 + 1]
    ret
.w16:
    lea eax, [ebp + VM86_CTX32.regs16 + ecx * 2]
    ret
@endproc

;; @proc vm86_dos_int21
;; @abi win32
;; @in EBP = VM86_CTX32*
;; @out Bridges DOS INT 21h syscalls onto Win32 console, environment, time, memory, and VFS
;; @err Sets guest CF (bit 0 of regFlags) and AX = ERR_* on DOS syscall error
;; @clobbers eax, ecx, edx
;; @desc Emulates MS-DOS INT 21h for 16-bit .COM and MZ .EXE guest programs inside WINCMD32.EXE.
@proc vm86_dos_int21
    push ebx
    push esi
    push edi

    ; Clear guest CF optimistically
    and word [ebp + VM86_CTX32.regFlags], 0xFFFE
    movzx eax, byte [ebp + VM86_CTX32.regs16 + 1] ; AH

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
    je .ignore_ok
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
    cmp al, 0x3D
    je .open_h
    cmp al, 0x3C
    je .creat_h
    cmp al, 0x3E
    je .close_h
    cmp al, 0x3F
    je .read_h
    cmp al, 0x40
    je .write_h
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
    je .ignore_ok
    cmp al, 0x4A
    je .ignore_ok
    cmp al, 0x5E
    je .netbios_name
    cmp al, 0x60
    je .truename_h
    cmp al, 0x62
    je .get_psp
    cmp al, 0x51
    je .get_psp

    ; Unsupported DOS function -> CF=1, AX=1
    or word [ebp + VM86_CTX32.regFlags], 1
    mov word [ebp + VM86_CTX32.regs16], ERR_INVALID_FUNCTION
    jmp .done

.exit_00:
    mov byte [ebp + VM86_CTX32.bExitCode], 0
    mov byte [ebp + VM86_CTX32.bHalted], 1
    jmp .done

.exit_4c:
    mov al, byte [ebp + VM86_CTX32.regs16]
    mov byte [ebp + VM86_CTX32.bExitCode], al
    mov byte [ebp + VM86_CTX32.bHalted], 1
    jmp .done

.ch_out:
    sub esp, 4
    mov al, byte [ebp + VM86_CTX32.regs16 + 4] ; DL
    mov byte [esp], al
    mov esi, esp
    mov ecx, 1
    call con_write_out32
    add esp, 4
    jmp .done

.raw_io:
    mov al, byte [ebp + VM86_CTX32.regs16 + 4] ; DL
    cmp al, 0xFF
    je .raw_in_empty
    jmp .ch_out
.raw_in_empty:
    or word [ebp + VM86_CTX32.regFlags], 0x0040 ; ZF=1 (no key waiting)
    mov byte [ebp + VM86_CTX32.regs16], 0
    jmp .done

.prt_dollar:
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov dx, word [ebp + VM86_CTX32.regs16 + 4]  ; DX
    call vm86_lin32
    mov esi, eax
    xor ecx, ecx
.dlr_lp:
    cmp ecx, 16384
    jae .dlr_out
    cmp byte [esi + ecx], '$'
    je .dlr_out
    inc ecx
    jmp .dlr_lp
.dlr_out:
    call con_write_out32
    jmp .done

.get_drv:
    mov ecx, 260
    call arena_alloc32
    jc .drv_c
    mov edi, eax
    mov ecx, 256
    call hal_get_cwd32
    jc .drv_c
    mov al, byte [edi]
    call ascii_upper_al
    sub al, 'A'
    mov byte [ebp + VM86_CTX32.regs16], al
    jmp .done
.drv_c:
    mov byte [ebp + VM86_CTX32.regs16], 2 ; C:
    jmp .done

.set_dta:
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov word [ebp + VM86_CTX32.wDtaSeg], ax
    mov ax, word [ebp + VM86_CTX32.regs16 + 4]  ; DX
    mov word [ebp + VM86_CTX32.wDtaOff], ax
    jmp .done

.get_dta:
    mov ax, word [ebp + VM86_CTX32.wDtaSeg]
    mov word [ebp + VM86_CTX32.sregs16], ax ; ES
    mov ax, word [ebp + VM86_CTX32.wDtaOff]
    mov word [ebp + VM86_CTX32.regs16 + 6], ax ; BX (+6)
    jmp .done

.dos_ver:
    mov word [ebp + VM86_CTX32.regs16], 0x0005 ; DOS 5.00
    mov word [ebp + VM86_CTX32.regs16 + 6], 0xFF00
    mov word [ebp + VM86_CTX32.regs16 + 2], 0
    jmp .done

.get_vec:
    mov word [ebp + VM86_CTX32.sregs16], 0xF000
    mov word [ebp + VM86_CTX32.regs16 + 6], 0x0000 ; BX (+6)
    jmp .done

.get_psp:
    mov ax, word [ebp + VM86_CTX32.wPspSeg]
    mov word [ebp + VM86_CTX32.regs16 + 6], ax ; BX (+6)
    jmp .done

.get_date:
    sub esp, 16
    mov ebx, esp
    push ebx
    call dword [__imp__GetLocalTime@4]
    mov ax, word [ebx + SYSTEMTIME16.wYear]
    mov word [ebp + VM86_CTX32.regs16 + 2], ax ; CX = year
    mov dh, byte [ebx + SYSTEMTIME16.wMonth]
    mov dl, byte [ebx + SYSTEMTIME16.wDay]
    mov word [ebp + VM86_CTX32.regs16 + 4], dx ; DH=month, DL=day
    mov al, byte [ebx + SYSTEMTIME16.wDayOfWeek]
    mov byte [ebp + VM86_CTX32.regs16], al
    add esp, 16
    jmp .done

.get_time:
    sub esp, 16
    mov ebx, esp
    push ebx
    call dword [__imp__GetLocalTime@4]
    mov ch, byte [ebx + SYSTEMTIME16.wHour]
    mov cl, byte [ebx + SYSTEMTIME16.wMinute]
    mov word [ebp + VM86_CTX32.regs16 + 2], cx
    mov dh, byte [ebx + SYSTEMTIME16.wSecond]
    movzx eax, word [ebx + SYSTEMTIME16.wMilliseconds]
    push edx
    xor edx, edx
    mov ecx, 10
    div ecx
    pop edx
    mov dl, al
    mov word [ebp + VM86_CTX32.regs16 + 4], dx
    add esp, 16
    jmp .done

.open_h:
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov dx, word [ebp + VM86_CTX32.regs16 + 4]  ; DX
    call vm86_lin32
    mov esi, eax
    mov al, byte [ebp + VM86_CTX32.regs16]
    and al, 3
    mov ecx, GENERIC_READ
    cmp al, 0
    je .do_open_guest
    mov ecx, GENERIC_WRITE
    cmp al, 1
    je .do_open_guest
    mov ecx, GENERIC_READ + GENERIC_WRITE
.do_open_guest:
    mov edx, OPEN_EXISTING
    xor eax, eax
    call hal_open_file32
    jc .dos_err
    jmp .alloc_guest_fd

.creat_h:
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6]
    mov dx, word [ebp + VM86_CTX32.regs16 + 4]
    call vm86_lin32
    mov esi, eax
    mov ecx, GENERIC_READ + GENERIC_WRITE
    mov edx, CREATE_ALWAYS
    xor eax, eax
    call hal_open_file32
    jc .dos_err

.alloc_guest_fd:
    mov ebx, 5
.fd_lp:
    cmp ebx, VM86_MAX_HANDLES
    jae .too_many_h
    cmp dword [ebp + VM86_CTX32.hostHandles + ebx * 4], INVALID_HANDLE_VALUE
    je .got_fd
    inc ebx
    jmp .fd_lp
.got_fd:
    mov dword [ebp + VM86_CTX32.hostHandles + ebx * 4], eax
    mov word [ebp + VM86_CTX32.regs16], bx
    jmp .done
.too_many_h:
    push eax
    call dword [__imp__CloseHandle@4]
    mov eax, ERR_TOO_MANY_OPEN_FILES
    jmp .dos_err

.close_h:
    movzx ebx, word [ebp + VM86_CTX32.regs16 + 6] ; BX (+6)
    cmp ebx, 5
    jb .done
    cmp ebx, VM86_MAX_HANDLES
    jae .bad_h
    mov eax, dword [ebp + VM86_CTX32.hostHandles + ebx * 4]
    cmp eax, INVALID_HANDLE_VALUE
    je .bad_h
    push eax
    call dword [__imp__CloseHandle@4]
    mov dword [ebp + VM86_CTX32.hostHandles + ebx * 4], INVALID_HANDLE_VALUE
    jmp .done

.read_h:
    movzx ebx, word [ebp + VM86_CTX32.regs16 + 6] ; BX (+6)
    cmp ebx, VM86_MAX_HANDLES
    jae .bad_h
    mov edi, dword [ebp + VM86_CTX32.hostHandles + ebx * 4]
    cmp edi, INVALID_HANDLE_VALUE
    je .bad_h
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov dx, word [ebp + VM86_CTX32.regs16 + 4]  ; DX (+4)
    call vm86_lin32
    mov esi, eax
    movzx ecx, word [ebp + VM86_CTX32.regs16 + 2] ; CX (+2)
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push 0
    push edx
    push ecx
    push esi
    push edi
    call dword [__imp__ReadFile@20]
    pop ecx
    test eax, eax
    jz .bad_h
    mov word [ebp + VM86_CTX32.regs16], cx
    jmp .done

.write_h:
    movzx ebx, word [ebp + VM86_CTX32.regs16 + 6] ; BX (+6)
    cmp ebx, VM86_MAX_HANDLES
    jae .bad_h
    mov edi, dword [ebp + VM86_CTX32.hostHandles + ebx * 4]
    cmp edi, INVALID_HANDLE_VALUE
    je .bad_h
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov dx, word [ebp + VM86_CTX32.regs16 + 4]  ; DX (+4)
    call vm86_lin32
    mov esi, eax
    movzx ecx, word [ebp + VM86_CTX32.regs16 + 2] ; CX (+2)
    cmp ebx, 2
    ja .write_raw_h
    push ecx
    mov ebx, edi
    call con_write_handle32
    pop ecx
    mov word [ebp + VM86_CTX32.regs16], cx
    jmp .done
.write_raw_h:
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push 0
    push edx
    push ecx
    push esi
    push edi
    call dword [__imp__WriteFile@20]
    pop ecx
    test eax, eax
    jz .bad_h
    mov word [ebp + VM86_CTX32.regs16], cx
    jmp .done

.lseek_h:
    movzx ebx, word [ebp + VM86_CTX32.regs16 + 6] ; BX (+6)
    cmp ebx, VM86_MAX_HANDLES
    jae .bad_h
    mov edi, dword [ebp + VM86_CTX32.hostHandles + ebx * 4]
    cmp edi, INVALID_HANDLE_VALUE
    je .bad_h
    movzx eax, word [ebp + VM86_CTX32.regs16 + 2] ; CX (high)
    shl eax, 16
    mov ax, word [ebp + VM86_CTX32.regs16 + 4]    ; DX (low)
    movzx ecx, byte [ebp + VM86_CTX32.regs16]     ; AL (origin 0..2)
    push ecx
    push 0
    push eax
    push edi
    call dword [__imp__SetFilePointer@16]
    cmp eax, -1
    je .bad_h
    mov word [ebp + VM86_CTX32.regs16], ax
    shr eax, 16
    mov word [ebp + VM86_CTX32.regs16 + 4], ax
    jmp .done

.attr_h:
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6]
    mov dx, word [ebp + VM86_CTX32.regs16 + 4]
    call vm86_lin32
    mov esi, eax
    cmp byte [ebp + VM86_CTX32.regs16], 0
    jne .set_attr_g
    call hal_get_attr32
    jc .dos_err
    and eax, 0x3F
    mov word [ebp + VM86_CTX32.regs16 + 2], ax ; CX (+2)
    jmp .done
.set_attr_g:
    movzx ecx, word [ebp + VM86_CTX32.regs16 + 2]
    call hal_set_attr32
    jc .dos_err
    jmp .done

.ioctl_h:
    cmp byte [ebp + VM86_CTX32.regs16], 0
    jne .ignore_ok
    mov word [ebp + VM86_CTX32.regs16 + 4], 0x0083 ; char device stdout/stdin
    jmp .done

.getcwd_h:
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov dx, word [ebp + VM86_CTX32.regs16 + 12] ; SI (+12)
    call vm86_lin32
    mov edi, eax
    mov ecx, 256
    sub esp, 264
    mov ebx, esp
    push edi
    mov edi, ebx
    call hal_get_cwd32
    pop edi
    jc .cwd_err
    ; Strip "X:" prefix as DOS AH=47h expects!
    mov esi, ebx
    cmp byte [esi + 1], ':'
    jne .cp_cwd_g
    add esi, 2
    cmp byte [esi], 0x5C
    jne .cp_cwd_g
    inc esi
.cp_cwd_g:
    call str_copy32
    add esp, 264
    jmp .done
.cwd_err:
    add esp, 264
    jmp .dos_err

.alloc_mem:
    mov word [ebp + VM86_CTX32.regs16], 0x6000 ; free upper conventional segment
    jmp .done

.netbios_name:
    ; AX=5E00h -> write %COMPUTERNAME% padded to 15 spaces into DS:DX, CH=1, CL=0
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov dx, word [ebp + VM86_CTX32.regs16 + 4]  ; DX
    call vm86_lin32
    mov edi, eax
    push edi
    mov al, 0x20
    mov ecx, 15
    rep stosb
    mov byte [edi], 0
    pop edi
    mov esi, addr(szVarCompName)
    call env_get32
    test eax, eax
    jz .nb_done
    mov esi, eax
    xor ecx, ecx
.nb_cp:
    cmp ecx, 15
    jae .nb_done
    mov al, byte [esi + ecx]
    test al, al
    jz .nb_done
    mov byte [edi + ecx], al
    inc ecx
    jmp .nb_cp
.nb_done:
    mov word [ebp + VM86_CTX32.regs16 + 2], 0x0100 ; CH=1 valid
    jmp .done

.truename_h:
    mov ax, word [ebp + VM86_CTX32.sregs16 + 6] ; DS
    mov dx, word [ebp + VM86_CTX32.regs16 + 12] ; SI (+12)
    call vm86_lin32
    mov esi, eax
    call path_normalize32
    jc .dos_err
    mov esi, eax
    mov ax, word [ebp + VM86_CTX32.sregs16]     ; ES (+0)
    mov dx, word [ebp + VM86_CTX32.regs16 + 14] ; DI (+14)
    call vm86_lin32
    mov edi, eax
    call str_copy32
    jmp .done

.ignore_ok:
    jmp .done

.bad_h:
    mov eax, ERR_INVALID_HANDLE
.dos_err:
    or word [ebp + VM86_CTX32.regFlags], 1
    mov word [ebp + VM86_CTX32.regs16], ax

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc
