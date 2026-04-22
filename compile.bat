@echo off
setlocal

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo ERROR: VS BuildTools not found at %VCVARS%
    exit /b 1
)
call %VCVARS% > nul 2>&1

set PA_INC=C:\vcpkg\installed\x64-windows\include
set PA_LIB=C:\vcpkg\installed\x64-windows\lib\portaudio.lib
set PA_DLL=C:\vcpkg\installed\x64-windows\bin\portaudio.dll

echo Compiling ns_rt.exe ...
cl.exe /nologo /std:c11 /O2 /W3 /utf-8 /I. /I.. /I%PA_INC% /MT ^
    ns_core.c fft4g.c noise_suppression.c ns_realtime.c ^
    /Fe:ns_rt.exe ^
    /link %PA_LIB% winmm.lib ole32.lib uuid.lib setupapi.lib avrt.lib
if %ERRORLEVEL% neq 0 ( echo Build FAILED & exit /b 1 )

echo Copying portaudio.dll ...
copy /Y %PA_DLL% portaudio.dll > nul

echo.
echo Build OK: ns_rt.exe
echo.
echo Usage:
echo   ns_rt.exe                   -- list audio devices
echo   ns_rt.exe ^<dev_idx^>         -- record with default settings (raw_ch=0, dsp_ch=6, total_ch=7)
echo   ns_rt.exe ^<dev_idx^> 0 6 7 2 -- aggressive mode
echo.

endlocal
