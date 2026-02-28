"""
sweep_137.py — SUBSTRATE_INT parameter sweep (v2: closed-loop, 25-sentence corpus)
Builds engine at each N, runs tests + metrics, writes sweep_results.tsv

Usage: python sweep_137.py
"""
import subprocess, re, os, sys

PC_DIR = os.path.dirname(os.path.abspath(__file__))
VCVARS = r'"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"'
SOURCES = (
    "main.cu substrate.cu tests/test_gpu.cu engine.c onetwo.c wire.c "
    "transducer.c reporter.c sense.c tests/test_core.c tests/test_lifecycle.c "
    "tests/test_observer.c tests/test_stress.c tests/test_sense.c sweep_tracking.c"
)

N_VALUES = [100, 110, 117, 120, 125, 127, 130, 133, 135, 136, 137, 138,
            139, 140, 143, 147, 150, 160, 170, 180]

HEADER = ("N\tpasses\tfails\talive\trelay\tcollision\tcrystal\t"
          "total_error\ttotal_valence\tcrystal_cycles\tuncoupled_frac\t"
          "grow_int\tprune_int\ttotal_edges\tfail_list\n")


def build_and_run(n):
    """Build with SUBSTRATE_INT=N, run test + sweep, return parsed results."""
    bat = os.path.join(PC_DIR, "_sweep_build.bat")
    with open(bat, "w") as f:
        f.write("@echo off\n")
        f.write(f"cd /d \"{PC_DIR}\"\n")
        f.write(f"call {VCVARS} x64 >nul 2>&1\n")
        f.write(f"cd /d \"{PC_DIR}\"\n")
        f.write(f"nvcc -O2 -arch=sm_75 -DSUBSTRATE_INT={n}u "
                f"-o xyzt_pc.exe {SOURCES} >_sweep_nvcc.log 2>&1\n")
        f.write("echo EXIT:%ERRORLEVEL%\n")

    subprocess.run(["cmd.exe", "/C", bat], capture_output=True, text=True,
                   cwd=PC_DIR, timeout=300)

    exe = os.path.join(PC_DIR, "xyzt_pc.exe")
    if not os.path.exists(exe):
        nvcc_log = ""
        log_path = os.path.join(PC_DIR, "_sweep_nvcc.log")
        if os.path.exists(log_path):
            with open(log_path) as lf:
                nvcc_log = lf.read()[:500]
        return {"n": n, "build": False, "error": nvcc_log}

    # Run tests
    try:
        tr = subprocess.run([exe, "test"], capture_output=True, text=True,
                           cwd=PC_DIR, timeout=180)
        test_out = tr.stdout + tr.stderr
    except subprocess.TimeoutExpired:
        test_out = "TIMEOUT"

    m = re.search(r"(\d+) passed, (\d+) failed, (\d+) total", test_out)
    passes = int(m.group(1)) if m else -1
    fails = int(m.group(2)) if m else -1

    fail_lines = [line.strip() for line in test_out.splitlines()
                  if line.strip().startswith("FAIL")]
    fail_list = "; ".join(fail_lines) if fail_lines else ""

    # Run sweep metrics
    try:
        sr = subprocess.run([exe, "sweep"], capture_output=True, text=True,
                           cwd=PC_DIR, timeout=300)
        sweep_out = sr.stdout + sr.stderr
    except subprocess.TimeoutExpired:
        sweep_out = "TIMEOUT"

    sweep_data = {}
    for line in sweep_out.splitlines():
        if line.startswith("SWEEP\t"):
            parts = line.split("\t")
            if len(parts) >= 13:
                sweep_data = {
                    "alive": parts[2], "relay": parts[3],
                    "collision": parts[4], "crystal": parts[5],
                    "total_error": parts[6], "total_valence": parts[7],
                    "crystal_cycles": parts[8], "uncoupled_frac": parts[9],
                    "grow_int": parts[10], "prune_int": parts[11],
                    "total_edges": parts[12],
                }

    return {
        "n": n, "build": True, "passes": passes, "fails": fails,
        "fail_list": fail_list, **sweep_data
    }


def main():
    tsv_path = os.path.join(PC_DIR, "sweep_results.tsv")
    with open(tsv_path, "w") as out:
        out.write(HEADER)

    print(f"=== 137 SWEEP v2 (closed-loop, 25 sentences, 40 cycles): {len(N_VALUES)} values ===")
    print(HEADER.strip())

    for n in N_VALUES:
        print(f"\n--- N={n} ---", flush=True)
        result = build_and_run(n)

        if not result["build"]:
            line = f"{n}\tBUILD_FAIL\t\t\t\t\t\t\t\t\t\t\t\t\t{result.get('error','')}"
            print(line)
            with open(tsv_path, "a") as out:
                out.write(line + "\n")
            continue

        cols = ["passes", "fails", "alive", "relay", "collision", "crystal",
                "total_error", "total_valence", "crystal_cycles", "uncoupled_frac",
                "grow_int", "prune_int", "total_edges", "fail_list"]
        line = f"{n}\t" + "\t".join(str(result.get(c, "")) for c in cols)
        print(line)
        with open(tsv_path, "a") as out:
            out.write(line + "\n")

    for f in ["_sweep_build.bat", "_sweep_nvcc.log"]:
        p = os.path.join(PC_DIR, f)
        if os.path.exists(p):
            os.remove(p)

    print(f"\n=== DONE. Results in {tsv_path} ===")


if __name__ == "__main__":
    main()
