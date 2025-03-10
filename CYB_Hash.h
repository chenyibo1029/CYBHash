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
template<typename Derived, typename KeyType, typename ValueType, bool allow_id_conflict = true>
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
            keyValue.resize(1);
            keyValue[0] = std::pair<IDType, ValueType>{keyValue.size(), ValueType()};
            return true;
        }

        if (this->size() > 1 << MAX_BIT_SIZE) {
            cout<<"table size too large"<<endl;
            return false;
        }
        vector<IDType> ids;
        for(auto &p : *this) {
            if(p.first.size()<5 || p.first.size()>16 || p.first.size()==16 && p.first[15]!='0') {
                std::cout << p.first << " min length is expected to be 5, symbol too long max length supported is 16, and if the length is 16, the last character must be '0'" << std::endl;
                std::exit(0);
            }
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
        if(mask == 0) {
            return false;
        }
        
        // 初始化查找表
        keyValue.resize(1 << __builtin_popcountll(mask));
        for(int i = 0; i < keyValue.size(); i++) {
            keyValue[i] = std::pair<IDType, ValueType>{keyValue.size(), ValueType()};
        }
        
        fill_table();
        return true;
    }

    //try to find a mask that minimizes the number of collisions
    IDType find_min_mask_by_cost(const vector<IDType> &ids, bool allow_conflict, int highest_bit) {
        if(!allow_id_conflict) {
            if(allow_conflict) {
                cout<<"the mode should be allow_id_conflict = false"<<endl;
                exit(0);
            } 
        }
        IDType mask = 0; // 初始掩码为0（不选择任何位）
        std::map<IDType, int> hash_values;
        //cout << "ids.size() " << ids.size() << endl;
        long long min_cost = ids.size() * ids.size();
        for (int bits_added = 0; bits_added < highest_bit; bits_added++) {
            int best_bit = -1;
            // for (int bit = sizeof(IDType)*8-1; bit >=0; bit--) {
            for (int bit = 0; bit < highest_bit; bit++) {
                IDType bit_mask = ((IDType)1) << bit;
                if (mask & bit_mask)
                    continue; 

                IDType test_mask = mask | bit_mask;
                // cout<<bit<<" new mask: "<<test_mask<<endl;
                hash_values.clear();
                size_t unique_values = 0;

                for (auto id : ids) {
                    IDType hash = _pext_u64(id, test_mask);
                    hash_values[hash]++;
                    // cout<<id<<" "<<test_mask<<" hash: "<<hash<<endl;
                }
                long long collisions = 0;
                for (auto &p : hash_values) {
                    if(p.first == 0) { //0 may be conflict with option code
                        collisions += 1; //so if final hash is 0, it means this mask is not safe for future option combine case
                    }
                    collisions += p.second * (p.second - 1) / 2;
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
        if(allow_conflict) {
            return mask;
        } else if(min_cost>0) {
            printf("find mask failed, min_cost %lld ids.size() %ld\n", min_cost, ids.size());
            return 0;
        }
        //printf("find mask by cost %lx %d\n", mask, __builtin_popcountll(mask));
        return mask;
    }
    
    inline ValueType get_no_key_conflict_value(const char *sym) const {
        //in some case, all symbols can be perfectly mapped to a single key
        // we can return the value directly
        IDType k = static_cast<const Derived *>(this)->get_key(sym);
        return keyValue[k].second;
    }

    
    inline ValueType get_value(const char *sym) const {
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
        return ValueType{};
    }

    inline IDType get_key(const char *sym) const {
        IDType id = static_cast<const Derived *>(this)->get_id(sym);
        return _pext_u64(id, mask);
    }

    // 默认的calculate_mask实现，派生类可以重写
    IDType calculate_mask(const vector<IDType>& ids) {
        return find_min_mask_by_cost(ids, allow_id_conflict, sizeof(IDType) * 8);
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
    
protected:
    std::vector<std::pair<IDType, ValueType>> keyValue = {std::pair<IDType, ValueType>{1, ValueType{}}};
    IDType mask = 0;
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE>
class option_symbol_table : public base_symbol_table<option_symbol_table<KeyType, ValueType>, KeyType, ValueType> {
public:
    static constexpr const char* table_name = "option";
    using Base = base_symbol_table<option_symbol_table<KeyType, ValueType>, KeyType, ValueType>;
    using Base::keyValue;
    using Base::get_value;
    using Base::mask;

    // The requires clause should be in a concept or function template, not as a standalone statement
    // Moving the constraint logic to a static_assert
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");
                 
    inline IDType get_id(const char *sym) const {
        size_t len = 8;
        for (const char* p = sym+8; *p; ++p) len++;
        return get_id(sym, len);
    }

    inline IDType get_id(const char *sym, const size_t len) const {
        // eb2505-C-10000,  HO2504-P-2375 future option
        static const uint64_t masks[] = {
            0x0f,                 // 9: 1 byte
            0x0f0f,               // 10: 2 bytes
            0x0f0f0f,             // 11: 3 bytes
            0x0f0f0f0f,           // 12: 4 bytes
            0x0f0f0f0f0f,         // 13: 5 bytes
            0x0f0f0f0f0f0f,       // 14: 6 bytes
            0x0f0f0f0f0f0f0f,     // 15: 7 bytes
            0x010f0f0f0f0f0f0f,   // 16: 8 bytes
        };
        key64 idLeft = *(key64 *)sym;
        key64 leftInfo = _pext_u64(idLeft, 0x0f0f0f0f0f0f5f1f);
        key64 idRight = *(key64 *)(sym + 8);
        key64 rightInfo = _pext_u64(idRight, masks[len - 9]);
        return leftInfo + (rightInfo << 35);
    }

    inline IDType get_id(const char *sym, const char *exch) const {
        // 90005134 SZE, 10009005 SSE, for etf option
        return (*(key64 *)sym) + (exch[1] << 4);
    }

    inline ValueType get_value(const char *sym, size_t len) const {
        IDType id = get_id(sym, len);
        IDType k = _pext_u64(id, mask);
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
        key64 id = *(key64*)sym; //600000.SSE 000001.SZE 688001.BSE
        id += sym[8]<<4;
        return id;
    }
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE, bool allow_id_conflict = false>
class future_symbol_table : public base_symbol_table<future_symbol_table<KeyType, ValueType, allow_id_conflict>, KeyType, ValueType> {
public:
    using Base = base_symbol_table<future_symbol_table<KeyType, ValueType, allow_id_conflict>, KeyType, ValueType>;
    using Base::find_min_mask_by_cost;
    using Base::get_key;
    using Base::keyValue;
    
    static constexpr const char* table_name = "future";
    static_assert(std::is_same<KeyType, std::string>::value || 
                 std::is_same<KeyType, std::string_view>::value,
                 "KeyType must be std::string or std::string_view");
    
    //do not allow id conflict
    inline IDType calculate_mask(const vector<IDType>& ids) {
        return find_min_mask_by_cost(ids, false, sizeof(IDType) * 6);
    }

    inline IDType get_id(const char *sym) const {
        key64 id = *(uint64_t*)sym & 0xfFfFfFfFfFfF;
        //return id; // if the market data does't contain option code, we can use this
        return id & (-(sym[5] == 0 || sym[6] == 0));
    }

    inline ValueType get_value(const char *sym) const {
        IDType id = get_id(sym);
        IDType k = get_key(sym);
        if (keyValue[k].first == id) {
            return keyValue[k].second;
        }
        return ValueType{};
    }
};

// 中间基类，包含future类的共同功能
template<typename Derived, typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE, 
         int MAX_BIT_SIZE = 25> // 添加最大位大小参数
class future_no_conflict_base_table : public base_symbol_table<Derived, KeyType, ValueType> {
public:
    using Base = base_symbol_table<Derived, KeyType, ValueType>;
    using Base::find_min_mask_by_cost;
    using Base::mask;

    static constexpr const char* table_name = "future_base";
    
    future_no_conflict_base_table() {}
    
    // 共享的 sync_with_additional_symbols 实现
    bool sync_with_additional_symbols(const vector<KeyType>& all_symbols) {
        vector<std::pair<IDType, string>> allIDs;
        vector<IDType> ids;
        
        // 收集当前表中的ID
        for(auto &p : *this) {
            IDType id = static_cast<Derived*>(this)->get_id(p.first.data());
            for(int i = 0; i < allIDs.size(); i++) {
                if(allIDs[i].first == id) {
                    printf("error %s %s id: %lx\n", p.first.data(), allIDs[i].second.data(), id);
                    return false;
                }
            }
            ids.push_back(id);
            allIDs.push_back(std::pair<IDType, string>{id, p.first});
        }
        
        // 处理额外符号
        for(auto &p : all_symbols) {
            if(p.size() > 6) {
                continue;
            }
            IDType id = static_cast<Derived*>(this)->get_id(p.data());
            bool found = false;
            for(int i = 0; i < allIDs.size(); i++) {
                if(allIDs[i].first == id && allIDs[i].second != p) {
                    printf("error %s %s id: %lx\n", p.data(), allIDs[i].second.data(), id);
                    return false;
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

        if(all_ids.size() >= 1 << MAX_BIT_SIZE) {
            cout << "future table size too large, max size is " << (1 << MAX_BIT_SIZE) << endl;
            return false;
        }
        
        // 使用所有ID计算掩码，不允许冲突
        this->mask = this->find_min_mask_by_cost(all_ids, false, sizeof(IDType) * 6);

        future.resize(1 << __builtin_popcountll(this->mask));
        for(int i = 0; i < future.size(); i++) {
            future[i] = ValueType{};
        }

        for(auto &p : *this) {
            IDType k = this->get_key(p.first.data());
            future[k] = p.second;
        }
        
        return true;
    }

    inline ValueType get_value(const char *sym) const {
        IDType k = this->get_key(sym);
        return future[k];
    }

    
protected:
    std::vector<ValueType> future;
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE>
class future_no_conflict_table : public future_no_conflict_base_table<future_no_conflict_table<KeyType, ValueType>, KeyType, ValueType> {
public:
    using Base = future_no_conflict_base_table<future_no_conflict_table<KeyType, ValueType>, KeyType, ValueType>;
    using Base::mask;
    
    static constexpr const char* table_name = "future_no_conflict";
    
    future_no_conflict_table() {}
    
    inline IDType get_id(const char *sym) const {
        return *(uint64_t*)sym & (-(sym[5] == 0 || sym[6] == 0));
    }
};

template<typename KeyType = std::string, typename ValueType = DEFAULT_VALUE_TYPE>
class future_no_conf_no_opt_table : public future_no_conflict_base_table<future_no_conf_no_opt_table<KeyType, ValueType>, KeyType, ValueType> {
public:
    using Base = future_no_conflict_base_table<future_no_conf_no_opt_table<KeyType, ValueType>, KeyType, ValueType>;
    using Base::mask;
    
    static constexpr const char* table_name = "future_no_conf_no_opt";
    
    future_no_conf_no_opt_table() {}

    inline IDType get_id(const char *sym) const {
        return *(key64*)sym;
    }
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
         bool result = true;
         if(!stock.sync_with_unordered_map()) {
             
         }
         return stock.sync_with_unordered_map() && future.sync_with_unordered_map() && option.sync_with_unordered_map();
    }

    inline ValueType get_value(const char *sym) const {
        if(sym[5]==0||sym[6]==0) {
          return future.get_value(sym);
        } else if(sym[6]=='.') {
            return stock.get_value(sym);
        } else {
            return option.get_value(sym);
        }
    }

    inline ValueType get_value(const char *sym, size_t len) const {
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
     option_symbol_table<KeyType, ValueType> option;
};

#endif // CYB_HASH_H
