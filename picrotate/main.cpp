#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <chrono>
#include <vector>
#include <cmath>
#include <functional>
#include <random>

#include <new>

const int width = 1920;
const int height = 1920;

using namespace std;

int main() {
    std::random_device randev;

    int srcw = width;
    int srch = height;
    auto psrc = (uint32_t*)_aligned_malloc(srcw * srch * 4, 64);

    for(int i = 0; i < srcw * srch; ++i)
        psrc[i] = randev();
    
    int dstw = srch;
    int dsth = srcw;
    auto pdst = (uint32_t*)_aligned_malloc(dstw * dsth * 4, 64);

    auto seqr_rotate = [&]() {
        for(int i = 0; i < srch; ++i) {
            for(int j = 0; j < srcw; ++j) {
                pdst[j * dstw + i] = psrc[i * srcw + j];
            }
        }
    };

    auto seqw_rotate = [&]() {
        for(int i = 0; i < srch; ++i) {
            for(int j = 0; j < srcw; ++j) {
                pdst[j * dstw + i] = psrc[i * srcw + j];
            }
        }
    };

    // 最小需求：2K的L1。一个缓存行64字节放16个像素，宽高都是16一共1K，读写都缓存所以要2K
    int mincachesize = (std::hardware_constructive_interference_size) * (std::hardware_constructive_interference_size / 4) * 2;
    auto cache_rotate = [&](int cachesize) {
        int src_tile_x = (int)sqrt(cachesize / mincachesize) * 16;
        int src_tile_y = src_tile_x;
        int src_tile_x_count = (srcw + src_tile_x - 1) / src_tile_x;
        int src_tile_y_count = (srch + src_tile_y - 1) / src_tile_y;
        for(int y = 0; y < src_tile_x_count; ++y) {
            for(int x = 0; x < src_tile_y_count; ++x) {
                int endj = (y + 1) * src_tile_y;
                if (endj > srch)
                    endj = srch;
                for(int j = y * src_tile_y; j < endj; ++j) {
                    int endi = (x + 1) * src_tile_x;
                    if (endi > srcw)
                        endi = srcw;
                    for(int i = x * src_tile_x; i < endi; ++i) {
                        pdst[j * dstw + i] = psrc[i * srcw + j];
                    }
                }
            }
        }
    };

    auto do_test = [&](auto name, auto proc) {
        using namespace std::chrono;
        using clock = std::chrono::steady_clock;
        auto begin = clock::now();
        auto cost_time = clock::now() - begin;
        int count = 0;
        for(;;) {
            proc();
            ++count;

            if (count % 100 == 0) {
                cost_time = clock::now() - begin;
                if (cost_time > seconds(10)) {
                    printf("%s: avg: %g fps, %g MB/S\n", name, 
                        count * 1e9 / duration_cast<nanoseconds>(cost_time).count(),
                        count * (srcw * srch * 4 / 1e6) * 1e9 / duration_cast<nanoseconds>(cost_time).count()
                    );
                    break;
                }
            }
        }
    };

    // 热身
    std::vector<uint32_t> chk(dstw * dsth * 4);
    for(int i = 0; i < 100; ++i)
        memset(chk.data(), i, chk.size() * 4);
    printf("rotate %dx%d image\n", srcw, srch);
    seqr_rotate();
    memcpy(chk.data(), pdst, dstw * dsth * 4);
    memset(pdst, 0, dstw * dsth * 4);
    cache_rotate(mincachesize);
    if (memcmp(chk.data(), pdst, dstw * dsth * 4) == 0) {
        do_test("sequence read", seqr_rotate);
        do_test("sequence write", seqw_rotate);
        do_test("2K cached", std::bind(cache_rotate, mincachesize));
        do_test("L1 cached", std::bind(cache_rotate, 32768));
        do_test("4x L1 cached", std::bind(cache_rotate, 4 * 32768));
        do_test("L2 cached", std::bind(cache_rotate, 1048576 * 2));
    } else {
        printf("%s", "incorrect cached rw implementation.\n");
    }

    _aligned_free(psrc);
    _aligned_free(pdst);

    return 0;
}
