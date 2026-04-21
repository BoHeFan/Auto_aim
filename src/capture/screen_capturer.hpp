#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <opencv2/opencv.hpp>
#include <stdexcept>

namespace auto_aim {

class ScreenCapturer {
public:
    explicit ScreenCapturer(int crop_width, int crop_height, int monitor_index = 0);
    ~ScreenCapturer();
    
    ScreenCapturer(const ScreenCapturer&) = delete;
    ScreenCapturer& operator=(const ScreenCapturer&) = delete;
    
    bool captureFrame(cv::Mat& frame, const cv::Rect& crop_region);
    
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getMonitorIndex() const { return monitor_index_; }
    
    bool reinitialize();
    bool hasAccessLost() const { return access_lost_; }

private:
    bool initialize();
    void releaseResources();
    
    ID3D11Texture2D* staging_texture_ = nullptr;
    IDXGIFactory1* factory_ = nullptr;
    IDXGIAdapter1* adapter_ = nullptr;
    IDXGIOutput* output_ = nullptr;
    IDXGIOutput1* output1_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIOutputDuplication* duplicator_ = nullptr;
    
    cv::Mat bgra_buffer_;
    int width_ = 0;
    int height_ = 0;
    int monitor_index_ = 0;
    bool access_lost_ = false;
};

} // namespace auto_aim
