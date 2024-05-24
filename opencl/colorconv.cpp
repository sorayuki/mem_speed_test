#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 200

#include <CL/opencl.hpp>
#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <windows.h>

namespace {
    bool available_ = false;
    cl::Context ctx_;
    cl::CommandQueue queue_;
    cl::Program program_;
    size_t gmemAlign_;

    std::atomic_int32_t alloc_buffers_;

    // buffer池
    struct BufferItem {
        size_t capacity = 0;
        size_t size = 0;
        cl::Buffer buffer;
        cl::Buffer hostBuffer;
        void* hostPtr;
        BufferItem* prev = nullptr;
        BufferItem* next = nullptr;

        std::chrono::steady_clock::time_point recycleTime = {};

        BufferItem(size_t capacity) {
            ++alloc_buffers_;
            buffer = cl::Buffer(ctx_, CL_MEM_READ_WRITE, capacity);
            hostBuffer = cl::Buffer(ctx_, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR, capacity);
            hostPtr = queue_.enqueueMapBuffer(hostBuffer, true, CL_MAP_WRITE, 0, capacity);
            this->capacity = capacity;
        }

        ~BufferItem() {
            queue_.enqueueUnmapMemObject(hostBuffer, hostPtr);
            --alloc_buffers_;
        }

        bool From(const void* src) {
            // auto dst = queue_.enqueueMapBuffer(buffer, true, CL_MAP_WRITE_INVALIDATE_REGION, 0, capacity);
            // if (dst) {
            //     std::memcpy(dst, src, size);
            //     queue_.enqueueUnmapMemObject(buffer, dst);
            // }
            // return dst != nullptr;

            // hostPtr = queue_.enqueueMapBuffer(hostBuffer, true, CL_MAP_WRITE_INVALIDATE_REGION, 0, size);
            // queue_.enqueueUnmapMemObject(hostBuffer, hostPtr);
            // queue_.enqueueCopyBuffer(hostBuffer, buffer, 0, 0, size);

            // memcpy(hostPtr, src, size);
            queue_.enqueueWriteBuffer(buffer, false, 0, size, src);

            return true;
        }

        bool To(void* dst) {
            // auto src = queue_.enqueueMapBuffer(buffer, true, CL_MAP_READ, 0, capacity);
            // if (src) {
            //     std::memcpy(dst, src, size);
            //     queue_.enqueueUnmapMemObject(buffer, src);
            // }
            // return src != nullptr;

            // queue_.enqueueCopyBuffer(buffer, hostBuffer, 0, 0, size);
            // hostPtr = queue_.enqueueMapBuffer(hostBuffer, true, CL_MAP_READ, 0, size);
            // queue_.enqueueUnmapMemObject(hostBuffer, hostPtr);

            queue_.enqueueReadBuffer(buffer, true, 0, size, dst);
            // memcpy(dst, hostPtr, size);

            return true;
        }

        cl::Buffer& operator*() {
            return buffer;
        }
    };

    std::atomic_flag poolLock = ATOMIC_FLAG_INIT;
    BufferItem* poolHead = nullptr;

    const int maxRecycledBufferSize = 80 * 1024 * 1024; // 80 MB 池的大小
    std::chrono::steady_clock::duration maxRecycledBufferLife = std::chrono::milliseconds(500); // 500 毫秒过期时间

    void CleanBuffer() {
        auto now = std::chrono::steady_clock::now();
        while (poolLock.test_and_set());
        size_t totalSize = 0;
        BufferItem* cur = poolHead;
        // 找到限制点
        while (cur && totalSize < maxRecycledBufferSize && now - cur->recycleTime < maxRecycledBufferLife) {
            totalSize += cur->capacity;
            cur = cur->next;
        }

        // 断开
        if (cur) {
            if (cur->prev)
                cur->prev->next = nullptr;
            else
                poolHead = nullptr;
        }
        poolLock.clear();

        // 释放
        std::unique_ptr<BufferItem> x(cur);
        while (x)
            x.reset(x->next);
    }

    void ReleaseBuffer(BufferItem* item) {
        item->recycleTime = std::chrono::steady_clock::now();

        while (poolLock.test_and_set());
        item->prev = nullptr;
        item->next = poolHead;
        if (poolHead)
            poolHead->prev = item;
        poolHead = item;
        poolLock.clear();

        CleanBuffer();
    }

    struct BufferItemDeleter {
        void operator()(BufferItem* item) const {
            if (item) {
                ReleaseBuffer(item);
            }
        }
    };

    using BufferItemPtr = std::unique_ptr<BufferItem, BufferItemDeleter>;

    BufferItemPtr getBuffer(size_t size) {
        CleanBuffer();

        while (poolLock.test_and_set());
        BufferItem* item = poolHead;
        while (item) {
            if (item->capacity >= size && item->capacity <= size * 2) {
                item->size = size;
                break;
            }
            item = item->next;
        }

        // 摘除节点
        if (item) {
            if (item->prev) {
                item->prev->next = item->next;
            }
            else {
                poolHead = item->next;
            }

            if (item->next)
                item->next->prev = item->prev;
        }
        poolLock.clear();

        // 新建节点
        if (!item) {
            auto capacity = size;
            if (capacity % gmemAlign_ != 0) {
                capacity += gmemAlign_ - (capacity % gmemAlign_);
            }
            item = new BufferItem{ capacity };
            item->size = size;
        }

        return BufferItemPtr(item);
    }
};

class CLEnv {
public:
    CLEnv() {
        std::vector<std::tuple<cl::Platform, cl::Device>> platdevlist;
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
                }
            }
        }

        for(int i = 0; i < platdevlist.size(); ++i) {
            printf("%d: platform: %s, device: %s\n", i, std::get<0>(platdevlist[i]).getInfo<CL_PLATFORM_NAME>().c_str(), std::get<1>(platdevlist[i]).getInfo<CL_DEVICE_NAME>().c_str());
        }
        int selected_index;
        printf("select one: ");
        fflush(stdout);
        scanf("%d", &selected_index);

        std::tuple<cl::Platform, cl::Device> selected = platdevlist[selected_index];

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

            kernel void nv12_to_bgra_frame(
	            global uchar* dst, int stride,
	            global const uchar* srcY, int strideY,
	            global const uchar* srcUV, int strideUV,
                const float4 rfactor, const float4 gfactor, const float4 bfactor
            ) {
	            const int xd2 = get_global_id(0);
	            const int yd2 = get_global_id(1);
                const int x = xd2 * 2;
                const int y = yd2 * 2;

                uchar2 uv = vload2(xd2, srcUV + yd2 * strideUV);

                __attribute__((opencl_unroll_hint))
                for(int j = 0; j < 2; ++j) {
                    __attribute__((opencl_unroll_hint))
                    for(int i = 0; i < 2; ++i) {
                        uchar4 srcu8 = (uchar4)(srcY[(y + j) * strideY + (x + i)], uv.x, uv.y, 1);
                        float4 srcvec = convert_float4(srcu8) - (float4)(0.0, 128.0, 128.0, 0.0);
                        uchar4 dstu8 = convert_uchar4_sat((float4)(dot(srcvec, bfactor), dot(srcvec, gfactor), dot(srcvec, rfactor), 255.0));
                        vstore4(dstu8, x + i, dst + (y + j) * stride);
                    }
                }
            }

            kernel void i420_to_bgra_frame(
	            global uchar* dst, int stride,
	            global const uchar* srcY, int strideY,
	            global const uchar* srcU, int strideU,
                global const uchar* srcV, int strideV,
                const float4 rfactor, const float4 gfactor, const float4 bfactor
            ) {
	            const int xd2 = get_global_id(0);
	            const int yd2 = get_global_id(1);
                const int x = xd2 * 2;
                const int y = yd2 * 2;

                uchar su = srcU[yd2 * strideU + xd2];
                uchar sv = srcV[yd2 * strideV + xd2];

                __attribute__((opencl_unroll_hint))
                for(int j = 0; j < 2; ++j) {
                    __attribute__((opencl_unroll_hint))
                    for(int i = 0; i < 2; ++i) {
                        uchar sy = srcY[(y + j) * strideY + (x + i)];
                        uchar4 srcu8 = (uchar4)(sy, su, sv, 1);
                        float4 srcvec = convert_float4(srcu8) - (float4)(0.0, 128.0, 128.0, 0.0);
                        uchar4 dstu8 = convert_uchar4_sat((float4)(dot(srcvec, bfactor), dot(srcvec, gfactor), dot(srcvec, rfactor), 255.0));
                        vstore4(dstu8, x + i, dst + (y + j) * stride);
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
                MessageBoxA(NULL, pair.second.c_str(), "error", MB_ICONERROR);
            }

            return;
        }

        available_ = true;
    }

    ~CLEnv() {
        std::unique_ptr<BufferItem> x{};

        while (poolLock.test_and_set());
        x.reset(poolHead);
        poolHead = nullptr;
        poolLock.clear();

        while (x) {
            x.reset(x->next);
        }
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
    int width, int height,
    const void* src, int stride,
    void* dstY, int strideY,
    void* dstU, int strideU,
    void* dstV, int strideV
) {
    if (!available_)
        return false;

    auto func = cl::KernelFunctor<
        cl::Buffer, int, 
        cl::Buffer, int, cl::Buffer, int, cl::Buffer, int,
        cl_float4, cl_float4, cl_float4,
        cl_uchar2, cl_uchar2>
        (program_, "bgra_to_i420_frame");

    auto factors = GetRGB2YUVFactor();
    if (!factors)
        return false;

    auto [yfactor, ufactor, vfactor, yrange, uvrange] = *factors;

    // auto srcbuf = getBuffer(stride * height);
    // auto ybuf = getBuffer(height * strideY);
    // auto ubuf = getBuffer(height / 2 * strideU);
    // auto vbuf = getBuffer(height / 2 * strideV);

    // srcbuf->From(src);

    cl::Buffer srcbuf(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, (size_t)stride * height, (void*)src);
    cl::Buffer ybuf(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, (size_t)strideY * height, (void*)dstY);
    cl::Buffer ubuf(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, (size_t)strideU * height / 2, (void*)dstU);
    cl::Buffer vbuf(ctx_, CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, (size_t)strideV * height / 2, (void*)dstV);

    func(cl::EnqueueArgs(queue_, cl::NDRange(width / 2, height / 2)),
        srcbuf, stride, ybuf, strideY, ubuf, strideU, vbuf, strideV,
        yfactor, ufactor, vfactor,
        yrange, uvrange
    );

    queue_.enqueueMapBuffer(ybuf, true, CL_MEM_READ_ONLY, 0, strideY * height);
    queue_.enqueueMapBuffer(ubuf, true, CL_MEM_READ_ONLY, 0, strideU * height / 2);
    queue_.enqueueMapBuffer(vbuf, true, CL_MEM_READ_ONLY, 0, strideV * height / 2);
    queue_.enqueueUnmapMemObject(ybuf, dstY);
    queue_.enqueueUnmapMemObject(ubuf, dstU);
    queue_.enqueueUnmapMemObject(vbuf, dstV);

    // ybuf->To(dstY);
    // ubuf->To(dstU);
    // vbuf->To(dstV);

    return true;
}


bool opencl_nv12_to_bgra_frame(
    int width, int height,
    void* dst, int stride,
    const void* srcY, int strideY,
    const void* srcUV, int strideUV
) {
    if (!available_)
        return false;

    auto factors = GetYUV2RGBFactor();
    if (!factors)
        return false;

    auto [rfactor, gfactor, bfactor] = *factors;

    auto func = cl::KernelFunctor<
        cl::Buffer, int, 
        cl::Buffer, int, cl::Buffer, int,
        cl_float4, cl_float4, cl_float4>
        (program_, "nv12_to_bgra_frame");

    auto dstbuf = getBuffer(stride * height);
    auto ybuf = getBuffer(height * strideY);
    auto uvbuf = getBuffer(height / 2 * strideUV);
    
    ybuf->From(srcY);
    uvbuf->From(srcUV);

    func(cl::EnqueueArgs(queue_, cl::NDRange(width / 2, height / 2)),
        **dstbuf, stride, **ybuf, strideY, **uvbuf, strideUV,
        rfactor, gfactor, bfactor
    );
    
    dstbuf->To(dst);

    return true;
}


bool opencl_i420_to_bgra_frame(
    const void* y, size_t strideY,
    const void* u, size_t strideU,
    const void* v, size_t strideV,
    unsigned int width, unsigned int height,
    size_t dst_stride, void* dst
) {
    if (!available_)
        return false;

    auto factors = GetYUV2RGBFactor();
    if (!factors)
        return false;

    auto [rfactor, gfactor, bfactor] = *factors;

    auto func = cl::KernelFunctor<
        cl::Buffer, int,
        cl::Buffer, int, cl::Buffer, int, cl::Buffer, int,
        cl_float4, cl_float4, cl_float4>
        (program_, "i420_to_bgra_frame");

    auto dstbuf = getBuffer(dst_stride * height);
    auto ybuf = getBuffer(height * strideY);
    auto ubuf = getBuffer(height / 2 * strideU);
    auto vbuf = getBuffer(height / 2 * strideV);

    ybuf->From(y);
    ubuf->From(u);
    vbuf->From(v);

    func(cl::EnqueueArgs(queue_, cl::NDRange(width / 2, height / 2)),
        **dstbuf, dst_stride,
        **ybuf, strideY, **ubuf, strideU, **vbuf, strideV,
        rfactor, gfactor, bfactor
    );

    dstbuf->To(dst);

    return true;
}

int main() {
    // std::vector<char> src(2560 * 1440 * 4);
    // std::vector<char> y(2560 * 1440);
    // std::vector<char> u(2560 * 1440 / 4);
    // std::vector<char> v(2560 * 1440 / 4);
    auto src = _aligned_malloc(2560 * 1440 * 4, 1);
    auto y = (char*)_aligned_malloc(2560 * 1440, 1);
    auto u = (char*)_aligned_malloc(2560 * 1440 / 4, 1);
    auto v = (char*)_aligned_malloc(2560 * 1440 / 4, 1);
    memset(src, 0, 2560 * 1440 * 4);
    memset(y, 0, 2560 * 1440);
    memset(u, 0, 2560 * 1440 / 4);
    memset(v, 0, 2560 * 1440 / 4);

    auto begin = std::chrono::steady_clock::now();
    size_t frames = 0;
    double fps = 60.0;

    for(;;) {
        auto diff = std::chrono::steady_clock::now() - begin;
        if (diff > std::chrono::seconds(1)) {
            printf("fps = %g\n", frames * 1000.0 / std::chrono::duration_cast<std::chrono::milliseconds>(diff).count());
            begin = std::chrono::steady_clock::now();
            frames = 0;
        }
        // auto targetTime = begin + frames * std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)) / fps;
        // std::this_thread::sleep_until(targetTime);
        opencl_bgra_to_i420_frame(2560, 1440, 
            src, 2560 * 4,
            y, 2560,
            u, 2560 / 2,
            v, 2560 / 2
        );
        frames += 1;
        // printf("frame: %d\n", frames);

        if (false && frames == 1) {
            for(int i = 0; i < 2560 * 1440; ++i) if (y[i] != 16) { printf("check failed.\n"); break; }
            for(int i = 0; i < 2560 * 1440 / 4; ++i) if (u[i] != -128) { printf("check failed.\n"); break; }
            for(int i = 0; i < 2560 * 1440 / 4; ++i) if (v[i] != -128) { printf("check failed.\n"); break; }
        }
    }

    _aligned_free(src);
    _aligned_free(y);
    _aligned_free(u);
    _aligned_free(v);

    return 0;
}