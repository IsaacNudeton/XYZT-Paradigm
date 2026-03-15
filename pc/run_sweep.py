"""N-sweep: recompile with each SUBSTRATE_INT value and run tracking.
Output: sweep_results.tsv (tab-separated)

Usage: python run_sweep.py [--quick]
  --quick: fewer N values (5 points) for fast iteration
"""
import subprocess, sys, os, time

SRCFILES = (
    "main.cu substrate.cu yee.cu tests/test_gpu.cu engine.c onetwo.c wire.c "
    "transducer.c reporter.c sense.c tline.c tests/test_core.c "
    "tests/test_lifecycle.c tests/test_observer.c tests/test_stress.c "
    "tests/test_sense.c tests/test_collision.c tests/test_t3_stage1.c "
    "tests/test_t3_full.c tests/test_save_load.c tests/test_tline.c "
    "tests/test_child_conflict.c sweep_tracking.c"
)

FULL_VALS = [
    80, 90, 100, 110, 120, 125, 130, 133, 134, 135, 136, 137,
    138, 139, 140, 141, 143, 145, 150, 155, 160, 170, 180, 200,
]
QUICK_VALS = [100, 125, 137, 150, 180]

def main():
    quick = "--quick" in sys.argv
    nvals = QUICK_VALS if quick else FULL_VALS
    os.chdir(os.path.dirname(os.path.abspath(__file__)))

    outfile = "sweep_results.tsv"
    header = None
    rows = []

    print(f"=== N-SWEEP: {len(nvals)} values {'(quick)' if quick else '(full)'} ===")
    t0 = time.time()

    VCVARS = r'"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64'

    for n in nvals:
        # Build with -DSUBSTRATE_INT=Nu (need vcvarsall for cl.exe)
        cmd_build = (
            f'cmd /c "{VCVARS} >nul 2>&1 && '
            f'nvcc -O2 -arch=sm_75 -DSUBSTRATE_INT={n}u '
            f'-o xyzt_sweep.exe {SRCFILES}"'
        )
        print(f"  N={n:4d} building...", end="", flush=True)
        r = subprocess.run(cmd_build, shell=True, capture_output=True, text=True)
        if r.returncode != 0:
            print(f" BUILD FAILED")
            print(r.stderr[:500])
            continue

        # Run tracking
        print(" running...", end="", flush=True)
        t1 = time.time()
        r = subprocess.run(
            ["./xyzt_sweep.exe", "tracking"],
            capture_output=True, text=True, timeout=600
        )
        dt = time.time() - t1

        if r.returncode != 0:
            print(f" RUN FAILED rc={r.returncode} ({dt:.1f}s)")
            if r.stderr:
                print(f"    stderr: {r.stderr[:200]}")
            continue

        # Parse output: skip "===" line, first non-=== line is header, rest is data
        for line in r.stdout.strip().split("\n"):
            line = line.strip()
            if not line or line.startswith("==="):
                continue
            if line.startswith("N\t"):
                header = line
            elif line.startswith("[diag]") or line.startswith("  ["):
                continue  # skip diagnostics
            elif line[0].isdigit():
                rows.append(line)
                print(f" score={line.split(chr(9))[1]} ({dt:.1f}s)")
                break

    # Write results
    with open(outfile, "w") as f:
        if header:
            f.write(header + "\n")
        for row in rows:
            f.write(row + "\n")

    elapsed = time.time() - t0
    print(f"\n=== Done: {len(rows)}/{len(nvals)} in {elapsed:.0f}s ===")
    print(f"Results: {outfile}")

    # Print summary table
    if rows:
        print(f"\n{'N':>5} {'score':>7} {'recall':>7} {'spec':>7} "
              f"{'err_acc':>8} {'pk_ge':>6} {'pk_acc':>7} "
              f"{'za_pk':>6} {'zb_pk':>6} {'z_rat':>6}")
        print("-" * 72)
        for row in rows:
            cols = row.split("\t")
            if len(cols) >= 26:
                print(f"{cols[0]:>5} {cols[1]:>7} {cols[2]:>7} {cols[3]:>7} "
                      f"{cols[17]:>8} {cols[21]:>6} {cols[22]:>7} "
                      f"{cols[23]:>6} {cols[24]:>6} {cols[25]:>6}")


if __name__ == "__main__":
    main()
