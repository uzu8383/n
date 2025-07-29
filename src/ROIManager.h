#pragma once

#include "Common.h"

class ROIManager {
public:
    ROIManager();
    ~ROIManager();
    
    // ROI management
    bool setROI(int stream_id, const cv::Rect& roi);
    cv::Rect getROI(int stream_id) const;
    bool hasROI(int stream_id) const;
    bool clearROI(int stream_id);
    
    // ROI history navigation (prevents crashes)
    bool navigateROIHistory(int stream_id, int direction); // -1 for previous, +1 for next
    bool resetROIHistory(int stream_id);
    size_t getROIHistorySize(int stream_id) const;
    
    // ROI validation
    bool isValidROI(const cv::Rect& roi, const cv::Size& frame_size) const;
    cv::Rect constrainROI(const cv::Rect& roi, const cv::Size& frame_size) const;
    
    // Object reset handling
    bool resetObjectTracking(int stream_id);
    bool shouldSearchInROI(int stream_id) const;
    void markObjectIntroduced(int stream_id, const cv::Rect& roi);
    void markObjectLost(int stream_id);
    
    // State management
    void saveState(const std::string& filename) const;
    bool loadState(const std::string& filename);
    void clearAllStates();
    
    // Event callbacks
    void setROIChangeCallback(std::function<void(int, const cv::Rect&)> callback);
    
private:
    struct ROIContext {
        cv::Rect current_roi;
        std::vector<cv::Rect> roi_history;
        size_t current_history_index;
        bool has_valid_roi;
        bool object_introduced;
        bool should_search;
        std::chrono::steady_clock::time_point last_update;
        mutable std::mutex context_mutex;
        
        ROIContext() : current_history_index(0), has_valid_roi(false), 
                      object_introduced(false), should_search(true) {}
    };
    
    // Internal methods
    void addToHistory(ROIContext& context, const cv::Rect& roi);
    bool navigateHistory(ROIContext& context, int direction);
    void resetContext(ROIContext& context);
    void emitROIChangeEvent(int stream_id, const cv::Rect& roi);
    
    // Safe access methods
    ROIContext* getContext(int stream_id);
    const ROIContext* getContext(int stream_id) const;
    
    // Member variables
    std::unordered_map<int, std::unique_ptr<ROIContext>> roi_contexts_;
    mutable std::mutex contexts_mutex_;
    
    // Configuration
    static constexpr size_t MAX_HISTORY_SIZE = 20;
    static constexpr int MIN_ROI_SIZE = 32;
    static constexpr double MIN_ROI_RATIO = 0.01; // 1% of frame size
    
    // Event callback
    std::function<void(int, const cv::Rect&)> roi_change_callback_;
};