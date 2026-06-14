#include "cpu_search.h"
#include "peak_predictor.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#ifdef _OPENMP
#include <omp.h>
#else
inline int omp_get_max_threads() { return 1; }
inline int omp_get_num_threads() { return 1; }
#endif

uint128 parse_uint128(const std::string &str) {
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
    if (c >= '0' && c <= '9')
      val = c - '0';
    else if (hex && c >= 'a' && c <= 'f')
      val = c - 'a' + 10;
    else if (hex && c >= 'A' && c <= 'F')
      val = c - 'A' + 10;
    else
      break;

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

bool save_checkpoint(const std::string &filename, uint128 last_num,
                     const std::vector<PeakRecord> &max_value_peaks,
                     const std::vector<PeakRecord> &steps_peaks,
                     const std::vector<PeakRecord> &sigma_peaks,
                     const PeakState &global_peaks) {
  std::ofstream ofs(filename);
  if (!ofs.is_open()) {
    std::cerr << "Warning: Could not open checkpoint file " << filename
              << " for writing." << std::endl;
    return false;
  }
  ofs << "last_num: " << to_string(last_num) << "\n";
  ofs << "max_value: " << to_string(global_peaks.current_max_value) << "\n";
  ofs << "max_steps: " << global_peaks.current_max_steps << "\n";
  ofs << "max_sigma: " << global_peaks.current_max_sigma << "\n\n";

  ofs << "max_value_peaks:\n";
  for (const auto &peak : max_value_peaks) {
    ofs << to_string(peak.start_val) << " " << to_string(peak.metric_val)
        << "\n";
  }
  ofs << "\n";

  ofs << "steps_peaks:\n";
  for (const auto &peak : steps_peaks) {
    ofs << to_string(peak.start_val) << " " << to_string(peak.metric_val)
        << "\n";
  }
  ofs << "\n";

  ofs << "sigma_peaks:\n";
  for (const auto &peak : sigma_peaks) {
    ofs << to_string(peak.start_val) << " " << to_string(peak.metric_val)
        << "\n";
  }
  ofs << "\n";

  ofs.close();
  return true;
}

bool load_checkpoint(const std::string &filename, uint128 &last_num,
                     std::vector<PeakRecord> &max_value_peaks,
                     std::vector<PeakRecord> &steps_peaks,
                     std::vector<PeakRecord> &sigma_peaks,
                     PeakState &global_peaks) {
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

    if (line.empty())
      continue;

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
          last_num = parse_uint128(val);
        } else if (key == "max_value") {
          global_peaks.current_max_value = parse_uint128(val);
        } else if (key == "max_steps") {
          global_peaks.current_max_steps = std::stoul(val);
        } else if (key == "max_sigma") {
          global_peaks.current_max_sigma = std::stoul(val);
        }
      }
    } else {
      std::istringstream iss(line);
      std::string start_str, metric_str;
      if (iss >> start_str >> metric_str) {
        PeakRecord record;
        record.start_val = parse_uint128(start_str);
        record.metric_val = parse_uint128(metric_str);
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

void print_help() {
  std::cout << "Usage: hailstone_cpu [options] [positional_start] "
               "[positional_end]\n\n"
            << "Options:\n"
            << "  -h, --help                 Show this help message\n"
            << "  --start-num, --start_num VALUE  Starting number of the "
               "search range (default: 3)\n"
            << "  --end-num, --end_num VALUE      Ending number of the search "
               "range (default: 100000)\n"
            << "  --start-block, --start_block INDEX Starting block index "
               "(each block is 2^32 items, overrides start-num)\n"
            << "  --end-block, --end_block INDEX     Ending block index "
               "(overrides end-num)\n"
            << "  --num-blocks, --num_blocks COUNT   Number of blocks to check "
               "(overrides end-num/end-block)\n"
            << "  --checkpoint, --checkpoint_file FILE Checkpoint file path "
               "(default: hailstone.chk)\n"
            << "  --no-checkpoint, --no_checkpoint     Disable saving and "
               "restoring checkpoints\n"
            << "  --no-save-checkpoint, --no_save_checkpoint Disable saving "
               "checkpoints at the end of search\n"
            << "  --cutoff-width, --cutoff_width VALUE Enable suffix-first search with "
               "given bit-width (8, 12, 16, or 20)\n\n"
            << "Note: Positional parameters can still be used as a fallback if "
               "no named options are provided.\n";
}

uint128 block_to_num(uint64_t block) {
  uint64_t low = block << 32;
  uint64_t high = block >> 32;
  return uint128(low, high);
}

int main(int argc, char *argv[]) {
  std::cout << "=== Hailstone CPU Search Program ===" << std::endl;
  std::cout << "[OpenMP Diagnostic] Max Threads: " << omp_get_max_threads() << std::endl;
  #pragma omp parallel
  {
      #pragma omp single
      std::cout << "[OpenMP Diagnostic] Active Threads in parallel region: " << omp_get_num_threads() << std::endl;
  }

  uint128 start(3);
  uint128 end(100000);

  bool has_start_num = false;
  bool has_end_num = false;
  bool has_start_block = false;
  bool has_end_block = false;
  bool has_num_blocks = false;

  uint128 opt_start_num(0);
  uint128 opt_end_num(0);
  uint64_t opt_start_block = 0;
  uint64_t opt_end_block = 0;
  uint64_t opt_num_blocks = 0;

  bool checkpoint_enabled = true;
  bool save_checkpoint_enabled = true;
  std::string checkpoint_file = "hailstone.chk";
  int cutoff_width = 20;

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
        opt_start_num = parse_uint128(argv[++i]);
        has_start_num = true;
      } else {
        std::cerr << "Error: " << arg << " requires an argument." << std::endl;
        return 1;
      }
    } else if (arg == "--end-num" || arg == "--end_num") {
      if (i + 1 < argc) {
        opt_end_num = parse_uint128(argv[++i]);
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

  if (cutoff_width != 0 && cutoff_width != 8 && cutoff_width != 12 && cutoff_width != 16 && cutoff_width != 20) {
    std::cerr << "Error: --cutoff-width must be 8, 12, 16, or 20." << std::endl;
    return 1;
  }


  bool has_range_options = has_start_block || has_start_num || has_end_block ||
                           has_end_num || has_num_blocks;
  if (has_range_options && !positional_args.empty()) {
    std::cerr << "Error: Cannot mix named options and positional arguments."
              << std::endl;
    print_help();
    return 1;
  }

  std::vector<PeakRecord> max_value_peaks;
  std::vector<PeakRecord> steps_peaks;
  std::vector<PeakRecord> sigma_peaks;
  PeakState global_peaks;
  uint128 last_num(0);
  bool checkpoint_loaded = false;

  if (checkpoint_enabled) {
    if (load_checkpoint(checkpoint_file, last_num, max_value_peaks, steps_peaks,
                        sigma_peaks, global_peaks)) {
      checkpoint_loaded = true;
      std::cout << "Loaded checkpoint: " << checkpoint_file
                << " (last number searched: " << to_string(last_num) << ")"
                << std::endl;
    }
  }

  // Determine start boundary
  if (has_start_block || has_start_num) {
    if (has_start_num) {
      start = opt_start_num;
    } else {
      start = block_to_num(opt_start_block);
      if (start < uint128(3)) {
        start = uint128(3);
      }
    }
  } else if (!positional_args.empty()) {
    start = parse_uint128(positional_args[0]);
  } else if (checkpoint_loaded) {
    start = last_num + uint128(1);
  } else {
    start = uint128(3);
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
        base_block = (opt_start_num.high << 32) | (opt_start_num.low >> 32);
      } else if (checkpoint_loaded) {
        base_block = (start.high << 32) | (start.low >> 32);
      }
      end = block_to_num(base_block + opt_num_blocks);
    } else {
      end = block_to_num(opt_end_block + 1);
    }
  } else if (positional_args.size() > 1) {
    end = parse_uint128(positional_args[1]);
  } else {
    // Default range size of 100,000 numbers
    end = start + uint128(99997);
  }

  if (positional_args.size() > 2) {
    std::cerr << "Error: Too many positional arguments." << std::endl;
    print_help();
    return 1;
  }

  std::cout << "Searching range: [" << to_string(start) << ", "
            << to_string(end) << "]" << std::endl;

  SearchMetrics metrics = {0};

  if (cutoff_width > 0) {
    std::cout << "Using Suffix-First Search with width: " << cutoff_width << std::endl;
    std::cout << "Generating allowed suffixes... " << std::flush;
    auto base_suffixes = generate_base_dependent_suffixes(cutoff_width);
    std::cout << base_suffixes.std_allowed.size() << " allowed suffixes generated." << std::endl;

    uint128 threshold(1 << cutoff_width);
    if (start < threshold) {
      uint128 standard_end = (end < threshold) ? end : (threshold - uint128(1));
      std::cout << "Running standard search on boundary range [" << to_string(start) << ", " << to_string(standard_end) << "]" << std::endl;
      cpu_search_block_0(start, standard_end, max_value_peaks, steps_peaks,
                         sigma_peaks, global_peaks, metrics);
      start = standard_end + uint128(1);
    }

    if (start <= end) {
      uint128 block_boundary(0x100000000ULL);
      if (start < block_boundary) {
        uint128 block_0_end = end;
        if (end >= block_boundary) {
          block_0_end = block_boundary - uint128(1);
        }
        cpu_search_block_0_suffix_first(start, block_0_end, cutoff_width, base_suffixes,
                                        max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
        if (end >= block_boundary) {
          cpu_search_range_suffix_first(block_boundary, end, cutoff_width, base_suffixes,
                                        max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
        }
      } else {
        cpu_search_range_suffix_first(start, end, cutoff_width, base_suffixes,
                                      max_value_peaks, steps_peaks, sigma_peaks, global_peaks, metrics);
      }
    }
  } else {
    uint128 block_boundary(0x100000000ULL);
    if (start < block_boundary) {
      uint128 block_0_end = end;
      if (end >= block_boundary) {
        block_0_end = block_boundary - uint128(1);
      }
      cpu_search_block_0(start, block_0_end, max_value_peaks, steps_peaks,
                         sigma_peaks, global_peaks, metrics);
      if (end >= block_boundary) {
        cpu_search_blocks_gt_0(block_boundary, end, max_value_peaks, steps_peaks,
                               sigma_peaks, global_peaks, metrics);
      }
    } else {
      cpu_search_blocks_gt_0(start, end, max_value_peaks, steps_peaks,
                             sigma_peaks, global_peaks, metrics);
    }
  }



  std::cout << "\n=== Search Completed ===" << std::endl;
  std::cout << "Elapsed Time: " << std::fixed << std::setprecision(4)
            << metrics.elapsed_seconds << " s" << std::endl;
  std::cout << "Numbers Checked: " << metrics.total_numbers_checked
            << std::endl;
  std::cout << "Steps Computed: " << metrics.total_steps_computed << std::endl;
  std::cout << "Average Steps: "
            << (metrics.total_numbers_checked > 0
                    ? (double)metrics.total_steps_computed /
                          metrics.total_numbers_checked
                    : 0.0)
            << std::endl;
  std::cout << "Skipped (Even): " << metrics.numbers_skipped_even << std::endl;
  std::cout << "Skipped (Mod 6): " << metrics.numbers_skipped_mod6 << std::endl;
  std::cout << "Overflowed (> 2^128): " << metrics.numbers_overflowed
            << std::endl;

  double m_ips =
      (metrics.total_numbers_checked / 1000000.0) / metrics.elapsed_seconds;
  std::cout << "Throughput: " << std::fixed << std::setprecision(2) << m_ips
            << " M numbers/s" << std::endl;

  std::cout << "\n=== Peaks Found ===" << std::endl;
  std::cout << "Max Value Peaks:" << std::endl;
  for (const auto &peak : max_value_peaks) {
    std::cout << "  n = " << to_string(peak.start_val)
              << " -> max_val = " << to_string(peak.metric_val) << std::endl;
  }

  std::cout << "\nSteps Peaks:" << std::endl;
  for (const auto &peak : steps_peaks) {
    std::cout << "  n = " << to_string(peak.start_val)
              << " -> steps = " << to_string(peak.metric_val) << std::endl;
  }

  std::cout << "\nStopping Time (sigma) Peaks:" << std::endl;
  for (const auto &peak : sigma_peaks) {
    std::cout << "  n = " << to_string(peak.start_val)
              << " -> sigma = " << to_string(peak.metric_val) << std::endl;
  }

  if (checkpoint_enabled && save_checkpoint_enabled) {
    if (save_checkpoint(checkpoint_file, end, max_value_peaks, steps_peaks,
                        sigma_peaks, global_peaks)) {
      std::cout << "Saved checkpoint: " << checkpoint_file << std::endl;
    }
  }

  // Print future predictions at the end of the search
  PeakPredictor final_predictor;
  for (const auto &peak : steps_peaks) {
    final_predictor.add_confirmed_peak(peak.start_val, peak.metric_val.low);
  }
  final_predictor.print_future_predictions_by_block();

  return 0;
}
