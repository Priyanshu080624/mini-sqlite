cmake_minimum_required(VERSION 3.15)
project(mini_sqlite CXX)

if(WIN32)
    add_compile_options(/utf-8)
endif()

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add the executable
add_executable(minidb_demo
    src/main.cpp
    src/storage/pager.cpp
)

# Tell the compiler where to find headers (so #include "storage/pager.h" works)
target_include_directories(minidb_demo PRIVATE src)

# Enable sanitizers and warnings based on the compiler being used
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # For GCC/Clang (like MSYS2)
    target_compile_options(minidb_demo PRIVATE -Wall -Wextra -g -fsanitize=address,undefined)
    target_link_options(minidb_demo PRIVATE -fsanitize=address,undefined)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # For Visual Studio's MSVC compiler (Windows native ASAN support)
    target_compile_options(minidb_demo PRIVATE /W4 /Zi /fsanitize=address)
    target_link_options(minidb_demo PRIVATE /DEBUG)
endif()
