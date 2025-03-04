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

typedef unsigned int DEFAULT_VALUE_TYPE;
typedef unsigned long long key64;

typedef key64 IDType;

constexpr const int MAX_BIT_SIZE = 25;

// 基于CRTP的符号表基类
template<typename Derived, typename KeyType, typename ValueType, bool allow_id_conflict = true, bool no_conflict_in_key = false>
class base_symbol_table : public std::unordered_map<KeyType, ValueType> {
public:
    // 使用static_assert确保KeyType只能是string, string_view或char*
    static_assert(
        std::is_same<KeyType, std::string>::value || 
        std::is_same<KeyType, std::string_view>::value,
        "KeyType must be std::string or std::string_view"
    );
    base_symbol_table() {
        testEndian();
        keyValue.resize(1);
        keyValue[0] = std::pair<IDType, ValueType>{keyValue.size(), ValueType()};
    }

    void testEndian() {
        const char *p = "600000.SSE";
        key64 id = *(key64*)p;
        if((id & 0xff) != '6') {
            cout<<"big endian, unpexpected"<<endl;
            exit(1);
        }
    }

    bool sync_with_unordered_map() {
        if (this->size() == 0) {
            mask = 0;
            return true;
        }

        if (this->size() > 1 << MAX_BIT_SIZE) {
            cout<<"table size too large, try using template parameter SmallTbl"<<endl;
            return false;
        }
        vector<IDType> ids;
        for(auto &p : *this) {
            IDType id = static_cast<Derived*>(this)->get_id(p.first.data());
            if(find(ids.begin(), ids.end(), id) != ids.end()) {
                for(auto &q : *this) {
                    if(static_cast<Derived*>(this)->get_id(q.first.data()) == id) {
                        printf("error %s %s id: %llx\n", q.first.data(), p.first.data(), id);
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
        
        fill_table();
        return true;
    }

    //try to find a mask that minimizes the number of collisions
    IDType find_min_mask_by_cost(const vector<IDType> &ids, bool allow_conflict) {
        if(!allow_id_conflict) {
            if(allow_conflict) {
                cout<<"the mode should be allow_id_conflict = false"<<endl;
                exit(0);
            } 
        }
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
            } else if (allow_conflict && __builtin_popcountll(mask) >= MAX_BIT_SIZE) {
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
            if(p==0) {
                return false;
            }
            if(find(se.begin(), se.end(), p) != se.end()) {
                return false;
            }
            se.push_back(p);
        }
        return true;
    }

    inline ValueType get_no_key_conflict_value(const char *sym) const {
        //in some case, all symbols can be perfectly mapped to a single key
        // we can return the value directly
        IDType k = static_cast<const Derived *>(this)->get_key(sym);
        return keyValue[k].second;
    }

    
    inline ValueType get_value(const char *sym) const {
        if(!static_cast<const Derived*>(this)->should_process(sym)) {
            return ValueType{};
        }

        if(allow_id_conflict) {
            IDType id = static_cast<const Derived *>(this)->get_id(sym);
            IDType k = static_cast<const Derived *>(this)->get_key(sym);
            int size_mask = keyValue.size() - 1;

            for (int pos = k;; pos = (pos + 1) & size_mask) {
                if (keyValue[pos].first == id) {
                    return keyValue[pos].second;
                } else if (keyValue[pos].first == keyValue.size()) {
                    return ValueType{};
                }
            }
        } else {
            IDType id = static_cast<const Derived *>(this)->get_id(sym);
            IDType k = static_cast<const Derived *>(this)->get_key(sym);
            if (keyValue[k].first == id) {
                return keyValue[k].second;
            }
        }
        return ValueType{};
    }

    inline IDType get_key(const char *sym) const {
        IDType id = static_cast<const Derived *>(this)->get_id(sym);
        return _pext_u64(id, mask);
    }

    // 默认的calculate_mask实现，派生类可以重写
    IDType calculate_mask(const vector<IDType>& ids) {
        return find_min_mask_by_cost(ids, allow_id_conflict);
    }
    
    void fill_table() {
        for(auto &p : *this) {
            IDType id = static_cast<Derived*>(this)->get_id(p.first.data());
            IDType k = get_key(p.first.data());
            
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
    std::vector<std::pair<IDType, ValueType>> keyValue = {std::pair<IDType, ValueType>{1, ValueType{}}};
    IDType mask = 0;
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE>
class option_symbol_table : public base_symbol_table<option_symbol_table<KeyType, ValueType>, KeyType, ValueType> {
public:
    static constexpr const char* table_name = "option";
    using Base = base_symbol_table<option_symbol_table<KeyType, ValueType>, KeyType, ValueType>;
    using Base::get_value;
    using Base::keyValue;
    using Base::mask;

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

    inline IDType get_id(const char *sym, size_t len) const {
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
   
    inline ValueType get_value(const char *sym, size_t len) const {
         IDType id = get_id(sym, len);
         IDType k = _pext_u64(id, mask);  // 使用Base::_pext_u64而不是get_key
         int size_mask = keyValue.size() - 1;
         for (int pos = k;; pos = (pos + 1) & size_mask) {
            if (keyValue[pos].first == id) {
                return keyValue[pos].second;
            } else if (keyValue[pos].first == keyValue.size()) {
                return ValueType();
            }
         }
         return ValueType();
    }
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE, bool allow_id_conflict = true>
class stock_symbol_table : public base_symbol_table<stock_symbol_table<KeyType, ValueType, allow_id_conflict>, KeyType, ValueType> {
public:
    static constexpr const char* table_name = "stock";
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");
    
    inline IDType get_id(const char *sym) const {
        key64 id = *(key64*)sym;
        char *p = (char*)&id;
        p[7] = sym[8]; // 600000.SSE, 000001.SZE, exchange code is at 8th position
        return id;
    }

    inline IDType get_id(const char *sym, const char *exchange) const {
        key64 id = *(key64*)sym;
        char *p = (char*)&id;
        p[7] = exchange[1]; // exchange "SSE" or "SZE"
        return id;
    }
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE, bool allow_id_conflict = false>
class future_symbol_table : public base_symbol_table<future_symbol_table<KeyType, ValueType, allow_id_conflict>, KeyType, ValueType> {
public:
    using Base = base_symbol_table<future_symbol_table<KeyType, ValueType, allow_id_conflict>, KeyType, ValueType>;
    using Base::find_min_mask_by_cost;
    static constexpr const char* table_name = "future";
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");
    
    //do not allow id conflict
    IDType calculate_mask(const vector<IDType>& ids) {
        return find_min_mask_by_cost(ids, false);
    }

    inline IDType get_id(const char *sym) const {
        key64 id = *(uint64_t*)sym & 0xffffffffffff;
        return id & (-(sym[5] == 0 || sym[6] == 0));
    }
};
        

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE>
class future_no_conflict_symbol_table : public base_symbol_table<future_no_conflict_symbol_table<KeyType, ValueType>, KeyType, ValueType> {
public:
    using Base = base_symbol_table<future_no_conflict_symbol_table<KeyType, ValueType>, KeyType, ValueType>;
    using Base::mask;
    using Base::get_key;
    using Base::find_min_mask_by_cost;
    
    static constexpr const char* table_name = "future";

    void sync_with_unordered_map() {
       // This method should not be called for future_no_conflict_symbol_table, is not safe
       // Use modify_with_all_symbols instead, should confirm know all symbols will be used
       static_assert(false, "sync_with_unordered_map() should not be called for future_no_conflict_symbol_table, it's not safe. "
                            "Use sync_with_additional_symbols() instead.");
    }
    
    bool sync_with_additional_symbols(const vector<KeyType>& all_symbols) {
        vector<std::pair<IDType, KeyType>> allIDs;
        vector<IDType> ids;
        
        for(auto &p : *this) {
            IDType id = get_id(p.first.data());
            for(int i = 0; i < allIDs.size(); i++) {
                if(allIDs[i].first == id) {
                    printf("error %s %s id: %llx\n", p.first.data(), allIDs[i].second.data(), id);
                    exit(0);
                }
            }
            ids.push_back(id);
            allIDs.push_back(std::pair<IDType, KeyType>{id, p.first});
        }
        
        for(auto &p : all_symbols) {
            if(p.size() > 6) {
                continue;
            }
            IDType id = get_id(p.data());
            bool found = false;
            for(int i = 0; i < allIDs.size(); i++) {
                if(allIDs[i].first == id && allIDs[i].second != p) {
                    printf("error id conflict %s %s id: %llx\n", p.data(), allIDs[i].second.data(), id);
                    exit(0);
                }
                if(allIDs[i].first == id && allIDs[i].second == p) {
                    found = true;
                }
            }
            if(!found) {
                allIDs.push_back(std::pair<IDType, KeyType>{id, p});
            }
        }
        
        vector<IDType> all_ids;
        for(auto &p : allIDs) {
            all_ids.push_back(p.first);
        }

        if(all_ids.size()>=1<<MAX_BIT_SIZE) {
            cout<<"future_no_conflict_symbol_table size too large, max size is"<<(1<<MAX_BIT_SIZE)<<endl;
            return false;
        }
        
        // 使用所有ID计算掩码，不允许冲突
        mask = find_min_mask_by_cost(all_ids, false);
        
        future.resize(1 << __builtin_popcountll(mask));
        future_size = 1 << __builtin_popcountll(mask);
        
        for(int i = 0; i < future_size; i++) {
            future[i] = ValueType{};
        }
        
        //cout << "future ids size: " << ids.size() << " mask: " << mask << " " 
             //<< __builtin_popcountll(mask) << " future size: " << future_size << endl;
             
        for(auto &p : *this) {
            IDType id = get_id(p.first.data());
            IDType k = get_key(p.first.data());
            future[k] = p.second;
        }
        return true;
    }
    
    inline IDType get_id(const char *sym) const {
        // 直接读取前6个字节作为ID，避免掩码操作
        // 使用位移操作代替乘法，编译器会优化成条件移动指令
        IDType id = *(uint64_t*)sym & 0xffffffffffff;
        return id & (-(sym[5] == 0 || sym[6] == 0));
        // 使用位运算 id & (-(condition)) 代替 id *= condition
        // 当条件为真时，-1 & id = id；当条件为假时，0 & id = 0
    }
    
    inline ValueType get_value(const char *sym) const {
        IDType k = get_key(sym);
        return future[k];
    }
    
private:
    std::vector<ValueType> future;
    int future_size = 0;
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE>
class combine_symbol_table : public std::unordered_map<KeyType, ValueType> {
  public:
    static constexpr const char* table_name = "combine";
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");

    combine_symbol_table() {
    }

    bool sync_with_unordered_map() {
         stock.clear();
         future.clear();
         option.clear();
         for(auto &p : *this) {
            KeyType sym = p.first;
            if(sym.size() == 5 || sym.size() == 6) {
                future[sym.data()] = p.second;
            } else if(sym[6]=='.') { //600000.SSE, 000001.SZE
                stock[sym.data()] = p.second;
            } else {
                option[sym.data()] = p.second;
            }
         }
         return stock.sync_with_unordered_map() && future.sync_with_unordered_map() && option.sync_with_unordered_map();
    }

    ValueType get_value(const char *sym) const {
        if(sym[5]==0||sym[6]==0) {
          return future.get_value(sym);
        } else if(sym[6]=='.') {
            return stock.get_value(sym);
        } else {
            return option.get_value(sym);
        }
    }

    ValueType get_value(const char *sym, size_t len) const {
        if(sym[5]==0||sym[6]==0) {
          return future.get_value(sym);
        } else if(sym[6]=='.') { //600000.SSE, 000001.SZE
            return stock.get_value(sym);
        } else {
            return option.get_value(sym, len);
        }
    }

private:
     stock_symbol_table<KeyType, ValueType> stock;
     future_symbol_table<KeyType, ValueType> future;
     //future_no_conflict_symbol_table<KeyType, ValueType> future_no_conflict;
     option_symbol_table<KeyType, ValueType> option;
};

#endif // CYB_HASH_H
