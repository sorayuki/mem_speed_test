# OpenCL convert 1920x1920 BGRX32 to YUV420P synchronously

Shows framerate (FPS)

The test runs only once for 15s for each. May not accurate.

# Explain

## HostMem: memory block in host side.  
+ Regular: allocated by ```new```
+ Aligned: allocated by ```_aligned_malloc```, aligned to page boundary and CL_DEVICE_MEM_BASE_ADDR_ALIGN
+ Pinned: allocated by ```clCreateBuffer``` with CL_ALLOC_HOST_PTR and mapped

## clBuf: clBuffer which used as kernel parameters

+ Regular: ```clCreateBuffer``` with only read/write flags
+ HostAlloc: ```clCreateBuffer``` with CL_ALLOC_HOST_PTR
+ UseHost: ```clCreateBuffer``` with CL_USE_HOST_PTR

## Method: the method transport data between HostMem and clBuf  
+ R/WBuf: use ```clEnqueueReadBuffer``` and ```clEnqueueWriteBuffer``` API
+ MapCpy: use ```clEnqueueMapBuffer``` -> ```memcpy``` -> ```clEnqueueUnmapMemoryObject``` or ```clEnqueueSvmmap``` -> ```memcpy``` -> ```clEnqueueSVMUnmap```
+ SvmCpy: use ```clEnqueueSvmMemcpy```
+ Map: use ```clEnqueueMapBuffer``` -> ```clEnqueueUnmapMemoryObject``` or ```clEnqueueSvmmap``` -> ```clEnqueueSVMUnmap``` (the case that caller can use OpenCL's buffer directly)  

```
// Map but no memcpy case example
class CLProcessor {
public:
    void* mapInputBuffer(size_t size);
    void unmapInputBuffer();
    int Invoke();
    std::tuple<void*, size_t> mapOutputBuffer();
    void unmapOutputBuffer();
};

// Other case example
class CLProcessor {
public:
    int Invoke(const void* input, size_t inputSize, void* output, size_t outputSize);
};
```

# Test Result For OpenCL
## Intel UHD Graphics (i9-13900HX)

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

Run in performance mode.
Sync mode (not pipelined)

|HostMem|clBuf    |Method|ReuseClBuf |ReuseClBuf |NoReuse    |NoReuse    |
|-------|---------|------|-----------|-----------|-----------|-----------|
|       |         |      |Performance|Quiet      |Performance|Quiet      |
|Regular|Regular  |R/WBuf|696.124    |418.515    |406.763    |           |
|Regular|Regular  |MapCpy|675.564    |380.606    |401.541    |           |
|Regular|SVM      |SvmCpy|700.021    |402.168    |405.251    |           |
|Regular|SVM      |MapCpy|765.042    |365.445    |396.376    |           |
|Regular|HostAlloc|R/WBuf|714.867    |399.886    |409.717    |           |
|Regular|HostAlloc|MapCpy|713.011    |378.29     |382.909    |           |
|Regular|UseHost  |/     |/          |/          |345.01     |127.319    |
|Aligned|Regular  |R/WBuf|732.834    |426.259    |448.527    |           |
|Aligned|UseHost  |/     |/          |/          |941.101    |364.323    |
|Aligned|SVM      |SvmCpy|787.509    |443.993    |436.746    |           |
|Pinned |Regular  |R/WBuf|781.758    |437.705    |433.105    |           |
|Pinned |HostAlloc|R/WBuf|792.734    |441.432    |431.588    |           |
|Pinned |SVM      |SvmCpy|792.734    |433.096    |440.679    |           |
|Regular|Regular  |Map   |2004.07    |           |580.949    |           |
|Regular|SVM      |Map   |2052.12    |           |590.909    |           |
|Regular|HostAlloc|Map   |2063.19    |           |589.251    |           |

## Nvidia RTX 4090 laptop

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

Run in performance mode.
Sync mode (not pipelined)

|HostMem|clBuf    |Method|ReuseClBuf |ReuseClBuf |NoReuse    |
|-------|---------|------|-----------|-----------|-----------|
|       |         |      |Performance|Quiet      |Performance|
|Regular|Regular  |R/WBuf|664.03     |383.381    |380.05     |
|Regular|Regular  |MapCpy|501.50     |250.837    |151.15     |
|Regular|SVM      |SvmCpy|667.31     |309.502    |126.91     |
|Regular|SVM      |MapCpy|529.61     |212.052    |119.39     |
|Regular|HostAlloc|R/WBuf|669.14     |272.954    |406.08     |
|Regular|HostAlloc|MapCpy|508.60     |187.856    |157.93     |
|Regular|UseHost  |/     |/          |/          |279.604    |
|Aligned|Regular  |R/WBuf|669.95     |285.887    |388.79     |
|Aligned|UseHost  |/     |/          |/          |291.224    |
|Aligned|SVM      |SvmCpy|700.66     |287.374    |132.62     |
|Pinned |Regular  |R/WBuf|1082.74    |532.563    |577.17     |
|Pinned |HostAlloc|R/WBuf|1080.81    |523.973    |581.04     |
|Pinned |SVM      |SvmCpy|1075.46    |529.542    |151.26     |
|Regular|Regular  |Map   |960.02     |           |160.34     |
|Regular|SVM      |Map   |1058.47    |           |141.33     |
|Regular|HostAlloc|Map   |956.24     |           |163.18     |


## Nvidia RTX 2060 desktop

(collected at cde46e404a2cb8e11d2508e155e816469a12d538)

Memory: DDR4 8G*4 2400MHz
pipelined

|HostMem|clBuf    |Method|ReuseClBuf |NoReuse    |
|-------|---------|------|-----------|-----------|
|Regular|Regular  |R/WBuf|251.48     |185.75     |
|Regular|Regular  |MapCpy|214.73     |82.16      |
|Regular|SVM      |SvmCpy|263.27     |82.8       |
|Regular|SVM      |MapCpy|232.78     |82.97      |
|Regular|HostAlloc|R/WBuf|256.43     |200.29     |
|Regular|HostAlloc|MapCpy|225.84     |84.34      |
|Regular|UseHost  |/     |           |           |
|Aligned|Regular  |R/WBuf|270.58     |199.5      |
|Aligned|UseHost  |/     |           |           |
|Aligned|SVM      |SvmCpy|265.43     |84.86      |
|Pinned |Regular  |R/WBuf|497.86     |297.63     |
|Pinned |HostAlloc|R/WBuf|499.14     |304.42     |
|Pinned |SVM      |SvmCpy|495.37     |104.51     |
|Regular|Regular  |Map   |441.54     |107.29     |
|Regular|SVM      |Map   |464.76     |104.27     |
|Regular|HostAlloc|Map   |437.69     |108.04     |


## AMD Vega64 Desktop

(Outdated, collected before 7da2afe06bc8e7e4a52b23bd2bf151e195349ba1)

not my computer

|HostMem|clBuf    |Method|ReuseClBuf|NoReuse    |
|-------|---------|------|----------|-----------|
|Regular|Regular  |R/WBuf|122.6     |122.2      |
|Regular|Regular  |MapCpy|119.9     |120.3      |
|Regular|Regular  |Map   |          |           |
|Regular|SVM      |SvmCpy|123       |72.1       |
|Regular|SVM      |MapCpy|120       |45.6       |
|Regular|SVM      |Map   |          |           |
|Regular|HostAlloc|R/WBuf|122.7     |122.3      |
|Regular|HostAlloc|MapCpy|120       |120.1      |
|Regular|HostAlloc|Map   |          |           |
|Regular|UseHost  |/     |/         |132.9      |
|Aligned|Regular  |R/WBuf|123       |122.3      |
|Aligned|UseHost  |/     |/         |134.3      |
|Pinned |Regular  |R/WBuf|143.4     |143.1      |
|Pinned |HostAlloc|R/WBuf|143.2     |142.9      |
* Some data not tested

## OpenCL on D3D12: Intel UHD Graphics (i9-13900HX)

(Outdated, collected before 7da2afe06bc8e7e4a52b23bd2bf151e195349ba1)

Memory: DDR5 Single-Rank 16GB*2 5600MHz

|HostMem|clBuf    |Method|ReuseClBuf|ReuseClBuf |NoReuse    |NoReuse    |
|-------|---------|------|----------|-----------|-----------|-----------|
|       |         |      |Quite     |Performance|Quite      |Performance|
|Regular|Regular  |R/WBuf|63.9      |           |54.1       |           |
|Regular|Regular  |MapCpy|61        |           |49.7       |           |
|Regular|Regular  |Map   |          |           |           |           |
|Regular|SVM      |SvmCpy|/         |/          |/          |/          |
|Regular|SVM      |MapCpy|/         |/          |/          |/          |
|Regular|SVM      |Map   |/         |/          |/          |/          |
|Regular|HostAlloc|R/WBuf|61.7      |           |55         |           |
|Regular|HostAlloc|MapCpy|59.5      |           |50.7       |           |
|Regular|HostAlloc|Map   |          |           |           |           |
|Regular|UseHost  |/     |/         |/          |36.2       |           |
|Aligned|Regular  |R/WBuf|64.2      |           |56         |           |
|Aligned|UseHost  |/     |/         |/          |36.3       |           |
|Pinned |Regular  |R/WBuf|10.7      |           |10.6       |           |
|Pinned |HostAlloc|R/WBuf|10.9      |           |10.7       |           |

## OpenCL on D3D12: Nvidia RTX 4090 laptop

(Outdated, collected before 7da2afe06bc8e7e4a52b23bd2bf151e195349ba1)

Memory: DDR5 Single-Rank 16GB*2 5600MHz

|HostMem|clBuf    |Method|ReuseClBuf|ReuseClBuf |NoReuse    |NoReuse    |
|-------|---------|------|----------|-----------|-----------|-----------|
|       |         |      |Quite     |Performance|Quite      |Performance|
|Regular|Regular  |R/WBuf|184.4     |           |121.9      |           |
|Regular|Regular  |MapCpy|152.4     |           |111.4      |           |
|Regular|Regular  |Map   |211.1     |           |139.9      |           |
|Regular|SVM      |SvmCpy|/         |/          |/          |/          |
|Regular|SVM      |MapCpy|/         |/          |/          |/          |
|Regular|SVM      |Map   |/         |/          |/          |/          |
|Regular|HostAlloc|R/WBuf|171.2     |           |98.4*      |           |
|Regular|HostAlloc|MapCpy|146.4     |           |109.3      |           |
|Regular|HostAlloc|Map   |222.1     |           |139.8      |           |
|Regular|UseHost  |/     |/         |/          |53.7*      |           |
|Aligned|Regular  |R/WBuf|168.9     |           |99.1*      |           |
|Aligned|UseHost  |/     |/         |/          |39.3*      |           |
|Pinned |Regular  |R/WBuf|11.1      |           |11.1       |           |
|Pinned |HostAlloc|R/WBuf|11.4      |           |11         |           |


# Test Result For libANGLE / D3D11

Host to device transfer may incorrect in map mode, because ```nvidia-smi dmon -s puct``` shows little host to device transfer.

## OpenGL ES via libANGLE / RAW D3D11: Intel UHD Graphics (i9-13900HX)

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

Reuse Shader Storage Buffer Object

run 15 seconds, performance mode, max fan speed

Optimized: try zero copy
Not optimized: always use fallback

|InputBuffer   |Shader Type|Angle |D3D11 |D3D11 Optimized|
|--------------|-----------|------|------|---------------|
|MapBufferRange|Compute    |115.89|173.94|276.55         |
|BufferSubData |Compute    |78.93 |29.12 |31.73          |
|MapBufferRange|Render     |151.81|226.44|489.58         |
|BufferSubData |Render     |95.27 |31.76 |33.49          |


## OpenGL ES via libANGLE / RAW D3D11: Nvidia RTX 4090 laptop

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

Reuse Shader Storage Buffer Object

run 15 seconds, performance mode, max fan speed

|InputBuffer   |Shader Type|Angle |D3D11 |D3D11 Optimized|
|--------------|-----------|------|------|---------------|
|MapBufferRange|Compute    |310.48|418   |1112.59        |
|BufferSubData |Compute    |137.52|421.80|423.38         |
|MapBufferRange|Render     |343.24|469.65|1418.1         |
|BufferSubData |Render     |142.70|464.89|469.05         |


## OpenGL ES via libANGLE / RAW D3D11: Nvidia RTX 2060 desktop

Memory: DDR4 8G*4 2400MHz

Reuse Shader Storage Buffer Object

|InputBuffer   |Shader Type|Angle |D3D11 Optimized|
|--------------|-----------|------|---------------|
|MapBufferRange|Compute    |70.29 |450.3          |
|BufferSubData |Compute    |68.02 |202.37         |
|MapBufferRange|Render     |71.92 |693.2          |
|BufferSubData |Render     |78.29 |233.25         |

