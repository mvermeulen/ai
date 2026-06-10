cmake_minimum_required(VERSION 3.16)
project(hailstone CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Add optimization flags
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-O3 -march=native)
endif()

# CPU search target
add_executable(hailstone_cpu cpu/main.cpp cpu/cpu_search.cpp)

# Unit test target
add_executable(hailstone_test_uint128 tests/test_uint128.cpp)

# Cross-backend verification suite target
add_executable(hailstone_verify tests/test_verify.cpp)

# Vulkan search target
find_package(Vulkan REQUIRED)
if(Vulkan_FOUND)
    add_executable(hailstone_vulkan gpu_vulkan/main.cpp)
    target_link_libraries(hailstone_vulkan PRIVATE Vulkan::Vulkan)
    
    # Compile SPIR-V shader automatically
    find_program(GLSLC_EXECUTABLE glslc)
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
        # Fallback: copy precompiled shader
        add_custom_command(TARGET hailstone_vulkan POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/gpu_vulkan/shader.spv
            ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
        )
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
