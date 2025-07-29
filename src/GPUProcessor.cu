#include "GPUProcessor.h"

// CUDA kernel for image enhancement
__global__ void imageEnhancementKernel(unsigned char* src, unsigned char* dst,
                                     int width, int height, int channels,
                                     float alpha, float beta) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int idy = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (idx < width && idy < height) {
        int pixel_idx = (idy * width + idx) * channels;
        
        for (int c = 0; c < channels; ++c) {
            float value = src[pixel_idx + c];
            value = alpha * value + beta;
            value = fminf(255.0f, fmaxf(0.0f, value));
            dst[pixel_idx + c] = static_cast<unsigned char>(value);
        }
    }
}

// CUDA kernel for denoising (simple bilateral filter approximation)
__global__ void denoiseKernel(unsigned char* src, unsigned char* dst,
                             int width, int height, int channels) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int idy = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (idx < width && idy < height) {
        int pixel_idx = (idy * width + idx) * channels;
        
        // Simple 3x3 averaging filter for denoising
        float sum[3] = {0.0f, 0.0f, 0.0f};
        int count = 0;
        
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = idx + dx;
                int ny = idy + dy;
                
                if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                    int neighbor_idx = (ny * width + nx) * channels;
                    for (int c = 0; c < channels; ++c) {
                        sum[c] += src[neighbor_idx + c];
                    }
                    count++;
                }
            }
        }
        
        for (int c = 0; c < channels; ++c) {
            dst[pixel_idx + c] = static_cast<unsigned char>(sum[c] / count);
        }
    }
}

// CUDA kernel for contrast adjustment
__global__ void contrastAdjustmentKernel(unsigned char* src, unsigned char* dst,
                                        int width, int height, int channels,
                                        float alpha, float beta) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int idy = blockIdx.y * blockDim.y + threadIdx.y;
    
    if (idx < width && idy < height) {
        int pixel_idx = (idy * width + idx) * channels;
        
        for (int c = 0; c < channels; ++c) {
            float value = src[pixel_idx + c];
            value = alpha * (value - 128.0f) + 128.0f + beta;
            value = fminf(255.0f, fmaxf(0.0f, value));
            dst[pixel_idx + c] = static_cast<unsigned char>(value);
        }
    }
}

// C wrapper functions
extern "C" {
    void launchImageEnhancementKernel(unsigned char* src, unsigned char* dst,
                                    int width, int height, int channels,
                                    float alpha, float beta, cudaStream_t stream) {
        dim3 blockSize(16, 16);
        dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                     (height + blockSize.y - 1) / blockSize.y);
        
        imageEnhancementKernel<<<gridSize, blockSize, 0, stream>>>(
            src, dst, width, height, channels, alpha, beta);
    }
    
    void launchDenoiseKernel(unsigned char* src, unsigned char* dst,
                           int width, int height, int channels, cudaStream_t stream) {
        dim3 blockSize(16, 16);
        dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                     (height + blockSize.y - 1) / blockSize.y);
        
        denoiseKernel<<<gridSize, blockSize, 0, stream>>>(
            src, dst, width, height, channels);
    }
    
    void launchContrastAdjustmentKernel(unsigned char* src, unsigned char* dst,
                                      int width, int height, int channels,
                                      float alpha, float beta, cudaStream_t stream) {
        dim3 blockSize(16, 16);
        dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                     (height + blockSize.y - 1) / blockSize.y);
        
        contrastAdjustmentKernel<<<gridSize, blockSize, 0, stream>>>(
            src, dst, width, height, channels, alpha, beta);
    }
}

// GPUProcessor implementation
GPUProcessor::GPUProcessor() 
    : is_initialized_(false), last_processing_time_(0.0), device_id_(0) {
}

GPUProcessor::~GPUProcessor() {
    cleanup();
}

bool GPUProcessor::initialize() {
    try {
        // Get device count
        int device_count;
        CUDA_CHECK(cudaGetDeviceCount(&device_count));
        
        if (device_count == 0) {
            Logger::log(Logger::ERROR, "No CUDA devices found");
            return false;
        }
        
        // Select best device (assume device 0 for A4000)
        CUDA_CHECK(cudaSetDevice(device_id_));
        CUDA_CHECK(cudaGetDeviceProperties(&device_props_, device_id_));
        
        Logger::log(Logger::INFO, "Using CUDA device: " + std::string(device_props_.name) +
                   " (Compute " + std::to_string(device_props_.major) + "." + 
                   std::to_string(device_props_.minor) + ")");
        
        // Allocate memory
        if (!allocateMemory()) {
            Logger::log(Logger::ERROR, "Failed to allocate GPU memory");
            return false;
        }
        
        is_initialized_ = true;
        Logger::log(Logger::INFO, "GPUProcessor initialized successfully");
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception initializing GPUProcessor: " + std::string(e.what()));
        return false;
    }
}

void GPUProcessor::cleanup() {
    if (is_initialized_) {
        deallocateMemory();
        is_initialized_ = false;
        Logger::log(Logger::INFO, "GPUProcessor cleaned up");
    }
}

bool GPUProcessor::processFrame(cv::cuda::GpuMat& frame, cv::cuda::Stream& stream) {
    if (!is_initialized_) {
        Logger::log(Logger::ERROR, "GPUProcessor not initialized");
        return false;
    }
    
    start_time_ = std::chrono::steady_clock::now();
    
    try {
        // Ensure temp buffer has correct size
        if (temp_buffer_.size() != frame.size() || temp_buffer_.type() != frame.type()) {
            temp_buffer_.create(frame.size(), frame.type());
        }
        
        // Apply image enhancement
        if (!enhanceImage(frame, temp_buffer_, stream)) {
            Logger::log(Logger::WARNING, "Image enhancement failed");
            return false;
        }
        
        // Copy result back to input frame
        temp_buffer_.copyTo(frame, stream);
        
        // Wait for completion and calculate processing time
        stream.waitForCompletion();
        end_time_ = std::chrono::steady_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time_ - start_time_);
        last_processing_time_ = duration.count() / 1000.0; // Convert to milliseconds
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception processing frame: " + std::string(e.what()));
        return false;
    }
}

bool GPUProcessor::enhanceImage(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, cv::cuda::Stream& stream) {
    try {
        if (src.empty()) {
            return false;
        }
        
        // Ensure destination has correct size and type
        if (dst.size() != src.size() || dst.type() != src.type()) {
            dst.create(src.size(), src.type());
        }
        
        // Launch custom enhancement kernel
        launchImageEnhancementKernel(
            src.ptr<unsigned char>(),
            dst.ptr<unsigned char>(),
            src.cols, src.rows, src.channels(),
            DEFAULT_CONTRAST_ALPHA, DEFAULT_CONTRAST_BETA,
            cv::cuda::StreamAccessor::getStream(stream)
        );
        
        // Check for kernel launch errors
        CUDA_CHECK(cudaGetLastError());
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception in enhanceImage: " + std::string(e.what()));
        return false;
    }
}

bool GPUProcessor::denoiseImage(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, cv::cuda::Stream& stream) {
    try {
        if (src.empty()) {
            return false;
        }
        
        if (dst.size() != src.size() || dst.type() != src.type()) {
            dst.create(src.size(), src.type());
        }
        
        launchDenoiseKernel(
            src.ptr<unsigned char>(),
            dst.ptr<unsigned char>(),
            src.cols, src.rows, src.channels(),
            cv::cuda::StreamAccessor::getStream(stream)
        );
        
        CUDA_CHECK(cudaGetLastError());
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception in denoiseImage: " + std::string(e.what()));
        return false;
    }
}

bool GPUProcessor::adjustContrast(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, 
                                 float alpha, float beta, cv::cuda::Stream& stream) {
    try {
        if (src.empty()) {
            return false;
        }
        
        if (dst.size() != src.size() || dst.type() != src.type()) {
            dst.create(src.size(), src.type());
        }
        
        launchContrastAdjustmentKernel(
            src.ptr<unsigned char>(),
            dst.ptr<unsigned char>(),
            src.cols, src.rows, src.channels(),
            alpha, beta,
            cv::cuda::StreamAccessor::getStream(stream)
        );
        
        CUDA_CHECK(cudaGetLastError());
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception in adjustContrast: " + std::string(e.what()));
        return false;
    }
}

bool GPUProcessor::preprocessForDetection(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, 
                                        cv::Size target_size, cv::cuda::Stream& stream) {
    try {
        // Resize to target size
        if (!resize(src, dst, target_size, stream)) {
            return false;
        }
        
        // Convert to RGB if needed (assuming input is BGR)
        if (src.channels() == 3) {
            cv::cuda::GpuMat temp;
            if (!convertColorSpace(dst, temp, cv::COLOR_BGR2RGB, stream)) {
                return false;
            }
            dst = temp;
        }
        
        // Normalize to [0, 1] range (this would typically be done in the detection model)
        dst.convertTo(dst, CV_32F, 1.0/255.0, stream);
        
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception in preprocessForDetection: " + std::string(e.what()));
        return false;
    }
}

bool GPUProcessor::resize(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, 
                         cv::Size size, cv::cuda::Stream& stream) {
    try {
        cv::cuda::resize(src, dst, size, 0, 0, cv::INTER_LINEAR, stream);
        return true;
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception in resize: " + std::string(e.what()));
        return false;
    }
}

bool GPUProcessor::convertColorSpace(cv::cuda::GpuMat& src, cv::cuda::GpuMat& dst, 
                                    int code, cv::cuda::Stream& stream) {
    try {
        cv::cuda::cvtColor(src, dst, code, 0, stream);
        return true;
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception in convertColorSpace: " + std::string(e.what()));
        return false;
    }
}

size_t GPUProcessor::getGPUMemoryUsage() const {
    size_t free_mem, total_mem;
    CUDA_CHECK(cudaMemGetInfo(&free_mem, &total_mem));
    return total_mem - free_mem;
}

bool GPUProcessor::allocateMemory() {
    try {
        // Allocate temporary buffers
        temp_buffer_.create(DEFAULT_HEIGHT, DEFAULT_WIDTH, CV_8UC3);
        processing_buffer_.create(DEFAULT_HEIGHT, DEFAULT_WIDTH, CV_8UC3);
        
        Logger::log(Logger::INFO, "Allocated GPU memory buffers");
        return true;
        
    } catch (const std::exception& e) {
        Logger::log(Logger::ERROR, "Exception allocating GPU memory: " + std::string(e.what()));
        return false;
    }
}

void GPUProcessor::deallocateMemory() {
    try {
        temp_buffer_.release();
        processing_buffer_.release();
        Logger::log(Logger::DEBUG, "Deallocated GPU memory buffers");
    } catch (const std::exception& e) {
        Logger::log(Logger::WARNING, "Exception deallocating GPU memory: " + std::string(e.what()));
    }
}