# scopedalloc.h
C++14 allocator for STL containers

try following sample code with:
```bash
g++ main.cpp -g -fsanitize=address -Wall -Werror -std=c++14
```

```cpp
// main.cpp
#include <cstdlib>
#include <iostream>
#include <list>
#include <deque>
#include <map>
#include <cstring>
#include <unordered_map>
#include <numeric>
#include "scopedalloc.h"

int main()
{
    svobuf<int, 12> buf;

    buf.c.push_back(0);
    buf.c.push_back(1);
    buf.c.push_back(2);

    scoped_alloc::dynamic_arena<> f;
    std::vector<std::vector<int, scoped_alloc::allocator<int>>> vec;
    vec.resize(12, typename decltype(vec)::value_type(f));


    f.alloc(12800);
    vec.back().push_back(0);

    std::list<std::vector<int, scoped_alloc::allocator<int>>> lis;
    lis.push_back(typename decltype(vec)::value_type(f));
    lis.push_back(typename decltype(vec)::value_type(f));
    lis.push_back(typename decltype(vec)::value_type(f));
    lis.push_back(typename decltype(vec)::value_type(f));
    lis.push_back(typename decltype(vec)::value_type(f));

    lis.pop_back();
    lis.pop_back();
    lis.pop_back();

    lis.front().push_back(12);

    using overalign = std::aligned_storage_t<128, 32>;

    overalign t;
    std::memset(&t, 0, sizeof(t));
    std::cout << alignof(t) << std::endl;

    scoped_alloc::dynamic_arena<32> overf;
    std::vector<overalign, scoped_alloc::allocator<overalign, alignof(overalign)>> ovec(overf);

    overf.alloc(1024);

    ovec.push_back(t);
    ovec.push_back({});
    ovec.push_back({});

    std::map<int, int, std::less<int>, scoped_alloc::allocator<overalign, alignof(overalign)>> tmap(overf);
    tmap[12] = 23;

    scoped_alloc::fixed_arena<1024, 32> fixedf;
    std::map<int, int, std::less<int>, scoped_alloc::allocator<overalign, alignof(overalign)>> fixedmap(fixedf);
    fixedmap.insert({2, 10});

    scoped_alloc::dynamic_arena<8> h;
    std::unordered_map<int, int *, std::hash<int>, std::equal_to<int>, scoped_alloc::allocator<std::pair<const int, int *>, 8>> hashmap(h);

    std::vector<int> v(100);
    std::iota(v.begin(), v.end(), 0);

    const size_t n = 10000;

    h.alloc(n * 40);
    hashmap.reserve(n);
    for(size_t i = 0; i < n; ++i){
        hashmap[i] = v.data() + i % v.size();
    }

    std::cout << "usage: " << h.usage() << std::endl;
    std::cout << "element size : " << sizeof(decltype(hashmap)::value_type) << std::endl;
    std::cout << "cost         : " << ((h.usage() * h.get_buf().size) / n - sizeof(decltype(hashmap)::value_type)) << std::endl;


    scoped_alloc::dynamic_arena<8> rb;
    std::map<int, int *, std::less<int>, scoped_alloc::allocator<std::pair<const int, int *>, 8>> rbmap(rb);

    const size_t m = 12800;
    rb.alloc(m * 64);

    for(size_t i = 0; i < m; ++i){
        rbmap[i] = v.data() + i % v.size();
    }

    std::cout << "usage: " << rb.usage() << std::endl;
    std::cout << "element size : " << sizeof(decltype(rbmap)::value_type) << std::endl;
    std::cout << "cost         : " << (rb.used() * 1.0f / m - sizeof(decltype(rbmap)::value_type)) << std::endl;

    using list_valtype = std::pair<int, int *>;
    scoped_alloc::dynamic_arena<8> la;
    std::list<list_valtype, scoped_alloc::allocator<list_valtype, 8>> l(la);

    la.alloc(4096);
    l.push_back({1, nullptr});
    l.push_back({2, nullptr});

    scoped_alloc::dynamic_arena<8> dqa;
    dqa.alloc(128);
    std::deque<int, scoped_alloc::allocator<int, 8>> dq(dqa);
    return 0;
}


```
