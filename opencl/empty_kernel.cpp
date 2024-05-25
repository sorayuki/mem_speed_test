#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 200

#include <CL/opencl.hpp>
#include <vector>
#include <string>
#include <utility>
#include <optional>

int main() {
    std::vector<std::pair<cl::Platform, cl::Device>> platdevlist;
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    for (auto& p : platforms) {
        std::string platver = p.getInfo<CL_PLATFORM_VERSION>();
        if (platver.find("OpenCL 2.") != std::string::npos ||
            platver.find("OpenCL 3.") != std::string::npos)
        {
            cl::vector<cl::Device> devices;
            p.getDevices(CL_DEVICE_TYPE_ALL, &devices);

            for (auto& d : devices) {
                platdevlist.emplace_back(std::make_pair(p, d));
            }
        }
    }

    for(int i = 0; i < platdevlist.size(); ++i) {
        fprintf(stderr, "%d: platform: %s, device: %s\n", 
            i, 
            platdevlist[i].first.getInfo<CL_PLATFORM_NAME>().c_str(), 
            platdevlist[i].second.getInfo<CL_DEVICE_NAME>().c_str()
        );
    }
    int selected_index;
    fprintf(stderr, "select one: \n");
    scanf("%d", &selected_index);

    auto selected = platdevlist[selected_index];

    auto ctx = cl::Context(cl::vector<cl::Device>{ selected.second });
    auto queue = cl::CommandQueue{ ctx, selected.second, cl::QueueProperties::None };

    std::string sourcecode{ "kernel void empty(global char* dst, global const char* src) { }" };
    auto program = cl::Program(ctx, {sourcecode});
    try {
        program.build("-cl-std=CL2.0");
    }
    catch (...) {
        for (auto& pair : program.getBuildInfo<CL_PROGRAM_BUILD_LOG>()) {
            fprintf(stderr, "%s", pair.second.c_str());
        }

        return 1;
    }

    auto func = cl::KernelFunctor<cl::Buffer, cl::Buffer>(program, "empty");

    std::vector<char> srcbuf(2560 * 1440 * 4);
    std::vector<char> dstbuf(2560 * 1440 * 1.5);
    cl::Buffer src(ctx, CL_MEM_READ_ONLY, (size_t)2560 * 1440 * 4);
    cl::Buffer dst(ctx, CL_MEM_WRITE_ONLY, (size_t)2560 * 1440 * 1.5);

    auto iter_count = 1;
    auto begin = std::chrono::steady_clock::now();
    size_t frames = 0;
    for(;;) {
        auto diff = std::chrono::steady_clock::now() - begin;
        if (diff > std::chrono::seconds(1)) {
            fprintf(stderr, "fps = %g\n", frames * 1000.0 / std::chrono::duration_cast<std::chrono::milliseconds>(diff).count());
            fflush(stdout);
            if (iter_count >= 60)
                break;
            begin = std::chrono::steady_clock::now();
            frames = 0;
            ++iter_count;
        }
        queue.enqueueWriteBuffer(src, CL_FALSE, 0, 2560 * 1440 * 4, srcbuf.data());
        func(cl::EnqueueArgs(queue, cl::NDRange(2560 / 2, 1440 / 2)), dst, src); // comment out this line and the 3D usage gone.
        queue.enqueueReadBuffer(dst, CL_TRUE, 0, 2560 * 1440 * 1.5, dstbuf.data());
        frames += 1;
    }
    return 0;
}
