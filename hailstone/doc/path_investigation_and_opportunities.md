# Investigation: Trajectory Path Representation and Optimization Opportunities

This document details the trajectory path representation used in the Collatz search (via the `hailstone_path` utility) and explores mathematical and computational opportunities for leveraging these paths to optimize search performance.

---

## 1. Path Representation and Notation

The `hailstone_path` utility produces a compact, character-based string representation of the Collatz trajectory of a starting value $n$:

| Symbol | Description | Mathematical Operation |
| :---: | :--- | :--- |
| `*` | Combined odd step | $x \to \frac{3x+1}{2}$ |
| `/` | Even division step | $x \to \frac{x}{2}$ (when not part of a $3x+1$ transition) |
| `^` | Peak indicator | Marks the global maximum intermediate value in the trajectory |
| `|` | Stopping time marker | Marks the first point where the trajectory value falls below the starting value $n$ |

### Combined vs. Split Steps (Exception Rules)
To make the notation concise, the utility combines a $3x+1$ multiplication and its subsequent division by 2 into a single `*` character. However, if the trajectory reaches its **peak** (`^`) or its **stopping time** (`|`) directly after the multiplication step but *before* the division by 2, the step is split:
- The multiplication is represented as a lone `*` (without the implicit division by 2).
- The peak (`^`) or stopping time (`|`) is appended.
- The division by 2 is deferred and recorded separately as a `/` or as part of a subsequent step.

Additionally, because all trajectories eventually reach the cycle $4 \to 2 \to 1$, the path representation terminates once the value reaches $2$ (omitting the final trivial `/` step to $1$).

---

## 2. Illustration: Trajectory of 9

To illustrate the notation and the step-by-step state changes, let us examine the starting number **9**.

### Verbose Output Steps
Running `./build/hailstone_path 9 -v` generates the following trace:

```text
* 14
/ 7
| 7
* 11
* 17
* 52
^ 52
/ 26
/ 13
* 20
/ 10
/ 5
* 8
/ 4
/ 2
```

### Analytical Breakdown
The following table details how each symbol in the path `*/|***^//*//*//` is generated:

| Current Value ($x$) | Operation | Resulting Value | Symbol(s) | Explanation / Context |
| :---: | :--- | :---: | :---: | :--- |
| **9** | $\frac{3(9)+1}{2}$ | 14 | `*` | 9 is odd; undergoes combined $3x+1$ and division by 2. |
| **14** | $\frac{14}{2}$ | 7 | `/` | 14 is even; division by 2. |
| **7** | (Threshold check) | 7 | `|` | $7 < 9$; stopping time is reached. We append `|`. |
| **7** | $\frac{3(7)+1}{2}$ | 11 | `*` | 7 is odd; combined step. |
| **11** | $\frac{3(11)+1}{2}$ | 17 | `*` | 11 is odd; combined step. |
| **17** | $3(17)+1$ | 52 | `*` | **Exception Rule**: $52$ is the trajectory peak. We split the step. |
| **52** | (Peak check) | 52 | `^` | We mark the peak value $52$. |
| **52** | $\frac{52}{2}$ | 26 | `/` | Even division step (deferred from the split step). |
| **26** | $\frac{26}{2}$ | 13 | `/` | 26 is even; division by 2. |
| **13** | $\frac{3(13)+1}{2}$ | 20 | `*` | 13 is odd; combined step. |
| **20** | $\frac{20}{2}$ | 10 | `/` | 20 is even; division by 2. |
| **10** | $\frac{10}{2}$ | 5 | `/` | 10 is even; division by 2. |
| **5** | $\frac{3(5)+1}{2}$ | 8 | `*` | 5 is odd; combined step. |
| **8** | $\frac{8}{2}$ | 4 | `/` | 8 is even; division by 2. |
| **4** | $\frac{4}{2}$ | 2 | `/` | 4 is even; division by 2. |
| **2** | (Termination) | 2 | (None) | Loop ends because $x \le 2$. |

### Summary Statistics (`-s`)
- **Path String**: `*/|***^//*//*//`
- **Total Steps ($H$-trajectory)**: 19 steps (from 9 to 1)
- **Stopping Time ($T$-trajectory)**: 2 steps (divisions by 2 to fall below 9: $9 \to 14 \to 7$)
- **Peak Value**: 52 (reached at step 8)

---

## 3. Path Reconstruction and Legality Validation

Because there is a 1:1 mapping between a valid trajectory path and its starting number, we can translate between the two representations dynamically and use whichever representation is most advantageous for a given optimization.

### Reconstructing a Number from a Path
To reconstruct the starting number, we begin with the terminal state $x = 2$ and process the path operations in reverse order (from right to left):

1. **Reverse Division (`/`)**: 
   Since the forward step was $x \to \frac{x}{2}$, the reverse step is:
   $$x_{prev} = 2x$$
2. **Reverse Combined Step (`*` not followed by `^`)**:
   Since the forward step was $x \to \frac{3x+1}{2}$, the reverse step is:
   $$x_{prev} = \frac{2x - 1}{3}$$
3. **Reverse Split Step (`*` followed by `^`)**:
   Since the forward step was $x \to 3x + 1$, the reverse step is:
   $$x_{prev} = \frac{x - 1}{3}$$

---

### Path Legality and Parity Constraints
Not all arbitrary sequences of `*` and `/` symbols represent valid, physically possible Collatz trajectories. A path is considered illegal if it violates arithmetic divisibility or residue class ancestry constraints:

#### 1. Arithmetic Divisibility Constraint
At any point in the reverse traversal, the reconstructed predecessor must be an integer and must satisfy the parity requirements of the forward step:
- For a reverse combined step, $2x - 1$ must be divisible by 3, and the resulting $x_{prev}$ must be odd.
- For a reverse split step, $x - 1$ must be divisible by 3, and the resulting $x_{prev}$ must be odd (which also requires that $x$ be even).

#### 2. Residue Class Ancestry Constraint (Parity Rule)
A number $n$ can only have an odd predecessor in the Collatz graph if there exists an integer $p$ such that:
$$3p + 1 = n \implies p = \frac{n - 1}{3}$$

If $n \equiv 0 \pmod 3$, then $n - 1 \equiv 2 \pmod 3$, which is not divisible by 3. Consequently, any number divisible by 3 cannot have an odd predecessor. In terms of path notation:
- If a value $V$ in the trajectory satisfies $V \equiv 0 \pmod 3$, then the step that reached $V$ (in the forward direction) **must** be an even division step (`/`), never a combined or split odd step (`*`).
- For example, in the trajectory of 9, the value $9 \equiv 0 \pmod 3$. Thus, its predecessor in the trajectory (14) must be even, and reached via division (`/ 14` in reverse). Any path that attempts to transition to 9 via a multiplication step is mathematically illegal.

---

## 4. Mathematical Insights & Congruence Mappings

The 1:1 equivalence between a trajectory path and its starting number allows us to use either representation to analyze and search for optimizations. This equivalence yields several mathematical insights:

### Insight A: Suffix-Prefix Correspondence (Modulo $2^N$ Mapping)
The binary suffix (the last $N$ bits) of any starting number $n$ has a direct, deterministic relationship to the first $N$ division steps in the trajectory's path representation.
- For any starting number $n$ and bit-width $N$, the sequence of division operations (the `*` and `/` symbols in the path, ignoring peak `^` and stopping `|` markers) is uniquely determined by the congruence class $n \pmod{2^N}$.
- Even though combined `*` steps are sometimes split around a peak, the underlying sequence of division steps remains identical for all numbers in the same congruence class.

#### Example 1: $n \equiv 1 \pmod 4$ (Binary Suffix $01_2$)
Let $n = 4m + 1$ for $m \ge 1$.
1. **First Step (Odd)**: 
   $$3n + 1 = 3(4m + 1) + 1 = 12m + 4 \xrightarrow{/2} 6m + 2 \quad (\text{Emits } *)$$
2. **Second Step (Even)**:
   Since $6m + 2$ is even, we divide by 2:
   $$\frac{6m + 2}{2} = 3m + 1 \quad (\text{Emits } /)$$
3. **Stopping Time Check**:
   $$3m + 1 < 4m + 1 \quad (\text{for all } m \ge 1) \quad (\text{Emits } |)$$

This proves that **every** starting number $n \equiv 1 \pmod 4$ (except 1) begins with the path prefix `*/|`.

#### Example 2: $n \equiv 3 \pmod 4$ (Binary Suffix $11_2$)
Let $n = 4m + 3$ for $m \ge 0$.
1. **First Step (Odd)**:
   $$3n + 1 = 3(4m + 3) + 1 = 12m + 10 \xrightarrow{/2} 6m + 5 \quad (\text{Emits } *)$$
2. **Second Step (Odd)**:
   Since $6m + 5$ is odd, the next step is another $3x+1$ transition, emitting another `*`.

This proves that **every** starting number $n \equiv 3 \pmod 4$ begins with the path prefix `**`.

#### Suffix Filtering via Existing Peaks
Because the residue class $n \pmod{2^N}$ determines the first $N$ steps of a trajectory path, we can inspect the starting path prefixes of historically known steps peaks to identify structural trends:
- **Empirical Hypothesis**: Record-breaking steps peaks appear to only utilize a very small subset of possible starting path configurations (and thus a small subset of possible binary suffixes).
- **Opportunity for Stronger Cutoffs**: While Suffix-First search uses `fpoly` tables to discard duplicate and even residue classes (yielding a mathematically rigorous a priori pruning), there are still many mathematically "allowed" suffixes that never actually produce steps peaks in practice.
- **Research Question**: *Why are certain allowed suffixes never selected as peaks?* If we can mathematically formalize why these suffixes fail to produce record-breaking trajectories, we can introduce additional a priori filters to prune these non-performing residue classes, achieving even greater search speedups.

---

### Insight B: Residue Class Merging and Path Collisions
When starting numbers in different congruence classes undergo division and multiplication, their paths can collide, meaning they reach the exact same value and share the remainder of their trajectories.

#### Example: 3-bit Width Suffixes (Modulo 8)
Let us analyze two congruence classes modulo 8:
1. **Suffix $100_2$ ($n \equiv 4 \pmod 8$)**:
   Let $n = 8m + 4$ for $m \ge 1$.
   - **Step 1 (Even)**: $\frac{8m+4}{2} = 4m+2 \quad (\text{Emits } /)$
   - **Step 2 (Even)**: $\frac{4m+2}{2} = 2m+1 \quad (\text{Emits } /)$
   - **Step 3 (Odd)**: $\frac{3(2m+1)+1}{2} = 3m+2 \quad (\text{Emits } *)$
   - **Path Prefix**: `//*`
   - **Reached Value**: $3m+2$

2. **Suffix $101_2$ ($n \equiv 5 \pmod 8$)**:
   Let $n = 8m + 5$ for $m \ge 0$.
   - **Step 1 (Odd)**: $\frac{3(8m+5)+1}{2} = 12m+8 \quad (\text{Emits } *)$
   - **Step 2 (Even)**: $\frac{12m+8}{2} = 6m+4 \quad (\text{Emits } /)$
   - **Step 3 (Even)**: $\frac{6m+4}{2} = 3m+2 \quad (\text{Emits } /)$
   - **Path Prefix**: `*//`
   - **Reached Value**: $3m+2$

#### Collision Analysis
Both classes reach the exact same intermediate value $3m+2$ after their respective 3 division steps, despite having different path prefixes (`//*` vs `*//`). Because the subsequent steps depend only on this intermediate value, their paths merge/combine from this point forward.

```
n ≡ 4 (mod 8) ---> [ / ] ---> [ / ] ---> [ * ] ---\
                                                   ---> [ 3m + 2 ] ---> (Identical Path Suffix)
n ≡ 5 (mod 8) ---> [ * ] ---> [ / ] ---> [ / ] ---/
```

---

## 5. Path Convergence and Trajectory Families

If two paths share the same ending path representation (the same suffix of operations), their trajectories have converged. Analyzing how paths merge, extend, and order their markers yields several structural insights:

### Insight A: Path Prepending and Peak Predictions
When a trajectory converges with another, its path string is often a prepended version of the other's path (modulo adjustments for early stopping time resolution).
- **Example: 9 vs. 18**:
  - Path of 9: `*/|***^//*//*//`
  - Path of 18: `/|*/***^//*//*//`
  - Since $18 / 2 = 9$, the trajectory of 18 is identical to 9's trajectory after the first division. This is represented by prepending `/|` to 9's path (with the subsequent `|` marker at 7 omitted because the stopping time was already resolved at 9).
- **Peak Prediction Correspondence**:
  This path prepending directly mirrors the mathematical predecessor equations used to predict future steps peaks:
  - **Case A ($X \equiv 0 \pmod 3$)**: The prediction $P = 2X$ corresponds to prepending `/` to the path of $X$.
  - **Case B ($X \equiv 1 \pmod 3$)**: The prediction $P = \frac{4X-1}{3}$ corresponds to prepending `*/` to the path of $X$ (since $P \to 3P+1 \to 2X \to X$). For a deep dive into this pattern, its modular constraints, and steps peak predictions (including resolving counter-examples), see the [$(3x+1)/4$ Path Pattern Investigation](file:///home/mev/source/ai/hailstone/doc/3x_plus_1_over_4_path_investigation.md).
- **Opportunity**: We can systematically study if other prepended sequences (like `*//` or `//`) can be legally prepended to existing peak paths to discover new classes of peak-generating numbers.

---

### Insight B: Trajectory Peak Families
When two starting values share the exact same suffix after their peak marker `^`, they belong to the same **trajectory family**.
- All starting numbers in a family reach a common peak value and follow the exact same path down to 2.
- Classifying trajectories into families centered around specific peaks (such as 16, 52, 160, etc.) offers a way to study the density and population of steps peaks. We can investigate what fraction of steps peaks are isolated individuals versus members of large families.

---

### Insight C: Relative Ordering of Peak (`^`) and Stopping (`|`) Markers
The relative positions of `^` and `|` in a path string reveal whether a number drops below its starting value before or after reaching its peak.
- **Conjecture**: If a starting number $n$ reaches its stopping time `|` *before* its peak `^`, it is unlikely to be a record-breaking peak in steps.
- **Rationale**: If `|` appears before `^`, the trajectory drops to some value $n' < n$ before climbing to the peak. Because $n' < n$, the smaller starting number $n'$ already covers the remainder of the trajectory (including the peak and subsequent steps) in fewer total steps. Therefore, another smaller predecessor of $n'$ is mathematically positioned to reach the peak first and accumulate more steps than $n$.
- **Empirical Validation**: In stopping time peaks, the peak `^` is always reached before the stopping time `|`. For a detailed statistical trace, see the [Paths Taken by Peaks in Stopping Time Investigation](file:///home/mev/source/ai/hailstone/doc/stopping_time_path_investigation.md).

---

### Insight D: Collision-Avoidance (Minimality) in Peak Trajectories
For any record-breaking stopping time ($\sigma$) or maximum value peak, we can analyze the path segment *prior* to reaching the peak (or stopping point) and ask: *Why was there no other path that merged with this one that started from a lower number?*
- **The Parity of Peak Ancestry**: If a path originating from a lower starting value $k < n$ had merged with $n$'s path before $n$ reached its defining landmark (peak or stopping time), then $k$ would have reached that landmark first. Since $k$ is smaller, $n$ would be dominated and fail to be a peak.
- **Path Pattern Analysis**: This implies that the pre-peak segments of record-breaking paths must possess properties that prevent early collisions (merging) with trajectories of smaller numbers. We can study specific pattern signatures—such as long sequences of repeating divisions `//...` or repeating multiplications `**...`—to understand how stopping time and max value trajectories preserve their minimality and why certain starting numbers succeed in becoming peaks. For a complete analysis of the collision avoidance, run lengths, and containment results of stopping time paths, see the [Paths Taken by Peaks in Stopping Time Investigation](file:///home/mev/source/ai/hailstone/doc/stopping_time_path_investigation.md).

---

### Insight E: Trajectory Family Minimality and Growth Patterns
When studying a trajectory family (multiple starting numbers that reach the same maximum peak value), we can analyze the different merging paths to find the **minimal representative** (the lowest starting number in the family):
- **Finding the Lowest Ancestor**: For a given peak, multiple branches merge into it. We can reason about the shape of these branches to explain why a particular path pattern yields the lowest starting number (the first/lowest member to seed the family).
- **Peak Value Growth Patterns**: We can analyze the structure of paths leading to maximum values to see if there are specific patterns or "rules of scale" that run out or propagate to create ever-increasing peak values. Recognizing these growth templates could allow us to construct high-value trajectories directly rather than relying on sequential search.

---

## 6. Optimization Opportunities

Analyzing trajectory paths opens several avenues for structural optimization in high-performance Collatz search engines:

### Opportunity A: Compact Bit-Vector Path Caching
Currently, the search engine computes steps using arithmetic loops.
- **Concept**: Since trajectories frequently merge (due to the Collatz graph's tree structure), many numbers share identical path suffixes.
- **Design**: We can represent path segments as compact bit-vectors (e.g., `0` for division `/`, `1` for odd step `*`).
- **Application**: A thread-local or GPU-shared suffix cache could store `(value, bit-vector-path)` pairs. If a trajectory hits a cached value, it can immediately replay the bit-vector or add the pre-calculated step length, skipping ALU cycles.

### Opportunity B: Symbolic Path Pruning
By analyzing path prefix equations (like the $4m+1$ and $4m+3$ proofs above), we can identify path patterns that are guaranteed *never* to yield new records.
- **Steps Peaks**: A trajectory that drops below its starting value too quickly (e.g. `*/|` prefix) has a very short stopping time and is mathematically restricted from accumulating enough steps to challenge the global steps record.
- **Implementation**: We can formalize constraints on what path prefixes are allowed to contain peaks. If a prefix class has a path prefix that implies early stopping or low peak values, we can prune the entire residue class before execution.

### Opportunity C: Path Signature Congruence
Just as we exclude even classes and mod-6 congruences, we can look for "doomed" path signatures.
- If two residue classes modulo $2^w$ generate paths that collide at a value $V$ using the same number of steps, they have identical subsequent paths.
- By tracking path signatures, we can dynamically discover new a priori cutoff classes at runtime, augmenting the static `fpoly` tables with dynamic collision maps.

### Opportunity D: Verification via Path Replays
Currently, cross-backend verification (`hailstone_verify`) compares final peak values and step counts.
- **Design**: We can verify correctness by checking if the path strings generated by the CPU, HIP, and Vulkan backends are identical.
- **Benefit**: Comparing paths provides a much stronger differential testing guarantee than comparing scalar outputs, as it ensures every state transition along the trajectory is mathematically identical across hardware platforms.

