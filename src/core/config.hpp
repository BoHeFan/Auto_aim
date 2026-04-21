#pragma once
#include <string>
#include <windows.h>
#include "types.hpp"

namespace auto_aim {

class Config {
public:
    // Model settings
    std::string model_path = "apex_final.onnx";
    ModelType model_type = ModelType::YOLOv8_v11;
    InferenceDevice inference_device = InferenceDevice::TensorRT;
    std::string trt_cache_path = ".\\engine_cache";
    
    // Capture settings
    int crop_size = 640;
    int game_width = 0;
    int game_height = 0;
    int monitor_index = 0;
    
    // Key bindings
    int rifle_key1 = VK_LBUTTON;
    int rifle_key2 = 0;
    bool rifle_dual_trigger = false;
    int sniper_key = VK_RBUTTON;
    
    // Aim settings
    int rifle_max_lock_dist = 100;
    int sniper_max_lock_dist = 200;
    int min_lock_distance_pixels = 20;
    
    // Detection settings
    float confidence_threshold = 0.65f;
    float nms_threshold = 0.4f;
    
    // Smoothing settings
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
    
    // UI settings
    bool enable_visualization = true;
    std::string window_name = "YOLO Real-time Detection";
    std::string ui_window_name = "Control Panel";
    
    // Validation
    void validate();
    
    // CLI
    void applyCommandLineArgs(int argc, char* argv[]);
    static void printUsage();
};

} // namespace auto_aim
