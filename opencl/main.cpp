// https://www.intel.com/content/www/us/en/docs/opencl-sdk/developer-guide-core-xeon/2018/check-list-for-opencl-optimizations.html
// https://www.intel.com/content/dam/develop/external/us/en/documents/tutorial-svm-basic.pdf
// https://github.com/rsnemmen/OpenCL-examples/tree/master/add_numbers

// using CopyBuffer on D3D12 implementation will cause enqueueReadBuffer error.
// SVMAllocator has some problems
// https://github.com/KhronosGroup/OpenCL-CLHPP/issues/294

#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 200

#define WITH_ALLOC 1

#include <CL/opencl.hpp>
#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>
#include <numeric>
#include <chrono>
 
constexpr size_t numElements = 64 * 1048576; // 64 M
constexpr size_t numBytes = numElements * sizeof(int); // *4
constexpr size_t loopCount = 5;


template<class T>
class AlignAlloc: public std::allocator<T> {
public:
    template<class Other>
    struct rebind {
        using other = AlignAlloc<Other>;
    };

    int align_;

    AlignAlloc(int align) {
        align = 4096;
        align_ = align;
    }

    template<class RHST>
    AlignAlloc(const AlignAlloc<RHST>& rhs) {
        align_ = rhs.align_;
    }

    T* allocate(size_t count) {
        return (T*)_aligned_malloc(sizeof(T) * count, align_);
    }

    void deallocate(void* ptr, size_t count) {
        _aligned_free(ptr);
    }
};


struct ProfileItem {
    cl::Event copyInBegin;
    cl::Event copyInEnd;
    cl::Event exec;
    cl::Event copyOutBegin;
    cl::Event copyOutEnd;
};


struct ProfileCtx {
    static std::chrono::steady_clock::duration getDuration(cl::Event& lhs) {
        try {
            auto ns = 
                lhs.getProfilingInfo<CL_PROFILING_COMMAND_END>()
                - lhs.getProfilingInfo<CL_PROFILING_COMMAND_START>();
            return std::chrono::nanoseconds(ns);
        } catch(...) {
            return std::chrono::nanoseconds(0);
        }
    }

    static std::chrono::steady_clock::duration getDuration(cl::Event& lhs, cl::Event& rhs) {
        try {
            return (lhs() ? getDuration(lhs) : std::chrono::nanoseconds(0))
                + (rhs() ? getDuration(rhs) : std::chrono::nanoseconds(0));
            // if (lhs() && rhs()) {
            //     auto ns = 
            //         lhs.getProfilingInfo<CL_PROFILING_COMMAND_END>()
            //         - rhs.getProfilingInfo<CL_PROFILING_COMMAND_START>();
            //     return std::chrono::nanoseconds(ns);
            // } else if (lhs()) {
            //     auto ns = getDuration(lhs);
            //     return std::chrono::nanoseconds(ns);
            // } else {
            //     return std::chrono::nanoseconds(0);
            // }
        } catch(...) {
            return std::chrono::nanoseconds(0);
        }
    }

    static int64_t getDurationNs(std::chrono::steady_clock::duration dur) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(dur).count();
    }
    
    std::chrono::steady_clock::time_point
        all_begin = std::chrono::steady_clock::now();

    std::vector<ProfileItem> items;

    void Add(ProfileItem& item) {
        items.push_back(item);
    }

    void printInfo() {
        std::chrono::steady_clock::duration copyin_cost{}, exec_cost{}, copyout_cost{};

        std::sort(items.begin(), items.end(), [](auto lhs, auto rhs) {
            auto totallhs = getDuration(lhs.copyInEnd, lhs.copyInBegin) + getDuration(lhs.exec) + getDuration(lhs.copyOutEnd, lhs.copyOutBegin);
            auto totalrhs = getDuration(rhs.copyInEnd, rhs.copyInBegin) + getDuration(rhs.exec) + getDuration(rhs.copyOutEnd, rhs.copyOutBegin);
            return totallhs < totalrhs;
        });

        // drop highest and lowest
        for(auto i = 1; i < items.size() - 1; ++i) {
            auto& item = items[i];
            copyin_cost += getDuration(item.copyInEnd, item.copyInBegin);
            exec_cost += getDuration(item.exec);
            copyout_cost += getDuration(item.copyOutEnd, item.copyOutBegin);
        }

        int64_t copyinbytes = (int64_t)numBytes;
        int64_t copyoutbytes = (int64_t)numBytes;
        auto copyinCostNs = getDurationNs(copyin_cost / (items.size() - 2));
        auto copyOutCostNs = getDurationNs(copyout_cost / (items.size() - 2));
        auto execCostNs = getDurationNs(exec_cost / (items.size() - 2));
        auto copyinMBps = 1.0 * copyinbytes / 1024 / 1024 / (copyinCostNs / 1000.0 / 1000.0 / 1000.0);
        auto copyoutMBps = 1.0 * copyoutbytes / 1024 / 1024 / (copyOutCostNs / 1000.0 / 1000.0 / 1000.0);
        auto execMBps = 1.0 * copyinbytes / 1024 / 1024 / (execCostNs / 1000.0 / 1000.0 / 1000.0);

        std::cout
            << "\ttotal cost: " << getDurationNs((std::chrono::steady_clock::now() - all_begin) / loopCount) / 1000.0 / 1000.0 << "ms" << std::endl
            << "\tcopyin cost: " << copyinCostNs / 1000.0 / 1000.0 << "ms (" << copyinMBps << " MB/S)" << std::endl
            << "\texec cost: " << execCostNs / 1000.0 / 1000.0 << "ms (" << execMBps << " MB/S)" << std::endl
            << "\tcopyout cost: " << copyOutCostNs / 1000.0 / 1000.0 << "ms (" << copyoutMBps << " MB/S)"  << std::endl;
    }
};


struct TestCtx {
    cl::Device device;
    int memalign;
};


void DoTest(TestCtx ctx) {
    std::cout << "align= " << ctx.memalign << std::endl;

    cl::vector<cl::Device> devices { ctx.device };
    cl::Context clctx(devices);

    // C++11 raw string literal for the first kernel
    // Raw string literal for the second kernel
    std::string kernel{R"CLC(
        kernel void CopyBuffer(global const int* restrict input, global int* restrict output)
        {
            output[get_global_id(0)] = input[get_global_id(0)] + 1.0;
        }
    )CLC"};
 
    std::vector<std::string> programStrings;
    programStrings.push_back(kernel);
 
    cl::Program copyBufferProgram(clctx, programStrings);
    try {
        copyBufferProgram.build("-cl-std=CL2.0");
        //copyBufferProgram.build();
    }
    catch (...) {
        // Print build info for all devices
        cl_int buildErr = CL_SUCCESS;
        auto buildInfo = copyBufferProgram.getBuildInfo<CL_PROGRAM_BUILD_LOG>(&buildErr);
        for (auto &pair : buildInfo) {
            std::cerr << pair.second << std::endl << std::endl;
        }
 
        return;
    }

    // https://www.intel.com/content/www/us/en/docs/opencl-sdk/developer-guide-core-xeon/2018/profiling-operations-using-opencl-profiling-events.html
    cl::CommandQueue queue{ clctx, cl::QueueProperties::Profiling };

    AlignAlloc<int> alloc(ctx.memalign);
    std::vector<int, AlignAlloc<int>> input(numElements, 1, alloc);
    std::vector<int, AlignAlloc<int>> output(numElements, alloc);

    auto checkoutput = [&]() {
        return;
        for(auto& x: output)
            if (x != 2)
                throw std::runtime_error("check output failed.");
    };

    auto outputResult = [](auto begintime) {
        auto iobytes = (int64_t)numBytes * loopCount;

    };

    {
        std::cout << "using gpu buffer" << std::endl;
        ProfileCtx pc;

        auto copyBuffer =
            cl::KernelFunctor<cl::Buffer, cl::Buffer>(copyBufferProgram, "CopyBuffer");

#if WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
            cl::Buffer inputbuf{ clctx, CL_MEM_READ_ONLY, numBytes };
            cl::Buffer outputbuf{ clctx, CL_MEM_WRITE_ONLY, numBytes };

#if !WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
            ProfileItem pi;
            // queue.enqueueCopyBuffer(inputbuf, outputbuf, 0, 0, numBytes, nullptr, &pi.bufferCopy);
            queue.enqueueWriteBuffer(inputbuf, true, 0, numBytes, input.data(), nullptr, &pi.copyInEnd);

            cl_int error;
            pi.exec = copyBuffer(cl::EnqueueArgs(queue, numElements),
                inputbuf,
                outputbuf,
                error);

            queue.enqueueReadBuffer(outputbuf, true, 0, numBytes, output.data(), nullptr, &pi.copyOutEnd);

            if (i == 0) {
                checkoutput();
            }

            pc.Add(pi);
        }

        pc.printInfo();

        memset(output.data(), 0, numBytes);
    }

    {
        std::cout << "using host buffer" << std::endl;
        ProfileCtx pc;

        auto copyBuffer =
            cl::KernelFunctor<cl::Buffer, cl::Buffer>(copyBufferProgram, "CopyBuffer");

#if WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
            cl::Buffer inputbuf{ clctx, CL_MEM_ALLOC_HOST_PTR | CL_MEM_READ_ONLY, numBytes };
            cl::Buffer outputbuf{ clctx, CL_MEM_ALLOC_HOST_PTR | CL_MEM_READ_WRITE, numBytes };
#if !WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
            ProfileItem pi;
            queue.enqueueWriteBuffer(inputbuf, true, 0, numBytes, input.data(), nullptr, &pi.copyInEnd);

            cl_int error;
            pi.exec = copyBuffer(cl::EnqueueArgs(queue, numElements),
                inputbuf,
                outputbuf,
                error);

            queue.enqueueReadBuffer(outputbuf, true, 0, numBytes, output.data(), nullptr, &pi.copyOutEnd);

            if (i == 0) {
                checkoutput();
            }

            pc.Add(pi);
        }

        pc.printInfo();

        memset(output.data(), 0, numBytes);
    }

    {
        std::cout << "map host alloc buffer" << std::endl;
        ProfileCtx pc;

        auto copyBuffer =
            cl::KernelFunctor<cl::Buffer, cl::Buffer>(copyBufferProgram, "CopyBuffer");

#if WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
        cl::Buffer inputbuf{ clctx, CL_MEM_ALLOC_HOST_PTR | CL_MEM_READ_ONLY, numBytes };
        cl::Buffer outputbuf{ clctx, CL_MEM_ALLOC_HOST_PTR | CL_MEM_WRITE_ONLY, numBytes };
#if !WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
            ProfileItem pi;
            auto pa = queue.enqueueMapBuffer(inputbuf, true, CL_MAP_WRITE_INVALIDATE_REGION, 0, numBytes, nullptr, &pi.copyInBegin);
            memcpy(pa, input.data(), numBytes);
            queue.enqueueUnmapMemObject(inputbuf, pa, nullptr, &pi.copyInEnd);

            cl_int error;
            pi.exec = copyBuffer(cl::EnqueueArgs(queue, numElements),
                inputbuf,
                outputbuf,
                error);

            auto po = queue.enqueueMapBuffer(outputbuf, true, CL_MAP_READ, 0, numBytes, nullptr, &pi.copyOutBegin);
            memcpy(output.data(), po, numBytes);
            queue.enqueueUnmapMemObject(outputbuf, po, nullptr, &pi.copyOutEnd);

            if (i == 0) {
                checkoutput();
            }
            pc.Add(pi);
        }

        pc.printInfo();

        memset(output.data(), 0, numBytes);
    }

    {
        std::cout << "map host buffer" << std::endl;
        ProfileCtx pc;

        auto copyBuffer =
            cl::KernelFunctor<cl::Buffer, cl::Buffer>(copyBufferProgram, "CopyBuffer");

        for(int i = 0; i < loopCount; ++i) {
            ProfileItem pi;
            cl::Buffer inputbuf{ clctx, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, numBytes, input.data() };
            cl::Buffer outputbuf{ clctx, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, numBytes, output.data() };

            cl_int error;
            pi.exec = copyBuffer(cl::EnqueueArgs(queue, numElements),
                inputbuf,
                outputbuf,
                error);
            auto po = queue.enqueueMapBuffer(outputbuf, true, CL_MAP_READ, 0, numBytes, nullptr, &pi.copyOutEnd);
            queue.enqueueUnmapMemObject(outputbuf, po);

            if (i == 0) {
                checkoutput();
            }

            pc.Add(pi);
        }

        pc.printInfo();

        memset(output.data(), 0, numBytes);
    }

    {
        std::cout << "map gpu buffer" << std::endl;
        ProfileCtx pc;

        auto copyBuffer =
            cl::KernelFunctor<cl::Buffer, cl::Buffer>(copyBufferProgram, "CopyBuffer");

#if WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
        cl::Buffer inputbuf{ clctx, CL_MEM_READ_ONLY, numBytes };
        cl::Buffer outputbuf{ clctx, CL_MEM_WRITE_ONLY, numBytes };
#if !WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
            ProfileItem pi;
            auto pa = queue.enqueueMapBuffer(inputbuf, true, CL_MAP_WRITE_INVALIDATE_REGION, 0, numBytes, nullptr, &pi.copyInBegin);
            memcpy(pa, input.data(), numBytes);
            queue.enqueueUnmapMemObject(inputbuf, pa, nullptr, &pi.copyInEnd);

            cl_int error;
            pi.exec = copyBuffer(cl::EnqueueArgs(queue, numElements),
                inputbuf,
                outputbuf,
                error);

            auto po = queue.enqueueMapBuffer(outputbuf, true, CL_MAP_READ, 0, numBytes, nullptr, &pi.copyOutBegin);
            memcpy(output.data(), po, numBytes);
            queue.enqueueUnmapMemObject(outputbuf, po, nullptr, &pi.copyOutEnd);

            if (i == 0) {
                checkoutput();
            }
            pc.Add(pi);
        }

        pc.printInfo();

        memset(output.data(), 0, numBytes);
    }

    {
        std::cout << "using host buffer and copy to gpu" << std::endl;
        ProfileCtx pc;

        auto copyBuffer =
            cl::KernelFunctor<cl::Buffer, cl::Buffer>(copyBufferProgram, "CopyBuffer");

        for(int i = 0; i < loopCount; ++i) {
            ProfileItem pi;
            cl::Buffer preinputbuf{ clctx, CL_MEM_USE_HOST_PTR, numBytes, input.data() };
            cl::Buffer inputbuf{ clctx, CL_MEM_READ_ONLY, numBytes };
            cl::Buffer outputbuf{ clctx, CL_MEM_WRITE_ONLY, numBytes };
            cl::Buffer postoutputbuf{ clctx, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, numBytes, output.data() };

            queue.enqueueCopyBuffer(preinputbuf, inputbuf, 0, 0, numBytes, nullptr, &pi.copyInEnd);

            cl_int error;
            pi.exec = copyBuffer(cl::EnqueueArgs(queue, numElements),
                inputbuf,
                outputbuf,
                error);
            
            queue.enqueueCopyBuffer(outputbuf, postoutputbuf, 0, 0, numBytes, nullptr, &pi.copyOutBegin);
            auto po = queue.enqueueMapBuffer(postoutputbuf, true, CL_MAP_READ, 0, numBytes, nullptr, &pi.copyOutEnd);
            queue.enqueueUnmapMemObject(postoutputbuf, po);

            if (i == 0) {
                checkoutput();
            }

            pc.Add(pi);
        }

        pc.printInfo();

        memset(output.data(), 0, numBytes);
    }

    {
        std::cout << "using host alloc buffer and copy to gpu" << std::endl;
        ProfileCtx pc;

        auto copyBuffer =
            cl::KernelFunctor<cl::Buffer, cl::Buffer>(copyBufferProgram, "CopyBuffer");

#if WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
        cl::Buffer preinputbuf{ clctx, CL_MEM_ALLOC_HOST_PTR, numBytes };
        cl::Buffer inputbuf{ clctx, CL_MEM_READ_ONLY, numBytes };
        cl::Buffer outputbuf{ clctx, CL_MEM_WRITE_ONLY, numBytes };
        cl::Buffer postoutputbuf{ clctx, CL_MEM_ALLOC_HOST_PTR, numBytes };
#if !WITH_ALLOC
        for(int i = 0; i < loopCount; ++i) {
#endif
            ProfileItem pi;
            auto pa = queue.enqueueMapBuffer(preinputbuf, true, CL_MAP_WRITE_INVALIDATE_REGION, 0, numBytes);
            memcpy(pa, input.data(), numBytes);
            queue.enqueueUnmapMemObject(preinputbuf, pa, nullptr);

            queue.enqueueCopyBuffer(preinputbuf, inputbuf, 0, 0, numBytes, nullptr, &pi.copyInEnd);

            cl_int error;
            pi.exec = copyBuffer(cl::EnqueueArgs(queue, numElements),
                inputbuf,
                outputbuf,
                error);

            queue.enqueueCopyBuffer(outputbuf, postoutputbuf, 0, 0, numBytes, nullptr, &pi.copyOutEnd);

            auto po = queue.enqueueMapBuffer(postoutputbuf, true, CL_MAP_READ, 0, numBytes);
            memcpy(output.data(), po, numBytes);
            queue.enqueueUnmapMemObject(postoutputbuf, po);

            if (i == 0) {
                checkoutput();
            }
            pc.Add(pi);
        }

        pc.printInfo();

        memset(output.data(), 0, numBytes);
    }

    // or it may crash on D3D12 implementation
    queue.finish();
}

int main(void)
{
    int addralign = 0;

    // Filter for a 2.0 or newer platform and set it as the default
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);
    cl::Platform plat;
    for (auto &p : platforms) {
        std::cout << "name: " << p.getInfo<CL_PLATFORM_NAME>() << std::endl;
        std::cout << "vender: " << p.getInfo<CL_PLATFORM_VENDOR>() << std::endl;
        std::cout << "version: " << p.getInfo<CL_PLATFORM_VERSION>() << std::endl;

        std::string platver = p.getInfo<CL_PLATFORM_VERSION>();
        if (platver.find("OpenCL 2.") == std::string::npos &&
            platver.find("OpenCL 3.") == std::string::npos) 
        {
            std::cout << "\tNo opencl 2.x or 3.x, skip." << std::endl;
            continue;
        }

        if (p.getInfo<CL_PLATFORM_VENDOR>().find("Microsoft") != std::string::npos)
        {
            std::cout << "\tskip." << std::endl;
            continue;
        }

        cl::vector<cl::Device> devices;
        p.getDevices(CL_DEVICE_TYPE_ALL, &devices);
        for(auto& d: devices) {
            int align = 4096; // align for main memory
            bool testsvm = false;

            auto devname = d.getInfo<CL_DEVICE_NAME>();
            std::cout << "device name: " << devname << std::endl;

            auto svmcaps = d.getInfo<CL_DEVICE_SVM_CAPABILITIES>();
            std::cout << "svm caps: " << svmcaps << std::endl;
            if (svmcaps)
                testsvm = true;

            auto devtype = d.getInfo<CL_DEVICE_TYPE>();
            std::cout << "device type: " << 
            [&]() {
                switch(devtype) {
                    case CL_DEVICE_TYPE_DEFAULT: return "Default";
                    case CL_DEVICE_TYPE_CPU: return "CPU";
                    case CL_DEVICE_TYPE_GPU: return "GPU";
                    case CL_DEVICE_TYPE_ACCELERATOR: return "Accelerator";
                    case CL_DEVICE_TYPE_CUSTOM: return "Custom";
                    default: return "Unknown";
                }
            }()
            << std::endl;

            auto new_align = d.getInfo<CL_DEVICE_MEM_BASE_ADDR_ALIGN>();
            std::cout << "memory base align: " << new_align << std::endl;

            align = std::lcm(new_align, align);

            if (devtype == CL_DEVICE_TYPE_CPU)
                continue;
            //DoTest( { d, align } );
            DoTest( { d, 1 } );
        }

        std::cout << std::endl;
    }
 
    return 0;
}
