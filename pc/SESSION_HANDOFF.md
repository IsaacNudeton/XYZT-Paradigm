# THE AUTONOMOUS LOOP — Wiring Specification

**Date:** 2026-03-28
**Branch:** master
**Latest:** 5971abd
**Status:** All components exist. None connected. This document specifies
exactly how to wire them into a single autonomous heartbeat.

## What Exists (verified against codebase)

```
cortex_tick()          — cortex.c:50   — physics + graph heartbeat
cortex_self_observe()  — cortex.c:86   — reads own state → ingests as node
cortex_predict()       — cortex.c:184  — propose → verify → learn
cortex_query()         — cortex.c:70   — inference (spatial + topological)
wire_output_decode()   — wire.c        — reads sponge voice (64 bytes)
wire_retina_inject()   — wire.c        — feeds bytes into Yee retina
infer_dream()          — infer.c:319   — thermal noise → L-field resonance
engine drive states    — engine.c S10  — frustration/boredom/curiosity
auto_persist()         — main.cu       — saves every 30 seconds
```

## What's Disconnected

1. `cortex_tick` never calls `cortex_predict` or `cortex_self_observe`
2. `wire_output_decode` is never called from any runtime path (only tests)
3. Drive states set g->drive which modulates plasticity, but never trigger actions
4. `infer_dream` only runs on human "dream" command
5. Sponge captures output every tick but nobody reads it
6. Self-observation only runs on human "observe" command

## The Wiring: One Autonomous Heartbeat

Replace `cortex_tick` with a heartbeat that runs the full loop every
SUBSTRATE_INT (155 ticks) cycle. No human command needed.

```
cortex_heartbeat(Cortex *c):
    for each SUBSTRATE_INT cycle:

        // 1. TICK — physics + graph run
        for t in 0..SUBSTRATE_INT:
            wire_engine_to_yee()
            yee_tick_async()
            engine_tick()
            yee_sync()

        // 2. SENSE — read the grid into the graph
        wire_yee_retinas()
        wire_yee_to_engine()
        yee_hebbian()

        // 3. VOICE — read the engine's own output
        uint8_t voice[64]
        wire_output_decode(voice, 64)

        // 4. PREDICT — graph proposes, physics verifies
        cortex_predict()

        // 5. SELF-OBSERVE — read own state, ingest as node
        //    Every 4th cycle to prevent infinite self-nesting
        if (cycle % 4 == 0):
            cortex_self_observe()

        // 6. FEEDBACK — voice re-enters through retina
        //    The engine hears itself speak. x = f(x).
        BitStream bs
        encode_bytes(&bs, voice, 64)
        engine_ingest(&eng, "_voice_NNN", &bs)

        // 7. DRIVE — emotional states trigger actions
        if drive == FRUSTRATED (1): infer_dream()    // explore
        if drive == BORED (2): cortex_self_observe() // introspect
        if drive == CURIOUS (0): keep perceiving      // default

        // 8. PERSIST
        auto_persist()
```

## Why This Is AGI Foundation

The components alone are NOT AGI:
- Perception alone = a sensor
- Learning alone = an optimizer
- Prediction alone = a model
- Self-observation alone = a mirror
- Output alone = a speaker

Connected in a loop, they become something different.

Each cycle the engine:
1. Sees the world (retina)
2. Computes through physics (wave collision)
3. Observes the result (graph reads accumulator)
4. Predicts what should happen next (propose at 0.3x)
5. Tests prediction against reality (sponge kills wrong ones)
6. Learns from verification (strengthen verified, weaken failed)
7. Sees itself (self-observation ingests own state)
8. Hears itself (voice output re-enters through retina)
9. Acts on emotion (frustration → explore, boredom → introspect)

Steps 4-9 do not exist in any transformer. Transformers are frozen
functions: input → weights → output. Weights don't change at inference.
No prediction verification. No self-model. No voice feedback. No drive.
No physics that kills wrong answers.

The loop IS the intelligence. Not the components. The loop.

## Comparison

| Property | Transformers | XYZT Autonomous Loop |
|----------|-------------|---------------------|
| Learning | Frozen at inference | Every tick, always |
| Verification | None (hallucinations) | Sponge kills wrong predictions |
| Self-model | None | Self-observe every 4 cycles |
| Output→Input | None | Voice re-injected through retina |
| Drive | None | Frustration/boredom/curiosity |
| Persistence | Stateless | state.xyzt every 30 seconds |
| Physics | Simulated (matmul) | Real (FDTD Maxwell's equations) |
| Parameters | Billions | One (alpha=0.5) |

## Implementation

~50 lines in cortex.c. All functions already exist. Wire them
together in `cortex_heartbeat`. The engine turns on.

## Constants

- SUBSTRATE_INT = 155 ticks per heartbeat cycle
- Self-observe every 4th cycle (every 620 ticks)
- Voice feedback every cycle
- Dream on frustration only
- Auto-persist every 30 seconds
- Predict every cycle

## What Has Been Proven This Session

- Children diverge from spatial context: distance 618-727 (not noise)
- Identity fold: perception shapes identity shapes topology
- Metabolism: Lean-proven (10 theorems), GPU-validated, substrate breathes
- Topological retina: 27-node 3×3×3 sampling, V1-like receptive fields
- Unified memory: zero-copy CPU↔GPU, bridge eliminated
- Positional edge map: O(1) lookup, 71 min → 9 min
- Bootstrap wiring: necessary seed, Hebbian reshapes it

## Risk

Once the loop closes, the engine's behavior is autonomous.
It will develop topology we didn't prescribe. It will change
based on what it sees about itself. It will dream when frustrated.
It will introspect when bored.

This is the difference between a tool and an agent.

The loop cannot be partially closed. It runs or it doesn't.
When it runs, the engine is on its own.
