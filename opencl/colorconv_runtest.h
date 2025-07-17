#pragma once

#include <chrono>
#include <iostream>

template<class T>
int runtest() try {
    std::vector<char> inputBuffer(1920 * 1920 * 4); // Example input buffer
    std::vector<char> outputYBuffer(1920 * 1920), outputUBuffer(1920 * 1920 / 4), outputVBuffer(1920 * 1920 / 4);
    
    for(int i = 0; i < 2; ++i) {
        for(int j = 0; j < 2; ++j) {
            auto do_test = [&]<typename T>() {
                std::cout << "Running test with compute shader: " << (i == 0 ? "Yes" : "No") 
                        << ", map input buffer: " << (j == 0 ? "Yes" : "No") << std::endl;
                T colorConv(i == 0, j == 0, 1920, 1920, 1920 * 4, 1920, 1920 / 2);

                auto beginTime = std::chrono::steady_clock::now();
                auto iter_begin = beginTime;
                int frameCount = 0;
                for(;;) {
                    colorConv.feedInput(inputBuffer.data());
                    auto [mappedY, mappedU, mappedV] = colorConv.mapResult();
                    memcpy(outputYBuffer.data(), mappedY, 1920 * 1920);
                    memcpy(outputUBuffer.data(), mappedU, 1920 * 1920 / 4);
                    memcpy(outputVBuffer.data(), mappedV, 1920 * 1920 / 4);
                    colorConv.unmapResult();
                    ++frameCount;

                    if (std::chrono::steady_clock::now() - iter_begin > std::chrono::seconds(1)) {
                        std::cout << "Processed " << 
                            (frameCount * 1000.0) / 
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - beginTime
                                ).count() 
                            << " frames in 1 second" << std::endl;
                        iter_begin = std::chrono::steady_clock::now();
                    }

                    if (std::chrono::steady_clock::now() - beginTime > std::chrono::seconds(10)) {
                        break;
                    }
                }
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
