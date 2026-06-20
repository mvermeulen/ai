#include <iostream>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

struct BaseDependentSuffixes {
    std::vector<uint32_t> std_allowed;
    std::vector<uint32_t> allowed_tables[9];
    uint32_t std_skipped[9] = {0};
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
        suffix_polys[i] = PolyKey{pow2, pow3, bits};
    }

    std::map<PolyKey, ClassInfo> classes;
    for (int i = 0; i < total_suffixes; ++i) {
        const auto& key = suffix_polys[i];
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
    
    for (int i = 0; i < total_suffixes; ++i) {
        const auto& key = suffix_polys[i];
        const auto& info = classes[key];
        if (info.first_suffix == i && !info.has_even) {
            res.std_allowed.push_back(i);
        }
    }

    // Precompute skipped counts for std_allowed boundary checking
    for (uint32_t s : res.std_allowed) {
        for (int B = 0; B < 9; ++B) {
            uint32_t rem = (B + s) % 9;
            if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
                res.std_skipped[B]++;
            }
        }
    }

    for (const auto& pair : classes) {
        const auto& info = pair.second;
        if (info.has_even) continue;
        
        uint32_t r1 = info.first_suffix;
        
        // For each base B % 9, check if any member of the class lands on a skipped residue
        for (int B = 0; B < 9; ++B) {
            bool has_skipped = false;
            for (uint32_t m : info.members) {
                uint32_t rem = (B + m) % 9;
                if (rem == 2 || rem == 4 || rem == 5 || rem == 8) {
                    has_skipped = true;
                    break;
                }
            }
            if (!has_skipped) {
                res.allowed_tables[B].push_back(r1);
            }
        }
    }
    
    return res;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output_binary_path>\n";
        return 1;
    }
    std::string out_path = argv[1];
    
    int width = 24;
    std::cout << "Precomputing allowed suffixes for width " << width << "... " << std::endl;
    
    auto start = std::chrono::high_resolution_clock::now();
    auto res = generate_base_dependent_suffixes(width);
    auto end = std::chrono::high_resolution_clock::now();
    
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "Finished simulation in " << elapsed << " seconds." << std::endl;
    
    FILE* fp = fopen(out_path.c_str(), "wb");
    if (!fp) {
        std::cerr << "Error: Could not open output file " << out_path << " for writing.\n";
        return 1;
    }
    
    // Header format: [std_count, count_B0..B8, std_skipped_B0..B8] -> total 19 uint32_t
    uint32_t header[19];
    header[0] = static_cast<uint32_t>(res.std_allowed.size());
    for (int i = 0; i < 9; ++i) {
        header[1 + i] = static_cast<uint32_t>(res.allowed_tables[i].size());
        header[10 + i] = res.std_skipped[i];
    }
    
    if (fwrite(header, sizeof(uint32_t), 19, fp) != 19) {
        std::cerr << "Error: Failed to write header to " << out_path << "\n";
        fclose(fp);
        return 1;
    }
    
    if (fwrite(res.std_allowed.data(), sizeof(uint32_t), res.std_allowed.size(), fp) != res.std_allowed.size()) {
        std::cerr << "Error: Failed to write std_allowed array to " << out_path << "\n";
        fclose(fp);
        return 1;
    }
    
    uint64_t total_written = 19 + res.std_allowed.size();
    for (int i = 0; i < 9; ++i) {
        size_t size = res.allowed_tables[i].size();
        if (size > 0) {
            if (fwrite(res.allowed_tables[i].data(), sizeof(uint32_t), size, fp) != size) {
                std::cerr << "Error: Failed to write allowed_tables[" << i << "] array to " << out_path << "\n";
                fclose(fp);
                return 1;
            }
            total_written += size;
        }
    }
    
    fclose(fp);
    std::cout << "Successfully wrote " << out_path << " (" 
              << total_written * sizeof(uint32_t) 
              << " bytes)." << std::endl;
              
    return 0;
}
