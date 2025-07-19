#pragma once

#include <chrono>
#include <algorithm>
#include <iostream>

template<class T>
int runtest() try {
    std::vector<uint32_t> inputBuffer1(1920 * 1920, 0xffff0000); // Example input buffer
    std::vector<uint32_t> inputBuffer2(1920 * 1920, 0xff00ff00); // Example input buffer
    std::vector<char> outputYBuffer(1920 * 1920), outputUBuffer(1920 * 1920 / 4), outputVBuffer(1920 * 1920 / 4);
    
    for(int i = 0; i < 2; ++i) {
        for(int j = 0; j < 2; ++j) {
            auto do_test = [&]<typename T>() {
                using namespace std::chrono;
                std::cout << "Running test with compute shader: " << (i == 0 ? "Yes" : "No") 
                        << ", map input buffer: " << (j == 0 ? "Yes" : "No") << std::endl;
                
                steady_clock::duration copy_cost{ 0 };
                uint64_t copy_bytes{ 0 };
                T colorConv(i == 0, j == 0, 1920, 1920, 1920 * 4, 1920, 1920 / 2);

                auto beginTime = steady_clock::now();
                auto iter_begin = beginTime;
                int frameCount = 0;
                for(;;) {
                    if (frameCount % 2)
                        colorConv.feedInput((char*)inputBuffer1.data());
                    else
                        colorConv.feedInput((char*)inputBuffer2.data());
                    auto [mappedY, mappedU, mappedV] = colorConv.mapResult();
                    auto startTime = steady_clock::now();
                    memcpy(outputYBuffer.data(), mappedY, 1920 * 1920);
                    copy_bytes += 1920 * 1920;
                    memcpy(outputUBuffer.data(), mappedU, 1920 * 1920 / 4);
                    copy_bytes += 1920 * 1920 / 4;
                    memcpy(outputVBuffer.data(), mappedV, 1920 * 1920 / 4);
                    copy_bytes += 1920 * 1920 / 4;
                    copy_cost += steady_clock::now() - startTime;
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
                        if (std::equal_range(outputYBuffer.begin(), outputYBuffer.end(), -107).first == outputYBuffer.end()) {
                            std::cerr << "check fail" << std::endl;
                        }
                        if (std::equal_range(outputUBuffer.begin(), outputUBuffer.end(), 43).first == outputUBuffer.end()) {
                            std::cerr << "check fail" << std::endl;
                        }
                        if (std::equal_range(outputVBuffer.begin(), outputVBuffer.end(), 21).first == outputVBuffer.end()) {
                            std::cerr << "check fail" << std::endl;
                        }
                    }
                    else if (frameCount == 2) {
                        if (std::equal_range(outputYBuffer.begin(), outputYBuffer.end(), 29).first == outputYBuffer.end()) {
                            std::cerr << "check fail" << std::endl;
                        }
                        if (std::equal_range(outputUBuffer.begin(), outputUBuffer.end(), -1).first == outputUBuffer.end()) {
                            std::cerr << "check fail" << std::endl;
                        }
                        if (std::equal_range(outputVBuffer.begin(), outputVBuffer.end(), 107).first == outputVBuffer.end()) {
                            std::cerr << "check fail" << std::endl;
                        }
                    }
                }
                std::cout << "Read mapped buffer speed: " << copy_bytes / 1024.0 / 1024.0 / (duration_cast<milliseconds>(copy_cost).count() / 1000.0) << " MB/s" << std::endl;
            };
            do_test.template operator()<T>();
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
