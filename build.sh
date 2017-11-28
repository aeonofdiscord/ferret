#!/bin/sh
lsb_release -ir
mkdir -p build
cd build
cmake ..
make
cp ferret ..
cd ..

