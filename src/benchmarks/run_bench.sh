#!/bin/bash

echo DDAX 4G, governor perf, rand
LD_LIBRARY_PATH=/home/nesperf/repos/nvml/src/nondebug:/usr/local/lib taskset 0xffffff000000ffffff numactl -N 0 /home/nesperf/repos/nvml/src/benchmarks/pmembench pmembench_rpmem_dd_rand.cfg

sleep 10

echo DDAX 4G, governor perf, seq
LD_LIBRARY_PATH=/home/nesperf/repos/nvml/src/nondebug:/usr/local/lib taskset 0xffffff000000ffffff numactl -N 0 /home/nesperf/repos/nvml/src/benchmarks/pmembench pmembench_rpmem_dd.cfg

sleep 10

echo DDAX 120G, governor perf, seq
LD_LIBRARY_PATH=/home/nesperf/repos/nvml/src/nondebug:/usr/local/lib taskset 0xffffff000000ffffff numactl -N 0 /home/nesperf/repos/nvml/src/benchmarks/pmembench pmembench_rpmem_DD.cfg

sleep 10

echo DRAM DDAX 96, governor perf, seq
PMEM_IS_PMEM_FORCE=1 LD_LIBRARY_PATH=/home/nesperf/repos/nvml/src/nondebug:/usr/local/lib taskset 0xffffff000000ffffff /home/nesperf/repos/nvml/src/benchmarks/pmembench pmembench_rpmem_RR.cfg

sleep 10

another_test.sh 
