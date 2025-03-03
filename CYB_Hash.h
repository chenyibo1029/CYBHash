/*
 * MIT License
 *
 * Copyright (c) 2023 Chen Yibo <112997821@qq.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef CYB_HASH_H
#define CYB_HASH_H

//Choose Yummy Bits Hash
//https://github.com/chenyibo1029/CYBHash
#include <bits/stdc++.h>
using namespace std;
#include <immintrin.h>
#include <unordered_set>
#include <iostream>
#include <x86intrin.h>
#include <bitset>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <string>

typedef unsigned int key32;
typedef unsigned short key16;
typedef unsigned char key8;
typedef unsigned long long key64;
typedef unsigned __int128 key128;
typedef int value32;

typedef key32 VALUE_TYPE;
typedef key64 IDType;

// 基于CRTP的符号表基类
template<typename Derived, typename KeyType = std::string, typename ValueType = VALUE_TYPE>
class base_symbol_table : public std::unordered_map<KeyType, ValueType> {
public:
    // 使用static_assert确保KeyType只能是string, string_view或char*
    static_assert(
        std::is_same<KeyType, std::string>::value || 
        std::is_same<KeyType, std::string_view>::value,
        "KeyType must be std::string or std::string_view"
    );

    base_symbol_table() {}

    void intFromSelf() {
        init(*this);
    }

    void init(const std::unordered_map<KeyType, ValueType>& symbols) {
        vector<IDType> ids;
        for(auto &p : symbols) {
            IDType id = static_cast<Derived*>(this)->get_id(p.first.data());
            if(find(ids.begin(), ids.end(), id) != ids.end()) {
                for(auto &q : symbols) {
                    if(static_cast<Derived*>(this)->get_id(q.first.data()) == id) {
                        printf("error %s %s id: %lx\n", q.first.data(), p.first.data(), id);
                        exit(0);
                    }
                }
            }
            ids.push_back(id);
        }
        
        // 使用派生类可能重写的方法计算掩码
        mask = static_cast<Derived*>(this)->calculate_mask(ids);
        
        // 初始化查找表
        keyValue.resize(1 << __builtin_popcountll(mask));
        for(int i = 0; i < keyValue.size(); i++) {
            keyValue[i] = std::pair<IDType, ValueType>{keyValue.size(), ValueType()};
        }
        
        // 填充查找表
        static_cast<Derived*>(this)->fill_table(symbols);
    }

    IDType find_min_mask_by_cost(const vector<IDType> &ids, bool allow_conflict) {
        IDType mask = 0; // 初始掩码为0（不选择任何位）
        std::map<IDType, std::pair<IDType, int>> hash_values;
        //cout << "ids.size() " << ids.size() << endl;
        long long min_cost = ids.size() * ids.size();
        for (int bits_added = 0; bits_added < sizeof(IDType) * 8; bits_added++) {
            int best_bit = -1;
            // for (int bit = sizeof(IDType)*8-1; bit >=0; bit--) {
            for (int bit = 0; bit < sizeof(IDType) * 8; bit++) {
                IDType bit_mask = ((IDType)1) << bit;
                if (mask & bit_mask)
                    continue; 

                IDType test_mask = mask | bit_mask;
                // cout<<bit<<" new mask: "<<test_mask<<endl;
                hash_values.clear();
                size_t unique_values = 0;

                for (auto id : ids) {
                    IDType hash = _pext_u64(id, test_mask);
                    if (hash_values.find(hash) == hash_values.end()) {
                        hash_values[hash] = pair{0, 0};
                    }
                    hash_values[hash].first++;
                    // cout<<id<<" "<<test_mask<<" hash: "<<hash<<endl;
                }
                long long collisions = 0;
                for (auto &p : hash_values) {
                    collisions += p.second.first * (p.second.first - 1) / 2;
                }
                if (collisions < min_cost) {
                    min_cost = collisions;
                    best_bit = bit;
                }

                if (min_cost == 0) {
                    break;
                }
            }
            mask |= ((IDType)1) << best_bit;
            if (min_cost == 0) {
                //cout << "find min cost, exit global search" << endl;
                break;
            }

            if (allow_conflict && min_cost <= ids.size() * 0.1 &&
                ((key64)1 << __builtin_popcountll(mask)) >= ids.size() * 1.5) {
                //printf("find mask by cost %lx %d, min_cost %lld ids.size() %d\n", mask, __builtin_popcountll(mask), min_cost, ids.size());
                return mask;
            } else if (allow_conflict && __builtin_popcountll(mask) >= 25) {
                //  printf("mask too large %lx %d, min_cost %lld ids.size() %d\n", mask, __builtin_popcountll(mask), min_cost, ids.size());
                return mask;
            }
        }
        //printf("find mask by cost %lx %d\n", mask, __builtin_popcountll(mask));
        return mask;
    }
    
    bool check_mask(IDType new_mask, const vector<IDType>& ids) {
        vector<IDType> se;
        for(auto id : ids) {
            IDType p = _pext_u64(id, new_mask);
            if(find(se.begin(), se.end(), p) != se.end()) {
                return false;
            }
            se.push_back(p);
        }
        return true;
    }
    
    IDType find_min_mask_high_to_low(const vector<IDType>& ids) {
        IDType mask = ~static_cast<IDType>(0);
        for(int i = sizeof(IDType)*8-1; i >= 0; i--) {
            IDType new_mask = mask & (~((((IDType)1) << i)));
            if(check_mask(new_mask, ids)) {
                mask = new_mask;
            }
        }
        return mask;
    }
    
    inline ValueType get_value(const char *sym) const {
        if(!static_cast<const Derived*>(this)->should_process(sym)) {
            return 0;
        }
        
        IDType id = static_cast<const Derived*>(this)->get_id(sym);
        IDType k = static_cast<const Derived*>(this)->get_key(sym);
        int size_mask = keyValue.size() - 1;
        
        for(int pos = k;; pos = (pos+1) & size_mask) {
            if(keyValue[pos].first == id) { 
                return keyValue[pos].second;
            } else if(keyValue[pos].first == keyValue.size()) {
                return ValueType();
            }
        }
        return ValueType();
    }
    
    inline IDType get_key(const char *sym) const {
        IDType id = static_cast<const Derived*>(this)->get_id(sym);
        return _pext_u64(id, mask);
    }
    
    // 默认的calculate_mask实现，派生类可以重写
    IDType calculate_mask(const vector<IDType>& ids) {
        return find_min_mask_by_cost(ids, true);
    }
    
    // 默认的fill_table实现，派生类可以重写
    void fill_table(const std::unordered_map<std::string, ValueType>& symbols) {
        for(auto &p : symbols) {
            IDType id = static_cast<Derived*>(this)->get_id(p.first.data());
            IDType k = get_key(p.first.data());
            
            if(k >= keyValue.size()) {
                //cout << p.first << " id: " << id << " error k: " << k << " size: " << keyValue.size() << endl;
            }
            
            int size_mask = keyValue.size() - 1;  
            for(int pos = k;; pos = (pos+1) & size_mask) {
                if(keyValue[pos].first == keyValue.size()) { 
                    keyValue[pos] = std::pair<IDType, ValueType>{id, p.second}; 
                    break;
                }
            }
        }
    }
    
    // 默认的should_process实现，派生类可以重写
    bool should_process(const char *sym) const {
        return true;
    }
    
protected:
    std::vector<std::pair<IDType, ValueType>> keyValue;
    IDType mask;
};

template<typename KeyType = std::string, typename ValueType = VALUE_TYPE>
class option_symbol_table : public base_symbol_table<option_symbol_table<KeyType, ValueType>, KeyType, ValueType> {
public:
    static constexpr const char* table_name = "option";
    
    // The requires clause should be in a concept or function template, not as a standalone statement
    // Moving the constraint logic to a static_assert
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");
                 
    inline IDType get_id(const char *sym) const {
        size_t len = 8;
        for (const char* p = sym+8; *p; ++p) len++;
        key64 t64 = *(key64*)(sym+8);
        static const uint64_t masks[] = {
            0xff,               // 9: 1 byte
            0xffff,             // 10: 2 bytes
            0xffffff,           // 11: 3 bytes
            0xffffffff,         // 12: 4 bytes
            0xffffffffff,       // 13: 5 bytes
            0xffffffffffff,     // 14: 6 bytes
            0xffffffffffffff,   // 15: 7 bytes
            0xffffffffffffffff  // 16: 8 bytes
        };
        t64 &= masks[len - 9];
        key64 a = (*(key64*)sym);
        a += t64<<20;
        return a;
    }
};

template<typename KeyType = std::string, typename ValueType = VALUE_TYPE>
class stock_symbol_table : public base_symbol_table<stock_symbol_table<KeyType, ValueType>> {
public:
    static constexpr const char* table_name = "stock";
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");
    inline bool should_process(const char *sym) const {
        return sym[6] == '.';
    }
    
    inline IDType get_id(const char *sym) const {
        key64 id = *(key64*)sym;
        id &= 0xffffffffffff;
        id += sym[8]<<4;
        return id;
    }
};

template<typename KeyType = std::string, typename ValueType = VALUE_TYPE>
class future_symbol_table : public base_symbol_table<future_symbol_table<KeyType, ValueType>> {
public:
    static constexpr const char* table_name = "future";
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");
    inline IDType get_id(const char *sym) const {
        key64 id = *(uint64_t*)sym & 0xffffffffffff;
        return id & (-(sym[5] == 0 || sym[6] == 0));
    }
};
        

template<typename KeyType = std::string, typename ValueType = VALUE_TYPE>
class future_no_conflict_symbol_table : public base_symbol_table<future_no_conflict_symbol_table<KeyType, ValueType>> {
public:
    using Base = base_symbol_table<future_no_conflict_symbol_table<KeyType, ValueType>>;
    using Base::mask;
    using Base::get_key;
    using Base::find_min_mask_by_cost;
    
    static constexpr const char* table_name = "future";
    
    void init_with_all_symbols(const std::unordered_map<KeyType, ValueType>& symbols, const vector<std::string>& all_symbols) {
        vector<std::pair<IDType, string>> allIDs;
        vector<IDType> ids;
        
        for(auto &p : symbols) {
            IDType id = get_id(p.first.data());
            for(int i = 0; i < allIDs.size(); i++) {
                if(allIDs[i].first == id) {
                    printf("error %s %s id: %lx\n", p.first.data(), allIDs[i].second.data(), id);
                    exit(0);
                }
            }
            ids.push_back(id);
            allIDs.push_back(std::pair<IDType, string>{id, p.first});
        }
        
        for(auto &p : all_symbols) {
            if(p.size() > 6) {
                continue;
            }
            IDType id = get_id(p.data());
            bool found = false;
            for(int i = 0; i < allIDs.size(); i++) {
                if(allIDs[i].first == id && allIDs[i].second != p) {
                    printf("error %s %s id: %lx\n", p.data(), allIDs[i].second.data(), id);
                    exit(0);
                }
                if(allIDs[i].first == id && allIDs[i].second == p) {
                    found = true;
                }
            }
            if(!found) {
                allIDs.push_back(std::pair<IDType, string>{id, p});
            }
        }
        
        vector<IDType> all_ids;
        for(auto &p : allIDs) {
            all_ids.push_back(p.first);
        }
        
        // 使用所有ID计算掩码，不允许冲突
        mask = find_min_mask_by_cost(all_ids, false);
        
        future.resize(1 << __builtin_popcountll(mask));
        future_size = 1 << __builtin_popcountll(mask);
        
        for(int i = 0; i < future_size; i++) {
            future[i] = {};
        }
        
        //cout << "future ids size: " << ids.size() << " mask: " << mask << " " 
             //<< __builtin_popcountll(mask) << " future size: " << future_size << endl;
             
        for(auto &p : symbols) {
            IDType id = get_id(p.first.data());
            IDType k = get_key(p.first.data());
            future[k] = p.second;
        }
    }
    
    bool should_process(const char *sym) const {
        return sym[5] == 0 || sym[6] == 0;
    }
    
    inline IDType get_id(const char *sym) const {
        // 直接读取前6个字节作为ID，避免掩码操作
        // 使用位移操作代替乘法，编译器会优化成条件移动指令
        IDType id = *(uint64_t*)sym & 0xffffffffffff;
        return id & (-(sym[5] == 0 || sym[6] == 0));
        // 使用位运算 id & (-(condition)) 代替 id *= condition
        // 当条件为真时，-1 & id = id；当条件为假时，0 & id = 0
    }
    
    inline IDType get_value(const char *sym) const {
        IDType k = get_key(sym);
        return future[k];
    }
    
private:
    std::vector<ValueType> future;
    int future_size = 0;
};

class china_symbol_table {
  public:
    
    china_symbol_table() {
    }
    china_symbol_table(const std::unordered_map<std::string, VALUE_TYPE>& symbols, const vector<std::string>& find_data) {
         std::unordered_map<std::string, VALUE_TYPE> future5_map;
         std::unordered_map<std::string, VALUE_TYPE> future6_map;
         std::unordered_map<std::string, VALUE_TYPE> future_map;
         std::unordered_map<std::string, VALUE_TYPE> stock_map;
         std::unordered_map<std::string, VALUE_TYPE> option_map;
         for(auto &p : symbols) {
            std::string sym = p.first;
            if(sym.size() == 5) {
                future5_map[sym] = p.second;
                future_map[sym] = p.second;
            } else if(sym.size() == 6) {
                future6_map[sym] = p.second;
                future_map[sym] = p.second;
            } else if(sym[6]=='.') { //600000.SSE, 000001.SZE
                stock_map[sym] = p.second;
            } else {
                option_map[sym] = p.second;
            }
         }
         stock.init(stock_map);
         //future.init(future_map);
         future.init_with_all_symbols(future_map, find_data);
         option.init(option_map);
    }

    value32 get_value(const char *sym) const {
        if(sym[5]==0||sym[6]==0) {
          return future.get_value(sym);
        } else if(sym[6]=='.') {
            return stock.get_value(sym);
        } else {
            return option.get_value(sym);
        }
    }

private:
     stock_symbol_table<> stock;
     //future__symbol_table<> future;
     future_no_conflict_symbol_table<> future;
     option_symbol_table<> option;
};

#endif // CYB_HASH_H