# Design Document: Hailstone Backwards Step Search

This document outlines the design and mathematical framework for a **Backwards Step Search** in the Collatz (hailstone) project. Instead of searching forward from starting numbers $n$ to find peak properties, a backwards step search traverses the Collatz tree in reverse starting from a target number (usually 1 or 2, or known peaks) to find starting numbers that generate specific path structures, exact step counts, or high intermediate peaks.

---

## 1. Overriding Principles

1. **Correctness Above All Else**: Since the backwards search involves navigating a highly branching tree with multiple math constraints, correctness of transition selection, path reconstruction, and overflow bounds is paramount.
2. **Computational Speed**: The tool must be designed for high performance. We will start with a single-threaded CPU reference implementation to validate the algorithm, and design it such that it can eventually be extended to multi-threaded CPU or GPU implementation if needed.
3. **Incremental and Lazy Elaboration**: Because the backwards tree grows exponentially (branching factor of up to 2 per step), we must approach search space generation lazily and incrementally, avoiding full tree expansion.
4. **Forward Pass Bounding**: Information from forward search passes (such as checkpoints containing max values/peaks for specific starting ranges) can act as a dynamic bound for the backwards elaboration. 
   - *Example*: For starting numbers less than $2^{16}$, the absolute largest intermediate value reached is $593,279,152$ (reached by starting value $60,975$). 
   - If we are searching for peak starting values within the $2^{16}$ range, any branch in the backwards step search that goes above $593,279,152$ is not immediately pruned permanently (discarded), but rather its elaboration is **deferred** (suspended/queued) until the search range boundary is expanded and the max value limit is raised.
   - This integrates forward-pass metadata directly into the deferred bounding/pruning logic of the backwards search.

---

## 2. Mathematical Foundation of Backwards Transitions

To search backwards, we invert the Collatz function. In the forward direction, the transition from $x_{i}$ to $x_{i+1}$ is:
- **Even Division**: $x_{i+1} = x_i / 2$ (if $x_i$ is even)
- **Odd Step**: $x_{i+1} = 3x_i + 1$ (if $x_i$ is odd)
- **Combined Step**: $x_{i+1} = (3x_i + 1) / 2$ (if $x_i$ is odd)

In the reverse direction, from a value $x$, the possible predecessors $x_{prev}$ are:

### A. Reverse Division (Even Step)
Every number $x$ has exactly one even predecessor:
$$x_{prev} = 2x$$
This transition is always valid for any integer $x$.

### B. Reverse Odd Step (Split Transition)
A number $x$ can have an odd predecessor via the split $3p+1$ operation if and only if:
1. $x - 1$ is divisible by 3:
   $$x \equiv 1 \pmod 3$$
2. The resulting predecessor is odd:
   $$x_{prev} = \frac{x - 1}{3} \equiv 1 \pmod 2$$

Since $x_{prev}$ must be odd, $x - 1$ must be odd-times-three (which is odd). Thus, $x$ must be even. 
Together, this implies $x$ must satisfy:
$$x \equiv 4 \pmod 6$$
If $x \equiv 4 \pmod 6$, then $x_{prev} = (x-1)/3$ is a valid odd predecessor.

### C. Reverse Combined Odd Step
If we use the combined representation $x \to (3x+1)/2$, the predecessor $x_{prev}$ is:
$$x_{prev} = \frac{2x - 1}{3}$$
This transition is valid if and only if:
1. $2x - 1$ is divisible by 3:
   $$2x \equiv 1 \pmod 3 \implies x \equiv 2 \pmod 3$$
2. The resulting predecessor is odd:
   $$x_{prev} = \frac{2x - 1}{3} \equiv 1 \pmod 2$$
   Since $2x - 1$ is always odd, dividing it by 3 (when divisible) always yields an odd number. Thus, the only requirement is:
   $$x \equiv 2 \pmod 3$$

## 3. Search Objectives & Search Methods

The primary search method is a **Breadth-First Search (BFS)** designed to construct a "max steps" lookup table. Rather than traversing the tree greedily or deeply, we build step-by-step distance layers from the root.

### Incremental & Lazy BFS Layers
To manage memory and compute bounds, we elaborate the BFS tree level-by-level:
1. **Level Structures**: Each "level" (representing exactly $d$ steps from the root) is represented as a structured set of values.
2. **Successor Generation**: We use the set of values at Level $d$ to populate the structures for Level $d+1$ using the reverse Collatz transitions.
3. **Lazy Elaboration / Deferral**: If any value generated for Level $d+1$ exceeds the current maximum value bound, its expansion is deferred. It remains queued in a deferred set for that level.
4. **Tracking Completion & Minima**: We keep track of what has been fully elaborated. Once a level is completed, we record the lowest starting value at that level (which represents the smallest starting integer requiring exactly that number of steps to reach the root).

---

## 4. High-Level Architecture

We propose introducing a new command-line tool or adding options to the existing utilities.

#### Component Design:
- **`BackwardsSearch` Module / Class**:
  - Manages the active levels and coordinates lazy elaboration.
  - Automatically auto-increments the target range $L$ and max bound $M$ when the active traversal queue for the current range is exhausted.
- **Level Representation**:
  - Because the reverse Collatz relation starting from 1 forms a strict tree (every number has exactly one forward successor, so no merges or duplicates can ever occur), no hash sets or deduplication structures are required.
  - Active nodes at each level are stored in a simple, flat `std::vector` of structures:
    ```cpp
    struct SearchNode {
        uint128 value;
        uint8_t flags; // e.g. bit 0: deferred_2x, bit 1: deferred_mul_3
    };
    ```
  - Instead of allocating memory for deferred child values, parents of deferred branches are kept in the current levels with their flags set to indicate which branches are deferred.
- **Pruning and Deferral Strategies**:
  - **Dynamic Value Bounding (Deferral)**: Suspending elaboration of branches whose values exceed $M$ by setting the deferred flags on their parent nodes, avoiding redundant value storage.
  - **Overflow Checking**: Handling bounds checking for 128-bit integers.
  - **Modular and Parity Checks**: Applying reverse modular transition rules to avoid illegal branches.

---

## 5. Search Limits and Termination Criteria

### A. Target Starting Value
The backwards search starts from **1** and proceeds in reverse to find all starting numbers $n$ that transition to 1.
- Note on step mapping: The split transition model is used to align with forward step counts. The reverse division $2x$ counts as **1 step** (advancing node to Level $d+1$), and the reverse combined odd step $(2x-1)/3$ counts as **2 steps** (advancing node directly to Level $d+2$).

### B. Search Goal: Steps Peaks Table
The primary output is to reconstruct/recreate the `steps_peaks` table (similar to the one in `golden_48.chk`). As the backward search continues level-by-level, it tracks the running record of steps and prints progress (new steps peaks found).

### C. Termination Criteria ("Done" for Range $L$)
For a given forward search range $L$, we extract the maximum intermediate value $M$ from the forward-pass checkpoints/metadata (e.g. `max_value_peaks`).
1. At any level $d$, any generated node $V > M$ is deferred (flags updated on parent node).
2. Any generated node $n < L$ is recognized as a starting number within our range and recorded with its step count $d$.
3. The search for the range $L$ is **completed** when the queue of active nodes $\le M$ for the current level is empty (all remaining active branches have either terminated or been deferred).

### D. Value Representation Bounds (First Stage: `uint64_t`, Future: `uint128`)
For the full scale, Collatz intermediate values utilize 128-bit unsigned integers (`uint128`). However, to keep computations fast, facilitate prototyping, and enable instrumentation of the algorithm, the **First Stage/Prototype** will target starting numbers $n < 2^{32}$ (Block 0):
- **Representation**: All values are stored and calculated using standard 64-bit unsigned integers (`uint64_t`).
- **Max-Value Bound $M$**: For $L = 2^{32}$, the absolute largest intermediate value reached is $7,125,885,122,794,452,160$ (reached by starting value $1,410,123,943$), which fits within a 64-bit unsigned integer ($2^{64} - 1 \approx 1.84 \times 10^{19}$).
- **Overflow Guards**: Explicit arithmetic overflow checks will be implemented on all reverse steps.
- **Future Phase**: In the next phase, the numeric representation and checkpoints will be expanded to `uint128` to support starting values up to $2^{64}$.

---

## 6. Checkpointing & Save/Restore

Since the backwards search can be extremely long-running, we require a robust checkpointing system.

### A. Saved State Components
To successfully resume the search without losing progress, the checkpoint must serialize:
1. **Search Parameters**: The current target range limit $L$, and the current max-value bound $M$.
2. **Current Traversal State**:
   - The current BFS level index $d$.
   - The list of active levels and their `SearchNode` sets (with value and deferral flags).
3. **Discovered Peaks**: The running `steps_peaks` table generated up to the current level.

### B. Serialization Format
The checkpoint format will be kept **simple and human-readable** (similar to the standard `.chk` files in the repository). This makes it easy to inspect progress, verify state, and see the list of steps directly. If memory scale requires it in the future, we will refactor to optimize serialization.

---

## 7. Memory Optimization and Instrumentation

As the backwards Collatz tree expands, the number of active nodes at each depth layer can grow exponentially. Space efficiency and performance monitoring are critical design considerations.

### A. Instrumentation & Reporting Metrics
To help diagnose growth patterns and discover potential optimizations, the tool will track and report:
1. **Level Active Size**: Number of active nodes in the current Level $d$ set.
2. **Level Deferred Size**: Number of deferred nodes in the queue for the current Level $d$.
3. **Total Deferred Footprint**: The cumulative size of all deferred queues across all levels.
4. **Memory Footprint**: Estimate of memory usage for the active and deferred structures.
5. **Pruning Rate**: Percentage of generated predecessors that are immediately pruned due to math/parity violations or bounds checks.
6. **Execution Velocity**: Rate of level elaboration (nodes generated/processed per second).

### B. Future Space & Search Optimizations
To support scaling to larger ranges (towards $2^{64}$), we will watch for and design interfaces to support:
1. **Bit-Packed/Compressed Value Representation**: Storing numbers efficiently if tree density allows.
2. **A Priori Suffix/Prefix Pruning**: Using mathematical rules (like those in Suffix-First search) to prune entire subtrees that can be proven to never produce values $\le L$.
3. **Lazy Subtree Expansion**: Postponing branches that show slow convergence or extreme growth patterns.
4. **Disk-Backed or Off-Heap Buffering**: Offloading large deferred queues or inactive level sets to disk to keep RAM usage bounded.

---

## 8. System Integration & Execution

We will build this as a CPU reference implementation (e.g. `hailstone_backwards` tool).
- **Automated Loop**: Runs indefinitely in an automated loop, auto-incrementing $L$ to the next boundary (loading $M$ dynamically from the master `.chk` file) when the active queue of the current range is exhausted.
- **Inputs**:
  - Checkpoint/Master file (like `golden_master.chk` or `golden_48.chk`) to load `max_value_peaks` and establish bounds for incremental ranges.
  - A backwards search checkpoint file (default: `hailstone_backwards.chk`) for saving/restoring search state.
- **Outputs**:
  - Reconstructed `steps_peaks` table matching the level traversal.
  - Real-time progress updates and memory/instrumentation metrics.
  - Periodic and final checkpoint saves.

---

## 9. References & Research Papers

- **Collatz Backwards Algorithms & Pruning**: [Efficient Calculation of the Collatz Conjecture](https://arxiv.org/abs/2602.10466) (arXiv:2602.10466). Focuses on backwards algorithms with heavy pruning (to be analyzed for potential transfer/adaptations in future scaling phases).
