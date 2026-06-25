#!/bin/bash
set -e

# Ensure we're in the project root
cd "$(dirname "$0")"

echo "=== Checking Dependencies ==="
if ! command -v cmake &> /dev/null; then
    echo "❌ Error: cmake is not installed."
    echo "Please install it: sudo apt-get install cmake"
    exit 1
fi

if ! dpkg -s libcurl4-openssl-dev &> /dev/null; then
    echo "❌ Error: libcurl4-openssl-dev is not installed."
    echo "Please install it: sudo apt-get install -y libcurl4-openssl-dev"
    exit 1
fi

echo "=== Building BillMinder ==="
mkdir -p build
cd build
cmake ..
make -j$(nproc)
cd ..

echo ""
echo "=== Starting Services ==="
mkdir -p data

echo "Starting Backend API Service on port 8080..."
./build/core/billminder_core &
BACKEND_PID=$!

echo "Starting Web UI on port 8000..."
python3 -m http.server 8000 -d ui &
UI_PID=$!

echo ""
echo "====================================================="
echo "✅ BillMinder is up and running!"
echo "🌐 Open your browser to: http://localhost:8000"
echo "💻 Access the CLI via: ./build/cli/billminder_cli"
echo "====================================================="
echo "Press Ctrl+C to stop services."

# Trap Ctrl+C (SIGINT) and SIGTERM to kill background processes
trap "echo -e '\nStopping BillMinder services...'; kill $BACKEND_PID $UI_PID 2>/dev/null; exit 0" INT TERM

# Wait for all background processes indefinitely
wait
