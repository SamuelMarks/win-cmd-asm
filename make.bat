@ECHO OFF
REM Universal MS-DOS (1.25/2.00/4.00/7.x) and Windows NT (3.1..11) Build Entrypoint
REM Usage: make [all|lint|lower|build|verify|package|test|ci|hooks|clean|nasm|fasm|jwasm]
IF "%OS%"=="Windows_NT" GOTO :WIN_NT
CALL BUILD.BAT %1 %2 %3
GOTO :EOF

:WIN_NT
CALL BUILD.CMD %*

:EOF
