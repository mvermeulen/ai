# Hailstone (Collatz) Search Program

This repository implements a high-performance, multi-backend Collatz (3x+1) peak search program. It extends the ideas described in the Leavens-Vermeulen (1992) paper to search for new peaks across three major statistics:
1. **Max Value**: The highest intermediate value reached during the trajectory.
2. **Steps (Total Stopping Time)**: The number of steps taken to reach 1.
3. **Stopping Time ($\sigma$)**: The number of steps taken to fall below the starting value.

To ensure safety and compatibility for values beyond $2^{64}$, calculations are executed using custom 128-bit unsigned integer arithmetic (`uint128`) with overflow checking.

---

## Architecture & Backends

The project is structured with three computational backends:
* **CPU Reference (`cpu/`)**: A clean C++ reference implementation serving as the golden standard of correctness.
* **Vulkan Compute (`gpu_vulkan/`)**: A cross-vendor GPU execution backend using Vulkan Compute shaders (`GL_EXT_shader_explicit_arithmetic_types_int64` and Kogge-Stone workgroup prefix scans).
* **HIP ROCm (`gpu_hip/`)**: A highly-optimized GPU backend targeted at AMD hardware (e.g. Strix Halo integrated systems).

To resolve peak buffer limits on the GPU side, the host executes the searches in consecutive chunks of size `2,000,000` values, feeding the global peak thresholds back to the device to prevent candidate log buffer overflows.

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
