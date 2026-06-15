# Investigation: Paths Taken by Peaks in Stopping Time

This document investigates the trajectory paths of record-breaking stopping time peaks (often called $\sigma$-peaks) in the Collatz search. Building on the core path representations detailed in [Trajectory Path Representation and Optimization Opportunities](file:///home/mev/source/ai/hailstone/doc/path_investigation_and_opportunities.md), we analyze the structures of these paths prior to reaching the stopping time to find common patterns, mathematical constraints, and structural relationships between stopping-time-maximizing trajectories and steps-maximizing trajectories.

---

## 1. Summary Statistics of Stopping Time Peaks

For a starting value $n$, the stopping time $\sigma$ is the number of steps (divisions by 2) required for the trajectory to fall below $n$. We examine the path prefix *prior* to reaching the stopping time (the string up to the `|` character) for all 27 historical stopping time peaks found in the search:

| Starting Number ($n$) | $\sigma$ | Pre-Stop Length | Stars ($*$) / Slashes ($/$) | Ratio ($*//$) | Max Run ($*//$) | Avg Run ($*//$) | Peak before Stop? |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| 3 | 4 | 5 | 2 / 3 | 0.667 | 2 / 3 | 2.00 / 3.00 | Yes |
| 7 | 7 | 8 | 4 / 4 | 1.000 | 3 / 2 | 2.00 / 2.00 | Yes |
| 27 | 59 | 60 | 37 / 23 | 1.609 | 6 / 4 | 2.47 / 1.53 | Yes |
| 703 | 81 | 82 | 51 / 31 | 1.645 | 6 / 4 | 2.83 / 1.72 | Yes |
| 10,087 | 105 | 106 | 66 / 40 | 1.650 | 7 / 6 | 2.64 / 1.60 | Yes |
| 35,655 | 135 | 136 | 85 / 51 | 1.667 | 8 / 6 | 2.66 / 1.59 | Yes |
| 270,271 | 164 | 165 | 103 / 62 | 1.661 | 8 / 5 | 2.71 / 1.63 | Yes |
| 362,343 | 165 | 166 | 104 / 62 | 1.677 | 10 / 5 | 2.97 / 1.77 | Yes |
| 381,727 | 173 | 174 | 109 / 65 | 1.677 | 10 / 5 | 2.95 / 1.76 | Yes |
| 626,331 | 176 | 177 | 111 / 66 | 1.682 | 14 / 4 | 2.71 / 1.61 | Yes |
| 1,027,431 | 183 | 184 | 115 / 69 | 1.667 | 7 / 6 | 2.61 / 1.57 | Yes |
| 1,126,015 | 224 | 225 | 141 / 84 | 1.679 | 11 / 6 | 3.20 / 1.91 | Yes |
| 8,088,063 | 246 | 247 | 155 / 92 | 1.685 | 10 / 5 | 2.54 / 1.51 | Yes |
| 13,421,671 | 287 | 288 | 181 / 107 | 1.692 | 12 / 10 | 3.12 / 1.84 | Yes |
| 20,638,335 | 292 | 293 | 184 / 109 | 1.688 | 9 / 7 | 3.02 / 1.79 | Yes |
| 26,716,671 | 298 | 299 | 188 / 111 | 1.694 | 12 / 7 | 2.85 / 1.68 | Yes |
| 56,924,955 | 308 | 309 | 194 / 115 | 1.687 | 12 / 5 | 2.77 / 1.64 | Yes |
| 63,728,127 | 376 | 377 | 237 / 140 | 1.693 | 9 / 4 | 2.44 / 1.44 | Yes |
| 217,740,015 | 395 | 396 | 249 / 147 | 1.694 | 10 / 6 | 2.65 / 1.56 | Yes |
| 1,200,991,791 | 398 | 399 | 251 / 148 | 1.696 | 9 / 5 | 2.79 / 1.64 | Yes |
| 1,827,397,567 | 433 | 434 | 273 / 161 | 1.696 | 13 / 7 | 2.90 / 1.71 | Yes |
| 2,788,008,987 | 447 | 448 | 282 / 166 | 1.699 | 14 / 4 | 2.74 / 1.61 | Yes |
| 12,235,060,455 | 547 | 548 | 345 / 203 | 1.700 | 11 / 5 | 2.74 / 1.61 | Yes |
| 898,696,369,947 | 550 | 551 | 347 / 204 | 1.701 | 13 / 6 | 2.65 / 1.56 | Yes |
| 2,081,751,768,559 | 606 | 607 | 382 / 225 | 1.698 | 11 / 7 | 2.62 / 1.54 | Yes |
| 13,179,928,405,231 | 688 | 689 | 434 / 255 | 1.702 | 14 / 6 | 2.75 / 1.61 | Yes |
| 31,835,572,457,967 | 712 | 713 | 449 / 264 | 1.701 | 10 / 8 | 2.58 / 1.52 | Yes |

---

## 2. Core Observations & Structural Patterns

### A. Global Peak Location (Peak-Before-Stop Rule)
In **100%** of the recorded stopping time peaks, the global maximum intermediate value in the trajectory is reached **before** the stopping time is resolved (meaning the peak marker `^` is located prior to the stopping time marker `|` in the path string). 

This is mathematically intuitive: for a trajectory to accumulate a record-breaking number of steps above $n$, it must climb to a very high peak. Once it falls below $n$, the pre-stop path is terminated. If the peak were after the stopping point, the trajectory would have dropped below $n$ earlier, terminating the pre-stop path at a much shorter length, disqualifying it from being a stopping time peak.

### B. Critical Growth Balance (The 1.70 Ratio)
For a starting value $n$, each combined odd step (`*`) multiplies the value by approximately $1.5$ (since $x \to \frac{3x+1}{2} \approx 1.5x$), and each division step (`/`) multiplies the value by $0.5$. 

To prevent the trajectory from dropping below its starting value $n$, the cumulative growth factor must remain greater than $1.0$:
$$(1.5)^{n_*} \times (0.5)^{n_/} \ge 1.0$$
$$\implies n_* \ln(1.5) \ge n_/ \ln(2.0)$$
$$\implies \frac{n_*}{n_/} \ge \frac{\ln(2)}{\ln(1.5)} \approx 1.70951$$

As $n$ grows larger, the $+1$ offset in the $3x+1$ step becomes negligible, and the trajectories of stopping time peaks behave like critically balanced random walks that hover just above $n$. The ratio of Stars to Slashes in the pre-stop paths of large peaks is consistently and remarkably close to this threshold:
- For $n = 12,235,060,455$, the ratio is **1.700**
- For $n = 898,696,369,947$, the ratio is **1.701**
- For $n = 13,179,928,405,231$, the ratio is **1.702**
- For $n = 31,835,572,457,967$, the ratio is **1.701**

Because the ratio is slightly less than $1.70951$, the value at the end of the pre-stop segment is forced to fall below $n$ at the next division step, ending the pre-stop path.

### C. Run-Length Asymmetry
In a standard random walk where odd and even transitions have a 50% probability, the expected run length of consecutive divisions by 2 is 2. (In our path representation, since the first division of any $3x+1$ step is implicit inside `*`, the expected number of consecutive `/` characters following a `*` is $2-1=1$, which translates to an average run length of 2 when consecutive runs of `/` do occur).

However, the empirical results show a striking asymmetry:
- **Slash Runs (`/`)**: The average run length of `/` is consistently between **1.4 and 1.9** (much lower than the random walk average of 2). The maximum run of consecutive slashes prior to the stopping time rarely exceeds **7** (except for $n = 13,421,671$, which has a run of 10 divisions directly descending from its enormous peak).
- **Star Runs (`*`)**: The average run length of `*` is consistently between **2.4 and 3.2** (significantly higher than 2). The maximum run of consecutive stars frequently reaches **10 to 14**.

**Mathematical Explanation**: If a trajectory experiences a long run of consecutive divisions (e.g., `//////`), the intermediate value divides by $2^6 = 64$ or more, causing a rapid collapse. Such collapses almost always plunge the value below the starting value $n$, terminating the pre-stop path. Thus, selection bias naturally prunes paths with long slash runs. To survive as a stopping time peak, the trajectory must favor long star runs to build and maintain altitude, while dividing in short, single-step intervals (e.g., alternating `*/*` or `*//*`).

---

## 3. Substring Comparisons: Collision-Avoidance and Autonomy

### A. Comparison Between Distinct Sigma Peaks
We compared the pre-stop paths of all 27 sigma peaks pairwise to search for common substrings.
- **The $362,343$ and $381,727$ Merge**: A massive common suffix of length **166** was found between $362,343$ and $381,727$. This is a complete merge of their trajectories (see below for detail).
- **Other Peaks**: For all other pairs of distinct sigma peaks, the longest common substring (LCS) in their pre-stop paths is extremely short, never exceeding **26 characters**.

This confirms that each stopping time peak represents a mathematically autonomous, independent family of trajectories. They do not share long pre-stop histories or merge early, indicating that stopping-time-maximizing trajectories must avoid collisions with other known paths to preserve their record-breaking minimality.

#### Detailed Analysis of the $362,343$ & $381,727$ Merge
The trajectories of $362,343$ and $381,727$ merge early:
- $362,343 \to 543,515$ (1 step)
- $381,727 \to 572,591 \to 858,887 \to 1,288,331 \to 1,932,497 \to 2,898,746 \to 1,449,373 \to 2,174,060 \to 1,087,030 \to 543,515$ (9 steps)

At the value $543,515$, the two paths collide. From this point forward, their trajectories are identical. 
Because $381,727$ is larger than $362,343$, one might expect the larger threshold of $381,727$ to trigger its stopping time earlier. However, the step before their shared stopping point is $646,868$ (which is larger than both $362,343$ and $381,727$). The next step is a division by 2, yielding **$323,434$**. Because $323,434$ is less than both $362,343$ and $381,727$, the stopping time for both numbers is resolved at this exact same value. 
- $362,343$ reaches its stopping time after 165 steps.
- $381,727$ reaches its stopping time after 173 steps ($165 + (9 - 1) = 173$).
- This accounts for the identical 166-character pre-stop suffix, as they share the trajectory from $543,515$ to $323,434$.

---

### B. Comparison with Steps Peaks
We cross-compared the pre-stop paths of the 27 stopping time peaks ($\sigma$-peaks) against the full paths of all steps peaks:

1. **Intersection Peaks (33.3%)**: Exactly 9 of the 27 sigma peaks are also record-breaking steps peaks:
   - `[3, 7, 27, 703, 35,655, 626,331, 63,728,127, 12,235,060,455, 2,081,751,768,559]`
   - Naturally, their pre-stop paths are exact substrings of their own step trajectories.

2. **The Predecessor Peak (`1,126,015`)**: The pre-stop path of $\sigma$-peak `1,126,015` (len 226) is an exact substring of steps peak `1,501,353`.
   - This occurs because $1,501,353$ transitions to $1,126,015$ via a combined odd step followed by division: $1,501,353 \to 4,504,060 \to 2,252,030 \to 1,126,015$ (path prefix `*//`). 
   - $1,126,015$ was not recorded as a steps peak because its step count (524) matched the existing record held by $837,799$ but did not exceed it. However, the predecessor $1,501,353$ added steps to break the record at 530 steps.

3. **Isolated Sigma Families (63.0%)**: The remaining 17 stopping time peaks have **no containment** in any steps peaks. Their longest overlap with any steps peak path is extremely short, never exceeding **25 characters**.

This is a critical finding: **most stopping time peaks belong to trajectory families that are completely distinct from steps peaks.** 
- Steps peaks prioritize the *total* length of the trajectory, which includes the post-stopping-time segment (the path after it falls below $n$, down to $2$).
- Stopping time peaks prioritize only the pre-stopping-time segment. 
- The fact that 63% of sigma peaks are isolated from steps peaks indicates that many paths that are optimized to hover above their start value for a long time do not generate exceptionally long tails once they fall below their threshold, and vice versa.

---

## 4. Mathematical Opportunities & Research Directions

### A. Heuristic Suffix Filtering
We can use the statistical properties of pre-stop paths to prune residue classes.
- If a congruence class $n \pmod{2^w}$ forces a path prefix that contains a run of slashes longer than 6, or exhibits a Star-to-Slash ratio significantly below $1.70$, it is statistically highly unlikely to challenge the stopping time record.
- By incorporating run-length bounds into the static `fpoly` generation tables, we can filter out non-performing residue classes before launching the search.

### B. Pre-Stop Path Synthesis
Can we construct new candidates by prepending valid operations to existing stopping time peak paths?
- For example, if we can find a residue class that transitions via a long star run to a value that matches the start of a known sigma peak, we can synthesize extremely high stopping time numbers directly.
- The constraint is that the prepended path must remain above the synthesized starting value $N_{new}$ throughout the prepended segment.
