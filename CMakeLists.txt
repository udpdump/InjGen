cmake_minimum_required(VERSION 3.20)
project(InjGen LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    enable_language(ASM_MASM)
    add_executable(InjGen
        ConsoleApplication1/core.cpp
        ConsoleApplication1/proc.cpp
        ConsoleApplication1/scn.cpp
        ConsoleApplication1/krnljmper.c
        ConsoleApplication1/krnljmper.x64.asm
    )
    target_include_directories(InjGen PRIVATE ConsoleApplication1)
    target_compile_definitions(InjGen PRIVATE _CONSOLE)
else()
    add_executable(InjGen
        ConsoleApplication1/core.cpp
        ConsoleApplication1/proc.cpp
        ConsoleApplication1/scn.cpp
    )
    target_include_directories(InjGen PRIVATE ConsoleApplication1)
    target_compile_options(InjGen PRIVATE -Wall -O3)
endif()
