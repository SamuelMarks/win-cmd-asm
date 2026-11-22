;; @file exp_prs.inc
;; @isa armv4
;; @desc ARM32 5-phase batch expander (%*, %0..%9, %~[fdpnxsatz], %VAR:~s,l%, %VAR:a=b%, %%I, !VAR!) and Arena AST parser.

@cpu armv4
@format inc

;; @proc strip_outer_quotes_a32
;; @abi arm_ce
;; @in r0 = NUL-terminated string
;; @out r0 = arena copy with matching outer `"..."` stripped
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Removes surrounding double quotes if present (`%~` modifier semantics).
@proc strip_outer_quotes_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    bl str_len_a32
    mov r5, r0
    cmp r5, #2
    bcc .dup_plain
    ldrb r0, [r4, #0]
    cmp r0, #0x22
    bne .dup_plain
    sub r1, r5, #1
    ldrb r0, [r4, r1]
    cmp r0, #0x22
    bne .dup_plain
    sub r5, r5, #2
    add r0, r5, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r3, r0
    add r1, r4, #1
    mov r2, r5
    push {r3, r4}
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    pop {r3, r4}
    mov r0, r3
    b .done
.dup_plain:
    mov r0, r4
    bl arena_strdup_a32
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc apply_tilde_mods_a32
;; @abi arm_ce
;; @in r0 = raw argument value, r1 = modifier mask (bit0=f,1=d,2=p,3=n,4=x,5=s,6=a,7=t,8=z)
;; @out r0 = formatted WTF-8 result in arena
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Evaluates `%~[fdpnxsatz]` parameter & FOR-variable path/metadata modifiers.
@proc apply_tilde_mods_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #336 ; FIND_INFO_A32 (320B) at sp+0
    mov r5, r1       ; mask
    bl strip_outer_quotes_a32
    mov r4, r0
    cmp r4, #0
    beq .ret_empty
    cmp r5, #0
    beq .ret_r4

    ; Normalize full path for all path/stat modifiers
    mov r0, r4
    bl norm_path_a32
    mov r6, r0 ; full normalized path
    cmp r6, #0
    beq .ret_r4

    mov_imm32 r0, 1024
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_r4
    mov r7, r0 ; outputbase
    mov r8, r0 ; output cursor
    mov r1, #0
    strb r1, [r8, #0]

    ; Query filesystem metadata if a/t/z/s requested (bits 5..8 -> 0x1E0)
    mov_imm32 r0, 0x000001E0
    tst r5, r0
    beq .chk_f

    mov r0, r6
    bl to_u16_longpath_a32
    mov r1, sp
    bl hal_find_first
    cmn r0, #1
    beq .chk_f
    bl hal_find_close

    ; bit 6 (0x40): attributes `--a------` / `d--------`
    tst r5, #0x40
    beq .chk_t
    ldr r2, [sp, #FIND_INFO_A32.dwAttr]
    mov r1, #0x2D
    tst r2, #FILE_ATTRIBUTE_DIRECTORY
    beq .at0
    mov r1, #0x64 ; 'd'
.at0:
    strb r1, [r8, #0]
    mov r1, #0x2D
    tst r2, #FILE_ATTRIBUTE_READONLY
    beq .at1
    mov r1, #0x72 ; 'r'
.at1:
    strb r1, [r8, #1]
    mov r1, #0x2D
    tst r2, #FILE_ATTRIBUTE_ARCHIVE
    beq .at2
    mov r1, #0x61 ; 'a'
.at2:
    strb r1, [r8, #2]
    mov r1, #0x2D
    tst r2, #FILE_ATTRIBUTE_HIDDEN
    beq .at3
    mov r1, #0x68 ; 'h'
.at3:
    strb r1, [r8, #3]
    mov r1, #0x2D
    tst r2, #FILE_ATTRIBUTE_SYSTEM
    beq .at4
    mov r1, #0x73 ; 's'
.at4:
    strb r1, [r8, #4]
    mov r1, #0x2D
    strb r1, [r8, #5]
    strb r1, [r8, #6]
    strb r1, [r8, #7]
    strb r1, [r8, #8]
    mov r1, #0x20
    strb r1, [r8, #9]
    add r8, r8, #10

.chk_t:
    tst r5, #0x80
    beq .chk_z
    ldr r0, [sp, #FIND_INFO_A32.ftWriteLo]
    ldr r1, [sp, #FIND_INFO_A32.ftWriteHi]
    mov r2, r8
    bl fmt_filetime_a32
    mov r8, r0
    mov r1, #0x20
    strb r1, [r8, #0]
    add r8, r8, #1

.chk_z:
    mov_imm32 r0, 0x00000100
    tst r5, r0
    beq .chk_f
    ldr r0, [sp, #FIND_INFO_A32.uSizeLo]
    ldr r1, [sp, #FIND_INFO_A32.uSizeHi]
    mov r2, r8
    mov r3, #0 ; plain decimal
    bl fmt_u64_commas_a32
    add r8, r8, r0
    mov r1, #0x20
    strb r1, [r8, #0]
    add r8, r8, #1

.chk_f:
    ; If `f` (bit 0) or `s` (bit 5 without d/p/n/x), append full path!
    tst r5, #0x01
    bne .emit_full
    and r0, r5, #0x1E
    cmp r0, #0
    bne .split_dpnx
    tst r5, #0x20
    bne .emit_short_or_full
    b .trim_sp_end

.emit_short_or_full:
    ldrb r0, [sp, #FIND_INFO_A32.szShort]
    cmp r0, #0
    beq .emit_full
    ; Replace leaf of r6 with szShort
    mov r0, r8
    mov r1, r6
    bl str_copy_a32
    mov r1, r0
.bk_sl:
    cmp r1, r8
    bls .cp_sh
    sub r1, r1, #1
    ldrb r2, [r1, #0]
    cmp r2, #0x5C
    bne .bk_sl
    add r1, r1, #1
.cp_sh:
    mov r0, r1
    add r1, sp, #FIND_INFO_A32.szShort
    bl str_copy_a32
    mov r8, r0
    b .finish_mod

.emit_full:
    mov r0, r8
    mov r1, r6
    bl str_copy_a32
    mov r8, r0
    b .finish_mod

.split_dpnx:
    ; Locate drive end (2 if X:, else 0), last backslash `r9`, and last dot after last backslash `r10`
    mov r9, r6  ; last_sep
    mov r1, r6
    ldrb r0, [r6, #1]
    cmp r0, #0x3A
    bne .sc_sl
    add r1, r6, #2
    mov r9, r1
.sc_sl:
    ldrb r0, [r1, #0]
    cmp r0, #0
    beq .sc_dot_init
    cmp r0, #0x5C
    bne .sc_sl_nx
    add r9, r1, #1 ; first char of filename!
.sc_sl_nx:
    add r1, r1, #1
    b .sc_sl

.sc_dot_init:
    mov r10, r1 ; end of string (default if no extension)
    mov r2, r9
.sc_dot:
    ldrb r0, [r2, #0]
    cmp r0, #0
    beq .emit_parts
    cmp r0, #0x2E
    bne .sc_dot_nx
    cmp r2, r9 ; leading dot `.gitignore`? cmd treats `.ext` as extension
    mov r10, r2
.sc_dot_nx:
    add r2, r2, #1
    b .sc_dot

.emit_parts:
    ; d (0x02): drive `X:`
    tst r5, #0x02
    beq .part_p
    ldrb r0, [r6, #1]
    cmp r0, #0x3A
    bne .part_p
    ldrb r0, [r6, #0]
    strb r0, [r8, #0]
    mov r0, #0x3A
    strb r0, [r8, #1]
    add r8, r8, #2
.part_p:
    ; p (0x04): path from after drive up to r9
    tst r5, #0x04
    beq .part_n
    mov r1, r6
    ldrb r0, [r6, #1]
    cmp r0, #0x3A
    bne .p_st
    add r1, r6, #2
.p_st:
    subs r2, r9, r1
    bls .part_n
    mov r0, r8
    bl mem_copy_a32
    mov r8, r0
.part_n:
    ; n (0x08): stem from r9 up to r10
    tst r5, #0x08
    beq .part_x
    subs r2, r10, r9
    bls .part_x
    mov r0, r8
    mov r1, r9
    bl mem_copy_a32
    mov r8, r0
.part_x:
    ; x (0x10): extension from r10 to NUL
    tst r5, #0x10
    beq .finish_mod
    mov r0, r8
    mov r1, r10
    bl str_copy_a32
    mov r8, r0
    b .finish_mod

.trim_sp_end:
    cmp r8, r7
    bls .finish_mod
    sub r1, r8, #1
    ldrb r0, [r1, #0]
    cmp r0, #0x20
    bne .finish_mod
    mov r8, r1

.finish_mod:
    mov r0, #0
    strb r0, [r8, #0]
    mov r0, r7
    add sp, sp, #336
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ret_r4:
    mov r0, r4
    add sp, sp, #336
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ret_empty:
    mov r0, #4
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_null
    mov r1, #0
    strb r1, [r0, #0]
.ret_null:
    add sp, sp, #336
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc parse_signed_dec_a32
;; @abi arm_ce
;; @in r0 = string cursor
;; @out r0 = parsed i32, r1 = advanced cursor
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Parses an optional `-`/`+` signed decimal integer for `%VAR:~start,len%`.
@proc parse_signed_dec_a32
    mov r1, r0
    mov r2, #0 ; neg flag
    ldrb r0, [r1, #0]
    cmp r0, #0x2D ; '-'
    bne .chk_plus
    mov r2, #1
    add r1, r1, #1
    b .digits
.chk_plus:
    cmp r0, #0x2B ; '+'
    bne .digits
    add r1, r1, #1
.digits:
    mov r0, #0
.d_lp:
    ldrb r3, [r1, #0]
    cmp r3, #0x30
    bcc .d_done
    cmp r3, #0x39
    bhi .d_done
    sub r3, r3, #0x30
    push {r2, r3}
    mov r2, #10
    mul r3, r0, r2
    mov r0, r3
    pop {r2, r3}
    add r0, r0, r3
    add r1, r1, #1
    b .d_lp
.d_done:
    cmp r2, #0
    beq .ret_d
    mov r3, #0
    sub r0, r3, r0
.ret_d:
    bx lr
@endproc

;; @proc expand_var_op_a32
;; @abi arm_ce
;; @in r0 = inner token (between `%..%` or `!..!`, e.g. `VAR`, `VAR:~0,3`, `VAR:old=new`, `VAR:*old=new`)
;; @out r0 = expanded string in arena (or 0 if undefined and no `:` operator)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Evaluates variable lookup plus `:~start[,len]` slicing and `:old=new` / `:*old=new` substitution.
@proc expand_var_op_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0
    ; Scan for `:` separator
    mov r5, #0
.find_col:
    ldrb r0, [r4, r5]
    cmp r0, #0
    beq .plain_lookup
    cmp r0, #0x3A
    beq .has_col
    add r5, r5, #1
    b .find_col

.plain_lookup:
    mov r0, r4
    bl env_get_a32
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.has_col:
    ; Extract var name r4[0..r5]
    add r0, r5, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_z
    mov r6, r0
    mov r1, r4
    mov r2, r5
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r6
    bl env_get_a32
    cmp r0, #0
    beq .ret_z
    mov r6, r0 ; value string
    bl str_len_a32
    mov r7, r0 ; vlen

    add r8, r4, r5
    add r8, r8, #1 ; spec after ':'
    ldrb r0, [r8, #0]
    cmp r0, #0x7E ; '~' substring slice!
    beq .do_slice

    ; Search & replace `:old=new` or `:*old=new`
    mov r9, #0 ; star_prefix = 0
    cmp r0, #0x2A ; '*'
    bne .find_eq
    mov r9, #1
    add r8, r8, #1
.find_eq:
    mov r5, #0
.eq_lp:
    ldrb r0, [r8, r5]
    cmp r0, #0
    beq .ret_val
    cmp r0, #0x3D ; '='
    beq .got_eq
    add r5, r5, #1
    b .eq_lp
.got_eq:
    cmp r5, #0
    beq .ret_val
    add r10, r8, r5
    add r10, r10, #1 ; new_str

    ; Allocate generous output buffer (vlen * 4 + 256)
    lsl r0, r7, #2
    mov_imm32 r1, 512
    add r0, r0, r1
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_val
    mov r4, r0 ; out base
    mov r7, r0 ; out cursor

    cmp r9, #0
    beq .rep_loop
    ; `:*old=new`: find first match of r8[0..r5] in r6, replace prefix..match with r10!
    mov r9, r6
.st_lp:
    ldrb r0, [r9, #0]
    cmp r0, #0
    beq .ret_val
    mov r0, r9
    mov r1, r8
    mov r2, r5
    bl str_ncmp_ci_a32
    cmp r0, #0
    beq .st_hit
    add r9, r9, #1
    b .st_lp
.st_hit:
    mov r0, r4
    mov r1, r10
    bl str_copy_a32
    add r1, r9, r5
    bl str_copy_a32
    mov r0, r4
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.rep_loop:
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .rep_fin
    mov r0, r6
    mov r1, r8
    mov r2, r5
    bl str_ncmp_ci_a32
    cmp r0, #0
    beq .rep_hit
    ldrb r0, [r6, #0]
    strb r0, [r7, #0]
    add r6, r6, #1
    add r7, r7, #1
    b .rep_loop
.rep_hit:
    mov r0, r7
    mov r1, r10
    bl str_copy_a32
    mov r7, r0
    add r6, r6, r5
    b .rep_loop
.rep_fin:
    mov r0, #0
    strb r0, [r7, #0]
    mov r0, r4
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.do_slice:
    add r0, r8, #1
    bl parse_signed_dec_a32
    mov r8, r0 ; start
    mov r9, r7 ; default end = vlen
    ldrb r0, [r1, #0]
    cmp r0, #0x2C ; ','
    bne .norm_bounds
    add r0, r1, #1
    bl parse_signed_dec_a32
    mov r10, r0 ; len
    cmp r8, #0
    bge .st_nonneg1
    adds r8, r7, r8
    bge .st_nonneg1
    mov r8, #0
.st_nonneg1:
    cmp r8, r7
    ble .chk_len_sign
    mov r8, r7
.chk_len_sign:
    cmp r10, #0
    bge .pos_len
    adds r9, r7, r10
    b .clamp_end
.pos_len:
    add r9, r8, r10
    b .clamp_end

.norm_bounds:
    cmp r8, #0
    bge .clamp_end
    adds r8, r7, r8
    bge .clamp_end
    mov r8, #0

.clamp_end:
    cmp r8, r7
    ble .chk_end_hi
    mov r8, r7
.chk_end_hi:
    cmp r9, r7
    ble .chk_end_lo
    mov r9, r7
.chk_end_lo:
    cmp r9, r8
    bge .alloc_sl
    mov r9, r8
.alloc_sl:
    sub r5, r9, r8
    add r0, r5, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_z
    mov r4, r0
    add r1, r6, r8
    mov r2, r5
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r4
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ret_val:
    mov r0, r6
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ret_z:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc parse_tilde_mask_a32
;; @abi arm_ce
;; @in r0 = pointer right after `~`
;; @out r0 = modifier bitmask, r1 = pointer to parameter/var char (or 0 if invalid)
;; @err r1 = 0 if no valid parameter/FOR char follows
;; @clobbers r0, r1, r2, r3
;; @desc Parses `[fdpnxsatz]*` flags after `%~` or `%%~`.
@proc parse_tilde_mask_a32
    mov r1, r0
    mov r0, #0 ; mask
.m_lp:
    ldrb r2, [r1, #0]
    cmp r2, #0
    beq .bad
    ; Convert r2 lowercase
    mov r3, r2
    cmp r3, #0x41
    bcc .chk_dig
    cmp r3, #0x5A
    bhi .chk_flag
    add r3, r3, #0x20
.chk_flag:
    cmp r3, #0x66 ; 'f'
    beq .f_f
    cmp r3, #0x64 ; 'd'
    beq .f_d
    cmp r3, #0x70 ; 'p'
    beq .f_p
    cmp r3, #0x6E ; 'n'
    beq .f_n
    cmp r3, #0x78 ; 'x'
    beq .f_x
    cmp r3, #0x73 ; 's'
    beq .f_s
    cmp r3, #0x61 ; 'a'
    beq .f_a
    cmp r3, #0x74 ; 't'
    beq .f_t
    cmp r3, #0x7A ; 'z'
    beq .f_z
.chk_dig:
    bx lr
.f_f:
    ; Peek next: if next is end/space, is 'f' the FOR variable `%%~f`?!
    ; Only treat as modifier if another valid letter/digit follows!
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x01
    add r1, r1, #1
    b .m_lp
.f_d:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x02
    add r1, r1, #1
    b .m_lp
.f_p:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x04
    add r1, r1, #1
    b .m_lp
.f_n:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x08
    add r1, r1, #1
    b .m_lp
.f_x:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x10
    add r1, r1, #1
    b .m_lp
.f_s:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x20
    add r1, r1, #1
    b .m_lp
.f_a:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x40
    add r1, r1, #1
    b .m_lp
.f_t:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x80
    add r1, r1, #1
    b .m_lp
.f_z:
    ldrb r3, [r1, #1]
    cmp r3, #0x21
    bcc .done_m
    orr r0, r0, #0x100
    add r1, r1, #1
    b .m_lp
.done_m:
    bx lr
.bad:
    mov r1, #0
    bx lr
@endproc

;; @proc get_batch_arg_a32
;; @abi arm_ce
;; @in r0 = digit 0..9
;; @out r0 = argument string (or empty string)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Looks up `%0`..`%9` on the active top BATCH_FRAME_A32 honoring SHIFT [/n].
@proc get_batch_arg_a32
    push {r4, lr}
    mov r4, r0
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .empty
    sub r1, r1, #1
    lsl r1, r1, #6 ; * 64 (BATCH_FRAME_A32_SIZE)
    ldr r2, [r0, #ARM32_CTX.pBatchStack]
    add r2, r2, r1 ; active frame
    ldr r3, [r2, #BATCH_FRAME_A32.dwShiftBase]
    add r4, r4, r3
    ldr r3, [r2, #BATCH_FRAME_A32.dwArgCount]
    cmp r4, r3
    bhs .empty
    ldr r2, [r2, #BATCH_FRAME_A32.pArgVec]
    lsl r4, r4, #2
    ldr r0, [r2, r4]
    pop {r4, lr}
    bx lr
.empty:
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc expand_phase1_a32
;; @abi arm_ce
;; @in r0 = raw logical line, r1 = in_batch (1 = batch file, 0 = interactive/cmdline)
;; @out r0 = Phase-1 `%`-expanded WTF-8 line in arena
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Performs Phase 1 immediate `%` parameter (`%0`..`%9`, `%*`, `%~`) and `%VAR%` expansion.
@proc expand_phase1_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; src
    mov r5, r1 ; in_batch

    mov_imm32 r0, A32_LINE_MAX
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r6, r0 ; out base
    mov r7, r0 ; out cursor

.lp:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .fin
    cmp r0, #0x25 ; '%'
    bne .lit

    ldrb r1, [r4, #1]
    cmp r1, #0
    beq .lit

    ; `%%`: in batch -> emit `%` (unless followed by FOR var/tilde, wait: `%%I` in batch becomes `%I` for Phase 3!! YES! In cmd.exe Phase 1, `%%` ALWAYS collapses to a single `%`, which turns `%%I` into `%I` and `%%~fI` into `%~fI` for Phase 3!!).
    cmp r1, #0x25
    bne .chk_batch_only
    cmp r5, #0
    beq .emit_two_pct
    mov r0, #0x25
    strb r0, [r7, #0]
    add r7, r7, #1
    add r4, r4, #2
    b .lp
.emit_two_pct:
    mov r0, #0x25
    strb r0, [r7, #0]
    strb r0, [r7, #1]
    add r7, r7, #2
    add r4, r4, #2
    b .lp

.chk_batch_only:
    cmp r5, #0
    beq .try_env_var

    ; `%*` in batch -> raw argument tail
    cmp r1, #0x2A
    bne .chk_tilde_p1
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .skip2
    sub r1, r1, #1
    lsl r1, r1, #6
    ldr r2, [r0, #ARM32_CTX.pBatchStack]
    add r2, r2, r1
    ldr r1, [r2, #BATCH_FRAME_A32.pRawArgs]
    cmp r1, #0
    beq .skip2
    mov r0, r7
    bl str_copy_a32
    mov r7, r0
.skip2:
    add r4, r4, #2
    b .lp

.chk_tilde_p1:
    cmp r1, #0x7E ; '~'
    bne .chk_dig_p1
    add r0, r4, #2
    bl parse_tilde_mask_a32
    cmp r1, #0
    beq .try_env_var
    ldrb r2, [r1, #0]
    cmp r2, #0x30
    bcc .try_env_var
    cmp r2, #0x39
    bhi .try_env_var
    mov r8, r0 ; mask
    add r9, r1, #1 ; next src
    sub r0, r2, #0x30
    bl get_batch_arg_a32
    cmp r0, #0
    beq .adv_r9
    mov r1, r8
    bl apply_tilde_mods_a32
    cmp r0, #0
    beq .adv_r9
    mov r1, r0
    mov r0, r7
    bl str_copy_a32
    mov r7, r0
.adv_r9:
    mov r4, r9
    b .lp

.chk_dig_p1:
    cmp r1, #0x30
    bcc .try_env_var
    cmp r1, #0x39
    bhi .try_env_var
    sub r0, r1, #0x30
    bl get_batch_arg_a32
    cmp r0, #0
    beq .skip2
    mov r1, r0
    mov r0, r7
    bl str_copy_a32
    mov r7, r0
    add r4, r4, #2
    b .lp

.try_env_var:
    ; Scan for closing `%` on same line
    add r8, r4, #1
    mov r9, #0
.sc_pct:
    ldrb r0, [r8, r9]
    cmp r0, #0
    beq .no_close_pct
    cmp r0, #0x25
    beq .got_close_pct
    add r9, r9, #1
    b .sc_pct

.no_close_pct:
    cmp r5, #0
    bne .drop_lone_pct
    b .lit
.drop_lone_pct:
    add r4, r4, #1
    b .lp

.got_close_pct:
    cmp r9, #0
    beq .lit
    add r0, r9, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .lit
    mov r10, r0
    mov r1, r8
    mov r2, r9
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r10
    bl expand_var_op_a32
    cmp r0, #0
    bne .emit_var_val
    ; Undefined `%VAR%`: in batch -> empty; in cmdline -> keep literal `%VAR%`!
    cmp r5, #0
    beq .lit
    add r4, r8, r9
    add r4, r4, #1
    b .lp

.emit_var_val:
    mov r1, r0
    mov r0, r7
    bl str_copy_a32
    mov r7, r0
    add r4, r8, r9
    add r4, r4, #1
    b .lp

.lit:
    ldrb r0, [r4, #0]
    strb r0, [r7, #0]
    add r7, r7, #1
    add r4, r4, #1
    b .lp

.fin:
    mov r0, #0
    strb r0, [r7, #0]
    mov r0, r6
.done:
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc lookup_for_var_a32
;; @abi arm_ce
;; @in r0 = variable character (e.g. 'I')
;; @out r0 = bound value pointer or 0 if not an active FOR variable
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Searches ARM32_CTX.pForStack from innermost to outermost scope for `%V`.
@proc lookup_for_var_a32
    push {r4, lr}
    mov r4, r0
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwForDepth]
    ldr r2, [r0, #ARM32_CTX.pForStack]
.f_lp:
    cmp r1, #0
    beq .nf
    sub r1, r1, #1
    lsl r3, r1, #3 ; * 8 (FOR_VAR_A32_SIZE)
    add r3, r2, r3
    ldrb r0, [r3, #FOR_VAR_A32.bActive]
    cmp r0, #0
    beq .f_lp
    ldrb r0, [r3, #FOR_VAR_A32.bVarChar]
    cmp r0, r4
    bne .f_lp
    ldr r0, [r3, #FOR_VAR_A32.pValue]
    pop {r4, lr}
    bx lr
.nf:
    mov r0, #0
    pop {r4, lr}
    bx lr
@endproc

;; @proc expand_phase34_a32
;; @abi arm_ce
;; @in r0 = token/argument string, r1 = strip_quotes_and_carets (1 = unquote/unescape, 0 = raw)
;; @out r0 = Phase-3 (`%I`/`%~fI`) + Phase-4 (`!VAR!` delayed + caret) expanded string in arena
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Substitutes active FOR variables, expands delayed `!VAR!` when enabled, and strips carets/quotes.
@proc expand_phase34_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #8
    mov r4, r0
    str r1, [sp, #0]

    ; Pass A: Phase 3 FOR variable substitution (`%I` and `%~[mods]I`)
    mov_imm32 r0, A32_LINE_MAX
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r5, r0 ; passA base
    mov r6, r0 ; passA cursor

.p3_lp:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .p3_done
    cmp r0, #0x25 ; '%'
    bne .p3_lit
    ldrb r1, [r4, #1]
    cmp r1, #0
    beq .p3_lit
    cmp r1, #0x7E ; '~'
    bne .p3_direct
    add r0, r4, #2
    bl parse_tilde_mask_a32
    cmp r1, #0
    beq .p3_direct
    ldrb r0, [r1, #0]
    mov r8, r0 ; var char
    mov r9, r1 ; ptr to var char
    mov r10, r0
    ; Save mask on stack
    str r0, [sp, #4]
    ; Re-parse mask cleanly
    add r0, r4, #2
    bl parse_tilde_mask_a32
    mov r8, r0 ; mask
    ldrb r0, [r9, #0]
    bl lookup_for_var_a32
    cmp r0, #0
    beq .p3_direct
    mov r1, r8
    bl apply_tilde_mods_a32
    mov r1, r0
    mov r0, r6
    bl str_copy_a32
    mov r6, r0
    add r4, r9, #1
    b .p3_lp

.p3_direct:
    ldrb r0, [r4, #1]
    bl lookup_for_var_a32
    cmp r0, #0
    beq .p3_lit
    mov r1, r0
    mov r0, r6
    bl str_copy_a32
    mov r6, r0
    add r4, r4, #2
    b .p3_lp

.p3_lit:
    ldrb r0, [r4, #0]
    strb r0, [r6, #0]
    add r6, r6, #1
    add r4, r4, #1
    b .p3_lp

.p3_done:
    mov r0, #0
    strb r0, [r6, #0]

    ; Pass B: Phase 4 `!VAR!` delayed expansion (if bDelayedExp != 0) + caret/quote handling
    bl hal_get_ctx
    ldrb r8, [r0, #ARM32_CTX.bDelayedExp]

    mov_imm32 r0, A32_LINE_MAX
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r6, r0 ; passB base
    mov r7, r0 ; passB cursor
    mov r4, r5 ; read from passA
    mov r9, #0 ; in_quotes

.p4_lp:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .p4_done

    cmp r0, #0x22 ; '"'
    bne .chk_caret4
    eor r9, r9, #1
    ldr r1, [sp, #0]
    cmp r1, #0
    bne .skip_q4
    strb r0, [r7, #0]
    add r7, r7, #1
.skip_q4:
    add r4, r4, #1
    b .p4_lp

.chk_caret4:
    cmp r0, #0x5E ; '^'
    bne .chk_bang4
    cmp r9, #0
    bne .chk_caret_in_q
    ; Unquoted `^c` -> literal `c`!
    ldrb r1, [r4, #1]
    cmp r1, #0
    beq .adv4_1
    strb r1, [r7, #0]
    add r7, r7, #1
    add r4, r4, #2
    b .p4_lp
.chk_caret_in_q:
    ; Inside quotes, `^!` escapes `!` when delayed expansion is active!
    cmp r8, #0
    beq .p4_lit
    ldrb r1, [r4, #1]
    cmp r1, #0x21
    bne .p4_lit
    strb r1, [r7, #0]
    add r7, r7, #1
    add r4, r4, #2
    b .p4_lp

.chk_bang4:
    cmp r8, #0
    beq .p4_lit
    cmp r0, #0x21 ; '!'
    bne .p4_lit
    ; Scan for closing `!`
    add r1, r4, #1
    mov r2, #0
.sc_bg:
    ldrb r3, [r1, r2]
    cmp r3, #0
    beq .adv4_1
    cmp r3, #0x21
    beq .got_bg
    add r2, r2, #1
    b .sc_bg
.got_bg:
    cmp r2, #0
    beq .adv4_1
    push {r1, r2}
    add r0, r2, #2
    bl arena_alloc_a32
    mov r10, r0
    pop {r1, r2}
    cmp r10, #0
    beq .p4_lit
    push {r1, r2}
    mov r0, r10
    bl mem_copy_a32
    mov r3, #0
    strb r3, [r0, #0]
    mov r0, r10
    bl expand_var_op_a32
    pop {r1, r2}
    add r4, r1, r2
    add r4, r4, #1
    cmp r0, #0
    beq .p4_lp
    mov r1, r0
    mov r0, r7
    bl str_copy_a32
    mov r7, r0
    b .p4_lp

.adv4_1:
    add r4, r4, #1
    b .p4_lp

.p4_lit:
    ldrb r0, [r4, #0]
    strb r0, [r7, #0]
    add r7, r7, #1
    add r4, r4, #1
    b .p4_lp

.p4_done:
    mov r0, #0
    strb r0, [r7, #0]
    mov r0, r6
.done:
    add sp, sp, #8
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc alloc_ast_a32
;; @abi arm_ce
;; @in r0 = AST_* kind byte
;; @out r0 = zeroed AST_NODE_A32*
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Allocates and zeroes a 32-byte AST_NODE_A32 node with bKind = r0.
@proc alloc_ast_a32
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r0, #AST_NODE_A32_SIZE
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r5, r0
    mov r1, #AST_NODE_A32_SIZE
    bl mem_zero_a32
    strb r4, [r5, #AST_NODE_A32.bKind]
    mov r0, r5
.done:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc skip_ws_a32
;; @abi arm_ce
;; @in r0 = string cursor
;; @out r0 = cursor advanced past spaces, tabs, `,`, `;`, `=`? Wait: spaces and tabs only (`0x20`, `0x09`)!
;; @err none
;; @clobbers r0, r1
;; @desc Advances r0 past ASCII space and tab characters.
@proc skip_ws_a32
.lp:
    ldrb r1, [r0, #0]
    cmp r1, #0x20
    beq .adv
    cmp r1, #0x09
    beq .adv
    bx lr
.adv:
    add r0, r0, #1
    b .lp
@endproc

;; @proc scan_cmd_token_a32
;; @abi arm_ce
;; @in r0 = cursor
;; @out r0 = arena token string, r1 = advanced cursor
;; @err r0 = 0 on end/delimiter
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Scans one command/argument token respecting `"..."` quotes and `^` escapes, stopping at unquoted whitespace or `&|<>)`.
@proc scan_cmd_token_a32
    push {r4, r5, r6, lr}
    bl skip_ws_a32
    mov r4, r0
    mov r5, r0
    mov r6, #0 ; in_quote
.sc:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .end_sc
    cmp r0, #0x22
    bne .chk_car
    eor r6, r6, #1
    add r5, r5, #1
    b .sc
.chk_car:
    cmp r6, #0
    bne .adv_sc
    cmp r0, #0x5E ; '^'
    bne .chk_meta
    ldrb r1, [r5, #1]
    cmp r1, #0
    beq .adv_sc
    add r5, r5, #2
    b .sc
.chk_meta:
    cmp r0, #0x20
    beq .end_sc
    cmp r0, #0x09
    beq .end_sc
    cmp r0, #0x26 ; '&'
    beq .end_sc
    cmp r0, #0x7C ; '|'
    beq .end_sc
    cmp r0, #0x3C ; '<'
    beq .end_sc
    cmp r0, #0x3E ; '>'
    beq .end_sc
    cmp r0, #0x29 ; ')'
    beq .end_sc
.adv_sc:
    add r5, r5, #1
    b .sc

.end_sc:
    subs r6, r5, r4
    beq .none
    add r0, r6, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .none
    mov r3, r0
    mov r1, r4
    mov r2, r6
    push {r3, r4}
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    pop {r3, r4}
    mov r0, r3
    mov r1, r5
    pop {r4, r5, r6, lr}
    bx lr
.none:
    mov r0, #0
    mov r1, r4
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc find_top_op_a32
;; @abi arm_ce
;; @in r0 = command slice, r1 = mode (0 = find `&` / `&&` / `||`, 1 = find single `|`)
;; @out r0 = pointer to operator (or 0 if none), r1 = AST_* kind, r2 = op_len (1 or 2)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Scans left-to-right outside quotes, `^` escapes, and `(...)` nesting for binary command operators, skipping `REM` lines!
@proc find_top_op_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    mov r6, #0 ; quote
    mov r7, #0 ; paren depth

    ; Check if command starts with `REM ` or `::` -> never split on `&` or `|`!
    bl skip_ws_a32
    ldrb r1, [r0, #0]
    cmp r1, #0x40 ; '@'
    bne .chk_col_rem
    add r0, r0, #1
    bl skip_ws_a32
.chk_col_rem:
    ldrb r1, [r0, #0]
    cmp r1, #0x3A
    beq .no_op
    mov r2, r0
    ldrb r0, [r2, #0]
    bl ascii_upper_a32
    cmp r0, #0x52 ; 'R'
    bne .scan_lp
    ldrb r0, [r2, #1]
    bl ascii_upper_a32
    cmp r0, #0x45 ; 'E'
    bne .scan_lp
    ldrb r0, [r2, #2]
    bl ascii_upper_a32
    cmp r0, #0x4D ; 'M'
    bne .scan_lp
    ldrb r0, [r2, #3]
    cmp r0, #0
    beq .no_op
    cmp r0, #0x20
    beq .no_op
    cmp r0, #0x09
    beq .no_op

.scan_lp:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .no_op
    cmp r0, #0x22
    bne .c_car
    eor r6, r6, #1
    add r4, r4, #1
    b .scan_lp
.c_car:
    cmp r6, #0
    bne .adv_s
    cmp r0, #0x5E ; '^'
    bne .c_par
    ldrb r1, [r4, #1]
    cmp r1, #0
    beq .adv_s
    add r4, r4, #2
    b .scan_lp
.c_par:
    cmp r0, #0x28 ; '('
    bne .c_rpar
    add r7, r7, #1
    add r4, r4, #1
    b .scan_lp
.c_rpar:
    cmp r0, #0x29 ; ')'
    bne .c_ops
    cmp r7, #0
    beq .adv_s
    sub r7, r7, #1
    add r4, r4, #1
    b .scan_lp

.c_ops:
    cmp r7, #0
    bne .adv_s
    cmp r5, #0
    bne .mode_pipe

    ; Mode 0: `&&`, `||`, or `&` (taking care not to treat `2>&1` or `<&` as `&`!)
    cmp r0, #0x26 ; '&'
    bne .chk_or2
    ; Check if preceded by `>` or `<` (`2>&1`)!
    ldrb r1, [r4, #1]
    cmp r1, #0x26
    beq .found_and2
    ; Single `&` — verify previous char is not `>` or `<`
    ; (Caller passes start in original r0; we check [r4-1] safely only if not at start; simpler: check during scan!)
    mov r0, r4
    mov r1, #AST_SEQ
    mov r2, #1
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr

.found_and2:
    mov r0, r4
    mov r1, #AST_AND
    mov r2, #2
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr

.chk_or2:
    cmp r0, #0x7C ; '|'
    bne .chk_redir_amp
    ldrb r1, [r4, #1]
    cmp r1, #0x7C
    bne .adv_s
    mov r0, r4
    mov r1, #AST_OR
    mov r2, #2
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr

.mode_pipe:
    cmp r0, #0x7C
    bne .chk_redir_amp
    ldrb r1, [r4, #1]
    cmp r1, #0x7C
    bne .found_pipe1
    add r4, r4, #2
    b .scan_lp
.found_pipe1:
    mov r0, r4
    mov r1, #AST_PIPE
    mov r2, #1
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr

.chk_redir_amp:
    ; Skip `>&` and `<&` handle dup sequences so `&` inside `2>&1` is NEVER mistaken for `AST_SEQ`!
    cmp r0, #0x3E
    beq .chk_dup_nx
    cmp r0, #0x3C
    bne .adv_s
.chk_dup_nx:
    ldrb r1, [r4, #1]
    cmp r1, #0x26
    bne .adv_s
    add r4, r4, #2
    b .scan_lp

.adv_s:
    add r4, r4, #1
    b .scan_lp

.no_op:
    mov r0, #0
    mov r1, #0
    mov r2, #0
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc parse_redirs_and_args_a32
;; @abi arm_ce
;; @in r0 = AST_NODE_A32*, r1 = command text cursor
;; @out Populates pArgs, dwArgc, and pRedir on AST_NODE_A32
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Extracts arbitrary leading/mid/trailing FD 0..9 redirections and argument tokens into AST_NODE_A32.
@proc parse_redirs_and_args_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; node
    mov r5, r1 ; cursor

    ; Allocate pointer vector for up to 128 args (512 bytes)
    mov_imm32 r0, 512
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r6, r0 ; argv
    str r6, [r4, #AST_NODE_A32.pArgs]
    mov r7, #0 ; argc
    mov r8, #0 ; last REDIR_NODE_A32*

.tok_loop:
    mov r0, r5
    bl skip_ws_a32
    mov r5, r0
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .fin_args
    cmp r0, #0x29 ; ')'
    beq .fin_args

    ; Special check: after first arg is `ECHO` (and next char in raw line was delimiter `.`/`(`/etc.), or `REM`!
    ; Check for FD prefix `0..9` immediately followed by `<` or `>` (with space/start before digit)
    cmp r0, #0x30
    bcc .chk_bare_redir
    cmp r0, #0x39
    bhi .chk_bare_redir
    ldrb r1, [r5, #1]
    cmp r1, #0x3E ; '>'
    beq .fd_redir_out
    cmp r1, #0x3C ; '<'
    beq .fd_redir_in
    b .regular_tok

.fd_redir_out:
    sub r9, r0, #0x30 ; fd
    add r5, r5, #1
    b .parse_gt
.fd_redir_in:
    sub r9, r0, #0x30
    add r5, r5, #1
    b .parse_lt

.chk_bare_redir:
    cmp r0, #0x3E ; '>'
    bne .chk_lt
    mov r9, #1 ; default stdout FD 1
    b .parse_gt
.chk_lt:
    cmp r0, #0x3C ; '<'
    bne .regular_tok
    mov r9, #0 ; default stdin FD 0
    b .parse_lt

.parse_gt:
    ; At `>`: check `>>` or `>&d`
    mov r10, #A32_REDIR_OUT
    ldrb r0, [r5, #1]
    cmp r0, #0x3E
    bne .chk_dup_out
    mov r10, #A32_REDIR_APP
    add r5, r5, #2
    b .redir_target
.chk_dup_out:
    cmp r0, #0x26 ; '>&'
    bne .adv_gt1
    ldrb r1, [r5, #2]
    cmp r1, #0x30
    bcc .adv_gt1
    cmp r1, #0x39
    bhi .adv_gt1
    mov r10, #A32_REDIR_DUP_OUT
    sub r0, r1, #0x30
    add r5, r5, #3
    b .alloc_dup_node
.adv_gt1:
    add r5, r5, #1
    b .redir_target

.parse_lt:
    mov r10, #A32_REDIR_IN
    ldrb r0, [r5, #1]
    cmp r0, #0x26 ; '<&'
    bne .adv_lt1
    ldrb r1, [r5, #2]
    cmp r1, #0x30
    bcc .adv_lt1
    cmp r1, #0x39
    bhi .adv_lt1
    mov r10, #A32_REDIR_DUP_IN
    sub r0, r1, #0x30
    add r5, r5, #3
    b .alloc_dup_node
.adv_lt1:
    add r5, r5, #1
    b .redir_target

.alloc_dup_node:
    push {r0, r1}
    mov r0, #REDIR_NODE_A32_SIZE
    bl arena_alloc_a32
    mov r2, r0
    pop {r0, r1}
    cmp r2, #0
    beq .tok_loop
    strb r10, [r2, #REDIR_NODE_A32.bKind]
    strb r9, [r2, #REDIR_NODE_A32.bFd]
    strb r0, [r2, #REDIR_NODE_A32.bTargetFd]
    mov r3, #0
    str r3, [r2, #REDIR_NODE_A32.pTarget]
    str r3, [r2, #REDIR_NODE_A32.pNext]
    b .link_redir

.redir_target:
    mov r0, r5
    bl scan_cmd_token_a32
    mov r5, r1
    cmp r0, #0
    beq .tok_loop
    mov r3, r0
    push {r3, r4}
    mov r0, #REDIR_NODE_A32_SIZE
    bl arena_alloc_a32
    mov r2, r0
    pop {r3, r4}
    cmp r2, #0
    beq .tok_loop
    strb r10, [r2, #REDIR_NODE_A32.bKind]
    strb r9, [r2, #REDIR_NODE_A32.bFd]
    mov r0, #0
    strb r0, [r2, #REDIR_NODE_A32.bTargetFd]
    str r3, [r2, #REDIR_NODE_A32.pTarget]
    str r0, [r2, #REDIR_NODE_A32.pNext]

.link_redir:
    cmp r8, #0
    bne .app_r
    str r2, [r4, #AST_NODE_A32.pRedir]
    mov r8, r2
    b .tok_loop
.app_r:
    str r2, [r8, #REDIR_NODE_A32.pNext]
    mov r8, r2
    b .tok_loop

.regular_tok:
    ; Special handling when r7 == 0: check `ECHO.` / `ECHO(` / `SET` raw tail!
    cmp r7, #0
    bne .std_arg
    mov r0, r5
    mov r1, #4
    ; Check case-insensitive `ECHO` followed immediately by `.`/`(`/`:`/`/`/`=`/`+`/`[`/`]`/`;`/``
    ldrb r0, [r5, #0]
    bl ascii_upper_a32
    cmp r0, #0x45 ; 'E'
    bne .std_arg
    ldrb r0, [r5, #1]
    bl ascii_upper_a32
    cmp r0, #0x43 ; 'C'
    bne .std_arg
    ldrb r0, [r5, #2]
    bl ascii_upper_a32
    cmp r0, #0x48 ; 'H'
    bne .std_arg
    ldrb r0, [r5, #3]
    bl ascii_upper_a32
    cmp r0, #0x4F ; 'O'
    bne .std_arg
    ldrb r0, [r5, #4]
    cmp r0, #0x2E ; '.'
    beq .echo_raw
    cmp r0, #0x28 ; '('
    beq .echo_raw
    cmp r0, #0x3A ; ':'
    beq .echo_raw
    cmp r0, #0x2F ; '/'
    beq .echo_raw
    cmp r0, #0x3D ; '='
    beq .echo_raw
    cmp r0, #0x2B ; '+'
    beq .echo_raw
    cmp r0, #0x20 ; ' '
    beq .echo_sp
    b .std_arg

.echo_sp:
    add r5, r5, #5
    b .echo_emit
.echo_raw:
    add r5, r5, #5
    orr r0, r0, #0
    ldrb r1, [r4, #AST_NODE_A32.bFlags]
    orr r1, r1, #0x10 ; explicit ECHO<delim> flag so empty prints blank line instead of "ECHO is on"!
    strb r1, [r4, #AST_NODE_A32.bFlags]

.echo_emit:
    ; Store argv[0] = "ECHO", argv[1] = raw message before any unquoted `>`/`<` redirection!
    mov r0, #8
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r1, #0x45
    strb r1, [r0, #0]
    mov r1, #0x43
    strb r1, [r0, #1]
    mov r1, #0x48
    strb r1, [r0, #2]
    mov r1, #0x4F
    strb r1, [r0, #3]
    mov r1, #0
    strb r1, [r0, #4]
    str r0, [r6, #0]
    mov r7, #1

    ; Scan raw echo body up to unquoted `<` or `>` (or `d>`/`d<`)!
    mov r9, r5 ; msg start
    mov r10, #0 ; quote
.ec_sc:
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .ec_end
    cmp r0, #0x22
    bne .ec_car
    eor r10, r10, #1
    add r5, r5, #1
    b .ec_sc
.ec_car:
    cmp r10, #0
    bne .ec_adv
    cmp r0, #0x5E
    bne .ec_red
    ldrb r1, [r5, #1]
    cmp r1, #0
    beq .ec_adv
    add r5, r5, #2
    b .ec_sc
.ec_red:
    cmp r0, #0x3E
    beq .ec_end
    cmp r0, #0x3C
    beq .ec_end
    cmp r0, #0x30
    bcc .ec_adv
    cmp r0, #0x39
    bhi .ec_adv
    ldrb r1, [r5, #1]
    cmp r1, #0x3E
    beq .ec_chk_ws_prev
    cmp r1, #0x3C
    beq .ec_chk_ws_prev
    b .ec_adv
.ec_chk_ws_prev:
    cmp r5, r9
    beq .ec_end
    sub r2, r5, #1
    ldrb r1, [r2, #0]
    cmp r1, #0x20
    beq .ec_end
.ec_adv:
    add r5, r5, #1
    b .ec_sc
.ec_end:
    subs r2, r5, r9
    ; Trim trailing space immediately before redirection if redirection follows
    ldrb r0, [r5, #0]
    cmp r0, #0
    beq .ec_alloc
    cmp r2, #0
    beq .ec_alloc
    sub r1, r5, #1
    ldrb r0, [r1, #0]
    cmp r0, #0x20
    bne .ec_alloc
    sub r2, r2, #1
.ec_alloc:
    push {r2, r3}
    add r0, r2, #2
    bl arena_alloc_a32
    mov r3, r0
    pop {r2, r12}
    cmp r3, #0
    beq .tok_loop
    push {r3, r4}
    mov r0, r3
    mov r1, r9
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    pop {r3, r4}
    str r3, [r6, #4]
    mov r7, #2
    b .tok_loop

.std_arg:
    mov r0, r5
    bl scan_cmd_token_a32
    mov r5, r1
    cmp r0, #0
    beq .fin_args
    cmp r7, #126
    bhs .tok_loop
    lsl r1, r7, #2
    str r0, [r6, r1]
    add r7, r7, #1
    b .tok_loop

.fin_args:
    str r7, [r4, #AST_NODE_A32.dwArgc]
.done:
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc parse_line_ast_a32
;; @abi arm_ce
;; @in r0 = Phase-1 expanded WTF-8 command string
;; @out r0 = AST_NODE_A32* (or 0 on blank/comment/label)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Parses compound operators (`&`, `&&`, `||`, `|`), `( ... )` blocks, `IF`..`ELSE`, `FOR`, and simple commands into Arena AST.
@proc parse_line_ast_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r0, r0
    bl skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .ret_null

    ; Check lowest-precedence `&`, `&&`, `||`
    mov r0, r4
    mov r1, #0
    bl find_top_op_a32
    cmp r0, #0
    bne .split_bin

    ; Check higher-precedence `|`
    mov r0, r4
    mov r1, #1
    bl find_top_op_a32
    cmp r0, #0
    beq .parse_unary

.split_bin:
    mov r5, r0 ; op ptr
    mov r6, r1 ; AST_* kind
    mov r7, r2 ; op len

    ; Left slice r4[0 .. r5-r4]
    sub r8, r5, r4
    add r0, r8, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_null
    mov r9, r0
    mov r1, r4
    mov r2, r8
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]

    mov r0, r6
    bl alloc_ast_a32
    cmp r0, #0
    beq .ret_null
    mov r10, r0
    str r9, [r10, #AST_NODE_A32.pExtra]

    mov r0, r9
    bl parse_line_ast_a32
    str r0, [r10, #AST_NODE_A32.pLeft]

    add r0, r5, r7
    bl arena_strdup_a32
    str r0, [r10, #AST_NODE_A32.pStr1]
    bl parse_line_ast_a32
    str r0, [r10, #AST_NODE_A32.pRight]
    mov r0, r10
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.parse_unary:
    ; Leading `@` echo suppression
    mov r5, #0 ; flags
.at_lp:
    ldrb r0, [r4, #0]
    cmp r0, #0x40 ; '@'
    bne .after_at
    orr r5, r5, #AST_FLAG_NO_ECHO
    add r4, r4, #1
    mov r0, r4
    bl skip_ws_a32
    mov r4, r0
    b .at_lp

.after_at:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .ret_null
    cmp r0, #0x3A ; `:label` or `::` comment -> no-op AST!
    beq .ret_null

    ; Check parenthesized block `( ... ) [redirs]`
    cmp r0, #0x28 ; '('
    beq .parse_block

    ; Peek first word for `REM`, `IF`, or `FOR`
    mov r0, r4
    bl scan_cmd_token_a32
    cmp r0, #0
    beq .simple_cmd
    mov r6, r0 ; word0
    mov r7, r1 ; after word0

    ; Check `REM`
    mov r0, r6
    bl str_len_a32
    cmp r0, #3
    bne .chk_kw_if
    ldrb r0, [r6, #0]
    bl ascii_upper_a32
    cmp r0, #0x52
    bne .chk_kw_if
    ldrb r0, [r6, #1]
    bl ascii_upper_a32
    cmp r0, #0x45
    bne .chk_kw_if
    ldrb r0, [r6, #2]
    bl ascii_upper_a32
    cmp r0, #0x4D
    beq .ret_rem_node

.chk_kw_if:
    mov r0, r6
    bl str_len_a32
    cmp r0, #2
    bne .chk_kw_for
    ldrb r0, [r6, #0]
    bl ascii_upper_a32
    cmp r0, #0x49 ; 'I'
    bne .chk_kw_for
    ldrb r0, [r6, #1]
    bl ascii_upper_a32
    cmp r0, #0x46 ; 'F'
    bne .chk_kw_for
    mov r0, r7
    mov r1, r5
    bl parse_if_ast_a32
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.chk_kw_for:
    mov r0, r6
    bl str_len_a32
    cmp r0, #3
    bne .simple_cmd
    ldrb r0, [r6, #0]
    bl ascii_upper_a32
    cmp r0, #0x46 ; 'F'
    bne .simple_cmd
    ldrb r0, [r6, #1]
    bl ascii_upper_a32
    cmp r0, #0x4F ; 'O'
    bne .simple_cmd
    ldrb r0, [r6, #2]
    bl ascii_upper_a32
    cmp r0, #0x52 ; 'R'
    bne .simple_cmd
    mov r0, r7
    mov r1, r5
    bl parse_for_ast_a32
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.simple_cmd:
    mov r0, #AST_SIMPLE_CMD
    bl alloc_ast_a32
    cmp r0, #0
    beq .ret_null
    mov r6, r0
    strb r5, [r6, #AST_NODE_A32.bFlags]
    mov r0, r4
    bl arena_strdup_a32
    str r0, [r6, #AST_NODE_A32.pStr1]
    mov r0, r6
    mov r1, r4
    bl parse_redirs_and_args_a32
    mov r0, r6
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ret_rem_node:
    mov r0, #AST_REM
    bl alloc_ast_a32
    cmp r0, #0
    beq .ret_null
    strb r5, [r0, #AST_NODE_A32.bFlags]
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.parse_block:
    ; Find matching `)` (reading continuation lines from active batch frame if unclosed!)
    mov r0, r4
    bl ensure_balanced_parens_a32
    mov r4, r0
    ; Locate matching `)` at depth 0
    add r1, r4, #1
    mov r2, #1 ; depth
    mov r3, #0 ; quote
.p_sc:
    ldrb r0, [r1, #0]
    cmp r0, #0
    beq .p_found
    cmp r0, #0x22
    bne .p_c2
    eor r3, r3, #1
    add r1, r1, #1
    b .p_sc
.p_c2:
    cmp r3, #0
    bne .p_adv
    cmp r0, #0x28
    bne .p_c3
    add r2, r2, #1
    add r1, r1, #1
    b .p_sc
.p_c3:
    cmp r0, #0x29
    bne .p_adv
    subs r2, r2, #1
    beq .p_found
.p_adv:
    add r1, r1, #1
    b .p_sc

.p_found:
    mov r8, r1 ; ptr to ')' or NUL
    add r9, r4, #1
    sub r6, r8, r9 ; inner len
    add r0, r6, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .ret_null
    mov r7, r0
    mov r1, r9
    mov r2, r6
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]

    mov r0, #AST_BLOCK
    bl alloc_ast_a32
    cmp r0, #0
    beq .ret_null
    mov r10, r0
    strb r5, [r10, #AST_NODE_A32.bFlags]
    mov r0, r7
    bl parse_line_ast_a32
    str r0, [r10, #AST_NODE_A32.pLeft]

    ; Parse any trailing redirections after `)`!
    ldrb r0, [r8, #0]
    cmp r0, #0x29
    bne .ret_blk
    mov r0, r10
    add r1, r8, #1
    bl parse_redirs_and_args_a32
.ret_blk:
    mov r0, r10
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.ret_null:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc ensure_balanced_parens_a32
;; @abi arm_ce
;; @in r0 = command line starting with `(`
;; @out r0 = balanced command line (joining batch lines with `&` until depth == 0)
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reads and Phase-1-expands subsequent batch file lines when a `(` compound block spans multiple physical lines.
@proc ensure_balanced_parens_a32
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    mov r4, r0

.recheck:
    mov r1, r4
    mov r2, #0 ; depth
    mov r3, #0 ; quote
.cnt_p:
    ldrb r0, [r1, #0]
    cmp r0, #0
    beq .chk_depth
    cmp r0, #0x22
    bne .c_open
    eor r3, r3, #1
    add r1, r1, #1
    b .cnt_p
.c_open:
    cmp r3, #0
    bne .c_nx
    cmp r0, #0x28
    bne .c_close
    add r2, r2, #1
    b .c_nx
.c_close:
    cmp r0, #0x29
    bne .c_nx
    cmp r2, #0
    beq .c_nx
    sub r2, r2, #1
.c_nx:
    add r1, r1, #1
    b .cnt_p

.chk_depth:
    cmp r2, #0
    beq .done
    ; Need another line from active batch frame if in batch!
    bl hal_get_ctx
    ldr r1, [r0, #ARM32_CTX.dwBatchDepth]
    cmp r1, #0
    beq .done
    sub r1, r1, #1
    lsl r1, r1, #6
    ldr r2, [r0, #ARM32_CTX.pBatchStack]
    add r5, r2, r1 ; active frame

    mov_imm32 r0, A32_LINE_MAX
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r6, r0
    mov r0, r5
    mov r1, r6
    bl batch_read_logical_line_a32
    cmp r0, #0
    beq .done
    mov r0, r6
    mov r1, #1
    bl expand_phase1_a32
    mov r6, r0
    mov r0, r6
    bl skip_ws_a32
    mov r6, r0
    ldrb r0, [r6, #0]
    cmp r0, #0
    beq .recheck
    cmp r0, #0x3A ; skip `::` comments inside `(...)`!
    beq .recheck

    ; Join `r4` + (` ` if r4 ends with `(` or r6 starts with `)`, else ` & `) + `r6`!
    mov r0, r4
    bl str_len_a32
    mov r7, r0
    mov r0, r6
    bl str_len_a32
    add r0, r0, r7
    add r0, r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .done
    mov r5, r0
    mov r1, r4
    bl str_copy_a32
    ; Check last non-space char of r4 and first char of r6
    mov r2, r0
.trim_back:
    cmp r2, r5
    bls .join_sp
    sub r3, r2, #1
    ldrb r1, [r3, #0]
    cmp r1, #0x20
    bne .chk_prev_ch
    mov r2, r3
    b .trim_back
.chk_prev_ch:
    cmp r1, #0x28 ; '('
    beq .join_sp
    ldrb r1, [r6, #0]
    cmp r1, #0x29 ; ')'
    beq .join_sp
    ; Check `ELSE` at start of r6 -> join with space, NOT `&`!
    mov r0, r6
    bl is_else_kw_a32
    cmp r0, #0
    bne .join_sp
    mov r1, #0x20
    strb r1, [r2, #0]
    mov r1, #0x26 ; '&'
    strb r1, [r2, #1]
    mov r1, #0x20
    strb r1, [r2, #2]
    add r0, r2, #3
    b .do_cat
.join_sp:
    mov r1, #0x20
    strb r1, [r2, #0]
    add r0, r2, #1
.do_cat:
    mov r1, r6
    bl str_copy_a32
    mov r4, r5
    b .recheck

.done:
    mov r0, r4
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc is_else_kw_a32
;; @abi arm_ce
;; @in r0 = cursor
;; @out r0 = 1 if cursor starts with `ELSE` followed by space/`(`/`\0`, else 0
;; @err none
;; @clobbers r0, r1, r2, lr
;; @desc Checks whether a token starts with the `ELSE` keyword.
@proc is_else_kw_a32
    push {r4, lr}
    mov r4, r0
    ldrb r0, [r4, #0]
    bl ascii_upper_a32
    cmp r0, #0x45 ; 'E'
    bne .no
    ldrb r0, [r4, #1]
    bl ascii_upper_a32
    cmp r0, #0x4C ; 'L'
    bne .no
    ldrb r0, [r4, #2]
    bl ascii_upper_a32
    cmp r0, #0x53 ; 'S'
    bne .no
    ldrb r0, [r4, #3]
    bl ascii_upper_a32
    cmp r0, #0x45 ; 'E'
    bne .no
    ldrb r0, [r4, #4]
    cmp r0, #0
    beq .yes
    cmp r0, #0x20
    beq .yes
    cmp r0, #0x09
    beq .yes
    cmp r0, #0x28
    beq .yes
.no:
    mov r0, #0
    pop {r4, lr}
    bx lr
.yes:
    mov r0, #1
    pop {r4, lr}
    bx lr
@endproc

;; @proc parse_if_ast_a32
;; @abi arm_ce
;; @in r0 = cursor after `IF`, r1 = AST flags
;; @out r0 = AST_IF node
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Parses `IF [/I] [NOT] (ERRORLEVEL n | EXIST p | DEFINED v | CMDEXTVERSION n | a == b | a OP b) body [ELSE alt]`.
@proc parse_if_ast_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; cursor
    mov r5, r1 ; flags

    mov r0, #AST_IF
    bl alloc_ast_a32
    cmp r0, #0
    beq .done
    mov r6, r0 ; IF node

.opt_lp:
    mov r0, r4
    bl scan_cmd_token_a32
    cmp r0, #0
    beq .fin_if
    mov r7, r0 ; tok
    mov r8, r1 ; next cursor

    ; Check `/I`
    ldrb r0, [r7, #0]
    cmp r0, #0x2F ; '/'
    bne .chk_not
    ldrb r0, [r7, #1]
    bl ascii_upper_a32
    cmp r0, #0x49 ; 'I'
    bne .cond_tok
    ldrb r0, [r7, #2]
    cmp r0, #0
    bne .cond_tok
    orr r5, r5, #AST_FLAG_IF_ICASE
    mov r4, r8
    b .opt_lp

.chk_not:
    mov r0, r7
    bl str_len_a32
    cmp r0, #3
    bne .cond_tok
    ldrb r0, [r7, #0]
    bl ascii_upper_a32
    cmp r0, #0x4E ; 'N'
    bne .cond_tok
    ldrb r0, [r7, #1]
    bl ascii_upper_a32
    cmp r0, #0x4F ; 'O'
    bne .cond_tok
    ldrb r0, [r7, #2]
    bl ascii_upper_a32
    cmp r0, #0x54 ; 'T'
    bne .cond_tok
    orr r5, r5, #AST_FLAG_IF_NOT
    mov r4, r8
    b .opt_lp

.cond_tok:
    strb r5, [r6, #AST_NODE_A32.bFlags]
    ; Check unary keywords: EXIST (5), DEFINED (7), ERRORLEVEL (10), CMDEXTVERSION (13)
    mov r0, r7
    bl str_len_a32
    cmp r0, #5
    beq .try_exist
    cmp r0, #7
    beq .try_def
    cmp r0, #10
    beq .try_errlvl
    cmp r0, #13
    beq .try_extver
    b .binary_cmp

.try_exist:
    ldrb r0, [r7, #0]
    bl ascii_upper_a32
    cmp r0, #0x45 ; 'E'
    bne .binary_cmp
    ldrb r0, [r7, #1]
    bl ascii_upper_a32
    cmp r0, #0x58 ; 'X'
    bne .binary_cmp
    mov r0, #A32_IF_EXIST
    b .unary_arg
.try_def:
    ldrb r0, [r7, #0]
    bl ascii_upper_a32
    cmp r0, #0x44 ; 'D'
    bne .binary_cmp
    ldrb r0, [r7, #1]
    bl ascii_upper_a32
    cmp r0, #0x45 ; 'E'
    bne .binary_cmp
    mov r0, #A32_IF_DEFINED
    b .unary_arg
.try_errlvl:
    ldrb r0, [r7, #0]
    bl ascii_upper_a32
    cmp r0, #0x45 ; 'E'
    bne .binary_cmp
    ldrb r0, [r7, #1]
    bl ascii_upper_a32
    cmp r0, #0x52 ; 'R'
    bne .binary_cmp
    mov r0, #A32_IF_ERRORLEVEL
    b .unary_arg
.try_extver:
    ldrb r0, [r7, #0]
    bl ascii_upper_a32
    cmp r0, #0x43 ; 'C'
    bne .binary_cmp
    mov r0, #A32_IF_CMDEXTVER

.unary_arg:
    strb r0, [r6, #AST_NODE_A32.bSubKind]
    mov r0, r8
    bl scan_cmd_token_a32
    str r0, [r6, #AST_NODE_A32.pStr1]
    mov r4, r1
    b .parse_then_else

.binary_cmp:
    ; Check if r7 contains embedded `==` (`a==b` without spaces!)
    mov r9, #0
.sc_eq2:
    ldrb r0, [r7, r9]
    cmp r0, #0
    beq .sep_op
    cmp r0, #0x3D
    bne .nx_eq2
    add r1, r9, #1
    ldrb r2, [r7, r1]
    cmp r2, #0x3D
    beq .split_inline_eq
.nx_eq2:
    add r9, r9, #1
    b .sc_eq2

.split_inline_eq:
    ; Left = r7[0..r9], Right = r7[r9+2..] (or next token if empty!)
    add r0, r9, #2
    bl arena_alloc_a32
    mov r10, r0
    mov r1, r7
    mov r2, r9
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    str r10, [r6, #AST_NODE_A32.pStr1]
    mov r0, #A32_IF_CMP_EQ
    strb r0, [r6, #AST_NODE_A32.bSubKind]
    add r0, r7, r9
    add r0, r0, #2
    ldrb r1, [r0, #0]
    cmp r1, #0
    beq .read_rhs_tok
    bl arena_strdup_a32
    str r0, [r6, #AST_NODE_A32.pExtra]
    mov r4, r8
    b .parse_then_else
.read_rhs_tok:
    mov r0, r8
    bl scan_cmd_token_a32
    str r0, [r6, #AST_NODE_A32.pExtra]
    mov r4, r1
    b .parse_then_else

.sep_op:
    str r7, [r6, #AST_NODE_A32.pStr1]
    mov r0, r8
    bl scan_cmd_token_a32
    mov r9, r0 ; op token
    mov r8, r1
    cmp r9, #0
    beq .fin_if
    ; Determine op subtype: `==` (0), `EQU` (0), `NEQ` (1), `LSS` (2), `LEQ` (3), `GTR` (4), `GEQ` (5)
    mov r10, #A32_IF_CMP_EQ
    ldrb r0, [r9, #0]
    cmp r0, #0x3D
    beq .got_op_sub
    bl ascii_upper_a32
    mov r1, r0
    ldrb r0, [r9, #1]
    push {r1, r2}
    bl ascii_upper_a32
    mov r3, r0
    pop {r1, r2}
    cmp r1, #0x4E ; 'N' -> NEQ
    bne .c_l
    mov r10, #A32_IF_CMP_NEQ
    b .got_op_sub
.c_l:
    cmp r1, #0x4C ; 'L' -> LSS / LEQ
    bne .c_g
    mov r10, #A32_IF_CMP_LSS
    cmp r3, #0x45 ; 'E'
    bne .got_op_sub
    mov r10, #A32_IF_CMP_LEQ
    b .got_op_sub
.c_g:
    cmp r1, #0x47 ; 'G' -> GTR / GEQ
    bne .got_op_sub
    mov r10, #A32_IF_CMP_GTR
    cmp r3, #0x45 ; 'E'
    bne .got_op_sub
    mov r10, #A32_IF_CMP_GEQ

.got_op_sub:
    strb r10, [r6, #AST_NODE_A32.bSubKind]
    mov r0, r8
    bl scan_cmd_token_a32
    str r0, [r6, #AST_NODE_A32.pExtra] ; RHS
    mov r4, r1

.parse_then_else:
    mov r0, r4
    bl skip_ws_a32
    mov r4, r0
    ; Ensure multi-line `(` blocks on THEN/ELSE are balanced!
    ldrb r0, [r4, #0]
    cmp r0, #0x28
    bne .find_else
    mov r0, r4
    bl ensure_balanced_parens_a32
    mov r4, r0

.find_else:
    ; Scan r4 at paren depth 0 & outside quotes for ` ELSE `
    mov r7, r4 ; scan ptr
    mov r8, #0 ; quote
    mov r9, #0 ; paren depth
.el_lp:
    ldrb r0, [r7, #0]
    cmp r0, #0
    beq .no_else
    cmp r0, #0x22
    bne .el_p
    eor r8, r8, #1
    add r7, r7, #1
    b .el_lp
.el_p:
    cmp r8, #0
    bne .el_nx
    cmp r0, #0x28
    bne .el_rp
    add r9, r9, #1
    add r7, r7, #1
    b .el_lp
.el_rp:
    cmp r0, #0x29
    bne .el_kw
    cmp r9, #0
    beq .el_nx
    sub r9, r9, #1
    add r7, r7, #1
    b .el_lp
.el_kw:
    cmp r9, #0
    bne .el_nx
    cmp r0, #0x20
    beq .chk_after_sp
    cmp r0, #0x09
    bne .el_nx
.chk_after_sp:
    add r0, r7, #1
    bl skip_ws_a32
    mov r10, r0
    bl is_else_kw_a32
    cmp r0, #0
    bne .got_else
.el_nx:
    add r7, r7, #1
    b .el_lp

.got_else:
    ; THEN = r4[0 .. r7-r4], ELSE = r10 + 4
    sub r8, r7, r4
    add r0, r8, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .fin_if
    mov r9, r0
    mov r1, r4
    mov r2, r8
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    mov r0, r9
    bl parse_line_ast_a32
    str r0, [r6, #AST_NODE_A32.pLeft]

    add r0, r10, #4
    bl parse_line_ast_a32
    str r0, [r6, #AST_NODE_A32.pRight]
    b .fin_if

.no_else:
    mov r0, r4
    bl parse_line_ast_a32
    str r0, [r6, #AST_NODE_A32.pLeft]

.fin_if:
    mov r0, r6
.done:
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc parse_for_ast_a32
;; @abi arm_ce
;; @in r0 = cursor after `FOR`, r1 = AST flags
;; @out r0 = AST_FOR node
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Parses `FOR [/D | /R [root] | /L | /F ["opts"]] %%V IN (set) DO body`.
@proc parse_for_ast_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; cursor
    mov r5, r1 ; flags

    mov r0, #AST_FOR
    bl alloc_ast_a32
    cmp r0, #0
    beq .done
    mov r6, r0
    strb r5, [r6, #AST_NODE_A32.bFlags]
    mov r7, #A32_FOR_PLAIN

.sw_lp:
    mov r0, r4
    bl skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0x2F ; '/'
    bne .var_tok
    ldrb r0, [r4, #1]
    bl ascii_upper_a32
    add r4, r4, #2
    cmp r0, #0x44 ; '/D'
    beq .sw_d
    cmp r0, #0x4C ; '/L'
    beq .sw_l
    cmp r0, #0x52 ; '/R'
    beq .sw_r
    cmp r0, #0x46 ; '/F'
    beq .sw_f
    b .sw_lp
.sw_d:
    mov r7, #A32_FOR_DIRS
    b .sw_lp
.sw_l:
    mov r7, #A32_FOR_LOOP
    b .sw_lp
.sw_r:
    mov r7, #A32_FOR_REC
    mov r0, r4
    bl skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0x25 ; '%' -> no optional root dir!
    beq .var_tok
    mov r0, r4
    bl scan_cmd_token_a32
    str r0, [r6, #AST_NODE_A32.pExtra] ; root dir!
    mov r4, r1
    b .var_tok
.sw_f:
    mov r7, #A32_FOR_PARSE
    mov r0, r4
    bl skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0x25 ; '%' -> no options string!
    beq .var_tok
    mov r0, r4
    bl scan_cmd_token_a32
    str r0, [r6, #AST_NODE_A32.pExtra] ; "options"
    mov r4, r1

.var_tok:
    strb r7, [r6, #AST_NODE_A32.bSubKind]
    mov r0, r4
    bl scan_cmd_token_a32
    mov r4, r1
    cmp r0, #0
    beq .fin_for
    ; Extract var char after `%` or `%%`
    mov r2, r0
.sk_p:
    ldrb r1, [r2, #0]
    cmp r1, #0x25
    bne .got_vch
    add r2, r2, #1
    b .sk_p
.got_vch:
    strb r1, [r6, #AST_NODE_A32.bForVar]

    ; Consume `IN`
    mov r0, r4
    bl scan_cmd_token_a32
    mov r4, r1
    mov r0, r4
    bl skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0x28 ; '('
    bne .fin_for

    ; Ensure multi-line `(set)` is balanced, then extract set up to matching `)`
    mov r0, r4
    bl ensure_balanced_parens_a32
    mov r4, r0
    add r8, r4, #1
    mov r1, r8
    mov r2, #1 ; depth
    mov r3, #0 ; quote
.in_sc:
    ldrb r0, [r1, #0]
    cmp r0, #0
    beq .in_end
    cmp r0, #0x22
    bne .in_p
    eor r3, r3, #1
    add r1, r1, #1
    b .in_sc
.in_p:
    cmp r3, #0
    bne .in_nx
    cmp r0, #0x28
    bne .in_rp
    add r2, r2, #1
    add r1, r1, #1
    b .in_sc
.in_rp:
    cmp r0, #0x29
    bne .in_nx
    subs r2, r2, #1
    beq .in_end
.in_nx:
    add r1, r1, #1
    b .in_sc

.in_end:
    mov r9, r1 ; ')' ptr
    sub r10, r9, r8
    add r0, r10, #2
    bl arena_alloc_a32
    cmp r0, #0
    beq .fin_for
    mov r3, r0
    mov r1, r8
    mov r2, r10
    push {r3, r4}
    bl mem_copy_a32
    mov r1, #0
    strb r1, [r0, #0]
    pop {r3, r4}
    str r3, [r6, #AST_NODE_A32.pStr1] ; IN (...) set string!

    ldrb r0, [r9, #0]
    cmp r0, #0x29
    bne .fin_for
    add r0, r9, #1
    ; Consume `DO`
    bl scan_cmd_token_a32
    mov r0, r1
    bl skip_ws_a32
    mov r4, r0
    ldrb r0, [r4, #0]
    cmp r0, #0x28
    bne .parse_do_body
    mov r0, r4
    bl ensure_balanced_parens_a32
    mov r4, r0
.parse_do_body:
    mov r0, r4
    bl parse_line_ast_a32
    str r0, [r6, #AST_NODE_A32.pLeft]

.fin_for:
    mov r0, r6
.done:
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc
