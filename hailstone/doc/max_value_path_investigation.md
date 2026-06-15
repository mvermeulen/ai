# Investigation: Paths Taken by Peaks in Maximum Values

This document details the trajectory path properties of record-breaking maximum value peaks (frequently called max value peaks) in the Collatz search. Building on [Trajectory Path Representation and Optimization Opportunities](file:///home/mev/source/ai/hailstone/doc/path_investigation_and_opportunities.md), we explore the structural patterns of their paths before and after reaching the global peak, analyzing mathematical constraints, run behaviors, and substring containment relations with steps peaks and stopping time ($\sigma$) peaks.

---

## 1. Summary Statistics of Max Value Peaks

For a starting value $n$, the max value peak is reached at the global maximum intermediate value in the trajectory, marked by `^` in the path representation. We examine the trajectory split into the **Climb (Pre-Peak)** segment and the **Descent (Post-Peak)** segment for all 62 historical maximum value peaks:

| Starting Number ($n$) | Peak Value | Climb Stars / Slashes | Climb Ratio | Descent Stars / Slashes | Descent Ratio |
| :--- | :--- | :---: | :---: | :---: | :---: |
| 3 | 16 | 2 / 0 | N/A | 0 / 3 | 0.000 |
| 7 | 52 | 3 / 0 | N/A | 2 / 6 | 0.333 |
| 15 | 160 | 4 / 0 | N/A | 1 / 7 | 0.143 |
| 27 | 9,232 | 33 / 12 | 2.750 | 8 / 17 | 0.471 |
| 255 | 13,120 | 8 / 0 | N/A | 7 / 17 | 0.412 |
| 447 | 39,364 | 23 / 8 | 2.875 | 11 / 21 | 0.524 |
| 639 | 41,524 | 12 / 2 | 6.000 | 35 / 35 | 1.000 |
| 703 | 250,504 | 35 / 13 | 2.692 | 27 / 33 | 0.818 |
| 1,819 | 1,276,936 | 23 / 5 | 4.600 | 35 / 40 | 0.875 |
| 4,255 | 6,810,136 | 37 / 12 | 3.083 | 36 / 43 | 0.837 |
| 4,591 | 8,153,620 | 27 / 6 | 4.500 | 34 / 42 | 0.810 |
| 9,663 | 27,114,424 | 23 / 3 | 7.667 | 43 / 49 | 0.878 |
| 20,895 | 50,143,264 | 38 / 12 | 3.167 | 55 / 57 | 0.965 |
| 26,623 | 106,358,020 | 29 / 6 | 4.833 | 84 / 75 | 1.120 |
| 31,911 | 121,012,864 | 34 / 9 | 3.778 | 22 / 39 | 0.564 |
| 60,975 | 593,279,152 | 50 / 17 | 2.941 | 73 / 71 | 1.028 |
| 77,671 | 1,570,824,736 | 33 / 6 | 5.500 | 50 / 59 | 0.847 |
| 113,383 | 2,482,111,348 | 52 / 17 | 3.059 | 37 / 52 | 0.712 |
| 138,367 | 2,798,323,360 | 33 / 6 | 5.500 | 23 / 44 | 0.523 |
| 159,487 | 17,202,377,752 | 32 / 3 | 10.667 | 32 / 52 | 0.615 |
| 270,271 | 24,648,077,896 | 88 / 36 | 2.444 | 62 / 70 | 0.886 |
| 665,215 | 52,483,285,312 | 62 / 21 | 2.952 | 101 / 94 | 1.074 |
| 704,511 | 56,991,483,520 | 33 / 4 | 8.250 | 53 / 66 | 0.803 |
| 1,042,431 | 90,239,155,648 | 93 / 39 | 2.385 | 69 / 76 | 0.908 |
| 1,212,415 | 139,646,736,808 | 39 / 7 | 5.571 | 80 / 83 | 0.964 |
| 1,441,407 | 151,629,574,372 | 61 / 20 | 3.050 | 73 / 79 | 0.924 |
| 1,875,711 | 155,904,349,696 | 57 / 18 | 3.167 | 78 / 82 | 0.951 |
| 1,988,859 | 156,914,378,224 | 62 / 21 | 2.952 | 95 / 92 | 1.033 |
| 2,643,183 | 190,459,818,484 | 84 / 34 | 2.471 | 74 / 80 | 0.925 |
| 2,684,647 | 352,617,812,944 | 53 / 15 | 3.533 | 93 / 92 | 1.011 |
| 3,041,127 | 622,717,901,620 | 37 / 5 | 7.400 | 95 / 94 | 1.011 |
| 3,873,535 | 858,555,169,576 | 56 / 16 | 3.500 | 60 / 74 | 0.811 |
| 4,637,979 | 1.319e+12 | 72 / 25 | 2.880 | 141 / 122 | 1.156 |
| 5,656,191 | 2.412e+12 | 73 / 25 | 2.920 | 73 / 83 | 0.880 |
| 6,416,623 | 4.800e+12 | 59 / 16 | 3.688 | 119 / 111 | 1.072 |
| 6,631,675 | 6.034e+13 | 72 / 20 | 3.600 | 142 / 128 | 1.109 |
| 19,638,399 | 3.063e+14 | 140 / 59 | 2.373 | 85 / 97 | 0.876 |
| 38,595,583 | 4.746e+14 | 83 / 26 | 3.192 | 94 / 103 | 0.913 |
| 80,049,391 | 2.185e+15 | 73 / 19 | 3.842 | 138 / 131 | 1.053 |
| 120,080,895 | 3.278e+15 | 73 / 19 | 3.842 | 86 / 101 | 0.851 |
| 210,964,383 | 6.405e+15 | 116 / 44 | 2.636 | 57 / 85 | 0.671 |
| 319,804,831 | 1.414e+18 | 77 / 14 | 5.500 | 141 / 142 | 0.993 |
| 1,410,123,943 | 7.126e+18 | 144 / 53 | 2.717 | 142 / 145 | 0.979 |
| 8,528,817,511 | 1.814e+19 | 94 / 25 | 3.760 | 174 / 165 | 1.055 |
| 12,327,829,503 | 2.072e+19 | 90 / 23 | 3.913 | 107 / 126 | 0.849 |
| 23,035,537,407 | 6.884e+19 | 88 / 21 | 4.190 | 222 / 195 | 1.138 |
| 45,871,962,271 | 8.234e+19 | 97 / 27 | 3.593 | 104 / 126 | 0.825 |
| 51,739,336,447 | 1.146e+20 | 130 / 46 | 2.826 | 154 / 156 | 0.987 |
| 59,152,641,055 | 1.515e+20 | 156 / 61 | 2.557 | 167 / 164 | 1.018 |
| 59,436,135,663 | 2.057e+20 | 167 / 67 | 2.493 | 127 / 141 | 0.901 |
| 70,141,259,775 | 4.210e+20 | 194 / 82 | 2.366 | 221 / 197 | 1.122 |
| 77,566,362,559 | 9.166e+20 | 129 / 43 | 3.000 | 149 / 156 | 0.955 |
| 110,243,094,271 | 1.372e+21 | 124 / 40 | 3.100 | 83 / 118 | 0.703 |
| 204,430,613,247 | 1.415e+21 | 167 / 66 | 2.530 | 124 / 142 | 0.873 |
| 231,913,730,799 | 2.190e+21 | 84 / 17 | 4.941 | 128 / 145 | 0.883 |
| 272,025,660,543 | 2.195e+22 | 91 / 18 | 5.056 | 141 / 156 | 0.904 |
| 446,559,217,279 | 3.953e+22 | 122 / 36 | 3.389 | 167 / 172 | 0.971 |
| 567,839,862,631 | 1.005e+23 | 122 / 35 | 3.486 | 168 / 174 | 0.966 |
| 871,673,828,443 | 4.006e+23 | 97 / 19 | 5.105 | 139 / 159 | 0.874 |
| 2,674,309,547,647 | 7.704e+23 | 195 / 77 | 2.532 | 187 / 188 | 0.995 |
| 3,716,509,988,199 | 2.079e+26 | 155 / 46 | 3.370 | 139 / 168 | 0.827 |
| 9,016,346,070,511 | 2.522e+26 | 155 / 47 | 3.298 | 171 / 187 | 0.914 |

---

## 2. The Duality of Max Value Paths: Climb vs. Descent

A record-breaking maximum value trajectory is characterized by a stark mathematical split around its global peak (`^`).

### A. The Climb (Hyper-Ascent Phase)
During the pre-peak phase, the trajectory is optimized strictly for vertical growth. This requires climbing as fast as possible, which translates to a extremely high ratio of combined odd steps (`*`) to division steps (`/`):
- For large starting numbers, the Climb Ratio ($*//$) ranges from **2.3 to over 5.1**, with an average of **~3.5**.
- For $n = 871,673,828,443$, the trajectory undergoes **97 odd steps and only 19 divisions** before hitting its peak of $4.006 \times 10^{23}$ (ratio **5.105**).
- For $n = 272,025,660,543$, the trajectory undergoes **91 odd steps and only 18 divisions** (ratio **5.056**).

This represents a "hyper-ascent" where the number of divisions is kept to an absolute minimum, allowing the value to grow by a factor of roughly $(1.5)^{n_*} \times (0.5)^{n_/} \approx 1.5^{97} \times 0.5^{19} \approx 4.6 \times 10^{16} \times 1.9 \times 10^{-6} \approx 8.7 \times 10^{10}$ times its starting value.

### B. The Descent (Decay Phase)
Once the peak is reached, the trajectory enters its post-peak phase, which is characterized by a rapid collapse. 
- The Descent Ratio ($*//$) is consistently very low, ranging from **0.70 to 1.14**, with a steady average around **~0.90**.
- Since $0.90 < 1.7095$, the division operations dominate, causing the value to shrink rapidly until it merges into the lower-order cycle.

---

## 3. Ordering of Peaks and Stopping Times: The Duality of Landmarks

### A. Max Value and Stopping Time Peaks: Strict Peak-Before-Stop
In **100% of the 62 max value peaks** (and also 100% of stopping time peaks), the peak `^` is reached **before** the stopping time `|` is resolved. 
This is a mathematical necessity:
1. The stopping time is the *first* step where the value falls below the starting value $n$ ($x_k < n$).
2. The peak value $P_{peak}$ is the global maximum of the entire trajectory.
3. For all max value peaks, $P_{peak} > n$ (as they immediately climb on the first odd step).
4. At the step $p$ where the peak is reached, the value is $P_{peak} > n$. Since $P_{peak}$ is not less than $n$, the stopping time has not yet been resolved.
5. Therefore, the stopping time can only occur at some step $s > p$ (after the peak has been passed).

### B. Steps Peaks: Stop-Before-Peak Inheritance
In contrast, in **70% of steps peaks** (72 out of 103), the stopping time `|` is reached **before** the peak `^` is encountered.
- This occurs because steps peaks prioritize the *total* number of steps. 
- A number $n$ can become a steps peak by being a predecessor of a smaller, already-known record trajectory (e.g. $n \to n'$ in $k$ steps where $n' < n$).
- Since the trajectory immediately drops below $n$ to $n'$ early in the transition, the stopping time is resolved after very few steps.
- However, the peak of the inherited trajectory is reached much later (inside $n'$'s segment), resulting in `|` appearing before `^`.
- Example: Steps peak $6 \to 3 \to 10 \to 5 \to 16 \to 8 \to 4 \to 2 \to 1$. The stopping time is resolved at step 1 ($3 < 6$), but the peak 16 is reached at step 4.

Max value peaks can **never** inherit their peaks from smaller trajectories because their peak value must strictly exceed all previous peak values. Thus, they must climb to their peaks autonomously before any merge happens.

---

## 4. Substring Comparisons & Containment

### A. Pre-Peak Containment (Autonomy Constraint)
Because a max value peak must establish a new record peak value, its pre-peak path cannot collide/merge with any previous trajectory prior to reaching its peak (otherwise it would be capped at that previous trajectory's peak). 
- Consequently, the pre-peak paths of max value peaks are **completely autonomous**.
- Pairwise comparison of pre-peak paths shows no significant suffix matches (the longest suffix match between any two distinct pre-peaks is only **16 characters**).
- There is **no containment** of any large max value pre-peak paths inside steps peaks or stopping time peaks.

### B. Post-Peak Suffixes (Graph Merges)
Once the peak is passed, the trajectory is descending. Because the Collatz graph is a tree where branches merge into a few main trunks, descending paths collide frequently:
- **Massive Post-Peak Suffixes**: Distinct max value peaks share very long suffixes of their post-peak paths. 
  - $1,988,859$ and $23,035,537,407$ share a **91-character suffix**.
  - $2,684,647$ and $8,528,817,511$ share an **88-character suffix**.
  - $8,528,817,511$ and $23,035,537,407$ share an **84-character suffix**.

This indicates that during their descent, their paths merge and they follow the exact same sequence of divisions and multiplications down to 2.

### C. Family Merges with Steps and Stopping Time Peaks
There are exactly **8 families** where a Max Value Peak reaches the exact same peak value as a record-breaking Steps Peak or Stopping Time (Sigma) Peak:

1. **Peak Value 16**: Max Value Peak `3`, Steps Peaks `[3, 6]`, Sigma Peak `[3]`.
2. **Peak Value 52**: Max Value Peak `7`, Steps Peaks `[7, 9, 18]`, Sigma Peak `[7]`.
3. **Peak Value 9,232**: Max Value Peak `27`, Steps Peaks `[27, 54, 73, 97, 129, 171, 231, 313, 327, 649]`, Sigma Peak `[27]`.
4. **Peak Value 250,504**: Max Value Peak `703`, Steps Peaks `[703, 2223, 2463, 2919]`, Sigma Peak `[703]`.
5. **Peak Value 106,358,020**: Max Value Peak `26,623`, Steps Peaks `[26623, 52,527]`.
6. **Peak Value 593,279,152**: Max Value Peak `60,975`, Steps Peak `[142,587]`.
7. **Peak Value 24,648,077,896**: Max Value Peak `270,271`, Sigma Peak `[270271]`.
8. **Peak Value 90,239,155,648**: Max Value Peak `1,042,431`, Steps Peak `[1,501,353]`, Sigma Peak `[1,126,015]`.

For these families, the steps peak merges with the max value peak **exactly at its peak**. 
- Because they share the peak value, the steps peak trajectory contains the **entire** post-peak path of the corresponding max value peak.
- For example, the 144-character post-peak path of $60,975$ is exactly contained in steps peak $142,587$.
- The 145-character post-peak path of $1,042,431$ is exactly contained in steps peak $1,501,353$.

For all other 54 max value peaks, their post-peak paths are **never** contained in steps peaks because the steps peaks never reach their high peak values (they only merge with their descents at much lower values, sharing suffixes but not the full post-peak paths).
