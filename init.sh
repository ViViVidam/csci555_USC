#!/bin/bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
cd ..
cd NPB3.4.2/NPB3.4-MPI/
make BT S
