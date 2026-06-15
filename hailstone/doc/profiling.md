# CPU Profiling Guide

This document details how to build and profile the Hailstone executables to analyze microarchitectural and code-level performance (including source lines mapping with `perf`).

---

## 1. Profiling Build Configurations
Separate profiling targets are configured in `CMakeLists.txt` alongside standard Release builds. These are named with a `_profile` suffix:
- `hailstone_cpu_profile` (CPU search)
- `hailstone_verify_profile` (backend verification suite)
- `hailstone_path_profile` (path representation utility)
- `hailstone_vulkan_profile` (if Vulkan is enabled)
- `hailstone_hip_profile` (if HIP is enabled)

These targets maintain full Release optimizations (`-O3 -march=native`) but compile and link with `-g` and `-fno-omit-frame-pointer` flags. This ensures we profile actual production-like compiler optimizations while preserving stack frame pointers for clean call graphs and source line maps.

---

## 2. Building Profile Targets
To build the profiling targets, run the following commands:
```bash
# Configure the build (Release mode is required for benchmarking)
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build all targets (including profile targets)
make -j$(nproc)

# Or build only the CPU profiling binary
make hailstone_cpu_profile -j$(nproc)
```

---

## 3. Recording Performance with `perf`
Run the profile binary under `perf record` to gather CPU samples:
```bash
# Record CPU cycles with call graph generation (--call-graph fp)
perf record -g -- ./hailstone_cpu_profile --start-num 3 --end-num 50000000 --no-checkpoint
```
*Note: Use `--no-checkpoint` to run a clean search range, or `--no-save-checkpoint` if you want to warm-start using the checkpoint without modifying it.*

---

## 4. Viewing Reports with Source Lines
To view the recorded profile mapped to source lines and assembly:
```bash
perf report --annotate
```

Inside the interactive TUI:
1. Locate the hot functions (e.g., `cpu_search_range` or `cpu_search_range_suffix_first`).
2. Press **Enter** or **a** to annotate the function.
3. You will see C++ source code lines side-by-side with the compiled assembly instructions and the percentage of cycles spent on each line.

> [!NOTE]
> If you run into permission errors when using `perf record` (e.g. `perf_event_paranoid` restrictions), you may need to adjust the Linux performance kernel setting:
> ```bash
> sudo sysctl -w kernel.perf_event_paranoid=1
> ```
