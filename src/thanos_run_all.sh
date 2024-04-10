#!/bin/bash
for f in ../NPB3.4.2/NPB3.4-OMP/bin/*.x; do
  ./thanos -S 2 $f > "$f.1_output"
  ./thanos -S 2 $f > "$f.2_output"
  ./thanos -S 2 $f > "$f.3_output"
done