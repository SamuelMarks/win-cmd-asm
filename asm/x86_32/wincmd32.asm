;; @file wincmd32.asm
;; @isa 386
;; @desc CRT-free IA-32 PE32 (Subsystem 3.10) shell for Windows NT 3.1..11 and Windows 95/98/Me.

@cpu 386
@format coff32
@global _mainCRTStartup

@include "../inc/err_code.inc"
@include "../inc/win_equ.inc"
@include "../inc/pe_strc.inc"

@extern __imp__GetStdHandle@4, dword
@extern __imp__WriteFile@20, dword
@extern __imp__GetCommandLineA@0, dword
@extern __imp__GetVersion@0, dword
@extern __imp__ExitProcess@4, dword

@section text

;; @proc w32_write_stdout
;; @abi win32
;; @in ESI = buffer pointer, ECX = byte count
;; @out Writes ECX bytes to STD_OUTPUT_HANDLE via WriteFile
;; @err none
;; @clobbers eax, ecx, edx
;; @desc CRT-free console output helper compatible with Windows NT 3.1+ and Windows 95+.
@proc w32_write_stdout
    push ebp
    push ebx
    mov ebp, esp
    sub esp, 4
    mov ebx, ecx

    push -11
    call dword [__imp__GetStdHandle@4]

    lea edx, [ebp - 4]
    push 0
    push edx
    push ebx
    push esi
    push eax
    call dword [__imp__WriteFile@20]

    add esp, 4
    pop ebx
    pop ebp
    ret
@endproc

;; @proc w32_str_len
;; @abi win32
;; @in ESI = NUL-terminated string
;; @out ECX = byte length (excluding NUL)
;; @err none
;; @clobbers eax, ecx
;; @desc Computes ASCII/UTF-8 byte length on pure i386 baseline without CRT strlen.
@proc w32_str_len
    xor ecx, ecx
.lp:
    mov al, byte [esi + ecx]
    test al, al
    jz .done
    inc ecx
    jmp .lp
.done:
    ret
@endproc

;; @proc w32_skip_exe_token
;; @abi win32
;; @in EAX = raw GetCommandLineA pointer
;; @out EAX = pointer to switches/arguments after executable path
;; @err none
;; @clobbers eax, edx
;; @desc Skips quoted or unquoted argv[0] program path and trailing spaces.
@proc w32_skip_exe_token
    mov dl, byte [eax]
    cmp dl, '"'
    je .quoted
.unq_lp:
    mov dl, byte [eax]
    test dl, dl
    jz .done
    cmp dl, 0x20
    je .skip_ws
    cmp dl, 0x09
    je .skip_ws
    inc eax
    jmp .unq_lp
.quoted:
    inc eax
.q_lp:
    mov dl, byte [eax]
    test dl, dl
    jz .done
    inc eax
    cmp dl, '"'
    jne .q_lp
.skip_ws:
    mov dl, byte [eax]
    cmp dl, 0x20
    je .ws_adv
    cmp dl, 0x09
    je .ws_adv
    jmp .done
.ws_adv:
    inc eax
    jmp .skip_ws
.done:
    ret
@endproc

;; @proc w32_exec_tail
;; @abi win32
;; @in EAX = command arguments after argv[0]
;; @out EAX = exit code (0 = success)
;; @err Returns non-zero exit code on unrecognized command
;; @clobbers eax, ecx, edx
;; @desc Evaluates /?, /C VER, /C ECHO <msg>, /C REM, or default version query.
@proc w32_exec_tail
    push ebx
    push esi
    mov esi, eax
    xor ebx, ebx
.sw_lp:
    mov al, byte [esi]
    test al, al
    jz .done_ok
    cmp al, 0x20
    je .adv_ws
    cmp al, '/'
    jne .dispatch_verb
    mov ah, byte [esi + 1]
    cmp ah, '?'
    je .show_help
    and ah, 0xDF
    cmp ah, 'C'
    je .skip_c
    cmp ah, 'K'
    je .skip_c
    add esi, 2
    jmp .sw_lp
.adv_ws:
    inc esi
    jmp .sw_lp
.skip_c:
    add esi, 2
.ws2:
    cmp byte [esi], 0x20
    jne .dispatch_verb
    inc esi
    jmp .ws2

.show_help:
    mov esi, addr(szW32Help)
    call w32_str_len
    call w32_write_stdout
    xor eax, eax
    pop esi
    pop ebx
    ret

.dispatch_verb:
    mov al, byte [esi]
    test al, al
    jz .done_ok
    and al, 0xDF
    cmp al, 'V'
    je .do_ver
    cmp al, 'E'
    je .do_echo
    cmp al, 'R'
    je .done_ok
    mov esi, addr(szW32BadCmd)
    call w32_str_len
    call w32_write_stdout
    mov eax, 1
    pop esi
    pop ebx
    ret

.do_ver:
    call dword [__imp__GetVersion@0]
    test eax, 0x80000000
    jnz .ver_9x
    mov esi, addr(szW32VerNt)
    jmp .emit_ver
.ver_9x:
    mov esi, addr(szW32Ver9x)
.emit_ver:
    call w32_str_len
    call w32_write_stdout
    xor eax, eax
    pop esi
    pop ebx
    ret

.do_echo:
    ; Advance past "ECHO" + single space
    add esi, 4
    cmp byte [esi], 0x20
    jne .echo_print
    inc esi
.echo_print:
    call w32_str_len
    call w32_write_stdout
    mov esi, addr(szW32CrLf)
    mov ecx, 2
    call w32_write_stdout
    xor eax, eax
    pop esi
    pop ebx
    ret

.done_ok:
    xor eax, eax
    pop esi
    pop ebx
    ret
@endproc

;; @proc _mainCRTStartup
;; @abi win32
;; @in Process entrypoint invoked directly by NT 3.1..11 or Win95..Me loader
;; @out Terminates via ExitProcess(EAX)
;; @err Exits with command status
;; @clobbers eax, ecx, edx
;; @desc CRT-free PE32 console entrypoint (MajorSubsystemVersion=3, MinorSubsystemVersion=10).
@proc _mainCRTStartup
    push ebp
    mov ebp, esp

    call dword [__imp__GetCommandLineA@0]
    call w32_skip_exe_token
    call w32_exec_tail

    push eax
    call dword [__imp__ExitProcess@4]
    pop ebp
    ret
@endproc

@section rdata

@data db szW32VerNt, "WINCMD32 Pure-ASM Shell [Windows NT 3.1-11 IA-32 PE32]", 0x0D, 0x0A, 0
@data db szW32Ver9x, "WINCMD32 Pure-ASM Shell [Windows 95/98/Me IA-32 PE32]", 0x0D, 0x0A, 0
@data db szW32Help, "WINCMD32 [/C cmd] [/K cmd] [/Q] [/D] [/E:ON|OFF] [/V:ON|OFF]", 0x0D, 0x0A, 0
@data db szW32BadCmd, "Bad command or file name", 0x0D, 0x0A, 0
@data db szW32CrLf, 0x0D, 0x0A, 0
