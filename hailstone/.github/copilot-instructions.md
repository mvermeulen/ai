# GitHub Copilot Repository Instructions

This file provides context and coding guidelines for GitHub Copilot when generating code, auto-completing logic, or responding in Copilot Chat within this repository.

---

## 1. Project Context & Technologies
- **Name**: Hailstone (Collatz) Search Program.
- **Goal**: Multi-backend, high-performance search for Collatz peaks across three metrics (Max Value, Steps, Stopping Time $\sigma$).
- **Languages**: C++20, GLSL (Vulkan Compute), HIP/C++ (ROCm).
- **Core Optimizations**: Vermeulen Polynomials, Suffix-First Search equivalence classes, polynomial step lookup termination table (`steps8`), native 64-bit transition under $2^{32}$, AVX-512 CPU SIMD Vectorization.

---

## 2. Core Rule: Mathematical Correctness First
- **Rule**: Correctness is the absolute priority. Speed is meaningless without correct mathematical trajectories.
- **Arithmetic Suggestions**: 
  - For values $\ge 2^{32}$, calculations must execute using the custom 128-bit unsigned integer type `uint128` defined in [include/uint128.h](file:///home/mev/source/ai/hailstone/include/uint128.h).
  - When suggesting additions or shifts, always use overflow-checked methods: `add_check_overflow`, `shift_left_1`, and `check_mul3_add1_overflow` / `mul3_add1`.
  - For values $< 2^{32}$, loops transition to fast, native 64-bit (`uint64_t`) arithmetic. Ensure that the logic transitioning from 128-bit to 64-bit does not drop or duplicate iterations.

---

## 3. Recommended Workflow & Verification Rules
When suggesting code modifications:
1. **Compilation**: Remind the user to configure and build from the `build` directory:
   ```bash
   mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -j$(nproc)
   ```
2. **Correctness Verification**:
   - Run `hailstone_test_uint128` to verify custom arithmetic correctness.
   - Run `hailstone_verify` to perform full cross-backend differential alignment and checkpoint verification.
3. **Benchmark Verification**:
   - Run `python3 benchmarks/benchmark.py --mode quick` to verify that modifications do not introduce performance regressions.

---

## 4. Documentation & Log Maintenance
- **Peak Records**: If the search range increases or new peaks are found, tell the user to run `python3 tools/generate_peaks_doc.py` to regenerate the documentation in `doc/hailstone_peaks.md`.
- **Performance History**: Benchmarking changes automatically appends run metadata to `benchmarks/history.json` and updates the table in `benchmarks/README.md`.
- **Rule Sustainability**: If project structure or build steps change, help keep `CLAUDE.md`, `.clinerules`, and `.github/copilot-instructions.md` updated.
- **Local/Less-capable LLM Compatibility**: Keep comments clean, explicit, and document mathematical formulas completely. Do not guess on complex SIMD or shader code.
