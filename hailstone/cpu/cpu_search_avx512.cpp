#ifdef HAS_AVX512_COMPILER_SUPPORT

#include "cpu_search.h"
#include "peak_predictor.h"
#include <chrono>
#include <vector>
#include <immintrin.h>
#include <algorithm>

#ifndef POLY_WIDTH
#define POLY_WIDTH 8
#endif

// Declare steps8 as extern to prevent duplicate definitions in link phase
extern "C" {
    extern const uint32_t steps8[256];
}

#define CONCAT_IMPL(x, y) x##y
#define CONCAT(x, y) CONCAT_IMPL(x, y)
#define steps_table CONCAT(steps, POLY_WIDTH)

// 64-bit Collatz intermediate overflow threshold: 3 * x + 1 > 2^64
constexpr uint64_t OVERFLOW_LIMIT = 0x5555555555555555ULL;

void cpu_search_range_suffix_first_avx512_std(uint128 start, uint128 end, 
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

    // Stack-allocated lane data aligned to 64-byte boundaries for AVX-512 loading/storing
    alignas(64) uint64_t lane_start_val[8] = {0};
    alignas(64) uint64_t lane_curr[8] = {0};
    alignas(64) uint64_t lane_steps[8] = {0};
    alignas(64) uint64_t lane_max_val[8] = {0};
    alignas(64) uint64_t lane_suffix[8] = {0};

    for (uint128 x = start_prefix; x <= end_prefix; x = x + uint128(1)) {
        uint128 base = shift_left(x, width);
        uint64_t base_mod9 = ((base.high % 9) * 7 + (base.low % 9)) % 9;
        
        const std::vector<uint32_t>& allowed = base_suffixes.allowed_tables[base_mod9];

        bool is_fully_in_bounds = (x > start_prefix && x < end_prefix);

        if (is_fully_in_bounds) {
            uint32_t std_skipped = base_suffixes.std_skipped[base_mod9];
            metrics.total_numbers_checked += std_allowed_size;
            metrics.numbers_skipped_mod6 += std_skipped;

            uint32_t allowed_idx = 0;
            uint32_t allowed_size = (uint32_t)allowed.size();

            // Populate initial 8 vector lanes
            __mmask8 active_mask = 0;
            for (int lane = 0; lane < 8; ++lane) {
                if (allowed_idx < allowed_size) {
                    uint32_t suffix = allowed[allowed_idx++];
                    uint128 curr_val = base + uint128(suffix);
                    lane_start_val[lane] = curr_val.low;
                    lane_curr[lane] = curr_val.low;
                    lane_steps[lane] = 0;
                    lane_max_val[lane] = curr_val.low;
                    lane_suffix[lane] = suffix;
                    active_mask |= (1 << lane);
                }
            }

            if (active_mask == 0) continue;

            // Load initial state into registers
            __m512i v_start_val = _mm512_load_epi64(lane_start_val);
            __m512i v_curr = _mm512_load_epi64(lane_curr);
            __m512i v_steps = _mm512_load_epi64(lane_steps);
            __m512i v_max_val = _mm512_load_epi64(lane_max_val);

            // Hoist loop-invariant vector constants outside the while loop
            const __m512i v_zero = _mm512_setzero_si512();
            const __m512i v_one = _mm512_set1_epi64(1);
            const __m512i v_minus_one = _mm512_set1_epi64(-1LL);
            const __m512i v_63 = _mm512_set1_epi64(63);
            const __m512i v_1050 = _mm512_set1_epi64(1050);
            const __m512i v_overflow_limit = _mm512_set1_epi64(OVERFLOW_LIMIT);
            const __m512i v_block_limit = _mm512_set1_epi64(0x100000000ULL);
            const __m512i v_init_max_steps = _mm512_set1_epi64(init_max_steps);
            const __m512i v_poly_width_mask = _mm512_set1_epi64(1 << POLY_WIDTH);

            __m512i v_dropped = v_zero; // 0: not dropped below start, -1: dropped

            while (active_mask != 0) {
                // Safeguard against 64-bit overflow (curr > OVERFLOW_LIMIT)
                __mmask8 overflow_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_curr, v_overflow_limit, _MM_CMPINT_GT);
                if (overflow_mask != 0) {
                    _mm512_store_epi64(lane_curr, v_curr);
                    _mm512_store_epi64(lane_steps, v_steps);
                    _mm512_store_epi64(lane_max_val, v_max_val);
                    for (int lane = 0; lane < 8; ++lane) {
                        if ((overflow_mask & (1 << lane)) && (active_mask & (1 << lane))) {
                            // Extract lane and delegate to scalar 128-bit loop
                            uint128 start_val_128 = base + uint128(lane_suffix[lane]);
                            CollatzStats stats = compute_collatz_poly(start_val_128, init_max_steps);
                            metrics.total_steps_computed += stats.steps;

                            // Update peaks scalar-side
                            predictor.process_up_to(start_val_128, steps_peaks);
                            global_peaks.current_max_steps = predictor.current_max_steps;
                            if (stats.max_value > global_peaks.current_max_value) {
                                global_peaks.current_max_value = stats.max_value;
                                max_value_peaks.push_back({start_val_128, stats.max_value});
                            }
                            #ifdef TRACK_ALMOST_PEAKS

                            if (stats.steps + 2 >= global_peaks.current_max_steps) {

                                predictor.add_confirmed_peak(start_val_128, stats.steps);

                                if (stats.steps > global_peaks.current_max_steps) {

                                    global_peaks.current_max_steps = predictor.current_max_steps;

                                    steps_peaks.push_back({start_val_128, uint128(stats.steps)});

                                } else {

                                    global_peaks.almost_steps_peaks.push_back({start_val_128, uint128(stats.steps)});

                                }

                            }

                            #else

                            if (stats.steps > global_peaks.current_max_steps) {

                                predictor.add_confirmed_peak(start_val_128, stats.steps);

                                global_peaks.current_max_steps = predictor.current_max_steps;

                                steps_peaks.push_back({start_val_128, uint128(stats.steps)});

                            }

                            #endif
                            if (stats.stopping_time > global_peaks.current_max_sigma) {
                                global_peaks.current_max_sigma = stats.stopping_time;
                                sigma_peaks.push_back({start_val_128, uint128(stats.stopping_time)});
                            }

                            // Deactivate lane
                            active_mask &= ~(1 << lane);
                        }
                    }
                    // Reload active state into registers
                    v_curr = _mm512_load_epi64(lane_curr);
                    v_steps = _mm512_load_epi64(lane_steps);
                    v_max_val = _mm512_load_epi64(lane_max_val);
                }

                if (active_mask == 0) break;

                // Update dropped status for active lanes using current v_curr before the step
                __mmask8 dropped_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_curr, v_start_val, _MM_CMPINT_LT);
                v_dropped = _mm512_mask_blend_epi64(dropped_mask, v_dropped, v_minus_one);

                // Check termination conditions (Escape & Prune) BEFORE Collatz math
                __mmask8 escape_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_curr, v_poly_width_mask, _MM_CMPINT_LT);

                __mmask8 dropped_bits = _mm512_mask_cmp_epi64_mask(active_mask, v_dropped, v_zero, _MM_CMPINT_NE);
                __mmask8 in_block_0_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_curr, v_block_limit, _MM_CMPINT_LT);
                __m512i v_steps_offset = _mm512_add_epi64(v_steps, v_1050);
                __mmask8 pruned_bits = _mm512_mask_cmp_epu64_mask(active_mask & dropped_bits & in_block_0_mask, v_steps_offset, v_init_max_steps, _MM_CMPINT_LT);

                __mmask8 completed_mask = escape_mask | pruned_bits;

                if (completed_mask != 0) {
                    _mm512_store_epi64(lane_curr, v_curr);
                    _mm512_store_epi64(lane_steps, v_steps);
                    _mm512_store_epi64(lane_max_val, v_max_val);

                    for (int lane = 0; lane < 8; ++lane) {
                        if ((completed_mask & (1 << lane)) && (active_mask & (1 << lane))) {
                            uint64_t steps = lane_steps[lane];
                            if (!(pruned_bits & (1 << lane))) {
                                // Add remaining steps from precomputed table
                                steps += steps_table[lane_curr[lane]];
                            }

                            uint128 start_val_128 = base + uint128(lane_suffix[lane]);
                            metrics.total_steps_computed += steps;

                            // Dynamic Peak Validation scalar-side check
                            uint32_t current_max_steps = global_peaks.current_max_steps;
                            uint64_t current_max_value = global_peaks.current_max_value.low;
                            uint32_t current_max_sigma = global_peaks.current_max_sigma;

                            if (steps > current_max_steps || lane_max_val[lane] > current_max_value || steps > current_max_sigma) {
                                CollatzStats stats = compute_collatz_poly(start_val_128, init_max_steps);
                                predictor.process_up_to(start_val_128, steps_peaks);
                                global_peaks.current_max_steps = predictor.current_max_steps;
                                if (stats.max_value > global_peaks.current_max_value) {
                                    global_peaks.current_max_value = stats.max_value;
                                    max_value_peaks.push_back({start_val_128, stats.max_value});
                                }
                                #ifdef TRACK_ALMOST_PEAKS

                                if (stats.steps + 2 >= global_peaks.current_max_steps) {

                                    predictor.add_confirmed_peak(start_val_128, stats.steps);

                                    if (stats.steps > global_peaks.current_max_steps) {

                                        global_peaks.current_max_steps = predictor.current_max_steps;

                                        steps_peaks.push_back({start_val_128, uint128(stats.steps)});

                                    } else {

                                        global_peaks.almost_steps_peaks.push_back({start_val_128, uint128(stats.steps)});

                                    }

                                }

                                #else

                                if (stats.steps > global_peaks.current_max_steps) {

                                    predictor.add_confirmed_peak(start_val_128, stats.steps);

                                    global_peaks.current_max_steps = predictor.current_max_steps;

                                    steps_peaks.push_back({start_val_128, uint128(stats.steps)});

                                }

                                #endif
                                if (stats.stopping_time > global_peaks.current_max_sigma) {
                                    global_peaks.current_max_sigma = stats.stopping_time;
                                    sigma_peaks.push_back({start_val_128, uint128(stats.stopping_time)});
                                }
                            }

                            // Mark lane inactive
                            active_mask &= ~(1 << lane);
                        }
                    }

                    if (active_mask != 0) {
                        // Reload active state into registers
                        v_curr = _mm512_load_epi64(lane_curr);
                        v_steps = _mm512_load_epi64(lane_steps);
                        v_max_val = _mm512_load_epi64(lane_max_val);
                    }
                }

                if (active_mask != 0) {
                    // Collatz math: next_val = 3 * curr_val + 1
                    __m512i v_next = _mm512_add_epi64(_mm512_slli_epi64(v_curr, 1), v_curr);
                    v_next = _mm512_add_epi64(v_next, v_one);

                    // Update max value for lanes that have NOT dropped below start
                    __mmask8 not_dropped_mask = _mm512_mask_cmp_epi64_mask(active_mask, v_dropped, v_zero, _MM_CMPINT_EQ);
                    v_max_val = _mm512_mask_max_epu64(v_max_val, not_dropped_mask, v_max_val, v_next);

                    // ctz = 63 - lzcnt(next & -next)
                    __m512i v_neg = _mm512_sub_epi64(v_zero, v_next);
                    __m512i v_lowest_bit = _mm512_and_si512(v_next, v_neg);
                    __m512i v_lz = _mm512_lzcnt_epi64(v_lowest_bit);
                    __m512i v_ctz = _mm512_sub_epi64(v_63, v_lz);

                    // Shift right variable
                    v_curr = _mm512_srlv_epi64(v_next, v_ctz);

                    // Steps increment = ctz + 1
                    __m512i v_inc = _mm512_add_epi64(v_ctz, v_one);
                    v_steps = _mm512_mask_add_epi64(v_steps, active_mask, v_steps, v_inc);
                }

                // Refill completed lanes to maintain 100% occupancy
                if (active_mask != 0xFF && allowed_idx < allowed_size) {
                    _mm512_store_epi64(lane_start_val, v_start_val);
                    _mm512_store_epi64(lane_curr, v_curr);
                    _mm512_store_epi64(lane_steps, v_steps);
                    _mm512_store_epi64(lane_max_val, v_max_val);
                    alignas(64) int64_t lane_dropped_temp[8];
                    _mm512_store_epi64(lane_dropped_temp, v_dropped);

                    for (int lane = 0; lane < 8; ++lane) {
                        if (!(active_mask & (1 << lane)) && allowed_idx < allowed_size) {
                            uint32_t suffix = allowed[allowed_idx++];
                            uint128 curr_val = base + uint128(suffix);
                            lane_start_val[lane] = curr_val.low;
                            lane_curr[lane] = curr_val.low;
                            lane_steps[lane] = 0;
                            lane_max_val[lane] = curr_val.low;
                            lane_suffix[lane] = suffix;
                            lane_dropped_temp[lane] = 0;
                            active_mask |= (1 << lane);
                        }
                    }

                    v_start_val = _mm512_load_epi64(lane_start_val);
                    v_curr = _mm512_load_epi64(lane_curr);
                    v_steps = _mm512_load_epi64(lane_steps);
                    v_max_val = _mm512_load_epi64(lane_max_val);
                    v_dropped = _mm512_load_epi64(lane_dropped_temp);
                }
            }
        } else {
            // Boundary block: perform individual prefix bounds checking and exact metrics tracking scalar-side
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

                bool is_pruned = !std::binary_search(allowed.begin(), allowed.end(), suffix);
                if (is_pruned) continue;

                predictor.process_up_to(curr, steps_peaks);
                global_peaks.current_max_steps = predictor.current_max_steps;

                CollatzStats stats = compute_collatz_poly(curr, init_max_steps);
                metrics.total_steps_computed += stats.steps;

                if (stats.max_value > global_peaks.current_max_value) {
                    global_peaks.current_max_value = stats.max_value;
                    max_value_peaks.push_back({curr, stats.max_value});
                }

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

                if (stats.stopping_time > global_peaks.current_max_sigma) {
                    global_peaks.current_max_sigma = stats.stopping_time;
                    sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
                }
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

const uint64_t lut3_64[] = {
    1ULL, // 3^0
    3ULL, // 3^1
    9ULL, // 3^2
    27ULL, // 3^3
    81ULL, // 3^4
    243ULL, // 3^5
    729ULL, // 3^6
    2187ULL, // 3^7
    6561ULL, // 3^8
    19683ULL, // 3^9
    59049ULL, // 3^10
    177147ULL, // 3^11
    531441ULL, // 3^12
    1594323ULL, // 3^13
    4782969ULL, // 3^14
    14348907ULL, // 3^15
    43046721ULL, // 3^16
    129140163ULL, // 3^17
    387420489ULL, // 3^18
    1162261467ULL, // 3^19
    3486784401ULL, // 3^20
    10460353203ULL, // 3^21
    31381059609ULL, // 3^22
    94143178827ULL, // 3^23
    282429536481ULL, // 3^24
    847288609443ULL, // 3^25
    2541865828329ULL, // 3^26
    7625597484987ULL, // 3^27
    22876792454961ULL, // 3^28
    68630377364883ULL, // 3^29
    205891132094649ULL, // 3^30
    617673396283947ULL, // 3^31
    1853020188851841ULL, // 3^32
    5559060566555523ULL, // 3^33
    16677181699666569ULL, // 3^34
    50031545098999707ULL, // 3^35
    150094635296999121ULL, // 3^36
    450283905890997363ULL, // 3^37
    1350851717672992089ULL, // 3^38
    4052555153018976267ULL, // 3^39
    12157665459056928801ULL, // 3^40
};

const uint64_t max_safe_k_64[] = {
    18446744073709551615ULL, // max k for alpha=0
    6148914691236517205ULL, // max k for alpha=1
    2049638230412172401ULL, // max k for alpha=2
    683212743470724133ULL, // max k for alpha=3
    227737581156908044ULL, // max k for alpha=4
    75912527052302681ULL, // max k for alpha=5
    25304175684100893ULL, // max k for alpha=6
    8434725228033631ULL, // max k for alpha=7
    2811575076011210ULL, // max k for alpha=8
    937191692003736ULL, // max k for alpha=9
    312397230667912ULL, // max k for alpha=10
    104132410222637ULL, // max k for alpha=11
    34710803407545ULL, // max k for alpha=12
    11570267802515ULL, // max k for alpha=13
    3856755934171ULL, // max k for alpha=14
    1285585311390ULL, // max k for alpha=15
    428528437130ULL, // max k for alpha=16
    142842812376ULL, // max k for alpha=17
    47614270792ULL, // max k for alpha=18
    15871423597ULL, // max k for alpha=19
    5290474532ULL, // max k for alpha=20
    1763491510ULL, // max k for alpha=21
    587830503ULL, // max k for alpha=22
    195943501ULL, // max k for alpha=23
    65314500ULL, // max k for alpha=24
    21771500ULL, // max k for alpha=25
    7257166ULL, // max k for alpha=26
    2419055ULL, // max k for alpha=27
    806351ULL, // max k for alpha=28
    268783ULL, // max k for alpha=29
    89594ULL, // max k for alpha=30
    29864ULL, // max k for alpha=31
    9954ULL, // max k for alpha=32
    3318ULL, // max k for alpha=33
    1106ULL, // max k for alpha=34
    368ULL, // max k for alpha=35
    122ULL, // max k for alpha=36
    40ULL, // max k for alpha=37
    13ULL, // max k for alpha=38
    4ULL, // max k for alpha=39
    1ULL, // max k for alpha=40
};

void cpu_search_range_suffix_first_avx512_domain(uint128 start, uint128 end, 
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

    // Stack-allocated lane data aligned to 64-byte boundaries for AVX-512 loading/storing
    alignas(64) uint64_t lane_start_val[8] = {0};
    alignas(64) uint64_t lane_curr[8] = {0};
    alignas(64) uint64_t lane_steps[8] = {0};
    alignas(64) uint64_t lane_max_val[8] = {0};
    alignas(64) uint64_t lane_suffix[8] = {0};
    alignas(64) uint64_t lane_t_steps[8] = {0}; // Tracks division-by-2 steps for stopping time
    alignas(64) uint64_t lane_stopping_time[8] = {0};
    alignas(64) uint64_t lane_has_stopped_sigma[8] = {0};

    for (uint128 x = start_prefix; x <= end_prefix; x = x + uint128(1)) {
        uint128 base = shift_left(x, width);
        uint64_t base_mod9 = ((base.high % 9) * 7 + (base.low % 9)) % 9;
        
        const std::vector<uint32_t>& allowed = base_suffixes.allowed_tables[base_mod9];

        bool is_fully_in_bounds = (x > start_prefix && x < end_prefix);

        if (is_fully_in_bounds) {
            uint32_t std_skipped = base_suffixes.std_skipped[base_mod9];
            metrics.total_numbers_checked += std_allowed_size;
            metrics.numbers_skipped_mod6 += std_skipped;

            uint32_t allowed_idx = 0;
            uint32_t allowed_size = (uint32_t)allowed.size();

            // Populate initial 8 vector lanes
            __mmask8 active_mask = 0;
            for (int lane = 0; lane < 8; ++lane) {
                if (allowed_idx < allowed_size) {
                    uint32_t suffix = allowed[allowed_idx++];
                    uint128 curr_val = base + uint128(suffix);
                    lane_start_val[lane] = curr_val.low;
                    lane_curr[lane] = curr_val.low;
                    lane_steps[lane] = 0;
                    lane_max_val[lane] = curr_val.low;
                    lane_suffix[lane] = suffix;
                    lane_t_steps[lane] = 0;
                    lane_stopping_time[lane] = 0;
                    lane_has_stopped_sigma[lane] = 0;
                    active_mask |= (1 << lane);
                }
            }

            if (active_mask == 0) continue;

            // Load initial state into registers
            __m512i v_start_val = _mm512_load_epi64(lane_start_val);
            __m512i v_curr = _mm512_load_epi64(lane_curr);
            __m512i v_steps = _mm512_load_epi64(lane_steps);
            __m512i v_max_val = _mm512_load_epi64(lane_max_val);
            __m512i v_t_steps = _mm512_load_epi64(lane_t_steps);
            __m512i v_stopping_time = _mm512_load_epi64(lane_stopping_time);
            __m512i v_has_stopped_sigma = _mm512_load_epi64(lane_has_stopped_sigma);

            // Hoist loop-invariant vector constants outside the while loop
            const __m512i v_zero = _mm512_setzero_si512();
            const __m512i v_one = _mm512_set1_epi64(1);
            const __m512i v_two = _mm512_set1_epi64(2);
            const __m512i v_minus_one = _mm512_set1_epi64(-1LL);
            const __m512i v_63 = _mm512_set1_epi64(63);
            const __m512i v_64 = _mm512_set1_epi64(64);
            const __m512i v_1050 = _mm512_set1_epi64(1050);
            const __m512i v_cap_mask = _mm512_set1_epi64(1ULL << 40);
            const __m512i v_block_limit = _mm512_set1_epi64(0x100000000ULL);
            const __m512i v_init_max_steps = _mm512_set1_epi64(init_max_steps);
            const __m512i v_poly_width_mask = _mm512_set1_epi64(1 << POLY_WIDTH);

            __m512i v_dropped = v_zero; // 0: not dropped below start, -1: dropped

            while (active_mask != 0) {
                // Enter n+1 domain
                __m512i v_curr_plus_1 = _mm512_add_epi64(v_curr, v_one);

                // Cap alpha at 40
                __m512i v_curr_capped = _mm512_or_si512(v_curr_plus_1, v_cap_mask);

                // ctz of capped value = 63 - lzcnt(val & -val)
                __m512i v_neg = _mm512_sub_epi64(v_zero, v_curr_capped);
                __m512i v_lowest_bit = _mm512_and_si512(v_curr_capped, v_neg);
                __m512i v_lz = _mm512_lzcnt_epi64(v_lowest_bit);
                __m512i v_alpha = _mm512_sub_epi64(v_63, v_lz);

                // Right shift to get k
                __m512i v_k = _mm512_srlv_epi64(v_curr_plus_1, v_alpha);

                // Gather max_safe_k_64 thresholds
                __m512i v_max_safe_k = _mm512_i64gather_epi64(v_alpha, (long long const*)max_safe_k_64, 8);

                // Check lane overflow (k > max_safe_k)
                __mmask8 overflow_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_k, v_max_safe_k, _MM_CMPINT_GT);

                if (overflow_mask != 0) {
                    _mm512_store_epi64(lane_curr, v_curr);
                    _mm512_store_epi64(lane_steps, v_steps);
                    _mm512_store_epi64(lane_max_val, v_max_val);
                    _mm512_store_epi64(lane_stopping_time, v_stopping_time);

                    for (int lane = 0; lane < 8; ++lane) {
                        if ((overflow_mask & (1 << lane)) && (active_mask & (1 << lane))) {
                            // Extract lane and delegate to scalar 128-bit loop
                            uint128 start_val_128 = base + uint128(lane_suffix[lane]);
                            CollatzStats stats = compute_collatz_poly(start_val_128, init_max_steps);
                            metrics.total_steps_computed += stats.steps;

                            // Update peaks scalar-side
                            predictor.process_up_to(start_val_128, steps_peaks);
                            global_peaks.current_max_steps = predictor.current_max_steps;
                            if (stats.max_value > global_peaks.current_max_value) {
                                global_peaks.current_max_value = stats.max_value;
                                max_value_peaks.push_back({start_val_128, stats.max_value});
                            }
                            if (stats.steps > global_peaks.current_max_steps) {
                                predictor.add_confirmed_peak(start_val_128, stats.steps);
                                global_peaks.current_max_steps = predictor.current_max_steps;
                                steps_peaks.push_back({start_val_128, uint128(stats.steps)});
                            }
                            if (stats.stopping_time > global_peaks.current_max_sigma) {
                                global_peaks.current_max_sigma = stats.stopping_time;
                                sigma_peaks.push_back({start_val_128, uint128(stats.stopping_time)});
                            }

                            // Deactivate lane
                            active_mask &= ~(1 << lane);
                        }
                    }

                    // Reload active state
                    v_curr = _mm512_load_epi64(lane_curr);
                    v_steps = _mm512_load_epi64(lane_steps);
                    v_max_val = _mm512_load_epi64(lane_max_val);
                    v_stopping_time = _mm512_load_epi64(lane_stopping_time);
                }

                if (active_mask == 0) break;

                // Update dropped status using current v_curr before the step
                __mmask8 dropped_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_curr, v_start_val, _MM_CMPINT_LT);
                v_dropped = _mm512_mask_blend_epi64(dropped_mask, v_dropped, v_minus_one);

                // Gather lut3_64 multipliers
                __m512i v_lut3 = _mm512_i64gather_epi64(v_alpha, (long long const*)lut3_64, 8);

                // Multiply to get m = k * 3^alpha
                __m512i v_m = _mm512_mullo_epi64(v_k, v_lut3);

                // Exit n+1 domain to get n_new = m - 1
                __m512i v_n_new = _mm512_sub_epi64(v_m, v_one);

                // Update max value for lanes that have NOT dropped below start
                __mmask8 not_dropped_mask = _mm512_mask_cmp_epi64_mask(active_mask, v_dropped, v_zero, _MM_CMPINT_EQ);
                __m512i v_segment_peak = _mm512_sub_epi64(_mm512_slli_epi64(v_m, 1), v_two); // 2m - 2
                v_max_val = _mm512_mask_max_epu64(v_max_val, not_dropped_mask, v_max_val, v_segment_peak);

                // Check termination conditions (Escape & Prune) BEFORE shifting right by beta
                __mmask8 escape_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_n_new, v_poly_width_mask, _MM_CMPINT_LT);

                __mmask8 dropped_bits = _mm512_mask_cmp_epi64_mask(active_mask, v_dropped, v_zero, _MM_CMPINT_NE);
                __mmask8 in_block_0_mask = _mm512_mask_cmp_epu64_mask(active_mask, v_n_new, v_block_limit, _MM_CMPINT_LT);
                __m512i v_steps_offset = _mm512_add_epi64(v_steps, v_1050);
                __mmask8 pruned_bits = _mm512_mask_cmp_epu64_mask(active_mask & dropped_bits & in_block_0_mask, v_steps_offset, v_init_max_steps, _MM_CMPINT_LT);

                __mmask8 completed_mask = escape_mask | pruned_bits;

                if (completed_mask != 0) {
                    _mm512_store_epi64(lane_curr, v_curr);
                    _mm512_store_epi64(lane_steps, v_steps);
                    _mm512_store_epi64(lane_max_val, v_max_val);
                    _mm512_store_epi64(lane_stopping_time, v_stopping_time);

                    for (int lane = 0; lane < 8; ++lane) {
                        if ((completed_mask & (1 << lane)) && (active_mask & (1 << lane))) {
                            uint64_t steps = lane_steps[lane];
                            if (!(pruned_bits & (1 << lane))) {
                                // Add remaining steps from precomputed table
                                steps += steps_table[lane_curr[lane]];
                            }

                            uint128 start_val_128 = base + uint128(lane_suffix[lane]);
                            metrics.total_steps_computed += steps;

                            // Dynamic Peak Validation scalar-side check
                            uint32_t current_max_steps = global_peaks.current_max_steps;
                            uint64_t current_max_value = global_peaks.current_max_value.low;
                            uint32_t current_max_sigma = global_peaks.current_max_sigma;
                            uint32_t stopping_time = lane_stopping_time[lane];

                            if (steps > current_max_steps || lane_max_val[lane] > current_max_value || stopping_time > current_max_sigma) {
                                CollatzStats stats = compute_collatz_poly(start_val_128, init_max_steps);
                                predictor.process_up_to(start_val_128, steps_peaks);
                                global_peaks.current_max_steps = predictor.current_max_steps;
                                if (stats.max_value > global_peaks.current_max_value) {
                                    global_peaks.current_max_value = stats.max_value;
                                    max_value_peaks.push_back({start_val_128, stats.max_value});
                                }
                                if (stats.steps > global_peaks.current_max_steps) {
                                    predictor.add_confirmed_peak(start_val_128, stats.steps);
                                    global_peaks.current_max_steps = predictor.current_max_steps;
                                    steps_peaks.push_back({start_val_128, uint128(stats.steps)});
                                }
                                if (stats.stopping_time > global_peaks.current_max_sigma) {
                                    global_peaks.current_max_sigma = stats.stopping_time;
                                    sigma_peaks.push_back({start_val_128, uint128(stats.stopping_time)});
                                }
                            }

                            // Mark lane inactive
                            active_mask &= ~(1 << lane);
                        }
                    }

                    if (active_mask != 0) {
                        v_curr = _mm512_load_epi64(lane_curr);
                        v_steps = _mm512_load_epi64(lane_steps);
                        v_max_val = _mm512_load_epi64(lane_max_val);
                        v_stopping_time = _mm512_load_epi64(lane_stopping_time);
                    }
                }

                if (active_mask == 0) break;

                // Find trailing zeros of n_new (beta)
                __m512i v_neg_n_new = _mm512_sub_epi64(v_zero, v_n_new);
                __m512i v_lowest_bit_beta = _mm512_and_si512(v_n_new, v_neg_n_new);
                __m512i v_lz_beta = _mm512_lzcnt_epi64(v_lowest_bit_beta);
                __m512i v_beta = _mm512_sub_epi64(v_63, v_lz_beta);

                // Update stopping time (sigma) vector-side
                __mmask8 has_no_sigma = _mm512_mask_cmp_epi64_mask(active_mask, v_has_stopped_sigma, v_zero, _MM_CMPINT_EQ);
                if (has_no_sigma != 0) {
                    // Compute L_m and L_n
                    __m512i v_lz_m = _mm512_lzcnt_epi64(v_n_new);
                    __m512i v_L_m = _mm512_sub_epi64(v_64, v_lz_m);

                    __m512i v_lz_n = _mm512_lzcnt_epi64(v_start_val);
                    __m512i v_L_n = _mm512_sub_epi64(v_64, v_lz_n);

                    __m512i v_j = _mm512_sub_epi64(v_L_m, v_L_n);
                    v_j = _mm512_max_epi64(v_j, v_one);

                    __m512i v_shifted_n_new = _mm512_srlv_epi64(v_n_new, v_j);
                    __mmask8 j_inc_mask = _mm512_mask_cmp_epu64_mask(has_no_sigma, v_start_val, v_shifted_n_new, _MM_CMPINT_LE);
                    v_j = _mm512_mask_add_epi64(v_j, j_inc_mask, v_j, v_one);

                    __mmask8 sigma_found = _mm512_mask_cmp_epu64_mask(has_no_sigma, v_j, v_beta, _MM_CMPINT_LE);
                    if (sigma_found != 0) {
                        __m512i v_new_sigma = _mm512_add_epi64(_mm512_add_epi64(v_t_steps, v_alpha), v_j);
                        v_stopping_time = _mm512_mask_blend_epi64(sigma_found, v_stopping_time, v_new_sigma);
                        v_has_stopped_sigma = _mm512_mask_blend_epi64(sigma_found, v_has_stopped_sigma, v_minus_one);
                    }
                }

                // Shift right by beta to get next odd value
                v_curr = _mm512_srlv_epi64(v_n_new, v_beta);

                // Increment steps & t_steps
                __m512i v_steps_inc = _mm512_add_epi64(_mm512_slli_epi64(v_alpha, 1), v_beta); // 2 * alpha + beta
                v_steps = _mm512_mask_add_epi64(v_steps, active_mask, v_steps, v_steps_inc);

                __m512i v_t_steps_inc = _mm512_add_epi64(v_alpha, v_beta); // alpha + beta
                v_t_steps = _mm512_mask_add_epi64(v_t_steps, active_mask, v_t_steps, v_t_steps_inc);

                // Refill completed lanes
                if (active_mask != 0xFF && allowed_idx < allowed_size) {
                    _mm512_store_epi64(lane_start_val, v_start_val);
                    _mm512_store_epi64(lane_curr, v_curr);
                    _mm512_store_epi64(lane_steps, v_steps);
                    _mm512_store_epi64(lane_max_val, v_max_val);
                    _mm512_store_epi64(lane_stopping_time, v_stopping_time);
                    
                    alignas(64) int64_t lane_dropped_temp[8];
                    _mm512_store_epi64(lane_dropped_temp, v_dropped);

                    alignas(64) int64_t lane_t_steps_temp[8];
                    _mm512_store_epi64(lane_t_steps_temp, v_t_steps);

                    alignas(64) int64_t lane_has_stopped_sigma_temp[8];
                    _mm512_store_epi64(lane_has_stopped_sigma_temp, v_has_stopped_sigma);

                    for (int lane = 0; lane < 8; ++lane) {
                        if (!(active_mask & (1 << lane)) && allowed_idx < allowed_size) {
                            uint32_t suffix = allowed[allowed_idx++];
                            uint128 curr_val = base + uint128(suffix);
                            lane_start_val[lane] = curr_val.low;
                            lane_curr[lane] = curr_val.low;
                            lane_steps[lane] = 0;
                            lane_max_val[lane] = curr_val.low;
                            lane_suffix[lane] = suffix;
                            lane_dropped_temp[lane] = 0;
                            lane_t_steps_temp[lane] = 0;
                            lane_stopping_time[lane] = 0;
                            lane_has_stopped_sigma_temp[lane] = 0;
                            active_mask |= (1 << lane);
                        }
                    }

                    v_start_val = _mm512_load_epi64(lane_start_val);
                    v_curr = _mm512_load_epi64(lane_curr);
                    v_steps = _mm512_load_epi64(lane_steps);
                    v_max_val = _mm512_load_epi64(lane_max_val);
                    v_dropped = _mm512_load_epi64(lane_dropped_temp);
                    v_t_steps = _mm512_load_epi64(lane_t_steps_temp);
                    v_stopping_time = _mm512_load_epi64(lane_stopping_time);
                    v_has_stopped_sigma = _mm512_load_epi64(lane_has_stopped_sigma_temp);
                }
            }
        } else {
            // Boundary block: perform individual prefix bounds checking and exact metrics tracking scalar-side
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

                bool is_pruned = !std::binary_search(allowed.begin(), allowed.end(), suffix);
                if (is_pruned) continue;

                predictor.process_up_to(curr, steps_peaks);
                global_peaks.current_max_steps = predictor.current_max_steps;

                CollatzStats stats = compute_collatz_poly(curr, init_max_steps);
                metrics.total_steps_computed += stats.steps;

                if (stats.max_value > global_peaks.current_max_value) {
                    global_peaks.current_max_value = stats.max_value;
                    max_value_peaks.push_back({curr, stats.max_value});
                }

                if (stats.steps > global_peaks.current_max_steps) {
                    predictor.add_confirmed_peak(curr, stats.steps);
                    global_peaks.current_max_steps = predictor.current_max_steps;
                    steps_peaks.push_back({curr, uint128(stats.steps)});
                }

                if (stats.stopping_time > global_peaks.current_max_sigma) {
                    global_peaks.current_max_sigma = stats.stopping_time;
                    sigma_peaks.push_back({curr, uint128(stats.stopping_time)});
                }
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

void cpu_search_range_suffix_first_avx512(uint128 start, uint128 end, 
                                          int width,
                                          const BaseDependentSuffixes& base_suffixes,
                                          std::vector<PeakRecord>& max_value_peaks,
                                          std::vector<PeakRecord>& steps_peaks,
                                          std::vector<PeakRecord>& sigma_peaks,
                                          PeakState& global_peaks,
                                          SearchMetrics& metrics) {
    if (use_domain_switching) {
        cpu_search_range_suffix_first_avx512_domain(start, end, width, base_suffixes, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
    } else {
        cpu_search_range_suffix_first_avx512_std(start, end, width, base_suffixes, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
    }
}

#endif // HAS_AVX512_COMPILER_SUPPORT

