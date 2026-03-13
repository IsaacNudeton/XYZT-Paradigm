@echo off
REM Build script for child divergence test

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Failed to set up Visual Studio environment
    echo Please run this from a Developer Command Prompt instead
    exit /b %errorlevel%
)

echo.
echo Building test_divergence.exe...
echo.

nvcc -O2 -arch=sm_75 -o test_divergence.exe tests/test_child_divergence.c engine.c onetwo.c wire.c transducer.c reporter.c tline.c sense.c substrate.cu

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Compilation failed
    exit /b %errorlevel%
)

echo.
echo Build successful! Running test...
echo.

test_divergence.exe
