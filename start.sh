#!/bin/bash

cd build
rm -r *
cmake ..
make
./Emile  -l 0-3 -n 4 -- -q 8 -p 0x2
