;; @file vm86_exec_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 8086/80186 CPU interpreter (X19-X28 pinned, 256-entry table dispatch, X18 TEB untouched) & .COM/MZ loader.

@cpu arm64
@format inc

;; @proc vm86_check_jcc_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = opcode low nibble (0..15)
;; @out w0 = 1 if branch taken, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Evaluates all 16 Intel 8086 Jcc condition predicates.
@proc vm86_check_jcc_a64
    ldrh w2, [x0, #VM86_CTX_A64.regFlags]
    and w3, w1, #1 ; invert bit
    lsr w1, w1, #1
    mov w0, #0

    cbz w1, .c_o
    cmp w1, #1
    b.eq .c_b
    cmp w1, #2
    b.eq .c_z
    cmp w1, #3
    b.eq .c_be
    cmp w1, #4
    b.eq .c_s
    cmp w1, #5
    b.eq .c_p
    cmp w1, #6
    b.eq .c_l
    ; 7 = JLE / JG: ZF=1 or SF!=OF
    tbnz w2, #6, .one
    b .c_l

.c_o:
    tbnz w2, #11, .one
    b .inv
.c_b:
    tbnz w2, #0, .one
    b .inv
.c_z:
    tbnz w2, #6, .one
    b .inv
.c_be:
    tbnz w2, #0, .one
    tbnz w2, #6, .one
    b .inv
.c_s:
    tbnz w2, #7, .one
    b .inv
.c_p:
    tbnz w2, #2, .one
    b .inv
.c_l:
    lsr w0, w2, #7
    lsr w1, w2, #11
    eor w0, w0, w1
    and w0, w0, #1
    b .inv
.one:
    mov w0, #1
.inv:
    eor w0, w0, w3
    ret
@endproc

;; @proc vm86_step_str_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = string opcode (0xA4..0xAF)
;; @out Executes one or REP-counted 8086 string operation (MOVS, CMPS, STOS, LODS, SCAS)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc 8086 string primitive engine honoring DF, REP, REPNE, and segment overrides on AArch64.
@proc vm86_step_str_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp

    mov x19, x0 ; ctx
    mov w20, w1 ; op

    ldrb w0, [x19, #VM86_CTX_A64.bRepMode]
    cbz w0, .calc_step
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w1, [x2, #2] ; CX
    cbz w1, .done

.calc_step:
    mov w21, #1
    tbz w20, #0, .chk_df
    mov w21, #2
.chk_df:
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    tbz w0, #10, .rep_lp ; DF = 0x0400 (bit 10)
    neg w21, w21

.rep_lp:
    cmp w20, #0xA4
    b.eq .movsb_op
    cmp w20, #0xA5
    b.eq .movsw_op
    cmp w20, #0xA6
    b.eq .cmpsb_op
    cmp w20, #0xA7
    b.eq .cmpsw_op
    cmp w20, #0xAA
    b.eq .stosb_op
    cmp w20, #0xAB
    b.eq .stosw_op
    cmp w20, #0xAC
    b.eq .lodsb_op
    cmp w20, #0xAD
    b.eq .lodsw_op
    cmp w20, #0xAE
    b.eq .scasb_op
    b .scasw_op

.movsb_op:
    mov x0, x19
    mov w1, #3
    bl vm86_eff_seg_a64
    mov w1, w0
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #12] ; SI
    mov x0, x19
    bl vm86_lin_a64
    ldrb w22, [x0, #0]
    ldrh w1, [x19, #VM86_CTX_A64.sregs16] ; ES
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14] ; DI
    mov x0, x19
    bl vm86_lin_a64
    strb w22, [x0, #0]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #12]
    add w0, w0, w21
    strh w0, [x3, #12]
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]
    b .rep_no_zf

.movsw_op:
    mov x0, x19
    mov w1, #3
    bl vm86_eff_seg_a64
    mov w1, w0
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #12]
    mov x0, x19
    bl vm86_lin_a64
    ldrh w22, [x0, #0]
    ldrh w1, [x19, #VM86_CTX_A64.sregs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14]
    mov x0, x19
    bl vm86_lin_a64
    strh w22, [x0, #0]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #12]
    add w0, w0, w21
    strh w0, [x3, #12]
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]
    b .rep_no_zf

.stosb_op:
    ldrh w1, [x19, #VM86_CTX_A64.sregs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14]
    mov x0, x19
    bl vm86_lin_a64
    ldrb w1, [x19, #VM86_CTX_A64.regs16]
    strb w1, [x0, #0]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]
    b .rep_no_zf

.stosw_op:
    ldrh w1, [x19, #VM86_CTX_A64.sregs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14]
    mov x0, x19
    bl vm86_lin_a64
    ldrh w1, [x19, #VM86_CTX_A64.regs16]
    strh w1, [x0, #0]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]
    b .rep_no_zf

.lodsb_op:
    mov x0, x19
    mov w1, #3
    bl vm86_eff_seg_a64
    mov w1, w0
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #12]
    mov x0, x19
    bl vm86_lin_a64
    ldrb w0, [x0, #0]
    strb w0, [x19, #VM86_CTX_A64.regs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #12]
    add w0, w0, w21
    strh w0, [x3, #12]
    b .rep_no_zf

.lodsw_op:
    mov x0, x19
    mov w1, #3
    bl vm86_eff_seg_a64
    mov w1, w0
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #12]
    mov x0, x19
    bl vm86_lin_a64
    ldrh w0, [x0, #0]
    strh w0, [x19, #VM86_CTX_A64.regs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #12]
    add w0, w0, w21
    strh w0, [x3, #12]
    b .rep_no_zf

.scasb_op:
    ldrh w1, [x19, #VM86_CTX_A64.sregs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14]
    mov x0, x19
    bl vm86_lin_a64
    ldrb w3, [x0, #0]
    ldrb w2, [x19, #VM86_CTX_A64.regs16]
    mov x0, x19
    mov w1, #7
    mov w4, #0
    bl vm86_alu_a64
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]
    b .rep_zf

.scasw_op:
    ldrh w1, [x19, #VM86_CTX_A64.sregs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14]
    mov x0, x19
    bl vm86_lin_a64
    ldrh w3, [x0, #0]
    ldrh w2, [x19, #VM86_CTX_A64.regs16]
    mov x0, x19
    mov w1, #7
    mov w4, #1
    bl vm86_alu_a64
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]
    b .rep_zf

.cmpsb_op:
    mov x0, x19
    mov w1, #3
    bl vm86_eff_seg_a64
    mov w1, w0
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #12]
    mov x0, x19
    bl vm86_lin_a64
    ldrb w22, [x0, #0]
    ldrh w1, [x19, #VM86_CTX_A64.sregs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14]
    mov x0, x19
    bl vm86_lin_a64
    ldrb w3, [x0, #0]
    mov w2, w22
    mov x0, x19
    mov w1, #7
    mov w4, #0
    bl vm86_alu_a64
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #12]
    add w0, w0, w21
    strh w0, [x3, #12]
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]
    b .rep_zf

.cmpsw_op:
    mov x0, x19
    mov w1, #3
    bl vm86_eff_seg_a64
    mov w1, w0
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #12]
    mov x0, x19
    bl vm86_lin_a64
    ldrh w22, [x0, #0]
    ldrh w1, [x19, #VM86_CTX_A64.sregs16]
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x3, #14]
    mov x0, x19
    bl vm86_lin_a64
    ldrh w3, [x0, #0]
    mov w2, w22
    mov x0, x19
    mov w1, #7
    mov w4, #1
    bl vm86_alu_a64
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x3, #12]
    add w0, w0, w21
    strh w0, [x3, #12]
    ldrh w0, [x3, #14]
    add w0, w0, w21
    strh w0, [x3, #14]

.rep_zf:
    ldrb w0, [x19, #VM86_CTX_A64.bRepMode]
    cbz w0, .done
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w1, [x3, #2]
    sub w1, w1, #1
    and w1, w1, #0xFFFF
    strh w1, [x3, #2]
    cbz w1, .done
    ldrh w2, [x19, #VM86_CTX_A64.regFlags]
    cmp w0, #0xF3
    b.eq .chk_repe
    tbz w2, #6, .rep_lp
    b .done
.chk_repe:
    tbnz w2, #6, .rep_lp
    b .done

.rep_no_zf:
    ldrb w0, [x19, #VM86_CTX_A64.bRepMode]
    cbz w0, .done
    add x3, x19, #VM86_CTX_A64.regs16
    ldrh w1, [x3, #2]
    sub w1, w1, #1
    and w1, w1, #0xFFFF
    strh w1, [x3, #2]
    cbnz w1, .rep_lp

.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_step_low_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = primary opcode (0x00..0x3F)
;; @out Executes ALU r/m, r/imm, or PUSH/POP sreg
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Handles opcodes 0x00..0x3F (ADD, OR, ADC, SBB, AND, SUB, XOR, CMP, PUSH/POP ES/CS/SS/DS).
@proc vm86_step_low_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp

    mov x19, x0 ; ctx
    mov w20, w1 ; op

    and w0, w20, #7
    cmp w0, #6
    b.eq .push_sreg
    cmp w0, #7
    b.eq .pop_sreg
    cmp w0, #4
    b.eq .alu_al_imm8
    cmp w0, #5
    b.eq .alu_ax_imm16

    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0 ; EA ptr
    and w2, w20, #1
    mov x0, x19
    bl vm86_reg_ptr_a64
    mov x22, x0 ; reg ptr

    lsr w23, w20, #3
    and w23, w23, #7 ; alu_op
    and w24, w20, #1 ; is_word

    tbnz w20, #1, .to_reg
    cbnz w24, .mr16
    ldrb w2, [x21, #0]
    ldrb w3, [x22, #0]
    mov x0, x19
    mov w1, w23
    mov w4, #0
    bl vm86_alu_a64
    cmp w23, #7
    b.eq .done
    strb w0, [x21, #0]
    b .done
.mr16:
    ldrh w2, [x21, #0]
    ldrh w3, [x22, #0]
    mov x0, x19
    mov w1, w23
    mov w4, #1
    bl vm86_alu_a64
    cmp w23, #7
    b.eq .done
    strh w0, [x21, #0]
    b .done

.to_reg:
    cbnz w24, .rm16
    ldrb w2, [x22, #0]
    ldrb w3, [x21, #0]
    mov x0, x19
    mov w1, w23
    mov w4, #0
    bl vm86_alu_a64
    cmp w23, #7
    b.eq .done
    strb w0, [x22, #0]
    b .done
.rm16:
    ldrh w2, [x22, #0]
    ldrh w3, [x21, #0]
    mov x0, x19
    mov w1, w23
    mov w4, #1
    bl vm86_alu_a64
    cmp w23, #7
    b.eq .done
    strh w0, [x22, #0]
    b .done

.alu_al_imm8:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w3, w0
    ldrb w2, [x19, #VM86_CTX_A64.regs16]
    lsr w23, w20, #3
    and w23, w23, #7
    mov x0, x19
    mov w1, w23
    mov w4, #0
    bl vm86_alu_a64
    cmp w23, #7
    b.eq .done
    strb w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.alu_ax_imm16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w3, w0
    ldrh w2, [x19, #VM86_CTX_A64.regs16]
    lsr w23, w20, #3
    and w23, w23, #7
    mov x0, x19
    mov w1, w23
    mov w4, #1
    bl vm86_alu_a64
    cmp w23, #7
    b.eq .done
    strh w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.push_sreg:
    lsr w1, w20, #3
    and w1, w1, #3
    uxtw x1, w1
    lsl x1, x1, #1
    add x2, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x2, x1]
    mov x0, x19
    bl vm86_push16_a64
    b .done

.pop_sreg:
    lsr w21, w20, #3
    and w21, w21, #3
    uxtw x21, w21
    lsl x21, x21, #1
    mov x0, x19
    bl vm86_pop16_a64
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, x21]

.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_step_mid_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = primary opcode (0x40..0xBF)
;; @out Executes INC/DEC/PUSH/POP r16, Jcc, GRP1 imm, TEST/XCHG/MOV/LEA, CBW/CWD/PUSHF/POPF, MOV moffs, String, MOV imm
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Handles opcodes 0x40..0xBF on 64-bit AArch64.
@proc vm86_step_mid_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp

    mov x19, x0
    mov w20, w1

    cmp w20, #0x47
    b.ls .inc_r16
    cmp w20, #0x4F
    b.ls .dec_r16
    cmp w20, #0x57
    b.ls .push_r16
    cmp w20, #0x5F
    b.ls .pop_r16
    cmp w20, #0x68
    b.eq .push_imm16
    cmp w20, #0x6A
    b.eq .push_imm8
    cmp w20, #0x6F
    b.ls .done
    cmp w20, #0x7F
    b.ls .jcc_rel8
    cmp w20, #0x83
    b.ls .grp1_imm
    cmp w20, #0x85
    b.ls .test_rm_r
    cmp w20, #0x87
    b.ls .xchg_rm_r
    cmp w20, #0x8B
    b.ls .mov_rm_r
    cmp w20, #0x8C
    b.eq .mov_rm_sreg
    cmp w20, #0x8D
    b.eq .lea_r16
    cmp w20, #0x8E
    b.eq .mov_sreg_rm
    cmp w20, #0x8F
    b.eq .pop_rm16
    cmp w20, #0x97
    b.ls .xchg_ax_r16
    cmp w20, #0x98
    b.eq .cbw_op
    cmp w20, #0x99
    b.eq .cwd_op
    cmp w20, #0x9A
    b.eq .call_far_ptr
    cmp w20, #0x9C
    b.eq .pushf_op
    cmp w20, #0x9D
    b.eq .popf_op
    cmp w20, #0x9E
    b.eq .sahf_op
    cmp w20, #0x9F
    b.eq .lahf_op
    cmp w20, #0xA3
    b.ls .mov_moffs
    cmp w20, #0xA7
    b.ls .str_op
    cmp w20, #0xA9
    b.ls .test_acc_imm
    cmp w20, #0xAF
    b.ls .str_op
    cmp w20, #0xB7
    b.ls .mov_r8_imm
    b .mov_r16_imm

.inc_r16:
    and w1, w20, #7
    uxtw x1, w1
    lsl x1, x1, #1
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, x1]
    add w0, w0, #1
    strh w0, [x2, x1]
    mov w1, w0
    mov x0, x19
    bl vm86_set_szp16_a64
    b .done

.dec_r16:
    and w1, w20, #7
    uxtw x1, w1
    lsl x1, x1, #1
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, x1]
    sub w0, w0, #1
    strh w0, [x2, x1]
    mov w1, w0
    mov x0, x19
    bl vm86_set_szp16_a64
    b .done

.push_r16:
    and w1, w20, #7
    uxtw x1, w1
    lsl x1, x1, #1
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w1, [x2, x1]
    mov x0, x19
    bl vm86_push16_a64
    b .done

.pop_r16:
    and w21, w20, #7
    uxtw x21, w21
    lsl x21, x21, #1
    mov x0, x19
    bl vm86_pop16_a64
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, x21]
    b .done

.push_imm16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w1, w0
    mov x0, x19
    bl vm86_push16_a64
    b .done

.push_imm8:
    mov x0, x19
    bl vm86_fetch8_a64
    sxtb w1, w0
    mov x0, x19
    bl vm86_push16_a64
    b .done

.jcc_rel8:
    mov x0, x19
    bl vm86_fetch8_a64
    sxtb w21, w0
    and w1, w20, #0x0F
    mov x0, x19
    bl vm86_check_jcc_a64
    cbz w0, .done
    ldrh w0, [x19, #VM86_CTX_A64.regIp]
    add w0, w0, w21
    strh w0, [x19, #VM86_CTX_A64.regIp]
    b .done

.grp1_imm:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0 ; EA
    mov w22, w1 ; sub-op 0..7
    cmp w20, #0x81
    b.eq .g1_w16
    cmp w20, #0x83
    b.eq .g1_s8
    mov x0, x19
    bl vm86_fetch8_a64
    mov w3, w0
    ldrb w2, [x21, #0]
    mov x0, x19
    mov w1, w22
    mov w4, #0
    bl vm86_alu_a64
    cmp w22, #7
    b.eq .done
    strb w0, [x21, #0]
    b .done
.g1_w16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w23, w0
    b .g1_do16
.g1_s8:
    mov x0, x19
    bl vm86_fetch8_a64
    sxtb w23, w0
.g1_do16:
    ldrh w2, [x21, #0]
    mov w3, w23
    mov x0, x19
    mov w1, w22
    mov w4, #1
    bl vm86_alu_a64
    cmp w22, #7
    b.eq .done
    strh w0, [x21, #0]
    b .done

.test_rm_r:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    and w2, w20, #1
    mov x0, x19
    bl vm86_reg_ptr_a64
    mov x22, x0
    tbnz w20, #0, .tst16
    ldrb w2, [x21, #0]
    ldrb w3, [x22, #0]
    mov x0, x19
    mov w1, #4
    mov w4, #0
    bl vm86_alu_a64
    b .done
.tst16:
    ldrh w2, [x21, #0]
    ldrh w3, [x22, #0]
    mov x0, x19
    mov w1, #4
    mov w4, #1
    bl vm86_alu_a64
    b .done

.xchg_rm_r:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    and w2, w20, #1
    mov x0, x19
    bl vm86_reg_ptr_a64
    mov x22, x0
    tbnz w20, #0, .xcg16
    ldrb w0, [x21, #0]
    ldrb w1, [x22, #0]
    strb w1, [x21, #0]
    strb w0, [x22, #0]
    b .done
.xcg16:
    ldrh w0, [x21, #0]
    ldrh w1, [x22, #0]
    strh w1, [x21, #0]
    strh w0, [x22, #0]
    b .done

.mov_rm_r:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    and w2, w20, #1
    mov x0, x19
    bl vm86_reg_ptr_a64
    mov x22, x0
    tbnz w20, #1, .mv_to_reg
    tbnz w20, #0, .mv_mr16
    ldrb w0, [x22, #0]
    strb w0, [x21, #0]
    b .done
.mv_mr16:
    ldrh w0, [x22, #0]
    strh w0, [x21, #0]
    b .done
.mv_to_reg:
    tbnz w20, #0, .mv_rm16
    ldrb w0, [x21, #0]
    strb w0, [x22, #0]
    b .done
.mv_rm16:
    ldrh w0, [x21, #0]
    strh w0, [x22, #0]
    b .done

.mov_rm_sreg:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    mov w2, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    and w1, w1, #3
    uxtw x1, w1
    lsl x1, x1, #1
    add x2, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x2, x1]
    strh w1, [x0, #0]
    b .done

.mov_sreg_rm:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    mov w2, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    and w21, w1, #3
    uxtw x21, w21
    lsl x21, x21, #1
    ldrh w0, [x0, #0]
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, x21]
    b .done

.lea_r16:
    strb wzr, [x19, #VM86_CTX_A64.bSegOvr]
    ldrh w21, [x19, #VM86_CTX_A64.sregs16]
    strh wzr, [x19, #VM86_CTX_A64.sregs16]
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    mov w2, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    strh w21, [x19, #VM86_CTX_A64.sregs16]
    ldr x2, [x19, #VM86_CTX_A64.pMem]
    sub x0, x0, x2
    uxtw x1, w1
    lsl x1, x1, #1
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, x1]
    b .done

.pop_rm16:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    mov w2, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    mov x0, x19
    bl vm86_pop16_a64
    strh w0, [x21, #0]
    b .done

.xchg_ax_r16:
    and w1, w20, #7
    uxtw x1, w1
    lsl x1, x1, #1
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, #0]
    ldrh w3, [x2, x1]
    strh w3, [x2, #0]
    strh w0, [x2, x1]
    b .done

.cbw_op:
    ldrsb w0, [x19, #VM86_CTX_A64.regs16]
    strh w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.cwd_op:
    ldrsh w0, [x19, #VM86_CTX_A64.regs16]
    asr w0, w0, #16
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, #4]
    b .done

.call_far_ptr:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w21, w0
    mov x0, x19
    bl vm86_fetch16_a64
    mov w22, w0
    add x2, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x2, #2]
    mov x0, x19
    bl vm86_push16_a64
    ldrh w1, [x19, #VM86_CTX_A64.regIp]
    mov x0, x19
    bl vm86_push16_a64
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w22, [x2, #2]
    strh w21, [x19, #VM86_CTX_A64.regIp]
    b .done

.pushf_op:
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    mov_imm32 w2, 0xF002
    orr w1, w1, w2
    mov x0, x19
    bl vm86_push16_a64
    b .done

.popf_op:
    mov x0, x19
    bl vm86_pop16_a64
    orr w0, w0, #2
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done

.sahf_op:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrb w0, [x1, #1]
    orr w0, w0, #2
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    and w1, w1, #0xFF00
    orr w1, w1, w0
    strh w1, [x19, #VM86_CTX_A64.regFlags]
    b .done

.lahf_op:
    ldrb w0, [x19, #VM86_CTX_A64.regFlags]
    orr w0, w0, #2
    add x1, x19, #VM86_CTX_A64.regs16
    strb w0, [x1, #1]
    b .done

.mov_moffs:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w21, w0
    mov x0, x19
    mov w1, #3
    bl vm86_eff_seg_a64
    mov w1, w0
    mov w2, w21
    mov x0, x19
    bl vm86_lin_a64
    cmp w20, #0xA0
    b.eq .m_a0
    cmp w20, #0xA1
    b.eq .m_a1
    cmp w20, #0xA2
    b.eq .m_a2
    ldrh w1, [x19, #VM86_CTX_A64.regs16]
    strh w1, [x0, #0]
    b .done
.m_a0:
    ldrb w0, [x0, #0]
    strb w0, [x19, #VM86_CTX_A64.regs16]
    b .done
.m_a1:
    ldrh w0, [x0, #0]
    strh w0, [x19, #VM86_CTX_A64.regs16]
    b .done
.m_a2:
    ldrb w1, [x19, #VM86_CTX_A64.regs16]
    strb w1, [x0, #0]
    b .done

.str_op:
    mov x0, x19
    mov w1, w20
    bl vm86_step_str_a64
    b .done

.test_acc_imm:
    tbnz w20, #0, .tai16
    mov x0, x19
    bl vm86_fetch8_a64
    mov w3, w0
    ldrb w2, [x19, #VM86_CTX_A64.regs16]
    mov x0, x19
    mov w1, #4
    mov w4, #0
    bl vm86_alu_a64
    b .done
.tai16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w3, w0
    ldrh w2, [x19, #VM86_CTX_A64.regs16]
    mov x0, x19
    mov w1, #4
    mov w4, #1
    bl vm86_alu_a64
    b .done

.mov_r8_imm:
    mov x0, x19
    and w1, w20, #7
    mov w2, #0
    bl vm86_reg_ptr_a64
    mov x21, x0
    mov x0, x19
    bl vm86_fetch8_a64
    strb w0, [x21, #0]
    b .done

.mov_r16_imm:
    and w21, w20, #7
    uxtw x21, w21
    lsl x21, x21, #1
    mov x0, x19
    bl vm86_fetch16_a64
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, x21]

.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_step_high_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = primary opcode (0xC0..0xFF)
;; @out Executes RET, LES/LDS, MOV rm/imm, INT, GRP2 shifts, LOOP/JCXZ, CALL/JMP, HLT, GRP3 mul/div/neg/not, flags, GRP4/5
;; @err Sets bHalted = 1 on HLT, INT 20h, ret-0, or divide fault
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Handles opcodes 0xC0..0xFF using hardware-trap-safe division on AArch64.
@proc vm86_step_high_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp

    mov x19, x0
    mov w20, w1

    cmp w20, #0xC1
    b.ls .grp2_imm_shift
    cmp w20, #0xC2
    b.eq .ret_imm16
    cmp w20, #0xC3
    b.eq .ret_near
    cmp w20, #0xC4
    b.eq .les_op
    cmp w20, #0xC5
    b.eq .lds_op
    cmp w20, #0xC7
    b.ls .mov_rm_imm
    cmp w20, #0xCA
    b.eq .retf_imm16
    cmp w20, #0xCB
    b.eq .retf_op
    cmp w20, #0xCC
    b.eq .done
    cmp w20, #0xCD
    b.eq .int_imm8
    cmp w20, #0xCF
    b.eq .iret_op
    cmp w20, #0xD3
    b.ls .grp2_shift
    cmp w20, #0xDF
    b.ls .done
    cmp w20, #0xE3
    b.ls .loop_jcxz
    cmp w20, #0xE7
    b.ls .in_out_imm
    cmp w20, #0xE8
    b.eq .call_rel16
    cmp w20, #0xE9
    b.eq .jmp_rel16
    cmp w20, #0xEA
    b.eq .jmp_far_ptr
    cmp w20, #0xEB
    b.eq .jmp_rel8
    cmp w20, #0xEF
    b.ls .done
    cmp w20, #0xF4
    b.eq .hlt_op
    cmp w20, #0xF5
    b.eq .cmc_op
    cmp w20, #0xF7
    b.ls .grp3_op
    cmp w20, #0xFD
    b.ls .flag_ops
    b .grp45_op

.in_out_imm:
    mov x0, x19
    bl vm86_fetch8_a64
    b .done

.ret_imm16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w21, w0
    mov x0, x19
    bl vm86_pop16_a64
    strh w0, [x19, #VM86_CTX_A64.regIp]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, #8]
    add w0, w0, w21
    strh w0, [x2, #8]
    b .done

.ret_near:
    mov x0, x19
    bl vm86_pop16_a64
    cbz w0, .ret_zero_psp
    strh w0, [x19, #VM86_CTX_A64.regIp]
    b .done
.ret_zero_psp:
    strb wzr, [x19, #VM86_CTX_A64.bExitCode]
    mov w0, #1
    strb w0, [x19, #VM86_CTX_A64.bHalted]
    b .done

.les_op:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    mov w2, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    uxtw x22, w1
    lsl x22, x22, #1
    ldrh w0, [x21, #0]
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, x22]
    ldrh w0, [x21, #2]
    strh w0, [x19, #VM86_CTX_A64.sregs16]
    b .done

.lds_op:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    mov w2, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    uxtw x22, w1
    lsl x22, x22, #1
    ldrh w0, [x21, #0]
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, x22]
    ldrh w0, [x21, #2]
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #6]
    b .done

.mov_rm_imm:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    tbnz w20, #0, .mrmi16
    mov x0, x19
    bl vm86_fetch8_a64
    strb w0, [x21, #0]
    b .done
.mrmi16:
    mov x0, x19
    bl vm86_fetch16_a64
    strh w0, [x21, #0]
    b .done

.retf_imm16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w21, w0
    mov x0, x19
    bl vm86_pop16_a64
    strh w0, [x19, #VM86_CTX_A64.regIp]
    mov x0, x19
    bl vm86_pop16_a64
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #2]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, #8]
    add w0, w0, w21
    strh w0, [x2, #8]
    b .done

.retf_op:
    mov x0, x19
    bl vm86_pop16_a64
    strh w0, [x19, #VM86_CTX_A64.regIp]
    mov x0, x19
    bl vm86_pop16_a64
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #2]
    b .done

.int_imm8:
    mov x0, x19
    bl vm86_fetch8_a64
    cmp w0, #0x20
    b.eq .ret_zero_psp
    cmp w0, #0x21
    b.eq .do_int21
    cmp w0, #0x10
    b.eq .do_int10
    cmp w0, #0x16
    b.eq .do_int16
    cmp w0, #0x1A
    b.eq .do_int1a
    cmp w0, #0x25
    b.eq .do_int25_26
    cmp w0, #0x26
    b.eq .do_int25_26
    cmp w0, #0x2F
    b.eq .do_int2f
    cmp w0, #0x67
    b.eq .do_int67
    b .done

.do_int21:
    mov x0, x19
    bl vm86_dos_int21_a64
    b .done

.do_int10:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrb w0, [x1, #1] ; AH
    cmp w0, #0x0E
    b.eq .int10_tty
    cmp w0, #0x0F
    b.ne .done
    ; AH=0Fh Get Video Mode -> AL=03h (80x25 color), AH=80 cols, BH=0
    mov_imm32 w0, 0x5003
    strh w0, [x1, #0]
    strb wzr, [x1, #7]
    b .done
.int10_tty:
    ldrb w0, [x1, #0] ; AL
    strb w0, [sp, #0]
    strb wzr, [sp, #1]
    mov x0, sp
    bl out_str_a64
    b .done

.do_int16:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrb w0, [x1, #1] ; AH
    cmp w0, #0x01
    b.eq .int16_poll
    ; AH=00h Read Key -> read 1 byte from stdin or 0x0D
    add x0, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x0, #0]
    mov x1, sp
    mov w2, #1
    bl hal_read_handle_a64
    mov w2, #0x0D
    cbz w0, .st_k16
    ldrb w2, [sp, #0]
.st_k16:
    strh w2, [x19, #VM86_CTX_A64.regs16]
    b .done
.int16_poll:
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    orr w0, w0, #0x40 ; ZF=1 no key waiting
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done

.do_int1a:
    add x1, x19, #VM86_CTX_A64.regs16
    strh wzr, [x1, #2]
    mov w0, #100
    strh w0, [x1, #4]
    strb wzr, [x1, #0]
    b .done

.do_int25_26:
    ; Authentic DOS INT 25h/26h contract: push FLAGS onto stack and return CF=1
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    mov x0, x19
    bl vm86_push16_a64
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    orr w0, w0, #1
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    mov_imm32 w0, 0x0207
    strh w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.do_int2f:
    strh wzr, [x19, #VM86_CTX_A64.regs16]
    b .done

.do_int67:
    ; EMS not present -> AH = 0x84
    add x1, x19, #VM86_CTX_A64.regs16
    mov w0, #0x84
    strb w0, [x1, #1]
    b .done

.iret_op:
    mov x0, x19
    bl vm86_pop16_a64
    strh w0, [x19, #VM86_CTX_A64.regIp]
    mov x0, x19
    bl vm86_pop16_a64
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #2]
    mov x0, x19
    bl vm86_pop16_a64
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done

.grp2_imm_shift:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0 ; EA
    mov w22, w1 ; sub-op
    mov x0, x19
    bl vm86_fetch8_a64
    mov w23, w0 ; imm8 shift count
    b .do_shift_cnt

.grp2_shift:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0 ; EA
    mov w22, w1 ; sub-op 0..7
    mov w23, #1
    tbz w20, #1, .do_shift_cnt
    add x1, x19, #VM86_CTX_A64.regs16
    ldrb w23, [x1, #2] ; CL
.do_shift_cnt:
    and w23, w23, #31
    cbz w23, .done
    tbnz w20, #0, .sh16
    ldrb w0, [x21, #0]
    cmp w22, #5 ; SHR
    b.eq .shr8
    cmp w22, #7 ; SAR
    b.eq .sar8
    cmp w22, #0 ; ROL
    b.eq .rol8
    cmp w22, #1 ; ROR
    b.eq .ror8
    lsl w0, w0, w23
    lsr w24, w0, #8
    and w24, w24, #1
    b .st_sh8
.shr8:
    sub w1, w23, #1
    lsr w24, w0, w1
    and w24, w24, #1
    lsr w0, w0, w23
    b .st_sh8
.sar8:
    sxtb w0, w0
    sub w1, w23, #1
    asr w24, w0, w1
    and w24, w24, #1
    asr w0, w0, w23
    b .st_sh8
.rol8:
    and w1, w23, #7
    cbz w1, .st_sh8_nocf
    lsl w2, w0, w1
    mov w3, #8
    sub w3, w3, w1
    lsr w0, w0, w3
    orr w0, w0, w2
    and w24, w0, #1
    b .st_sh8
.ror8:
    and w1, w23, #7
    cbz w1, .st_sh8_nocf
    lsr w2, w0, w1
    mov w3, #8
    sub w3, w3, w1
    lsl w0, w0, w3
    orr w0, w0, w2
    lsr w24, w0, #7
    and w24, w24, #1
    b .st_sh8
.st_sh8_nocf:
    ldrh w24, [x19, #VM86_CTX_A64.regFlags]
    and w24, w24, #1
.st_sh8:
    and w0, w0, #0xFF
    strb w0, [x21, #0]
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    and w1, w1, #0xFFFE
    orr w1, w1, w24
    strh w1, [x19, #VM86_CTX_A64.regFlags]
    cmp w22, #4
    b.lo .done
    mov w1, w0
    mov x0, x19
    bl vm86_set_szp8_a64
    b .done

.sh16:
    ldrh w0, [x21, #0]
    cmp w22, #5
    b.eq .shr16
    cmp w22, #7
    b.eq .sar16
    cmp w22, #0
    b.eq .rol16
    cmp w22, #1
    b.eq .ror16
    lsl w0, w0, w23
    lsr w24, w0, #16
    and w24, w24, #1
    b .st_sh16
.shr16:
    sub w1, w23, #1
    lsr w24, w0, w1
    and w24, w24, #1
    lsr w0, w0, w23
    b .st_sh16
.sar16:
    sxth w0, w0
    sub w1, w23, #1
    asr w24, w0, w1
    and w24, w24, #1
    asr w0, w0, w23
    b .st_sh16
.rol16:
    and w1, w23, #15
    cbz w1, .st_sh16_nocf
    lsl w2, w0, w1
    mov w3, #16
    sub w3, w3, w1
    lsr w0, w0, w3
    orr w0, w0, w2
    and w24, w0, #1
    b .st_sh16
.ror16:
    and w1, w23, #15
    cbz w1, .st_sh16_nocf
    lsr w2, w0, w1
    mov w3, #16
    sub w3, w3, w1
    lsl w0, w0, w3
    orr w0, w0, w2
    lsr w24, w0, #15
    and w24, w24, #1
    b .st_sh16
.st_sh16_nocf:
    ldrh w24, [x19, #VM86_CTX_A64.regFlags]
    and w24, w24, #1
.st_sh16:
    and w23, w0, #0xFFFF
    strh w23, [x21, #0]
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    and w1, w1, #0xFFFE
    orr w1, w1, w24
    strh w1, [x19, #VM86_CTX_A64.regFlags]
    cmp w22, #4
    b.lo .done
    mov w1, w23
    mov x0, x19
    bl vm86_set_szp16_a64
    b .done

.loop_jcxz:
    mov x0, x19
    bl vm86_fetch8_a64
    sxtb w21, w0
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, #2] ; CX
    cmp w20, #0xE3
    b.eq .jcxz_c
    sub w0, w0, #1
    and w0, w0, #0xFFFF
    strh w0, [x2, #2]
    cbz w0, .done
    cmp w20, #0xE2
    b.eq .take_rel
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    cmp w20, #0xE1
    b.eq .loope_c
    tbz w1, #6, .take_rel
    b .done
.loope_c:
    tbnz w1, #6, .take_rel
    b .done
.jcxz_c:
    cbnz w0, .done
.take_rel:
    ldrh w0, [x19, #VM86_CTX_A64.regIp]
    add w0, w0, w21
    strh w0, [x19, #VM86_CTX_A64.regIp]
    b .done

.call_rel16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w21, w0
    ldrh w1, [x19, #VM86_CTX_A64.regIp]
    mov x0, x19
    bl vm86_push16_a64
    ldrh w0, [x19, #VM86_CTX_A64.regIp]
    add w0, w0, w21
    strh w0, [x19, #VM86_CTX_A64.regIp]
    b .done

.jmp_rel16:
    mov x0, x19
    bl vm86_fetch16_a64
    ldrh w1, [x19, #VM86_CTX_A64.regIp]
    add w1, w1, w0
    strh w1, [x19, #VM86_CTX_A64.regIp]
    b .done

.jmp_far_ptr:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w21, w0
    mov x0, x19
    bl vm86_fetch16_a64
    strh w21, [x19, #VM86_CTX_A64.regIp]
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #2]
    b .done

.jmp_rel8:
    mov x0, x19
    bl vm86_fetch8_a64
    sxtb w0, w0
    ldrh w1, [x19, #VM86_CTX_A64.regIp]
    add w1, w1, w0
    strh w1, [x19, #VM86_CTX_A64.regIp]
    b .done

.hlt_op:
    mov w0, #1
    strb w0, [x19, #VM86_CTX_A64.bHalted]
    b .done

.cmc_op:
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    eor w0, w0, #1
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done

.grp3_op:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0 ; EA
    mov w22, w1 ; sub-op 0..7
    tbnz w20, #0, .g3_16
    cbz w22, .g3_tst8
    cmp w22, #2
    b.eq .g3_not8
    cmp w22, #3
    b.eq .g3_neg8
    cmp w22, #4
    b.eq .g3_mul8
    cmp w22, #5
    b.eq .g3_imul8
    cmp w22, #6
    b.eq .g3_div8
    b .g3_idiv8

.g3_tst8:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w3, w0
    ldrb w2, [x21, #0]
    mov x0, x19
    mov w1, #4
    mov w4, #0
    bl vm86_alu_a64
    b .done
.g3_not8:
    ldrb w0, [x21, #0]
    mvn w0, w0
    strb w0, [x21, #0]
    b .done
.g3_neg8:
    ldrb w3, [x21, #0]
    mov x0, x19
    mov w1, #5
    mov w2, #0
    mov w4, #0
    bl vm86_alu_a64
    strb w0, [x21, #0]
    b .done
.g3_mul8:
    ldrb w0, [x19, #VM86_CTX_A64.regs16]
    ldrb w1, [x21, #0]
    mul w2, w0, w1
    strh w2, [x19, #VM86_CTX_A64.regs16]
    b .done
.g3_imul8:
    ldrsb w0, [x19, #VM86_CTX_A64.regs16]
    ldrsb w1, [x21, #0]
    mul w2, w0, w1
    strh w2, [x19, #VM86_CTX_A64.regs16]
    b .done
.g3_div8:
    ldrh w0, [x19, #VM86_CTX_A64.regs16]
    ldrb w1, [x21, #0]
    cbz w1, .hlt_op
    udiv w2, w0, w1
    msub w3, w2, w1, w0
    and w2, w2, #0xFF
    orr w2, w2, w3, lsl #8
    strh w2, [x19, #VM86_CTX_A64.regs16]
    b .done
.g3_idiv8:
    ldrsh w0, [x19, #VM86_CTX_A64.regs16]
    ldrsb w1, [x21, #0]
    cbz w1, .hlt_op
    bl math_sdiv32_a64
    and w0, w0, #0xFF
    and w1, w1, #0xFF
    orr w0, w0, w1, lsl #8
    strh w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.g3_16:
    cbz w22, .g3_tst16
    cmp w22, #2
    b.eq .g3_not16
    cmp w22, #3
    b.eq .g3_neg16
    cmp w22, #4
    b.eq .g3_mul16
    cmp w22, #5
    b.eq .g3_imul16
    cmp w22, #6
    b.eq .g3_div16
    b .g3_idiv16

.g3_tst16:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w3, w0
    ldrh w2, [x21, #0]
    mov x0, x19
    mov w1, #4
    mov w4, #1
    bl vm86_alu_a64
    b .done
.g3_not16:
    ldrh w0, [x21, #0]
    mvn w0, w0
    strh w0, [x21, #0]
    b .done
.g3_neg16:
    ldrh w3, [x21, #0]
    mov x0, x19
    mov w1, #5
    mov w2, #0
    mov w4, #1
    bl vm86_alu_a64
    strh w0, [x21, #0]
    b .done
.g3_mul16:
    ldrh w0, [x19, #VM86_CTX_A64.regs16]
    ldrh w1, [x21, #0]
    mul w2, w0, w1
    strh w2, [x19, #VM86_CTX_A64.regs16]
    lsr w2, w2, #16
    add x3, x19, #VM86_CTX_A64.regs16
    strh w2, [x3, #4]
    b .done
.g3_imul16:
    ldrsh w0, [x19, #VM86_CTX_A64.regs16]
    ldrsh w1, [x21, #0]
    mul w2, w0, w1
    strh w2, [x19, #VM86_CTX_A64.regs16]
    lsr w2, w2, #16
    add x3, x19, #VM86_CTX_A64.regs16
    strh w2, [x3, #4]
    b .done
.g3_div16:
    ldrh w1, [x21, #0]
    cbz w1, .hlt_op
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, #4] ; DX
    ldrh w3, [x2, #0] ; AX
    orr w0, w3, w0, lsl #16
    udiv w4, w0, w1
    msub w5, w4, w1, w0
    strh w4, [x2, #0]
    strh w5, [x2, #4]
    b .done
.g3_idiv16:
    ldrsh w1, [x21, #0]
    cbz w1, .hlt_op
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, #4]
    ldrh w3, [x2, #0]
    orr w0, w3, w0, lsl #16
    bl math_sdiv32_a64
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, #0]
    strh w1, [x2, #4]
    b .done

.flag_ops:
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    cmp w20, #0xF8
    b.eq .f_clc
    cmp w20, #0xF9
    b.eq .f_stc
    cmp w20, #0xFC
    b.eq .f_cld
    cmp w20, #0xFD
    b.eq .f_std
    b .done
.f_clc:
    and w0, w0, #0xFFFE
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done
.f_stc:
    orr w0, w0, #1
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done
.f_cld:
    mov_imm32 w1, 0xFBFF
    and w0, w0, w1
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done
.f_std:
    orr w0, w0, #0x0400
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .done

.grp45_op:
    mov x0, x19
    bl vm86_fetch8_a64
    mov w1, w0
    and w2, w20, #1
    mov x0, x19
    bl vm86_decode_modrm_a64
    mov x21, x0
    mov w22, w1
    tbnz w20, #0, .g5_16
    ldrb w0, [x21, #0]
    cbz w22, .g4_inc8
    sub w0, w0, #1
    b .g4_st8
.g4_inc8:
    add w0, w0, #1
.g4_st8:
    strb w0, [x21, #0]
    mov w1, w0
    mov x0, x19
    bl vm86_set_szp8_a64
    b .done

.g5_16:
    ldrh w23, [x21, #0]
    cbz w22, .g5_inc
    cmp w22, #1
    b.eq .g5_dec
    cmp w22, #2
    b.eq .g5_call_n
    cmp w22, #3
    b.eq .g5_call_f
    cmp w22, #4
    b.eq .g5_jmp_n
    cmp w22, #5
    b.eq .g5_jmp_f
    mov w1, w23
    mov x0, x19
    bl vm86_push16_a64
    b .done
.g5_inc:
    add w23, w23, #1
    strh w23, [x21, #0]
    mov w1, w23
    mov x0, x19
    bl vm86_set_szp16_a64
    b .done
.g5_dec:
    sub w23, w23, #1
    strh w23, [x21, #0]
    mov w1, w23
    mov x0, x19
    bl vm86_set_szp16_a64
    b .done
.g5_call_n:
    ldrh w1, [x19, #VM86_CTX_A64.regIp]
    mov x0, x19
    bl vm86_push16_a64
    strh w23, [x19, #VM86_CTX_A64.regIp]
    b .done
.g5_call_f:
    ldrh w24, [x21, #2]
    add x2, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x2, #2]
    mov x0, x19
    bl vm86_push16_a64
    ldrh w1, [x19, #VM86_CTX_A64.regIp]
    mov x0, x19
    bl vm86_push16_a64
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w24, [x2, #2]
    strh w23, [x19, #VM86_CTX_A64.regIp]
    b .done
.g5_jmp_n:
    strh w23, [x19, #VM86_CTX_A64.regIp]
    b .done
.g5_jmp_f:
    strh w23, [x19, #VM86_CTX_A64.regIp]
    ldrh w0, [x21, #2]
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #2]

.done:
    add sp, sp, #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_step_one_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*
;; @out Decodes prefixes via 256-entry g_vm86_cls_a64 table and dispatches one 8086 instruction
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc 256-entry table-driven single-instruction 8086/80186 decoder for ARM64 vm8086.
@proc vm86_step_one_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp

    mov x19, x0
    mov w1, #0xFF
    strb w1, [x19, #VM86_CTX_A64.bSegOvr]
    strb wzr, [x19, #VM86_CTX_A64.bRepMode]
    adr_sym x20, g_vm86_cls_a64

.pref_lp:
    mov x0, x19
    bl vm86_fetch8_a64
    uxtw x2, w0
    ldrb w3, [x20, x2] ; 256-entry class: 0=low, 1=mid, 2=high, 3..6=ES/CS/SS/DS, 7=REP, 8=LOCK
    cbz w3, .go_low
    cmp w3, #1
    b.eq .go_mid
    cmp w3, #2
    b.eq .go_high
    cmp w3, #6
    b.ls .set_seg_ovr
    cmp w3, #7
    b.eq .set_rep
    b .pref_lp

.set_seg_ovr:
    sub w3, w3, #3
    strb w3, [x19, #VM86_CTX_A64.bSegOvr]
    b .pref_lp

.set_rep:
    strb w0, [x19, #VM86_CTX_A64.bRepMode]
    b .pref_lp

.go_low:
    mov w1, w0
    mov x0, x19
    bl vm86_step_low_a64
    b .exit

.go_mid:
    mov w1, w0
    mov x0, x19
    bl vm86_step_mid_a64
    b .exit

.go_high:
    mov w1, w0
    mov x0, x19
    bl vm86_step_high_a64

.exit:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_run_image_a64
;; @abi win_arm64
;; @in x0 = normalized WTF-8 .COM or DOS MZ .EXE file path, x1 = raw command tail (" arg1 arg2")
;; @out w0 = DOS exit code (0..255), w1 = 0 on success or ERR_* if invalid image
;; @err w1 != 0 if file cannot be opened or is a PE/NE executable
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Allocates 1 MiB + 64 KiB guest RAM, builds DOS PSP + FCB1/FCB2 + env, loads .COM/MZ, and runs pinned X19-X28 loop!
@proc vm86_run_image_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    sub sp, sp, #256 ; VM86_CTX_A64 (256B, 16-byte aligned!) at sp+0
    mov x29, sp

    mov x21, x0 ; path
    mov x22, x1 ; tail
    mov x19, sp ; VM86_CTX_A64*

    mov x0, x19
    mov x1, #256
    bl mem_zero_a64

    ; Allocate 1 MiB + 64 KiB (1,114,112-byte) isolated virtual 8086 address space
    mov x0, #0
    mov_imm32 x1, A64_VM86_MEM_SIZE
    mov_imm32 w2, 0x3000 ; MEM_RESERVE | MEM_COMMIT
    mov w3, #4           ; PAGE_READWRITE
    bl hal_virtual_alloc_a64
    cbz x0, .no_mem
    str x0, [x19, #VM86_CTX_A64.pMem]
    mov x20, x0 ; X20 pinned = pMem

    ; Initialize guest handle table (0..2 = active fdTable[0..2], 3..19 = -1)
    mov w0, #0
    mov x1, #-1
    add x3, x19, #VM86_CTX_A64.hostHandles
.init_h:
    uxtw x2, w0
    lsl x2, x2, #3
    str x1, [x3, x2]
    add w0, w0, #1
    cmp w0, #20
    b.lo .init_h

    bl hal_get_ctx_a64
    mov x23, x0 ; X23 = ARM64_CTX*
    add x1, x23, #ARM64_CTX.fdTable
    add x2, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x1, #0]
    str x0, [x2, #0]
    ldr x0, [x1, #8]
    str x0, [x2, #8]
    ldr x0, [x1, #16]
    str x0, [x2, #16]

    ; Bridge host WTF-8 environment block into segment 0x0060 (offset 0x0600)
    add x0, x20, #0x0600
    ldr x1, [x23, #ARM64_CTX.pEnvBlock]
    ldr w2, [x23, #ARM64_CTX.dwEnvLen]
    mov_imm32 w3, 0x3F00
    cmp w2, w3
    b.ls .cp_env
    mov w2, w3
.cp_env:
    uxtw x2, w2
    bl mem_copy_a64
    strb wzr, [x0, #0]
    strb wzr, [x0, #1]

    ; Build authentic 256-byte PSP at segment 0x1000 (linear 0x10000)
    mov_imm32 w0, 0x1000
    strh w0, [x19, #VM86_CTX_A64.wPspSeg]
    strh w0, [x19, #VM86_CTX_A64.wDtaSeg]
    mov w0, #0x80
    strh w0, [x19, #VM86_CTX_A64.wDtaOff]

    mov_imm32 x0, 0x10000
    add x24, x20, x0 ; PSP base
    mov w0, #0xCD
    strb w0, [x24, #0]
    mov w0, #0x20
    strb w0, [x24, #1]
    mov_imm32 w0, 0x9FFF
    strh w0, [x24, #2]
    mov w0, #0x0060
    strh w0, [x24, #0x2C]

    ; Initialize FCB1 (0x5C) and FCB2 (0x6C) 11-byte blank-padded filename areas
    mov w0, #0x20
    mov w1, #1
.fcb_pad:
    uxtw x2, w1
    add x3, x24, #0x5C
    strb w0, [x3, x2]
    add x3, x24, #0x6C
    strb w0, [x3, x2]
    add w1, w1, #1
    cmp w1, #12
    b.lo .fcb_pad

    ; Populate PSP:0x80 command tail
    mov w2, #0
    cbz x22, .tail_done
.tail_lp:
    cmp w2, #126
    b.hs .tail_done
    uxtw x3, w2
    ldrb w0, [x22, x3]
    cbz w0, .tail_done
    add x1, x24, #0x81
    strb w0, [x1, x3]
    add w2, w2, #1
    b .tail_lp
.tail_done:
    strb w2, [x24, #0x80]
    add x1, x24, #0x81
    uxtw x3, w2
    mov w0, #0x0D
    strb w0, [x1, x3]

    ; Open and read .COM / MZ .EXE into 0x10100
    mov x0, x21
    bl to_u16_longpath_a64
    mov w1, #0
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .fnf_err
    mov x25, x0 ; hFile

    mov_imm32 x0, 0x10100
    add x24, x20, x0 ; image load buffer
    mov x0, x25
    mov x1, x24
    mov_imm32 w2, 0x7FE00
    bl hal_read_handle_a64
    mov w26, w0 ; bytes read
    mov x0, x25
    bl hal_close_handle_a64
    cbz w26, .bad_img

    ; Check 'MZ' / 'ZM' signature vs flat .COM
    ldrh w0, [x24, #0]
    mov_imm32 w1, 0x5A4D
    cmp w0, w1
    b.eq .load_mz
    mov_imm32 w1, 0x4D5A
    cmp w0, w1
    b.eq .load_mz

    ; Flat .COM setup (CS=DS=ES=SS=0x1000, IP=0x0100, SP=0xFFFE, [SS:FFFE]=0x0000)
    mov_imm32 w0, 0x1000
    add x1, x19, #VM86_CTX_A64.sregs16
    strh w0, [x1, #0]
    strh w0, [x1, #2]
    strh w0, [x1, #4]
    strh w0, [x1, #6]
    mov w0, #0x0100
    strh w0, [x19, #VM86_CTX_A64.regIp]
    mov_imm32 w0, 0xFFFE
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #8]
    mov_imm32 w0, 0x0202
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    b .run_vm

.load_mz:
    cmp w26, #28
    b.lo .bad_img
    cmp w26, #0x40
    b.lo .reloc_mz
    ldrh w0, [x24, #0x18]
    cmp w0, #0x40
    b.lo .reloc_mz
    ldr w0, [x24, #0x3C]
    cmp w0, w26
    b.hs .reloc_mz
    uxtw x0, w0
    ldrh w0, [x24, x0]
    mov_imm32 w1, 0x4550 ; 'PE'
    cmp w0, w1
    b.eq .bad_img
    mov_imm32 w1, 0x454E ; 'NE'
    cmp w0, w1
    b.eq .bad_img

.reloc_mz:
    ldrh w0, [x24, #0x08]
    lsl w21, w0, #4 ; hdr_bytes
    cmp w21, w26
    b.hs .bad_img

    ldrh w0, [x24, #0x0E]
    mov_imm32 w1, 0x1010
    add w0, w0, w1
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #4] ; SS
    ldrh w0, [x24, #0x10]
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, #8] ; SP
    ldrh w0, [x24, #0x14]
    strh w0, [x19, #VM86_CTX_A64.regIp]
    ldrh w0, [x24, #0x16]
    mov_imm32 w1, 0x1010
    add w0, w0, w1
    add x2, x19, #VM86_CTX_A64.sregs16
    strh w0, [x2, #2] ; CS
    mov_imm32 w0, 0x1000
    strh w0, [x2, #0] ; ES = PSP
    strh w0, [x2, #6] ; DS = PSP
    mov_imm32 w0, 0x0202
    strh w0, [x19, #VM86_CTX_A64.regFlags]

    ldrh w25, [x24, #0x06] ; reloc_cnt
    ldrh w27, [x24, #0x18] ; reloc_off

    mov x28, #0
    cbz w25, .shift_mz
    uxtw x0, w25
    lsl x0, x0, #2
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .bad_img
    mov x28, x0 ; saved reloc table copy
    uxtw x1, w27
    add x1, x24, x1
    uxtw x2, w25
    lsl x2, x2, #2
    bl mem_copy_a64

.shift_mz:
    sub w2, w26, w21 ; body_len
    uxtw x2, w2
    mov x0, x24
    uxtw x1, w21
    add x1, x24, x1
    bl mem_copy_a64

    ; Apply MZ segment relocations (+0x1010) across 64 KiB boundaries
.rel_lp:
    cbz w25, .run_vm
    ldrh w0, [x28, #0] ; off
    ldrh w1, [x28, #2] ; seg
    lsl w1, w1, #4
    add w0, w0, w1
    uxtw x0, w0
    add x2, x24, x0
    ldrh w1, [x2, #0]
    mov_imm32 w3, 0x1010
    add w1, w1, w3
    strh w1, [x2, #0]
    add x28, x28, #4
    sub w25, w25, #1
    b .rel_lp

.run_vm:
    ; Pin virtual 8086 CPU state across X19..X28 (X18 TEB strictly untouched!)
    ; X19 = VM86_CTX_A64*, X20 = pMem, X23 = ARM64_CTX*, W24 = stepsLeft,
    ; W25 = pinned IP, W26 = pinned CS, W27 = pinned DS, W28 = pinned FLAGS
    mov_imm32 w24, 50000000
    str w24, [x19, #VM86_CTX_A64.dwStepsLeft]
    mov_imm32 x22, ARM64_CTX.bCtrlBreak

.step_lp:
    ldrb w0, [x19, #VM86_CTX_A64.bHalted]
    cbnz w0, .done_ok
    ldrb w0, [x23, x22]
    cbnz w0, .ctrl_c
    cbz w24, .done_ok
    sub w24, w24, #1
    str w24, [x19, #VM86_CTX_A64.dwStepsLeft]

    ; Mirror hot guest registers in W25..W28 around step dispatch
    ldrh w25, [x19, #VM86_CTX_A64.regIp]
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w26, [x1, #2]
    ldrh w27, [x1, #6]
    ldrh w28, [x19, #VM86_CTX_A64.regFlags]

    mov x0, x19
    bl vm86_step_one_a64
    b .step_lp

.ctrl_c:
    mov w0, #0xFF
    strb w0, [x19, #VM86_CTX_A64.bExitCode]

.done_ok:
    ; Close any guest file handles 5..19 left open
    mov w21, #5
.cl_lp:
    cmp w21, #20
    b.hs .free_ok
    uxtw x1, w21
    lsl x1, x1, #3
    add x2, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x2, x1]
    cmn x0, #1
    b.eq .nx_cl
    bl hal_close_handle_a64
.nx_cl:
    add w21, w21, #1
    b .cl_lp

.free_ok:
    ldrb w21, [x19, #VM86_CTX_A64.bExitCode]
    mov x0, x20
    mov x1, #0
    mov_imm32 w2, 0x8000 ; MEM_RELEASE
    bl hal_virtual_free_a64
    mov w0, w21
    mov w1, #0
    b .exit

.fnf_err:
    mov w21, #ERR_FILE_NOT_FOUND
    b .free_err
.bad_img:
    mov_imm32 w21, ERR_BAD_COMMAND
.free_err:
    mov x0, x20
    mov x1, #0
    mov_imm32 w2, 0x8000
    bl hal_virtual_free_a64
    mov w0, #1
    mov w1, w21
    b .exit

.no_mem:
    mov w0, #1
    mov w1, #ERR_OUT_OF_MEMORY

.exit:
    add sp, sp, #256
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
