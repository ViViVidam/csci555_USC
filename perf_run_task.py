import random
import os
import sys
import subprocess
import time
max_usr_cnt = 2
# total_usr_cnt = 100
targets = ["bt", "cg", "dc", "ep", "ft", "is", "lu", "mg", "sp", "ua"]



def RunTask(run_seq: bool) -> None:
    processPool = []
    os.chdir("NPB3.4.2/NPB3.4-OMP/bin/")
    files = os.listdir()
    for i in range(max_usr_cnt):
        for file in files:
            if file.endswith(".x"):
                fout = open(f"{file}.{i}_output", "w")
                perf_out = open(f"{file}.{i}_output_perf", "w")
                ret = subprocess.Popen(["perf","stat","--per-node","-o",f"{perf_out}","-a","-e","task-clock,cycles,instructions,mem_load_l3_miss_retired.remote_fwd,mem_load_l3_miss_retired.remote_hitm,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,mem_inst_retired.all_loads,mem_inst_retired.all_stores,mem_load_l3_miss_retired.local_dram,mem_load_l3_miss_retired.remote_dram",f"./{file}"], stdout=fout)
                if run_seq:
                    ret.wait()
                    print(ret.stderr)
                else:
                    time.sleep(10)
                processPool.append(ret)
        if not run_seq:
            for process in processPool:
                process.wait()
            processPool.clear()
        print(f"round {i}")
    os.chdir("../../../")


def RunTaskThanos(run_seq: bool) -> None:
    processPool = []
    os.chdir("build")
    files = os.listdir("../NPB3.4.2/NPB3.4-OMP/bin/")
    for i in range(max_usr_cnt):
        for file in files:
            if file.endswith(".x"):
                fout = open(f"../NPB3.4.2/NPB3.4-OMP/bin/{file}.{i}_output", "w")
                perf_out = open(f"{file}.{i}_output_perf", "w")
                ret = subprocess.Popen(["perf","stat","--per-node","-o",f"{perf_out}","-a","-e","task-clock,cycles,instructions,mem_load_l3_miss_retired.remote_fwd,mem_load_l3_miss_retired.remote_hitm,cache-references,cache-misses,L1-dcache-loads,L1-dcache-load-misses,L1-dcache-stores,L1-dcache-store-misses,mem_inst_retired.all_loads,mem_inst_retired.all_stores,mem_load_l3_miss_retired.local_dram,mem_load_l3_miss_retired.remote_dram", "./thanos","-v 1",f"../NPB3.4.2/NPB3.4-OMP/bin/{file}"], stdout=fout)
                if run_seq:
                    ret.wait()
                    print(ret.stderr)
                else:
                    processPool.append(ret)
                    time.sleep(10)
        if not run_seq:
            for process in processPool:
                process.wait()
            processPool.clear()
        print(f"round {i}")
    os.chdir("../../../")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("mode: 1(sequential) 2(parallel), use thanos:1/0")
        exit(0)
    mode = int(sys.argv[1])
    use_thanos = (sys.argv[2] == '1')
    beg = time.time()
    if use_thanos:
        RunTaskThanos(mode == 1)
    else:
        RunTask(mode == 1)
    print(f"runtime {(time.time() - beg) / max_usr_cnt}")