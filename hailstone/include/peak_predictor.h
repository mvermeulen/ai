#ifndef HAILSTONE_PEAK_PREDICTOR_H
#define HAILSTONE_PEAK_PREDICTOR_H

#include "common.h"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>

struct PredictedPeak {
    uint128 pred_n;
    uint32_t pred_steps;
    uint128 source_n;
};

class PeakPredictor {
public:
    uint32_t current_max_steps;
    std::vector<PeakRecord> confirmed_peaks;
    std::vector<PredictedPeak> active_predictions;

#ifdef OMIT_STEPS_COMPUTATION
    PeakPredictor() : current_max_steps(0xFFFFFFFF) {}
#else
    PeakPredictor() : current_max_steps(0) {}
#endif

    static inline uint128 divide_by_3(uint128 a) {
        unsigned __int128 val = a.high;
        val = (val << 64) | a.low;
        val /= 3;
        return uint128(static_cast<uint64_t>(val), static_cast<uint64_t>(val >> 64));
    }

    static inline std::string uint128_to_str(uint128 n) {
        if (n.low == 0 && n.high == 0) return "0";
        unsigned __int128 val = n.high;
        val = (val << 64) | n.low;
        std::string s = "";
        while (val > 0) {
            s = std::to_string(static_cast<int>(val % 10)) + s;
            val /= 10;
        }
        return s;
    }

    static inline std::string format_uint128(uint128 n) {
        std::string s = uint128_to_str(n);
        std::string res = "";
        int count = 0;
        for (int i = (int)s.length() - 1; i >= 0; --i) {
            if (count > 0 && count % 3 == 0) {
                res = "," + res;
            }
            res = s[i] + res;
            count++;
        }
        return res;
    }

    static inline bool have_same_path(uint128 n, uint128 n_plus_1) {
        uint128 x = n;
        uint128 y = n_plus_1;
        uint32_t steps_x = 0;
        uint32_t steps_y = 0;

        const uint32_t max_steps = 10000;

        while (x != y && (x > uint128(1) || y > uint128(1))) {
            if (steps_x > max_steps || steps_y > max_steps) {
                return false;
            }
            if (x > uint128(1)) {
                if ((x.low & 1) == 0) {
                    x = shift_right(x, 1);
                } else {
                    bool overflow = false;
                    x = mul3_add1(x, overflow);
                    if (overflow) return false;
                }
                steps_x++;
            }
            if (y > uint128(1)) {
                if ((y.low & 1) == 0) {
                    y = shift_right(y, 1);
                } else {
                    bool overflow = false;
                    y = mul3_add1(y, overflow);
                    if (overflow) return false;
                }
                steps_y++;
            }
        }
        return (x == y && steps_x == steps_y);
    }

    void add_confirmed_peak(uint128 n, uint32_t steps) {
#ifdef OMIT_STEPS_COMPUTATION
        return;
#endif
        // 1. Update max steps
        if (steps > current_max_steps) {
            current_max_steps = steps;
        }

        // 2. Add to confirmed peaks list (avoid duplicates)
        bool found = false;
        for (const auto& p : confirmed_peaks) {
            if (p.start_val == n) {
                found = true;
                break;
            }
        }
        if (!found) {
            confirmed_peaks.push_back({n, uint128(steps)});
        }

        // 3. Generate predictions from this peak
        // sum_mod3 = (n.high % 3 + n.low % 3) % 3
        uint64_t n_mod3 = (n.high % 3 + n.low % 3) % 3;
        if (n_mod3 == 0) {
            // P = 2n, steps = steps + 1
            uint128 pred_n = n + n;
            uint32_t pred_steps = steps + 1;
            add_prediction_if_better(pred_n, pred_steps, n);
        } else if (n_mod3 == 1) {
            // P = (4n - 1) / 3, steps = steps + 3
            uint128 four_n = n + n + n + n;
            if (four_n >= n) { // check overflow
                uint128 four_n_minus_1 = four_n - uint128(1);
                uint128 pred_n = divide_by_3(four_n_minus_1);
                uint32_t pred_steps = steps + 3;
                add_prediction_if_better(pred_n, pred_steps, n);
            }
        }

        // 3b. Try predicting based on n + 1 if n is an even peak
        if ((n.low & 1) == 0) {
            uint128 n_plus_1 = n + uint128(1);
            if (n_plus_1 >= n) { // check overflow
                if (have_same_path(n, n_plus_1)) {
                    uint64_t np1_mod3 = (n_plus_1.high % 3 + n_plus_1.low % 3) % 3;
                    if (np1_mod3 == 0) {
                        uint128 pred_n = n_plus_1 + n_plus_1;
                        uint32_t pred_steps = steps + 1;
                        add_prediction_if_better(pred_n, pred_steps, n_plus_1);
                    } else if (np1_mod3 == 1) {
                        uint128 four_np1 = n_plus_1 + n_plus_1 + n_plus_1 + n_plus_1;
                        if (four_np1 >= n_plus_1) {
                            uint128 four_np1_minus_1 = four_np1 - uint128(1);
                            uint128 pred_n = divide_by_3(four_np1_minus_1);
                            uint32_t pred_steps = steps + 3;
                            add_prediction_if_better(pred_n, pred_steps, n_plus_1);
                        }
                    }
                }
            }
        }

        // 4. Prune active predictions
        prune_predictions();
    }

    void add_prediction_if_better(uint128 pred_n, uint32_t pred_steps, uint128 source_n) {
        if (pred_steps <= current_max_steps) return;

        // Check if we already have a prediction for pred_n
        for (auto& p : active_predictions) {
            if (p.pred_n == pred_n) {
                if (pred_steps > p.pred_steps) {
                    p.pred_steps = pred_steps;
                    p.source_n = source_n;
                }
                return;
            }
        }
        active_predictions.push_back({pred_n, pred_steps, source_n});
    }

    void prune_predictions() {
        std::vector<PredictedPeak> kept;
        for (const auto& p : active_predictions) {
            if (p.pred_steps > current_max_steps) {
                kept.push_back(p);
            }
        }
        active_predictions = kept;
    }

    void prune_predictions_less_than(uint128 start) {
        std::vector<PredictedPeak> kept;
        for (const auto& p : active_predictions) {
            if (p.pred_n >= start) {
                kept.push_back(p);
            }
        }
        active_predictions = kept;
    }

    template <typename T, typename Create>
    void process_up_to_generic(uint128 curr_n, std::vector<T>& steps_peaks, Create create) {
#ifdef OMIT_STEPS_COMPUTATION
        return;
#endif
        bool updated = true;
        while (updated) {
            updated = false;

            int best_idx = -1;
            uint128 best_n = uint128(0, 0);

            for (size_t i = 0; i < active_predictions.size(); ++i) {
                const auto& p = active_predictions[i];
                if (p.pred_n <= curr_n && p.pred_steps > current_max_steps) {
                    if (best_idx == -1 || p.pred_n < best_n) {
                        best_idx = (int)i;
                        best_n = p.pred_n;
                    }
                }
            }

            if (best_idx != -1) {
                PredictedPeak confirmed = active_predictions[best_idx];
                active_predictions.erase(active_predictions.begin() + best_idx);

                std::cout << "[Peak Predictor] Confirmed peak: n = " << format_uint128(confirmed.pred_n)
                          << " -> steps = " << confirmed.pred_steps
                          << " (Predicted from source n = " << format_uint128(confirmed.source_n) << ")" << std::endl;

                steps_peaks.push_back(create(confirmed.pred_n, confirmed.pred_steps));
                add_confirmed_peak(confirmed.pred_n, confirmed.pred_steps);

                updated = true;
            }
        }
    }

    void process_up_to(uint128 curr_n, std::vector<PeakRecord>& steps_peaks) {
        process_up_to_generic(curr_n, steps_peaks, [](uint128 n, uint32_t steps) {
            return PeakRecord{n, uint128(steps)};
        });
    }

    void print_future_predictions_by_block() const {
#ifdef OMIT_STEPS_COMPUTATION
        return;
#endif
        if (active_predictions.empty()) return;

        std::vector<PredictedPeak> sorted_preds = active_predictions;
        std::sort(sorted_preds.begin(), sorted_preds.end(), [](const PredictedPeak& a, const PredictedPeak& b) {
            return a.pred_n < b.pred_n;
        });

        std::cout << "\n=== Predicted Peaks in Future Blocks ===" << std::endl;
        std::cout << "  " << std::left << std::setw(25) << "Predicted Peak (n)"
                  << " | " << std::setw(6) << "Steps"
                  << " | " << std::setw(15) << "Block #"
                  << " | " << "Source Peak (n)" << std::endl;
        std::cout << "  " << std::string(75, '-') << std::endl;

        for (const auto& p : sorted_preds) {
            unsigned __int128 val = p.pred_n.high;
            val = (val << 64) | p.pred_n.low;
            double block_num = (double)val / (double)(1ULL << 32);

            std::cout << "  " << std::left << std::setw(25) << format_uint128(p.pred_n)
                      << " | " << std::setw(6) << p.pred_steps
                      << " | Block " << std::setw(9) << std::fixed << std::setprecision(2) << block_num
                      << " | " << format_uint128(p.source_n) << std::endl;
        }
        std::cout << std::endl;
    }
};

#endif // HAILSTONE_PEAK_PREDICTOR_H
