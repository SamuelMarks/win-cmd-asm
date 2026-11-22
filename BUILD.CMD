@ECHO OFF
SETLOCAL
REM Native Windows NT 3.1 .. Windows 11 Multi-ISA, Multi-Assembler Build Script
REM Usage: BUILD.CMD [all | clean | hooks | nasm | fasm | jwasm]

IF /I "%~1"=="clean" GOTO :CLEAN
IF /I "%~1"=="hooks" (
    git config core.hooksPath .githooks || EXIT /B 1
    GOTO :DONE
)

WHERE cargo >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    cargo run --release --quiet -p asm-lint -- --dir asm || EXIT /B 1
    cargo run --release --quiet -p asm-conv -- --in-dir asm --out-dir dist/src || EXIT /B 1
)

IF NOT EXIST "build/libs" MKDIR "build/libs"
IF NOT EXIST "build/nasm/dos16" MKDIR "build/nasm/dos16"
IF NOT EXIST "build/fasm/dos16" MKDIR "build/fasm/dos16"
IF NOT EXIST "build/jwasm/dos16" MKDIR "build/jwasm/dos16"
IF NOT EXIST "build/nasm/win16" MKDIR "build/nasm/win16"
IF NOT EXIST "build/fasm/win16" MKDIR "build/fasm/win16"
IF NOT EXIST "build/jwasm/win16" MKDIR "build/jwasm/win16"
IF NOT EXIST "build/nasm/x86_32" MKDIR "build/nasm/x86_32"
IF NOT EXIST "build/fasm/x86_32" MKDIR "build/fasm/x86_32"
IF NOT EXIST "build/jwasm/x86_32" MKDIR "build/jwasm/x86_32"
IF NOT EXIST "build/nasm/x86_64" MKDIR "build/nasm/x86_64"
IF NOT EXIST "build/fasm/x86_64" MKDIR "build/fasm/x86_64"
IF NOT EXIST "build/jwasm/x86_64" MKDIR "build/jwasm/x86_64"
IF NOT EXIST "build/fasmarm/arm32" MKDIR "build/fasmarm/arm32"
IF NOT EXIST "build/fasmarm/arm64" MKDIR "build/fasmarm/arm64"

WHERE llvm-dlltool >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    llvm-dlltool -m i386 -k -d asm/tc/arm_ms/k32_x86.def -l build/libs/kernel32_x86.lib || EXIT /B 1
    llvm-dlltool -m i386:x86-64 -d asm/tc/arm_ms/k32_x64.def -l build/libs/kernel32_x64.lib || EXIT /B 1
)

IF /I "%~1"=="fasm" GOTO :BUILD_FASM
IF /I "%~1"=="jwasm" GOTO :BUILD_JWASM

:BUILD_NASM
WHERE nasm >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/dos16/ -f bin dist/src/nasm/dos16/wincmd.asm -o build/nasm/dos16/WINCMD.COM || EXIT /B 1
    nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/dos16/ -f bin dist/src/nasm/dos16/wincmd1.asm -o build/nasm/dos16/WINCMD1.COM || EXIT /B 1
    nasm -Ox -w+all -w-reloc -w+error -f bin dist/src/nasm/dos16/util/asmconv.asm -o build/nasm/dos16/ASMCONV.COM || EXIT /B 1
    FOR %%U IN (mem attrib find findstr sort more fc comp tree where xcopy replace robocopy choice timeout truename reg hostname whoami) DO (
        nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/dos16/util/ -f bin dist/src/nasm/dos16/util/%%U.asm -o build/nasm/dos16/%%U.COM || EXIT /B 1
    )
    nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/win16/ -f bin dist/src/nasm/win16/wincmd_pif.asm -o build/nasm/win16/WINCMD.PIF || EXIT /B 1
    nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/win16/ -f bin dist/src/nasm/win16/w16_main.asm -o build/nasm/win16/WINCMD16.EXE || EXIT /B 1
    nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/win16/ -f bin dist/src/nasm/win16/w16_286.asm -o build/nasm/win16/WINCMD286.EXE || EXIT /B 1
    WHERE lld-link >NUL 2>NUL
    IF NOT ERRORLEVEL 1 (
        nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/x86_32/ -f win32 dist/src/nasm/x86_32/wincmd32.asm -o build/nasm/x86_32/wincmd32.obj || EXIT /B 1
        lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup build/nasm/x86_32/wincmd32.obj build/libs/kernel32_x86.lib /OUT:build/nasm/x86_32/WINCMD32.EXE || EXIT /B 1
        nasm -Ox -w+all -w-reloc -w+error -I dist/src/nasm/x86_64/ -f win64 dist/src/nasm/x86_64/wincmd64.asm -o build/nasm/x86_64/wincmd64.obj || EXIT /B 1
        lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,5.02 /STACK:1048576,65536 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup build/nasm/x86_64/wincmd64.obj build/libs/kernel32_x64.lib /OUT:build/nasm/x86_64/WINCMD64.EXE || EXIT /B 1
    )
)
IF /I "%~1"=="nasm" GOTO :DONE

:BUILD_FASM
WHERE fasm >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    fasm dist/src/fasm/dos16/wincmd.asm build/fasm/dos16/WINCMD.COM >NUL || EXIT /B 1
    fasm dist/src/fasm/dos16/wincmd1.asm build/fasm/dos16/WINCMD1.COM >NUL || EXIT /B 1
    fasm dist/src/fasm/dos16/util/asmconv.asm build/fasm/dos16/ASMCONV.COM >NUL || EXIT /B 1
    FOR %%U IN (mem attrib find findstr sort more fc comp tree where xcopy replace robocopy choice timeout truename reg hostname whoami) DO (
        fasm dist/src/fasm/dos16/util/%%U.asm build/fasm/dos16/%%U.COM >NUL || EXIT /B 1
    )
    fasm dist/src/fasm/win16/wincmd_pif.asm build/fasm/win16/WINCMD.PIF >NUL || EXIT /B 1
    fasm dist/src/fasm/win16/w16_main.asm build/fasm/win16/WINCMD16.EXE >NUL || EXIT /B 1
    fasm dist/src/fasm/win16/w16_286.asm build/fasm/win16/WINCMD286.EXE >NUL || EXIT /B 1
    fasm dist/src/fasm/x86_64/wincmd64.asm build/fasm/x86_64/WINCMD64.EXE >NUL || EXIT /B 1
    WHERE lld-link >NUL 2>NUL
    IF NOT ERRORLEVEL 1 (
        fasm dist/src/fasm/x86_32/wincmd32.asm build/fasm/x86_32/wincmd32.obj >NUL || EXIT /B 1
        lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup build/fasm/x86_32/wincmd32.obj build/libs/kernel32_x86.lib /OUT:build/fasm/x86_32/WINCMD32.EXE || EXIT /B 1
    )
)
WHERE fasmarm >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    fasmarm dist/src/fasmarm/arm32/v4/wincmdv4.asm build/fasmarm/arm32/WINCMD_ARMV4.EXE >NUL || EXIT /B 1
    fasmarm dist/src/fasmarm/arm32/v7/wincmdv7.asm build/fasmarm/arm32/WINCMD_ARM32.EXE >NUL || EXIT /B 1
    fasmarm dist/src/fasmarm/arm64/wincmda64.asm build/fasmarm/arm64/WINCMD_ARM64.EXE >NUL || EXIT /B 1
)
IF /I "%~1"=="fasm" GOTO :DONE

:BUILD_JWASM
WHERE jwasm >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    jwasm -q -WX -W4 -Idist/src/jwasm/dos16 -bin -Fobuild/jwasm/dos16/WINCMD.COM dist/src/jwasm/dos16/wincmd.asm || EXIT /B 1
    jwasm -q -WX -W4 -Idist/src/jwasm/dos16 -bin -Fobuild/jwasm/dos16/WINCMD1.COM dist/src/jwasm/dos16/wincmd1.asm || EXIT /B 1
    jwasm -q -WX -W4 -bin -Fobuild/jwasm/dos16/ASMCONV.COM dist/src/jwasm/dos16/util/asmconv.asm || EXIT /B 1
    FOR %%U IN (mem attrib find findstr sort more fc comp tree where xcopy replace robocopy choice timeout truename reg hostname whoami) DO (
        jwasm -q -WX -W4 -Idist/src/jwasm/dos16/util -bin -Fobuild/jwasm/dos16/%%U.COM dist/src/jwasm/dos16/util/%%U.asm || EXIT /B 1
    )
    jwasm -q -WX -W4 -Idist/src/jwasm/win16 -bin -Fobuild/jwasm/win16/WINCMD.PIF dist/src/jwasm/win16/wincmd_pif.asm || EXIT /B 1
    jwasm -q -WX -W4 -Idist/src/jwasm/win16 -bin -Fobuild/jwasm/win16/WINCMD16.EXE dist/src/jwasm/win16/w16_main.asm || EXIT /B 1
    jwasm -q -WX -W4 -Idist/src/jwasm/win16 -bin -Fobuild/jwasm/win16/WINCMD286.EXE dist/src/jwasm/win16/w16_286.asm || EXIT /B 1
    WHERE lld-link >NUL 2>NUL
    IF NOT ERRORLEVEL 1 (
        jwasm -q -WX -W4 -Idist/src/jwasm/x86_32 -coff -safeseh -Fobuild/jwasm/x86_32/wincmd32.obj dist/src/jwasm/x86_32/wincmd32.asm || EXIT /B 1
        lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup build/jwasm/x86_32/wincmd32.obj build/libs/kernel32_x86.lib /OUT:build/jwasm/x86_32/WINCMD32.EXE || EXIT /B 1
        jwasm -q -WX -W4 -Idist/src/jwasm/x86_64 -win64 -Fobuild/jwasm/x86_64/wincmd64.obj dist/src/jwasm/x86_64/wincmd64.asm || EXIT /B 1
        lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,5.02 /STACK:1048576,65536 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup build/jwasm/x86_64/wincmd64.obj build/libs/kernel32_x64.lib /OUT:build/jwasm/x86_64/WINCMD64.EXE || EXIT /B 1
    )
)
GOTO :DONE

:CLEAN
IF EXIST build RMDIR /S /Q build
IF EXIST dist RMDIR /S /Q dist
GOTO :DONE

:DONE
ENDLOCAL
EXIT /B 0
