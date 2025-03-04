#!/bin/bash

g++ -std=c++17 -mbmi2 -O3 -DNDEBUG testCYBHash.cc -o cyb_hash

if [ $? -eq 0 ]; then
  echo "Compilation successful, running test..."
  ./cyb_hash
  echo "\n running stock test..."
  ./cyb_hash stock.txt stock.txt stock
  echo "\n running combine test..."
  ./cyb_hash combine.txt combine.txt combine
  rm cyb_hash
else
  echo "Compilation failed."
fi

