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
        self.last_frame_time = time.time()
        self.reconnect_delay = 5  # Yeniden bağlanma gecikmesi (saniye)
        
    def start(self):
        """Thread'i başlatır"""
        if not self.thread.is_alive():
            self.thread.start()
            
    def stop(self):
        """Thread'i durdurur"""
        self.stopped = True
        if self.capture:
            self.capture.release()
            
    def get_frame(self):
        """En son kareyi thread-safe şekilde alır"""
        with self.frame_lock:
            return self.frame.copy() if self.frame is not None else None
            
    def run(self):
        """Ana thread döngüsü"""
        while not self.stopped:
            try:
                # Video kaynağını aç
                self.capture = cv2.VideoCapture(self.source_path)
                if not self.capture.isOpened():
                    logging.error(f"Video kaynağı açılamadı: {self.source_path}")
                    time.sleep(self.reconnect_delay)
                    continue
                    
                # Tampon boyutunu minimize et (düşük gecikme için)
                self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)
                
                self.is_running = True
                logging.info(f"Video kaynağı başarıyla açıldı: {self.source_path}")
                
                while not self.stopped and self.capture.isOpened():
                    ret, frame = self.capture.read()
                    if not ret:
                        logging.warning(f"Kare okunamadı: {self.source_path}")
                        break
                        
                    # Frame'i thread-safe şekilde güncelle
                    with self.frame_lock:
                        self.frame = frame
                        self.last_frame_time = time.time()
                        
                    # CPU kullanımını azaltmak için kısa bir bekleme
                    time.sleep(0.001)
                    
            except Exception as e:
                logging.error(f"Video yakalama hatası ({self.source_path}): {e}")
                
            finally:
                self.is_running = False
                if self.capture:
                    self.capture.release()
                    
                # Yeniden bağlanmayı dene (dosya kaynağı değilse)
                if not self.stopped and not self.is_file_source:
                    logging.info(f"Yeniden bağlanmaya çalışılıyor: {self.source_path}")
                    time.sleep(self.reconnect_delay)


# === BÖLGE TESPİT SINIFI ===
class DetectionZone:
    """Tespit bölgesi sınıfı"""
    def __init__(self, name, points, zone_type="motion", sensitivity=50):
        self.name = name
        self.points = points  # [(x1,y1), (x2,y2), ...]
        self.zone_type = zone_type  # "motion", "intrusion", "line_crossing"
        self.sensitivity = sensitivity
        self.active = True
        self.alarm_active = False
        self.last_detection_time = 0
        self.detection_count = 0
        
    def is_point_inside(self, point):
        """Bir noktanın bölge içinde olup olmadığını kontrol eder"""
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


# === HAREKET TESPİT SINIFI ===
class MotionDetector:
    """Gelişmiş hareket tespit sınıfı"""
    def __init__(self):
        self.background_subtractor = cv2.createBackgroundSubtractorMOG2(
            detectShadows=True, varThreshold=50, history=500
        )
        self.previous_frame = None
        self.motion_threshold = 1000
        self.min_contour_area = 500
        self.detection_zones = []
        
    def add_zone(self, zone):
        """Tespit bölgesi ekler"""
        self.detection_zones.append(zone)
        
    def remove_zone(self, zone_name):
        """Tespit bölgesini kaldırır"""
        self.detection_zones = [z for z in self.detection_zones if z.name != zone_name]
        
    def detect_motion(self, frame):
        """Hareket tespiti yapar"""
        if frame is None:
            return False, []
            
        # Arka plan çıkarma
        fg_mask = self.background_subtractor.apply(frame)
        
        # Gürültüyü azalt
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        fg_mask = cv2.morphologyEx(fg_mask, cv2.MORPH_OPEN, kernel)
        
        # Konturları bul
        contours, _ = cv2.findContours(fg_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        detections = []
        motion_detected = False
        
        for contour in contours:
            area = cv2.contourArea(contour)
            if area > self.min_contour_area:
                # Hareket merkezi
                M = cv2.moments(contour)
                if M["m00"] != 0:
                    cx = int(M["m10"] / M["m00"])
                    cy = int(M["m01"] / M["m00"])
                    
                    # Bölge kontrolü
                    for zone in self.detection_zones:
                        if zone.active and zone.is_point_inside((cx, cy)):
                            detections.append({
                                'zone': zone.name,
                                'center': (cx, cy),
                                'area': area,
                                'contour': contour
                            })
                            motion_detected = True
                            zone.last_detection_time = time.time()
                            zone.detection_count += 1
                            
        return motion_detected, detections


# === PLC İLETİŞİM SINIFI ===
class PLCController:
    """PLC iletişim sınıfı"""
    def __init__(self):
        self.client = None
        self.connected = False
        self.ip_address = "192.168.1.100"
        self.rack = 0
        self.slot = 1
        
    def connect(self, ip_address=None, rack=0, slot=1):
        """PLC'ye bağlan"""
        if snap7 is None:
            logging.error("snap7 kütüphanesi yüklü değil")
            return False
            
        try:
            if ip_address:
                self.ip_address = ip_address
            self.rack = rack
            self.slot = slot
            
            self.client = snap7.Client()
            self.client.connect(self.ip_address, self.rack, self.slot)
            self.connected = True
            logging.info(f"PLC'ye başarıyla bağlanıldı: {self.ip_address}")
            return True
            
        except Exception as e:
            logging.error(f"PLC bağlantı hatası: {e}")
            self.connected = False
            return False
            
    def disconnect(self):
        """PLC bağlantısını kes"""
        if self.client:
            self.client.disconnect()
            self.connected = False
            logging.info("PLC bağlantısı kesildi")
            
    def write_bool(self, db_number, start_offset, bit_offset, value):
        """Boolean değer yaz"""
        if not self.connected:
            return False
            
        try:
            data = self.client.db_read(db_number, start_offset, 1)
            set_bool(data, 0, bit_offset, value)
            self.client.db_write(db_number, start_offset, data)
            return True
        except Exception as e:
            logging.error(f"PLC yazma hatası: {e}")
            return False
            
    def read_bool(self, db_number, start_offset, bit_offset):
        """Boolean değer oku"""
        if not self.connected:
            return False
            
        try:
            data = self.client.db_read(db_number, start_offset, 1)
            return snap7.util.get_bool(data, 0, bit_offset)
        except Exception as e:
            logging.error(f"PLC okuma hatası: {e}")
            return False


# === VIDEO GÖRÜNTÜLEME WİDGETI ===
class VideoDisplayWidget(QLabel):
    """Video görüntüleme widget'ı"""
    zone_created = pyqtSignal(str, list)  # zone_name, points
    
    def __init__(self):
        super().__init__()
        self.setMinimumSize(640, 480)
        self.setStyleSheet("border: 1px solid black")
        self.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setText("Video Yükleniyor...")
        
        # Bölge çizimi için değişkenler
        self.drawing_zone = False
        self.current_zone_points = []
        self.zones = []
        self.zone_colors = {}
        
    def mousePressEvent(self, event):
        """Fare tıklaması olayı"""
        if event.button() == Qt.MouseButton.LeftButton and self.drawing_zone:
            # Göreli koordinatları hesapla
            pos = event.position().toPoint()
            self.current_zone_points.append((pos.x(), pos.y()))
            self.update()
            
    def mouseDoubleClickEvent(self, event):
        """Çift tıklama olayı - bölge çizimini tamamla"""
        if self.drawing_zone and len(self.current_zone_points) >= 3:
            zone_name, ok = QInputDialog.getText(self, 'Bölge Adı', 'Bölge adını girin:')
            if ok and zone_name:
                self.zones.append({
                    'name': zone_name,
                    'points': self.current_zone_points.copy()
                })
                self.zone_colors[zone_name] = QColor(np.random.randint(0, 255), 
                                                   np.random.randint(0, 255), 
                                                   np.random.randint(0, 255))
                self.zone_created.emit(zone_name, self.current_zone_points.copy())
                
            self.current_zone_points = []
            self.drawing_zone = False
            self.update()
            
    def start_zone_drawing(self):
        """Bölge çizimini başlat"""
        self.drawing_zone = True
        self.current_zone_points = []
        
    def paintEvent(self, event):
        """Çizim olayı"""
        super().paintEvent(event)
        
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        # Mevcut bölgeleri çiz
        for zone in self.zones:
            if len(zone['points']) >= 3:
                color = self.zone_colors.get(zone['name'], QColor(255, 0, 0))
                painter.setPen(QPen(color, 2))
                
                # Çokgen çiz
                points = [QPoint(p[0], p[1]) for p in zone['points']]
                polygon = QPolygon(points)
                painter.drawPolygon(polygon)
                
                # Bölge adını yaz
                if points:
                    painter.drawText(points[0], zone['name'])
        
        # Çizilmekte olan bölgeyi çiz
        if self.drawing_zone and len(self.current_zone_points) > 0:
            painter.setPen(QPen(QColor(255, 255, 0), 2))
            
            # Noktaları çiz
            for point in self.current_zone_points:
                painter.drawEllipse(QPoint(point[0], point[1]), 3, 3)
                
            # Çizgileri çiz
            if len(self.current_zone_points) > 1:
                for i in range(len(self.current_zone_points) - 1):
                    p1 = QPoint(self.current_zone_points[i][0], self.current_zone_points[i][1])
                    p2 = QPoint(self.current_zone_points[i+1][0], self.current_zone_points[i+1][1])
                    painter.drawLine(p1, p2)
                    
    def update_frame(self, frame):
        """Video karesi güncelle"""
        if frame is not None:
            height, width, channel = frame.shape
            bytes_per_line = 3 * width
            q_image = QImage(frame.data, width, height, bytes_per_line, QImage.Format.Format_RGB888).rgbSwapped()
            
            # Widget boyutuna göre ölçeklendir
            pixmap = QPixmap.fromImage(q_image)
            scaled_pixmap = pixmap.scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
            self.setPixmap(scaled_pixmap)


# === ANA UYGULAMA SINIFI ===
class SurveillanceSystem(QMainWindow):
    """Ana gözetleme sistemi uygulaması"""
    
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Gelişmiş Gözetleme Sistemi")
        self.setGeometry(100, 100, 1200, 800)
        
        # Sistem bileşenleri
        self.video_sources = {}  # source_id: RTSPCaptureThread
        self.motion_detectors = {}  # source_id: MotionDetector
        self.plc_controller = PLCController()
        self.detection_zones = {}  # source_id: [DetectionZone, ...]
        
        # Timer
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self.update_displays)
        self.update_timer.start(33)  # ~30 FPS
        
        # UI kurulumu
        self.setup_ui()
        
        # Alarm sistemi
        self.alarm_active = False
        self.alarm_log = []
        
    def setup_ui(self):
        """Kullanıcı arayüzünü kur"""
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        
        # Ana layout
        main_layout = QHBoxLayout(central_widget)
        
        # Sol panel - Kontroller
        left_panel = QWidget()
        left_panel.setMaximumWidth(300)
        left_layout = QVBoxLayout(left_panel)
        
        # Video kaynağı ekleme
        source_group = QGroupBox("Video Kaynakları")
        source_layout = QVBoxLayout(source_group)
        
        self.add_source_btn = QPushButton("Video Kaynağı Ekle")
        self.add_source_btn.clicked.connect(self.add_video_source)
        source_layout.addWidget(self.add_source_btn)
        
        self.source_list = QListWidget()
        source_layout.addWidget(self.source_list)
        
        left_layout.addWidget(source_group)
        
        # Bölge yönetimi
        zone_group = QGroupBox("Tespit Bölgeleri")
        zone_layout = QVBoxLayout(zone_group)
        
        self.add_zone_btn = QPushButton("Bölge Çiz")
        self.add_zone_btn.clicked.connect(self.start_zone_drawing)
        zone_layout.addWidget(self.add_zone_btn)
        
        self.zone_list = QListWidget()
        zone_layout.addWidget(self.zone_list)
        
        left_layout.addWidget(zone_group)
        
        # PLC kontrolleri
        plc_group = QGroupBox("PLC Kontrolü")
        plc_layout = QVBoxLayout(plc_group)
        
        self.plc_connect_btn = QPushButton("PLC'ye Bağlan")
        self.plc_connect_btn.clicked.connect(self.connect_plc)
        plc_layout.addWidget(self.plc_connect_btn)
        
        self.plc_status_label = QLabel("PLC: Bağlı Değil")
        plc_layout.addWidget(self.plc_status_label)
        
        left_layout.addWidget(plc_group)
        
        # Alarm kontrolleri
        alarm_group = QGroupBox("Alarm Sistemi")
        alarm_layout = QVBoxLayout(alarm_group)
        
        self.alarm_enable_cb = QCheckBox("Alarmları Etkinleştir")
        self.alarm_enable_cb.setChecked(True)
        alarm_layout.addWidget(self.alarm_enable_cb)
        
        self.test_alarm_btn = QPushButton("Alarm Testi")
        self.test_alarm_btn.clicked.connect(self.test_alarm)
        alarm_layout.addWidget(self.test_alarm_btn)
        
        left_layout.addWidget(alarm_group)
        
        left_layout.addStretch()
        main_layout.addWidget(left_panel)
        
        # Sağ panel - Video görüntüleme
        self.video_display = VideoDisplayWidget()
        self.video_display.zone_created.connect(self.on_zone_created)
        main_layout.addWidget(self.video_display)
        
        # Durum çubuğu
        self.statusBar().showMessage("Sistem hazır")
        
    def add_video_source(self):
        """Video kaynağı ekle"""
        source_path, ok = QInputDialog.getText(self, 'Video Kaynağı', 
                                              'RTSP URL veya dosya yolu girin:')
        if ok and source_path:
            source_id = len(self.video_sources)
            
            # Dosya mı yoksa RTSP mi kontrol et
            is_file = os.path.isfile(source_path) or not source_path.startswith(('rtsp://', 'http://'))
            
            # Video yakalama thread'i oluştur
            capture_thread = RTSPCaptureThread(source_path, source_id, is_file)
            self.video_sources[source_id] = capture_thread
            
            # Hareket dedektörü oluştur
            motion_detector = MotionDetector()
            self.motion_detectors[source_id] = motion_detector
            
            # Liste widget'ına ekle
            item = QListWidgetItem(f"Kaynak {source_id}: {source_path}")
            self.source_list.addItem(item)
            
            # Thread'i başlat
            capture_thread.start()
            
            logging.info(f"Video kaynağı eklendi: {source_path}")
            
    def start_zone_drawing(self):
        """Bölge çizimi başlat"""
        if self.video_sources:
            self.video_display.start_zone_drawing()
            QMessageBox.information(self, "Bölge Çizimi", 
                                  "Video üzerinde tıklayarak bölge çizin. Çift tıklayarak tamamlayın.")
        else:
            QMessageBox.warning(self, "Uyarı", "Önce bir video kaynağı ekleyin!")
            
    def on_zone_created(self, zone_name, points):
        """Bölge oluşturulduğunda çağrılır"""
        # Aktif video kaynağı için bölge oluştur
        if self.video_sources:
            source_id = 0  # İlk kaynak için (genişletilebilir)
            
            zone = DetectionZone(zone_name, points)
            
            if source_id not in self.detection_zones:
                self.detection_zones[source_id] = []
            self.detection_zones[source_id].append(zone)
            
            # Hareket dedektörüne ekle
            if source_id in self.motion_detectors:
                self.motion_detectors[source_id].add_zone(zone)
                
            # Liste widget'ına ekle
            item = QListWidgetItem(zone_name)
            self.zone_list.addItem(item)
            
            logging.info(f"Tespit bölgesi oluşturuldu: {zone_name}")
            
    def connect_plc(self):
        """PLC'ye bağlan"""
        ip_address, ok = QInputDialog.getText(self, 'PLC IP Adresi', 
                                            'PLC IP adresini girin:', text='192.168.1.100')
        if ok and ip_address:
            if self.plc_controller.connect(ip_address):
                self.plc_status_label.setText("PLC: Bağlı")
                self.plc_connect_btn.setText("PLC Bağlantısını Kes")
            else:
                self.plc_status_label.setText("PLC: Bağlantı Hatası")
                
    def test_alarm(self):
        """Alarm testi"""
        self.trigger_alarm("Test Alarmı", "Manuel test")
        
    def trigger_alarm(self, alarm_type, details):
        """Alarm tetikle"""
        if not self.alarm_enable_cb.isChecked():
            return
            
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        alarm_data = {
            'timestamp': timestamp,
            'type': alarm_type,
            'details': details
        }
        
        # Log'a kaydet
        self.alarm_log.append(alarm_data)
        logging.warning(f"ALARM: {alarm_type} - {details}")
        
        # Ses çal (Windows'ta)
        if winsound:
            try:
                winsound.Beep(1000, 500)  # 1000Hz, 500ms
            except:
                pass
                
        # PLC'ye sinyal gönder
        if self.plc_controller.connected:
            self.plc_controller.write_bool(1, 0, 0, True)  # DB1, Byte0, Bit0
            
        # Durum çubuğunu güncelle
        self.statusBar().showMessage(f"ALARM: {alarm_type}")
        
    def update_displays(self):
        """Görüntüleri güncelle"""
        for source_id, capture_thread in self.video_sources.items():
            if capture_thread.is_running:
                frame = capture_thread.get_frame()
                if frame is not None:
                    # Hareket tespiti
                    if source_id in self.motion_detectors:
                        motion_detected, detections = self.motion_detectors[source_id].detect_motion(frame)
                        
                        if motion_detected:
                            for detection in detections:
                                # Alarm tetikle
                                self.trigger_alarm("Hareket Tespiti", 
                                                 f"Bölge: {detection['zone']}")
                                
                                # Tespit edilen alanı çiz
                                cv2.drawContours(frame, [detection['contour']], -1, (0, 255, 0), 2)
                                cv2.circle(frame, detection['center'], 5, (0, 0, 255), -1)
                    
                    # İlk kaynağı görüntüle (genişletilebilir)
                    if source_id == 0:
                        self.video_display.update_frame(frame)
                        
    def closeEvent(self, event):
        """Uygulama kapatılırken"""
        # Tüm thread'leri durdur
        for capture_thread in self.video_sources.values():
            capture_thread.stop()
            
        # PLC bağlantısını kes
        self.plc_controller.disconnect()
        
        event.accept()


# === ANA FONKSİYON ===
def main():
    """Ana fonksiyon"""
    app = QApplication(sys.argv)
    
    # Uygulama ikonu ve stili
    app.setApplicationName("Gözetleme Sistemi")
    app.setApplicationVersion("1.0")
    
    # Ana pencereyi oluştur ve göster
    window = SurveillanceSystem()
    window.show()
    
    # Uygulamayı çalıştır
    sys.exit(app.exec())


if __name__ == "__main__":
    main()