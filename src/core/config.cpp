#include "config.hpp"
#include <iostream>
#include <algorithm>
#include <cstdlib>

namespace auto_aim {

void Config::validate() {
    confidence_threshold = std::clamp(confidence_threshold, 0.0f, 1.0f);
    nms_threshold = std::clamp(nms_threshold, 0.0f, 1.0f);
    
    rifle_key1_aim_x_ratio = std::clamp(rifle_key1_aim_x_ratio, 0.0, 1.0);
    rifle_key1_aim_y_ratio = std::clamp(rifle_key1_aim_y_ratio, 0.0, 1.0);
    rifle_key2_aim_x_ratio = std::clamp(rifle_key2_aim_x_ratio, 0.0, 1.0);
    rifle_key2_aim_y_ratio = std::clamp(rifle_key2_aim_y_ratio, 0.0, 1.0);
    sniper_aim_x_ratio = std::clamp(sniper_aim_x_ratio, 0.0, 1.0);
    sniper_aim_y_ratio = std::clamp(sniper_aim_y_ratio, 0.0, 1.0);
    
    if (crop_size <= 0) crop_size = 640;
    if (crop_size > 1920) crop_size = 1920;
    if (aim_speed <= 0) aim_speed = 1.0;
    if (aim_smoothing <= 0) aim_smoothing = 1.0;
    
    if (rifle_max_lock_dist < min_lock_distance_pixels) {
        rifle_max_lock_dist = min_lock_distance_pixels + 50;
    }
    if (sniper_max_lock_dist < min_lock_distance_pixels) {
        sniper_max_lock_dist = min_lock_distance_pixels + 50;
    }
}

void Config::applyCommandLineArgs(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--model" && i + 1 < argc) {
            model_path = argv[++i];
        }
        else if (arg == "--device" && i + 1 < argc) {
            int dev = std::stoi(argv[++i]);
            if (dev >= 0 && dev <= 3) {
                inference_device = static_cast<InferenceDevice>(dev);
            }
        }
        else if (arg == "--crop-size" && i + 1 < argc) {
            crop_size = std::stoi(argv[++i]);
        }
        else if (arg == "--confidence" && i + 1 < argc) {
            confidence_threshold = std::stof(argv[++i]);
        }
        else if (arg == "--no-vis") {
            enable_visualization = false;
        }
        else if (arg == "--help" || arg == "-h") {
            printUsage();
            std::exit(0);
        }
    }
}

void Config::printUsage() {
    std::cout << "AutoAim v1.0.0 - Real-time AI Aim Assistant\n\n";
    std::cout << "Usage: AutoAim [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --model <path>       Path to ONNX model file\n";
    std::cout << "  --device <0-3>       Inference device (0=CPU, 1=CUDA, 2=TensorRT, 3=OpenVINO)\n";
    std::cout << "  --crop-size <n>      Crop region size (default: 640)\n";
    std::cout << "  --confidence <f>     Detection confidence threshold (0.0-1.0)\n";
    std::cout << "  --no-vis             Disable visualization window\n";
    std::cout << "  --help, -h           Show this help message\n";
}

} // namespace auto_aim
