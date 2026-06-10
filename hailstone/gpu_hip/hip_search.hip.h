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

#endif // HAILSTONE_HIP_SEARCH_H
