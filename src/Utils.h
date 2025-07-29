#pragma once

#include "Common.h"

class Utils {
public:
    // Configuration management
    static bool loadConfiguration(const std::string& config_file, std::vector<StreamConfig>& configs);
    static bool saveConfiguration(const std::string& config_file, const std::vector<StreamConfig>& configs);
    
    // Performance monitoring
    static void printSystemInfo();
    static void printGPUInfo();
    static size_t getAvailableMemory();
    static double getCPUUsage();
    static double getGPUUsage();
    
    // File operations
    static bool fileExists(const std::string& filename);
    static std::string getCurrentTimestamp();
    static std::string formatDuration(std::chrono::milliseconds duration);
    
    // Image operations
    static bool saveImage(const cv::Mat& image, const std::string& filename);
    static cv::Mat overlayROI(const cv::Mat& image, const cv::Rect& roi, const cv::Scalar& color = cv::Scalar(0, 255, 0));
    static cv::Mat drawDetections(const cv::Mat& image, const std::vector<DetectedObject>& objects);
    
    // Network operations
    static bool testRTSPConnection(const std::string& rtsp_url, int timeout_ms = 5000);
    static bool isValidRTSPURL(const std::string& url);
    
    // String operations
    static std::vector<std::string> split(const std::string& str, char delimiter);
    static std::string trim(const std::string& str);
    static std::string toLower(const std::string& str);
    
    // Math operations
    static double calculateFPS(int frame_count, std::chrono::milliseconds duration);
    static cv::Rect scaleRect(const cv::Rect& rect, double scale_x, double scale_y);
    static cv::Point2f rectCenter(const cv::Rect& rect);
    
private:
    Utils() = delete; // Static class
};