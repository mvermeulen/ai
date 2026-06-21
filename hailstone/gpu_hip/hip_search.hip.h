#ifndef HAILSTONE_HIP_SEARCH_H
#define HAILSTONE_HIP_SEARCH_H

#include "common.h"
#include <string>

void hip_search_range(
    uint128 start,
    uint128 end,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics,
    bool use_domain_switching = false
);

void hip_search_block_0(
    uint128 start_num,
    uint128 end_num,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics,
    bool use_domain_switching = false
);

void hip_search_blocks_gt_0(
    uint128 start_num,
    uint128 end_num,
    std::vector<PeakRecord>& max_value_peaks,
    std::vector<PeakRecord>& steps_peaks,
    std::vector<PeakRecord>& sigma_peaks,
    PeakState& global_peaks,
    SearchMetrics& metrics,
    bool use_domain_switching = false
);

BaseDependentSuffixes generate_base_dependent_suffixes(int width);
bool load_allowed_suffixes_binary(const std::string& filepath, BaseDependentSuffixes& suffixes);
BaseDependentSuffixes load_allowed_suffixes_24();

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
    bool use_domain_switching = false
);

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
    bool use_domain_switching = false
);

#endif // HAILSTONE_HIP_SEARCH_H

