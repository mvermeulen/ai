# World Cup 2026 Tracker & Simulator (`wc`)

An advanced C++20 tracker and Monte Carlo simulator for the upcoming 48-team FIFA World Cup 2026. The simulation engine uses an Elo-calibrated Poisson goal model with host-nation advantage, dynamically allocates Round of 32 knockout matchups using FIFA's official tiebreaker algorithms, and features an embedded glassmorphic Web UI dashboard.

---

## Features

- **48-Team Bracket Support**: Fully implements the new 48-team format (12 groups of 4). Automatically handles group standings, ranks third-place qualifiers, and performs a backtracking search constraint-satisfaction mapping to pair the 8 best third-place teams with group winners.
- **Elo Poisson Simulation Engine**: Simulates match outcomes by projecting goal distributions from base rates, team ELO ratings, and host-nation advantages (USA, Canada, and Mexico).
- **Match Progression Importance**: Evaluates the mathematical weight of upcoming games by calculating the change in advancement probabilities for both competitors under win, draw, and loss scenarios.
- **Embedded Web Server**: Multi-threaded socket-based HTTP/REST server serving a premium glassmorphic dashboard showcasing group standings, simulation results, match importance delta matrices, and an interactive "What-If" sandbox.
- **Live Ingestion Feed**: Python script mapping to live feeds (ESPN scoreboard APIs) to sync current match scores and statuses into the tracker.

---

## Requirements

- **C++ Compiler**: `g++` (version 10+ supporting C++20)
- **CMake**: Version 3.16+
- **OpenMP**: For parallelizing Monte Carlo simulations
- **Python 3**: For live scores ingestion script

---

## Compilation

You can build the project using CMake directly or via the convenience wrapper script `./build.sh`:

### Option A: Using CMake Directly
```bash
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```
This builds `wc` and `wc_tests` inside the `build` directory.

### Option B: Using the Helper Script
```bash
chmod +x build.sh
./build.sh
```
This builds using CMake and automatically copies the executables to the root directory for convenience.

---

## Running Unit Tests

Run the test suite built on the Catch framework to verify Elo simulation, shootout coin-flips, tiebreakers, and parsing logic:

```bash
./wc_tests
```

---

## Command Line Usage

Run the main application binary `./wc` with one of the following commands:

### 1. Calibrate Model Parameters
Calibrates the Poisson Elo model coefficients from past World Cup match data (2002–2022) and saves the results to `data/model_coefficients.csv`:
```bash
./wc backfit-model
```

### 2. View Group Standings
Computes and prints the current ASCII group standings including points, goal difference (GD), goals for (GF), and qualification status:
```bash
./wc status
```

### 3. Run Tournament Simulations
Runs Monte Carlo simulations to calculate advancement probabilities for every team across all tournament stages:
```bash
# Simulates 10,000 iterations (uses OpenMP multi-threading if available)
./wc simulate 10000
```

### 4. Analyze Match Importance
Calculates how unplayed group-stage matches impact teams' chances of advancing to the Round of 32:
```bash
./wc impact 1000
```

### 5. Sync Live Scores
Calls the Python score ingestion script to synchronize the schedule with ESPN scoreboard APIs:
```bash
./wc fetch-live
```

### 6. Start Web Dashboard
Launches the embedded HTTP server to host the interactive glassmorphic dashboard:
```bash
./wc web 8080
```
Then navigate to `http://localhost:8080` in your web browser.

---

## Project Structure

- `src/` - C++ Source Code
  - `model/` - Domain logic (`Team`, `Match`, `Tournament`, `Tiebreaker`, `MonteCarlo`)
  - `util/` - Utilities (`CsvParser`)
  - `output/` - Presentation layers (`AsciiPrinter`, socket-based `WebServer` and HTML assets)
- `tests/` - Unit tests
- `data/` - Teams CSV, schedule CSV, historical calibration CSVs, and model parameters
- `scripts/` - Scripting utilities (`fetch_live_scores.py` score sync tool)
- `build.sh` - Simple g++ build driver
- `CMakeLists.txt` - CMake configuration file
