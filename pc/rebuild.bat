@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d E:\dev\xyzt-hardware\pc
echo STARTING BUILD > build_debug2.txt
nvcc -O2 -arch=sm_75 -o xyzt_pc.exe main.cu substrate.cu tests\test_gpu.cu engine.c onetwo.c wire.c transducer.c reporter.c sense.c tests\test_core.c tests\test_lifecycle.c tests\test_observer.c tests\test_stress.c tests\test_sense.c tests\test_collision.c tests\test_t3_stage1.c tests\test_t3_full.c tests\test_save_load.c sweep_tracking.c >> build_debug2.txt 2>&1
echo EXIT=%ERRORLEVEL% >> build_debug2.txt
if exist xyzt_pc.exe (echo EXE_EXISTS >> build_debug2.txt) else (echo EXE_MISSING >> build_debug2.txt)
