;; @file u_data64.inc
;; @isa x64
;; @desc Shared read-only constants and zero-initialized .bss state for x86-64 PE32+ utilities.

@cpu x64
@format inc

@data db szU64Crlf, 13, 10, 0
@data db szU64Kernel32, "KERNEL32.DLL", 0
@data db szU64Advapi32, "ADVAPI32.DLL", 0
@data db szU64Psapi, "PSAPI.DLL", 0
@data dw u16U64ConIn, 'C', 'O', 'N', 'I', 'N', '$', 0

@section bss

@res qword g_u64_stdin, 1
@res qword g_u64_stdout, 1
@res qword g_u64_stderr, 1
@res qword g_u64_arena, 1
@res dword g_u64_out_is_con, 1
@res dword g_u64_in_is_con, 1
@res dword g_u64_io_done, 1
@res byte g_u64_cmd, 4096
@res byte g_u64_arg_a, 1024
@res byte g_u64_arg_b, 1024
@res byte g_u64_arg_c, 1024
@res byte g_u64_path_tmp, 1024
@res byte g_u64_wbuf1, 2048
@res byte g_u64_wbuf2, 2048
@res byte g_u64_num_buf, 64
@res byte g_u64_find_raw, 640
