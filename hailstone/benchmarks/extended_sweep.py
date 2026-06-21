#!/usr/bin/env python3
import sys
import os
import subprocess
import time
import json
import argparse

# Path helper
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(PROJECT_DIR, "build")

def run_cmd(cmd, env=None, timeout=60):
    try:
        res = subprocess.run(cmd, env=env, shell=True, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=BUILD_DIR, timeout=timeout)
        return res.stdout, res.stderr
    except subprocess.TimeoutExpired as e:
        print(f"  [TIMEOUT] after {timeout}s: {cmd}")
        return "", "Timeout expired"
    except subprocess.CalledProcessError as e:
        print(f"  [ERROR] executing: {cmd}")
        return e.stdout, e.stderr

def parse_throughput(stdout):
    for line in stdout.splitlines():
        line = line.strip()
        if "Throughput:" in line:
            # e.g., "Throughput: 15.68 M numbers/s"
            parts = line.split("Throughput:")
            if len(parts) > 1:
                val_str = parts[1].strip().split()[0]
                try:
                    return float(val_str)
                except ValueError:
                    pass
    return 0.0

def parse_time(stdout):
    for line in stdout.splitlines():
        line = line.strip()
        # CPU prints: "Elapsed Time: 12.3456 s"
        if "Elapsed Time:" in line:
            parts = line.split("Elapsed Time:")
            if len(parts) > 1:
                val_str = parts[1].replace("s", "").strip()
                try:
                    return float(val_str)
                except ValueError:
                    pass
        # Vulkan prints: "Kernel Execution Time: 12.34 ms"
        elif "Kernel Execution Time:" in line:
            parts = line.split("Kernel Execution Time:")
            if len(parts) > 1:
                val_str = parts[1].replace("ms", "").strip()
                try:
                    return float(val_str) / 1000.0
                except ValueError:
                    pass
    return 0.0

def check_avx512_support():
    cpu_bin = os.path.join(BUILD_DIR, "hailstone_cpu")
    if not os.path.exists(cpu_bin):
        return False
    # Check if CPU binary outputs AVX-512 support by running a tiny query
    try:
        # Run standard CPU help or quick 1-number range to see if it supports it
        cmd = f"./hailstone_cpu --no-checkpoint --start-num 3 --end-num 3"
        res = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=BUILD_DIR)
        # Standard output checks runtime support
        # Let's inspect stdout for AVX-512 indications or check if the processor supports it
        if "AVX-512" in res.stdout or "avx512" in res.stdout:
            return True
    except Exception:
        pass
    
    # Fallback to cpuinfo check on Linux
    if os.path.exists("/proc/cpuinfo"):
        try:
            with open("/proc/cpuinfo", "r") as f:
                content = f.read()
                if "avx512" in content:
                    return True
        except Exception:
            pass
    return False

def main():
    parser = argparse.ArgumentParser(description="Extended Collatz Search Sweep Benchmark")
    parser.add_argument("--start-block", type=int, default=100000, help="Starting block index (default: 100000)")
    parser.add_argument("--count", type=int, default=1000000, help="Number of starting values to check per run (default: 1000000)")
    parser.add_argument("--backends", type=str, default="cpu,vulkan,hip", help="Comma-separated backends to run (default: cpu,vulkan,hip)")
    parser.add_argument("--timeout", type=int, default=120, help="Timeout in seconds for each configuration run (default: 120)")
    parser.add_argument("--output", type=str, default="extended_sweep_results.json", help="Path to save JSON results (default: extended_sweep_results.json)")
    args = parser.parse_args()

    selected_backends = [b.strip().lower() for b in args.backends.split(",")]
    
    # Calculate range
    start_num = args.start_block * (2**32) + 3 # start on odd number to avoid quick skip
    end_num = start_num + args.count
    
    print("======================================================================")
    print("             HAILSTONE EXTENDED SWEEP BENCHMARK DRIVER")
    print("======================================================================")
    print(f"Start Block : {args.start_block} ({start_num})")
    print(f"Count       : {args.count:,} values")
    print(f"End Number  : {end_num}")
    print(f"Backends    : {', '.join(selected_backends)}")
    print(f"Save Path   : {args.output}")
    print("======================================================================")

    # Verify build directory
    if not os.path.exists(BUILD_DIR):
        print(f"Error: Build directory {BUILD_DIR} does not exist. Please run 'make' first.")
        sys.exit(1)

    # Detect AVX-512 support
    has_avx512 = check_avx512_support()
    print(f"AVX-512 CPU hardware/compiler support detected: {has_avx512}")

    # Build the matrix
    configs = []

    # 1. CPU configs
    if "cpu" in selected_backends:
        cpu_bin = os.path.join(BUILD_DIR, "hailstone_cpu")
        if os.path.exists(cpu_bin):
            for threads in [32, 1]:
                # Test AVX-512 configurations
                # If host has no AVX-512 support, we still run the configurations but mark it as scalar fallback
                avx_options = [True, False] if has_avx512 else [False]
                for avx in avx_options:
                    for ds in [True, False]:
                        for cutoff in [0, 20, 24]:
                            configs.append({
                                "backend": "cpu",
                                "bin": "./hailstone_cpu",
                                "threads": threads,
                                "avx512": avx,
                                "domain_switching": ds,
                                "cutoff": cutoff
                            })
        else:
            print("Warning: hailstone_cpu binary not found. Skipping CPU backend.")

    # 2. Vulkan configs
    if "vulkan" in selected_backends:
        vulkan_bin = os.path.join(BUILD_DIR, "hailstone_vulkan")
        if os.path.exists(vulkan_bin):
            for ds in [True, False]:
                for cutoff in [0, 20, 24]:
                    configs.append({
                        "backend": "vulkan",
                        "bin": "./hailstone_vulkan",
                        "threads": None,
                        "avx512": None,
                        "domain_switching": ds,
                        "cutoff": cutoff
                    })
        else:
            print("Warning: hailstone_vulkan binary not found. Skipping Vulkan backend.")

    # 3. HIP configs
    if "hip" in selected_backends:
        hip_bin = os.path.join(BUILD_DIR, "hailstone_hip")
        if os.path.exists(hip_bin):
            for ds in [True, False]:
                for cutoff in [0, 20, 24]:
                    configs.append({
                        "backend": "hip",
                        "bin": "./hailstone_hip",
                        "threads": None,
                        "avx512": None,
                        "domain_switching": ds,
                        "cutoff": cutoff
                    })
        else:
            print("Warning: hailstone_hip binary not found. Skipping HIP backend.")

    if not configs:
        print("No valid binaries found to run benchmarks. Build the project first.")
        sys.exit(1)

    print(f"Total configurations to benchmark: {len(configs)}")
    print("Running benchmarks (this may take a few minutes)...")

    results = []
    
    # Establish base environment
    base_env = os.environ.copy()

    for idx, cfg in enumerate(configs):
        # Build command flags
        flags = [
            cfg["bin"],
            "--no-checkpoint",
            "--no-save-checkpoint",
            f"--start-num {start_num}",
            f"--end-num {end_num}"
        ]
        
        # CPU-specific settings
        env = base_env.copy()
        if cfg["backend"] == "cpu":
            env["OMP_NUM_THREADS"] = str(cfg["threads"])
            if cfg["avx512"]:
                flags.append("--use-avx512")
            else:
                flags.append("--no-avx512")
        
        # Domain Switching
        if cfg["domain_switching"]:
            flags.append("--domain-switching")
        else:
            flags.append("--no-domain-switching")
            
        # Cutoff Width
        flags.append(f"--cutoff-width {cfg['cutoff']}")

        cmd = " ".join(flags)
        
        cfg_desc = f"{cfg['backend'].upper()} | DS:{'ON' if cfg['domain_switching'] else 'OFF'} | Cutoff:{cfg['cutoff']}"
        if cfg['backend'] == 'cpu':
            cfg_desc += f" | Th:{cfg['threads']} | AVX:{'ON' if cfg['avx512'] else 'OFF'}"

        print(f"[{idx+1}/{len(configs)}] Running: {cfg_desc} ... ", end="", flush=True)

        t_start = time.perf_counter()
        stdout, stderr = run_cmd(cmd, env=env, timeout=args.timeout)
        t_end = time.perf_counter()
        
        duration = t_end - t_start
        throughput = parse_throughput(stdout)
        reported_time = parse_time(stdout)
        
        if throughput == 0.0:
            # Fallback calculation if binary didn't print throughput
            throughput = (args.count / 1e6) / duration if duration > 0 else 0.0
            status = "Parsed fallback" if stdout else "Failed/Timeout"
        else:
            status = "Success"

        run_result = {
            "backend": cfg["backend"],
            "threads": cfg["threads"],
            "avx512": cfg["avx512"],
            "domain_switching": cfg["domain_switching"],
            "cutoff": cfg["cutoff"],
            "cmd": cmd,
            "throughput_m_s": throughput,
            "reported_time_s": reported_time if reported_time > 0 else duration,
            "wall_time_s": duration,
            "status": status
        }
        
        results.append(run_result)
        print(f"Done! {throughput:.2f} M/s ({run_result['reported_time_s']:.3f}s)")

    # Save to JSON
    with open(args.output, "w") as f:
        json.dump(results, f, indent=4)
    print(f"\nSaved raw results to {args.output}")

    # Identify CPU baseline for relative speedup computation
    # Baseline: CPU, Threads=1, AVX512=OFF, DS=OFF, Cutoff=0
    baseline_throughput = 0.0
    for r in results:
        if (r["backend"] == "cpu" and 
            r["threads"] == 1 and 
            r["avx512"] == False and 
            r["domain_switching"] == False and 
            r["cutoff"] == 0):
            baseline_throughput = r["throughput_m_s"]
            break
            
    # If not found, use first run as baseline
    if baseline_throughput == 0.0 and results:
        # Find any CPU thread=1 run
        for r in results:
            if r["backend"] == "cpu" and r["threads"] == 1:
                baseline_throughput = r["throughput_m_s"]
                break
        if baseline_throughput == 0.0:
            baseline_throughput = results[0]["throughput_m_s"]

    # Print Markdown Table
    print("\n" + "="*80)
    print("                      SWEEP BENCHMARK SUMMARY TABLE")
    print("="*80)
    
    headers = ["Backend", "Threads", "AVX-512", "Domain Switch", "Cutoff Width", "Throughput (M/s)", "Time (s)", "Speedup"]
    print("| " + " | ".join(headers) + " |")
    print("|" + "|".join(["---" for _ in headers]) + "|")
    
    for r in results:
        threads_str = str(r["threads"]) if r["threads"] is not None else "N/A"
        avx_str = "ON" if r["avx512"] else ("OFF" if r["avx512"] is not None else "N/A")
        ds_str = "ON" if r["domain_switching"] else "OFF"
        cutoff_str = str(r["cutoff"])
        
        speedup = r["throughput_m_s"] / baseline_throughput if baseline_throughput > 0 else 1.0
        speedup_str = f"{speedup:.2f}x" if speedup >= 1.0 else f"{speedup:.2f}x"
        if r["throughput_m_s"] == baseline_throughput:
            speedup_str = "1.00x (Base)"
            
        row = [
            r["backend"].upper(),
            threads_str,
            avx_str,
            ds_str,
            cutoff_str,
            f"{r['throughput_m_s']:.2f}",
            f"{r['reported_time_s']:.3f}",
            speedup_str
        ]
        print("| " + " | ".join(row) + " |")
    print("="*80)

if __name__ == "__main__":
    main()
