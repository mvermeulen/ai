#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

std::string get_project_dir() {
    // Assuming binary is in build/ or /usr/bin/
    // Actually, let's just use relative to current working dir or common paths
    std::vector<std::string> search_paths = { "./", "./build/", "../build/", "/usr/bin/" };
    for (const auto& p : search_paths) {
        if (access((p + "hailstone_cpu").c_str(), X_OK) == 0) {
            return p;
        }
    }
    return "./";
}

std::string exec_and_get_output(const std::string& cmd, int timeout_sec = 10) {
    // Simple popen wrapper with timeout (via timeout command)
    std::string full_cmd = "timeout " + std::to_string(timeout_sec) + " " + cmd + " 2>/dev/null";
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

std::string get_capabilities_json() {
    std::string cache_path = "/tmp/hailstoned_benchmarks.cache";
    std::ifstream cache_in(cache_path);
    if (cache_in.good()) {
        std::stringstream buffer;
        buffer << cache_in.rdbuf();
        return buffer.str();
    }

    std::string bin_dir = get_project_dir();
    struct BackendConfig {
        std::string name;
        std::string binary;
        std::string extra_args;
    };
    std::vector<BackendConfig> backends = {
        {"cpu", "hailstone_cpu", " --use-avx512"},
        {"cpu_domain", "hailstone_cpu", " --domain-switching --use-avx512"},
        {"cpu_nosteps", "hailstone_cpu_nosteps", " --use-avx512"},
        {"cpu_domain_nosteps", "hailstone_cpu_nosteps", " --domain-switching --use-avx512"},
        {"vulkan", "hailstone_vulkan", ""},
        {"vulkan_domain", "hailstone_vulkan", " --domain-switching"},
        {"vulkan_nosteps", "hailstone_vulkan_nosteps", ""},
        {"vulkan_domain_nosteps", "hailstone_vulkan_nosteps", " --domain-switching"},
        {"hip", "hailstone_hip", ""},
        {"hip_domain", "hailstone_hip", " --domain-switching"},
        {"hip_nosteps", "hailstone_hip_nosteps", ""},
        {"hip_domain_nosteps", "hailstone_hip_nosteps", " --domain-switching"}
    };
    std::string json = "{";
    bool first = true;
    for (const auto& b : backends) {
        std::string path = bin_dir + b.binary;
        if (access(path.c_str(), X_OK) == 0) {
            std::string cmd = path + " --no-checkpoint --start-num 3 --end-num 5000003 --cutoff-width 20" + b.extra_args;
            std::string out = exec_and_get_output(cmd);
            std::smatch match;
            std::regex rgx("Throughput:\\s+([\\d\\.]+)\\s+M numbers/s");
            if (std::regex_search(out, match, rgx) && match.size() > 1) {
                if (!first) json += ", ";
                json += "\"" + b.name + "\": " + match.str(1);
                first = false;
            }
        }
    }
    json += "}";
    
    std::ofstream cache_out(cache_path);
    if (cache_out.good()) {
        cache_out << json;
    }
    return json;
}

std::string get_status_json() {
    double loadavg[3] = {0.0, 0.0, 0.0};
    getloadavg(loadavg, 3);
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    
    std::string lock_path = "/tmp/hailstoned.lock";
    std::ifstream lock_in(lock_path);
    std::string status = "idle";
    std::string job_json = "null";
    
    if (lock_in.good()) {
        std::string pid_str, backend, start_n, end_n;
        if (lock_in >> pid_str >> backend >> start_n >> end_n) {
            // Check if PID is actually running
            std::string proc_path = "/proc/" + pid_str;
            struct stat sts;
            if (stat(proc_path.c_str(), &sts) == 0) {
                status = "busy";
                job_json = "{\"job_id\": \"active\", \"backend\": \"" + backend + "\", \"start_num\": \"" + start_n + "\", \"end_num\": \"" + end_n + "\"}";
            } else {
                // Stale lock
                unlink(lock_path.c_str());
            }
        }
    }
    
    std::string json = "{";
    json += "\"status\": \"" + status + "\", ";
    json += "\"cpu_cores\": " + std::to_string(nprocs) + ", ";
    json += "\"system_load\": [" + std::to_string(loadavg[0]) + ", " + std::to_string(loadavg[1]) + ", " + std::to_string(loadavg[2]) + "], ";
    json += "\"backends\": " + get_capabilities_json() + ", ";
    json += "\"current_job\": " + job_json;
    json += "}";
    return json;
}

void handle_client(int client_fd) {
    char buffer[4096];
    std::string request_data;
    
    // Read the first line for the command
    while (true) {
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer)-1, 0);
        if (bytes <= 0) {
            close(client_fd);
            return;
        }
        buffer[bytes] = '\0';
        request_data += buffer;
        if (request_data.find("\n") != std::string::npos) {
            break;
        }
    }
    
    std::istringstream iss(request_data);
    std::string cmd;
    iss >> cmd;
    
    if (cmd == "CAPABILITIES") {
        std::string resp = get_capabilities_json() + "\n";
        send(client_fd, resp.c_str(), resp.length(), 0);
        close(client_fd);
    } 
    else if (cmd == "STATUS") {
        std::string resp = get_status_json() + "\n";
        send(client_fd, resp.c_str(), resp.length(), 0);
        close(client_fd);
    }
    else if (cmd == "BENCHMARK") {
        std::string backend, start_n, end_n, cutoff;
        iss >> backend >> start_n >> end_n >> cutoff;
        
        std::string actual_backend = backend;
        bool domain_switching = false;
        if (backend == "cpu_domain") {
            actual_backend = "cpu";
            domain_switching = true;
        } else if (backend == "cpu_nosteps") {
            actual_backend = "cpu_nosteps";
            domain_switching = false;
        } else if (backend == "cpu_domain_nosteps") {
            actual_backend = "cpu_nosteps";
            domain_switching = true;
        } else if (backend == "vulkan_domain") {
            actual_backend = "vulkan";
            domain_switching = true;
        } else if (backend == "vulkan_nosteps") {
            actual_backend = "vulkan_nosteps";
            domain_switching = false;
        } else if (backend == "vulkan_domain_nosteps") {
            actual_backend = "vulkan_nosteps";
            domain_switching = true;
        } else if (backend == "hip_domain") {
            actual_backend = "hip";
            domain_switching = true;
        } else if (backend == "hip_nosteps") {
            actual_backend = "hip_nosteps";
            domain_switching = false;
        } else if (backend == "hip_domain_nosteps") {
            actual_backend = "hip_nosteps";
            domain_switching = true;
        }

        std::string bin_path = get_project_dir() + "hailstone_" + actual_backend;
        
        int pipe_fd[2];
        if (pipe(pipe_fd) == -1) {
            std::string resp = "{\"status\": \"failed\", \"error\": \"pipe failed\"}\n";
            send(client_fd, resp.c_str(), resp.length(), MSG_NOSIGNAL);
            close(client_fd);
            return;
        }
        
        pid_t child_pid = fork();
        if (child_pid == 0) {
            // Child
            close(pipe_fd[0]);
            dup2(pipe_fd[1], STDOUT_FILENO);
            dup2(pipe_fd[1], STDERR_FILENO);
            close(pipe_fd[1]);
            
            execl(bin_path.c_str(), bin_path.c_str(), "--no-checkpoint", 
                  "--start-num", start_n.c_str(), "--end-num", end_n.c_str(), 
                  "--cutoff-width", cutoff.c_str(), 
                  (domain_switching ? "--domain-switching" : "--no-domain-switching"), 
                  (char*)NULL);
            exit(1);
        } 
        else if (child_pid > 0) {
            // Parent
            close(pipe_fd[1]);
            
            std::string out_data;
            while (true) {
                ssize_t bytes = read(pipe_fd[0], buffer, sizeof(buffer)-1);
                if (bytes <= 0) break;
                buffer[bytes] = '\0';
                out_data += buffer;
            }
            waitpid(child_pid, NULL, 0);
            close(pipe_fd[0]);
            
            double throughput = 0.0;
            std::smatch match;
            std::regex rgx("Throughput:\\s+([\\d\\.]+)\\s+M numbers/s");
            if (std::regex_search(out_data, match, rgx) && match.size() > 1) {
                throughput = std::stod(match.str(1));
            }
            
            std::string resp = "{\"status\": \"success\", \"throughput\": " + std::to_string(throughput) + "}\n";
            send(client_fd, resp.c_str(), resp.length(), MSG_NOSIGNAL);
            close(client_fd);
        } else {
            std::string resp = "{\"status\": \"failed\", \"error\": \"fork failed\"}\n";
            send(client_fd, resp.c_str(), resp.length(), MSG_NOSIGNAL);
            close(client_fd);
        }
    }
    else if (cmd == "COMPUTE") {
        std::string backend, start_n, end_n, cutoff;
        iss >> backend >> start_n >> end_n >> cutoff;
        
        // Read checkpoint data until __END_CHECKPOINT__
        std::string chk_data;
        std::string end_marker = "__END_CHECKPOINT__";
        std::string rem = request_data.substr(request_data.find("\n") + 1);
        chk_data += rem;
        
        while (chk_data.find(end_marker) == std::string::npos) {
            ssize_t bytes = recv(client_fd, buffer, sizeof(buffer)-1, 0);
            if (bytes <= 0) break;
            buffer[bytes] = '\0';
            chk_data += buffer;
        }
        
        size_t end_pos = chk_data.find(end_marker);
        if (end_pos != std::string::npos) {
            chk_data = chk_data.substr(0, end_pos);
        }
        
        std::string chk_file = "/tmp/hailstoned_job_" + std::to_string(getpid()) + ".chk";
        std::ofstream chk_out(chk_file);
        chk_out << chk_data;
        chk_out.close();
        
        std::string actual_backend = backend;
        bool domain_switching = false;
        if (backend == "cpu_domain") {
            actual_backend = "cpu";
            domain_switching = true;
        } else if (backend == "cpu_nosteps") {
            actual_backend = "cpu_nosteps";
            domain_switching = false;
        } else if (backend == "cpu_domain_nosteps") {
            actual_backend = "cpu_nosteps";
            domain_switching = true;
        } else if (backend == "vulkan_domain") {
            actual_backend = "vulkan";
            domain_switching = true;
        } else if (backend == "vulkan_nosteps") {
            actual_backend = "vulkan_nosteps";
            domain_switching = false;
        } else if (backend == "vulkan_domain_nosteps") {
            actual_backend = "vulkan_nosteps";
            domain_switching = true;
        } else if (backend == "hip_domain") {
            actual_backend = "hip";
            domain_switching = true;
        } else if (backend == "hip_nosteps") {
            actual_backend = "hip_nosteps";
            domain_switching = false;
        } else if (backend == "hip_domain_nosteps") {
            actual_backend = "hip_nosteps";
            domain_switching = true;
        }

        std::string bin_path = get_project_dir() + "hailstone_" + actual_backend;
        
        int pipe_fd[2];
        if (pipe(pipe_fd) == -1) {
            close(client_fd);
            return;
        }
        
        pid_t child_pid = fork();
        if (child_pid == 0) {
            // Child
            close(pipe_fd[0]); // close read end
            dup2(pipe_fd[1], STDOUT_FILENO); // redirect stdout to pipe
            dup2(pipe_fd[1], STDERR_FILENO);
            close(pipe_fd[1]);
            
            if (actual_backend.find("cpu") != std::string::npos) {
                execl(bin_path.c_str(), bin_path.c_str(), "--checkpoint", chk_file.c_str(), 
                      "--start-num", start_n.c_str(), "--end-num", end_n.c_str(), 
                      "--cutoff-width", cutoff.c_str(), 
                      (domain_switching ? "--domain-switching" : "--no-domain-switching"), 
                      "--use-avx512",
                      (char*)NULL);
            } else {
                execl(bin_path.c_str(), bin_path.c_str(), "--checkpoint", chk_file.c_str(), 
                      "--start-num", start_n.c_str(), "--end-num", end_n.c_str(), 
                      "--cutoff-width", cutoff.c_str(), 
                      (domain_switching ? "--domain-switching" : "--no-domain-switching"), 
                      (char*)NULL);
            }
            exit(1);
        } 
        else if (child_pid > 0) {
            // Parent
            close(pipe_fd[1]); // close write end
            
            // Create lock file
            std::ofstream lock_out("/tmp/hailstoned.lock");
            lock_out << child_pid << " " << backend << " " << start_n << " " << end_n << "\n";
            lock_out.close();
            
            struct pollfd pfds[2];
            pfds[0].fd = pipe_fd[0]; // Child's stdout
            pfds[0].events = POLLIN;
            pfds[1].fd = client_fd; // Client socket
            pfds[1].events = POLLIN | POLLHUP | POLLERR;
            
            bool client_active = true;
            while (true) {
                int pret = poll(pfds, 2, 100); // 100ms timeout
                if (pret < 0) break;
                
                // Check child output
                if (pfds[0].revents & POLLIN) {
                    ssize_t bytes = read(pipe_fd[0], buffer, sizeof(buffer));
                    if (bytes > 0) {
                        send(client_fd, buffer, bytes, MSG_NOSIGNAL);
                    } else if (bytes <= 0) {
                        break; // EOF from child
                    }
                }
                
                // Check client connection
                if ((pfds[1].revents & POLLHUP) || (pfds[1].revents & POLLERR)) {
                    client_active = false;
                    break;
                }
                if (pfds[1].revents & POLLIN) {
                    // Client shouldn't be sending anything. If read returns 0, client closed.
                    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
                    if (bytes == 0) {
                        client_active = false;
                        break;
                    }
                }
                
                // Check if child exited
                int status;
                if (waitpid(child_pid, &status, WNOHANG) == child_pid) {
                    break;
                }
            }
            
            if (!client_active) {
                kill(child_pid, SIGTERM);
                usleep(100000); // wait 100ms
                kill(child_pid, SIGKILL);
                waitpid(child_pid, NULL, 0);
            } else {
                // Wait for child to finish normally
                int status = 0;
                waitpid(child_pid, &status, 0);
                
                if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
                    std::string error_msg = "{\"status\": \"failed\", \"error\": \"Process exited with code " + std::to_string(WEXITSTATUS(status)) + "\"}\n";
                    send(client_fd, error_msg.c_str(), error_msg.length(), MSG_NOSIGNAL);
                } else {
                    // Send back checkpoint file
                    std::ifstream chk_in(chk_file);
                    if (chk_in.good()) {
                        std::stringstream buffer;
                        buffer << chk_in.rdbuf();
                        std::string payload = "\n__BEGIN_CHECKPOINT__\n" + buffer.str() + "\n__END_CHECKPOINT__\n";
                        send(client_fd, payload.c_str(), payload.length(), MSG_NOSIGNAL);
                    }
                }
            }
            
            close(pipe_fd[0]);
            unlink(chk_file.c_str());
            unlink("/tmp/hailstoned.lock");
            close(client_fd);
        }
    } else {
        close(client_fd);
    }
}

int main(int argc, char* argv[]) {
    // Try to change directory to where the allowed_suffixes files might be
    if (access("/usr/share/hailstone/allowed_suffixes_24.bin", F_OK) == 0) {
        chdir("/usr/share/hailstone");
    } else if (access("/usr/bin/allowed_suffixes_24.bin", F_OK) == 0) {
        chdir("/usr/bin");
    }

    int port = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--port") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --port requires a numeric argument" << std::endl;
                return 1;
            }
            try {
                port = std::stoi(argv[i + 1]);
            } catch (const std::exception& e) {
                std::cerr << "Error: invalid --port value '" << argv[i + 1] << "' (" << e.what() << ")" << std::endl;
                return 1;
            }
            ++i;
        }
    }

    if (port == 0) {
        // xinetd mode - communicate via stdin/stdout
        // To handle this cleanly without changing handle_client too much,
        // we map STDIN_FILENO to the client_fd. 
        // Wait, socket functions (recv/send) work on stdin if it's a socket.
        // xinetd passes the connected socket as fd 0, 1, and 2.
        handle_client(STDIN_FILENO);
        return 0;
    }

    // Standalone multi-threaded server mode
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Error: socket(AF_INET, SOCK_STREAM) failed: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Error: bind(0.0.0.0:" << port << ") failed: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
        close(server_fd);
        return 1;
    }
    if (listen(server_fd, 10) < 0) {
        std::cerr << "Error: listen() failed: " << strerror(errno) << " (errno=" << errno << ")" << std::endl;
        close(server_fd);
        return 1;
    }

    std::cout << "Standalone hailstoned running on port " << port << std::endl;

    while (true) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            std::thread(handle_client, client_fd).detach();
        }
    }

    close(server_fd);
    return 0;
}
