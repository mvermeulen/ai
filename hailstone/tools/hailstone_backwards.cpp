#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <map>
#include <set>
#include <cmath>
#include <cstdio>
#include <cstring>

// Represents a peak in the master checkpoint
struct MasterCheckpoint {
    struct Peak {
        uint64_t start_val;
        uint64_t metric_val;
    };
    std::vector<Peak> max_value_peaks;
    std::vector<Peak> steps_peaks;
    
    bool load(const std::string& filename) {
        std::ifstream ifs(filename);
        if (!ifs.is_open()) return false;
        std::string line;
        std::string section = "header";
        while (std::getline(ifs, line)) {
            // trim leading/trailing whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (line.empty()) continue;
            if (line == "max_value_peaks:") {
                section = "max_value_peaks";
                continue;
            } else if (line == "steps_peaks:") {
                section = "steps_peaks";
                continue;
            } else if (line == "sigma_peaks:") {
                section = "sigma_peaks";
                continue;
            }
            if (section == "max_value_peaks" || section == "steps_peaks") {
                std::istringstream iss(line);
                std::string start_str, metric_str;
                if (iss >> start_str >> metric_str) {
                    try {
                        uint64_t start_val = std::stoull(start_str);
                        uint64_t metric_val = std::stoull(metric_str);
                        Peak peak = {start_val, metric_val};
                        if (section == "max_value_peaks") {
                            max_value_peaks.push_back(peak);
                        } else {
                            steps_peaks.push_back(peak);
                        }
                    } catch (...) {
                        // ignore values that exceed uint64_t limits for other ranges
                    }
                }
            }
        }
        return true;
    }
};

// Forward declaration of verification helper
bool verify_results(const std::vector<std::pair<uint64_t, uint32_t>>& generated_peaks, 
                    const std::vector<MasterCheckpoint::Peak>& reference_peaks,
                    uint64_t limit_L);

// Backwards search checkpoint representation
struct BackwardsCheckpoint {
    uint64_t last_L = 0;
    uint64_t max_M = 0;
    uint32_t current_d = 0;
    std::vector<std::vector<uint64_t>> deferred_levels;
    std::vector<std::pair<uint64_t, uint32_t>> steps_peaks;
};

bool save_backwards_checkpoint(const std::string& filename, 
                               uint64_t last_L, 
                               uint64_t max_M, 
                               uint32_t current_d,
                               const std::vector<std::vector<uint64_t>>& deferred_levels,
                               const std::vector<std::pair<uint64_t, uint32_t>>& steps_peaks) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) return false;
    
    ofs << "last_L: " << last_L << "\n";
    ofs << "max_M: " << max_M << "\n";
    ofs << "current_d: " << current_d << "\n\n";
    
    ofs << "deferred_nodes:\n";
    for (size_t d = 0; d < deferred_levels.size(); ++d) {
        for (uint64_t val : deferred_levels[d]) {
            ofs << d << " " << val << "\n";
        }
    }
    ofs << "\n";
    
    ofs << "steps_peaks:\n";
    for (const auto& peak : steps_peaks) {
        ofs << peak.first << " " << peak.second << "\n";
    }
    
    return true;
}

bool load_backwards_checkpoint(const std::string& filename, BackwardsCheckpoint& bc) {
    FILE* fp = fopen(filename.c_str(), "r");
    if (!fp) return false;
    
    char line_buf[256];
    bc.deferred_levels.clear();
    bc.steps_peaks.clear();
    
    bool in_deferred = false;
    bool in_peaks = false;
    
    // Reserve estimation if we can get file size
    std::ifstream in(filename, std::ifstream::ate | std::ifstream::binary);
    if (in.is_open()) {
        auto size = in.tellg();
        bc.deferred_levels.reserve(1000);
    }
    
    while (fgets(line_buf, sizeof(line_buf), fp)) {
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
            line_buf[--len] = '\0';
        }
        if (len == 0) continue;
        
        if (strcmp(line_buf, "deferred_nodes:") == 0) {
            in_deferred = true;
            in_peaks = false;
            continue;
        } else if (strcmp(line_buf, "steps_peaks:") == 0) {
            in_deferred = false;
            in_peaks = true;
            continue;
        }
        
        if (!in_deferred && !in_peaks) {
            char* colon = strchr(line_buf, ':');
            if (colon) {
                *colon = '\0';
                char* val = colon + 1;
                while (*val == ' ' || *val == '\t') val++;
                if (strcmp(line_buf, "last_L") == 0) {
                    bc.last_L = strtoull(val, nullptr, 10);
                } else if (strcmp(line_buf, "max_M") == 0) {
                    bc.max_M = strtoull(val, nullptr, 10);
                } else if (strcmp(line_buf, "current_d") == 0) {
                    bc.current_d = strtoul(val, nullptr, 10);
                }
            }
        } else if (in_deferred) {
            char* end;
            uint32_t d = strtoul(line_buf, &end, 10);
            uint64_t val = strtoull(end, nullptr, 10);
            
            if (val <= bc.max_M) {
                // Old format conversion:
                uint32_t child_d = d + 1;
                uint64_t child_val = 2 * val;
                if (child_d >= bc.deferred_levels.size()) {
                    bc.deferred_levels.resize(child_d + 1);
                }
                bc.deferred_levels[child_d].push_back(child_val);
            } else {
                // New format:
                if (d >= bc.deferred_levels.size()) {
                    bc.deferred_levels.resize(d + 1);
                }
                bc.deferred_levels[d].push_back(val);
            }
        } else if (in_peaks) {
            char* end;
            uint64_t val = strtoull(line_buf, &end, 10);
            uint32_t steps = strtoul(end, nullptr, 10);
            bc.steps_peaks.push_back({val, steps});
        }
    }
    fclose(fp);
    return true;
}

uint64_t find_max_value_bound(const MasterCheckpoint& mc, uint64_t limit_L) {
    uint64_t max_val = 0;
    for (const auto& peak : mc.max_value_peaks) {
        if (peak.start_val < limit_L) {
            if (peak.metric_val > max_val) {
                max_val = peak.metric_val;
            }
        }
    }
    return max_val;
}

double get_max_growth(uint32_t s) {
    if (s == 0) return 1.0;
    if (s % 2 == 0) {
        return std::pow(1.5, s / 2);
    } else {
        return 3.0 * std::pow(1.5, (s - 1) / 2);
    }
}

bool is_active(uint64_t z, uint32_t child_d, uint64_t current_L, uint64_t new_M, uint32_t max_steps_bound) {
    if (z > new_M) return false;
    if (child_d < max_steps_bound) {
        double max_allowed = (double)current_L * get_max_growth(max_steps_bound - child_d) * 1.05;
        if ((double)z >= max_allowed) return false;
    } else {
        if (z >= current_L) return false;
    }
    return true;
}

uint32_t find_max_steps_bound(const MasterCheckpoint& mc, uint64_t limit_L) {
    uint32_t max_steps = 0;
    for (const auto& peak : mc.steps_peaks) {
        if (peak.start_val < limit_L) {
            if (peak.metric_val > max_steps) {
                max_steps = peak.metric_val;
            }
        }
    }
    return max_steps;
}

void record_step_count(uint64_t y, uint32_t steps, uint64_t last_L, uint64_t current_L,
                       std::vector<uint32_t>& step_counts,
                       const std::vector<std::pair<uint64_t, uint32_t>>& running_peaks,
                       std::vector<std::pair<uint64_t, uint32_t>>& extra_candidates,
                       uint32_t& max_steps_recorded) {
    if (y < 3) return;
    if (y >= last_L && y < current_L) {
        step_counts[y - last_L] = steps;
        if (steps > max_steps_recorded) {
            max_steps_recorded = steps;
            std::cout << "  New BFS Candidate Peak: n = " << y << " -> steps = " << steps << std::endl;
        }
    } else if (y < last_L) {
        // Find the peak immediately before y in running_peaks
        uint32_t prev_steps = 0;
        for (const auto& peak : running_peaks) {
            if (peak.first < y) {
                if (peak.second > prev_steps) {
                    prev_steps = peak.second;
                }
            } else {
                break;
            }
        }
        if (steps > prev_steps) {
            extra_candidates.push_back({y, steps});
            if (steps > max_steps_recorded) {
                max_steps_recorded = steps;
                std::cout << "  New BFS Candidate Peak (Historical): n = " << y << " -> steps = " << steps << std::endl;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== Hailstone Backwards Step Search Prototype ===" << std::endl;
    
    std::string master_chk = "golden_master.chk";
    std::string backwards_chk = "hailstone_backwards.chk";
    uint64_t target_L = 4294967296ULL; // 2^32
    bool use_checkpoint = true;
    bool verbose = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Options:\n"
                      << "  -m, --master FILE        Path to forward master checkpoint file (default: golden_master.chk)\n"
                      << "  -c, --checkpoint FILE    Path to backwards checkpoint file (default: hailstone_backwards.chk)\n"
                      << "  -l, --limit VALUE        Target limit L to search up to (default: 4294967296)\n"
                      << "  --no-checkpoint          Disable loading and saving checkpoints\n"
                      << "  -v, --verbose            Enable verbose metric reporting per level\n";
            return 0;
        } else if (arg == "-m" || arg == "--master") {
            master_chk = argv[++i];
        } else if (arg == "-c" || arg == "--checkpoint") {
            backwards_chk = argv[++i];
        } else if (arg == "-l" || arg == "--limit") {
            target_L = std::stoull(argv[++i]);
        } else if (arg == "--no-checkpoint") {
            use_checkpoint = false;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        }
    }
    
    MasterCheckpoint mc;
    std::cout << "Loading master checkpoint " << master_chk << "... " << std::flush;
    if (!mc.load(master_chk)) {
        std::cerr << "Failed to load master checkpoint. Make sure the file exists." << std::endl;
        return 1;
    }
    std::cout << "Loaded " << mc.max_value_peaks.size() << " max value peaks and " 
              << mc.steps_peaks.size() << " steps peaks." << std::endl;
              
    // Traversal structures
    std::vector<std::vector<uint64_t>> active_levels;
    std::vector<std::vector<uint64_t>> deferred_levels;
    std::vector<std::pair<uint64_t, uint32_t>> running_peaks;
    uint64_t start_L = 65536; // Start with L = 2^16
    uint64_t last_L = 3;
    uint64_t current_M = 0;
    
    BackwardsCheckpoint bc;
    bool checkpoint_loaded = false;
    if (use_checkpoint) {
        std::cout << "Checking for backwards checkpoint " << backwards_chk << "... " << std::flush;
        auto load_start = std::chrono::high_resolution_clock::now();
        if (load_backwards_checkpoint(backwards_chk, bc)) {
            checkpoint_loaded = true;
            auto load_end = std::chrono::high_resolution_clock::now();
            double load_time = std::chrono::duration<double>(load_end - load_start).count();
            std::cout << "Loaded in " << load_time << " s." << std::endl;
            std::cout << "Resuming from last_L = " << bc.last_L 
                      << ", current_d = " << bc.current_d << std::endl;
                      
            last_L = bc.last_L;
            start_L = bc.last_L;
            current_M = bc.max_M;
            running_peaks = bc.steps_peaks;
            
            deferred_levels = std::move(bc.deferred_levels);
        } else {
            std::cout << "Not found. Starting new search." << std::endl;
        }
    }
    
    if (!checkpoint_loaded) {
        // Initialize new search: 1 is the root at level 0
        active_levels.resize(1);
        active_levels[0].push_back(1);
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Incremental loop expanding L
    uint64_t current_L = start_L;
    if (current_L < 65536) current_L = 65536;
    
    while (last_L < target_L) {
        if (current_L > target_L) {
            current_L = target_L;
        }
        
        uint64_t new_M = find_max_value_bound(mc, current_L);
        uint32_t max_steps_bound = find_max_steps_bound(mc, current_L);
        uint32_t max_steps_recorded = running_peaks.empty() ? 0 : running_peaks.back().second;
        
        std::cout << "\n========================================\n"
                  << "Expanding range: L = " << current_L << " (last_L = " << last_L << ")\n"
                  << "Max Value Bound M = " << new_M << "\n"
                  << "Max Steps Bound S = " << max_steps_bound << "\n"
                  << "========================================" << std::endl;
                  
        // Allocate space for the new range step counts
        uint64_t range_size = current_L - last_L;
        std::vector<uint32_t> step_counts(range_size, 0);
        std::vector<std::pair<uint64_t, uint32_t>> extra_candidates;
        
        // 1. Activate deferred nodes from previous ranges
        uint64_t nodes_activated = 0;
        for (size_t d = 0; d < deferred_levels.size(); ++d) {
            if (deferred_levels[d].empty()) continue;
            
            size_t write_idx = 0;
            for (size_t i = 0; i < deferred_levels[d].size(); ++i) {
                uint64_t z = deferred_levels[d][i];
                if (is_active(z, d, current_L, new_M, max_steps_bound)) {
                    // Activate this node!
                    if (d >= active_levels.size()) {
                        active_levels.resize(d + 1);
                    }
                    active_levels[d].push_back(z);
                    nodes_activated++;
                    
                    // Also record step counts if it lies in the current or previous ranges
                    record_step_count(z, d, last_L, current_L, step_counts, running_peaks, extra_candidates, max_steps_recorded);
                } else {
                    deferred_levels[d][write_idx++] = z;
                }
            }
            deferred_levels[d].resize(write_idx);
        }
        
        if (nodes_activated > 0) {
            std::cout << "Activated " << nodes_activated << " deferred nodes from previous ranges." << std::endl;
        }
        
        uint64_t total_nodes_processed = 0;
        uint64_t total_nodes_deferred = 0;
        uint64_t total_nodes_pruned = 0;
        
        auto range_start_time = std::chrono::high_resolution_clock::now();
        
        // 2. Perform BFS loop for active nodes only
        for (size_t d = 0; d < active_levels.size(); ++d) {
            if (active_levels[d].empty()) continue;
            
            std::vector<uint64_t> active_next;
            active_next.reserve(active_levels[d].size());
            
            for (uint64_t x : active_levels[d]) {
                // Elaborate new node
                if (x % 3 == 0) {
                    continue; // Prune multiples of 3
                }
                
                // 1. Odd transition
                if (x % 6 == 4) {
                    uint64_t y = (x - 1) / 3;
                    if (y > 1) {
                        if (is_active(y, d + 1, current_L, new_M, max_steps_bound)) {
                            record_step_count(y, d + 1, last_L, current_L, step_counts, running_peaks, extra_candidates, max_steps_recorded);
                            active_next.push_back(y);
                            total_nodes_processed++;
                        } else {
                            if (d + 1 >= deferred_levels.size()) {
                                deferred_levels.resize(d + 2);
                            }
                            deferred_levels[d + 1].push_back(y);
                            total_nodes_deferred++;
                        }
                    }
                }
                
                // 2. Division transition
                if (x > 0x7FFFFFFFFFFFFFFFULL) {
                    total_nodes_pruned++;
                    continue;
                }
                uint64_t next_val = 2 * x;
                if (is_active(next_val, d + 1, current_L, new_M, max_steps_bound)) {
                    record_step_count(next_val, d + 1, last_L, current_L, step_counts, running_peaks, extra_candidates, max_steps_recorded);
                    active_next.push_back(next_val);
                    total_nodes_processed++;
                } else {
                    if (next_val > 0x7FFFFFFFFFFFFFFFULL) {
                        total_nodes_pruned++;
                    } else {
                        if (d + 1 >= deferred_levels.size()) {
                            deferred_levels.resize(d + 2);
                        }
                        deferred_levels[d + 1].push_back(next_val);
                        total_nodes_deferred++;
                    }
                }
            }
            
            // Clear active_levels[d] as it is fully processed to release memory
            active_levels[d].clear();
            active_levels[d].shrink_to_fit();
            
            if (!active_next.empty()) {
                if (d + 1 >= active_levels.size()) {
                    active_levels.resize(d + 2);
                }
                active_levels[d + 1].insert(active_levels[d + 1].end(), active_next.begin(), active_next.end());
            }
        }
        
        auto range_end_time = std::chrono::high_resolution_clock::now();
        double elapsed_sec = std::chrono::duration<double>(range_end_time - range_start_time).count();
        
        // Report Level & Memory Footprint metrics
        uint64_t active_nodes_count = 0;
        uint64_t deferred_nodes_count = 0;
        uint32_t active_levels_count = 0;
        
        for (size_t d = 0; d < active_levels.size(); ++d) {
            if (!active_levels[d].empty()) active_levels_count++;
            active_nodes_count += active_levels[d].size();
        }
        for (size_t d = 0; d < deferred_levels.size(); ++d) {
            deferred_nodes_count += deferred_levels[d].size();
        }
        
        std::cout << "Range search finished. Elapsed time: " << std::fixed << std::setprecision(4) << elapsed_sec << " s\n"
                  << "Nodes elaborated: " << total_nodes_processed << " (Velocity: " 
                  << (elapsed_sec > 0 ? (total_nodes_processed / elapsed_sec) : 0.0) << " nodes/s)\n"
                  << "Pruned (overflow): " << total_nodes_pruned << "\n"
                  << "Active levels: " << active_levels_count << ", Active nodes: " << active_nodes_count 
                  << ", Deferred nodes: " << deferred_nodes_count << "\n"
                  << "Estimated memory footprint: " << ((deferred_nodes_count * sizeof(uint64_t)) / 1024.0) << " KB" << std::endl;
                  
        // Merge step_counts into running peaks list using doubling peak candidates post-reconciliation
        std::cout << "Filtering and updating steps peaks with mod-3 doubling post-reconciliation..." << std::endl;
        
        std::vector<std::pair<uint64_t, uint32_t>> candidates = running_peaks;
        candidates.insert(candidates.end(), extra_candidates.begin(), extra_candidates.end());
        
        for (uint64_t i = 0; i < range_size; ++i) {
            uint64_t n = last_L + i;
            if (n < 3) continue;
            uint32_t steps = step_counts[i];
            if (steps > 0) {
                candidates.push_back({n, steps});
            }
        }
        
        // Generate even doubling candidates recursively up to current_L
        size_t scan_idx = 0;
        while (scan_idx < candidates.size()) {
            uint64_t n = candidates[scan_idx].first;
            uint32_t steps = candidates[scan_idx].second;
            if (n <= 0x7FFFFFFFFFFFFFFFULL) {
                uint64_t double_n = 2 * n;
                if (double_n < current_L) {
                    candidates.push_back({double_n, steps + 1});
                }
            }
            scan_idx++;
        }
        
        // Deduplicate keeping maximum steps
        std::map<uint64_t, uint32_t> unique_candidates;
        for (const auto& cand : candidates) {
            auto it = unique_candidates.find(cand.first);
            if (it == unique_candidates.end() || cand.second > it->second) {
                unique_candidates[cand.first] = cand.second;
            }
        }
        
        // Sort by starting value
        std::vector<std::pair<uint64_t, uint32_t>> sorted_candidates(unique_candidates.begin(), unique_candidates.end());
        
        // Run peak filter
        std::vector<std::pair<uint64_t, uint32_t>> next_peaks;
        uint32_t max_steps_so_far = 0;
        for (const auto& cand : sorted_candidates) {
            if (cand.second > max_steps_so_far) {
                max_steps_so_far = cand.second;
                next_peaks.push_back(cand);
            }
        }
        
        // Print new peaks
        for (const auto& peak : next_peaks) {
            bool is_new = true;
            for (const auto& prev : running_peaks) {
                if (prev.first == peak.first) {
                    is_new = false;
                    break;
                }
            }
            if (is_new) {
                std::cout << "  New Peak: n = " << peak.first << " -> steps = " << peak.second << std::endl;
            }
        }
        running_peaks = std::move(next_peaks);
        
        last_L = current_L;
        
        if (use_checkpoint) {
            std::cout << "Saving checkpoint to " << backwards_chk << "... " << std::flush;
            uint32_t current_d = deferred_levels.size();
            if (save_backwards_checkpoint(backwards_chk, last_L, new_M, current_d, deferred_levels, running_peaks)) {
                std::cout << "Saved." << std::endl;
            } else {
                std::cout << "Failed!" << std::endl;
            }
        }
        
        // Dynamic Range Expansion
        if (current_L == target_L) {
            break;
        }
        current_L *= 2; // Double the range limit
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    double total_elapsed = std::chrono::duration<double>(end_time - start_time).count();
    std::cout << "\n========================================\n"
              << "Backward Search Complete! Total time: " << total_elapsed << " s\n"
              << "========================================" << std::endl;
              
    // Final verification against reference peaks from golden master
    verify_results(running_peaks, mc.steps_peaks, target_L);
    
    return 0;
}

bool verify_results(const std::vector<std::pair<uint64_t, uint32_t>>& generated_peaks, 
                    const std::vector<MasterCheckpoint::Peak>& reference_peaks,
                    uint64_t limit_L) {
    std::cout << "\n=== Verification Results ===" << std::endl;
    bool all_match = true;
    size_t match_count = 0;
    
    std::map<uint64_t, uint32_t> ref_map;
    for (const auto& peak : reference_peaks) {
        if (peak.start_val < limit_L) {
            ref_map[peak.start_val] = peak.metric_val;
        }
    }
    
    for (const auto& gen_peak : generated_peaks) {
        uint64_t n = gen_peak.first;
        uint32_t steps = gen_peak.second;
        
        auto it = ref_map.find(n);
        if (it == ref_map.end()) {
            std::cout << "  [FAIL] Generated peak n = " << n << " with steps = " << steps 
                      << " not found in reference peaks!" << std::endl;
            all_match = false;
        } else if (it->second != steps) {
            std::cout << "  [FAIL] Generated peak n = " << n << " has steps = " << steps 
                      << " but reference has steps = " << it->second << "!" << std::endl;
            all_match = false;
        } else {
            match_count++;
        }
    }
    
    size_t expected_count = ref_map.size();
    if (match_count < expected_count) {
        std::cout << "  [FAIL] Missed " << (expected_count - match_count) << " reference peaks!" << std::endl;
        std::cout << "  Expected " << expected_count << " peaks, but only generated " << match_count << "." << std::endl;
        all_match = false;
    }
    
    if (all_match) {
        std::cout << "  [SUCCESS] All " << match_count << " generated peaks match reference peaks exactly!" << std::endl;
    }
    return all_match;
}
