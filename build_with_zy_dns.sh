#!/bin/bash

mkdir build
cd build
cmake .. -DWITH_ZY_DNS=ON
make -j