cmake_minimum_required(VERSION 3.16)

project(wc VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "")

# OpenMP for parallel Monte Carlo simulation
find_package(OpenMP)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/tests/include)

if(OpenMP_CXX_FOUND)
    include_directories("/usr/lib/gcc/x86_64-linux-gnu/15/include")
endif()

add_executable(wc
    src/main.cpp
    src/util/CsvParser.cpp
    src/model/Team.cpp
    src/model/Match.cpp
    src/model/Tournament.cpp
    src/model/Tiebreaker.cpp
    src/model/MonteCarlo.cpp
    src/output/AsciiPrinter.cpp
    src/output/WebServer.cpp
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(wc OpenMP::OpenMP_CXX)
endif()

# Test executable
enable_testing()

add_executable(wc_tests
    tests/test_main.cpp
    tests/test_team.cpp
    tests/test_match.cpp
    tests/test_tournament.cpp
    tests/test_tiebreaker.cpp
    tests/test_montecarlo.cpp
    tests/test_asciiprinter.cpp
    src/util/CsvParser.cpp
    src/model/Team.cpp
    src/model/Match.cpp
    src/model/Tournament.cpp
    src/model/Tiebreaker.cpp
    src/model/MonteCarlo.cpp
    src/output/AsciiPrinter.cpp
    src/output/WebServer.cpp
)

target_compile_definitions(wc_tests PRIVATE WC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(OpenMP_CXX_FOUND)
    target_link_libraries(wc_tests OpenMP::OpenMP_CXX)
endif()

add_test(NAME wc_tests COMMAND wc_tests)
