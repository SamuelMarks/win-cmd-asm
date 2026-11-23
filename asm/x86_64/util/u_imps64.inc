;; @file u_imps64.inc
;; @isa x64
;; @desc Shared Windows XP Pro x64 (5.2) KERNEL32.DLL static IAT imports for x86-64 PE32+ utilities (Zero CRT).

@cpu x64
@format inc

@extern __imp_CloseHandle, qword
@extern __imp_CompareFileTime, qword
@extern __imp_CopyFileW, qword
@extern __imp_CreateDirectoryW, qword
@extern __imp_CreateFileW, qword
@extern __imp_CreateThread, qword
@extern __imp_DeleteFileW, qword
@extern __imp_ExitProcess, qword
@extern __imp_FindClose, qword
@extern __imp_FindFirstFileW, qword
@extern __imp_FindNextFileW, qword
@extern __imp_GetCommandLineW, qword
@extern __imp_GetComputerNameExW, qword
@extern __imp_GetComputerNameW, qword
@extern __imp_GetConsoleMode, qword
@extern __imp_GetCurrentDirectoryW, qword
@extern __imp_GetCurrentProcess, qword
@extern __imp_GetEnvironmentVariableW, qword
@extern __imp_GetFileAttributesW, qword
@extern __imp_GetFileTime, qword
@extern __imp_GetFullPathNameW, qword
@extern __imp_GetLastError, qword
@extern __imp_GetModuleHandleA, qword
@extern __imp_GetProcAddress, qword
@extern __imp_GetStdHandle, qword
@extern __imp_GetTickCount, qword
@extern __imp_GlobalMemoryStatusEx, qword
@extern __imp_LoadLibraryA, qword
@extern __imp_ReadFile, qword
@extern __imp_SetFileAttributesW, qword
@extern __imp_Sleep, qword
@extern __imp_VirtualAlloc, qword
@extern __imp_VirtualFree, qword
@extern __imp_WaitForMultipleObjects, qword
@extern __imp_WaitForSingleObject, qword
@extern __imp_WriteConsoleW, qword
@extern __imp_WriteFile, qword
