;; @file exp_prs_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 5-phase batch expander (%*, %0..%9, %~[fdpnxsatz|$VAR:], %VAR:~s,l%, %VAR:a=b%, %%I, !VAR!) and Arena AST parser.

@cpu arm64
@format inc

;; @proc unquote_dup_a64
;; @abi win_arm64
;; @in x0 = NUL-terminated string
;; @out x0 = arena copy with matching outer `"..."` stripped
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Duplicates x0 into the statement arena and strips surrounding double quotes without mutating caller storage.
@proc unquote_dup_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    bl arena_strdup_a64
    cbz x0, .done
    bl strip_outer_quotes_a64
.done:
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc fmt_u64_plain_a64
;; @abi win_arm64
;; @in x0 = unsigned 64-bit integer, x1 = output buffer (>= 24 bytes)
;; @out x0 = byte length of formatted decimal string
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6
;; @desc Formats a 64-bit unsigned integer in plain decimal ASCII without commas (for `%~z1`).
@proc fmt_u64_plain_a64
    mov x4, x1
    cbnz x0, .nz
    mov w2, #0x30
    strb w2, [x4, #0]
    strb wzr, [x4, #1]
    mov x0, #1
    ret
.nz:
    mov x2, x0
    mov x5, x4
    mov x6, #10
.lp:
    cbz x2, .rev
    udiv x3, x2, x6
    msub x0, x3, x6, x2
    add w0, w0, #0x30
    strb w0, [x4, #0]
    add x4, x4, #1
    mov x2, x3
    b .lp
.rev:
    strb wzr, [x4, #0]
    sub x0, x4, x1
    sub x4, x4, #1
.r_lp:
    cmp x5, x4
    b.hs .done
    ldrb w2, [x5, #0]
    ldrb w3, [x4, #0]
    strb w3, [x5, #0]
    strb w2, [x4, #0]
    add x5, x5, #1
    sub x4, x4, #1
    b .r_lp
.done:
    ret
@endproc

;; @proc search_env_path_mod_a64
;; @abi win_arm64
;; @in x0 = env var name (`PATH`), x1 = leaf filename
;; @out x0 = resolved full path in arena (or empty string if not found)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Implements `%~$VAR:n` / `%%~$VAR:I` searching semicolon-delimited directories in `%VAR%`.
@proc search_env_path_mod_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov x0, x1
    bl unquote_dup_a64
    mov x20, x0 ; clean leaf
    cbz x20, .empty
    ldrb w0, [x20, #0]
    cbz w0, .empty

    mov x0, x19
    bl env_get_a64
    cbz x0, .empty
    mov x21, x0 ; path list cursor

.dir_lp:
    ldrb w0, [x21, #0]
    cbz w0, .empty
    cmp w0, #0x3B
    b.ne .seg_start
    add x21, x21, #1
    b .dir_lp
.seg_start:
    mov x22, x21
.seg_sc:
    ldrb w0, [x22, #0]
    cbz w0, .got_seg
    cmp w0, #0x3B
    b.eq .got_seg
    add x22, x22, #1
    b .seg_sc
.got_seg:
    sub x23, x22, x21
    cbz x23, .adv_seg
    mov x0, x20
    bl str_len_a64
    add x0, x0, x23
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .empty
    mov x24, x0
    mov x1, x21
    mov x2, x23
    bl mem_copy_a64
    sub x1, x0, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.eq .app_leaf
    cmp w2, #0x2F
    b.eq .app_leaf
    mov w2, #0x5C
    strb w2, [x0, #0]
    add x0, x0, #1
.app_leaf:
    mov x1, x20
    bl str_copy_a64
    mov x0, x24
    bl norm_path_a64
    mov x24, x0
    bl to_u16_longpath_a64
    cbz x0, .adv_seg
    bl hal_get_file_attr_a64
    cmn w0, #1
    b.eq .adv_seg
    tbnz w0, #4, .adv_seg ; skip directories (`FILE_ATTRIBUTE_DIRECTORY` = 0x10)
    mov x0, x24
    b .done

.adv_seg:
    mov x21, x22
    ldrb w0, [x21, #0]
    cbz w0, .empty
    add x21, x21, #1
    b .dir_lp

.empty:
    mov x0, #8
    bl arena_alloc_a64
    cbz x0, .done
    strb wzr, [x0, #0]
.done:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc apply_tilde_mods_a64
;; @abi win_arm64
;; @in x0 = raw argument value, w1 = modifier mask (bit0=f,1=d,2=p,3=n,4=x,5=s,6=a,7=t,8=z)
;; @out x0 = formatted WTF-8 result in arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Evaluates `%~[fdpnxsatz]` parameter & FOR-variable path/metadata modifiers.
@proc apply_tilde_mods_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #336 ; FIND_INFO_A64 (320B) at sp+0
    mov x29, sp
    mov w20, w1 ; mask

    bl unquote_dup_a64
    mov x19, x0
    cbz x19, .ret_empty
    cbz w20, .ret_x19

    mov x0, sp
    mov x1, #320
    bl mem_zero_a64

    mov x0, x19
    bl norm_path_a64
    mov x21, x0 ; normalized full path
    cbz x21, .ret_x19

    mov x0, x21
    bl str_len_a64
    add x0, x0, #128
    bl arena_alloc_a64
    cbz x0, .ret_x19
    mov x22, x0 ; output base
    mov x23, x0 ; output cursor
    strb wzr, [x23, #0]

    ; Query filesystem metadata if s/a/t/z requested (bits 5..8 -> 0x1E0)
    tst w20, #0x1E0
    b.eq .chk_f

    mov x0, x21
    bl to_u16_longpath_a64
    cbz x0, .chk_f
    mov x1, sp
    bl hal_find_first_a64
    cmn x0, #1
    b.eq .chk_f
    bl hal_find_close_a64

    ; bit 6 (0x40): attributes `--a------` / `d--------`
    tbz w20, #6, .chk_t
    ldr w2, [sp, #FIND_INFO_A64.dwAttr]
    mov w1, #0x2D
    tbz w2, #4, .at0
    mov w1, #0x64
.at0:
    strb w1, [x23, #0]
    mov w1, #0x2D
    tbz w2, #0, .at1
    mov w1, #0x72
.at1:
    strb w1, [x23, #1]
    mov w1, #0x2D
    tbz w2, #5, .at2
    mov w1, #0x61
.at2:
    strb w1, [x23, #2]
    mov w1, #0x2D
    tbz w2, #1, .at3
    mov w1, #0x68
.at3:
    strb w1, [x23, #3]
    mov w1, #0x2D
    tbz w2, #2, .at4
    mov w1, #0x73
.at4:
    strb w1, [x23, #4]
    mov w1, #0x2D
    strb w1, [x23, #5]
    strb w1, [x23, #6]
    strb w1, [x23, #7]
    strb w1, [x23, #8]
    mov w1, #0x20
    strb w1, [x23, #9]
    add x23, x23, #10

.chk_t:
    tbz w20, #7, .chk_z
    ldr x0, [sp, #FIND_INFO_A64.uWriteTime]
    mov x1, x23
    bl fmt_filetime_a64
    mov x23, x0
    mov w1, #0x20
    strb w1, [x23, #0]
    add x23, x23, #1

.chk_z:
    tbz w20, #8, .chk_f
    ldr x0, [sp, #FIND_INFO_A64.uSize]
    mov x1, x23
    bl fmt_u64_plain_a64
    add x23, x23, x0
    mov w1, #0x20
    strb w1, [x23, #0]
    add x23, x23, #1

.chk_f:
    tbnz w20, #0, .emit_full
    and w0, w20, #0x1E
    cbnz w0, .split_dpnx
    tbnz w20, #5, .emit_short_or_full
    b .trim_sp_end

.emit_short_or_full:
    ldrb w0, [sp, #FIND_INFO_A64.szShort]
    cbz w0, .emit_full
    mov x0, x23
    mov x1, x21
    bl str_copy_a64
    mov x1, x0
.bk_sl:
    cmp x1, x23
    b.ls .cp_sh
    sub x1, x1, #1
    ldrb w2, [x1, #0]
    cmp w2, #0x5C
    b.ne .bk_sl
    add x1, x1, #1
.cp_sh:
    mov x0, x1
    add x1, sp, #FIND_INFO_A64.szShort
    bl str_copy_a64
    mov x23, x0
    b .finish_mod

.emit_full:
    mov x0, x23
    mov x1, x21
    bl str_copy_a64
    mov x23, x0
    b .finish_mod

.split_dpnx:
    mov x24, x21 ; last_sep
    mov x1, x21
    ldrb w0, [x21, #1]
    cmp w0, #0x3A
    b.ne .sc_sl
    add x1, x21, #2
    mov x24, x1
.sc_sl:
    ldrb w0, [x1, #0]
    cbz w0, .sc_dot_init
    cmp w0, #0x5C
    b.ne .sc_sl_nx
    add x24, x1, #1
.sc_sl_nx:
    add x1, x1, #1
    b .sc_sl

.sc_dot_init:
    mov x25, x1 ; end of string
    mov x2, x24
.sc_dot:
    ldrb w0, [x2, #0]
    cbz w0, .emit_parts
    cmp w0, #0x2E
    b.ne .sc_dot_nx
    mov x25, x2
.sc_dot_nx:
    add x2, x2, #1
    b .sc_dot

.emit_parts:
    tbz w20, #1, .part_p
    ldrb w0, [x21, #1]
    cmp w0, #0x3A
    b.ne .part_p
    ldrb w0, [x21, #0]
    strb w0, [x23, #0]
    mov w0, #0x3A
    strb w0, [x23, #1]
    add x23, x23, #2
.part_p:
    tbz w20, #2, .part_n
    mov x1, x21
    ldrb w0, [x21, #1]
    cmp w0, #0x3A
    b.ne .p_st
    add x1, x21, #2
.p_st:
    subs x2, x24, x1
    b.ls .part_n
    mov x0, x23
    bl mem_copy_a64
    mov x23, x0
.part_n:
    tbz w20, #3, .part_x
    ; If `%~s` + `n`/`x` requested and `szShort` non-empty, split `szShort`? Use standard stem [x24..x25)
    subs x2, x25, x24
    b.ls .part_x
    mov x0, x23
    mov x1, x24
    bl mem_copy_a64
    mov x23, x0
.part_x:
    tbz w20, #4, .finish_mod
    mov x0, x23
    mov x1, x25
    bl str_copy_a64
    mov x23, x0
    b .finish_mod

.trim_sp_end:
    cmp x23, x22
    b.ls .finish_mod
    sub x1, x23, #1
    ldrb w0, [x1, #0]
    cmp w0, #0x20
    b.ne .finish_mod
    mov x23, x1

.finish_mod:
    strb wzr, [x23, #0]
    mov x0, x22
    b .exit

.ret_x19:
    mov x0, x19
    b .exit

.ret_empty:
    mov x0, #8
    bl arena_alloc_a64
    cbz x0, .exit
    strb wzr, [x0, #0]
.exit:
    add sp, sp, #336
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc parse_signed_dec_a64
;; @abi win_arm64
;; @in x0 = string cursor
;; @out x0 = parsed signed 64-bit integer, x1 = advanced cursor
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Parses an optional `-`/`+` signed decimal integer for `%VAR:~start,len%`.
@proc parse_signed_dec_a64
    mov x1, x0
    mov w2, #0
    ldrb w0, [x1, #0]
    cmp w0, #0x2D
    b.ne .chk_plus
    mov w2, #1
    add x1, x1, #1
    b .digits
.chk_plus:
    cmp w0, #0x2B
    b.ne .digits
    add x1, x1, #1
.digits:
    mov x0, #0
    mov x4, #10
.d_lp:
    ldrb w3, [x1, #0]
    cmp w3, #0x30
    b.lo .d_done
    cmp w3, #0x39
    b.hi .d_done
    sub w3, w3, #0x30
    uxtw x3, w3
    mul x0, x0, x4
    add x0, x0, x3
    add x1, x1, #1
    b .d_lp
.d_done:
    cbz w2, .ret_d
    neg x0, x0
.ret_d:
    ret
@endproc

;; @proc expand_var_op_a64
;; @abi win_arm64
;; @in x0 = inner token (between `%..%` or `!..!`, e.g. `VAR`, `VAR:~0,3`, `VAR:old=new`, `VAR:*old=new`)
;; @out x0 = expanded string in arena (or 0 if undefined)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Evaluates variable lookup plus `:~start[,len]` slicing and `:old=new` / `:*old=new` substitution.
@proc expand_var_op_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    mov x20, #0
.find_col:
    ldrb w0, [x19, x20]
    cbz w0, .plain_lookup
    cmp w0, #0x3A
    b.eq .has_col
    add x20, x20, #1
    b .find_col

.plain_lookup:
    mov x0, x19
    bl env_get_a64
    b .exit

.has_col:
    cbz x20, .plain_lookup ; `=ExitCode` starts with `=`, not `:`
    add x0, x20, #2
    bl arena_alloc_a64
    cbz x0, .ret_z
    mov x21, x0
    mov x1, x19
    mov x2, x20
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x21
    bl env_get_a64
    cbz x0, .ret_z
    mov x21, x0 ; value string
    bl str_len_a64
    mov x22, x0 ; vlen

    add x23, x19, x20
    add x23, x23, #1 ; spec after ':'
    ldrb w0, [x23, #0]
    cmp w0, #0x7E
    b.eq .do_slice

    mov w24, #0 ; star_prefix = 0
    cmp w0, #0x2A
    b.ne .find_eq
    mov w24, #1
    add x23, x23, #1
.find_eq:
    mov x20, #0
.eq_lp:
    ldrb w0, [x23, x20]
    cbz w0, .ret_val
    cmp w0, #0x3D
    b.eq .got_eq
    add x20, x20, #1
    b .eq_lp
.got_eq:
    cbz x20, .ret_val
    add x25, x23, x20
    add x25, x25, #1 ; new_str

    lsl x0, x22, #2
    add x0, x0, #512
    bl arena_alloc_a64
    cbz x0, .ret_val
    mov x19, x0 ; out base
    mov x22, x0 ; out cursor

    cbz w24, .rep_loop
    mov x24, x21
.st_lp:
    ldrb w0, [x24, #0]
    cbz w0, .ret_val
    mov x0, x24
    mov x1, x23
    mov x2, x20
    bl str_ncmp_ci_a64
    cbz w0, .st_hit
    add x24, x24, #1
    b .st_lp
.st_hit:
    mov x0, x19
    mov x1, x25
    bl str_copy_a64
    add x1, x24, x20
    bl str_copy_a64
    mov x0, x19
    b .exit

.rep_loop:
    ldrb w0, [x21, #0]
    cbz w0, .rep_fin
    mov x0, x21
    mov x1, x23
    mov x2, x20
    bl str_ncmp_ci_a64
    cbz w0, .rep_hit
    ldrb w0, [x21, #0]
    strb w0, [x22, #0]
    add x21, x21, #1
    add x22, x22, #1
    b .rep_loop
.rep_hit:
    mov x0, x22
    mov x1, x25
    bl str_copy_a64
    mov x22, x0
    add x21, x21, x20
    b .rep_loop
.rep_fin:
    strb wzr, [x22, #0]
    mov x0, x19
    b .exit

.do_slice:
    add x0, x23, #1
    bl parse_signed_dec_a64
    mov x23, x0 ; start (signed 64)
    mov x24, x22 ; end = vlen
    ldrb w0, [x1, #0]
    cmp w0, #0x2C
    b.ne .norm_bounds
    add x0, x1, #1
    bl parse_signed_dec_a64
    mov x25, x0 ; len
    cmp x23, #0
    b.ge .st_nonneg1
    adds x23, x22, x23
    b.ge .st_nonneg1
    mov x23, #0
.st_nonneg1:
    cmp x23, x22
    b.le .chk_len_sign
    mov x23, x22
.chk_len_sign:
    cmp x25, #0
    b.ge .pos_len
    add x24, x22, x25
    b .clamp_end
.pos_len:
    add x24, x23, x25
    b .clamp_end

.norm_bounds:
    cmp x23, #0
    b.ge .clamp_end
    adds x23, x22, x23
    b.ge .clamp_end
    mov x23, #0

.clamp_end:
    cmp x23, x22
    b.le .chk_end_hi
    mov x23, x22
.chk_end_hi:
    cmp x24, x22
    b.le .chk_end_lo
    mov x24, x22
.chk_end_lo:
    cmp x24, x23
    b.ge .alloc_sl
    mov x24, x23
.alloc_sl:
    sub x20, x24, x23
    add x0, x20, #2
    bl arena_alloc_a64
    cbz x0, .ret_z
    mov x19, x0
    add x1, x21, x23
    mov x2, x20
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x19
    b .exit

.ret_val:
    mov x0, x21
    b .exit
.ret_z:
    mov x0, #0
.exit:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc parse_tilde_mask_a64
;; @abi win_arm64
;; @in x0 = pointer right after `~`
;; @out w0 = modifier bitmask, x1 = pointer to parameter/var char (or 0 if invalid), x2 = $VAR start (if `$VAR:`), x3 = $VAR len
;; @err x1 = 0 if no valid parameter/FOR char follows
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Parses `[fdpnxsatz]*` and optional `$VAR:` modifiers after `%~` or `%%~`.
@proc parse_tilde_mask_a64
    mov x1, x0
    mov w0, #0
    mov x2, #0
    mov x3, #0
.m_lp:
    ldrb w4, [x1, #0]
    cbz w4, .bad
    cmp w4, #0x24 ; '$VAR:'
    b.eq .parse_dollar
    mov w5, w4
    cmp w5, #0x41
    b.lo .done_m
    cmp w5, #0x5A
    b.hi .chk_flag
    add w5, w5, #0x20
.chk_flag:
    cmp w5, #0x66
    b.eq .f_f
    cmp w5, #0x64
    b.eq .f_d
    cmp w5, #0x70
    b.eq .f_p
    cmp w5, #0x6E
    b.eq .f_n
    cmp w5, #0x78
    b.eq .f_x
    cmp w5, #0x73
    b.eq .f_s
    cmp w5, #0x61
    b.eq .f_a
    cmp w5, #0x74
    b.eq .f_t
    cmp w5, #0x7A
    b.eq .f_z
    ret
.f_f:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x01
    add x1, x1, #1
    b .m_lp
.f_d:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x02
    add x1, x1, #1
    b .m_lp
.f_p:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x04
    add x1, x1, #1
    b .m_lp
.f_n:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x08
    add x1, x1, #1
    b .m_lp
.f_x:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x10
    add x1, x1, #1
    b .m_lp
.f_s:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x20
    add x1, x1, #1
    b .m_lp
.f_a:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x40
    add x1, x1, #1
    b .m_lp
.f_t:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x80
    add x1, x1, #1
    b .m_lp
.f_z:
    ldrb w5, [x1, #1]
    cmp w5, #0x21
    b.lo .done_m
    orr w0, w0, #0x100
    add x1, x1, #1
    b .m_lp
.parse_dollar:
    add x2, x1, #1
    mov x3, #0
.dl_lp:
    ldrb w5, [x2, x3]
    cbz w5, .bad
    cmp w5, #0x3A
    b.eq .got_dl_col
    add x3, x3, #1
    b .dl_lp
.got_dl_col:
    cbz x3, .bad
    add x1, x2, x3
    add x1, x1, #1
    ldrb w5, [x1, #0]
    cmp w5, #0x21
    b.lo .bad
.done_m:
    ret
.bad:
    mov x1, #0
    ret
@endproc

;; @proc eval_tilde_val_a64
;; @abi win_arm64
;; @in x0 = raw value, w1 = mask, x2 = dollar_var_ptr (or 0), x3 = dollar_var_len
;; @out x0 = expanded string in arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Combines `$VAR:` path search (when x2 != 0) with `%~[fdpnxsatz]` formatting.
@proc eval_tilde_val_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov w20, w1
    cbz x2, .apply_m
    mov x21, x2
    mov x22, x3
    add x0, x22, #2
    bl arena_alloc_a64
    cbz x0, .apply_m
    mov x4, x0
    stp x4, xzr, [sp, #-16]!
    mov x1, x21
    mov x2, x22
    bl mem_copy_a64
    strb wzr, [x0, #0]
    ldp x0, xzr, [sp], #16
    mov x1, x19
    bl search_env_path_mod_a64
    mov x19, x0
    ldrb w1, [x19, #0]
    cbz w1, .done_raw
.apply_m:
    mov x0, x19
    mov w1, w20
    bl apply_tilde_mods_a64
    b .exit
.done_raw:
    mov x0, x19
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc get_batch_arg_a64
;; @abi win_arm64
;; @in w0 = digit 0..9
;; @out x0 = argument string (or 0 if out of range)
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Looks up `%0`..`%9` on the active top BATCH_FRAME_A64 honoring SHIFT [/n].
@proc get_batch_arg_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov w19, w0
    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .empty
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6 ; * 64 (BATCH_FRAME_A64)
    ldr x2, [x0, #ARM64_CTX.pBatchStack]
    add x2, x2, x1
    ldr w3, [x2, #BATCH_FRAME_A64.dwShiftOff]
    add w19, w19, w3
    ldr w3, [x2, #BATCH_FRAME_A64.dwArgCount]
    cmp w19, w3
    b.hs .empty
    ldr x2, [x2, #BATCH_FRAME_A64.pArgVec]
    uxtw x19, w19
    lsl x19, x19, #3
    ldr x0, [x2, x19]
    b .done
.empty:
    mov x0, #0
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc expand_phase1_a64
;; @abi win_arm64
;; @in x0 = raw logical line, w1 = in_batch (1 = batch file, 0 = interactive/cmdline)
;; @out x0 = Phase-1 `%`-expanded WTF-8 line in arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Performs Phase 1 immediate `%` parameter (`%0`..`%9`, `%*`, `%~`) and `%VAR%` expansion.
@proc expand_phase1_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; src
    mov w20, w1 ; in_batch

    mov_imm32 x0, A64_LINE_MAX
    bl arena_alloc_a64
    cbz x0, .done
    mov x21, x0 ; out base
    mov x22, x0 ; out cursor

.lp:
    ldrb w0, [x19, #0]
    cbz w0, .fin
    cmp w0, #0x25
    b.ne .lit

    ldrb w1, [x19, #1]
    cbz w1, .lit

    cmp w1, #0x25
    b.ne .chk_batch_only
    cbz w20, .emit_two_pct
    mov w0, #0x25
    strb w0, [x22, #0]
    add x22, x22, #1
    add x19, x19, #2
    b .lp
.emit_two_pct:
    mov w0, #0x25
    strb w0, [x22, #0]
    strb w0, [x22, #1]
    add x22, x22, #2
    add x19, x19, #2
    b .lp

.chk_batch_only:
    cbz w20, .try_env_var

    cmp w1, #0x2A
    b.ne .chk_tilde_p1
    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .skip2
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x0, #ARM64_CTX.pBatchStack]
    add x2, x2, x1
    ldr x1, [x2, #BATCH_FRAME_A64.pRawArgs]
    cbz x1, .skip2
    mov x0, x22
    bl str_copy_a64
    mov x22, x0
.skip2:
    add x19, x19, #2
    b .lp

.chk_tilde_p1:
    cmp w1, #0x7E
    b.ne .chk_dig_p1
    add x0, x19, #2
    bl parse_tilde_mask_a64
    cbz x1, .try_env_var
    ldrb w4, [x1, #0]
    cmp w4, #0x30
    b.lo .try_env_var
    cmp w4, #0x39
    b.hi .try_env_var
    mov w23, w0 ; mask
    add x24, x1, #1 ; next src
    mov x25, x2 ; dollar_var_ptr
    mov x26, x3 ; dollar_var_len
    sub w0, w4, #0x30
    bl get_batch_arg_a64
    cbz x0, .adv_x24
    mov w1, w23
    mov x2, x25
    mov x3, x26
    bl eval_tilde_val_a64
    cbz x0, .adv_x24
    mov x1, x0
    mov x0, x22
    bl str_copy_a64
    mov x22, x0
.adv_x24:
    mov x19, x24
    b .lp

.chk_dig_p1:
    cmp w1, #0x30
    b.lo .try_env_var
    cmp w1, #0x39
    b.hi .try_env_var
    sub w0, w1, #0x30
    bl get_batch_arg_a64
    cbz x0, .skip2
    mov x1, x0
    mov x0, x22
    bl str_copy_a64
    mov x22, x0
    add x19, x19, #2
    b .lp

.try_env_var:
    add x23, x19, #1
    mov x24, #0
.sc_pct:
    ldrb w0, [x23, x24]
    cbz w0, .no_close_pct
    cmp w0, #0x25
    b.eq .got_close_pct
    add x24, x24, #1
    b .sc_pct

.no_close_pct:
    cbnz w20, .drop_lone_pct
    b .lit
.drop_lone_pct:
    add x19, x19, #1
    b .lp

.got_close_pct:
    cbz x24, .lit
    add x0, x24, #2
    bl arena_alloc_a64
    cbz x0, .lit
    mov x25, x0
    mov x1, x23
    mov x2, x24
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x25
    bl expand_var_op_a64
    cbnz x0, .emit_var_val
    cbz w20, .lit
    add x19, x23, x24
    add x19, x19, #1
    b .lp

.emit_var_val:
    mov x1, x0
    mov x0, x22
    bl str_copy_a64
    mov x22, x0
    add x19, x23, x24
    add x19, x19, #1
    b .lp

.lit:
    ldrb w0, [x19, #0]
    strb w0, [x22, #0]
    add x22, x22, #1
    add x19, x19, #1
    b .lp

.fin:
    strb wzr, [x22, #0]
    mov x0, x21
.done:
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc lookup_for_var_a64
;; @abi win_arm64
;; @in w0 = variable character (e.g. 'I')
;; @out x0 = bound value pointer or 0 if not an active FOR variable
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Searches ARM64_CTX.pForStack from innermost to outermost scope for `%V`.
@proc lookup_for_var_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov w19, w0
    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwForDepth]
    ldr x2, [x0, #ARM64_CTX.pForStack]
.f_lp:
    cbz w1, .nf
    sub w1, w1, #1
    uxtw x3, w1
    lsl x3, x3, #4 ; * 16 (FOR_VAR_A64)
    add x3, x2, x3
    ldrb w0, [x3, #FOR_VAR_A64.bActive]
    cbz w0, .f_lp
    ldrb w0, [x3, #FOR_VAR_A64.bVarChar]
    cmp w0, w19
    b.ne .f_lp
    ldr x0, [x3, #FOR_VAR_A64.pValue]
    b .done
.nf:
    mov x0, #0
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc expand_phase34_a64
;; @abi win_arm64
;; @in x0 = token/argument string, w1 = strip_quotes_and_carets (1 = unquote/unescape, 0 = raw)
;; @out x0 = Phase-3 (`%I`/`%~fI`) + Phase-4 (`!VAR!` delayed + caret) expanded string in arena
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Substitutes active FOR variables, expands delayed `!VAR!` when enabled, and strips carets/quotes.
@proc expand_phase34_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    sub sp, sp, #16
    mov x29, sp
    mov x19, x0
    str w1, [sp, #0]

    mov_imm32 x0, A64_LINE_MAX
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0 ; passA base
    mov x21, x0 ; passA cursor

.p3_lp:
    ldrb w0, [x19, #0]
    cbz w0, .p3_done
    cmp w0, #0x25
    b.ne .p3_lit
    ldrb w1, [x19, #1]
    cbz w1, .p3_lit
    cmp w1, #0x7E
    b.ne .p3_direct
    add x0, x19, #2
    bl parse_tilde_mask_a64
    cbz x1, .p3_direct
    mov w23, w0 ; mask
    mov x24, x1 ; ptr to var char
    mov x25, x2 ; dollar_var_ptr
    mov x26, x3 ; dollar_var_len
    ldrb w0, [x24, #0]
    bl lookup_for_var_a64
    cbz x0, .p3_direct
    mov w1, w23
    mov x2, x25
    mov x3, x26
    bl eval_tilde_val_a64
    cbz x0, .p3_adv_mod
    mov x1, x0
    mov x0, x21
    bl str_copy_a64
    mov x21, x0
.p3_adv_mod:
    add x19, x24, #1
    b .p3_lp

.p3_direct:
    ldrb w0, [x19, #1]
    bl lookup_for_var_a64
    cbz x0, .p3_lit
    mov x1, x0
    mov x0, x21
    bl str_copy_a64
    mov x21, x0
    add x19, x19, #2
    b .p3_lp

.p3_lit:
    ldrb w0, [x19, #0]
    strb w0, [x21, #0]
    add x21, x21, #1
    add x19, x19, #1
    b .p3_lp

.p3_done:
    strb wzr, [x21, #0]

    bl hal_get_ctx_a64
    ldrb w23, [x0, #ARM64_CTX.bDelayedExp]

    mov_imm32 x0, A64_LINE_MAX
    bl arena_alloc_a64
    cbz x0, .done
    mov x21, x0 ; passB base
    mov x22, x0 ; passB cursor
    mov x19, x20 ; read from passA
    mov w24, #0 ; in_quotes

.p4_lp:
    ldrb w0, [x19, #0]
    cbz w0, .p4_done

    cmp w0, #0x22
    b.ne .chk_caret4
    eor w24, w24, #1
    ldr w1, [sp, #0]
    cbnz w1, .skip_q4
    strb w0, [x22, #0]
    add x22, x22, #1
.skip_q4:
    add x19, x19, #1
    b .p4_lp

.chk_caret4:
    cmp w0, #0x5E
    b.ne .chk_bang4
    cbnz w24, .chk_caret_in_q
    ldrb w1, [x19, #1]
    cbz w1, .adv4_1
    strb w1, [x22, #0]
    add x22, x22, #1
    add x19, x19, #2
    b .p4_lp
.chk_caret_in_q:
    cbz w23, .p4_lit
    ldrb w1, [x19, #1]
    cmp w1, #0x21
    b.ne .p4_lit
    strb w1, [x22, #0]
    add x22, x22, #1
    add x19, x19, #2
    b .p4_lp

.chk_bang4:
    cbz w23, .p4_lit
    cmp w0, #0x21
    b.ne .p4_lit
    add x25, x19, #1
    mov x26, #0
.sc_bg:
    ldrb w3, [x25, x26]
    cbz w3, .adv4_1
    cmp w3, #0x21
    b.eq .got_bg
    add x26, x26, #1
    b .sc_bg
.got_bg:
    cbz x26, .adv4_1
    add x0, x26, #2
    bl arena_alloc_a64
    cbz x0, .p4_lit
    mov x27, x0
    mov x1, x25
    mov x2, x26
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x27
    bl expand_var_op_a64
    add x19, x25, x26
    add x19, x19, #1
    cbz x0, .p4_lp
    mov x1, x0
    mov x0, x22
    bl str_copy_a64
    mov x22, x0
    b .p4_lp

.adv4_1:
    add x19, x19, #1
    b .p4_lp

.p4_lit:
    ldrb w0, [x19, #0]
    strb w0, [x22, #0]
    add x22, x22, #1
    add x19, x19, #1
    b .p4_lp

.p4_done:
    strb wzr, [x22, #0]
    mov x0, x21
.done:
    add sp, sp, #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc alloc_ast_a64
;; @abi win_arm64
;; @in w0 = AST_* kind byte
;; @out x0 = zeroed AST_NODE_A64*
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Allocates and zeroes a 64-byte AST_NODE_A64 node with bKind = w0.
@proc alloc_ast_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov w19, w0
    mov x0, #64 ; sizeof(AST_NODE_A64)
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov x1, #64
    bl mem_zero_a64
    strb w19, [x20, #AST_NODE_A64.bKind]
    mov x0, x20
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc skip_ws_a64
;; @abi win_arm64
;; @in x0 = string cursor
;; @out x0 = cursor advanced past spaces and tabs
;; @err none
;; @clobbers x0, x1
;; @desc Advances x0 past ASCII space and tab characters.
@proc skip_ws_a64
.lp:
    ldrb w1, [x0, #0]
    cmp w1, #0x20
    b.eq .adv
    cmp w1, #0x09
    b.eq .adv
    ret
.adv:
    add x0, x0, #1
    b .lp
@endproc

;; @proc scan_cmd_token_a64
;; @abi win_arm64
;; @in x0 = cursor
;; @out x0 = arena token string, x1 = advanced cursor
;; @err x0 = 0 on end/delimiter
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Scans one command/argument token respecting `"..."` quotes and `^` escapes, stopping at unquoted whitespace or `&|<>)`.
@proc scan_cmd_token_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    bl skip_ws_a64
    mov x19, x0
    mov x20, x0
    mov w21, #0 ; in_quote
.sc:
    ldrb w0, [x20, #0]
    cbz w0, .end_sc
    cmp w0, #0x22
    b.ne .chk_car
    eor w21, w21, #1
    add x20, x20, #1
    b .sc
.chk_car:
    cbnz w21, .adv_sc
    cmp w0, #0x5E
    b.ne .chk_meta
    ldrb w1, [x20, #1]
    cbz w1, .adv_sc
    add x20, x20, #2
    b .sc
.chk_meta:
    cmp w0, #0x20
    b.eq .end_sc
    cmp w0, #0x09
    b.eq .end_sc
    cmp w0, #0x26
    b.eq .end_sc
    cmp w0, #0x7C
    b.eq .end_sc
    cmp w0, #0x3C
    b.eq .end_sc
    cmp w0, #0x3E
    b.eq .end_sc
    cmp w0, #0x29
    b.eq .end_sc
.adv_sc:
    add x20, x20, #1
    b .sc

.end_sc:
    subs x21, x20, x19
    b.eq .none
    add x0, x21, #2
    bl arena_alloc_a64
    cbz x0, .none
    mov x22, x0
    mov x1, x19
    mov x2, x21
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x22
    mov x1, x20
    b .exit
.none:
    mov x0, #0
    mov x1, x19
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc find_top_op_a64
;; @abi win_arm64
;; @in x0 = command slice, w1 = mode (0 = find `&` / `&&` / `||`, 1 = find single `|`)
;; @out x0 = pointer to operator (or 0 if none), w1 = AST_* kind, x2 = op_len (1 or 2)
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Scans left-to-right outside quotes, `^` escapes, and `(...)` nesting for binary command operators, skipping `REM` lines.
@proc find_top_op_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov w20, w1
    mov w21, #0 ; quote
    mov w22, #0 ; paren depth

    bl skip_ws_a64
    ldrb w1, [x0, #0]
    cmp w1, #0x40
    b.ne .chk_col_rem
    add x0, x0, #1
    bl skip_ws_a64
.chk_col_rem:
    ldrb w1, [x0, #0]
    cmp w1, #0x3A
    b.eq .no_op
    mov x2, x0
    ldrb w0, [x2, #0]
    bl ascii_upper_a64
    cmp w0, #0x52
    b.ne .scan_lp
    ldrb w0, [x2, #1]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .scan_lp
    ldrb w0, [x2, #2]
    bl ascii_upper_a64
    cmp w0, #0x4D
    b.ne .scan_lp
    ldrb w0, [x2, #3]
    cbz w0, .no_op
    cmp w0, #0x20
    b.eq .no_op
    cmp w0, #0x09
    b.eq .no_op

.scan_lp:
    ldrb w0, [x19, #0]
    cbz w0, .no_op
    cmp w0, #0x22
    b.ne .c_car
    eor w21, w21, #1
    add x19, x19, #1
    b .scan_lp
.c_car:
    cbnz w21, .adv_s
    cmp w0, #0x5E
    b.ne .c_par
    ldrb w1, [x19, #1]
    cbz w1, .adv_s
    add x19, x19, #2
    b .scan_lp
.c_par:
    cmp w0, #0x28
    b.ne .c_rpar
    add w22, w22, #1
    add x19, x19, #1
    b .scan_lp
.c_rpar:
    cmp w0, #0x29
    b.ne .c_ops
    cbz w22, .adv_s
    sub w22, w22, #1
    add x19, x19, #1
    b .scan_lp

.c_ops:
    cbnz w22, .adv_s
    cbnz w20, .mode_pipe

    cmp w0, #0x26
    b.ne .chk_or2
    ldrb w1, [x19, #1]
    cmp w1, #0x26
    b.eq .found_and2
    mov x0, x19
    mov w1, #AST_SEQ
    mov x2, #1
    b .exit

.found_and2:
    mov x0, x19
    mov w1, #AST_AND
    mov x2, #2
    b .exit

.chk_or2:
    cmp w0, #0x7C
    b.ne .chk_redir_amp
    ldrb w1, [x19, #1]
    cmp w1, #0x7C
    b.ne .adv_s
    mov x0, x19
    mov w1, #AST_OR
    mov x2, #2
    b .exit

.mode_pipe:
    cmp w0, #0x7C
    b.ne .chk_redir_amp
    ldrb w1, [x19, #1]
    cmp w1, #0x7C
    b.ne .found_pipe1
    add x19, x19, #2
    b .scan_lp
.found_pipe1:
    mov x0, x19
    mov w1, #AST_PIPE
    mov x2, #1
    b .exit

.chk_redir_amp:
    cmp w0, #0x3E
    b.eq .chk_dup_nx
    cmp w0, #0x3C
    b.ne .adv_s
.chk_dup_nx:
    ldrb w1, [x19, #1]
    cmp w1, #0x26
    b.ne .adv_s
    add x19, x19, #2
    b .scan_lp

.adv_s:
    add x19, x19, #1
    b .scan_lp

.no_op:
    mov x0, #0
    mov w1, #0
    mov x2, #0
.exit:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc parse_redirs_and_args_a64
;; @abi win_arm64
;; @in x0 = AST_NODE_A64*, x1 = command text cursor
;; @out Populates pArgv, dwArgc, and pRedir on AST_NODE_A64
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Extracts arbitrary leading/mid/trailing FD 0..9 redirections and argument tokens into AST_NODE_A64.
@proc parse_redirs_and_args_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; node
    mov x20, x1 ; cursor

    mov_imm32 x0, 1024 ; 128 * 8-byte pointers
    bl arena_alloc_a64
    cbz x0, .done
    mov x21, x0 ; argv
    str x21, [x19, #AST_NODE_A64.pArgv]
    mov w22, #0 ; argc
    mov x23, #0 ; last REDIR_NODE_A64*

.tok_loop:
    mov x0, x20
    bl skip_ws_a64
    mov x20, x0
    ldrb w0, [x20, #0]
    cbz w0, .fin_args
    cmp w0, #0x29
    b.eq .fin_args

    cmp w0, #0x30
    b.lo .chk_bare_redir
    cmp w0, #0x39
    b.hi .chk_bare_redir
    ldrb w1, [x20, #1]
    cmp w1, #0x3E
    b.eq .fd_redir_out
    cmp w1, #0x3C
    b.eq .fd_redir_in
    b .regular_tok

.fd_redir_out:
    sub w24, w0, #0x30
    add x20, x20, #1
    b .parse_gt
.fd_redir_in:
    sub w24, w0, #0x30
    add x20, x20, #1
    b .parse_lt

.chk_bare_redir:
    cmp w0, #0x3E
    b.ne .chk_lt
    mov w24, #1
    b .parse_gt
.chk_lt:
    cmp w0, #0x3C
    b.ne .regular_tok
    mov w24, #0
    b .parse_lt

.parse_gt:
    mov w25, #A64_REDIR_OUT
    ldrb w0, [x20, #1]
    cmp w0, #0x3E
    b.ne .chk_dup_out
    mov w25, #A64_REDIR_APP
    add x20, x20, #2
    b .redir_target
.chk_dup_out:
    cmp w0, #0x26
    b.ne .adv_gt1
    ldrb w1, [x20, #2]
    cmp w1, #0x30
    b.lo .adv_gt1
    cmp w1, #0x39
    b.hi .adv_gt1
    mov w25, #A64_REDIR_DUP_OUT
    sub w26, w1, #0x30
    add x20, x20, #3
    b .alloc_dup_node
.adv_gt1:
    add x20, x20, #1
    b .redir_target

.parse_lt:
    mov w25, #A64_REDIR_IN
    ldrb w0, [x20, #1]
    cmp w0, #0x26
    b.ne .adv_lt1
    ldrb w1, [x20, #2]
    cmp w1, #0x30
    b.lo .adv_lt1
    cmp w1, #0x39
    b.hi .adv_lt1
    mov w25, #A64_REDIR_DUP_IN
    sub w26, w1, #0x30
    add x20, x20, #3
    b .alloc_dup_node
.adv_lt1:
    add x20, x20, #1
    b .redir_target

.alloc_dup_node:
    mov x0, #32 ; sizeof(REDIR_NODE_A64)
    bl arena_alloc_a64
    cbz x0, .tok_loop
    mov x2, x0
    strb w25, [x2, #REDIR_NODE_A64.bKind]
    strb w24, [x2, #REDIR_NODE_A64.bSrcFd]
    strb w26, [x2, #REDIR_NODE_A64.bDstFd]
    str xzr, [x2, #REDIR_NODE_A64.pTarget]
    str xzr, [x2, #REDIR_NODE_A64.pNext]
    b .link_redir

.redir_target:
    mov x0, x20
    bl scan_cmd_token_a64
    mov x20, x1
    cbz x0, .tok_loop
    mov x26, x0
    mov x0, #32
    bl arena_alloc_a64
    cbz x0, .tok_loop
    mov x2, x0
    strb w25, [x2, #REDIR_NODE_A64.bKind]
    strb w24, [x2, #REDIR_NODE_A64.bSrcFd]
    strb wzr, [x2, #REDIR_NODE_A64.bDstFd]
    str x26, [x2, #REDIR_NODE_A64.pTarget]
    str xzr, [x2, #REDIR_NODE_A64.pNext]

.link_redir:
    cbnz x23, .app_r
    str x2, [x19, #AST_NODE_A64.pRedir]
    mov x23, x2
    b .tok_loop
.app_r:
    str x2, [x23, #REDIR_NODE_A64.pNext]
    mov x23, x2
    b .tok_loop

.regular_tok:
    cbnz w22, .std_arg
    ldrb w0, [x20, #0]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .std_arg
    ldrb w0, [x20, #1]
    bl ascii_upper_a64
    cmp w0, #0x43
    b.ne .std_arg
    ldrb w0, [x20, #2]
    bl ascii_upper_a64
    cmp w0, #0x48
    b.ne .std_arg
    ldrb w0, [x20, #3]
    bl ascii_upper_a64
    cmp w0, #0x4F
    b.ne .std_arg
    ldrb w0, [x20, #4]
    cmp w0, #0x2E
    b.eq .echo_raw
    cmp w0, #0x28
    b.eq .echo_raw
    cmp w0, #0x3A
    b.eq .echo_raw
    cmp w0, #0x2F
    b.eq .echo_raw
    cmp w0, #0x3D
    b.eq .echo_raw
    cmp w0, #0x2B
    b.eq .echo_raw
    cmp w0, #0x5B
    b.eq .echo_raw
    cmp w0, #0x5D
    b.eq .echo_raw
    cmp w0, #0x3B
    b.eq .echo_raw
    cmp w0, #0x5C
    b.eq .echo_raw
    cmp w0, #0x20
    b.eq .echo_sp
    b .std_arg

.echo_sp:
    add x20, x20, #5
    b .echo_emit
.echo_raw:
    add x20, x20, #5
    ldrb w1, [x19, #AST_NODE_A64.bFlags]
    orr w1, w1, #0x10
    strb w1, [x19, #AST_NODE_A64.bFlags]

.echo_emit:
    mov x0, #8
    bl arena_alloc_a64
    cbz x0, .done
    mov w1, #0x45
    strb w1, [x0, #0]
    mov w1, #0x43
    strb w1, [x0, #1]
    mov w1, #0x48
    strb w1, [x0, #2]
    mov w1, #0x4F
    strb w1, [x0, #3]
    strb wzr, [x0, #4]
    str x0, [x21, #0]
    mov w22, #1

    mov x24, x20 ; msg start
    mov w25, #0 ; quote
.ec_sc:
    ldrb w0, [x20, #0]
    cbz w0, .ec_end
    cmp w0, #0x22
    b.ne .ec_car
    eor w25, w25, #1
    add x20, x20, #1
    b .ec_sc
.ec_car:
    cbnz w25, .ec_adv
    cmp w0, #0x5E
    b.ne .ec_red
    ldrb w1, [x20, #1]
    cbz w1, .ec_adv
    add x20, x20, #2
    b .ec_sc
.ec_red:
    cmp w0, #0x3E
    b.eq .ec_end
    cmp w0, #0x3C
    b.eq .ec_end
    cmp w0, #0x30
    b.lo .ec_adv
    cmp w0, #0x39
    b.hi .ec_adv
    ldrb w1, [x20, #1]
    cmp w1, #0x3E
    b.eq .ec_chk_ws_prev
    cmp w1, #0x3C
    b.eq .ec_chk_ws_prev
    b .ec_adv
.ec_chk_ws_prev:
    cmp x20, x24
    b.eq .ec_end
    sub x2, x20, #1
    ldrb w1, [x2, #0]
    cmp w1, #0x20
    b.eq .ec_end
.ec_adv:
    add x20, x20, #1
    b .ec_sc
.ec_end:
    sub x26, x20, x24
    ldrb w0, [x20, #0]
    cbz w0, .ec_alloc
    cbz x26, .ec_alloc
    sub x1, x20, #1
    ldrb w0, [x1, #0]
    cmp w0, #0x20
    b.ne .ec_alloc
    sub x26, x26, #1
.ec_alloc:
    add x0, x26, #2
    bl arena_alloc_a64
    cbz x0, .tok_loop
    mov x25, x0
    mov x1, x24
    mov x2, x26
    bl mem_copy_a64
    strb wzr, [x0, #0]
    str x25, [x21, #8]
    mov w22, #2
    b .tok_loop

.std_arg:
    mov x0, x20
    bl scan_cmd_token_a64
    mov x20, x1
    cbz x0, .fin_args
    cmp w22, #126
    b.hs .tok_loop
    uxtw x1, w22
    lsl x1, x1, #3
    str x0, [x21, x1]
    add w22, w22, #1
    b .tok_loop

.fin_args:
    str w22, [x19, #AST_NODE_A64.dwArgc]
.done:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc is_else_kw_a64
;; @abi win_arm64
;; @in x0 = cursor
;; @out w0 = 1 if cursor starts with `ELSE` followed by space/`(`/`\0`, else 0
;; @err none
;; @clobbers x0, x1
;; @desc Checks whether a token starts with the `ELSE` keyword.
@proc is_else_kw_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    ldrb w0, [x19, #0]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .no
    ldrb w0, [x19, #1]
    bl ascii_upper_a64
    cmp w0, #0x4C
    b.ne .no
    ldrb w0, [x19, #2]
    bl ascii_upper_a64
    cmp w0, #0x53
    b.ne .no
    ldrb w0, [x19, #3]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .no
    ldrb w0, [x19, #4]
    cbz w0, .yes
    cmp w0, #0x20
    b.eq .yes
    cmp w0, #0x09
    b.eq .yes
    cmp w0, #0x28
    b.eq .yes
.no:
    mov w0, #0
    b .done
.yes:
    mov w0, #1
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ensure_balanced_parens_a64
;; @abi win_arm64
;; @in x0 = command line starting with `(`
;; @out x0 = balanced command line (joining batch lines with `&` until depth == 0)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Reads and Phase-1-expands subsequent batch file lines when a `(` compound block spans multiple physical lines.
@proc ensure_balanced_parens_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0

.recheck:
    mov x1, x19
    mov w2, #0 ; depth
    mov w3, #0 ; quote
.cnt_p:
    ldrb w0, [x1, #0]
    cbz w0, .chk_depth
    cmp w0, #0x22
    b.ne .c_open
    eor w3, w3, #1
    add x1, x1, #1
    b .cnt_p
.c_open:
    cbnz w3, .c_nx
    cmp w0, #0x28
    b.ne .c_close
    add w2, w2, #1
    b .c_nx
.c_close:
    cmp w0, #0x29
    b.ne .c_nx
    cbz w2, .c_nx
    sub w2, w2, #1
.c_nx:
    add x1, x1, #1
    b .cnt_p

.chk_depth:
    cbz w2, .done
    bl hal_get_ctx_a64
    ldr w1, [x0, #ARM64_CTX.dwBatchDepth]
    cbz w1, .done
    sub w1, w1, #1
    uxtw x1, w1
    lsl x1, x1, #6
    ldr x2, [x0, #ARM64_CTX.pBatchStack]
    add x20, x2, x1

    mov_imm32 x0, A64_LINE_MAX
    bl arena_alloc_a64
    cbz x0, .done
    mov x21, x0
    mov x0, x20
    mov x1, x21
    bl batch_read_logical_line_a64
    cbz w0, .done
    mov x0, x21
    mov w1, #1
    bl expand_phase1_a64
    cbz x0, .done
    bl skip_ws_a64
    mov x21, x0
    ldrb w0, [x21, #0]
    cbz w0, .recheck
    cmp w0, #0x3A
    b.eq .recheck

    mov x0, x19
    bl str_len_a64
    mov x22, x0
    mov x0, x21
    bl str_len_a64
    add x0, x0, x22
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .done
    mov x20, x0
    mov x1, x19
    bl str_copy_a64
    mov x22, x0
.trim_back:
    cmp x22, x20
    b.ls .join_sp
    sub x3, x22, #1
    ldrb w1, [x3, #0]
    cmp w1, #0x20
    b.ne .chk_prev_ch
    mov x22, x3
    b .trim_back
.chk_prev_ch:
    cmp w1, #0x28
    b.eq .join_sp
    ldrb w1, [x21, #0]
    cmp w1, #0x29
    b.eq .join_sp
    mov x0, x21
    bl is_else_kw_a64
    cbnz w0, .join_sp
    mov w1, #0x20
    strb w1, [x22, #0]
    mov w1, #0x26
    strb w1, [x22, #1]
    mov w1, #0x20
    strb w1, [x22, #2]
    add x0, x22, #3
    b .do_cat
.join_sp:
    mov w1, #0x20
    strb w1, [x22, #0]
    add x0, x22, #1
.do_cat:
    mov x1, x21
    bl str_copy_a64
    mov x19, x20
    b .recheck

.done:
    mov x0, x19
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc parse_if_ast_a64
;; @abi win_arm64
;; @in x0 = cursor after `IF`, w1 = AST flags
;; @out x0 = AST_IF node
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Parses `IF [/I] [NOT] (ERRORLEVEL n | EXIST p | DEFINED v | CMDEXTVERSION n | a == b | a OP b) body [ELSE alt]`.
@proc parse_if_ast_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; cursor
    mov w20, w1 ; flags

    mov w0, #AST_IF
    bl alloc_ast_a64
    cbz x0, .done
    mov x21, x0 ; IF node

.opt_lp:
    mov x0, x19
    bl scan_cmd_token_a64
    cbz x0, .fin_if
    mov x22, x0 ; tok
    mov x23, x1 ; next cursor

    ldrb w0, [x22, #0]
    cmp w0, #0x2F
    b.ne .chk_not
    ldrb w0, [x22, #1]
    bl ascii_upper_a64
    cmp w0, #0x49
    b.ne .cond_tok
    ldrb w0, [x22, #2]
    cbnz w0, .cond_tok
    orr w20, w20, #AST_FLAG_IF_ICASE
    mov x19, x23
    b .opt_lp

.chk_not:
    mov x0, x22
    bl str_len_a64
    cmp x0, #3
    b.ne .cond_tok
    ldrb w0, [x22, #0]
    bl ascii_upper_a64
    cmp w0, #0x4E
    b.ne .cond_tok
    ldrb w0, [x22, #1]
    bl ascii_upper_a64
    cmp w0, #0x4F
    b.ne .cond_tok
    ldrb w0, [x22, #2]
    bl ascii_upper_a64
    cmp w0, #0x54
    b.ne .cond_tok
    orr w20, w20, #AST_FLAG_IF_NOT
    mov x19, x23
    b .opt_lp

.cond_tok:
    strb w20, [x21, #AST_NODE_A64.bFlags]
    mov x0, x22
    bl str_len_a64
    cmp x0, #5
    b.eq .try_exist
    cmp x0, #7
    b.eq .try_def
    cmp x0, #10
    b.eq .try_errlvl
    cmp x0, #13
    b.eq .try_extver
    b .binary_cmp

.try_exist:
    ldrb w0, [x22, #0]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .binary_cmp
    ldrb w0, [x22, #1]
    bl ascii_upper_a64
    cmp w0, #0x58
    b.ne .binary_cmp
    mov w0, #A64_IF_EXIST
    b .unary_arg
.try_def:
    ldrb w0, [x22, #0]
    bl ascii_upper_a64
    cmp w0, #0x44
    b.ne .binary_cmp
    ldrb w0, [x22, #1]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .binary_cmp
    mov w0, #A64_IF_DEFINED
    b .unary_arg
.try_errlvl:
    ldrb w0, [x22, #0]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .binary_cmp
    ldrb w0, [x22, #1]
    bl ascii_upper_a64
    cmp w0, #0x52
    b.ne .binary_cmp
    mov w0, #A64_IF_ERRORLEVEL
    b .unary_arg
.try_extver:
    ldrb w0, [x22, #0]
    bl ascii_upper_a64
    cmp w0, #0x43
    b.ne .binary_cmp
    mov w0, #A64_IF_CMDEXTVER

.unary_arg:
    strb w0, [x21, #AST_NODE_A64.bSubKind]
    mov x0, x23
    bl scan_cmd_token_a64
    str x0, [x21, #AST_NODE_A64.pStr1]
    mov x19, x1
    b .parse_then_else

.binary_cmp:
    mov x24, #0
.sc_eq2:
    ldrb w0, [x22, x24]
    cbz w0, .sep_op
    cmp w0, #0x3D
    b.ne .nx_eq2
    add x1, x24, #1
    ldrb w2, [x22, x1]
    cmp w2, #0x3D
    b.eq .split_inline_eq
.nx_eq2:
    add x24, x24, #1
    b .sc_eq2

.split_inline_eq:
    add x0, x24, #2
    bl arena_alloc_a64
    cbz x0, .fin_if
    mov x25, x0
    mov x1, x22
    mov x2, x24
    bl mem_copy_a64
    strb wzr, [x0, #0]
    str x25, [x21, #AST_NODE_A64.pStr1]
    mov w0, #A64_IF_CMP_EQ
    strb w0, [x21, #AST_NODE_A64.bSubKind]
    add x0, x22, x24
    add x0, x0, #2
    ldrb w1, [x0, #0]
    cbz w1, .read_rhs_tok
    bl arena_strdup_a64
    str x0, [x21, #AST_NODE_A64.pExtra]
    mov x19, x23
    b .parse_then_else
.read_rhs_tok:
    mov x0, x23
    bl scan_cmd_token_a64
    str x0, [x21, #AST_NODE_A64.pExtra]
    mov x19, x1
    b .parse_then_else

.sep_op:
    str x22, [x21, #AST_NODE_A64.pStr1]
    mov x0, x23
    bl scan_cmd_token_a64
    mov x24, x0
    mov x23, x1
    cbz x24, .fin_if
    mov w25, #A64_IF_CMP_EQ
    ldrb w0, [x24, #0]
    cmp w0, #0x3D
    b.eq .got_op_sub
    bl ascii_upper_a64
    mov w26, w0
    ldrb w0, [x24, #1]
    bl ascii_upper_a64
    mov w3, w0
    cmp w26, #0x4E
    b.ne .c_l
    mov w25, #A64_IF_CMP_NEQ
    b .got_op_sub
.c_l:
    cmp w26, #0x4C
    b.ne .c_g
    mov w25, #A64_IF_CMP_LSS
    cmp w3, #0x45
    b.ne .got_op_sub
    mov w25, #A64_IF_CMP_LEQ
    b .got_op_sub
.c_g:
    cmp w26, #0x47
    b.ne .got_op_sub
    mov w25, #A64_IF_CMP_GTR
    cmp w3, #0x45
    b.ne .got_op_sub
    mov w25, #A64_IF_CMP_GEQ

.got_op_sub:
    strb w25, [x21, #AST_NODE_A64.bSubKind]
    mov x0, x23
    bl scan_cmd_token_a64
    str x0, [x21, #AST_NODE_A64.pExtra]
    mov x19, x1

.parse_then_else:
    mov x0, x19
    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x28
    b.ne .find_else
    mov x0, x19
    bl ensure_balanced_parens_a64
    mov x19, x0

.find_else:
    mov x22, x19 ; scan ptr
    mov w23, #0 ; quote
    mov w24, #0 ; paren depth
.el_lp:
    ldrb w0, [x22, #0]
    cbz w0, .no_else
    cmp w0, #0x22
    b.ne .el_p
    eor w23, w23, #1
    add x22, x22, #1
    b .el_lp
.el_p:
    cbnz w23, .el_nx
    cmp w0, #0x28
    b.ne .el_rp
    add w24, w24, #1
    add x22, x22, #1
    b .el_lp
.el_rp:
    cmp w0, #0x29
    b.ne .el_kw
    cbz w24, .el_nx
    sub w24, w24, #1
    add x22, x22, #1
    b .el_lp
.el_kw:
    cbnz w24, .el_nx
    cmp w0, #0x20
    b.eq .chk_after_sp
    cmp w0, #0x09
    b.ne .el_nx
.chk_after_sp:
    add x0, x22, #1
    bl skip_ws_a64
    mov x25, x0
    bl is_else_kw_a64
    cbnz w0, .got_else
.el_nx:
    add x22, x22, #1
    b .el_lp

.got_else:
    sub x23, x22, x19
    add x0, x23, #2
    bl arena_alloc_a64
    cbz x0, .fin_if
    mov x24, x0
    mov x1, x19
    mov x2, x23
    bl mem_copy_a64
    strb wzr, [x0, #0]
    mov x0, x24
    bl parse_line_ast_a64
    str x0, [x21, #AST_NODE_A64.pLeft]

    add x0, x25, #4
    bl parse_line_ast_a64
    str x0, [x21, #AST_NODE_A64.pRight]
    b .fin_if

.no_else:
    mov x0, x19
    bl parse_line_ast_a64
    str x0, [x21, #AST_NODE_A64.pLeft]

.fin_if:
    mov x0, x21
.done:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc parse_for_ast_a64
;; @abi win_arm64
;; @in x0 = cursor after `FOR`, w1 = AST flags
;; @out x0 = AST_FOR node
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Parses `FOR [/D | /R [root] | /L | /F ["opts"]] %%V IN (set) DO body`.
@proc parse_for_ast_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    mov w20, w1

    mov w0, #AST_FOR
    bl alloc_ast_a64
    cbz x0, .done
    mov x21, x0
    strb w20, [x21, #AST_NODE_A64.bFlags]
    mov w22, #A64_FOR_PLAIN

.sw_lp:
    mov x0, x19
    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x2F
    b.ne .var_tok
    ldrb w0, [x19, #1]
    bl ascii_upper_a64
    add x19, x19, #2
    cmp w0, #0x44
    b.eq .sw_d
    cmp w0, #0x4C
    b.eq .sw_l
    cmp w0, #0x52
    b.eq .sw_r
    cmp w0, #0x46
    b.eq .sw_f
    b .sw_lp
.sw_d:
    mov w22, #A64_FOR_DIRS
    b .sw_lp
.sw_l:
    mov w22, #A64_FOR_LOOP
    b .sw_lp
.sw_r:
    mov w22, #A64_FOR_REC
    mov x0, x19
    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x25
    b.eq .var_tok
    mov x0, x19
    bl scan_cmd_token_a64
    str x0, [x21, #AST_NODE_A64.pExtra]
    mov x19, x1
    b .var_tok
.sw_f:
    mov w22, #A64_FOR_PARSE
    mov x0, x19
    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x25
    b.eq .var_tok
    mov x0, x19
    bl scan_cmd_token_a64
    str x0, [x21, #AST_NODE_A64.pExtra]
    mov x19, x1

.var_tok:
    strb w22, [x21, #AST_NODE_A64.bSubKind]
    mov x0, x19
    bl scan_cmd_token_a64
    mov x19, x1
    cbz x0, .fin_for
    mov x2, x0
.sk_p:
    ldrb w1, [x2, #0]
    cmp w1, #0x25
    b.ne .got_vch
    add x2, x2, #1
    b .sk_p
.got_vch:
    strb w1, [x21, #AST_NODE_A64.bForVar]

    mov x0, x19
    bl scan_cmd_token_a64
    mov x19, x1
    mov x0, x19
    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x28
    b.ne .fin_for

    mov x0, x19
    bl ensure_balanced_parens_a64
    mov x19, x0
    add x23, x19, #1
    mov x1, x23
    mov w2, #1
    mov w3, #0
.in_sc:
    ldrb w0, [x1, #0]
    cbz w0, .in_end
    cmp w0, #0x22
    b.ne .in_p
    eor w3, w3, #1
    add x1, x1, #1
    b .in_sc
.in_p:
    cbnz w3, .in_nx
    cmp w0, #0x28
    b.ne .in_rp
    add w2, w2, #1
    add x1, x1, #1
    b .in_sc
.in_rp:
    cmp w0, #0x29
    b.ne .in_nx
    subs w2, w2, #1
    b.eq .in_end
.in_nx:
    add x1, x1, #1
    b .in_sc

.in_end:
    mov x24, x1
    sub x25, x24, x23
    add x0, x25, #2
    bl arena_alloc_a64
    cbz x0, .fin_for
    mov x26, x0
    mov x1, x23
    mov x2, x25
    bl mem_copy_a64
    strb wzr, [x0, #0]
    str x26, [x21, #AST_NODE_A64.pStr1]

    ldrb w0, [x24, #0]
    cmp w0, #0x29
    b.ne .fin_for
    add x0, x24, #1
    bl scan_cmd_token_a64
    mov x0, x1
    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cmp w0, #0x28
    b.ne .parse_do_body
    mov x0, x19
    bl ensure_balanced_parens_a64
    mov x19, x0
.parse_do_body:
    mov x0, x19
    bl parse_line_ast_a64
    str x0, [x21, #AST_NODE_A64.pLeft]

.fin_for:
    mov x0, x21
.done:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc parse_line_ast_a64
;; @abi win_arm64
;; @in x0 = Phase-1 expanded WTF-8 command string
;; @out x0 = AST_NODE_A64* (or 0 on blank/comment/label)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Parses compound operators (`&`, `&&`, `||`, `|`), `( ... )` blocks, `IF`..`ELSE`, `FOR`, and simple commands into Arena AST.
@proc parse_line_ast_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    mov x29, sp

    bl skip_ws_a64
    mov x19, x0
    ldrb w0, [x19, #0]
    cbz w0, .ret_null

    mov x0, x19
    mov w1, #0
    bl find_top_op_a64
    cbnz x0, .split_bin

    mov x0, x19
    mov w1, #1
    bl find_top_op_a64
    cbz x0, .parse_unary

.split_bin:
    mov x20, x0 ; op ptr
    mov w21, w1 ; AST_* kind
    mov x22, x2 ; op len

    sub x23, x20, x19
    add x0, x23, #2
    bl arena_alloc_a64
    cbz x0, .ret_null
    mov x24, x0
    mov x1, x19
    mov x2, x23
    bl mem_copy_a64
    strb wzr, [x0, #0]

    mov w0, w21
    bl alloc_ast_a64
    cbz x0, .ret_null
    mov x25, x0
    str x24, [x25, #AST_NODE_A64.pExtra]

    mov x0, x24
    bl parse_line_ast_a64
    str x0, [x25, #AST_NODE_A64.pLeft]

    add x0, x20, x22
    bl arena_strdup_a64
    str x0, [x25, #AST_NODE_A64.pStr1]
    bl parse_line_ast_a64
    str x0, [x25, #AST_NODE_A64.pRight]
    mov x0, x25
    b .exit

.parse_unary:
    mov w20, #0 ; flags
.at_lp:
    ldrb w0, [x19, #0]
    cmp w0, #0x40
    b.ne .after_at
    orr w20, w20, #AST_FLAG_NO_ECHO
    add x19, x19, #1
    mov x0, x19
    bl skip_ws_a64
    mov x19, x0
    b .at_lp

.after_at:
    ldrb w0, [x19, #0]
    cbz w0, .ret_null
    cmp w0, #0x3A
    b.eq .ret_null
    cmp w0, #0x28
    b.eq .parse_block

    mov x0, x19
    bl scan_cmd_token_a64
    cbz x0, .simple_cmd
    mov x21, x0 ; word0
    mov x22, x1 ; after word0

    mov x0, x21
    bl str_len_a64
    cmp x0, #3
    b.ne .chk_kw_if
    ldrb w0, [x21, #0]
    bl ascii_upper_a64
    cmp w0, #0x52
    b.ne .chk_kw_if
    ldrb w0, [x21, #1]
    bl ascii_upper_a64
    cmp w0, #0x45
    b.ne .chk_kw_if
    ldrb w0, [x21, #2]
    bl ascii_upper_a64
    cmp w0, #0x4D
    b.eq .ret_rem_node

.chk_kw_if:
    mov x0, x21
    bl str_len_a64
    cmp x0, #2
    b.ne .chk_kw_for
    ldrb w0, [x21, #0]
    bl ascii_upper_a64
    cmp w0, #0x49
    b.ne .chk_kw_for
    ldrb w0, [x21, #1]
    bl ascii_upper_a64
    cmp w0, #0x46
    b.ne .chk_kw_for
    mov x0, x22
    mov w1, w20
    bl parse_if_ast_a64
    b .exit

.chk_kw_for:
    mov x0, x21
    bl str_len_a64
    cmp x0, #3
    b.ne .simple_cmd
    ldrb w0, [x21, #0]
    bl ascii_upper_a64
    cmp w0, #0x46
    b.ne .simple_cmd
    ldrb w0, [x21, #1]
    bl ascii_upper_a64
    cmp w0, #0x4F
    b.ne .simple_cmd
    ldrb w0, [x21, #2]
    bl ascii_upper_a64
    cmp w0, #0x52
    b.ne .simple_cmd
    mov x0, x22
    mov w1, w20
    bl parse_for_ast_a64
    b .exit

.simple_cmd:
    mov w0, #AST_SIMPLE_CMD
    bl alloc_ast_a64
    cbz x0, .ret_null
    mov x21, x0
    strb w20, [x21, #AST_NODE_A64.bFlags]
    mov x0, x19
    bl arena_strdup_a64
    str x0, [x21, #AST_NODE_A64.pStr1]
    mov x0, x21
    mov x1, x19
    bl parse_redirs_and_args_a64
    mov x0, x21
    b .exit

.ret_rem_node:
    mov w0, #AST_REM
    bl alloc_ast_a64
    cbz x0, .ret_null
    strb w20, [x0, #AST_NODE_A64.bFlags]
    b .exit

.parse_block:
    mov x0, x19
    bl ensure_balanced_parens_a64
    mov x19, x0
    add x1, x19, #1
    mov w2, #1
    mov w3, #0
.p_sc:
    ldrb w0, [x1, #0]
    cbz w0, .p_found
    cmp w0, #0x22
    b.ne .p_c2
    eor w3, w3, #1
    add x1, x1, #1
    b .p_sc
.p_c2:
    cbnz w3, .p_adv
    cmp w0, #0x28
    b.ne .p_c3
    add w2, w2, #1
    add x1, x1, #1
    b .p_sc
.p_c3:
    cmp w0, #0x29
    b.ne .p_adv
    subs w2, w2, #1
    b.eq .p_found
.p_adv:
    add x1, x1, #1
    b .p_sc

.p_found:
    mov x23, x1
    add x24, x19, #1
    sub x21, x23, x24
    add x0, x21, #2
    bl arena_alloc_a64
    cbz x0, .ret_null
    mov x22, x0
    mov x1, x24
    mov x2, x21
    bl mem_copy_a64
    strb wzr, [x0, #0]

    mov w0, #AST_BLOCK
    bl alloc_ast_a64
    cbz x0, .ret_null
    mov x25, x0
    strb w20, [x25, #AST_NODE_A64.bFlags]
    mov x0, x22
    bl parse_line_ast_a64
    str x0, [x25, #AST_NODE_A64.pLeft]

    ldrb w0, [x23, #0]
    cmp w0, #0x29
    b.ne .ret_blk
    mov x0, x25
    add x1, x23, #1
    bl parse_redirs_and_args_a64
.ret_blk:
    mov x0, x25
    b .exit

.ret_null:
    mov x0, #0
.exit:
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
