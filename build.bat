@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /LD /O2 /MT Sprint.cpp /Fe:Sprint.asi user32.lib
echo ---- build exit code: %errorlevel% ----
