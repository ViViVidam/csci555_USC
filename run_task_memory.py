import random
import os
import sys
import subprocess
import time
from tqdm import tqdm
max_usr_cnt = 20
# total_usr_cnt = 100
targets = ["bt", "cg", "ep", "ft", "is", "lu", "mg", "sp", "ua"]



def RunTask(run_para: bool,thread_num: int) -> None:
    processPool = []
    os.chdir("multithread/")
    files = os.listdir()
    for file in files:
        if file.endswith(".x"):
            for i in tqdm(range(max_usr_cnt)):
                fout = open(f"{file}.{i}_output", "w")
                ret = subprocess.Popen([f"./{file}",f"{thread_num}",f"{4}"], stdout=fout)
                if not run_para:
                    ret.wait()
                    print(ret.stderr)
                else:
                    processPool.append(ret)
                    time.sleep(random.random() * 5)
    if run_para:
        for process in processPool:
            process.wait()
        processPool.clear()
    os.chdir("../")


def RunTaskThanos(run_para:bool,thread_num:int) -> None:
    os.chdir("build")
    processPool = []
    files = os.listdir("../multithread/")
    for file in files:
        if file.endswith(".x"):
            for i in tqdm(range(max_usr_cnt)):
                fout = open(f"../multithread/{file}.{i}_output", "w")
                ret = subprocess.Popen(["./thanos","-v 0",f"../multithread/{file}",f"{thread_num}",f"{4}"], stdout=fout)
                if not run_para:
                    ret.wait()
                    print(ret.stderr)
                else:
                    processPool.append(ret)
                    time.sleep(random.random() * 5)
    if run_para:
        for process in processPool:
            process.wait()
        processPool.clear()
        print(f"round {i}")
    os.chdir("../")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("parallel:0/1,max thread num: int, use thanos:1/0")
        exit(0)
    mode = int(sys.argv[1])
    thread_num = int(sys.argv[2])
    use_thanos = (sys.argv[3] == '1')
    beg = time.time()
    if use_thanos:
        RunTaskThanos(mode==1,thread_num)
    else:
        RunTask(mode==1,thread_num)
    print(f"runtime {(time.time() - beg) / max_usr_cnt}")