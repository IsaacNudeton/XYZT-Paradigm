@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "%~dp0"
echo Building XYZT Daemon...
nvcc -O2 -arch=sm_75 -o xyzt_daemon.exe daemon.cu substrate.cu yee.cu engine.c onetwo.c wire.c transducer.c reporter.c sense.c tline.c io.c infer.c cortex.c sonify.c 2>&1
if %ERRORLEVEL% EQU 0 (
    echo DAEMON BUILD SUCCESS
    dir xyzt_daemon.exe
) else (
    echo DAEMON BUILD FAILED with error %ERRORLEVEL%
)
