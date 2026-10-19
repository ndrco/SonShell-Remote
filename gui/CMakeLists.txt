cmake_minimum_required(VERSION 3.22)
project(sonshell_gui VERSION 0.1.0 LANGUAGES CXX)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_executable(sonshell-gui main.cpp)
target_compile_features(sonshell-gui PRIVATE cxx_std_17)
target_link_libraries(sonshell-gui PRIVATE Qt6::Widgets util)
target_compile_options(sonshell-gui PRIVATE -Wall -Wextra -Wpedantic)
