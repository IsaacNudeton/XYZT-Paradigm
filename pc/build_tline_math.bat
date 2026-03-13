@echo off
REM Build script for TLine math test

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Failed to set up Visual Studio environment
    exit /b %errorlevel%
)

echo.
echo Building test_tline_math.exe...
echo.

nvcc -O2 -arch=sm_75 -o test_tline_math.exe tests/test_tline_math.c tline.c

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Compilation failed
    exit /b %errorlevel%
)

echo.
echo Build successful! Running test...
echo.

test_tline_math.exe
