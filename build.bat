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

REM Configure with CMake (prioritize CUDA 12.8)
set "CUDA_12_8=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
set "CUDA_12_9=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9"

if exist "%CUDA_12_8%" (
    set "CUDA_ROOT=%CUDA_12_8%"
    echo Using CUDA 12.8 (Primary choice)
) else if exist "%CUDA_12_9%" (
    set "CUDA_ROOT=%CUDA_12_9%"
    echo Using CUDA 12.9 (Fallback)
) else (
    echo ERROR: No compatible CUDA version found (12.8 or 12.9)
    echo Expected locations:
    echo   %CUDA_12_8%
    echo   %CUDA_12_9%
    pause
    exit /b 1
)

cmake .. -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
    -DWITH_CUDA=%WITH_CUDA% ^
    -DOpenCV_DIR="C:/opencv/build" ^
    -DCUDA_TOOLKIT_ROOT_DIR="%CUDA_ROOT%" ^
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