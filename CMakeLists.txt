﻿# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

project ("facade")

# Include sub-projects.
add_subdirectory ("include/facade")
add_subdirectory ("depends/googletest/googletest")
add_subdirectory ("facade_test")
add_subdirectory ("example")
