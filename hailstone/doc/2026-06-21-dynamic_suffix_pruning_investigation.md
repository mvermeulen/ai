# Investigation of Dynamic Suffix Pruning in the Hailstone (Collatz) Search

This document presents the mathematical foundation, empirical partitioning, and architectural design for **Dynamic Suffix Pruning** to accelerate record-breaking steps peak searches.

---

## 1. Overview and Objectives

The primary goal of this investigation is to study trajectory drop factors during suffix evaluation and leverage historical record bounds to dynamically prune candidates before evaluation. Our objectives are to:
- Establish static suffix-level drop bounds based on Vermeulen Polynomials.
- Construct a mathematical framework to prune step candidates using drop counts and power-of-2 peak record steps ($M(2^W)$).
- Design a low-overhead, pre-computed static data structure coupled with a dynamic runtime filtering system.
- Implement a self-optimizing feedback loop that updates allowed search candidates in real-time when new steps records are found.

## 2. Mathematical Foundation of Suffix Drop Pruning

For any candidate value $n$ in the search range $[2^W, 2^{W+1})$, we attempt to show that its total step count cannot exceed the current record $S_{max} = M(2^W)$. 

If the trajectory starting at $n$ drops below $n/2^b$ at step $k$, then the total step count is bounded by:
$$Steps(n) \le k + M(n') \le k + M(2^{W+1-b})$$
where $M(y)$ is the maximum steps seen for starting values $\le y$.

Thus, if:
$$k + M(2^{W+1-b}) \le M(2^W)$$
then $n$ cannot beat the current record and can be safely **pruned**.

At the suffix level, we pre-compute the step count $k$ for different drop factors $2^b$ (for $b \ge 2$). A suffix $s \pmod{2^w}$ is guaranteed to drop below $n/2^b$ at step $j$ (with $d_j \le w$ divisions and $p_j$ odd steps) for all $X \ge 1$ if:
1. **Coefficient Drop**: $2^{d_j - b} > 3^{p_j}$ (the growth coefficient drops below $2^{-b}$)
2. **Boundary Drop**: $x_j(1) < \frac{2^w + s}{2^b}$ (the boundary condition at $X=1$)


## 3. Suffix Partitioning: Steps-Only vs. Stopping Time Suffixes

We analyze the allowed suffixes (those not pruned by the Even-Class Exclusion rule) at different polynomial widths $w$. We partition them into two categories:
1. **Steps-Only Suffixes (Goes Below)**: Suffixes $s$ where the trajectory starting at $X \cdot 2^w + s$ is mathematically guaranteed to fall below its starting value within the first $w$ divisions by 2 (for all $X \ge 1$). Since these trajectories drop below their starting point quickly, they have small stopping times and cannot be record-breaking stopping time peaks. They are only searched because they might lead to record-breaking total steps (via a long tail after dropping below the starting value).
2. **Stopping Time Allowed Suffixes (Does Not Go Below)**: Suffixes $s$ where the trajectory does not fall below its starting value during the first $w$ divisions. These are candidates for both stopping time peaks and steps peaks.

### Mathematical Condition
For a starting value $n = X \cdot 2^w + s$, the value at step $j$ (where $d_j$ is the number of divisions and $p_j$ is the number of odd steps) is:
$$x_j = \frac{3^{p_j} \cdot n + A_j}{2^{d_j}}$$
The trajectory falls below $n$ at step $j$ for all $X \ge 1$ if:
1. $3^{p_j} < 2^{d_j}$ (the growth coefficient is less than 1)
2. $x_j(1) < x_0(1)$ (the boundary value at $X=1$, i.e. $2^w + s$, falls below its starting point)

### Empirical Results across Widths

The table below shows the partition of allowed suffixes across different polynomial widths $w$:

| Width ($w$) | Total Allowed | Steps-Only (Goes Below) | Both (Does Not Go Below) | % Steps-Only (Goes Below) |
|:---:|:---:|:---:|:---:|:---:|
| **4** | 5 | 2 | 3 | 40.00% |
| **6** | 17 | 10 | 7 | 58.82% |
| **8** | 58 | 42 | 16 | 72.41% |
| **10** | 203 | 151 | 52 | 74.38% |
| **12** | 716 | 534 | 182 | 74.58% |
| **14** | 2,542 | 1,949 | 593 | 76.67% |
| **16** | 9,057 | 7,337 | 1,720 | 81.01% |
| **18** | 32,432 | 26,328 | 6,104 | 81.18% |
| **20** | 116,606 | 94,362 | 22,244 | 80.92% |
| **22** | 420,835 | 344,866 | 75,969 | 81.95% |
| **24** | 1,523,909 | 1,289,753 | 234,156 | **84.63%** |

### Key Insights
- **Increasing Dominance**: As the polynomial width $w$ increases, the proportion of suffixes that are "Steps-Only" (go below their starting point) grows significantly, reaching over **84%** at $w = 24$.
- **Modulo 4 Congruence**: As expected, any allowed suffix ending in `01` binary (mod 4 == 1) will go below its starting point very quickly (after 1 odd step and 2 divisions, since $3^1 < 2^2$). These make up a large fraction of the Steps-Only category.
- **Search Space Reduction**: For a search targeting *only* stopping time peaks, we can prune **84.63%** of the remaining allowed suffixes at $w=24$, leaving only **15.37%** of the candidates to be evaluated.

## 4. Multi-Layer Drop Cutoffs for Steps Peaks

This investigation analyzes how far trajectories drop during their suffix evaluation, and how we can use these drop factors to prune candidates from the search for record-breaking **steps peaks**.

### Mathematical Theory
Let $M(2^W)$ represent the maximum steps of any trajectory starting at a value $\le 2^W$.
For a search range $[2^W, 2^{W+1})$ (where we aim to find trajectories with steps exceeding $S_{max} = M(2^W)$):
If a trajectory starting at $n$ drops below $n / 2^b$ at step $k$:
$$Steps(n) \le k + M(2^{W+1-b})$$

Therefore, if:
$$k + M(2^{W+1-b}) \le M(2^W)$$
then $n$ cannot beat the current max steps record and is **pruned**.

At the suffix level, a suffix $s \pmod{2^w}$ is guaranteed to drop below $n / 2^b$ (for all $X \ge 1$) at step $j$ of its trajectory if:
1. $2^{d_j - b} > 3^{p_j}$ (growth coefficient drops below $2^{-b}$)
2. $x_j(1) < \frac{2^w + s}{2^b}$ (boundary condition at $X=1$)

### Max Steps by Power of 2 ($M(2^W)$)
Based on the historical steps peaks, we build the following table of $M(2^W)$ for $W \in [30, 52]$:

| Power ($W$) | Range Upper Bound ($2^W$) | Max Steps ($M(2^W)$) |
|:---:|:---:|:---:|
| **30** | 1,073,741,824 | 986 |
| **31** | 2,147,483,648 | 1,008 |
| **32** | 4,294,967,296 | 1,050 |
| **33** | 8,589,934,592 | 1,131 |
| **34** | 17,179,869,184 | 1,210 |
| **35** | 34,359,738,368 | 1,219 |
| **36** | 68,719,476,736 | 1,220 |
| **37** | 137,438,953,472 | 1,234 |
| **38** | 274,877,906,944 | 1,307 |
| **39** | 549,755,813,888 | 1,321 |
| **40** | 1,099,511,627,776 | 1,348 |
| **41** | 2,199,023,255,552 | 1,437 |
| **42** | 4,398,046,511,104 | 1,549 |
| **43** | 8,796,093,022,208 | 1,563 |
| **44** | 17,592,186,044,416 | 1,569 |
| **45** | 35,184,372,088,832 | 1,601 |
| **46** | 70,368,744,177,664 | 1,659 |
| **47** | 140,737,488,355,328 | 1,823 |
| **48** | 281,474,976,710,656 | 1,847 |
| **49** | 562,949,953,421,312 | 1,856 |
| **50** | 1,125,899,906,842,624 | 1,862 |
| **51** | 2,251,799,813,685,248 | 1,871 |
| **52** | 4,503,599,627,370,496 | 1,903 |

### Empirical Pruning Rates across Search Ranges ($W$)
Applying this multi-layer drop cutoff to all allowed suffixes gives the following pruning percentages:

#### Suffix Width $w = 16$ (Allowed: 9,057)
| Search Range ($2^W$) | Record Max Steps ($M(2^W)$) | Pruned Suffixes | % Pruned |
|:---:|:---:|:---:|:---:|
| **$2^{32}$** | 1,050 | 2,318 | 25.59% |
| **$2^{33}$** | 1,131 | 2,318 | 25.59% |
| **$2^{36}$** | 1,220 | 183 | 2.02% |
| **$2^{40}$** | 1,348 | 2,318 | 25.59% |
| **$2^{44}$** | 1,569 | 416 | 4.59% |
| **$2^{48}$** | 1,847 | 2,318 | 25.59% |
| **$2^{52}$** | 1,903 | 2,318 | 25.59% |

#### Suffix Width $w = 20$ (Allowed: 116,606)
| Search Range ($2^W$) | Record Max Steps ($M(2^W)$) | Pruned Suffixes | % Pruned |
|:---:|:---:|:---:|:---:|
| **$2^{32}$** | 1,050 | 40,916 | 35.09% |
| **$2^{33}$** | 1,131 | 40,916 | 35.09% |
| **$2^{36}$** | 1,220 | 8,740 | 7.50% |
| **$2^{40}$** | 1,348 | 28,594 | 24.52% |
| **$2^{44}$** | 1,569 | 9,854 | 8.45% |
| **$2^{48}$** | 1,847 | 26,824 | 23.00% |
| **$2^{52}$** | 1,903 | 40,916 | 35.09% |

#### Suffix Width $w = 24$ (Allowed: 1,523,909)
| Search Range ($2^W$) | Record Max Steps ($M(2^W)$) | Pruned Suffixes | % Pruned |
|:---:|:---:|:---:|:---:|
| **$2^{32}$** | 1,050 | 554,643 | 36.40% |
| **$2^{33}$** | 1,131 | 554,643 | 36.40% |
| **$2^{36}$** | 1,220 | 95,624 | 6.27% |
| **$2^{40}$** | 1,348 | 416,958 | 27.36% |
| **$2^{44}$** | 1,569 | 107,032 | 7.02% |
| **$2^{48}$** | 1,847 | 407,230 | 26.72% |
| **$2^{52}$** | 1,903 | 479,294 | 31.45% |

### Key Insights
1. **Pruning Capacity Limit**: For ranges with large step count gaps (e.g. $2^{33}$ where the gap $M(2^{33}) - M(2^{32}) = 81$), the pruning rate saturates at **36.40%** for $w = 24$. This is because the maximum step count $k$ inside a width-24 suffix that meets the $4\times$ coefficient drop constraint ($2^{d_j - 2} > 3^{p_j} \implies p_j \le 13$) is bounded by $k = p_j + d_j \le 13 + 24 = 37$ steps. Since $37 \le 42$ and $37 \le 81$, the step limit condition is always satisfied for any suffix that achieves a $4\times$ drop.
2. **Gap Sensitivity**: The pruning rate is highly sensitive to the step record gaps of the adjacent layers. For example, the range $2^{36}$ has an extremely narrow gap $M(2^{36}) - M(2^{35}) = 1220 - 1219 = 1$ step. In this region, a trajectory must drop by $4\times$ in at most 1 step, which is impossible (minimum steps to drop $4\times$ is 2 divisions). Consequently, $b=2$ cannot prune, and pruning relies on larger drops ($b \ge 3$) that happen in very few steps, yielding a smaller but still useful **6.27%** pruning.
3. **Substantial Search Space Reduction**: In most search ranges, this static drop cutoff removes **25% to 36%** of the allowed candidates, providing a direct, branch-free speedup for search engines looking for steps peaks.

## 5. Architectural Design for Dynamic Suffix Pruning

To implement these findings in the search engine without adding performance-sapping branches to the inner loop, we propose a two-phase **Pre-Computed + Dynamic Suffix Filter** architecture.

### A. Allowed Suffix Drop Distributions ($w = 24$)
Before designing the tables, we mapped the maximum drop capabilities across all $1,523,909$ allowed suffixes at width 24:

#### 1. Maximum Drop Factor ($2^b$) Partition
Shows the exact highest drop factor achieved by suffixes:
* **$2^0$ (No $4\times$ drop)**: 969,266 suffixes (63.60%)
* **$2^2$ ($4\times$ drop)**: 231,030 suffixes (15.16%)
* **$2^3$ ($8\times$ drop)**: 227,989 suffixes (14.96%)
* **$2^4$ ($16\times$ drop)**: 76,684 suffixes (5.03%)
* **$2^5$ ($32\times$ drop)**: 11,509 suffixes (0.76%)
* **$2^6$ ($64\times$ drop)**: 7,320 suffixes (0.48%)
* **$2^7$ ($128\times$ drop)**: 23 suffixes (0.00%)
* **$2^8$ ($256\times$ drop)**: 88 suffixes (0.01%)

#### 2. Cumulative Steps to Drop Stats
For each drop factor, we track the step counts required to reach it:
* **$2^2$ ($4\times$ drop)**: 554,643 suffixes \| Min: 10 steps \| Max: 36 steps \| Avg: 26.66 steps
* **$2^3$ ($8\times$ drop)**: 323,613 suffixes \| Min: 11 steps \| Max: 37 steps \| Avg: 31.63 steps
* **$2^4$ ($16\times$ drop)**: 95,624 suffixes \| Min: 17 steps \| Max: 36 steps \| Avg: 32.31 steps
* **$2^5$ ($32\times$ drop)**: 18,940 suffixes \| Min: 21 steps \| Max: 34 steps \| Avg: 32.36 steps
* **$2^6$ ($64\times$ drop)**: 7,431 suffixes \| Min: 27 steps \| Max: 35 steps \| Avg: 34.46 steps
* **$2^7$ ($128\times$ drop)**: 111 suffixes \| Min: 33 steps \| Max: 33 steps \| Avg: 33.00 steps
* **$2^8$ ($256\times$ drop)**: 88 suffixes \| Min: 34 steps \| Max: 34 steps \| Avg: 34.00 steps

### B. Pre-Computed Data Structure (Static File)
Since allowed suffixes and their trajectories are static, we pre-compute the minimum steps to achieve drops $2^2$ through $2^8$. We store this in a compact binary array structure:

```cpp
struct SuffixDropInfo {
    uint32_t suffix;
    uint8_t min_steps[7]; // Steps to reach drop 2^b (b = 2 to 8). 0 if unreachable.
};
```
For $1,523,909$ suffixes at $w=24$, this table consumes exactly **$16.76$ MB** of memory, fitting comfortably in L3 cache or GPU constant/global memory.

### C. Dynamic Startup Filter
When the program starts up and receives the target search range $W$ (where $n \in [2^W, 2^{W+1})$) and the current peak record $S_{max}$:
1. Compute the dynamic threshold for each drop depth $b \in [2, 8]$:
   $$\text{Threshold}(b) = S_{max} - M(2^{W+1-b})$$
2. Iterate through the pre-computed `SuffixDropInfo` list. A suffix is marked as **active** if and only if for all $b \in [2, 8]$ where `min_steps[b-2] > 0`:
   $$\text{min\_steps}[b-2] > \text{Threshold}(b)$$
3. Build a compact, linear array of only the active suffixes:
   `std::vector<uint32_t> active_suffixes;`
4. The inner loops of the search engine (CPU or GPU) then iterate *only* over this pre-filtered list, maintaining a branch-free, optimized execution path.

### D. Self-Optimizing Runtime Updates (Boundary Feedback)
If a search worker discovers a new record-breaking steps peak with value $S_{new} > S_{max}$:
1. Lock and update $S_{max} = S_{new}$.
2. Recalculate $\text{Threshold}(b) = S_{new} - M(2^{W+1-b})$.
3. Filter the `active_suffixes` list in-place. Because $S_{new} > S_{max}$, the thresholds increase, allowing us to prune even more suffixes.
4. For $w=24$, filtering the $1.5\text{M}$ array takes less than $1$ millisecond on a single CPU thread.
5. Workers are notified of the shrunken active suffix list. This creates a feedback loop where finding peaks dynamically accelerates the remaining search.



