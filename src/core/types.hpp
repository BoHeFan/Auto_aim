#pragma once
#include <opencv2/opencv.hpp>

namespace auto_aim {

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

enum class InferenceDevice {
    CPU = 0,
    CUDA = 1,
    TensorRT = 2,
    OpenVINO = 3
};

enum class ModelType {
    YOLOv8_v11 = 0,
    YOLOv5 = 1,
    End2End = 2
};

} // namespace auto_aim
