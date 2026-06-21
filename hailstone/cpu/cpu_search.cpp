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
#include <cstdlib>
#include <cstdio>

bool use_domain_switching = false;

// Precomputed powers of 3 lookup table (up to 3^40)
extern const uint128 lut3[] = {
    uint128(0x0000000000000001ULL, 0x0000000000000000ULL), // 3^0
    uint128(0x0000000000000003ULL, 0x0000000000000000ULL), // 3^1
    uint128(0x0000000000000009ULL, 0x0000000000000000ULL), // 3^2
    uint128(0x000000000000001bULL, 0x0000000000000000ULL), // 3^3
    uint128(0x0000000000000051ULL, 0x0000000000000000ULL), // 3^4
    uint128(0x00000000000000f3ULL, 0x0000000000000000ULL), // 3^5
    uint128(0x00000000000002d9ULL, 0x0000000000000000ULL), // 3^6
    uint128(0x000000000000088bULL, 0x0000000000000000ULL), // 3^7
    uint128(0x00000000000019a1ULL, 0x0000000000000000ULL), // 3^8
    uint128(0x0000000000004ce3ULL, 0x0000000000000000ULL), // 3^9
    uint128(0x000000000000e6a9ULL, 0x0000000000000000ULL), // 3^10
    uint128(0x000000000002b3fbULL, 0x0000000000000000ULL), // 3^11
    uint128(0x0000000000081bf1ULL, 0x0000000000000000ULL), // 3^12
    uint128(0x00000000001853d3ULL, 0x0000000000000000ULL), // 3^13
    uint128(0x000000000048fb79ULL, 0x0000000000000000ULL), // 3^14
    uint128(0x0000000000daf26bULL, 0x0000000000000000ULL), // 3^15
    uint128(0x000000000290d741ULL, 0x0000000000000000ULL), // 3^16
    uint128(0x0000000007b285c3ULL, 0x0000000000000000ULL), // 3^17
    uint128(0x0000000017179149ULL, 0x0000000000000000ULL), // 3^18
    uint128(0x000000004546b3dbULL, 0x0000000000000000ULL), // 3^19
    uint128(0x00000000cfd41b91ULL, 0x0000000000000000ULL), // 3^20
    uint128(0x000000026f7c52b3ULL, 0x0000000000000000ULL), // 3^21
    uint128(0x000000074e74f819ULL, 0x0000000000000000ULL), // 3^22
    uint128(0x00000015eb5ee84bULL, 0x0000000000000000ULL), // 3^23
    uint128(0x00000041c21cb8e1ULL, 0x0000000000000000ULL), // 3^24
    uint128(0x000000c546562aa3ULL, 0x0000000000000000ULL), // 3^25
    uint128(0x0000024fd3027fe9ULL, 0x0000000000000000ULL), // 3^26
    uint128(0x000006ef79077fbbULL, 0x0000000000000000ULL), // 3^27
    uint128(0x000014ce6b167f31ULL, 0x0000000000000000ULL), // 3^28
    uint128(0x00003e6b41437d93ULL, 0x0000000000000000ULL), // 3^29
    uint128(0x0000bb41c3ca78b9ULL, 0x0000000000000000ULL), // 3^30
    uint128(0x000231c54b5f6a2bULL, 0x0000000000000000ULL), // 3^31
    uint128(0x0006954fe21e3e81ULL, 0x0000000000000000ULL), // 3^32
    uint128(0x0013bfefa65abb83ULL, 0x0000000000000000ULL), // 3^33
    uint128(0x003b3fcef3103289ULL, 0x0000000000000000ULL), // 3^34
    uint128(0x00b1bf6cd930979bULL, 0x0000000000000000ULL), // 3^35
    uint128(0x02153e468b91c6d1ULL, 0x0000000000000000ULL), // 3^36
    uint128(0x063fbad3a2b55473ULL, 0x0000000000000000ULL), // 3^37
    uint128(0x12bf307ae81ffd59ULL, 0x0000000000000000ULL), // 3^38
    uint128(0x383d9170b85ff80bULL, 0x0000000000000000ULL), // 3^39
    uint128(0xa8b8b452291fe821ULL, 0x0000000000000000ULL), // 3^40
};

// Maximum safe k = (2^128 - 1) / 3^alpha to avoid overflow
extern const uint128 max_safe_k[] = {
    uint128(0xffffffffffffffffULL, 0xffffffffffffffffULL), // for alpha=0
    uint128(0x5555555555555555ULL, 0x5555555555555555ULL), // for alpha=1
    uint128(0xc71c71c71c71c71cULL, 0x1c71c71c71c71c71ULL), // for alpha=2
    uint128(0xed097b425ed097b4ULL, 0x097b425ed097b425ULL), // for alpha=3
    uint128(0xa4587e6b74f03291ULL, 0x0329161f9add3c0cULL), // for alpha=4
    uint128(0x8c1d7f7926fabb85ULL, 0x010db20a88f46959ULL), // for alpha=5
    uint128(0xd95f2a7db7a8e92cULL, 0x0059e60382fc231dULL), // for alpha=6
    uint128(0x48750e29e7e2f864ULL, 0x001df75680feb65fULL), // for alpha=7
    uint128(0x6d7c5a0df7f652ccULL, 0x0009fd1cd5aa3ccaULL), // for alpha=8
    uint128(0xcf297359fd521b99ULL, 0x0003545ef1e36998ULL), // for alpha=9
    uint128(0x450dd11dff1b5e88ULL, 0x00011c1fa5f67888ULL), // for alpha=10
    uint128(0x6c59f05f55091f82ULL, 0x00005eb53752282dULL), // for alpha=11
    uint128(0xcec8a5751c585fd6ULL, 0x00001f91bd1b62b9ULL), // for alpha=12
    uint128(0x44ed8c7c5ec81ff2ULL, 0x00000a85e9b3cb93ULL), // for alpha=13
    uint128(0xc1a4842974ed5ffbULL, 0x00000381f89143dbULL), // for alpha=14
    uint128(0x95e1816326f9caa9ULL, 0x0000012b52db169eULL), // for alpha=15
    uint128(0x31f5d5cbb7a898e3ULL, 0x00000063c649078aULL), // for alpha=16
    uint128(0xbb51f1ee928d884bULL, 0x00000021421857d8ULL), // for alpha=17
    uint128(0x3e70a5fa30d9d819ULL, 0x0000000b16081d48ULL), // for alpha=18
    uint128(0x6a258ca8baf34808ULL, 0x00000003b202b46dULL), // for alpha=19
    uint128(0x78b72ee2e8fbc2adULL, 0x000000013b563c24ULL), // for alpha=20
    uint128(0xd2e7ba4ba2fe9639ULL, 0x00000000691cbeb6ULL), // for alpha=21
    uint128(0x9ba2936e8baa3213ULL, 0x00000000230994e7ULL), // for alpha=22
    uint128(0x33e0dbcf83e36606ULL, 0x000000000baddc4dULL), // for alpha=23
    uint128(0x66a049452bf67757ULL, 0x0000000003e49ec4ULL), // for alpha=24
    uint128(0x22356dc1b95227c7ULL, 0x00000000014c34ecULL), // for alpha=25
    uint128(0xb611cf40931b6297ULL, 0x00000000006ebc4eULL), // for alpha=26
    uint128(0x9205efc0310920ddULL, 0x000000000024e96fULL), // for alpha=27
    uint128(0xdb574feabb030af4ULL, 0x00000000000c4dcfULL), // for alpha=28
    uint128(0xf3c7c54e3e5658fcULL, 0x00000000000419efULL), // for alpha=29
    uint128(0xa697ec6f6a1cc854ULL, 0x0000000000015dfaULL), // for alpha=30
    uint128(0xe232a425235eed71ULL, 0x00000000000074a8ULL), // for alpha=31
    uint128(0xf610e161b674f9d0ULL, 0x00000000000026e2ULL), // for alpha=32
    uint128(0x5205a075e77c5345ULL, 0x0000000000000cf6ULL), // for alpha=33
    uint128(0x1b57357ca27ec66cULL, 0x0000000000000452ULL), // for alpha=34
    uint128(0xb3c7bc7ee0d4ecceULL, 0x0000000000000170ULL), // for alpha=35
    uint128(0xe697e97fa046f99aULL, 0x000000000000007aULL), // for alpha=36
    uint128(0xf787f87fe017a888ULL, 0x0000000000000028ULL), // for alpha=37
    uint128(0xa7d7fd7ff55d382dULL, 0x000000000000000dULL), // for alpha=38
    uint128(0x8d47ff2aa71f12b9ULL, 0x0000000000000004ULL), // for alpha=39
    uint128(0x846d550e37b5063dULL, 0x0000000000000001ULL), // for alpha=40
};

#ifndef POLY_WIDTH
#define POLY_WIDTH 8
#endif

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define steps_table CONCAT(steps, POLY_WIDTH)

CollatzStats compute_collatz_std(uint128 n) {
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
        uint128 next_val_shifted = shift_right(next_val, p);
        
        // Track stopping time (sigma) on T-iterates: next_val >> 1, next_val >> 2, ..., next_val >> p
        if (!has_stopped_sigma) {
            if (next_val_shifted < n) {
                for (int k = 1; k <= p; ++k) {
                    uint128 val_k = shift_right(next_val, k);
                    if (val_k < n) {
                        stats.stopping_time = t_steps + k;
                        has_stopped_sigma = true;
                        break;
                    }
                }
            }
        }

        stats.steps += p;
        t_steps += p;
        curr = next_val_shifted;

        if (curr < n) {
            dropped_below_start = true;
        }
    }

    return stats;
}

CollatzStats compute_collatz_domain(uint128 n) {
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

    // Handle even starting values on the first step
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
        // Enter n+1 domain
        curr = curr + uint128(1);
        int alpha = ctz64(curr.low | (1ULL << 40));
        uint128 k = shift_right(curr, alpha);
        if (k > max_safe_k[alpha]) {
            stats.overflow = true;
            return stats;
        }
        uint128 m = k * lut3[alpha];
        
        // Check if 2m - 2 overflows 128 bits: m > 2^127
        if ((m.high & 0x8000000000000000ULL) != 0) {
            stats.overflow = true;
            return stats;
        }

        uint128 n_new = m - uint128(1);
        int beta = count_trailing_zeros(n_new);

        // Update max value (only needed before the trajectory drops below the starting value)
        if (!dropped_below_start) {
            bool of_peak = false;
            uint128 segment_peak = shift_left_1(m, of_peak) - uint128(2);
            if (segment_peak > stats.max_value) {
                stats.max_value = segment_peak;
            }
        }

        // Track stopping time (sigma)
        if (!has_stopped_sigma) {
            int L_m = 128 - count_leading_zeros(n_new);
            int L_n = 128 - count_leading_zeros(stats.start_val);
            int j = L_m - L_n;
            if (j < 1) j = 1;

            if (stats.start_val <= shift_right(n_new, j)) {
                j++;
            }
            if (j <= beta) {
                stats.stopping_time = t_steps + alpha + j;
                has_stopped_sigma = true;
            }
        }

        stats.steps += 2 * alpha + beta;
        t_steps += alpha + beta;
        curr = shift_right(n_new, beta);

        if (curr < stats.start_val) {
            dropped_below_start = true;
        }
    }

    return stats;
}

CollatzStats compute_collatz(uint128 n) {
    if (use_domain_switching) {
        return compute_collatz_domain(n);
    } else {
        return compute_collatz_std(n);
    }
}

CollatzStats compute_collatz_poly_std(uint128 n, uint32_t current_max_steps) {
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
        if (curr.high == 0 && curr.low < 0x100000000ULL && dropped_below_start && has_stopped_sigma) {
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
        uint128 next_val_shifted = shift_right(next_val, p);
        
        // Track stopping time (sigma) on T-iterates: next_val >> 1, next_val >> 2, ..., next_val >> p
        if (!has_stopped_sigma) {
            if (next_val_shifted < n) {
                for (int k = 1; k <= p; ++k) {
                    uint128 val_k = shift_right(next_val, k);
                    if (val_k < n) {
                        stats.stopping_time = t_steps + k;
                        has_stopped_sigma = true;
                        break;
                    }
                }
            }
        }

        stats.steps += p;
        t_steps += p;
        curr = next_val_shifted;

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

CollatzStats compute_collatz_poly_domain(uint128 n, uint32_t current_max_steps) {
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

    // Handle even starting values on the first step
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
        if (curr.high == 0 && curr.low < 0x100000000ULL && dropped_below_start && has_stopped_sigma) {
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

        // Enter n+1 domain
        curr = curr + uint128(1);
        int alpha = ctz64(curr.low | (1ULL << 40));
        uint128 k = shift_right(curr, alpha);
        if (k > max_safe_k[alpha]) {
            stats.overflow = true;
            return stats;
        }
        uint128 m = k * lut3[alpha];
        
        // Check if 2m - 2 overflows 128 bits: m > 2^127
        if ((m.high & 0x8000000000000000ULL) != 0) {
            stats.overflow = true;
            return stats;
        }

        uint128 n_new = m - uint128(1);
        int beta = count_trailing_zeros(n_new);

        // Update max value (only needed before the trajectory drops below the starting value)
        if (!dropped_below_start) {
            bool of_peak = false;
            uint128 segment_peak = shift_left_1(m, of_peak) - uint128(2);
            if (segment_peak > stats.max_value) {
                stats.max_value = segment_peak;
            }
        }

        // Track stopping time (sigma)
        if (!has_stopped_sigma) {
            int L_m = 128 - count_leading_zeros(n_new);
            int L_n = 128 - count_leading_zeros(stats.start_val);
            int j = L_m - L_n;
            if (j < 1) j = 1;

            if (stats.start_val <= shift_right(n_new, j)) {
                j++;
            }
            if (j <= beta) {
                stats.stopping_time = t_steps + alpha + j;
                has_stopped_sigma = true;
            }
        }

        stats.steps += 2 * alpha + beta;
        t_steps += alpha + beta;
        curr = shift_right(n_new, beta);

        if (curr < stats.start_val) {
            dropped_below_start = true;
        }
    }

    // Once the value drops below 2^N, look up the remaining steps in the steps table
    if (curr > uint128(1)) {
        stats.steps += steps_table[curr.low];
    }

    return stats;
}

CollatzStats compute_collatz_poly(uint128 n, uint32_t current_max_steps) {
    if (use_domain_switching) {
        return compute_collatz_poly_domain(n, current_max_steps);
    } else {
        return compute_collatz_poly_std(n, current_max_steps);
    }
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
        if (!predictor.active_predictions.empty()) {
            predictor.process_up_to(curr, steps_peaks);
            global_peaks.current_max_steps = predictor.current_max_steps;
        }

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
        #ifdef TRACK_ALMOST_PEAKS

        if (stats.steps + 2 >= global_peaks.current_max_steps) {

            predictor.add_confirmed_peak(curr, stats.steps);

            if (stats.steps > global_peaks.current_max_steps) {

                global_peaks.current_max_steps = predictor.current_max_steps;

                steps_peaks.push_back({curr, uint128(stats.steps)});

            } else {

                global_peaks.almost_steps_peaks.push_back({curr, uint128(stats.steps)});

            }

        }

        #else

        if (stats.steps > global_peaks.current_max_steps) {

            predictor.add_confirmed_peak(curr, stats.steps);

            global_peaks.current_max_steps = predictor.current_max_steps;

            steps_peaks.push_back({curr, uint128(stats.steps)});

        }

        #endif

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
        if (!predictor.active_predictions.empty()) {
            predictor.process_up_to(u128_curr, steps_peaks);
            global_peaks.current_max_steps = predictor.current_max_steps;
        }

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
                uint64_t next_val_shifted = next_val >> p;
                if (!has_stopped_sigma) {
                    if (next_val_shifted < val) {
                        for (int k = 1; k <= p; ++k) {
                            uint64_t val_k = next_val >> k;
                            if (val_k < val) {
                                stopping_time = t_steps + k;
                                has_stopped_sigma = true;
                                break;
                            }
                        }
                    }
                }

                steps += p;
                t_steps += p;
                temp_curr = next_val_shifted;

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

        #ifdef TRACK_ALMOST_PEAKS


        if (steps + 2 >= global_peaks.current_max_steps) {


            predictor.add_confirmed_peak(uint128(curr), steps);


            if (steps > global_peaks.current_max_steps) {


                global_peaks.current_max_steps = predictor.current_max_steps;


                steps_peaks.push_back({uint128(curr), uint128(steps)});


            } else {


                global_peaks.almost_steps_peaks.push_back({uint128(curr), uint128(steps)});


            }


        }


        #else


        if (steps > global_peaks.current_max_steps) {


            predictor.add_confirmed_peak(uint128(curr), steps);


            global_peaks.current_max_steps = predictor.current_max_steps;


            steps_peaks.push_back({uint128(curr), uint128(steps)});


        }


        #endif

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

    auto last_report_time = std::chrono::steady_clock::now();
    double report_interval = 3600.0;
    const char* env_interval = std::getenv("HAILSTONE_REPORT_INTERVAL");
    if (env_interval) {
        try {
            report_interval = std::stod(env_interval);
        } catch (...) {}
    }

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

            #ifdef TRACK_ALMOST_PEAKS

            for (const auto& peak : global_peaks.almost_steps_peaks) {

                predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);

            }

            #endif
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


                #ifdef TRACK_ALMOST_PEAKS


                for (const auto& peak : global_peaks.almost_steps_peaks) {


                    predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);


                }


                #endif
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

            #ifdef TRACK_ALMOST_PEAKS

            for (const auto& peak : global_peaks.almost_steps_peaks) {

                predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);

            }

            #endif
            predictor.prune_predictions_less_than(curr_start);
        }

        // Time-based progress update
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time).count() >= report_interval) {
            uint64_t current_block = (curr_start.high << 32) | (curr_start.low >> 32);
            uint64_t start_block = (start_num.high << 32) | (start_num.low >> 32);
            uint64_t blocks_searched = current_block - start_block;
            std::cout << "[Progress Update] Blocks searched: " << blocks_searched
                      << ", Current block: " << current_block << std::endl;
            last_report_time = now;
        }
    }
}

void cpu_search_blocks_gt_0_suffix_first(uint128 start_num, uint128 end_num,
                                         int width,
                                         const BaseDependentSuffixes& base_suffixes,
                                         std::vector<PeakRecord>& max_value_peaks,
                                         std::vector<PeakRecord>& steps_peaks,
                                         std::vector<PeakRecord>& sigma_peaks,
                                         PeakState& global_peaks,
                                         SearchMetrics& metrics,
                                         bool avx512_enabled) {
    if (start_num < uint128(0x100000000ULL)) {
        throw std::invalid_argument("cpu_search_blocks_gt_0_suffix_first: range starts below block 1");
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

    auto last_report_time = std::chrono::steady_clock::now();
    double report_interval = 3600.0;
    const char* env_interval = std::getenv("HAILSTONE_REPORT_INTERVAL");
    if (env_interval) {
        try {
            report_interval = std::stod(env_interval);
        } catch (...) {}
    }

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

#ifdef HAS_AVX512_COMPILER_SUPPORT
            if (avx512_enabled && is_avx512_supported() && block_end.high == 0) {
                cpu_search_range_suffix_first_avx512(curr_start, block_end, width, base_suffixes,
                                                     max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
            } else {
                cpu_search_range_suffix_first(curr_start, block_end, width, base_suffixes,
                                              max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
            }
#else
            cpu_search_range_suffix_first(curr_start, block_end, width, base_suffixes,
                                          max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
#endif

            // Rebuild the master predictor from the updated steps peaks list
            predictor = PeakPredictor();
            for (const auto& peak : steps_peaks) {
                predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
            }

            #ifdef TRACK_ALMOST_PEAKS

            for (const auto& peak : global_peaks.almost_steps_peaks) {

                predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);

            }

            #endif
            predictor.prune_predictions_less_than(block_end + uint128(1));

            curr_start = block_end + uint128(1);
            force_sequential = false; // Reset flag after one sequential run
        } else {
            // Parallel search blocks
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
#ifdef HAS_AVX512_COMPILER_SUPPORT
                if (avx512_enabled && is_avx512_supported() && block_ends[0].high == 0) {
                    cpu_search_range_suffix_first_avx512(block_starts[0], block_ends[0], width, base_suffixes,
                                                         max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
                } else {
                    cpu_search_range_suffix_first(block_starts[0], block_ends[0], width, base_suffixes,
                                                  max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
                }
#else
                cpu_search_range_suffix_first(block_starts[0], block_ends[0], width, base_suffixes,
                                              max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
#endif

                predictor = PeakPredictor();
                for (const auto& peak : steps_peaks) {
                    predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
                }


                #ifdef TRACK_ALMOST_PEAKS


                for (const auto& peak : global_peaks.almost_steps_peaks) {


                    predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);


                }


                #endif
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
#ifdef HAS_AVX512_COMPILER_SUPPORT
                if (avx512_enabled && is_avx512_supported() && block_ends[i].high == 0) {
                    cpu_search_range_suffix_first_avx512(block_starts[i], block_ends[i], width, base_suffixes,
                                                         local_max_value_peaks[i],
                                                         local_steps_peaks[i],
                                                         local_sigma_peaks[i],
                                                         local_global_peaks[i],
                                                         local_metrics[i]);
                } else {
                    cpu_search_range_suffix_first(block_starts[i], block_ends[i], width, base_suffixes,
                                                  local_max_value_peaks[i],
                                                  local_steps_peaks[i],
                                                  local_sigma_peaks[i],
                                                  local_global_peaks[i],
                                                  local_metrics[i]);
                }
#else
                cpu_search_range_suffix_first(block_starts[i], block_ends[i], width, base_suffixes,
                                              local_max_value_peaks[i],
                                              local_steps_peaks[i],
                                              local_sigma_peaks[i],
                                              local_global_peaks[i],
                                              local_metrics[i]);
#endif
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

            #ifdef TRACK_ALMOST_PEAKS

            for (const auto& peak : global_peaks.almost_steps_peaks) {

                predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);

            }

            #endif
            predictor.prune_predictions_less_than(curr_start);
        }

        // Time-based progress update
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time).count() >= report_interval) {
            uint64_t current_block = (curr_start.high << 32) | (curr_start.low >> 32);
            uint64_t start_block = (start_num.high << 32) | (start_num.low >> 32);
            uint64_t blocks_searched = current_block - start_block;
            std::cout << "[Progress Update] Blocks searched: " << blocks_searched
                      << ", Current block: " << current_block << std::endl;
            last_report_time = now;
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
        for (int B = 0; B < 9; ++B) {
            uint32_t rem = (B + s) % 9;
            if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
                res.std_skipped[B]++;
            }
        }
    }

    // Build base-dependent allowed lists
    for (const auto& pair : classes) {
        const auto& info = pair.second;
        if (info.has_even) continue;
        
        uint32_t r1 = info.first_suffix;
        // For each base B % 9, check if any member of the class lands on a skipped residue
        for (int B = 0; B < 9; ++B) {
            bool has_skipped = false;
            for (uint32_t m : info.members) {
                uint32_t rem = (B + m) % 9;
                if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
                    has_skipped = true;
                    break;
                }
            }
            if (!has_skipped) {
                res.allowed_tables[B].push_back(r1);
            }
        }
    }
    
    for (int B = 0; B < 9; ++B) {
        std::sort(res.allowed_tables[B].begin(), res.allowed_tables[B].end());
    }
    
    return res;
}

bool load_allowed_suffixes_binary(const std::string& filepath, BaseDependentSuffixes& suffixes) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return false;

    uint32_t header[19];
    if (fread(header, sizeof(uint32_t), 19, fp) != 19) {
        fclose(fp);
        return false;
    }

    uint32_t std_count = header[0];
    suffixes.std_allowed.resize(std_count);
    for (int i = 0; i < 9; ++i) {
        suffixes.allowed_tables[i].resize(header[1 + i]);
        suffixes.std_skipped[i] = header[10 + i];
    }

    if (fread(suffixes.std_allowed.data(), sizeof(uint32_t), std_count, fp) != std_count) {
        fclose(fp);
        return false;
    }

    for (int i = 0; i < 9; ++i) {
        size_t size = suffixes.allowed_tables[i].size();
        if (size > 0) {
            if (fread(suffixes.allowed_tables[i].data(), sizeof(uint32_t), size, fp) != size) {
                fclose(fp);
                return false;
            }
        }
    }

    fclose(fp);
    return true;
}

BaseDependentSuffixes load_allowed_suffixes_24() {
    BaseDependentSuffixes suffixes;
    std::vector<std::string> paths = {
        "build/allowed_suffixes_24.bin",
        "allowed_suffixes_24.bin",
        "cpu/allowed_suffixes_24.bin",
        "gpu_vulkan/allowed_suffixes_24.bin"
    };
    bool loaded = false;
    for (const auto& path : paths) {
        if (load_allowed_suffixes_binary(path, suffixes)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        std::cerr << "\nError: Could not load build-time precomputed allowed_suffixes_24.bin file from any search path!" << std::endl;
        std::cerr << "Please ensure the binary file was generated and exists." << std::endl;
        std::exit(1);
    }
    return suffixes;
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
        uint64_t base_mod9 = base % 9;
        
        const std::vector<uint32_t>& allowed = base_suffixes.allowed_tables[base_mod9];

        bool is_fully_in_bounds = (x > start_prefix && x < end_prefix);

        if (is_fully_in_bounds) {
            uint32_t std_skipped = base_suffixes.std_skipped[base_mod9];
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

                #ifdef TRACK_ALMOST_PEAKS


                if (steps + 2 >= global_peaks.current_max_steps) {


                    predictor.add_confirmed_peak(uint128(curr), steps);


                    if (steps > global_peaks.current_max_steps) {


                        global_peaks.current_max_steps = predictor.current_max_steps;


                        steps_peaks.push_back({uint128(curr), uint128(steps)});


                    } else {


                        global_peaks.almost_steps_peaks.push_back({uint128(curr), uint128(steps)});


                    }


                }


                #else


                if (steps > global_peaks.current_max_steps) {


                    predictor.add_confirmed_peak(uint128(curr), steps);


                    global_peaks.current_max_steps = predictor.current_max_steps;


                    steps_peaks.push_back({uint128(curr), uint128(steps)});


                }


                #endif

                if (stopping_time > global_peaks.current_max_sigma) {
                    global_peaks.current_max_sigma = stopping_time;
                    sigma_peaks.push_back({uint128(curr), uint128(stopping_time)});
                }
            }
        } else {
            // Boundary block: perform individual prefix bounds checking and exact metrics tracking
            for (uint32_t suffix : base_suffixes.std_allowed) {
                uint64_t curr = base | suffix;
                
                if (curr < start_64) continue;
                if (curr > end_64) break;

                uint32_t rem = (base_mod9 + suffix) % 9;
                if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
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

                #ifdef TRACK_ALMOST_PEAKS


                if (steps + 2 >= global_peaks.current_max_steps) {


                    predictor.add_confirmed_peak(uint128(curr), steps);


                    if (steps > global_peaks.current_max_steps) {


                        global_peaks.current_max_steps = predictor.current_max_steps;


                        steps_peaks.push_back({uint128(curr), uint128(steps)});


                    } else {


                        global_peaks.almost_steps_peaks.push_back({uint128(curr), uint128(steps)});


                    }


                }


                #else


                if (steps > global_peaks.current_max_steps) {


                    predictor.add_confirmed_peak(uint128(curr), steps);


                    global_peaks.current_max_steps = predictor.current_max_steps;


                    steps_peaks.push_back({uint128(curr), uint128(steps)});


                }


                #endif

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

    uint32_t std_allowed_size = (uint32_t)base_suffixes.std_allowed.size();

    for (uint128 x = start_prefix; x <= end_prefix; x = x + uint128(1)) {
        uint128 base = shift_left(x, width);
        uint64_t base_mod9 = ((base.high % 9) * 7 + (base.low % 9)) % 9;
        
        const std::vector<uint32_t>& allowed = base_suffixes.allowed_tables[base_mod9];

        bool is_fully_in_bounds = (x > start_prefix && x < end_prefix);

        if (is_fully_in_bounds) {
            uint32_t std_skipped = base_suffixes.std_skipped[base_mod9];
            metrics.total_numbers_checked += std_allowed_size;
            metrics.numbers_skipped_mod6 += std_skipped;

            bool check_predictor = false;
            if (!predictor.active_predictions.empty()) {
                uint128 min_pred = predictor.active_predictions[0].pred_n;
                for (const auto& p : predictor.active_predictions) {
                    if (p.pred_n < min_pred) min_pred = p.pred_n;
                }
                uint128 base_end = base + uint128((1ULL << width) - 1);
                if (min_pred <= base_end) {
                    check_predictor = true;
                }
            }

            if (check_predictor) {
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
                    #ifdef TRACK_ALMOST_PEAKS

                    if (stats.steps + 2 >= global_peaks.current_max_steps) {

                        predictor.add_confirmed_peak(curr, stats.steps);

                        if (stats.steps > global_peaks.current_max_steps) {

                            global_peaks.current_max_steps = predictor.current_max_steps;

                            steps_peaks.push_back({curr, uint128(stats.steps)});

                        } else {

                            global_peaks.almost_steps_peaks.push_back({curr, uint128(stats.steps)});

                        }

                    }

                    #else

                    if (stats.steps > global_peaks.current_max_steps) {

                        predictor.add_confirmed_peak(curr, stats.steps);

                        global_peaks.current_max_steps = predictor.current_max_steps;

                        steps_peaks.push_back({curr, uint128(stats.steps)});

                    }

                    #endif

                    // Check stopping time (sigma) peak
                    if (stats.stopping_time > global_peaks.current_max_sigma) {
                        global_peaks.current_max_sigma = stats.stopping_time;
                        sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
                    }
                }
            } else {
                for (uint32_t suffix : allowed) {
                    uint128 curr = base + uint128(suffix);

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
                    #ifdef TRACK_ALMOST_PEAKS

                    if (stats.steps + 2 >= global_peaks.current_max_steps) {

                        predictor.add_confirmed_peak(curr, stats.steps);

                        if (stats.steps > global_peaks.current_max_steps) {

                            global_peaks.current_max_steps = predictor.current_max_steps;

                            steps_peaks.push_back({curr, uint128(stats.steps)});

                        } else {

                            global_peaks.almost_steps_peaks.push_back({curr, uint128(stats.steps)});

                        }

                    }

                    #else

                    if (stats.steps > global_peaks.current_max_steps) {

                        predictor.add_confirmed_peak(curr, stats.steps);

                        global_peaks.current_max_steps = predictor.current_max_steps;

                        steps_peaks.push_back({curr, uint128(stats.steps)});

                    }

                    #endif

                    // Check stopping time (sigma) peak
                    if (stats.stopping_time > global_peaks.current_max_sigma) {
                        global_peaks.current_max_sigma = stats.stopping_time;
                        sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
                    }
                }
            }
        } else {
            // Boundary block: perform individual prefix bounds checking and exact metrics tracking
            for (uint32_t suffix : base_suffixes.std_allowed) {
                uint128 curr = base + uint128(suffix);

                if (curr < start) continue;
                if (curr > end) break;

                uint32_t rem = (base_mod9 + suffix) % 9;
                if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
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
                #ifdef TRACK_ALMOST_PEAKS

                if (stats.steps + 2 >= global_peaks.current_max_steps) {

                    predictor.add_confirmed_peak(curr, stats.steps);

                    if (stats.steps > global_peaks.current_max_steps) {

                        global_peaks.current_max_steps = predictor.current_max_steps;

                        steps_peaks.push_back({curr, uint128(stats.steps)});

                    } else {

                        global_peaks.almost_steps_peaks.push_back({curr, uint128(stats.steps)});

                    }

                }

                #else

                if (stats.steps > global_peaks.current_max_steps) {

                    predictor.add_confirmed_peak(curr, stats.steps);

                    global_peaks.current_max_steps = predictor.current_max_steps;

                    steps_peaks.push_back({curr, uint128(stats.steps)});

                }

                #endif

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

