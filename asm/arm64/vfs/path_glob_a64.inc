;; @file path_glob_a64.inc
;; @isa arm64
;; @desc 64-bit AArch64 path normalizer (A:..Z:, UNC, verbatim \?\ 32K), reserved devices, FILETIME, and O(N*M) DP globber.

@cpu arm64
@format inc

;; @proc str_ncmp_ci_a64
;; @abi win_arm64
;; @in x0 = s1, x1 = s2, x2 = max byte count
;; @out w0 = 0 if equal for x2 bytes (or up to NUL), signed diff otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Bounded ASCII case-insensitive string comparison.
@proc str_ncmp_ci_a64
    mov x4, x0
    mov x5, x1
.lp:
    cbz x2, .eq
    ldrb w0, [x4, #0]
    ldrb w3, [x5, #0]
    sub w1, w0, #0x61
    cmp w1, #26
    b.hs .u2
    sub w0, w0, #0x20
.u2:
    sub w1, w3, #0x61
    cmp w1, #26
    b.hs .cmp
    sub w3, w3, #0x20
.cmp:
    subs w1, w0, w3
    b.ne .diff
    cbz w0, .eq
    add x4, x4, #1
    add x5, x5, #1
    sub x2, x2, #1
    b .lp
.eq:
    mov w0, #0
    ret
.diff:
    mov w0, w1
    ret
@endproc

;; @proc check_device_a64
;; @abi win_arm64
;; @in x0 = WTF-8 path string
;; @out w0 = DEV_NONE (0), DEV_NUL (1), DEV_CON (2), or DEV_OTHER (3)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6
;; @desc Matches CON, NUL, PRN, AUX, CLOCK$, CONIN$, CONOUT$, COM1..9, LPT1..9 across bare, path-prefixed, `:`, and `.ext` forms.
@proc check_device_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    mov x29, sp
    mov x19, x0
    cbz x19, .not_dev

    ; Verbatim `\?` paths bypass legacy Win32 device interception (`\.` still matches)
    ldrb w0, [x19, #0]
    cmp w0, #0x5C
    b.ne .find_base
    ldrb w1, [x19, #1]
    cmp w1, #0x5C
    b.ne .find_base
    ldrb w2, [x19, #2]
    cmp w2, #0x3F
    b.eq .not_dev

.find_base:
    mov x20, x19
    ldrb w0, [x19, #0]
    cbz w0, .not_dev
    ldrb w1, [x19, #1]
    cmp w1, #0x3A
    b.ne .scan_sep
    add x19, x19, #2
    mov x20, x19
.scan_sep:
    ldrb w0, [x19, #0]
    cbz w0, .got_base
    cmp w0, #0x5C
    b.eq .upd_base
    cmp w0, #0x2F
    b.eq .upd_base
    add x19, x19, #1
    b .scan_sep
.upd_base:
    add x19, x19, #1
    mov x20, x19
    b .scan_sep

.got_base:
    mov x6, #0
.stem_lp:
    ldrb w0, [x20, x6]
    cbz w0, .chk_len
    cmp w0, #0x2E
    b.eq .chk_len
    cmp w0, #0x3A
    b.eq .chk_trailing_colon
    add x6, x6, #1
    b .stem_lp
.chk_trailing_colon:
    add x1, x6, #1
    ldrb w0, [x20, x1]
    cbnz w0, .not_dev

.chk_len:
    cmp x6, #3
    b.eq .len3
    cmp x6, #4
    b.eq .len4
    cmp x6, #6
    b.eq .len6
    cmp x6, #7
    b.eq .len7
    b .not_dev

.len3:
    ldrb w0, [x20, #0]
    bl ascii_upper_a64
    mov w4, w0
    ldrb w0, [x20, #1]
    bl ascii_upper_a64
    mov w5, w0
    ldrb w0, [x20, #2]
    bl ascii_upper_a64
    cmp w4, #0x4E ; 'N'
    b.ne .try_con
    cmp w5, #0x55
    b.ne .not_dev
    cmp w0, #0x4C
    b.ne .not_dev
    mov w0, #DEV_NUL
    b .done
.try_con:
    cmp w4, #0x43 ; 'C'
    b.ne .try_prn
    cmp w5, #0x4F
    b.ne .not_dev
    cmp w0, #0x4E
    b.ne .not_dev
    mov w0, #DEV_CON
    b .done
.try_prn:
    cmp w4, #0x50 ; 'P'
    b.ne .try_aux
    cmp w5, #0x52
    b.ne .not_dev
    cmp w0, #0x4E
    b.ne .not_dev
    mov w0, #DEV_OTHER
    b .done
.try_aux:
    cmp w4, #0x41 ; 'A'
    b.ne .not_dev
    cmp w5, #0x55
    b.ne .not_dev
    cmp w0, #0x58
    b.ne .not_dev
    mov w0, #DEV_OTHER
    b .done

.len4:
    ldrb w0, [x20, #3]
    cmp w0, #0x31
    b.lo .not_dev
    cmp w0, #0x39
    b.hi .not_dev
    ldrb w0, [x20, #0]
    bl ascii_upper_a64
    cmp w0, #0x43 ; 'C'
    b.eq .chk_com
    cmp w0, #0x4C ; 'L'
    b.eq .chk_lpt
    b .not_dev
.chk_com:
    ldrb w0, [x20, #1]
    bl ascii_upper_a64
    cmp w0, #0x4F
    b.ne .not_dev
    ldrb w0, [x20, #2]
    bl ascii_upper_a64
    cmp w0, #0x4D
    b.ne .not_dev
    mov w0, #DEV_OTHER
    b .done
.chk_lpt:
    ldrb w0, [x20, #1]
    bl ascii_upper_a64
    cmp w0, #0x50
    b.ne .not_dev
    ldrb w0, [x20, #2]
    bl ascii_upper_a64
    cmp w0, #0x54
    b.ne .not_dev
    mov w0, #DEV_OTHER
    b .done

.len6:
    ldrb w0, [x20, #5]
    cmp w0, #0x24
    b.ne .not_dev
    ldrb w0, [x20, #0]
    bl ascii_upper_a64
    cmp w0, #0x43
    b.ne .not_dev
    ldrb w0, [x20, #1]
    bl ascii_upper_a64
    cmp w0, #0x4F ; CONIN$
    b.eq .ret_con6
    cmp w0, #0x4C ; CLOCK$
    b.eq .ret_oth6
    b .not_dev
.ret_con6:
    mov w0, #DEV_CON
    b .done
.ret_oth6:
    mov w0, #DEV_OTHER
    b .done

.len7:
    ldrb w0, [x20, #6]
    cmp w0, #0x24
    b.ne .not_dev
    ldrb w0, [x20, #0]
    bl ascii_upper_a64
    cmp w0, #0x43
    b.ne .not_dev
    ldrb w0, [x20, #3]
    bl ascii_upper_a64
    cmp w0, #0x4F
    b.ne .not_dev
    mov w0, #DEV_CON
    b .done

.not_dev:
    mov w0, #DEV_NONE
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc set_drv_cwd_a64
;; @abi win_arm64
;; @in x0 = normalized WTF-8 CWD path
;; @out Updates ARM64_CTX.szCwd and per-drive drvCwdsPtrs[drv]
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Updates active directory and synchronizes hidden `=A:`..`=Z:` per-drive CWD cache.
@proc set_drv_cwd_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x29, sp
    mov x19, x0

    bl hal_get_ctx_a64
    mov x20, x0
    add x0, x20, #ARM64_CTX.szCwd
    mov x1, x19
    bl str_copy_a64

    ldrb w0, [x19, #0]
    bl ascii_upper_a64
    sub w21, w0, #0x41
    cmp w21, #26
    b.hs .done
    ldrb w1, [x19, #1]
    cmp w1, #0x3A
    b.ne .done

    add x22, x20, #ARM64_CTX.drvCwdsPtrs
    uxtw x2, w21
    lsl x2, x2, #3
    ldr x0, [x22, x2]
    cbnz x0, .have_slot
    mov x0, #512
    bl perm_alloc_a64
    cbz x0, .done
    uxtw x2, w21
    lsl x2, x2, #3
    str x0, [x22, x2]
.have_slot:
    mov x1, x19
    bl str_copy_a64
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc norm_path_a64
;; @abi win_arm64
;; @in x0 = raw WTF-8 path
;; @out x0 = normalized WTF-8 path in arena (supporting A:..Z:, UNC, and verbatim \?\ 32K paths)
;; @err x0 = 0 on OOM
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Resolves relative/drive-relative/root-relative paths, collapses `.` & `..`, and strips trailing dots/spaces unless `\?`.
@proc norm_path_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x0 ; raw input

    bl str_len_a64
    add x0, x0, #1024
    bl arena_alloc_a64
    cbz x0, .oom
    mov x20, x0 ; work buffer

    ; Check verbatim `\?` prefix -> preserve verbatim!
    ldrb w0, [x19, #0]
    cmp w0, #0x5C
    b.ne .not_verb
    ldrb w1, [x19, #1]
    cmp w1, #0x5C
    b.ne .not_verb
    ldrb w2, [x19, #2]
    cmp w2, #0x3F
    b.ne .not_verb
    ldrb w3, [x19, #3]
    cmp w3, #0x5C
    b.ne .not_verb
    mov x0, x20
    mov x1, x19
    bl str_copy_a64
    mov x0, x20
    b .exit

.not_verb:
    bl hal_get_ctx_a64
    mov x21, x0

    mov x0, x19
    bl str_len_a64
    add x0, x0, #16
    bl arena_alloc_a64
    cbz x0, .oom
    mov x22, x0 ; slash-fixed input
    mov x1, x19
    mov x2, x22
.sl_lp:
    ldrb w3, [x1, #0]
    cmp w3, #0x2F
    b.ne .sl_st
    mov w3, #0x5C
.sl_st:
    strb w3, [x2, #0]
    cbz w3, .sl_done
    add x1, x1, #1
    add x2, x2, #1
    b .sl_lp

.sl_done:
    ldrb w0, [x22, #0]
    cmp w0, #0x5C
    b.ne .chk_drv
    ldrb w1, [x22, #1]
    cmp w1, #0x5C
    b.eq .is_unc

    ; Root-relative `\foo`: prepend active drive `C:` from szCwd
    add x1, x21, #ARM64_CTX.szCwd
    ldrb w2, [x1, #1]
    cmp w2, #0x3A
    b.ne .bare_abs
    ldrb w0, [x1, #0]
    bl ascii_upper_a64
    strb w0, [x20, #0]
    mov w0, #0x3A
    strb w0, [x20, #1]
    add x0, x20, #2
    mov x1, x22
    bl str_copy_a64
    mov x23, #3 ; root floor = 3 (`X:`)
    b .collapse

.bare_abs:
    mov x0, x20
    mov x1, x22
    bl str_copy_a64
    mov x23, #1
    b .collapse

.is_unc:
    mov x0, x20
    mov x1, x22
    bl str_copy_a64
    mov x23, #2
.unc_srv:
    ldrb w0, [x20, x23]
    cbz w0, .collapse
    cmp w0, #0x5C
    b.eq .unc_shr_start
    add x23, x23, #1
    b .unc_srv
.unc_shr_start:
    add x23, x23, #1
.unc_shr:
    ldrb w0, [x20, x23]
    cbz w0, .collapse
    cmp w0, #0x5C
    b.eq .unc_fl_ok
    add x23, x23, #1
    b .unc_shr
.unc_fl_ok:
    add x23, x23, #1
    b .collapse

.chk_drv:
    ldrb w1, [x22, #1]
    cmp w1, #0x3A
    b.ne .rel_cwd
    ldrb w0, [x22, #0]
    bl ascii_upper_a64
    mov w24, w0
    ldrb w2, [x22, #2]
    cmp w2, #0x5C
    b.eq .nt_drv_abs

    ; Drive-relative `X:sub`: lookup drvCwdsPtrs[X-'A'] or default `X:`
    sub w1, w24, #0x41
    cmp w1, #26
    b.hs .nt_drv_abs
    add x2, x21, #ARM64_CTX.drvCwdsPtrs
    uxtw x1, w1
    lsl x1, x1, #3
    ldr x1, [x2, x1]
    cbz x1, .nt_drv_root_def
    mov x0, x20
    bl str_copy_a64
    b .app_drv_rel
.nt_drv_root_def:
    strb w24, [x20, #0]
    mov w0, #0x3A
    strb w0, [x20, #1]
    mov w0, #0x5C
    strb w0, [x20, #2]
    add x0, x20, #3
    strb wzr, [x0, #0]
.app_drv_rel:
    ldrb w1, [x22, #2]
    cbz w1, .set_fl3
    sub x2, x0, #1
    ldrb w1, [x2, #0]
    cmp w1, #0x5C
    b.eq .cp_dr_tail
    mov w1, #0x5C
    strb w1, [x0, #0]
    add x0, x0, #1
.cp_dr_tail:
    add x1, x22, #2
    bl str_copy_a64
.set_fl3:
    mov x23, #3
    b .collapse

.nt_drv_abs:
    strb w24, [x20, #0]
    mov w0, #0x3A
    strb w0, [x20, #1]
    add x0, x20, #2
    add x1, x22, #2
    bl str_copy_a64
    mov x23, #3
    b .collapse

.rel_cwd:
    add x1, x21, #ARM64_CTX.szCwd
    mov x0, x20
    bl str_copy_a64
    ldrb w2, [x22, #0]
    cbz w2, .det_floor
    sub x2, x0, #1
    ldrb w1, [x2, #0]
    cmp w1, #0x5C
    b.eq .app_rel2
    mov w1, #0x5C
    strb w1, [x0, #0]
    add x0, x0, #1
.app_rel2:
    mov x1, x22
    bl str_copy_a64
.det_floor:
    mov x23, #1
    ldrb w0, [x20, #1]
    cmp w0, #0x3A
    b.ne .collapse
    mov x23, #3

.collapse:
    add x0, x20, x23 ; read cursor
    add x1, x20, x23 ; write cursor

.comp_lp:
    ldrb w2, [x0, #0]
    cbz w2, .comp_fin
    cmp w2, #0x5C
    b.ne .comp_start
    add x0, x0, #1
    b .comp_lp

.comp_start:
    mov x3, x0
.comp_scan:
    ldrb w2, [x0, #0]
    cbz w2, .comp_end
    cmp w2, #0x5C
    b.eq .comp_end
    add x0, x0, #1
    b .comp_scan

.comp_end:
    sub x4, x0, x3
    cmp x4, #1
    b.ne .chk_dotdot
    ldrb w2, [x3, #0]
    cmp w2, #0x2E
    b.eq .comp_lp

.chk_dotdot:
    cmp x4, #2
    b.ne .emit_comp
    ldrb w2, [x3, #0]
    cmp w2, #0x2E
    b.ne .emit_comp
    ldrb w2, [x3, #1]
    cmp w2, #0x2E
    b.ne .emit_comp
    add x2, x20, x23
    cmp x1, x2
    b.ls .comp_lp
.pop_sep:
    cmp x1, x2
    b.ls .comp_lp
    sub x1, x1, #1
    ldrb w5, [x1, #0]
    cmp w5, #0x5C
    b.ne .pop_sep
    b .comp_lp

.emit_comp:
.trim_tail:
    cbz x4, .comp_lp
    sub x2, x4, #1
    ldrb w5, [x3, x2]
    cmp w5, #0x2E
    b.eq .dec_c
    cmp w5, #0x20
    b.eq .dec_c
    b .do_emit_c
.dec_c:
    sub x4, x4, #1
    b .trim_tail

.do_emit_c:
    add x2, x20, x23
    cmp x1, x2
    b.ls .cp_c_bytes
    mov w5, #0x5C
    strb w5, [x1, #0]
    add x1, x1, #1
.cp_c_bytes:
    cbz x4, .comp_lp
    ldrb w5, [x3, #0]
    strb w5, [x1, #0]
    add x3, x3, #1
    add x1, x1, #1
    sub x4, x4, #1
    b .cp_c_bytes

.comp_fin:
    strb wzr, [x1, #0]
    mov x0, x20
    b .exit

.oom:
    mov x0, #0
.exit:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc matches_glob_a64
;; @abi win_arm64
;; @in x0 = pattern, x1 = candidate filename, w2 = case_sensitive (1 = exact, 0 = case-insensitive)
;; @out w0 = 1 if match, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Zero-backtracking O(N*M) rolling-array DP wildcard engine supporting `*`, `?`, `<`, `>`, `"`.
@proc matches_glob_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    sub sp, sp, #544
    mov x29, sp
    mov x19, x0 ; pat
    mov x20, x1 ; str
    mov w21, w2 ; case_sensitive

    ; Special `*.*` matches all filenames in cmd.exe / Win32
    ldrb w0, [x19, #0]
    cmp w0, #0x2A
    b.ne .calc_plen
    ldrb w0, [x19, #1]
    cmp w0, #0x2E
    b.ne .calc_plen
    ldrb w0, [x19, #2]
    cmp w0, #0x2A
    b.ne .calc_plen
    ldrb w0, [x19, #3]
    cbz w0, .match_yes

.calc_plen:
    mov x0, x19
    bl str_len_a64
    cmp x0, #256
    b.ls .plen_ok
    mov x0, #256
.plen_ok:
    mov x22, x0 ; M = plen

    mov x0, sp
    mov x1, #544
    bl mem_zero_a64

    mov w0, #1
    strb w0, [sp, #0]
    mov x23, #0
.init_star:
    cmp x23, x22
    b.hs .outer_str
    ldrb w0, [x19, x23]
    cmp w0, #0x2A
    b.eq .set_init
    cmp w0, #0x3C
    b.eq .set_init
    b .outer_str
.set_init:
    add x23, x23, #1
    mov w0, #1
    strb w0, [sp, x23]
    b .init_star

.outer_str:
    ldrb w24, [x20, #0]
    cbz w24, .finish_dp

    add x0, sp, #272
    add x1, x22, #2
    bl mem_zero_a64

    mov x23, #0 ; j = 0..M-1
.inner_pat:
    cmp x23, x22
    b.hs .swap_rows
    ldrb w0, [sp, x23]
    cbz w0, .next_j

    ldrb w25, [x19, x23]
    cmp w25, #0x2A
    b.eq .on_star
    cmp w25, #0x3C
    b.eq .on_dos_star
    cmp w25, #0x3F
    b.eq .on_one
    cmp w25, #0x3E
    b.eq .on_one
    cmp w25, #0x22
    b.eq .on_dos_dot

    mov w0, w25
    cbnz w21, .no_fold_p
    bl ascii_upper_a64
.no_fold_p:
    mov w26, w0
    mov w0, w24
    cbnz w21, .no_fold_s
    bl ascii_upper_a64
.no_fold_s:
    cmp w26, w0
    b.ne .next_j

.on_one:
    add x1, sp, #272
    add x2, x23, #1
    mov w0, #1
    strb w0, [x1, x2]
    b .next_j

.on_dos_dot:
    cmp w24, #0x2E
    b.eq .on_one
    b .next_j

.on_dos_star:
    add x1, sp, #272
    mov w0, #1
    strb w0, [x1, x23]
    add x2, x23, #1
    strb w0, [x1, x2]
    b .next_j

.on_star:
    add x1, sp, #272
    mov w0, #1
    strb w0, [x1, x23]
    add x2, x23, #1
    strb w0, [x1, x2]

.next_j:
    add x23, x23, #1
    b .inner_pat

.swap_rows:
    mov x23, #0
.prop_lp:
    cmp x23, x22
    b.hs .copy_back
    add x1, sp, #272
    ldrb w0, [x1, x23]
    cbz w0, .prop_nx
    ldrb w2, [x19, x23]
    cmp w2, #0x2A
    b.eq .do_prop
    cmp w2, #0x3C
    b.eq .do_prop
    cmp w2, #0x22
    b.ne .prop_nx
    ldrb w3, [x20, #1]
    cbnz w3, .prop_nx
.do_prop:
    add x2, x23, #1
    mov w0, #1
    strb w0, [x1, x2]
.prop_nx:
    add x23, x23, #1
    b .prop_lp

.copy_back:
    mov x0, sp
    add x1, sp, #272
    add x2, x22, #2
    bl mem_copy_a64
    add x20, x20, #1
    b .outer_str

.finish_dp:
    ldrb w0, [sp, x22]
    cbnz w0, .match_yes
    mov w0, #0
    b .exit
.match_yes:
    mov w0, #1
.exit:
    add sp, sp, #544
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc fmt_2dig_a64
;; @abi win_arm64
;; @in w0 = 0..99, x1 = output pointer
;; @out x0 = x1 + 2
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Writes two zero-padded decimal digits at x1.
@proc fmt_2dig_a64
    mov w2, #10
    udiv w3, w0, w2
    msub w0, w3, w2, w0
    add w3, w3, #0x30
    add w0, w0, #0x30
    strb w3, [x1, #0]
    strb w0, [x1, #1]
    add x0, x1, #2
    ret
@endproc

;; @proc fmt_filetime_a64
;; @abi win_arm64
;; @in x0 = 64-bit Win32 FILETIME, x1 = output buffer (>= 20 bytes)
;; @out Writes "YYYY-MM-DD  HH:MM" at x1 and returns x1 + 17
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7
;; @desc Converts 64-bit Win32 FILETIME (100 ns ticks since 1601-01-01) to "YYYY-MM-DD  HH:MM" via 64-bit AArch64 UDIV.
@proc fmt_filetime_a64
    hint #25
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    mov x29, sp
    mov x19, x1
    cbz x0, .def_ft

    ; Total minutes since 1601-01-01 = ticks / 600,000,000
    mov_imm32 x2, 600000000
    udiv x0, x0, x2

    mov x2, #1440
    udiv x20, x0, x2        ; days since 1601-01-01
    msub x21, x20, x2, x0   ; minute of day (0..1439)

    ; Hinnant civil calendar from days since 1601-01-01 (+ 584694 -> 0000-03-01 epoch)
    mov_imm32 x0, 584694
    add x20, x20, x0
    mov_imm32 x2, 146097
    udiv x22, x20, x2       ; era
    msub x23, x22, x2, x20  ; doe (0..146096)

    mov x2, #1460
    udiv x0, x23, x2
    sub x4, x23, x0
    mov_imm32 x2, 36524
    udiv x0, x23, x2
    add x4, x4, x0
    mov_imm32 x2, 146096
    cmp x23, x2
    b.lo .no_end_era
    sub x4, x4, #1
.no_end_era:
    mov x2, #365
    udiv x4, x4, x2         ; yoe (0..399)
    mov x2, #400
    mul x5, x22, x2
    add x22, x5, x4         ; year

    mov x2, #365
    mul x5, x4, x2
    lsr x0, x4, #2
    add x5, x5, x0
    mov x2, #100
    udiv x0, x4, x2
    sub x5, x5, x0
    sub x23, x23, x5        ; doy (0..365)

    mov x2, #5
    mul x0, x23, x2
    add x0, x0, #2
    mov x2, #153
    udiv x24, x0, x2        ; mp (0..11)

    mov x2, #153
    mul x0, x24, x2
    add x0, x0, #2
    mov x2, #5
    udiv x0, x0, x2
    sub x23, x23, x0
    add x23, x23, #1        ; day (1..31)

    cmp x24, #10
    b.lo .mp_lo
    sub x24, x24, #9        ; month 1..2
    add x22, x22, #1
    b .fmt_ymd
.mp_lo:
    add x24, x24, #3        ; month 3..12

.fmt_ymd:
    mov w2, #100
    udiv w0, w22, w2
    msub w22, w0, w2, w22
    mov x1, x19
    bl fmt_2dig_a64
    mov x1, x0
    mov w0, w22
    bl fmt_2dig_a64
    mov w2, #0x2D
    strb w2, [x0, #0]
    add x1, x0, #1
    mov w0, w24
    bl fmt_2dig_a64
    mov w2, #0x2D
    strb w2, [x0, #0]
    add x1, x0, #1
    mov w0, w23
    bl fmt_2dig_a64
    mov w2, #0x20
    strb w2, [x0, #0]
    strb w2, [x0, #1]
    add x19, x0, #2

    mov w2, #60
    udiv w0, w21, w2
    msub w21, w0, w2, w21
    mov x1, x19
    bl fmt_2dig_a64
    mov w2, #0x3A
    strb w2, [x0, #0]
    add x1, x0, #1
    mov w0, w21
    bl fmt_2dig_a64
    strb wzr, [x0, #0]
    b .exit

.def_ft:
    mov w0, #0x32 ; "2026-01-01  00:00"
    strb w0, [x19, #0]
    mov w0, #0x30
    strb w0, [x19, #1]
    mov w0, #0x32
    strb w0, [x19, #2]
    mov w0, #0x36
    strb w0, [x19, #3]
    mov w0, #0x2D
    strb w0, [x19, #4]
    mov w0, #0x30
    strb w0, [x19, #5]
    mov w0, #0x31
    strb w0, [x19, #6]
    mov w0, #0x2D
    strb w0, [x19, #7]
    mov w0, #0x30
    strb w0, [x19, #8]
    mov w0, #0x31
    strb w0, [x19, #9]
    mov w0, #0x20
    strb w0, [x19, #10]
    strb w0, [x19, #11]
    mov w0, #0x30
    strb w0, [x19, #12]
    strb w0, [x19, #13]
    mov w0, #0x3A
    strb w0, [x19, #14]
    mov w0, #0x30
    strb w0, [x19, #15]
    strb w0, [x19, #16]
    strb wzr, [x19, #17]
    add x0, x19, #17
.exit:
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
