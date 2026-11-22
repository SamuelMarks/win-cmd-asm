;; @file vm86_exec64.inc
;; @isa x64
;; @desc 8086 CPU instruction interpreter and .COM / MZ .EXE PSP loader for x86-64 embedded vm8086.

@cpu x64
@format inc

;; @proc vm86_check_jcc64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DL = opcode low nibble (0..15)
;; @out EAX = 1 if branch taken, 0 otherwise
;; @err none
;; @clobbers rax, rdx, r8, r9
;; @desc Evaluates all 16 Intel 8086 Jcc condition predicates including odd-nibble negation.
@proc vm86_check_jcc64
    movzx r8d, word [rcx + VM86_CTX64.regFlags]
    mov r9d, edx
    and r9d, 1 ; invert bit
    shr dl, 1
    xor eax, eax
    cmp dl, 0
    je .c_o
    cmp dl, 1
    je .c_b
    cmp dl, 2
    je .c_z
    cmp dl, 3
    je .c_be
    cmp dl, 4
    je .c_s
    cmp dl, 5
    je .c_p
    cmp dl, 6
    je .c_l
    test r8d, 0x0040
    jnz .set1
    jmp .c_l
.c_o:
    test r8d, 0x0800
    setnz al
    jmp .inv
.c_b:
    test r8d, 0x0001
    setnz al
    jmp .inv
.c_z:
    test r8d, 0x0040
    setnz al
    jmp .inv
.c_be:
    test r8d, 0x0041
    setnz al
    jmp .inv
.c_s:
    test r8d, 0x0080
    setnz al
    jmp .inv
.c_p:
    test r8d, 0x0004
    setnz al
    jmp .inv
.c_l:
    mov edx, r8d
    shr r8d, 7
    shr edx, 11
    xor r8d, edx
    and r8d, 1
    mov eax, r8d
    jmp .inv
.set1:
    mov eax, 1
.inv:
    xor eax, r9d
    ret
@endproc

;; @proc vm86_step_str_op64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*, DL = string opcode (0xA4..0xAF)
;; @out Executes one or REP-counted string iteration (MOVS, CMPS, STOS, LODS, SCAS)
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Executes 8086 string primitives honoring DF, segment overrides, REP (0xF3), and REPNE (0xF2).
@proc_frame vm86_step_str_op64, 48, rbp, rbx, rsi, rdi, r12
    mov rbp, rcx
    movzx ebx, dl

.rep_lp:
    cmp byte [rbp + VM86_CTX64.bRepMode], 0
    je .one_iter
    cmp word [rbp + VM86_CTX64.regs16 + 2], 0
    je .done

.one_iter:
    mov esi, 1
    test bl, 1
    jz .got_sz
    mov esi, 2
.got_sz:
    test word [rbp + VM86_CTX64.regFlags], 0x0400
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
    mov rcx, rbp
    mov dl, 3
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    mov rcx, rbp
    call vm86_lin64
    movzx r12d, byte [rax]
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    mov byte [rax], r12b
    add word [rbp + VM86_CTX64.regs16 + 12], si
    add word [rbp + VM86_CTX64.regs16 + 14], si
    jmp .rep_post_no_zf

.movsw_op:
    mov rcx, rbp
    mov dl, 3
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    mov rcx, rbp
    call vm86_lin64
    movzx r12d, word [rax]
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    mov word [rax], r12w
    add word [rbp + VM86_CTX64.regs16 + 12], si
    add word [rbp + VM86_CTX64.regs16 + 14], si
    jmp .rep_post_no_zf

.stosb_op:
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    mov dl, byte [rbp + VM86_CTX64.regs16]
    mov byte [rax], dl
    add word [rbp + VM86_CTX64.regs16 + 14], si
    jmp .rep_post_no_zf

.stosw_op:
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    mov dx, word [rbp + VM86_CTX64.regs16]
    mov word [rax], dx
    add word [rbp + VM86_CTX64.regs16 + 14], si
    jmp .rep_post_no_zf

.lodsb_op:
    mov rcx, rbp
    mov dl, 3
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    mov rcx, rbp
    call vm86_lin64
    mov dl, byte [rax]
    mov byte [rbp + VM86_CTX64.regs16], dl
    add word [rbp + VM86_CTX64.regs16 + 12], si
    jmp .rep_post_no_zf

.lodsw_op:
    mov rcx, rbp
    mov dl, 3
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    mov rcx, rbp
    call vm86_lin64
    mov dx, word [rax]
    mov word [rbp + VM86_CTX64.regs16], dx
    add word [rbp + VM86_CTX64.regs16 + 12], si
    jmp .rep_post_no_zf

.scasb_op:
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    movzx r9d, byte [rax]
    movzx r8d, byte [rbp + VM86_CTX64.regs16]
    mov rcx, rbp
    mov dl, 7
    xor r10d, r10d
    call vm86_alu16_64
    add word [rbp + VM86_CTX64.regs16 + 14], si
    jmp .rep_post_zf

.scasw_op:
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    movzx r9d, word [rax]
    movzx r8d, word [rbp + VM86_CTX64.regs16]
    mov rcx, rbp
    mov dl, 7
    mov r10b, 1
    call vm86_alu16_64
    add word [rbp + VM86_CTX64.regs16 + 14], si
    jmp .rep_post_zf

.cmpsb_op:
    mov rcx, rbp
    mov dl, 3
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    mov rcx, rbp
    call vm86_lin64
    movzx edi, byte [rax]
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    movzx r9d, byte [rax]
    mov r8d, edi
    mov rcx, rbp
    mov dl, 7
    xor r10d, r10d
    call vm86_alu16_64
    add word [rbp + VM86_CTX64.regs16 + 12], si
    add word [rbp + VM86_CTX64.regs16 + 14], si
    jmp .rep_post_zf

.cmpsw_op:
    mov rcx, rbp
    mov dl, 3
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, word [rbp + VM86_CTX64.regs16 + 12]
    mov rcx, rbp
    call vm86_lin64
    movzx edi, word [rax]
    mov dx, word [rbp + VM86_CTX64.sregs16]
    mov r8w, word [rbp + VM86_CTX64.regs16 + 14]
    mov rcx, rbp
    call vm86_lin64
    movzx r9d, word [rax]
    mov r8d, edi
    mov rcx, rbp
    mov dl, 7
    mov r10b, 1
    call vm86_alu16_64
    add word [rbp + VM86_CTX64.regs16 + 12], si
    add word [rbp + VM86_CTX64.regs16 + 14], si

.rep_post_zf:
    cmp byte [rbp + VM86_CTX64.bRepMode], 0
    je .done
    dec word [rbp + VM86_CTX64.regs16 + 2]
    jz .done
    cmp byte [rbp + VM86_CTX64.bRepMode], 0xF3
    je .chk_repe
    test word [rbp + VM86_CTX64.regFlags], 0x0040
    jz .rep_lp
    jmp .done
.chk_repe:
    test word [rbp + VM86_CTX64.regFlags], 0x0040
    jnz .rep_lp
    jmp .done

.rep_post_no_zf:
    cmp byte [rbp + VM86_CTX64.bRepMode], 0
    je .done
    dec word [rbp + VM86_CTX64.regs16 + 2]
    jnz .rep_lp

.done:
    @epilog_ret
@endproc

;; @proc vm86_step_one64
;; @abi win64_ms
;; @in RCX = VM86_CTX64*
;; @out Executes one complete 8086 instruction (including prefixes)
;; @err Sets bHalted = 1 on HLT, INT 20h, INT 21h/4Ch, or divide fault
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Decodes and executes a single 8086 instruction inside VM86_CTX64 with zero mid-frame RSP mutations.
@proc_frame vm86_step_one64, 56, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 56 = 40 + 16
    mov rbp, rcx
    mov byte [rbp + VM86_CTX64.bSegOvr], 0xFF
    mov byte [rbp + VM86_CTX64.bRepMode], 0

.prefix_lp:
    mov rcx, rbp
    call vm86_fetch8_64
    movzx ebx, al
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
    cmp bl, 0xF0
    je .prefix_lp
    jmp .dispatch

.pre_es:
    mov byte [rbp + VM86_CTX64.bSegOvr], 0
    jmp .prefix_lp
.pre_cs:
    mov byte [rbp + VM86_CTX64.bSegOvr], 1
    jmp .prefix_lp
.pre_ss:
    mov byte [rbp + VM86_CTX64.bSegOvr], 2
    jmp .prefix_lp
.pre_ds:
    mov byte [rbp + VM86_CTX64.bSegOvr], 3
    jmp .prefix_lp
.pre_rep:
    mov byte [rbp + VM86_CTX64.bRepMode], bl
    jmp .prefix_lp

.dispatch:
    cmp bl, 0x3F
    ja .above_3f
    mov eax, ebx
    and eax, 7
    cmp eax, 6
    je .push_sreg
    cmp eax, 7
    je .pop_sreg
    cmp eax, 4
    je .alu_al_imm8
    cmp eax, 5
    je .alu_ax_imm16

    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax ; EA ptr
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_reg_ptr64
    mov rsi, rax ; reg ptr

    mov r12d, ebx
    shr r12d, 3
    and r12d, 7 ; alu_op
    mov r13d, ebx
    and r13d, 1 ; is_word

    test bl, 2
    jnz .alu_to_reg
    test r13b, r13b
    jnz .alu_mr16
    movzx r8d, byte [rdi]
    movzx r9d, byte [rsi]
    mov rcx, rbp
    mov dl, r12b
    xor r10d, r10d
    call vm86_alu16_64
    cmp r12d, 7
    je .done
    mov byte [rdi], al
    jmp .done
.alu_mr16:
    movzx r8d, word [rdi]
    movzx r9d, word [rsi]
    mov rcx, rbp
    mov dl, r12b
    mov r10b, 1
    call vm86_alu16_64
    cmp r12d, 7
    je .done
    mov word [rdi], ax
    jmp .done

.alu_to_reg:
    test r13b, r13b
    jnz .alu_rm16
    movzx r8d, byte [rsi]
    movzx r9d, byte [rdi]
    mov rcx, rbp
    mov dl, r12b
    xor r10d, r10d
    call vm86_alu16_64
    cmp r12d, 7
    je .done
    mov byte [rsi], al
    jmp .done
.alu_rm16:
    movzx r8d, word [rsi]
    movzx r9d, word [rdi]
    mov rcx, rbp
    mov dl, r12b
    mov r10b, 1
    call vm86_alu16_64
    cmp r12d, 7
    je .done
    mov word [rsi], ax
    jmp .done

.alu_al_imm8:
    mov rcx, rbp
    call vm86_fetch8_64
    movzx r9d, al
    movzx r8d, byte [rbp + VM86_CTX64.regs16]
    mov r12d, ebx
    shr r12d, 3
    and r12d, 7
    mov rcx, rbp
    mov dl, r12b
    xor r10d, r10d
    call vm86_alu16_64
    cmp r12d, 7
    je .done
    mov byte [rbp + VM86_CTX64.regs16], al
    jmp .done

.alu_ax_imm16:
    mov rcx, rbp
    call vm86_fetch16_64
    movzx r9d, ax
    movzx r8d, word [rbp + VM86_CTX64.regs16]
    mov r12d, ebx
    shr r12d, 3
    and r12d, 7
    mov rcx, rbp
    mov dl, r12b
    mov r10b, 1
    call vm86_alu16_64
    cmp r12d, 7
    je .done
    mov word [rbp + VM86_CTX64.regs16], ax
    jmp .done

.push_sreg:
    mov ecx, ebx
    shr ecx, 3
    and ecx, 3
    mov dx, word [rbp + VM86_CTX64.sregs16 + rcx * 2]
    mov rcx, rbp
    call vm86_push16_64
    jmp .done

.pop_sreg:
    mov esi, ebx
    shr esi, 3
    and esi, 3
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.sregs16 + rsi * 2], ax
    jmp .done

.above_3f:
    cmp bl, 0x47
    jbe .inc_r16
    cmp bl, 0x4F
    jbe .dec_r16
    cmp bl, 0x57
    jbe .push_r16
    cmp bl, 0x5F
    jbe .pop_r16
    cmp bl, 0x6F
    jbe .done
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
    je .done
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
    mov ecx, ebx
    and ecx, 7
    inc word [rbp + VM86_CTX64.regs16 + rcx * 2]
    mov dx, word [rbp + VM86_CTX64.regs16 + rcx * 2]
    mov rcx, rbp
    call vm86_set_szp16_64
    jmp .done

.dec_r16:
    mov ecx, ebx
    and ecx, 7
    dec word [rbp + VM86_CTX64.regs16 + rcx * 2]
    mov dx, word [rbp + VM86_CTX64.regs16 + rcx * 2]
    mov rcx, rbp
    call vm86_set_szp16_64
    jmp .done

.push_r16:
    mov ecx, ebx
    and ecx, 7
    mov dx, word [rbp + VM86_CTX64.regs16 + rcx * 2]
    mov rcx, rbp
    call vm86_push16_64
    jmp .done

.pop_r16:
    mov esi, ebx
    and esi, 7
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.regs16 + rsi * 2], ax
    jmp .done

.jcc_rel8:
    mov rcx, rbp
    call vm86_fetch8_64
    movsx esi, al
    mov rcx, rbp
    mov dl, bl
    and dl, 0x0F
    call vm86_check_jcc64
    test eax, eax
    jz .done
    add word [rbp + VM86_CTX64.regIp], si
    jmp .done

.grp1_imm:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    mov esi, edx ; sub-op 0..7
    cmp bl, 0x81
    je .g1_w16
    cmp bl, 0x83
    je .g1_s8
    mov rcx, rbp
    call vm86_fetch8_64
    movzx r9d, al
    movzx r8d, byte [rdi]
    mov rcx, rbp
    mov dl, sil
    xor r10d, r10d
    call vm86_alu16_64
    cmp esi, 7
    je .done
    mov byte [rdi], al
    jmp .done
.g1_w16:
    mov rcx, rbp
    call vm86_fetch16_64
    movzx r9d, ax
    jmp .g1_do16
.g1_s8:
    mov rcx, rbp
    call vm86_fetch8_64
    movsx eax, al
    movzx r9d, ax
.g1_do16:
    movzx r8d, word [rdi]
    mov rcx, rbp
    mov dl, sil
    mov r10b, 1
    call vm86_alu16_64
    cmp esi, 7
    je .done
    mov word [rdi], ax
    jmp .done

.test_rm_r:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_reg_ptr64
    mov rsi, rax
    test bl, 1
    jnz .tst16
    movzx r8d, byte [rdi]
    movzx r9d, byte [rsi]
    mov rcx, rbp
    mov dl, 4
    xor r10d, r10d
    call vm86_alu16_64
    jmp .done
.tst16:
    movzx r8d, word [rdi]
    movzx r9d, word [rsi]
    mov rcx, rbp
    mov dl, 4
    mov r10b, 1
    call vm86_alu16_64
    jmp .done

.xchg_rm_r:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_reg_ptr64
    test bl, 1
    jnz .xcg16
    mov cl, byte [rdi]
    mov dl, byte [rax]
    mov byte [rdi], dl
    mov byte [rax], cl
    jmp .done
.xcg16:
    mov cx, word [rdi]
    mov dx, word [rax]
    mov word [rdi], dx
    mov word [rax], cx
    jmp .done

.mov_rm_r:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_reg_ptr64
    test bl, 2
    jnz .mv_to_reg
    test bl, 1
    jnz .mv_mr16
    mov cl, byte [rax]
    mov byte [rdi], cl
    jmp .done
.mv_mr16:
    mov cx, word [rax]
    mov word [rdi], cx
    jmp .done
.mv_to_reg:
    test bl, 1
    jnz .mv_rm16
    mov cl, byte [rdi]
    mov byte [rax], cl
    jmp .done
.mv_rm16:
    mov cx, word [rdi]
    mov word [rax], cx
    jmp .done

.mov_rm_sreg:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    and edx, 3
    mov cx, word [rbp + VM86_CTX64.sregs16 + rdx * 2]
    mov word [rax], cx
    jmp .done

.mov_sreg_rm:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    and edx, 3
    mov cx, word [rax]
    mov word [rbp + VM86_CTX64.sregs16 + rdx * 2], cx
    jmp .done

.lea_r16:
    mov byte [rbp + VM86_CTX64.bSegOvr], 0
    mov r14w, word [rbp + VM86_CTX64.sregs16]
    mov word [rbp + VM86_CTX64.sregs16], 0
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov word [rbp + VM86_CTX64.sregs16], r14w
    sub rax, qword [rbp + VM86_CTX64.pMem]
    mov word [rbp + VM86_CTX64.regs16 + rdx * 2], ax
    jmp .done

.pop_rm16:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rdi], ax
    jmp .done

.xchg_ax_r16:
    mov ecx, ebx
    and ecx, 7
    mov ax, word [rbp + VM86_CTX64.regs16]
    mov dx, word [rbp + VM86_CTX64.regs16 + rcx * 2]
    mov word [rbp + VM86_CTX64.regs16], dx
    mov word [rbp + VM86_CTX64.regs16 + rcx * 2], ax
    jmp .done

.cbw_op:
    movsx eax, byte [rbp + VM86_CTX64.regs16]
    mov word [rbp + VM86_CTX64.regs16], ax
    jmp .done

.cwd_op:
    movsx eax, word [rbp + VM86_CTX64.regs16]
    shr eax, 16
    mov word [rbp + VM86_CTX64.regs16 + 4], ax
    jmp .done

.call_far_ptr:
    mov rcx, rbp
    call vm86_fetch16_64
    mov esi, eax
    mov rcx, rbp
    call vm86_fetch16_64
    mov edi, eax
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.sregs16 + 2]
    call vm86_push16_64
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.regIp]
    call vm86_push16_64
    mov word [rbp + VM86_CTX64.sregs16 + 2], di
    mov word [rbp + VM86_CTX64.regIp], si
    jmp .done

.pushf_op:
    mov dx, word [rbp + VM86_CTX64.regFlags]
    or dx, 0xF002
    mov rcx, rbp
    call vm86_push16_64
    jmp .done

.popf_op:
    mov rcx, rbp
    call vm86_pop16_64
    or ax, 0x0002
    mov word [rbp + VM86_CTX64.regFlags], ax
    jmp .done

.sahf_op:
    movzx eax, byte [rbp + VM86_CTX64.regs16 + 1]
    and word [rbp + VM86_CTX64.regFlags], 0xFF00
    or ax, 0x02
    or word [rbp + VM86_CTX64.regFlags], ax
    jmp .done

.lahf_op:
    mov al, byte [rbp + VM86_CTX64.regFlags]
    or al, 0x02
    mov byte [rbp + VM86_CTX64.regs16 + 1], al
    jmp .done

.mov_moffs:
    mov rcx, rbp
    call vm86_fetch16_64
    mov esi, eax
    mov rcx, rbp
    mov dl, 3
    call vm86_eff_seg64
    mov dx, ax
    mov r8w, si
    mov rcx, rbp
    call vm86_lin64
    cmp bl, 0xA0
    je .moff_a0
    cmp bl, 0xA1
    je .moff_a1
    cmp bl, 0xA2
    je .moff_a2
    mov dx, word [rbp + VM86_CTX64.regs16]
    mov word [rax], dx
    jmp .done
.moff_a0:
    mov dl, byte [rax]
    mov byte [rbp + VM86_CTX64.regs16], dl
    jmp .done
.moff_a1:
    mov dx, word [rax]
    mov word [rbp + VM86_CTX64.regs16], dx
    jmp .done
.moff_a2:
    mov dl, byte [rbp + VM86_CTX64.regs16]
    mov byte [rax], dl
    jmp .done

.str_op:
    mov rcx, rbp
    mov dl, bl
    call vm86_step_str_op64
    jmp .done

.test_acc_imm:
    test bl, 1
    jnz .tai16
    mov rcx, rbp
    call vm86_fetch8_64
    movzx r9d, al
    movzx r8d, byte [rbp + VM86_CTX64.regs16]
    mov rcx, rbp
    mov dl, 4
    xor r10d, r10d
    call vm86_alu16_64
    jmp .done
.tai16:
    mov rcx, rbp
    call vm86_fetch16_64
    movzx r9d, ax
    movzx r8d, word [rbp + VM86_CTX64.regs16]
    mov rcx, rbp
    mov dl, 4
    mov r10b, 1
    call vm86_alu16_64
    jmp .done

.mov_r8_imm:
    mov rcx, rbp
    mov edx, ebx
    and edx, 7
    xor r8d, r8d
    call vm86_reg_ptr64
    mov rdi, rax
    mov rcx, rbp
    call vm86_fetch8_64
    mov byte [rdi], al
    jmp .done

.mov_r16_imm:
    mov esi, ebx
    and esi, 7
    mov rcx, rbp
    call vm86_fetch16_64
    mov word [rbp + VM86_CTX64.regs16 + rsi * 2], ax
    jmp .done

.ret_imm16:
    mov rcx, rbp
    call vm86_fetch16_64
    mov esi, eax
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.regIp], ax
    add word [rbp + VM86_CTX64.regs16 + 8], si
    jmp .done

.ret_near:
    mov rcx, rbp
    call vm86_pop16_64
    test ax, ax
    jz .ret_zero_psp
    mov word [rbp + VM86_CTX64.regIp], ax
    jmp .done
.ret_zero_psp:
    mov byte [rbp + VM86_CTX64.bExitCode], 0
    mov byte [rbp + VM86_CTX64.bHalted], 1
    jmp .done

.les_op:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov cx, word [rax]
    mov word [rbp + VM86_CTX64.regs16 + rdx * 2], cx
    mov cx, word [rax + 2]
    mov word [rbp + VM86_CTX64.sregs16], cx
    jmp .done

.lds_op:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov cx, word [rax]
    mov word [rbp + VM86_CTX64.regs16 + rdx * 2], cx
    mov cx, word [rax + 2]
    mov word [rbp + VM86_CTX64.sregs16 + 6], cx
    jmp .done

.mov_rm_imm:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    test bl, 1
    jnz .mrmi16
    mov rcx, rbp
    call vm86_fetch8_64
    mov byte [rdi], al
    jmp .done
.mrmi16:
    mov rcx, rbp
    call vm86_fetch16_64
    mov word [rdi], ax
    jmp .done

.retf_imm16:
    mov rcx, rbp
    call vm86_fetch16_64
    mov esi, eax
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.regIp], ax
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.sregs16 + 2], ax
    add word [rbp + VM86_CTX64.regs16 + 8], si
    jmp .done

.retf_op:
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.regIp], ax
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.sregs16 + 2], ax
    jmp .done

.int_imm8:
    mov rcx, rbp
    call vm86_fetch8_64
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
    mov byte [rbp + VM86_CTX64.bExitCode], 0
    mov byte [rbp + VM86_CTX64.bHalted], 1
    jmp .done
.int21:
    mov rcx, rbp
    call vm86_dos_int21_64
    jmp .done
.int10:
    cmp byte [rbp + VM86_CTX64.regs16 + 1], 0x0E
    jne .done
    mov al, byte [rbp + VM86_CTX64.regs16]
    mov byte [rsp + 32], al
    lea rcx, [rsp + 32]
    mov edx, 1
    call con_write_out64
    jmp .done
.int1a:
    mov word [rbp + VM86_CTX64.regs16 + 2], 0
    mov word [rbp + VM86_CTX64.regs16 + 4], 100
    mov byte [rbp + VM86_CTX64.regs16], 0
    jmp .done
.int2f:
    mov word [rbp + VM86_CTX64.regs16], 0
    jmp .done

.iret_op:
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.regIp], ax
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.sregs16 + 2], ax
    mov rcx, rbp
    call vm86_pop16_64
    mov word [rbp + VM86_CTX64.regFlags], ax
    jmp .done

.grp2_shift:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    mov esi, edx
    mov cl, 1
    test bl, 2
    jz .sh_cnt_ready
    mov cl, byte [rbp + VM86_CTX64.regs16 + 2]
.sh_cnt_ready:
    and cl, 31
    jz .done
    test bl, 1
    jnz .sh16
    mov al, byte [rdi]
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
    setc dl
    mov byte [rdi], al
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    movzx edx, dl
    or word [rbp + VM86_CTX64.regFlags], dx
    jmp .done
.ror8:
    ror al, cl
    setc dl
    mov byte [rdi], al
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    movzx edx, dl
    or word [rbp + VM86_CTX64.regFlags], dx
    jmp .done
.shl8:
    shl al, cl
    jmp .st_sh8
.shr8:
    shr al, cl
    jmp .st_sh8
.sar8:
    sar al, cl
.st_sh8:
    setc dl
    mov byte [rdi], al
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    movzx edx, dl
    or word [rbp + VM86_CTX64.regFlags], dx
    mov rcx, rbp
    mov dl, al
    call vm86_set_szp8_64
    jmp .done

.sh16:
    mov ax, word [rdi]
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
    setc dl
    mov word [rdi], ax
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    movzx edx, dl
    or word [rbp + VM86_CTX64.regFlags], dx
    jmp .done
.ror16:
    ror ax, cl
    setc dl
    mov word [rdi], ax
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    movzx edx, dl
    or word [rbp + VM86_CTX64.regFlags], dx
    jmp .done
.shl16:
    shl ax, cl
    jmp .st_sh16
.shr16:
    shr ax, cl
    jmp .st_sh16
.sar16:
    sar ax, cl
.st_sh16:
    setc dl
    mov word [rdi], ax
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    movzx edx, dl
    or word [rbp + VM86_CTX64.regFlags], dx
    mov rcx, rbp
    mov dx, ax
    call vm86_set_szp16_64
    jmp .done

.loop_jcxz:
    mov rcx, rbp
    call vm86_fetch8_64
    movsx esi, al
    cmp bl, 0xE3
    je .jcxz_chk
    dec word [rbp + VM86_CTX64.regs16 + 2]
    jz .done
    cmp bl, 0xE2
    je .take_rel
    cmp bl, 0xE1
    je .loope_chk
    test word [rbp + VM86_CTX64.regFlags], 0x0040
    jz .take_rel
    jmp .done
.loope_chk:
    test word [rbp + VM86_CTX64.regFlags], 0x0040
    jnz .take_rel
    jmp .done
.jcxz_chk:
    cmp word [rbp + VM86_CTX64.regs16 + 2], 0
    jne .done
.take_rel:
    add word [rbp + VM86_CTX64.regIp], si
    jmp .done

.call_rel16:
    mov rcx, rbp
    call vm86_fetch16_64
    mov esi, eax
    mov rcx, rbp
    mov dx, word [rbp + VM86_CTX64.regIp]
    call vm86_push16_64
    add word [rbp + VM86_CTX64.regIp], si
    jmp .done

.jmp_rel16:
    mov rcx, rbp
    call vm86_fetch16_64
    add word [rbp + VM86_CTX64.regIp], ax
    jmp .done

.jmp_far_ptr:
    mov rcx, rbp
    call vm86_fetch16_64
    mov esi, eax
    mov rcx, rbp
    call vm86_fetch16_64
    mov word [rbp + VM86_CTX64.regIp], si
    mov word [rbp + VM86_CTX64.sregs16 + 2], ax
    jmp .done

.jmp_rel8:
    mov rcx, rbp
    call vm86_fetch8_64
    movsx eax, al
    add word [rbp + VM86_CTX64.regIp], ax
    jmp .done

.hlt_op:
    mov byte [rbp + VM86_CTX64.bHalted], 1
    jmp .done

.cmc_op:
    xor word [rbp + VM86_CTX64.regFlags], 1
    jmp .done

.grp3_op:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    mov esi, edx
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
    mov rcx, rbp
    call vm86_fetch8_64
    movzx r9d, al
    movzx r8d, byte [rdi]
    mov rcx, rbp
    mov dl, 4
    xor r10d, r10d
    call vm86_alu16_64
    jmp .done
.g3_not8:
    not byte [rdi]
    jmp .done
.g3_neg8:
    xor r8d, r8d
    movzx r9d, byte [rdi]
    mov rcx, rbp
    mov dl, 5
    xor r10d, r10d
    call vm86_alu16_64
    mov byte [rdi], al
    jmp .done
.g3_mul8:
    movzx eax, byte [rbp + VM86_CTX64.regs16]
    movzx ecx, byte [rdi]
    imul eax, ecx
    mov word [rbp + VM86_CTX64.regs16], ax
    jmp .done
.g3_imul8:
    movsx eax, byte [rbp + VM86_CTX64.regs16]
    movsx ecx, byte [rdi]
    imul eax, ecx
    mov word [rbp + VM86_CTX64.regs16], ax
    jmp .done
.g3_div8:
    movzx eax, word [rbp + VM86_CTX64.regs16]
    movzx ecx, byte [rdi]
    test ecx, ecx
    jz .hlt_op
    xor edx, edx
    div ecx
    mov byte [rbp + VM86_CTX64.regs16], al
    mov byte [rbp + VM86_CTX64.regs16 + 1], dl
    jmp .done
.g3_idiv8:
    movsx eax, word [rbp + VM86_CTX64.regs16]
    movsx ecx, byte [rdi]
    test ecx, ecx
    jz .hlt_op
    cdq
    idiv ecx
    mov byte [rbp + VM86_CTX64.regs16], al
    mov byte [rbp + VM86_CTX64.regs16 + 1], dl
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
    mov rcx, rbp
    call vm86_fetch16_64
    movzx r9d, ax
    movzx r8d, word [rdi]
    mov rcx, rbp
    mov dl, 4
    mov r10b, 1
    call vm86_alu16_64
    jmp .done
.g3_not16:
    not word [rdi]
    jmp .done
.g3_neg16:
    xor r8d, r8d
    movzx r9d, word [rdi]
    mov rcx, rbp
    mov dl, 5
    mov r10b, 1
    call vm86_alu16_64
    mov word [rdi], ax
    jmp .done
.g3_mul16:
    movzx eax, word [rbp + VM86_CTX64.regs16]
    movzx ecx, word [rdi]
    mul ecx
    mov word [rbp + VM86_CTX64.regs16], ax
    shr eax, 16
    mov word [rbp + VM86_CTX64.regs16 + 4], ax
    jmp .done
.g3_imul16:
    movsx eax, word [rbp + VM86_CTX64.regs16]
    movsx ecx, word [rdi]
    imul eax, ecx
    mov word [rbp + VM86_CTX64.regs16], ax
    shr eax, 16
    mov word [rbp + VM86_CTX64.regs16 + 4], ax
    jmp .done
.g3_div16:
    movzx eax, word [rbp + VM86_CTX64.regs16 + 4]
    shl eax, 16
    mov ax, word [rbp + VM86_CTX64.regs16]
    movzx ecx, word [rdi]
    test ecx, ecx
    jz .hlt_op
    xor edx, edx
    div ecx
    mov word [rbp + VM86_CTX64.regs16], ax
    mov word [rbp + VM86_CTX64.regs16 + 4], dx
    jmp .done
.g3_idiv16:
    movzx eax, word [rbp + VM86_CTX64.regs16 + 4]
    shl eax, 16
    mov ax, word [rbp + VM86_CTX64.regs16]
    movsx ecx, word [rdi]
    test ecx, ecx
    jz .hlt_op
    cdq
    idiv ecx
    mov word [rbp + VM86_CTX64.regs16], ax
    mov word [rbp + VM86_CTX64.regs16 + 4], dx
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
    and word [rbp + VM86_CTX64.regFlags], 0xFFFE
    jmp .done
.f_stc:
    or word [rbp + VM86_CTX64.regFlags], 0x0001
    jmp .done
.f_cld:
    and word [rbp + VM86_CTX64.regFlags], 0xFBFF
    jmp .done
.f_std:
    or word [rbp + VM86_CTX64.regFlags], 0x0400
    jmp .done

.grp45_op:
    mov rcx, rbp
    call vm86_fetch8_64
    mov dl, al
    mov r8b, bl
    and r8b, 1
    mov rcx, rbp
    call vm86_decode_modrm64
    mov rdi, rax
    test bl, 1
    jnz .g5_16
    cmp edx, 0
    je .g4_inc8
    dec byte [rdi]
    mov dl, byte [rdi]
    mov rcx, rbp
    call vm86_set_szp8_64
    jmp .done
.g4_inc8:
    inc byte [rdi]
    mov dl, byte [rdi]
    mov rcx, rbp
    call vm86_set_szp8_64
    jmp .done

.g5_16:
    cmp edx, 0
    je .g5_inc
    cmp edx, 1
    je .g5_dec
    cmp edx, 2
    je .g5_call_n
    cmp edx, 3
    je .g5_call_f
    cmp edx, 4
    je .g5_jmp_n
    cmp edx, 5
    je .g5_jmp_f
    mov dx, word [rdi]
    mov rcx, rbp
    call vm86_push16_64
    jmp .done
.g5_inc:
    inc word [rdi]
    mov dx, word [rdi]
    mov rcx, rbp
    call vm86_set_szp16_64
    jmp .done
.g5_dec:
    dec word [rdi]
    mov dx, word [rdi]
    mov rcx, rbp
    call vm86_set_szp16_64
    jmp .done
.g5_call_n:
    movzx esi, word [rdi]
    mov dx, word [rbp + VM86_CTX64.regIp]
    mov rcx, rbp
    call vm86_push16_64
    mov word [rbp + VM86_CTX64.regIp], si
    jmp .done
.g5_call_f:
    movzx esi, word [rdi]
    movzx r15d, word [rdi + 2]
    mov dx, word [rbp + VM86_CTX64.sregs16 + 2]
    mov rcx, rbp
    call vm86_push16_64
    mov dx, word [rbp + VM86_CTX64.regIp]
    mov rcx, rbp
    call vm86_push16_64
    mov word [rbp + VM86_CTX64.sregs16 + 2], r15w
    mov word [rbp + VM86_CTX64.regIp], si
    jmp .done
.g5_jmp_n:
    mov ax, word [rdi]
    mov word [rbp + VM86_CTX64.regIp], ax
    jmp .done
.g5_jmp_f:
    mov ax, word [rdi]
    mov word [rbp + VM86_CTX64.regIp], ax
    mov ax, word [rdi + 2]
    mov word [rbp + VM86_CTX64.sregs16 + 2], ax

.done:
    @epilog_ret
@endproc

;; @proc vm86_run_image64
;; @abi win64_ms
;; @in RCX = .COM or DOS MZ .EXE file path, RDX = raw command tail (e.g. " arg1 arg2")
;; @out EAX = 8-bit DOS child exit code, CF=0; or CF=1, EAX=ERR_* if not a valid 16-bit DOS image
;; @err CF=1, EAX=ERR_*
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Loads a 16-bit .COM or relocated MZ .EXE into a 1 MiB + 64 KiB vm8086 space and executes to exit on 64-bit Windows!
@proc_frame vm86_run_image64, 328, rbp, rbx, rsi, rdi, r12, r13, r14, r15
    ; 8 saved regs (even) -> 328 = 40 + 18*16
    ; [rsp + 48..303] : VM86_CTX64 (256 bytes, rbp)
    ; [rsp + 304]     : bytes_read (dword)
    mov r12, rcx ; image path
    mov r13, rdx ; cmd tail
    lea rbp, [rsp + 48]

    mov rdi, rbp
    xor eax, eax
    mov ecx, 32
    rep stosq

    mov ecx, VM86_MEM_BYTES64
    call perm_alloc64
    jc .fail_no_mem
    mov qword [rbp + VM86_CTX64.pMem], rax

    xor ecx, ecx
.init_h:
    mov qword [rcx * 8 + rbp + VM86_CTX64.hostHandles], INVALID_HANDLE_VALUE
    inc ecx
    cmp ecx, VM86_MAX_HANDLES64
    jb .init_h

    mov ecx, STD_INPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + VM86_CTX64.hostHandles], rax
    mov ecx, STD_OUTPUT_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + VM86_CTX64.hostHandles + 8], rax
    mov ecx, STD_ERROR_HANDLE
    call qword [rel __imp_GetStdHandle]
    mov qword [rbp + VM86_CTX64.hostHandles + 16], rax

    mov rsi, qword [rel g_env_buf]
    mov rdi, qword [rbp + VM86_CTX64.pMem]
    add rdi, 0x00600
    mov rcx, qword [rel g_env_used]
    cmp rcx, 0x3F00
    jbe .cp_env
    mov ecx, 0x3F00
.cp_env:
    rep movsb
    mov word [rdi], 0

    mov word [rbp + VM86_CTX64.wPspSeg], 0x1000
    mov word [rbp + VM86_CTX64.wDtaSeg], 0x1000
    mov word [rbp + VM86_CTX64.wDtaOff], 0x0080
    mov rbx, qword [rbp + VM86_CTX64.pMem]
    add rbx, 0x10000
    mov word [rbx], 0x20CD
    mov word [rbx + 2], 0x9FFF
    mov word [rbx + 0x2C], 0x0060

    xor ecx, ecx
    test r13, r13
    jz .tail_done
.tail_lp:
    cmp ecx, 126
    jae .tail_done
    mov al, byte [r13 + rcx]
    test al, al
    jz .tail_done
    mov byte [rbx + 0x81 + rcx], al
    inc ecx
    jmp .tail_lp
.tail_done:
    mov byte [rbx + 0x80], cl
    mov byte [rbx + 0x81 + rcx], 0x0D

    mov rcx, r12
    mov edx, GENERIC_READ
    mov r8d, OPEN_EXISTING
    xor r9d, r9d
    call hal_open_file64
    jc .free_err
    mov rbx, rax ; hFile

    mov rdi, qword [rbp + VM86_CTX64.pMem]
    add rdi, 0x10100
    mov dword [rsp + 304], 0
    mov rcx, rbx
    mov rdx, rdi
    mov r8d, 0x7FE00
    lea r9, [rsp + 304]
    mov qword [rsp + 32], 0
    call qword [rel __imp_ReadFile]
    mov esi, dword [rsp + 304]
    mov rcx, rbx
    call qword [rel __imp_CloseHandle]
    test esi, esi
    jz .bad_img

    mov ax, word [rdi]
    cmp ax, 0x5A4D
    je .load_mz
    cmp ax, 0x4D5A
    je .load_mz

    mov word [rbp + VM86_CTX64.sregs16], 0x1000
    mov word [rbp + VM86_CTX64.sregs16 + 2], 0x1000
    mov word [rbp + VM86_CTX64.sregs16 + 4], 0x1000
    mov word [rbp + VM86_CTX64.sregs16 + 6], 0x1000
    mov word [rbp + VM86_CTX64.regIp], 0x0100
    mov word [rbp + VM86_CTX64.regs16 + 8], 0xFFFE
    mov word [rbp + VM86_CTX64.regFlags], 0x0202
    jmp .run_loop

.load_mz:
    cmp esi, 28
    jb .bad_img
    cmp esi, 0x40
    jb .reloc_mz
    cmp word [rdi + 0x18], 0x0040
    jb .reloc_mz
    mov eax, dword [rdi + 0x3C]
    cmp eax, esi
    jae .reloc_mz
    cmp word [rdi + rax], 0x4550 ; 'PE'
    je .bad_img
    cmp word [rdi + rax], 0x454E ; 'NE'
    je .bad_img

.reloc_mz:
    movzx r12d, word [rdi + 0x08] ; e_cparhdr
    shl r12d, 4 ; hdr_bytes
    cmp r12d, esi
    jae .bad_img

    movzx eax, word [rdi + 0x0E]
    add eax, 0x1010
    mov word [rbp + VM86_CTX64.sregs16 + 4], ax
    mov ax, word [rdi + 0x10]
    mov word [rbp + VM86_CTX64.regs16 + 8], ax
    mov ax, word [rdi + 0x14]
    mov word [rbp + VM86_CTX64.regIp], ax
    movzx eax, word [rdi + 0x16]
    add eax, 0x1010
    mov word [rbp + VM86_CTX64.sregs16 + 2], ax
    mov word [rbp + VM86_CTX64.sregs16], 0x1000
    mov word [rbp + VM86_CTX64.sregs16 + 6], 0x1000
    mov word [rbp + VM86_CTX64.regFlags], 0x0202

    movzx r13d, word [rdi + 0x06] ; reloc_cnt
    movzx r14d, word [rdi + 0x18] ; reloc_off
    xor ebx, ebx
    test r13d, r13d
    jz .shift_body
    lea rcx, [r13 * 4 + 8]
    call arena_alloc64
    jc .bad_img
    mov rbx, rax
    mov r15, rdi
    mov r8, rsi
    lea rsi, [rdi + r14]
    mov rdi, rax
    mov rcx, r13
    rep movsd
    mov rdi, r15
    mov rsi, r8

.shift_body:
    sub esi, r12d ; body_len
    mov r15, rdi
    mov ecx, esi
    lea rsi, [rdi + r12]
    rep movsb
    mov rdi, r15

    mov rcx, r13
.rel_lp:
    test rcx, rcx
    jz .run_loop
    movzx edx, word [rbx]
    movzx eax, word [rbx + 2]
    shl eax, 4
    add eax, edx
    add word [rdi + rax], 0x1010
    add rbx, 4
    dec rcx
    jmp .rel_lp

.run_loop:
    mov dword [rbp + VM86_CTX64.dwStepsLeft], 50000000
.step_lp:
    cmp byte [rbp + VM86_CTX64.bHalted], 0
    jne .done_ok
    cmp dword [rel g_ctrl_break], 0
    jne .ctrl_c_halt
    dec dword [rbp + VM86_CTX64.dwStepsLeft]
    jz .done_ok
    mov rcx, rbp
    call vm86_step_one64
    jmp .step_lp

.ctrl_c_halt:
    mov byte [rbp + VM86_CTX64.bExitCode], 0xFF

.done_ok:
    mov ebx, 5
.cl_g:
    cmp ebx, VM86_MAX_HANDLES64
    jae .free_mem_ok
    mov rcx, qword [rbx * 8 + rbp + VM86_CTX64.hostHandles]
    cmp rcx, INVALID_HANDLE_VALUE
    je .nx_g
    call qword [rel __imp_CloseHandle]
.nx_g:
    inc ebx
    jmp .cl_g

.free_mem_ok:
    movzx ebx, byte [rbp + VM86_CTX64.bExitCode]
    mov rcx, qword [rbp + VM86_CTX64.pMem]
    call perm_free64
    mov eax, ebx
    clc
    @epilog_ret

.bad_img:
    mov eax, ERR_BAD_COMMAND
.free_err:
    mov ebx, eax
    mov rcx, qword [rbp + VM86_CTX64.pMem]
    call perm_free64
    mov eax, ebx
.fail_no_mem:
    stc
    @epilog_ret
@endproc
