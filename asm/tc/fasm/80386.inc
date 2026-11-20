;; @file 80386.inc
;; @isa 386
;; @desc Strict FASM Intel i386 baseline instruction filter rejecting 486+/Pentium/P6 mnemonics.

macro bswap a* { err 'bswap requires 80486+; forbidden in baseline i386 mode' }
macro xadd a*,b* { err 'xadd requires 80486+; forbidden in baseline i386 mode' }
macro cmpxchg a*,b* { err 'cmpxchg requires 80486+; forbidden in baseline i386 mode' }
macro cmpxchg8b a* { err 'cmpxchg8b requires Pentium; forbidden in baseline i386 mode' }
macro cpuid { err 'unguarded cpuid forbidden in baseline i386 mode' }
