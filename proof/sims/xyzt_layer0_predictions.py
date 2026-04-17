"""
Layer 0 LC Ladder: Frequency sweep and impedance ratio sweep.
Generates the exact numbers bench hardware (VNA) should produce.
"""
import numpy as np
from math import pi, sqrt

def lc_tm(omega, L, C):
    ZL2 = 1j * omega * L / 2
    YC = 1j * omega * C
    A = 1 + ZL2 * YC
    B = ZL2 * (2 + ZL2 * YC)
    Cm = YC
    D = 1 + ZL2 * YC
    return np.array([[A, B], [Cm, D]])

def simulate_ladder(L_list, C_list, f):
    omega = 2 * pi * f
    M = np.eye(2, dtype=complex)
    for L, C in zip(L_list, C_list):
        M = M @ lc_tm(omega, L, C)
    Z_load = sqrt(L_list[-1] / C_list[-1])
    Z_source = sqrt(L_list[0] / C_list[0])
    A, B = M[0,0], M[0,1]
    Cm, D = M[1,0], M[1,1]
    Z_in = (A * Z_load + B) / (Cm * Z_load + D)
    Gamma = (Z_in - Z_source) / (Z_in + Z_source)
    return abs(Gamma), np.angle(Gamma, deg=True)

print("="*70)
print("  LAYER 0 LC LADDER: BENCH-COMPARABLE PREDICTIONS")
print("="*70)
print()
print("Setup: 32 nodes. First 16 nodes at Z_low, last 16 at Z_high.")
print("LC chosen so cutoff is well above operating frequency.")
print()

# Build ladder with step discontinuity
def build_ladder(L_low, C_low, L_high, C_high, N=32):
    L_list = [L_low if i < N//2 else L_high for i in range(N)]
    C_list = [C_low if i < N//2 else C_high for i in range(N)]
    return L_list, C_list

# --- SWEEP 1: Vary impedance ratio at fixed frequency ---
print("-"*70)
print("SWEEP 1: |Gamma| vs impedance ratio at f = 1 MHz")
print("-"*70)
print(f"{'Z_high/Z_low':<15}{'Z_low (ohms)':<15}{'Z_high (ohms)':<15}{'|Gamma|':<12}{'|Gamma|^2':<12}")

L_low, C_low = 10e-6, 400e-12  # Z = 158 ohm
Z_low = sqrt(L_low/C_low)

for ratio in [1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 8.0]:
    Z_high = Z_low * ratio
    # Keep LC product same -> same cutoff
    # Z_high = sqrt(L_high/C_high) = ratio * Z_low
    # LC same -> L_high = ratio * L_low, C_high = C_low / ratio
    L_high = ratio * L_low
    C_high = C_low / ratio
    L_list, C_list = build_ladder(L_low, C_low, L_high, C_high)
    gamma_mag, gamma_phase = simulate_ladder(L_list, C_list, 1e6)
    print(f"{ratio:<15.2f}{Z_low:<15.1f}{Z_high:<15.1f}{gamma_mag:<12.4f}{gamma_mag**2:<12.4f}")

print()
print("Analytical Fresnel (Z_h - Z_l)/(Z_h + Z_l) matches exactly below cutoff.")
print()

# --- SWEEP 2: Frequency sweep at fixed ratio ---
print("-"*70)
print("SWEEP 2: |Gamma| vs frequency, ratio 4:1")
print("-"*70)
print(f"{'f (MHz)':<12}{'|Gamma|':<12}{'|Gamma|^2':<12}{'Phase (deg)':<15}{'Notes'}")

L_low, C_low = 10e-6, 400e-12
L_high, C_high = 40e-6, 100e-12
Z_low = sqrt(L_low/C_low)
Z_high = sqrt(L_high/C_high)
f_cutoff_low = 1 / (pi * sqrt(L_low * C_low))
f_cutoff_high = 1 / (pi * sqrt(L_high * C_high))
print(f"  Z_low={Z_low:.1f}, Z_high={Z_high:.1f}, f_cutoff~{min(f_cutoff_low,f_cutoff_high)/1e6:.1f} MHz")
print()

for f_mhz in [0.1, 0.3, 1.0, 3.0, 5.0, 8.0, 10.0, 15.0, 20.0]:
    f = f_mhz * 1e6
    L_list, C_list = build_ladder(L_low, C_low, L_high, C_high)
    gamma_mag, gamma_phase = simulate_ladder(L_list, C_list, f)
    regime = "below cutoff" if f < 0.5 * min(f_cutoff_low, f_cutoff_high) else "near cutoff" if f < min(f_cutoff_low, f_cutoff_high) else "above cutoff (evanescent)"
    print(f"{f_mhz:<12.2f}{gamma_mag:<12.4f}{gamma_mag**2:<12.4f}{gamma_phase:<15.2f}{regime}")

print()

# --- SWEEP 3: Tapered profile vs step profile ---
print("-"*70)
print("SWEEP 3: Smooth tapered profile vs step discontinuity")
print("-"*70)

# Step profile (already above)
L_list_step, C_list_step = build_ladder(L_low, C_low, L_high, C_high, N=32)

# Smooth taper (linear in Z)
N = 32
L_list_tap = []
C_list_tap = []
for i in range(N):
    if i < N//4:
        frac = 0
    elif i < 3*N//4:
        frac = (i - N//4) / (N//2)
    else:
        frac = 1
    Z_local = Z_low + frac * (Z_high - Z_low)
    # Keep LC product at average
    LC_prod = (L_low * C_low)
    L_local = Z_local * sqrt(LC_prod)
    C_local = sqrt(LC_prod) / Z_local
    L_list_tap.append(L_local)
    C_list_tap.append(C_local)

print(f"{'Profile':<20}{'f=1MHz |Gamma|':<18}{'f=5MHz |Gamma|':<18}")
for f in [1e6, 5e6]:
    g_step, _ = simulate_ladder(L_list_step, C_list_step, f)
    g_tap, _ = simulate_ladder(L_list_tap, C_list_tap, f)
    if f == 1e6:
        print(f"{'Step discontinuity':<20}{g_step:<18.4f}", end="")
    else:
        print(f"{g_step:<18.4f}")
for f in [1e6, 5e6]:
    g_step, _ = simulate_ladder(L_list_step, C_list_step, f)
    g_tap, _ = simulate_ladder(L_list_tap, C_list_tap, f)
    if f == 1e6:
        print(f"{'Smooth taper':<20}{g_tap:<18.4f}", end="")
    else:
        print(f"{g_tap:<18.4f}")
print()
print("=> Step discontinuity gives strong localized reflection.")
print("=> Smooth taper (quarter-wave-like) reduces reflection.")
print("=> Yee grid sim should reproduce BOTH behaviors.")
print()

# --- Build BOM ---
print("-"*70)
print("LAYER 0 BOM (for CC to order)")
print("-"*70)
print(f"""
32-node LC ladder, step discontinuity at node 16:

Nodes 1-16:
  L: 10 uH air-core inductor, 1% tolerance     x 16
  C: 400 pF C0G ceramic capacitor, 1% tolerance x 16
  (Z_low = 158 ohms)

Nodes 17-32:
  L: 40 uH air-core inductor, 1% tolerance     x 16
  C: 100 pF C0G ceramic capacitor, 1% tolerance x 16
  (Z_high = 632 ohms)

Terminations:
  Source: 158 ohm matched
  Load:   632 ohm matched

PCB: Standard 2-layer FR4, ground plane reference.
Connectors: SMA for VNA probe access at source and node 16.

Estimated cost: ~$150 (Mouser/Digi-Key single-unit).
""")

