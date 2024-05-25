#define CL_HPP_ENABLE_EXCEPTIONS
#define CL_HPP_TARGET_OPENCL_VERSION 200

#include <CL/opencl.hpp>
#include <vector>
#include <string>
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
    bool isClShareMemory_ = false;

    class CCClBuffer {
        size_t size_;
        cl::Buffer buffer_;
        void* hostPtr_;

        bool isUseHost_ = false;
        bool isWrite_ = false;

        void Init(void* ptr, size_t size, bool isWrite) {
            size_ = size;
            isWrite_ = isWrite;
            hostPtr_ = ptr;

            auto addr = (intptr_t)ptr;
            cl_mem_flags rwFlag = isWrite_ ? CL_MEM_WRITE_ONLY : CL_MEM_READ_ONLY;
            if (isClShareMemory_ && addr % gmemAlign_ == 0) {
                buffer_ = cl::Buffer{ ctx_, CL_MEM_USE_HOST_PTR | rwFlag, size_, ptr };
                isUseHost_ = true;
            }
            else {
                buffer_ = cl::Buffer{ ctx_, rwFlag , size_ };
                if (!isWrite_) {
                    queue_.enqueueWriteBuffer(buffer_, false, 0, size_, ptr);
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
            if (isUseHost_ && isWrite_) {
                // 同步数据刷一下
                void* hostptr = queue_.enqueueMapBuffer(buffer_, true, CL_MAP_READ, 0, size_);
                queue_.enqueueUnmapMemObject(buffer_, hostptr);
            }
            else {
                queue_.enqueueReadBuffer(buffer_, true, 0, size_, hostPtr_);
            }
        }

        operator cl::Buffer& () {
            return buffer_;
        }
    };
};

size_t cl_mem_align() {
    // return gmemAlign_;
    static std::optional<size_t> align;
    if (!align) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        align = std::lcm(gmemAlign_, si.dwPageSize);
    }
    
    return *align;
}


class CLEnv {
public:
    CLEnv() {
        std::vector<std::tuple<cl::Platform, cl::Device>> platdevlist;
        std::vector<bool> isShareMemory;
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
                    isShareMemory.emplace_back(p.getInfo<CL_PLATFORM_NAME>().find("Intel") != std::string::npos);
                }
            }
        }

        for(int i = 0; i < platdevlist.size(); ++i) {
            fprintf(stderr, "%d: platform: %s, device: %s\n", i, std::get<0>(platdevlist[i]).getInfo<CL_PLATFORM_NAME>().c_str(), std::get<1>(platdevlist[i]).getInfo<CL_DEVICE_NAME>().c_str());
        }
        int selected_index;
        fprintf(stderr, "select one: \n");
        fflush(stdout);
        scanf("%d", &selected_index);

        auto selected = platdevlist[selected_index];
        isClShareMemory_ = isShareMemory[selected_index];

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


int main() {
    // std::vector<char> src(2560 * 1440 * 4);
    // std::vector<char> y(2560 * 1440);
    // std::vector<char> u(2560 * 1440 / 4);
    // std::vector<char> v(2560 * 1440 / 4);
    auto src = _aligned_malloc(2560 * 1440 * 4, cl_mem_align());
    auto y = (char*)_aligned_malloc(2560 * 1440, cl_mem_align());
    auto u = (char*)_aligned_malloc(2560 * 1440 / 4, cl_mem_align());
    auto v = (char*)_aligned_malloc(2560 * 1440 / 4, cl_mem_align());
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
            fprintf(stderr, "fps = %g\n", frames * 1000.0 / std::chrono::duration_cast<std::chrono::milliseconds>(diff).count());
            fflush(stdout);
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
        // fprintf(stderr, "frame: %d\n", frames);

        if (false && frames == 1) {
            for(int i = 0; i < 2560 * 1440; ++i) if (y[i] != 16) { fprintf(stderr, "check failed.\n"); break; }
            for(int i = 0; i < 2560 * 1440 / 4; ++i) if (u[i] != -128) { fprintf(stderr, "check failed.\n"); break; }
            for(int i = 0; i < 2560 * 1440 / 4; ++i) if (v[i] != -128) { fprintf(stderr, "check failed.\n"); break; }
        }
    }

    _aligned_free(src);
    _aligned_free(y);
    _aligned_free(u);
    _aligned_free(v);

    return 0;
}