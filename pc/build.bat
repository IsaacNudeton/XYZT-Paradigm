@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cd /d "%~dp0"
echo Building XYZT PC Engine...
nvcc -O2 -arch=sm_75 -o xyzt_pc.exe main.cu yee.cu engine.c onetwo.c wire.c transducer.c reporter.c sense.c tline.c io.c infer.c cortex.c sonify.c tests/test_core.c tests/test_lifecycle.c tests/test_observer.c tests/test_stress.c tests/test_sense.c tests/test_collision.c tests/test_t3_stage1.c tests/test_t3_full.c tests/test_save_load.c tests/test_tline.c tests/test_child_conflict.c tests/test_external.c tests/test_lfield.c tests/test_stress_system.c tests/test_duality.c tests/test_resonance.c tests/test_self_observe.c tests/test_predict.c tests/test_generalize.c tests/test_output.c tests/test_babble.c 2>&1
if %ERRORLEVEL% EQU 0 (
    echo BUILD SUCCESS
    dir xyzt_pc.exe
) else (
    echo BUILD FAILED with error %ERRORLEVEL%
)
