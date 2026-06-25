cmake_minimum_required(VERSION 3.24)

find_package(Threads REQUIRED)
find_package(CURL REQUIRED)

# Build SQLite from the amalgamation source
add_library(sqlite3 OBJECT
    ../third_party/sqlite3/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC ../third_party/sqlite3)
# Disable warnings for sqlite3 compilation
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /w)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# Core Domain & Database Library
add_library(core_lib
    src/db.cpp
    src/server.cpp
    src/projection.cpp
    src/email_service.cpp
    src/notification_engine.cpp
)
target_include_directories(core_lib PUBLIC include ../third_party ../third_party/sqlite3)
target_link_libraries(core_lib PUBLIC sqlite3 Threads::Threads CURL::libcurl)

if(NOT MSVC AND NOT APPLE)
    target_link_libraries(core_lib PUBLIC dl)
endif()

# Core Service Executable
add_executable(billminder_core
    src/main.cpp
)
target_link_libraries(billminder_core PRIVATE core_lib)

# Testing
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(core_tests
    tests/test_db.cpp
    tests/test_projection.cpp
)
target_link_libraries(core_tests PRIVATE core_lib gtest_main)
include(GoogleTest)
gtest_discover_tests(core_tests)
