#include "RTSPStreamManager.h"

RTSPStreamManager::RTSPStreamManager() 
    : gpu_processor_(std::make_unique<GPUProcessor>()) {
    Logger::log(Logger::INFO, "RTSPStreamManager initialized");
}

RTSPStreamManager::~RTSPStreamManager() {
    stop();
    Logger::log(Logger::INFO, "RTSPStreamManager destroyed");
}

bool RTSPStreamManager::addStream(const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    if (streams_.find(config.stream_id) != streams_.end()) {
        Logger::log(Logger::WARNING, "Stream " + std::to_string(config.stream_id) + " already exists");
        return false;
    }
    
    if (streams_.size() >= MAX_STREAMS) {
        Logger::log(Logger::ERROR, "Maximum number of streams (" + std::to_string(MAX_STREAMS) + ") reached");
        return false;
    }
    
    auto context = std::make_unique<StreamContext>();
    context->config = config;
    
    if (!allocateGPUResources(*context)) {
        Logger::log(Logger::ERROR, "Failed to allocate GPU resources for stream " + std::to_string(config.stream_id));
        return false;
    }
    
    streams_[config.stream_id] = std::move(context);
    
    Logger::log(Logger::INFO, "Added stream " + std::to_string(config.stream_id) + ": " + config.rtsp_url);
    return true;
}

bool RTSPStreamManager::removeStream(int stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        Logger::log(Logger::WARNING, "Stream " + std::to_string(stream_id) + " not found");
        return false;
    }
    
    cleanupStream(*it->second);
    streams_.erase(it);
    
    Logger::log(Logger::INFO, "Removed stream " + std::to_string(stream_id));
    return true;
}

bool RTSPStreamManager::updateStreamConfig(int stream_id, const StreamConfig& config) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        Logger::log(Logger::WARNING, "Stream " + std::to_string(stream_id) + " not found");
        return false;
    }
    
    // Stop current stream processing
    cleanupStream(*it->second);
    
    // Update configuration
    it->second->config = config;
    
    // Restart if manager is running
    if (is_running_.load()) {
        it->second->worker_thread = std::make_unique<std::thread>(&RTSPStreamManager::streamWorker, this, stream_id);
    }
    
    Logger::log(Logger::INFO, "Updated configuration for stream " + std::to_string(stream_id));
    return true;
}

bool RTSPStreamManager::start() {
    if (is_running_.load()) {
        Logger::log(Logger::WARNING, "Stream manager is already running");
        return false;
    }
    
    should_stop_.store(false);
    is_running_.store(true);
    
    // Start event processing thread
    event_thread_ = std::make_unique<std::thread>(&RTSPStreamManager::eventWorker, this);
    
    // Start all stream workers
    std::lock_guard<std::mutex> lock(streams_mutex_);
    for (auto& [stream_id, context] : streams_) {
        if (context->config.enabled) {
            context->worker_thread = std::make_unique<std::thread>(&RTSPStreamManager::streamWorker, this, stream_id);
        }
    }
    
    Logger::log(Logger::INFO, "Started stream manager with " + std::to_string(streams_.size()) + " streams");
    return true;
}

bool RTSPStreamManager::stop() {
    if (!is_running_.load()) {
        return false;
    }
    
    should_stop_.store(true);
    is_running_.store(false);
    
    // Stop all stream workers
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        for (auto& [stream_id, context] : streams_) {
            cleanupStream(*context);
        }
    }
    
    // Stop event thread
    if (event_thread_ && event_thread_->joinable()) {
        event_thread_->join();
    }
    
    Logger::log(Logger::INFO, "Stopped stream manager");
    return true;
}

bool RTSPStreamManager::getLatestFrame(int stream_id, StreamFrame& frame) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }
    
    return it->second->frame_buffer.pop(frame, std::chrono::milliseconds(50));
}

bool RTSPStreamManager::getFrameQueue(int stream_id, std::vector<StreamFrame>& frames, size_t max_frames) {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }
    
    frames.clear();
    frames.reserve(max_frames);
    
    StreamFrame frame;
    while (frames.size() < max_frames && it->second->frame_buffer.pop(frame, std::chrono::milliseconds(10))) {
        frames.push_back(std::move(frame));
    }
    
    return !frames.empty();
}

double RTSPStreamManager::getStreamFPS(int stream_id) const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return 0.0;
    }
    
    return it->second->current_fps.load();
}

size_t RTSPStreamManager::getBufferSize(int stream_id) const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return 0;
    }
    
    return it->second->frame_buffer.size();
}

bool RTSPStreamManager::isStreamConnected(int stream_id) const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return false;
    }
    
    return it->second->is_connected.load();
}

void RTSPStreamManager::setEventCallback(std::function<void(const Event&)> callback) {
    event_callback_ = callback;
}

void RTSPStreamManager::streamWorker(int stream_id) {
    Logger::log(Logger::INFO, "Started worker for stream " + std::to_string(stream_id));
    
    StreamContext* context = nullptr;
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            Logger::log(Logger::ERROR, "Stream context not found for stream " + std::to_string(stream_id));
            return;
        }
        context = it->second.get();
    }
    
    cv::Mat frame;
    auto fps_counter = std::chrono::steady_clock::now();
    int frame_count = 0;
    
    while (!should_stop_.load() && context->config.enabled) {
        try {
            if (!context->is_connected.load()) {
                if (!initializeStream(*context)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
                    continue;
                }
            }
            
            bool success = context->capture.read(frame);
            if (!success || frame.empty()) {
                handleStreamError(stream_id, "Failed to read frame");
                context->is_connected.store(false);
                continue;
            }
            
            if (!processFrame(*context, frame)) {
                Logger::log(Logger::WARNING, "Failed to process frame for stream " + std::to_string(stream_id));
                continue;
            }
            
            // Calculate FPS
            frame_count++;
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - fps_counter);
            
            if (duration.count() >= 1000) {
                double fps = frame_count * 1000.0 / duration.count();
                context->current_fps.store(fps);
                frame_count = 0;
                fps_counter = now;
            }
            
            // Maintain target FPS
            auto target_interval = std::chrono::milliseconds(static_cast<int>(1000.0 / context->config.fps));
            auto elapsed = std::chrono::steady_clock::now() - context->last_frame_time;
            if (elapsed < target_interval) {
                std::this_thread::sleep_for(target_interval - elapsed);
            }
            context->last_frame_time = std::chrono::steady_clock::now();
            
        } catch (const std::exception& e) {
            handleStreamError(stream_id, "Exception in stream worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
    
    Logger::log(Logger::INFO, "Stopped worker for stream " + std::to_string(stream_id));
}

bool RTSPStreamManager::initializeStream(StreamContext& context) {
    try {
        // Configure capture properties for optimal performance
        context.capture.set(cv::CAP_PROP_BUFFERSIZE, 1);
        context.capture.set(cv::CAP_PROP_FPS, context.config.fps);
        context.capture.set(cv::CAP_PROP_FRAME_WIDTH, context.config.width);
        context.capture.set(cv::CAP_PROP_FRAME_HEIGHT, context.config.height);
        
        // Try to open the stream
        if (!context.capture.open(context.config.rtsp_url, cv::CAP_FFMPEG)) {
            Logger::log(Logger::ERROR, "Failed to open RTSP stream: " + context.config.rtsp_url);
            return false;
        }
        
        // Verify stream properties
        double actual_fps = context.capture.get(cv::CAP_PROP_FPS);
        int actual_width = static_cast<int>(context.capture.get(cv::CAP_PROP_FRAME_WIDTH));
        int actual_height = static_cast<int>(context.capture.get(cv::CAP_PROP_FRAME_HEIGHT));
        
        Logger::log(Logger::INFO, "Stream " + std::to_string(context.config.stream_id) + 
                   " opened: " + std::to_string(actual_width) + "x" + std::to_string(actual_height) + 
                   " @ " + std::to_string(actual_fps) + " FPS");
        
        context.is_connected.store(true);
        emitEvent(EventType::STREAM_ERROR, context.config.stream_id, "Stream connected");
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception initializing stream " + std::to_string(context.config.stream_id) + 
                   ": " + e.what());
        return false;
    }
}

void RTSPStreamManager::cleanupStream(StreamContext& context) {
    context.is_connected.store(false);
    
    if (context.worker_thread && context.worker_thread->joinable()) {
        context.worker_thread->join();
        context.worker_thread.reset();
    }
    
    if (context.capture.isOpened()) {
        context.capture.release();
    }
    
    deallocateGPUResources(context);
}

bool RTSPStreamManager::processFrame(StreamContext& context, cv::Mat& frame) {
    try {
        // Upload frame to GPU
        context.gpu_buffer.upload(frame, context.cuda_stream);
        
        // Apply ROI if specified
        cv::cuda::GpuMat processed_frame = context.gpu_buffer;
        if (context.config.roi.width > 0 && context.config.roi.height > 0) {
            processed_frame = context.gpu_buffer(context.config.roi);
        }
        
        // Process frame with GPU
        if (gpu_processor_) {
            gpu_processor_->processFrame(processed_frame, context.cuda_stream);
        }
        
        // Create stream frame
        StreamFrame stream_frame;
        stream_frame.stream_id = context.config.stream_id;
        stream_frame.timestamp = std::chrono::steady_clock::now();
        stream_frame.is_valid = true;
        
        // Download processed frame back to CPU if needed
        processed_frame.download(stream_frame.frame, context.cuda_stream);
        stream_frame.gpu_frame = processed_frame.clone();
        
        // Add to buffer (remove oldest if buffer is full)
        while (context.frame_buffer.size() >= MAX_BUFFER_SIZE) {
            StreamFrame old_frame;
            context.frame_buffer.pop(old_frame, std::chrono::milliseconds(1));
        }
        
        context.frame_buffer.push(std::move(stream_frame));
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception processing frame for stream " + 
                   std::to_string(context.config.stream_id) + ": " + e.what());
        return false;
    }
}

bool RTSPStreamManager::allocateGPUResources(StreamContext& context) {
    try {
        // Allocate GPU memory for frame buffer
        context.gpu_buffer.create(context.config.height, context.config.width, CV_8UC3);
        
        Logger::log(Logger::INFO, "Allocated GPU resources for stream " + std::to_string(context.config.stream_id));
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Failed to allocate GPU resources for stream " + 
                   std::to_string(context.config.stream_id) + ": " + e.what());
        return false;
    }
}

void RTSPStreamManager::deallocateGPUResources(StreamContext& context) {
    try {
        context.gpu_buffer.release();
        Logger::log(Logger::DEBUG, "Deallocated GPU resources for stream " + std::to_string(context.config.stream_id));
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, "Exception deallocating GPU resources: " + std::string(e.what()));
    }
}

void RTSPStreamManager::handleStreamError(int stream_id, const std::string& error_msg) {
    Logger::log(Logger::ERROR, "Stream " + std::to_string(stream_id) + " error: " + error_msg);
    emitEvent(EventType::STREAM_ERROR, stream_id, error_msg);
}

bool RTSPStreamManager::reconnectStream(StreamContext& context) {
    Logger::log(Logger::INFO, "Attempting to reconnect stream " + std::to_string(context.config.stream_id));
    
    if (context.capture.isOpened()) {
        context.capture.release();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(RECONNECT_DELAY_MS));
    return initializeStream(context);
}

void RTSPStreamManager::eventWorker() {
    Logger::log(Logger::INFO, "Started event worker thread");
    
    while (!should_stop_.load()) {
        Event event(EventType::STREAM_ERROR, -1);
        if (event_queue_.pop(event, std::chrono::milliseconds(100))) {
            if (event_callback_) {
                try {
                    event_callback_(event);
                } catch (const std::exception& e) {
                    Logger::log(Logger::ERROR, "Exception in event callback: " + std::string(e.what()));
                }
            }
        }
    }
    
    Logger::log(Logger::INFO, "Stopped event worker thread");
}

void RTSPStreamManager::emitEvent(EventType type, int stream_id, const std::string& message) {
    Event event(type, stream_id);
    event.message = message;
    event_queue_.push(event);
}