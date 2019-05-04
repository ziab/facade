# CMakeList.txt : CMake project for facade, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Add source to this project's executable.
add_executable (facade "facade.cpp" "facade.h")

# TODO: Add tests and install targets if needed.
set_target_properties(facade PROPERTIES
            CXX_STANDARD 17
            CXX_EXTENSIONS OFF)

if(MSVC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif(MSVC)