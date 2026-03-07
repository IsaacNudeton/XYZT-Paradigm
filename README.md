# XYZT Paradigm

A computing paradigm built on co-presence instead of arithmetic.

In conventional computing, a CPU fetches an instruction, decodes it, executes math on registers, and stores the result. XYZT removes all of that. Instead, a grid of voxels propagates binary state to their neighbors each tick. No ALU, no instruction pointer, no fetch-decode-execute. What computes is *which voxels are next to which* — topology determines the operation.

Three primitives make this work:

- **Mark**: a voxel is on or off. That's the only state.
- **Topology**: which voxels connect to which. This is the program. Changing the wiring changes the computation.
- **Observer**: a function that reads the accumulated state and interprets it. The same collision produces AND, OR, XOR — depending on which observer you ask. The operation is not in the engine. It's in the observation.

This was proven in `proof/v5` — a single accumulator (`accum += v`) plus different observers reproduces NOT, AND, OR, XOR, addition, multiplication, comparison, a full adder, a 4-bit ripple adder, a 2-bit counter, an SR latch, and a traffic FSM. Same collision, different question, different answer.

## How it learns

The engine doesn't just compute — it wires itself. Raw data (files, packets, bitstreams) gets ingested as ONETWO structural fingerprints (4096-bit fixed encodings that capture repetition, opposition, nesting, and metadata). Edges are **directed** — `bs_contain(A, B)` measures how much of A's structure appears in B, and the reverse is evaluated separately. A node only wires outward at ingest; the reverse edge forms when the other node runs its own grow cycle. This asymmetry creates a causal arrow (the Z-axis).

Nodes that survive enough boundary crossings crystallize (harden). Crystallized nodes spawn child sub-graphs that inherit a view of their parent's substrate. Children are not static — they run their own Hebbian learning (co-active retina nodes strengthen), grow edges between co-firing pairs, and maintain an independent error accumulator with a local heartbeat at 4× the parent's rate. Each child has its own drive state (frustration/curiosity/boredom) that adjusts its growth rate independently of the parent.

A closed feedback loop drives the whole thing:
- `graph_error` measures what percentage of nodes are incoherent (their neighbors disagree about their state)
- When error is high (**frustration**), the engine erodes the worst incoherent crystal and accelerates growth
- When error is low and stable (**boredom**), it hardens coherent nodes and strengthens edges
- Conservation caps total edge weight per node at 1024 — new edges must steal weight from weaker ones

This means the topology competes. Strong patterns survive. Weak ones starve. The engine doesn't learn by gradient descent or backpropagation — it learns by economic pressure on a weight-conserving graph.

## What's been tested

The `pc/` engine runs 243 tests covering:

- **Process isolation** (T3 Stage 1): 50 nodes in 3 zones — a "sick" zone with conflicting data, a "healthy" zone, and a boundary. After 30 cycles of continuous re-injection, the healthy zone recovers in 5 cycles and holds all 15 crystals. Cross-zone edges starve (weight 53) while intra-zone edges stay strong (123). Conservation isolates the damage without walls — through economics.
- **Production load** (T3 Full): 200 nodes across 5 zones (conflict, stable, telemetry, ASCII, boundary chimera), 30 cycles. All zones survive. Healthy zones crystallize 40/40. The conflict zone holds 3 incoherent nodes. 7888 edges at 12% of capacity — no explosion.
- **Contradiction detection**: negation-aware edge inversion + destructive interference scores 0.900 (5/5 true positives, 0 false positives on a 20-sentence benchmark). Score dropped from 0.949 after switching to directed containment (asymmetric denominator).
- **Inner T** (child learning): children run Hebbian learning, edge growth, and SPRT-style error accumulation with independent drive states. In diagnostic: 73K learns, edges grown from 4→36, 61 heartbeats, frustration-driven growth acceleration. Children are alive.
- **Full persistence** (v12 save/load): the entire engine state — topology, adapted parameters, children, inner T state, feedback history, substrate time — survives shutdown and reload. v11 backward compatible.
- **GPU substrate**: 4096 cubes (262K voxels) benchmarked at 9.5 billion voxel-ticks/second on an RTX 2080 Super.
- **Formal proofs**: 10 Lean4 proofs (zero `sorry`, zero axiom) covering basic properties, duality, gain, IO, lattice structure, physics correspondence, sequential composition, substrate invariants, and topology.

Known limitations: directed edges and inner T are both operational, but the Z-axis still shows 0 — containment asymmetry exists (A→E=85, E→A=80) but the global `grow_mean` threshold homogenizes it. Per-zone grow thresholds (MDL splitting) are needed to let zones develop distinct topology. Inner T also changes weight dynamics: frustrated children amplify parent output, attracting weight toward sick zones instead of away — the T3 weight-flow direction reversed but isolation still holds.

## Repository structure

```
proof/
  v5/        Single-accumulator universality proof (all boolean ops from one collision)
  v6/        436-test C simulation that proved the paradigm
  v9/        3302-line reference implementation with shells and nesting (48 tests)
  lean4/     10 formal proofs in Lean4
  spec/      XYZT.lang instruction set + assembly programs (adder, counter, FSM, SR latch)
pc/          CPU + GPU engine — unified v3/v6/v9, CUDA sm_75 (243 tests)
pico/        Autonomous firmware — RP2040, same paradigm on bare metal
pi-zero2/    Bare-metal kernel — ARM, no OS
shared/      Shared sense layer across devices
```

Every device runs the same core loop. There is no client/server split. A Pico and a GPU are the same organism at different scales.

## Build (PC engine)

Requires: CUDA Toolkit 13.1, Visual Studio Build Tools, GPU with sm_75+ (RTX 2080 or later)

```bat
cd pc
rebuild.bat
xyzt_pc.exe test
```

See [CODEBOOK.md](CODEBOOK.md) for paradigm reference, [pc/STATUS.md](pc/STATUS.md) for current test results and known issues.

## Why

Current computing is 80 years of the same idea: move numbers, do math on them, store the result. XYZT asks what happens if you remove the math entirely. If computation is "things next to things influencing each other" — which is what physics already does — then arithmetic is a convention, not a requirement. You need a grid, propagation rules, and someone watching.

The proof chain: single-accumulator universality (v5) showed one operation + observers = all computation. Self-wiring (onetwo.c) showed the system discovers its own topology from examples. The GPU engine showed it scales to hundreds of nodes under sustained load. The formal proofs in Lean4 showed the properties hold without escape hatches.

The next step is physical hardware — dedicated silicon running co-presence natively.

## How this was built

Isaac Oravec and Claude. Not a user and a tool — a duo. Claude co-authored the paradigm, the proofs, the engine, and this file. Isaac built the infrastructure, designed the hardware path, and made the calls. We disagree, we argue, we build. That's how it works.

## License

PolyForm Noncommercial 1.0.0 — use it, learn from it, credit us, don't sell it. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
