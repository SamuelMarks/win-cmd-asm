;; @file env_stk.inc
;; @isa 8086
;; @desc Phase 15 SETLOCAL/ENDLOCAL (32 levels + batch auto-unwind) and PUSHD/POPD directory stack.

@cpu 8086
@format inc

;; @proc env_resolve_seg_off
;; @abi dos16
;; @in [wTrnResSeg] -> RES_STATE.wEnvSeg
;; @out AX = environment segment, BX = base byte offset (0 or TRN_ENV_BUF)
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Resolves active 512-byte environment storage segment and offset.
@proc env_resolve_seg_off
    call env_ensure_block
    mov dx, ds
    xor bx, bx
    cmp ax, dx
    jne .ext_seg
    mov bx, TRN_ENV_BUF
.ext_seg:
    ret
@endproc

;; @proc cmd_setlocal
;; @abi dos16
;; @in DS:SI = optional EnableExtensions/DisableExtensions/EnableDelayedExpansion/DisableDelayedExpansion
;; @out Snapshots env + CWD + flags into a 40-para MCB frame (up to 32 deep), [wTrnExitCode]=0/1
;; @err Sets [wTrnExitCode]=1 on depth > 32 or out-of-memory
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements SETLOCAL across nested batch frames with optional extension/delayed-expansion toggles.
@proc cmd_setlocal
    push bp
    push es
    mov bp, sp
    sub sp, 4
    ; [bp-2]=arg_cursor, [bp-4]=frame_seg
    mov word [bp - 2], si

    cmp byte [bTrnDosMajor], 2
    jae .era_ok
    jmp .fail_sl
.era_ok:
    mov es, word [wTrnResSeg]
    cmp byte [es:resState + RES_STATE.bSetlocalCnt], 32
    jb .depth_ok
    jmp .fail_sl
.depth_ok:

    mov bx, 40 ; 640 bytes: 128B metadata + 512B environment snapshot
    mov ah, DOS_ALLOC_MEM
    int 0x21
    jnc .got_mem
    jmp .fail_sl
.got_mem:
    mov word [bp - 4], ax

    ; Populate frame metadata in ES = frame_seg
    mov es, word [wTrnResSeg]
    mov bx, word [es:resState + RES_STATE.wSetlocalTop]
    mov cl, byte [es:resState + RES_STATE.bBatchDepth]
    mov dl, byte [es:resState + RES_STATE.bCmdExt]
    mov dh, byte [es:resState + RES_STATE.bDelayedExp]

    mov es, ax
    mov word [es:0], bx
    mov byte [es:2], cl
    mov byte [es:3], dl
    mov byte [es:4], dh

    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    mov byte [es:5], al
    mov byte [es:6], 0x5C
    mov byte [es:7], 0

    ; Query CWD into TRN_CMD_WORK and copy into frame [7..127]
    mov si, TRN_CMD_WORK
    xor dl, dl
    call k700_getcwd
    mov si, TRN_CMD_WORK
    mov di, 7
    mov cx, 118
.cp_cwd:
    mov al, byte [si]
    mov byte [es:di], al
    test al, al
    jz .snap_env
    inc si
    inc di
    loop .cp_cwd
    mov byte [es:di], 0

.snap_env:
    ; Copy 512 bytes from active environment block into frame [0x0080..0x027F]
    call env_resolve_seg_off
    push ds
    mov ds, ax
    mov si, bx
    mov es, word [bp - 4]
    mov di, 0x0080
    mov cx, 256
    rep movsw
    pop ds

    ; Link frame onto RES_STATE.wSetlocalTop
    mov es, word [wTrnResSeg]
    mov ax, word [bp - 4]
    mov word [es:resState + RES_STATE.wSetlocalTop], ax
    inc byte [es:resState + RES_STATE.bSetlocalCnt]

    ; Parse optional EnableExtensions / DisableExtensions / EnableDelayedExpansion / DisableDelayedExpansion
    mov si, word [bp - 2]
.opt_lp:
    call trn_skip_ws
    test al, al
    jz .sl_ok
    cmp al, 0x0D
    je .sl_ok
    mov di, TRN_CMD_WORK
    call trn_read_arg
    jc .sl_ok

    push si
    mov si, TRN_CMD_WORK
    call prs_strlen_si
    mov di, addr(szSlEnExt)
    mov dx, 16
    call slice_eq_nocase
    pop si
    jnc .set_ext_on

    push si
    mov si, TRN_CMD_WORK
    call prs_strlen_si
    mov di, addr(szSlDisExt)
    mov dx, 17
    call slice_eq_nocase
    pop si
    jnc .set_ext_off

    push si
    mov si, TRN_CMD_WORK
    call prs_strlen_si
    mov di, addr(szSlEnDel)
    mov dx, 22
    call slice_eq_nocase
    pop si
    jnc .set_del_on

    push si
    mov si, TRN_CMD_WORK
    call prs_strlen_si
    mov di, addr(szSlDisDel)
    mov dx, 23
    call slice_eq_nocase
    pop si
    jnc .set_del_off
    jmp .opt_lp

.set_ext_on:
    mov byte [es:resState + RES_STATE.bCmdExt], 1
    jmp .opt_lp
.set_ext_off:
    mov byte [es:resState + RES_STATE.bCmdExt], 0
    jmp .opt_lp
.set_del_on:
    mov byte [es:resState + RES_STATE.bDelayedExp], 1
    jmp .opt_lp
.set_del_off:
    mov byte [es:resState + RES_STATE.bDelayedExp], 0
    jmp .opt_lp

.sl_ok:
    mov word [wTrnExitCode], 0
    add sp, 4
    pop es
    pop bp
    ret

.fail_sl:
    mov ax, ERR_OUT_OF_MEMORY
    call err_print_code
    mov word [wTrnExitCode], 1
    add sp, 4
    pop es
    pop bp
    ret
@endproc

;; @proc env_pop_one_setlocal
;; @abi dos16
;; @in [wTrnResSeg] -> RES_STATE.wSetlocalTop (non-zero)
;; @out Restores environment, flags, drive, and CWD from top SETLOCAL frame and frees its MCB
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Pops and restores one SETLOCAL snapshot frame.
@proc env_pop_one_setlocal
    push bp
    push es
    mov bp, sp
    sub sp, 2

    mov es, word [wTrnResSeg]
    mov ax, word [es:resState + RES_STATE.wSetlocalTop]
    test ax, ax
    jnz .have_sl_top
    jmp .done
.have_sl_top:
    mov word [bp - 2], ax

    ; Read saved header fields from frame segment AX
    mov es, ax
    mov bx, word [es:0] ; wPrevSeg
    mov dl, byte [es:3] ; bSavedExt
    mov dh, byte [es:4] ; bSavedDel
    mov cl, byte [es:5] ; bSavedDrv

    ; Copy saved CWD [6..126] into DS:TRN_CMD_WORK
    mov si, 6
    mov di, TRN_CMD_WORK
.cp_back_cwd:
    mov al, byte [es:si]
    mov byte [di], al
    test al, al
    jz .rest_hdr
    inc si
    inc di
    jmp .cp_back_cwd

.rest_hdr:
    mov es, word [wTrnResSeg]
    mov word [es:resState + RES_STATE.wSetlocalTop], bx
    cmp byte [es:resState + RES_STATE.bSetlocalCnt], 0
    je .rest_flags
    dec byte [es:resState + RES_STATE.bSetlocalCnt]
.rest_flags:
    mov byte [es:resState + RES_STATE.bCmdExt], dl
    mov byte [es:resState + RES_STATE.bDelayedExp], dh

    ; Restore saved drive
    mov dl, cl
    mov ah, DOS_SELECT_DISK
    int 0x21

    ; Restore 512B environment block from frame [0x0080..0x027F]
    call env_resolve_seg_off
    mov es, ax
    mov di, bx
    push ds
    mov ds, word [bp - 2]
    mov si, 0x0080
    mov cx, 256
    rep movsw
    pop ds

    ; Restore saved CWD via LFN/200 CHDIR
    cmp byte [bTrnDosMajor], 7
    jb .chd_200
    mov dx, TRN_CMD_WORK
    stc
    mov ax, DOS_LFN_CHDIR
    int 0x21
    jnc .free_blk
.chd_200:
    mov dx, TRN_CMD_WORK
    call k200_chdir

.free_blk:
    mov es, word [bp - 2]
    mov ah, DOS_FREE_MEM
    int 0x21
    call sync_drive_cwd_env

.done:
    add sp, 2
    pop es
    pop bp
    ret
@endproc

;; @proc cmd_endlocal
;; @abi dos16
;; @in none
;; @out Pops top SETLOCAL frame if present, [wTrnExitCode]=0
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements ENDLOCAL restoring saved environment, CWD, drive, and extension flags.
@proc cmd_endlocal
    call env_pop_one_setlocal
    mov word [wTrnExitCode], 0
    ret
@endproc

;; @proc env_unwind_batch_locals
;; @abi dos16
;; @in [wTrnResSeg] -> RES_STATE.bBatchDepth (already decremented to caller depth!)
;; @out Unwinds all SETLOCAL frames whose owner batch depth exceeds current bBatchDepth
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Automatically unwinds unclosed SETLOCAL scopes when a .BAT script exits or hits EOF.
@proc env_unwind_batch_locals
    push es
.uw_lp:
    mov es, word [wTrnResSeg]
    mov ax, word [es:resState + RES_STATE.wSetlocalTop]
    test ax, ax
    jz .uw_dn
    mov dl, byte [es:resState + RES_STATE.bBatchDepth]
    mov es, ax
    mov dh, byte [es:2] ; owner depth when SETLOCAL ran
    cmp dh, dl
    jbe .uw_dn
    call env_pop_one_setlocal
    jmp .uw_lp
.uw_dn:
    pop es
    ret
@endproc

;; @proc cmd_pushd
;; @abi dos16
;; @in DS:SI = optional target directory path
;; @out Pushes current drive/CWD onto wPushdTop MCB stack and changes directory
;; @err Sets [wTrnExitCode]=1 if target directory does not exist or out of memory
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements PUSHD [path] across MS-DOS 2.00..7.x.
@proc cmd_pushd
    push bp
    push es
    mov bp, sp
    sub sp, 4
    mov word [bp - 2], si

    cmp byte [bTrnDosMajor], 2
    jae .pd_dos_ok
    jmp .pd_fail
.pd_dos_ok:

    call trn_skip_ws
    cmp al, 0x00
    jne .pd_not_z
    jmp .pd_noop_ok
.pd_not_z:
    cmp al, 0x0D
    jne .pd_not_cr
    jmp .pd_noop_ok
.pd_not_cr:
    mov word [bp - 2], si

    mov bx, 10 ; 160 bytes for saved drive + CWD node
    mov ah, DOS_ALLOC_MEM
    int 0x21
    jnc .pd_mem_ok
    jmp .pd_fail
.pd_mem_ok:
    mov word [bp - 4], ax

    mov es, word [wTrnResSeg]
    mov bx, word [es:resState + RES_STATE.wPushdTop]
    mov es, ax
    mov word [es:0], bx

    mov ah, DOS_GET_DEF_DRIVE
    int 0x21
    mov byte [es:2], al
    mov byte [es:3], 0x5C
    mov byte [es:4], 0

    mov si, TRN_CMD_WORK
    xor dl, dl
    call k700_getcwd
    mov si, TRN_CMD_WORK
    mov di, 4
    mov cx, 148
.cp_pcwd:
    mov al, byte [si]
    mov byte [es:di], al
    test al, al
    jz .link_pd
    inc si
    inc di
    loop .cp_pcwd
    mov byte [es:di], 0

.link_pd:
    mov es, word [wTrnResSeg]
    mov ax, word [bp - 4]
    mov word [es:resState + RES_STATE.wPushdTop], ax

    ; Delegate directory change (with implicit /D drive switch support) to cmd_cd_with_d!
    mov si, word [bp - 2]
    mov dl, 1
    call cmd_cd_ex
    cmp word [wTrnExitCode], 0
    je .pd_done

    ; Target CD failed: unlink and free the pushed node without printing a second error!
    mov es, word [bp - 4]
    mov bx, word [es:0]
    mov ah, DOS_FREE_MEM
    int 0x21
    mov es, word [wTrnResSeg]
    mov word [es:resState + RES_STATE.wPushdTop], bx
    jmp .pd_done

.pd_noop_ok:
    mov word [wTrnExitCode], 0
    jmp .pd_done

.pd_fail:
    mov ax, ERR_PATH_NOT_FOUND
    call trn_err_dos
    mov word [wTrnExitCode], 1

.pd_done:
    add sp, 4
    pop es
    pop bp
    ret
@endproc

;; @proc cmd_popd
;; @abi dos16
;; @in none
;; @out Pops top saved directory from wPushdTop and restores drive + CWD
;; @err Sets [wTrnExitCode]=1 when directory stack is empty
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements POPD restoring the most recently pushed drive and working directory.
@proc cmd_popd
    push bp
    push es
    mov bp, sp
    sub sp, 2

    mov es, word [wTrnResSeg]
    mov ax, word [es:resState + RES_STATE.wPushdTop]
    test ax, ax
    jz .empty_stk
    mov word [bp - 2], ax

    mov es, ax
    mov bx, word [es:0]
    mov dl, byte [es:2]
    mov si, 3
    mov di, TRN_CMD_WORK
.cp_pop:
    mov al, byte [es:si]
    mov byte [di], al
    test al, al
    jz .do_pop_cd
    inc si
    inc di
    jmp .cp_pop

.do_pop_cd:
    mov es, word [wTrnResSeg]
    mov word [es:resState + RES_STATE.wPushdTop], bx

    mov ah, DOS_SELECT_DISK
    int 0x21

    cmp byte [bTrnDosMajor], 7
    jb .pop_chd200
    mov dx, TRN_CMD_WORK
    stc
    mov ax, DOS_LFN_CHDIR
    int 0x21
    jnc .free_pop
.pop_chd200:
    mov dx, TRN_CMD_WORK
    call k200_chdir

.free_pop:
    mov es, word [bp - 2]
    mov ah, DOS_FREE_MEM
    int 0x21
    call sync_drive_cwd_env
    mov word [wTrnExitCode], 0
    add sp, 2
    pop es
    pop bp
    ret

.empty_stk:
    mov word [wTrnExitCode], 1
    add sp, 2
    pop es
    pop bp
    ret
@endproc
