from os import listdir
files = listdir()
targets = ["bt","cg","ep","ft","is","lu","mg","sp","ua","dc"]
sample_sz = 5
classname = "C"
matchp = " Time in seconds"
for file in files:
    times = 0
    if file.endswith("perf"):
        with open(file, "r") as f:
            for line in f.readlines():
                core,_,count,name,_ = line.split("\t")
                tcount = 0.
                if name == "cache-misses":
                    print(f"{file} {core}: {float(count)/tcount}, total of {count}")
                elif name =="cache-references":
                    tcount = float(count)