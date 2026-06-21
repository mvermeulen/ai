#!/bin/bash
# run_openmp_experiment.sh
# Tests scaling of the CPU AVX-512 backend with OMIT_STEPS_COMPUTATION under OpenMP.

# Search 320 blocks (perfectly divisible by 32 cores) starting from block 100,000.
# 320 blocks * 2^32 = ~1.37 Trillion starting numbers.
START_BLOCK=100000
NUM_BLOCKS=320

echo "=========================================================="
echo "Running Baseline: 1 Thread (OMP_NUM_THREADS=1)"
echo "Checking $NUM_BLOCKS blocks (~1.37 Trillion values)"
echo "=========================================================="
export OMP_NUM_THREADS=1
time ./build/hailstone_cpu --no-checkpoint --no-save-checkpoint --use-avx512 --domain-switching --cutoff-width 24 --start-block $START_BLOCK --num-blocks $NUM_BLOCKS > baseline_1_thread.log 2>&1
grep -E "Elapsed Time|Numbers Checked|Throughput" baseline_1_thread.log

echo ""
echo "=========================================================="
echo "Running Parallel: All Cores (OMP_NUM_THREADS=32)"
echo "Checking $NUM_BLOCKS blocks (~1.37 Trillion values)"
echo "=========================================================="
export OMP_NUM_THREADS=32
time ./build/hailstone_cpu --no-checkpoint --no-save-checkpoint --use-avx512 --domain-switching --cutoff-width 24 --start-block $START_BLOCK --num-blocks $NUM_BLOCKS > parallel_max_threads.log 2>&1
grep -E "Elapsed Time|Numbers Checked|Throughput" parallel_max_threads.log
