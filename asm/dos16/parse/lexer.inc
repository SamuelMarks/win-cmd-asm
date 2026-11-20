;; @file lexer.inc
;; @isa 8086
;; @desc Phase 2a context-sensitive batch tokenizer (quotes, ^ escapes, DBCS/UTF-8, @, REM, ECHO<d>).

@cpu 8086
@format inc

;; @const TOK_EOF
;; @desc End of input or newline boundary token.
@const TOK_EOF = 0x00
;; @const TOK_LPAREN
;; @desc Unquoted '(' compound block opener.
@const TOK_LPAREN = 0x01
;; @const TOK_RPAREN
;; @desc Unquoted ')' compound block closer.
@const TOK_RPAREN = 0x02
;; @const TOK_AMP
;; @desc Unquoted '&' sequential command separator.
@const TOK_AMP = 0x03
;; @const TOK_AND
;; @desc Unquoted '&&' conditional success operator.
@const TOK_AND = 0x04
;; @const TOK_OR
;; @desc Unquoted '||' conditional failure operator.
@const TOK_OR = 0x05
;; @const TOK_PIPE
;; @desc Unquoted '|' pipeline operator.
@const TOK_PIPE = 0x06
;; @const TOK_NL
;; @desc Embedded newline separator inside multiline (...) compound block.
@const TOK_NL = 0x07
;; @const TOK_WORD
;; @desc Command/argument word token.
@const TOK_WORD = 0x08

;; @proc lex_skip_hspace
;; @abi dos16
;; @in DS:SI = input cursor
;; @out DS:SI advanced past horizontal spaces (0x20, 0x09), AL = current byte
;; @err none
;; @clobbers ax, si
;; @desc Skips horizontal whitespace without crossing embedded CR/LF block separators.
@proc lex_skip_hspace
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

;; @proc lex_peek_op
;; @abi dos16
;; @in DS:SI = input cursor (already past horizontal whitespace)
;; @out AL = TOK_* (TOK_EOF, TOK_NL, TOK_LPAREN, TOK_RPAREN, TOK_AND, TOK_OR, TOK_PIPE, TOK_AMP, or TOK_WORD),
;; @out AH = operator byte length (0, 1, or 2)
;; @err none
;; @clobbers ax
;; @desc Classifies unquoted structural operator at DS:SI without consuming it.
@proc lex_peek_op
    mov al, byte [si]
    test al, al
    jz .is_eof
    cmp al, 0x0D
    je .is_nl
    cmp al, 0x0A
    je .is_nl
    cmp al, '('
    je .is_lp
    cmp al, ')'
    je .is_rp
    cmp al, '&'
    je .is_amp
    cmp al, '|'
    je .is_bar
    mov al, TOK_WORD
    xor ah, ah
    ret

.is_eof:
    mov al, TOK_EOF
    xor ah, ah
    ret
.is_nl:
    mov al, TOK_NL
    mov ah, 1
    cmp byte [si], 0x0D
    jne .nl_done
    cmp byte [si + 1], 0x0A
    jne .nl_done
    mov ah, 2
.nl_done:
    ret
.is_lp:
    mov al, TOK_LPAREN
    mov ah, 1
    ret
.is_rp:
    mov al, TOK_RPAREN
    mov ah, 1
    ret
.is_amp:
    cmp byte [si + 1], '&'
    je .is_and2
    mov al, TOK_AMP
    mov ah, 1
    ret
.is_and2:
    mov al, TOK_AND
    mov ah, 2
    ret
.is_bar:
    cmp byte [si + 1], '|'
    je .is_or2
    mov al, TOK_PIPE
    mov ah, 1
    ret
.is_or2:
    mov al, TOK_OR
    mov ah, 2
    ret
@endproc

;; @proc lex_is_echo_delim
;; @abi dos16
;; @in AL = byte immediately following "ECHO"
;; @out CF=0 if AL is a classic ECHO<delim> char (. ( / : + = [ ] ; \), CF=1 otherwise
;; @err none
;; @clobbers none
;; @desc Tests the 10 special non-whitespace ECHO delimiters of COMMAND.COM / cmd.exe.
@proc lex_is_echo_delim
    cmp al, '.'
    je .yes
    cmp al, '('
    je .yes
    cmp al, '/'
    je .yes
    cmp al, ':'
    je .yes
    cmp al, '+'
    je .yes
    cmp al, '='
    je .yes
    cmp al, '['
    je .yes
    cmp al, ']'
    je .yes
    cmp al, ';'
    je .yes
    cmp al, 0x5C
    je .yes
    stc
    ret
.yes:
    clc
    ret
@endproc

;; @proc lex_read_word
;; @abi dos16
;; @in DS:SI = cursor, DS:DI = 128B token buffer
;; @out DS:DI = NUL-terminated word (quotes preserved for downstream arg/modifier use),
;; @out CX = length, DS:SI = advanced, CF=0 (or CF=1 if at structural operator / EOF)
;; @err CF=1 if no word token at DS:SI
;; @clobbers ax, bx, cx, dx, si, di
;; @desc Reads one whitespace/operator-delimited word honoring quotes, ^ escapes, DBCS & UTF-8.
@proc lex_read_word
    call lex_skip_hspace
    call lex_peek_op
    cmp al, TOK_WORD
    jne .j_no_word
    mov al, byte [si]
    cmp al, '<'
    je .j_no_word
    cmp al, '>'
    jne .start_w
.j_no_word:
    jmp .no_word
.start_w:

    push bp
    mov bp, di
    xor cx, cx
    xor dl, dl ; quote state (0=unquoted, 1=inside "...")

    push es
    mov es, word [wTrnResSeg]
    mov dh, byte [es:resState + RES_STATE.bDialect]
    pop es

.w_lp:
    mov al, byte [si]
    test al, al
    jz .j_w_done
    cmp al, '"'
    jne .not_q
    xor dl, 1
    jmp .emit_ch

.not_q:
    test dl, dl
    jnz .chk_mb

    ; Unquoted: check whitespace, newline, structural operators, and redirections
    cmp al, 0x20
    je .j_w_done
    cmp al, 0x09
    je .j_w_done
    cmp al, 0x0D
    je .j_w_done
    cmp al, 0x0A
    je .j_w_done
    cmp al, '&'
    je .j_w_done
    cmp al, '|'
    je .j_w_done
    cmp al, '('
    je .j_w_done
    cmp al, ')'
    je .j_w_done
    cmp al, '<'
    je .j_w_done
    cmp al, '>'
    jne .chk_w_caret
.j_w_done:
    jmp .w_done

.chk_w_caret:
    ; Unquoted '^' in NT_CMD/FREEDOS (dh >= 5) escapes next character so ^& ^| ^> stay in word!
    cmp al, '^'
    jne .chk_mb
    cmp dh, 5
    jb .chk_mb
    cmp byte [si + 1], 0
    je .emit_ch
    cmp byte [si + 1], 0x0D
    je .emit_ch
    cmp byte [si + 1], 0x0A
    je .emit_ch
    ; Keep '^' + escaped char intact in AST command text so Phase 4 strips '^' at execution!
    cmp cx, 124
    jae .sk_pair
    mov byte [di], al
    inc di
    inc cx
    inc si
    mov al, byte [si]
    jmp .emit_ch
.sk_pair:
    add si, 2
    jmp .w_lp

.chk_mb:
    ; DBCS 2-byte lead+trail atomic copy
    push ax
    push cx
    call dbcs_is_lead
    pop cx
    pop ax
    jnc .chk_utf8
    cmp cx, 124
    jae .adv_dbcs
    mov byte [di], al
    inc di
    inc cx
    inc si
    mov al, byte [si]
    test al, al
    jz .w_done
    jmp .emit_ch
.adv_dbcs:
    inc si
    cmp byte [si], 0
    je .w_done
    inc si
    jmp .w_lp

.chk_utf8:
    ; Multi-byte UTF-8 lead (0xC2..0xF4): copy continuation bytes (0x80..0xBF) atomically
    cmp al, 0xC2
    jb .emit_ch
    cmp al, 0xF4
    ja .emit_ch
    cmp byte [si + 1], 0x80
    jb .emit_ch
    cmp byte [si + 1], 0xBF
    ja .emit_ch
.u8_cont:
    cmp cx, 125
    jae .u8_sk
    mov byte [di], al
    inc di
    inc cx
.u8_sk:
    inc si
    mov al, byte [si]
    cmp al, 0x80
    jb .j_w_lp
    cmp al, 0xBF
    jbe .u8_cont
.j_w_lp:
    jmp .w_lp

.emit_ch:
    cmp cx, 125
    jae .just_adv
    mov byte [di], al
    inc di
    inc cx
.just_adv:
    inc si
    jmp .w_lp

.w_done:
    mov byte [di], 0
    mov di, bp
    pop bp
    clc
    ret

.no_word:
    stc
    ret
@endproc

;; @proc lex_count_paren_delta
;; @abi dos16
;; @in DS:SI = physical line, CX = current open paren depth
;; @out CX = updated open paren depth (>= 0)
;; @err none
;; @clobbers ax, bx, cx, dx, si
;; @desc Counts unquoted, unescaped '(' and ')' outside REM/:: comments for multiline blocks.
@proc lex_count_paren_delta
    call lex_skip_hspace
    cmp al, ':'
    je .done
    ; Check leading REM comment
    mov ah, al
    and ah, 0xDF
    cmp ah, 'R'
    jne .scan
    mov ah, byte [si + 1]
    and ah, 0xDF
    cmp ah, 'E'
    jne .scan
    mov ah, byte [si + 2]
    and ah, 0xDF
    cmp ah, 'M'
    jne .scan
    mov ah, byte [si + 3]
    cmp ah, 0
    je .done
    cmp ah, 0x20
    je .done
    cmp ah, 0x09
    je .done

.scan:
    xor dl, dl ; quote toggle
.lp:
    mov al, byte [si]
    test al, al
    jz .done
    cmp al, '"'
    jne .nq
    xor dl, 1
    inc si
    jmp .lp
.nq:
    test dl, dl
    jnz .adv
    cmp al, '^'
    jne .chk_p
    cmp byte [si + 1], 0
    je .done
    add si, 2
    jmp .lp
.chk_p:
    cmp al, '('
    jne .chk_rp
    inc cx
    inc si
    jmp .lp
.chk_rp:
    cmp al, ')'
    jne .adv
    test cx, cx
    jz .adv
    dec cx
.adv:
    inc si
    jmp .lp
.done:
    ret
@endproc
