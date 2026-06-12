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

std::vector<uint32_t> generate_allowed_suffixes(int width);

void hip_search_range_suffix_first(
    uint128 start,
    uint128 end,
    int width,
    const std::vector<uint32_t>& allowed_suffixes,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
);

void hip_search_block_0_suffix_first(
    uint128 start_num,
    uint128 end_num,
    int width,
    const std::vector<uint32_t>& allowed_suffixes,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics
);

#endif // HAILSTONE_HIP_SEARCH_H

