# XYZT Unified PC Engine — Status

**Date:** February 28, 2026
**Tests:** 175/175 passing
**Tracking:** 0.949 (contradiction detection via destructive interference)

## What It Is

Self-contained AI engine running on CPU+GPU. No external services, no network.
Merges three XYZT engine versions:
- **v9** (shells, Fresnel boundaries, ONETWO feedback, crystallization, nesting)
- **v6** (CUDA tile architecture, zero-arithmetic tick, backpressure)
- **v3** (spatial coordinates, auto-wiring, impedance, transducer)

**Hardware:** RTX 2080 Super (sm_75), Windows, CUDA 13.1

## What Works (Proven)

- **Full cascade:** ingest → ONETWO fingerprint → GPU co-presence → Hebbian → stabilize → crystallize → nest
- **Contradiction detection:** negation-aware edge inversion + destructive interference. Score 0.949 (recall=1.0, specificity=0.9)
- **Pass-aware sense:** windowed feature extraction per state_buf region. Pass 4 inversion byte now visible (21 burst features in contradiction vs 15 normal)
- **GPU substrate:** 4096 cubes (262K voxels), 9.5B voxel-ticks/sec benchmark
- **3 shells:** carbon (Z=1.0), silicon (Z=1.5), verifier (Z=2.25) with Fresnel boundary propagation
- **Crystallization:** valence >= 200 triggers nesting. All ingested nodes crystallize.
- **Retina entanglement:** child graphs read parent's substrate via zero-copy pointer
- **Z-depth observers:** obs_and (Z=1), obs_freq (Z=3), obs_corr (Z=4), obs_xor (energy collision)
- **Lysis:** automatic valence decay + apoptosis under contradiction
- **Save/load:** binary format, shells + boundary edges (partial — see gaps)

## What's Broken / Incomplete

### HIGH priority

| Issue | Details |
|-------|---------|
| **Save/load loses nesting** | engine_save() doesn't persist child_pool, child_owner, onetwo state, or SubstrateT. Loading a saved engine drops all children and pattern history. |
| **T3 not run yet** | Retina code is in and tests pass, but we haven't tested whether children develop distinct topologies from distinct parents with GPU live. |
| **Behavioral homogenization** | Acid test (3 text + 1 binary, 25K ticks) showed spatial separation exists but all files crystallize identically. Edge weights converge to same values. ONETWO fingerprints lose distinctness through mutual containment averaging. |
| **Z axis always 0** | graph_compute_z() overwrites Z with causal ordering level. All edges are bidirectional → all nodes get Z=0. Hash-chained Z is wasted. |

### MEDIUM priority

| Issue | Details |
|-------|---------|
| **Gateway seeding** | GPU inter-cube routing (kernel_route_3d) exists but gateway lanes are never seeded with real values from engine. Cubes are isolated islands. |
| **Child Hebbian** | child_tick_once() just propagates — no learning phase. Children can't grow edges between co-firing retina nodes. Internal topology is static. |
| **No child-to-child communication** | Children of different parents don't interact. No substrate-level connection between child graphs. |
| **Build fragility** | nvcc + vcvarsall.bat through bash is flaky. Requires powershell workaround. do_build.bat works but isn't tracked. |

### LOW priority (dead code / API bloat)

| Issue | Details |
|-------|---------|
| substrate_hebbian_update() | Declared in substrate.cuh, implemented in substrate.cu, never called. GPU kernel does Hebbian internally. |
| wire_gateways() | Implemented in wire.c, never called. GPU routing subsumes it. |
| transducer_ingest(), transducer_stdin() | Declared, never called. Future interactive modes. |
| onetwo_generate() | Inverse parse (bitstream → bytes). Declared, not used. |
| test_hash.c | Standalone utility, not linked into main build. |

## Architecture

```
┌─────────────────────────────────────────────┐
│              REPORTER (reporter.c)          │  printf from engine state
├─────────────────────────────────────────────┤
│          CPU ENGINE (engine.c/h)            │  v9 shells + ONETWO + nesting
│  ingest → wire → tick → learn → crystallize │  retina entanglement for children
│  ONETWO feedback: delta error → stability   │
├──────────────┬──────────────────────────────┤
│   WIRE BRIDGE│     GPU SUBSTRATE (3D)       │  v6 cubes + v3 spatial coords
│  (wire.c/h)  │  substrate.cu/cuh            │  262K voxels, shift registers
│  Hebbian CPU │  CUDA kernels: tick, route   │  threshold tap, co-presence
│  ↔GPU sync   │  observe, auto-wire, inject  │  RTX 2080 Super
├──────────────┴──────────────────────────────┤
│         TRANSDUCER (transducer.c/h)         │  file/text → bitstream
│     ONETWO ENCODER (onetwo.c/h)             │  4096-bit structural fingerprint
├─────────────────────────────────────────────┤
│              main.cu                        │  entry, test, bench, interactive
└─────────────────────────────────────────────┘
```

## File Inventory

| File | Lines | Role |
|------|-------|------|
| engine.c | 1350 | CPU engine core |
| engine.h | 396 | All types + inline helpers |
| main.cu | 564 | Entry point, tests, interactive CLI |
| substrate.cu | 500 | 7 CUDA kernels |
| substrate.cuh | 167 | GPU types + host API |
| onetwo.c | 329 | ONETWO encoder |
| onetwo.h | 61 | ONETWO API |
| wire.c | 217 | CPU↔GPU bridge |
| wire.h | 37 | Wire API |
| transducer.c | 149 | Input abstraction |
| transducer.h | 53 | Transducer API |
| reporter.c | 139 | Status output |
| Makefile | 29 | Linux build (unused) |
| build.bat | — | Windows build |

## Next Steps (by impact)

1. **Sense → engine feedback** — windowed sense detects contradiction (burst diff in pass 4). Feed this back to accelerate lysis.
2. **Run T3** — ingest 3+ files with GPU live, tick 25K, check if children develop distinct topologies
3. **Fix save/load** — persist child_pool, child_owner, onetwo, SubstrateT
4. **Seed gateways** — connect cubes so substrate patterns can propagate across the volume
5. **Add child Hebbian** — let children grow edges between co-firing retina nodes
6. **Address homogenization** — distinct inputs should produce distinct learned structures
