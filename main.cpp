#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/tracking.hpp>

#ifdef WITH_CUDA
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudaarithm.hpp>
#include <opencv2/cudafeatures2d.hpp>
#endif

#include <windows.h>
#include <mmsystem.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <chrono>
#include <fstream>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "winmm.lib")

// Forward declarations
class RTSPStreamManager;
class ObjectTracker;
class ROIManager;
class AlarmSystem;

// Configuration structure
struct StreamConfig {
    std::string rtsp_url;
    std::string stream_name;
    cv::Rect roi;
    bool enabled = true;
    bool use_gpu = true;
    int stream_id;
};

struct ObjectConfig {
    std::string name;
    cv::Rect template_roi;
    cv::Mat template_image;
    bool is_active = false;
    bool alarm_enabled = true;
    double match_threshold = 0.8;
    int stream_id = -1;
};

// Thread-safe frame buffer
class FrameBuffer {
private:
    std::queue<cv::Mat> frames;
    std::mutex mtx;
    std::condition_variable cv;
    size_t max_size = 3; // Keep only latest 3 frames to prevent memory buildup

public:
    void push(const cv::Mat& frame) {
        std::lock_guard<std::mutex> lock(mtx);
        if (frames.size() >= max_size) {
            frames.pop();
        }
        frames.push(frame.clone());
        cv.notify_one();
    }

    bool pop(cv::Mat& frame, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mtx);
        if (cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !frames.empty(); })) {
            frame = frames.front();
            frames.pop();
            return true;
        }
        return false;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx);
        return frames.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx);
        while (!frames.empty()) {
            frames.pop();
        }
    }
};

// CUDA-accelerated object detector
class CudaObjectDetector {
private:
#ifdef WITH_CUDA
    cv::cuda::GpuMat gpu_frame, gpu_template, gpu_result;
    cv::Ptr<cv::cuda::TemplateMatching> matcher;
#endif
    cv::Mat cpu_template;
    bool use_cuda;

public:
    CudaObjectDetector(bool enable_cuda = true) : use_cuda(enable_cuda) {
#ifdef WITH_CUDA
        if (use_cuda && cv::cuda::getCudaEnabledDeviceCount() > 0) {
            matcher = cv::cuda::createTemplateMatching(CV_8UC3, cv::TM_CCOEFF_NORMED);
        } else {
            use_cuda = false;
        }
#else
        use_cuda = false;
#endif
    }

    void setTemplate(const cv::Mat& template_img) {
        cpu_template = template_img.clone();
#ifdef WITH_CUDA
        if (use_cuda) {
            gpu_template.upload(template_img);
        }
#endif
    }

    double matchTemplate(const cv::Mat& frame, cv::Point& best_loc) {
        if (cpu_template.empty()) return 0.0;

#ifdef WITH_CUDA
        if (use_cuda) {
            gpu_frame.upload(frame);
            matcher->match(gpu_frame, gpu_template, gpu_result);
            
            cv::Mat result;
            gpu_result.download(result);
            
            double min_val, max_val;
            cv::Point min_loc, max_loc;
            cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);
            
            best_loc = max_loc;
            return max_val;
        } else {
#endif
            cv::Mat result;
            cv::matchTemplate(frame, cpu_template, result, cv::TM_CCOEFF_NORMED);
            
            double min_val, max_val;
            cv::Point min_loc, max_loc;
            cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);
            
            best_loc = max_loc;
            return max_val;
#ifdef WITH_CUDA
        }
#endif
    }
};

// ROI Manager with crash protection
class ROIManager {
private:
    std::vector<cv::Rect> roi_history;
    int current_roi_index = -1;
    std::mutex roi_mutex;
    const int MAX_ROI_HISTORY = 10;

public:
    void addROI(const cv::Rect& roi) {
        std::lock_guard<std::mutex> lock(roi_mutex);
        
        // Prevent adding invalid ROIs
        if (roi.width <= 0 || roi.height <= 0) {
            return;
        }
        
        roi_history.push_back(roi);
        if (roi_history.size() > MAX_ROI_HISTORY) {
            roi_history.erase(roi_history.begin());
        }
        current_roi_index = roi_history.size() - 1;
    }

    cv::Rect getCurrentROI() {
        std::lock_guard<std::mutex> lock(roi_mutex);
        if (current_roi_index >= 0 && current_roi_index < roi_history.size()) {
            return roi_history[current_roi_index];
        }
        return cv::Rect(); // Return empty rect if no valid ROI
    }

    bool previousROI() {
        std::lock_guard<std::mutex> lock(roi_mutex);
        if (current_roi_index > 0) {
            current_roi_index--;
            return true;
        }
        return false;
    }

    bool nextROI() {
        std::lock_guard<std::mutex> lock(roi_mutex);
        if (current_roi_index < roi_history.size() - 1) {
            current_roi_index++;
            return true;
        }
        return false;
    }

    void resetToDefault() {
        std::lock_guard<std::mutex> lock(roi_mutex);
        roi_history.clear();
        current_roi_index = -1;
    }

    bool hasValidROI() const {
        std::lock_guard<std::mutex> lock(roi_mutex);
        return current_roi_index >= 0 && current_roi_index < roi_history.size();
    }
};

// Object tracker with improved state management
class ObjectTracker {
private:
    std::map<std::string, ObjectConfig> objects;
    std::map<std::string, std::unique_ptr<CudaObjectDetector>> detectors;
    std::map<std::string, ROIManager> roi_managers;
    std::mutex tracker_mutex;
    
public:
    void addObject(const std::string& name, const cv::Mat& template_img, int stream_id) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        
        ObjectConfig config;
        config.name = name;
        config.template_image = template_img.clone();
        config.is_active = true;
        config.stream_id = stream_id;
        
        objects[name] = config;
        detectors[name] = std::make_unique<CudaObjectDetector>(true);
        detectors[name]->setTemplate(template_img);
        
        // Initialize ROI manager for this object
        roi_managers[name] = ROIManager();
    }

    void removeObject(const std::string& name) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        
        // Reset object state properly
        if (objects.find(name) != objects.end()) {
            objects[name].is_active = false;
            objects[name].template_image = cv::Mat(); // Clear template
            
            // Reset ROI to default state
            roi_managers[name].resetToDefault();
            
            // Remove from maps
            objects.erase(name);
            detectors.erase(name);
            roi_managers.erase(name);
        }
    }

    bool isObjectActive(const std::string& name) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        auto it = objects.find(name);
        return it != objects.end() && it->second.is_active;
    }

    double detectObject(const std::string& name, const cv::Mat& frame, cv::Point& location) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        
        auto obj_it = objects.find(name);
        auto det_it = detectors.find(name);
        
        if (obj_it == objects.end() || det_it == detectors.end() || !obj_it->second.is_active) {
            return 0.0;
        }

        return det_it->second->matchTemplate(frame, location);
    }

    void setROI(const std::string& name, const cv::Rect& roi) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        auto roi_it = roi_managers.find(name);
        if (roi_it != roi_managers.end()) {
            roi_it->second.addROI(roi);
        }
    }

    cv::Rect getROI(const std::string& name) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        auto roi_it = roi_managers.find(name);
        if (roi_it != roi_managers.end()) {
            return roi_it->second.getCurrentROI();
        }
        return cv::Rect();
    }

    bool navigateROI(const std::string& name, bool forward) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        auto roi_it = roi_managers.find(name);
        if (roi_it != roi_managers.end()) {
            return forward ? roi_it->second.nextROI() : roi_it->second.previousROI();
        }
        return false;
    }

    void resetObject(const std::string& name) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        
        // Fix: Properly reset object without causing alarm
        auto obj_it = objects.find(name);
        if (obj_it != objects.end()) {
            obj_it->second.is_active = false;
            obj_it->second.template_image = cv::Mat();
            
            // Reset ROI manager
            auto roi_it = roi_managers.find(name);
            if (roi_it != roi_managers.end()) {
                roi_it->second.resetToDefault();
            }
        }
    }

    std::vector<std::string> getActiveObjects() {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        std::vector<std::string> active;
        for (const auto& pair : objects) {
            if (pair.second.is_active) {
                active.push_back(pair.first);
            }
        }
        return active;
    }
};

// Alarm system
class AlarmSystem {
private:
    std::atomic<bool> alarm_active{false};
    std::thread alarm_thread;
    std::atomic<bool> should_stop{false};
    std::mutex log_mutex;
    std::ofstream log_file;

public:
    AlarmSystem() {
        log_file.open("alarms.log", std::ios::app);
        alarm_thread = std::thread(&AlarmSystem::alarmWorker, this);
    }

    ~AlarmSystem() {
        should_stop = true;
        if (alarm_thread.joinable()) {
            alarm_thread.join();
        }
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    void triggerAlarm(const std::string& object_name, int stream_id) {
        alarm_active = true;
        
        // Log alarm
        std::lock_guard<std::mutex> lock(log_mutex);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        if (log_file.is_open()) {
            log_file << "ALARM: " << std::ctime(&time_t) << " - Object: " << object_name 
                     << ", Stream: " << stream_id << std::endl;
            log_file.flush();
        }
        
        std::cout << "ALARM TRIGGERED: " << object_name << " on stream " << stream_id << std::endl;
    }

private:
    void alarmWorker() {
        while (!should_stop) {
            if (alarm_active) {
                // Play alarm sound
                PlaySound(TEXT("SystemExclamation"), NULL, SND_ALIAS | SND_ASYNC);
                alarm_active = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

// RTSP Stream handler with CUDA optimization
class RTSPStream {
private:
    cv::VideoCapture cap;
    FrameBuffer frame_buffer;
    std::thread capture_thread;
    std::atomic<bool> running{false};
    StreamConfig config;
    std::atomic<bool> frame_ready{false};

#ifdef WITH_CUDA
    cv::cuda::GpuMat gpu_frame, gpu_resized;
#endif

public:
    RTSPStream(const StreamConfig& cfg) : config(cfg) {
        // Configure capture with optimized settings
        cap.open(config.rtsp_url, cv::CAP_FFMPEG);
        if (cap.isOpened()) {
            // Optimize capture settings
            cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
            cap.set(cv::CAP_PROP_FPS, 25);
            cap.set(cv::CAP_PROP_FRAME_WIDTH, 1920);
            cap.set(cv::CAP_PROP_FRAME_HEIGHT, 1080);
        }
    }

    ~RTSPStream() {
        stop();
    }

    bool start() {
        if (!cap.isOpened()) {
            std::cerr << "Failed to open RTSP stream: " << config.rtsp_url << std::endl;
            return false;
        }

        running = true;
        capture_thread = std::thread(&RTSPStream::captureWorker, this);
        return true;
    }

    void stop() {
        running = false;
        if (capture_thread.joinable()) {
            capture_thread.join();
        }
        cap.release();
    }

    bool getFrame(cv::Mat& frame) {
        return frame_buffer.pop(frame, 50); // 50ms timeout
    }

    bool isRunning() const {
        return running && cap.isOpened();
    }

    const StreamConfig& getConfig() const {
        return config;
    }

private:
    void captureWorker() {
        cv::Mat frame;
        while (running) {
            if (cap.read(frame)) {
                if (!frame.empty()) {
                    // Process frame with CUDA if available
                    cv::Mat processed_frame;
                    if (config.use_gpu) {
                        processed_frame = processFrameGPU(frame);
                    } else {
                        processed_frame = frame;
                    }
                    
                    frame_buffer.push(processed_frame);
                    frame_ready = true;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                // Try to reconnect
                if (!cap.isOpened()) {
                    cap.open(config.rtsp_url, cv::CAP_FFMPEG);
                }
            }
        }
    }

    cv::Mat processFrameGPU(const cv::Mat& frame) {
#ifdef WITH_CUDA
        if (cv::cuda::getCudaEnabledDeviceCount() > 0) {
            gpu_frame.upload(frame);
            
            // Resize if needed for performance
            if (frame.cols > 1920 || frame.rows > 1080) {
                cv::cuda::resize(gpu_frame, gpu_resized, cv::Size(1920, 1080));
                cv::Mat result;
                gpu_resized.download(result);
                return result;
            }
            
            cv::Mat result;
            gpu_frame.download(result);
            return result;
        }
#endif
        return frame;
    }
};

// Main stream manager
class RTSPStreamManager {
private:
    std::vector<std::unique_ptr<RTSPStream>> streams;
    std::unique_ptr<ObjectTracker> tracker;
    std::unique_ptr<AlarmSystem> alarm_system;
    std::vector<StreamConfig> configs;
    std::atomic<bool> processing_active{true};
    std::vector<std::thread> processing_threads;

public:
    RTSPStreamManager() {
        tracker = std::make_unique<ObjectTracker>();
        alarm_system = std::make_unique<AlarmSystem>();
    }

    ~RTSPStreamManager() {
        stopAll();
    }

    void addStream(const StreamConfig& config) {
        configs.push_back(config);
        auto stream = std::make_unique<RTSPStream>(config);
        streams.push_back(std::move(stream));
    }

    bool startAll() {
        bool all_started = true;
        
        for (auto& stream : streams) {
            if (!stream->start()) {
                all_started = false;
                std::cerr << "Failed to start stream: " << stream->getConfig().stream_name << std::endl;
            }
        }

        // Start processing threads (one per stream for optimal performance)
        for (int i = 0; i < streams.size(); ++i) {
            processing_threads.emplace_back(&RTSPStreamManager::processStream, this, i);
        }

        return all_started;
    }

    void stopAll() {
        processing_active = false;
        
        for (auto& thread : processing_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        processing_threads.clear();

        for (auto& stream : streams) {
            stream->stop();
        }
        streams.clear();
    }

    void addObject(const std::string& name, const cv::Mat& template_img, int stream_id) {
        tracker->addObject(name, template_img, stream_id);
    }

    void removeObject(const std::string& name) {
        tracker->removeObject(name);
    }

    void resetObject(const std::string& name) {
        tracker->resetObject(name);
    }

    ObjectTracker* getTracker() {
        return tracker.get();
    }

private:
    void processStream(int stream_index) {
        if (stream_index >= streams.size()) return;

        auto& stream = streams[stream_index];
        cv::Mat frame;
        
        while (processing_active && stream->isRunning()) {
            if (stream->getFrame(frame)) {
                processFrame(frame, stream_index);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Minimal delay
        }
    }

    void processFrame(const cv::Mat& frame, int stream_id) {
        auto active_objects = tracker->getActiveObjects();
        
        for (const auto& object_name : active_objects) {
            cv::Point location;
            double confidence = tracker->detectObject(object_name, frame, location);
            
            if (confidence > 0.8) { // Object detected
                // Object is present, no alarm
            } else {
                // Object missing, trigger alarm
                alarm_system->triggerAlarm(object_name, stream_id);
            }
        }
    }
};

// Main application class
class RTSPMonitorApp {
private:
    std::unique_ptr<RTSPStreamManager> stream_manager;
    std::vector<StreamConfig> stream_configs;
    bool running = false;

public:
    RTSPMonitorApp() {
        stream_manager = std::make_unique<RTSPStreamManager>();
        setupDefaultStreams();
    }

    void setupDefaultStreams() {
        // Configure up to 16 RTSP streams
        for (int i = 0; i < 16; ++i) {
            StreamConfig config;
            config.stream_id = i;
            config.stream_name = "Stream_" + std::to_string(i + 1);
            config.rtsp_url = "rtsp://192.168.1." + std::to_string(100 + i) + ":554/stream";
            config.enabled = false; // Disabled by default, enable as needed
            config.use_gpu = true;
            
            stream_configs.push_back(config);
        }
    }

    void enableStream(int index, const std::string& rtsp_url) {
        if (index >= 0 && index < stream_configs.size()) {
            stream_configs[index].rtsp_url = rtsp_url;
            stream_configs[index].enabled = true;
            stream_manager->addStream(stream_configs[index]);
        }
    }

    bool start() {
        // Add enabled streams
        for (const auto& config : stream_configs) {
            if (config.enabled) {
                stream_manager->addStream(config);
            }
        }

        running = stream_manager->startAll();
        return running;
    }

    void stop() {
        running = false;
        stream_manager->stopAll();
    }

    void addObjectToTrack(const std::string& name, const cv::Mat& template_img, int stream_id) {
        stream_manager->addObject(name, template_img, stream_id);
    }

    void removeObjectFromTracking(const std::string& name) {
        stream_manager->removeObject(name);
    }

    void resetObjectTracking(const std::string& name) {
        stream_manager->resetObject(name);
    }

    ObjectTracker* getTracker() {
        return stream_manager->getTracker();
    }

    bool isRunning() const {
        return running;
    }
};

// Main function
int main() {
    std::cout << "RTSP Multi-Stream Monitor with CUDA Acceleration" << std::endl;
    std::cout << "=================================================" << std::endl;

#ifdef WITH_CUDA
    int cuda_devices = cv::cuda::getCudaEnabledDeviceCount();
    std::cout << "CUDA devices available: " << cuda_devices << std::endl;
    if (cuda_devices > 0) {
        cv::cuda::printShortCudaDeviceInfo(cv::cuda::getDevice());
    }
#else
    std::cout << "CUDA support not compiled in" << std::endl;
#endif

    RTSPMonitorApp app;

    // Example: Enable some streams (modify URLs as needed)
    app.enableStream(0, "rtsp://admin:password@192.168.1.100:554/stream1");
    app.enableStream(1, "rtsp://admin:password@192.168.1.101:554/stream1");
    // Add more streams as needed...

    if (!app.start()) {
        std::cerr << "Failed to start application" << std::endl;
        return -1;
    }

    std::cout << "Application started successfully. Press 'q' to quit." << std::endl;

    // Simple console interface
    char key;
    while (app.isRunning() && (key = getchar()) != 'q') {
        if (key == 'r') {
            // Example: Reset object tracking
            std::cout << "Enter object name to reset: ";
            std::string object_name;
            std::cin >> object_name;
            app.resetObjectTracking(object_name);
            std::cout << "Object " << object_name << " reset successfully." << std::endl;
        }
    }

    app.stop();
    std::cout << "Application stopped." << std::endl;
    return 0;
}