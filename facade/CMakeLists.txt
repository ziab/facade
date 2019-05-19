# CMakeList.txt : CMake project for facade, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

add_library(facade INTERFACE)

target_include_directories(facade INTERFACE 
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../depends/cereal/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../depends/digestpp
	)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  # using GCC
	# can remove if gcc-9 is used
	target_link_libraries(facade INTERFACE stdc++fs)
	set(THREADS_PREFER_PTHREAD_FLAG ON)
	find_package(Threads REQUIRED)
	target_link_libraries(facade INTERFACE Threads::Threads)
endif()
