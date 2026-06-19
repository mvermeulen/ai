#ifndef HAILSTONE_COMMON_H
#define HAILSTONE_COMMON_H

#include "uint128.h"
#include <vector>

#ifdef EXCLUDE_01_SUFFIX
    #ifndef TRACK_ALMOST_PEAKS
        #error "EXCLUDE_01_SUFFIX requires TRACK_ALMOST_PEAKS to be enabled!"
    #endif
#endif

// Statistics for a single starting value
struct CollatzStats {
    uint128 start_val;
    uint32_t steps;         // total steps in H-trajectory (odd -> 3n+1, even -> n/2)
    uint32_t stopping_time; // stopping time \sigma in T-trajectory (least k where T^k(n) < n)
    uint128 max_value;      // maximum value reached in H-trajectory
    bool overflow;          // true if calculation overflowed 128 bits
};

// Represents a peak found for a specific statistic
struct PeakRecord {
    uint128 start_val;
    uint128 metric_val;     // value of the metric (steps, max_value, or stopping_time)
};

// State containing the current largest values to check for peaks
struct PeakState {
    uint128 current_max_value; // highest max_value seen so far
    uint32_t current_max_steps; // highest steps seen so far
    uint32_t current_max_sigma; // highest stopping_time \sigma seen so far
    
    std::vector<PeakRecord> almost_steps_peaks;

    HD_ATTR PeakState() 
        : current_max_value(0), current_max_steps(0), current_max_sigma(0) {}
};

// Metrics collected during a search range
struct SearchMetrics {
    uint64_t total_numbers_checked;
    uint64_t total_steps_computed;
    uint64_t numbers_skipped_even;
    uint64_t numbers_skipped_mod6;
    uint64_t numbers_overflowed;
    double elapsed_seconds;
};

struct BaseDependentSuffixes {
    std::vector<uint32_t> std_allowed;
    std::vector<uint32_t> allowed_0; // Suffixes allowed when base % 6 == 0
    std::vector<uint32_t> allowed_2; // Suffixes allowed when base % 6 == 2
    std::vector<uint32_t> allowed_4; // Suffixes allowed when base % 6 == 4

    uint32_t std_skipped_0 = 0;
    uint32_t std_skipped_1 = 0;
    uint32_t std_skipped_2 = 0;
};

#endif // HAILSTONE_COMMON_H
