#!/bin/bash
set -e

echo "=== Building wc (World Cup Tracker & Simulator) with CMake ==="
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
cd ..

# Copy binaries to root directory to maintain CLI compatibility
rm -f ./wc ./wc_tests
cp build/wc .
cp build/wc_tests .

echo "Build successful! Run ./wc_tests to run unit tests."
