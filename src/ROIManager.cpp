#include "ROIManager.h"
#include <fstream>
#include <json/json.h>

ROIManager::ROIManager() {
    Logger::log(Logger::INFO, "ROIManager initialized");
}

ROIManager::~ROIManager() {
    clearAllStates();
    Logger::log(Logger::INFO, "ROIManager destroyed");
}

bool ROIManager::setROI(int stream_id, const cv::Rect& roi) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    // Get or create context
    auto& context_ptr = roi_contexts_[stream_id];
    if (!context_ptr) {
        context_ptr = std::make_unique<ROIContext>();
    }
    
    std::lock_guard<std::mutex> context_lock(context_ptr->context_mutex);
    
    // Validate ROI (basic validation, frame size validation should be done externally)
    if (roi.width <= 0 || roi.height <= 0) {
        Logger::log(Logger::WARNING, "Invalid ROI dimensions for stream " + std::to_string(stream_id));
        return false;
    }
    
    // Update context
    context_ptr->current_roi = roi;
    context_ptr->has_valid_roi = true;
    context_ptr->last_update = std::chrono::steady_clock::now();
    
    // Add to history
    addToHistory(*context_ptr, roi);
    
    Logger::log(Logger::INFO, "Set ROI for stream " + std::to_string(stream_id) + 
               ": (" + std::to_string(roi.x) + "," + std::to_string(roi.y) + 
               "," + std::to_string(roi.width) + "," + std::to_string(roi.height) + ")");
    
    // Emit change event
    emitROIChangeEvent(stream_id, roi);
    
    return true;
}

cv::Rect ROIManager::getROI(int stream_id) const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    const auto* context = getContext(stream_id);
    if (!context) {
        return cv::Rect(); // Empty rect
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    return context->has_valid_roi ? context->current_roi : cv::Rect();
}

bool ROIManager::hasROI(int stream_id) const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    const auto* context = getContext(stream_id);
    if (!context) {
        return false;
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    return context->has_valid_roi;
}

bool ROIManager::clearROI(int stream_id) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto* context = getContext(stream_id);
    if (!context) {
        return false;
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    
    context->current_roi = cv::Rect();
    context->has_valid_roi = false;
    context->object_introduced = false;
    context->should_search = true; // Reset to default search behavior
    
    Logger::log(Logger::INFO, "Cleared ROI for stream " + std::to_string(stream_id));
    
    // Emit change event with empty ROI
    emitROIChangeEvent(stream_id, cv::Rect());
    
    return true;
}

bool ROIManager::navigateROIHistory(int stream_id, int direction) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto* context = getContext(stream_id);
    if (!context) {
        Logger::log(Logger::WARNING, "No ROI context found for stream " + std::to_string(stream_id));
        return false;
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    
    // Prevent crashes by checking history bounds
    if (context->roi_history.empty()) {
        Logger::log(Logger::INFO, "No ROI history available for stream " + std::to_string(stream_id));
        return false;
    }
    
    // Navigate safely
    if (!navigateHistory(*context, direction)) {
        Logger::log(Logger::DEBUG, "Cannot navigate ROI history in direction " + std::to_string(direction) + 
                   " for stream " + std::to_string(stream_id));
        return false;
    }
    
    // Update current ROI from history
    context->current_roi = context->roi_history[context->current_history_index];
    context->has_valid_roi = true;
    context->last_update = std::chrono::steady_clock::now();
    
    Logger::log(Logger::INFO, "Navigated ROI history for stream " + std::to_string(stream_id) + 
               " to index " + std::to_string(context->current_history_index));
    
    // Emit change event
    emitROIChangeEvent(stream_id, context->current_roi);
    
    return true;
}

bool ROIManager::resetROIHistory(int stream_id) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto* context = getContext(stream_id);
    if (!context) {
        return false;
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    
    // Reset to initial state instead of crashing
    context->roi_history.clear();
    context->current_history_index = 0;
    context->current_roi = cv::Rect();
    context->has_valid_roi = false;
    context->object_introduced = false;
    context->should_search = true;
    
    Logger::log(Logger::INFO, "Reset ROI history for stream " + std::to_string(stream_id));
    
    // Emit change event with empty ROI
    emitROIChangeEvent(stream_id, cv::Rect());
    
    return true;
}

size_t ROIManager::getROIHistorySize(int stream_id) const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    const auto* context = getContext(stream_id);
    if (!context) {
        return 0;
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    return context->roi_history.size();
}

bool ROIManager::isValidROI(const cv::Rect& roi, const cv::Size& frame_size) const {
    // Check basic validity
    if (roi.width <= 0 || roi.height <= 0) {
        return false;
    }
    
    // Check minimum size
    if (roi.width < MIN_ROI_SIZE || roi.height < MIN_ROI_SIZE) {
        return false;
    }
    
    // Check if ROI is within frame bounds
    if (roi.x < 0 || roi.y < 0 || 
        roi.x + roi.width > frame_size.width || 
        roi.y + roi.height > frame_size.height) {
        return false;
    }
    
    // Check minimum ratio
    double roi_area = roi.width * roi.height;
    double frame_area = frame_size.width * frame_size.height;
    if (roi_area / frame_area < MIN_ROI_RATIO) {
        return false;
    }
    
    return true;
}

cv::Rect ROIManager::constrainROI(const cv::Rect& roi, const cv::Size& frame_size) const {
    cv::Rect constrained = roi;
    
    // Constrain position
    constrained.x = std::max(0, std::min(constrained.x, frame_size.width - MIN_ROI_SIZE));
    constrained.y = std::max(0, std::min(constrained.y, frame_size.height - MIN_ROI_SIZE));
    
    // Constrain size
    constrained.width = std::max(MIN_ROI_SIZE, 
                                std::min(constrained.width, frame_size.width - constrained.x));
    constrained.height = std::max(MIN_ROI_SIZE, 
                                 std::min(constrained.height, frame_size.height - constrained.y));
    
    return constrained;
}

bool ROIManager::resetObjectTracking(int stream_id) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto* context = getContext(stream_id);
    if (!context) {
        // Create context if it doesn't exist
        roi_contexts_[stream_id] = std::make_unique<ROIContext>();
        context = roi_contexts_[stream_id].get();
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    
    // Reset object tracking state
    context->object_introduced = false;
    context->should_search = true; // Return to initial search behavior
    
    // Clear ROI if object was introduced and then reset
    if (context->has_valid_roi) {
        context->current_roi = cv::Rect();
        context->has_valid_roi = false;
        
        Logger::log(Logger::INFO, "Reset object tracking and cleared ROI for stream " + std::to_string(stream_id));
        
        // Emit change event with empty ROI
        emitROIChangeEvent(stream_id, cv::Rect());
    } else {
        Logger::log(Logger::INFO, "Reset object tracking for stream " + std::to_string(stream_id));
    }
    
    return true;
}

bool ROIManager::shouldSearchInROI(int stream_id) const {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    const auto* context = getContext(stream_id);
    if (!context) {
        return true; // Default behavior: search everywhere
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    
    // If object was introduced and ROI is set, don't search
    // If object was reset, search everywhere
    return context->should_search;
}

void ROIManager::markObjectIntroduced(int stream_id, const cv::Rect& roi) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto& context_ptr = roi_contexts_[stream_id];
    if (!context_ptr) {
        context_ptr = std::make_unique<ROIContext>();
    }
    
    std::lock_guard<std::mutex> context_lock(context_ptr->context_mutex);
    
    context_ptr->object_introduced = true;
    context_ptr->should_search = false; // Stop searching once object is introduced
    
    // Set ROI if provided
    if (roi.width > 0 && roi.height > 0) {
        context_ptr->current_roi = roi;
        context_ptr->has_valid_roi = true;
        addToHistory(*context_ptr, roi);
    }
    
    Logger::log(Logger::INFO, "Marked object as introduced for stream " + std::to_string(stream_id));
}

void ROIManager::markObjectLost(int stream_id) {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    
    auto* context = getContext(stream_id);
    if (!context) {
        return;
    }
    
    std::lock_guard<std::mutex> context_lock(context->context_mutex);
    
    // Object lost - should trigger alarm but not reset search behavior
    // The search behavior should only be reset by explicit user action
    Logger::log(Logger::INFO, "Marked object as lost for stream " + std::to_string(stream_id));
}

void ROIManager::saveState(const std::string& filename) const {
    try {
        Json::Value root;
        Json::Value streams(Json::arrayValue);
        
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        
        for (const auto& [stream_id, context_ptr] : roi_contexts_) {
            if (!context_ptr) continue;
            
            std::lock_guard<std::mutex> context_lock(context_ptr->context_mutex);
            
            Json::Value stream_data;
            stream_data["stream_id"] = stream_id;
            stream_data["has_valid_roi"] = context_ptr->has_valid_roi;
            stream_data["object_introduced"] = context_ptr->object_introduced;
            stream_data["should_search"] = context_ptr->should_search;
            
            if (context_ptr->has_valid_roi) {
                Json::Value roi;
                roi["x"] = context_ptr->current_roi.x;
                roi["y"] = context_ptr->current_roi.y;
                roi["width"] = context_ptr->current_roi.width;
                roi["height"] = context_ptr->current_roi.height;
                stream_data["current_roi"] = roi;
            }
            
            // Save history
            Json::Value history(Json::arrayValue);
            for (const auto& hist_roi : context_ptr->roi_history) {
                Json::Value roi;
                roi["x"] = hist_roi.x;
                roi["y"] = hist_roi.y;
                roi["width"] = hist_roi.width;
                roi["height"] = hist_roi.height;
                history.append(roi);
            }
            stream_data["roi_history"] = history;
            stream_data["current_history_index"] = static_cast<int>(context_ptr->current_history_index);
            
            streams.append(stream_data);
        }
        
        root["streams"] = streams;
        
        std::ofstream file(filename);
        file << root;
        
        Logger::log(Logger::INFO, "Saved ROI state to " + filename);
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Failed to save ROI state: " + std::string(e.what()));
    }
}

bool ROIManager::loadState(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            Logger::log(Logger::WARNING, "Could not open ROI state file: " + filename);
            return false;
        }
        
        Json::Value root;
        file >> root;
        
        std::lock_guard<std::mutex> lock(contexts_mutex_);
        
        // Clear existing state
        roi_contexts_.clear();
        
        const Json::Value& streams = root["streams"];
        for (const auto& stream_data : streams) {
            int stream_id = stream_data["stream_id"].asInt();
            
            auto context = std::make_unique<ROIContext>();
            context->has_valid_roi = stream_data["has_valid_roi"].asBool();
            context->object_introduced = stream_data["object_introduced"].asBool();
            context->should_search = stream_data["should_search"].asBool();
            
            if (context->has_valid_roi && stream_data.isMember("current_roi")) {
                const Json::Value& roi = stream_data["current_roi"];
                context->current_roi = cv::Rect(
                    roi["x"].asInt(),
                    roi["y"].asInt(),
                    roi["width"].asInt(),
                    roi["height"].asInt()
                );
            }
            
            // Load history
            if (stream_data.isMember("roi_history")) {
                const Json::Value& history = stream_data["roi_history"];
                for (const auto& hist_roi : history) {
                    context->roi_history.emplace_back(
                        hist_roi["x"].asInt(),
                        hist_roi["y"].asInt(),
                        hist_roi["width"].asInt(),
                        hist_roi["height"].asInt()
                    );
                }
                
                if (stream_data.isMember("current_history_index")) {
                    context->current_history_index = stream_data["current_history_index"].asUInt();
                    // Ensure index is valid
                    if (context->current_history_index >= context->roi_history.size()) {
                        context->current_history_index = context->roi_history.empty() ? 0 : context->roi_history.size() - 1;
                    }
                }
            }
            
            roi_contexts_[stream_id] = std::move(context);
        }
        
        Logger::log(Logger::INFO, "Loaded ROI state from " + filename);
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Failed to load ROI state: " + std::string(e.what()));
        return false;
    }
}

void ROIManager::clearAllStates() {
    std::lock_guard<std::mutex> lock(contexts_mutex_);
    roi_contexts_.clear();
    Logger::log(Logger::INFO, "Cleared all ROI states");
}

void ROIManager::setROIChangeCallback(std::function<void(int, const cv::Rect&)> callback) {
    roi_change_callback_ = callback;
}

void ROIManager::addToHistory(ROIContext& context, const cv::Rect& roi) {
    // Add to history, maintaining maximum size
    context.roi_history.push_back(roi);
    
    if (context.roi_history.size() > MAX_HISTORY_SIZE) {
        context.roi_history.erase(context.roi_history.begin());
    }
    
    // Update current index to point to the new entry
    context.current_history_index = context.roi_history.size() - 1;
}

bool ROIManager::navigateHistory(ROIContext& context, int direction) {
    if (context.roi_history.empty()) {
        return false;
    }
    
    size_t new_index = context.current_history_index;
    
    if (direction < 0) { // Previous
        if (context.current_history_index == 0) {
            return false; // Already at beginning
        }
        new_index = context.current_history_index - 1;
    } else if (direction > 0) { // Next
        if (context.current_history_index >= context.roi_history.size() - 1) {
            return false; // Already at end
        }
        new_index = context.current_history_index + 1;
    } else {
        return false; // Invalid direction
    }
    
    context.current_history_index = new_index;
    return true;
}

void ROIManager::resetContext(ROIContext& context) {
    context.current_roi = cv::Rect();
    context.roi_history.clear();
    context.current_history_index = 0;
    context.has_valid_roi = false;
    context.object_introduced = false;
    context.should_search = true;
}

void ROIManager::emitROIChangeEvent(int stream_id, const cv::Rect& roi) {
    if (roi_change_callback_) {
        try {
            roi_change_callback_(stream_id, roi);
        } catch (const std::exception& e) {
            Logger::log(Logger::ERROR, "Exception in ROI change callback: " + std::string(e.what()));
        }
    }
}

ROIManager::ROIContext* ROIManager::getContext(int stream_id) {
    auto it = roi_contexts_.find(stream_id);
    return (it != roi_contexts_.end()) ? it->second.get() : nullptr;
}

const ROIManager::ROIContext* ROIManager::getContext(int stream_id) const {
    auto it = roi_contexts_.find(stream_id);
    return (it != roi_contexts_.end()) ? it->second.get() : nullptr;
}