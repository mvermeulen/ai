# Investigation: 64-bit Loop Transition Optimization (CPU & GPU)

This document details the design, implementation, and empirical evaluation of the **64-bit Loop Transition Optimization** on the CPU backend, and outlines the proposal for evaluating this optimization on the GPU backends (HIP and Vulkan).

---

## 1. Optimization Concept & Algorithm Design

When searching Collatz trajectories using 128-bit multi-precision data types (`uint128`), the arithmetic operations (addition, multiplication, and shifts) are relatively expensive because they must be emulated by the compiler (on the CPU) or using custom structures (on the GPU).

However, during a search range starting at Block index $\ge 1$ (values $\ge 2^{32}$), we observe that once the trajectory value drops below $2^{32}$:
1. The value is guaranteed to have dropped below the starting value `n` (since $n \ge 2^{32}$). Thus, the trajectory has already stopped, and peak tracking (`max_value`) is no longer required (`dropped_below_start = true`).
2. The stopping time has been resolved (`has_stopped_sigma = true`), so we do not need to check `stopping_time`.
3. The remaining Collatz iterations are guaranteed to fit within a standard 64-bit unsigned integer without overflowing.

Based on these observations, we can safely transition the trajectory calculation from 128-bit multi-precision logic to a fast, native 64-bit loop body once the current value satisfies:
$$\text{curr.high} == 0 \quad \land \quad \text{dropped\_below\_start} \quad \land \quad \text{has\_stopped\_sigma}$$

This transitions the remaining steps to native CPU/GPU hardware instructions, bypassing 128-bit emulation overhead.

---

## 2. CPU Implementation & Performance Results

We implemented both the 64-bit loop transition check and the early steps-pruning logic at the top of the main 128-bit loop in `compute_collatz_poly` inside [cpu_search.cpp](file:///home/mev/source/ai/hailstone/cpu/cpu_search.cpp):

```cpp
if (curr.high == 0 && dropped_below_start && has_stopped_sigma) {
    // Steps-pruning early-termination check:
    // If the accumulated steps plus the maximum possible steps for a starting
    // number < 2^32 (which is 1,050) is less than the current global steps peak,
    // this trajectory cannot yield a new steps peak, so we terminate early.
    if (stats.steps + 1050 < current_max_steps) {
        return stats;
    }
    uint64_t curr_64 = curr.low;
    while (curr_64 >= (1 << POLY_WIDTH)) {
        uint64_t next_val = 3 * curr_64 + 1;
        stats.steps++;
        int p = ctz64(next_val);
        curr_64 = next_val >> p;
        stats.steps += p;
    }
    curr = uint128(curr_64, 0);
    break;
}
```

We benchmarked the throughput on the CPU backend (Intel/AMD x86_64 host) at **Block 100** (range: `429496729600` to `429501729600`) over 5,000,000 starting numbers with `--cutoff-width 8`. We evaluated four configurations:
1. **Baseline**: Standard 128-bit search without any 64-bit transition.
2. **64-bit Transition Only**: Transitioning to native 64-bit types upon falling below $2^{32}$, but without steps pruning.
3. **64-bit Transition + Steps Pruning (Cold)**: Running without a warm checkpoint, so the global steps peak starts at 0 and is updated incrementally as new local peaks are found during the search range.
4. **64-bit Transition + Steps Pruning (Warm)**: Running with a warm checkpoint (`hailstone.chk`) that initializes the global steps peak to the correct historical maximum for values checked so far (steps = `1321`).

### CPU Benchmark Results (Block 100, Range: 5,000,000 values)

| Configuration | Throughput | Execution Time | Speedup vs Baseline | Avg. Steps Computed | Total Steps Computed |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **1. Baseline (Unoptimized)** | **6.95 M numbers/s** | 0.1631 s | 1.00x (Ref) | 192.11 | 217.6 M |
| **2. 64-bit Transition Only** | **10.53 M numbers/s** | 0.1076 s | 1.515x | 192.11 | 217.6 M |
| **3. 64-bit Transition + Steps Pruning (Cold)** | **10.67 M numbers/s** | 0.1061 s | 1.535x | 192.11 | 217.6 M |
| **4. 64-bit Transition + Steps Pruning (Warm)** | **32.11 M numbers/s** | 0.0353 s | **4.62x** | **9.50** | **10.8 M** |

### CPU Performance Analysis
*   **ALU Efficiency**: Transitioning to native 64-bit registers lets the CPU execute 64-bit additions and bit-shifts natively in a single clock cycle, instead of emulating 128-bit math (which involves multiple instructions, carries, and memory accesses).
*   **Pruned Loop Body**: Because we only transition after stopping conditions are met, the 64-bit loop body is stripped of all conditional branches related to `max_value` updating and `stopping_time` checking, significantly improving pipeline execution and instruction-level parallelism (ILP).
*   **Steps-Pruning Efficiency**: When the search starts with a warm checkpoint containing a high global steps peak (e.g. `current_max_steps = 1321`), the check `stats.steps + 1050 < 1321` (effectively `stats.steps < 271`) is immediately satisfied by almost all trajectories once they transition below $2^{32}$. This results in a massive **20x reduction** in the total number of steps computed (from 217.6 M down to 10.8 M), raising the average search throughput to **32.11 M numbers/s**—a **3.0x speedup** over 64-bit transition alone and a **4.6x speedup** over the unoptimized baseline.


---

## 3. GPU (HIP) Implementation & Performance Results

We implemented both the 64-bit loop transition and early steps-pruning checks inside the 128-bit Phase 2 loops in [hip_search.hip.cpp](file:///home/mev/source/ai/hailstone/gpu_hip/hip_search.hip.cpp) (for both `collatz_search_kernel` and `collatz_search_kernel_suffix_first`):

```cpp
if (curr.high == 0) {
    if (steps + 1050 < init_max_steps) {
        curr = one;
        break;
    }
    uint64_t curr_64 = curr.low;
    while (curr_64 >= 256) {
        uint32_t r = curr_64 & 255;
        poly p = d_fpoly_table[r];
        uint64_t next_val = (curr_64 >> 8) * p.mul3 + p.add;
        int extra_div = __builtin_ctzll(next_val);
        curr_64 = next_val >> extra_div;
        steps += p.steps + extra_div;
    }
    curr = uint128(curr_64, 0);
    break;
}
```

*Note: In the pruning case, we explicitly set `curr = one` before breaking. This prevents the trailing code block from incorrectly accessing the precomputed `d_steps_table` out-of-bounds since `curr` has not been fully reduced to 1.*

We benchmarked the HIP backend performance at **Block 100** (range: `429496729600` to `429746729600`) over a range of **250,000,000** starting numbers:

### GPU HIP Benchmark Results (Block 100, Range: 250,000,000 values)

| Configuration | Suffix-First Width | Throughput | Execution Time | Speedup vs Baseline | Avg. Steps Computed |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Baseline (Unoptimized)** | Width 8 | **479.50 M numbers/s** | 0.1181 s | 1.00x (Ref) | 189.14 |
| **Transition + Pruning (Cold)** | Width 8 | **838.49 M numbers/s** | 0.0676 s | **1.75x** | 46.93 |
| **Transition + Pruning (Warm)** | Width 8 | **877.20 M numbers/s** | 0.0646 s | **1.83x** | **12.30** |
| | | | | | |
| **Baseline (Unoptimized)** | Width 20 | **365.66 M numbers/s** | 0.0760 s | 1.00x (Ref) | 201.39 |
| **Transition + Pruning (Cold)** | Width 20 | **719.01 M numbers/s** | 0.0387 s | **1.97x** | 65.31 |
| **Transition + Pruning (Warm)** | Width 20 | **702.39 M numbers/s** | 0.0396 s | **1.92x** | **14.71** |

### GPU Performance & Architectural Analysis

1. **Throughput Boost and ALU Efficiency**:
   Transitioning the remainder of the trajectory to native 64-bit registers on the GPU bypasses the complex, multiple-instruction emulation overhead of 128-bit multi-precision arithmetic. This delivers a **1.75x to 1.97x speedup** in overall throughput.
2. **SIMT Warp Divergence Limits**:
   Unlike a CPU where threads execute independently, a GPU executes threads in wavefronts/warps of 32 or 64 threads. A wavefront can only exit early when **all** threads in that wavefront satisfy the pruning check and break out. If even a single thread in the wavefront has a longer trajectory or hasn't pruned, the entire wavefront continues executing. This explains why the warm start (initializing `init_max_steps = 1321`) provides a relatively modest additional speedup over cold start (e.g. 1.83x vs 1.75x for width 8) compared to the 3.0x speedup observed on the CPU.
3. **Kernel Launch and Synchronization Bottlenecks**:
   At throughputs approaching 900 M numbers/s, a 250M range search completes in just ~64 ms. At this timescale, the fixed overhead of kernel launch execution, host-device communication, and shared memory reduction/prefix scan synchronizations represent a significant portion of total execution time, limiting the upper bound of warm-start performance gains.

---

## 4. GPU (Vulkan) Implementation & Performance Results

We implemented both the 64-bit loop transition and early steps-pruning checks in GLSL inside the Phase 2 loop of the Vulkan compute shader [shader.comp](file:///home/mev/source/ai/hailstone/gpu_vulkan/shader.comp):

```glsl
if (curr.high == 0UL) {
    if (steps + 1050 < init_max_steps) {
        curr = one;
        break;
    }
    uint64_t curr_64 = curr.low;
    while (curr_64 >= 256UL) {
        uint r = uint(curr_64 & 255UL);
        poly p = fpoly_table.polys[r];
        uint64_t next_val = (curr_64 >> 8) * uint64_t(p.mul3) + uint64_t(p.add);
        uint extra_div = ctz64(next_val);
        curr_64 = next_val >> extra_div;
        steps += p.steps + extra_div;
    }
    curr.low = curr_64;
    curr.high = 0UL;
    break;
}
```

*Note: Similar to HIP, setting `curr = one` upon early termination prevents any subsequent out-of-bounds reads in the precomputed step lookup table on the GPU.*

We benchmarked the Vulkan backend performance at **Block 100** (range: `429496729600` to `429746729600`) over a range of **250,000,000** starting numbers:

### GPU Vulkan Benchmark Results (Block 100, Range: 250,000,000 values)

| Configuration | Suffix-First Width | Throughput | Kernel Execution Time | Speedup vs Baseline | Avg. Steps Computed |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Baseline (Unoptimized)** | Width 8 | **566.63 M numbers/s** | 99.96 ms | 1.00x (Ref) | 189.14 |
| **Transition + Pruning (Cold)** | Width 8 | **1050.73 M numbers/s** | 53.91 ms | **1.85x** | 46.03 |
| **Transition + Pruning (Warm)** | Width 8 | **1115.47 M numbers/s** | 50.78 ms | **1.97x** | **12.30** |
| | | | | | |
| **Baseline (Unoptimized)** | Width 20 | **473.00 M numbers/s** | 58.78 ms | 1.00x (Ref) | 201.39 |
| **Transition + Pruning (Cold)** | Width 20 | **735.15 M numbers/s** | 37.82 ms | **1.55x** | 64.61 |
| **Transition + Pruning (Warm)** | Width 20 | **852.87 M numbers/s** | 32.60 ms | **1.80x** | **14.71** |

### GPU Vulkan Performance Analysis

1. **Extreme Throughput delta (1.1+ Billion/s)**:
   The Vulkan backend reaches **1115.47 M numbers/s** under the Warm Suffix-First Width 8 configuration. This represents a **1.97x speedup** over the unoptimized Vulkan baseline, which was already highly optimized compared to HIP (likely due to highly efficient SPIR-V code compilation by the RADV driver on Linux).
2. **Impact of Suffix-First Width**:
   - **Width 8**: Bypassing Collatz steps on the GPU reduced the average steps computed per number from **189.14** to **12.30**. The kernel execution time dropped by **50%** (from 99.96 ms to 50.78 ms).
   - **Width 20**: The search space is extremely pruned, checking fewer starting numbers (27.8M instead of 56.6M). The optimized kernel execution time dropped from 58.78 ms to 32.60 ms (**1.80x speedup**), reducing average steps computed to **14.71**.
3. **Warm vs. Cold Speedups on Vulkan**:
   Similar to the HIP backend, Vulkan warm starts show minor speedup improvements over cold starts (e.g. 1115.47 M vs 1050.73 M numbers/s for width 8) because of SIMT warp divergence and the relatively high fraction of time spent on launch/synchronization overhead in extremely short execution windows (~32 ms - 50 ms).


