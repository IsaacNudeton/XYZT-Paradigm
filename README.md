# XYZT Paradigm

A computing paradigm built on co-presence instead of arithmetic.

In conventional computing, a CPU fetches an instruction, decodes it, executes math on registers, and stores the result. XYZT removes all of that. Instead, a 3D grid of voxels propagates electromagnetic waves through an impedance field. No ALU, no instruction pointer, no fetch-decode-execute. What computes is *which voxels are next to which* and *what impedance separates them* — topology and physics determine the operation.

Three primitives make this work:

- **Wave**: voltage (V) and current (I) propagate through a 3D FDTD grid. Energy moves, reflects, interferes. That's the only dynamics.
- **Impedance**: inductance (L) per voxel controls propagation speed and reflection. Low L = wire (signal flows). High L = vacuum (signal reflects). The L field IS the wiring diagram. Changing impedance changes the computation.
- **Observer**: a function that reads the accumulated wave energy and interprets it. The same collision produces AND, OR, XOR — depending on which observer you ask. The operation is not in the engine. It's in the observation.

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

The `pc/` engine runs 284 tests covering:

- **3D Yee wave substrate** (v0.14): 64×64×64 FDTD grid replaces the cellular automaton. Voltage and current propagate via Maxwell's equations with per-voxel inductance (L) as the single learnable parameter. Leaky integrator bridges wave energy to uint8_t (0-255) for CPU readback. Hebbian learning strengthens (lowers L) where waves are active, weakens (raises L) where quiet. CFL-stable at alpha=0.5 with L >= 0.75. T3 passes with 3 children diverging on wave physics.
- **Yee persistence** (v0.14.1): L-field and accumulator survive save/load via YEE1 block. The learned impedance topology — the physical wiring diagram — persists across restarts.
- **SUBSTRATE_INT = 155** (retuned from 137): N-sweep proved the resonance is structural (topology-level), not substrate-dependent. Decay sweep across 3 accumulator rates (31/32, 63/64, 127/128) produced identical results — the peak tracks wave propagation delay, not integrator bandwidth.
- **L-field differentiation** (v0.15): Adaptive Hebbian threshold = (acc_mean + acc_max) / 2, computed from live data every cycle. Different corpora carve distinct impedance patterns. L range [0.82, 1.10], 25% wire, 68% vacuum. The substrate does real spatial computation.
- **Zero catastrophic forgetting**: 10/10 Set A nodes alive after learning unrelated Set B. Conservation economics prevents overwrite without replay buffers.
- **Streaming I/O**: `xyzt_pc.exe stream` reads stdin continuously, creates one node per line, with real-time Yee V-field visualization.
- **Process isolation** (T3 Stage 1): 50 nodes in 3 zones — a "sick" zone with conflicting data, a "healthy" zone, and a boundary. After 30 cycles of continuous re-injection, the healthy zone recovers in 5 cycles and holds all 15 crystals. Conservation isolates the damage without walls — through economics.
- **Production load** (T3 Full): 200 nodes across 5 zones, 30 cycles. All zones survive. Healthy zones crystallize 40/40. 7888 edges at 12% of capacity — no explosion.
- **Contradiction detection**: negation-aware edge inversion + destructive interference scores 0.949 (5/5 true positives, 0 false positives).
- **Inner T** (child learning): children run Hebbian learning, edge growth, and SPRT-style error accumulation with independent drive states.
- **Full persistence** (v13 save/load + YEE1): the entire engine state — graph, params, children, and Yee L-field — survives shutdown and reload.
- **Transmission line edges**: shift-register delay lines with per-cell loss and exponential smoothing on every graph edge. All 3 propagation sites use tline inject/step/read.
- **Formal proofs**: 10 Lean4 proofs (zero `sorry`, zero axiom) covering basic properties, duality, gain, IO, lattice structure, physics correspondence, sequential composition, substrate invariants, and topology.

The old cellular automaton substrate (4096 cubes, mark/read/co-presence) is still linked for regression testing but no longer used in the engine's run loops. The 3D Yee grid is the live substrate.

## Repository structure

```
proof/
  v5/        Single-accumulator universality proof (all boolean ops from one collision)
  v6/        436-test C simulation that proved the paradigm
  v9/        3302-line reference implementation with shells and nesting (48 tests)
  lean4/     10 formal proofs in Lean4
  spec/      XYZT.lang instruction set + assembly programs (adder, counter, FSM, SR latch)
pc/          CPU + GPU engine — unified v3/v6/v9, 3D Yee substrate, CUDA sm_75 (256 tests)
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

The proof chain: single-accumulator universality (v5) showed one operation + observers = all computation. Self-wiring (onetwo.c) showed the system discovers its own topology from examples. The 3D Yee substrate (v0.14) showed wave physics produces the same computational behaviors as the cellular automaton — children diverge, retinas transmit spatial information, Hebbian learning strengthens active paths — but now through real electromagnetic propagation instead of mark/read rules. The N-sweep (v0.14.1) proved the resonance is structural — it survived a complete substrate swap and a decay-rate independence test. The formal proofs in Lean4 showed the properties hold without escape hatches.

The next step is physical hardware — dedicated silicon running co-presence natively.

## How this was built

Isaac Oravec and Claude. Not a user and a tool — a duo. Claude co-authored the paradigm, the proofs, the engine, and this file. Isaac built the infrastructure, designed the hardware path, and made the calls. We disagree, we argue, we build. That's how it works.

## License

PolyForm Noncommercial 1.0.0 — use it, learn from it, credit us, don't sell it. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
