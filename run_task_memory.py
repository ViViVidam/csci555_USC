import random
import os
import sys
import subprocess
import time
max_usr_cnt = 5
# total_usr_cnt = 100
targets = ["bt", "cg", "ep", "ft", "is", "lu", "mg", "sp", "ua"]



def RunTask(thread_num: int) -> None:
    os.chdir("multithread/")
    files = os.listdir()
    for i in range(max_usr_cnt):
        for file in files:
            if file.endswith(".x"):
                fout = open(f"{file}.{i}_output", "w")
                ret = subprocess.Popen([f"./{file}",f"{thread_num}",f"{4}"], stdout=fout)
                ret.wait()
                '''
                time.sleep(random.random() * 15 + 5)
                processPool.append(ret)
        if not run_seq:
            for process in processPool:
                process.wait()
            processPool.clear()
            '''
        print(f"round {i}")
    os.chdir("../")


def RunTaskThanos(thread_num:int) -> None:
    os.chdir("build")
    files = os.listdir("../multithread/")
    for i in range(max_usr_cnt):
        for file in files:
            if file.endswith(".x"):
                fout = open(f"../multithread/{file}.{i}_output", "w")
                ret = subprocess.Popen(["./thanos","-v 0",f"../multithread/{file}",f"{thread_num}",f"{4}"], stdout=fout)
                ret.wait()
                '''
                if run_seq:
                    ret.wait()
                    print(ret.stderr)
                else:
                    processPool.append(ret)
                    time.sleep(random.random() * 15 + 5)
        if not run_seq:
            for process in processPool:
                process.wait()
            processPool.clear()
            '''
        print(f"round {i}")
    os.chdir("../")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("thread num: int, use thanos:1/0")
        exit(0)
    thread_num = int(sys.argv[1])
    use_thanos = (sys.argv[2] == '1')
    beg = time.time()
    if use_thanos:
        RunTaskThanos(thread_num)
    else:
        RunTask(thread_num)
    print(f"runtime {(time.time() - beg) / max_usr_cnt}")