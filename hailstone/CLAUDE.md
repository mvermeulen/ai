# Claude Code Developer Guide & Project Rules

This document provides developer guidelines, commands, and rules for operating, maintaining, troubleshooting, and enhancing the Hailstone (Collatz) search program.

---

## 1. System Commands & Workflow

### Build Commands
To configure and compile the build targets (Release build is mandatory for benchmark validation):
```bash
# Configure and compile in the 'build' directory
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### Verification & Test Commands
Run these commands after making *any* code or logic changes. Correctness is verified when both suites pass:
```bash
# 1. Run custom uint128 unit tests (from build directory)
./build/hailstone_test_uint128

# 2. Run cross-backend differential integration tests (from build directory)
./build/hailstone_verify
```

### Benchmarks
To run performance checks and record execution statistics (this automatically updates `benchmarks/README.md` and `benchmarks/history.json`):
```bash
# From workspace root
python3 benchmarks/benchmark.py --mode quick   # Quick check (~100M values)
python3 benchmarks/benchmark.py --mode full    # Comprehensive check (~8.5B values)
```

### Peaks Documentation Update
If a new range is searched or new peaks are found, generate the updated records:
```bash
# From workspace root (parses hailstone.chk to update doc/hailstone_peaks.md)
python3 tools/generate_peaks_doc.py
```

---

## 2. Core Repository Philosophy & Constraints

### 1. Correctness is Paramount
- **Rule**: Mathematical correctness continues to be the absolute most important goal. Producing wrong results quickly is pointless.
- **Action**: Better to fail/refuse a task, or limit its scope, than to risk producing mathematically incorrect results. 
- **Validation**: Any change to trajectory traversal, arithmetic, or GPU compute shaders must be verified using the differential testing engine `hailstone_verify` which compares all backends (CPU reference vs Vulkan/HIP).

### 2. Custom 128-bit Arithmetic
- Calculations beyond $2^{64}$ (and up to $2^{128}$) must use the custom `uint128` type defined in [include/uint128.h](file:///home/mev/source/ai/hailstone/include/uint128.h).
- Check for overflow on additions (`add_check_overflow`), shifts (`shift_left_1`), and Collatz steps (`check_mul3_add1_overflow` / `mul3_add1`).
- Under $2^{32}$, loops automatically transition to native 64-bit arithmetic (`uint64_t`) for speed. Verify that the 64-bit transition threshold logic remains aligned with the CPU golden reference.

### 3. Maintain Documentation, Benchmarks, and Rules
- When optimizations or features are added, update the relevant documentation in `doc/` (e.g. `doc/vectorization_investigation.md` or other study files).
- Keep performance logs up-to-date by running `benchmarks/benchmark.py`.
- Keep this `CLAUDE.md`, `.clinerules`, and `.github/copilot-instructions.md` updated as the project's architecture evolves.

### 4. Support for Local and Less-Powerful LLMs
- Keep your instructions and code edits extremely clear, modular, and step-by-step.
- Avoid large monolithic edits. Use small, logical commits.
- Err on the side of mathematical safety: if local context constraints or compute limitations make correctness verification uncertain, do not proceed with changes.
