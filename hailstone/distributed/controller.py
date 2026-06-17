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
    "vulkan": 200.0,
    "hip": 300.0
}

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

def tcp_worker_thread(address, backend, start, end, cutoff, checkpoint_payload, job_id, expected_timeout):
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
        
        if checkpoint_data:
            print(f"[Success] Job {job_id} completed on {address} in {metrics['elapsed_seconds']:.2f}s.")
            state.merge_worker_checkpoint(checkpoint_data)
            with state.lock:
                state.total_numbers_checked += metrics["numbers_checked"]
                state.total_steps_computed += metrics["steps_computed"]
                state.elapsed_seconds += metrics["elapsed_seconds"]
                state.active_jobs.pop(address, None)
                worker = state.workers.get(address)
                if worker:
                    worker["status"] = "online"
        else:
            print(f"[Failed] Job {job_id} failed on {address}: Socket closed before checkpoint received.")
            with state.lock:
                if address in state.active_jobs and state.active_jobs[address]["job_id"] == job_id:
                    state.failed_chunks.append((start, end))
                    state.active_jobs.pop(address, None)
                    worker = state.workers.get(address)
                    if worker and worker["status"] != "offline":
                        worker["status"] = "online"

    except Exception as e:
        print(f"[Failed] Job {job_id} exception on {address}: {e}")
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
            "sigma_peaks": []
        }

        # Load initial checkpoint if it exists
        self.load_checkpoint()

    def add_worker(self, address):
        with self.lock:
            if address not in self.workers:
                self.workers[address] = {
                    "status": "offline",
                    "backends": {},
                    "cpu_cores": 0,
                    "system_load": [0.0, 0.0, 0.0],
                    "error_count": 0,
                    "last_seen": 0.0
                }
                print(f"Registered worker: {address}")

    def remove_worker(self, address):
        with self.lock:
            if address in self.workers:
                del self.workers[address]
                print(f"Removed worker: {address}")

    def load_checkpoint(self):
        if not os.path.exists(self.checkpoint_path):
            print(f"No existing checkpoint found at {self.checkpoint_path}. Starting clean.")
            return
        
        print(f"Loading consolidated checkpoint: {self.checkpoint_path}")
        try:
            res = {
                "last_num": 0,
                "max_value": 0,
                "max_steps": 0,
                "max_sigma": 0,
                "max_value_peaks": [],
                "steps_peaks": [],
                "sigma_peaks": []
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
            print(f"Loaded peak state successfully. Resuming from starting number: {self.next_search_num}")
        except Exception as e:
            print(f"Error reading checkpoint file: {e}")

    def save_checkpoint(self):
        try:
            # Sort and filter peaks to ensure strict mathematical validity
            self.prune_peaks()
            
            # Write standard text-based format
            with open(self.checkpoint_path, "w") as f:
                f.write(f"last_num: {self.global_peaks['last_num']}\n")
                f.write(f"max_value: {self.global_peaks['max_value']}\n")
                f.write(f"max_steps: {self.global_peaks['max_steps']}\n")
                f.write(f"max_sigma: {self.global_peaks['max_sigma']}\n\n")
                
                f.write("max_value_peaks:\n")
                for peak in self.global_peaks["max_value_peaks"]:
                    f.write(f"{peak['start_val']} {peak['metric_val']}\n")
                f.write("\n")
                
                f.write("steps_peaks:\n")
                for peak in self.global_peaks["steps_peaks"]:
                    f.write(f"{peak['start_val']} {peak['metric_val']}\n")
                f.write("\n")
                
                f.write("sigma_peaks:\n")
                for peak in self.global_peaks["sigma_peaks"]:
                    f.write(f"{peak['start_val']} {peak['metric_val']}\n")
                f.write("\n")
            # print(f"Saved consolidated checkpoint to {self.checkpoint_path}")
        except Exception as e:
            print(f"Error saving consolidated checkpoint: {e}")

    def prune_peaks(self):
        # Strictly applies Collatz peak condition to the lists
        for section in ["max_value_peaks", "steps_peaks", "sigma_peaks"]:
            # Deduplicate by start_val
            dedup = {}
            for p in self.global_peaks[section]:
                start = p["start_val"]
                metric = p["metric_val"]
                if start not in dedup or metric > dedup[start]:
                    dedup[start] = metric
            
            # Sort ascending by start_val
            sorted_list = [{"start_val": s, "metric_val": m} for s, m in dedup.items()]
            sorted_list.sort(key=lambda x: x["start_val"])
            
            # Filter strictly increasing
            filtered = []
            max_metric = -1
            for p in sorted_list:
                if p["metric_val"] > max_metric:
                    filtered.append(p)
                    max_metric = p["metric_val"]
            
            self.global_peaks[section] = filtered
        
        # Update header values
        self.global_peaks["max_value"] = self.global_peaks["max_value_peaks"][-1]["metric_val"] if self.global_peaks["max_value_peaks"] else 0
        self.global_peaks["max_steps"] = self.global_peaks["steps_peaks"][-1]["metric_val"] if self.global_peaks["steps_peaks"] else 0
        self.global_peaks["max_sigma"] = self.global_peaks["sigma_peaks"][-1]["metric_val"] if self.global_peaks["sigma_peaks"] else 0

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
        
        return "\n".join(lines)

    def merge_worker_checkpoint(self, checkpoint_text):
        res = {
            "last_num": 0,
            "max_value_peaks": [],
            "steps_peaks": [],
            "sigma_peaks": []
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
            for s in ["max_value_peaks", "steps_peaks", "sigma_peaks"]:
                self.global_peaks[s].extend(res[s])
            
            # Incrementally update last_num
            if res["last_num"] > self.global_peaks["last_num"]:
                self.global_peaks["last_num"] = res["last_num"]
            
            # Prune and save checkpoint file
            self.save_checkpoint()

# Singleton state
state = ControllerState()

def background_scheduler():
    while True:
        try:
            # 1. Health check & update workers state
            for worker_addr in list(state.workers.keys()):
                data, err = tcp_exchange(worker_addr, "STATUS", timeout=1.5)
                
                with state.lock:
                    w = state.workers[worker_addr]
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
            
            # 2. Check running jobs and handle adaptive timeouts
            now = time.time()
            for worker_addr, job in list(state.active_jobs.items()):
                with state.lock:
                    worker = state.workers.get(worker_addr)
                
                # Check for timeout or offline status
                is_offline = worker and worker["status"] == "offline"
                is_timeout = now > job["timeout"]
                
                if is_offline or is_timeout:
                    reason = "went offline" if is_offline else "timed out"
                    print(f"[Warning] Job {job['job_id']} on {worker_addr} {reason}. Re-queuing range [{job['start_num']}, {job['end_num']}].")
                    
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
                cutoff_width = state.task_cutoff_width
                target_duration = state.task_target_duration
                task_end = state.task_end_num

            if run_scheduler:
                # Find online, idle workers
                for worker_addr, worker in state.workers.items():
                    if worker["status"] == "online":
                        # Does it support our backend?
                        throughput = worker["backends"].get(backend)
                        if throughput is None or throughput == 0:
                            continue # Backend unsupported on worker
                        
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
                            
                            print(f"Dispatching range [{start}, {end}] to {worker_addr} (expected run: {expected_run:.1f}s, timeout: {timeout_dur:.1f}s)...")
                            
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
                                worker_addr, backend, start, end, cutoff_width, checkpoint_payload, job_id, timeout_dur))
                            t.daemon = True
                            t.start()
            
            # Check if everything is finished
            with state.lock:
                if state.is_running and state.next_search_num > task_end and not state.active_jobs and not state.failed_chunks:
                    print("=== Distributed Search Completed successfully! ===")
                    state.is_running = False
                    
        except Exception as e:
            print(f"Scheduler exception: {e}")
            
        time.sleep(1.0)

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
                        combined_throughput += worker["backends"].get(state.task_backend, 0.0)

                res = {
                    "is_running": state.is_running,
                    "task": {
                        "start_num": str(state.task_start_num),
                        "end_num": str(state.task_end_num),
                        "backend": state.task_backend,
                        "cutoff_width": state.task_cutoff_width,
                        "target_duration": state.task_target_duration
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
                
                start_num = int(body.get("start_num", 3))
                end_num = int(body.get("end_num", 100000))
                backend = body.get("backend", "cpu")
                cutoff_width = int(body.get("cutoff_width", 20))
                target_duration = float(body.get("target_duration", 1.0))

                if start_num < 3 or end_num < start_num:
                    self.send_json({"error": "Invalid start/end numbers"}, 400)
                    return

                # Configure Task
                state.task_start_num = start_num
                state.task_end_num = end_num
                state.task_backend = backend
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
                print(f"=== Starting Distributed Search: [{start_num}, {end_num}] via {backend} ===")
                
            self.send_json({"status": "started"})
            return

        elif path == "/api/stop" or path == "/api/cancel":
            with state.lock:
                if not state.is_running:
                    self.send_json({"status": "idle", "message": "Search is not currently running"})
                    return
                
                print("Stopping distributed search on request...")
                state.is_running = False
                
                # Cancel all running jobs on workers
                for worker_addr, job in list(state.active_jobs.items()):
                    print(f"Cancelling job {job['job_id']} on {worker_addr}...")
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
    args = parser.parse_args()

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
    print(f"Central controller running on http://localhost:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping controller server...")
        server.shutdown()
        server.server_close()

if __name__ == "__main__":
    main()
