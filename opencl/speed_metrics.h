#pragma once

#include <chrono>
#include <cstdint>

class SpeedMetrics {
    using clock = std::chrono::high_resolution_clock;
    clock::duration totalCost_ = std::chrono::seconds(0);

    uint64_t totalBytes_ = 0;
public:
    template<class T>
    void RunCopy(T&& lambda) {
        auto start = clock::now();
        totalBytes_ += lambda();
        totalCost_ += clock::now() - start;
    }

    double GetSpeedInMBps() const {
        using namespace std::chrono;
        if (totalCost_.count() == 0) return 0.0;
        return (static_cast<double>(totalBytes_) / (1 << 20)) / (duration_cast<milliseconds>(totalCost_).count() / 1e3);
    }
};
