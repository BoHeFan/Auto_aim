#include "screen_capturer.hpp"
#include "utils/constants.hpp"

namespace auto_aim {

template<class T>
void SafeRelease(T** ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

ScreenCapturer::ScreenCapturer(int crop_width, int crop_height, int monitor_index)
    : monitor_index_(monitor_index) {
    if (!initialize()) {
        throw std::runtime_error("Failed to initialize screen capturer.");
    }
    
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = crop_width;
    desc.Height = crop_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    
    HRESULT hr = device_->CreateTexture2D(&desc, NULL, &staging_texture_);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create staging texture.");
    }
    
    bgra_buffer_.create(crop_height, crop_width, CV_8UC4);
}

ScreenCapturer::~ScreenCapturer() {
    releaseResources();
}

bool ScreenCapturer::initialize() {
    HRESULT hr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory_);
    if (FAILED(hr)) return false;
    
    if (FAILED(factory_->EnumAdapters1(0, &adapter_))) return false;
    if (FAILED(adapter_->EnumOutputs(monitor_index_, &output_))) return false;
    
    DXGI_OUTPUT_DESC outputDesc;
    output_->GetDesc(&outputDesc);
    width_ = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    height_ = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    
    if (FAILED(D3D11CreateDevice(adapter_, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_))) {
        return false;
    }
    if (FAILED(output_->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1_))) return false;
    if (FAILED(output1_->DuplicateOutput(device_, &duplicator_))) return false;
    
    return true;
}

void ScreenCapturer::releaseResources() {
    SafeRelease(&staging_texture_);
    SafeRelease(&duplicator_);
    SafeRelease(&output1_);
    SafeRelease(&output_);
    SafeRelease(&adapter_);
    SafeRelease(&factory_);
    SafeRelease(&context_);
    SafeRelease(&device_);
}

bool ScreenCapturer::reinitialize() {
    releaseResources();
    access_lost_ = false;
    return initialize();
}

bool ScreenCapturer::captureFrame(cv::Mat& frame, const cv::Rect& crop_region) {
    if (!duplicator_) return false;
    
    IDXGIResource* pDesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;
    HRESULT hr = duplicator_->AcquireNextFrame(constants::DXGI_TIMEOUT_MS, &frameInfo, &pDesktopResource);
    
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
    if (hr == DXGI_ERROR_ACCESS_LOST) {
        access_lost_ = true;
        duplicator_->ReleaseFrame();
        return false;
    }
    if (FAILED(hr)) {
        duplicator_->ReleaseFrame();
        return false;
    }
    
    ID3D11Texture2D* pAcquiredDesktopImage = nullptr;
    hr = pDesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pAcquiredDesktopImage);
    SafeRelease(&pDesktopResource);
    if (FAILED(hr)) {
        duplicator_->ReleaseFrame();
        return false;
    }
    
    D3D11_BOX sourceRegion;
    sourceRegion.left = crop_region.x;
    sourceRegion.right = crop_region.x + crop_region.width;
    sourceRegion.top = crop_region.y;
    sourceRegion.bottom = crop_region.y + crop_region.height;
    sourceRegion.front = 0;
    sourceRegion.back = 1;
    
    context_->CopySubresourceRegion(staging_texture_, 0, 0, 0, 0, pAcquiredDesktopImage, 0, &sourceRegion);
    
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    hr = context_->Map(staging_texture_, 0, D3D11_MAP_READ, 0, &mappedResource);
    if (FAILED(hr)) {
        SafeRelease(&pAcquiredDesktopImage);
        duplicator_->ReleaseFrame();
        return false;
    }
    
    cv::Mat bgra_frame(crop_region.height, crop_region.width, CV_8UC4, mappedResource.pData, mappedResource.RowPitch);
    cv::cvtColor(bgra_frame, frame, cv::COLOR_BGRA2BGR);
    
    context_->Unmap(staging_texture_, 0);
    SafeRelease(&pAcquiredDesktopImage);
    duplicator_->ReleaseFrame();
    return true;
}

} // namespace auto_aim
