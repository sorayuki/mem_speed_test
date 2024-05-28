# OpenCL convert 1920x1920 BGRX32 to YUV420P synchronously

Shows framerate (FPS)

The test runs only once for 15s for each. May not accurate.

Memory: DDR5 Single-Rank 16GB*2 5600MHz 

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
|HostMem|clBuf    |Method|ReuseClBuf|ReuseClBuf |NoReuse    |NoReuse    |
|-------|---------|------|----------|-----------|-----------|-----------|
|       |         |      |Quite     |Performance|Quite      |Performance|
|Regular|Regular  |R/WBuf|305.3     |494        |220.7      |412.1      |
|Regular|Regular  |MapCpy|297.7     |490.1      |230        |421        |
|Regular|Regular  |Map   |489.2     |806.5      |363.6      |606.7      |
|Regular|SVM      |SvmCpy|363.8     |544.6      |206.6      |348.2      |
|Regular|SVM      |MapCpy|504.6     |758.4      |239.1      |427        |
|Regular|SVM      |Map   |1765.9    |2123.5     |360.1      |598.7      |
|Regular|HostAlloc|R/WBuf|296.4     |489.9      |217.1      |409        |
|Regular|HostAlloc|MapCpy|297.2     |485.2      |240.2      |422        |
|Regular|HostAlloc|Map   |475.3     |800.7      |357.6      |604.9      |
|Regular|UseHost  |/     |/         |/          |195        |368.4      |
|Aligned|Regular  |R/WBuf|300.6     |497.4      |224.7      |412.2      |
|Aligned|UseHost  |/     |/         |/          |528        |963.4      |
|Pinned |Regular  |R/WBuf|294.4     |484.1      |221.6      |399.3      |
|Pinned |HostAlloc|R/WBuf|300.1     |479.5      |230.6      |402.9      |

## Nvidia RTX 4090 laptop
|HostMem|clBuf    |Method|ReuseClBuf|ReuseClBuf |NoReuse    |NoReuse    |
|-------|---------|------|----------|-----------|-----------|-----------|
|       |         |      |Quite     |Performance|Quite      |Performance|
|Regular|Regular  |R/WBuf|286.7     |474.1      |229        |401.9      |
|Regular|Regular  |MapCpy|54.7      |124.8*     |37.25      |81.8*      |
|Regular|Regular  |Map   |69.1      |140.8      |44         |101*       |
|Regular|SVM      |SvmCpy|472.1     |694.7      |51         |96.6*      |
|Regular|SVM      |MapCpy|331.9     |496.4      |48.6       |110.8*     |
|Regular|SVM      |Map   |679.5     |762.6      |61.9       |132.4      |
|Regular|HostAlloc|R/WBuf|292.9     |485.9      |226.4      |402        |
|Regular|HostAlloc|MapCpy|47.5      |123*       |34.8       |84.7       |
|Regular|HostAlloc|Map   |57.9      |138.6*     |35.8       |90.7*      |
|Regular|UseHost  |/     |/         |/          |141        |256.9      |
|Aligned|Regular  |R/WBuf|282.3     |482.4      |225.3      |373.2      |
|Aligned|UseHost  |/     |/         |/          |146        |260.7      |
|Pinned |Regular  |R/WBuf|380.5     |479.8*     |328.8      |486.2      |
|Pinned |HostAlloc|R/WBuf|381       |467.4      |332        |487.4      |
* In performance mode, the laptop will balance the power between CPU and GPU. So the FPS keep changing during test.

## AMD Vega64 Desktop
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

