# CMakeList.txt : CMake project for facade, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

if (MSVC)
    foreach (flag_var
             CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
             CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
             CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
             CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
		
		if (NOT BUILD_SHARED_LIBS AND NOT gtest_force_shared_crt)
			string(REPLACE "/MD" "-MT" ${flag_var} "${${flag_var}}")
		endif()

		# string(REPLACE "/W3" "/W4" ${flag_var} "${${flag_var}}")
    endforeach()
    
	add_compile_definitions(_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17")
endif()

file(GLOB_RECURSE cppfiles "*.cpp")
# Add source to this project's executable.
add_executable (facade_test ${cppfiles})

target_link_libraries(facade_test PRIVATE facade)

target_link_libraries(facade_test PRIVATE gtest)

# TODO: Add tests and install targets if needed.
set_target_properties(facade_test PROPERTIES
            CXX_STANDARD 17
            CXX_EXTENSIONS OFF)