@echo off
echo ===============================================
echo NVIDIA-SMI Quick Fix Script
echo ===============================================

echo Step 1: Testing current nvidia-smi status...
nvidia-smi >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [OK] nvidia-smi is working!
    nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv
    echo.
    echo Your GPU is properly configured. No fix needed.
    goto :end
) else (
    echo [ISSUE] nvidia-smi is not working
)

echo.
echo Step 2: Checking if nvidia-smi exists...
if exist "C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe" (
    echo [OK] nvidia-smi.exe found
    echo [FIX] Adding to PATH...
    set PATH=%PATH%;"C:\Program Files\NVIDIA Corporation\NVSMI"
    
    echo Testing after PATH fix...
    nvidia-smi >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo [SUCCESS] nvidia-smi now working after PATH fix!
        nvidia-smi --query-gpu=name,driver_version --format=csv
        goto :end
    ) else (
        echo [ISSUE] Still not working after PATH fix
    )
) else (
    echo [ERROR] nvidia-smi.exe not found - GPU driver not installed
)

echo.
echo Step 3: Checking GPU in Device Manager...
powershell -Command "Get-WmiObject -Class Win32_VideoController | Where-Object {$_.Name -like '*RTX A4000*' -or $_.Name -like '*NVIDIA*'} | Select-Object Name, DriverVersion, DriverDate"

echo.
echo Step 4: Checking NVIDIA services...
sc query "NVDisplay.ContainerLocalSystem" | find "RUNNING" >nul
if %ERRORLEVEL% equ 0 (
    echo [OK] NVIDIA Display Container service is running
) else (
    echo [ISSUE] NVIDIA Display Container service not running
    echo [FIX] Attempting to start service...
    net start "NVDisplay.ContainerLocalSystem"
)

echo.
echo ===============================================
echo DIAGNOSIS COMPLETE
echo ===============================================

if not exist "C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe" (
    echo.
    echo ❌ PROBLEM: GPU Driver not installed or corrupted
    echo.
    echo 💡 SOLUTION:
    echo 1. Download latest NVIDIA driver for RTX A4000:
    echo    https://www.nvidia.com/drivers
    echo 2. Product Type: Quadro
    echo 3. Product Series: RTX A-Series  
    echo 4. Product: RTX A4000
    echo 5. OS: Windows 10/11 64-bit
    echo.
    echo 6. Install with "Clean Installation" option
    echo 7. Restart computer after installation
    echo 8. Run this script again to verify
    echo.
    goto :end
)

echo.
echo ❌ PROBLEM: Driver installed but nvidia-smi not accessible
echo.
echo 💡 QUICK FIXES TO TRY:
echo.
echo Fix 1: Add to System PATH permanently
echo   - Windows Key + R, type: sysdm.cpl
echo   - Advanced tab → Environment Variables
echo   - System Variables → PATH → Edit → New
echo   - Add: C:\Program Files\NVIDIA Corporation\NVSMI
echo   - OK → OK → Restart Command Prompt
echo.
echo Fix 2: Reinstall GPU Driver
echo   - Device Manager → Display Adapters → RTX A4000
echo   - Right-click → Uninstall device
echo   - Check "Delete driver software"
echo   - Restart → Install latest driver
echo.
echo Fix 3: Check Windows Updates
echo   - Settings → Update & Security → Windows Update
echo   - Install all available updates
echo   - Restart and try again
echo.

:end
echo.
echo For detailed troubleshooting, see: NVIDIA_SMI_TROUBLESHOOTING.md
echo.
pause