@echo off
echo ===============================================
echo RTSP Monitor GUI Build Script for Windows
echo ===============================================

REM Set environment variables
set BUILD_DIR=build_gui
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
echo Configuring GUI project with CMake...
echo Build Type: %CMAKE_BUILD_TYPE%
echo CUDA Support: %WITH_CUDA%
echo.

REM Configure with CMake (using the GUI CMakeLists.txt)
cmake .. -f ../CMakeLists_GUI.txt -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
    -DWITH_CUDA=%WITH_CUDA% ^
    -DOpenCV_DIR="C:/opencv/build" ^
    -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8" ^
    -DQt6_DIR="C:/Qt/6.6.0/msvc2022_64/lib/cmake/Qt6"

if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    echo Make sure Qt6 is installed at C:/Qt/6.6.0/msvc2022_64/
    pause
    exit /b 1
)

echo.
echo Building GUI project...
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
echo GUI Build completed successfully!
echo Executable location: %BUILD_DIR%\%CMAKE_BUILD_TYPE%\RTSPMonitorGUI.exe
echo ===============================================
echo.

cd ..
pause