#pragma once

#include <chrono>
#include <algorithm>
#include <iostream>
#include <numeric>

#include "speed_metrics.h"

template<class T>
int runtest() try {
    std::vector<uint32_t> inputBuffer1(1920 * 1920, 0xff800000); // Example input buffer
    std::vector<uint32_t> inputBuffer2(1920 * 1920, 0xff008000); // Example input buffer
    std::vector<uint8_t> outputYBuffer(1920 * 1920), outputUBuffer(1920 * 1920 / 4), outputVBuffer(1920 * 1920 / 4);
    
    for(int i = 0; i < 2; ++i) {
        for(int j = 0; j < 2; ++j) {
            auto do_test = [&]() {
                using namespace std::chrono;
                std::cout << "Running test with compute shader: " << (i == 0 ? "Yes" : "No") 
                        << ", map input buffer: " << (j == 0 ? "Yes" : "No") << std::endl;

                uint64_t errval = 0;

                SpeedMetrics outSpeed;
                T colorConv(i == 0, j == 0, 1920, 1920, 1920 * 4, 1920, 1920 / 2);

                auto beginTime = high_resolution_clock::now();
                auto iter_begin = beginTime;
                int frameCount = 0;
                for(;;) {
                    if (frameCount % 2)
                        colorConv.feedInput((char*)inputBuffer1.data());
                    else
                        colorConv.feedInput((char*)inputBuffer2.data());
                    auto [mappedY, mappedU, mappedV] = colorConv.mapResult();
                    auto py = mappedY, pu = mappedU, pv = mappedV;
                    outSpeed.RunCopy([&]() {
                        uint64_t copy_bytes = 0;
                        memcpy(outputYBuffer.data(), py, 1920 * 1920);
                        copy_bytes += 1920 * 1920;
                        memcpy(outputUBuffer.data(), pu, 1920 * 1920 / 4);
                        copy_bytes += 1920 * 1920 / 4;
                        memcpy(outputVBuffer.data(), pv, 1920 * 1920 / 4);
                        copy_bytes += 1920 * 1920 / 4;
                        return copy_bytes;
                    });

                    colorConv.unmapResult();
                    ++frameCount;

                    if (steady_clock::now() - iter_begin > seconds(1)) {
                        std::cout << "Processed " << 
                            (frameCount * 1000.0) / 
                                duration_cast<milliseconds>(
                                    steady_clock::now() - beginTime
                                ).count() 
                            << " frames in 1 second" << std::endl;
                        iter_begin = steady_clock::now();
                    }

                    if (steady_clock::now() - beginTime > seconds(10)) {
                        break;
                    }

                    if (frameCount == 1) {
                        uint64_t errval_local = 0;

                        errval_local = std::accumulate(outputYBuffer.begin(), outputYBuffer.end(), errval_local, [](uint64_t sum, uint8_t val) {
                            return sum + std::abs((int16_t)(uint16_t)val - 75);
                        });
                        errval_local = std::accumulate(outputUBuffer.begin(), outputUBuffer.end(), errval_local, [](uint64_t sum, uint8_t val) {
                            return sum + std::abs((int16_t)(uint16_t)val - 85);
                        });
                        errval_local = std::accumulate(outputVBuffer.begin(), outputVBuffer.end(), errval_local, [](uint64_t sum, uint8_t val) {
                            return sum + std::abs((int16_t)(uint16_t)val - 74);
                        });

                        errval += errval_local;
                    }
                    else if (frameCount == 2) {
                        uint64_t errval_local = 0;

                        errval_local = std::accumulate(outputYBuffer.begin(), outputYBuffer.end(), errval_local, [](uint64_t sum, uint8_t val) {
                            return sum + std::abs((int16_t)(uint16_t)val - 14);
                        });
                        errval_local = std::accumulate(outputUBuffer.begin(), outputUBuffer.end(), errval_local, [](uint64_t sum, uint8_t val) {
                            return sum + std::abs((int16_t)(uint16_t)val - 192);
                        });
                        errval_local = std::accumulate(outputVBuffer.begin(), outputVBuffer.end(), errval_local, [](uint64_t sum, uint8_t val) {
                            return sum + std::abs((int16_t)(uint16_t)val - 117);
                        });

                        errval += errval_local;
                    }
                }
                std::cout << "In / Out memcpy (MB/s): " << colorConv.GetInputSpeedInMBps() << " / " << outSpeed.GetSpeedInMBps() << std::endl;
                std::cout << "Error distance: " << errval * 1.0 / 2 << std::endl;
            };
            do_test();
        }
    };


    return 0;
} catch(std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
} catch(...) {
    std::cerr << "Unknown error occurred" << std::endl;
    return 1;
}
