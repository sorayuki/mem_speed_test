#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 200

#include <CL/opencl.hpp>
#include <vector>
#include <string>
#include <list>
#include <tuple>
#include <optional>
#include <numeric>
#include <memory>
#include <chrono>
#include <future>
#include <windows.h>
#include <assert.h>

namespace {
    bool available_ = false;
    cl::Context ctx_;
    cl::CommandQueue hostToDeviceQueue_;
    cl::CommandQueue computeQueue_;
    cl::CommandQueue deviceToHostQueue_;
    cl::Program program_;
    size_t gmemAlign_ = 4;
    bool reuseBuffer_ = true;
    bool pipelineMode_ = false;
    bool supportSvm_ = false;
    
    // Memory Copy Mode
    void* no_memcpy(void* dst, const void* src, size_t size) {
        auto ps = (const char*)src;
        auto pd = (char*)dst;
        for(size_t i = 0; i < size; i += gmemAlign_) {
            pd[i] = ps[i];
        }
        return dst;
    }
    
    void* para_memcpy(void* dst, const void* src, size_t size) {
        std::future<void*> waitlist[3];
        for(int i = 0; i < 4; ++i) {
            int copyoffset = i * size / 4;
            int copysize = (i + 1) * size / 4 - copyoffset;
            if (i == 3) {
                memcpy((char*)dst + copyoffset, (const char*)src + copyoffset, copysize);
            } else {
                waitlist[i] = std::async(std::launch::async, memcpy, (char*)dst + copyoffset, (const char*)src + copyoffset, copysize);
            }
        }
        return dst;
    }

    void* (*mymemcpy)(void* dst, const void* src, size_t size) = memcpy;
    
    // Buffer Mode
    const int BM_USE_HOST = 0b00000001;
    const int BM_DEVICE   = 0b00000010;
    const int BM_HOST     = 0b00000100;
    const int BM_SVM      = 0b00001000;
    const int BM_COPY     = 0b00010000;
    const int BM_MAP      = 0b00100000;
    int bufferMode_ = BM_DEVICE | BM_COPY;

    size_t cl_mem_align() {
        return gmemAlign_;
        static std::optional<size_t> align;
        if (!align) {
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            align = std::lcm(gmemAlign_, si.dwPageSize);
        }
        
        return *align;
    }

    struct PinnedMemory {
        template<class T>
        struct MemBlock {
            cl::Buffer buffer_;
            size_t size_;
            T* hostPtr_;
        public:
            MemBlock(size_t size) {
                size_ = size;
                buffer_ = cl::Buffer( ctx_, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, size_);
                hostPtr_ = (T*) hostToDeviceQueue_.enqueueMapBuffer(buffer_, true, CL_MAP_WRITE, 0, size_);
            }

            ~MemBlock() {
                hostToDeviceQueue_.enqueueUnmapMemObject(buffer_, hostPtr_);
            }

            operator T*() const {
                return hostPtr_;
            }
        };
    };


    struct AlignedMemory {
        template<class T>
        struct MemBlock {
            size_t size_;
            T* hostPtr_;
        public:
            MemBlock(size_t size) {
                size_ = size;
                hostPtr_ = (T*)_aligned_malloc(size, cl_mem_align());
            }

            ~MemBlock() {
                _aligned_free(hostPtr_);
            }

            operator T*() const {
                return hostPtr_;
            }
        };
    };

    struct RegularMemory {
        template<class T>
        struct MemBlock {
            std::vector<T> data_;
            T* hostPtr_;
        public:
            MemBlock(size_t size) {
                data_.resize(size);
                hostPtr_ = data_.data();
            }

            ~MemBlock() {
            }

            operator T*() {
                return hostPtr_;
            }

            T& operator [](int index) {
                return hostPtr_[index];
            }
        };
    };
};


class CLEnv {
public:
    CLEnv() {
        std::vector<std::tuple<cl::Platform, cl::Device>> platdevlist;
        std::vector<bool> is_igpu;
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
                    platdevlist.emplace_back(std::make_tuple(p, d));

                    cl::size_type retval;
                    cl::size_type retsize;
                    clGetDeviceInfo(d(), CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl::size_type), &retval, &retsize);
                    is_igpu.emplace_back(retval);
                }
            }
        }

        for(int i = 0; i < platdevlist.size(); ++i) {
            fprintf(stderr, "%d: platform: %s (%s), device: %s %s\n", 
                i, 
                std::get<0>(platdevlist[i]).getInfo<CL_PLATFORM_NAME>().c_str(),
                std::get<0>(platdevlist[i]).getInfo<CL_PLATFORM_VERSION>().c_str(),
                std::get<1>(platdevlist[i]).getInfo<CL_DEVICE_NAME>().c_str(),
                is_igpu[i] ? "[MEM]" : "[GMEM]"
            );
        }
        int selected_index;
        fprintf(stderr, "select one: \n");
        fflush(stdout);
        scanf("%d", &selected_index);

        auto selected = platdevlist[selected_index];

        ctx_ = cl::Context(cl::vector<cl::Device>{ std::get<1>(selected) });
        hostToDeviceQueue_ = cl::CommandQueue{ ctx_, std::get<1>(selected), cl::QueueProperties::None };
        computeQueue_ = cl::CommandQueue{ ctx_, std::get<1>(selected), cl::QueueProperties::None };
        deviceToHostQueue_ = cl::CommandQueue{ ctx_, std::get<1>(selected), cl::QueueProperties::None };
        gmemAlign_ = std::get<1>(selected).getInfo<CL_DEVICE_MEM_BASE_ADDR_ALIGN>();
        auto svm_caps = std::get<1>(selected).getInfo<CL_DEVICE_SVM_CAPABILITIES>();
        supportSvm_ = !!svm_caps;

        std::string extensions = std::get<0>(selected).getInfo<CL_PLATFORM_EXTENSIONS>();
        for(auto& c: extensions) {
            if (c == ' ') c = '\n';
        }
        fprintf(stderr, "EXTENSIONS:\n%s\n", extensions.c_str());

        std::string sourcecode{ R"OPENCLCODE(
            kernel void bgra_to_i420_frame(
	            global const uchar* src, int stride,
	            global uchar* dstY, int strideY,
	            global uchar* dstU, int strideU,
	            global uchar* dstV, int strideV,
                const float4 yfactor, const float4 ufactor, const float4 vfactor,
                const uchar2 yrange, const uchar2 uvrange
            ) {
	            const int xd2 = get_global_id(0);
	            const int yd2 = get_global_id(1);
                const int x = xd2 * 2;
                const int y = yd2 * 2;
                
                for(int j = 0; j < 2; ++j) {
                    for(int i = 0; i < 2; ++i) {
                        float4 srcvec = convert_float4(vload4(x + i, src + (y + j) * stride));
                        srcvec.w = 1.0f;
                        dstY[(y + j) * strideY + (x + i)] = clamp(convert_uchar_sat(dot(srcvec, yfactor)), yrange.x, yrange.y);
                        if (i == 0 && j == 0) {
                            dstU[yd2 * strideU + xd2] = clamp(convert_uchar_sat(dot(srcvec, ufactor)), uvrange.x, uvrange.y);
                            dstV[yd2 * strideV + xd2] = clamp(convert_uchar_sat(dot(srcvec, vfactor)), uvrange.x, uvrange.y);
                        }
                    }
                }
            }
        )OPENCLCODE" };

        std::vector<std::string> programStrings;
        programStrings.push_back(sourcecode);

        program_ = cl::Program(ctx_, programStrings);
        try {
            program_.build("-cl-std=CL2.0");
        }
        catch (...) {
            cl_int buildErr = CL_SUCCESS;
            auto buildInfo = program_.getBuildInfo<CL_PROGRAM_BUILD_LOG>(&buildErr);
            for (auto& pair : buildInfo) {
                fprintf(stderr, "%s\n", pair.second.c_str());
            }

            return;
        }

        available_ = true;
    }

    ~CLEnv() {
    }
};

static CLEnv cl_env_;


static void assign(cl_float4& r, int x, int y, int z, int w) {
    r.x = x / 256.0f;
    r.y = y / 256.0f;
    r.z = z / 256.0f;
    r.w = w / 1.0f;
};


std::optional<std::tuple<cl_float4, cl_float4, cl_float4>> GetYUV2RGBFactor() {
    cl_float4 rfactor, gfactor, bfactor;
    assign(rfactor, 298, 0, 459, 0);
    assign(gfactor, 298, -55, -136, 0);
    assign(bfactor, 298, 541, 0, 0);
    return std::make_tuple(rfactor, gfactor, bfactor);
}


std::optional<std::tuple<cl_float4, cl_float4, cl_float4, cl_uchar2, cl_uchar2>> GetRGB2YUVFactor() {
    cl_float4 yfactor, ufactor, vfactor;
    cl_uchar2 yrange, uvrange;
    assign(yfactor, 47, 160, 16, 16);
    assign(ufactor, 110, -102, -10, 128);
    assign(vfactor, -25, -86, 112, 128);
    yrange.x = 16; yrange.y = 235;
    uvrange.x = 16; uvrange.y = 240;
    return std::make_tuple(yfactor, ufactor, vfactor, yrange, uvrange);
}

// 预创建缓冲区的转换器类
class BGRAToI420Converter {
private:
    size_t width_, height_;
    size_t srcStride_, yStride_, uStride_, vStride_;
    size_t srcSize_, ySize_, uSize_, vSize_;
    int bufferMode_;
    
    // 不同类型的缓冲区
    std::unique_ptr<cl::Buffer> srcBuffer_, yBuffer_, uBuffer_, vBuffer_;
    void* srcSvmPtr_ = nullptr;
    void* ySvmPtr_ = nullptr;
    void* uSvmPtr_ = nullptr;
    void* vSvmPtr_ = nullptr;
    
    cl_float4 yfactor_, ufactor_, vfactor_;
    cl_uchar2 yrange_, uvrange_;
    
    bool initBuffers() {
        bufferMode_ = ::bufferMode_;
        
        // 计算各缓冲区大小
        srcSize_ = srcStride_ * height_;
        ySize_ = yStride_ * height_;
        uSize_ = uStride_ * height_ / 2;
        vSize_ = vStride_ * height_ / 2;
        
        if (bufferMode_ & BM_SVM) {
            // SVM模式
            srcSvmPtr_ = clSVMAlloc(ctx_(), CL_MEM_READ_WRITE, srcSize_, 0);
            ySvmPtr_ = clSVMAlloc(ctx_(), CL_MEM_READ_WRITE, ySize_, 0);
            uSvmPtr_ = clSVMAlloc(ctx_(), CL_MEM_READ_WRITE, uSize_, 0);
            vSvmPtr_ = clSVMAlloc(ctx_(), CL_MEM_READ_WRITE, vSize_, 0);
            
            if (!srcSvmPtr_ || !ySvmPtr_ || !uSvmPtr_ || !vSvmPtr_) {
                cleanup();
                return false;
            }
        } else if (bufferMode_ & BM_USE_HOST) {
            // USE_HOST模式 - 缓冲区将在execute时创建
            return true;
        } else {
            // DEVICE或HOST模式
            size_t srcFlags = CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY;
            size_t dstFlags = CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY;
            
            if (bufferMode_ & BM_HOST) {
                srcFlags |= CL_MEM_ALLOC_HOST_PTR;
                dstFlags |= CL_MEM_ALLOC_HOST_PTR;
            }
            
            srcBuffer_ = std::make_unique<cl::Buffer>(ctx_, srcFlags, srcSize_);
            yBuffer_ = std::make_unique<cl::Buffer>(ctx_, dstFlags, ySize_);
            uBuffer_ = std::make_unique<cl::Buffer>(ctx_, dstFlags, uSize_);
            vBuffer_ = std::make_unique<cl::Buffer>(ctx_, dstFlags, vSize_);
        }
        
        return true;
    }
    
    void cleanup() {
        if (bufferMode_ & BM_SVM) {
            if (srcSvmPtr_) clSVMFree(ctx_(), srcSvmPtr_);
            if (ySvmPtr_) clSVMFree(ctx_(), ySvmPtr_);
            if (uSvmPtr_) clSVMFree(ctx_(), uSvmPtr_);
            if (vSvmPtr_) clSVMFree(ctx_(), vSvmPtr_);
            
            srcSvmPtr_ = ySvmPtr_ = uSvmPtr_ = vSvmPtr_ = nullptr;
        }

        srcBuffer_.reset();
        yBuffer_.reset();
        uBuffer_.reset();
        vBuffer_.reset();
    }
    
public:
    BGRAToI420Converter(size_t width, size_t height, 
                        size_t srcStride, size_t yStride, 
                        size_t uStride, size_t vStride)
        : width_(width), height_(height)
        , srcStride_(srcStride), yStride_(yStride)
        , uStride_(uStride), vStride_(vStride)
    {
        auto factors = GetRGB2YUVFactor();
        if (factors) {
            std::tie(yfactor_, ufactor_, vfactor_, yrange_, uvrange_) = *factors;
        }
        
        if (!initBuffers()) {
            throw std::runtime_error("Failed to initialize OpenCL buffers");
        }
    }
    
    ~BGRAToI420Converter() {
        cleanup();
    }
    
    bool execute(const void* src, void* dstY, void* dstU, void* dstV) {
        if (!available_) return false;
        
        cl::Event event;
        
        // 顺序调用三个分离的方法
        if (!executeInput(src, event)) return false;
        if (!executeCompute(event)) return false;
        if (!executeOutput(dstY, dstU, dstV, event)) return false;
        
        // 等待输出完成
        event.wait();
        
        return true;
    }
    
    bool executeInput(const void* src, cl::Event& event) {
        if (!available_) return false;

        if (!reuseBuffer_) {
            cleanup();
            initBuffers();
        }
        
        if (bufferMode_ & BM_SVM) {
            return executeInputSVM(src, event);
        } else if (bufferMode_ & BM_USE_HOST) {
            return executeInputUseHost(src, event);
        } else {
            return executeInputBuffer(src, event);
        }
    }
    
    bool executeCompute(cl::Event& ioEvent) {
        if (!available_) return false;
        
        if (bufferMode_ & BM_SVM) {
            return executeComputeSVM(ioEvent, ioEvent);
        } else if (bufferMode_ & BM_USE_HOST) {
            // USE_HOST模式下没有输出buffer是不能执行计算的
            // return executeComputeUseHost(ioEvent, ioEvent);
            ioEvent = cl::Event();
            return true;
        } else {
            return executeComputeBuffer(ioEvent, ioEvent);
        }
    }
    
    bool executeOutput(void* dstY, void* dstU, void* dstV, cl::Event& ioEvent) {
        if (!available_) return false;
        
        if (bufferMode_ & BM_SVM) {
            return executeOutputSVM(dstY, dstU, dstV, ioEvent, ioEvent);
        } else if (bufferMode_ & BM_USE_HOST) {
            return executeOutputUseHost(dstY, dstU, dstV, ioEvent, ioEvent);
        } else {
            return executeOutputBuffer(dstY, dstU, dstV, ioEvent, ioEvent);
        }
    }
    
private:
    // USE_HOST缓冲区存储（需要在execute过程中持续存在）
    std::unique_ptr<cl::Buffer> tempSrcBuf_, tempYBuf_, tempUBuf_, tempVBuf_;
    
    bool executeInputSVM(const void* src, cl::Event& event) {
        if (bufferMode_ & BM_COPY) {
            auto ret = clEnqueueSVMMemcpy(hostToDeviceQueue_(), false, srcSvmPtr_, src, srcSize_, 0, nullptr, &event());
            if (ret != CL_SUCCESS) return false;
        } else if (bufferMode_ & BM_MAP) {
            cl_int ret = clEnqueueSVMMap(hostToDeviceQueue_(), true, CL_MAP_WRITE_INVALIDATE_REGION, srcSvmPtr_, srcSize_, 0, nullptr, &event());
            if (ret != CL_SUCCESS) return false;
            (*mymemcpy)(srcSvmPtr_, src, srcSize_);
            ret = clEnqueueSVMUnmap(hostToDeviceQueue_(), srcSvmPtr_, 0, nullptr, nullptr);
            if (ret != CL_SUCCESS) return false;
        }
        
        return true;
    }
    
    bool executeInputUseHost(const void* src, cl::Event& event) {
        // 创建USE_HOST缓冲区
        tempSrcBuf_ = std::make_unique<cl::Buffer>(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY, srcSize_, (void*)src);
        
        // USE_HOST模式下输入不需要显式传输，直接标记event为完成
        event = cl::Event();
        return true;
    }
    
    bool executeInputBuffer(const void* src, cl::Event& event) {
        if (bufferMode_ & BM_COPY) {
            hostToDeviceQueue_.enqueueWriteBuffer(*srcBuffer_, false, 0, srcSize_, src, nullptr, &event);
        } else if (bufferMode_ & BM_MAP) {
            auto mapped = (void*)hostToDeviceQueue_.enqueueMapBuffer(*srcBuffer_, true, CL_MAP_WRITE_INVALIDATE_REGION, 0, srcSize_);
            (*mymemcpy)(mapped, src, srcSize_);
            hostToDeviceQueue_.enqueueUnmapMemObject(*srcBuffer_, mapped, nullptr, &event);
        }
        
        return true;
    }
    
    bool executeComputeSVM(const cl::Event& waitEvent, cl::Event& event) {
        auto func = cl::KernelFunctor<
            void*, cl_uint,
            void*, cl_uint, void*, cl_uint, void*, cl_uint,
            cl_float4, cl_float4, cl_float4,
            cl_uchar2, cl_uchar2>
            (program_, "bgra_to_i420_frame");
        
        std::vector<cl::Event> waitEvents;
        if (waitEvent() != nullptr) {
            waitEvents.push_back(waitEvent);
        }
        
        event = func(cl::EnqueueArgs(computeQueue_, waitEvents, cl::NDRange(width_ / 2, height_ / 2)),
            srcSvmPtr_, (cl_uint)srcStride_, 
            ySvmPtr_, (cl_uint)yStride_, 
            uSvmPtr_, (cl_uint)uStride_, 
            vSvmPtr_, (cl_uint)vStride_,
            yfactor_, ufactor_, vfactor_,
            yrange_, uvrange_);
        
        return true;
    }
    
    bool executeComputeUseHost(const cl::Event& waitEvent, cl::Event& event) {
        auto func = cl::KernelFunctor<
            cl::Buffer, cl_uint,
            cl::Buffer, cl_uint, cl::Buffer, cl_uint, cl::Buffer, cl_uint,
            cl_float4, cl_float4, cl_float4,
            cl_uchar2, cl_uchar2>
            (program_, "bgra_to_i420_frame");
        
        std::vector<cl::Event> waitEvents;
        if (waitEvent() != nullptr) {
            waitEvents.push_back(waitEvent);
        }
        
        event = func(cl::EnqueueArgs(computeQueue_, waitEvents, cl::NDRange(width_ / 2, height_ / 2)),
            *tempSrcBuf_, (cl_uint)srcStride_, 
            *tempYBuf_, (cl_uint)yStride_, 
            *tempUBuf_, (cl_uint)uStride_, 
            *tempVBuf_, (cl_uint)vStride_,
            yfactor_, ufactor_, vfactor_,
            yrange_, uvrange_);
        
        return true;
    }
    
    bool executeComputeBuffer(const cl::Event& waitEvent, cl::Event& event) {
        auto func = cl::KernelFunctor<
            cl::Buffer, cl_uint,
            cl::Buffer, cl_uint, cl::Buffer, cl_uint, cl::Buffer, cl_uint,
            cl_float4, cl_float4, cl_float4,
            cl_uchar2, cl_uchar2>
            (program_, "bgra_to_i420_frame");
        
        std::vector<cl::Event> waitEvents;
        if (waitEvent() != nullptr) {
            waitEvents.push_back(waitEvent);
        }
        
        event = func(cl::EnqueueArgs(computeQueue_, waitEvents, cl::NDRange(width_ / 2, height_ / 2)),
            *srcBuffer_, (cl_uint)srcStride_, 
            *yBuffer_, (cl_uint)yStride_, 
            *uBuffer_, (cl_uint)uStride_, 
            *vBuffer_, (cl_uint)vStride_,
            yfactor_, ufactor_, vfactor_,
            yrange_, uvrange_);
        
        return true;
    }
    
    bool executeOutputSVM(void* dstY, void* dstU, void* dstV, const cl::Event& waitEvent, cl::Event& event) {
        std::vector<cl::Event> waitEvents;
        if (waitEvent() != nullptr) {
            waitEvents.push_back(waitEvent);
        }
        
        if (bufferMode_ & BM_COPY) {
            std::vector<cl_event> waitList;
            for (auto& e : waitEvents) {
                waitList.push_back(e());
            }
            auto ret = clEnqueueSVMMemcpy(deviceToHostQueue_(), false, dstY, ySvmPtr_, ySize_, waitList.size(), waitList.data(), nullptr);
            if (ret != CL_SUCCESS) return false;
            ret = clEnqueueSVMMemcpy(deviceToHostQueue_(), false, dstU, uSvmPtr_, uSize_, 0, nullptr, nullptr);
            if (ret != CL_SUCCESS) return false;
            cl_event ev;
            ret = clEnqueueSVMMemcpy(deviceToHostQueue_(), false, dstV, vSvmPtr_, vSize_, 0, nullptr, &ev);
            event = cl::Event(ev);
            if (ret != CL_SUCCESS) return false;
        } else if (bufferMode_ & BM_MAP) {
            std::vector<cl_event> waitList;
            for (auto& e : waitEvents) {
                waitList.push_back(e());
            }
            cl_int ret = clEnqueueSVMMap(deviceToHostQueue_(), true, CL_MAP_READ, ySvmPtr_, ySize_, waitList.size(), waitList.data(), nullptr);
            if (ret != CL_SUCCESS) return false;
            (*mymemcpy)(dstY, ySvmPtr_, ySize_);
            ret = clEnqueueSVMUnmap(deviceToHostQueue_(), ySvmPtr_, 0, nullptr, nullptr);
            if (ret != CL_SUCCESS) return false;
            
            ret = clEnqueueSVMMap(deviceToHostQueue_(), true, CL_MAP_READ, uSvmPtr_, uSize_, 0, nullptr, nullptr);
            if (ret != CL_SUCCESS) return false;
            (*mymemcpy)(dstU, uSvmPtr_, uSize_);
            ret = clEnqueueSVMUnmap(deviceToHostQueue_(), uSvmPtr_, 0, nullptr, nullptr);
            if (ret != CL_SUCCESS) return false;
            
            ret = clEnqueueSVMMap(deviceToHostQueue_(), true, CL_MAP_READ, vSvmPtr_, vSize_, 0, nullptr, nullptr);
            if (ret != CL_SUCCESS) return false;
            (*mymemcpy)(dstV, vSvmPtr_, vSize_);
            cl_event ev;
            ret = clEnqueueSVMUnmap(deviceToHostQueue_(), vSvmPtr_, 0, nullptr, &ev);
            event = cl::Event(ev);
            if (ret != CL_SUCCESS) return false;
        }
        
        return true;
    }
    
    bool executeOutputUseHost(void* dstY, void* dstU, void* dstV, const cl::Event& waitEvent, cl::Event& event) {
        // 创建输出缓冲区
        tempYBuf_ = std::make_unique<cl::Buffer>(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY, ySize_, dstY);
        tempUBuf_ = std::make_unique<cl::Buffer>(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY, uSize_, dstU);
        tempVBuf_ = std::make_unique<cl::Buffer>(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY, vSize_, dstV);
        
        if (!executeComputeUseHost(waitEvent, event))
            return false;

        // USE_HOST模式下输出不需要显式传输，但需要确保kernel执行完成
        std::vector<cl::Event> waitEvents;
        if (event() != nullptr) {
            waitEvents.push_back(event);
        }
        
        void* hostptr = deviceToHostQueue_.enqueueMapBuffer(*tempYBuf_, false, CL_MAP_READ, 0, ySize_, &waitEvents, nullptr);
        deviceToHostQueue_.enqueueUnmapMemObject(*tempYBuf_, hostptr);
        hostptr = deviceToHostQueue_.enqueueMapBuffer(*tempUBuf_, false, CL_MAP_READ, 0, uSize_, &waitEvents, nullptr);
        deviceToHostQueue_.enqueueUnmapMemObject(*tempUBuf_, hostptr);
        cl::Event outEvent;
        hostptr = deviceToHostQueue_.enqueueMapBuffer(*tempVBuf_, false, CL_MAP_READ, 0, vSize_, &waitEvents, &outEvent);
        deviceToHostQueue_.enqueueUnmapMemObject(*tempVBuf_, hostptr);
        event = std::move(outEvent);
        return true;
    }
    
    bool executeOutputBuffer(void* dstY, void* dstU, void* dstV, const cl::Event& waitEvent, cl::Event& event) {
        std::vector<cl::Event> waitEvents;
        if (waitEvent() != nullptr) {
            waitEvents.push_back(waitEvent);
        }
        
        if (bufferMode_ & BM_COPY) {
            deviceToHostQueue_.enqueueReadBuffer(*yBuffer_, false, 0, ySize_, dstY, &waitEvents);
            deviceToHostQueue_.enqueueReadBuffer(*uBuffer_, false, 0, uSize_, dstU);
            deviceToHostQueue_.enqueueReadBuffer(*vBuffer_, false, 0, vSize_, dstV, nullptr, &event);
        } else if (bufferMode_ & BM_MAP) {
            auto yMapped = (void*)deviceToHostQueue_.enqueueMapBuffer(*yBuffer_, true, CL_MAP_READ, 0, ySize_, &waitEvents);
            (*mymemcpy)(dstY, yMapped, ySize_);
            deviceToHostQueue_.enqueueUnmapMemObject(*yBuffer_, yMapped);
            
            auto uMapped = (void*)deviceToHostQueue_.enqueueMapBuffer(*uBuffer_, true, CL_MAP_READ, 0, uSize_);
            (*mymemcpy)(dstU, uMapped, uSize_);
            deviceToHostQueue_.enqueueUnmapMemObject(*uBuffer_, uMapped);
            
            auto vMapped = (void*)deviceToHostQueue_.enqueueMapBuffer(*vBuffer_, true, CL_MAP_READ, 0, vSize_);
            (*mymemcpy)(dstV, vMapped, vSize_);
            deviceToHostQueue_.enqueueUnmapMemObject(*vBuffer_, vMapped, nullptr, &event);
        }
        
        return true;
    }
};


template<class MemT, class BufferT>
void test() {
    const int width = 1920;
    const int height = 1920;

    typename MemT::template MemBlock<char> src(width * height * 4);
    typename MemT::template MemBlock<char> y(width * height);
    typename MemT::template MemBlock<char> u(width * height / 4);
    typename MemT::template MemBlock<char> v(width * height / 4);
    memset(src, 255, width * height * 4);
    memset(y, 0, width * height);
    memset(u, 0, width * height / 4);
    memset(v, 0, width * height / 4);

    // 创建转换器对象
    BGRAToI420Converter converter[3] = {
        {width, height, width * 4, width, width / 2, width / 2},
        {width, height, width * 4, width, width / 2, width / 2},
        {width, height, width * 4, width, width / 2, width / 2}
    };

    std::optional<cl::Event> convertEvent[3];

    auto begin = std::chrono::steady_clock::now();
    auto iter_begin = begin;
    size_t frames = 0;
    size_t iter_frames = 0;
    double fps = 60.0;

    for(int ind = 0;; ++ind) {
        // pipeline mode or sync mode
        int x[] = { (ind + 2) % 3, (ind + 1) % 3, ind % 3 };
        if (pipelineMode_ == 0) for(auto& i: x) i = 0;

        auto diff = std::chrono::steady_clock::now() - begin;
        auto iter_diff = std::chrono::steady_clock::now() - iter_begin;
        if (iter_diff > std::chrono::seconds(1)) {
            fprintf(stderr, "fps = %g, avg = %g\n",
                iter_frames * 1000.0 / std::chrono::duration_cast<std::chrono::milliseconds>(iter_diff).count(),
                frames * 1000.0 / std::chrono::duration_cast<std::chrono::milliseconds>(diff).count()
            );
            iter_begin = std::chrono::steady_clock::now();
            iter_frames = 0;
        }

        if (diff > std::chrono::seconds(15))
            break;
        // auto targetTime = begin + frames * std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) / fps;
        // std::this_thread::sleep_until(targetTime);
        
        // 使用转换器对象执行转换
        if (convertEvent[x[0]]) {
            convertEvent[x[0]]->wait();

            if (frames == 1) {
	            for(int i = 0; i < width * height; ++i) if (y[i] != -21) { fprintf(stderr, "check failed.\n"); break; }
	            for(int i = 0; i < width * height / 4; ++i) if (u[i] != 126) { fprintf(stderr, "check failed.\n"); break; }
	            for(int i = 0; i < width * height / 4; ++i) if (v[i] != -128) { fprintf(stderr, "check failed.\n"); break; }
	        }

            iter_frames += 1;
            frames += 1;
        } else {
            convertEvent[x[0]] = cl::Event();
        }
        converter[x[0]].executeInput(src, *convertEvent[x[0]]);

        if (convertEvent[x[1]]) {
            converter[x[1]].executeCompute(*convertEvent[x[1]]);
        }

        if (convertEvent[x[2]]) {
            converter[x[2]].executeOutput(y, u, v, *convertEvent[x[2]]);
        }
    }

    cl::CommandQueue* queues[] = { &deviceToHostQueue_, &hostToDeviceQueue_, &computeQueue_ };
    for (auto& q : queues) {
        cl::Event ev;
        q->enqueueMarkerWithWaitList(nullptr, &ev);
        ev.wait();
    }
}

int main() {
    fprintf(stderr, "%s\n", "OpenCL Buffer? (0 = device buffer, 1 = svm buffer, 2 = host buffer, 3 = direct)");
    int bufferType;
    scanf("%d", &bufferType);

    if (bufferType == 0) {
        bufferMode_ = BM_DEVICE;
    } else if (bufferType == 1) {
        bufferMode_ = BM_SVM;
    } else if (bufferType == 2) {
        bufferMode_ = BM_HOST;
    } else if (bufferType == 3) {
        bufferMode_ = BM_USE_HOST;
    } else {
        throw std::runtime_error("Invalid buffer type");
    }

    fprintf(stderr, "%s\n", "Reuse OpenCL Buffer object? (0 = no, 1 = yes)");
    scanf("%d", &reuseBuffer_);

    fprintf(stderr, "%s\n", "Buffer copy mode? (0 = OpenCL Enqueue, 1 = Map)");
    int bufferCopyMode;
    scanf("%d", &bufferCopyMode);

    if (bufferCopyMode == 0) {
        bufferMode_ |= BM_COPY;
    } else if (bufferCopyMode == 1) {
        bufferMode_ |= BM_MAP;
    } else {
        throw std::runtime_error("Invalid buffer copy mode");
    }

    if (bufferCopyMode == 1) {
        fprintf(stderr, "%s\n", "memcpy implementation? (0 = memcpy, 1 = no copy, 2 = parallel copy)");
        int memcpyImpl;
        scanf("%d", &memcpyImpl);
        if (memcpyImpl == 0) {
            mymemcpy = &memcpy;
        } else if (memcpyImpl == 1) {
            mymemcpy = &no_memcpy;
        } else if (memcpyImpl == 2) {
            mymemcpy = &para_memcpy;
        } else {
            throw std::runtime_error("Invalid memcpy implementation");
        }
    }

    fprintf(stderr, "Host memory mode? (0 = regular, 1 = aligned, 2 = pinned)\n");
    int hostMemoryMode;
    scanf("%d", &hostMemoryMode);

    fprintf(stderr, "Use Pipeline? (0 = no, 1 = yes)\n");
    scanf("%d", &pipelineMode_);

    auto innerSwitch = [&](auto t) {
        using T = decltype(t);
        switch(hostMemoryMode) {
            case 0: test<RegularMemory, T>(); break;
            case 1: test<AlignedMemory, T>(); break;
            case 2: test<PinnedMemory, T>(); break;
            default: throw std::runtime_error("Invalid host memory mode");
        }
    };

    if (bufferMode_ & BM_SVM) {
        innerSwitch((void*)nullptr);
    } else {
        innerSwitch(cl::Buffer{});
    }

    return 0;
}