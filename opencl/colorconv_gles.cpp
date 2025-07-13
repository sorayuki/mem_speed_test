#include <EGL/egl.h>
#include <GLES3/gl31.h>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <cstring>

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
    
    // Process Y data - 16 pixels total (8 pixels x 2 rows)
    for (int dy = 0; dy < 2 && topLeft.y + dy < height; dy++) {
        // Process 8 pixels per row, write 2 uints (4 bytes each)
        for (int dx = 0; dx < 8; dx += 4) {
            uint yPacked = 0u;
            
            for (int i = 0; i < 4 && topLeft.x + dx + i < width; i++) {
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
                
                // Convert to YUV using vector dot product
                vec3 yuv = rgbaToYuv(rgba);
                
                // Pack Y value
                uint y_val = uint(clamp(yuv.x, 0.0, 255.0));
                yPacked |= (y_val << (i * 8));
            }
            
            // Write Y data (4 bytes at once)
            int yIdx = ((topLeft.y + dy) * outputYStride + topLeft.x + dx) / 4;
            outputYData[yIdx] = yPacked;
        }
    }
    
    // Process U and V data - 4 samples each from 8x2 block (2x2 subsampling)
    uint uPacked = 0u;
    uint vPacked = 0u;
    
    for (int dy = 0; dy < 2; dy += 2) {
        for (int dx = 0; dx < 8; dx += 2) {
            int x = topLeft.x + dx;
            int y = topLeft.y + dy;
            
            if (x < width && y < height) {
                // Get input pixel from top-left of 2x2 block
                int inputIdx = (y * inputStride + x * 4) / 4;
                uint pixel = inputData[inputIdx];
                
                // Unpack RGBA to float vec4
                vec4 rgba = vec4(
                    float((pixel >> 0) & 0xFFu),
                    float((pixel >> 8) & 0xFFu),
                    float((pixel >> 16) & 0xFFu),
                    1.0  // Alpha component for range control
                );
                
                // Convert to YUV using vector dot product
                vec3 yuv = rgbaToYuv(rgba);
                
                // Pack U and V values
                uint u_val = uint(clamp(yuv.y, 0.0, 255.0));
                uint v_val = uint(clamp(yuv.z, 0.0, 255.0));
                
                // Calculate position in packed uint (4 samples from 8x2 -> 4 samples)
                int uvPos = dx / 2;
                uPacked |= (u_val << (uvPos * 8));
                vPacked |= (v_val << (uvPos * 8));
            }
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

layout(local_size_x = 4, local_size_y = 1, local_size_z = 1) in;

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
out vec2 texCoord;

void main() {
    gl_Position = position;
    texCoord = position.xy * 0.5 + 0.5;
}
)";
            
            // Fragment shader
            std::string fragmentShaderSource = R"(#version 310 es
precision highp float;
precision highp int;

in vec2 texCoord;

)" + std::string(sharedShaderCode) + R"(

void main() {
    // Map texture coordinates to 8x2 regions
    ivec2 coord = ivec2(texCoord * vec2(width, height));
    ivec2 topLeft = ivec2((coord.x / 8) * 8, (coord.y / 2) * 2);
    
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
            glViewport(0, 0, width_, height_);
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

#include <chrono>
#include <iostream>
int main() try {
    std::vector<char> inputBuffer(1920 * 1920 * 4); // Example input buffer
    std::vector<char> outputYBuffer(1920 * 1920), outputUBuffer(1920 * 1920 / 4), outputVBuffer(1920 * 1920 / 4);
    for(int i = 0; i < 2; ++i) {
        for(int j = 0; j < 2; ++j) {
            std::cout << "Running test with compute shader: " << (i == 0 ? "Yes" : "No") 
                      << ", map input buffer: " << (j == 0 ? "Yes" : "No") << std::endl;
            ColorConvGLES colorConv(i == 0, j == 0, 1920, 1920, 1920 * 4, 1920, 1920 / 2);

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

                if (std::chrono::steady_clock::now() - beginTime > std::chrono::seconds(15)) {
                    break;
                }
            }
        }
    }

    return 0;
} catch(std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
} catch(...) {
    std::cerr << "Unknown error occurred" << std::endl;
    return 1;
}
