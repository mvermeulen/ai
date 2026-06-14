#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdint>
#include "uint128.h"

// Convert uint128 to a decimal string representation
std::string to_string(uint128 n) {
    if (n == uint128(0))
        return "0";
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

// Pre-calculates the trajectory to find the peak value and stopping time value
bool compute_trajectory(uint64_t start_64, uint128& peak_val, uint64_t& steps, uint64_t& stopping_time, uint128& stopping_time_val, bool& has_stopping) {
    uint128 start(start_64);
    uint128 x = start;
    peak_val = start;
    has_stopping = false;
    stopping_time = 0;
    stopping_time_val = uint128(0);
    steps = 0;
    uint64_t t_steps = 0;

    while (x > uint128(1)) {
        if ((x.low & 1) != 0) {
            bool overflow = false;
            uint128 next_x = mul3_add1(x, overflow);
            if (overflow) {
                return false; // Overflow
            }
            steps++;
            if (next_x > peak_val) {
                peak_val = next_x;
            }

            x = shift_right(next_x, 1);
            steps++;
            t_steps++;

            if (!has_stopping && x < start) {
                stopping_time = t_steps;
                stopping_time_val = x;
                has_stopping = true;
            }
        } else {
            x = shift_right(x, 1);
            steps++;
            t_steps++;

            if (!has_stopping && x < start) {
                stopping_time = t_steps;
                stopping_time_val = x;
                has_stopping = true;
            }
        }
    }
    return true;
}

// Generates the string representation of the trajectory path
std::string generate_path(uint64_t start_64, uint128 peak_val, uint128 stopping_time_val, bool has_stopping) {
    std::string path;
    uint128 start(start_64);
    uint128 x = start;
    bool peak_marked = false;
    bool stopping_marked = false;

    // Check if starting value is the peak
    if (x == peak_val) {
        path += '^';
        peak_marked = true;
    }

    while (x > uint128(2)) {
        if ((x.low & 1) != 0) {
            bool overflow = false;
            uint128 temp = mul3_add1(x, overflow); // Safe because compute_trajectory passed successfully
            if (temp == peak_val) {
                path += "*^";
                peak_marked = true;
                x = temp;
            } else {
                x = shift_right(temp, 1);
                path += '*';
                // Check if the divided result reached stopping time or peak
                if (has_stopping && !stopping_marked && x == stopping_time_val) {
                    path += '|';
                    stopping_marked = true;
                }
                if (!peak_marked && x == peak_val) {
                    path += '^';
                    peak_marked = true;
                }
            }
        } else {
            x = shift_right(x, 1);
            path += '/';
            // Check if this division reached stopping time or peak
            if (has_stopping && !stopping_marked && x == stopping_time_val) {
                path += '|';
                stopping_marked = true;
            }
            if (!peak_marked && x == peak_val) {
                path += '^';
                peak_marked = true;
            }
        }
    }
    return path;
}

// Prints each step incrementally in verbose mode
void print_verbose(uint64_t start_64, uint128 peak_val, uint128 stopping_time_val, bool has_stopping) {
    uint128 start(start_64);
    uint128 x = start;
    bool peak_marked = false;
    bool stopping_marked = false;

    // Check if starting value is the peak
    if (x == peak_val) {
        std::cout << "^ " << to_string(x) << std::endl;
        peak_marked = true;
    }

    while (x > uint128(2)) {
        if ((x.low & 1) != 0) {
            bool overflow = false;
            uint128 temp = mul3_add1(x, overflow); // Safe because compute_trajectory passed successfully
            if (temp == peak_val) {
                std::cout << "* " << to_string(temp) << std::endl;
                std::cout << "^ " << to_string(temp) << std::endl;
                peak_marked = true;
                x = temp;
            } else {
                x = shift_right(temp, 1);
                std::cout << "* " << to_string(x) << std::endl;
                // Check if the divided result reached stopping time or peak
                if (has_stopping && !stopping_marked && x == stopping_time_val) {
                    std::cout << "| " << to_string(x) << std::endl;
                    stopping_marked = true;
                }
                if (!peak_marked && x == peak_val) {
                    std::cout << "^ " << to_string(x) << std::endl;
                    peak_marked = true;
                }
            }
        } else {
            x = shift_right(x, 1);
            std::cout << "/ " << to_string(x) << std::endl;
            // Check if this division reached stopping time or peak
            if (has_stopping && !stopping_marked && x == stopping_time_val) {
                std::cout << "| " << to_string(x) << std::endl;
                stopping_marked = true;
            }
            if (!peak_marked && x == peak_val) {
                std::cout << "^ " << to_string(x) << std::endl;
                peak_marked = true;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    bool show_stats = false;
    bool verbose = false;
    std::string num_str = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-s" || arg == "--statistics") {
            show_stats = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else {
            if (num_str.empty()) {
                num_str = arg;
            } else {
                std::cerr << "Error: Too many arguments or invalid argument '" << arg << "'" << std::endl;
                return 1;
            }
        }
    }

    if (num_str.empty()) {
        std::cerr << "Usage: " << argv[0] << " <number> [options]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  -s, --statistics   Print summary statistics (steps, stopping time, max value)" << std::endl;
        std::cerr << "  -v, --verbose      Print each step incrementally" << std::endl;
        return 1;
    }

    uint64_t start = 0;
    try {
        size_t idx;
        unsigned long long val = std::stoull(num_str, &idx);
        if (idx < num_str.size()) {
            throw std::invalid_argument("Extra characters in input");
        }
        if (val == 0) {
            throw std::invalid_argument("Starting value must be greater than 0");
        }
        start = static_cast<uint64_t>(val);
    } catch (const std::exception& e) {
        std::cerr << "Error: Invalid input number '" << num_str << "'. Must be a 64-bit positive integer." << std::endl;
        return 1;
    }

    uint128 peak_val;
    uint128 stopping_time_val;
    uint64_t steps = 0;
    uint64_t stopping_time = 0;
    bool has_stopping = false;

    if (!compute_trajectory(start, peak_val, steps, stopping_time, stopping_time_val, has_stopping)) {
        std::cerr << "Error: Overflow detected during trajectory computation for " << start << "." << std::endl;
        return 1;
    }

    if (verbose) {
        print_verbose(start, peak_val, stopping_time_val, has_stopping);
    } else {
        std::string path = generate_path(start, peak_val, stopping_time_val, has_stopping);
        std::cout << path << std::endl;
    }

    if (show_stats) {
        std::cout << "Steps: " << steps << std::endl;
        if (has_stopping) {
            std::cout << "Stopping Time: " << stopping_time << std::endl;
        } else {
            std::cout << "Stopping Time: N/A" << std::endl;
        }
        std::cout << "Max Value: " << to_string(peak_val) << std::endl;
    }

    return 0;
}
