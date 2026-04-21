#include "mouse_controller.hpp"
#include <iostream>

namespace auto_aim {

MouseController::MouseController() : pid_() {
    std::cout << "--- Initializing Mouse Simulation Driver... ---" << std::endl;
    initialized_ = loadDll() && initDriver();
}

MouseController::~MouseController() {
    if (h_dll_ && destroy_fn_) {
        destroy_fn_();
        FreeLibrary(h_dll_);
    }
}

bool MouseController::loadDll() {
    h_dll_ = LoadLibrary(L"IbInputSimulator.dll");
    if (!h_dll_) {
        std::cout << "WARNING: Failed to load IbInputSimulator.dll. Mouse moves will be unavailable." << std::endl;
        return false;
    }
    
    init_fn_ = (pIbSendInit)GetProcAddress(h_dll_, "IbSendInit");
    destroy_fn_ = (pIbSendDestroy)GetProcAddress(h_dll_, "IbSendDestroy");
    move_fn_ = (pIbSendMouseMove)GetProcAddress(h_dll_, "IbSendMouseMove");
    
    return init_fn_ && destroy_fn_ && move_fn_;
}

bool MouseController::initDriver() {
    if (!init_fn_) return false;
    
    Send::Error error = init_fn_(Send::SendType::Razer, Send::InitFlags{}, nullptr);
    if (error != Send::Error::Success) {
        std::cout << "WARNING: Mouse driver init failed (code=" << static_cast<int>(error) << "). Run as Admin." << std::endl;
        last_error_ = error;
        return false;
    }
    return true;
}

void MouseController::moveRelative(int dx, int dy) {
    if (h_dll_ && move_fn_) {
        move_fn_(static_cast<uint32_t>(dx), static_cast<uint32_t>(dy), Send::MoveMode::Relative);
    }
}

void MouseController::moveSmooth(double target_x, double target_y, double dt, double aim_speed, double aim_smoothing) {
    double out_x, out_y;
    pid_.compute(target_x, target_y, dt, out_x, out_y);
    out_x = (out_x * aim_speed) / aim_smoothing;
    out_y = (out_y * aim_speed) / aim_smoothing;
    moveRelative(static_cast<int>(out_x), static_cast<int>(out_y));
}

}
