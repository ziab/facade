# CMakeList.txt : CMake project for facade, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

add_library(facade INTERFACE)

target_include_directories(facade INTERFACE 
	${CMAKE_CURRENT_SOURCE_DIR}
	../cereal/include
	)
