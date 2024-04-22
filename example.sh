#!/bin/bash
perf stat --per-node -a -e task-clock,cycles,instructions,branches,branch-misses -e  mem_load_l3_miss_retired.remote_fwd,mem_load_l3_miss_retired.remote_hitm -e cache-references,cache-misses -e LLC-loads,LLC-load-misses,LLC-stores,LLC-store-misses -e L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,mem_inst_retired.all_loads,mem_inst_retired.all_stores,mem_load_l3_miss_retired.local_dram,mem_load_l3_miss_retired.remote_dram