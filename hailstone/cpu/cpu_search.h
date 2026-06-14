#ifndef HAILSTONE_CPU_SEARCH_H
#define HAILSTONE_CPU_SEARCH_H

#include "common.h"

// Computes Collatz statistics for a single starting value.
CollatzStats compute_collatz(uint128 n);

// Computes Collatz statistics using polynomial precomputed tables once value is below 2^N.
CollatzStats compute_collatz_poly(uint128 n, uint32_t current_max_steps);

// Searches a range of odd numbers [start, end] for peaks.
// Appends newly found peaks to the respective vectors and updates the global_peaks state.
void cpu_search_range(uint128 start, uint128 end, 
                      std::vector<PeakRecord>& max_value_peaks,
                      std::vector<PeakRecord>& steps_peaks,
                      std::vector<PeakRecord>& sigma_peaks,
                      PeakState& global_peaks,
                      SearchMetrics& metrics);

void cpu_search_block_0(uint128 start_num, uint128 end_num,
                        std::vector<PeakRecord>& max_value_peaks,
                        std::vector<PeakRecord>& steps_peaks,
                        std::vector<PeakRecord>& sigma_peaks,
                        PeakState& global_peaks,
                        SearchMetrics& metrics);

void cpu_search_blocks_gt_0(uint128 start_num, uint128 end_num,
                            std::vector<PeakRecord>& max_value_peaks,
                            std::vector<PeakRecord>& steps_peaks,
                            std::vector<PeakRecord>& sigma_peaks,
                            PeakState& global_peaks,
                            SearchMetrics& metrics);

void cpu_search_blocks_gt_0_suffix_first(uint128 start_num, uint128 end_num,
                                         int width,
                                         const BaseDependentSuffixes& base_suffixes,
                                         std::vector<PeakRecord>& max_value_peaks,
                                         std::vector<PeakRecord>& steps_peaks,
                                         std::vector<PeakRecord>& sigma_peaks,
                                         PeakState& global_peaks,
                                         SearchMetrics& metrics,
                                         bool avx512_enabled);

// Generates the list of allowed non-redundant odd suffixes for a given width
std::vector<uint32_t> generate_allowed_suffixes(int width);

// Generates base-dependent allowed suffixes lists
BaseDependentSuffixes generate_base_dependent_suffixes(int width);

// Suffix-first search for blocks > 0 (128-bit)
void cpu_search_range_suffix_first(uint128 start, uint128 end, 
                                   int width,
                                   const BaseDependentSuffixes& base_suffixes,
                                   std::vector<PeakRecord>& max_value_peaks,
                                   std::vector<PeakRecord>& steps_peaks,
                                   std::vector<PeakRecord>& sigma_peaks,
                                   PeakState& global_peaks,
                                   SearchMetrics& metrics);

// Suffix-first search for block 0 (64-bit)
void cpu_search_block_0_suffix_first(uint128 start_num, uint128 end_num,
                                     int width,
                                     const BaseDependentSuffixes& base_suffixes,
                                     std::vector<PeakRecord>& max_value_peaks,
                                     std::vector<PeakRecord>& steps_peaks,
                                     std::vector<PeakRecord>& sigma_peaks,
                                     PeakState& global_peaks,
                                     SearchMetrics& metrics);

// Runtime capability checks and AVX-512 search function
bool is_avx512_supported();

#ifdef HAS_AVX512_COMPILER_SUPPORT
void cpu_search_range_suffix_first_avx512(uint128 start, uint128 end, 
                                          int width,
                                          const BaseDependentSuffixes& base_suffixes,
                                          std::vector<PeakRecord>& max_value_peaks,
                                          std::vector<PeakRecord>& steps_peaks,
                                          std::vector<PeakRecord>& sigma_peaks,
                                          PeakState& global_peaks,
                                          SearchMetrics& metrics);
#endif

#endif // HAILSTONE_CPU_SEARCH_H

