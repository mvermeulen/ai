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

# Build allowed suffixes generator target
add_executable(hailstone_gen_allowed_24 tools/gen_allowed_suffixes.cpp)

# Generate allowed suffixes binary file at build time
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/allowed_suffixes_24.bin
    COMMAND hailstone_gen_allowed_24 ${CMAKE_CURRENT_BINARY_DIR}/allowed_suffixes_24.bin
    DEPENDS hailstone_gen_allowed_24
    COMMENT "Precomputing allowed suffixes binary (width 24) at build time"
)

add_custom_target(generate_suffixes_24 DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/allowed_suffixes_24.bin)

# CPU search target
find_package(OpenMP REQUIRED)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx512f -mavx512cd -mavx512dq" COMPILER_SUPPORTS_AVX512)

if(COMPILER_SUPPORTS_AVX512)
    message(STATUS "AVX-512 compiler support detected. Vectorized CPU search will be compiled.")
    add_executable(hailstone_cpu cpu/main.cpp cpu/cpu_search.cpp cpu/cpu_search_avx512.cpp)
    set_source_files_properties(cpu/cpu_search_avx512.cpp PROPERTIES COMPILE_FLAGS "-O3 -march=native -mavx512f -mavx512cd -mavx512dq")
    target_compile_definitions(hailstone_cpu PRIVATE HAS_AVX512_COMPILER_SUPPORT)
    
    # Profile CPU target
    add_executable(hailstone_cpu_profile cpu/main.cpp cpu/cpu_search.cpp cpu/cpu_search_avx512.cpp)
    set_source_files_properties(cpu/cpu_search_avx512.cpp PROPERTIES COMPILE_FLAGS "-O3 -march=native -mavx512f -mavx512cd -mavx512dq")
    target_compile_definitions(hailstone_cpu_profile PRIVATE HAS_AVX512_COMPILER_SUPPORT)
else()
    message(WARNING "Compiler does not support AVX-512 flags. Vectorized search will be disabled.")
    add_executable(hailstone_cpu cpu/main.cpp cpu/cpu_search.cpp)
    
    # Profile CPU target
    add_executable(hailstone_cpu_profile cpu/main.cpp cpu/cpu_search.cpp)
endif()

target_link_libraries(hailstone_cpu PRIVATE OpenMP::OpenMP_CXX)
target_link_libraries(hailstone_cpu_profile PRIVATE OpenMP::OpenMP_CXX)
target_compile_options(hailstone_cpu_profile PRIVATE -g -fno-omit-frame-pointer)
target_link_options(hailstone_cpu_profile PRIVATE -g -fno-omit-frame-pointer)

# Unit test target
add_executable(hailstone_test_uint128 tests/test_uint128.cpp)

# Cross-backend verification suite target
add_executable(hailstone_verify tests/test_verify.cpp)

# Profile verification target
add_executable(hailstone_verify_profile tests/test_verify.cpp)
target_compile_options(hailstone_verify_profile PRIVATE -g -fno-omit-frame-pointer)
target_link_options(hailstone_verify_profile PRIVATE -g -fno-omit-frame-pointer)

# Polynomial generator utility target
add_executable(hailstone_genpoly tools/genpoly.cpp)

# Hailstone path representation utility target
add_executable(hailstone_path tools/hailstone_path.cpp)

# Profile path utility target
add_executable(hailstone_path_profile tools/hailstone_path.cpp)
target_compile_options(hailstone_path_profile PRIVATE -g -fno-omit-frame-pointer)
target_link_options(hailstone_path_profile PRIVATE -g -fno-omit-frame-pointer)

# Hailstone backwards step search utility target
add_executable(hailstone_backwards tools/hailstone_backwards.cpp)

# Vulkan search target
find_package(Vulkan)
find_program(GLSLC_EXECUTABLE glslc)
set(PRECOMPILED_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/gpu_vulkan/shader.spv)

if(Vulkan_FOUND AND (GLSLC_EXECUTABLE OR EXISTS ${PRECOMPILED_SHADER}))
    message(STATUS "Vulkan compute support enabled.")
    add_executable(hailstone_vulkan gpu_vulkan/main.cpp)
    target_link_libraries(hailstone_vulkan PRIVATE Vulkan::Vulkan)
    
    # Profile Vulkan target
    add_executable(hailstone_vulkan_profile gpu_vulkan/main.cpp)
    target_link_libraries(hailstone_vulkan_profile PRIVATE Vulkan::Vulkan)
    target_compile_options(hailstone_vulkan_profile PRIVATE -g -fno-omit-frame-pointer)
    target_link_options(hailstone_vulkan_profile PRIVATE -g -fno-omit-frame-pointer)
    
    # Compile SPIR-V shader automatically
    if(GLSLC_EXECUTABLE)
        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
            COMMAND ${GLSLC_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gpu_vulkan/shader.comp -o ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gpu_vulkan/shader.comp
            COMMENT "Compiling shader.comp to SPIR-V in build directory"
        )
        # Add dependency on the compiled spv file
        target_sources(hailstone_vulkan PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shader.spv)
        target_sources(hailstone_vulkan_profile PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shader.spv)
    else()
        message(STATUS "glslc not found; using precompiled shader at ${PRECOMPILED_SHADER}")
        add_custom_command(TARGET hailstone_vulkan POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PRECOMPILED_SHADER}
            ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
        )
        add_custom_command(TARGET hailstone_vulkan_profile POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${PRECOMPILED_SHADER}
            ${CMAKE_CURRENT_BINARY_DIR}/shader.spv
        )
    endif()
else()
    message(STATUS "Vulkan or shader compiler (glslc) not found. Skipping hailstone_vulkan targets.")
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
    
    # Profile HIP target
    add_executable(hailstone_hip_profile gpu_hip/main.hip.cpp gpu_hip/hip_search.hip.cpp)
    set_source_files_properties(gpu_hip/main.hip.cpp gpu_hip/hip_search.hip.cpp PROPERTIES LANGUAGE HIP)
    target_include_directories(hailstone_hip_profile PRIVATE include)
    target_compile_options(hailstone_hip_profile PRIVATE -g -fno-omit-frame-pointer)
    target_link_options(hailstone_hip_profile PRIVATE -g -fno-omit-frame-pointer)
endif()

# Dependencies on build-time precomputed allowed suffixes
add_dependencies(hailstone_cpu generate_suffixes_24)
add_dependencies(hailstone_cpu_profile generate_suffixes_24)
add_dependencies(hailstone_verify generate_suffixes_24)
add_dependencies(hailstone_verify_profile generate_suffixes_24)
if(TARGET hailstone_vulkan)
    add_dependencies(hailstone_vulkan generate_suffixes_24)
    add_dependencies(hailstone_vulkan_profile generate_suffixes_24)
endif()
if(TARGET hailstone_hip)
    add_dependencies(hailstone_hip generate_suffixes_24)
    add_dependencies(hailstone_hip_profile generate_suffixes_24)
endif()

