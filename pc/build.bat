@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "%~dp0"
echo Building XYZT PC Engine...
nvcc -O2 -arch=sm_75 -o xyzt_pc.exe main.cu substrate.cu yee.cu tests/test_gpu.cu engine.c onetwo.c wire.c transducer.c reporter.c sense.c tline.c io.c tests/test_core.c tests/test_lifecycle.c tests/test_observer.c tests/test_stress.c tests/test_sense.c tests/test_collision.c tests/test_t3_stage1.c tests/test_t3_full.c tests/test_save_load.c tests/test_tline.c tests/test_child_conflict.c tests/test_external.c tests/test_lfield.c sweep_tracking.c 2>&1
if %ERRORLEVEL% EQU 0 (
    echo BUILD SUCCESS
    dir xyzt_pc.exe
) else (
    echo BUILD FAILED with error %ERRORLEVEL%
)
