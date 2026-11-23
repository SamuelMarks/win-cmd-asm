;; @file u_bss_a32.inc
;; @isa armv7
;; @desc Shared zero-initialized .bss state for ARM32 Thumb-2 PE32 utilities.

@cpu armv7
@format inc

@res dword g_ua32_stdin, 1
@res dword g_ua32_stdout, 1
@res dword g_ua32_stderr, 1
@res dword g_ua32_arena, 1
@res dword g_ua32_out_is_con, 1
@res dword g_ua32_in_is_con, 1
@res dword g_ua32_io_done, 1
@res byte g_ua32_cmd, 4096
@res byte g_ua32_arg_a, 1024
@res byte g_ua32_arg_b, 1024
@res byte g_ua32_arg_c, 1024
@res byte g_ua32_path_tmp, 1024
@res byte g_ua32_wbuf1, 2048
@res byte g_ua32_wbuf2, 2048
@res byte g_ua32_num_buf, 64
@res byte g_ua32_find_raw, 640
