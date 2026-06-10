#include "hip_search.hip.h"
#include <iostream>
#include <iomanip>
#include <string>

uint128 parse_uint128(const std::string& str) {
    uint128 res(0, 0);
    bool hex = false;
    size_t start_idx = 0;
    if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        hex = true;
        start_idx = 2;
    }
    for (size_t i = start_idx; i < str.size(); ++i) {
        char c = str[i];
        int val = 0;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (hex && c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else break;

        bool overflow = false;
        if (hex) {
            uint128 r2 = shift_left_1(res, overflow);
            uint128 r4 = shift_left_1(r2, overflow);
            uint128 r8 = shift_left_1(r4, overflow);
            uint128 r16 = shift_left_1(r8, overflow);
            res = r16 + uint128(val);
        } else {
            uint128 r2 = shift_left_1(res, overflow);
            uint128 r4 = shift_left_1(r2, overflow);
            uint128 r8 = shift_left_1(r4, overflow);
            res = r8 + r2 + uint128(val);
        }
    }
    return res;
}

std::string to_string(uint128 n) {
    if (n == uint128(0)) return "0";
    std::string s = "";
    uint128 temp_n = n;
    while (temp_n > uint128(0)) {
        uint128 quotient(0, 0);
        uint64_t rem = 0;
        for (int i = 127; i >= 0; --i) {
            rem = (rem << 1) | (((temp_n.high & 0x8000000000000000ULL) != 0) ? 1 : 0);
            bool of = false;
            temp_n = shift_left_1(temp_n, of);
            quotient = shift_left_1(quotient, of);
            if (rem >= 10) {
                rem -= 10;
                quotient.low |= 1;
            }
        }
        s = std::to_string(rem) + s;
        temp_n = quotient;
    }
    return s;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Hailstone HIP Search Program ===" << std::endl;

    uint128 start(3);
    uint128 end(100000);

    if (argc > 1) {
        start = parse_uint128(argv[1]);
    }
    if (argc > 2) {
        end = parse_uint128(argv[2]);
    }

    std::cout << "Searching range: [" << to_string(start) << ", " << to_string(end) << "]" << std::endl;

    std::vector<PeakRecord> max_value_peaks;
    std::vector<PeakRecord> steps_peaks;
    std::vector<PeakRecord> sigma_peaks;
    PeakState global_peaks;
    SearchMetrics metrics = {0};

    hip_search_range(start, end, max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);

    std::cout << "\n=== Search Completed ===" << std::endl;
    std::cout << "Elapsed Time: " << std::fixed << std::setprecision(4) << metrics.elapsed_seconds << " s" << std::endl;
    std::cout << "Numbers Checked: " << metrics.total_numbers_checked << std::endl;
    std::cout << "Steps Computed: " << metrics.total_steps_computed << std::endl;
    std::cout << "Average Steps: " << (metrics.total_numbers_checked > 0 ? (double)metrics.total_steps_computed / metrics.total_numbers_checked : 0.0) << std::endl;
    std::cout << "Skipped (Even): " << metrics.numbers_skipped_even << std::endl;
    std::cout << "Skipped (Mod 6): " << metrics.numbers_skipped_mod6 << std::endl;
    std::cout << "Overflowed (> 2^128): " << metrics.numbers_overflowed << std::endl;

    double m_ips = (metrics.total_numbers_checked / 1000000.0) / metrics.elapsed_seconds;
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) << m_ips << " M numbers/s" << std::endl;

    std::cout << "\n=== Peaks Found ===" << std::endl;
    std::cout << "Max Value Peaks:" << std::endl;
    for (const auto& peak : max_value_peaks) {
        std::cout << "  n = " << to_string(peak.start_val) << " -> max_val = " << to_string(peak.metric_val) << std::endl;
    }

    std::cout << "\nSteps Peaks:" << std::endl;
    for (const auto& peak : steps_peaks) {
        std::cout << "  n = " << to_string(peak.start_val) << " -> steps = " << to_string(peak.metric_val) << std::endl;
    }

    std::cout << "\nStopping Time (sigma) Peaks:" << std::endl;
    for (const auto& peak : sigma_peaks) {
        std::cout << "  n = " << to_string(peak.start_val) << " -> sigma = " << to_string(peak.metric_val) << std::endl;
    }

    return 0;
}
