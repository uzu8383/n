#include "Common.h"
#include "RTSPStreamManager.h"
#include "ObjectTracker.h"
#include "ROIManager.h"
#include "Utils.h"

#include <signal.h>
#include <csignal>

// Global variables for signal handling
std::atomic<bool> g_shutdown_requested{false};
std::unique_ptr<RTSPStreamManager> g_stream_manager;

void signalHandler(int signal) {
    Logger::log(Logger::INFO, "Received signal " + std::to_string(signal) + ", shutting down...");
    g_shutdown_requested.store(true);
    
    if (g_stream_manager) {
        g_stream_manager->stop();
    }
}

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -c, --config <file>     Configuration file (default: config.json)\n"
              << "  -h, --help             Show this help message\n"
              << "  -v, --verbose          Enable verbose logging\n"
              << "  --test-streams         Test RTSP connections and exit\n"
              << "  --gpu-info             Show GPU information and exit\n"
              << "  --system-info          Show system information and exit\n"
              << std::endl;
}

bool testStreamConnections(const std::vector<StreamConfig>& configs) {
    Logger::log(Logger::INFO, "Testing RTSP stream connections...");
    
    bool all_connected = true;
    for (const auto& config : configs) {
        if (!config.enabled) continue;
        
        Logger::log(Logger::INFO, "Testing stream " + std::to_string(config.stream_id) + 
                   ": " + config.rtsp_url);
        
        if (Utils::testRTSPConnection(config.rtsp_url, 10000)) {
            Logger::log(Logger::INFO, "  ✓ Connected successfully");
        } else {
            Logger::log(Logger::ERROR, "  ✗ Connection failed");
            all_connected = false;
        }
    }
    
    return all_connected;
}

void setupEventHandlers(RTSPStreamManager& stream_manager, 
                       ObjectTracker& object_tracker,
                       ROIManager& roi_manager) {
    
    // Stream manager events
    stream_manager.setEventCallback([](const Event& event) {
        switch (event.type) {
            case EventType::STREAM_ERROR:
                Logger::log(Logger::ERROR, "Stream " + std::to_string(event.stream_id) + 
                           " error: " + event.message);
                break;
            case EventType::ALARM_TRIGGERED:
                Logger::log(Logger::WARNING, "ALARM: Stream " + std::to_string(event.stream_id) + 
                           " - " + event.message);
                // Here you could add sound alerts, notifications, etc.
                break;
            default:
                break;
        }
    });
    
    // Object tracker events
    object_tracker.setObjectDetectedCallback([&roi_manager](int stream_id, const DetectedObject& obj) {
        Logger::log(Logger::INFO, "Object detected in stream " + std::to_string(stream_id) + 
                   ": " + obj.class_name + " (confidence: " + 
                   std::to_string(static_cast<int>(obj.confidence * 100)) + "%)");
        
        // Mark object as introduced in ROI manager
        roi_manager.markObjectIntroduced(stream_id, obj.bbox);
    });
    
    object_tracker.setObjectLostCallback([&roi_manager](int stream_id, int object_id) {
        Logger::log(Logger::WARNING, "Object " + std::to_string(object_id) + 
                   " lost in stream " + std::to_string(stream_id));
        
        // Mark object as lost in ROI manager (triggers alarm)
        roi_manager.markObjectLost(stream_id);
    });
    
    // ROI manager events
    roi_manager.setROIChangeCallback([](int stream_id, const cv::Rect& roi) {
        if (roi.width > 0 && roi.height > 0) {
            Logger::log(Logger::INFO, "ROI changed for stream " + std::to_string(stream_id) + 
                       ": (" + std::to_string(roi.x) + "," + std::to_string(roi.y) + 
                       "," + std::to_string(roi.width) + "," + std::to_string(roi.height) + ")");
        } else {
            Logger::log(Logger::INFO, "ROI cleared for stream " + std::to_string(stream_id));
        }
    });
}

void monitoringLoop(RTSPStreamManager& stream_manager,
                   ObjectTracker& object_tracker,
                   ROIManager& roi_manager) {
    
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(10);
    
    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= stats_interval) {
            // Print statistics
            Logger::log(Logger::INFO, "=== Performance Statistics ===");
            
            // Stream statistics
            for (int stream_id = 0; stream_id < MAX_STREAMS; ++stream_id) {
                if (stream_manager.isStreamConnected(stream_id)) {
                    double fps = stream_manager.getStreamFPS(stream_id);
                    size_t buffer_size = stream_manager.getBufferSize(stream_id);
                    size_t object_count = object_tracker.getObjectCount(stream_id);
                    
                    Logger::log(Logger::INFO, "Stream " + std::to_string(stream_id) + 
                               ": " + std::to_string(fps) + " FPS, Buffer: " + 
                               std::to_string(buffer_size) + ", Objects: " + 
                               std::to_string(object_count));
                }
            }
            
            // System statistics
            double cpu_usage = Utils::getCPUUsage();
            size_t available_memory = Utils::getAvailableMemory();
            
            Logger::log(Logger::INFO, "System: CPU " + std::to_string(cpu_usage) + 
                       "%, Memory: " + std::to_string(available_memory / (1024*1024)) + " MB available");
            
            last_stats_time = now;
        }
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string config_file = "config.json";
    bool test_streams = false;
    bool show_gpu_info = false;
    bool show_system_info = false;
    bool verbose = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                std::cerr << "Error: --config requires a filename" << std::endl;
                return 1;
            }
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--test-streams") {
            test_streams = true;
        } else if (arg == "--gpu-info") {
            show_gpu_info = true;
        } else if (arg == "--system-info") {
            show_system_info = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // Show information and exit if requested
    if (show_system_info) {
        Utils::printSystemInfo();
        return 0;
    }
    
    if (show_gpu_info) {
        Utils::printGPUInfo();
        return 0;
    }
    
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    Logger::log(Logger::INFO, "Starting RTSP Multi-Stream Processor");
    Logger::log(Logger::INFO, "Configuration file: " + config_file);
    
    // Print system information
    Utils::printSystemInfo();
    Utils::printGPUInfo();
    
    // Load configuration
    std::vector<StreamConfig> stream_configs;
    if (!Utils::loadConfiguration(config_file, stream_configs)) {
        // Create default configuration if file doesn't exist
        Logger::log(Logger::INFO, "Creating default configuration");
        
        StreamConfig default_config;
        default_config.stream_id = 0;
        default_config.rtsp_url = "rtsp://admin:password@192.168.1.100:554/stream1";
        default_config.enabled = false; // Disabled by default
        stream_configs.push_back(default_config);
        
        Utils::saveConfiguration(config_file, stream_configs);
        Logger::log(Logger::INFO, "Please edit " + config_file + " and configure your RTSP streams");
        return 0;
    }
    
    if (stream_configs.empty()) {
        Logger::log(Logger::ERROR, "No stream configurations found");
        return 1;
    }
    
    // Test connections if requested
    if (test_streams) {
        bool all_connected = testStreamConnections(stream_configs);
        return all_connected ? 0 : 1;
    }
    
    try {
        // Initialize components
        auto stream_manager = std::make_unique<RTSPStreamManager>();
        auto object_tracker = std::make_unique<ObjectTracker>();
        auto roi_manager = std::make_shared<ROIManager>();
        
        g_stream_manager = std::move(stream_manager);
        
        // Initialize object tracker
        if (!object_tracker->initialize()) {
            Logger::log(Logger::ERROR, "Failed to initialize object tracker");
            return 1;
        }
        
        // Set up ROI integration
        object_tracker->setROIManager(roi_manager);
        
        // Setup event handlers
        setupEventHandlers(*g_stream_manager, *object_tracker, *roi_manager);
        
        // Add streams to manager
        int enabled_streams = 0;
        for (const auto& config : stream_configs) {
            if (config.enabled) {
                if (g_stream_manager->addStream(config)) {
                    enabled_streams++;
                } else {
                    Logger::log(Logger::ERROR, "Failed to add stream " + std::to_string(config.stream_id));
                }
            }
        }
        
        if (enabled_streams == 0) {
            Logger::log(Logger::ERROR, "No enabled streams configured");
            return 1;
        }
        
        Logger::log(Logger::INFO, "Added " + std::to_string(enabled_streams) + " enabled streams");
        
        // Start stream manager
        if (!g_stream_manager->start()) {
            Logger::log(Logger::ERROR, "Failed to start stream manager");
            return 1;
        }
        
        Logger::log(Logger::INFO, "Stream manager started successfully");
        Logger::log(Logger::INFO, "Processing " + std::to_string(enabled_streams) + " RTSP streams");
        Logger::log(Logger::INFO, "Press Ctrl+C to stop");
        
        // Main processing loop
        while (!g_shutdown_requested.load() && g_stream_manager->isRunning()) {
            // Process frames from all streams
            for (const auto& config : stream_configs) {
                if (!config.enabled) continue;
                
                StreamFrame frame;
                if (g_stream_manager->getLatestFrame(config.stream_id, frame)) {
                    // Track objects in the frame
                    std::vector<DetectedObject> objects;
                    if (object_tracker->trackObjects(config.stream_id, frame.frame, objects)) {
                        // Objects are automatically handled by event callbacks
                        
                        // Optionally save frames with detections for debugging
                        if (verbose && !objects.empty()) {
                            cv::Mat debug_frame = Utils::drawDetections(frame.frame, objects);
                            cv::Rect roi = roi_manager->getROI(config.stream_id);
                            if (roi.width > 0 && roi.height > 0) {
                                debug_frame = Utils::overlayROI(debug_frame, roi);
                            }
                            
                            std::string filename = "debug_stream_" + std::to_string(config.stream_id) + 
                                                 "_" + Utils::getCurrentTimestamp() + ".jpg";
                            Utils::saveImage(debug_frame, filename);
                        }
                    }
                }
            }
            
            // Small delay to prevent excessive CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Cleanup
        Logger::log(Logger::INFO, "Shutting down...");
        
        if (g_stream_manager) {
            g_stream_manager->stop();
        }
        
        object_tracker->cleanup();
        
        // Save ROI state
        roi_manager->saveState("roi_state.json");
        
        Logger::log(Logger::INFO, "Shutdown complete");
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception in main: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}