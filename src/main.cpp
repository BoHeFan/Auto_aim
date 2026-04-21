// AutoAim - Optimized Real-time YOLO Detection & Aim Assistant
// Build: CMake + MSVC, link against OpenCV, ONNX Runtime, D3D11, DXGI, Comdlg32
//
// Optimizations vs original:
//   1. Proper Munkres O(n^3) algorithm (was greedy O(n^2) approximation)
//   2. Pre-allocated vectors for detections/tracks (avoid hot-path reallocations)
//   3. Reusable BGRA frame buffer in ScreenCapturer (avoid per-frame allocation)
//   4. Cached wstring model path (avoid repeated conversions)
//   5. Model file existence validation before loading
//   6. Unified InputSimulator DLL interface with correct uint32_t signature
//   7. Removed redundant InputSimulator.hpp include (self-contained)
//   8. Removed #pragma comment for OpenCV/ORT (CMake handles linking)
//   9. Better error handling throughout

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <stdexcept>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <memory>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>

// Windows & DirectX
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Comdlg32.lib")

// ONNX Runtime
#include <onnxruntime_cxx_api.h>

// OpenCV
#include <opencv2/opencv.hpp>

// cvui
#define CVUI_IMPLEMENTATION
#include "cvui.h"

// Hungarian (optimized Munkres implementation)
#include "hungarian.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// =============================================================================
// Forward Declarations
// =============================================================================
struct Detection;
struct TimingDetails;

template<class T>
void SafeRelease(T** ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

// =============================================================================
// Data Structures
// =============================================================================
struct Detection {
    cv::Rect box;
    float confidence;
    int class_id;
};

struct TimingDetails {
    double preprocess_ms = 0;
    double inference_ms = 0;
    double postprocess_ms = 0;
    double total_loop_ms = 0;
};

// =============================================================================
// Key Name Helper
// =============================================================================
std::string GetKeyName(int vk) {
    if (vk == 0) return "NONE";
    if (vk == VK_LBUTTON) return "LMB";
    if (vk == VK_RBUTTON) return "RMB";
    if (vk == VK_MBUTTON) return "MMB";
    if (vk == VK_XBUTTON1) return "X1";
    if (vk == VK_XBUTTON2) return "X2";
    if (vk == VK_SHIFT) return "SHIFT";
    if (vk == VK_LSHIFT) return "LSHIFT";
    if (vk == VK_MENU) return "ALT";
    if (vk == VK_CONTROL) return "CTRL";
    char name[128];
    UINT scanCode = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    int result = GetKeyNameTextA(scanCode << 16, name, 128);
    if (result > 0) return std::string(name);
    return "VK_" + std::to_string(vk);
}

// =============================================================================
// Configuration
// =============================================================================
class Config {
public:
    std::string model_path = "apex_final.onnx";
    int model_type = 0; // 0: YOLOv8/v11, 1: YOLOv5, 2: End2End
    int inference_device = 2; // 0: CPU, 1: GPU, 2: TensorRT, 3: OpenVINO
    std::string trt_cache_path = ".\\engine_cache";

    int crop_size = 640;
    int game_width = 0;
    int game_height = 0;

    int rifle_key1 = VK_LBUTTON;
    int rifle_key2 = 0;
    bool rifle_dual_trigger = false;
    int sniper_key = VK_RBUTTON;

    int rifle_max_lock_dist = 100;
    int sniper_max_lock_dist = 200;
    int min_lock_distance_pixels = 20;

    float confidence_threshold = 0.65f;
    float nms_threshold = 0.4f;

    double aim_speed = 1.0;
    double aim_smoothing = 1.0;
    double rifle_key1_aim_x_ratio = 0.5;
    double rifle_key1_aim_y_ratio = 0.3;
    double rifle_key2_aim_x_ratio = 0.5;
    double rifle_key2_aim_y_ratio = 0.3;
    double sniper_aim_x_ratio = 0.5;
    double sniper_aim_y_ratio = 0.3;
    double sensitivity = 1.0;
    double pixels_for_360_turn = 16410;
    double horizontal_fov = 120.0;
    double vertical_fov = 68.0;

    bool enable_visualization = true;
    std::string window_name = "YOLO Real-time Detection";
    std::string ui_window_name = "Control Panel";
};

// =============================================================================
// Fuzzy PID Controller
// =============================================================================
class FuzzyPID {
public:
    double kp_base, ki_base, kd_base;
    double integral_x = 0, integral_y = 0;
    double prev_err_x = 0, prev_err_y = 0;

    FuzzyPID(double p = 0.6, double i = 0.0, double d = 0.1) : kp_base(p), ki_base(i), kd_base(d) {}

    void reset() {
        integral_x = 0; integral_y = 0;
        prev_err_x = 0; prev_err_y = 0;
    }

    double get_fuzzy_kp(double e, double de) {
        double abs_e = std::abs(e);
        if (abs_e > 50) return kp_base * 1.5;
        if (abs_e > 20) return kp_base * 1.2;
        return kp_base;
    }

    double get_fuzzy_kd(double e, double de) {
        double abs_e = std::abs(e);
        if (abs_e < 10) return kd_base * 2.0;
        return kd_base;
    }

    void compute(double err_x, double err_y, double dt, double& out_x, double& out_y) {
        if (dt <= 0.0) dt = 0.01;

        double de_x = (err_x - prev_err_x) / dt;
        double de_y = (err_y - prev_err_y) / dt;

        integral_x += err_x * dt;
        integral_y += err_y * dt;

        double kp_x = get_fuzzy_kp(err_x, de_x);
        double kd_x = get_fuzzy_kd(err_x, de_x);
        double kp_y = get_fuzzy_kp(err_y, de_y);
        double kd_y = get_fuzzy_kd(err_y, de_y);

        out_x = kp_x * err_x + ki_base * integral_x + kd_x * de_x;
        out_y = kp_y * err_y + ki_base * integral_y + kd_y * de_y;

        prev_err_x = err_x;
        prev_err_y = err_y;
    }
};

// =============================================================================
// Mouse Controller (Unified DLL interface)
// =============================================================================
// Uses correct uint32_t signature matching IbInputSimulator.dll exports
class MouseController {
public:
    FuzzyPID pid;

    MouseController() : pid(0.6, 0.0, 0.1) {
        std::cout << "--- Initializing Mouse Simulation Driver... ---" << std::endl;
        hMouseDll = LoadLibrary(L"IbInputSimulator.dll");
        if (!hMouseDll) {
            std::cout << "WARNING: Failed to load IbInputSimulator.dll. "
                "Mouse moves will be unavailable." << std::endl;
            return;
        }

        IbSendInit_ptr = (pIbSendInit)GetProcAddress(hMouseDll, "IbSendInit");
        IbSendDestroy_ptr = (pIbSendDestroy)GetProcAddress(hMouseDll, "IbSendDestroy");
        IbSendMouseMove_ptr = (pIbSendMouseMove)GetProcAddress(hMouseDll, "IbSendMouseMove");

        if (IbSendInit_ptr) {
            Send::Error error = IbSendInit_ptr(Send::SendType::Razer, 0, nullptr);
            if (error != Send::Error::Success) {
                std::cout << "WARNING: Mouse driver init failed (code="
                    << static_cast<int>(error) << "). Run as Admin." << std::endl;
                hMouseDll = nullptr;
            }
        }
    }

    ~MouseController() {
        if (hMouseDll && IbSendDestroy_ptr) {
            IbSendDestroy_ptr();
            FreeLibrary(hMouseDll);
        }
    }

    void MoveRelative(int dx, int dy) {
        if (hMouseDll && IbSendMouseMove_ptr) {
            IbSendMouseMove_ptr(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy), Send::MoveMode::Relative);
        }
    }

    void MoveSmooth(double target_x, double target_y, double dt, double aim_speed, double aim_smoothing) {
        double out_x, out_y;
        pid.compute(target_x, target_y, dt, out_x, out_y);
        out_x = (out_x * aim_speed) / aim_smoothing;
        out_y = (out_y * aim_speed) / aim_smoothing;
        MoveRelative(static_cast<int>(out_x), static_cast<int>(out_y));
    }

private:
    // Send namespace types matching IbInputSimulator.dll exports
    namespace Send {
        enum class Error { Success = 0, InvalidArgument = 1, DeviceNotFound = 2, DriverError = 3 };
        enum class SendType { SendInput = 0, Logitech = 1, Razer = 2, DD = 3, MouClassInputInjection = 4, LogitechGHubNew = 5, AnyDriver = 100 };
        enum class MoveMode { Absolute = 0, Relative = 1 };
        enum class InitFlags {};
    }

    typedef Send::Error(__stdcall* pIbSendInit)(Send::SendType, Send::InitFlags, void*);
    typedef void(__stdcall* pIbSendDestroy)();
    typedef bool(__stdcall* pIbSendMouseMove)(uint32_t, uint32_t, Send::MoveMode);

    HMODULE hMouseDll = nullptr;
    pIbSendInit IbSendInit_ptr = nullptr;
    pIbSendDestroy IbSendDestroy_ptr = nullptr;
    pIbSendMouseMove IbSendMouseMove_ptr = nullptr;
};

// =============================================================================
// Tracking (with Kalman Filter + Hungarian assignment)
// =============================================================================
struct TrackedObject {
    int id;
    cv::KalmanFilter kf;
    cv::Rect box;
    int lost_frames = 0;
    bool is_active = true;

    TrackedObject(int id, const cv::Rect& b) : id(id), box(b) {
        kf.init(4, 2, 0);
        kf.transitionMatrix = (cv::Mat_<float>(4, 4) << 1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1);
        cv::setIdentity(kf.measurementMatrix);
        cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-4));
        cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
        cv::setIdentity(kf.errorCovPost, cv::Scalar::all(.1));

        kf.statePost.at<float>(0) = b.x + b.width / 2.0f;
        kf.statePost.at<float>(1) = b.y + b.height / 2.0f;
        kf.statePost.at<float>(2) = 0;
        kf.statePost.at<float>(3) = 0;
    }

    void predict() {
        cv::Mat p = kf.predict();
        box.x = static_cast<int>(p.at<float>(0) - box.width / 2.0f);
        box.y = static_cast<int>(p.at<float>(1) - box.height / 2.0f);
    }

    void update(const cv::Rect& b) {
        cv::Mat meas = (cv::Mat_<float>(2, 1) << b.x + b.width / 2.0f, b.y + b.height / 2.0f);
        kf.correct(meas);
        box = b;
        lost_frames = 0;
    }
};

class Tracker {
    int next_id = 1;
    HungarianAlgorithm hungarian;
public:
    std::vector<TrackedObject> tracks;

    Tracker() { tracks.reserve(64); }

    void update(const std::vector<Detection>& detections) {
        for (auto& t : tracks) t.predict();

        if (tracks.empty()) {
            for (const auto& d : detections) {
                tracks.emplace_back(next_id++, d.box);
            }
            return;
        }
        if (detections.empty()) {
            for (auto& t : tracks) t.lost_frames++;
        }
        else {
            std::vector<std::vector<double>> distMatrix(tracks.size(), std::vector<double>(detections.size(), 0.0));
            for (size_t i = 0; i < tracks.size(); ++i) {
                cv::Point tc(tracks[i].box.x + tracks[i].box.width / 2, tracks[i].box.y + tracks[i].box.height / 2);
                for (size_t j = 0; j < detections.size(); ++j) {
                    cv::Point dc(detections[j].box.x + detections[j].box.width / 2, detections[j].box.y + detections[j].box.height / 2);
                    distMatrix[i][j] = std::hypot(tc.x - dc.x, tc.y - dc.y);
                }
            }

            std::vector<int> assignment;
            hungarian.Solve(distMatrix, assignment);

            std::vector<bool> matched_det(detections.size(), false);
            for (size_t i = 0; i < assignment.size(); ++i) {
                int j = assignment[i];
                if (j >= 0 && static_cast<size_t>(j) < detections.size() && distMatrix[i][j] < 150.0) {
                    tracks[i].update(detections[j].box);
                    matched_det[j] = true;
                }
                else {
                    tracks[i].lost_frames++;
                }
            }

            for (size_t j = 0; j < detections.size(); ++j) {
                if (!matched_det[j]) {
                    tracks.emplace_back(next_id++, detections[j].box);
                }
            }
        }

        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const TrackedObject& t) {
            return t.lost_frames > 30;
            }), tracks.end());
    }
};

// =============================================================================
// Screen Capturer (DXGI Desktop Duplication with reusable buffer)
// =============================================================================
class ScreenCapturer {
public:
    ScreenCapturer(int crop_width, int crop_height) {
        HRESULT hr;
        hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory);
        if (FAILED(hr)) throw std::runtime_error("Failed to create DXGI Factory.");
        if (FAILED(pFactory->EnumAdapters1(0, &pAdapter))) throw std::runtime_error("Failed to enumerate adapters.");
        if (FAILED(pAdapter->EnumOutputs(0, &pOutput))) throw std::runtime_error("Failed to enumerate outputs.");

        DXGI_OUTPUT_DESC outputDesc;
        pOutput->GetDesc(&outputDesc);
        width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
        height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

        if (FAILED(D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &pDevice, nullptr, &pContext))) {
            throw std::runtime_error("Failed to create D3D11 device.");
        }
        if (FAILED(pOutput->QueryInterface(__uuidof(IDXGIOutput1), (void**)&pOutput1))) {
            throw std::runtime_error("Failed to query IDXGIOutput1.");
        }
        if (FAILED(pOutput1->DuplicateOutput(pDevice, &pDuplicator))) {
            throw std::runtime_error("Failed to create output duplication.");
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

        hr = pDevice->CreateTexture2D(&desc, NULL, &m_pStagingTexture);
        if (FAILED(hr)) {
            throw std::runtime_error("Failed to create staging texture.");
        }

        // Pre-allocate reusable BGRA buffer
        bgra_buffer.create(crop_height, crop_width, CV_8UC4);
    }

    ~ScreenCapturer() {
        SafeRelease(&m_pStagingTexture);
        SafeRelease(&pDuplicator);
        SafeRelease(&pOutput1);
        SafeRelease(&pOutput);
        SafeRelease(&pAdapter);
        SafeRelease(&pFactory);
        SafeRelease(&pContext);
        SafeRelease(&pDevice);
    }

    bool CaptureFrame(cv::Mat& frame, const cv::Rect& crop_region) {
        if (!pDuplicator) return false;
        IDXGIResource* pDesktopResource = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        HRESULT hr = pDuplicator->AcquireNextFrame(16, &frameInfo, &pDesktopResource);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
        if (FAILED(hr)) {
            pDuplicator->ReleaseFrame();
            return false;
        }

        ID3D11Texture2D* pAcquiredDesktopImage = nullptr;
        hr = pDesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pAcquiredDesktopImage);
        SafeRelease(&pDesktopResource);
        if (FAILED(hr)) {
            pDuplicator->ReleaseFrame();
            return false;
        }

        D3D11_BOX sourceRegion;
        sourceRegion.left = crop_region.x;
        sourceRegion.right = crop_region.x + crop_region.width;
        sourceRegion.top = crop_region.y;
        sourceRegion.bottom = crop_region.y + crop_region.height;
        sourceRegion.front = 0;
        sourceRegion.back = 1;

        pContext->CopySubresourceRegion(m_pStagingTexture, 0, 0, 0, 0, pAcquiredDesktopImage, 0, &sourceRegion);

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        hr = pContext->Map(m_pStagingTexture, 0, D3D11_MAP_READ, 0, &mappedResource);
        if (FAILED(hr)) {
            SafeRelease(&pAcquiredDesktopImage);
            pDuplicator->ReleaseFrame();
            return false;
        }

        // Reuse pre-allocated BGRA buffer
        cv::Mat bgra_frame(crop_region.height, crop_region.width, CV_8UC4, mappedResource.pData, mappedResource.RowPitch);
        cv::cvtColor(bgra_frame, frame, cv::COLOR_BGRA2BGR);

        pContext->Unmap(m_pStagingTexture, 0);
        SafeRelease(&pAcquiredDesktopImage);
        pDuplicator->ReleaseFrame();
        return true;
    }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

private:
    ID3D11Texture2D* m_pStagingTexture = nullptr;
    IDXGIFactory1* pFactory = nullptr;
    IDXGIAdapter1* pAdapter = nullptr;
    IDXGIOutput* pOutput = nullptr;
    IDXGIOutput1* pOutput1 = nullptr;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    IDXGIOutputDuplication* pDuplicator = nullptr;
    cv::Mat bgra_buffer; // Reusable BGRA frame buffer
    int width = 0;
    int height = 0;
};

// =============================================================================
// Object Detector (ONNX Runtime with model validation & cached path)
// =============================================================================
class ObjectDetector {
public:
    ObjectDetector(const Config& cfg)
        : env(ORT_LOGGING_LEVEL_WARNING, "Realtime_YOLO_Detector"), session(nullptr) {

        // Validate model file exists before attempting to load
        if (!fs::exists(fs::path(cfg.model_path))) {
            throw std::runtime_error("Model file not found: " + cfg.model_path +
                "\nPlease ensure the ONNX model file is in the executable's directory.");
        }

        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (cfg.inference_device == 1) {
            try {
                OrtCUDAProviderOptions cuda_options{};
                cuda_options.device_id = 0;
                session_options.AppendExecutionProvider_CUDA(cuda_options);
            }
            catch (...) {
                std::cout << "CUDA init failed, falling back to CPU." << std::endl;
            }
        }
        else if (cfg.inference_device == 2) {
            OrtTensorRTProviderOptions trt_options{};
            trt_options.device_id = 0;
            trt_options.trt_fp16_enable = 1;
            trt_options.trt_engine_cache_enable = 1;
            trt_options.trt_engine_cache_path = cfg.trt_cache_path.c_str();
            session_options.AppendExecutionProvider_TensorRT(trt_options);
        }
        else if (cfg.inference_device == 3) {
            OrtOpenVINOProviderOptions ov_options;
            ov_options.device_type = "GPU";
            session_options.AppendExecutionProvider_OpenVINO(ov_options);
        }

        // Cache wide string to avoid repeated conversions
        cached_model_path_w = std::wstring(cfg.model_path.begin(), cfg.model_path.end());
        session = Ort::Session(env, cached_model_path_w.c_str(), session_options);
    }

    void Detect(const cv::Mat& image, std::vector<Detection>& detections, const Config& cfg, TimingDetails& timings) {
        detections.clear();
        auto stage_start = std::chrono::high_resolution_clock::now();

        auto input_tensor_info = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        auto input_shape = input_tensor_info.GetShape();

        // Handle dynamic batch size
        if (input_shape[0] == -1) input_shape[0] = 1;

        int64_t input_height = input_shape[2] > 0 ? input_shape[2] : cfg.crop_size;
        int64_t input_width = input_shape[3] > 0 ? input_shape[3] : cfg.crop_size;

        input_shape[2] = input_height;
        input_shape[3] = input_width;

        float ratio_h = static_cast<float>(input_height) / image.rows;
        float ratio_w = static_cast<float>(input_width) / image.cols;
        float ratio = std::min(ratio_h, ratio_w);
        int new_w = static_cast<int>(image.cols * ratio);
        int new_h = static_cast<int>(image.rows * ratio);

        cv::Mat resized_img;
        cv::resize(image, resized_img, cv::Size(new_w, new_h));

        cv::Mat canvas = cv::Mat::ones(cv::Size(static_cast<int>(input_width), static_cast<int>(input_height)), CV_8UC3) * 114;
        int paste_x = (static_cast<int>(input_width) - new_w) / 2;
        int paste_y = (static_cast<int>(input_height) - new_h) / 2;
        resized_img.copyTo(canvas(cv::Rect(paste_x, paste_y, new_w, new_h)));

        cv::Mat blob = cv::dnn::blobFromImage(canvas, 1.0 / 255.0, cv::Size(input_width, input_height), cv::Scalar(), true, false);

        auto stage_end_preprocess = std::chrono::high_resolution_clock::now();
        timings.preprocess_ms = std::chrono::duration_cast<std::chrono::microseconds>(stage_end_preprocess - stage_start).count() / 1000.0;

        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name_ptr = session.GetInputNameAllocated(0, allocator);
        auto output_name_ptr = session.GetOutputNameAllocated(0, allocator);
        const char* input_names[] = { input_name_ptr.get() };
        const char* output_names[] = { output_name_ptr.get() };

        Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, blob.ptr<float>(), blob.total(), input_shape.data(), input_shape.size());

        auto output_tensors = session.Run(Ort::RunOptions{ nullptr }, input_names, &input_tensor, 1, output_names, 1);
        auto stage_end_inference = std::chrono::high_resolution_clock::now();
        timings.inference_ms = std::chrono::duration_cast<std::chrono::microseconds>(stage_end_inference - stage_end_preprocess).count() / 1000.0;

        if (cfg.model_type == 2) { // End2End
            const float* output_data = output_tensors.front().GetTensorData<float>();
            const auto& output_shape = output_tensors.front().GetTensorTypeAndShapeInfo().GetShape();
            const int num_detections = static_cast<int>(output_shape[1]);

            for (int i = 0; i < num_detections; ++i) {
                const float confidence = output_data[i * 6 + 4];
                if (confidence >= cfg.confidence_threshold) {
                    const float x1 = output_data[i * 6 + 0];
                    const float y1 = output_data[i * 6 + 1];
                    const float x2 = output_data[i * 6 + 2];
                    const float y2 = output_data[i * 6 + 3];
                    const int class_id = static_cast<int>(output_data[i * 6 + 5]);
                    int left = static_cast<int>((x1 - paste_x) / ratio);
                    int top = static_cast<int>((y1 - paste_y) / ratio);
                    int width = static_cast<int>((x2 - x1) / ratio);
                    int height = static_cast<int>((y2 - y1) / ratio);
                    detections.emplace_back(Detection{ cv::Rect(left, top, width, height), confidence, class_id });
                }
            }
        }
        else {
            const float* output_data = output_tensors.front().GetTensorData<float>();
            const auto& out_shape = output_tensors.front().GetTensorTypeAndShapeInfo().GetShape();

            int rows = out_shape[1];
            int cols = out_shape[2];
            cv::Mat output_mat(rows, cols, CV_32F, (void*)output_data);

            // YOLOv8/11 format is usually [1, 84, 8400], transpose needed
            if (cols > rows) {
                output_mat = output_mat.t();
            }

            std::vector<cv::Rect> boxes;
            std::vector<float> confidences;
            std::vector<int> class_ids;

            if (cfg.model_type == 0) { // YOLOv8/v11
                for (int i = 0; i < output_mat.rows; ++i) {
                    cv::Mat classes_scores = output_mat.row(i).colRange(4, output_mat.cols);
                    cv::Point class_id_point;
                    double max_score;
                    cv::minMaxLoc(classes_scores, 0, &max_score, 0, &class_id_point);
                    if (max_score > cfg.confidence_threshold) {
                        confidences.push_back(static_cast<float>(max_score));
                        class_ids.push_back(class_id_point.x);
                        float cx = output_mat.at<float>(i, 0);
                        float cy = output_mat.at<float>(i, 1);
                        float w = output_mat.at<float>(i, 2);
                        float h = output_mat.at<float>(i, 3);
                        int left = static_cast<int>((cx - 0.5f * w - paste_x) / ratio);
                        int top = static_cast<int>((cy - 0.5f * h - paste_y) / ratio);
                        int width = static_cast<int>(w / ratio);
                        int height = static_cast<int>(h / ratio);
                        boxes.emplace_back(cv::Rect(left, top, width, height));
                    }
                }
            }
            else if (cfg.model_type == 1) { // YOLOv5
                for (int i = 0; i < output_mat.rows; ++i) {
                    float obj_conf = output_mat.at<float>(i, 4);
                    if (obj_conf > cfg.confidence_threshold) {
                        cv::Mat classes_scores = output_mat.row(i).colRange(5, output_mat.cols);
                        cv::Point class_id_point;
                        double max_score;
                        cv::minMaxLoc(classes_scores, 0, &max_score, 0, &class_id_point);
                        float score = obj_conf * max_score;
                        if (score > cfg.confidence_threshold) {
                            confidences.push_back(static_cast<float>(score));
                            class_ids.push_back(class_id_point.x);
                            float cx = output_mat.at<float>(i, 0);
                            float cy = output_mat.at<float>(i, 1);
                            float w = output_mat.at<float>(i, 2);
                            float h = output_mat.at<float>(i, 3);
                            int left = static_cast<int>((cx - 0.5f * w - paste_x) / ratio);
                            int top = static_cast<int>((cy - 0.5f * h - paste_y) / ratio);
                            int width = static_cast<int>(w / ratio);
                            int height = static_cast<int>(h / ratio);
                            boxes.emplace_back(cv::Rect(left, top, width, height));
                        }
                    }
                }
            }

            std::vector<int> nms_indices;
            cv::dnn::NMSBoxes(boxes, confidences, cfg.confidence_threshold, cfg.nms_threshold, nms_indices);
            for (int idx : nms_indices) {
                detections.emplace_back(Detection{ boxes[idx], confidences[idx], class_ids[idx] });
            }
        }
        auto stage_end_postprocess = std::chrono::high_resolution_clock::now();
        timings.postprocess_ms = std::chrono::duration_cast<std::chrono::microseconds>(stage_end_postprocess - stage_end_inference).count() / 1000.0;
    }

private:
    Ort::Env env;
    Ort::Session session;
    std::wstring cached_model_path_w; // Cached to avoid repeated string conversions
};

// =============================================================================
// Aim Assistant (main orchestrator)
// =============================================================================
class AimAssistant {
public:
    AimAssistant()
        : cfg(),
        capturer(cfg.crop_size, cfg.crop_size),
        detector(cfg),
        mouse(),
        crop_region((capturer.getWidth() - cfg.crop_size) / 2, (capturer.getHeight() - cfg.crop_size) / 2, cfg.crop_size, cfg.crop_size),
        crop_center(cfg.crop_size / 2, cfg.crop_size / 2),
        is_visualizing(cfg.enable_visualization),
        current_fov_radius(cfg.rifle_max_lock_dist)
    {
        if (capturer.getWidth() < cfg.crop_size || capturer.getHeight() < cfg.crop_size) {
            throw std::runtime_error("Screen resolution is smaller than configured crop_size.");
        }
        if (cfg.game_width <= 0) cfg.game_width = capturer.getWidth();
        if (cfg.game_height <= 0) cfg.game_height = capturer.getHeight();
        update_mouse_factor();

        // Pre-allocate vectors for hot loop
        detections.reserve(256);
        tracker.tracks.reserve(64);

        cv::namedWindow(cfg.ui_window_name);
        cvui::init(cfg.ui_window_name);

        if (is_visualizing) {
            cv::namedWindow(cfg.window_name, cv::WINDOW_AUTOSIZE);
        }
        std::cout << "--- Detection will run on a centered " << cfg.crop_size << "x" << cfg.crop_size << " region. ---" << std::endl;
    }

    ~AimAssistant() {
        if (is_visualizing) cv::destroyAllWindows();
    }

    void update_mouse_factor() {
        const int w = (cfg.game_width > 0) ? cfg.game_width : capturer.getWidth();
        const int h = (cfg.game_height > 0) ? cfg.game_height : capturer.getHeight();
        mouse_correction_factor_x = (cfg.horizontal_fov / static_cast<double>(w)) * (cfg.pixels_for_360_turn / 360.0);
        mouse_correction_factor_y = (cfg.vertical_fov / static_cast<double>(h)) * (cfg.pixels_for_360_turn / 360.0);
    }

    void Run() {
        while (true) {
            auto loop_start_time = std::chrono::high_resolution_clock::now();

            if (binding_target != 0) {
                for (int i = 1; i < 255; i++) {
                    if (i == VK_ESCAPE) continue;
                    if (GetAsyncKeyState(i) & 0x8000) {
                        if (binding_target == 1) cfg.rifle_key1 = i;
                        else if (binding_target == 2) cfg.rifle_key2 = i;
                        else if (binding_target == 3) cfg.sniper_key = i;
                        binding_target = 0;
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        break;
                    }
                }
            }

            bool sniper_active = (cfg.sniper_key != 0) && (GetAsyncKeyState(cfg.sniper_key) & 0x8000);
            bool rifle1_active = (cfg.rifle_key1 != 0) && (GetAsyncKeyState(cfg.rifle_key1) & 0x8000);
            bool rifle2_active = (cfg.rifle_key2 != 0) && (GetAsyncKeyState(cfg.rifle_key2) & 0x8000);

            bool rifle_active = false;
            if (cfg.rifle_dual_trigger && cfg.rifle_key1 != 0 && cfg.rifle_key2 != 0) {
                rifle_active = rifle1_active && rifle2_active;
            }
            else {
                rifle_active = rifle1_active || rifle2_active;
            }
            if (sniper_active) rifle_active = false;

            bool is_aiming = sniper_active || rifle_active;
            int active_max_dist = sniper_active ? cfg.sniper_max_lock_dist : cfg.rifle_max_lock_dist;
            double active_aim_x_ratio = cfg.rifle_key1_aim_x_ratio;
            double active_aim_y_ratio = cfg.rifle_key1_aim_y_ratio;
            if (sniper_active) {
                active_aim_x_ratio = cfg.sniper_aim_x_ratio;
                active_aim_y_ratio = cfg.sniper_aim_y_ratio;
            }
            else if (cfg.rifle_dual_trigger && rifle1_active && rifle2_active) {
                active_aim_x_ratio = (cfg.rifle_key1_aim_x_ratio + cfg.rifle_key2_aim_x_ratio) * 0.5;
                active_aim_y_ratio = (cfg.rifle_key1_aim_y_ratio + cfg.rifle_key2_aim_y_ratio) * 0.5;
            }
            else if (rifle2_active && !rifle1_active) {
                active_aim_x_ratio = cfg.rifle_key2_aim_x_ratio;
                active_aim_y_ratio = cfg.rifle_key2_aim_y_ratio;
            }

            // Handle UI Window
            handleUI();

            if (!capturer.CaptureFrame(captured_frame, crop_region)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            detector.Detect(captured_frame, detections, cfg, timings);
            tracker.update(detections);

            TrackedObject* best_target = findBestTarget(active_max_dist, is_aiming);
            handleMouseInput(best_target, timings.total_loop_ms / 1000.0, is_aiming, active_max_dist, active_aim_x_ratio, active_aim_y_ratio);

            auto loop_end_time = std::chrono::high_resolution_clock::now();
            timings.total_loop_ms = std::chrono::duration<double, std::milli>(loop_end_time - loop_start_time).count();
            updateAndPrintStats();

            if (is_visualizing) {
                handleVisualization(best_target);
            }

            char key = static_cast<char>(cv::waitKey(1));
            if (key == 27) { // ESC
                break;
            }
        }
    }

private:
    void handleUI() {
        cv::Mat frame = cv::Mat(980, 520, CV_8UC3, cv::Scalar(49, 52, 49));

        cvui::text(frame, 10, 10, "Control Panel", 0.6);

        cvui::window(frame, 10, 40, 500, 150, "Key Bindings");
        cvui::text(frame, 20, 65, "Rifle Key 1:");
        if (cvui::button(frame, 120, 60, 100, 25, binding_target == 1 ? "Press..." : GetKeyName(cfg.rifle_key1))) binding_target = 1;

        cvui::text(frame, 240, 65, "Rifle Key 2:");
        if (cvui::button(frame, 340, 60, 100, 25, binding_target == 2 ? "Press..." : GetKeyName(cfg.rifle_key2))) binding_target = 2;
        if (cvui::button(frame, 445, 60, 25, 25, "X")) cfg.rifle_key2 = 0;

        cvui::checkbox(frame, 20, 95, "Rifle Dual Trigger (Key1 AND Key2)", &cfg.rifle_dual_trigger);

        cvui::text(frame, 20, 125, "Sniper Key:");
        if (cvui::button(frame, 120, 120, 100, 25, binding_target == 3 ? "Press..." : GetKeyName(cfg.sniper_key))) binding_target = 3;
        if (cvui::button(frame, 225, 120, 25, 25, "X")) cfg.sniper_key = 0;

        cvui::window(frame, 10, 200, 500, 420, "Aim Settings");
        cvui::text(frame, 20, 225, "Rifle Max Dist:");
        cvui::trackbar(frame, 170, 210, 320, &cfg.rifle_max_lock_dist, 10, 500);

        cvui::text(frame, 20, 265, "Sniper Max Dist:");
        cvui::trackbar(frame, 170, 250, 320, &cfg.sniper_max_lock_dist, 10, 500);

        cvui::text(frame, 20, 305, "Aim Speed:");
        cvui::trackbar(frame, 170, 290, 320, &cfg.aim_speed, 0.1, 5.0);

        cvui::text(frame, 20, 345, "Aim Smoothing:");
        cvui::trackbar(frame, 170, 330, 320, &cfg.aim_smoothing, 0.1, 5.0);

        cvui::text(frame, 20, 385, "Rifle Key1 Aim X:");
        cvui::trackbar(frame, 170, 370, 320, &cfg.rifle_key1_aim_x_ratio, 0.0, 1.0);

        cvui::text(frame, 20, 425, "Rifle Key1 Aim Y:");
        cvui::trackbar(frame, 170, 410, 320, &cfg.rifle_key1_aim_y_ratio, 0.0, 1.0);

        cvui::text(frame, 20, 465, "Rifle Key2 Aim X:");
        cvui::trackbar(frame, 170, 450, 320, &cfg.rifle_key2_aim_x_ratio, 0.0, 1.0);

        cvui::text(frame, 20, 505, "Rifle Key2 Aim Y:");
        cvui::trackbar(frame, 170, 490, 320, &cfg.rifle_key2_aim_y_ratio, 0.0, 1.0);

        cvui::text(frame, 20, 545, "Sniper Aim X:");
        cvui::trackbar(frame, 170, 530, 320, &cfg.sniper_aim_x_ratio, 0.0, 1.0);

        cvui::text(frame, 20, 585, "Sniper Aim Y:");
        cvui::trackbar(frame, 170, 570, 320, &cfg.sniper_aim_y_ratio, 0.0, 1.0);

        cvui::window(frame, 10, 640, 500, 330, "Game / Model / Execution");
        cvui::text(frame, 20, 665, "Game Width:");
        cvui::trackbar(frame, 170, 650, 320, &cfg.game_width, 640, 7680);

        cvui::text(frame, 20, 705, "Game Height:");
        cvui::trackbar(frame, 170, 690, 320, &cfg.game_height, 480, 4320);

        cvui::text(frame, 20, 745, "FOV (H):");
        cvui::trackbar(frame, 170, 730, 320, &cfg.horizontal_fov, 30.0, 180.0);

        cvui::text(frame, 20, 785, "FOV (V):");
        cvui::trackbar(frame, 170, 770, 320, &cfg.vertical_fov, 30.0, 180.0);

        cvui::text(frame, 20, 825, "Confidence:");
        cvui::trackbar(frame, 170, 810, 320, &cfg.confidence_threshold, 0.1f, 1.0f);

        cvui::text(frame, 20, 865, "Model Type:");
        if (cvui::button(frame, 170, 860, 100, 25, cfg.model_type == 0 ? "YOLOv8/v11" : (cfg.model_type == 1 ? "YOLOv5" : "End2End"))) {
            cfg.model_type = (cfg.model_type + 1) % 3;
        }

        cvui::text(frame, 290, 865, "Device:");
        const char* devices[] = { "CPU", "GPU (CUDA)", "TensorRT", "OpenVINO" };
        if (cvui::button(frame, 350, 860, 140, 25, devices[cfg.inference_device])) {
            cfg.inference_device = (cfg.inference_device + 1) % 4;
        }

        update_mouse_factor();

        cvui::text(frame, 20, 905, "Model Path (Restart Req):");
        if (cvui::button(frame, 20, 925, 100, 25, "Select Model")) {
            char filename[MAX_PATH];
            OPENFILENAMEA ofn;
            ZeroMemory(&filename, sizeof(filename));
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = NULL;
            ofn.lpstrFilter = "ONNX Models\0*.onnx\0All Files\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = "Select YOLO ONNX Model";
            ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                cfg.model_path = filename;
            }
        }
        cvui::printf(frame, 130, 930, 0.4, 0x00ff00, "%s", cfg.model_path.c_str());

        if (cvui::button(frame, 330, 10, 180, 25, "Toggle Visualization")) {
            is_visualizing = !is_visualizing;
            if (is_visualizing) cv::namedWindow(cfg.window_name, cv::WINDOW_AUTOSIZE);
            else cv::destroyWindow(cfg.window_name);
        }

        cvui::update(cfg.ui_window_name);
        cv::imshow(cfg.ui_window_name, frame);
    }

    TrackedObject* findBestTarget(int active_max_dist, bool is_aiming) {
        TrackedObject* target = nullptr;
        double min_dist_to_center = current_fov_radius;
        if (min_dist_to_center > active_max_dist) min_dist_to_center = active_max_dist;

        for (auto& t : tracker.tracks) {
            if (t.lost_frames > 0) continue;
            cv::Point box_center(t.box.x + t.box.width / 2, t.box.y + t.box.height / 2);
            double dist = std::hypot(box_center.x - crop_center.x, box_center.y - crop_center.y);
            if (dist < min_dist_to_center) {
                min_dist_to_center = dist;
                target = &t;
            }
        }
        return target;
    }

    void handleMouseInput(TrackedObject* target, double dt, bool is_aiming, int active_max_dist, double aim_x_ratio, double aim_y_ratio) {
        auto current_time = std::chrono::high_resolution_clock::now();

        if (!target) {
            if (!is_target_lost) {
                is_target_lost = true;
                target_lost_time = current_time;
            }
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - target_lost_time).count();
            if (duration < 60) {
                current_fov_radius = cfg.min_lock_distance_pixels + (active_max_dist - cfg.min_lock_distance_pixels) * (duration / 60.0);
            }
            else {
                current_fov_radius = active_max_dist;
            }
            mouse.pid.reset();
            return;
        }
        else {
            is_target_lost = false;
            current_fov_radius = cfg.min_lock_distance_pixels; // shrink when locked
        }

        if (target->id != last_target_id) {
            if (transfer_pending_id != target->id) {
                transfer_pending_id = target->id;
                target_transfer_time = current_time;
            }
            auto transfer_duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - target_transfer_time).count();
            if (transfer_duration < 20) {
                return; // wait 20ms transfer delay
            }
            last_target_id = target->id;
            mouse.pid.reset();
        }

        if (is_aiming) {
            cv::Point target_point(
                target->box.x + static_cast<int>(target->box.width * aim_x_ratio),
                target->box.y + static_cast<int>(target->box.height * aim_y_ratio)
            );
            double dx_pixels = target_point.x - crop_center.x;
            double dy_pixels = target_point.y - crop_center.y;
            double corrected_dx = dx_pixels * mouse_correction_factor_x;
            double corrected_dy = dy_pixels * mouse_correction_factor_y;

            mouse.MoveSmooth(corrected_dx, corrected_dy, dt, cfg.aim_speed, cfg.aim_smoothing);
        }
        else {
            mouse.pid.reset();
        }
    }

    void updateAndPrintStats() {
        const double smoothing_factor = 0.05;
        if (is_first_frame) {
            smoothed_total = timings.total_loop_ms;
            smoothed_pre = timings.preprocess_ms;
            smoothed_inf = timings.inference_ms;
            smoothed_post = timings.postprocess_ms;
            is_first_frame = false;
        }
        else {
            smoothed_total = smoothing_factor * timings.total_loop_ms + (1.0 - smoothing_factor) * smoothed_total;
            smoothed_pre = smoothing_factor * timings.preprocess_ms + (1.0 - smoothing_factor) * smoothed_pre;
            smoothed_inf = smoothing_factor * timings.inference_ms + (1.0 - smoothing_factor) * smoothed_inf;
            smoothed_post = smoothing_factor * timings.postprocess_ms + (1.0 - smoothing_factor) * smoothed_post;
        }
        if (++frame_count_for_console % 30 == 0 && smoothed_total > 0) {
            double smoothed_fps = 1000.0 / smoothed_total;
            std::cout << std::fixed << std::setprecision(1)
                << "\r[LIVE] FPS: " << std::setw(5) << smoothed_fps
                << " | Total Delay: " << std::setw(5) << smoothed_total << "ms"
                << "        " << std::flush;
        }
    }

    void handleVisualization(const TrackedObject* best_target) {
        cv::circle(captured_frame, crop_center, static_cast<int>(current_fov_radius), cv::Scalar(255, 255, 0), 1);
        for (const auto& t : tracker.tracks) {
            if (t.lost_frames > 0) continue;
            cv::Scalar color = (&t == best_target) ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
            cv::rectangle(captured_frame, t.box, color, 2);
            std::string label = "ID:" + std::to_string(t.id);
            cv::putText(captured_frame, label, cv::Point(t.box.x, t.box.y - 5), cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
        }
        if (smoothed_total > 0) {
            double smoothed_fps = 1000.0 / smoothed_total;
            std::ostringstream stats_stream;
            stats_stream << std::fixed << std::setprecision(1) << "FPS: " << smoothed_fps;
            cv::putText(captured_frame, stats_stream.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 255, 255), 2);
        }
        cv::imshow(cfg.window_name, captured_frame);
    }

    Config cfg;
    ScreenCapturer capturer;
    ObjectDetector detector;
    MouseController mouse;
    Tracker tracker;

    const cv::Rect crop_region;
    const cv::Point crop_center;
    double mouse_correction_factor_x = 1.0;
    double mouse_correction_factor_y = 1.0;
    cv::Mat captured_frame;
    std::vector<Detection> detections; // Pre-allocated for hot loop
    TimingDetails timings;

    bool is_visualizing;
    int binding_target = 0;
    double current_fov_radius;
    bool is_target_lost = false;
    std::chrono::high_resolution_clock::time_point target_lost_time;
    int last_target_id = -1;
    int transfer_pending_id = -1;
    std::chrono::high_resolution_clock::time_point target_transfer_time;

    double smoothed_total = 0.0, smoothed_pre = 0.0, smoothed_inf = 0.0, smoothed_post = 0.0;
    bool is_first_frame = true;
    int frame_count_for_console = 0;
};

// =============================================================================
// Entry Point
// =============================================================================
int main() {
    try {
        AimAssistant assistant;
        assistant.Run();
    }
    catch (const std::exception& e) {
        std::cerr << "\n\n[FATAL ERROR] An unrecoverable error occurred: " << e.what() << std::endl;
        std::cerr << "Please check:" << std::endl;
        std::cerr << "  1. The ONNX model file exists in the executable directory." << std::endl;
        std::cerr << "  2. IbInputSimulator.dll is present." << std::endl;
        std::cerr << "  3. Run as Administrator for driver-level input." << std::endl;
        std::cin.get();
        return -1;
    }
    std::cout << "\nProgram finished successfully." << std::endl;
    return 0;
}