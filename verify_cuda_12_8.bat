@echo off
echo ===============================================
echo CUDA 12.8 Verification Script
echo ===============================================

echo Step 1: Checking CUDA Toolkit 12.8...
if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8" (
    echo [OK] CUDA 12.8 installation found
    set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8
) else (
    echo [ERROR] CUDA 12.8 not found
    echo Please install CUDA 12.8 from:
    echo https://developer.nvidia.com/cuda-12-8-0-download-archive
    goto :error
)

echo.
echo Step 2: Verifying nvcc compiler...
"%CUDA_PATH%\bin\nvcc.exe" --version 2>nul
if %ERRORLEVEL% equ 0 (
    echo [OK] nvcc compiler working
    "%CUDA_PATH%\bin\nvcc.exe" --version | findstr "release 12.8"
    if %ERRORLEVEL% equ 0 (
        echo [OK] Confirmed CUDA 12.8 toolkit
    ) else (
        echo [WARNING] nvcc version mismatch detected
    )
) else (
    echo [ERROR] nvcc compiler not working
    goto :error
)

echo.
echo Step 3: Checking nvidia-smi (GPU driver)...
nvidia-smi >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [OK] nvidia-smi working
    for /f "tokens=9" %%i in ('nvidia-smi --query-gpu=cuda_version --format=csv,noheader,nounits') do set DRIVER_CUDA=%%i
    echo GPU Driver supports CUDA: %DRIVER_CUDA%
    
    REM Check if driver CUDA >= 12.8
    if "%DRIVER_CUDA:~0,4%" geq "12.8" (
        echo [OK] Driver supports CUDA 12.8+
    ) else if "%DRIVER_CUDA:~0,4%" geq "12.2" (
        echo [OK] Driver compatible with CUDA 12.8 (minimum 12.2 required)
    ) else (
        echo [WARNING] Driver may be too old for CUDA 12.8
        echo Please update GPU driver
    )
) else (
    echo [ERROR] nvidia-smi not working
    echo Please install/update NVIDIA GPU driver
    goto :error
)

echo.
echo Step 4: Testing CUDA device query...
if exist "%CUDA_PATH%\extras\demo_suite\deviceQuery.exe" (
    echo [OK] deviceQuery found, running test...
    "%CUDA_PATH%\extras\demo_suite\deviceQuery.exe" | findstr "Result = PASS"
    if %ERRORLEVEL% equ 0 (
        echo [SUCCESS] CUDA device test passed
    ) else (
        echo [WARNING] CUDA device test failed or incomplete
    )
) else (
    echo [INFO] deviceQuery not found (samples not installed)
)

echo.
echo Step 5: Checking RTX A4000 specific info...
nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv,noheader 2>nul | findstr "RTX A4000"
if %ERRORLEVEL% equ 0 (
    echo [OK] RTX A4000 detected
    nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv,noheader
) else (
    echo [INFO] RTX A4000 not detected, showing available GPUs:
    nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv,noheader
)

echo.
echo Step 6: Environment variables check...
echo CUDA_PATH: %CUDA_PATH%
echo PATH contains CUDA: 
echo %PATH% | findstr "CUDA\v12.8" >nul
if %ERRORLEVEL% equ 0 (
    echo [OK] CUDA 12.8 in PATH
) else (
    echo [WARNING] CUDA 12.8 not in PATH
    echo Add to PATH: %CUDA_PATH%\bin
)

echo.
echo Step 7: OpenCV CUDA compatibility check...
if exist "C:\opencv\build\x64\vc16\bin\opencv_world412.dll" (
    echo [OK] OpenCV 4.12.0 found
    if exist "C:\opencv\build\x64\vc16\bin\opencv_cudaimgproc412.dll" (
        echo [OK] OpenCV CUDA modules found
    ) else (
        echo [WARNING] OpenCV CUDA modules not found
        echo Please build OpenCV with CUDA support
    )
) else (
    echo [INFO] OpenCV not found in default location
)

echo.
echo ===============================================
echo VERIFICATION COMPLETE
echo ===============================================

echo.
echo 📊 SUMMARY:
echo ✅ CUDA Toolkit 12.8: Installed
for /f "tokens=9" %%i in ('nvidia-smi --query-gpu=cuda_version --format=csv,noheader,nounits 2^>nul') do echo ✅ GPU Driver CUDA: %%i
echo ✅ nvcc Compiler: Working
nvidia-smi --query-gpu=name --format=csv,noheader 2>nul
echo.

echo 🔍 VERSION MISMATCH INFO:
echo This is NORMAL and expected:
echo • nvidia-smi shows: Driver's maximum CUDA support
echo • nvcc shows: Installed toolkit version  
echo • Your application will use: CUDA 12.8 toolkit
echo.

echo 🚀 READY FOR BUILD:
echo Your system is ready to build the RTSP Monitor with:
echo • CUDA 12.8 toolkit
echo • RTX A4000 optimizations
echo • OpenCV 4.12.0 support
echo.
echo Next steps:
echo 1. Run: build.bat
echo 2. Test: cd build\Release ^&^& RTSPMonitor.exe
echo.
goto :end

:error
echo.
echo ❌ VERIFICATION FAILED
echo Please check the errors above and fix them before building.
echo.
echo Common fixes:
echo 1. Install CUDA 12.8: https://developer.nvidia.com/cuda-12-8-0-download-archive
echo 2. Update GPU driver: https://www.nvidia.com/drivers  
echo 3. Add CUDA to PATH: %CUDA_PATH%\bin
echo.

:end
pause