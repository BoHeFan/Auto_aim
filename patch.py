import re

with open('/workspace/main.cpp', 'r', encoding='utf-8') as f:
    code = f.read()

# 1. Add GetKeyName before Config
get_key_name_code = """
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

class Config {"""
code = code.replace("class Config {", get_key_name_code)

# 2. Modify Config class
old_config_inner = """    std::string model_path = "apex_final.onnx";
    int model_type = 0; // 0: YOLOv8/v11, 1: YOLOv5, 2: End2End
    bool use_openvino = false;
    std::string trt_cache_path = ".\\\\engine_cache";
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
    std::string ui_window_name = "Control Panel";"""
    
new_config_inner = """    std::string model_path = "apex_final.onnx";
    int model_type = 0; // 0: YOLOv8/v11, 1: YOLOv5, 2: End2End
    int inference_device = 2; // 0: CPU, 1: GPU, 2: TensorRT, 3: OpenVINO
    std::string trt_cache_path = ".\\\\engine_cache";
    
    int crop_size = 640;
    
    int rifle_key1 = VK_LBUTTON;
    int rifle_key2 = 0;
    bool rifle_dual_trigger = false;
    int sniper_key = VK_RBUTTON;
    
    int rifle_max_lock_dist = 100;
    int sniper_max_lock_dist = 200;
    int min_lock_distance_pixels = 20;
    
    float confidence_threshold = 0.5f;
    float nms_threshold = 0.4f;
    
    double aim_speed = 1.0;
    double aim_smoothing = 1.0;
    double target_y_ratio = 0.3;
    double sensitivity = 1.0;
    double pixels_for_360_turn = 16410;
    double horizontal_fov = 120.0;
    double vertical_fov = 68.0;
    
    bool enable_visualization = true;
    std::string window_name = "YOLO Real-time Detection";
    std::string ui_window_name = "Control Panel";"""
code = code.replace(old_config_inner, new_config_inner)

# 3. MouseController MoveSmooth signature
code = code.replace(
    "void MoveSmooth(double target_x, double target_y, double dt) {",
    "void MoveSmooth(double target_x, double target_y, double dt, double aim_speed, double aim_smoothing) {"
)
code = code.replace(
    "MoveRelative(static_cast<int>(out_x), static_cast<int>(out_y));",
    "out_x = (out_x * aim_speed) / aim_smoothing;\\n        out_y = (out_y * aim_speed) / aim_smoothing;\\n        MoveRelative(static_cast<int>(out_x), static_cast<int>(out_y));"
)

# 4. ObjectDetector inference device logic
old_detector_init = """        if (cfg.use_openvino) {
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
        }"""
        
new_detector_init = """        if (cfg.inference_device == 1) {
            try {
                OrtCUDAProviderOptions cuda_options{};
                cuda_options.device_id = 0;
                session_options.AppendExecutionProvider_CUDA(cuda_options);
            } catch(...) {
                std::cout << "CUDA init failed, falling back to CPU." << std::endl;
            }
        } else if (cfg.inference_device == 2) {
            OrtTensorRTProviderOptions trt_options{};
            trt_options.device_id = 0;
            trt_options.trt_fp16_enable = 1;
            trt_options.trt_engine_cache_enable = 1;
            trt_options.trt_engine_cache_path = cfg.trt_cache_path.c_str();
            session_options.AppendExecutionProvider_TensorRT(trt_options);
        } else if (cfg.inference_device == 3) {
            OrtOpenVINOProviderOptions ov_options;
            ov_options.device_type = "GPU"; // XMX unit
            session_options.AppendExecutionProvider_OpenVINO(ov_options);
        }"""
code = code.replace(old_detector_init, new_detector_init)

# 5. AimAssistant Run loop
old_run_loop = """        while (true) {
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
            handleMouseInput(best_target, timings.total_loop_ms / 1000.0);"""
            
new_run_loop = """        while (true) {
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
            } else {
                rifle_active = rifle1_active || rifle2_active;
            }
            if (sniper_active) rifle_active = false;
            
            bool is_aiming = sniper_active || rifle_active;
            int active_max_dist = sniper_active ? cfg.sniper_max_lock_dist : cfg.rifle_max_lock_dist;

            // Handle UI Window
            handleUI();
            
            if (!capturer.CaptureFrame(captured_frame, crop_region)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            
            detector.Detect(captured_frame, detections, cfg, timings);
            tracker.update(detections);
            
            TrackedObject* best_target = findBestTarget(active_max_dist, is_aiming);
            handleMouseInput(best_target, timings.total_loop_ms / 1000.0, is_aiming, active_max_dist);"""
code = code.replace(old_run_loop, new_run_loop)

# 6. AimAssistant constructor current_fov_radius
code = code.replace(
    "current_fov_radius(cfg.max_lock_distance_pixels)",
    "current_fov_radius(cfg.rifle_max_lock_dist)"
)

# 7. AimAssistant handleUI
handle_ui_start = code.find("void handleUI() {")
handle_ui_end = code.find("TrackedObject* findBestTarget(")
old_handle_ui = code[handle_ui_start:handle_ui_end]

new_handle_ui = """void handleUI() {
        cv::Mat frame = cv::Mat(750, 500, CV_8UC3, cv::Scalar(49, 52, 49));
        
        cvui::text(frame, 10, 10, "Control Panel", 0.6);
        
        cvui::window(frame, 10, 40, 480, 120, "Key Bindings");
        cvui::text(frame, 20, 65, "Rifle Key 1:");
        if (cvui::button(frame, 120, 60, 100, 25, binding_target == 1 ? "Press..." : GetKeyName(cfg.rifle_key1))) binding_target = 1;
        
        cvui::text(frame, 240, 65, "Rifle Key 2:");
        if (cvui::button(frame, 340, 60, 100, 25, binding_target == 2 ? "Press..." : GetKeyName(cfg.rifle_key2))) binding_target = 2;
        if (cvui::button(frame, 445, 60, 25, 25, "X")) cfg.rifle_key2 = 0;
        
        cvui::checkbox(frame, 20, 95, "Rifle Dual Trigger (Key1 AND Key2)", &cfg.rifle_dual_trigger);
        
        cvui::text(frame, 20, 125, "Sniper Key:");
        if (cvui::button(frame, 120, 120, 100, 25, binding_target == 3 ? "Press..." : GetKeyName(cfg.sniper_key))) binding_target = 3;
        if (cvui::button(frame, 225, 120, 25, 25, "X")) cfg.sniper_key = 0;
        
        cvui::window(frame, 10, 170, 480, 280, "Aim Settings");
        cvui::text(frame, 20, 195, "Rifle Max Dist:");
        cvui::trackbar(frame, 150, 180, 300, &cfg.rifle_max_lock_dist, 10, 500);
        
        cvui::text(frame, 20, 235, "Sniper Max Dist:");
        cvui::trackbar(frame, 150, 220, 300, &cfg.sniper_max_lock_dist, 10, 500);
        
        cvui::text(frame, 20, 275, "Aim Speed:");
        cvui::trackbar(frame, 150, 260, 300, &cfg.aim_speed, 0.1, 5.0);
        
        cvui::text(frame, 20, 315, "Aim Smoothing:");
        cvui::trackbar(frame, 150, 300, 300, &cfg.aim_smoothing, 0.1, 5.0);
        
        cvui::text(frame, 20, 355, "FOV (H):");
        cvui::trackbar(frame, 150, 340, 300, &cfg.horizontal_fov, 30.0, 180.0);
        
        cvui::text(frame, 20, 395, "FOV (V):");
        cvui::trackbar(frame, 150, 380, 300, &cfg.vertical_fov, 30.0, 180.0);
        
        update_mouse_factor();
        
        cvui::window(frame, 10, 460, 480, 220, "Model & Execution");
        cvui::text(frame, 20, 485, "Confidence:");
        cvui::trackbar(frame, 150, 470, 300, &cfg.confidence_threshold, 0.1f, 1.0f);
        
        cvui::text(frame, 20, 520, "Model Type:");
        if (cvui::button(frame, 150, 515, 100, 25, cfg.model_type == 0 ? "YOLOv8/v11" : (cfg.model_type == 1 ? "YOLOv5" : "End2End"))) {
            cfg.model_type = (cfg.model_type + 1) % 3;
        }
        
        cvui::text(frame, 20, 560, "Device:");
        const char* devices[] = {"CPU", "GPU (CUDA)", "TensorRT", "OpenVINO"};
        if (cvui::button(frame, 150, 555, 120, 25, devices[cfg.inference_device])) {
            cfg.inference_device = (cfg.inference_device + 1) % 4;
        }
        
        cvui::text(frame, 20, 600, "Model Path (Restart Req):");
        if (cvui::button(frame, 20, 620, 100, 25, "Select Model")) {
            char filename[MAX_PATH];
            OPENFILENAMEA ofn;
            ZeroMemory(&filename, sizeof(filename));
            ZeroMemory(&ofn, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = NULL;
            ofn.lpstrFilter = "ONNX Models\\0*.onnx\\0All Files\\0*.*\\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrTitle = "Select YOLO ONNX Model";
            ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                cfg.model_path = filename;
            }
        }
        cvui::printf(frame, 130, 625, 0.4, 0x00ff00, "%s", cfg.model_path.c_str());
        
        if (cvui::button(frame, 10, 690, 150, 30, "Toggle Visualization")) {
            is_visualizing = !is_visualizing;
            if (is_visualizing) cv::namedWindow(cfg.window_name, cv::WINDOW_AUTOSIZE);
            else cv::destroyWindow(cfg.window_name);
        }
        
        cvui::update(cfg.ui_window_name);
        cv::imshow(cfg.ui_window_name, frame);
    }

    """
code = code.replace(old_handle_ui, new_handle_ui)

# 8. AimAssistant findBestTarget
old_find_best = """TrackedObject* findBestTarget() {
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
    }"""
    
new_find_best = """TrackedObject* findBestTarget(int active_max_dist, bool is_aiming) {
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
    }"""
code = code.replace(old_find_best, new_find_best)

# 9. AimAssistant handleMouseInput
old_handle_mouse = """void handleMouseInput(TrackedObject* target, double dt) {
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
    }"""
    
new_handle_mouse = """void handleMouseInput(TrackedObject* target, double dt, bool is_aiming, int active_max_dist) {
        auto current_time = std::chrono::high_resolution_clock::now();

        if (!target) {
            if (!is_target_lost) {
                is_target_lost = true;
                target_lost_time = current_time;
            }
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - target_lost_time).count();
            if (duration < 60) {
                current_fov_radius = cfg.min_lock_distance_pixels + (active_max_dist - cfg.min_lock_distance_pixels) * (duration / 60.0);
            } else {
                current_fov_radius = active_max_dist;
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

        if (is_aiming) {
            cv::Point target_point(
                target->box.x + target->box.width / 2,
                target->box.y + static_cast<int>(target->box.height * cfg.target_y_ratio)
            );
            double dx_pixels = target_point.x - crop_center.x;
            double dy_pixels = target_point.y - crop_center.y;
            double corrected_dx = dx_pixels * mouse_correction_factor_x;
            double corrected_dy = dy_pixels * mouse_correction_factor_y;

            mouse.MoveSmooth(corrected_dx, corrected_dy, dt, cfg.aim_speed, cfg.aim_smoothing);
        } else {
            mouse.pid.reset();
        }
    }"""
code = code.replace(old_handle_mouse, new_handle_mouse)

# Add binding_target member variable to AimAssistant
code = code.replace("bool is_visualizing;", "bool is_visualizing;\\n    int binding_target = 0;")

with open('/workspace/main_patched.cpp', 'w', encoding='utf-8') as f:
    f.write(code)

