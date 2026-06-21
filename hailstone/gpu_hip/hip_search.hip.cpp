#include "hip_search.hip.h"
#include "steps_table.h"
#include "peak_predictor.h"
#ifndef POLY_WIDTH
#define POLY_WIDTH 8
#endif
#include "fpoly_table.h"
#include <hip/hip_runtime.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <stdexcept>
#include <map>
#include <cstdlib>
#include <cstdio>

// Precomputed powers of 3 lookup table (up to 3^40)
const uint128 lut3[] = {
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
const uint128 max_safe_k[] = {
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


__constant__ uint32_t d_steps_table[256];
__constant__ poly d_fpoly_table[256];

struct uint128_pod {
    uint64_t low;
    uint64_t high;
};

__constant__ uint64_t d_lut3_64[41];
__constant__ uint64_t d_max_safe_k_64[41];
__constant__ uint128_pod d_lut3_128[41];
__constant__ uint128_pod d_max_safe_k_128[41];


__device__ inline uint128 mul_uint64_check_overflow(uint128 a, uint64_t b, bool& overflow) {
    if (b == 0) {
        overflow = false;
        return uint128(0, 0);
    }
    unsigned __int128 low_prod = (unsigned __int128)a.low * b;
    uint64_t low_prod_low = (uint64_t)low_prod;
    uint64_t low_prod_high = (uint64_t)(low_prod >> 64);

    unsigned __int128 high_prod = (unsigned __int128)a.high * b;
    unsigned __int128 final_high = high_prod + low_prod_high;

    if (final_high > 0xFFFFFFFFFFFFFFFFULL) {
        overflow = true;
        return uint128(0, 0);
    }
    overflow = false;
    return uint128(low_prod_low, (uint64_t)final_high);
}

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

template <bool USE_64BIT, bool USE_DOMAIN_SWITCHING>
__global__ void collatz_search_kernel(
    uint128 start,
    uint64_t total_odds,
    uint128 init_max_val,
    uint32_t init_max_steps,
    uint32_t init_max_sigma,
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

                if (USE_DOMAIN_SWITCHING) {
                    if (n_64 < 256) {
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
                    } else {
                        while (curr >= 256) {
                            curr = curr + 1;
                            int alpha = __builtin_ctzll(curr | (1ULL << 40));
                            uint64_t k = curr >> alpha;
                            if (k > d_max_safe_k_64[alpha]) {
                                overflowed = true;
                                break;
                            }
                            uint64_t m = k * d_lut3_64[alpha];
                            
                            if ((m & 0x8000000000000000ULL) != 0) {
                                overflowed = true;
                                break;
                            }

                            uint64_t n_new = m - 1;
                            int beta = __builtin_ctzll(n_new);

                            if (!dropped_below_start) {
                                uint64_t segment_peak = (m << 1) - 2;
                                if (segment_peak > max_val_64) {
                                    max_val_64 = segment_peak;
                                }
                            }

                            if (!has_stopped_sigma) {
                                int L_m = 64 - __builtin_clzll(n_new);
                                int L_n = 64 - __builtin_clzll(n_64);
                                int j = L_m - L_n;
                                if (j < 1) j = 1;

                                if (n_64 <= (n_new >> j)) {
                                    j++;
                                }
                                if (j <= beta) {
                                    stopping_time = t_steps + alpha + j;
                                    has_stopped_sigma = true;
                                }
                            }

                            steps += 2 * alpha + beta;
                            t_steps += alpha + beta;
                            curr = n_new >> beta;

                            if (curr < n_64) {
                                dropped_below_start = true;
                            }
                        }
                        if (!overflowed && curr > 1) {
                            steps += d_steps_table[curr];
                        }
                    }
                } else {
                    if (n_64 < 256) {
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
                    } else {
                        while (curr >= 256) {
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
                        if (!overflowed && curr > 1) {
                            steps += d_steps_table[curr];
                        }
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

                if (USE_DOMAIN_SWITCHING) {
                    uint128 limit(256);
                    if (n < limit) {
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
                    } else {
                        while (curr >= limit) {
                            if (curr.high == 0 && curr.low < 0x100000000ULL && dropped_below_start && has_stopped_sigma) {
                                if (steps + 1050 < init_max_steps) {
                                    curr = one;
                                    break;
                                }
                                uint64_t curr_64 = curr.low;
                                while (curr_64 >= 256) {
                                    uint64_t next_val = 3 * curr_64 + 1;
                                    steps++;
                                    int extra_div = __builtin_ctzll(next_val);
                                    curr_64 = next_val >> extra_div;
                                    steps += extra_div;
                                }
                                curr = uint128(curr_64, 0);
                                break;
                            }

                            // Enter n+1 domain
                            curr = curr + one;
                            int alpha = ctz64(curr.low | (1ULL << 40));
                            uint128 k = shift_right(curr, alpha);
                            if (k > uint128(d_max_safe_k_128[alpha].low, d_max_safe_k_128[alpha].high)) {
                                overflowed = true;
                                break;
                            }
                            uint128 m = k * uint128(d_lut3_128[alpha].low, d_lut3_128[alpha].high);

                            if ((m.high & 0x8000000000000000ULL) != 0) {
                                overflowed = true;
                                break;
                            }

                            uint128 n_new = m - one;
                            int beta = count_trailing_zeros(n_new);

                            if (!dropped_below_start) {
                                bool of_peak = false;
                                uint128 segment_peak = shift_left_1(m, of_peak) - two;
                                if (segment_peak > max_val) {
                                    max_val = segment_peak;
                                }
                            }

                            if (!has_stopped_sigma) {
                                int L_m = 128 - count_leading_zeros(n_new);
                                int L_n = 128 - count_leading_zeros(n);
                                int j = L_m - L_n;
                                if (j < 1) j = 1;

                                if (n <= shift_right(n_new, j)) {
                                    j++;
                                }
                                if (j <= beta) {
                                    stopping_time = t_steps + alpha + j;
                                    has_stopped_sigma = true;
                                }
                            }

                            steps += 2 * alpha + beta;
                            t_steps += alpha + beta;
                            curr = shift_right(n_new, beta);

                            if (curr < n) {
                                dropped_below_start = true;
                            }
                        }
                        if (!overflowed && curr > one) {
                            steps += d_steps_table[curr.low];
                        }
                    }
                } else {
                    if (n < uint128(256)) {
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
                    } else {
                        // Initial polynomial check and possible immediate jump
                        bool init_odd = ((n.low & 1) != 0);
                        if (init_odd && !dropped_below_start) {
                            uint32_t initial_suffix = n.low & 255;
                            poly init_p = d_fpoly_table[initial_suffix];
                            if (init_p.smaller) {
                                dropped_below_start = true;
                                has_stopped_sigma = true;

                                bool overflow = false;
                                uint128 next_val = mul_uint64_check_overflow(shift_right(curr, 8), init_p.mul3, overflow);
                                if (overflow) {
                                    overflowed = true;
                                } else {
                                    next_val = add_check_overflow(next_val, uint128(init_p.add), overflow);
                                    if (overflow) {
                                        overflowed = true;
                                    } else {
                                        int extra_div = count_trailing_zeros(next_val);
                                        curr = shift_right(next_val, extra_div);
                                        steps += init_p.steps + extra_div;
                                    }
                                }
                            }
                        }

                        if (!overflowed) {
                            // Phase 1 Loop: standard Collatz iterations before dropped_below_start is true
                            while (curr >= uint128(256) && !dropped_below_start) {
                                bool overflow = false;
                                uint128 next_val = mul3_add1(curr, overflow);
                                if (overflow) {
                                    overflowed = true;
                                    break;
                                }
                                steps++;

                                if (next_val > max_val) {
                                    max_val = next_val;
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

                            // Phase 2 Loop: fast polynomial jumps after dropped_below_start is true
                            while (curr >= uint128(256) && !overflowed) {
                                if (curr.high == 0 && curr.low < 0x100000000ULL) {
                                    if (steps + 1050 < init_max_steps) {
                                        curr = one;
                                        break;
                                    }
                                    uint64_t curr_64 = curr.low;
                                    while (curr_64 >= 256) {
                                        uint32_t r = curr_64 & 255;
                                        poly p = d_fpoly_table[r];
                                        uint64_t next_val = (curr_64 >> 8) * p.mul3 + p.add;
                                        int extra_div = __builtin_ctzll(next_val);
                                        curr_64 = next_val >> extra_div;
                                        steps += p.steps + extra_div;
                                    }
                                    curr = uint128(curr_64, 0);
                                    break;
                                }

                                uint32_t r = curr.low & 255;
                                poly p = d_fpoly_table[r];
                                
                                bool overflow = false;
                                uint128 next_val = mul_uint64_check_overflow(shift_right(curr, 8), p.mul3, overflow);
                                if (overflow) {
                                    overflowed = true;
                                    break;
                                }
                                next_val = add_check_overflow(next_val, uint128(p.add), overflow);
                                if (overflow) {
                                    overflowed = true;
                                    break;
                                }
                                
                                int extra_div = count_trailing_zeros(next_val);
                                curr = shift_right(next_val, extra_div);
                                steps += p.steps + extra_div;
                            }
                        }
                        if (!overflowed && curr > one) {
                            steps += d_steps_table[curr.low];
                        }
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
    SearchMetrics& metrics,
    bool use_domain_switching
) {
    // Force start to be odd
    if (start.low % 2 == 0) {
        start = start + uint128(1);
    }
    if (start > end) return;

    // Initialize PeakPredictor
    PeakPredictor predictor;
    for (const auto& peak : steps_peaks) {
        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
    }
    predictor.prune_predictions_less_than(start);

    static bool steps_copied = false;
    if (!steps_copied) {
        uint32_t steps_u32[256];
        for (int i = 0; i < 256; ++i) {
            steps_u32[i] = static_cast<uint32_t>(steps8[i]);
        }
        HIP_CHECK(hipMemcpyToSymbol(d_steps_table, steps_u32, 256 * sizeof(uint32_t)));
        HIP_CHECK(hipMemcpyToSymbol(d_fpoly_table, fpoly8, 256 * sizeof(poly)));

        // Initialize dynamic tables for domain switching
        uint64_t host_lut3_64[41];
        uint64_t host_max_safe_k_64[41];
        host_lut3_64[0] = 1ULL;
        host_max_safe_k_64[0] = 0xFFFFFFFFFFFFFFFFULL;
        for (int i = 1; i <= 40; ++i) {
            host_lut3_64[i] = host_lut3_64[i - 1] * 3ULL;
            host_max_safe_k_64[i] = 0xFFFFFFFFFFFFFFFFULL / host_lut3_64[i];
        }
        HIP_CHECK(hipMemcpyToSymbol(d_lut3_64, host_lut3_64, 41 * sizeof(uint64_t)));
        HIP_CHECK(hipMemcpyToSymbol(d_max_safe_k_64, host_max_safe_k_64, 41 * sizeof(uint64_t)));
        HIP_CHECK(hipMemcpyToSymbol(d_lut3_128, lut3, 41 * sizeof(uint128)));
        HIP_CHECK(hipMemcpyToSymbol(d_max_safe_k_128, max_safe_k, 41 * sizeof(uint128)));

        steps_copied = true;
    }

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

    const uint64_t CHUNK_SIZE = 1000000000;
    uint128 current_chunk_start = start;

    double total_kernel_time = 0.0;
    uint64_t total_odds_checked = 0;

    auto last_report_time = std::chrono::steady_clock::now();
    double report_interval = 3600.0;
    const char* env_interval = std::getenv("HAILSTONE_REPORT_INTERVAL");
    if (env_interval) {
        try {
            report_interval = std::stod(env_interval);
        } catch (...) {}
    }

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

            // Confirm predictions up to current_chunk_start
            predictor.process_up_to(current_chunk_start, masterStepsPeaks);
            masterPeaks.current_max_steps = predictor.current_max_steps;

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
                if (use_domain_switching) {
                    hipLaunchKernelGGL((collatz_search_kernel<true, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                        chunk_start_val, chunk_odds,
                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                        d_max_val_peaks, d_max_val_count,
                        d_steps_peaks, d_steps_count,
                        d_sigma_peaks, d_sigma_count,
                        d_global_peaks, d_metrics
                    );
                } else {
                    hipLaunchKernelGGL((collatz_search_kernel<true, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                        chunk_start_val, chunk_odds,
                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                        d_max_val_peaks, d_max_val_count,
                        d_steps_peaks, d_steps_count,
                        d_sigma_peaks, d_sigma_count,
                        d_global_peaks, d_metrics
                    );
                }
            } else {
                if (use_domain_switching) {
                    hipLaunchKernelGGL((collatz_search_kernel<false, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                        chunk_start_val, chunk_odds,
                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                        d_max_val_peaks, d_max_val_count,
                        d_steps_peaks, d_steps_count,
                        d_sigma_peaks, d_sigma_count,
                        d_global_peaks, d_metrics
                    );
                } else {
                    hipLaunchKernelGGL((collatz_search_kernel<false, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                        chunk_start_val, chunk_odds,
                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                        d_max_val_peaks, d_max_val_count,
                        d_steps_peaks, d_steps_count,
                        d_sigma_peaks, d_sigma_count,
                        d_global_peaks, d_metrics
                    );
                }
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
                
                std::sort(chunkSteps.begin(), chunkSteps.end(), [](const PeakRecord& a, const PeakRecord& b) {
                    return a.start_val < b.start_val;
                });

                for (const auto& peak : chunkSteps) {
                    predictor.process_up_to(peak.start_val, masterStepsPeaks);
                    if (peak.metric_val.low > predictor.current_max_steps) {
                        masterStepsPeaks.push_back(peak);
                        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
                    }
                }
            }

            if (sigma_count > 0) {
                int to_copy = std::min(sigma_count, (int)MAX_PEAK_RECORDS);
                std::vector<PeakRecord> chunkSigma(to_copy);
                HIP_CHECK(hipMemcpy(chunkSigma.data(), d_sigma_peaks, to_copy * sizeof(PeakRecord), hipMemcpyDeviceToHost));
                masterSigmaPeaks.insert(masterSigmaPeaks.end(), chunkSigma.begin(), chunkSigma.end());
            }

            predictor.process_up_to(current_chunk_end, masterStepsPeaks);
            masterPeaks.current_max_steps = predictor.current_max_steps;

            // Update masterPeaks thresholds
            if (chunkPeaks.current_max_value > masterPeaks.current_max_value) {
                masterPeaks.current_max_value = chunkPeaks.current_max_value;
            }
            if (chunkPeaks.current_max_sigma > masterPeaks.current_max_sigma) {
                masterPeaks.current_max_sigma = chunkPeaks.current_max_sigma;
            }
        }

        current_chunk_start = current_chunk_start + uint128(CHUNK_SIZE);

        // Time-based progress update
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time).count() >= report_interval) {
            uint64_t current_block = (current_chunk_start.high << 32) | (current_chunk_start.low >> 32);
            uint64_t start_block = (start.high << 32) | (start.low >> 32);
            uint64_t blocks_searched = current_block - start_block;
            std::cout << "[Progress Update] Blocks searched: " << blocks_searched
                      << ", Current block: " << current_block << std::endl;
            last_report_time = now;
        }
    }

    // Confirm any remaining predictions up to end
    predictor.process_up_to(end, masterStepsPeaks);
    masterPeaks.current_max_steps = predictor.current_max_steps;

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
    SearchMetrics& metrics,
    bool use_domain_switching
) {
    if (end_num >= uint128(0x100000000ULL)) {
        throw std::invalid_argument("hip_search_block_0: range extends beyond block 0");
    }
    hip_search_range(start_num, end_num, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics, use_domain_switching);
}

void hip_search_blocks_gt_0(
    uint128 start_num,
    uint128 end_num,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics,
    bool use_domain_switching
) {
    if (start_num < uint128(0x100000000ULL)) {
        throw std::invalid_argument("hip_search_blocks_gt_0: range starts below block 1");
    }
    hip_search_range(start_num, end_num, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics, use_domain_switching);
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


template <bool USE_64BIT, bool CHECK_START, bool CHECK_END, bool USE_DOMAIN_SWITCHING>
__global__ void collatz_search_kernel_suffix_first(
    uint128 start_prefix,
    uint128 start_val,
    uint128 end_val,
    uint128 init_max_val,
    uint32_t init_max_steps,
    uint32_t init_max_sigma,
    uint32_t allowed_suffixes_size,
    const uint32_t* d_allowed_suffixes,
    int width,
    uint64_t total_work_items,
    uint64_t prefix_stride,
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

    int l_id = threadIdx.x;
    int g_id = blockIdx.x * blockDim.x + threadIdx.x;

    bool active_thread = (g_id < total_work_items);

    uint128 n(0, 0);
    uint64_t n_64 = 0ULL;
    uint64_t start_64 = start_val.low;
    uint64_t end_64 = end_val.low;

    uint64_t prefix_index = g_id / allowed_suffixes_size;
    uint32_t suffix_index = g_id % allowed_suffixes_size;
    
    if (active_thread) {
        uint32_t suffix = d_allowed_suffixes[suffix_index];
        if (USE_64BIT) {
            uint64_t prefix = start_prefix.low + prefix_index * prefix_stride;
            uint64_t curr = (prefix << width) | suffix;
            n_64 = curr;
            
            if (CHECK_START && curr < start_64) {
                active_thread = false;
            } else if (CHECK_END && curr > end_64) {
                active_thread = false;
            }
        } else {
            uint128 prefix = start_prefix + uint128(prefix_index * prefix_stride);
            uint128 curr = shift_left(prefix, width) + uint128(suffix);
            n = curr;
            
            if (CHECK_START && curr < start_val) {
                active_thread = false;
            } else if (CHECK_END && curr > end_val) {
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

                if (USE_DOMAIN_SWITCHING) {
                    if (n_64 < 256) {
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
                    } else {
                        while (curr >= 256) {
                            curr = curr + 1;
                            int alpha = __builtin_ctzll(curr | (1ULL << 40));
                            uint64_t k = curr >> alpha;
                            if (k > d_max_safe_k_64[alpha]) {
                                overflowed = true;
                                break;
                            }
                            uint64_t m = k * d_lut3_64[alpha];
                            
                            if ((m & 0x8000000000000000ULL) != 0) {
                                overflowed = true;
                                break;
                            }

                            uint64_t n_new = m - 1;
                            int beta = __builtin_ctzll(n_new);

                            if (!dropped_below_start) {
                                uint64_t segment_peak = (m << 1) - 2;
                                if (segment_peak > max_val_64) {
                                    max_val_64 = segment_peak;
                                }
                            }

                            if (!has_stopped_sigma) {
                                int L_m = 64 - __builtin_clzll(n_new);
                                int L_n = 64 - __builtin_clzll(n_64);
                                int j = L_m - L_n;
                                if (j < 1) j = 1;

                                if (n_64 <= (n_new >> j)) {
                                    j++;
                                }
                                if (j <= beta) {
                                    stopping_time = t_steps + alpha + j;
                                    has_stopped_sigma = true;
                                }
                            }

                            steps += 2 * alpha + beta;
                            t_steps += alpha + beta;
                            curr = n_new >> beta;

                            if (curr < n_64) {
                                dropped_below_start = true;
                            }
                        }
                        if (!overflowed && curr > 1) {
                            steps += d_steps_table[curr];
                        }
                    }
                } else {
                    if (n_64 < 256) {
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
                    } else {
                        while (curr >= 256) {
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
                        if (!overflowed && curr > 1) {
                            steps += d_steps_table[curr];
                        }
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

                if (USE_DOMAIN_SWITCHING) {
                    uint128 limit(256);
                    if (n < limit) {
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
                    } else {
                        while (curr >= limit) {
                            if (curr.high == 0 && curr.low < 0x100000000ULL && dropped_below_start && has_stopped_sigma) {
                                if (steps + 1050 < init_max_steps) {
                                    curr = one;
                                    break;
                                }
                                uint64_t curr_64 = curr.low;
                                while (curr_64 >= 256) {
                                    uint64_t next_val = 3 * curr_64 + 1;
                                    steps++;
                                    int extra_div = __builtin_ctzll(next_val);
                                    curr_64 = next_val >> extra_div;
                                    steps += extra_div;
                                }
                                curr = uint128(curr_64, 0);
                                break;
                            }

                            // Enter n+1 domain
                            curr = curr + one;
                            int alpha = ctz64(curr.low | (1ULL << 40));
                            uint128 k = shift_right(curr, alpha);
                            if (k > uint128(d_max_safe_k_128[alpha].low, d_max_safe_k_128[alpha].high)) {
                                overflowed = true;
                                break;
                            }
                            uint128 m = k * uint128(d_lut3_128[alpha].low, d_lut3_128[alpha].high);

                            if ((m.high & 0x8000000000000000ULL) != 0) {
                                overflowed = true;
                                break;
                            }

                            uint128 n_new = m - one;
                            int beta = count_trailing_zeros(n_new);

                            if (!dropped_below_start) {
                                bool of_peak = false;
                                uint128 segment_peak = shift_left_1(m, of_peak) - two;
                                if (segment_peak > max_val) {
                                    max_val = segment_peak;
                                }
                            }

                            if (!has_stopped_sigma) {
                                int L_m = 128 - count_leading_zeros(n_new);
                                int L_n = 128 - count_leading_zeros(n);
                                int j = L_m - L_n;
                                if (j < 1) j = 1;

                                if (n <= shift_right(n_new, j)) {
                                    j++;
                                }
                                if (j <= beta) {
                                    stopping_time = t_steps + alpha + j;
                                    has_stopped_sigma = true;
                                }
                            }

                            steps += 2 * alpha + beta;
                            t_steps += alpha + beta;
                            curr = shift_right(n_new, beta);

                            if (curr < n) {
                                dropped_below_start = true;
                            }
                        }
                        if (!overflowed && curr > one) {
                            steps += d_steps_table[curr.low];
                        }
                    }
                } else {
                    if (n < uint128(256)) {
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
                    } else {
                        // Initial polynomial check and possible immediate jump
                        bool init_odd = ((n.low & 1) != 0);
                        if (init_odd && !dropped_below_start) {
                            uint32_t initial_suffix = n.low & 255;
                            poly init_p = d_fpoly_table[initial_suffix];
                            if (init_p.smaller) {
                                dropped_below_start = true;
                                has_stopped_sigma = true;

                                bool overflow = false;
                                uint128 next_val = mul_uint64_check_overflow(shift_right(curr, 8), init_p.mul3, overflow);
                                if (overflow) {
                                    overflowed = true;
                                } else {
                                    next_val = add_check_overflow(next_val, uint128(init_p.add), overflow);
                                    if (overflow) {
                                        overflowed = true;
                                    } else {
                                        int extra_div = count_trailing_zeros(next_val);
                                        curr = shift_right(next_val, extra_div);
                                        steps += init_p.steps + extra_div;
                                    }
                                }
                            }
                        }

                        if (!overflowed) {
                            // Phase 1 Loop: standard Collatz iterations before dropped_below_start is true
                            while (curr >= uint128(256) && !dropped_below_start) {
                                bool overflow = false;
                                uint128 next_val = mul3_add1(curr, overflow);
                                if (overflow) {
                                    overflowed = true;
                                    break;
                                }
                                steps++;

                                if (next_val > max_val) {
                                    max_val = next_val;
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

                            // Phase 2 Loop: fast polynomial jumps after dropped_below_start is true
                            while (curr >= uint128(256) && !overflowed) {
                                if (curr.high == 0 && curr.low < 0x100000000ULL) {
                                    if (steps + 1050 < init_max_steps) {
                                        curr = one;
                                        break;
                                    }
                                    uint64_t curr_64 = curr.low;
                                    while (curr_64 >= 256) {
                                        uint32_t r = curr_64 & 255;
                                        poly p = d_fpoly_table[r];
                                        uint64_t next_val = (curr_64 >> 8) * p.mul3 + p.add;
                                        int extra_div = __builtin_ctzll(next_val);
                                        curr_64 = next_val >> extra_div;
                                        steps += p.steps + extra_div;
                                    }
                                    curr = uint128(curr_64, 0);
                                    break;
                                }

                                uint32_t r = curr.low & 255;
                                poly p = d_fpoly_table[r];
                                
                                bool overflow = false;
                                uint128 next_val = mul_uint64_check_overflow(shift_right(curr, 8), p.mul3, overflow);
                                if (overflow) {
                                    overflowed = true;
                                    break;
                                }
                                next_val = add_check_overflow(next_val, uint128(p.add), overflow);
                                if (overflow) {
                                    overflowed = true;
                                    break;
                                }
                                
                                int extra_div = count_trailing_zeros(next_val);
                                curr = shift_right(next_val, extra_div);
                                steps += p.steps + extra_div;
                            }
                        }
                        if (!overflowed && curr > one) {
                            steps += d_steps_table[curr.low];
                        }
                    }
                }
            }
        }
    }

    if (active_thread) {
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

    shared_max_val_low[l_id] = active_thread ? max_val.low : 0ULL;
    shared_max_val_high[l_id] = active_thread ? max_val.high : 0ULL;
    shared_steps[l_id] = active_thread ? steps : 0;
    shared_sigma[l_id] = active_thread ? stopping_time : 0;
    __syncthreads();

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

void accumulate_boundary_metrics(uint64_t prefix, uint64_t start_64, uint64_t end_64, int width, 
                                 const BaseDependentSuffixes& base_suffixes, SearchMetrics& metrics) {
    uint64_t base = prefix << width;
    uint64_t base_mod9 = base % 9;
    
    for (uint32_t suffix : base_suffixes.std_allowed) {
        uint64_t curr = base | suffix;
        if (curr < start_64) continue;
        if (curr > end_64) break;
        
        metrics.total_numbers_checked++;
        uint32_t rem = (base_mod9 + suffix) % 9;
        if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
            metrics.numbers_skipped_mod6++;
        }
    }
}

void accumulate_boundary_metrics_128(uint128 prefix, uint128 start, uint128 end, int width, 
                                     const BaseDependentSuffixes& base_suffixes, SearchMetrics& metrics) {
    uint128 base = shift_left(prefix, width);
    uint64_t base_mod9 = ((base.high % 9) * 7 + (base.low % 9)) % 9;
    
    for (uint32_t suffix : base_suffixes.std_allowed) {
        uint128 curr = base + uint128(suffix);
        if (curr < start) continue;
        if (curr > end) break;
        
        metrics.total_numbers_checked++;
        uint32_t rem = (base_mod9 + suffix) % 9;
        if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
            metrics.numbers_skipped_mod6++;
        }
    }
}

void hip_search_range_suffix_first(
    uint128 start,
    uint128 end,
    int width,
    const BaseDependentSuffixes& base_suffixes,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics,
    bool use_domain_switching
) {
    if (start > end) return;

    // Initialize PeakPredictor
    PeakPredictor predictor;
    for (const auto& peak : steps_peaks) {
        predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
    }
    predictor.prune_predictions_less_than(start);

    static bool steps_copied = false;
    if (!steps_copied) {
        uint32_t steps_u32[256];
        for (int i = 0; i < 256; ++i) {
            steps_u32[i] = static_cast<uint32_t>(steps8[i]);
        }
        HIP_CHECK(hipMemcpyToSymbol(d_steps_table, steps_u32, 256 * sizeof(uint32_t)));
        HIP_CHECK(hipMemcpyToSymbol(d_fpoly_table, fpoly8, 256 * sizeof(poly)));

        // Initialize dynamic tables for domain switching
        uint64_t host_lut3_64[41];
        uint64_t host_max_safe_k_64[41];
        host_lut3_64[0] = 1ULL;
        host_max_safe_k_64[0] = 0xFFFFFFFFFFFFFFFFULL;
        for (int i = 1; i <= 40; ++i) {
            host_lut3_64[i] = host_lut3_64[i - 1] * 3ULL;
            host_max_safe_k_64[i] = 0xFFFFFFFFFFFFFFFFULL / host_lut3_64[i];
        }
        HIP_CHECK(hipMemcpyToSymbol(d_lut3_64, host_lut3_64, 41 * sizeof(uint64_t)));
        HIP_CHECK(hipMemcpyToSymbol(d_max_safe_k_64, host_max_safe_k_64, 41 * sizeof(uint64_t)));
        HIP_CHECK(hipMemcpyToSymbol(d_lut3_128, lut3, 41 * sizeof(uint128)));
        HIP_CHECK(hipMemcpyToSymbol(d_max_safe_k_128, max_safe_k, 41 * sizeof(uint128)));

        steps_copied = true;
    }

    uint32_t* d_allowed_tables[9] = {nullptr};
    for (int B = 0; B < 9; ++B) {
        if (!base_suffixes.allowed_tables[B].empty()) {
            HIP_CHECK(hipMalloc(&d_allowed_tables[B], base_suffixes.allowed_tables[B].size() * sizeof(uint32_t)));
            HIP_CHECK(hipMemcpy(d_allowed_tables[B], base_suffixes.allowed_tables[B].data(), base_suffixes.allowed_tables[B].size() * sizeof(uint32_t), hipMemcpyHostToDevice));
        }
    }

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

    SearchMetrics masterMetrics = {0};
    PeakState masterPeaks = global_peaks;
    std::vector<PeakRecord> masterMaxValPeaks = max_value_peaks;
    std::vector<PeakRecord> masterStepsPeaks = steps_peaks;
    std::vector<PeakRecord> masterSigmaPeaks = sigma_peaks;

    const uint64_t CHUNK_SIZE = 1000000000;
    uint128 current_chunk_start = start;

    double total_kernel_time = 0.0;
    uint64_t total_numbers_processed = 0;
    uint32_t std_allowed_size = (uint32_t)base_suffixes.std_allowed.size();

    auto last_report_time = std::chrono::steady_clock::now();
    double report_interval = 3600.0;
    const char* env_interval = std::getenv("HAILSTONE_REPORT_INTERVAL");
    if (env_interval) {
        try {
            report_interval = std::stod(env_interval);
        } catch (...) {}
    }

    while (current_chunk_start <= end) {
        uint128 current_chunk_end = current_chunk_start + uint128(CHUNK_SIZE - 1);
        if (current_chunk_end > end) {
            current_chunk_end = end;
        }

        uint128 start_prefix = shift_right(current_chunk_start, width);
        uint128 end_prefix = shift_right(current_chunk_end, width);
        
        bool use_64bit = (end < uint128(0x100000000ULL));

        // Confirm predictions up to current_chunk_start
        predictor.process_up_to(current_chunk_start, masterStepsPeaks);
        masterPeaks.current_max_steps = predictor.current_max_steps;

        int zero = 0;
        SearchMetrics initial_chunk_metrics = {0};

        HIP_CHECK(hipMemcpy(d_max_val_count, &zero, sizeof(int), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(d_steps_count, &zero, sizeof(int), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(d_sigma_count, &zero, sizeof(int), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(d_global_peaks, &masterPeaks, sizeof(PeakState), hipMemcpyHostToDevice));
        HIP_CHECK(hipMemcpy(d_metrics, &initial_chunk_metrics, sizeof(SearchMetrics), hipMemcpyHostToDevice));

        auto t_start = std::chrono::high_resolution_clock::now();

        if (start_prefix == end_prefix) {
            // Case 1: Start and end in the same prefix (single boundary)
            uint128 base = shift_left(start_prefix, width);
            uint64_t base_mod9 = ((base.high % 9) * 7 + (base.low % 9)) % 9;
            uint32_t* d_allowed = d_allowed_tables[base_mod9];
            uint32_t allowed_size = base_suffixes.allowed_tables[base_mod9].size();
            if (allowed_size > 0) {
                uint64_t total_work_items = allowed_size;
                int threads_per_block = 256;
                int blocks = (total_work_items + threads_per_block - 1) / threads_per_block;

                if (use_64bit) {
                    if (use_domain_switching) {
                        hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, true, true, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                            start_prefix, current_chunk_start, current_chunk_end,
                            masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                            allowed_size, d_allowed, width, total_work_items, 1,
                            d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                        );
                    } else {
                        hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, true, true, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                            start_prefix, current_chunk_start, current_chunk_end,
                            masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                            allowed_size, d_allowed, width, total_work_items, 1,
                            d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                        );
                    }
                } else {
                    if (use_domain_switching) {
                        hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, true, true, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                            start_prefix, current_chunk_start, current_chunk_end,
                            masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                            allowed_size, d_allowed, width, total_work_items, 1,
                            d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                        );
                    } else {
                        hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, true, true, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                            start_prefix, current_chunk_start, current_chunk_end,
                            masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                            allowed_size, d_allowed, width, total_work_items, 1,
                            d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                        );
                    }
                }
            }
            if (use_64bit) {
                accumulate_boundary_metrics(start_prefix.low, current_chunk_start.low, current_chunk_end.low, width, base_suffixes, masterMetrics);
            } else {
                accumulate_boundary_metrics_128(start_prefix, current_chunk_start, current_chunk_end, width, base_suffixes, masterMetrics);
            }
        } else {
            // Case 2: Start and end prefixes are different
            bool start_is_boundary = (current_chunk_start > shift_left(start_prefix, width));
            if (start_is_boundary) {
                uint128 base = shift_left(start_prefix, width);
                uint64_t base_mod9 = ((base.high % 9) * 7 + (base.low % 9)) % 9;
                uint32_t* d_allowed = d_allowed_tables[base_mod9];
                uint32_t allowed_size = base_suffixes.allowed_tables[base_mod9].size();
                if (allowed_size > 0) {
                    uint64_t total_work_items = allowed_size;
                    int threads_per_block = 256;
                    int blocks = (total_work_items + threads_per_block - 1) / threads_per_block;

                    if (use_64bit) {
                        if (use_domain_switching) {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, true, false, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                start_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        } else {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, true, false, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                start_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        }
                    } else {
                        if (use_domain_switching) {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, true, false, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                start_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        } else {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, true, false, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                start_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        }
                    }
                }
                if (use_64bit) {
                    accumulate_boundary_metrics(start_prefix.low, current_chunk_start.low, ((start_prefix.low + 1) << width) - 1, width, base_suffixes, masterMetrics);
                } else {
                    accumulate_boundary_metrics_128(start_prefix, current_chunk_start, shift_left(start_prefix + uint128(1), width) - uint128(1), width, base_suffixes, masterMetrics);
                }
            }

            bool end_is_boundary = (current_chunk_end < (shift_left(end_prefix + uint128(1), width) - uint128(1)));
            if (end_is_boundary) {
                uint128 base = shift_left(end_prefix, width);
                uint64_t base_mod9 = ((base.high % 9) * 7 + (base.low % 9)) % 9;
                uint32_t* d_allowed = d_allowed_tables[base_mod9];
                uint32_t allowed_size = base_suffixes.allowed_tables[base_mod9].size();
                if (allowed_size > 0) {
                    uint64_t total_work_items = allowed_size;
                    int threads_per_block = 256;
                    int blocks = (total_work_items + threads_per_block - 1) / threads_per_block;

                    if (use_64bit) {
                        if (use_domain_switching) {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, false, true, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                end_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        } else {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, false, true, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                end_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        }
                    } else {
                        if (use_domain_switching) {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, false, true, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                end_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        } else {
                            hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, false, true, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                end_prefix, current_chunk_start, current_chunk_end,
                                masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                allowed_size, d_allowed, width, total_work_items, 1,
                                d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                            );
                        }
                    }
                }
                if (use_64bit) {
                    accumulate_boundary_metrics(end_prefix.low, end_prefix.low << width, current_chunk_end.low, width, base_suffixes, masterMetrics);
                } else {
                    accumulate_boundary_metrics_128(end_prefix, shift_left(end_prefix, width), current_chunk_end, width, base_suffixes, masterMetrics);
                }
            }

            // Intermediate prefixes mod 9 groups
            uint128 mid_start_prefix = start_prefix + (start_is_boundary ? uint128(1) : uint128(0));
            uint128 mid_end_prefix = end_prefix - (end_is_boundary ? uint128(1) : uint128(0));

            if (mid_start_prefix <= mid_end_prefix) {
                uint64_t mult = (1ULL << width) % 9;
                for (int rem_mod9 = 0; rem_mod9 < 9; ++rem_mod9) {
                    uint128 first_prefix = mid_start_prefix;
                    while (first_prefix <= mid_end_prefix) {
                        uint64_t m9 = ((first_prefix.high % 9) * 7 + first_prefix.low % 9) % 9;
                        if (m9 == rem_mod9) break;
                        first_prefix = first_prefix + uint128(1);
                    }

                    uint128 last_prefix = mid_end_prefix;
                    while (last_prefix >= first_prefix) {
                        uint64_t m9 = ((last_prefix.high % 9) * 7 + last_prefix.low % 9) % 9;
                        if (m9 == rem_mod9) break;
                        last_prefix = last_prefix - uint128(1);
                    }

                    if (first_prefix <= last_prefix) {
                        uint64_t diff = (last_prefix - first_prefix).low;
                        uint64_t num_prefixes_group = diff / 9 + 1;

                        uint64_t base_mod9 = (rem_mod9 * mult) % 9;
                        uint32_t* d_allowed = d_allowed_tables[base_mod9];
                        uint32_t allowed_size = base_suffixes.allowed_tables[base_mod9].size();
                        if (allowed_size > 0 && num_prefixes_group > 0) {
                            uint64_t total_work_items = num_prefixes_group * allowed_size;
                            int threads_per_block = 256;
                            int blocks = (total_work_items + threads_per_block - 1) / threads_per_block;

                            if (use_64bit) {
                                if (use_domain_switching) {
                                    hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, false, false, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                        first_prefix, current_chunk_start, current_chunk_end,
                                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                        allowed_size, d_allowed, width, total_work_items, 9,
                                        d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                                    );
                                } else {
                                    hipLaunchKernelGGL((collatz_search_kernel_suffix_first<true, false, false, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                        first_prefix, current_chunk_start, current_chunk_end,
                                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                        allowed_size, d_allowed, width, total_work_items, 9,
                                        d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                                    );
                                }
                            } else {
                                if (use_domain_switching) {
                                    hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, false, false, true>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                        first_prefix, current_chunk_start, current_chunk_end,
                                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                        allowed_size, d_allowed, width, total_work_items, 9,
                                        d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                                    );
                                } else {
                                    hipLaunchKernelGGL((collatz_search_kernel_suffix_first<false, false, false, false>), dim3(blocks), dim3(threads_per_block), 0, 0,
                                        first_prefix, current_chunk_start, current_chunk_end,
                                        masterPeaks.current_max_value, masterPeaks.current_max_steps, masterPeaks.current_max_sigma,
                                        allowed_size, d_allowed, width, total_work_items, 9,
                                        d_max_val_peaks, d_max_val_count, d_steps_peaks, d_steps_count, d_sigma_peaks, d_sigma_count, d_global_peaks, d_metrics
                                    );
                                }
                            }
                        }
                        
                        // Accumulate host metrics
                        uint32_t std_skipped = base_suffixes.std_skipped[base_mod9];
                        masterMetrics.total_numbers_checked += num_prefixes_group * std_allowed_size;
                        masterMetrics.numbers_skipped_mod6 += num_prefixes_group * std_skipped;
                    }
                }
            }
        }

        HIP_CHECK(hipDeviceSynchronize());

        auto t_end = std::chrono::high_resolution_clock::now();
        total_kernel_time += std::chrono::duration<double>(t_end - t_start).count();

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

        masterMetrics.total_steps_computed += chunkMetrics.total_steps_computed;
        masterMetrics.numbers_overflowed += chunkMetrics.numbers_overflowed;

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
            
            std::sort(chunkSteps.begin(), chunkSteps.end(), [](const PeakRecord& a, const PeakRecord& b) {
                return a.start_val < b.start_val;
            });

            for (const auto& peak : chunkSteps) {
                predictor.process_up_to(peak.start_val, masterStepsPeaks);
                if (peak.metric_val.low > predictor.current_max_steps) {
                    masterStepsPeaks.push_back(peak);
                    predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
                }
            }
        }

        if (sigma_count > 0) {
            int to_copy = std::min(sigma_count, (int)MAX_PEAK_RECORDS);
            std::vector<PeakRecord> chunkSigma(to_copy);
            HIP_CHECK(hipMemcpy(chunkSigma.data(), d_sigma_peaks, to_copy * sizeof(PeakRecord), hipMemcpyDeviceToHost));
            masterSigmaPeaks.insert(masterSigmaPeaks.end(), chunkSigma.begin(), chunkSigma.end());
        }

        predictor.process_up_to(current_chunk_end, masterStepsPeaks);
        masterPeaks.current_max_steps = predictor.current_max_steps;

        if (chunkPeaks.current_max_value > masterPeaks.current_max_value) {
            masterPeaks.current_max_value = chunkPeaks.current_max_value;
        }
        if (chunkPeaks.current_max_sigma > masterPeaks.current_max_sigma) {
            masterPeaks.current_max_sigma = chunkPeaks.current_max_sigma;
        }

        total_numbers_processed += (current_chunk_end - current_chunk_start + uint128(1)).low;
        current_chunk_start = current_chunk_start + uint128(CHUNK_SIZE);

        // Time-based progress update
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time).count() >= report_interval) {
            uint64_t current_block = (current_chunk_start.high << 32) | (current_chunk_start.low >> 32);
            uint64_t start_block = (start.high << 32) | (start.low >> 32);
            uint64_t blocks_searched = current_block - start_block;
            std::cout << "[Progress Update] Blocks searched: " << blocks_searched
                      << ", Current block: " << current_block << std::endl;
            last_report_time = now;
        }
    }

    // Confirm any remaining predictions up to end
    predictor.process_up_to(end, masterStepsPeaks);
    masterPeaks.current_max_steps = predictor.current_max_steps;

    filter_peaks(masterMaxValPeaks, masterMaxValPeaks.size());
    filter_peaks(masterStepsPeaks, masterStepsPeaks.size());
    filter_peaks(masterSigmaPeaks, masterSigmaPeaks.size());

    max_value_peaks = std::move(masterMaxValPeaks);
    steps_peaks = std::move(masterStepsPeaks);
    sigma_peaks = std::move(masterSigmaPeaks);
    global_peaks = masterPeaks;
    
    metrics.total_numbers_checked += masterMetrics.total_numbers_checked;
    metrics.total_steps_computed += masterMetrics.total_steps_computed;
    metrics.numbers_skipped_even += total_numbers_processed / 2; // Approximate even numbers skipped
    metrics.numbers_skipped_mod6 += masterMetrics.numbers_skipped_mod6;
    metrics.numbers_overflowed += masterMetrics.numbers_overflowed;
    metrics.elapsed_seconds += total_kernel_time;

    for (int B = 0; B < 9; ++B) {
        if (d_allowed_tables[B]) HIP_CHECK(hipFree(d_allowed_tables[B]));
    }
    HIP_CHECK(hipFree(d_max_val_peaks));
    HIP_CHECK(hipFree(d_steps_peaks));
    HIP_CHECK(hipFree(d_sigma_peaks));
    HIP_CHECK(hipFree(d_max_val_count));
    HIP_CHECK(hipFree(d_steps_count));
    HIP_CHECK(hipFree(d_sigma_count));
    HIP_CHECK(hipFree(d_global_peaks));
    HIP_CHECK(hipFree(d_metrics));
}

void hip_search_block_0_suffix_first(
    uint128 start_num,
    uint128 end_num,
    int width,
    const BaseDependentSuffixes& base_suffixes,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics,
    bool use_domain_switching
) {
    if (end_num >= uint128(0x100000000ULL)) {
        throw std::invalid_argument("hip_search_block_0_suffix_first: range extends beyond block 0");
    }
    hip_search_range_suffix_first(start_num, end_num, width, base_suffixes, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics, use_domain_switching);
}

