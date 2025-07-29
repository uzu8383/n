import sys
import cv2
import numpy as np
import time
from collections import deque
import threading
import json
import logging
import os
from datetime import datetime
import math
import csv
import subprocess
import re
import copy

# ==============================================================================
# LOGLAMA KURULUMU
# ==============================================================================
def setup_logger():
    log_formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)
    if not logger.handlers:
        file_handler = logging.FileHandler('alarms.log', mode='a', encoding='utf-8')
        file_handler.setFormatter(log_formatter)
        logger.addHandler(file_handler)
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setFormatter(log_formatter)
        logger.addHandler(console_handler)

setup_logger()

# ==============================================================================
# Diğer Importlar
# ==============================================================================
try:
    import winsound
except ImportError:
    logging.warning("'winsound' modülü bu sistemde bulunamadı (sadece Windows'ta mevcuttur). Alarm sesleri çalınmayacak.")
    winsound = None

try:
    import snap7
    from snap7.util import set_real, get_real, set_bool
except ImportError:
    logging.warning("'python-snap7' kütüphanesi bulunamadı. PLC özellikleri çalışmayacak. Yüklemek için: pip install python-snap7")
    snap7 = None

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QPushButton, QVBoxLayout, QHBoxLayout,
    QLabel, QFileDialog, QDialog, QGridLayout, QSlider, QGroupBox,
    QMessageBox, QButtonGroup, QInputDialog, QSizePolicy, QSplitter,
    QTableWidget, QTableWidgetItem, QAbstractItemView, QHeaderView, QSpinBox, QDoubleSpinBox,
    QCheckBox, QLineEdit, QListWidget, QListWidgetItem, QDialogButtonBox,
    QColorDialog
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal, QPoint, QRect, QEvent, QLine
from PyQt6.QtGui import QImage, QPixmap, QPainter, QPen, QColor, QMouseEvent, QIcon, QPolygon, QFont


# === YENİ EKLENEN SINIF: RTSP/Video Yakalama İş Parçacığı ===
class RTSPCaptureThread:
    """
    Her video kaynağını kendi iş parçacığında çalıştırır.
    - Düşük gecikme için tampon boyutunu 1'e ayarlar.
    - Bağlantı kesildiğinde otomatik olarak yeniden bağlanır.
    - Ana iş parçacığının en son kareyi engellenmeden almasını sağlar.
    """
    def __init__(self, source_path, source_id, is_file_source=False):
        self.source_path = source_path
        self.source_id = source_id
        self.is_file_source = is_file_source
        self.stopped = False
        self.capture = None
        self.thread = threading.Thread(target=self.run, args=(), daemon=True)
        self.frame = None
        self.is_running = False # Thread'in başarılı bir şekilde çalışıp çalışmadığını belirtir
        self.frame_lock = threading.Lock()
        self.reconnect_delay = 5  # Yeniden bağlanma gecikmesi (saniye)
        self.last_frame_time = time.time()
        
    def start(self):
        """Thread'i başlatır"""
        self.stopped = False
        if not self.thread.is_alive():
            self.thread = threading.Thread(target=self.run, args=(), daemon=True)
            self.thread.start()
        return self
    
    def stop(self):
        """Thread'i durdurur"""
        self.stopped = True
        if self.capture:
            self.capture.release()
        if self.thread.is_alive():
            self.thread.join(timeout=2.0)
    
    def read(self):
        """En son kareyi döndürür (thread-safe)"""
        with self.frame_lock:
            return self.frame.copy() if self.frame is not None else None
    
    def is_alive(self):
        """Thread'in çalışıp çalışmadığını kontrol eder"""
        return self.thread.is_alive() and self.is_running
    
    def run(self):
        """Ana thread döngüsü"""
        while not self.stopped:
            try:
                self._connect()
                if self.capture and self.capture.isOpened():
                    self.is_running = True
                    self._capture_loop()
                else:
                    logging.warning(f"Kaynak {self.source_id} bağlanamadı, {self.reconnect_delay} saniye sonra tekrar denenecek")
                    time.sleep(self.reconnect_delay)
            except Exception as e:
                logging.error(f"Kaynak {self.source_id} hata: {e}")
                self.is_running = False
                time.sleep(self.reconnect_delay)
            finally:
                if self.capture:
                    self.capture.release()
                    self.capture = None
    
    def _connect(self):
        """Video kaynağına bağlanır"""
        try:
            if self.capture:
                self.capture.release()
            
            self.capture = cv2.VideoCapture(self.source_path)
            
            if not self.is_file_source:
                # RTSP için optimizasyonlar
                self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Düşük gecikme
                self.capture.set(cv2.CAP_PROP_FPS, 25)
            
            if self.capture.isOpened():
                logging.info(f"Kaynak {self.source_id} başarıyla bağlandı: {self.source_path}")
                return True
            else:
                logging.error(f"Kaynak {self.source_id} bağlanamadı: {self.source_path}")
                return False
                
        except Exception as e:
            logging.error(f"Kaynak {self.source_id} bağlantı hatası: {e}")
            return False
    
    def _capture_loop(self):
        """Kare yakalama döngüsü"""
        consecutive_failures = 0
        max_failures = 10
        
        while not self.stopped and self.capture and self.capture.isOpened():
            try:
                ret, frame = self.capture.read()
                
                if ret and frame is not None:
                    with self.frame_lock:
                        self.frame = frame
                    self.last_frame_time = time.time()
                    consecutive_failures = 0
                else:
                    consecutive_failures += 1
                    if consecutive_failures >= max_failures:
                        logging.warning(f"Kaynak {self.source_id}: {max_failures} ardışık başarısız okuma, yeniden bağlanılacak")
                        break
                    time.sleep(0.1)
                    
            except Exception as e:
                logging.error(f"Kaynak {self.source_id} kare okuma hatası: {e}")
                break
        
        self.is_running = False


# === VIDEO WIDGET SINIFI ===
class VideoWidget(QLabel):
    """Video görüntüleme widget'ı"""
    
    # Sinyaller
    mouse_pressed = pyqtSignal(QPoint)
    mouse_moved = pyqtSignal(QPoint)
    mouse_released = pyqtSignal(QPoint)
    
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(320, 240)
        self.setStyleSheet("border: 1px solid gray;")
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setText("Video Yükleniyor...")
        self.setScaledContents(True)
        
        # Mouse tracking
        self.setMouseTracking(True)
        self.mouse_press_pos = None
        
    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.mouse_press_pos = event.position().toPoint()
            self.mouse_pressed.emit(self.mouse_press_pos)
        super().mousePressEvent(event)
    
    def mouseMoveEvent(self, event):
        if self.mouse_press_pos:
            self.mouse_moved.emit(event.position().toPoint())
        super().mouseMoveEvent(event)
    
    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton and self.mouse_press_pos:
            self.mouse_released.emit(event.position().toPoint())
            self.mouse_press_pos = None
        super().mouseReleaseEvent(event)
    
    def update_frame(self, frame):
        """OpenCV frame'ini widget'a gösterir"""
        if frame is not None:
            rgb_image = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            h, w, ch = rgb_image.shape
            bytes_per_line = ch * w
            qt_image = QImage(rgb_image.data, w, h, bytes_per_line, QImage.Format.Format_RGB888)
            pixmap = QPixmap.fromImage(qt_image)
            self.setPixmap(pixmap)


# === ALARM BÖLGE SINIFI ===
class AlarmZone:
    """Alarm bölgesi sınıfı"""
    
    def __init__(self, points, zone_id, name="Alarm Bölgesi", sensitivity=50):
        self.points = points  # Çokgen noktaları
        self.zone_id = zone_id
        self.name = name
        self.sensitivity = sensitivity
        self.is_active = True
        self.color = QColor(255, 0, 0, 100)  # Kırmızı, yarı şeffaf
        self.alarm_active = False
        self.last_alarm_time = 0
        self.alarm_cooldown = 5  # Saniye
        
    def contains_point(self, point):
        """Bir noktanın bölge içinde olup olmadığını kontrol eder"""
        if len(self.points) < 3:
            return False
        
        # Point-in-polygon algoritması
        x, y = point
        n = len(self.points)
        inside = False
        
        p1x, p1y = self.points[0]
        for i in range(1, n + 1):
            p2x, p2y = self.points[i % n]
            if y > min(p1y, p2y):
                if y <= max(p1y, p2y):
                    if x <= max(p1x, p2x):
                        if p1y != p2y:
                            xinters = (y - p1y) * (p2x - p1x) / (p2y - p1y) + p1x
                        if p1x == p2x or x <= xinters:
                            inside = not inside
            p1x, p1y = p2x, p2y
        
        return inside
    
    def check_motion(self, motion_mask):
        """Hareket maskesinde bu bölgede hareket olup olmadığını kontrol eder"""
        if not self.is_active or len(self.points) < 3:
            return False
        
        # Bölge maskesi oluştur
        mask = np.zeros(motion_mask.shape, dtype=np.uint8)
        cv2.fillPoly(mask, [np.array(self.points, dtype=np.int32)], 255)
        
        # Bölgedeki hareket piksellerini say
        motion_in_zone = cv2.bitwise_and(motion_mask, mask)
        motion_pixels = cv2.countNonZero(motion_in_zone)
        
        # Toplam bölge piksellerini hesapla
        total_pixels = cv2.countNonZero(mask)
        
        if total_pixels == 0:
            return False
        
        # Hareket yüzdesini hesapla
        motion_percentage = (motion_pixels / total_pixels) * 100
        
        # Alarm kontrolü
        current_time = time.time()
        if motion_percentage > self.sensitivity:
            if not self.alarm_active and (current_time - self.last_alarm_time) > self.alarm_cooldown:
                self.alarm_active = True
                self.last_alarm_time = current_time
                return True
        else:
            self.alarm_active = False
        
        return False


# === HAREKET ALGILAMA SINIFI ===
class MotionDetector:
    """Hareket algılama sınıfı"""
    
    def __init__(self):
        self.background_subtractor = cv2.createBackgroundSubtractorMOG2(
            detectShadows=True,
            varThreshold=50,
            history=500
        )
        self.kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
        self.min_contour_area = 500
        self.max_contour_area = 50000
        
    def detect(self, frame):
        """Hareket algılama"""
        if frame is None:
            return None, []
        
        # Arka plan çıkarma
        fg_mask = self.background_subtractor.apply(frame)
        
        # Gürültü temizleme
        fg_mask = cv2.morphologyEx(fg_mask, cv2.MORPH_OPEN, self.kernel)
        fg_mask = cv2.morphologyEx(fg_mask, cv2.MORPH_CLOSE, self.kernel)
        
        # Konturları bul
        contours, _ = cv2.findContours(fg_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        # Geçerli konturları filtrele
        valid_contours = []
        for contour in contours:
            area = cv2.contourArea(contour)
            if self.min_contour_area < area < self.max_contour_area:
                valid_contours.append(contour)
        
        return fg_mask, valid_contours


# === ANA UYGULAMA SINIFI ===
class VideoSurveillanceApp(QMainWindow):
    """Ana video gözetim uygulaması"""
    
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Video Gözetim Sistemi")
        self.setGeometry(100, 100, 1200, 800)
        
        # Video kaynakları
        self.video_sources = []
        self.capture_threads = []
        
        # Hareket algılama
        self.motion_detectors = []
        self.alarm_zones = []
        
        # UI bileşenleri
        self.video_widgets = []
        self.is_recording = False
        self.video_writers = []
        
        # Timer
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_frames)
        self.timer.start(33)  # ~30 FPS
        
        self.setup_ui()
        self.setup_default_sources()
    
    def setup_ui(self):
        """Kullanıcı arayüzünü oluşturur"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # Ana layout
        main_layout = QVBoxLayout(central_widget)
        
        # Kontrol paneli
        control_panel = self.create_control_panel()
        main_layout.addWidget(control_panel)
        
        # Video grid
        self.video_grid = QGridLayout()
        video_widget = QWidget()
        video_widget.setLayout(self.video_grid)
        main_layout.addWidget(video_widget)
        
        # 4 video widget oluştur
        for i in range(4):
            video_widget = VideoWidget()
            video_widget.mouse_pressed.connect(lambda pos, idx=i: self.on_video_clicked(pos, idx))
            self.video_widgets.append(video_widget)
            row, col = divmod(i, 2)
            self.video_grid.addWidget(video_widget, row, col)
    
    def create_control_panel(self):
        """Kontrol panelini oluşturur"""
        panel = QGroupBox("Kontrol Paneli")
        layout = QHBoxLayout(panel)
        
        # Video kaynak ekleme
        add_source_btn = QPushButton("Video Kaynağı Ekle")
        add_source_btn.clicked.connect(self.add_video_source)
        layout.addWidget(add_source_btn)
        
        # Kayıt başlat/durdur
        self.record_btn = QPushButton("Kayıt Başlat")
        self.record_btn.clicked.connect(self.toggle_recording)
        layout.addWidget(self.record_btn)
        
        # Alarm bölgesi ekleme
        add_zone_btn = QPushButton("Alarm Bölgesi Ekle")
        add_zone_btn.clicked.connect(self.add_alarm_zone)
        layout.addWidget(add_zone_btn)
        
        # Ayarlar
        settings_btn = QPushButton("Ayarlar")
        settings_btn.clicked.connect(self.show_settings)
        layout.addWidget(settings_btn)
        
        return panel
    
    def setup_default_sources(self):
        """Varsayılan video kaynaklarını ayarlar"""
        # Webcam'i ekle
        self.add_video_source_internal("0", is_camera=True)
    
    def add_video_source(self):
        """Video kaynağı ekleme dialogu"""
        source, ok = QInputDialog.getText(self, "Video Kaynağı", 
                                        "Video kaynağı (dosya yolu, RTSP URL veya kamera indeksi):")
        if ok and source:
            self.add_video_source_internal(source)
    
    def add_video_source_internal(self, source_path, is_camera=False):
        """Video kaynağını dahili olarak ekler"""
        if len(self.video_sources) >= 4:
            QMessageBox.warning(self, "Uyarı", "Maksimum 4 video kaynağı desteklenmektedir.")
            return
        
        try:
            # Kamera indeksi kontrolü
            if source_path.isdigit():
                source_path = int(source_path)
                is_file_source = False
            elif source_path.lower().startswith(('rtsp://', 'http://', 'https://')):
                is_file_source = False
            else:
                is_file_source = True
            
            # Capture thread oluştur
            capture_thread = RTSPCaptureThread(source_path, len(self.video_sources), is_file_source)
            capture_thread.start()
            
            # Hareket algılayıcı oluştur
            motion_detector = MotionDetector()
            
            # Listelere ekle
            self.video_sources.append(source_path)
            self.capture_threads.append(capture_thread)
            self.motion_detectors.append(motion_detector)
            
            logging.info(f"Video kaynağı eklendi: {source_path}")
            
        except Exception as e:
            QMessageBox.critical(self, "Hata", f"Video kaynağı eklenirken hata: {e}")
            logging.error(f"Video kaynağı ekleme hatası: {e}")
    
    def update_frames(self):
        """Video karelerini günceller"""
        for i, (capture_thread, motion_detector, video_widget) in enumerate(
            zip(self.capture_threads, self.motion_detectors, self.video_widgets)):
            
            if capture_thread.is_alive():
                frame = capture_thread.read()
                if frame is not None:
                    # Hareket algılama
                    motion_mask, contours = motion_detector.detect(frame)
                    
                    # Alarm bölgesi kontrolü
                    self.check_alarm_zones(motion_mask, i)
                    
                    # Konturları çiz
                    if contours:
                        cv2.drawContours(frame, contours, -1, (0, 255, 0), 2)
                    
                    # Alarm bölgelerini çiz
                    self.draw_alarm_zones(frame, i)
                    
                    # Widget'ı güncelle
                    video_widget.update_frame(frame)
                    
                    # Kayıt
                    if self.is_recording and i < len(self.video_writers):
                        if self.video_writers[i] is not None:
                            self.video_writers[i].write(frame)
            else:
                # Bağlantı kaybı mesajı
                video_widget.setText(f"Kaynak {i+1}\nBağlantı Kesildi")
    
    def check_alarm_zones(self, motion_mask, source_index):
        """Alarm bölgelerini kontrol eder"""
        if motion_mask is None:
            return
        
        for zone in self.alarm_zones:
            if zone.check_motion(motion_mask):
                self.trigger_alarm(zone, source_index)
    
    def draw_alarm_zones(self, frame, source_index):
        """Alarm bölgelerini frame üzerine çizer"""
        for zone in self.alarm_zones:
            if len(zone.points) >= 3:
                points = np.array(zone.points, dtype=np.int32)
                color = (0, 0, 255) if zone.alarm_active else (255, 0, 0)
                cv2.polylines(frame, [points], True, color, 2)
                
                # Bölge adını yaz
                if len(zone.points) > 0:
                    x, y = zone.points[0]
                    cv2.putText(frame, zone.name, (x, y-10), 
                              cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 1)
    
    def trigger_alarm(self, zone, source_index):
        """Alarm tetiklenir"""
        alarm_msg = f"ALARM: {zone.name} - Kaynak {source_index + 1}"
        logging.warning(alarm_msg)
        
        # Ses çal (Windows'ta)
        if winsound:
            try:
                winsound.Beep(1000, 500)  # 1000Hz, 500ms
            except:
                pass
        
        # Kayıt başlat (eğer başlamamışsa)
        if not self.is_recording:
            self.start_recording()
    
    def add_alarm_zone(self):
        """Alarm bölgesi ekleme dialogu"""
        if not self.video_sources:
            QMessageBox.warning(self, "Uyarı", "Önce bir video kaynağı ekleyin.")
            return
        
        name, ok = QInputDialog.getText(self, "Alarm Bölgesi", "Bölge adı:")
        if ok and name:
            # Basit bir dörtgen bölge oluştur (örnek)
            points = [(100, 100), (300, 100), (300, 200), (100, 200)]
            zone = AlarmZone(points, len(self.alarm_zones), name)
            self.alarm_zones.append(zone)
            logging.info(f"Alarm bölgesi eklendi: {name}")
    
    def toggle_recording(self):
        """Kayıt başlat/durdur"""
        if not self.is_recording:
            self.start_recording()
        else:
            self.stop_recording()
    
    def start_recording(self):
        """Kayıt başlat"""
        if not self.video_sources:
            QMessageBox.warning(self, "Uyarı", "Kayıt için video kaynağı gerekli.")
            return
        
        try:
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            
            for i, capture_thread in enumerate(self.capture_threads):
                if capture_thread.is_alive():
                    frame = capture_thread.read()
                    if frame is not None:
                        h, w = frame.shape[:2]
                        filename = f"kayit_kaynak{i+1}_{timestamp}.avi"
                        fourcc = cv2.VideoWriter_fourcc(*'XVID')
                        writer = cv2.VideoWriter(filename, fourcc, 20.0, (w, h))
                        self.video_writers.append(writer)
                    else:
                        self.video_writers.append(None)
                else:
                    self.video_writers.append(None)
            
            self.is_recording = True
            self.record_btn.setText("Kayıt Durdur")
            logging.info("Kayıt başlatıldı")
            
        except Exception as e:
            QMessageBox.critical(self, "Hata", f"Kayıt başlatılırken hata: {e}")
            logging.error(f"Kayıt başlatma hatası: {e}")
    
    def stop_recording(self):
        """Kayıt durdur"""
        for writer in self.video_writers:
            if writer:
                writer.release()
        
        self.video_writers.clear()
        self.is_recording = False
        self.record_btn.setText("Kayıt Başlat")
        logging.info("Kayıt durduruldu")
    
    def on_video_clicked(self, pos, video_index):
        """Video widget'ına tıklandığında"""
        logging.info(f"Video {video_index + 1} tıklandı: {pos}")
    
    def show_settings(self):
        """Ayarlar dialogu"""
        QMessageBox.information(self, "Ayarlar", "Ayarlar penceresi henüz geliştirilmemiştir.")
    
    def closeEvent(self, event):
        """Uygulama kapatılırken"""
        # Kayıt durdur
        if self.is_recording:
            self.stop_recording()
        
        # Capture thread'leri durdur
        for capture_thread in self.capture_threads:
            capture_thread.stop()
        
        # Timer'ı durdur
        self.timer.stop()
        
        logging.info("Uygulama kapatıldı")
        event.accept()


# === ANA PROGRAM ===
def main():
    """Ana program"""
    app = QApplication(sys.argv)
    app.setApplicationName("Video Gözetim Sistemi")
    
    # Ana pencereyi oluştur ve göster
    window = VideoSurveillanceApp()
    window.show()
    
    # Uygulamayı çalıştır
    sys.exit(app.exec())


if __name__ == "__main__":
    main()