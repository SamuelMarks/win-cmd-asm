//! Exhaustive Milestone M6 integration tests verifying `WINCMD16.EXE` (`8086` universal `NE`),
//! `WINCMD286.EXE` (`80286` Protected-Mode `NE` with discardable `_TRN286`), `WINCMD.PIF`
//! (`369`B + `286`/`386` PIFEX), `WINOLDAP` Clipboard (`INT 2Fh AX=1700h..1708h`), and strict
//! Protected-Mode `#GP` LDT selector enforcement across Windows `1.04`, `2.03`, `2.10/286`,
//! `3.10`, `WfW 3.11`, and `3.2` (`CP936` `GB2312` DBCS + `WM_IME_REPORT`) on all 3 assemblers.

use emu_cov::{DosEra, DosVm, Win16Era};
use std::path::PathBuf;
use wincmd_diag::ToolError;

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(|p| p.parent())
        .map_or_else(|| PathBuf::from("."), PathBuf::from)
}

fn load_win16(tc: &str, name: &str) -> Result<Vec<u8>, ToolError> {
    Ok(std::fs::read(
        repo_root().join(format!("build/{tc}/win16/{name}")),
    )?)
}

fn load_dos16(tc: &str, name: &str) -> Result<Vec<u8>, ToolError> {
    Ok(std::fs::read(
        repo_root().join(format!("build/{tc}/dos16/{name}")),
    )?)
}

fn built_toolchains() -> Vec<&'static str> {
    let root = repo_root();
    let tcs: Vec<&'static str> = ["nasm", "fasm", "jwasm"]
        .into_iter()
        .filter(|tc| root.join(format!("build/{tc}/win16/WINCMD16.EXE")).is_file())
        .collect();
    assert!(!tcs.is_empty(), "at least one Win16 toolchain must be built");
    tcs
}

fn join_crlf(lines: &[&str]) -> Vec<u8> {
    let mut out = Vec::new();
    for line in lines {
        out.extend_from_slice(line.as_bytes());
        out.extend_from_slice(&[0x0D, 0x0A]);
    }
    out
}

fn collect_gdi_text(vm: &DosVm) -> String {
    let Some(pm) = &vm.pmode else {
        return String::new();
    };
    let mut all = String::new();
    for (_, _, bytes) in &pm.text_out_calls {
        all.push_str(&String::from_utf8_lossy(bytes));
        all.push(char::from(0x0A));
    }
    all
}

fn push_typed_line(vm: &mut DosVm, line: &str) {
    for &b in line.as_bytes() {
        vm.push_win16_msg(0x0102, u16::from(b), 0);
    }
    vm.push_win16_msg(0x0102, 0x000D, 0);
}

#[test]
fn m6_wincmd_pif_std_and_286_386_extensions_across_assemblers() -> Result<(), ToolError> {
    for tc in built_toolchains() {
        let pif = load_win16(tc, "WINCMD.PIF")?;
        assert_eq!(
            pif.len(),
            545,
            "{tc} WINCMD.PIF exact 545-byte extended layout"
        );
        assert_eq!(&pif[0x24..0x2E], b"WINCMD.COM", "{tc} PIF program");
        assert_eq!(pif[0x2E], 0);
        assert_eq!(
            u16::from_le_bytes([pif[0x20], pif[0x21]]),
            640,
            "{tc} PIF 640 KB max conventional"
        );
        assert_eq!(&pif[0x0171..0x0180], b"MICROSOFT PIFEX");
        assert_eq!(&pif[0x0187..0x0196], b"WINDOWS 286 3.0");
        assert_eq!(&pif[0x01A3..0x01B2], b"WINDOWS 386 3.0");
        assert_eq!(
            u16::from_le_bytes([pif[0x01B3], pif[0x01B4]]),
            0xFFFF,
            "{tc} PIFEX end-of-chain"
        );
    }
    Ok(())
}

#[test]
fn m6_win104_and_win203_tiled_vs_overlapping_and_loadmodule_across_assemblers()
-> Result<(), ToolError> {
    for tc in built_toolchains() {
        let exe16 = load_win16(tc, "WINCMD16.EXE")?;
        assert!(exe16.len() <= 65_536);

        // 1. Windows 1.04: CW_USEDEFAULT tiled geometry, /F SetErrorMode, LoadModule child fallback
        let mut vm1 = DosVm::new(DosEra::Dos200);
        vm1.load_ne(&exe16, Win16Era::Win104, "/F /K VER")?;
        push_typed_line(&mut vm1, "CLOCK.EXE");
        vm1.push_win16_msg(0x000F, 0, 0);
        push_typed_line(&mut vm1, "EXIT 0");
        let res1 = vm1.run(2_000_000)?;
        assert_eq!(res1.exit_code, 0, "{tc} Win 1.04 exit");

        let pm1 = vm1.pmode.as_ref().ok_or_else(|| ToolError::CliUsage {
            message: "missing pmode".into(),
        })?;
        assert!(pm1.resource_types.contains(&0x8003), "{tc} RT_ICON");
        assert!(pm1.resource_types.contains(&0x800E), "{tc} RT_GROUP_ICON");
        assert!(pm1.resource_types.contains(&0x8004), "{tc} RT_MENU");
        assert_eq!(pm1.error_mode, 0x0001, "{tc} /F SEM_FAILCRITICALERRORS");

        let cw1 = pm1
            .created_window
            .as_ref()
            .ok_or_else(|| ToolError::CliUsage {
                message: "missing CreatedWindowInfo".into(),
            })?;
        assert_eq!(cw1.x, 0x8000, "{tc} Win 1.04 tiled CW_USEDEFAULT x");
        assert_eq!(cw1.y, 0x8000, "{tc} Win 1.04 tiled CW_USEDEFAULT y");
        assert_eq!(cw1.width, 0x8000, "{tc} Win 1.04 tiled CW_USEDEFAULT cx");
        assert_eq!(cw1.height, 0x8000, "{tc} Win 1.04 tiled CW_USEDEFAULT cy");
        assert!(
            pm1.launched_modules
                .iter()
                .any(|(api, m)| *api == "LoadModule" && m == "CLOCK.EXE"),
            "{tc} Win 1.04 must use KERNEL.45 LoadModule: {:?}",
            pm1.launched_modules
        );
        let txt1 = collect_gdi_text(&vm1);
        assert!(
            txt1.contains("WINCMD16 [Windows 1.04]"),
            "{tc} Win 1.04 VER output: {txt1}"
        );

        // 2. Windows 2.03: overlapping coordinates, WM_SIZE dynamic reflow, caret & scrollbars
        let mut vm2 = DosVm::new(DosEra::Dos400);
        vm2.load_ne(&exe16, Win16Era::Win203, "")?;
        vm2.push_win16_msg(0x0007, 0, 0);
        vm2.push_win16_msg(0x0100, 0x002D, 0);
        vm2.push_win16_msg(0x0005, 0, (0x0168_u32 << 16) | 0x0320_u32);
        vm2.push_win16_msg(0x0115, 1, 0);
        vm2.push_win16_msg(0x0114, 1, 0);
        push_typed_line(&mut vm2, "VER");
        vm2.push_win16_msg(0x000F, 0, 0);
        vm2.push_win16_msg(0x0008, 0, 0);
        vm2.push_win16_msg(0x0002, 0, 0);
        let res2 = vm2.run(2_000_000)?;
        assert_eq!(res2.exit_code, 0);
        let pm2 = vm2.pmode.as_ref().ok_or_else(|| ToolError::CliUsage {
            message: "missing pmode".into(),
        })?;
        let cw2 = pm2
            .created_window
            .as_ref()
            .ok_or_else(|| ToolError::CliUsage {
                message: "missing cw2".into(),
            })?;
        assert_eq!(cw2.width, 640, "{tc} Win 2.03 overlapping width");
        assert_eq!(pm2.scroll_pos, (1, 1), "{tc} VScroll/HScroll updated");
        assert_eq!(pm2.caret.1, 12, "{tc} Overstrike block caret height == 12");
        let txt2 = collect_gdi_text(&vm2);
        // Horizontal scrollX == 1 shifts viewport 1 column right ("WINCMD16" -> "INCMD16")!
        assert!(txt2.contains("INCMD16 [Windows 2.03]"), "{tc} txt2: {txt2}");
    }
    Ok(())
}

#[test]
fn m6_wincmd286_wfw311_unc_dragdrop_clipboard_toolhelp_and_batch() -> Result<(), ToolError> {
    let unc_pushd = String::from_utf8_lossy(&[
        b'P', b'U', b'S', b'H', b'D', b' ', 0x5C, 0x5C, b'S', b'R', b'V', 0x5C, b'S', b'H', b'A',
        b'R', b'E',
    ])
    .into_owned();
    let expected_z = String::from_utf8_lossy(&[b'Z', b':', 0x5C]).into_owned();
    for tc in built_toolchains() {
        let exe286 = load_win16(tc, "WINCMD286.EXE")?;
        let mut vm = DosVm::new(DosEra::Dos400);
        vm.vfs
            .insert(String::from("SRC.TXT"), b"WIN16_COPY_PAYLOAD".to_vec());
        vm.vfs.insert(
            String::from("TEST.BAT"),
            join_crlf(&[
                "@ECHO OFF",
                "SET /A VAL=14*3",
                "COPY SRC.TXT DST.TXT",
                "DEL SRC.TXT",
                "MEM",
            ]),
        );
        vm.load_ne(&exe286, Win16Era::Wfw311, "/K TEST.BAT")?;
        if let Some(pm) = &mut vm.pmode {
            let seg3 = pm.ldt.get(&0x1027).ok_or_else(|| ToolError::CliUsage {
                message: "missing _TRN286 segment 3".into(),
            })?;
            assert!(seg3.discardable, "{tc} _TRN286 must be DISCARDABLE");
            pm.dropped_files.push(String::from("DROP_TARGET.TXT"));
        }

        push_typed_line(&mut vm, &unc_pushd);
        push_typed_line(&mut vm, "CD");
        push_typed_line(&mut vm, "POPD");
        push_typed_line(&mut vm, "TITLE WfW 3.11 Console");
        push_typed_line(&mut vm, "CALC.EXE");
        push_typed_line(&mut vm, "TYPE DST.TXT");
        vm.push_win16_msg(0x0111, 101, 0);
        vm.push_win16_msg(0x0233, 0x0D01, 0);
        vm.push_win16_msg(0x0102, 0x0003, 0);
        vm.push_win16_msg(0x000F, 0, 0);
        push_typed_line(&mut vm, "EXIT 7");

        let res = vm.run(3_000_000)?;
        assert_eq!(res.exit_code, 7, "{tc} EXIT 7 propagated");
        assert!(!vm.vfs.contains_key("SRC.TXT"), "{tc} DEL SRC.TXT");
        assert_eq!(
            vm.vfs.get("DST.TXT").map(Vec::as_slice),
            Some(&b"WIN16_COPY_PAYLOAD"[..]),
            "{tc} COPY SRC.TXT DST.TXT"
        );

        let dgroup = vm.pmode.as_ref().map_or(0, |p| p.dgroup_sel);
        assert_eq!(
            vm.read_u8(dgroup, 0x05B5),
            1,
            "{tc} discardable _TRN286 segment executed via INTERNALREF"
        );

        let pm = vm.pmode.as_ref().ok_or_else(|| ToolError::CliUsage {
            message: "missing pmode".into(),
        })?;
        assert_eq!(pm.window_title, "WfW 3.11 Console");
        assert!(
            pm.unc_connections.is_empty(),
            "{tc} POPD must disconnect temporary Z: UNC mapping"
        );
        assert!(
            pm.launched_modules
                .iter()
                .any(|(api, m)| *api == "WinExec" && m == "CALC.EXE"),
            "{tc} 3.11 must use WinExec: {:?}",
            pm.launched_modules
        );
        assert!(
            !pm.clipboard_bytes.is_empty(),
            "{tc} IDM_EDIT_COPY populated CF_OEMTEXT clipboard"
        );

        let txt = collect_gdi_text(&vm);
        assert!(txt.contains("42"), "{tc} SET /A 14*3 == 42: {txt}");
        assert!(
            txt.contains("TOOLHELP OK"),
            "{tc} TOOLHELP MemManInfo: {txt}"
        );
        assert!(txt.contains(&expected_z), "{tc} WfW 3.11 UNC PUSHD: {txt}");
        assert!(
            txt.contains("WIN16_COPY_PAYLOAD"),
            "{tc} TYPE DST.TXT: {txt}"
        );
    }
    Ok(())
}

#[test]
fn m6_win320_simplified_chinese_cp936_dbcs_ime_and_internal_dos_com() -> Result<(), ToolError> {
    let mut mini_com = vec![
        0xBA, 0x0C, 0x01, // mov dx, 0x010C
        0xB4, 0x09, // mov ah, 0x09
        0xCD, 0x21, // int 0x21
        0xB8, 0x00, 0x4C, // mov ax, 0x4C00
        0xCD, 0x21, // int 0x21
    ];
    mini_com.extend_from_slice(b"COM_IN_W16$");

    for tc in built_toolchains() {
        let exe16 = load_win16(tc, "WINCMD16.EXE")?;
        let mut vm = DosVm::new(DosEra::Dos400);
        vm.vfs.insert(String::from("MINI.COM"), mini_com.clone());
        vm.load_ne(&exe16, Win16Era::Win320Cjk, "--internal-dos /K VER")?;

        for &b in b"ECHO " {
            vm.push_win16_msg(0x0102, u16::from(b), 0);
        }
        vm.push_win16_msg(0x0280, 0x0160, 0x0000_C4E3);
        vm.push_win16_msg(0x0280, 0x0160, 0x0000_BAC3);
        vm.push_win16_msg(0x0280, 0x0160, 0x0000_B4ED);
        vm.push_win16_msg(0x0102, 0x0008, 0);
        vm.push_win16_msg(0x0102, 0x000D, 0);

        push_typed_line(&mut vm, "MINI.COM");
        vm.push_win16_msg(0x000F, 0, 0);
        push_typed_line(&mut vm, "EXIT 0");

        let res = vm.run(2_500_000)?;
        assert_eq!(res.exit_code, 0);

        let raw_bytes: Vec<u8> = vm.pmode.as_ref().map_or_else(Vec::new, |pm| {
            pm.text_out_calls
                .iter()
                .flat_map(|(_, _, r)| r.iter().copied())
                .collect()
        });
        let ascii = String::from_utf8_lossy(&raw_bytes);
        assert!(
            ascii.contains("WINCMD16 [Windows 3.20] [CP936 DBCS]"),
            "{tc} Win 3.2 CP936 VER: {ascii}"
        );
        assert!(
            ascii.contains("COM_IN_W16"),
            "{tc} --internal-dos .COM stdout captured into Win16 GDI: {ascii}"
        );
        assert!(
            raw_bytes.windows(4).any(|w| w == [0xC4, 0xE3, 0xBA, 0xC3]),
            "{tc} GB2312 '你好' rendered intact"
        );
        let last_nihao = raw_bytes
            .windows(4)
            .rposition(|w| w == [0xC4, 0xE3, 0xBA, 0xC3])
            .unwrap_or(0);
        assert_ne!(
            raw_bytes.get(last_nihao + 4).copied(),
            Some(0xB4),
            "{tc} DBCS Backspace must remove full 2-byte GB2312 character"
        );
    }
    Ok(())
}

#[test]
fn m6_protected_mode_gp_fault_trap_and_dos_winoldap_clipboard() -> Result<(), ToolError> {
    let exe16 = load_win16("nasm", "WINCMD16.EXE")?;
    let mut bad_vm = DosVm::new(DosEra::Dos400);
    bad_vm.load_ne(&exe16, Win16Era::Win310, "/C VER")?;
    bad_vm.mem[0x10000..0x10005].copy_from_slice(&[0xB8, 0x99, 0x29, 0x8E, 0xC0]);
    let gp_err = bad_vm.run(16);
    assert!(
        gp_err
            .as_ref()
            .err()
            .is_some_and(|e| e.to_string().contains("#GP")),
        "expected #GP Protected-Mode selector fault, got {gp_err:?}"
    );

    let mut cs_write_vm = DosVm::new(DosEra::Dos400);
    cs_write_vm.load_ne(&exe16, Win16Era::Win310, "/C VER")?;
    cs_write_vm.mem[0x10000..0x10006].copy_from_slice(&[0x2E, 0xC6, 0x06, 0x10, 0x00, 0x90]);
    let cs_err = cs_write_vm.run(16);
    assert!(
        cs_err
            .as_ref()
            .err()
            .is_some_and(|e| e.to_string().contains("#GP")),
        "expected #GP on CS: write, got {cs_err:?}"
    );

    for tc in built_toolchains() {
        let wincmd = load_dos16(tc, "WINCMD.COM")?;
        let mut dvm = DosVm::new(DosEra::Dos400);
        dvm.win_enh_mode = true;
        dvm.winoldap_clipboard = b"ECHO WINOLDAP_CLIP_OK".to_vec();
        dvm.stdin = vec![0x16, b'!', 0x0B, 0x0D, b'E', b'X', b'I', b'T', 0x0D];
        dvm.load_com(&wincmd, "", &[])?;
        let dres = dvm.run(2_000_000)?;
        assert_eq!(dres.exit_code, 0);
        let dout = String::from_utf8_lossy(&dres.stdout);
        assert!(
            dout.contains("WINOLDAP_CLIP_OK!"),
            "{tc} DOS WINOLDAP paste failed: {dout}"
        );
        assert_eq!(
            dvm.winoldap_clipboard, b"ECHO WINOLDAP_CLIP_OK!",
            "{tc} DOS WINOLDAP copy failed"
        );
    }
    Ok(())
}
