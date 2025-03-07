# Copyright (c) 2020 - 2021 Advanced Micro Devices, Inc. All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

cmake_minimum_required(VERSION 3.10)
if (NOT DEFINED ROCM_PATH )
     set ( ROCM_PATH "/opt/rocm"  CACHE STRING "Default ROCM installation directory." )
endif ()

if(NOT DEFINED HIP_PATH)
    if(NOT DEFINED ENV{HIP_PATH})
        set(HIP_PATH ${ROCM_PATH} CACHE PATH "Path to which HIP has been installed")
    else()
        set(HIP_PATH $ENV{HIP_PATH} CACHE PATH "Path to which HIP has been installed")
    endif()
endif()
set(CMAKE_MODULE_PATH "${HIP_PATH}/hip/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_HIP_ARCHITECTURES OFF)
project(12_cmake)

set(HIP_CLANG_NUM_PARALLEL_JOBS 2)
find_package(HIP QUIET)
if(HIP_FOUND)
    message(STATUS "Found HIP: " ${HIP_VERSION})
else()
    message(FATAL_ERROR "Could not find HIP. Ensure that HIP is either installed in ${ROCM_PATH}/hip or the variable HIP_PATH is set to point to the right location.")
endif()

set(MY_SOURCE_FILES MatrixTranspose.cpp)
set(MY_TARGET_NAME MatrixTranspose)
set(MY_HIPCC_OPTIONS)
set(MY_CLANG_OPTIONS)
set(MY_NVCC_OPTIONS)

set_source_files_properties(${MY_SOURCE_FILES} PROPERTIES HIP_SOURCE_PROPERTY_FORMAT 1)
hip_add_executable(${MY_TARGET_NAME} ${MY_SOURCE_FILES} HIPCC_OPTIONS ${MY_HIPCC_OPTIONS} CLANG_OPTIONS ${MY_CLANG_OPTIONS} NVCC_OPTIONS ${MY_NVCC_OPTIONS})

# Search for rocm in common locations
list(APPEND CMAKE_PREFIX_PATH ${ROCM_PATH}/hip ${ROCM_PATH})
find_package(hip QUIET CONFIG)
if(TARGET hip::host)
    message(STATUS "Found hip::host at ${hip_DIR}")
    target_link_libraries(${MY_TARGET_NAME} hip::host)
endif()
