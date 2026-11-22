;; @file w16_exec.inc
;; @isa 8086
;; @desc In-process Win16 shell, .BAT runner, SET /A, WfW 3.11 UNC PUSHD/POPD, TOOLHELP MEM & child exec.

@cpu 8086
@format inc

;; @proc w16_skip_spaces
;; @abi win16
;; @in DS:SI = string pointer
;; @out DS:SI advanced past spaces (0x20) and tabs (0x09)
;; @err none
;; @clobbers ax, si
;; @desc Advances SI to the first non-whitespace byte.
@proc w16_skip_spaces
.lp:
    mov al, byte [si]
    cmp al, 0x20
    je .adv
    cmp al, 0x09
    je .adv
    ret
.adv:
    inc si
    jmp .lp
@endproc

;; @proc w16_match_kw
;; @abi win16
;; @in DS:SI = input command token, DS:DI = uppercase keyword (NUL-terminated)
;; @out CF=0 and SI advanced past keyword + spaces on match; CF=1 and SI preserved otherwise
;; @err CF=1 on mismatch
;; @clobbers ax, di, si
;; @desc Case-insensitive command verb matcher requiring word boundary after verb.
@proc w16_match_kw
    push bx
    mov bx, si
.lp:
    mov ah, byte [di]
    test ah, ah
    jz .chk_end
    mov al, byte [si]
    cmp al, 'a'
    jb .cmp_ch
    cmp al, 'z'
    ja .cmp_ch
    and al, 0xDF
.cmp_ch:
    cmp al, ah
    jne .no_match
    inc si
    inc di
    jmp .lp

.chk_end:
    mov al, byte [si]
    test al, al
    jz .matched
    cmp al, 0x20
    je .matched_ws
    cmp al, 0x09
    je .matched_ws
    cmp al, '.'
    je .matched_dot
    cmp al, '/'
    je .matched
    jmp .no_match

.matched_dot:
    inc si
    jmp .matched
.matched_ws:
    call w16_skip_spaces
.matched:
    pop bx
    clc
    ret
.no_match:
    mov si, bx
    pop bx
    stc
    ret
@endproc

;; @proc w16_fmt_u16
;; @abi win16
;; @in AX = unsigned 16-bit integer, DS:DI = output buffer
;; @out Writes decimal digits and NUL terminator at DS:DI; DI points to NUL
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Formats a 16-bit unsigned integer into decimal ASCII in DGROUP with constant SP.
@proc w16_fmt_u16
    push si
    mov si, (bW16RowRender + 158)
    mov byte [si], 0
    mov bx, 10
.div_lp:
    dec si
    xor dx, dx
    div bx
    add dl, '0'
    mov byte [si], dl
    test ax, ax
    jnz .div_lp
.cp_lp:
    mov al, byte [si]
    test al, al
    jz .done
    mov byte [di], al
    inc si
    inc di
    jmp .cp_lp
.done:
    mov byte [di], 0
    pop si
    ret
@endproc

;; @proc w16_parse_u16
;; @abi win16
;; @in DS:SI = decimal string (optional leading '-')
;; @out AX = signed/unsigned 16-bit value, SI advanced past digits
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Parses a signed 16-bit integer literal for SET /A and EXIT.
@proc w16_parse_u16
    call w16_skip_spaces
    xor cx, cx
    cmp byte [si], '-'
    jne .pos
    mov cx, 1
    inc si
.pos:
    xor ax, ax
.lp:
    mov bl, byte [si]
    cmp bl, '0'
    jb .done_digits
    cmp bl, '9'
    ja .done_digits
    sub bl, '0'
    xor bh, bh
    mov dx, 10
    mul dx
    add ax, bx
    inc si
    jmp .lp
.done_digits:
    test cx, cx
    jz .ret_val
    neg ax
.ret_val:
    ret
@endproc

;; @proc w16_env_set
;; @abi win16
;; @in DS:SI = "NAME=VALUE" string
;; @out Stores or updates NAME=VALUE inside bW16EnvBlock
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Appends/replaces an environment entry inside the 1 KiB DGROUP environment block.
@proc w16_env_set
    ; Find end of existing double-NUL block (simple compact append/overwrite)
    mov di, bW16EnvBlock
.find_end:
    cmp byte [di], 0
    je .at_end
.skip_entry:
    inc di
    cmp byte [di - 1], 0
    jne .skip_entry
    jmp .find_end

.at_end:
    cmp di, (bW16EnvBlock + 960)
    jae .done
.cp_lp:
    mov al, byte [si]
    mov byte [di], al
    inc si
    inc di
    test al, al
    jnz .cp_lp
    mov byte [di], 0
.done:
    ret
@endproc

;; @proc w16_cmd_ver
;; @abi win16
;; @in DS = DGROUP
;; @out Prints "WINCMD16 [Windows X.YY]" (plus "[CP936 DBCS]" on Windows 3.2) to terminal
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Reports exact running 16-bit Windows version from wW16VerWord.
@proc w16_cmd_ver
    mov si, (szVerPrefix - w16_data_base)
    mov di, bW16ScratchA
    call w16_str_copy
    dec di

    mov ax, word [wW16VerWord]
    xor ah, ah
    call w16_fmt_u16
    mov byte [di], '.'
    inc di

    mov ax, word [wW16VerWord]
    mov al, ah
    xor ah, ah
    cmp ax, 10
    jae .fmt_minor
    mov byte [di], '0'
    inc di
.fmt_minor:
    call w16_fmt_u16

    mov byte [di], ']'
    inc di
    mov byte [di], 0

    cmp byte [bW16HasDbcs], 0
    je .emit
    mov si, (szVerDbcsTag - w16_data_base)
    call w16_str_copy

.emit:
    mov si, bW16ScratchA
    call w16_term_writeln
    mov word [wW16ExitCode], 0
    ret
@endproc

;; @proc w16_cmd_set
;; @abi win16
;; @in DS:SI = arguments after SET
;; @out Evaluates SET, SET VAR=VAL, or SET /A VAR=EXPR
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements SET and SET /A arithmetic inside Win16.
@proc w16_cmd_set
    call w16_skip_spaces
    cmp byte [si], 0
    jne .not_list

    mov di, bW16EnvBlock
.list_lp:
    cmp byte [di], 0
    jne .list_item
    jmp .done_ok
.list_item:
    mov si, di
    push di
    call w16_term_writeln
    pop di
.adv_e:
    inc di
    cmp byte [di - 1], 0
    jne .adv_e
    jmp .list_lp

.not_list:
    cmp byte [si], '/'
    je .chk_sa
    jmp .plain_assign
.chk_sa:
    mov al, byte [si + 1]
    and al, 0xDF
    cmp al, 'A'
    je .do_sa
    jmp .plain_assign
.do_sa:
    add si, 2
    call w16_skip_spaces

    ; Copy LHS "NAME=" prefix into bW16ScratchA
    mov di, bW16ScratchA
.lhs_lp:
    mov al, byte [si]
    test al, al
    jnz .lhs_put
    jmp .done_ok
.lhs_put:
    mov byte [di], al
    inc si
    inc di
    cmp al, '='
    jne .lhs_lp

    push di
    call w16_parse_u16
    mov cx, ax
    call w16_skip_spaces
    mov bl, byte [si]
    cmp bl, '+'
    je .op_add
    cmp bl, '-'
    je .op_sub
    cmp bl, '*'
    je .op_mul
    mov ax, cx
    jmp .store_res

.op_add:
    inc si
    push cx
    call w16_parse_u16
    pop cx
    add ax, cx
    jmp .store_res
.op_sub:
    inc si
    push cx
    call w16_parse_u16
    pop cx
    sub cx, ax
    mov ax, cx
    jmp .store_res
.op_mul:
    inc si
    push cx
    call w16_parse_u16
    pop cx
    imul cx

.store_res:
    pop di
    test ax, ax
    jns .fmt_pos
    mov byte [di], '-'
    inc di
    neg ax
.fmt_pos:
    push di
    call w16_fmt_u16
    mov si, bW16ScratchA
    call w16_env_set
    pop si
    call w16_term_writeln
    jmp .done_ok

.plain_assign:
    call w16_env_set
.done_ok:
    mov word [wW16ExitCode], 0
    ret
@endproc

;; @proc w16_cmd_pushd
;; @abi win16
;; @in DS:SI = target directory or \SERVER\SHARE UNC path
;; @out Saves current CWD; maps Z: via WfW 3.11 WNetAddConnection if UNC path
;; @err Sets wW16ExitCode = 1 if UNC unsupported on pre-WfW OS
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements PUSHD with native Windows for Workgroups 3.11 UNC drive mapping.
@proc w16_cmd_pushd
    call w16_skip_spaces
    cmp byte [si], 0
    je .done_ok

    push si
    mov si, bW16CwdBuf
    mov di, bW16PushdPrev
    call w16_str_copy
    pop si
    mov word [wW16PushdDepth], 1

    cmp byte [si], 0x5C
    jne .local_dir
    cmp byte [si + 1], 0x5C
    jne .local_dir

    ; UNC \SERVER\SHARE path -> require WfW 3.11 WNetAddConnection
    cmp word [pfnWNetAdd_p12 + 2], 0
    je .unc_unavail
    push es
    push ds
    push si
    xor ax, ax
    push ax
    push ax
    push ds
    mov ax, (szUncDrvZ - w16_data_base)
    push ax
    call far [pfnWNetAdd_p12]
    pop es
    test ax, ax
    jnz .unc_unavail

    mov byte [bW16PushdUncDrv], 'Z'
    mov si, (szUncRootZ - w16_data_base)
    mov di, bW16CwdBuf
    call w16_str_copy
    jmp .done_ok

.local_dir:
    mov byte [bW16PushdUncDrv], 0
    mov di, bW16CwdBuf
    call w16_str_copy
.done_ok:
    mov word [wW16ExitCode], 0
    ret

.unc_unavail:
    mov word [wW16ExitCode], 1
    ret
@endproc

;; @proc w16_cmd_popd
;; @abi win16
;; @in DS = DGROUP
;; @out Unmaps temporary UNC drive via WNetCancelConnection if active and restores CWD
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements POPD with WfW 3.11 WNetCancelConnection cleanup.
@proc w16_cmd_popd
    cmp word [wW16PushdDepth], 0
    je .done
    cmp byte [bW16PushdUncDrv], 0
    je .restore_cwd
    cmp word [pfnWNetCancel_p6 + 2], 0
    je .clr_unc
    push es
    push ds
    mov ax, (szUncDrvZ - w16_data_base)
    push ax
    mov ax, 1
    push ax
    call far [pfnWNetCancel_p6]
    pop es
.clr_unc:
    mov byte [bW16PushdUncDrv], 0
.restore_cwd:
    mov si, bW16PushdPrev
    mov di, bW16CwdBuf
    call w16_str_copy
    mov word [wW16PushdDepth], 0
.done:
    mov word [wW16ExitCode], 0
    ret
@endproc

;; @proc w16_cmd_mem
;; @abi win16
;; @in DS = DGROUP
;; @out Queries TOOLHELP!MemManInfo, KERNEL!GetFreeSpace & USER!GetFreeSystemResources
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Reports Win16 memory & system resources (TOOLHELP-enhanced on Win 3.1+).
@proc w16_cmd_mem
    push es
    cmp word [pfnGetFreeSpace_p2 + 2], 0
    je .chk_toolhelp
    xor ax, ax
    push ax
    call far [pfnGetFreeSpace_p2]

.chk_toolhelp:
    cmp word [pfnGetSysRes_p2 + 2], 0
    je .probe_th
    xor ax, ax
    push ax
    call far [pfnGetSysRes_p2]

.probe_th:
    cmp word [pfnMemManInfo_p4 + 2], 0
    je .basic_mem

    mov word [bW16MemManBuf], 34
    mov word [bW16MemManBuf + 2], 0
    push ds
    mov ax, bW16MemManBuf
    push ax
    call far [pfnMemManInfo_p4]
    pop es
    mov si, (szMemToolhelp - w16_data_base)
    call w16_term_writeln
    mov word [wW16ExitCode], 0
    ret

.basic_mem:
    pop es
    mov si, (szMemBasic - w16_data_base)
    call w16_term_writeln
    mov word [wW16ExitCode], 0
    ret
@endproc

;; @proc w16_cmd_type
;; @abi win16
;; @in DS:SI = filename in DGROUP
;; @out Streams file contents via OpenFile + _lread + _lclose into GDI terminal
;; @err Sets wW16ExitCode = 1 if file missing
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Implements TYPE using selector-safe Win16 file handles.
@proc w16_cmd_type
    call w16_skip_spaces
    call w16_vfs_open_read
    jc .not_found
    push es
    mov si, ax

.rd_lp:
    push si
    push ds
    mov ax, bW16ScratchA
    push ax
    mov ax, 200
    push ax
    call far [cs:IMP_LREAD_P8]
    cmp ax, 0
    je .close_ok
    cmp ax, 0xFFFF
    je .close_ok
    mov bx, ax
    mov byte [bx + bW16ScratchA], 0
    push si
    mov si, bW16ScratchA
    call w16_term_write_str
    pop si
    jmp .rd_lp

.close_ok:
    push si
    call far [cs:IMP_LCLOSE_P2]
    call w16_term_newline
    pop es
    mov word [wW16ExitCode], 0
    ret

.not_found:
    mov si, (szErrFile - w16_data_base)
    call w16_term_writeln
    mov word [wW16ExitCode], 1
    ret
@endproc

;; @proc w16_cmd_copy
;; @abi win16
;; @in DS:SI = "<src> <dst>"
;; @out Copies src to dst via OpenFile + _lcreat + _lread + _lwrite + _lclose
;; @err Sets wW16ExitCode = 1 if src missing
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Selector-safe in-process COPY built-in for Win16.
@proc w16_cmd_copy
    call w16_skip_spaces
    mov di, bW16ScratchA
.s1_lp:
    mov al, byte [si]
    test al, al
    jz .go_cp_err
    cmp al, 0x20
    je .s1_done
    mov byte [di], al
    inc si
    inc di
    jmp .s1_lp
.go_cp_err:
    jmp .cp_err
.s1_done:
    mov byte [di], 0
    call w16_skip_spaces
    cmp byte [si], 0
    je .go_cp_err

    push si
    mov si, bW16ScratchA
    call w16_vfs_open_read
    pop di
    jc .go_cp_err

    push es
    mov si, ax
    push ds
    push di
    xor ax, ax
    push ax
    call far [cs:IMP_LCREAT_P6]
    cmp ax, 0xFFFF
    je .close_src_err
    mov di, ax

.cp_loop:
    push si
    push ds
    mov ax, bW16ScratchA
    push ax
    mov ax, 200
    push ax
    call far [cs:IMP_LREAD_P8]
    test ax, ax
    jz .cp_done
    cmp ax, 0xFFFF
    je .cp_done

    push di
    push ds
    mov bx, bW16ScratchA
    push bx
    push ax
    call far [cs:IMP_LWRITE_P8]
    jmp .cp_loop

.cp_done:
    push di
    call far [cs:IMP_LCLOSE_P2]
    push si
    call far [cs:IMP_LCLOSE_P2]
    pop es
    mov word [wW16ExitCode], 0
    ret

.close_src_err:
    push si
    call far [cs:IMP_LCLOSE_P2]
    pop es
.cp_err:
    mov word [wW16ExitCode], 1
    ret
@endproc

;; @proc w16_run_internal_com
;; @abi win16
;; @in AX = open HFILE of .COM binary, DS = DGROUP
;; @out Executes 8086 .COM image inside a locked GlobalAlloc selector, capturing INT 21h output
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Selector-safe --internal-dos .COM interpreter capturing stdout into Win16 GDI terminal.
@proc w16_run_internal_com
    push es
    mov di, ax

    ; Allocate 4 KiB virtual PSP+.COM image block via GlobalAlloc
    mov ax, GMEM_MOVEABLE_ZEROINIT
    push ax
    xor ax, ax
    push ax
    mov ax, 0x1000
    push ax
    call far [cs:IMP_GLOBALALLOC_P6]
    test ax, ax
    jnz .have_com_mem
    jmp .close_only
.have_com_mem:
    mov si, ax

    push si
    call far [cs:IMP_GLOBALLOCK_P2]
    push dx

    ; Read .COM payload into DX:0x0100 and close file handle immediately
    push di
    push dx
    mov ax, 0x0100
    push ax
    mov ax, 0x0E00
    push ax
    call far [cs:IMP_LREAD_P8]
    push di
    call far [cs:IMP_LCLOSE_P2]

    pop dx
    mov es, dx
    mov word [es:0x0000], 0x20CD
    mov bx, 0x0100
    xor cx, cx
    xor dx, dx

.vm_lp:
    mov al, byte [es:bx]
    inc bx
    cmp al, 0xBA
    je .op_mov_dx
    cmp al, 0xB4
    je .op_mov_ah
    cmp al, 0xB2
    je .op_mov_dl
    cmp al, 0xB8
    je .op_mov_ax
    cmp al, 0xCD
    je .op_int
    jmp .vm_exit0

.op_mov_dx:
    mov dx, word [es:bx]
    add bx, 2
    jmp .vm_lp
.op_mov_ah:
    mov ch, byte [es:bx]
    inc bx
    jmp .vm_lp
.op_mov_dl:
    mov dl, byte [es:bx]
    inc bx
    jmp .vm_lp
.op_mov_ax:
    mov cx, word [es:bx]
    add bx, 2
    jmp .vm_lp

.op_int:
    mov al, byte [es:bx]
    inc bx
    cmp al, 0x20
    jne .chk_4c
    jmp .vm_exit0
.chk_4c:
    cmp ch, 0x4C
    jne .chk_02
    jmp .vm_exit_al
.chk_02:
    cmp ch, 0x02
    je .int21_02
    cmp ch, 0x09
    je .int21_09
    jmp .vm_lp

.int21_02:
    mov byte [bW16ScratchA], dl
    mov byte [bW16ScratchA + 1], 0
    push es
    push bx
    push cx
    push dx
    push si
    mov si, bW16ScratchA
    call w16_term_write_str
    pop si
    pop dx
    pop cx
    pop bx
    pop es
    jmp .vm_lp

.int21_09:
    push di
    mov di, dx
    xor ax, ax
.cp9:
    push bx
    mov bx, ax
    mov al, byte [es:di]
    cmp al, '$'
    je .cp9_end
    test al, al
    jz .cp9_end
    mov byte [bx + bW16ScratchA], al
    mov ax, bx
    pop bx
    inc di
    inc ax
    cmp ax, 240
    jb .cp9
    push bx
    mov bx, ax
.cp9_end:
    mov byte [bx + bW16ScratchA], 0
    pop bx
    pop di
    push es
    push bx
    push cx
    push dx
    push si
    mov si, bW16ScratchA
    call w16_term_write_str
    pop si
    pop dx
    pop cx
    pop bx
    pop es
    jmp .vm_lp

.vm_exit_al:
    xor ch, ch
    mov word [wW16ExitCode], cx
    jmp .vm_free
.vm_exit0:
    mov word [wW16ExitCode], 0
.vm_free:
    push si
    call far [cs:IMP_GLOBALUNLOCK_P2]
    push si
    call far [cs:IMP_GLOBALFREE_P2]
    pop es
    ret

.close_only:
    push di
    call far [cs:IMP_LCLOSE_P2]
    pop es
    ret
@endproc

;; @proc w16_exec_external
;; @abi win16
;; @in DS:SI = external command/program name
;; @out Inspects MZ/NE header; runs .BAT in-process, NE via WinExec/LoadModule, .COM via internal/PIF
;; @err Sets wW16ExitCode = 1 if launch fails
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Dispatches .BAT, Win16 NE, and DOS .COM/MZ programs across Windows 1.0..3.2.
@proc w16_exec_external
    ; Copy first token (program path) into bW16ScratchA
    mov di, bW16ScratchA
.tok_lp:
    mov al, byte [si]
    test al, al
    jz .tok_done
    cmp al, 0x20
    je .tok_done
    mov byte [di], al
    inc si
    inc di
    jmp .tok_lp
.tok_done:
    mov byte [di], 0

    ; Check if filename ends in .BAT or .CMD -> run batch in-process!
    mov si, bW16ScratchA
    call w16_str_len
    cmp cx, 4
    jb .probe_file
    mov bx, cx
    sub bx, 4
    cmp byte [bx + bW16ScratchA], '.'
    jne .probe_file
    mov al, byte [bx + bW16ScratchA + 1]
    and al, 0xDF
    cmp al, 'B'
    je .go_bat
    cmp al, 'C'
    jne .probe_file
    mov al, byte [bx + bW16ScratchA + 2]
    and al, 0xDF
    cmp al, 'M'
    jne .probe_file
.go_bat:
    jmp .run_bat

.probe_file:
    mov si, bW16ScratchA
    call w16_vfs_open_read
    jc .launch_os_mod
    mov bx, ax

    ; Check if --internal-dos mode and .COM extension
    cmp byte [bW16InternalDos], 0
    je .inspect_hdr
    mov si, bW16ScratchA
    call w16_str_len
    cmp cx, 4
    jb .inspect_hdr
    mov di, cx
    sub di, 4
    mov al, byte [di + bW16ScratchA + 1]
    and al, 0xDF
    cmp al, 'C'
    jne .inspect_hdr
    mov al, byte [di + bW16ScratchA + 2]
    and al, 0xDF
    cmp al, 'O'
    jne .inspect_hdr
    mov ax, bx
    call w16_run_internal_com
    ret

.inspect_hdr:
    push es
    push bx
    push bx
    push ds
    mov ax, bW16OfStruct
    push ax
    mov ax, 64
    push ax
    call far [cs:IMP_LREAD_P8]
    pop bx
    push bx
    call far [cs:IMP_LCLOSE_P2]
    pop es

.launch_os_mod:
    ; Use WinExec (KERNEL.166) on Windows 3.0+, or LoadModule (KERNEL.45) on Windows 1.0/2.0!
    push es
    cmp word [pfnWinExec_p6 + 2], 0
    je .use_loadmodule

    push ds
    mov ax, bW16ScratchA
    push ax
    mov ax, 1
    push ax
    call far [pfnWinExec_p6]
    jmp .check_hinst

.use_loadmodule:
    ; Zero 14-byte LOADPARMS16 in bW16OfStruct and call KERNEL.45 LoadModule
    mov di, bW16OfStruct
    mov cx, 7
.zlp:
    mov word [di], 0
    add di, 2
    loop .zlp
    push ds
    mov ax, bW16ScratchA
    push ax
    push ds
    mov ax, bW16OfStruct
    push ax
    call far [cs:IMP_LOADMODULE_P8]

.check_hinst:
    cmp ax, 32
    jb .launch_err
    ; Poll GetModuleUsage(hInst) while yielding VM slice (INT 2Fh AX=1680h)
    push ax
    call far [cs:IMP_GETMODULEUSAGE_P2]
    mov ax, 0x1680
    int 0x2F
    pop es
    mov word [wW16ExitCode], 0
    ret

.launch_err:
    pop es
    mov word [wW16ExitCode], 1
    ret

.run_bat:
    mov si, bW16ScratchA
    call w16_run_batch_file
    ret
@endproc

;; @proc w16_run_batch_file
;; @abi win16
;; @in DS:SI = .BAT/.CMD filename in DGROUP
;; @out Reads script, closes handle immediately (FILES=8 safe), executes lines until EOF or Ctrl+C
;; @err Sets wW16ExitCode = 1 if script missing
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Stateless in-process .BAT/.CMD runner inside Win16.
@proc w16_run_batch_file
    call w16_vfs_open_read
    jc .missing
    push es
    mov bx, ax
    push bx
    push bx
    push ds
    mov ax, bW16ScratchB
    push ax
    mov ax, 250
    push ax
    call far [cs:IMP_LREAD_P8]
    mov di, ax
    pop bx
    push bx
    call far [cs:IMP_LCLOSE_P2]
    pop es

    cmp di, 0xFFFF
    je .missing
    mov byte [di + bW16ScratchB], 0

    mov si, bW16ScratchB
.line_lp:
    cmp byte [bW16CtrlBreak], 0
    jne .done
    mov al, byte [si]
    test al, al
    jz .done
    cmp al, 0x0D
    je .skip_eol
    cmp al, 0x0A
    je .skip_eol

    mov di, bW16InputBuf
.copy_ln:
    mov al, byte [si]
    test al, al
    jz .got_ln
    cmp al, 0x0D
    je .got_ln
    cmp al, 0x0A
    je .got_ln
    mov byte [di], al
    inc si
    inc di
    jmp .copy_ln
.got_ln:
    mov byte [di], 0
    push si
    mov si, bW16InputBuf
    call w16_exec_line
    pop si
    jmp .line_lp

.skip_eol:
    inc si
    jmp .line_lp
.done:
    ret
.missing:
    mov word [wW16ExitCode], 1
    ret
@endproc

;; @proc w16_exec_line
;; @abi win16
;; @in DS:SI = NUL-terminated command line
;; @out Dispatches built-in Win16 commands or external .BAT / NE / .COM targets
;; @err Updates wW16ExitCode
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Primary Win16 command dispatcher shared by /C, /K, .BAT, and interactive prompt.
@proc w16_exec_line
    call w16_skip_spaces
    cmp byte [si], '@'
    jne .no_at
    inc si
    call w16_skip_spaces
.no_at:
    mov al, byte [si]
    test al, al
    jz .early_ret
    cmp al, ':'
    je .early_ret

    mov di, (szKwRem - w16_data_base)
    call w16_match_kw
    jnc .early_ret
    jmp .chk_ver
.early_ret:
    ret

.chk_ver:
    mov di, (szKwVer - w16_data_base)
    call w16_match_kw
    jc .nx_cls
    call w16_cmd_ver
    ret

.nx_cls:
    mov di, (szKwCls - w16_data_base)
    call w16_match_kw
    jc .nx_echo
    call w16_term_clear
    mov word [wW16ExitCode], 0
    ret

.nx_echo:
    mov di, (szKwEcho - w16_data_base)
    call w16_match_kw
    jc .nx_set
    call w16_term_writeln
    mov word [wW16ExitCode], 0
    ret

.nx_set:
    mov di, (szKwSet - w16_data_base)
    call w16_match_kw
    jc .nx_cd
    call w16_cmd_set
    ret

.nx_cd:
    mov di, (szKwCd - w16_data_base)
    call w16_match_kw
    jc .nx_pushd
    cmp byte [si], 0
    je .cd_print
    mov di, bW16CwdBuf
    call w16_str_copy
    mov word [wW16ExitCode], 0
    ret
.cd_print:
    mov si, bW16CwdBuf
    call w16_term_writeln
    mov word [wW16ExitCode], 0
    ret

.nx_pushd:
    mov di, (szKwPushd - w16_data_base)
    call w16_match_kw
    jc .nx_popd
    call w16_cmd_pushd
    ret

.nx_popd:
    mov di, (szKwPopd - w16_data_base)
    call w16_match_kw
    jc .nx_mem
    call w16_cmd_popd
    ret

.nx_mem:
    mov di, (szKwMem - w16_data_base)
    call w16_match_kw
    jc .nx_type
    call w16_cmd_mem
    ret

.nx_type:
    mov di, (szKwType - w16_data_base)
    call w16_match_kw
    jc .nx_del
    call w16_cmd_type
    ret

.nx_del:
    mov di, (szKwDel - w16_data_base)
    call w16_match_kw
    jc .nx_copy
    call w16_vfs_delete
    mov word [wW16ExitCode], 0
    jnc .ret_noop
    mov word [wW16ExitCode], 1
    ret

.nx_copy:
    mov di, (szKwCopy - w16_data_base)
    call w16_match_kw
    jc .nx_title
    call w16_cmd_copy
    ret

.nx_title:
    mov di, (szKwTitle - w16_data_base)
    call w16_match_kw
    jc .nx_call
    cmp word [wW16HWnd], 0
    je .ret_noop
    push es
    push word [wW16HWnd]
    push ds
    push si
    call far [cs:IMP_SETWINDOWTEXT_P6]
    pop es
    mov word [wW16ExitCode], 0
    ret

.nx_call:
    mov di, (szKwCall - w16_data_base)
    call w16_match_kw
    jc .nx_exit
    call w16_exec_line
    ret

.nx_exit:
    mov di, (szKwExit - w16_data_base)
    call w16_match_kw
    jc .nx_ext
    xor ax, ax
    cmp byte [si], 0
    je .post_exit
    call w16_parse_u16
.post_exit:
    mov word [wW16ExitCode], ax
    push es
    push ax
    call far [cs:IMP_POSTQUITMESSAGE_P2]
    pop es
    ret

.nx_ext:
    call w16_exec_external
.ret_noop:
    ret
@endproc

;; @proc w16_run_startup_cmdline
;; @abi win16
;; @in DS = DGROUP, [wW16CmdLineSel]:[wW16CmdLineOff] holds task tail
;; @out Copies tail into DGROUP, applies /F, /Q, --internal-dos, /C <cmd>, /K <cmd>
;; @err none
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Processes Win16 startup switches after window creation.
@proc w16_run_startup_cmdline
    ; Initialize default CWD "C:"
    mov si, (szDefCwd - w16_data_base)
    mov di, bW16CwdBuf
    call w16_str_copy

    cmp word [wW16CmdLineSel], 0
    jne .copy_arg_tail
    jmp .show_banner

.copy_arg_tail:
    push es
    mov es, word [wW16CmdLineSel]
    mov bx, word [wW16CmdLineOff]
    xor di, di
.cp_tail:
    mov al, byte [es:bx]
    mov byte [di + bW16ScratchB], al
    test al, al
    jz .tail_copied
    inc bx
    inc di
    cmp di, 250
    jb .cp_tail
    mov byte [di + bW16ScratchB], 0
.tail_copied:
    pop es

    mov si, bW16ScratchB
.sw_lp:
    call w16_skip_spaces
    mov al, byte [si]
    test al, al
    jnz .have_sw_ch
    jmp .show_banner
.have_sw_ch:
    cmp al, '-'
    je .chk_long_flag
    cmp al, '/'
    je .parse_slash
    jmp .exec_rem
.parse_slash:

    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'F'
    je .sw_fail
    cmp ah, 'Q'
    je .sw_quiet
    cmp ah, 'C'
    je .sw_c
    cmp ah, 'K'
    je .sw_k
    add si, 2
    jmp .sw_lp

.sw_fail:
    mov byte [bW16FailCrit], 1
    push es
    push si
    mov ax, SEM_FAILCRITICALERRORS16
    push ax
    call far [cs:IMP_SETERRORMODE_P2]
    pop si
    pop es
    add si, 2
    jmp .sw_lp

.sw_quiet:
    mov byte [bW16EchoFlag], 0
    add si, 2
    jmp .sw_lp

.chk_long_flag:
    cmp byte [si + 1], '-'
    jne .exec_rem
    mov byte [bW16InternalDos], 1
.skip_long:
    mov al, byte [si]
    test al, al
    jz .show_banner
    cmp al, 0x20
    je .sw_lp
    inc si
    jmp .skip_long

.sw_c:
    mov byte [bW16BatchClose], 1
    add si, 2
    call w16_skip_spaces
    call w16_exec_line
    push es
    push word [wW16ExitCode]
    call far [cs:IMP_POSTQUITMESSAGE_P2]
    pop es
    ret

.sw_k:
    add si, 2
    call w16_skip_spaces
.exec_rem:
    call w16_exec_line
    ret

.show_banner:
    mov si, (szW16Banner - w16_data_base)
    call w16_term_writeln
    mov si, bW16CwdBuf
    call w16_term_write_str
    mov si, (szPromptGt - w16_data_base)
    call w16_term_write_str
    ret
@endproc
