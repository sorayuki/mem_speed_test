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

## OpenGL ES via libANGLE / RAW D3D11: Intel UHD Graphics (i9-13900HX)

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

Reuse Shader Storage Buffer Object

run 10 seconds, performance mode, max fan speed

|InputBuffer   |Shader Type|Angle  |D3D11  |
|--------------|-----------|-------|-------|
|MapBufferRange|Compute    |129.611|198.006|
|BufferSubData |Compute    |86.5002|32.1769|
|MapBufferRange|Render     |168.126|248.974|
|BufferSubData |Render     |103.117|33.8445|

+ ANGLE Render Error distance: 0
+ D3D11 Render Error distance: 86342

## OpenGL ES via libANGLE / RAW D3D11: Nvidia RTX 4090 laptop

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

Reuse Shader Storage Buffer Object

run 10 seconds, performance mode, max fan speed

|InputBuffer   |Shader Type|Angle  |D3D11  |
|--------------|-----------|-------|-------|
|MapBufferRange|Compute    |309.458|413.403|
|BufferSubData |Compute    |144.533|416.399|
|MapBufferRange|Render     |342.873|444.038|
|BufferSubData |Render     |160.089|447.897|

+ ANGLE Render Error distance: 0
+ D3D11 Render Error distance: 86342

Conclusion: The bottleneck in the test of D3D11 is memcpy.


## OpenGL ES via libANGLE / RAW D3D11: Nvidia RTX 2060 desktop

Memory: DDR4 8G*4 2400MHz

Reuse Shader Storage Buffer Object

|InputBuffer   |Shader Type|Angle |D3D11  |
|--------------|-----------|------|-------|
|MapBufferRange|Compute    |64.364|206.495|
|BufferSubData |Compute    |75.526|196.678|
|MapBufferRange|Render     |85.336|233.463|
|BufferSubData |Render     |83.813|238.692|

# Test Result for Native OpenGL ES

Memcpy: MB/S

**May not use up GPU Power, two instance may double the speed.**

**CPU Frequency may not run in max possibility.**

## Xiaomi 15

Vendor: Qualcomm

Renderer: Adreno (TM) 830

|InputBuffer   |Shader Type|FPS   |In Memcpy|Out Memcpy|
|--------------|-----------|------|---------|----------|
|MapBufferRange|Compute    |63.224|6646.19  |6604.9    |
|BufferSubData |Compute    |60.325|         |5482.2    |
|MapBufferRange|Render     |252.94|8154.76  |8222.38   |
|BufferSubData |Render     |243.82|         |7840.81   |


## Xiaomi 11 Ultra

Vendor: Qualcomm

Renderer: Adreno (TM) 660

|InputBuffer   |Shader Type|FPS   |In Memcpy|Out Memcpy|
|--------------|-----------|------|---------|----------|
|MapBufferRange|Compute    |18.978|5595.39  |277.163   |
|BufferSubData |Compute    |17.690|         |236.184   |
|MapBufferRange|Render     |80.225|6631.7   |811.221   |
|BufferSubData |Render     |81.520|         |823.722   |


## RedMagic 8 Pro

Vendor: Qualcomm

Renderer: Adreno (TM) 740

破坏神模式

|InputBuffer   |Shader Type|FPS   |In Memcpy|Out Memcpy|
|--------------|-----------|------|---------|----------|
|MapBufferRange|Compute    |346.47|14445.4  |14929.9   |
|BufferSubData |Compute    |350.71|         |14674.5   |
|MapBufferRange|Render     |386.79|14306.7  |14688.1   |
|BufferSubData |Render     |386.11|         |14717.5   |


## RedMagic 10 Pro

Vendor: Qualcomm

Renderer: Adreno (TM) 830

破坏神模式

|InputBuffer   |Shader Type|FPS   |In Memcpy|Out Memcpy|
|--------------|-----------|------|---------|----------|
|MapBufferRange|Compute    |204.90|28346.2  |33469.2   |
|BufferSubData |Compute    |203.48|         |34319.5   |
|MapBufferRange|Render     |799.56|34643.4  |36895.6   |
|BufferSubData |Render     |801.22|         |37029.5   |


不开破坏神模式

|InputBuffer   |Shader Type|FPS   |In Memcpy|Out Memcpy|
|--------------|-----------|------|---------|----------|
|MapBufferRange|Compute    |57.468|4995.21  |4932.67   |
|BufferSubData |Compute    |58.480|         |4849.23   |
|MapBufferRange|Render     |220.3 |7687.73  |7698.65   |
|BufferSubData |Render     |209.45|         |7374.38   |


## OnePlus 7 Pro

Vendor: Qualcomm

Renderer: Adreno (TM) 640

|InputBuffer   |Shader Type|FPS   |In Memcpy|Out Memcpy|
|--------------|-----------|------|---------|----------|
|MapBufferRange|Compute    |11.814|5164.93  |189.018   |
|BufferSubData |Compute    |10.317|         |138.529   |
|MapBufferRange|Render     |38.237|4549.28  |308.266   |
|BufferSubData |Render     |44.469|         |376.976   |


## Oppo Find X8 s

Vendor: ARM

Renderer: Mali-G925-Immortalis MC12

|InputBuffer   |Shader Type|FPS   |In Memcpy|Out Memcpy|
|--------------|-----------|------|---------|----------|
|MapBufferRange|Compute    |79.383|7998.11  |831.042   |
|BufferSubData |Compute    |79.868|         |809.325   |
|MapBufferRange|Render     |66.049|7647.21  |710.031   |
|BufferSubData |Render     |68.985|         |756.706   |
