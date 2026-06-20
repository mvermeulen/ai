# Advanced Algorithmic Optimizations for Collatz Conjecture Search

This report analyzes computational advancements in Collatz (3x+1) search, drawing on recent academic papers, open-source repositories, and community discussions. It details how these optimizations map to our existing `hailstone` codebase and outlines a pathway to integrate them.

---

## 1. Executive Summary

Exhaustive verification of the Collatz conjecture requires processing astronomical ranges of integers. Traditional brute-force algorithms are bottlenecked by:
1. **Multi-Precision Arithmetic:** Especially 128-bit or higher multiplication and division in the hot loop.
2. **Memory Bandwidth & Cache Latency:** Space-time tradeoff tables that grow exponentially with bit-width, causing cache misses.

We analyze three key optimization paradigms to resolve these bottlenecks:
1. **Domain-Switching Arithmetic (David Barina):** Switching calculations to the $n+1$ domain when $n$ is odd, allowing multiple iterations to be compressed into a single step using the count trailing zeros (`ctz`) instruction and a small $O(1)$ table of powers of three.
2. **Modulo-9 Sieve & Suffix Pruning:** Conditionally filtering out residue classes based on their base-prefix modulo 9.
3. **Bitwise Shift-Add Logic (Wei Ren):** Framing $3x+1$ as $(x \ll 1) + x + 1$ to verify extremely large numbers (e.g., $2^{100000}-1$) without multiplication.

---

## 2. Key Improvements from Literature

### David Barina's $2^{71}$ Verification (Domain-Switching)
* **Sources:** [Springer Article (10.1007/s11227-025-07337-0)](https://link.springer.com/article/10.1007/s11227-025-07337-0), [xbarin02/collatz Github](https://github.com/xbarin02/collatz)
* **Methodology:** When $n$ is odd, $n+1$ is even. By analyzing the parity steps under the transformation $y = n+1$, Barina proved that if $n+1 = 2^\alpha \cdot k$ (where $k$ is odd), the trajectory after $\alpha$ odd-even steps reaches:
  $$n_{new} = 3^\alpha \cdot k - 1$$
* **Optimization:** Using the hardware `ctz` instruction, we find $\alpha$ in 1 clock cycle. We shift $n+1$ by $\alpha$, multiply by $3^\alpha$ (from a L1-cached 41-element table), and subtract 1. This compresses $\alpha$ standard Collatz iterations (average of 2 steps) into a single step, reducing 128-bit multiplication and shifting overhead by **~50%**.

### Wei Ren's Mersenne Verification ($2^{100000}-1$)
* **Source:** [ResearchGate Paper (329473945)](https://www.researchgate.net/publication/329473945_Collatz_Conjecture_for_2100000-1_Is_True_-_Algorithms_for_Verifying_Extremely_Large_Numbers)
* **Methodology:** Verified the convergence of $2^{100000}-1$ (approx. 30,000 digits) using bitwise logic. The operation $3x+1$ is written as:
  $$3x+1 = (x \ll 1) + x + 1$$
  This allows arbitrary-precision calculations using single-pass carry-save binary addition, bypassing the need for multi-word multiplication.

---

## 3. Integration with the `hailstone` Codebase

### Recommendation 1: Domain-Switched Arithmetic
We can replace the standard `mul3_add1` and shift logic in our 128-bit loop with Barina's domain-switching logic:

```cpp
// Precomputed powers of 3 lookup table (up to 3^40 fits in uint128)
const uint128 lut3[] = { /* 3^0, 3^1, ..., 3^40 */ };
const uint128 max_safe_n[] = { /* UINT128_MAX / 3^alpha */ };

curr = curr + 1; // Enter n+1 domain
do {
    int alpha = count_trailing_zeros(curr);
    if (alpha >= 41) alpha = 40; // Cap to avoid table overflow
    
    curr >>= alpha;
    if (curr > max_safe_n[alpha]) {
        // Handle overflow using GMP
    }
    curr *= lut3[alpha];
    
} while (!(curr & 1)); // Loop until odd in the n+1 domain
curr = curr - 1; // Exit back to original domain
```

---

### Recommendation 2: Incrementing by 4 & $(3x+1)/4$ Study
David Barina's verification loop skips checking starting values $n \equiv 1 \pmod 4$ by incrementing by 4 (`curr += 4`), which restricts checks to $n \equiv 3 \pmod 4$.

* **Correspondence to our Codebase:** This optimization is functionally identical to the prefix exclusion studied in our [Path Patterns of the Form $(3x+1)/4$](file:///home/mev/source/ai/hailstone/doc/3x_plus_1_over_4_path_investigation.md) report, where we benchmarked the `EXCLUDE_01_SUFFIX` flag. 
* **Conclusion:** Our study demonstrated a **29% to 35% search speedup** by pruning the `01` suffix. Barina's sequential loop enforces this a priori by only checking the $4k+3$ residue class.

---

### Recommendation 3: Modulo-9 Sieve and Suffix Pruning Interaction
David Barina's implementation applies a Modulo-9 sieve to prune starting values $n \equiv \{2, 4, 5, 8\} \pmod 9$ (for odd numbers, this additionally filters $n \equiv 4 \pmod 9$, since $\{2, 5, 8\} \pmod 9$ are already pruned by the Modulo-3 check).

* **Interaction with Suffix Pruning:** We currently implement base-dependent suffix pruning by classifying the prefix $X \cdot 2^w$ mod 6. Because $2^w \equiv 4 \pmod 6$, the residue is determined by $X \pmod 3$, dividing our suffix lists into three tables (`allowed_0`, `allowed_2`, and `allowed_4` suffixes, see [Suffix Residue Class Modulo 6 Study](file:///home/mev/source/ai/hailstone/doc/residue_class_mod6_distribution_study.md)).
* **Tripling the Tables:** Extending this base-dependent pruning to Modulo-9 requires classifying $X \cdot 2^w \pmod 9$. Since $2^6 \equiv 1 \pmod 9$, the residue depends on $X \pmod 9$ for a given width $w$ (where $w$ is a multiple of 2). 
  - This increases the number of distinct precomputed suffix lists from **3 to 9**.
  - While this triples the tables, it enables further pruning of the $4 \pmod 9$ residue class from the search space.
* **Coalescing Opportunities:** In GPU compute shaders (Vulkan/HIP) and vector units (AVX-512), grouping threads into nine distinct dispatches based on $X \pmod 9$ rather than three can improve memory coalescing and minimize warp divergence by eliminating more inactive lanes before execution.

---

## 4. Active Investigation: Peak Metrics under Domain Switching

Because our codebase does not simply verify convergence but searches for three specific peak statistics (Max Value, Steps, and Stopping Time $\sigma$), we must analyze how Barina's domain-switching shortcut affects these metrics:

### 1. Total Steps Count
During a domain-switched jump $n \to n_{new} = 3^\alpha \cdot k - 1$ (where $n+1 = 2^\alpha \cdot k$):
* The jump performs exactly $\alpha$ odd steps ($3x+1$) and $\alpha$ divisions by 2. This accounts for $2\alpha$ steps.
* The subtraction and division by $2^\beta$ at the end of the segment accounts for $\beta$ steps.
* **Result:** We can track the exact step count by accumulating:
  $$\Delta \text{steps} = 2\alpha + \beta$$
  This allows $O(1)$ step updates without executing the intermediate iterations.

### 2. Maximum Value (Max Value Peak)
During the jump, we skip intermediate trajectory values. We must guarantee we do not miss a new Max Value peak.
Let $n_i$ be the odd values during the jump. Since:
$$n_i + 1 = 1.5^i (n_0 + 1)$$
The sequence $n_i$ is strictly increasing. The peak value reached during the jump is always the intermediate value right before the final division by 2, which is:
$$\text{Peak} = 2 \cdot (3^\alpha \cdot k) - 2$$
Since $m = 3^\alpha \cdot k$ is computed directly as `(n >> alpha) * 3^alpha`, we can calculate the peak of the entire segment in $O(1)$ time:
$$\text{Peak} = 2m - 2$$
This value can be directly compared with the global max value peak, preserving peak-search correctness.

### 3. Stopping Time ($\sigma$)
The stopping time $\sigma$ is the step count where the trajectory first drops below the starting value $n_0$.
* Since $n_i$ is strictly increasing during the jump, the trajectory can **never** fall below $n_0$ during the intermediate steps.
* It can only drop below $n_0$ during the final division by $2^\beta$.
* We find the first division step $j \in [1, \beta]$ where the value falls below $n_0$:
  $$\frac{m-1}{2^j} < n_0 \implies 2^j > \frac{m-1}{n_0}$$
* Let $R = \frac{m-1}{n_0}$. The smallest integer $j$ satisfying $2^j > R$ is:
  $$j = \lfloor \log_2(R) \rfloor + 1$$
* Using the hardware count leading zeros (`clz`) instruction:
  $$j = (128 - \text{clz}(R))$$
* **Result:** If $j \le \beta$, the stopping time is reached during this segment at step:
  $$\sigma = \text{current\_steps} + 2\alpha + j$$
  If $j > \beta$, the trajectory does not drop below $n_0$ in this segment, and we continue. This allows us to track the stopping time peak exactly in $O(1)$ time.
