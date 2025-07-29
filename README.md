# Gelişmiş Gözetleme Sistemi (Advanced Surveillance System)

Bu proje, OpenCV ve PyQt6 kullanılarak geliştirilmiş kapsamlı bir video gözetleme sistemidir. RTSP kameraları, dosya tabanlı videolar ve PLC entegrasyonu ile çalışır.

## Özellikler

### 🎥 Video İşleme
- **Çoklu Video Kaynağı**: RTSP kameraları ve video dosyaları desteği
- **Düşük Gecikme**: Thread tabanlı video yakalama
- **Otomatik Yeniden Bağlanma**: Bağlantı koptuğunda otomatik yeniden bağlanır
- **Gerçek Zamanlı Görüntüleme**: ~30 FPS ile akıcı video görüntüleme

### 🔍 Hareket Tespiti
- **Gelişmiş Algoritma**: MOG2 background subtractor kullanır
- **Özelleştirilebilir Bölgeler**: Fare ile çizilebilir tespit bölgeleri
- **Gürültü Filtreleme**: Morfological işlemlerle false alarm azaltma
- **Görsel Feedback**: Tespit edilen hareketlerin görsel gösterimi

### 🚨 Alarm Sistemi
- **Çoklu Alarm Türü**: Hareket tespiti, izinsiz giriş vb.
- **Ses Alarmı**: Windows sistemlerde ses uyarısı
- **Log Kayıtları**: Tüm alarmların detaylı kaydı
- **PLC Entegrasyonu**: Endüstriyel sistemlerle entegrasyon

### 🔧 PLC Kontrolü
- **Siemens S7 Desteği**: python-snap7 kütüphanesi ile
- **Boolean I/O**: PLC'ye alarm sinyalleri gönderme
- **Gerçek Zamanlı İletişim**: Anlık veri alışverişi

## Kurulum

### Gereksinimler
- Python 3.8+
- OpenCV
- PyQt6
- NumPy
- python-snap7 (PLC için opsiyonel)

### Kurulum Adımları

1. **Depoyu klonlayın:**
```bash
git clone <repository-url>
cd surveillance-system
```

2. **Sanal ortam oluşturun (önerilir):**
```bash
python -m venv venv
source venv/bin/activate  # Linux/Mac
# veya
venv\Scripts\activate     # Windows
```

3. **Bağımlılıkları yükleyin:**
```bash
pip install -r requirements.txt
```

4. **Uygulamayı çalıştırın:**
```bash
python surveillance_system.py
```

## Kullanım

### Video Kaynağı Ekleme
1. **"Video Kaynağı Ekle"** butonuna tıklayın
2. RTSP URL'sini veya video dosya yolunu girin:
   - RTSP örneği: `rtsp://admin:password@192.168.1.100:554/stream1`
   - Dosya örneği: `/path/to/video.mp4`
3. Video otomatik olarak yüklenecektir

### Tespit Bölgesi Oluşturma
1. **"Bölge Çiz"** butonuna tıklayın
2. Video üzerinde fare ile noktaları işaretleyin
3. Çift tıklayarak bölgeyi tamamlayın
4. Bölge adını girin

### PLC Bağlantısı
1. **"PLC'ye Bağlan"** butonuna tıklayın
2. PLC IP adresini girin (varsayılan: 192.168.1.100)
3. Bağlantı durumu gösterilecektir

### Alarm Yönetimi
- **Alarmları Etkinleştir**: Checkbox ile alarm sistemini açın/kapatın
- **Alarm Testi**: Manuel alarm testi yapın
- Tüm alarmlar `alarms.log` dosyasına kaydedilir

## Teknik Detaylar

### Sınıf Yapısı

#### `RTSPCaptureThread`
- Thread tabanlı video yakalama
- Düşük gecikme için buffer optimizasyonu
- Otomatik yeniden bağlanma mekanizması

#### `MotionDetector`
- MOG2 background subtraction
- Kontour tabanlı hareket analizi
- Bölge bazlı tespit filtreleme

#### `DetectionZone`
- Poligon tabanlı bölge tanımı
- Point-in-polygon algoritması
- Tespit istatistikleri

#### `PLCController`
- Siemens S7 PLC iletişimi
- Boolean ve Real değer okuma/yazma
- Bağlantı yönetimi

#### `VideoDisplayWidget`
- PyQt6 tabanlı video gösterimi
- İnteraktif bölge çizimi
- Gerçek zamanlı güncelleme

### Performans Optimizasyonları
- **Thread Pool**: Her video kaynağı ayrı thread'de
- **Buffer Management**: Minimum gecikme için buffer=1
- **Memory Management**: Frame kopyalama optimizasyonu
- **CPU Usage**: Adaptive sleep intervals

## Yapılandırma

### Video Ayarları
```python
# RTSPCaptureThread sınıfında
self.reconnect_delay = 5  # Yeniden bağlanma gecikmesi
capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Buffer boyutu
```

### Hareket Tespiti Ayarları
```python
# MotionDetector sınıfında
self.motion_threshold = 1000  # Hareket eşiği
self.min_contour_area = 500   # Minimum kontour alanı
```

### PLC Ayarları
```python
# PLCController sınıfında
self.ip_address = "192.168.1.100"  # PLC IP adresi
self.rack = 0                      # Rack numarası
self.slot = 1                      # Slot numarası
```

## Sorun Giderme

### Yaygın Sorunlar

1. **Video açılmıyor:**
   - RTSP URL'sinin doğruluğunu kontrol edin
   - Ağ bağlantısını kontrol edin
   - Kamera kimlik bilgilerini doğrulayın

2. **PLC bağlanamıyor:**
   - IP adresinin doğruluğunu kontrol edin
   - Ağ bağlantısını test edin
   - python-snap7 kütüphanesinin yüklü olduğundan emin olun

3. **Hareket tespiti çalışmıyor:**
   - Tespit bölgelerinin doğru çizildiğini kontrol edin
   - Hassasiyet ayarlarını kontrol edin
   - Aydınlatma koşullarını kontrol edin

### Log Dosyaları
- **alarms.log**: Tüm alarm kayıtları
- **Console Output**: Gerçek zamanlı sistem mesajları

## Geliştirme

### Yeni Özellik Ekleme
1. İlgili sınıfı genişletin
2. UI bileşenlerini ekleyin
3. Signal/slot bağlantılarını yapın
4. Test edin

### Katkıda Bulunma
1. Fork yapın
2. Feature branch oluşturun
3. Değişikliklerinizi commit edin
4. Pull request gönderin

## Lisans

Bu proje MIT lisansı altında lisanslanmıştır.

## İletişim

Sorularınız ve önerileriniz için issue açabilirsiniz.

---

**Not**: Bu sistem endüstriyel kullanım için tasarlanmıştır. Güvenlik kritik uygulamalarda ek testler yapılması önerilir.