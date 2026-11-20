;; @file wincmd64.asm
;; @isa x64
;; @desc CRT-free x86-64 PE32+ (Subsystem 5.02) shell with .pdata/.xdata SEH for XP x64..Windows 11.

@cpu x64
@format coff64
@global mainCRTStartup

@include "../inc/err_code.inc"
@include "../inc/win_equ.inc"
@include "../inc/pe_strc.inc"

@extern __imp_GetStdHandle, qword
@extern __imp_WriteFile, qword
@extern __imp_GetCommandLineA, qword
@extern __imp_GetVersion, qword
@extern __imp_ExitProcess, qword

@section text

;; @proc w64_write_stdout
;; @abi win64_ms
;; @in RCX = buffer pointer, RDX = byte count
;; @out Writes EDX bytes to STD_OUTPUT_HANDLE via WriteFile
;; @err none
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc Microsoft x64 ABI console writer with 32B shadow space + 5th stack arg + .pdata SEH.
@proc_frame w64_write_stdout, 40, rbx, rsi
    mov rsi, rcx
    mov ebx, edx

    mov ecx, -11
    call qword [rel __imp_GetStdHandle]

    mov rcx, rax
    mov rdx, rsi
    mov r8d, ebx
    lea r9, [rsp + 36]
    mov qword [rsp + 32], 0
    call qword [rel __imp_WriteFile]

    @epilog_ret
@endproc

;; @proc w64_str_len
;; @abi win64_ms
;; @in RCX = NUL-terminated string
;; @out EDX = byte length
;; @err none
;; @clobbers rax, rdx
;; @desc Leaf string-length helper on x86-64.
@proc w64_str_len
    xor edx, edx
.lp:
    mov al, byte [rcx + rdx]
    test al, al
    jz .done
    inc edx
    jmp .lp
.done:
    ret
@endproc

;; @proc w64_skip_exe_token
;; @abi win64_ms
;; @in RCX = raw GetCommandLineA pointer
;; @out RCX = pointer to switches/arguments after executable path
;; @err none
;; @clobbers rax, rcx
;; @desc Skips quoted or unquoted argv[0] program path and trailing spaces on x64.
@proc w64_skip_exe_token
    mov al, byte [rcx]
    cmp al, '"'
    je .quoted
.unq_lp:
    mov al, byte [rcx]
    test al, al
    jz .done
    cmp al, 0x20
    je .skip_ws
    cmp al, 0x09
    je .skip_ws
    inc rcx
    jmp .unq_lp
.quoted:
    inc rcx
.q_lp:
    mov al, byte [rcx]
    test al, al
    jz .done
    inc rcx
    cmp al, '"'
    jne .q_lp
.skip_ws:
    mov al, byte [rcx]
    cmp al, 0x20
    je .ws_adv
    cmp al, 0x09
    je .ws_adv
    jmp .done
.ws_adv:
    inc rcx
    jmp .skip_ws
.done:
    ret
@endproc

;; @proc mainCRTStartup
;; @abi win64_ms
;; @in Process entrypoint invoked by Windows XP Pro x64 through Windows 11 loader
;; @out Terminates via ExitProcess(ECX)
;; @err Exits with command status
;; @clobbers rax, rcx, rdx, r8, r9, r10, r11
;; @desc CRT-free PE32+ framed entrypoint (16B RSP aligned, .pdata/.xdata SEH unwind info).
@proc_frame mainCRTStartup, 32, rsi
    call qword [rel __imp_GetCommandLineA]
    mov rcx, rax
    call w64_skip_exe_token
    mov rsi, rcx

.sw_lp:
    mov al, byte [rsi]
    test al, al
    jz .done_ok
    cmp al, 0x20
    je .adv_ws
    cmp al, '/'
    jne .verb
    mov ah, byte [rsi + 1]
    cmp ah, '?'
    je .help
    and ah, 0xDF
    cmp ah, 'C'
    je .skip_ck
    cmp ah, 'K'
    je .skip_ck
    add rsi, 2
    jmp .sw_lp
.adv_ws:
    inc rsi
    jmp .sw_lp
.skip_ck:
    add rsi, 2
.ws2:
    cmp byte [rsi], 0x20
    jne .verb
    inc rsi
    jmp .ws2

.help:
    lea rcx, [rel szW64Help]
    call w64_str_len
    call w64_write_stdout
    xor ecx, ecx
    jmp .exit_now

.verb:
    mov al, byte [rsi]
    test al, al
    jz .done_ok
    and al, 0xDF
    cmp al, 'V'
    je .ver
    cmp al, 'E'
    je .echo
    cmp al, 'R'
    je .done_ok
    lea rcx, [rel szW64BadCmd]
    call w64_str_len
    call w64_write_stdout
    mov ecx, 1
    jmp .exit_now

.ver:
    call qword [rel __imp_GetVersion]
    lea rcx, [rel szW64Ver]
    call w64_str_len
    call w64_write_stdout
    xor ecx, ecx
    jmp .exit_now

.echo:
    add rsi, 4
    cmp byte [rsi], 0x20
    jne .echo_out
    inc rsi
.echo_out:
    mov rcx, rsi
    call w64_str_len
    call w64_write_stdout
    lea rcx, [rel szW64CrLf]
    mov edx, 2
    call w64_write_stdout
    xor ecx, ecx
    jmp .exit_now

.done_ok:
    xor ecx, ecx

.exit_now:
    call qword [rel __imp_ExitProcess]
    @epilog_ret
@endproc

@section rdata

@data db szW64Ver, "WINCMD64 Pure-ASM Shell [Windows XP x64 - Windows 11 PE32+]", 0x0D, 0x0A, 0
@data db szW64Help, "WINCMD64 [/C cmd] [/K cmd] [/Q] [/D] [/E:ON|OFF] [/V:ON|OFF]", 0x0D, 0x0A, 0
@data db szW64BadCmd, "Bad command or file name", 0x0D, 0x0A, 0
@data db szW64CrLf, 0x0D, 0x0A, 0
