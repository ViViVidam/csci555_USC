from multiprocessing import Process
import random
import os
import time
max_usr_cnt = 10
total_usr_cnt = 100
targets = ["bt","cg","ep","ft","is","lu","mg","sp","ua"]
processPool = []

def RunTask():
    idx = random.randint(0,len(targets)-1)
    os.system(f"./{targets[idx]}.C.x")

if __name__ == "__main__":
    for i in range(max_usr_cnt):
        t = Process(target=RunTask)
        processPool.append(t)
    for p in processPool:
        p.start()
        time.sleep(random.random()*5)
    for p in processPool:
        p.join()
