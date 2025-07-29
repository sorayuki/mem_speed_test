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

LegionZone Extreme mode

FPS

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|PerfMode |QuietMode|
|-------|--------|----------|--------|---------------|---------|---------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |724.768  |478.307  |
|Regular|Device  |Yes       |Map     |std            |777.5    |489.579  |
|Regular|Device  |Yes       |R/W Buf |Pipeline       |601.956  |429.907  |
|Regular|Device  |No        |R/W Buf |No pipeline    |426.329  |262.219  |
|Regular|Host    |Yes       |R/W Buf |No pipeline    |745.182  |491.367  |
|Regular|Host    |Yes       |Map     |std            |773.733  |479.095  |
|Regular|Host    |Yes       |R/W Buf |Pipeline       |728.356  |415.948  |
|Regular|Host    |No        |R/W Buf |No pipeline    |425.492  |177.447  |
|Regular|SVM     |Yes       |R/W Buf |No pipeline    |716.631  |409.503  |
|Regular|SVM     |Yes       |Map     |std            |779.102  |402.426  |
|Regular|SVM     |Yes       |R/W Buf |Pipeline       |589.722  |363.526  |
|Regular|SVM     |No        |R/W Buf |No pipeline    |422.772  |178.808  |
|Regular|UseHost |No        |/       |No pipeline    |343.204  |128.057  |
|Aligned|UseHost |No        |/       |No pipeline    |964.658  |389.658  |
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |757.818  |452.541  |
|Pinned |Device  |Yes       |R/W Buf |pipeline       |615.917  |388.271  |
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |768.363  |454.085  |
|Pinned |Host    |Yes       |R/W Buf |pipeline       |615.747  |382.607  |
|Pinned |SVM     |Yes       |R/W Buf |No pipeline    |784.552  |454.228  |
|Pinned |SVM     |Yes       |R/W Buf |pipeline       |618.273  |380.582  |
|Regular|Device  |Yes       |Map     |no copy        |2079.84  |1637.06  |
|Regular|Host    |Yes       |Map     |no copy        |2058.42  |1491.25  |
|Regular|SVM     |Yes       |Map     |no copy        |2094.48  |1430.88  |

* map & no copy is not comparable with others
* perfmode is LegionZone Extreme mode with force max fan speed


## Nvidia RTX 4090 laptop PCIE 4x16

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

LegionZone Extreme mode

FPS

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|PerfMode |QuietMode|
|-------|--------|----------|--------|---------------|---------|---------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |701.549  |280.827  |
|Regular|Device  |Yes       |Map     |std            |521.373  |213.441  |
|Regular|Device  |Yes       |R/W Buf |Pipeline       |688.722  |294.482  |
|Regular|Device  |No        |R/W Buf |No pipeline    |375.036  |166.215  |
|Regular|Host    |Yes       |R/W Buf |No pipeline    |643.846  |332.454  |
|Regular|Host    |Yes       |Map     |std            |511.314  |219.08   |
|Regular|Host    |Yes       |R/W Buf |Pipeline       |674.493  |325.892  |
|Regular|Host    |No        |R/W Buf |No pipeline    |358.85   |161.444  |
|Regular|SVM     |Yes       |R/W Buf |No pipeline    |689.436  |293.443  |
|Regular|SVM     |Yes       |Map     |std            |531.535  |230.062  |
|Regular|SVM     |Yes       |R/W Buf |Pipeline       |720.934  |336.045  |
|Regular|SVM     |No        |R/W Buf |No pipeline    |127.649  |42.988   |
|Regular|UseHost |No        |/       |No pipeline    |277.342  |107.593  |
|Aligned|UseHost |No        |/       |No pipeline    |275.144  |106.94   |
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |1081.47  |558.093  |
|Pinned |Device  |Yes       |R/W Buf |pipeline       |1178.22  |614.473  |
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |1083.96  |560.265  |
|Pinned |Host    |Yes       |R/W Buf |pipeline       |1175.06  |606.322  |
|Pinned |SVM     |Yes       |R/W Buf |No pipeline    |1093.89  |552.448  |
|Pinned |SVM     |Yes       |R/W Buf |pipeline       |1177.21  |614.287  |
|Regular|Device  |Yes       |Map     |no copy        |948.665  |478.627  |
|Regular|Host    |Yes       |Map     |no copy        |960.8    |482.193  |
|Regular|SVM     |Yes       |Map     |no copy        |1060.55  |536.573  |

* map & no copy is not comparable with others
* perfmode is LegionZone Extreme mode with force max fan speed


## Nvidia RTX 2060 desktop PCIE 3x16

CPU: Intel i7-10700
Memory: DDR4 8G*4 2400MHz

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|FPS      |
|-------|--------|----------|--------|---------------|---------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |248.36   |
|Regular|Device  |Yes       |Map     |std            |221.343  |
|Regular|Device  |Yes       |Map     |no copy        |421.702  |
|Regular|Device  |Yes       |R/W Buf |Pipeline       |290.044  |
|Regular|Device  |No        |R/W Buf |No pipeline    |208.179  |
|Regular|Host    |Yes       |R/W Buf |No pipeline    |278.808  |
|Regular|Host    |Yes       |Map     |std            |246.56   |
|Regular|Host    |Yes       |Map     |no copy        |422.303  |
|Regular|Host    |Yes       |R/W Buf |Pipeline       |278.534  |
|Regular|Host    |No        |R/W Buf |No pipeline    |190.629  |
|Regular|SVM     |Yes       |R/W Buf |No pipeline    |278.142  |
|Regular|SVM     |Yes       |Map     |std            |260.873  |
|Regular|SVM     |Yes       |Map     |no copy        |463.717  |
|Regular|SVM     |Yes       |R/W Buf |Pipeline       |287.527  |
|Regular|SVM     |No        |R/W Buf |No pipeline    |96.8681  |
|Regular|UseHost |No        |/       |No pipeline    |159.829  |
|Aligned|UseHost |No        |/       |No pipeline    |155.639  |
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |465.744  |
|Pinned |Device  |Yes       |R/W Buf |pipeline       |505.815  |
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |439.989  |
|Pinned |Host    |Yes       |R/W Buf |pipeline       |496.896  |
|Pinned |SVM     |Yes       |R/W Buf |No pipeline    |467.537  |
|Pinned |SVM     |Yes       |R/W Buf |pipeline       |504.386  |


## RegMagic 10 Pro

破坏神模式

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|FPS      |
|-------|--------|----------|--------|---------------|---------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |461.154|
|Regular|Device  |Yes       |Map     |std            |744.344|
|Regular|Device  |Yes       |Map     |parallel       |669.712|
|Regular|Device  |Yes       |R/W Buf |Pipeline       |403.038|
|Regular|Device  |No        |R/W Buf |No pipeline    |91.4972|
|Regular|Host    |Yes       |R/W Buf |No pipeline    |460.287|
|Regular|Host    |Yes       |Map     |std            |745.735|
|Regular|Host    |Yes       |Map     |parallel       |672.566|
|Regular|Host    |Yes       |R/W Buf |Pipeline       |442.249|
|Regular|Host    |No        |R/W Buf |No pipeline    |92.3241|
|Regular|SVM     |Yes       |R/W Buf |No pipeline    |428.622|
|Regular|SVM     |Yes       |Map     |std            |725.268|
|Regular|SVM     |Yes       |Map     |parallel       |670.854|
|Regular|SVM     |Yes       |R/W Buf |Pipeline       |407.376|
|Regular|SVM     |No        |R/W Buf |No pipeline    |92.3678|
|Regular|UseHost |No        |/       |No pipeline    |96.3795|
|Aligned|UseHost |No        |/       |No pipeline    |95.5658|
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |420.227|
|Pinned |Device  |Yes       |R/W Buf |pipeline       |410.787|
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |437.848|
|Pinned |Host    |Yes       |R/W Buf |pipeline       |407.975|
|Pinned |SVM     |Yes       |R/W Buf |No pipeline    |415.424|
|Pinned |SVM     |Yes       |R/W Buf |pipeline       |408.916|
|Regular|Device  |Yes       |Map     |no copy        |872.52|
|Regular|Host    |Yes       |Map     |no copy        |873.028|
|Regular|SVM     |Yes       |Map     |no copy        |785.551|


## OnePlus 7 Pro

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|FPS      |
|-------|--------|----------|--------|---------------|---------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |47.068|
|Regular|Device  |Yes       |Map     |std            |151.869|
|Regular|Device  |Yes       |Map     |no copy        |136.745|
|Regular|Device  |Yes       |Map     |parallel       |79.6855|
|Regular|Device  |Yes       |R/W Buf |Pipeline       |75.0089|
|Regular|Device  |No        |R/W Buf |No pipeline    |23.1481|
|Regular|Host    |Yes       |R/W Buf |No pipeline    |40.8492|
|Regular|Host    |Yes       |Map     |std            |143.518|
|Regular|Host    |Yes       |Map     |no copy        |167.758|
|Regular|Host    |Yes       |Map     |parallel       |91.8135|
|Regular|Host    |Yes       |R/W Buf |Pipeline       |40.9184|
|Regular|Host    |No        |R/W Buf |No pipeline    |26.6077|
|Regular|SVM     |Yes       |R/W Buf |No pipeline    |37.0971|
|Regular|SVM     |Yes       |Map     |std            |81.666|
|Regular|SVM     |Yes       |Map     |no copy        |64.9553|
|Regular|SVM     |Yes       |Map     |parallel       |55.0997|
|Regular|SVM     |Yes       |R/W Buf |Pipeline       |90.5283|
|Regular|SVM     |No        |R/W Buf |No pipeline    |25.1285|
|Regular|UseHost |No        |/       |No pipeline    |33.8554|
|Aligned|UseHost |No        |/       |No pipeline    |32.563|
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |41.4993|
|Pinned |Device  |Yes       |R/W Buf |pipeline       |70.9536|
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |41.6637|
|Pinned |Host    |Yes       |R/W Buf |pipeline       |70.1617|
|Pinned |SVM     |Yes       |R/W Buf |No pipeline    |37.0344|
|Pinned |SVM     |Yes       |R/W Buf |pipeline       |62.624|


## Oppo Find X8 s

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|FPS      |
|-------|--------|----------|--------|---------------|---------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |76.5628|
|Regular|Device  |Yes       |Map     |std            |70.6741|
|Regular|Device  |Yes       |Map     |no copy        |104.882|
|Regular|Device  |Yes       |Map     |parallel       |79.009|
|Regular|Device  |Yes       |R/W Buf |Pipeline       |132.944|
|Regular|Device  |No        |R/W Buf |No pipeline    |82.2102|
|Regular|Host    |Yes       |R/W Buf |No pipeline    |76.5136|
|Regular|Host    |Yes       |Map     |std            |73.6431|
|Regular|Host    |Yes       |Map     |no copy        |102.225|
|Regular|Host    |Yes       |Map     |parallel       |78.4508|
|Regular|Host    |Yes       |R/W Buf |Pipeline       |70.3585|
|Regular|Host    |No        |R/W Buf |No pipeline    |80.7242|
|Regular|SVM     |Yes       |R/W Buf |No pipeline    |74.4212|
|Regular|SVM     |Yes       |Map     |std            |71.0944|
|Regular|SVM     |Yes       |Map     |no copy        |96.8384|
|Regular|SVM     |Yes       |Map     |parallel       |78.6054|
|Regular|SVM     |Yes       |R/W Buf |Pipeline       |139.614|
|Regular|SVM     |No        |R/W Buf |No pipeline    |74.4311|
|Regular|UseHost |No        |/       |No pipeline    |86.0452|
|Aligned|UseHost |No        |/       |No pipeline    |101.001|
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |77.2182|
|Pinned |Device  |Yes       |R/W Buf |pipeline       |125.347|
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |74.4048|
|Pinned |Host    |Yes       |R/W Buf |pipeline       |127.705|
|Pinned |SVM     |Yes       |R/W Buf |No pipeline    |74.2922|
|Pinned |SVM     |Yes       |R/W Buf |pipeline       |123.843|


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


Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

LegionZone Extreme mode

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|FPS    |
|-------|--------|----------|--------|---------------|-------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |80.1079|
|Regular|Device  |Yes       |Map     |std            |67.1774|
|Regular|Device  |Yes       |Map     |parallel       |69.7015|
|Regular|Device  |Yes       |R/W Buf |Pipeline       |79.3718|
|Regular|Device  |No        |R/W Buf |No pipeline    |71.6365|
|Regular|Host    |Yes       |R/W Buf |No pipeline    |42.2624|
|Regular|Host    |Yes       |Map     |std            |46.4361|
|Regular|Host    |Yes       |Map     |parallel       |73.7193|
|Regular|Host    |Yes       |R/W Buf |Pipeline       |45.4963|
|Regular|Host    |No        |R/W Buf |No pipeline    |40.8552|
|Regular|UseHost |No        |/       |No pipeline    |51.4794|
|Aligned|UseHost |No        |/       |No pipeline    |51.2948|
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |17.7425|
|Pinned |Device  |Yes       |R/W Buf |pipeline       |17.6574|
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |15.2916|
|Pinned |Host    |Yes       |R/W Buf |pipeline       |17.269 |
|Regular|Device  |Yes       |Map     |no copy        |73.4595|
|Regular|Host    |Yes       |Map     |no copy        |106.412|


## OpenCL on D3D12: Nvidia RTX 4090 laptop PCIE 4x16

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

LegionZone Extreme mode

|HostMem|clBuf   |ReuseClBuf|CopyMode|Memcpy/Pipeline|FPS    |
|-------|--------|----------|--------|---------------|-------|
|Regular|Device  |Yes       |R/W Buf |No pipeline    |400.714|
|Regular|Device  |Yes       |Map     |std            |224.019|
|Regular|Device  |Yes       |Map     |parallel       |256.754|
|Regular|Device  |Yes       |R/W Buf |Pipeline       |412.957|
|Regular|Device  |No        |R/W Buf |No pipeline    |229.268|
|Regular|Host    |Yes       |R/W Buf |No pipeline    |39.0526|
|Regular|Host    |Yes       |Map     |std            |35.8255|
|Regular|Host    |Yes       |Map     |parallel       |75.1597|
|Regular|Host    |Yes       |R/W Buf |Pipeline       |38.8418|
|Regular|Host    |No        |R/W Buf |No pipeline    |36.7921|
|Regular|UseHost |No        |/       |No pipeline    |86.7528|
|Aligned|UseHost |No        |/       |No pipeline    |81.2017|
|Pinned |Device  |Yes       |R/W Buf |No pipeline    |17.9674|
|Pinned |Device  |Yes       |R/W Buf |pipeline       |17.7706|
|Pinned |Host    |Yes       |R/W Buf |No pipeline    |11.336 |
|Pinned |Host    |Yes       |R/W Buf |pipeline       |14.655 |
|Regular|Device  |Yes       |Map     |no copy        |299.986|
|Regular|Host    |Yes       |Map     |no copy        |87.8465|


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
