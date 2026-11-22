;; @file vm86_a32.inc
;; @isa armv4
;; @desc Embedded ARM32 (ARMv4 & ARMv7-A Thumb-2) vm8086 helpers, ALU, ModR/M, and DOS INT 21h bridge.

@cpu armv4
@format inc

;; @proc vm86_lin_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = 16-bit segment, r2 = 16-bit offset
;; @out r0 = host byte pointer inside [pMem .. pMem + 0x10FFFF]
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Computes 20-bit + HMA linear guest pointer (seg * 16 + off) inside VM86_CTX_A32.pMem.
@proc vm86_lin_a32
    mov_imm32 r3, 0xFFFF
    and r1, r1, r3
    and r2, r2, r3
    lsl r1, r1, #4
    add r1, r1, r2
    mov_imm32 r3, 0x0010FFFC
    cmp r1, r3
    bls .in_hma
    mov_imm32 r3, 0x000FFFFF
    and r1, r1, r3
.in_hma:
    ldr r0, [r0, #VM86_CTX_A32.pMem]
    add r0, r0, r1
    bx lr
@endproc

;; @proc vm86_eff_seg_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = default sreg index (0=ES, 1=CS, 2=SS, 3=DS)
;; @out r0 = effective 16-bit segment value
;; @err none
;; @clobbers r0, r1, r2
;; @desc Resolves active segment override prefix (or default segment register).
@proc vm86_eff_seg_a32
    ldrb r2, [r0, #VM86_CTX_A32.bSegOvr]
    cmp r2, #0xFF
    beq .use_def
    mov r1, r2
.use_def:
    and r1, r1, #3
    lsl r1, r1, #1
    add r0, r0, #VM86_CTX_A32.sregs16
    ldrh r0, [r0, r1]
    bx lr
@endproc

;; @proc vm86_fetch8_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*
;; @out r0 = fetched byte (0..255), advances guest IP by 1
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Fetches one instruction byte at CS:IP.
@proc vm86_fetch8_a32
    push {r4, lr}
    mov r4, r0
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #2] ; CS
    ldrh r2, [r4, #VM86_CTX_A32.regIp]
    add r3, r2, #1
    strh r3, [r4, #VM86_CTX_A32.regIp]
    mov r0, r4
    bl vm86_lin_a32
    ldrb r0, [r0, #0]
    pop {r4, lr}
    bx lr
@endproc

;; @proc vm86_fetch16_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*
;; @out r0 = 16-bit little-endian word at CS:IP, advances IP by 2
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Fetches two bytes at CS:IP safely even when IP is odd (100% ARMv4 alignment-safe).
@proc vm86_fetch16_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    bl vm86_fetch8_a32
    mov r5, r0
    mov r0, r4
    bl vm86_fetch8_a32
    lsl r0, r0, #8
    orr r0, r0, r5
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc vm86_push16_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = 16-bit value to push
;; @out Decrements guest SP by 2 and stores r1 little-endian at SS:SP
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Pushes a 16-bit word onto the virtual 8086 stack using byte-safe unaligned store.
@proc vm86_push16_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #8] ; SP
    sub r2, r2, #2
    mov_imm32 r3, 0xFFFF
    and r2, r2, r3
    add r3, r4, #VM86_CTX_A32.regs16
    strh r2, [r3, #8]
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #4] ; SS
    mov r0, r4
    bl vm86_lin_a32
    mov r1, r5
    bl a32_store_u16_ua
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc vm86_pop16_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*
;; @out r0 = 16-bit word popped from SS:SP, increments guest SP by 2
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Pops a 16-bit word from the virtual 8086 stack using byte-safe unaligned load.
@proc vm86_pop16_a32
    push {r4, lr}
    mov r4, r0
    add r3, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r3, #8] ; SP
    add r1, r2, #2
    strh r1, [r3, #8]
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #4] ; SS
    mov r0, r4
    bl vm86_lin_a32
    bl a32_load_u16_ua
    pop {r4, lr}
    bx lr
@endproc

;; @proc vm86_parity_pf_a32
;; @abi arm_ce
;; @in r0 = 8-bit low byte
;; @out r0 = 0x04 (PF=1 if even parity) or 0x00
;; @err none
;; @clobbers r0, r1
;; @desc Computes the Intel 8086 Parity Flag (PF = bit 2) for the low 8 bits of r0.
@proc vm86_parity_pf_a32
    and r0, r0, #0xFF
    lsr r1, r0, #4
    eor r0, r0, r1
    lsr r1, r0, #2
    eor r0, r0, r1
    lsr r1, r0, #1
    eor r0, r0, r1
    mvn r0, r0
    and r0, r0, #1
    lsl r0, r0, #2
    bx lr
@endproc

;; @proc vm86_set_szp8_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = 8-bit result
;; @out Updates ZF (0x40), SF (0x80), and PF (0x04) in regFlags
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Updates 8086 SZP flags for an 8-bit operation.
@proc vm86_set_szp8_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    and r5, r1, #0xFF
    mov r0, r5
    bl vm86_parity_pf_a32
    ldrh r2, [r4, #VM86_CTX_A32.regFlags]
    mov_imm32 r3, 0xFF3B
    and r2, r2, r3
    orr r2, r2, r0
    cmp r5, #0
    bne .nz8
    orr r2, r2, #0x40
.nz8:
    tst r5, #0x80
    beq .ns8
    orr r2, r2, #0x80
.ns8:
    strh r2, [r4, #VM86_CTX_A32.regFlags]
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc vm86_set_szp16_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = 16-bit result
;; @out Updates ZF (0x40), SF (0x80), and PF (0x04) in regFlags
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Updates 8086 SZP flags for a 16-bit operation.
@proc vm86_set_szp16_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov_imm32 r3, 0xFFFF
    and r5, r1, r3
    mov r0, r5
    bl vm86_parity_pf_a32
    ldrh r2, [r4, #VM86_CTX_A32.regFlags]
    mov_imm32 r3, 0xFF3B
    and r2, r2, r3
    orr r2, r2, r0
    cmp r5, #0
    bne .nz16
    orr r2, r2, #0x40
.nz16:
    mov_imm32 r3, 0x8000
    tst r5, r3
    beq .ns16
    orr r2, r2, #0x80
.ns16:
    strh r2, [r4, #VM86_CTX_A32.regFlags]
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc vm86_alu_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = alu_op (0..7: ADD,OR,ADC,SBB,AND,SUB,XOR,CMP), r2 = dst, r3 = src, [sp+0] = is_word (0/1)
;; @out r0 = masked 8/16-bit result, updates CF/PF/ZF/SF/OF in regFlags
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Pure-RISC exact 8-bit and 16-bit 8086 ALU flag synthesizer (CF, OF, SF, ZF, PF).
@proc vm86_alu_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    ldr r8, [sp, #32] ; is_word (0 or 1)
    mov r4, r0        ; ctx
    mov r5, r1        ; op
    cmp r8, #0
    bne .mask16
    and r6, r2, #0xFF
    and r7, r3, #0xFF
    mov r9, #0x80     ; sign bit mask
    mov r10, #0xFF    ; width mask
    b .do_op
.mask16:
    mov_imm32 r10, 0xFFFF
    and r6, r2, r10
    and r7, r3, r10
    mov_imm32 r9, 0x8000

.do_op:
    ldrh r0, [r4, #VM86_CTX_A32.regFlags]
    and r0, r0, #1 ; old CF

    cmp r5, #0
    beq .op_add
    cmp r5, #1
    beq .op_or
    cmp r5, #2
    beq .op_adc
    cmp r5, #3
    beq .op_sbb
    cmp r5, #4
    beq .op_and
    cmp r5, #5
    beq .op_sub
    cmp r5, #6
    beq .op_xor
    ; op == 7: CMP (identical flags to SUB)
    b .op_sub

.op_add:
    mov r0, #0
.op_adc:
    add r1, r6, r7
    add r1, r1, r0 ; full 32-bit sum
    ; CF = 1 iff r1 > r10
    mov r2, #0
    cmp r1, r10
    bls .add_nc
    mov r2, #1
.add_nc:
    ; Signed overflow OF = (~(dst ^ src) & (dst ^ res)) & sign_mask
    eor r3, r6, r7
    mvn r3, r3
    eor r0, r6, r1
    and r3, r3, r0
    tst r3, r9
    beq .fin_flags
    mov_imm32 r0, 0x0800
    orr r2, r2, r0
    b .fin_flags

.op_sub:
    mov r0, #0
.op_sbb:
    add r3, r7, r0 ; src + cf
    sub r1, r6, r3
    mov r2, #0
    cmp r6, r3
    bcs .sub_nc
    mov r2, #1
.sub_nc:
    ; Signed overflow OF = ((dst ^ src) & (dst ^ res)) & sign_mask
    eor r3, r6, r7
    eor r0, r6, r1
    and r3, r3, r0
    tst r3, r9
    beq .fin_flags
    mov_imm32 r0, 0x0800
    orr r2, r2, r0
    b .fin_flags

.op_or:
    orr r1, r6, r7
    mov r2, #0
    b .fin_flags
.op_and:
    and r1, r6, r7
    mov r2, #0
    b .fin_flags
.op_xor:
    eor r1, r6, r7
    mov r2, #0

.fin_flags:
    and r6, r1, r10 ; masked result
    mov r7, r2      ; CF | OF
    mov r0, r6
    bl vm86_parity_pf_a32
    orr r7, r7, r0
    cmp r6, #0
    bne .no_zf
    orr r7, r7, #0x40
.no_zf:
    tst r6, r9
    beq .no_sf
    orr r7, r7, #0x80
.no_sf:
    ldrh r0, [r4, #VM86_CTX_A32.regFlags]
    mov_imm32 r1, 0x0600 ; preserve DF | IF
    and r0, r0, r1
    orr r0, r0, r7
    orr r0, r0, #2
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    mov r0, r6
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc vm86_reg_ptr_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = reg index (0..7), r2 = is_word (0/1)
;; @out r0 = host pointer to guest 8-bit or 16-bit register in VM86_CTX_A32.regs16
;; @err none
;; @clobbers r0, r1, r2
;; @desc Resolves byte pointer to AL/CL/DL/BL/AH/CH/DH/BH or word pointer to AX..DI.
@proc vm86_reg_ptr_a32
    and r1, r1, #7
    add r0, r0, #VM86_CTX_A32.regs16
    cmp r2, #0
    bne .w16
    cmp r1, #4
    bcs .hi8
    lsl r1, r1, #1
    add r0, r0, r1
    bx lr
.hi8:
    sub r1, r1, #4
    lsl r1, r1, #1
    add r0, r0, r1
    add r0, r0, #1
    bx lr
.w16:
    lsl r1, r1, #1
    add r0, r0, r1
    bx lr
@endproc

;; @proc vm86_decode_modrm_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = modrm byte, r2 = is_word (0/1)
;; @out r0 = host byte pointer to r/m8 or r/m16, r1 = reg field (0..7)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Decodes 16-bit 8086 ModR/M effective address inside VM86_CTX_A32 or guest RAM.
@proc vm86_decode_modrm_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0 ; ctx
    and r5, r1, #0xFF ; modrm
    mov r6, r2 ; is_word

    lsr r0, r5, #6
    cmp r0, #3
    beq .mod_reg

    mov r7, r0 ; mod (0..2)
    and r1, r5, #7 ; r/m
    mov r8, #3 ; default DS
    add r2, r4, #VM86_CTX_A32.regs16

    cmp r1, #0
    beq .rm0
    cmp r1, #1
    beq .rm1
    cmp r1, #2
    beq .rm2
    cmp r1, #3
    beq .rm3
    cmp r1, #4
    beq .rm4
    cmp r1, #5
    beq .rm5
    cmp r1, #6
    beq .rm6
    ldrh r6, [r2, #6] ; [BX]
    b .disp
.rm0:
    ldrh r6, [r2, #6]
    ldrh r3, [r2, #12]
    add r6, r6, r3 ; [BX+SI]
    b .disp
.rm1:
    ldrh r6, [r2, #6]
    ldrh r3, [r2, #14]
    add r6, r6, r3 ; [BX+DI]
    b .disp
.rm2:
    mov r8, #2
    ldrh r6, [r2, #10]
    ldrh r3, [r2, #12]
    add r6, r6, r3 ; [BP+SI]
    b .disp
.rm3:
    mov r8, #2
    ldrh r6, [r2, #10]
    ldrh r3, [r2, #14]
    add r6, r6, r3 ; [BP+DI]
    b .disp
.rm4:
    ldrh r6, [r2, #12] ; [SI]
    b .disp
.rm5:
    ldrh r6, [r2, #14] ; [DI]
    b .disp
.rm6:
    cmp r7, #0
    beq .disp16_only
    mov r8, #2
    ldrh r6, [r2, #10] ; [BP+disp]
    b .disp

.disp16_only:
    mov r0, r4
    bl vm86_fetch16_a32
    mov r6, r0
    b .calc_lin

.disp:
    cmp r7, #1
    beq .disp8
    cmp r7, #2
    beq .disp16
    b .calc_lin
.disp8:
    mov r0, r4
    bl vm86_fetch8_a32
    tst r0, #0x80
    beq .d8_pos
    mov_imm32 r1, 0xFF00
    orr r0, r0, r1
.d8_pos:
    add r6, r6, r0
    b .calc_lin
.disp16:
    mov r0, r4
    bl vm86_fetch16_a32
    add r6, r6, r0

.calc_lin:
    mov r0, r4
    mov r1, r8
    bl vm86_eff_seg_a32
    mov r1, r0
    mov r2, r6
    mov r0, r4
    bl vm86_lin_a32
    b .done

.mod_reg:
    mov r0, r4
    mov r1, r5
    mov r2, r6
    bl vm86_reg_ptr_a32

.done:
    lsr r1, r5, #3
    and r1, r1, #7
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc vm86_dos_file_int21_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*, r1 = AH function code
;; @out Handles file/directory/path DOS INT 21h calls (39h..47h, 56h, 5Eh, 60h)
;; @err Sets guest CF=1 and AX=ERR_* on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Bridges guest DOS handle/directory syscalls onto the host ARM32 HAL.
@proc vm86_dos_file_int21_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r0 ; ctx
    mov r5, r1 ; AH

    cmp r5, #0x39
    beq .mkdir_g
    cmp r5, #0x3A
    beq .rmdir_g
    cmp r5, #0x3C
    beq .creat_h
    cmp r5, #0x3D
    beq .open_h
    cmp r5, #0x3E
    beq .close_h
    cmp r5, #0x3F
    beq .read_h
    cmp r5, #0x40
    beq .write_h
    cmp r5, #0x41
    beq .unlink_g
    cmp r5, #0x42
    beq .lseek_h
    cmp r5, #0x43
    beq .attr_h
    cmp r5, #0x44
    beq .ioctl_h
    cmp r5, #0x47
    beq .getcwd_h
    cmp r5, #0x56
    beq .rename_g
    cmp r5, #0x60
    beq .truename_h
    mov r0, #ERR_INVALID_FUNCTION
    b .dos_err

.ds_dx_u16:
    ; Helper inline via branch? No, let's resolve DS:DX -> norm_path_a32 -> to_u16_longpath_a32
.mkdir_g:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4]
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    bl hal_mkdir
    cmp r0, #0
    beq .err_path
    b .done

.rmdir_g:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4]
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    bl hal_rmdir
    cmp r0, #0
    beq .err_path
    b .done

.unlink_g:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4]
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    bl hal_delete_file
    cmp r0, #0
    beq .err_fnf
    b .done

.rename_g:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4]
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    mov r6, r0
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #0] ; ES
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #14] ; DI
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    mov r1, r0
    mov r0, r6
    bl hal_move_file
    cmp r0, #0
    beq .err_acc
    b .done

.creat_h:
    mov r6, #1 ; write_trunc
    b .do_open_h
.open_h:
    ldrb r0, [r4, #VM86_CTX_A32.regs16] ; AL
    and r0, r0, #3
    mov r6, #0 ; read
    cmp r0, #0
    beq .do_open_h
    mov r6, #3 ; rw_open
.do_open_h:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4]
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    mov r1, r6
    bl hal_open_file
    cmn r0, #1
    beq .err_fnf
    mov r6, r0 ; host handle
    mov r7, #5
.fd_lp:
    cmp r7, #20
    bcs .too_many
    lsl r1, r7, #2
    add r2, r4, #VM86_CTX_A32.hostHandles
    ldr r0, [r2, r1]
    cmn r0, #1
    beq .got_fd
    add r7, r7, #1
    b .fd_lp
.got_fd:
    str r6, [r2, r1]
    strh r7, [r4, #VM86_CTX_A32.regs16]
    b .done
.too_many:
    mov r0, r6
    bl hal_close_handle
    mov r0, #ERR_TOO_MANY_OPEN_FILES
    b .dos_err

.close_h:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrh r6, [r1, #6] ; BX
    cmp r6, #5
    bcc .done
    cmp r6, #20
    bcs .bad_h
    lsl r1, r6, #2
    add r2, r4, #VM86_CTX_A32.hostHandles
    ldr r0, [r2, r1]
    cmn r0, #1
    beq .bad_h
    mvn r3, #0
    str r3, [r2, r1]
    bl hal_close_handle
    b .done

.read_h:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrh r6, [r1, #6] ; BX
    cmp r6, #20
    bcs .bad_h
    lsl r1, r6, #2
    add r2, r4, #VM86_CTX_A32.hostHandles
    ldr r7, [r2, r1]
    cmn r7, #1
    beq .bad_h
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6] ; DS
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4] ; DX
    mov r0, r4
    bl vm86_lin_a32
    mov r1, r0
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #2] ; CX
    mov r0, r7
    bl hal_read_handle
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.write_h:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrh r6, [r1, #6] ; BX
    cmp r6, #20
    bcs .bad_h
    lsl r1, r6, #2
    add r2, r4, #VM86_CTX_A32.hostHandles
    ldr r7, [r2, r1]
    cmn r7, #1
    beq .bad_h
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6] ; DS
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4] ; DX
    mov r0, r4
    bl vm86_lin_a32
    mov r1, r0
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r8, [r2, #2] ; CX
    mov r0, r7
    mov r2, r8
    bl hal_write_handle
    strh r8, [r4, #VM86_CTX_A32.regs16]
    b .done

.lseek_h:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrh r6, [r1, #6] ; BX
    cmp r6, #20
    bcs .bad_h
    lsl r1, r6, #2
    add r2, r4, #VM86_CTX_A32.hostHandles
    ldr r7, [r2, r1]
    cmn r7, #1
    beq .bad_h
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r2, #2] ; CX (high word)
    ldrh r1, [r2, #4] ; DX (low word)
    lsl r0, r0, #16
    orr r1, r1, r0
    asr r2, r1, #31   ; sign-extend 32-bit DOS offset to 64-bit
    ldrb r3, [r4, #VM86_CTX_A32.regs16] ; AL method
    mov r0, r7
    bl hal_seek_file
    cmn r0, #1
    beq .bad_h
    strh r0, [r4, #VM86_CTX_A32.regs16]
    lsr r0, r0, #16
    add r2, r4, #VM86_CTX_A32.regs16
    strh r0, [r2, #4]
    b .done

.attr_h:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4]
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    bl to_u16_longpath_a32
    mov r6, r0
    ldrb r0, [r4, #VM86_CTX_A32.regs16]
    cmp r0, #0
    bne .set_attr_g
    mov r0, r6
    bl hal_get_file_attr
    cmn r0, #1
    beq .err_fnf
    and r0, r0, #0x3F
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #2]
    b .done
.set_attr_g:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrh r1, [r1, #2]
    mov r0, r6
    bl hal_set_file_attr
    cmp r0, #0
    beq .err_acc
    b .done

.ioctl_h:
    ldrb r0, [r4, #VM86_CTX_A32.regs16]
    cmp r0, #0
    bne .done
    mov r0, #0x83
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #4]
    b .done

.getcwd_h:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6] ; DS
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #12] ; SI
    mov r0, r4
    bl vm86_lin_a32
    mov r6, r0
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r1, r0, r1
    ldrb r0, [r1, #1]
    cmp r0, #0x3A
    bne .chk_lead_sl
    add r1, r1, #2
.chk_lead_sl:
    ldrb r0, [r1, #0]
    cmp r0, #0x5C
    bne .cp_cwd
    add r1, r1, #1
.cp_cwd:
    mov r0, r6
    bl str_copy_a32
    b .done

.truename_h:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6] ; DS
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #12] ; SI
    mov r0, r4
    bl vm86_lin_a32
    bl norm_path_a32
    mov r6, r0
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #0] ; ES
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #14] ; DI
    mov r0, r4
    bl vm86_lin_a32
    mov r1, r6
    bl str_copy_a32
    b .done

.err_fnf:
    mov r0, #ERR_FILE_NOT_FOUND
    b .dos_err
.err_path:
    mov r0, #ERR_PATH_NOT_FOUND
    b .dos_err
.err_acc:
    mov r0, #ERR_ACCESS_DENIED
    b .dos_err
.bad_h:
    mov r0, #ERR_INVALID_HANDLE
.dos_err:
    ldrh r1, [r4, #VM86_CTX_A32.regFlags]
    orr r1, r1, #1
    strh r1, [r4, #VM86_CTX_A32.regFlags]
    strh r0, [r4, #VM86_CTX_A32.regs16]
.done:
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc vm86_dos_int21_a32
;; @abi arm_ce
;; @in r0 = VM86_CTX_A32*
;; @out Dispatches DOS INT 21h character, process, memory, time, and VFS syscalls
;; @err Sets guest CF=1 and AX=ERR_* on error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Emulates MS-DOS INT 21h inside ARMv4 & ARMv7 WINCMD executables.
@proc vm86_dos_int21_a32
    push {r4, r5, r6, lr}
    sub sp, sp, #32
    mov r4, r0
    ldrh r1, [r4, #VM86_CTX_A32.regFlags]
    bic r1, r1, #1
    strh r1, [r4, #VM86_CTX_A32.regFlags]

    add r1, r4, #VM86_CTX_A32.regs16
    ldrb r5, [r1, #1] ; AH

    cmp r5, #0x00
    beq .exit_00
    cmp r5, #0x4C
    beq .exit_4c
    cmp r5, #0x02
    beq .ch_out
    cmp r5, #0x06
    beq .raw_io
    cmp r5, #0x09
    beq .prt_dollar
    cmp r5, #0x19
    beq .get_drv
    cmp r5, #0x1A
    beq .set_dta
    cmp r5, #0x25
    beq .done
    cmp r5, #0x2A
    beq .get_date
    cmp r5, #0x2C
    beq .get_time
    cmp r5, #0x2F
    beq .get_dta
    cmp r5, #0x30
    beq .dos_ver
    cmp r5, #0x35
    beq .get_vec
    cmp r5, #0x48
    beq .alloc_mem
    cmp r5, #0x49
    beq .done
    cmp r5, #0x4A
    beq .done
    cmp r5, #0x51
    beq .get_psp
    cmp r5, #0x62
    beq .get_psp

    mov r0, r4
    mov r1, r5
    bl vm86_dos_file_int21_a32
    b .done

.exit_00:
    mov r0, #0
    strb r0, [r4, #VM86_CTX_A32.bExitCode]
    mov r0, #1
    strb r0, [r4, #VM86_CTX_A32.bHalted]
    b .done

.exit_4c:
    ldrb r0, [r4, #VM86_CTX_A32.regs16]
    strb r0, [r4, #VM86_CTX_A32.bExitCode]
    mov r0, #1
    strb r0, [r4, #VM86_CTX_A32.bHalted]
    b .done

.ch_out:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrb r0, [r1, #4] ; DL
    strb r0, [sp, #0]
    mov r0, #0
    strb r0, [sp, #1]
    mov r0, sp
    bl out_str_a32
    b .done

.raw_io:
    add r1, r4, #VM86_CTX_A32.regs16
    ldrb r0, [r1, #4]
    cmp r0, #0xFF
    bne .ch_out
    ldrh r0, [r4, #VM86_CTX_A32.regFlags]
    orr r0, r0, #0x40
    strh r0, [r4, #VM86_CTX_A32.regFlags]
    mov r0, #0
    strb r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.prt_dollar:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r1, [r1, #6]
    add r2, r4, #VM86_CTX_A32.regs16
    ldrh r2, [r2, #4]
    mov r0, r4
    bl vm86_lin_a32
    mov r5, r0
    mov r6, #0
.dlr_lp:
    mov_imm32 r1, 16384
    cmp r6, r1
    bcs .dlr_emit
    ldrb r0, [r5, r6]
    cmp r0, #0x24 ; '$'
    beq .dlr_emit
    add r6, r6, #1
    b .dlr_lp
.dlr_emit:
    add r0, r4, #VM86_CTX_A32.hostHandles
    ldr r0, [r0, #4] ; guest handle 1 (stdout)
    mov r1, r5
    mov r2, r6
    bl hal_write_handle
    b .done

.get_drv:
    bl hal_get_ctx
    mov_imm32 r1, ARM32_CTX.szCwd
    add r1, r0, r1
    ldrb r0, [r1, #0]
    bl ascii_upper_a32
    cmp r0, #0x41
    bcc .def_c
    cmp r0, #0x5A
    bhi .def_c
    sub r0, r0, #0x41
    strb r0, [r4, #VM86_CTX_A32.regs16]
    b .done
.def_c:
    mov r0, #2
    strb r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.set_dta:
    add r1, r4, #VM86_CTX_A32.sregs16
    ldrh r0, [r1, #6]
    strh r0, [r4, #VM86_CTX_A32.wDtaSeg]
    add r1, r4, #VM86_CTX_A32.regs16
    ldrh r0, [r1, #4]
    strh r0, [r4, #VM86_CTX_A32.wDtaOff]
    b .done

.get_dta:
    ldrh r0, [r4, #VM86_CTX_A32.wDtaSeg]
    strh r0, [r4, #VM86_CTX_A32.sregs16]
    ldrh r0, [r4, #VM86_CTX_A32.wDtaOff]
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #6]
    b .done

.dos_ver:
    mov r0, #5
    strh r0, [r4, #VM86_CTX_A32.regs16]
    mov_imm32 r0, 0xFF00
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #6]
    mov r0, #0
    strh r0, [r1, #2]
    b .done

.get_vec:
    mov_imm32 r0, 0xF000
    strh r0, [r4, #VM86_CTX_A32.sregs16]
    mov r0, #0
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #6]
    b .done

.alloc_mem:
    mov_imm32 r0, 0x6000
    strh r0, [r4, #VM86_CTX_A32.regs16]
    b .done

.get_psp:
    ldrh r0, [r4, #VM86_CTX_A32.wPspSeg]
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #6]
    b .done

.get_date:
    mov r0, sp
    bl hal_get_local_time
    ldrh r0, [sp, #0] ; wYear
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #2] ; CX
    ldrh r0, [sp, #2] ; wMonth
    ldrh r2, [sp, #6] ; wDay
    lsl r0, r0, #8
    orr r0, r0, r2
    strh r0, [r1, #4] ; DX
    ldrh r0, [sp, #4] ; wDayOfWeek
    strb r0, [r1, #0] ; AL
    b .done

.get_time:
    mov r0, sp
    bl hal_get_local_time
    ldrh r0, [sp, #8] ; wHour
    ldrh r2, [sp, #10] ; wMinute
    lsl r0, r0, #8
    orr r0, r0, r2
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #2] ; CX
    ldrh r0, [sp, #14] ; wMilliseconds
    mov r1, #10
    bl math_udiv32
    ldrh r2, [sp, #12] ; wSecond
    lsl r2, r2, #8
    orr r0, r0, r2
    add r1, r4, #VM86_CTX_A32.regs16
    strh r0, [r1, #4] ; DX

.done:
    add sp, sp, #32
    pop {r4, r5, r6, lr}
    bx lr
@endproc
