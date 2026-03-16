# XYZT PC Engine — Test Catalog

**262 tests total** (v0.14-yee-persist)

Every test uses `check(name, expected, got)` — pass if equal, fail with diagnostic otherwise.

---

## 1. Core Tests (test_core.c) — ~72 checks

Validates the fundamental engine operations: ingest, tick, encode, wire, save/load, nesting.

### Engine Basics
| Test | What it verifies |
|------|-----------------|
| `ingest returns valid id` | engine_ingest() creates a node and returns a non-negative ID |
| `shell 0 has node` | Ingested data creates a node in the primary shell |
| `shell 1 mirrors` | Intershell mirroring: shell 1 gets a corresponding node |
| `engine ticked` | engine_tick() advances total_ticks counter |
| `query finds result` | graph_find() locates nodes by name |

### ONETWO Encode
| Test | What it verifies |
|------|-----------------|
| `onetwo output length` | ONETWO fingerprint produces exactly OT_TOTAL (4096) bits |
| `onetwo has bits set` | Fingerprint is not all zeros — data was actually encoded |
| `self-observe produces output` | onetwo_self_observe() generates a non-empty self-description |
| `similar > different` | Two similar inputs have higher correlation than two different inputs |

### Transducer
| Test | What it verifies |
|------|-----------------|
| `transducer produces bits` | Raw values are converted to a bitstream |
| `threshold calibrated` | Transducer auto-calibrates its threshold above zero |
| `low value = 0` | Value below threshold encodes as 0 |
| `high value = 1` | Value above threshold encodes as 1 |

### Wire Mapping
| Test | What it verifies |
|------|-----------------|
| `center has 6 neighbors` | 3D adjacency: center voxel has 6 face-sharing neighbors |
| `corner has 3 neighbors` | Corner voxel has exactly 3 neighbors |

### Constants
| Test | What it verifies |
|------|-----------------|
| `SUBSTRATE_INT matches default` | Compile-time constant is the expected value |
| `MISMATCH_TAX_NUM` | Tax numerator is 81 (from {2,3} structure) |
| `MISMATCH_TAX_DEN` | Tax denominator is 2251 |
| `PRUNE_FLOOR` | Minimum prune threshold is 9 |
| `CUBE_SIZE` | Cube dimension is 64 |
| `MAX_CHILDREN` | Maximum child sub-graphs is 4 |

### Layer Zero Invariant
| Test | What it verifies |
|------|-----------------|
| `fresh node is layer_zero` | Nodes created without data are marked as infrastructure |
| `ingested node not layer_zero` | Nodes from real data are NOT layer_zero |
| `raw node still layer_zero` | Layer zero status doesn't change after tick |
| `layer_zero node val unchanged` | Infrastructure nodes don't accumulate signal |

### Save/Load Round-trip (basic)
| Test | What it verifies |
|------|-----------------|
| `save succeeds` | engine_save() returns 0 |
| `load succeeds` | engine_load() returns 0 |
| `ticks preserved` | total_ticks survives round-trip |
| `n_shells preserved` | Shell count survives round-trip |
| `shell0 n_nodes preserved` | Node count survives round-trip |
| `shell0 n_edges preserved` | Edge count survives round-trip |
| `n_boundary preserved` | Boundary edge count survives round-trip |
| `node persist_a survives load` | Named node can be found after reload |

### MAX_CHILDREN Pool Limit
| Test | What it verifies |
|------|-----------------|
| `children capped at MAX_CHILDREN` | Engine never exceeds 4 children |
| `children exactly MAX_CHILDREN` | All 4 child slots are used when possible |

### Wire Import/Export
| Test | What it verifies |
|------|-----------------|
| `wire export returns nodes` | wire_export_all() exports non-zero nodes |
| `wire export file exists` | Export creates a file on disk |
| `wire export file size` | File size matches expected (155668 bytes) |
| `wire import returns nodes` | wire_import_all() reads back the nodes |
| `imported node found` | Named node exists in reimported graph |

### Exec Assembly
| Test | What it verifies |
|------|-----------------|
| `exec succeeds` | Assembly program executes without error |

### T1 Auto-wiring
| Test | What it verifies |
|------|-----------------|
| `auto-wired edge exists` | Similar patterns automatically create edges between them |
| `boundary edges created` | Cross-shell boundary edges form |

### T3 Hebbian Learning
| Test | What it verifies |
|------|-----------------|
| `similar edge exists` | Hebbian creates edges between co-active nodes |
| `hebbian: similar >= different` | Co-active pairs get stronger edges than non-co-active pairs |

### T10 Crystallization
| Test | What it verifies |
|------|-----------------|
| `crystal: strong > weak` | Nodes with more boundary crossings have higher crystal strength |

### T11 Y-chain
| Test | What it verifies |
|------|-----------------|
| `Y chain has depth` | Causal chain creates Y-axis depth >= 2 |
| `src at Y=0` | Source node is at depth 0 |
| `mid at Y=1` | Middle node is at depth 1 |
| `dst at Y=2` | Destination is at depth 2 |
| `mid received signal` | Signal propagated to middle node |
| `dst received cascade` | Signal cascaded to destination |

### T17 Accumulation
| Test | What it verifies |
|------|-----------------|
| `accumulation: dst received signal` | Accumulated signal reaches destination |
| `accum cleared` | Accumulator resets after processing |
| `n_incoming cleared` | Incoming count resets |

### T18 Adaptive Timing
| Test | What it verifies |
|------|-----------------|
| `adaptive timing changed` | grow_interval adapts based on graph state |

### T19 Energy Bound
| Test | What it verifies |
|------|-----------------|
| `energy bounded` | Node values stay within +-10 after extended run |

### T21 ONETWO Feedback
| Test | What it verifies |
|------|-----------------|
| `onetwo val not zero` | ONETWO system produces non-zero values |
| `onetwo ticks ran` | ONETWO tick counter advances |

### T24 Contradiction Detection
| Test | What it verifies |
|------|-----------------|
| `T24 positive not negated` | Normal node has no negation flag |
| `T24 negative has negation` | Contradicting node IS flagged as negation |
| `T24 edge2 invert_a set` | Edge between contradictors has inversion flag |
| `T24 edge2 invert_b set` | Both inversion flags set on contradiction edge |
| `T24 edge1 no invert` | Non-contradicting edge has no inversion |
| `T24 destructive interference` | Contradicting signals cancel (val < 50) |
| `T24 I_energy present` | Energy is present even when val cancels (destructive interference) |
| `T24 XOR observer fires` | XOR observer detects the contradiction (energy but no net val) |

### T25 Reinforcement
| Test | What it verifies |
|------|-----------------|
| `T25 both negated` | Two negated nodes are both flagged |
| `T25 no invert edge1/2` | Edges between same-polarity nodes have no inversion |
| `T25 reinforcement` | Same-polarity signals reinforce (val > 50) |

---

## 2. GPU Tests (test_gpu.cu) — ~6 checks

Validates GPU substrate integration: child spawning and retina readback.

| Test | What it verifies |
|------|-----------------|
| `children spawned` | Engine produces at least one child sub-graph |
| `child has 13 nodes` | Child graph has expected retina topology (13 nodes) |
| `child has 8 edges` | Child graph has expected edge count |
| `child ticked` | Child graph has been ticked at least once |
| `output node alive` | Child's output node is alive |
| `retina nodes not layer_zero` | Retina nodes carry real data, not infrastructure |

---

## 3. Lifecycle Tests (test_lifecycle.c) — ~15 checks

Tests the full life cycle: birth, crystallization, lysis (death), and drive states.

| Test | What it verifies |
|------|-----------------|
| `child spawned for remove test` | Precondition: a child exists to remove |
| `child removed` | nest_remove() decrements child count |
| `parent child_id cleared` | Parent node's child_id resets to -1 |
| `dead node val unchanged` | Dead nodes don't accumulate signal |
| `dead node still dead` | Dead nodes stay dead across ticks |
| `fresnel T+R=1 for all K` | Fresnel coefficients conserve energy (T+R=1) |
| `xor: no energy no val` | XOR observer: no energy + no val = 0 |
| `xor: energy + val (constructive)` | XOR observer: constructive interference = 0 |
| `xor: energy + no val (destructive)` | XOR observer: destructive interference = 1 |
| `xor: no energy + val (passthrough)` | XOR observer: passthrough = 0 |
| `destructive: I_energy > 0` | Destructive interference has energy but cancelled val |
| `destructive: val cancelled` | Net value is near zero despite energy |
| `lysis: child spawned/removed/cleared` | Full apoptosis cycle works |
| `error spike from poison` | Contradictory input causes error spike |
| `coherent nodes protected under error` | High-valence nodes survive error spikes |
| `cycle: all 4 children spawned` | Full nesting cycle fills all child slots |
| `cycle: one child removed by lysis` | Weak child gets killed (apoptosis) |
| `cycle: new child spawned into freed slot` | Freed slot gets reused |
| `grow created collision points` | Edge growth creates signal collision points |
| `coh_a/b computed` | Zone coherence metrics are non-zero |
| `prev_val tracked` | Previous value tracking works for delta computation |
| `frustration: incoherent eroded` | High error erodes weak nodes |
| `frustration: coherent mostly held` | Strong nodes survive frustration |
| `boredom: coherent gained valence` | Low error hardens coherent nodes |
| `curiosity: no major valence change` | Medium error keeps valence stable |

---

## 4. Observer Tests (test_observer.c) — ~15 checks

Tests the Z-depth observer stack: same collision, different observer, different answer.

| Test | What it verifies |
|------|-----------------|
| `Z0 obs_bool(+42)=1` | Boolean observer: positive value = true |
| `Z0 obs_bool(-7)=0` | Boolean observer: negative value = false |
| `Z0 obs_bool(0)=0` | Boolean observer: zero = false |
| `Z1 and(50,50,30)=1` | AND observer: both above threshold = true |
| `Z1 and(50,10,30)=0` | AND observer: one below threshold = false |
| `Z1 and(10,50,30)=0` | AND observer: other below threshold = false |
| `Z1 and(-50,50,30)=1` | AND observer: absolute value used |
| `Z3 freq(3,50)=1 resonant` | Frequency observer: 3 crossings + mass = resonant |
| `Z3 freq(1,50)=0 relay` | Frequency observer: 1 crossing = relay (not resonant) |
| `Z3 freq(0,50)=0 dead` | Frequency observer: no crossings = dead |
| `Z3 freq(2,0)=0 no mass` | Frequency observer: crossings but no mass = nothing |
| `Z4 corr(10,5,1)=1 both up` | Correlation observer: both increasing = correlated |
| `Z4 corr(3,8,0)=1 both down` | Correlation observer: both decreasing = correlated |
| `Z4 corr(10,5,0)=0 diverge` | Correlation observer: diverging = uncorrelated |
| `Z4 corr(3,8,1)=0 diverge` | Correlation observer: diverging = uncorrelated |
| `zdep: I_energy > 0` | Integration test: energy present at depth |
| `zdep: Z0 returns 0 or 1` | Z0 observer returns valid boolean |
| `zdep: Z2 XOR fires` | XOR observer fires on destructive interference |
| `zdep: valence grew` | Valence increases over observation window |

---

## 5. Stress Tests (test_stress.c) — ~16 checks

Adversarial tests adapted from USE v3. Push every subsystem to its limits.

| Test | What it verifies |
|------|-----------------|
| `S1 all constants predicted` | ONETWO predicts constant sequences (201 values) |
| `S2 all linear predicted` | ONETWO predicts linear sequences (600 sequences, delta -50..+50) |
| `S3 all geometric predicted` | ONETWO predicts geometric sequences (36 sequences) |
| `S4 all acceleration predicted` | ONETWO predicts acceleration sequences (200 sequences) |
| `S5 density correct for all bytes` | Observation density correct for all 256 byte values |
| `S6 periods detected` | Period detection works for periods 2-32 |
| `S7 repeated halves = 100% symmetry` | Repeated patterns score 100% symmetry (50 trials) |
| `S8 density/symmetry/period in range` | 1000 random bitstreams: all features in valid range |
| `S9 empty/single/maxlen` | Edge cases: empty, single bit, max-length bitstreams |
| `S10 20 trials deterministic` | Same input always produces same engine state (determinism) |
| `S11 all values in [-1M, +1M]` | No value overflow under 1000 random ingests |
| `S12 stable node exists` | At least one node stabilizes after extended run |
| `S12 valence > 0` | Stable node has non-zero valence |
| `S13 novel input created new node` | New data always creates new nodes |
| `S14 no int32 wrap` | No integer overflow in large computations |
| `S15 pool capped at MAX_NODES` | Node pool doesn't exceed MAX_NODES |
| `S15 overflow returns -1` | Node creation returns -1 when full |
| `S16 edges capped at MAX_EDGES` | Edge pool doesn't exceed MAX_EDGES |
| `S16 had failures (overflow rejected)` | Edge overflow is detected and rejected |

---

## 6. Sense Tests (test_sense.c) — ~10 checks

Tests the windowed feature extraction layer (7 feature types: ARDCSBP).

| Test | What it verifies |
|------|-----------------|
| `SE1 fill count/val/ts` | Event buffer fills correctly with values and timestamps |
| `SE2 features found` | Sense extraction produces features |
| `SE2 all 8 channels active` | All 8 feature channels produce output |
| `SE3 duty features found` | Duty cycle features detected from patterned input |
| `SE4 regularity detected` | Regular temporal patterns are identified |
| `SE4 period detected` | Period estimation works |
| `SE5 all sense nodes s: prefixed` | Sense nodes use the "s:" namespace prefix |
| `SE6 sense edges created` | Sense nodes wire to graph nodes |
| `SE7 had features / edge decayed` | Features exist and edges decay over time |
| `SE8 sense nodes have identity` | Sense nodes carry ONETWO identity fingerprints |
| `SE9 sense ran in tick` | Sense runs automatically during engine_tick() |
| `SE10 ingest ok / engine ticked` | Full sense + ingest + tick integration works |

---

## 7. Collision Tests (test_collision.c) — ~7 checks

Tests the engine's response to conflicting data — the immune system.

| Test | What it verifies |
|------|-----------------|
| `prefix collision: divergence detected` | Two patterns sharing a prefix but diverging are distinguished |
| `identical patterns: both survive` | Identical patterns reinforce, not conflict |
| `independent patterns: both survive` | Unrelated patterns coexist without interference |
| `feedback loop: error responds to repeated collision` | Error signal rises under sustained conflicting input |
| `bus: A-B cross-wiring exists` | Cross-group edges form during bus collision |
| `bus: intra-group wiring exists` | Within-group edges also form |
| `bus: graph wired under collision` | Graph remains connected under collision stress |

---

## 8. T3 Stage 1 — Process Isolation (test_t3_stage1.c) — ~3 checks

50 nodes in 3 zones (healthy A, healthy B, sick C with contradictions). Tests whether conservation isolates damage without explicit walls.

| Test | What it verifies |
|------|-----------------|
| `t3s1: cross-zone edges exist` | Zones are connected, not isolated by walls |
| `t3s1: zone B crystallized (stable daemon)` | Healthy zone survives nearby sickness |
| `t3s1: zone A resolved` | Contradicted zone resolves through plasticity + pruning |

---

## 9. T3 Full — Production Load (test_t3_full.c) — ~7 checks

200 nodes across 5 zones (A-E), 30 SUBSTRATE_INT cycles. Tests the engine at realistic scale.

| Test | What it verifies |
|------|-----------------|
| `t3full: zone B crystallized` | Stable zone reaches full crystallization |
| `t3full: zone C crystallized` | Second stable zone also crystallizes |
| `t3full: zone D survived, consistent, accumulating` | Dynamic zone maintains integrity |
| `t3full: zone A alive and differentiated` | Chimera zone stays alive with distinct structure |
| `t3full: zone A Lc var < zone B` | Cleaving cools the conflict zone (lower plasticity variance) |
| `t3full: cleaving active` | Edge severing actively occurs under stress |
| `t3full: no zone collapsed` | All 5 zones survive — no total failure |

---

## 10. Save/Load Tests (test_save_load.c) — ~32 checks

Full v13 persistence round-trip + Yee L-field persistence.

### v13 Graph Round-trip
| Test | What it verifies |
|------|-----------------|
| `sl: save/load succeeded` | File I/O works |
| `sl: total_ticks` | Tick counter persists |
| `sl: learn/boundary/low_error_run` | Engine parameters persist |
| `sl: T.count` | SubstrateT counter persists |
| `sl: onetwo.feedback[7]` | ONETWO feedback state persists |
| `sl: onetwo.tick_count/total_inputs` | ONETWO counters persist |
| `sl: n_nodes/n_edges` | Graph topology persists |
| `sl: grow_interval/grow_mean/thresholds` | Adaptive parameters persist |
| `sl: total_learns/total_grown` | Statistics persist |
| `sl: node alive/valence/val/coherent/name` | Individual node state persists |
| `sl: error_accum/prev_output/heartbeat/drive` | Inner T state persists |
| `sl: n_children/child_owner/child topology` | Child sub-graphs persist |
| `sl: n_boundary_edges` | Boundary edges persist |
| `sl: loaded engine survived 5 more cycles` | Loaded engine continues running |

### Yee L-field Round-trip
| Test | What it verifies |
|------|-----------------|
| `yee_sl: save/load succeeded` | YEE1 block writes and reads |
| `yee_sl: L uniform after re-init` | Yee re-init resets L to default (1.0) |
| `yee_sl: L[0]/L[1000]/L[262143] survived` | Non-default L values survive save/destroy/reload |

---

## 11. Transmission Line Tests (test_tline.c) — ~6 checks

Tests the shift-register edge delay lines that carry signals between nodes.

| Test | What it verifies |
|------|-----------------|
| `tline: Z=0 is XNOR` | At zero impedance offset, collision produces XNOR |
| `tline: Z=1 is AND` | At unit impedance offset, collision produces AND |
| `tline: Z=2 is XOR` | At double impedance, collision produces XOR |
| `tline: XNOR at all 5 impedance values` | XNOR gate is universal across impedance sweep |
| `tline: MAJORITY(A,B,D) 8/8` | Three-input majority gate from composed tlines (all 8 truth table entries) |
| `tline: collision node impedance grew` | Impedance increases at collision points (backreaction) |
| `tline: input node impedance unchanged` | Non-collision nodes unaffected |
| `tline: high-L edge arrives later` | Higher inductance = slower propagation (delay) |
| `tline: Schwarzschild identity exact` | Time dilation formula matches predicted values (6/6) |

---

## 12. Child Conflict Tests (test_child_conflict.c) — ~5 checks

Tests how child sub-graphs respond to contradictory input from their parent.

| Test | What it verifies |
|------|-----------------|
| `cc: child has 13 nodes` | Child graph starts with expected topology |
| `cc: child has 8 edges` | Child graph starts with expected edge count |
| `cc: edges survived stabilization` | Child edges persist through stabilization period |
| `cc: child entered frustration` | Child's drive state responds to conflicting retina input |
| `cc: conflict changed edge structure` | Child graph restructures under sustained contradiction |

---

## Summary by Category

| Category | Tests | Purpose |
|----------|-------|---------|
| Correctness | ~200 | Does the engine produce correct results? |
| Stability | ~30 | Does the engine stay bounded under stress? |
| Persistence | ~35 | Does state survive save/load? |
| Adversarial | ~20 | Does the engine handle edge cases and overflow? |
| Integration | ~15 | Do all subsystems work together? |
| Physics | ~15 | Do wave/tline/Fresnel obey expected laws? |
