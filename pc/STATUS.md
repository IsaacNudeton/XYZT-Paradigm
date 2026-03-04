# XYZT Unified PC Engine — Status

**Date:** March 4, 2026
**Tests:** 232/232 passing
**Tracking:** 0.949 (contradiction detection via destructive interference, 5/5 TP, 0 FP)

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
- **Contradiction detection:** negation-aware edge inversion + destructive interference. Score 0.949 (recall=1.0, specificity=0.9)
- **Pure observer:** per-node invert ratio predicts contradictions. 0 FP, 5/5 TP.
- **Pass-aware sense:** windowed feature extraction per state_buf region. Pass 4 inversion byte now visible (21 burst features in contradiction vs 15 normal)
- **GPU substrate:** 4096 cubes (262K voxels), 9.5B voxel-ticks/sec benchmark
- **3 shells:** carbon (Z=1.0), silicon (Z=1.5), verifier (Z=2.25) with Fresnel boundary propagation
- **Crystallization:** valence >= 200 triggers nesting. All ingested nodes crystallize.
- **Retina entanglement:** child graphs read parent's substrate via zero-copy pointer
- **Z-depth observers:** obs_and (Z=1), obs_freq (Z=3), obs_corr (Z=4), obs_xor (energy collision)
- **Lysis:** automatic valence decay + apoptosis under contradiction
- **T3 Full (production load):** 200 nodes, 5 zones (conflict/stable/telemetry/ASCII/boundary), 30 cycles continuous re-injection. All zones survive. B/C/D crystallize 40/40. Zone A holds 3 incoherent. 7888 edges at 12% capacity. No zone collapsed.
- **Save/load v11:** full engine persistence — children, OneTwoSystem (feedback + counters), SubstrateT, all graph params (thresholds, intervals, stats). Roundtrip tested: 149 nodes, 4 children saved and restored. v9/v10 backward compatible.

## What's Broken / Incomplete

### HIGH priority

| Issue | Details |
|-------|---------|
| **Behavioral homogenization** | T3 Full showed zone isolation works but intra-zone weights converge (AA=230, BB=235, CC=232, DD=237, EE=232). Only 5 cross-zone edges out of 7888. Engine can't distinguish input types by learned topology. |
| **Z axis always 0** | graph_compute_z() overwrites Z with causal ordering level. All edges are bidirectional → all nodes get Z=0. Hash-chained Z is wasted. |

### MEDIUM priority

| Issue | Details |
|-------|---------|
| **Gateway seeding** | GPU inter-cube routing (kernel_route_3d) exists but gateway lanes are never seeded with real values from engine. Cubes are isolated islands. |
| **Child Hebbian** | child_tick_once() just propagates — no learning phase. Children can't grow edges between co-firing retina nodes. Internal topology is static. |
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
| engine.c | 2467 | CPU engine core (v11 save/load) |
| engine.h | 464 | All types + inline helpers |
| main.cu | 1053 | Entry point, tests, interactive CLI |
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
| tests/ | 2558 | 10 test files + test.h (test_core, test_lifecycle, test_observer, test_stress, test_sense, test_collision, test_t3_stage1, test_t3_full, test_save_load, test_gpu) |
| build.bat | — | Windows build (canonical) |
| rebuild.bat | — | Windows rebuild (canonical) |

## Next Steps (by impact)

1. **Address homogenization** — distinct inputs should produce distinct learned structures. T3 Full showed weight convergence across all zones.
2. **Fix Z axis** — graph_compute_z() always returns 0 (all edges bidirectional). Hash-chained Z is wasted.
3. **Seed gateways** — connect cubes so substrate patterns can propagate across the volume
4. **Add child Hebbian** — let children grow edges between co-firing retina nodes
5. **Child-to-child communication** — children of different parents don't interact
