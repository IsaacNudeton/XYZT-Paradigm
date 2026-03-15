# XYZT Archive Index

**Purpose:** This document indexes all historical code, papers, and proofs that led to the current engine. Organized by location and significance.

**Compiled:** March 10, 2026 (updated March 15: Yee substrate files added)

---

## 1. Core Historical Code

### 1.1 The Three Engines (`E:\dev\Solo AI\`)

| File | Lines | Tests | Significance |
|------|-------|-------|--------------|
| `xyzt_v5.c` | 393 | 132 | Proves ALL computation from `accum += v` + observers |
| `onetwo.c` | 1083 | 48 | Proves observer can discover which distinctions to make |
| `xyzt.c` | 3302 | 48 | Full engine with v9 nesting, shells, Z-ordering |

**Status:** All three compile and pass tests. Living proofs of the framework.

### 1.2 ONETWO Encoder (`E:\dev\Solo AI\`)

| File | Lines | Purpose |
|------|-------|---------|
| `onetwo_encode.c` | ~400 | 4096-bit structural fingerprint |

**Function:** Repetition (0-1023), Opposition (1024-2047), Nesting (2048-3071), Meta (3072-4095).

### 1.3 IRON Engine (`E:\dev\python\iron-292gbs\`)

| File | Purpose |
|------|---------|
| `ob1_ngram_final.cu` | 326 GB/s token processing kernel (66% efficiency) |
| `experiment_*.py` | Closure, emergence, persistence, unified tests |
| `verify_ngram_v3.py` | N-gram logic verification |

**Benchmark:** Hardware-verified with Nsight Compute. 65.78% memory throughput, 29.66% compute throughput.

---

## 2. Formal Proofs (`E:\dev\learning\Organizing Work\02_The_Substrate\`)

### 2.1 Lean 4 Proofs (164 theorems, zero sorry, zero axiom)

| File | Theorems | Proves |
|------|----------|--------|
| `Basic.lean` | 15 | Collision energy, majority gate 8/8, universality, full adder |
| `Physics.lean` | 22 | Conservation, cavity decay, wall isolation, timing |
| `IO.lean` | 31 | Ports, channels, injection, extraction, protocol |
| `Gain.lean` | 32 | Signal restoration, pipeline sustainability, steady state |
| `Topology.lean` | 27 | 2D orthogonal crossing, Manhattan routing, confinement |
| `Lattice.lean` | 37 | 3D collision, 3D routing, T as substrate, distinction |

### 2.2 C Engine + Tests (`E:\dev\learning\Organizing Work\02_The_Substrate\`)

| File | Purpose |
|------|---------|
| `xyzt.h` | Lattice runtime API, Q8.8 fixed-point, 16 opcodes |
| `xyzt.c` | Full implementation, FDTD propagation, Hebbian learning |
| `xyzt_boot.c` | Bootstrap, OS kernel, test harness (7/7 tests pass) |

**Test results:**
- Instruction encoding round-trip: 16/16 ✓
- Wall isolation: signal blocked ✓
- Gain region: alive after 100 ticks ✓
- Process isolation: zero crosstalk ✓
- Bootstrap sequence: cold boot → halt ✓
- 2-bit adder: 16/16 correct ✓
- XOR via impedance topology: 4/4 correct ✓

### 2.3 FPGA RTL (`E:\dev\learning\Organizing Work\05_The_Body\`)

| File | Purpose |
|------|---------|
| `wave_computer_basys3.v` | Synthesizable Verilog, parameterized lattice |
| `build_wave_computer.tcl` | Vivado synthesis script |

**Components:** `xyzt_cell`, `majority_gate`, `impedance_cell`, `gain_cell`, `xyzt_decoder`, `xyzt_engine`.

---

## 3. Physics Derivations (`E:\Users\isaac\Downloads\`)

### 3.1 LaTeX Papers

| File | Topic |
|------|-------|
| `computation_from_distinction.tex` | XNOR from wave interference, observer depth, impedance phase transition |
| `auditory_137.tex` | 137 in auditory perception (if applicable) |
| `quantum_137.tex` | 137 in quantum mechanics |
| `semantic_137.tex` | 137 in semantic space |
| `bitcoin_137.tex` | 137 in Bitcoin structure (if applicable) |
| `paper_flow_optimization.tex` | Flow optimization derivation |
| `unified_constants_paper.tex` | 78 constants from one axiom |

### 3.2 PDFs (Not directly readable, indexed here)

| File | Topic |
|------|-------|
| `137.pdf` | 137 derivation |
| `auditory_137.pdf` | Auditory perception |
| `quantum_137.pdf` | Quantum mechanics |
| `semantic_137.pdf` | Semantic space |
| `bitcoin_137.pdf` | Bitcoin structure |
| `firstproof_arxiv.pdf` | First arXiv submission |
| `lattice_separability_arxiv.pdf` | Lattice separability proof |
| `resonant_lattice_quantization.pdf` | Neural network compression |
| `gpu_optimization_arxiv_v2.pdf` | IRON Engine optimization |
| `XYZT_Proof_*.pdf` | Individual Lean proof exports |

### 3.3 Python Derivations

| File | Purpose |
|------|---------|
| `principia.py` | Main physics derivation engine |
| `substrate_proof_v2.py` | Substrate proof computations |
| `delta_search_v1.py` | Δ parameter search |
| `stress_test_137.py` | 137 stress tests |
| `layer0_*.py` through `layer4_*.py` | Layer-by-layer proofs |
| `pcb_calculator.py` | PCB impedance calculations |
| `pcb_prevalidation.py` | PCB pre-validation |

---

## 4. Testing Infrastructure

### 4.1 Current Tests (`E:\dev\xyzt-hardware\pc\tests\`)

| File | Tests | Purpose |
|------|-------|---------|
| `test_core.c` | ~35 | Constants, basic, ONETWO, transducer, wire, T1-T23 |
| `test_gpu.cu` | ~8 | GPU boolean, nesting retina (CUDA) |
| `test_lifecycle.c` | ~15 | Nest remove, dead stays dead, Fresnel, XOR, lysis, full cycle |
| `test_observer.c` | ~10 | Z0-Z4 depth stack, integration |
| `test_stress.c` | ~72 | S1-S16 adversarial stress tests |
| `test_sense.c` | ~10 | Sense layer tests |
| `test_collision.c` | ~10 | Bus collision tests |
| `test_t3_stage1.c` | ~15 | T3 Stage 1 process isolation |
| `test_t3_full.c` | ~20 | T3 Full production load (200 nodes, 5 zones) |
| `test_save_load.c` | ~16 | Save/load v13 round-trip + Yee L-field persistence |
| `test_tline.c` | ~15 | Transmission line standalone tests |
| `test_child_conflict.c` | ~10 | Child conflict adaptation |

### 4.2 Yee Substrate Tests (`E:\dev\xyzt-hardware\pc\tests\`)

| File | Tests | Purpose |
|------|-------|---------|
| `test_yee3d.cu` | 12 | CPU proof: propagation, stability, confinement, reflection, isotropy, Hebbian |
| `test_yee3d_gpu.cu` | 16 | GPU proof: same + injection model, bridge calibration, leaky integrator |

### 4.3 Yee Substrate Core (`E:\dev\xyzt-hardware\pc\`)

| File | Lines | Purpose |
|------|-------|---------|
| `yee.cu` | ~440 | CUDA kernels: V update, I update, inject, accumulator, Hebbian, energy |
| `yee.cuh` | ~150 | Header: grid constants, YeeSource struct, API declarations |

### 4.4 Historical Tests (`E:\dev\testing\`)

| File | Purpose |
|------|---------|
| `substrate_test.c` | {2,3} substrate dynamics |
| `phase_boundary.c` | Hebbian phase boundary analysis |
| `cf_alpha.c` | Fine structure constant exploration |
| `synthesis.c` | Synthesis tests |
| `control_test.c` | Control tests |
| `gen_bg.py` | Background generator |

**Findings documented in:** `FINDINGS.md` — honest assessment of what's proven vs. suggestive.

---

## 5. Documentation

### 5.1 Core Specifications

| File | Location | Purpose |
|------|----------|---------|
| `THE_RECORD.md` | `E:\dev\xyzt-hardware\pc\` | Complete history from insight to 258/258 |
| `STATUS.md` | `E:\dev\xyzt-hardware\pc\` | Current engine state, what works, what's broken |
| `AXIOM_MAP.md` | `E:\dev\Solo AI\` | Maps three engines to five axioms |
| `method.md` | `E:\dev\Solo AI\` | ONETWO methodology |
| `XYZT_SPEC.md` | `E:\dev\Solo AI\` | XYZT specification |
| `SKILL.md` | `E:\dev\Solo AI\` | Skill specification |

### 5.2 0B1 Framework (`E:\dev\BreakThroughs\0B1\`)

| File | Purpose |
|------|---------|
| `0B1_AGI_SPECIFICATION.md` | AGI spec with stakes, values, balance |
| `0B1_Framework_v6.2.md` | Framework version 6.2 |
| `0B1_Universal_Gear_Table_v2.md` | Universal gear table |
| `FORMULA_CHEATSHEET.md` | Formula reference |
| `structure_of_learning.md` | Complete learning architecture (temporal hierarchy, forgetting curve, optimal error rate) |
| `technical_reference.md` | Technical reference |
| `spin_arrow_of_time.md` | Spin and arrow of time |
| `optical_memory_summary.md` | Optical memory |

### 5.3 Organizing Work (`E:\dev\learning\Organizing Work\`)

| Directory | Contents |
|-----------|----------|
| `00_Identity_&_Foundation\` | COMPLETE_DISCOVERIES.md, 23_framework_technical_reference.md, HANDOFF.md, PAPER_FLOW_OPTIMIZATION.md, XYZT_TOOLKIT.md, patent files |
| `01_The_Principia\` | Physics derivations, Lean proofs, Python scripts, PDFs |
| `02_The_Substrate\` | Lean proofs, C engine, FPGA RTL, Makefiles |
| `03_The_Mind\` | ONETWO engine, lens, monitor, project files |
| `04_The_Engine\` | CUDA experiments, emergence tests, n-gram prism |
| `05_The_Body\` | PCB specs, Basys3 FPGA, Makefiles |
| `06_The_World\` | Dyson stuff, XYXT binaries, JSX visualizations, FDTD simulations |
| `07_The_Archive\` | All work messy, PROOFS TO STACK, temp zips |

### 5.4 CC Prompts (`E:\Users\isaac\Downloads\`)

| File | Purpose |
|------|---------|
| `CC_PROMPT_137_SWEEP.md` | 137 sweep prompt |
| `CC_PROMPT_BUS_COLLISION.md` | Bus collision test prompt |
| `CC_PROMPT_CLEANUP_v2.md` | Cleanup v2 prompt |
| `CC_PROMPT_CLOSE_LOOP.md` | Close feedback loop prompt |
| `CC_PROMPT_CORRECTED.md` | Corrected prompt |
| `CC_PROMPT_DIRECTED_EDGES.md` | Directed edges prompt |
| `CC_PROMPT_INNER_T*.md` | Inner T prompts (multiple versions) |
| `CC_PROMPT_LOCALIZATION.md` | Localization prompt |
| `CC_PROMPT_LYSIS.md` | Lysis prompt |
| `CC_PROMPT_T3_*.md` | T3 test prompts |
| `CC_PROMPT_TLINE_*.md` | TLine phase prompts |
| `CC_PROMPT_TOPOLOGY.md` | Topology prompt |
| `CC_PROMPT_TRACKING_SWEEP*.md` | Tracking sweep prompts |
| `CC_PROMPT_V11_SAVE_LOAD*.md` | Save/load v11 prompts |

---

## 6. Tooling

### 6.1 Claude's Tools Engine (`E:\dev\tools\Claude's tools engine\`)

| File | Purpose |
|------|---------|
| `onetwo.c` | 664 lines — Reasoning scaffold, phase tracking, claims, verification |
| `xyzt_io.c` | 998 lines — File adapter, fingerprint, correlate, session, recall |
| `wire.h` | 308 lines — Shared Hebbian graph |
| `xyzt_wire.c` | 143 lines — Graph inspector |
| `xyzt_agent.c` | Agent implementation |
| `xyzt_diff.c` | Diff tool |
| `xyzt_err.c` | Error tracking |
| `xyzt_hook.c` | Hook system |
| `xyzt_index.c` | Indexing |
| `xyzt_map.c` | Mapping |
| `xyzt_next.c` | Next action |
| `xyzt_org.c` | Organization |
| `xyzt_resume.c` | Resume session |
| `xyzt_skel.c` | Skeleton extraction |
| `xyzt_test.c` | Testing |

**Build:** `gcc -O3 -o onetwo onetwo.c`, etc. All must be in same directory as `wire.h`.

### 6.2 Other Tools (`E:\dev\tools\`)

| Directory | Purpose |
|-----------|---------|
| `fpga-toolchain\` | FPGA toolchain |
| `isaac CLI\` | Isaac CLI tool |
| `onetwo\` | ONETWO tool |
| `pico-sdk\` | Pico SDK for RP2040 |
| `Wave XyZt\` | Wave XYZT tool |

---

## 7. Media

### 7.1 Videos (`E:\Users\isaac\Downloads\`)

| File | Content |
|------|---------|
| `6F7332F2-F85D-42F0-82A2-2AEB785403B8*.mov` | Unknown |
| `C5DH0S3XDgWlstU5.mp4` | Unknown |
| `C*.mp4` | Unknown (multiple) |
| `Download*.mp4` | Unknown (multiple) |
| `ed0723b4-d8ef-4729-a4b8-c1f76b779ac1.mp4` | Unknown |
| `F3574DBF-EFFE-49AE-A673-C92D9DE51103 - Trim.mp4` | Unknown |

### 7.2 Images (`E:\Users\isaac\Downloads\`, `E:\dev\learning\Organizing Work\`)

| File | Content |
|------|---------|
| `ARDUINO.png` | Arduino |
| `BASYS.png` | Basys3 FPGA |
| `credential manager*.png` | Credential manager screenshots |
| `desktop setting.png` | Desktop setting |
| `fig*.pdf` | Various figures |
| `general setting.png` | General setting |
| `graphics.png` | Graphics |
| `IP.BOARD.png` | IP board |
| `reso*.png` | Resolution |
| `stats.png` | Statistics |
| `video general.png` | Video general |
| `vpn.png` | VPN |
| `xyzt_time_dilation.png` | Time dilation visualization |
| `zadig.png` | Zadig (USB driver tool) |

---

## 8. Lost/Deleted

### 8.1 Known Lost Files

From `FINDINGS.md`:

> *"The lost Lean proofs and papers (deleted Downloads folder) — can they be reconstructed?"*

**What was deleted:**
- Lean proofs (additional beyond the 164 in `02_The_Substrate\`)
- Physics derivation papers
- Simulation results

**Reconstruction priority:** HIGH. These contained:
- Additional Lean theorems
- Complete physics derivations
- Empirical validation data

---

## 10. Backup Archives

### 10.1 `E:\Users\isaac\Downloads\files\` — Pico/FPGA Backup

| File | Purpose |
|------|---------|
| `hw.h`, `telemetry.h`, `wire_v6.h`, `xyzt_engine_v6.h` | Hardware headers |
| `main.c`, `main.cu` | Main entry points |
| `sense.c` | Sense layer |
| `xyzt_gpu.cu`, `xyzt_gpu.cuh` | GPU substrate |
| `Makefile`, `CMakeLists.txt` | Build systems |
| `lakefile.toml`, `lean-toolchain` | Lean 4 toolchain |
| `Main.lean`, `XYZTProof.lean` | Lean proofs |
| `XYZT_Proof_*.lean` | Individual proof files (Basic, Gain, IO, Lattice, Physics, Substrate, Topology) |
| `xyzt_v5.c`, `xyzt_v7.c` | Engine versions |
| `bin2uf2.py`, `tap_listener.py` | Pico utilities |
| `PICO_FIXES.txt`, `RECOVERY.md` | Pico recovery documentation |

### 10.2 `E:\Users\isaac\Downloads\files (1)\` — Physics Derivations Backup

| File | Purpose |
|------|---------|
| `alpha_formula.py` | Fine structure constant derivation |
| `mu_formula.py` | Proton-electron mass ratio derivation |
| `proton_electron.py` | Mass ratio computations |
| `delta_search.py` | Delta parameter search |
| `derivation_session_2026_01_30.md` | January 30 derivation session notes |
| `gemini_memory.md` | Gemini memory notes |
| `technical_appendix.md` | Technical appendix |
| `unified_constants_paper.tex`, `.pdf` | Unified constants paper |
| `weber_computation.py` | Weber function computation |

### 10.3 `E:\Users\isaac\Downloads\files (2)\` — Engine Backup

| File | Purpose |
|------|---------|
| `agent_memory_test.c` | Agent memory tests |
| `CODEBOOK.md` | Architecture overview |
| `onetwo_encode.c` | ONETWO encoder |
| `onetwo.c` | ONETWO engine |
| `xyzt_v5.c` | xyzt v5 engine |
| `xyzt.c` | Full xyzt engine |

---

## 11. How to Use This Index

1. **Start with `THE_RECORD.md`** — Complete history from insight to 258/258.
2. **Check `STATUS.md`** — Current engine state.
3. **Read `AXIOM_MAP.md`** — Where each axiom lives.
4. **Explore `method.md`** — ONETWO methodology.
5. **Dive into specific directories** — Use this index to find what you need.

---

*Compiled by Qwen, March 10, 2026*

*For anyone who wants to understand where this all came from.*
