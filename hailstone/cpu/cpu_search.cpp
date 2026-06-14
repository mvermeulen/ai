// Suffix-first CPU search implementation
#include "cpu_search.h"
#include "steps_table.h"
#include "peak_predictor.h"
#ifdef _OPENMP
#include <omp.h>
#else
inline int omp_get_max_threads() { return 1; }
inline int omp_get_num_threads() { return 1; }
#endif
#include <chrono>
#include <stdexcept>
#include <cassert>
#include <map>

#ifndef POLY_WIDTH
#define POLY_WIDTH 8
#endif

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define steps_table CONCAT(steps, POLY_WIDTH)

CollatzStats compute_collatz(uint128 n) {
    CollatzStats stats;
    stats.start_val = n;
    stats.steps = 0;
    stats.stopping_time = 0;
    stats.max_value = n;
    stats.overflow = false;

    // Base cases
    if (n == uint128(1)) {
        stats.steps = 0;
        stats.stopping_time = 0;
        stats.max_value = 1;
        return stats;
    }
    if (n == uint128(2)) {
        stats.steps = 1;
        stats.stopping_time = 1;
        stats.max_value = 2;
        return stats;
    }

    uint128 curr = n;
    uint32_t t_steps = 0;
    bool has_stopped_sigma = false;
    bool dropped_below_start = false;

    // Handle even starting values on the first step (though search normally skips evens)
    if ((curr.low & 1) == 0) {
        int p = count_trailing_zeros(curr);
        curr = shift_right(curr, p);
        stats.steps += p;
        stats.stopping_time = 1;
        has_stopped_sigma = true;
        t_steps += p;
        dropped_below_start = (curr < n);
    }

    while (curr > uint128(1)) {
        // Since curr is odd, next step of H is 3 * curr + 1 (which is even)
        bool overflow = false;
        uint128 next_val = mul3_add1(curr, overflow);
        if (overflow) {
            stats.overflow = true;
            return stats;
        }
        stats.steps++; // for the 3n + 1 step

        // Update max value (only needed before the trajectory drops below the starting value)
        if (!dropped_below_start) {
            if (next_val > stats.max_value) {
                stats.max_value = next_val;
            }
        }

        // Division by 2^p to make it odd again
        int p = count_trailing_zeros(next_val);
        
        // Track stopping time (sigma) on T-iterates: next_val >> 1, next_val >> 2, ..., next_val >> p
        if (!has_stopped_sigma) {
            for (int k = 1; k <= p; ++k) {
                uint128 val_k = shift_right(next_val, k);
                if (val_k < n) {
                    stats.stopping_time = t_steps + k;
                    has_stopped_sigma = true;
                    break;
                }
            }
        }

        next_val = shift_right(next_val, p);
        stats.steps += p;
        t_steps += p;
        curr = next_val;

        if (curr < n) {
            dropped_below_start = true;
        }
    }

    return stats;
}

CollatzStats compute_collatz_poly(uint128 n, uint32_t current_max_steps) {
    // Assert n is greater than or equal to 2^N where N is the polynomial width
    assert(n >= uint128(1 << POLY_WIDTH));

    CollatzStats stats;
    stats.start_val = n;
    stats.steps = 0;
    stats.stopping_time = 0;
    stats.max_value = n;
    stats.overflow = false;

    uint128 curr = n;
    uint32_t t_steps = 0;
    bool has_stopped_sigma = false;
    bool dropped_below_start = false;

    // Handle even starting values on the first step (though search normally skips evens)
    if ((curr.low & 1) == 0) {
        int p = count_trailing_zeros(curr);
        curr = shift_right(curr, p);
        stats.steps += p;
        stats.stopping_time = 1;
        has_stopped_sigma = true;
        t_steps += p;
        dropped_below_start = (curr < n);
    }

    while (curr >= uint128(1 << POLY_WIDTH)) {
        if (curr.high == 0 && dropped_below_start && has_stopped_sigma) {
            if (stats.steps + 1050 < current_max_steps) {
                return stats;
            }
            uint64_t curr_64 = curr.low;
            while (curr_64 >= (1 << POLY_WIDTH)) {
                uint64_t next_val = 3 * curr_64 + 1;
                stats.steps++;
                int p = ctz64(next_val);
                curr_64 = next_val >> p;
                stats.steps += p;
            }
            curr = uint128(curr_64, 0);
            break;
        }

        // Since curr is odd, next step of H is 3 * curr + 1 (which is even)
        bool overflow = false;
        uint128 next_val = mul3_add1(curr, overflow);
        if (overflow) {
            stats.overflow = true;
            return stats;
        }
        stats.steps++; // for the 3n + 1 step

        // Update max value (only needed before the trajectory drops below the starting value)
        if (!dropped_below_start) {
            if (next_val > stats.max_value) {
                stats.max_value = next_val;
            }
        }

        // Division by 2^p to make it odd again
        int p = count_trailing_zeros(next_val);
        
        // Track stopping time (sigma) on T-iterates: next_val >> 1, next_val >> 2, ..., next_val >> p
        if (!has_stopped_sigma) {
            for (int k = 1; k <= p; ++k) {
                uint128 val_k = shift_right(next_val, k);
                if (val_k < n) {
                    stats.stopping_time = t_steps + k;
                    has_stopped_sigma = true;
                    break;
                }
            }
        }

        next_val = shift_right(next_val, p);
        stats.steps += p;
        t_steps += p;
        curr = next_val;

        if (curr < n) {
            dropped_below_start = true;
        }
    }

    // Once the value drops below 2^N, look up the remaining steps in the steps table
    if (curr > uint128(1)) {
        stats.steps += steps_table[curr.low];
    }

    return stats;
}

void cpu_search_range(uint128 start, uint128 end, 
                      std::vector<PeakRecord>& max_value_peaks,
                      std::vector<PeakRecord>& steps_peaks,
                      std::vector<PeakRecord>& sigma_peaks,
                      PeakState& global_peaks,
                      SearchMetrics& metrics) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Initialize peak predictor from existing steps peaks
    PeakPredictor predictor;
    for (const auto& peak : steps_peaks) {
        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
    }
    predictor.prune_predictions_less_than(start);

    // Ensure start is odd
    uint128 curr = start;
    if ((curr.low & 1) == 0) {
        metrics.numbers_skipped_even++;
        curr = curr + uint128(1);
    }

    uint32_t init_max_steps = global_peaks.current_max_steps;

    for (; curr <= end; curr = curr + uint128(2)) {
        // Process predictions up to curr
        predictor.process_up_to(curr, steps_peaks);
        global_peaks.current_max_steps = predictor.current_max_steps;

        // Modulo 6 cutoff: if curr % 6 == 5, skip
        // Since curr is odd, curr % 6 can be 1, 3, or 5.
        // We can do a fast modulo 6 check
        // n % 6 == 5 is equivalent to (n % 3 == 2) because n is odd (n % 2 == 1).
        // Let's compute (n.low + 2 * (n.high % 3)) % 3 or similar, or just write a division-free remainder.
        // For uint128, n % 3:
        // n = n.high * 2^64 + n.low
        // 2^64 % 3 = (2^2)^32 % 3 = 1^32 % 3 = 1.
        // So n % 3 = (n.high + n.low) % 3.
        // This is incredibly beautiful and fast!
        uint64_t sum_mod3 = (curr.high % 3 + curr.low % 3) % 3;
        if (sum_mod3 == 2) {
            metrics.numbers_skipped_mod6++;
            metrics.total_numbers_checked++;
            continue;
        }

        CollatzStats stats;
        if (curr >= uint128(1 << POLY_WIDTH)) {
            stats = compute_collatz_poly(curr, init_max_steps);
        } else {
            stats = compute_collatz(curr);
        }
        metrics.total_numbers_checked++;

        if (stats.overflow) {
            metrics.numbers_overflowed++;
            continue;
        }

        metrics.total_steps_computed += stats.steps;

        // Check max_value peak
        if (stats.max_value > global_peaks.current_max_value) {
            global_peaks.current_max_value = stats.max_value;
            max_value_peaks.push_back({curr, stats.max_value});
        }

        // Check steps peak
        if (stats.steps > global_peaks.current_max_steps) {
            predictor.add_confirmed_peak(curr, stats.steps);
            global_peaks.current_max_steps = predictor.current_max_steps;
            steps_peaks.push_back({curr, uint128(stats.steps)});
        }

        // Check stopping time (sigma) peak
        if (stats.stopping_time > global_peaks.current_max_sigma) {
            global_peaks.current_max_sigma = stats.stopping_time;
            sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
        }
    }

    // Confirm any remaining predictions up to end
    predictor.process_up_to(end, steps_peaks);
    global_peaks.current_max_steps = predictor.current_max_steps;

    // Accumulate evens skipped in the range
    // In a range [start, end], half the numbers are even.
    // Since we incremented by 2, we skipped all evens within the loop.
    // Total numbers in [start, end] is end - start + 1.
    // Half of them are even.
    if (end >= start) {
        unsigned __int128 total_range = end.low - start.low; // approximate
        // Let's calculate exactly:
        uint128 diff = end + uint128(1) - start;
        uint128 evens = shift_right(diff, 1);
        metrics.numbers_skipped_even += evens.low;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    metrics.elapsed_seconds += diff.count();
}

void cpu_search_block_0(uint128 start_num, uint128 end_num,
                        std::vector<PeakRecord>& max_value_peaks,
                        std::vector<PeakRecord>& steps_peaks,
                        std::vector<PeakRecord>& sigma_peaks,
                        PeakState& global_peaks,
                        SearchMetrics& metrics) {
    if (end_num >= uint128(0x100000000ULL)) {
        throw std::invalid_argument("cpu_search_block_0: range extends beyond block 0");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Initialize peak predictor from existing steps peaks
    PeakPredictor predictor;
    for (const auto& peak : steps_peaks) {
        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
    }
    predictor.prune_predictions_less_than(start_num);

    uint64_t start_64 = start_num.low;
    uint64_t end_64 = end_num.low;

    uint64_t curr = start_64;
    if ((curr & 1) == 0) {
        metrics.numbers_skipped_even++;
        curr += 1;
    }

    for (; curr <= end_64; curr += 2) {
        uint128 u128_curr(curr);
        predictor.process_up_to(u128_curr, steps_peaks);
        global_peaks.current_max_steps = predictor.current_max_steps;

        uint64_t sum_mod3 = curr % 3;
        if (sum_mod3 == 2) {
            metrics.numbers_skipped_mod6++;
            metrics.total_numbers_checked++;
            continue;
        }

        uint64_t val = curr;
        uint32_t steps = 0;
        uint32_t stopping_time = 0;
        uint64_t max_val = curr;
        bool overflowed = false;

        if (val == 1) {
            steps = 0;
            stopping_time = 0;
            max_val = 1;
        } else if (val == 2) {
            steps = 1;
            stopping_time = 1;
            max_val = 2;
        } else {
            uint64_t temp_curr = val;
            uint32_t t_steps = 0;
            bool has_stopped_sigma = false;
            bool dropped_below_start = false;

            while (temp_curr > 1) {
                if (temp_curr > 0x5555555555555555ULL) {
                    overflowed = true;
                    break;
                }
                uint64_t next_val = 3 * temp_curr + 1;
                steps++;

                if (!dropped_below_start) {
                    if (next_val > max_val) {
                        max_val = next_val;
                    }
                }

                int p = ctz64(next_val);
                if (!has_stopped_sigma) {
                    for (int k = 1; k <= p; ++k) {
                        uint64_t val_k = next_val >> k;
                        if (val_k < val) {
                            stopping_time = t_steps + k;
                            has_stopped_sigma = true;
                            break;
                        }
                    }
                }

                next_val >>= p;
                steps += p;
                t_steps += p;
                temp_curr = next_val;

                if (temp_curr < val) {
                    dropped_below_start = true;
                }
            }
        }

        metrics.total_numbers_checked++;
        if (overflowed) {
            metrics.numbers_overflowed++;
            continue;
        }

        metrics.total_steps_computed += steps;

        uint128 u128_max_val(max_val);
        if (u128_max_val > global_peaks.current_max_value) {
            global_peaks.current_max_value = u128_max_val;
            max_value_peaks.push_back({uint128(curr), u128_max_val});
        }

        if (steps > global_peaks.current_max_steps) {
            predictor.add_confirmed_peak(uint128(curr), steps);
            global_peaks.current_max_steps = predictor.current_max_steps;
            steps_peaks.push_back({uint128(curr), uint128(steps)});
        }

        if (stopping_time > global_peaks.current_max_sigma) {
            global_peaks.current_max_sigma = stopping_time;
            sigma_peaks.push_back({uint128(curr), uint128(stopping_time)});
        }
    }

    // Confirm any remaining predictions up to end
    predictor.process_up_to(end_num, steps_peaks);
    global_peaks.current_max_steps = predictor.current_max_steps;

    if (end_64 >= start_64) {
        uint64_t diff = end_64 + 1 - start_64;
        uint64_t evens = diff >> 1;
        metrics.numbers_skipped_even += evens;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    metrics.elapsed_seconds += diff.count();
}

void cpu_search_blocks_gt_0(uint128 start_num, uint128 end_num,
                            std::vector<PeakRecord>& max_value_peaks,
                            std::vector<PeakRecord>& steps_peaks,
                            std::vector<PeakRecord>& sigma_peaks,
                            PeakState& global_peaks,
                            SearchMetrics& metrics) {
    if (start_num < uint128(0x100000000ULL)) {
        throw std::invalid_argument("cpu_search_blocks_gt_0: range starts below block 1");
    }

    if (start_num > end_num) return;

    // Initialize the master PeakPredictor from existing steps peaks
    PeakPredictor predictor;
    for (const auto& peak : steps_peaks) {
        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
    }
    predictor.prune_predictions_less_than(start_num);

    uint128 curr_start = start_num;
    bool force_sequential = false;

    while (curr_start <= end_num) {
        int num_threads = omp_get_max_threads();
        if (num_threads < 1) num_threads = 1;

        // Determine range allocations for a potential parallel search to check if any predictions exist in range
        uint128 parallel_end = curr_start;
        uint128 temp_start = curr_start;
        for (int i = 0; i < num_threads; ++i) {
            if (temp_start > end_num) break;

            uint64_t current_block = temp_start.low >> 32;
            uint128 block_end = uint128(((current_block + 1) << 32) - 1);
            if (block_end > end_num) {
                block_end = end_num;
            }
            parallel_end = block_end;
            temp_start = block_end + uint128(1);
        }

        bool has_prediction_in_range = false;
        for (const auto& p : predictor.active_predictions) {
            if (p.pred_n <= parallel_end) {
                has_prediction_in_range = true;
                break;
            }
        }

        if (has_prediction_in_range || force_sequential) {
            // Sequential fallback
            uint64_t current_block = curr_start.low >> 32;
            uint128 block_end = uint128(((current_block + 1) << 32) - 1);
            if (block_end > end_num) {
                block_end = end_num;
            }

            cpu_search_range(curr_start, block_end, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);

            // Rebuild the master predictor from the updated steps peaks list
            predictor = PeakPredictor();
            for (const auto& peak : steps_peaks) {
                predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
            }
            predictor.prune_predictions_less_than(block_end + uint128(1));

            curr_start = block_end + uint128(1);
            force_sequential = false; // Reset flag after one sequential run
        } else {
            // Parallel search blocks
            // Determine how many blocks we can search in parallel
            std::vector<uint128> block_starts;
            std::vector<uint128> block_ends;

            temp_start = curr_start;
            for (int i = 0; i < num_threads; ++i) {
                if (temp_start > end_num) break;

                uint64_t current_block = temp_start.low >> 32;
                uint128 block_end = uint128(((current_block + 1) << 32) - 1);
                if (block_end > end_num) {
                    block_end = end_num;
                }

                block_starts.push_back(temp_start);
                block_ends.push_back(block_end);

                temp_start = block_end + uint128(1);
            }

            int num_allocated_blocks = (int)block_starts.size();
            if (num_allocated_blocks == 0) break;

            if (num_allocated_blocks == 1 || num_threads == 1) {
                // Just run single block sequentially
                cpu_search_range(block_starts[0], block_ends[0], max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);

                predictor = PeakPredictor();
                for (const auto& peak : steps_peaks) {
                    predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
                }
                predictor.prune_predictions_less_than(block_ends[0] + uint128(1));

                curr_start = block_ends[0] + uint128(1);
                continue;
            }

            // Allocate local outputs for each thread
            std::vector<std::vector<PeakRecord>> local_max_value_peaks(num_allocated_blocks, max_value_peaks);
            std::vector<std::vector<PeakRecord>> local_steps_peaks(num_allocated_blocks, steps_peaks);
            std::vector<std::vector<PeakRecord>> local_sigma_peaks(num_allocated_blocks, sigma_peaks);
            std::vector<PeakState> local_global_peaks(num_allocated_blocks, global_peaks);
            std::vector<SearchMetrics> local_metrics(num_allocated_blocks);
            for (int i = 0; i < num_allocated_blocks; ++i) {
                local_metrics[i] = SearchMetrics{0};
            }

            auto parallel_start_time = std::chrono::high_resolution_clock::now();

            // Execute in parallel using OpenMP
            #pragma omp parallel for schedule(static, 1) num_threads(num_allocated_blocks)
            for (int i = 0; i < num_allocated_blocks; ++i) {
                cpu_search_range(block_starts[i], block_ends[i],
                                 local_max_value_peaks[i],
                                 local_steps_peaks[i],
                                 local_sigma_peaks[i],
                                 local_global_peaks[i],
                                 local_metrics[i]);
            }

            auto parallel_end_time = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> parallel_diff = parallel_end_time - parallel_start_time;

            // Check if any thread found a new peak
            int first_peak_idx = -1;
            for (int i = 0; i < num_allocated_blocks; ++i) {
                bool new_peak = (local_steps_peaks[i].size() > steps_peaks.size()) ||
                                (local_max_value_peaks[i].size() > max_value_peaks.size()) ||
                                (local_sigma_peaks[i].size() > sigma_peaks.size());
                if (new_peak) {
                    first_peak_idx = i;
                    break;
                }
            }

            if (first_peak_idx == -1) {
                // No peaks found in any parallel block range. All succeeded!
                // Merge metrics
                for (int i = 0; i < num_allocated_blocks; ++i) {
                    metrics.total_numbers_checked += local_metrics[i].total_numbers_checked;
                    metrics.total_steps_computed += local_metrics[i].total_steps_computed;
                    metrics.numbers_skipped_even += local_metrics[i].numbers_skipped_even;
                    metrics.numbers_skipped_mod6 += local_metrics[i].numbers_skipped_mod6;
                    metrics.numbers_overflowed += local_metrics[i].numbers_overflowed;
                }
                metrics.elapsed_seconds += parallel_diff.count();
                
                // Advance curr_start
                curr_start = block_ends[num_allocated_blocks - 1] + uint128(1);
            } else {
                // A peak was found starting in block index first_peak_idx!
                // 1. Keep results from all blocks prior to first_peak_idx
                for (int i = 0; i < first_peak_idx; ++i) {
                    metrics.total_numbers_checked += local_metrics[i].total_numbers_checked;
                    metrics.total_steps_computed += local_metrics[i].total_steps_computed;
                    metrics.numbers_skipped_even += local_metrics[i].numbers_skipped_even;
                    metrics.numbers_skipped_mod6 += local_metrics[i].numbers_skipped_mod6;
                    metrics.numbers_overflowed += local_metrics[i].numbers_overflowed;
                }
                metrics.elapsed_seconds += parallel_diff.count();
                
                // 2. Discard results of blocks starting from first_peak_idx.
                // Roll back to the beginning of block first_peak_idx and force sequential.
                curr_start = block_starts[first_peak_idx];
                force_sequential = true;
            }

            // Sync master predictor from the updated steps_peaks
            predictor = PeakPredictor();
            for (const auto& peak : steps_peaks) {
                predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
            }
            predictor.prune_predictions_less_than(curr_start);
        }
    }
}

std::vector<uint32_t> generate_allowed_suffixes(int width) {
    int total_suffixes = 1 << width;
    struct PolyKey {
        uint32_t pow2;
        uint32_t pow3;
        uint64_t add;
        bool operator<(const PolyKey& o) const {
            if (pow2 != o.pow2) return pow2 < o.pow2;
            if (pow3 != o.pow3) return pow3 < o.pow3;
            return add < o.add;
        }
    };

    struct ClassInfo {
        int first_suffix = -1;
        bool has_even = false;
    };
    std::map<PolyKey, ClassInfo> classes;
    std::vector<PolyKey> suffix_polys(total_suffixes);

    for (int i = 0; i < total_suffixes; ++i) {
        uint64_t bits = i;
        int w = width;
        uint32_t pow2 = 0;
        uint32_t pow3 = 0;
        while (w > 0) {
            if (bits & 1) {
                bits = bits * 3 + 1;
                pow3++;
            } else {
                bits >>= 1;
                pow2++;
                w--;
            }
        }
        PolyKey key{pow2, pow3, bits};
        suffix_polys[i] = key;
        
        auto& info = classes[key];
        if (info.first_suffix == -1) {
            info.first_suffix = i;
        }
        if (i % 2 == 0) {
            info.has_even = true;
        }
    }

    std::vector<uint32_t> allowed;
    for (int i = 0; i < total_suffixes; ++i) {
        const auto& key = suffix_polys[i];
        const auto& info = classes[key];
        if (info.first_suffix == i && !info.has_even) {
            allowed.push_back(i);
        }
    }
    return allowed;
}

BaseDependentSuffixes generate_base_dependent_suffixes(int width) {
    int total_suffixes = 1 << width;
    struct PolyKey {
        uint32_t pow2;
        uint32_t pow3;
        uint64_t add;
        bool operator<(const PolyKey& o) const {
            if (pow2 != o.pow2) return pow2 < o.pow2;
            if (pow3 != o.pow3) return pow3 < o.pow3;
            return add < o.add;
        }
    };

    struct ClassInfo {
        int first_suffix = -1;
        bool has_even = false;
        std::vector<uint32_t> members;
    };
    std::map<PolyKey, ClassInfo> classes;
    std::vector<PolyKey> suffix_polys(total_suffixes);

    for (int i = 0; i < total_suffixes; ++i) {
        uint64_t bits = i;
        int w = width;
        uint32_t pow2 = 0;
        uint32_t pow3 = 0;
        while (w > 0) {
            if (bits & 1) {
                bits = bits * 3 + 1;
                pow3++;
            } else {
                bits >>= 1;
                pow2++;
                w--;
            }
        }
        PolyKey key{pow2, pow3, bits};
        suffix_polys[i] = key;
        
        auto& info = classes[key];
        if (info.first_suffix == -1) {
            info.first_suffix = i;
        }
        info.members.push_back(i);
        if (i % 2 == 0) {
            info.has_even = true;
        }
    }

    BaseDependentSuffixes res;
    
    // Build std_allowed
    for (int i = 0; i < total_suffixes; ++i) {
        const auto& key = suffix_polys[i];
        const auto& info = classes[key];
        if (info.first_suffix == i && !info.has_even) {
            res.std_allowed.push_back(i);
        }
    }
    
    // Precompute skipped counts for std_allowed
    for (uint32_t s : res.std_allowed) {
        if (s % 3 == 2) res.std_skipped_0++;
        if ((1 + s) % 3 == 2) res.std_skipped_1++;
        if ((2 + s) % 3 == 2) res.std_skipped_2++;
    }

    // Build base-dependent allowed lists
    for (const auto& pair : classes) {
        const auto& info = pair.second;
        if (info.has_even) continue;
        
        uint32_t r1 = info.first_suffix;
        bool has_1 = false;
        bool has_3 = false;
        bool has_5 = false;
        for (uint32_t m : info.members) {
            uint32_t rem = m % 6;
            if (rem == 1) has_1 = true;
            else if (rem == 3) has_3 = true;
            else if (rem == 5) has_5 = true;
        }
        
        if (!has_5) res.allowed_0.push_back(r1);
        if (!has_3) res.allowed_2.push_back(r1);
        if (!has_1) res.allowed_4.push_back(r1);
    }
    
    std::sort(res.allowed_0.begin(), res.allowed_0.end());
    std::sort(res.allowed_2.begin(), res.allowed_2.end());
    std::sort(res.allowed_4.begin(), res.allowed_4.end());
    
    return res;
}

void cpu_search_block_0_suffix_first(uint128 start_num, uint128 end_num,
                                     int width,
                                     const BaseDependentSuffixes& base_suffixes,
                                     std::vector<PeakRecord>& max_value_peaks,
                                     std::vector<PeakRecord>& steps_peaks,
                                     std::vector<PeakRecord>& sigma_peaks,
                                     PeakState& global_peaks,
                                     SearchMetrics& metrics) {
    if (end_num >= uint128(0x100000000ULL)) {
        throw std::invalid_argument("cpu_search_block_0_suffix_first: range extends beyond block 0");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // Initialize peak predictor from existing steps peaks
    PeakPredictor predictor;
    for (const auto& peak : steps_peaks) {
        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
    }
    predictor.prune_predictions_less_than(start_num);

    uint64_t start_64 = start_num.low;
    uint64_t end_64 = end_num.low;

    uint64_t start_prefix = start_64 >> width;
    uint64_t end_prefix = end_64 >> width;

    uint32_t std_allowed_size = (uint32_t)base_suffixes.std_allowed.size();

    // Loop through prefixes
    for (uint64_t x = start_prefix; x <= end_prefix; ++x) {
        uint64_t base = x << width;
        uint64_t x_mod3 = x % 3;
        
        uint64_t base_mod6 = (x_mod3 == 0) ? 0 : ((x_mod3 == 1) ? 4 : 2);
        
        const std::vector<uint32_t>& allowed = (base_mod6 == 0) ? base_suffixes.allowed_0 :
                                               ((base_mod6 == 2) ? base_suffixes.allowed_2 :
                                                                   base_suffixes.allowed_4);

        bool is_fully_in_bounds = (x > start_prefix && x < end_prefix);

        if (is_fully_in_bounds) {
            uint32_t std_skipped = (base_mod6 == 0) ? base_suffixes.std_skipped_0 :
                                   ((base_mod6 == 2) ? base_suffixes.std_skipped_2 :
                                                       base_suffixes.std_skipped_1);
            metrics.total_numbers_checked += std_allowed_size;
            metrics.numbers_skipped_mod6 += std_skipped;

            for (uint32_t suffix : allowed) {
                uint64_t curr = base | suffix;
                uint128 u128_curr(curr);
                predictor.process_up_to(u128_curr, steps_peaks);
                global_peaks.current_max_steps = predictor.current_max_steps;

                uint64_t val = curr;
                uint32_t steps = 0;
                uint32_t stopping_time = 0;
                uint64_t max_val = curr;
                bool overflowed = false;

                if (val == 1) {
                    steps = 0;
                    stopping_time = 0;
                    max_val = 1;
                } else if (val == 2) {
                    steps = 1;
                    stopping_time = 1;
                    max_val = 2;
                } else {
                    uint64_t temp_curr = val;
                    uint32_t t_steps = 0;
                    bool has_stopped_sigma = false;
                    bool dropped_below_start = false;

                    while (temp_curr > 1) {
                        if (temp_curr > 0x5555555555555555ULL) {
                            overflowed = true;
                            break;
                        }
                        uint64_t next_val = 3 * temp_curr + 1;
                        steps++;

                        if (!dropped_below_start) {
                            if (next_val > max_val) {
                                max_val = next_val;
                            }
                        }

                        int p = ctz64(next_val);
                        if (!has_stopped_sigma) {
                            for (int k = 1; k <= p; ++k) {
                                uint64_t val_k = next_val >> k;
                                if (val_k < val) {
                                    stopping_time = t_steps + k;
                                    has_stopped_sigma = true;
                                    break;
                                }
                            }
                        }

                        next_val >>= p;
                        steps += p;
                        t_steps += p;
                        temp_curr = next_val;

                        if (temp_curr < val) {
                            dropped_below_start = true;
                        }
                    }
                }

                if (overflowed) {
                    metrics.numbers_overflowed++;
                    continue;
                }

                metrics.total_steps_computed += steps;

                uint128 u128_max_val(max_val);
                if (u128_max_val > global_peaks.current_max_value) {
                    global_peaks.current_max_value = u128_max_val;
                    max_value_peaks.push_back({uint128(curr), u128_max_val});
                }

                if (steps > global_peaks.current_max_steps) {
                    predictor.add_confirmed_peak(uint128(curr), steps);
                    global_peaks.current_max_steps = predictor.current_max_steps;
                    steps_peaks.push_back({uint128(curr), uint128(steps)});
                }

                if (stopping_time > global_peaks.current_max_sigma) {
                    global_peaks.current_max_sigma = stopping_time;
                    sigma_peaks.push_back({uint128(curr), uint128(stopping_time)});
                }
            }
        } else {
            // Boundary block: perform individual prefix bounds checking and exact metrics tracking
            uint64_t mult = (1ULL << width) % 3;
            uint64_t base_mod3 = (x_mod3 * mult) % 3;

            for (uint32_t suffix : base_suffixes.std_allowed) {
                uint64_t curr = base | suffix;
                
                if (curr < start_64) continue;
                if (curr > end_64) break;

                if ((base_mod3 + suffix) % 3 == 2) {
                    metrics.numbers_skipped_mod6++;
                    metrics.total_numbers_checked++;
                    continue;
                }

                metrics.total_numbers_checked++;

                // Check if this suffix is pruned under base-dependent rules
                bool is_pruned = !std::binary_search(allowed.begin(), allowed.end(), suffix);
                if (is_pruned) continue;

                uint128 u128_curr(curr);
                predictor.process_up_to(u128_curr, steps_peaks);
                global_peaks.current_max_steps = predictor.current_max_steps;

                uint64_t val = curr;
                uint32_t steps = 0;
                uint32_t stopping_time = 0;
                uint64_t max_val = curr;
                bool overflowed = false;

                if (val == 1) {
                    steps = 0;
                    stopping_time = 0;
                    max_val = 1;
                } else if (val == 2) {
                    steps = 1;
                    stopping_time = 1;
                    max_val = 2;
                } else {
                    uint64_t temp_curr = val;
                    uint32_t t_steps = 0;
                    bool has_stopped_sigma = false;
                    bool dropped_below_start = false;

                    while (temp_curr > 1) {
                        if (temp_curr > 0x5555555555555555ULL) {
                            overflowed = true;
                            break;
                        }
                        uint64_t next_val = 3 * temp_curr + 1;
                        steps++;

                        if (!dropped_below_start) {
                            if (next_val > max_val) {
                                max_val = next_val;
                            }
                        }

                        int p = ctz64(next_val);
                        if (!has_stopped_sigma) {
                            for (int k = 1; k <= p; ++k) {
                                uint64_t val_k = next_val >> k;
                                if (val_k < val) {
                                    stopping_time = t_steps + k;
                                    has_stopped_sigma = true;
                                    break;
                                }
                            }
                        }

                        next_val >>= p;
                        steps += p;
                        t_steps += p;
                        temp_curr = next_val;

                        if (temp_curr < val) {
                            dropped_below_start = true;
                        }
                    }
                }

                if (overflowed) {
                    metrics.numbers_overflowed++;
                    continue;
                }

                metrics.total_steps_computed += steps;

                uint128 u128_max_val(max_val);
                if (u128_max_val > global_peaks.current_max_value) {
                    global_peaks.current_max_value = u128_max_val;
                    max_value_peaks.push_back({uint128(curr), u128_max_val});
                }

                if (steps > global_peaks.current_max_steps) {
                    predictor.add_confirmed_peak(uint128(curr), steps);
                    global_peaks.current_max_steps = predictor.current_max_steps;
                    steps_peaks.push_back({uint128(curr), uint128(steps)});
                }

                if (stopping_time > global_peaks.current_max_sigma) {
                    global_peaks.current_max_sigma = stopping_time;
                    sigma_peaks.push_back({uint128(curr), uint128(stopping_time)});
                }
            }
        }
    }

    // Confirm any remaining predictions up to end
    predictor.process_up_to(end_num, steps_peaks);
    global_peaks.current_max_steps = predictor.current_max_steps;

    // Accumulate evens skipped in the range
    if (end_64 >= start_64) {
        uint64_t diff = end_64 + 1 - start_64;
        uint64_t evens = diff >> 1;
        metrics.numbers_skipped_even += evens;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    metrics.elapsed_seconds += diff.count();
}

void cpu_search_range_suffix_first(uint128 start, uint128 end, 
                                   int width,
                                   const BaseDependentSuffixes& base_suffixes,
                                   std::vector<PeakRecord>& max_value_peaks,
                                   std::vector<PeakRecord>& steps_peaks,
                                   std::vector<PeakRecord>& sigma_peaks,
                                   PeakState& global_peaks,
                                   SearchMetrics& metrics) {
    auto start_time = std::chrono::high_resolution_clock::now();
    uint32_t init_max_steps = global_peaks.current_max_steps;

    // Initialize peak predictor from existing steps peaks
    PeakPredictor predictor;
    for (const auto& peak : steps_peaks) {
        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
    }
    predictor.prune_predictions_less_than(start);

    uint128 start_prefix = shift_right(start, width);
    uint128 end_prefix = shift_right(end, width);

    uint64_t mult = (1ULL << width) % 3;
    uint32_t std_allowed_size = (uint32_t)base_suffixes.std_allowed.size();

    for (uint128 x = start_prefix; x <= end_prefix; x = x + uint128(1)) {
        uint128 base = shift_left(x, width);
        uint64_t x_mod3 = (x.high % 3 + x.low % 3) % 3;
        uint64_t base_mod3 = (x_mod3 * mult) % 3;
        
        uint64_t base_mod6 = (x_mod3 == 0) ? 0 : ((x_mod3 == 1) ? 4 : 2);
        
        const std::vector<uint32_t>& allowed = (base_mod6 == 0) ? base_suffixes.allowed_0 :
                                               ((base_mod6 == 2) ? base_suffixes.allowed_2 :
                                                                   base_suffixes.allowed_4);

        bool is_fully_in_bounds = (x > start_prefix && x < end_prefix);

        if (is_fully_in_bounds) {
            uint32_t std_skipped = (base_mod3 == 0) ? base_suffixes.std_skipped_0 :
                                   ((base_mod3 == 2) ? base_suffixes.std_skipped_2 :
                                                       base_suffixes.std_skipped_1);
            metrics.total_numbers_checked += std_allowed_size;
            metrics.numbers_skipped_mod6 += std_skipped;

            for (uint32_t suffix : allowed) {
                uint128 curr = base + uint128(suffix);

                predictor.process_up_to(curr, steps_peaks);
                global_peaks.current_max_steps = predictor.current_max_steps;

                CollatzStats stats;
                if (curr >= uint128(1 << POLY_WIDTH)) {
                    stats = compute_collatz_poly(curr, init_max_steps);
                } else {
                    stats = compute_collatz(curr);
                }

                if (stats.overflow) {
                    metrics.numbers_overflowed++;
                    continue;
                }

                metrics.total_steps_computed += stats.steps;

                // Check max_value peak
                if (stats.max_value > global_peaks.current_max_value) {
                    global_peaks.current_max_value = stats.max_value;
                    max_value_peaks.push_back({curr, stats.max_value});
                }

                // Check steps peak
                if (stats.steps > global_peaks.current_max_steps) {
                    predictor.add_confirmed_peak(curr, stats.steps);
                    global_peaks.current_max_steps = predictor.current_max_steps;
                    steps_peaks.push_back({curr, uint128(stats.steps)});
                }

                // Check stopping time (sigma) peak
                if (stats.stopping_time > global_peaks.current_max_sigma) {
                    global_peaks.current_max_sigma = stats.stopping_time;
                    sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
                }
            }
        } else {
            // Boundary block: perform individual prefix bounds checking and exact metrics tracking
            for (uint32_t suffix : base_suffixes.std_allowed) {
                uint128 curr = base + uint128(suffix);

                if (curr < start) continue;
                if (curr > end) break;

                if ((base_mod3 + suffix) % 3 == 2) {
                    metrics.numbers_skipped_mod6++;
                    metrics.total_numbers_checked++;
                    continue;
                }

                metrics.total_numbers_checked++;

                // Check if this suffix is pruned under base-dependent rules
                bool is_pruned = !std::binary_search(allowed.begin(), allowed.end(), suffix);
                if (is_pruned) continue;

                predictor.process_up_to(curr, steps_peaks);
                global_peaks.current_max_steps = predictor.current_max_steps;

                CollatzStats stats;
                if (curr >= uint128(1 << POLY_WIDTH)) {
                    stats = compute_collatz_poly(curr, init_max_steps);
                } else {
                    stats = compute_collatz(curr);
                }

                if (stats.overflow) {
                    metrics.numbers_overflowed++;
                    continue;
                }

                metrics.total_steps_computed += stats.steps;

                // Check max_value peak
                if (stats.max_value > global_peaks.current_max_value) {
                    global_peaks.current_max_value = stats.max_value;
                    max_value_peaks.push_back({curr, stats.max_value});
                }

                // Check steps peak
                if (stats.steps > global_peaks.current_max_steps) {
                    predictor.add_confirmed_peak(curr, stats.steps);
                    global_peaks.current_max_steps = predictor.current_max_steps;
                    steps_peaks.push_back({curr, uint128(stats.steps)});
                }

                // Check stopping time (sigma) peak
                if (stats.stopping_time > global_peaks.current_max_sigma) {
                    global_peaks.current_max_sigma = stats.stopping_time;
                    sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
                }
            }
        }
    }

    // Confirm any remaining predictions up to end
    predictor.process_up_to(end, steps_peaks);
    global_peaks.current_max_steps = predictor.current_max_steps;

    // Accumulate evens skipped in the range
    if (end >= start) {
        uint128 diff = end + uint128(1) - start;
        uint128 evens = shift_right(diff, 1);
        metrics.numbers_skipped_even += evens.low;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    metrics.elapsed_seconds += diff.count();
}

bool is_avx512_supported() {
#if (defined(__x86_64__) || defined(_M_X64)) && defined(__GNUC__)
    return __builtin_cpu_supports("avx512f") && 
           __builtin_cpu_supports("avx512cd") &&
           __builtin_cpu_supports("avx512dq");
#else
    return false;
#endif
}

