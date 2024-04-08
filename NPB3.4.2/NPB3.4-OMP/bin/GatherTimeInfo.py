from os import listdir
files = listdir()
targets = ["bt","cg","ep","ft","is","lu","mg","sp","ua"]
sample_sz = 3
classname = "C"
matchp = "Time in seconds"
for target in targets:
    for i in range(sample_sz):
        times = 0
        with open(f"{target}.{classname}.x.{i}_output","r") as f:
            for line in f.readlines():
                if line[:len(matchp)] == matchp:
                    time = line.split(" ")[-1]
                    times += float(time)
        print(f"{target} {classname} {times/sample_sz}")