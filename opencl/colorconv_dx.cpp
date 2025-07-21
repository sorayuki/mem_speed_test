#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <atlbase.h>

#include <stdexcept>
#include <chrono>
#include <string>
#include <tuple>
#include <vector>
#include <cstring>
#include <algorithm>
#include <iostream>

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
    discard;
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
            // 方法：使用两个buffer，dynamic用于快速写入，default用于GPU计算
            inputBuffer_ = CreateDynamicBuffer(inputStrideBytes_ * height_, D3D11_BIND_SHADER_RESOURCE);
            inputUAVBuffer_ = CreateStructuredBuffer(inputStrideBytes_ * height_, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        } else {
            // 最快方法：直接使用DEFAULT buffer + UpdateSubresource
            inputBuffer_ = CreateStructuredBuffer(inputStrideBytes_ * height_, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
            inputUAVBuffer_ = inputBuffer_;
        }
        
        // Create output buffers
        outputYBuffer_ = CreateStructuredBuffer(outputYStrideBytes_ * height_, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        outputUBuffer_ = CreateStructuredBuffer(outputUStrideBytes_ * (height_ / 2), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        outputVBuffer_ = CreateStructuredBuffer(outputVStrideBytes_ * (height_ / 2), D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, nullptr);
        
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
        
        // Create depth stencil state - disable depth test and depth write
        D3D11_DEPTH_STENCIL_DESC depthStencilDesc = {};
        depthStencilDesc.DepthEnable = false;                    // 关闭深度测试
        depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;  // 关闭深度写入
        depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;    // 深度测试总是通过（虽然已关闭）
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

    size_t input_copy_bytes = 0;
    using clock = std::chrono::high_resolution_clock;
    clock::duration copy_cost = std::chrono::seconds(0);
    
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

        if (mapInputBuffer_) {
            std::cout << "Write mapped buffer speed: " << (input_copy_bytes / 1048576.0 / (std::chrono::duration_cast<std::chrono::milliseconds>(copy_cost).count() / 1000.0)) << " MB/S" << std::endl;
        }
    }

    void feedInput(char* inputBuffer) {
        HRESULT hr;
        
        // 优化的输入缓冲区更新策略
        if (mapInputBuffer_) {
            // 方法2：使用Map/Unmap + CopyResource（适合频繁更新的数据）
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            hr = context_->Map(inputBuffer_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            if (SUCCEEDED(hr)) {
                auto startTime = clock::now();
                memcpy(mappedResource.pData, inputBuffer, inputStrideBytes_ * height_);
                input_copy_bytes += inputStrideBytes_ * height_;
                copy_cost += clock::now() - startTime;

                context_->Unmap(inputBuffer_, 0);
            }
            
            // 快速复制到UAV buffer
            context_->CopyResource(inputUAVBuffer_, inputBuffer_);
        } else {
            // 方法1：直接UpdateSubresource（通常是最快的）
            context_->UpdateSubresource(inputUAVBuffer_, 0, nullptr, inputBuffer, inputStrideBytes_, 0);
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
        
        context_->CopyResource(stagingYBuffer_, outputYBuffer_);
        context_->CopyResource(stagingUBuffer_, outputUBuffer_);
        context_->CopyResource(stagingVBuffer_, outputVBuffer_);
        
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
        // 传统方式：unmap staging buffers
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


#include "colorconv_runtest.h"
int main() {
    return runtest<ColorConvD3D11>();
}
