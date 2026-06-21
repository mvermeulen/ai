# Mathematical Basis of Steps Peak Prediction in the Hailstone (Collatz) Search

This document presents the mathematical foundation for predicting future record-breaking peak trajectories (specifically steps peaks) during the Hailstone (Collatz) search, including both odd and even starting values.

---

## 1. Core Predecessor Equations

Let $X$ be a confirmed steps peak with step count $S_X$. In the Collatz trajectory graph, $X$ has predecessors that must pass through $X$ to reach $1$. These predecessors are candidates for larger starting numbers with more steps.

Since $X$ is a peak, we only consider numbers $Y > X$ that take the fewest additional steps to reach $X$:

### Case A: $X \equiv 0 \pmod 3$
* **Prediction:** $P = 2X$
* **Steps:** $S_P = S_X + 1$
* **Proof:** 
  The values leading to $X$ in a single step can only come from:
  1. An division step: $2X \to X$ (always valid).
  2. A multiplication step: $Y \to 3Y+1 = X$, which requires $3Y = X - 1$.
  
  Since $X \equiv 0 \pmod 3$, $X-1 \equiv 2 \pmod 3$, which is not divisible by 3. Thus, there is no integer $Y$ such that $3Y+1 = X$.
  
  The only direct predecessor is $2X$. Because $2X$ takes exactly 1 step to reach $X$, its total steps is $S_X + 1$.

### Case B: $X \equiv 1 \pmod 3$
* **Prediction:** $P = \frac{4X-1}{3}$
* **Steps:** $S_P = S_X + 3$
* **Proof:**
  When $X \equiv 1 \pmod 3$, both divisions and multiplications can lead to $X$.
  Let's trace the odd predecessor $P$ that reaches $X$ via a multiplication and two divisions:
  $$P \xrightarrow{3P+1} 4X \xrightarrow{/2} 2X \xrightarrow{/2} X$$
  
  This trajectory takes exactly 3 steps to reach $X$ from $P$. For $P$ to be an integer:
  $$3P + 1 = 4X \implies P = \frac{4X - 1}{3}$$
  
  Since $X \equiv 1 \pmod 3$, we have $4X - 1 \equiv 4(1) - 1 = 3 \equiv 0 \pmod 3$. Therefore, $P$ is always an integer. Additionally, because $X$ is odd, $P = \frac{4X-1}{3}$ is always odd.
  
  Since $P$ takes exactly 3 steps to reach $X$, its total steps is $S_X + 3$.

---

## 2. Domination & Exclusion Proofs

### Why $2X$ is Dominated when $X \equiv 1 \pmod 3$
If $X \equiv 1 \pmod 3$, we could theoretically predict $2X$ (steps: $S_X + 1$) and $P = \frac{4X-1}{3}$ (steps: $S_X + 3$).
However:
$$P = \frac{4X-1}{3} \approx 1.33X < 2X \quad (\text{for all } X \ge 1)$$

Because $P < 2X$ and $S_P = S_X + 3 > S_{2X} = S_X + 1$, the smaller number $P$ has strictly more steps than $2X$. By the time a sequential search reaches $2X$, it will have already encountered $P$. Since $P$ has more steps, $2X$ can never be a record-breaking peak.

Thus, $2X$ is dominated and excluded from the candidate pool. The only valid peak candidate is $P = \frac{4X-1}{3}$.

### Why $X \equiv 2 \pmod 3$ Cannot Be a Peak
Any odd starting number $X \equiv 2 \pmod 3$ can be written as $X = 6m + 5$ for some $m \ge 0$.
We can always construct a smaller odd number $X' = 4m + 3 < X$.
Tracing $X'$:
$$X' = 4m + 3 \xrightarrow{3x+1} 12m + 10 \xrightarrow{/2} 6m + 5 = X$$

Thus, $X'$ is smaller than $X$ but takes 2 steps to reach $X$, meaning $S_{X'} = S_X + 2 > S_X$. Since a smaller number $X'$ has more steps, $X$ can never be a peak in steps.

---

## 3. Sequential Prediction & Block Insertion Algorithm

When we search sequentially:
1. Maintain a list of **Active Predictions**, sorted by starting number.
2. When a new peak $X$ is found:
   - Generate its predicted peak $P$ (using the $0 \pmod 3$ or $1 \pmod 3$ rules).
   - Add $P$ to the active predictions.
   - Prune any active predictions $Q$ where $\text{Steps}(Q) \le \text{Steps}(X)$.
3. As the search completes up to $N$:
   - For any active prediction $P \le N$:
     - If $P$ was not pruned (meaning no peak with $\ge \text{Steps}(P)$ steps was found before $P$), then $P$ is **confirmed** as a peak.
     - Insert $P$ into the peaks list, update the current max steps, and generate predictions from $P$.
