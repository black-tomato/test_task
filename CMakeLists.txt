cmake_minimum_required(VERSION 3.14)
project(MessageQueue VERSION 1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS OFF)
endif()

add_executable(MessageQueueDemo main.cpp MessageQueue.h)

target_compile_features(MessageQueueDemo PRIVATE cxx_std_17)

if((CMAKE_CXX_COMPILER_ID MATCHES "GNU") OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    add_compile_options(
        -Werror
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(MessageQueueDemo pthread)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/W4 /WX)
endif()
