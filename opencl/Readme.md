# OpenCL convert 1920x1920 BGRX32 to YUV420P synchronously

Shows framerate (FPS).  
Tested in energy-saving mode.  
The test runs only once for 15s for each. May not accurate.  

||||Intel|13900HX UHD|Nvidia|4090 laptop|
|-|-|-|-|-|-|-|
|HostMem|clBuf    |Method|ReuseClBuf|NoReuse|ReuseClBuf|NoReuse|
|Regular|Regular  |R/WBuf|305.3 |220.7|286.7|229  |
|Regular|Regular  |MapCpy|297.7 |230  |54.7 |37.25|
|Regular|Regular  |Map   |489.2 |363.6|69.1 |44   |
|Regular|SVM      |SvmCpy|363.8 |206.6|472.1|51   |
|Regular|SVM      |MapCpy|504.6 |239.1|331.9|48.6 |
|Regular|SVM      |Map   |1765.9|360.1|679.5|61.9 |
|Regular|HostAlloc|R/WBuf|296.4 |217.1|292.9|226.4|
|Regular|HostAlloc|MapCpy|297.2 |240.2|47.5 |34.8 |
|Regular|HostAlloc|Map   |475.3 |357.6|57.9 |35.8 |
|Regular|UseHost  |      |      |195  |     |141  |
|Aligned|Regular  |R/WBuf|300.6 |224.7|282.3|225.3|
|Aligned|UseHost  |      |      |528  |     |146  |
|Pinned |Regular  |R/WBuf|294.4 |221.6|380.5|328.8|
|Pinned |HostAlloc|R/WBuf|300.1 |230.6|381  |332  |

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