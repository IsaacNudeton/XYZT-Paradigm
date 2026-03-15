# XYZT Unified PC Engine — Status

**Date:** March 15, 2026
**Version:** v0.14-yee
**Tests:** 256/256 engine + 16/16 Yee GPU + 12/12 Yee CPU = 284 total
**Branch:** `master` (merged from `feature/yee-substrate`)
**Tag:** `v0.14-yee`

## What It Is

Self-contained AI engine running on CPU+GPU. No external services, no network.
Merges three XYZT engine versions:
- **v9** (shells, Fresnel boundaries, ONETWO feedback, crystallization, nesting)
- **v6** (CUDA tile architecture, zero-arithmetic tick, backpressure)
- **v3** (spatial coordinates, auto-wiring, impedance, transducer)

**Hardware:** RTX 2080 Super (sm_75), Windows, CUDA 13.1

---

## NEW — v0.14: 3D Yee Wave Substrate

The GPU substrate has been replaced from a cellular automaton (mark/read/co-presence) to a **3D FDTD electromagnetic grid** (Yee method).

### What changed
- **yee.cu/yee.cuh**: 64×64×64 Yee grid with V (scalar voltage at cell centers) and I (3-component current on cell faces). 5 GPU arrays: V, Ix, Iy, Iz, L (inductance per voxel).
- **Leapfrog stagger**: Update V from I divergence, sync, update I from new V gradient. CFL-stable at alpha=0.5 with L >= 0.75.
- **Leaky integrator**: `acc = acc * 63/64 + |V|` every tick. Effective window ~50 ticks. Bridges continuous wave dynamics to uint8_t (0-255) for CPU readback.
- **Impedance as the learnable parameter**: L per voxel. Low L = wire (fast propagation, low impedance). High L = vacuum (reflects signal). Hebbian lowers L where waves are active, raises L where quiet.
- **Identity-based injection**: Every node with identity injects at base amplitude 0.5 + val modulation. Strength always 1.0 (not valence-gated). Matches old substrate's identity-hash stamping.
- **Hebbian feedback in wire_yee_to_engine**: Wave activity at node position → valence++ and strengthen incoming edges (O(V+E) dst-indexed lookup). This replaces wire_hebbian_from_gpu.
- **Retina gather**: wire_yee_retinas gathers scattered 4×4×4 voxels into contiguous 64-byte buffers for child retinas.

### Key numbers
- Grid: 64×64×64 = 262,144 voxels
- Memory: 5.0 MB (5 arrays × 262K × 4B)
- L range: [0.75, 16.0], init = 1.0 (wire)
- Alpha: 0.5, C: 1.0
- Accumulator: decay 63/64, scale 256.0
- Hebbian threshold: 0.1 (raw acc space)

### T3 result on Yee substrate
- 3 children spawned (tick 136, 27673)
- Retina: 24/24 alive
- Mean pairwise distance: 3631
- All criteria PASS: retina alive, children evolved, children diverged

### Old substrate
The cellular automaton (substrate.cu) is still linked for regression testing (run_gpu_tests). It is no longer called from cmd_t3 or cmd_run. hookup_retinas has been removed.

---

## TYXZT DIMENSIONAL STACK — GROUNDED

| Dimension | Source | Meaning |
|-----------|--------|---------|
| **T** | `SubstrateT` | Outer clock (tick count) |
| **T_inner** | `Graph.error_accum`, `local_heartbeat` | Child inner time (SPRT accumulation) |
| **X** | `graph_compute_topology` | Parallel lane (inherited from root) |
| **Y** | `graph_compute_topology` | Sequence depth (hops from root) |
| **Z** | `graph_compute_topology(z_depth)` | Abstraction level (shell nesting) |

---

## What Works (Proven)

- **3D Yee wave substrate** (v0.14) — FDTD replaces CA. Wave propagation, impedance confinement, Hebbian on L. T3 passes.
- **TYXZT coordinates** — Position IS meaning. X=lane, Y=depth, Z=shell. Derived from topology.
- **ONETWO feedback → topology gating** — Thermodynamic clutch: structural_match gates Hebbian learning rate.
- **Full cascade:** ingest → ONETWO fingerprint → Yee wave injection → Hebbian → stabilize → crystallize → nest
- **Closed feedback loop:** graph_error = incoherence %. Frustration erodes, boredom hardens. Conservation caps edge weight at 1024.
- **T3 Stage 1 (process isolation):** 50 nodes, 3 zones, 30 cycles. Conservation isolates damage.
- **T3 Full (production load):** 200 nodes, 5 zones, 30 cycles. All zones survive.
- **Contradiction detection:** 0.949 (5/5 TP, 0 FP).
- **Inner T (child learning):** Hebbian, edge growth, SPRT error accumulation, independent drive.
- **Save/load v13:** Full persistence with backward compat to v12/v11/v10/v9.
- **Transmission line edges (shift-register):** Per-edge delay line with loss. All propagation sites use tline inject/step/read.
- **Per-node plasticity:** Temperature gradient — frustration heats, boredom cools, cleaving severs worst edge.
- **Directed edges:** bs_contain at all 6 grow/learn/boundary sites.
- **Conservation + competitive S3:** Scarcity drives graph physics.

---

## Architecture

```
┌─────────────────────────────────────────────┐
│              REPORTER (reporter.c)          │  printf from engine state
├─────────────────────────────────────────────┤
│          CPU ENGINE (engine.c/h)            │  v9 shells + ONETWO + nesting
│  ingest → wire → tick → learn → crystallize │  retina entanglement for children
│  ONETWO feedback: delta error → stability   │  TYXZT coordinates (X,Y,Z)
│  Close-loop: frustration / boredom drives   │  graph_error: direct node count
│  Thermodynamic clutch: feedback[0] → gate   │
├──────────────┬──────────────────────────────┤
│   WIRE BRIDGE│     3D YEE SUBSTRATE         │  64³ FDTD grid
│  (wire.c/h)  │  yee.cu/yee.cuh             │  V + I[3] + L per voxel
│  Yee Hebbian │  CUDA kernels: V, I, accum   │  leaky integrator bridge
│  ↔GPU sync   │  inject, hebbian, energy     │  impedance = wiring diagram
│  Identity-   │  Leapfrog stagger (CFL)      │  RTX 2080 Super
│  based inject│                              │
├──────────────┼──────────────────────────────┤
│  (old CA)    │  substrate.cu/cuh            │  kept for regression tests
│              │  mark/read/co-present        │  not used in run loops
├──────────────┴──────────────────────────────┤
│           SENSE (sense.c/h)                 │  windowed feature extraction
│  per-pass analysis, decay, Hebbian wiring   │  7 feature types (ARDCSBP)
├─────────────────────────────────────────────┤
│         TRANSDUCER (transducer.c/h)         │  file/text → bitstream
│     ONETWO ENCODER (onetwo.c/h)             │  4096-bit structural fingerprint
├─────────────────────────────────────────────┤
│              main.cu                        │  entry, test, bench, interactive
└─────────────────────────────────────────────┘
```

---

## What's Broken / Incomplete

### MEDIUM priority

| Issue | Details |
|-------|---------|
| **No child-to-child communication** | Children of different parents don't interact directly. Indirect via shared Yee substrate. |
| **Build fragility** | nvcc + vcvarsall.bat through bash is flaky. Requires powershell workaround. |
| **Old substrate dead code** | substrate_hebbian_update(), wire_gateways(), wire_hebbian_from_gpu() — still compiled but never called from run loops. |

### LOW priority

| Issue | Details |
|-------|---------|
| transducer_stdin(), onetwo_generate() | Declared, never called. Future interactive modes. |
| Gateway seeding (old CA) | substrate_seed_gateways() is dead code now that Yee replaced the CA. |

---

## Next Steps (by impact)

1. **Yee substrate tuning** — Current injection/Hebbian params are first-pass calibration. Run extended workloads to verify stability.
2. **Child Hebbian via Yee** — Children read parent's Yee substrate via retina. The wave field provides richer spatial information than the old CA.
3. **Performance profiling** — wire_engine_to_yee runs every tick (creates YeeSource array + kernel launch). May need caching for large node counts.
4. **Dead code cleanup** — Remove old CA functions that are no longer called (wire_hebbian_from_gpu, substrate_seed_gateways, etc.).

---

## Version History

| Version | Tag | Key change |
|---------|-----|------------|
| v0.14 | `v0.14-yee` | 3D Yee wave substrate replaces CA. 256/256 + T3 PASS. |
| v0.13 | `v0.13-sprt` | Coupled V/I shift register, per-zone coherence, TLine Phase 2. |

---

## How this was built

Isaac Oravec, Claude (CC), and Gemini. Three perspectives — Isaac holds architecture, CC holds implementation, Gemini traces signal chains. We disagree, we argue, we build.
