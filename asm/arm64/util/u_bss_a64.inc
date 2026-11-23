;; @file u_bss_a64.inc
;; @isa arm64
;; @desc Shared zero-initialized .bss state for ARM64 AArch64 PE32+ utilities.

@cpu arm64
@format inc

@res qword g_ua64_stdin, 1
@res qword g_ua64_stdout, 1
@res qword g_ua64_stderr, 1
@res qword g_ua64_arena, 1
@res dword g_ua64_out_is_con, 1
@res dword g_ua64_in_is_con, 1
@res dword g_ua64_io_done, 1
@res byte g_ua64_cmd, 4096
@res byte g_ua64_arg_a, 1024
@res byte g_ua64_arg_b, 1024
@res byte g_ua64_arg_c, 1024
@res byte g_ua64_path_tmp, 1024
@res byte g_ua64_wbuf1, 2048
@res byte g_ua64_wbuf2, 2048
@res byte g_ua64_num_buf, 64
@res byte g_ua64_find_raw, 640
