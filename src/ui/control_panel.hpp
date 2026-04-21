#pragma once
#include <opencv2/opencv.hpp>
#include "core/config.hpp"
#include "utils/key_names.hpp"

namespace auto_aim {

class ControlPanel {
public:
    explicit ControlPanel(const std::string& window_name = "Control Panel");
    ~ControlPanel();
    
    bool render(Config& cfg);
    
    int bindingTarget() const { return binding_target_; }
    void setBindingTarget(int target) { binding_target_ = target; }
    
    bool isVisualizationEnabled() const { return enable_visualization_; }
    void toggleVisualization() { enable_visualization_ = !enable_visualization_; }

private:
    void renderKeyBindings(Config& cfg);
    void renderAimSettings(Config& cfg);
    void renderGameSettings(Config& cfg);
    void renderModelSettings(Config& cfg);
    
    std::string window_name_;
    int binding_target_ = 0;
    bool enable_visualization_ = true;
};

} // namespace auto_aim
