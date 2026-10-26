cmake_minimum_required(VERSION 3.24...3.31)
cmake_policy(SET CMP0091 NEW)
project(ct2_server_native LANGUAGES C CXX)

include(FetchContent)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_POLICY_DEFAULT_CMP0067 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_MKL OFF CACHE BOOL "" FORCE)
set(WITH_DNNL OFF CACHE BOOL "" FORCE)
set(WITH_CUDA OFF CACHE BOOL "" FORCE)
set(OPENMP_RUNTIME NONE CACHE STRING "" FORCE)
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "" FORCE)
set(JSONCPP_STATIC_WINDOWS_RUNTIME ON CACHE BOOL "" FORCE)
set(CPUINFO_RUNTIME_TYPE static CACHE STRING "" FORCE)
set(BUILD_OBJECT_LIBS OFF CACHE BOOL "" FORCE)
set(TOKENIZERS_CPP_MSVC_RUNTIME_LIBRARY MT CACHE STRING "" FORCE)
set(SPM_ENABLE_SHARED OFF CACHE BOOL "" FORCE)
set(SPM_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_TCMALLOC OFF CACHE BOOL "" FORCE)
set(SPM_ENABLE_MSVC_MT_BUILD ON CACHE BOOL "" FORCE)
set(ABSL_MSVC_STATIC_RUNTIME ON CACHE BOOL "" FORCE)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "" FORCE)
  set(WITH_ACCELERATE ON CACHE BOOL "" FORCE)
  set(WITH_RUY OFF CACHE BOOL "" FORCE)
else()
  set(WITH_ACCELERATE OFF CACHE BOOL "" FORCE)
  set(WITH_RUY ON CACHE BOOL "" FORCE)
endif()
# MSVC ARM64 uses upstream's generic kernels (no forged GCC architecture macros).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$" AND MSVC)
  set(ENABLE_CPU_DISPATCH OFF CACHE BOOL "" FORCE)
else()
  set(ENABLE_CPU_DISPATCH ON CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(ctranslate2
  GIT_REPOSITORY https://github.com/OpenNMT/CTranslate2.git
  GIT_TAG 5de572ef60b67a0276c5989ee73d0072808ff4c3
  GIT_SUBMODULES third_party/cpu_features third_party/ruy third_party/spdlog
  GIT_SUBMODULES_RECURSE TRUE
  PATCH_COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ci/prepare-dependency.py" ct2 <SOURCE_DIR>)
FetchContent_Declare(jsoncpp
  GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
  GIT_TAG 89e2973c754a9c02a49974d839779b151e95afd6)
FetchContent_Declare(tokenizers_cpp
  GIT_REPOSITORY https://github.com/mlc-ai/tokenizers-cpp.git
  GIT_TAG c586c52f93f7b060753bd2388eb96a105cb7374d
  GIT_SUBMODULES_RECURSE TRUE
  PATCH_COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ci/prepare-dependency.py" tokenizers <SOURCE_DIR>)
FetchContent_MakeAvailable(ctranslate2 jsoncpp tokenizers_cpp)

# Do not expose cpp/include: it also contains stale CT2, tokenizer and JsonCpp
# headers which would silently mix old ABIs with the freshly built libraries.
set(PORTABLE_HEADERS "${CMAKE_CURRENT_BINARY_DIR}/portable-headers")
file(MAKE_DIRECTORY "${PORTABLE_HEADERS}")
foreach(header Eigen inja nlohmann httplib.h)
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/include/${header}" DESTINATION "${PORTABLE_HEADERS}")
endforeach()
add_executable(ct2-server ct2-server/main.cpp)
target_include_directories(ct2-server PRIVATE
  "${PORTABLE_HEADERS}" "${tokenizers_cpp_SOURCE_DIR}/sentencepiece/src")
target_link_libraries(ct2-server PRIVATE ctranslate2 tokenizers_cpp sentencepiece-static jsoncpp_static)
if(MSVC)
  target_compile_options(ct2-server PRIVATE /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
  target_compile_definitions(ct2-server PRIVATE WIN32 _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_link_libraries(ct2-server PRIVATE ws2_32)
endif()
install(TARGETS ct2-server RUNTIME DESTINATION bin COMPONENT server)
if(BUILD_TESTING)
  add_test(NAME server-http-smoke COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/ci/smoke-server.py" "$<TARGET_FILE:ct2-server>")
  set_tests_properties(server-http-smoke PROPERTIES TIMEOUT 45)
  add_executable(model-support-test tests/model_support_test.cpp)
  target_include_directories(model-support-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/ct2-server" "${PORTABLE_HEADERS}")
  if(MSVC)
    target_compile_options(model-support-test PRIVATE /utf-8 /EHsc)
    target_compile_definitions(model-support-test PRIVATE NOMINMAX)
  endif()
  add_test(NAME model-support COMMAND model-support-test)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-data")
  set_tests_properties(model-support PROPERTIES TIMEOUT 30
    ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_BINARY_DIR}/test-data;TMP=${CMAKE_CURRENT_BINARY_DIR}/test-data;TEMP=${CMAKE_CURRENT_BINARY_DIR}/test-data")
endif()
