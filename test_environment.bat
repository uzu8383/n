@echo off
echo ===============================================
echo Environment Test Script
echo ===============================================

echo Testing path handling with spaces and parentheses...

REM Test Visual Studio paths
set "VS_COMMUNITY=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
echo Testing VS Community path: %VS_COMMUNITY%
if exist "%VS_COMMUNITY%" (
    echo [OK] VS Community path works correctly
) else (
    echo [INFO] VS Community not found (normal if not installed)
)

REM Test CUDA paths  
set "CUDA_12_8=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
echo Testing CUDA 12.8 path: %CUDA_12_8%
if exist "%CUDA_12_8%" (
    echo [OK] CUDA 12.8 path works correctly
) else (
    echo [INFO] CUDA 12.8 not found (normal if not installed)
)

REM Test OpenCV path
set "OPENCV_BUILD=C:\opencv\build"
echo Testing OpenCV path: %OPENCV_BUILD%
if exist "%OPENCV_BUILD%" (
    echo [OK] OpenCV path works correctly
) else (
    echo [INFO] OpenCV not found (normal if not installed)
)

echo.
echo ===============================================
echo Path Test Complete
echo ===============================================
echo.
echo If no errors above, you can safely run:
echo   setup_environment.bat
echo   build.bat
echo.
pause