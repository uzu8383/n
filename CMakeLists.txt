cmake_minimum_required(VERSION 3.16)
project(RTSPMonitor LANGUAGES CXX CUDA)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set CUDA standard
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Enable CUDA support (optional)
option(WITH_CUDA "Enable CUDA support" ON)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler-specific options
if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Od /Zi")
    
    # Enable parallel compilation
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
    
    # Disable specific warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /wd4996 /wd4267 /wd4244")
    
    # VS 2019 specific optimizations
    if(MSVC_VERSION GREATER_EQUAL 1920 AND MSVC_VERSION LESS 1930)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ob2")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
        message(STATUS "Using Visual Studio 2019 optimizations")
    endif()
endif()

# Find required packages
find_package(OpenCV REQUIRED)

# CUDA configuration
if(WITH_CUDA)
    find_package(CUDA QUIET)
    if(CUDA_FOUND)
        enable_language(CUDA)
        find_package(OpenCV REQUIRED COMPONENTS opencv_core opencv_imgproc opencv_highgui opencv_objdetect opencv_tracking opencv_cudaimgproc opencv_cudaarithm opencv_cudafeatures2d)
        
        # Set CUDA architecture (adjust for your GPU)
        set(CMAKE_CUDA_ARCHITECTURES "86") # For RTX A4000, use 86. Adjust as needed.
        
        # CUDA compiler flags
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -gencode arch=compute_86,code=sm_86")
        set(CMAKE_CUDA_FLAGS_RELEASE "${CMAKE_CUDA_FLAGS_RELEASE} -O3 -DNDEBUG")
        set(CMAKE_CUDA_FLAGS_DEBUG "${CMAKE_CUDA_FLAGS_DEBUG} -g -G")
        
        # CUDA 12.9 specific optimizations for A4000
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --use_fast_math")
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --optimize 3")
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --maxrregcount=32")
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xptxas -O3")
        set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} -Xcompiler /O2")
        
        # VS 2019 CUDA host compiler
        if(MSVC_VERSION GREATER_EQUAL 1920 AND MSVC_VERSION LESS 1930)
            set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}")
        endif()
        
        message(STATUS "CUDA support enabled")
        add_definitions(-DWITH_CUDA)
    else()
        set(WITH_CUDA OFF)
        message(WARNING "CUDA not found, building without CUDA support")
    endif()
else()
    find_package(OpenCV REQUIRED COMPONENTS opencv_core opencv_imgproc opencv_highgui opencv_objdetect opencv_tracking)
    message(STATUS "CUDA support disabled")
endif()

# Include directories
include_directories(${OpenCV_INCLUDE_DIRS})

# Source files
set(SOURCES
    main.cpp
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} ${OpenCV_LIBS})

# Windows-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} winmm)
endif()

# Compiler definitions
if(WITH_CUDA)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_CUDA)
endif()

# Set target properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(WITH_CUDA)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        CUDA_STANDARD 17
        CUDA_STANDARD_REQUIRED ON
        CUDA_RESOLVE_DEVICE_SYMBOLS ON
    )
endif()

# Installation
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# Copy OpenCV DLLs on Windows
if(WIN32)
    # Find OpenCV DLLs and copy them to output directory
    get_target_property(OpenCV_LIB_PATH opencv_core IMPORTED_LOCATION_RELEASE)
    if(OpenCV_LIB_PATH)
        get_filename_component(OpenCV_BIN_DIR ${OpenCV_LIB_PATH} DIRECTORY)
        file(GLOB OpenCV_DLLS "${OpenCV_BIN_DIR}/*.dll")
        
        foreach(dll ${OpenCV_DLLS})
            add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${dll} $<TARGET_FILE_DIR:${PROJECT_NAME}>
            )
        endforeach()
    endif()
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  CUDA support: ${WITH_CUDA}")
if(WITH_CUDA)
    message(STATUS "  CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")
endif()
message(STATUS "  OpenCV version: ${OpenCV_VERSION}")
message(STATUS "  OpenCV libraries: ${OpenCV_LIBS}")
message(STATUS "")