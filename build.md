---
layout: default
title: "Build"
---

## Intel

```
-Xpreprocessor -fopenmp -mavx2 -mfma
```

## Apple Silicon
 
``` 
-Xpreprocessor -fopenmp -march=native
```

## Intel

```
cmake -S . -B build ^
  -DCMAKE_SYSTEM_NAME=Windows ^
  -DCMAKE_SYSTEM_PROCESSOR=AMD64 ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DENABLE_CPU_DISPATCH=ON ^
  -DWITH_MKL=OFF ^
  -DOPENMP_RUNTIME=COMP ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" ^
  -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
  -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
  -DCMAKE_CXX_FLAGS="/MT /EHsc /utf-8" ^
  -DCMAKE_C_FLAGS="/MT /utf-8" ^
  -DCMAKE_POLICY_VERSION_MINIMUM="3.5" 
```

## Windows (Intel MKL, reference build)

```
git clone https://github.com/OpenNMT/CTranslate2.git --recursive
```

```
cmake -S . -B build ^
  -DCMAKE_SYSTEM_NAME=Windows ^
  -DCMAKE_SYSTEM_PROCESSOR=AMD64 ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DENABLE_CPU_DISPATCH=ON ^
  -DWITH_MKL=ON ^
  -DOPENMP_RUNTIME=INTEL ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" ^
  -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
  -DCMAKE_C_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
  -DCMAKE_CXX_FLAGS="/MT /EHsc /utf-8" ^
  -DCMAKE_C_FLAGS="/MT /utf-8" ^
  -DCMAKE_POLICY_VERSION_MINIMUM="3.5" 
```

## Windows ARM

* CMakeLists.txt

```
set(CMAKE_SYSTEM_PROCESSOR "ARM64")
```

```
rmdir /S /Q build
cmake -S . -B build ^
  -DCMAKE_SYSTEM_NAME=Windows ^
  -DCMAKE_SYSTEM_PROCESSOR=ARM64 ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DENABLE_CPU_DISPATCH=OFF ^
  -DWITH_MKL=OFF ^
  -DOPENMP_RUNTIME=COMP ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" ^
  -DCMAKE_CXX_FLAGS="/EHsc /utf-8 /D CT2_ARM64_BUILD /D __aarch64__" ^
  -DCMAKE_C_FLAGS="/utf-8 /D CT2_ARM64_BUILD /D __aarch64__"
```

 ## JsonCpp

```
cmake -S . -B build ^
  -DCMAKE_SYSTEM_NAME=Windows ^
  -DCMAKE_SYSTEM_PROCESSOR=AMD64 ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" ^
  -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
  -DCMAKE_CXX_FLAGS="/MT /EHsc /utf-8" 
``` 
 
```
rmdir /S /Q build
```

```
cmake -S . -B build ^
  -DCMAKE_SYSTEM_NAME=Windows ^
  -DCMAKE_SYSTEM_PROCESSOR=ARM64 ^
  -DBUILD_SHARED_LIBS=OFF ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" ^
  -DCMAKE_CXX_FLAGS_RELEASE="/MT /O2 /Ob2 /DNDEBUG" ^
  -DCMAKE_CXX_FLAGS="/MT /EHsc /utf-8 /D CT2_ARM64_BUILD /D __aarch64__" 
```  

```
cmake --build build --config Release 
```

## tokenizers-cpp, sentencepiece

```
git clone https://github.com/mlc-ai/tokenizers-cpp --recursive
cd tokenizers-cpp
```

```
cmake -S . -B build ^
      -G "Visual Studio 18 2026" ^
      -A ARM64 ^
      -DSPM_ENABLE_SHARED=OFF ^
      -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" 
```

```
rmdir /S /Q build
```

```
cmake -S . -B build ^
      -G "Visual Studio 18 2026" ^
      -DSPM_ENABLE_SHARED=OFF ^
      -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded" 
```

```
cmake --build build --config Release 
```

---

```
cmake -S . -B build `
    -G "Visual Studio 17 2022" `
    -A arm64 `
    -DCMAKE_CXX_FLAGS="/bigobj /openmp /O2 /fp:fast /Ob2" `
    -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_POLICY_VERSION_MINIMUM="3.5" 
```
