#include <iostream>
#include <vector>
#include <map>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

struct BaseDependentSuffixes {
    std::vector<uint32_t> std_allowed;
    std::vector<uint32_t> allowed_0;
    std::vector<uint32_t> allowed_2;
    std::vector<uint32_t> allowed_4;
    uint32_t std_skipped_0 = 0;
    uint32_t std_skipped_1 = 0;
    uint32_t std_skipped_2 = 0;
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

    for (uint32_t s : res.std_allowed) {
        if (s % 3 == 2) res.std_skipped_0++;
        if ((1 + s) % 3 == 2) res.std_skipped_1++;
        if ((2 + s) % 3 == 2) res.std_skipped_2++;
    }

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
    
    uint32_t header[7];
    header[0] = static_cast<uint32_t>(res.std_allowed.size());
    header[1] = static_cast<uint32_t>(res.allowed_0.size());
    header[2] = static_cast<uint32_t>(res.allowed_2.size());
    header[3] = static_cast<uint32_t>(res.allowed_4.size());
    header[4] = res.std_skipped_0;
    header[5] = res.std_skipped_1;
    header[6] = res.std_skipped_2;
    
    if (fwrite(header, sizeof(uint32_t), 7, fp) != 7) {
        std::cerr << "Error: Failed to write header to " << out_path << "\n";
        fclose(fp);
        return 1;
    }
    
    if (fwrite(res.std_allowed.data(), sizeof(uint32_t), res.std_allowed.size(), fp) != res.std_allowed.size() ||
        fwrite(res.allowed_0.data(), sizeof(uint32_t), res.allowed_0.size(), fp) != res.allowed_0.size() ||
        fwrite(res.allowed_2.data(), sizeof(uint32_t), res.allowed_2.size(), fp) != res.allowed_2.size() ||
        fwrite(res.allowed_4.data(), sizeof(uint32_t), res.allowed_4.size(), fp) != res.allowed_4.size()) {
        std::cerr << "Error: Failed to write suffix data arrays to " << out_path << "\n";
        fclose(fp);
        return 1;
    }
    
    fclose(fp);
    std::cout << "Successfully wrote " << out_path << " (" 
              << (7 + res.std_allowed.size() + res.allowed_0.size() + res.allowed_2.size() + res.allowed_4.size()) * sizeof(uint32_t) 
              << " bytes)." << std::endl;
              
    return 0;
}
