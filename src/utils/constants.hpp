#pragma once

namespace auto_aim {
namespace constants {

// Tracking constants
constexpr double TRACKING_DISTANCE_THRESHOLD = 150.0;
constexpr int MAX_LOST_FRAMES = 30;
constexpr int TARGET_TRANSFER_DELAY_MS = 20;
constexpr int FOV_SHRINK_DELAY_MS = 60;

// PID controller defaults
constexpr double PID_KP_BASE = 0.6;
constexpr double PID_KI_BASE = 0.0;
constexpr double PID_KD_BASE = 0.1;

// Fuzzy PID thresholds
constexpr double FUZZY_KP_THRESHOLD_HIGH = 50.0;
constexpr double FUZZY_KP_THRESHOLD_MID = 20.0;
constexpr double FUZZY_KD_THRESHOLD = 10.0;
constexpr double FUZZY_KP_MULTIPLIER_HIGH = 1.5;
constexpr double FUZZY_KP_MULTIPLIER_MID = 1.2;
constexpr double FUZZY_KD_MULTIPLIER = 2.0;

// Screen capture
constexpr int DXGI_TIMEOUT_MS = 16;
constexpr int DEFAULT_CROP_SIZE = 640;

// Kalman filter
constexpr double KF_PROCESS_NOISE = 1e-4;
constexpr double KF_MEASUREMENT_NOISE = 1e-1;
constexpr double KF_ERROR_COV = 0.1;

// UI
constexpr int UI_WIDTH = 520;
constexpr int UI_HEIGHT = 980;

} // namespace constants
} // namespace auto_aim
