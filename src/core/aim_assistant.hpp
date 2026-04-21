#pragma once
#include "config.hpp"
#include "tracker.hpp"
#include "detection.hpp"
#include "../capture/screen_capturer.hpp"
#include "../input/mouse_controller.hpp"
#include "../ui/control_panel.hpp"

namespace auto_aim {

class AimAssistant {
public:
    explicit AimAssistant(const Config& cfg = Config());
    ~AimAssistant();
    
    void run();
    void stop() { running_ = false; }
    
    const Config& config() const { return cfg_; }
    Config& config() { return cfg_; }

private:
    void processFrame();
    TrackedObject* findBestTarget(int active_max_dist);
    void handleMouseInput(TrackedObject* target, double dt, bool is_aiming, 
                          int active_max_dist, double aim_x_ratio, double aim_y_ratio);
    void updateStats();
    
    Config cfg_;
    ScreenCapturer capturer_;
    std::unique_ptr<ObjectDetector> detector_;
    MouseController mouse_;
    Tracker tracker_;
    ControlPanel ui_;
    
    cv::Rect crop_region_;
    cv::Point crop_center_;
    cv::Mat captured_frame_;
    std::vector<Detection> detections_;
    TimingDetails timings_;
    
    bool running_ = true;
    double current_fov_radius_;
    bool is_target_lost_ = false;
    std::chrono::high_resolution_clock::time_point target_lost_time_;
    int last_target_id_ = -1;
    int transfer_pending_id_ = -1;
    std::chrono::high_resolution_clock::time_point target_transfer_time_;
    
    double smoothed_total_ = 0.0;
    bool is_first_frame_ = true;
    int frame_count_ = 0;
};

} // namespace auto_aim
