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
#pragma comment(lib, "onnxruntime.lib")

// OpenCV
#include <opencv2/opencv.hpp>
#ifdef _DEBUG
#pragma comment(lib, "opencv_world4110d.lib")
#else
#pragma comment(lib, "opencv_world4110.lib")
#endif

// cvui
#define CVUI_IMPLEMENTATION
#include "cvui.h"

// Input Simulator
#include "InputSimulator.hpp"

// Hungarian
#include "hungarian.hpp"

using namespace std::chrono_literals;

// Forward Declarations
struct Detection;
struct TimingDetails;

template<class T>
void SafeRelease(T** ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

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

class Config {
public:
    std::string model_path = "apex_final.onnx";
    int model_type = 0; // 0: YOLOv8/v11, 1: YOLOv5, 2: End2End
    bool use_openvino = false;
    std::string trt_cache_path = ".\\engine_cache";
    int crop_size = 640;
    int max_lock_distance_pixels = 100;
    int min_lock_distance_pixels = 20; // dynamic FOV min
    float confidence_threshold = 0.5f;
    float nms_threshold = 0.4f;
    int smooth_aim_key = VK_LBUTTON;
    int single_shot_key = VK_F8;
    double aim_smoothing = 1.0;
    double target_y_ratio = 0.3;
    double sensitivity = 1.0;
    double pixels_for_360_turn = 16410;
    double horizontal_fov = 120.0;
    double vertical_fov = 68.0;
    bool enable_visualization = true;
    std::string window_name = "YOLO Real-time Detection";
    std::string ui_window_name = "Control Panel";
};

class FuzzyPID {
public:
    double kp_base, ki_base, kd_base;
    double integral_x = 0, integral_y = 0;
    double prev_err_x = 0, prev_err_y = 0;
    
    FuzzyPID(double p=0.6, double i=0.0, double d=0.1) : kp_base(p), ki_base(i), kd_base(d) {}
    
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

class MouseController {
public:
    FuzzyPID pid;
    MouseController() : pid(0.6, 0.0, 0.1) {
        std::cout << "--- Initializing Mouse Simulation Driver... ---" << std::endl;
        hMouseDll = LoadLibrary(L"IbInputSimulator.dll");
        if (!hMouseDll) {
            std::cout << "WARNING: Failed to load IbInputSimulator.dll. Mouse moves will be simulated via std::cout for testing." << std::endl;
            return;
        }

        IbSendInit_ptr = (pIbSendInit)GetProcAddress(hMouseDll, "IbSendInit");
        IbSendDestroy_ptr = (pIbSendDestroy)GetProcAddress(hMouseDll, "IbSendDestroy");
        IbSendMouseMove_ptr = (pIbSendMouseMove)GetProcAddress(hMouseDll, "IbSendMouseMove");

        if (IbSendInit_ptr) {
            Send::Error error = IbSendInit_ptr(Send::SendType::Razer, 0, nullptr);
            if (error != Send::Error::Success) {
                std::cout << "WARNING: Mouse driver init failed. Run as Admin." << std::endl;
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
            IbSendMouseMove_ptr(dx, dy, Send::MoveMode::Relative);
        } else {
            // fallback
        }
    }

    void MoveSmooth(double target_x, double target_y, double dt) {
        double out_x, out_y;
        pid.compute(target_x, target_y, dt, out_x, out_y);
        MoveRelative(static_cast<int>(out_x), static_cast<int>(out_y));
    }

private:
    typedef Send::Error(__stdcall* pIbSendInit)(Send::SendType, Send::InitFlags, void*);
    typedef void(__stdcall* pIbSendDestroy)();
    typedef bool(__stdcall* pIbSendMouseMove)(int, int, Send::MoveMode);

    HMODULE hMouseDll = nullptr;
    pIbSendInit IbSendInit_ptr = nullptr;
    pIbSendDestroy IbSendDestroy_ptr = nullptr;
    pIbSendMouseMove IbSendMouseMove_ptr = nullptr;
};

// --- Tracking ---
struct TrackedObject {
    int id;
    cv::KalmanFilter kf;
    cv::Rect box;
    int lost_frames = 0;
    bool is_active = true;
    
    TrackedObject(int id, const cv::Rect& b) : id(id), box(b) {
        kf.init(4, 2, 0);
        kf.transitionMatrix = (cv::Mat_<float>(4, 4) << 1,0,1,0, 0,1,0,1, 0,0,1,0, 0,0,0,1);
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
        box.x = p.at<float>(0) - box.width / 2.0f;
        box.y = p.at<float>(1) - box.height / 2.0f;
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
        } else {
            std::vector<std::vector<double>> distMatrix(tracks.size(), std::vector<double>(detections.size(), 0.0));
            for (size_t i = 0; i < tracks.size(); ++i) {
                cv::Point tc(tracks[i].box.x + tracks[i].box.width/2, tracks[i].box.y + tracks[i].box.height/2);
                for (size_t j = 0; j < detections.size(); ++j) {
                    cv::Point dc(detections[j].box.x + detections[j].box.width/2, detections[j].box.y + detections[j].box.height/2);
                    distMatrix[i][j] = std::hypot(tc.x - dc.x, tc.y - dc.y);
                }
            }
            
            std::vector<int> assignment;
            hungarian.Solve(distMatrix, assignment);
            
            std::vector<bool> matched_det(detections.size(), false);
            for (size_t i = 0; i < assignment.size(); ++i) {
                int j = assignment[i];
                if (j >= 0 && distMatrix[i][j] < 150.0) { // distance threshold
                    tracks[i].update(detections[j].box);
                    matched_det[j] = true;
                } else {
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
            return t.lost_frames > 30; // retain for 30 frames
        }), tracks.end());
    }
};

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

        D3D11_TEXTURE2D_DESC desc;
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
    int width = 0;
    int height = 0;
};

class ObjectDetector {
public:
    ObjectDetector(const Config& cfg)
        : env(ORT_LOGGING_LEVEL_WARNING, "Realtime_YOLO_Detector"), session(nullptr) {
        
        Ort::SessionOptions session_options;
        session_options.SetIntraOpNumThreads(1);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        if (cfg.use_openvino) {
            OrtOpenVINOProviderOptions ov_options;
            ov_options.device_type = "GPU"; // XMX unit
            session_options.AppendExecutionProvider_OpenVINO(ov_options);
        } else {
            OrtTensorRTProviderOptions trt_options{};
            trt_options.device_id = 0;
            trt_options.trt_fp16_enable = 1;
            trt_options.trt_engine_cache_enable = 1;
            trt_options.trt_engine_cache_path = cfg.trt_cache_path.c_str();
            session_options.AppendExecutionProvider_TensorRT(trt_options);
        }

        // Convert string to wstring
        std::wstring widestr = std::wstring(cfg.model_path.begin(), cfg.model_path.end());
        session = Ort::Session(env, widestr.c_str(), session_options);
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
            } else if (cfg.model_type == 1) { // YOLOv5
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
};

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
        current_fov_radius(cfg.max_lock_distance_pixels)
    {
        if (capturer.getWidth() < cfg.crop_size || capturer.getHeight() < cfg.crop_size) {
            throw std::runtime_error("Screen resolution is smaller than configured crop_size.");
        }
        update_mouse_factor();
        
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
        mouse_correction_factor_x = (cfg.horizontal_fov / static_cast<double>(capturer.getWidth())) * (cfg.pixels_for_360_turn / 360.0);
        mouse_correction_factor_y = (cfg.vertical_fov / static_cast<double>(capturer.getHeight())) * (cfg.pixels_for_360_turn / 360.0);
    }

    void Run() {
        while (true) {
            auto loop_start_time = std::chrono::high_resolution_clock::now();
            
            // Handle UI Window
            handleUI();
            
            if (!capturer.CaptureFrame(captured_frame, crop_region)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            
            detector.Detect(captured_frame, detections, cfg, timings);
            tracker.update(detections);
            
            TrackedObject* best_target = findBestTarget();
            handleMouseInput(best_target, timings.total_loop_ms / 1000.0);
            
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
        cv::Mat frame = cv::Mat(450, 400, CV_8UC3, cv::Scalar(49, 52, 49));
        
        cvui::text(frame, 10, 10, "Control Panel", 0.6);
        cvui::text(frame, 10, 40, "FOV (Horizontal):");
        cvui::trackbar(frame, 150, 25, 200, &cfg.horizontal_fov, 30.0, 180.0);
        
        cvui::text(frame, 10, 80, "FOV (Vertical):");
        cvui::trackbar(frame, 150, 65, 200, &cfg.vertical_fov, 30.0, 180.0);
        
        update_mouse_factor();
        
        cvui::text(frame, 10, 120, "Aim Smoothing:");
        cvui::trackbar(frame, 150, 105, 200, &cfg.aim_smoothing, 0.1, 5.0);
        
        cvui::text(frame, 10, 160, "Max Lock Dist:");
        cvui::trackbar(frame, 150, 145, 200, &cfg.max_lock_distance_pixels, 10, 300);

        cvui::text(frame, 10, 200, "Confidence:");
        cvui::trackbar(frame, 150, 185, 200, &cfg.confidence_threshold, 0.1f, 1.0f);
        
        if (cvui::button(frame, 10, 230, "Toggle Visualization")) {
            is_visualizing = !is_visualizing;
            if (is_visualizing) cv::namedWindow(cfg.window_name, cv::WINDOW_AUTOSIZE);
            else cv::destroyWindow(cfg.window_name);
        }
        
        cvui::text(frame, 10, 270, "Model Type:");
        if (cvui::button(frame, 100, 265, cfg.model_type == 0 ? "YOLOv8/v11" : (cfg.model_type == 1 ? "YOLOv5" : "End2End"))) {
            cfg.model_type = (cfg.model_type + 1) % 3;
        }
        
        cvui::text(frame, 10, 310, "Model Path (Restart Required):");
        if (cvui::button(frame, 10, 330, "Select Model")) {
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
        cvui::printf(frame, 130, 335, 0.4, 0x00ff00, "%s", cfg.model_path.c_str());
        
        cvui::text(frame, 10, 360, "Execution Provider:");
        if (cvui::button(frame, 150, 355, cfg.use_openvino ? "OpenVINO (Intel XMX)" : "TensorRT (NVIDIA)")) {
            cfg.use_openvino = !cfg.use_openvino;
        }
        
        cvui::update(cfg.ui_window_name);
        cv::imshow(cfg.ui_window_name, frame);
    }

    TrackedObject* findBestTarget() {
        TrackedObject* target = nullptr;
        double min_dist_to_center = current_fov_radius;
        for (auto& t : tracker.tracks) {
            if (t.lost_frames > 0) continue; // Only aim at visible targets or allow aiming at predicted? 
            // Allow aiming at predicted if lost_frames < 30
            cv::Point box_center(t.box.x + t.box.width / 2, t.box.y + t.box.height / 2);
            double dist = std::hypot(box_center.x - crop_center.x, box_center.y - crop_center.y);
            if (dist < min_dist_to_center) {
                min_dist_to_center = dist;
                target = &t;
            }
        }
        return target;
    }

    void handleMouseInput(TrackedObject* target, double dt) {
        auto current_time = std::chrono::high_resolution_clock::now();
        
        if (!target) {
            if (!is_target_lost) {
                is_target_lost = true;
                target_lost_time = current_time;
            }
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - target_lost_time).count();
            if (duration < 60) {
                current_fov_radius = cfg.min_lock_distance_pixels + (cfg.max_lock_distance_pixels - cfg.min_lock_distance_pixels) * (duration / 60.0);
            } else {
                current_fov_radius = cfg.max_lock_distance_pixels;
            }
            mouse.pid.reset();
            return;
        } else {
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

        if (GetAsyncKeyState(cfg.smooth_aim_key) & 0x8000) {
            cv::Point target_point(
                target->box.x + target->box.width / 2,
                target->box.y + static_cast<int>(target->box.height * cfg.target_y_ratio)
            );
            double dx_pixels = target_point.x - crop_center.x;
            double dy_pixels = target_point.y - crop_center.y;
            double corrected_dx = dx_pixels * mouse_correction_factor_x;
            double corrected_dy = dy_pixels * mouse_correction_factor_y;
            
            mouse.MoveSmooth(corrected_dx, corrected_dy, dt);
        } else {
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
    std::vector<Detection> detections;
    TimingDetails timings;
    
    bool is_visualizing;
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

int main() {
    try {
        AimAssistant assistant;
        assistant.Run();
    }
    catch (const std::exception& e) {
        std::cerr << "\n\n[FATAL ERROR] An unrecoverable error occurred: " << e.what() << std::endl;
        std::cin.get();
        return -1;
    }
    std::cout << "\nProgram finished successfully." << std::endl;
    return 0;
}
