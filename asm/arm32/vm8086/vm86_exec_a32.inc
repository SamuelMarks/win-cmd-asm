;; @file vm86_exec_a32.inc
;; @isa armv4
;; @desc ARM32 (ARMv4 & ARMv7-A Thumb-2) 8086 CPU instruction interpreter and .COM / MZ .EXE PSP loader.

@cpu armv4
@format inc

;; @proc vm86_check_jcc_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = opcode low nibble (0..15)
;; @out r0 = 1 if branch taken, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Evaluates all 16 Intel 8086 Jcc condition predicates.
@proc vm86_check_jcc_a32
    ldrh r2, [r0, #VM86_CTX_A32.regFlags]
    and r3, r1, #1 ; invert bit
    lsr r1, r1, #1
    mov r0, #0

    cmp r1, #0
    beq .c_o
    cmp r1, #1
    beq .c_b
    cmp r1, #2
    beq .c_z
    cmp r1, #3
    beq .c_be
    cmp r1, #4
    beq .c_s
    cmp r1, #5
    beq .c_p
    cmp r1, #6
    beq .c_l
    ; 7 = JLE / JG: ZF=1 or SF!=OF
    tst r2, #0x40
    bne .one
    b .c_l

.c_o:
    mov_imm32 r1, 0x0800
    tst r2, r1
    bne .one
    b .inv
.c_b:
    tst r2, #0x01
    bne .one
    b .inv
.c_z:
    tst r2, #0x40
    bne .one
    b .inv
.c_be:
    tst r2, #0x41
    bne .one
    b .inv
.c_s:
    tst r2, #0x80
    bne .one
    b .inv
.c_p:
    tst r2, #0x04
    bne .one
    b .inv
.c_l:
    lsr r0, r2, #7
    lsr r1, r2, #11
    eor r0, r0, r1
    and r0, r0, #1
    b .inv
.one:
    mov r0, #1
.inv:
    eor r0, r0, r3
    bx lr
@endproc

;; @proc vm86_step_str_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = string opcode (0xA4..0xAF)
;; @out Executes one or REP-counted 8086 string operation (MOVS, CMPS, STOS, LODS, SCAS)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Byte-alignment-safe 8086 string primitive engine honoring DF, REP, REPNE, and segment overrides.
@proc vm86_step_str_a32
    push {r4, r5, r6, r7, r8, lr}
    sub sp, sp, #8
    mov r4, r0
    mov r5, r1

.rep_lp:
    ldrb r0, [r4, #VM86_CTX_A32.bRepMode]
    cmp r0, #0
    beq .one_it
    add r1, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r1, #2] ; CX
    cmp r0, #0
    beq .done

.one_it:
    mov r6, #1
    tst r5, #1
    beq .got_step
    mov r6, #2
.got_step:
    ldrh r0, [r4, #VM86_CTX_A32.regFlags]
    mov_imm32 r1, 0x0400
    tst r0, r1
    beq .dir_ready
    mov r0, #0
    sub r6, r0, r6
.dir_ready:
    cmp r5, #0xA4
    beq .movsb_op
    cmp r5, #0xA5
    beq .movsw_op
    cmp r5, #0xAA
    beq .stosb_op
    cmp r5, #0xAB
    beq .stosw_op
    cmp r5, #0xAC
    beq .lodsb_op
    cmp r5, #0xAD
    beq .lodsw_op
    cmp r5, #0xAE
    beq .scasb_op
    cmp r5, #0xAF
    beq .scasw_op
    cmp r5, #0xA6
    beq .cmpsb_op
    b .cmpsw_op

.movsb_op:
    mov r0, r4
    mov r1, #3
    bl vm86_eff_seg_a32
    mov r1, r0
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #12] ; SI
    mov r0, r4
    bl vm86_lin_a32
    ldrb r7, [r0, #0]
    ldrh r1, [r4, #VM86_CTX_A32.sregs16] ; ES
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14] ; DI
    mov r0, r4
    bl vm86_lin_a32
    strb r7, [r0, #0]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #12]
    add r0, r0, r6
    strh r0, [r3, #12]
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]
    b .rep_no_zf

.movsw_op:
    mov r0, r4
    mov r1, #3
    bl vm86_eff_seg_a32
    mov r1, r0
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #12]
    mov r0, r4
    bl vm86_lin_a32
    bl a32_load_u16_ua
    mov r7, r0
    ldrh r1, [r4, #VM86_CTX_A32.sregs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14]
    mov r0, r4
    bl vm86_lin_a32
    mov r1, r7
    bl a32_store_u16_ua
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #12]
    add r0, r0, r6
    strh r0, [r3, #12]
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]
    b .rep_no_zf

.stosb_op:
    ldrh r1, [r4, #VM86_CTX_A32.sregs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14]
    mov r0, r4
    bl vm86_lin_a32
    ldrb r1, [r4, #VM86_CTX_A32.regs16]
    strb r1, [r0, #0]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]
    b .rep_no_zf

.stosw_op:
    ldrh r1, [r4, #VM86_CTX_A32.sregs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14]
    mov r0, r4
    bl vm86_lin_a32
    ldrh r1, [r4, #VM86_CTX_A32.regs16]
    bl a32_store_u16_ua
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]
    b .rep_no_zf

.lodsb_op:
    mov r0, r4
    mov r1, #3
    bl vm86_eff_seg_a32
    mov r1, r0
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #12]
    mov r0, r4
    bl vm86_lin_a32
    ldrb r0, [r0, #0]
    strb r0, [r4, #VM86_CTX_A32.regs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #12]
    add r0, r0, r6
    strh r0, [r3, #12]
    b .rep_no_zf

.lodsw_op:
    mov r0, r4
    mov r1, #3
    bl vm86_eff_seg_a32
    mov r1, r0
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #12]
    mov r0, r4
    bl vm86_lin_a32
    bl a32_load_u16_ua
    strh r0, [r4, #VM86_CTX_A32.regs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #12]
    add r0, r0, r6
    strh r0, [r3, #12]
    b .rep_no_zf

.scasb_op:
    ldrh r1, [r4, #VM86_CTX_A32.sregs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14]
    mov r0, r4
    bl vm86_lin_a32
    ldrb r3, [r0, #0]
    ldrb r2, [r4, #VM86_CTX_A32.regs16]
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #7
    bl vm86_alu_a32
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]
    b .rep_zf

.scasw_op:
    ldrh r1, [r4, #VM86_CTX_A32.sregs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14]
    mov r0, r4
    bl vm86_lin_a32
    bl a32_load_u16_ua
    mov r3, r0
    ldrh r2, [r4, #VM86_CTX_A32.regs16]
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #7
    bl vm86_alu_a32
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]
    b .rep_zf

.cmpsb_op:
    mov r0, r4
    mov r1, #3
    bl vm86_eff_seg_a32
    mov r1, r0
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #12]
    mov r0, r4
    bl vm86_lin_a32
    ldrb r7, [r0, #0]
    ldrh r1, [r4, #VM86_CTX_A32.sregs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14]
    mov r0, r4
    bl vm86_lin_a32
    ldrb r3, [r0, #0]
    mov r2, r7
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #7
    bl vm86_alu_a32
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #12]
    add r0, r0, r6
    strh r0, [r3, #12]
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]
    b .rep_zf

.cmpsw_op:
    mov r0, r4
    mov r1, #3
    bl vm86_eff_seg_a32
    mov r1, r0
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #12]
    mov r0, r4
    bl vm86_lin_a32
    bl a32_load_u16_ua
    mov r7, r0
    ldrh r1, [r4, #VM86_CTX_A32.sregs16]
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #14]
    mov r0, r4
    bl vm86_lin_a32
    bl a32_load_u16_ua
    mov r3, r0
    mov r2, r7
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #7
    bl vm86_alu_a32
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r3, #12]
    add r0, r0, r6
    strh r0, [r3, #12]
    ldrh r0, [r3, #14]
    add r0, r0, r6
    strh r0, [r3, #14]

.rep_zf:
    ldrb r0, [r4, #VM86_CTX_A32.bRepMode]
    cmp r0, #0
    beq .done
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r1, [r3, #2]
    subs r1, r1, #1
    strh r1, [r3, #2]
    beq .done
    ldrh r2, [r4, #VM86_CTX_A32.regFlags]
    cmp r0, #0xF3
    beq .chk_repe
    tst r2, #0x40
    beq .rep_lp
    b .done
.chk_repe:
    tst r2, #0x40
    bne .rep_lp
    b .done

.rep_no_zf:
    ldrb r0, [r4, #VM86_CTX_A32.bRepMode]
    cmp r0, #0
    beq .done
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r1, [r3, #2]
    subs r1, r1, #1
    strh r1, [r3, #2]
    bne .rep_lp

.done:
    add sp, sp, #8
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc vm86_step_low_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = primary opcode (0x00..0x3F)
;; @out Executes ALU r/m, r/imm, or PUSH/POP sreg
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Handles opcodes 0x00..0x3F (ADD, OR, ADC, SBB, AND, SUB, XOR, CMP, PUSH/POP ES/CS/SS/DS).
@proc vm86_step_low_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #12
    mov r4, r0 ; ctx
    mov r5, r1 ; op

    and r0, r5, #7
    cmp r0, #6
    beq .push_sreg
    cmp r0, #7
    beq .pop_sreg
    cmp r0, #4
    beq .alu_al_imm8
    cmp r0, #5
    beq .alu_ax_imm16

    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0 ; EA ptr
    and r2, r5, #1
    mov r0, r4
    bl vm86_reg_ptr_a32
    mov r7, r0 ; reg ptr

    lsr r8, r5, #3
    and r8, r8, #7 ; alu_op
    and r9, r5, #1 ; is_word
    str r9, [sp, #0]

    tst r5, #2
    bne .to_reg
    cmp r9, #0
    bne .mr16
    ldrb r2, [r6, #0]
    ldrb r3, [r7, #0]
    mov r0, r4
    mov r1, r8
    bl vm86_alu_a32
    cmp r8, #7
    beq .done
    strb r0, [r6, #0]
    b .done
.mr16:
    mov r0, r6
    bl a32_load_u16_ua
    mov r9, r0
    mov r0, r7
    bl a32_load_u16_ua
    mov r3, r0
    mov r2, r9
    mov r0, r4
    mov r1, r8
    bl vm86_alu_a32
    cmp r8, #7
    beq .done
    mov r1, r0
    mov r0, r6
    bl a32_store_u16_ua
    b .done

.to_reg:
    cmp r9, #0
    bne .rm16
    ldrb r2, [r7, #0]
    ldrb r3, [r6, #0]
    mov r0, r4
    mov r1, r8
    bl vm86_alu_a32
    cmp r8, #7
    beq .done
    strb r0, [r7, #0]
    b .done
.rm16:
    mov r0, r7
    bl a32_load_u16_ua
    mov r9, r0
    mov r0, r6
    bl a32_load_u16_ua
    mov r3, r0
    mov r2, r9
    mov r0, r4
    mov r1, r8
    bl vm86_alu_a32
    cmp r8, #7
    beq .done
    mov r1, r0
    mov r0, r7
    bl a32_store_u16_ua
    b .done

.alu_al_imm8:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r3, r0
    ldrb r2, [r4, #VM86_CTX_A32.regs16]
    lsr r8, r5, #3
    and r8, r8, #7
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, r8
    bl vm86_alu_a32
    cmp r8, #7
    beq .done
    strb r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.alu_ax_imm16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r3, r0
    ldrh r2, [r4, #VM86_CTX_A32.regs16]
    lsr r8, r5, #3
    and r8, r8, #7
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, r8
    bl vm86_alu_a32
    cmp r8, #7
    beq .done
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.push_sreg:
    lsr r1, r5, #3
    and r1, r1, #3
    lsl r1, r1, #1
    add r2, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r2, r1]
    mov r0, r4
    bl vm86_push16_a32
    b .done

.pop_sreg:
    lsr r6, r5, #3
    and r6, r6, #3
    lsl r6, r6, #1
    mov r0, r4
    bl vm86_pop16_a32
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, r6]

.done:
    add sp, sp, #12
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc vm86_step_mid_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = primary opcode (0x40..0xBF)
;; @out Executes INC/DEC/PUSH/POP r16, Jcc, GRP1 imm, TEST/XCHG/MOV/LEA, CBW/CWD/PUSHF/POPF, MOV moffs, String, MOV imm
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Handles opcodes 0x40..0xBF with 100% ARMv4 alignment safety.
@proc vm86_step_mid_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #12
    mov r4, r0
    mov r5, r1

    cmp r5, #0x47
    bls .inc_r16
    cmp r5, #0x4F
    bls .dec_r16
    cmp r5, #0x57
    bls .push_r16
    cmp r5, #0x5F
    bls .pop_r16
    cmp r5, #0x6F
    bls .done
    cmp r5, #0x7F
    bls .jcc_rel8
    cmp r5, #0x83
    bls .grp1_imm
    cmp r5, #0x85
    bls .test_rm_r
    cmp r5, #0x87
    bls .xchg_rm_r
    cmp r5, #0x8B
    bls .mov_rm_r
    cmp r5, #0x8C
    beq .mov_rm_sreg
    cmp r5, #0x8D
    beq .lea_r16
    cmp r5, #0x8E
    beq .mov_sreg_rm
    cmp r5, #0x8F
    beq .pop_rm16
    cmp r5, #0x97
    bls .xchg_ax_r16
    cmp r5, #0x98
    beq .cbw_op
    cmp r5, #0x99
    beq .cwd_op
    cmp r5, #0x9A
    beq .call_far_ptr
    cmp r5, #0x9C
    beq .pushf_op
    cmp r5, #0x9D
    beq .popf_op
    cmp r5, #0x9E
    beq .sahf_op
    cmp r5, #0x9F
    beq .lahf_op
    cmp r5, #0xA3
    bls .mov_moffs
    cmp r5, #0xA7
    bls .str_op
    cmp r5, #0xA9
    bls .test_acc_imm
    cmp r5, #0xAF
    bls .str_op
    cmp r5, #0xB7
    bls .mov_r8_imm
    b .mov_r16_imm

.inc_r16:
    and r1, r5, #7
    lsl r1, r1, #1
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, r1]
    add r0, r0, #1
    strh r0, [r2, r1]
    mov r1, r0
    mov r0, r4
    bl vm86_set_szp16_a32
    b .done

.dec_r16:
    and r1, r5, #7
    lsl r1, r1, #1
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, r1]
    sub r0, r0, #1
    strh r0, [r2, r1]
    mov r1, r0
    mov r0, r4
    bl vm86_set_szp16_a32
    b .done

.push_r16:
    and r1, r5, #7
    lsl r1, r1, #1
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r1, [r2, r1]
    mov r0, r4
    bl vm86_push16_a32
    b .done

.pop_r16:
    and r6, r5, #7
    lsl r6, r6, #1
    mov r0, r4
    bl vm86_pop16_a32
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, r6]
    b .done

.jcc_rel8:
    mov r0, r4
    bl vm86_fetch8_a32
    tst r0, #0x80
    beq .jcc_p
    mov_imm32 r1, 0xFF00
    orr r0, r0, r1
.jcc_p:
    mov r6, r0
    and r1, r5, #0x0F
    mov r0, r4
    bl vm86_check_jcc_a32
    cmp r0, #0
    beq .done
    ldrh r0, [r4, #VM86_CTX_A32.regIp]
    add r0, r0, r6
    strh r0, [r4, #VM86_CTX_A32.regIp]
    b .done

.grp1_imm:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0 ; EA
    mov r7, r1 ; sub-op 0..7
    cmp r5, #0x81
    beq .g1_w16
    cmp r5, #0x83
    beq .g1_s8
    mov r0, r4
    bl vm86_fetch8_a32
    mov r3, r0
    ldrb r2, [r6, #0]
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, r7
    bl vm86_alu_a32
    cmp r7, #7
    beq .done
    strb r0, [r6, #0]
    b .done
.g1_w16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r8, r0
    b .g1_do16
.g1_s8:
    mov r0, r4
    bl vm86_fetch8_a32
    tst r0, #0x80
    beq .g1_s8p
    mov_imm32 r1, 0xFF00
    orr r0, r0, r1
.g1_s8p:
    mov r8, r0
.g1_do16:
    mov r0, r6
    bl a32_load_u16_ua
    mov r2, r0
    mov r3, r8
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, r7
    bl vm86_alu_a32
    cmp r7, #7
    beq .done
    mov r1, r0
    mov r0, r6
    bl a32_store_u16_ua
    b .done

.test_rm_r:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_reg_ptr_a32
    mov r7, r0
    tst r5, #1
    bne .tst16
    ldrb r2, [r6, #0]
    ldrb r3, [r7, #0]
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #4
    bl vm86_alu_a32
    b .done
.tst16:
    mov r0, r6
    bl a32_load_u16_ua
    mov r8, r0
    mov r0, r7
    bl a32_load_u16_ua
    mov r3, r0
    mov r2, r8
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #4
    bl vm86_alu_a32
    b .done

.xchg_rm_r:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_reg_ptr_a32
    mov r7, r0
    tst r5, #1
    bne .xcg16
    ldrb r0, [r6, #0]
    ldrb r1, [r7, #0]
    strb r1, [r6, #0]
    strb r0, [r7, #0]
    b .done
.xcg16:
    mov r0, r6
    bl a32_load_u16_ua
    mov r8, r0
    mov r0, r7
    bl a32_load_u16_ua
    mov r9, r0
    mov r0, r6
    mov r1, r9
    bl a32_store_u16_ua
    mov r0, r7
    mov r1, r8
    bl a32_store_u16_ua
    b .done

.mov_rm_r:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_reg_ptr_a32
    mov r7, r0
    tst r5, #2
    bne .mv_to_reg
    tst r5, #1
    bne .mv_mr16
    ldrb r0, [r7, #0]
    strb r0, [r6, #0]
    b .done
.mv_mr16:
    mov r0, r7
    bl a32_load_u16_ua
    mov r1, r0
    mov r0, r6
    bl a32_store_u16_ua
    b .done
.mv_to_reg:
    tst r5, #1
    bne .mv_rm16
    ldrb r0, [r6, #0]
    strb r0, [r7, #0]
    b .done
.mv_rm16:
    mov r0, r6
    bl a32_load_u16_ua
    mov r1, r0
    mov r0, r7
    bl a32_store_u16_ua
    b .done

.mov_rm_sreg:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    mov r2, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    and r1, r1, #3
    lsl r1, r1, #1
    add r2, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r2, r1]
    bl a32_store_u16_ua
    b .done

.mov_sreg_rm:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    mov r2, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    and r6, r1, #3
    lsl r6, r6, #1
    bl a32_load_u16_ua
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, r6]
    b .done

.lea_r16:
    mov r0, #0
    strb r0, [r4, #VM86_CTX_A32.bSegOvr]
    ldrh r8, [r4, #VM86_CTX_A32.sregs16]
    mov r0, #0
    strh r0, [r4, #VM86_CTX_A32.sregs16]
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    mov r2, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    strh r8, [r4, #VM86_CTX_A32.sregs16]
    ldr r2, [r4, #VM86_CTX_A32.pMem]
    sub r0, r0, r2
    lsl r1, r1, #1
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, r1]
    b .done

.pop_rm16:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    mov r2, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    mov r0, r4
    bl vm86_pop16_a32
    mov r1, r0
    mov r0, r6
    bl a32_store_u16_ua
    b .done

.xchg_ax_r16:
    and r1, r5, #7
    lsl r1, r1, #1
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, #0]
    ldrh r3, [r2, r1]
    strh r3, [r2, #0]
    strh r0, [r2, r1]
    b .done

.cbw_op:
    ldrb r0, [r4, #VM86_CTX_A32.regs16]
    tst r0, #0x80
    beq .cbw_st
    mov_imm32 r1, 0xFF00
    orr r0, r0, r1
.cbw_st:
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.cwd_op:
    ldrh r0, [r4, #VM86_CTX_A32.regs16]
    mov r1, #0
    mov_imm32 r2, 0x8000
    tst r0, r2
    beq .cwd_st
    mov_imm32 r1, 0xFFFF
.cwd_st:
    add r2, r4, #VM86_CTX_A32.regs16
    strh r1, [r2, #4]
    b .done

.call_far_ptr:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r6, r0
    mov r0, r4
    bl vm86_fetch16_a32
    mov r7, r0
    add r2, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r2, #2]
    mov r0, r4
    bl vm86_push16_a32
    ldrh r1, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_push16_a32
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r7, [r2, #2]
    strh r6, [r4, #VM86_CTX_A32.regIp]
    b .done

.pushf_op:
    ldrh r1, [r4, #VM86_CTX_A32.regFlags]
    mov_imm32 r2, 0xF002
    orr r1, r1, r2
    mov r0, r4
    bl vm86_push16_a32
    b .done

.popf_op:
    mov r0, r4
    bl vm86_pop16_a32
    orr r0, r0, #2
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .done

.sahf_op:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrb r0, [r1, #1]
    orr r0, r0, #2
    ldrh r1, [r4, #VM86_CTX_A32.regFlags]
    mov_imm32 r2, 0xFF00
    and r1, r1, r2
    orr r1, r1, r0
    strh r1, [r4, #VM86_CTX_A32.regFlags]
    b .done

.lahf_op:
    ldrb r0, [r4, #VM86_CTX_A32.regFlags]
    orr r0, r0, #2
    add r1, r4, #VM86_CTX_A32.regs16
    strb r0, [r1, #1]
    b .done

.mov_moffs:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r6, r0
    mov r0, r4
    mov r1, #3
    bl vm86_eff_seg_a32
    mov r1, r0
    mov r2, r6
    mov r0, r4
    bl vm86_lin_a32
    cmp r5, #0xA0
    beq .m_a0
    cmp r5, #0xA1
    beq .m_a1
    cmp r5, #0xA2
    beq .m_a2
    ldrh r1, [r4, #VM86_CTX_A32.regs16]
    bl a32_store_u16_ua
    b .done
.m_a0:
    ldrb r0, [r0, #0]
    strb r0, [r4, #VM86_CTX_A32.regs16]
    b .done
.m_a1:
    bl a32_load_u16_ua
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done
.m_a2:
    ldrb r1, [r4, #VM86_CTX_A32.regs16]
    strb r1, [r0, #0]
    b .done

.str_op:
    mov r0, r4
    mov r1, r5
    bl vm86_step_str_a32
    b .done

.test_acc_imm:
    tst r5, #1
    bne .tai16
    mov r0, r4
    bl vm86_fetch8_a32
    mov r3, r0
    ldrb r2, [r4, #VM86_CTX_A32.regs16]
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #4
    bl vm86_alu_a32
    b .done
.tai16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r3, r0
    ldrh r2, [r4, #VM86_CTX_A32.regs16]
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #4
    bl vm86_alu_a32
    b .done

.mov_r8_imm:
    mov r0, r4
    and r1, r5, #7
    mov r2, #0
    bl vm86_reg_ptr_a32
    mov r6, r0
    mov r0, r4
    bl vm86_fetch8_a32
    strb r0, [r6, #0]
    b .done

.mov_r16_imm:
    and r6, r5, #7
    lsl r6, r6, #1
    mov r0, r4
    bl vm86_fetch16_a32
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, r6]

.done:
    add sp, sp, #12
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc vm86_step_high_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = primary opcode (0xC0..0xFF)
;; @out Executes RET, LES/LDS, MOV rm/imm, INT, GRP2 shifts, LOOP/JCXZ, CALL/JMP, HLT, GRP3 mul/div/neg/not, flags, GRP4/5
;; @err Sets bHalted = 1 on HLT, INT 20h, ret-0, or divide fault
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Handles opcodes 0xC0..0xFF using software/trap-safe division and byte-safe memory loads.
@proc vm86_step_high_a32
    push {r4, r5, r6, r7, r8, r9, lr}
    sub sp, sp, #12
    mov r4, r0
    mov r5, r1

    cmp r5, #0xC2
    beq .ret_imm16
    cmp r5, #0xC3
    beq .ret_near
    cmp r5, #0xC4
    beq .les_op
    cmp r5, #0xC5
    beq .lds_op
    cmp r5, #0xC7
    bls .mov_rm_imm
    cmp r5, #0xCA
    beq .retf_imm16
    cmp r5, #0xCB
    beq .retf_op
    cmp r5, #0xCC
    beq .done
    cmp r5, #0xCD
    beq .int_imm8
    cmp r5, #0xCF
    beq .iret_op
    cmp r5, #0xD3
    bls .grp2_shift
    cmp r5, #0xDF
    bls .done
    cmp r5, #0xE3
    bls .loop_jcxz
    cmp r5, #0xE7
    bls .in_out_imm
    cmp r5, #0xE8
    beq .call_rel16
    cmp r5, #0xE9
    beq .jmp_rel16
    cmp r5, #0xEA
    beq .jmp_far_ptr
    cmp r5, #0xEB
    beq .jmp_rel8
    cmp r5, #0xEF
    bls .done
    cmp r5, #0xF4
    beq .hlt_op
    cmp r5, #0xF5
    beq .cmc_op
    cmp r5, #0xF7
    bls .grp3_op
    cmp r5, #0xFD
    bls .flag_ops
    b .grp45_op

.in_out_imm:
    mov r0, r4
    bl vm86_fetch8_a32
    b .done

.ret_imm16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r6, r0
    mov r0, r4
    bl vm86_pop16_a32
    strh r0, [r4, #VM86_CTX_A32.regIp]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, #8]
    add r0, r0, r6
    strh r0, [r2, #8]
    b .done

.ret_near:
    mov r0, r4
    bl vm86_pop16_a32
    cmp r0, #0
    beq .ret_zero_psp
    strh r0, [r4, #VM86_CTX_A32.regIp]
    b .done
.ret_zero_psp:
    mov r0, #0
    strb r0, [r4, #VM86_CTX_A32.bExitCode]
    mov r0, #1
    strb r0, [r4, #VM86_CTX_A32.bHalted]
    b .done

.les_op:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    mov r2, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    lsl r7, r1, #1
    bl a32_load_u16_ua
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, r7]
    add r0, r6, #2
    bl a32_load_u16_ua
    strh r0, [r4, #VM86_CTX_A32.sregs16]
    b .done

.lds_op:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    mov r2, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    lsl r7, r1, #1
    bl a32_load_u16_ua
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, r7]
    add r0, r6, #2
    bl a32_load_u16_ua
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #6]
    b .done

.mov_rm_imm:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    tst r5, #1
    bne .mrmi16
    mov r0, r4
    bl vm86_fetch8_a32
    strb r0, [r6, #0]
    b .done
.mrmi16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r1, r0
    mov r0, r6
    bl a32_store_u16_ua
    b .done

.retf_imm16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r6, r0
    mov r0, r4
    bl vm86_pop16_a32
    strh r0, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_pop16_a32
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #2]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, #8]
    add r0, r0, r6
    strh r0, [r2, #8]
    b .done

.retf_op:
    mov r0, r4
    bl vm86_pop16_a32
    strh r0, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_pop16_a32
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #2]
    b .done

.int_imm8:
    mov r0, r4
    bl vm86_fetch8_a32
    cmp r0, #0x20
    beq .ret_zero_psp
    cmp r0, #0x21
    beq .do_int21
    cmp r0, #0x10
    beq .do_int10
    cmp r0, #0x1A
    beq .do_int1a
    cmp r0, #0x2F
    beq .do_int2f
    b .done
.do_int21:
    mov r0, r4
    bl vm86_dos_int21_a32
    b .done
.do_int10:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrb r0, [r1, #1] ; AH
    cmp r0, #0x0E
    bne .done
    ldrb r0, [r1, #0] ; AL
    strb r0, [sp, #4]
    mov r0, #0
    strb r0, [sp, #5]
    add r0, sp, #4
    bl out_str_a32
    b .done
.do_int1a:
    add r1, r4, #VM86_CTX_A32.regs16
    mov r0, #0
    strh r0, [r1, #2]
    mov r0, #100
    strh r0, [r1, #4]
    mov r0, #0
    strb r0, [r1, #0]
    b .done
.do_int2f:
    mov r0, #0
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.iret_op:
    mov r0, r4
    bl vm86_pop16_a32
    strh r0, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_pop16_a32
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #2]
    mov r0, r4
    bl vm86_pop16_a32
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .done

.grp2_shift:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0 ; EA
    mov r7, r1 ; sub-op 0..7
    mov r8, #1
    tst r5, #2
    beq .cnt_ok
    add r1, r4, #VM86_CTX_A32.regs16
    ldrb r8, [r1, #2] ; CL
.cnt_ok:
    and r8, r8, #31
    cmp r8, #0
    beq .done
    tst r5, #1
    bne .sh16
    ldrb r0, [r6, #0]
    cmp r7, #5 ; SHR
    beq .shr8
    cmp r7, #7 ; SAR
    beq .sar8
    cmp r7, #1 ; ROR
    beq .ror8
    lsl r0, r0, r8
    lsr r9, r0, #8
    and r9, r9, #1
    b .st_sh8
.shr8:
    sub r1, r8, #1
    lsr r9, r0, r1
    and r9, r9, #1
    lsr r0, r0, r8
    b .st_sh8
.sar8:
    tst r0, #0x80
    beq .shr8
    mov_imm32 r1, 0xFFFFFF00
    orr r0, r0, r1
    sub r1, r8, #1
    asr r9, r0, r1
    and r9, r9, #1
    asr r0, r0, r8
    b .st_sh8
.ror8:
    and r1, r8, #7
    lsr r2, r0, r1
    mov r3, #8
    sub r3, r3, r1
    lsl r0, r0, r3
    orr r0, r0, r2
    lsr r9, r0, #7
    and r9, r9, #1
.st_sh8:
    and r0, r0, #0xFF
    strb r0, [r6, #0]
    ldrh r1, [r4, #VM86_CTX_A32.regFlags]
    bic r1, r1, #1
    orr r1, r1, r9
    strh r1, [r4, #VM86_CTX_A32.regFlags]
    mov r1, r0
    mov r0, r4
    bl vm86_set_szp8_a32
    b .done

.sh16:
    mov r0, r6
    bl a32_load_u16_ua
    cmp r7, #5
    beq .shr16
    cmp r7, #7
    beq .sar16
    cmp r7, #1
    beq .ror16
    lsl r0, r0, r8
    lsr r9, r0, #16
    and r9, r9, #1
    b .st_sh16
.shr16:
    sub r1, r8, #1
    lsr r9, r0, r1
    and r9, r9, #1
    lsr r0, r0, r8
    b .st_sh16
.sar16:
    mov_imm32 r1, 0x8000
    tst r0, r1
    beq .shr16
    mov_imm32 r1, 0xFFFF0000
    orr r0, r0, r1
    sub r1, r8, #1
    asr r9, r0, r1
    and r9, r9, #1
    asr r0, r0, r8
    b .st_sh16
.ror16:
    and r1, r8, #15
    lsr r2, r0, r1
    mov r3, #16
    sub r3, r3, r1
    lsl r0, r0, r3
    orr r0, r0, r2
    lsr r9, r0, #15
    and r9, r9, #1
.st_sh16:
    mov_imm32 r1, 0xFFFF
    and r8, r0, r1
    mov r0, r6
    mov r1, r8
    bl a32_store_u16_ua
    ldrh r1, [r4, #VM86_CTX_A32.regFlags]
    bic r1, r1, #1
    orr r1, r1, r9
    strh r1, [r4, #VM86_CTX_A32.regFlags]
    mov r1, r8
    mov r0, r4
    bl vm86_set_szp16_a32
    b .done

.loop_jcxz:
    mov r0, r4
    bl vm86_fetch8_a32
    tst r0, #0x80
    beq .lp_p
    mov_imm32 r1, 0xFF00
    orr r0, r0, r1
.lp_p:
    mov r6, r0
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, #2] ; CX
    cmp r5, #0xE3
    beq .jcxz_c
    subs r0, r0, #1
    strh r0, [r2, #2]
    beq .done
    cmp r5, #0xE2
    beq .take_rel
    ldrh r1, [r4, #VM86_CTX_A32.regFlags]
    cmp r5, #0xE1
    beq .loope_c
    tst r1, #0x40
    beq .take_rel
    b .done
.loope_c:
    tst r1, #0x40
    bne .take_rel
    b .done
.jcxz_c:
    cmp r0, #0
    bne .done
.take_rel:
    ldrh r0, [r4, #VM86_CTX_A32.regIp]
    add r0, r0, r6
    strh r0, [r4, #VM86_CTX_A32.regIp]
    b .done

.call_rel16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r6, r0
    ldrh r1, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_push16_a32
    ldrh r0, [r4, #VM86_CTX_A32.regIp]
    add r0, r0, r6
    strh r0, [r4, #VM86_CTX_A32.regIp]
    b .done

.jmp_rel16:
    mov r0, r4
    bl vm86_fetch16_a32
    ldrh r1, [r4, #VM86_CTX_A32.regIp]
    add r1, r1, r0
    strh r1, [r4, #VM86_CTX_A32.regIp]
    b .done

.jmp_far_ptr:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r6, r0
    mov r0, r4
    bl vm86_fetch16_a32
    strh r6, [r4, #VM86_CTX_A32.regIp]
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #2]
    b .done

.jmp_rel8:
    mov r0, r4
    bl vm86_fetch8_a32
    tst r0, #0x80
    beq .jr8_p
    mov_imm32 r1, 0xFF00
    orr r0, r0, r1
.jr8_p:
    ldrh r1, [r4, #VM86_CTX_A32.regIp]
    add r1, r1, r0
    strh r1, [r4, #VM86_CTX_A32.regIp]
    b .done

.hlt_op:
    mov r0, #1
    strb r0, [r4, #VM86_CTX_A32.bHalted]
    b .done

.cmc_op:
    ldrh r0, [r4, #VM86_CTX_A32.regFlags]
    eor r0, r0, #1
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .done

.grp3_op:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0 ; EA
    mov r7, r1 ; sub-op 0..7
    tst r5, #1
    bne .g3_16
    cmp r7, #0
    beq .g3_tst8
    cmp r7, #2
    beq .g3_not8
    cmp r7, #3
    beq .g3_neg8
    cmp r7, #4
    beq .g3_mul8
    cmp r7, #5
    beq .g3_imul8
    cmp r7, #6
    beq .g3_div8
    b .g3_idiv8

.g3_tst8:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r3, r0
    ldrb r2, [r6, #0]
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #4
    bl vm86_alu_a32
    b .done
.g3_not8:
    ldrb r0, [r6, #0]
    mvn r0, r0
    strb r0, [r6, #0]
    b .done
.g3_neg8:
    ldrb r3, [r6, #0]
    mov r2, #0
    mov r0, #0
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #5
    bl vm86_alu_a32
    strb r0, [r6, #0]
    b .done
.g3_mul8:
    ldrb r0, [r4, #VM86_CTX_A32.regs16]
    ldrb r1, [r6, #0]
    mul r2, r0, r1
    strh r2, [r4, #VM86_CTX_A32.regs16]
    b .done
.g3_imul8:
    ldrsb r0, [r4, #VM86_CTX_A32.regs16]
    ldrsb r1, [r6, #0]
    mul r2, r0, r1
    strh r2, [r4, #VM86_CTX_A32.regs16]
    b .done
.g3_div8:
    ldrh r0, [r4, #VM86_CTX_A32.regs16]
    ldrb r1, [r6, #0]
    cmp r1, #0
    beq .hlt_op
    bl math_udiv32
    lsl r1, r1, #8
    and r0, r0, #0xFF
    orr r0, r0, r1
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done
.g3_idiv8:
    ldrh r0, [r4, #VM86_CTX_A32.regs16]
    lsl r0, r0, #16
    asr r0, r0, #16
    ldrsb r1, [r6, #0]
    cmp r1, #0
    beq .hlt_op
    bl math_sdiv32
    and r0, r0, #0xFF
    and r1, r1, #0xFF
    lsl r1, r1, #8
    orr r0, r0, r1
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.g3_16:
    cmp r7, #0
    beq .g3_tst16
    cmp r7, #2
    beq .g3_not16
    cmp r7, #3
    beq .g3_neg16
    cmp r7, #4
    beq .g3_mul16
    cmp r7, #5
    beq .g3_imul16
    cmp r7, #6
    beq .g3_div16
    b .g3_idiv16

.g3_tst16:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r8, r0
    mov r0, r6
    bl a32_load_u16_ua
    mov r2, r0
    mov r3, r8
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #4
    bl vm86_alu_a32
    b .done
.g3_not16:
    mov r0, r6
    bl a32_load_u16_ua
    mvn r1, r0
    mov r0, r6
    bl a32_store_u16_ua
    b .done
.g3_neg16:
    mov r0, r6
    bl a32_load_u16_ua
    mov r3, r0
    mov r2, #0
    mov r0, #1
    str r0, [sp, #0]
    mov r0, r4
    mov r1, #5
    bl vm86_alu_a32
    mov r1, r0
    mov r0, r6
    bl a32_store_u16_ua
    b .done
.g3_mul16:
    ldrh r8, [r4, #VM86_CTX_A32.regs16]
    mov r0, r6
    bl a32_load_u16_ua
    mul r1, r8, r0
    strh r1, [r4, #VM86_CTX_A32.regs16]
    lsr r1, r1, #16
    add r2, r4, #VM86_CTX_A32.regs16
    strh r1, [r2, #4]
    b .done
.g3_imul16:
    ldrh r8, [r4, #VM86_CTX_A32.regs16]
    lsl r8, r8, #16
    asr r8, r8, #16
    mov r0, r6
    bl a32_load_u16_ua
    lsl r0, r0, #16
    asr r0, r0, #16
    mul r1, r8, r0
    strh r1, [r4, #VM86_CTX_A32.regs16]
    lsr r1, r1, #16
    add r2, r4, #VM86_CTX_A32.regs16
    strh r1, [r2, #4]
    b .done
.g3_div16:
    mov r0, r6
    bl a32_load_u16_ua
    cmp r0, #0
    beq .hlt_op
    mov r1, r0
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, #4] ; DX
    ldrh r3, [r2, #0] ; AX
    lsl r0, r0, #16
    orr r0, r0, r3
    bl math_udiv32
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, #0]
    strh r1, [r2, #4]
    b .done
.g3_idiv16:
    mov r0, r6
    bl a32_load_u16_ua
    lsl r1, r0, #16
    asr r1, r1, #16
    cmp r1, #0
    beq .hlt_op
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, #4]
    ldrh r3, [r2, #0]
    lsl r0, r0, #16
    orr r0, r0, r3
    bl math_sdiv32
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, #0]
    strh r1, [r2, #4]
    b .done

.flag_ops:
    ldrh r0, [r4, #VM86_CTX_A32.regFlags]
    cmp r5, #0xF8
    beq .f_clc
    cmp r5, #0xF9
    beq .f_stc
    cmp r5, #0xFC
    beq .f_cld
    cmp r5, #0xFD
    beq .f_std
    b .done
.f_clc:
    bic r0, r0, #1
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .done
.f_stc:
    orr r0, r0, #1
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .done
.f_cld:
    mov_imm32 r1, 0x0400
    bic r0, r0, r1
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .done
.f_std:
    mov_imm32 r1, 0x0400
    orr r0, r0, r1
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .done

.grp45_op:
    mov r0, r4
    bl vm86_fetch8_a32
    mov r1, r0
    and r2, r5, #1
    mov r0, r4
    bl vm86_decode_modrm_a32
    mov r6, r0
    mov r7, r1
    tst r5, #1
    bne .g5_16
    ldrb r0, [r6, #0]
    cmp r7, #0
    beq .g4_inc8
    sub r0, r0, #1
    b .g4_st8
.g4_inc8:
    add r0, r0, #1
.g4_st8:
    strb r0, [r6, #0]
    mov r1, r0
    mov r0, r4
    bl vm86_set_szp8_a32
    b .done

.g5_16:
    mov r0, r6
    bl a32_load_u16_ua
    mov r8, r0
    cmp r7, #0
    beq .g5_inc
    cmp r7, #1
    beq .g5_dec
    cmp r7, #2
    beq .g5_call_n
    cmp r7, #3
    beq .g5_call_f
    cmp r7, #4
    beq .g5_jmp_n
    cmp r7, #5
    beq .g5_jmp_f
    mov r1, r8
    mov r0, r4
    bl vm86_push16_a32
    b .done
.g5_inc:
    add r8, r8, #1
    mov r0, r6
    mov r1, r8
    bl a32_store_u16_ua
    mov r1, r8
    mov r0, r4
    bl vm86_set_szp16_a32
    b .done
.g5_dec:
    sub r8, r8, #1
    mov r0, r6
    mov r1, r8
    bl a32_store_u16_ua
    mov r1, r8
    mov r0, r4
    bl vm86_set_szp16_a32
    b .done
.g5_call_n:
    ldrh r1, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_push16_a32
    strh r8, [r4, #VM86_CTX_A32.regIp]
    b .done
.g5_call_f:
    add r0, r6, #2
    bl a32_load_u16_ua
    mov r9, r0
    add r2, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r2, #2]
    mov r0, r4
    bl vm86_push16_a32
    ldrh r1, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_push16_a32
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r9, [r2, #2]
    strh r8, [r4, #VM86_CTX_A32.regIp]
    b .done
.g5_jmp_n:
    strh r8, [r4, #VM86_CTX_A32.regIp]
    b .done
.g5_jmp_f:
    strh r8, [r4, #VM86_CTX_A32.regIp]
    add r0, r6, #2
    bl a32_load_u16_ua
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #2]

.done:
    add sp, sp, #12
    pop {r4, r5, r6, r7, r8, r9, lr}
    bx lr
@endproc

;; @proc vm86_step_one_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*
;; @out Decodes prefixes and dispatches one 8086 instruction
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Top-level single-instruction 8086 decoder for ARM32 vm8086.
@proc vm86_step_one_a32
    push {r4, lr}
    mov r4, r0
    mov r1, #0xFF
    strb r1, [r4, #VM86_CTX_A32.bSegOvr]
    mov r1, #0
    strb r1, [r4, #VM86_CTX_A32.bRepMode]

.pref_lp:
    mov r0, r4
    bl vm86_fetch8_a32
    cmp r0, #0x26
    beq .p_es
    cmp r0, #0x2E
    beq .p_cs
    cmp r0, #0x36
    beq .p_ss
    cmp r0, #0x3E
    beq .p_ds
    cmp r0, #0xF2
    beq .p_rep
    cmp r0, #0xF3
    beq .p_rep
    cmp r0, #0xF0
    beq .pref_lp

    mov r1, r0
    mov r0, r4
    cmp r1, #0x3F
    bls .go_low
    cmp r1, #0xBF
    bls .go_mid
    bl vm86_step_high_a32
    pop {r4, lr}
    bx lr
.go_low:
    bl vm86_step_low_a32
    pop {r4, lr}
    bx lr
.go_mid:
    bl vm86_step_mid_a32
    pop {r4, lr}
    bx lr

.p_es:
    mov r0, #0
    strb r0, [r4, #VM86_CTX_A32.bSegOvr]
    b .pref_lp
.p_cs:
    mov r0, #1
    strb r0, [r4, #VM86_CTX_A32.bSegOvr]
    b .pref_lp
.p_ss:
    mov r0, #2
    strb r0, [r4, #VM86_CTX_A32.bSegOvr]
    b .pref_lp
.p_ds:
    mov r0, #3
    strb r0, [r4, #VM86_CTX_A32.bSegOvr]
    b .pref_lp
.p_rep:
    strb r0, [r4, #VM86_CTX_A32.bRepMode]
    b .pref_lp
@endproc

;; @proc vm86_run_image_a32
;; @abi arm_ce
;; @in r0 = normalized WTF-8 .COM or DOS MZ .EXE file path, r1 = raw command tail (" arg1 arg2")
;; @out r0 = DOS exit code (0..255), r1 = 0 on success or ERR_* if invalid image
;; @err r1 != 0 if file cannot be opened or is a PE/NE executable
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Allocates 1 MiB + 64 KiB guest RAM via VirtualAlloc, builds DOS PSP + env, loads .COM/MZ, and executes on ARM32!
@proc vm86_run_image_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #208 ; VM86_CTX_A32 (192B) at sp+0
    mov r5, r0 ; path
    mov r6, r1 ; tail
    mov r4, sp ; ctx

    mov r0, r4
    mov r1, #192
    bl mem_zero_a32

    ; Allocate 1 MiB + 64 KiB isolated virtual 8086 RAM
    mov r0, #0
    mov_imm32 r1, A32_VM86_MEM_SIZE
    mov_imm32 r2, 0x3000 ; MEM_RESERVE | MEM_COMMIT
    mov r3, #4           ; PAGE_READWRITE
    bl hal_virtual_alloc
    cmp r0, #0
    beq .no_mem
    str r0, [r4, #VM86_CTX_A32.pMem]
    mov r7, r0 ; pMem

    ; Initialize guest handle table (0..2 = active fdTable[0..2], 3..19 = -1)
    mov r0, #0
    mvn r1, #0
.init_h:
    lsl r2, r0, #2
    add r3, r4, #VM86_CTX_A32.hostHandles
    str r1, [r3, r2]
    add r0, r0, #1
    cmp r0, #20
    bcc .init_h

    bl hal_get_ctx
    mov r8, r0
    add r1, r8, #ARM32_CTX.fdTable
    add r2, r4, #VM86_CTX_A32.hostHandles
    ldr r0, [r1, #0]
    str r0, [r2, #0]
    ldr r0, [r1, #4]
    str r0, [r2, #4]
    ldr r0, [r1, #8]
    str r0, [r2, #8]

    ; Copy host WTF-8 environment block into segment 0x0060 (offset 0x0600)
    mov_imm32 r0, 0x0600
    add r0, r7, r0
    ldr r1, [r8, #ARM32_CTX.pEnvBlock]
    ldr r2, [r8, #ARM32_CTX.dwEnvLen]
    mov_imm32 r3, 0x3F00
    cmp r2, r3
    bls .cp_env
    mov r2, r3
.cp_env:
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    strb r1, [r0, #1]

    ; Build 256-byte PSP at segment 0x1000 (linear 0x10000)
    mov_imm32 r0, 0x1000
    strh r0, [r4, #VM86_CTX_A32.wPspSeg]
    strh r0, [r4, #VM86_CTX_A32.wDtaSeg]
    mov r0, #0x80
    strh r0, [r4, #VM86_CTX_A32.wDtaOff]

    mov_imm32 r0, 0x10000
    add r8, r7, r0 ; PSP base
    mov r0, #0xCD
    strb r0, [r8, #0]
    mov r0, #0x20
    strb r0, [r8, #1]
    mov_imm32 r1, 0x9FFF
    add r0, r8, #2
    bl a32_store_u16_ua
    mov r1, #0x0060
    add r0, r8, #0x2C
    bl a32_store_u16_ua

    ; Populate PSP:0x80 command tail
    mov r2, #0
    cmp r6, #0
    beq .tail_done
.tail_lp:
    cmp r2, #126
    bcs .tail_done
    ldrb r0, [r6, r2]
    cmp r0, #0
    beq .tail_done
    add r1, r8, #0x81
    strb r0, [r1, r2]
    add r2, r2, #1
    b .tail_lp
.tail_done:
    strb r2, [r8, #0x80]
    add r1, r8, #0x81
    mov r0, #0x0D
    strb r0, [r1, r2]

    ; Open and read .COM / MZ .EXE into 0x10100
    mov r0, r5
    bl to_u16_longpath_a32
    mov r1, #0
    bl hal_open_file
    cmn r0, #1
    beq .fnf_err
    mov r6, r0 ; hFile

    mov_imm32 r0, 0x10100
    add r8, r7, r0 ; image load buffer
    mov r0, r6
    mov r1, r8
    mov_imm32 r2, 0x7FE00
    bl hal_read_handle
    mov r9, r0 ; bytes read
    mov r0, r6
    bl hal_close_handle
    cmp r9, #0
    beq .bad_img

    ; Check 'MZ' / 'ZM' signature vs flat .COM
    mov r0, r8
    bl a32_load_u16_ua
    mov_imm32 r1, 0x5A4D
    cmp r0, r1
    beq .load_mz
    mov_imm32 r1, 0x4D5A
    cmp r0, r1
    beq .load_mz

    ; Flat .COM setup (CS=DS=ES=SS=0x1000, IP=0x0100, SP=0xFFFE, [SS:FFFE]=0x0000)
    mov_imm32 r0, 0x1000
    add r1, r4, #VM86_CTX_A32.sregs16
    strh r0, [r1, #0]
    strh r0, [r1, #2]
    strh r0, [r1, #4]
    strh r0, [r1, #6]
    mov r0, #0x0100
    strh r0, [r4, #VM86_CTX_A32.regIp]
    mov_imm32 r0, 0xFFFE
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #8]
    mov_imm32 r0, 0x0202
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    b .run_vm

.load_mz:
    cmp r9, #28
    bcc .bad_img
    cmp r9, #0x40
    bcc .reloc_mz
    add r0, r8, #0x18
    bl a32_load_u16_ua
    cmp r0, #0x40
    bcc .reloc_mz
    add r0, r8, #0x3C
    bl a32_load_u32_ua
    cmp r0, r9
    bcs .reloc_mz
    add r0, r8, r0
    bl a32_load_u16_ua
    mov_imm32 r1, 0x4550 ; 'PE'
    cmp r0, r1
    beq .bad_img
    mov_imm32 r1, 0x454E ; 'NE'
    cmp r0, r1
    beq .bad_img

.reloc_mz:
    add r0, r8, #0x08
    bl a32_load_u16_ua
    lsl r5, r0, #4 ; hdr_bytes
    cmp r5, r9
    bcs .bad_img

    add r0, r8, #0x0E
    bl a32_load_u16_ua
    mov_imm32 r1, 0x1010
    add r0, r0, r1
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #4] ; SS
    add r0, r8, #0x10
    bl a32_load_u16_ua
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, #8] ; SP
    add r0, r8, #0x14
    bl a32_load_u16_ua
    strh r0, [r4, #VM86_CTX_A32.regIp]
    add r0, r8, #0x16
    bl a32_load_u16_ua
    mov_imm32 r1, 0x1010
    add r0, r0, r1
    add r2, r4, #VM86_CTX_A32.sregs16
    strh r0, [r2, #2] ; CS
    mov_imm32 r0, 0x1000
    strh r0, [r2, #0] ; ES = PSP
    strh r0, [r2, #6] ; DS = PSP
    mov_imm32 r0, 0x0202
    strh r0, [r4, #VM86_CTX_A32.regFlags]

    add r0, r8, #0x06
    bl a32_load_u16_ua
    mov r6, r0 ; reloc_cnt
    add r0, r8, #0x18
    bl a32_load_u16_ua
    mov r10, r0 ; reloc_off

    mov r0, #0
    cmp r6, #0
    beq .shift_mz
    lsl r0, r6, #2
    add r0, r0, #8
    bl arena_alloc_a32
    cmp r0, #0
    beq .bad_img
    mov r3, r0
    add r1, r8, r10
    lsl r2, r6, #2
    push {r3, r4}
    bl mem_copy_a32
    pop {r3, r4}
    mov r10, r3 ; saved reloc table copy

.shift_mz:
    sub r2, r9, r5 ; body_len
    mov r0, r8
    add r1, r8, r5
    bl mem_copy_a32

    ; Apply MZ segment relocations (+0x1010) using byte-safe unaligned helpers!
.rel_lp:
    cmp r6, #0
    beq .run_vm
    mov r0, r10
    bl a32_load_u16_ua
    mov r5, r0 ; off
    add r0, r10, #2
    bl a32_load_u16_ua
    lsl r0, r0, #4
    add r0, r0, r5
    add r9, r8, r0 ; target word in loaded MZ body
    mov r0, r9
    bl a32_load_u16_ua
    mov_imm32 r1, 0x1010
    add r1, r0, r1
    mov r0, r9
    bl a32_store_u16_ua
    add r10, r10, #4
    sub r6, r6, #1
    b .rel_lp

.run_vm:
    mov_imm32 r0, 50000000
    str r0, [r4, #VM86_CTX_A32.dwStepsLeft]
    bl hal_get_ctx
    mov r8, r0

.step_lp:
    ldrb r0, [r4, #VM86_CTX_A32.bHalted]
    cmp r0, #0
    bne .done_ok
    ldrb r0, [r8, #ARM32_CTX.bCtrlBreak]
    cmp r0, #0
    bne .ctrl_c
    ldr r0, [r4, #VM86_CTX_A32.dwStepsLeft]
    subs r0, r0, #1
    str r0, [r4, #VM86_CTX_A32.dwStepsLeft]
    beq .done_ok
    mov r0, r4
    bl vm86_step_one_a32
    b .step_lp

.ctrl_c:
    mov r0, #0xFF
    strb r0, [r4, #VM86_CTX_A32.bExitCode]

.done_ok:
    ; Close any guest file handles 5..19 left open
    mov r5, #5
.cl_lp:
    cmp r5, #20
    bcs .free_ok
    lsl r1, r5, #2
    add r2, r4, #VM86_CTX_A32.hostHandles
    ldr r0, [r2, r1]
    cmn r0, #1
    beq .nx_cl
    bl hal_close_handle
.nx_cl:
    add r5, r5, #1
    b .cl_lp

.free_ok:
    ldrb r5, [r4, #VM86_CTX_A32.bExitCode]
    mov r0, r7
    mov r1, #0
    mov_imm32 r2, 0x8000 ; MEM_RELEASE
    bl hal_virtual_free
    mov r0, r5
    mov r1, #0
    add sp, sp, #208
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.fnf_err:
    mov r5, #ERR_FILE_NOT_FOUND
    b .free_err
.bad_img:
    mov_imm32 r5, ERR_BAD_COMMAND
.free_err:
    mov r0, r7
    mov r1, #0
    mov_imm32 r2, 0x8000
    bl hal_virtual_free
    mov r0, #1
    mov r1, r5
    add sp, sp, #208
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.no_mem:
    mov r0, #1
    mov r1, #ERR_OUT_OF_MEMORY
    add sp, sp, #208
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc
