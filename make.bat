@ECHO OFF
REM Universal MS-DOS (2.00/4.00/7.x) and Windows NT (3.1..11) Build Entrypoint
IF "%OS%"=="Windows_NT" GOTO :WIN_NT
CALL BUILD.BAT %1 %2 %3
GOTO :EOF

:WIN_NT
CALL BUILD.CMD %*
