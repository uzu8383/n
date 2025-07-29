# NVIDIA-SMI Troubleshooting Rehberi

## Sorun: CUDA 12.8 Yüklü Ama nvidia-smi Çalışmıyor

### 🔍 Sorunun Nedenleri

CUDA Toolkit ile GPU Driver arasındaki fark:
- **CUDA Toolkit**: Development tools (nvcc, libraries)
- **GPU Driver**: Hardware ile iletişim kuran driver
- **nvidia-smi**: GPU Driver ile birlikte gelir, CUDA Toolkit ile değil

## Adım 1: Durumu Tespit Etme

### Driver Kontrolü
```cmd
# nvidia-smi komutu çalışıyor mu?
nvidia-smi

# Olası hatalar:
# - 'nvidia-smi' is not recognized as an internal or external command
# - NVIDIA-SMI has failed because it couldn't communicate with the NVIDIA driver
# - No devices were found
```

### CUDA Toolkit Kontrolü
```cmd
# CUDA Toolkit kurulu mu?
nvcc --version
# Çıktı: Cuda compilation tools, release 12.8, V12.8.xxx

# CUDA PATH kontrolü
echo %CUDA_PATH%
# Çıktı: C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8
```

### GPU Varlık Kontrolü
```cmd
# Device Manager'dan GPU kontrolü
# Windows + X -> Device Manager -> Display adapters
# RTX A4000 görünüyor mu?
```

## Adım 2: GPU Driver Kurulumu

### Driver İndirme
1. **NVIDIA Driver İndirme Sayfası**: https://www.nvidia.com/drivers
2. **Product Type**: Quadro
3. **Product Series**: RTX A-Series
4. **Product**: RTX A4000
5. **Operating System**: Windows 10/11 64-bit

### Önerilen Driver Versiyonları
```cmd
# CUDA 12.8 için uyumlu driver versiyonları:
# Minimum: 520.61.05
# Önerilen: 537.13 veya üzeri
# En güncel: 546.xx serisi
```

### Driver Kurulumu
```cmd
# 1. Eski driver'ları kaldırın (opsiyonel)
# Control Panel -> Programs -> Uninstall
# - NVIDIA Graphics Driver
# - NVIDIA PhysX System Software

# 2. Yeni driver'ı yönetici olarak çalıştırın
# 3. "Clean Installation" seçeneğini işaretleyin
# 4. Kurulum sonrası yeniden başlatın
```

## Adım 3: Kurulum Sonrası Doğrulama

### nvidia-smi Test
```cmd
# Driver kurulumu sonrası test
nvidia-smi

# Beklenen çıktı:
# +-----------------------------------------------------------------------------+
# | NVIDIA-SMI 537.13       Driver Version: 537.13       CUDA Version: 12.2  |
# |-------------------------------+----------------------+----------------------+
# | GPU  Name            TCC/WDDM | Bus-Id        Disp.A | Volatile Uncorr. ECC |
# | Fan  Temp  Perf  Pwr:Usage/Cap|         Memory-Usage | GPU-Util  Compute M. |
# |===============================+======================+======================|
# |   0  RTX A4000           WDDM | 00000000:01:00.0  On |                  Off |
# | 41%   30C    P8    15W / 140W |    xxx MiB / 16376MiB|      0%      Default |
# +-------------------------------+----------------------+----------------------+
```

### PATH Kontrolü
```cmd
# nvidia-smi PATH'de mi?
where nvidia-smi
# Beklenen: C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe

# PATH'e ekleme (gerekirse)
set PATH=%PATH%;"C:\Program Files\NVIDIA Corporation\NVSMI"
```

## Adım 4: Yaygın Sorunlar ve Çözümleri

### 1. "nvidia-smi not recognized" Hatası

#### Çözüm A: PATH Ekleme
```cmd
# Geçici çözüm
set PATH=%PATH%;"C:\Program Files\NVIDIA Corporation\NVSMI"

# Kalıcı çözüm (System Properties -> Environment Variables)
# System PATH'e ekleyin: C:\Program Files\NVIDIA Corporation\NVSMI
```

#### Çözüm B: Driver Yeniden Kurulumu
```cmd
# 1. Device Manager -> Display adapters -> RTX A4000 -> Uninstall
# 2. "Delete driver software" işaretleyin
# 3. Yeniden başlatın
# 4. En güncel driver'ı kurun
```

### 2. "Failed to communicate with NVIDIA driver" Hatası

#### Çözüm A: Driver Service Kontrolü
```cmd
# NVIDIA services çalışıyor mu?
services.msc

# Kontrol edilecek servisler:
# - NVIDIA Display Container LS (Running)
# - NVIDIA LocalSystem Container (Running)
# - NVIDIA NetworkService Container (Running)

# Servis başlatma
net start "NVIDIA Display Container LS"
```

#### Çözüm B: Registry Temizliği
```cmd
# Registry Editor (regedit) açın
# HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\nvlddmkm
# Bu key varsa ve sorun devam ediyorsa driver'ı tamamen kaldırıp yeniden kurun
```

### 3. "CUDA Version Mismatch" Uyarısı

```cmd
# nvidia-smi çıktısında:
# CUDA Version: 12.2 (Driver'dan)
# nvcc --version: 12.8 (Toolkit'ten)

# Bu normal bir durumdur. Driver CUDA version'ı minimum desteklenen versiyondur.
# CUDA 12.8 Toolkit, 12.2 driver ile uyumludur.
```

## Adım 5: CUDA 12.8 ile Uyumluluk

### Driver-CUDA Uyumluluk Tablosu
| CUDA Toolkit | Minimum Driver | Önerilen Driver |
|--------------|----------------|-----------------|
| 12.8         | 520.61.05      | 537.13+         |
| 12.9         | 545.84         | 546.xx+         |

### Uyumluluk Testi
```cmd
# CUDA runtime test
cd "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8\extras\demo_suite"
deviceQuery.exe

# Beklenen çıktı:
# Device 0: "RTX A4000"
#   CUDA Driver Version / Runtime Version: 12.2 / 12.8
#   CUDA Capability Major/Minor version number: 8.6
#   Result = PASS
```

## Adım 6: Alternatif Çözümler

### 1. NVIDIA Control Panel Kullanımı
```cmd
# nvidia-smi alternatifi olarak
# NVIDIA Control Panel -> System Information
# GPU bilgilerini görüntüleyebilirsiniz
```

### 2. GPU-Z Kullanımı
```cmd
# Üçüncü parti tool: GPU-Z
# https://www.techpowerup.com/gpuz/
# Detaylı GPU bilgileri sağlar
```

### 3. PowerShell ile GPU Bilgisi
```powershell
# Windows PowerShell
Get-WmiObject -Class Win32_VideoController | Select-Object Name, DriverVersion, DriverDate

# Çıktı:
# Name        : NVIDIA RTX A4000
# DriverVersion : 31.0.15.3713
# DriverDate    : 20231010000000.000000-000
```

## Adım 7: Proje Build Testi

### CUDA Availability Test
```cmd
# Projenizi build edin
build.bat

# Çalıştırın
cd build\Release
RTSPMonitor.exe

# Çıktıda görmeniz gereken:
# CUDA devices available: 1
# RTX A4000, 24576 MB, sm_86
# OpenCV 4.12.0 + CUDA 12.9 + A4000 optimizations enabled
```

### Manuel CUDA Test
```cpp
// test_cuda.cpp
#include <cuda_runtime.h>
#include <iostream>

int main() {
    int deviceCount;
    cudaGetDeviceCount(&deviceCount);
    
    std::cout << "CUDA Devices: " << deviceCount << std::endl;
    
    if (deviceCount > 0) {
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, 0);
        std::cout << "Device: " << prop.name << std::endl;
        std::cout << "Compute Capability: " << prop.major << "." << prop.minor << std::endl;
        std::cout << "Memory: " << prop.totalGlobalMem / (1024*1024) << " MB" << std::endl;
        return 0;
    }
    
    std::cout << "No CUDA devices found!" << std::endl;
    return -1;
}
```

```cmd
# Compile ve test
nvcc test_cuda.cpp -o test_cuda.exe
test_cuda.exe

# Beklenen çıktı:
# CUDA Devices: 1
# Device: RTX A4000  
# Compute Capability: 8.6
# Memory: 24576 MB
```

## Adım 8: Sistem Optimizasyonları

### GPU Performance Mode
```cmd
# nvidia-smi çalıştıktan sonra
nvidia-smi -pm 1  # Persistence mode
nvidia-smi -ac 6001,1560  # Max clocks for A4000
```

### Windows Power Settings
```cmd
# High Performance power plan
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c

# GPU power management disable
# Device Manager -> RTX A4000 -> Properties -> Power Management
# "Allow computer to turn off this device" işaretini kaldırın
```

## Özet Checklist

### ✅ Yapılması Gerekenler:
1. **GPU Driver kurulu mu?** → En güncel NVIDIA driver kurun
2. **nvidia-smi PATH'de mi?** → System PATH'e ekleyin
3. **NVIDIA Services çalışıyor mu?** → Services.msc'den kontrol edin
4. **CUDA Toolkit uyumlu mu?** → 12.8 için 520.61.05+ driver gerekli
5. **GPU tanınıyor mu?** → Device Manager'dan kontrol edin

### 🔧 Hızlı Fix:
```cmd
# 1. En güncel NVIDIA driver indirin ve kurun
# 2. Yeniden başlatın
# 3. Test edin:
nvidia-smi
nvcc --version

# 4. Proje build edin:
build.bat
```

Bu adımları takip ettikten sonra hem `nvidia-smi` çalışacak hem de CUDA 12.8 ile RTX A4000 GPU'nuz optimal performansla çalışacaktır.