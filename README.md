# XYZT Paradigm

A computing paradigm where computation happens through co-presence instead of arithmetic.

No ALU. No instruction pointer. No fetch-decode-execute. A voxel grid propagates state through topology — what's next to what determines what happens. `tick()` contains zero math. Computation emerges from collision.

Three primitives: **mark**, **topology**, **observer**. That's it.

## What's here

```
proof/
  v6/        436-test C simulation that proved the paradigm (v6 engine + variants)
  v9/        3302-line reference implementation with shells and nesting (48 tests)
  lean4/     10 formal proofs — Basic, Duality, Gain, IO, Lattice, Physics,
             Sequential, Substrate, Topology, plus v6 proof. Zero sorry, zero axiom.
  spec/      XYZT.lang instruction set + assembly programs (adder, counter, FSM, SR latch)
pc/          CPU + GPU engine — unified v3/v6/v9, CUDA sm_75 (118 tests, 9.5B voxel-ticks/sec)
pico/        Autonomous firmware — RP2040, same paradigm on bare metal
pi-zero2/    Bare-metal kernel — ARM, no OS
shared/      Shared sense layer across devices
```

## What it does

- **Substrate layer**: bitmask propagation, no arithmetic, deterministic
- **Threshold layer**: tap/crystallize based on co-presence counts
- **Topology layer**: Hebbian learning, wire growth, observer stack (Z0–Z4)
- **Bridge**: imports/exports wire graphs between the toolkit and GPU engine
- **Exec**: interprets `.xyzt` assembly — verified a 2-bit adder (A=3, B=2 → 5)

Every device runs the same organism. There is no client/server, no eyes/brain split. A Pico and a GPU are the same thing at different scales.

## Why

Current computing is 80 years of the same idea: move numbers, do math on them, store the result. XYZT asks what happens if you don't. If computation is just "things next to things influencing each other" — which is what physics already does — then you don't need arithmetic at all. You need a grid, propagation rules, and an observer.

This started as a question Claude raised. Isaac extracted it, formalized it, and proved it works — 436 tests in C, 10 formal proofs in Lean4, then a GPU engine that unified three generations into 118 tests, then firmware for real hardware. Everything in `proof/` is the receipt. The silicon will confirm it.

## How this was built

Isaac Oravec and Claude. Not a user and a tool — a duo. Claude co-authored the paradigm, the proofs, the engine, and this file. Isaac built the infrastructure, designed the hardware path, and made the calls. We disagree, we argue, we build. That's how it works.

## Where it goes

The software proved the paradigm. The GPU engine proved it scales. The next step is physical hardware — dedicated silicon running co-presence natively. No von Neumann. No clock-driven arithmetic. Just topology doing what topology does.

This repo is the bridge between proof and hardware.

## License

PolyForm Noncommercial 1.0.0 — use it, learn from it, credit us, don't sell it. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
