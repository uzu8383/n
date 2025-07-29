#pragma once

#include "Common.h"
#include "GPUProcessor.h"

class RTSPStreamManager {
public:
    RTSPStreamManager();
    ~RTSPStreamManager();
    
    // Stream management
    bool addStream(const StreamConfig& config);
    bool removeStream(int stream_id);
    bool updateStreamConfig(int stream_id, const StreamConfig& config);
    
    // Control methods
    bool start();
    bool stop();
    bool isRunning() const { return is_running_.load(); }
    
    // Frame access
    bool getLatestFrame(int stream_id, StreamFrame& frame);
    bool getFrameQueue(int stream_id, std::vector<StreamFrame>& frames, size_t max_frames = 5);
    
    // Statistics
    double getStreamFPS(int stream_id) const;
    size_t getBufferSize(int stream_id) const;
    bool isStreamConnected(int stream_id) const;
    
    // Event handling
    void setEventCallback(std::function<void(const Event&)> callback);
    
private:
    struct StreamContext {
        StreamConfig config;
        cv::VideoCapture capture;
        std::unique_ptr<std::thread> worker_thread;
        ThreadSafeQueue<StreamFrame> frame_buffer;
        std::atomic<bool> is_connected{false};
        std::atomic<double> current_fps{0.0};
        std::chrono::steady_clock::time_point last_frame_time;
        std::mutex context_mutex;
        
        // GPU resources
        cv::cuda::GpuMat gpu_buffer;
        cv::cuda::Stream cuda_stream;
        
        StreamContext() = default;
        ~StreamContext() {
            if (worker_thread && worker_thread->joinable()) {
                worker_thread->join();
            }
        }
    };
    
    // Core methods
    void streamWorker(int stream_id);
    bool initializeStream(StreamContext& context);
    void cleanupStream(StreamContext& context);
    bool processFrame(StreamContext& context, cv::Mat& frame);
    
    // GPU memory management
    bool allocateGPUResources(StreamContext& context);
    void deallocateGPUResources(StreamContext& context);
    
    // Error handling
    void handleStreamError(int stream_id, const std::string& error_msg);
    bool reconnectStream(StreamContext& context);
    
    // Member variables
    std::unordered_map<int, std::unique_ptr<StreamContext>> streams_;
    std::mutex streams_mutex_;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> should_stop_{false};
    
    // GPU processor
    std::unique_ptr<GPUProcessor> gpu_processor_;
    
    // Event system
    std::function<void(const Event&)> event_callback_;
    ThreadSafeQueue<Event> event_queue_;
    std::unique_ptr<std::thread> event_thread_;
    
    // Configuration
    static constexpr int RECONNECT_DELAY_MS = 5000;
    static constexpr int FRAME_TIMEOUT_MS = 1000;
    static constexpr size_t MAX_BUFFER_SIZE = 30;
    
    // Event processing
    void eventWorker();
    void emitEvent(EventType type, int stream_id, const std::string& message = "");
};