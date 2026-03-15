# XYZT — Codebook

Isaac Oravec & Claude — February–March 2026

## Five Principles

**Distinction** — This is not that. 0 or 1. True or false. Everything starts here.

**{2,3}** — Two different things need a third thing connecting them. Two chips need a wire. Two points need a substrate. The smallest structure anything can have.

**ONETWO** — Break apart (ONE), build back up (TWO). How you learn, fix, or make anything.

**XYZT** — What structure looks like when distinction has room to unfold. X=sequence, Y=comparison, Z=depth, T=substrate (the fact that someone is looking).

**Intershell** — You can't trust your own work alone. A second perspective must independently arrive at the same answer.

## Files

### xyzt.c (3302 lines) — The Graph Engine
The full system. Shells, Fresnel boundaries, Hebbian learning, S-matrix resolve, nesting.

Depends on: `onetwo_encode.c` (included via `#include`)

Build: `gcc -O2 -o xyzt xyzt.c -lm`
Run: `./xyzt self-test`
Result: **48/48 passed**

What it does:
- Ingests raw data → bloom-encoded identity + ONETWO structural fingerprint
- Nodes live in shells with impedance boundaries (Fresnel K=Z2/Z1)
- Hebbian learning: edges that fire together strengthen
- Valence crystallization: nodes that survive boundary crossings harden
- Nesting: crystallized nodes (valence ≥ 200) spawn child graphs
- ONETWO feedback loop: 8-channel observation feeds into every tick
- Adaptive timing: intervals scale by K=3/2 impedance
- Z-ordering: toposort causal depth for propagation

### onetwo_encode.c (487 lines) — Structural Fingerprint
4096-bit fixed output encoding. "Send the AI rules OF the data, not the data."

Four sections × 1024 bits:
- **Repetition** (0-1023): run-length spectrum, autocorrelation, frequency shape, byte diversity
- **Opposition** (1024-2047): delta spectrum, XOR spectrum, class transitions, symmetry
- **Nesting** (2048-3071): multi-scale similarity, delimiter depth, substring repeats, entropy gradient
- **Meta** (3072-4095): length class, overall entropy, unique ratio, density

Provides: `onetwo_parse()`, `onetwo_generate()`, `onetwo_self_observe()`

### onetwo.c (1084 lines) — Standalone ONETWO System
The full bidirectional binary architecture. Self-contained. Includes its own grid engine.

Build: `gcc -O2 -o onetwo onetwo.c -lm`
Run: `./onetwo`
Result: **48/48 passed**

What it does:
- Bitstream layer: distinction (the 2)
- ONETWO layer: find opposites, predict, self-observe
- Grid layer: v5 pure accumulation + per-address prediction + residue propagation
- Self-wiring: edges that reduce residue survive, others die
- Computation discovery: discovers XOR, AND, OR from examples — finds both the wiring AND the observer
- Residue = surprise. Silence = understanding. Only new information moves.

Key insight: **propagates residue, not value.** Constant input goes silent. Chaotic input stays loud. The system wires itself toward silence.

### xyzt_v5.c (393 lines) — Pure Accumulation Proof
The proof that one operation (`accum += v`) + observers = all computation.

Build: `gcc -O2 -o xyzt_v5 xyzt_v5.c`
Run: `./xyzt_v5`
Result: **All pass**

Proves: NOT, AND, OR, XOR, majority, all 6 boolean ops from one collision, addition, subtraction, multiplication, comparison, full adder, 4-bit ripple adder, neural XOR, 2-bit counter, SR latch, traffic FSM.

Same collision. Different observer. The operation is not in the engine. The operation is in the observation.

## Architecture (from whiteboard)

```
        INPUTS
        ↓↓↓↓↓↓
  ┌──────────────────┐
  │ ONETWO find opp. │ ← self-observes
  └──────┬───┬───────┘
         ↓   ↓
    ┌────┐   ┌────────┐
    │MATH│   │PHYSICS │   ← co-emergent
    │rule│   │  grid  │   ← can't have one without the other
    └──┬─┘   └─┬──────┘
       └───┬───┘
      ┌────┴────┐
      │ Observer │   ← AI = collision of math + physics
      │ accum+=v │   ← the ONLY operation
      └────┬────┘
           ↓
    emerged lattice    ← topology IS knowledge
           │
           └──→ feeds back to ONETWO (the loop)
```

Math = Distinction = the language (symbols, 0/1, true/false)
Physics = D(∅) → {2,3} = the structure (two things + substrate)
Observer = where they collide = where computation happens

## Wave Substrate (v0.14)

The GPU substrate evolved from a cellular automaton (mark/read/co-presence) to a 3D Yee FDTD grid. Same paradigm, real physics.

- **V** (scalar) at cell centers, **I** (vector, 3 components) on cell faces
- **L** (inductance) per voxel — the single learnable parameter
- Low L = wire (signal propagates fast). High L = vacuum (signal reflects).
- The impedance field IS the wiring diagram. Same concept as the old reads[] bitmask, different encoding.
- Hebbian: lower L where waves are active (strengthen), raise L where quiet (weaken).
- CFL stability: `alpha <= sqrt(L*C/3)` in 3D. Natural floor on how strong a wire can get (L >= 0.75).

Key files: `yee.cu`, `yee.cuh`, `tests/test_yee3d.cu`, `tests/test_yee3d_gpu.cu`

## What's Proven

- One operation (`accum += v`) + observers = universal computation (v5)
- Residue propagation: silence = understanding, surprise = learning (onetwo.c)
- Self-wiring: system discovers its own topology from examples (onetwo.c T15-T20)
- Nesting: crystallized nodes spawn child graphs (xyzt.c T26-T28)
- ONETWO feedback loop runs during tick (xyzt.c T24)
- Intershell: shell 0 and shell 1 confirm each other (xyzt.c T7)
- Agent memory: self-reinforcing nodes hold state without registers (tested Feb 26)
- Local topology mirrors global (independently discovered by Tesla patent US20260050706A1)
- 3D Yee wave substrate produces same computational behaviors as CA (v0.14, T3 PASS)

## The Loop (Closed)

ONETWO feedback drives topology changes. The loop is closed:

- `graph_error` = incoherence percentage (0-100), floor at 30 nodes
- Dual thresholds: fp_thresh=34 (fingerprint), ge_thresh=14% (graph_error)
- **Frustration** (error > threshold): erode worst incoherent crystal, accelerate growth
- **Boredom** (low error, sustained): harden coherent nodes, strengthen edges
- Conservation: `MAX_NODE_WEIGHT=1024`, competitive S3 steals from weakest edge
- Sense decays to zero — silence = understanding

Bus collision test: 15 raw packets, 3 groups, continuous re-injection — 2604 frustration ticks from pure bitstream collision. No English, no keywords. The engine reads its own immune response.

## Constants (imposed vs emerged — open question)

- `137` as SUBSTRATE_INT — imposed (α⁻¹ ≈ 137, but is this physical or convenient?)
- `81/2251` as mismatch tax — imposed
- Shell impedances `{1.0, 1.5, 2.25}` — imposed (K=3/2 ratio)
- Fresnel coefficients — emerged from impedance ratios (physics)
- Valence thresholds — emerged from boundary crossings
- ONETWO structural fingerprints — emerged from data
- Self-wired topology — emerged from residue minimization
