#!/usr/bin/env python3
import sys
import os
import subprocess
import time
import json
import argparse
from datetime import datetime

# Path helper
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, "build")
HISTORY_FILE = os.path.join(SCRIPT_DIR, "history.json")
README_FILE = os.path.join(SCRIPT_DIR, "README.md")

def run_cmd(cmd, cwd=BUILD_DIR):
    try:
        res = subprocess.run(cmd, shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=cwd)
        return res.stdout
    except subprocess.CalledProcessError as e:
        print(f"Error executing command: {cmd}")
        print(f"Stdout:\n{e.stdout}")
        print(f"Stderr:\n{e.stderr}")
        raise e

def verify_correctness():
    print("Step 1: Running correctness verification suite...")
    verify_bin = os.path.join(BUILD_DIR, "hailstone_verify")
    if not os.path.exists(verify_bin):
        raise FileNotFoundError(f"Verification binary not found at {verify_bin}. Please run build first.")
    
    try:
        run_cmd("./hailstone_verify")
        print("[PASS] Verification suite completed successfully.")
    except Exception as e:
        print("[FAIL] Verification suite failed! Aborting benchmark.")
        sys.exit(1)

def parse_cpu_output(stdout):
    results = {}
    lines = stdout.splitlines()
    for line in lines:
        line = line.strip()
        if line.startswith("Elapsed Time:"):
            results["kernel_time_ms"] = float(line.split(":")[1].replace("s", "").strip()) * 1000.0
        elif line.startswith("Numbers Checked:"):
            results["numbers_checked"] = int(line.split(":")[1].strip())
        elif line.startswith("Steps Computed:"):
            results["steps_computed"] = int(line.split(":")[1].strip())
        elif line.startswith("Throughput:"):
            results["throughput_m_numbers_s"] = float(line.split(":")[1].replace("M numbers/s", "").strip())
    
    # Extract peaks counts
    max_val_peaks = 0
    steps_peaks = 0
    sigma_peaks = 0
    current_section = 0
    for line in lines:
        line = line.strip()
        if "Max Value Peaks:" in line:
            current_section = 1
        elif "Steps Peaks:" in line:
            current_section = 2
        elif "Stopping Time (sigma) Peaks:" in line:
            current_section = 3
        elif line.startswith("n ="):
            if current_section == 1:
                max_val_peaks += 1
            elif current_section == 2:
                steps_peaks += 1
            elif current_section == 3:
                sigma_peaks += 1
                
    results["max_val_peaks_found"] = max_val_peaks
    results["steps_peaks_found"] = steps_peaks
    results["sigma_peaks_found"] = sigma_peaks
    results["memory_transfer_time_ms"] = 0.0
    return results

def parse_vulkan_output(stdout):
    results = {}
    lines = stdout.splitlines()
    for line in lines:
        line = line.strip()
        if line.startswith("Memory Transfer Time:"):
            results["memory_transfer_time_ms"] = float(line.split(":")[1].replace("ms", "").strip())
        elif line.startswith("Kernel Execution Time:"):
            results["kernel_time_ms"] = float(line.split(":")[1].replace("ms", "").strip())
        elif line.startswith("Numbers Checked:"):
            results["numbers_checked"] = int(line.split(":")[1].strip())
        elif line.startswith("Steps Computed:"):
            results["steps_computed"] = int(line.split(":")[1].strip())
        elif line.startswith("Throughput:"):
            results["throughput_m_numbers_s"] = float(line.split(":")[1].replace("M numbers/s", "").strip())

    # Extract peaks counts
    for line in lines:
        line = line.strip()
        if line.startswith("Max Value Peaks ("):
            results["max_val_peaks_found"] = int(line[line.rfind("(")+1:line.rfind(")")])
        elif line.startswith("Steps Peaks ("):
            results["steps_peaks_found"] = int(line[line.rfind("(")+1:line.rfind(")")])
        elif line.startswith("Stopping Time (sigma) Peaks ("):
            results["sigma_peaks_found"] = int(line[line.rfind("(")+1:line.rfind(")")])

    return results

def record_results(run_data):
    history = []
    if os.path.exists(HISTORY_FILE):
        try:
            with open(HISTORY_FILE, "r") as f:
                history = json.load(f)
        except Exception:
            pass
            
    history.append(run_data)
    with open(HISTORY_FILE, "w") as f:
        json.dump(history, f, indent=4)
    print(f"Results recorded in {HISTORY_FILE}")
    update_readme(history)

def update_readme(history):
    with open(README_FILE, "w") as f:
        f.write("# Hailstone Benchmark Optimization History\n\n")
        f.write("This directory contains execution times and performance measurements for different optimization iterations of the CPU, Vulkan, and HIP search programs.\n\n")
        
        f.write("## Historical Runs\n\n")
        f.write("| Date & Time | Backend | Range | Checked | Throughput | Kernel Time | Mem Transfer | Peaks (Val/Steps/Sig) |\n")
        f.write("|---|---|---|---|---|---|---|---|\n")
        
        # Sort history by date descending
        sorted_history = sorted(history, key=lambda x: x["timestamp"], reverse=True)
        for run in sorted_history:
            range_str = f"[{run['start']}, {run['end']}]"
            checked_str = f"{run['numbers_checked']:,}"
            throughput_str = f"{run['throughput_m_numbers_s']:.2f} M/s"
            kernel_str = f"{run['kernel_time_ms']:.2f} ms"
            mem_str = f"{run['memory_transfer_time_ms']:.2f} ms" if run['memory_transfer_time_ms'] > 0 else "N/A"
            peaks_str = f"{run['max_val_peaks_found']}/{run['steps_peaks_found']}/{run['sigma_peaks_found']}"
            f.write(f"| {run['timestamp']} | {run['backend']} | {range_str} | {checked_str} | {throughput_str} | {kernel_str} | {mem_str} | {peaks_str} |\n")
            
    print(f"Markdown table updated in {README_FILE}")

def main():
    parser = argparse.ArgumentParser(description="Collatz Search Benchmark Driver")
    parser.add_argument("--mode", choices=["quick", "full"], default="quick",
                        help="Benchmark range configuration: quick (100M range, ~4s CPU), full (8.5B range, ~5m CPU)")
    parser.add_argument("--backends", choices=["cpu", "vulkan", "hip", "all"], default="all",
                        help="Backends to benchmark")
    parser.add_argument("--no-verify", action="store_true", help="Skip running verify suite first")
    args = parser.parse_args()

    # Determine range
    if args.mode == "quick":
        start, end = 3, 100000000
    else:
        start, end = 3, 8589934592

    # Ensure build directory exists
    if not os.path.exists(BUILD_DIR):
        print(f"Build directory {BUILD_DIR} does not exist. Please compile first.")
        sys.exit(1)

    # Correctness check
    if not args.no_verify:
        verify_correctness()

    # Determine backends to run
    backends = []
    if args.backends in ["cpu", "all"]:
        backends.append("CPU")
    if args.backends in ["vulkan", "all"] and os.path.exists(os.path.join(BUILD_DIR, "hailstone_vulkan")):
        backends.append("Vulkan")
    if args.backends in ["hip", "all"] and os.path.exists(os.path.join(BUILD_DIR, "hailstone_hip")):
        backends.append("HIP")

    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    for backend in backends:
        print(f"\nStep 2: Benchmarking {backend} on range [{start:,}, {end:,}]...")
        if backend == "CPU":
            cmd = f"./hailstone_cpu {start} {end}"
            stdout = run_cmd(cmd)
            results = parse_cpu_output(stdout)
        elif backend == "Vulkan":
            cmd = f"./hailstone_vulkan {start} {end}"
            stdout = run_cmd(cmd)
            results = parse_vulkan_output(stdout)
        elif backend == "HIP":
            cmd = f"./hailstone_hip {start} {end}"
            stdout = run_cmd(cmd)
            results = parse_cpu_output(stdout)
            
        run_data = {
            "timestamp": timestamp,
            "backend": backend,
            "start": start,
            "end": end,
            **results
        }
        
        print(f"  Throughput: {run_data['throughput_m_numbers_s']:.2f} M numbers/s")
        print(f"  Kernel Time: {run_data['kernel_time_ms']:.2f} ms")
        print(f"  Peaks found: Max Val: {run_data['max_val_peaks_found']}, Steps: {run_data['steps_peaks_found']}, Sigma: {run_data['sigma_peaks_found']}")
        
        record_results(run_data)

if __name__ == "__main__":
    main()
