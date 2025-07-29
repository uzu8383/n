@echo off
echo ===============================================
echo Environment Setup for RTSP Monitor
echo ===============================================

REM Check for Visual Studio (prioritize Enterprise)
echo Checking for Visual Studio 2019...
set "VS_ENTERPRISE=C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_PROFESSIONAL=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
set "VS_COMMUNITY=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"

if exist "%VS_ENTERPRISE%" (
    echo [OK] Visual Studio 2019 Enterprise found (Primary choice)
    set "VS_PATH=%VS_ENTERPRISE%"
    echo Using Enterprise edition with advanced optimization features
) else if exist "%VS_PROFESSIONAL%" (
    echo [OK] Visual Studio 2019 Professional found (Fallback)
    set "VS_PATH=%VS_PROFESSIONAL%"
) else if exist "%VS_COMMUNITY%" (
    echo [OK] Visual Studio 2019 Community found (Fallback)
    set "VS_PATH=%VS_COMMUNITY%"
) else (
    echo [ERROR] Visual Studio 2019 not found!
    echo Please install Visual Studio 2019 with C++ development tools
    echo Expected locations (in priority order):
    echo   %VS_ENTERPRISE%
    echo   %VS_PROFESSIONAL%
    echo   %VS_COMMUNITY%
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

REM Check for CUDA (prioritize 12.8)
echo Checking for CUDA...
set "CUDA_12_8=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
set "CUDA_12_9=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9"

if exist "%CUDA_12_8%" (
    echo [OK] CUDA 12.8 found (Primary choice)
    set "CUDA_PATH=%CUDA_12_8%"
    echo Note: nvidia-smi may show 12.9 - this is normal (driver version)
) else if exist "%CUDA_12_9%" (
    echo [OK] CUDA 12.9 found (Fallback)
    set "CUDA_PATH=%CUDA_12_9%"
    echo Note: CUDA 12.8 is the primary target for this project
) else (
    echo [ERROR] CUDA not found in default location
    echo Please ensure CUDA 12.8 or 12.9 is installed
    echo Expected locations:
    echo   %CUDA_12_8%
    echo   %CUDA_12_9%
    echo Download CUDA 12.8: https://developer.nvidia.com/cuda-12-8-0-download-archive
    goto :error
)

REM Check for OpenCV
echo Checking for OpenCV...
set "OPENCV_BUILD=C:\opencv\build"

if exist "%OPENCV_BUILD%" (
    echo [OK] OpenCV found at %OPENCV_BUILD%
) else (
    echo [ERROR] OpenCV not found
    echo Expected location: %OPENCV_BUILD%
    echo Please install OpenCV and extract to C:\opencv\
    echo Download from: https://opencv.org/releases/
    goto :error
)

REM Check for OpenCV CUDA modules
set "OPENCV_CUDA_LIB=C:\opencv\build\x64\vc16\lib"
if exist "%OPENCV_CUDA_LIB%\opencv_cudaimgproc*.lib" (
    echo [OK] OpenCV with CUDA support found
) else (
    echo [WARNING] OpenCV CUDA modules not found
    echo Expected location: %OPENCV_CUDA_LIB%
    echo For optimal performance, use OpenCV compiled with CUDA support
)

REM Check nvidia-smi availability
echo.
echo Checking nvidia-smi...
nvidia-smi >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [OK] nvidia-smi working - GPU driver installed
    nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv,noheader,nounits
) else (
    echo [WARNING] nvidia-smi not working - GPU driver may be missing
    echo Please install NVIDIA GPU driver for RTX A4000
    echo Download from: https://www.nvidia.com/drivers
    echo See NVIDIA_SMI_TROUBLESHOOTING.md for detailed help
)

REM Set environment variables
echo.
echo Setting up environment variables...
set "PATH=%PATH%;C:\opencv\build\x64\vc16\bin"
set "PATH=%PATH%;%CUDA_PATH%\bin"
set "OpenCV_DIR=C:\opencv\build"

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