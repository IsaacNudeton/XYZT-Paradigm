# XYZT Substrate Spec v0.4

**Physics-first. Materials-second. Bench-comparable.**

\---

## What changed from v0.3

v0.3 reached for known materials (LC ladders, YIG, graphene) and retrofitted
XYZT invariants onto them. That's not how we built XYZT itself — we derived
from bedrock, then mapped to measurements.

v0.4 inverts the structure. First: what XYZT's math *forces* about any
substrate (Part A). Second: what's a fabrication choice ("copper weight,"
Isaac's term — dynamical, not topological) (Part B). Third: staged build
combining both, with numerically verified bench predictions (Part C).

This matches the distinction we already derived: **{2,3} gives trace routing
(structure, forced by physics); M is copper weight (scale, chosen by
fabrication).** The substrate spec obeys the same split.

All Part A predictions are physics we have already derived:

* α = Z₀/(2R\_K) — verified to machine precision
* N\_c = 3 — Lean-proved, 53 theorems, 0 sorry
* ωr/c = 1 cutoff — same equation at every scale (black hole through PCB)
* Wyler cascade k = 33 − 3/π² — matches measured α to 0.0002%
* Schwarzschild from telegrapher's equations — machine precision

\---

## PART A — What XYZT Derives (Non-Negotiable)

These are substrate constraints that follow from physics we have already
proven. A substrate that violates any of these cannot run XYZT.

### D1. N\_c = 3: exactly three orthogonal propagation modes

**Derivation:** Three independent constraints intersect uniquely at N\_c = 3.

* **C1 (Niven):** 1/N\_c ∉ {0, 1/4, 1}, so orbit is chaotic (confinement).
N\_c ∈ {1, 4} fail.
* **C2 (Spin-statistics):** N\_c odd, so baryons are fermions (chemistry).
N\_c even fails.
* **C3 (Koide waveguide):** 2 + N\_c ≤ 6, so waveguide is not saturated.
N\_c ≥ 5 fails.

Only N\_c = 3 passes all three. Lean-proved, zero sorry.

**Substrate implication:** the substrate must support exactly three orthogonal
propagating modes. Not two. Not four. Three. Realizations are variant
(charge/thermal/spin channels; TE₁/TE₂/TE₃ modes; ω/2ω/3ω frequency triad;
0°/120°/240° phase quadratures) but count is invariant.

**Substrate Q\_eff = (2 + N\_modes)/6 = 5/6 ≈ 0.833.** Bounded below saturation.
This leaves headroom for Hebbian coupling without collapsing the mode
structure.

### D2. ωr/c = 1: cutoff condition at substrate cell scale

**Derivation:** Waveguide cutoff appears at every scale with the same
equation.

|Scale|Aperture r|Cutoff|Regime|
|-|-|-|-|
|Black hole (solar)|r\_s ≈ 3 km|≈ 16 kHz|Below: opaque|
|Electron (Compton)|ƛ\_e ≈ 3.86e−13 m|≈ 1.2e23 Hz|Below: stable|
|Proton (DIS)|r\_p ≈ 0.84e−15 m|≈ 5.7e25 Hz|Below: sees blob|
|Graphene lattice|≈ 1.4e−10 m|≈ 3.4e20 Hz|Below: below band|
|PCB trace (250μm)|w = 2.5e−4 m|≈ 191 GHz|Below: evanescent|
|Layer 0 cell (3 cm)|3e−2 m|≈ 1.6 GHz|Bench regime|

**Substrate implication:** given a cell size a and a propagation velocity
v = 1/√(L'C'), operating frequency must satisfy ω·a/v < 1 for signals to
propagate. This is not a choice — it's the cutoff.

For a lumped LC cell with L, C (not per-unit-length): cutoff is
f\_c = 1/(π·√(LC)). Operate below this. Above it, signal is evanescent.

### D3. Z₀ and R\_K as natural impedance anchors

**Derivation:** α = e²/(4πε₀ℏc), identically α = Z₀/(2R\_K), where
Z₀ = μ₀c = 376.73 Ω and R\_K = h/e² = 25812.8 Ω.

Verified numerically to machine precision. This is not a derivation attempt —
it's an identity of quantities we already measure.

**Substrate implication:** the universe provides two fundamental impedance
scales, related by α. A substrate spanning from Z₀ (propagation layer) to
R\_K (quantum coherence layer) has impedance contrast 2/α ≈ 274×, provided
for free by fundamental constants. The substrate does not need to engineer
this contrast — it inherits it by choosing materials at both scales.

**Concrete consequence:** Layer 2b (graphene/YIG heterostructure) operates
across both scales. Graphene Dirac fluid couples to R\_K (quantum Hall
regime). YIG below operates near Z₀. The impedance ratio between layers is
a physical constant, not a fabrication parameter.

### D4. Statz-deMars / Lotka-Volterra gain kernel

**Derivation:** from the gain kernel physics already Lean-verified (10
theorems). Metabolic capacity C has a fixed point; response grows with signal
up to saturation, decays with recovery. Same structure as laser rate
equations (Statz-deMars 1960), Lotka-Volterra (1925), population inversion,
BEC dynamics.

&#x20;   dM/dt = α·M·(1 − M/C) − γ·M + ξ(t)


* α (gain coefficient): from pump (see D5).
* C (capacity): intrinsic to material. Lean-proven invariant when C is
frozen from Adam-style updates; required for reservoir stability.

**Substrate implication:** the substrate must exhibit this response shape
intrinsically. Redistributive nonlinearity (parametric instability, Suhl
processes, magnon-magnon scattering) gives C natively. External clipping
circuits do not satisfy this — they violate D5.

### D5. I6: Information boundary

**Derivation:** XYZT's core paradigm claim. Any information-bearing switch
(transistor, gate, FET) in the propagation/learning loop reintroduces the
von Neumann architecture XYZT eliminates.

**I6a (hard):** zero information-bearing switches. Signal and geometry
determine routing. This is non-negotiable.

**I6b (permissive):** uniform unpatterned energy bath (RF, optical, thermal)
allowed to supply α in D4. Biological analog: ATP supplies metabolic
capacity; synaptic weights are the information.

**Timing constraint on pump:** to prevent smuggled clock, pump must be CW or
τ\_pump strictly decoupled from τ\_prop (either τ\_pump ≫ τ\_prop or
τ\_pump ≪ τ\_prop). Envelope frequency must not align with computational
propagation time.

### D6. Hebbian rule from telegrapher's equations

**Derivation:** in the transmission line universe, mass is dL/dt·κ·u where u
is local energy density. At universal scale this is back-reaction — the
mechanism by which E=mc² operates on the Yee grid.

At substrate scale, the same equation gives the Hebbian rule:

&#x20;   dZ(x)/dt = f(|ψ(x,t)|², history)


with relaxation window τ\_prop ≪ τ\_relax ≪ τ\_experiment.

**Substrate implication:** signal energy must modify local impedance,
persistently but not forever. Candidate mechanisms (all variant
realizations of the same invariant): ferromagnetic hysteresis,
photorefractive index change, magnetostriction, electromigration,
phase-change materials, chemical product accumulation.

\---

## PART B — What Fabrication Picks ("Copper Weight")

These are choices. They do not follow from XYZT physics. They are engineering
selections within the envelope Part A defines.

* **Specific material composition.** YIG vs twisted bilayer graphene vs
(R,Gd)₃(Fe,Ga)₅O₁₂ vs magnetoelastic hybrid. All can satisfy Part A
if designed to.
* **Operating frequency within the D2-derived range.** Cutoff sets the
ceiling. Absolute value is chosen for fab convenience and available
instrumentation.
* **Absolute impedance level.** Anywhere between Z₀ and R\_K. Scaling within
the window is a fab choice.
* **Specific learning/memory mechanism.** Remanence, photorefractive,
magnetostrictive — all realize D6. The pick is material-specific.
* **Pump modality.** Parametric microwave, Floquet optical, thermal bias —
all realize D5 I6b. Pick per material.
* **Observer readout.** Quantum capacitance, SAW magnetostriction, THz
spectroscopy, near-field probe. Orthogonal-to-compute is preferred per D5.

**This split is the answer to "are we deriving or reaching":** we derive the
structure (Part A); we pick the scale (Part B). Both are honest work.
Claiming we derive the material composition would be overfitting XYZT's
universal-scale derivations onto substrate-scale fabrication — the same
mistake as claiming {2,3} derives the Higgs VEV at 246 GeV. It doesn't.
{2,3} derives the Koide routing; VEV is dynamical/scale, a "copper weight"
at the cosmological level.

\---

## PART C — Staged Build with Verified Predictions

Each layer combines Part A structure with Part B choices. Each has
numerically verified bench-comparable predictions from the stress test
script (xyzt\_stress\_test/layer0\_sweep.py).

### Layer 0 — Passive LC Ladder \[T3, buildable, \~$150]

**Part A satisfied:** D1 (we can engineer 3-mode structure later), D2
(below-cutoff operation), D3 (Z₀-scale), D5 I6a (passive), not D3 R\_K-scale
(graphene wasn't in Layer 0).

**Part B chosen:** commodity air-core inductors + C0G ceramics, step
discontinuity at node 16, 32 nodes, 1 MHz operation.

**Exact predictions from simulation (transfer matrix, verified against
analytical Fresnel):**

| Ratio Z\_h/Z\_l | Z\_low (Ω) | Z\_high (Ω) | |Γ| | |Γ|² |
|---------------|-----------|------------|-----|------|
| 1.0           | 158       | 158        | 0.005| 0.0000 |
| 1.5           | 158       | 237        | 0.200| 0.0400 |
| 2.0           | 158       | 316        | 0.333| 0.1111 |
| 3.0           | 158       | 474        | 0.500| 0.2500 |
| 4.0           | 158       | 632        | 0.600| 0.3600 |
| 5.0           | 158       | 790        | 0.667| 0.4445 |

**Sealed predictions (P0.x, commit before bench measurement):**

* **P0.1:** At 1 MHz with 4:1 impedance ratio (Z\_low=158 Ω, Z\_high=632 Ω),
measured |Γ| at the discontinuity = 0.600 ± 0.05 (analytical Fresnel
matches transfer-matrix sim to all decimals shown).
* **P0.2:** Frequency sweep 0.1–3 MHz shows |Γ| = 0.600 constant (flat
below cutoff). At f = 5 MHz, |Γ| rises to 0.98 (cutoff). At f = 10 MHz,
|Γ| = 1.0 (evanescent, total reflection).
* **P0.3:** Smooth taper (quarter-wave-like transition) reduces |Γ| from
0.600 (step) to 0.091 (tapered) at 1 MHz, same endpoints. This tests that
the substrate's impedance *profile* matters, not just endpoints.
* **P0.4:** Observer-node accumulator outputs match `accum += v` trace from
xyzt-hardware/pc sim to ≤ 5% per node.

**BOM:** 32× 10μH air-core L + 32× 400pF C0G C (Z\_low), 32× 40μH L +
32× 100pF C (Z\_high), matched SMA terminations, FR4 PCB.

**Tools:** VNA + scope already in Isaac's lab.

\---

### Layer 1 — Ferrite-Loaded Line (Write-Only Hebbian) \[T3, months]

**Part A added vs Layer 0:** D6 (one-way).

**Part B chosen:** NiZn or MnZn ferrite beads at selected junctions.

**Sealed predictions:**

* **P1.1:** After training pulse ≥ 1 μJ/node, pre/post S₂₁ differs by
≥ 3 dB at trained frequency, persisting ≥ 1 s.
* **P1.2:** Opposite-polarity retraining reduces written state
non-monotonically (hysteresis traversal, not simple erase).
* **P1.3:** Measured dμ/dt fits saturating nonlinear f(|ψ|²) with R² > 0.9.

\---

### Layer 2a — YIG Magnonic Network \[T2→T3, 6–18 months]

**Part A added:** D4 (intrinsic via Suhl), D6 (bidirectional via magnon
scattering).

**Part B chosen:** YIG/GGG, parallel parametric pumping, SAW readout.

Q factor 10⁴–10⁵ exceeds the 1/α ≈ 137 bound *because* spin waves decouple
from EM at first order. Physics-consistent high Q.

**Sealed predictions P2a.1–P2a.3** as in v0.3.

\---

### Layer 2b — Graphene/YIG Heterostructure \[T3, 12–24 months]

**Part A added:** D1 native (charge/thermal/spin = 3 modes in one material),
D3 across both Z₀ and R\_K scales, D4 intrinsic (hydrodynamic + magnonic),
D6 via magnetic proximity.

**Part B chosen:** YIG/GGG + hBN-encapsulated graphene + atomic-precision
interface.

Dirac fluid at Dirac point gives native mode-triadicity via Wiedemann-Franz
violation (charge and thermal decouple). Spin is the third channel via
graphene spin currents + proximity-induced exchange from YIG.

Quantized impedance: Dirac fluid locks to R\_K = h/e². Substrate inherits
fundamental constant, not a fab tolerance.

**Sealed predictions P2b.1–P2b.3** as in v0.3.

**Open question Q1 (from v0.3, still open):** gain α in graphene channel —
from YIG layer (hypothesis A) or from Floquet pump (hypothesis B). Needs
resolution before fab commit.

\---

### Layer 3 — Derived Substrate \[T3 targets, years]

**Part A targets:** D1 native triadic, D3 spanning Z₀→R\_K, D4 intrinsic,
D6 via material self-coupling, room-T if possible.

**Part B search space (DFT-screenable):**

* Compensated rare-earth iron garnets with tunable T\_comp near operating T
* Dirac-like 2D materials beyond graphene (silicene, germanene, topological
surface states)
* Magnetoelastic hybrids with strong spin-phonon coupling

**Sealed prediction P3.4 (new in v0.3, retained):** native triadic substrate
outperforms externally-patterned triadic substrate on identical XYZT task by
factor derivable from mode-mixing Hamiltonian. Failure falsifies N\_c = 3
extension to substrate level.

\---

## PART D — Honest Assessment

**What XYZT uniquely derives:**

* Structure: N\_c = 3, impedance ratios locked by α, cutoff ωr/c = 1,
Statz-deMars shape, passive info boundary, Hebbian from telegrapher's.

**What XYZT does NOT derive:**

* Specific material composition. Picking YIG vs graphene is analogous to
picking copper weight on a PCB — dynamical, not topological.

**What this spec does:**

* Derives the structural envelope (Part A).
* Picks materials that fit inside the envelope (Part B).
* Builds and tests staged layers with verified numerical predictions (Part C).

**Falsifiability lock-in:**

* P0.1–P0.4: Layer 0 bench measurements, all within a week once PCB arrives.
* P1.1–P1.3: Layer 1 within 3 months.
* P2a.x, P2b.x: 6–24 months with lab collaboration.
* P3.1–P3.4: DFT screen ranks materials; experimental validation follows.

If P0.1 fails — if the LC ladder doesn't reproduce Fresnel reflection at
the derived impedance step — XYZT's transmission-line-as-computation claim
is wrong at the most basic level. This is the earliest and cheapest
falsifier. Order the parts.

\---

*v0.4. All Part A items are T1 (mathematically verified) or T2 (strong signal
from derivations we've already done, verified to machine precision in
xyzt\_stress\_test/physics\_check.py). Part B items are engineering choices,
not claims. Part C predictions are T3 (falsifiable, untested). P3.4 is T3
with a nontrivial derivation still to complete. Nothing here promoted
beyond earned tier.*

