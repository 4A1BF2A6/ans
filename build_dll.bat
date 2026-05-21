@echo off
setlocal
cd /d %~dp0

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo ERROR: VS BuildTools not found at %VCVARS%
    exit /b 1
)
call %VCVARS% > nul 2>&1

if not exist build mkdir build

echo Compiling webrtc_ns.dll (shared library for Python ctypes)...
rem 中间件 (.obj) 写到 build\，DLL 留在顶层；.lib/.exp 也走 build\
cl.exe /nologo /std:c11 /O2 /W3 /utf-8 /I. /I.. /MT /LD ^
    /Fobuild\ ^
    ns_core.c fft4g.c noise_suppression.c ^
    /Fe:webrtc_ns.dll ^
    /link /DEF:webrtc_ns.def /IMPLIB:build\webrtc_ns.lib
if %ERRORLEVEL% neq 0 ( echo Build FAILED & exit /b 1 )

echo.
echo Build OK: webrtc_ns.dll  (intermediates -^> build\)
dir /b webrtc_ns.dll
dir /b build\webrtc_ns.lib build\webrtc_ns.exp 2>nul

endlocal
