# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

This repository implements a high-performance, multi-backend Collatz (3x+1) peak search program, extending the Leavens-Vermeulen (1992) paper to search for new peaks in three statistics: **Max Value**, **Steps** (total stopping time), and **Stopping Time σ**. Values beyond 2^64 are handled via a custom 128-bit integer type with overflow checking.

---

## 1. Golden Rule: Correctness Above All

Mathematical correctness is the absolute top priority — producing wrong results quickly is worse than not producing them. When touching trajectory traversal, arithmetic, table generation, or checkpoint format, verify with the differential test suite (below) before considering the change done. Prefer failing/limiting scope over risking incorrect search results. If uncertain whether a change preserves correctness, say so rather than proceeding.

## 2. Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

Release build is mandatory for any benchmark/performance validation. Optional CMake options: `TRACK_ALMOST_PEAKS`, `EXCLUDE_01_SUFFIX` (requires the former), `OMIT_STEPS_COMPUTATION` (builds the `_nosteps` high-throughput variants, also passed to `glslc` for the Vulkan shader).

Key build targets: `hailstone_cpu`, `hailstone_vulkan` (if Vulkan+glslc found), `hailstone_hip` (if ROCm/HIP compiler found), `hailstone_verify`, `hailstone_test_uint128`, `hailstone_path`, `hailstone_backwards`, `hailstoned` (distributed worker daemon), plus `_nosteps` and `_profile` variants of the search binaries. Vulkan and HIP backends are conditionally compiled based on `find_package`/`check_language` — don't assume they exist on every machine.

### Profiling

```bash
make hailstone_cpu_profile -j$(nproc)
perf record -g -- ./build/hailstone_cpu_profile --num-blocks 1
perf report --annotate
```

Other profile targets: `hailstone_verify_profile`, `hailstone_path_profile`, `hailstone_vulkan_profile`, `hailstone_hip_profile`. See `doc/2026-06-15-profiling.md`.

## 3. Verification & Tests

Run after **any** code or logic change; a change isn't done until all of these pass:

```bash
./build/hailstone_test_uint128                                   # uint128 arithmetic unit tests
./build/hailstone_verify                                         # cross-backend differential tests (CPU vs Vulkan/HIP)
python3 tools/compare_checkpoint.py hailstone_debug.chk golden_master.chk  # vs Leavens-Vermeulen golden master
```

`hailstone_verify` (`tests/test_verify.cpp`) runs the compiled binaries as subprocesses (`popen`) with matched arguments and diffs their reported peaks/metrics — it needs the binaries already built, and it force-injects `--cutoff-width 0` unless the invocation already specifies a cutoff width or is a help call.

## 4. Benchmarks

```bash
python3 benchmarks/benchmark.py --mode quick   # ~100M values
python3 benchmarks/benchmark.py --mode full    # ~8.5B values
```

This automatically appends to `benchmarks/history.json` and updates `benchmarks/README.md`. Don't land a change that regresses these numbers without calling it out. `benchmarks/extended_sweep.py` and `benchmarks/block_sweep.py` run larger configuration-matrix sweeps (backend × threads × domain-switching × cutoff-width) at deeper block ranges.

## 5. Peaks Documentation

If a new range is searched or new peaks are found:

```bash
python3 tools/generate_peaks_doc.py   # parses hailstone.chk -> doc/hailstone_peaks.md
```

---

## Architecture

### Three interchangeable search backends, one arithmetic contract

- **`cpu/`** — multi-threaded OpenMP reference implementation. This is the **golden standard**; Vulkan and HIP must match its output exactly for the same inputs. `cpu_search.cpp` has scalar paths; `cpu_search_avx512.cpp` is a separately-compiled (`-mavx512f -mavx512cd -mavx512dq`) SIMD path processing 8 trajectories per `__m512i`, auto-detected at runtime (`is_avx512_supported()`) with scalar fallback — never assume AVX-512 is available.
- **`gpu_vulkan/`** — cross-vendor compute shader backend (`shader.comp`, GLSL with `GL_EXT_shader_explicit_arithmetic_types_int64`), compiled to SPIR-V by `glslc` at build time (or copied from a precompiled `.spv` if `glslc` isn't found).
- **`gpu_hip/`** — AMD ROCm backend (`.hip.cpp`), targeting `gfx1150` by default (`CMAKE_HIP_ARCHITECTURES`).

All three share `include/common.h` (the `CollatzStats`/`PeakRecord`/`PeakState`/`SearchMetrics` structs used everywhere) and `include/uint128.h` (custom 128-bit type — see below). `HD_ATTR` in `uint128.h` expands to `__host__ __device__ inline` under HIP/CUDA compilers and plain `inline` otherwise, so the same header compiles across all backends including device code.

### uint128 arithmetic rules

- Anything that can exceed 2^64 must go through `uint128` (`include/uint128.h`), not native ints.
- Always use the overflow-checked entry points for arithmetic that can overflow: `add_check_overflow`, `shift_left_1`, `check_mul3_add1_overflow`/`mul3_add1`.
- **64-bit fast path**: whenever a trajectory or search range is entirely under 2^32 ("block 0"), the code switches to native `uint64_t` loops for speed (see `cpu_search_block_0*` vs `cpu_search_blocks_gt_0*` in `cpu/cpu_search.h`). Any change to trajectory math must keep this transition point exactly aligned with the CPU golden reference — mismatches here are the classic source of silent correctness bugs.

### The optimization stack (order they compose in)

Understanding these is necessary before touching search-loop code, since they layer on top of each other:

1. **Vermeulen polynomials** (`fpoly`/`mpoly`, generated by `tools/genpoly.cpp` into `include/fpoly_table.h`) — collapse identical odd/even step sequences for a residue class mod 2^w into closed-form polynomials, avoiding per-step simulation until the trajectory has been divided by 2 exactly w times.
2. **Steps lookup table** (`include/steps_table.h`, `steps8`) — once a trajectory drops below 2^8, the remaining step count to reach 1 is looked up in O(1) instead of simulated.
3. **Suffix-First search** (`--cutoff-width`, default 20; `tools/gen_allowed_suffixes.cpp` precomputes `allowed_suffixes_24.bin` at build time via the `generate_suffixes_24` custom target) — restricts the search to non-redundant residue-class suffixes (mod-6 filtering + even-class exclusion), pruning ~88% of trajectories. `--cutoff-width 0` disables it.
4. **Domain switching** (`--domain-switching`, David Bařina's technique) — helps CPU (fewer instructions) but hurts GPU (128-bit emulation overhead from branch divergence) — don't enable it by default on GPU code paths.
5. **AVX-512 vectorization** — CPU-only, only active when cutoff width > 0, and only a net win when domain-switching is also on (offsets lane-refill overhead).
6. **`OMIT_STEPS_COMPUTATION`** — compile-time flag building `_nosteps` binaries that skip exact step-count tracking entirely for max throughput; changes `PeakState`'s default `current_max_steps` sentinel (see `include/common.h`).

If you change table generation logic (`tools/genpoly.cpp`, `tools/gen_allowed_suffixes.cpp`) the precomputed headers/binaries (`fpoly_table.h`, `steps_table.h`, `allowed_suffixes_24.bin`) must be regenerated and re-verified, not hand-edited.

Design rationale and measured numbers for each of these live in dated study docs under `doc/` (e.g. `doc/2026-06-11-fpoly_apriori_cutoffs_investigation.md`, `doc/2026-06-20-domain_switching_arithmetic_study.md`, `doc/2026-06-14-vectorization_investigation.md`) — check for an existing doc before re-deriving an investigation.

### Checkpointing

Search binaries load/save peak state and last-checked position to a `.chk` file (default `hailstone.chk`) so runs can resume. Resuming with no bounds continues from `last_num + 1` for a default 100,000-number range; an explicit `--start-num`/`--start-block` still loads baseline peak thresholds from the checkpoint. `tools/compare_checkpoint.py` diffs a checkpoint against `golden_master.chk` (and the `golden_48/50/52.chk` snapshots) for correctness regression checking.

### Distributed mode

`distributed/controller.py` (Python, hosts a web UI in `distributed/web/`) partitions a search range across worker machines and dispatches to `hailstoned` (`distributed/hailstoned.cpp`, packaged via `distributed/package_deb.sh`), a per-worker daemon that auto-detects locally compiled CPU/Vulkan/HIP binaries, micro-benchmarks them at startup, and talks to the controller over a raw TCP socket protocol. Peaks from all workers are merged into a single `.chk` file that's binary-compatible with single-node runs. `distributed/daemon.py` is a legacy/alternate Python daemon implementation.

### `hailstone_path` utility

`tools/hailstone_path.cpp` renders a single trajectory as a symbol string (`*` = 3x+1 step + halving, `/` = plain halving, `^` = peak marker, `|` = stopping-time marker), useful for manually sanity-checking a specific starting value against backend output.

---

## Working with local/less-capable LLMs

Keep edits small and incremental rather than large monolithic rewrites — this codebase's correctness bar means large unreviewed diffs in trajectory/arithmetic code are risky. If context or compute constraints make it hard to verify correctness of a change, don't proceed — say so.

## Keeping instructions in sync

`.clinerules` and `.github/copilot-instructions.md` mirror this file for other tools. If you change build steps, test suites, or repository structure, update those too.
