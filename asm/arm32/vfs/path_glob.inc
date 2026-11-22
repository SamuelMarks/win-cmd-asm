;; @file path_glob.inc
;; @isa armv4
;; @desc ARM32 multi-era path normalizer (WinCE rootless ``, A:..Z:, UNC, `\?` 32K), devices, FILETIME, and O(N*M) DP globber.

@cpu armv4
@format inc

;; @const DEV_NONE
;; @desc Not a reserved DOS/Win32 character device.
@const DEV_NONE = 0

;; @const DEV_NUL
;; @desc Reserved NUL bit-bucket device.
@const DEV_NUL = 1

;; @const DEV_CON
;; @desc Reserved CON / CONIN$ / CONOUT$ console device.
@const DEV_CON = 2

;; @const DEV_OTHER
;; @desc Reserved PRN / AUX / CLOCK$ / COM1..9 / LPT1..9 device.
@const DEV_OTHER = 3

;; @proc check_device_a32
;; @abi arm_ce
;; @in r0 = WTF-8 path string
;; @out r0 = DEV_NONE (0), DEV_NUL (1), DEV_CON (2), or DEV_OTHER (3)
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Matches CON, NUL, PRN, AUX, CLOCK$, CONIN$, CONOUT$, COM1..9, LPT1..9 across bare, path-prefixed, `:`, and `.ext` forms.
@proc check_device_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    cmp r4, #0
    beq .not_dev

    ; Skip verbatim `\?` (never device unless `\.`)
    ldrb r0, [r4, #0]
    cmp r0, #0x5C
    bne .find_base
    ldrb r1, [r4, #1]
    cmp r1, #0x5C
    bne .find_base
    ldrb r2, [r4, #2]
    cmp r2, #0x3F ; '?'
    beq .not_dev

.find_base:
    ; Scan to final path component after last '', '/', or ':' (if drive prefix X:)
    mov r5, r4
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .not_dev
    ldrb r1, [r4, #1]
    cmp r1, #0x3A
    bne .scan_sep
    add r4, r4, #2
    mov r5, r4
.scan_sep:
    ldrb r0, [r4, #0]
    cmp r0, #0
    beq .got_base
    cmp r0, #0x5C
    beq .upd_base
    cmp r0, #0x2F
    beq .upd_base
    add r4, r4, #1
    b .scan_sep
.upd_base:
    add r4, r4, #1
    mov r5, r4
    b .scan_sep

.got_base:
    ; Measure stem length before '.' or ':' or NUL
    mov r6, #0
.stem_lp:
    ldrb r0, [r5, r6]
    cmp r0, #0
    beq .chk_len
    cmp r0, #0x2E ; '.'
    beq .chk_len
    cmp r0, #0x3A ; ':'
    beq .chk_trailing_colon
    add r6, r6, #1
    b .stem_lp
.chk_trailing_colon:
    add r1, r6, #1
    ldrb r0, [r5, r1]
    cmp r0, #0
    bne .not_dev

.chk_len:
    cmp r6, #3
    beq .len3
    cmp r6, #4
    beq .len4
    cmp r6, #6
    beq .len6
    cmp r6, #7
    beq .len7
    b .not_dev

.len3:
    ldrb r0, [r5, #0]
    bl ascii_upper_a32
    mov r1, r0
    ldrb r0, [r5, #1]
    push {r1, r2}
    bl ascii_upper_a32
    mov r3, r0
    pop {r1, r2}
    ldrb r0, [r5, #2]
    push {r1, r3}
    bl ascii_upper_a32
    pop {r1, r3}
    ; r1=c0, r3=c1, r0=c2
    cmp r1, #0x4E ; 'N'
    bne .try_con
    cmp r3, #0x55 ; 'U'
    bne .not_dev
    cmp r0, #0x4C ; 'L'
    bne .not_dev
    mov r0, #DEV_NUL
    pop {r4, r5, r6, lr}
    bx lr
.try_con:
    cmp r1, #0x43 ; 'C'
    bne .try_prn
    cmp r3, #0x4F ; 'O'
    bne .not_dev
    cmp r0, #0x4E ; 'N'
    bne .not_dev
    mov r0, #DEV_CON
    pop {r4, r5, r6, lr}
    bx lr
.try_prn:
    cmp r1, #0x50 ; 'P'
    bne .try_aux
    cmp r3, #0x52 ; 'R'
    bne .not_dev
    cmp r0, #0x4E ; 'N'
    bne .not_dev
    mov r0, #DEV_OTHER
    pop {r4, r5, r6, lr}
    bx lr
.try_aux:
    cmp r1, #0x41 ; 'A'
    bne .not_dev
    cmp r3, #0x55 ; 'U'
    bne .not_dev
    cmp r0, #0x58 ; 'X'
    bne .not_dev
    mov r0, #DEV_OTHER
    pop {r4, r5, r6, lr}
    bx lr

.len4:
    ; COM1..COM9 or LPT1..LPT9
    ldrb r0, [r5, #3]
    cmp r0, #0x31
    bcc .not_dev
    cmp r0, #0x39
    bhi .not_dev
    ldrb r0, [r5, #0]
    bl ascii_upper_a32
    cmp r0, #0x43 ; 'C'
    beq .chk_com
    cmp r0, #0x4C ; 'L'
    beq .chk_lpt
    b .not_dev
.chk_com:
    ldrb r0, [r5, #1]
    bl ascii_upper_a32
    cmp r0, #0x4F ; 'O'
    bne .not_dev
    ldrb r0, [r5, #2]
    bl ascii_upper_a32
    cmp r0, #0x4D ; 'M'
    bne .not_dev
    mov r0, #DEV_OTHER
    pop {r4, r5, r6, lr}
    bx lr
.chk_lpt:
    ldrb r0, [r5, #1]
    bl ascii_upper_a32
    cmp r0, #0x50 ; 'P'
    bne .not_dev
    ldrb r0, [r5, #2]
    bl ascii_upper_a32
    cmp r0, #0x54 ; 'T'
    bne .not_dev
    mov r0, #DEV_OTHER
    pop {r4, r5, r6, lr}
    bx lr

.len6:
    ; CONIN$ or CLOCK$
    ldrb r0, [r5, #5]
    cmp r0, #0x24 ; '$'
    bne .not_dev
    ldrb r0, [r5, #0]
    bl ascii_upper_a32
    cmp r0, #0x43
    bne .not_dev
    ldrb r0, [r5, #1]
    bl ascii_upper_a32
    cmp r0, #0x4F ; CONIN$
    beq .ret_con6
    cmp r0, #0x4C ; CLOCK$
    beq .ret_oth6
    b .not_dev
.ret_con6:
    mov r0, #DEV_CON
    pop {r4, r5, r6, lr}
    bx lr
.ret_oth6:
    mov r0, #DEV_OTHER
    pop {r4, r5, r6, lr}
    bx lr

.len7:
    ; CONOUT$
    ldrb r0, [r5, #6]
    cmp r0, #0x24
    bne .not_dev
    ldrb r0, [r5, #0]
    bl ascii_upper_a32
    cmp r0, #0x43
    bne .not_dev
    ldrb r0, [r5, #3]
    bl ascii_upper_a32
    cmp r0, #0x4F
    bne .not_dev
    mov r0, #DEV_CON
    pop {r4, r5, r6, lr}
    bx lr

.not_dev:
    mov r0, #DEV_NONE
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc set_drv_cwd_a32
;; @abi arm_ce
;; @in r0 = normalized WTF-8 CWD path
;; @out Updates ARM32_CTX.szCwd and per-drive drvCwdsPtrs[drv]
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Updates current directory and caches per-drive `=X:` directory entry.
@proc set_drv_cwd_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    bl hal_get_ctx
    mov r5, r0
    add r0, r5, #ARM32_CTX.szCwd
    mov r1, r4
    bl str_copy_a32

    ldrb r0, [r4, #0]
    bl ascii_upper_a32
    cmp r0, #0x41
    bcc .done
    cmp r0, #0x5A
    bhi .done
    ldrb r1, [r4, #1]
    cmp r1, #0x3A
    bne .done
    sub r6, r0, #0x41 ; drive index 0..25
    mov_imm32 r1, ARM32_CTX.drvCwdsPtrs
    add r1, r5, r1
    lsl r2, r6, #2
    ldr r0, [r1, r2]
    cmp r0, #0
    bne .have_slot
    mov_imm32 r0, 512
    push {r1, r2}
    bl perm_alloc_a32
    pop {r1, r2}
    cmp r0, #0
    beq .done
    str r0, [r1, r2]
.have_slot:
    mov r1, r4
    bl str_copy_a32
.done:
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc norm_path_a32
;; @abi arm_ce
;; @in r0 = raw WTF-8 path
;; @out r0 = normalized WTF-8 path in arena (supporting WinCE ``, A:..Z:, UNC, and `\?` verbatim)
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Resolves relative/drive-relative/root-relative paths, collapses `.` & `..`, and strips Win32 trailing dots/spaces unless `\?`.
@proc norm_path_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    mov r4, r0 ; raw input
    bl str_len_a32
    mov_imm32 r1, 1024
    add r0, r0, r1
    bl arena_alloc_a32
    cmp r0, #0
    beq .oom
    mov r5, r0 ; work buffer

    ; Check verbatim `\?` prefix -> copy with `/` preserved or ``? `\?` preserves verbatim!
    ldrb r0, [r4, #0]
    cmp r0, #0x5C
    bne .not_verb
    ldrb r1, [r4, #1]
    cmp r1, #0x5C
    bne .not_verb
    ldrb r2, [r4, #2]
    cmp r2, #0x3F
    bne .not_verb
    ldrb r3, [r4, #3]
    cmp r3, #0x5C
    bne .not_verb
    mov r0, r5
    mov r1, r4
    bl str_copy_a32
    mov r0, r5
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.not_verb:
    bl hal_get_ctx
    mov r6, r0
    ldrb r7, [r6, #ARM32_CTX.bIsWinCe]

    ; Convert `/` -> `` into temp raw copy first
    mov r0, r4
    bl str_len_a32
    add r0, r0, #8
    bl arena_alloc_a32
    cmp r0, #0
    beq .oom
    mov r8, r0 ; slash-fixed input
    mov r1, r4
    mov r2, r8
.sl_lp:
    ldrb r3, [r1, #0]
    cmp r3, #0x2F
    bne .sl_st
    mov r3, #0x5C
.sl_st:
    strb r3, [r2, #0]
    cmp r3, #0
    beq .sl_done
    add r1, r1, #1
    add r2, r2, #1
    b .sl_lp

.sl_done:
    ; Determine root prefix & joined path into r5
    ldrb r0, [r8, #0]
    cmp r0, #0x5C
    bne .chk_drv
    ldrb r1, [r8, #1]
    cmp r1, #0x5C
    beq .is_unc
    ; Root-relative `\foo`: on WinCE root is ``; on WinNT prepend active drive `C:` if szCwd has `X:`
    cmp r7, #0
    bne .ce_abs
    add r1, r6, #ARM32_CTX.szCwd
    ldrb r2, [r1, #1]
    cmp r2, #0x3A
    bne .ce_abs
    ldrb r0, [r1, #0]
    bl ascii_upper_a32
    strb r0, [r5, #0]
    mov r0, #0x3A
    strb r0, [r5, #1]
    add r0, r5, #2
    mov r1, r8
    bl str_copy_a32
    mov r9, #3 ; root floor = 3 (`X:`)
    b .collapse

.ce_abs:
    mov r0, r5
    mov r1, r8
    bl str_copy_a32
    mov r9, #1 ; root floor = 1 (``)
    b .collapse

.is_unc:
    mov r0, r5
    mov r1, r8
    bl str_copy_a32
    ; Compute UNC `\server\share` floor
    mov r9, #2
.unc_srv:
    ldrb r0, [r5, r9]
    cmp r0, #0
    beq .collapse
    cmp r0, #0x5C
    beq .unc_shr_start
    add r9, r9, #1
    b .unc_srv
.unc_shr_start:
    add r9, r9, #1
.unc_shr:
    ldrb r0, [r5, r9]
    cmp r0, #0
    beq .collapse
    cmp r0, #0x5C
    beq .unc_fl_ok
    add r9, r9, #1
    b .unc_shr
.unc_fl_ok:
    add r9, r9, #1
    b .collapse

.chk_drv:
    ldrb r1, [r8, #1]
    cmp r1, #0x3A
    bne .rel_cwd
    ; Has `X:` prefix! On WinCE, map `C:\foo` seamlessly onto root `\foo`!
    cmp r7, #0
    beq .nt_drv
    ldrb r2, [r8, #2]
    cmp r2, #0x5C
    beq .ce_strip_drv
    mov r0, #0x5C
    strb r0, [r5, #0]
    add r0, r5, #1
    add r1, r8, #2
    bl str_copy_a32
    mov r9, #1
    b .collapse
.ce_strip_drv:
    mov r0, r5
    add r1, r8, #2
    bl str_copy_a32
    mov r9, #1
    b .collapse

.nt_drv:
    ldrb r0, [r8, #0]
    bl ascii_upper_a32
    mov r10, r0
    ldrb r2, [r8, #2]
    cmp r2, #0x5C
    beq .nt_drv_abs
    ; Drive-relative `X:sub`: lookup drvCwdsPtrs[X-'A'] or default `X:`
    sub r1, r10, #0x41
    cmp r1, #26
    bhs .nt_drv_abs
    mov_imm32 r2, ARM32_CTX.drvCwdsPtrs
    add r2, r6, r2
    lsl r1, r1, #2
    ldr r1, [r2, r1]
    cmp r1, #0
    beq .nt_drv_root_def
    mov r0, r5
    bl str_copy_a32
    b .app_drv_rel
.nt_drv_root_def:
    strb r10, [r5, #0]
    mov r0, #0x3A
    strb r0, [r5, #1]
    mov r0, #0x5C
    strb r0, [r5, #2]
    add r0, r5, #3
    mov r1, #0
    strb r1, [r0, #0]
.app_drv_rel:
    ldrb r1, [r8, #2]
    cmp r1, #0
    beq .set_fl3
    sub r2, r0, #1
    ldrb r1, [r2, #0]
    cmp r1, #0x5C
    beq .cp_dr_tail
    mov r1, #0x5C
    strb r1, [r0, #0]
    add r0, r0, #1
.cp_dr_tail:
    add r1, r8, #2
    bl str_copy_a32
.set_fl3:
    mov r9, #3
    b .collapse

.nt_drv_abs:
    strb r10, [r5, #0]
    mov r0, #0x3A
    strb r0, [r5, #1]
    add r0, r5, #2
    add r1, r8, #2
    bl str_copy_a32
    mov r9, #3
    b .collapse

.rel_cwd:
    add r1, r6, #ARM32_CTX.szCwd
    mov r0, r5
    bl str_copy_a32
    ldrb r2, [r8, #0]
    cmp r2, #0
    beq .det_floor
    sub r2, r0, #1
    ldrb r1, [r2, #0]
    cmp r1, #0x5C
    beq .app_rel2
    mov r1, #0x5C
    strb r1, [r0, #0]
    add r0, r0, #1
.app_rel2:
    mov r1, r8
    bl str_copy_a32
.det_floor:
    mov r9, #1
    ldrb r0, [r5, #1]
    cmp r0, #0x3A
    bne .collapse
    mov r9, #3

.collapse:
    ; In-place component canonicalizer on r5 with root floor r9 (``, `X:`, or `\s\sh`)
    add r0, r5, r9 ; read cursor
    add r1, r5, r9 ; write cursor

.comp_lp:
    ldrb r2, [r0, #0]
    cmp r2, #0
    beq .comp_fin
    cmp r2, #0x5C
    bne .comp_start
    add r0, r0, #1
    b .comp_lp

.comp_start:
    mov r3, r0 ; comp begin
.comp_scan:
    ldrb r2, [r0, #0]
    cmp r2, #0
    beq .comp_end
    cmp r2, #0x5C
    beq .comp_end
    add r0, r0, #1
    b .comp_scan

.comp_end:
    sub r4, r0, r3 ; comp len
    ; Check `.`
    cmp r4, #1
    bne .chk_dotdot
    ldrb r2, [r3, #0]
    cmp r2, #0x2E
    beq .comp_lp

.chk_dotdot:
    cmp r4, #2
    bne .emit_comp
    ldrb r2, [r3, #0]
    cmp r2, #0x2E
    bne .emit_comp
    ldrb r2, [r3, #1]
    cmp r2, #0x2E
    bne .emit_comp
    ; Pop previous component back to root floor (r5 + r9)
    add r2, r5, r9
    cmp r1, r2
    bls .comp_lp
.pop_sep:
    cmp r1, r2
    bls .comp_lp
    sub r1, r1, #1
    ldrb r10, [r1, #0]
    cmp r10, #0x5C
    bne .pop_sep
    b .comp_lp

.emit_comp:
    ; Strip Win32 trailing dots and spaces from component
.trim_tail:
    cmp r4, #0
    beq .comp_lp
    sub r2, r4, #1
    ldrb r10, [r3, r2]
    cmp r10, #0x2E
    beq .dec_c
    cmp r10, #0x20
    beq .dec_c
    b .do_emit_c
.dec_c:
    sub r4, r4, #1
    b .trim_tail

.do_emit_c:
    add r2, r5, r9
    cmp r1, r2
    bls .cp_c_bytes
    mov r10, #0x5C
    strb r10, [r1, #0]
    add r1, r1, #1
.cp_c_bytes:
    cmp r4, #0
    beq .comp_lp
    ldrb r10, [r3, #0]
    strb r10, [r1, #0]
    add r3, r3, #1
    add r1, r1, #1
    sub r4, r4, #1
    b .cp_c_bytes

.comp_fin:
    mov r2, #0
    strb r2, [r1, #0]
    mov r0, r5
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.oom:
    mov r0, #0
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc to_u16_longpath_a32
;; @abi arm_ce
;; @in r0 = normalized WTF-8 path
;; @out r0 = UTF-16LE path in arena (auto-promoted to `\?` or `\?\UNC` on ARMNT when >= 248 chars)
;; @err r0 = 0 on OOM
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Converts a normalized WTF-8 path to UTF-16LE, promoting long paths (>= 248 chars) to `\?` on Win10/11.
@proc to_u16_longpath_a32
    push {r4, r5, r6, lr}
    mov r4, r0
    bl hal_get_ctx
    ldrb r5, [r0, #ARM32_CTX.bIsWinCe]
    cmp r5, #0
    bne .direct

    mov r0, r4
    bl str_len_a32
    cmp r0, #248
    bcc .direct

    ; Already `\?`?
    ldrb r1, [r4, #0]
    cmp r1, #0x5C
    bne .chk_drv_lp
    ldrb r1, [r4, #1]
    cmp r1, #0x5C
    bne .direct
    ldrb r1, [r4, #2]
    cmp r1, #0x3F
    beq .direct
    ; UNC `\server\share` -> `\?\UNC\server\share`
    add r0, r0, #16
    bl arena_alloc_a32
    cmp r0, #0
    beq .direct
    mov r6, r0
    mov r1, #0x5C
    strb r1, [r6, #0]
    strb r1, [r6, #1]
    mov r1, #0x3F
    strb r1, [r6, #2]
    mov r1, #0x5C
    strb r1, [r6, #3]
    mov r1, #0x55 ; 'U'
    strb r1, [r6, #4]
    mov r1, #0x4E ; 'N'
    strb r1, [r6, #5]
    mov r1, #0x43 ; 'C'
    strb r1, [r6, #6]
    mov r1, #0x5C
    strb r1, [r6, #7]
    add r0, r6, #8
    add r1, r4, #2
    bl str_copy_a32
    mov r4, r6
    b .direct

.chk_drv_lp:
    ldrb r1, [r4, #1]
    cmp r1, #0x3A
    bne .direct
    add r0, r0, #12
    bl arena_alloc_a32
    cmp r0, #0
    beq .direct
    mov r6, r0
    mov r1, #0x5C
    strb r1, [r6, #0]
    strb r1, [r6, #1]
    mov r1, #0x3F
    strb r1, [r6, #2]
    mov r1, #0x5C
    strb r1, [r6, #3]
    add r0, r6, #4
    mov r1, r4
    bl str_copy_a32
    mov r4, r6

.direct:
    mov r0, r4
    bl wtf8_to_u16_a32
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc matches_glob_a32
;; @abi arm_ce
;; @in r0 = pattern, r1 = candidate filename, r2 = case_sensitive (1 = exact, 0 = case-insensitive)
;; @out r0 = 1 if match, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Zero-backtracking O(N*M) rolling-array DP wildcard engine supporting `*`, `?`, `<`, `>`, `"`.
@proc matches_glob_a32
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #528
    ; sp+0..260 = dp_cur[0..260], sp+264..524 = dp_nxt[0..260]
    mov r4, r0 ; pat
    mov r5, r1 ; str
    mov r6, r2 ; cs

    ; Special `*.*` matches all filenames in cmd.exe / Win32!
    ldrb r0, [r4, #0]
    cmp r0, #0x2A
    bne .calc_plen
    ldrb r0, [r4, #1]
    cmp r0, #0x2E
    bne .calc_plen
    ldrb r0, [r4, #2]
    cmp r0, #0x2A
    bne .calc_plen
    ldrb r0, [r4, #3]
    cmp r0, #0
    beq .match_yes

.calc_plen:
    mov r0, r4
    bl str_len_a32
    cmp r0, #256
    bls .plen_ok
    mov r0, #256
.plen_ok:
    mov r7, r0 ; M = plen

    mov r0, sp
    mov_imm32 r1, 528
    bl mem_zero_a32

    ; dp_cur[0] = 1, propagate leading `*` / `<` / `"`
    mov r0, #1
    strb r0, [sp, #0]
    mov r8, #0
.init_star:
    cmp r8, r7
    bhs .outer_str
    ldrb r0, [r4, r8]
    cmp r0, #0x2A ; '*'
    beq .set_init
    cmp r0, #0x3C ; '<' DOS_STAR
    beq .set_init
    b .outer_str
.set_init:
    add r8, r8, #1
    mov r0, #1
    strb r0, [sp, r8]
    b .init_star

.outer_str:
    ldrb r9, [r5, #0]
    cmp r9, #0
    beq .finish_dp

    ; Zero dp_nxt[0..M+1]
    add r0, sp, #264
    add r1, r7, #2
    bl mem_zero_a32

    mov r8, #0 ; j = 0..M-1
.inner_pat:
    cmp r8, r7
    bhs .swap_rows
    ldrb r0, [sp, r8]
    cmp r0, #0
    beq .next_j

    ldrb r10, [r4, r8]
    cmp r10, #0x2A ; '*'
    beq .on_star
    cmp r10, #0x3C ; '<'
    beq .on_dos_star
    cmp r10, #0x3F ; '?'
    beq .on_one
    cmp r10, #0x3E ; '>'
    beq .on_one
    cmp r10, #0x22 ; '"' DOS_DOT
    beq .on_dos_dot

    ; Literal character match
    mov r0, r10
    cmp r6, #0
    bne .no_fold_p
    bl ascii_upper_a32
.no_fold_p:
    mov r2, r0
    mov r0, r9
    cmp r6, #0
    bne .no_fold_s
    push {r2, r3}
    bl ascii_upper_a32
    pop {r2, r3}
.no_fold_s:
    cmp r2, r0
    bne .next_j

.on_one:
    add r1, sp, #264
    add r2, r8, #1
    mov r0, #1
    strb r0, [r1, r2]
    b .next_j

.on_dos_dot:
    cmp r9, #0x2E
    beq .on_one
    b .next_j

.on_dos_star:
    add r1, sp, #264
    mov r0, #1
    strb r0, [r1, r8]
    add r2, r8, #1
    strb r0, [r1, r2]
    b .next_j

.on_star:
    add r1, sp, #264
    mov r0, #1
    strb r0, [r1, r8]
    add r2, r8, #1
    strb r0, [r1, r2]

.next_j:
    add r8, r8, #1
    b .inner_pat

.swap_rows:
    ; Propagate `*` transitions across dp_nxt!
    mov r8, #0
.prop_lp:
    cmp r8, r7
    bhs .copy_back
    add r1, sp, #264
    ldrb r0, [r1, r8]
    cmp r0, #0
    beq .prop_nx
    ldrb r2, [r4, r8]
    cmp r2, #0x2A
    beq .do_prop
    cmp r2, #0x3C
    beq .do_prop
    cmp r2, #0x22
    bne .prop_nx
    ldrb r3, [r5, #1]
    cmp r3, #0
    bne .prop_nx
.do_prop:
    add r2, r8, #1
    mov r0, #1
    strb r0, [r1, r2]
.prop_nx:
    add r8, r8, #1
    b .prop_lp

.copy_back:
    mov r0, sp
    add r1, sp, #264
    add r2, r7, #2
    bl mem_copy_a32
    add r5, r5, #1
    b .outer_str

.finish_dp:
    ldrb r0, [sp, r7]
    cmp r0, #0
    bne .match_yes
    mov r0, #0
    add sp, sp, #528
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr

.match_yes:
    mov r0, #1
    add sp, sp, #528
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc fmt_2dig_a32
;; @abi arm_ce
;; @in r0 = 0..99, r1 = output pointer
;; @out r0 = r1 + 2
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Writes two zero-padded decimal digits at r1.
@proc fmt_2dig_a32
    push {r4, lr}
    mov r4, r1
    mov r1, #10
    bl math_udiv32
    add r0, r0, #0x30
    add r1, r1, #0x30
    strb r0, [r4, #0]
    strb r1, [r4, #1]
    add r0, r4, #2
    pop {r4, lr}
    bx lr
@endproc

;; @proc fmt_filetime_a32
;; @abi arm_ce
;; @in r0 = ftLo, r1 = ftHi, r2 = output buffer (>= 20 bytes)
;; @out Writes "YYYY-MM-DD  HH:MM" at r2 and returns r2 + 17
;; @err none
;; @clobbers r0, r1, r2, r3, lr
;; @desc Converts 64-bit Win32 FILETIME (100 ns ticks since 1601-01-01) to "YYYY-MM-DD  HH:MM" in pure ARM UAL.
@proc fmt_filetime_a32
    push {r4, r5, r6, r7, r8, lr}
    mov r4, r2
    orrs r3, r0, r1
    beq .def_ft

    ; Approximate (ft >> 30) seconds * 1.073741824 -> exact shift/div:
    ; First shift 64-bit right by 7 (divide by 128 -> 12.8 us units), then divide by 78125 (= 1 second)!
    lsr r0, r0, #7
    lsl r2, r1, #25
    orr r0, r0, r2
    lsr r1, r1, #7
    ; Now divide 64-bit (r1:r0) by 78125 * 60 = 4,687,500 (0x0047868C = 1 minute in 12.8us units!)
    ; Since 2026 FILETIME is ~1.34e17 -> /128 = 1.04e15 -> /4,687,500 = ~223,500,000 (fits in u32!!)
    ; Two-step division: divide by 62500 (0xF424), then by 75!
    mov r5, r0 ; lo
    mov r0, r1 ; hi
    mov_imm32 r1, 62500
    bl math_udiv32
    ; r1 = rem (< 62500), r5 = lo. Combine (r1 << 16) | (r5 >> 16):
    lsr r0, r5, #16
    lsl r1, r1, #16
    orr r0, r0, r1
    mov_imm32 r1, 62500
    bl math_udiv32
    lsl r6, r0, #16
    mov_imm32 r0, 0x0000FFFF
    and r0, r5, r0
    lsl r1, r1, #16
    orr r0, r0, r1
    mov_imm32 r1, 62500
    bl math_udiv32
    orr r0, r6, r0 ; total (ticks / 8,000,000)
    mov r1, #75
    bl math_udiv32 ; r0 = total minutes since 1601-01-01!

    mov_imm32 r1, 1440
    bl math_udiv32
    mov r5, r0 ; days since 1601-01-01
    mov r6, r1 ; minute of day (0..1439)

    ; Civil date from days since 1601-01-01 (Hinnant shifted epoch: days - 134774 + 719468 = days + 584694)
    mov_imm32 r0, 584694
    add r5, r5, r0
    mov r0, r5
    mov_imm32 r1, 146097
    bl math_udiv32
    mov r7, r0 ; era
    mov r8, r1 ; doe (0..146096)

    ; yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365
    mov r0, r8
    mov_imm32 r1, 1460
    bl math_udiv32
    sub r2, r8, r0
    push {r2, r3}
    mov r0, r8
    mov_imm32 r1, 36524
    bl math_udiv32
    pop {r2, r3}
    add r2, r2, r0
    mov_imm32 r1, 146096
    cmp r8, r1
    bcc .no_end_era
    sub r2, r2, #1
.no_end_era:
    mov r0, r2
    mov_imm32 r1, 365
    bl math_udiv32
    mov r2, r0 ; yoe (0..399)
    mov_imm32 r1, 400
    mul r3, r7, r1
    add r7, r3, r2 ; y

    ; doy = doe - (365*yoe + yoe/4 - yoe/100)
    mov_imm32 r1, 365
    mul r3, r2, r1
    lsr r0, r2, #2
    add r3, r3, r0
    push {r2, r3}
    mov r0, r2
    mov r1, #100
    bl math_udiv32
    pop {r2, r3}
    sub r3, r3, r0
    sub r8, r8, r3 ; doy (0..365)

    ; mp = (5*doy + 2) / 153
    mov r1, #5
    mul r0, r8, r1
    add r0, r0, #2
    mov r1, #153
    bl math_udiv32
    mov r5, r0 ; mp (0..11)

    ; d = doy - (153*mp + 2)/5 + 1
    mov r1, #153
    mul r0, r5, r1
    add r0, r0, #2
    mov r1, #5
    bl math_udiv32
    sub r8, r8, r0
    add r8, r8, #1 ; day (1..31)

    cmp r5, #10
    bcc .mp_lo
    sub r5, r5, #9 ; month 1..2
    add r7, r7, #1
    b .fmt_ymd
.mp_lo:
    add r5, r5, #3 ; month 3..12

.fmt_ymd:
    ; Write YYYY-MM-DD  HH:MM
    mov r0, r7
    mov r1, #100
    bl math_udiv32
    mov r7, r1 ; yy
    mov r1, r4
    bl fmt_2dig_a32
    mov r1, r0
    mov r0, r7
    bl fmt_2dig_a32
    mov r2, #0x2D
    strb r2, [r0, #0]
    add r1, r0, #1
    mov r0, r5
    bl fmt_2dig_a32
    mov r2, #0x2D
    strb r2, [r0, #0]
    add r1, r0, #1
    mov r0, r8
    bl fmt_2dig_a32
    mov r2, #0x20
    strb r2, [r0, #0]
    strb r2, [r0, #1]
    add r4, r0, #2

    mov r0, r6
    mov r1, #60
    bl math_udiv32
    mov r6, r1 ; min
    mov r1, r4
    bl fmt_2dig_a32
    mov r2, #0x3A
    strb r2, [r0, #0]
    add r1, r0, #1
    mov r0, r6
    bl fmt_2dig_a32
    mov r2, #0
    strb r2, [r0, #0]
    pop {r4, r5, r6, r7, r8, lr}
    bx lr

.def_ft:
    mov r0, #0x32 ; "2026-01-01  00:00"
    strb r0, [r4, #0]
    mov r0, #0x30
    strb r0, [r4, #1]
    mov r0, #0x32
    strb r0, [r4, #2]
    mov r0, #0x36
    strb r0, [r4, #3]
    mov r0, #0x2D
    strb r0, [r4, #4]
    mov r0, #0x30
    strb r0, [r4, #5]
    mov r0, #0x31
    strb r0, [r4, #6]
    mov r0, #0x2D
    strb r0, [r4, #7]
    mov r0, #0x30
    strb r0, [r4, #8]
    mov r0, #0x31
    strb r0, [r4, #9]
    mov r0, #0x20
    strb r0, [r4, #10]
    strb r0, [r4, #11]
    mov r0, #0x30
    strb r0, [r4, #12]
    strb r0, [r4, #13]
    mov r0, #0x3A
    strb r0, [r4, #14]
    mov r0, #0x30
    strb r0, [r4, #15]
    strb r0, [r4, #16]
    mov r0, #0
    strb r0, [r4, #17]
    add r0, r4, #17
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc
