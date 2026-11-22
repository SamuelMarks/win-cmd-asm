;; @file vm86_exec.inc
;; @isa 386
;; @desc 8086 CPU instruction interpreter and .COM / MZ .EXE PSP loader for embedded vm8086.

@cpu 386
@format inc

;; @proc vm86_eval_cond32
;; @abi win32
;; @in EBP = VM86_CTX32*, AL = condition code (0..15 from 70..7F)
;; @out EAX = 1 if condition holds, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Evaluates all 16 Intel 8086 conditional jump predicates from VM86_CTX32.regFlags.
@proc vm86_eval_cond32
    movzx ecx, word [ebp + VM86_CTX32.regFlags]
    mov dl, al
    shr dl, 1
    xor eax, eax
    cmp dl, 0 ; O / NO (bit 11 = 0x0800)
    je .c_o
    cmp dl, 1 ; B / NB (bit 0 = 0x0001)
    je .c_b
    cmp dl, 2 ; Z / NZ (bit 6 = 0x0040)
    je .c_z
    cmp dl, 3 ; BE / NBE (CF | ZF)
    je .c_be
    cmp dl, 4 ; S / NS (bit 7 = 0x0080)
    je .c_s
    cmp dl, 5 ; P / NP (bit 2 = 0x0004)
    je .c_p
    cmp dl, 6 ; L / NL (SF != OF)
    je .c_l
    ; LE / NLE (ZF || (SF != OF))
    test ecx, 0x0040
    jnz .set1
    jmp .c_l

.c_o:
    test ecx, 0x0800
    setnz al
    jmp .inv_chk
.c_b:
    test ecx, 0x0001
    setnz al
    jmp .inv_chk
.c_z:
    test ecx, 0x0040
    setnz al
    jmp .inv_chk
.c_be:
    test ecx, 0x0041
    setnz al
    jmp .inv_chk
.c_s:
    test ecx, 0x0080
    setnz al
    jmp .inv_chk
.c_p:
    test ecx, 0x0004
    setnz al
    jmp .inv_chk
.c_l:
    mov edx, ecx
    shr ecx, 7
    shr edx, 11
    xor ecx, edx
    and ecx, 1
    mov eax, ecx
    jmp .inv_chk
.set1:
    mov eax, 1
.inv_chk:
    ; Wait: reload low bit of original AL? Save original AL in CH beforeshr!
    ret
@endproc

;; @proc vm86_check_jcc32
;; @abi win32
;; @in EBP = VM86_CTX32*, AL = opcode low nibble (0..15)
;; @out EAX = 1 if branch taken, 0 otherwise
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Evaluates Jcc condition nibble including odd-nibble negation.
@proc vm86_check_jcc32
    push ebx
    mov bl, al
    and bl, 1
    call vm86_eval_cond32
    movzx ecx, bl
    xor eax, ecx
    pop ebx
    ret
@endproc

;; @proc vm86_step_str_op32
;; @abi win32
;; @in EBP = VM86_CTX32*, AL = string opcode (0xA4..0xAF)
;; @out Executes one or REP-counted string iteration (MOVS, CMPS, STOS, LODS, SCAS)
;; @err none
;; @clobbers eax, ecx, edx
;; @desc Executes 8086 string primitives honoring DF, segment overrides, REP (0xF3), and REPNE (0xF2).
@proc vm86_step_str_op32
    push ebx
    push esi
    push edi
    mov bl, al ; opcode A4..AF

.rep_lp:
    cmp byte [ebp + VM86_CTX32.bRepMode], 0
    je .one_iter
    cmp word [ebp + VM86_CTX32.regs16 + 2], 0 ; CX == 0?
    je .done

.one_iter:
    ; Compute delta = +1/-1 (byte) or +2/-2 (word) in SI/DI
    mov esi, 1
    test bl, 1
    jz .got_sz
    mov esi, 2
.got_sz:
    test word [ebp + VM86_CTX32.regFlags], 0x0400 ; DF
    jz .got_dir
    neg esi
.got_dir:
    cmp bl, 0xA4
    je .movsb_op
    cmp bl, 0xA5
    je .movsw_op
    cmp bl, 0xAA
    je .stosb_op
    cmp bl, 0xAB
    je .stosw_op
    cmp bl, 0xAC
    je .lodsb_op
    cmp bl, 0xAD
    je .lodsw_op
    cmp bl, 0xAE
    je .scasb_op
    cmp bl, 0xAF
    je .scasw_op
    cmp bl, 0xA6
    je .cmpsb_op
    jmp .cmpsw_op

.movsb_op:
    mov cl, 3
    call vm86_eff_seg32
    mov dx, word [ebp + VM86_CTX32.regs16 + 12] ; SI
    call vm86_lin32
    mov dl, byte [eax]
    push edx
    mov ax, word [ebp + VM86_CTX32.sregs16]     ; ES
    mov dx, word [ebp + VM86_CTX32.regs16 + 14] ; DI
    call vm86_lin32
    pop edx
    mov byte [eax], dl
    add word [ebp + VM86_CTX32.regs16 + 12], si
    add word [ebp + VM86_CTX32.regs16 + 14], si
    jmp .rep_post_no_zf

.movsw_op:
    mov cl, 3
    call vm86_eff_seg32
    mov dx, word [ebp + VM86_CTX32.regs16 + 12]
    call vm86_lin32
    movzx edx, word [eax]
    push edx
    mov ax, word [ebp + VM86_CTX32.sregs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + 14]
    call vm86_lin32
    pop edx
    mov word [eax], dx
    add word [ebp + VM86_CTX32.regs16 + 12], si
    add word [ebp + VM86_CTX32.regs16 + 14], si
    jmp .rep_post_no_zf

.stosb_op:
    mov ax, word [ebp + VM86_CTX32.sregs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + 14]
    call vm86_lin32
    mov dl, byte [ebp + VM86_CTX32.regs16] ; AL
    mov byte [eax], dl
    add word [ebp + VM86_CTX32.regs16 + 14], si
    jmp .rep_post_no_zf

.stosw_op:
    mov ax, word [ebp + VM86_CTX32.sregs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + 14]
    call vm86_lin32
    mov dx, word [ebp + VM86_CTX32.regs16] ; AX
    mov word [eax], dx
    add word [ebp + VM86_CTX32.regs16 + 14], si
    jmp .rep_post_no_zf

.lodsb_op:
    mov cl, 3
    call vm86_eff_seg32
    mov dx, word [ebp + VM86_CTX32.regs16 + 12]
    call vm86_lin32
    mov dl, byte [eax]
    mov byte [ebp + VM86_CTX32.regs16], dl
    add word [ebp + VM86_CTX32.regs16 + 12], si
    jmp .rep_post_no_zf

.lodsw_op:
    mov cl, 3
    call vm86_eff_seg32
    mov dx, word [ebp + VM86_CTX32.regs16 + 12]
    call vm86_lin32
    mov dx, word [eax]
    mov word [ebp + VM86_CTX32.regs16], dx
    add word [ebp + VM86_CTX32.regs16 + 12], si
    jmp .rep_post_no_zf

.scasb_op:
    mov ax, word [ebp + VM86_CTX32.sregs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + 14]
    call vm86_lin32
    mov dl, byte [eax]
    mov al, byte [ebp + VM86_CTX32.regs16]
    mov cx, 0x0007 ; CMP 8-bit
    call vm86_alu16
    add word [ebp + VM86_CTX32.regs16 + 14], si
    jmp .rep_post_zf

.scasw_op:
    mov ax, word [ebp + VM86_CTX32.sregs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + 14]
    call vm86_lin32
    mov dx, word [eax]
    mov ax, word [ebp + VM86_CTX32.regs16]
    mov cx, 0x0107 ; CMP 16-bit
    call vm86_alu16
    add word [ebp + VM86_CTX32.regs16 + 14], si
    jmp .rep_post_zf

.cmpsb_op:
    mov cl, 3
    call vm86_eff_seg32
    mov dx, word [ebp + VM86_CTX32.regs16 + 12]
    call vm86_lin32
    movzx edi, byte [eax]
    mov ax, word [ebp + VM86_CTX32.sregs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + 14]
    call vm86_lin32
    mov dl, byte [eax]
    mov eax, edi
    mov cx, 0x0007
    call vm86_alu16
    add word [ebp + VM86_CTX32.regs16 + 12], si
    add word [ebp + VM86_CTX32.regs16 + 14], si
    jmp .rep_post_zf

.cmpsw_op:
    mov cl, 3
    call vm86_eff_seg32
    mov dx, word [ebp + VM86_CTX32.regs16 + 12]
    call vm86_lin32
    movzx edi, word [eax]
    mov ax, word [ebp + VM86_CTX32.sregs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + 14]
    call vm86_lin32
    mov dx, word [eax]
    mov eax, edi
    mov cx, 0x0107
    call vm86_alu16
    add word [ebp + VM86_CTX32.regs16 + 12], si
    add word [ebp + VM86_CTX32.regs16 + 14], si

.rep_post_zf:
    cmp byte [ebp + VM86_CTX32.bRepMode], 0
    je .done
    dec word [ebp + VM86_CTX32.regs16 + 2]
    jz .done
    cmp byte [ebp + VM86_CTX32.bRepMode], 0xF3 ; REPE: continue while ZF=1
    je .chk_repe
    test word [ebp + VM86_CTX32.regFlags], 0x0040 ; REPNE: continue while ZF=0
    jz .rep_lp
    jmp .done
.chk_repe:
    test word [ebp + VM86_CTX32.regFlags], 0x0040
    jnz .rep_lp
    jmp .done

.rep_post_no_zf:
    cmp byte [ebp + VM86_CTX32.bRepMode], 0
    je .done
    dec word [ebp + VM86_CTX32.regs16 + 2]
    jnz .rep_lp

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc vm86_step_one32
;; @abi win32
;; @in EBP = VM86_CTX32*
;; @out Executes one complete 8086 instruction (including prefixes)
;; @err Sets bHalted = 1 on HLT, INT 20h, INT 21h/4Ch, or unrecoverable fault
;; @clobbers eax, ecx, edx
;; @desc Decodes and executes a single 8086 instruction inside VM86_CTX32.
@proc vm86_step_one32
    push ebx
    push esi
    push edi

    mov byte [ebp + VM86_CTX32.bSegOvr], 0xFF
    mov byte [ebp + VM86_CTX32.bRepMode], 0

.prefix_lp:
    call vm86_fetch8
    mov bl, al
    cmp bl, 0x26
    je .pre_es
    cmp bl, 0x2E
    je .pre_cs
    cmp bl, 0x36
    je .pre_ss
    cmp bl, 0x3E
    je .pre_ds
    cmp bl, 0xF2
    je .pre_rep
    cmp bl, 0xF3
    je .pre_rep
    cmp bl, 0xF0 ; LOCK
    je .prefix_lp
    jmp .dispatch

.pre_es:
    mov byte [ebp + VM86_CTX32.bSegOvr], 0
    jmp .prefix_lp
.pre_cs:
    mov byte [ebp + VM86_CTX32.bSegOvr], 1
    jmp .prefix_lp
.pre_ss:
    mov byte [ebp + VM86_CTX32.bSegOvr], 2
    jmp .prefix_lp
.pre_ds:
    mov byte [ebp + VM86_CTX32.bSegOvr], 3
    jmp .prefix_lp
.pre_rep:
    mov byte [ebp + VM86_CTX32.bRepMode], bl
    jmp .prefix_lp

.dispatch:
    ; 0x00..0x3D: Standard 8-family ALU (ADD, OR, ADC, SBB, AND, SUB, XOR, CMP) + PUSH/POP sreg
    cmp bl, 0x3F
    ja .above_3f
    mov al, bl
    and al, 7
    cmp al, 6
    je .push_sreg
    cmp al, 7
    je .pop_sreg
    cmp al, 4
    je .alu_al_imm8
    cmp al, 5
    je .alu_ax_imm16

    ; ModR/M ALU (dir = bit 1, word = bit 0, op = bits 3..5)
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax ; EA ptr
    mov dl, bl
    and dl, 1
    call vm86_reg_ptr32
    mov esi, eax ; reg ptr

    mov cl, bl
    shr cl, 3
    and cl, 7 ; alu_op
    mov ch, bl
    and ch, 1 ; is_word

    test bl, 2 ; dir=0 -> EA = op(EA, reg); dir=1 -> reg = op(reg, EA)
    jnz .alu_to_reg
    test ch, ch
    jnz .alu_mr16
    mov al, byte [edi]
    mov dl, byte [esi]
    push ecx
    call vm86_alu16
    pop ecx
    cmp cl, 7
    je .done
    mov byte [edi], al
    jmp .done
.alu_mr16:
    mov ax, word [edi]
    mov dx, word [esi]
    push ecx
    call vm86_alu16
    pop ecx
    cmp cl, 7
    je .done
    mov word [edi], ax
    jmp .done

.alu_to_reg:
    test ch, ch
    jnz .alu_rm16
    mov al, byte [esi]
    mov dl, byte [edi]
    push ecx
    call vm86_alu16
    pop ecx
    cmp cl, 7
    je .done
    mov byte [esi], al
    jmp .done
.alu_rm16:
    mov ax, word [esi]
    mov dx, word [edi]
    push ecx
    call vm86_alu16
    pop ecx
    cmp cl, 7
    je .done
    mov word [esi], ax
    jmp .done

.alu_al_imm8:
    call vm86_fetch8
    mov dl, al
    mov al, byte [ebp + VM86_CTX32.regs16]
    mov cl, bl
    shr cl, 3
    and cl, 7
    xor ch, ch
    push ecx
    call vm86_alu16
    pop ecx
    cmp cl, 7
    je .done
    mov byte [ebp + VM86_CTX32.regs16], al
    jmp .done

.alu_ax_imm16:
    call vm86_fetch16
    mov dx, ax
    mov ax, word [ebp + VM86_CTX32.regs16]
    mov cl, bl
    shr cl, 3
    and cl, 7
    mov ch, 1
    push ecx
    call vm86_alu16
    pop ecx
    cmp cl, 7
    je .done
    mov word [ebp + VM86_CTX32.regs16], ax
    jmp .done

.push_sreg:
    movzx ecx, bl
    shr ecx, 3
    and ecx, 3
    mov bx, word [ebp + VM86_CTX32.sregs16 + ecx * 2]
    call vm86_push16
    jmp .done

.pop_sreg:
    movzx esi, bl
    shr esi, 3
    and esi, 3
    call vm86_pop16
    mov word [ebp + VM86_CTX32.sregs16 + esi * 2], ax
    jmp .done

.above_3f:
    ; 0x40..0x47 INC r16, 0x48..0x4F DEC r16, 0x50..0x57 PUSH r16, 0x58..0x5F POP r16
    cmp bl, 0x47
    jbe .inc_r16
    cmp bl, 0x4F
    jbe .dec_r16
    cmp bl, 0x57
    jbe .push_r16
    cmp bl, 0x5F
    jbe .pop_r16
    cmp bl, 0x6F
    jbe .done ; ignore unused/bound
    cmp bl, 0x7F
    jbe .jcc_rel8
    cmp bl, 0x83
    jbe .grp1_imm
    cmp bl, 0x85
    jbe .test_rm_r
    cmp bl, 0x87
    jbe .xchg_rm_r
    cmp bl, 0x8B
    jbe .mov_rm_r
    cmp bl, 0x8C
    je .mov_rm_sreg
    cmp bl, 0x8D
    je .lea_r16
    cmp bl, 0x8E
    je .mov_sreg_rm
    cmp bl, 0x8F
    je .pop_rm16
    cmp bl, 0x97
    jbe .xchg_ax_r16
    cmp bl, 0x98
    je .cbw_op
    cmp bl, 0x99
    je .cwd_op
    cmp bl, 0x9A
    je .call_far_ptr
    cmp bl, 0x9C
    je .pushf_op
    cmp bl, 0x9D
    je .popf_op
    cmp bl, 0x9E
    je .sahf_op
    cmp bl, 0x9F
    je .lahf_op
    cmp bl, 0xA3
    jbe .mov_moffs
    cmp bl, 0xA7
    jbe .str_op
    cmp bl, 0xA9
    jbe .test_acc_imm
    cmp bl, 0xAF
    jbe .str_op
    cmp bl, 0xB7
    jbe .mov_r8_imm
    cmp bl, 0xBF
    jbe .mov_r16_imm
    cmp bl, 0xC2
    je .ret_imm16
    cmp bl, 0xC3
    je .ret_near
    cmp bl, 0xC4
    je .les_op
    cmp bl, 0xC5
    je .lds_op
    cmp bl, 0xC7
    jbe .mov_rm_imm
    cmp bl, 0xCA
    je .retf_imm16
    cmp bl, 0xCB
    je .retf_op
    cmp bl, 0xCC
    je .int3_op
    cmp bl, 0xCD
    je .int_imm8
    cmp bl, 0xCF
    je .iret_op
    cmp bl, 0xD3
    jbe .grp2_shift
    cmp bl, 0xE3
    jbe .loop_jcxz
    cmp bl, 0xE8
    je .call_rel16
    cmp bl, 0xE9
    je .jmp_rel16
    cmp bl, 0xEA
    je .jmp_far_ptr
    cmp bl, 0xEB
    je .jmp_rel8
    cmp bl, 0xF4
    je .hlt_op
    cmp bl, 0xF5
    je .cmc_op
    cmp bl, 0xF7
    jbe .grp3_op
    cmp bl, 0xFD
    jbe .flag_ops
    jmp .grp45_op

.inc_r16:
    movzx ecx, bl
    and ecx, 7
    inc word [ebp + VM86_CTX32.regs16 + ecx * 2]
    mov ax, word [ebp + VM86_CTX32.regs16 + ecx * 2]
    call vm86_set_szp16
    jmp .done

.dec_r16:
    movzx ecx, bl
    and ecx, 7
    dec word [ebp + VM86_CTX32.regs16 + ecx * 2]
    mov ax, word [ebp + VM86_CTX32.regs16 + ecx * 2]
    call vm86_set_szp16
    jmp .done

.push_r16:
    movzx ecx, bl
    and ecx, 7
    mov bx, word [ebp + VM86_CTX32.regs16 + ecx * 2]
    call vm86_push16
    jmp .done

.pop_r16:
    movzx esi, bl
    and esi, 7
    call vm86_pop16
    mov word [ebp + VM86_CTX32.regs16 + esi * 2], ax
    jmp .done

.jcc_rel8:
    call vm86_fetch8
    movsx esi, al
    mov al, bl
    and al, 0x0F
    call vm86_check_jcc32
    test eax, eax
    jz .done
    add word [ebp + VM86_CTX32.regIp], si
    jmp .done

.grp1_imm:
    ; 0x80/0x82: r/m8, imm8; 0x81: r/m16, imm16; 0x83: r/m16, sign-ext imm8
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    mov esi, ecx ; sub-op 0..7
    cmp bl, 0x81
    je .g1_w16
    cmp bl, 0x83
    je .g1_s8
    call vm86_fetch8
    mov dl, al
    mov al, byte [edi]
    mov ecx, esi
    xor ch, ch
    call vm86_alu16
    cmp esi, 7
    je .done
    mov byte [edi], al
    jmp .done
.g1_w16:
    call vm86_fetch16
    mov dx, ax
    jmp .g1_do16
.g1_s8:
    call vm86_fetch8
    movsx edx, al
.g1_do16:
    mov ax, word [edi]
    mov ecx, esi
    mov ch, 1
    call vm86_alu16
    cmp esi, 7
    je .done
    mov word [edi], ax
    jmp .done

.test_rm_r:
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    mov dl, bl
    and dl, 1
    call vm86_reg_ptr32
    mov esi, eax
    test bl, 1
    jnz .tst16
    mov al, byte [edi]
    mov dl, byte [esi]
    mov cx, 0x0004 ; AND (discards result!)
    call vm86_alu16
    jmp .done
.tst16:
    mov ax, word [edi]
    mov dx, word [esi]
    mov cx, 0x0104
    call vm86_alu16
    jmp .done

.xchg_rm_r:
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    mov dl, bl
    and dl, 1
    call vm86_reg_ptr32
    test bl, 1
    jnz .xcg16
    mov cl, byte [edi]
    mov dl, byte [eax]
    mov byte [edi], dl
    mov byte [eax], cl
    jmp .done
.xcg16:
    mov cx, word [edi]
    mov dx, word [eax]
    mov word [edi], dx
    mov word [eax], cx
    jmp .done

.mov_rm_r:
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    mov dl, bl
    and dl, 1
    call vm86_reg_ptr32
    test bl, 2 ; dir
    jnz .mv_to_reg
    test bl, 1
    jnz .mv_mr16
    mov cl, byte [eax]
    mov byte [edi], cl
    jmp .done
.mv_mr16:
    mov cx, word [eax]
    mov word [edi], cx
    jmp .done
.mv_to_reg:
    test bl, 1
    jnz .mv_rm16
    mov cl, byte [edi]
    mov byte [eax], cl
    jmp .done
.mv_rm16:
    mov cx, word [edi]
    mov word [eax], cx
    jmp .done

.mov_rm_sreg:
    call vm86_fetch8
    mov ch, 1
    call vm86_decode_modrm32
    and ecx, 3
    mov dx, word [ebp + VM86_CTX32.sregs16 + ecx * 2]
    mov word [eax], dx
    jmp .done

.mov_sreg_rm:
    call vm86_fetch8
    mov ch, 1
    call vm86_decode_modrm32
    and ecx, 3
    mov dx, word [eax]
    mov word [ebp + VM86_CTX32.sregs16 + ecx * 2], dx
    jmp .done

.lea_r16:
    ; Decode offset without adding pMem + seg*16: compute linear - seg*16 - pMem!
    mov byte [ebp + VM86_CTX32.bSegOvr], 0 ; force ES
    push word [ebp + VM86_CTX32.sregs16]
    mov word [ebp + VM86_CTX32.sregs16], 0
    call vm86_fetch8
    mov ch, 1
    call vm86_decode_modrm32
    pop word [ebp + VM86_CTX32.sregs16]
    sub eax, dword [ebp + VM86_CTX32.pMem]
    mov word [ebp + VM86_CTX32.regs16 + ecx * 2], ax
    jmp .done

.pop_rm16:
    call vm86_fetch8
    mov ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    call vm86_pop16
    mov word [edi], ax
    jmp .done

.xchg_ax_r16:
    movzx ecx, bl
    and ecx, 7
    mov ax, word [ebp + VM86_CTX32.regs16]
    mov dx, word [ebp + VM86_CTX32.regs16 + ecx * 2]
    mov word [ebp + VM86_CTX32.regs16], dx
    mov word [ebp + VM86_CTX32.regs16 + ecx * 2], ax
    jmp .done

.cbw_op:
    movsx ax, byte [ebp + VM86_CTX32.regs16]
    mov word [ebp + VM86_CTX32.regs16], ax
    jmp .done

.cwd_op:
    movsx eax, word [ebp + VM86_CTX32.regs16]
    shr eax, 16
    mov word [ebp + VM86_CTX32.regs16 + 4], ax ; DX
    jmp .done

.call_far_ptr:
    call vm86_fetch16
    mov esi, eax ; new IP
    call vm86_fetch16
    mov edi, eax ; new CS
    mov bx, word [ebp + VM86_CTX32.sregs16 + 2]
    call vm86_push16
    mov bx, word [ebp + VM86_CTX32.regIp]
    call vm86_push16
    mov word [ebp + VM86_CTX32.sregs16 + 2], di
    mov word [ebp + VM86_CTX32.regIp], si
    jmp .done

.pushf_op:
    mov bx, word [ebp + VM86_CTX32.regFlags]
    or bx, 0xF002
    call vm86_push16
    jmp .done

.popf_op:
    call vm86_pop16
    or ax, 0x0002
    mov word [ebp + VM86_CTX32.regFlags], ax
    jmp .done

.sahf_op:
    movzx eax, byte [ebp + VM86_CTX32.regs16 + 1] ; AH
    and word [ebp + VM86_CTX32.regFlags], 0xFF00
    or ax, 0x02
    or word [ebp + VM86_CTX32.regFlags], ax
    jmp .done

.lahf_op:
    mov al, byte [ebp + VM86_CTX32.regFlags]
    or al, 0x02
    mov byte [ebp + VM86_CTX32.regs16 + 1], al
    jmp .done

.mov_moffs:
    call vm86_fetch16
    mov esi, eax ; disp16
    mov cl, 3
    call vm86_eff_seg32
    mov edx, esi
    call vm86_lin32
    cmp bl, 0xA0
    je .moff_a0
    cmp bl, 0xA1
    je .moff_a1
    cmp bl, 0xA2
    je .moff_a2
    mov dx, word [ebp + VM86_CTX32.regs16]
    mov word [eax], dx
    jmp .done
.moff_a0:
    mov dl, byte [eax]
    mov byte [ebp + VM86_CTX32.regs16], dl
    jmp .done
.moff_a1:
    mov dx, word [eax]
    mov word [ebp + VM86_CTX32.regs16], dx
    jmp .done
.moff_a2:
    mov dl, byte [ebp + VM86_CTX32.regs16]
    mov byte [eax], dl
    jmp .done

.str_op:
    mov al, bl
    call vm86_step_str_op32
    jmp .done

.test_acc_imm:
    test bl, 1
    jnz .tai16
    call vm86_fetch8
    mov dl, al
    mov al, byte [ebp + VM86_CTX32.regs16]
    mov cx, 0x0004
    call vm86_alu16
    jmp .done
.tai16:
    call vm86_fetch16
    mov dx, ax
    mov ax, word [ebp + VM86_CTX32.regs16]
    mov cx, 0x0104
    call vm86_alu16
    jmp .done

.mov_r8_imm:
    movzx ecx, bl
    and ecx, 7
    xor dl, dl
    call vm86_reg_ptr32
    mov edi, eax
    call vm86_fetch8
    mov byte [edi], al
    jmp .done

.mov_r16_imm:
    movzx esi, bl
    and esi, 7
    call vm86_fetch16
    mov word [ebp + VM86_CTX32.regs16 + esi * 2], ax
    jmp .done

.ret_imm16:
    call vm86_fetch16
    mov esi, eax
    call vm86_pop16
    mov word [ebp + VM86_CTX32.regIp], ax
    add word [ebp + VM86_CTX32.regs16 + 8], si
    jmp .done

.ret_near:
    call vm86_pop16
    test ax, ax
    jz .ret_zero_psp
    mov word [ebp + VM86_CTX32.regIp], ax
    jmp .done
.ret_zero_psp:
    mov byte [ebp + VM86_CTX32.bExitCode], 0
    mov byte [ebp + VM86_CTX32.bHalted], 1
    jmp .done

.les_op:
    call vm86_fetch8
    mov ch, 1
    call vm86_decode_modrm32
    mov dx, word [eax]
    mov word [ebp + VM86_CTX32.regs16 + ecx * 2], dx
    mov dx, word [eax + 2]
    mov word [ebp + VM86_CTX32.sregs16], dx ; ES
    jmp .done

.lds_op:
    call vm86_fetch8
    mov ch, 1
    call vm86_decode_modrm32
    mov dx, word [eax]
    mov word [ebp + VM86_CTX32.regs16 + ecx * 2], dx
    mov dx, word [eax + 2]
    mov word [ebp + VM86_CTX32.sregs16 + 6], dx ; DS
    jmp .done

.mov_rm_imm:
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    test bl, 1
    jnz .mrmi16
    call vm86_fetch8
    mov byte [edi], al
    jmp .done
.mrmi16:
    call vm86_fetch16
    mov word [edi], ax
    jmp .done

.retf_imm16:
    call vm86_fetch16
    mov esi, eax
    call vm86_pop16
    mov word [ebp + VM86_CTX32.regIp], ax
    call vm86_pop16
    mov word [ebp + VM86_CTX32.sregs16 + 2], ax
    add word [ebp + VM86_CTX32.regs16 + 8], si
    jmp .done

.retf_op:
    call vm86_pop16
    mov word [ebp + VM86_CTX32.regIp], ax
    call vm86_pop16
    mov word [ebp + VM86_CTX32.sregs16 + 2], ax
    jmp .done

.int3_op:
    jmp .done

.int_imm8:
    call vm86_fetch8
    cmp al, 0x20
    je .int20
    cmp al, 0x21
    je .int21
    cmp al, 0x10
    je .int10
    cmp al, 0x1A
    je .int1a
    cmp al, 0x2F
    je .int2f
    jmp .done
.int20:
    mov byte [ebp + VM86_CTX32.bExitCode], 0
    mov byte [ebp + VM86_CTX32.bHalted], 1
    jmp .done
.int21:
    call vm86_dos_int21
    jmp .done
.int10:
    cmp byte [ebp + VM86_CTX32.regs16 + 1], 0x0E ; TTY teletype AL
    jne .done
    sub esp, 4
    mov al, byte [ebp + VM86_CTX32.regs16]
    mov byte [esp], al
    mov esi, esp
    mov ecx, 1
    call con_write_out32
    add esp, 4
    jmp .done
.int1a:
    mov word [ebp + VM86_CTX32.regs16 + 2], 0
    mov word [ebp + VM86_CTX32.regs16 + 4], 100
    mov byte [ebp + VM86_CTX32.regs16], 0
    jmp .done
.int2f:
    mov word [ebp + VM86_CTX32.regs16], 0
    jmp .done

.iret_op:
    call vm86_pop16
    mov word [ebp + VM86_CTX32.regIp], ax
    call vm86_pop16
    mov word [ebp + VM86_CTX32.sregs16 + 2], ax
    call vm86_pop16
    mov word [ebp + VM86_CTX32.regFlags], ax
    jmp .done

.grp2_shift:
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    mov esi, ecx ; sub-op 0..7
    mov cl, 1
    test bl, 2
    jz .sh_cnt_ready
    mov cl, byte [ebp + VM86_CTX32.regs16 + 2] ; CL
.sh_cnt_ready:
    and cl, 31
    jz .done
    test bl, 1
    jnz .sh16
    mov al, byte [edi]
    cmp esi, 0
    je .rol8
    cmp esi, 1
    je .ror8
    cmp esi, 4
    je .shl8
    cmp esi, 5
    je .shr8
    cmp esi, 7
    je .sar8
    shl al, cl
    jmp .st_sh8
.rol8:
    rol al, cl
    jmp .st_sh8
.ror8:
    ror al, cl
    jmp .st_sh8
.shl8:
    shl al, cl
    jmp .st_sh8
.shr8:
    shr al, cl
    jmp .st_sh8
.sar8:
    sar al, cl
.st_sh8:
    pushfd
    pop edx
    mov byte [edi], al
    and word [ebp + VM86_CTX32.regFlags], 0xF73E
    and dx, 0x08C1
    or word [ebp + VM86_CTX32.regFlags], dx
    jmp .done

.sh16:
    mov ax, word [edi]
    cmp esi, 0
    je .rol16
    cmp esi, 1
    je .ror16
    cmp esi, 4
    je .shl16
    cmp esi, 5
    je .shr16
    cmp esi, 7
    je .sar16
    shl ax, cl
    jmp .st_sh16
.rol16:
    rol ax, cl
    jmp .st_sh16
.ror16:
    ror ax, cl
    jmp .st_sh16
.shl16:
    shl ax, cl
    jmp .st_sh16
.shr16:
    shr ax, cl
    jmp .st_sh16
.sar16:
    sar ax, cl
.st_sh16:
    pushfd
    pop edx
    mov word [edi], ax
    and word [ebp + VM86_CTX32.regFlags], 0xF73E
    and dx, 0x08C1
    or word [ebp + VM86_CTX32.regFlags], dx
    jmp .done

.loop_jcxz:
    call vm86_fetch8
    movsx esi, al
    cmp bl, 0xE3
    je .jcxz_chk
    dec word [ebp + VM86_CTX32.regs16 + 2]
    jz .done
    cmp bl, 0xE2 ; LOOP
    je .take_rel
    cmp bl, 0xE1 ; LOOPE
    je .loope_chk
    test word [ebp + VM86_CTX32.regFlags], 0x0040
    jz .take_rel
    jmp .done
.loope_chk:
    test word [ebp + VM86_CTX32.regFlags], 0x0040
    jnz .take_rel
    jmp .done
.jcxz_chk:
    cmp word [ebp + VM86_CTX32.regs16 + 2], 0
    jne .done
.take_rel:
    add word [ebp + VM86_CTX32.regIp], si
    jmp .done

.call_rel16:
    call vm86_fetch16
    mov esi, eax
    mov bx, word [ebp + VM86_CTX32.regIp]
    call vm86_push16
    add word [ebp + VM86_CTX32.regIp], si
    jmp .done

.jmp_rel16:
    call vm86_fetch16
    add word [ebp + VM86_CTX32.regIp], ax
    jmp .done

.jmp_far_ptr:
    call vm86_fetch16
    mov esi, eax
    call vm86_fetch16
    mov word [ebp + VM86_CTX32.regIp], si
    mov word [ebp + VM86_CTX32.sregs16 + 2], ax
    jmp .done

.jmp_rel8:
    call vm86_fetch8
    movsx eax, al
    add word [ebp + VM86_CTX32.regIp], ax
    jmp .done

.hlt_op:
    mov byte [ebp + VM86_CTX32.bHalted], 1
    jmp .done

.cmc_op:
    xor word [ebp + VM86_CTX32.regFlags], 1
    jmp .done

.grp3_op:
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    mov esi, ecx
    test bl, 1
    jnz .g3_16
    cmp esi, 0
    je .g3_tst8
    cmp esi, 2
    je .g3_not8
    cmp esi, 3
    je .g3_neg8
    cmp esi, 4
    je .g3_mul8
    cmp esi, 5
    je .g3_imul8
    cmp esi, 6
    je .g3_div8
    jmp .g3_idiv8
.g3_tst8:
    call vm86_fetch8
    mov dl, al
    mov al, byte [edi]
    mov cx, 0x0004
    call vm86_alu16
    jmp .done
.g3_not8:
    not byte [edi]
    jmp .done
.g3_neg8:
    xor al, al
    mov dl, byte [edi]
    mov cx, 0x0005
    call vm86_alu16
    mov byte [edi], al
    jmp .done
.g3_mul8:
    movzx eax, byte [ebp + VM86_CTX32.regs16]
    movzx ecx, byte [edi]
    imul eax, ecx
    mov word [ebp + VM86_CTX32.regs16], ax
    jmp .done
.g3_imul8:
    movsx eax, byte [ebp + VM86_CTX32.regs16]
    movsx ecx, byte [edi]
    imul eax, ecx
    mov word [ebp + VM86_CTX32.regs16], ax
    jmp .done
.g3_div8:
    movzx eax, word [ebp + VM86_CTX32.regs16]
    movzx ecx, byte [edi]
    test ecx, ecx
    jz .hlt_op
    xor edx, edx
    div ecx
    mov byte [ebp + VM86_CTX32.regs16], al
    mov byte [ebp + VM86_CTX32.regs16 + 1], dl
    jmp .done
.g3_idiv8:
    movsx eax, word [ebp + VM86_CTX32.regs16]
    movsx ecx, byte [edi]
    test ecx, ecx
    jz .hlt_op
    cdq
    idiv ecx
    mov byte [ebp + VM86_CTX32.regs16], al
    mov byte [ebp + VM86_CTX32.regs16 + 1], dl
    jmp .done

.g3_16:
    cmp esi, 0
    je .g3_tst16
    cmp esi, 2
    je .g3_not16
    cmp esi, 3
    je .g3_neg16
    cmp esi, 4
    je .g3_mul16
    cmp esi, 5
    je .g3_imul16
    cmp esi, 6
    je .g3_div16
    jmp .g3_idiv16
.g3_tst16:
    call vm86_fetch16
    mov dx, ax
    mov ax, word [edi]
    mov cx, 0x0104
    call vm86_alu16
    jmp .done
.g3_not16:
    not word [edi]
    jmp .done
.g3_neg16:
    xor ax, ax
    mov dx, word [edi]
    mov cx, 0x0105
    call vm86_alu16
    mov word [edi], ax
    jmp .done
.g3_mul16:
    movzx eax, word [ebp + VM86_CTX32.regs16]
    movzx ecx, word [edi]
    mul ecx
    mov word [ebp + VM86_CTX32.regs16], ax
    shr eax, 16
    mov word [ebp + VM86_CTX32.regs16 + 4], ax
    jmp .done
.g3_imul16:
    movsx eax, word [ebp + VM86_CTX32.regs16]
    movsx ecx, word [edi]
    imul eax, ecx
    mov word [ebp + VM86_CTX32.regs16], ax
    shr eax, 16
    mov word [ebp + VM86_CTX32.regs16 + 4], ax
    jmp .done
.g3_div16:
    movzx eax, word [ebp + VM86_CTX32.regs16 + 4]
    shl eax, 16
    mov ax, word [ebp + VM86_CTX32.regs16]
    movzx ecx, word [edi]
    test ecx, ecx
    jz .hlt_op
    xor edx, edx
    div ecx
    mov word [ebp + VM86_CTX32.regs16], ax
    mov word [ebp + VM86_CTX32.regs16 + 4], dx
    jmp .done
.g3_idiv16:
    movzx eax, word [ebp + VM86_CTX32.regs16 + 4]
    shl eax, 16
    mov ax, word [ebp + VM86_CTX32.regs16]
    movsx ecx, word [edi]
    test ecx, ecx
    jz .hlt_op
    cdq
    idiv ecx
    mov word [ebp + VM86_CTX32.regs16], ax
    mov word [ebp + VM86_CTX32.regs16 + 4], dx
    jmp .done

.flag_ops:
    cmp bl, 0xF8
    je .f_clc
    cmp bl, 0xF9
    je .f_stc
    cmp bl, 0xFC
    je .f_cld
    cmp bl, 0xFD
    je .f_std
    jmp .done
.f_clc:
    and word [ebp + VM86_CTX32.regFlags], 0xFFFE
    jmp .done
.f_stc:
    or word [ebp + VM86_CTX32.regFlags], 0x0001
    jmp .done
.f_cld:
    and word [ebp + VM86_CTX32.regFlags], 0xFBFF
    jmp .done
.f_std:
    or word [ebp + VM86_CTX32.regFlags], 0x0400
    jmp .done

.grp45_op:
    call vm86_fetch8
    mov ch, bl
    and ch, 1
    call vm86_decode_modrm32
    mov edi, eax
    test bl, 1
    jnz .g5_16
    cmp ecx, 0
    je .g4_inc8
    dec byte [edi]
    mov al, byte [edi]
    call vm86_set_szp8
    jmp .done
.g4_inc8:
    inc byte [edi]
    mov al, byte [edi]
    call vm86_set_szp8
    jmp .done

.g5_16:
    cmp ecx, 0
    je .g5_inc
    cmp ecx, 1
    je .g5_dec
    cmp ecx, 2
    je .g5_call_n
    cmp ecx, 3
    je .g5_call_f
    cmp ecx, 4
    je .g5_jmp_n
    cmp ecx, 5
    je .g5_jmp_f
    mov bx, word [edi]
    call vm86_push16
    jmp .done
.g5_inc:
    inc word [edi]
    mov ax, word [edi]
    call vm86_set_szp16
    jmp .done
.g5_dec:
    dec word [edi]
    mov ax, word [edi]
    call vm86_set_szp16
    jmp .done
.g5_call_n:
    mov esi, eax
    movzx esi, word [edi]
    mov bx, word [ebp + VM86_CTX32.regIp]
    call vm86_push16
    mov word [ebp + VM86_CTX32.regIp], si
    jmp .done
.g5_call_f:
    mov si, word [edi]
    mov cx, word [edi + 2]
    push ecx
    mov bx, word [ebp + VM86_CTX32.sregs16 + 2]
    call vm86_push16
    mov bx, word [ebp + VM86_CTX32.regIp]
    call vm86_push16
    pop ecx
    mov word [ebp + VM86_CTX32.sregs16 + 2], cx
    mov word [ebp + VM86_CTX32.regIp], si
    jmp .done
.g5_jmp_n:
    mov ax, word [edi]
    mov word [ebp + VM86_CTX32.regIp], ax
    jmp .done
.g5_jmp_f:
    mov ax, word [edi]
    mov word [ebp + VM86_CTX32.regIp], ax
    mov ax, word [edi + 2]
    mov word [ebp + VM86_CTX32.sregs16 + 2], ax

.done:
    pop edi
    pop esi
    pop ebx
    ret
@endproc

;; @proc vm86_run_image32
;; @abi win32
;; @in ESI = .COM or DOS MZ .EXE file path, EDI = raw command tail (e.g. " arg1 arg2")
;; @out EAX = 8-bit DOS child exit code, CF=0; or CF=1, EAX=ERR_* if not a valid 16-bit DOS image
;; @err CF=1, EAX=ERR_*
;; @clobbers eax, ecx, edx
;; @desc Loads a 16-bit .COM or relocated MZ .EXE into a 1 MiB + 64 KiB vm8086 space and executes to exit.
@proc vm86_run_image32
    push ebx
    push esi
    push edi
    push ebp
    sub esp, 192
    mov ebp, esp

    ; Zero VM86_CTX32 (192 bytes = 48 dwords)
    push edi
    mov edi, ebp
    xor eax, eax
    mov ecx, 48
    rep stosd
    pop edi

    ; Allocate 1 MiB + 64 KiB guest address space
    mov ecx, VM86_MEM_BYTES
    call perm_alloc32
    jc .fail_no_mem
    mov dword [ebp + VM86_CTX32.pMem], eax

    ; Initialize guest handle table: 0=stdin, 1=stdout, 2=stderr, 3..19=INVALID
    xor ecx, ecx
.init_h:
    mov dword [ebp + VM86_CTX32.hostHandles + ecx * 4], INVALID_HANDLE_VALUE
    inc ecx
    cmp ecx, VM86_MAX_HANDLES
    jb .init_h

    push STD_INPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + VM86_CTX32.hostHandles], eax
    push STD_OUTPUT_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + VM86_CTX32.hostHandles + 4], eax
    push STD_ERROR_HANDLE
    call dword [__imp__GetStdHandle@4]
    mov dword [ebp + VM86_CTX32.hostHandles + 8], eax

    ; Copy environment block into guest segment 0x0060 (linear 0x00600, max 16 KiB)
    push esi
    push edi
    mov esi, dword [g_env_buf]
    mov edi, dword [ebp + VM86_CTX32.pMem]
    add edi, 0x00600
    mov ecx, dword [g_env_used]
    cmp ecx, 0x3F00
    jbe .cp_env
    mov ecx, 0x3F00
.cp_env:
    rep movsb
    mov word [edi], 0
    pop edi
    pop esi

    ; Build 256-byte PSP at segment 0x1000 (linear 0x10000)
    mov word [ebp + VM86_CTX32.wPspSeg], 0x1000
    mov word [ebp + VM86_CTX32.wDtaSeg], 0x1000
    mov word [ebp + VM86_CTX32.wDtaOff], 0x0080
    mov ebx, dword [ebp + VM86_CTX32.pMem]
    add ebx, 0x10000
    mov word [ebx], 0x20CD         ; INT 20h
    mov word [ebx + 2], 0x9FFF     ; wMemTop
    mov word [ebx + 0x2C], 0x0060  ; wEnvSeg

    ; Populate PSP:0x80 command tail (up to 126 chars + 0x0D)
    push esi
    mov esi, edi
    xor ecx, ecx
    test esi, esi
    jz .tail_done
.tail_lp:
    cmp ecx, 126
    jae .tail_done
    mov al, byte [esi + ecx]
    test al, al
    jz .tail_done
    mov byte [ebx + 0x81 + ecx], al
    inc ecx
    jmp .tail_lp
.tail_done:
    mov byte [ebx + 0x80], cl
    mov byte [ebx + 0x81 + ecx], 0x0D
    pop esi

    ; Open image file and read into guest memory at 0x10100 (up to 512 KiB)
    mov ecx, GENERIC_READ
    mov edx, OPEN_EXISTING
    xor eax, eax
    call hal_open_file32
    jc .free_err
    mov ebx, eax ; hFile

    mov edi, dword [ebp + VM86_CTX32.pMem]
    add edi, 0x10100
    sub esp, 4
    mov edx, esp
    mov dword [edx], 0
    push 0
    push edx
    push 0x7FE00
    push edi
    push ebx
    call dword [__imp__ReadFile@20]
    pop esi ; bytes_read
    push ebx
    call dword [__imp__CloseHandle@4]
    test esi, esi
    jz .bad_img

    ; Check 'MZ' (0x5A4D) or 'ZM' (0x4D5A) header vs flat .COM!
    mov ax, word [edi]
    cmp ax, 0x5A4D
    je .load_mz
    cmp ax, 0x4D5A
    je .load_mz

    ; --- Flat .COM setup (CS=DS=ES=SS=0x1000, IP=0x0100, SP=0xFFFE, [SS:FFFE]=0x0000) ---
    mov word [ebp + VM86_CTX32.sregs16], 0x1000     ; ES
    mov word [ebp + VM86_CTX32.sregs16 + 2], 0x1000 ; CS
    mov word [ebp + VM86_CTX32.sregs16 + 4], 0x1000 ; SS
    mov word [ebp + VM86_CTX32.sregs16 + 6], 0x1000 ; DS
    mov word [ebp + VM86_CTX32.regIp], 0x0100
    mov word [ebp + VM86_CTX32.regs16 + 8], 0xFFFE  ; SP
    mov word [ebp + VM86_CTX32.regFlags], 0x0202
    jmp .run_loop

.load_mz:
    cmp esi, 28
    jb .bad_img
    ; Reject PE ('PE\0\0') or NE ('NE') headers if e_lfanew points to one!
    cmp esi, 0x40
    jb .reloc_mz
    cmp word [edi + 0x18], 0x0040
    jb .reloc_mz
    mov eax, dword [edi + 0x3C]
    cmp eax, esi
    jae .reloc_mz
    cmp word [edi + eax], 0x4550 ; 'PE'
    je .bad_img
    cmp word [edi + eax], 0x454E ; 'NE'
    je .bad_img

.reloc_mz:
    ; Load module image at segment 0x1010 (right above PSP 0x1000): shift body down from [edi + hdr_bytes] to [edi]!
    movzx ecx, word [edi + 0x08] ; e_cparhdr
    shl ecx, 4 ; hdr_bytes
    cmp ecx, esi
    jae .bad_img

    ; Save MZ header fields before shifting image in-place:
    ; e_crlc (+0x06), e_ss (+0x0E), e_sp (+0x10), e_ip (+0x14), e_cs (+0x16), e_lfarlc (+0x18)
    movzx eax, word [edi + 0x0E]
    add eax, 0x1010
    mov word [ebp + VM86_CTX32.sregs16 + 4], ax ; SS
    mov ax, word [edi + 0x10]
    mov word [ebp + VM86_CTX32.regs16 + 8], ax  ; SP
    mov ax, word [edi + 0x14]
    mov word [ebp + VM86_CTX32.regIp], ax       ; IP
    movzx eax, word [edi + 0x16]
    add eax, 0x1010
    mov word [ebp + VM86_CTX32.sregs16 + 2], ax ; CS
    mov word [ebp + VM86_CTX32.sregs16], 0x1000     ; ES = PSP
    mov word [ebp + VM86_CTX32.sregs16 + 6], 0x1000 ; DS = PSP
    mov word [ebp + VM86_CTX32.regFlags], 0x0202

    ; Copy relocation table to arena before shifting body
    movzx edx, word [edi + 0x06] ; reloc count
    movzx ebx, word [edi + 0x18] ; reloc table offset
    push ecx ; hdr_bytes
    push edx ; reloc_cnt
    test edx, edx
    jz .no_relocs_alloc
    lea ecx, [edx * 4 + 8]
    call arena_alloc32
    jc .pop2_bad
    push edi
    push esi
    lea esi, [edi + ebx]
    mov ecx, dword [esp + 8] ; reloc_cnt
    mov edi, eax
    mov ebx, eax ; reloc_copy
    rep movsd
    pop esi
    pop edi
.no_relocs_alloc:
    pop edx ; reloc_cnt
    pop ecx ; hdr_bytes

    ; Shift body [edi + ecx .. edi + esi) -> [edi ..)
    push edx ; reloc_cnt
    sub esi, ecx ; body_len
    mov eax, ecx ; hdr_bytes
    mov ecx, esi ; body_len
    lea esi, [edi + eax]
    push edi
    rep movsb
    pop edi ; module base (linear 0x10100 = seg 0x1010)
    pop ecx ; reloc_cnt
    jmp .rel_lp

.pop2_bad:
    pop edx
    pop ecx
    jmp .bad_img

    ; Apply segment relocations (+0x1010)
.rel_lp:
    test ecx, ecx
    jz .run_loop
    movzx edx, word [ebx]     ; off
    movzx eax, word [ebx + 2] ; seg
    shl eax, 4
    add eax, edx
    add word [edi + eax], 0x1010
    add ebx, 4
    dec ecx
    jmp .rel_lp

.run_loop:
    mov dword [ebp + VM86_CTX32.dwStepsLeft], 50000000 ; 50M instruction watchdog
.step_lp:
    cmp byte [ebp + VM86_CTX32.bHalted], 0
    jne .done_ok
    cmp dword [g_ctrl_break], 0
    jne .ctrl_c_halt
    dec dword [ebp + VM86_CTX32.dwStepsLeft]
    jz .done_ok
    call vm86_step_one32
    jmp .step_lp

.ctrl_c_halt:
    mov byte [ebp + VM86_CTX32.bExitCode], 0xFF

.done_ok:
    ; Close any open guest file handles >= 5
    mov ebx, 5
.cl_g:
    cmp ebx, VM86_MAX_HANDLES
    jae .free_mem_ok
    mov eax, dword [ebp + VM86_CTX32.hostHandles + ebx * 4]
    cmp eax, INVALID_HANDLE_VALUE
    je .nx_g
    push eax
    call dword [__imp__CloseHandle@4]
.nx_g:
    inc ebx
    jmp .cl_g

.free_mem_ok:
    movzx ebx, byte [ebp + VM86_CTX32.bExitCode]
    mov eax, dword [ebp + VM86_CTX32.pMem]
    call perm_free32
    mov eax, ebx
    add esp, 192
    pop ebp
    pop edi
    pop esi
    pop ebx
    clc
    ret

.bad_img:
    mov eax, ERR_BAD_COMMAND
    jmp .free_err
.free_err:
    push eax
    mov eax, dword [ebp + VM86_CTX32.pMem]
    call perm_free32
    pop eax
.fail_no_mem:
    add esp, 192
    pop ebp
    pop edi
    pop esi
    pop ebx
    stc
    ret
@endproc
