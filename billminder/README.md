# BillMinder

BillMinder is a personal system for tracking upcoming and projected bills, helping ensure obligations are paid on time, and giving forward visibility into expected cash flow. 

## Architecture
- **Core Service**: Written in C++20. Handles SQLite persistence, scheduling logic, and HTTP API serving.
- **CLI**: Written in C++. Interacts with the Core Service via REST API.
- **Web UI**: Vanilla JS, HTML, CSS only.

## Quick Start

The easiest way to get BillMinder running is using the provided startup script. This will compile the C++ binaries, boot up the local SQLite database service, and launch the Web Dashboard.

```bash
# Compile and start all services
./start.sh
```

Once running, you can immediately start entering bills:
- **Web Dashboard**: Open `http://localhost:8000` in your browser.
- **CLI Tool**: Keep the services running in one terminal, and in another terminal run:
  ```bash
  ./build/cli/billminder_cli list
  ./build/cli/billminder_cli add "Rent" 1500.00 2026-07-01 monthly
  ```

### Manual Building
If you prefer to build manually:
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Security Model
- **Local-First**: Operational data stays on the local machine.
- **Data Privacy**: No passwords, tokens, or personal identifiers are stored in the repo.
- **Private Data Directory**: The database file (`bills.sqlite`) resides in the `data/` directory which is strictly ignored by version control.
