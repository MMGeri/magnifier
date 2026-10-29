cmake_minimum_required(VERSION 3.25)
project(magnifier LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
	add_executable(magnifier WIN32 src/magnifier.cpp)

	target_compile_definitions(magnifier PRIVATE UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0601)

	# Explicitly link what the code uses (MSBuild project already does this; CMake needs it).
	target_link_libraries(magnifier PRIVATE magnification shlwapi)
else()
	add_executable(magnifier src/magnifier.cpp)
endif()
