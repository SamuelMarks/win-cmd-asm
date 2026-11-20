;; @file 80286.inc
;; @isa 286
;; @desc Strict FASM Intel 80286 instruction filter rejecting 80386+ mnemonics.

macro movzx a*,b* { err 'movzx forbidden in 80286 mode' }
macro movsx a*,b* { err 'movsx forbidden in 80286 mode' }
macro bswap a* { err 'bswap forbidden in 80286 mode' }
macro shld a*,b*,c* { err 'shld forbidden in 80286 mode' }
macro shrd a*,b*,c* { err 'shrd forbidden in 80286 mode' }
