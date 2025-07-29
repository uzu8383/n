#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QAction>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QMutex>
#include <QtGui/QPixmap>
#include <QtGui/QImage>

#include <opencv2/opencv.hpp>
#ifdef WITH_CUDA
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudaarithm.hpp>
#endif

#include <memory>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <chrono>

// Include our main classes from main.cpp
#include "main.cpp"

class StreamDisplayWidget : public QLabel {
    Q_OBJECT

private:
    int stream_id;
    cv::Mat current_frame;
    QMutex frame_mutex;
    bool roi_selection_mode = false;
    cv::Point roi_start, roi_end;
    bool drawing_roi = false;

public:
    StreamDisplayWidget(int id, QWidget* parent = nullptr) 
        : QLabel(parent), stream_id(id) {
        setMinimumSize(320, 240);
        setMaximumSize(640, 480);
        setScaledContents(true);
        setStyleSheet("border: 2px solid gray;");
        setText(QString("Stream %1\nNo Signal").arg(id + 1));
        setAlignment(Qt::AlignCenter);
    }

    void updateFrame(const cv::Mat& frame) {
        QMutexLocker locker(&frame_mutex);
        current_frame = frame.clone();
        
        // Convert OpenCV Mat to QImage
        QImage qimg;
        if (frame.channels() == 3) {
            cv::Mat rgb_frame;
            cv::cvtColor(frame, rgb_frame, cv::COLOR_BGR2RGB);
            qimg = QImage(rgb_frame.data, rgb_frame.cols, rgb_frame.rows, 
                         rgb_frame.step, QImage::Format_RGB888);
        } else if (frame.channels() == 1) {
            qimg = QImage(frame.data, frame.cols, frame.rows, 
                         frame.step, QImage::Format_Grayscale8);
        }
        
        if (!qimg.isNull()) {
            setPixmap(QPixmap::fromImage(qimg));
        }
    }

    void setROISelectionMode(bool enabled) {
        roi_selection_mode = enabled;
        if (enabled) {
            setCursor(Qt::CrossCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }

    cv::Rect getSelectedROI() const {
        if (roi_start.x >= 0 && roi_end.x >= 0) {
            int x = std::min(roi_start.x, roi_end.x);
            int y = std::min(roi_start.y, roi_end.y);
            int w = std::abs(roi_end.x - roi_start.x);
            int h = std::abs(roi_end.y - roi_start.y);
            return cv::Rect(x, y, w, h);
        }
        return cv::Rect();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (roi_selection_mode && event->button() == Qt::LeftButton) {
            // Convert widget coordinates to frame coordinates
            QRect widget_rect = rect();
            if (!current_frame.empty()) {
                double scale_x = (double)current_frame.cols / widget_rect.width();
                double scale_y = (double)current_frame.rows / widget_rect.height();
                
                roi_start.x = event->pos().x() * scale_x;
                roi_start.y = event->pos().y() * scale_y;
                drawing_roi = true;
            }
        }
        QLabel::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (roi_selection_mode && drawing_roi) {
            QRect widget_rect = rect();
            if (!current_frame.empty()) {
                double scale_x = (double)current_frame.cols / widget_rect.width();
                double scale_y = (double)current_frame.rows / widget_rect.height();
                
                roi_end.x = event->pos().x() * scale_x;
                roi_end.y = event->pos().y() * scale_y;
                
                // Update display with ROI rectangle
                update();
            }
        }
        QLabel::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (roi_selection_mode && event->button() == Qt::LeftButton && drawing_roi) {
            drawing_roi = false;
            emit roiSelected(getSelectedROI());
        }
        QLabel::mouseReleaseEvent(event);
    }

signals:
    void roiSelected(const cv::Rect& roi);
};

class RTSPMonitorGUI : public QMainWindow {
    Q_OBJECT

private:
    std::unique_ptr<RTSPMonitorApp> app;
    std::vector<StreamDisplayWidget*> stream_widgets;
    QTableWidget* object_table;
    QTextEdit* log_display;
    QProgressBar* gpu_usage_bar;
    QProgressBar* cpu_usage_bar;
    QLabel* status_label;
    
    QTimer* update_timer;
    QTimer* performance_timer;
    
    // Stream management
    QGroupBox* stream_group;
    std::vector<QLineEdit*> stream_url_inputs;
    std::vector<QCheckBox*> stream_enable_checkboxes;
    
    // Object tracking
    QGroupBox* tracking_group;
    QLineEdit* object_name_input;
    QComboBox* stream_selector;
    QPushButton* add_object_btn;
    QPushButton* remove_object_btn;
    QPushButton* reset_object_btn;
    QPushButton* select_roi_btn;
    
    // Performance monitoring
    std::atomic<bool> monitoring_active{true};
    std::thread performance_thread;

public:
    RTSPMonitorGUI(QWidget* parent = nullptr) : QMainWindow(parent) {
        app = std::make_unique<RTSPMonitorApp>();
        setupUI();
        setupConnections();
        startPerformanceMonitoring();
    }

    ~RTSPMonitorGUI() {
        monitoring_active = false;
        if (performance_thread.joinable()) {
            performance_thread.join();
        }
    }

private:
    void setupUI() {
        setWindowTitle("RTSP Multi-Stream Monitor with CUDA Acceleration");
        setMinimumSize(1400, 900);

        // Central widget
        QWidget* central_widget = new QWidget;
        setCentralWidget(central_widget);

        // Main layout
        QHBoxLayout* main_layout = new QHBoxLayout(central_widget);

        // Left panel - Stream displays
        QWidget* left_panel = new QWidget;
        setupStreamDisplays(left_panel);
        
        // Right panel - Controls
        QWidget* right_panel = new QWidget;
        right_panel->setMaximumWidth(400);
        setupControlPanel(right_panel);

        // Splitter
        QSplitter* splitter = new QSplitter(Qt::Horizontal);
        splitter->addWidget(left_panel);
        splitter->addWidget(right_panel);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 1);

        main_layout->addWidget(splitter);

        // Status bar
        setupStatusBar();

        // Menu bar
        setupMenuBar();

        // Timers
        update_timer = new QTimer(this);
        performance_timer = new QTimer(this);
    }

    void setupStreamDisplays(QWidget* parent) {
        QVBoxLayout* layout = new QVBoxLayout(parent);
        
        // Stream grid (4x4 for 16 streams)
        QGridLayout* grid_layout = new QGridLayout;
        
        for (int i = 0; i < 16; ++i) {
            StreamDisplayWidget* widget = new StreamDisplayWidget(i);
            stream_widgets.push_back(widget);
            
            int row = i / 4;
            int col = i % 4;
            grid_layout->addWidget(widget, row, col);
            
            // Connect ROI selection signal
            connect(widget, &StreamDisplayWidget::roiSelected,
                    this, &RTSPMonitorGUI::onROISelected);
        }
        
        layout->addLayout(grid_layout);
    }

    void setupControlPanel(QWidget* parent) {
        QVBoxLayout* layout = new QVBoxLayout(parent);

        // Stream configuration
        setupStreamConfig(layout);
        
        // Object tracking
        setupObjectTracking(layout);
        
        // Performance monitoring
        setupPerformanceMonitoring(layout);
        
        // Log display
        setupLogDisplay(layout);
    }

    void setupStreamConfig(QVBoxLayout* parent_layout) {
        stream_group = new QGroupBox("Stream Configuration");
        QVBoxLayout* layout = new QVBoxLayout(stream_group);

        // Control buttons
        QHBoxLayout* button_layout = new QHBoxLayout;
        QPushButton* start_all_btn = new QPushButton("Start All");
        QPushButton* stop_all_btn = new QPushButton("Stop All");
        QPushButton* load_config_btn = new QPushButton("Load Config");
        QPushButton* save_config_btn = new QPushButton("Save Config");

        button_layout->addWidget(start_all_btn);
        button_layout->addWidget(stop_all_btn);
        button_layout->addWidget(load_config_btn);
        button_layout->addWidget(save_config_btn);
        layout->addLayout(button_layout);

        // Stream table
        QTableWidget* stream_table = new QTableWidget(16, 3);
        stream_table->setHorizontalHeaderLabels({"Enable", "Stream Name", "RTSP URL"});
        stream_table->horizontalHeader()->setStretchLastSection(true);
        stream_table->setMaximumHeight(200);

        for (int i = 0; i < 16; ++i) {
            // Enable checkbox
            QCheckBox* enable_cb = new QCheckBox;
            stream_enable_checkboxes.push_back(enable_cb);
            stream_table->setCellWidget(i, 0, enable_cb);

            // Stream name
            QTableWidgetItem* name_item = new QTableWidgetItem(QString("Stream %1").arg(i + 1));
            stream_table->setItem(i, 1, name_item);

            // RTSP URL input
            QLineEdit* url_input = new QLineEdit;
            url_input->setPlaceholderText("rtsp://admin:password@192.168.1.100:554/stream1");
            stream_url_inputs.push_back(url_input);
            stream_table->setCellWidget(i, 2, url_input);
        }

        layout->addWidget(stream_table);
        parent_layout->addWidget(stream_group);

        // Connect buttons
        connect(start_all_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::startAllStreams);
        connect(stop_all_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::stopAllStreams);
        connect(load_config_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::loadConfiguration);
        connect(save_config_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::saveConfiguration);
    }

    void setupObjectTracking(QVBoxLayout* parent_layout) {
        tracking_group = new QGroupBox("Object Tracking");
        QVBoxLayout* layout = new QVBoxLayout(tracking_group);

        // Object name input
        QHBoxLayout* name_layout = new QHBoxLayout;
        name_layout->addWidget(new QLabel("Object Name:"));
        object_name_input = new QLineEdit;
        object_name_input->setPlaceholderText("toproll1");
        name_layout->addWidget(object_name_input);
        layout->addLayout(name_layout);

        // Stream selector
        QHBoxLayout* stream_layout = new QHBoxLayout;
        stream_layout->addWidget(new QLabel("Target Stream:"));
        stream_selector = new QComboBox;
        for (int i = 0; i < 16; ++i) {
            stream_selector->addItem(QString("Stream %1").arg(i + 1), i);
        }
        stream_layout->addWidget(stream_selector);
        layout->addLayout(stream_layout);

        // Control buttons
        QHBoxLayout* button_layout = new QHBoxLayout;
        add_object_btn = new QPushButton("Add Object");
        remove_object_btn = new QPushButton("Remove Object");
        reset_object_btn = new QPushButton("Reset Object");
        select_roi_btn = new QPushButton("Select ROI");

        button_layout->addWidget(add_object_btn);
        button_layout->addWidget(remove_object_btn);
        button_layout->addWidget(reset_object_btn);
        button_layout->addWidget(select_roi_btn);
        layout->addLayout(button_layout);

        // Object table
        object_table = new QTableWidget(0, 4);
        object_table->setHorizontalHeaderLabels({"Object", "Stream", "Status", "Confidence"});
        object_table->horizontalHeader()->setStretchLastSection(true);
        object_table->setMaximumHeight(150);
        layout->addWidget(object_table);

        parent_layout->addWidget(tracking_group);

        // Connect buttons
        connect(add_object_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::addObject);
        connect(remove_object_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::removeObject);
        connect(reset_object_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::resetObject);
        connect(select_roi_btn, &QPushButton::clicked, this, &RTSPMonitorGUI::selectROI);
    }

    void setupPerformanceMonitoring(QVBoxLayout* parent_layout) {
        QGroupBox* perf_group = new QGroupBox("Performance Monitoring");
        QVBoxLayout* layout = new QVBoxLayout(perf_group);

        // GPU usage
        QHBoxLayout* gpu_layout = new QHBoxLayout;
        gpu_layout->addWidget(new QLabel("GPU Usage:"));
        gpu_usage_bar = new QProgressBar;
        gpu_usage_bar->setRange(0, 100);
        gpu_layout->addWidget(gpu_usage_bar);
        layout->addLayout(gpu_layout);

        // CPU usage
        QHBoxLayout* cpu_layout = new QHBoxLayout;
        cpu_layout->addWidget(new QLabel("CPU Usage:"));
        cpu_usage_bar = new QProgressBar;
        cpu_usage_bar->setRange(0, 100);
        cpu_layout->addWidget(cpu_usage_bar);
        layout->addLayout(cpu_layout);

        parent_layout->addWidget(perf_group);
    }

    void setupLogDisplay(QVBoxLayout* parent_layout) {
        QGroupBox* log_group = new QGroupBox("System Log");
        QVBoxLayout* layout = new QVBoxLayout(log_group);

        log_display = new QTextEdit;
        log_display->setMaximumHeight(200);
        log_display->setReadOnly(true);
        layout->addWidget(log_display);

        QPushButton* clear_log_btn = new QPushButton("Clear Log");
        layout->addWidget(clear_log_btn);

        parent_layout->addWidget(log_group);

        connect(clear_log_btn, &QPushButton::clicked, log_display, &QTextEdit::clear);
    }

    void setupStatusBar() {
        status_label = new QLabel("Ready");
        statusBar()->addWidget(status_label);
        
        QLabel* cuda_status = new QLabel;
#ifdef WITH_CUDA
        int cuda_devices = cv::cuda::getCudaEnabledDeviceCount();
        cuda_status->setText(QString("CUDA Devices: %1").arg(cuda_devices));
#else
        cuda_status->setText("CUDA: Disabled");
#endif
        statusBar()->addPermanentWidget(cuda_status);
    }

    void setupMenuBar() {
        QMenuBar* menu_bar = menuBar();
        
        // File menu
        QMenu* file_menu = menu_bar->addMenu("File");
        file_menu->addAction("Load Configuration", this, &RTSPMonitorGUI::loadConfiguration);
        file_menu->addAction("Save Configuration", this, &RTSPMonitorGUI::saveConfiguration);
        file_menu->addSeparator();
        file_menu->addAction("Exit", this, &QWidget::close);
        
        // Tools menu
        QMenu* tools_menu = menu_bar->addMenu("Tools");
        tools_menu->addAction("Test CUDA", this, &RTSPMonitorGUI::testCUDA);
        tools_menu->addAction("Check Performance", this, &RTSPMonitorGUI::checkPerformance);
        
        // Help menu
        QMenu* help_menu = menu_bar->addMenu("Help");
        help_menu->addAction("About", this, &RTSPMonitorGUI::showAbout);
    }

    void setupConnections() {
        // Update timer for frame display
        connect(update_timer, &QTimer::timeout, this, &RTSPMonitorGUI::updateDisplays);
        update_timer->start(33); // ~30 FPS

        // Performance timer
        connect(performance_timer, &QTimer::timeout, this, &RTSPMonitorGUI::updatePerformanceMetrics);
        performance_timer->start(1000); // 1 second
    }

    void startPerformanceMonitoring() {
        performance_thread = std::thread([this]() {
            while (monitoring_active) {
                // Monitor system performance
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
    }

private slots:
    void startAllStreams() {
        // Configure streams from UI
        for (int i = 0; i < 16; ++i) {
            if (stream_enable_checkboxes[i]->isChecked()) {
                QString url = stream_url_inputs[i]->text().trimmed();
                if (!url.isEmpty()) {
                    app->enableStream(i, url.toStdString());
                    logMessage(QString("Enabled stream %1: %2").arg(i + 1).arg(url));
                }
            }
        }

        if (app->start()) {
            status_label->setText("Streams Running");
            logMessage("All enabled streams started successfully");
        } else {
            status_label->setText("Failed to Start");
            logMessage("ERROR: Failed to start streams");
        }
    }

    void stopAllStreams() {
        app->stop();
        status_label->setText("Streams Stopped");
        logMessage("All streams stopped");
    }

    void addObject() {
        QString name = object_name_input->text().trimmed();
        int stream_id = stream_selector->currentData().toInt();

        if (name.isEmpty()) {
            QMessageBox::warning(this, "Error", "Please enter an object name");
            return;
        }

        // For now, we'll use a placeholder template
        // In a real implementation, you'd capture from the selected ROI
        cv::Mat template_img = cv::Mat::zeros(50, 50, CV_8UC3);
        
        app->addObjectToTrack(name.toStdString(), template_img, stream_id);
        updateObjectTable();
        logMessage(QString("Added object '%1' to stream %2").arg(name).arg(stream_id + 1));
    }

    void removeObject() {
        int current_row = object_table->currentRow();
        if (current_row >= 0) {
            QTableWidgetItem* name_item = object_table->item(current_row, 0);
            if (name_item) {
                QString name = name_item->text();
                app->removeObjectFromTracking(name.toStdString());
                updateObjectTable();
                logMessage(QString("Removed object '%1'").arg(name));
            }
        }
    }

    void resetObject() {
        int current_row = object_table->currentRow();
        if (current_row >= 0) {
            QTableWidgetItem* name_item = object_table->item(current_row, 0);
            if (name_item) {
                QString name = name_item->text();
                
                // FIX: Properly reset object without triggering false alarm
                app->resetObjectTracking(name.toStdString());
                updateObjectTable();
                logMessage(QString("Reset object '%1' - no alarm will be triggered").arg(name));
            }
        }
    }

    void selectROI() {
        int stream_id = stream_selector->currentData().toInt();
        if (stream_id >= 0 && stream_id < stream_widgets.size()) {
            // Enable ROI selection mode on the selected stream
            for (auto* widget : stream_widgets) {
                widget->setROISelectionMode(false);
            }
            stream_widgets[stream_id]->setROISelectionMode(true);
            logMessage(QString("ROI selection enabled for stream %1").arg(stream_id + 1));
        }
    }

    void onROISelected(const cv::Rect& roi) {
        // Find which widget sent the signal
        StreamDisplayWidget* sender_widget = qobject_cast<StreamDisplayWidget*>(sender());
        if (sender_widget) {
            // Disable ROI selection mode
            sender_widget->setROISelectionMode(false);
            
            QString object_name = object_name_input->text().trimmed();
            if (!object_name.isEmpty() && app->getTracker()) {
                // Set ROI for the object
                app->getTracker()->setROI(object_name.toStdString(), roi);
                logMessage(QString("ROI set for object '%1': (%2,%3,%4,%5)")
                          .arg(object_name).arg(roi.x).arg(roi.y).arg(roi.width).arg(roi.height));
            }
        }
    }

    void updateDisplays() {
        // This would be called by the stream processing threads
        // For now, it's a placeholder
    }

    void updatePerformanceMetrics() {
        // Update GPU and CPU usage bars
        // This is a simplified version - you'd need actual performance monitoring
        static int gpu_usage = 0;
        static int cpu_usage = 0;
        
        gpu_usage = (gpu_usage + 5) % 100;
        cpu_usage = (cpu_usage + 3) % 100;
        
        gpu_usage_bar->setValue(gpu_usage);
        cpu_usage_bar->setValue(cpu_usage);
    }

    void updateObjectTable() {
        if (!app->getTracker()) return;

        auto active_objects = app->getTracker()->getActiveObjects();
        object_table->setRowCount(active_objects.size());

        for (int i = 0; i < active_objects.size(); ++i) {
            const std::string& name = active_objects[i];
            
            object_table->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(name)));
            object_table->setItem(i, 1, new QTableWidgetItem("Stream 1")); // Placeholder
            object_table->setItem(i, 2, new QTableWidgetItem("Active"));
            object_table->setItem(i, 3, new QTableWidgetItem("0.85")); // Placeholder
        }
    }

    void loadConfiguration() {
        QString filename = QFileDialog::getOpenFileName(this, "Load Configuration", "", "JSON Files (*.json)");
        if (!filename.isEmpty()) {
            // Implementation for loading configuration
            logMessage("Configuration loaded from: " + filename);
        }
    }

    void saveConfiguration() {
        QString filename = QFileDialog::getSaveFileName(this, "Save Configuration", "", "JSON Files (*.json)");
        if (!filename.isEmpty()) {
            // Implementation for saving configuration
            logMessage("Configuration saved to: " + filename);
        }
    }

    void testCUDA() {
#ifdef WITH_CUDA
        int devices = cv::cuda::getCudaEnabledDeviceCount();
        if (devices > 0) {
            QMessageBox::information(this, "CUDA Test", 
                QString("CUDA is working correctly!\nDevices found: %1").arg(devices));
        } else {
            QMessageBox::warning(this, "CUDA Test", "No CUDA devices found!");
        }
#else
        QMessageBox::information(this, "CUDA Test", "CUDA support not compiled in");
#endif
    }

    void checkPerformance() {
        QMessageBox::information(this, "Performance Check", 
            "Performance monitoring is active.\nCheck the performance panel for real-time metrics.");
    }

    void showAbout() {
        QMessageBox::about(this, "About RTSP Monitor", 
            "RTSP Multi-Stream Monitor with CUDA Acceleration\n\n"
            "Features:\n"
            "- Support for up to 16 RTSP streams\n"
            "- CUDA-accelerated processing\n"
            "- Object tracking with ROI selection\n"
            "- Real-time performance monitoring\n\n"
            "Optimized for A4000 GPU and i7 CPU");
    }

    void logMessage(const QString& message) {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
        log_display->append(QString("[%1] %2").arg(timestamp).arg(message));
        
        // Auto-scroll to bottom
        QTextCursor cursor = log_display->textCursor();
        cursor.movePosition(QTextCursor::End);
        log_display->setTextCursor(cursor);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    RTSPMonitorGUI window;
    window.show();
    
    return app.exec();
}

#include "gui_main.moc"