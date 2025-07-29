#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <opencv2/tracking.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/cudaimgproc.hpp>
#include <opencv2/cudawarping.hpp>
#include <opencv2/cudaarithm.hpp>

#include <cuda_runtime.h>
#include <cuda_runtime_api.h>
#include <device_launch_parameters.h>
#include <curand_kernel.h>

#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <unordered_map>
#include <chrono>
#include <iostream>
#include <fstream>
#include <algorithm>

// CUDA error checking macro
#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            std::cerr << "CUDA error at " << __FILE__ << ":" << __LINE__ << " - " << cudaGetErrorString(err) << std::endl; \
            exit(1); \
        } \
    } while(0)

// Configuration constants
constexpr int MAX_STREAMS = 16;
constexpr int DEFAULT_BUFFER_SIZE = 10;
constexpr int GPU_MEMORY_POOL_SIZE = 1024 * 1024 * 512; // 512MB
constexpr double DEFAULT_FPS = 25.0;
constexpr int DEFAULT_WIDTH = 1920;
constexpr int DEFAULT_HEIGHT = 1080;

// Structure definitions
struct StreamConfig {
    std::string rtsp_url;
    int stream_id;
    cv::Rect roi;
    bool enabled;
    double fps;
    int width;
    int height;
    
    StreamConfig() : stream_id(-1), enabled(false), fps(DEFAULT_FPS), 
                    width(DEFAULT_WIDTH), height(DEFAULT_HEIGHT) {}
};

struct DetectedObject {
    int id;
    cv::Rect bbox;
    float confidence;
    std::string class_name;
    cv::Point2f center;
    std::chrono::steady_clock::time_point last_seen;
    bool is_tracked;
    
    DetectedObject() : id(-1), confidence(0.0f), is_tracked(false) {}
};

struct ROIState {
    cv::Rect current_roi;
    std::vector<cv::Rect> roi_history;
    size_t history_index;
    bool is_valid;
    std::mutex roi_mutex;
    
    ROIState() : history_index(0), is_valid(false) {}
};

struct StreamFrame {
    cv::Mat frame;
    cv::cuda::GpuMat gpu_frame;
    std::chrono::steady_clock::time_point timestamp;
    int stream_id;
    bool is_valid;
    
    StreamFrame() : stream_id(-1), is_valid(false) {}
};

// Event types for communication
enum class EventType {
    OBJECT_DETECTED,
    OBJECT_LOST,
    STREAM_ERROR,
    ROI_CHANGED,
    ALARM_TRIGGERED
};

struct Event {
    EventType type;
    int stream_id;
    DetectedObject object;
    std::string message;
    std::chrono::steady_clock::time_point timestamp;
    
    Event(EventType t, int sid) : type(t), stream_id(sid), 
                                  timestamp(std::chrono::steady_clock::now()) {}
};

// Thread-safe queue template
template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;

public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        condition_.notify_one();
    }

    bool pop(T& item, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (condition_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            item = queue_.front();
            queue_.pop();
            return true;
        }
        return false;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

// Logging utility
class Logger {
public:
    enum Level { DEBUG, INFO, WARNING, ERROR };
    
    static void log(Level level, const std::string& message) {
        std::lock_guard<std::mutex> lock(log_mutex_);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        
        std::cout << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] "
                  << "[" << level_str[level] << "] " << message << std::endl;
    }
    
private:
    static std::mutex log_mutex_;
};

std::mutex Logger::log_mutex_;