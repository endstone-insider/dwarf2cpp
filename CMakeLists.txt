cmake_minimum_required(VERSION 3.15)
project(dwarf2cpp CXX)

find_package(argparse REQUIRED)
find_package(LLVM REQUIRED)
find_package(spdlog REQUIRED)

add_executable(dwarf2cpp src/main.cpp
        src/context.cpp
        src/context.h
        src/entry.cpp
        src/entry.h
        src/source_file.cpp
        src/source_file.h
        src/type_printer.cpp
        src/type_printer.h)
target_link_libraries(dwarf2cpp argparse::argparse llvm-core::llvm-core spdlog::spdlog)
