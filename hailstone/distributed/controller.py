#!/usr/bin/env python3
import os
import sys
import json
import threading
import time
import urllib.parse
import socket
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingTCPServer

# Defaults
DEFAULT_PORT = 8080
DEFAULT_CHECKPOINT = "hailstone_distributed.chk"

DEFAULT_THROUGHPUT = {
    "cpu": 10.0,
    "cpu_domain": 12.0,
    "cpu_nosteps": 40.0,
    "cpu_domain_nosteps": 45.0,
    "vulkan": 200.0,
    "vulkan_domain": 220.0,
    "vulkan_nosteps": 25.0,
    "vulkan_domain_nosteps": 28.0,
    "hip": 300.0,
    "hip_domain": 330.0,
    "hip_nosteps": 35.0,
    "hip_domain_nosteps": 38.0
}

# Logging settings
log_lock = threading.Lock()
log_file_path = None

def log(message):
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    prefix = ""
    message_str = str(message)
    if message_str.startswith("\n"):
        prefix = "\n"
        message_str = message_str[1:]
    
    formatted_msg = f"{prefix}[{timestamp}] {message_str}"
    print(formatted_msg)
    
    if log_file_path:
        with log_lock:
            try:
                with open(log_file_path, "a", encoding="utf-8") as f:
                    f.write(formatted_msg + "\n")
            except Exception as e:
                sys.stderr.write(f"[{timestamp}] Error writing to log file {log_file_path}: {e}\n")

def tcp_exchange(address, cmd, payload=None, timeout=2.0):
    try:
        host, port = address.split(":")
        port = int(port)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((host, port))
        
        request = f"{cmd}\n"
        if payload:
            request += payload
            if not request.endswith("\n"):
                request += "\n"
                
        sock.sendall(request.encode("utf-8"))
        
        response = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
            
        sock.close()
        return json.loads(response.decode("utf-8")), None
    except Exception as e:
        return None, str(e)

def run_range_benchmark(address, backend, start, end, cutoff):
    cmd = f"BENCHMARK {backend} {start} {end} {cutoff}"
    data, err = tcp_exchange(address, cmd, timeout=15.0)
    if err:
        return 0.0, err
    if data and data.get("status") == "success":
        return float(data.get("throughput", 0.0)), None
    return 0.0, data.get("error", "Unknown benchmark error")

def benchmark_worker_thread(worker_addr, task_start_time, task_start_num, global_backend, global_omit_steps, global_cutoff):
    log(f"Starting initial range-specific benchmarking for worker {worker_addr}...")
    try:
        with state.lock:
            worker = state.workers.get(worker_addr)
            if not worker:
                return
            backends = dict(worker.get("backends", {}))
            
        def apply_omit(b): return f"{b}_nosteps" if global_omit_steps else b

        candidates = []
        if global_backend == "best_gpu":
            has_gpu = False
            if apply_omit("hip") in backends or "hip" in backends:
                candidates.append((apply_omit("hip"), 20))
                candidates.append((apply_omit("hip"), 24))
                has_gpu = True
            if apply_omit("vulkan") in backends or "vulkan" in backends:
                candidates.append((apply_omit("vulkan"), 20))
                candidates.append((apply_omit("vulkan"), 24))
                has_gpu = True
            
            if not has_gpu:
                log(f"Worker {worker_addr} has no GPU backend available. Falling back to CPU for benchmarking.")
                candidates.append((apply_omit("cpu"), 20))
                candidates.append((apply_omit("cpu"), 24))
        else:
            candidates.append((apply_omit(global_backend), global_cutoff))
            
        results = {}
        for b, c in candidates:
            with state.lock:
                if not state.is_running or state.task_start_time != task_start_time:
                    log(f"Benchmark run cancelled for worker {worker_addr}: task state changed.")
                    return
            
            T_baseline = backends.get(b, DEFAULT_THROUGHPUT.get(b, 10.0))
            if T_baseline is None or T_baseline == 0:
                T_baseline = DEFAULT_THROUGHPUT.get(b, 10.0)
                
            size = int(T_baseline * 1000000.0 * 5.0)
            size = max(size, 1000000)
            
            start = task_start_num
            end = start + size - 1
            
            log(f"Benchmarking {worker_addr} range [{start}, {end}] config: {b} with cutoff {c} (target 5s)...")
            throughput, err = run_range_benchmark(worker_addr, b, start, end, c)
            if err:
                log(f"[Warning] Benchmark config {b}/{c} failed on {worker_addr}: {err}")
                results[(b, c)] = 0.0
            else:
                log(f"Benchmark config {b}/{c} on {worker_addr} throughput: {throughput:.2f} M/s")
                results[(b, c)] = throughput
        
        best_cfg = None
        best_throughput = -1.0
        for cfg, t in results.items():
            if t > best_throughput:
                best_throughput = t
                best_cfg = cfg
                
        if best_cfg is None or best_throughput <= 0.0:
            log(f"[Warning] All benchmarks failed on {worker_addr}. Using fallback default.")
            if global_backend == "best_gpu":
                if apply_omit("hip") in backends or "hip" in backends:
                    best_cfg = (apply_omit("hip"), 20)
                elif apply_omit("vulkan") in backends or "vulkan" in backends:
                    best_cfg = (apply_omit("vulkan"), 20)
                else:
                    best_cfg = (apply_omit("cpu"), 20)
            else:
                best_cfg = (apply_omit(global_backend), global_cutoff)
            best_throughput = backends.get(best_cfg[0], DEFAULT_THROUGHPUT.get(best_cfg[0], 10.0))
            if best_throughput is None or best_throughput == 0:
                best_throughput = 10.0
                
        best_backend, best_cutoff = best_cfg
        
        with state.lock:
            if state.is_running and state.task_start_time == task_start_time:
                worker = state.workers.get(worker_addr)
                if worker:
                    worker["selected_backend"] = best_backend
                    worker["selected_cutoff"] = best_cutoff
                    worker["benchmarked_for_task"] = task_start_time
                    if "throughput_history" not in worker:
                        worker["throughput_history"] = {}
                    worker["throughput_history"][best_backend] = [best_throughput]
                    worker["status"] = "online"
                    log(f"[Tuned] Worker {worker_addr} auto-tuned to: {best_backend} with cutoff {best_cutoff} (throughput: {best_throughput:.2f} M/s)")
            else:
                log(f"Benchmark finished but ignored for {worker_addr}: task state changed.")
                
    except Exception as e:
        log(f"Exception during benchmarking for {worker_addr}: {e}")
        with state.lock:
            worker = state.workers.get(worker_addr)
            if worker and worker["status"] == "benchmarking":
                worker["status"] = "online"

def tcp_worker_thread(address, backend, start, end, cutoff, checkpoint_payload, job_id, expected_timeout):
    start_time = time.time()
    try:
        host, port = address.split(":")
        port = int(port)
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(expected_timeout)
        sock.connect((host, port))
        
        with state.lock:
            if address in state.active_jobs and state.active_jobs[address]["job_id"] == job_id:
                state.active_jobs[address]["socket"] = sock
        
        request = f"COMPUTE {backend} {start} {end} {cutoff}\n{checkpoint_payload}\n__END_CHECKPOINT__\n"
        sock.sendall(request.encode("utf-8"))
        
        buffer = ""
        checkpoint_data = ""
        in_checkpoint = False
        metrics = {
            "elapsed_seconds": 0.0,
            "numbers_checked": 0,
            "steps_computed": 0,
            "throughput_m_numbers_s": 0.0
        }
        
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buffer += chunk.decode("utf-8")
            
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                
                if line == "__BEGIN_CHECKPOINT__":
                    in_checkpoint = True
                elif line == "__END_CHECKPOINT__":
                    in_checkpoint = False
                elif in_checkpoint:
                    checkpoint_data += line + "\n"
                else:
                    if "Elapsed Time:" in line:
                        val = line.split(":", 1)[1].replace("s", "").strip()
                        metrics["elapsed_seconds"] = float(val)
                    elif "Kernel Execution Time:" in line:
                        val = line.split(":", 1)[1].replace("ms", "").strip()
                        metrics["elapsed_seconds"] = float(val) / 1000.0
                    elif "Numbers Checked:" in line:
                        metrics["numbers_checked"] = int(line.split(":", 1)[1].strip())
                    elif "Steps Computed:" in line:
                        metrics["steps_computed"] = int(line.split(":", 1)[1].strip())
                    elif "Throughput:" in line:
                        val = line.split(":", 1)[1].replace("M numbers/s", "").strip()
                        metrics["throughput_m_numbers_s"] = float(val)
        
        sock.close()
        
        if checkpoint_data and metrics["elapsed_seconds"] > 0:
            actual_elapsed = time.time() - start_time
            log(f"[Success] Job {job_id} completed on {address} in {metrics['elapsed_seconds']:.2f}s (actual controller time: {actual_elapsed:.2f}s).")
            state.merge_worker_checkpoint(checkpoint_data)
            with state.lock:
                state.total_numbers_checked += metrics["numbers_checked"]
                state.total_steps_computed += metrics["steps_computed"]
                state.elapsed_seconds += metrics["elapsed_seconds"]
                state.active_jobs.pop(address, None)
                worker = state.workers.get(address)
                if worker:
                    worker["status"] = "online"
                    if "throughput_history" not in worker:
                        worker["throughput_history"] = {}
                    if backend not in worker["throughput_history"]:
                        worker["throughput_history"][backend] = []
                    
                    range_size = end - start + 1
                    if metrics["elapsed_seconds"] > 0:
                        range_throughput = (range_size / 1000000.0) / metrics["elapsed_seconds"]
                        worker["throughput_history"][backend].append(range_throughput)
                    else:
                        worker["throughput_history"][backend].append(metrics["throughput_m_numbers_s"])
                        
                    if len(worker["throughput_history"][backend]) > 5:
                        worker["throughput_history"][backend].pop(0)
        else:
            actual_elapsed = time.time() - start_time
            log(f"[Failed] Job {job_id} failed on {address} after {actual_elapsed:.2f}s: Worker returned no valid metrics or socket closed.")
            with state.lock:
                if address in state.active_jobs and state.active_jobs[address]["job_id"] == job_id:
                    state.failed_chunks.append((start, end))
                    state.active_jobs.pop(address, None)
                    worker = state.workers.get(address)
                    if worker and worker["status"] != "offline":
                        worker["status"] = "online"

    except Exception as e:
        actual_elapsed = time.time() - start_time
        log(f"[Failed] Job {job_id} exception on {address} after {actual_elapsed:.2f}s: {e}")
        with state.lock:
            if address in state.active_jobs and state.active_jobs[address]["job_id"] == job_id:
                state.failed_chunks.append((start, end))
                state.active_jobs.pop(address, None)
                worker = state.workers.get(address)
                if worker and worker["status"] != "offline":
                    worker["status"] = "online"

class ControllerState:
    def __init__(self, checkpoint_path=DEFAULT_CHECKPOINT):
        self.lock = threading.Lock()
        self.checkpoint_lock = threading.Lock()
        self.checkpoint_path = checkpoint_path
        self.project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.web_dir = os.path.join(self.project_dir, "distributed", "web")
        
        # Ensure web directory structure exists
        os.makedirs(self.web_dir, exist_ok=True)

        # Worker Registry
        # address ("ip:port") -> details
        self.workers = {}

        # Task Setup
        self.is_running = False
        self.task_start_num = 3
        self.task_end_num = 100000
        self.task_backend = "cpu"
        self.task_omit_steps = False
        self.task_cutoff_width = 20
        self.task_target_duration = 1.0  # minutes

        # Work Queue & Progress
        self.next_search_num = 3
        self.failed_chunks = []         # list of (start, end)
        self.active_jobs = {}           # worker_address -> job dict
        
        # Consolidated Stats
        self.total_numbers_checked = 0
        self.total_steps_computed = 0
        self.elapsed_seconds = 0.0
        self.task_start_time = 0.0
        
        # Peak State (fully compatible with single node chk files)
        self.global_peaks = {
            "last_num": 0,
            "max_value": 0,
            "max_steps": 0,
            "max_sigma": 0,
            "max_value_peaks": [],     # list of {"start_val": int, "metric_val": int}
            "steps_peaks": [],
            "sigma_peaks": [],
            "almost_steps_peaks": []
        }
        
        # Fast dictionary tracking to avoid list duplicates and sorting under lock
        self.global_peaks_dict = {
            "max_value_peaks": {},
            "steps_peaks": {},
            "sigma_peaks": {},
            "almost_steps_peaks": {}
        }

        # Load initial checkpoint if it exists
        self.load_checkpoint()

    def _worker_health_checker(self, worker_addr):
        while True:
            try:
                # Check status
                data, err = tcp_exchange(worker_addr, "STATUS", timeout=1.5)
                
                with self.lock:
                    if worker_addr not in self.workers:
                        # Worker was removed
                        break
                    w = self.workers[worker_addr]
                    if err:
                        w["error_count"] += 1
                        if w["error_count"] >= 3:
                            w["status"] = "offline"
                    else:
                        w["status"] = "busy" if data["status"] == "busy" else "online"
                        w["backends"] = data["backends"]
                        w["cpu_cores"] = data["cpu_cores"]
                        w["system_load"] = data["system_load"]
                        w["error_count"] = 0
                        w["last_seen"] = time.time()
            except Exception as e:
                log(f"Error in health checker for {worker_addr}: {e}")
                
            time.sleep(5.0)

    def add_worker(self, address):
        with self.lock:
            if address not in self.workers:
                self.workers[address] = {
                    "status": "offline",
                    "backends": {},
                    "throughput_history": {},
                    "cpu_cores": 0,
                    "system_load": [0.0, 0.0, 0.0],
                    "error_count": 0,
                    "last_seen": 0.0,
                    "benchmarked_for_task": None,
                    "selected_backend": None,
                    "selected_cutoff": None
                }
                log(f"Registered worker: {address}")
                # Start health checker thread for this worker
                t = threading.Thread(target=self._worker_health_checker, args=(address,))
                t.daemon = True
                t.start()

    def remove_worker(self, address):
        with self.lock:
            if address in self.workers:
                del self.workers[address]
                log(f"Removed worker: {address}")

    def load_checkpoint(self):
        if not os.path.exists(self.checkpoint_path):
            log(f"No existing checkpoint found at {self.checkpoint_path}. Starting clean.")
            return
        
        log(f"Loading consolidated checkpoint: {self.checkpoint_path}")
        try:
            res = {
                "last_num": 0,
                "max_value": 0,
                "max_steps": 0,
                "max_sigma": 0,
                "max_value_peaks": [],
                "steps_peaks": [],
                "sigma_peaks": [],
                "almost_steps_peaks": []
            }
            
            with open(self.checkpoint_path, "r") as f:
                lines = f.readlines()
            
            section = "header"
            for line in lines:
                line = line.strip()
                if not line:
                    continue
                if line == "max_value_peaks:":
                    section = "max_value_peaks"
                    continue
                elif line == "steps_peaks:":
                    section = "steps_peaks"
                    continue
                elif line == "sigma_peaks:":
                    section = "sigma_peaks"
                    continue
                elif line == "almost_steps_peaks:":
                    section = "almost_steps_peaks"
                    continue
                
                if section == "header":
                    if ":" in line:
                        k, v = line.split(":", 1)
                        k = k.strip()
                        v = v.strip()
                        if k == "last_num":
                            res["last_num"] = int(v)
                        elif k == "max_value":
                            res["max_value"] = int(v)
                        elif k == "max_steps":
                            res["max_steps"] = int(v)
                        elif k == "max_sigma":
                            res["max_sigma"] = int(v)
                else:
                    parts = line.split()
                    if len(parts) == 2:
                        res[section].append({
                            "start_val": int(parts[0]),
                            "metric_val": int(parts[1])
                        })
            
            self.global_peaks = res
            self.next_search_num = res["last_num"] + 1
            # Populate dictionary representation for fast index updates
            self.global_peaks_dict = {
                "max_value_peaks": {p["start_val"]: p["metric_val"] for p in res["max_value_peaks"]},
                "steps_peaks": {p["start_val"]: p["metric_val"] for p in res["steps_peaks"]},
                "sigma_peaks": {p["start_val"]: p["metric_val"] for p in res["sigma_peaks"]},
                "almost_steps_peaks": {p["start_val"]: p["metric_val"] for p in res.get("almost_steps_peaks", [])}
            }
            log(f"Loaded peak state successfully. Resuming from starting number: {self.next_search_num}")
        except Exception as e:
            log(f"Error reading checkpoint file: {e}")

    def save_checkpoint(self):
        with self.checkpoint_lock:
            with self.lock:
                last_num = self.global_peaks["last_num"]
                peaks_dict_copy = {
                    "max_value_peaks": dict(self.global_peaks_dict["max_value_peaks"]),
                    "steps_peaks": dict(self.global_peaks_dict["steps_peaks"]),
                    "sigma_peaks": dict(self.global_peaks_dict["sigma_peaks"]),
                    "almost_steps_peaks": dict(self.global_peaks_dict["almost_steps_peaks"])
                }
            
            try:
                max_value_peaks = []
                steps_peaks = []
                sigma_peaks = []
                
                # 1. Prune max_value_peaks
                sorted_mv = sorted(peaks_dict_copy["max_value_peaks"].items())
                max_metric = -1
                for s, m in sorted_mv:
                    if m > max_metric:
                        max_value_peaks.append({"start_val": s, "metric_val": m})
                        max_metric = m
                        
                # 2. Prune steps_peaks
                sorted_sp = sorted(peaks_dict_copy["steps_peaks"].items())
                max_metric = -1
                for s, m in sorted_sp:
                    if m > max_metric:
                        steps_peaks.append({"start_val": s, "metric_val": m})
                        max_metric = m
                        
                # 3. Prune sigma_peaks
                sorted_si = sorted(peaks_dict_copy["sigma_peaks"].items())
                max_metric = -1
                for s, m in sorted_si:
                    if m > max_metric:
                        sigma_peaks.append({"start_val": s, "metric_val": m})
                        max_metric = m
                
                # 4. Almost steps peaks
                almost_steps_peaks = [{"start_val": s, "metric_val": m} for s, m in sorted(peaks_dict_copy["almost_steps_peaks"].items())]
                
                max_value = max_value_peaks[-1]["metric_val"] if max_value_peaks else 0
                max_steps = steps_peaks[-1]["metric_val"] if steps_peaks else 0
                max_sigma = sigma_peaks[-1]["metric_val"] if sigma_peaks else 0
                
                temp_path = self.checkpoint_path + ".tmp"
                with open(temp_path, "w") as f:
                    f.write(f"last_num: {last_num}\n")
                    f.write(f"max_value: {max_value}\n")
                    f.write(f"max_steps: {max_steps}\n")
                    f.write(f"max_sigma: {max_sigma}\n\n")
                    
                    f.write("max_value_peaks:\n")
                    for peak in max_value_peaks:
                        f.write(f"{peak['start_val']} {peak['metric_val']}\n")
                    f.write("\n")
                    
                    f.write("steps_peaks:\n")
                    for peak in steps_peaks:
                        f.write(f"{peak['start_val']} {peak['metric_val']}\n")
                    f.write("\n")
                    
                    f.write("sigma_peaks:\n")
                    for peak in sigma_peaks:
                        f.write(f"{peak['start_val']} {peak['metric_val']}\n")
                    f.write("\n")
                    
                    if almost_steps_peaks:
                        f.write("almost_steps_peaks:\n")
                        for peak in almost_steps_peaks:
                            f.write(f"{peak['start_val']} {peak['metric_val']}\n")
                        f.write("\n")
                
                os.replace(temp_path, self.checkpoint_path)
                
                with self.lock:
                    self.global_peaks["last_num"] = last_num
                    self.global_peaks["max_value"] = max_value
                    self.global_peaks["max_steps"] = max_steps
                    self.global_peaks["max_sigma"] = max_sigma
                    self.global_peaks["max_value_peaks"] = max_value_peaks
                    self.global_peaks["steps_peaks"] = steps_peaks
                    self.global_peaks["sigma_peaks"] = sigma_peaks
                    self.global_peaks["almost_steps_peaks"] = almost_steps_peaks
            except Exception as e:
                log(f"Error saving consolidated checkpoint: {e}")

    def save_checkpoint_async(self):
        t = threading.Thread(target=self.save_checkpoint)
        t.daemon = True
        t.start()

    def serialize_peaks_string(self):
        # Outputs current peak state as string compatible with daemon input
        lines = []
        lines.append(f"last_num: {self.global_peaks['last_num']}")
        lines.append(f"max_value: {self.global_peaks['max_value']}")
        lines.append(f"max_steps: {self.global_peaks['max_steps']}")
        lines.append(f"max_sigma: {self.global_peaks['max_sigma']}\n")
        
        lines.append("max_value_peaks:")
        for p in self.global_peaks["max_value_peaks"]:
            lines.append(f"{p['start_val']} {p['metric_val']}")
        lines.append("")
        
        lines.append("steps_peaks:")
        for p in self.global_peaks["steps_peaks"]:
            lines.append(f"{p['start_val']} {p['metric_val']}")
        lines.append("")
        
        lines.append("sigma_peaks:")
        for p in self.global_peaks["sigma_peaks"]:
            lines.append(f"{p['start_val']} {p['metric_val']}")
        lines.append("")
        
        if "almost_steps_peaks" in self.global_peaks:
            lines.append("almost_steps_peaks:")
            for p in self.global_peaks["almost_steps_peaks"]:
                lines.append(f"{p['start_val']} {p['metric_val']}")
            lines.append("")
            
        return "\n".join(lines)

    def merge_worker_checkpoint(self, checkpoint_text):
        res = {
            "last_num": 0,
            "max_value_peaks": [],
            "steps_peaks": [],
            "sigma_peaks": [],
            "almost_steps_peaks": []
        }
        
        section = "header"
        for line in checkpoint_text.splitlines():
            line = line.strip()
            if not line:
                continue
            if line == "max_value_peaks:":
                section = "max_value_peaks"
                continue
            elif line == "steps_peaks:":
                section = "steps_peaks"
                continue
            elif line == "sigma_peaks:":
                section = "sigma_peaks"
                continue
            elif line == "almost_steps_peaks:":
                section = "almost_steps_peaks"
                continue
            
            if section == "header":
                if ":" in line:
                    k, v = line.split(":", 1)
                    if k.strip() == "last_num":
                        res["last_num"] = int(v.strip())
            else:
                parts = line.split()
                if len(parts) == 2:
                    res[section].append({
                        "start_val": int(parts[0]),
                        "metric_val": int(parts[1])
                    })
        
        # Merge arrays into global state
        with self.lock:
            for s in ["max_value_peaks", "steps_peaks", "sigma_peaks", "almost_steps_peaks"]:
                for peak in res[s]:
                    start = peak["start_val"]
                    metric = peak["metric_val"]
                    if start not in self.global_peaks_dict[s] or metric > self.global_peaks_dict[s][start]:
                        self.global_peaks_dict[s][start] = metric
            
            # Incrementally update last_num
            if res["last_num"] > self.global_peaks["last_num"]:
                self.global_peaks["last_num"] = res["last_num"]
            
        # Asynchronously prune and save checkpoint file
        self.save_checkpoint_async()

# Singleton state
state = ControllerState()

def background_scheduler():
    while True:
        try:
            # 1. Check running jobs and handle adaptive timeouts
            now = time.time()
            for worker_addr, job in list(state.active_jobs.items()):
                with state.lock:
                    worker = state.workers.get(worker_addr)
                
                # Check for timeout or offline status
                is_offline = worker and worker["status"] == "offline"
                is_timeout = now > job["timeout"]
                
                if is_offline or is_timeout:
                    reason = "went offline" if is_offline else "timed out"
                    log(f"[Warning] Job {job['job_id']} on {worker_addr} {reason}. Re-queuing range [{job['start_num']}, {job['end_num']}].")
                    
                    sock = job.get("socket")
                    if sock:
                        try:
                            sock.close()
                        except:
                            pass
                    
                    with state.lock:
                        state.failed_chunks.append((job["start_num"], job["end_num"]))
                        state.active_jobs.pop(worker_addr, None)
                        if worker and worker["status"] != "offline":
                            worker["status"] = "online"
            
            # 3. Dispatch work if scheduler is active
            with state.lock:
                run_scheduler = state.is_running
                backend = state.task_backend
                omit_steps = state.task_omit_steps
                cutoff_width = state.task_cutoff_width
                target_duration = state.task_target_duration
                task_end = state.task_end_num
                task_start_num = state.task_start_num
                task_start_time = state.task_start_time

            if run_scheduler:
                # Find online, idle workers
                for worker_addr, worker in state.workers.items():
                    if worker["status"] == "online":
                        # Check if worker needs range-specific benchmarking/auto-tuning for current task
                        if worker.get("benchmarked_for_task") != task_start_time:
                            with state.lock:
                                worker["status"] = "benchmarking"
                            t = threading.Thread(target=benchmark_worker_thread, args=(
                                worker_addr, task_start_time, task_start_num,
                                backend, omit_steps, cutoff_width
                            ))
                            t.daemon = True
                            t.start()
                            continue

                        # Use worker-specific tuned configurations
                        worker_backend = worker.get("selected_backend", backend)
                        if "selected_backend" not in worker:
                            worker_backend = f"{backend}_nosteps" if omit_steps else backend
                        worker_cutoff = worker.get("selected_cutoff", cutoff_width)

                        throughput = worker["backends"].get(worker_backend)
                        if throughput is None or throughput == 0:
                            continue # Backend unsupported on worker
                        
                        if "throughput_history" in worker and worker_backend in worker["throughput_history"]:
                            hist = worker["throughput_history"][worker_backend]
                            if hist:
                                throughput = sum(hist) / len(hist)

                        if throughput <= 0.0:
                            throughput = 0.1  # Fallback to prevent division by zero
                        
                        # We have an idle worker! Decide next range
                        chunk_range = None
                        with state.lock:
                            if state.failed_chunks:
                                chunk_range = state.failed_chunks.pop(0)
                            elif state.next_search_num <= task_end:
                                # Calculate dynamic chunk size: duration (sec) * throughput (M/s) * 1,000,000
                                size = int(target_duration * 60.0 * throughput * 1000000.0)
                                size = max(size, 1000000) # Enforce min chunk size of 1M
                                
                                start = state.next_search_num
                                end = min(start + size - 1, task_end)
                                chunk_range = (start, end)
                                state.next_search_num = end + 1
                        
                        if chunk_range:
                            start, end = chunk_range
                            job_id = f"job_{int(time.time())}_{start}"
                            
                            # Create expected duration and adaptive timeout
                            expected_run = (end - start + 1) / (throughput * 1000000.0)
                            timeout_dur = expected_run * 2.5 + 30.0 # 2.5x buffer + 30s grace
                            
                            # Prepare start checkpoint data
                            checkpoint_payload = state.serialize_peaks_string()
                            
                            log(f"Dispatching range [{start}, {end}] to {worker_addr} (using backend: {worker_backend}, cutoff: {worker_cutoff}, expected run: {expected_run:.1f}s, timeout: {timeout_dur:.1f}s)...")
                            
                            with state.lock:
                                worker["status"] = "busy"
                                state.active_jobs[worker_addr] = {
                                    "job_id": job_id,
                                    "start_num": start,
                                    "end_num": end,
                                    "start_time": time.time(),
                                    "timeout": time.time() + timeout_dur,
                                    "socket": None
                                }
                            
                            t = threading.Thread(target=tcp_worker_thread, args=(
                                worker_addr, worker_backend, start, end, worker_cutoff, checkpoint_payload, job_id, timeout_dur))
                            t.daemon = True
                            t.start()
            
            # Check if everything is finished
            with state.lock:
                if state.is_running and state.next_search_num > task_end and not state.active_jobs and not state.failed_chunks:
                    log("=== Distributed Search Completed successfully! ===")
                    state.is_running = False
                    
        except Exception as e:
            log(f"Scheduler exception: {e}")
            
        time.sleep(0.5)

class ControllerHTTPHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Quiet requests logging
        pass

    def send_json(self, data, status_code=200):
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode("utf-8"))

    def serve_file(self, filepath, content_type):
        if not os.path.exists(filepath):
            self.send_json({"error": f"File {os.path.basename(filepath)} not found"}, 404)
            return
        try:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.end_headers()
            with open(filepath, "rb") as f:
                self.wfile.write(f.read())
        except Exception as e:
            self.send_json({"error": f"Could not read file: {e}"}, 500)

    def do_GET(self):
        url_parsed = urllib.parse.urlparse(self.path)
        path = url_parsed.path

        if path.startswith("/api/"):
            self.handle_api_get(path)
            return

        # Serve Web UI files
        if path == "/" or path == "/index.html":
            self.serve_file(os.path.join(state.web_dir, "index.html"), "text/html")
        elif path == "/style.css":
            self.serve_file(os.path.join(state.web_dir, "style.css"), "text/css")
        elif path == "/app.js":
            self.serve_file(os.path.join(state.web_dir, "app.js"), "application/javascript")
        else:
            self.send_json({"error": "Not Found"}, 404)

    def handle_api_get(self, path):
        if path == "/api/status":
            with state.lock:
                # Stringify large numbers to prevent precision loss in javascript
                # Helper to stringify fields of dict lists
                def stringify_peak_list(lst):
                    return [{"start_val": str(p["start_val"]), "metric_val": str(p["metric_val"])} for p in lst]

                task_progress = 0.0
                total_range = state.task_end_num - state.task_start_num + 1
                if total_range > 0 and state.is_running:
                    completed_so_far = state.next_search_num - state.task_start_num
                    # Subtract active job ranges so progress bar is accurate
                    for job in state.active_jobs.values():
                        completed_so_far -= (job["end_num"] - job["start_num"] + 1)
                    for start, end in state.failed_chunks:
                        completed_so_far -= (end - start + 1)
                    task_progress = (completed_so_far / total_range) * 100.0
                    task_progress = min(max(task_progress, 0.0), 100.0)
                elif not state.is_running and state.next_search_num > state.task_end_num:
                    task_progress = 100.0

                serialized_peaks = {
                    "last_num": str(state.global_peaks["last_num"]),
                    "max_value": str(state.global_peaks["max_value"]),
                    "max_steps": int(state.global_peaks["max_steps"]),
                    "max_sigma": int(state.global_peaks["max_sigma"]),
                    "max_value_peaks": stringify_peak_list(state.global_peaks["max_value_peaks"]),
                    "steps_peaks": stringify_peak_list(state.global_peaks["steps_peaks"]),
                    "sigma_peaks": stringify_peak_list(state.global_peaks["sigma_peaks"])
                }

                # Calculate current combined throughput of active busy daemons
                combined_throughput = 0.0
                for w_addr, job in state.active_jobs.items():
                    worker = state.workers.get(w_addr)
                    if worker:
                        w_backend = worker.get("selected_backend", state.task_backend)
                        t = worker["backends"].get(w_backend, 0.0) if w_backend != "best_gpu" else 0.0
                        if "throughput_history" in worker and w_backend in worker["throughput_history"]:
                            hist = worker["throughput_history"][w_backend]
                            if hist:
                                t = sum(hist) / len(hist)
                        combined_throughput += t

                res = {
                    "is_running": state.is_running,
                    "task": {
                        "start_num": str(state.task_start_num),
                        "end_num": str(state.task_end_num),
                        "backend": state.task_backend,
                        "cutoff_width": state.task_cutoff_width,
                        "target_duration": state.task_target_duration,
                        "omit_steps": state.task_omit_steps
                    },
                    "progress": {
                        "next_search_num": str(state.next_search_num),
                        "total_numbers_checked": state.total_numbers_checked,
                        "total_steps_computed": state.total_steps_computed,
                        "elapsed_seconds": state.elapsed_seconds,
                        "percent_completed": task_progress,
                        "combined_throughput_m_s": combined_throughput
                    },
                    "global_peaks": serialized_peaks,
                    "workers": state.workers
                }
                self.send_json(res)
            return
        
        self.send_json({"error": "Not Found"}, 404)

    def do_POST(self):
        url_parsed = urllib.parse.urlparse(self.path)
        path = url_parsed.path

        content_length = int(self.headers.get("Content-Length", 0))
        post_data = self.rfile.read(content_length) if content_length > 0 else b""
        
        try:
            body = json.loads(post_data.decode("utf-8")) if post_data else {}
        except Exception as e:
            self.send_json({"error": f"Invalid JSON: {e}"}, 400)
            return

        if path == "/api/start":
            with state.lock:
                if state.is_running:
                    self.send_json({"error": "Search is already running"}, 400)
                    return
                
                backend = body.get("backend", "cpu")
                omit_steps = body.get("omit_steps", False)
                cutoff_width = int(body.get("cutoff_width", 20))
                target_duration = float(body.get("target_duration", 1.0))

                start_val = body.get("start_num")
                if start_val is None or start_val == "":
                    start_num = state.next_search_num
                else:
                    try:
                        start_num = int(start_val)
                    except Exception:
                        self.send_json({"error": "Invalid start number"}, 400)
                        return

                end_val = body.get("end_num")
                if end_val is None or end_val == "":
                    end_num = 10**30  # Effectively continuous
                else:
                    try:
                        end_num = int(end_val)
                    except Exception:
                        self.send_json({"error": "Invalid end number"}, 400)
                        return

                if start_num < 3 or end_num < start_num:
                    self.send_json({"error": "Invalid start/end numbers"}, 400)
                    return

                # Configure Task
                state.task_start_num = start_num
                state.task_end_num = end_num
                state.task_backend = backend
                state.task_omit_steps = omit_steps
                state.task_cutoff_width = cutoff_width
                state.task_target_duration = target_duration
                
                state.next_search_num = start_num
                state.failed_chunks = []
                state.active_jobs = {}
                state.total_numbers_checked = 0
                state.total_steps_computed = 0
                state.elapsed_seconds = 0.0
                state.task_start_time = time.time()
                
                # Check if we should initialize checkpoint headers
                if state.global_peaks["last_num"] == 0:
                    state.global_peaks["last_num"] = start_num - 1
                
                state.is_running = True
                log(f"=== Starting Distributed Search: [{start_num}, {end_num}] via {backend} ===")
                
            self.send_json({"status": "started"})
            return

        elif path == "/api/stop" or path == "/api/cancel":
            with state.lock:
                if not state.is_running:
                    self.send_json({"status": "idle", "message": "Search is not currently running"})
                    return
                
                log("Stopping distributed search on request...")
                state.is_running = False
                
                # Cancel all running jobs on workers
                for worker_addr, job in list(state.active_jobs.items()):
                    log(f"Cancelling job {job['job_id']} on {worker_addr}...")
                    sock = job.get("socket")
                    if sock:
                        try:
                            sock.close()
                        except:
                            pass
                
                state.active_jobs = {}
                state.failed_chunks = []
                
            self.send_json({"status": "stopped"})
            return

        elif path == "/api/daemons":
            address = body.get("address")
            if not address:
                self.send_json({"error": "Missing 'address' in body"}, 400)
                return
            state.add_worker(address)
            self.send_json({"status": "registered", "address": address})
            return

        elif path == "/api/daemons/remove":
            address = body.get("address")
            if not address:
                self.send_json({"error": "Missing 'address' in body"}, 400)
                return
            state.remove_worker(address)
            self.send_json({"status": "removed", "address": address})
            return

        self.send_json({"error": "Not Found"}, 404)

class ThreadingHTTPServer(ThreadingTCPServer, HTTPServer):
    allow_reuse_address = True

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Hailstone Distributed Search Controller")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Port to run the controller Web UI on")
    parser.add_argument("--workers", type=str, default="", help="Comma-separated list of worker daemon host:ports")
    parser.add_argument("--checkpoint", type=str, default=DEFAULT_CHECKPOINT, help="Consolidated checkpoint file path")
    parser.add_argument("--log", type=str, default=None, help="File path to log controller output to")
    args = parser.parse_args()

    # Initialize Logger
    if args.log:
        global log_file_path
        log_file_path = os.path.abspath(args.log)

    # Initialize Controller State
    global state
    state = ControllerState(checkpoint_path=args.checkpoint)

    # Register CLI workers
    if args.workers:
        for addr in args.workers.split(","):
            addr = addr.strip()
            if addr:
                state.add_worker(addr)
    else:
        # Default fallback to register local daemon
        state.add_worker("localhost:5429")

    # Start scheduling background thread
    sched_thread = threading.Thread(target=background_scheduler)
    sched_thread.daemon = True
    sched_thread.start()

    server = ThreadingHTTPServer(("0.0.0.0", args.port), ControllerHTTPHandler)
    log(f"Central controller running on http://localhost:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("\nStopping controller server...")
        server.shutdown()
        server.server_close()

if __name__ == "__main__":
    main()
