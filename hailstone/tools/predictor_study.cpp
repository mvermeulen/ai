#include <iostream>
#include <vector>
#include <unordered_set>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

uint32_t get_steps(uint64_t x) {
    if (x == 0) return 0;
    if (x == 1) return 0;
    if (x == 2) return 1;

    uint32_t steps = 0;
    while (x > 1) {
        if (x % 2 == 0) {
            int p = __builtin_ctzll(x);
            x >>= p;
            steps += p;
        } else {
            // Check for overflow isn't strictly necessary for x < 2^32, 
            // but we'll just use a direct 64-bit computation
            x = 3 * x + 1;
            steps++;
            
            int p = __builtin_ctzll(x);
            x >>= p;
            steps += p;
        }
    }
    return steps;
}

int main() {
    std::cout << "Starting empirical predictor study from 1 to 2^32..." << std::endl;
    
    uint32_t current_max_steps = 0;
    uint64_t max_value = 0xffffffffULL;
    
    std::unordered_set<uint64_t> found_predictors;
    std::vector<uint64_t> peaks;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // The main search loop logic (as in block 0)
    // We skip evens, and we skip mod 6 == 5 (i.e. x % 3 == 2 since x is odd)
    for (uint64_t i = 1; i <= max_value; i += 2) {
        if (i % 3 == 2) {
            continue; // Mod 6 cutoff (x % 2 == 1 && x % 3 == 2 -> x % 6 == 5)
        }
        
        uint32_t steps = get_steps(i);
        
        // Record almost peaks (predictors)
        if (current_max_steps >= 2 && steps >= current_max_steps - 2) {
            found_predictors.insert(i);
        }
        
        // Record actual peaks
        if (steps > current_max_steps) {
            current_max_steps = steps;
            peaks.push_back(i);
            found_predictors.insert(i); // a peak is trivially a predictor
        }
        
        if (i % 0x10000000 == 1) {
            std::cout << "Progress: " << i << " / " << max_value 
                      << " (max_steps: " << current_max_steps << ")" << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end_time - start_time).count();
    
    // Post-search analysis
    uint64_t suffix_01_peaks_count = 0;
    uint64_t predicted_peaks_count = 0;
    uint64_t missed_peaks_count = 0;
    
    uint64_t missed_due_to_even = 0;
    uint64_t missed_due_to_mod6 = 0;
    uint64_t missed_due_to_other = 0;

    std::cout << "\nAnalyzing 01 suffix peaks..." << std::endl;
    for (uint64_t P : peaks) {
        if (P % 4 == 1) { // 01 suffix
            suffix_01_peaks_count++;
            uint64_t req_predictor = (3 * P + 1) / 4;
            
            if (found_predictors.find(req_predictor) != found_predictors.end()) {
                predicted_peaks_count++;
            } else {
                missed_peaks_count++;
                
                // Determine why it was missed
                if (req_predictor % 2 == 0) {
                    missed_due_to_even++;
                } else if (req_predictor % 6 == 5) {
                    missed_due_to_mod6++;
                } else {
                    missed_due_to_other++;
                }
            }
        }
    }
    
    std::cout << "\n=== Predictor Study Results ===" << std::endl;
    std::cout << "Time Elapsed: " << elapsed << " seconds." << std::endl;
    std::cout << "Total Peaks Found: " << peaks.size() << std::endl;
    std::cout << "Total Predictors Found (almost peaks): " << found_predictors.size() << std::endl;
    std::cout << "Predictors vs Peaks Ratio: " << (double)found_predictors.size() / peaks.size() << "x more predictors" << std::endl;
    
    std::cout << "\n--- 01 Suffix Peak Prediction ---" << std::endl;
    std::cout << "Total 01 Peaks: " << suffix_01_peaks_count << std::endl;
    std::cout << "Successfully Predicted: " << predicted_peaks_count 
              << " (" << (double)predicted_peaks_count / suffix_01_peaks_count * 100 << "%)" << std::endl;
    std::cout << "Missed Predictors: " << missed_peaks_count 
              << " (" << (double)missed_peaks_count / suffix_01_peaks_count * 100 << "%)" << std::endl;
              
    if (missed_peaks_count > 0) {
        std::cout << "\n--- Breakdown of Missed Predictors ---" << std::endl;
        std::cout << "Missed because predictor was EVEN: " << missed_due_to_even << std::endl;
        std::cout << "Missed because predictor was MOD 6 == 5: " << missed_due_to_mod6 << std::endl;
        std::cout << "Missed for OTHER reasons (e.g. not evaluated, or didn't reach max_steps-2): " << missed_due_to_other << std::endl;
    }
    
    return 0;
}
