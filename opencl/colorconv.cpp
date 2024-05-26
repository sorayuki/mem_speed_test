#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 200

#include <CL/opencl.hpp>
#include <vector>
#include <string>
#include <list>
#include <tuple>
#include <optional>
#include <numeric>
#include <windows.h>


namespace {
    bool available_ = false;
    cl::Context ctx_;
    cl::CommandQueue queue_;
    cl::Program program_;
    size_t gmemAlign_ = 4;
    bool reuseBuffer_ = true;
    
    const int BM_USE_HOST = 0b00000001;
    const int BM_DEVICE   = 0b00000010;
    const int BM_HOST     = 0b00000100;
    const int BM_COPY     = 0b00001000;
    const int BM_MAP      = 0b00010000;
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
                hostPtr_ = (T*) queue_.enqueueMapBuffer(buffer_, true, CL_MAP_WRITE, 0, size_);
            }

            ~MemBlock() {
                queue_.enqueueUnmapMemObject(buffer_, hostPtr_);
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

    class BufferPool {
        struct Item {
            cl::Buffer buffer;
            size_t flags;
            size_t size;
        };

        std::list<Item> pool;
    public:
        cl::Buffer retain(size_t flags, size_t size) {
            for(auto it = pool.begin(); it != pool.end(); ++it) {
                if (it->flags == flags && it->size == size) {
                    cl::Buffer retval = it->buffer;
                    pool.erase(it);
                    return retval;
                }
            }

            return cl::Buffer(ctx_, flags, size);
        }

        void release(cl::Buffer& buffer, size_t flags, size_t size) {
            if (!reuseBuffer_) return;
            pool.push_front({ buffer, flags, size });
            while (pool.size() > 8) {
                pool.pop_back();
            }
        }
    } pool_;

    class CCClBuffer {
        size_t size_;
        cl::Buffer buffer_;
        size_t flags_;
        void* hostPtr_;

        int localBufferMode_;
        bool isWrite_ = false;

        void Init(void* ptr, size_t size, bool isWrite) {
            size_ = size;
            isWrite_ = isWrite;
            hostPtr_ = ptr;
            localBufferMode_ = bufferMode_;

            auto addr = (intptr_t)ptr;
            flags_ = isWrite_ ? (CL_MEM_WRITE_ONLY | CL_MEM_HOST_READ_ONLY) : (CL_MEM_READ_ONLY | CL_MEM_HOST_WRITE_ONLY);
            
            if (localBufferMode_ & BM_USE_HOST) {
                buffer_ = cl::Buffer{ ctx_, CL_MEM_USE_HOST_PTR | flags_, size_, ptr };
            }
            else {
                if (localBufferMode_ & BM_DEVICE) {
                } else if (localBufferMode_ == BM_HOST) {
                    flags_ |= CL_MEM_ALLOC_HOST_PTR;
                }

                buffer_ = pool_.retain(flags_, size_);

                if (isWrite_ == false) {
                    if (localBufferMode_ & BM_COPY)
                        queue_.enqueueWriteBuffer(buffer_, false, 0, size_, ptr);
                    else if (localBufferMode_ & BM_MAP) {
                        auto mapped = (void*)queue_.enqueueMapBuffer(buffer_, true, CL_MAP_WRITE, 0, size_);
                        memcpy(mapped, hostPtr_, size_);
                        queue_.enqueueUnmapMemObject(buffer_, mapped);
                    }
                }
            }
        }
    public:
        CCClBuffer(void* ptr, size_t size, bool isWrite) {
            Init(ptr, size, isWrite);
        }

        CCClBuffer(const void* ptr, size_t size) {
            Init((void*)ptr, size, false);
        }

        ~CCClBuffer() {
            if (isWrite_) {
                if (localBufferMode_ & BM_USE_HOST) {
                    // 同步数据刷一下
                    void* hostptr = queue_.enqueueMapBuffer(buffer_, true, CL_MAP_READ, 0, size_);
                    queue_.enqueueUnmapMemObject(buffer_, hostptr);
                }
                else {
                    if (localBufferMode_ & BM_COPY)
                        queue_.enqueueReadBuffer(buffer_, true, 0, size_, hostPtr_);
                    else if (localBufferMode_ & BM_MAP) {
                        auto mapped = (void*)queue_.enqueueMapBuffer(buffer_, true, CL_MAP_READ, 0, size_);
                        memcpy(hostPtr_, mapped, size_);
                        queue_.enqueueUnmapMemObject(buffer_, mapped);
                    }

                    pool_.release(buffer_, flags_, size_);
                }
            }
        }

        operator cl::Buffer& () {
            return buffer_;
        }
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
                    auto iGPU = false;
                    iGPU = iGPU || p.getInfo<CL_PLATFORM_NAME>().find("Intel") != std::string::npos;
                    iGPU = iGPU || (p.getInfo<CL_PLATFORM_NAME>().find("AMD") != std::string::npos && d.getInfo<CL_DEVICE_NAME>().find("gfx") != std::string::npos);

                    cl::size_type retval;
                    cl::size_type retsize;
                    clGetDeviceInfo(d(), CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(cl::size_type), &retval, &retsize);
                    is_igpu.emplace_back(retval);
                }
            }
        }

        for(int i = 0; i < platdevlist.size(); ++i) {
            fprintf(stderr, "%d: platform: %s, device: %s %s\n", 
                i, 
                std::get<0>(platdevlist[i]).getInfo<CL_PLATFORM_NAME>().c_str(),
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
        queue_ = cl::CommandQueue{ ctx_, std::get<1>(selected), cl::QueueProperties::None };
        gmemAlign_ = std::get<1>(selected).getInfo<CL_DEVICE_MEM_BASE_ADDR_ALIGN>();

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
                
                __attribute__((opencl_unroll_hint))
                for(int j = 0; j < 2; ++j) {
                    __attribute__((opencl_unroll_hint))
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


bool opencl_bgra_to_i420_frame(
    size_t width, size_t height,
    const void* src, size_t stride,
    void* dstY, size_t strideY,
    void* dstU, size_t strideU,
    void* dstV, size_t strideV
) {
    if (!available_)
        return false;

    auto func = cl::KernelFunctor<
        cl::Buffer, cl_uint,
        cl::Buffer, cl_uint, cl::Buffer, cl_uint, cl::Buffer, cl_uint,
        cl_float4, cl_float4, cl_float4,
        cl_uchar2, cl_uchar2>
        (program_, "bgra_to_i420_frame");

    auto factors = GetRGB2YUVFactor();
    if (!factors)
        return false;

    auto [yfactor, ufactor, vfactor, yrange, uvrange] = *factors;

    CCClBuffer srcbuf{ src, stride * height },
        ybuf{ dstY, height * strideY, true },
        ubuf{ dstU, height / 2 * strideU, true },
        vbuf{ dstV, height / 2 * strideV, true };

    func(cl::EnqueueArgs(queue_, cl::NDRange(width / 2, height / 2)),
        srcbuf, (cl_uint)stride, ybuf, (cl_uint)strideY, ubuf, (cl_uint)strideU, vbuf, (cl_uint)strideV,
        yfactor, ufactor, vfactor,
        yrange, uvrange
    );

    return true;
}


bool opencl_nv12_to_bgra_frame(
    size_t width, size_t height,
    void* dst, size_t stride,
    const void* srcY, size_t strideY,
    const void* srcUV, size_t strideUV
) {
    if (!available_)
        return false;

    auto factors = GetYUV2RGBFactor();
    if (!factors)
        return false;

    auto [rfactor, gfactor, bfactor] = *factors;

    auto func = cl::KernelFunctor<
        cl::Buffer, cl_uint,
        cl::Buffer, cl_uint, cl::Buffer, cl_uint,
        cl_float4, cl_float4, cl_float4>
        (program_, "nv12_to_bgra_frame");

    CCClBuffer dstbuf{ dst, stride * height, true },
        ybuf{ srcY, height * strideY },
        uvbuf{ srcUV, height / 2 * strideUV };

    func(cl::EnqueueArgs(queue_, cl::NDRange(width / 2, height / 2)),
        dstbuf, (cl_uint)stride, ybuf, (cl_uint)strideY, uvbuf, (cl_uint)strideUV,
        rfactor, gfactor, bfactor
    );

    return true;
}


bool opencl_i420_to_bgra_frame(
    const void* y, size_t strideY,
    const void* u, size_t strideU,
    const void* v, size_t strideV,
    size_t width, size_t height,
    size_t dst_stride, void* dst
) {
    if (!available_)
        return false;

    auto factors = GetYUV2RGBFactor();
    if (!factors)
        return false;

    auto [rfactor, gfactor, bfactor] = *factors;

    auto func = cl::KernelFunctor<
        cl::Buffer, cl_uint,
        cl::Buffer, cl_uint, cl::Buffer, cl_uint, cl::Buffer, cl_uint,
        cl_float4, cl_float4, cl_float4>
        (program_, "i420_to_bgra_frame");

    CCClBuffer dstbuf{ dst, dst_stride * height, true },
        ybuf{ y, strideY * height },
        ubuf{ u, strideU * height / 2 },
        vbuf{ v, strideV * height / 2 };

    func(cl::EnqueueArgs(queue_, cl::NDRange(width / 2, height / 2)),
        dstbuf, (cl_uint)dst_stride,
        ybuf, (cl_uint)strideY, ubuf, (cl_uint)strideU, vbuf, (cl_uint)strideV,
        rfactor, gfactor, bfactor
    );

    return true;
}

template<class MemT>
void test() {
    const int width = 1920;
    const int height = 1920;

    auto src = typename MemT::MemBlock<char>(width * height * 4);
    auto y = typename MemT::MemBlock<char>(width * height);
    auto u = typename MemT::MemBlock<char>(width * height / 4);
    auto v = typename MemT::MemBlock<char>(width * height / 4);
    memset(src, 255, width * height * 4);
    memset(y, 0, width * height);
    memset(u, 0, width * height / 4);
    memset(v, 0, width * height / 4);

    auto begin = std::chrono::steady_clock::now();
    auto iter_begin = begin;
    size_t frames = 0;
    size_t iter_frames = 0;
    double fps = 60.0;

    for(;;) {
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
        opencl_bgra_to_i420_frame(width, height, 
            src, width * 4,
            y, width,
            u, width / 2,
            v, width / 2
        );
        iter_frames += 1;
        frames += 1;

        if (frames == 1) {
            for(int i = 0; i < width * height; ++i) if (y[i] != -21) { fprintf(stderr, "check failed.\n"); break; }
            for(int i = 0; i < width * height / 4; ++i) if (u[i] != 126) { fprintf(stderr, "check failed.\n"); break; }
            for(int i = 0; i < width * height / 4; ++i) if (v[i] != -128) { fprintf(stderr, "check failed.\n"); break; }
        }
    }
}

int main() {
    fprintf(stderr, "%s", "Reuse opencl buffer?\n");
    scanf("%d", &reuseBuffer_);

    struct {
        const char* msg;
        int flags;
        int allocType;
    } items[] = {
        { "Regular memory, device buffer, copy", BM_DEVICE | BM_COPY, 0 },
        { "Regular memory, device buffer, map", BM_DEVICE | BM_MAP, 0 },
        { "Regular memory, host buffer, copy", BM_HOST | BM_MAP, 0 },
        { "Regular memory, host buffer, map", BM_HOST | BM_MAP, 0 },
        { "Regular memory, direct", BM_USE_HOST, 0 },
        { "Aligned memory, device buffer, copy", BM_DEVICE | BM_COPY, 1 },
        { "Aligned memory, direct", BM_USE_HOST, 1 },
        { "Pinned memory, device buffer, copy", BM_DEVICE | BM_COPY, 2 },
        { "Pinned memory, host buffer, copy", BM_HOST | BM_COPY, 2 },
        { 0, 0, 0 }
    };

    int i = 0;
    for(;; ++i) {
        if (items[i].msg == 0) break;
        fprintf(stderr, "%d: %s\n", i, items[i].msg);
    }
        int memtype = i;
        scanf("%d", &memtype);
        bufferMode_ = items[memtype].flags;

        switch(items[memtype].allocType) {
            case 0: test<RegularMemory>(); break;
            case 1: test<AlignedMemory>(); break;
            case 2: test<PinnedMemory>(); break;
        }
    return 0;
}