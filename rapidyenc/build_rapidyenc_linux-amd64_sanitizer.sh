#!/usr/bin/env bash
cd ../ || exit 2
rm -rf build
mkdir -p build
cd build || exit 3
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address -g" . || exit 4
cmake --build . --config Release || exit 5
ls . rapidyenc_static/
