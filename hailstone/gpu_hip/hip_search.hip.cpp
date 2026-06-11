#include "hip_search.hip.h"
#include <hip/hip_runtime.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <stdexcept>

#define MAX_PEAK_RECORDS 65536

#define HIP_CHECK(command) \
    do { \
        hipError_t status = command; \
        if (status != hipSuccess) { \
            std::cerr << "HIP Error: " << hipGetErrorString(status) \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            exit(1); \
        } \
    } while (0)

template <bool USE_64BIT>
__global__ void collatz_search_kernel(
    uint128 start,
    uint64_t total_odds,
    PeakRecord* max_value_peaks,
    int* max_value_count,
    PeakRecord* steps_peaks,
    int* steps_count,
    PeakRecord* sigma_peaks,
    int* sigma_count,
    PeakState* global_peaks,
    SearchMetrics* metrics
) {
    __shared__ uint64_t shared_max_val_low[256];
    __shared__ uint64_t shared_max_val_high[256];
    __shared__ uint32_t shared_steps[256];
    __shared__ uint32_t shared_sigma[256];

    // Read initial global peaks at start of kernel to filter local candidates
    uint128 init_max_val = global_peaks->current_max_value;
    uint32_t init_max_steps = global_peaks->current_max_steps;
    uint32_t init_max_sigma = global_peaks->current_max_sigma;

    int l_id = threadIdx.x;
    int g_id = blockIdx.x * blockDim.x + threadIdx.x;

    bool active_thread = (g_id < total_odds);

    // Compute the starting odd integer n
    uint128 n(0, 0);
    uint64_t n_64 = 0ULL;

    if (USE_64BIT) {
        n_64 = start.low + (uint64_t)g_id * 2ULL;
        if (active_thread) {
            if (n_64 % 3 == 2) {
                atomicAdd(&(metrics->numbers_skipped_mod6), 1);
                atomicAdd(&(metrics->total_numbers_checked), 1);
                active_thread = false;
            }
        }
    } else {
        n = start + uint128(g_id * 2);
        if (active_thread) {
            uint64_t sum_mod3 = (n.high % 3 + n.low % 3) % 3;
            if (sum_mod3 == 2) {
                atomicAdd(&(metrics->numbers_skipped_mod6), 1);
                atomicAdd(&(metrics->total_numbers_checked), 1);
                active_thread = false;
            }
        }
    }

    uint32_t steps = 0;
    uint32_t stopping_time = 0;
    uint128 max_val(0, 0);
    uint64_t max_val_64 = 0ULL;
    bool overflowed = false;

    if (active_thread) {
        if (USE_64BIT) {
            max_val_64 = n_64;
            if (n_64 == 1) {
                steps = 0;
                stopping_time = 0;
                max_val_64 = 1;
            } else if (n_64 == 2) {
                steps = 1;
                stopping_time = 1;
                max_val_64 = 2;
            } else {
                uint64_t curr = n_64;
                uint32_t t_steps = 0;
                bool has_stopped_sigma = false;
                bool dropped_below_start = false;

                while (curr > 1) {
                    if (curr > 0x5555555555555555ULL) {
                        overflowed = true;
                        break;
                    }
                    uint64_t next_val = 3 * curr + 1;
                    steps++;

                    if (!dropped_below_start) {
                        if (next_val > max_val_64) {
                            max_val_64 = next_val;
                        }
                    }

                    int p = __builtin_ctzll(next_val);
                    if (!has_stopped_sigma) {
                        for (int k = 1; k <= p; ++k) {
                            uint64_t val_k = next_val >> k;
                            if (val_k < n_64) {
                                stopping_time = t_steps + k;
                                has_stopped_sigma = true;
                                break;
                            }
                        }
                    }

                    curr = next_val >> p;
                    steps += p;
                    t_steps += p;

                    if (curr < n_64) {
                        dropped_below_start = true;
                    }
                }
            }
        } else {
            max_val = n;
            uint128 one(1);
            uint128 two(2);

            if (n == one) {
                steps = 0;
                stopping_time = 0;
                max_val = one;
            } else if (n == two) {
                steps = 1;
                stopping_time = 1;
                max_val = two;
            } else {
                uint128 curr = n;
                uint32_t t_steps = 0;
                bool has_stopped_sigma = false;
                bool dropped_below_start = false;

                while (curr > one) {
                    bool overflow = false;
                    uint128 next_val = mul3_add1(curr, overflow);
                    if (overflow) {
                        overflowed = true;
                        break;
                    }
                    steps++;

                    if (!dropped_below_start) {
                        if (next_val > max_val) {
                            max_val = next_val;
                        }
                    }

                    int p = count_trailing_zeros(next_val);
                    if (!has_stopped_sigma) {
                        for (int k = 1; k <= p; ++k) {
                            uint128 val_k = shift_right(next_val, k);
                            if (val_k < n) {
                                stopping_time = t_steps + k;
                                has_stopped_sigma = true;
                                break;
                            }
                        }
                    }

                    next_val = shift_right(next_val, p);
                    steps += p;
                    t_steps += p;
                    curr = next_val;

                    if (curr < n) {
                        dropped_below_start = true;
                    }
                }
            }
        }
    }

    if (active_thread) {
        atomicAdd((unsigned long long*)&(metrics->total_numbers_checked), 1ULL);
        if (overflowed) {
            atomicAdd((unsigned long long*)&(metrics->numbers_overflowed), 1ULL);
            active_thread = false;
        } else {
            atomicAdd((unsigned long long*)&(metrics->total_steps_computed), (unsigned long long)steps);
        }
    }

    if (active_thread && USE_64BIT) {
        max_val = uint128(max_val_64);
        n = uint128(n_64);
    }

    // Workgroup reduction & prefix scan to log local peaks
    shared_max_val_low[l_id] = active_thread ? max_val.low : 0ULL;
    shared_max_val_high[l_id] = active_thread ? max_val.high : 0ULL;
    shared_steps[l_id] = active_thread ? steps : 0;
    shared_sigma[l_id] = active_thread ? stopping_time : 0;
    __syncthreads();

    // 1. Prefix scan for max_val
    for (uint32_t stride = 1; stride < 256; stride *= 2) {
        uint64_t temp_low = 0ULL;
        uint64_t temp_high = 0ULL;
        if (l_id >= stride) {
            temp_low = shared_max_val_low[l_id - stride];
            temp_high = shared_max_val_high[l_id - stride];
        }
        __syncthreads();
        if (l_id >= stride) {
            uint128 temp(temp_low, temp_high);
            uint128 my_val(shared_max_val_low[l_id], shared_max_val_high[l_id]);
            if (temp > my_val) {
                shared_max_val_low[l_id] = temp.low;
                shared_max_val_high[l_id] = temp.high;
            }
        }
        __syncthreads();
    }

    // 2. Prefix scan for steps
    for (uint32_t stride = 1; stride < 256; stride *= 2) {
        uint32_t temp_steps = 0;
        if (l_id >= stride) {
            temp_steps = shared_steps[l_id - stride];
        }
        __syncthreads();
        if (l_id >= stride) {
            if (temp_steps > shared_steps[l_id]) {
                shared_steps[l_id] = temp_steps;
            }
        }
        __syncthreads();
    }

    // 3. Prefix scan for sigma
    for (uint32_t stride = 1; stride < 256; stride *= 2) {
        uint32_t temp_sigma = 0;
        if (l_id >= stride) {
            temp_sigma = shared_sigma[l_id - stride];
        }
        __syncthreads();
        if (l_id >= stride) {
            if (temp_sigma > shared_sigma[l_id]) {
                shared_sigma[l_id] = temp_sigma;
            }
        }
        __syncthreads();
    }

    if (active_thread) {
        // Max value local peak check
        uint128 prev_max_val = (l_id == 0) ? uint128(0, 0) : uint128(shared_max_val_low[l_id - 1], shared_max_val_high[l_id - 1]);

        if (max_val > prev_max_val) {
            if (max_val > init_max_val) {
                int idx = atomicAdd(max_value_count, 1);
                if (idx < MAX_PEAK_RECORDS) {
                    max_value_peaks[idx].start_val = n;
                    max_value_peaks[idx].metric_val = max_val;
                }

                if (max_val > global_peaks->current_max_value) {
                    global_peaks->current_max_value = max_val;
                }
            }
        }

        // Steps local peak check
        uint32_t prev_steps = (l_id == 0) ? 0 : shared_steps[l_id - 1];
        if (steps > prev_steps) {
            if (steps > init_max_steps) {
                int idx = atomicAdd(steps_count, 1);
                if (idx < MAX_PEAK_RECORDS) {
                    steps_peaks[idx].start_val = n;
                    steps_peaks[idx].metric_val = uint128(steps);
                }

                atomicMax(&(global_peaks->current_max_steps), steps);
            }
        }

        // Sigma local peak check
        uint32_t prev_sigma = (l_id == 0) ? 0 : shared_sigma[l_id - 1];
        if (stopping_time > prev_sigma) {
            if (stopping_time > init_max_sigma) {
                int idx = atomicAdd(sigma_count, 1);
                if (idx < MAX_PEAK_RECORDS) {
                    sigma_peaks[idx].start_val = n;
                    sigma_peaks[idx].metric_val = uint128(stopping_time);
                }

                atomicMax(&(global_peaks->current_max_sigma), stopping_time);
            }
        }
    }
}

static void filter_peaks(std::vector<PeakRecord>& peaks, int count) {
    peaks.resize(std::min(count, (int)MAX_PEAK_RECORDS));
    std::sort(peaks.begin(), peaks.end(), [](const PeakRecord& a, const PeakRecord& b) {
        return a.start_val < b.start_val;
    });

    std::vector<PeakRecord> filtered;
    uint128 running_max(0, 0);
    for (const auto& peak : peaks) {
        if (peak.metric_val > running_max) {
            running_max = peak.metric_val;
            filtered.push_back(peak);
        }
    }
    peaks = std::move(filtered);
}

void hip_search_range(
    uint128 start,
    uint128 end,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
) {
    // Force start to be odd
    if (start.low % 2 == 0) {
        start = start + uint128(1);
    }
    if (start > end) return;

    // Allocate GPU buffers once
    PeakRecord* d_max_val_peaks;
    PeakRecord* d_steps_peaks;
    PeakRecord* d_sigma_peaks;
    int* d_max_val_count;
    int* d_steps_count;
    int* d_sigma_count;
    PeakState* d_global_peaks;
    SearchMetrics* d_metrics;

    HIP_CHECK(hipMalloc(&d_max_val_peaks, MAX_PEAK_RECORDS * sizeof(PeakRecord)));
    HIP_CHECK(hipMalloc(&d_steps_peaks, MAX_PEAK_RECORDS * sizeof(PeakRecord)));
    HIP_CHECK(hipMalloc(&d_sigma_peaks, MAX_PEAK_RECORDS * sizeof(PeakRecord)));
    HIP_CHECK(hipMalloc(&d_max_val_count, sizeof(int)));
    HIP_CHECK(hipMalloc(&d_steps_count, sizeof(int)));
    HIP_CHECK(hipMalloc(&d_sigma_count, sizeof(int)));
    HIP_CHECK(hipMalloc(&d_global_peaks, sizeof(PeakState)));
    HIP_CHECK(hipMalloc(&d_metrics, sizeof(SearchMetrics)));

    // Master statistics/metrics
    SearchMetrics masterMetrics = {0};
    PeakState masterPeaks = global_peaks;
    std::vector<PeakRecord> masterMaxValPeaks = max_value_peaks;
    std::vector<PeakRecord> masterStepsPeaks = steps_peaks;
    std::vector<PeakRecord> masterSigmaPeaks = sigma_peaks;

    const uint64_t CHUNK_SIZE = 2000000;
    uint128 current_chunk_start = start;

    double total_kernel_time = 0.0;
    uint64_t total_odds_checked = 0;

    while (current_chunk_start <= end) {
        uint128 current_chunk_end = current_chunk_start + uint128(CHUNK_SIZE - 1);
        if (current_chunk_end > end) {
            current_chunk_end = end;
        }

        uint128 chunk_start_val = current_chunk_start;
        if (chunk_start_val.low % 2 == 0) {
            chunk_start_val = chunk_start_val + uint128(1);
        }

        uint128 chunk_end_val = current_chunk_end;
        if (chunk_end_val >= chunk_start_val) {
            if (chunk_end_val.low % 2 == 0) {
                chunk_end_val = chunk_end_val - uint128(1);
            }
        }

        if (chunk_start_val <= chunk_end_val) {
            uint128 chunk_odds_128 = shift_right(chunk_end_val - chunk_start_val, 1) + uint128(1);
            uint64_t chunk_odds = chunk_odds_128.low;
            total_odds_checked += chunk_odds;

            // Initialize/reset device buffers for this chunk
            int zero = 0;
            SearchMetrics initial_chunk_metrics = {0};

            HIP_CHECK(hipMemcpy(d_max_val_count, &zero, sizeof(int), hipMemcpyHostToDevice));
            HIP_CHECK(hipMemcpy(d_steps_count, &zero, sizeof(int), hipMemcpyHostToDevice));
            HIP_CHECK(hipMemcpy(d_sigma_count, &zero, sizeof(int), hipMemcpyHostToDevice));
            HIP_CHECK(hipMemcpy(d_global_peaks, &masterPeaks, sizeof(PeakState), hipMemcpyHostToDevice));
            HIP_CHECK(hipMemcpy(d_metrics, &initial_chunk_metrics, sizeof(SearchMetrics), hipMemcpyHostToDevice));

            int threads_per_block = 256;
            int blocks = (chunk_odds + threads_per_block - 1) / threads_per_block;

            auto t_start = std::chrono::high_resolution_clock::now();

            bool use_64bit = (end < uint128(0x100000000ULL));
            if (use_64bit) {
                hipLaunchKernelGGL(collatz_search_kernel<true>, dim3(blocks), dim3(threads_per_block), 0, 0,
                    chunk_start_val, chunk_odds,
                    d_max_val_peaks, d_max_val_count,
                    d_steps_peaks, d_steps_count,
                    d_sigma_peaks, d_sigma_count,
                    d_global_peaks, d_metrics
                );
            } else {
                hipLaunchKernelGGL(collatz_search_kernel<false>, dim3(blocks), dim3(threads_per_block), 0, 0,
                    chunk_start_val, chunk_odds,
                    d_max_val_peaks, d_max_val_count,
                    d_steps_peaks, d_steps_count,
                    d_sigma_peaks, d_sigma_count,
                    d_global_peaks, d_metrics
                );
            }

            HIP_CHECK(hipDeviceSynchronize());

            auto t_end = std::chrono::high_resolution_clock::now();
            total_kernel_time += std::chrono::duration<double>(t_end - t_start).count();

            // Read metrics back
            int max_val_count = 0;
            int steps_count = 0;
            int sigma_count = 0;
            SearchMetrics chunkMetrics = {0};
            PeakState chunkPeaks;

            HIP_CHECK(hipMemcpy(&max_val_count, d_max_val_count, sizeof(int), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(&steps_count, d_steps_count, sizeof(int), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(&sigma_count, d_sigma_count, sizeof(int), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(&chunkPeaks, d_global_peaks, sizeof(PeakState), hipMemcpyDeviceToHost));
            HIP_CHECK(hipMemcpy(&chunkMetrics, d_metrics, sizeof(SearchMetrics), hipMemcpyDeviceToHost));

            // Accumulate metrics
            masterMetrics.total_numbers_checked += chunkMetrics.total_numbers_checked;
            masterMetrics.total_steps_computed += chunkMetrics.total_steps_computed;
            masterMetrics.numbers_skipped_mod6 += chunkMetrics.numbers_skipped_mod6;
            masterMetrics.numbers_overflowed += chunkMetrics.numbers_overflowed;

            // Copy candidate lists
            if (max_val_count > 0) {
                int to_copy = std::min(max_val_count, (int)MAX_PEAK_RECORDS);
                std::vector<PeakRecord> chunkMaxVal(to_copy);
                HIP_CHECK(hipMemcpy(chunkMaxVal.data(), d_max_val_peaks, to_copy * sizeof(PeakRecord), hipMemcpyDeviceToHost));
                masterMaxValPeaks.insert(masterMaxValPeaks.end(), chunkMaxVal.begin(), chunkMaxVal.end());
            }

            if (steps_count > 0) {
                int to_copy = std::min(steps_count, (int)MAX_PEAK_RECORDS);
                std::vector<PeakRecord> chunkSteps(to_copy);
                HIP_CHECK(hipMemcpy(chunkSteps.data(), d_steps_peaks, to_copy * sizeof(PeakRecord), hipMemcpyDeviceToHost));
                masterStepsPeaks.insert(masterStepsPeaks.end(), chunkSteps.begin(), chunkSteps.end());
            }

            if (sigma_count > 0) {
                int to_copy = std::min(sigma_count, (int)MAX_PEAK_RECORDS);
                std::vector<PeakRecord> chunkSigma(to_copy);
                HIP_CHECK(hipMemcpy(chunkSigma.data(), d_sigma_peaks, to_copy * sizeof(PeakRecord), hipMemcpyDeviceToHost));
                masterSigmaPeaks.insert(masterSigmaPeaks.end(), chunkSigma.begin(), chunkSigma.end());
            }

            // Update masterPeaks thresholds
            if (chunkPeaks.current_max_value > masterPeaks.current_max_value) {
                masterPeaks.current_max_value = chunkPeaks.current_max_value;
            }
            if (chunkPeaks.current_max_steps > masterPeaks.current_max_steps) {
                masterPeaks.current_max_steps = chunkPeaks.current_max_steps;
            }
            if (chunkPeaks.current_max_sigma > masterPeaks.current_max_sigma) {
                masterPeaks.current_max_sigma = chunkPeaks.current_max_sigma;
            }
        }

        current_chunk_start = current_chunk_start + uint128(CHUNK_SIZE);
    }

    // Filter peaks on CPU side
    filter_peaks(masterMaxValPeaks, masterMaxValPeaks.size());
    filter_peaks(masterStepsPeaks, masterStepsPeaks.size());
    filter_peaks(masterSigmaPeaks, masterSigmaPeaks.size());

    // Assign final values to output variables
    max_value_peaks = std::move(masterMaxValPeaks);
    steps_peaks = std::move(masterStepsPeaks);
    sigma_peaks = std::move(masterSigmaPeaks);
    global_peaks = masterPeaks;
    metrics.total_numbers_checked += masterMetrics.total_numbers_checked;
    metrics.total_steps_computed += masterMetrics.total_steps_computed;
    metrics.numbers_skipped_even += total_odds_checked;
    metrics.numbers_skipped_mod6 += masterMetrics.numbers_skipped_mod6;
    metrics.numbers_overflowed += masterMetrics.numbers_overflowed;
    metrics.elapsed_seconds += total_kernel_time;

    // Free buffers
    HIP_CHECK(hipFree(d_max_val_peaks));
    HIP_CHECK(hipFree(d_steps_peaks));
    HIP_CHECK(hipFree(d_sigma_peaks));
    HIP_CHECK(hipFree(d_max_val_count));
    HIP_CHECK(hipFree(d_steps_count));
    HIP_CHECK(hipFree(d_sigma_count));
    HIP_CHECK(hipFree(d_global_peaks));
    HIP_CHECK(hipFree(d_metrics));
}

void hip_search_block_0(
    uint128 start_num,
    uint128 end_num,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
) {
    if (end_num >= uint128(0x100000000ULL)) {
        throw std::invalid_argument("hip_search_block_0: range extends beyond block 0");
    }
    hip_search_range(start_num, end_num, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
}

void hip_search_blocks_gt_0(
    uint128 start_num,
    uint128 end_num,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
) {
    if (start_num < uint128(0x100000000ULL)) {
        throw std::invalid_argument("hip_search_blocks_gt_0: range starts below block 1");
    }
    hip_search_range(start_num, end_num, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
}
