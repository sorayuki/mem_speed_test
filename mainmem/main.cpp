#define SIMDPP_ARCH_X86_AVX2
#include "simdpp/simd.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <malloc.h>
#include <chrono>

struct STD {
    static void cpy(void* dst, const void* src, intptr_t size) {
        std::memcpy(dst, src, size);
    }
};

struct ALG {
    static void cpy(void* dst, const void* src, intptr_t size) {
        std::copy_n((const char*)src, size, (char*)dst);
    }
};

struct SIMD {
    static void cpy(void* dst, const void* src, intptr_t size) {
        register simdpp::int64x4 tmp;
        for(int i = 0; i < size; i += 32) {
            tmp = simdpp::load((const char*)src + i);
            simdpp::prefetch_read((const char*)src + i + 512);
            simdpp::stream((char*)dst + i, tmp);
        }
    }
};

template<class IMP>
void DoTest(const char* name) {
    const size_t size = 1024 * 1024 * 1024; // 1G
    const size_t loop = 150;

    void* src = _aligned_malloc(size, 32);
    void* dst = _aligned_malloc(size, 32);
    for(int i = 0; i < 5; ++i) // warm up
        memcpy(dst, src, size); // resolve page fault

    using namespace std::chrono;

    auto begin = steady_clock::now();
    for(int i = 0; i < loop; ++i) {
        IMP::cpy(dst, src, size);
    }
    auto cost = steady_clock::now() - begin;
    auto speed = (size * loop) / 1048576.0 / (duration_cast<nanoseconds>(cost).count() / 1000000000.0);
    
    printf("using %s: %g MB/S\n", name, speed);

    _aligned_free(src);
    _aligned_free(dst);
}

int main(int, char**){
    DoTest<STD>("std::memcpy");
    DoTest<STD>("std::copy_n");
    DoTest<SIMD>("simdpp");
    return 0;
}