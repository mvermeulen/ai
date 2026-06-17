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

void cpu_search_range_suffix_first_avx512(uint128 start, uint128 end, 
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

    // Stack-allocated lane data aligned to 64-byte boundaries for AVX-512 loading/storing
    alignas(64) uint64_t lane_start_val[8] = {0};
    alignas(64) uint64_t lane_curr[8] = {0};
    alignas(64) uint64_t lane_steps[8] = {0};
    alignas(64) uint64_t lane_max_val[8] = {0};
    alignas(64) uint64_t lane_suffix[8] = {0};

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

                if ((base_mod3 + suffix) % 3 == 2) {
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

#endif // HAS_AVX512_COMPILER_SUPPORT
