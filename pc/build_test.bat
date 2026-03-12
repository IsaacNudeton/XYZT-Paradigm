@echo off
REM Build script for child divergence test
REM Sets up MSVC environment and compiles with nvcc

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if %errorlevel% neq 0 (
    echo ERROR: Failed to set up Visual Studio environment
    exit /b %errorlevel%
)

echo.
echo Building test_divergence.exe...
echo.

nvcc -O2 -arch=sm_75 -o test_divergence.exe tests/test_child_divergence.c engine.c onetwo.c wire.c transducer.c reporter.c tline.c sense.c substrate.cu -lm

if %errorlevel% neq 0 (
    echo.
    echo ERROR: Compilation failed
    exit /b %errorlevel%
)

echo.
echo Build successful! Running test...
echo.

test_divergence.exe
