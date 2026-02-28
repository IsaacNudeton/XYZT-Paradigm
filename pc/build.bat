@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "%~dp0"
echo Building XYZT PC Engine...
nvcc -O2 -arch=sm_75 -o xyzt_pc.exe main.cu substrate.cu tests/test_gpu.cu engine.c onetwo.c wire.c transducer.c reporter.c tests/test_core.c tests/test_lifecycle.c tests/test_observer.c tests/test_stress.c 2>&1
if %ERRORLEVEL% EQU 0 (
    echo BUILD SUCCESS
    dir xyzt_pc.exe
) else (
    echo BUILD FAILED with error %ERRORLEVEL%
)
