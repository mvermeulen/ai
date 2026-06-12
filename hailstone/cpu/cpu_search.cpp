#include "cpu_search.h"
#include "steps_table.h"
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

    // Ensure start is odd
    uint128 curr = start;
    if ((curr.low & 1) == 0) {
        metrics.numbers_skipped_even++;
        curr = curr + uint128(1);
    }

    for (; curr <= end; curr = curr + uint128(2)) {
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
            stats = compute_collatz_poly(curr, global_peaks.current_max_steps);
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
            global_peaks.current_max_steps = stats.steps;
            steps_peaks.push_back({curr, uint128(stats.steps)});
        }

        // Check stopping time (sigma) peak
        if (stats.stopping_time > global_peaks.current_max_sigma) {
            global_peaks.current_max_sigma = stats.stopping_time;
            sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
        }
    }

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

    uint64_t start_64 = start_num.low;
    uint64_t end_64 = end_num.low;

    uint64_t curr = start_64;
    if ((curr & 1) == 0) {
        metrics.numbers_skipped_even++;
        curr += 1;
    }

    for (; curr <= end_64; curr += 2) {
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
            global_peaks.current_max_steps = steps;
            steps_peaks.push_back({uint128(curr), uint128(steps)});
        }

        if (stopping_time > global_peaks.current_max_sigma) {
            global_peaks.current_max_sigma = stopping_time;
            sigma_peaks.push_back({uint128(curr), uint128(stopping_time)});
        }
    }

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
    cpu_search_range(start_num, end_num, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
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

void cpu_search_block_0_suffix_first(uint128 start_num, uint128 end_num,
                                     int width,
                                     const std::vector<uint32_t>& allowed_suffixes,
                                     std::vector<PeakRecord>& max_value_peaks,
                                     std::vector<PeakRecord>& steps_peaks,
                                     std::vector<PeakRecord>& sigma_peaks,
                                     PeakState& global_peaks,
                                     SearchMetrics& metrics) {
    if (end_num >= uint128(0x100000000ULL)) {
        throw std::invalid_argument("cpu_search_block_0_suffix_first: range extends beyond block 0");
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    uint64_t start_64 = start_num.low;
    uint64_t end_64 = end_num.low;

    uint64_t start_prefix = start_64 >> width;
    uint64_t end_prefix = end_64 >> width;

    // Loop through prefixes
    for (uint64_t x = start_prefix; x <= end_prefix; ++x) {
        uint64_t base = x << width;
        uint64_t x_mod3 = x % 3;
        
        uint64_t mult = (1ULL << width) % 3;
        uint64_t base_mod3 = (x_mod3 * mult) % 3;

        for (uint32_t suffix : allowed_suffixes) {
            uint64_t curr = base | suffix;
            
            // Boundary checks
            if (curr < start_64) continue;
            if (curr > end_64) break; // Suffixes are ordered; subsequent ones will also exceed end_64

            // Modulo 6 cutoff: if curr % 6 == 5 (equivalent to curr % 3 == 2 since curr is odd)
            if ((base_mod3 + suffix) % 3 == 2) {
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
                global_peaks.current_max_steps = steps;
                steps_peaks.push_back({uint128(curr), uint128(steps)});
            }

            if (stopping_time > global_peaks.current_max_sigma) {
                global_peaks.current_max_sigma = stopping_time;
                sigma_peaks.push_back({uint128(curr), uint128(stopping_time)});
            }
        }
    }

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
                                   const std::vector<uint32_t>& allowed_suffixes,
                                   std::vector<PeakRecord>& max_value_peaks,
                                   std::vector<PeakRecord>& steps_peaks,
                                   std::vector<PeakRecord>& sigma_peaks,
                                   PeakState& global_peaks,
                                   SearchMetrics& metrics) {
    auto start_time = std::chrono::high_resolution_clock::now();

    uint128 start_prefix = shift_right(start, width);
    uint128 end_prefix = shift_right(end, width);

    uint64_t mult = (1ULL << width) % 3;

    for (uint128 x = start_prefix; x <= end_prefix; x = x + uint128(1)) {
        uint128 base = shift_left(x, width);
        uint64_t x_mod3 = (x.high % 3 + x.low % 3) % 3;
        uint64_t base_mod3 = (x_mod3 * mult) % 3;

        for (uint32_t suffix : allowed_suffixes) {
            uint128 curr = base + uint128(suffix);

            // Boundary checks
            if (curr < start) continue;
            if (curr > end) break; // Suffixes are ordered; subsequent ones will exceed end

            // Modulo 6 cutoff: if curr % 6 == 5 (equivalent to (curr % 3 == 2) since curr is odd)
            if ((base_mod3 + suffix) % 3 == 2) {
                metrics.numbers_skipped_mod6++;
                metrics.total_numbers_checked++;
                continue;
            }

            CollatzStats stats;
            if (curr >= uint128(1 << POLY_WIDTH)) {
                stats = compute_collatz_poly(curr, global_peaks.current_max_steps);
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
                global_peaks.current_max_steps = stats.steps;
                steps_peaks.push_back({curr, uint128(stats.steps)});
            }

            // Check stopping time (sigma) peak
            if (stats.stopping_time > global_peaks.current_max_sigma) {
                global_peaks.current_max_sigma = stats.stopping_time;
                sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
            }
        }
    }

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

