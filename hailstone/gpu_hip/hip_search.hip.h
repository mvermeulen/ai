#ifndef HAILSTONE_HIP_SEARCH_H
#define HAILSTONE_HIP_SEARCH_H

#include "common.h"

void hip_search_range(
    uint128 start,
    uint128 end,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
);

void hip_search_block_0(
    uint128 start_num,
    uint128 end_num,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
);

void hip_search_blocks_gt_0(
    uint128 start_num,
    uint128 end_num,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
);

#endif // HAILSTONE_HIP_SEARCH_H
