#!/bin/bash

g++ -std=c++17 -mbmi2 -O3 -DNDEBUG testCYBHash.cc -o cyb_hash

if [ $? -eq 0 ]; then
  echo ''
  echo " running combine future and option test..."
  ./cyb_hash combine.txt combine.txt combine
  echo ''
  echo " running combine  future test..."
  ./cyb_hash shfe.txt shfe.txt combine
  echo ''
  echo " running combine  stock test..."
  ./cyb_hash stock.txt stock.txt combine
  echo ''
  echo " running stock test..."
  ./cyb_hash stock.txt stock.txt stock
  echo ''
  echo " running future_table with future and option md test..."
  ./cyb_hash dce.txt combine.txt
  echo ''
  echo " running only future test..."
  ./cyb_hash dce.txt dce.txt 
  #rm cyb_hash
else
  echo "Compilation failed."
fi

