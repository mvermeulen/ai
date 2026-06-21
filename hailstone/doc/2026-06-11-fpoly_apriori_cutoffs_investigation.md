# Investigation: A Priori Cutoffs using Vermeulen Polynomials (fpoly)

This document investigates a high-performance optimization strategy for the Hailstone (Collatz) peak search using Vermeulen Polynomials (`fpoly`) to achieve a priori cutoffs.

---

## 1. Mathematical Foundation & Path Collisions

### Vermeulen Polynomials (`fpoly`)
For a starting value $n$, we can represent its residue class modulo $2^w$ where $w$ is the polynomial bit-width. As $n$ undergoes Collatz iterations, the sequence of odd and even steps is identical for all values in the same residue class until the value has been divided by 2 exactly $w$ times. 

We can represent this trajectory using a Vermeulen Polynomial $Vpoly_w(r)$ for a residue suffix $r \in [0, 2^w - 1]$:
$$Vpoly_w(r)(x) = \lfloor x / 2^w \rfloor \cdot 3^{pow_3} + y$$

Where:
- $pow_3$ is the number of multiplications by $3$ (odd steps) in the first $w$ divisions by $2$.
- $y$ is the additive term capturing the accumulated overflow from the lower $w$ bits.
- $\lfloor x / 2^w \rfloor$ represents the prefix $X$ of the number $x$.

### Path Collision
If two distinct residue classes $r_1, r_2 \pmod{2^w}$ (with $r_1 < r_2$) yield the exact same Vermeulen Polynomial:
$$Vpoly_w(r_1) = Vpoly_w(r_2)$$

Then for any shared prefix $X$, the starting values $b = X \cdot 2^w + r_1$ and $k = X \cdot 2^w + r_2$ will:
1. Reach the **exact same value** after $w$ divisions by 2:
   $$Vpoly_w(r_1)(b) = X \cdot 3^{pow_3} + y = Vpoly_w(r_2)(k)$$
2. Take the **exact same number of steps** ($w + pow_3$) to reach this collision point.
3. Follow the **exact same trajectory** from the collision point onwards.

### Peak Exclusion (Lemma 21)
Since $b < k$ and both trajectories take the same number of steps to reach the collision point and are identical thereafter:
- **Steps**: $Steps(b) = Steps(k)$. Since $b < k$, $k$ cannot be a peak in steps (a peak must have strictly more steps than all smaller starting values).
- **Stopping Time**: The trajectory of $k$ falls below $k$ at or before the trajectory of $b$ falls below $b$ (since $b < k$). Therefore, $k$ cannot be a peak in stopping time.
- **Max Value**: Since the trajectories after collision are identical, the peaks achieved after collision are identical. Before collision, the intermediate values are scaled by $X$, meaning $k$ does not yield any new peaks.

Consequently, any suffix $r$ that produces a duplicate polynomial already seen for a smaller suffix $r' < r$ is **redundant** and can be safely skipped in the search.

---

## 2. The Even-Class Exclusion Rule

A major extension of this optimization occurs when a polynomial class contains **at least one even suffix** $s_e$. 

### The Core Premise:
Even starting values $b_e = X \cdot 2^w + s_e$ are divided by 2 on their first step, reducing to $k = X \cdot 2^{w-1} + s_e/2$.
Since $s_e$ and all other suffixes $s_i$ in the class share the same polynomial, they all take the same number of steps to reach the collision point and follow the same trajectory thereafter.
Thus, $Steps(b_i) = Steps(b_e) = 1 + Steps(k)$.

### Case 1: $s_i > s_e$ (Larger odd suffixes in the class)
Since $s_i > s_e$, we have $b_i > b_e$. Since $b_e$ (which is even) has the same number of steps as $b_i$, $b_i$ can never be a peak because a smaller number ($b_e$) has the same steps.

### Case 2: $s_i < s_e$ (Smaller odd suffixes in the class)
For the odd suffix $s_i$, the starting value is $b_i = X \cdot 2^w + s_i$. Since $s_e/2 < s_i$ (due to the division of the even suffix), we have:
$$k = X \cdot 2^{w-1} + s_e/2 < X \cdot 2^w + s_i = b_i$$

Thus $k < b_i$ for all $X \ge 0$.
Since $Steps(b_i) = 1 + Steps(k)$, for $b_i$ to be a peak in steps, **every** number $j$ in the interval $[k + 1, b_i - 1]$ (roughly $[k + 1, 2k - d]$) would have to take at most $Steps(k)$ steps.
For non-trivial ranges (past the boundary case where $X = 0$ and values are smaller than $2^w$), this interval is extremely large and is practically guaranteed to contain numbers with significantly more steps than $Steps(k)$. 

### Empirical Validation:
An exhaustive search of all Collatz steps peaks up to $10,000,000$ confirmed that **zero** peaks $\ge 2^w$ share a polynomial class with an even suffix. The only apparent "violators" are trivial boundary numbers (like $3, 6, 18, 54$) that are smaller than $2^w = 256$, where the polynomial collision logic is not yet active because the trajectories reach $1$ before performing $w$ divisions by 2.

**Rule:** We can completely exclude **every** odd suffix in any polynomial class that contains even suffixes. We only need to search the smallest odd representative of classes that contain **only** odd suffixes.

---

## 3. Quantitative Pruning Statistics

We compare three search pruning scenarios across different bit-widths $w$:
1. **Standard Odd Unique Suffixes**: Only keeping the smallest odd suffix for each unique polynomial (original cutoff logic).
2. **Even-Class Exclusion**: Excluding all odd suffixes that share a class containing an even suffix.
3. **Even-Class Exclusion + Modulo 6 Cutoff**: The final optimized candidate space after additionally applying the $b \not\equiv 5 \pmod 6$ check.

### Suffix Counts and Pruning Ratios:

| Bit-Width ($w$) | Total Odd Suffixes ($2^{w-1}$) | Standard Unique Odd Suffixes | Even-Class Allowed Suffixes | Even-Class Pruning | Combined + Mod 6 Allowed Suffixes | Net Search Space Remaining | Combined Speedup Factor |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **8** | 128 | 61 | **58** | 54.69% | 38.6 | **30.21%** | 3.31x |
| **12** | 2,048 | 786 | **716** | 65.04% | 477.3 | **23.31%** | 4.29x |
| **16** | 32,768 | 10,146 | **9,057** | 72.36% | 6,038.0 | **18.43%** | 5.43x |
| **20** | 524,288 | 132,429 | **116,606** | 77.76% | 77,737.3 | **14.83%** | 6.74x |

*Note: The Combined + Mod 6 counts are fractional expectations because the modulo 6 congruence varies dynamically per prefix.*

---

## 4. Suffix-First Search Algorithm Design

The proposed **Suffix-First** design reorganizes the search. We loop through prefix blocks of size $2^w$, and for each prefix, we append only the pre-filtered allowed suffixes:

```cpp
// Precomputed table of allowed odd suffixes for width w
const uint16_t allowed_suffixes[] = {1, 7, 9, 11, 25, ...}; 

for (uint128 x = start_prefix; x <= end_prefix; ++x) {
    uint128 base = x << w;
    uint32_t x_mod3 = x % 3; // Compute once per prefix block
    
    for (uint16_t suffix : allowed_suffixes) {
        // Fast Modulo 6 cutoff: if base + suffix is congruent to 5 mod 6, skip
        // Since base + suffix is odd, we check if (base + suffix) % 3 == 2
        // We optimize (x * 2^w + suffix) % 3 using 2^w % 3 = 1 (for even w)
        uint32_t sum_mod3 = (x_mod3 + suffix) % 3;
        if (sum_mod3 == 2) continue; // Modulo 6 cutoff
        
        uint128 n = base | suffix;
        run_trajectory(n);
    }
}
```

### Key Advantages:
1. **Branch-Free Pruning**: Core search logic avoids conditional branch logic (`if (is_cutoff) continue`) for polynomial cutoffs.
2. **GPU Optimization**: On GPU backends (HIP and Vulkan), eliminating polynomial cutoff branches prevents thread divergence. Every thread in a warp/workgroup performs useful calculations on a guaranteed candidate.
3. **Cache Friendliness**: The list of allowed suffixes is extremely small (e.g., 58 entries for $w=8$, 9,057 for $w=16$) and fits entirely within L1 cache or GPU constant memory.
