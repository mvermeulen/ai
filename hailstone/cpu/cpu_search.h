#ifndef HAILSTONE_CPU_SEARCH_H
#define HAILSTONE_CPU_SEARCH_H

#include "common.h"

// Computes Collatz statistics for a single starting value.
CollatzStats compute_collatz(uint128 n);

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

#endif // HAILSTONE_CPU_SEARCH_H
