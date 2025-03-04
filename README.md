# CYBHash

Choose the "yummy bits" hash. It is a hashing method that maps symbols to values by selecting appropriate binary representations. It can quickly map symbols of Chinese futures, stocks, and options to arbitrary keys. It is extremely fast for futures symbols, approximately 10 times faster than std::unordered_map. This method borrows ideas from https://github.com/qlibs/mph and https://github.com/MengRao/str. Both of these projects are very fast. StrHash has a smart design that inherits from unordered_map, enabling the tool to directly replace existing code. And mph uses the pext instruction from BMI2 to extract the hash value, which is extremely fast. Compared to mph, the contribution of CYBHash is providing a method to convert option symbols to int64 and build the map at runtime. This feature makes it more convenient. Thanks to the authors of mph and StrHash.
