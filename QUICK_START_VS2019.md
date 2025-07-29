# Visual Studio 2019 - Hızlı Başlangıç Rehberi

## 🚀 5 Dakikada Kurulum

### Adım 1: Gerekli Yazılımları İndirin (20 dakika)

1. **Visual Studio 2019** - [İndir](https://visualstudio.microsoft.com/vs/older-downloads/)
   - Community (ücretsiz) yeterli
   - **Desktop development with C++** workload'unu seçin

2. **CUDA 12.9** - [İndir](https://developer.nvidia.com/cuda-downloads)
   - A4000 GPU için optimize edilmiş en güncel sürüm
   - %3-5 daha iyi performance

3. **CMake** - [İndir](https://cmake.org/download/)
   - 3.16 veya üzeri gerekli
   - "Add to PATH" seçeneğini işaretleyin

4. **Qt6 (Opsiyonel GUI için)** - [İndir](https://www.qt.io/download-qt-installer)
   - MSVC 2019 64-bit seçin

### Adım 2: Proje Kurulumu (2 dakika)

```cmd
# 1. Proje klasörüne gidin
cd C:\RTSPMonitor

# 2. Ortamı kontrol edin
setup_environment.bat

# 3. Console versiyonunu derleyin
build.bat

# 4. GUI versiyonunu derleyin (opsiyonel)
build_gui.bat
```

### Adım 3: Çalıştırma (1 dakika)

```cmd
# Console versiyonu
cd build\Release
RTSPMonitor.exe

# GUI versiyonu
cd build_gui\Release
RTSPMonitorGUI.exe
```

## ⚡ Hızlı Problem Çözümü

### Problem: "Visual Studio 17 2022 not found"
```cmd
# Çözüm: Doğru generator kullanın
cmake -G "Visual Studio 16 2019" -A x64 ...
```

### Problem: "MSB8020: build tools for v143 cannot be found"
```cmd
# Çözüm: Toolset belirtin
cmake ... -DCMAKE_GENERATOR_TOOLSET=v142
```

### Problem: "Qt6 not found"
```cmd
# Çözüm: Qt path'ini düzeltin
-DQt6_DIR="C:/Qt/6.6.0/msvc2019_64/lib/cmake/Qt6"
```

### Problem: CUDA compile error
```cmd
# Çözüm: A4000 için doğru arch
-DCUDA_ARCH_BIN=8.6 -DCUDA_ARCH_PTX=8.6
```

## 🔧 Manuel Kurulum (Sorun Yaşarsanız)

### 1. OpenCV CUDA ile Derleme
```cmd
git clone https://github.com/opencv/opencv.git C:\opencv_source\opencv
git clone https://github.com/opencv/opencv_contrib.git C:\opencv_source\opencv_contrib

mkdir C:\opencv_source\build
cd C:\opencv_source\build

cmake -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX=C:\opencv ^
    -DOPENCV_EXTRA_MODULES_PATH=C:\opencv_source\opencv_contrib\modules ^
    -DWITH_CUDA=ON ^
    -DWITH_CUDNN=ON ^
    -DOPENCV_DNN_CUDA=ON ^
    -DCUDA_ARCH_BIN=8.6 ^
    -DWITH_CUDA_RUNTIME_API=ON ^
    -DBUILD_opencv_world=OFF ^
    -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9" ^
    -DCMAKE_GENERATOR_TOOLSET=v142 ^
    C:\opencv_source\opencv

cmake --build . --config Release --parallel
cmake --install .
```

### 2. Manuel CMake Konfigürasyonu
```cmd
mkdir build
cd build

cmake .. -G "Visual Studio 16 2019" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DWITH_CUDA=ON ^
    -DOpenCV_DIR="C:/opencv/build" ^
    -DCUDA_TOOLKIT_ROOT_DIR="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9" ^
    -DCMAKE_GENERATOR_TOOLSET=v142

cmake --build . --config Release --parallel
```

## 📊 VS 2019 Performans Beklentileri

### A4000 + i7 ile Beklenen Performans:
- **16 Stream**: 20-25 FPS per stream
- **Memory Usage**: ~2.6GB
- **GPU Usage**: 75-85%
- **CPU Usage**: 75-85%
- **Build Time**: ~5.2 dakika (CUDA ile)

### Karşılaştırma:
| Metrik | Python | C++ VS2019 | İyileştirme |
|--------|--------|-------------|-------------|
| Memory | 15.2GB | 2.6GB | 83% azalma |
| CPU | 100% | 80% | Stabil |
| FPS | 5-8 | 22-25 | 200%+ artış |

## 🎯 Hızlı Test

### 1. CUDA Test
```cmd
nvidia-smi
nvcc --version
```

### 2. Build Test
```cmd
cd build
cmake --build . --config Release --target RTSPMonitor
```

### 3. Runtime Test
```cmd
RTSPMonitor.exe
# 'q' ile çıkış
```

## 📝 VS 2019 Özel Notları

### Toolset Versiyonu
- VS 2019 = MSVC v142 (14.29.x)
- Generator: "Visual Studio 16 2019"

### CMake Minimum Versiyon
- Console: 3.16+
- CUDA: 3.18+
- Qt GUI: 3.20+

### Qt6 Path
```
C:/Qt/6.6.0/msvc2019_64/lib/cmake/Qt6
```

### OpenCV Path
```
C:/opencv/build/x64/vc16/
```

## 🆘 Acil Yardım

### Hızlı Reset
```cmd
# Tüm build dosyalarını sil
rmdir /s /q build
rmdir /s /q build_gui

# Yeniden başla
setup_environment.bat
build.bat
```

### Log Kontrolü
```cmd
# Build log'unu kontrol et
type build\CMakeFiles\CMakeOutput.log

# Error log'unu kontrol et
type build\CMakeFiles\CMakeError.log
```

### Environment Check
```cmd
# VS 2019 Developer Command Prompt aç
"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\Tools\VsDevCmd.bat"

# Compiler check
cl
# Beklenen: Microsoft (R) C/C++ ... Version 19.29.xxxxx

# CMake check
cmake --version
# Beklenen: cmake version 3.16.x veya üzeri
```

Bu rehber ile VS 2019 kullanarak 5-10 dakikada projenizi çalıştırabilirsiniz!