#!/bin/bash
export PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig:$PKG_CONFIG_PATH
cd build
rm -r *
cmake ..
make
./Emile -l 0-1 -a 01:00.1
