# CPU Performance Profiling Report: Block 1024 Search (Warm Start)

This report provides a detailed microarchitectural performance analysis of the CPU backend search program (`hailstone_cpu`) executing on **Block 1024** (the numeric range `[4,398,046,511,104, 4,402,341,478,400]`) for exactly 1 block of $2^{32}$ numbers.

To capture representative production behavior, the search was executed as a **Warm Start** by loading the historical steps peak from `hailstone.chk` at startup (restoring the baseline peak of **1549 steps**) and utilizing the new `--no-save-checkpoint` flag to prevent modification of the checkpoint file.

---

## 1. Executive Summary & Hardware Performance Metrics

The CPU search was executed on block 1024 with the Suffix-First search width set to 20. Because we run exactly 1 block, OpenMP executed the search sequentially on a single thread (utilizing 1.0 core).

### CPU Search Telemetry

| Metric | Measured Value | Description |
| :--- | :--- | :--- |
| **Search Range** | `[4,398,046,511,104, 4,402,341,478,400]` | $2^{32}$ values starting at $1024 \times 2^{32}$ ($2^{42}$) |
| **Elapsed Time** | **17.3224 seconds** | Total execution time for the block search |
| **Throughput (Reported)** | **28.15 M numbers/s** | `total_numbers_checked / elapsed_time` |
| **Actual Trajectory Throughput**| **18.38 M trajectories/s** | Number of non-skipped trajectories checked per second |
| **Total Numbers Checked** | `477,618,176` | Non-even numbers in search classes |
| **Skipped (Even)** | `2,147,483,648` | Even numbers skipped at the start |
| **Skipped (Modulo 6)** | `159,206,017` | Suffixes congruent to skipped mod 6 residue groups |
| **Actual Trajectories Computed** | **318,412,159** | Total iterations executing the Collatz loop |
| **Total Steps Computed** | `6,124,693,980` (6.12 Billion) | Total stopping time iterations across the run |
| **Average Steps / Trajectory**| **19.23** | Actual average steps computed per executed trajectory |

### Hardware Performance Counter Profile (perf stat -d)

| Hardware Counter | Count / Measurement | Analysis / Frequency |
| :--- | :--- | :--- |
| **CPU Cycles** | `87,642,185,114` | **4.9 GHz** cycles frequency |
| **Instructions Retired** | `236,864,082,693` | **2.70 Instructions Per Cycle (IPC)** |
| **Branches Executed** | `54,894,471,788` | 3,092.4 Million branches / second |
| **Branch Misses** | `1,588,484,182` | **2.90% branch-miss rate** (97.1% accuracy) |
| **L1 Data Cache Misses** | `31,923,597` | **0.10% L1-dcache miss rate** |
| **Frontend Stalled Cycles** | `25,429,661,737` | 29.00% frontend cycles idle |
| **Context Switches** | `108` | 6.1 switches / second |
| **CPU Utilization** | **1.0 CPUs (100% of 1 core)** | Sequential block-level execution |

---

## 2. Microarchitectural Analysis & Insights

### 1. Exceptional Pipeline Efficiency (IPC = 2.70)
An Instructions Per Cycle (IPC) of **2.70** is extremely high. By using a warm checkpoint:
* Trajectories are terminated almost immediately after transitioning below $2^{32}$.
* The processor spends less time in long, dependency-chained loop iterations and more time executing tight, highly parallelized transition checks.
* The instruction pipeline suffers very few dependency stalls, allowing the superscalar out-of-order execution engine to retire 2.7 instructions per cycle.

### 2. Low Branch Misprediction Overhead (2.90% Miss Rate)
Despite the chaotic nature of the Collatz trajectories, only **2.90%** of the 54.9 Billion branches were mispredicted. This is due to:
* Complete removal of the hot-path modulo 6 division-check (`n % 6 == 5`) via the Base-Dependent Suffix List Optimization.
* Highly structured conditional exits inside the search controller.

### 3. Near-Perfect L1 Cache Locality (99.9% Hits)
The L1 data cache load miss rate is **0.10%**. All active memory structures (the 256-entry `steps_table` and base-dependent allowed lists) fit completely within the L1 data cache, avoiding high-latency DRAM fetches.

### 4. Instruction Count Per Step (38.67 instructions/step)
Dividing instructions retired by total steps computed yields:
$$\frac{236.864 \times 10^9 \text{ instructions}}{6.125 \times 10^9 \text{ steps}} = 38.67 \text{ instructions per step}$$
* This is higher than a cold start (which averages `4.16` instructions/step) because the total steps computed is extremely small (6.12B instead of 102.3B).
* Each of the 318.4 Million trajectories carries a fixed instruction setup/tear-down overhead (setup, loop validation, 128-bit checks, and transition logic). Since this overhead is distributed over only 19.23 steps per trajectory on average, the instructions-per-step ratio increases. However, the total instruction footprint is reduced by **44.3%** (from 425.7B down to 236.9B instructions).

---

## 3. Pruning Dynamics: Cold Start vs. Warm Start

The steps-pruning check evaluates:
```cpp
if (stats.steps + 1050 < current_max_steps) {
    return stats;
}
```

By comparing our cold start baseline (no checkpoint loaded) with this warm start baseline (checkpoint loaded), we can quantify the impact of steps pruning:

| Parameter | Cold Start (`--no-checkpoint`) | Warm Start (Loaded Checkpoint) | Performance Impact |
| :--- | :--- | :--- | :--- |
| **Initial Max Steps Peak** | `0` steps | **`1549` steps** | Inherited from `hailstone.chk` |
| **Total Steps Computed** | `102,303,724,963` | **`6,124,693,980`** | **16.7x Reduction (-94.0%)** |
| **Average Steps / Trajectory**| `321.29` | **`19.23`** | Trajectories are aborted early |
| **Execution Time** | `39.61` seconds | **`17.32` seconds** | **2.3x Speedup (+128.7%)** |
| **Throughput (Reported)** | `12.06 M/s` | **`28.15 M/s`** | Measured search rate increases |

* Under a **Cold Start**, the steps peak begins at 0 and slowly escalates to 1221. Pruning is inactive for the first portion of the block and only becomes active once the local peak exceeds 1050 (which happened at $n = 4,398,176,664,057$, or 4.1% into the search space).
* Under a **Warm Start**, the program initializes `current_max_steps` to **1549** from step one. The check `stats.steps + 1050 < 1549` simplifies to `stats.steps < 499`. Because almost all trajectories drop below $2^{32}$ in fewer than 499 steps, they are pruned immediately upon transition, saving 96.1 Billion step calculations.

---

## 4. OpenMP Thread Scaling and Block Allocation

During this run, the program diagnostic reported:
`[OpenMP Diagnostic] Max Threads: 32` / `Active Threads in parallel region: 32`
However, the hardware profile showed only **1.0 CPUs** utilized (100% of a single core). 

### Rationale
The CPU backend parallelizes work at the **block level** rather than dividing a single block among threads:
```cpp
#pragma omp parallel for schedule(static, 1) num_threads(num_allocated_blocks)
for (int i = 0; i < num_allocated_blocks; ++i) {
    cpu_search_range(block_starts[i], block_ends[i], ...);
}
```
* Each block represents $2^{32}$ values.
* Since the search was launched with `--num-blocks 1`, the variable `num_allocated_blocks` was evaluated as `1`.
* Consequently, OpenMP allocated exactly **1 thread** to process the block sequentially.
* *To utilize all 32 threads, the search must be run over 32 or more blocks (e.g. `--num-blocks 32`).*

---

## 5. SIMD Vectorization: AVX-512 Vectorized Search

To accelerate the CPU backend search, a vectorized search path was implemented using x86 AVX-512 SIMD intrinsics. This path is compiled into a separate translation unit with compiler flags `-mavx512f -mavx512cd -mavx512dq` and is dynamically executed at runtime if the host CPU is detected to support these capabilities.

### Vector Search Telemetry (Block 1024 Warm Start)

Executing the exact same warm start Block 1024 search yields a direct comparison between the scalar and AVX-512 vectorized search paths:

| Search Mode | Elapsed Time | Throughput | Total Steps Computed | Speedup | Peak Parity |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Scalar Reference** | **16.36 seconds** | **29.19 M/s** | `6,124,693,398` | Baseline (1.0x) | Identical peaks |
| **AVX-512 Vectorized** | **7.74 seconds** | **61.67 M/s** | `6,124,693,398` | **2.11x Speedup** | Identical peaks |

### Microarchitectural Insights

1. **Exact Step Equivalence**: Through careful alignment of the steps-pruning checks to the start of the AVX-512 loop iteration (prior to applying the $3x+1$ step), the AVX-512 path achieves bit-perfect equivalence in steps count and peak output down to the single unit.
2. **Lane Compaction and Refilling**: Utilizing AVX-512 register masking, completed trajectories are compressed and inactive lanes are continuously refilled with new candidates from the suffix list. This preserves 100% active lane occupancy throughout the bulk of the prefix blocks.
3. **Instruction Overhead Reduction**: By packing 8 lanes into a single 512-bit ZMM register, vector operations like $3x+1$ and variable right shifts are performed on all 8 lanes in parallel. Combined with the `lzcnt` instruction trick (`63 - lzcnt(x & -x)`), trailing zero counting is executed entirely in parallel, yielding a **+111.3% throughput gain**.
