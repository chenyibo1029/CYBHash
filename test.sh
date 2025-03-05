#!/bin/bash

g++ -std=c++17 -mbmi2 -O3 -DNDEBUG testCYBHash.cc -o cyb_hash

if [ $? -eq 0 ]; then
  echo ''
  echo " running stock test..."
  ./cyb_hash stock.txt stock.txt stock
  echo ''
  echo " running combine test..."
  ./cyb_hash combine.txt combine.txt combine
  echo ''
  echo " running future option test..."
  ./cyb_hash dce.txt combine.txt
  echo ''
  echo " running only future test..."
  ./cyb_hash dce.txt dce.txt 
  #rm cyb_hash
else
  echo "Compilation failed."
fi

