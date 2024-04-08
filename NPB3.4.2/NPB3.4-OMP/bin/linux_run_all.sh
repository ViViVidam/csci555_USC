#!/bin/bash
for f in *.x; do
  ./$f > "$f.1_output"
  ./$f > "$f.2_output"
  ./$f > "$f.3_output"
done