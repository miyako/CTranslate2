"""Export and independently link-check a source-built static SDK."""

import json
import os
import pathlib
import shutil
import sys

from native_sdk import LIBRARIES, cache_value, export_sdk, library_name, run

root = pathlib.Path(__file__).resolve().parents[2]
arch = sys.argv[1]
sdk = export_sdk(root, arch)
build = root / ".build-native/build"
check = sdk / "link-check"
check.mkdir()
sources = root / "cpp/ct2-server/main.cpp"
libraries = "\n".join(json.dumps(str(sdk / "libraries" / library_name(name, os.name == "nt")))
                      for name in LIBRARIES)
system_libraries = ("ntdll userenv ws2_32 wsock32 bcrypt iphlpapi psapi" if os.name == "nt"
                    else '"-framework Accelerate" "-framework CoreFoundation"')
(check / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.24)
cmake_policy(SET CMP0091 NEW)
project(sdk_link_check LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_executable(sdk-check {json.dumps(str(sources))})
target_include_directories(sdk-check PRIVATE {json.dumps(str(sdk / 'include'))}
  {json.dumps(str(build / 'portable-headers'))})
target_link_libraries(sdk-check PRIVATE {libraries} {system_libraries})
if(MSVC)
  target_compile_options(sdk-check PRIVATE /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor)
  target_compile_definitions(sdk-check PRIVATE WIN32 NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
""")
configure = ["cmake", "-S", str(check), "-B", str(check / "build"),
             "-G", cache_value(build, "CMAKE_GENERATOR"), "-DCMAKE_BUILD_TYPE=Release"]
if os.name == "nt":
    platform = cache_value(build, "CMAKE_GENERATOR_PLATFORM")
    if platform:
        configure += ["-A", platform]
else:
    configure += [f"-DCMAKE_OSX_ARCHITECTURES={arch}", "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0"]
run(*configure)
run("cmake", "--build", str(check / "build"), "--config", "Release", "--parallel", "3")
binary = check / "build" / ("sdk-check.exe" if os.name == "nt" else "sdk-check")
if os.name == "nt" and not binary.exists():
    binary = check / "build/Release/sdk-check.exe"
run(sys.executable, str(root / "scripts/ci/smoke-server.py"), str(binary))
shutil.rmtree(check)
run(sys.executable, str(root / "scripts/ci/verify-ide-sdk.py"), str(sdk), arch)
artifacts = root / ".build-native/sdk-artifacts"
artifacts.mkdir(exist_ok=True)
shutil.make_archive(str(artifacts / sdk.name), "zip", sdk.parent, sdk.name)
