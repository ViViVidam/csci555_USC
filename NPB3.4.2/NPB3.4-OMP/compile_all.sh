#!/bin/bash
class="C"
targets=(bt cg dt ep ft is lu mg sp ua)
for i in "${targets[@]}"
do
   make "$i" CLASS="$class"
   # or do whatever with individual element of the array
done

make dc CLASS=B