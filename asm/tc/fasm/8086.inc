;; @file 8086.inc
;; @isa 8086
;; @desc Strict FASM 1978 Intel 8086 instruction filter rejecting 80186+/80286+/80386+ mnemonics.

macro pusha { err 'pusha forbidden in strict 8086 mode' }
macro popa  { err 'popa forbidden in strict 8086 mode' }
macro enter a*,b* { err 'enter forbidden in strict 8086 mode' }
macro leave { err 'leave forbidden in strict 8086 mode' }
macro movzx a*,b* { err 'movzx forbidden in strict 8086 mode' }
macro movsx a*,b* { err 'movsx forbidden in strict 8086 mode' }
macro bswap a* { err 'bswap forbidden in strict 8086 mode' }
