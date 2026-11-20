;; @file pe_strc.inc
;; @isa all
;; @desc Exact Phase 19 assemble-time byte-asserted PE32, PE32+, SEH, and Win32/Win64 structures.

@format inc

;; @struc IMAGE_FILE_HEADER
;; @desc 20-byte COFF / PE file header.
@struc IMAGE_FILE_HEADER, 20
    @field Machine, word, 1
    @field NumberOfSections, word, 1
    @field TimeDateStamp, dword, 1
    @field PointerToSymbolTable, dword, 1
    @field NumberOfSymbols, dword, 1
    @field SizeOfOptionalHeader, word, 1
    @field Characteristics, word, 1
@endstruc

;; @struc IMAGE_OPTIONAL_HEADER32
;; @desc 224-byte PE32 Optional Header (IA-32, ARMv4, ARMv7).
@struc IMAGE_OPTIONAL_HEADER32, 224
    @field Magic, word, 1
    @field MajorLinkerVersion, byte, 1
    @field MinorLinkerVersion, byte, 1
    @field SizeOfCode, dword, 1
    @field SizeOfInitializedData, dword, 1
    @field SizeOfUninitializedData, dword, 1
    @field AddressOfEntryPoint, dword, 1
    @field BaseOfCode, dword, 1
    @field BaseOfData, dword, 1
    @field ImageBase, dword, 1
    @field SectionAlignment, dword, 1
    @field FileAlignment, dword, 1
    @field MajorOperatingSystemVersion, word, 1
    @field MinorOperatingSystemVersion, word, 1
    @field MajorImageVersion, word, 1
    @field MinorImageVersion, word, 1
    @field MajorSubsystemVersion, word, 1
    @field MinorSubsystemVersion, word, 1
    @field Win32VersionValue, dword, 1
    @field SizeOfImage, dword, 1
    @field SizeOfHeaders, dword, 1
    @field CheckSum, dword, 1
    @field Subsystem, word, 1
    @field DllCharacteristics, word, 1
    @field SizeOfStackReserve, dword, 1
    @field SizeOfStackCommit, dword, 1
    @field SizeOfHeapReserve, dword, 1
    @field SizeOfHeapCommit, dword, 1
    @field LoaderFlags, dword, 1
    @field NumberOfRvaAndSizes, dword, 1
    @field DataDirectory, qword, 16
@endstruc

;; @struc IMAGE_OPTIONAL_HEADER64
;; @desc 240-byte PE32+ Optional Header (x86-64 and ARM64).
@struc IMAGE_OPTIONAL_HEADER64, 240
    @field Magic, word, 1
    @field MajorLinkerVersion, byte, 1
    @field MinorLinkerVersion, byte, 1
    @field SizeOfCode, dword, 1
    @field SizeOfInitializedData, dword, 1
    @field SizeOfUninitializedData, dword, 1
    @field AddressOfEntryPoint, dword, 1
    @field BaseOfCode, dword, 1
    @field ImageBase, qword, 1
    @field SectionAlignment, dword, 1
    @field FileAlignment, dword, 1
    @field MajorOperatingSystemVersion, word, 1
    @field MinorOperatingSystemVersion, word, 1
    @field MajorImageVersion, word, 1
    @field MinorImageVersion, word, 1
    @field MajorSubsystemVersion, word, 1
    @field MinorSubsystemVersion, word, 1
    @field Win32VersionValue, dword, 1
    @field SizeOfImage, dword, 1
    @field SizeOfHeaders, dword, 1
    @field CheckSum, dword, 1
    @field Subsystem, word, 1
    @field DllCharacteristics, word, 1
    @field SizeOfStackReserve, qword, 1
    @field SizeOfStackCommit, qword, 1
    @field SizeOfHeapReserve, qword, 1
    @field SizeOfHeapCommit, qword, 1
    @field LoaderFlags, dword, 1
    @field NumberOfRvaAndSizes, dword, 1
    @field DataDirectory, qword, 16
@endstruc

;; @struc IMAGE_SECTION_HEADER
;; @desc 40-byte COFF / PE section table header.
@struc IMAGE_SECTION_HEADER, 40
    @field SecName, byte, 8
    @field VirtualSize, dword, 1
    @field VirtualAddress, dword, 1
    @field SizeOfRawData, dword, 1
    @field PointerToRawData, dword, 1
    @field PointerToRelocations, dword, 1
    @field PointerToLinenumbers, dword, 1
    @field NumberOfRelocations, word, 1
    @field NumberOfLinenumbers, word, 1
    @field Characteristics, dword, 1
@endstruc

;; @struc IMAGE_IMPORT_DESCRIPTOR
;; @desc 20-byte PE import directory table descriptor.
@struc IMAGE_IMPORT_DESCRIPTOR, 20
    @field OriginalFirstThunk, dword, 1
    @field TimeDateStamp, dword, 1
    @field ForwarderChain, dword, 1
    @field DllNameRva, dword, 1
    @field FirstThunk, dword, 1
@endstruc

;; @struc IMAGE_LOAD_CONFIG_DIRECTORY32
;; @desc 72-byte PE32 Load Configuration Directory supporting /SAFESEH handler tables.
@struc IMAGE_LOAD_CONFIG_DIRECTORY32, 72
    @field cbSize, dword, 1
    @field TimeDateStamp, dword, 1
    @field MajorVersion, word, 1
    @field MinorVersion, word, 1
    @field GlobalFlagsClear, dword, 1
    @field GlobalFlagsSet, dword, 1
    @field CriticalSectionDefaultTimeout, dword, 1
    @field DeCommitFreeBlockThreshold, dword, 1
    @field DeCommitTotalFreeThreshold, dword, 1
    @field LockPrefixTable, dword, 1
    @field MaximumAllocationSize, dword, 1
    @field VirtualMemoryThreshold, dword, 1
    @field ProcessHeapFlags, dword, 1
    @field ProcessAffinityMask, dword, 1
    @field CSDVersion, word, 1
    @field DependentLoadFlags, word, 1
    @field EditList, dword, 1
    @field SecurityCookie, dword, 1
    @field SEHandlerTable, dword, 1
    @field SEHandlerCount, dword, 1
@endstruc

;; @struc IMAGE_LOAD_CONFIG_DIRECTORY64
;; @desc 148-byte PE32+ Load Configuration Directory supporting Control Flow Guard (GUARD_CF).
@struc IMAGE_LOAD_CONFIG_DIRECTORY64, 148
    @field cbSize, dword, 1
    @field TimeDateStamp, dword, 1
    @field MajorVersion, word, 1
    @field MinorVersion, word, 1
    @field GlobalFlagsClear, dword, 1
    @field GlobalFlagsSet, dword, 1
    @field CriticalSectionDefaultTimeout, dword, 1
    @field DeCommitFreeBlockThreshold, qword, 1
    @field DeCommitTotalFreeThreshold, qword, 1
    @field LockPrefixTable, qword, 1
    @field MaximumAllocationSize, qword, 1
    @field VirtualMemoryThreshold, qword, 1
    @field ProcessAffinityMask, qword, 1
    @field ProcessHeapFlags, dword, 1
    @field CSDVersion, word, 1
    @field DependentLoadFlags, word, 1
    @field EditList, qword, 1
    @field SecurityCookie, qword, 1
    @field SEHandlerTable, qword, 1
    @field SEHandlerCount, qword, 1
    @field GuardCFCheckFunctionPointer, qword, 1
    @field GuardCFDispatchFunctionPointer, qword, 1
    @field GuardCFFunctionTable, qword, 1
    @field GuardCFFunctionCount, qword, 1
    @field GuardFlags, dword, 1
@endstruc

;; @struc RUNTIME_FUNCTION_X64
;; @desc 12-byte x86-64 .pdata exception directory entry.
@struc RUNTIME_FUNCTION_X64, 12
    @field BeginAddress, dword, 1
    @field EndAddress, dword, 1
    @field UnwindInfoAddress, dword, 1
@endstruc

;; @struc UNWIND_INFO_X64
;; @desc 4-byte x86-64 .xdata UNWIND_INFO fixed header.
@struc UNWIND_INFO_X64, 4
    @field VersionAndFlags, byte, 1
    @field SizeOfProlog, byte, 1
    @field CountOfCodes, byte, 1
    @field FrameRegisterAndOffset, byte, 1
@endstruc

;; @struc IMAGE_ARM_RUNTIME_FUNCTION_ENTRY
;; @desc 8-byte ARM32 Windows ARMNT .pdata unwind entry.
@struc IMAGE_ARM_RUNTIME_FUNCTION_ENTRY, 8
    @field BeginAddress, dword, 1
    @field UnwindData, dword, 1
@endstruc

;; @struc IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY
;; @desc 8-byte ARM64 Windows PE32+ .pdata packed/unpacked unwind entry.
@struc IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY, 8
    @field BeginAddress, dword, 1
    @field UnwindData, dword, 1
@endstruc

;; @struc WIN32_FIND_DATAW
;; @desc 592-byte WinNT / WinCE UTF-16LE FindFirstFileW / FindNextFileW buffer.
@struc WIN32_FIND_DATAW, 592
    @field dwFileAttributes, dword, 1
    @field ftCreationTime, qword, 1
    @field ftLastAccessTime, qword, 1
    @field ftLastWriteTime, qword, 1
    @field nFileSizeHigh, dword, 1
    @field nFileSizeLow, dword, 1
    @field dwReserved0, dword, 1
    @field dwReserved1, dword, 1
    @field cFileName, word, 260
    @field cAlternateFileName, word, 14
@endstruc

;; @struc STARTUPINFOW
;; @desc 68-byte 32-bit Win32 CreateProcess STARTUPINFOW/A structure.
@struc STARTUPINFOW, 68
    @field cb, dword, 1
    @field lpReserved, dword, 1
    @field lpDesktop, dword, 1
    @field lpTitle, dword, 1
    @field dwX, dword, 1
    @field dwY, dword, 1
    @field dwXSize, dword, 1
    @field dwYSize, dword, 1
    @field dwXCountChars, dword, 1
    @field dwYCountChars, dword, 1
    @field dwFillAttribute, dword, 1
    @field dwFlags, dword, 1
    @field wShowWindow, word, 1
    @field cbReserved2, word, 1
    @field lpReserved2, dword, 1
    @field hStdInput, dword, 1
    @field hStdOutput, dword, 1
    @field hStdError, dword, 1
@endstruc

;; @struc STARTUPINFOW64
;; @desc 104-byte 64-bit Win64/ARM64 CreateProcessW STARTUPINFOW structure.
@struc STARTUPINFOW64, 104
    @field cb, dword, 1
    @field dwPad0, dword, 1
    @field lpReserved, qword, 1
    @field lpDesktop, qword, 1
    @field lpTitle, qword, 1
    @field dwX, dword, 1
    @field dwY, dword, 1
    @field dwXSize, dword, 1
    @field dwYSize, dword, 1
    @field dwXCountChars, dword, 1
    @field dwYCountChars, dword, 1
    @field dwFillAttribute, dword, 1
    @field dwFlags, dword, 1
    @field wShowWindow, word, 1
    @field cbReserved2, word, 1
    @field dwPad1, dword, 1
    @field lpReserved2, qword, 1
    @field hStdInput, qword, 1
    @field hStdOutput, qword, 1
    @field hStdError, qword, 1
@endstruc

;; @struc PROCESS_INFORMATION
;; @desc 16-byte 32-bit Win32 CreateProcess PROCESS_INFORMATION structure.
@struc PROCESS_INFORMATION, 16
    @field hProcess, dword, 1
    @field hThread, dword, 1
    @field dwProcessId, dword, 1
    @field dwThreadId, dword, 1
@endstruc

;; @struc PROCESS_INFORMATION64
;; @desc 24-byte 64-bit Win64/ARM64 CreateProcessW PROCESS_INFORMATION structure.
@struc PROCESS_INFORMATION64, 24
    @field hProcess, qword, 1
    @field hThread, qword, 1
    @field dwProcessId, dword, 1
    @field dwThreadId, dword, 1
@endstruc
