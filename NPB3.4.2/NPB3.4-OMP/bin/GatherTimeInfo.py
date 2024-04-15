from os import listdir
files = listdir()
targets = ["bt","cg","ep","ft","is","lu","mg","sp","ua","dc"]
sample_sz = 5
classname = "C"
matchp = " Time in seconds"
for target in targets:
    times = 0
    if target == "dc":
        for i in range(0,sample_sz):
            with open(f"{target}.B.x.{i}_output","r") as f:
                for line in f.readlines():
                    if line[:len(matchp)] == matchp:
                        time = line.split(" ")[-1]
                        times += float(time)
    else:
        for i in range(0,sample_sz):
            with open(f"{target}.{classname}.x.{i}_output","r") as f:
                for line in f.readlines():
                    if line[:len(matchp)] == matchp:
                        time = line.split(" ")[-1]
                        times += float(time)
    print(f"{target} {classname} {times/sample_sz}")