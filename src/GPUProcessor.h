#pragma once

#include "Common.h"

class GPUProcessor {
public:
    GPUProcessor();
    ~GPUProcessor();
    
    // Initialize GPU resources
    bool initialize();
    void cleanup();
    
    // Frame processing
    bool processFrame(cv::cuda::GpuMat& frame, cv::cuda::Stream& stream);
    
    // Image enhancement
    bool enhanceImage(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, cv::cuda::Stream& stream);
    bool denoiseImage(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, cv::cuda::Stream& stream);
    bool adjustContrast(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, float alpha, float beta, cv::cuda::Stream& stream);
    
    // Object detection preprocessing
    bool preprocessForDetection(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, cv::Size target_size, cv::cuda::Stream& stream);
    
    // Utility functions
    bool resize(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, cv::Size size, cv::cuda::Stream& stream);
    bool convertColorSpace(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, int code, cv::cuda::Stream& stream);
    
    // Performance monitoring
    double getProcessingTime() const { return last_processing_time_; }
    size_t getGPUMemoryUsage() const;
    
private:
    // GPU memory management
    bool allocateMemory();
    void deallocateMemory();
    
    // CUDA kernels
    bool launchEnhancementKernel(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, cv::cuda::Stream& stream);
    
    // Member variables
    bool is_initialized_;
    double last_processing_time_;
    
    // GPU memory buffers
    cv::cuda::GpuMat temp_buffer_;
    cv::cuda::GpuMat processing_buffer_;
    
    // CUDA context
    int device_id_;
    cudaDeviceProp device_props_;
    
    // Performance counters
    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point end_time_;
    
    // Constants
    static constexpr float DEFAULT_CONTRAST_ALPHA = 1.2f;
    static constexpr float DEFAULT_CONTRAST_BETA = 10.0f;
};

// CUDA kernel declarations
extern "C" {
    void launchImageEnhancementKernel(unsigned char* src, unsigned char* dst, 
                                    int width, int height, int channels,
                                    float alpha, float beta, cudaStream_t stream);
    
    void launchDenoiseKernel(unsigned char* src, unsigned char* dst,
                           int width, int height, int channels, cudaStream_t stream);
    
    void launchContrastAdjustmentKernel(unsigned char* src, unsigned char* dst,
                                      int width, int height, int channels,
                                      float alpha, float beta, cudaStream_t stream);
}