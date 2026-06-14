cmake_minimum_required(VERSION 3.16)
project(hailstone CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Add optimization flags
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-O3 -march=native)
endif()

# CPU search target
find_package(OpenMP REQUIRED)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx512f -mavx512cd -mavx512dq" COMPILER_SUPPORTS_AVX512)

if(COMPILER_SUPPORTS_AVX512)
    message(STATUS "AVX-512 compiler support detected. Vectorized CPU search will be compiled.")
    add_executable(hailstone_cpu cpu/main.cpp cpu/cpu_search.cpp cpu/cpu_search_avx512.cpp)
    set_source_files_properties(cpu/cpu_search_avx512.cpp PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512cd -mavx512dq")
    target_compile_definitions(hailstone_cpu PRIVATE HAS_AVX512_COMPILER_SUPPORT)
else()
    message(WARNING "Compiler does not support AVX-512 flags. Vectorized search will be disabled.")
    add_executable(hailstone_cpu cpu/main.cpp cpu/cpu_search.cpp)
endif()

target_link_libraries(hailstone_cpu PRIVATE OpenMP::OpenMP_CXX)

# Unit test target
add_executable(hailstone_test_uint128 tests/test_uint128.cpp)

# Cross-backend verification suite target
add_executable(hailstone_verify tests/test_verify.cpp)

# Polynomial generator utility target
add_executable(hailstone_genpoly tools/genpoly.cpp)

# Hailstone path representation utility target
add_executable(hailstone_path tools/hailstone_path.cpp)

# Vulkan search target
find_package(Vulkan REQUIRED)
if(Vulkan_FOUND)
    add_executable(hailstone_vulkan gpu_vulkan/main.cpp)
    target_link_libraries(hailstone_vulkan PRIVATE Vulkan::Vulkan)
    
    # Compile SPIR-V shader automatically
    find_program(GLSLC_EXECUTABLE glslc)
    set(PRECOMPILED_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/gpu_vulkan/shader.spv)
    if(GLSLC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
            COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gpu_vulkan/shader.comp -o ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gpu_vulkan/shader.comp
            COMMENT "Compiling shader.comp to SPIR-V in build directory"
        )
        # Add dependency on the compiled spv file
        target_sources(hailstone_vulkan PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shader.spv)
    else()
        if(EXISTS ${PRECOMPILED_SHADER})
            message(STATUS "glslc not found; using precompiled shader at ${PRECOMPILED_SHADER}")
            add_custom_command(TARGET hailstone_vulkan POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PRECOMPILED_SHADER}
                ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
            )
        else()
            message(FATAL_ERROR
                "glslc was not found and no precompiled shader is available.\n"
                "Install glslc (for example from the Vulkan SDK package), or add a precompiled shader at:\n"
                "  ${PRECOMPILED_SHADER}\n"
                "Then re-run CMake configure."
            )
        endif()
    endif()
endif()

# HIP search target (optional)
include(CheckLanguage)
check_language(HIP)
if(CMAKE_HIP_COMPILER)
    enable_language(HIP)
    set(CMAKE_HIP_STANDARD 20)
    set(CMAKE_HIP_STANDARD_REQUIRED ON)
    set(CMAKE_HIP_ARCHITECTURES "gfx1150" CACHE STRING "HIP architectures to target")
    
    add_executable(hailstone_hip gpu_hip/main.hip.cpp gpu_hip/hip_search.hip.cpp)
    set_source_files_properties(gpu_hip/main.hip.cpp gpu_hip/hip_search.hip.cpp PROPERTIES LANGUAGE HIP)
    target_include_directories(hailstone_hip PRIVATE include)
else()
    message(STATUS "HIP compiler not found. Skipping hailstone_hip target.")
endif()
