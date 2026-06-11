#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

// Represents a Collatz residue class polynomial of the form: (3^pow3 * X) / 2^pow2 + add
struct Polynomial {
    uint32_t pow2 = 0;   // Number of divisions by 2 (exponent of 2 in divisor)
    uint32_t pow3 = 0;   // Number of multiplications by 3 (exponent of 3 in multiplier)
    uint64_t add = 0;    // Additive constant term accumulated
    bool max = true;     // Tracks if the intermediate value ratio 3^pow3 / 2^pow2 has remained >= 1.0
};

// Metadata for unique polynomials stored in the table
struct PolyInfo {
    Polynomial p;
    // Track if any residue class index mapping to this polynomial satisfies: index % 6 == R
    // This is used for input pre-filtering, as starting numbers must be odd (congruent to 1, 3, or 5 mod 6)
    bool mod6_1 = false;
    bool mod6_3 = false;
    bool mod6_5 = false;
};

// Global powers of 2 and 3 for fast lookup
std::vector<uint64_t> powers_of_2(64);
std::vector<uint64_t> powers_of_3(40);

void init_powers() {
    uint64_t pow2 = 1;
    uint64_t pow3 = 1;
    for (int i = 0; i < 64; ++i) {
        powers_of_2[i] = pow2;
        pow2 *= 2;
    }
    for (int i = 0; i < 40; ++i) {
        powers_of_3[i] = pow3;
        pow3 *= 3;
    }
}

// Simplified hailstone to calculate termination steps
int hailsteps(uint64_t n) {
    int steps = 0;
    while (n > 1) {
        if (n & 1) {
            n = n * 3 + 1;
            steps++;
        }
        n >>= 1;
        steps++;
    }
    return steps;
}

// Print polynomial representation matching the original C format
void print_poly(const Polynomial& p) {
    if (p.pow3 != 0) {
        printf("%llu", static_cast<unsigned long long>(powers_of_3[p.pow3]));
    }
    if (p.pow2 == 0) {
        printf("*X");
    } else {
        printf("[X/%llu]", static_cast<unsigned long long>(powers_of_2[p.pow2]));
    }
    if (p.add != 0) {
        printf(" + %llu", static_cast<unsigned long long>(p.add));
    }
}

class PolynomialTable {
public:
    // Lookup polynomial, inserting if not present.
    // Returns index in the table, sets is_new to true if newly inserted.
    int lookup_and_insert(const Polynomial& p, bool& is_new) {
        PolynomialKey key{p.pow2, p.pow3, p.add};
        auto it = key_to_index.find(key);
        if (it != key_to_index.end()) {
            is_new = false;
            return it->second;
        }

        is_new = true;
        int index = static_cast<int>(polytable.size());
        PolyInfo info;
        info.p = p;
        polytable.push_back(info);
        key_to_index[key] = index;
        return index;
    }

    PolyInfo& get(int index) {
        return polytable[index];
    }

    const PolyInfo& get(int index) const {
        return polytable[index];
    }

    size_t size() const {
        return polytable.size();
    }

private:
    struct PolynomialKey {
        uint32_t pow2;
        uint32_t pow3;
        uint64_t add;

        bool operator<(const PolynomialKey& other) const {
            if (pow2 != other.pow2) return pow2 < other.pow2;
            if (pow3 != other.pow3) return pow3 < other.pow3;
            return add < other.add;
        }
    };

    std::vector<PolyInfo> polytable;
    std::map<PolynomialKey, int> key_to_index;
};

// Compute final and maximum ratio polynomials for a starting residue class representation
void compute_poly(uint64_t bits, int width, Polynomial& final_poly, Polynomial& max_poly, bool verbose) {
    int power3 = 0;
    int power2 = 0;
    double max_ratio = 1.0;

    max_poly.pow2 = 0;
    max_poly.pow3 = 0;
    max_poly.add = 0;
    max_poly.max = true;

    if (verbose) {
        printf("poly(%llu,%d)\n", static_cast<unsigned long long>(bits), width);
    }

    Polynomial partial;

    while (width > 0) {
        if (static_cast<double>(powers_of_3[power3]) / powers_of_2[power2] < 1.0) {
            max_poly.max = false;
        }
        bool max_found = false;
        if (bits & 1) {
            bits = bits * 3 + 1;
            power3++;
            if (powers_of_3[power3] > powers_of_2[power2]) {
                double ratio = static_cast<double>(powers_of_3[power3]) / powers_of_2[power2];
                if (ratio > max_ratio) {
                    max_found = true;
                    max_poly.pow3 = power3;
                    max_poly.pow2 = power2;
                    max_poly.add = bits;
                    max_ratio = ratio;
                }
            }
        } else {
            bits >>= 1;
            width--;
            power2++;
        }
        partial.pow2 = power2;
        partial.pow3 = power3;
        partial.add = bits;

        if (verbose) {
            printf("\t");
            print_poly(partial);
            if (max_found) {
                printf(" <max>");
            }
            printf("(%d)\n", max_poly.max ? 1 : 0);
        }
    }

    if (static_cast<double>(powers_of_3[power3]) / powers_of_2[power2] < 1.0) {
        max_poly.max = false;
    }

    final_poly.pow2 = power2;
    final_poly.pow3 = power3;
    final_poly.add = bits;
}

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  -w, --width VALUE   Set the bit width (1 to 20, default: 8)\n"
              << "  -c, --cutoff        Generate cutoff table\n"
              << "  -f, --fpoly         Generate fpoly table\n"
              << "  -m, --mpoly         Generate mpoly table\n"
              << "  -t, --steps         Generate step count table\n"
              << "  -v, --verbose       Enable verbose debugging output\n"
              << "  -h, --help          Show this help message\n";
}

int main(int argc, char* argv[]) {
    int width = 8;
    bool cflag = false;
    bool fflag = false;
    bool mflag = false;
    bool tflag = false;
    bool vflag = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-w" || arg == "--width") {
            if (i + 1 < argc) {
                width = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a value.\n";
                return 1;
            }
        } else if (arg == "-c" || arg == "--cutoff") {
            cflag = true;
        } else if (arg == "-f" || arg == "--fpoly") {
            fflag = true;
        } else if (arg == "-m" || arg == "--mpoly") {
            mflag = true;
        } else if (arg == "-t" || arg == "--steps") {
            tflag = true;
        } else if (arg == "-v" || arg == "--verbose") {
            vflag = true;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_help(argv[0]);
            return 1;
        }
    }

    if (width < 1 || width > 20) {
        std::cerr << "Error: width must be between 1 and 20 (got " << width << ").\n";
        return 1;
    }

    init_powers();

    if (tflag) {
        printf("int steps%d[] = {\n", width);
        for (int i = 0; i < (1 << width); ++i) {
            printf("%d, /* %d */\n", hailsteps(i), i);
        }
        printf("};\n");
        return 0;
    }

    PolynomialTable table;
    std::vector<int> final_idx(1 << width); // Indices of fpoly (final states after width divisions by 2)
    std::vector<int> max_idx(1 << width);   // Indices of mpoly (peak states maximizing 3^a / 2^b ratio)
    std::vector<bool> dup_idx(1 << width);  // Tracks if the final polynomial is a duplicate

    if (vflag) {
        printf("/* polynomial table\n");
    }

    for (int i = 0; i < (1 << width); ++i) {
        Polynomial final_poly, max_poly;
        // Compute both the final state and intermediate max-ratio state
        compute_poly(i, width, final_poly, max_poly, vflag);

        bool is_new = false;
        int idx = table.lookup_and_insert(final_poly, is_new);
        dup_idx[i] = !is_new;
        final_idx[i] = idx;

        // Record mod6 residue compatibility for starting values congruent to i (mod 2^width)
        switch (i % 6) {
            case 1: table.get(idx).mod6_1 = true; break;
            case 3: table.get(idx).mod6_3 = true; break;
            case 5: table.get(idx).mod6_5 = true; break;
        }

        // Record the max ratio polynomial in the unique table
        bool is_new_max = false;
        max_idx[i] = table.lookup_and_insert(max_poly, is_new_max);
    }

    if (vflag) {
        printf("*/\n");
    }

    if (cflag) {
        printf("unsigned char cutoff%d[] = {\n", width);
    } else if (fflag) {
        // fpoly represents the final polynomial class after width divisions by 2
        printf("struct poly { unsigned int mul3; unsigned short div2; unsigned short steps; unsigned int add; unsigned char smaller; } fpoly%d[] = {\n", width);
    } else if (mflag) {
        // mpoly represents the intermediate maximum ratio polynomial class
        printf("struct poly { unsigned int mul3; unsigned short div2; unsigned short steps; unsigned int add; unsigned char smaller; } mpoly%d[] = {\n", width);
    }

    for (int i = 0; i < (1 << width); ++i) {
        int f_idx = final_idx[i];
        int m_idx = max_idx[i];

        if (cflag) {
            unsigned int mask = 0;
            if (dup_idx[i]) mask |= 0x80;
            if (!table.get(m_idx).p.max) mask |= 0x40;
            if (table.get(f_idx).mod6_1) mask |= 0x4;
            if (table.get(f_idx).mod6_3) mask |= 0x2;
            if (table.get(f_idx).mod6_5) mask |= 0x1;

            printf("%#02x,\t/* %d%s%s", mask, i, (dup_idx[i] ? " dup" : ""), (table.get(m_idx).p.max ? "" : " nomax"));
            printf(" ");
            print_poly(table.get(f_idx).p);
            printf(" */\n");
        } else if (fflag) {
            // Print the final polynomial (fpoly) details
            const auto& p = table.get(f_idx).p;
            printf("{ %u, %hu, %hu, %llu, %d }, /* %d: ",
                   static_cast<unsigned int>(powers_of_3[p.pow3]),
                   static_cast<unsigned short>(p.pow2),
                   static_cast<unsigned short>(p.pow3 + p.pow2),
                   static_cast<unsigned long long>(p.add),
                   powers_of_2[p.pow2] > powers_of_3[p.pow3] ? 1 : 0,
                   i);
            print_poly(p);
            printf(" */\n");
        } else if (mflag) {
            // Print the maximum ratio polynomial (mpoly) details
            const auto& final_p = table.get(f_idx).p;
            const auto& max_p = table.get(m_idx).p;
            // If the maximum ratio occurred at the start (pow2 == 0), fallback to fpoly
            if (max_p.pow2 == 0) {
                printf("{ %u, %hu, %hu, %llu, %d }, /* %d: ",
                       static_cast<unsigned int>(powers_of_3[final_p.pow3]),
                       static_cast<unsigned short>(final_p.pow2),
                       static_cast<unsigned short>(final_p.pow3 + final_p.pow2),
                       static_cast<unsigned long long>(final_p.add),
                       powers_of_2[final_p.pow2] > powers_of_3[final_p.pow3] ? 1 : 0,
                       i);
                print_poly(final_p);
            } else {
                printf("{ %u, %hu, %hu, %llu, %d }, /* %d: ",
                       static_cast<unsigned int>(powers_of_3[max_p.pow3]),
                       static_cast<unsigned short>(max_p.pow2),
                       static_cast<unsigned short>(max_p.pow3 + max_p.pow2),
                       static_cast<unsigned long long>(max_p.add),
                       powers_of_2[max_p.pow2] > powers_of_3[max_p.pow3] ? 1 : 0,
                       i);
                print_poly(max_p);
            }
            printf(" */\n");
        }
    }

    if (cflag || fflag || mflag) {
        printf("};\n");
    }

    return 0;
}
