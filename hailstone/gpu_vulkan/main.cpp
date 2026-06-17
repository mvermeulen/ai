#include <iostream>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <cstring>
#include <cassert>
#include <iomanip>
#include <chrono>
#include <map>
#include <cstdlib>
#include <cstdio>
#include <vulkan/vulkan.h>
#include "steps_table.h"
#include "peak_predictor.h"
#define POLY_WIDTH 8
#include "fpoly_table.h"

struct poly_gpu {
    uint32_t mul3;
    uint32_t div2;
    uint32_t steps;
    uint32_t add;
    uint32_t smaller;
};

#define MAX_PEAK_RECORDS 65536

struct uint128_gpu {
    uint64_t low;
    uint64_t high;
};

struct PeakRecordGpu {
    uint128_gpu start;
    uint128_gpu val;
};

struct GlobalPeaksGpu {
    uint128_gpu max_val;
    uint32_t max_steps;
    uint32_t max_sigma;
};

struct PeaksCountGpu {
    uint32_t max_val_count;
    uint32_t steps_count;
    uint32_t sigma_count;
};

struct GlobalMetricsGpu {
    uint64_t total_checked;
    uint64_t total_steps;
    uint64_t skipped_mod6;
    uint64_t overflowed;
};

struct PushConstantsGpu {
    uint64_t start_prefix_low;
    uint64_t start_prefix_high;
    uint64_t start_val_low;
    uint64_t start_val_high;
    uint64_t end_val_low;
    uint64_t end_val_high;
    uint32_t allowed_suffixes_size;
    uint32_t cutoff_width;
    uint32_t total_work_items;
    uint32_t allowed_offset;
    uint32_t prefix_stride;
    uint32_t check_start;
    uint32_t check_end;
    uint64_t init_max_val_low;
    uint64_t init_max_val_high;
    uint32_t init_max_steps;
    uint32_t init_max_sigma;
};

BaseDependentSuffixes generate_base_dependent_suffixes(int width) {
    int total_suffixes = 1 << width;
    struct PolyKey {
        uint32_t pow2;
        uint32_t pow3;
        uint64_t add;
        bool operator<(const PolyKey& o) const {
            if (pow2 != o.pow2) return pow2 < o.pow2;
            if (pow3 != o.pow3) return pow3 < o.pow3;
            return add < o.add;
        }
    };

    struct ClassInfo {
        int first_suffix = -1;
        bool has_even = false;
        std::vector<uint32_t> members;
    };
    std::map<PolyKey, ClassInfo> classes;
    std::vector<PolyKey> suffix_polys(total_suffixes);

    for (int i = 0; i < total_suffixes; ++i) {
        uint64_t bits = i;
        int w = width;
        uint32_t pow2 = 0;
        uint32_t pow3 = 0;
        while (w > 0) {
            if (bits & 1) {
                bits = bits * 3 + 1;
                pow3++;
            } else {
                bits >>= 1;
                pow2++;
                w--;
            }
        }
        PolyKey key{pow2, pow3, bits};
        suffix_polys[i] = key;
        
        auto& info = classes[key];
        if (info.first_suffix == -1) {
            info.first_suffix = i;
        }
        info.members.push_back(i);
        if (i % 2 == 0) {
            info.has_even = true;
        }
    }

    BaseDependentSuffixes res;
    
    // Build std_allowed
    for (int i = 0; i < total_suffixes; ++i) {
        const auto& key = suffix_polys[i];
        const auto& info = classes[key];
        if (info.first_suffix == i && !info.has_even) {
            res.std_allowed.push_back(i);
        }
    }
    
    // Precompute skipped counts for std_allowed
    for (uint32_t s : res.std_allowed) {
        if (s % 3 == 2) res.std_skipped_0++;
        if ((1 + s) % 3 == 2) res.std_skipped_1++;
        if ((2 + s) % 3 == 2) res.std_skipped_2++;
    }

    // Build base-dependent allowed lists
    for (const auto& pair : classes) {
        const auto& info = pair.second;
        if (info.has_even) continue;
        
        uint32_t r1 = info.first_suffix;
        bool has_1 = false;
        bool has_3 = false;
        bool has_5 = false;
        for (uint32_t m : info.members) {
            uint32_t rem = m % 6;
            if (rem == 1) has_1 = true;
            else if (rem == 3) has_3 = true;
            else if (rem == 5) has_5 = true;
        }
        
        if (!has_5) res.allowed_0.push_back(r1);
        if (!has_3) res.allowed_2.push_back(r1);
        if (!has_1) res.allowed_4.push_back(r1);
    }
    
    std::sort(res.allowed_0.begin(), res.allowed_0.end());
    std::sort(res.allowed_2.begin(), res.allowed_2.end());
    std::sort(res.allowed_4.begin(), res.allowed_4.end());
    
    return res;
}

bool load_allowed_suffixes_binary(const std::string& filepath, BaseDependentSuffixes& suffixes) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return false;

    uint32_t header[7];
    if (fread(header, sizeof(uint32_t), 7, fp) != 7) {
        fclose(fp);
        return false;
    }

    uint32_t std_count = header[0];
    uint32_t count_0 = header[1];
    uint32_t count_2 = header[2];
    uint32_t count_4 = header[3];
    suffixes.std_skipped_0 = header[4];
    suffixes.std_skipped_1 = header[5];
    suffixes.std_skipped_2 = header[6];

    suffixes.std_allowed.resize(std_count);
    suffixes.allowed_0.resize(count_0);
    suffixes.allowed_2.resize(count_2);
    suffixes.allowed_4.resize(count_4);

    if (fread(suffixes.std_allowed.data(), sizeof(uint32_t), std_count, fp) != std_count ||
        fread(suffixes.allowed_0.data(), sizeof(uint32_t), count_0, fp) != count_0 ||
        fread(suffixes.allowed_2.data(), sizeof(uint32_t), count_2, fp) != count_2 ||
        fread(suffixes.allowed_4.data(), sizeof(uint32_t), count_4, fp) != count_4) {
        fclose(fp);
        return false;
    }

    fclose(fp);
    return true;
}

BaseDependentSuffixes load_allowed_suffixes_24() {
    BaseDependentSuffixes suffixes;
    std::vector<std::string> paths = {
        "build/allowed_suffixes_24.bin",
        "allowed_suffixes_24.bin",
        "cpu/allowed_suffixes_24.bin",
        "gpu_vulkan/allowed_suffixes_24.bin"
    };
    bool loaded = false;
    for (const auto& path : paths) {
        if (load_allowed_suffixes_binary(path, suffixes)) {
            loaded = true;
            break;
        }
    }
    if (!loaded) {
        std::cerr << "\nError: Could not load build-time precomputed allowed_suffixes_24.bin file from any search path!" << std::endl;
        std::cerr << "Please ensure the binary file was generated and exists." << std::endl;
        std::exit(1);
    }
    return suffixes;
}


// Error check helper
#define VK_CHECK(x) \
    do { \
        VkResult err = x; \
        if (err != VK_SUCCESS) { \
            std::cerr << "Vulkan Error: " << err << " at " << __LINE__ << std::endl; \
            exit(1); \
        } \
    } while (0)

std::vector<uint32_t> generate_allowed_suffixes(int width) {
    int total_suffixes = 1 << width;
    struct PolyKey {
        uint32_t pow2;
        uint32_t pow3;
        uint64_t add;
        bool operator<(const PolyKey& o) const {
            if (pow2 != o.pow2) return pow2 < o.pow2;
            if (pow3 != o.pow3) return pow3 < o.pow3;
            return add < o.add;
        }
    };

    struct ClassInfo {
        int first_suffix = -1;
        bool has_even = false;
    };
    std::map<PolyKey, ClassInfo> classes;
    std::vector<PolyKey> suffix_polys(total_suffixes);

    for (int i = 0; i < total_suffixes; ++i) {
        uint64_t bits = i;
        int w = width;
        uint32_t pow2 = 0;
        uint32_t pow3 = 0;
        while (w > 0) {
            if (bits & 1) {
                bits = bits * 3 + 1;
                pow3++;
            } else {
                bits >>= 1;
                pow2++;
                w--;
            }
        }
        PolyKey key{pow2, pow3, bits};
        suffix_polys[i] = key;
        
        auto& info = classes[key];
        if (info.first_suffix == -1) {
            info.first_suffix = i;
        }
        if (i % 2 == 0) {
            info.has_even = true;
        }
    }

    std::vector<uint32_t> allowed;
    for (int i = 0; i < total_suffixes; ++i) {
        const auto& key = suffix_polys[i];
        const auto& info = classes[key];
        if (info.first_suffix == i && !info.has_even) {
            allowed.push_back(i);
        }
    }
    return allowed;
}

std::vector<char> read_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file " + filename);
    }
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

std::string u128_to_string(uint128_gpu n) {
    if (n.low == 0 && n.high == 0) return "0";
    std::string s = "";
    // standard 128-bit conversion
    unsigned __int128 temp = n.high;
    temp = (temp << 64) | n.low;
    while (temp > 0) {
        s = std::to_string((int)(temp % 10)) + s;
        temp /= 10;
    }
    return s;
}

uint128_gpu parse_uint128_gpu(const std::string& str) {
    uint128_gpu res = {0, 0};
    unsigned __int128 temp = 0;
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
        temp = temp * (hex ? 16 : 10) + val;
    }
    res.low = static_cast<uint64_t>(temp);
    res.high = static_cast<uint64_t>(temp >> 64);
    return res;
}

uint32_t find_memory_type(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}
#include <algorithm>

void filter_and_print_peaks(const std::string& name, std::vector<PeakRecordGpu>& peaks, uint32_t count) {
    if (count == 0) {
        std::cout << "\n" << name << " (0):" << std::endl;
        return;
    }
    peaks.resize(count);

    // Sort by start value ascending
    std::sort(peaks.begin(), peaks.end(), [](const PeakRecordGpu& a, const PeakRecordGpu& b) {
        unsigned __int128 start_a = a.start.high;
        start_a = (start_a << 64) | a.start.low;
        unsigned __int128 start_b = b.start.high;
        start_b = (start_b << 64) | b.start.low;
        return start_a < start_b;
    });

    // Filter false positives (out-of-order execution artifacts)
    std::vector<PeakRecordGpu> true_peaks;
    unsigned __int128 running_max = 0;
    for (const auto& peak : peaks) {
        unsigned __int128 val = peak.val.high;
        val = (val << 64) | peak.val.low;
        if (val > running_max) {
            running_max = val;
            true_peaks.push_back(peak);
        }
    }

    std::cout << "\n" << name << " (" << true_peaks.size() << "):" << std::endl;
    for (const auto& peak : true_peaks) {
        std::cout << "  n = " << u128_to_string(peak.start) << " -> value = " << u128_to_string(peak.val) << std::endl;
    }
}

bool save_checkpoint(const std::string& filename,
                     uint128_gpu last_num,
                     const std::vector<PeakRecordGpu>& max_value_peaks,
                     const std::vector<PeakRecordGpu>& steps_peaks,
                     const std::vector<PeakRecordGpu>& sigma_peaks,
                     const GlobalPeaksGpu& global_peaks) {
    std::ofstream ofs(filename);
    if (!ofs.is_open()) {
        std::cerr << "Warning: Could not open checkpoint file " << filename << " for writing." << std::endl;
        return false;
    }
    ofs << "last_num: " << u128_to_string(last_num) << "\n";
    ofs << "max_value: " << u128_to_string(global_peaks.max_val) << "\n";
    ofs << "max_steps: " << global_peaks.max_steps << "\n";
    ofs << "max_sigma: " << global_peaks.max_sigma << "\n\n";

    ofs << "max_value_peaks:\n";
    for (const auto& peak : max_value_peaks) {
        ofs << u128_to_string(peak.start) << " " << u128_to_string(peak.val) << "\n";
    }
    ofs << "\n";

    ofs << "steps_peaks:\n";
    for (const auto& peak : steps_peaks) {
        ofs << u128_to_string(peak.start) << " " << u128_to_string(peak.val) << "\n";
    }
    ofs << "\n";

    ofs << "sigma_peaks:\n";
    for (const auto& peak : sigma_peaks) {
        ofs << u128_to_string(peak.start) << " " << u128_to_string(peak.val) << "\n";
    }
    ofs << "\n";

    ofs.close();
    return true;
}

bool load_checkpoint(const std::string& filename,
                     uint128_gpu& last_num,
                     std::vector<PeakRecordGpu>& max_value_peaks,
                     std::vector<PeakRecordGpu>& steps_peaks,
                     std::vector<PeakRecordGpu>& sigma_peaks,
                     GlobalPeaksGpu& global_peaks) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        return false;
    }
    
    std::string line;
    std::string section = "header";

    while (std::getline(ifs, line)) {
        // Trim
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

        if (section == "header") {
            size_t colon = line.find(":");
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                val.erase(0, val.find_first_not_of(" \t"));
                if (key == "last_num") {
                    last_num = parse_uint128_gpu(val);
                } else if (key == "max_value") {
                    global_peaks.max_val = parse_uint128_gpu(val);
                } else if (key == "max_steps") {
                    global_peaks.max_steps = std::stoul(val);
                } else if (key == "max_sigma") {
                    global_peaks.max_sigma = std::stoul(val);
                }
            }
        } else {
            std::istringstream iss(line);
            std::string start_str, metric_str;
            if (iss >> start_str >> metric_str) {
                PeakRecordGpu record;
                record.start = parse_uint128_gpu(start_str);
                record.val = parse_uint128_gpu(metric_str);
                if (section == "max_value_peaks") {
                    max_value_peaks.push_back(record);
                } else if (section == "steps_peaks") {
                    steps_peaks.push_back(record);
                } else if (section == "sigma_peaks") {
                    sigma_peaks.push_back(record);
                }
            }
        }
    }
    
    ifs.close();
    return true;
}

void accumulate_boundary_metrics_vulkan(uint64_t prefix, uint64_t start_64, uint64_t end_64, int width, 
                                        const BaseDependentSuffixes& base_suffixes, GlobalMetricsGpu& metrics) {
    uint64_t base = prefix << width;
    uint64_t mult = (1ULL << width) % 3;
    uint64_t base_mod3 = ((prefix % 3) * mult) % 3;
    
    for (uint32_t suffix : base_suffixes.std_allowed) {
        uint64_t curr = base | suffix;
        if (curr < start_64) continue;
        if (curr > end_64) break;
        
        metrics.total_checked++;
        if ((base_mod3 + suffix) % 3 == 2) {
            metrics.skipped_mod6++;
        }
    }
}

void accumulate_boundary_metrics_vulkan_128(unsigned __int128 prefix, unsigned __int128 start, unsigned __int128 end, int width, 
                                            const BaseDependentSuffixes& base_suffixes, GlobalMetricsGpu& metrics) {
    unsigned __int128 base = prefix << width;
    uint64_t mult = (1ULL << width) % 3;
    uint64_t prefix_mod3 = static_cast<uint64_t>(prefix % 3);
    uint64_t base_mod3 = (prefix_mod3 * mult) % 3;
    
    for (uint32_t suffix : base_suffixes.std_allowed) {
        unsigned __int128 curr = base + suffix;
        if (curr < start) continue;
        if (curr > end) break;
        
        metrics.total_checked++;
        if ((base_mod3 + suffix) % 3 == 2) {
            metrics.skipped_mod6++;
        }
    }
}

void vulkan_search_range_internal(
    unsigned __int128 start_val,
    unsigned __int128 end_val,
    int cutoff_width,
    const BaseDependentSuffixes& base_suffixes,
    uint32_t offset_0, uint32_t size_0,
    uint32_t offset_2, uint32_t size_2,
    uint32_t offset_4, uint32_t size_4,
    VkDevice device,
    VkQueue computeQueue,
    VkCommandBuffer commandBuffer,
    VkPipelineLayout pipelineLayout,
    VkPipeline pipeline,
    VkDescriptorSet descriptorSet,
    const std::vector<VkBuffer>& buffers,
    const std::vector<VkDeviceMemory>& bufferMemories,
    const std::vector<VkDeviceSize>& bufferSizes,
    GlobalPeaksGpu& masterPeaks,
    std::vector<PeakRecordGpu>& masterMaxValPeaks,
    std::vector<PeakRecordGpu>& masterStepsPeaks,
    std::vector<PeakRecordGpu>& masterSigmaPeaks,
    GlobalMetricsGpu& masterMetrics,
    double& mem_transfer_time_ms,
    double& total_kernel_time_ms
) {
    if (start_val > end_val) return;

    // Initialize PeakPredictor
    PeakPredictor predictor;
    for (const auto& peak : masterStepsPeaks) {
        uint128 n(peak.start.low, peak.start.high);
        predictor.add_confirmed_peak(n, peak.val.low);
    }
    predictor.prune_predictions_less_than(uint128(static_cast<uint64_t>(start_val), static_cast<uint64_t>(start_val >> 64)));

    const unsigned __int128 CHUNK_SIZE = 2000000;
    unsigned __int128 current_chunk_start = start_val;

    auto last_report_time = std::chrono::steady_clock::now();
    double report_interval = 3600.0;
    const char* env_interval = std::getenv("HAILSTONE_REPORT_INTERVAL");
    if (env_interval) {
        try {
            report_interval = std::stod(env_interval);
        } catch (...) {}
    }

    while (current_chunk_start <= end_val) {
        unsigned __int128 current_chunk_end = current_chunk_start + CHUNK_SIZE - 1;
        if (current_chunk_end > end_val) {
            current_chunk_end = end_val;
        }

        unsigned __int128 chunk_start_val = current_chunk_start;
        if (cutoff_width == 0 && (chunk_start_val & 1) == 0) {
            chunk_start_val += 1;
        }

        unsigned __int128 chunk_end_val = current_chunk_end;
        if (cutoff_width == 0 && chunk_end_val >= chunk_start_val) {
            if ((chunk_end_val & 1) == 0) {
                chunk_end_val -= 1;
            }
        }

        if (chunk_start_val <= chunk_end_val) {
            // Confirm predictions up to current_chunk_start
            {
                uint128 u128_chunk_start(static_cast<uint64_t>(current_chunk_start), static_cast<uint64_t>(current_chunk_start >> 64));
                predictor.process_up_to_generic(u128_chunk_start, masterStepsPeaks, [](uint128 n, uint32_t steps) {
                    PeakRecordGpu r;
                    r.start.low = n.low;
                    r.start.high = n.high;
                    r.val.low = steps;
                    r.val.high = 0;
                    return r;
                });
                masterPeaks.max_steps = predictor.current_max_steps;
            }

            // Copy masterPeaks to bufferMemories[0] and reset locks/counts/metrics
            {
                auto start_write = std::chrono::high_resolution_clock::now();
                void* data;
                VK_CHECK(vkMapMemory(device, bufferMemories[0], 0, bufferSizes[0], 0, &data));
                std::memcpy(data, &masterPeaks, sizeof(GlobalPeaksGpu));
                vkUnmapMemory(device, bufferMemories[0]);

                VK_CHECK(vkMapMemory(device, bufferMemories[1], 0, bufferSizes[1], 0, &data));
                std::memset(data, 0, bufferSizes[1]);
                vkUnmapMemory(device, bufferMemories[1]);

                VK_CHECK(vkMapMemory(device, bufferMemories[2], 0, bufferSizes[2], 0, &data));
                std::memset(data, 0, bufferSizes[2]);
                vkUnmapMemory(device, bufferMemories[2]);

                VK_CHECK(vkMapMemory(device, bufferMemories[6], 0, bufferSizes[6], 0, &data));
                std::memset(data, 0, bufferSizes[6]);
                vkUnmapMemory(device, bufferMemories[6]);
                auto end_write = std::chrono::high_resolution_clock::now();
                mem_transfer_time_ms += std::chrono::duration<double, std::milli>(end_write - start_write).count();
            }

            // Record commands
            VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

            bool use_64bit = (end_val < (unsigned __int128)0x100000000ULL);

            if (cutoff_width > 0) {
                unsigned __int128 start_prefix = chunk_start_val >> cutoff_width;
                unsigned __int128 end_prefix = chunk_end_val >> cutoff_width;
                uint32_t std_allowed_size = static_cast<uint32_t>(base_suffixes.std_allowed.size());

                if (start_prefix == end_prefix) {
                    // Case 1: Start and end in the same prefix (single boundary)
                    uint64_t m3 = static_cast<uint64_t>(start_prefix % 3);
                    uint64_t base_mod6 = (m3 == 0) ? 0 : ((m3 == 1) ? 4 : 2);
                    uint32_t allowed_offset = (base_mod6 == 0) ? offset_0 : ((base_mod6 == 2) ? offset_2 : offset_4);
                    uint32_t allowed_size = (base_mod6 == 0) ? size_0 : ((base_mod6 == 2) ? size_2 : size_4);

                    if (allowed_size > 0) {
                        uint32_t total_work_items = allowed_size;
                        uint32_t gc = (total_work_items + 255) / 256;

                        PushConstantsGpu pcs{};
                        pcs.start_prefix_low = static_cast<uint64_t>(start_prefix);
                        pcs.start_prefix_high = static_cast<uint64_t>(start_prefix >> 64);
                        pcs.start_val_low = static_cast<uint64_t>(chunk_start_val);
                        pcs.start_val_high = static_cast<uint64_t>(chunk_start_val >> 64);
                        pcs.end_val_low = static_cast<uint64_t>(chunk_end_val);
                        pcs.end_val_high = static_cast<uint64_t>(chunk_end_val >> 64);
                        pcs.allowed_suffixes_size = allowed_size;
                        pcs.cutoff_width = cutoff_width;
                        pcs.total_work_items = total_work_items;
                        pcs.allowed_offset = allowed_offset;
                        pcs.prefix_stride = 1;
                        pcs.check_start = 1;
                        pcs.check_end = 1;
                        pcs.init_max_val_low = masterPeaks.max_val.low;
                        pcs.init_max_val_high = masterPeaks.max_val.high;
                        pcs.init_max_steps = masterPeaks.max_steps;
                        pcs.init_max_sigma = masterPeaks.max_sigma;

                        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsGpu), &pcs);
                        vkCmdDispatch(commandBuffer, gc, 1, 1);
                    }

                    if (use_64bit) {
                        accumulate_boundary_metrics_vulkan(static_cast<uint64_t>(start_prefix), static_cast<uint64_t>(chunk_start_val), static_cast<uint64_t>(chunk_end_val), cutoff_width, base_suffixes, masterMetrics);
                    } else {
                        accumulate_boundary_metrics_vulkan_128(start_prefix, chunk_start_val, chunk_end_val, cutoff_width, base_suffixes, masterMetrics);
                    }
                } else {
                    // Case 2: Start and end prefixes are different
                    bool start_is_boundary = (chunk_start_val > (start_prefix << cutoff_width));
                    if (start_is_boundary) {
                        uint64_t m3 = static_cast<uint64_t>(start_prefix % 3);
                        uint64_t base_mod6 = (m3 == 0) ? 0 : ((m3 == 1) ? 4 : 2);
                        uint32_t allowed_offset = (base_mod6 == 0) ? offset_0 : ((base_mod6 == 2) ? offset_2 : offset_4);
                        uint32_t allowed_size = (base_mod6 == 0) ? size_0 : ((base_mod6 == 2) ? size_2 : size_4);

                    if (allowed_size > 0) {
                        uint32_t total_work_items = allowed_size;
                        uint32_t gc = (total_work_items + 255) / 256;

                        PushConstantsGpu pcs{};
                        pcs.start_prefix_low = static_cast<uint64_t>(start_prefix);
                        pcs.start_prefix_high = static_cast<uint64_t>(start_prefix >> 64);
                        pcs.start_val_low = static_cast<uint64_t>(chunk_start_val);
                        pcs.start_val_high = static_cast<uint64_t>(chunk_start_val >> 64);
                        pcs.end_val_low = static_cast<uint64_t>(chunk_end_val);
                        pcs.end_val_high = static_cast<uint64_t>(chunk_end_val >> 64);
                        pcs.allowed_suffixes_size = allowed_size;
                        pcs.cutoff_width = cutoff_width;
                        pcs.total_work_items = total_work_items;
                        pcs.allowed_offset = allowed_offset;
                        pcs.prefix_stride = 1;
                        pcs.check_start = 1;
                        pcs.check_end = 0;
                        pcs.init_max_val_low = masterPeaks.max_val.low;
                        pcs.init_max_val_high = masterPeaks.max_val.high;
                        pcs.init_max_steps = masterPeaks.max_steps;
                        pcs.init_max_sigma = masterPeaks.max_sigma;

                        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsGpu), &pcs);
                        vkCmdDispatch(commandBuffer, gc, 1, 1);
                    }

                        if (use_64bit) {
                            accumulate_boundary_metrics_vulkan(static_cast<uint64_t>(start_prefix), static_cast<uint64_t>(chunk_start_val), ((static_cast<uint64_t>(start_prefix) + 1) << cutoff_width) - 1, cutoff_width, base_suffixes, masterMetrics);
                        } else {
                            accumulate_boundary_metrics_vulkan_128(start_prefix, chunk_start_val, ((start_prefix + 1) << cutoff_width) - 1, cutoff_width, base_suffixes, masterMetrics);
                        }
                    }

                    bool end_is_boundary = (chunk_end_val < (((end_prefix + 1) << cutoff_width) - 1));
                    if (end_is_boundary) {
                        uint64_t m3 = static_cast<uint64_t>(end_prefix % 3);
                        uint64_t base_mod6 = (m3 == 0) ? 0 : ((m3 == 1) ? 4 : 2);
                        uint32_t allowed_offset = (base_mod6 == 0) ? offset_0 : ((base_mod6 == 2) ? offset_2 : offset_4);
                        uint32_t allowed_size = (base_mod6 == 0) ? size_0 : ((base_mod6 == 2) ? size_2 : size_4);

                    if (allowed_size > 0) {
                        uint32_t total_work_items = allowed_size;
                        uint32_t gc = (total_work_items + 255) / 256;

                        PushConstantsGpu pcs{};
                        pcs.start_prefix_low = static_cast<uint64_t>(end_prefix);
                        pcs.start_prefix_high = static_cast<uint64_t>(end_prefix >> 64);
                        pcs.start_val_low = static_cast<uint64_t>(chunk_start_val);
                        pcs.start_val_high = static_cast<uint64_t>(chunk_start_val >> 64);
                        pcs.end_val_low = static_cast<uint64_t>(chunk_end_val);
                        pcs.end_val_high = static_cast<uint64_t>(chunk_end_val >> 64);
                        pcs.allowed_suffixes_size = allowed_size;
                        pcs.cutoff_width = cutoff_width;
                        pcs.total_work_items = total_work_items;
                        pcs.allowed_offset = allowed_offset;
                        pcs.prefix_stride = 1;
                        pcs.check_start = 0;
                        pcs.check_end = 1;
                        pcs.init_max_val_low = masterPeaks.max_val.low;
                        pcs.init_max_val_high = masterPeaks.max_val.high;
                        pcs.init_max_steps = masterPeaks.max_steps;
                        pcs.init_max_sigma = masterPeaks.max_sigma;

                        VkMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

                        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsGpu), &pcs);
                        vkCmdDispatch(commandBuffer, gc, 1, 1);
                    }

                        if (use_64bit) {
                            accumulate_boundary_metrics_vulkan(static_cast<uint64_t>(end_prefix), static_cast<uint64_t>(end_prefix << cutoff_width), static_cast<uint64_t>(chunk_end_val), cutoff_width, base_suffixes, masterMetrics);
                        } else {
                            accumulate_boundary_metrics_vulkan_128(end_prefix, end_prefix << cutoff_width, chunk_end_val, cutoff_width, base_suffixes, masterMetrics);
                        }
                    }

                    // Intermediate prefixes mod 3 groups
                    unsigned __int128 mid_start_prefix = start_prefix + (start_is_boundary ? 1 : 0);
                    unsigned __int128 mid_end_prefix = end_prefix - (end_is_boundary ? 1 : 0);

                    if (mid_start_prefix <= mid_end_prefix) {
                        uint64_t mult = (1ULL << cutoff_width) % 3;
                        for (int rem_mod3 = 0; rem_mod3 < 3; ++rem_mod3) {
                            unsigned __int128 first_prefix = mid_start_prefix;
                            while (first_prefix <= mid_end_prefix) {
                                uint64_t m3 = static_cast<uint64_t>(first_prefix % 3);
                                if (m3 == rem_mod3) break;
                                first_prefix += 1;
                            }

                            unsigned __int128 last_prefix = mid_end_prefix;
                            while (last_prefix >= first_prefix) {
                                uint64_t m3 = static_cast<uint64_t>(last_prefix % 3);
                                if (m3 == rem_mod3) break;
                                last_prefix -= 1;
                            }

                            if (first_prefix <= last_prefix) {
                                uint64_t diff = static_cast<uint64_t>(last_prefix - first_prefix);
                                uint64_t num_prefixes_group = diff / 3 + 1;

                                uint64_t base_mod6 = (rem_mod3 == 0) ? 0 : ((rem_mod3 == 1) ? 4 : 2);
                                uint32_t allowed_offset = (base_mod6 == 0) ? offset_0 : ((base_mod6 == 2) ? offset_2 : offset_4);
                                uint32_t allowed_size = (base_mod6 == 0) ? size_0 : ((base_mod6 == 2) ? size_2 : size_4);

                                if (allowed_size > 0 && num_prefixes_group > 0) {
                                    uint32_t total_work_items = static_cast<uint32_t>(num_prefixes_group * allowed_size);
                                    uint32_t gc = (total_work_items + 255) / 256;

                                    PushConstantsGpu pcs{};
                                    pcs.start_prefix_low = static_cast<uint64_t>(first_prefix);
                                    pcs.start_prefix_high = static_cast<uint64_t>(first_prefix >> 64);
                                    pcs.start_val_low = static_cast<uint64_t>(chunk_start_val);
                                    pcs.start_val_high = static_cast<uint64_t>(chunk_start_val >> 64);
                                    pcs.end_val_low = static_cast<uint64_t>(chunk_end_val);
                                    pcs.end_val_high = static_cast<uint64_t>(chunk_end_val >> 64);
                                    pcs.allowed_suffixes_size = allowed_size;
                                    pcs.cutoff_width = cutoff_width;
                                    pcs.total_work_items = total_work_items;
                                    pcs.allowed_offset = allowed_offset;
                                    pcs.prefix_stride = 3;
                                    pcs.check_start = 0;
                                    pcs.check_end = 0;
                                    pcs.init_max_val_low = masterPeaks.max_val.low;
                                    pcs.init_max_val_high = masterPeaks.max_val.high;
                                    pcs.init_max_steps = masterPeaks.max_steps;
                                    pcs.init_max_sigma = masterPeaks.max_sigma;

                                    VkMemoryBarrier barrier{};
                                    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                                    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

                                    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsGpu), &pcs);
                                    vkCmdDispatch(commandBuffer, gc, 1, 1);
                                }

                                // Accumulate host metrics
                                uint64_t base_mod3 = (rem_mod3 * mult) % 3;
                                uint32_t std_skipped = (base_mod3 == 0) ? base_suffixes.std_skipped_0 :
                                                       ((base_mod3 == 2) ? base_suffixes.std_skipped_2 : base_suffixes.std_skipped_1);
                                masterMetrics.total_checked += num_prefixes_group * std_allowed_size;
                                masterMetrics.skipped_mod6 += num_prefixes_group * std_skipped;
                            }
                        }
                    }
                }
            } else {
            // cutoff_width == 0 (standard non-suffix-first dispatch)
            unsigned __int128 chunk_odds_128 = (chunk_end_val - chunk_start_val) / 2 + 1;
            uint32_t total_work_items = static_cast<uint32_t>(chunk_odds_128);
            uint32_t gc = (total_work_items + 255) / 256;

            PushConstantsGpu pcs{};
            pcs.start_prefix_low = 0;
            pcs.start_prefix_high = 0;
            pcs.start_val_low = static_cast<uint64_t>(chunk_start_val);
            pcs.start_val_high = static_cast<uint64_t>(chunk_start_val >> 64);
            pcs.end_val_low = static_cast<uint64_t>(chunk_end_val);
            pcs.end_val_high = static_cast<uint64_t>(chunk_end_val >> 64);
            pcs.allowed_suffixes_size = 1;
            pcs.cutoff_width = 0;
            pcs.total_work_items = total_work_items;
            pcs.allowed_offset = 0;
            pcs.prefix_stride = 1;
            pcs.check_start = 0;
            pcs.check_end = 0;
            pcs.init_max_val_low = masterPeaks.max_val.low;
            pcs.init_max_val_high = masterPeaks.max_val.high;
            pcs.init_max_steps = masterPeaks.max_steps;
            pcs.init_max_sigma = masterPeaks.max_sigma;

            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantsGpu), &pcs);
            vkCmdDispatch(commandBuffer, gc, 1, 1);
        }

        VK_CHECK(vkEndCommandBuffer(commandBuffer));

        // Submit work
        auto t_start_chunk = std::chrono::high_resolution_clock::now();

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFence fence;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

        VK_CHECK(vkQueueSubmit(computeQueue, 1, &submitInfo, fence));
        VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

        auto t_end_chunk = std::chrono::high_resolution_clock::now();
        total_kernel_time_ms += std::chrono::duration<double, std::milli>(t_end_chunk - t_start_chunk).count();

        vkDestroyFence(device, fence, nullptr);

        // Read results back
        GlobalMetricsGpu chunkMetrics{};
        PeaksCountGpu chunkCounts{};

        auto start_read = std::chrono::high_resolution_clock::now();
        {
            void* data;
            VK_CHECK(vkMapMemory(device, bufferMemories[6], 0, bufferSizes[6], 0, &data));
            std::memcpy(&chunkMetrics, data, sizeof(GlobalMetricsGpu));
            vkUnmapMemory(device, bufferMemories[6]);

            VK_CHECK(vkMapMemory(device, bufferMemories[2], 0, bufferSizes[2], 0, &data));
            std::memcpy(&chunkCounts, data, sizeof(PeaksCountGpu));
            vkUnmapMemory(device, bufferMemories[2]);

            if (chunkCounts.max_val_count > 0) {
                uint32_t to_copy = std::min(chunkCounts.max_val_count, (uint32_t)MAX_PEAK_RECORDS);
                std::vector<PeakRecordGpu> chunkMaxVal(to_copy);
                VK_CHECK(vkMapMemory(device, bufferMemories[3], 0, bufferSizes[3], 0, &data));
                std::memcpy(chunkMaxVal.data(), data, to_copy * sizeof(PeakRecordGpu));
                vkUnmapMemory(device, bufferMemories[3]);
                masterMaxValPeaks.insert(masterMaxValPeaks.end(), chunkMaxVal.begin(), chunkMaxVal.end());
            }

            if (chunkCounts.steps_count > 0) {
                uint32_t to_copy = std::min(chunkCounts.steps_count, (uint32_t)MAX_PEAK_RECORDS);
                std::vector<PeakRecordGpu> chunkSteps(to_copy);
                VK_CHECK(vkMapMemory(device, bufferMemories[4], 0, bufferSizes[4], 0, &data));
                std::memcpy(chunkSteps.data(), data, to_copy * sizeof(PeakRecordGpu));
                vkUnmapMemory(device, bufferMemories[4]);

                std::sort(chunkSteps.begin(), chunkSteps.end(), [](const PeakRecordGpu& a, const PeakRecordGpu& b) {
                    uint128 start_a(a.start.low, a.start.high);
                    uint128 start_b(b.start.low, b.start.high);
                    return start_a < start_b;
                });

                for (const auto& peak : chunkSteps) {
                    uint128 n(peak.start.low, peak.start.high);
                    predictor.process_up_to_generic(n, masterStepsPeaks, [](uint128 n, uint32_t steps) {
                        PeakRecordGpu r;
                        r.start.low = n.low;
                        r.start.high = n.high;
                        r.val.low = steps;
                        r.val.high = 0;
                        return r;
                    });
                    if (peak.val.low > predictor.current_max_steps) {
                        masterStepsPeaks.push_back(peak);
                        predictor.add_confirmed_peak(n, peak.val.low);
                    }
                }
            }

            if (chunkCounts.sigma_count > 0) {
                uint32_t to_copy = std::min(chunkCounts.sigma_count, (uint32_t)MAX_PEAK_RECORDS);
                std::vector<PeakRecordGpu> chunkSigma(to_copy);
                VK_CHECK(vkMapMemory(device, bufferMemories[5], 0, bufferSizes[5], 0, &data));
                std::memcpy(chunkSigma.data(), data, to_copy * sizeof(PeakRecordGpu));
                vkUnmapMemory(device, bufferMemories[5]);
                masterSigmaPeaks.insert(masterSigmaPeaks.end(), chunkSigma.begin(), chunkSigma.end());
            }
        }
        auto end_read = std::chrono::high_resolution_clock::now();
        mem_transfer_time_ms += std::chrono::duration<double, std::milli>(end_read - start_read).count();

        // Accumulate metrics
        masterMetrics.total_steps += chunkMetrics.total_steps;
        masterMetrics.overflowed += chunkMetrics.overflowed;
        if (cutoff_width == 0) {
            masterMetrics.total_checked += chunkMetrics.total_checked;
            masterMetrics.skipped_mod6 += chunkMetrics.skipped_mod6;
        }

        // Update master peaks based on this chunk's results
        GlobalPeaksGpu chunkPeaks{};
        {
            void* data;
            VK_CHECK(vkMapMemory(device, bufferMemories[0], 0, bufferSizes[0], 0, &data));
            std::memcpy(&chunkPeaks, data, sizeof(GlobalPeaksGpu));
            vkUnmapMemory(device, bufferMemories[0]);
        }

        unsigned __int128 current_master_val = masterPeaks.max_val.high;
        current_master_val = (current_master_val << 64) | masterPeaks.max_val.low;
        unsigned __int128 chunk_max_val = chunkPeaks.max_val.high;
        chunk_max_val = (chunk_max_val << 64) | chunkPeaks.max_val.low;

        if (chunk_max_val > current_master_val) {
            masterPeaks.max_val = chunkPeaks.max_val;
        }
        {
            uint128 u128_chunk_end(static_cast<uint64_t>(current_chunk_end), static_cast<uint64_t>(current_chunk_end >> 64));
            predictor.process_up_to_generic(u128_chunk_end, masterStepsPeaks, [](uint128 n, uint32_t steps) {
                PeakRecordGpu r;
                r.start.low = n.low;
                r.start.high = n.high;
                r.val.low = steps;
                r.val.high = 0;
                return r;
            });
            masterPeaks.max_steps = predictor.current_max_steps;
        }
        if (chunkPeaks.max_sigma > masterPeaks.max_sigma) {
            masterPeaks.max_sigma = chunkPeaks.max_sigma;
        }
    }

    current_chunk_start += CHUNK_SIZE;

    // Time-based progress update
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time).count() >= report_interval) {
        uint64_t current_block = static_cast<uint64_t>(current_chunk_start >> 32);
        uint64_t start_block = static_cast<uint64_t>(start_val >> 32);
        uint64_t blocks_searched = current_block - start_block;
        std::cout << "[Progress Update] Blocks searched: " << blocks_searched
                  << ", Current block: " << current_block << std::endl;
        last_report_time = now;
    }
}

// Confirm any remaining predictions up to end_val
{
    uint128 u128_end(static_cast<uint64_t>(end_val), static_cast<uint64_t>(end_val >> 64));
    predictor.process_up_to_generic(u128_end, masterStepsPeaks, [](uint128 n, uint32_t steps) {
        PeakRecordGpu r;
        r.start.low = n.low;
        r.start.high = n.high;
        r.val.low = steps;
        r.val.high = 0;
        return r;
    });
    masterPeaks.max_steps = predictor.current_max_steps;
}
}

void vulkan_search_block_0(
unsigned __int128 start_val,
unsigned __int128 end_val,
int cutoff_width,
const BaseDependentSuffixes& base_suffixes,
uint32_t offset_0, uint32_t size_0,
uint32_t offset_2, uint32_t size_2,
uint32_t offset_4, uint32_t size_4,
VkDevice device,
VkQueue computeQueue,
VkCommandBuffer commandBuffer,
VkPipelineLayout pipelineLayout,
VkPipeline pipeline,
VkDescriptorSet descriptorSet,
const std::vector<VkBuffer>& buffers,
const std::vector<VkDeviceMemory>& bufferMemories,
const std::vector<VkDeviceSize>& bufferSizes,
GlobalPeaksGpu& masterPeaks,
std::vector<PeakRecordGpu>& masterMaxValPeaks,
std::vector<PeakRecordGpu>& masterStepsPeaks,
std::vector<PeakRecordGpu>& masterSigmaPeaks,
GlobalMetricsGpu& masterMetrics,
double& mem_transfer_time_ms,
double& total_kernel_time_ms
) {
if (end_val >= (unsigned __int128)0x100000000ULL) {
    throw std::invalid_argument("vulkan_search_block_0: range extends beyond block 0");
}
vulkan_search_range_internal(
    start_val, end_val, cutoff_width, base_suffixes,
    offset_0, size_0, offset_2, size_2, offset_4, size_4,
    device, computeQueue, commandBuffer,
    pipelineLayout, pipeline, descriptorSet, buffers, bufferMemories, bufferSizes,
    masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
    masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
);
}

void vulkan_search_blocks_gt_0(
unsigned __int128 start_val,
unsigned __int128 end_val,
int cutoff_width,
const BaseDependentSuffixes& base_suffixes,
uint32_t offset_0, uint32_t size_0,
uint32_t offset_2, uint32_t size_2,
uint32_t offset_4, uint32_t size_4,
VkDevice device,
VkQueue computeQueue,
VkCommandBuffer commandBuffer,
VkPipelineLayout pipelineLayout,
VkPipeline pipeline,
VkDescriptorSet descriptorSet,
const std::vector<VkBuffer>& buffers,
const std::vector<VkDeviceMemory>& bufferMemories,
const std::vector<VkDeviceSize>& bufferSizes,
GlobalPeaksGpu& masterPeaks,
std::vector<PeakRecordGpu>& masterMaxValPeaks,
std::vector<PeakRecordGpu>& masterStepsPeaks,
std::vector<PeakRecordGpu>& masterSigmaPeaks,
GlobalMetricsGpu& masterMetrics,
double& mem_transfer_time_ms,
double& total_kernel_time_ms
) {
if (start_val < (unsigned __int128)0x100000000ULL) {
    throw std::invalid_argument("vulkan_search_blocks_gt_0: range starts below block 1");
}
vulkan_search_range_internal(
    start_val, end_val, cutoff_width, base_suffixes,
    offset_0, size_0, offset_2, size_2, offset_4, size_4,
    device, computeQueue, commandBuffer,
    pipelineLayout, pipeline, descriptorSet, buffers, bufferMemories, bufferSizes,
    masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
    masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
);
}

void print_help() {
    std::cout << "Usage: hailstone_vulkan [options] [positional_start] [positional_end]\n\n"
              << "Options:\n"
              << "  -h, --help                 Show this help message\n"
              << "  --start-num, --start_num VALUE  Starting number of the search range (default: 3)\n"
              << "  --end-num, --end_num VALUE      Ending number of the search range (default: 100000)\n"
              << "  --start-block, --start_block INDEX Starting block index (each block is 2^32 items, overrides start-num)\n"
              << "  --end-block, --end_block INDEX     Ending block index (overrides end-num)\n"
              << "  --num-blocks, --num_blocks COUNT   Number of blocks to check (overrides end-num/end-block)\n"
              << "  --checkpoint, --checkpoint_file FILE Checkpoint file path (default: hailstone.chk)\n"
              << "  --no-checkpoint, --no_checkpoint     Disable saving and restoring checkpoints\n"
              << "  --no-save-checkpoint, --no_save_checkpoint Disable saving checkpoints at the end of search\n"
              << "  --cutoff-width, --cutoff_width VALUE Enable suffix-first search with given bit-width (8, 12, 16, 20, or 24)\n\n"
              << "Note: Positional parameters can still be used as a fallback if no named options are provided.\n";
}

unsigned __int128 block_to_num(uint64_t block) {
    unsigned __int128 res = block;
    return res << 32;
}

int main(int argc, char* argv[]) {
    std::cout << "=== Hailstone Vulkan Compute Search ===" << std::endl;

    uint128_gpu start = {3, 0};
    uint128_gpu end = {100000, 0};

    bool has_start_num = false;
    bool has_end_num = false;
    bool has_start_block = false;
    bool has_end_block = false;
    bool has_num_blocks = false;

    uint128_gpu opt_start_num = {0, 0};
    uint128_gpu opt_end_num = {0, 0};
    uint64_t opt_start_block = 0;
    uint64_t opt_end_block = 0;
    uint64_t opt_num_blocks = 0;

    bool checkpoint_enabled = true;
    bool save_checkpoint_enabled = true;
    std::string checkpoint_file = "hailstone.chk";
    int cutoff_width = 24;

    std::vector<std::string> positional_args;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else if (arg == "--start-block" || arg == "--start_block") {
            if (i + 1 < argc) {
                opt_start_block = std::stoull(argv[++i]);
                has_start_block = true;
            } else {
                std::cerr << "Error: " << arg << " requires an argument." << std::endl;
                return 1;
            }
        } else if (arg == "--end-block" || arg == "--end_block") {
            if (i + 1 < argc) {
                opt_end_block = std::stoull(argv[++i]);
                has_end_block = true;
            } else {
                std::cerr << "Error: " << arg << " requires an argument." << std::endl;
                return 1;
            }
        } else if (arg == "--num-blocks" || arg == "--num_blocks") {
            if (i + 1 < argc) {
                opt_num_blocks = std::stoull(argv[++i]);
                has_num_blocks = true;
            } else {
                std::cerr << "Error: " << arg << " requires an argument." << std::endl;
                return 1;
            }
        } else if (arg == "--start-num" || arg == "--start_num") {
            if (i + 1 < argc) {
                opt_start_num = parse_uint128_gpu(argv[++i]);
                has_start_num = true;
            } else {
                std::cerr << "Error: " << arg << " requires an argument." << std::endl;
                return 1;
            }
        } else if (arg == "--end-num" || arg == "--end_num") {
            if (i + 1 < argc) {
                opt_end_num = parse_uint128_gpu(argv[++i]);
                has_end_num = true;
            } else {
                std::cerr << "Error: " << arg << " requires an argument." << std::endl;
                return 1;
            }
        } else if (arg == "--checkpoint" || arg == "--checkpoint_file") {
            if (i + 1 < argc) {
                checkpoint_file = argv[++i];
                checkpoint_enabled = true;
            } else {
                std::cerr << "Error: " << arg << " requires an argument." << std::endl;
                return 1;
            }
        } else if (arg == "--no-checkpoint" || arg == "--no_checkpoint") {
            checkpoint_enabled = false;
        } else if (arg == "--no-save-checkpoint" || arg == "--no_save_checkpoint") {
            save_checkpoint_enabled = false;
        } else if (arg == "--cutoff-width" || arg == "--cutoff_width") {
            if (i + 1 < argc) {
                cutoff_width = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires an argument." << std::endl;
                return 1;
            }
        } else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_help();
            return 1;
        } else {
            positional_args.push_back(arg);
        }
    }

    if (cutoff_width != 0 && cutoff_width != 8 && cutoff_width != 12 && cutoff_width != 16 && cutoff_width != 20 && cutoff_width != 24) {
        std::cerr << "Error: --cutoff-width must be 8, 12, 16, 20, or 24." << std::endl;
        return 1;
    }

    bool has_range_options = has_start_block || has_start_num || has_end_block || has_end_num || has_num_blocks;
    if (has_range_options && !positional_args.empty()) {
        std::cerr << "Error: Cannot mix named options and positional arguments." << std::endl;
        print_help();
        return 1;
    }

    GlobalMetricsGpu masterMetrics = {0, 0, 0, 0};
    GlobalPeaksGpu masterPeaks = {{0, 0}, 0, 0};
    std::vector<PeakRecordGpu> masterMaxValPeaks;
    std::vector<PeakRecordGpu> masterStepsPeaks;
    std::vector<PeakRecordGpu> masterSigmaPeaks;

    uint128_gpu last_num = {0, 0};
    bool checkpoint_loaded = false;

    if (checkpoint_enabled) {
        if (load_checkpoint(checkpoint_file, last_num, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks, masterPeaks)) {
            checkpoint_loaded = true;
            std::cout << "Loaded checkpoint: " << checkpoint_file << " (last number searched: " << u128_to_string(last_num) << ")" << std::endl;
        }
    }

    // Determine start boundary
    if (has_start_block || has_start_num) {
        if (has_start_num) {
            start = opt_start_num;
        } else {
            unsigned __int128 start_val = block_to_num(opt_start_block);
            if (start_val < 3) {
                start_val = 3;
            }
            start.low = static_cast<uint64_t>(start_val);
            start.high = static_cast<uint64_t>(start_val >> 64);
        }
    } else if (!positional_args.empty()) {
        start = parse_uint128_gpu(positional_args[0]);
    } else if (checkpoint_loaded) {
        unsigned __int128 last_val = last_num.high;
        last_val = (last_val << 64) | last_num.low;
        unsigned __int128 start_val = last_val + 1;
        start.low = static_cast<uint64_t>(start_val);
        start.high = static_cast<uint64_t>(start_val >> 64);
    } else {
        start = {3, 0};
    }

    // Determine end boundary
    if (has_end_num || has_end_block || has_num_blocks) {
        if (has_end_num) {
            end = opt_end_num;
        } else if (has_num_blocks) {
            uint64_t base_block = 0;
            if (has_start_block) {
                base_block = opt_start_block;
            } else if (has_start_num) {
                unsigned __int128 start_val = opt_start_num.high;
                start_val = (start_val << 64) | opt_start_num.low;
                base_block = static_cast<uint64_t>(start_val >> 32);
            } else if (checkpoint_loaded) {
                unsigned __int128 start_val = start.high;
                start_val = (start_val << 64) | start.low;
                base_block = static_cast<uint64_t>(start_val >> 32);
            }
            unsigned __int128 end_val = block_to_num(base_block + opt_num_blocks);
            end.low = static_cast<uint64_t>(end_val);
            end.high = static_cast<uint64_t>(end_val >> 64);
        } else {
            unsigned __int128 end_val = block_to_num(opt_end_block + 1);
            end.low = static_cast<uint64_t>(end_val);
            end.high = static_cast<uint64_t>(end_val >> 64);
        }
    } else if (positional_args.size() > 1) {
        end = parse_uint128_gpu(positional_args[1]);
    } else {
        // Default range size of 100,000 numbers
        unsigned __int128 start_val = start.high;
        start_val = (start_val << 64) | start.low;
        unsigned __int128 end_val = start_val + 99997;
        end.low = static_cast<uint64_t>(end_val);
        end.high = static_cast<uint64_t>(end_val >> 64);
    }

    if (positional_args.size() > 2) {
        std::cerr << "Error: Too many positional arguments." << std::endl;
        print_help();
        return 1;
    }

    std::cout << "Searching range: [" << u128_to_string(start) << ", " << u128_to_string(end) << "]" << std::endl;

    unsigned __int128 start_val = start.high;
    start_val = (start_val << 64) | start.low;
    unsigned __int128 end_val = end.high;
    end_val = (end_val << 64) | end.low;

    if (start_val > end_val) {
        std::cerr << "Error: start > end" << std::endl;
        return 1;
    }

    // Force start to be odd
    if ((start_val & 1) == 0) {
        start_val += 1;
        start.low = static_cast<uint64_t>(start_val);
        start.high = static_cast<uint64_t>(start_val >> 64);
    }

    unsigned __int128 total_range = end_val - start_val + 1;
    uint64_t total_odds = static_cast<uint64_t>((total_range + 1) / 2);

    std::cout << "Total odd starting values to check: " << total_odds << std::endl;

    // 1. Initialize Vulkan
    VkInstance instance;
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Hailstone Vulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance));

    // 2. Select physical device & find compute queue
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "Error: No Vulkan devices found!" << std::endl;
        return 1;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    VkPhysicalDevice physicalDevice = devices[0];
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::cout << "Using GPU: " << deviceProperties.deviceName << std::endl;

    uint32_t queueFamilyIndex = uint32_t(-1);
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            queueFamilyIndex = i;
            break;
        }
    }

    if (queueFamilyIndex == uint32_t(-1)) {
        std::cerr << "Error: No compute queue family found!" << std::endl;
        return 1;
    }

    // 3. Create logical device
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.shaderInt64 = VK_TRUE;

    VkPhysicalDeviceShaderAtomicInt64Features atomicInt64Features{};
    atomicInt64Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_INT64_FEATURES;
    atomicInt64Features.shaderBufferInt64Atomics = VK_TRUE;

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkDevice device;
    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pNext = &atomicInt64Features;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    VK_CHECK(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));

    VkQueue computeQueue;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &computeQueue);

    // 4. Load SPIR-V Shader Module
    std::vector<char> shaderCode;
    try {
        shaderCode = read_file("build/shader.spv");
    } catch (...) {
        try {
            shaderCode = read_file("shader.spv");
        } catch (...) {
            try {
                shaderCode = read_file("gpu_vulkan/shader.spv");
            } catch (...) {
                std::cerr << "Error: shader.spv not found!" << std::endl;
                return 1;
            }
        }
    }

    VkShaderModuleCreateInfo shaderModuleCreateInfo{};
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.codeSize = shaderCode.size();
    shaderModuleCreateInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(device, &shaderModuleCreateInfo, nullptr, &shaderModule));

    // 5. Create buffers
    // Binding indices match shader bindings:
    // Binding 0: GlobalPeaks (24 bytes)
    // Binding 1: LockBuffer (4 bytes)
    // Binding 2: PeaksCount (12 bytes)
    // Binding 3: MaxValuePeaks (MAX_PEAK_RECORDS * sizeof(PeakRecordGpu))
    // Binding 4: StepsPeaks (MAX_PEAK_RECORDS * sizeof(PeakRecordGpu))
    // Binding 5: SigmaPeaks (MAX_PEAK_RECORDS * sizeof(PeakRecordGpu))
    // Binding 6: GlobalMetrics (16 bytes)
    // Binding 7: StepsTable (1024 bytes)
    BaseDependentSuffixes base_suffixes;
    std::vector<uint32_t> allowed_suffixes_packed;
    uint32_t offset_0 = 0, size_0 = 0;
    uint32_t offset_2 = 0, size_2 = 0;
    uint32_t offset_4 = 0, size_4 = 0;

    if (cutoff_width > 0) {
        std::cout << "Using Suffix-First Search with width: " << cutoff_width << std::endl;
        if (cutoff_width == 24) {
            std::cout << "Loading allowed suffixes... " << std::flush;
            base_suffixes = load_allowed_suffixes_24();
        } else {
            std::cout << "Generating allowed suffixes... " << std::flush;
            base_suffixes = generate_base_dependent_suffixes(cutoff_width);
        }
        
        offset_0 = 0;
        size_0 = static_cast<uint32_t>(base_suffixes.allowed_0.size());
        allowed_suffixes_packed.insert(allowed_suffixes_packed.end(), base_suffixes.allowed_0.begin(), base_suffixes.allowed_0.end());

        offset_2 = static_cast<uint32_t>(allowed_suffixes_packed.size());
        size_2 = static_cast<uint32_t>(base_suffixes.allowed_2.size());
        allowed_suffixes_packed.insert(allowed_suffixes_packed.end(), base_suffixes.allowed_2.begin(), base_suffixes.allowed_2.end());

        offset_4 = static_cast<uint32_t>(allowed_suffixes_packed.size());
        size_4 = static_cast<uint32_t>(base_suffixes.allowed_4.size());
        allowed_suffixes_packed.insert(allowed_suffixes_packed.end(), base_suffixes.allowed_4.begin(), base_suffixes.allowed_4.end());

        if (cutoff_width == 24) {
            std::cout << base_suffixes.std_allowed.size() << " std, " 
                      << size_0 << " mod0, " 
                      << size_2 << " mod2, " 
                      << size_4 << " mod4 allowed suffixes loaded." << std::endl;
        } else {
            std::cout << base_suffixes.std_allowed.size() << " std, " 
                      << size_0 << " mod0, " 
                      << size_2 << " mod2, " 
                      << size_4 << " mod4 allowed suffixes generated." << std::endl;
        }
    } else {
        allowed_suffixes_packed = { 0 };
    }
    size_t allowed_suffixes_buf_size = allowed_suffixes_packed.size() * sizeof(uint32_t);

    std::vector<VkDeviceSize> bufferSizes = {
        sizeof(GlobalPeaksGpu),
        4,
        sizeof(PeaksCountGpu),
        MAX_PEAK_RECORDS * sizeof(PeakRecordGpu),
        MAX_PEAK_RECORDS * sizeof(PeakRecordGpu),
        MAX_PEAK_RECORDS * sizeof(PeakRecordGpu),
        sizeof(GlobalMetricsGpu),
        256 * sizeof(uint32_t),
        allowed_suffixes_buf_size,
        256 * sizeof(poly_gpu)
    };

    std::vector<VkBuffer> buffers(10);
    std::vector<VkDeviceMemory> bufferMemories(10);

    for (size_t i = 0; i < 10; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSizes[i];
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &buffers[i]));

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device, buffers[i], &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = find_memory_type(
            physicalDevice, memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        VK_CHECK(vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemories[i]));
        VK_CHECK(vkBindBufferMemory(device, buffers[i], bufferMemories[i], 0));
    }

    // Copy steps table to bufferMemories[7] once at startup
    {
        uint32_t steps_u32[256];
        for (int i = 0; i < 256; ++i) {
            steps_u32[i] = static_cast<uint32_t>(steps8[i]);
        }
        void* data;
        VK_CHECK(vkMapMemory(device, bufferMemories[7], 0, bufferSizes[7], 0, &data));
        std::memcpy(data, steps_u32, bufferSizes[7]);
        vkUnmapMemory(device, bufferMemories[7]);
    }

    // Copy allowed suffixes to bufferMemories[8] once at startup
    {
        void* data;
        VK_CHECK(vkMapMemory(device, bufferMemories[8], 0, bufferSizes[8], 0, &data));
        std::memcpy(data, allowed_suffixes_packed.data(), bufferSizes[8]);
        vkUnmapMemory(device, bufferMemories[8]);
    }

    // Copy fpoly8 table to bufferMemories[9] once at startup
    {
        poly_gpu fpoly_u32[256];
        for (int i = 0; i < 256; ++i) {
            fpoly_u32[i].mul3 = static_cast<uint32_t>(fpoly8[i].mul3);
            fpoly_u32[i].div2 = static_cast<uint32_t>(fpoly8[i].div2);
            fpoly_u32[i].steps = static_cast<uint32_t>(fpoly8[i].steps);
            fpoly_u32[i].add = static_cast<uint32_t>(fpoly8[i].add);
            fpoly_u32[i].smaller = static_cast<uint32_t>(fpoly8[i].smaller);
        }
        void* data;
        VK_CHECK(vkMapMemory(device, bufferMemories[9], 0, bufferSizes[9], 0, &data));
        std::memcpy(data, fpoly_u32, bufferSizes[9]);
        vkUnmapMemory(device, bufferMemories[9]);
    }

    double mem_transfer_time_ms = 0.0;
    double total_kernel_time_ms = 0.0;

    // Master metrics on host (masterPeaks, maxValPeaks, stepsPeaks, sigmaPeaks are defined at start of main)

    // 6. Create descriptor pool & sets
    std::vector<VkDescriptorSetLayoutBinding> bindings(10);
    for (uint32_t i = 0; i < 10; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 10;
    layoutInfo.pBindings = bindings.data();
    VkDescriptorSetLayout descriptorSetLayout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 10;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    VkDescriptorPool descriptorPool;
    VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));

    VkDescriptorSetAllocateInfo allocSetInfo{};
    allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocSetInfo.descriptorPool = descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    VK_CHECK(vkAllocateDescriptorSets(device, &allocSetInfo, &descriptorSet));

    // Update descriptor sets with our buffers
    std::vector<VkDescriptorBufferInfo> bufferInfos(10);
    std::vector<VkWriteDescriptorSet> descriptorWrites(10);
    for (uint32_t i = 0; i < 10; ++i) {
        bufferInfos[i].buffer = buffers[i];
        bufferInfos[i].offset = 0;
        bufferInfos[i].range = bufferSizes[i];

        descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[i].dstSet = descriptorSet;
        descriptorWrites[i].dstBinding = i;
        descriptorWrites[i].dstArrayElement = 0;
        descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        descriptorWrites[i].descriptorCount = 1;
        descriptorWrites[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(device, 10, descriptorWrites.data(), 0, nullptr);

    // 7. Create Pipeline Layout with Push Constants
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantsGpu);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipelineLayout pipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

    // 8. Create Compute Pipelines with Specialization Constants
    struct SpecializationData {
        VkBool32 use_64bit;
        VkBool32 use_suffix_first;
    };

    std::vector<VkSpecializationMapEntry> specEntries(2);
    specEntries[0].constantID = 0;
    specEntries[0].offset = offsetof(SpecializationData, use_64bit);
    specEntries[0].size = sizeof(VkBool32);

    specEntries[1].constantID = 1;
    specEntries[1].offset = offsetof(SpecializationData, use_suffix_first);
    specEntries[1].size = sizeof(VkBool32);

    SpecializationData specData;

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 2;
    specInfo.pMapEntries = specEntries.data();
    specInfo.dataSize = sizeof(SpecializationData);
    specInfo.pData = &specData;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage.pSpecializationInfo = &specInfo;

    // Create 4 pipelines:
    // 1) Block 0 Standard: use_64bit = VK_TRUE, use_suffix_first = VK_FALSE
    specData.use_64bit = VK_TRUE;
    specData.use_suffix_first = VK_FALSE;
    VkPipeline pipeline_block0_std;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_block0_std));

    // 2) Blocks > 0 Standard: use_64bit = VK_FALSE, use_suffix_first = VK_FALSE
    specData.use_64bit = VK_FALSE;
    specData.use_suffix_first = VK_FALSE;
    VkPipeline pipeline_blocks_gt_0_std;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_blocks_gt_0_std));

    // 3) Block 0 Suffix-First: use_64bit = VK_TRUE, use_suffix_first = VK_TRUE
    specData.use_64bit = VK_TRUE;
    specData.use_suffix_first = VK_TRUE;
    VkPipeline pipeline_block0_sf;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_block0_sf));

    // 4) Blocks > 0 Suffix-First: use_64bit = VK_FALSE, use_suffix_first = VK_TRUE
    specData.use_64bit = VK_FALSE;
    specData.use_suffix_first = VK_TRUE;
    VkPipeline pipeline_blocks_gt_0_sf;
    VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_blocks_gt_0_sf));

    // 9. Command Pool & Command Buffer
    VkCommandPoolCreateInfo commandPoolInfo{};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.queueFamilyIndex = queueFamilyIndex;
    VkCommandPool commandPool;
    VK_CHECK(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool));

    VkCommandBufferAllocateInfo cmdBufferAllocInfo{};
    cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferAllocInfo.commandPool = commandPool;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufferAllocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &commandBuffer));

    unsigned __int128 block_boundary = 0x100000000ULL;

    if (cutoff_width > 0) {
        unsigned __int128 threshold_val = (unsigned __int128)1 << cutoff_width;
        if (start_val < threshold_val) {
            unsigned __int128 standard_end = (end_val < threshold_val) ? end_val : (threshold_val - 1);
            std::cout << "Running standard search on boundary range ["
                      << u128_to_string({static_cast<uint64_t>(start_val), static_cast<uint64_t>(start_val >> 64)})
                      << ", "
                      << u128_to_string({static_cast<uint64_t>(standard_end), static_cast<uint64_t>(standard_end >> 64)})
                      << "]" << std::endl;
            vulkan_search_block_0(
                start_val, standard_end, 0, base_suffixes,
                0, 0, 0, 0, 0, 0,
                device, computeQueue, commandBuffer,
                pipelineLayout, pipeline_block0_std, descriptorSet, buffers, bufferMemories, bufferSizes,
                masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
                masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
            );
            start_val = standard_end + 1;
        }

        if (start_val <= end_val) {
            if (start_val < block_boundary) {
                unsigned __int128 block_0_end = end_val;
                if (end_val >= block_boundary) {
                    block_0_end = block_boundary - 1;
                }
                vulkan_search_block_0(
                    start_val, block_0_end, cutoff_width, base_suffixes,
                    offset_0, size_0, offset_2, size_2, offset_4, size_4,
                    device, computeQueue, commandBuffer,
                    pipelineLayout, pipeline_block0_sf, descriptorSet, buffers, bufferMemories, bufferSizes,
                    masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
                    masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
                );
                if (end_val >= block_boundary) {
                    vulkan_search_blocks_gt_0(
                        block_boundary, end_val, cutoff_width, base_suffixes,
                        offset_0, size_0, offset_2, size_2, offset_4, size_4,
                        device, computeQueue, commandBuffer,
                        pipelineLayout, pipeline_blocks_gt_0_sf, descriptorSet, buffers, bufferMemories, bufferSizes,
                        masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
                        masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
                    );
                }
            } else {
                vulkan_search_blocks_gt_0(
                    start_val, end_val, cutoff_width, base_suffixes,
                    offset_0, size_0, offset_2, size_2, offset_4, size_4,
                    device, computeQueue, commandBuffer,
                    pipelineLayout, pipeline_blocks_gt_0_sf, descriptorSet, buffers, bufferMemories, bufferSizes,
                    masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
                    masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
                );
            }
        }
    } else {
        // Cutoff width = 0 (Standard Search)
        if (start_val < block_boundary) {
            unsigned __int128 block_0_end = end_val;
            if (end_val >= block_boundary) {
                block_0_end = block_boundary - 1;
            }
            vulkan_search_block_0(
                start_val, block_0_end, 0, base_suffixes,
                0, 0, 0, 0, 0, 0,
                device, computeQueue, commandBuffer,
                pipelineLayout, pipeline_block0_std, descriptorSet, buffers, bufferMemories, bufferSizes,
                masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
                masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
            );
            if (end_val >= block_boundary) {
                vulkan_search_blocks_gt_0(
                    block_boundary, end_val, 0, base_suffixes,
                    0, 0, 0, 0, 0, 0,
                    device, computeQueue, commandBuffer,
                    pipelineLayout, pipeline_blocks_gt_0_std, descriptorSet, buffers, bufferMemories, bufferSizes,
                    masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
                    masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
                );
            }
        } else {
            vulkan_search_blocks_gt_0(
                start_val, end_val, 0, base_suffixes,
                0, 0, 0, 0, 0, 0,
                device, computeQueue, commandBuffer,
                pipelineLayout, pipeline_blocks_gt_0_std, descriptorSet, buffers, bufferMemories, bufferSizes,
                masterPeaks, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks,
                masterMetrics, mem_transfer_time_ms, total_kernel_time_ms
            );
        }
    }

    std::cout << "\n=== Vulkan Search Completed ===" << std::endl;
    std::cout << "Memory Transfer Time: " << std::fixed << std::setprecision(2) << mem_transfer_time_ms << " ms" << std::endl;
    std::cout << "Kernel Execution Time: " << std::fixed << std::setprecision(2) << total_kernel_time_ms << " ms" << std::endl;
    std::cout << "Global Max Val Peak: " << u128_to_string(masterPeaks.max_val) << std::endl;
    std::cout << "Global Max Steps Peak: " << masterPeaks.max_steps << std::endl;
    std::cout << "Global Max Sigma Peak: " << masterPeaks.max_sigma << std::endl;
    std::cout << "Numbers Checked: " << masterMetrics.total_checked << std::endl;
    std::cout << "Steps Computed: " << masterMetrics.total_steps << std::endl;
    std::cout << "Average Steps: " << (masterMetrics.total_checked > 0 ? (double)masterMetrics.total_steps / masterMetrics.total_checked : 0.0) << std::endl;
    std::cout << "Skipped (Even): " << total_odds << " (implicitly skipped)" << std::endl;
    std::cout << "Skipped (Mod 6): " << masterMetrics.skipped_mod6 << std::endl;
    std::cout << "Overflowed (> 2^128): " << masterMetrics.overflowed << std::endl;

    double m_ips = (masterMetrics.total_checked / 1000000.0) / (total_kernel_time_ms / 1000.0);
    std::cout << "Throughput: " << std::fixed << std::setprecision(2) << m_ips << " M numbers/s" << std::endl;

    std::cout << "\n=== Peaks Found (Vulkan) ===" << std::endl;
    filter_and_print_peaks("Max Value Peaks", masterMaxValPeaks, masterMaxValPeaks.size());
    filter_and_print_peaks("Steps Peaks", masterStepsPeaks, masterStepsPeaks.size());
    filter_and_print_peaks("Stopping Time (sigma) Peaks", masterSigmaPeaks, masterSigmaPeaks.size());

    // Print future predictions at the end of the search
    PeakPredictor final_predictor;
    for (const auto &peak : masterStepsPeaks) {
        uint128 n(peak.start.low, peak.start.high);
        final_predictor.add_confirmed_peak(n, peak.val.low);
    }
    final_predictor.print_future_predictions_by_block();

    // 13. Clean up Vulkan
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyPipeline(device, pipeline_block0_std, nullptr);
    vkDestroyPipeline(device, pipeline_blocks_gt_0_std, nullptr);
    vkDestroyPipeline(device, pipeline_block0_sf, nullptr);
    vkDestroyPipeline(device, pipeline_blocks_gt_0_sf, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyShaderModule(device, shaderModule, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

    for (size_t i = 0; i < 10; ++i) {
        vkDestroyBuffer(device, buffers[i], nullptr);
        vkFreeMemory(device, bufferMemories[i], nullptr);
    }

    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    if (checkpoint_enabled && save_checkpoint_enabled) {
        if (save_checkpoint(checkpoint_file, end, masterMaxValPeaks, masterStepsPeaks, masterSigmaPeaks, masterPeaks)) {
            std::cout << "Saved checkpoint: " << checkpoint_file << std::endl;
        }
    }

    return 0;
}
