@echo off
echo ===============================================
echo RTSP Monitor Build Script for Windows
echo ===============================================

REM Set environment variables
set BUILD_DIR=build
set CMAKE_BUILD_TYPE=Release
set WITH_CUDA=ON

REM Check if build directory exists
if exist %BUILD_DIR% (
    echo Cleaning existing build directory...
    rmdir /s /q %BUILD_DIR%
)

REM Create build directory
mkdir %BUILD_DIR%
cd %BUILD_DIR%

echo.
echo Configuring project with CMake...
echo Build Type: %CMAKE_BUILD_TYPE%
echo CUDA Support: %WITH_CUDA%
echo.

REM Configure with CMake
cmake .. -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
    -DWITH_CUDA=%WITH_CUDA% ^
    -DOpenCV_DIR="C:/opencv/build" ^
    -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8" ^
    -DCMAKE_GENERATOR_TOOLSET=v142

if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo Building project...
echo.

REM Build the project
cmake --build . --config %CMAKE_BUILD_TYPE% --parallel

if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo ===============================================
echo Build completed successfully!
echo Executable location: %BUILD_DIR%\%CMAKE_BUILD_TYPE%\RTSPMonitor.exe
echo ===============================================
echo.

cd ..
pause