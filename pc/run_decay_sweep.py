"""Decay × N sweep: run tracking at 3 decay rates × N values.
Tests whether peak position tracks integrator bandwidth.

Decay rates:
  31/32  = 0.96875   τ ≈ 31 ticks (fast decay, short memory)
  63/64  = 0.984375  τ ≈ 63 ticks (current default)
  127/128 = 0.992188 τ ≈ 127 ticks (slow decay, long memory)
"""
import subprocess, sys, os, time

SRCFILES = (
    "main.cu substrate.cu yee.cu tests/test_gpu.cu engine.c onetwo.c wire.c "
    "transducer.c reporter.c sense.c tline.c tests/test_core.c "
    "tests/test_lifecycle.c tests/test_observer.c tests/test_stress.c "
    "tests/test_sense.c tests/test_collision.c tests/test_t3_stage1.c "
    "tests/test_t3_full.c tests/test_save_load.c tests/test_tline.c "
    "tests/test_child_conflict.c tests/test_external.c sweep_tracking.c"
)

VCVARS = r'"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64'

DECAY_RATES = [
    ("31/32",  "0.96875f",    31),
    ("63/64",  "0.984375f",   63),
    ("127/128","0.9921875f", 127),
]

# Dense near old peak (137) and new peak region (150-180)
N_VALS = [
    100, 120, 130, 133, 134, 135, 136, 137, 138, 139,
    140, 143, 145, 150, 155, 160, 170, 180, 200,
]

def main():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    t_total = time.time()

    all_results = {}  # decay_label -> [(N, score, recall, spec, full_row)]

    for decay_label, decay_val, tau in DECAY_RATES:
        print(f"\n{'='*60}")
        print(f"  DECAY = {decay_label}  (tau ~{tau} ticks)")
        print(f"{'='*60}")

        results = []
        for n in N_VALS:
            cmd_build = (
                f'cmd /c "{VCVARS} >nul 2>&1 && '
                f'nvcc -O2 -arch=sm_75 -DSUBSTRATE_INT={n}u '
                f'-DYEE_ACC_DECAY={decay_val} '
                f'-o xyzt_sweep.exe {SRCFILES}"'
            )
            print(f"  N={n:4d} building...", end="", flush=True)
            r = subprocess.run(cmd_build, shell=True, capture_output=True, text=True)
            if r.returncode != 0:
                print(f" BUILD FAILED")
                print(r.stderr[:300])
                continue

            print(" running...", end="", flush=True)
            t1 = time.time()
            r = subprocess.run(
                ["./xyzt_sweep.exe", "tracking"],
                capture_output=True, text=True, timeout=600
            )
            dt = time.time() - t1

            if r.returncode != 0:
                print(f" RUN FAILED ({dt:.1f}s)")
                continue

            # Parse the data row
            for line in r.stdout.strip().split("\n"):
                line = line.strip()
                if not line or line.startswith("===") or line.startswith("N\t"):
                    continue
                if line.startswith("[") or line.startswith("  ["):
                    continue
                if line[0].isdigit():
                    cols = line.split("\t")
                    score = cols[1] if len(cols) > 1 else "?"
                    recall = cols[2] if len(cols) > 2 else "?"
                    spec = cols[3] if len(cols) > 3 else "?"
                    results.append((n, score, recall, spec, line))
                    print(f" score={score} recall={recall} ({dt:.1f}s)")
                    break

        all_results[decay_label] = results

    # Write combined TSV
    with open("decay_sweep_results.tsv", "w") as f:
        f.write("decay\tN\tscore\trecall\tspec\n")
        for decay_label, rows in all_results.items():
            for n, score, recall, spec, _ in rows:
                f.write(f"{decay_label}\t{n}\t{score}\t{recall}\t{spec}\n")

    # Print summary
    elapsed = time.time() - t_total
    print(f"\n{'='*60}")
    print(f"  DECAY SWEEP COMPLETE — {elapsed:.0f}s total")
    print(f"{'='*60}\n")

    # Side-by-side comparison
    print(f"{'N':>5}", end="")
    for label, _, tau in DECAY_RATES:
        print(f"  {label:>8} (τ={tau})", end="")
    print()
    print("-" * 60)

    for n in N_VALS:
        print(f"{n:>5}", end="")
        for label, _, _ in DECAY_RATES:
            rows = all_results.get(label, [])
            match = [r for r in rows if r[0] == n]
            if match:
                print(f"  {match[0][1]:>8}", end="")
            else:
                print(f"  {'---':>8}", end="")
        print()

    # Find peaks
    print()
    for label, _, tau in DECAY_RATES:
        rows = all_results.get(label, [])
        if rows:
            best = max(rows, key=lambda r: float(r[1]))
            print(f"  {label:>8} peak: N={best[0]} score={best[1]} recall={best[2]}")


if __name__ == "__main__":
    main()
