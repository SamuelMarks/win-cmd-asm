;; @file u_data32.inc
;; @isa 386
;; @desc Shared read-only constants and zero-initialized .bss state for IA-32 PE32 utilities.

@cpu 386
@format inc

@data db szU32Crlf, 13, 10, 0
@data db szU32Kernel32, "KERNEL32.DLL", 0
@data db szU32Advapi32, "ADVAPI32.DLL", 0
@data db szU32Psapi, "PSAPI.DLL", 0
@data db szU32ConIn, "CONIN$", 0

@section bss

@res dword g_u32_is_win9x, 1
@res dword g_u32_stdin, 1
@res dword g_u32_stdout, 1
@res dword g_u32_stderr, 1
@res dword g_u32_out_is_con, 1
@res dword g_u32_in_is_con, 1
@res dword g_u32_arena, 1
@res dword g_u32_io_done, 1
@res byte g_u32_cmd, 4096
@res byte g_u32_arg_a, 1024
@res byte g_u32_arg_b, 1024
@res byte g_u32_arg_c, 1024
@res byte g_u32_path_tmp, 1024
@res byte g_u32_wbuf1, 2048
@res byte g_u32_wbuf2, 2048
@res byte g_u32_num_buf, 64
@res byte g_u32_find_raw, 640
