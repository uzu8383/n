cmake_minimum_required(VERSION 3.18)
project(RTSPMultiStreamProcessor LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Find required packages
find_package(OpenCV REQUIRED)
find_package(CUDA REQUIRED)
find_package(PkgConfig REQUIRED)

# Check for CUDA 12.8
if(CUDA_VERSION VERSION_LESS "12.8")
    message(WARNING "CUDA version ${CUDA_VERSION} found, but 12.8 or higher is recommended")
endif()

# Find additional libraries
pkg_check_modules(GSTREAMER REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP REQUIRED gstreamer-app-1.0)

# Include directories
include_directories(${OpenCV_INCLUDE_DIRS})
include_directories(${CUDA_INCLUDE_DIRS})
include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(src)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -fopenmp")
set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -O3 --use_fast_math -gencode arch=compute_86,code=sm_86")

# Source files
set(SOURCES
    src/main.cpp
    src/RTSPStreamManager.cpp
    src/ObjectTracker.cpp
    src/ROIManager.cpp
    src/GPUProcessor.cu
    src/Utils.cpp
)

# Headers
set(HEADERS
    src/RTSPStreamManager.h
    src/ObjectTracker.h
    src/ROIManager.h
    src/GPUProcessor.h
    src/Utils.h
    src/Common.h
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    ${OpenCV_LIBS}
    ${CUDA_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    pthread
    gomp
)

# CUDA separable compilation
set_property(TARGET ${PROJECT_NAME} PROPERTY CUDA_SEPARABLE_COMPILATION ON)

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)