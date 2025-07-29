#include "Utils.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <sys/stat.h>
#include <json/json.h>

bool Utils::loadConfiguration(const std::string& config_file, std::vector<StreamConfig>& configs) {
    try {
        if (!fileExists(config_file)) {
            Logger::log(Logger::WARNING, "Configuration file not found: " + config_file);
            return false;
        }
        
        std::ifstream file(config_file);
        Json::Value root;
        file >> root;
        
        configs.clear();
        
        const Json::Value& streams = root["streams"];
        for (const auto& stream_data : streams) {
            StreamConfig config;
            config.stream_id = stream_data["stream_id"].asInt();
            config.rtsp_url = stream_data["rtsp_url"].asString();
            config.enabled = stream_data.get("enabled", true).asBool();
            config.fps = stream_data.get("fps", DEFAULT_FPS).asDouble();
            config.width = stream_data.get("width", DEFAULT_WIDTH).asInt();
            config.height = stream_data.get("height", DEFAULT_HEIGHT).asInt();
            
            if (stream_data.isMember("roi")) {
                const Json::Value& roi = stream_data["roi"];
                config.roi = cv::Rect(
                    roi["x"].asInt(),
                    roi["y"].asInt(),
                    roi["width"].asInt(),
                    roi["height"].asInt()
                );
            }
            
            configs.push_back(config);
        }
        
        Logger::log(Logger::INFO, "Loaded " + std::to_string(configs.size()) + " stream configurations");
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Failed to load configuration: " + std::string(e.what()));
        return false;
    }
}

bool Utils::saveConfiguration(const std::string& config_file, const std::vector<StreamConfig>& configs) {
    try {
        Json::Value root;
        Json::Value streams(Json::arrayValue);
        
        for (const auto& config : configs) {
            Json::Value stream_data;
            stream_data["stream_id"] = config.stream_id;
            stream_data["rtsp_url"] = config.rtsp_url;
            stream_data["enabled"] = config.enabled;
            stream_data["fps"] = config.fps;
            stream_data["width"] = config.width;
            stream_data["height"] = config.height;
            
            if (config.roi.width > 0 && config.roi.height > 0) {
                Json::Value roi;
                roi["x"] = config.roi.x;
                roi["y"] = config.roi.y;
                roi["width"] = config.roi.width;
                roi["height"] = config.roi.height;
                stream_data["roi"] = roi;
            }
            
            streams.append(stream_data);
        }
        
        root["streams"] = streams;
        
        std::ofstream file(config_file);
        file << root;
        
        Logger::log(Logger::INFO, "Saved configuration to " + config_file);
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Failed to save configuration: " + std::string(e.what()));
        return false;
    }
}

void Utils::printSystemInfo() {
    Logger::log(Logger::INFO, "=== System Information ===");
    
    // CPU information
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") != std::string::npos) {
            Logger::log(Logger::INFO, "CPU: " + line.substr(line.find(":") + 2));
            break;
        }
    }
    
    // Memory information
    std::ifstream meminfo("/proc/meminfo");
    while (std::getline(meminfo, line)) {
        if (line.find("MemTotal") != std::string::npos) {
            Logger::log(Logger::INFO, "Memory: " + line.substr(line.find(":") + 2));
            break;
        }
    }
    
    // OpenCV build information
    Logger::log(Logger::INFO, "OpenCV Version: " + cv::getVersionString());
    Logger::log(Logger::INFO, "OpenCV CUDA Support: " + (cv::cuda::getCudaEnabledDeviceCount() > 0 ? "YES" : "NO"));
}

void Utils::printGPUInfo() {
    Logger::log(Logger::INFO, "=== GPU Information ===");
    
    int device_count = cv::cuda::getCudaEnabledDeviceCount();
    if (device_count == 0) {
        Logger::log(Logger::WARNING, "No CUDA-enabled devices found");
        return;
    }
    
    for (int i = 0; i < device_count; ++i) {
        cv::cuda::DeviceInfo dev_info(i);
        Logger::log(Logger::INFO, "Device " + std::to_string(i) + ": " + dev_info.name());
        Logger::log(Logger::INFO, "  Compute Capability: " + std::to_string(dev_info.majorVersion()) + 
                   "." + std::to_string(dev_info.minorVersion()));
        Logger::log(Logger::INFO, "  Total Memory: " + std::to_string(dev_info.totalMemory() / (1024*1024)) + " MB");
        Logger::log(Logger::INFO, "  Free Memory: " + std::to_string(dev_info.freeMemory() / (1024*1024)) + " MB");
    }
}

size_t Utils::getAvailableMemory() {
    std::ifstream meminfo("/proc/meminfo");
    std::string line;
    size_t available_kb = 0;
    
    while (std::getline(meminfo, line)) {
        if (line.find("MemAvailable") != std::string::npos) {
            std::istringstream iss(line);
            std::string key, value, unit;
            iss >> key >> value >> unit;
            available_kb = std::stoull(value);
            break;
        }
    }
    
    return available_kb * 1024; // Convert to bytes
}

double Utils::getCPUUsage() {
    static long long prev_idle = 0, prev_total = 0;
    
    std::ifstream proc_stat("/proc/stat");
    std::string line;
    std::getline(proc_stat, line);
    
    std::istringstream iss(line);
    std::string cpu;
    long long user, nice, system, idle, iowait, irq, softirq, steal;
    
    iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
    
    long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    long long total_diff = total - prev_total;
    long long idle_diff = idle - prev_idle;
    
    double usage = 0.0;
    if (total_diff > 0) {
        usage = 100.0 * (total_diff - idle_diff) / total_diff;
    }
    
    prev_idle = idle;
    prev_total = total;
    
    return usage;
}

double Utils::getGPUUsage() {
    // This would require NVIDIA Management Library (NVML) for accurate GPU usage
    // For now, return 0.0 as placeholder
    return 0.0;
}

bool Utils::fileExists(const std::string& filename) {
    struct stat buffer;
    return (stat(filename.c_str(), &buffer) == 0);
}

std::string Utils::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    return oss.str();
}

std::string Utils::formatDuration(std::chrono::milliseconds duration) {
    auto hours = std::chrono::duration_cast<std::chrono::hours>(duration);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration % std::chrono::hours(1));
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration % std::chrono::minutes(1));
    auto ms = duration % std::chrono::seconds(1);
    
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << hours.count() << ":"
        << std::setfill('0') << std::setw(2) << minutes.count() << ":"
        << std::setfill('0') << std::setw(2) << seconds.count() << "."
        << std::setfill('0') << std::setw(3) << ms.count();
    
    return oss.str();
}

bool Utils::saveImage(const cv::Mat& image, const std::string& filename) {
    try {
        return cv::imwrite(filename, image);
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Failed to save image: " + std::string(e.what()));
        return false;
    }
}

cv::Mat Utils::overlayROI(const cv::Mat& image, const cv::Rect& roi, const cv::Scalar& color) {
    cv::Mat result = image.clone();
    
    if (roi.width > 0 && roi.height > 0) {
        cv::rectangle(result, roi, color, 2);
        
        // Add semi-transparent overlay
        cv::Mat overlay = result.clone();
        cv::rectangle(overlay, roi, color, -1);
        cv::addWeighted(result, 0.8, overlay, 0.2, 0, result);
    }
    
    return result;
}

cv::Mat Utils::drawDetections(const cv::Mat& image, const std::vector<DetectedObject>& objects) {
    cv::Mat result = image.clone();
    
    for (const auto& obj : objects) {
        // Draw bounding box
        cv::Scalar color = obj.is_tracked ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);
        cv::rectangle(result, obj.bbox, color, 2);
        
        // Draw center point
        cv::circle(result, obj.center, 3, color, -1);
        
        // Draw label
        std::string label = obj.class_name + " (" + std::to_string(static_cast<int>(obj.confidence * 100)) + "%)";
        int baseline;
        cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        
        cv::Point label_pos(obj.bbox.x, obj.bbox.y - 5);
        cv::rectangle(result, label_pos, 
                     cv::Point(label_pos.x + text_size.width, label_pos.y - text_size.height - baseline),
                     color, -1);
        
        cv::putText(result, label, cv::Point(label_pos.x, label_pos.y - baseline), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    }
    
    return result;
}

bool Utils::testRTSPConnection(const std::string& rtsp_url, int timeout_ms) {
    try {
        cv::VideoCapture cap;
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
        
        // Set timeout (this may not work with all backends)
        cap.set(cv::CAP_PROP_OPEN_TIMEOUT_MSEC, timeout_ms);
        
        bool success = cap.open(rtsp_url, cv::CAP_FFMPEG);
        if (success) {
            cap.release();
        }
        
        return success;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception testing RTSP connection: " + std::string(e.what()));
        return false;
    }
}

bool Utils::isValidRTSPURL(const std::string& url) {
    std::regex rtsp_regex(R"(^rtsp://[\w\-\.]+(?::\d+)?(?:/[\w\-\./?%&=]*)?$)");
    return std::regex_match(url, rtsp_regex);
}

std::vector<std::string> Utils::split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    
    return tokens;
}

std::string Utils::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::string Utils::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

double Utils::calculateFPS(int frame_count, std::chrono::milliseconds duration) {
    if (duration.count() == 0) return 0.0;
    return frame_count * 1000.0 / duration.count();
}

cv::Rect Utils::scaleRect(const cv::Rect& rect, double scale_x, double scale_y) {
    return cv::Rect(
        static_cast<int>(rect.x * scale_x),
        static_cast<int>(rect.y * scale_y),
        static_cast<int>(rect.width * scale_x),
        static_cast<int>(rect.height * scale_y)
    );
}

cv::Point2f Utils::rectCenter(const cv::Rect& rect) {
    return cv::Point2f(rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f);
}