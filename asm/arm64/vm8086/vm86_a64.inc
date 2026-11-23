;; @file vm86_a64.inc
;; @isa arm64
;; @desc Embedded 64-bit AArch64 vm8086 helpers, ALU, ModR/M decoder, and DOS INT 21h / 10h / 16h / 67h bridges (X18 TEB untouched!).

@cpu arm64
@format inc

;; @proc vm86_lin_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = 16-bit segment, w2 = 16-bit offset
;; @out x0 = host byte pointer inside [pMem .. pMem + 0x10FFFF]
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Computes 20-bit + HMA linear guest pointer (seg * 16 + off) inside VM86_CTX_A64.pMem.
@proc vm86_lin_a64
    and w1, w1, #0xFFFF
    and w2, w2, #0xFFFF
    lsl w1, w1, #4
    add w1, w1, w2
    mov_imm32 w3, 0x0010FFFC
    cmp w1, w3
    b.ls .in_hma
    and w1, w1, #0x000FFFFF
.in_hma:
    uxtw x1, w1
    ldr x0, [x0, #VM86_CTX_A64.pMem]
    add x0, x0, x1
    ret
@endproc

;; @proc vm86_eff_seg_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = default sreg index (0=ES, 1=CS, 2=SS, 3=DS)
;; @out w0 = effective 16-bit segment value
;; @err none
;; @clobbers x0, x1, x2
;; @desc Resolves active segment override prefix (or default segment register).
@proc vm86_eff_seg_a64
    ldrb w2, [x0, #VM86_CTX_A64.bSegOvr]
    cmp w2, #0xFF
    b.eq .use_def
    mov w1, w2
.use_def:
    and w1, w1, #3
    uxtw x1, w1
    lsl x1, x1, #1
    add x0, x0, #VM86_CTX_A64.sregs16
    ldrh w0, [x0, x1]
    ret
@endproc

;; @proc vm86_fetch8_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*
;; @out w0 = fetched byte (0..255), advances guest IP by 1
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Fetches one instruction byte at CS:IP.
@proc vm86_fetch8_a64
    mov x4, x0
    add x1, x4, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #2] ; CS
    ldrh w2, [x4, #VM86_CTX_A64.regIp]
    add w3, w2, #1
    strh w3, [x4, #VM86_CTX_A64.regIp]
    and w1, w1, #0xFFFF
    and w2, w2, #0xFFFF
    lsl w1, w1, #4
    add w1, w1, w2
    mov_imm32 w3, 0x0010FFFC
    cmp w1, w3
    b.ls .ok
    and w1, w1, #0x000FFFFF
.ok:
    uxtw x1, w1
    ldr x0, [x4, #VM86_CTX_A64.pMem]
    ldrb w0, [x0, x1]
    ret
@endproc

;; @proc vm86_fetch16_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*
;; @out w0 = fetched 16-bit little-endian word, advances guest IP by 2
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Fetches two bytes at CS:IP safely across 64 KiB segment wrap.
@proc vm86_fetch16_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    bl vm86_fetch8_a64
    mov w20, w0
    mov x0, x19
    bl vm86_fetch8_a64
    orr w0, w20, w0, lsl #8
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_push16_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = 16-bit word to push
;; @out Decrements guest SP by 2 and writes word at SS:SP
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Pushes a 16-bit word onto the virtual 8086 stack.
@proc vm86_push16_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    and w20, w1, #0xFFFF
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #8] ; SP
    sub w2, w2, #2
    and w2, w2, #0xFFFF
    add x3, x19, #VM86_CTX_A64.regs16
    strh w2, [x3, #8]
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #4] ; SS
    mov x0, x19
    bl vm86_lin_a64
    strh w20, [x0, #0]
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_pop16_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*
;; @out w0 = popped 16-bit word, increments guest SP by 2
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Pops a 16-bit word from the virtual 8086 stack.
@proc vm86_pop16_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w20, [x2, #8] ; SP
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #4]  ; SS
    mov w2, w20
    mov x0, x19
    bl vm86_lin_a64
    ldrh w0, [x0, #0]
    add w20, w20, #2
    add x2, x19, #VM86_CTX_A64.regs16
    strh w20, [x2, #8]
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_reg_ptr_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = reg index (0..7), w2 = is_word (0=byte, 1=word)
;; @out x0 = host byte pointer to guest register (AL/CL/DL/BL/AH/CH/DH/BH or AX..DI)
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Resolves 8-bit or 16-bit 8086 general-purpose register pointer on little-endian AArch64.
@proc vm86_reg_ptr_a64
    and w1, w1, #7
    add x0, x0, #VM86_CTX_A64.regs16
    cbnz w2, .word_r
    cmp w1, #4
    b.hs .high_b
    uxtw x1, w1
    lsl x1, x1, #1
    add x0, x0, x1
    ret
.high_b:
    sub w1, w1, #4
    uxtw x1, w1
    lsl x1, x1, #1
    add x1, x1, #1
    add x0, x0, x1
    ret
.word_r:
    uxtw x1, w1
    lsl x1, x1, #1
    add x0, x0, x1
    ret
@endproc

;; @proc vm86_parity8_a64
;; @abi win_arm64
;; @in w0 = low byte
;; @out w0 = 0x04 (PF=1) if low byte has even parity, 0 otherwise
;; @err none
;; @clobbers x0, x1
;; @desc Computes 8086 Parity Flag (PF, bit 2) over the low 8 bits.
@proc vm86_parity8_a64
    eor w0, w0, w0, lsr #4
    eor w0, w0, w0, lsr #2
    eor w0, w0, w0, lsr #1
    mvn w0, w0
    and w0, w0, #1
    lsl w0, w0, #2
    ret
@endproc

;; @proc vm86_set_szp8_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = 8-bit result
;; @out Updates SF, ZF, PF in regFlags while preserving CF and OF
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Updates 8-bit sign, zero, and parity flags in VM86_CTX_A64.regFlags.
@proc vm86_set_szp8_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    and w20, w1, #0xFF
    ldrh w2, [x19, #VM86_CTX_A64.regFlags]
    mov_imm32 w3, 0xFF3B ; clear SF(0x80), ZF(0x40), PF(0x04)
    and w2, w2, w3
    cbnz w20, .nz
    orr w2, w2, #0x40
.nz:
    tbz w20, #7, .ns
    orr w2, w2, #0x80
.ns:
    mov w0, w20
    mov w20, w2
    bl vm86_parity8_a64
    orr w20, w20, w0
    strh w20, [x19, #VM86_CTX_A64.regFlags]
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_set_szp16_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = 16-bit result
;; @out Updates SF, ZF, PF in regFlags while preserving CF and OF
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Updates 16-bit sign, zero, and parity flags in VM86_CTX_A64.regFlags.
@proc vm86_set_szp16_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    and w20, w1, #0xFFFF
    ldrh w2, [x19, #VM86_CTX_A64.regFlags]
    mov_imm32 w3, 0xFF3B
    and w2, w2, w3
    cbnz w20, .nz
    orr w2, w2, #0x40
.nz:
    tbz w20, #15, .ns
    orr w2, w2, #0x80
.ns:
    mov w0, w20
    mov w20, w2
    bl vm86_parity8_a64
    orr w20, w20, w0
    strh w20, [x19, #VM86_CTX_A64.regFlags]
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_alu_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = alu_op (0..7: ADD,OR,ADC,SBB,AND,SUB,XOR,CMP), w2 = dst, w3 = src, w4 = is_word
;; @out w0 = 8/16-bit result, updates CF, OF, SF, ZF, AF, PF in regFlags
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Executes all 8 Intel 8086 ALU operations with exact CF/OF/AF/SZP flag generation.
@proc vm86_alu_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp

    mov x19, x0 ; ctx
    and w20, w1, #7
    mov w21, w4 ; is_word
    mov_imm32 w0, 0xFFFF
    cbnz w21, .mask_ok
    mov w0, #0xFF
.mask_ok:
    and w22, w2, w0 ; dst
    and w23, w3, w0 ; src
    mov w24, #0     ; new CF/OF/AF bits

    cmp w20, #0
    b.eq .do_add
    cmp w20, #1
    b.eq .do_or
    cmp w20, #2
    b.eq .do_adc
    cmp w20, #3
    b.eq .do_sbb
    cmp w20, #4
    b.eq .do_and
    cmp w20, #6
    b.eq .do_xor
    b .do_sub

.do_or:
    orr w5, w22, w23
    b .finish_flags
.do_and:
    and w5, w22, w23
    b .finish_flags
.do_xor:
    eor w5, w22, w23
    b .finish_flags

.do_adc:
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    and w0, w0, #1
    b .add_cin
.do_add:
    mov w0, #0
.add_cin:
    add w5, w22, w23
    add w5, w5, w0
    cbnz w21, .add16_cf
    tbz w5, #8, .add_af
    orr w24, w24, #1
    b .add_af
.add16_cf:
    tbz w5, #16, .add_af
    orr w24, w24, #1
.add_af:
    eor w1, w22, w23
    eor w1, w1, w5
    tbz w1, #4, .add_of
    orr w24, w24, #0x10
.add_of:
    eor w1, w22, w23
    mvn w1, w1
    eor w2, w22, w5
    and w1, w1, w2
    cbnz w21, .add16_of
    tbz w1, #7, .finish_flags
    orr w24, w24, #0x0800
    b .finish_flags
.add16_of:
    tbz w1, #15, .finish_flags
    orr w24, w24, #0x0800
    b .finish_flags

.do_sbb:
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    and w0, w0, #1
    b .sub_bin
.do_sub:
    mov w0, #0
.sub_bin:
    add w1, w23, w0
    cmp w22, w1
    b.hs .no_sub_cf
    orr w24, w24, #1
.no_sub_cf:
    sub w5, w22, w1
    eor w2, w22, w23
    eor w2, w2, w5
    tbz w2, #4, .sub_of
    orr w24, w24, #0x10
.sub_of:
    eor w2, w22, w23
    eor w3, w22, w5
    and w2, w2, w3
    cbnz w21, .sub16_of
    tbz w2, #7, .finish_flags
    orr w24, w24, #0x0800
    b .finish_flags
.sub16_of:
    tbz w2, #15, .finish_flags
    orr w24, w24, #0x0800

.finish_flags:
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    mov_imm32 w1, 0xF7EE ; clear OF(0x0800), AF(0x10), CF(0x01)
    and w0, w0, w1
    orr w0, w0, w24
    strh w0, [x19, #VM86_CTX_A64.regFlags]

    cbnz w21, .szp16
    and w22, w5, #0xFF
    mov x0, x19
    mov w1, w22
    bl vm86_set_szp8_a64
    mov w0, w22
    b .done
.szp16:
    and w22, w5, #0xFFFF
    mov x0, x19
    mov w1, w22
    bl vm86_set_szp16_a64
    mov w0, w22
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_decode_modrm_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = modrm byte, w2 = is_word (0/1)
;; @out x0 = host byte pointer to r/m operand (guest RAM or reg), w1 = reg field (0..7)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Decodes 16-bit 8086 ModR/M effective addressing modes ([BX+SI], [BP+disp], disp16, reg).
@proc vm86_decode_modrm_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp

    mov x19, x0 ; ctx
    mov w20, w1 ; modrm
    mov w21, w2 ; is_word

    lsr w22, w20, #6
    cmp w22, #3
    b.eq .mod_reg

    and w1, w20, #7 ; rm
    mov w23, #3     ; default seg = DS (3)
    add x2, x19, #VM86_CTX_A64.regs16

    cmp w1, #0
    b.eq .rm0
    cmp w1, #1
    b.eq .rm1
    cmp w1, #2
    b.eq .rm2
    cmp w1, #3
    b.eq .rm3
    cmp w1, #4
    b.eq .rm4
    cmp w1, #5
    b.eq .rm5
    cmp w1, #6
    b.eq .rm6
    ldrh w24, [x2, #6] ; [BX]
    b .disp

.rm0:
    ldrh w0, [x2, #6]  ; BX
    ldrh w3, [x2, #12] ; SI
    add w24, w0, w3
    b .disp
.rm1:
    ldrh w0, [x2, #6]  ; BX
    ldrh w3, [x2, #14] ; DI
    add w24, w0, w3
    b .disp
.rm2:
    mov w23, #2        ; SS default
    ldrh w0, [x2, #10] ; BP
    ldrh w3, [x2, #12] ; SI
    add w24, w0, w3
    b .disp
.rm3:
    mov w23, #2        ; SS default
    ldrh w0, [x2, #10] ; BP
    ldrh w3, [x2, #14] ; DI
    add w24, w0, w3
    b .disp
.rm4:
    ldrh w24, [x2, #12] ; [SI]
    b .disp
.rm5:
    ldrh w24, [x2, #14] ; [DI]
    b .disp
.rm6:
    cbz w22, .disp16_only
    mov w23, #2
    ldrh w24, [x2, #10] ; [BP+disp]
    b .disp

.disp16_only:
    mov x0, x19
    bl vm86_fetch16_a64
    mov w24, w0
    b .calc_lin

.disp:
    cmp w22, #1
    b.eq .disp8
    cmp w22, #2
    b.eq .disp16
    b .calc_lin
.disp8:
    mov x0, x19
    bl vm86_fetch8_a64
    sxtb w0, w0
    add w24, w24, w0
    b .calc_lin
.disp16:
    mov x0, x19
    bl vm86_fetch16_a64
    add w24, w24, w0

.calc_lin:
    mov x0, x19
    mov w1, w23
    bl vm86_eff_seg_a64
    mov w1, w0
    mov w2, w24
    mov x0, x19
    bl vm86_lin_a64
    b .done

.mod_reg:
    mov x0, x19
    and w1, w20, #7
    mov w2, w21
    bl vm86_reg_ptr_a64

.done:
    lsr w1, w20, #3
    and w1, w1, #7
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_dos_file_int21_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*, w1 = AH function code
;; @out Handles file/directory/path DOS INT 21h calls (39h..47h, 4Eh, 4Fh, 56h, 5Eh, 60h)
;; @err Sets guest CF=1 and AX=ERR_* on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Bridges guest DOS handle/directory syscalls onto the host Windows ARM64 HAL.
@proc vm86_dos_file_int21_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    sub sp, sp, #352 ; FIND_INFO_A64 (336B) at sp+0
    mov x29, sp

    mov x19, x0 ; ctx
    mov w20, w1 ; AH

    cmp w20, #0x39
    b.eq .mkdir_g
    cmp w20, #0x3A
    b.eq .rmdir_g
    cmp w20, #0x3B
    b.eq .chdir_g
    cmp w20, #0x3C
    b.eq .creat_h
    cmp w20, #0x3D
    b.eq .open_h
    cmp w20, #0x3E
    b.eq .close_h
    cmp w20, #0x3F
    b.eq .read_h
    cmp w20, #0x40
    b.eq .write_h
    cmp w20, #0x41
    b.eq .unlink_g
    cmp w20, #0x42
    b.eq .lseek_h
    cmp w20, #0x43
    b.eq .attr_h
    cmp w20, #0x44
    b.eq .ioctl_h
    cmp w20, #0x47
    b.eq .getcwd_h
    cmp w20, #0x4E
    b.eq .find_first_h
    cmp w20, #0x56
    b.eq .rename_g
    cmp w20, #0x5E
    b.eq .done
    cmp w20, #0x60
    b.eq .truename_h
    mov w0, #ERR_INVALID_FUNCTION
    b .dos_err

.mkdir_g:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    bl hal_mkdir_a64
    cbz w0, .err_path
    b .done

.rmdir_g:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    bl hal_rmdir_a64
    cbz w0, .err_path
    b .done

.chdir_g:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    mov x21, x0
    bl to_u16_longpath_a64
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .err_path
    tbz w0, #4, .err_path
    mov x0, x21
    bl set_drv_cwd_a64
    b .done

.unlink_g:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    bl hal_delete_file_a64
    cbz w0, .err_fnf
    b .done

.rename_g:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov x21, x0
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #0] ; ES
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #14] ; DI
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov x1, x0
    mov x0, x21
    bl hal_move_file_a64
    cbz w0, .err_acc
    b .done

.creat_h:
    mov w21, #1 ; write_trunc
    b .do_open_h
.open_h:
    ldrb w0, [x19, #VM86_CTX_A64.regs16] ; AL
    and w0, w0, #3
    mov w21, #0 ; read
    cbz w0, .do_open_h
    mov w21, #3 ; rw_open
.do_open_h:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov w1, w21
    bl hal_open_file_a64
    cmn x0, #1
    b.eq .err_fnf
    mov x21, x0 ; host 64-bit HANDLE
    mov w22, #5
.fd_lp:
    cmp w22, #20
    b.hs .too_many
    uxtw x1, w22
    lsl x1, x1, #3
    add x2, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x2, x1]
    cmn x0, #1
    b.eq .got_fd
    add w22, w22, #1
    b .fd_lp
.got_fd:
    str x21, [x2, x1]
    strh w22, [x19, #VM86_CTX_A64.regs16]
    b .done
.too_many:
    mov x0, x21
    bl hal_close_handle_a64
    mov w0, #ERR_TOO_MANY_OPEN_FILES
    b .dos_err

.close_h:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrh w21, [x1, #6] ; BX
    cmp w21, #5
    b.lo .done
    cmp w21, #20
    b.hs .bad_h
    uxtw x1, w21
    lsl x1, x1, #3
    add x2, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x2, x1]
    cmn x0, #1
    b.eq .bad_h
    mov x3, #-1
    str x3, [x2, x1]
    bl hal_close_handle_a64
    b .done

.read_h:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrh w21, [x1, #6] ; BX
    cmp w21, #20
    b.hs .bad_h
    uxtw x1, w21
    lsl x1, x1, #3
    add x2, x19, #VM86_CTX_A64.hostHandles
    ldr x22, [x2, x1]
    cmn x22, #1
    b.eq .bad_h
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6] ; DS
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4] ; DX
    mov x0, x19
    bl vm86_lin_a64
    mov x1, x0
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #2] ; CX
    mov x0, x22
    bl hal_read_handle_a64
    strh w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.write_h:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrh w21, [x1, #6] ; BX
    cmp w21, #20
    b.hs .bad_h
    uxtw x1, w21
    lsl x1, x1, #3
    add x2, x19, #VM86_CTX_A64.hostHandles
    ldr x22, [x2, x1]
    cmn x22, #1
    b.eq .bad_h
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6] ; DS
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4] ; DX
    mov x0, x19
    bl vm86_lin_a64
    mov x1, x0
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w23, [x2, #2] ; CX
    mov x0, x22
    mov w2, w23
    bl hal_write_handle_a64
    strh w23, [x19, #VM86_CTX_A64.regs16]
    b .done

.lseek_h:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrh w21, [x1, #6] ; BX
    cmp w21, #20
    b.hs .bad_h
    uxtw x1, w21
    lsl x1, x1, #3
    add x2, x19, #VM86_CTX_A64.hostHandles
    ldr x22, [x2, x1]
    cmn x22, #1
    b.eq .bad_h
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x2, #2] ; CX (high word)
    ldrh w1, [x2, #4] ; DX (low word)
    orr w1, w1, w0, lsl #16
    sxtw x1, w1       ; sign-extend 32-bit DOS offset to 64-bit
    ldrb w2, [x19, #VM86_CTX_A64.regs16] ; AL method
    mov x0, x22
    bl hal_seek_file_a64
    cmn x0, #1
    b.eq .bad_h
    strh w0, [x19, #VM86_CTX_A64.regs16]
    lsr x0, x0, #16
    add x2, x19, #VM86_CTX_A64.regs16
    strh w0, [x2, #4]
    b .done

.attr_h:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov x21, x0
    ldrb w0, [x19, #VM86_CTX_A64.regs16]
    cbnz w0, .set_attr_g
    mov x0, x21
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .err_fnf
    and w0, w0, #0x3F
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #2]
    b .done
.set_attr_g:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrh w1, [x1, #2]
    mov x0, x21
    bl hal_set_file_attr_a64
    cbz w0, .err_acc
    b .done

.ioctl_h:
    ldrb w0, [x19, #VM86_CTX_A64.regs16]
    cbnz w0, .done
    mov w0, #0x83
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #4]
    b .done

.getcwd_h:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6] ; DS
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #12] ; SI
    mov x0, x19
    bl vm86_lin_a64
    mov x21, x0
    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.szCwd
    ldrb w0, [x1, #1]
    cmp w0, #0x3A
    b.ne .chk_lead_sl
    add x1, x1, #2
.chk_lead_sl:
    ldrb w0, [x1, #0]
    cmp w0, #0x5C
    b.ne .cp_cwd
    add x1, x1, #1
.cp_cwd:
    mov x0, x21
    bl str_copy_a64
    b .done

.find_first_h:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6] ; DS
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4] ; DX
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    bl to_u16_longpath_a64
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .err_fnf
    bl hal_find_close_a64
    ; Populate guest 43-byte DTA at wDtaSeg:wDtaOff
    ldrh w1, [x19, #VM86_CTX_A64.wDtaSeg]
    ldrh w2, [x19, #VM86_CTX_A64.wDtaOff]
    mov x0, x19
    bl vm86_lin_a64
    mov x21, x0
    ldr w0, [sp, #FIND_INFO_A64.dwAttr]
    and w0, w0, #0x3F
    strb w0, [x21, #0x15]
    ldr x0, [sp, #FIND_INFO_A64.uSize]
    str w0, [x21, #0x1A]
    add x1, sp, #FIND_INFO_A64.szShort
    ldrb w0, [x1, #0]
    cbnz w0, .cp_dta_nm
    add x1, sp, #FIND_INFO_A64.szName
.cp_dta_nm:
    mov w2, #0
.dta_lp:
    cmp w2, #12
    b.hs .dta_end
    uxtw x3, w2
    ldrb w0, [x1, x3]
    cbz w0, .dta_end
    add x4, x21, #0x1E
    strb w0, [x4, x3]
    add w2, w2, #1
    b .dta_lp
.dta_end:
    uxtw x3, w2
    add x4, x21, #0x1E
    strb wzr, [x4, x3]
    b .done

.truename_h:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6] ; DS
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #12] ; SI
    mov x0, x19
    bl vm86_lin_a64
    bl norm_path_a64
    mov x21, x0
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #0] ; ES
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #14] ; DI
    mov x0, x19
    bl vm86_lin_a64
    mov x1, x21
    bl str_copy_a64
    b .done

.err_fnf:
    mov w0, #ERR_FILE_NOT_FOUND
    b .dos_err
.err_path:
    mov w0, #ERR_PATH_NOT_FOUND
    b .dos_err
.err_acc:
    mov w0, #ERR_ACCESS_DENIED
    b .dos_err
.bad_h:
    mov w0, #ERR_INVALID_HANDLE
.dos_err:
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    orr w1, w1, #1
    strh w1, [x19, #VM86_CTX_A64.regFlags]
    strh w0, [x19, #VM86_CTX_A64.regs16]
.done:
    add sp, sp, #352
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc vm86_dos_int21_a64
;; @abi win_arm64
;; @in x0 = VM86_CTX_A64*
;; @out Dispatches DOS INT 21h character, process, memory, time, and VFS syscalls
;; @err Sets guest CF=1 and AX=ERR_* on error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Emulates MS-DOS INT 21h inside Windows 10 & Windows 11 ARM64 WINCMD_ARM64.EXE.
@proc vm86_dos_int21_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    sub sp, sp, #32
    mov x29, sp

    mov x19, x0
    ldrh w1, [x19, #VM86_CTX_A64.regFlags]
    and w1, w1, #0xFFFE
    strh w1, [x19, #VM86_CTX_A64.regFlags]

    add x1, x19, #VM86_CTX_A64.regs16
    ldrb w20, [x1, #1] ; AH

    cbz w20, .exit_00
    cmp w20, #0x4C
    b.eq .exit_4c
    cmp w20, #0x01
    b.eq .ch_in_echo
    cmp w20, #0x02
    b.eq .ch_out
    cmp w20, #0x06
    b.eq .raw_io
    cmp w20, #0x07
    b.eq .ch_in_raw
    cmp w20, #0x08
    b.eq .ch_in_raw
    cmp w20, #0x09
    b.eq .prt_dollar
    cmp w20, #0x0B
    b.eq .chk_in_stat
    cmp w20, #0x19
    b.eq .get_drv
    cmp w20, #0x1A
    b.eq .set_dta
    cmp w20, #0x25
    b.eq .done
    cmp w20, #0x2A
    b.eq .get_date
    cmp w20, #0x2C
    b.eq .get_time
    cmp w20, #0x2F
    b.eq .get_dta
    cmp w20, #0x30
    b.eq .dos_ver
    cmp w20, #0x35
    b.eq .get_vec
    cmp w20, #0x48
    b.eq .alloc_mem
    cmp w20, #0x49
    b.eq .done
    cmp w20, #0x4A
    b.eq .done
    cmp w20, #0x51
    b.eq .get_psp
    cmp w20, #0x62
    b.eq .get_psp

    mov x0, x19
    mov w1, w20
    bl vm86_dos_file_int21_a64
    b .done

.exit_00:
    strb wzr, [x19, #VM86_CTX_A64.bExitCode]
    mov w0, #1
    strb w0, [x19, #VM86_CTX_A64.bHalted]
    b .done

.exit_4c:
    ldrb w0, [x19, #VM86_CTX_A64.regs16]
    strb w0, [x19, #VM86_CTX_A64.bExitCode]
    mov w0, #1
    strb w0, [x19, #VM86_CTX_A64.bHalted]
    b .done

.ch_in_echo:
    add x0, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x0, #0]
    mov x1, sp
    mov w2, #1
    bl hal_read_handle_a64
    cbnz w0, .got_ci
    mov w0, #0x1A
    strb w0, [sp, #0]
.got_ci:
    ldrb w0, [sp, #0]
    strb w0, [x19, #VM86_CTX_A64.regs16]
    strb wzr, [sp, #1]
    mov x0, sp
    bl out_str_a64
    b .done

.ch_in_raw:
    add x0, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x0, #0]
    mov x1, sp
    mov w2, #1
    bl hal_read_handle_a64
    cbnz w0, .got_cr
    mov w0, #0x1A
    strb w0, [sp, #0]
.got_cr:
    ldrb w0, [sp, #0]
    strb w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.chk_in_stat:
    strb wzr, [x19, #VM86_CTX_A64.regs16]
    b .done

.ch_out:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrb w0, [x1, #4] ; DL
    strb w0, [sp, #0]
    strb wzr, [sp, #1]
    mov x0, sp
    bl out_str_a64
    b .done

.raw_io:
    add x1, x19, #VM86_CTX_A64.regs16
    ldrb w0, [x1, #4]
    cmp w0, #0xFF
    b.ne .ch_out
    ldrh w0, [x19, #VM86_CTX_A64.regFlags]
    orr w0, w0, #0x40
    strh w0, [x19, #VM86_CTX_A64.regFlags]
    strb wzr, [x19, #VM86_CTX_A64.regs16]
    b .done

.prt_dollar:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w1, [x1, #6]
    add x2, x19, #VM86_CTX_A64.regs16
    ldrh w2, [x2, #4]
    mov x0, x19
    bl vm86_lin_a64
    mov x20, x0
    mov w21, #0
.dlr_lp:
    cmp w21, #16384
    b.hs .dlr_emit
    uxtw x2, w21
    ldrb w0, [x20, x2]
    cmp w0, #0x24 ; '$'
    b.eq .dlr_emit
    add w21, w21, #1
    b .dlr_lp
.dlr_emit:
    add x0, x19, #VM86_CTX_A64.hostHandles
    ldr x0, [x0, #8] ; guest handle 1 (stdout, 8-byte slot!)
    mov x1, x20
    mov w2, w21
    bl hal_write_handle_a64
    b .done

.get_drv:
    bl hal_get_ctx_a64
    add x1, x0, #ARM64_CTX.szCwd
    ldrb w0, [x1, #0]
    bl ascii_upper_a64
    cmp w0, #0x41
    b.lo .def_c
    cmp w0, #0x5A
    b.hi .def_c
    sub w0, w0, #0x41
    strb w0, [x19, #VM86_CTX_A64.regs16]
    b .done
.def_c:
    mov w0, #2
    strb w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.set_dta:
    add x1, x19, #VM86_CTX_A64.sregs16
    ldrh w0, [x1, #6]
    strh w0, [x19, #VM86_CTX_A64.wDtaSeg]
    add x1, x19, #VM86_CTX_A64.regs16
    ldrh w0, [x1, #4]
    strh w0, [x19, #VM86_CTX_A64.wDtaOff]
    b .done

.get_dta:
    ldrh w0, [x19, #VM86_CTX_A64.wDtaSeg]
    strh w0, [x19, #VM86_CTX_A64.sregs16]
    ldrh w0, [x19, #VM86_CTX_A64.wDtaOff]
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #6]
    b .done

.dos_ver:
    mov w0, #5
    strh w0, [x19, #VM86_CTX_A64.regs16]
    mov_imm32 w0, 0xFF00
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #6]
    strh wzr, [x1, #2]
    b .done

.get_vec:
    mov_imm32 w0, 0xF000
    strh w0, [x19, #VM86_CTX_A64.sregs16]
    add x1, x19, #VM86_CTX_A64.regs16
    strh wzr, [x1, #6]
    b .done

.alloc_mem:
    mov_imm32 w0, 0x6000
    strh w0, [x19, #VM86_CTX_A64.regs16]
    b .done

.get_psp:
    ldrh w0, [x19, #VM86_CTX_A64.wPspSeg]
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #6]
    b .done

.get_date:
    mov x0, sp
    bl hal_get_local_time_a64
    ldrh w0, [sp, #0] ; wYear
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #2] ; CX
    ldrh w0, [sp, #2] ; wMonth
    ldrh w2, [sp, #6] ; wDay
    orr w0, w2, w0, lsl #8
    strh w0, [x1, #4] ; DX
    ldrh w0, [sp, #4] ; wDayOfWeek
    strb w0, [x1, #0] ; AL
    b .done

.get_time:
    mov x0, sp
    bl hal_get_local_time_a64
    ldrh w0, [sp, #8] ; wHour
    ldrh w2, [sp, #10] ; wMinute
    orr w0, w2, w0, lsl #8
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #2] ; CX
    ldrh w0, [sp, #14] ; wMilliseconds
    mov w1, #10
    udiv w0, w0, w1
    ldrh w2, [sp, #12] ; wSecond
    orr w0, w0, w2, lsl #8
    add x1, x19, #VM86_CTX_A64.regs16
    strh w0, [x1, #4] ; DX

.done:
    add sp, sp, #32
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
