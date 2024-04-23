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
    for target in targets:
        if target == "dc":
            file = f"{target}.B.x"
        else:
            file = f"{target}.C.x"
        for i in range(max_usr_cnt):
            fout = open(f"{file}.{i}_output", "w")
            ret = subprocess.Popen(["perf",f"./{file}"], stdout=fout)
            if run_seq:
                ret.wait()
                print(ret.stderr)
            else:
                time.sleep(random.random() * 20 + 10)
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
    for target in targets:
        if target == "dc":
            file = f"{target}.B.x"
        else:
            file = f"{target}.C.x"
        for i in range(max_usr_cnt):
            fout = open(f"../NPB3.4.2/NPB3.4-OMP/bin/{file}.{i}_output", "w")
            ret = subprocess.Popen(["perf ./thanos","-v 1",f"../NPB3.4.2/NPB3.4-OMP/bin/{file}"], stdout=fout)
            if run_seq:
                ret.wait()
                print(ret.stderr)
            else:
                processPool.append(ret)
                time.sleep(random.random() * 20 + 10)
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