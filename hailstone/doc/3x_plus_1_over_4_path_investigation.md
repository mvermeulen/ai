# Investigation: Path Patterns of the Form $(3x+1)/4$

This document investigates the mathematical structure of Collatz trajectories that begin with a $(3x+1)/4$ pattern, corresponding to the path prefix `*/` (ignoring peak and stopping time markers). We analyze their predictability, identify steps peaks that fit this pattern, and examine the counter-examples where the predecessor $Q = (3P+1)/4$ is not itself a historical steps peak.

---

## 1. Goal of the Investigation

The primary objective of this investigation is to determine if steps peaks can be predicted programmatically using trajectory prefix extension:
1. **Peak Prediction**: Can we predict new steps peaks $P$ starting with `*/` from a known steps peak $Q$ using the predecessor relation:
   $$P = \frac{4Q - 1}{3}$$
2. **Completeness**: Will this formula predict *all* steps peaks that start with `*/`? Or are there steps peaks that this misses?
3. **A Priori Optimization**: If we can prove that every steps peak starting with `*/` originates from a known class of steps peaks (or easily identifiable "pseudo-peaks"), we can use this structure to design highly effective a priori pruning filters in the search engine.

---

## 2. Mathematical Foundation

### Parity and Modular Requirements
For a starting value $P$ to begin its trajectory with the combined odd step `*` followed by an even division step `/`, the following operations are executed:

1. **First Step (Odd)**: $P \to 3P+1 \xrightarrow{/2} \frac{3P+1}{2}$ (represented as `*`).
2. **Second Step (Even)**: Since the next symbol is `/`, the value $y = \frac{3P+1}{2}$ must be even, so we divide by 2:
   $$y \to \frac{y}{2} = \frac{3P+1}{4} = Q$$

This sequence of three Collatz steps ($P \to 3P+1 \to \frac{3P+1}{2} \to \frac{3P+1}{4}$) is only possible if:
$$\frac{3P+1}{2} \equiv 0 \pmod 2 \implies 3P + 1 \equiv 0 \pmod 4$$

Since $3P \equiv -1 \equiv 3 \pmod 4$, and $3^{-1} \equiv 3 \pmod 4$, we find:
$$P \equiv 3 \times 3 \equiv 1 \pmod 4$$

Thus, a trajectory starts with the `*/` prefix if and only if **the starting value $P$ has a binary suffix of $01_2$ ($P \equiv 1 \pmod 4$).**

### Predecessor Reconstruction
In the reverse direction, we can reconstruct the starting value $P$ from the value $Q$ reached after these 3 steps:
$$P = \frac{4Q - 1}{3}$$

For $P$ to be a valid odd integer starting value:
1. **Integrability**: $4Q - 1$ must be divisible by 3:
   $$4Q - 1 \equiv Q - 1 \equiv 0 \pmod 3 \implies Q \equiv 1 \pmod 3$$
2. **Parity**: $P$ must be odd, which is automatically satisfied since $4Q - 1$ is always odd, and dividing an odd number by 3 yields an odd number.

> [!IMPORTANT]
> The predecessor relation $P = \frac{4Q-1}{3}$ is mathematically valid if and only if $Q \equiv 1 \pmod 3$.

---

## 3. Analysis of Steps Peaks

An empirical analysis of the 97 historical steps peaks in the range `[3, 8,796,093,022,208]` reveals that **21 steps peaks** start with the `*/` prefix. 

Of these 21 peaks:
- **15 peaks** are direct extensions of an existing steps peak $Q$ in the database.
- **6 peaks** are "counter-examples" because the corresponding value $Q = (3P+1)/4$ is **not** a steps peak.

### Table of the 21 `*/` Steps Peaks
The following table lists all steps peaks $P$ starting with `*/`, the calculated successor $Q = (3P+1)/4$, and whether $Q$ is a steps peak.

| Peak $P$ | Total Steps | Successor $Q = \frac{3P+1}{4}$ | Steps of $Q$ | Is $Q$ a Steps Peak? |
| :--- | :---: | :--- | :---: | :---: |
| 9 | 19 | 7 | 16 | **Yes** (Peak 7) |
| 25 | 23 | 19 | 20 | No (Counter-example) |
| 73 | 115 | 55 | 112 | No (Counter-example) |
| 97 | 118 | 73 | 115 | **Yes** (Peak 73) |
| 129 | 121 | 97 | 118 | **Yes** (Peak 97) |
| 313 | 130 | 235 | 127 | No (Counter-example) |
| 649 | 144 | 487 | 141 | No (Counter-example) |
| 1,161 | 181 | 871 | 178 | **Yes** (Peak 871) |
| 23,529 | 281 | 17,647 | 278 | **Yes** (Peak 17,647) |
| 1,117,065 | 527 | 837,799 | 524 | **Yes** (Peak 837,799) |
| 1,501,353 | 530 | 1,126,015 | 527 | No (Counter-example) |
| 2,298,025 | 559 | 1,723,519 | 556 | **Yes** (Peak 1,723,519) |
| 3,064,033 | 562 | 2,298,025 | 559 | **Yes** (Peak 2,298,025) |
| 11,200,681 | 688 | 8,400,511 | 685 | **Yes** (Peak 8400,511) |
| 14,934,241 | 691 | 11,200,681 | 688 | **Yes** (Peak 11,200,681) |
| 169,941,673 | 953 | 127,456,255 | 950 | No (Counter-example) |
| 226,588,897 | 956 | 169,941,673 | 953 | **Yes** (Peak 169,941,673) |
| 17,828,259,369 | 1,213 | 13,371,194,527 | 1,210 | **Yes** (Peak 13,371,194,527) |
| 568,847,878,633 | 1,324 | 426,635,908,975 | 1,321 | **Yes** (Peak 426,635,908,975) |
| 2,775,669,024,745 | 1,440 | 2,081,751,768,559 | 1,437 | **Yes** (Peak 2,081,751,768,559) |
| 3,700,892,032,993 | 1,443 | 2,775,669,024,745 | 1,440 | **Yes** (Peak 2,775,669,024,745) |

---

## 4. Resolving the Counter-Examples

The discovery of 6 counter-examples (such as $P=25$ leading to $Q=19$) suggests at first glance that we cannot predict all `*/` peaks solely from the steps peaks table. However, a deeper mathematical analysis reveals a consistent, elegant rule that resolves these counter-examples.

### The Pseudo-Peak Minimality Rule

For any starting value $x$, let $S(x)$ denote the number of steps to reach 1.

Suppose $P$ is a steps peak. By definition:
$$S(y) < S(P) \quad \text{for all } y < P$$

Since $P \equiv 1 \pmod 4$ starts with the prefix `*/`, we have:
$$S(P) = S(Q) + 3 \quad \text{where } Q = \frac{3P+1}{4}$$

Now, why is $Q$ sometimes not a steps peak? 
If $Q$ is not a steps peak, there must exist some smaller competitor $Q' < Q$ that takes at least as many steps:
$$S(Q') \ge S(Q) \quad \text{for some } Q' < Q$$

If we try to construct a predecessor $P' = \frac{4Q'-1}{3}$ to compete with $P$:
1. For $P'$ to be a valid starting integer, we must have $Q' \equiv 1 \pmod 3$.
2. If $Q' \equiv 1 \pmod 3$ exists, then:
   $$P' = \frac{4Q'-1}{3} < \frac{4Q-1}{3} = P$$
   $$S(P') = S(Q') + 3 \ge S(Q) + 3 = S(P)$$
   This would mean $P' < P$ has $S(P') \ge S(P)$, which contradicts the fact that $P$ is a steps peak!

Therefore, **no such $Q' < Q$ with $Q' \equiv 1 \pmod 3$ can exist.** 

> [!TIP]
> **Conclusion**: Even though $Q$ may not be the *global* steps peak (because there is some smaller competitor $Q'' < Q$ with $Q'' \not\equiv 1 \pmod 3$ that takes $\ge S(Q)$ steps), $Q$ is guaranteed to be the **minimal starting value in the residue class $1 \pmod 3$** that takes $S(Q)$ steps.

### Prediction via Even Peak + 1 Heuristic

We have introduced a new prediction heuristic: when an even steps peak $E$ is confirmed, we check if $E + 1$ has the same trajectory path (i.e. they merge at the same step count). If they do, we generate predictions using $E + 1$ as a virtual confirmed steps peak.

Applying this heuristic to the 6 counter-examples:
1. **$P=25$ (Successor $Q=19$, Competitor $E=18$):**
   $18$ is an even steps peak (20 steps). Since $18$ and $19$ merge at 22 with the exact same step count (5 steps), they have the same path. We use $19 \equiv 1 \pmod 3$ to predict $P = \frac{4(19)-1}{3} = 25$ (23 steps). **(Found!)**
2. **$P=73$ (Successor $Q=55$, Competitor $E=54$):**
   $54$ is an even steps peak (112 steps). Since $54$ and $55$ merge at 94 with the exact same step count (7 steps), they have the same path. We use $55 \equiv 1 \pmod 3$ to predict $P = \frac{4(55)-1}{3} = 73$ (115 steps). **(Found!)**
3. **$P=169,941,673$ (Successor $Q=127,456,255$, Competitor $E=127,456,254$):**
   $127,456,254$ is an even steps peak (950 steps). Since $127,456,254$ and $127,456,255$ merge at $1,837,442,495$ with the exact same step count (22 steps), they have the same path. We use $127,456,255 \equiv 1 \pmod 3$ to predict $P = \frac{4(127,456,255)-1}{3} = 169,941,673$ (953 steps). **(Found!)**

For the remaining three counter-examples ($P=313, 649, 1,501,353$), their competitors ($231, 327, 1,117,065$) are odd. Because the competitors are odd, there is no even steps peak $E$ to serve as a starting point for this heuristic.

**Summary:** With the new heuristic, **3 out of the 6** previously unpredicted counter-examples are now successfully predicted.

### Analysis of the 6 Counter-Examples

Let us verify this rule for the 6 counter-examples by finding the smaller competitor $Q'' < Q$ that prevents $Q$ from being a steps peak:

1. **$P=25 \to Q=19$** ($S(19) = 20$):
   - $19 \equiv 1 \pmod 3$.
   - The competitor is **$18$** ($18 < 19$, $18 \equiv 0 \pmod 3$, $S(18) = 20$).
   - Since $18 \equiv 0 \pmod 3$, it has no odd predecessor, so it cannot form a competitor for $P$. Hence, $P=25$ remains a steps peak.
2. **$P=73 \to Q=55$** ($S(55) = 112$):
   - $55 \equiv 1 \pmod 3$.
   - The competitor is **$54$** ($54 < 55$, $54 \equiv 0 \pmod 3$, $S(54) = 112$).
   - Since $54 \equiv 0 \pmod 3$, it has no odd predecessor. Hence, $P=73$ remains a steps peak.
3. **$P=313 \to Q=235$** ($S(235) = 127$):
   - $235 \equiv 1 \pmod 3$.
   - The competitor is **$231$** ($231 < 235$, $231 \equiv 0 \pmod 3$, $S(231) = 127$).
   - Since $231 \equiv 0 \pmod 3$, it has no odd predecessor. Hence, $P=313$ remains a steps peak.
4. **$P=649 \to Q=487$** ($S(487) = 141$):
   - $487 \equiv 1 \pmod 3$.
   - The competitor is **$327$** ($327 < 487$, $327 \equiv 0 \pmod 3$, $S(327) = 143$).
   - Since $327 \equiv 0 \pmod 3$, it cannot block $P$. Hence, $P=649$ remains a steps peak.
5. **$P=1,501,353 \to Q=1,126,015$** ($S(1,126,015) = 530$):
   - $1,126,015 \equiv 1 \pmod 3$.
   - The competitor is **$1,117,065$** ($1,117,065 < 1,126,015$, $1,117,065 \equiv 0 \pmod 3$, $S(1,117,065) = 527$).
   - Since $1,117,065 \equiv 0 \pmod 3$, it cannot block $P$. Hence, $P=1,501,353$ remains a steps peak.
6. **$P=169,941,673 \to Q=127,456,255$** ($S(127,456,255) = 953$):
   - $127,456,255 \equiv 1 \pmod 3$.
   - The competitor is **$127,456,254$** ($127,456,254 < 127,456,255$, $127,456,254 \equiv 0 \pmod 3$, $S(127,456,254) = 950$).
   - Since $127,456,254 \equiv 0 \pmod 3$, it has no odd predecessor. Hence, $P=169,941,673$ remains a steps peak.

---

## 5. Detailed Convergence Analysis for the 6 Counter-Examples

When we pull back a pseudo-peak $Q$ to construct a steps peak candidate $P = \frac{4Q-1}{3}$, we find that $P$ is a steps peak but $Q$ is not. This is because $Q$ has a smaller even competitor $Q'$ with the same or higher steps count. To understand how these candidates behave, we track at which intermediate value $V$ their trajectories merge with a path of a previously computed steps peak $R < P$.

Because they share the value $V$, their mathematical trajectories from that point down to 2 are identical. Any minor differences in their path string suffixes are due to notation formatting rules (e.g. peak or stopping time splitting a combined step in one trajectory but not the other).

### Counter-Example $P = 25$
- **Earliest Merging Steps Peak**: $R = 7$
- **Convergence Value $V$**: `11`
- **Steps to Reach $V$**:
  - For $P = 25$: reaches $V$ after `6` steps
  - For $R = 7$: reaches $V$ after `1` steps
- **Unique Prefix Paths before Merge**:
  - Path prefix for $P$: `*/**///`
  - Path prefix for $R$: `*`
- **Value Trajectory before Merge**:
  - $P$ trajectory: `25 -> 38 -> 19 -> 29 -> 44 -> 22 -> 11`
  - $R$ trajectory: `7 -> 11`
- **Notation Match**: Trajectories match mathematically from $V$, but string representations differ due to peak/stopping notation markers.

### Counter-Example $P = 73$
- **Earliest Merging Steps Peak**: $R = 27$
- **Convergence Value $V$**: `47`
- **Steps to Reach $V$**:
  - For $P = 73$: reaches $V$ after `7` steps
  - For $R = 27$: reaches $V$ after `4` steps
- **Unique Prefix Paths before Merge**:
  - Path prefix for $P$: `*/***//`
  - Path prefix for $R$: `**/*`
- **Value Trajectory before Merge**:
  - $P$ trajectory: `73 -> 110 -> 55 -> 83 -> 125 -> 188 -> 94 -> 47`
  - $R$ trajectory: `27 -> 41 -> 62 -> 31 -> 47`
- **Notation Match**: Suffixes match exactly.

### Counter-Example $P = 313$
- **Earliest Merging Steps Peak**: $R = 27$
- **Convergence Value $V$**: `182`
- **Steps to Reach $V$**:
  - For $P = 313$: reaches $V$ after `23` steps
  - For $R = 27$: reaches $V$ after `10` steps
- **Unique Prefix Paths before Merge**:
  - Path prefix for $P$: `*/**/*/***/*/*//*****//`
  - Path prefix for $R$: `**/*****/*`
- **Value Trajectory before Merge**:
  - $P$ trajectory: `313 -> 470 -> 235 -> 353 -> ... (16 intermediate steps) ... -> 485 -> 728 -> 364 -> 182`
  - $R$ trajectory: `27 -> 41 -> 62 -> 31 -> ... (3 intermediate steps) ... -> 161 -> 242 -> 121 -> 182`
- **Notation Match**: Suffixes match exactly.

### Counter-Example $P = 649$
- **Earliest Merging Steps Peak**: $R = 231$
- **Convergence Value $V$**: `587`
- **Steps to Reach $V$**:
  - For $P = 649$: reaches $V$ after `16` steps
  - For $R = 231$: reaches $V$ after `5` steps
- **Unique Prefix Paths before Merge**:
  - Path prefix for $P$: `*/***/***//***//`
  - Path prefix for $R$: `***/*`
- **Value Trajectory before Merge**:
  - $P$ trajectory: `649 -> 974 -> 487 -> 731 -> ... (9 intermediate steps) ... -> 1,565 -> 2,348 -> 1,174 -> 587`
  - $R$ trajectory: `231 -> 347 -> 521 -> 782 -> 391 -> 587`
- **Notation Match**: Suffixes match exactly.

### Counter-Example $P = 1,501,353$
- **Earliest Merging Steps Peak**: $R = 97$
- **Convergence Value $V$**: `146`
- **Steps to Reach $V$**:
  - For $P = 1,501,353$: reaches $V$ after `259` steps
  - For $R = 97$: reaches $V$ after `1` steps
- **Unique Prefix Paths before Merge**:
  - Path prefix for $P$: `*/*******/******///*****/*/*/******/*/**////***//*//**//***********//**/*//**********//***/**/*//*/*****//**///*/********//******//*//****/****///****/*/****/*/*/*****/***//****/*/*//*****//////***//*/*//*******////*////*/////////*/**///*//**//****/////**//*//`
  - Path prefix for $R$: `*`
- **Value Trajectory before Merge**:
  - $P$ trajectory: `1,501,353 -> 2,252,030 -> 1,126,015 -> 1,689,023 -> ... (252 intermediate steps) ... -> 389 -> 584 -> 292 -> 146`
  - $R$ trajectory: `97 -> 146`
- **Notation Match**: Trajectories match mathematically from $V$, but string representations differ due to peak/stopping notation markers.

### Counter-Example $P = 169,941,673$
- **Earliest Merging Steps Peak**: $R = 63,728,127$
- **Convergence Value $V$**: `1,837,442,495`
- **Steps to Reach $V$**:
  - For $P = 169,941,673$: reaches $V$ after `14` steps
  - For $R = 63,728,127$: reaches $V$ after `11` steps
- **Unique Prefix Paths before Merge**:
  - Path prefix for $P$: `*/**********//`
  - Path prefix for $R$: `*********/*`
- **Value Trajectory before Merge**:
  - $P$ trajectory: `169,941,673 -> 254,912,510 -> 127,456,255 -> 191,184,383 -> ... (7 intermediate steps) ... -> 4,899,846,653 -> 7,349,769,980 -> 3,674,884,990 -> 1,837,442,495`
  - $R$ trajectory: `63,728,127 -> 95,592,191 -> 143,388,287 -> 215,082,431 -> ... (4 intermediate steps) ... -> 1,633,282,217 -> 2,449,923,326 -> 1,224,961,663 -> 1,837,442,495`
- **Notation Match**: Suffixes match exactly.

---

## 6. Implications for Search Optimization

While the relationship $P = \frac{4Q-1}{3}$ holds mathematically for all steps peaks $P \equiv 1 \pmod 4$ starting with `*/`, translating this relationship into a search optimization presents significant challenges and opportunities:

### Challenges for A Priori Pruning
To safely prune a residue class modulo $2^N$ *a priori* (i.e. before running the trajectory search up to $P$ or $Q$), we must have a mathematically rigorous guarantee that we will not miss any record-breaking steps peaks.
1. **Retrospective vs. Predictive**: It is trivial to identify the competitor $Q'$ in retrospect once the trajectories of $P$ and $Q$ have been calculated. However, predicting *in advance* whether a specific pseudo-peak $Q \equiv 1 \pmod 3$ will be blocked by a smaller competitor $Q' < Q$ of another residue class is extremely difficult.
2. **Requirement of Provable Completeness**: For the search to remain complete, we do not need to identify *all* competitors for every new candidate of the form $(3x+1)/4$. Rather, we need to prove that we will have found *a* competitor (for $P$) so that the peak would have been predicted. If a heuristic incorrectly assumes $Q$ is dominated (when in fact no competitor exists that blocks $P$ from being a steps peak), the search engine will miss $P$ and fail to discover a true global steps peak.
3. **Mathematical Complexity**: Since Collatz trajectories are highly chaotic, there is currently no known general formula to determine the step count of $Q$ and its competitors $Q'$ without explicitly computing them. Thus, a pure a priori mathematical filter to prune these classes without checking them is likely out of reach.

### Heuristic and Cache-Based Opportunities
Instead of relying on a priori mathematical pruning (which risks missing peaks if incomplete), we can consider heuristic approaches that accelerate the search by leveraging intermediate states:
- **Trajectory Cache Cross-Referencing**: During the search, we compute steps for many values in parallel. If a thread computes the step count of an even competitor $Q' \equiv 0 \pmod 3$, we could store this result. When checking a candidate $Q \equiv 1 \pmod 3$, if it is larger than a known $Q'$ with the same or higher step count, we can immediately classify $Q$ as dominated and skip computing its predecessor $P = \frac{4Q-1}{3}$.
- **Heuristic Filtering**: We can use caches or bounds to identify residue classes where the expected steps count is too low to challenge the current record. However, we must remain extremely cautious to distinguish between heuristic speedups and mathematically complete optimizations.

> [!WARNING]
> Because of the difficulty in proving that a pseudo-peak is dominated without calculating the competitor's path, using the $(3x+1)/4$ relation as a hard a priori pruning filter is currently **not recommended**. It introduces a high risk of missing record-breaking steps peaks due to the lack of a provable way to guarantee that we will have found a competitor that predicts the peak.

---

## 7. Experiment: Predicting from Peaks or Ties

### Hypothesis
In high-performance sequential search, our current prediction rules generate a candidate $P = \frac{4Q-1}{3}$ only when its successor $Q$ is a **confirmed steps peak** (or if the even peak $+ 1$ heuristic applies).
We propose expanding the prediction rule: **generate predictions from any starting value $Q$ that either sets a new steps peak record OR ties the current steps peak record.**

Since exceptions are caused by $Q$ being blocked by a smaller competitor $Q' < Q$ with an equal or greater step count, $Q$ must tie or fall below the current peak. If $Q$ ties the peak record, this expanded rule should generate the prediction for $P$.

### Experimental Results on the 6 Exceptions
We ran a sequential simulation of the search up to the database limit. The table below evaluates whether the modified prediction rule catches the 6 exceptions (plus the 7th exception at 26T, which falls outside the database range of `[3, 8,796,093,022,208]`):

| Exception Peak $P$ | Successor $Q = \frac{3P+1}{4}$ | Steps of $Q$ | Peak Steps at $Q$ (Competitor) | Does $Q$ Tie Peak? | Predicted by Original (with $+1$ heuristic)? | Predicted by Modified (Peaks or Ties)? |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **25** | 19 | 20 | 20 (from 18) | **Yes** | **Yes** (from 18+1) | **Yes** (from 19) |
| **73** | 55 | 112 | 112 (from 54) | **Yes** | **Yes** (from 54+1) | **Yes** (from 55) |
| **313** | 235 | 127 | 127 (from 231) | **Yes** | No | **Yes** (from 235) |
| **649** | 487 | 141 | 143 (from 327) | **No** (141 < 143) | No | **No** |
| **1,501,353** | 1,126,015 | 527 | 527 (from 1,117,065) | **Yes** | No | **Yes** (from 1,126,015) |
| **169,941,673** | 127,456,255 | 950 | 950 (from 127,456,254) | **Yes** | **Yes** (from 127,456,254+1) | **Yes** (from 127,456,255) |
| **26,262,557,464,201** | 19,696,918,098,151 | 1585 | 1585 (from 19,536,224,150,271) | **Yes** | No | **Yes** (from 19,696,918,098,151) |

#### Analysis of the Missed Exception ($P = 649$)
The only exception missed by the modified rule is **649**. 
- Its successor is $Q = 487$ (141 steps).
- When the search reaches 487, the active peak record is 143 steps (set by 327).
- Because 141 is strictly less than 143, 487 does not tie the peak. Therefore, the tie-based rule does not generate a prediction from 487, and 649 remains unpredicted.

### Discussion & Implications for Suffix Cutoff

1. **Inherent Incompleteness**: While the modified rule ("predict from peaks or ties") successfully catches **5 out of 6** database exceptions (and **6 out of 7** overall exceptions), the fact that **649** remains unpredicted is a critical blocker. 
   - For $P = 649$, its successor $Q = 487$ takes 141 steps, which is strictly less than the active record of 143 steps (set by 327).
   - Because 487 is neither a peak nor a tie, no local heuristic based on active peak records can ever generate the prediction for 649.
   - Consequently, there is no mathematical guarantee that similar, larger exceptions do not exist in unsearched ranges (e.g. beyond 175T).

2. **Absolute Priority of Correctness**: 
   - In a Collatz search engine, correctness is the absolute highest priority. Missing even a single peak is completely unacceptable.
   - Arguing that exceptions like 649 are "rare" or using a hardcoded list of known exceptions only works for ranges that have already been searched and verified. It is mathematically invalid to extrapolate this to unsearched ranges.
   - Therefore, any optimization strategy (such as a suffix cutoff) that relies on the completeness of peak predictions to prune the search space is **mathematically unsafe** and must be rejected.

3. **Recommendation**:
   - **Pruning Strategy**: A priori search space pruning must be restricted **solely** to mathematically complete and proven rules. This includes the Vermeulen polynomial (`fpoly`) collision/even-class exclusion and modulo 6 checks, which do not depend on step counts or peak records and are guaranteed to preserve all peaks.
   - **Role of the Peak Predictor**: The `PeakPredictor` should **never** be used to prune the search space. Its usage must remain strictly limited to verification (e.g., cross-referencing and confirming predicted peaks during execution) or as a diagnostic tool, where incompleteness does not threaten the correctness of the search.



