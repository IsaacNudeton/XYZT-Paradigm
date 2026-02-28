@echo off
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
echo === STARTING NVCC === > rebuild_log.txt
nvcc -O2 -arch=sm_75 -o xyzt_pc.exe main.cu substrate.cu tests/test_gpu.cu engine.c onetwo.c wire.c transducer.c reporter.c tests/test_core.c tests/test_lifecycle.c tests/test_observer.c tests/test_stress.c >> rebuild_log.txt 2>&1
echo === EXIT CODE: %ERRORLEVEL% === >> rebuild_log.txt
if exist xyzt_pc.exe (
    echo BUILD SUCCESS >> rebuild_log.txt
    dir xyzt_pc.exe >> rebuild_log.txt
) else (
    echo BUILD FAILED >> rebuild_log.txt
)
