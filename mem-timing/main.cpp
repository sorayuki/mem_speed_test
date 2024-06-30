#include <stdlib.h>
#include <stdint.h>
#include <iostream>
#include <format>
#include <ranges>
#include <chrono>
#include <random>

class StopWatch {
    using Clock = std::chrono::steady_clock;
    Clock::time_point begin_;
public:
    StopWatch() {
        begin_ = Clock::now();
    }

    size_t cost_ns() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin_).count();
    }
};

int main() {
    constexpr int64_t retrycount = 5;
    constexpr int64_t totalsize = 1 * 1024 * 1024 * 1024;

    constexpr int64_t freq = 2600; // 2600MHz = 5600MT/S
    constexpr int64_t mem_chs = 4; // 2 channels per dimm, 2 dimm
    constexpr int64_t mem_ch_size = 8; // channel width = 32bit
    constexpr int64_t rowsize = (1 << 10) * 64; // 2^10, from HWiNFO -> memory -> row 0 -> column address bits. cacheline = 64
    constexpr int64_t tCL = 42;
    constexpr int64_t tRCD = 42;
    constexpr int64_t tRP = 42;
    constexpr int64_t tRAS = 82;

    auto ptr = (uint8_t*)_aligned_malloc(totalsize, rowsize);

    std::random_device rnd;
    for(int i = 0; i < totalsize; i += 4096)
        ptr[i] = rnd() % 256;
    
    int64_t fullseq;
    int64_t cacheonly;
    int64_t computecost;

    {
        StopWatch w1;
        register int sum = 0;
        for(int t = 0; t < retrycount; ++t) {
            for(int row = 0; row < totalsize; row += rowsize) {
                for(int rowoff = 0; rowoff < rowsize; rowoff += 64) { // change cacheline every iter
                    for(int lineoff = 0; lineoff < 64; ++lineoff) {
                        sum += ptr[row + rowoff + lineoff];
                    }
                }
            }
        }
        auto cost_ns = w1.cost_ns() / retrycount;
        fullseq = cost_ns;
        std::clog
            << sum
            << "\rrow seq, cacheline seq: \t" << cost_ns << std::endl;
    }

    {
        StopWatch w1;
        register int sum = 0;
        for(int t = 0; t < retrycount; ++t) {
            for(int row = 0; row < totalsize; row += rowsize) {
                for(int rowoff = 0; rowoff < rowsize; rowoff += 64) { // change cacheline every iter
                    for(int lineoff = 0; lineoff < 1; ++lineoff) {
                        sum += ptr[row + rowoff + lineoff];
                    }
                }
            }
        }
        auto cost_ns = w1.cost_ns() / retrycount;
        cacheonly = cost_ns;
        computecost = (fullseq - cacheonly) * 64 / 63;
        std::clog
            << sum
            << "\rrow seq, cacheline skip: \t" << cost_ns << std::endl;
    }
    
    {
        StopWatch w1;
        register int sum = 0;
        for(int t = 0; t < retrycount; ++t) {
            for(int row = 0; row < totalsize; row += rowsize) {
                for(int lineoff = 0; lineoff < 64; ++lineoff) {
                    for(int rowoff = 0; rowoff < rowsize; rowoff += 64) { // change cacheline every iter
                        sum += ptr[row + rowoff + lineoff];
                    }
                }
            }
        }
        auto cost_ns = w1.cost_ns() / retrycount - computecost;
        std::clog
            << sum
            << "\rrow seq, cacheline jump (no compute): \t" << cost_ns << std::endl;
    }

    {
        StopWatch w1;
        register int sum = 0;
        for(int t = 0; t < retrycount; ++t) {
            for(int row = totalsize - rowsize; row >= 0; row -= rowsize) {
                for(int rowoff = 0; rowoff < rowsize; rowoff += 64) {
                    for(int lineoff = 0; lineoff < 64; ++lineoff) { // change cacheline every iter
                        sum += ptr[row + rowoff + lineoff];
                    }
                }
            }
        }
        auto cost_ns = w1.cost_ns() / retrycount - computecost;
        std::clog
            << sum
            << "\rrow rev, cacheline seq (no compute): \t" << cost_ns << std::endl;
    }

    {
        StopWatch w1;
        register int sum = 0;
        for(int t = 0; t < retrycount; ++t) {
            for(int row = totalsize - rowsize; row >= 0; row -= rowsize) {
                for(int lineoff = 0; lineoff < 64; ++lineoff) {
                    for(int rowoff = 0; rowoff < rowsize; rowoff += 64) { // change cacheline every iter
                        sum += ptr[row + rowoff + lineoff];
                    }
                }
            }
        }
        auto cost_ns = w1.cost_ns() / retrycount - computecost;
        std::clog
            << sum
            << "\rrow rev, cacheline jump (no compute): \t" << cost_ns << std::endl;
    }

    {
        StopWatch w1;
        register int sum = 0;
        for(int t = 0; t < retrycount; ++t) {
            for(int rowoff = 0; rowoff < rowsize; rowoff += 64) {
                for(int lineoff = 0; lineoff < 64; ++lineoff) {
                    for(int row = 0; row < totalsize; row += rowsize) { // change row every iter
                        sum += ptr[row + rowoff + lineoff];
                    }
                }
            }
        }

        auto cost_ns = w1.cost_ns() / retrycount - computecost;
        std::clog
            << sum
            << "\rcacheline seq, row jump (no compute): \t" << cost_ns << std::endl;
    }
}