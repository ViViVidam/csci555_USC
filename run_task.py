import random
import os
import sys
import subprocess

max_usr_cnt = 5
# total_usr_cnt = 100
targets = ["bt", "cg", "ep", "ft", "is", "lu", "mg", "sp", "ua"]
processPool = []


def RunTask(run_seq: bool) -> None:
    os.chdir("NPB3.4.2/NPB3.4-OMP/bin/")
    files = os.listdir()
    for file in files:
        if file.endswith(".x"):
            for i in range(max_usr_cnt):
                fout = open("{file}.{i}_output", "w")
                ret = subprocess.Popen(["./thanos", f"./NPB3.4.2/NPB3.4-OMP/bin/{file}"], stdout=fout)
                if run_seq:
                    ret.wait()
                    print(ret.stderr)
                else:
                    ret.wait(random.random() * 3)
                fout.close()
    os.chdir("../../../")


def RunTaskThanos(run_seq: bool) -> None:
    os.chdir("bin")
    files = os.listdir("./NPB3.4.2/NPB3.4-OMP/bin/")
    for file in files:
        if file.endswith(".x"):
            for i in range(max_usr_cnt):
                fout = open("./NPB3.4.2/NPB3.4-OMP/bin/{file}.{i}_output", "w")
                ret = subprocess.Popen(["./thanos", f"./NPB3.4.2/NPB3.4-OMP/bin/{file}"], stdout=fout)
                if run_seq:
                    ret.wait()
                else:
                    ret.wait(random.random() * 3)
                fout.close()
    os.chdir("../../../")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("mode: 1(sequential) 2(parallel), use thanos:1/0")
        exit(0)
    mode = int(sys.argv[1])
    use_thanos = bool(sys.argv[2])
    if use_thanos:
        RunTaskThanos(mode == 1)
    else:
        RunTask(mode == 1)