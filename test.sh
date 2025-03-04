#!/bin/bash

g++ -std=c++17 -mbmi2 -O3 -DNDEBUG testCYBHash.cc -o cyb_hash

if [ $? -eq 0 ]; then
  echo "Compilation successful, running test..."
  ./cyb_hash
else
  echo "Compilation failed."
fi
