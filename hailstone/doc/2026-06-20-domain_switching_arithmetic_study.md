# Technical Study: Domain-Switching Arithmetic (David Bařina) in CPU Backend

This study investigates the mathematical foundation, performance safety boundaries, and implementation strategy for integrating **Domain-Switching Arithmetic** (proposed by David Bařina) into the `hailstone` CPU search backend.

---

## 1. Executive Summary
* **Goal:** Evaluate the performance and correctness of domain-switching arithmetic on the CPU reference backend (without AVX or GPU changes first).
* **Core Paradigm:** When $n$ is odd, $y = n+1$ is even. By analyzing sequence steps in the $n+1$ domain, we can skip $\alpha = \text{ctz}(n+1)$ odd-even step pairs using a precomputed table of powers of three:
  $$n_{new} = 3^\alpha \cdot \frac{n+1}{2^\alpha} - 1$$
* **Key Constraints:**
  1. **Correctness & Peak Preservation:** Must maintain mathematical correctness for steps, max value, and stopping time ($\sigma$) peaks.
  2. **Option Toggle:** Must support both the original algorithm and the new domain-switching algorithm via a runtime flag.
  3. **Overflow Handling:** Intermediate values must not overflow 128-bits. We must implement pre-checks using a precomputed safe threshold table.
  4. **Low-Level Speedups:** Implement a branchless, division-free method to compute $\sigma$ and find $\alpha$.

---

## 2. Mathematical Formulation & Record Preservation

### Trajectory Jumps
For any odd starting number $n$, let $y = n + 1$ and $y = 2^\alpha \cdot k$ where $k$ is odd and $\alpha \ge 1$.
The domain-switched shortcut maps:
$$n \to n_{new} = 3^\alpha \cdot k - 1$$
This single jump represents exactly $\alpha$ odd steps and $\alpha$ division-by-2 steps in the standard Collatz trajectory.
Following this jump, $n_{new}$ is even. In the next step, we perform division by $2^\beta$ where $\beta = \text{ctz}(n_{new})$ to return to the next odd integer.

### 1. Step Count Updating
Each jump represents exactly $\alpha$ odd steps ($3x+1$) and $\alpha$ divisions by 2. The subsequent divisions by $2^\beta$ represent $\beta$ steps.
Thus, the total steps accumulated is:
$$\Delta \text{steps} = 2\alpha + \beta$$
This allows exact $O(1)$ step updates.

### 2. Maximum Value (Max Value Peak)
During the jump, the sequence of odd numbers $n_i$ satisfies:
$$n_i + 1 = 1.5^i (n_0 + 1)$$
Since $1.5 > 1$, $n_i$ is strictly increasing. The peak value reached is always right before the final division by 2, corresponding to:
$$\text{Peak} = 2 \cdot (3^\alpha \cdot k) - 2 = 2m - 2$$
where $m = 3^\alpha \cdot k$. This allows exact $O(1)$ peak updates.

### 3. Stopping Time ($\sigma$ Peak)
Since $n_i$ is strictly increasing, the trajectory can never drop below the starting value $n_0$ during intermediate steps. It can only drop below $n_0$ during the final division by $2^\beta$.
We want to find the first division step $j \in [1, \beta]$ where:
$$\frac{m-1}{2^j} < n_0 \implies 2^j > \frac{m-1}{n_0}$$
Let $R = \frac{m-1}{n_0}$. The smallest $j$ satisfying $2^j > R$ is:
$$j = \lfloor \log_2(R) \rfloor + 1$$
We can find $j$ in $O(1)$ time without division:
* Let $L_m = 128 - \text{clz}(m - 1)$ be the bit length of $m-1$.
* Let $L_n = 128 - \text{clz}(n_0)$ be the bit length of $n_0$.
* The estimate $j_{est} = L_m - L_n$.
* The true $j$ is either $j_{est}$ or $j_{est} + 1$.
* We test the condition using a division-free right shift:
  $$\text{if } (n_0 > (m - 1) \gg j_{est}) \implies j = j_{est} \text{ else } j = j_{est} + 1$$
* If $j \le \beta$, the stopping time is reached during this segment at step:
  $$\sigma = \text{current\_steps} + 2\alpha + j$$

---

## 3. Performance & Safety Optimizations

### 1. Branchless Capping of $\alpha$
To avoid table overflow, we cap $\alpha$ to a maximum value `MAX_ALPHA` (e.g. 40). We can perform this check in a single branchless operation using a bitwise OR mask on the low 64 bits:
```cpp
// If MAX_ALPHA is 40, set bit 40 in curr.low. 
// This guarantees that count_trailing_zeros will return at most 40.
int alpha = ctz64(curr.low | (1ULL << 40));
```
Because the input $curr$ is even, $curr.low$ has at least one trailing zero, meaning `alpha` is in $[1, 40]$. This avoids reading `curr.high` and avoids branch logic.

### 2. Overflow Protection & Hybrid Delegation
As starting numbers approach $2^{64}$, intermediate steps in the Collatz trajectory can exceed the $2^{64}$ boundary. We handle this across both backends to maintain correctness and peak efficiency:

* **Scalar CPU Backend (128-bit):**
  Since $m = (curr \gg \alpha) \cdot 3^\alpha$, we must guarantee $3^\alpha \cdot k$ does not overflow $2^{128}-1$. Let $k = curr \gg \alpha$. Before multiplying, we check:
  ```cpp
  if (k > max_safe_k[alpha]) {
      overflow = true;
      return;
  }
  ```
  where `max_safe_k[alpha]` is the precomputed threshold $\lfloor (2^{128}-1) / 3^\alpha \rfloor$.

* **AVX-512 Vectorized Backend (64-bit registers):**
  Since AVX-512 processes 8 parallel lanes using 64-bit integers for inputs $< 2^{64}$, we check if $k \cdot 3^\alpha > 2^{64}-1$ using a vector gather of a 64-bit precomputed threshold table `max_safe_k_64`:
  ```cpp
  __mmask8 overflow_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_k, v_max_safe_k, _MM_CMPINT_GT);
  ```
  If any lane triggers `overflow_mask != 0`, we temporarily dump the vector registers back to stack-allocated arrays, extract the overflowing lane, and delegate its trajectory to the 128-bit scalar pathway (`compute_collatz_poly`). The remaining active lanes continue executing in the vector registers.

This hybrid delegation model ensures that rare overflow cases near the $2^{64}$ boundary are handled with absolute correctness (by falling back to 128-bit arithmetic) without incurring any performance penalty on the common-case lanes.

### 3. LUT Sizing & Precision
If `MAX_ALPHA = 40`, the table of powers of three fits in `uint64_t` since $3^{40} < 2^{64}$.
This means the multiplication `(curr >> alpha) * lut3[alpha]` is a 128-bit by 64-bit multiplication rather than a full 128-bit by 128-bit multiplication, yielding a significant speedup.

---

## 4. Implementation Plan for the CPU Reference Backend

1. **Extend [uint128.h](file:///home/mev/source/ai/hailstone/include/uint128.h):**
   * Implement `count_leading_zeros(uint128)` using `__builtin_clzll` on `high` and `low`.
   
2. **Add CLI option to [main.cpp](file:///home/mev/source/ai/hailstone/cpu/main.cpp):**
   * Add `--domain-switching` and `--no-domain-switching` flags (with a backing boolean variable `use_domain_switching`).
   
3. **Duplicate search loops in [cpu_search.cpp](file:///home/mev/source/ai/hailstone/cpu/cpu_search.cpp):**
   * Keep standard `compute_collatz` and `compute_collatz_poly` as `compute_collatz_std` and `compute_collatz_poly_std`.
   * Implement `compute_collatz_domain` and `compute_collatz_poly_domain` using Bařina's algorithm.
   * Based on the `use_domain_switching` flag, invoke the appropriate backend helper.

4. **Verify correctness:**
   * Run unit tests and `./hailstone_verify` to guarantee results are identical under both pathways.
   * **Differential Trajectory Fuzzing:** Implement a fuzzing loop in `test_uint128.cpp` testing $10^6$ random odd starting values (across both 64-bit and 128-bit ranges) and asserting that standard vs. domain-switching trajectory functions return identical steps, max value, stopping time, and overflow status.
   * **Boundary Checking:** Explicitly verify trajectories for boundary inputs such as $2^k$, $2^k-1$, and values right at the overflow thresholds.
   * **Instruction & Cache Profiling:** Profile CPU instruction reduction using `perf stat -e instructions,cycles,L1-dcache-load-misses,branches,branch-misses` over both algorithms to measure structural efficiency.

---

## 5. Benchmark & Performance Results

### Scalar CPU Reference Backend (32 Threads)
To evaluate the runtime efficiency of Bařina's domain-switching algorithm in scalar mode, we ran CPU reference searches (32 threads) with AVX-512 forced off (`--no-avx512`) over three distinct scale ranges (10M starting numbers checked per run).

| Range / Scale | Block Index | Standard CPU Throughput | Domain-Switching Throughput | Delta (Domain-Switching vs Standard) |
| :--- | :--- | :--- | :--- | :--- |
| **Low Range** ($[3, 10^7]$) | Block 0 | **15.68 M/s** | **15.27 M/s** | $-2.6\%$ (Slight Overhead) |
| **High Range** ($[2^{42}, 2^{42} + 10^7]$) | Block 1024 | **10.54 M/s** | **10.36 M/s** | $-1.7\%$ (Parity) |
| **Very High Range** ($[2^{52}, 2^{52} + 10^7]$) | Block 1,000,000 | **5.73 M/s** | **6.73 M/s** | **$+17.5\%$ (Significant Speedup)** |

### AVX-512 Vectorized CPU Backend (32 Threads)
We extended the domain-switching algorithm to the 512-bit vector registers of the AVX-512 backend. The vectorized implementation handles 8 parallel lanes using 64-bit integer lanes for values below $2^{64}$. The benchmark was executed over three different block ranges, processing one complete block ($2^{32}$ elements, filtered down by modulo restrictions) per run:

| Range / Scale | Block Index | Standard AVX-512 Throughput | Domain-Switching AVX-512 Throughput | Delta (Domain-Switching vs Standard) |
| :--- | :--- | :--- | :--- | :--- |
| **Low Range** ($[2^{32}, 2^{33}]$) | Block 1 | **13.07 M/s** | **9.93 M/s** | $-24.0\%$ (Overhead Dominates) |
| **High Range** ($[2^{42}, 2^{42} + 2^{32}]$) | Block 1024 | **8.15 M/s** | **8.36 M/s** | **$+2.6\%$ (Parity / Crossover)** |
| **Very High Range** ($[2^{48}, 2^{48} + 2^{32}]$) | Block 100,000 | **5.73 M/s** | **7.54 M/s** | **$+31.6\%$ (Significant Speedup)** |

### Core Analysis & Takeaways

1. **Crossover Point & Scaling:**
   * **Scalar Crossover:** Occurs around Block 1024 ($2^{42}$). Below this point, standard arithmetic is slightly faster due to lower scalar overhead. Above it, domain-switching's iteration reduction dominates, yielding $+17.5\%$ speedup by Block 1,000,000 ($2^{52}$).
   * **AVX-512 Crossover:** Also occurs around Block 1024 ($2^{42}$). Because the vectorized domain-switching loop does more complex vector operations (such as vector gathers for $3^\alpha$ and $max\_safe\_k$, vector multiplication, and vector stopping time tracking) per step, the overhead is higher than in the scalar case, leading to a $-24\%$ reduction in throughput at Block 1.
   * However, as the range increases, the average trajectory steps increase (e.g. 199.27 steps at Block 100,000 vs 142.10 steps at Block 1). Trajectories stay in the 128-bit vector loop longer, where domain-switching jumps skip multiple steps per iteration. This results in a massive **$+31.6\%$ throughput improvement** at Block 100,000, scaling even better than the scalar backend.

2. **Correctness & Stability:**
   * Both pathways produced identical step counts, peak values, stopping times ($\sigma$), and prediction signals across all ranges.
   * Suffix-first alignment tests across bit-widths of 8, 12, 16, and 20 verified identical peak discovery, checkpoint compatibility, and cross-backend interoperability.
   * Trajectory differential fuzzing over 1,000,000 random starts verified zero mathematical discrepancy between the standard CPU and domain-switching backends.

---

## 6. Distributed Search Integration

We integrated domain-switching arithmetic as a fourth distinct backend choice (`cpu_domain`) in the `hailstone` distributed cluster system:

1. **Worker Daemon Optimization:**
   * Updated C++ (`hailstoned.cpp`) and Python (`daemon.py`) worker daemons to benchmark `cpu` and `cpu_domain` independently on startup.
   * Configured the task execution mapper to route the `cpu_domain` backend option to the `hailstone_cpu` executable with the `--domain-switching` argument, while `cpu` uses `--no-domain-switching`.

2. **Central Scheduler & Web UI:**
   * Configured the central scheduler (`controller.py`) to recognize and track progress/throughput using the new `cpu_domain` option.
   * Updated the Web UI dashboard to include "CPU Domain-Switching" in the configuration dropdown, displaying it as `CPU (DS)` in worker nodes' capability stats.
   * Verified complete integration with cluster range partitioning, checkpoint merging, and automated failover.
