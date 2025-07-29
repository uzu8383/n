# Video Gözetim Sistemi

Bu proje, Python ve PyQt6 kullanılarak geliştirilmiş gelişmiş bir video gözetim sistemidir. Sistem, RTSP kameralar, web kameralar ve video dosyaları ile çalışabilir, hareket algılama, alarm bölgeleri ve otomatik kayıt özelliklerine sahiptir.

## Özellikler

### 🎥 Video Kaynakları
- **Çoklu Video Desteği**: Aynı anda 4 farklı video kaynağını destekler
- **RTSP Kameralar**: IP kameralardan canlı yayın
- **Web Kameralar**: USB kameralar (0, 1, 2, vb.)
- **Video Dosyaları**: MP4, AVI ve diğer formatlar
- **Otomatik Yeniden Bağlanma**: Bağlantı kesildiğinde otomatik yeniden bağlanır

### 🚨 Hareket Algılama
- **Gelişmiş Algoritma**: MOG2 background subtraction kullanır
- **Gürültü Filtreleme**: Morphological operations ile temizleme
- **Ayarlanabilir Hassasiyet**: Kontür alanı filtreleme
- **Görsel Gösterim**: Hareket eden nesnelerin etrafına yeşil çerçeve

### 🎯 Alarm Bölgeleri
- **Özel Bölgeler**: Çokgen şeklinde alarm bölgeleri tanımlama
- **Hassasiyet Ayarı**: Bölge bazında hassasiyet kontrolü
- **Cooldown Sistemi**: Spam alarmları önlemek için bekleme süresi
- **Görsel Feedback**: Aktif alarmlar kırmızı, pasif olanlar mavi

### 📹 Kayıt Sistemi
- **Otomatik Kayıt**: Alarm tetiklendiğinde otomatik kayıt başlar
- **Manuel Kontrol**: Kayıt başlat/durdur butonları
- **Çoklu Kaynak**: Her video kaynağı ayrı dosyaya kaydedilir
- **Zaman Damgası**: Dosya adlarında tarih/saat bilgisi

### 🔧 Teknik Özellikler
- **Thread-Safe**: Her video kaynağı ayrı thread'de çalışır
- **Düşük Gecikme**: RTSP için buffer optimizasyonu
- **Hata Yönetimi**: Kapsamlı exception handling
- **Logging**: Detaylı log kayıtları
- **PLC Desteği**: Snap7 ile Siemens PLC entegrasyonu (opsiyonel)

## Kurulum

### Gereksinimler
- Python 3.8 veya üzeri
- OpenCV 4.8+
- PyQt6 6.5+
- NumPy 1.24+

### Adım 1: Depoyu Klonlayın
```bash
git clone <repository-url>
cd video-surveillance-system
```

### Adım 2: Sanal Ortam Oluşturun (Önerilen)
```bash
python -m venv venv
source venv/bin/activate  # Linux/Mac
# veya
venv\Scripts\activate  # Windows
```

### Adım 3: Bağımlılıkları Yükleyin
```bash
pip install -r requirements.txt
```

### Adım 4: Uygulamayı Çalıştırın
```bash
python video_surveillance_system.py
```

## Kullanım

### Video Kaynağı Ekleme
1. **"Video Kaynağı Ekle"** butonuna tıklayın
2. Aşağıdaki formatlardan birini girin:
   - Web kamerası: `0`, `1`, `2` (kamera indeksi)
   - RTSP URL: `rtsp://username:password@ip:port/path`
   - HTTP stream: `http://ip:port/stream`
   - Video dosyası: `/path/to/video.mp4`

### Alarm Bölgesi Oluşturma
1. **"Alarm Bölgesi Ekle"** butonuna tıklayın
2. Bölge adını girin
3. Sistem otomatik olarak örnek bir dörtgen bölge oluşturur
4. Bölge parametreleri kod üzerinden özelleştirilebilir

### Kayıt İşlemleri
- **Manuel Kayıt**: "Kayıt Başlat" butonuna tıklayın
- **Otomatik Kayıt**: Alarm tetiklendiğinde otomatik başlar
- Kayıtlar `kayit_kaynak{N}_{timestamp}.avi` formatında kaydedilir

## Yapılandırma

### Hareket Algılama Ayarları
```python
# MotionDetector sınıfında
self.min_contour_area = 500      # Minimum hareket alanı
self.max_contour_area = 50000    # Maksimum hareket alanı
self.background_subtractor = cv2.createBackgroundSubtractorMOG2(
    detectShadows=True,
    varThreshold=50,             # Hassasiyet
    history=500                  # Geçmiş frame sayısı
)
```

### Alarm Bölgesi Ayarları
```python
# AlarmZone sınıfında
self.sensitivity = 50            # Hassasiyet (0-100)
self.alarm_cooldown = 5          # Cooldown süresi (saniye)
```

### RTSP Optimizasyonları
```python
# RTSPCaptureThread sınıfında
self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Düşük gecikme
self.capture.set(cv2.CAP_PROP_FPS, 25)        # FPS ayarı
self.reconnect_delay = 5                       # Yeniden bağlanma gecikmesi
```

## Dosya Yapısı

```
video-surveillance-system/
├── video_surveillance_system.py  # Ana uygulama
├── requirements.txt               # Python bağımlılıkları
├── README.md                     # Bu dosya
├── alarms.log                    # Log dosyası (otomatik oluşur)
└── kayit_*.avi                   # Kayıt dosyaları (otomatik oluşur)
```

## Sınıf Yapısı

### RTSPCaptureThread
- Video yakalama işlemlerini ayrı thread'de yönetir
- Otomatik yeniden bağlanma özelliği
- Thread-safe frame okuma

### VideoWidget
- PyQt6 QLabel tabanlı video görüntüleme
- Mouse event'leri destekler
- OpenCV frame'lerini Qt formatına dönüştürür

### AlarmZone
- Çokgen şeklinde alarm bölgeleri
- Point-in-polygon algoritması
- Hareket yüzdesi hesaplama

### MotionDetector
- MOG2 background subtraction
- Morphological operations ile gürültü temizleme
- Kontür filtreleme

### VideoSurveillanceApp
- Ana uygulama sınıfı
- UI yönetimi
- Video kaynakları koordinasyonu

## Troubleshooting

### Kamera Bağlanamıyor
- Kamera indeksini kontrol edin (0, 1, 2...)
- Başka uygulamanın kamerayı kullanmadığından emin olun
- USB bağlantısını kontrol edin

### RTSP Bağlantı Sorunları
- URL formatını kontrol edin: `rtsp://user:pass@ip:port/path`
- Ağ bağlantısını test edin
- Kamera ayarlarını kontrol edin

### Performans Sorunları
- Video çözünürlüğünü düşürün
- FPS değerini azaltın
- Buffer boyutunu ayarlayın

## Geliştirme

### Yeni Özellik Ekleme
1. İlgili sınıfı bulun veya yeni sınıf oluşturun
2. Gerekli metotları implement edin
3. UI'a entegre edin
4. Test edin

### Debug Modu
```python
# Logging seviyesini DEBUG'a ayarlayın
logger.setLevel(logging.DEBUG)
```

## Lisans

Bu proje MIT lisansı altında yayınlanmıştır.

## Katkıda Bulunma

1. Fork yapın
2. Feature branch oluşturun (`git checkout -b feature/amazing-feature`)
3. Commit yapın (`git commit -m 'Add amazing feature'`)
4. Branch'i push edin (`git push origin feature/amazing-feature`)
5. Pull Request oluşturun

## Destek

Sorularınız için:
- Issue açın
- Email: [your-email@example.com]
- Documentation: [wiki-link]