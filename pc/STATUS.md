# XYZT Unified PC Engine — Status

**Date:** March 7, 2026
**Tests:** 252/252 passing
**Tracking:** 0.949 (contradiction detection via destructive interference, 5/5 TP, 0 FP — recovered from 0.900 by per-node grow threshold)

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
- **Per-node grow threshold (MDL-style):** dense nodes (n_in≥4) demand higher correlation. Incoherent nodes get 2/3 threshold. Replaced flat global `grow_mean`. Recovered tracking from 0.900 to 0.949.
- **Transmission line edges (TLineEdge):** Self-contained FDTD edge system alongside existing Edge. Per-cell V[], I[], Lc[] with telegrapher's equations. 9 tests: XNOR/AND/XOR from wave collision, MAJORITY(8/8) from impedance cascade, back-reaction collision-only, propagation delay scales with L, Schwarzschild τ/t exact. Ported from xyzt_unified.c + universe_tline_v2.c.

## What's Broken / Incomplete

### HIGH priority

| Issue | Details |
|-------|---------|
| **Z axis still 0** | Fingerprint asymmetry path is dead (delta stuck at 5, symmetric encoding). Z will come from transmission line edges: propagation depth in cells = Z. TLineEdge proven standalone, needs integration into engine tick cycle. |
| **TLine integration** | TLineEdge is parallel infrastructure — not yet wired into engine. Replace `accum += val * weight / 255` with FDTD propagation. Touches S3 (propagate), S5 (grow), Hebbian, save/load, child_tick_once. Biggest structural change since v9. |

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
| engine.c | 2772 | CPU engine core (v12 save/load, inner T, TLine FDTD) |
| engine.h | 527 | All types + inline helpers + TLineEdge/TLineGraph |
| main.cu | 1258 | Entry point, tests, diagnostics, interactive CLI |
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
| tests/ | 3117 | 11 test files + test.h (test_core, test_lifecycle, test_observer, test_stress, test_sense, test_collision, test_t3_stage1, test_t3_full, test_save_load, test_tline, test_gpu) |
| build.bat | — | Windows build (canonical) |
| rebuild.bat | — | Windows rebuild (canonical) |

## Next Steps (by impact)

1. **TLine integration** — Wire TLineEdge into engine's tick cycle. Replace `accum += val * weight / 255` with FDTD propagation. Start with child edges (smallest blast radius), then main graph. Z emerges as propagation depth.
2. **Re-sweep with TLine** — N-sweep was flat (0.900 at every N). With transmission line edges, different N changes how many FDTD steps elapse between samples — the heartbeat rate becomes a tuning parameter for filter bandwidth.
3. **Child pruning** — children grow 4→36 edges but never prune. Need weight-based pruning or conservation to prevent saturation.
4. **Seed gateways** — connect cubes so substrate patterns can propagate across the volume
5. **Child-to-child communication** — children of different parents don't interact

## What's Done (completed work)

- ✓ **Transmission line edges** (2b177c9) — TLineEdge with FDTD, 9 new tests, gates emerge from wave physics. Proven standalone alongside existing Edge.
- ✓ **Per-node grow threshold** (f978520) — MDL-style local threshold. Dense nodes demand higher correlation. Tracking recovered 0.900→0.949.
- ✓ **Inner T** (1c81194) — children learn, grow, accumulate error, drive independently. 73K learns, 36 edges, 61 heartbeats.
- ✓ **Directed edges** (2249226) — `bs_contain` at all 6 sites, single-wire grow, child tick fix.
- ✓ **v12 save/load** — 15 graph params (inner T fields), v11 backward compatible.
- ✓ **Z-axis diagnostic** — fingerprint asymmetry path ruled out (delta=5, encoding is symmetric). TLine is the path forward.
- ✗ **Directional popcount filter** — tested, no effect (delta stayed at 5). Reverted. Confirmed bottleneck is encoding, not filter.
- ✗ **Hebbian stability hack** — proposed then abandoned. Transmission line edges provide the same filtering through physics (lossy propagation) instead of software.
