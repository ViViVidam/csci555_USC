import random
import os
import sys
import subprocess
import time

max_usr_cnt = 2
# total_usr_cnt = 100
targets = ["bt", "cg", "dc", "ep", "ft", "is", "lu", "mg", "sp", "ua"]
t_counts = [0.] * len(targets)
v_counts = [0.] * len(targets)
def RunTask(run_seq: bool) -> None:
    processPool = []
    perf_outs = []
    os.chdir("NPB3.4.2/NPB3.4-OMP/bin/")
    files = os.listdir()
    for i in range(max_usr_cnt):
        for file in files:
            if file.endswith(".x"):
                fout = open(f"{file}.{i}_output", "w")
                perf_out = f"{file}.{i}_output_perf"
                ret = subprocess.Popen(
                    ["perf", "stat","-o",f"{perf_out}","--per-node","-a","-e", "cache-references,cache-misses",
                     f"./{file}"], stdout=fout)
                if run_seq:
                    ret.wait()
                    #print(ret.stderr)
                    with open(perf_out, "r") as f:
                        for line in f.readlines()[5:]:
                            splits = line.split()
                            if len(splits) < 4:
                                continue
                            #print(line,splits)
                            core = splits[0]
                            n = splits[3]
                            count = splits[2].replace(',','')
                            if n == "cache-misses":
                                v_counts[targets.index(perf_out[:2])] += float(count)
                            elif n == "cache-references":
                                t_counts[targets.index(perf_out[:2])] += float(count)
                else:
                    time.sleep(10)
                processPool.append(ret)
        if not run_seq:
            for perf_out, process in zip(perf_outs, processPool):
                process.wait()
                with open(perf_out, "r") as f:
                    for line in f.readlines()[5:]:
                        splits = line.split()
                        if len(splits) < 4:
                            continue
                        n = splits[3]
                        count = splits[2].replace(',','')
                        if n == "cache-misses":
                            v_counts[targets.index(perf_out[:2])] += float(count)
                        elif n == "cache-references":
                            t_counts[targets.index(perf_out[:2])] += float(count)
            processPool.clear()
            perf_outs.clear()
        print(f"round {i}")
    os.chdir("../../../")


def RunTaskThanos(run_seq: bool) -> None:
    processPool = []
    perf_outs = []
    os.chdir("build")
    files = os.listdir("../NPB3.4.2/NPB3.4-OMP/bin/")
    for i in range(max_usr_cnt):
        for file in files:
            if file.endswith(".x"):
                fout = open(f"../NPB3.4.2/NPB3.4-OMP/bin/{file}.{i}_output", "w")
                perf_out = f"{file}.{i}_output_perf"
                ret = subprocess.Popen(
                    ["perf", "stat", "--per-node", "-a", "-o", f"{perf_out}", "-e", "cache-references,cache-misses",
                     "./thanos", "-v 1", f"../NPB3.4.2/NPB3.4-OMP/bin/{file}"], stdout=fout)
                if run_seq:
                    ret.wait()
                    with open(perf_out, "r") as f:
                        for line in f.readlines()[5:]:
                            splits = line.split()
                            if len(splits) < 4:
                                continue
                            n = splits[3]
                            count = splits[2].replace(',','')
                            if n == "cache-misses":
                                v_counts[targets.index(perf_out[:2])] += float(count)
                            elif n == "cache-references":
                                t_counts[targets.index(perf_out[:2])] += float(count)
                else:
                    processPool.append(ret)
                    perf_outs.append(perf_out)
                    time.sleep(10)
        if not run_seq:
            for perf_out, process in zip(perf_outs,processPool):
                process.wait()
                with open(perf_out, "r") as f:
                    for line in f.readlines()[5:]:
                        splits = line.split()
                        if len(splits) < 4:
                            continue
                        n = splits[3]
                        count = splits[2].replace(',','')
                        if n == "cache-misses":
                            v_counts[targets.index(perf_out[:2])] += float(count)
                        elif n == "cache-references":
                            t_counts[targets.index(perf_out[:2])] += float(count)
            processPool.clear()
            perf_outs.clear()
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
    for name,v,t in zip(targets,v_counts,t_counts):
        v = v / max_usr_cnt
        t = t / max_usr_cnt
        print(f"{name} {v/t}: {int(v)}")