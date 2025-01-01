#define SIMDPP_ARCH_X86_AVX2
#include "simdpp/simd.h"

#include <malloc.h>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <atomic>
#include <thread>
#include <random>
#include <functional>
#include <FastMemcpy_Avx.h>

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
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
};

struct FastMemcpy {
    static void cpy(void* dst, const void* src, intptr_t size) {
        memcpy_fast(dst, src, size);
    }
};

struct TmpTest {
    static void cpy(void* dst, const void* src, intptr_t size) {
        auto ps = (const uint8_t*) src;
        auto pd = (uint8_t*) dst;

        int us = 0;
        // 一个内存行一个内存行处理
        for(int off = 0; off < size; off += 65536) {
            // 先cache来源的内存行
            for(int cl = 0; cl < 65536; cl += 64)
                us += ps[off + cl];
            // 复制
            memcpy(pd + off, ps + off, 65536);
        }
    }
};

const size_t size = 1024 * 1024 * 1024; // 1024 MB
const size_t loop = 30;

template<class IMP>
void DoTest(const char* name, std::atomic_int64_t* speedBps) {
    void* src = _aligned_malloc(size, 65536);
    void* dst = _aligned_malloc(size, 65536);
    for(int i = 0; i < 3; ++i) // warm up
        memcpy(dst, src, size); // resolve page fault

    using namespace std::chrono;

    auto begin = steady_clock::now();
    for(int i = 0; i < loop; ++i) {
        IMP::cpy(dst, src, size);
    }
    auto cost = steady_clock::now() - begin;
    auto speed = (size * loop) / 1048576.0 / (duration_cast<nanoseconds>(cost).count() / 1000000000.0);
    
    printf("using %s: %g MB/S\n", name, speed);

    speedBps->fetch_add(size * loop / (duration_cast<nanoseconds>(cost).count() / 1000000000.0));

    _aligned_free(src);
    _aligned_free(dst);
}

int main(int, char**){
    // const int parallel = std::thread::hardware_concurrency();
    const int parallel = 8;
    std::vector<std::thread> t;

    printf("thread: %d\n", parallel);

    std::atomic_int64_t memcpyBps{0}, simdppBps{0}, fastmemcpyBps{0}, tmptestBps{0};

    for(int i = 0; i < parallel; ++i)
        t.emplace_back([&]() {
            std::vector<std::function<void()>> funcs;

            funcs.emplace_back([&]() { DoTest<STD>("std::memcpy", &memcpyBps); });
            funcs.emplace_back([&]() { DoTest<SIMD>("simdpp", &simdppBps); });
            funcs.emplace_back([&]() { DoTest<FastMemcpy>("FastMemcpy", &fastmemcpyBps); });
            funcs.emplace_back([&]() { DoTest<TmpTest>("TmpTest", &tmptestBps); });

            std::mt19937 mt(std::random_device{}());
            std::shuffle(funcs.begin(), funcs.end(), mt);
            for(auto& f: funcs)
                f();
        });

    for(int i = 0; i < parallel; ++i)
        t[i].join();

    printf("%s", "\n");
    printf("%s", "Result\n");
    printf("std::memcpy %g MB/S\n", memcpyBps.load() / 1048576.0);
    printf("simdpp %g MB/S\n", simdppBps.load() / 1048576.0);
    printf("FastMemcpy %g MB/S\n", fastmemcpyBps.load() / 1048576.0);
    printf("TmpTest %g MB/S\n", tmptestBps.load() / 1048576.0);

    printf("%s", "End.\n");

    int dummy;
    scanf("%d", &dummy);

    return 0;
}