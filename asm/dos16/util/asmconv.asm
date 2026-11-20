;; @file asmconv.asm
;; @isa 8086
;; @desc Self-hosted MS-DOS 8086 canonical assembly dialect lowerer (ASMCONV.COM <= 4 KiB).

@cpu 8086
@format com
@entry asmconv_entry

;; @const TARGET_NASM
;; @desc Output dialect selector for Netwide Assembler (/N).
@const TARGET_NASM = 0

;; @const TARGET_FASM
;; @desc Output dialect selector for Flat Assembler (/F).
@const TARGET_FASM = 1

;; @const TARGET_JWASM
;; @desc Output dialect selector for JWasm MASM-compatible assembler (/J).
@const TARGET_JWASM = 2

;; @const SZ_IN_PATH
;; @desc Uninitialized 64-byte input path buffer in .COM scratch RAM (0x0900).
@const SZ_IN_PATH = 0x0900

;; @const SZ_OUT_PATH
;; @desc Uninitialized 64-byte output path buffer in .COM scratch RAM (0x0940).
@const SZ_OUT_PATH = 0x0940

;; @const SZ_PROC_BUF
;; @desc Uninitialized 64-byte active @proc identifier buffer in .COM scratch RAM (0x0980).
@const SZ_PROC_BUF = 0x0980

;; @const BUF_OUT
;; @desc Uninitialized 4 KiB output write buffer in .COM scratch RAM (0x1000..0x1FFF).
@const BUF_OUT = 0x1000

;; @const BUF_IN
;; @desc Uninitialized 16 KiB input source buffer in .COM scratch RAM (0x2000..0x5FFF).
@const BUF_IN = 0x2000

;; @proc asmconv_entry
;; @abi dos16
;; @in DS=ES=CS=PSP segment, [0x0080]=command tail
;; @out Process exits via INT 21h AH=4Ch
;; @err Exits with code 1 on invalid CLI or file I/O failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Entrypoint for ASMCONV.COM (/N | /F | /J <in.asm> <out.asm>).
@proc asmconv_entry
    cld
    call parse_cli_args
    jc .cli_err
    cmp byte [bHelpOnly], 1
    je .show_help

    call load_input_file
    jc .io_err

    call create_output_file
    jc .io_err

    call convert_buffer
    jc .close_err

    call close_output_file
    mov ax, 0x4C00
    int 0x21
    ret

.show_help:
    mov dx, addr(szHelpMsg)
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C00
    int 0x21
    ret

.close_err:
    call close_output_file
.io_err:
    mov dx, addr(szIoErrMsg)
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C01
    int 0x21
    ret

.cli_err:
    mov dx, addr(szHelpMsg)
    mov ah, 0x09
    int 0x21
    mov ax, 0x4C01
    int 0x21
    ret
@endproc

;; @proc skip_spaces
;; @abi dos16
;; @in DS:SI = input pointer
;; @out DS:SI = first non-space/non-tab byte, AL = [SI]
;; @err none
;; @clobbers ax, si
;; @desc Advances SI past ASCII space (0x20) and horizontal tab (0x09) bytes.
@proc skip_spaces
.loop:
    mov al, byte [si]
    cmp al, 0x20
    je .adv
    cmp al, 0x09
    je .adv
    ret
.adv:
    inc si
    jmp .loop
@endproc

;; @proc copy_token_asciiz
;; @abi dos16
;; @in DS:SI = tail cursor, ES:DI = destination buffer (max 64 bytes)
;; @out CF=0 if non-empty token copied, CF=1 if at CR/NUL
;; @err CF=1 on empty token
;; @clobbers ax, cx, si, di
;; @desc Copies one whitespace-delimited filename token into an ASCIIZ buffer.
@proc copy_token_asciiz
    call skip_spaces
    cmp al, 0x0D
    je .empty
    cmp al, 0x00
    je .empty
    mov cx, 63
.copy_lp:
    mov al, byte [si]
    cmp al, 0x0D
    je .done
    cmp al, 0x00
    je .done
    cmp al, 0x20
    je .done
    cmp al, 0x09
    je .done
    mov byte [di], al
    inc si
    inc di
    loop .copy_lp
.done:
    mov byte [di], 0
    clc
    ret
.empty:
    stc
    ret
@endproc

;; @proc parse_cli_args
;; @abi dos16
;; @in PSP command tail at 0x0081
;; @out [bTarget], [bHelpOnly], [szInPath], [szOutPath], CF=0 on success
;; @err CF=1 on syntax error
;; @clobbers ax, cx, si, di
;; @desc Parses /N, /F, /J, or /? switch plus input and output filenames.
@proc parse_cli_args
    mov byte [bHelpOnly], 0
    mov si, 0x0081
    call skip_spaces
    cmp al, '/'
    je .got_slash
    cmp al, '-'
    je .got_slash
    stc
    ret
.got_slash:
    inc si
    mov al, byte [si]
    inc si
    cmp al, '?'
    je .is_help
    and al, 0xDF
    cmp al, 'H'
    je .is_help
    cmp al, 'N'
    je .is_nasm
    cmp al, 'F'
    je .is_fasm
    cmp al, 'J'
    je .is_jwasm
    stc
    ret
.is_help:
    mov byte [bHelpOnly], 1
    clc
    ret
.is_nasm:
    mov byte [bTarget], TARGET_NASM
    jmp .parse_paths
.is_fasm:
    mov byte [bTarget], TARGET_FASM
    jmp .parse_paths
.is_jwasm:
    mov byte [bTarget], TARGET_JWASM
.parse_paths:
    mov di, SZ_IN_PATH
    call copy_token_asciiz
    jc .fail
    mov di, SZ_OUT_PATH
    call copy_token_asciiz
    jc .fail
    clc
    ret
.fail:
    stc
    ret
@endproc

;; @proc load_input_file
;; @abi dos16
;; @in [SZ_IN_PATH] = ASCIIZ path
;; @out [wInLen] = bytes read into BUF_IN, CF=0 on success
;; @err CF=1 on open/read failure
;; @clobbers ax, bx, cx, dx
;; @desc Reads the canonical source file into BUF_IN (up to 16 KiB) and NUL-terminates it.
@proc load_input_file
    mov dx, SZ_IN_PATH
    mov ax, 0x3D00
    int 0x21
    jc .err
    mov bx, ax
    mov dx, BUF_IN
    mov cx, 16380
    mov ah, 0x3F
    int 0x21
    jc .close_fail
    mov word [wInLen], ax
    mov ah, 0x3E
    int 0x21
    mov bx, word [wInLen]
    mov byte [BUF_IN + bx], 0
    clc
    ret
.close_fail:
    mov ah, 0x3E
    int 0x21
.err:
    stc
    ret
@endproc

;; @proc create_output_file
;; @abi dos16
;; @in [SZ_OUT_PATH] = ASCIIZ path
;; @out [wOutHandle] = open handle, CF=0 on success
;; @err CF=1 on create error
;; @clobbers ax, cx, dx
;; @desc Creates/truncates the output assembly file for writing.
@proc create_output_file
    mov dx, SZ_OUT_PATH
    xor cx, cx
    mov ah, 0x3C
    int 0x21
    jc .err
    mov word [wOutHandle], ax
    mov word [wOutPos], 0
    mov byte [bJwasmComSeg], 0
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc flush_out_buf
;; @abi dos16
;; @in [wOutHandle], [wOutPos]
;; @out [wOutPos] = 0, CF=0 on success
;; @err CF=1 on write failure
;; @clobbers ax, bx, cx, dx
;; @desc Flushes buffered output bytes in BUF_OUT to [wOutHandle].
@proc flush_out_buf
    mov cx, word [wOutPos]
    test cx, cx
    jz .ok
    mov bx, word [wOutHandle]
    mov dx, BUF_OUT
    mov ah, 0x40
    int 0x21
    jc .err
    cmp ax, cx
    jne .err
    mov word [wOutPos], 0
.ok:
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc emit_byte
;; @abi dos16
;; @in AL = byte to append
;; @out CF=0 on success
;; @err CF=1 on flush failure
;; @clobbers ax, bx, cx, dx
;; @desc Appends one byte to BUF_OUT, flushing automatically at 4096 bytes.
@proc emit_byte
    mov bx, word [wOutPos]
    mov byte [BUF_OUT + bx], al
    inc bx
    mov word [wOutPos], bx
    cmp bx, 4096
    jb .ok
    call flush_out_buf
    ret
.ok:
    clc
    ret
@endproc

;; @proc emit_asciiz
;; @abi dos16
;; @in DS:DI = NUL-terminated string
;; @out CF=0 on success
;; @err CF=1 on write failure
;; @clobbers ax, bx, cx, dx, di
;; @desc Appends a NUL-terminated string at DS:DI to the output stream.
@proc emit_asciiz
.lp:
    mov al, byte [di]
    test al, al
    jz .done
    call emit_byte
    jc .err
    inc di
    jmp .lp
.done:
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc emit_crlf
;; @abi dos16
;; @in none
;; @out CF=0 on success
;; @err CF=1 on write failure
;; @clobbers ax, bx, cx, dx
;; @desc Appends DOS CRLF (0x0D, 0x0A) to the output stream.
@proc emit_crlf
    mov al, 0x0D
    call emit_byte
    jc .err
    mov al, 0x0A
    call emit_byte
    ret
.err:
    stc
    ret
@endproc

;; @proc close_output_file
;; @abi dos16
;; @in [wOutHandle]
;; @out Output flushed and handle closed
;; @err none
;; @clobbers ax, bx, cx, dx, di
;; @desc Emits JWasm trailer if needed, flushes bufOut, and closes [wOutHandle].
@proc close_output_file
    cmp byte [bJwasmComSeg], 1
    jne .do_flush
    mov di, addr(szJwTrailer)
    call emit_asciiz
.do_flush:
    call flush_out_buf
    mov bx, word [wOutHandle]
    test bx, bx
    jz .done
    mov ah, 0x3E
    int 0x21
    mov word [wOutHandle], 0
.done:
    ret
@endproc

;; @proc match_kw
;; @abi dos16
;; @in DS:SI = line cursor, DS:DI = ASCIIZ keyword
;; @out CF=0 and SI advanced if matched; CF=1 and SI restored if not matched
;; @err none
;; @clobbers ax, si, di
;; @desc Case-sensitive prefix matcher used for canonical @directives.
@proc match_kw
    push si
.lp:
    mov al, byte [di]
    test al, al
    jz .matched
    cmp al, byte [si]
    jne .no
    inc si
    inc di
    jmp .lp
.matched:
    pop ax
    clc
    ret
.no:
    pop si
    stc
    ret
@endproc

;; @proc emit_line_rest_rewritten
;; @abi dos16
;; @in DS:SI = start of remainder of current line
;; @out DS:SI = positioned at EOL (CR/LF/NUL), CF=0 on success
;; @err CF=1 on write error
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Copies current line while lowering 0xHEX -> 0HEXh and addr(sym) -> OFFSET sym for JWasm.
@proc emit_line_rest_rewritten
.lp:
    mov al, byte [si]
    cmp al, 0x00
    je .done
    cmp al, 0x0D
    je .done
    cmp al, 0x0A
    je .done

    ; Check `addr(` token
    mov di, addr(szKwAddr)
    call match_kw
    jnc .got_addr

    ; Check `0x` hex prefix on JWasm (reload AL since match_kw clobbers AL)
    mov al, byte [si]
    cmp byte [bTarget], TARGET_JWASM
    jne .plain_char
    cmp al, '0'
    jne .plain_char
    cmp byte [si + 1], 'x'
    jne .plain_char
    add si, 2
    mov al, '0'
    call emit_byte
    jc .err
.hex_lp:
    mov al, byte [si]
    call is_hex_char
    jc .hex_end
    call emit_byte
    jc .err
    inc si
    jmp .hex_lp
.hex_end:
    mov al, 'h'
    call emit_byte
    jc .err
    jmp .lp

.got_addr:
    cmp byte [bTarget], TARGET_JWASM
    jne .addr_copy
    mov di, addr(szJwOffset)
    call emit_asciiz
    jc .err
.addr_copy:
    mov al, byte [si]
    cmp al, 0x00
    je .done
    cmp al, ')'
    je .addr_close
    call emit_byte
    jc .err
    inc si
    jmp .addr_copy
.addr_close:
    inc si
    jmp .lp

.plain_char:
    mov al, byte [si]
    call emit_byte
    jc .err
    inc si
    jmp .lp

.done:
    call emit_crlf
    ret
.err:
    stc
    ret
@endproc

;; @proc is_hex_char
;; @abi dos16
;; @in AL = ASCII byte
;; @out CF=0 if [0-9A-Fa-f], CF=1 otherwise
;; @err none
;; @clobbers none
;; @desc Tests whether AL is an ASCII hexadecimal digit without modifying AL.
@proc is_hex_char
    cmp al, '0'
    jb .no
    cmp al, '9'
    jbe .yes
    cmp al, 'A'
    jb .no
    cmp al, 'F'
    jbe .yes
    cmp al, 'a'
    jb .no
    cmp al, 'f'
    jbe .yes
.no:
    stc
    ret
.yes:
    clc
    ret
@endproc

;; @proc emit_const_dir
;; @abi dos16
;; @in DS:SI = cursor after "@const "
;; @out DS:SI = advanced to EOL, CF=0 on success
;; @err CF=1 on write failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Emits NAME equ/=|EQU VALUE for canonical @const lines.
@proc emit_const_dir
    call skip_spaces
.cname_lp:
    mov al, byte [si]
    cmp al, 0x00
    je .const_eol
    cmp al, '='
    je .got_eq
    cmp al, 0x20
    je .skip_to_eq
    call emit_byte
    jc .err
    inc si
    jmp .cname_lp
.skip_to_eq:
    call skip_spaces
    cmp al, '='
    jne .const_eol
.got_eq:
    inc si
    call skip_spaces
    mov di, addr(szEquNasm)
    cmp byte [bTarget], TARGET_NASM
    je .emit_eq_kw
    mov di, addr(szEquFasm)
    cmp byte [bTarget], TARGET_FASM
    je .emit_eq_kw
    mov di, addr(szEquJwasm)
.emit_eq_kw:
    call emit_asciiz
    jc .err
    call emit_line_rest_rewritten
    ret
.const_eol:
    call skip_to_eol
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc emit_proc_dir
;; @abi dos16
;; @in DS:SI = cursor after "@proc "
;; @out DS:SI = advanced to EOL, SZ_PROC_BUF populated, CF=0 on success
;; @err CF=1 on write failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Emits NAME: or NAME PROC NEAR for canonical @proc lines.
@proc emit_proc_dir
    call skip_spaces
    mov di, SZ_PROC_BUF
.pname_lp:
    mov al, byte [si]
    cmp al, 0x00
    je .pname_done
    cmp al, 0x0D
    je .pname_done
    cmp al, 0x0A
    je .pname_done
    cmp al, 0x20
    je .pname_done
    mov byte [di], al
    call emit_byte
    jc .err
    inc si
    inc di
    jmp .pname_lp
.pname_done:
    mov byte [di], 0
    call skip_to_eol
    cmp byte [bTarget], TARGET_JWASM
    je .proc_jw
    mov al, ':'
    call emit_byte
    jc .err
    call emit_crlf
    ret
.proc_jw:
    mov di, addr(szJwProcNear)
    call emit_asciiz
    ret
.err:
    stc
    ret
@endproc

;; @proc handle_at_directive
;; @abi dos16
;; @in DS:SI = pointer to '@' at start of trimmed line
;; @out DS:SI = advanced to EOL, CF=0 on success
;; @err CF=1 on write failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Lowers @cpu, @format, @entry, @const, @proc, and @endproc lines for /N, /F, or /J.
@proc handle_at_directive
    mov di, addr(szDirCpu)
    call match_kw
    jnc .do_cpu

    mov di, addr(szDirFmtCom)
    call match_kw
    jnc .do_fmt_com

    mov di, addr(szDirConst)
    call match_kw
    jnc .do_const

    mov di, addr(szDirProc)
    call match_kw
    jnc .do_proc

    mov di, addr(szDirEndProc)
    call match_kw
    jnc .do_endproc

    call skip_to_eol
    clc
    ret

.do_const:
    call emit_const_dir
    ret

.do_proc:
    call emit_proc_dir
    ret

.do_cpu:
    call skip_to_eol
    cmp byte [bTarget], TARGET_NASM
    je .cpu_nasm
    cmp byte [bTarget], TARGET_JWASM
    je .cpu_jwasm
    clc
    ret
.cpu_nasm:
    mov di, addr(szNasmCpu)
    call emit_asciiz
    ret
.cpu_jwasm:
    mov di, addr(szJwCpu)
    call emit_asciiz
    ret

.do_fmt_com:
    call skip_to_eol
    cmp byte [bTarget], TARGET_NASM
    je .com_nasm
    cmp byte [bTarget], TARGET_FASM
    je .com_fasm
    mov byte [bJwasmComSeg], 1
    mov di, addr(szJwComHdr)
    call emit_asciiz
    ret
.com_nasm:
    mov di, addr(szNasmComHdr)
    call emit_asciiz
    ret
.com_fasm:
    mov di, addr(szFasmComHdr)
    call emit_asciiz
    ret

.do_endproc:
    call skip_to_eol
    cmp byte [bTarget], TARGET_JWASM
    jne .endp_ok
    mov di, SZ_PROC_BUF
    call emit_asciiz
    jc .err
    mov di, addr(szJwEndp)
    call emit_asciiz
    ret
.endp_ok:
    clc
    ret
.err:
    stc
    ret
@endproc

;; @proc skip_to_eol
;; @abi dos16
;; @in DS:SI = cursor
;; @out DS:SI = pointing at CR, LF, or NUL
;; @err none
;; @clobbers ax, si
;; @desc Advances SI to the end of the current source line.
@proc skip_to_eol
.lp:
    mov al, byte [si]
    cmp al, 0x00
    je .done
    cmp al, 0x0D
    je .done
    cmp al, 0x0A
    je .done
    inc si
    jmp .lp
.done:
    ret
@endproc

;; @proc advance_past_eol
;; @abi dos16
;; @in DS:SI = pointing at CR, LF, or NUL
;; @out DS:SI = start of next line (or NUL)
;; @err none
;; @clobbers ax, si
;; @desc Consumes CR, LF, or CRLF line terminator.
@proc advance_past_eol
    mov al, byte [si]
    cmp al, 0x0D
    jne .chk_lf
    inc si
    mov al, byte [si]
.chk_lf:
    cmp al, 0x0A
    jne .done
    inc si
.done:
    ret
@endproc

;; @proc convert_buffer
;; @abi dos16
;; @in BUF_IN = NUL-terminated canonical source
;; @out Lowered lines emitted to BUF_OUT, CF=0 on success
;; @err CF=1 on write failure
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Main line-by-line lowering loop over BUF_IN.
@proc convert_buffer
    mov si, BUF_IN
.line_lp:
    mov al, byte [si]
    test al, al
    jz .done
    push si
    call skip_spaces
    cmp al, '@'
    je .is_dir
    pop si
    call emit_line_rest_rewritten
    jc .err
    call advance_past_eol
    jmp .line_lp

.is_dir:
    pop ax
    call handle_at_directive
    jc .err
    call advance_past_eol
    jmp .line_lp

.done:
    clc
    ret
.err:
    stc
    ret
@endproc

@data db szHelpMsg, "ASMCONV [/N|/F|/J] <in.asm> <out.asm>", 0x0D, 0x0A, '$'
@data db szIoErrMsg, "ASMCONV: file I/O error", 0x0D, 0x0A, '$'
@data db szKwAddr, "addr(", 0
@data db szJwOffset, "OFFSET ", 0
@data db szDirCpu, "@cpu", 0
@data db szDirFmtCom, "@format com", 0
@data db szDirConst, "@const ", 0
@data db szDirProc, "@proc ", 0
@data db szDirEndProc, "@endproc", 0
@data db szNasmCpu, "CPU 8086", 0x0D, 0x0A, 0
@data db szJwCpu, ".8086", 0x0D, 0x0A, "OPTION CASEMAP:NONE", 0x0D, 0x0A, 0
@data db szNasmComHdr, "BITS 16", 0x0D, 0x0A, "ORG 0x0100", 0x0D, 0x0A, 0
@data db szFasmComHdr, "format binary as 'com'", 0x0D, 0x0A, "use16", 0x0D, 0x0A, "org 0x0100", 0x0D, 0x0A, 0
@data db szJwComHdr, "_TEXT SEGMENT PARA PUBLIC 'CODE'", 0x0D, 0x0A, "ASSUME CS:_TEXT, DS:_TEXT, ES:_TEXT, SS:_TEXT", 0x0D, 0x0A, "ORG 0100h", 0x0D, 0x0A, 0
@data db szJwTrailer, "_TEXT ENDS", 0x0D, 0x0A, "END", 0x0D, 0x0A, 0
@data db szEquNasm, " equ ", 0
@data db szEquFasm, " = ", 0
@data db szEquJwasm, " EQU ", 0
@data db szJwProcNear, " PROC NEAR", 0x0D, 0x0A, 0
@data db szJwEndp, " ENDP", 0x0D, 0x0A, 0

@data db bTarget, 0
@data db bHelpOnly, 0
@data db bJwasmComSeg, 0
@data dw wInLen, 0
@data dw wOutHandle, 0
@data dw wOutPos, 0
