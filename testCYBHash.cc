/*
MIT License

Copyright (c) 2019 Meng Rao <raomeng1@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
#include "CYB_Hash.h"
 #include "StrHash.h"

// 用于文件数据的全局变量
const int STR_LEN = 16;
long long int loop = 1000;                      
typedef std::string KeyType;
std::vector<KeyType> tbl_data;  // 订阅的合约
std::vector<KeyType> find_data; // 行情数据
typedef unsigned int key32;
typedef key32 VALUE_TYPE;
typedef key64 IDType;
typedef Str<STR_LEN> Key;
typedef short Value;
vector<std::pair<IDType, VALUE_TYPE>> _symbolID;

// 计时函数
inline uint64_t getns() { return std::chrono::high_resolution_clock::now().time_since_epoch().count(); }

string mode = "future";
bool check_sym(const string &s) {
    if (s.empty() || s[0] == '#') {
        return false;
    }
    if (mode == "combine") {
        return true;
    }
    if (s.length() <= 6) {
        if (mode == "future") {
            return true;
        } else {
            return false;
        }
    } else if (s[6] == '.') { // 600000.SSE, 000001.SZE
        if (mode == "stock") {
            return true;
        } else {
            return false;
        }
    } else if(s.length()>8) {
        if (mode == "option") {
            return true;
        } else {
            return false;
        }   
    }
    return false;
}

// 从文件读取数据
bool load_data_from_file(const std::string &filename, const std::string tFile) {
    std::ifstream file(filename);
    std::ifstream testFile(tFile);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return false;
    }
    if (!testFile.is_open()) {
        std::cerr << "Error: Could not open test file " << tFile << std::endl;
        return false;
    }

    tbl_data.clear();
    find_data.clear();

    string s;
    while (getline(file, s)) {
        // 去除可能的空白字符和换行符
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);

        if (!s.empty() && check_sym(s)) {
            tbl_data.push_back(s); // 读取每个订阅合约
        }
    }

    while (getline(testFile, s)) {
        // 去除可能的空白字符和换行符
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);

        if (!s.empty()) {
            find_data.push_back(s); // 读取每个行情数据
        }
    }

    std::cout << "Loaded " << tbl_data.size() << " symbols and " << find_data.size() << " queries from " << filename
              << std::endl;

    return true;
}

// 使用文件数据测试unordered_map
void bench_file_unordered_map() {
    // 创建标准映射用于查找
    std::unordered_map<KeyType, uint16_t> map;
    for (size_t i = 0; i < tbl_data.size(); i++) {
        map[tbl_data[i]] = i + 1;
    }

    // 计时并执行查找
    uint64_t sum = 0;
    auto before = getns();

    for (int l = 0; l < loop; l++) {
        for (auto &s : find_data) {

            // std::string_view key(s.data(), STR_LEN);
            auto it = map.find(s);
            if (it != map.end()) {
                sum += it->second;
            }
        }
    }

    auto after = getns();
    double avg_ns = static_cast<double>(after - before) / (loop * find_data.size());

    std::cout << "unordered_map file data: sum=" << sum << ", avg time=" << avg_ns << "ns per lookup"
              << " (" << (1000000000.0 / avg_ns) << " lookups/sec)" << std::endl;
}

// 使用文件数据进行综合比较测试
void bench_with_file_data() {
    if (tbl_data.empty() || find_data.empty()) {
        std::cout << "No data loaded from file. Skipping file data benchmark." << std::endl;
        return;
    }

    std::cout << "\n--- Benchmarking with data from file ---" << std::endl;
    std::cout << "Symbol count: " << tbl_data.size() << ", Query count: " << find_data.size() << std::endl;

    // 运行各种算法的基准测试
    bench_file_unordered_map();
    // 暂时移除其他基准测试
    // bench_file_robin_map();
    // bench_file_robin_hood_map();
    // bench_file_dense_hash_map();
}


template<uint32_t HashFunc>
void bench_hash() {
  StrHash<STR_LEN, Value, 0, HashFunc, true> ht;
  for (int i = 0; i < tbl_data.size(); i++) {
    //cout<<tbl_data[i]<<endl;
    ht.emplace(tbl_data[i].data(), i + 1);
  }
  if (!ht.doneModify()) {
    cout << "table size too large, try using template parameter SmallTbl=false" << endl;
    return;
  }
  // the std::map can be cleared to save memory if only fastFind is called afterwards
  // ht.clear();

  key64 sum = 0;
  auto before = getns();
  for (int l = 0; l < loop; l++) {
    for (auto& s : find_data) {
      //int result = ht.fastFind(*(const Key*)s.data());
      //if(result>0 && result != ma[s]){
      //  cout<<"error "<<s<<" "<<result<<" "<<ma[s]<<endl;
      //}
      sum += ht.fastFind(*(const Key*)s.data());
    }
  }
  auto after = getns();
  double avg_ns = static_cast<double>(after - before) / (loop * find_data.size());
  cout << "bench_hash " << HashFunc << " sum: " << sum
       << " avg lat: " << avg_ns
       <<" ns per lookup " << (1000000000.0 / avg_ns) <<" lookups/sec" << endl;
}


void bench_combine_symbol_table() {
    combine_symbol_table<std::string, VALUE_TYPE> combine_table;
    for (int i = 0; i < tbl_data.size(); i++) {
        combine_table[tbl_data[i]] = i + 1;
    }
    if(!combine_table.sync_with_unordered_map()) {
        cout<<"table size too large"<<endl;
        return;
    }

    uint64_t sum = 0;
    auto before = getns();
    for (int l = 0; l < loop; l++) {
        for (auto &s : find_data) {
            sum += combine_table.get_value(s.data());
        }
    }
    auto after = getns();
    double avg_ns = static_cast<double>(after - before) / (loop * find_data.size());
    cout << "combine_symbol_table sum: " << sum << " avg lat: " << avg_ns << " ns per lookup " << (1000000000.0 / avg_ns)
         << " lookups/sec" << endl;
}

void bench_future_symbol_table() {
    future_symbol_table<std::string, VALUE_TYPE> future_table;
    for (int i = 0; i < tbl_data.size(); i++) {
        future_table[tbl_data[i]] = i + 1;
    }

    if(!future_table.sync_with_unordered_map()) {
        cout<<"table size too large"<<endl;
        return;
    }
    uint64_t sum = 0;
    auto before = getns();
    for (int l = 0; l < loop; l++) {
        for (auto &s : find_data) {
            // int result = table.get_value(s.data());
            // if( (symbols.find(s)==symbols.end() && result!=0) || (symbols.find(s)!=symbols.end() && symbols[s] !=
            // result) ) {
            //   cout<<"error "<<s<<" "<<symbols[s]<<" "<<result<<endl;
            // }
            sum += future_table.get_value(s.data());
        }
    }
    auto after = getns();
    double avg_ns = static_cast<double>(after - before) / (loop * find_data.size());
    cout << "future_symbol_table sum: " << sum << " avg lat: " << avg_ns << " ns per lookup " << (1000000000.0 / avg_ns)
         << " lookups/sec" << endl;

    future_no_conflict_symbol_table<std::string, VALUE_TYPE> future_no_conflict_table;
    for (int i = 0; i < tbl_data.size(); i++) {
        future_no_conflict_table[tbl_data[i]] = i + 1;
    }
    if(!future_no_conflict_table.sync_with_additional_symbols(find_data)) {
        cout<<"table size too large"<<endl;
        return;
    }
    uint64_t sum2 = 0;
    auto before2 = getns();
    for (int l = 0; l < loop; l++) {
        for (auto &s : find_data) {
            sum2 += future_no_conflict_table.get_value(s.data());
        }
    }
    auto after2 = getns();
    double avg_ns2 = static_cast<double>(after2 - before2) / (loop * find_data.size());
    cout << "future_no_conflict_symbol_table sum: " << sum2 << " avg lat: " << avg_ns2 << " ns per lookup "
         << (1000000000.0 / avg_ns2) << " lookups/sec" << endl;
}

void bench_stock_symbol_table() {
    stock_symbol_table<std::string, VALUE_TYPE> stock_table;
    for (int i = 0; i < tbl_data.size(); i++) {
        stock_table[tbl_data[i]] = i + 1;
    }

    if(!stock_table.sync_with_unordered_map()) {
        cout<<"table size too large"<<endl;
        return;
    }
    uint64_t sum = 0;
    auto before = getns();
    for (int l = 0; l < loop; l++) {
        for (auto &s : find_data) {
            sum += stock_table.get_value(s.data());
        }
    }
    auto after = getns();
    double avg_ns = static_cast<double>(after - before) / (loop * find_data.size());
    cout << "stock_symbol_table sum: " << sum << " avg lat: " << avg_ns << " ns per lookup " << (1000000000.0 / avg_ns)
         << " lookups/sec" << endl;
}

void bench_option_symbol_table() {
    option_symbol_table<std::string, VALUE_TYPE> option_table;
    for (int i = 0; i < tbl_data.size(); i++) {
        option_table[tbl_data[i]] = i + 1;
    }

    if(!option_table.sync_with_unordered_map()) {
        cout<<"table size too large"<<endl;
        return;
    }
    uint64_t sum = 0;
    auto before = getns();
    for (int l = 0; l < loop; l++) {
        for (auto &s : find_data) {
            sum += option_table.get_value(s.data());
        }
    }
    auto after = getns();
    double avg_ns = static_cast<double>(after - before) / (loop * find_data.size());
    cout << "option_symbol_table sum: " << sum << " avg lat: " << avg_ns << " ns per lookup " << (1000000000.0 / avg_ns)
         << " lookups/sec" << endl;
}

int main(int argc, char **argv) {
    // 运行硬编码数据的性能基准测试
    std::cout << "\n--- Benchmarking  ---" << std::endl;

    // 如果有命令行参数指定数据文件，则从文件读取数据
    std::string datafile = "shfe.txt"; // 默认使用 shfe.txt
    std::string testfile = "shfe.txt"; // 默认使用同一个文件进行测试
    if (argc > 1) {
        datafile = argv[1];
        if (argc > 2) {
            testfile = argv[2];
        }
        if (argc > 3) {
            mode = argv[3];
        }
        // 尝试加载文件数据
        load_data_from_file(datafile, testfile);
        while(loop>5 && loop*find_data.size()>100000000){
            loop /= 2;
        }
        if (argc > 4) {
            loop = atoi(argv[4]);
        }
    } else {
        load_data_from_file(datafile, testfile);
    }
    std::cout << "subscribe: " << datafile << " query: " << testfile << " mode: " << mode << " loop: " << loop << std::endl;


    // 运行各种基准测试
    bench_hash<1>();
    bench_hash<2>();
    bench_hash<3>();
    bench_hash<4>();
    bench_hash<5>();
    //bench_hash<6>();
    if (mode == "future") {
        bench_future_symbol_table();
    } else if (mode == "stock") {
        bench_stock_symbol_table();
    } else if(mode=="option") {
        bench_option_symbol_table();
    } else {
        bench_combine_symbol_table();
    }
    bench_file_unordered_map();

    return 0;
}