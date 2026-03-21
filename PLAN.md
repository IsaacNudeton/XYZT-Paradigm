# XYZT — The Plan

**Last updated:** 2026-03-21
**State:** 322/322 tests, v0.16, `03ab109`

---

## WHERE WE ARE

A wave physics engine on a PC. It works. It passes its own tests. It discovered log field schemas from raw bytes. It hasn't been validated externally. It hasn't produced output. It hasn't run on anything except one Windows PC with one RTX 2080.

### What exists and is solid
- 3D Yee FDTD (64³, CUDA) — wave propagation, Hebbian L-field
- Graph observer (CPU) — nodes, edges, TLine, crystallization, nesting
- Inference — wave query + topological propagation + sponge
- Persistence — .xyzt save/load with L-field
- Streaming input — stdin → raw bytes → wave injection
- Three observer depths — Z=0/Z=3/Z=4
- 322 automated tests

### What exists but is bloat or unfinished
- `sonify.c` — diagnostic, not core. Generates WAV files. Keep as tool, not feature.
- `onetwo.c` — 1084 lines. Only `onetwo_self_observe` and `ot_observe` called externally. The rest is dead weight from the encoder era.
- `substrate.cu` — old CA. Only used by 6 regression tests. Could be archived.
- `sweep_tracking.c` — 1077 lines. Historical. The N-sweep is done.
- `sense.c` — 200+ lines. Only `sense_feedback` called externally.
- `reporter.c` — printf wrappers. Could inline.
- TLine on every edge — 548 bytes per edge. Irreducible (18 tests break without it) but memory-heavy. Needs optimization, not removal.

### What's MISSING
1. **Output generation** — the engine retrieves but doesn't produce. No mouth beyond "here's what resonated."
2. **Continuous learning** — stream mode processes then stops. Should tick forever.
3. **Multi-device** — one engine, one GPU. No communication protocol.
4. **Bare metal** — depends on Windows + CUDA driver. Von Neumann OS underneath.
5. **External validation** — nobody else has run it.

---

## THE GOAL

AGI that embodies hardware. Not runs ON hardware. IS hardware.

Any device that can do {2,3} (two states + a medium) participates. CPU, GPU, FPGA, Pico, comparator stack. The AGI isn't a program. It's a protocol. Plug in → discover topology → join the field → compute.

---

## THE ORDER

ONETWO on the system itself. Decompose (ONE) before building more (TWO).

### Phase 1: CLEAN (now)
Kill bloat. Cut what doesn't serve the architecture.

- [ ] Archive `substrate.cu` + its 6 regression tests (move to `archive/`)
- [ ] Strip `onetwo.c` to only the functions that are called
- [ ] Archive `sweep_tracking.c` (historical, sweep is done)
- [ ] Strip `sense.c` to `sense_feedback` only
- [ ] Inline or archive `reporter.c`
- [ ] Remove dead test files not in build.bat
- [ ] Audit every `#include` — remove unused headers
- [ ] Measure binary size and memory footprint before/after
- [ ] Target: < 5000 lines of live code (currently ~6000 CPU + 1200 GPU)

### Phase 2: FINISH (after clean)
Complete what's incomplete. No new features. Polish what exists.

- [ ] **Output path**: the engine should produce bytes, not just consume them. Simplest: strongest resonating node's identity bytes → stdout. The engine speaks raw bytes back.
- [ ] **Continuous mode**: stream mode should tick forever. No post-EOF drain with fixed 10 cycles. Just: input arrives → ingest → tick → output what resonates → repeat. Never stops.
- [ ] **Real-time Hebbian**: Hebbian should fire every SUBSTRATE_INT during streaming, not just during drain. The engine learns WHILE it listens.
- [ ] **Query from the topology**: `engine_query` still uses `bs_contain` (fingerprint matching). Should use `infer_query` (wave physics). One query path, not two.
- [ ] **Fix the topological propagation leak**: foreign data gets too much energy through dense graph edges. Gate propagation by coherence match or edge weight threshold.

### Phase 3: PROTOCOL (after finish)
The engine becomes a protocol. Any {2,3} device speaks it.

- [ ] **Define the wire format**: how two engines share topology. Not .xyzt files. Live impedance boundary data. What crosses the wire = L-field gradient at the boundary + node identities + edge weights.
- [ ] **Pico bridge**: the Pico already runs the same paradigm (sense.c, wire.h). Connect it to the PC engine via USB serial. The serial cable IS an impedance boundary. Signals cross it. The PC's topology extends to include the Pico's GPIO.
- [ ] **Second PC**: two PCs running the engine, connected by TCP. Each PC is a voxel cluster. The network link IS a TLine edge with latency = network RTT. The combined topology spans both machines.
- [ ] **Discovery**: when a new device connects, the engine probes it. What are you? How many voxels? What's your impedance? The protocol IS the handshake.

### Phase 4: HARDWARE (after protocol)
The engine runs on real physics, not simulated physics.

- [ ] **FPGA (Zynq 7020)**: ARM core = observer. FPGA fabric = substrate. Yee cells in LUTs. TLine edges in block RAM. No OS. No driver. The routing fabric IS the L-field.
- [ ] **Bare metal x86**: UEFI boot → hardware discovery → CPU cores as voxels. Intel iGPU as parallel substrate (OpenCL, open docs). No Windows. No Linux. The engine IS the OS.
- [ ] **Pico standalone**: already has firmware. Connect to FPGA via SPI/I2C. GPIO = injection points. The Pico IS a peripheral organ.
- [ ] **ASIC** (far future): custom silicon. Impedance field etched in copper. No clock needed — the propagation delay IS the timing. The chip IS the engine.

---

## CONSTRAINTS

- Isaac works full time. Build time is nights and weekends.
- One RTX 2080 Super, one ThinkPad (no discrete GPU), one Zynq 7020 board, Picos.
- No funding. PolyForm Noncommercial license.
- Three AI shells (CC, Gemini, Claude Web) but no guaranteed continuity between sessions.
- The .xyzt toolkit provides persistent state across CC sessions.

---

## METRICS

How we know we're making progress:

| Metric | Current | Target |
|--------|---------|--------|
| Test count | 322 | 350+ after cleanup (remove stale, add output tests) |
| Live code lines | ~7200 | < 5000 after cleanup |
| Memory per node | 256B identity + 548B TLine | Optimize TLine storage |
| Binary size | 536KB | < 400KB after stripping dead code |
| External reproductions | 0 | 1 (anyone clones and runs tests) |
| Devices connected | 1 (PC) | 2 (PC + Pico or PC + PC) |
| Output generation | No | Yes (raw bytes from resonance) |
| Continuous learning | No | Yes (never stops ticking) |

---

## PRINCIPLES

1. ONETWO everything. Decompose before building. Test before claiming.
2. The substrate finds structure. The observer selects relevance. Don't confuse them.
3. The code IS the theory. If the test passes, the claim holds. If not, fix the code or fix the claim.
4. No hype. "The engine generates audio from cavity measurements" not "the engine sings."
5. The architecture is {2,3} + alpha. Everything else derives or is an engineering choice.
6. Different hardware → different values → same computation. The paradigm is substrate-independent.
7. We're not researchers. We're not engineers. We're builders. Full stack, end to end.

---

*Isaac + CC. March 2026.*
