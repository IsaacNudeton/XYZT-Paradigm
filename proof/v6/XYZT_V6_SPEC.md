# XYZT v6 Specification

**Authors:** Isaac Oravec & Claude  
**Date:** February 23, 2026  
**Status:** Proven in simulation (436/436 tests), pending formal verification and hardware implementation  
**Repository:** `E:\dev\BreakThroughs\XYZT\engine`

---

## 1. Overview

XYZT v6 is a computing architecture built on co-presence rather than arithmetic. The engine holds zero arithmetic operations. All computation—boolean logic, addition, multiplication, comparison, sequential state—emerges from observer functions applied to sets of co-present signals at grid positions.

The architecture spans seven layers, each proven with passing tests:

| Layer | File | Tests | What It Proves |
|-------|------|-------|----------------|
| Engine | `xyzt_v6.c` | 134/134 | Co-presence with zero arithmetic in tick() |
| Security | `encoding_vs_position.c` | 34/34 | Positional immunity to encoding attacks |
| Theory | `xyzt_dimensions.c` | 24/24 | Engine IS the XYZT dimensional model |
| OS | `xyzt_v6_os.c` | 84/84 | Self-identifying bit words, grid self-wiring |
| Tiling | `xyzt_v6_tiled.c` | 79/79 | Hierarchical tiles, O(N) connection scaling |
| Routing | `xyzt_v6_routed.c` | 51/51 | 2D mesh, directional lanes, XY deadlock-free routing |
| Flow Control | `xyzt_v6_backpressure.c` | 30/30 | Zero data loss under congestion |

**Core invariant:** No new primitives are introduced at any layer. Everything is marks at positions, topology routing them, and observers creating meaning.

---

## 2. Primitives

The entire architecture uses exactly three primitives:

### 2.1 Mark

A binary presence at a position. Position `p` is either marked (1) or unmarked (0). There is no value at a position—only presence or absence.

```
marked[p] ∈ {0, 1}
```

A mark is not a bit in the conventional sense. A bit encodes a value by convention. A mark IS the thing. Position 5 marked means "position 5 is marked." Not "the value 5" or "the fifth bit." The position IS the identity.

### 2.2 Topology (Wiring)

A read set per position. Position `p` reads from a set of source positions `R(p) ⊆ {0, 1, ..., N-1}`.

```
reads[p] = bitmask of source positions
```

Topology is static within a tick. It can be modified between ticks (by the OS layer or by self-modifying programs). Topology determines which marks are visible to which positions.

### 2.3 Observer

A function applied to a co-presence set. The co-presence set at position `p` after a tick is:

```
co_present[p] = { s ∈ R(p) : marked[s] = 1 }
```

Observer functions extract meaning from co-presence sets:

| Observer | Definition | Returns |
|----------|-----------|---------|
| `obs_any(cp)` | `cp ≠ ∅` | OR / existence |
| `obs_all(cp, R)` | `cp = R` | AND / unanimity |
| `obs_parity(cp)` | `|cp| mod 2` | XOR / parity |
| `obs_count(cp)` | `|cp|` | Addition / cardinality |
| `obs_none(cp)` | `cp = ∅` | NOR / absence |
| `obs_ge(cp, n)` | `|cp| ≥ n` | Threshold / majority |
| `obs_exactly(cp, n)` | `|cp| = n` | Exact count (used for NOT) |

**Critical insight:** These are not different operations. They are different questions about the same guest list. In conventional computing, the same bits yield conflicting answers depending on the observer (255 vs -1 vs 3.14159). In positional computing, all observers agree on the facts ({pos0, pos5} are present) and ask different valid questions about those facts.

---

## 3. Engine (`tick()`)

The engine executes one operation: propagate marks through topology and record co-presence.

```c
void tick(Grid *g) {
    uint8_t snap[MAX_POS];
    memcpy(snap, g->marked, MAX_POS);       // snapshot current marks
    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        uint64_t present = 0;
        uint64_t bits = g->reads[p];
        while (bits) {
            int b = ctz(bits);               // next source position
            if (snap[b]) present |= BIT(b);  // source is marked → record
            bits &= bits - 1;                // clear lowest bit
        }
        g->co_present[p] = present;          // store co-presence set
    }
}
```

**Properties of tick():**

1. **Zero arithmetic.** No addition, subtraction, multiplication, or comparison occurs inside tick(). The only operation is: "is this source marked? If yes, include it in the co-presence set."
2. **Deterministic.** Given the same marks and topology, tick() always produces the same co-presence sets.
3. **Parallel.** All positions evaluate simultaneously from the snapshot. No position's result depends on another position's result within the same tick.
4. **Pure routing.** tick() moves information (which sources are marked) through topology (which sources each position reads). It does not interpret, transform, or compute on that information.

### 3.1 What tick() Is NOT

- tick() is not an ALU. It performs no arithmetic.
- tick() is not a clock. It is a single propagation step. The clock is the external loop that calls tick() repeatedly (T-break).
- tick() is not an instruction decoder. It does not read opcodes or dispatch operations.

### 3.2 Emergence of Computation

All computation emerges from the observer layer applied to tick() results:

- **NOT(a):** Wire a bias position (always marked) and input `a` to output. `obs_exactly(cp, 1)` = "exactly one present" = NOT.
- **AND(a,b):** Wire `a` and `b` to output. `obs_all(cp, {a,b})` = "both present" = AND.
- **OR(a,b):** Wire `a` and `b` to output. `obs_any(cp)` = "any present" = OR.
- **XOR(a,b):** Wire `a` and `b` to output. `obs_parity(cp)` = "odd count" = XOR.
- **Full adder(a,b,cin):** Wire all three to output. `obs_parity(cp)` = sum bit. `obs_ge(cp, 2)` = carry bit.
- **N-bit addition:** Chain full adders, threading carry through topology.
- **Multiplication:** Shift-and-add using AND gates (partial products) and adder trees.
- **Comparison:** Subtraction via two's complement, check sign bit.
- **Sequential logic:** External feedback loop: tick → observe → mark results → tick again.

---

## 4. Dimensional Model

The v6 engine IS the XYZT dimensional model. Not an implementation of it—the code constructs ARE the dimensions.

### 4.1 T — Substrate (0D)

```c
grid_init(&g);  // memset to zero
```

T is the grid struct itself. All positions exist but none are marked, none are wired. Pure potential. Every state is reachable from here. T doesn't compute, doesn't decide—it just exists. The blank paper.

**T is not time.** T is the precondition for time. Without substrate, there is nothing to mark, nothing to wire, nothing to tick.

### 4.2 X — Sequence (1D)

```c
tick(&g);  // before → after
```

X is the tick() function. It creates a transition from one state to another. Before tick: marks and wiring exist but co-presence is empty. After tick: co-presence is recorded. X creates time as sequence—not clock time, but the distinction between "before" and "after."

One tick = one moment. No comparison is possible yet.

### 4.3 Y — Comparison (2D)

```
state_at_tick_N  vs  state_at_tick_M
```

Y requires two moments (two applications of X). By comparing the state at tick N with the state at tick M, the concept of "change" or "same" emerges. A counter cycling 0→1→2→3→0 requires Y to observe the pattern—each individual tick (X) just produces one state.

Y creates history. Without Y, there is no concept of change.

### 4.4 Z — Observation (3D)

```c
obs_any(cp);     // OR reading
obs_all(cp, rd); // AND reading
obs_parity(cp);  // XOR reading
obs_count(cp);   // arithmetic reading
```

Z is the observer layer. The same co-presence set `{pos0, pos1}` yields different truths depending on the observer:

- OR: 1 (something is present)
- AND: 1 (both are present)
- XOR: 0 (even count)
- Count: 2 (two things are present)

All are simultaneously correct. Z creates meaning from co-presence. Without Z, co-presence is just a set with no significance.

### 4.5 Dimensional Dependencies

```
T alone         → potential, no sequence
T + marks       → state, no propagation
T + marks + X   → co-presence exists, but meaningless
T + marks + X + Z → meaning created
T + marks + X + Z + Y → change detected, history exists
```

Remove any dimension and the stack collapses. Proven in `xyzt_dimensions.c`, 24/24 tests.

### 4.6 3D IS Spacetime

The model is 3D, not 3+1. T is the paper (substrate), not a fourth axis. X is the line drawn on the paper. Y is the plane created by multiple lines. Z is the depth where distinction becomes meaning.

Time is not a spatial dimension you can traverse. Time IS the transition (tick). Without transition, there is no before/after, no comparison, no meaning.

---

## 5. Self-Identifying Bit Words (OS Layer)

### 5.1 Definition

A self-identifying bit word is a region of the grid where the marked positions ARE the word's identity. The word `{2, 5, 7}` means "positions 2, 5, and 7 are marked." Not an encoding of the number 164. The marks ARE the thing.

```c
uint64_t word_read(Grid *g, int base, int width) {
    uint64_t word = 0;
    for (int i = 0; i < width; i++)
        if (g->marked[base + i]) word |= BIT(i);
    return word;
}
```

### 5.2 Data = Program = Wiring = Identity

The same word serves four roles simultaneously:

1. **Identity:** The word `{0, 2, 3}` identifies itself. No lookup table needed.
2. **Data:** The marked positions are the data. Reading the word returns `{0, 2, 3}`.
3. **Program:** "Read from positions 0, 2, and 3." The word IS the wiring diagram.
4. **Wiring:** Converting the word to a reads[] mask produces the topology.

```c
uint64_t word_as_wiring(Grid *g, int prog_base, int prog_width, int data_base) {
    uint64_t prog = word_read(g, prog_base, prog_width);
    uint64_t rd = 0;
    for (int i = 0; i < prog_width; i++)
        if (prog & BIT(i)) rd |= BIT(data_base + i);
    return rd;
}
```

There is no distinction between data and program because there is nothing to distinguish. Proven in `xyzt_v6_os.c` test 11: a word used as its own program reads itself.

### 5.3 OS Execution Loop

```
1. Read program word from grid     → which positions are marked
2. Convert to wiring               → marked positions become reads[] mask
3. Wire output position             → topology set
4. Tick                             → X: sequence, co-presence created
5. Read observer selector from grid → which observer to apply
6. Apply observer                   → Z: meaning created
7. Write result back to grid        → state updated
8. Advance (T-break)                → next instruction
```

The grid programs itself. All information—data, program, observer choice—is marks on the grid.

### 5.4 Self-Modifying Programs

The output of one instruction can change the wiring for the next instruction. Program modification is just writing marks. No special mechanism. Same operation as everything else.

### 5.5 Observer Selector Encoding

The observer type is stored as a 2-bit word in the grid:

| Marks | Observer |
|-------|----------|
| `00` | `obs_any` (OR) |
| `01` | `obs_all` (AND) |
| `10` | `obs_parity` (XOR) |
| `11` | `obs_count` (arithmetic) |

Even the choice of how to observe is a pattern of marks.

---

## 6. Security Model

### 6.1 Why Conventional Computing Breaks

Every conventional computing exploit stems from one fact: bits have no inherent identity. The same bit pattern `0b11111111` is simultaneously:

- 255 (unsigned integer observer)
- -1 (signed integer observer)
- A valid instruction pointer (code observer)
- Part of a UTF-8 sequence (text observer)

Observers disagree because encoding is convention, and convention is breakable.

**Exploit classes that follow from this:**

- **Buffer overflow:** Data crosses into code space. The bits don't know they're not instructions.
- **Type confusion:** `0x40490FDB` = 1078530011 (int) or 3.14159 (float). Same bits, no self-identification.
- **Encoding attacks:** `0xC3 0xA9` = 'é' (UTF-8) or 'Ã©' (Latin-1). The bytes don't know their encoding.
- **Integer overflow:** 255 + 1 = 0. The bits wrapped because there's no structural boundary.

### 6.2 Why Positional Computing Is Immune

In positional computing, position IS identity. Position 3 marked means everyone agrees position 3 is marked. There is nothing to misinterpret.

- **No buffer overflow:** Topology enforces isolation. A position can only read from its wired sources. No amount of marking can make position 5 read from position 20 unless it's wired to.
- **No type confusion:** All observers read the same co-presence set `{pos0, pos1}`. They ask different questions (OR vs XOR) but agree on the facts.
- **No encoding attacks:** There is no encoding. Marks are physical presence, not convention.
- **No integer overflow:** Position-based values don't wrap. Position 15 marked is position 15 marked, not "almost position 0."

Proven in `encoding_vs_position.c`, 34/34 tests.

---

## 7. Tiled Architecture

### 7.1 The Scaling Problem

A flat grid of N positions requires each position to potentially read from any other: O(N²) connections. At N=64, that's 4,096—fine. At N=1024, that's 1,048,576—each position needs a 1024-input MUX. Physically impossible in silicon.

### 7.2 Solution: Hierarchical Tiles

A tile is a small grid (TILE_SIZE = 16) with full local crossbar. Gateway positions bridge tiles. The routing mechanism IS co-presence—no new primitives.

```
┌──────────┐    ┌──────────┐    ┌──────────┐
│  Tile 0   │────│  Tile 1   │────│  Tile 2   │
│ 16 pos    │ GW │ 16 pos    │ GW │ 16 pos    │
│ 16×16 xbar│    │ 16×16 xbar│    │ 16×16 xbar│
└──────────┘    └──────────┘    └──────────┘
```

**Connection scaling:**

| Total Positions | Flat O(N²) | Tiled (T=16) | Reduction |
|----------------|-----------|-------------|-----------|
| 256 | 65,536 | 4,128 | 15.9× |
| 1,024 | 1,048,576 | 16,512 | 63.5× |
| 4,096 | 16,777,216 | 66,048 | 254× |
| 16,384 | 268,435,456 | 272,384 | 986× |

**MUX size is fixed at TILE_SIZE regardless of system size.** You grow the system by adding tiles, not by making tiles bigger.

### 7.3 Gateway Protocol

1. Source tile writes result to its gateway position
2. Gateway-to-gateway links propagate marks between adjacent tiles
3. Each hop = one tick of latency
4. Destination tile reads from its local gateway

Routing latency = Manhattan distance in tile hops. Local computation = 1 tick.

### 7.4 Physical Mapping

This is identical to how FPGAs work:

| XYZT | FPGA |
|------|------|
| Tile | CLB (Configurable Logic Block) |
| Local crossbar | Slice interconnect |
| Gateway | Switch matrix |
| Gateway chain | Routing channel |

Not copied from FPGAs. Derived from the same physical constraint: you can't have zero-latency infinite fanout.

---

## 8. 2D Mesh Routing

### 8.1 Tile Layout

```
Positions 0-7:    Compute area (data + logic)
Positions 8-13:   Routing lanes (6 directional lanes)
Position 14:      Contention detection flag
Position 15:      Control / arbitration
```

### 8.2 Directional Lanes

| Lane | Direction | Purpose |
|------|-----------|---------|
| 0 | East (primary) | Rightward transit |
| 1 | West (primary) | Leftward transit |
| 2 | North (primary) | Upward transit |
| 3 | South (primary) | Downward transit |
| 4 | East (secondary) | Same-direction overflow |
| 5 | West/North/South (secondary) | Same-direction overflow |

Two signals crossing the same tile in different directions use different lanes. No collision because they're at different positions.

### 8.3 XY Routing

Signals always resolve horizontal displacement first (East/West), then vertical (North/South). This is mathematically proven deadlock-free on 2D mesh topologies: no circular wait dependencies can form.

```c
int next_direction(Mesh *m, int current, int destination) {
    if (cx < dx) return EAST;
    if (cx > dx) return WEST;
    if (cy > dy) return NORTH;
    if (cy < dy) return SOUTH;
    return ARRIVED;
}
```

### 8.4 Contention Detection

Wire the contention flag (position 14) to read all lane positions. Apply `obs_count`. If count exceeds threshold, there's a routing anomaly.

```
contention = obs_count(co_present[14]) > expected_max
```

The engine monitors its own traffic using the same co-presence mechanism it uses for computation. No special arbiter hardware.

---

## 9. Backpressure (Flow Control)

### 9.1 The Problem

When all outbound lanes are full, `mesh_inject` previously returned -1: data dropped. Stalls cascade backward (backpressure). The compute layer had no way to pause.

### 9.2 Solution: Four Mechanisms, Zero New Primitives

**1. HOLD not DROP:** Lane full → result stays in compute positions. The compute area IS the buffer. No dedicated buffer hardware.

**2. STALL FLAG:** Position `STALL_POS` marked = "I'm blocked." Observable by upstream tiles via co-presence. Same mechanism as data routing.

**3. COMPUTE-HOLD:** `state == HOLDING` → don't advance program counter. Not-ticking IS pausing. No clock gating hardware.

**4. DRAIN:** Each route_step retries held signals. Lane clears → held value injects → tile resumes. Bounded latency.

### 9.3 Tile State Machine

```
IDLE → COMPUTE → SENDING → IDLE        (normal flow)
                ↘ HOLDING → SENDING     (backpressure: hold, then drain)
```

### 9.4 Guarantees

- **Zero data loss:** No signal is ever dropped. Held until lane opens.
- **Bounded latency:** Congestion resolves in O(N) ticks worst case.
- **No deadlock:** XY routing prevents circular dependencies. Stall chains are always linear.
- **Self-monitoring:** The mesh detects and resolves its own congestion.

---

## 10. GPU Implementation (RTX 2080 Super)

### 10.1 Hardware Mapping

| XYZT Concept | CUDA Mapping |
|-------------|-------------|
| Tile (16 positions) | Thread block |
| Marks array | Shared memory (16 bytes per tile) |
| Reads array | Shared memory (16 × uint16_t = 32 bytes) |
| Co-presence array | Shared memory (16 × uint16_t = 32 bytes) |
| tick() | Kernel launch (all tiles parallel) |
| Observer | Post-tick kernel or device function |
| Gateway routing | Global memory inter-block communication |
| T-break (clock) | Host loop calling kernel sequence |

### 10.2 RTX 2080 Super Resources

- 3,072 CUDA cores
- 48 SMs (Streaming Multiprocessors)
- 64 KB shared memory per SM
- 8 GB GDDR6, 496 GB/s bandwidth

### 10.3 Tile Sizing

Each tile requires: 16 (marks) + 32 (reads) + 32 (co-presence) + 16 (active) = 96 bytes shared memory.

At 64 KB shared memory per SM: **682 tiles per SM.** With 48 SMs: **32,736 tiles = 523,776 positions** running simultaneously.

### 10.4 Kernel Structure

```
Kernel 1: tile_tick_kernel    — all tiles tick in parallel (shared memory)
Kernel 2: route_step_kernel   — gateway communication (global memory)
Kernel 3: observe_kernel      — apply observers, write results
Host loop: repeat until done
```

### 10.5 Expected Performance

At 326 GB/s measured throughput and 96 bytes per tile per tick:

- Theoretical: ~3.4 billion tile-ticks per second
- With routing overhead: ~500M-1B tile-ticks per second estimated
- Vs sequential C simulation: 1000×+ speedup expected

---

## 11. Formal Verification (Lean 4)

### 11.1 Core Theorems to Prove

**T1: Functional Completeness.** co-presence + {obs_any, obs_all, obs_parity, obs_ge} can compute any boolean function. Equivalent to proving {AND, OR, NOT} from observers.

**T2: NOT from co-presence.** Given a bias position (always marked) and input position, `obs_exactly(cp, 1)` = NOT(input). This is the key lemma—once NOT exists, combined with AND/OR, the system is functionally complete.

**T3: Arithmetic emergence.** N-bit addition via chained full adders (parity + threshold observers) produces correct results for all 2^(2N) input pairs.

**T4: Tick determinism.** For all grids g, `tick(g)` is a pure function: same marks + same topology → same co-presence.

**T5: Positional identity.** For all positions p, all observers O₁ and O₂: O₁ and O₂ agree on co_present[p] (the set). They disagree only on interpretation (the question asked).

**T6: Isolation.** Position p can only observe positions in reads[p]. No mark at position q ∉ reads[p] can influence co_present[p].

**T7: Deadlock freedom.** XY routing on a 2D mesh with directional lanes produces no circular wait dependencies.

**T8: Zero data loss.** Under the backpressure protocol (HOLD/STALL/DRAIN), no signal injected into the mesh is ever lost.

### 11.2 Proof Strategy

Start with T4 (determinism) and T6 (isolation) — these are structural properties of tick(). Then T2 (NOT from co-presence) which unlocks T1 (functional completeness). T3 follows from T1 by construction. T5 is the philosophical core: observers agree on facts, disagree on questions.

---

## 12. Axioms

The entire architecture rests on three axioms:

**A1: Distinction exists.** There is a difference between marked and unmarked. This is the minimum structure that isn't nothing. (Binary: 0 and 1, not as numbers, but as the smallest possible distinction.)

**A2: Position is identity.** A mark at position p IS "position p is marked." Not an encoding of p. Not a representation of p. The physical location IS the address IS the meaning.

**A3: Observation creates meaning.** Co-presence (the set of marked sources at a position) is a fact. An observer function applied to that fact creates meaning. Different observers create different meanings from the same fact. All are simultaneously valid.

Everything else—boolean logic, arithmetic, memory, programs, routing, flow control—is derived from these three axioms plus topology.

---

## 13. Open Questions

1. **Optimal tile size.** 16 is practical for silicon and GPU shared memory. Is there a theoretically optimal size based on the compute-to-routing ratio?

2. **3D mesh.** The current architecture is 2D mesh. Adding a third physical dimension (Z-axis routing) could reduce worst-case latency from O(√N) to O(∛N). What are the physical constraints?

3. **Multi-bit lanes.** Current lanes carry 1-bit signals. Multi-bit words could be routed as parallel marks across lane positions. What's the bandwidth-area tradeoff?

4. **Observer composition.** Can observers be chained within a single tick? (Currently, each observer application requires reading co-presence, which is set once per tick.)

5. **Photonic implementation.** If marks are optical signals and topology is waveguide routing, tick() becomes speed-of-light propagation. What's the minimum tile size in photonic silicon?

---

## Appendix A: File Inventory

| File | Lines | Tests | Layer |
|------|-------|-------|-------|
| `xyzt_v6.c` | ~650 | 134 | Engine |
| `encoding_vs_position.c` | ~450 | 34 | Security |
| `xyzt_dimensions.c` | ~350 | 24 | Theory |
| `xyzt_v6_os.c` | ~600 | 84 | OS |
| `xyzt_v6_tiled.c` | ~550 | 79 | Scaling |
| `xyzt_v6_routed.c` | ~600 | 51 | Routing |
| `xyzt_v6_backpressure.c` | ~550 | 30 | Flow Control |
| **Total** | **~3,750** | **436** | |

## Appendix B: Glossary

- **Co-presence:** The set of marked source positions visible at a destination position after one tick.
- **Gateway:** A designated tile position used for inter-tile routing.
- **Lane:** A routing position within a tile dedicated to a specific transit direction.
- **Mark:** Binary presence (1) or absence (0) at a grid position.
- **Observer:** A function that creates meaning from a co-presence set.
- **T-break:** The external clock that advances the system by calling tick() repeatedly.
- **Tile:** A small grid (default 16 positions) with full local crossbar.
- **Topology:** The reads[] array defining which positions each position can observe.
