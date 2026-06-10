#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <memory>
#include <array>
#include <algorithm>
#include <sys/stat.h>

struct RunResults {
    std::string backend_name;
    long long numbers_checked = -1;
    long long steps_computed = -1;
    long long skipped_mod6 = -1;
    long long overflowed = -1;
    std::vector<std::pair<std::string, std::string>> max_val_peaks;
    std::vector<std::pair<std::string, std::string>> steps_peaks;
    std::vector<std::pair<std::string, std::string>> sigma_peaks;
};

bool file_exists(const std::string& filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

struct FileDeleter {
    void operator()(FILE* f) const {
        if (f) pclose(f);
    }
};

std::string run_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, FileDeleter> pipe(popen(cmd.c_str(), "r"));
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

RunResults parse_output(const std::string& backend, const std::string& output) {
    RunResults res;
    res.backend_name = backend;
    std::istringstream iss(output);
    std::string line;
    int current_section = 0; // 1: Max Value, 2: Steps, 3: Sigma

    while (std::getline(iss, line)) {
        // Trim leading and trailing whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        // Metrics parsing
        if (line.find("Numbers Checked:") != std::string::npos) {
            res.numbers_checked = std::stoll(line.substr(line.find(":") + 1));
        } else if (line.find("Steps Computed:") != std::string::npos) {
            res.steps_computed = std::stoll(line.substr(line.find(":") + 1));
        } else if (line.find("Skipped (Mod 6):") != std::string::npos) {
            res.skipped_mod6 = std::stoll(line.substr(line.find(":") + 1));
        } else if (line.find("Overflowed (> 2^128):") != std::string::npos) {
            res.overflowed = std::stoll(line.substr(line.find(":") + 1));
        }

        // Section switches
        if (line.find("Max Value Peaks") != std::string::npos) {
            current_section = 1;
            continue;
        } else if (line.find("Steps Peaks") != std::string::npos) {
            current_section = 2;
            continue;
        } else if (line.find("Stopping Time (sigma) Peaks") != std::string::npos) {
            current_section = 3;
            continue;
        }

        // Parse peak lines: "n = X -> value/max_val/steps/sigma = Y"
        if (line.rfind("n =", 0) == 0) {
            size_t n_pos = 2;
            size_t arrow_pos = line.find("->");
            if (arrow_pos != std::string::npos) {
                std::string n_str = line.substr(n_pos, arrow_pos - n_pos);
                n_str.erase(0, n_str.find_first_not_of(" \t"));
                n_str.erase(n_str.find_last_not_of(" \t") + 1);

                std::string val_part = line.substr(arrow_pos + 2);
                size_t eq_pos = val_part.find("=");
                if (eq_pos != std::string::npos) {
                    std::string val_str = val_part.substr(eq_pos + 1);
                    val_str.erase(0, val_str.find_first_not_of(" \t"));
                    val_str.erase(val_str.find_last_not_of(" \t") + 1);

                    auto peak = std::make_pair(n_str, val_str);
                    if (current_section == 1) {
                        res.max_val_peaks.push_back(peak);
                    } else if (current_section == 2) {
                        res.steps_peaks.push_back(peak);
                    } else if (current_section == 3) {
                        res.sigma_peaks.push_back(peak);
                    }
                }
            }
        }
    }
    return res;
}

bool compare_results(const RunResults& ref, const RunResults& target) {
    bool passed = true;

    auto print_err_header = [&ref, &target]() {
        std::cerr << "Mismatch between golden [" << ref.backend_name << "] and target [" << target.backend_name << "]" << std::endl;
    };

    if (ref.numbers_checked != target.numbers_checked) {
        print_err_header();
        std::cerr << "  Numbers Checked: " << ref.numbers_checked << " vs " << target.numbers_checked << std::endl;
        passed = false;
    }
    if (ref.steps_computed != target.steps_computed) {
        print_err_header();
        std::cerr << "  Steps Computed: " << ref.steps_computed << " vs " << target.steps_computed << std::endl;
        passed = false;
    }
    if (ref.skipped_mod6 != target.skipped_mod6) {
        print_err_header();
        std::cerr << "  Skipped (Mod 6): " << ref.skipped_mod6 << " vs " << target.skipped_mod6 << std::endl;
        passed = false;
    }
    if (ref.overflowed != target.overflowed) {
        print_err_header();
        std::cerr << "  Overflowed: " << ref.overflowed << " vs " << target.overflowed << std::endl;
        passed = false;
    }

    auto compare_peaks = [&](const std::string& name, 
                             const std::vector<std::pair<std::string, std::string>>& ref_p, 
                             const std::vector<std::pair<std::string, std::string>>& target_p) {
        if (ref_p.size() != target_p.size()) {
            print_err_header();
            std::cerr << "  " << name << " count mismatch: " << ref_p.size() << " vs " << target_p.size() << std::endl;
            passed = false;
            return;
        }
        for (size_t i = 0; i < ref_p.size(); ++i) {
            if (ref_p[i].first != target_p[i].first || ref_p[i].second != target_p[i].second) {
                print_err_header();
                std::cerr << "  " << name << " mismatch at index " << i << ": n=" << ref_p[i].first << "->val=" << ref_p[i].second 
                          << " vs n=" << target_p[i].first << "->val=" << target_p[i].second << std::endl;
                passed = false;
            }
        }
    };

    compare_peaks("Max Value Peaks", ref.max_val_peaks, target.max_val_peaks);
    compare_peaks("Steps Peaks", ref.steps_peaks, target.steps_peaks);
    compare_peaks("Stopping Time (sigma) Peaks", ref.sigma_peaks, target.sigma_peaks);

    return passed;
}

int main() {
    std::cout << "=== Cross-Backend Verification Engine ===" << std::endl;

    std::vector<std::string> test_ranges = {
        "3 100",
        "3 1000",
        "100 1000",
        "1000 10000",
        "27 27" // single peak value
    };

    bool all_passed = true;

    for (const auto& range : test_ranges) {
        std::cout << "\nTesting range: [" << range << "]" << std::endl;

        // Run CPU (golden reference)
        std::string cpu_cmd = "./hailstone_cpu " + range;
        std::string cpu_out;
        try {
            cpu_out = run_command(cpu_cmd);
        } catch (const std::exception& e) {
            std::cerr << "Error running CPU search: " << e.what() << std::endl;
            return 1;
        }
        RunResults cpu_res = parse_output("CPU", cpu_out);

        // Run Vulkan
        if (file_exists("./hailstone_vulkan")) {
            std::string vulkan_cmd = "./hailstone_vulkan " + range;
            std::string vulkan_out;
            try {
                vulkan_out = run_command(vulkan_cmd);
            } catch (const std::exception& e) {
                std::cerr << "Error running Vulkan search: " << e.what() << std::endl;
                all_passed = false;
                continue;
            }
            RunResults vulkan_res = parse_output("Vulkan", vulkan_out);
            if (compare_results(cpu_res, vulkan_res)) {
                std::cout << "  [PASS] Vulkan matches CPU reference." << std::endl;
            } else {
                std::cout << "  [FAIL] Vulkan mismatch!" << std::endl;
                all_passed = false;
            }
        } else {
            std::cout << "  [SKIP] Vulkan executable not found." << std::endl;
        }

        // Run HIP
        if (file_exists("./hailstone_hip")) {
            std::string hip_cmd = "./hailstone_hip " + range;
            std::string hip_out;
            try {
                hip_out = run_command(hip_cmd);
            } catch (const std::exception& e) {
                std::cerr << "Error running HIP search: " << e.what() << std::endl;
                all_passed = false;
                continue;
            }
            RunResults hip_res = parse_output("HIP", hip_out);
            if (compare_results(cpu_res, hip_res)) {
                std::cout << "  [PASS] HIP matches CPU reference." << std::endl;
            } else {
                std::cout << "  [FAIL] HIP mismatch!" << std::endl;
                all_passed = false;
            }
        } else {
            // Not found is expected in our environment since ROCm is not installed
        }
    }

    if (all_passed) {
        std::cout << "\nALL BACKENDS VERIFIED SUCCESSFULLY!" << std::endl;
        return 0;
    } else {
        std::cout << "\nVERIFICATION FAILED!" << std::endl;
        return 1;
    }
}
