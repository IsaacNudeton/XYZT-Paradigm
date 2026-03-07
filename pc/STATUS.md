# XYZT Unified PC Engine — Status

**Date:** March 6, 2026
**Tests:** 243/243 passing
**Tracking:** 0.900 (contradiction detection via destructive interference, 5/5 TP, 0 FP — down from 0.949 after directed edges changed the containment denominator)

## What It Is

Self-contained AI engine running on CPU+GPU. No external services, no network.
Merges three XYZT engine versions:
- **v9** (shells, Fresnel boundaries, ONETWO feedback, crystallization, nesting)
- **v6** (CUDA tile architecture, zero-arithmetic tick, backpressure)
- **v3** (spatial coordinates, auto-wiring, impedance, transducer)

**Hardware:** RTX 2080 Super (sm_75), Windows, CUDA 13.1

## What Works (Proven)

- **Full cascade:** ingest → ONETWO fingerprint → GPU co-presence → Hebbian → stabilize → crystallize → nest
- **Closed feedback loop:** `graph_error` = incoherence percentage (0-100). Dual thresholds: fp_thresh=34 (fingerprint delta), ge_thresh=14% (graph_error, derived from mismatch tax). Floor at 30 nodes — below that, fingerprint-only. Frustration erodes worst incoherent crystal, boredom hardens coherent nodes.
- **T3 Stage 1 (process isolation):** 50 nodes, 3 zones (conflict/stable/boundary), 30 cycles continuous re-injection. Zone B recovers in 5 cycles, holds 15/15 crystallized. AC edges starved to weight 53, BB stays at 123. Conservation isolates the sick process.
- **Bus collision test:** 15 raw packets, 3 groups, continuous re-injection. Structural differentiation via topology. Protocol-agnostic.
- **Conservation:** `MAX_NODE_WEIGHT=1024` budget per node. Competitive S3: at capacity, active edges steal from weakest. Sense decay to zero — silence = understanding.
- **Directed edges:** `bs_contain` replaces `bs_mutual_contain` at all 6 grow/learn/boundary sites. Single-wire grow (reverse forms naturally in peer's grow cycle). Child tick fix ensures crystallized parents with only outgoing edges still resolve.
- **Contradiction detection:** negation-aware edge inversion + destructive interference. Score 0.900 (recall=1.0, specificity=0.9). Down from 0.949 — asymmetric containment changes the denominator.
- **Pure observer:** per-node invert ratio predicts contradictions. 0 FP, 5/5 TP.
- **Pass-aware sense:** windowed feature extraction per state_buf region. Pass 4 inversion byte now visible (21 burst features in contradiction vs 15 normal)
- **GPU substrate:** 4096 cubes (262K voxels), 9.5B voxel-ticks/sec benchmark
- **3 shells:** carbon (Z=1.0), silicon (Z=1.5), verifier (Z=2.25) with Fresnel boundary propagation
- **Crystallization:** valence >= 200 triggers nesting. All ingested nodes crystallize.
- **Retina entanglement:** child graphs read parent's substrate via zero-copy pointer
- **Z-depth observers:** obs_and (Z=1), obs_freq (Z=3), obs_corr (Z=4), obs_xor (energy collision)
- **Lysis:** automatic valence decay + apoptosis under contradiction
- **T3 Full (production load):** 200 nodes, 5 zones (conflict/stable/telemetry/ASCII/boundary), 30 cycles continuous re-injection. All zones survive. B/C/D crystallize 40/40. Zone A holds 3 incoherent. 7888 edges at 12% capacity. No zone collapsed.
- **Inner T (child learning):** `child_tick_once` has Hebbian (co-active strengthen, inactive weaken), edge growth (co-active pairs → output node), and local heartbeat at `SUBSTRATE_INT/4` with SPRT error accumulator. Independent drive state: frustration accelerates growth, boredom crystallizes edges. Diagnostic: 73K learns, 36 edges (from 4), 61 heartbeats, drive=1.
- **Save/load v12:** full engine persistence — children, inner T state (error_accum, prev_output, local_heartbeat, drive), OneTwoSystem, SubstrateT, all graph params (15 params, was 11). v11/v10/v9 backward compatible.

## What's Broken / Incomplete

### HIGH priority

| Issue | Details |
|-------|---------|
| **Z axis still 0** | Directed edges + inner T are in, but containment asymmetry is too small (A→E=85, E→A=80, delta=5). 4514 bidir vs 4 unidir edges. `grow_mean` homogenizes the asymmetry. Per-zone grow thresholds needed. |
| **Behavioral homogenization** | `grow_mean` is global — all zones use the same threshold. Per-zone MDL splitting criterion is the fix. Inner T changed weight dynamics: frustrated children attract weight toward sick zones (T3 weight-flow direction reversed). Isolation still holds. |

### MEDIUM priority

| Issue | Details |
|-------|---------|
| **Gateway seeding** | GPU inter-cube routing (kernel_route_3d) exists but gateway lanes are never seeded with real values from engine. Cubes are isolated islands. |
| **Child pruning** | Children grow edges (4→36) but never prune. At 36 edges on 9 nodes, children saturate. Need weight-based pruning or edge capacity limit. |
| **No child-to-child communication** | Children of different parents don't interact. No substrate-level connection between child graphs. |
| **Build fragility** | nvcc + vcvarsall.bat through bash is flaky. Requires powershell workaround. Canonical scripts: build.bat, rebuild.bat. |

### LOW priority (dead code / API bloat)

| Issue | Details |
|-------|---------|
| substrate_hebbian_update() | Declared in substrate.cuh, implemented in substrate.cu, never called. GPU kernel does Hebbian internally. |
| wire_gateways() | Implemented in wire.c, never called. GPU routing subsumes it. |
| transducer_ingest(), transducer_stdin() | Declared, never called. Future interactive modes. |
| onetwo_generate() | Inverse parse (bitstream → bytes). Declared, not used. |

## Architecture

```
┌─────────────────────────────────────────────┐
│              REPORTER (reporter.c)          │  printf from engine state
├─────────────────────────────────────────────┤
│          CPU ENGINE (engine.c/h)            │  v9 shells + ONETWO + nesting
│  ingest → wire → tick → learn → crystallize │  retina entanglement for children
│  ONETWO feedback: delta error → stability   │
│  Close-loop: frustration / boredom drives   │
│  graph_error: direct incoherent node count  │
├──────────────┬──────────────────────────────┤
│   WIRE BRIDGE│     GPU SUBSTRATE (3D)       │  v6 cubes + v3 spatial coords
│  (wire.c/h)  │  substrate.cu/cuh            │  262K voxels, shift registers
│  Hebbian CPU │  CUDA kernels: tick, route   │  threshold tap, co-presence
│  ↔GPU sync   │  observe, auto-wire, inject  │  RTX 2080 Super
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

## File Inventory

| File | Lines | Role |
|------|-------|------|
| engine.c | 2557 | CPU engine core (v12 save/load, inner T) |
| engine.h | 470 | All types + inline helpers |
| main.cu | 1256 | Entry point, tests, diagnostics, interactive CLI |
| substrate.cu | 520 | 7 CUDA kernels |
| substrate.cuh | 167 | GPU types + host API |
| onetwo.c | 329 | ONETWO encoder |
| onetwo.h | 61 | ONETWO API |
| wire.c | 245 | CPU↔GPU bridge |
| wire.h | 37 | Wire API |
| transducer.c | 149 | Input abstraction |
| transducer.h | 53 | Transducer API |
| reporter.c | 139 | Status output |
| sense.c | 396 | Sense layer (windowed, pass-aware) |
| sense.h | 61 | Sense API |
| sweep_tracking.c | 966 | Parameter sweep + tracking tests |
| tests/ | 2918 | 10 test files + test.h (test_core, test_lifecycle, test_observer, test_stress, test_sense, test_collision, test_t3_stage1, test_t3_full, test_save_load, test_gpu) |
| build.bat | — | Windows build (canonical) |
| rebuild.bat | — | Windows rebuild (canonical) |

## Next Steps (by impact)

1. **Per-zone grow_mean** — MDL splitting criterion instead of global average. The global threshold homogenizes zone topology and flattens containment asymmetry. This is the remaining blocker for Z emergence.
2. **Re-sweep at T3 scale** — 200 nodes, children with inner T, per-zone thresholds. The N-sweep was flat (0.949 at every N, grow_interval pegged to 200). With inner T operational, the sweep may find a resonant frequency.
3. **Child pruning** — children grow 4→36 edges but never prune. Need weight-based pruning or conservation to prevent saturation.
4. **Seed gateways** — connect cubes so substrate patterns can propagate across the volume
5. **Child-to-child communication** — children of different parents don't interact

## What's Done (completed this session)

- ✓ **Inner T** (1c81194) — children learn, grow, accumulate error, drive independently. 73K learns, 36 edges, 61 heartbeats.
- ✓ **Child Hebbian** — co-active retina nodes strengthen, inactive weaken. Part of inner T.
- ✓ **Directed edges** (2249226) — `bs_contain` at all 6 sites, single-wire grow, child tick fix.
- ✓ **v12 save/load** — 15 graph params (inner T fields), v11 backward compatible.
- ✓ **Z-axis diagnostic** — T3-style 5-zone diverse data, per-zone Z reporting, containment asymmetry readout.
