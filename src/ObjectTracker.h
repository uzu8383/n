#pragma once

#include "Common.h"
#include "ROIManager.h"

class ObjectTracker {
public:
    ObjectTracker();
    ~ObjectTracker();
    
    // Initialization
    bool initialize(const std::string& model_path = "");
    void cleanup();
    
    // Object tracking
    bool trackObjects(int stream_id, cv::Mat& frame, std::vector<DetectedObject>& objects);
    bool trackObjectsGPU(int stream_id, cv::cuda::GpuMat& frame, std::vector<DetectedObject>& objects);
    
    // Object management
    bool introduceObject(int stream_id, const cv::Rect& bbox, const std::string& class_name = "object");
    bool resetTracking(int stream_id);
    bool removeObject(int stream_id, int object_id);
    
    // Configuration
    void setDetectionThreshold(float threshold) { detection_threshold_ = threshold; }
    void setTrackingThreshold(float threshold) { tracking_threshold_ = threshold; }
    void setMaxObjects(int max_objects) { max_objects_per_stream_ = max_objects; }
    
    // State queries
    std::vector<DetectedObject> getTrackedObjects(int stream_id) const;
    size_t getObjectCount(int stream_id) const;
    bool hasObjects(int stream_id) const;
    
    // ROI integration
    void setROIManager(std::shared_ptr<ROIManager> roi_manager);
    
    // Event callbacks
    void setObjectDetectedCallback(std::function<void(int, const DetectedObject&)> callback);
    void setObjectLostCallback(std::function<void(int, int)> callback);
    
private:
    struct TrackedObject {
        int id;
        cv::Rect bbox;
        cv::Point2f center;
        float confidence;
        std::string class_name;
        std::chrono::steady_clock::time_point last_seen;
        std::chrono::steady_clock::time_point first_seen;
        cv::Ptr<cv::Tracker> tracker;
        bool is_active;
        int miss_count;
        
        TrackedObject() : id(-1), confidence(0.0f), is_active(false), miss_count(0) {}
    };
    
    struct StreamTrackingContext {
        std::vector<std::unique_ptr<TrackedObject>> objects;
        int next_object_id;
        bool is_initialized;
        cv::dnn::Net detection_net;
        std::mutex context_mutex;
        
        // Tracking state
        bool object_introduced;
        bool should_reset_on_loss;
        std::chrono::steady_clock::time_point last_detection;
        
        StreamTrackingContext() : next_object_id(1), is_initialized(false), 
                                 object_introduced(false), should_reset_on_loss(false) {}
    };
    
    // Core methods
    bool detectObjects(cv::Mat& frame, std::vector<cv::Rect>& detections, std::vector<float>& confidences);
    bool detectObjectsGPU(cv::cuda::GpuMat& frame, std::vector<cv::Rect>& detections, std::vector<float>& confidences);
    
    void updateTrackers(StreamTrackingContext& context, cv::Mat& frame);
    void associateDetections(StreamTrackingContext& context, 
                           const std::vector<cv::Rect>& detections,
                           const std::vector<float>& confidences);
    
    int createNewTracker(StreamTrackingContext& context, const cv::Rect& bbox, 
                        float confidence, const std::string& class_name);
    
    void removeInactiveObjects(StreamTrackingContext& context);
    void handleObjectLoss(int stream_id, StreamTrackingContext& context, int object_id);
    
    // Utility methods
    float calculateIoU(const cv::Rect& rect1, const cv::Rect& rect2) const;
    cv::Point2f calculateCenter(const cv::Rect& rect) const;
    bool isValidDetection(const cv::Rect& detection, const cv::Size& frame_size) const;
    
    // ROI handling
    bool shouldProcessInROI(int stream_id, const cv::Rect& detection) const;
    cv::Rect getProcessingROI(int stream_id, const cv::Size& frame_size) const;
    
    // Member variables
    std::unordered_map<int, std::unique_ptr<StreamTrackingContext>> stream_contexts_;
    mutable std::mutex contexts_mutex_;
    
    // Configuration
    float detection_threshold_;
    float tracking_threshold_;
    int max_objects_per_stream_;
    int max_miss_count_;
    std::chrono::milliseconds object_timeout_;
    
    // Detection model
    bool use_gpu_detection_;
    std::string model_path_;
    std::vector<std::string> class_names_;
    
    // ROI integration
    std::shared_ptr<ROIManager> roi_manager_;
    
    // Event callbacks
    std::function<void(int, const DetectedObject&)> object_detected_callback_;
    std::function<void(int, int)> object_lost_callback_;
    
    // Constants
    static constexpr float DEFAULT_DETECTION_THRESHOLD = 0.5f;
    static constexpr float DEFAULT_TRACKING_THRESHOLD = 0.3f;
    static constexpr int DEFAULT_MAX_OBJECTS = 10;
    static constexpr int DEFAULT_MAX_MISS_COUNT = 5;
    static constexpr int OBJECT_TIMEOUT_MS = 5000;
};