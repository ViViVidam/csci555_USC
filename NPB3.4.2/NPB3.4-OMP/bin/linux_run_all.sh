#!/bin/bash
for f in *.x; do
  ./$f > "$f.output"
done