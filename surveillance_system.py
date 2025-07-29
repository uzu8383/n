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
    QColorDialog, QComboBox, QTabWidget
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
                
                # RTSP performans iyileştirmeleri
                if self.source_path.startswith('rtsp://'):
                    self.capture.set(cv2.CAP_PROP_FPS, 30)
                    self.capture.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc('M', 'J', 'P', 'G'))
                    # TCP yerine UDP kullan (daha hızlı)
                    self.capture.set(cv2.CAP_PROP_OPEN_TIMEOUT_MSEC, 5000)
                    self.capture.set(cv2.CAP_PROP_READ_TIMEOUT_MSEC, 5000)
                
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


# === CAM KENARI ÖLÇÜM SINIFI ===
class GlassEdgeMeasurement:
    """Cam kenarı ölçüm sınıfı - Cam üretiminde kalite kontrol için"""
    def __init__(self):
        self.edge_detection_method = "canny"  # "canny", "sobel", "laplacian"
        self.canny_threshold1 = 50
        self.canny_threshold2 = 150
        self.blur_kernel_size = 5
        self.morphology_kernel_size = 3
        self.min_edge_length = 100
        self.measurement_unit = "mm"  # "mm", "cm", "pixel"
        self.pixel_to_mm_ratio = 1.0  # 1 pixel = ? mm
        self.tolerance_range = 2.0  # mm cinsinden tolerans
        self.reference_measurements = {}  # Referans ölçümler
        self.measurement_history = []
        self.enabled = False
        
    def detect_edges(self, frame):
        """Kenar tespiti yapar"""
        if frame is None:
            return None
            
        # Gri tonlamaya çevir
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        # Gürültüyü azalt
        if self.blur_kernel_size > 0:
            gray = cv2.GaussianBlur(gray, (self.blur_kernel_size, self.blur_kernel_size), 0)
        
        # Kenar tespiti
        if self.edge_detection_method == "canny":
            edges = cv2.Canny(gray, self.canny_threshold1, self.canny_threshold2)
        elif self.edge_detection_method == "sobel":
            sobelx = cv2.Sobel(gray, cv2.CV_64F, 1, 0, ksize=3)
            sobely = cv2.Sobel(gray, cv2.CV_64F, 0, 1, ksize=3)
            edges = np.sqrt(sobelx**2 + sobely**2)
            edges = np.uint8(edges / edges.max() * 255)
        elif self.edge_detection_method == "laplacian":
            edges = cv2.Laplacian(gray, cv2.CV_64F)
            edges = np.uint8(np.absolute(edges))
            
        # Morfological işlemler
        if self.morphology_kernel_size > 0:
            kernel = np.ones((self.morphology_kernel_size, self.morphology_kernel_size), np.uint8)
            edges = cv2.morphologyEx(edges, cv2.MORPH_CLOSE, kernel)
            
        return edges
        
    def find_glass_edges(self, edges):
        """Cam kenarlarını bulur"""
        if edges is None:
            return []
            
        # Konturları bul
        contours, _ = cv2.findContours(edges, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        
        glass_edges = []
        for contour in contours:
            # Kontour uzunluğunu kontrol et
            arc_length = cv2.arcLength(contour, False)
            if arc_length > self.min_edge_length:
                glass_edges.append({
                    'contour': contour,
                    'length': arc_length,
                    'area': cv2.contourArea(contour)
                })
                
        return glass_edges
        
    def measure_dimensions(self, glass_edges):
        """Cam boyutlarını ölçer"""
        measurements = {
            'width': 0,
            'height': 0,
            'perimeter': 0,
            'area': 0,
            'edge_quality': 0,
            'defects': []
        }
        
        if not glass_edges:
            return measurements
            
        # En büyük kontur (ana cam kenarı)
        main_edge = max(glass_edges, key=lambda x: x['area'])
        
        # Bounding rectangle
        x, y, w, h = cv2.boundingRect(main_edge['contour'])
        
        # Pixel'den mm'ye çevir
        width_mm = w * self.pixel_to_mm_ratio
        height_mm = h * self.pixel_to_mm_ratio
        perimeter_mm = main_edge['length'] * self.pixel_to_mm_ratio
        area_mm2 = main_edge['area'] * (self.pixel_to_mm_ratio ** 2)
        
        measurements.update({
            'width': width_mm,
            'height': height_mm,
            'perimeter': perimeter_mm,
            'area': area_mm2,
            'bounding_rect': (x, y, w, h)
        })
        
        return measurements
        
    def check_tolerances(self, measurements):
        """Tolerans kontrolü yapar"""
        tolerance_results = {
            'within_tolerance': True,
            'violations': []
        }
        
        for param, reference_value in self.reference_measurements.items():
            if param in measurements:
                measured_value = measurements[param]
                deviation = abs(measured_value - reference_value)
                
                if deviation > self.tolerance_range:
                    tolerance_results['within_tolerance'] = False
                    tolerance_results['violations'].append({
                        'parameter': param,
                        'measured': measured_value,
                        'reference': reference_value,
                        'deviation': deviation,
                        'tolerance': self.tolerance_range
                    })
                    
        return tolerance_results
        
    def save_measurement(self, measurements):
        """Ölçüm sonuçlarını kaydet"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        measurement_record = {
            'timestamp': timestamp,
            'measurements': measurements,
            'tolerances': self.check_tolerances(measurements)
        }
        
        self.measurement_history.append(measurement_record)
        
        # CSV'ye kaydet
        csv_file = 'glass_measurements.csv'
        file_exists = os.path.isfile(csv_file)
        
        with open(csv_file, 'a', newline='', encoding='utf-8') as file:
            fieldnames = ['timestamp', 'width', 'height', 'perimeter', 'area', 'within_tolerance']
            writer = csv.DictWriter(file, fieldnames=fieldnames)
            
            if not file_exists:
                writer.writeheader()
                
            tolerances = measurement_record['tolerances']
            
            writer.writerow({
                'timestamp': measurement_record['timestamp'],
                'width': measurements.get('width', 0),
                'height': measurements.get('height', 0),
                'perimeter': measurements.get('perimeter', 0),
                'area': measurements.get('area', 0),
                'within_tolerance': tolerances['within_tolerance']
            })


# === CAM KENARI ÖLÇÜM AYARLARI DİYALOGU ===
class GlassEdgeSettingsDialog(QDialog):
    """Cam kenarı ölçüm ayarları diyalogu"""
    def __init__(self, glass_measurement, parent=None):
        super().__init__(parent)
        self.glass_measurement = glass_measurement
        self.setWindowTitle("Cam Kenarı Ölçüm Ayarları")
        self.setFixedSize(500, 600)
        self.setup_ui()
        self.load_current_settings()
        
    def setup_ui(self):
        layout = QVBoxLayout(self)
        
        # Tab widget
        tab_widget = QTabWidget()
        layout.addWidget(tab_widget)
        
        # Genel ayarlar tab
        general_tab = QWidget()
        general_layout = QVBoxLayout(general_tab)
        
        # Kenar tespit yöntemi
        method_group = QGroupBox("Kenar Tespit Yöntemi")
        method_layout = QVBoxLayout(method_group)
        
        self.method_combo = QComboBox()
        self.method_combo.addItems(["canny", "sobel", "laplacian"])
        method_layout.addWidget(QLabel("Yöntem:"))
        method_layout.addWidget(self.method_combo)
        
        general_layout.addWidget(method_group)
        
        # Canny parametreleri
        canny_group = QGroupBox("Canny Parametreleri")
        canny_layout = QGridLayout(canny_group)
        
        canny_layout.addWidget(QLabel("Alt Eşik:"), 0, 0)
        self.canny_threshold1_spin = QSpinBox()
        self.canny_threshold1_spin.setRange(1, 255)
        canny_layout.addWidget(self.canny_threshold1_spin, 0, 1)
        
        canny_layout.addWidget(QLabel("Üst Eşik:"), 1, 0)
        self.canny_threshold2_spin = QSpinBox()
        self.canny_threshold2_spin.setRange(1, 255)
        canny_layout.addWidget(self.canny_threshold2_spin, 1, 1)
        
        general_layout.addWidget(canny_group)
        
        # Filtreleme parametreleri
        filter_group = QGroupBox("Filtreleme Parametreleri")
        filter_layout = QGridLayout(filter_group)
        
        filter_layout.addWidget(QLabel("Blur Kernel Boyutu:"), 0, 0)
        self.blur_kernel_spin = QSpinBox()
        self.blur_kernel_spin.setRange(0, 15)
        self.blur_kernel_spin.setSingleStep(2)
        filter_layout.addWidget(self.blur_kernel_spin, 0, 1)
        
        filter_layout.addWidget(QLabel("Min Kenar Uzunluğu:"), 1, 0)
        self.min_edge_length_spin = QSpinBox()
        self.min_edge_length_spin.setRange(10, 1000)
        filter_layout.addWidget(self.min_edge_length_spin, 1, 1)
        
        general_layout.addWidget(filter_group)
        
        tab_widget.addTab(general_tab, "Genel")
        
        # Kalibrasyon tab
        calib_tab = QWidget()
        calib_layout = QVBoxLayout(calib_tab)
        
        calib_group = QGroupBox("Kalibrasyon Ayarları")
        calib_grid = QGridLayout(calib_group)
        
        calib_grid.addWidget(QLabel("Pixel/mm Oranı:"), 0, 0)
        self.pixel_mm_ratio_spin = QDoubleSpinBox()
        self.pixel_mm_ratio_spin.setRange(0.001, 100.0)
        self.pixel_mm_ratio_spin.setDecimals(3)
        self.pixel_mm_ratio_spin.setSingleStep(0.1)
        calib_grid.addWidget(self.pixel_mm_ratio_spin, 0, 1)
        
        calib_grid.addWidget(QLabel("Tolerans Aralığı:"), 1, 0)
        self.tolerance_spin = QDoubleSpinBox()
        self.tolerance_spin.setRange(0.1, 50.0)
        self.tolerance_spin.setDecimals(2)
        self.tolerance_spin.setSuffix(" mm")
        calib_grid.addWidget(self.tolerance_spin, 1, 1)
        
        calib_layout.addWidget(calib_group)
        
        # Referans ölçümler
        ref_group = QGroupBox("Referans Ölçümler")
        ref_layout = QGridLayout(ref_group)
        
        ref_layout.addWidget(QLabel("Referans Genişlik:"), 0, 0)
        self.ref_width_spin = QDoubleSpinBox()
        self.ref_width_spin.setRange(0, 10000)
        self.ref_width_spin.setDecimals(2)
        self.ref_width_spin.setSuffix(" mm")
        ref_layout.addWidget(self.ref_width_spin, 0, 1)
        
        ref_layout.addWidget(QLabel("Referans Yükseklik:"), 1, 0)
        self.ref_height_spin = QDoubleSpinBox()
        self.ref_height_spin.setRange(0, 10000)
        self.ref_height_spin.setDecimals(2)
        self.ref_height_spin.setSuffix(" mm")
        ref_layout.addWidget(self.ref_height_spin, 1, 1)
        
        calib_layout.addWidget(ref_group)
        
        tab_widget.addTab(calib_tab, "Kalibrasyon")
        
        # Butonlar
        button_layout = QHBoxLayout()
        
        self.ok_btn = QPushButton("Tamam")
        self.ok_btn.clicked.connect(self.accept)
        button_layout.addWidget(self.ok_btn)
        
        self.cancel_btn = QPushButton("İptal")
        self.cancel_btn.clicked.connect(self.reject)
        button_layout.addWidget(self.cancel_btn)
        
        self.apply_btn = QPushButton("Uygula")
        self.apply_btn.clicked.connect(self.apply_settings)
        button_layout.addWidget(self.apply_btn)
        
        layout.addLayout(button_layout)
        
    def load_current_settings(self):
        """Mevcut ayarları yükle"""
        gm = self.glass_measurement
        
        self.method_combo.setCurrentText(gm.edge_detection_method)
        self.canny_threshold1_spin.setValue(gm.canny_threshold1)
        self.canny_threshold2_spin.setValue(gm.canny_threshold2)
        self.blur_kernel_spin.setValue(gm.blur_kernel_size)
        self.min_edge_length_spin.setValue(gm.min_edge_length)
        self.pixel_mm_ratio_spin.setValue(gm.pixel_to_mm_ratio)
        self.tolerance_spin.setValue(gm.tolerance_range)
        
        # Referans değerler
        self.ref_width_spin.setValue(gm.reference_measurements.get('width', 0))
        self.ref_height_spin.setValue(gm.reference_measurements.get('height', 0))
        
    def apply_settings(self):
        """Ayarları uygula"""
        gm = self.glass_measurement
        
        gm.edge_detection_method = self.method_combo.currentText()
        gm.canny_threshold1 = self.canny_threshold1_spin.value()
        gm.canny_threshold2 = self.canny_threshold2_spin.value()
        gm.blur_kernel_size = self.blur_kernel_spin.value()
        gm.min_edge_length = self.min_edge_length_spin.value()
        gm.pixel_to_mm_ratio = self.pixel_mm_ratio_spin.value()
        gm.tolerance_range = self.tolerance_spin.value()
        
        # Referans ölçümler
        gm.reference_measurements['width'] = self.ref_width_spin.value()
        gm.reference_measurements['height'] = self.ref_height_spin.value()
        
        logging.info("Cam kenarı ölçüm ayarları güncellendi")


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
        
        # Cam kenarı ölçüm gösterimi
        self.show_glass_measurements = False
        self.glass_measurements = None
        
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
        
    def set_glass_measurements(self, measurements):
        """Cam ölçüm sonuçlarını ayarla"""
        self.glass_measurements = measurements
        self.update()
        
    def toggle_glass_measurement_display(self, show):
        """Cam ölçüm gösterimini aç/kapat"""
        self.show_glass_measurements = show
        self.update()
        
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
                    
        # Cam ölçüm sonuçlarını çiz
        if self.show_glass_measurements and self.glass_measurements:
            self.draw_glass_measurements(painter)
            
    def draw_glass_measurements(self, painter):
        """Cam ölçüm sonuçlarını çiz"""
        measurements = self.glass_measurements
        
        # Bounding rectangle çiz
        if 'bounding_rect' in measurements:
            x, y, w, h = measurements['bounding_rect']
            painter.setPen(QPen(QColor(0, 255, 255), 2))  # Cyan
            painter.drawRect(x, y, w, h)
            
            # Ölçüm bilgilerini yaz
            font = QFont()
            font.setPointSize(10)
            painter.setFont(font)
            painter.setPen(QPen(QColor(255, 255, 255), 1))
            
            info_text = []
            info_text.append(f"Genişlik: {measurements.get('width', 0):.2f} mm")
            info_text.append(f"Yükseklik: {measurements.get('height', 0):.2f} mm")
            
            # Bilgi kutusunu çiz
            text_y = y - 10
            for text in info_text:
                text_y -= 20
                painter.drawText(x, text_y, text)
                    
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
        
        # Cam kenarı ölçüm sistemi
        self.glass_measurement = GlassEdgeMeasurement()
        
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
        
        # Cam Kenarı Ölçüm Ayarları
        glass_group = QGroupBox("Cam Kenarı Ölçüm")
        glass_layout = QVBoxLayout(glass_group)
        
        self.glass_enable_cb = QCheckBox("Cam Kenarı Ölçümünü Etkinleştir")
        self.glass_enable_cb.stateChanged.connect(self.toggle_glass_measurement)
        glass_layout.addWidget(self.glass_enable_cb)
        
        self.glass_settings_btn = QPushButton("Ölçüm Ayarları")
        self.glass_settings_btn.clicked.connect(self.open_glass_settings)
        glass_layout.addWidget(self.glass_settings_btn)
        
        self.glass_show_cb = QCheckBox("Ölçümleri Göster")
        self.glass_show_cb.stateChanged.connect(self.toggle_glass_display)
        glass_layout.addWidget(self.glass_show_cb)
        
        # Cam ölçüm bilgileri
        self.glass_info_label = QLabel("Ölçüm Bilgileri:\nHenüz ölçüm yok")
        self.glass_info_label.setStyleSheet("background-color: #f0f0f0; padding: 5px; border: 1px solid #ccc;")
        self.glass_info_label.setWordWrap(True)
        glass_layout.addWidget(self.glass_info_label)
        
        left_layout.addWidget(glass_group)
        
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
            
    def toggle_glass_measurement(self, state):
        """Cam kenarı ölçümünü etkinleştir/devre dışı bırak"""
        self.glass_measurement.enabled = state == Qt.CheckState.Checked.value
        if self.glass_measurement.enabled:
            logging.info("Cam kenarı ölçümü etkinleştirildi")
        else:
            logging.info("Cam kenarı ölçümü devre dışı bırakıldı")
            
    def toggle_glass_display(self, state):
        """Cam ölçüm gösterimini aç/kapat"""
        show = state == Qt.CheckState.Checked.value
        self.video_display.toggle_glass_measurement_display(show)
        
    def open_glass_settings(self):
        """Cam kenarı ölçüm ayarları diyalogunu aç"""
        dialog = GlassEdgeSettingsDialog(self.glass_measurement, self)
        if dialog.exec() == QDialog.DialogCode.Accepted:
            logging.info("Cam kenarı ölçüm ayarları kaydedildi")
            
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
                    
                    # Cam kenarı ölçümü
                    if self.glass_measurement.enabled and source_id == 0:
                        self.process_glass_measurement(frame)
                    
                    # İlk kaynağı görüntüle (genişletilebilir)
                    if source_id == 0:
                        self.video_display.update_frame(frame)
                        
    def process_glass_measurement(self, frame):
        """Cam kenarı ölçümünü işle"""
        try:
            # Kenar tespiti
            edges = self.glass_measurement.detect_edges(frame)
            if edges is not None:
                # Cam kenarlarını bul
                glass_edges = self.glass_measurement.find_glass_edges(edges)
                
                if glass_edges:
                    # Ölçümleri hesapla
                    measurements = self.glass_measurement.measure_dimensions(glass_edges)
                    
                    # Tolerans kontrolü
                    tolerance_results = self.glass_measurement.check_tolerances(measurements)
                    
                    # Ölçüm sonuçlarını kaydet
                    self.glass_measurement.save_measurement(measurements)
                    
                    # UI'yi güncelle
                    self.update_glass_info_display(measurements, tolerance_results)
                    
                    # Video display'e ölçümleri gönder
                    self.video_display.set_glass_measurements(measurements)
                    
                    # Tolerans dışı alarm
                    if not tolerance_results['within_tolerance']:
                        violations = tolerance_results['violations']
                        for violation in violations:
                            self.trigger_alarm("Cam Ölçüm Tolerans Hatası", 
                                             f"{violation['parameter']}: {violation['measured']:.2f} mm (Referans: {violation['reference']:.2f} mm)")
                            
        except Exception as e:
            logging.error(f"Cam kenarı ölçüm hatası: {e}")
            
    def update_glass_info_display(self, measurements, tolerance_results):
        """Cam ölçüm bilgi gösterimini güncelle"""
        info_text = "Cam Ölçüm Bilgileri:\n"
        info_text += f"Genişlik: {measurements.get('width', 0):.2f} mm\n"
        info_text += f"Yükseklik: {measurements.get('height', 0):.2f} mm\n"
        info_text += f"Çevre: {measurements.get('perimeter', 0):.2f} mm\n"
        info_text += f"Alan: {measurements.get('area', 0):.2f} mm²\n"
        
        if tolerance_results['within_tolerance']:
            info_text += "Tolerans: ✓ UYGUN"
            self.glass_info_label.setStyleSheet("background-color: #d4edda; padding: 5px; border: 1px solid #c3e6cb; color: #155724;")
        else:
            info_text += f"Tolerans: ✗ UYGUN DEĞİL ({len(tolerance_results['violations'])} hata)"
            self.glass_info_label.setStyleSheet("background-color: #f8d7da; padding: 5px; border: 1px solid #f5c6cb; color: #721c24;")
            
        self.glass_info_label.setText(info_text)
                        
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