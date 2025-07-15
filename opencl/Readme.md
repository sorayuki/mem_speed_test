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

# Test Result
## Intel UHD Graphics (i9-13900HX)

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

|HostMem|clBuf    |Method|ReuseClBuf|ReuseClBuf |NoReuse    |NoReuse    |
|-------|---------|------|----------|-----------|-----------|-----------|
|       |         |      |Quite     |Performance|Quite      |Performance|
|Regular|Regular  |R/WBuf|303.7     |462        |252        |400.8      |
|Regular|Regular  |MapCpy|290       |432        |248        |378.5      |
|Regular|SVM      |SvmCpy|313.8     |443        |195        |307.6      |
|Regular|SVM      |MapCpy|475       |620        |250        |383.8      |
|Regular|HostAlloc|R/WBuf|303       |460        |253        |397.4      |
|Regular|HostAlloc|MapCpy|290       |431        |245        |372.5      |
|Regular|UseHost  |/     |/         |/          |230        |355        |
|Aligned|Regular  |R/WBuf|313       |468        |256        |392        |
|Aligned|UseHost  |/     |/         |/          |584        |917        |
|Aligned|SVM      |SvmCpy|332       |477        |206        |321        |
|Pinned |Regular  |R/WBuf|304.3     |465        |256        |386        |
|Pinned |HostAlloc|R/WBuf|298       |462        |256        |383.5      |
|Pinned |SVM      |SvmCpy|323       |463.7      |200        |296        |
|Regular|Regular  |Map   |482       |           |372        |           |
|Regular|SVM      |Map   |1747      |           |377        |           |
|Regular|HostAlloc|Map   |483       |           |374        |           |

## Nvidia RTX 4090 laptop

Memory: DDR5 Double-Rank 48GB*2 5600MHz run at 5200MHz

|HostMem|clBuf    |Method|ReuseClBuf|ReuseClBuf |NoReuse    |NoReuse    |
|-------|---------|------|----------|-----------|-----------|-----------|
|       |         |      |Quite     |Performance|Quite      |Performance|
|Regular|Regular  |R/WBuf|286.3     |445.6      |220        |349        |
|Regular|Regular  |MapCpy|80*       |130.8*     |54*        |96.4       |
|Regular|SVM      |SvmCpy|441.5     |659        |54*        |93.5*      |
|Regular|SVM      |MapCpy|313.8     |433.6      |58*        |100*       |
|Regular|HostAlloc|R/WBuf|300*      |428.9      |225        |346.4      |
|Regular|HostAlloc|MapCpy|79*       |131*       |54*        |94.5*      |
|Regular|UseHost  |/     |/         |/          |150        |250        |
|Aligned|Regular  |R/WBuf|290*      |426.5      |220        |345.9      |
|Aligned|UseHost  |/     |/         |/          |152        |241.7      |
|Aligned|SVM      |SvmCpy|465*      |654.6      |54*        |95*        |
|Pinned |Regular  |R/WBuf|371*      |479        |320        |446.3      |
|Pinned |HostAlloc|R/WBuf|366       |499.4      |321        |440        |
|Pinned |SVM      |SvmCpy|722       |802.7      |65*        |121*       |
|Regular|Regular  |Map   |          |131*       |           |97.4*      |
|Regular|SVM      |Map   |          |743        |           |114*       |
|Regular|HostAlloc|Map   |          |135*       |           |97*        |

* The laptop will hit power limit when doing memcpy. The result selected before it slows down.

## AMD Vega64 Desktop

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


## OpenGL ES via libANGLE / RAW D3D11: Intel UHD Graphics (i9-13900HX)

Reuse Shader Storage Buffer Object

run 15 seconds, performance mode, max fan speed

|InputBuffer   |Shader Type|Angle |D3D11 |
|--------------|-----------|------|------|
|MapBufferRange|Compute    |115.89|173.94|
|BufferSubData |Compute    |78.93 |29.12 |
|MapBufferRange|Render     |151.81|226.44|
|BufferSubData |Render     |95.27 |31.76 |


## OpenGL ES via libANGLE / RAW D3D11: Nvidia RTX 4090 laptop

Reuse Shader Storage Buffer Object

run 15 seconds, performance mode, max fan speed

|InputBuffer   |Shader Type|Angle |D3D11 |
|--------------|-----------|------|------|
|MapBufferRange|Compute    |310.48|418   |
|BufferSubData |Compute    |137.52|421.80|
|MapBufferRange|Render     |343.24|469.65|
|BufferSubData |Render     |142.70|464.89|

