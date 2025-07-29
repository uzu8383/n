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
        self.is_running = False
        self.frame_count = 0
        self.fps = 0
        self.last_fps_time = time.time()
        self.connection_retry_count = 0
        self.max_retries = 5
        self.retry_delay = 2.0
        
    def start(self):
        """İş parçacığını başlatır"""
        if not self.thread.is_alive():
            self.stopped = False
            self.thread.start()
            logging.info(f"Video kaynağı {self.source_id} başlatıldı: {self.source_path}")
            
    def stop(self):
        """İş parçacığını durdurur"""
        self.stopped = True
        if self.capture:
            self.capture.release()
        if self.thread.is_alive():
            self.thread.join(timeout=2.0)
        logging.info(f"Video kaynağı {self.source_id} durduruldu")
        
    def get_frame(self):
        """En son kareyi döndürür (thread-safe)"""
        return self.frame.copy() if self.frame is not None else None
        
    def get_fps(self):
        """Mevcut FPS değerini döndürür"""
        return self.fps
        
    def run(self):
        """Ana yakalama döngüsü"""
        while not self.stopped:
            try:
                if not self.capture or not self.capture.isOpened():
                    if not self._connect_to_source():
                        time.sleep(self.retry_delay)
                        continue
                        
                ret, frame = self.capture.read()
                if ret:
                    self.frame = frame
                    self.frame_count += 1
                    
                    # FPS hesaplama
                    current_time = time.time()
                    if current_time - self.last_fps_time >= 1.0:
                        self.fps = self.frame_count / (current_time - self.last_fps_time)
                        self.frame_count = 0
                        self.last_fps_time = current_time
                        self.connection_retry_count = 0  # Başarılı okuma, retry sayacını sıfırla
                else:
                    # Kare okunamadı, bağlantıyı yeniden kur
                    logging.warning(f"Video kaynağı {self.source_id} kare okunamadı, yeniden bağlanılıyor...")
                    self.capture.release()
                    self.capture = None
                    time.sleep(0.1)
                    
            except Exception as e:
                logging.error(f"Video kaynağı {self.source_id} hatası: {str(e)}")
                if self.capture:
                    self.capture.release()
                    self.capture = None
                time.sleep(self.retry_delay)
                
        self.is_running = False
        
    def _connect_to_source(self):
        """Video kaynağına bağlanır"""
        try:
            if self.connection_retry_count >= self.max_retries:
                logging.error(f"Video kaynağı {self.source_id} maksimum yeniden deneme sayısına ulaştı")
                return False
                
            self.connection_retry_count += 1
            
            if self.is_file_source:
                self.capture = cv2.VideoCapture(self.source_path)
            else:
                # RTSP için özel ayarlar
                self.capture = cv2.VideoCapture(self.source_path)
                self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)  # Tampon boyutunu 1'e ayarla
                self.capture.set(cv2.CAP_PROP_FPS, 30)  # FPS'i 30'a ayarla
                
            if self.capture.isOpened():
                logging.info(f"Video kaynağı {self.source_id} başarıyla bağlandı")
                self.connection_retry_count = 0
                return True
            else:
                logging.warning(f"Video kaynağı {self.source_id} bağlanamadı (deneme {self.connection_retry_count}/{self.max_retries})")
                return False
                
        except Exception as e:
            logging.error(f"Video kaynağı {self.source_id} bağlantı hatası: {str(e)}")
            return False


# === HAREKET ALGILAMA SINIFI ===
class MotionDetector:
    def __init__(self):
        self.background_subtractor = cv2.createBackgroundSubtractorMOG2(
            history=500, varThreshold=16, detectShadows=False
        )
        self.kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        self.min_area = 500
        self.sensitivity = 0.3
        
    def detect_motion(self, frame):
        """Hareket algılama"""
        if frame is None:
            return False, None, 0
            
        # Arka plan çıkarma
        fg_mask = self.background_subtractor.apply(frame)
        
        # Gürültü azaltma
        fg_mask = cv2.morphologyEx(fg_mask, cv2.MORPH_OPEN, self.kernel)
        fg_mask = cv2.morphologyEx(fg_mask, cv2.MORPH_CLOSE, self.kernel)
        
        # Konturları bul
        contours, _ = cv2.findContours(fg_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        motion_detected = False
        motion_area = 0
        
        for contour in contours:
            area = cv2.contourArea(contour)
            if area > self.min_area:
                motion_detected = True
                motion_area += area
                
        # Hassasiyet ayarı
        if motion_area > 0:
            frame_area = frame.shape[0] * frame.shape[1]
            motion_ratio = motion_area / frame_area
            motion_detected = motion_ratio > self.sensitivity
            
        return motion_detected, fg_mask, motion_area
        
    def set_sensitivity(self, sensitivity):
        """Hassasiyet ayarı (0.1 - 1.0)"""
        self.sensitivity = max(0.1, min(1.0, sensitivity))
        
    def set_min_area(self, min_area):
        """Minimum hareket alanı ayarı"""
        self.min_area = max(100, min_area)


# === ALARM YÖNETİCİSİ ===
class AlarmManager:
    def __init__(self):
        self.alarms = []
        self.alarm_sound_enabled = True
        self.alarm_duration = 5.0  # saniye
        self.last_alarm_time = 0
        self.alarm_cooldown = 10.0  # saniye
        
    def add_alarm(self, source_id, alarm_type, details=None):
        """Yeni alarm ekler"""
        current_time = time.time()
        
        # Cooldown kontrolü
        if current_time - self.last_alarm_time < self.alarm_cooldown:
            return False
            
        alarm = {
            'id': len(self.alarms) + 1,
            'source_id': source_id,
            'type': alarm_type,
            'details': details or {},
            'timestamp': current_time,
            'datetime': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
            'acknowledged': False
        }
        
        self.alarms.append(alarm)
        self.last_alarm_time = current_time
        
        # Log kaydı
        logging.info(f"ALARM: {alarm_type} - Kaynak: {source_id} - Detaylar: {details}")
        
        # Ses alarmı
        if self.alarm_sound_enabled:
            self._play_alarm_sound()
            
        return True
        
    def _play_alarm_sound(self):
        """Alarm sesi çalar"""
        if winsound:
            try:
                winsound.MessageBeep(winsound.MB_ICONEXCLAMATION)
            except:
                pass
                
    def get_recent_alarms(self, count=10):
        """Son alarmları döndürür"""
        return sorted(self.alarms, key=lambda x: x['timestamp'], reverse=True)[:count]
        
    def acknowledge_alarm(self, alarm_id):
        """Alarmı onaylar"""
        for alarm in self.alarms:
            if alarm['id'] == alarm_id:
                alarm['acknowledged'] = True
                logging.info(f"Alarm onaylandı: ID {alarm_id}")
                break
                
    def clear_old_alarms(self, days=7):
        """Eski alarmları temizler"""
        cutoff_time = time.time() - (days * 24 * 3600)
        self.alarms = [alarm for alarm in self.alarms if alarm['timestamp'] > cutoff_time]


# === ANA UYGULAMA PENCERESİ ===
class AlarmSystemGUI(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Gelişmiş Alarm Sistemi")
        self.setGeometry(100, 100, 1400, 900)
        
        # Ana bileşenler
        self.capture_threads = {}
        self.motion_detectors = {}
        self.alarm_manager = AlarmManager()
        
        # UI bileşenleri
        self.video_labels = {}
        self.status_labels = {}
        self.fps_labels = {}
        
        # Zamanlayıcılar
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self.update_video_frames)
        self.update_timer.start(33)  # ~30 FPS
        
        self.alarm_check_timer = QTimer()
        self.alarm_check_timer.timeout.connect(self.check_alarms)
        self.alarm_check_timer.start(100)  # 100ms
        
        self.setup_ui()
        
    def setup_ui(self):
        """Kullanıcı arayüzünü kurar"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # Ana layout
        main_layout = QHBoxLayout(central_widget)
        
        # Sol panel - Video görüntüleri
        video_panel = QWidget()
        video_layout = QGridLayout(video_panel)
        
        # Video etiketleri oluştur
        for i in range(4):
            row = i // 2
            col = i % 2
            
            # Video container
            video_container = QWidget()
            video_container.setMinimumSize(400, 300)
            video_container.setStyleSheet("border: 2px solid #ccc; background-color: #000;")
            
            container_layout = QVBoxLayout(video_container)
            
            # Video label
            video_label = QLabel(f"Video {i+1}")
            video_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            video_label.setStyleSheet("color: white; font-size: 14px;")
            video_label.setMinimumSize(380, 280)
            
            # Status label
            status_label = QLabel("Bağlantı yok")
            status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            status_label.setStyleSheet("color: red; font-size: 12px;")
            
            # FPS label
            fps_label = QLabel("FPS: 0")
            fps_label.setAlignment(Qt.AlignmentFlag.AlignRight)
            fps_label.setStyleSheet("color: yellow; font-size: 10px;")
            
            container_layout.addWidget(video_label)
            container_layout.addWidget(status_label)
            container_layout.addWidget(fps_label)
            
            video_layout.addWidget(video_container, row, col)
            
            # Referansları sakla
            self.video_labels[i] = video_label
            self.status_labels[i] = status_label
            self.fps_labels[i] = fps_label
            
        # Sağ panel - Kontroller
        control_panel = QWidget()
        control_panel.setMaximumWidth(300)
        control_layout = QVBoxLayout(control_panel)
        
        # Video kaynakları grubu
        sources_group = QGroupBox("Video Kaynakları")
        sources_layout = QVBoxLayout(sources_group)
        
        # Kaynak ekleme butonları
        add_rtsp_btn = QPushButton("RTSP Kaynağı Ekle")
        add_rtsp_btn.clicked.connect(self.add_rtsp_source)
        sources_layout.addWidget(add_rtsp_btn)
        
        add_file_btn = QPushButton("Video Dosyası Ekle")
        add_file_btn.clicked.connect(self.add_file_source)
        sources_layout.addWidget(add_file_btn)
        
        add_camera_btn = QPushButton("Kamera Ekle")
        add_camera_btn.clicked.connect(self.add_camera_source)
        sources_layout.addWidget(add_camera_btn)
        
        # Alarm ayarları grubu
        alarm_group = QGroupBox("Alarm Ayarları")
        alarm_layout = QVBoxLayout(alarm_group)
        
        # Hassasiyet ayarı
        sensitivity_label = QLabel("Hassasiyet:")
        self.sensitivity_slider = QSlider(Qt.Orientation.Horizontal)
        self.sensitivity_slider.setRange(10, 100)
        self.sensitivity_slider.setValue(30)
        self.sensitivity_slider.valueChanged.connect(self.update_sensitivity)
        
        alarm_layout.addWidget(sensitivity_label)
        alarm_layout.addWidget(self.sensitivity_slider)
        
        # Ses alarmı
        self.sound_checkbox = QCheckBox("Ses Alarmı")
        self.sound_checkbox.setChecked(True)
        self.sound_checkbox.toggled.connect(self.toggle_sound_alarm)
        alarm_layout.addWidget(self.sound_checkbox)
        
        # Alarm listesi
        alarm_list_label = QLabel("Son Alarmlar:")
        self.alarm_list = QListWidget()
        self.alarm_list.setMaximumHeight(200)
        
        alarm_layout.addWidget(alarm_list_label)
        alarm_layout.addWidget(self.alarm_list)
        
        # Kontrol butonları
        control_buttons_layout = QHBoxLayout()
        
        start_btn = QPushButton("Başlat")
        start_btn.clicked.connect(self.start_monitoring)
        control_buttons_layout.addWidget(start_btn)
        
        stop_btn = QPushButton("Durdur")
        stop_btn.clicked.connect(self.stop_monitoring)
        control_buttons_layout.addWidget(stop_btn)
        
        # Layout'ları ana panele ekle
        control_layout.addWidget(sources_group)
        control_layout.addWidget(alarm_group)
        control_layout.addLayout(control_buttons_layout)
        control_layout.addStretch()
        
        # Ana layout'a panelleri ekle
        main_layout.addWidget(video_panel, 1)
        main_layout.addWidget(control_panel)
        
    def add_rtsp_source(self):
        """RTSP kaynağı ekler"""
        url, ok = QInputDialog.getText(self, "RTSP Kaynağı", "RTSP URL'sini girin:")
        if ok and url:
            self.add_video_source(url, is_file=False)
            
    def add_file_source(self):
        """Video dosyası ekler"""
        file_path, _ = QFileDialog.getOpenFileName(
            self, "Video Dosyası Seç", "", "Video Files (*.mp4 *.avi *.mkv *.mov)"
        )
        if file_path:
            self.add_video_source(file_path, is_file=True)
            
    def add_camera_source(self):
        """Kamera kaynağı ekler"""
        camera_id, ok = QInputDialog.getInt(self, "Kamera", "Kamera ID'sini girin:", 0, 0, 10)
        if ok:
            self.add_video_source(str(camera_id), is_file=False)
            
    def add_video_source(self, source_path, is_file=False):
        """Video kaynağı ekler"""
        # Boş slot bul
        slot_id = None
        for i in range(4):
            if i not in self.capture_threads:
                slot_id = i
                break
                
        if slot_id is None:
            QMessageBox.warning(self, "Hata", "Tüm video slotları dolu!")
            return
            
        # Capture thread oluştur
        capture_thread = RTSPCaptureThread(source_path, slot_id, is_file)
        capture_thread.start()
        
        # Motion detector oluştur
        motion_detector = MotionDetector()
        
        # Referansları sakla
        self.capture_threads[slot_id] = capture_thread
        self.motion_detectors[slot_id] = motion_detector
        
        # UI güncelle
        self.status_labels[slot_id].setText("Bağlanıyor...")
        self.status_labels[slot_id].setStyleSheet("color: orange; font-size: 12px;")
        
        logging.info(f"Video kaynağı eklendi: Slot {slot_id} - {source_path}")
        
    def update_video_frames(self):
        """Video karelerini günceller"""
        for slot_id, capture_thread in self.capture_threads.items():
            if capture_thread.is_running:
                frame = capture_thread.get_frame()
                if frame is not None:
                    # Frame'i QImage'e dönüştür
                    height, width, channel = frame.shape
                    bytes_per_line = 3 * width
                    q_image = QImage(frame.data, width, height, bytes_per_line, QImage.Format.Format_RGB888)
                    q_image = q_image.rgbSwapped()
                    
                    # Pixmap oluştur ve label'a yerleştir
                    pixmap = QPixmap.fromImage(q_image)
                    scaled_pixmap = pixmap.scaled(
                        self.video_labels[slot_id].size(),
                        Qt.AspectRatioMode.KeepAspectRatio,
                        Qt.TransformationMode.SmoothTransformation
                    )
                    self.video_labels[slot_id].setPixmap(scaled_pixmap)
                    
                    # FPS güncelle
                    fps = capture_thread.get_fps()
                    self.fps_labels[slot_id].setText(f"FPS: {fps:.1f}")
                    
                    # Status güncelle
                    self.status_labels[slot_id].setText("Aktif")
                    self.status_labels[slot_id].setStyleSheet("color: green; font-size: 12px;")
                    
                    # Motion detection
                    motion_detected, _, _ = self.motion_detectors[slot_id].detect_motion(frame)
                    if motion_detected:
                        self.alarm_manager.add_alarm(slot_id, "Hareket Algılandı", {"frame_size": frame.shape})
                        
    def check_alarms(self):
        """Alarmları kontrol eder"""
        recent_alarms = self.alarm_manager.get_recent_alarms(5)
        
        # Alarm listesini güncelle
        self.alarm_list.clear()
        for alarm in recent_alarms:
            item_text = f"{alarm['datetime']} - {alarm['type']} (Kaynak {alarm['source_id']})"
            if not alarm['acknowledged']:
                item_text += " [YENİ]"
            self.alarm_list.addItem(item_text)
            
    def update_sensitivity(self):
        """Hassasiyet ayarını günceller"""
        sensitivity = self.sensitivity_slider.value() / 100.0
        for detector in self.motion_detectors.values():
            detector.set_sensitivity(sensitivity)
            
    def toggle_sound_alarm(self, enabled):
        """Ses alarmını açar/kapatır"""
        self.alarm_manager.alarm_sound_enabled = enabled
        
    def start_monitoring(self):
        """İzlemeyi başlatır"""
        for capture_thread in self.capture_threads.values():
            capture_thread.start()
        logging.info("Video izleme başlatıldı")
        
    def stop_monitoring(self):
        """İzlemeyi durdurur"""
        for capture_thread in self.capture_threads.values():
            capture_thread.stop()
        logging.info("Video izleme durduruldu")
        
    def closeEvent(self, event):
        """Uygulama kapatılırken"""
        self.stop_monitoring()
        event.accept()


# === ANA FONKSİYON ===
def main():
    app = QApplication(sys.argv)
    app.setApplicationName("Gelişmiş Alarm Sistemi")
    
    # Stil ayarları
    app.setStyle('Fusion')
    
    window = AlarmSystemGUI()
    window.show()
    
    sys.exit(app.exec())


if __name__ == "__main__":
    main()