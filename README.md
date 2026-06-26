# AI & High-Performance Computation Suite

A collection of AI-related, simulation, and high-performance calculation projects, ranging from local LLM translation tools to Monte Carlo sports simulators and vectorized math search engines.

Each subfolder contains its own self-contained codebase and comprehensive documentation.

---

## Project Catalog

### 1. TranslateGemma Tooling (`translate/`)
* **Description**: Wrapper utilities and applications to run local text and document translation using Google's **TranslateGemma** model via **Ollama**.
* **Features**:
  - Premium Web UI with interactive source/target language selection, drag-and-drop batch upload, file staging, and live prompt preview.
  - Multi-parameter Python CLI wrapper supporting `stdin`, `stdout`, and direct file reads/writes.
  - Quick-use Bash entrypoint.
* **Documentation**: See [translate/README.md](file:///home/mev/source/ai/translate/README.md) for installation and usage instructions.

---

### 2. World Cup 2026 Tracker & Simulator (`worldcup/`)
* **Description**: An advanced C++20 tracker and Monte Carlo simulator for the upcoming 48-team FIFA World Cup 2026.
* **Features**:
  - Elo-calibrated Poisson goal model with host-nation advantage adjustments.
  - Full simulation engine projecting team advancement probabilities across all tournament phases.
  - Ingestion script to pull real-time score feeds via ESPN Scoreboard APIs.
  - Embedded multi-threaded HTTP/REST web server with a glassmorphic dashboard showcasing group tables, bracket configurations, and what-if sandboxes.
* **Documentation**: See [worldcup/README.md](file:///home/mev/source/ai/worldcup/README.md) for details on build requirements, CLI commands, and server setup.

---

### 3. Hailstone (Collatz) Search Program (`hailstone/`)
* **Description**: A multi-backend, high-performance Collatz (3x+1) peak search engine searching for new peak trajectories beyond $2^{64}$.
* **Features**:
  - CPU (OpenMP, AVX-512 SIMD vectorization), Vulkan Compute, and AMD HIP (ROCm) GPU backends.
  - Custom 128-bit unsigned integer math structure (`uint128`).
  - Heavy performance optimization via Vermeulen Polynomial filter tables, early loop exits, and suffix-first search pruning.
  - Distributed search mode: dynamically clusters tasks via high-performance C++ TCP daemons (`hailstoned`) and a Python coordinator controller with Web monitoring.
* **Documentation**: See [hailstone/README.md](file:///home/mev/source/ai/hailstone/README.md) for architectural documentation, performance benchmarks, and run setup instructions.

---

### 4. BillMinder (`billminder/`)
* **Description**: A personal system for tracking upcoming and projected bills, ensuring obligations are paid on time, and providing forward visibility into expected cash flow.
* **Features**:
  - C++20 Core Service handling SQLite persistence, scheduling logic, and HTTP API serving.
  - C++ CLI tool for interacting with the Core Service via REST API.
  - Vanilla JS, HTML, CSS Web Dashboard.
  - Automated weekly email notifications for upcoming bills.
* **Documentation**: See [billminder/README.md](file:///home/mev/source/ai/billminder/README.md) for quick start instructions, architecture, and configuration.

---

### 5. Local Deep Research (`local-deep-research/`)
* **Description**: Upstream project with local customizations.
* **Type**: Git Submodule tracking `mev-custom` branch.
* **Documentation**: See [local-deep-research/README.md](file:///home/mev/source/ai/local-deep-research/README.md).

---

### 6. Ollama Model Lab (`ollama-model-lab/`)
* **Description**: Upstream project with local customizations.
* **Type**: Git Submodule tracking `mev-custom` branch.
* **Documentation**: See [ollama-model-lab/README.md](file:///home/mev/source/ai/ollama-model-lab/README.md).

---

## Directory Structure

```text
ai/
├── billminder/            # Personal bill tracking, scheduling, and cash flow visibility system
├── hailstone/             # High-performance Collatz peak search program
├── local-deep-research/   # Upstream project with local customizations (submodule)
├── ollama-model-lab/      # Upstream project with local customizations (submodule)
├── translate/             # TranslateGemma local LLM translation wrappers & UI
└── worldcup/              # FIFA World Cup 2026 tracker, simulator & web server
```
