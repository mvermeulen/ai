# Hailstone (Collatz) Search Program

This repository implements a high-performance, multi-backend Collatz (3x+1) peak search program. It extends the ideas described in the Leavens-Vermeulen (1992) paper to search for new peaks across three major statistics:
1. **Max Value**: The highest intermediate value reached during the trajectory.
2. **Steps (Total Stopping Time)**: The number of steps taken to reach 1.
3. **Stopping Time ($\sigma$)**: The number of steps taken to fall below the starting value.

To ensure safety and compatibility for values beyond $2^{64}$, calculations are executed using custom 128-bit unsigned integer arithmetic (`uint128`) with overflow checking.

---

## Vermeulen Polynomials & Steps Table Optimization

Vermeulen polynomials and precomputed lookup tables speed up the Collatz search by filtering and shortcutting calculations:

1. **Vermeulen Polynomials (`fpoly` and `mpoly`)**: For any starting value $x \equiv i \pmod{2^w}$, the sequence of odd/even steps is identical until $x$ has been divided by 2 exactly $w$ times.
   - **Final Polynomials (`fpoly`)**: Represents the final state of the residue class after exactly $w$ divisions by 2.
   - **Maximum Intermediate Polynomials (`mpoly`)**: Represents the peak intermediate state along the path where the ratio $\frac{3^a}{2^b}$ is maximized, used to bound peak intermediate values and prune search classes.
2. **Polynomial Step Lookup Optimization**: Instead of running trajectories all the way to 1, the search loops (CPU, HIP, and Vulkan) terminate early when the value drops below $2^8 = 256$. The remaining steps are retrieved in $O(1)$ time from a precomputed steps lookup table (`steps8`). This reduces loop iterations, thread divergence, and instruction counts, accelerating GPU execution by up to **4.0x** and CPU execution by **79%**.

---

## Architecture & Backends

The project is structured with three computational backends:
* **CPU Reference (`cpu/`)**: A clean C++ reference implementation serving as the golden standard of correctness.
* **Vulkan Compute (`gpu_vulkan/`)**: A cross-vendor GPU execution backend using Vulkan Compute shaders (`GL_EXT_shader_explicit_arithmetic_types_int64` and Kogge-Stone workgroup prefix scans).
* **HIP ROCm (`gpu_hip/`)**: A highly-optimized GPU backend targeted at AMD hardware (e.g. Strix Halo integrated systems).

To resolve peak buffer limits on the GPU side, the host executes the searches in consecutive chunks of size `2,000,000` values, feeding the global peak thresholds back to the device to prevent candidate log buffer overflows.

To optimize execution, searches lying completely within Block 0 (starting values under $2^{32}$) automatically run using native 64-bit integer arithmetic instead of the custom 128-bit integer type, yielding up to a 3x speedup across CPU and GPU backends.

---

## Directory Structure

```
hailstone/
├── benchmarks/
│   ├── benchmark.py       # Main Python automation benchmark script
│   ├── history.json       # Historical performance logs
│   └── README.md          # Execution record of optimized iterations
├── cpu/                   # CPU Reference implementation
├── doc/                   # Original reference materials
├── gpu_hip/               # AMD HIP (ROCm) backend implementation
├── gpu_vulkan/            # Vulkan Compute backend implementation
├── include/               # Shared headers (uint128 definition and common structs)
├── tests/                 # Unit tests (uint128 verification, differential testing)
├── CMakeLists.txt         # Root CMake build configuration
└── README.md              # This file
```

---

## How to Build

### Prerequisites
* A C++20 compatible compiler (GCC/Clang)
* Vulkan SDK (headers and loader)
* `glslc` (shader compiler, usually included in the Vulkan SDK)
* CMake (version 3.16+)
* AMD ROCm Toolkit (optional, for HIP backend)

### Build Steps
1. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```
2. Configure the project:
   ```bash
   cmake -DCMAKE_BUILD_TYPE=Release ..
   ```
3. Compile:
   ```bash
   make -j$(nproc)
   ```

This will build:
* `hailstone_cpu`: CPU reference executable
* `hailstone_vulkan`: Vulkan Compute executable
* `hailstone_verify`: Cross-backend verification executable
* `hailstone_test_uint128`: `uint128` math unit tests
* `hailstone_hip` (if ROCm is found): AMD HIP executable

---

## Usage & Command-Line Options

The search executables (`hailstone_cpu`, `hailstone_vulkan`, and `hailstone_hip`) accept command-line options to configure the search range. We define a **block** as $2^{32}$ items.

### Available Options
* `-h, --help`: Show help and usage instructions.
* `--start-num, --start_num VALUE`: Starting number of the search range (default: `3`).
* `--end-num, --end_num VALUE`: Ending number of the search range (default: `100000`).
* `--start-block, --start_block INDEX`: Starting block index (each block is $2^{32}$ items, overrides `--start-num`).
* `--end-block, --end_block INDEX`: Ending block index (each block is $2^{32}$ items, overrides `--end-num`).
* `--num-blocks, --num_blocks COUNT`: Number of blocks to check (each block is $2^{32}$ items, overrides `--end-num`/`--end-block`).
* `--checkpoint, --checkpoint_file FILE`: Checkpoint file path (default: `hailstone.chk`).
* `--no-checkpoint, --no_checkpoint`: Disable saving and restoring checkpoints.

### Checkpointing & Resumption
State checkpointing is enabled by default. Before completing, search programs save the baseline peak thresholds, the list of all peaks, and the last number checked to a checkpoint file.
- When continuing a search, running the binary with no starting bound options will resume from `last_num + 1` of the checkpoint.
- If no ending boundary is provided on a resumed run, the search range size defaults to `100,000` numbers.
- If an explicit starting range (`--start-num` / `--start-block`) is passed, the search will respect that start point, but will still load baseline peak thresholds from the checkpoint.

*Note: Backward-compatible positional arguments (`[start] [end]`) are still supported as a fallback if no named range options are provided.*

### Examples
1. **Numeric Range** (ignoring checkpoints):
   ```bash
   ./hailstone_cpu --no-checkpoint --start-num 3 --end-num 100000
   ```
2. **Resuming Search from Checkpoint**:
   ```bash
   # Resume and run for another 100,000 numbers from where the default checkpoint left off
   ./hailstone_cpu
   ```
3. **Block Range** (e.g. 8GB test configuration covering blocks 0 and 1):
   ```bash
   ./hailstone_vulkan --start-block 0 --num-blocks 2
   ```
4. **Legacy Positional Fallback**:
   ```bash
   ./hailstone_hip 3 100000
   ```

---

## Verification & Benchmarks

### 1. Run Verification Suite
Before benchmarking, always verify backend correctness. Run the differential integration test from the build directory:
```bash
./hailstone_verify
```

### 2. Run Benchmarks
Use the Python benchmark driver from the root directory to run benchmarks and log performance metrics:
```bash
# Run a quick check (100M values) to verify performance
python3 benchmarks/benchmark.py --mode quick

# Run the full benchmark run up to 2^32 * 2 (8,589,934,592)
python3 benchmarks/benchmark.py --mode full
```

Benchmark results and peak counts will automatically append to the optimization history in [benchmarks/README.md](file:///home/mev/source/ai/hailstone/benchmarks/README.md) and [benchmarks/history.json](file:///home/mev/source/ai/hailstone/benchmarks/history.json).
