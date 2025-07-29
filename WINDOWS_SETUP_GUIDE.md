# Windows Setup Guide - RTSP Multi-Stream Monitor

Bu rehber, A4000 GPU ve i7 CPU'lu Windows sisteminde 16+ RTSP yayını için optimize edilmiş C++ uygulamasının kurulumunu açıklar.

## Sistem Gereksinimleri

### Donanım
- **GPU**: NVIDIA RTX A4000 (CUDA Compute Capability 8.6)
- **CPU**: Intel i7 (önerilen: 8+ çekirdek)
- **RAM**: Minimum 16GB (32GB önerilir)
- **Depolama**: 10GB boş alan

### Yazılım
- **İşletim Sistemi**: Windows 10/11 (64-bit)
- **CUDA**: 12.9 (En güncel sürüm)
- **Visual Studio**: 2019 (Community/Professional/Enterprise)
- **CMake**: 3.16 veya üzeri (VS 2019 uyumlu)
- **OpenCV**: 4.12.0 (Latest version with advanced CUDA 12.9 support)

## Adım 1: Visual Studio 2019 Kurulumu

1. [Visual Studio 2019](https://visualstudio.microsoft.com/vs/older-downloads/) indirin (Community/Professional/Enterprise)
2. Kurulum sırasında şu bileşenleri seçin:
   - **Desktop development with C++**
   - **MSVC v142 - VS 2019 C++ x64/x86 build tools (v14.29)**
   - **Windows 10 SDK (10.0.19041.0 veya üzeri)**
   - **CMake tools for Visual Studio**
   - **Git for Windows** (opsiyonel)

## Adım 2: CUDA 12.9 Kurulumu

1. [NVIDIA CUDA Toolkit 12.9](https://developer.nvidia.com/cuda-downloads) indirin
   - **Network Installer** (küçük dosya) veya **Local Installer** (büyük dosya) seçebilirsiniz
   - A4000 GPU için optimize edilmiş en güncel sürüm
2. Varsayılan konuma kurun: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.9`
3. Kurulum sırasında seçilecek bileşenler:
   - ✅ **CUDA Toolkit 12.9**
   - ✅ **CUDA Visual Studio Integration**
   - ✅ **CUDA Runtime**
   - ✅ **CUDA Documentation** (opsiyonel)
   - ✅ **CUDA Samples** (opsiyonel)
4. Kurulum sonrası sistem yeniden başlatın
5. Doğrulama için komut satırında çalıştırın:
   ```cmd
   nvcc --version
   # Beklenen çıktı: Cuda compilation tools, release 12.9, V12.9.xxx
   
   nvidia-smi
   # GPU bilgilerini ve CUDA Runtime version'ını gösterir
   ```

## Adım 3: CMake Kurulumu

1. [CMake](https://cmake.org/download/) indirin (Windows x64 Installer)
2. Kurulum sırasında "Add CMake to system PATH" seçeneğini işaretleyin
3. Doğrulama:
   ```cmd
   cmake --version
   ```

## Adım 4: OpenCV CUDA Desteği ile Kurulumu

### Seçenek A: Önceden Derlenmiş Sürüm (Hızlı)
1. [OpenCV 4.12.0](https://opencv.org/releases/) indirin
2. `C:\opencv\` klasörüne çıkarın
3. **DİKKAT**: Bu sürüm CUDA desteği olmayabilir

### Seçenek B: Kaynak Koddan Derleme (Önerilen - CUDA Desteği İçin)

#### OpenCV 4.12.0 Kaynak Kodunu İndirin
```cmd
git clone https://github.com/opencv/opencv.git C:\opencv_source\opencv
git clone https://github.com/opencv/opencv_contrib.git C:\opencv_source\opencv_contrib
cd C:\opencv_source\opencv
git checkout 4.12.0
cd C:\opencv_source\opencv_contrib
git checkout 4.12.0
```

#### OpenCV'yi CUDA ile Derleyin
```cmd
mkdir C:\opencv_source\build
cd C:\opencv_source\build

cmake -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX=C:\opencv ^
    -DOPENCV_EXTRA_MODULES_PATH=C:\opencv_source\opencv_contrib\modules ^
    -DWITH_CUDA=ON ^
    -DWITH_CUDNN=ON ^
    -DOPENCV_DNN_CUDA=ON ^
    -DENABLE_FAST_MATH=ON ^
    -DCUDA_FAST_MATH=ON ^
    -DCUDA_ARCH_BIN=8.6 ^
    -DCUDA_ARCH_PTX=8.6 ^
    -DWITH_CUBLAS=ON ^
    -DWITH_CUFFT=ON ^
    -DWITH_NVCUVID=ON ^
    -DWITH_CUDA_RUNTIME_API=ON ^
    -DBUILD_opencv_cudacodec=ON ^
    -DBUILD_opencv_world=OFF ^
    -DBUILD_EXAMPLES=OFF ^
    -DBUILD_TESTS=OFF ^
    -DBUILD_PERF_TESTS=OFF ^
    -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9" ^
    -DCMAKE_GENERATOR_TOOLSET=v142 ^
    C:\opencv_source\opencv

cmake --build . --config Release --parallel
cmake --install .
```

Bu işlem 30-60 dakika sürebilir.

## Adım 5: Proje Kurulumu

1. Proje dosyalarını bir klasöre kopyalayın (örn: `C:\RTSPMonitor\`)
2. Komut satırını yönetici olarak açın
3. Proje klasörüne gidin:
   ```cmd
   cd C:\RTSPMonitor
   ```

4. Ortam kurulumunu kontrol edin:
   ```cmd
   setup_environment.bat
   ```

5. Projeyi derleyin:
   ```cmd
   build.bat
   ```

## Adım 6: Konfigürasyon

### RTSP Stream URL'lerini Ayarlayın
`main.cpp` dosyasında stream URL'lerini güncelleyin:

```cpp
// Example: Enable some streams (modify URLs as needed)
app.enableStream(0, "rtsp://admin:password@192.168.1.100:554/stream1");
app.enableStream(1, "rtsp://admin:password@192.168.1.101:554/stream1");
app.enableStream(2, "rtsp://admin:password@192.168.1.102:554/stream1");
// ... 16 streama kadar ekleyebilirsiniz
```

### Performans Optimizasyonları

#### GPU Bellek Ayarları
NVIDIA Kontrol Paneli'nde:
1. **Manage 3D Settings** → **Global Settings**
2. **CUDA - GPUs** → A4000'i seçin
3. **Power Management Mode** → "Prefer Maximum Performance"

#### Sistem Optimizasyonları
1. **Windows Power Plan**: "High Performance" seçin
2. **Virtual Memory**: 32GB'a ayarlayın
3. **Windows Defender**: Proje klasörünü hariç tutun

## Adım 7: Çalıştırma

1. Executable'ı çalıştırın:
   ```cmd
   cd build\Release
   RTSPMonitor.exe
   ```

2. Konsol arayüzü:
   - `q`: Çıkış
   - `r`: Nesne takibini sıfırla

## Performans İzleme

### GPU Kullanımını İzleyin
```cmd
nvidia-smi -l 1
```

### Sistem Kaynaklarını İzleyin
- **Task Manager** → **Performance** sekmesi
- **GPU Memory**: %80'in altında kalmalı
- **CPU Usage**: %70'in altında kalmalı

## Sorun Giderme

### CUDA Hataları
```cmd
# CUDA cihazlarını kontrol edin
nvidia-smi
# CUDA runtime'ı test edin
cd "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8\extras\demo_suite"
deviceQuery.exe
```

### OpenCV CUDA Hataları
```cpp
// Kod içinde CUDA desteğini kontrol edin
std::cout << "CUDA devices: " << cv::cuda::getCudaEnabledDeviceCount() << std::endl;
```

### Bellek Hataları
- Stream sayısını azaltın
- Frame buffer boyutunu küçültün (`max_size = 2`)
- GPU belleğini monitör edin

### Bağlantı Hataları
- RTSP URL'lerini kontrol edin
- Network latency'sini test edin
- Firewall ayarlarını kontrol edin

## Optimizasyon İpuçları

### 16+ Stream İçin
1. **Stream Resolution**: 1080p ile sınırlayın
2. **Frame Rate**: 25 FPS'e ayarlayın
3. **Buffer Size**: 1 frame ile sınırlayın
4. **Thread Count**: CPU çekirdek sayısına eşit

### GPU Optimizasyonu
```cpp
// GPU bellek havuzu ön ayarı
cv::cuda::setBufferPoolUsage(true);
cv::cuda::setBufferPoolConfig(cv::cuda::getDevice(), 1024 * 1024 * 512, 2);
```

### Network Optimizasyonu
- Gigabit Ethernet kullanın
- Network buffer'ları artırın
- QoS ayarları yapın

## Hata Düzeltmeleri

Bu C++ versiyonunda Python versiyonundaki hatalar düzeltilmiştir:

### 1. ROI Crash Sorunu
- **Sorun**: Sağ tuş ile ROI geçmişinde gezinirken crash
- **Çözüm**: Thread-safe ROI yönetimi ve bounds checking

### 2. Reset Alarm Sorunu
- **Sorun**: Nesne reset sonrası kayıp alarmı
- **Çözüm**: Proper object state management

```cpp
void resetObject(const std::string& name) {
    // Nesneyi düzgün şekilde sıfırla
    obj_it->second.is_active = false;
    obj_it->second.template_image = cv::Mat();
    roi_it->second.resetToDefault();
}
```

## Bakım

### Log Dosyaları
- `alarms.log`: Alarm kayıtları
- Düzenli olarak temizleyin

### Performans İzleme
- GPU sıcaklığını izleyin (<80°C)
- Bellek kullanımını kontrol edin
- Network trafiğini monitör edin

## Destek

Sorun yaşarsanız:
1. Log dosyalarını kontrol edin
2. System Event Viewer'ı inceleyin
3. GPU driver'larını güncelleyin
4. CUDA toolkit'i yeniden kurun

---

Bu kurulum kılavuzu A4000 GPU ve i7 CPU ile 16+ RTSP stream'i sorunsuz işleyebilecek optimized bir sistem oluşturacaktır.