#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl31.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <atlbase.h>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxgi.lib")

class ColorConvGLES {
    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;

    bool useComputeShader_ = true;
    bool mapInputBuffer_ = true;

    GLuint shaderProgram_ = 0;
    GLuint inputBufferId_ = 0;
    int inputStrideBytes_ = 0;
    GLuint outputYBufferId_ = 0;
    int outputYStrideBytes_ = 0;
    GLuint outputUBufferId_ = 0;
    int outputUStrideBytes_ = 0;
    GLuint outputVBufferId_ = 0;
    int outputVStrideBytes_ = 0;
    
    // For vertex shader pipeline
    GLuint vao_ = 0;
    GLuint vbo_ = 0;

    int width_ = 0;
    int height_ = 0;
    
    void* mappedYPtr_ = nullptr;
    void* mappedUPtr_ = nullptr;
    void* mappedVPtr_ = nullptr;

    EGLConfig chooseConfig() {
        EGLint configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
        };
        
        EGLint numConfigs;
        EGLConfig config;
        
        if (!eglChooseConfig(eglDisplay_, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
            throw std::runtime_error("Failed to choose EGL config");
        }
        
        return config;
    }

    bool CreateEGLContext() {
        eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (eglDisplay_ == EGL_NO_DISPLAY) {
            throw std::runtime_error("Failed to get EGL display");
        }
        
        EGLint major, minor;
        if (!eglInitialize(eglDisplay_, &major, &minor)) {
            throw std::runtime_error("Failed to initialize EGL");
        }
        
        EGLConfig config = chooseConfig();
        
        EGLint contextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_CONTEXT_MINOR_VERSION, 1,
            EGL_NONE
        };
        
        eglContext_ = eglCreateContext(eglDisplay_, config, EGL_NO_CONTEXT, contextAttribs);
        if (eglContext_ == EGL_NO_CONTEXT) {
            throw std::runtime_error("Failed to create EGL context");
        }
        
        // Create PBuffer surface
        EGLint pbufferAttribs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        
        eglSurface_ = eglCreatePbufferSurface(eglDisplay_, config, pbufferAttribs);
        if (eglSurface_ == EGL_NO_SURFACE) {
            throw std::runtime_error("Failed to create PBuffer surface");
        }
        
        if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
            throw std::runtime_error("Failed to make EGL context current");
        }
        
        return true;
    }

    bool BuildShader() {
        GLuint vertexShader, fragmentShader, computeShader;
        
        // Shared shader code
        const char* sharedShaderCode = R"(
layout(std430, binding = 0) buffer InputBuffer {
    uint inputData[];
};

layout(std430, binding = 1) buffer OutputYBuffer {
    uint outputYData[];
};

layout(std430, binding = 2) buffer OutputUBuffer {
    uint outputUData[];
};

layout(std430, binding = 3) buffer OutputVBuffer {
    uint outputVData[];
};

uniform int width;
uniform int height;
uniform int inputStride;
uniform int outputYStride;
uniform int outputUStride;
uniform int outputVStride;

// YUV conversion matrices (vec4 to support full/partial range control)
// Current: Full range configuration
// Y = 0.299*R + 0.587*G + 0.114*B + 0.0     -> Y range [0,255]
// U = -0.169*R - 0.331*G + 0.5*B + 128.0    -> U range [0,255] 
// V = 0.5*R - 0.419*G - 0.081*B + 128.0     -> V range [0,255]
//
// For partial range (TV range), modify to:
// Y = 0.257*R + 0.504*G + 0.098*B + 16.0    -> Y range [16,235]
// U = -0.148*R - 0.291*G + 0.439*B + 128.0  -> U range [16,240]
// V = 0.439*R - 0.368*G - 0.071*B + 128.0   -> V range [16,240]
const vec4 yCoeff = vec4(0.299, 0.587, 0.114, 0.0);      // w component for range offset
const vec4 uCoeff = vec4(-0.169, -0.331, 0.5, 128.0);    // w component for U offset
const vec4 vCoeff = vec4(0.5, -0.419, -0.081, 128.0);    // w component for V offset

// Shared conversion functions
vec3 rgbaToYuv(vec4 rgba) {
    float y_val = dot(rgba, yCoeff);
    float u_val = dot(rgba, uCoeff);
    float v_val = dot(rgba, vCoeff);
    return vec3(y_val, u_val, v_val);
}

float rgbaToY(vec4 rgba) {
    return dot(rgba, yCoeff);
}

void processRegion(ivec2 topLeft) {
    // Process 8x2 region (16 pixels) - outputs exactly 16 Y, 4 U, 4 V samples
    // Each shader invocation processes one 8x2 block aligned to 4-byte boundaries
    
    uint uPacked = 0u;
    uint vPacked = 0u;

    // Process Y data - 16 pixels total (8 pixels x 2 rows)
    #pragma unroll 2
    for (int dy = 0; dy < 2; dy++) {
        // Process 8 pixels per row, write 2 uints (4 bytes each)
        #pragma unroll 2
        for (int dx = 0; dx < 8; dx += 4) {
            uint yPacked = 0u;
            #pragma unroll 4
            for (int i = 0; i < 4; i++) {
                int x = topLeft.x + dx + i;
                int y = topLeft.y + dy;
                
                // Get input pixel
                int inputIdx = (y * inputStride + x * 4) / 4;
                uint pixel = inputData[inputIdx];
                
                // Unpack RGBA to float vec4
                vec4 rgba = vec4(
                    float((pixel >> 0) & 0xFFu),
                    float((pixel >> 8) & 0xFFu),
                    float((pixel >> 16) & 0xFFu),
                    1.0  // Alpha component for range control
                );
                
                if (dy == 0 && i % 2 == 0) {
                    // Convert to YUV using vector dot product
                    vec3 yuv = rgbaToYuv(rgba);
                    
                    // Pack Y value
                    uint y_val = uint(clamp(yuv.x, 0.0, 255.0));
                    yPacked |= (y_val << (i * 8));
                    uPacked |= (uint(clamp(yuv.y, 0.0, 255.0)) << ((dx + i) * 4));
                    vPacked |= (uint(clamp(yuv.z, 0.0, 255.0)) << ((dx + i) * 4));
                } else {
                    uint y_val = uint(clamp(rgbaToY(rgba), 0.0, 255.0));
                    yPacked |= (y_val << (i * 8));
                }
            }
            
            // Write Y data (4 bytes at once)
            int yIdx = ((topLeft.y + dy) * outputYStride + topLeft.x + dx) / 4;
            outputYData[yIdx] = yPacked;
        }
    }
    
    // Write U and V data (4 samples each as 1 uint)
    int uvY = topLeft.y / 2;
    int uvX = topLeft.x / 2;
    int uIdx = (uvY * outputUStride + uvX) / 4;
    int vIdx = (uvY * outputVStride + uvX) / 4;
    
    outputUData[uIdx] = uPacked;
    outputVData[vIdx] = vPacked;
}
)";
        
        if (useComputeShader_) {
            // Compute shader for RGBA to I420 conversion
            std::string computeShaderSource = R"(#version 310 es
precision highp float;
precision highp int;

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

)" + std::string(sharedShaderCode) + R"(

void main() {
    // Each workgroup processes 8x2 region (16 Y samples, 4 U samples, 4 V samples)
    ivec2 topLeft = ivec2(int(gl_GlobalInvocationID.x) * 8, int(gl_GlobalInvocationID.y) * 2);
    
    if (topLeft.x >= width || topLeft.y >= height) {
        return;
    }
    
    processRegion(topLeft);
}
)";
            
            computeShader = glCreateShader(GL_COMPUTE_SHADER);
            const char* computeShaderSourcePtr = computeShaderSource.c_str();
            glShaderSource(computeShader, 1, &computeShaderSourcePtr, nullptr);
            glCompileShader(computeShader);
            
            GLint success;
            glGetShaderiv(computeShader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLchar infoLog[512];
                glGetShaderInfoLog(computeShader, 512, nullptr, infoLog);
                throw std::runtime_error("Compute shader compilation failed: " + std::string(infoLog));
            }
            
            shaderProgram_ = glCreateProgram();
            glAttachShader(shaderProgram_, computeShader);
            glLinkProgram(shaderProgram_);
            
            glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &success);
            if (!success) {
                GLchar infoLog[512];
                glGetProgramInfoLog(shaderProgram_, 512, nullptr, infoLog);
                throw std::runtime_error("Shader program linking failed: " + std::string(infoLog));
            }
            
            glDeleteShader(computeShader);
        } else {
            // Vertex shader
            const char* vertexShaderSource = R"(#version 310 es
in vec4 position;

void main() {
    gl_Position = position;
}
)";
            
            // Fragment shader
            std::string fragmentShaderSource = R"(#version 310 es
precision highp float;
precision highp int;

)" + std::string(sharedShaderCode) + R"(

void main() {
    ivec2 topLeft = ivec2(gl_FragCoord.xy * vec2(8, 2));

    if (topLeft.x >= width || topLeft.y >= height) {
        discard;
    }
    
    processRegion(topLeft);
    
    discard;
}
)";
            
            vertexShader = glCreateShader(GL_VERTEX_SHADER);
            glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
            glCompileShader(vertexShader);
            
            GLint success;
            glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLchar infoLog[512];
                glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
                throw std::runtime_error("Vertex shader compilation failed: " + std::string(infoLog));
            }
            
            fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
            const char* fragmentShaderSourcePtr = fragmentShaderSource.c_str();
            glShaderSource(fragmentShader, 1, &fragmentShaderSourcePtr, nullptr);
            glCompileShader(fragmentShader);
            
            glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
            if (!success) {
                GLchar infoLog[512];
                glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
                throw std::runtime_error("Fragment shader compilation failed: " + std::string(infoLog));
            }
            
            shaderProgram_ = glCreateProgram();
            glAttachShader(shaderProgram_, vertexShader);
            glAttachShader(shaderProgram_, fragmentShader);
            glLinkProgram(shaderProgram_);
            
            glGetProgramiv(shaderProgram_, GL_LINK_STATUS, &success);
            if (!success) {
                GLchar infoLog[512];
                glGetProgramInfoLog(shaderProgram_, 512, nullptr, infoLog);
                throw std::runtime_error("Shader program linking failed: " + std::string(infoLog));
            }
            
            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);
            
            // Create VAO and VBO for fullscreen quad
            glGenVertexArrays(1, &vao_);
            glGenBuffers(1, &vbo_);
            
            float vertices[] = {
                -1.0f, -1.0f, 0.0f, 1.0f,
                 1.0f, -1.0f, 0.0f, 1.0f,
                -1.0f,  1.0f, 0.0f, 1.0f,
                 1.0f,  1.0f, 0.0f, 1.0f
            };
            
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
            
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
            
            glBindVertexArray(0);
        }
        
        return true;
    }

    bool CreateIOBuffers() {
        // Create input buffer
        glGenBuffers(1, &inputBufferId_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, inputBufferId_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, inputStrideBytes_ * height_, nullptr, GL_DYNAMIC_DRAW);
        
        // Create output Y buffer
        glGenBuffers(1, &outputYBufferId_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputYBufferId_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, outputYStrideBytes_ * height_, nullptr, GL_DYNAMIC_READ);
        
        // Create output U buffer
        glGenBuffers(1, &outputUBufferId_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputUBufferId_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, outputUStrideBytes_ * (height_ / 2), nullptr, GL_DYNAMIC_READ);
        
        // Create output V buffer
        glGenBuffers(1, &outputVBufferId_);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputVBufferId_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, outputVStrideBytes_ * (height_ / 2), nullptr, GL_DYNAMIC_READ);
        
        return true;
    }

public:
    ColorConvGLES(bool useComputeShader, bool useMapInputBuffer, int width, int height, int inputStrideBytes, int outputYStrideBytes, int outputUVStrideBytes) 
        : useComputeShader_(useComputeShader), mapInputBuffer_(useMapInputBuffer), width_(width), height_(height), 
          inputStrideBytes_(inputStrideBytes), outputYStrideBytes_(outputYStrideBytes),
          outputUStrideBytes_(outputUVStrideBytes), outputVStrideBytes_(outputUVStrideBytes) {
        
        CreateEGLContext();
        BuildShader();
        CreateIOBuffers();
    }

    ~ColorConvGLES() {
        // Unmap any mapped buffers first
        unmapResult();
        
        // Clean up OpenGL resources
        if (inputBufferId_) glDeleteBuffers(1, &inputBufferId_);
        if (outputYBufferId_) glDeleteBuffers(1, &outputYBufferId_);
        if (outputUBufferId_) glDeleteBuffers(1, &outputUBufferId_);
        if (outputVBufferId_) glDeleteBuffers(1, &outputVBufferId_);
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (shaderProgram_) glDeleteProgram(shaderProgram_);
        
        // Clean up EGL resources
        if (eglDisplay_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (eglContext_ != EGL_NO_CONTEXT) {
                eglDestroyContext(eglDisplay_, eglContext_);
            }
            if (eglSurface_ != EGL_NO_SURFACE) {
                eglDestroySurface(eglDisplay_, eglSurface_);
            }
            eglTerminate(eglDisplay_);
        }
    }

    void feedInput(char* inputBuffer) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, inputBufferId_);
        if (mapInputBuffer_) {
            void* mappedInput = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, inputStrideBytes_ * height_, GL_MAP_WRITE_BIT);
            memcpy(mappedInput, inputBuffer, inputStrideBytes_ * height_);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        } else {
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, inputStrideBytes_ * height_, inputBuffer);
        }
        
        // Bind buffers to shader
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, inputBufferId_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, outputYBufferId_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, outputUBufferId_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, outputVBufferId_);
        
        glUseProgram(shaderProgram_);
        
        // Set uniforms
        glUniform1i(glGetUniformLocation(shaderProgram_, "width"), width_);
        glUniform1i(glGetUniformLocation(shaderProgram_, "height"), height_);
        glUniform1i(glGetUniformLocation(shaderProgram_, "inputStride"), inputStrideBytes_);
        glUniform1i(glGetUniformLocation(shaderProgram_, "outputYStride"), outputYStrideBytes_);
        glUniform1i(glGetUniformLocation(shaderProgram_, "outputUStride"), outputUStrideBytes_);
        glUniform1i(glGetUniformLocation(shaderProgram_, "outputVStride"), outputVStrideBytes_);
        
        if (useComputeShader_) {
            // Dispatch compute shader - each workgroup processes 8x2 region
            glDispatchCompute((width_ + 7) / 8, (height_ + 1) / 2, 1);
        } else {
            glViewport(0, 0, (width_ + 7) / 8, (height_ + 1) / 2);
            // Render fullscreen quad
            glBindVertexArray(vao_);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindVertexArray(0);
        }
        
        // Wait for completion
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glFinish();
    }

    std::tuple<char*, char*, char*> mapResult() {
        // Map Y buffer
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputYBufferId_);
        mappedYPtr_ = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, outputYStrideBytes_ * height_, GL_MAP_READ_BIT);
        
        // Map U buffer
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputUBufferId_);
        mappedUPtr_ = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, outputUStrideBytes_ * (height_ / 2), GL_MAP_READ_BIT);
        
        // Map V buffer
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputVBufferId_);
        mappedVPtr_ = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, outputVStrideBytes_ * (height_ / 2), GL_MAP_READ_BIT);
        
        return std::make_tuple(static_cast<char*>(mappedYPtr_), static_cast<char*>(mappedUPtr_), static_cast<char*>(mappedVPtr_));
    }

    void unmapResult() {
        // Unmap Y buffer
        if (mappedYPtr_) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputYBufferId_);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            mappedYPtr_ = nullptr;
        }
        
        // Unmap U buffer
        if (mappedUPtr_) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputUBufferId_);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            mappedUPtr_ = nullptr;
        }
        
        // Unmap V buffer
        if (mappedVPtr_) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, outputVBufferId_);
            glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            mappedVPtr_ = nullptr;
        }
    }
};


class ColorConvD3D11 {
    CComPtr<ID3D11Device> device_;
    CComPtr<ID3D11DeviceContext> context_;
    CComPtr<IDXGISwapChain> swapChain_;
    
    bool useComputeShader_ = true;
    bool mapInputBuffer_ = true;
    
    CComPtr<ID3D11ComputeShader> computeShader_;
    CComPtr<ID3D11VertexShader> vertexShader_;
    CComPtr<ID3D11PixelShader> pixelShader_;
    
    CComPtr<ID3D11Buffer> inputBuffer_;
    CComPtr<ID3D11Buffer> inputUAVBuffer_;  // Separate UAV buffer for compute operations
    CComPtr<ID3D11Buffer> outputYBuffer_;
    CComPtr<ID3D11Buffer> outputUBuffer_;
    CComPtr<ID3D11Buffer> outputVBuffer_;
    
    CComPtr<ID3D11Buffer> constantBuffer_;
    
    CComPtr<ID3D11UnorderedAccessView> inputBufferUAV_;
    CComPtr<ID3D11UnorderedAccessView> outputYBufferUAV_;
    CComPtr<ID3D11UnorderedAccessView> outputUBufferUAV_;
    CComPtr<ID3D11UnorderedAccessView> outputVBufferUAV_;
    
    CComPtr<ID3D11ShaderResourceView> inputBufferSRV_;
    
    // For vertex shader pipeline
    CComPtr<ID3D11Buffer> vertexBuffer_;
    CComPtr<ID3D11InputLayout> inputLayout_;
    CComPtr<ID3D11RasterizerState> rasterizerState_;
    CComPtr<ID3D11DepthStencilState> depthStencilState_;
    CComPtr<ID3D11BlendState> blendState_;
    
    int width_ = 0;
    int height_ = 0;
    int inputStrideBytes_ = 0;
    int outputYStrideBytes_ = 0;
    int outputUStrideBytes_ = 0;
    int outputVStrideBytes_ = 0;
    
    void* mappedYPtr_ = nullptr;
    void* mappedUPtr_ = nullptr;
    void* mappedVPtr_ = nullptr;
    
    // Staging buffers for reading back results  
    CComPtr<ID3D11Buffer> stagingYBuffer_;
    CComPtr<ID3D11Buffer> stagingUBuffer_;
    CComPtr<ID3D11Buffer> stagingVBuffer_;
    
    struct ConstantBufferData {
        int width;
        int height;
        int inputStride;
        int outputYStride;
        int outputUStride;
        int outputVStride;
        int padding[2];
    };
    
    bool CreateD3D11Device() {
        HRESULT hr;
        
        // Create device and context
        D3D_FEATURE_LEVEL featureLevel;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &device_,
            &featureLevel,
            &context_
        );
        
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create D3D11 device");
        }
        
        return true;
    }
    
    bool CreateShaders() {
        // Shared HLSL shader code
        const char* sharedShaderCode = R"(
RWStructuredBuffer<uint> inputData : register(u0);
RWStructuredBuffer<uint> outputYData : register(u1);
RWStructuredBuffer<uint> outputUData : register(u2);
RWStructuredBuffer<uint> outputVData : register(u3);

cbuffer Constants : register(b0) {
    int width;
    int height;
    int inputStride;
    int outputYStride;
    int outputUStride;
    int outputVStride;
    int padding[2];
}

// YUV conversion coefficients (same as OpenGL ES version)
static const float4 yCoeff = float4(0.299, 0.587, 0.114, 0.0);
static const float4 uCoeff = float4(-0.169, -0.331, 0.5, 128.0);
static const float4 vCoeff = float4(0.5, -0.419, -0.081, 128.0);

float3 rgbaToYuv(float4 rgba) {
    float y_val = dot(rgba, yCoeff);
    float u_val = dot(rgba, uCoeff);
    float v_val = dot(rgba, vCoeff);
    return float3(y_val, u_val, v_val);
}

float rgbaToY(float4 rgba) {
    return dot(rgba, yCoeff);
}

void processRegion(int2 topLeft) {
    uint uPacked = 0u;
    uint vPacked = 0u;

    [unroll]
    for (int dy = 0; dy < 2; dy++) {
        [unroll]
        for (int dx = 0; dx < 8; dx += 4) {
            uint yPacked = 0u;
            [unroll]
            for (int i = 0; i < 4; i++) {
                int x = topLeft.x + dx + i;
                int y = topLeft.y + dy;
                
                int inputIdx = (y * inputStride + x * 4) / 4;
                uint pixel = inputData[inputIdx];
                
                float4 rgba = float4(
                    float((pixel >> 0) & 0xFFu),
                    float((pixel >> 8) & 0xFFu),
                    float((pixel >> 16) & 0xFFu),
                    1.0
                );
                
                if (dy == 0 && i % 2 == 0) {
                    float3 yuv = rgbaToYuv(rgba);
                    
                    uint y_val = (uint)clamp(yuv.x, 0.0, 255.0);
                    yPacked |= (y_val << (i * 8));
                    uPacked |= ((uint)clamp(yuv.y, 0.0, 255.0) << ((dx + i) * 4));
                    vPacked |= ((uint)clamp(yuv.z, 0.0, 255.0) << ((dx + i) * 4));
                } else {
                    uint y_val = (uint)clamp(rgbaToY(rgba), 0.0, 255.0);
                    yPacked |= (y_val << (i * 8));
                }
            }
            
            int yIdx = ((topLeft.y + dy) * outputYStride + topLeft.x + dx) / 4;
            outputYData[yIdx] = yPacked;
        }
    }
    
    int uvY = topLeft.y / 2;
    int uvX = topLeft.x / 2;
    int uIdx = (uvY * outputUStride + uvX) / 4;
    int vIdx = (uvY * outputVStride + uvX) / 4;
    
    outputUData[uIdx] = uPacked;
    outputVData[vIdx] = vPacked;
}
)";
        
        HRESULT hr;
        ID3DBlob* shaderBlob = nullptr;
        ID3DBlob* errorBlob = nullptr;
        
        if (useComputeShader_) {
            // Compute shader
            std::string computeShaderSource = std::string(sharedShaderCode) + R"(
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 topLeft = int2(id.x * 8, id.y * 2);
    
    if (topLeft.x >= width || topLeft.y >= height) {
        return;
    }
    
    processRegion(topLeft);
}
)";
            
            hr = D3DCompile(
                computeShaderSource.c_str(),
                computeShaderSource.length(),
                nullptr,
                nullptr,
                nullptr,
                "main",
                "cs_5_0",
                0,
                0,
                &shaderBlob,
                &errorBlob
            );
            
            if (FAILED(hr)) {
                std::string error = errorBlob ? (char*)errorBlob->GetBufferPointer() : "Unknown error";
                if (errorBlob) errorBlob->Release();
                throw std::runtime_error("Compute shader compilation failed: " + error);
            }
            
            hr = device_->CreateComputeShader(
                shaderBlob->GetBufferPointer(),
                shaderBlob->GetBufferSize(),
                nullptr,
                &computeShader_
            );
            
            shaderBlob->Release();
            if (errorBlob) errorBlob->Release();
            
            if (FAILED(hr)) {
                throw std::runtime_error("Failed to create compute shader");
            }
        } else {
            // Vertex shader
            const char* vertexShaderSource = R"(
struct VS_INPUT {
    float4 position : POSITION;
};

struct VS_OUTPUT {
    float4 position : SV_POSITION;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.position = input.position;
    return output;
}
)";
            
            hr = D3DCompile(
                vertexShaderSource,
                strlen(vertexShaderSource),
                nullptr,
                nullptr,
                nullptr,
                "main",
                "vs_5_0",
                0,
                0,
                &shaderBlob,
                &errorBlob
            );
            
            if (FAILED(hr)) {
                std::string error = errorBlob ? (char*)errorBlob->GetBufferPointer() : "Unknown error";
                if (errorBlob) errorBlob->Release();
                throw std::runtime_error("Vertex shader compilation failed: " + error);
            }
            
            hr = device_->CreateVertexShader(
                shaderBlob->GetBufferPointer(),
                shaderBlob->GetBufferSize(),
                nullptr,
                &vertexShader_
            );
            
            if (FAILED(hr)) {
                shaderBlob->Release();
                if (errorBlob) errorBlob->Release();
                throw std::runtime_error("Failed to create vertex shader");
            }
            
            // Create input layout
            D3D11_INPUT_ELEMENT_DESC inputElementDescs[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}
            };
            
            hr = device_->CreateInputLayout(
                inputElementDescs,
                ARRAYSIZE(inputElementDescs),
                shaderBlob->GetBufferPointer(),
                shaderBlob->GetBufferSize(),
                &inputLayout_
            );
            
            shaderBlob->Release();
            if (errorBlob) errorBlob->Release();
            
            if (FAILED(hr)) {
                throw std::runtime_error("Failed to create input layout");
            }
            
            // Pixel shader
            std::string pixelShaderSource = std::string(sharedShaderCode) + R"(
struct PS_INPUT {
    float4 position : SV_POSITION;
};

void main(PS_INPUT input) {
    int2 topLeft = int2(input.position.xy * float2(8, 2));

    if (topLeft.x >= width || topLeft.y >= height) {
        discard;
    }
    
    processRegion(topLeft);
}
)";
            
            hr = D3DCompile(
                pixelShaderSource.c_str(),
                pixelShaderSource.length(),
                nullptr,
                nullptr,
                nullptr,
                "main",
                "ps_5_0",
                0,
                0,
                &shaderBlob,
                &errorBlob
            );
            
            if (FAILED(hr)) {
                std::string error = errorBlob ? (char*)errorBlob->GetBufferPointer() : "Unknown error";
                if (errorBlob) errorBlob->Release();
                throw std::runtime_error("Pixel shader compilation failed: " + error);
            }
            
            hr = device_->CreatePixelShader(
                shaderBlob->GetBufferPointer(),
                shaderBlob->GetBufferSize(),
                nullptr,
                &pixelShader_
            );
            
            shaderBlob->Release();
            if (errorBlob) errorBlob->Release();
            
            if (FAILED(hr)) {
                throw std::runtime_error("Failed to create pixel shader");
            }
        }
        
        return true;
    }
    
    bool CreateBuffers() {
        // Create constant buffer
        constantBuffer_ = CreateConstantBuffer(sizeof(ConstantBufferData));
        
        // Create input buffer
        if (mapInputBuffer_) {
            // For mapped buffer, create a dynamic buffer for CPU write
            inputBuffer_ = CreateDynamicBuffer(inputStrideBytes_ * height_, D3D11_BIND_SHADER_RESOURCE);
            
            // Create separate UAV buffer for compute operations
            inputUAVBuffer_ = CreateStructuredBuffer(inputStrideBytes_ * height_, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        } else {
            // For non-mapped buffer, create a default buffer for GPU access
            inputBuffer_ = CreateStructuredBuffer(inputStrideBytes_ * height_, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
            
            // Use the same buffer for UAV
            inputUAVBuffer_ = inputBuffer_;
        }
        
        // Create output buffers
        outputYBuffer_ = CreateStructuredBuffer(outputYStrideBytes_ * height_, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        outputUBuffer_ = CreateStructuredBuffer(outputUStrideBytes_ * (height_ / 2), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        outputVBuffer_ = CreateStructuredBuffer(outputVStrideBytes_ * (height_ / 2), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        
        // Create staging buffers for reading back results (create once, reuse many times)
        stagingYBuffer_ = CreateStagingBuffer(outputYStrideBytes_ * height_);
        stagingUBuffer_ = CreateStagingBuffer(outputUStrideBytes_ * (height_ / 2));
        stagingVBuffer_ = CreateStagingBuffer(outputVStrideBytes_ * (height_ / 2));
        
        // Create UAVs
        inputBufferUAV_ = CreateBufferUAV(inputUAVBuffer_, (inputStrideBytes_ * height_) / sizeof(uint32_t));
        outputYBufferUAV_ = CreateBufferUAV(outputYBuffer_, (outputYStrideBytes_ * height_) / sizeof(uint32_t));
        outputUBufferUAV_ = CreateBufferUAV(outputUBuffer_, (outputUStrideBytes_ * (height_ / 2)) / sizeof(uint32_t));
        outputVBufferUAV_ = CreateBufferUAV(outputVBuffer_, (outputVStrideBytes_ * (height_ / 2)) / sizeof(uint32_t));
        
        if (!useComputeShader_) {
            // Create vertex buffer for fullscreen quad
            float vertices[] = {
                -1.0f, -1.0f, 0.0f, 1.0f,
                 1.0f, -1.0f, 0.0f, 1.0f,
                -1.0f,  1.0f, 0.0f, 1.0f,
                 1.0f,  1.0f, 0.0f, 1.0f
            };
            
            vertexBuffer_ = CreateVertexBuffer(vertices, sizeof(vertices));
        }
        
        return true;
    }
    
    bool CreateRenderStates() {
        if (useComputeShader_) {
            return true;
        }
        
        HRESULT hr;
        
        // Create rasterizer state
        D3D11_RASTERIZER_DESC rasterizerDesc = {};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.FrontCounterClockwise = false;
        rasterizerDesc.DepthBias = 0;
        rasterizerDesc.SlopeScaledDepthBias = 0.0f;
        rasterizerDesc.DepthBiasClamp = 0.0f;
        rasterizerDesc.DepthClipEnable = true;
        rasterizerDesc.ScissorEnable = false;
        rasterizerDesc.MultisampleEnable = false;
        rasterizerDesc.AntialiasedLineEnable = false;
        
        hr = device_->CreateRasterizerState(&rasterizerDesc, &rasterizerState_);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create rasterizer state");
        }
        
        // Create depth stencil state
        D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
        depthStencilDesc.DepthEnable = false;
        depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;
        depthStencilDesc.StencilEnable = false;
        
        hr = device_->CreateDepthStencilState(&depthStencilDesc, &depthStencilState_);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create depth stencil state");
        }
        
        // Create blend state
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.AlphaToCoverageEnable = false;
        blendDesc.IndependentBlendEnable = false;
        blendDesc.RenderTarget[0].BlendEnable = false;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        
        hr = device_->CreateBlendState(&blendDesc, &blendState_);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create blend state");
        }
        
        return true;
    }
    
public:
    ColorConvD3D11(bool useComputeShader, bool useMapInputBuffer, int width, int height, int inputStrideBytes, int outputYStrideBytes, int outputUVStrideBytes) 
        : useComputeShader_(useComputeShader), mapInputBuffer_(useMapInputBuffer), width_(width), height_(height), 
          inputStrideBytes_(inputStrideBytes), outputYStrideBytes_(outputYStrideBytes),
          outputUStrideBytes_(outputUVStrideBytes), outputVStrideBytes_(outputUVStrideBytes) {
        
        CreateD3D11Device();
        CreateShaders();
        CreateBuffers();
        CreateRenderStates();
    }

    ~ColorConvD3D11() {
        // Unmap any mapped buffers first
        unmapResult();
        
        // CComPtr will automatically release all resources
        // No need to manually call Release()
    }

    void feedInput(char* inputBuffer) {
        HRESULT hr;
        
        // Update input buffer
        if (mapInputBuffer_) {
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            hr = context_->Map(inputBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            if (SUCCEEDED(hr)) {
                memcpy(mappedResource.pData, inputBuffer, inputStrideBytes_ * height_);
                context_->Unmap(inputBuffer_, 0);
            }
            
            // Copy from mapped buffer to UAV buffer
            context_->CopyResource(inputUAVBuffer_, inputBuffer_);
        } else {
            context_->UpdateSubresource(inputBuffer_, 0, nullptr, inputBuffer, inputStrideBytes_, 0);
        }
        
        // Update constant buffer
        ConstantBufferData constantData = {
            width_, height_, inputStrideBytes_, outputYStrideBytes_, 
            outputUStrideBytes_, outputVStrideBytes_, {0, 0}
        };
        
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = context_->Map(constantBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            memcpy(mappedResource.pData, &constantData, sizeof(ConstantBufferData));
            context_->Unmap(constantBuffer_, 0);
        }
        
        if (useComputeShader_) {
            // Set compute shader and resources
            context_->CSSetShader(computeShader_, nullptr, 0);
            
            ID3D11Buffer* constantBuffers[] = {constantBuffer_};
            context_->CSSetConstantBuffers(0, 1, constantBuffers);
            
            ID3D11UnorderedAccessView* uavs[] = {inputBufferUAV_, outputYBufferUAV_, outputUBufferUAV_, outputVBufferUAV_};
            context_->CSSetUnorderedAccessViews(0, 4, uavs, nullptr);
            
            // Dispatch compute shader
            context_->Dispatch((width_ + 7) / 8, (height_ + 1) / 2, 1);
        } else {
            // Set vertex shader pipeline
            context_->VSSetShader(vertexShader_, nullptr, 0);
            context_->PSSetShader(pixelShader_, nullptr, 0);
            
            ID3D11Buffer* constantBuffers[] = {constantBuffer_};
            context_->PSSetConstantBuffers(0, 1, constantBuffers);
            
            // For pixel shader, we need to set UAVs in the output merger stage
            ID3D11UnorderedAccessView* uavs[] = {inputBufferUAV_, outputYBufferUAV_, outputUBufferUAV_, outputVBufferUAV_};
            context_->OMSetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0, 4, uavs, nullptr);
            
            // Set render states
            context_->RSSetState(rasterizerState_);
            context_->OMSetDepthStencilState(depthStencilState_, 0);
            context_->OMSetBlendState(blendState_, nullptr, 0xFFFFFFFF);
            
            // Set input layout and vertex buffer
            context_->IASetInputLayout(inputLayout_);
            UINT stride = sizeof(float) * 4;
            UINT offset = 0;
            ID3D11Buffer* vertexBuffers[] = {vertexBuffer_};
            context_->IASetVertexBuffers(0, 1, vertexBuffers, &stride, &offset);
            context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            
            // Set viewport
            D3D11_VIEWPORT viewport = {};
            viewport.TopLeftX = 0;
            viewport.TopLeftY = 0;
            viewport.Width = (float)((width_ + 7) / 8);
            viewport.Height = (float)((height_ + 1) / 2);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            context_->RSSetViewports(1, &viewport);
            
            // Draw
            context_->Draw(4, 0);
        }
        
        // Wait for completion
        context_->Flush();
    }

    std::tuple<char*, char*, char*> mapResult() {
        HRESULT hr;
        
        // Copy to staging buffers (reuse existing staging buffers)
        context_->CopyResource(stagingYBuffer_, outputYBuffer_);
        context_->CopyResource(stagingUBuffer_, outputUBuffer_);
        context_->CopyResource(stagingVBuffer_, outputVBuffer_);
        
        // Map staging buffers
        D3D11_MAPPED_SUBRESOURCE mappedYResource, mappedUResource, mappedVResource;
        hr = context_->Map(stagingYBuffer_, 0, D3D11_MAP_READ, 0, &mappedYResource);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to map staging Y buffer");
        }
        
        hr = context_->Map(stagingUBuffer_, 0, D3D11_MAP_READ, 0, &mappedUResource);
        if (FAILED(hr)) {
            context_->Unmap(stagingYBuffer_, 0);
            throw std::runtime_error("Failed to map staging U buffer");
        }
        
        hr = context_->Map(stagingVBuffer_, 0, D3D11_MAP_READ, 0, &mappedVResource);
        if (FAILED(hr)) {
            context_->Unmap(stagingYBuffer_, 0);
            context_->Unmap(stagingUBuffer_, 0);
            throw std::runtime_error("Failed to map staging V buffer");
        }
        
        mappedYPtr_ = mappedYResource.pData;
        mappedUPtr_ = mappedUResource.pData;
        mappedVPtr_ = mappedVResource.pData;
        
        return std::make_tuple(static_cast<char*>(mappedYPtr_), static_cast<char*>(mappedUPtr_), static_cast<char*>(mappedVPtr_));
    }

    void unmapResult() {
        // Unmap staging buffers (but don't release them, they will be reused)
        if (mappedYPtr_ && stagingYBuffer_) {
            context_->Unmap(stagingYBuffer_, 0);
            mappedYPtr_ = nullptr;
        }
        if (mappedUPtr_ && stagingUBuffer_) {
            context_->Unmap(stagingUBuffer_, 0);
            mappedUPtr_ = nullptr;
        }
        if (mappedVPtr_ && stagingVBuffer_) {
            context_->Unmap(stagingVBuffer_, 0);
            mappedVPtr_ = nullptr;
        }
    }
    
    CComPtr<ID3D11Buffer> CreateConstantBuffer(UINT byteWidth) {
        D3D11_BUFFER_DESC constantBufferDesc = {};
        constantBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        constantBufferDesc.ByteWidth = byteWidth;
        constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        constantBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        
        CComPtr<ID3D11Buffer> buffer;
        HRESULT hr = device_->CreateBuffer(&constantBufferDesc, nullptr, &buffer);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create constant buffer");
        }
        
        return buffer;
    }
    
    CComPtr<ID3D11Buffer> CreateVertexBuffer(const void* vertices, UINT byteWidth) {
        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
        vertexBufferDesc.ByteWidth = byteWidth;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        
        D3D11_SUBRESOURCE_DATA vertexData = {};
        vertexData.pSysMem = vertices;
        
        CComPtr<ID3D11Buffer> buffer;
        HRESULT hr = device_->CreateBuffer(&vertexBufferDesc, &vertexData, &buffer);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create vertex buffer");
        }
        
        return buffer;
    }
    
    CComPtr<ID3D11Buffer> CreateDynamicBuffer(UINT byteWidth, UINT bindFlags) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = byteWidth;
        bufferDesc.BindFlags = bindFlags;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint32_t);
        
        CComPtr<ID3D11Buffer> buffer;
        HRESULT hr = device_->CreateBuffer(&bufferDesc, nullptr, &buffer);
        if (FAILED(hr)) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Failed to create dynamic buffer. HRESULT: 0x%08X, Size: %d bytes", hr, byteWidth);
            throw std::runtime_error(errorMsg);
        }
        
        return buffer;
    }
    
    // Helper function implementations
    CComPtr<ID3D11Buffer> CreateStructuredBuffer(UINT byteWidth, D3D11_USAGE usage, UINT bindFlags, UINT cpuAccessFlags, const void* initialData) {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = usage;
        bufferDesc.ByteWidth = byteWidth;
        bufferDesc.BindFlags = bindFlags;
        bufferDesc.CPUAccessFlags = cpuAccessFlags;
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bufferDesc.StructureByteStride = sizeof(uint32_t);
        
        D3D11_SUBRESOURCE_DATA* pInitialData = nullptr;
        D3D11_SUBRESOURCE_DATA initialDataDesc = {};
        if (initialData) {
            initialDataDesc.pSysMem = initialData;
            pInitialData = &initialDataDesc;
        }
        
        CComPtr<ID3D11Buffer> buffer;
        HRESULT hr = device_->CreateBuffer(&bufferDesc, pInitialData, &buffer);
        if (FAILED(hr)) {
            char errorMsg[256];
            sprintf_s(errorMsg, "Failed to create structured buffer. HRESULT: 0x%08X, Size: %d bytes", hr, byteWidth);
            throw std::runtime_error(errorMsg);
        }
        
        return buffer;
    }
    
    CComPtr<ID3D11UnorderedAccessView> CreateBufferUAV(ID3D11Buffer* buffer, UINT numElements) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numElements;
        
        CComPtr<ID3D11UnorderedAccessView> uav;
        HRESULT hr = device_->CreateUnorderedAccessView(buffer, &uavDesc, &uav);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create buffer UAV");
        }
        
        return uav;
    }
    
    CComPtr<ID3D11Buffer> CreateStagingBuffer(UINT byteWidth) {
        D3D11_BUFFER_DESC stagingDesc = {};
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.ByteWidth = byteWidth;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        stagingDesc.StructureByteStride = 0;
        
        CComPtr<ID3D11Buffer> buffer;
        HRESULT hr = device_->CreateBuffer(&stagingDesc, nullptr, &buffer);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create staging buffer");
        }
        
        return buffer;
    }
};


#include <chrono>
#include <iostream>

int main() try {
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
            std::cout << "=== Testing OpenGL ES Version ===" << std::endl;
            do_test.template operator()<ColorConvGLES>();

            std::cout << "=== Testing D3D11 Version ===" << std::endl;
            do_test.template operator()<ColorConvD3D11>();
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
