#pragma once
#include <vector>
#include <string>
#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>
#include <filesystem>
#include "types.hpp"
#include "config.hpp"

namespace auto_aim {

class ObjectDetector {
public:
    explicit ObjectDetector(const Config& cfg);
    ~ObjectDetector() = default;
    
    void detect(const cv::Mat& image, std::vector<Detection>& detections, 
                const Config& cfg, TimingDetails& timings);
    
    std::string modelPath() const { return model_path_; }
    bool isLoaded() const { return loaded_; }
    
    bool reloadModel(const std::string& new_path, const Config& cfg);

private:
    void parseYOLOv8Output(const float* data, int rows, int cols, 
                           std::vector<Detection>& dets, const Config& cfg,
                           int paste_x, int paste_y, float ratio);
    void parseYOLOv5Output(const float* data, int rows, int cols,
                           std::vector<Detection>& dets, const Config& cfg,
                           int paste_x, int paste_y, float ratio);
    void parseEnd2EndOutput(const float* data, int num_dets,
                            std::vector<Detection>& dets, const Config& cfg,
                            int paste_x, int paste_y, float ratio);
    
    void configureExecutionProvider(Ort::SessionOptions& opts, const Config& cfg);
    
    Ort::Env env_;
    Ort::Session session_{nullptr};
    std::wstring cached_model_path_w_;
    std::string model_path_;
    bool loaded_ = false;
};

} // namespace auto_aim
