#!/usr/bin/env python3
import os
import sys
import json
import threading
import subprocess
import time
import re
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingTCPServer
import urllib.parse

# Configuration
DEFAULT_PORT = 5000

class DaemonState:
    def __init__(self):
        self.lock = threading.Lock()
        self.active_job = None       # dict of current job details or None
        self.active_process = None   # subprocess.Popen object or None
        self.job_results = {}        # job_id -> dict of results
        self.backends = {}           # backend_name -> throughput in M numbers/s (or None)
        
        self.project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        self.build_dir = os.path.join(self.project_dir, "build")
        self.temp_dir = os.path.join(self.project_dir, "distributed", "temp")
        
        # Ensure temp directory exists
        os.makedirs(self.temp_dir, exist_ok=True)

    def run_benchmarks(self):
        print("=== Running Startup Micro-benchmarks ===")
        backends_to_test = [
            {"name": "cpu", "binary": "hailstone_cpu", "extra_args": ["--no-domain-switching"]},
            {"name": "cpu_domain", "binary": "hailstone_cpu", "extra_args": ["--domain-switching"]},
            {"name": "vulkan", "binary": "hailstone_vulkan", "extra_args": []},
            {"name": "hip", "binary": "hailstone_hip", "extra_args": []}
        ]
        
        for config in backends_to_test:
            name = config["name"]
            binary_name = config["binary"]
            path = os.path.join(self.build_dir, binary_name)
            if not os.path.exists(path):
                print(f"Backend '{name}' not found (missing binary: {binary_name})")
                self.backends[name] = None
                continue
            
            print(f"Benchmarking backend '{name}'... ", end="", flush=True)
            try:
                # Run a quick 5M range search to measure actual throughput
                cmd = [path, "--no-checkpoint", "--start-num", "3", "--end-num", "5000003", "--cutoff-width", "20"] + config["extra_args"]
                proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10)
                if proc.returncode == 0:
                    match = re.search(r"Throughput:\s+([\d.]+)\s+M numbers/s", proc.stdout)
                    if match:
                        throughput = float(match.group(1))
                        print(f"Success! Throughput: {throughput:.2f} M/s")
                        self.backends[name] = throughput
                        continue
                print(f"Failed (exit code {proc.returncode})")
                self.backends[name] = None
            except subprocess.TimeoutExpired:
                print("Failed (timeout)")
                self.backends[name] = None
            except Exception as e:
                print(f"Failed (error: {e})")
                self.backends[name] = None
        print("=== Benchmarks Completed ===\n")

    def get_system_stats(self):
        # Gather basic CPU / Core details
        cpu_cores = os.cpu_count() or 1
        system_load = [0.0, 0.0, 0.0]
        if hasattr(os, 'getloadavg'):
            try:
                system_load = list(os.getloadavg())
            except Exception:
                pass
        return {
            "cpu_cores": cpu_cores,
            "system_load": system_load
        }

    def parse_stdout(self, stdout):
        results = {
            "elapsed_seconds": 0.0,
            "numbers_checked": 0,
            "steps_computed": 0,
            "throughput_m_numbers_s": 0.0
        }
        for line in stdout.splitlines():
            line = line.strip()
            if "Elapsed Time:" in line:
                val = line.split(":", 1)[1].replace("s", "").strip()
                results["elapsed_seconds"] = float(val)
            elif "Kernel Execution Time:" in line:
                val = line.split(":", 1)[1].replace("ms", "").strip()
                results["elapsed_seconds"] = float(val) / 1000.0
            elif "Numbers Checked:" in line:
                results["numbers_checked"] = int(line.split(":", 1)[1].strip())
            elif "Steps Computed:" in line:
                results["steps_computed"] = int(line.split(":", 1)[1].strip())
            elif "Throughput:" in line:
                val = line.split(":", 1)[1].replace("M numbers/s", "").strip()
                results["throughput_m_numbers_s"] = float(val)
        return results

# Singleton state
state = DaemonState()

class DaemonHTTPHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        # Quiet requests logging
        pass

    def send_json(self, data, status_code=200):
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode("utf-8"))

    def do_GET(self):
        url_parsed = urllib.parse.urlparse(self.path)
        path = url_parsed.path

        if path == "/status":
            with state.lock:
                stats = state.get_system_stats()
                res = {
                    "status": "searching" if state.active_job else "idle",
                    "cpu_cores": stats["cpu_cores"],
                    "system_load": stats["system_load"],
                    "backends": state.backends,
                    "current_job": state.active_job
                }
                self.send_json(res)
            return

        elif path.startswith("/api/job/"):
            job_id = path.split("/")[-1]
            with state.lock:
                if state.active_job and state.active_job["job_id"] == job_id:
                    self.send_json({"status": "running", "job_id": job_id})
                    return
                elif job_id in state.job_results:
                    self.send_json(state.job_results[job_id])
                    return
                else:
                    self.send_json({"error": f"Job {job_id} not found"}, 404)
                    return

        self.send_json({"error": "Not Found"}, 404)

    def do_POST(self):
        url_parsed = urllib.parse.urlparse(self.path)
        path = url_parsed.path

        # Read POST body
        content_length = int(self.headers.get("Content-Length", 0))
        post_data = self.rfile.read(content_length) if content_length > 0 else b""
        
        try:
            body = json.loads(post_data.decode("utf-8")) if post_data else {}
        except Exception as e:
            self.send_json({"error": f"Invalid JSON body: {e}"}, 400)
            return

        if path == "/api/search":
            # Extract arguments
            job_id = body.get("job_id")
            start_num = body.get("start_num")
            end_num = body.get("end_num")
            backend = body.get("backend", "cpu")
            cutoff_width = body.get("cutoff_width", 20)
            checkpoint_data = body.get("checkpoint_data", "")

            if not job_id or not start_num or not end_num:
                self.send_json({"error": "Missing parameters 'job_id', 'start_num', or 'end_num'"}, 400)
                return

            if backend not in state.backends or state.backends[backend] is None:
                self.send_json({"error": f"Backend '{backend}' is not available on this worker"}, 400)
                return

            with state.lock:
                if state.active_job is not None:
                    self.send_json({"error": "Daemon is busy with another job"}, 409)
                    return

                state.active_job = {
                    "job_id": job_id,
                    "start_num": start_num,
                    "end_num": end_num,
                    "backend": backend,
                    "cutoff_width": cutoff_width,
                    "start_time": time.time()
                }

            # Launch job thread
            thread = threading.Thread(target=self.execute_job, args=(job_id, start_num, end_num, backend, cutoff_width, checkpoint_data))
            thread.daemon = True
            thread.start()

            self.send_json({"status": "queued", "job_id": job_id}, 202)
            return

        elif path == "/api/cancel":
            with state.lock:
                if state.active_job is None:
                    self.send_json({"status": "idle", "message": "No active job to cancel"})
                    return
                
                print(f"Cancelling active job: {state.active_job['job_id']}")
                if state.active_process:
                    try:
                        state.active_process.terminate()
                        # Wait briefly and kill if not terminated
                        time.sleep(0.5)
                        if state.active_process.poll() is None:
                            state.active_process.kill()
                    except Exception as e:
                        print(f"Error terminating process: {e}")

                job_id = state.active_job["job_id"]
                state.job_results[job_id] = {
                    "status": "failed",
                    "error": "Job was cancelled by host request"
                }
                state.active_job = None
                state.active_process = None
            
            self.send_json({"status": "cancelled", "job_id": job_id})
            return

        self.send_json({"error": "Not Found"}, 404)

    def execute_job(self, job_id, start_num, end_num, backend, cutoff_width, checkpoint_data):
        chk_filepath = os.path.join(state.temp_dir, f"{job_id}.chk")
        
        # Write starting checkpoint file
        try:
            with open(chk_filepath, "w") as f:
                f.write(checkpoint_data)
        except Exception as e:
            self.finalize_job(job_id, "failed", error=f"Could not create input checkpoint file: {e}")
            return

        actual_backend = backend
        extra_args = []
        if backend == "cpu_domain":
            actual_backend = "cpu"
            extra_args = ["--domain-switching"]
        elif backend == "cpu":
            extra_args = ["--no-domain-switching"]

        binary_name = f"hailstone_{actual_backend}"
        executable_path = os.path.join(state.build_dir, binary_name)

        cmd = [
            executable_path,
            "--checkpoint", chk_filepath,
            "--start-num", str(start_num),
            "--end-num", str(end_num),
            "--cutoff-width", str(cutoff_width)
        ] + extra_args

        print(f"Running job {job_id} [{start_num}, {end_num}] using {binary_name} {' '.join(extra_args)}...")
        
        proc = None
        try:
            with state.lock:
                # Double check that we weren't cancelled while setting up
                if state.active_job is None or state.active_job["job_id"] != job_id:
                    self.cleanup_file(chk_filepath)
                    return
                state.active_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, cwd=state.build_dir)
                proc = state.active_process

            stdout, stderr = proc.communicate()
            
            # Verify if process was terminated/cancelled in the meantime
            with state.lock:
                if state.active_job is None or state.active_job["job_id"] != job_id:
                    self.cleanup_file(chk_filepath)
                    return

            if proc.returncode == 0:
                # Read updated checkpoint file
                updated_checkpoint = ""
                try:
                    with open(chk_filepath, "r") as f:
                        updated_checkpoint = f.read()
                except Exception as e:
                    self.finalize_job(job_id, "failed", error=f"Could not read updated checkpoint file: {e}")
                    self.cleanup_file(chk_filepath)
                    return

                metrics = state.parse_stdout(stdout)
                self.finalize_job(
                    job_id, 
                    "completed", 
                    elapsed_seconds=metrics["elapsed_seconds"],
                    numbers_checked=metrics["numbers_checked"],
                    steps_computed=metrics["steps_computed"],
                    throughput=metrics["throughput_m_numbers_s"],
                    checkpoint_data=updated_checkpoint
                )
            else:
                self.finalize_job(job_id, "failed", error=f"Process exited with code {proc.returncode}. Stderr: {stderr}")

        except Exception as e:
            self.finalize_job(job_id, "failed", error=f"Error executing subprocess: {e}")
        finally:
            self.cleanup_file(chk_filepath)

    def finalize_job(self, job_id, status, error=None, elapsed_seconds=0.0, numbers_checked=0, steps_computed=0, throughput=0.0, checkpoint_data=""):
        with state.lock:
            # Only finalize if this is the active job (avoid race condition with cancels)
            if state.active_job and state.active_job["job_id"] == job_id:
                if status == "completed":
                    result = {
                        "status": "completed",
                        "elapsed_seconds": elapsed_seconds,
                        "numbers_checked": numbers_checked,
                        "steps_computed": steps_computed,
                        "throughput_m_numbers_s": throughput,
                        "checkpoint_data": checkpoint_data
                    }
                else:
                    result = {
                        "status": "failed",
                        "error": error
                    }
                
                state.job_results[job_id] = result
                state.active_job = None
                state.active_process = None
                
                # Limit memory storage of completed jobs
                if len(state.job_results) > 50:
                    oldest_job = next(iter(state.job_results))
                    state.job_results.pop(oldest_job)

                print(f"Job {job_id} finalized as: {status}")

    def cleanup_file(self, filepath):
        try:
            if os.path.exists(filepath):
                os.remove(filepath)
        except Exception as e:
            print(f"Error removing temporary file {filepath}: {e}")

class ThreadingHTTPServer(ThreadingTCPServer, HTTPServer):
    # Allow port reuse
    allow_reuse_address = True

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Hailstone Distributed Search Daemon")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Port to run the daemon on")
    args = parser.parse_args()

    # Discover and benchmark binaries
    state.run_benchmarks()

    server = ThreadingHTTPServer(("0.0.0.0", args.port), DaemonHTTPHandler)
    print(f"Worker daemon running on http://localhost:{args.port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping worker daemon...")
        server.shutdown()
        server.server_close()

if __name__ == "__main__":
    main()
