#!/usr/bin/env python3
"""
Checkpoint Comparison Utility (Golden Master Verification)

This script compares a search checkpoint file against the Leavens-Vermeulen paper's 
peaks table formatted as 'golden_master.chk'. It filters peaks in both files up 
to the minimum shared search limit and checks for:
  1. Missing peaks (peaks in golden master but missing in the search output)
  2. Extra peaks (peaks in search output but missing in the golden master)
  3. Metric value mismatches (different peak values or step counts)
  4. Global summary statistics correctness (max_value, max_steps, max_sigma)

Usage:
    python3 tools/compare_checkpoint.py <candidate.chk> <golden_master.chk>

Example:
    python3 tools/compare_checkpoint.py hailstone_debug.chk golden_master.chk
"""
import sys
import os

def parse_checkpoint(file_path):
    if not os.path.exists(file_path):
        print(f"Error: Checkpoint file not found: {file_path}")
        return None

    data = {
        "last_num": 0,
        "max_value": 0,
        "max_steps": 0,
        "max_sigma": 0,
        "max_value_peaks": [],
        "steps_peaks": [],
        "sigma_peaks": []
    }

    current_section = None
    with open(file_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            if line.startswith("last_num:"):
                data["last_num"] = int(line.split(":", 1)[1].strip())
            elif line.startswith("max_value:"):
                data["max_value"] = int(line.split(":", 1)[1].strip())
            elif line.startswith("max_steps:"):
                data["max_steps"] = int(line.split(":", 1)[1].strip())
            elif line.startswith("max_sigma:"):
                data["max_sigma"] = int(line.split(":", 1)[1].strip())
            elif line == "max_value_peaks:":
                current_section = "max_value_peaks"
            elif line == "steps_peaks:":
                current_section = "steps_peaks"
            elif line == "sigma_peaks:":
                current_section = "sigma_peaks"
            else:
                parts = line.split()
                if len(parts) == 2:
                    start_val, metric_val = int(parts[0]), int(parts[1])
                    if current_section:
                        data[current_section].append((start_val, metric_val))
    return data

def compare_lists(name, list_cand, list_gold, limit):
    # Filter both lists up to the search limit
    cand_filtered = [p for p in list_cand if p[0] <= limit]
    gold_filtered = [p for p in list_gold if p[0] <= limit]

    cand_dict = dict(cand_filtered)
    gold_dict = dict(gold_filtered)

    cand_keys = set(cand_dict.keys())
    gold_keys = set(gold_dict.keys())

    missing_in_cand = sorted(list(gold_keys - cand_keys))
    extra_in_cand = sorted(list(cand_keys - gold_keys))

    ignored_extra_peaks = []
    if name == "sigma_peaks":
        # Extra peaks in candidate are allowed if they are above 7.8 billion (7,800,000,000),
        # because the Leavens paper's printed table of stopping time peaks stopped before that threshold.
        ignored_extra_peaks = [k for k in extra_in_cand if k > 7800000000]
        extra_in_cand = [k for k in extra_in_cand if k <= 7800000000]

    mismatches = []
    common_keys = sorted(list(cand_keys & gold_keys))
    for k in common_keys:
        if cand_dict[k] != gold_dict[k]:
            mismatches.append((k, cand_dict[k], gold_dict[k]))

    discrepancies = False
    if missing_in_cand:
        discrepancies = True
        print(f"  [FAIL] Missing {name} (starting numbers in Golden but not Candidate):")
        for k in missing_in_cand:
            print(f"    - Starting number: {k:,} (expected value: {gold_dict[k]:,})")

    if extra_in_cand:
        discrepancies = True
        print(f"  [FAIL] Extra {name} (starting numbers in Candidate but not Golden):")
        for k in extra_in_cand:
            print(f"    - Starting number: {k:,} (candidate value: {cand_dict[k]:,})")

    if ignored_extra_peaks:
        print(f"  [INFO] Ignored {len(ignored_extra_peaks)} extra {name} above 7.8 billion:")
        for k in ignored_extra_peaks:
            print(f"    - Starting number: {k:,} (candidate value: {cand_dict[k]:,})")

    if mismatches:
        discrepancies = True
        print(f"  [FAIL] Metric value mismatches for {name}:")
        for k, cv, gv in mismatches:
            print(f"    - Starting number: {k:,} -> Candidate: {cv:,} vs Golden: {gv:,}")

    if not discrepancies:
        print(f"  [OK] All {len(gold_filtered)} peaks in {name} are correct and match up to {limit:,}.")
    
    return discrepancies

def main():
    if len(sys.argv) < 3:
        print("Usage: compare_checkpoint.py <candidate.chk> <golden_master.chk>")
        sys.exit(1)

    cand_file = sys.argv[1]
    gold_file = sys.argv[2]

    print(f"Comparing candidate '{cand_file}' with golden master '{gold_file}'...")

    cand = parse_checkpoint(cand_file)
    gold = parse_checkpoint(gold_file)

    if not cand or not gold:
        sys.exit(1)

    limit = min(cand["last_num"], gold["last_num"])
    print(f"Minimum shared search limit: {limit:,}")
    print(f"Candidate last_num: {cand['last_num']:,}")
    print(f"Golden last_num:    {gold['last_num']:,}")
    print()

    has_errors = False

    # Check max_value peaks
    print("Checking max_value_peaks:")
    if compare_lists("max_value_peaks", cand["max_value_peaks"], gold["max_value_peaks"], limit):
        has_errors = True
    print()

    # Check steps peaks
    print("Checking steps_peaks:")
    if compare_lists("steps_peaks", cand["steps_peaks"], gold["steps_peaks"], limit):
        has_errors = True
    print()

    # Check sigma peaks
    print("Checking sigma_peaks:")
    if compare_lists("sigma_peaks", cand["sigma_peaks"], gold["sigma_peaks"], limit):
        has_errors = True
    print()

    # Verify summary fields up to limit
    print("Checking global summary metrics...")
    gold_mv_at_limit = max([p[1] for p in gold["max_value_peaks"] if p[0] <= limit])
    gold_st_at_limit = max([p[1] for p in gold["steps_peaks"] if p[0] <= limit])
    gold_sg_at_limit = max([p[1] for p in gold["sigma_peaks"] if p[0] <= limit])

    # If the candidate's search limit is exactly the limit, we compare candidate's global summary directly
    if cand["last_num"] == limit:
        if cand["max_value"] != gold_mv_at_limit:
            has_errors = True
            print(f"  [FAIL] Global max_value mismatch: Candidate has {cand['max_value']:,}, expected {gold_mv_at_limit:,}")
        else:
            print(f"  [OK] Global max_value is correct ({cand['max_value']:,})")

        if cand["max_steps"] != gold_st_at_limit:
            has_errors = True
            print(f"  [FAIL] Global max_steps mismatch: Candidate has {cand['max_steps']:,}, expected {gold_st_at_limit:,}")
        else:
            print(f"  [OK] Global max_steps is correct ({cand['max_steps']:,})")

        if limit <= 7800000000:
            if cand["max_sigma"] != gold_sg_at_limit:
                has_errors = True
                print(f"  [FAIL] Global max_sigma mismatch: Candidate has {cand['max_sigma']:,}, expected {gold_sg_at_limit:,}")
            else:
                print(f"  [OK] Global max_sigma is correct ({cand['max_sigma']:,})")
        else:
            cand_sg_max = max([p[1] for p in cand["sigma_peaks"] if p[0] <= limit]) if cand["sigma_peaks"] else 0
            if cand["max_sigma"] < gold_sg_at_limit:
                has_errors = True
                print(f"  [FAIL] Global max_sigma mismatch: Candidate has {cand['max_sigma']:,}, expected at least {gold_sg_at_limit:,}")
            elif cand["max_sigma"] != cand_sg_max:
                has_errors = True
                print(f"  [FAIL] Global max_sigma inconsistency: Candidate header has {cand['max_sigma']:,}, but candidate peaks max is {cand_sg_max:,}")
            else:
                print(f"  [OK] Global max_sigma is correct and consistent ({cand['max_sigma']:,})")
    else:
        print("  [INFO] Candidate search limit is larger than golden master limit; skipping global summary validation.")

    print()
    if has_errors:
        print("Verification result: [FAILED] Correctness issues detected!")
        sys.exit(1)
    else:
        print("Verification result: [PASSED] Checkpoint matches golden master successfully!")
        sys.exit(0)

if __name__ == "__main__":
    main()
