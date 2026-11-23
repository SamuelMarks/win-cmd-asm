;; @file u_common_a32.inc
;; @isa armv7
;; @desc Shared CRT-free ARMv7-A Thumb-2 PE32 (0x01C4 ARMNT) utility runtime (*W Unicode, CLI, UTF, VFS, 64-bit formatters).

@cpu armv7
@format inc

;; @proc ua32_strlen
;; @abi arm_nt
;; @in r0 = NUL-terminated UTF-8 string
;; @out r0 = byte length
;; @err none
;; @clobbers r0, r1, r2
;; @desc Counts bytes in NUL-terminated string r0.
@proc ua32_strlen
    mov r1, #0
.lp:
    ldrb r2, [r0, r1]
    cbz r2, .dn
    add r1, r1, #1
    b .lp
.dn:
    mov r0, r1
    bx lr
@endproc

;; @proc ua32_strcpy
;; @abi arm_nt
;; @in r0 = dst buffer, r1 = NUL-terminated src string
;; @out r0 = byte length copied
;; @err none
;; @clobbers r0, r2, r3
;; @desc Copies NUL-terminated string from r1 into r0.
@proc ua32_strcpy
    mov r2, #0
.lp:
    ldrb r3, [r1, r2]
    strb r3, [r0, r2]
    cbz r3, .dn
    add r2, r2, #1
    b .lp
.dn:
    mov r0, r2
    bx lr
@endproc

;; @proc ua32_chr_upper
;; @abi arm_nt
;; @in r0 = ASCII byte (0..255)
;; @out r0 = uppercase ASCII byte
;; @err none
;; @clobbers r0
;; @desc Folds ASCII 'a'..'z' to 'A'..'Z'.
@proc ua32_chr_upper
    and r0, r0, #0xFF
    cmp r0, #97
    blo .dn
    cmp r0, #122
    bhi .dn
    sub r0, r0, #32
.dn:
    bx lr
@endproc

;; @proc ua32_str_eq_ci
;; @abi arm_nt
;; @in r0 = string A, r1 = string B
;; @out r0 = 1 if equal case-insensitively, 0 otherwise
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Case-insensitive ASCII string equality comparison.
@proc ua32_str_eq_ci
    mov r2, #0
.lp:
    ldrb r3, [r0, r2]
    cmp r3, #97
    blo .u1
    cmp r3, #122
    bhi .u1
    sub r3, r3, #32
.u1:
    ldrb r12, [r1, r2]
    cmp r12, #97
    blo .u2
    cmp r12, #122
    bhi .u2
    sub r12, r12, #32
.u2:
    cmp r3, r12
    bne .no
    cbz r3, .yes
    add r2, r2, #1
    b .lp
.yes:
    mov r0, #1
    bx lr
.no:
    mov r0, #0
    bx lr
@endproc

;; @proc ua32_w_to_u8
;; @abi arm_nt
;; @in r0 = UTF-16LE src, r1 = UTF-8 dst, r2 = max dst bytes
;; @out r0 = UTF-8 byte length written (NUL-terminated)
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Pure Thumb-2 UTF-16LE to UTF-8/WTF-8 transcoder.
@proc ua32_w_to_u8
    push {r4, r5}
    mov r4, r1 ; start
    add r5, r1, r2
    sub r5, r5, #4 ; limit
.lp:
    ldrh r3, [r0]
    add r0, r0, #2
    cbz r3, .done
    cmp r1, r5
    bhs .done
    cmp r3, #0x80
    bhs .u2
    strb r3, [r1]
    add r1, r1, #1
    b .lp
.u2:
    cmp r3, #0x800
    bhs .u3
    lsr r12, r3, #6
    orr r12, r12, #0xC0
    strb r12, [r1]
    and r12, r3, #0x3F
    orr r12, r12, #0x80
    strb r12, [r1, #1]
    add r1, r1, #2
    b .lp
.u3:
    lsr r12, r3, #12
    orr r12, r12, #0xE0
    strb r12, [r1]
    lsr r12, r3, #6
    and r12, r12, #0x3F
    orr r12, r12, #0x80
    strb r12, [r1, #1]
    and r12, r3, #0x3F
    orr r12, r12, #0x80
    strb r12, [r1, #2]
    add r1, r1, #3
    b .lp
.done:
    mov r3, #0
    strb r3, [r1]
    sub r0, r1, r4
    pop {r4, r5}
    bx lr
@endproc

;; @proc ua32_u8_to_w
;; @abi arm_nt
;; @in r0 = UTF-8 src, r1 = UTF-16LE dst (2048B)
;; @out r0 = WCHAR count written (excluding NUL)
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Pure Thumb-2 UTF-8/WTF-8 to UTF-16LE transcoder.
@proc ua32_u8_to_w
    push {r4, r5}
    mov r4, r1
    add r5, r1, #2036
.lp:
    ldrb r2, [r0]
    cbz r2, .done
    cmp r1, r5
    bhs .done
    add r0, r0, #1
    cmp r2, #0x80
    blo .emit
    and r3, r2, #0xE0
    cmp r3, #0xC0
    bne .chk3
    ldrb r3, [r0]
    cbz r3, .emit
    add r0, r0, #1
    and r2, r2, #0x1F
    lsl r2, r2, #6
    and r3, r3, #0x3F
    orr r2, r2, r3
    b .emit
.chk3:
    and r3, r2, #0xF0
    cmp r3, #0xE0
    bne .emit
    ldrb r3, [r0]
    cbz r3, .emit
    and r2, r2, #0x0F
    lsl r2, r2, #12
    and r3, r3, #0x3F
    lsl r3, r3, #6
    orr r2, r2, r3
    ldrb r12, [r0, #1]
    cmp r12, #0
    beq .adv1
    add r0, r0, #2
    and r12, r12, #0x3F
    orr r2, r2, r12
    b .emit
.adv1:
    add r0, r0, #1
.emit:
    strh r2, [r1]
    add r1, r1, #2
    b .lp
.done:
    mov r2, #0
    strh r2, [r1]
    sub r0, r1, r4
    lsr r0, r0, #1
    pop {r4, r5}
    bx lr
@endproc

;; @proc ua32_init
;; @abi arm_nt
;; @in none
;; @out r0 = pointer to command-line argument tail (after argv[0]) in g_ua32_cmd
;; @err Terminates with exit code 1 if 512 KiB VirtualAlloc fails
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Initializes std handles, console detection, 512 KiB VirtualAlloc arena, and UTF-8 command line.
@proc ua32_init
    push {r4, lr}

    mvn r0, #9 ; -10 = STD_INPUT_HANDLE
    call_iat __imp_GetStdHandle
    adr_sym r1, g_ua32_stdin
    str r0, [r1]

    mvn r0, #10 ; -11 = STD_OUTPUT_HANDLE
    call_iat __imp_GetStdHandle
    adr_sym r1, g_ua32_stdout
    str r0, [r1]
    mov r4, r0

    mvn r0, #11 ; -12 = STD_ERROR_HANDLE
    call_iat __imp_GetStdHandle
    adr_sym r1, g_ua32_stderr
    str r0, [r1]

    mov r0, r4
    adr_sym r1, g_ua32_io_done
    call_iat __imp_GetConsoleMode
    adr_sym r1, g_ua32_out_is_con
    str r0, [r1]

    adr_sym r1, g_ua32_stdin
    ldr r0, [r1]
    adr_sym r1, g_ua32_io_done
    call_iat __imp_GetConsoleMode
    adr_sym r1, g_ua32_in_is_con
    str r0, [r1]

    mov r0, #0
    mov_imm32 r1, 524288
    mov_imm32 r2, 0x3000
    mov r3, #4
    call_iat __imp_VirtualAlloc
    cmp r0, #0
    bne .arena_ok
    mov r0, #1
    call_iat __imp_ExitProcess
.arena_ok:
    adr_sym r1, g_ua32_arena
    str r0, [r1]

    call_iat __imp_GetCommandLineW
    adr_sym r1, g_ua32_cmd
    mov_imm32 r2, 4092
    bl ua32_w_to_u8

    adr_sym r0, g_ua32_cmd
.ws0:
    ldrb r1, [r0]
    cmp r1, #0x20
    beq .adv0
    cmp r1, #0x09
    bne .tok0
.adv0:
    add r0, r0, #1
    b .ws0
.tok0:
    cmp r1, #34
    beq .q0
.uq0:
    ldrb r1, [r0]
    cbz r1, .done
    cmp r1, #0x20
    beq .done
    cmp r1, #0x09
    beq .done
    add r0, r0, #1
    b .uq0
.q0:
    add r0, r0, #1
.qlp0:
    ldrb r1, [r0]
    cbz r1, .done
    add r0, r0, #1
    cmp r1, #34
    bne .qlp0
.done:
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_exit
;; @abi arm_nt
;; @in r0 = process exit code
;; @out Does not return
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Releases work arena and terminates via KERNEL32!ExitProcess.
@proc ua32_exit
    push {r4, lr}
    mov r4, r0
    adr_sym r1, g_ua32_arena
    ldr r0, [r1]
    cbz r0, .ex
    mov r1, #0
    mov_imm32 r2, 0x8000
    call_iat __imp_VirtualFree
.ex:
    mov r0, r4
    call_iat __imp_ExitProcess
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_write_handle
;; @abi arm_nt
;; @in r0 = handle, r1 = UTF-8 buf, r2 = byte len, r3 = is_console (1 or 0)
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes UTF-8 bytes via WriteConsoleW (console) or WriteFile (pipes/files).
@proc ua32_write_handle
    push {r4, r5, r6, r7, r8, lr}
    sub sp, sp, #8
    cmp r2, #0
    beq .done
    mov r4, r0 ; handle
    mov r5, r1 ; buf
    mov r6, r2 ; remaining
    cmp r3, #0
    beq .raw_file

.chunk_lp:
    cmp r6, #0
    beq .done
    mov r7, r6
    cmp r7, #512
    bls .got_c
    mov r7, #512
.got_c:
    adr_sym r8, g_ua32_path_tmp
    mov r0, #0
.cp:
    cmp r0, r7
    bhs .cp_dn
    ldrb r1, [r5, r0]
    strb r1, [r8, r0]
    add r0, r0, #1
    b .cp
.cp_dn:
    mov r1, #0
    strb r1, [r8, r0]
    mov r0, r8
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    mov r2, r0
    mov r0, r4
    adr_sym r1, g_ua32_wbuf1
    adr_sym r3, g_ua32_io_done
    mov r12, #0
    str r12, [sp]
    call_iat __imp_WriteConsoleW
    add r5, r5, r7
    sub r6, r6, r7
    b .chunk_lp

.raw_file:
    mov r0, r4
    mov r1, r5
    mov r2, r6
    adr_sym r3, g_ua32_io_done
    mov r12, #0
    str r12, [sp]
    call_iat __imp_WriteFile
.done:
    add sp, sp, #8
    pop {r4, r5, r6, r7, r8, lr}
    bx lr
@endproc

;; @proc ua32_write_out
;; @abi arm_nt
;; @in r0 = UTF-8 bytes, r1 = byte length
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes r1 UTF-8 bytes from r0 to standard output.
@proc ua32_write_out
    push {r4, lr}
    mov r2, r1
    mov r1, r0
    adr_sym r0, g_ua32_stdout
    ldr r0, [r0]
    adr_sym r3, g_ua32_out_is_con
    ldr r3, [r3]
    bl ua32_write_handle
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_write_str
;; @abi arm_nt
;; @in r0 = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes NUL-terminated UTF-8 string r0 to standard output.
@proc ua32_write_str
    push {r4, lr}
    mov r4, r0
    bl ua32_strlen
    mov r1, r0
    mov r0, r4
    bl ua32_write_out
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_write_err_str
;; @abi arm_nt
;; @in r0 = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes NUL-terminated UTF-8 string r0 to standard error.
@proc ua32_write_err_str
    push {r4, lr}
    mov r4, r0
    bl ua32_strlen
    mov r2, r0
    mov r1, r4
    adr_sym r0, g_ua32_stderr
    ldr r0, [r0]
    mov r3, #0
    bl ua32_write_handle
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_write_crlf
;; @abi arm_nt
;; @in none
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes CRLF to standard output.
@proc ua32_write_crlf
    push {r4, lr}
    adr_sym r0, szUa32Crlf
    mov r1, #2
    bl ua32_write_out
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_write_chr
;; @abi arm_nt
;; @in r0 = single byte character
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Writes single byte r0 to standard output.
@proc ua32_write_chr
    push {r4, lr}
    adr_sym r1, g_ua32_num_buf
    strb r0, [r1]
    mov r0, r1
    mov r1, #1
    bl ua32_write_out
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_print_u32
;; @abi arm_nt
;; @in r0 = unsigned 32-bit integer
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Formats r0 in base 10 via ARMv7 hardware UDIV and writes to stdout.
@proc ua32_print_u32
    push {r4, lr}
    adr_sym r1, g_ua32_num_buf
    add r1, r1, #31
    mov r2, #0
    strb r2, [r1]
    mov r3, #10
.lp:
    sub r1, r1, #1
    udiv r2, r0, r3
    mls r12, r2, r3, r0
    add r12, r12, #48
    strb r12, [r1]
    mov r0, r2
    cmp r0, #0
    bne .lp
    mov r0, r1
    bl ua32_write_str
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_print_u64
;; @abi arm_nt
;; @in r0 = low 32 bits, r1 = high 32 bits of 64-bit unsigned integer
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Formats 64-bit unsigned integer r1:r0 in base 10 via bit-serial restoring division by 10.
@proc ua32_print_u64
    push {r4, r5, r6, r7, lr}
    sub sp, sp, #4
    adr_sym r4, g_ua32_num_buf
    add r4, r4, #48
    mov r2, #0
    strb r2, [r4]
    mov r5, r0 ; lo
    mov r6, r1 ; hi
.digit_lp:
    sub r4, r4, #1
    ; Divide r6:r5 by 10 -> quotient in r6:r5, remainder in r2
    mov r2, #0 ; rem
    mov r3, #64 ; 64 bits
.bit_lp:
    lsl r2, r2, #1
    lsr r12, r6, #31
    orr r2, r2, r12
    lsl r6, r6, #1
    lsr r12, r5, #31
    orr r6, r6, r12
    lsl r5, r5, #1
    cmp r2, #10
    blo .no_sub
    sub r2, r2, #10
    orr r5, r5, #1
.no_sub:
    subs r3, r3, #1
    bne .bit_lp
    add r2, r2, #48
    strb r2, [r4]
    orrs r0, r5, r6
    bne .digit_lp

    mov r0, r4
    bl ua32_write_str
    add sp, sp, #4
    pop {r4, r5, r6, r7, lr}
    bx lr
@endproc

;; @proc ua32_print_hex8
;; @abi arm_nt
;; @in r0 = 8-bit unsigned value
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints 2 uppercase hex digits for r0.
@proc ua32_print_hex8
    push {r4, lr}
    and r4, r0, #0xFF
    lsr r0, r4, #4
    cmp r0, #10
    blo .d1
    add r0, r0, #7
.d1:
    add r0, r0, #48
    bl ua32_write_chr
    and r0, r4, #0x0F
    cmp r0, #10
    blo .d2
    add r0, r0, #7
.d2:
    add r0, r0, #48
    bl ua32_write_chr
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_print_hex32
;; @abi arm_nt
;; @in r0 = 32-bit unsigned value
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Prints 8 uppercase hex digits for r0.
@proc ua32_print_hex32
    push {r4, lr}
    mov r4, r0
    lsr r0, r4, #24
    bl ua32_print_hex8
    lsr r0, r4, #16
    bl ua32_print_hex8
    lsr r0, r4, #8
    bl ua32_print_hex8
    mov r0, r4
    bl ua32_print_hex8
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_skip_ws
;; @abi arm_nt
;; @in r0 = cursor in command line
;; @out r0 = advanced cursor, r1 = byte at [r0]
;; @err none
;; @clobbers r0, r1
;; @desc Advances past spaces and tabs.
@proc ua32_skip_ws
.lp:
    ldrb r1, [r0]
    cmp r1, #0x20
    beq .adv
    cmp r1, #0x09
    bne .dn
.adv:
    add r0, r0, #1
    b .lp
.dn:
    bx lr
@endproc

;; @proc ua32_read_tok
;; @abi arm_nt
;; @in r0 = cursor in command line, r1 = destination token buffer (1024B)
;; @out r0 = updated cursor after token, r1 = token length
;; @err none
;; @clobbers r0, r1, r2, r3, r12
;; @desc Reads next whitespace-delimited or quoted token into r1.
@proc ua32_read_tok
    mov r2, r1 ; start
.ws:
    ldrb r3, [r0]
    cmp r3, #0x20
    beq .wsa
    cmp r3, #0x09
    bne .tok
.wsa:
    add r0, r0, #1
    b .ws
.tok:
    cbz r3, .done
    mov r12, #0
.tlp:
    ldrb r3, [r0]
    cbz r3, .done
    cmp r3, #0x20
    beq .done
    cmp r3, #0x09
    beq .done
    cmp r3, #34
    beq .inq
    cmp r12, #1000
    bhs .sk
    strb r3, [r1]
    add r1, r1, #1
    add r12, r12, #1
.sk:
    add r0, r0, #1
    b .tlp
.inq:
    add r0, r0, #1
.qlp:
    ldrb r3, [r0]
    cbz r3, .done
    add r0, r0, #1
    cmp r3, #34
    beq .tlp
    cmp r12, #1000
    bhs .qlp
    strb r3, [r1]
    add r1, r1, #1
    add r12, r12, #1
    b .qlp
.done:
    mov r3, #0
    strb r3, [r1]
    sub r1, r1, r2
    bx lr
@endproc

;; @proc ua32_parse_u32
;; @abi arm_nt
;; @in r0 = ASCII decimal digit string
;; @out r0 = parsed u32 value, r1 = pointer to first non-digit
;; @err none
;; @clobbers r0, r1, r2, r3
;; @desc Parses unsigned decimal integer from r0.
@proc ua32_parse_u32
    mov r1, r0
    mov r0, #0
    mov r3, #10
.lp:
    ldrb r2, [r1]
    cmp r2, #48
    blo .dn
    cmp r2, #57
    bhi .dn
    sub r2, r2, #48
    mla r0, r0, r3, r2
    add r1, r1, #1
    b .lp
.dn:
    bx lr
@endproc

;; @proc ua32_get_attr
;; @abi arm_nt
;; @in r0 = UTF-8 path
;; @out r0 = Win32 file attributes or 0xFFFFFFFF
;; @err r0 = 0xFFFFFFFF
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries file attributes via GetFileAttributesW.
@proc ua32_get_attr
    push {r4, lr}
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    call_iat __imp_GetFileAttributesW
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_set_attr
;; @abi arm_nt
;; @in r0 = UTF-8 path, r1 = Win32 attributes
;; @out r0 = non-zero on success, 0 on failure
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Sets file attributes via SetFileAttributesW.
@proc ua32_set_attr
    push {r4, lr}
    mov r4, r1
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    mov r1, r4
    call_iat __imp_SetFileAttributesW
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_copy_file
;; @abi arm_nt
;; @in r0 = UTF-8 src, r1 = UTF-8 dst, r2 = bFailIfExists
;; @out r0 = non-zero on success, 0 on failure
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Copies a file via CopyFileW.
@proc ua32_copy_file
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r1
    mov r5, r2
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    mov r0, r4
    adr_sym r1, g_ua32_wbuf2
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    adr_sym r1, g_ua32_wbuf2
    mov r2, r5
    call_iat __imp_CopyFileW
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc ua32_mkdir
;; @abi arm_nt
;; @in r0 = UTF-8 directory path
;; @out r0 = non-zero on success, 0 on failure
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Creates directory via CreateDirectoryW.
@proc ua32_mkdir
    push {r4, lr}
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    mov r1, #0
    call_iat __imp_CreateDirectoryW
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_delete_file
;; @abi arm_nt
;; @in r0 = UTF-8 file path
;; @out r0 = non-zero on success, 0 on failure
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Deletes file via DeleteFileW.
@proc ua32_delete_file
    push {r4, lr}
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    call_iat __imp_DeleteFileW
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_get_cwd
;; @abi arm_nt
;; @in r0 = UTF-8 dst buffer, r1 = capacity
;; @out r0 = UTF-8 byte length
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries current working directory in UTF-8.
@proc ua32_get_cwd
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    mov r0, #1000
    adr_sym r1, g_ua32_wbuf1
    call_iat __imp_GetCurrentDirectoryW
    adr_sym r0, g_ua32_wbuf1
    mov r1, r4
    mov r2, r5
    bl ua32_w_to_u8
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc ua32_get_env
;; @abi arm_nt
;; @in r0 = UTF-8 var name, r1 = UTF-8 dst, r2 = capacity
;; @out r0 = UTF-8 byte length (0 if not set)
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Queries environment variable via GetEnvironmentVariableW.
@proc ua32_get_env
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r1
    mov r5, r2
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    adr_sym r1, g_ua32_wbuf2
    mov r2, #1000
    call_iat __imp_GetEnvironmentVariableW
    cbz r0, .not_set
    adr_sym r0, g_ua32_wbuf2
    mov r1, r4
    mov r2, r5
    bl ua32_w_to_u8
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.not_set:
    mov r0, #0
    strb r0, [r4]
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc ua32_open_read
;; @abi arm_nt
;; @in r0 = UTF-8 path
;; @out r0 = Win32 handle or -1
;; @err r0 = -1
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Opens file or directory for reading / metadata query.
@proc ua32_open_read
    push {r4, lr}
    sub sp, sp, #16
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    mov_imm32 r1, 0x80000000
    mov r2, #3
    mov r3, #0
    mov r12, #3 ; OPEN_EXISTING
    str r12, [sp]
    mov_imm32 r12, 0x02000080
    str r12, [sp, #4]
    mov r12, #0
    str r12, [sp, #8]
    call_iat __imp_CreateFileW
    add sp, sp, #16
    pop {r4, lr}
    bx lr
@endproc

;; @proc ua32_load_stream
;; @abi arm_nt
;; @in r0 = UTF-8 path (empty=stdin), r1 = dst buffer, r2 = max bytes, r3 = decode_bom (1 or 0)
;; @out r0 = bytes stored in r1, r1 = 0 on success or 1 on open error
;; @err r1 = 1 on open error
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Reads file or stdin up to r2 bytes and normalizes UTF-8 / UTF-16LE / UTF-16BE BOMs.
@proc ua32_load_stream
    push {r4, r5, r6, r7, r8, r9, r10, lr}
    sub sp, sp, #8
    mov r5, r1 ; dst
    mov r6, r2 ; max_bytes
    mov r7, r3 ; decode_bom
    mov r8, #0 ; close_flag

    ldrb r3, [r0]
    cmp r3, #0
    bne .open_f
    adr_sym r1, g_ua32_stdin
    ldr r4, [r1]
    b .rd_init

.open_f:
    bl ua32_open_read
    cmn r0, #1
    beq .fail
    mov r4, r0
    mov r8, #1

.rd_init:
    mov r9, #0 ; total
.rd_lp:
    subs r2, r6, r9
    beq .eof
    mov_imm32 r12, 32768
    cmp r2, r12
    bls .do_rd
    mov r2, r12
.do_rd:
    mov r0, r4
    add r1, r5, r9
    adr_sym r3, g_ua32_io_done
    mov r12, #0
    str r12, [sp]
    call_iat __imp_ReadFile
    cbz r0, .eof
    adr_sym r1, g_ua32_io_done
    ldr r0, [r1]
    cbz r0, .eof
    add r9, r9, r0
    b .rd_lp

.eof:
    cmp r8, #0
    beq .chk_bom
    mov r0, r4
    call_iat __imp_CloseHandle

.chk_bom:
    mov r0, #0
    strb r0, [r5, r9]
    add r1, r9, #1
    strb r0, [r5, r1]
    cmp r7, #0
    beq .ok_dn
    cmp r9, #2
    blo .ok_dn

    cmp r9, #3
    blo .chk_u16le
    ldrb r0, [r5]
    cmp r0, #0xEF
    bne .chk_u16le
    ldrb r0, [r5, #1]
    cmp r0, #0xBB
    bne .chk_u16le
    ldrb r0, [r5, #2]
    cmp r0, #0xBF
    bne .chk_u16le
    sub r9, r9, #3
    mov r1, #0
.sh_u8:
    cmp r1, r9
    bhi .ok_dn
    add r2, r1, #3
    ldrb r0, [r5, r2]
    strb r0, [r5, r1]
    add r1, r1, #1
    b .sh_u8

.chk_u16le:
    ldrb r0, [r5]
    cmp r0, #0xFF
    bne .chk_u16be
    ldrb r0, [r5, #1]
    cmp r0, #0xFE
    bne .chk_u16be
    b .cvt_u16

.chk_u16be:
    ldrb r0, [r5]
    cmp r0, #0xFE
    bne .ok_dn
    ldrb r0, [r5, #1]
    cmp r0, #0xFF
    bne .ok_dn
    mov r1, #2
.sw_be:
    add r2, r1, #1
    cmp r2, r9
    bhs .cvt_u16
    ldrb r0, [r5, r1]
    ldrb r3, [r5, r2]
    strb r3, [r5, r1]
    strb r0, [r5, r2]
    add r1, r1, #2
    b .sw_be

.cvt_u16:
    adr_sym r0, g_ua32_arena
    ldr r10, [r0]
    mov_imm32 r12, 262144
    add r10, r10, r12
    sub r9, r9, #2
    mov r1, #0
.cp_w:
    cmp r1, r9
    bhs .cp_w_dn
    add r2, r1, #2
    ldrb r0, [r5, r2]
    strb r0, [r10, r1]
    add r1, r1, #1
    b .cp_w
.cp_w_dn:
    mov r0, #0
    strh r0, [r10, r1]
    mov r0, r10
    mov r1, r5
    mov r2, r6
    bl ua32_w_to_u8
    mov r9, r0

.ok_dn:
    mov r0, r9
    mov r1, #0
    add sp, sp, #8
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
.fail:
    mov r0, #0
    mov r1, #1
    add sp, sp, #8
    pop {r4, r5, r6, r7, r8, r9, r10, lr}
    bx lr
@endproc

;; @proc ua32_save_file
;; @abi arm_nt
;; @in r0 = UTF-8 path, r1 = buffer, r2 = byte len
;; @out r0 = 1 on success, 0 on error
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Creates/overwrites file r0 and writes r2 bytes from r1.
@proc ua32_save_file
    push {r4, r5, r6, lr}
    sub sp, sp, #16
    mov r5, r1
    mov r6, r2
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    mov_imm32 r1, 0x40000000
    mov r2, #0
    mov r3, #0
    mov r12, #2 ; CREATE_ALWAYS
    str r12, [sp]
    mov r12, #0x80
    str r12, [sp, #4]
    mov r12, #0
    str r12, [sp, #8]
    call_iat __imp_CreateFileW
    cmn r0, #1
    beq .fail
    mov r4, r0
    mov r0, r4
    mov r1, r5
    mov r2, r6
    adr_sym r3, g_ua32_io_done
    mov r12, #0
    str r12, [sp]
    call_iat __imp_WriteFile
    mov r0, r4
    call_iat __imp_CloseHandle
    mov r0, #1
    add sp, sp, #16
    pop {r4, r5, r6, lr}
    bx lr
.fail:
    mov r0, #0
    add sp, sp, #16
    pop {r4, r5, r6, lr}
    bx lr
@endproc

;; @proc ua32_find_unpack
;; @abi arm_nt
;; @in r0 = WIN32_FIND_DATAW (592B), r1 = 544B record (0:attr, 4:ftLo, 8:ftHi, 12:szHi, 16:szLo, 20:UTF-8 name[520])
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Unpacks WIN32_FIND_DATAW into normalized 544-byte record.
@proc ua32_find_unpack
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    ldr r2, [r4]
    str r2, [r5]
    ldr r2, [r4, #20]
    str r2, [r5, #4]
    ldr r2, [r4, #24]
    str r2, [r5, #8]
    ldr r2, [r4, #28]
    str r2, [r5, #12]
    ldr r2, [r4, #32]
    str r2, [r5, #16]
    add r0, r4, #44
    add r1, r5, #20
    mov r2, #516
    bl ua32_w_to_u8
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc ua32_find_first
;; @abi arm_nt
;; @in r0 = UTF-8 wildcard pattern, r1 = 544B entry buffer
;; @out r0 = search handle (-1 if no match)
;; @err r0 = -1
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Starts directory enumeration via FindFirstFileW.
@proc ua32_find_first
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r5, r1
    adr_sym r1, g_ua32_wbuf1
    bl ua32_u8_to_w
    adr_sym r0, g_ua32_wbuf1
    adr_sym r1, g_ua32_find_raw
    call_iat __imp_FindFirstFileW
    cmn r0, #1
    beq .dn
    mov r4, r0
    adr_sym r0, g_ua32_find_raw
    mov r1, r5
    bl ua32_find_unpack
    mov r0, r4
.dn:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc ua32_find_next
;; @abi arm_nt
;; @in r0 = search handle, r1 = 544B entry buffer
;; @out r0 = non-zero on match, 0 when exhausted
;; @err r0 = 0
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Advances directory enumeration via FindNextFileW.
@proc ua32_find_next
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r5, r1
    adr_sym r1, g_ua32_find_raw
    call_iat __imp_FindNextFileW
    cbz r0, .dn
    mov r4, r0
    adr_sym r0, g_ua32_find_raw
    mov r1, r5
    bl ua32_find_unpack
    mov r0, r4
.dn:
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc

;; @proc ua32_path_join
;; @abi arm_nt
;; @in r0 = dst buffer (1024B), r1 = dir UTF-8, r2 = leaf UTF-8
;; @out none
;; @err none
;; @clobbers r0, r1, r2, r3, r12, lr
;; @desc Joins directory r1 and leaf r2 into r0 with single backslash separator.
@proc ua32_path_join
    push {r4, r5, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r2
    bl ua32_strcpy
    cbz r0, .only_leaf
    sub r1, r0, #1
    ldrb r2, [r4, r1]
    cmp r2, #0x5C
    beq .at_sep
    cmp r2, #47
    beq .at_sep
    cmp r2, #58
    beq .at_sep
    mov r2, #0x5C
    strb r2, [r4, r0]
    add r0, r0, #1
.at_sep:
    add r0, r4, r0
    mov r1, r5
    bl ua32_strcpy
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
.only_leaf:
    mov r0, r4
    mov r1, r5
    bl ua32_strcpy
    add sp, sp, #4
    pop {r4, r5, lr}
    bx lr
@endproc
