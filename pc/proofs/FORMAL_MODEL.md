# XYZT Formal Model — Φ Extracted from Source

**Source files:** `pc/engine.c`, `pc/engine.h`, `pc/tline.c`, `pc/tline.h`
**Extracted:** March 2026 by Claude (Intershell), verified against code line-by-line.

---

## 1. State Space

The system state at tick t is a tuple:

```
Σ(t) = ( S(t), W(t), Θ(t) )
```

where:

- **S(t) ∈ ℤ^|V|** — node values. `S_i(t) = nodes[i].val ∈ [-10^6, 10^6]`
- **W(t) ∈ ℝ^(|E| × n_cells)** — edge transmission line states. Each edge e has a vector `V_e[0..n_cells-1]` and per-cell inductance `Lc_e[0..n_cells-1]`
- **Θ(t) ∈ {0,1}^|V| × [0,255]^|V| × ...** — node metadata: `alive`, `valence`, `identity` (BitStream), `plasticity`, `I_energy`, `coherent`, `coord`

The graph topology G = (V, E) where:
- V = set of nodes, |V| ≤ 4096
- E = set of directed hyperedges, |E| ≤ 65536
- Each edge e = (src_a, src_b, dst, tline, invert_a, invert_b)
- Edge is a 2-source → 1-dest gate with a transmission line delay

**Critical structural point:** edges carry TWO sources and ONE destination. The edge IS a collision site. This is not standard graph structure — it's a hypergraph where every edge is a Y-junction.

---

## 2. The Tick Map: Φ

One tick consists of stages S1–S10 applied in sequence:

```
Σ(t+1) = S10 ∘ S9 ∘ S8 ∘ S7 ∘ S6 ∘ S5 ∘ S4 ∘ S3 ∘ S2 ∘ S1 ( Σ(t) )
```

Not all stages fire every tick. Stages S5, S6, S8, S9, S10 fire at intervals.

### Stage S3: Propagation (fires every tick)

For each Z-level zl = 0, 1, ..., z_max:

**S3a. Edge propagation** — for each edge e with dst at z-level zl:

```
input_e = (v_a + v_b) / VAL_CEILING

where:
    v_a = invert_a ? -S_{src_a} : S_{src_a}
    v_b = invert_b ? -S_{src_b} : S_{src_b}
```

Then inject into TLine and step:

```
tline.V[0] ← input_e                          (boundary condition)

for i = n_cells-1 down to 1:
    atten_i = clamp(1 - (R + G · Lc[i]), 0, 1)
    tline.V[i] = α · (tline.V[i-1] · atten_i) + (1-α) · tline.V[i]

output_e = tline.V[n_cells - 1]
```

where α = 0.5 (TLINE_ALPHA), R = base resistive loss, G = inductance-coupled loss.

**Accumulate at destination:**

```
accum_{dst} += output_e · VAL_CEILING
I_energy_{dst} += |output_e| · VAL_CEILING
n_incoming_{dst} += 1
```

**Correlation tracking:**

```
if S_{src_a} ≠ 0 and S_{src_b} ≠ 0:
    sign_product = (sign(S_{src_a}) == sign(S_{src_b})) ? +1 : -1
    e.correlation = 0.95 · e.correlation + 0.05 · sign_product
```

### Stage S4: Resolve (fires every tick, per Z-level after S3)

For each node i at z-level zl with n_incoming > 0 or accum ≠ 0:

```
old_val = S_i

if n_incoming ≤ 1:
    S_i = accum_i
else:
    total = accum_i + old_val
    N = n_incoming + 1
    S_i = clamp( 2·total/N - old_val, -INT32_MAX/2, INT32_MAX/2 )

# Valence inertia (crystallization):
if valence_i > 0:
    S_i = (old_val · valence_i + S_i · (255 - valence_i)) / 255

# Energy conservation cap:
cap = |old_val| + |accum_i|
if |S_i| > cap: S_i = sign(S_i) · cap

# Hard ceiling:
S_i = clamp(S_i, -10^6, +10^6)
```

**Collision detection (valence growth):**

```
if n_incoming ≥ 2 and not contradicted:
    valence_i += 1
    for each edge e with dst = i:
        valence_{src_a(e)} += 3    # upstream reward
```

**I_energy residual (destructive interference detection):**

```
if n_incoming ≥ 2:
    I_energy_i = max(0, I_energy_i - |S_i|)
else:
    I_energy_i = 0
```

The XOR observer reads: `I_energy > 0 and |S_i| · 4 ≤ I_energy` → destructive collision detected.

### Stage S5: Grow (fires every grow_interval ticks)

For each pair (i, j) of alive nodes with sufficient identity:

```
corr = contain(identity_i, identity_j)      # one-directional containment
rev  = contain(identity_j, identity_i)

# Directional gate: only wire i→j if contain(i,j) > contain(j,i) + 3
if rev > corr + Z_ASYM_MARGIN: skip

# Opportunity scoring: prefer creating collision points
opp = 3 if n_in[j] ≤ 1 else (2 if n_in[j] ≤ 3 else 1)
eff_corr = corr · opp / 3

if eff_corr > local_threshold:
    create edge (i, i, j) with weight = corr·255/100    [feed-forward]
    OR edge (i, j, i) with weight = corr·255/100         [listen]
```

### Stage S6: Prune (fires every prune_interval ticks)

```
remove all edges e where weight(e) < prune_threshold
```

### Stage S7: T-driven Decay (fires every tick)

```
for each node i:
    age = T_now - last_active_i
    if age > 3 · SUBSTRATE_INT:
        valence_i -= 1
        for each outgoing edge e from i:
            Lc_e[c] *= 1.01 · plasticity_i    (weaken)
            weight_e = product_attenuation(tline_e)
```

### Stage S9: Hebbian Learning (fires every learn_interval ticks)

For each edge e:

```
corr = contain(identity_{src_a}, identity_{src_b})

# Target inductance: high correlation → low Lc (strong), low → high Lc (weak)
target_Lc = L_base · (200 - corr) / 100

# Learning rate with plasticity and structural match gating
rate = (81/2251) · plasticity_{src_a} · plasticity_{dst} · match_gate

# Update each cell
for c = 0 to n_cells-1:
    error = target_Lc - Lc_e[c]
    Lc_e[c] += error · rate

weight_e = product_attenuation(tline_e)
```

**Key:** The Hebbian rule targets inductance, not weight directly. Weight is a DERIVED quantity (product of per-cell attenuations). The fundamental learnable parameters are the Lc[c] values — the per-cell inductance profile of each transmission line.

### Stage S10: ONETWO Observation (fires every SUBSTRATE_INT ticks)

Samples full engine state into a byte buffer, runs through the ONETWO system which produces feedback[0..7]. feedback[0] gates the Hebbian learning rate in S9 (structural_match).

---

## 3. Key Mathematical Properties to Prove

### 3.1 The TLine as a Linear Operator (per step)

For fixed Lc, R, G, the TLine step is:

```
V_i(t+1) = α · a_i · V_{i-1}(t) + (1-α) · V_i(t)     for i ≥ 1
V_0(t+1) = input(t)                                       (driven)
         or a_0 · V_0(t)                                   (floating)
```

where a_i = clamp(1 - R - G·Lc_i, 0, 1).

This is a LINEAR map on V for fixed Lc. The TLine is a linear time-invariant shift register with per-cell gain < 1.

**Consequence:** Signal propagation through a single edge is a contraction mapping when all a_i < 1.

### 3.2 The Resolve Step as Weighted Average

The resolve formula for n_incoming > 1:

```
S_new = 2·(accum + S_old)/(n_incoming + 1) - S_old
```

Rearranging:

```
S_new = (2·accum - (n_incoming - 1)·S_old) / (n_incoming + 1)
```

With valence inertia:

```
S_final = (valence · S_old + (255 - valence) · S_new) / 255
```

This is a convex combination of old and new. As valence → 255, the node becomes frozen (S_final → S_old). Crystallization = increasing valence = increasing inertia = decreasing sensitivity to new input.

### 3.3 The Hebbian Rule as Gradient Descent on Impedance

```
Lc += rate · (target_Lc - Lc)
```

This is exponential convergence to target_Lc with time constant 1/rate. The target is:

```
target_Lc = L_base · (2 - corr/100)
```

So corr = 100% → target = L_base (minimum loss, strongest coupling)
   corr = 0%   → target = 2·L_base (maximum loss, weakest coupling)

This IS gradient descent on ||Lc - target||² with learning rate `rate`.

### 3.4 Topology Self-Modification

The graph changes through:
- **Grow (S5):** adds edges where identity containment exceeds threshold
- **Prune (S6):** removes edges where weight drops below threshold
- **Hebbian (S9):** modifies edge Lc profiles based on identity correlation
- **Decay (S7):** weakens edges from inactive nodes

The grow/prune cycle is an attractor on the space of topologies:
edges are added when nodes correlate and removed when coupling weakens.

The fixed point of grow + prune + Hebbian is a topology where:
- Every edge connects nodes whose identities are sufficiently correlated
- Every edge's Lc profile matches the correlation (weight reflects structural similarity)
- No edge is weak enough to prune
- No unconnected pair is correlated enough to grow

**This IS a fixed point equation on (S, W, G).**

---

## 4. The Composition: Φ as Self-Modifying Dynamical System

```
Φ: (S, W, Θ, G) → (S', W', Θ', G')

where:
    S' = resolve(propagate(S, W, G))            [S3 + S4]
    W' = hebbian(W, Θ, feedback)                [S9]
    Θ' = crystal(Θ, S') + decay(Θ, T)          [valence, plasticity, coherence]
    G' = grow(G, Θ) ∪ prune(G, W')             [S5 + S6]
```

**The state and the topology co-evolve.** This distinguishes XYZT from:
- Turing machines (topology = tape, fixed)
- Neural networks (topology fixed during inference; separate training phase)
- Cellular automata (topology = grid, fixed)
- Reservoir computing (reservoir fixed, only readout trained)

XYZT modifies its own connectivity as a function of what it processes, every cycle. The program and the data and the hardware are the same object.

---

## 5. What Needs Proving (Theorem Targets)

**Theorem 1 (Turing Completeness):** ∃ an XYZT configuration (G₀, Σ₀) that simulates a universal Turing machine.

Proof sketch: encode tape as node values, transition table as edge topology with inversion flags, head position as the active wavefront.

**Theorem 2 (Topological Isolation):** If patterns P₁ and P₂ have disjoint wave support in G (no shared edges), then learning P₂ does not modify any Lc on edges in the support of P₁.

Proof: follows directly from the Hebbian rule — Lc_e only updates if src_a and src_b identity correlation changes, and identity is set at ingest time, not modified by wave propagation.

**Theorem 3 (Contraction / Noise Robustness):** For a crystallized region (all nodes with valence > v_thresh), the resolve map is a contraction with rate (255 - valence)/255.

Proof: the valence inertia formula is S_final = λ·S_old + (1-λ)·S_new where λ = valence/255. Perturbation δ in accum produces δ' in S_final where |δ'| ≤ (1-λ)|δ|. For valence = 200, this is 0.22x contraction per tick.

**Theorem 4 (Symmetry Breaking / Self-Identification):** Starting from identical node values S_i(0) = S_j(0) for all i,j, if G has any topological asymmetry, then ∃T such that d(S_i(T), S_j(T)) > 0 for some i ≠ j.

Proof sketch: different nodes occupy different positions in the graph, therefore receive different superpositions of incoming signals, therefore resolve to different values. The Hebbian rule amplifies these differences by modifying Lc differently for different edges. This is a symmetry-breaking cascade analogous to spontaneous magnetization.

Key: a Von Neumann machine with identical initial state and deterministic program cannot break symmetry without external randomness. XYZT breaks symmetry through topology-dependent wave superposition.

---

## 6. Constants (from code)

| Constant | Value | Source |
|---|---|---|
| SUBSTRATE_INT | 155 | N-sweep empirical + theoretical derivation |
| TLINE_ALPHA | 0.5 | Smoothing parameter |
| R | 0.15 | Base resistive loss (default) |
| G | 0.02 | Inductance-coupled loss (default) |
| n_cells | 8 | Default edge length |
| VAL_CEILING | 1,000,000 | Node value hard cap |
| MISMATCH_TAX | 81/2251 ≈ 0.036 | Hebbian learning rate base |
| PLASTICITY range | [0.5, 2.0] | Per-node learning rate multiplier |
| Z_ASYM_MARGIN | 3 | Directional gate for edge creation |
| GROW_K | 6 | Max new edges per node per grow cycle |
| PRUNE_FLOOR | 9 | Weight below which edges are pruned |

---

*This document is the mathematical reference for all Lean4 proofs targeting XYZT computational class.*
