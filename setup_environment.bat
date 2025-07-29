@echo off
echo ===============================================
echo Environment Setup for RTSP Monitor
echo ===============================================

REM Check for Visual Studio
echo Checking for Visual Studio 2022...
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [OK] Visual Studio 2022 Community found
    set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [OK] Visual Studio 2022 Professional found
    set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    echo [OK] Visual Studio 2022 Enterprise found
    set VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat
) else (
    echo [ERROR] Visual Studio 2022 not found!
    echo Please install Visual Studio 2022 with C++ development tools
    goto :error
)

REM Check for CMake
echo Checking for CMake...
cmake --version >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [OK] CMake found
    cmake --version
) else (
    echo [ERROR] CMake not found!
    echo Please install CMake from https://cmake.org/download/
    goto :error
)

REM Check for CUDA
echo Checking for CUDA 12.8...
if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8" (
    echo [OK] CUDA 12.8 found
    set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8
) else (
    echo [WARNING] CUDA 12.8 not found in default location
    echo Please ensure CUDA 12.8 is installed
    echo Download from: https://developer.nvidia.com/cuda-downloads
)

REM Check for OpenCV
echo Checking for OpenCV...
if exist "C:\opencv\build" (
    echo [OK] OpenCV found at C:\opencv\build
) else (
    echo [ERROR] OpenCV not found at C:\opencv\build
    echo Please install OpenCV and extract to C:\opencv\
    echo Download from: https://opencv.org/releases/
    goto :error
)

REM Check for OpenCV CUDA modules
if exist "C:\opencv\build\x64\vc16\lib\opencv_cudaimgproc*.lib" (
    echo [OK] OpenCV with CUDA support found
) else (
    echo [WARNING] OpenCV CUDA modules not found
    echo For optimal performance, use OpenCV compiled with CUDA support
)

REM Set environment variables
echo.
echo Setting up environment variables...
set PATH=%PATH%;C:\opencv\build\x64\vc16\bin
set PATH=%PATH%;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8\bin
set OpenCV_DIR=C:\opencv\build

echo [OK] Environment setup completed successfully!
echo.
echo Next steps:
echo 1. Run build.bat to compile the project
echo 2. Configure your RTSP stream URLs in the code
echo 3. Run the executable from build\Release\RTSPMonitor.exe
echo.
goto :end

:error
echo.
echo [ERROR] Environment setup failed!
echo Please install the missing dependencies and run this script again.
echo.
pause
exit /b 1

:end
pause