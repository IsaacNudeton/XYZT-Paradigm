@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
echo Building test_yee3d_gpu.exe...
nvcc -O2 -arch=sm_75 -o test_yee3d_gpu.exe yee.cu tests/test_yee3d_gpu.cu
if %ERRORLEVEL% EQU 0 (
    echo BUILD OK
) else (
    echo BUILD FAILED with code %ERRORLEVEL%
)
