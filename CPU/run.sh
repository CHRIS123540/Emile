#!/bin/bash

cd build
rm *
cmake ..
make
./basicfwd -a c1:00.0 -l 0-10