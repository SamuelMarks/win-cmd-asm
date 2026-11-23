;; @file u_common_a64.inc
;; @isa arm64
;; @desc Shared CRT-free ARM64 AArch64 PE32+ (0xAA64) utility runtime (PAC/BTI, strict X18 TEB preservation, 16B SP alignment).

@cpu arm64
@format inc

;; @proc ua64_strlen
;; @abi arm64_ms
;; @in x0 = NUL-terminated UTF-8 string
;; @out x0 = byte length
;; @err none
;; @clobbers x0, x1, x2
;; @desc Counts bytes in NUL-terminated string x0.
@proc ua64_strlen
    mov x1, #0
.lp:
    ldrb w2, [x0, x1]
    cbz w2, .dn
    add x1, x1, #1
    b .lp
.dn:
    mov x0, x1
    ret
@endproc

;; @proc ua64_strcpy
;; @abi arm64_ms
;; @in x0 = dst buffer, x1 = NUL-terminated src string
;; @out x0 = byte length copied
;; @err none
;; @clobbers x0, x2, x3
;; @desc Copies NUL-terminated string from x1 into x0.
@proc ua64_strcpy
    mov x2, #0
.lp:
    ldrb w3, [x1, x2]
    strb w3, [x0, x2]
    cbz w3, .dn
    add x2, x2, #1
    b .lp
.dn:
    mov x0, x2
    ret
@endproc

;; @proc ua64_chr_upper
;; @abi arm64_ms
;; @in w0 = ASCII byte
;; @out w0 = uppercase ASCII byte
;; @err none
;; @clobbers x0
;; @desc Folds ASCII 'a'..'z' to 'A'..'Z'.
@proc ua64_chr_upper
    and w0, w0, #0xFF
    cmp w0, #97
    b.lo .dn
    cmp w0, #122
    b.hi .dn
    sub w0, w0, #32
.dn:
    ret
@endproc

;; @proc ua64_str_eq_ci
;; @abi arm64_ms
;; @in x0 = string A, x1 = string B
;; @out w0 = 1 if equal case-insensitively, 0 otherwise
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Case-insensitive ASCII string equality comparison.
@proc ua64_str_eq_ci
    mov x2, #0
.lp:
    ldrb w3, [x0, x2]
    cmp w3, #97
    b.lo .u1
    cmp w3, #122
    b.hi .u1
    sub w3, w3, #32
.u1:
    ldrb w4, [x1, x2]
    cmp w4, #97
    b.lo .u2
    cmp w4, #122
    b.hi .u2
    sub w4, w4, #32
.u2:
    cmp w3, w4
    b.ne .no
    cbz w3, .yes
    add x2, x2, #1
    b .lp
.yes:
    mov w0, #1
    ret
.no:
    mov w0, #0
    ret
@endproc

;; @proc ua64_w_to_u8
;; @abi arm64_ms
;; @in x0 = UTF-16LE src, x1 = UTF-8 dst, x2 = max dst bytes
;; @out x0 = UTF-8 byte length written (NUL-terminated)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Pure AArch64 UTF-16LE to UTF-8/WTF-8 transcoder.
@proc ua64_w_to_u8
    mov x4, x1
    add x5, x1, x2
    sub x5, x5, #4
.lp:
    ldrh w3, [x0], #2
    cbz w3, .done
    cmp x1, x5
    b.hs .done
    cmp w3, #0x80
    b.hs .u2
    strb w3, [x1], #1
    b .lp
.u2:
    cmp w3, #0x800
    b.hs .u3
    lsr w2, w3, #6
    orr w2, w2, #0xC0
    strb w2, [x1]
    and w2, w3, #0x3F
    orr w2, w2, #0x80
    strb w2, [x1, #1]
    add x1, x1, #2
    b .lp
.u3:
    lsr w2, w3, #12
    orr w2, w2, #0xE0
    strb w2, [x1]
    lsr w2, w3, #6
    and w2, w2, #0x3F
    orr w2, w2, #0x80
    strb w2, [x1, #1]
    and w2, w3, #0x3F
    orr w2, w2, #0x80
    strb w2, [x1, #2]
    add x1, x1, #3
    b .lp
.done:
    strb wzr, [x1]
    sub x0, x1, x4
    ret
@endproc

;; @proc ua64_u8_to_w
;; @abi arm64_ms
;; @in x0 = UTF-8 src, x1 = UTF-16LE dst (2048B)
;; @out x0 = WCHAR count written (excluding NUL)
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Pure AArch64 UTF-8/WTF-8 to UTF-16LE transcoder.
@proc ua64_u8_to_w
    mov x4, x1
    add x5, x1, #2036
.lp:
    ldrb w2, [x0]
    cbz w2, .done
    cmp x1, x5
    b.hs .done
    add x0, x0, #1
    cmp w2, #0x80
    b.lo .emit
    and w3, w2, #0xE0
    cmp w3, #0xC0
    b.ne .chk3
    ldrb w3, [x0]
    cbz w3, .emit
    add x0, x0, #1
    and w2, w2, #0x1F
    lsl w2, w2, #6
    and w3, w3, #0x3F
    orr w2, w2, w3
    b .emit
.chk3:
    and w3, w2, #0xF0
    cmp w3, #0xE0
    b.ne .emit
    ldrb w3, [x0]
    cbz w3, .emit
    and w2, w2, #0x0F
    lsl w2, w2, #12
    and w3, w3, #0x3F
    lsl w3, w3, #6
    orr w2, w2, w3
    ldrb w3, [x0, #1]
    cbz w3, .adv1
    add x0, x0, #2
    and w3, w3, #0x3F
    orr w2, w2, w3
    b .emit
.adv1:
    add x0, x0, #1
.emit:
    strh w2, [x1], #2
    b .lp
.done:
    strh wzr, [x1]
    sub x0, x1, x4
    lsr x0, x0, #1
    ret
@endproc

;; @proc ua64_init
;; @abi arm64_ms
;; @in none
;; @out x0 = pointer to command-line argument tail (after argv[0]) in g_ua64_cmd
;; @err Terminates with exit code 1 if 512 KiB VirtualAlloc fails
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Initializes std handles, console detection, 512 KiB VirtualAlloc arena, and UTF-8 command line (never touching x18).
@proc ua64_init
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!

    mov w0, #-10
    call_iat __imp_GetStdHandle
    adr_sym x1, g_ua64_stdin
    str x0, [x1]

    mov w0, #-11
    call_iat __imp_GetStdHandle
    adr_sym x1, g_ua64_stdout
    str x0, [x1]
    mov x19, x0

    mov w0, #-12
    call_iat __imp_GetStdHandle
    adr_sym x1, g_ua64_stderr
    str x0, [x1]

    mov x0, x19
    adr_sym x1, g_ua64_io_done
    call_iat __imp_GetConsoleMode
    adr_sym x1, g_ua64_out_is_con
    str w0, [x1]

    adr_sym x1, g_ua64_stdin
    ldr x0, [x1]
    adr_sym x1, g_ua64_io_done
    call_iat __imp_GetConsoleMode
    adr_sym x1, g_ua64_in_is_con
    str w0, [x1]

    mov x0, #0
    mov_imm32 x1, 524288
    mov_imm32 w2, 0x3000
    mov w3, #4
    call_iat __imp_VirtualAlloc
    cbnz x0, .arena_ok
    mov w0, #1
    call_iat __imp_ExitProcess
.arena_ok:
    adr_sym x1, g_ua64_arena
    str x0, [x1]

    call_iat __imp_GetCommandLineW
    adr_sym x1, g_ua64_cmd
    mov_imm32 x2, 4092
    bl ua64_w_to_u8

    adr_sym x0, g_ua64_cmd
.ws0:
    ldrb w1, [x0]
    cmp w1, #0x20
    b.eq .adv0
    cmp w1, #0x09
    b.ne .tok0
.adv0:
    add x0, x0, #1
    b .ws0
.tok0:
    cmp w1, #34
    b.eq .q0
.uq0:
    ldrb w1, [x0]
    cbz w1, .done
    cmp w1, #0x20
    b.eq .done
    cmp w1, #0x09
    b.eq .done
    add x0, x0, #1
    b .uq0
.q0:
    add x0, x0, #1
.qlp0:
    ldrb w1, [x0]
    cbz w1, .done
    add x0, x0, #1
    cmp w1, #34
    b.ne .qlp0
.done:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_exit
;; @abi arm64_ms
;; @in w0 = process exit code
;; @out Does not return
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Releases work arena and terminates via KERNEL32!ExitProcess.
@proc ua64_exit
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov w19, w0
    adr_sym x1, g_ua64_arena
    ldr x0, [x1]
    cbz x0, .ex
    mov x1, #0
    mov_imm32 w2, 0x8000
    call_iat __imp_VirtualFree
.ex:
    mov w0, w19
    call_iat __imp_ExitProcess
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_write_handle
;; @abi arm64_ms
;; @in x0 = handle, x1 = UTF-8 buf, x2 = byte len, w3 = is_console (1 or 0)
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes UTF-8 bytes via WriteConsoleW (console) or WriteFile (pipes/files).
@proc ua64_write_handle
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    cbz x2, .done
    mov x19, x0 ; handle
    mov x20, x1 ; buf
    mov x21, x2 ; remaining
    cbz w3, .raw_file

.chunk_lp:
    cbz x21, .done
    mov x22, x21
    cmp x22, #512
    b.ls .got_c
    mov x22, #512
.got_c:
    adr_sym x4, g_ua64_path_tmp
    mov x5, #0
.cp:
    cmp x5, x22
    b.hs .cp_dn
    ldrb w6, [x20, x5]
    strb w6, [x4, x5]
    add x5, x5, #1
    b .cp
.cp_dn:
    strb wzr, [x4, x5]
    mov x0, x4
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    mov x2, x0
    mov x0, x19
    adr_sym x1, g_ua64_wbuf1
    adr_sym x3, g_ua64_io_done
    mov x4, #0
    call_iat __imp_WriteConsoleW
    add x20, x20, x22
    sub x21, x21, x22
    b .chunk_lp

.raw_file:
    mov x0, x19
    mov x1, x20
    mov x2, x21
    adr_sym x3, g_ua64_io_done
    mov x4, #0
    call_iat __imp_WriteFile
.done:
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_write_out
;; @abi arm64_ms
;; @in x0 = UTF-8 bytes, x1 = byte len
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes x1 UTF-8 bytes from x0 to standard output.
@proc ua64_write_out
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x2, x1
    mov x1, x0
    adr_sym x4, g_ua64_stdout
    ldr x0, [x4]
    adr_sym x4, g_ua64_out_is_con
    ldr w3, [x4]
    bl ua64_write_handle
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_write_str
;; @abi arm64_ms
;; @in x0 = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes NUL-terminated UTF-8 string x0 to standard output.
@proc ua64_write_str
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    bl ua64_strlen
    mov x1, x0
    mov x0, x19
    bl ua64_write_out
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_write_err_str
;; @abi arm64_ms
;; @in x0 = NUL-terminated UTF-8 string
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes NUL-terminated UTF-8 string x0 to standard error.
@proc ua64_write_err_str
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    bl ua64_strlen
    mov x2, x0
    mov x1, x19
    adr_sym x4, g_ua64_stderr
    ldr x0, [x4]
    mov w3, #0
    bl ua64_write_handle
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_write_crlf
;; @abi arm64_ms
;; @in none
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes CRLF to standard output.
@proc ua64_write_crlf
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x0, szUa64Crlf
    mov x1, #2
    bl ua64_write_out
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_write_chr
;; @abi arm64_ms
;; @in w0 = single byte character
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Writes single byte w0 to standard output.
@proc ua64_write_chr
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x1, g_ua64_num_buf
    strb w0, [x1]
    mov x0, x1
    mov x1, #1
    bl ua64_write_out
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_print_u64
;; @abi arm64_ms
;; @in x0 = 64-bit unsigned integer
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Formats 64-bit unsigned integer x0 in base 10 via hardware UDIV/MSUB.
@proc ua64_print_u64
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x1, g_ua64_num_buf
    add x1, x1, #48
    strb wzr, [x1]
    mov x2, #10
.lp:
    sub x1, x1, #1
    udiv x3, x0, x2
    msub x4, x3, x2, x0
    add w4, w4, #48
    strb w4, [x1]
    mov x0, x3
    cbnz x0, .lp
    mov x0, x1
    bl ua64_write_str
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_print_hex8
;; @abi arm64_ms
;; @in w0 = 8-bit unsigned value
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints 2 uppercase hex digits for w0.
@proc ua64_print_hex8
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    and w19, w0, #0xFF
    lsr w0, w19, #4
    cmp w0, #10
    b.lo .d1
    add w0, w0, #7
.d1:
    add w0, w0, #48
    bl ua64_write_chr
    and w0, w19, #0x0F
    cmp w0, #10
    b.lo .d2
    add w0, w0, #7
.d2:
    add w0, w0, #48
    bl ua64_write_chr
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_print_hex32
;; @abi arm64_ms
;; @in w0 = 32-bit unsigned value
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Prints 8 uppercase hex digits for w0.
@proc ua64_print_hex32
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov w19, w0
    lsr w0, w19, #24
    bl ua64_print_hex8
    lsr w0, w19, #16
    bl ua64_print_hex8
    lsr w0, w19, #8
    bl ua64_print_hex8
    mov w0, w19
    bl ua64_print_hex8
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_skip_ws
;; @abi arm64_ms
;; @in x0 = cursor in command line
;; @out x0 = advanced cursor, w1 = byte at [x0]
;; @err none
;; @clobbers x0, x1
;; @desc Advances past spaces and tabs.
@proc ua64_skip_ws
.lp:
    ldrb w1, [x0]
    cmp w1, #0x20
    b.eq .adv
    cmp w1, #0x09
    b.ne .dn
.adv:
    add x0, x0, #1
    b .lp
.dn:
    ret
@endproc

;; @proc ua64_read_tok
;; @abi arm64_ms
;; @in x0 = cursor in command line, x1 = destination token buffer (1024B)
;; @out x0 = updated cursor after token, x1 = token length
;; @err none
;; @clobbers x0, x1, x2, x3, x4
;; @desc Reads next whitespace-delimited or quoted token into x1.
@proc ua64_read_tok
    mov x2, x1
.ws:
    ldrb w3, [x0]
    cmp w3, #0x20
    b.eq .wsa
    cmp w3, #0x09
    b.ne .tok
.wsa:
    add x0, x0, #1
    b .ws
.tok:
    cbz w3, .done
    mov x4, #0
.tlp:
    ldrb w3, [x0]
    cbz w3, .done
    cmp w3, #0x20
    b.eq .done
    cmp w3, #0x09
    b.eq .done
    cmp w3, #34
    b.eq .inq
    cmp x4, #1000
    b.hs .sk
    strb w3, [x1], #1
    add x4, x4, #1
.sk:
    add x0, x0, #1
    b .tlp
.inq:
    add x0, x0, #1
.qlp:
    ldrb w3, [x0]
    cbz w3, .done
    add x0, x0, #1
    cmp w3, #34
    b.eq .tlp
    cmp x4, #1000
    b.hs .qlp
    strb w3, [x1], #1
    add x4, x4, #1
    b .qlp
.done:
    strb wzr, [x1]
    sub x1, x1, x2
    ret
@endproc

;; @proc ua64_parse_u32
;; @abi arm64_ms
;; @in x0 = ASCII decimal digit string
;; @out w0 = parsed u32 value, x1 = pointer to first non-digit
;; @err none
;; @clobbers x0, x1, x2, x3
;; @desc Parses unsigned decimal integer from x0.
@proc ua64_parse_u32
    mov x1, x0
    mov w0, #0
    mov w3, #10
.lp:
    ldrb w2, [x1]
    cmp w2, #48
    b.lo .dn
    cmp w2, #57
    b.hi .dn
    sub w2, w2, #48
    madd w0, w0, w3, w2
    add x1, x1, #1
    b .lp
.dn:
    ret
@endproc

;; @proc ua64_get_attr
;; @abi arm64_ms
;; @in x0 = UTF-8 path
;; @out w0 = Win32 file attributes or 0xFFFFFFFF
;; @err w0 = 0xFFFFFFFF
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries file attributes via GetFileAttributesW.
@proc ua64_get_attr
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    call_iat __imp_GetFileAttributesW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_set_attr
;; @abi arm64_ms
;; @in x0 = UTF-8 path, w1 = Win32 attributes
;; @out w0 = non-zero on success, 0 on failure
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Sets file attributes via SetFileAttributesW.
@proc ua64_set_attr
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov w19, w1
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    mov w1, w19
    call_iat __imp_SetFileAttributesW
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_copy_file
;; @abi arm64_ms
;; @in x0 = UTF-8 src, x1 = UTF-8 dst, w2 = bFailIfExists
;; @out w0 = non-zero on success, 0 on failure
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Copies a file via CopyFileW.
@proc ua64_copy_file
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x1
    mov w20, w2
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    mov x0, x19
    adr_sym x1, g_ua64_wbuf2
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    adr_sym x1, g_ua64_wbuf2
    mov w2, w20
    call_iat __imp_CopyFileW
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_mkdir
;; @abi arm64_ms
;; @in x0 = UTF-8 directory path
;; @out w0 = non-zero on success, 0 on failure
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Creates directory via CreateDirectoryW.
@proc ua64_mkdir
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    mov x1, #0
    call_iat __imp_CreateDirectoryW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_delete_file
;; @abi arm64_ms
;; @in x0 = UTF-8 file path
;; @out w0 = non-zero on success, 0 on failure
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Deletes file via DeleteFileW.
@proc ua64_delete_file
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    call_iat __imp_DeleteFileW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_get_cwd
;; @abi arm64_ms
;; @in x0 = UTF-8 dst buffer, x1 = capacity
;; @out x0 = UTF-8 byte length
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries current working directory in UTF-8.
@proc ua64_get_cwd
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    mov x20, x1
    mov w0, #1000
    adr_sym x1, g_ua64_wbuf1
    call_iat __imp_GetCurrentDirectoryW
    adr_sym x0, g_ua64_wbuf1
    mov x1, x19
    mov x2, x20
    bl ua64_w_to_u8
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_get_env
;; @abi arm64_ms
;; @in x0 = UTF-8 var name, x1 = UTF-8 dst, x2 = capacity
;; @out x0 = UTF-8 byte length (0 if not set)
;; @err x0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Queries environment variable via GetEnvironmentVariableW.
@proc ua64_get_env
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x1
    mov x20, x2
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    adr_sym x1, g_ua64_wbuf2
    mov w2, #1000
    call_iat __imp_GetEnvironmentVariableW
    cbz w0, .not_set
    adr_sym x0, g_ua64_wbuf2
    mov x1, x19
    mov x2, x20
    bl ua64_w_to_u8
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
.not_set:
    strb wzr, [x19]
    mov x0, #0
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_open_read
;; @abi arm64_ms
;; @in x0 = UTF-8 path
;; @out x0 = 64-bit HANDLE or -1
;; @err x0 = -1
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Opens file or directory for reading / metadata query.
@proc ua64_open_read
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    mov_imm32 w1, 0x80000000
    mov w2, #3
    mov x3, #0
    mov w4, #3 ; OPEN_EXISTING
    mov_imm32 w5, 0x02000080
    mov x6, #0
    call_iat __imp_CreateFileW
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_load_stream
;; @abi arm64_ms
;; @in x0 = UTF-8 path (empty=stdin), x1 = dst buffer, x2 = max bytes, w3 = decode_bom (1 or 0)
;; @out x0 = bytes stored in x1, w1 = 0 on success or 1 on open error
;; @err w1 = 1 on open error
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Reads file or stdin up to x2 bytes and normalizes UTF-8 / UTF-16LE / UTF-16BE BOMs.
@proc ua64_load_stream
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!

    mov x20, x1 ; dst
    mov x21, x2 ; max_bytes
    mov w22, w3 ; decode_bom
    mov w23, #0 ; close_flag

    ldrb w4, [x0]
    cbnz w4, .open_f
    adr_sym x4, g_ua64_stdin
    ldr x19, [x4]
    b .rd_init

.open_f:
    bl ua64_open_read
    cmn x0, #1
    b.eq .fail
    mov x19, x0
    mov w23, #1

.rd_init:
    mov x24, #0 ; total
.rd_lp:
    subs x2, x21, x24
    b.eq .eof
    mov_imm32 x4, 32768
    cmp x2, x4
    b.ls .do_rd
    mov x2, x4
.do_rd:
    mov x0, x19
    add x1, x20, x24
    adr_sym x3, g_ua64_io_done
    mov x4, #0
    call_iat __imp_ReadFile
    cbz w0, .eof
    adr_sym x4, g_ua64_io_done
    ldr w0, [x4]
    cbz w0, .eof
    add x24, x24, x0
    b .rd_lp

.eof:
    cbz w23, .chk_bom
    mov x0, x19
    call_iat __imp_CloseHandle

.chk_bom:
    strb wzr, [x20, x24]
    add x4, x24, #1
    strb wzr, [x20, x4]
    cbz w22, .ok_dn
    cmp x24, #2
    b.lo .ok_dn

    cmp x24, #3
    b.lo .chk_u16le
    ldrb w0, [x20]
    cmp w0, #0xEF
    b.ne .chk_u16le
    ldrb w0, [x20, #1]
    cmp w0, #0xBB
    b.ne .chk_u16le
    ldrb w0, [x20, #2]
    cmp w0, #0xBF
    b.ne .chk_u16le
    sub x24, x24, #3
    mov x1, #0
.sh_u8:
    cmp x1, x24
    b.hi .ok_dn
    add x2, x1, #3
    ldrb w0, [x20, x2]
    strb w0, [x20, x1]
    add x1, x1, #1
    b .sh_u8

.chk_u16le:
    ldrb w0, [x20]
    cmp w0, #0xFF
    b.ne .chk_u16be
    ldrb w0, [x20, #1]
    cmp w0, #0xFE
    b.ne .chk_u16be
    b .cvt_u16

.chk_u16be:
    ldrb w0, [x20]
    cmp w0, #0xFE
    b.ne .ok_dn
    ldrb w0, [x20, #1]
    cmp w0, #0xFF
    b.ne .ok_dn
    mov x1, #2
.sw_be:
    add x2, x1, #1
    cmp x2, x24
    b.hs .cvt_u16
    ldrb w0, [x20, x1]
    ldrb w3, [x20, x2]
    strb w3, [x20, x1]
    strb w0, [x20, x2]
    add x1, x1, #2
    b .sw_be

.cvt_u16:
    adr_sym x4, g_ua64_arena
    ldr x4, [x4]
    mov_imm32 x5, 262144
    add x4, x4, x5
    sub x24, x24, #2
    mov x1, #0
.cp_w:
    cmp x1, x24
    b.hs .cp_w_dn
    add x2, x1, #2
    ldrb w0, [x20, x2]
    strb w0, [x4, x1]
    add x1, x1, #1
    b .cp_w
.cp_w_dn:
    strh wzr, [x4, x1]
    mov x0, x4
    mov x1, x20
    mov x2, x21
    bl ua64_w_to_u8
    mov x24, x0

.ok_dn:
    mov x0, x24
    mov w1, #0
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
.fail:
    mov x0, #0
    mov w1, #1
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_save_file
;; @abi arm64_ms
;; @in x0 = UTF-8 path, x1 = buffer, x2 = byte len
;; @out w0 = 1 on success, 0 on error
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Creates/overwrites file x0 and writes x2 bytes from x1.
@proc ua64_save_file
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    mov x20, x1
    mov x21, x2
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    mov_imm32 w1, 0x40000000
    mov w2, #0
    mov x3, #0
    mov w4, #2 ; CREATE_ALWAYS
    mov w5, #0x80
    mov x6, #0
    call_iat __imp_CreateFileW
    cmn x0, #1
    b.eq .fail
    mov x19, x0
    mov x0, x19
    mov x1, x20
    mov x2, x21
    adr_sym x3, g_ua64_io_done
    mov x4, #0
    call_iat __imp_WriteFile
    mov x0, x19
    call_iat __imp_CloseHandle
    mov w0, #1
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
.fail:
    mov w0, #0
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_find_unpack
;; @abi arm64_ms
;; @in x0 = WIN32_FIND_DATAW (592B), x1 = 544B record (0:attr, 8:ft64, 16:size64, 24:UTF-8 name[520])
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Unpacks WIN32_FIND_DATAW into 64-bit aligned 544-byte record.
@proc ua64_find_unpack
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    ldr w2, [x0]
    str w2, [x1]
    ldr w2, [x0, #20]
    str w2, [x1, #8]
    ldr w2, [x0, #24]
    str w2, [x1, #12]
    ldr w2, [x0, #28] ; nFileSizeHigh
    ldr w3, [x0, #32] ; nFileSizeLow
    orr x3, x3, x2, lsl #32
    str x3, [x1, #16]
    add x0, x0, #44
    add x1, x1, #24
    mov x2, #516
    bl ua64_w_to_u8
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_find_first
;; @abi arm64_ms
;; @in x0 = UTF-8 wildcard pattern, x1 = 544B entry buffer
;; @out x0 = search handle (-1 if no match)
;; @err x0 = -1
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Starts directory enumeration via FindFirstFileW.
@proc ua64_find_first
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x20, x1
    adr_sym x1, g_ua64_wbuf1
    bl ua64_u8_to_w
    adr_sym x0, g_ua64_wbuf1
    adr_sym x1, g_ua64_find_raw
    call_iat __imp_FindFirstFileW
    cmn x0, #1
    b.eq .dn
    mov x19, x0
    adr_sym x0, g_ua64_find_raw
    mov x1, x20
    bl ua64_find_unpack
    mov x0, x19
.dn:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_find_next
;; @abi arm64_ms
;; @in x0 = search handle, x1 = 544B entry buffer
;; @out w0 = non-zero on match, 0 when exhausted
;; @err w0 = 0
;; @clobbers x0, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
;; @desc Advances directory enumeration via FindNextFileW.
@proc ua64_find_next
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x20, x1
    adr_sym x1, g_ua64_find_raw
    call_iat __imp_FindNextFileW
    cbz w0, .dn
    mov w19, w0
    adr_sym x0, g_ua64_find_raw
    mov x1, x20
    bl ua64_find_unpack
    mov w0, w19
.dn:
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc

;; @proc ua64_path_join
;; @abi arm64_ms
;; @in x0 = dst buffer (1024B), x1 = dir UTF-8, x2 = leaf UTF-8
;; @out none
;; @err none
;; @clobbers x0, x1, x2, x3, x4, x5
;; @desc Joins directory x1 and leaf x2 into x0 with single backslash separator.
@proc ua64_path_join
    hint #25
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    mov x20, x2
    bl ua64_strcpy
    cbz x0, .only_leaf
    sub x1, x0, #1
    ldrb w2, [x19, x1]
    cmp w2, #0x5C
    b.eq .at_sep
    cmp w2, #47
    b.eq .at_sep
    cmp w2, #58
    b.eq .at_sep
    mov w2, #0x5C
    strb w2, [x19, x0]
    add x0, x0, #1
.at_sep:
    add x0, x19, x0
    mov x1, x20
    bl ua64_strcpy
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
.only_leaf:
    mov x0, x19
    mov x1, x20
    bl ua64_strcpy
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    hint #29
    ret
@endproc
