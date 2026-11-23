;; @file u_imps_a32.inc
;; @isa armv7
;; @desc Shared Windows RT / Windows 10 ARM32 KERNEL32.DLL static IAT imports for ARMv7-A Thumb-2 utilities (Zero CRT).

@cpu armv7
@format inc

@extern __imp_CloseHandle, dword
@extern __imp_CompareFileTime, dword
@extern __imp_CopyFileW, dword
@extern __imp_CreateDirectoryW, dword
@extern __imp_CreateFileW, dword
@extern __imp_CreateThread, dword
@extern __imp_DeleteFileW, dword
@extern __imp_ExitProcess, dword
@extern __imp_FindClose, dword
@extern __imp_FindFirstFileW, dword
@extern __imp_FindNextFileW, dword
@extern __imp_GetCommandLineW, dword
@extern __imp_GetComputerNameExW, dword
@extern __imp_GetComputerNameW, dword
@extern __imp_GetConsoleMode, dword
@extern __imp_GetCurrentDirectoryW, dword
@extern __imp_GetCurrentProcess, dword
@extern __imp_GetEnvironmentVariableW, dword
@extern __imp_GetFileAttributesW, dword
@extern __imp_GetFileTime, dword
@extern __imp_GetFullPathNameW, dword
@extern __imp_GetLastError, dword
@extern __imp_GetModuleHandleW, dword
@extern __imp_GetProcAddress, dword
@extern __imp_GetStdHandle, dword
@extern __imp_GetTickCount, dword
@extern __imp_GlobalMemoryStatusEx, dword
@extern __imp_LoadLibraryW, dword
@extern __imp_ReadFile, dword
@extern __imp_SetFileAttributesW, dword
@extern __imp_Sleep, dword
@extern __imp_VirtualAlloc, dword
@extern __imp_VirtualFree, dword
@extern __imp_WaitForMultipleObjects, dword
@extern __imp_WaitForSingleObject, dword
@extern __imp_WriteConsoleW, dword
@extern __imp_WriteFile, dword
